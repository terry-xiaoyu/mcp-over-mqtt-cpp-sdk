# MCP over MQTT Server C++ SDK

[![GitHub](https://img.shields.io/github/license/terry-xiaoyu/mcp-over-mqtt-cpp-sdk)](https://github.com/terry-xiaoyu/mcp-over-mqtt-cpp-sdk)

A C++ SDK for implementing MCP (Model Context Protocol) servers over MQTT transport, based on the [MCP over MQTT specification](https://github.com/emqx/mcp-over-mqtt).

## Key Design Principle

**Users control the MQTT client, SDK handles MCP logic.**

This SDK does NOT manage the MQTT connection itself. Instead:

1. **You create and manage your own MQTT client** using any MQTT library you prefer
2. **You implement a simple interface** (`IMqttClient`) to connect your MQTT client to the SDK
3. **The SDK only processes MCP-related topics** (`$mcp-*`) and ignores everything else
4. **You have full access to your MQTT client** for any non-MCP purposes

This design gives you complete flexibility to:
- Use any MQTT library (Paho, mosquitto, etc.)
- Handle custom topics and messages
- Configure MQTT client exactly as needed
- Integrate with existing MQTT infrastructure

## Features

- **Service Discovery**: Automatically publishes server presence notifications
- **Initialization**: Handles MCP initialization handshake with clients
- **Tools**: Register and expose tools that clients can call
- **Health Check**: Responds to ping requests
- **Shutdown**: Proper cleanup and disconnection handling

## Requirements

- C++17 or later
- CMake 3.14 or later
- [nlohmann/json](https://github.com/nlohmann/json) library
- Any MQTT 5.0 client library (for implementing `IMqttClient`)

### Installing Dependencies on ARM64 Linux

```bash
# Install build tools
sudo apt-get update
sudo apt-get install -y build-essential cmake git

# Install nlohmann-json
sudo apt-get install -y nlohmann-json3-dev

# Optional: Install Paho MQTT C++ for the example
# (You can use any MQTT library you prefer)
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
mkdir build && cd build
cmake -DPAHO_WITH_SSL=ON -DPAHO_BUILD_SHARED=ON ..
make -j$(nproc)
sudo make install
cd ../..

git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp
mkdir build && cd build
cmake -DPAHO_WITH_SSL=ON -DPAHO_BUILD_SHARED=ON ..
make -j$(nproc)
sudo make install
cd ../..

sudo ldconfig
```

### Installing Dependencies on macOS (M1/ARM64)

```bash
# Install Homebrew if not already installed
# /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install nlohmann-json
brew install nlohmann-json

# Optional: Install Paho MQTT C++ for the example
# Since brew doesn't have paho-mqtt-cpp, install from source:
brew install openssl cmake

git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
mkdir build && cd build
cmake -DPAHO_WITH_SSL=ON -DPAHO_BUILD_SHARED=ON \
      -DOPENSSL_ROOT_DIR=$(brew --prefix openssl) ..
make -j$(sysctl -n hw.ncpu)
sudo make install
cd ../..

git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp
mkdir build && cd build
cmake -DPAHO_WITH_SSL=ON -DPAHO_BUILD_SHARED=ON ..
make -j$(sysctl -n hw.ncpu)
sudo make install
cd ../..
```

## Building the SDK

### Linux
```bash
cd mcp_server_cpp_sdk
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### macOS
```bash
cd mcp_server_cpp_sdk
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

### Build Options

```bash
# Build static library instead of shared
cmake -DBUILD_SHARED_LIBS=OFF ..

# Skip building examples
cmake -DBUILD_EXAMPLES=OFF ..
```

## Quick Start

### Step 1: Implement IMqttClient Interface

```cpp
#include <mcp_mqtt.h>

class MyMqttClient : public mcp_mqtt::IMqttClient {
public:
    // Implement required methods
    bool isConnected() const override { /* ... */ }
    bool subscribe(const std::string& topic, int qos, bool noLocal) override { /* ... */ }
    bool unsubscribe(const std::string& topic) override { /* ... */ }
    bool publish(const std::string& topic, const std::string& payload,
                 int qos, bool retained,
                 const std::map<std::string, std::string>& userProps) override { /* ... */ }
    std::string getClientId() const override { /* ... */ }
    void setMessageHandler(MqttMessageHandler handler) override { /* ... */ }
    void setConnectionLostCallback(std::function<void(const std::string&)> callback) override { /* ... */ }
};
```

### Step 2: Create and Connect Your MQTT Client

```cpp
MyMqttClient mqttClient;
mqttClient.connect("tcp://localhost:1883", "my-server-id");
```

### Step 3: Create and Configure MCP Server

```cpp
using namespace mcp_mqtt;

McpServer server;

// Configure server info
ServerInfo info{"MyServer", "1.0.0"};
server.configure(info);

// Set description for service discovery
server.setServiceDescription("My MCP server provides useful tools");

// Register tools
Tool myTool;
myTool.name = "greet";
myTool.description = "Generate a greeting";
myTool.inputSchema.properties = {
    {"name", {{"type", "string"}}}
};
myTool.inputSchema.required = {"name"};

server.registerTool(myTool, [](const nlohmann::json& args) -> ToolCallResult {
    std::string name = args.value("name", "World");
    return ToolCallResult::success("Hello, " + name + "!");
});
```

### Step 4: Start MCP Server with Your MQTT Client

```cpp
McpServerConfig config;
config.serverId = "my-server-id";
config.serverName = "myapp/greeter";

server.start(&mqttClient, config);
```

### Step 5: Use Your MQTT Client for Non-MCP Purposes

```cpp
// You can still use your MQTT client for custom topics!
mqttClient.subscribe("custom/topic", 1, false);
mqttClient.publish("custom/status", "online", 1, false, {});
```

## API Reference

### IMqttClient Interface

Users must implement this interface to provide MQTT functionality:

```cpp
class IMqttClient {
public:
    virtual bool isConnected() const = 0;
    virtual bool subscribe(const std::string& topic, int qos, bool noLocal) = 0;
    virtual bool unsubscribe(const std::string& topic) = 0;
    virtual bool publish(const std::string& topic, const std::string& payload,
                         int qos, bool retained,
                         const std::map<std::string, std::string>& userProps) = 0;
    virtual std::string getClientId() const = 0;
    virtual void setMessageHandler(MqttMessageHandler handler) = 0;
    virtual void setConnectionLostCallback(std::function<void(const std::string&)> callback) = 0;
};
```

**Important**: Your `setMessageHandler` implementation should route ALL incoming messages to the handler. The SDK will automatically filter and only process MCP-related topics (`$mcp-*`).

### McpServer Class

```cpp
// Configure server
void configure(const ServerInfo& serverInfo, const ServerCapabilities& capabilities = {});
void setServiceDescription(const std::string& description, const std::optional<nlohmann::json>& meta = std::nullopt);

// Lifecycle
bool start(IMqttClient* mqttClient, const McpServerConfig& config);
void stop();
bool isRunning() const;

// Tools
bool registerTool(const Tool& tool, ToolHandler handler);
void unregisterTool(const std::string& name);
std::vector<Tool> getTools() const;

// Callbacks
void setClientConnectedCallback(ClientConnectedCallback callback);
void setClientDisconnectedCallback(ClientDisconnectedCallback callback);

// Getters
const std::string& getServerId() const;
const std::string& getServerName() const;
std::vector<std::string> getConnectedClients() const;
```

### Tool Definition

```cpp
struct Tool {
    std::string name;
    std::string description;
    ToolInputSchema inputSchema;
};

struct ToolInputSchema {
    std::string type = "object";
    nlohmann::json properties;
    std::vector<std::string> required;
};
```

### Tool Handler

```cpp
using ToolHandler = std::function<ToolCallResult(const nlohmann::json& arguments)>;

// Return success
return ToolCallResult::success("Result text");

// Return error
return ToolCallResult::error("Error message");
```

## Complete Example

See [examples/simple_server.cpp](examples/simple_server.cpp) for a complete example that:
- Implements `IMqttClient` using Paho MQTT C++
- Creates a calculator server with four tools
- Demonstrates using the MQTT client for non-MCP purposes

### Running the Example

```bash
# Build (requires Paho MQTT C++)
cd mcp_server_cpp_sdk/build
make simple_server

# Run directly from build directory (RPATH is set automatically by CMake)
./examples/simple_server [broker_address] [server_id] [server_name]
./examples/simple_server tcp://localhost:1883 demo-server-001 demo/calculator
```

If you see `error while loading shared libraries: libmcp_mqtt_server.so`, use one of the following methods:

**Method 1: Install the library to system path**
```bash
cd build
sudo make install
sudo ldconfig        # Linux only
```

**Method 2: Set library search path temporarily**
```bash
# Linux
export LD_LIBRARY_PATH=$(pwd):$LD_LIBRARY_PATH

# macOS
export DYLD_LIBRARY_PATH=$(pwd):$DYLD_LIBRARY_PATH

# Then run
./examples/simple_server tcp://localhost:1883 demo-server-001 demo/calculator
```

## Protocol Details

### MQTT Topics Used by SDK

| Topic | Purpose |
|-------|---------|
| `$mcp-server/{server-id}/{server-name}` | Control topic (receives initialize requests) |
| `$mcp-server/presence/{server-id}/{server-name}` | Presence topic (service discovery) |
| `$mcp-rpc/{client-id}/{server-id}/{server-name}` | RPC topic (request/response) |
| `$mcp-client/presence/{client-id}` | Client presence (subscribed) |

### Supported MCP Methods

| Method | Description |
|--------|-------------|
| `initialize` | Establish connection with client |
| `ping` | Health check |
| `tools/list` | List available tools |
| `tools/call` | Invoke a tool |

## Logging

The SDK includes a built-in logger with runtime-configurable log levels. By default, the log level is `INFO`.

### Log Levels

| Level | Description |
|-------|-------------|
| `DEBUG` | Detailed internal operations: MQTT message routing, JSON parsing, topic subscriptions |
| `INFO` | Key lifecycle events: server start/stop, client connect/disconnect, tool calls |
| `WARN` | Unexpected but recoverable situations: unknown methods, missing sessions |
| `ERROR` | Failures: parse errors, connection lost, tool execution errors |
| `OFF` | Disable all logging |

### Setting Log Level

```cpp
#include <mcp_mqtt.h>
using namespace mcp_mqtt;

// Enable debug logging (shows all messages)
Logger::setLevel(LogLevel::DEBUG);

// Default: INFO level
Logger::setLevel(LogLevel::INFO);

// Only warnings and errors
Logger::setLevel(LogLevel::WARN);

// Disable all SDK logging
Logger::setLevel(LogLevel::OFF);
```

The log level can be changed at any time during runtime. Output format:

```
2025-01-15 14:30:00.123 [DEBUG] [mcp] Received MQTT message: topic=$mcp-rpc/..., payload=...
2025-01-15 14:30:00.124 [INFO ] [mcp] Tool call: tool=add, client=client-001
```

## Thread Safety

- The SDK uses internal mutexes to protect shared state
- Callbacks are invoked from the MQTT client's message handler thread
- Tool handlers should be thread-safe if they access shared resources

## MQTT Client Requirements

Your MQTT client implementation must:

1. **Use MQTT 5.0** - Required for user properties and No Local subscription option
2. **Route all messages to the handler** - The SDK filters internally
3. **Support No Local subscription option** - To prevent receiving own messages
4. **Handle connection will message** - For presence cleanup on disconnect

### Recommended Will Message Setup

```cpp
// Set will message to clear presence on unexpected disconnect
std::string willTopic = "$mcp-server/presence/" + serverId + "/" + serverName;
std::string willPayload = "";  // Empty payload clears retained message
// Configure your MQTT client with this will message (QoS 1, retained)
```

## License

MIT License
