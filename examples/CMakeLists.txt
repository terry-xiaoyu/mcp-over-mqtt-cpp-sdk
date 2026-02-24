cmake_minimum_required(VERSION 3.14)

# Example requires Paho MQTT C++
add_executable(simple_server simple_server.cpp)
target_link_libraries(simple_server
    PRIVATE
        mcp_mqtt_server
        PahoMqttCpp::paho-mqttpp3
)
