cmake_minimum_required(VERSION 3.13)
set(NAME wifi-probe)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_BOARD pico_w)
set(PICO_CYW43_ARCH_DEFAULT_COUNTRY_CODE AU)

# initialize pico-sdk from submodule
# note: this must happen before project()
include(../pico-sdk/pico_sdk_init.cmake)
include(../pimoroni-pico/pimoroni_pico_import.cmake)

project(${NAME} C CXX ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# initialize all the pimoroni drivers and libraries and stuff
include(libraries/pico_display_2/pico_display_2)
include(libraries/pico_graphics/pico_graphics)
include(drivers/rgbled/rgbled)
include(drivers/st7789/st7789)
include(drivers/button/button)
# required commons that aren't imported upstream
include(libraries/bitmap_fonts/bitmap_fonts)
include(libraries/hershey_fonts/hershey_fonts)
include(common/pimoroni_bus)

# rest of your project
add_executable(${NAME} main.cpp)

target_include_directories(wifi-probe PRIVATE .)

target_link_libraries(${NAME}
        pico_stdlib
        pico_multicore
        pico_sync
        pico_graphics
        pico_cyw43_arch_lwip_threadsafe_background
#        pico_cyw43_arch_lwip_poll
        pico_display_2
        rgbled
        st7789
        button
        )

# enable usb output
pico_enable_stdio_usb(${NAME} 1)
pico_enable_stdio_uart(${NAME} 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${NAME})
