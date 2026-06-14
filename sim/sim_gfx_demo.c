/* Interactive visual demo of the simulator's bitmap graphics.
 *
 * Opens a bitmap window and draws a gradient, an X and a filled box, so the
 * half-block compositor can be seen in a real terminal. Run in a terminal of at
 * least 127x65:   make sim_gfx_demo && ./sim_gfx_demo
 * Press any key to quit.
 */
#include "japi_base.h"

int main(void) {
    japi_init();
    vga_clear(VGA_WHITE, VGA_BLACK);
    vga_print(0, 2, "Japi Base sim - bitmap graphics (press any key to quit)",
              VGA_YELLOW, VGA_BLACK);

    /* 100x50 cells at scale 2 -> 400x300 logical px (120 KB, within the cap). */
    if (!japi_bitmap_open(4, 3, 100, 50, 2, false)) {
        vga_print(2, 2, "bitmap open failed", VGA_RED, VGA_BLACK);
        vga_update();
        return 1;
    }
    int W = japi_bitmap_width(), H = japi_bitmap_height();

    /* Red/green gradient background (R in the high bits, G in the next two). */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            japi_bitmap_pixel(x, y,
                (uint8_t)((((x * 4 / W) & 3) << 4) | (((y * 4 / H) & 3) << 2)));

    /* A white X across the window. */
    int n = (W < H) ? W : H;
    for (int i = 0; i < n; i++) {
        japi_bitmap_pixel(i * W / n, i * H / n, VGA_WHITE);
        japi_bitmap_pixel(W - 1 - i * W / n, i * H / n, VGA_WHITE);
    }

    /* A cyan filled box in the middle. */
    for (int y = H / 3; y < 2 * H / 3; y++)
        for (int x = W / 3; x < 2 * W / 3; x++)
            japi_bitmap_pixel(x, y, VGA_CYAN);

    vga_update();
    while (!japi_has_char()) vga_update();
    return 0;
}
