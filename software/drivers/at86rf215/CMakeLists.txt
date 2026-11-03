# Register a CMake library for this driver
zephyr_library()

# Include directories go here
zephyr_library_include_directories(.)

# Source Files go here
zephyr_library_sources(
        rf215.c
        rf215_spi.c
)