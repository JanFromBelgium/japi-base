/* Japi Base host simulator — Linux backend for the Japi Base API.
 *
 * Implements the same symbols as the Pico's japi_base.c so editor/app code
 * written against japi_base.h compiles and runs unchanged on the dev machine.
 * Dependency-free: pure C + (later) ANSI terminal. No curses/SDL.
 *
 * Status: SKELETON (plan step 2). Text buffer writes are real; the ANSI
 * renderer, keyboard, and file I/O are stubs filled in steps 3-6.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
/* The FatFs `DIR` typedef (used in japi_base.h's japi_dir_t union) clashes
   by name with POSIX's `DIR` from <dirent.h>. We only use FatFs's name as a
   sizing-stub on the sim side, so we rename it for this translation unit
   and pull in dirent.h with the real POSIX type afterwards. */
#define DIR FF_DIR_SIM_STUB
#include "japi_base.h"
#undef DIR
#include <dirent.h>
#include "japi_sim.h"

/* --- ANSI terminal renderer ---------------------------------------------
 * Renders the 127x64 text buffer with truecolour SGR escapes. 2-bit channel
 * (R=bits5-4, G=3-2, B=1-0 of the 6-bit colour) maps to 0/81/174/255, the
 * same levels as tools/gen_starry.py. SGR is only re-emitted when fg/bg
 * changes, to keep the byte count down. Needs a terminal of at least
 * 127 columns x 65 rows (Jan's vertical screen is ideal); a smaller window
 * just wraps. Presented on vga_update() — the natural frame boundary,
 * so editor/app code stays unchanged across sim and hardware.
 */
static const int LVL[4] = {0, 81, 174, 255};
static int sim_term_ready = 0;
static void sim_pump(void);   /* defined in the keyboard section below */

/* --- Bitmap overlay state (mirrors the platform's japi_base.c) -----------
 * A real logical pixel buffer, byte-for-byte identical in semantics to the
 * hardware, so PIXEL/LINE/CIRCLE code is verified the same way on both. The
 * terminal cannot show 8x12 pixels per character cell, so the renderer
 * down-samples each cell to a half-block (U+2580): the top half takes the
 * foreground colour and the bottom half the background, giving two preview
 * pixels per cell in full 64-colour. The logical buffer (and headless tests
 * against it) stay exact; only this on-screen preview is approximate.
 */
static uint8_t *sim_bm_buf  = NULL;  /* front buffer (the one shown) */
static uint8_t *sim_bm_work = NULL;  /* back buffer drawn into (double-buffer only) */
static bool sim_bm_double   = false;
static int sim_bm_col = 0, sim_bm_row = 0;  /* top-left char cell of the window */
static int sim_bm_wch = 0, sim_bm_hch = 0;  /* window size in character cells */
static int sim_bm_scale = 1;
static int sim_bm_lw = 0, sim_bm_lh = 0;    /* logical pixel dimensions */

/* The buffer the program draws into: back buffer when double-buffered. */
static uint8_t *sim_bm_target(void) {
    return sim_bm_double ? sim_bm_work : sim_bm_buf;
}

/* Read the front buffer's colour at a screen-pixel offset within the window,
 * mapping through the scale and clamping to the logical bounds. */
static uint8_t sim_bm_sample(int sx, int sy) {
    int lx = sx / sim_bm_scale, ly = sy / sim_bm_scale;
    if (lx < 0) lx = 0; else if (lx >= sim_bm_lw) lx = sim_bm_lw - 1;
    if (ly < 0) ly = 0; else if (ly >= sim_bm_lh) ly = sim_bm_lh - 1;
    return sim_bm_buf[ly * sim_bm_lw + lx];
}

static void sim_term_restore(void) {
    fputs("\033[0m\033[?25h\033[?1049l", stdout);  /* SGR reset, cursor on, leave alt-screen */
    fflush(stdout);
}

static void sim_render(void) {
    if (!sim_term_ready) {
        fputs("\033[?1049h\033[?25l\033[2J", stdout); /* alt-screen, hide cursor, clear */
        atexit(sim_term_restore);
        sim_term_ready = 1;
    }
    for (int r = 0; r < VGA_ROWS; r++) {
        printf("\033[%d;1H", r + 1);               /* absolute row pos -> no scrolling */
        int pf = -1, pb = -1;                      /* force SGR at start of each row */
        for (int c = 0; c < VGA_COLS; c++) {
            /* Inside the open bitmap window? Draw a half-block preview pixel:
               foreground = upper-half sample, background = lower-half sample. */
            if (sim_bm_buf && r >= sim_bm_row && r < sim_bm_row + sim_bm_hch
                           && c >= sim_bm_col && c < sim_bm_col + sim_bm_wch) {
                int sx     = (c - sim_bm_col) * FONT_W + FONT_W / 2;
                int sy_top = (r - sim_bm_row) * FONT_H + FONT_H / 4;
                int sy_bot = (r - sim_bm_row) * FONT_H + (FONT_H * 3) / 4;
                uint8_t top = sim_bm_sample(sx, sy_top);
                uint8_t bot = sim_bm_sample(sx, sy_bot);
                if (top != pf || bot != pb) {
                    printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm",
                           LVL[(top >> 4) & 3], LVL[(top >> 2) & 3], LVL[top & 3],
                           LVL[(bot >> 4) & 3], LVL[(bot >> 2) & 3], LVL[bot & 3]);
                    pf = top; pb = bot;
                }
                fputs("\xe2\x96\x80", stdout);   /* U+2580 upper half block */
                continue;
            }
            vga_char_t ch = vga_text_buffer[r][c];
            if (ch.fg != pf || ch.bg != pb) {
                printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm",
                       LVL[(ch.fg >> 4) & 3], LVL[(ch.fg >> 2) & 3], LVL[ch.fg & 3],
                       LVL[(ch.bg >> 4) & 3], LVL[(ch.bg >> 2) & 3], LVL[ch.bg & 3]);
                pf = ch.fg; pb = ch.bg;
            }
            unsigned char g = ch.code;
            putchar((g >= 32 && g < 127) ? g : ' ');
        }
        fputs("\033[0m", stdout);                  /* reset, no newline (no scroll) */
    }
    fflush(stdout);
}

/* --- Shared state (same as the platform exposes) --- */
vga_char_t vga_text_buffer[VGA_ROWS][VGA_COLS];
uint8_t    japi_keymap[768] = {0};

/* --- Init --- */
void japi_init(void) {
    memset(vga_text_buffer, 0, sizeof(vga_text_buffer));
}

/* --- VGA text API (real buffer writes; renderer added in step 3) --- */
void vga_clear(uint8_t fg, uint8_t bg) {
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++) {
            vga_text_buffer[r][c].code = ' ';
            vga_text_buffer[r][c].fg   = fg;
            vga_text_buffer[r][c].bg   = bg;
        }
}

void vga_set_char(int row, int col, uint8_t code, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS) return;
    vga_text_buffer[row][col].code = code;
    vga_text_buffer[row][col].fg   = fg;
    vga_text_buffer[row][col].bg   = bg;
}

void vga_print(int row, int col, const char *str, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= VGA_ROWS) return;
    for (int i = 0; str[i] && (col + i) < VGA_COLS; i++)
        vga_set_char(row, col + i, (uint8_t)str[i], fg, bg);
}

void vga_update(void) {
    /* Double-buffered bitmap: swap the just-drawn back buffer to the front
       before presenting, mirroring the hardware's vblank flip. */
    if (sim_bm_double) {
        uint8_t *t = sim_bm_buf; sim_bm_buf = sim_bm_work; sim_bm_work = t;
    }
    sim_render();
    sim_pump();
}  /* present + pump input */

void vga_redefine_char(uint8_t code, const uint8_t bitmap[FONT_H]) {
    (void)code; (void)bitmap;   /* font not modelled in the text simulator */
}

/* --- Bitmap overlay: real buffer, identical semantics to japi_base.c ----- */
bool japi_bitmap_open(int col, int row, int w_chars, int h_chars, int scale,
                      bool double_buffered) {
    if (sim_bm_buf) return false;
    if (scale != 1 && scale != 2) return false;
    int pw = w_chars * FONT_W, ph = h_chars * FONT_H;
    if (pw <= 0 || ph <= 0) return false;
    if (col < 0 || row < 0 || col + w_chars > VGA_COLS || row + h_chars > VGA_ROWS)
        return false;
    int lw = pw / scale, lh = ph / scale;
    /* No fixed RAM cap: the heap is the limit (matches japi_base.c). malloc gates
     * the size; the 127x64 char grid bounds lw*lh to <= 780288. */
    uint8_t *buf = malloc((size_t)lw * lh);
    if (!buf) return false;
    uint8_t *work = NULL;
    if (double_buffered) {
        work = malloc(lw * lh);
        if (!work) { free(buf); return false; }
        memset(work, VGA_BLACK, lw * lh);
    }
    memset(buf, VGA_BLACK, lw * lh);
    sim_bm_col = col; sim_bm_row = row; sim_bm_wch = w_chars; sim_bm_hch = h_chars;
    sim_bm_scale = scale; sim_bm_lw = lw; sim_bm_lh = lh;
    sim_bm_double = double_buffered; sim_bm_work = work; sim_bm_buf = buf;
    return true;
}

void japi_bitmap_close(void) {
    free(sim_bm_buf); free(sim_bm_work);
    sim_bm_buf = NULL; sim_bm_work = NULL; sim_bm_double = false;
}

void japi_bitmap_pixel(int x, int y, uint8_t colour) {
    uint8_t *t = sim_bm_target();
    if (t && x >= 0 && x < sim_bm_lw && y >= 0 && y < sim_bm_lh)
        t[y * sim_bm_lw + x] = colour;
}

void japi_bitmap_clear(uint8_t colour) {
    uint8_t *t = sim_bm_target();
    if (t) memset(t, colour, sim_bm_lw * sim_bm_lh);
}

uint8_t *japi_bitmap_buffer(void) { return sim_bm_target(); }
int      japi_bitmap_width(void)  { return sim_bm_lw; }
int      japi_bitmap_height(void) { return sim_bm_lh; }

/* --- Keyboard ring buffer (same model as the Pico's kbd buffer) --- */
#define SIM_KBD_SIZE 256
static uint16_t sim_kbd[SIM_KBD_SIZE];
static int sim_kbd_head = 0, sim_kbd_tail = 0;

void sim_key_push(uint16_t code) {
    int next = (sim_kbd_head + 1) % SIM_KBD_SIZE;
    if (next != sim_kbd_tail) {           /* drop if full, like the hardware */
        sim_kbd[sim_kbd_head] = code;
        sim_kbd_head = next;
    }
}

void sim_type(const char *s) {
    for (; *s; s++) sim_key_push((uint8_t)*s);
}

/* --- Live raw-terminal input (step 5) ---
 * stdin is put in non-canonical, no-echo mode (VMIN=0/VTIME=0 -> non-blocking).
 * Each pump reads all available bytes at once and translates ASCII + the
 * common CSI/SS3 escape sequences to JAPI_KEY_* codes, pushing them into the
 * same ring buffer as the injection API. Pumped from has_char/get_char and
 * vga_update so editor/app code is unchanged vs hardware. ISIG is off so
 * Ctrl+C/Z arrive as keys (an editor needs them); ESC is the quit key. Ctrl +
 * letter maps to JAPI_KEY_CTRL(uppercase letter) to match the platform driver,
 * with the conventional Ctrl+H/I/M = BS/TAB/ENTER exceptions.
 */
static int sim_raw_on = 0;
static struct termios sim_tios_orig;

static void sim_raw_restore(void) {
    if (sim_raw_on) { tcsetattr(0, TCSANOW, &sim_tios_orig); sim_raw_on = 0; }
}

static void sim_raw_init(void) {
    if (sim_raw_on || !isatty(0)) return;     /* skip when stdin is not a tty */
    tcgetattr(0, &sim_tios_orig);
    struct termios r = sim_tios_orig;
    r.c_lflag &= ~(ICANON | ECHO | ISIG);
    r.c_iflag &= ~(IXON | ICRNL);
    r.c_cc[VMIN] = 0; r.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &r);
    sim_raw_on = 1;
    atexit(sim_raw_restore);
}

static uint16_t sim_csi_letter(unsigned char c) {
    switch (c) {
        case 'A': return JAPI_KEY_UP;    case 'B': return JAPI_KEY_DOWN;
        case 'C': return JAPI_KEY_RIGHT; case 'D': return JAPI_KEY_LEFT;
        case 'H': return JAPI_KEY_HOME;  case 'F': return JAPI_KEY_END;
    }
    return 0;
}

/* Apply a CSI modifier (2=Shift, 5=Ctrl, 6=Ctrl+Shift; others ignored) to a
   base nav code in the 0x0101..0x010A range — same offsets the PRE-K1 PS/2
   decoder uses (Shift +0x60, Ctrl +0x70, Ctrl+Shift +0x80). */
static uint16_t sim_apply_csi_mod(uint16_t base, int mod) {
    if (!base) return 0;
    switch (mod) {
        case 1: case 0: return base;             /* no modifier */
        case 2:         return base + 0x60;      /* Shift */
        case 5:         return base + 0x70;      /* Ctrl */
        case 6:         return base + 0x80;      /* Ctrl+Shift */
        default:        return 0;                /* combos we don't map yet */
    }
}

static void sim_pump(void) {
    sim_raw_init();
    if (!sim_raw_on) return;
    unsigned char b[64];
    int n = (int)read(0, b, sizeof b);
    for (int i = 0; i < n; ) {
        unsigned char c = b[i];
        if (c == 0x1b && i + 1 < n && (b[i+1] == '[' || b[i+1] == 'O')) {
            /* Parse ESC[<p1>[;<p2>]<final> (or SS3 ESC O<final>, no params).
               Modifier comes in via p2, xterm-style: 2=Shift, 5=Ctrl, 6=Ctrl+Shift. */
            int j = i + 2;
            int p1 = -1, p2 = -1;
            if (j < n && b[j] >= '0' && b[j] <= '9') {
                p1 = 0;
                while (j < n && b[j] >= '0' && b[j] <= '9') { p1 = p1*10 + (b[j]-'0'); j++; }
            }
            if (j < n && b[j] == ';') {
                j++;
                if (j < n && b[j] >= '0' && b[j] <= '9') {
                    p2 = 0;
                    while (j < n && b[j] >= '0' && b[j] <= '9') { p2 = p2*10 + (b[j]-'0'); j++; }
                }
            }
            if (j >= n) { i += 2; continue; }     /* incomplete: drop the ESC[ */
            unsigned char final = b[j];
            uint16_t base = sim_csi_letter(final);
            if (!base && final == '~') {
                switch (p1) {
                    case 1: case 7: base = JAPI_KEY_HOME;   break;
                    case 2:         base = JAPI_KEY_INSERT; break;
                    case 3:         base = JAPI_KEY_DELETE; break;
                    case 4: case 8: base = JAPI_KEY_END;    break;
                    case 5:         base = JAPI_KEY_PGUP;   break;
                    case 6:         base = JAPI_KEY_PGDN;   break;
                    /* function keys, xterm "ESC[<n>~" form */
                    case 11: base = JAPI_KEY_F1;  break; case 12: base = JAPI_KEY_F2;  break;
                    case 13: base = JAPI_KEY_F3;  break; case 14: base = JAPI_KEY_F4;  break;
                    case 15: base = JAPI_KEY_F5;  break; case 17: base = JAPI_KEY_F6;  break;
                    case 18: base = JAPI_KEY_F7;  break; case 19: base = JAPI_KEY_F8;  break;
                    case 20: base = JAPI_KEY_F9;  break; case 21: base = JAPI_KEY_F10; break;
                    case 23: base = JAPI_KEY_F11; break; case 24: base = JAPI_KEY_F12; break;
                }
            }
            /* F1..F4 in the SS3 form "ESC O P/Q/R/S" (sent by xterm/gnome). */
            if (!base && (final == 'P' || final == 'Q' || final == 'R' || final == 'S'))
                base = (uint16_t)(JAPI_KEY_F1 + (final - 'P'));
            int mod = (p2 > 0) ? p2 : 1;
            uint16_t code = sim_apply_csi_mod(base, mod);
            if (code) sim_key_push(code);
            i = j + 1;
            continue;
        }
        /* ESC followed by a plain letter = Alt+letter on most terminals.
           Map to JAPI_KEY_ALT(uppercase) so the menu accelerators reach
           the editor the same way the PS/2 driver delivers them. */
        if (c == 0x1b && i + 1 < n) {
            unsigned char nx = b[i+1];
            char up = 0;
            if (nx >= 'a' && nx <= 'z') up = nx - 32;
            else if (nx >= 'A' && nx <= 'Z') up = nx;
            if (up) {
                sim_key_push(JAPI_KEY_ALT_BASE | (uint16_t)up);
                i += 2; continue;
            }
        }
        if (c == 0x1b)  { sim_key_push(JAPI_KEY_ESCAPE);    i++; continue; }
        if (c == 0x7f)  { sim_key_push(JAPI_KEY_BACKSPACE); i++; continue; }
        if (c == '\r' || c == '\n') { sim_key_push(0x000D); i++; continue; }
        if (c == '\t')  { sim_key_push(JAPI_KEY_TAB);       i++; continue; }
        if (c >= 1 && c <= 26) {                       /* Ctrl + letter */
            sim_key_push(JAPI_KEY_CTRL_BASE | ('A' + c - 1));
            i++; continue;
        }
        sim_key_push(c);                                /* printable byte */
        i++;
    }
}

bool japi_has_char(void) {
    sim_pump();
    return sim_kbd_head != sim_kbd_tail;
}

uint16_t japi_get_char(void) {
    sim_pump();
    if (sim_kbd_head == sim_kbd_tail) return 0;
    uint16_t c = sim_kbd[sim_kbd_tail];
    sim_kbd_tail = (sim_kbd_tail + 1) % SIM_KBD_SIZE;
    return c;
}

/* KEYDOWN: a terminal delivers key PRESSES (characters) but no key-up events, so
   the host sim cannot track a live held-state -- always reports "not held". Real
   key-state needs the hardware (the PS/2 driver); test KEYDOWN on the Pico. */
bool japi_keydown(uint16_t code) {
    (void)code;
    return false;
}

/* --- Sound: no-ops on host --- */
void japi_play(uint8_t n,uint16_t d){(void)n;(void)d;}
void japi_play_ch(int c,uint8_t n,uint16_t d){(void)c;(void)n;(void)d;}
void japi_sound_wave(int c,uint8_t t){(void)c;(void)t;}
void japi_sound_freq(int c,uint16_t h){(void)c;(void)h;}
void japi_sound_volume(int c,uint8_t v){(void)c;(void)v;}
void japi_sound_pan(int c,uint8_t p){(void)c;(void)p;}
void japi_sound_envelope(int c,uint16_t a,uint16_t d,uint8_t s,uint16_t r){(void)c;(void)a;(void)d;(void)s;(void)r;}
void japi_sound_note_on(int c){(void)c;}
void japi_sound_note_off(int c){(void)c;}
void japi_sound_off(void){}

/* --- File I/O over stdio ---
 * Drive letters map to local directories under the working dir:
 *   A: -> simdisk_A/   (flash floppy)     C: -> simdisk_C/   (SD card)
 * The FILE* is stashed in the opaque japi_file_t union (>=64 bytes here).
 * f->type: 0 = closed, 1 = open.
 */
static int sim_map_path(const char *p, char *out, int outsz) {
    if (p[0] && p[1] == ':') {
        char d = p[0];
        const char *dir = (d == 'A' || d == 'a') ? "simdisk_A"
                        : (d == 'C' || d == 'c') ? "simdisk_C" : 0;
        if (!dir) return 0;
        mkdir(dir, 0777);                       /* ensure the "disk" exists */
        snprintf(out, outsz, "%s/%s", dir, p + 2);
        return 1;
    }
    return 0;
}

bool japi_fopen(japi_file_t *f, const char *p, uint8_t m) {
    char path[512];
    if (!sim_map_path(p, path, sizeof path)) { f->type = 0; return false; }
    FILE *fp;
    if ((m & JAPI_READ) && (m & JAPI_WRITE)) {     /* random read+write */
        fp = fopen(path, "r+b");                    /* open existing, keep its data */
        if (!fp) fp = fopen(path, "w+b");           /* else create it */
    } else {
        const char *mode = (m & JAPI_WRITE)  ? "wb"
                         : (m & JAPI_APPEND) ? "ab" : "rb";
        fp = fopen(path, mode);
    }
    if (!fp) { f->type = 0; return false; }
    *(FILE **)&f->fat = fp;
    f->type = 1;
    return true;
}

int japi_fread(japi_file_t *f, void *b, int n) {
    if (f->type != 1) return -1;
    return (int)fread(b, 1, (size_t)n, *(FILE **)&f->fat);
}

int japi_fwrite(japi_file_t *f, const void *b, int n) {
    if (f->type != 1) return -1;
    return (int)fwrite(b, 1, (size_t)n, *(FILE **)&f->fat);
}

void japi_fclose(japi_file_t *f) {
    if (f->type == 1) { fclose(*(FILE **)&f->fat); f->type = 0; }
}

bool japi_remove(const char *p) {
    char path[512];
    return sim_map_path(p, path, sizeof path) && remove(path) == 0;
}

bool japi_mkdir(const char *p) {
    char path[512];
    return sim_map_path(p, path, sizeof path) && mkdir(path, 0777) == 0;
}

bool japi_rename(const char *from, const char *to) {
    char a[512], b[512];
    if (!sim_map_path(from, a, sizeof a) || !sim_map_path(to, b, sizeof b)) return false;
    return rename(a, b) == 0;
}

bool japi_rmdir(const char *p) {
    char path[512];
    return sim_map_path(p, path, sizeof path) && rmdir(path) == 0;
}

/* --- CPU clock (host stubs: there is no overclock or reboot on a PC) --- */
int  japi_get_cpu_clock_mhz(void) { return 324; }      /* pretend the default tier */
bool japi_set_cpu_clock(int mhz)  { (void)mhz; return false; }  /* no-op, no reboot */
bool japi_clock_was_reverted(void){ return false; }
int  japi_clock_reverted_from(void){ return 0; }

bool japi_exists(const char *p) {
    char path[512]; struct stat st;
    return sim_map_path(p, path, sizeof path) && stat(path, &st) == 0;
}

int japi_fsize(japi_file_t *f) {
    if (f->type != 1) return -1;
    FILE *fp = *(FILE **)&f->fat;
    long cur = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    long sz = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return (int)sz;
}

/* Random-access seek to byte offset `pos` (0-based, SEEK_SET). Host stdio
   mirror of the Pico f_lseek/lfs_file_seek path. */
bool japi_fseek(japi_file_t *f, int pos) {
    if (f->type != 1) return false;
    return fseek(*(FILE **)&f->fat, (long)pos, SEEK_SET) == 0;
}

bool japi_opendir(japi_dir_t *d, const char *p) {
    char path[512];
    if (!sim_map_path(p, path, sizeof path)) { d->type = 0; return false; }
    DIR *dp = opendir(path);
    if (!dp) { d->type = 0; return false; }
    *(DIR **)&d->fat = dp;
    d->type = 1;
    return true;
}

bool japi_readdir(japi_dir_t *d, char *name_out, int name_max) {
    if (d->type != 1 || name_max <= 0) return false;
    DIR *dp = *(DIR **)&d->fat;
    struct dirent *e;
    while ((e = readdir(dp)) != 0) {
        const char *n = e->d_name;
        if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0))) continue;
        strncpy(name_out, n, (size_t)name_max - 1);
        name_out[name_max - 1] = 0;
        return true;
    }
    return false;
}

void japi_closedir(japi_dir_t *d) {
    if (d->type == 1) { closedir(*(DIR **)&d->fat); d->type = 0; }
}
