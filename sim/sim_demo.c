/* Step 3 smoke test: exercise the text API + ANSI renderer.
   Shows all 64 colours as fg/bg swatches, a framed title, and an ASCII
   border, then presents one frame via vga_wait_vblank().
   Run it in a terminal of >=127x65 to view; ESC handling comes in step 5. */
#include <stdio.h>
#include "japi_base.h"

int main(void) {
    japi_init();
    vga_clear(VGA_WHITE, VGA_BLACK);

    /* ASCII frame around the whole 127x64 screen */
    for (int c = 0; c < VGA_COLS; c++) {
        vga_set_char(0, c, '-', VGA_CYAN, VGA_BLACK);
        vga_set_char(VGA_ROWS - 1, c, '-', VGA_CYAN, VGA_BLACK);
    }
    for (int r = 0; r < VGA_ROWS; r++) {
        vga_set_char(r, 0, '|', VGA_CYAN, VGA_BLACK);
        vga_set_char(r, VGA_COLS - 1, '|', VGA_CYAN, VGA_BLACK);
    }

    vga_print(2, 3, "Japi Base simulator - step 3: ANSI renderer (127x64, 64 colours)",
              VGA_YELLOW, VGA_BLACK);

    /* 64-colour palette: 4 rows x 16, each cell shows its hex code in fg too */
    vga_print(4, 3, "Background swatches (all 64 colours):", VGA_WHITE, VGA_BLACK);
    for (int col6 = 0; col6 < 64; col6++) {
        int rr = 5 + col6 / 16;
        int cc = 3 + (col6 % 16) * 4;
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", col6);
        vga_set_char(rr, cc,     ' ', VGA_WHITE, (uint8_t)col6);
        vga_set_char(rr, cc + 1, hex[0], VGA_WHITE, (uint8_t)col6);
        vga_set_char(rr, cc + 2, hex[1], VGA_WHITE, (uint8_t)col6);
    }

    vga_print(11, 3, "Foreground swatches (all 64 colours):", VGA_WHITE, VGA_BLACK);
    for (int col6 = 0; col6 < 64; col6++) {
        int rr = 12 + col6 / 16;
        int cc = 3 + (col6 % 16) * 4;
        vga_set_char(rr, cc,     '#', (uint8_t)col6, VGA_BLACK);
        vga_set_char(rr, cc + 1, '#', (uint8_t)col6, VGA_BLACK);
        vga_set_char(rr, cc + 2, '#', (uint8_t)col6, VGA_BLACK);
    }

    vga_print(18, 3, "Named colours:", VGA_WHITE, VGA_BLACK);
    vga_print(19, 3, "RED",     VGA_RED,     VGA_BLACK);
    vga_print(19, 10, "GREEN",  VGA_GREEN,   VGA_BLACK);
    vga_print(19, 18, "BLUE",   VGA_BLUE,    VGA_BLACK);
    vga_print(19, 25, "YELLOW", VGA_YELLOW,  VGA_BLACK);
    vga_print(19, 34, "CYAN",   VGA_CYAN,    VGA_BLACK);
    vga_print(19, 41, "MAGENTA",VGA_MAGENTA, VGA_BLACK);

    vga_print(VGA_ROWS - 2, 3, "Same source compiles for the Pico - this is the host backend.",
              VGA_GREEN, VGA_BLACK);

    vga_wait_vblank();          /* present one frame */
    printf("\n");               /* drop the prompt below the grid */
    return 0;
}
