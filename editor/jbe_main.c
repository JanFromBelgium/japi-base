/* Sim entry-point for JBE: opens a file via the Japi Base file API and runs
   the viewer loop. On the Pico the equivalent main() would call jbe_load
   with a fixed startup file (e.g. "A:scratch.bas"). */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "japi_base.h"
#include "jbe.h"

int main(int argc, char **argv) {
    japi_init();

    jbe_state_t st;
    jbe_init(&st);

    if (argc < 2) {
        vga_clear(VGA_WHITE, VGA_DARK_BLUE);
        vga_print(2, 2, "Usage: jbe <path>   (e.g. A:test.txt)", VGA_YELLOW, VGA_DARK_BLUE);
        vga_print(4, 2, "Press any key to exit.", VGA_CYAN, VGA_DARK_BLUE);
        vga_wait_vblank();
        while (!japi_has_char()) vga_wait_vblank();
        return 1;
    }
    if (!jbe_load(&st, argv[1])) {
        vga_clear(VGA_WHITE, VGA_DARK_BLUE);
        vga_print(2, 2, "Could not open file:", VGA_YELLOW, VGA_DARK_BLUE);
        vga_print(3, 2, argv[1],                VGA_WHITE,  VGA_DARK_BLUE);
        vga_print(5, 2, "Press any key to exit.", VGA_CYAN, VGA_DARK_BLUE);
        vga_wait_vblank();
        while (!japi_has_char()) vga_wait_vblank();
        return 2;
    }

    struct timespec ts = { 0, 10 * 1000 * 1000 };   /* ~10 ms idle */
    for (;;) {
        jbe_render(&st);
        vga_wait_vblank();
        while (japi_has_char()) {
            uint16_t k = japi_get_char();
            if (k == JAPI_KEY_ESCAPE) { jbe_free(&st); return 0; }
            jbe_handle_key(&st, k);
        }
        nanosleep(&ts, 0);
    }
}
