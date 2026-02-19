# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(Birdsong C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(Birdsong)

target_sources(Birdsong PRIVATE birdsong.c)

# enable usb output
pico_enable_stdio_usb(Birdsong 1)

target_link_libraries(Birdsong pico_stdlib pico_multicore pico_bootsel_via_double_reset hardware_pio hardware_dma hardware_sync hardware_spi hardware_clocks)

pico_add_extra_outputs(Birdsong)