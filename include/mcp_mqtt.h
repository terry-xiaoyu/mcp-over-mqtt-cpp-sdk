#ifndef MCP_MQTT_H
#define MCP_MQTT_H

/**
 * @file mcp_mqtt.h
 * @brief Main include file for MCP over MQTT Server C++ SDK
 *
 * Include this file to get access to all SDK classes and types.
 *
 * Usage:
 * 1. Implement IMqttClient interface with your preferred MQTT library
 * 2. Create and connect your MQTT client
 * 3. Create McpServer, configure it, register tools
 * 4. Call McpServer::start() with your MQTT client
 * 5. The SDK handles all MCP-related messaging automatically
 */

#include "mcp_mqtt/types.h"
#include "mcp_mqtt/json_rpc.h"
#include "mcp_mqtt/mqtt_interface.h"
#include "mcp_mqtt/tool_manager.h"
#include "mcp_mqtt/mcp_server.h"

#endif // MCP_MQTT_H
