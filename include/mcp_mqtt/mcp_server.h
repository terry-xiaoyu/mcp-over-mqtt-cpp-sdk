#ifndef MCP_MQTT_SERVER_H
#define MCP_MQTT_SERVER_H

#include <string>
#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include "types.h"
#include "json_rpc.h"
#include "mqtt_interface.h"
#include "tool_manager.h"

namespace mcp_mqtt {

/**
 * @brief MCP over MQTT Server SDK main class.
 *
 * This class provides the MCP server-side implementation of the MCP over MQTT
 * protocol. It handles:
 * - Service discovery (online/offline notifications)
 * - Initialization handshake with clients
 * - Ping/pong health checks
 * - Tool registration and invocation
 * - Shutdown procedures
 *
 * IMPORTANT: This SDK does NOT manage the MQTT connection itself. Users must:
 * 1. Create and configure their own MQTT client using any MQTT library
 * 2. Implement the IMqttClient interface
 * 3. Pass the implementation to this SDK
 *
 * The SDK only handles $mcp-* topics internally. Users have full control
 * over the MQTT client for any other purposes.
 */
class McpServer {
public:
    /**
     * @brief Callback for client connection events
     * @param clientId The MCP client ID
     * @param clientInfo Client information
     */
    using ClientConnectedCallback = std::function<void(const std::string& clientId,
                                                        const ClientInfo& clientInfo)>;

    /**
     * @brief Callback for client disconnection events
     * @param clientId The MCP client ID
     */
    using ClientDisconnectedCallback = std::function<void(const std::string& clientId)>;

    McpServer();
    ~McpServer();

    /**
     * @brief Configure the server
     * @param serverInfo Server information (name and version)
     * @param capabilities Server capabilities
     */
    void configure(const ServerInfo& serverInfo, const ServerCapabilities& capabilities = {});

    /**
     * @brief Set the server description for service discovery
     * @param description Brief description of server functionality
     * @param meta Optional metadata
     */
    void setServiceDescription(const std::string& description,
                                const std::optional<nlohmann::json>& meta = std::nullopt);

    /**
     * @brief Start the MCP server with an external MQTT client
     *
     * The MQTT client must already be connected to the broker.
     * The SDK will:
     * - Subscribe to the control topic
     * - Publish the presence (server online) notification
     * - Register a message handler to process MCP messages
     *
     * @param mqttClient Pointer to user's MQTT client implementation (must outlive McpServer)
     * @param config MCP server configuration (serverId, serverName)
     * @return true if server started successfully
     */
    bool start(IMqttClient* mqttClient, const McpServerConfig& config);

    /**
     * @brief Stop the MCP server
     *
     * This will:
     * - Send disconnected notifications to all connected clients
     * - Clear the presence (publish empty retained message)
     * - Unsubscribe from MCP topics
     *
     * Note: This does NOT disconnect the MQTT client. Users manage the
     * MQTT connection lifecycle separately.
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const;

    // Tool management

    /**
     * @brief Register a tool
     * @param tool Tool definition
     * @param handler Handler function
     * @return true if registered successfully
     */
    bool registerTool(const Tool& tool, ToolHandler handler);

    /**
     * @brief Unregister a tool
     * @param name Tool name
     */
    void unregisterTool(const std::string& name);

    /**
     * @brief Get registered tools
     */
    std::vector<Tool> getTools() const;

    // Callbacks

    /**
     * @brief Set callback for client connection events
     */
    void setClientConnectedCallback(ClientConnectedCallback callback);

    /**
     * @brief Set callback for client disconnection events
     */
    void setClientDisconnectedCallback(ClientDisconnectedCallback callback);

    // Getters

    /**
     * @brief Get server ID
     */
    const std::string& getServerId() const;

    /**
     * @brief Get server name
     */
    const std::string& getServerName() const;

    /**
     * @brief Get connected client sessions
     */
    std::vector<std::string> getConnectedClients() const;

private:
    IMqttClient* mqttClient_ = nullptr;  // Non-owning pointer to user's MQTT client
    ServerInfo serverInfo_;
    ServerCapabilities capabilities_;
    ServerOnlineParams onlineParams_;

    std::atomic<bool> running_{false};
    std::string serverId_;
    std::string serverName_;

    ToolManager toolManager_;

    mutable std::mutex sessionsMutex_;
    std::map<std::string, ClientSession> clientSessions_;

    ClientConnectedCallback clientConnectedCallback_;
    ClientDisconnectedCallback clientDisconnectedCallback_;

    // Internal methods
    void publishPresence();
    void clearPresence();
    void setupSubscriptions();
    void cleanupSubscriptions();

    // Message routing - called for ALL incoming messages, filters MCP topics
    void handleIncomingMessage(const MqttIncomingMessage& message);

    // MCP message handlers
    void handleControlMessage(const std::string& topic, const std::string& payload,
                               const std::map<std::string, std::string>& userProps);
    void handleRpcMessage(const std::string& topic, const std::string& payload);
    void handleClientPresence(const std::string& topic, const std::string& payload);

    // Request handlers
    void handleInitialize(const std::string& mcpClientId, const JsonRpcRequest& request);
    void handleInitializedNotification(const std::string& mcpClientId);
    void handlePing(const std::string& mcpClientId, const JsonRpcRequest& request);
    void handleToolsList(const std::string& mcpClientId, const JsonRpcRequest& request);
    void handleToolsCall(const std::string& mcpClientId, const JsonRpcRequest& request);
    void handleDisconnectedNotification(const std::string& mcpClientId);

    // Topic helpers
    std::string getControlTopic() const;
    std::string getPresenceTopic() const;
    std::string getRpcTopic(const std::string& mcpClientId) const;
    std::string getClientPresenceTopic(const std::string& mcpClientId) const;

    // Parse client ID from topic
    std::optional<std::string> parseClientIdFromRpcTopic(const std::string& topic) const;
    std::optional<std::string> parseClientIdFromPresenceTopic(const std::string& topic) const;

    // Check if topic is MCP-related
    bool isMcpTopic(const std::string& topic) const;

    // Send response
    void sendResponse(const std::string& mcpClientId, const JsonRpcResponse& response);
    void sendNotification(const std::string& mcpClientId, const JsonRpcNotification& notification);

    // Cleanup client session
    void cleanupClientSession(const std::string& mcpClientId);
};

} // namespace mcp_mqtt

#endif // MCP_MQTT_SERVER_H
