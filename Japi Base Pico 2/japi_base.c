#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "japi_base.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"   // __dmb() + save_and_disable_interrupts for flash writes
#include "hardware/flash.h"  // flash_range_program/erase for our littlefs block device
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/pwm.h"
#include <math.h>
#include "font_8x12.h"
#include "japi_base.pio.h"
#include "third_party_libs.h"
#include "japi_kbd_defaults.h"
#include "pico/binary_info.h"
#include "japi_pin_config.h"

bi_decl(bi_program_version_string("Japi Base Version"));
bi_decl(bi_program_url("https://github.com/JanFromBelgium/japi-base"));
bi_decl(bi_program_description("Japi Base Description"));

// Define some feature groups
#define FG_VGA      0
#define FG_AUDIO    1
#define FG_SD_CARD  2
#define FG_KEYB     3

// Describe the feature groups
bi_decl(bi_program_feature_group(0x1111, FG_VGA, "VGA"));
bi_decl(bi_program_feature_group(0x1111, FG_AUDIO, "Audio"));
bi_decl(bi_program_feature_group(0x1111, FG_SD_CARD, "SD Card"));
bi_decl(bi_program_feature_group(0x1111, FG_KEYB, "Keyboard"));

// Add some settings, which are configurable (on `japi_base.UF2` or device)
// using picotool.
bi_decl(bi_ptr_int32(0x1111, FG_VGA, pin_rgb_base, PIN_RGB_BASE));
bi_decl(bi_ptr_int32(0x1111, FG_VGA, pin_hsync, PIN_HSYNC));
bi_decl(bi_ptr_int32(0x1111, FG_VGA, pin_vsync, PIN_VSYNC));

bi_decl(bi_ptr_int32(0x1111, FG_AUDIO, pin_audio_l, PIN_AUDIO_L));

bi_decl(bi_ptr_int32(0x1111, FG_KEYB, pin_keyb_data, PIN_KEYB_DATA));
bi_decl(bi_ptr_int32(0x1111, FG_KEYB, pin_keyb_clk, PIN_KEYB_CLK));

bi_decl(bi_ptr_int32(0x1111, FG_SD_CARD, pin_sd_ss, PIN_SD_SS));
bi_decl(bi_1pin_with_name(PIN_SD_MISO, "PIN_SD_MISO"));
bi_decl(bi_1pin_with_name(PIN_SD_MOSI, "PIN_SD_MOSI"));
bi_decl(bi_1pin_with_name(PIN_SD_SCK, "PIN_SD_SCK"));

// =========================================================================
// INTERNAL STATE
// =========================================================================

#define TOTAL_LINES   806
#define PIXEL_COUNT  1023
#define LINE_COUNT    767

static uint8_t vga_line_buf_0[VGA_WIDTH] __attribute__((section(".scratch_x")))
                                         __attribute__((aligned(4)));
static uint8_t vga_line_buf_1[VGA_WIDTH] __attribute__((section(".scratch_y")))
                                         __attribute__((aligned(4)));
static uint32_t nibble_expand[16] __attribute__((section(".data")));

/* Double-buffered text buffer. Apps write to vga_text_buffer; the scanline
   render reads from vga_text_active. vga_update() copies one into the
   other during the vertical blank, so each frame is shown atomically and
   render time can exceed one frame without tearing. */
vga_char_t vga_text_buffer[VGA_ROWS][VGA_COLS];
static vga_char_t vga_text_active[VGA_ROWS][VGA_COLS];

uint8_t japi_keymap[768] = {0};

static int dma_chan_0;
static int dma_chan_1;
static volatile int scanline_counter = 0;

// =========================================================================
// CPU CLOCK STATE (runtime 260 <-> 324 MHz switch with watchdog safety net)
// =========================================================================
// Three tiers, each with its own core voltage and VGA program (cycles/pixel):
//   260 MHz @ 1.15 V  (k=4, the safe floor)  <- default / fallback landing
//   324 MHz @ 1.20 V  (k=5, the default high gear)
//   390 MHz @ 1.30 V  (k=6, opt-in up-size; 65x6 -> exact 65 MHz dot clock)
// The desired tier is persisted in flash ("cpuclock.cfg") and applied on the
// next boot. Any tier above the floor is guarded by the hardware watchdog:
// scratch[0] holds a marker that survives a watchdog reset (but not a power
// cycle), so the boot code can tell an intentional reboot (API) from a hang and
// step the failing board down one tier (390->324, 324->260).
#define JAPI_WD_TRYING_324  0x4A325452u  // "J2TR": a 324 attempt is in progress
#define JAPI_WD_TRYING_390  0x4A395452u  // "J9TR": a 390 attempt is in progress
#define JAPI_WD_CLEAN       0x4A434C4Eu  // "JCLN": API rebooted us on purpose
#define JAPI_WD_REVERTED    0x4A525456u  // "JRTV": last attempt was reverted
#define JAPI_WD_TIMEOUT_MS  1000         // ample vs the 60 Hz heartbeat

static int  japi_active_clock_mhz       = 260;
static bool japi_wd_armed               = false;
static bool japi_clock_reverted         = false;
static int  japi_clock_reverted_from_mhz = 0;   // tier that failed (0 = none)

static uint32_t japi_trying_marker(int mhz) {
    return (mhz == 390) ? JAPI_WD_TRYING_390 : JAPI_WD_TRYING_324;
}

static enum vreg_voltage japi_voltage_for(int mhz) {
    if (mhz >= 390) return VREG_VOLTAGE_1_30;
    if (mhz >= 324) return VREG_VOLTAGE_1_20;
    return VREG_VOLTAGE_1_15;   // 260 MHz floor
}

// =========================================================================
// KEYBOARD ENGINE
// =========================================================================

#define JAPI_KBD_BUF_SIZE 64
static uint16_t kbd_buffer[JAPI_KBD_BUF_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

static bool    ps2_break_code  = false;
static bool    ps2_extended    = false;
static bool    ps2_e1_state    = false;
static uint8_t ps2_e1_count    = 0;

static bool ps2_shift_l     = false;
static bool ps2_shift_r     = false;
static bool ps2_ctrl_l      = false;
static bool ps2_ctrl_r      = false;
static bool ps2_alt_l       = false;
static bool ps2_altgr       = false;
static bool ps2_gui_l       = false;
static bool ps2_gui_r       = false;
static bool ps2_menu        = false;

static bool ps2_caps_lock   = false;
static bool ps2_num_lock    = true;   // default ON (matches PC convention); toggle with NumLock key
static bool ps2_scroll_lock = false;

#define ps2_shift_any  (ps2_shift_l || ps2_shift_r)
#define ps2_ctrl_any   (ps2_ctrl_l  || ps2_ctrl_r)

/* PrintScreen is a global screenshot hotkey. The Core 1 keyboard ISR raises
   this request (it must not do slow SD I/O), and Core 0 services it in
   vga_update() -- see the screenshot section at the end of this file. */
static volatile bool screenshot_request = false;
static void japi_capture_screenshot(void);

bool japi_has_char(void) {
    return kbd_head != kbd_tail;
}

uint16_t japi_get_char(void) {
    if (kbd_head == kbd_tail) return 0;
    uint16_t c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % JAPI_KBD_BUF_SIZE;
    return c;
}

uint8_t japi_kbd_modifier_state(void) {
    return (ps2_shift_l ? JAPI_MOD_SHIFT_L : 0)
         | (ps2_shift_r ? JAPI_MOD_SHIFT_R : 0)
         | (ps2_ctrl_l  ? JAPI_MOD_CTRL_L  : 0)
         | (ps2_ctrl_r  ? JAPI_MOD_CTRL_R  : 0)
         | (ps2_alt_l   ? JAPI_MOD_ALT_L   : 0)
         | (ps2_altgr   ? JAPI_MOD_ALTGR   : 0)
         | (ps2_gui_l   ? JAPI_MOD_GUI_L   : 0)
         | (ps2_gui_r   ? JAPI_MOD_GUI_R   : 0);
}

static inline void __not_in_flash_func(kbd_push)(uint16_t c) {
    int next = (kbd_head + 1) % JAPI_KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

// =========================================================================
// AUDIO ENGINE
// =========================================================================

// audio_pwm_slice is intitialised in audio_init and depends on the value of
// pin_audio_l, which is set to PIN_AUDIO_L (unless configured using picotool).
static uint32_t audio_pwm_slice;
#define AUDIO_PWM_WRAP    6500
#define AUDIO_SAMPLES     806
#define AUDIO_SAMPLE_RATE 48360
#define AUDIO_CENTER      3250
#define AUDIO_AMPLITUDE   3249
#define WAVE_TABLE_SIZE   256
#define PHASE_INC_HZ      88821u

#define ENV_IDLE    0
#define ENV_ATTACK  1
#define ENV_DECAY   2
#define ENV_SUSTAIN 3
#define ENV_RELEASE 4

static int16_t wave_sine[WAVE_TABLE_SIZE];
static int16_t wave_square[WAVE_TABLE_SIZE];
static int16_t wave_saw[WAVE_TABLE_SIZE];
static int16_t wave_triangle[WAVE_TABLE_SIZE];
static int16_t *wave_ptrs[4];

typedef struct {
    uint16_t attack_rate;
    uint16_t decay_rate;
    uint16_t sustain_level;
    uint16_t release_rate;
    uint16_t level;
    uint8_t  phase;
} audio_env_t;

typedef struct {
    uint32_t    phase;
    uint32_t    step;
    int16_t    *wavetable;
    uint8_t     volume;
    uint8_t     pan;
    audio_env_t env;
    uint32_t    auto_off;
} audio_melody_t;

static audio_melody_t melody_ch[JAPI_SOUND_CHANNELS];

static const uint16_t midi_to_hz[] = {
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0,
      33,   35,   37,   39,   41,   44,   46,   49,
      52,   55,   58,   62,   65,   69,   73,   78,
      82,   87,   93,   98,  104,  110,  117,  123,
     131,  139,  147,  156,  165,  175,  185,  196,
     208,  220,  233,  247,  262,  277,  294,  311,
     330,  349,  370,  392,  415,  440,  466,  494,
     523,  554,  587,  622,  659,  698,  740,  784,
     831,  880,  932,  988, 1047, 1109, 1175, 1245,
    1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
    2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136,
    3322, 3520, 3729, 3951,
};

typedef struct { uint16_t left; uint16_t right; } audio_sample_t;
static audio_sample_t audio_buf[2][AUDIO_SAMPLES];
static int audio_play_buf = 0;
static int audio_play_idx = 0;
static int audio_calc_idx = 0;

static uint16_t audio_ms_to_rate(uint16_t ms) {
    if (ms == 0) return 65535;
    uint32_t samples = (uint32_t)ms * AUDIO_SAMPLE_RATE / 1000;
    if (samples == 0) return 65535;
    return (uint16_t)(65535 / samples);
}

static inline uint16_t __not_in_flash_func(env_tick)(audio_env_t *e) {
    switch (e->phase) {
        case ENV_ATTACK:
            if (e->level <= 65535 - e->attack_rate)
                e->level += e->attack_rate;
            else { e->level = 65535; e->phase = ENV_DECAY; }
            break;
        case ENV_DECAY:
            if (e->level > e->sustain_level + e->decay_rate)
                e->level -= e->decay_rate;
            else { e->level = e->sustain_level; e->phase = ENV_SUSTAIN; }
            break;
        case ENV_SUSTAIN:
            break;
        case ENV_RELEASE:
            if (e->level > e->release_rate)
                e->level -= e->release_rate;
            else { e->level = 0; e->phase = ENV_IDLE; }
            break;
        default:
            e->level = 0;
    }
    return e->level;
}

static inline audio_sample_t __not_in_flash_func(synth_render_sample)(void) {
    int32_t left = 0, right = 0;

    for (int i = 0; i < JAPI_SOUND_CHANNELS; i++) {
        audio_melody_t *ch = &melody_ch[i];
        if (ch->env.phase == ENV_IDLE) continue;
        if (ch->auto_off > 0) {
            ch->auto_off--;
            if (ch->auto_off == 0) ch->env.phase = ENV_RELEASE;
        }
        ch->phase += ch->step;
        int16_t wave = ch->wavetable[(ch->phase >> 24) & 0xFF];
        uint16_t env = env_tick(&ch->env);
        int32_t val = ((int32_t)wave * (env >> 8) * ch->volume) >> 16;
        left  += (val * (255 - ch->pan)) >> 8;
        right += (val * ch->pan) >> 8;
    }

    int32_t lc = left + AUDIO_CENTER;
    int32_t rc = right + AUDIO_CENTER;
    if (lc < 0) lc = 0; if (lc > 6499) lc = 6499;
    if (rc < 0) rc = 0; if (rc > 6499) rc = 6499;
    audio_sample_t s = { (uint16_t)lc, (uint16_t)rc };
    return s;
}

static void audio_init(void) {
    audio_pwm_slice = PWM_GPIO_SLICE_NUM(pin_audio_l);
    for (int i = 0; i < WAVE_TABLE_SIZE; i++) {
        float angle = 2.0f * 3.14159265f * i / WAVE_TABLE_SIZE;
        wave_sine[i]     = (int16_t)(sinf(angle) * AUDIO_AMPLITUDE);
        wave_square[i]   = (i < 128) ? AUDIO_AMPLITUDE : -AUDIO_AMPLITUDE;
        wave_saw[i]      = (int16_t)((i * AUDIO_AMPLITUDE * 2 / 255) - AUDIO_AMPLITUDE);
        wave_triangle[i] = (i < 128)
            ? (int16_t)(-AUDIO_AMPLITUDE + i * AUDIO_AMPLITUDE * 2 / 127)
            : (int16_t)(AUDIO_AMPLITUDE - (i - 128) * AUDIO_AMPLITUDE * 2 / 127);
    }
    wave_ptrs[JAPI_WAVE_SINE]     = wave_sine;
    wave_ptrs[JAPI_WAVE_SQUARE]   = wave_square;
    wave_ptrs[JAPI_WAVE_SAW]      = wave_saw;
    wave_ptrs[JAPI_WAVE_TRIANGLE] = wave_triangle;

    for (int i = 0; i < JAPI_SOUND_CHANNELS; i++) {
        melody_ch[i].wavetable = wave_sine;
        melody_ch[i].volume = 200;
        melody_ch[i].pan = 128;
        melody_ch[i].env.phase = ENV_IDLE;
        melody_ch[i].env.attack_rate  = audio_ms_to_rate(10);
        melody_ch[i].env.decay_rate   = audio_ms_to_rate(100);
        melody_ch[i].env.sustain_level = 45000;
        melody_ch[i].env.release_rate = audio_ms_to_rate(200);
        melody_ch[i].auto_off = 0;
    }

    for (int i = 0; i < AUDIO_SAMPLES; i++) {
        audio_buf[0][i].left  = AUDIO_CENTER;
        audio_buf[0][i].right = AUDIO_CENTER;
        audio_buf[1][i].left  = AUDIO_CENTER;
        audio_buf[1][i].right = AUDIO_CENTER;
    }

    gpio_set_function(pin_audio_l, GPIO_FUNC_PWM);
    gpio_set_function(pin_audio_l + 1, GPIO_FUNC_PWM);
    pwm_set_wrap(audio_pwm_slice, AUDIO_PWM_WRAP - 1);
    pwm_set_chan_level(audio_pwm_slice, PWM_CHAN_A, AUDIO_CENTER);
    pwm_set_chan_level(audio_pwm_slice, PWM_CHAN_B, AUDIO_CENTER);
    pwm_set_enabled(audio_pwm_slice, true);
}

// =========================================================================
// BITMAP OVERLAY STATE
// =========================================================================


// Volatile: Core 1's scanline render gates on this. It is published last (after
// all geometry fields) with a __dmb() so the render never sees a buffer with
// stale size/offset fields. The render snapshots it once per line.
static uint8_t * volatile bitmap_buf = NULL; // the buffer the scanline render reads (front)
static uint8_t *bitmap_work = NULL; // back buffer the program draws into (double-buffer only)
static bool bitmap_double   = false;// true => draw into bitmap_work, swap on vga_update()
static int bitmap_px_x   = 0;   // screen pixel offset (x) of top-left
static int bitmap_px_y   = 0;   // screen pixel offset (y) of top-left
static int bitmap_px_w   = 0;   // width in screen pixels
static int bitmap_px_h   = 0;   // height in screen pixels
static int bitmap_log_w  = 0;   // logical (buffer) width in pixels
static int bitmap_log_h  = 0;   // logical (buffer) height in pixels
static int bitmap_scale  = 1;   // 1 or 2

// The buffer the program draws into: the back buffer when double-buffered,
// otherwise the single (live) buffer.
static uint8_t *bitmap_draw_target(void) {
    return bitmap_double ? bitmap_work : bitmap_buf;
}

// =========================================================================
// VGA RENDERING
// =========================================================================

static void __not_in_flash_func(vga_render_line)(uint8_t *dest, int line_idx) {
    if (line_idx < 0 || line_idx >= VGA_HEIGHT) {
        memset(dest, 0, VGA_WIDTH);
        return;
    }

    int char_row  = line_idx / FONT_H;
    int font_line = line_idx % FONT_H;

    // Layout: text flush-left from byte 0; 127 cols × 8 = 1016 px text +
    // 8 px trailing black to fill the 1024-byte DMA buffer.
    // bitmap_px_x is column-aligned (multiple of FONT_W) so split-rendering
    // works cleanly: render text up to start_col, then bitmap directly into
    // dest, then text from end_col onwards. No wasted writes under the bitmap.

    int bm_start = VGA_COLS;   // first text col covered by bitmap (= no bitmap)
    int bm_end   = VGA_COLS;   // first text col after bitmap
    int bm_sy    = 0;
    uint8_t *bmp = bitmap_buf; // snapshot the gate once; geometry is valid if non-NULL
    if (bmp) {
        int sy = line_idx - bitmap_px_y;
        if (sy >= 0 && sy < bitmap_px_h) {
            bm_start = bitmap_px_x / FONT_W;
            bm_end   = bm_start + bitmap_px_w / FONT_W;
            bm_sy    = sy;
        }
    }

    vga_char_t *curr_char = &vga_text_active[char_row][0];
    uint32_t   *p32       = (uint32_t *)dest;

    // --- Text segment 1: cols [0, bm_start) ---
    for (int col = 0; col < bm_start; col++) {
        uint8_t glyph = font_8x12[curr_char->code][font_line];
        uint32_t fg_word = (uint32_t)curr_char->fg * 0x01010101U;
        uint32_t bg_word = (uint32_t)curr_char->bg * 0x01010101U;
        uint32_t mask = nibble_expand[(glyph >> 4) & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);
        mask = nibble_expand[glyph & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);
        curr_char++;
    }

    // --- Bitmap segment (only if active) ---
    if (bm_start < VGA_COLS) {
        if (bitmap_scale == 1) {
            memcpy(dest + bitmap_px_x,
                   bmp + bm_sy * bitmap_log_w,
                   bitmap_log_w);
        } else {
            const uint32_t *src32 = (const uint32_t *)(bmp + (bm_sy >> 1) * bitmap_log_w);
            uint32_t *dst32 = (uint32_t *)(dest + bitmap_px_x);
            int n4 = bitmap_log_w >> 2;
            for (int i = 0; i < n4; i++) {
                uint32_t v  = *src32++;
                uint32_t p0 = v & 0xFFu;
                uint32_t p1 = (v >> 8)  & 0xFFu;
                uint32_t p2 = (v >> 16) & 0xFFu;
                uint32_t p3 = v >> 24;
                *dst32++ = (p0 * 0x0101u) | (p1 * 0x01010000u);
                *dst32++ = (p2 * 0x0101u) | (p3 * 0x01010000u);
            }
        }
        // Reposition for text segment 2
        curr_char = &vga_text_active[char_row][bm_end];
        p32       = (uint32_t *)(dest + bm_end * FONT_W);
    }

    // --- Text segment 2: cols [bm_end, VGA_COLS) ---
    for (int col = bm_end; col < VGA_COLS; col++) {
        uint8_t glyph = font_8x12[curr_char->code][font_line];
        uint32_t fg_word = (uint32_t)curr_char->fg * 0x01010101U;
        uint32_t bg_word = (uint32_t)curr_char->bg * 0x01010101U;
        uint32_t mask = nibble_expand[(glyph >> 4) & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);
        mask = nibble_expand[glyph & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);
        curr_char++;
    }

    // Trailing 8 px black to fill 1024-byte DMA buffer.
    *p32++ = 0;
    *p32++ = 0;
}

// =========================================================================
// DMA INTERRUPT HANDLER (runs on Core 1)
// =========================================================================

static void __not_in_flash_func(vga_dma_handler)(void) {
    if (dma_hw->ints1 & (1u << dma_chan_0)) {
        dma_hw->ints1 = (1u << dma_chan_0);
        dma_channel_set_read_addr(dma_chan_0, vga_line_buf_0, false);
        int next = scanline_counter + 2;
        if (next >= TOTAL_LINES) next -= TOTAL_LINES;
        vga_render_line(vga_line_buf_0, next);
    } else if (dma_hw->ints1 & (1u << dma_chan_1)) {
        dma_hw->ints1 = (1u << dma_chan_1);
        dma_channel_set_read_addr(dma_chan_1, vga_line_buf_1, false);
        int next = scanline_counter + 2;
        if (next >= TOTAL_LINES) next -= TOTAL_LINES;
        vga_render_line(vga_line_buf_1, next);
    }

    scanline_counter++;
    if (scanline_counter >= TOTAL_LINES) scanline_counter = 0;

    // Resync: PIO raises IRQ flag 0 once per frame at start of visible line 0.
    // Polling here (instead of via separate IRQ) avoids priority races with
    // an overrunning DMA handler, and always lands on a safe inter-scanline
    // boundary.
    if (pio0_hw->irq & 1u) {
        pio0_hw->irq = 1u;
        scanline_counter = 0;
    }

    // Watchdog heartbeat (only when a 324 MHz attempt armed it). As long as
    // this Core 1 handler keeps running once per frame the chip is alive; if a
    // glitch freezes it the watchdog reboots and the boot code reverts to 260.
    if (japi_wd_armed && scanline_counter == 0) watchdog_update();

    // Poll PS/2 keyboard FIFO
    while (!pio_sm_is_rx_fifo_empty(pio0, 2)) {
        uint32_t raw_frame = pio_sm_get(pio0, 2);
        uint8_t sc = (raw_frame >> 22) & 0xFF;

        if (sc == 0xE1) { ps2_e1_state = true; ps2_e1_count = 7; continue; }
        if (ps2_e1_state) {
            if (ps2_e1_count > 0) ps2_e1_count--;
            if (ps2_e1_count == 0) { ps2_e1_state = false; kbd_push(JAPI_KEY_PAUSE); }
            continue;
        }

        if (sc == 0xE0) { ps2_extended = true;   continue; }
        if (sc == 0xF0) { ps2_break_code = true; continue; }

        if (ps2_extended && sc == 0x12) {
            ps2_break_code = false; ps2_extended = false; continue;
        }

        if (ps2_break_code) {
            ps2_break_code = false;
            switch (sc) {
                case 0x12: ps2_shift_l = false; break;
                case 0x59: ps2_shift_r = false; break;
                case 0x14: if (ps2_extended) ps2_ctrl_r = false;
                           else ps2_ctrl_l = false; break;
                case 0x11: if (ps2_extended) ps2_altgr = false;
                           else ps2_alt_l = false; break;
                case 0x1F: if (ps2_extended) ps2_gui_l = false; break;
                case 0x27: if (ps2_extended) ps2_gui_r = false; break;
                case 0x2F: if (ps2_extended) ps2_menu = false; break;
            }
            ps2_extended = false; continue;
        }

        uint16_t result = 0;
        if (sc == 0x12) { ps2_shift_l = true; ps2_extended = false; continue; }
        if (sc == 0x59) { ps2_shift_r = true; ps2_extended = false; continue; }
        if (sc == 0x14) {
            if (ps2_extended) ps2_ctrl_r = true; else ps2_ctrl_l = true;
            ps2_extended = false; continue;
        }
        if (sc == 0x11) {
            if (ps2_extended) ps2_altgr = true; else ps2_alt_l = true;
            ps2_extended = false; continue;
        }
        if (ps2_extended && sc == 0x1F) { ps2_gui_l = true; ps2_extended = false; continue; }
        if (ps2_extended && sc == 0x27) { ps2_gui_r = true; ps2_extended = false; continue; }
        if (ps2_extended && sc == 0x2F) { ps2_menu  = true; ps2_extended = false; continue; }

        if (sc == 0x58) { ps2_caps_lock = !ps2_caps_lock; kbd_push(JAPI_KEY_CAPS_LOCK); continue; }
        if (sc == 0x77) { ps2_num_lock  = !ps2_num_lock;  kbd_push(JAPI_KEY_NUM_LOCK);  continue; }
        if (sc == 0x7E) { ps2_scroll_lock = !ps2_scroll_lock; kbd_push(JAPI_KEY_SCROLL_LOCK); continue; }

        if (ps2_extended) {
            switch (sc) {
                case 0x75: result = JAPI_KEY_UP;      break;
                case 0x72: result = JAPI_KEY_DOWN;    break;
                case 0x6B: result = JAPI_KEY_LEFT;    break;
                case 0x74: result = JAPI_KEY_RIGHT;   break;
                case 0x6C: result = JAPI_KEY_HOME;    break;
                case 0x69: result = JAPI_KEY_END;     break;
                case 0x7D: result = JAPI_KEY_PGUP;    break;
                case 0x7A: result = JAPI_KEY_PGDN;    break;
                case 0x70: result = JAPI_KEY_INSERT;  break;
                case 0x71: result = JAPI_KEY_DELETE;   break;
                case 0x4A: result = '/';            break;  /* keypad / always types '/' */
                case 0x5A: result = 0x0D;            break;  /* keypad Enter == Enter */
                case 0x7C:
                    /* PrintScreen: GLOBAL screenshot hotkey. Raise the request
                       and deliver no key, so it works everywhere -- in the
                       editor and while a BASIC program runs. The capture runs
                       on Core 0 in vga_update(); here (Core 1 ISR) we only flag
                       it. result stays 0, so nothing is pushed to the buffer. */
                    screenshot_request = true;
                    break;
            }
            ps2_extended = false;
            // Editor selection: emit modifier variants for the navigation
            // codes only (0x0101..0x010A). Numpad from this switch is out of
            // range and passes through unchanged; PrtScr was intercepted above
            // as a screenshot (result == 0).
            if (result >= JAPI_KEY_UP && result <= JAPI_KEY_DELETE) {
                if (ps2_ctrl_any && ps2_shift_any) result += 0x80;
                else if (ps2_shift_any)            result += 0x60;
                else if (ps2_ctrl_any)             result += 0x70;
            }
            if (result) kbd_push(result);
            continue;
        }

        {
            uint16_t fkey = 0;
            switch (sc) {
                case 0x05: fkey = JAPI_KEY_F1;  break;
                case 0x06: fkey = JAPI_KEY_F2;  break;
                case 0x04: fkey = JAPI_KEY_F3;  break;
                case 0x0C: fkey = JAPI_KEY_F4;  break;
                case 0x03: fkey = JAPI_KEY_F5;  break;
                case 0x0B: fkey = JAPI_KEY_F6;  break;
                case 0x83: fkey = JAPI_KEY_F7;  break;
                case 0x0A: fkey = JAPI_KEY_F8;  break;
                case 0x01: fkey = JAPI_KEY_F9;  break;
                case 0x09: fkey = JAPI_KEY_F10; break;
                case 0x78: fkey = JAPI_KEY_F11; break;
                case 0x07: fkey = JAPI_KEY_F12; break;
            }
            if (fkey) {
                if (ps2_ctrl_any) result = fkey + (JAPI_KEY_CF1 - JAPI_KEY_F1);
                else if (ps2_shift_any) result = fkey + (JAPI_KEY_SF1 - JAPI_KEY_F1);
                else result = fkey;
                kbd_push(result); continue;
            }
        }

        if (sc == 0x76) { kbd_push(JAPI_KEY_ESCAPE); continue; }
        if (sc == 0x5A) { kbd_push(0x0D); continue; }

        {
            uint16_t numkey_on = 0, numkey_off = 0;
            // With Num Lock on, the keypad types plain ASCII digits/operators so
            // it works as a numeric pad everywhere (the editor and BASIC only
            // accept 0x20..0x7E). With Num Lock off it keeps its navigation
            // meaning (arrows/Home/End/PgUp/PgDn/Ins/Del).
            switch (sc) {
                case 0x70: numkey_on = '0'; numkey_off = JAPI_KEY_INSERT; break;
                case 0x69: numkey_on = '1'; numkey_off = JAPI_KEY_END;    break;
                case 0x72: numkey_on = '2'; numkey_off = JAPI_KEY_DOWN;   break;
                case 0x7A: numkey_on = '3'; numkey_off = JAPI_KEY_PGDN;   break;
                case 0x6B: numkey_on = '4'; numkey_off = JAPI_KEY_LEFT;   break;
                case 0x73: numkey_on = '5'; numkey_off = 0;               break;
                case 0x74: numkey_on = '6'; numkey_off = JAPI_KEY_RIGHT;  break;
                case 0x6C: numkey_on = '7'; numkey_off = JAPI_KEY_HOME;   break;
                case 0x75: numkey_on = '8'; numkey_off = JAPI_KEY_UP;     break;
                case 0x7D: numkey_on = '9'; numkey_off = JAPI_KEY_PGUP;   break;
                case 0x71: numkey_on = '.'; numkey_off = JAPI_KEY_DELETE; break;
                case 0x79: numkey_on = '+'; numkey_off = '+'; break;
                case 0x7B: numkey_on = '-'; numkey_off = '-'; break;
                case 0x7C: numkey_on = '*'; numkey_off = '*'; break;
            }
            if (numkey_on) {
                bool effective_numlock = ps2_shift_any ? !ps2_num_lock : ps2_num_lock;
                result = effective_numlock ? numkey_on : numkey_off;
                if (result) kbd_push(result); continue;
            }
        }

        // Physical Tab key (scancode 0x0D): emit plain / Ctrl / Shift variants
        // so applications can tell Ctrl+Tab and Shift+Tab apart from a plain
        // Tab. Done before the keymap lookup so it is independent of the layout.
        // Ctrl+I keeps mapping to TAB below -- it has a different scancode.
        if (sc == 0x0D) {
            if      (ps2_ctrl_any)  result = JAPI_KEY_CTAB;
            else if (ps2_shift_any) result = JAPI_KEY_STAB;
            else                    result = JAPI_KEY_TAB;
            kbd_push(result); continue;
        }

        {
            uint16_t index;
            if (ps2_altgr) index = sc + 512;
            else if (ps2_shift_any) index = sc + 256;
            else index = sc;

            result = (index < 768) ? japi_keymap[index] : 0;
            if (ps2_ctrl_any) {
                // Ctrl + letter   -> JAPI_KEY_CTRL(uppercase letter), with
                //   conventional exceptions: Ctrl+H/I/M still emit BS/TAB/ENTER
                //   so those keys behave like their dedicated counterparts.
                // Ctrl + any other printable ASCII -> JAPI_KEY_CTRL_BASE | c
                //   (so e.g. Ctrl+_, Ctrl+/, Ctrl+1 are distinguishable from the
                //   plain character by the application).
                char up = 0;
                if (result >= 'a' && result <= 'z')      up = result - 32;
                else if (result >= 'A' && result <= 'Z') up = result;
                if (up == 'H')      result = JAPI_KEY_BACKSPACE;
                else if (up == 'I') result = JAPI_KEY_TAB;
                else if (up == 'M') result = JAPI_KEY_ENTER;
                else if (up)        result = JAPI_KEY_CTRL_BASE | up;
                else if (result >= 0x20 && result < 0x80) result = JAPI_KEY_CTRL_BASE | result;
            }
            if (!ps2_ctrl_any && ps2_caps_lock) {
                if (result >= 'a' && result <= 'z') result -= 32;
                else if (result >= 'A' && result <= 'Z') result += 32;
            }
            // Left-Alt + letter -> menu accelerator code (Alt+F, ...).
            // Right-Alt (AltGr) is excluded so the keyboard layout (ù µ £)
            // keeps working unchanged.
            if (ps2_alt_l && !ps2_altgr) {
                if (result >= 'a' && result <= 'z')
                    result = JAPI_KEY_ALT_BASE | (result - 32);
                else if (result >= 'A' && result <= 'Z')
                    result = JAPI_KEY_ALT_BASE | result;
            }
            if (result) kbd_push(result);
        }
    }

    // Audio: output sample from play buffer
    audio_sample_t *as = &audio_buf[audio_play_buf][audio_play_idx];
    pwm_set_chan_level(audio_pwm_slice, PWM_CHAN_A, as->left);
    pwm_set_chan_level(audio_pwm_slice, PWM_CHAN_B, as->right);
    audio_play_idx++;

    // Audio: calculate one sample per scanline (spread across all 806 lines).
    // Skip the synth entirely when every channel is idle: the calc buffer is
    // initialised to AUDIO_CENTER (silence) and stays valid until a note starts.
    if (audio_calc_idx < AUDIO_SAMPLES) {
        bool any_active = false;
        for (int i = 0; i < JAPI_SOUND_CHANNELS; i++) {
            if (melody_ch[i].env.phase != ENV_IDLE) { any_active = true; break; }
        }
        if (any_active) {
            audio_buf[1 - audio_play_buf][audio_calc_idx] = synth_render_sample();
        } else {
            audio_buf[1 - audio_play_buf][audio_calc_idx].left  = AUDIO_CENTER;
            audio_buf[1 - audio_play_buf][audio_calc_idx].right = AUDIO_CENTER;
        }
        audio_calc_idx++;
    }

    // Audio: swap buffers at frame boundary
    if (audio_play_idx >= AUDIO_SAMPLES) {
        audio_play_buf = 1 - audio_play_buf;
        audio_play_idx = 0;
        audio_calc_idx = 0;
    }

}

// =========================================================================
// VGA PUBLIC API
// =========================================================================

void vga_clear(uint8_t fg, uint8_t bg) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_set_char(r, c, 32, fg, bg);
}

void vga_set_char(int row, int col, uint8_t code, uint8_t fg, uint8_t bg) {
    if (row >= 0 && row < VGA_ROWS && col >= 0 && col < VGA_COLS) {
        vga_text_buffer[row][col].code = code;
        vga_text_buffer[row][col].fg   = fg;
        vga_text_buffer[row][col].bg   = bg;
    }
}

void vga_print(int row, int col, const char *str, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= VGA_ROWS) return;
    for (int i = 0; str[i] != 0 && (col + i) < VGA_COLS; i++) {
        vga_set_char(row, col + i, str[i], fg, bg);
    }
}

void vga_update(void) {
    while (scanline_counter < VGA_HEIGHT) tight_loop_contents();
    /* Vblank started — scan is past the visible region. Promote the app's
       write buffer to the active (read-by-scanline) buffer. ~32 KB, well
       within the vertical blanking window. */
    memcpy(vga_text_active, vga_text_buffer, sizeof(vga_text_active));
    /* Double-buffered bitmap: promote the program's back buffer to the screen.
       We are in vblank (scan past the visible region), so swapping the pointer
       the scanline render reads is safe -- and far cheaper than a copy. */
    if (bitmap_double) {
        uint8_t *t = bitmap_buf; bitmap_buf = bitmap_work; bitmap_work = t;
    }

    /* Global screenshot (PrintScreen). The just-presented frame is now in
       vga_text_active and the bitmap is swapped, so this captures exactly
       what is on the monitor. Done here on Core 0 -- the SD write must never
       happen in the Core 1 keyboard ISR that raised the request. */
    if (screenshot_request) {
        screenshot_request = false;
        japi_capture_screenshot();
    }
}

void vga_redefine_char(uint8_t code, const uint8_t bitmap[FONT_H]) {
    memcpy(font_8x12[code], bitmap, FONT_H);
}

// =========================================================================
// BITMAP PUBLIC API
// =========================================================================

bool japi_bitmap_open(int col, int row, int w_chars, int h_chars, int scale,
                      bool double_buffered) {
    if (bitmap_buf) return false;
    if (scale != 1 && scale != 2) return false;
    int pw = w_chars * FONT_W;
    int ph = h_chars * FONT_H;
    if (pw <= 0 || ph <= 0) return false;
    if (col < 0 || row < 0 || col + w_chars > VGA_COLS || row + h_chars > VGA_ROWS)
        return false;
    int lw = pw / scale;
    int lh = ph / scale;
    /* No fixed RAM cap: the heap is the real limit. malloc returns NULL when the
     * buffer does not fit (PICO_MALLOC_PANIC=0), and we report that cleanly to the
     * caller instead of refusing a size that would actually have fit. A program
     * can read the largest free block with the BASIC FREE() function to size a
     * bitmap up front. The 127x64 char grid bounds lw*lh to <= 780288, so the
     * uint32 multiply cannot overflow. */
    uint8_t *buf = malloc((size_t)lw * lh);
    if (!buf) return false;
    uint8_t *work = NULL;
    if (double_buffered) {
        work = malloc(lw * lh);              // second buffer; bail cleanly if it won't fit
        if (!work) { free(buf); return false; }
        memset(work, VGA_BLACK, lw * lh);
    }
    memset(buf, VGA_BLACK, lw * lh);
    bitmap_px_x   = col * FONT_W;  // PADDING TEST: text now flush-left
    bitmap_px_y   = row * FONT_H;
    bitmap_px_w   = pw;
    bitmap_px_h   = ph;
    bitmap_log_w  = lw;
    bitmap_log_h  = lh;
    bitmap_scale  = scale;
    bitmap_double = double_buffered;
    bitmap_work   = work;
    __dmb();                 // publish all geometry before the render can see buf
    bitmap_buf    = buf;     // the render gate: set last
    return true;
}

void japi_bitmap_close(void) {
    uint8_t *buf  = bitmap_buf;
    uint8_t *work = bitmap_work;
    bitmap_buf    = NULL;     // close the render gate first
    __dmb();                  // ensure Core 1 sees NULL before we free / reset
    bitmap_work   = NULL;
    bitmap_double = false;
    // Clear the geometry so a later open never leaves the render reading a
    // fresh buffer with leftover (larger) size fields between the two writes.
    bitmap_px_x = bitmap_px_y = bitmap_px_w = bitmap_px_h = 0;
    bitmap_log_w = bitmap_log_h = 0;
    free(buf);
    free(work);
}

void japi_bitmap_pixel(int x, int y, uint8_t colour) {
    uint8_t *t = bitmap_draw_target();
    if (t && x >= 0 && x < bitmap_log_w && y >= 0 && y < bitmap_log_h)
        t[y * bitmap_log_w + x] = colour;
}

void japi_bitmap_clear(uint8_t colour) {
    uint8_t *t = bitmap_draw_target();
    if (t)
        memset(t, colour, bitmap_log_w * bitmap_log_h);
}

uint8_t *japi_bitmap_buffer(void) {
    return bitmap_draw_target();
}

int japi_bitmap_width(void) {
    return bitmap_log_w;
}

int japi_bitmap_height(void) {
    return bitmap_log_h;
}

// =========================================================================
// SOUND PUBLIC API
// =========================================================================

void japi_sound_wave(int ch, uint8_t type) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS || type >= 4) return;
    melody_ch[ch].wavetable = wave_ptrs[type];
}

void japi_sound_freq(int ch, uint16_t hz) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].step = (uint32_t)hz * PHASE_INC_HZ;
}

void japi_sound_volume(int ch, uint8_t vol) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].volume = vol;
}

void japi_sound_pan(int ch, uint8_t pan) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].pan = pan;
}

void japi_sound_envelope(int ch, uint16_t attack_ms, uint16_t decay_ms,
                         uint8_t sustain, uint16_t release_ms) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].env.attack_rate  = audio_ms_to_rate(attack_ms);
    melody_ch[ch].env.decay_rate   = audio_ms_to_rate(decay_ms);
    melody_ch[ch].env.sustain_level = (uint16_t)sustain * 257;
    melody_ch[ch].env.release_rate = audio_ms_to_rate(release_ms);
}

void japi_sound_note_on(int ch) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].phase = 0;
    melody_ch[ch].env.level = 0;
    melody_ch[ch].env.phase = ENV_ATTACK;
    melody_ch[ch].auto_off = 0;
}

void japi_sound_note_off(int ch) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    melody_ch[ch].env.phase = ENV_RELEASE;
    melody_ch[ch].auto_off = 0;
}

void japi_sound_off(void) {
    for (int i = 0; i < JAPI_SOUND_CHANNELS; i++) {
        melody_ch[i].env.phase = ENV_IDLE;
        melody_ch[i].auto_off = 0;
    }
}

void japi_play_ch(int ch, uint8_t note, uint16_t duration_ms) {
    if (ch < 0 || ch >= JAPI_SOUND_CHANNELS) return;
    if (note == 255) {
        melody_ch[ch].env.phase = ENV_IDLE;
        melody_ch[ch].auto_off = 0;
        return;
    }
    if (note >= sizeof(midi_to_hz) / sizeof(midi_to_hz[0])) return;
    uint16_t hz = midi_to_hz[note];
    if (hz == 0) return;
    melody_ch[ch].step = (uint32_t)hz * PHASE_INC_HZ;
    melody_ch[ch].phase = 0;
    melody_ch[ch].env.level = 0;
    melody_ch[ch].env.phase = ENV_ATTACK;
    melody_ch[ch].auto_off = (uint32_t)duration_ms * AUDIO_SAMPLE_RATE / 1000;
}

void japi_play(uint8_t note, uint16_t duration_ms) {
    japi_play_ch(0, note, duration_ms);
}

// =========================================================================
// CORE 1 ENTRY: VGA ENGINE
// =========================================================================

static void core1_engine_entry(void) {
    // Register DMA IRQ on Core 1
    dma_channel_set_irq1_enabled(dma_chan_0, true);
    dma_channel_set_irq1_enabled(dma_chan_1, true);
    irq_set_exclusive_handler(DMA_IRQ_1, vga_dma_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    // Start DMA and PIO
    dma_channel_start(dma_chan_0);
    pio_sm_set_enabled(pio0, 1, true);  // VSync SM
    pio_sm_set_enabled(pio0, 0, true);  // HSync+Pixel SM

    // Signal Core 0 that engine is running
    multicore_fifo_push_blocking(1);

    // Core 1 idles — all work is done in the DMA IRQ handler
    while (1) tight_loop_contents();
}

// =========================================================================
// JAPI_INIT: called from Core 0
// =========================================================================

// FatFs requirements
static FATFS __attribute__((section(".usb_ram"))) fs;
// FatFs timestamp for files on the SD. Japi Base has no real-time clock, so we
// stamp the FAT epoch -- 1980-01-01 00:00:00 -- the canonical "date not set"
// value. Using the epoch keeps EVERY timestamp field uniformly at the zero
// point (the last-access date FatFs doesn't drive sits at the epoch too), so
// nothing looks like a believable real date. FAT packs the date in the high 16
// bits (year-1980 << 9 | month << 5 | day) and time in the low 16; the year
// field is 0 for 1980 and month/day must be >= 1.
uint32_t get_fattime(void) {
    return ((uint32_t)1 << 21) | ((uint32_t)1 << 16);
}
void* ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void* m) { free(m); }

// LittleFS — 360K built-in media at the end of the 4MB flash
#define JAPI_LFS_SIZE (360 * 1024)
#define JAPI_LFS_OFFSET (PICO_FLASH_SIZE_BYTES - JAPI_LFS_SIZE)
static struct lfs_config *lfs_cfg;
static lfs_t lfs;
static bool lfs_mounted = false;
static bool sd_mounted = false;

static void lfs_write_file(const char *path, const void *data, lfs_size_t size) {
    lfs_file_t f;
    if (lfs_file_open(&lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == LFS_ERR_OK) {
        lfs_file_write(&lfs, &f, data, size);
        lfs_file_close(&lfs, &f);
    }
}

static bool lfs_read_file(const char *path, void *data, lfs_size_t size) {
    lfs_file_t f;
    if (lfs_file_open(&lfs, &f, path, LFS_O_RDONLY) != LFS_ERR_OK)
        return false;
    lfs_ssize_t br = lfs_file_read(&lfs, &f, data, size);
    lfs_file_close(&lfs, &f);
    return br == (lfs_ssize_t)size;
}

// No lfs_populate_defaults(): a fresh filesystem is EMPTY. A QWERTY_US user
// needs no files at all; any other layout is supplied as a <name>.kbd file on
// the removable media (A:) or copied by the user onto the built-in media (C:).

#ifdef JAPI_MIGRATE_CONFIG
// One-shot migration, compiled ONLY into the "migrate" UF2. All built-in config
// and state moved from the loose C: root (and the old C:syntax/ folder) into a
// single C:config/ folder. This relocates a user's existing files there once,
// using lfs_rename (a cheap in-place metadata move -- no data copy). It is
// idempotent: renaming an absent source simply fails and is ignored. After
// booting this UF2 once, flash the clean (non-migrate) firmware.
static void lfs_migrate_to_config(void) {
    if (!lfs_mounted) return;

    char dst[80];

    // Known single config/state files in the root.
    static const char *named[] = { "config.sys", "cpuclock.cfg", "screenshot.num", 0 };
    for (int i = 0; named[i]; i++) {
        snprintf(dst, sizeof dst, "config/%s", named[i]);
        lfs_rename(&lfs, named[i], dst);
    }

    // static: keep the big lfs_info (~264 B) and the name table off the ~2 KB
    // Core 0 stack. Single-threaded boot context, so reuse across both walks.
    static struct lfs_info info;
    static lfs_dir_t       dir;
    static char            names[8][48];
    int n;

    // Every *.kbd in the root. Collect names first -- renaming during the walk
    // would disturb the directory iterator -- then move them.
    n = 0;
    if (lfs_dir_open(&lfs, &dir, "/") >= 0) {
        while (n < 8 && lfs_dir_read(&lfs, &dir, &info) > 0) {
            if (info.type != LFS_TYPE_REG) continue;
            int L = (int)strlen(info.name);
            if (L > 4 && L < (int)sizeof names[0] && info.name[L-4] == '.' &&
                (info.name[L-3]|32) == 'k' && (info.name[L-2]|32) == 'b' &&
                (info.name[L-1]|32) == 'd') {
                strncpy(names[n], info.name, sizeof names[0] - 1);
                names[n][sizeof names[0] - 1] = 0;
                n++;
            }
        }
        lfs_dir_close(&lfs, &dir);
    }
    for (int i = 0; i < n; i++) {
        snprintf(dst, sizeof dst, "config/%s", names[i]);
        lfs_rename(&lfs, names[i], dst);
    }

    // Old C:syntax/* -> C:config/syntax/*, then drop the now-empty syntax/ dir.
    n = 0;
    if (lfs_dir_open(&lfs, &dir, "syntax") >= 0) {
        while (n < 8 && lfs_dir_read(&lfs, &dir, &info) > 0) {
            if (info.type != LFS_TYPE_REG) continue;
            if ((int)strlen(info.name) >= (int)sizeof names[0]) continue;
            strncpy(names[n], info.name, sizeof names[0] - 1);
            names[n][sizeof names[0] - 1] = 0;
            n++;
        }
        lfs_dir_close(&lfs, &dir);
    }
    char src[80];
    for (int i = 0; i < n; i++) {
        snprintf(src, sizeof src, "syntax/%s",        names[i]);
        snprintf(dst, sizeof dst, "config/syntax/%s", names[i]);
        lfs_rename(&lfs, src, dst);
    }
    lfs_remove(&lfs, "syntax");   // empty now -> removed (ignored otherwise)
}
#endif // JAPI_MIGRATE_CONFIG

// ===========================================================================
// littlefs block device for the RP2350 internal flash -- Japi Base's own glue.
//
// littlefs itself is BSD-3-Clause; this thin layer is all that ties it to the
// flash, so the firmware carries no GPL-only code. It just maps littlefs's four
// block-device callbacks onto the Pico SDK's hardware_flash API for our reserved
// region. Geometry matches what the previous adapter used (block = sector =
// 4 KiB, prog = page = 256 B), so a filesystem written before this change stays
// readable. Only Core 0 touches littlefs, so no locking is needed; flash program
// and erase run with interrupts disabled (as before).
// ===========================================================================
static uint32_t japi_lfs_base;                 // byte offset of our region in flash

static int japi_bd_read(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, void *buffer, lfs_size_t size) {
    // Read through the uncached XIP window so a read right after a program or
    // erase never returns stale cached bytes.
    const uint8_t *src = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE
                         + japi_lfs_base + (uint32_t)block * c->block_size + off);
    memcpy(buffer, src, size);
    return LFS_ERR_OK;
}

static int japi_bd_prog(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t addr = japi_lfs_base + (uint32_t)block * c->block_size + off;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(addr, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int japi_bd_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = japi_lfs_base + (uint32_t)block * c->block_size;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(addr, c->block_size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int japi_bd_sync(const struct lfs_config *c) { (void)c; return LFS_ERR_OK; }

// Static config + I/O buffers (no malloc). lfs needs read/prog buffers of
// cache_size and a lookahead buffer of lookahead_size bytes.
static struct lfs_config japi_lfs_config;
static uint8_t japi_lfs_read_buf[FLASH_PAGE_SIZE * 4];
static uint8_t japi_lfs_prog_buf[FLASH_PAGE_SIZE * 4];
static uint8_t japi_lfs_look_buf[32];

// Build the lfs_config for the flash region [offset, offset+size). Both must be
// flash-sector aligned. Returns NULL on bad geometry.
static struct lfs_config *japi_lfs_make_config(uint32_t offset, uint32_t size) {
    if (!size || offset % FLASH_SECTOR_SIZE || size % FLASH_SECTOR_SIZE) return NULL;
    japi_lfs_base = offset;
    struct lfs_config *c = &japi_lfs_config;
    memset(c, 0, sizeof *c);
    c->read  = japi_bd_read;
    c->prog  = japi_bd_prog;
    c->erase = japi_bd_erase;
    c->sync  = japi_bd_sync;
    c->read_size       = 1;
    c->prog_size       = FLASH_PAGE_SIZE;     // 256
    c->block_size      = FLASH_SECTOR_SIZE;   // 4096
    c->block_count     = size / FLASH_SECTOR_SIZE;
    c->block_cycles    = 300;                 // wear-levelling threshold
    c->cache_size      = FLASH_PAGE_SIZE * 4; // 1024
    c->lookahead_size  = 32;
    c->read_buffer     = japi_lfs_read_buf;
    c->prog_buffer     = japi_lfs_prog_buf;
    c->lookahead_buffer = japi_lfs_look_buf;
    return c;
}

static void lfs_init_filesystem(void) {
    set_sd_ss_pin(pin_sd_ss);
    lfs_cfg = japi_lfs_make_config(JAPI_LFS_OFFSET, JAPI_LFS_SIZE);
    if (!lfs_cfg) return;

    if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) {
        // Fresh flash (e.g. right after a UF2 upload): create an empty
        // filesystem and leave it empty — nothing is pre-populated.
        if (lfs_format(&lfs, lfs_cfg) != LFS_ERR_OK) return;
        if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) return;
    }
    lfs_mounted = true;

    // All built-in config + state lives under C:config/ (keeps the C: root
    // clean and consistent with the editor's C:config/syntax/ schemes). Create
    // it up-front so the keyboard / clock / screenshot writers can rely on it.
    // mkdir is idempotent here: an existing folder just returns LFS_ERR_EXIST.
    lfs_mkdir(&lfs, "config");
    lfs_mkdir(&lfs, "config/syntax");

#ifdef JAPI_MIGRATE_CONFIG
    // Migrate-UF2 only: relocate any legacy loose files into C:config/ once,
    // before the clock target and keyboard mapping are read from there below.
    lfs_migrate_to_config();
#endif
}

// Parse a 'KEYBOARD MAPPING = <name>' line into out[]. True if a name was found.
static bool parse_keyboard_mapping(const char *buf, char *out, int outsize) {
    const char *match = strstr(buf, "KEYBOARD MAPPING = ");
    if (!match) return false;
    const char *value = match + 19;
    int i = 0;
    while (value[i] && value[i] != '\r' && value[i] != '\n' && i < outsize - 1) {
        out[i] = value[i];
        i++;
    }
    out[i] = '\0';
    return i > 0;
}

// Keyboard layout selection (see the flow-keyboard-target diagram):
//   built-in QWERTY_US  ->  C: (LFS) config.sys + <name>.kbd  ->  A: (SD) ditto.
// A mapping is only applied when its <name>.kbd file actually lives on that
// medium, so a QWERTY_US user needs no files and an empty filesystem is fine.
// removable media (A:) wins over the built-in media (C:).
static void lfs_load_keyboard(void) {
    char name[32];
    /* static: a FatFs FIL is ~550 B (it holds a 512-byte sector buffer) and the
       keymap copy is 768 B. Two FILs + tmp on the stack here would be ~2 KB,
       right at the Core 0 stack limit. This runs once at init (single-threaded),
       so static storage is safe and keeps it well off the stack. */
    static uint8_t tmp[JAPI_KBD_SIZE];

    // 0. Built-in default — the world's most common layout.
    memcpy(japi_keymap, kbd_QWERTY_US, JAPI_KBD_SIZE);

    // 1. Built-in media (C: / LFS): a user-written config.sys may name a
    //    mapping whose <name>.kbd file is also stored on the built-in media.
    if (lfs_mounted) {
        char buf[128] = {0};
        lfs_file_t f;
        if (lfs_file_open(&lfs, &f, "config/config.sys", LFS_O_RDONLY) == LFS_ERR_OK) {
            lfs_ssize_t br = lfs_file_read(&lfs, &f, buf, sizeof(buf) - 1);
            lfs_file_close(&lfs, &f);
            if (br > 0) {
                buf[br] = '\0';
                if (parse_keyboard_mapping(buf, name, sizeof(name))) {
                    char fn[48];
                    snprintf(fn, sizeof(fn), "config/%s.kbd", name);
                    if (lfs_read_file(fn, tmp, JAPI_KBD_SIZE))
                        memcpy(japi_keymap, tmp, JAPI_KBD_SIZE);
                }
            }
        }
    }

    // 2. Removable media (A: / SD): same scheme, and it overrides C:.
    if (sd_mounted) {
        static FIL cf;        /* static: keep the ~550-byte FatFs FIL off the stack */
        if (f_open(&cf, "0:/config.sys", FA_READ) == FR_OK) {
            char buf[128] = {0};
            UINT br = 0;
            if (f_read(&cf, buf, sizeof(buf) - 1, &br) == FR_OK && br > 0) {
                buf[br] = '\0';
                if (parse_keyboard_mapping(buf, name, sizeof(name))) {
                    char fn[48];
                    snprintf(fn, sizeof(fn), "0:/%s.kbd", name);
                    static FIL kf;   /* static: keep the ~550-byte FIL off the stack */
                    if (f_open(&kf, fn, FA_READ) == FR_OK) {
                        UINT rd = 0;
                        if (f_read(&kf, tmp, JAPI_KBD_SIZE, &rd) == FR_OK &&
                            rd == JAPI_KBD_SIZE)
                            memcpy(japi_keymap, tmp, JAPI_KBD_SIZE);
                        f_close(&kf);
                    }
                }
            }
            f_close(&cf);
        }
    }
}

// --- CPU clock: persistent target + runtime switch API ---
// Stored in its own little file rather than config.sys, which is user-owned
// and may be absent.

static int japi_read_clock_target(void) {
    char buf[8] = {0};
    if (lfs_mounted && lfs_read_file("config/cpuclock.cfg", buf, 3)) {
        if (buf[0] == '3' && buf[1] == '9' && buf[2] == '0') return 390;
        if (buf[0] == '3' && buf[1] == '2' && buf[2] == '4') return 324;
        if (buf[0] == '2' && buf[1] == '6' && buf[2] == '0') return 260;
    }
    return 324;   // factory default: the 324 MHz high gear
}

static void japi_write_clock_target(int mhz) {
    if (!lfs_mounted) return;
    const char *s = (mhz == 390) ? "390" : (mhz == 324) ? "324" : "260";
    lfs_write_file("config/cpuclock.cfg", s, 3);
}

int  japi_get_cpu_clock_mhz(void)   { return japi_active_clock_mhz; }
bool japi_clock_was_reverted(void)  { return japi_clock_reverted; }
int  japi_clock_reverted_from(void) { return japi_clock_reverted_from_mhz; }

bool japi_set_cpu_clock(int mhz) {
    if (mhz != 260 && mhz != 324 && mhz != 390) return false;
    japi_write_clock_target(mhz);
    // Mark this reset as intentional so the next boot does not mistake it for a
    // hang, then reboot cleanly to apply the new clock + voltage + VGA program.
    watchdog_hw->scratch[0] = JAPI_WD_CLEAN;
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();   // never reached
}

void japi_init(void) {
    // --- Sanitise the (picotool-configurable) GPIO assignments before any
    //     hardware uses them. An out-of-range or inconsistent pin set for a
    //     subsystem is reverted to the compiled-in defaults so the board always
    //     comes up usable. The pure validator lives in japi_pin_config.c. ---
    japi_pins_t pins = {
        .rgb_base  = pin_rgb_base, .hsync = pin_hsync, .vsync = pin_vsync,
        .audio_l   = pin_audio_l,
        .keyb_data = pin_keyb_data, .keyb_clk = pin_keyb_clk,
        .sd_ss     = pin_sd_ss,
    };
    static const japi_pins_t pin_defaults = {
        .rgb_base  = PIN_RGB_BASE, .hsync = PIN_HSYNC, .vsync = PIN_VSYNC,
        .audio_l   = PIN_AUDIO_L,
        .keyb_data = PIN_KEYB_DATA, .keyb_clk = PIN_KEYB_CLK,
        .sd_ss     = PIN_SD_SS,
    };
    japi_validate_pins(&pins, &pin_defaults);
    pin_rgb_base  = pins.rgb_base;  pin_hsync = pins.hsync;  pin_vsync = pins.vsync;
    pin_audio_l   = pins.audio_l;
    pin_keyb_data = pins.keyb_data; pin_keyb_clk = pins.keyb_clk;
    pin_sd_ss     = pins.sd_ss;

    // --- Filesystem first: the desired CPU clock lives in flash and must be
    //     read before we set the clock and pick the matching VGA program. ---
    lfs_init_filesystem();

    // --- CPU clock selection (watchdog safety net for the 324/390 tiers) ---
    int target = japi_read_clock_target();
    japi_clock_reverted = false;
    japi_clock_reverted_from_mhz = 0;

    // Recovery: an unplanned reset during an attempt at 'target' means this
    // board could not hold it -> step down one tier (390->324, 324->260).
    if (target > 260 && watchdog_hw->scratch[0] == japi_trying_marker(target)) {
        japi_clock_reverted_from_mhz = target;
        target = (target == 390) ? 324 : 260;
        japi_write_clock_target(target);
        japi_clock_reverted = true;
    }

    japi_active_clock_mhz = target;

    // Tiers above the 260 floor are watchdog-guarded attempts. A hang anywhere
    // from here resets within JAPI_WD_TIMEOUT_MS and steps down on the next boot
    // (a 390->324 fallback is itself a fresh 324 attempt, so it can cascade to
    // 260 if needed). The fed heartbeat lives in vga_dma_handler on Core 1.
    if (target > 260) {
        watchdog_hw->scratch[0] = japi_trying_marker(target);
        watchdog_enable(JAPI_WD_TIMEOUT_MS, true);
        japi_wd_armed = true;
    } else {
        watchdog_hw->scratch[0] = japi_clock_reverted ? JAPI_WD_REVERTED
                                                      : JAPI_WD_CLEAN;
    }

    // --- Clock setup --- (raise voltage first, then clock; safe since the SDK
    // boots at ~150 MHz where any tier voltage is more than sufficient)
    vreg_set_voltage(japi_voltage_for(target));
    sleep_ms(10);
    set_sys_clock_khz(target * 1000, true);

    // --- Audio PWM ---
    audio_init();

    // --- Nibble expand LUT ---
    for (int i = 0; i < 16; i++) {
        uint32_t m = 0;
        if (i & 8) m |= 0x000000FF;
        if (i & 4) m |= 0x0000FF00;
        if (i & 2) m |= 0x00FF0000;
        if (i & 1) m |= 0xFF000000;
        nibble_expand[i] = m;
    }

    // --- Bus priority for DMA ---
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    PIO pio = pio0;

    // --- HSync & Pixel SM (SM0) ---
    // The cycles/pixel matches the clock so the dot clock stays ~65 MHz: 4 at
    // 260, 5 at 324, 6 at 390. Only one program is loaded, chosen at runtime.
    uint offset_h;
    pio_sm_config c_h;
    if (japi_active_clock_mhz >= 390) {
        offset_h = pio_add_program(pio, &vga_hsync_pixels_oc390_program);
        c_h = vga_hsync_pixels_oc390_program_get_default_config(offset_h);
    } else if (japi_active_clock_mhz >= 324) {
        offset_h = pio_add_program(pio, &vga_hsync_pixels_oc325_program);
        c_h = vga_hsync_pixels_oc325_program_get_default_config(offset_h);
    } else {
        offset_h = pio_add_program(pio, &vga_hsync_pixels_program);
        c_h = vga_hsync_pixels_program_get_default_config(offset_h);
    }
    uint sm_h = 0;
    sm_config_set_sideset_pins(&c_h, pin_hsync);
    sm_config_set_out_pins(&c_h, pin_rgb_base, 6);
    sm_config_set_fifo_join(&c_h, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_h, 1.0f);
    sm_config_set_out_shift(&c_h, true, true, 32);

    pio_gpio_init(pio, pin_hsync);
    for (int i = 0; i < 6; i++) pio_gpio_init(pio, pin_rgb_base + i);
    pio_sm_set_consecutive_pindirs(pio, sm_h, pin_hsync,    1, true);
    pio_sm_set_consecutive_pindirs(pio, sm_h, pin_rgb_base, 6, true);
    pio_sm_init(pio, sm_h, offset_h, &c_h);
    pio_sm_put_blocking(pio, sm_h, PIXEL_COUNT);
    pio_sm_exec(pio, sm_h, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm_h, pio_encode_mov(pio_y, pio_osr));

    // --- VSync SM (SM1) ---
    uint offset_v = pio_add_program(pio, &vga_vsync_program);
    uint sm_v = 1;
    pio_sm_config c_v = vga_vsync_program_get_default_config(offset_v);
    sm_config_set_sideset_pins(&c_v, pin_vsync);
    sm_config_set_clkdiv(&c_v, 1.0f);

    pio_gpio_init(pio, pin_vsync);
    pio_sm_set_consecutive_pindirs(pio, sm_v, pin_vsync, 1, true);
    pio_sm_init(pio, sm_v, offset_v, &c_v);
    pio_sm_put_blocking(pio, sm_v, LINE_COUNT);
    pio_sm_exec(pio, sm_v, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm_v, pio_encode_mov(pio_y, pio_osr));

    // --- PS/2 Keyboard SM (SM2) ---
    uint offset_ps2 = pio_add_program(pio, &ps2_keyboard_program);
    pio_sm_config c_ps2 = ps2_keyboard_program_get_default_config(offset_ps2);
    pio_gpio_init(pio, pin_keyb_data);
    gpio_pull_up(pin_keyb_data);
    sm_config_set_in_pins(&c_ps2, pin_keyb_data);
    pio_gpio_init(pio, pin_keyb_clk);
    gpio_pull_up(pin_keyb_clk);

    // Configure a JMPPIN, so we can use the `wait jmppin` PIO instruction
    // instead of `wait gpio`, which means we don't need to alter the PIO
    // code if we configure pin_keyb_clk (using picotool).
    sm_config_set_jmp_pin(&c_ps2, pin_keyb_clk);

    // The SM samples the data line on the PS/2 clock's falling edge (wait
    // instructions on GP14), so the clock rate is set by the PS/2 device, not
    // by this divider. Running the SM at full clk_sys (260 MHz) makes it so
    // sensitive that a few-nanosecond glitch on a noisy clock line (long jumper
    // wires, breadboards, USB-to-PS/2 bridges) reads as a false falling edge
    // and corrupts the bit stream. Slowing the SM to ~160 kHz low-pass filters
    // those glitches while still oversampling the 16.7 kHz max PS/2 clock ~10x,
    // so every real edge is caught. Thanks to Raspberry Pi forum user
    // PeterStansfeld for diagnosing this with a USB-to-PS/2 bridge.
    sm_config_set_clkdiv(&c_ps2, clock_get_hz(clk_sys) / 160000);
    sm_config_set_in_shift(&c_ps2, true, true, 11);
    pio_sm_init(pio, 2, offset_ps2, &c_ps2);
    pio_sm_set_enabled(pio, 2, true);

    // --- DMA Ping-Pong ---
    dma_chan_0 = dma_claim_unused_channel(true);
    dma_chan_1 = dma_claim_unused_channel(true);

    dma_channel_config c0 = dma_channel_get_default_config(dma_chan_0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_dreq(&c0, pio_get_dreq(pio, sm_h, true));
    channel_config_set_chain_to(&c0, dma_chan_1);

    dma_channel_config c1 = dma_channel_get_default_config(dma_chan_1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_dreq(&c1, pio_get_dreq(pio, sm_h, true));
    channel_config_set_chain_to(&c1, dma_chan_0);

    dma_channel_configure(dma_chan_0, &c0, &pio->txf[sm_h], vga_line_buf_0,
                          VGA_WIDTH / 4, false);
    dma_channel_configure(dma_chan_1, &c1, &pio->txf[sm_h], vga_line_buf_1,
                          VGA_WIDTH / 4, false);

    // --- Removable media (A: / SD): mount BEFORE the keyboard load so a
    //     config.sys on the card can override the built-in media (C: / LFS).
    //     Must also happen before Core 1. ---
    sleep_ms(100);
    if (sd_init_driver()) {
        if (f_mount(&fs, "0:", 1) == FR_OK)
            sd_mounted = true;
    }

    // --- Keyboard layout: built-in QWERTY_US, optionally overridden by a
    //     <name>.kbd named in config.sys on C: (LFS) then A: (SD). Before Core 1! ---
    lfs_load_keyboard();

    // --- Pre-render first two lines ---
    scanline_counter = 0;
    vga_render_line(vga_line_buf_0, 0);
    vga_render_line(vga_line_buf_1, 1);

    // --- Launch Core 1 (VGA engine) ---
    multicore_launch_core1(core1_engine_entry);
    multicore_fifo_pop_blocking();  // Wait for Core 1 to signal ready
}

// =========================================================================
// UNIFIED FILE I/O API
// =========================================================================

#define FS_NONE 0
#define FS_SD   1
#define FS_LFS  2

static uint8_t japi_parse_drive(const char **path) {
    if ((*path)[1] == ':') {
        char d = (*path)[0];
        *path += 2;
        if (d == 'A' || d == 'a') return FS_SD;   // removable media (SD card)
        if (d == 'C' || d == 'c') return FS_LFS;  // built-in media (LittleFS flash)
    }
    return FS_NONE;
}

// Build the FatFs volume path "0:<p>" for the SD card. Sized for the full
// supported path (JAPI_PATH_MAX) so a long path is never silently truncated --
// the old per-call char[68] buffers were too small. File ops run on Core 0
// only and never nest, so one shared static buffer is safe and also keeps these
// off the small (~2 KB) stack.
static const char *sd_volpath(const char *p) {
    static char buf[JAPI_PATH_MAX + 4];   // "0:" + path + NUL (+ slack)
    snprintf(buf, sizeof buf, "0:%s", p);
    return buf;
}

// Make sure the SD card is mounted, mounting it now if it isn't. Called on every
// A: access, so a card inserted AFTER boot is picked up on the next use instead
// of needing a restart. sd_init_driver() is idempotent (it sets up the SPI once
// and then returns early), and f_mount(..., 1) re-probes the card each call, so
// a now-present card mounts. (Card *removal* is not auto-detected -- A: accesses
// simply fail until a card is back.)
static bool ensure_sd_mounted(void) {
    if (sd_mounted) return true;
    if (sd_init_driver() && f_mount(&fs, "0:", 1) == FR_OK)
        sd_mounted = true;
    return sd_mounted;
}

bool japi_fopen(japi_file_t *f, const char *path, uint8_t mode) {
    const char *p = path;
    f->type = japi_parse_drive(&p);

    /* READ and WRITE together = random-access: open for read+write, create the
       file if absent, and DO NOT truncate existing data (records survive). */
    bool rw = (mode & JAPI_READ) && (mode & JAPI_WRITE);

    if (f->type == FS_SD && ensure_sd_mounted()) {
        BYTE fa;
        if (rw)                       fa = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
        else if (mode & JAPI_APPEND)  fa = FA_WRITE | FA_OPEN_APPEND;
        else if (mode & JAPI_WRITE)   fa = FA_WRITE | FA_CREATE_ALWAYS;
        else                          fa = FA_READ;
        return f_open(&f->fat, sd_volpath(p), fa) == FR_OK;
    }

    if (f->type == FS_LFS && lfs_mounted) {
        int flags;
        if (rw)                       flags = LFS_O_RDWR  | LFS_O_CREAT;
        else if (mode & JAPI_APPEND)  flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
        else if (mode & JAPI_WRITE)   flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
        else                          flags = LFS_O_RDONLY;
        return lfs_file_open(&lfs, &f->lfs, p, flags) == LFS_ERR_OK;
    }

    f->type = FS_NONE;
    return false;
}

int japi_fread(japi_file_t *f, void *buf, int size) {
    if (f->type == FS_SD) {
        UINT br = 0;
        if (f_read(&f->fat, buf, size, &br) == FR_OK) return (int)br;
        return -1;
    }
    if (f->type == FS_LFS) {
        lfs_ssize_t r = lfs_file_read(&lfs, &f->lfs, buf, size);
        return (r >= 0) ? (int)r : -1;
    }
    return -1;
}

int japi_fwrite(japi_file_t *f, const void *buf, int size) {
    if (f->type == FS_SD) {
        UINT bw = 0;
        if (f_write(&f->fat, buf, size, &bw) == FR_OK) return (int)bw;
        return -1;
    }
    if (f->type == FS_LFS) {
        lfs_ssize_t r = lfs_file_write(&lfs, &f->lfs, buf, size);
        return (r >= 0) ? (int)r : -1;
    }
    return -1;
}

// Random-access seek to byte offset `pos` (0-based). Mirrors the fread/fwrite
// dispatch: f_lseek on FAT volumes, lfs_file_seek on LittleFS.
bool japi_fseek(japi_file_t *f, int pos) {
    if (f->type == FS_SD) {
        return f_lseek(&f->fat, (FSIZE_t)pos) == FR_OK;
    }
    if (f->type == FS_LFS) {
        return lfs_file_seek(&lfs, &f->lfs, (lfs_soff_t)pos, LFS_SEEK_SET) >= 0;
    }
    return false;
}

void japi_fclose(japi_file_t *f) {
    if (f->type == FS_SD)  f_close(&f->fat);
    if (f->type == FS_LFS) lfs_file_close(&lfs, &f->lfs);
    f->type = FS_NONE;
}

bool japi_remove(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && ensure_sd_mounted()) {
        return f_unlink(sd_volpath(p)) == FR_OK;
    }
    if (drv == FS_LFS && lfs_mounted)
        return lfs_remove(&lfs, p) == LFS_ERR_OK;
    return false;
}

bool japi_mkdir(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && ensure_sd_mounted()) {
        return f_mkdir(sd_volpath(p)) == FR_OK;
    }
    if (drv == FS_LFS && lfs_mounted)
        return lfs_mkdir(&lfs, p) == LFS_ERR_OK;
    return false;
}

bool japi_exists(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && ensure_sd_mounted()) {
        FILINFO fno;
        return f_stat(sd_volpath(p), &fno) == FR_OK;
    }
    if (drv == FS_LFS && lfs_mounted) {
        struct lfs_info info;
        return lfs_stat(&lfs, p, &info) == LFS_ERR_OK;
    }
    return false;
}

int japi_fsize(japi_file_t *f) {
    if (f->type == FS_SD)  return (int)f_size(&f->fat);
    if (f->type == FS_LFS) return (int)lfs_file_size(&lfs, &f->lfs);
    return -1;
}

/* Directory listing. type field doubles as backend tag (FS_SD / FS_LFS /
   FS_NONE), same as japi_file_t. "." / ".." entries are skipped. */
bool japi_opendir(japi_dir_t *d, const char *path) {
    const char *p = path;
    d->type = japi_parse_drive(&p);
    if (d->type == FS_SD && ensure_sd_mounted()) {
        return f_opendir(&d->fat, sd_volpath(p)) == FR_OK;
    }
    if (d->type == FS_LFS && lfs_mounted) {
        if (lfs_dir_open(&lfs, &d->lfs, *p ? p : "/") == LFS_ERR_OK) return true;
    }
    d->type = FS_NONE;
    return false;
}

bool japi_readdir(japi_dir_t *d, char *name_out, int name_max) {
    if (name_max <= 0) return false;
    if (d->type == FS_SD) {
        FILINFO fno;
        for (;;) {
            if (f_readdir(&d->fat, &fno) != FR_OK || fno.fname[0] == 0) return false;
            if (fno.fname[0] == '.' && (fno.fname[1] == 0 ||
                (fno.fname[1] == '.' && fno.fname[2] == 0))) continue;
            strncpy(name_out, fno.fname, (size_t)name_max - 1);
            name_out[name_max - 1] = 0;
            return true;
        }
    }
    if (d->type == FS_LFS) {
        struct lfs_info info;
        for (;;) {
            int r = lfs_dir_read(&lfs, &d->lfs, &info);
            if (r <= 0) return false;
            if (info.name[0] == '.' && (info.name[1] == 0 ||
                (info.name[1] == '.' && info.name[2] == 0))) continue;
            strncpy(name_out, info.name, (size_t)name_max - 1);
            name_out[name_max - 1] = 0;
            return true;
        }
    }
    return false;
}

void japi_closedir(japi_dir_t *d) {
    if (d->type == FS_SD)  f_closedir(&d->fat);
    if (d->type == FS_LFS) lfs_dir_close(&lfs, &d->lfs);
    d->type = FS_NONE;
}

// =========================================================================
// SCREENSHOT  (PrintScreen -> 8-bit indexed BMP on the SD card)
// =========================================================================
// A faithful capture of exactly what is on the monitor: we re-run the engine's
// own scanline compositor (vga_render_line) for every line and stream the
// result to an 8-bit indexed BMP. The pixel bytes ARE the 6-bit palette values
// the DAC uses, so the BMP's palette holds those same 64 colours and the pixel
// data needs no per-pixel conversion. RAM cost is two static 1 KB buffers; no
// full framebuffer -- the image is streamed straight to the card.
//
// Numbering is a persistent, monotonic counter on C: (LittleFS, the built-in
// flash). It survives reboots and SD-card swaps and is never reused, so shots
// sort chronologically like an old film camera. The image itself goes to A:
// (the removable SD). The counter advances only after a successful save, so a
// failed capture leaves no gap.

static uint8_t shot_row[VGA_WIDTH];     // one scanline; static, never on the ~2 KB stack
static uint8_t shot_pal[256 * 4];       // BMP palette (BGRA); static for the same reason

static inline void shot_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void shot_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void japi_capture_screenshot(void) {
    if (!ensure_sd_mounted()) return;        // image goes to the SD; mount it (maybe just inserted)

    // Next number = current + 1; persisted only after a successful save below.
    uint32_t cur = 0;
    lfs_read_file("config/screenshot.num", &cur, sizeof cur);   // leaves cur=0 if missing
    uint32_t num = cur + 1;

    char path[40];
    snprintf(path, sizeof path, "A:screenshot%04u.bmp", (unsigned)num);

    /* static: a japi_file_t embeds a FatFs FIL (~550 bytes, it carries a
       512-byte sector buffer), and the Core 0 stack is only ~2 KB. This runs
       single-threaded from vga_update() and never nests, so static storage is
       safe and keeps that big object off the stack. */
    static japi_file_t f;
    if (!japi_fopen(&f, path, JAPI_WRITE)) return;       // counter not advanced -> no gap

    const uint32_t W = VGA_WIDTH, H = VGA_HEIGHT;        // 1024 x 768; W % 4 == 0 -> no row padding
    const uint32_t pixel_offset = 14 + 40 + 256 * 4;     // headers + 256-entry palette
    const uint32_t image_size   = W * H;                 // 1 byte / pixel
    const uint32_t file_size    = pixel_offset + image_size;

    // BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40)
    uint8_t hdr[54];
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'B'; hdr[1] = 'M';
    shot_put_u32(hdr + 2,  file_size);
    shot_put_u32(hdr + 10, pixel_offset);
    shot_put_u32(hdr + 14, 40);            // info header size
    shot_put_u32(hdr + 18, W);
    shot_put_u32(hdr + 22, H);             // positive height -> bottom-up rows
    shot_put_u16(hdr + 26, 1);             // planes
    shot_put_u16(hdr + 28, 8);             // bits per pixel (indexed)
    shot_put_u32(hdr + 30, 0);             // BI_RGB (uncompressed)
    shot_put_u32(hdr + 34, image_size);
    shot_put_u32(hdr + 46, 256);           // colours used
    shot_put_u32(hdr + 50, 256);           // colours important
    int ok = 1;
    ok &= japi_fwrite(&f, hdr, sizeof hdr) == (int)sizeof hdr;

    // Palette: index -> RGB of the low 6 bits (RRGGBB), exactly the bits the DAC
    // wires up. Each 2-bit channel (0..3) scales by 85 to 0..255. Stored BGRA.
    for (int i = 0; i < 256; i++) {
        int c = i & 0x3F;
        shot_pal[i * 4 + 0] = (uint8_t)(( c        & 3) * 85);  // B
        shot_pal[i * 4 + 1] = (uint8_t)(((c >> 2)  & 3) * 85);  // G
        shot_pal[i * 4 + 2] = (uint8_t)(((c >> 4)  & 3) * 85);  // R
        shot_pal[i * 4 + 3] = 0;
    }
    ok &= japi_fwrite(&f, shot_pal, (int)sizeof shot_pal) == (int)sizeof shot_pal;

    // Pixel data, bottom-up: rebuild every scanline with the engine's own
    // compositor (text + bitmap overlay) and stream it straight to the card.
    for (int y = (int)H - 1; y >= 0; y--) {
        vga_render_line(shot_row, y);
        ok &= japi_fwrite(&f, shot_row, (int)W) == (int)W;
    }

    japi_fclose(&f);

    // Advance the persistent counter only on a fully successful save -- so a
    // full or failing card leaves no gap in the numbering -- and drop the
    // half-written file otherwise rather than leaving a corrupt screenshot.
    if (ok) lfs_write_file("config/screenshot.num", &num, sizeof num);
    else    japi_remove(path);
}
