/* Headless test for JBE MVP step 1.
 * Writes a small file to A: via the platform API, loads it, drives keys
 * through the sim injection API, and asserts cursor/viewport behaviour.
 * Exit 0 = pass. */
#include <stdio.h>
#include <string.h>
#include "japi_base.h"
#include "japi_sim.h"
#include "jbe.h"

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

/* Build a small fixture file on the simulated A: drive. */
static bool make_fixture(const char *path, const char *content) {
    japi_file_t f;
    if (!japi_fopen(&f, path, JAPI_WRITE)) return false;
    int n = (int)strlen(content);
    bool ok = (japi_fwrite(&f, content, n) == n);
    japi_fclose(&f);
    return ok;
}

int main(void) {
    japi_init();

    /* 5 lines, with a deliberately long 3rd line for horizontal scroll. */
    const char *FIXTURE =
        "first line\n"
        "second\n"
        "this is a much longer line used to exercise horizontal scrolling well past 127 columns -- xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
        "fourth\n"
        "fifth and last\n";
    CHECK(make_fixture("A:jbe_t.txt", FIXTURE), "write fixture to A:");

    jbe_state_t st;
    jbe_init(&st);
    CHECK(jbe_load(&st, "A:jbe_t.txt"), "load fixture");
    CHECK(st.n_lines == 5,              "5 lines parsed");
    CHECK(st.cur_row == 0 && st.cur_col == 0, "cursor starts at (0,0)");
    CHECK(st.top_row == 0 && st.left_col == 0, "viewport starts at (0,0)");

    /* DOWN/RIGHT/END/HOME basics */
    jbe_handle_key(&st, JAPI_KEY_DOWN);  CHECK(st.cur_row == 1, "DOWN -> row 1");
    jbe_handle_key(&st, JAPI_KEY_END);   CHECK(st.cur_col == st.len[1], "END at end of 'second'");
    jbe_handle_key(&st, JAPI_KEY_HOME);  CHECK(st.cur_col == 0, "HOME -> col 0");

    /* Column clipped after vertical move into a shorter line */
    st.cur_col = 0; st.cur_row = 0;
    jbe_handle_key(&st, JAPI_KEY_END);   /* col = strlen("first line")=10 */
    jbe_handle_key(&st, JAPI_KEY_DOWN);  /* "second" len 6 -> col clipped to 6 */
    CHECK(st.cur_col == 6, "column clipped to shorter line");

    /* Horizontal auto-scroll on the long line */
    st.cur_row = 2; st.cur_col = 0; st.left_col = 0;
    jbe_handle_key(&st, JAPI_KEY_END);
    CHECK(st.cur_col == st.len[2], "END jumped to end of long line");
    CHECK(st.left_col > 0,         "viewport scrolled right to follow cursor");

    /* Vertical scroll past viewport with PgDn (file has only 5 lines so it
       clamps to last line; viewport adjusts to keep cursor visible). */
    st.cur_row = 0; st.top_row = 0;
    jbe_handle_key(&st, JAPI_KEY_PGDN);
    CHECK(st.cur_row == st.n_lines - 1, "PGDN clamps to last line");

    /* Render does not crash and leaves cursor cell distinguishable */
    jbe_render(&st);
    int sr = JBE_VIEW_TOP + (st.cur_row - st.top_row);
    int sc = st.cur_col - st.left_col;
    if (sc >= 0 && sc < JBE_VIEW_WIDTH) {
        vga_char_t cell = vga_text_buffer[sr][sc];
        CHECK(cell.fg == VGA_DARK_BLUE && cell.bg == VGA_WHITE,
              "cursor cell rendered as reverse video");
    }

    jbe_free(&st);
    japi_remove("A:jbe_t.txt");

    if (fails == 0) { printf("PASS: JBE MVP step 1 (read-only viewer)\n"); return 0; }
    printf("%d check(s) failed\n", fails);
    return 1;
}
