#include "mcp_mqtt/json_rpc.h"

namespace mcp_mqtt {

// JsonRpcRequest implementation
std::optional<JsonRpcRequest> JsonRpcRequest::fromJson(const nlohmann::json& j) {
    try {
        JsonRpcRequest req;

        if (!j.contains("jsonrpc") || j["jsonrpc"] != JSONRPC_VERSION) {
            return std::nullopt;
        }
        req.jsonrpc = j["jsonrpc"];

        if (!j.contains("method") || !j["method"].is_string()) {
            return std::nullopt;
        }
        req.method = j["method"];

        if (j.contains("id")) {
            req.id = JsonRpc::jsonToId(j["id"]);
        }

        if (j.contains("params")) {
            req.params = j["params"];
        }

        return req;
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json JsonRpcRequest::toJson() const {
    nlohmann::json j;
    j["jsonrpc"] = jsonrpc;
    j["method"] = method;

    if (!std::holds_alternative<std::monostate>(id)) {
        j["id"] = JsonRpc::idToJson(id);
    }

    if (params.has_value()) {
        j["params"] = params.value();
    }

    return j;
}

bool JsonRpcRequest::isNotification() const {
    return std::holds_alternative<std::monostate>(id);
}

// JsonRpcResponse implementation
JsonRpcResponse JsonRpcResponse::success(const JsonRpcId& id, const nlohmann::json& result) {
    JsonRpcResponse resp;
    resp.id = id;
    resp.result = result;
    return resp;
}

JsonRpcResponse JsonRpcResponse::errorResponse(const JsonRpcId& id, int code,
                                                const std::string& message,
                                                const std::optional<nlohmann::json>& data) {
    JsonRpcResponse resp;
    resp.id = id;
    nlohmann::json err;
    err["code"] = code;
    err["message"] = message;
    if (data.has_value()) {
        err["data"] = data.value();
    }
    resp.error = err;
    return resp;
}

nlohmann::json JsonRpcResponse::toJson() const {
    nlohmann::json j;
    j["jsonrpc"] = jsonrpc;
    j["id"] = JsonRpc::idToJson(id);

    if (result.has_value()) {
        j["result"] = result.value();
    } else if (error.has_value()) {
        j["error"] = error.value();
    }

    return j;
}

// JsonRpcNotification implementation
nlohmann::json JsonRpcNotification::toJson() const {
    nlohmann::json j;
    j["jsonrpc"] = jsonrpc;
    j["method"] = method;

    if (params.has_value()) {
        j["params"] = params.value();
    }

    return j;
}

JsonRpcNotification JsonRpcNotification::create(const std::string& method,
                                                 const std::optional<nlohmann::json>& params) {
    JsonRpcNotification notif;
    notif.method = method;
    notif.params = params;
    return notif;
}

// Utility functions
namespace JsonRpc {

nlohmann::json idToJson(const JsonRpcId& id) {
    if (std::holds_alternative<std::monostate>(id)) {
        return nullptr;
    } else if (std::holds_alternative<int64_t>(id)) {
        return std::get<int64_t>(id);
    } else {
        return std::get<std::string>(id);
    }
}

JsonRpcId jsonToId(const nlohmann::json& j) {
    if (j.is_null()) {
        return std::monostate{};
    } else if (j.is_number_integer()) {
        return j.get<int64_t>();
    } else if (j.is_string()) {
        return j.get<std::string>();
    }
    return std::monostate{};
}

std::string serialize(const nlohmann::json& j) {
    return j.dump();
}

std::optional<nlohmann::json> parse(const std::string& str) {
    try {
        return nlohmann::json::parse(str);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace JsonRpc

} // namespace mcp_mqtt
