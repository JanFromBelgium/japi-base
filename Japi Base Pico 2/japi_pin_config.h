// =========================================================================
// GPIO PIN CONFIGURATION VALIDATION
// =========================================================================
//
// Peter Stansfeld's binary_info contribution lets the VGA, audio, keyboard and
// SD-card GPIO assignments be reconfigured with picotool, without rebuilding the
// firmware. That flexibility means the firmware can be handed an invalid pin set
// (a number outside the usable GPIO range, a left audio pin that is not paired
// with its right channel, a sync line that overlaps the RGB block, ...).
//
// This module is the safety net: it validates a candidate configuration and,
// for any subsystem whose pins break a rule, reverts THAT subsystem to its
// compiled-in defaults so the board always comes up usable. It is deliberately
// free of any Pico SDK dependency so the logic can be unit-tested on a host
// (see tests/pin_config_test.c).

#ifndef JAPI_PIN_CONFIG_H
#define JAPI_PIN_CONFIG_H

// The runtime-configurable GPIO assignments, grouped by subsystem. A value is
// filled from the matching picotool-configurable pin_* variable in japi_base.c
// and validated by japi_validate_pins() before any hardware is brought up.
typedef struct {
    int rgb_base;   // VGA: first of 6 consecutive RGB output pins (base..base+5)
    int hsync;      // VGA: horizontal sync line
    int vsync;      // VGA: vertical sync line
    int audio_l;    // Audio: left PWM channel; the right channel is audio_l + 1
    int keyb_data;  // PS/2 keyboard: data line
    int keyb_clk;   // PS/2 keyboard: clock line
    int sd_ss;      // microSD: SPI chip-select (CS) line
} japi_pins_t;

// One bit per subsystem, returned by japi_validate_pins() to report which groups
// were reset to their defaults because their configuration was invalid.
enum {
    JAPI_PINFB_VGA   = 1u << 0,
    JAPI_PINFB_AUDIO = 1u << 1,
    JAPI_PINFB_KEYB  = 1u << 2,
    JAPI_PINFB_SD    = 1u << 3,
};

// Highest valid Bank-0 GPIO on the Raspberry Pi Pico 2 (RP2350A: GP0..GP29).
// The RP2350B variant exposes more pins; raise this if the firmware ever targets
// it. Kept here (not pulled from the SDK) so the validator stays host-testable.
#define JAPI_GPIO_MAX 29

// Validate `cfg` against the GPIO range and the within-group invariants. Every
// subsystem whose configuration breaks a rule has ALL of its pins overwritten
// with the corresponding values from `defaults`, because a partially valid group
// (say, valid RGB pins but an out-of-range sync line) is useless. Returns a
// bitmask of the JAPI_PINFB_* groups that were reset; 0 means everything was
// already valid and `cfg` is left untouched.
//
// Pure function: no hardware or SDK calls, so it can be exercised on a host.
unsigned japi_validate_pins(japi_pins_t *cfg, const japi_pins_t *defaults);

#endif // JAPI_PIN_CONFIG_H
