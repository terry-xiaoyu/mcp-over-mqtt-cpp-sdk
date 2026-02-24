#ifndef MCP_MQTT_JSON_RPC_H
#define MCP_MQTT_JSON_RPC_H

#include <string>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>
#include "types.h"

namespace mcp_mqtt {

// JSON-RPC error codes
namespace JsonRpcError {
    constexpr int PARSE_ERROR = -32700;
    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int INTERNAL_ERROR = -32603;
}

// JSON-RPC ID type (can be string, int, or null)
using JsonRpcId = std::variant<std::monostate, int64_t, std::string>;

// JSON-RPC Request
struct JsonRpcRequest {
    std::string jsonrpc = JSONRPC_VERSION;
    JsonRpcId id;
    std::string method;
    std::optional<nlohmann::json> params;

    static std::optional<JsonRpcRequest> fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
    bool isNotification() const;
};

// JSON-RPC Response
struct JsonRpcResponse {
    std::string jsonrpc = JSONRPC_VERSION;
    JsonRpcId id;
    std::optional<nlohmann::json> result;
    std::optional<nlohmann::json> error;

    static JsonRpcResponse success(const JsonRpcId& id, const nlohmann::json& result);
    static JsonRpcResponse errorResponse(const JsonRpcId& id, int code,
                                         const std::string& message,
                                         const std::optional<nlohmann::json>& data = std::nullopt);
    nlohmann::json toJson() const;
};

// JSON-RPC Notification
struct JsonRpcNotification {
    std::string jsonrpc = JSONRPC_VERSION;
    std::string method;
    std::optional<nlohmann::json> params;

    nlohmann::json toJson() const;
    static JsonRpcNotification create(const std::string& method,
                                      const std::optional<nlohmann::json>& params = std::nullopt);
};

// Utility functions
namespace JsonRpc {
    nlohmann::json idToJson(const JsonRpcId& id);
    JsonRpcId jsonToId(const nlohmann::json& j);
    std::string serialize(const nlohmann::json& j);
    std::optional<nlohmann::json> parse(const std::string& str);
}

} // namespace mcp_mqtt

#endif // MCP_MQTT_JSON_RPC_H
