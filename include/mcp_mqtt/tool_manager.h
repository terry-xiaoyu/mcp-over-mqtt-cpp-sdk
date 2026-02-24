#ifndef MCP_MQTT_TOOL_MANAGER_H
#define MCP_MQTT_TOOL_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>
#include "types.h"

namespace mcp_mqtt {

/**
 * @brief Manages tool registration and invocation for MCP server.
 */
class ToolManager {
public:
    ToolManager() = default;
    ~ToolManager() = default;

    /**
     * @brief Register a tool
     * @param tool Tool definition
     * @param handler Handler function for tool calls
     * @return true if registered successfully
     */
    bool registerTool(const Tool& tool, ToolHandler handler);

    /**
     * @brief Unregister a tool
     * @param name Tool name
     */
    void unregisterTool(const std::string& name);

    /**
     * @brief Get all registered tools
     * @return Vector of tool definitions
     */
    std::vector<Tool> getTools() const;

    /**
     * @brief Check if a tool exists
     * @param name Tool name
     */
    bool hasTool(const std::string& name) const;

    /**
     * @brief Call a tool
     * @param name Tool name
     * @param arguments Tool arguments
     * @return Tool call result
     */
    ToolCallResult callTool(const std::string& name, const nlohmann::json& arguments);

    /**
     * @brief Get tools as JSON for tools/list response
     */
    nlohmann::json getToolsJson() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, Tool> tools_;
    std::map<std::string, ToolHandler> handlers_;
};

} // namespace mcp_mqtt

#endif // MCP_MQTT_TOOL_MANAGER_H
