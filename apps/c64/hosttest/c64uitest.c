/* ============================================================================
 * os8088 - apps/c64/hosttest/c64uitest.c    C64's screen model, on the host
 *
 * Part of C64 (C64-SPEC §14.5), a reimplementation of VICE 3.10's x64.
 * VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later - see
 * apps/c64/COPYING.
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and apps/c64/build.sh compiles and runs it BEFORE the target build; a
 * failure stops the build.
 *
 * ----------------------------------------------------------------------------
 * WHY
 * ----------------------------------------------------------------------------
 * c64scr.c is a damage model: dirty pages and a write window narrow a
 * recompose to a cell span, a pixel compare against an 8,000-byte shadow
 * narrows the DRAW to a smaller span, and a signature test turns a scrolled
 * frame into one gfx_scroll. Every one of those decisions is a chance to
 * leave a stale cell, to draw a cell twice, or to trust a shadow that has
 * stopped describing the glass - and PERFORMANCE.md is explicit that none of
 * those three shows in an emulator's screendump.
 *
 * So this stubs the API with a PIXEL MODEL OF THE GLASS - gfx_blit1 writes
 * real pixels, gfx_fill fills, gfx_scroll MOVES THE PIXELS AND FILLS THE
 * VACATED ROWS WITH GARBAGE (legal: SPEC.md 5.5, and it is what catches a
 * flush that trusts the shadow for a row the scroll vacated) - includes the
 * whole program against it, drives it, and after every step asserts
 *
 *     the glass shows what the shadow says it shows,
 *
 * pixel for pixel over the whole 320x200 screen. Then it prints THE COST
 * TABLE IN MILLISECONDS (C64-SPEC §9.7), priced per primitive from
 * PERFORMANCE.md's 756 us a drawing call and this package's own
 * tests/c64band figures, and dumps the machine and the composed frame for
 * tools/c64ref.py - the INDEPENDENT pixel-level compositor - to check bit for
 * bit.
 *
 * ----------------------------------------------------------------------------
 * WHAT IT CANNOT SEE, STATED
 * ----------------------------------------------------------------------------
 * `int` is four bytes here and two there - so a SIXTEEN-BIT WRAP cannot be
 * reproduced by running the code here at all: `size + 1023` overflowing on
 * the target is arithmetic this host simply does not do, and the loader's
 * ceiling is written shift-then-round so that it cannot wrap by construction
 * rather than because a row below proved it. What the rows below DO gate is
 * every consequence that survives the width difference: a claim smaller than
 * the read into it, a leaked claim, and both ends of the $FFFF test. And the assembly half of the
 * package (c64band.inc, c64mem.inc, c64cpu.inc) cannot run on this host, so
 * the routines below are HOST TRANSCRIPTIONS of what those files do. That
 * makes tools/c64ref.py a check on the ALGORITHM - the mode decode, the
 * character-generator addressing, the luminance rule - and not on the 8086
 * encoding of it. The encoding is gated elsewhere and deliberately:
 * hosttest/c64memtest.sh runs the shipping c64mem.inc and c64band.inc on a
 * real x86 under SS != DS, tests/c64band puts the shipping composer's output
 * on the glass beside a known-good line, and the QEMU screendumps are of the
 * shipping composer.
 *
 * ----------------------------------------------------------------------------
 * HOW IT IS BUILT
 * ----------------------------------------------------------------------------
 *   cc -O1 -w -I apps/c64/hosttest -I apps/c64 \
 *      -o build/c64uitest apps/c64/hosttest/c64uitest.c && build/c64uitest
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

#include "os88.h"                       /* the STUB SDK, ahead of apps/cc on
                                         * the include path */

/* ==========================================================================
 * THE GLASS - one byte a pixel, over the whole window's content box
 * ========================================================================*/
/* GW/GH ARE THE CONTENT BOX AND NOT THE FRAME, which is the distinction the
 * first draft of this file got wrong: it set GW to C64_W_W and the model was
 * then TWO PIXELS WIDER THAN THE MACHINE, because kernel/wm.inc:5442's
 * wm_geom answers `W_W - 2` (the window's two side borders). The 42-cell
 * status row's last cell lived in those two pixels, so the pause lamp `P` was
 * never on the glass in a windowed C64 and nothing here could see it.
 * main() asserts the relation against the package's own constant. */
#define GX 8                            /* the content origin: wm_snap puts */
#define GY 40                           /* x on a cell boundary */
#define GW 336                          /* == C64_W_W - 2, checked in main */
#define GH 226
#define GLW 700
#define GLH 500

static unsigned char glass[GLH][GLW];   /* 0 dark, 1 lit, 2 grey, 0xAA garbage */
static int pen_disabled, cur_colour = 15;

static int n_blit, n_fill, n_scroll, n_frame, n_run, n_str, n_cpu;
static long n_blit_bytes;               /* a blit is a floor plus its pixels */
static int n_band_calls, n_band_cells, n_span_calls, n_sig_calls;
static int scroll_refuse;               /* make gfx_scroll answer -1 */
static int blit_refuse;                 /* ...and gfx_blit1, which os88.h:446
                                         * says a kern_small kernel does for
                                         * every band: the package must not
                                         * record a shadow for it */

static int fails;

static void fail(const char *what)
{
    printf("c64uitest: FAIL - %s\n", what);
    fails++;
}

/* ==========================================================================
 * THE GFX LOCK, MODELLED - because the lock is a CONTRACT and not a comment
 *
 * gfx_lock is a NON-RECURSIVE spin released only by the UI task
 * (kernel/ui.inc:1997-2003), and every window callback - W_PAINT, W_ONCLICK,
 * AM_ONCMD through the bar - is dispatched WITH IT ALREADY HELD
 * (apps/cc/crt0.asm:478: "you must never take the lock"). A handler that
 * takes it hangs the machine dead: no beep, no watchdog, no recovery, and
 * nothing on any screendump. The first draft of this harness modelled
 * gfx_lock and gfx_unlock as empty functions and os88_task_spawn as
 * `return 1`, and passed a File > Exit emulator that did both wrong.
 *
 * So: the stubs count DEPTH, every drawing stub asserts it is at exactly 1,
 * os88_task_spawn asserts it is HELD and answers the real 0 / -1, and every
 * step ends at 0. apps/runcpm/hosttest/rcuitest.c:90-111 is the shape.
 * ========================================================================*/
static int lock_depth;
static int clip_armed;                  /* the package called wm_clip_set */
static int clip_refuses;                /* ...and the kernel can refuse it */
static int cov_x1 = 1, cov_y1, cov_x2, cov_y2;   /* empty: x1 > x2 */
static int cov_hits;

static int alive_calls, alive_after_destroy, win_destroyed;
static jmp_buf alive_out;

static void need_lock(const char *who)
{
    if (lock_depth != 1) {
        printf("c64uitest: HARNESS - %s called with lock depth %d\n",
               who, lock_depth);
        exit(1);
    }
}

/* --- the drawing stubs --------------------------------------------------- */
void os88_gfx_lock(void)
{
    if (lock_depth != 0) {
        printf("c64uitest: HARNESS - gfx_lock taken at depth %d "
               "(it is a NON-RECURSIVE spin: this is a dead machine)\n",
               lock_depth);
        exit(1);
    }
    lock_depth++;
}
void os88_gfx_unlock(void)
{
    if (lock_depth != 1) {
        printf("c64uitest: HARNESS - gfx_unlock at depth %d\n", lock_depth);
        exit(1);
    }
    lock_depth--;
    clip_armed = 0;                     /* the clip dies with the lock (11.3) */
}
void os88_set_color(int c) { cur_colour = c; }
void os88_gfx_pen(int d) { pen_disabled = d; }

/* ==========================================================================
 * THE CLIP, MODELLED - because "arm it" is a contract and not a comment
 *
 * The kernel arms a clip region for W_PAINT and for NOTHING ELSE (SPEC.md
 * 11.3): a callback that draws from W_ONCLICK, W_ONCMD or the name menu's
 * About item and does not call os88_wm_clip_set first paints straight over
 * whatever is covering the window. The first draft of this file answered
 * os88_wm_clip_set with an unconditional 0 and let plot() write anywhere, so
 * the one thing the call exists for could not be checked - and the About
 * panel opened from a menu with no clip at all.
 *
 * So: a region is armed and plot() ENFORCES it. `clip_armed` is what the
 * package has asked for; `cover` is a rectangle of the screen that belongs to
 * some other window, and a pixel written there while no clip is armed is a
 * failure with the coordinates in it.
 * ========================================================================*/
static void plot(int x, int y, int v)
{
    if (x < 0 || y < 0 || x >= GLW || y >= GLH)
        return;
    if (!clip_armed && x >= cov_x1 && x <= cov_x2
                    && y >= cov_y1 && y <= cov_y2) {
        if (cov_hits < 3)
            printf("c64uitest:   a pixel at (%d,%d) landed on the window "
                   "COVERING us, with no clip armed (SPEC.md 11.3)\n", x, y);
        cov_hits++;
        return;                         /* the covering window's pixel stands */
    }
    glass[y][x] = (unsigned char)v;
}

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int x, y, v;
    need_lock("gfx_fill");
    v = pen_disabled ? 2 : (cur_colour == OS88_BLACK ? 0 : 1);
    for (y = y1; y <= y2; y++)
        for (x = x1; x <= x2; x++)
            plot(x, y, v);
    n_fill++;
}

void os88_gfx_frame(int x1, int y1, int x2, int y2)
{
    int x, y;
    need_lock("gfx_frame");
    for (x = x1; x <= x2; x++) { plot(x, y1, 1); plot(x, y2, 1); }
    for (y = y1; y <= y2; y++) { plot(x1, y, 1); plot(x2, y, 1); }
    n_frame++;
}

int os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *b = (const unsigned char *)bits;
    int r, c;
    need_lock("gfx_blit1");
    if (blit_refuse)
        return -1;                      /* NOTHING DRAWN - os88.h:446 */
    if ((x & 7) || (w & 7) || rows < 1 || rows > 255)
        return -1;
    for (r = 0; r < rows; r++)
        for (c = 0; c < w; c++)
            plot(x + c, y + r, (b[r * stride + (c >> 3)] >> (7 - (c & 7))) & 1);
    n_blit++;
    n_blit_bytes += (long)(w >> 3) * rows;
    return 0;
}

/* gfx_scroll MOVES the pixels and leaves the vacated rows GARBAGE. SPEC.md
 * 5.5 says the vacated rows are the caller's to repaint and says nothing
 * about what is in them, so this fills them with a value no composer can
 * produce: a flush that trusts a stale shadow for one of those rows fails the
 * audit here rather than on someone's screen.
 *
 * AND POSITIVE dy MOVES THE CONTENT UP. SPEC.md 5.5, os88.h:437,
 * apps/cc/os88thunk.asm:267 and kernel/vga12.inc:3927 ("positive = content
 * up") all say so, and apps/runcpm/rcterm.c:631 consumes it that way. The
 * first draft of this stub implemented ONLY the `dy < 0` branch and
 * implemented it as content-UP, with the sentinel in the BOTTOM rows - the
 * exact opposite of the machine on both counts - and `dy > 0` fell through
 * the `if` and answered 0 with the glass untouched. That is why c64_flush
 * shipped asking for `-k * 8`: the package and the model were wrong in the
 * same direction, so the k = 9 step passed. LESSONS.md 7's first trap
 * verbatim - "verify the stubs model what the machine does".
 *
 * The refusals §5.5 names are modelled too, so nothing gates the package
 * against asking for one: dy == 0, |dy| >= the rect's height, an inverted or
 * empty rect, and x edges off a cell boundary. */
int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int x, y;
    need_lock("gfx_scroll");
    n_scroll++;                         /* count the CALL, not the success:
                                         * "one gfx_scroll per flush" is a
                                         * claim about what the flush asks
                                         * for (9.4) */
    if (scroll_refuse)
        return -1;
    if ((x1 & 7) || ((x2 + 1) & 7))
        return -1;
    if (x2 < x1 || y2 < y1)
        return -1;
    if (dy == 0 || dy >= y2 - y1 + 1 || -dy >= y2 - y1 + 1)
        return -1;
    if (dy > 0) {                       /* content UP: row y takes row y + dy,
                                         * and the VACATED rows are at the
                                         * BOTTOM */
        for (y = y1; y <= y2 - dy; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = glass[y + dy][x];
        for (y = y2 - dy + 1; y <= y2; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = 0xAA;
    } else {                            /* ...and content DOWN, copied from
                                         * the bottom up so it does not smear,
                                         * vacating the TOP */
        for (y = y2; y >= y1 - dy; y--)
            for (x = x1; x <= x2; x++)
                glass[y][x] = glass[y + dy][x];
        for (y = y1; y <= y1 - dy - 1; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = 0xAA;
    }
    return 0;
}

/* THE GLYPH CELLS ARE COUNTED, and the cost table would be a lie without
 * them: PERFORMANCE.md prices a cell at ~900 us and text is the most
 * expensive thing this package draws. The first version of cost_row summed
 * gfx_fill/blit1/scroll/frame and the three C64BENCH_* composer figures and
 * never touched n_run/n_str at all, so ovl_about_show's 160 cells in 9
 * font_str calls - ~151 ms - were reported as 0.0 ms, and c64_flush's
 * `C64.ROM is not on this disk` screen as 0.756.
 *
 * AND THEY PUT PIXELS ON THE GLASS. Counting alone made three surfaces
 * invisible to every check in this file - the ROM-less notice (9.5's own
 * refusal screen), the status row and the About panel are ALL text - so
 * "was it repainted", "did the expose restore it" and "did anything draw
 * outside the clip" could not be asked about any of them. The face here is
 * not the kernel's 8x8 (that is a kernel gate, and this file says in its
 * header that it models the algorithm and not the encoding): it is a
 * DETERMINISTIC cell pattern per character, which is all a coverage,
 * restoration or clip question needs. A space is blank, so a run of spaces
 * still reads as an erase. */
static long n_run_cells, n_str_cells;

static void font_cell(int x, int y, int ch, int ink, int paper, int opaque)
{
    int r, c, on;
    unsigned g;

    for (r = 0; r < 8; r++) {
        g = (ch == ' ' || ch == 0) ? 0u
          : (unsigned)((ch * 37 + r * 173 + (ch >> 3)) & 0xFF);
        for (c = 0; c < 8; c++) {
            on = (int)((g >> (7 - c)) & 1u);
            if (on)
                plot(x + c, y + r, ink);
            else if (opaque)
                plot(x + c, y + r, paper);
        }
    }
}

static int col_v(int colour)
{
    if (pen_disabled)
        return 2;
    return colour == OS88_BLACK ? 0 : 1;
}

void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int i;
    need_lock("font_run");
    n_run++;
    n_run_cells += (long)strlen(s);
    for (i = 0; s[i]; i++)
        font_cell(x + i * 8, y, (unsigned char)s[i],
                  col_v(ink), col_v(paper), 1);
}
void os88_font_str(int x, int y, const char *s)
{
    int i;
    need_lock("font_str");
    n_str++;
    n_str_cells += (long)strlen(s);
    for (i = 0; s[i]; i++)
        font_cell(x + i * 8, y, (unsigned char)s[i], col_v(cur_colour), 0, 0);
}

/* --- the window stubs ---------------------------------------------------- */
static int dmg_whole = 1;
static struct os88_rect dmg;
static void *the_win = (void *)0x1234;

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    return the_win;
}
void os88_wm_content(void *win, struct os88_pt *o) { o->x = GX; o->y = GY; }
/* the LIVE content box: wm_fit clamps it on a 200-line desktop, and a
 * WF_FULL window is the whole glass (9.8) */
static int geom_w = GW;
static int geom_h = GH;
int  os88_wm_geom(void *win, struct os88_size *s)
{ s->w = geom_w; s->h = geom_h; return 0; }
int  os88_wm_damage(void *win, struct os88_rect *r) { *r = dmg; return dmg_whole; }
/* os88_wm_clip_set - arms the damage clip for the REST OF THIS LOCK HOLD (it
 * dies at the kernel's own gfx_unlock, which is why nothing ever undoes it),
 * and answers -1 when nothing of the window is on the glass at all - the case
 * every caller must have a second path for. */
int os88_wm_clip_set(void *win)
{
    need_lock("wm_clip_set");
    if (clip_refuses)
        return -1;
    clip_armed = 1;
    return 0;
}
void os88_wm_snap(void *win, int on) { }
void os88_wm_ownbg(void *win, int on) { }
void os88_wm_onwake(void *win) { }
int  os88_wm_wake(void *win) { return 0; }
void os88_wm_destroy(void *win)
{
    need_lock("wm_destroy");            /* without the lock it does not take:
                                         * the window hides and the dock tile
                                         * stays (LESSONS.md 6) */
    win_destroyed = 1;
}
/* OSAPI_FULLSCREEN REPAINTS YOU WHOLE, SYNCHRONOUSLY, IN BOTH DIRECTIONS
 * (os88.h:604; kernel/wm.inc:4147 - wm_fullscreen calls wm_raise with AL = 1,
 * so W_PAINT runs NESTED inside this call). Modelling it as `return 0` is
 * what let a c64_sh_inval() sit on the success arm of Preferences >
 * Fullscreen, throwing away a shadow the kernel had just made true and
 * drawing the identical 25 bands again on the next wake. */
static int fs_on;
int  os88_fullscreen(void *win, int enter)
{
    fs_on = enter;
    geom_w = enter ? 640 : GW;
    geom_h = enter ? 440 : GH;
    dmg_whole = 1;
    clip_armed = 1;                     /* wm_raise arms it for the nested
                                         * W_PAINT, as the kernel does */
    os88_paint(win);                    /* nested, under the caller's lock */
    return 0;
}
void os88_menu_set(void *win, struct os88_menuset *set) { }
void os88_about_set(void *win) { }

/* os88_task_spawn's REAL contract (os88.h:615-620): call it from a callback
 * with the gfx lock HELD - "that is required, not allowed" - and it answers 0,
 * or -1 REFUSED because the 12-slot task table is full, which the SDK calls
 * NORMAL and transient. `spawn_refuses` is the negative control: a caller that
 * treats -1 as success, or 0 as failure, has to fail here. */
static int spawn_refuses, spawned;
int  os88_task_spawn(void *win)
{
    need_lock("task_spawn");
    if (spawn_refuses)
        return -1;
    spawned++;
    return 0;
}
/* ...and the sleep counts, so a worker that never calls task_alive is a
 * FAILURE and not a hung build: the first draft of the C64 worker parked in
 * exactly this loop for ever. */
static int sleep_calls;
void os88_task_sleep(int ticks)
{
    if (lock_depth != 0) {
        printf("c64uitest: HARNESS - task_sleep called with the gfx lock "
               "HELD (depth %d)\n", lock_depth);
        exit(1);
    }
    if (++sleep_calls > 64 && win_destroyed)
        longjmp(alive_out, 2);          /* the worker is going round for ever
                                         * without ever calling task_alive */
}

/* os88_task_alive - THE CALL THAT ACTUALLY CLOSES A PACKAGE (os88.h:622): the
 * kernel tears the instance down INSIDE it, so it never returns once the
 * window is gone, and it must be called with the gfx lock NOT held ("not
 * reentrant: calling this while holding it deadlocks the machine"). The first
 * draft of the C64 worker never called it at all - File > Exit emulator
 * destroyed the window and then parked in a bare sleep loop, leaving a dead
 * dock tile, the task slot and both claims for the rest of the session - and
 * nothing here could see it, because nothing here ran the worker. So: it
 * counts, it refuses the lock, and once the window has been destroyed it
 * MODELS not returning, with a longjmp out of the worker. */
void os88_task_alive(void *win)
{
    if (lock_depth != 0) {
        printf("c64uitest: HARNESS - task_alive called with the gfx lock "
               "HELD (depth %d): os88.h:622 - this deadlocks the machine\n",
               lock_depth);
        exit(1);
    }
    alive_calls++;
    if (win_destroyed) {
        alive_after_destroy++;
        longjmp(alive_out, 1);          /* it never returns (os88.h:624) */
    }
}
static char last_toast[64];
int  os88_toast(const char *t, int ticks)
{
    strncpy(last_toast, t, sizeof(last_toast) - 1);
    return 0;
}
int  os88_cpu(void) { return OS88_CPU_8086; }
/* a VGA desktop: 640x480 with the dock at 440, so the window gets its whole
 * 245 and GH = 226 of content. The CGA case - a 200-line desktop that CANNOT
 * give C64_CONT_H - is driven as its own step below. */
static int vid_h = 480, vid_dock = 440, vid_kind = OS88_VID_VGA;
void os88_video(struct os88_video *v)
{
    v->w = 640; v->h = vid_h; v->dock_top = vid_dock;
    v->kind = vid_kind; v->bpp = (vid_kind == OS88_VID_VGA) ? 4 : 1;
}
static unsigned the_ticks = 100;
unsigned os88_ticks(void) { return the_ticks; }
int  os88_file_dlg(int mode, void *win, const char *d) { return 0; }

/* --- the claims and the files: CAPACITIES, and one file is not another ----
 *
 * The first draft answered os88_mem_claim() with two fixed segment numbers
 * for the only two sizes the program asks for at launch, and answered EVERY
 * os88_file_read_seg() by opening C64.ROM whatever name it was handed. Smart
 * attach could not be modelled at all: a .PRG asks for a claim of
 * ceil(size / 1KB), reads the file into it and moves it into the C64's RAM,
 * and with fixed segments and one file that whole path was untestable - which
 * is why TWO arithmetic defects in it survived the wave. `size_lo + 1023` is
 * 16-bit on the target and wraps above 64,512 bytes, and the end test
 * refused a PRG whose last byte is exactly $FFFF.
 *
 * So: claims have real CAPACITIES and distinct segments, freeing is tracked
 * (a leaked scratch claim is a failure), and a read of more bytes than the
 * claim can hold is a failure HERE rather than 64KB of kernel overwritten on
 * a real machine. Files are served by NAME.
 * ----------------------------------------------------------------------- */
#define SEG_RAM 0x1000
#define SEG_ROM 0x2000
#define CLAIM_MAX 4
static unsigned char ram[65536];
static unsigned char rom[20480];
static unsigned char scratch[CLAIM_MAX][65536];
static struct { unsigned seg; long cap; int live; } claim[CLAIM_MAX];
static int ram_live, rom_live, claims_live;
static int rom_present = 1;

static unsigned char *segbase(unsigned seg)
{
    int i;
    if (seg == SEG_RAM) return ram;
    if (seg == SEG_ROM) return rom;
    for (i = 0; i < CLAIM_MAX; i++)
        if (claim[i].live && claim[i].seg == seg)
            return scratch[i];
    return 0;
}

static long segcap(unsigned seg)
{
    int i;
    if (seg == SEG_RAM) return 65536;
    if (seg == SEG_ROM) return 20480;
    for (i = 0; i < CLAIM_MAX; i++)
        if (claim[i].live && claim[i].seg == seg)
            return claim[i].cap;
    return -1;
}

unsigned os88_mem_claim(int kb)
{
    int i;
    if (kb <= 0)
        return 0;                       /* a claim of nothing is a refusal,
                                         * and the loader's 16-bit ceiling
                                         * used to ask for one */
    if (kb == 64 && !ram_live) { ram_live = 1; return SEG_RAM; }
    if (kb == 20 && !rom_live) { rom_live = 1; return SEG_ROM; }
    for (i = 0; i < CLAIM_MAX; i++)
        if (!claim[i].live) {
            claim[i].live = 1;
            claim[i].seg = (unsigned)(0x4000 + i * 0x400);
            claim[i].cap = (long)kb * 1024;
            claims_live++;
            return claim[i].seg;
        }
    return 0;                           /* the heap is full: a normal path */
}

int os88_mem_free(unsigned seg)
{
    int i;
    if (seg == SEG_RAM) { ram_live = 0; return 0; }
    if (seg == SEG_ROM) { rom_live = 0; return 0; }
    for (i = 0; i < CLAIM_MAX; i++)
        if (claim[i].live && claim[i].seg == seg) {
            claim[i].live = 0;
            claims_live--;
            return 0;
        }
    fail("os88_mem_free of a segment that is not a live claim");
    return -1;
}

unsigned os88_mem_largest_kb(void) { return 200; }
int  os88_peek(unsigned seg, unsigned off) { return segbase(seg)[off]; }
void os88_poke(unsigned seg, unsigned off, int v) { segbase(seg)[off] = (unsigned char)v; }

/* the .PRG fixture the next os88_file_read_seg serves, by name */
static char fx_name[16];
static unsigned char fx_data[65536];
static long fx_len;

unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    FILE *f;
    unsigned n;
    long room = segcap(seg);

    if (room < 0) {
        fail("a read into a segment that is not a live claim");
        return 0;
    }
    if ((long)cap > room) {
        /* THE ONE THAT MATTERS: on a real machine this is `cap` bytes written
         * into a claim that is not that big - somebody else's memory, or the
         * kernel's - and it does not fault. */
        printf("c64uitest: FAIL - a %u-byte read into a %ld-byte claim\n",
               cap, room);
        fails++;
        return 0;
    }
    if (fx_name[0] && strcmp(name, fx_name) == 0) {
        n = (unsigned)((fx_len < (long)cap) ? fx_len : (long)cap);
        memcpy(segbase(seg), fx_data, n);
        return n;
    }
    if (strcmp(name, "C64.ROM") != 0)   /* c64.c's C64_ROM_NAME, which
                                         * is not declared until it is
                                         * #included below */
        return 0;                       /* no such file: a normal refusal */
    if (!rom_present)
        return 0;
    f = fopen("build/c64-rom/C64.ROM", "rb");
    if (!f) {
        printf("c64uitest: build/c64-rom/C64.ROM is missing - "
               "run `python3 tools/c64rom.py` first\n");
        exit(2);
    }
    n = (unsigned)fread(segbase(seg), 1, cap, f);
    fclose(f);
    return n;
}

/* --- the runtime helpers ------------------------------------------------- */
void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memmove(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
void os88_strcpy(char *d, const char *s, unsigned cap)
{
    unsigned i = 0;
    if (cap == 0) return;
    while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}
char *os88_utoa(unsigned v, char *d) { sprintf(d, "%u", v); return d; }

/* ==========================================================================
 * THE ASSEMBLY HALF, TRANSCRIBED FOR THE HOST
 *
 * These are apps/c64/c64mem.inc's and c64band.inc's routines in C. See the
 * header: this makes tools/c64ref.py a check on the algorithm and not on the
 * 8086 encoding, and the encoding has gates of its own.
 * ========================================================================*/
#define H_SCR_BASE 0xFFC0
#define H_SCR_END  0x3A

int c64_rd(unsigned a) { return ram[a & 0xFFFF]; }
int c64_rd16(unsigned a) { return ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8); }
int c64_rom_rd(unsigned off) { return rom[off % 20480]; }
int c64_scr_rd(unsigned off) { return ram[H_SCR_BASE + off]; }
void c64_scr_wr(unsigned off, int v) { ram[H_SCR_BASE + off] = (unsigned char)v; }

static void h_dirtybit(unsigned a)
{
    int page = (a >> 8) & 0xFF;
    ram[H_SCR_BASE + 0x00 + (page >> 3)] |= (unsigned char)(0x80 >> (page & 7));
}

/* c64mem.inc's _c64_wr, from `.store` down. THE WINDOW IS TAKEN OVER THE
 * WATCH RANGE at $32/$34 and not over the address space: without that test
 * every stack push and every zero-page write widens it across the whole
 * matrix the moment there is a core, and the flush's per-row intersection
 * always answers "all forty cells". The cost table below is only honest
 * because h_noise() writes the stack and zero page too. */
static void h_window(unsigned a)
{
    unsigned wlo = ram[H_SCR_BASE + 0x32] | (ram[H_SCR_BASE + 0x33] << 8);
    unsigned whi = ram[H_SCR_BASE + 0x34] | (ram[H_SCR_BASE + 0x35] << 8);
    unsigned lo, hi;
    ram[H_SCR_BASE + 0x30] = 1;
    if (a < wlo || a > whi)
        return;
    lo = ram[H_SCR_BASE + 0x2C] | (ram[H_SCR_BASE + 0x2D] << 8);
    hi = ram[H_SCR_BASE + 0x2E] | (ram[H_SCR_BASE + 0x2F] << 8);
    if (a < lo) { ram[H_SCR_BASE + 0x2C] = a & 0xFF; ram[H_SCR_BASE + 0x2D] = a >> 8; }
    if (a > hi) { ram[H_SCR_BASE + 0x2E] = a & 0xFF; ram[H_SCR_BASE + 0x2F] = a >> 8; }
}

void c64_wr(unsigned a, int v)
{
    a &= 0xFFFF;
    if (a >= H_SCR_BASE && a < H_SCR_BASE + H_SCR_END)
        return;                             /* the scratch is not RAM (3.5) */
    ram[a] = (unsigned char)v;
    h_window(a);
    h_dirtybit(a);
}

void c64_dirty(unsigned a) { h_dirtybit(a & 0xFFFF); }

void c64_zcopy_in(unsigned a, const void *src, unsigned n)
{ memmove(ram + (a & 0xFFFF), src, n); }
void c64_zcopy_out(void *dst, unsigned a, unsigned n)
{ memmove(dst, ram + (a & 0xFFFF), n); }
void c64_zzcopy_in(unsigned a, unsigned sseg, unsigned soff, unsigned n)
{ memmove(ram + (a & 0xFFFF), segbase(sseg) + soff, n); }
void c64_zfill(unsigned a, int v, unsigned n)
{ memset(ram + (a & 0xFFFF), v, n); }

int c64_run(unsigned cycles) { n_cpu++; return 1; /* C64_RUN_JAM: wave 1's
                                                   * core has no opcodes */ }

/* c64band.inc, transcribed. The composer is c64_band1's phase A and B: read
 * the character and the colour, index the character generator, and put the
 * glyph byte through the {and, xor} pair the colour resolves to. */
extern unsigned char c64_cmask[32];
extern unsigned char c64_bgfill;

#define HB_STRIDE 40

void c64_band1(unsigned char *dst, int first, int last,
               unsigned mseg, unsigned moff,
               const unsigned char *col,
               unsigned gseg, unsigned goff,
               int mode, int bg)
{
    const unsigned char *m = segbase(mseg);
    const unsigned char *g = segbase(gseg);
    int c, k, ch, fg;

    if (last < first)
        return;
    n_band_calls++;
    n_band_cells += last - first + 1;
    if (mode != 0) {
        for (k = 0; k < 8; k++)
            for (c = first; c <= last; c++)
                dst[k * HB_STRIDE + c] = c64_bgfill;
        return;
    }
    for (c = first; c <= last; c++) {
        ch = m[(moff + c) & 0xFFFF];
        fg = col[c] & 15;
        for (k = 0; k < 8; k++) {
            int gl = g[(goff + ch * 8 + k) & 0xFFFF];
            dst[k * HB_STRIDE + c] =
                (unsigned char)((gl & c64_cmask[fg * 2]) ^ c64_cmask[fg * 2 + 1]);
        }
    }
}

int c64_rowspan(const unsigned char *a, const unsigned char *b, int n)
{
    int k, c, f = 0x7FFF, l = -1;
    n_span_calls++;
    for (k = 0; k < 8; k++)
        for (c = 0; c < n; c++)
            if (a[k * HB_STRIDE + c] != b[k * HB_STRIDE + c]) {
                if (c < f) f = c;
                if (c > l) l = c;
            }
    if (l < 0)
        return -1;
    return (f << 8) | l;
}

void c64_rowcopy(unsigned char *dst, const unsigned char *src, int n)
{
    int k;
    for (k = 0; k < 8; k++)
        memmove(dst + k * HB_STRIDE, src + k * HB_STRIDE, n);
}

unsigned c64_rowsig(unsigned mseg, unsigned moff, const unsigned char *col, int n)
{
    const unsigned char *m = segbase(mseg);
    unsigned s = 0;
    int i;
    n_sig_calls++;
    for (i = 0; i < n; i++) {
        unsigned bx = m[(moff + i) & 0xFFFF] | ((unsigned)col[i] << 8);
        s ^= bx;
        s = ((s << 1) | (s >> 15)) & 0xFFFF;
    }
    return s;
}

static unsigned char x2tab[512];
void c64_x2init(void)
{
    int b, k;
    for (b = 0; b < 256; b++) {
        unsigned d = 0;
        for (k = 7; k >= 0; k--) {
            d <<= 2;
            if (b & (1 << k))
                d |= 3;
        }
        x2tab[b * 2] = (unsigned char)(d >> 8);
        x2tab[b * 2 + 1] = (unsigned char)(d & 0xFF);
    }
}

void c64_band_x2(unsigned char *dst, const unsigned char *src, int rows)
{
    int r, c;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < HB_STRIDE; c++) {
            dst[r * 160 + c * 2] = x2tab[src[r * HB_STRIDE + c] * 2];
            dst[r * 160 + c * 2 + 1] = x2tab[src[r * HB_STRIDE + c] * 2 + 1];
        }
        memmove(dst + r * 160 + 80, dst + r * 160, 80);
    }
}

/* ==========================================================================
 * THE PROGRAM
 * ========================================================================*/
#include "c64.c"

/* c64cpu.inc's register file, which the C declares extern (4.1). It is
 * defined AFTER the include because the struct is c64.c's. */
struct c64_mach c64_m;

/* ==========================================================================
 * THE AUDIT: the glass shows what the shadow says it shows
 * ========================================================================*/
static int audit_rows = C64_ROWS;       /* rows the window actually shows */

static void audit(const char *where)
{
    int r, y, x, bad = 0;
    for (r = 0; r < audit_rows; r++) {
        for (y = 0; y < 8; y++) {
            for (x = 0; x < 320; x++) {
                int want = (c64_sh[r * 320 + y * 40 + (x >> 3)]
                            >> (7 - (x & 7))) & 1;
                /* from the LIVE screen origin: it is centred in the content
                 * box and fullscreen moves it (9.8) */
                int got = glass[c64_gsy + r * 8 + y][c64_gsx + x];
                if (got != want) {
                    if (bad < 3)
                        printf("c64uitest:   row %d px (%d,%d) glass %d shadow %d\n",
                               r, x, y, got, want);
                    bad++;
                }
            }
        }
    }
    if (bad) {
        printf("c64uitest: %s - %d pixel(s) where the glass and the shadow "
               "disagree\n", where, bad);
        fails++;
    }
}

/* ==========================================================================
 * THE SCROLL'S DIRECTION, ON THE MODELLED GLASS
 *
 * The audit above compares the glass with the SHADOW, and a package that
 * scrolls the wrong way while sliding its shadow the same wrong way agrees
 * with itself. So the direction is checked against pixels taken BEFORE the
 * scroll: positive dy moves the content UP (SPEC.md 5.5), so row y afterwards
 * is row y + k*8 from before, over the rows the scroll did not vacate.
 * ========================================================================*/
static unsigned char g_snap[200][320];
static int g_snap_rows;

static void glass_snap(void)
{
    int y, x;
    g_snap_rows = c64_gnrows * 8;
    for (y = 0; y < g_snap_rows; y++)
        for (x = 0; x < 320; x++)
            g_snap[y][x] = glass[c64_gsy + y][c64_gsx + x];
}

/* row_truth - compose row `r` from the sources as they stand NOW, with the
 * flush's own frame registers, and assert the glass shows it. This is the
 * only check in the file that does not go through the shadow, and it exists
 * for one reason: a package that scrolls its shadow and its glass the same
 * wrong way agrees with itself, and audit() cannot tell. */
static unsigned char truth_bnd[40 * 8];

static void row_truth(int r, const char *where)
{
    int y, x, bad = 0, save_calls = n_band_calls, save_cells = n_band_cells;

    c64_band1(truth_bnd, 0, C64_COLS - 1, c64_m.ramseg,
              c64_mbase + C64_X40(r), c64_col + C64_X40(r),
              c64_chr_seg, c64_chr_off, c64_mode, c64_vic[0x21] & 15);
    n_band_calls = save_calls;              /* the audit is not a cost */
    n_band_cells = save_cells;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 320; x++) {
            int want = (truth_bnd[y * 40 + (x >> 3)] >> (7 - (x & 7))) & 1;
            if (glass[c64_gsy + r * 8 + y][c64_gsx + x] != want)
                bad++;
        }
    if (bad) {
        printf("c64uitest: %s - row %d: %d pixel(s) where the GLASS "
               "disagrees with the row's own SOURCES\n", where, r, bad);
        fails++;
    }
}

static void glass_moved_up(int k)
{
    int y, x, bad = 0, moved = 0;
    int n = g_snap_rows - k * 8;

    for (y = 0; y < n; y++)
        for (x = 0; x < 320; x++) {
            if (glass[c64_gsy + y][c64_gsx + x] != g_snap[y + k * 8][x])
                bad++;
            if (g_snap[y][x] != g_snap[y + k * 8][x])
                moved++;                    /* the fixture really does differ
                                             * by the shift, so this is not a
                                             * test that passes on a no-op */
        }
    if (moved == 0) {
        printf("c64uitest: HARNESS - the shift fixture is identical either "
               "way, so the direction cannot be checked\n");
        exit(1);
    }
    if (bad)
        fail("a k-row scroll did not move the picture UP: positive dy is "
             "content up (SPEC.md 5.5, os88.h:437) and the vacated rows are "
             "the k at the BOTTOM");
}

/* ==========================================================================
 * THE COST TABLE, IN MILLISECONDS (C64-SPEC §9.7)
 *
 * Priced per primitive: PERFORMANCE.md's 756 us for any gfx_* drawing call,
 * whatever it draws, and this package's own tests/c64band figures for the
 * compose, the span compare and the signature (c64scr.c's C64BENCH_*). It is
 * a MODEL, not a measurement of the whole path - which is exactly what
 * PERFORMANCE.md rule 4 asks for until a counter on the target replaces it.
 * ========================================================================*/
static int b_blit, b_fill, b_scroll, b_frame, b_run, b_str, b_bandc, b_cells,
           b_span, b_sig;
static long b_blit_bytes, b_run_cells, b_str_cells;

static void cost_begin(void)
{
    b_blit = n_blit; b_fill = n_fill; b_scroll = n_scroll; b_frame = n_frame;
    b_run = n_run; b_str = n_str;
    b_bandc = n_band_calls; b_cells = n_band_cells;
    b_span = n_span_calls; b_sig = n_sig_calls;
    b_blit_bytes = n_blit_bytes;
    b_run_cells = n_run_cells; b_str_cells = n_str_cells;
}

static void cost_row(const char *what)
{
    long us, gcells;
    int dcalls = (n_blit - b_blit) + (n_fill - b_fill) + (n_scroll - b_scroll)
               + (n_frame - b_frame)
               + (n_run - b_run) + (n_str - b_str);
    /* PERFORMANCE.md's 756 us is the FLOOR of any gfx_* call - os88_font_run
     * and os88_font_str included, which is why they are in `dcalls` - and a
     * wide blit costs its pixels on top: tests/c64band measured 320x8 at
     * 1.84 ms, so (1840 - 756) / 320 = 3.4 us a band byte. Pricing every blit
     * at the floor alone under-reported a full expose by a factor of two.
     * A GLYPH CELL IS 900 us on top of its call's floor, which is the other
     * half of PERFORMANCE.md's own row: 756 + 78 x 900 = 70.9 ms against the
     * ~71 ms it quotes for a 78-cell line of text. */
    gcells = (n_run_cells - b_run_cells) + (n_str_cells - b_str_cells);
    us = (long)dcalls * 756
       + (n_blit_bytes - b_blit_bytes) * 34 / 10
       + gcells * 900
       + (long)(n_band_calls - b_bandc) * C64BENCH_CALL
       + (long)(n_band_cells - b_cells) * C64BENCH_CELL
       + (long)(n_span_calls - b_span) * C64BENCH_SPAN
       + (long)(n_sig_calls - b_sig) * C64BENCH_SIG;
    printf("  %-32s %7.1f ms   %2d blit %2d fill %2d scroll  "
           "%3d cells in %2d compose call(s)  %3ld glyph cell(s)\n",
           what, us / 1000.0, n_blit - b_blit, n_fill - b_fill,
           n_scroll - b_scroll, n_band_cells - b_cells,
           n_band_calls - b_bandc, gcells);
}

/* ==========================================================================
 * THE DUMP tools/c64ref.py checks (C64-SPEC §14.5)
 * ========================================================================*/
static void dump_for_ref(const char *state_path, const char *frame_path)
{
    FILE *f;
    f = fopen(state_path, "wb");
    if (!f) { fail("cannot write the c64ref state file"); return; }
    fwrite(ram, 1, 65536, f);
    fwrite(c64_col, 1, 1024, f);
    fwrite(c64_vic, 1, 64, f);
    fwrite(c64_cia2, 1, 16, f);
    fwrite(rom + 0x4000, 1, 4096, f);       /* CHARGEN, C64.ROM's layout */
    fclose(f);
    f = fopen(frame_path, "wb");
    if (!f) { fail("cannot write the c64ref frame file"); return; }
    fwrite(c64_sh, 1, 8000, f);
    fclose(f);
}

/* ==========================================================================
 * THE SCRIPT
 * ========================================================================*/
static void flush_now(void)
{
    the_ticks += 4;
    os88_onwake(the_win);                   /* the wake runs with the lock NOT
                                             * held and takes it itself (74.1) */
    if (lock_depth != 0) { fail("a wake left the gfx lock held"); exit(1); }
}

/* THE KERNEL DISPATCHES EVERY CALLBACK UNDER THE LOCK, so the script does
 * too - that is the whole point of modelling the depth. */
/* W_PAINT IS THE ONE CALLBACK THE KERNEL ARMS THE CLIP FOR (11.3), so the
 * script arms it here and nowhere else - which is the whole point of
 * modelling it. */
static void do_paint(void *w)
{ os88_gfx_lock(); clip_armed = 1; os88_paint(w); os88_gfx_unlock(); }
static void do_click(int x, int y, void *w)
{ os88_gfx_lock(); os88_onclick(x, y, w); os88_gfx_unlock(); }
static void do_cmd(int item, int menu, void *w)
{ os88_gfx_lock(); os88_oncmd(item, menu, w); os88_gfx_unlock(); }
static void do_about(void *w)
{ os88_gfx_lock(); os88_about(w); os88_gfx_unlock(); }

/* --- SMART ATTACH, driven the way the product is (11.3) -------------------
 * os88_onfile refuses by SIZE and then calls ovl_load_prg, which claims
 * ceil(size / 1KB), reads the file into it, moves it into the C64's RAM at
 * the file's own two-byte load address and frees the claim. Both halves of
 * that arithmetic were wrong and nothing here could ask. */
static int attach(unsigned load, long body, int fill)
{
    long i;
    int before = claims_live;

    strcpy(fx_name, "TEST.PRG");
    fx_data[0] = (unsigned char)(load & 0xFF);
    fx_data[1] = (unsigned char)(load >> 8);
    for (i = 0; i < body; i++)
        fx_data[2 + i] = (unsigned char)(fill ? fill : (int)((i * 7 + 1) & 0xFF));
    fx_len = body + 2;
    last_toast[0] = 0;
    os88_gfx_lock();
    os88_onfile(OS88_FDLG_OPEN, "TEST.PRG", (unsigned)fx_len, 0, the_win);
    os88_gfx_unlock();
    if (claims_live != before)
        fail("Smart attach leaked its scratch claim");
    return strncmp(last_toast, "Loaded ", 7) == 0;
}

/* h_noise - what a CORE does to memory between flushes, and what the write
 * window has to survive: a JSR pushes at $01xx, a BASIC statement writes zero
 * page and the variables above $0800. Without the watch range every one of
 * these widens the window across the whole matrix and every cost row below
 * becomes a description of a machine with no CPU in it. */
static void h_noise(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        c64_wr(0x0100 + 0xF0 + i, (unsigned char)i);    /* the stack */
        c64_wr(0x00A0 + i, (unsigned char)i);           /* zero page */
        c64_wr(0x0810 + i, (unsigned char)i);           /* BASIC's variables */
    }
}

/* ==========================================================================
 * THE ROM-LESS MACHINE, AS ITS OWN PROCESS (1.4, 9.5)
 *
 * `build/c64uitest --no-rom` is a SECOND RUN, because os88_main runs once per
 * process and the refusal surface is decided there. Without it nothing in
 * this file ever cleared rom_present, so the one screen a user of a
 * mis-copied disk actually sees - the permanent notice naming the file - was
 * never drawn by any check. It is four os88_font_run of the KERNEL's face on
 * a fill, and it has to survive an expose like anything else.
 * ========================================================================*/
static int no_rom_main(void)
{
    void *win;
    int x, y, lit, left;

    rom_present = 0;
    memset(glass, 0x55, sizeof(glass));
    win = os88_main();
    if (win == 0) {
        fail("os88_main refused the launch when C64.ROM is missing - 1.4: "
             "the window STAYS UP to say which file is not there");
        return 1;
    }
    if (!c64_norom)
        fail("a missing C64.ROM did not raise the refusal surface");
    if (strncmp(last_toast, "C64: no C64.ROM", 15) != 0)
        fail("a missing C64.ROM was not toasted as well (9.8: both routes)");
    dmg_whole = 1;
    do_paint(win);
    lit = 0;
    for (y = 0; y < 60; y++)
        for (x = 0; x < 320; x++)
            if (glass[c64_gsy + 30 + y][c64_gsx + x] == 1)
                lit++;
    if (lit < 200)
        fail("the `C64.ROM is not on this disk` notice is not on the glass");

    /* IT IS DRAWN ONCE. Every menu command on this machine answers through
     * c64_say, and os88_onwake flushes and re-posts for the message's whole
     * ~5 seconds - so an ungated banner is ~45 repaints of 1 fill + 124 glyph
     * cells, ~115 ms each, at a ~100% duty cycle, on a static screen. */
    cost_begin();
    c64_say("Warp mode on.");
    flush_now();
    flush_now();
    if (n_fill - b_fill > 1 || (n_run_cells - b_run_cells) > 40)
        fail("the ROM-less notice was repainted by a message going up");

    /* ...AND A PARTIAL EXPOSE OF IT IS REPAIRED. The notice has no shadow and
     * no per-row force records - the branch that draws it returns before the
     * band machinery - so the gate above has to be answered by the expose
     * path, or an exposed strip of it stays blank for ever. */
    c64_msg[0] = 0;
    the_ticks += 200;
    flush_now();
    for (y = 0; y < 40; y++)
        for (x = 0; x < 160; x++)
            glass[c64_gsy + 40 + y][c64_gsx + x] = 0x55;   /* something covered
                                                            * it and went away */
    dmg_whole = 0;
    dmg.x1 = c64_gsx; dmg.y1 = c64_gsy + 40;
    dmg.x2 = c64_gsx + 159; dmg.y2 = c64_gsy + 79;
    do_paint(win);
    left = 0;
    for (y = 0; y < 40; y++)
        for (x = 0; x < 160; x++)
            if (glass[c64_gsy + 40 + y][c64_gsx + x] == 0x55)
                left++;
    if (left)
        fail("a partial expose of the ROM-less notice was never repaired - "
             "the surface has no shadow, so the expose path has to say so");

    if (fails) {
        printf("c64uitest --no-rom: %d FAILURE(S)\n", fails);
        return 1;
    }
    printf("c64uitest --no-rom: PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    void *win;
    int i, k;

    if (argc > 1 && strcmp(argv[1], "--no-rom") == 0)
        return no_rom_main();

    memset(glass, 0x55, sizeof(glass));     /* nothing is drawn yet, and 0x55
                                             * is neither lit nor dark */

    win = os88_main();
    if (win == 0) { fail("os88_main refused with the ROM present"); return 1; }

    /* THE MODEL IS THE MACHINE'S WIDTH, and this is the one line that makes
     * that true. os88_wm_create authors a FRAME; os88_wm_geom answers
     * `W_W - 2` (kernel/wm.inc:5442). GW is the CONTENT box, so the package
     * must author two more than the row it lays out - and when it did not,
     * §10.1's 42nd cell (the pause lamp) lived in pixels the window does not
     * have and was dropped on every flush-driven redraw. */
    if (C64_W_W - 2 != GW)
        fail("the window's frame is not the modelled content box + 2 - "
             "wm_geom answers W_W - 2 and the status row is laid out in "
             "GW pixels");

    /* --- the first paint: a whole expose ------------------------------- */
    dmg_whole = 1;
    cost_begin();
    do_paint(win);
    printf("c64uitest: the cost table, in milliseconds "
           "(C64-SPEC §9.7)\n");
    cost_row("full expose, 25 rows");
    audit("after the first paint");
    if (n_scroll != 0)
        fail("the first paint scrolled");

    /* the boot screen must actually be on the glass: READY. is at row 5 */
    {
        int lit = 0, x, y;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 48; x++)
                if (glass[c64_gsy + 5 * 8 + y][c64_gsx + x])
                    lit++;
        if (lit < 20)
            fail("READY. is not on the glass");
    }
    dump_for_ref("build/c64ref-state.bin", "build/c64ref-frame.bin");
    /* ...and the luminance table itself, for tools/c64ref.py --lumcheck. A
     * rendered frame can only ask about ONE background at a time, and 9.6's
     * fact is about all 256 ordered pairs - the seven equal-luminance pairs
     * in BOTH directions included. */
    {
        FILE *lf = fopen("build/c64ref-lum.bin", "wb");
        if (!lf)
            fail("cannot write the luminance table");
        else {
            fwrite(c64_lum, 1, 16, lf);
            fclose(lf);
        }
    }

    /* --- one changed cell ---------------------------------------------- */
    cost_begin();
    h_noise();                              /* ...while a core runs (see above) */
    c64_wr(0x0400 + 10 * 40 + 20, 1);       /* an 'A' in row 10, column 20 */
    flush_now();
    cost_row("one changed cell");
    audit("after one changed cell");
    if (n_band_cells - b_cells != 1)
        fail("one changed cell did not compose exactly one cell");
    if (n_blit - b_blit != 1)
        fail("one changed cell was not one blit");

    /* --- one changed row ------------------------------------------------ */
    cost_begin();
    h_noise();
    for (i = 0; i < 40; i++)
        c64_wr(0x0400 + 12 * 40 + i, (unsigned char)(1 + (i % 26)));
    flush_now();
    cost_row("one changed row");
    audit("after one changed row");
    if (n_blit - b_blit != 1)
        fail("one changed row was not one blit");
    if (n_band_cells - b_cells != 40)
        fail("one changed row did not compose its 40 cells");

    /* --- TWO POKES IN DISTANT ROWS: the window INTERSECTED with the pages
     *     (9.2). One lo/hi window over the matrix says "rows 0..24" for a
     *     score written at row 0 and a status line at row 24 inside the same
     *     flush interval - which is what a game and the KERNAL both do - and
     *     25 rows recomposing 1000 cells for two changed bytes is the cost
     *     the window exists to avoid. Row i's forty bytes lie in at most two
     *     256-byte pages, so a row is dirty only if the window covers it AND
     *     one of its own pages is marked: rows 0..6 (page 4) and 19..24
     *     (page 7) here, and only the two that actually changed blit. ---- */
    cost_begin();
    h_noise();
    c64_wr(0x0400 + 0 * 40 + 3, 8);
    c64_wr(0x0400 + 24 * 40 + 3, 9);
    flush_now();
    cost_row("two pokes, rows 0 and 24");
    audit("after two distant pokes");
    if (n_band_calls - b_bandc > 14)
        fail("two pokes in distant rows recomposed more than their own "
             "pages' rows - the write window is not intersected with the "
             "dirty-page bitmap");
    if (n_blit - b_blit != 2)
        fail("two pokes in distant rows were not exactly two blits");

    /* --- a k = 9 shift: ONE gfx_scroll and 9 composed rows -------------- */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 7 + k) % 26)));
    flush_now();
    audit("before the shift");

    glass_snap();                           /* ...so the DIRECTION can be
                                             * checked and not only the count */
    cost_begin();
    memmove(ram + 0x0400, ram + 0x0400 + 9 * 40, (25 - 9) * 40);
    for (i = 25 - 9; i < 25; i++)
        for (k = 0; k < 40; k++)
            ram[0x0400 + i * 40 + k] = 32;
    for (i = 0x0400; i < 0x0400 + 1000; i++)
        c64_dirty((unsigned)i);
    c64_scr_wr(0x2C, 0x00); c64_scr_wr(0x2D, 0x04);
    c64_scr_wr(0x2E, 0xE7); c64_scr_wr(0x2F, 0x07);
    for (i = 0; i < C64_ROWS; i++) { c64_rowd[i] = 1; }
    c64_dirty_any = 1;
    flush_now();
    cost_row("a k = 9 shift");
    audit("after a k = 9 shift");
    if (n_scroll - b_scroll != 1)
        fail("the k = 9 shift was not exactly ONE gfx_scroll per flush");
    if (n_blit - b_blit != 9)
        fail("the k = 9 shift did not draw exactly its 9 vacated rows");
    /* THE PIXELS MOVED UP. Counting the calls cannot see a scroll that went
     * the wrong way - the package asked for `-k * 8` for the whole of wave 1
     * and this step passed, because the stub modelled the same wrong
     * convention. A row that scrolled up is one row HIGHER on the glass than
     * it was, and the VACATED rows are the k at the BOTTOM. */
    glass_moved_up(9);
    /* ...AND EVERY SHIFTED ROW WAS STILL COMPOSED AND COMPARED. 9.4: the
     * signature is a HINT and nothing rests on it. Skipping the 16 slid rows
     * would save 188 ms and buy a wrong screen that never repairs itself
     * (the collision step below). */
    if (n_band_calls - b_bandc != C64_ROWS)
        fail("a k = 9 shift did not compose and compare every row - the "
             "16-bit signature is a hint, not proof (9.4)");

    /* ...and the same shift with gfx_scroll REFUSING: the fallback must be
     * spans and the shadow must stay true (9.4) */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 5 + k) % 26)));
    flush_now();
    scroll_refuse = 1;
    cost_begin();
    memmove(ram + 0x0400, ram + 0x0400 + 3 * 40, (25 - 3) * 40);
    for (i = 25 - 3; i < 25; i++)
        for (k = 0; k < 40; k++)
            ram[0x0400 + i * 40 + k] = 32;
    c64_scr_wr(0x2C, 0x00); c64_scr_wr(0x2D, 0x04);
    c64_scr_wr(0x2E, 0xE7); c64_scr_wr(0x2F, 0x07);
    for (i = 0; i < C64_ROWS; i++) c64_rowd[i] = 1;
    c64_dirty_any = 1;
    flush_now();
    cost_row("a k = 3 shift, scroll refused");
    audit("after a refused scroll");
    if (n_scroll - b_scroll != 1)
        fail("the refused scroll was not attempted exactly once");
    scroll_refuse = 0;

    /* --- A SIGNATURE COLLISION: the shift test is a HINT (9.4) ----------
     * c64_rowsig is an XOR of `matrix | colour << 8` under a one-bit rotate
     * per cell, so a cell's contribution is `rol(v, 40 - i)`: two rows that
     * differ in ONE cell each, one column apart, collide whenever the second
     * difference is the first rotated left once. Screen code 34 in column 5
     * and screen code 36 in column 6, over an otherwise blank row of one
     * colour, is such a pair - (32 ^ 34) rol 35 == (32 ^ 36) rol 34 == 0x10 -
     * and they are different glyphs, so the pixels differ.
     *
     * Build a frame whose shift test therefore MATCHES at k = 1 while row 0's
     * real content is not what row 1 held. The scroll is emitted (that is
     * fine and is what the hint is for), the shadow slides with it - and the
     * compose-and-compare that 9.4 insists on is the only thing standing
     * between that and a wrong row that never repairs itself, because on the
     * next flush nothing is dirty any more. row_truth() checks the glass
     * against the row's own sources, which is the one thing audit() cannot
     * do here: a package that slides its shadow the same wrong way agrees
     * with itself. */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (i == 1 && k == 5) ? 34 : 32);
    flush_now();
    audit("before the signature collision");
    cost_begin();
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (i == 0 && k == 6) ? 36 : 32);
    flush_now();
    cost_row("a k = 1 shift the signature got WRONG");
    if (n_scroll - b_scroll != 1)
        fail("the collision fixture did not reach the scroll path at all - "
             "it is not testing what it says");
    audit("after the signature collision");
    row_truth(0, "after the signature collision");

    /* --- AND gfx_blit1 REFUSING: 9.5's SECOND PATH, ONCE ---------------
     * os88.h:446 - blit1 answers -1 with NOTHING DRAWN, for a broken argument
     * or for a kern_small kernel that carries the slot without the body, "so
     * TEST IT and have a second path". Two wrong answers were shipped and
     * both are gated here: discarding the answer and updating the shadow
     * anyway (a permanently blank screen the compare then refuses to repair),
     * and keeping the row OWED (the wake re-posts and the row recomposes and
     * is refused again, for ever). The right answer is 9.5's: draw the row
     * with the kernel's face, ONCE, and say what the machine cannot do.
     *
     * audit() is NOT applicable inside this bracket, and that is a property
     * of the fallback rather than a gap: the glass carries the FONT's pixels
     * while the shadow carries the composed band, which on this path is a
     * proxy for the sources and not a claim about pixels (c64_row_font says
     * so). The shadow is thrown away and repainted at the end of the step,
     * and audited then. */
    flush_now();
    blit_refuse = 1;
    cost_begin();
    last_toast[0] = 0;
    h_noise();
    c64_wr(0x0400 + 7 * 40 + 5, 3);
    flush_now();
    cost_row("a changed cell, blit1 REFUSING");
    if (n_blit - b_blit != 0)
        fail("a refused blit was counted as drawn");
    if ((n_run_cells - b_run_cells) == 0)
        fail("a refused blit drew nothing at all - 9.5 gives it a font path");
    if (last_toast[0] == 0)
        fail("a refused blit said nothing - SPEC.md 47: a refusal names its "
             "fact, and 9.8 says on both routes");
    cost_begin();
    the_ticks += 4;
    os88_onwake(the_win);
    if ((n_run_cells - b_run_cells) != 0 || n_band_calls - b_bandc != 0)
        fail("the flush after a refused blit drew the row AGAIN - the "
             "fallback is a draw, not a deferral");
    blit_refuse = 0;
    c64_msg[0] = 0;
    c64_st_ok = 0;
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    audit("after the band path came back");

    /* --- 25 rows changed and NOT a shift -------------------------------- */
    cost_begin();
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 13 + k * 3) % 26)));
    flush_now();
    cost_row("25 rows changed, not a shift");
    audit("after a 25-row non-shift change");
    if (n_scroll - b_scroll != 0)
        fail("a non-shift change emitted a scroll");

    /* --- a $D020-only change: fills only, no bands ---------------------- */
    cost_begin();
    h_noise();
    c64_io_wr(0xD020, 1);                   /* the border to white */
    flush_now();
    cost_row("a $D020 change");
    audit("after a $D020 change");
    if (n_band_calls - b_bandc != 0)
        fail("a border change composed a band");
    if (n_fill - b_fill == 0)
        fail("a border change filled nothing");

    /* --- a register write that changes NOTHING draws nothing ----------- */
    cost_begin();
    for (i = 0; i < 8; i++)
        c64_io_wr(0xD011, 0x1B);            /* what a raster IRQ writes, 50x a
                                             * second, forever */
    c64_io_wr(0xD016, c64_vic[0x16]);
    c64_io_wr(0xDD00, 0x17 ^ 0x08);         /* the KERNAL bit-banging the
                                             * serial bus: NOT the bank bits */
    flush_now();
    cost_row("8 x $D011, $D016, a $DD00 serial edge");
    audit("after unchanged register writes");
    if (n_blit - b_blit != 0 || n_band_calls - b_bandc != 0)
        fail("a register write that changed nothing drew something");
    c64_io_wr(0xDD00, 0x17);

    /* --- a register write that DOES change: recompose, then COMPARE ----- */
    cost_begin();
    c64_io_wr(0xD016, (c64_vic[0x16] ^ 0x01));   /* fine scroll x: every row
                                                  * recomposes, and composes
                                                  * the SAME pixels */
    flush_now();
    cost_row("a $D016 change that draws the same picture");
    audit("after a $D016 change");
    if (n_band_calls - b_bandc == 0)
        fail("a frame-register change did not recompose");
    if (n_blit - b_blit != 0)
        fail("a frame-register change that composed the same pixels still "
             "drew - the shadow compare was forced off");

    /* --- a slice with no tick boundary costs nothing (9.7) -------------- */
    cost_begin();
    c64_wr(0x0400 + 3 * 40 + 7, 5);         /* something to draw... */
    os88_onwake(the_win);                   /* ...in the tick that just
                                             * flushed: the flush runs AT MOST
                                             * ONCE PER HOST TICK (9.3) */
    if (n_blit - b_blit || n_fill - b_fill || n_band_calls - b_bandc)
        fail("a wake inside the flushed tick drew something");
    cost_row("a wake with no tick boundary");
    flush_now();
    audit("after the deferred cell was flushed");

    /* --- a RAM character set: the same audit, and c64ref checks it ------ */
    c64_io_wr(0xD018, 0x1C);                /* matrix $0400, charset $3000 */
    for (i = 0; i < 2048; i++)
        c64_wr(0x3000 + i, (unsigned char)((i * 37) & 0xFF));
    flush_now();
    audit("after a RAM character set");
    dump_for_ref("build/c64ref-state2.bin", "build/c64ref-frame2.bin");

    /* --- the status row DELTA-draws: one field, no erase ---------------- */
    c64_msg[0] = 0;
    c64_st_ok = 0;
    c64_dirty_any = 1;
    flush_now();                            /* a full row, erased and drawn */
    cost_begin();
    c64_joy1 = 0x11;                        /* up + fire on control port 1 */
    c64_dirty_any = 1;                      /* ...and nothing else: the
                                             * flush calls c64_status every
                                             * time and the FIELD COMPARE is
                                             * the gate, which is the path the
                                             * product takes. Setting the flag
                                             * by hand measured a path nothing
                                             * in the package could reach. */
    flush_now();
    cost_row("one joystick indicator changed");
    if (n_fill - b_fill != 0)
        fail("a status DELTA erased the row - only a full redraw may fill");
    if (n_blit - b_blit != 1)
        fail("a status delta was not exactly ONE band");
    /* ...and a message REPLACING a message is the same field with different
     * text: it must still be drawn (c64_say clears c64_st_ok for it). */
    c64_say("Warp mode on.");
    flush_now();
    if (strcmp(last_toast, "Warp mode on.") != 0)
        fail("c64_say did not toast");
    cost_begin();
    c64_say("Paused.");
    flush_now();
    if (n_run - b_run == 0)
        fail("a message replacing a message did not reach the row");
    /* ...AND IT COMES DOWN BY ITSELF. The wake must keep re-posting while a
     * message is up, or the deadline is never reached with no core running
     * and the message owns the row for ever (C64-SPEC §10.1). */
    if (!c64_kick)
        fail("no wake was posted while a message was up - the message can "
             "never expire");
    the_ticks += 120;                       /* past the ~5 s deadline */
    flush_now();
    if (c64_msg[0] != 0)
        fail("the message did not expire");
    cost_begin();
    flush_now();
    if (n_run - b_run != 0 || n_blit - b_blit != 0)
        fail("the row redrew again after the message had already come down");
    c64_msg[0] = 0;
    c64_joy1 = 0;
    c64_st_ok = 0;
    c64_dirty_any = 1;
    flush_now();

    /* --- the About panel owns its rows, and closing it is DAMAGE ------- */
    c64_msg[0] = 0;
    c64_st_ok = 0;
    c64_dirty_any = 1;
    flush_now();
    do_about(win);
    if (!c64_abt)
        fail("Help > About VICE... did not open the panel");
    cost_begin();
    dmg_whole = 1;
    do_paint(win);
    cost_row("an expose with the About panel up");
    if (n_band_calls - b_bandc > C64_ROWS - 8)
        fail("an expose under the About panel composed rows the panel covers");

    /* ...and an expose that does NOT reach the panel must not repaint it.
     * ovl_about_show is 1 fill + 2 frames + 9 font_str over 160 glyph cells,
     * ~153 ms; called unconditionally it made an expose with the panel up
     * cost MORE than the full expose the hold rows exist to beat, and a menu
     * closing over one corner of the window paid it. */
    cost_begin();
    dmg_whole = 0;
    dmg.x1 = GX; dmg.y1 = GY;
    dmg.x2 = GX + 100; dmg.y2 = GY + 24;    /* above the panel */
    do_paint(win);
    cost_row("an expose that misses the panel");
    if ((n_run_cells - b_run_cells) + (n_str_cells - b_str_cells) != 0)
        fail("an expose that did not touch the About panel repainted it");
    if (!c64_abt)
        fail("an expose that missed the panel dropped the latch");
    dmg_whole = 1;
    do_paint(win);
                                            /* no audit here: the panel
                                             * is on the glass and the
                                             * shadow does not describe
                                             * the rows it owns until it
                                             * closes */

    cost_begin();
    do_click(GX + 4, GY + 4, win);          /* a click anywhere closes it */
    cost_row("the About panel closing");
    if (c64_abt)
        fail("the click did not close the About panel");
    audit("after the About panel closed");
    if (n_band_calls - b_bandc > 16)
        fail("closing the About panel recomposed more than the rows it "
             "covered - c64_sh_inval is not what a 14-row panel costs");

    /* --- a 200-line desktop: the window is CLAMPED and the status row is
     *     still on the glass. C64_CONT_H is 226 and a CGA desktop cannot give
     *     it; the row carries §1.4's permanent fact and every refusal, and a
     *     fixed offset from the top put it off the bottom entirely. ------- */
    geom_h = 150;
    audit_rows = (geom_h - C64_STATH - C64_BORDER * 2) / 8;
    c64_sh_inval();
    dmg_whole = 1;
    memset(glass, 0x55, sizeof(glass));
    do_paint(win);
    audit("after a clamped-window repaint");
    {
        int lit = 0, x, y;
        for (y = 0; y < C64_STATH; y++)
            for (x = 0; x < GW; x++)
                if (glass[GY + geom_h - C64_STATH + y][GX + x] != 0x55)
                    lit++;
        if (lit == 0)
            fail("the status row is not on the glass when the window is "
                 "clamped shorter than C64_CONT_H");
    }
    /* ...and a scroll is still ONE gfx_scroll there: the shift test must be
     * bounded by the rows on the glass, or c64_shsig[] past them stays 0 and
     * the test can never match on the target adapter (9.4). */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 3 + k) % 26)));
    flush_now();
    audit("before the clamped shift");
    cost_begin();
    memmove(ram + 0x0400, ram + 0x0400 + 40, 24 * 40);
    for (k = 0; k < 40; k++)
        ram[0x0400 + 24 * 40 + k] = 32;
    for (i = 0x0400; i < 0x0400 + 1000; i++)
        c64_dirty((unsigned)i);
    c64_scr_wr(0x2C, 0x00); c64_scr_wr(0x2D, 0x04);
    c64_scr_wr(0x2E, 0xE7); c64_scr_wr(0x2F, 0x07);
    for (i = 0; i < C64_ROWS; i++)
        c64_rowd[i] = 1;
    c64_dirty_any = 1;
    flush_now();
    cost_row("a k = 1 shift on a clamped window");
    audit("after a clamped k = 1 shift");
    if (n_scroll - b_scroll != 1)
        fail("a scroll on a clamped window was not ONE gfx_scroll - the "
             "shift test is not bounded by the rows on the glass");
    geom_h = GH;
    audit_rows = C64_ROWS;
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    audit("after the window came back");

    /* --- FULLSCREEN (9.8): the screen is CENTRED, the border and the status
     *     row fill the whole box, and OSAPI_FULLSCREEN's own synchronous
     *     repaint is not paid for twice. -------------------------------- */
    memset(glass, 0x55, sizeof(glass));
    cost_begin();
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win); /* Preferences > Fullscreen */
    cost_row("entering fullscreen");
    if (!fs_on)
        fail("Preferences > Fullscreen did not enter fullscreen");
    audit("after entering fullscreen");
    if (c64_gsx != GX + 160)
        fail("the C64 screen is not centred in a fullscreen content box");
    {
        int x, y, unpainted = 0, statlit = 0;
        for (y = 0; y < 200; y++)
            for (x = 0; x < 160; x++)
                if (glass[c64_gsy + y][GX + x] == 0x55)
                    unpainted++;            /* the left border strip */
        for (y = 0; y < C64_STATH; y++)
            for (x = 400; x < 640; x++)
                if (glass[GY + geom_h - C64_STATH + y][GX + x] != 0x55)
                    statlit++;              /* ...and the right of the row */
        if (unpainted)
            fail("fullscreen left the border beside the C64 screen unpainted "
                 "- wm_draw_win filled the WHOLE frame white and nothing "
                 "covered it");
        if (statlit == 0)
            fail("the status row does not reach the right of a fullscreen "
                 "content box");
    }
    /* AND THE NEXT WAKE DRAWS NOTHING. The kernel repainted us whole INSIDE
     * os88_fullscreen, so the shadow already describes the new glass; an
     * c64_sh_inval() on that arm cost 25 bands of pure double-draw. */
    cost_begin();
    flush_now();
    cost_row("the wake after entering fullscreen");
    if (n_blit - b_blit || n_fill - b_fill || n_band_calls - b_bandc)
        fail("the wake after OSAPI_FULLSCREEN redrew a screen the kernel had "
             "already repainted - the shadow was thrown away");

    /* ...AND ALT+D IS THE DOOR BACK, which under WF_FULL is the ONLY one:
     * kernel/wm.inc draws no chrome for a fullscreen window, so the menu item
     * that got the user in is not on the glass. SPEC.md 11.2.1 binds F/Esc
     * and C64-SPEC §9.8 takes Alt+D instead because the C64 owns both. */
    memset(glass, 0x55, sizeof(glass));
    os88_gfx_lock();
    os88_onkey(0, 0x20, win);               /* BIOS Alt+D */
    os88_gfx_unlock();
    if (fs_on)
        fail("Alt+D did not leave fullscreen - a WF_FULL window has no menu "
             "bar, so this chord is the only way back");
    audit("after Alt+D left fullscreen");

    os88_gfx_lock();
    os88_onkey(0, 0x20, win);               /* ...and in again */
    os88_gfx_unlock();
    if (!fs_on)
        fail("Alt+D did not enter fullscreen - the door binds both ways "
             "(SPEC.md 11.2.1)");
    memset(glass, 0x55, sizeof(glass));
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win); /* ...and back, by the menu */
    if (fs_on)
        fail("Preferences > Fullscreen did not leave fullscreen");
    audit("after leaving fullscreen");
    cost_begin();
    flush_now();
    if (n_blit - b_blit || n_band_calls - b_bandc)
        fail("the wake after LEAVING fullscreen redrew the screen again");

    /* --- A WINDOW COVERING US, AND THE CALLBACKS THAT DRAW (11.3) -------
     * The kernel arms the damage clip for W_PAINT and for nothing else, so
     * every OTHER callback that draws has to arm it itself and to have a
     * second path for the refusal. Two in this package do: the About panel's
     * open (a menu pick or the name menu's About item) and its close (a
     * click). With a strip of the screen belonging to somebody else, plot()
     * refuses any pixel written there while no clip is armed, and says where.
     */
    cov_x1 = c64_gsx + 200; cov_y1 = c64_gsy;
    cov_x2 = c64_gsx + 319; cov_y2 = c64_gsy + 199;
    cov_hits = 0;
    do_about(win);
    if (!c64_abt)
        fail("Help > About VICE... did not open the panel over a covering "
             "window - it should CLIP, not decline");
    do_click(GX + 4, GY + 4, win);          /* ...and the close, the same */
    if (c64_abt)
        fail("the click did not close the About panel");
    if (cov_hits)
        fail("a callback that is not W_PAINT drew without arming the clip - "
             "SPEC.md 11.3, and it lands on the window covering us");

    /* ...AND WHEN THE CLIP REFUSES - nothing of us is on the glass - the
     * panel is not opened at all rather than drawn into somebody else's
     * window, and the close falls back to invalidating the shadow. */
    clip_refuses = 1;
    do_about(win);
    if (c64_abt)
        fail("the About panel opened while NOTHING of the window shows");
    if (cov_hits)
        fail("something drew after wm_clip_set refused");
    clip_refuses = 0;
    cov_x1 = 1; cov_x2 = 0;                 /* the strip is ours again */
    c64_msg[0] = 0;
    c64_sh_inval();
    c64_st_ok = 0;
    dmg_whole = 1;
    do_paint(win);
    audit("after the covering window went away");

    /* --- SMART ATTACH: the two arithmetics, at their edges (11.3) -------
     * `ceil(size / 1KB)` is 16-bit on the target - `size + 1023` wraps above
     * 64,512 - and the end test used to refuse a PRG whose LAST byte is
     * exactly $FFFF while claiming it ran past it. os88_onfile refuses
     * anything over 65,533 before the disk is touched, so these are the four
     * edges that exist. */
    if (!attach(0x0801, 1, 0x5A))
        fail("Smart attach refused the smallest legal PRG (3 bytes)");
    if (ram[0x0801] != 0x5A)
        fail("the smallest PRG did not land at its load address");
    if (!attach(0x0400, 64512 - 2, 0))
        fail("Smart attach refused a 64,512-byte PRG");
    if (!attach(0x0400, 64513 - 2, 0))
        fail("Smart attach refused a 64,513-byte PRG - `size + 1023` wrapped "
             "in 16 bits and asked for a claim of nothing");
    if (!attach(0x0000, 65533 - 2, 0))
        fail("Smart attach refused the largest PRG os88_onfile lets through "
             "(65,533 bytes)");
    if (!attach(0xFFFF, 1, 0x3C))
        fail("Smart attach refused a PRG whose LAST byte is $FFFF - the end "
             "test was on one PAST the end");
    if (ram[0xFFFF] != 0x3C)
        fail("the $FFFF-endpoint PRG did not land at $FFFF");
    if (attach(0xFFFF, 2, 0x11))
        fail("Smart attach accepted a PRG that really does run past $FFFF");
    if (strncmp(last_toast, "PRG runs past", 13) != 0)
        fail("a PRG past $FFFF was refused without naming the fact");
    fx_name[0] = 0;
    c64_msg[0] = 0;
    c64_sh_inval();
    c64_st_ok = 0;
    dmg_whole = 1;
    do_paint(win);
    audit("after Smart attach");

    /* --- THE ALT CHORDS THE MENU ADVERTISES (7.5, 11.1) -----------------
     * A caption is not an accelerator in this kernel: c64menu.c prints
     * VICE's own .vhk bindings and SPEC.md 12.2's bar binds none of them, so
     * without os88_onkey dispatching them they fall into the C64's key ring
     * and the printed promise is not kept. Alt+F12, Alt+Insert and Alt+Delete
     * are §7.5's undeliverable three and stay menu-only. */
    {
        int w0 = c64_warp, p0 = c64_pause, j0 = c64_joyswap;
        os88_gfx_lock(); os88_onkey(0, 0x11, win); os88_gfx_unlock();  /* W */
        if (c64_warp == w0)
            fail("Alt+W did not toggle Warp mode - the caption is a promise");
        os88_gfx_lock(); os88_onkey(0, 0x19, win); os88_gfx_unlock();  /* P */
        if (c64_pause == p0)
            fail("Alt+P did not toggle Pause emulation");
        os88_gfx_lock(); os88_onkey(0, 0x24, win); os88_gfx_unlock();  /* J */
        if (c64_joyswap == j0)
            fail("Alt+J did not swap the joysticks");
        os88_gfx_lock(); os88_onkey(0, 0x11, win); os88_gfx_unlock();
        os88_gfx_lock(); os88_onkey(0, 0x19, win); os88_gfx_unlock();
        os88_gfx_lock(); os88_onkey(0, 0x24, win); os88_gfx_unlock();
        if (c64_warp != w0 || c64_pause != p0 || c64_joyswap != j0)
            fail("an Alt chord does not toggle back");
    }
    c64_msg[0] = 0;

    /* --- ALT+D WITH THE ABOUT PANEL UP: the hold rows must not be computed
     *     from a rect the geometry has moved under. os88_paint takes them
     *     from c64_abt_x/y/w/h, and OSAPI_FULLSCREEN repaints the window
     *     whole and SYNCHRONOUSLY - so that paint runs nested with the OLD
     *     rect still in those statics and the box under it already 640 wide.
     *     The rows the old rect covered are then held, skipped by the flush
     *     and redrawn by nobody (os88_onwake returns early on c64_abt), and
     *     the rows the new rect covers are composed and then painted over.
     *     os88_onkey handles Alt+D unconditionally, so the chord reaches it;
     *     so does Preferences > Fullscreen, because a menu click is not a
     *     W_ONCLICK and does not close the panel. ---------------------- */
    memset(glass, 0x55, sizeof(glass));
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    do_about(win);
    if (!c64_abt)
        fail("the About panel did not open for the Alt+D step");
    os88_gfx_lock();
    os88_onkey(0, 0x20, win);               /* Alt+D with the panel up */
    os88_gfx_unlock();
    if (c64_abt)
        fail("the About panel survived a geometry change - its rect is now "
             "stale and the hold rows are computed from it");
    audit("after Alt+D with the About panel up");
    if (fs_on) {
        os88_gfx_lock();
        os88_onkey(0, 0x20, win);
        os88_gfx_unlock();
    }
    audit("after coming back from the Alt+D panel step");

    /* --- THE ROM-LESS BANNER IS DRAWN ONCE, NOT ON EVERY FLUSH -----------
     * c64_flush's norom branch is 1 fill + 4 font_run over 124 glyph cells,
     * ~115 ms, and every menu command on a ROM-less machine answers through
     * c64_say - which keeps os88_onwake flushing and re-posting for the
     * message's whole ~5 seconds. Unconditional, that is ~45 repaints of a
     * notice that never changes. c64_sh_ok already means "we know what is on
     * the glass"; it is the gate. */
    c64_norom = 1;
    c64_sh_inval();
    c64_dirty_any = 1;
    flush_now();                            /* the banner goes up */
    cost_begin();
    c64_dirty_any = 1;
    flush_now();
    if (n_fill - b_fill != 0 || (n_run_cells - b_run_cells) != 0)
        fail("the ROM-less banner was repainted on a flush that changed "
             "nothing");
    c64_norom = 0;
    c64_st_ok = 0;
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    audit("after the ROM-less banner came down");

    /* --- File > Exit emulator: the lock contract, and spawn's 0 / -1 ----
     * os88_oncmd is dispatched UNDER the gfx lock, so ovl_cmd must not take
     * it - the stubs above make that an immediate exit rather than a hang -
     * and os88_task_spawn answers 0 for success, -1 for a refusal that is
     * NORMAL and transient. Both were wrong and both passed the first
     * version of this harness. */
    spawn_refuses = 1;
    do_cmd(C64_I_EXIT, C64_M_FILE, win);
    if (spawned != 0)
        fail("a refused task_spawn still counted as a spawn");
    if (c64_state == C64_ST_DEAD)
        fail("a REFUSED task_spawn latched C64_ST_DEAD - the window is not "
             "being closed by anybody and the machine is frozen");
    spawn_refuses = 0;
    do_cmd(C64_I_EXIT, C64_M_FILE, win);
    if (spawned != 1)
        fail("Exit emulator did not spawn the closer");
    if (c64_state != C64_ST_DEAD)
        fail("a SUCCESSFUL task_spawn left the machine running into a window "
             "the worker is destroying");

    /* ...AND THE WORKER IT SPAWNED MUST CALL os88_task_alive(). That call is
     * what closes the package: the kernel tears the instance down inside it,
     * freeing the task, the instance, the region and both claims (os88.h:622,
     * apps/runcpm/runcpm.c:956-965). Without it - which is what wave 1
     * shipped - File > Exit emulator destroyed the window and parked in a
     * sleep loop, leaving a dead dock tile that answered no click, 84KB of
     * claims and a task slot gone for the session; crt0.asm's net only fires
     * if os88_worker RETURNS, and one that loops never does. The destroy
     * needs the lock and task_alive forbids it, so the stubs check both. */
    sleep_calls = 0;
    if (setjmp(alive_out) == 0) {
        os88_worker(win);
        fail("os88_worker returned - it must never return (os88.h:388)");
    }
    if (!win_destroyed)
        fail("the worker did not destroy the window");
    if (alive_after_destroy == 0)
        fail("the worker never called os88_task_alive() after the destroy - "
             "the task, the instance and both claims leak and the dock tile "
             "is a corpse (os88.h:622)");
    if (lock_depth != 0)
        fail("the worker left the gfx lock held");

    c64_state = C64_ST_HALT;
    c64_msg[0] = 0;

    printf("c64uitest: dirty pages counted this run: %u\n", c64_n_dirtypg);
    if (fails) {
        printf("c64uitest: %d FAILURE(S)\n", fails);
        return 1;
    }
    printf("c64uitest: PASS\n");
    return 0;
}
