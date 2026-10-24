cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ocs_can_loopback_tests)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
target_sources(app PRIVATE src/main.c
    ../../../src/can/mcp251863_probe.c ../../../src/can/mcp251863_loopback.c)
