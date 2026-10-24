cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ocs_radio_packet_tests)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
target_sources(app PRIVATE
    src/main.c
    ../../../src/radio/ocs_radio.c
    ../../../src/radio/at86rf215.c
    ../../../src/radio/at86rf215_packet.c
)
