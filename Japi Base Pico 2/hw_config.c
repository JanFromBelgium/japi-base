#include "third_party_libs.h"

// --- SPI bus configuration ---
spi_t spi = {
    .hw_inst = spi1,  // Hardware SPI channel 1
    .miso_gpio = 12,  // RX (MISO)
    .mosi_gpio = 11,  // TX (MOSI)
    .sck_gpio = 10,   // SCK
    .baud_rate = 1000000, // 1 MHz: plenty for config-time SD access, very stable

    // Limit drive strength to reduce signal reflections.
    .set_drive_strength = true,
    .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
    .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA
};

// --- SD card configuration ---
sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spi,
        .ss_gpio = 13, // CS (Chip Select)

        // Card detect MUST stay off: otherwise the library installs a
        // background GPIO interrupt that wrecks the VGA scanline timing.
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = -1,

        .set_drive_strength = true,
        .ss_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA
    }
};

// ========================================================
// Required FatFs_SPI interface functions
// ========================================================

// SD card accessors
size_t sd_get_num() {
    return 1;
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num == 0) return &sd_cards[0];
    return NULL;
}

// SPI bus accessors
size_t spi_get_num() {
    return 1;
}

spi_t *spi_get_by_num(size_t num) {
    if (num == 0) return &spi;
    return NULL;
}