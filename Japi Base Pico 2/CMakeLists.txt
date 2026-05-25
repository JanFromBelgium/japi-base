cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)
project(japi_base C CXX ASM)
pico_sdk_init()

add_executable(japi_base
    main.c
    japi_base.c
    hw_config.c
    third_party_libs.c
)

pico_generate_pio_header(japi_base ${CMAKE_CURRENT_LIST_DIR}/japi_base.pio)

pico_enable_stdio_uart(japi_base 1)
pico_enable_stdio_usb(japi_base 0)

target_link_libraries(japi_base
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_vreg
    hardware_irq
    hardware_clocks
    hardware_pwm
    hardware_spi
    hardware_flash
    pico_multicore
)

target_include_directories(japi_base PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(japi_base PRIVATE
    LFS_THREADSAFE=1
    LFS_NO_DEBUG=1
)

pico_add_extra_outputs(japi_base)
