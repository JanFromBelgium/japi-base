cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)
project(vga_driver_test C CXX ASM)
pico_sdk_init()

add_executable(vga_driver_test 
    main.c 
    vga_driver.c
    hw_config.c                                
    FatFs_SPI/sd_driver/sd_card.c              
    FatFs_SPI/sd_driver/spi.c
    FatFs_SPI/sd_driver/sd_spi.c       # HIER STAAT HIJ
    FatFs_SPI/sd_driver/crc.c          # EN DEZE OOK
    FatFs_SPI/ff15/source/diskio.c
    FatFs_SPI/ff15/source/ff.c
    FatFs_SPI/ff15/source/ffunicode.c
)

pico_generate_pio_header(vga_driver_test ${CMAKE_CURRENT_LIST_DIR}/vga_scanline.pio)
pico_enable_stdio_uart(vga_driver_test 1)
pico_enable_stdio_usb(vga_driver_test 0)

target_link_libraries(vga_driver_test
    pico_stdlib 
    hardware_pio 
    hardware_dma 
    hardware_vreg 
    hardware_irq 
    hardware_clocks
    hardware_spi
)

target_include_directories(vga_driver_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/FatFs_SPI
    ${CMAKE_CURRENT_LIST_DIR}/FatFs_SPI/sd_driver
    ${CMAKE_CURRENT_LIST_DIR}/FatFs_SPI/ff15/source
)

pico_add_extra_outputs(vga_driver_test)