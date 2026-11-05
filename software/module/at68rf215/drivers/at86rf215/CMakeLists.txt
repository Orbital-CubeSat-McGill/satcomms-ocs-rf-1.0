# Register a CMake library for this driver
zephyr_library()

# Include directories go here (public, so apps can include rf215.h)
zephyr_include_directories(.)

# Source Files go here
zephyr_library_sources(
        rf215.c
        rf215_spi.c
)