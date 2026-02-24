cmake_minimum_required(VERSION 3.14)
project(mcp_mqtt_server_sdk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# macOS: Ensure C++ standard library headers are found
if(APPLE)
    # Get SDK path
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(MACOS_SDK_PATH)
        # Add C++ standard library include path
        set(MACOS_CXX_INCLUDE "${MACOS_SDK_PATH}/usr/include/c++/v1")
        if(EXISTS "${MACOS_CXX_INCLUDE}")
            include_directories(SYSTEM "${MACOS_CXX_INCLUDE}")
        endif()
    endif()
endif()

# Options
option(BUILD_EXAMPLES "Build example applications" ON)
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Find required packages
find_package(nlohmann_json 3.9 REQUIRED)

# Optional: Find Paho MQTT C++ for the example
find_package(PahoMqttCpp QUIET)

# Source files
set(SDK_SOURCES
    src/mcp_server.cpp
    src/json_rpc.cpp
    src/tool_manager.cpp
)

# Header files
set(SDK_HEADERS
    include/mcp_mqtt.h
    include/mcp_mqtt/types.h
    include/mcp_mqtt/json_rpc.h
    include/mcp_mqtt/mqtt_interface.h
    include/mcp_mqtt/mcp_server.h
    include/mcp_mqtt/tool_manager.h
)

# Create library
add_library(mcp_mqtt_server ${SDK_SOURCES} ${SDK_HEADERS})

target_include_directories(mcp_mqtt_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcp_mqtt_server
    PUBLIC
        nlohmann_json::nlohmann_json
)

# Install library
install(TARGETS mcp_mqtt_server
    EXPORT mcp_mqtt_server-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT mcp_mqtt_server-targets
    FILE mcp_mqtt_server-targets.cmake
    NAMESPACE mcp_mqtt::
    DESTINATION lib/cmake/mcp_mqtt_server
)

# Build examples
if(BUILD_EXAMPLES AND PahoMqttCpp_FOUND)
    add_subdirectory(examples)
elseif(BUILD_EXAMPLES AND NOT PahoMqttCpp_FOUND)
    message(STATUS "Paho MQTT C++ not found, skipping examples. Install paho-mqtt-cpp to build examples.")
endif()
