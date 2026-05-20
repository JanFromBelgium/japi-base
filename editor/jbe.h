/* Japi Base Editor (JBE) — public API.
 * Uses ONLY japi_base.h symbols (the platform seam). The same source builds
 * for both the Linux simulator and the Pico firmware.
 *
 * MVP step 1: read-only viewer (line-pointer buffer + render rows 1-62 +
 * status bar + vertical/horizontal scroll + cursor; no editing yet). */
#ifndef JBE_H
#define JBE_H

#include <stdint.h>
#include <stdbool.h>
#include "japi_base.h"

#define JBE_TITLE_ROW    0
#define JBE_VIEW_TOP     1
#define JBE_VIEW_BOTTOM  62
#define JBE_VIEW_HEIGHT  (JBE_VIEW_BOTTOM - JBE_VIEW_TOP + 1)   /* 62 */
#define JBE_VIEW_WIDTH   VGA_COLS                                /* 127 */
#define JBE_STATUS_ROW   63
#define JBE_NAME_MAX     63

typedef struct {
    char **lines;        /* dynamic array of line strings (no trailing \n) */
    int   *len;          /* cached length per line */
    int    n_lines;
    int    cap_lines;
    int    cur_row;      /* cursor file row (0-based) */
    int    cur_col;      /* cursor column within the line */
    int    top_row;      /* viewport top = file row at screen row JBE_VIEW_TOP */
    int    left_col;     /* viewport left = file column at screen col 0 */
    char   filename[JBE_NAME_MAX + 1];
} jbe_state_t;

void jbe_init(jbe_state_t *s);
bool jbe_load(jbe_state_t *s, const char *path);
void jbe_free(jbe_state_t *s);

void jbe_handle_key(jbe_state_t *s, uint16_t k);
void jbe_render(const jbe_state_t *s);

#endif
