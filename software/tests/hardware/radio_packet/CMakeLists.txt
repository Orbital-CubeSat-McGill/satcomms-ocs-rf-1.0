cmake_minimum_required(VERSION 3.20.0)

get_filename_component(OCS_RF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
list(APPEND BOARD_ROOT ${OCS_RF_ROOT})
list(APPEND DTS_ROOT ${OCS_RF_ROOT})
if(NOT DEFINED DTC_OVERLAY_FILE)
    set(DTC_OVERLAY_FILE "${OCS_RF_ROOT}/app.overlay")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ocs_radio_packet_bench)
target_include_directories(app PRIVATE ${OCS_RF_ROOT}/include)
target_sources(app PRIVATE
    src/main.c
    ${OCS_RF_ROOT}/src/radio/ocs_radio.c
    ${OCS_RF_ROOT}/src/radio/at86rf215.c
    ${OCS_RF_ROOT}/src/radio/at86rf215_packet.c
    ${OCS_RF_ROOT}/src/radio/at86rf215_zephyr.c
)
