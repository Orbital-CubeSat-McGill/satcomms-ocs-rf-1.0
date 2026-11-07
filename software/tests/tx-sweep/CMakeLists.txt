cmake_minimum_required(VERSION 3.20.0)

# Where the ocs_rf_1 board definition lives
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Import the driver as a Zephyr module. This has to come before find_package(Zephyr)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_LIST_DIR}/../../module/at68rf215)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tx-sweep)

target_sources(app PRIVATE src/main.c)
