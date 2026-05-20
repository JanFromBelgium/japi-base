/* JBE MVP step 1 — read-only viewer.
 * Loads a text file via the Japi Base file API, keeps it as an array of line
 * strings, and lets the user navigate with arrows / Home / End / PgUp / PgDn.
 * The viewport auto-scrolls so the cursor stays visible.
 *
 * Colour scheme is the classic Turbo Pascal / QuickBASIC feel: dark-blue
 * background, white text, cyan title/status bars, the cursor cell as a
 * reverse-video block.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jbe.h"

#define JBE_BG          VGA_DARK_BLUE
#define JBE_FG          VGA_WHITE
#define JBE_BAR_FG      VGA_BLACK
#define JBE_BAR_BG      VGA_CYAN

/* --- Line buffer helpers ---------------------------------------------- */

static bool jbe_push_line(jbe_state_t *s, const char *buf, int n) {
    if (s->n_lines == s->cap_lines) {
        int nc = s->cap_lines ? s->cap_lines * 2 : 64;
        char **nl = realloc(s->lines, nc * sizeof *nl);
        int   *nlen = realloc(s->len,  nc * sizeof *nlen);
        if (!nl || !nlen) { free(nl); free(nlen); return false; }
        s->lines = nl; s->len = nlen; s->cap_lines = nc;
    }
    char *line = malloc(n + 1);
    if (!line) return false;
    memcpy(line, buf, n);
    line[n] = 0;
    s->lines[s->n_lines]   = line;
    s->len[s->n_lines]     = n;
    s->n_lines++;
    return true;
}

void jbe_init(jbe_state_t *s) {
    memset(s, 0, sizeof *s);
    strcpy(s->filename, "(no file)");
}

void jbe_free(jbe_state_t *s) {
    for (int i = 0; i < s->n_lines; i++) free(s->lines[i]);
    free(s->lines); free(s->len);
    jbe_init(s);
}

bool jbe_load(jbe_state_t *s, const char *path) {
    jbe_free(s);
    /* keep just the basename in the title; tolerate paths without separator */
    const char *bn = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') bn = p + 1;
    strncpy(s->filename, bn, JBE_NAME_MAX);
    s->filename[JBE_NAME_MAX] = 0;

    japi_file_t f;
    if (!japi_fopen(&f, path, JAPI_READ)) return false;
    int sz = japi_fsize(&f);
    if (sz < 0) { japi_fclose(&f); return false; }

    char *all = sz ? malloc(sz) : 0;
    if (sz && !all) { japi_fclose(&f); return false; }
    int got = sz ? japi_fread(&f, all, sz) : 0;
    japi_fclose(&f);
    if (got < 0) { free(all); return false; }

    /* Split on '\n'; strip a trailing '\r' so CRLF files render cleanly. */
    int start = 0;
    for (int i = 0; i < got; i++) {
        if (all[i] == '\n') {
            int end = i;
            if (end > start && all[end - 1] == '\r') end--;
            if (!jbe_push_line(s, all + start, end - start)) { free(all); return false; }
            start = i + 1;
        }
    }
    if (start < got) {                   /* trailing line without newline */
        if (!jbe_push_line(s, all + start, got - start)) { free(all); return false; }
    }
    if (s->n_lines == 0) jbe_push_line(s, "", 0);   /* always at least one */
    free(all);
    return true;
}

/* --- Navigation ------------------------------------------------------- */

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Keep the cursor inside the viewport by scrolling as needed. */
static void jbe_follow_cursor(jbe_state_t *s) {
    if (s->cur_row < s->top_row)               s->top_row = s->cur_row;
    if (s->cur_row >= s->top_row + JBE_VIEW_HEIGHT)
        s->top_row = s->cur_row - JBE_VIEW_HEIGHT + 1;
    if (s->cur_col < s->left_col)              s->left_col = s->cur_col;
    if (s->cur_col >= s->left_col + JBE_VIEW_WIDTH)
        s->left_col = s->cur_col - JBE_VIEW_WIDTH + 1;
    if (s->top_row  < 0) s->top_row  = 0;
    if (s->left_col < 0) s->left_col = 0;
}

void jbe_handle_key(jbe_state_t *s, uint16_t k) {
    if (s->n_lines == 0) return;
    int line_len = s->len[s->cur_row];
    switch (k) {
        case JAPI_KEY_UP:    s->cur_row = clampi(s->cur_row - 1, 0, s->n_lines - 1); break;
        case JAPI_KEY_DOWN:  s->cur_row = clampi(s->cur_row + 1, 0, s->n_lines - 1); break;
        case JAPI_KEY_LEFT:  s->cur_col = clampi(s->cur_col - 1, 0, line_len);       break;
        case JAPI_KEY_RIGHT: s->cur_col = clampi(s->cur_col + 1, 0, line_len);       break;
        case JAPI_KEY_HOME:  s->cur_col = 0;                                          break;
        case JAPI_KEY_END:   s->cur_col = line_len;                                   break;
        case JAPI_KEY_PGUP:  s->cur_row = clampi(s->cur_row - JBE_VIEW_HEIGHT, 0, s->n_lines - 1); break;
        case JAPI_KEY_PGDN:  s->cur_row = clampi(s->cur_row + JBE_VIEW_HEIGHT, 0, s->n_lines - 1); break;
        default: return;
    }
    /* Clip the column to the new line's length after a vertical move. */
    if (s->cur_col > s->len[s->cur_row]) s->cur_col = s->len[s->cur_row];
    jbe_follow_cursor(s);
}

/* --- Rendering -------------------------------------------------------- */

static void fill_row(int row, uint8_t fg, uint8_t bg) {
    for (int c = 0; c < VGA_COLS; c++) vga_set_char(row, c, ' ', fg, bg);
}

void jbe_render(const jbe_state_t *s) {
    /* Title bar */
    fill_row(JBE_TITLE_ROW, JBE_BAR_FG, JBE_BAR_BG);
    char title[VGA_COLS + 1];
    snprintf(title, sizeof title, " JBE - %s", s->filename);
    vga_print(JBE_TITLE_ROW, 0, title, JBE_BAR_FG, JBE_BAR_BG);

    /* Visible lines */
    for (int r = 0; r < JBE_VIEW_HEIGHT; r++) {
        int screen_row = JBE_VIEW_TOP + r;
        int file_row   = s->top_row + r;
        fill_row(screen_row, JBE_FG, JBE_BG);
        if (file_row >= s->n_lines) continue;
        const char *line = s->lines[file_row];
        int len  = s->len[file_row];
        int from = s->left_col;
        for (int c = 0; c < JBE_VIEW_WIDTH && from + c < len; c++) {
            unsigned char ch = (unsigned char)line[from + c];
            if (ch < 32 || ch == 127) ch = '.';     /* show controls as '.' */
            vga_set_char(screen_row, c, ch, JBE_FG, JBE_BG);
        }
    }

    /* Cursor: reverse-video block, only when visible */
    int cr = s->cur_row - s->top_row;
    int cc = s->cur_col - s->left_col;
    if (cr >= 0 && cr < JBE_VIEW_HEIGHT && cc >= 0 && cc < JBE_VIEW_WIDTH) {
        int sr = JBE_VIEW_TOP + cr;
        vga_char_t cell = vga_text_buffer[sr][cc];
        vga_set_char(sr, cc, cell.code ? cell.code : ' ', JBE_BG, JBE_FG);
    }

    /* Status bar */
    fill_row(JBE_STATUS_ROW, JBE_BAR_FG, JBE_BAR_BG);
    char status[VGA_COLS + 1];
    snprintf(status, sizeof status,
             " Ln %d, Col %d   %d line%s    ESC = quit",
             s->cur_row + 1, s->cur_col + 1, s->n_lines, s->n_lines == 1 ? "" : "s");
    vga_print(JBE_STATUS_ROW, 0, status, JBE_BAR_FG, JBE_BAR_BG);
}
