#include "mcp_mqtt/tool_manager.h"
#include "mcp_mqtt/logger.h"

namespace mcp_mqtt {

bool ToolManager::registerTool(const Tool& tool, ToolHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tools_.find(tool.name) != tools_.end()) {
        return false; // Tool already exists
    }

    tools_[tool.name] = tool;
    handlers_[tool.name] = handler;
    return true;
}

void ToolManager::unregisterTool(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    tools_.erase(name);
    handlers_.erase(name);
}

std::vector<Tool> ToolManager::getTools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Tool> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }
    return result;
}

bool ToolManager::hasTool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_.find(name) != tools_.end();
}

ToolCallResult ToolManager::callTool(const std::string& name, const nlohmann::json& arguments) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = handlers_.find(name);
    if (it == handlers_.end()) {
        MCP_LOG_ERROR("Tool not found: " << name);
        return ToolCallResult::error("Tool not found: " + name);
    }

    try {
        return it->second(arguments);
    } catch (const std::exception& e) {
        MCP_LOG_ERROR("Tool execution error: tool=" << name << ", error=" << e.what());
        return ToolCallResult::error(std::string("Tool execution error: ") + e.what());
    } catch (...) {
        MCP_LOG_ERROR("Unknown error during tool execution: tool=" << name);
        return ToolCallResult::error("Unknown error during tool execution");
    }
}

nlohmann::json ToolManager::getToolsJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& [name, tool] : tools_) {
        tools.push_back(tool.toJson());
    }
    return tools;
}

} // namespace mcp_mqtt
