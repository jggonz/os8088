/* ============================================================================
 * os8088 - apps/runcpm/hosttest/rcuitest.c    RUNCPM's terminal, on the host
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and apps/runcpm/build.sh compiles and runs it BEFORE the target build; a
 * failure stops the build.
 *
 * WHY. rcterm.c is a damage model (SPEC.md 71.2): a shadow of the glass, a
 * per-row compare, one call per changed span, one gfx_scroll for N scrolled
 * lines. Every one of those decisions is a chance to leave a stale cell or to
 * draw a cell twice, and PERFORMANCE.md is explicit that neither shows in an
 * emulator's screendump. So this stubs the API with a MODEL OF THE GLASS -
 * font_run and blit1 record what they put in each cell, gfx_scroll moves it
 * and fills the vacated rows with GARBAGE (legal: SPEC.md 5.5), gfx_fill
 * whitens - includes the whole program (runcpm.c and its parts) against it,
 * feeds it byte streams the way the Z80 side will, and after every step
 * asserts three things cell for cell: the glass shows the model, the shadow
 * describes the glass, and - for the scripted streams - the row reads what a
 * VT100 would have shown. Then it prints THE COST TABLE: calls and cells are
 * exact, and a 'model ms' column prices them PER PRIMITIVE from
 * PERFORMANCE.md's measurements (print_costs names the rates; the band's is
 * this package's own, from tests/rcband - Set 65) - a model, not a
 * measurement of the whole path, until a counter on the target replaces it.
 *
 * WHAT IT CANNOT SEE. `int` is four bytes here and two there; there is no
 * font - a glyph is 'this cell holds this character', and rc_band (assembly
 * on the target, apps/runcpm/rcband.inc) is modelled as identity: row 0 of
 * the band carries the character, row 1 the attribute, and the stub blit1
 * decodes them back. rc_band's real output is judged on the glass in QEMU.
 *
 * HOW IT IS BUILT. hosttest/os88.h is a STUB of apps/cc/os88.h with this
 * directory ahead of apps/cc on the include path:
 *
 *   cc -O1 -w -I apps/runcpm/hosttest -I apps/runcpm \
 *      -o build/rcuitest apps/runcpm/hosttest/rcuitest.c && build/rcuitest
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GW 84                    /* cells across: the content starts
                                  * one cell in and is 80 wide */
#define GH 480                   /* pixel rows */

static char gl_ch[GH][GW];       /* the character a call left in the cell
                                  * whose glyph band starts at pixel row y */
static unsigned char gl_rev[GH][GW];

static int n_run, n_blit, n_scroll, n_fill, n_blit_refused;
static int n_cells;              /* every cell put down, by any primitive */
static int n_run_cells;          /* ...of which font_run lettered */
static int n_blit_cells;         /* ...and blit1 emitted from a band */
static int n_scroll_rows;        /* pixel rows a gfx_scroll moved */
static int n_fill_rows;          /* pixel rows a gfx_fill whitened */
static int fill_w;               /* the last fill's width in pixels */
static int pen;
static int blit1_refuses;        /* model a kern_small machine */
static int scroll_refuses;

static void gclear(void)
{
    memset(gl_ch, ' ', sizeof(gl_ch));
    memset(gl_rev, 0, sizeof(gl_rev));
}

static void callreset(void)
{
    n_run = n_blit = n_scroll = n_fill = n_blit_refused = 0;
    n_cells = n_run_cells = n_blit_cells = n_scroll_rows = n_fill_rows = 0;
}

static int calls(void) { return n_run + n_blit + n_scroll + n_fill; }

/* --- the fake window ------------------------------------------------------ */
static int cont_x = 8, cont_y = 38, cont_w = 640, cont_h = 200;
static int lock_depth;
static int wakes_posted;
static int fullscreen;
static char last_toast[128];
static int damage_whole = 1;
static struct os88_rect damage_rect;

/* --- a fake folder tree: home -> A (clus 5) -> 0 (clus 9) -> RCPROBE.TXT --- */
static unsigned std_clus;        /* where the machine stands */
static int std_vol;
static int ferr;

#include "os88.h"

void os88_gfx_lock(void)   { lock_depth++; }
void os88_gfx_unlock(void) { lock_depth--; }
void os88_set_color(int c) { pen = c; }

static void need_lock(const char *who)
{
    if (lock_depth != 1) {
        printf("HARNESS: %s called with lock depth %d\n", who, lock_depth);
        exit(1);
    }
}

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int x, y, c;
    need_lock("gfx_fill");
    n_fill++;
    n_fill_rows += y2 - y1 + 1;
    fill_w = x2 - x1 + 1;
    for (c = x1 / 8; c <= x2 / 8 && c < GW; c++)
        for (y = 0; y + 7 < GH; y++)
            if (y >= y1 && y + 7 <= y2) {
                gl_ch[y][c] = (pen == OS88_WHITE) ? ' ' : '#';
                gl_rev[y][c] = 0;
            }
    (void)x;
}

int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int y, c;
    need_lock("gfx_scroll");
    if (scroll_refuses) return -1;
    if ((x1 & 7) != 0 || ((x2 + 1) & 7) != 0) {
        printf("HARNESS: gfx_scroll refused: x1=%d x2=%d not byte aligned\n",
               x1, x2);
        return -1;
    }
    n_scroll++;
    n_scroll_rows += y2 - y1 + 1;
    if (dy > 0) {
        for (y = y1; y <= y2 - dy; y++)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = gl_ch[y + dy][c];
                gl_rev[y][c] = gl_rev[y + dy][c];
            }
        for (y = y2 - dy + 1; y <= y2; y++)      /* vacated: GARBAGE (5.5) */
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = '?';
                gl_rev[y][c] = 1;
            }
    } else {
        dy = -dy;
        for (y = y2; y >= y1 + dy; y--)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = gl_ch[y - dy][c];
                gl_rev[y][c] = gl_rev[y - dy][c];
            }
        for (y = y1; y < y1 + dy; y++)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = '?';
                gl_rev[y][c] = 1;
            }
    }
    return 0;
}

/* the band is rc_band's (below): row 0 = the character, row 1 = reverse */
int os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *b = bits;
    int c, n = w / 8;
    need_lock("gfx_blit1");
    if (blit1_refuses) { n_blit_refused++; return -1; }
    if ((x & 7) || (w & 7) || rows < 1 || rows > 255) {
        printf("HARNESS: blit1 refused: x=%d w=%d rows=%d\n", x, w, rows);
        return -1;
    }
    if (rows != 8 || stride != 80) {       /* the band's stride is RC_COLS,
                                              * always (rcband.inc) */
        printf("HARNESS: blit1 with rows=%d stride=%d w=%d - not a cell row\n",
               rows, stride, w);
        exit(1);
    }
    n_blit++;
    n_cells += n;
    n_blit_cells += n;
    for (c = 0; c < n; c++) {
        gl_ch[y][x / 8 + c] = (char)b[c];
        gl_rev[y][x / 8 + c] = b[stride + c];
    }
    return 0;
}

void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int c = x / 8;
    need_lock("font_run");
    if (x & 7) {
        printf("HARNESS: font_run at x=%d - not on a cell boundary\n", x);
        exit(1);
    }
    n_run++;
    while (*s) {
        gl_ch[y][c] = *s;
        gl_rev[y][c] = (ink == OS88_WHITE && paper == OS88_BLACK);
        n_cells++;
        n_run_cells++;
        c++;
        s++;
    }
}

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{ (void)x; (void)y; (void)w; (void)h; (void)title; return (void *)1; }
void os88_wm_content(void *win, struct os88_pt *o) { o->x = cont_x; o->y = cont_y; }
int  os88_wm_geom(void *win, struct os88_size *s) { s->w = cont_w; s->h = cont_h; return 0; }
int  os88_wm_damage(void *win, struct os88_rect *r)
{
    if (damage_whole) {
        r->x1 = cont_x; r->y1 = cont_y;
        r->x2 = cont_x + cont_w - 1; r->y2 = cont_y + cont_h - 1;
        return 1;
    }
    *r = damage_rect;
    return 0;
}
int  os88_wm_clip_set(void *win) { return 0; }
void os88_wm_snap(void *win, int on) {}
void os88_wm_ownbg(void *win, int on) {}
void os88_wm_onwake(void *win) {}
int  os88_wm_wake(void *win) { wakes_posted++; return 0; }
/* OSAPI_FULLSCREEN paints SYNCHRONOUSLY inside the slot (kernel/wm.inc
 * wm_fullscreen: wm_raise whole on enter, wm_paint_all on exit), under the
 * lock the caller holds - so the stub does what the machine does: the new
 * geometry, then os88_paint whole (LESSONS.md 7: a stub models the machine).
 * Fullscreen is 80x25 on every adapter; the framed geometry comes back on
 * exit. A stub that only recorded the flag measured nothing of the double
 * draw an rc_sh_inval() after the latch would have cost. */
static int fs_w, fs_h;
int  os88_fullscreen(void *win, int enter)
{
    if (enter) {
        if (fullscreen) return 0;
        fs_w = cont_w; fs_h = cont_h;
        cont_w = 640; cont_h = 200;
    } else {
        if (!fullscreen) return 0;
        cont_w = fs_w; cont_h = fs_h;
    }
    fullscreen = enter;
    damage_whole = 1;
    os88_paint(win);                       /* the caller holds the lock: the
                                            * primitives assert depth 1 */
    return 0;
}
void os88_menu_set(void *win, struct os88_menuset *set) {}
void os88_about_set(void *win) {}
int  os88_snd_tone(int hz, int ticks, int prio) { return 0; }
int  os88_toast(const char *text, int ticks)
{ strncpy(last_toast, text, sizeof(last_toast) - 1); return 0; }
int  os88_cpu(void) { return OS88_CPU_8086; }
unsigned os88_mem_claim(int kb) { return 0x2000; }
unsigned os88_mem_largest_kb(void) { return 200; }

int os88_ferr(void) { return ferr; }
void os88_file_here(struct os88_place *p) { p->clus = std_clus; p->vol = std_vol; }
int os88_file_goto_q_mark(unsigned clus, int vol)
{
    if (vol != 0 || (clus != 0 && clus != 5 && clus != 9)) { ferr = OS88_FERR_IO; return -1; }
    std_clus = clus; std_vol = vol; ferr = 0;
    return 0;
}
int os88_file_find(int ordinal, struct os88_find *f)
{
    memset(f, 0, sizeof(*f));
    if (std_clus == 0 && ordinal == 0) {
        strcpy(f->name, "RUNCPM.O88"); f->type = OS88_FT_PKG; return 1;
    }
    if (std_clus == 0 && ordinal == 1) {
        strcpy(f->name, "A"); f->type = OS88_FT_DIR; f->clus = 5; return 2;
    }
    if (std_clus == 5 && ordinal == 0) {
        strcpy(f->name, ".."); f->type = OS88_FT_UP; return 1;
    }
    if (std_clus == 5 && ordinal == 1) {
        strcpy(f->name, "0"); f->type = OS88_FT_DIR; f->clus = 9; return 2;
    }
    if (std_clus == 9 && ordinal == 0) {
        strcpy(f->name, "RCPROBE.TXT"); f->type = OS88_FT_FILE; f->size_lo = 26; return 1;
    }
    ferr = OS88_FERR_NOENT;
    return -1;
}
unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    if (std_clus == 9 && strcmp(name, "RCPROBE.TXT") == 0) {
        strncpy(buf, "A0 through goto_q_mark", cap);
        ferr = 0;
        return 22;
    }
    ferr = OS88_FERR_NOENT;
    return 0;
}

void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
void os88_strcpy(char *dst, const char *src, unsigned cap)
{ strncpy(dst, src, cap - 1); dst[cap - 1] = 0; }
char *os88_utoa(unsigned v, char *dst6) { sprintf(dst6, "%u", v); return dst6; }

/* --- the program ------------------------------------------------------------ */
#include "../runcpm.c"

/* --- the assembly, modelled (apps/runcpm/rcband.inc) ------------------------ */
void rc_band(unsigned char *dst, const unsigned char *chars,
             const unsigned char *attr, int first, int n)
{
    int c;
    for (c = 0; c < n; c++) {
        dst[c] = chars[c];
        dst[RC_COLS + c] = (attr[(first + c) >> 3] >> (7 - ((first + c) & 7))) & 1;
    }
}
int rc_rowdiff(const unsigned char *a, const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return i;
    return -1;
}
int rc_rowdiffl(const unsigned char *a, const unsigned char *b, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) if (a[i] != b[i]) return i;
    return -1;
}

/* ==========================================================================
 * THE CHECKS
 * ========================================================================*/
static void *win;
static int fails;
static int vis_rows(void) { int r = cont_h / 8; return r > RC_ROWS ? RC_ROWS : r; }
static int vis_cols(void) { int c = cont_w / 8; return c > RC_COLS ? RC_COLS : c; }

static int model_ch(int r, int c) { return rc_ch[RC_CHOFF(rc_prow[r]) + c]; }
static int model_rev(int r, int c)
{
    int a = (rc_at[RC_ATOFF(rc_prow[r]) + (c >> 3)] >> (7 - (c & 7))) & 1;
    if (rc_curvis && rc_cy == r && rc_cx == c) a ^= 1;
    return a;
}
static int glass_ch(int r, int c) { return gl_ch[cont_y + r * 8][cont_x / 8 + c]; }
static int glass_rev(int r, int c) { return gl_rev[cont_y + r * 8][cont_x / 8 + c]; }
static int shadow_ch(int r, int c) { return rc_sh[RC_CHOFF(rc_shrow[r]) + c]; }
static int shadow_rev(int r, int c)
{ return (rc_sha[RC_ATOFF(rc_shrow[r]) + (c >> 3)] >> (7 - (c & 7))) & 1; }

static void dump_row(int r)
{
    int c;
    printf("  model  %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(model_ch(r, c)); printf("|\n");
    printf("  glass  %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(glass_ch(r, c)); printf("|\n");
    printf("  shadow %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(shadow_ch(r, c) ? shadow_ch(r, c) : '~'); printf("|\n");
}

/* glass == model and shadow == glass, every visible cell */
static void audit(const char *step)
{
    int r, c, bad = 0;
    for (r = 0; r < vis_rows(); r++)
        for (c = 0; c < vis_cols(); c++) {
            if (glass_ch(r, c) != model_ch(r, c) || glass_rev(r, c) != model_rev(r, c)) {
                if (!bad) printf("FAIL %s: glass != model at row %d col %d (glass '%c'/%d model '%c'/%d)\n",
                                 step, r, c, glass_ch(r, c), glass_rev(r, c), model_ch(r, c), model_rev(r, c));
                bad++;
            }
            if (shadow_ch(r, c) != glass_ch(r, c) || shadow_rev(r, c) != glass_rev(r, c)) {
                if (!bad) printf("FAIL %s: shadow != glass at row %d col %d\n", step, r, c);
                bad++;
            }
        }
    if (bad) { fails++; dump_row(rc_cy); }
    if (lock_depth != 0) { printf("FAIL %s: lock left held (%d)\n", step, lock_depth); fails++; }
}

/* the row reads exactly this (an INDEPENDENT expectation, hand-written) */
static void expect_row(const char *step, int r, const char *text)
{
    int c, bad = 0;
    if (r >= vis_rows())
        return;                     /* a model row the window does not hold */
    for (c = 0; c < vis_cols(); c++) {
        int want = c < (int)strlen(text) ? text[c] : ' ';
        if (glass_ch(r, c) != want) { bad = 1; break; }
    }
    if (bad) {
        printf("FAIL %s: row %d reads wrong\n  want       |%s|\n", step, r, text);
        dump_row(r);
        fails++;
    }
}

static void expect_cursor(const char *step, int x, int y)
{
    if (rc_cx != x || rc_cy != y) {
        printf("FAIL %s: cursor at (%d,%d), wanted (%d,%d)\n", step, rc_cx, rc_cy, x, y);
        fails++;
    }
}

/* one wake: what the machine does after every slice */
static void wake(void) { rc_wakes = 2; os88_onwake(win); }   /* 2: the RC_W1
                                  * counter line is not reprinted (it prints
                                  * every 16th wake) - the table is the
                                  * terminal's cost, not the scaffold's */
static void feed(const char *s) { rc_puts(s); }
static void expose(void)
{
    damage_whole = 1;
    lock_depth = 1;                 /* the kernel holds it for a paint */
    os88_paint(win);
    lock_depth = 0;
}

/* --- the cost table ------------------------------------------------------- */
struct cost { const char *what; int calls, cells, scrolls, blits, runs, fills;
              int run_cells, blit_cells, scroll_rows, fill_rows, fill_w; };
static struct cost costs[20];
static int ncosts;
static void cost(const char *what)
{
    struct cost *c = &costs[ncosts++];
    c->what = what; c->calls = calls(); c->cells = n_cells;
    c->scrolls = n_scroll; c->blits = n_blit; c->runs = n_run; c->fills = n_fill;
    c->run_cells = n_run_cells; c->blit_cells = n_blit_cells;
    c->scroll_rows = n_scroll_rows; c->fill_rows = n_fill_rows; c->fill_w = fill_w;
}

/* THE MODEL of what the target pays - calls and cells are exact, the ms
 * column is a MODEL, per primitive, from PERFORMANCE.md's measurements:
 *   font_run   756 us a call + ~900 us a cell           (Part 2)
 *   band       rc_band + gfx_blit1 together, MEASURED on the icount harness
 *              (tests/rcband, `make rcbandbench`, Part 4's 0.359 ms a count):
 *              1 cell 1.03 ms, 11 cells 2.78, 79 cells 14.5 - a line of
 *              860 us a call + 173 us a cell (145 of it the compose, ~31 the
 *              emit; Set 64's blit1 alone is 395 + 31/cell). The first loop
 *              measured 306 us a cell on the same harness.
 *   gfx_scroll 756 us a call + rows x 150 us (VGA, 32-byte rows measured
 *              after §5.5.1: Set 54, 19,194 us / 128 rows; Hercules 269;
 *              an 80-byte row is more, unmeasured)
 *   gfx_fill   756 us a call + rows x (177 us + 0.28 us a pixel)   (Part 2)
 * A DIR row that scrolls the whole 200-row content is therefore ~30 ms of
 * scroll on VGA, not one 756-us call: wave 2's slice pacing (lines per flush)
 * is a performance decision, and 'N lines in one slice = ONE scroll' is the
 * thing that saves seconds on a TYPE. */
#define US_RUN_CALL   756
#define US_RUN_CELL   900
#define US_BAND_CALL  860
#define US_BAND_CELL  173
#define US_SCROLL_ROW 150
#define US_FILL_ROW   177
static double model_ms(const struct cost *c)
{
    double us = 0;
    us += (double)c->runs * US_RUN_CALL + (double)c->run_cells * US_RUN_CELL;
    us += (double)c->blits * US_BAND_CALL + (double)c->blit_cells * US_BAND_CELL;
    us += (double)c->scrolls * 756 + (double)c->scroll_rows * US_SCROLL_ROW;
    us += (double)c->fills * 756 + (double)c->fill_rows * (US_FILL_ROW + 0.28 * c->fill_w);
    return us / 1000.0;
}
static void print_costs(void)
{
    int i;
    printf("\n  cost table: calls and cells are EXACT; 'model ms' prices them per\n"
           "  primitive (font_run 756+900/cell, Part 2; band 860+173/cell MEASURED on\n"
           "  tests/rcband; scroll 756+150/row VGA; fill 756+177/row+px)\n");
    printf("  %-40s %5s %5s  %8s  %s\n", "operation", "calls", "cells", "model ms", "scroll/blit1/run/fill (rows moved/filled)");
    for (i = 0; i < ncosts; i++) {
        struct cost *c = &costs[i];
        printf("  %-40s %5d %5d  %8.1f  %d/%d/%d/%d (%d/%d)\n", c->what, c->calls, c->cells,
               model_ms(c), c->scrolls, c->blits, c->runs, c->fills,
               c->scroll_rows, c->fill_rows);
    }
}

/* ==========================================================================
 * THE SCRIPTS
 * ========================================================================*/
static void run_all(int cols_geom, int rows_geom, int table)
{
    static char line[128];
    int i;

    cont_w = cols_geom * 8; cont_h = rows_geom * 8;
    gclear(); callreset(); ncosts = 0; wakes_posted = 0;
    std_clus = 0; std_vol = 0; ferr = 0;
    rc_probe_done = 0; rc_wakes = 0;

    /* launch: os88_main builds the model (banner), the loader shows the
     * window and paints it whole */
    win = os88_main();
    if (!win) { printf("FAIL: os88_main returned 0 (%s)\n", last_toast); fails++; return; }
    if (rc_all_blank) { /* the banner wrote something */ }
    expose();
    audit("expose after launch");
    expect_row("banner 1", 0, "  CP/M Emulator v6.9 by Marcelo Dantas");
    expect_row("banner 2", 1, "      Built Jul 21 2026 - 20:43:19");   /* the
                                  * pinned upstream commit's date (SPEC.md 71) */
    expect_row("banner 3", 2, "----------------------------------------");
    expect_row("banner 4", 3, "CPU is 8086 native");
    expect_row("banner 5", 4, "BIOS at 0xfe00 - BDOS at 0xec00");
    expect_row("banner 6", 5, "BIOS/BDOS using interrupt handoff method");
    expect_row("banner 7", 6, "CCP CCP-DR.60K at 0xe400");
    expect_cursor("after banner", 0, 7);
    /* a whole repaint is ONE fill of the content and then only the rows with
     * something on them, each to its last non-blank cell: the banner's 7 and
     * the cursor's row (one cell) */
    if (n_fill != 1 || n_blit != 8 || calls() != 9) { printf("FAIL: the banner expose cost %d fills / %d bands / %d calls, wanted 1 / 8 / 9\n", n_fill, n_blit, calls()); fails++; }
    if (table) cost("expose: full repaint, banner (fill + 8 rows)");
    if (wakes_posted == 0) { printf("FAIL: the paint did not kick\n"); fails++; }

    /* the first wake: the probe (fake tree) and the counter line */
    callreset();
    wake();
    audit("first wake");
    expect_row("probe row", 7, "A\\0 probe: 22 bytes: A0 through goto_q_mark");
    if (std_clus != 0) { printf("FAIL: the probe did not come home (clus %u)\n", std_clus); fails++; }
    if (wakes_posted < 2) { printf("FAIL: the wake did not re-post\n"); fails++; }

    /* echo one character - the keystroke path: os88_onkey pushes, the
     * slice pops and echoes, the flush draws */
    feed("\033[12;1H");                 /* park the cursor on an empty row */
    wake();
    callreset();
    os88_onkey('a', 0x1E, win);
    wake();
    audit("echo a");
    expect_row("echo a", 11, "a");
    expect_cursor("echo a", 1, 11);
    if (calls() != 1 || n_cells != 2) { printf("FAIL: echo one character cost %d calls / %d cells, wanted 1 / 2\n", calls(), n_cells); fails++; }
    if (table) cost("echo one character (cell + cursor)");

    /* cursor-only move to another row: the old cursor cell and the new */
    callreset();
    feed("\033[14;10H");
    wake();
    audit("cursor move");
    expect_cursor("cursor move", 9, 13);
    if (calls() != 2 || n_cells != 2 || n_blit != 2) { printf("FAIL: cursor move to another row cost %d calls / %d cells / %d bands, wanted 2 / 2 / 2\n", calls(), n_cells, n_blit); fails++; }
    if (table) cost("cursor-only move to another row");

    /* cursor-only move on the SAME row, far: cols 9 -> 19 is a span of 11,
     * past the measured crossover (SPEC.md 71.2: a band is 860 us + 173 a
     * cell, so two 1-cell bands at 2.07 ms beat a span of 8 or more), so it
     * is two 1-cell bands */
    callreset();
    feed("\033[14;20H");
    wake();
    audit("cursor move same row");
    if (calls() != 2 || n_cells != 2) { printf("FAIL: cursor move along the row by 10 cost %d calls / %d cells, wanted 2 / 2 (split)\n", calls(), n_cells); fails++; }
    if (table) cost("cursor-only move on the same row, 10 apart");

    /* ...and near: cols 19 -> 22 is a span of 4, one band */
    callreset();
    feed("\033[14;23H");
    wake();
    audit("cursor move same row near");
    if (calls() != 1 || n_cells != 4) { printf("FAIL: cursor move along the row by 3 cost %d calls / %d cells, wanted 1 / 4 (the span)\n", calls(), n_cells); fails++; }
    if (table) cost("cursor-only move on the same row, 3 apart");

    /* a DIR listing that reaches the bottom and scrolls: 25 rows fed one at
     * a time, each row a wake, so the last ones are the scrolled case */
    feed("\r");
    for (i = 0; i < 20; i++) {
        sprintf(line, "A: FILE%02d   COM : FILE%02d   TXT : FILE%02d   BAS : FILE%02d   ASM\r\n", i, i, i, i);
        callreset();
        feed(line);
        wake();
        audit("dir row");
        if (i == 19) {
            /* the scroll, ONE fill of the vacated 8-px strip, the text row's
             * band and the cursor's 1-cell band on the vacated row (a
             * window of fewer rows shows neither the text row nor the
             * cursor's: model rows 23 and 24 are below a 17-row window) */
            if (vis_rows() == 25 && (n_scroll != 1 || n_fill != 1 || n_blit != 2 || calls() != 4)) { printf("FAIL: a scrolled DIR row cost %d scroll / %d fill / %d bands / %d calls, wanted 1 / 1 / 2 / 4\n", n_scroll, n_fill, n_blit, calls()); fails++; }
            if (table) cost("one DIR row at the bottom (scroll+fill+2 rows)");
        }
    }
    expect_cursor("dir bottom", 0, 24);
    expect_row("dir last", 23, "A: FILE19   COM : FILE19   TXT : FILE19   BAS : FILE19   ASM");
    expect_row("dir first", 4, "A: FILE00   COM : FILE00   TXT : FILE00   BAS : FILE00   ASM");

    /* three lines in one slice: ONE scroll of three */
    callreset();
    feed("line one\r\nline two\r\nline three\r\n");
    wake();
    audit("three lines");
    expect_row("three lines", 23, "line three");
    expect_row("three lines b", 21, "line one");
    if (table) cost("three lines in one slice (one scroll of 3)");

    /* the pending wrap: 80 characters then more */
    callreset();
    for (i = 0; i < 80; i++) rc_putc('0' + (i % 10));
    wake();
    audit("80 chars");
    expect_cursor("80 chars pending", 80, 24);
    feed("X");
    wake();
    audit("wrap");
    expect_row("wrap", 23, "01234567890123456789012345678901234567890123456789012345678901234567890123456789");
    expect_row("wrap x", 24, "X");
    expect_cursor("wrap", 1, 24);

    /* backspace and CR at the pending wrap */
    feed("\r\n");
    for (i = 0; i < 80; i++) rc_putc('a');
    rc_putc(8); rc_putc('b');           /* BS from the pending wrap: col 78 */
    wake();
    audit("bs at wrap");
    if (vis_rows() == 25 && (glass_ch(24, 78) != 'b' || (vis_cols() == 80 && glass_ch(24, 79) != 'a'))) { printf("FAIL: BS at the pending wrap\n"); fails++; }
    rc_putc(13);
    expect_cursor("cr at wrap", 0, 24);

    /* ESC[2J: one fill and the cursor */
    callreset();
    feed("\033[2J\033[H");
    wake();
    audit("clear");
    expect_cursor("clear", 0, 0);
    if (table) cost("ESC[2J ESC[H (fill + cursor)");
    for (i = 0; i < vis_rows(); i++) expect_row("clear rows", i, "");

    /* ...and a cursor-only move on the still-empty screen must NOT fill it
     * again (rc_all_blank is consumed by the flush that acted on it): the
     * shape of CLS then a cursor position then a wait for a key */
    callreset();
    feed("\033[3;5H");
    wake();
    audit("move after clear");
    expect_cursor("move after clear", 4, 2);
    if (n_fill != 0 || calls() > 2) { printf("FAIL: cursor move after ESC[2J cost %d fills / %d calls\n", n_fill, calls()); fails++; }
    if (table) cost("cursor-only move after ESC[2J (no refill)");
    callreset();
    feed("\033[?25l\033[?25h\033[10;20H");
    wake();
    audit("hide/show/move after clear");
    if (n_fill != 0) { printf("FAIL: ?25l/?25h/move after ESC[2J refilled (%d)\n", n_fill); fails++; }
    feed("\033[H");
    wake();

    /* TE-style full-screen redraw: 24 text rows and a reverse status line */
    callreset();
    feed("\033[2J\033[H");
    for (i = 0; i < 24; i++) {
        sprintf(line, "\033[%d;1HRow %02d of a document being edited in TE, seventy-nine columns of text.......", i + 1, i);
        feed(line);
    }
    feed("\033[25;1H\033[7m TE.COM  A:DOC.TXT  Line 1  Col 1  ^J for help                                 \033[0m");   /* 80 cells: the pending wrap, no scroll */
    feed("\033[1;1H");
    wake();
    audit("te redraw");
    expect_row("te row 0", 0, "Row 00 of a document being edited in TE, seventy-nine columns of text.......");
    if (glass_rev(24, 1) != 1 && vis_rows() == 25) { printf("FAIL: status line not reverse\n"); fails++; }
    if (table) cost("TE-style 25-row full-screen redraw");

    /* the same screen drawn again: nothing */
    callreset();
    for (i = 0; i < 24; i++) {
        sprintf(line, "\033[%d;1HRow %02d of a document being edited in TE, seventy-nine columns of text.......", i + 1, i);
        feed(line);
    }
    feed("\033[1;1H");
    wake();
    audit("te same");
    if (calls() != 0) { printf("FAIL: redrawing an identical screen cost %d calls\n", calls()); fails++; }
    if (table) cost("the identical screen written again");

    /* insert and delete line at the cursor (ESC[L / ESC[M) */
    callreset();
    feed("\033[5;1H\033[L");
    wake();
    audit("insert line");
    expect_row("insert line", 4, "");
    expect_row("insert line b", 5, "Row 04 of a document being edited in TE, seventy-nine columns of text.......");
    if (vis_rows() == 25)
        expect_row("insert line c", 23, "Row 22 of a document being edited in TE, seventy-nine columns of text.......");
    feed("\033[M");
    wake();
    audit("delete line");
    expect_row("delete line", 4, "Row 04 of a document being edited in TE, seventy-nine columns of text.......");
    if (table) cost("ESC[L then ESC[M (two wakes)");

    /* erase to end of line, hide the cursor, show it */
    callreset();
    feed("\033[3;10H\033[K");
    wake();
    audit("erase eol");
    expect_row("erase eol", 2, "Row 02 of");
    feed("\033[?25l");
    wake();
    audit("cursor hidden");
    if (glass_rev(2, 9) != 0) { printf("FAIL: cursor still shown after ?25l\n"); fails++; }
    if (model_rev(2, 9) != 0) { printf("FAIL: model cursor still on after ?25l\n"); fails++; }
    feed("\033[?25h");
    wake();
    audit("cursor shown");
    if (glass_rev(2, 9) != 1) { printf("FAIL: cursor not shown after ?25h\n"); fails++; }
    if (table) cost("erase to EOL, hide cursor, show cursor (3 wakes)");

    /* an unknown escape is swallowed whole */
    feed("\033[31;1m\033=\033[6nplain");
    wake();
    audit("unknown esc");
    expect_row("unknown esc", 2, "Row 02 ofplain");

    /* a partial expose: only the damaged rows are drawn */
    callreset();
    damage_whole = 0;
    damage_rect.x1 = cont_x + 16; damage_rect.x2 = cont_x + 100;
    damage_rect.y1 = cont_y + 40; damage_rect.y2 = cont_y + 60;
    /* somebody else drew there */
    { int r, c; for (r = 5; r <= 7; r++) for (c = 2; c <= 12; c++) { gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; gl_rev[cont_y + r*8][cont_x/8 + c] = 1; } }
    lock_depth = 1; os88_paint(win); lock_depth = 0;
    damage_whole = 1;
    audit("partial expose");
    if (table) cost("partial expose of 3 rows x 11 cells");

    /* a full expose of the TE screen */
    callreset();
    { int r, c; for (r = 0; r < 25; r++) for (c = 0; c < 80; c++) { gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; } }
    expose();
    audit("full expose");
    if (table) cost("full expose repaint of the TE screen");

    /* Alt+F: the latch paints us whole INSIDE the slot (the stub does what
     * wm_fullscreen does), and the flush that follows must find nothing to
     * do - the shadow describes the new glass. Both directions. */
    callreset();
    lock_depth = 1;                        /* W_ONKEY is dispatched under it */
    os88_onkey(0, 0x21, win);
    lock_depth = 0;
    if (!fullscreen) { printf("FAIL: Alt+F did not enter fullscreen\n"); fails++; }
    audit("alt-f enter");
    if (table) cost("Alt+F enter: the latch's own whole paint (80x25)");
    callreset();
    wake();
    audit("after alt-f enter");
    if (calls() != 0) { printf("FAIL: the flush after Alt+F enter cost %d calls (a second draw of the same screen)\n", calls()); fails++; }
    if (table) cost("the flush after Alt+F enter");
    callreset();
    lock_depth = 1;
    os88_onkey(0, 0x21, win);
    lock_depth = 0;
    if (fullscreen) { printf("FAIL: Alt+F did not leave fullscreen\n"); fails++; }
    audit("alt-f exit");
    callreset();
    wake();
    audit("after alt-f exit");
    if (calls() != 0) { printf("FAIL: the flush after Alt+F exit cost %d calls\n", calls()); fails++; }
    if (table) cost("the flush after Alt+F exit");

    /* the blit1-refused fallback: font_run per uniform span */
    blit1_refuses = 1;
    callreset();
    feed("\033[10;1H\033[Kfallback \033[7mreverse\033[0m plain again");
    wake();
    audit("blit1 refused");
    if (table) cost("a mixed row with blit1 refused (font_run runs)");
    blit1_refuses = 0;

    /* the scroll-refused fallback: rows redrawn */
    scroll_refuses = 1;
    callreset();
    feed("\033[25;1H\r\nscrolled without gfx_scroll\r\n");
    wake();
    audit("scroll refused");
    if (table) cost("two scrolled lines with gfx_scroll refused");
    scroll_refuses = 0;

    /* the bell rings outside the lock */
    rc_bells = 0;
    feed("\007\007");
    wake();
    if (rc_bells != 0) { printf("FAIL: bells not serviced\n"); fails++; }
}

int main(void)
{
    printf("rcuitest: RUNCPM terminal against a model of the glass\n");
    run_all(80, 25, 1);                 /* fullscreen / Hercules framed */
    print_costs();
    run_all(79, 25, 0);                 /* VGA framed (632 px of content) */
    run_all(78, 17, 0);                 /* CGA framed */
    if (fails) {
        printf("rcuitest: %d FAILURE(S)\n", fails);
        return 1;
    }
    printf("rcuitest: all checks passed on 80x25, 79x25 and 78x17\n");
    return 0;
}
