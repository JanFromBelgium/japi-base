#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "japi_base.h"
#include "demo.h"

// =========================================================================
// CHARACTER CODES
// =========================================================================

// Standard CP437 box-drawing (already in font_8x12, lines at mid-height)
#define CH_TL    0xDA  // ┌
#define CH_TR    0xBF  // ┐
#define CH_BL    0xC0  // └
#define CH_BR    0xD9  // ┘
#define CH_HZ    0xC4  // ─
#define CH_VT    0xB3  // │
#define CH_FULL  0xDB  // █


// =========================================================================
// DRAWING HELPERS
// =========================================================================

static void draw_box(int r1, int c1, int r2, int c2, uint8_t fg, uint8_t bg) {
    vga_set_char(r1, c1, CH_TL, fg, bg);
    vga_set_char(r1, c2, CH_TR, fg, bg);
    vga_set_char(r2, c1, CH_BL, fg, bg);
    vga_set_char(r2, c2, CH_BR, fg, bg);
    for (int c = c1+1; c < c2; c++) {
        vga_set_char(r1, c, CH_HZ, fg, bg);
        vga_set_char(r2, c, CH_HZ, fg, bg);
    }
    for (int r = r1+1; r < r2; r++) {
        vga_set_char(r, c1, CH_VT, fg, bg);
        vga_set_char(r, c2, CH_VT, fg, bg);
    }
}

static void draw_titled_box(int r1, int c1, int r2, int c2,
                            const char *title, uint8_t fg, uint8_t bg) {
    draw_box(r1, c1, r2, c2, fg, bg);
    vga_print(r1, c1 + 2, title, fg, bg);
}

static void fill_rect(int r1, int c1, int r2, int c2, uint8_t ch,
                      uint8_t fg, uint8_t bg) {
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++)
            vga_set_char(r, c, ch, fg, bg);
}

static void draw_shadow(int r1, int c1, int r2, int c2) {
    draw_box(r1+1, c1+1, r2+1, c2+1, VGA_BLACK, VGA_BLACK);
    fill_rect(r1+2, c1+2, r2, c2, ' ', VGA_BLACK, VGA_BLACK);
}

static uint16_t wait_key(void) {
    /* Idle on vblank so any text the page wrote before calling us
       reaches the active (read-by-scanline) buffer; otherwise the
       page would only become visible after the next vga_update
       elsewhere, which may never come on a static screen. */
    while (!japi_has_char()) vga_update();
    return japi_get_char();
}

#define TICK_MS 150
#define HOLD 0

static void play_tick(int ch, const uint8_t *notes, int idx, int len) {
    uint8_t n = notes[idx];
    if (n == 0 || n == NOTE_REST) return;
    int dur = TICK_MS;
    for (int j = idx + 1; j < len && notes[j] == 0; j++) dur += TICK_MS;
    japi_play_ch(ch, n, dur);
}

// =========================================================================
// COLOUR PALETTE
// =========================================================================

static void draw_palette(int start_row, int start_col) {
    int block_w = 3;
    for (int group = 0; group < 4; group++) {
        int row = start_row + group * 2;
        for (int i = 0; i < 16; i++) {
            uint8_t colour = (group << 4) | i;
            int col = start_col + i * block_w;
            for (int x = 0; x < block_w; x++) {
                vga_set_char(row,     col + x, CH_FULL, colour, colour);
                vga_set_char(row + 1, col + x, CH_FULL, colour, colour);
            }
        }
    }
}

// =========================================================================
// LARGE LETTER DISPLAY (font redefinition demo)
// =========================================================================

extern uint8_t font_8x12[256][12];

static void draw_large_char(int row, int col, uint8_t ch, uint8_t fg, uint8_t bg) {
    for (int y = 0; y < 12; y++) {
        uint8_t bits = font_8x12[ch][y];
        for (int x = 0; x < 8; x++) {
            if (bits & (0x80 >> x))
                vga_set_char(row + y, col + x, CH_FULL, fg, bg);
            else
                vga_set_char(row + y, col + x, ' ', bg, bg);
        }
    }
}

// =========================================================================
// PAGE 1: SHOWCASE (matches Pico 1 demo style)
// =========================================================================

#define BG VGA_DARK_BLUE

static void page_showcase(void) {
    vga_clear(VGA_WHITE, BG);

    // Column layout: left 1-60, gap 61-62, right 63-125
    int L1 = 1, L2 = 60;
    int R1 = 63, R2 = 125;

    // --- Title ---
    for (int c = 0; c < VGA_COLS; c++) vga_set_char(0, c, ' ', BG, VGA_CYAN);
    vga_print(0, 3, "JAPI BASE PICO 2", VGA_BLACK, VGA_CYAN);
    char title[40];
    snprintf(title, sizeof(title), "RP2350 @ %d MHz  |  Pico SDK 2.2.0",
             japi_get_cpu_clock_mhz());
    vga_print(0, 60, title, BG, VGA_CYAN);

    vga_print(2, 3, "VGA 1024x768 60Hz  127x64 text  64 foreground and 64 background colours.  PS/2 Keyboard.  SD Card (FatFs).", VGA_YELLOW, BG);
    char uses[120];
    snprintf(uses, sizeof(uses),
             "Uses: PIO0 (3 SMs: HSync+pixels, VSync, PS/2), Core 1, "
             "DMA IRQ0 (SD) + IRQ1 (VGA), sys clock %d MHz.",
             japi_get_cpu_clock_mhz());
    vga_print(3, 3, uses, VGA_YELLOW, BG);
    vga_print(4, 3, "Free: Core 0, PIO1 + PIO2 (8 SMs), DMA (polling only).", VGA_YELLOW, BG);
    vga_print(4, 52, "Do NOT change sys clock or use DMA IRQ0/IRQ1 or PIO0.", VGA_RED, BG);

    // --- Colour Palette (compact) ---
    draw_palette(6, 1);

    // --- Bitmap Graphics Demo (right of palette) ---
    // Four audio waveforms overlaid on a black "scope" background.
    int bm_col = 52, bm_row = 6, bm_w = 73, bm_h = 8;
    japi_bitmap_open(bm_col, bm_row, bm_w, bm_h, 1);
    int bpw = japi_bitmap_width(), bph = japi_bitmap_height();
    uint8_t *bbuf = japi_bitmap_buffer();
    memset(bbuf, VGA_BLACK, bpw * bph);

    {
        int y_mid = bph / 2;
        int amp   = bph / 2 - 8;
        float cycles  = 2.0f;
        float two_pi  = 6.28318530f;
        uint8_t wcol[4] = { VGA_YELLOW, VGA_CYAN, VGA_GREEN, VGA_MAGENTA };
        // Faint centerline.
        for (int x = 0; x < bpw; x++) bbuf[y_mid * bpw + x] = 0x15;
        for (int wi = 0; wi < 4; wi++) {
            uint8_t color = wcol[wi];
            int prev_py = y_mid;
            for (int x = 0; x < bpw; x++) {
                float phase = (float)x / (float)bpw * cycles * two_pi;
                float v = 0.0f;
                switch (wi) {
                    case 0: v = sinf(phase); break;
                    case 1: v = (sinf(phase) >= 0.0f) ? 1.0f : -1.0f; break;
                    // Saw shifted a quarter period to the left so its drop
                    // doesn't sit at x = 0.
                    case 2: { float p = fmodf(phase + two_pi * 0.25f, two_pi) / two_pi;
                              v = p * 2.0f - 1.0f; break; }
                    case 3: { float p = fmodf(phase, two_pi) / two_pi;
                              v = (p < 0.5f) ? (p * 4.0f - 1.0f) : (3.0f - p * 4.0f);
                              break; }
                }
                int py = y_mid - (int)(v * amp);
                if (py < 0) py = 0; if (py >= bph) py = bph - 1;
                bbuf[py * bpw + x] = color;
                // Connect across discontinuities for square and saw waves so
                // the vertical jump is visible. Threshold > 2 lets the saw's
                // gentle ramp through without thickening the line.
                int delta = py - prev_py; if (delta < 0) delta = -delta;
                if (x > 0 && (wi == 1 || wi == 2) && delta > 2) {
                    int lo = py < prev_py ? py : prev_py;
                    int hi = py < prev_py ? prev_py : py;
                    for (int yy = lo + 1; yy < hi; yy++)
                        bbuf[yy * bpw + x] = color;
                }
                prev_py = py;
            }
        }
    }
    vga_print(14, bm_col, "Bitmap graphics: 4 audio waveforms (sine/square/saw/triangle) overlaid", VGA_CYAN, BG);

    // === ROW 1: Default Character Set | Window Borders ===

    draw_titled_box(16, L1, 27, L2, " Default Character Set ", VGA_GREEN, BG);
    for (int ch = 0; ch < 256; ch++) {
        int r = 18 + (ch / 28);
        int c = L1 + 2 + (ch % 28) * 2;
        if (r <= 26 && c < L2 - 1)
            vga_set_char(r, c, ch, VGA_CYAN, BG);
    }

    // === RIGHT COLUMN: Windows and Frames (full height) ===

    draw_titled_box(16, R1, 40, R2, " Windows and Frames ", VGA_GREEN, BG);

    // Fill with standard CP437 light shade on lighter blue
    fill_rect(17, R1 + 1, 39, R2 - 1, 0xB0, VGA_CYAN, 0x02);

    // Top-left: border, no shadow
    {
        int wr1 = 18, wc1 = R1 + 2, wr2 = 26, wc2 = R1 + 28;
        draw_box(wr1, wc1, wr2, wc2, VGA_BLACK, VGA_CYAN);
        fill_rect(wr1+1, wc1+1, wr2-1, wc2-1, ' ', VGA_BLACK, VGA_CYAN);
        vga_print(wr1+1, wc1 + 2, "Border", VGA_BLACK, VGA_CYAN);
        vga_print(wr1+2, wc1 + 2, "No shadow", VGA_BLACK, VGA_CYAN);
    }

    // Top-right: no border, no shadow
    {
        int wr1 = 18, wc1 = R1 + 32, wr2 = 26, wc2 = R1 + 58;
        fill_rect(wr1, wc1, wr2, wc2, ' ', VGA_WHITE, VGA_GREEN);
        vga_print(wr1+1, wc1 + 2, "No border", VGA_WHITE, VGA_GREEN);
        vga_print(wr1+2, wc1 + 2, "No shadow", VGA_WHITE, VGA_GREEN);
    }

    // Bottom-left: border, with shadow
    {
        int wr1 = 29, wc1 = R1 + 2, wr2 = 37, wc2 = R1 + 28;
        draw_shadow(wr1, wc1, wr2, wc2);
        draw_box(wr1, wc1, wr2, wc2, VGA_BLACK, VGA_CYAN);
        fill_rect(wr1+1, wc1+1, wr2-1, wc2-1, ' ', VGA_BLACK, VGA_CYAN);
        vga_print(wr1+1, wc1 + 2, "Border", VGA_BLACK, VGA_CYAN);
        vga_print(wr1+2, wc1 + 2, "With shadow", VGA_BLACK, VGA_CYAN);
    }

    // Bottom-right: no border, with shadow
    {
        int wr1 = 29, wc1 = R1 + 32, wr2 = 37, wc2 = R1 + 58;
        draw_shadow(wr1, wc1, wr2, wc2);
        fill_rect(wr1, wc1, wr2, wc2, ' ', VGA_YELLOW, VGA_RED);
        vga_print(wr1+1, wc1 + 2, "No border", VGA_YELLOW, VGA_RED);
        vga_print(wr1+2, wc1 + 2, "With shadow", VGA_YELLOW, VGA_RED);
    }

    // === ROW 2 LEFT: Text Colours ===

    draw_titled_box(29, L1, 40, L2, " Text Colours ", VGA_GREEN, BG);
    vga_print(31, L1 + 3, "Each cell has an independent", VGA_WHITE, BG);
    vga_print(32, L1 + 3, "Foreground (fg) and Background (bg):", VGA_WHITE, BG);

    vga_print(34, L1 + 3,  "Red",     VGA_RED, BG);
    vga_print(34, L1 + 10, "Green",   VGA_GREEN, BG);
    vga_print(34, L1 + 19, "Blue",    VGA_BLUE, BG);
    vga_print(34, L1 + 27, "Yellow",  VGA_YELLOW, BG);
    vga_print(34, L1 + 37, "White",   VGA_WHITE, BG);
    vga_print(34, L1 + 46, "Cyan",    VGA_CYAN, BG);

    vga_print(36, L1 + 3,  "Magenta on blue",  VGA_MAGENTA, VGA_BLUE);
    vga_print(36, L1 + 22, "Black on yellow",  VGA_BLACK, VGA_YELLOW);
    vga_print(36, L1 + 41, "White on red",     VGA_WHITE, VGA_RED);

    vga_print(38, L1 + 3, "colour = (R<<4)|(G<<2)|B   with R, G and B each 0-3", VGA_CYAN, BG);

    // === ROW 3: Font Redefinition | Bar Chart Demo ===

    draw_titled_box(42, L1, 60, L2, " Runtime Font Redefinition ", VGA_GREEN, BG);
    vga_print(44, L1 + 3, "8 x 12 font matrix", VGA_YELLOW, BG);

    fill_rect(45, L1 + 3, 56, L1 + 10, ' ', 0x02, 0x02);
    fill_rect(45, L1 + 14, 56, L1 + 21, ' ', 0x02, 0x02);
    draw_large_char(45, L1 + 3, 'A', VGA_CYAN, 0x02);
    draw_large_char(45, L1 + 14, 'g', VGA_CYAN, 0x02);

    for (int y = 0; y < 12; y++) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X", font_8x12['A'][y]);
        vga_print(45 + y, L1 + 25, hex, VGA_GREEN, BG);
    }
    for (int y = 0; y < 12; y++) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02X", font_8x12['g'][y]);
        vga_print(45 + y, L1 + 31, hex, VGA_GREEN, BG);
    }

    vga_print(45, L1 + 35, "Allows live updating", VGA_WHITE, BG);
    vga_print(46, L1 + 35, "of the font.", VGA_WHITE, BG);
    vga_print(47, L1 + 35, "Ideal for custom", VGA_WHITE, BG);
    vga_print(48, L1 + 35, "User Interface,", VGA_WHITE, BG);
    vga_print(49, L1 + 35, "indicators &", VGA_WHITE, BG);
    vga_print(50, L1 + 35, "graphics.", VGA_WHITE, BG);

    vga_print(52, L1 + 35, "uint8_t bmp[12];", VGA_WHITE, BG);
    vga_print(53, L1 + 35, "// One byte per row", VGA_GREEN, BG);
    vga_print(54, L1 + 35, "// MSB = leftmost px", VGA_GREEN, BG);

    vga_print(58, L1 + 3, "vga_redefine_char(code, bmp);", VGA_WHITE, BG);

    draw_titled_box(42, R1, 60, R1 + 32, " Bar Chart ", VGA_GREEN, BG);

    int bars[] = {5, 12, 3, 9, 7, 2, 11, 4, 8, 6};
    uint8_t bar_colours[] = {VGA_RED, VGA_GREEN, VGA_BLUE, VGA_YELLOW,
                             VGA_CYAN, VGA_MAGENTA, VGA_WHITE, 0x24,
                             0x30, 0x0F};

    int vbar_start = R1 + 2;
    for (int b = 0; b < 10; b++) {
        int col = vbar_start + b * 3;
        for (int x = 0; x < 2; x++)
            for (int h = 0; h < bars[b]; h++)
                vga_set_char(59 - h, col + x, CH_FULL, bar_colours[b], BG);
    }

    // --- Predefined Colours ---
    draw_titled_box(42, R1 + 34, 60, R2, " Predefined Colours ", VGA_GREEN, BG);
    {
        struct { const char *name; const char *hex; uint8_t val; } colours[] = {
            {"VGA_BLACK",    "0x00", VGA_BLACK},
            {"VGA_WHITE",    "0x3F", VGA_WHITE},
            {"VGA_RED",      "0x30", VGA_RED},
            {"VGA_GREEN",    "0x0C", VGA_GREEN},
            {"VGA_BLUE",     "0x03", VGA_BLUE},
            {"VGA_YELLOW",   "0x3C", VGA_YELLOW},
            {"VGA_CYAN",     "0x0F", VGA_CYAN},
            {"VGA_MAGENTA",  "0x33", VGA_MAGENTA},
            {"VGA_DARK_BLUE","0x01", VGA_DARK_BLUE},
        };
        for (int i = 0; i < 9; i++) {
            int row = 44 + i;
            int col_base = R1 + 36;
            vga_set_char(row, col_base, CH_FULL, colours[i].val, colours[i].val);
            vga_set_char(row, col_base + 1, CH_FULL, colours[i].val, colours[i].val);
            vga_print(row, col_base + 3, colours[i].name, VGA_YELLOW, BG);
            vga_print(row, col_base + 18, colours[i].hex, VGA_WHITE, BG);
        }
    }
    vga_print(54, R1 + 36, "colour =", VGA_CYAN, BG);
    vga_print(55, R1 + 36, "(R<<4)|(G<<2)|B", VGA_CYAN, BG);
    vga_print(56, R1 + 36, "R,G,B = 0..3", VGA_CYAN, BG);

    // --- Footer ---
    for (int c = 0; c < VGA_COLS; c++) vga_set_char(63, c, ' ', VGA_BLACK, VGA_WHITE);
    vga_print(63, 2, "Playing: Korobeiniki (Tetris) \xF0 4-channel stereo sound", VGA_BLACK, VGA_WHITE);
    vga_print(63, 72, "Press any key for API quick reference...", VGA_BLACK, VGA_WHITE);

    // --- Sound demo: Korobeiniki (Tetris) — 4-channel ---
    japi_sound_wave(0, JAPI_WAVE_SINE);
    japi_sound_envelope(0, 5, 80, 180, 150);
    japi_sound_volume(0, 220);
    japi_sound_pan(0, 108);

    japi_sound_wave(1, JAPI_WAVE_TRIANGLE);
    japi_sound_envelope(1, 5, 60, 0, 50);
    japi_sound_volume(1, 100);
    japi_sound_pan(1, 148);

    japi_sound_wave(2, JAPI_WAVE_SAW);
    japi_sound_envelope(2, 3, 40, 80, 30);
    japi_sound_volume(2, 70);
    japi_sound_pan(2, 128);

    japi_sound_wave(3, JAPI_WAVE_SQUARE);
    japi_sound_envelope(3, 5, 100, 140, 100);
    japi_sound_volume(3, 130);
    japi_sound_pan(3, 128);

    static const uint8_t t_mel[] = {
        NOTE_E5,HOLD,NOTE_B4,NOTE_C5, NOTE_D5,HOLD,NOTE_C5,NOTE_B4,
        NOTE_A4,HOLD,NOTE_A4,NOTE_C5, NOTE_E5,HOLD,NOTE_D5,NOTE_C5,
        NOTE_B4,HOLD,HOLD,NOTE_C5,    NOTE_D5,HOLD,NOTE_E5,HOLD,
        NOTE_C5,HOLD,NOTE_A4,HOLD,    NOTE_A4,HOLD,HOLD,HOLD,
        NOTE_REST,HOLD,NOTE_D5,HOLD,  HOLD,NOTE_F5,NOTE_A5,HOLD,
        NOTE_G5,NOTE_F5,NOTE_E5,HOLD, HOLD,NOTE_C5,NOTE_E5,HOLD,
        NOTE_D5,NOTE_C5,NOTE_B4,HOLD, HOLD,NOTE_C5,NOTE_D5,HOLD,
        NOTE_E5,HOLD,NOTE_C5,HOLD,    NOTE_A4,HOLD,HOLD,HOLD,
        NOTE_E5,HOLD,NOTE_B4,NOTE_C5, NOTE_D5,HOLD,NOTE_C5,NOTE_B4,
        NOTE_A4,HOLD,NOTE_A4,NOTE_C5, NOTE_E5,HOLD,NOTE_D5,NOTE_C5,
        NOTE_B4,HOLD,HOLD,NOTE_C5,    NOTE_D5,HOLD,NOTE_E5,HOLD,
        NOTE_C5,HOLD,NOTE_A4,HOLD,    NOTE_A4,HOLD,HOLD,HOLD,
        NOTE_REST,HOLD,HOLD,HOLD,
    };
    static const uint8_t t_arp[] = {
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_E3,NOTE_GS3,NOTE_B3,NOTE_GS3, NOTE_E3,NOTE_GS3,NOTE_B3,NOTE_GS3,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_F3,NOTE_A3,NOTE_C4,NOTE_A3, NOTE_F3,NOTE_A3,NOTE_C4,NOTE_A3,
        NOTE_C4,NOTE_E4,NOTE_G4,NOTE_E4, NOTE_C4,NOTE_E4,NOTE_G4,NOTE_E4,
        NOTE_G3,NOTE_B3,NOTE_D4,NOTE_B3, NOTE_G3,NOTE_B3,NOTE_D4,NOTE_B3,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_E3,NOTE_GS3,NOTE_B3,NOTE_GS3, NOTE_E3,NOTE_GS3,NOTE_B3,NOTE_GS3,
        NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4, NOTE_A3,NOTE_C4,NOTE_E4,NOTE_C4,
        NOTE_A3,NOTE_C4,NOTE_E4,HOLD,
    };
    static const uint8_t t_stb[] = {
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_B3,HOLD,NOTE_REST,HOLD, NOTE_B3,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_A3,HOLD,NOTE_REST,HOLD, NOTE_A3,HOLD,NOTE_REST,HOLD,
        NOTE_G4,HOLD,NOTE_REST,HOLD, NOTE_G4,HOLD,NOTE_REST,HOLD,
        NOTE_B3,HOLD,NOTE_REST,HOLD, NOTE_B3,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_B3,HOLD,NOTE_REST,HOLD, NOTE_B3,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,NOTE_REST,HOLD, NOTE_E4,HOLD,NOTE_REST,HOLD,
        NOTE_E4,HOLD,HOLD,HOLD,
    };
    static const uint8_t t_bas[] = {
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E3,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E3,HOLD,HOLD,HOLD,
        NOTE_E2,HOLD,HOLD,HOLD, NOTE_B2,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E2,HOLD,HOLD,HOLD,
        NOTE_F2,HOLD,HOLD,HOLD, NOTE_C3,HOLD,HOLD,HOLD,
        NOTE_C3,HOLD,HOLD,HOLD, NOTE_G2,HOLD,HOLD,HOLD,
        NOTE_G2,HOLD,HOLD,HOLD, NOTE_D3,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E2,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E3,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E3,HOLD,HOLD,HOLD,
        NOTE_E2,HOLD,HOLD,HOLD, NOTE_B2,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD, NOTE_E2,HOLD,HOLD,HOLD,
        NOTE_A2,HOLD,HOLD,HOLD,
    };
    #define T_LEN ((int)sizeof(t_mel))
    int t_idx = 0;
    int t_timer = 0;

    // --- Animation loop: scrolling block on row 61, music ticking ---
    {
        for (int col = 1; col < VGA_COLS - 1; col++)
            vga_set_char(61, col, CH_HZ, VGA_CYAN, BG);

        play_tick(0, t_mel, 0, T_LEN);
        play_tick(1, t_stb, 0, T_LEN);
        play_tick(2, t_arp, 0, T_LEN);
        play_tick(3, t_bas, 0, T_LEN);
        t_timer = TICK_MS;

        int pos = 1;
        while (!japi_has_char()) {
            vga_update();   /* Publish previous iteration's writes
                                    (and on the first pass, the whole
                                    page_showcase initial draw). */

            vga_set_char(61, pos, CH_HZ, VGA_CYAN, BG);
            pos++;
            if (pos >= VGA_COLS - 1) pos = 1;
            vga_set_char(61, pos, CH_FULL, VGA_CYAN, BG);

            t_timer -= 40;
            if (t_timer <= 0) {
                t_idx++;
                if (t_idx >= T_LEN) t_idx = 0;
                play_tick(0, t_mel, t_idx, T_LEN);
                play_tick(1, t_stb, t_idx, T_LEN);
                play_tick(2, t_arp, t_idx, T_LEN);
                play_tick(3, t_bas, t_idx, T_LEN);
                t_timer = TICK_MS;
            }

            sleep_ms(40);
        }
        vga_set_char(61, pos, CH_HZ, VGA_CYAN, BG);
    }

    japi_bitmap_close();
    japi_sound_off();
    japi_get_char();
}

// =========================================================================
// PAGE 2: LARGE BITMAP DEMO (scale=2)
// =========================================================================

// Clear the caption strips above (rows 1..5) and below (rows 58..63) the
// 104x52 bitmap which sits at rows 6..57.
static void clear_caption_panel(void) {
    for (int r = 1; r <= 5; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_set_char(r, c, ' ', VGA_WHITE, BG);
    for (int r = 58; r <= 63; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga_set_char(r, c, ' ', VGA_WHITE, BG);
}

// PHASE 1: bouncing balls on a solid billiard-felt background.
// Flicker-free: the whole erase+move+draw mutation runs during the vertical
// blanking interval (after vga_update), so the VGA engine never scans a
// half-updated buffer. A solid background makes the erase a per-row memset,
// fast enough to finish well within the ~0.8 ms blank.
#define FELT 0x04   /* dark billiard green (RRGGBB: R=0 G=1 B=0) */

static void bitmap_phase_balls(uint8_t *buf, int W, int H, int duration_ms) {
    memset(buf, FELT, W * H);
    for (int x = 0; x < W; x++) { buf[x] = VGA_WHITE; buf[(H-1)*W + x] = VGA_WHITE; }
    for (int y = 0; y < H; y++) { buf[y*W] = VGA_WHITE; buf[y*W + W - 1] = VGA_WHITE; }

    clear_caption_panel();
    vga_print(2, 3, "832 x 624 screen pixels (416 x 312 logical, expanded x2).",
              VGA_CYAN,   BG);
    vga_print(3, 3, "Solid felt background makes the erase a fast per-row memset.",
              VGA_WHITE,  BG);
    vga_print(4, 3, "Erase + move + draw run entirely in the vertical blank: zero flicker.",
              VGA_YELLOW, BG);

    vga_print(60, 3, "127 KB pixel buffer, ~70% of the screen, within per-scanline budget.",
              VGA_GREEN, BG);

    // Positions/radii in LOGICAL pixels (buffer is 416x312, renderer doubles).
    #define NB 6
    int bx[NB]  = { 100, 250, 325,  60, 375, 200 };
    int by[NB]  = {  90,  60, 190, 240, 150, 125 };
    int bdx[NB] = {   2,  -2,   2,   1,  -2,   1 };
    int bdy[NB] = {   1,   2,  -1,  -2,   1,   2 };
    int br[NB]  = {  20,  16,  18,  22,  14,  19 };
    uint8_t bc[NB] = { VGA_RED, VGA_YELLOW, VGA_CYAN, VGA_GREEN, VGA_MAGENTA, VGA_WHITE };

    int frames = duration_ms / 16;
    while (frames-- > 0 && !japi_has_char()) {
        // Pace ~60fps, then align the mutation to the start of vblank.
        sleep_ms(15);
        vga_update();

        // Erase: fill each ball's bbox with felt (square erase is invisible
        // on a solid background). Clipped to interior to keep the border.
        for (int i = 0; i < NB; i++) {
            int r = br[i];
            int x0 = bx[i] - r, x1 = bx[i] + r;
            int y0 = by[i] - r, y1 = by[i] + r;
            if (x0 < 1) x0 = 1;  if (x1 > W - 2) x1 = W - 2;
            if (y0 < 1) y0 = 1;  if (y1 > H - 2) y1 = H - 2;
            for (int py = y0; py <= y1; py++)
                memset(&buf[py*W + x0], FELT, x1 - x0 + 1);
        }
        // Move.
        for (int i = 0; i < NB; i++) {
            bx[i] += bdx[i]; by[i] += bdy[i];
            if (bx[i] - br[i] <= 1 || bx[i] + br[i] >= W - 2) bdx[i] = -bdx[i];
            if (by[i] - br[i] <= 1 || by[i] + br[i] >= H - 2) bdy[i] = -bdy[i];
        }
        // Flat-colour balls with a single large white specular highlight,
        // offset toward the light (upper-left). No body shading.
        for (int i = 0; i < NB; i++) {
            int r = br[i];
            int r2 = r*r + r;
            uint8_t c_base = bc[i];
            // Highlight offset 0.33 r toward the light, radius 0.40 r.
            // Max reach = 0.33*sqrt(2)*r + 0.40 r ~= 0.87 r, comfortably
            // inside the ball at every radius so it never clips to a
            // non-circle (the red ball used to clip at the old 0.45 r).
            int hx = -r/3, hy = -r/3;
            int spec_r2 = (r*r*4) / 25;           // ~0.40 r radius
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (dx*dx + dy*dy <= r2) {
                        int px = bx[i] + dx, py = by[i] + dy;
                        if (px < 1 || px >= W-1 || py < 1 || py >= H-1) continue;
                        int sx = dx - hx, sy = dy - hy;
                        buf[py*W + px] =
                            (sx*sx + sy*sy <= spec_r2) ? VGA_WHITE : c_base;
                    }
        }
    }
    #undef NB
}

// PHASE 2: Van Gogh "The Starry Night" (1889, public domain).
// 256x192 Floyd-Steinberg dither into the 64-colour palette, embedded as a
// C array in starry_image.h. Single memcpy at entry -> zero CPU per frame.
static void bitmap_phase_photo(uint8_t *buf, int W, int H, int duration_ms) {
    (void)W; (void)H;
    memcpy(buf, starry_image, sizeof(starry_image));

    clear_caption_panel();
    vga_print(2, 3, "The Starry Night   --   Vincent van Gogh, 1889",  VGA_CYAN,   BG);
    vga_print(3, 3, "416 x 312 logical pixels in 64 colours, Floyd-Steinberg dithered.",
              VGA_YELLOW, BG);
    vga_print(4, 3, "Source: Wikimedia Commons, Google Art Project (public domain).",
              VGA_WHITE,  BG);

    vga_print(59, 3, "130 KB constant C array, one memcpy on entry, zero CPU per frame.",
              VGA_GREEN, BG);
    vga_print(60, 3, "Renderer expands each logical pixel to 2x2 -> 832 x 624 on screen.",
              VGA_WHITE, BG);
    vga_print(61, 3, "Renders within budget thanks to split text/bitmap rendering.",
              VGA_GREEN, BG);

    int slept = 0;
    while (slept < duration_ms && !japi_has_char()) {
        sleep_ms(50);
        slept += 50;
    }
}

// Both bitmap pages share this setup. Returns false (and shows an error)
// if the 130 KB buffer can't be allocated.
static bool open_bitmap_page(const char *title, int *W, int *H, uint8_t **buf) {
    vga_clear(VGA_WHITE, BG);
    for (int c = 0; c < VGA_COLS; c++) vga_set_char(0, c, ' ', VGA_BLACK, VGA_CYAN);
    vga_print(0, 3,  title, VGA_BLACK, VGA_CYAN);
    vga_print(0, 90, "Press any key ->", VGA_BLACK, VGA_CYAN);

    // 104 x 52 chars at scale=2 -> 832 x 624 px = 519,168 bytes (peak RAM).
    // Centered: col=11 (11 free L, 12 free R), row=6 (5 free top, 6 free bottom).
    if (!japi_bitmap_open(11, 6, 104, 52, 2)) {
        vga_print(30, 30, "Bitmap allocation failed!", VGA_RED, BG);
        wait_key();
        return false;
    }
    *W   = japi_bitmap_width();
    *H   = japi_bitmap_height();
    *buf = japi_bitmap_buffer();
    return true;
}

#define RUN_UNTIL_KEY 3600000   /* effectively until a key is pressed */

static void page_bitmap_balls(void) {
    int W, H; uint8_t *buf;
    if (!open_bitmap_page("JAPI BASE  -  Bitmap Graphics: Bouncing Balls (scale=2)",
                          &W, &H, &buf))
        return;
    bitmap_phase_balls(buf, W, H, RUN_UNTIL_KEY);
    japi_bitmap_close();
    japi_get_char();
}

static void page_bitmap_photo(void) {
    int W, H; uint8_t *buf;
    if (!open_bitmap_page("JAPI BASE  -  Bitmap Graphics: The Starry Night (scale=2)",
                          &W, &H, &buf))
        return;
    bitmap_phase_photo(buf, W, H, RUN_UNTIL_KEY);
    japi_bitmap_close();
    japi_get_char();
}

// =========================================================================
// PAGE 3: API QUICK REFERENCE
// =========================================================================

static void page_api(void) {
    vga_clear(VGA_YELLOW, BG);

    for (int c = 0; c < VGA_COLS; c++) vga_set_char(0, c, ' ', VGA_BLACK, VGA_CYAN);
    vga_print(0, 2, "JAPI BASE API  -  Quick Reference", VGA_BLACK, VGA_CYAN);
    vga_print(0, 50, "FatFs: carlk3  |  pico-lfs: T. Kokkonen", VGA_BLACK, VGA_CYAN);

    int L1 = 1, L2 = 62;
    int R1 = 64, R2 = 125;

    // === LEFT PANEL (open bottom) ===
    vga_set_char(2, L1, CH_TL, VGA_CYAN, BG);
    vga_set_char(2, L2, CH_TR, VGA_CYAN, BG);
    for (int c = L1+1; c < L2; c++) vga_set_char(2, c, CH_HZ, VGA_CYAN, BG);
    vga_print(2, L1 + 2, " Example Code - How to Use the API ", VGA_CYAN, BG);
    for (int r = 3; r <= 61; r++) {
        vga_set_char(r, L1, CH_VT, VGA_CYAN, BG);
        vga_set_char(r, L2, CH_VT, VGA_CYAN, BG);
    }

    // === RIGHT PANEL (open top, closes at row 49) ===
    for (int r = 2; r <= 48; r++) {
        vga_set_char(r, R1, CH_VT, VGA_CYAN, BG);
        vga_set_char(r, R2, CH_VT, VGA_CYAN, BG);
    }
    vga_set_char(49, R1, CH_BL, VGA_CYAN, BG);
    vga_set_char(49, R2, CH_BR, VGA_CYAN, BG);
    for (int c = R1+1; c < R2; c++) vga_set_char(49, c, CH_HZ, VGA_CYAN, BG);

    // === CODE: left panel rows 3-60 ===

    vga_print(3,  L1 + 2, "Build: cmake -B build && ninja -C build", VGA_WHITE, BG);

    vga_print(5,  L1 + 2, "#include \"japi_base.h\"",                 VGA_YELLOW, BG);
    vga_print(6,  L1 + 2, "int main() {",                             VGA_YELLOW, BG);
    vga_print(7,  L1 + 2, "    japi_init();",                         VGA_YELLOW, BG);
    vga_print(7,  L1 + 20, "// VGA + keyboard + SD + flash",          VGA_GREEN, BG);

    vga_print(9,  L1 + 2, "    // === Screen Output ===",             VGA_GREEN, BG);
    vga_print(10, L1 + 2, "    // vga_clear(fg, bg)",                 VGA_GREEN, BG);
    vga_print(11, L1 + 2, "    vga_clear(VGA_WHITE, VGA_BLUE);",      VGA_YELLOW, BG);
    vga_print(12, L1 + 2, "    // vga_print(row, col, str, fg, bg)",  VGA_GREEN, BG);
    vga_print(13, L1 + 2, "    vga_print(0,0,\"Hello!\",VGA_YELLOW,VGA_BLUE);", VGA_YELLOW, BG);
    vga_print(14, L1 + 2, "    // vga_set_char(row, col, ch, fg, bg)", VGA_GREEN, BG);
    vga_print(15, L1 + 2, "    vga_set_char(1,0,'A',VGA_CYAN,VGA_BLUE);", VGA_YELLOW, BG);
    vga_print(16, L1 + 2, "    // Direct buffer: .code .fg .bg",      VGA_GREEN, BG);
    vga_print(17, L1 + 2, "    vga_text_buffer[2][0].code = 'B';",    VGA_YELLOW, BG);
    vga_print(18, L1 + 2, "    vga_update();",                   VGA_YELLOW, BG);
    vga_print(18, L1 + 28, "// sync with refresh",                    VGA_GREEN, BG);

    vga_print(20, L1 + 2, "    // === Redefine Font Glyph ===",       VGA_GREEN, BG);
    vga_print(21, L1 + 2, "    // 8x12 bitmap: 12 bytes, MSB = left", VGA_GREEN, BG);
    vga_print(22, L1 + 2, "    vga_redefine_char(1, (uint8_t[]){",    VGA_YELLOW, BG);
    vga_print(23, L1 + 2, "      0x10,0x10,0x10,0x10,0x10,0xFE,",    VGA_YELLOW, BG);
    vga_print(24, L1 + 2, "      0x10,0x10,0x10,0x10,0x10,0x10});",   VGA_YELLOW, BG);
    vga_print(23, L1 + 44, "// cross glyph", VGA_GREEN, BG);
    vga_print(25, L1 + 2, "    vga_set_char(3,0,1,VGA_CYAN,1);",     VGA_YELLOW, BG);

    vga_print(27, L1 + 2, "    // === Custom Colour ===",             VGA_GREEN, BG);
    vga_print(28, L1 + 2, "    // 64 colours: (R<<4)|(G<<2)|B",      VGA_GREEN, BG);
    vga_print(29, L1 + 2, "    // R, G, B each 0..3",                VGA_GREEN, BG);
    vga_print(30, L1 + 2, "    uint8_t orange = (3<<4)|(2<<2)|0;",    VGA_YELLOW, BG);

    vga_print(32, L1 + 2, "    // === File I/O ===",                   VGA_GREEN, BG);
    vga_print(33, L1 + 2, "    // Drive A: = SD card (if inserted)",  VGA_GREEN, BG);
    vga_print(34, L1 + 2, "    // Drive C: = 360K flash (always)",    VGA_GREEN, BG);
    vga_print(35, L1 + 2, "    // Modes: JAPI_READ JAPI_WRITE",      VGA_GREEN, BG);
    vga_print(36, L1 + 2, "    //        JAPI_APPEND",                VGA_GREEN, BG);
    vga_print(37, L1 + 2, "    japi_file_t f;",                       VGA_YELLOW, BG);
    vga_print(37, L1 + 23, "// file handle",                          VGA_GREEN, BG);
    vga_print(38, L1 + 2, "    char buf[128];",                       VGA_YELLOW, BG);
    vga_print(38, L1 + 23, "// read buffer",                          VGA_GREEN, BG);

    vga_print(40, L1 + 2, "    // Write to SD card",                  VGA_GREEN, BG);
    vga_print(41, L1 + 2, "    if (japi_fopen(&f,\"A:hello.txt\",",   VGA_YELLOW, BG);
    vga_print(42, L1 + 2, "                   JAPI_WRITE)) {",        VGA_YELLOW, BG);
    vga_print(43, L1 + 2, "        japi_fwrite(&f,\"Hello!\\n\",7);", VGA_YELLOW, BG);
    vga_print(44, L1 + 2, "        japi_fclose(&f);",                 VGA_YELLOW, BG);
    vga_print(45, L1 + 2, "    }",                                    VGA_YELLOW, BG);

    vga_print(47, L1 + 2, "    // Copy from SD to built-in media",    VGA_GREEN, BG);
    vga_print(48, L1 + 2, "    if (japi_fopen(&f,\"A:hello.txt\",",   VGA_YELLOW, BG);
    vga_print(49, L1 + 2, "                   JAPI_READ)) {",         VGA_YELLOW, BG);
    vga_print(50, L1 + 2, "        int n=japi_fread(&f,buf,128);",    VGA_YELLOW, BG);
    vga_print(51, L1 + 2, "        japi_fclose(&f);",                 VGA_YELLOW, BG);
    vga_print(52, L1 + 2, "        japi_file_t f2;",                  VGA_YELLOW, BG);
    vga_print(53, L1 + 2, "        if (japi_fopen(&f2,\"C:hello.txt\",", VGA_YELLOW, BG);
    vga_print(54, L1 + 2, "                       JAPI_WRITE)) {",    VGA_YELLOW, BG);
    vga_print(55, L1 + 2, "            japi_fwrite(&f2,buf,n);",      VGA_YELLOW, BG);
    vga_print(56, L1 + 2, "            japi_fclose(&f2);",            VGA_YELLOW, BG);
    vga_print(57, L1 + 2, "        }",                                VGA_YELLOW, BG);
    vga_print(58, L1 + 2, "    }",                                    VGA_YELLOW, BG);

    vga_print(60, L1 + 2, "    // (continues right)",                 VGA_GREEN, BG);
    vga_print(60, L1 + 30, ">>>",                                     VGA_CYAN, BG);

    // === CODE: right panel rows 3-60 ===

    vga_print(3,  R1 + 2, "    // Check if file exists on flash",     VGA_GREEN, BG);
    vga_print(4,  R1 + 2, "    if (japi_exists(\"C:hello.txt\"))",    VGA_YELLOW, BG);
    vga_print(5,  R1 + 2, "        vga_print(4,0,\"OK!\",0x3F,1);",  VGA_YELLOW, BG);

    vga_print(7,  R1 + 2, "    // Read back from built-in media",     VGA_GREEN, BG);
    vga_print(8,  R1 + 2, "    if (japi_fopen(&f,\"C:hello.txt\",",   VGA_YELLOW, BG);
    vga_print(9,  R1 + 2, "                   JAPI_READ)) {",         VGA_YELLOW, BG);
    vga_print(10, R1 + 2, "        int n=japi_fread(&f,buf,127);",    VGA_YELLOW, BG);
    vga_print(11, R1 + 2, "        buf[n]=0;",                        VGA_YELLOW, BG);
    vga_print(11, R1 + 18, "// null-terminate",                       VGA_GREEN, BG);
    vga_print(12, R1 + 2, "        japi_fclose(&f);",                 VGA_YELLOW, BG);
    vga_print(13, R1 + 2, "        vga_print(5,0,buf,VGA_CYAN,1);",   VGA_YELLOW, BG);
    vga_print(14, R1 + 2, "    }",                                    VGA_YELLOW, BG);

    vga_print(16, R1 + 2, "    // Also available:",                   VGA_GREEN, BG);
    vga_print(17, R1 + 2, "    // japi_remove(path)  -> bool",       VGA_GREEN, BG);
    vga_print(18, R1 + 2, "    // japi_mkdir(path)   -> bool",       VGA_GREEN, BG);
    vga_print(19, R1 + 2, "    // japi_fsize(&f)     -> int",        VGA_GREEN, BG);

    vga_print(21, R1 + 2, "    // === Keyboard Input ===",            VGA_GREEN, BG);
    vga_print(22, R1 + 2, "    // japi_has_char() -> bool",           VGA_GREEN, BG);
    vga_print(23, R1 + 2, "    // japi_get_char() -> uint16_t",      VGA_GREEN, BG);
    vga_print(24, R1 + 2, "    while (1) {",                          VGA_YELLOW, BG);
    vga_print(25, R1 + 2, "        if (japi_has_char()) {",           VGA_YELLOW, BG);
    vga_print(26, R1 + 2, "            uint16_t k=japi_get_char();",  VGA_YELLOW, BG);
    vga_print(27, R1 + 2, "            // ASCII: 0x20..0xFF",        VGA_GREEN, BG);
    vga_print(28, R1 + 2, "            if (k >= 0x20 && k < 0x100)",  VGA_YELLOW, BG);
    vga_print(29, R1 + 2, "                vga_set_char(7,0,k,0x3F,1);", VGA_YELLOW, BG);
    vga_print(30, R1 + 2, "        }",                                VGA_YELLOW, BG);
    vga_print(31, R1 + 2, "    }",                                    VGA_YELLOW, BG);
    vga_print(32, R1 + 2, "}",                                        VGA_YELLOW, BG);

    vga_print(34, R1 + 2, "// --- Special Key Constants ---",         VGA_GREEN, BG);
    vga_print(35, R1 + 2, "// JAPI_KEY_ESCAPE",                      VGA_GREEN, BG);
    vga_print(36, R1 + 2, "// _UP _DOWN _LEFT _RIGHT",               VGA_GREEN, BG);
    vga_print(37, R1 + 2, "// _HOME _END _PGUP _PGDN",              VGA_GREEN, BG);
    vga_print(38, R1 + 2, "// _INSERT _DELETE",                      VGA_GREEN, BG);
    vga_print(39, R1 + 2, "// _F1.._F12  _SF1.._SF12  _CF1.._CF12", VGA_GREEN, BG);
    vga_print(40, R1 + 2, "// _TAB _BACKSPACE _ENTER _SPACE",        VGA_GREEN, BG);
    vga_print(41, R1 + 2, "// _NUM0.._NUM9 _NUM_ENTER _NUM_DOT",    VGA_GREEN, BG);
    vga_print(42, R1 + 2, "// _NUM_PLUS _MINUS _MUL _NUM_DIV",      VGA_GREEN, BG);

    vga_print(44, R1 + 2, "// --- Keyboard Layout ---",              VGA_GREEN, BG);
    vga_print(45, R1 + 2, "// Built-in layout: QWERTY_US",          VGA_GREEN, BG);
    vga_print(46, R1 + 2, "// For another, put NAME.kbd and",       VGA_GREEN, BG);
    vga_print(47, R1 + 2, "// config.sys on the SD (A:):",          VGA_GREEN, BG);
    vga_print(48, R1 + 2, "// KEYBOARD MAPPING = AZERTY_BE",         VGA_GREEN, BG);

    // === DIRECTORY LISTING ON A: (right panel, rows 50-55) ===
    // Visible confirmation that Japi Base can read files from the SD
    // card -- the user does not need to write any code to see that the
    // storage works. Up to 4 entries shown; if the card is absent or
    // empty we say so explicitly.
    draw_titled_box(50, R1, 55, R2, " Files on A:\\ ", VGA_GREEN, BG);
    {
        japi_dir_t d;
        if (!japi_opendir(&d, "A:")) {
            vga_print(51, R1 + 2, "(no SD card inserted)", VGA_CYAN, BG);
        } else {
            char name[64];
            int shown = 0;
            while (shown < 4 && japi_readdir(&d, name, sizeof(name))) {
                if (strlen(name) > 40) name[40] = 0;
                vga_print(51 + shown, R1 + 2, name, VGA_WHITE, BG);
                shown++;
            }
            japi_closedir(&d);
            if (shown == 0) {
                vga_print(51, R1 + 2, "(SD card is empty)", VGA_CYAN, BG);
            }
        }
    }

    // === KEYBOARD TEST (right panel, rows 56-62) ===
    draw_titled_box(56, R1, 62, R2, " Keyboard Test ", VGA_GREEN, BG);
    vga_print(57, R1 + 2,  "Char:",                                   VGA_WHITE, BG);
    vga_print(57, R1 + 18, "Hex:",                                    VGA_WHITE, BG);
    vga_print(57, R1 + 36, "Type:",                                   VGA_WHITE, BG);
    int type_row = 58, type_col = R1 + 2;
    int type_left = R1 + 2, type_right = R2 - 2;
    int type_top = 58, type_bottom = 61;
    for (int r = type_top; r <= type_bottom; r++)
        for (int c = type_left; c <= type_right; c++)
            vga_set_char(r, c, ' ', VGA_WHITE, 0x02);

    // Footer
    for (int c = 0; c < VGA_COLS; c++) vga_set_char(63, c, ' ', VGA_BLACK, VGA_WHITE);
    vga_print(63, 2, "ESC = back to showcase", VGA_BLACK, VGA_WHITE);

    while (true) {
        uint16_t c = wait_key();

        char hex_buf[24];
        snprintf(hex_buf, sizeof(hex_buf), "0x%04X  ", c);
        vga_print(57, R1 + 23, hex_buf, VGA_CYAN, BG);

        // Translate numpad keys (NumLock ON) to their ASCII equivalents so the
        // typing panel and "Char:" label show '5' rather than (special). The
        // Hex: column keeps the original code so the underlying value is
        // still visible.
        uint16_t disp = c;
        if (c >= JAPI_KEY_NUM0 && c <= JAPI_KEY_NUM9)  disp = '0' + (c - JAPI_KEY_NUM0);
        else if (c == JAPI_KEY_NUM_DOT)   disp = '.';
        else if (c == JAPI_KEY_NUM_PLUS)  disp = '+';
        else if (c == JAPI_KEY_NUM_MINUS) disp = '-';
        else if (c == JAPI_KEY_NUM_MUL)   disp = '*';
        else if (c == JAPI_KEY_NUM_DIV)   disp = '/';
        else if (c == JAPI_KEY_NUM_ENTER) disp = JAPI_KEY_ENTER;

        if (disp >= 0x20 && disp < 0x100) {
            char ch_buf[16];
            snprintf(ch_buf, sizeof(ch_buf), "'%c'       ", (char)disp);
            vga_print(57, R1 + 8,  ch_buf,           VGA_GREEN,   BG);
            vga_print(57, R1 + 42, "ASCII        ",  VGA_WHITE,   BG);
        } else if (disp < 0x20 && disp > 0) {
            char ch_buf[16];
            snprintf(ch_buf, sizeof(ch_buf), "Ctrl+%c    ", (char)(disp + 'A' - 1));
            vga_print(57, R1 + 8,  ch_buf,           VGA_MAGENTA, BG);
            vga_print(57, R1 + 42, "Control code ",  VGA_WHITE,   BG);
        } else {
            vga_print(57, R1 + 8,  "(special) ",     VGA_YELLOW,  BG);
            vga_print(57, R1 + 42, "Special key  ",  VGA_WHITE,   BG);
        }

        if (c == JAPI_KEY_ESCAPE) break;

        if (disp == JAPI_KEY_ENTER || disp == 0x0D) {
            type_col = type_left;
            type_row++;
            if (type_row > type_bottom) {
                for (int r = type_top; r < type_bottom; r++)
                    for (int k = type_left; k <= type_right; k++)
                        vga_text_buffer[r][k] = vga_text_buffer[r+1][k];
                for (int k = type_left; k <= type_right; k++)
                    vga_set_char(type_bottom, k, ' ', VGA_WHITE, 0x02);
                type_row = type_bottom;
            }
            continue;
        }

        if ((disp == JAPI_KEY_BACKSPACE || disp == 0x08) && type_col > type_left) {
            type_col--;
            vga_set_char(type_row, type_col, ' ', VGA_WHITE, 0x02);
            continue;
        }

        if (disp >= 0x20 && disp < 0x100) {
            vga_set_char(type_row, type_col, (char)disp, VGA_WHITE, 0x02);
            type_col++;
            if (type_col > type_right) {
                type_col = type_left;
                type_row++;
                if (type_row > type_bottom) {
                    for (int r = type_top; r < type_bottom; r++)
                        for (int k = type_left; k <= type_right; k++)
                            vga_text_buffer[r][k] = vga_text_buffer[r+1][k];
                    for (int k = type_left; k <= type_right; k++)
                        vga_set_char(type_bottom, k, ' ', VGA_WHITE, 0x02);
                    type_row = type_bottom;
                }
            }
        }
    }
}

// =========================================================================
// PAGE 4: CPU BENCHMARK (shows the effect of the system clock)
// =========================================================================
//
// VSync-paced animation runs at 60 Hz no matter the CPU clock, so nothing
// elsewhere in the demo visibly changes when the clock goes up -- a faster
// clock only buys more compute per frame. This page makes that visible: it
// runs a fixed, deterministic integer workload and times it with the 1 MHz
// system timer, which is independent of clk_sys, so it is an honest stopwatch.
// Lower time / higher Mops = faster. Flash the 260 MHz and the 324 MHz build
// and compare; the checksum must match (proof the work is identical). The loop
// also keeps the CPU pegged, which doubles as a warm-soak stability test.

#define BENCH_ITERS 20000000u

// Kept in RAM so timing is not skewed by flash/XIP caching -- a fair proxy for
// the interpreter inner loop (integer arithmetic + branches, no memory traffic).
static uint32_t __not_in_flash_func(bench_pass)(void) {
    uint32_t a = 1u, b = 2654435761u;
    for (uint32_t i = 0; i < BENCH_ITERS; i++) {
        a = a * 1103515245u + 12345u;   // LCG step
        a ^= a >> 13;
        a += b;
        b = (b << 1) | (b >> 31);       // rotate left
        b ^= a;
    }
    return a ^ b;
}

static void page_benchmark(void) {
    vga_clear(VGA_BLACK, BG);

    for (int c = 0; c < VGA_COLS; c++) vga_set_char(0, c, ' ', VGA_BLACK, VGA_CYAN);
    vga_print(0, 2, "JAPI BASE  -  CPU Benchmark", VGA_BLACK, VGA_CYAN);

    char line[80];
    snprintf(line, sizeof(line), "System clock: %d MHz   (dot clock ~65 MHz: 1024x768@60)",
             japi_get_cpu_clock_mhz());
    vga_print(2, 2, line, VGA_YELLOW, BG);
    vga_print(3, 2, "Fixed 20,000,000-iteration integer workload, timed each pass.",
              VGA_WHITE, BG);
    vga_print(4, 2, "Timer is independent of the CPU clock, so lower time = faster.",
              VGA_GREEN, BG);

    if (japi_clock_was_reverted()) {
        snprintf(line, sizeof(line),
                 "%d MHz was unstable on this board -- now running %d MHz.",
                 japi_clock_reverted_from(), japi_get_cpu_clock_mhz());
        vga_print(5, 2, line, VGA_RED, BG);
    }

    draw_titled_box(6, 2, 14, 60, " Results ", VGA_CYAN, BG);

    for (int c = 0; c < VGA_COLS; c++) vga_set_char(63, c, ' ', VGA_BLACK, VGA_WHITE);
    vga_print(63, 2, "Shift+T = up-size   t = down-size   (260 / 324 / 390 MHz)   ESC = showcase",
              VGA_BLACK, VGA_WHITE);

    uint64_t best = (uint64_t)-1;
    uint32_t passes = 0;

    while (true) {
        uint64_t t0  = time_us_64();
        uint32_t chk = bench_pass();
        uint64_t dt  = time_us_64() - t0;
        if (dt == 0) dt = 1;
        if (dt < best) best = dt;
        passes++;

        // Mops/s = iterations / microseconds, kept as hundredths. BENCH_ITERS*100
        // = 2e9 fits in 32 bits, so plain unsigned long printing is safe.
        uint32_t dt32           = (dt > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)dt;
        uint32_t best32         = (best > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)best;
        uint32_t mops_x100      = BENCH_ITERS * 100u / dt32;
        uint32_t best_mops_x100 = BENCH_ITERS * 100u / best32;

        snprintf(line, sizeof(line), "Pass time : %lu.%03lu ms        ",
                 (unsigned long)(dt32 / 1000), (unsigned long)(dt32 % 1000));
        vga_print(8, 4, line, VGA_WHITE, BG);
        snprintf(line, sizeof(line), "Throughput: %lu.%02lu Mops/s     ",
                 (unsigned long)(mops_x100 / 100), (unsigned long)(mops_x100 % 100));
        vga_print(9, 4, line, VGA_GREEN, BG);
        snprintf(line, sizeof(line), "Best      : %lu.%03lu ms  (%lu.%02lu Mops/s)   ",
                 (unsigned long)(best32 / 1000), (unsigned long)(best32 % 1000),
                 (unsigned long)(best_mops_x100 / 100), (unsigned long)(best_mops_x100 % 100));
        vga_print(10, 4, line, VGA_CYAN, BG);
        snprintf(line, sizeof(line), "Passes    : %lu        ", (unsigned long)passes);
        vga_print(11, 4, line, VGA_WHITE, BG);
        snprintf(line, sizeof(line), "Checksum  : 0x%08lX  (must match across builds)",
                 (unsigned long)chk);
        vga_print(12, 4, line, VGA_YELLOW, BG);

        vga_update();   // push the stats to the active (scanned) buffer

        if (japi_has_char()) {
            uint16_t k = japi_get_char();
            if (k == JAPI_KEY_ESCAPE) break;
            // 'T' (Shift+T) steps up a tier, 't' steps down (260 / 324 / 390).
            // Both persist the choice and reboot, so neither returns here.
            int cur = japi_get_cpu_clock_mhz();
            if (k == 'T') japi_set_cpu_clock((cur < 324) ? 324 : 390);
            if (k == 't') japi_set_cpu_clock((cur > 324) ? 324 : 260);
        }
    }
}

// =========================================================================
// MAIN
// =========================================================================

int main() {
    japi_init();

    while (true) {
        page_showcase();
        page_bitmap_balls();
        page_bitmap_photo();
        page_api();
        page_benchmark();
    }
}
