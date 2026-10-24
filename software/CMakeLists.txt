cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Keep the original software/ build entry point for the SPI board test.
if(NOT DEFINED CONF_FILE)
    set(CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/hardware/spi_bringup/prj.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ocs_rf_radio_bringup)

target_include_directories(app PRIVATE
    include
    src/radio
)

target_sources(app PRIVATE
    tests/hardware/spi_bringup/src/main.c
    src/radio/ocs_radio.c
    src/radio/at86rf215_packet.c
    src/radio/at86rf215.c
    src/radio/at86rf215_zephyr.c
)
