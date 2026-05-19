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
#include "japi_base.h"
#include "japi_sim.h"

/* --- ANSI terminal renderer ---------------------------------------------
 * Renders the 127x64 text buffer with truecolour SGR escapes. 2-bit channel
 * (R=bits5-4, G=3-2, B=1-0 of the 6-bit colour) maps to 0/81/174/255, the
 * same levels as tools/gen_starry.py. SGR is only re-emitted when fg/bg
 * changes, to keep the byte count down. Needs a terminal of at least
 * 127 columns x 65 rows (Jan's vertical screen is ideal); a smaller window
 * just wraps. Presented on vga_wait_vblank() — the natural frame boundary,
 * so editor/app code stays unchanged across sim and hardware.
 */
static const int LVL[4] = {0, 81, 174, 255};
static int sim_term_ready = 0;
static void sim_pump(void);   /* defined in the keyboard section below */

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

void vga_wait_vblank(void) { sim_render(); sim_pump(); }  /* present + pump input */

void vga_redefine_char(uint8_t code, const uint8_t bitmap[FONT_H]) {
    (void)code; (void)bitmap;   /* font not modelled in the text simulator */
}

/* --- Bitmap overlay: not modelled in the text simulator (stubs) --- */
bool     japi_bitmap_open(int c,int r,int w,int h,int s){(void)c;(void)r;(void)w;(void)h;(void)s;return false;}
void     japi_bitmap_close(void){}
void     japi_bitmap_pixel(int x,int y,uint8_t v){(void)x;(void)y;(void)v;}
void     japi_bitmap_clear(uint8_t v){(void)v;}
uint8_t *japi_bitmap_buffer(void){return 0;}
int      japi_bitmap_width(void){return 0;}
int      japi_bitmap_height(void){return 0;}

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
 * vga_wait_vblank so editor/app code is unchanged vs hardware. ISIG is off so
 * Ctrl+C/Z arrive as codes 3/26 (an editor needs them); ESC is the quit key.
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

static void sim_pump(void) {
    sim_raw_init();
    if (!sim_raw_on) return;
    unsigned char b[64];
    int n = (int)read(0, b, sizeof b);
    for (int i = 0; i < n; ) {
        unsigned char c = b[i];
        if (c == 0x1b && i + 1 < n && (b[i+1] == '[' || b[i+1] == 'O')) {
            if (i + 2 < n) {
                unsigned char k = b[i+2];
                uint16_t code = sim_csi_letter(k);
                if (code) { sim_key_push(code); i += 3; continue; }
                if (k >= '0' && k <= '9') {           /* ESC [ N ~ */
                    int j = i + 2;
                    while (j < n && b[j] != '~') j++;
                    uint16_t m = 0;
                    switch (k) {
                        case '1': case '7': m = JAPI_KEY_HOME;   break;
                        case '2':           m = JAPI_KEY_INSERT; break;
                        case '3':           m = JAPI_KEY_DELETE; break;
                        case '4': case '8': m = JAPI_KEY_END;    break;
                        case '5':           m = JAPI_KEY_PGUP;   break;
                        case '6':           m = JAPI_KEY_PGDN;   break;
                    }
                    if (m) sim_key_push(m);
                    i = (j < n) ? j + 1 : n;
                    continue;
                }
            }
            i += 2; continue;                          /* unknown CSI: skip */
        }
        if (c == 0x1b)  { sim_key_push(JAPI_KEY_ESCAPE);    i++; continue; }
        if (c == 0x7f)  { sim_key_push(JAPI_KEY_BACKSPACE); i++; continue; }
        if (c == '\r' || c == '\n') { sim_key_push(0x000D); i++; continue; }
        sim_key_push(c);                                /* printable + Ctrl-letters + Tab */
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
    const char *mode = (m & JAPI_WRITE)  ? "wb"
                     : (m & JAPI_APPEND) ? "ab" : "rb";
    FILE *fp = fopen(path, mode);
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
