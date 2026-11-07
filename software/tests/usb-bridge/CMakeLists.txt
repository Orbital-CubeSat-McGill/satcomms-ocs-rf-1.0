cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../module/at68rf215)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usb-bridge)

target_sources(app PRIVATE src/main.c src/cobs.c)
target_include_directories(app PRIVATE src)
