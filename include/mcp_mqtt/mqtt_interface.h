#ifndef MCP_MQTT_INTERFACE_H
#define MCP_MQTT_INTERFACE_H

#include <string>
#include <map>
#include <functional>
#include <optional>

namespace mcp_mqtt {

/**
 * @brief MQTT message structure for incoming messages
 */
struct MqttIncomingMessage {
    std::string topic;
    std::string payload;
    int qos = 0;
    bool retained = false;
    std::map<std::string, std::string> userProperties;
};

/**
 * @brief Callback type for incoming MQTT messages
 */
using MqttMessageHandler = std::function<void(const MqttIncomingMessage& message)>;

/**
 * @brief Interface that users must implement to provide MQTT functionality.
 *
 * Users should implement this interface using their preferred MQTT library
 * (e.g., Paho MQTT C++, mosquitto, etc.) and pass it to McpServer.
 *
 * The MCP SDK will use this interface to:
 * - Subscribe to MCP-related topics ($mcp-*)
 * - Publish MCP messages
 * - Set up will messages for presence
 *
 * Users retain full control of the MQTT client and can use it for
 * any non-MCP purposes as well.
 */
class IMqttClient {
public:
    virtual ~IMqttClient() = default;

    /**
     * @brief Check if the MQTT client is connected
     * @return true if connected to the broker
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Subscribe to a topic
     * @param topic Topic filter to subscribe
     * @param qos QoS level (0, 1, or 2)
     * @param noLocal Set No Local subscription option (MQTT 5.0) - prevents receiving own messages
     * @return true if subscription was successful
     */
    virtual bool subscribe(const std::string& topic, int qos, bool noLocal) = 0;

    /**
     * @brief Unsubscribe from a topic
     * @param topic Topic filter to unsubscribe
     * @return true if unsubscription was successful
     */
    virtual bool unsubscribe(const std::string& topic) = 0;

    /**
     * @brief Publish a message
     * @param topic Topic to publish to
     * @param payload Message payload
     * @param qos QoS level (0, 1, or 2)
     * @param retained Whether to retain the message
     * @param userProps User properties to include (MQTT 5.0)
     * @return true if publish was successful
     */
    virtual bool publish(const std::string& topic,
                         const std::string& payload,
                         int qos,
                         bool retained,
                         const std::map<std::string, std::string>& userProps = {}) = 0;

    /**
     * @brief Get the client ID used for this MQTT connection
     * @return The MQTT client ID
     */
    virtual std::string getClientId() const = 0;

    /**
     * @brief Set the message handler for incoming messages
     *
     * The MCP SDK will call this to register its message handler.
     * The implementation should route incoming messages to this handler.
     *
     * IMPORTANT: The implementation should call this handler for ALL incoming
     * messages. The MCP SDK will filter and only process MCP-related topics
     * (those starting with "$mcp-"). Non-MCP messages will be ignored by the SDK,
     * but users can still handle them separately in their own code.
     *
     * @param handler The message handler callback
     */
    virtual void setMessageHandler(MqttMessageHandler handler) = 0;

    /**
     * @brief Set connection lost callback
     * @param callback Callback invoked when connection is lost
     */
    virtual void setConnectionLostCallback(std::function<void(const std::string& reason)> callback) = 0;
};

/**
 * @brief Configuration for MCP server that uses external MQTT client
 */
struct McpServerConfig {
    std::string serverId;       // Unique server instance ID (used in topics)
    std::string serverName;     // Hierarchical server name (e.g., "myapp/tools/v1")
};

} // namespace mcp_mqtt

#endif // MCP_MQTT_INTERFACE_H
