# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MCP over MQTT Server C++ SDK — implements MCP (Model Context Protocol) servers over MQTT transport. The SDK handles MCP protocol logic while users control their own MQTT client via the `IMqttClient` interface.

## Build Commands

```bash
# Configure and build (from repo root)
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)    # macOS
make -j$(nproc)                 # Linux

# Build static library instead of shared
cmake -DBUILD_SHARED_LIBS=OFF ..

# Skip examples (avoids Paho MQTT dependency)
cmake -DBUILD_EXAMPLES=OFF ..

# Build just the example
make simple_server

# Run example
./examples/simple_server [broker_address] [server_id] [server_name]
```

No test suite exists. No linter is configured.

## Dependencies

- **Required:** `nlohmann/json` (3.9+) — found via `find_package`
- **Optional:** `PahoMqttCpp` — only needed for building examples; if not found, examples are silently skipped
- C++17, CMake 3.14+

## Architecture

### Core Design Principle

The SDK does NOT own the MQTT client. Users implement `IMqttClient` with their preferred MQTT library, then pass it to `McpServer::start()`. The SDK filters incoming messages by topic prefix (`$mcp-*`) and ignores everything else, so the user's MQTT client can serve dual purposes.

### Key Classes

- **`IMqttClient`** ([include/mcp_mqtt/mqtt_interface.h](include/mcp_mqtt/mqtt_interface.h)) — Interface users implement to bridge their MQTT library. Must support MQTT 5.0 (user properties, no-local subscriptions).
- **`McpServer`** ([include/mcp_mqtt/mcp_server.h](include/mcp_mqtt/mcp_server.h), [src/mcp_server.cpp](src/mcp_server.cpp)) — Main class. Manages lifecycle (start/stop), client sessions, message routing across control/RPC/presence topics, and dispatches tool calls.
- **`ToolManager`** ([include/mcp_mqtt/tool_manager.h](include/mcp_mqtt/tool_manager.h), [src/tool_manager.cpp](src/tool_manager.cpp)) — Thread-safe registry mapping tool names to `ToolHandler` callbacks.
- **`JsonRpcRequest`/`JsonRpcResponse`** ([include/mcp_mqtt/json_rpc.h](include/mcp_mqtt/json_rpc.h), [src/json_rpc.cpp](src/json_rpc.cpp)) — JSON-RPC 2.0 serialization/deserialization.
- **`Logger`** ([include/mcp_mqtt/logger.h](include/mcp_mqtt/logger.h)) — Header-only, thread-safe, runtime-configurable logger. Use macros `MCP_LOG_DEBUG`, `MCP_LOG_INFO`, `MCP_LOG_WARN`, `MCP_LOG_ERROR`.

### MQTT Topic Structure

| Topic Pattern | Purpose |
|---|---|
| `$mcp-server/{server-id}/{server-name}` | Control — receives `initialize` requests |
| `$mcp-server/presence/{server-id}/{server-name}` | Presence — retained message for service discovery |
| `$mcp-rpc/{client-id}/{server-id}/{server-name}` | RPC — bidirectional request/response per client |
| `$mcp-client/presence/{client-id}` | Client presence — monitored for disconnect detection |

### Message Flow

1. Client sends `initialize` to the control topic
2. Server subscribes to client-specific RPC topic and client presence
3. Client sends `tools/list`, `tools/call`, `ping` on RPC topic
4. Server responds on the same RPC topic
5. On shutdown, server publishes `notifications/disconnected` and clears presence

### Thread Safety

Internal mutexes protect client sessions and tool registry. Callbacks fire on the MQTT client's message handler thread. Tool handlers must be thread-safe if they access shared resources.

### Single Include Entry Point

`#include <mcp_mqtt.h>` — includes all SDK headers. Everything is in the `mcp_mqtt` namespace.

## Source Layout

- `include/mcp_mqtt.h` — umbrella header
- `include/mcp_mqtt/` — public headers (types, interfaces, server, tools, JSON-RPC, logger)
- `src/` — implementation files (mcp_server.cpp, json_rpc.cpp, tool_manager.cpp)
- `examples/simple_server.cpp` — complete reference implementation using Paho MQTT C++
- `cmake/` — CMake package config template for `find_package` integration
