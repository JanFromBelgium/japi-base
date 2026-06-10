// =========================================================================
// GPIO PIN CONFIGURATION VALIDATION (implementation)
// =========================================================================
// See japi_pin_config.h for the rationale. Pure C, no SDK dependency.

#include "japi_pin_config.h"

// True when `pin` is a usable Bank-0 GPIO number on this board.
static int pin_in_range(int pin) {
    return pin >= 0 && pin <= JAPI_GPIO_MAX;
}

// True when `pin` falls inside the 6-pin RGB output block [base, base + 5].
static int inside_rgb_block(int pin, int rgb_base) {
    return pin >= rgb_base && pin <= rgb_base + 5;
}

// VGA needs six consecutive in-range RGB pins plus two distinct sync lines that
// do not land on each other or inside the RGB block.
static int vga_group_valid(const japi_pins_t *c) {
    if (!pin_in_range(c->rgb_base) || !pin_in_range(c->rgb_base + 5)) return 0;
    if (!pin_in_range(c->hsync) || !pin_in_range(c->vsync))           return 0;
    if (c->hsync == c->vsync)                                         return 0;
    if (inside_rgb_block(c->hsync, c->rgb_base))                      return 0;
    if (inside_rgb_block(c->vsync, c->rgb_base))                      return 0;
    return 1;
}

// The left and right audio channels (audio_l and audio_l + 1) drive channels A
// and B of one PWM slice, so the left pin must be even and its partner in range.
static int audio_group_valid(const japi_pins_t *c) {
    if (!pin_in_range(c->audio_l) || !pin_in_range(c->audio_l + 1)) return 0;
    if ((c->audio_l & 1) != 0)                                      return 0;
    return 1;
}

// The PS/2 data and clock lines must both be in range and distinct.
static int keyb_group_valid(const japi_pins_t *c) {
    if (!pin_in_range(c->keyb_data) || !pin_in_range(c->keyb_clk)) return 0;
    if (c->keyb_data == c->keyb_clk)                              return 0;
    return 1;
}

// The SD chip-select line just needs to be a valid GPIO.
static int sd_group_valid(const japi_pins_t *c) {
    return pin_in_range(c->sd_ss);
}

unsigned japi_validate_pins(japi_pins_t *cfg, const japi_pins_t *defaults) {
    unsigned fell_back = 0;

    if (!vga_group_valid(cfg)) {
        cfg->rgb_base = defaults->rgb_base;
        cfg->hsync    = defaults->hsync;
        cfg->vsync    = defaults->vsync;
        fell_back |= JAPI_PINFB_VGA;
    }
    if (!audio_group_valid(cfg)) {
        cfg->audio_l = defaults->audio_l;
        fell_back |= JAPI_PINFB_AUDIO;
    }
    if (!keyb_group_valid(cfg)) {
        cfg->keyb_data = defaults->keyb_data;
        cfg->keyb_clk  = defaults->keyb_clk;
        fell_back |= JAPI_PINFB_KEYB;
    }
    if (!sd_group_valid(cfg)) {
        cfg->sd_ss = defaults->sd_ss;
        fell_back |= JAPI_PINFB_SD;
    }
    return fell_back;
}
