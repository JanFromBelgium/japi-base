#ifndef JAPI_BASE_H
#define JAPI_BASE_H

#include <stdint.h>
#include <stdbool.h>
#include "third_party_libs.h"

// =========================================================================
// SD CARD / SPI PIN CONFIGURATION (Japi Base hardware choices)
// =========================================================================
// Wired to spi1 on the left side of the Pico 2 (USB at top).
// Change these if you build a board with different pin assignments;
// the carlk3 SD-over-SPI driver in third_party_libs.c picks them up via
// the spi_t / sd_card_t struct instances defined at the bottom of that file.
#define PIN_SD_MISO     12  // RX
#define PIN_SD_MOSI     11  // TX
#define PIN_SD_SCK      10
#define PIN_SD_SS       13  // CS / Chip Select
#define SD_SPI_BAUD     1000000  // 1 MHz: plenty for config-time SD access, very stable

// =========================================================================
// VGA PIN CONFIGURATION
// =========================================================================
#define PIN_VSYNC       0
#define PIN_HSYNC       1
#define PIN_RGB_BASE    2

// =========================================================================
// AUDIO CONFIGURATION
// =========================================================================
#define PIN_AUDIO_L     8

// =========================================================================
// KEYBOARD PIN CONFIGURATION
// =========================================================================
#define PIN_KEYB_CLK    14
#define PIN_KEYB_DATA   15


// =========================================================================
// SCREEN DIMENSIONS
// =========================================================================
#define VGA_COLS        127
#define VGA_ROWS         64
#define VGA_WIDTH      1024
#define VGA_HEIGHT      768
#define FONT_W            8
#define FONT_H           12

// =========================================================================
// VGA COLOURS (6-bit, RR_GG_BB)
// =========================================================================
#define VGA_BLACK       0x00
#define VGA_WHITE       0x3F
#define VGA_RED         0x30
#define VGA_GREEN       0x0C
#define VGA_BLUE        0x03
#define VGA_YELLOW      0x3C
#define VGA_CYAN        0x0F
#define VGA_MAGENTA     0x33
#define VGA_DARK_BLUE   0x01

// =========================================================================
// SPECIAL KEYS (16-bit, values >= 256)
// =========================================================================

// Navigation
#define JAPI_KEY_UP         0x0101
#define JAPI_KEY_DOWN       0x0102
#define JAPI_KEY_LEFT       0x0103
#define JAPI_KEY_RIGHT      0x0104
#define JAPI_KEY_HOME       0x0105
#define JAPI_KEY_END        0x0106
#define JAPI_KEY_PGUP       0x0107
#define JAPI_KEY_PGDN       0x0108
#define JAPI_KEY_INSERT     0x0109
#define JAPI_KEY_DELETE     0x010A

// Function Keys
#define JAPI_KEY_F1         0x0111
#define JAPI_KEY_F2         0x0112
#define JAPI_KEY_F3         0x0113
#define JAPI_KEY_F4         0x0114
#define JAPI_KEY_F5         0x0115
#define JAPI_KEY_F6         0x0116
#define JAPI_KEY_F7         0x0117
#define JAPI_KEY_F8         0x0118
#define JAPI_KEY_F9         0x0119
#define JAPI_KEY_F10        0x011A
#define JAPI_KEY_F11        0x011B
#define JAPI_KEY_F12        0x011C

// Shifted Function Keys
#define JAPI_KEY_SF1        0x0121
#define JAPI_KEY_SF2        0x0122
#define JAPI_KEY_SF3        0x0123
#define JAPI_KEY_SF4        0x0124
#define JAPI_KEY_SF5        0x0125
#define JAPI_KEY_SF6        0x0126
#define JAPI_KEY_SF7        0x0127
#define JAPI_KEY_SF8        0x0128
#define JAPI_KEY_SF9        0x0129
#define JAPI_KEY_SF10       0x012A
#define JAPI_KEY_SF11       0x012B
#define JAPI_KEY_SF12       0x012C

// Control Function Keys
#define JAPI_KEY_CF1        0x0131
#define JAPI_KEY_CF2        0x0132
#define JAPI_KEY_CF3        0x0133
#define JAPI_KEY_CF4        0x0134
#define JAPI_KEY_CF5        0x0135
#define JAPI_KEY_CF6        0x0136
#define JAPI_KEY_CF7        0x0137
#define JAPI_KEY_CF8        0x0138
#define JAPI_KEY_CF9        0x0139
#define JAPI_KEY_CF10       0x013A
#define JAPI_KEY_CF11       0x013B
#define JAPI_KEY_CF12       0x013C

// Numpad (Num Lock ON)
#define JAPI_KEY_NUM0       0x0140
#define JAPI_KEY_NUM1       0x0141
#define JAPI_KEY_NUM2       0x0142
#define JAPI_KEY_NUM3       0x0143
#define JAPI_KEY_NUM4       0x0144
#define JAPI_KEY_NUM5       0x0145
#define JAPI_KEY_NUM6       0x0146
#define JAPI_KEY_NUM7       0x0147
#define JAPI_KEY_NUM8       0x0148
#define JAPI_KEY_NUM9       0x0149
#define JAPI_KEY_NUM_DOT    0x014A
#define JAPI_KEY_NUM_ENTER  0x014B
#define JAPI_KEY_NUM_PLUS   0x014C
#define JAPI_KEY_NUM_MINUS  0x014D
#define JAPI_KEY_NUM_MUL    0x014E
#define JAPI_KEY_NUM_DIV    0x014F

// System & Lock Events
#define JAPI_KEY_ESCAPE      0x0150
#define JAPI_KEY_TAB         0x0009
#define JAPI_KEY_BACKSPACE   0x0008
#define JAPI_KEY_ENTER       0x000D
#define JAPI_KEY_SPACE       0x0020
#define JAPI_KEY_CAPS_LOCK   0x0151
#define JAPI_KEY_NUM_LOCK    0x0155
#define JAPI_KEY_SCROLL_LOCK 0x0156
#define JAPI_KEY_PRTSCR      0x0152
#define JAPI_KEY_PAUSE       0x0153

// Modified navigation (editor selection / block select).
// Offsets from the base nav codes 0x0101..0x010A:
//   Shift = +0x60, Ctrl = +0x70, Ctrl+Shift = +0x80
#define JAPI_KEY_SUP        0x0161   // Shift + navigation
#define JAPI_KEY_SDOWN      0x0162
#define JAPI_KEY_SLEFT      0x0163
#define JAPI_KEY_SRIGHT     0x0164
#define JAPI_KEY_SHOME      0x0165
#define JAPI_KEY_SEND       0x0166
#define JAPI_KEY_SPGUP      0x0167
#define JAPI_KEY_SPGDN      0x0168
#define JAPI_KEY_SINSERT    0x0169
#define JAPI_KEY_SDELETE    0x016A
#define JAPI_KEY_CUP        0x0171   // Ctrl + navigation
#define JAPI_KEY_CDOWN      0x0172
#define JAPI_KEY_CLEFT      0x0173
#define JAPI_KEY_CRIGHT     0x0174
#define JAPI_KEY_CHOME      0x0175
#define JAPI_KEY_CEND       0x0176
#define JAPI_KEY_CPGUP      0x0177
#define JAPI_KEY_CPGDN      0x0178
#define JAPI_KEY_CINSERT    0x0179
#define JAPI_KEY_CDELETE    0x017A
#define JAPI_KEY_CSUP       0x0181   // Ctrl+Shift + navigation (block select)
#define JAPI_KEY_CSDOWN     0x0182
#define JAPI_KEY_CSLEFT     0x0183
#define JAPI_KEY_CSRIGHT    0x0184
#define JAPI_KEY_CSHOME     0x0185
#define JAPI_KEY_CSEND      0x0186
#define JAPI_KEY_CSPGUP     0x0187
#define JAPI_KEY_CSPGDN     0x0188
#define JAPI_KEY_CSINSERT   0x0189
#define JAPI_KEY_CSDELETE   0x018A

// Left-Alt + letter = menu accelerator (Alt+F = File, ...).
// Code = JAPI_KEY_ALT_BASE | uppercase letter, e.g. JAPI_KEY_ALT('F').
// Right-Alt (AltGr) is unaffected and keeps doing the keyboard layout.
#define JAPI_KEY_ALT_BASE   0x0200
#define JAPI_KEY_ALT(c)     (JAPI_KEY_ALT_BASE | (uint16_t)(c))

// Ctrl + printable ASCII = application shortcut.
// Code = JAPI_KEY_CTRL_BASE | character, e.g. JAPI_KEY_CTRL('S') = 0x0353,
// JAPI_KEY_CTRL('_') = 0x035F, JAPI_KEY_CTRL('1') = 0x0331.
// Letters are normalised to uppercase before encoding (Ctrl+s and Ctrl+S
// both yield 0x0353). Symbols and digits are passed through as-is.
// Conventional exceptions: Ctrl+H/I/M keep emitting BACKSPACE / TAB / ENTER
// (matches terminal convention and what the user expects when pressing those
// keys instead of the dedicated key).
#define JAPI_KEY_CTRL_BASE  0x0300
#define JAPI_KEY_CTRL(c)    (JAPI_KEY_CTRL_BASE | (uint16_t)(c))

// =========================================================================
// VGA DATA STRUCTURES
// =========================================================================

typedef struct {
    uint8_t code;
    uint8_t fg;
    uint8_t bg;
} vga_char_t;

extern vga_char_t vga_text_buffer[VGA_ROWS][VGA_COLS];

// =========================================================================
// JAPI BASE API
// =========================================================================

// Initialise Japi Base: clock, VGA, keyboard, SD card.
// Launches engine on Core 1. Returns when ready.
// Call this once at the start of main().
void japi_init(void);

// --- CPU clock ---
// Three tiers, each with its own core voltage and VGA program; the dot clock
// stays ~65 MHz (1024x768@60) on all of them, so the picture is unchanged:
//   260 MHz @ 1.15 V  (the safe floor / fallback)
//   324 MHz @ 1.20 V  (the default high gear)
//   390 MHz @ 1.30 V  (opt-in up-size)
// The choice is stored in flash and applied on the next boot (a clean reboot,
// ~1 s). A tier the board cannot hold is caught by a watchdog and automatically
// stepped down one tier (390->324, 324->260) -- no re-flash is ever needed.
//   japi_set_cpu_clock(260 | 324 | 390): persist the choice and reboot to apply.
//   japi_get_cpu_clock_mhz():  the clock the board is actually running at.
//   japi_clock_was_reverted(): true if the last attempt failed and stepped down.
//   japi_clock_reverted_from(): the tier that failed (MHz), or 0 if none.
bool japi_set_cpu_clock(int mhz);
int  japi_get_cpu_clock_mhz(void);
bool japi_clock_was_reverted(void);
int  japi_clock_reverted_from(void);

// --- VGA ---
void vga_clear(uint8_t fg, uint8_t bg);
void vga_set_char(int row, int col, uint8_t code, uint8_t fg, uint8_t bg);
void vga_print(int row, int col, const char *str, uint8_t fg, uint8_t bg);
void vga_update(void);
void vga_redefine_char(uint8_t code, const uint8_t new_bitmap[FONT_H]);

// --- BITMAP GRAPHICS ---
// scale = 1: 1 logical pixel = 1 screen pixel
// scale = 2: 1 logical pixel = 2x2 screen pixels (quarters the buffer size).
//            w_chars MUST be even at scale=2 (the renderer expands pixels in
//            8-wide blocks); japi_bitmap_open() returns false otherwise.
// Buffer is (w_chars*8/scale) x (h_chars*12/scale) bytes, must be <= JAPI_BITMAP_MAX_RAM
#define JAPI_BITMAP_MAX_RAM 131072

bool     japi_bitmap_open(int col, int row, int w_chars, int h_chars, int scale);
void     japi_bitmap_close(void);
void     japi_bitmap_pixel(int x, int y, uint8_t colour);
void     japi_bitmap_clear(uint8_t colour);
uint8_t *japi_bitmap_buffer(void);
int      japi_bitmap_width(void);
int      japi_bitmap_height(void);

// --- KEYBOARD ---
bool japi_has_char(void);
uint16_t japi_get_char(void);

// Live modifier state bitmask. Bit positions:
//   0: Shift-L   1: Shift-R   2: Ctrl-L    3: Ctrl-R
//   4: Alt-L     5: AltGr     6: GUI-L     7: GUI-R
// Useful for editors that want to mirror modifier indicators in their UI.
#define JAPI_MOD_SHIFT_L 0x01
#define JAPI_MOD_SHIFT_R 0x02
#define JAPI_MOD_CTRL_L  0x04
#define JAPI_MOD_CTRL_R  0x08
#define JAPI_MOD_ALT_L   0x10
#define JAPI_MOD_ALTGR   0x20
#define JAPI_MOD_GUI_L   0x40
#define JAPI_MOD_GUI_R   0x80
uint8_t japi_kbd_modifier_state(void);

// Keyboard layout table (768 bytes, loaded from LittleFS by japi_init)
extern uint8_t japi_keymap[768];

// --- SOUND ---
#define JAPI_WAVE_SINE      0
#define JAPI_WAVE_SQUARE    1
#define JAPI_WAVE_SAW       2
#define JAPI_WAVE_TRIANGLE  3

#define JAPI_SOUND_CHANNELS 4

// MIDI note constants (octaves 2-7)
#define NOTE_C2   36
#define NOTE_CS2  37
#define NOTE_D2   38
#define NOTE_DS2  39
#define NOTE_E2   40
#define NOTE_F2   41
#define NOTE_FS2  42
#define NOTE_G2   43
#define NOTE_GS2  44
#define NOTE_A2   45
#define NOTE_AS2  46
#define NOTE_B2   47

#define NOTE_C3   48
#define NOTE_CS3  49
#define NOTE_D3   50
#define NOTE_DS3  51
#define NOTE_E3   52
#define NOTE_F3   53
#define NOTE_FS3  54
#define NOTE_G3   55
#define NOTE_GS3  56
#define NOTE_A3   57
#define NOTE_AS3  58
#define NOTE_B3   59

#define NOTE_C4   60
#define NOTE_CS4  61
#define NOTE_D4   62
#define NOTE_DS4  63
#define NOTE_E4   64
#define NOTE_F4   65
#define NOTE_FS4  66
#define NOTE_G4   67
#define NOTE_GS4  68
#define NOTE_A4   69
#define NOTE_AS4  70
#define NOTE_B4   71

#define NOTE_C5   72
#define NOTE_CS5  73
#define NOTE_D5   74
#define NOTE_DS5  75
#define NOTE_E5   76
#define NOTE_F5   77
#define NOTE_FS5  78
#define NOTE_G5   79
#define NOTE_GS5  80
#define NOTE_A5   81
#define NOTE_AS5  82
#define NOTE_B5   83

#define NOTE_C6   84
#define NOTE_CS6  85
#define NOTE_D6   86
#define NOTE_DS6  87
#define NOTE_E6   88
#define NOTE_F6   89
#define NOTE_FS6  90
#define NOTE_G6   91
#define NOTE_GS6  92
#define NOTE_A6   93
#define NOTE_AS6  94
#define NOTE_B6   95

#define NOTE_C7   96
#define NOTE_CS7  97
#define NOTE_D7   98
#define NOTE_DS7  99
#define NOTE_E7  100
#define NOTE_F7  101
#define NOTE_FS7 102
#define NOTE_G7  103
#define NOTE_GS7 104
#define NOTE_A7  105
#define NOTE_AS7 106
#define NOTE_B7  107

#define NOTE_REST 255

// Simple API: non-blocking, auto note-off after duration
void japi_play(uint8_t note, uint16_t duration_ms);
void japi_play_ch(int ch, uint8_t note, uint16_t duration_ms);

// Per-channel control
void japi_sound_wave(int ch, uint8_t type);
void japi_sound_freq(int ch, uint16_t hz);
void japi_sound_volume(int ch, uint8_t vol);
void japi_sound_pan(int ch, uint8_t pan);
void japi_sound_envelope(int ch, uint16_t attack_ms, uint16_t decay_ms,
                         uint8_t sustain, uint16_t release_ms);
void japi_sound_note_on(int ch);
void japi_sound_note_off(int ch);
void japi_sound_off(void);

// --- FILE I/O (unified API for SD card and LittleFS flash) ---
//
// Drive letters:  A: = SD card (removable media, available if inserted)
//                 C: = LittleFS built-in media (360K flash, always available)

typedef struct {
    uint8_t type;
    union {
        FIL fat;
        lfs_file_t lfs;
    };
} japi_file_t;

#define JAPI_READ    1
#define JAPI_WRITE   2
#define JAPI_APPEND  4

bool japi_fopen(japi_file_t *f, const char *path, uint8_t mode);
int  japi_fread(japi_file_t *f, void *buf, int size);
int  japi_fwrite(japi_file_t *f, const void *buf, int size);
void japi_fclose(japi_file_t *f);
bool japi_remove(const char *path);
bool japi_mkdir(const char *path);
bool japi_exists(const char *path);
int  japi_fsize(japi_file_t *f);

/* Directory listing — opaque handle, walks entries one by one. The
   backend (FatFs on FAT volumes, lfs on LittleFS) decides internals.
   japi_readdir writes the entry name into name_out (NUL-terminated,
   truncated to name_max-1 chars) and returns true on each entry, false
   when the directory is exhausted. "." and ".." are not yielded. */
typedef struct {
    uint8_t type;
    union {
        DIR        fat;
        lfs_dir_t  lfs;
    };
} japi_dir_t;

bool japi_opendir(japi_dir_t *d, const char *path);
bool japi_readdir(japi_dir_t *d, char *name_out, int name_max);
void japi_closedir(japi_dir_t *d);

#endif // JAPI_BASE_H
