#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <mutex>

// Paho MQTT C++ headers
#include <mqtt/async_client.h>

// MCP SDK headers
#include <mcp_mqtt.h>

using namespace mcp_mqtt;

// Global flag for signal handling
std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    g_running = false;
}

/**
 * @brief Example implementation of IMqttClient using Paho MQTT C++
 *
 * This demonstrates how users implement the IMqttClient interface
 * using their preferred MQTT library. Users have full control over
 * the MQTT client and can use it for any purpose.
 */
class PahoMqttClientAdapter : public IMqttClient, public mqtt::callback {
public:
    PahoMqttClientAdapter(const std::string& brokerAddress, const std::string& clientId)
        : clientId_(clientId) {
        // Create MQTT 5.0 client
        mqtt::create_options createOpts(MQTTVERSION_5);
        client_ = std::make_unique<mqtt::async_client>(brokerAddress, clientId, createOpts);
        client_->set_callback(*this);
    }

    ~PahoMqttClientAdapter() override {
        if (client_->is_connected()) {
            client_->disconnect()->wait();
        }
    }

    // Connect to broker (user manages this)
    bool connect(const std::string& username = "", const std::string& password = "",
                 const std::string& willTopic = "", const std::string& willPayload = "") {
        try {
            auto connBuilder = mqtt::connect_options_builder()
                .mqtt_version(MQTTVERSION_5)
                .clean_start(true)
                .keep_alive_interval(std::chrono::seconds(60));

            if (!username.empty()) {
                connBuilder.user_name(username);
                if (!password.empty()) {
                    connBuilder.password(password);
                }
            }

            // Set will message if provided
            if (!willTopic.empty()) {
                mqtt::message willMsg(willTopic, willPayload, 1, true);
                connBuilder.will(willMsg);
            }

            // Set MQTT 5.0 properties
            mqtt::properties props;
            props.add(mqtt::property(mqtt::property::SESSION_EXPIRY_INTERVAL, 0));
            props.add(mqtt::property(mqtt::property::USER_PROPERTY,
                "MCP-COMPONENT-TYPE", "mcp-server"));
            connBuilder.properties(props);

            auto connOpts = connBuilder.finalize();
            client_->connect(connOpts)->wait();
            return true;
        } catch (const mqtt::exception& e) {
            std::cerr << "Connect error: " << e.what() << std::endl;
            return false;
        }
    }

    // IMqttClient interface implementation
    bool isConnected() const override {
        return client_->is_connected();
    }

    bool subscribe(const std::string& topic, int qos, bool noLocal) override {
        try {
            mqtt::subscribe_options subOpts;
            subOpts.set_no_local(noLocal);
            client_->subscribe(topic, qos, subOpts)->wait();
            return true;
        } catch (const mqtt::exception& e) {
            std::cerr << "Subscribe error: " << e.what() << std::endl;
            return false;
        }
    }

    bool unsubscribe(const std::string& topic) override {
        try {
            client_->unsubscribe(topic)->wait();
            return true;
        } catch (const mqtt::exception& e) {
            std::cerr << "Unsubscribe error: " << e.what() << std::endl;
            return false;
        }
    }

    bool publish(const std::string& topic, const std::string& payload,
                 int qos, bool retained,
                 const std::map<std::string, std::string>& userProps) override {
        try {
            auto msg = mqtt::make_message(topic, payload, qos, retained);

            // Add user properties
            mqtt::properties props;
            for (const auto& [key, value] : userProps) {
                props.add(mqtt::property(mqtt::property::USER_PROPERTY, key, value));
            }
            msg->set_properties(props);

            client_->publish(msg);
            return true;
        } catch (const mqtt::exception& e) {
            std::cerr << "Publish error: " << e.what() << std::endl;
            return false;
        }
    }

    std::string getClientId() const override {
        return clientId_;
    }

    void setMessageHandler(MqttMessageHandler handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        messageHandler_ = handler;
    }

    void setConnectionLostCallback(std::function<void(const std::string&)> callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        connectionLostCallback_ = callback;
    }

    // mqtt::callback overrides
    void message_arrived(mqtt::const_message_ptr msg) override {
        MqttMessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = messageHandler_;
        }

        if (handler) {
            MqttIncomingMessage inMsg;
            inMsg.topic = msg->get_topic();
            inMsg.payload = msg->to_string();
            inMsg.qos = msg->get_qos();
            inMsg.retained = msg->is_retained();

            // Extract user properties
            const auto& props = msg->get_properties();
            if (props.contains(mqtt::property::USER_PROPERTY)) {
                try {
                    auto userProps = mqtt::get<std::vector<mqtt::string_pair>>(
                        props, mqtt::property::USER_PROPERTY);
                    for (const auto& prop : userProps) {
                        inMsg.userProperties[std::string(std::get<0>(prop))] = std::string(std::get<1>(prop));
                    }
                } catch (...) {}
            }

            handler(inMsg);
        }
    }

    void connection_lost(const std::string& cause) override {
        std::function<void(const std::string&)> callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = connectionLostCallback_;
        }
        if (callback) {
            callback(cause);
        }
    }

    void connected(const std::string&) override {}
    void delivery_complete(mqtt::delivery_token_ptr) override {}

    // User can access the underlying client for non-MCP operations
    mqtt::async_client* getUnderlyingClient() {
        return client_.get();
    }

private:
    std::unique_ptr<mqtt::async_client> client_;
    std::string clientId_;
    std::mutex mutex_;
    MqttMessageHandler messageHandler_;
    std::function<void(const std::string&)> connectionLostCallback_;
};

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string brokerAddress = "tcp://localhost:1883";
    std::string serverId = "demo-server-001";
    std::string serverName = "demo/calculator";

    if (argc > 1) brokerAddress = argv[1];
    if (argc > 2) serverId = argv[2];
    if (argc > 3) serverName = argv[3];

    // Enable debug logging for the MCP SDK
    Logger::setLevel(LogLevel::DEBUG);

    std::cout << "=== MCP over MQTT Server Example ===" << std::endl;
    std::cout << "Broker: " << brokerAddress << std::endl;
    std::cout << "Server ID: " << serverId << std::endl;
    std::cout << "Server Name: " << serverName << std::endl;
    std::cout << std::endl;

    // Setup signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Step 1: Create and configure YOUR OWN MQTT client
    PahoMqttClientAdapter mqttClient(brokerAddress, serverId);

    // Set will message for presence (required by MCP protocol)
    std::string willTopic = "$mcp-server/presence/" + serverId + "/" + serverName;
    if (!mqttClient.connect("", "", willTopic, "")) {
        std::cerr << "Failed to connect to MQTT broker" << std::endl;
        return 1;
    }
    std::cout << "Connected to MQTT broker" << std::endl;

    // Step 2: Create and configure MCP server
    McpServer mcpServer;

    ServerInfo info;
    info.name = "DemoCalculatorServer";
    info.version = "1.0.0";

    ServerCapabilities caps;
    caps.tools = true;

    mcpServer.configure(info, caps);
    mcpServer.setServiceDescription(
        "A demo MCP server providing calculator tools (add, subtract, multiply, divide).");

    // Step 3: Register tools
    {
        Tool addTool;
        addTool.name = "add";
        addTool.description = "Add two numbers together";
        addTool.inputSchema.properties = {
            {"a", {{"type", "number"}, {"description", "First number"}}},
            {"b", {{"type", "number"}, {"description", "Second number"}}}
        };
        addTool.inputSchema.required = {"a", "b"};

        mcpServer.registerTool(addTool, [](const nlohmann::json& args) -> ToolCallResult {
            double a = args.value("a", 0.0);
            double b = args.value("b", 0.0);
            return ToolCallResult::success(std::to_string(a + b));
        });
    }

    {
        Tool subTool;
        subTool.name = "subtract";
        subTool.description = "Subtract second number from first";
        subTool.inputSchema.properties = {
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        };
        subTool.inputSchema.required = {"a", "b"};

        mcpServer.registerTool(subTool, [](const nlohmann::json& args) -> ToolCallResult {
            double a = args.value("a", 0.0);
            double b = args.value("b", 0.0);
            return ToolCallResult::success(std::to_string(a - b));
        });
    }

    {
        Tool mulTool;
        mulTool.name = "multiply";
        mulTool.description = "Multiply two numbers";
        mulTool.inputSchema.properties = {
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        };
        mulTool.inputSchema.required = {"a", "b"};

        mcpServer.registerTool(mulTool, [](const nlohmann::json& args) -> ToolCallResult {
            double a = args.value("a", 0.0);
            double b = args.value("b", 0.0);
            return ToolCallResult::success(std::to_string(a * b));
        });
    }

    {
        Tool divTool;
        divTool.name = "divide";
        divTool.description = "Divide first number by second";
        divTool.inputSchema.properties = {
            {"a", {{"type", "number"}, {"description", "Dividend"}}},
            {"b", {{"type", "number"}, {"description", "Divisor"}}}
        };
        divTool.inputSchema.required = {"a", "b"};

        mcpServer.registerTool(divTool, [](const nlohmann::json& args) -> ToolCallResult {
            double a = args.value("a", 0.0);
            double b = args.value("b", 0.0);
            if (b == 0) {
                return ToolCallResult::error("Division by zero");
            }
            return ToolCallResult::success(std::to_string(a / b));
        });
    }

    // Set callbacks
    mcpServer.setClientConnectedCallback([](const std::string& clientId, const ClientInfo& info) {
        std::cout << "[MCP] Client connected: " << clientId
                  << " (" << info.name << " v" << info.version << ")" << std::endl;
    });

    mcpServer.setClientDisconnectedCallback([](const std::string& clientId) {
        std::cout << "[MCP] Client disconnected: " << clientId << std::endl;
    });

    // Step 4: Start MCP server with YOUR MQTT client
    McpServerConfig mcpConfig;
    mcpConfig.serverId = serverId;
    mcpConfig.serverName = serverName;

    if (!mcpServer.start(&mqttClient, mcpConfig)) {
        std::cerr << "Failed to start MCP server" << std::endl;
        return 1;
    }

    std::cout << "\nMCP server is running!" << std::endl;
    std::cout << "Control topic: $mcp-server/" << serverId << "/" << serverName << std::endl;
    std::cout << "Presence topic: " << willTopic << std::endl;
    std::cout << "\nRegistered tools:" << std::endl;
    for (const auto& tool : mcpServer.getTools()) {
        std::cout << "  - " << tool.name << ": " << tool.description << std::endl;
    }

    // Step 5: You can still use your MQTT client for non-MCP purposes!
    std::cout << "\n--- Custom MQTT usage (non-MCP) ---" << std::endl;

    // Subscribe to a custom topic
    mqttClient.subscribe("demo/custom/#", 1, false);
    std::cout << "Subscribed to custom topic: demo/custom/#" << std::endl;

    // Publish to a custom topic
    mqttClient.publish("demo/status", "{\"server\": \"running\"}", 1, false, {});
    std::cout << "Published to custom topic: demo/status" << std::endl;

    std::cout << "\nPress Ctrl+C to exit..." << std::endl;

    // Main loop
    while (g_running && mcpServer.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    std::cout << "\nStopping MCP server..." << std::endl;
    mcpServer.stop();

    std::cout << "Server stopped. Goodbye!" << std::endl;
    return 0;
}
