#include "mcp_mqtt/mcp_server.h"
#include "mcp_mqtt/logger.h"

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
    MCP_LOG_INFO("Server configured: name=" << serverInfo_.name << ", version=" << serverInfo_.version);
}

void McpServer::setServiceDescription(const std::string& description,
                                       const std::optional<nlohmann::json>& meta) {
    onlineParams_.description = description;
    onlineParams_.meta = meta;
    MCP_LOG_DEBUG("Service description set: " << description);
}

bool McpServer::start(IMqttClient* mqttClient, const McpServerConfig& config) {
    if (running_) {
        MCP_LOG_WARN("Server already running, ignoring start()");
        return false;
    }

    if (!mqttClient || !mqttClient->isConnected()) {
        MCP_LOG_ERROR("MQTT client is not connected");
        return false;
    }

    mqttClient_ = mqttClient;
    serverId_ = config.serverId;
    serverName_ = config.serverName;

    MCP_LOG_INFO("Starting MCP server: serverId=" << serverId_ << ", serverName=" << serverName_);

    // Register our message handler - SDK will filter MCP topics
    mqttClient_->setMessageHandler([this](const MqttIncomingMessage& msg) {
        handleIncomingMessage(msg);
    });

    // Set connection lost callback
    mqttClient_->setConnectionLostCallback([this](const std::string& reason) {
        MCP_LOG_ERROR("MQTT connection lost: " << reason);
        running_ = false;
    });

    // Setup MCP subscriptions
    setupSubscriptions();

    // Publish presence (server online notification)
    publishPresence();

    running_ = true;
    MCP_LOG_INFO("MCP server started successfully");
    return true;
}

void McpServer::stop() {
    if (!running_) {
        return;
    }

    MCP_LOG_INFO("Stopping MCP server...");

    // Send disconnected notifications to all connected clients
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (const auto& [clientId, session] : clientSessions_) {
            MCP_LOG_DEBUG("Sending disconnect notification to client: " << clientId);
            auto notif = JsonRpcNotification::create("notifications/disconnected");
            sendNotification(clientId, notif);
        }
        MCP_LOG_INFO("Cleared " << clientSessions_.size() << " client session(s)");
        clientSessions_.clear();
    }

    // Clear presence
    clearPresence();

    // Cleanup subscriptions
    cleanupSubscriptions();

    running_ = false;
    mqttClient_ = nullptr;
    MCP_LOG_INFO("MCP server stopped");
}

bool McpServer::isRunning() const {
    return running_ && mqttClient_ && mqttClient_->isConnected();
}

bool McpServer::registerTool(const Tool& tool, ToolHandler handler) {
    bool ok = toolManager_.registerTool(tool, handler);
    if (ok) {
        MCP_LOG_INFO("Tool registered: " << tool.name);
    } else {
        MCP_LOG_WARN("Failed to register tool (already exists?): " << tool.name);
    }
    return ok;
}

void McpServer::unregisterTool(const std::string& name) {
    toolManager_.unregisterTool(name);
    MCP_LOG_INFO("Tool unregistered: " << name);
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
    MCP_LOG_DEBUG("Published presence on topic: " << topic);
}

void McpServer::clearPresence() {
    if (!mqttClient_) return;

    std::string topic = getPresenceTopic();
    // Publish empty retained message to clear presence
    mqttClient_->publish(topic, "", 1, true, {});
    MCP_LOG_DEBUG("Cleared presence on topic: " << topic);
}

void McpServer::setupSubscriptions() {
    // Subscribe to control topic
    std::string controlTopic = getControlTopic();
    mqttClient_->subscribe(controlTopic, 1, false);
    MCP_LOG_DEBUG("Subscribed to control topic: " << controlTopic);
}

void McpServer::cleanupSubscriptions() {
    if (!mqttClient_) return;

    // Unsubscribe from control topic
    std::string controlTopic = getControlTopic();
    mqttClient_->unsubscribe(controlTopic);
    MCP_LOG_DEBUG("Unsubscribed from control topic: " << controlTopic);

    // Unsubscribe from all client RPC and presence topics
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (const auto& [clientId, session] : clientSessions_) {
        mqttClient_->unsubscribe(getRpcTopic(clientId));
        mqttClient_->unsubscribe(getClientPresenceTopic(clientId));
        MCP_LOG_DEBUG("Unsubscribed from client topics: clientId=" << clientId);
    }
}

bool McpServer::isMcpTopic(const std::string& topic) const {
    return topic.rfind(MCP_SERVER_PREFIX, 0) == 0 ||
           topic.rfind(MCP_CLIENT_PREFIX, 0) == 0 ||
           topic.rfind(MCP_RPC_PREFIX, 0) == 0;
}

void McpServer::handleIncomingMessage(const MqttIncomingMessage& message) {
    MCP_LOG_DEBUG("Received MQTT message: topic=" << message.topic
              << ", payload=" << message.payload
              << ", qos=" << message.qos
              << ", retained=" << message.retained);

    // Only process MCP-related topics, ignore others
    if (!isMcpTopic(message.topic)) {
        MCP_LOG_DEBUG("Ignoring non-MCP topic: " << message.topic);
        return;
    }

    const std::string& topic = message.topic;
    const std::string& payload = message.payload;

    // Route to appropriate handler based on topic prefix
    if (topic.rfind(MCP_RPC_PREFIX, 0) == 0) {
        // RPC message
        MCP_LOG_DEBUG("Routing to RPC handler: " << topic);
        handleRpcMessage(topic, payload);
    } else if (topic == getControlTopic()) {
        // Control message (e.g., initialize request)
        MCP_LOG_DEBUG("Routing to control handler: " << topic);
        handleControlMessage(topic, payload, message.userProperties);
    } else if (topic.rfind("$mcp-client/presence/", 0) == 0) {
        // Client presence message
        MCP_LOG_DEBUG("Routing to client presence handler: " << topic);
        handleClientPresence(topic, payload);
    } else {
        MCP_LOG_DEBUG("Unhandled MCP topic: " << topic);
    }
}

void McpServer::handleControlMessage(const std::string& topic, const std::string& payload,
                                      const std::map<std::string, std::string>& userProps) {
    MCP_LOG_DEBUG("Handling control message: topic=" << topic << ", payload=" << payload);

    auto jsonOpt = JsonRpc::parse(payload);
    if (!jsonOpt) {
        MCP_LOG_ERROR("Failed to parse control message JSON: " << payload);
        return;
    }

    auto reqOpt = JsonRpcRequest::fromJson(*jsonOpt);
    if (!reqOpt) {
        MCP_LOG_ERROR("Invalid JSON-RPC request in control message");
        return;
    }

    const auto& request = *reqOpt;
    MCP_LOG_DEBUG("Control request: method=" << request.method);

    // Extract client ID from user properties
    std::string mcpClientId;
    auto it = userProps.find(USER_PROP_MQTT_CLIENT_ID);
    if (it != userProps.end()) {
        mcpClientId = it->second;
        MCP_LOG_DEBUG("Client ID from user properties: " << mcpClientId);
    }

    // If not in user properties, try to get from params (extension)
    if (mcpClientId.empty() && request.params && request.params->contains("mcpClientId")) {
        mcpClientId = (*request.params)["mcpClientId"];
        MCP_LOG_DEBUG("Client ID from params: " << mcpClientId);
    }

    if (request.method == "initialize" && !mcpClientId.empty()) {
        handleInitialize(mcpClientId, request);
    } else {
        MCP_LOG_WARN("Unhandled control method=" << request.method << " or empty clientId");
    }
}

void McpServer::handleRpcMessage(const std::string& topic, const std::string& payload) {
    // Parse client ID from topic
    auto clientIdOpt = parseClientIdFromRpcTopic(topic);
    if (!clientIdOpt) {
        MCP_LOG_WARN("Failed to parse client ID from RPC topic: " << topic);
        return;
    }
    std::string mcpClientId = *clientIdOpt;

    // Handle empty payload (shouldn't happen on RPC topic)
    if (payload.empty()) {
        MCP_LOG_WARN("Empty payload on RPC topic: " << topic);
        return;
    }

    MCP_LOG_DEBUG("RPC message from client=" << mcpClientId << ", payload=" << payload);

    auto jsonOpt = JsonRpc::parse(payload);
    if (!jsonOpt) {
        MCP_LOG_ERROR("Failed to parse RPC message JSON from client=" << mcpClientId);
        return;
    }

    // Check if it's a notification
    if (jsonOpt->contains("method") && !jsonOpt->contains("id")) {
        std::string method = (*jsonOpt)["method"];
        MCP_LOG_DEBUG("RPC notification: method=" << method << ", client=" << mcpClientId);

        if (method == "notifications/initialized") {
            handleInitializedNotification(mcpClientId);
        } else if (method == "notifications/disconnected") {
            handleDisconnectedNotification(mcpClientId);
        } else {
            MCP_LOG_DEBUG("Ignoring unknown notification: " << method);
        }
        return;
    }

    // Parse as request
    auto reqOpt = JsonRpcRequest::fromJson(*jsonOpt);
    if (!reqOpt) {
        MCP_LOG_ERROR("Invalid JSON-RPC request from client=" << mcpClientId);
        return;
    }

    const auto& request = *reqOpt;
    MCP_LOG_DEBUG("RPC request: method=" << request.method << ", client=" << mcpClientId);

    if (request.method == "ping") {
        handlePing(mcpClientId, request);
    } else if (request.method == "tools/list") {
        handleToolsList(mcpClientId, request);
    } else if (request.method == "tools/call") {
        handleToolsCall(mcpClientId, request);
    } else {
        MCP_LOG_WARN("Method not found: " << request.method << ", client=" << mcpClientId);
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
        MCP_LOG_WARN("Failed to parse client ID from presence topic: " << topic);
        return;
    }
    std::string mcpClientId = *clientIdOpt;

    MCP_LOG_DEBUG("Client presence message: client=" << mcpClientId
              << ", payload=" << (payload.empty() ? "(empty)" : payload));

    // Check if it's a disconnected notification
    if (!payload.empty()) {
        auto jsonOpt = JsonRpc::parse(payload);
        if (jsonOpt && jsonOpt->contains("method")) {
            std::string method = (*jsonOpt)["method"];
            if (method == "notifications/disconnected") {
                MCP_LOG_INFO("Client disconnected via presence: " << mcpClientId);
                handleDisconnectedNotification(mcpClientId);
            }
        }
    } else {
        MCP_LOG_DEBUG("Empty presence payload (client offline): " << mcpClientId);
    }
}

void McpServer::handleInitialize(const std::string& mcpClientId, const JsonRpcRequest& request) {
    MCP_LOG_INFO("Initializing client session: " << mcpClientId);

    // Validate protocol version
    std::string requestedVersion = MCP_PROTOCOL_VERSION;
    if (request.params && request.params->contains("protocolVersion")) {
        requestedVersion = (*request.params)["protocolVersion"];
    }
    MCP_LOG_DEBUG("Client requested protocol version: " << requestedVersion);

    // Create client session
    ClientSession session;
    session.mcpClientId = mcpClientId;
    session.protocolVersion = requestedVersion;

    if (request.params) {
        if (request.params->contains("clientInfo")) {
            auto ci = (*request.params)["clientInfo"];
            session.clientInfo.name = ci.value("name", "");
            session.clientInfo.version = ci.value("version", "");
            MCP_LOG_DEBUG("Client info: name=" << session.clientInfo.name
                      << ", version=" << session.clientInfo.version);
        }
        if (request.params->contains("capabilities")) {
            session.capabilities = (*request.params)["capabilities"];
            MCP_LOG_DEBUG("Client capabilities: " << session.capabilities.dump());
        }
    }

    // Subscribe to RPC topic for this client (with No Local option)
    std::string rpcTopic = getRpcTopic(mcpClientId);
    mqttClient_->subscribe(rpcTopic, 1, true);
    MCP_LOG_DEBUG("Subscribed to RPC topic: " << rpcTopic);

    // Subscribe to client's presence topic
    std::string clientPresenceTopic = getClientPresenceTopic(mcpClientId);
    mqttClient_->subscribe(clientPresenceTopic, 1, false);
    MCP_LOG_DEBUG("Subscribed to client presence topic: " << clientPresenceTopic);

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
    MCP_LOG_INFO("Initialize response sent to client: " << mcpClientId);
}

void McpServer::handleInitializedNotification(const std::string& mcpClientId) {
    MCP_LOG_DEBUG("Received initialized notification from client: " << mcpClientId);

    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = clientSessions_.find(mcpClientId);
    if (it != clientSessions_.end()) {
        it->second.initialized = true;
        MCP_LOG_INFO("Client session initialized: " << mcpClientId
                  << " (" << it->second.clientInfo.name << " v" << it->second.clientInfo.version << ")");

        // Notify callback
        if (clientConnectedCallback_) {
            clientConnectedCallback_(mcpClientId, it->second.clientInfo);
        }
    } else {
        MCP_LOG_WARN("Received initialized notification for unknown client: " << mcpClientId);
    }
}

void McpServer::handlePing(const std::string& mcpClientId, const JsonRpcRequest& request) {
    MCP_LOG_DEBUG("Ping from client: " << mcpClientId);
    // Respond with empty result
    auto response = JsonRpcResponse::success(request.id, nlohmann::json::object());
    sendResponse(mcpClientId, response);
}

void McpServer::handleToolsList(const std::string& mcpClientId, const JsonRpcRequest& request) {
    MCP_LOG_DEBUG("Tools list request from client: " << mcpClientId);

    nlohmann::json result;
    result["tools"] = toolManager_.getToolsJson();

    auto response = JsonRpcResponse::success(request.id, result);
    sendResponse(mcpClientId, response);
    MCP_LOG_DEBUG("Sent tools list (" << toolManager_.getTools().size() << " tools) to client: " << mcpClientId);
}

void McpServer::handleToolsCall(const std::string& mcpClientId, const JsonRpcRequest& request) {
    if (!request.params || !request.params->contains("name")) {
        MCP_LOG_ERROR("Tool call missing 'name' parameter, client=" << mcpClientId);
        auto response = JsonRpcResponse::errorResponse(
            request.id, JsonRpcError::INVALID_PARAMS,
            "Missing 'name' parameter");
        sendResponse(mcpClientId, response);
        return;
    }

    std::string toolName = (*request.params)["name"];
    nlohmann::json arguments = request.params->value("arguments", nlohmann::json::object());

    MCP_LOG_INFO("Tool call: tool=" << toolName << ", client=" << mcpClientId);
    MCP_LOG_DEBUG("Tool call arguments: " << arguments.dump());

    // Call the tool
    ToolCallResult result = toolManager_.callTool(toolName, arguments);

    if (result.isError) {
        MCP_LOG_WARN("Tool call failed: tool=" << toolName << ", client=" << mcpClientId);
    } else {
        MCP_LOG_DEBUG("Tool call succeeded: tool=" << toolName);
    }

    auto response = JsonRpcResponse::success(request.id, result.toJson());
    sendResponse(mcpClientId, response);
}

void McpServer::handleDisconnectedNotification(const std::string& mcpClientId) {
    MCP_LOG_INFO("Client disconnected: " << mcpClientId);
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

    MCP_LOG_DEBUG("Sending response to client=" << mcpClientId << ", topic=" << topic
              << ", payload=" << payload);

    std::map<std::string, std::string> props = {
        {USER_PROP_COMPONENT_TYPE, COMPONENT_TYPE_SERVER},
        {USER_PROP_MQTT_CLIENT_ID, serverId_}
    };
    mqttClient_->publish(topic, payload, 1, false, props);
}

void McpServer::sendNotification(const std::string& mcpClientId, const JsonRpcNotification& notification) {
    std::string topic = getRpcTopic(mcpClientId);
    std::string payload = JsonRpc::serialize(notification.toJson());

    MCP_LOG_DEBUG("Sending notification to client=" << mcpClientId << ", topic=" << topic
              << ", payload=" << payload);

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
            MCP_LOG_DEBUG("Removed client session: " << mcpClientId);
        } else {
            MCP_LOG_DEBUG("Client session not found for cleanup: " << mcpClientId);
            return;
        }
    }

    // Unsubscribe from client's topics
    std::string rpcTopic = getRpcTopic(mcpClientId);
    std::string presenceTopic = getClientPresenceTopic(mcpClientId);

    mqttClient_->unsubscribe(rpcTopic);
    mqttClient_->unsubscribe(presenceTopic);
    MCP_LOG_DEBUG("Unsubscribed from client topics: rpc=" << rpcTopic << ", presence=" << presenceTopic);

    // Notify callback
    if (clientDisconnectedCallback_) {
        clientDisconnectedCallback_(mcpClientId);
    }
}

} // namespace mcp_mqtt
