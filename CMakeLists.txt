cmake_minimum_required(VERSION 3.16)
project(OpenRGBAorusMonitorPlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Path to an OpenRGB source tree matching the installed OpenRGB (release_1.1)
set(OPENRGB_SOURCE_DIR "$ENV{HOME}/src/OpenRGB" CACHE PATH "OpenRGB source tree (headers only)")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

set(OPENRGB_INCLUDES
    ${OPENRGB_SOURCE_DIR}
    ${OPENRGB_SOURCE_DIR}/RGBController
    ${OPENRGB_SOURCE_DIR}/dependencies/json
)

# Device driver (shared by plugin and CLI)
add_library(aorus_monitor_driver STATIC src/AorusMonitorController.cpp)
target_include_directories(aorus_monitor_driver PUBLIC src)
target_link_libraries(aorus_monitor_driver PUBLIC PkgConfig::HIDAPI)

# OpenRGB plugin
add_library(OpenRGBAorusMonitorPlugin MODULE
    src/AorusMonitorPlugin.cpp
    src/AorusMonitorPlugin.h
)
target_include_directories(OpenRGBAorusMonitorPlugin PRIVATE ${OPENRGB_INCLUDES})
target_link_libraries(OpenRGBAorusMonitorPlugin PRIVATE aorus_monitor_driver Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(OpenRGBAorusMonitorPlugin PROPERTIES PREFIX "lib")

# Standalone test CLI
add_executable(aorus-monitor-rgb tools/aorus-monitor-rgb.cpp)
target_link_libraries(aorus-monitor-rgb PRIVATE aorus_monitor_driver)

install(TARGETS OpenRGBAorusMonitorPlugin LIBRARY DESTINATION "$ENV{HOME}/.config/OpenRGB/plugins")
install(TARGETS aorus-monitor-rgb RUNTIME DESTINATION "$ENV{HOME}/.local/bin")
