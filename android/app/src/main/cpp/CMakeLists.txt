cmake_minimum_required(VERSION 3.22.1)
project(phonebridge_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared protocol library lives at <repo>/protocol (same code the Windows side uses).
# This file is at <repo>/android/app/src/main/cpp -> five levels up is <repo>.
set(PB_PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../protocol)

add_library(phonebridge_native SHARED
    jni_bridge.cpp
    ${PB_PROTOCOL_DIR}/packet_parser.cpp)
target_include_directories(phonebridge_native PRIVATE ${PB_PROTOCOL_DIR})
target_link_libraries(phonebridge_native log)
