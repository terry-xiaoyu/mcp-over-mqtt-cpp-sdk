#ifndef MCP_MQTT_TYPES_H
#define MCP_MQTT_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace mcp_mqtt {

// JSON-RPC 2.0 constants
constexpr const char* JSONRPC_VERSION = "2.0";
constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

// User property keys (MQTT 5.0)
constexpr const char* USER_PROP_COMPONENT_TYPE = "MCP-COMPONENT-TYPE";
constexpr const char* USER_PROP_MQTT_CLIENT_ID = "MCP-MQTT-CLIENT-ID";
constexpr const char* USER_PROP_META = "MCP-META";
constexpr const char* USER_PROP_SERVER_NAME = "MCP-SERVER-NAME";

// Component type values
constexpr const char* COMPONENT_TYPE_SERVER = "mcp-server";
constexpr const char* COMPONENT_TYPE_CLIENT = "mcp-client";

// Default timeouts (milliseconds)
struct Timeouts {
    static constexpr int INITIALIZE = 30000;
    static constexpr int PING = 10000;
    static constexpr int TOOLS_LIST = 30000;
    static constexpr int TOOLS_CALL = 60000;
};

// Server info
struct ServerInfo {
    std::string name;
    std::string version;
};

// Client info
struct ClientInfo {
    std::string name;
    std::string version;
};

// Server capabilities
struct ServerCapabilities {
    bool tools = true;
    bool toolsListChanged = false;

    nlohmann::json toJson() const {
        nlohmann::json j;
        if (tools) {
            j["tools"] = nlohmann::json::object();
            if (toolsListChanged) {
                j["tools"]["listChanged"] = true;
            }
        }
        return j;
    }
};

// Tool input schema
struct ToolInputSchema {
    std::string type = "object";
    nlohmann::json properties;
    std::vector<std::string> required;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        if (!properties.empty()) {
            j["properties"] = properties;
        }
        if (!required.empty()) {
            j["required"] = required;
        }
        return j;
    }
};

// Tool definition
struct Tool {
    std::string name;
    std::string description;
    ToolInputSchema inputSchema;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["name"] = name;
        j["description"] = description;
        j["inputSchema"] = inputSchema.toJson();
        return j;
    }
};

// Tool call result content
struct ToolResultContent {
    std::string type = "text";
    std::string text;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["type"] = type;
        j["text"] = text;
        return j;
    }
};

// Tool call result
struct ToolCallResult {
    std::vector<ToolResultContent> content;
    bool isError = false;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["content"] = nlohmann::json::array();
        for (const auto& c : content) {
            j["content"].push_back(c.toJson());
        }
        if (isError) {
            j["isError"] = true;
        }
        return j;
    }

    static ToolCallResult success(const std::string& text) {
        ToolCallResult result;
        result.content.push_back({"text", text});
        return result;
    }

    static ToolCallResult error(const std::string& errorMessage) {
        ToolCallResult result;
        result.content.push_back({"text", errorMessage});
        result.isError = true;
        return result;
    }
};

// Tool handler function type
using ToolHandler = std::function<ToolCallResult(const nlohmann::json& arguments)>;

// Server online notification params
struct ServerOnlineParams {
    std::string description;
    std::optional<nlohmann::json> meta;

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["description"] = description;
        if (meta.has_value()) {
            j["meta"] = meta.value();
        }
        return j;
    }
};

// Connected client session
struct ClientSession {
    std::string mcpClientId;
    std::string protocolVersion;
    ClientInfo clientInfo;
    nlohmann::json capabilities;
    bool initialized = false;
};

} // namespace mcp_mqtt

#endif // MCP_MQTT_TYPES_H
