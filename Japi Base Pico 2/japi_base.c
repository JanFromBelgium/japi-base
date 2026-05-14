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
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/pwm.h"
#include <math.h>
#include "font_8x12.h"
#include "japi_base.pio.h"
#include "ff.h"
#include "sd_card.h"
#include "pico_lfs.h"
#include "japi_kbd_defaults.h"

// =========================================================================
// HARDWARE PIN CONFIGURATION
// =========================================================================
#define PIN_VSYNC    0
#define PIN_HSYNC    1
#define PIN_RGB_BASE 2
#define PIN_AUDIO_L  8
#define PIN_AUDIO_R  9

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

vga_char_t vga_text_buffer[VGA_ROWS][VGA_COLS];
uint8_t japi_keymap[768] = {0};

static int dma_chan_0;
static int dma_chan_1;
static volatile int scanline_counter = 0;

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
static bool ps2_num_lock    = false;
static bool ps2_scroll_lock = false;

#define ps2_shift_any  (ps2_shift_l || ps2_shift_r)
#define ps2_ctrl_any   (ps2_ctrl_l  || ps2_ctrl_r)

bool japi_has_char(void) {
    return kbd_head != kbd_tail;
}

uint16_t japi_get_char(void) {
    if (kbd_head == kbd_tail) return 0;
    uint16_t c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % JAPI_KBD_BUF_SIZE;
    return c;
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

#define AUDIO_PWM_SLICE   4
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

    gpio_set_function(PIN_AUDIO_L, GPIO_FUNC_PWM);
    gpio_set_function(PIN_AUDIO_R, GPIO_FUNC_PWM);
    pwm_set_wrap(AUDIO_PWM_SLICE, AUDIO_PWM_WRAP - 1);
    pwm_set_chan_level(AUDIO_PWM_SLICE, PWM_CHAN_A, AUDIO_CENTER);
    pwm_set_chan_level(AUDIO_PWM_SLICE, PWM_CHAN_B, AUDIO_CENTER);
    pwm_set_enabled(AUDIO_PWM_SLICE, true);
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
    uint32_t *p32 = (uint32_t *)dest;

    *p32++ = 0;

    vga_char_t *curr_char = &vga_text_buffer[char_row][0];

    for (int col = 0; col < VGA_COLS; col++) {
        uint8_t glyph = font_8x12[curr_char->code][font_line];
        uint32_t fg_word = (uint32_t)curr_char->fg * 0x01010101U;
        uint32_t bg_word = (uint32_t)curr_char->bg * 0x01010101U;

        uint32_t mask = nibble_expand[(glyph >> 4) & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);

        mask = nibble_expand[glyph & 0xF];
        *p32++ = (fg_word & mask) | (bg_word & ~mask);
        curr_char++;
    }

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
                case 0x4A: result = JAPI_KEY_NUM_DIV;  break;
                case 0x5A: result = JAPI_KEY_NUM_ENTER; break;
                case 0x7C: result = JAPI_KEY_PRTSCR;   break;
            }
            ps2_extended = false;
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
            switch (sc) {
                case 0x70: numkey_on = JAPI_KEY_NUM0;    numkey_off = JAPI_KEY_INSERT; break;
                case 0x69: numkey_on = JAPI_KEY_NUM1;    numkey_off = JAPI_KEY_END;    break;
                case 0x72: numkey_on = JAPI_KEY_NUM2;    numkey_off = JAPI_KEY_DOWN;   break;
                case 0x7A: numkey_on = JAPI_KEY_NUM3;    numkey_off = JAPI_KEY_PGDN;   break;
                case 0x6B: numkey_on = JAPI_KEY_NUM4;    numkey_off = JAPI_KEY_LEFT;   break;
                case 0x73: numkey_on = JAPI_KEY_NUM5;    numkey_off = 0;               break;
                case 0x74: numkey_on = JAPI_KEY_NUM6;    numkey_off = JAPI_KEY_RIGHT;  break;
                case 0x6C: numkey_on = JAPI_KEY_NUM7;    numkey_off = JAPI_KEY_HOME;   break;
                case 0x75: numkey_on = JAPI_KEY_NUM8;    numkey_off = JAPI_KEY_UP;     break;
                case 0x7D: numkey_on = JAPI_KEY_NUM9;    numkey_off = JAPI_KEY_PGUP;   break;
                case 0x71: numkey_on = JAPI_KEY_NUM_DOT; numkey_off = JAPI_KEY_DELETE; break;
                case 0x79: numkey_on = JAPI_KEY_NUM_PLUS; numkey_off = JAPI_KEY_NUM_PLUS; break;
                case 0x7B: numkey_on = JAPI_KEY_NUM_MINUS; numkey_off = JAPI_KEY_NUM_MINUS; break;
                case 0x7C: numkey_on = JAPI_KEY_NUM_MUL; numkey_off = JAPI_KEY_NUM_MUL; break;
            }
            if (numkey_on) {
                bool effective_numlock = ps2_shift_any ? !ps2_num_lock : ps2_num_lock;
                result = effective_numlock ? numkey_on : numkey_off;
                if (result) kbd_push(result); continue;
            }
        }

        {
            uint16_t index;
            if (ps2_altgr) index = sc + 512;
            else if (ps2_shift_any) index = sc + 256;
            else index = sc;

            result = (index < 768) ? japi_keymap[index] : 0;
            if (ps2_ctrl_any) {
                if (result >= 'a' && result <= 'z') result = result - 'a' + 1;
                else if (result >= 'A' && result <= 'Z') result = result - 'A' + 1;
            }
            if (!ps2_ctrl_any && ps2_caps_lock) {
                if (result >= 'a' && result <= 'z') result -= 32;
                else if (result >= 'A' && result <= 'Z') result += 32;
            }
            if (result) kbd_push(result);
        }
    }

    // Audio: output sample from play buffer
    audio_sample_t *as = &audio_buf[audio_play_buf][audio_play_idx];
    pwm_set_chan_level(AUDIO_PWM_SLICE, PWM_CHAN_A, as->left);
    pwm_set_chan_level(AUDIO_PWM_SLICE, PWM_CHAN_B, as->right);
    audio_play_idx++;

    // Audio: calculate one sample per scanline (spread across all 806 lines)
    if (audio_calc_idx < AUDIO_SAMPLES) {
        audio_buf[1 - audio_play_buf][audio_calc_idx] = synth_render_sample();
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

void vga_wait_vblank(void) {
    while (scanline_counter < VGA_HEIGHT) tight_loop_contents();
}

void vga_redefine_char(uint8_t code, const uint8_t bitmap[FONT_H]) {
    memcpy(font_8x12[code], bitmap, FONT_H);
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
uint32_t get_fattime(void) { return ((uint32_t)(2026-1980)<<25)|((uint32_t)5<<21)|((uint32_t)3<<16); }
void* ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void* m) { free(m); }

// LittleFS — 360K "floppy" at end of 4MB flash
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

static void lfs_populate_defaults(void) {
    lfs_write_file("config.sys", japi_default_config, strlen(japi_default_config));
    for (int i = 0; i < JAPI_KBD_COUNT; i++) {
        char fname[40];
        snprintf(fname, sizeof(fname), "%s.kbd", japi_kbd_defaults[i].name);
        lfs_write_file(fname, japi_kbd_defaults[i].data, JAPI_KBD_SIZE);
    }
}

static void lfs_init_filesystem(void) {
    lfs_cfg = pico_lfs_init(JAPI_LFS_OFFSET, JAPI_LFS_SIZE);
    if (!lfs_cfg) return;

    // Disable multicore lockout — Core 1 is not running yet
    struct pico_lfs_context *ctx = (struct pico_lfs_context *)lfs_cfg->context;
    ctx->multicore_lockout_enabled = false;

    if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) {
        if (lfs_format(&lfs, lfs_cfg) != LFS_ERR_OK) return;
        if (lfs_mount(&lfs, lfs_cfg) != LFS_ERR_OK) return;
        lfs_populate_defaults();
    } else {
        char buf[64] = {0};
        lfs_file_t f;
        if (lfs_file_open(&lfs, &f, "config.sys", LFS_O_RDONLY) == LFS_ERR_OK) {
            lfs_file_read(&lfs, &f, buf, sizeof(buf) - 1);
            lfs_file_close(&lfs, &f);
        }
        if (strcmp(buf, japi_default_config) != 0)
            lfs_write_file("config.sys", japi_default_config, strlen(japi_default_config));
    }
    lfs_mounted = true;
}

static void lfs_load_keyboard(void) {
    if (!lfs_mounted) return;
    char config_buf[128] = {0};
    char keyboard_layout[32] = "QWERTY_US";

    lfs_file_t f;
    if (lfs_file_open(&lfs, &f, "config.sys", LFS_O_RDONLY) == LFS_ERR_OK) {
        lfs_ssize_t br = lfs_file_read(&lfs, &f, config_buf, sizeof(config_buf) - 1);
        lfs_file_close(&lfs, &f);
        if (br > 0) {
            config_buf[br] = '\0';
            char *match = strstr(config_buf, "KEYBOARD MAPPING = ");
            if (match) {
                char *value = match + 19;
                int i = 0;
                while (value[i] && value[i] != '\r' && value[i] != '\n' && i < 31) {
                    keyboard_layout[i] = value[i];
                    i++;
                }
                keyboard_layout[i] = '\0';
            }
        }
    }

    char kbd_filename[40];
    snprintf(kbd_filename, sizeof(kbd_filename), "%s.kbd", keyboard_layout);
    if (!lfs_read_file(kbd_filename, japi_keymap, JAPI_KBD_SIZE)) {
        snprintf(kbd_filename, sizeof(kbd_filename), "%s.KBD", keyboard_layout);
        lfs_read_file(kbd_filename, japi_keymap, JAPI_KBD_SIZE);
    }
}

void japi_init(void) {
    // --- Clock setup ---
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(260000, true);

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
    uint offset_h = pio_add_program(pio, &vga_hsync_pixels_program);
    uint sm_h = 0;
    pio_sm_config c_h = vga_hsync_pixels_program_get_default_config(offset_h);
    sm_config_set_sideset_pins(&c_h, PIN_HSYNC);
    sm_config_set_out_pins(&c_h, PIN_RGB_BASE, 6);
    sm_config_set_fifo_join(&c_h, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_h, 1.0f);
    sm_config_set_out_shift(&c_h, true, true, 32);

    pio_gpio_init(pio, PIN_HSYNC);
    for (int i = 0; i < 6; i++) pio_gpio_init(pio, PIN_RGB_BASE + i);
    pio_sm_set_consecutive_pindirs(pio, sm_h, PIN_HSYNC,    1, true);
    pio_sm_set_consecutive_pindirs(pio, sm_h, PIN_RGB_BASE, 6, true);
    pio_sm_init(pio, sm_h, offset_h, &c_h);
    pio_sm_put_blocking(pio, sm_h, PIXEL_COUNT);
    pio_sm_exec(pio, sm_h, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm_h, pio_encode_mov(pio_y, pio_osr));

    // --- VSync SM (SM1) ---
    uint offset_v = pio_add_program(pio, &vga_vsync_program);
    uint sm_v = 1;
    pio_sm_config c_v = vga_vsync_program_get_default_config(offset_v);
    sm_config_set_sideset_pins(&c_v, PIN_VSYNC);
    sm_config_set_clkdiv(&c_v, 1.0f);

    pio_gpio_init(pio, PIN_VSYNC);
    pio_sm_set_consecutive_pindirs(pio, sm_v, PIN_VSYNC, 1, true);
    pio_sm_init(pio, sm_v, offset_v, &c_v);
    pio_sm_put_blocking(pio, sm_v, LINE_COUNT);
    pio_sm_exec(pio, sm_v, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm_v, pio_encode_mov(pio_y, pio_osr));

    // --- PS/2 Keyboard SM (SM2) ---
    uint offset_ps2 = pio_add_program(pio, &ps2_keyboard_program);
    pio_sm_config c_ps2 = ps2_keyboard_program_get_default_config(offset_ps2);
    pio_gpio_init(pio, 15);
    gpio_pull_up(15);
    sm_config_set_in_pins(&c_ps2, 15);
    pio_gpio_init(pio, 14);
    gpio_pull_up(14);
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

    // --- LittleFS: load keyboard layout from flash (before Core 1!) ---
    lfs_init_filesystem();
    lfs_load_keyboard();

    // --- Pre-render first two lines ---
    scanline_counter = 0;
    vga_render_line(vga_line_buf_0, 0);
    vga_render_line(vga_line_buf_1, 1);

    // --- Launch Core 1 (VGA engine) ---
    multicore_launch_core1(core1_engine_entry);
    multicore_fifo_pop_blocking();  // Wait for Core 1 to signal ready

    // --- SD card: mount for user access (optional) ---
    sleep_ms(100);
    if (sd_init_driver()) {
        if (f_mount(&fs, "0:", 1) == FR_OK)
            sd_mounted = true;
    }
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
        if (d == 'C' || d == 'c') return FS_SD;
        if (d == 'A' || d == 'a') return FS_LFS;
    }
    return FS_NONE;
}

bool japi_fopen(japi_file_t *f, const char *path, uint8_t mode) {
    const char *p = path;
    f->type = japi_parse_drive(&p);

    if (f->type == FS_SD && sd_mounted) {
        BYTE fa = 0;
        if (mode & JAPI_READ)    fa |= FA_READ;
        if (mode & JAPI_WRITE)   fa |= FA_WRITE | FA_CREATE_ALWAYS;
        if (mode & JAPI_APPEND)  fa |= FA_WRITE | FA_OPEN_APPEND;
        char sdpath[68];
        snprintf(sdpath, sizeof(sdpath), "0:%s", p);
        return f_open(&f->fat, sdpath, fa) == FR_OK;
    }

    if (f->type == FS_LFS && lfs_mounted) {
        int flags = 0;
        if (mode & JAPI_READ)    flags = LFS_O_RDONLY;
        if (mode & JAPI_WRITE)   flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
        if (mode & JAPI_APPEND)  flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
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

void japi_fclose(japi_file_t *f) {
    if (f->type == FS_SD)  f_close(&f->fat);
    if (f->type == FS_LFS) lfs_file_close(&lfs, &f->lfs);
    f->type = FS_NONE;
}

bool japi_remove(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && sd_mounted) {
        char sdpath[68];
        snprintf(sdpath, sizeof(sdpath), "0:%s", p);
        return f_unlink(sdpath) == FR_OK;
    }
    if (drv == FS_LFS && lfs_mounted)
        return lfs_remove(&lfs, p) == LFS_ERR_OK;
    return false;
}

bool japi_mkdir(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && sd_mounted) {
        char sdpath[68];
        snprintf(sdpath, sizeof(sdpath), "0:%s", p);
        return f_mkdir(sdpath) == FR_OK;
    }
    if (drv == FS_LFS && lfs_mounted)
        return lfs_mkdir(&lfs, p) == LFS_ERR_OK;
    return false;
}

bool japi_exists(const char *path) {
    const char *p = path;
    uint8_t drv = japi_parse_drive(&p);
    if (drv == FS_SD && sd_mounted) {
        FILINFO fno;
        char sdpath[68];
        snprintf(sdpath, sizeof(sdpath), "0:%s", p);
        return f_stat(sdpath, &fno) == FR_OK;
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
