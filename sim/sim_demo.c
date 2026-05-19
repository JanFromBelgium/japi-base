/* Step 5 interactive demo: live keyboard via the platform API.
   Type text; special keys are shown by name; ESC quits.
   Run in a terminal of >=127x65. Same source would run on the Pico. */
#define _POSIX_C_SOURCE 199309L   /* nanosleep */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "japi_base.h"

static const char *keyname(uint16_t k) {
    switch (k) {
        case JAPI_KEY_UP:    return "UP";    case JAPI_KEY_DOWN:  return "DOWN";
        case JAPI_KEY_LEFT:  return "LEFT";  case JAPI_KEY_RIGHT: return "RIGHT";
        case JAPI_KEY_HOME:  return "HOME";  case JAPI_KEY_END:   return "END";
        case JAPI_KEY_PGUP:  return "PGUP";  case JAPI_KEY_PGDN:  return "PGDN";
        case JAPI_KEY_INSERT:return "INSERT";case JAPI_KEY_DELETE:return "DELETE";
        case JAPI_KEY_ESCAPE:return "ESCAPE";
    }
    return 0;
}

int main(void) {
    japi_init();
    vga_clear(VGA_WHITE, VGA_DARK_BLUE);
    for (int c = 0; c < VGA_COLS; c++) {
        vga_set_char(0, c, ' ', VGA_BLACK, VGA_CYAN);
        vga_set_char(VGA_ROWS - 1, c, ' ', VGA_BLACK, VGA_CYAN);
    }
    vga_print(0, 2, "Japi Base simulator - step 5: live keyboard", VGA_BLACK, VGA_CYAN);
    vga_print(VGA_ROWS - 1, 2, "Type text  |  arrows/Home/End/PgUp/PgDn  |  ESC = quit",
              VGA_BLACK, VGA_CYAN);
    vga_print(3, 3, "Typed:", VGA_YELLOW, VGA_DARK_BLUE);
    vga_print(5, 3, "Last special key:", VGA_YELLOW, VGA_DARK_BLUE);

    char line[120]; int len = 0; line[0] = 0;
    struct timespec ts = { 0, 10 * 1000 * 1000 };  /* 10 ms */

    for (;;) {
        vga_wait_vblank();                          /* present + pump input */
        while (japi_has_char()) {
            uint16_t k = japi_get_char();
            if (k == JAPI_KEY_ESCAPE) return 0;
            const char *nm = keyname(k);
            if (nm) {
                char buf[40];
                snprintf(buf, sizeof buf, "%-10s   ", nm);
                vga_print(5, 21, buf, VGA_CYAN, VGA_DARK_BLUE);
            } else if (k == 0x0008) {               /* backspace */
                if (len > 0) { line[--len] = 0; vga_set_char(3, 10 + len, ' ',
                                 VGA_WHITE, VGA_DARK_BLUE); }
            } else if (k == 0x000D) {               /* enter: clear the line */
                for (int i = 0; i < len; i++) vga_set_char(3, 10 + i, ' ',
                                 VGA_WHITE, VGA_DARK_BLUE);
                len = 0; line[0] = 0;
            } else if (k >= 32 && k < 127 && len < (int)sizeof(line) - 1) {
                line[len++] = (char)k; line[len] = 0;
                vga_set_char(3, 10 + len - 1, (uint8_t)k, VGA_WHITE, VGA_DARK_BLUE);
            }
        }
        nanosleep(&ts, 0);
    }
}
