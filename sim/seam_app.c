/* Seam proof (step 7): a portable app that uses ONLY japi_base.h symbols —
 * no host headers, no sim headers, no platform-specific calls. The exact same
 * source is valid as a Pico firmware main(); here it links against the host
 * backend (japi_sim.c). Non-interactive so it runs headless: draw, present a
 * few frames, exit. On the Pico the loop would simply not terminate. */
#include "japi_base.h"

int main(void) {
    japi_init();
    vga_clear(VGA_WHITE, VGA_DARK_BLUE);
    vga_print(2, 4, "Seam proof: this source builds for sim AND Pico,",
              VGA_YELLOW, VGA_DARK_BLUE);
    vga_print(3, 4, "using only the japi_base.h platform API.",
              VGA_YELLOW, VGA_DARK_BLUE);
    vga_print(5, 4, "Press a key on hardware; the sim run just exits.",
              VGA_CYAN, VGA_DARK_BLUE);

    for (int frame = 0; frame < 3; frame++) {
        vga_update();
        if (japi_has_char() && japi_get_char() == JAPI_KEY_ESCAPE) break;
    }
    return 0;
}
