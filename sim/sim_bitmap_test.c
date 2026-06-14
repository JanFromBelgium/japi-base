/* Headless byte-exact test of the simulator's bitmap overlay.
 *
 * The logical buffer has identical semantics to the Pico's japi_base.c, so this
 * verifies the geometry, bounds, cap and double-buffer draw-target rules without
 * a terminal or any rendering. Run with: make test   (in this directory)
 */
#include "japi_base.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL: %s\n", (m)); fails++; } } while (0)

int main(void) {
    /* Open a 10x5-cell window at scale 1 -> 80x60 logical pixels. */
    CHECK(japi_bitmap_open(2, 2, 10, 5, 1, false), "open valid window");
    CHECK(japi_bitmap_width()  == 80, "width  = w_chars * 8");
    CHECK(japi_bitmap_height() == 60, "height = h_chars * 12");

    /* A written pixel is stored at the right offset and read back via buffer(). */
    japi_bitmap_pixel(3, 4, 0x2A);
    CHECK(japi_bitmap_buffer()[4 * 80 + 3] == 0x2A, "pixel stored at y*w+x");

    /* Out-of-bounds writes are ignored (no crash, in-bounds data untouched). */
    japi_bitmap_pixel(-1, 0, 0x3F);
    japi_bitmap_pixel(80, 0, 0x3F);
    japi_bitmap_pixel(0, 60, 0x3F);
    CHECK(japi_bitmap_buffer()[0] == VGA_BLACK, "out-of-bounds pixel ignored");

    /* clear fills the whole buffer. */
    japi_bitmap_clear(0x15);
    CHECK(japi_bitmap_buffer()[0] == 0x15 &&
          japi_bitmap_buffer()[80 * 60 - 1] == 0x15, "clear fills the buffer");

    /* Only one window at a time. */
    CHECK(!japi_bitmap_open(0, 0, 4, 4, 1, false), "second open refused");
    japi_bitmap_close();

    /* After close, a scale-2 window halves the logical dimensions. */
    CHECK(japi_bitmap_open(0, 0, 4, 4, 2, false), "reopen at scale 2");
    CHECK(japi_bitmap_width()  == 16, "scale-2 width  = w_chars*8/2");
    CHECK(japi_bitmap_height() == 24, "scale-2 height = h_chars*12/2");
    japi_bitmap_close();

    /* Invalid configurations are refused. */
    CHECK(!japi_bitmap_open(0, 0, 4, 4, 3, false),   "bad scale refused");
    CHECK(!japi_bitmap_open(0, 0, 200, 4, 1, false), "off-screen width refused");
    CHECK(!japi_bitmap_open(0, 0, 127, 64, 1, false),"over-cap buffer refused");

    /* Double-buffered: pixels land in the back buffer, which buffer() returns. */
    CHECK(japi_bitmap_open(0, 0, 8, 4, 1, true), "open double-buffered");
    japi_bitmap_clear(0x07);
    CHECK(japi_bitmap_buffer()[0] == 0x07, "double-buffer draw target written");
    japi_bitmap_close();

    if (fails == 0) { printf("sim_bitmap_test: ALL PASS\n"); return 0; }
    printf("sim_bitmap_test: %d FAILURE(S)\n", fails);
    return 1;
}
