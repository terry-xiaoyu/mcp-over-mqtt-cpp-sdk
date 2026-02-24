#include "mcp_mqtt/mcp_server.h"
#include <iostream>

namespace mcp_mqtt {

// MCP topic prefixes
static constexpr const char* MCP_SERVER_PREFIX = "$mcp-server/";
static constexpr const char* MCP_CLIENT_PREFIX = "$mcp-client/";
static constexpr const char* MCP_RPC_PREFIX = "$mcp-rpc/";

McpServer::McpServer() = default;

McpServer::~McpServer() {
    if (running_) {
        stop();
    }
}

void McpServer::configure(const ServerInfo& serverInfo, const ServerCapabilities& capabilities) {
    serverInfo_ = serverInfo;
    capabilities_ = capabilities;
}

void McpServer::setServiceDescription(const std::string& description,
                                       const std::optional<nlohmann::json>& meta) {
    onlineParams_.description = description;
    onlineParams_.meta = meta;
}

bool McpServer::start(IMqttClient* mqttClient, const McpServerConfig& config) {
    if (running_) {
        return false;
    }

    if (!mqttClient || !mqttClient->isConnected()) {
        std::cerr << "MQTT client is not connected" << std::endl;
        return false;
    }

    mqttClient_ = mqttClient;
    serverId_ = config.serverId;
    serverName_ = config.serverName;

    // Register our message handler - SDK will filter MCP topics
    mqttClient_->setMessageHandler([this](const MqttIncomingMessage& msg) {
        handleIncomingMessage(msg);
    });

    // Set connection lost callback
    mqttClient_->setConnectionLostCallback([this](const std::string& reason) {
        std::cerr << "MCP Server: MQTT connection lost: " << reason << std::endl;
        running_ = false;
    });

    // Setup MCP subscriptions
    setupSubscriptions();

    // Publish presence (server online notification)
    publishPresence();

    running_ = true;
    return true;
}

void McpServer::stop() {
    if (!running_) {
        return;
    }

    // Send disconnected notifications to all connected clients
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (const auto& [clientId, session] : clientSessions_) {
            auto notif = JsonRpcNotification::create("notifications/disconnected");
            sendNotification(clientId, notif);
        }
        clientSessions_.clear();
    }

    // Clear presence
    clearPresence();

    // Cleanup subscriptions
    cleanupSubscriptions();

    running_ = false;
    mqttClient_ = nullptr;
}

bool McpServer::isRunning() const {
    return running_ && mqttClient_ && mqttClient_->isConnected();
}

bool McpServer::registerTool(const Tool& tool, ToolHandler handler) {
    return toolManager_.registerTool(tool, handler);
}

void McpServer::unregisterTool(const std::string& name) {
    toolManager_.unregisterTool(name);
}

std::vector<Tool> McpServer::getTools() const {
    return toolManager_.getTools();
}

void McpServer::setClientConnectedCallback(ClientConnectedCallback callback) {
    clientConnectedCallback_ = callback;
}

void McpServer::setClientDisconnectedCallback(ClientDisconnectedCallback callback) {
    clientDisconnectedCallback_ = callback;
}

const std::string& McpServer::getServerId() const {
    return serverId_;
}

const std::string& McpServer::getServerName() const {
    return serverName_;
}

std::vector<std::string> McpServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    std::vector<std::string> clients;
    clients.reserve(clientSessions_.size());
    for (const auto& [clientId, session] : clientSessions_) {
        clients.push_back(clientId);
    }
    return clients;
}

// Internal methods

void McpServer::publishPresence() {
    std::string topic = getPresenceTopic();

    auto notif = JsonRpcNotification::create("notifications/server/online", onlineParams_.toJson());
    std::string payload = JsonRpc::serialize(notif.toJson());

    // Publish with retain flag
    std::map<std::string, std::string> props = {
        {USER_PROP_COMPONENT_TYPE, COMPONENT_TYPE_SERVER},
        {USER_PROP_MQTT_CLIENT_ID, serverId_}
    };
    mqttClient_->publish(topic, payload, 1, true, props);
}

void McpServer::clearPresence() {
    if (!mqttClient_) return;

    std::string topic = getPresenceTopic();
    // Publish empty retained message to clear presence
    mqttClient_->publish(topic, "", 1, true, {});
}

void McpServer::setupSubscriptions() {
    // Subscribe to control topic
    std::string controlTopic = getControlTopic();
    mqttClient_->subscribe(controlTopic, 1, false);
}

void McpServer::cleanupSubscriptions() {
    if (!mqttClient_) return;

    // Unsubscribe from control topic
    std::string controlTopic = getControlTopic();
    mqttClient_->unsubscribe(controlTopic);

    // Unsubscribe from all client RPC and presence topics
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (const auto& [clientId, session] : clientSessions_) {
        mqttClient_->unsubscribe(getRpcTopic(clientId));
        mqttClient_->unsubscribe(getClientPresenceTopic(clientId));
    }
}

bool McpServer::isMcpTopic(const std::string& topic) const {
    return topic.rfind(MCP_SERVER_PREFIX, 0) == 0 ||
           topic.rfind(MCP_CLIENT_PREFIX, 0) == 0 ||
           topic.rfind(MCP_RPC_PREFIX, 0) == 0;
}

void McpServer::handleIncomingMessage(const MqttIncomingMessage& message) {
    // Only process MCP-related topics, ignore others
    if (!isMcpTopic(message.topic)) {
        return;
    }

    const std::string& topic = message.topic;
    const std::string& payload = message.payload;

    // Route to appropriate handler based on topic prefix
    if (topic.rfind(MCP_RPC_PREFIX, 0) == 0) {
        // RPC message
        handleRpcMessage(topic, payload);
    } else if (topic == getControlTopic()) {
        // Control message (e.g., initialize request)
        handleControlMessage(topic, payload, message.userProperties);
    } else if (topic.rfind("$mcp-client/presence/", 0) == 0) {
        // Client presence message
        handleClientPresence(topic, payload);
    }
}

void McpServer::handleControlMessage(const std::string& topic, const std::string& payload,
                                      const std::map<std::string, std::string>& userProps) {
    auto jsonOpt = JsonRpc::parse(payload);
    if (!jsonOpt) {
        std::cerr << "Failed to parse control message" << std::endl;
        return;
    }

    auto reqOpt = JsonRpcRequest::fromJson(*jsonOpt);
    if (!reqOpt) {
        std::cerr << "Invalid JSON-RPC request" << std::endl;
        return;
    }

    const auto& request = *reqOpt;

    // Extract client ID from user properties
    std::string mcpClientId;
    auto it = userProps.find(USER_PROP_MQTT_CLIENT_ID);
    if (it != userProps.end()) {
        mcpClientId = it->second;
    }

    // If not in user properties, try to get from params (extension)
    if (mcpClientId.empty() && request.params && request.params->contains("mcpClientId")) {
        mcpClientId = (*request.params)["mcpClientId"];
    }

    if (request.method == "initialize" && !mcpClientId.empty()) {
        handleInitialize(mcpClientId, request);
    }
}

void McpServer::handleRpcMessage(const std::string& topic, const std::string& payload) {
    // Parse client ID from topic
    auto clientIdOpt = parseClientIdFromRpcTopic(topic);
    if (!clientIdOpt) {
        return;
    }
    std::string mcpClientId = *clientIdOpt;

    // Handle empty payload (shouldn't happen on RPC topic)
    if (payload.empty()) {
        return;
    }

    auto jsonOpt = JsonRpc::parse(payload);
    if (!jsonOpt) {
        return;
    }

    // Check if it's a notification
    if (jsonOpt->contains("method") && !jsonOpt->contains("id")) {
        std::string method = (*jsonOpt)["method"];

        if (method == "notifications/initialized") {
            handleInitializedNotification(mcpClientId);
        } else if (method == "notifications/disconnected") {
            handleDisconnectedNotification(mcpClientId);
        }
        return;
    }

    // Parse as request
    auto reqOpt = JsonRpcRequest::fromJson(*jsonOpt);
    if (!reqOpt) {
        return;
    }

    const auto& request = *reqOpt;

    if (request.method == "ping") {
        handlePing(mcpClientId, request);
    } else if (request.method == "tools/list") {
        handleToolsList(mcpClientId, request);
    } else if (request.method == "tools/call") {
        handleToolsCall(mcpClientId, request);
    } else {
        // Method not found
        auto response = JsonRpcResponse::errorResponse(
            request.id, JsonRpcError::METHOD_NOT_FOUND,
            "Method not found: " + request.method);
        sendResponse(mcpClientId, response);
    }
}

void McpServer::handleClientPresence(const std::string& topic, const std::string& payload) {
    auto clientIdOpt = parseClientIdFromPresenceTopic(topic);
    if (!clientIdOpt) {
        return;
    }
    std::string mcpClientId = *clientIdOpt;

    // Check if it's a disconnected notification
    if (!payload.empty()) {
        auto jsonOpt = JsonRpc::parse(payload);
        if (jsonOpt && jsonOpt->contains("method")) {
            std::string method = (*jsonOpt)["method"];
            if (method == "notifications/disconnected") {
                handleDisconnectedNotification(mcpClientId);
            }
        }
    }
}

void McpServer::handleInitialize(const std::string& mcpClientId, const JsonRpcRequest& request) {
    // Validate protocol version
    std::string requestedVersion = MCP_PROTOCOL_VERSION;
    if (request.params && request.params->contains("protocolVersion")) {
        requestedVersion = (*request.params)["protocolVersion"];
    }

    // Create client session
    ClientSession session;
    session.mcpClientId = mcpClientId;
    session.protocolVersion = requestedVersion;

    if (request.params) {
        if (request.params->contains("clientInfo")) {
            auto ci = (*request.params)["clientInfo"];
            session.clientInfo.name = ci.value("name", "");
            session.clientInfo.version = ci.value("version", "");
        }
        if (request.params->contains("capabilities")) {
            session.capabilities = (*request.params)["capabilities"];
        }
    }

    // Subscribe to RPC topic for this client (with No Local option)
    std::string rpcTopic = getRpcTopic(mcpClientId);
    mqttClient_->subscribe(rpcTopic, 1, true);

    // Subscribe to client's presence topic
    std::string clientPresenceTopic = getClientPresenceTopic(mcpClientId);
    mqttClient_->subscribe(clientPresenceTopic, 1, false);

    // Store session
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        clientSessions_[mcpClientId] = session;
    }

    // Build initialize response
    nlohmann::json result;
    result["protocolVersion"] = MCP_PROTOCOL_VERSION;
    result["capabilities"] = capabilities_.toJson();
    result["serverInfo"] = {
        {"name", serverInfo_.name},
        {"version", serverInfo_.version}
    };

    auto response = JsonRpcResponse::success(request.id, result);
    sendResponse(mcpClientId, response);
}

void McpServer::handleInitializedNotification(const std::string& mcpClientId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = clientSessions_.find(mcpClientId);
    if (it != clientSessions_.end()) {
        it->second.initialized = true;

        // Notify callback
        if (clientConnectedCallback_) {
            clientConnectedCallback_(mcpClientId, it->second.clientInfo);
        }
    }
}

void McpServer::handlePing(const std::string& mcpClientId, const JsonRpcRequest& request) {
    // Respond with empty result
    auto response = JsonRpcResponse::success(request.id, nlohmann::json::object());
    sendResponse(mcpClientId, response);
}

void McpServer::handleToolsList(const std::string& mcpClientId, const JsonRpcRequest& request) {
    nlohmann::json result;
    result["tools"] = toolManager_.getToolsJson();

    auto response = JsonRpcResponse::success(request.id, result);
    sendResponse(mcpClientId, response);
}

void McpServer::handleToolsCall(const std::string& mcpClientId, const JsonRpcRequest& request) {
    if (!request.params || !request.params->contains("name")) {
        auto response = JsonRpcResponse::errorResponse(
            request.id, JsonRpcError::INVALID_PARAMS,
            "Missing 'name' parameter");
        sendResponse(mcpClientId, response);
        return;
    }

    std::string toolName = (*request.params)["name"];
    nlohmann::json arguments = request.params->value("arguments", nlohmann::json::object());

    // Call the tool
    ToolCallResult result = toolManager_.callTool(toolName, arguments);

    auto response = JsonRpcResponse::success(request.id, result.toJson());
    sendResponse(mcpClientId, response);
}

void McpServer::handleDisconnectedNotification(const std::string& mcpClientId) {
    cleanupClientSession(mcpClientId);
}

std::string McpServer::getControlTopic() const {
    return "$mcp-server/" + serverId_ + "/" + serverName_;
}

std::string McpServer::getPresenceTopic() const {
    return "$mcp-server/presence/" + serverId_ + "/" + serverName_;
}

std::string McpServer::getRpcTopic(const std::string& mcpClientId) const {
    return "$mcp-rpc/" + mcpClientId + "/" + serverId_ + "/" + serverName_;
}

std::string McpServer::getClientPresenceTopic(const std::string& mcpClientId) const {
    return "$mcp-client/presence/" + mcpClientId;
}

std::optional<std::string> McpServer::parseClientIdFromRpcTopic(const std::string& topic) const {
    // Topic format: $mcp-rpc/{mcp-client-id}/{server-id}/{server-name}
    std::string prefix = "$mcp-rpc/";
    if (topic.find(prefix) != 0) {
        return std::nullopt;
    }

    std::string rest = topic.substr(prefix.length());
    size_t firstSlash = rest.find('/');
    if (firstSlash == std::string::npos) {
        return std::nullopt;
    }

    return rest.substr(0, firstSlash);
}

std::optional<std::string> McpServer::parseClientIdFromPresenceTopic(const std::string& topic) const {
    // Topic format: $mcp-client/presence/{mcp-client-id}
    std::string prefix = "$mcp-client/presence/";
    if (topic.find(prefix) != 0) {
        return std::nullopt;
    }

    return topic.substr(prefix.length());
}

void McpServer::sendResponse(const std::string& mcpClientId, const JsonRpcResponse& response) {
    std::string topic = getRpcTopic(mcpClientId);
    std::string payload = JsonRpc::serialize(response.toJson());

    std::map<std::string, std::string> props = {
        {USER_PROP_COMPONENT_TYPE, COMPONENT_TYPE_SERVER},
        {USER_PROP_MQTT_CLIENT_ID, serverId_}
    };
    mqttClient_->publish(topic, payload, 1, false, props);
}

void McpServer::sendNotification(const std::string& mcpClientId, const JsonRpcNotification& notification) {
    std::string topic = getRpcTopic(mcpClientId);
    std::string payload = JsonRpc::serialize(notification.toJson());

    std::map<std::string, std::string> props = {
        {USER_PROP_COMPONENT_TYPE, COMPONENT_TYPE_SERVER},
        {USER_PROP_MQTT_CLIENT_ID, serverId_}
    };
    mqttClient_->publish(topic, payload, 1, false, props);
}

void McpServer::cleanupClientSession(const std::string& mcpClientId) {
    ClientInfo clientInfo;

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = clientSessions_.find(mcpClientId);
        if (it != clientSessions_.end()) {
            clientInfo = it->second.clientInfo;
            clientSessions_.erase(it);
        } else {
            return;
        }
    }

    // Unsubscribe from client's topics
    std::string rpcTopic = getRpcTopic(mcpClientId);
    std::string presenceTopic = getClientPresenceTopic(mcpClientId);

    mqttClient_->unsubscribe(rpcTopic);
    mqttClient_->unsubscribe(presenceTopic);

    // Notify callback
    if (clientDisconnectedCallback_) {
        clientDisconnectedCallback_(mcpClientId);
    }
}

} // namespace mcp_mqtt
