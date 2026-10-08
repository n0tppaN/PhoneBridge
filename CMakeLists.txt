cmake_minimum_required(VERSION 3.20)
project(phonebridge LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(pb_protocol STATIC protocol/packet_parser.cpp)
target_include_directories(pb_protocol PUBLIC protocol)

enable_testing()
add_executable(protocol_tests tests/protocol/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE pb_protocol)
add_test(NAME protocol COMMAND protocol_tests)
