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

/* OSAPI_WM_CLOSE's two halves (SPEC.md 75.2), declared up here because plot()
 * is what enforces the second: `closed` counts the calls and `close_drew`
 * catches a package that draws AFTER asking to be closed. See os88_wm_close
 * below for the argument. */
static int closed, close_drew;

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
    if (x >= cov_x1 && x <= cov_x2 && y >= cov_y1 && y <= cov_y2) {
        /* THE COVERING WINDOW'S PIXEL STANDS EITHER WAY, and that is the
         * kernel: an ARMED clip region cuts the write in the kernel, and an
         * unarmed one means the write reached the glass and is a DEFECT. The
         * first version of this modelled only the second half - it let a
         * covered write through whenever a clip happened to be armed - so a
         * package that armed the clip and then drew outside the damage rect
         * painted over its neighbour in the model and the audit below read
         * those pixels as ours. Discard always; count only the illegal ones. */
        if (!clip_armed) {
            if (cov_hits < 3)
                printf("c64uitest:   a pixel at (%d,%d) landed on the window "
                       "COVERING us, with no clip armed (SPEC.md 11.3)\n",
                       x, y);
            cov_hits++;
        }
        return;
    }
    if (closed)
        close_drew = 1;                 /* 75.2: "call it and RETURN - do not
                                         * draw afterwards" */
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
static int scroll_dy;                   /* what the LAST scroll moved, so a
                                         * row can assert the dy and not only
                                         * the call count (9.8's 2x: k * 16) */
int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int x, y;
    need_lock("gfx_scroll");
    n_scroll++;
    scroll_dy = dy;                         /* count the CALL, not the success:
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

static char h_last_run[80];             /* the LAST text this row drew, so a
                                         * permanent status line can be read
                                         * back off the glass rather than
                                         * asserted about in the abstract */
static int h_last_run_x;                /* ...and WHERE it landed, which is
                                         * how a delta-drawn field is told
                                         * from the literal tail beside it:
                                         * `% cpu` and ` fps` never change and
                                         * a repaint that touches their cells
                                         * is the whole-field redraw coming
                                         * back (§9.7) */

/* ...AND EVERY RUN SINCE h_runs_clear(), BY THE x IT LANDED ON. One `last
 * run` stopped being enough the moment the status row drew more than one
 * thing per pass: a message now shares the row with the two LAMPS (§10.1), so
 * the last run on a jammed machine is `P` and not the jam line, and asking
 * `did the row say X` needs the run at X's own x. */
#define H_RUNMAX 24
static char h_run_txt[H_RUNMAX][80];
static int h_run_x[H_RUNMAX];
static int h_run_ink[H_RUNMAX];
static int h_nrun;

static void h_runs_clear(void) { h_nrun = 0; }

static const char *h_run_at(int x)      /* the run drawn at exactly x, or 0 */
{
    int i;
    for (i = h_nrun - 1; i >= 0; i--)
        if (h_run_x[i] == x)
            return h_run_txt[i];
    return 0;
}

static int h_run_ink_at(int x)          /* ...and its ink, which is how an
                                         * INVERTED cell - a lit lamp - is
                                         * told from an unlit one (§10.2) */
{
    int i;
    for (i = h_nrun - 1; i >= 0; i--)
        if (h_run_x[i] == x)
            return h_run_ink[i];
    return -1;
}
void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int i;
    need_lock("font_run");
    strncpy(h_last_run, s, sizeof(h_last_run) - 1);
    h_last_run[sizeof(h_last_run) - 1] = 0;
    h_last_run_x = x;
    if (h_nrun < H_RUNMAX) {
        strncpy(h_run_txt[h_nrun], s, sizeof(h_run_txt[0]) - 1);
        h_run_txt[h_nrun][sizeof(h_run_txt[0]) - 1] = 0;
        h_run_x[h_nrun] = x;
        h_run_ink[h_nrun] = ink;
        h_nrun++;
    }
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
/* os88_wm_close - OSAPI_WM_CLOSE (SPEC.md 75.2), the ONE door: the kernel's
 * own close path, the same one the close box takes, so app_close_win repaints
 * the dock strip and the tile goes. It is DEFERRED - it returns and the window
 * dies on the next UI pass - and its contract is "call it and RETURN: do not
 * draw afterwards, do not call it twice".
 *
 * BOTH HALVES ARE THE NEGATIVE CONTROL. `closed` counts, so a second call
 * fails here rather than closing a slot that a new tenant may already own;
 * and `close_drew` latches, so anything that draws after the call - which is
 * what putting the close in the middle of os88_oncmd does, since os88_oncmd's
 * tail kicks a wake that flushes - fails too. The lock may be held or not,
 * so unlike wm_destroy this asserts nothing about it. */
void os88_wm_close(void *win)
{
    if (closed++)
        fail("os88_wm_close called twice - the second names a slot the "
             "kernel may already have given to somebody else (75.2)");
    close_drew = 0;
}
/* OSAPI_FULLSCREEN REPAINTS YOU WHOLE, SYNCHRONOUSLY, IN BOTH DIRECTIONS
 * (os88.h:604; kernel/wm.inc:4147 - wm_fullscreen calls wm_raise with AL = 1,
 * so W_PAINT runs NESTED inside this call). Modelling it as `return 0` is
 * what let a c64_sh_inval() sit on the success arm of Preferences >
 * Fullscreen, throwing away a shadow the kernel had just made true and
 * drawing the identical 25 bands again on the next wake. */
static int fs_on;
static int fs_w = 640, fs_h = 440;      /* the DESKTOP a fullscreen window
                                         * fills, so a row can put the package
                                         * on a 640x480 VGA or a 640x200 CGA
                                         * and read 9.8's tier table off the
                                         * geometry it computes */
int  os88_fullscreen(void *win, int enter)
{
    fs_on = enter;
    geom_w = enter ? fs_w : GW;
    geom_h = enter ? fs_h : GH;
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
static long n_toast;
int  os88_toast(const char *t, int ticks)
{
    n_toast++;
    strncpy(last_toast, t, sizeof(last_toast) - 1);
    return 0;
}
static int h_cpu_tier = OS88_CPU_8086;   /* the target. A row sets it to say
                                          * what a FASTER machine is told */
int  os88_cpu(void) { return h_cpu_tier; }
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
 * ceil(size / 16) paragraphs, reads the file into it and moves it into the C64's RAM,
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
static unsigned h_rom_seg = SEG_ROM;    /* where os88_part_seg says the ROM is:
                                         * moved by the carve round trip below
                                         * (CC_ONMOVE_PARTS, C64-SPEC 3.1.1) */

/* ONE BIG CLAIM - drive 8's D64 (C64-SPEC 11.5) is 171KB, more than any
 * scratch slot: a claim of over 4,096 paragraphs lands here, and a segment
 * anywhere inside it is an address inside it, as on the machine */
#define H_BIGSEG 0xA000u
static unsigned char h_big[200 * 1024];
static unsigned h_big_paras;
/* THE COLOUR BRACKET'S GLASS (C64-SPEC 9.12): a real 320x200 framebuffer at
 * a fictional segment, so what c64_fsx_put wrote can be read back */
#define H_FSX_SEG 0xE000u                   /* (not H_BIGSEG's 0xA000) */
static unsigned char h_fsx_fb[320 * 200];
static int h_fsx_mask;                       /* no FSXM_VGA13 unless a row says */
static int h_fsx_in, h_fsx_runs, h_fsx_modes, h_fsx_border = -1;
static long h_fsx_drawn;                     /* drawing calls INSIDE the entry */
static int h_fsx_keyq[16];
static int h_fsx_keyn, h_fsx_keyi;

static unsigned char *segbase(unsigned seg)
{
    int i;
    if (seg == H_FSX_SEG) return h_fsx_fb;
    if (h_big_paras && seg >= H_BIGSEG && seg < H_BIGSEG + h_big_paras)
        return h_big + (long)(seg - H_BIGSEG) * 16;
    if (seg == SEG_RAM) return ram;
    if (seg == h_rom_seg) return rom;
    for (i = 0; i < CLAIM_MAX; i++)
        if (claim[i].live && claim[i].seg == seg)
            return scratch[i];
    return 0;
}

int os88_fsx_caps(void *win, int *kind)
{
    (void)win;
    if (kind)
        *kind = 0;
    return h_fsx_mask;
}

static long h_draws(void)
{
    return (long)n_blit + n_fill + n_scroll + n_run + n_str + n_toast;
}
int os88_fsx_run(void (*entry)(void), void *win, int flags)
{
    long d0;

    (void)flags;
    if (h_fsx_in)
        return -1;
    h_fsx_runs++;
    h_fsx_in = 1;
    d0 = h_draws();
    entry();
    h_fsx_drawn = h_draws() - d0;
    h_fsx_in = 0;
    if (win)
        os88_paint(win);                /* SPEC.md 53.6 step 4: the desktop
                                         * repainted, nested, before the
                                         * return - the lock is the caller's */
    return 0;
}

int os88_fsx_mode(int id, void *fsi)
{
    unsigned char *b = (unsigned char *)fsi;

    if (!h_fsx_in || !(h_fsx_mask & (1 << id)))
        return -1;
    h_fsx_modes++;
    memset(h_fsx_fb, 0, sizeof(h_fsx_fb));
    memset(b, 0, OS88_FSI_SIZE);
    b[OS88_FSI_SEG] = (unsigned char)(H_FSX_SEG & 0xFF);
    b[OS88_FSI_SEG + 1] = (unsigned char)(H_FSX_SEG >> 8);
    b[OS88_FSI_STRIDE] = 320 & 0xFF;
    b[OS88_FSI_STRIDE + 1] = 320 >> 8;
    b[OS88_FSI_BPP] = 8;
    b[OS88_FSI_MODE] = (unsigned char)id;
    return 0;
}

int os88_fsx_wait(int kind)
{
    (void)kind;
    if (!h_fsx_in)
        return -1;
    the_ticks++;                        /* a frame is a tick */
    return 0;
}

/* c64fsx.inc's routines, in C (C64-SPEC 9.12); c64memtest runs the shipping
 * ones on a real x86 */
void c64_c8glyph(unsigned char *gb, const unsigned char *xm, int n,
                 unsigned gseg, unsigned goff, int mask)
{
    const unsigned char *g = segbase(gseg);
    int c, y;
    for (c = 0; c < n; c++)
        for (y = 0; y < 8; y++)
            gb[c * 8 + y] = g[(goff + (xm[c] & mask) * 8 + y) & 0xFFFF];
}
void c64_c8row(unsigned char *dst, const unsigned char *gb,
               const unsigned char *k0, const unsigned char *k1,
               const unsigned char *k2, const unsigned char *k3,
               const unsigned char *mc, int n)
{
    int c, y, x;
    for (c = 0; c < n; c++)
        for (y = 0; y < 8; y++) {
            unsigned b = gb[c * 8 + y];
            unsigned char *d = dst + y * 320 + c * 8;
            for (x = 0; x < 8; x++) {
                if (!mc[c]) {
                    d[x] = ((b << x) & 0x80) ? k1[c] : k0[c];
                } else {
                    unsigned p = (b >> (6 - (x & 6))) & 3;
                    d[x] = p == 0 ? k0[c] : p == 1 ? k1[c] : p == 2 ? k2[c] : k3[c];
                }
            }
        }
}
void c64_c8spr(unsigned char *dst, const unsigned char *data, int x,
               int lo, int hi, const unsigned char *cols, int mc, int xs)
{
    int b, k, w = xs ? 2 : 1;
    for (b = 0; b < 24; b++) {
        unsigned byte = data[b >> 3];
        int c = -1;
        if (!mc) {
            if ((byte << (b & 7)) & 0x80)
                c = cols[2];
        } else {
            unsigned p = (byte >> (6 - (b & 6))) & 3;
            if (p)
                c = cols[p];
        }
        for (k = 0; k < w; k++, x++)
            if (c >= 0 && x >= lo && x < hi)
                dst[x] = (unsigned char)c;
    }
}
void c64_fsx_put(unsigned seg, unsigned off, const unsigned char *src, int n)
{
    memcpy(segbase(seg) + off, src, n);
}
unsigned c64_fsx_key(void)
{
    if (h_fsx_keyi >= h_fsx_keyn)
        return 0xFFFF;
    return (unsigned)(h_fsx_keyq[h_fsx_keyi++] & 0xFFFF);
}
void c64_fsx_dac(void) { }
void c64_fsx_border(int c) { h_fsx_border = c & 15; }

static long segcap(unsigned seg)
{
    int i;
    if (h_big_paras && seg >= H_BIGSEG && seg < H_BIGSEG + h_big_paras)
        return (long)(H_BIGSEG + h_big_paras - seg) * 16;
    if (seg == SEG_RAM) return 65536;
    if (seg == h_rom_seg) return 20480;
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

/* ...and the paragraph door (SPEC.md 50.3.7), which the .PRG attach uses:
 * the capacity is EXACTLY what was asked for, no granule over it, so a read
 * past the request fails here even where the kernel's rounding would hide it */
unsigned os88_mem_claim_p(unsigned paras)
{
    int i;
    if (paras == 0)
        return 0;
    if (paras > 4096) {
        if (h_big_paras || (long)paras * 16 > (long)sizeof(h_big))
            return 0;
        h_big_paras = paras;
        claims_live++;
        return H_BIGSEG;
    }
    for (i = 0; i < CLAIM_MAX; i++)
        if (!claim[i].live) {
            claim[i].live = 1;
            claim[i].seg = (unsigned)(0x4000 + i * 0x400);
            claim[i].cap = (long)paras * 16;
            claims_live++;
            return claim[i].seg;
        }
    return 0;
}

int os88_mem_free(unsigned seg)
{
    int i;
    if (seg == SEG_RAM) { ram_live = 0; return 0; }
    if (seg == SEG_ROM) { rom_live = 0; return 0; }
    if (h_big_paras && seg == H_BIGSEG) { h_big_paras = 0; claims_live--; return 0; }
    for (i = 0; i < CLAIM_MAX; i++)
        if (claim[i].live && claim[i].seg == seg) {
            claim[i].live = 0;
            claims_live--;
            return 0;
        }
    fail("os88_mem_free of a segment that is not a live claim");
    return -1;
}

/* os88_mem_movable - the record says so; the harness never compacts. What is
 * checked is WHO declares WHAT, and WHEN (C64-SPEC 3.1.1): the RAM once
 * os88_main has every copy of its segment, Paste's queue once it is filled */
static int ram_movable, n_movable;
static unsigned last_movable;
int os88_mem_movable(unsigned seg, int on)
{
    if (segcap(seg) < 0)
        fail("os88_mem_movable of a segment that is not a live claim");
    if (seg == SEG_RAM)
        ram_movable = on;
    if (on)
        last_movable = seg;
    n_movable++;
    return 0;
}

unsigned os88_mem_largest_kb(void) { return 200; }
int  os88_peek(unsigned seg, unsigned off) { return segbase(seg)[off]; }
void os88_poke(unsigned seg, unsigned off, int v) { segbase(seg)[off] = (unsigned char)v; }

/* drive 8's image fixture: what c64_read_at serves, by offset, 32KB a call
 * (C64-SPEC 11.5) - and the call's contract checked as the kernel holds it */
static unsigned char h_d64[174848];
static long h_d64_len;
unsigned c64_read_at(const char *name, unsigned seg, unsigned cap,
                     unsigned off_lo, unsigned off_hi)
{
    long off = (long)off_lo | ((long)off_hi << 16), n;
    (void)name;
    if (cap % 512 || off % 512)
        fail("c64_read_at off a cluster boundary");
    if (segcap(seg) < (long)cap && off + cap < h_d64_len)
        fail("c64_read_at into a claim smaller than its capacity");
    if (off >= h_d64_len)
        return 0;
    n = h_d64_len - off;
    if (n > cap)
        n = cap;
    memcpy(segbase(seg), h_d64 + off, (size_t)n);
    return (unsigned)n;
}

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
    return 0;                           /* no such file: a normal refusal.
                                         * C64.ROM used to be served here and
                                         * is a PART now (1.4) - see
                                         * os88_part_seg below */
}

/* --- THE PARTS STANDARD, on the host (SPEC.md 20.12) ----------------------
 * On the guest, crt0 calls op_load before os88_main: the 20KB is claimed and
 * the ROM is read into it, or the launch is refused before a sector is spent.
 * Here that is one segment holding the same bytes, loaded on the first ask,
 * out of the same build/c64-rom/C64.ROM the packer appends to the package.
 *
 * There is no `the part is missing` case to model, and that IS the change
 * wave 6 made: a required part is here when os88_main runs or the launch
 * never got that far. `--no-rom`, a whole second process that existed to draw
 * the notice a mis-copied disk showed, went with it. */
/* THE FAST CORE'S PART IS REFUSED HERE (C64-SPEC section 4.7): this host has
 * no 8086 to run it, and a refused fetch is the path that leaves the
 * accurate core's stub - the one every case below drives - running alone */
void c64f_core(unsigned dstseg, unsigned partseg) { (void)dstseg; (void)partseg; }
unsigned c64f_codesel(unsigned seg) { return seg; }
void c64j_off(unsigned core) { (void)core; }
void c64j_setup(unsigned dst, unsigned part, unsigned len, unsigned core, int force, int kb) { (void)dst; (void)part; (void)len; (void)core; (void)force; (void)kb; }

unsigned os88_part_seg(int i)
{
    FILE *f;
    if (i != 0)
        return 0;
    if (!rom_live) {
        f = fopen("build/c64-rom/C64.ROM", "rb");
        if (!f) {
            printf("c64uitest: build/c64-rom/C64.ROM is missing - "
                   "run `python3 tools/c64rom.py` first\n");
            exit(2);
        }
        if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
            printf("c64uitest: build/c64-rom/C64.ROM is short\n");
            exit(2);
        }
        fclose(f);
        rom_live = 1;
    }
    return h_rom_seg;
}

int os88_part_fetch(int i) { return i == 2 ? -1 : 0; }  /* part 2 is the fast
                                             * core, refused (C64-SPEC 4.7) */
void os88_part_drop(int i) { (void)i; }
int os88_part_lazyok(int i) { (void)i; return 1; }
int os88_part_optok(void) { return 1; }

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

static long n_ram_rd;                   /* Edit > Copy's whole cost is this
                                         * count: it draws nothing at all */
int c64_rd(unsigned a) { n_ram_rd++; return ram[a & 0xFFFF]; }
int c64_rd16(unsigned a) { return ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8); }
int c64_rom_rd(unsigned off) { return rom[off % 20480]; }
/* EVERY SCRATCH ACCESS IS COUNTED, because the flush's cost is not only what
 * it draws. c64_scr_rd / c64_scr_wr are C-callable NEAR THUNKS into the
 * emulated machine's memory (c64mem.inc): ten instructions and a segment load
 * each, ~38 us on the target, and c64_dirty_scan used to make about fifty of
 * them per flush - ~1.8 ms, comparable to the WHOLE of §9.7's `one changed
 * cell` row, and reported as 0.0 ms by a model that counted gfx calls only.
 * cost_row prices them now, and c64_dirty_take is the one call that replaced
 * the fifty. */
static long n_scr_acc, n_scr_take;

int c64_scr_rd(unsigned off) { n_scr_acc++; return ram[H_SCR_BASE + off]; }
void c64_scr_wr(unsigned off, int v)
{ n_scr_acc++; ram[H_SCR_BASE + off] = (unsigned char)v; }

/* c64mem.inc's _c64_dirty_take: the 32-byte page bitmap and the write
 * window's two words into the package's own memory, and the scratch reset in
 * the same pass. The layout IS the contract - dst[0..31] bitmap,
 * dst[32..35] the window - and hosttest/c64memtest.asm runs the shipping
 * assembly against it on a real x86 with SS != DS. */
void c64_dirty_take(unsigned char *dst)
{
    int i;
    n_scr_take++;
    for (i = 0; i < 32; i++) {
        dst[i] = ram[H_SCR_BASE + 0x00 + i];
        ram[H_SCR_BASE + 0x00 + i] = 0;
    }
    dst[32] = ram[H_SCR_BASE + 0x2C];
    dst[33] = ram[H_SCR_BASE + 0x2D];
    dst[34] = ram[H_SCR_BASE + 0x2E];
    dst[35] = ram[H_SCR_BASE + 0x2F];
    ram[H_SCR_BASE + 0x2C] = 0xFF;
    ram[H_SCR_BASE + 0x2D] = 0xFF;
    ram[H_SCR_BASE + 0x2E] = 0;
    ram[H_SCR_BASE + 0x2F] = 0;
    ram[H_SCR_BASE + 0x30] = 0;         /* ANY: this IS the read of it */
}

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

/* ...AND THE BYTES THE PASTE FEEDER TYPES, because the conversion is per BYTE
 * and lives there now (C64-SPEC §7.7). $0277 has exactly one writer in this
 * program - c64_paste_feed - so this counts the loop that converts, and
 * cost_row prices it. */
static long n_kbbuf;

void c64_wr(unsigned a, int v)
{
    a &= 0xFFFF;
    if (a >= 0x0277 && a < 0x0277 + 10)
        n_kbbuf++;
    if (a >= H_SCR_BASE && a < H_SCR_BASE + H_SCR_END)
        return;                             /* the scratch is not RAM (3.5) */
    ram[a] = (unsigned char)v;
    h_window(a);
    h_dirtybit(a);
}

void c64_dirty(unsigned a) { h_dirtybit(a & 0xFFFF); }

void c64_zcopy_in(unsigned a, const void *src, unsigned n)
{ memmove(ram + (a & 0xFFFF), src, n); }
/* ...AND EVERY BYTE OUT IS COUNTED, because Edit > Copy's whole cost is this
 * call and the loop that walks what it brought back. c64_copy_screen is the
 * only caller (C64-SPEC §7.7), so `bytes` IS the number of cells converted
 * and cost_row prices the conversion from it. */
static long n_zcopy_out, n_zcopy_bytes;
void c64_zcopy_out(void *dst, unsigned a, unsigned n)
{ n_zcopy_out++; n_zcopy_bytes += n; memmove(dst, ram + (a & 0xFFFF), n); }
void c64_zzcopy_in(unsigned a, unsigned sseg, unsigned soff, unsigned n)
{ memmove(ram + (a & 0xFFFF), segbase(sseg) + soff, n); }
void c64_zfill(unsigned a, int v, unsigned n)
{ memset(ram + (a & 0xFFFF), v, n); }


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

/* c64band.inc's c64_bandbm, in C (C64-SPEC 9.10); c64memtest runs the
 * shipping one on a real x86 */
extern unsigned char c64_dmask[16][4];
static long n_bm_cells;
void c64_bandbm(unsigned char *dst, const unsigned char *xm,
                const unsigned char *xb, const unsigned char *col,
                int n, int mc, int bg)
{
    int c, y;
    for (c = 0; c < n; c++) {
        const unsigned char *d0 = c64_dmask[bg & 15];
        const unsigned char *d1 = c64_dmask[xm[c] >> 4];
        const unsigned char *d2 = c64_dmask[xm[c] & 15];
        const unsigned char *d3 = c64_dmask[col[c] & 15];
        n_bm_cells++;
        for (y = 0; y < 8; y++) {
            unsigned b = xb[c * 8 + y], o = 0;
            int k;
            if (!mc)
                o = (b & d1[y & 3]) | (~b & d2[y & 3]);
            else
                for (k = 0; k < 4; k++) {
                    unsigned p = (b >> (6 - 2 * k)) & 3;
                    const unsigned char *d = p == 0 ? d0 : p == 1 ? d1 : p == 2 ? d2 : d3;
                    o |= d[y & 3] & (0xC0 >> (2 * k));
                }
            dst[y * HB_STRIDE + c] = (unsigned char)o;
        }
    }
}

int c64_rvdiff(const unsigned char *a, const unsigned char *b)
{ return memcmp(a, b, 48) != 0; }
int c64_neq(const unsigned char *a, const unsigned char *b, int n)
{ return memcmp(a, b, (size_t)n * 2) != 0; }
/* c64band.inc's c64_bandfind, in C (C64-SPEC 9.4.1) */
int c64_bfbest;
extern int c64_sb0, c64_sb1;
int c64_bandfind(const unsigned *sig, const unsigned *shsig, int n, int kmax)
{
    int k, i, j, run, ch, st = 0, bk = 0;
    c64_bfbest = 0;
    for (k = -kmax; k <= kmax; k++) {
        if (k == 0)
            continue;
        run = ch = 0;
        for (i = 0; i <= n; i++) {
            j = i + k;
            if (i < n && j >= 0 && j < n && sig[i] == shsig[j]) {
                if (!run) st = i;
                run++;
                if (sig[i] != shsig[i]) ch++;
                continue;
            }
            if (ch > c64_bfbest) {
                c64_bfbest = ch; bk = k; c64_sb0 = st; c64_sb1 = st + run - 1;
            }
            run = ch = 0;
        }
    }
    return bk;
}
unsigned c64_scr_left(void)
{
    /* (DEAD $24, CARRY $22: c64.c's C64_SCR_* are below this) */
    return (unsigned)((c64_scr_rd(0x24) | (c64_scr_rd(0x25) << 8))
                    + (c64_scr_rd(0x22) | (c64_scr_rd(0x23) << 8))) & 0xFFFF;
}

/* c64sid.inc's c64_osc3, in C with host 32-bit arithmetic (C64-SPEC
 * 11.4.1); c64memtest runs the shipping one on a real x86 */
static unsigned long h_o3acc = 0, h_o3nz = 0;
unsigned c64_osc3(unsigned dt, unsigned freq, unsigned ctrl, unsigned pw)
{
    unsigned long add, o, n, r;
    unsigned out = 0xFF, b;
    if (ctrl & 8) {
        h_o3acc = 0;
        h_o3nz = 0x7FFFFF;
    } else {
        add = (unsigned long)(dt & 0xFFFF) * (freq & 0xFFFF);
        if (add > 0x0FFFFFFFUL) add = 0x0FFFFFFFUL;
        o = h_o3acc + 0xF80000UL;
        n = o + add;
        r = (n >> 20) - (o >> 20);
        if (r > 256) r = 256;
        h_o3acc = (n - 0xF80000UL) & 0xFFFFFF;
        while (r--) {
            b = ((h_o3nz >> 22) ^ (h_o3nz >> 17)) & 1;
            h_o3nz = ((h_o3nz << 1) | b) & 0x7FFFFF;
        }
    }
    if ((ctrl & 0xF0) == 0)
        return 0;
    if (ctrl & 0x80) {
        unsigned long z = h_o3nz;
        b = (unsigned)((((z >> 22) & 1) << 7) | (((z >> 20) & 1) << 6)
            | (((z >> 16) & 1) << 5) | (((z >> 13) & 1) << 4)
            | (((z >> 11) & 1) << 3) | (((z >> 7) & 1) << 2)
            | (((z >> 4) & 1) << 1) | ((z >> 2) & 1));
        out &= b;
    }
    if (ctrl & 0x20)
        out &= (unsigned)(h_o3acc >> 16) & 0xFF;
    if (ctrl & 0x10) {
        b = (unsigned)(h_o3acc >> 15) & 0xFF;
        if (h_o3acc & 0x800000) b ^= 0xFF;
        out &= b;
    }
    if (ctrl & 0x40)
        out &= ((unsigned)(h_o3acc >> 12) & 0xFFF) >= (pw & 0xFFF) ? 0xFF : 0;
    return out;
}

void c64_sprrow(unsigned char *dst, const unsigned char *on,
                const unsigned char *lit, int n, int cell, int sh, int f, int l)
{
    int k;
    for (k = 0; k < n; k++, cell++) {
        unsigned o = (unsigned)on[k] << 8, t = (unsigned)lit[k] << 8;
        o >>= sh;
        t >>= sh;
        if (cell >= f && cell <= l)
            dst[cell] = (unsigned char)((dst[cell] & ~(o >> 8)) | (t >> 8));
        if (cell + 1 >= f && cell + 1 <= l)
            dst[cell + 1] = (unsigned char)((dst[cell + 1] & ~o) | t);
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

static long n_x2_calls;                 /* ...and 9.8's 2x, which is the one
                                         * thing in the flush that costs more
                                         * than the compare it follows:
                                         * C64BENCH_X2 is 20.19 ms for eight
                                         * rows on tests/c64band, WHATEVER the
                                         * span, because the doubler takes
                                         * whole 40-byte rows */
void c64_band_x2(unsigned char *dst, const unsigned char *src, int rows)
{
    n_x2_calls++;
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

/* c64_copy_row (c64mem.inc) - Edit > Copy's per-BYTE loop, in assembly on the
 * target and transcribed here. It is the same three steps clipboard.c:41-100
 * takes: the table index with the reverse-video bit masked off (charset.c:204),
 * last_non_whitespace (:67), the trim (:81-85) and one '\n'. c64memtest.asm
 * runs the REAL one on a real x86 with SS != DS; this models it. */
static long n_cprow, n_cpcell;
int c64_copy_row(unsigned dseg, unsigned doff, int n)
{
    unsigned char *d = segbase(dseg);
    int i, last = 0, c;
    n_cprow++;
    n_cpcell += n;                      /* THE CONVERSION HAS ITS OWN COUNTER
                                         * NOW. It used to be derived from the
                                         * BYTES c64_zcopy_out moved, which was
                                         * right only because c64_copy_screen
                                         * was its one caller and moved one
                                         * byte per converted cell. The mover
                                         * is general - wave 4's bitmap rows
                                         * and sprite fetches are exactly the
                                         * shape that will use it - and a
                                         * 320-byte pull would have been priced
                                         * at 320 cells of conversion that does
                                         * not exist. */
    for (i = 0; i < n; i++) {
        c = c64_sctab[c64_scrow[i] & 0x7F];
        d[doff + i] = (unsigned char)c;
        if (c != ' ')
            last = i + 1;
    }
    d[doff + last] = '\n';
    return last + 1;
}

/* c64cpu.inc's register file, which the C declares extern (4.1). It is
 * defined AFTER the include because the struct is c64.c's. */
struct c64_mach c64_m;

/* ==========================================================================
 * THE SCRIPTED CORE (C64-SPEC §14.5)
 *
 * c64cpu.inc is 8086 and cannot run here; what it OWES the C is a contract -
 * "spend at most `cycles`, leave what you did NOT spend in c64_m.cnt, answer
 * SLICE or JAM" - and this is that contract, scriptable, so the slice driver,
 * the alarm model, the two-word counters and the adaptation are driven rather
 * than reasoned about. The real core's own nine-row gate is
 * hosttest/c64cputest.sh, on a real x86 (§4.6).
 * ========================================================================*/
#define H_CPU_IDLE  0                       /* spend the budget, write nothing */
#define H_CPU_EARLY 1                       /* end after h_cpu_early cycles */
#define H_CPU_JAM   2
static int h_cpu_mode = H_CPU_IDLE;
static int h_slow_wake;                     /* host ticks the scripted core
                                             * burns per call, so a test can
                                             * make ONE slice span two tick
                                             * boundaries - the halving arm of
                                             * the adaptation (4.4) */
static int h_cpu_early = 1;
static unsigned h_cpu_ran;                  /* cycles this core has delivered */
static int h_booted;

static void h_boot_screen(void);

int c64_run(unsigned cycles)
{
    int n = (int)cycles;

    n_cpu++;
    the_ticks += (unsigned)h_slow_wake;
    if (!h_booted) {
        h_booted = 1;
        h_boot_screen();                    /* what the KERNAL writes first */
    }
    if (h_cpu_mode == H_CPU_JAM) {
        c64_m.cnt = 0;
        return 1;
    }
    if (h_cpu_mode == H_CPU_EARLY && h_cpu_early < n) {
        c64_m.cnt = (unsigned)(n - h_cpu_early);
        h_cpu_ran += (unsigned)h_cpu_early;
        return 0;
    }
    c64_m.cnt = 0;
    h_cpu_ran += (unsigned)n;
    return 0;
}

/* the banked reader, the cut and the re-bank. The vector table lives in the
 * modelled ROM image, which is what makes c64_reset_cpu's $FFFC read real. */
int c64_bread(unsigned a)
{
    a &= 0xFFFF;
    if (a >= 0xE000 && ram[H_SCR_BASE - 1] == ram[H_SCR_BASE - 1]) {
        /* the KERNAL is mapped in every state this harness reaches */
        int m = ram[H_SCR_BASE + 0x38];
        if (m == 2)
            return rom[a - 0xE000];
    }
    if (a >= 0xA000 && a < 0xC000 && ram[H_SCR_BASE + 0x36] == 1)
        return rom[0x2000 + (a - 0xA000)];
    if (a >= 0xD000 && a < 0xE000) {
        int m = ram[H_SCR_BASE + 0x37];
        if (m == 4)
            return c64_io_rd(a);
        if (m == 3)
            return rom[0x4000 + (a - 0xD000)];
    }
    if (a < 2)
        return c64_io_rd(a);
    return ram[a];
}

static int n_cut;
void c64_cut(void) { n_cut++; }
static int n_rebank;
void c64_rebank(void) { n_rebank++; }

unsigned c64_muldiv(unsigned a, unsigned b, unsigned d)
{
    unsigned long v;
    if (d == 0)
        return 0xFFFFu;
    v = ((unsigned long)a * b) / d;
    if (v > 0xFFFFu)
        return 0xFFFFu;
    return (unsigned)v;
}

unsigned c64_div32(unsigned hi, unsigned lo, unsigned d)
{
    unsigned long v = ((unsigned long)hi << 16) | lo;
    if (d == 0)
        return 0xFFFFu;
    v = v / d;
    if (v > 0xFFFFu)
        return 0xFFFFu;
    return (unsigned)v;
}

/* ==========================================================================
 * OSAPI_KEY_DOWN, MODELLED (C64-SPEC §7.2)
 *
 * The kernel's map: ASKING IS WHAT ARMS IT, and the first ask clears it and
 * answers "up". A stub that always answers 0 would measure nothing, so this
 * one has a state a test sets and an ARMED latch a test can check - because
 * arming it from the first slice instead of from os88_main is exactly the
 * defect rule 1 exists to prevent, and it is invisible on any glass.
 * ========================================================================*/
static unsigned char h_keydown[128];
static int h_key_armed;
static int h_key_asks;
static int h_key_first_in_main;
static int h_in_main;

int os88_key_down(int scan)
{
    h_key_asks++;
    if (!h_key_armed) {
        h_key_armed = 1;
        h_key_first_in_main = h_in_main;
        memset(h_keydown, 0, sizeof(h_keydown));
        return 0;                           /* the first ask always says up */
    }
    if (scan < 0 || scan > 127)
        return 0;
    return h_keydown[scan] ? 1 : 0;
}

/* ...AND THE SPEAKER CAN REFUSE, WHICH IS THE WHOLE POINT OF THE STUB.
 * os88_snd_tone answers -1 when another instance holds the grant (os88.h,
 * SPEC.md 34.3), and a stub that only ever grants measures the success path:
 * the package discarded all three returns and one refusal silenced the
 * emulated SID for the rest of the session, because the latch that asks is
 * raised only by a SID REGISTER WRITE. */
static int n_tone, h_tone_hz, h_tone_refuse;
/* SOUND.DRV's FM chip (C64-SPEC 11.4.3): c64.asm's two shim calls, a chip
 * the row says is there, and a register file the batches land in */
static int h_fm_chip;
static unsigned char h_opl[256];
static int h_opl_batches;
int c64_opl_open(void) { return h_fm_chip; }
int c64_opl_send(const unsigned char *pairs, int n)
{
    int i;
    h_opl_batches++;
    for (i = 0; i < n; i++)
        h_opl[pairs[i * 2]] = pairs[i * 2 + 1];
    return n;
}
int os88_snd_tone(int hz, int ticks, int prio)
{
    n_tone++;
    if (h_tone_refuse)
        return -1;
    h_tone_hz = hz;
    return 0;
}

/* ...AND THE CAPABILITY IS A SEPARATE QUESTION FROM THE GRANT (11.4). Every
 * machine this OS boots has SND_CAP_TONE (kernel/snd.inc:804 ORs it in
 * unconditionally), so the package's guard is unreachable on real hardware
 * and reachable here: h_caps is what drives the row that shows the program
 * NOT calling a slot whose capability the machine does not claim. */
#define H_SND_CAP_TONE 0x01
static int h_caps = H_SND_CAP_TONE;
static int h_caps_asks;
int os88_snd_caps(void)
{
    h_caps_asks++;
    return h_caps;
}

/* ==========================================================================
 * THE SYSTEM CLIPBOARD (SPEC.md 55), for Edit > Copy and Edit > Paste (7.7)
 *
 * One buffer for the whole machine, outliving the app that filled it - so the
 * stub is one buffer too. Both directions can REFUSE, which is what makes the
 * package's refusal paths reachable: clip_put answers -1 over CLIP_MAXKB or
 * when the heap cannot fund it, and leaves the clipboard EMPTY rather than
 * stale (kernel/clip.inc:84).
 * ========================================================================*/
#define H_CLIP_MAX (32 * 1024)
static char h_clip[H_CLIP_MAX];
static int h_clip_n = -1;               /* -1 = empty, as clip_size answers */
static int h_clip_refuse;
static int n_clip_put, n_clip_get;
static long n_clip_put_bytes;           /* ...and what kernel/clip.inc:70-125
                                         * actually moves, which is NOT free
                                         * and happens under the caller's lock
                                         * (C64-SPEC §7.7) */

int os88_clip_put(const void *text, unsigned len)
{
    n_clip_put++;
    n_clip_put_bytes += (long)len;
    if (h_clip_refuse || len > H_CLIP_MAX) {
        h_clip_n = -1;                  /* empty, not stale */
        return -1;
    }
    if (len == 0) {
        h_clip_n = -1;
        return 0;
    }
    memcpy(h_clip, text, len);
    h_clip_n = (int)len;
    return 0;
}

/* THE ANSWER IS AX AND THE PACKAGE'S int IS 16 BITS, so the cast is the
 * stub modelling the machine (LESSONS.md 7: a stub that does not model the
 * real thing measures the wrong path). kernel/clip.inc:84 refuses only what
 * is strictly ABOVE CLIP_MAXKB * 1024, so a clipboard of exactly 32,768 is
 * legal and comes back to the package as 0x8000 - which its 16-bit int reads
 * as -32,768. The host's int is 32 bits and cannot reproduce that on its own,
 * so the one arithmetic the target does would be untestable without this. */
int os88_clip_size(void) { return (int)(short)h_clip_n; }

static long n_clip_get_bytes;           /* ...and what a Paste has to convert */

/* The _seg forms (os88.h): the text lives in a HEAP CLAIM. They are what
 * keeps 3,074 bytes of staging out of the package's bss, and the negative
 * control is the claim's own CAPACITY - segbase()/segcap() model real claims,
 * so a put or a get that runs off the end of a 2KB claim fails HERE instead of
 * writing over the next claim on a real machine. A segment that is not live
 * is a failure too: that is the use-after-free this whole scheme risks. */
static unsigned char *seg_or_fail(unsigned seg, unsigned off, unsigned n,
                                  const char *who)
{
    unsigned char *b = segbase(seg);
    long cap = segcap(seg);
    if (b == 0 || cap < 0) {
        fail("a clipboard _seg call named a segment that is not a live claim");
        return 0;
    }
    if ((long)off + (long)n > cap) {
        printf("c64uitest:   %s(seg %04X, off %u, n %u) runs past the "
               "claim's %ld bytes\n", who, seg, off, n, cap);
        fail("a clipboard _seg call ran off the end of its claim");
        return 0;
    }
    return b + off;
}

int os88_clip_put_seg(unsigned seg, unsigned off, unsigned len)
{
    unsigned char *p = seg_or_fail(seg, off, len, "clip_put_seg");
    n_clip_put++;
    n_clip_put_bytes += (long)len;
    if (p == 0)
        return -1;
    if (h_clip_refuse || len > H_CLIP_MAX) {
        h_clip_n = -1;                  /* empty, not stale */
        return -1;
    }
    if (len == 0) {
        h_clip_n = -1;
        return 0;
    }
    memcpy(h_clip, p, len);
    h_clip_n = (int)len;
    return 0;
}

int os88_clip_get_seg(unsigned seg, unsigned off, unsigned cap)
{
    unsigned char *p;
    unsigned n;
    n_clip_get++;
    if (h_clip_n <= 0)
        return 0;
    n = (unsigned)h_clip_n;
    if (n > cap)
        n = cap;
    p = seg_or_fail(seg, off, n, "clip_get_seg");
    if (p == 0)
        return 0;
    memcpy(p, h_clip, n);
    n_clip_get_bytes += n;
    return (int)(short)n;
}

int os88_clip_get(void *buf, unsigned cap)
{
    unsigned n;
    n_clip_get++;
    if (h_clip_n <= 0)
        return 0;
    n = (unsigned)h_clip_n;
    if (n > cap)
        n = cap;                        /* what FITS, which is why the package
                                         * asks clip_size first (os88.h:787) */
    memcpy(buf, h_clip, n);
    n_clip_get_bytes += n;
    return (int)(short)n;               /* CX, in the package's 16-bit int */
}

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
static unsigned b_proved;
static int b_blit, b_fill, b_scroll, b_frame, b_run, b_str, b_bandc, b_cells,
           b_span, b_sig;
static long b_blit_bytes, b_run_cells, b_str_cells, b_scr_acc, b_scr_take;
static long b_zc, b_zcb, b_psb, b_kb;

/* THE OVERLAY BOUNDARY, WHICH THIS HARNESS COMPILES FLAT AND SO CANNOT COUNT.
 * A shipped `ovl_*` is far-called both ways through crt0.asm's cc_ovthunk and
 * every resident thing it calls goes back out through a shim, so one bridge
 * crossing is PERFORMANCE.md's 46.7 us far call plus the 11 us near call the
 * shim makes - C64COST_OVLCALL in c64scr.c. It is charged per row, from the
 * STRUCTURE of the command, and the count is written down beside the row that
 * charges it. Without this term the next ovl_* that loops gets priced at zero
 * the way Edit > Copy's first draft was: 2,000 crossings reported as 1,000
 * near calls. */
static long n_ovlx, b_ovlx;
static long b_cpb, b_cprow, b_cpcell, b_x2;

static void cost_begin(void)
{
    b_blit = n_blit; b_fill = n_fill; b_scroll = n_scroll; b_frame = n_frame;
    b_proved = c64_n_proved;
    b_run = n_run; b_str = n_str;
    b_bandc = n_band_calls; b_cells = n_band_cells;
    b_span = n_span_calls; b_sig = n_sig_calls;
    b_blit_bytes = n_blit_bytes;
    b_run_cells = n_run_cells; b_str_cells = n_str_cells;
    b_scr_acc = n_scr_acc; b_scr_take = n_scr_take;
    b_zc = n_zcopy_out; b_zcb = n_zcopy_bytes; b_psb = n_clip_get_bytes;
    b_kb = n_kbbuf; b_ovlx = n_ovlx; b_cpb = n_clip_put_bytes;
    b_cprow = n_cprow; b_cpcell = n_cpcell; b_x2 = n_x2_calls;
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
       + (long)(n_sig_calls - b_sig) * C64BENCH_SIG
    /* ...AND THE SCRATCH, WHICH IS NOT DRAWING AND IS NOT FREE. These two are
     * MODEL figures and say so (C64-SPEC §9.7): PERFORMANCE.md's 11 us for a
     * near call + ret plus the body's own clocks - ~125 for a scr_rd's ten
     * instructions and a segment load, ~820 for c64_dirty_take's two `rep`s
     * over 32 bytes on the 8088's 8-bit bus - at 0.21 us a clock. They are
     * the one part of this table not taken on tests/c64band, because the
     * bench measures c64band.inc and these live in c64mem.inc. */
       + (long)(n_scr_acc - b_scr_acc) * C64COST_SCRACC
       + (long)(n_scr_take - b_scr_take) * C64COST_TAKE
    /* ...AND THE TWO COMMANDS THAT TOUCH BYTES: Edit > Copy's matrix pull and
     * its per-cell conversion, and Edit > Paste's per-byte one. These are the
     * rows the wave-3 review found priced BELOW THE FLOOR - one near call for
     * a loop that was making two far calls a cell - so they are charged here,
     * in the one place, and the bridge term above is charged with them. */
       + (long)(n_zcopy_out - b_zc) * C64COST_ZCALL
       + (long)(n_zcopy_bytes - b_zcb) * C64COST_ZBYTE10 / 10
       + (long)(n_cpcell - b_cpcell) * C64COST_CPCELL
       + (long)(n_cprow - b_cprow) * C64COST_CPCALL
       + (long)(n_x2_calls - b_x2) * C64BENCH_X2
       + (long)(n_clip_get_bytes - b_psb) * C64COST_ZBYTE10 / 10
       + (long)(n_kbbuf - b_kb) * C64COST_PSBYTE
    /* ...AND THE ONE CALL IN A COMMAND THAT IS NOT THIS PACKAGE'S OWN CODE.
     * os88_clip_put was charged ONE bridge crossing and nothing for its body,
     * and its body is kernel/clip.inc:70-125: a clip_drop, a mem_claim and a
     * `rep movsb` of every byte - 17 clocks a byte on the 8088's 8-bit bus,
     * the same C64COST_ZBYTE10 the other movers are priced at, ~3.7 ms for
     * Copy's 1,026. It runs inside the CALLER's gfx lock, so it is held-lock
     * time like the rest of the row.
     *
     * THE mem_claim IS THE PART THIS CANNOT PRICE, and C64-SPEC §7.7 says so
     * rather than leaving it out silently: clip_put reclaims whenever the
     * rounded KB differs from the clipboard it already holds (clip.inc:96),
     * so the FIRST Copy of a session always takes that path, and
     * kernel/memory.inc:363 falls back to mem_compact and then mem_shed_one -
     * whose own header prices a compaction at "a memcpy in tenths of a
     * second". This package is precisely the one that fragments the arena: it
     * holds a pinned 64KB and a pinned 20KB claim in the middle of it. The
     * term is UNBOUNDED and is not modelled. */
       + (long)(n_clip_put_bytes - b_cpb) * C64COST_ZBYTE10 / 10
       + (long)(n_ovlx - b_ovlx) * C64COST_OVLCALL;
    printf("  %-32s %7.1f ms   %2d blit %2d fill %2d scroll  "
           "%3d cells in %2d compose call(s)  %3ld glyph cell(s)  "
           "%2ld scratch  %4ld byte(s) converted, %ld bridge\n",
           what, us / 1000.0, n_blit - b_blit, n_fill - b_fill,
           n_scroll - b_scroll, n_band_cells - b_cells,
           n_band_calls - b_bandc, gcells,
           (n_scr_acc - b_scr_acc) + (n_scr_take - b_scr_take),
           (n_cpcell - b_cpcell) + (n_kbbuf - b_kb),
           n_ovlx - b_ovlx);
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
    fwrite(c64_cia[1], 1, 16, f);
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
/* THE ONCE-A-SECOND SPEED FOLD IS A TIMER, and a timer that lands inside an
 * unrelated cost row prices the status widget as part of whatever that row
 * names - `one joystick indicator changed` came out at 5.9 ms with four glyph
 * cells in it that had nothing to do with a joystick. It is HELD by default,
 * so every cost row measures the thing it is named after; the speed rows
 * below let it go deliberately. */
static int h_speed_hold = 1;

static void flush_now(void)
{
    if (h_speed_hold)
        c64_sp_tick = the_ticks;            /* el = 4 < 18: no fold this wake */
    the_ticks += 4;
    c64_latch(0);                           /* the register writes the script
                                             * made reach the screen at the
                                             * display latch's line, once a
                                             * frame (9.9) - and a write in the
                                             * lines BELOW the display is the
                                             * next frame's (9.10), so it is
                                             * TWO latches that put whatever
                                             * the script wrote on every row */
    c64_latch(0);
    os88_onwake(the_win);                   /* the wake runs with the lock NOT
                                             * held and takes it itself (74.1) */
    if (lock_depth != 0) { fail("a wake left the gfx lock held"); exit(1); }
}

/* THE KERNEL DISPATCHES EVERY CALLBACK UNDER THE LOCK, so the script does
 * too - that is the whole point of modelling the depth. */
/* W_PAINT IS THE ONE CALLBACK THE KERNEL ARMS THE CLIP FOR (11.3), so the
 * script arms it here and nowhere else - which is the whole point of
 * modelling it. */
static void h_boot_screen(void)
{
    static const char *l1 = "**** COMMODORE 64 BASIC V2 ****";
    static const char *l2 = "64K RAM SYSTEM  38911 BASIC BYTES FREE";
    static const char *l3 = "READY.";
    const char *p;
    unsigned a;
    int i;

    for (i = 0; i < 1000; i++)
        c64_wr(0x0400 + i, 32);
    for (i = 0; i < 1000; i++)
        c64_col[i] = 14;
    a = 0x0400 + 40 + 4;
    for (p = l1; *p; p++) c64_wr(a++, (*p >= 'A' && *p <= 'Z') ? *p - 64 : *p);
    a = 0x0400 + 3 * 40 + 1;
    for (p = l2; *p; p++) c64_wr(a++, (*p >= 'A' && *p <= 'Z') ? *p - 64 : *p);
    a = 0x0400 + 5 * 40;
    for (p = l3; *p; p++) c64_wr(a++, (*p >= 'A' && *p <= 'Z') ? *p - 64 : *p);
    c64_wr(0x0400 + 6 * 40, 32 + 128);
    c64_dirty_all();
}

static void do_paint(void *w)
{ os88_gfx_lock(); clip_armed = 1; os88_paint(w); os88_gfx_unlock(); }
static void do_click(int x, int y, void *w)
{ os88_gfx_lock(); os88_onclick(x, y, w); os88_gfx_unlock(); }
static void do_cmd(int item, int menu, void *w)
{ os88_gfx_lock(); os88_oncmd(item, menu, w); os88_gfx_unlock(); }
static void do_about(void *w)
{ os88_gfx_lock(); os88_about(w); os88_gfx_unlock(); }

/* do_edit - EDIT > COPY AND EDIT > PASTE, DRIVEN THE WAY THE PRODUCT DRIVES
 * THEM. Neither command does its own work any more: os88_oncmd is dispatched
 * under the DESKTOP's gfx lock, and neither body is bounded by a count that
 * can be stated there - Copy is ~103 ms of conversion and Paste is a 2KB heap
 * claim, and both reach kernel/clip.inc's mem_claim, which may compact an
 * arena holding this app's pinned 64KB and 20KB. So the command LATCHES and
 * the top of the next wake runs ovl_clip_service with no lock held.
 *
 * THE FIRST HALF OF THIS FUNCTION IS THE NEGATIVE CONTROL and it is the whole
 * reason the helper exists rather than two lines at each call site: a command
 * that goes back to doing the work itself moves the clipboard counters while
 * lock_depth is 1, and fails HERE. The service is then called directly rather
 * than through os88_onwake, so the rows below price the clipboard work and not
 * a slice that would have run in the same wake. */
/* do_reset - File > Reset machine CPU and Power cycle machine, driven the way
 * the product drives them. Same shape and same reason as do_edit: the command
 * LATCHES because the power cycle's RAM fill is 64KB - about a quarter of a
 * second on the target - and os88_oncmd is dispatched under the desktop's gfx
 * lock. The first half is the negative control: a command that goes back to
 * filling RAM itself moves the machine's state while lock_depth is 1. */
static void do_reset(int item, void *w)
{
    int st0 = c64_state, req0 = c64_reset_req;
    do_cmd(item, C64_M_FILE, w);
    if (c64_state != st0 || c64_reset_req == req0)
        fail("File > Reset ran the reset from inside os88_oncmd, which the "
             "kernel dispatches UNDER the gfx lock - a 64KB RAM fill there is "
             "the whole desktop stopped (LESSONS.md 6)");
    if (lock_depth != 0)
        fail("do_reset: the command left the lock held");
    ovl_reset_service();                /* what the top of the next wake does */
    if (lock_depth != 0)
        fail("ovl_reset_service took the gfx lock - it is called from the "
             "wake and holds none (SPEC.md 74.1)");
}

static void do_edit(int item, void *w)
{
    int p0 = n_clip_put, g0 = n_clip_get, c0 = claims_live;
    do_cmd(item, C64_M_EDIT, w);
    if (n_clip_put != p0 || n_clip_get != g0 || claims_live != c0)
        fail("an Edit command touched the clipboard or the heap from inside "
             "os88_oncmd, which the kernel dispatches UNDER the gfx lock - "
             "that is the whole desktop stopped for an unbounded operation "
             "(LESSONS.md 6, C64-SPEC 7.7)");
    if (lock_depth != 0)
        fail("do_edit: the command left the lock held");
    ovl_clip_service(c64_mbase);        /* what the top of the next wake does */
    if (lock_depth != 0)
        fail("ovl_clip_service took the gfx lock - it is called from the wake "
             "and holds none (SPEC.md 74.1)");
}

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

/* THE ROM-LESS MACHINE HAD A WHOLE SECOND PROCESS HERE (`--no-rom`, 1.4) and
 * it is deleted rather than skipped. os88_main runs once per process and the
 * refusal surface was decided there, so the one screen a user of a mis-copied
 * disk actually saw - four lines naming the file - needed its own run to be
 * drawn by any check at all. With the ROM a PART of the package (SPEC.md
 * 20.12) there is no mis-copied disk: op_load has the 20KB and the bytes
 * before os88_main runs, or the launch was refused before a sector was spent.
 * What went with it: the notice and its once-only gate, its expose repair,
 * the message deadline's negative control on a branch that no longer exists,
 * and the greying of Advance frame, Copy and Paste on a machine that could
 * not start. The JAM half of every one of those rules is still here and still
 * checked - that state is real. */

int main(int argc, char **argv)
{
    /* THE THROTTLE (C64-SPEC 4.4.1): APPLE2's case one machine along - 64
     * ticks of this host's clock pay in 64 ticks of a PAL C64, a stall is
     * not paid back, warp is not held - and then OFF for every other case */
    {
        long tot = 0;
        int k, b;

        c64_th_tick = the_ticks;
        for (k = 0; k < 64; k++) {
            the_ticks++;
            while ((b = c64_th_allow(16384)) > 0) {
                tot += b;
                c64_th_spend(b);
            }
        }
        /* 64 / 18.2065 s x 985,248 Hz = 3,463,354 cycles */
        if (tot < 3463354L - 2048 || tot > 3463354L + 2048) {   /* 0.06% */
            printf("c64uitest: FAIL - the throttle paid %ld cycles for 64 "
                   "ticks and a PAL C64 runs 3,463,354 (C64-SPEC 4.4.1)\n", tot);
            exit(1);
        }
        the_ticks += 100;
        tot = 0;
        while ((b = c64_th_allow(16384)) > 0) {
            tot += b;
            c64_th_spend(b);
        }
        if (tot > 3L * 846 * 64) {
            printf("c64uitest: FAIL - a stall was paid back as %ld cycles\n", tot);
            exit(1);
        }
        c64_warp = 1;
        if (c64_th_allow(16384) != 16384) {
            printf("c64uitest: FAIL - warp is throttled\n");
            exit(1);
        }
        c64_warp = 0;
        printf("c64uitest: the throttle holds 64 ticks to a PAL C64 and does "
               "not pay a stall back\n");
        c64_th_off = 1;
    }

    void *win;
    int i, k;

    memset(glass, 0x55, sizeof(glass));     /* nothing is drawn yet, and 0x55
                                             * is neither lit nor dark */

    h_in_main = 1;
    win = os88_main();
    h_in_main = 0;
    if (win == 0) { fail("os88_main refused with the ROM present"); return 1; }

    /* THE RAM IS MOVABLE ONCE THE LAUNCH IS DONE (C64-SPEC 3.1.1), and
     * os88_onmove keeps BOTH words that can name it - c64_m.ramseg and, while
     * the character generator is in RAM, c64_chr_seg - and leaves the ROM's
     * where os88_part_seg says. A round trip, so nothing after this sees it */
    if (!ram_movable)
        fail("os88_main did not declare the 64KB RAM movable (C64-SPEC 3.1.1)");
    {
        unsigned chr = c64_chr_seg;
        os88_onmove(SEG_RAM, 0x7700);
        if (c64_m.ramseg != 0x7700 || c64_m.romseg != os88_part_seg(0)
            || (chr == SEG_RAM ? c64_chr_seg != 0x7700 : c64_chr_seg != chr))
            fail("os88_onmove did not follow the RAM claim into every word "
                 "that names it");
        os88_onmove(0x7700, SEG_RAM);
        if (c64_m.ramseg != SEG_RAM || c64_chr_seg != chr)
            fail("os88_onmove did not follow the RAM claim back");
        /* ...AND THE CARVE: crt0 calls os88_onmove with the CARVE's two bases
         * once op_base is the new one, so the ROM's word - and the character
         * generator's while it is the ROM's - must follow os88_part_seg's
         * answer whatever `was` and `now` say */
        h_rom_seg = 0x2800;
        os88_onmove(0x1F00, 0x2700);
        if (c64_m.romseg != 0x2800 || c64_m.ramseg != SEG_RAM
            || (chr == SEG_ROM ? c64_chr_seg != 0x2800 : c64_chr_seg != chr))
            fail("os88_onmove did not re-read the ROM's segment after the "
                 "parts carve moved (CC_ONMOVE_PARTS)");
        h_rom_seg = SEG_ROM;
        os88_onmove(0x2700, 0x1F00);
        if (c64_m.romseg != SEG_ROM || c64_chr_seg != chr)
            fail("os88_onmove did not follow the carve back");
    }

    /* THE KEY-STATE MAP MUST BE ARMED BEFORE THE FIRST os88_onkey (7.2's
     * rule 1). Its first call clears the map and always answers "up", so a
     * package that asks first from a callback erases the make os88_onkey has
     * already seen - the first key of the session, gone, and nothing on any
     * glass says so. */
    if (!h_key_armed)
        fail("os88_key_down was never asked from os88_main - the key-state "
             "map is unarmed and the first key of the session is lost");
    if (!h_key_first_in_main)
        fail("the key-state map was armed from somewhere other than "
             "os88_main (7.2's rule 1)");

    /* ...AND THE TWO CONVERSION TABLES ARE BUILT ALREADY. The code that fills
     * them runs exactly once a launch, so it is ovl_conv_init's and lives in
     * the overlay (SPEC.md 73.14) - and the overlay is an EAGER part of
     * C64.O88 (C64-SPEC 13.3), bound before os88_main runs, so os88_main
     * calls it. CR and LF both map to PETSCII CR, which no byte-map entry
     * does, so those two bytes say the fill ran and nothing else could. */
    if (c64_pettab[0x0A] != 0x0D || c64_pettab[0x0D] != 0x0D)
        fail("the PETSCII conversion tables were not built by os88_main - "
             "ovl_conv_init is called from there, the overlay being an "
             "eager part (C64-SPEC 13.3)");

    /* ...and the KERNAL's own first screen. The scripted core writes it on
     * its first slice, which is what a real one does: wave 2 has no
     * hand-poked boot screen any more (c64.c's head). */
    c64_run(1);
    c64_advance(1);

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
    /* ...AND THE 16 SLID ROWS WERE PROVED, NOT GUESSED (9.4.1). The
     * signature is still a HINT and nothing rests on it: a slid row is
     * skipped only when its forty matrix bytes and forty colour nibbles read
     * exactly what the row it came from was drawn from (c64_srcsh), which is
     * the same picture. So the 9 vacated rows are composed and the 16 slid
     * ones cost a 40-byte copy and an 80-byte compare each - it was 25
     * composes and compares. audit() above checked every pixel. The
     * collision step below is the case the proof exists to catch. Verified
     * red with c64_neq answering 0 (the collision's row 0 then stays wrong) */
    if (n_band_calls - b_bandc != 9 || c64_n_proved - b_proved != 16)
        fail("a k = 9 shift did not compose its 9 vacated rows and PROVE its "
             "16 slid ones (9.4.1)");

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
    /* (...and rows 2-6 really do move up one, behind it: the band test
     * wants three CHANGED rows before it believes a scroll, 9.4.1, and a
     * single collision alone is not one) */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (i == 1 && k == 5) ? 34
                   : (i >= 2 && i <= 6 && k == 10) ? (1 + i) : 32);
    flush_now();
    audit("before the signature collision");
    cost_begin();
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (i == 0 && k == 6) ? 36
                   : (i >= 1 && i <= 5 && k == 10) ? (2 + i) : 32);
    flush_now();
    cost_row("a k = 1 shift the signature got WRONG");
    if (n_scroll - b_scroll != 1)
        fail("the collision fixture did not reach the scroll path at all - "
             "it is not testing what it says");
    audit("after the signature collision");
    row_truth(0, "after the signature collision");

    /* --- A PARTIAL SCROLL, DOWN (9.4.1): rows 0-11 move down one into 1-12
     * and a new row 0 comes in, rows 13-24 standing still - the Portland
     * CUG demo's falling snow. One gfx_scroll of the band, its 12 slid rows
     * proved from their bytes, only the new row 0 composed, and every pixel
     * right (audit). Verified red with the band test kept to whole-screen,
     * upward shifts: no scroll, 13 composes. */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 11 + k * 3) % 26)));
    flush_now();
    audit("before the partial scroll");
    cost_begin();
    memmove(ram + 0x0400 + 40, ram + 0x0400, 12 * 40);
    for (k = 0; k < 40; k++)
        ram[0x0400 + k] = (unsigned char)(1 + ((k * 5) % 26));
    for (i = 0x0400; i < 0x0400 + 13 * 40; i++)
        c64_dirty((unsigned)i);
    c64_scr_wr(0x2C, 0x00); c64_scr_wr(0x2D, 0x04);
    c64_scr_wr(0x2E, (0x0400 + 13 * 40 - 1) & 0xFF);
    c64_scr_wr(0x2F, (0x0400 + 13 * 40 - 1) >> 8);
    c64_dirty_any = 1;
    flush_now();
    cost_row("a partial scroll down, rows 0-12");
    audit("after the partial scroll");
    if (n_scroll - b_scroll != 1 || n_band_calls - b_bandc != 1
            || c64_n_proved - b_proved != 12)
        fail("a partial downward scroll was not one gfx_scroll, one composed "
             "row and twelve proved ones (9.4.1)");

    /* --- ...AND A SPRITE OVER THE BAND (9.4.1). The scroll moves the
     * GLASS, sprite pixels and all, so a sprite drawn over rows 5-7 lands on
     * rows 6-8 - and in the same frame the sprite is switched off. Every row
     * of the band must then be its text and nothing else: a slid row is
     * examined whole, and one that came from under a sprite is never proved
     * (its record says so). Verified red with the proof trusting a record
     * made under a sprite: the sprite's pixels survive on rows 6-8. (The
     * whole-row mark, c64_rowd 8, guards a slid row whose bytes were NOT
     * rewritten, which a page-granular dirty set cannot produce here.) */
    {
        int r, j;
        for (j = 0; j < 63; j++)
            c64_wr(0x0340 + j, 0xFF);
        c64_wr(0x07F8, 0x0D);
        c64_io_wr(0xD027, 1);
        c64_io_wr(0xD000, 24 + 40);
        c64_io_wr(0xD001, 50 + 40);         /* rows 5-7 */
        c64_io_wr(0xD015, 1);
        c64_latch(0);
        c64_latch(0);
        flush_now();
        memmove(ram + 0x0400 + 40, ram + 0x0400, 12 * 40);
        for (k = 0; k < 40; k++)
            ram[0x0400 + k] = (unsigned char)(1 + ((k * 7) % 26));
        for (i = 0x0400; i < 0x0400 + 13 * 40; i++)
            c64_dirty((unsigned)i);
        c64_scr_wr(0x2C, 0x00); c64_scr_wr(0x2D, 0x04);
        c64_scr_wr(0x2E, (0x0400 + 13 * 40 - 1) & 0xFF);
        c64_scr_wr(0x2F, (0x0400 + 13 * 40 - 1) >> 8);
        c64_io_wr(0xD015, 0);               /* ...and the sprite goes */
        c64_latch(0);
        c64_latch(0);
        c64_dirty_any = 1;
        b_scroll = n_scroll;
        flush_now();
        if (n_scroll - b_scroll != 1)
            fail("the band under a sprite did not scroll - the sprite case "
                 "is not reaching what it tests");
        for (r = 0; r <= 12; r++)
            row_truth(r, "after a band scroll under a sprite");
    }

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

    /* --- a $D020-only change: fills only, no bands ----------------------
     * AND THE LEVEL HAS TO ACTUALLY FLIP, which is what the pair of rows
     * below is about. c64_border draws the strips `lit` or not (9.6), so the
     * only $D020 write that changes a pixel is one that crosses the
     * background's luminance. Colour 0 is 0 and colour 1 is 255 against
     * background 6's 60: dark, then light. */
    c64_io_wr(0xD021, 6);                   /* the KERNAL's own background */
    c64_io_wr(0xD020, 0);                   /* ...and a DARK border to start */
    flush_now();
    cost_begin();
    h_noise();
    c64_io_wr(0xD020, 1);                   /* the border to white: the mono
                                             * level flips, so it is drawn */
    flush_now();
    cost_row("a $D020 change");
    audit("after a $D020 change");
    if (n_band_calls - b_bandc != 0)
        fail("a border change composed a band");
    if (n_fill - b_fill == 0)
        fail("a border change filled nothing");

    /* --- THE NEGATIVE CONTROL, AND IT IS THE COMMON CASE: a $D020 write
     * that moves the REGISTER and not the mono LEVEL. Colours 14 (118) and
     * 15 (163) are both lighter than background 6 (60), so the border looks
     * identical either way - and c64_lum_update's flip test is the only thing
     * that may raise c64_border_dirty. c64_io_wr used to raise it
     * unconditionally one line later, which made that guard dead code and
     * cost four os88_gfx_fill calls - 3.0 ms - on the next flush after EVERY
     * $D020 write. Rasterbars, loader flashes and $D020 cycling are many
     * writes a frame. */
    cost_begin();
    h_noise();
    c64_io_wr(0xD020, 14);
    c64_io_wr(0xD020, 15);
    flush_now();
    cost_row("a $D020 change that keeps the level");
    audit("after a $D020 change that keeps the level");
    if (n_fill - b_fill != 0)
        fail("a $D020 write that does not change the mono LEVEL still drew "
             "the border - c64_lum_update's flip test is the only thing that "
             "may raise c64_border_dirty (9.6)");
    if (n_band_calls - b_bandc != 0)
        fail("a $D020 write composed a band");

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

    /* --- a FINE-SCROLL write composes NOTHING (5.1, 9.10) ---------------
     * The fine scroll is not honoured, so it cannot change the picture: a
     * program smooth-scrolling with $D011/$D016's low bits every frame
     * recomposed every row every frame until the latch's change test was
     * masked to the bits the composer draws from. Verified red with the
     * mask removed. */
    cost_begin();
    c64_io_wr(0xD016, (c64_vic[0x16] ^ 0x01));   /* fine scroll x */
    c64_io_wr(0xD011, (c64_vic[0x11] ^ 0x07));   /* ...and y */
    flush_now();
    cost_row("a fine-scroll change, $D011 and $D016");
    audit("after a fine-scroll change");
    if (n_band_calls - b_bandc != 0 || n_blit - b_blit != 0)
        fail("a fine-scroll write recomposed or drew - it cannot change the "
             "picture here (5.1)");
    c64_io_wr(0xD016, (c64_vic[0x16] ^ 0x01));
    c64_io_wr(0xD011, (c64_vic[0x11] ^ 0x07));

    /* --- a register write that DOES change: recompose, then COMPARE ----- */
    cost_begin();
    c64_io_wr(0xD021, 9);                   /* brown paper: the same luminance
                                             * as blue (60, 9.6), so every row
                                             * recomposes and composes the SAME
                                             * pixels */
    flush_now();
    cost_row("a $D021 change that draws the same picture");
    audit("after a $D021 change");
    if (n_band_calls - b_bandc == 0)
        fail("a frame-register change did not recompose");
    if (n_blit - b_blit != 0)
        fail("a frame-register change that composed the same pixels still "
             "drew - the shadow compare was forced off");
    c64_io_wr(0xD021, 6);
    flush_now();                            /* (back: and a flush in THIS tick
                                             * for the row below) */

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
    /* ...through the PRODUCT'S OWN PATH: the keyset is polled once per wake
     * out of os88_key_down (7.2, 8), so the test presses keys and lets the
     * poll find them. Assigning c64_joy2 by hand measured a path nothing in
     * the package could reach - and once the poll existed it overwrote the
     * assignment on the very next wake. */
    h_keydown[KSC_UP] = 1;
    h_keydown[KSC_CTRL] = 1;                /* up + fire on control port 2 */
    c64_dirty_any = 1;                      /* ...and nothing else: the
                                             * flush calls ovl_status every
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

    /* --- A SHORT MESSAGE MUST NOT ERASE THE WIDGETS BESIDE IT (§10.1) ---
     * The row is 42 cells and the joystick widget starts at cell 25, so a
     * message of 25 cells or fewer never reaches it - and the fill has to
     * stop where the message does. This is a wave-3 defect before it is a
     * saving: c64kbd.c raises `ScrollLock for joystick` FROM JOYSTICK USE, so
     * the first deflection of the stick blanked the two indicators that
     * report the stick, and the permanent JAM line (22 cells) blanked them
     * for the rest of the session. */
    {
        int x3, y3, litj, litl;

        c64_msg[0] = 0;
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();                        /* the whole row, widgets and all */
        litj = 0;
        for (y3 = 0; y3 < C64_STATH; y3++)
            for (x3 = C64_ST_JOYX; x3 < C64_ST_DRVX; x3++)
                if (glass[c64_gsty + y3][c64_gox + x3] == 1)
                    litj++;
        if (litj == 0)
            fail("the joystick widget is not on the glass to begin with");

        cost_begin();
        c64_say("ScrollLock for joystick");  /* 23 cells: SHORT */
        flush_now();
        cost_row("a short message going up");
        litl = 0;
        for (y3 = 0; y3 < C64_STATH; y3++)
            for (x3 = C64_ST_JOYX; x3 < C64_ST_DRVX; x3++)
                if (glass[c64_gsty + y3][c64_gox + x3] == 1)
                    litl++;
        if (litl != litj)
            fail("a SHORT status message erased the joystick widget - the "
                 "message occupies at most 25 of the row's 42 cells and the "
                 "widget starts at cell 25, so the fill must stop there. "
                 "Wave 3 raises this very message FROM JOYSTICK USE, so the "
                 "first deflection of the stick blanked the two indicators "
                 "that report it (§10.1)");
        if (n_run - b_run != 1)
            fail("a short message going up drew something besides the "
                 "message - the widgets right of it were already there");

        /* ...AND IT COMES DOWN FOR THE PRICE OF THE TWO SPEED FIELDS, not a
         * 37-cell rebuild of a row where 13 of the cells never changed. */
        the_ticks += 120;
        cost_begin();
        flush_now();
        cost_row("...and coming down again");
        if (n_run_cells - b_run_cells > 24)
            fail("a short message coming down rebuilt the whole row - only "
                 "the cells it was standing on are stale");

        /* THE NEGATIVE CONTROL: a LONG message still owns the whole field
         * area, because there is nowhere else for it to go - and the flush
         * after it comes down rebuilds the widgets it blanked. */
        cost_begin();
        c64_say("The clipboard refused to be read.");   /* 33 cells: LONG */
        flush_now();
        cost_row("a long message going up");
        litl = 0;
        for (y3 = 0; y3 < C64_STATH; y3++)
            for (x3 = C64_ST_JOYX; x3 < C64_ST_DRVX; x3++)
                if (glass[c64_gsty + y3][c64_gox + x3] == 1)
                    litl++;
        if (litl == litj)
            fail("a 33-cell message did NOT take the widget's cells - then "
                 "the short-message row above is not measuring anything");
        the_ticks += 120;
        flush_now();
        litl = 0;
        for (y3 = 0; y3 < C64_STATH; y3++)
            for (x3 = C64_ST_JOYX; x3 < C64_ST_DRVX; x3++)
                if (glass[c64_gsty + y3][c64_gox + x3] == 1)
                    litl++;
        if (litl != litj)
            fail("the widgets a LONG message blanked did not come back when "
                 "it expired");
        c64_msg[0] = 0;
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();
    }

    /* ...and a message REPLACING a message is the same field with different
     * text: it must still be drawn (c64_say clears c64_st_lok for it). */
    c64_say("Warp mode on.");
    flush_now();
    if (strcmp(last_toast, "Warp mode on.") != 0)
        fail("c64_say did not toast");
    cost_begin();
    c64_say("Paused.");
    flush_now();
    if (n_run - b_run == 0)
        fail("a message replacing a message did not reach the row");
    /* ...AND IT COMES DOWN BY ITSELF ON A RUNNING MACHINE, which asks for
     * the next wake because it is running and not because a message is up
     * (C64-SPEC §10.1: a machine the user STOPPED keeps its message until the
     * next event, and c64_wants_wake's row further down asserts that half). */
    if (!c64_kick)
        fail("no wake was posted by a RUNNING machine - the message can "
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

    /* --- THE SPEED FIGURES ARE A DELTA, and they are this program's ONE
     * RECURRING REDRAW: the first thing in it that draws on a TIMER rather
     * than on an event (§9.7). Both fields were rebuilt in place and re-run
     * whole every second - 24 glyph cells and 2 call floors, ~23 ms,
     * permanently, about ten times what a keystroke costs - and nine of those
     * cells were the two literal tails that never change. */
    {
        int spx;

        c64_msg[0] = 0;
        c64_state = C64_ST_RUN;
        c64_pause = 1;                      /* the scripted core stays out of
                                             * the way: the counters are the
                                             * fixture here */
        h_speed_hold = 0;                   /* ...and THIS row is the one that
                                             * wants the fold to happen */
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        c64_frames_lo = 0;
        c64_sp_lo = 0; c64_sp_hi = 0; c64_sp_fr = 0;
        c64_sp_tick = the_ticks - 14;       /* flush_now adds 4 before the
                                             * wake, so the fold sees exactly
                                             * el = 18 - one second */
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();                        /* the row full: `      0% cpu`,
                                             * `     0.0 fps`, tails and all */
        if (c64_pct != 0 || c64_fps10 != 0)
            fail("the speed fixture did not start from zero");
        /* THE PRODUCT'S OWN RECURRING SECOND, AND BOTH FIELDS MOVE IN IT.
         * The row this replaced advanced the cycle counter and left the
         * frame counter at zero, with the comment "the fps field does not
         * move at all, because no VIC frame was completed" - which is a
         * FIXTURE and not this program: one fold computes BOTH figures off
         * the same clock (§10.2), the frame count is an integer over an
         * 18-or-19-tick window, and it jitters every second at any speed. So
         * the gate asserted ONE font_run and the shipped behaviour - two -
         * would have failed it. The two fields cannot coalesce into one run
         * either: the fixed `% cpu` tail sits between them at x = 56..95.
         *
         * A REAL SECOND, TWICE. First a second of a machine at ~100 %:
         * 985,248 cycles is 985248 / 5413 = 182, and 182 * 10 / 18 = 101 %;
         * 50 VIC frames is 50 * 182 / 18 = 505, `    50.5`. Then the second
         * after it, one per cent and one frame different - which is what a
         * running machine's row actually does, and what this row measures. */
        c64_cyc_hi = 15; c64_cyc_lo = 2208;         /* 985,248 */
        c64_frames_lo = 50;
        c64_sp_tick = the_ticks - 14;
        c64_dirty_any = 0;
        flush_now();
        if (c64_pct != 101 || c64_fps10 != 505)
            fail("the speed fixture's first second is not a ~100 % machine - "
                 "985,248 cycles and 50 VIC frames is what one second of a "
                 "real PAL C64 is (§10.2)");
        c64_cyc_hi = 30; c64_cyc_lo = 15168;        /* +996,000 cycles */
        c64_frames_lo = 101;                        /* ...and +51 frames */
        c64_sp_tick = the_ticks - 14;
        c64_dirty_any = 0;                  /* ...and NOTHING else asks for
                                             * this flush: the widget's own
                                             * delta is the gate (c64.c) */
        cost_begin();
        h_last_run[0] = 0;
        h_last_run_x = -1;
        h_runs_clear();
        flush_now();
        cost_row("the speed figures changed");
        if (c64_pct != 102 || c64_fps10 != 515)
            fail("the speed fixture's second second did not move BOTH "
                 "fields");
        if (n_fill - b_fill != 0)
            fail("a speed-figure change ERASED the status row - only a full "
                 "redraw may fill (PERFORMANCE.md rule 2)");
        if (n_run - b_run != 2)
            fail("a speed-figure change was not exactly TWO font_runs - one "
                 "per changed NUMBER field. The whole-row path is four calls "
                 "and 24 cells, once a second, for ever (§9.7)");
        if (n_run_cells - b_run_cells > 4)
            fail("a speed-figure change drew more than four glyph cells - a "
                 "typical second is one or two per field");
        spx = c64_gox + C64_ST_CPUX;
        /* ...AND EACH RUN LANDED INSIDE ITS OWN NUMBER, never on a literal
         * tail: `% cpu` and ` fps` are drawn once with the row and have never
         * changed. */
        {
            int r, incpu = 0, infps = 0, fpx = c64_gox + C64_ST_FPSX;
            for (r = 0; r < h_nrun; r++) {
                if (h_run_x[r] >= spx && h_run_x[r] < spx + 7 * 8)
                    incpu++;
                else if (h_run_x[r] >= fpx && h_run_x[r] < fpx + 8 * 8)
                    infps++;
                else
                    fail("the speed delta landed outside both NUMBER fields "
                         "- the literal tails `% cpu` and ` fps` are drawn "
                         "once with the row and have never changed");
            }
            if (incpu != 1 || infps != 1)
                fail("the two speed fields did not draw one run each - the "
                     "`% cpu` tail between them means they can never "
                     "coalesce (§10.2)");
        }
        /* THE NEGATIVE CONTROL: switch the delta off - the full path is what
         * `!c64_st_ok` selects - and show the 24 cells and the erase coming
         * back, so the row above is measuring the delta and not an empty
         * flush. */
        cost_begin();
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();
        cost_row("the same row with the delta switched off");
        if (n_fill - b_fill != 1)
            fail("the negative control did not erase: the full path this row "
                 "exists to be cheaper than is no longer reachable");
        if (n_run_cells - b_run_cells < 24)
            fail("the negative control drew fewer than the 24 glyph cells "
                 "the whole-row path costs - it is not modelling the redraw "
                 "this delta replaced");
        h_speed_hold = 1;
        c64_pause = 0;
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        c64_sp_lo = 0; c64_sp_hi = 0; c64_sp_fr = 0;
        c64_sp_tick = the_ticks;
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();
    }

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

    /* --- THE COLOUR SCREEN (C64-SPEC 9.12). On a display with FSXM_VGA13,
     * Alt+D is the bracket: the window does not go WF_FULL, the C64's
     * 320x200 goes into the framebuffer one byte a pixel, nothing draws
     * through a desktop slot inside, and Alt+D comes back. Each pixel below
     * is read off the modelled framebuffer. Verified red with the text arm's
     * two colours swapped in ovl_fsx_compose, with the sprite pass taken out
     * of ovl_fsx_frame, and with c64_say's toast fence removed. c64memtest
     * runs the shipping c64fsx.inc composers on a real x86. */
    {
        int j, x, rt;
        unsigned g;
        static unsigned char save_ram[0x2001], save_col0;
        memcpy(save_ram, ram, sizeof(save_ram));    /* (the rows after this
                                                     * one read the screen) */
        save_col0 = c64_col[0];
        c64_io_wr(0xD021, 6);               /* blue paper */
        c64_io_wr(0xD020, 14);              /* light blue border */
        c64_io_wr(0xD018, 0x15);
        c64_io_wr(0xD011, 0x1B);
        c64_latch(0);
        c64_latch(0);
        c64_wr(0x0400, 0x01);               /* an 'A'... */
        c64_col[0] = 2;                     /* ...in red */
        h_fsx_mask = 1 << OS88_FSXM_VGA13;
        h_fsx_keyq[0] = 0x1E61;             /* 'a', for the machine */
        h_fsx_keyq[1] = 0x1100;             /* Alt+W, which SAYS "Warp mode
                                             * on." - a toast the fence must
                                             * hold back - and again, off */
        h_fsx_keyq[2] = 0x1100;
        h_fsx_keyq[3] = 0xFFFF;             /* (a pass with no key: a frame) */
        h_fsx_keyq[4] = 0x2000;             /* Alt+D */
        h_fsx_keyn = 5;
        h_fsx_keyi = 0;
        rt = c64_ring_t;
        h_fsx_runs = 0;
        os88_gfx_lock();
        os88_onkey(0, 0x20, win);           /* Alt+D */
        os88_gfx_unlock();
        if (h_fsx_runs != 1 || h_fsx_modes < 1 || fs_on)
            fail("Alt+D on a VGA did not run the colour bracket - or went "
                 "WF_FULL as well (9.12)");
        if (c64_warp)
            fail("Alt+W twice inside the colour bracket did not toggle warp "
                 "on and off again");
        if (h_fsx_drawn != 0)
            fail("something called a desktop drawing slot inside the colour "
                 "bracket - SPEC.md 53.7 forbids it after the mode set");
        if (h_fsx_border != 14)
            fail("the border is not $D020's colour in the overscan register");
        g = rom[0x4000 + 8];                /* the CHARGEN's 'A', line 0 */
        for (x = 0; x < 8; x++)
            if (h_fsx_fb[x] != (((g << x) & 0x80) ? 2 : 6))
                fail("a text cell is not its glyph in colour RAM's red on "
                     "$D021's blue (9.12)");
        if (c64_ring_t == rt)
            fail("a key typed inside the colour bracket did not reach the "
                 "machine's key ring");
        if (c64_fsx_up)
            fail("the bracket left c64_fsx_up set: no toast would ever be "
                 "raised again");

        /* ...a multicolour bitmap byte, with sprite 0 over cells 2-4 */
        c64_io_wr(0xD018, 0x18);
        c64_io_wr(0xD011, 0x3B);
        c64_io_wr(0xD016, c64_vic[0x16] | 0x10);
        c64_wr(0x0400, 0x12);               /* 01 = white, 10 = red */
        c64_col[0] = 5;                     /* 11 = green */
        c64_wr(0x2000, 0x1B);               /* pairs 00 01 10 11 */
        for (j = 0; j < 63; j++)
            c64_wr(0x0340 + j, 0);
        c64_wr(0x0340, 0xFF);
        c64_wr(0x07F8, 0x0D);
        c64_io_wr(0xD027, 7);               /* yellow */
        c64_io_wr(0xD000, 24 + 16);
        c64_io_wr(0xD001, 50);
        c64_io_wr(0xD015, 1);
        c64_latch(0);
        c64_latch(0);
        h_fsx_keyq[0] = 0xFFFF;
        h_fsx_keyq[1] = 0x2000;
        h_fsx_keyn = 2;
        h_fsx_keyi = 0;
        os88_gfx_lock();
        os88_onkey(0, 0x20, win);
        os88_gfx_unlock();
        {
            static const unsigned char want[8] = { 6, 6, 1, 1, 2, 2, 5, 5 };
            for (x = 0; x < 8; x++)
                if (h_fsx_fb[x] != want[x])
                    fail("a multicolour bitmap byte is not paper, high nibble, "
                         "low nibble, colour RAM in colour (9.12)");
        }
        for (x = 16; x < 24; x++)
            if (h_fsx_fb[x] != 7)
                fail("sprite 0 is not drawn in its colour over the bitmap "
                     "(9.12)");
        c64_io_wr(0xD015, 0);
        c64_io_wr(0xD016, c64_vic[0x16] & ~0x10);
        c64_io_wr(0xD011, 0x1B);
        c64_io_wr(0xD018, 0x15);
        c64_latch(0);
        c64_latch(0);
        h_fsx_mask = 0;
        for (j = 0; j < (int)sizeof(save_ram); j++)
            if (ram[j] != save_ram[j])
                c64_wr(j, save_ram[j]);
        c64_col[0] = save_col0;
        c64_dirty_all();
        flush_now();
    }
    cost_begin();
    flush_now();
    if (n_blit - b_blit || n_band_calls - b_bandc)
        fail("the wake after LEAVING fullscreen redrew the screen again");

    /* --- 9.8'S TIER TABLE: 2x, ON THE TIERS THAT CAN PAY FOR IT --------
     * The rows above ran on the TARGET (h_cpu_tier is CPU_8086 by default and
     * that is deliberate), where the table says 1:1 centred and the assertions
     * above are what 1:1 looks like. This is the other half of the same table,
     * and it is checked by reading the GEOMETRY the package computes rather
     * than by trusting a flag: c64_scw and c64_sch are the glass pixels per
     * C64 cell, and every cell<->pixel conversion in the package divides by
     * them.
     *
     * THE FRAME SHADOW STAYS 320x200 THROUGHOUT. The doubling is at blit time
     * and nowhere else, which is why none of the compare, signature or span
     * rows above need a second version. */
    h_cpu_tier = OS88_CPU_386;
    c64_tier_init();
    fs_w = 640; fs_h = 480;                 /* a VGA desktop */
    memset(glass, 0x55, sizeof(glass));
    cost_begin();
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);
    cost_row("entering fullscreen at 2x on VGA");
    if (!fs_on)
        fail("Preferences > Fullscreen did not enter fullscreen at 2x");
    if (c64_scw != 16 || c64_sch != 16)
        fail("a 640x480 fullscreen box did not magnify - 9.8's VGA row is "
             "2x, 640x400 centred");
    if (c64_gnrows != C64_ROWS)
        fail("640x400 does not hold all 25 C64 rows at 2x");
    if (c64_gsx != GX)
        fail("the 2x picture is not flush with a content box it fills "
             "exactly - the 1:1 border floor was applied at 2x and slid it "
             "right, clipping the last column");
    {
        int x, y, lit = 0, unpainted = 0;
        for (y = 0; y < 400; y++)
            for (x = 0; x < 640; x++) {
                if (glass[c64_gsy + y][GX + x] == 0x55)
                    unpainted++;
                else if (glass[c64_gsy + y][GX + x])
                    lit++;
            }
        if (unpainted)
            fail("the 2x picture left pixels of its own 640x400 unpainted");
        if (lit == 0)
            fail("the 2x picture is entirely dark - the doubled band never "
                 "reached the glass");
        /* AND IT IS REALLY DOUBLED, not stretched by the blit: each source
         * pixel row appears TWICE and each source pixel column twice, so
         * every 2x2 block of the magnified picture is uniform. A blit that
         * read the 40-byte band at the wrong stride, or c64_band_x2 called
         * with the wrong row count, breaks exactly this. */
        for (y = 0; y < 400; y += 2)
            for (x = 0; x < 640; x += 2)
                if (glass[c64_gsy + y][GX + x] != glass[c64_gsy + y][GX + x + 1]
                 || glass[c64_gsy + y][GX + x] != glass[c64_gsy + y + 1][GX + x]
                 || glass[c64_gsy + y][GX + x]
                        != glass[c64_gsy + y + 1][GX + x + 1]) {
                    fail("the 2x picture is not pixel-doubled: a 2x2 block of "
                         "the glass is not uniform");
                    y = 400; x = 640;
                }
    }
    cost_begin();
    flush_now();
    cost_row("the wake after entering fullscreen at 2x");
    if (n_blit - b_blit || n_fill - b_fill || n_band_calls - b_bandc)
        fail("the wake after OSAPI_FULLSCREEN at 2x redrew a screen the "
             "kernel had already repainted");

    /* ...AND A SCROLL STILL SCROLLS, at k * 16 rather than k * 8. The
     * alternative the plan allowed was falling back to bands, which at 2x is
     * 25 doubled rows - half a second - for what one gfx_scroll does. */
    for (i = 0; i < C64_ROWS; i++)
        for (k = 0; k < 40; k++)
            c64_wr(0x0400 + i * 40 + k, (unsigned char)(1 + ((i * 3 + k) % 26)));
    flush_now();
    cost_begin();
    memmove(ram + 0x0400, ram + 0x0400 + 40 * 3, 22 * 40);
    for (k = 0; k < 3 * 40; k++)
        ram[0x0400 + 22 * 40 + k] = 32;
    for (i = 0x0400; i < 0x0400 + 1000; i++)
        c64_dirty((unsigned)i);
    for (i = 0; i < C64_ROWS; i++)
        c64_rowd[i] = 1;
    c64_dirty_any = 1;
    flush_now();
    cost_row("a k = 3 shift at 2x");
    if (n_scroll - b_scroll != 1)
        fail("a shift at 2x was not ONE gfx_scroll - the scroll path is in "
             "GLASS pixels and k * 16 is a legal dy");
    if (scroll_dy != 3 * 16)
        fail("a k = 3 shift at 2x did not move the glass by 48 pixels");

    /* --- CGA: 640x200, so 2x HORIZONTAL and 1x vertical (9.8's CGA row).
     * A CGA pixel is already 2:1, so doubling X alone is what makes the
     * picture the right SHAPE there - it is not half a job. */
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win); /* out... */
    fs_w = 640; fs_h = 200;
    memset(glass, 0x55, sizeof(glass));
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win); /* ...and in on a CGA desktop */
    if (c64_scw != 16)
        fail("a 640x200 fullscreen box did not double HORIZONTALLY - 640 "
             "pixels hold the C64's 40 columns at 16 each exactly");
    if (c64_sch != 16 - 8)
        fail("a 640x200 fullscreen box doubled VERTICALLY - 400 rows do not "
             "fit in 200 and the picture would be half off the glass");
    if (c64_gnrows >= C64_ROWS || c64_gnrows < 20)
        fail("the CGA 2x row does not show most of the screen above its "
             "status row (9.1's standing clamp)");
    {
        int x, y, bad = 0;
        for (y = 0; y < c64_gnrows * 8; y++)
            for (x = 0; x < 640; x += 2)
                if (glass[c64_gsy + y][GX + x] != glass[c64_gsy + y][GX + x + 1])
                    bad++;
        if (bad)
            fail("the CGA 2x picture is not doubled in X");
    }

    /* --- THE NEGATIVE CONTROL, AND IT IS TWO. A tier that cannot pay stays
     * at 1:1 - that is 9.8's CPU_8086 row and it is a FACT the code tests,
     * not a guess about speed - and a kernel whose blit1 REFUSES comes down
     * to 1:1 too, because 2x has no font fallback and a picture nobody can
     * draw is the one outcome SPEC.md 47 exists to prevent. */
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);
    h_cpu_tier = OS88_CPU_8086;
    c64_tier_init();
    fs_w = 640; fs_h = 480;
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);
    if (c64_scw != 8 || c64_sch != 8)
        fail("the CPU_8086 tier magnified - 9.8's table says 1:1 centred "
             "there, and c64_band_x2 is 63 ms a screen ON TOP of the compose");
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);

    h_cpu_tier = OS88_CPU_386;
    c64_tier_init();
    blit_refuse = 1;
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);
    flush_now();
    flush_now();
    if (c64_scw != 8 || c64_sch != 8)
        fail("blit1 refused at 2x and the package stayed at 2x - there is no "
             "font fallback for a doubled band, so the picture never arrives");
    blit_refuse = 0;
    do_cmd(C64_I_FULLSCR, C64_M_PREF, win);
    c64_no2x = 0;                           /* the latch is per SESSION on the
                                             * target; the script clears it so
                                             * the rows after this are not run
                                             * against a package that has given
                                             * up on 2x */
    h_cpu_tier = OS88_CPU_8086;
    c64_tier_init();
    fs_w = 640; fs_h = 440;
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    audit("after the 2x rows put the window back");

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

    /* ======================================================================
     * WAVE 2: THE CLOCK, THE ALARM MODEL, THE WALL SLICE AND THE KEYBOARD
     *
     * Every one of these is a rule whose violation is SILENT - the glass says
     * nothing about a jiffy that is 20 per cent wrong, a budget that walked to
     * its cap while the user typed, or a SHIFT bit another key's release took
     * away with it.
     * ==================================================================== */

    /* --- the speed widget's two figures come off EMULATED CYCLES and
     * EMULATED VIC FRAMES (10.2), and the arithmetic is checked against
     * numbers with a known answer: exactly one real PAL second's worth of
     * cycles, over exactly one real second of host ticks, is 100 %; and
     * 19,656 cycles is one PAL frame, so the same run is 50.1 fps. */
    {
        int i2;
        c64_pause = 1;                      /* the scripted core stays out of
                                             * the way while the counters are
                                             * driven by hand */
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        c64_frames_lo = 0;
        c64_frame_cyc = 0;
        c64_sp_tick = the_ticks;
        c64_sp_lo = 0; c64_sp_hi = 0; c64_sp_fr = 0;
        /* ten seconds of host ticks, and ten seconds of a real 6510 fed
         * through the SAME c64_advance the machine uses - so the frame
         * counter is the VIC's own and not a second opinion */
        for (i2 = 0; i2 < 1002; i2++)
            c64_advance(9832);              /* 1002 * 9,832 = 9,851,664 */
        the_ticks += 182;                   /* 182 ticks = 10.0 s at 18.2 Hz */
        c64_speed_fold();
        if (c64_pct != 100)
            fail("`% cpu` is not emulated cycles over 985,248 - one real "
                 "PAL second's worth of cycles in one real second did not "
                 "read 100");
        if (c64_fps10 != 501)
            fail("`fps` is not EMULATED VIC FRAMES - 19,656 cycles a frame "
                 "at 100 % is 50.1, and the widget said otherwise");
        /* ...and a two-word counter fed 100,000 cycles a tick for 60 ticks
         * does not lap. `unsigned` is 16 bits on the target: 6,000,000 cycles
         * is ninety-one wraps of the low word, and a counter that lost them
         * would read a plausible small number rather than failing. */
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        for (i2 = 0; i2 < 60; i2++) {
            c64_advance(19656);             /* the alarm never hands the core
                                             * more than a frame */
            c64_advance(19656);
            c64_advance(19656);
            c64_advance(19656);
            c64_advance(19656);
            c64_advance(1720);              /* 5 * 19,656 + 1,720 = 100,000 */
        }
        if (c64_cyc_hi != (6000000u >> 16) || c64_cyc_lo != (6000000u & 0xFFFFu))
            fail("the two-word cycle counter lapped: 100,000 cycles a tick "
                 "for 60 ticks is 6,000,000 and it did not say so");
        c64_pause = 0;
    }

    /* --- the ALARM MODEL: nothing quantises to a jiffy, and the 60 Hz jiffy
     * EMERGES from the KERNAL programming CIA1 timer A (6.1, 6.3). */
    {
        int d, fired;
        ovl_reset_regs();
        c64_map_publish();
        /* the nearest alarm is a frame's own, not a jiffy and not a host
         * tick: from the display latch's line (9.9, a whole frame away) it
         * is the raster compare at line 0 - reset's - which is where the
         * frame begins. (The frame END is not an alarm of its own: c64io.c's
         * c64_alarm_next says why.) */
        c64_frame_cyc = (int)C64_X63(C64_LATCH_LINE);
        d = c64_alarm_next();
        if (d != C64_PAL_FRAME - (int)C64_X63(C64_LATCH_LINE))
            fail("with no timer running the next alarm is not the PAL frame "
                 "end - something has quantised the clock");
        /* now do what the KERNAL does: $4025 into CIA1 timer A, continuous,
         * interrupt on underflow. 16,421 cycles is 59.98 Hz on a PAL 6510,
         * and NOTHING in this port knows that number. */
        c64_io_wr(0xDC04, 0x25);
        c64_io_wr(0xDC05, 0x40);
        c64_io_wr(0xDC0D, 0x81);            /* unmask timer A */
        c64_io_wr(0xDC0E, 0x11);            /* force load, start, continuous */
        d = c64_alarm_next();
        if (d != 16422)
            fail("CIA1 timer A's underflow is not the next alarm at its own "
                 "latch + 1 - the jiffy is not emerging from the timer");
        c64_scr_wr(C64_SCR_IRQ, 0);
        c64_advance(16422);                 /* the counter reaches 0 and then
                                             * underflows: latch + 1 */
        if ((c64_scr_rd(C64_SCR_IRQ) & 1) == 0)
            fail("CIA1 timer A underflowed and no IRQ line came up");
        if ((c64_icd[0] & 0x01) == 0)
            fail("the timer A underflow did not set its ICR flag");
        /* reading the ICR clears the flags AND the line, as ciacore.c does */
        fired = c64_io_rd(0xDC0D);
        if ((fired & 0x81) != 0x81)
            fail("the ICR read did not answer bit 7 with the flag");
        if ((c64_scr_rd(C64_SCR_IRQ) & 1) != 0)
            fail("reading the ICR did not drop the IRQ line");
        /* ...and the SECOND underflow is a whole latch later, not a whole
         * host tick: the timer reloaded from its own latch and its phase is
         * retained. (The nearest ALARM at this point is the frame end, which
         * is the other accumulator doing its own thing - which is the point
         * of 6.3.) */
        if (c64_ta[0] != 16421)
            fail("the timer did not reload from its latch: its phase was "
                 "lost across the underflow");
        d = c64_alarm_next();
        if (d != C64_PAL_FRAME - (int)C64_X63(C64_LATCH_LINE) - 16422)
            fail("the frame end stopped being an independent accumulator");
        /* an I/O write that raises a line CUTS the run in progress (4.4) */
        n_cut = 0;
        c64_in_run = 1;
        c64_advance(16422);                 /* the flag comes back... */
        c64_io_rd(0xDC0D);
        c64_icd[0] |= 0x01;                 /* ...a flag already set... */
        c64_io_wr(0xDC0D, 0x81);            /* ...and now unmasked */
        c64_in_run = 0;
        if (n_cut == 0)
            fail("an I/O write that raised the IRQ line did not cut the run "
                 "- the interrupt waits for the next alarm");
        /* the VIC raster is the OTHER accumulator, and it is 63 cycles a
         * line: $D012 reads the line the cycle counter has reached */
        c64_frame_cyc = 100 * 63 + 7;
        if (c64_io_rd(0xD012) != 100)
            fail("$D012 is not the raster line the cycle counter reached");
        /* THE RASTER WAIT IN WHOLE PASSES (C64-SPEC 4.7.1). $0900: LDA $D012
         * / CMP #$32 / BNE $0900 - 9 cycles a pass, and line 100 keeps it
         * spinning: with 1,000 cycles of countdown left after the read, 111
         * passes go at once and 1 cycle is left, so the core still ends the
         * run on the instruction it would have. Then the value that leaves
         * the loop, a BEQ/JMP form (11 a pass), and a caller with no PC (the
         * accurate core, the translations), which must charge nothing.
         * Verified to fail with c64_poll_skip returning at once. */
        {
            static const unsigned char lp[] = { 0xAD, 0x12, 0xD0, 0xC9, 0x32, 0xD0, 0xF9 };
            static const unsigned char lj[] = { 0xAD, 0x12, 0xD0, 0xC9, 0x32, 0xF0, 0x03,
                                                0x4C, 0x00, 0x0A };
            int dead;
            memcpy(ram + 0x0900, lp, sizeof lp);
            memcpy(ram + 0x0A00, lj, sizeof lj);
#define H_DEAD() (c64_scr_rd(C64_SCR_DEAD) | (c64_scr_rd(C64_SCR_DEAD + 1) << 8))
#define H_SETDEAD(v) (c64_scr_wr(C64_SCR_DEAD, (v) & 0xFF), c64_scr_wr(C64_SCR_DEAD + 1, ((v) >> 8) & 0xFF))
            H_SETDEAD(1000);
            c64_fpc = 0x0903;
            c64_io_rd(0xD012);
            if ((dead = H_DEAD()) != 1)
                fail("a raster wait that spins was not taken in whole passes "
                     "to one cycle short of the budget (C64-SPEC 4.7.1)");
            if (c64_fpc != 0)
                fail("c64_poll_skip left the PC for the next caller");
            c64_frame_cyc = 0x32 * 63;
            H_SETDEAD(1000);
            c64_fpc = 0x0903;
            c64_io_rd(0xD012);
            if (H_DEAD() != 1000)
                fail("a raster read that LEAVES the wait was charged passes");
            c64_frame_cyc = 100 * 63;
            H_SETDEAD(1000);
            c64_fpc = 0x0A03;
            c64_io_rd(0xD012);
            if (H_DEAD() != 1000 - 90 * 11)
                fail("the BEQ / JMP form of the raster wait was not taken in "
                     "whole 11-cycle passes");
            H_SETDEAD(1000);
            c64_fpc = 0;
            c64_io_rd(0xD012);
            if (H_DEAD() != 1000)
                fail("a raster read with no PC (not the fast core's) was "
                     "charged passes");
            H_SETDEAD(0);
            (void)dead;
        }
        ovl_reset_regs();
        c64_map_publish();
        c64_scr_wr(C64_SCR_IRQ, 0);
    }

    /* --- THE PORT'S PUBLISHED READ-BACK (c64_pp_rb): c64fast.inc puts it in
     * RAM $0000/$0001 at the top of every fast-core run instead of asking
     * c64_io_rd twice, so it must BE c64_pp_read's answer after any write.
     * Verified to fail with c64_pp_rbset's call in c64_map_publish removed. */
    {
        static const int wr[][2] = { {0, 0x2F}, {1, 0x37}, {1, 0x30}, {0, 0x00},
                                     {0, 0xFF}, {1, 0x08}, {0, 0x2F}, {1, 0x37} };
        int i3;
        for (i3 = 0; i3 < 8; i3++) {
            c64_io_wr((unsigned)wr[i3][0], wr[i3][1]);
            if (c64_pp_rb != (unsigned)(c64_pp_read(0) | (c64_pp_read(1) << 8)))
                fail("the published port read-back is not c64_pp_read's: the "
                     "fast core's $00/$01 would read a stale port");
        }
    }

    /* --- DRIVE 8 (11.5): a D64 built here - the BAM, one directory sector,
     * a two-sector PRG "HELLO" at $C000 (254 + 10 bytes, the load address
     * first) - attached, and the KERNAL's LOAD trap served from it. The
     * trap's three answers: the file (and its bytes, end, status and the
     * return through LOADING), the directory as a BASIC program, FILE NOT
     * FOUND; and a device that is not 8 runs the replaced LDA #$60.
     * Verified to fail with the chain's second sector skipped (the end
     * address and the tail bytes both go wrong). */
    {
        static const unsigned char nm[] = { 'H', 'E', 'L', 'L', 'O' };
        unsigned b0, i4, endv;
        long o18_0 = 357L * 256, o18_1 = 358L * 256, o1_0 = 0, o1_1 = 256;
        memset(h_d64, 0, sizeof(h_d64));
        h_d64_len = 174848;
        h_d64[o18_0 + 0] = 18; h_d64[o18_0 + 1] = 1; h_d64[o18_0 + 2] = 0x41;
        for (i4 = 1; i4 <= 35; i4++)
            h_d64[o18_0 + 4 * i4] = (i4 == 1) ? 19 : (i4 == 18 ? 17 : 21);
        memset(h_d64 + o18_0 + 0x90, 0xA0, 27);
        memcpy(h_d64 + o18_0 + 0x90, "TEST DISK", 9);
        h_d64[o18_0 + 0xA2] = 'I'; h_d64[o18_0 + 0xA3] = 'D';
        h_d64[o18_0 + 0xA5] = '2'; h_d64[o18_0 + 0xA6] = 'A';
        h_d64[o18_1 + 0] = 0; h_d64[o18_1 + 1] = 0xFF;      /* the last dir sector */
        h_d64[o18_1 + 2] = 0x82;                             /* a closed PRG */
        h_d64[o18_1 + 3] = 1; h_d64[o18_1 + 4] = 0;          /* starts at 1/0 */
        memset(h_d64 + o18_1 + 5, 0xA0, 16);
        memcpy(h_d64 + o18_1 + 5, nm, 5);
        h_d64[o18_1 + 30] = 2;
        h_d64[o1_0 + 0] = 1; h_d64[o1_0 + 1] = 1;            /* -> 1/1 */
        h_d64[o1_0 + 2] = 0x00; h_d64[o1_0 + 3] = 0xC0;      /* load $C000 */
        for (i4 = 4; i4 < 256; i4++) h_d64[o1_0 + i4] = (unsigned char)i4;
        h_d64[o1_1 + 0] = 0; h_d64[o1_1 + 1] = 11;           /* last: 2..11 */
        for (i4 = 2; i4 < 12; i4++) h_d64[o1_1 + i4] = (unsigned char)(0xE0 + i4);
        ovl_d64_attach("TEST.D64", 0xAB00, 2);
        if (c64_d64_seg == 0)
            fail("a 174,848-byte D64 was not attached to drive 8");
#define H_LOADCALL(nmp, nl, sa) do { unsigned k_; \
            for (k_ = 0; k_ < (nl); k_++) c64_wr(0x0340 + k_, (nmp)[k_]); \
            c64_wr(0xB7, (nl)); c64_wr(0xBB, 0x40); c64_wr(0xBC, 0x03); \
            c64_wr(0xB9, (sa)); c64_wr(0xBA, 8); c64_wr(0x93, 0); \
            c64_wr(0xC3, 0x01); c64_wr(0xC4, 0x08); \
            c64_m.s = 0xF0; c64_m.pc = C64_D64_TRAP; ovl_d64_trap(); } while (0)
        H_LOADCALL(nm, 5, 1);
        endv = (unsigned)c64_rd(0xAE) | ((unsigned)c64_rd(0xAF) << 8);
        if (c64_m.pc != C64_D64_LOADING || endv != 0xC000 + 252 + 10)
            fail("LOAD\"HELLO\",8,1 from the D64 did not land at $C000 with "
                 "its end in $AE/$AF and the KERNAL's LOADING next");
        if (c64_rd(0xC000) != 4 || c64_rd(0xC000 + 252) != 0xE2 || c64_rd(0xC000 + 261) != 0xEB)
            fail("the loaded file's bytes are not the image's, across its two sectors");
        if (c64_rd(0x1EF) != ((C64_D64_EXIT - 1) & 0xFF) || c64_rd(0x1F0) != ((C64_D64_EXIT - 1) >> 8)
            || c64_m.s != 0xEE)
            fail("the trap did not push the KERNAL's own exit for LOADING to return into");
        if (c64_rd(0x90) != 0x40)
            fail("the status after a load is not the KERNAL's end-of-file");
        H_LOADCALL((const unsigned char *)"H*", 2, 0);         /* SA 0: to X/Y's $0801 */
        if (c64_rd(0x0801) != 4)
            fail("LOAD\"H*\",8 did not match the pattern and load at $0801");
        H_LOADCALL((const unsigned char *)"$", 1, 0);
        b0 = (unsigned)c64_rd(0x0801) | ((unsigned)c64_rd(0x0802) << 8);
        if (c64_rd(0x0805) != 0x12 || c64_rd(0x0807) != 'T' || b0 <= 0x0801)
            fail("LOAD\"$\",8 did not make the directory's header line");
        H_LOADCALL((const unsigned char *)"NOPE", 4, 0);
        if (c64_m.pc != C64_D64_NOTFOUND)
            fail("a name the disk has not got did not go to FILE NOT FOUND");
        c64_wr(0xBA, 9);
        c64_m.pc = C64_D64_TRAP;
        c64_m.a = 0;
        ovl_d64_trap();
        if (c64_m.pc != C64_D64_TRAP + 2 || c64_m.a != 0x60)
            fail("a LOAD from device 9 did not run the LDA #$60 the trap replaced");
        os88_mem_free(c64_d64_seg);
        c64_d64_seg = 0;
    }

    /* --- THE DISPLAY LATCH (9.9). A split screen writes $D011 twice a
     * frame - bitmap on at the top, off for a scroller below - and the
     * screen must show what stood at line 51, every frame the same, rather
     * than whichever write the flush happened to follow. Verified to fail
     * with c64_advance's c64_latch call removed: the mode never leaves 0. */
    {
        ovl_reset_regs();
        c64_map_publish();
        c64_frame_regs();
        c64_frame_cyc = 0;
        c64_io_wr(0xD011, 0x3B);            /* bitmap on, above line 51 */
        if (c64_mode != 0)
            fail("a $D011 write reached the screen before the display "
                 "latch's line (9.9)");
        c64_advance((int)C64_X63(C64_LATCH_LINE) + 10);
        /* (that latch composed the frame BEFORE it, 9.10: a write in the
         * lines above the display is the NEXT frame's, every row of it) */
        c64_advance((int)C64_X63(8 * 21 + 5));  /* row 21's second half... */
        c64_io_wr(0xD011, 0x1B);            /* ...text for the scroller below */
        c64_advance((int)C64_X63(60));      /* ...and below the display, */
        c64_io_wr(0xD011, 0x3B);            /* bitmap on again for the top */
        c64_advance((int)C64_X63(C64_PAL_LINES - 8 * 21 - 5 - 60));
        if (c64_mode != 2)
            fail("crossing line 51 did not take the display latch: the "
                 "bitmap mode set above it is not on the screen");
        /* PER ROW (9.10): rows 0-21 drew bitmap and 22-24 text, which is
         * what a split screen IS. Verified red with c64_latch applying the
         * whole log to every row: the screen is one mode */
        if (c64_rmode[0] != 2 || c64_rmode[21] != 2 || c64_rmode[22] != 0
                || c64_rmode[24] != 0 || !c64_rsplit)
            fail("a $D011 split below row 21 did not make rows 0-21 bitmap "
                 "and 22-24 text (9.10)");
        c64_advance(C64_PAL_FRAME);         /* the same frame again, NOTHING
                                             * written: it stays split */
        if (c64_mode != 2 || c64_rmode[24] != 2)
            fail("a split that is bitmap at line 51 every frame flipped the "
                 "screen to text: the latch read a write below its line");
        ovl_reset_regs();
        c64_map_publish();
        c64_frame_regs();
        c64_scr_wr(C64_SCR_IRQ, 0);
    }

    /* --- THE OTHER MODES AND THE SPRITES, IN 1BPP (9.10, 9.11). Each case
     * reads the shadow - what went to the glass - at row 0, line 0, cell 0.
     * Verified red with c64_bandx's bitmap cases swapped, and with the
     * c64_spr_draw call taken out of c64_flush. */
    {
        int j;
        c64_io_wr(0xD021, 6);               /* blue paper: luminance 60 */
        c64_io_wr(0xD018, 0x18);            /* matrix $0400, bitmap $2000 */
        c64_io_wr(0xD011, 0x3B);            /* BMM */
        for (j = 0; j < 1000; j++)
            c64_wr(0x0400 + j, 0x10);       /* 1 = white, 0 = black */
        for (j = 0; j < 8000; j++)
            c64_wr(0x2000 + j, 0xAA);
        for (j = 0; j < 1000; j++)
            c64_col[j] = 1;
        flush_now();
        if (c64_sh[0] != 0xAA || c64_sh[C64_X320(24) + 7 * 40 + 39] != 0xAA)
            fail("a hires bitmap of white-on-black $AA did not reach the "
                 "glass as $AA (9.10)");
        c64_io_wr(0xD016, c64_vic[0x16] | 0x10);    /* ...and MCM */
        flush_now();                        /* (MCM itself: every row) */
        j = (int)c64_n_cell;
        c64_wr(0x2000, 0x1B);               /* pairs 00 01 10 11: paper,
                                             * white, black, colour RAM's 1 */
        flush_now();
        if ((int)c64_n_cell - j > 32)
            fail("one bitmap byte written recomposed more than its page's 32 "
                 "cells (9.10)");
        /* paper 60 on line 0 of the Bayer matrix (8, 136, ...) is `10`, white
         * `11`, black `00`, white `11` */
        if (c64_sh[0] != 0xB3)
            fail("a multicolour bitmap byte did not dither its four colours "
                 "by luminance (9.10)");
        c64_io_wr(0xD016, c64_vic[0x16] & ~0x10);
        c64_io_wr(0xD011, 0x1B);
        c64_io_wr(0xD018, 0x15);            /* text, the ROM's characters */
        for (j = 0; j < 1000; j++)
            c64_wr(0x0400 + j, 0x20);       /* spaces */
        flush_now();
        if (c64_sh[0] != 0x00)
            fail("the screen of spaces is not dark on a dark paper");
        for (j = 0; j < 63; j++)
            c64_wr(0x0340 + j, 0);
        c64_wr(0x0340, 0xFF);
        c64_wr(0x0341, 0x0F);
        c64_wr(0x07F8, 0x0D);               /* sprite 0's data at $0340 */
        c64_io_wr(0xD027, 1);               /* white */
        c64_io_wr(0xD000, 24);              /* the display's top-left */
        c64_io_wr(0xD001, 50);
        c64_io_wr(0xD015, 1);
        flush_now();
        if (c64_sh[0] != 0xFF || c64_sh[1] != 0x0F || c64_sh[40] != 0x00)
            fail("sprite 0 at the display's top-left did not draw its first "
                 "line over the text (9.11)");
        c64_wr(0x0340, 0x81);               /* its DATA changes, nothing else */
        flush_now();
        if (c64_sh[0] != 0x81)
            fail("a write to a sprite's data did not redraw it (9.11)");
        j = (int)c64_n_cell;
        c64_dirty_any = 0;
        c64_io_wr(0xD000, 27);              /* three pixels right */
        c64_latch(0);
        c64_latch(0);
        if (!c64_dirty_any)                 /* (a machine that only moves a
                                             * sprite writes no RAM: the wake
                                             * flushes on this flag or not at
                                             * all - found on the glass, a
                                             * BASIC loop of POKE 53248,X) */
            fail("a sprite register change did not raise the flush gate (9.11)");
        flush_now();
        if (c64_sh[0] != 0x10 || c64_sh[1] != 0x21 || c64_sh[2] != 0xE0)
            fail("a sprite three pixels off a cell boundary did not shift its "
                 "bytes across the cells (9.11)");
        if ((int)c64_n_cell - j > 3 * 4)    /* 21 lines: rows 0-2 */
            fail("a sprite moving three pixels recomposed more than the four "
                 "cells a row it was and is over (9.11)");
        c64_io_wr(0xD015, 0);
        flush_now();
        if (c64_sh[0] != 0x00 || c64_sh[1] != 0x00)
            fail("a sprite switched off stayed on the glass (9.11)");
        c64_io_wr(0xD021, 6);
    }

    /* --- THE CLOCKS A PROGRAM READS MID-RUN, AND SID VOICE 3 (6.3, 11.4.1).
     * Both demos' snow is random: the Portland CUG one reads voice 3's noise
     * at $D41B and the Bright Pixel card mixes $D012 with CIA 1's timer A.
     * $D41B answered the stored byte (a constant: every flake in column 0)
     * and $DC04 the count at the run's start (the same value every frame).
     * Verified red with c64_osc3_now's hook taken out of c64_io_rd, and with
     * c64_tlive answering c64_ta[k]. */
    {
        int j, n, seen[256];
        unsigned t0, t1;
        ovl_reset_regs();
        c64_io_wr(0xD40E, 0xFF);            /* voice 3 fast, on noise */
        c64_io_wr(0xD40F, 0xFF);
        c64_io_wr(0xD412, 0x80);
        memset(seen, 0, sizeof(seen));
        n = 0;
        for (j = 0; j < 64; j++) {
            c64_advance(37 + (j & 7));      /* a loop's worth between reads */
            t0 = (unsigned)c64_io_rd(0xD41B);
            if (!seen[t0 & 0xFF]) {
                seen[t0 & 0xFF] = 1;
                n++;
            }
        }
        if (n < 24)
            fail("$D41B on the noise waveform is not a spread of values: "
                 "voice 3's oscillator is not running (11.4.1)");
        c64_io_wr(0xD412, 0x00);
        /* timer A running, read twice inside ONE run 100 cycles apart */
        c64_io_wr(0xDC04, 0xFF);
        c64_io_wr(0xDC05, 0x7F);
        c64_io_wr(0xDC0E, 0x11);            /* load and start */
        c64_in_run = 1;
        c64_run_n = 1000;
        c64_scr_wr(C64_SCR_CARRY, 0);       /* (no cut in this run) */
        c64_scr_wr(C64_SCR_CARRY + 1, 0);
        c64_scr_wr(C64_SCR_DEAD, 1000 & 0xFF);
        c64_scr_wr(C64_SCR_DEAD + 1, 1000 >> 8);
        t0 = (unsigned)c64_io_rd(0xDC04) | ((unsigned)c64_io_rd(0xDC05) << 8);
        c64_scr_wr(C64_SCR_DEAD, 900 & 0xFF);
        c64_scr_wr(C64_SCR_DEAD + 1, 900 >> 8);
        t1 = (unsigned)c64_io_rd(0xDC04) | ((unsigned)c64_io_rd(0xDC05) << 8);
        c64_in_run = 0;
        c64_scr_wr(C64_SCR_DEAD, 0);
        c64_scr_wr(C64_SCR_DEAD + 1, 0);
        if (t0 - t1 != 100)
            fail("CIA 1 timer A read twice in one run, 100 cycles apart, did "
                 "not count down by 100 - a read answers the run's start (6.3)");
        /* ...AND THE RASTER WAIT'S SKIP STOPS AT THE LINE IT WAITS FOR. The
         * Bright Pixel card's `LDA $D012 / CMP #50 / BEQ +3 / JMP back` read
         * at line 40 with 10,000 cycles of budget: the skip may charge whole
         * passes only up to line 50. Before, it charged them to the run's
         * end and the loop never saw 50 - verified red with c64_poll_lim's
         * answer ignored */
        {
            static const unsigned char loop[10] = {
                0xAD, 0x12, 0xD0, 0xC9, 50, 0xF0, 0x03, 0x4C, 0x00, 0x09 };
            int ln;
            for (j = 0; j < 10; j++)
                c64_wr(0x0900 + j, loop[j]);
            c64_frame_cyc = (int)C64_X63(40);
            c64_in_run = 1;
            c64_run_n = 10000;
            c64_scr_wr(C64_SCR_DEAD, 10000 & 0xFF);
            c64_scr_wr(C64_SCR_DEAD + 1, 10000 >> 8);
            c64_scr_wr(C64_SCR_CARRY, 0);
            c64_scr_wr(C64_SCR_CARRY + 1, 0);
            c64_fpc = 0x0903;               /* the fast core's PC after the LDA */
            if (c64_io_rd(0xD012) != 40)
                fail("the raster-wait row's setup is not at line 40");
            ln = c64_line_now();
            if (ln < 49 || ln > 50)
                fail("the raster wait's skip did not stop at the line the loop "
                     "waits for - it ran past line 50 (4.7.1, 9.10)");
            c64_in_run = 0;
            c64_scr_wr(C64_SCR_DEAD, 0);
            c64_scr_wr(C64_SCR_DEAD + 1, 0);
            c64_frame_cyc = 0;
        }
        ovl_reset_regs();
        c64_map_publish();
    }

    /* --- THE FREE-RUNNING TIMER, WHICH IS WHERE A SIGNED COMPARE HANGS.
     * `LDA #$FF / STA $DC04 / STA $DC05 / LDA #$11 / STA $DC0E` is the
     * standard C64 free-running-timer / RNG idiom AND the state
     * ovl_reset_regs leaves behind, so a $FFFF latch is not an exotic input.
     * `(int)0xFFFF` is -1: the compare was true for every n and the subtract
     * took nothing off, which is an INFINITE LOOP on the UI task, and
     * `(int)(0xFFFF + 1)` is 0, which made the same counter look like the
     * nearest alarm and ran the core one cycle at a time. Nothing else in
     * this file or in c64cputest uses a latch above $7FFF. */
    {
        int i2, spun;
        short n2;
        unsigned short c2;
        ovl_reset_regs();
        c64_map_publish();
        c64_scr_wr(C64_SCR_IRQ, 0);
        c64_io_wr(0xDC04, 0xFF);
        c64_io_wr(0xDC05, 0xFF);
        c64_io_wr(0xDC0E, 0x11);            /* force load, start, continuous */
        if (c64_ta[0] != 0xFFFFu)
            fail("a $FFFF latch did not load into timer A");
        c64_frame_cyc = (int)C64_X63(C64_LATCH_LINE);
        if (c64_alarm_next() != C64_PAL_FRAME - (int)C64_X63(C64_LATCH_LINE))
            fail("a $FFFF timer counter was taken for the nearest alarm - it "
                 "is 65,536 cycles away and the frame end is at most 19,656, "
                 "so the scheduler cast a wrapped value and the slice driver "
                 "then ran the core ONE CYCLE at a time");
        c64_icd[0] = 0;
        c64_advance(C64_PAL_FRAME);         /* ...AND THIS RETURNS */
        if (c64_ta[0] != (unsigned)(0xFFFFu - (unsigned)C64_PAL_FRAME))
            fail("a $FFFF timer did not simply count down by the cycles run");
        if ((c64_icd[0] & 0x01) != 0)
            fail("a $FFFF timer raised an underflow flag inside one frame - "
                 "65,536 is more than 19,656 and it cannot have underflowed");
        /* THE NEGATIVE CONTROL, and it is modelled rather than run for two
         * reasons. The defect it stands for DOES NOT TERMINATE, so it cannot
         * be called; and it is a SIXTEEN-BIT fact - `int` is 32 bits on this
         * host, where `(int)0xFFFF` is a perfectly ordinary 65,535 and
         * nothing wraps at all, which is exactly why every assertion above
         * passed on the host while the target hung. So the control spells the
         * target's widths out with `short`/`unsigned short`: `(short)0xFFFF`
         * is -1, the compare is true for every n, and the subtract takes
         * NOTHING off. If it ever stops inside the cap, this row has stopped
         * testing anything. */
        spun = 0;
        n2 = C64_PAL_FRAME;
        c2 = 0xFFFFu;
        for (i2 = 0; i2 < 1000; i2++) {
            if (!((short)n2 > (short)c2))
                break;
            n2 -= (short)c2 + 1;            /* subtracts ZERO */
            spun++;
        }
        if (spun < 1000)
            fail("the negative control terminated: the signed compare this "
                 "row exists to catch is no longer being modelled");
        ovl_reset_regs();
        c64_map_publish();
    }

    /* --- ...AND THE SAME WRAP IN THE OTHER DIRECTION: a `% cpu` quotient
     * over 32,767 must come back as the CAP, never as a flat 0 beside a live
     * fps. c64_div32 answers up to $FFFF and the clamp has to happen while
     * the value is still unsigned. */
    {
        c64_pause = 1;
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        c64_frames_lo = 0;
        c64_frame_cyc = 0;
        c64_sp_tick = the_ticks;
        c64_sp_lo = 0; c64_sp_hi = 0; c64_sp_fr = 0;
        c64_cyc_hi = 2800;                  /* 2,800 * 65,536 = 183,500,800
                                             * emulated cycles in one second:
                                             * / 5,413 is 33,900, which is past
                                             * 32,767 and NEGATIVE once cast */
        c64_cyc_lo = 0;
        /* ...AND THE FRAMES THAT SPEED IMPLIES, so the OTHER field of the
         * same scenario is evaluated too. 183,500,800 / 19,656 is 9,335 VIC
         * frames, and 9,335 * 182 / 18 is 94,387 - c64_muldiv answers $FFFF
         * on overflow (c64mem.inc:266) and the cast makes that -1, which
         * c64_st_num prints as a flat `     0.0 fps` beside a live
         * `16666% cpu`: the pair of numbers that could not both be true, one
         * field to the right of where the clamp was put. Zeroing this counter
         * is how the row used to walk past its own defect. */
        c64_frames_lo = 9335;
        the_ticks += 18;
        c64_speed_fold();
        if (c64_pct <= 0)
            fail("a `% cpu` quotient over 32,767 printed 0 - the clamp ran "
                 "AFTER the cast, so the value was already negative when it "
                 "was tested, and the row said 0 % beside a live fps (10.2)");
        if (c64_pct != 16666)               /* 30,000 capped, * 10 / 18 */
            fail("a `% cpu` quotient over the cap did not read the cap");
        if (c64_fps10 <= 0)
            fail("an `fps` quotient past c64_muldiv's range printed 0.0 - "
                 "the clamp is missing on the second field, so the row said "
                 "`16666% cpu` beside `0.0 fps` (10.2)");
        if (c64_fps10 != 30000)             /* the same cap, the same place */
            fail("an `fps` quotient over the cap did not read the cap");
        c64_pause = 0;
        c64_cyc_lo = 0; c64_cyc_hi = 0;
        c64_sp_lo = 0; c64_sp_hi = 0;
        c64_sp_tick = the_ticks;
    }

    /* --- THE WALL SLICE ADAPTS ONLY ON A GENUINELY EXHAUSTED SLICE (4.4).
     * Without that rule ordinary typing walks the budget to its cap and the
     * next busy slice is a second of stalled UI task (LESSONS.md 13) - and
     * nothing on the glass says which of the two happened. */
    {
        int seed, i2;
        c64_state = C64_ST_RUN;
        /* ...AND IT IS SEEDED FROM THE TIER, which C64-SPEC §4.4 says and the
         * code did not do: the budget began at C64_SLICE_MIN on every machine
         * and walked up four slices at a time, so the first dozen wakes of a
         * 386 ran 256-cycle slices with a full alarm query, a c64_run shell
         * and a c64_advance over both CIAs, the VIC and the TOD around each.
         * apps/runcpm/runcpm.c:1215's `512 << os88_cpu()`, clamped. */
        c64_budget = 0;
        c64_fastn = 3;
        c64_tier_init();
        if (c64_budget != 512)
            fail("the wall slice was not seeded from os88_cpu() - it is "
                 "`512 << os88_cpu()` clamped into 256..16,384 (§4.4)");
        if (c64_fastn != 0)
            fail("the tier seed left the fast-slice counter where it was");
        c64_budget = C64_SLICE_MIN;
        c64_fastn = 0;
        seed = c64_budget;
        /* A SLICE THAT DID NOT RUN DOES NOT ADAPT. On this machine there is
         * no blocking console read to end a slice early - a C64 always has
         * something to do - so the two ways a wake can end without spending
         * its budget are the machine being PAUSED and the machine having
         * JAMMED, and both are here. Typing into a paused machine is exactly
         * LESSONS.md 13's failure: without the rule, a hundred wakes that ran
         * no cycles at all walk the budget to its cap, and the next wake
         * after the resume is a second of stalled UI task. */
        c64_pause = 1;
        for (i2 = 0; i2 < 100; i2++) {
            os88_onkey('a', 0x1E, win);
            the_ticks += 1;
            os88_onwake(win);
        }
        c64_pause = 0;
        if (c64_budget != seed)
            fail("100 wakes that ran NO cycles moved the wall budget - only "
                 "an EXHAUSTED slice may adapt (4.4)");
        h_cpu_mode = H_CPU_JAM;
        c64_fastn = 0;
        for (i2 = 0; i2 < 8; i2++)
            os88_onwake(win);
        if (c64_budget != seed)
            fail("a JAMMED machine's wakes moved the wall budget");
        if (c64_state != C64_ST_JAM)
            fail("a JAM did not stop the machine");
        c64_state = C64_ST_RUN;
        c64_msg[0] = 0;
        /* ...and four exhausted slices inside ONE host tick double it */
        h_cpu_mode = H_CPU_IDLE;
        c64_fastn = 0;
        c64_budget = C64_SLICE_MIN;
        for (i2 = 0; i2 < 4; i2++)
            os88_onwake(win);               /* the_ticks does not move */
        if (c64_budget != C64_SLICE_MIN * 2)
            fail("four exhausted slices inside one host tick did not double "
                 "the wall budget");
        /* ...and one slice spanning two tick boundaries halves it */
        c64_fastn = 0;
        h_slow_wake = 2;
        os88_onwake(win);
        h_slow_wake = 0;
        if (c64_budget != C64_SLICE_MIN)
            fail("a slice that spanned two host ticks did not halve the "
                 "wall budget");
        h_cpu_mode = H_CPU_IDLE;
    }

    /* --- THE KEYBOARD'S FOUR RULES (7.2) ------------------------------- */
    {
        int i2, before;
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));

        /* RULE 4, AND ITS UNIT IS EMULATED CYCLES. A fresh press survives in
         * the matrix until the emulated machine has had one keyboard-scan
         * interval to see it, even when the key-state map already says the
         * key is up - which is what a press and release between two wakes
         * looks like.
         *
         * THE OLD ROW MEASURED WAKES AND ENSHRINED THE DEFECT: it asserted
         * the key was gone at the SECOND poll. A wake is one wall slice,
         * 256..16,384 cycles against the KERNAL's ~16,421-cycle scan period,
         * so "one wake" is between 1/64 and 1 of a single scan. Under QEMU
         * the core runs thousands of per cent and every press is scanned many
         * times over; on the 4.77 MHz target it runs a few per cent and
         * typing loses characters at random - PERFORMANCE.md's input overrun,
         * which no screendump can show. */
        c64_cyc_lo = 0;
        os88_onkey('a', 0x1E, win);
        c64_kbd_poll();
        if ((c64_matrix[1] & (1 << 2)) == 0)
            fail("a fresh press was not in the matrix at all");
        c64_cyc_lo = 4000;                  /* a quarter of a scan interval:
                                             * several polls' worth of slices
                                             * at the 256-cycle floor */
        c64_kbd_poll();
        c64_kbd_poll();
        c64_kbd_poll();
        if ((c64_matrix[1] & (1 << 2)) == 0)
            fail("a press whose host key is already UP was dropped before "
                 "the emulated machine had scanned the matrix once - the "
                 "freshness guarantee is being counted in WAKES, and one wake "
                 "can be 1/64 of a scan (7.2's rule 4)");
        c64_cyc_lo = (unsigned)C64K_FRESH_CYC + 1;
        c64_kbd_poll();
        if ((c64_matrix[1] & (1 << 2)) != 0)
            fail("a key whose scancode reads UP stayed in the matrix past a "
                 "whole emulated scan interval - the guarantee is not "
                 "BOUNDED, so a program that never reads the matrix makes a "
                 "key stick for ever");
        /* ...and the entry does leave: the down-list is empty, not merely
         * masked out of the matrix */
        if (c64_ndown != 0)
            fail("the released key was left on the down-list");
        c64_cyc_lo = 0;

        /* RULE 5: TAPS ONE AT A TIME. `a` then `s`, both already up on the
         * host: `s` waits in the ring until `a` has had its scan interval and
         * left, and never shares the matrix with it. Then `s` again at once:
         * it waits until `s` has been seen UP for an interval too. Verified
         * to fail with c64_kbd_busy answering 0: both keys are in the
         * matrix at the first poll. */
        c64_kbd_init();
        os88_onkey('a', 0x1E, win);
        os88_onkey('s', 0x1F, win);
        c64_kbd_poll();
        if (c64_ndown != 1 || c64_down[0] != 0x1E)
            fail("two taps went into the matrix together (7.2's rule 5): a "
                 "KERNAL scan reads two keys down as one of them or neither");
        c64_cyc_lo = (unsigned)C64K_FRESH_CYC + 1;
        c64_kbd_poll();                     /* `a` leaves */
        c64_kbd_poll();                     /* ...and `s` goes in */
        if (c64_ndown != 1 || c64_down[0] != 0x1F)
            fail("the waiting tap did not follow once the first had left");
        os88_onkey('s', 0x1F, win);
        c64_cyc_lo = 2u * C64K_FRESH_CYC + 2;
        c64_kbd_poll();                     /* the first `s` leaves */
        c64_kbd_poll();
        if (c64_ndown != 0)
            fail("a second tap of the same key followed with no gap: the "
                 "scan would read one long press (7.2's rule 5)");
        c64_cyc_lo = 3u * C64K_FRESH_CYC + 3;
        c64_kbd_poll();
        if (c64_ndown != 1 || c64_down[0] != 0x1F)
            fail("the second tap of the same key never went in");
        c64_kbd_init();
        c64_cyc_lo = 0;

        /* RULE 3: the matrix is REBUILT from the whole down-list. Two keys
         * that both want the synthetic SHIFT, one released: the survivor
         * must still have it. `!` is 7/0 with SHIFT and `"` is 7/3 with
         * SHIFT (gtk3_sym.vkm), and clearing one key's bits incrementally
         * takes the other's shift with it. */
        c64_kbd_init();
        h_keydown[0x02] = 1;                /* the `1` key, for `!` */
        h_keydown[0x03] = 1;                /* the `2` key, for `"` */
        os88_onkey('!', 0x02, win);
        os88_onkey('"', 0x03, win);
        c64_kbd_poll();
        c64_kbd_poll();
        if ((c64_matrix[C64K_LSHIFT_R] & (1 << C64K_LSHIFT_C)) == 0)
            fail("two shifted keys did not put SHIFT in the matrix");
        h_keydown[0x03] = 0;                /* release the `2` */
        c64_kbd_poll();
        if ((c64_matrix[7] & (1 << 0)) == 0)
            fail("releasing one key took the other out of the matrix");
        if ((c64_matrix[C64K_LSHIFT_R] & (1 << C64K_LSHIFT_C)) == 0)
            fail("releasing one shifted key cleared the SHIFT bit the OTHER "
                 "one still needs - the matrix was cleared incrementally "
                 "instead of rebuilt (7.2's rule 3)");

        /* the bounded overflow: the 17th simultaneous key is DROPPED, not
         * written past the end of a 16-entry list */
        c64_kbd_init();
        for (i2 = 0; i2 < 20; i2++) {
            h_keydown[0x10 + i2] = 1;
            os88_onkey('a' + i2, 0x10 + i2, win);
        }
        before = c64_ndrop;
        c64_kbd_poll();
        if (c64_ndown != C64K_DOWNMAX)
            fail("the down-list did not fill to exactly 16");
        if (c64_ndrop == before)
            fail("the 17th simultaneous key was not counted as dropped - it "
                 "was written past the end of a 16-entry list, or lost "
                 "without anybody noticing");
        for (i2 = 0; i2 < 20; i2++)
            h_keydown[0x10 + i2] = 0;

        /* CTRL+digit comes from the POLL and not from a chord (7.3), which
         * is what keeps VICE's Alt+digit captions */
        c64_kbd_init();
        h_keydown[KSC_CTRL] = 1;
        h_keydown[0x03] = 1;                /* the `2` key */
        c64_kbd_poll();
        if ((c64_matrix[C64K_LCTRL_R] & (1 << C64K_LCTRL_C)) == 0
            || (c64_matrix[7] & (1 << 3)) == 0)
            fail("CTRL+2 did not reach the matrix from the once-per-wake "
                 "poll - a colour code needs a chord this port refuses to "
                 "steal");
        h_keydown[KSC_CTRL] = 0;
        h_keydown[0x03] = 0;

        /* Ctrl+H, Ctrl+I and Ctrl+M are told from BS, Tab and CR by their
         * SCAN CODE and by nothing else (7.3) */
        c64_kbd_init();
        os88_onkey(9, KSC_TAB, win);        /* Tab = C= */
        c64_kbd_poll();
        if ((c64_matrix[C64K_LCBM_R] & (1 << C64K_LCBM_C)) == 0)
            fail("Tab did not reach the C= key");
        c64_kbd_init();
        os88_onkey(9, KSC_CTRLI, win);      /* the same ascii, Ctrl+I */
        c64_kbd_poll();
        if ((c64_matrix[C64K_LCTRL_R] & (1 << C64K_LCTRL_C)) == 0)
            fail("Ctrl+I arrived as Tab - the BIOS fold was not resolved on "
                 "the scan code (7.3)");

        /* A BARE SHIFT AND A BARE CTRL ARE IN THE MATRIX (keyboard.c:474,
         * :505). No other key is needed: a game polling $DC01 for the shift
         * key - the second fire button of a thousand of them - used to see
         * nothing at all, because SHIFT went in only when some other key's
         * mapping asked for it. */
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_LSHIFT] = 1;
        c64_kbd_poll();
        if ((c64_matrix[C64K_LSHIFT_R] & (1 << C64K_LSHIFT_C)) == 0)
            fail("a bare SHIFT is not in the matrix - VICE latches the "
                 "physical modifier with no other key down (keyboard.c:474)");
        h_keydown[KSC_LSHIFT] = 0;
        h_keydown[KSC_CTRL] = 1;
        c64_kbd_poll();
        if ((c64_matrix[C64K_LCTRL_R] & (1 << C64K_LCTRL_C)) == 0)
            fail("a bare CTRL is not in the matrix (keyboard.c:505)");
        if ((c64_matrix[C64K_LSHIFT_R] & (1 << C64K_LSHIFT_C)) != 0)
            fail("SHIFT stayed in the matrix after the host key came up - "
                 "the modifier is a LEVEL, not a latch");
        h_keydown[KSC_CTRL] = 0;
        /* ...AND DESHIFT STILL WINS. `*` is 6/1 with the .vkm's 0x10, which
         * means SHIFT actively RELEASED, and that is VICE's
         * `!virtual_deshift` in the same expression. */
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_LSHIFT] = 1;
        os88_onkey('*', 0x09, win);
        c64_kbd_poll();
        if ((c64_matrix[C64K_LSHIFT_R] & (1 << C64K_LSHIFT_C)) != 0)
            fail("a DESHIFT mapping did not clear the host's own SHIFT");
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));

        /* THE KEYSET TAKES ITS KEYS, AND THEY DO NOT REACH THE MATRIX.
         * VICE's keyboard_key_pressed (src/keyboard.c:788-798) walks the
         * ports mapped to a keyset, calls joystick_check_set for each and
         * RETURNS before kbd_queue_pushkey when one takes the key: a keyset
         * key drives the stick and is not typed. This port shipped the
         * joystick without that rule, so the four cursor keys drove port 2
         * AND entered the matrix at the same time - a game polling $DC00 got
         * phantom cursor presses out of $DC01, and moving the stick in BASIC
         * walked the cursor. Right is 0/2 in the .vkm and Down is 0/7. */
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_RIGHT] = 1;
        os88_onkey(0, KSC_RIGHT, win);
        c64_kbd_poll();
        if ((c64_joy2 & (1 << 3)) == 0)
            fail("the right cursor key did not drive port 2 RIGHT (§8)");
        if ((c64_matrix[0] & (1 << 2)) != 0)
            fail("a keyset key ALSO entered the keyboard matrix - VICE "
                 "returns before kbd_queue_pushkey for a key a keyset took "
                 "(keyboard.c:788-798), so the stick does not type");
        /* THE NEGATIVE CONTROL, and it is VICE's own first test in
         * joystick_check_set (joystick.c:598-601): with the keyset DISABLED
         * the very same key falls through to the matrix. */
        c64_keyset = 0;
        c64_kbd_poll();
        if ((c64_matrix[0] & (1 << 2)) == 0)
            fail("with the keyset OFF the cursor key did not reach the "
                 "matrix - the consumption is unconditional, so the row "
                 "above proves nothing");
        if (c64_joy2 != 0)
            fail("with the keyset OFF the cursor key still drove port 2");
        c64_keyset = 1;
        /* ...AND CTRL IS THE ONE DEPARTURE §8 STATES: it is both fire and
         * the CTRL key, so it still reaches the matrix. */
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_CTRL] = 1;
        h_keydown[0x03] = 1;                /* Ctrl+2, a colour code */
        c64_kbd_poll();
        if ((c64_joy2 & (1 << 4)) == 0)
            fail("Ctrl did not fire port 2");
        if ((c64_matrix[C64K_LCTRL_R] & (1 << C64K_LCTRL_C)) == 0
            || (c64_matrix[7] & (1 << 3)) == 0)
            fail("the keyset consumed CTRL - it is both fire and the CTRL "
                 "key, which is the one departure §8 states");
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));

        /* RESTORE is not a matrix key: Page_Up raises an NMI EDGE (7.4) */
        c64_kbd_init();
        c64_scr_wr(C64_SCR_IRQ, 0);
        os88_onkey(0, KSC_PGUP, win);
        c64_kbd_poll();
        if ((c64_scr_rd(C64_SCR_IRQ) & 0x02) == 0)
            fail("Page_Up did not raise the RESTORE NMI (7.4)");
        c64_scr_wr(C64_SCR_IRQ, 0);
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
    }

    /* --- what CIA1 answers, which is the only reason the matrix exists --- */
    {
        c64_kbd_init();
        ovl_reset_regs();
        c64_map_publish();
        c64_cia[0][0x02] = 0xFF;            /* PRA all outputs */
        c64_cia[0][0x03] = 0x00;            /* PRB all inputs */
        h_keydown[0x1E] = 1;
        os88_onkey('a', 0x1E, win);         /* A is row 1, column 2 */
        c64_kbd_poll();
        c64_cia[0][0x00] = (unsigned char)~(1 << 1);   /* select row 1 */
        if ((c64_kbd_pb() & (1 << 2)) != 0)
            fail("CIA1 PRB did not pull column 2 low with A held down");
        c64_cia[0][0x00] = (unsigned char)~(1 << 0);   /* the wrong row */
        if ((c64_kbd_pb() & (1 << 2)) == 0)
            fail("CIA1 PRB pulled a column low for a row nothing selected");
        h_keydown[0x1E] = 0;
        c64_kbd_init();
        memset(h_keydown, 0, sizeof(h_keydown));
        ovl_reset_regs();
        c64_map_publish();
    }

    /* --- A PAUSED MACHINE DOES NOT SPIN THE UI TASK (SPEC.md 74.1) -------
     * "A handler re-posts itself only while it has work - a wake round trip
     * is at least one task switch (693 us), so a handler that always
     * re-posts spins the UI task at ~1,400 wakes a second." Alt+P sets
     * c64_pause and NOT c64_state, so a paused machine is still C64_ST_RUN:
     * it ran nothing, drew nothing, and re-posted anyway. */
    {
        c64_state = C64_ST_RUN;
        c64_pause = 1;
        c64_msg[0] = 0;
        c64_dirty_any = 0;
        /* THE NEGATIVE CONTROL FIRST: the condition wave 2 shipped, in this
         * exact state, must ask for a wake - otherwise the row below proves
         * nothing about the fix. */
        if (!(c64_dirty_any || c64_state == C64_ST_RUN || c64_msg[0] != 0))
            fail("the negative control did not re-post: the shipped "
                 "condition is no longer being modelled here");
        if (c64_wants_wake())
            fail("a PAUSED machine asked for another wake - one wake round "
                 "trip is a task switch and ~1 ms of far calls, for a "
                 "machine the user deliberately stopped (SPEC.md 74.1)");
        c64_kick = 1;
        os88_onwake(win);
        if (c64_kick)
            fail("the slice driver re-posted from a paused machine");
        c64_pause = 0;
        if (!c64_wants_wake())
            fail("a RUNNING machine stopped asking for wakes");
        c64_msg[0] = 0;
        c64_dirty_any = 0;
        c64_state = C64_ST_JAM;
        if (c64_wants_wake())
            fail("a JAMMED machine asked for another wake");
        /* ...AND A MESSAGE IS NOT WORK. `c64_msg[0] != 0` used to be tested
         * FIRST, above both of those, so a stopped machine re-posted for the
         * whole five-second life of every message with nothing inside the
         * wake but a re-read of the clock: ~7,000 round trips, ~4.8 s of the
         * SHARED UI task, and Alt+P made it reachable on purpose. */
        c64_dirty_any = 0;
        strcpy(c64_msg, "Paused.");
        c64_msg_until = the_ticks + 90;
        if (!(c64_dirty_any || c64_msg[0] != 0))
            fail("the negative control did not re-post: the shipped "
                 "condition is no longer being modelled here");
        if (c64_wants_wake())
            fail("a JAMMED machine asked for a wake because a MESSAGE was "
                 "up - there is nothing to do inside it and a round trip is "
                 "693 us (SPEC.md 74.1, C64-SPEC §10.1)");
        c64_state = C64_ST_RUN;
        c64_pause = 1;
        if (c64_wants_wake())
            fail("a PAUSED machine asked for a wake because a MESSAGE was up");
        c64_pause = 0;
        c64_msg[0] = 0;
        c64_state = C64_ST_RUN;
    }

    /* --- THE SID'S HERTZ, AND THE NOTE A STOPPED MACHINE MUST NOT HOLD ----
     * A 6581 at the PAL dot clock sounds Fn * 985248 / 2^24 Hz. The port used
     * to round twice - into a 12-bit number and again on the way out - which
     * near the bottom of the range is a whole hertz where a hertz is the
     * unit, and the bottom of the range is exactly where the 20 Hz floor
     * decides whether the note is played at all. */
    {
        int t0;
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_abt = 0;
        c64_msg[0] = 0;
        c64_sid_said = 0;
        h_tone_refuse = 0;
        c64_io_wr(0xD418, 15);              /* the master volume up: a SID at
                                             * volume 0 is silent (11.4.2) */
        c64_io_wr(0xD400, 0xFF);            /* F = $FFFF, the top of the range */
        c64_io_wr(0xD401, 0xFF);
        c64_io_wr(0xD404, 0x11);            /* ...and the gate on */
        os88_onwake(win);
        if (h_tone_hz != 3848)
            fail("F = $FFFF is 3,848 Hz (65535 * 985248 / 2^24 = 3848.6) and "
                 "the port answered something else");
        c64_io_wr(0xD400, 0x55);            /* F = $0155 = 341: 20.02 Hz, the
                                             * first note ABOVE the floor */
        c64_io_wr(0xD401, 0x01);
        os88_onwake(win);
        if (h_tone_hz != 20)
            fail("F = $0155 is 20 Hz and must clear the 20 Hz floor - a "
                 "second rounding answered 19 and the note was dropped");
        c64_io_wr(0xD400, 0x54);            /* ...and one step below it */
        os88_onwake(win);
        if (h_tone_hz != 0)
            fail("a note below the 20 Hz floor was played");

        /* ...AND A STOPPED MACHINE IS QUIET. The tone is played with duration
         * 0, which SPEC.md 34 holds until something takes it down, and the
         * only thing that ever did was the guest closing the gate: Alt+P, a
         * JAM, a reset and the About panel all left the last note sounding
         * for ever. VICE suspends the sound at each of those points. */
        c64_io_wr(0xD400, 0x00);
        c64_io_wr(0xD401, 0x10);
        os88_onwake(win);
        if (h_tone_hz == 0)
            fail("the fixture note never sounded, so the silence below "
                 "proves nothing");
        do_cmd(C64_I_PAUSE, C64_M_PREF, win);
        if (h_tone_hz != 0)
            fail("Alt+P left the last SID note sounding - a paused machine "
                 "holds a tone with duration 0 for ever (11.4)");
        t0 = h_tone_hz;
        do_cmd(C64_I_PAUSE, C64_M_PREF, win);   /* ...and out again */
        os88_onwake(win);
        if (h_tone_hz == 0)
            fail("resuming did not put the machine's CURRENT note back - the "
                 "stop must re-arm c64_sid_dirty, not remember the note");
        (void)t0;
        /* ...and the JAM path, which is the same fact with no way back */
        c64_jam();
        if (h_tone_hz != 0)
            fail("a JAM left the last SID note sounding - the 6510 is dead "
                 "and nothing will ever close the gate");
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;
        c64_menu_state();
    }

    /* --- FRAME SKIP (9.13): a second in which flushing took more than
     * half the ticks adds one between flushes, a quarter or less takes one
     * away, never past C64_FS_MAX - and nothing moves inside the second.
     * Verified red with c64_fs_adapt's two thresholds swapped. */
    {
        int j;
        unsigned t = 5000;
        c64_fs_extra = 0;
        c64_fs_t0 = t;
        c64_fs_cost = 12;
        c64_fs_adapt(t + 10);
        if (c64_fs_extra != 0)
            fail("frame skip moved before its second was up (9.13)");
        c64_fs_adapt(t + 18);
        if (c64_fs_extra != 1)
            fail("a second two-thirds spent flushing did not add a tick "
                 "between flushes (9.13)");
        for (j = 0; j < 20; j++) {
            t = c64_fs_t0;
            c64_fs_cost = 15;
            c64_fs_adapt(t + 18);
        }
        if (c64_fs_extra != C64_FS_MAX)
            fail("frame skip is not capped at C64_FS_MAX");
        c64_fs_cost = 2;
        c64_fs_adapt(c64_fs_t0 + 18);
        if (c64_fs_extra != C64_FS_MAX - 1)
            fail("a quiet second did not take a tick back (9.13)");
        c64_fs_extra = 0;
        c64_fs_cost = 0;
    }

    /* --- WHICH VOICE THE SPEAKER PLAYS, AND THE FM CHIP (11.4.2, 11.4.3) --
     * Voice 1 held as a pad while voice 2 plays the melody: the speaker
     * follows the note that started LAST, not voice 1. Verified red with
     * c64_sid_voice1 back on voice 1 alone. Then the same SID on an FM
     * chip: each voice its own channel, 440 Hz as block 4 / F-number 580,
     * the key on while the gate is, off when it closes. Verified red with
     * ovl_fm_update's F-number arithmetic off by one block. */
    {
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;
        h_tone_refuse = 0;
        c64_io_wr(0xD418, 15);
        c64_io_wr(0xD400, 0x00);            /* voice 1: F = $1000, held */
        c64_io_wr(0xD401, 0x10);
        c64_io_wr(0xD404, 0x21);            /* sawtooth, gate */
        os88_onwake(win);
        c64_io_wr(0xD407, 0x45);            /* voice 2: F = $1D45, A4 */
        c64_io_wr(0xD408, 0x1D);
        c64_io_wr(0xD40B, 0x41);            /* pulse, gate: a NEW note */
        os88_onwake(win);
        if (h_tone_hz < 439 || h_tone_hz > 440)     /* (7493 x 3848 / 65535) */
            fail("with voice 1 held and a new note on voice 2, the speaker "
                 "did not play the new note (11.4.2)");
        c64_io_wr(0xD40B, 0x40);            /* its gate closes */
        os88_onwake(win);
        if (h_tone_hz != 240)               /* $1000 = 240.5 Hz */
            fail("when the newest note ends the speaker did not go back to "
                 "the voice still gated (11.4.2)");
        c64_io_wr(0xD404, 0x20);
        os88_onwake(win);
        if (h_tone_hz != 0)
            fail("with no gate open the speaker kept a tone");

        h_fm_chip = 1;                      /* ...and on a machine with OPL */
        memset(h_opl, 0, sizeof(h_opl));
        c64_snd_fm = c64_opl_open();
        os88_memset(c64_fmsh, 0xFF, sizeof(c64_fmsh));
        c64_io_wr(0xD40B, 0x41);            /* voice 2 again: A4, pulse */
        c64_io_wr(0xD40C, 0x09);            /* attack 0, decay 9 */
        c64_io_wr(0xD40D, 0xA4);            /* sustain 10, release 4 */
        os88_onwake(win);
        if (h_opl[0xA1] != (580 & 0xFF) || h_opl[0xB1] != (0x20 | (4 << 2) | (580 >> 8)))
            fail("voice 2's 440 Hz did not reach OPL channel 1 as block 4, "
                 "F-number 580, keyed on (11.4.3)");
        if (h_opl[0x21] != 0x22 || h_opl[0x64] != ((15 << 4) | 6)
                || h_opl[0x84] != (((15 - 10) << 4) | 8))
            fail("voice 2's pulse patch or its ADSR did not reach channel 1");
        c64_io_wr(0xD40B, 0x40);
        os88_onwake(win);
        if (h_opl[0xB1] & 0x20)
            fail("voice 2's gate closed and channel 1 stayed keyed on");
        c64_snd_fm = 0;
        h_fm_chip = 0;
    }

    /* --- A REFUSED SPEAKER GRANT IS NOT PERMANENT (11.4, SPEC.md 34.3) ---
     * os88_snd_tone answers -1 when another instance holds the grant. The
     * driver cleared the latch BEFORE the call and threw all three returns
     * away - and the latch is raised in exactly ONE place, a write to
     * $D400-$D41C - so a single refusal at the wrong moment silenced the
     * emulated SID until the guest next poked a SID register, which for the
     * ordinary `set the frequency, gate on, leave it` is never. Permanent
     * silence with nothing said about it is what SPEC.md 47 forbids. */
    {
        int t0, i3;
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;
        c64_sid_said = 0;
        c64_sid_tries = 0;
        h_tone_refuse = 1;
        c64_io_wr(0xD401, 0x10);            /* voice 1's frequency high byte */
        c64_io_wr(0xD404, 0x11);            /* ...and the gate on */
        if (!c64_sid_dirty)
            fail("a SID register write did not raise the tone latch");
        t0 = n_tone;
        os88_onwake(win);
        if (n_tone == t0)
            fail("the wake did not ask for the tone at all");
        if (!c64_sid_dirty)
            fail("a REFUSED os88_snd_tone cleared the latch - the emulated "
                 "SID is then silent for the rest of the session, because "
                 "only a SID REGISTER WRITE ever raises it again (11.4)");
        h_tone_refuse = 0;                  /* the other instance let go */
        t0 = n_tone;
        os88_onwake(win);
        if (n_tone == t0)
            fail("the refused tone was not re-asked on the next wake");
        if (c64_sid_dirty)
            fail("a GRANTED tone left the latch up - one far call a wake, "
                 "for ever");
        /* ...AND THE RETRY IS BOUNDED. A speaker held for good must cost
         * C64_SID_TRIES far calls and not one a wake for the session, and
         * the fact is said ONCE (SPEC.md 47), never once a wake. */
        h_tone_refuse = 1;
        c64_io_wr(0xD401, 0x20);
        for (i3 = 0; i3 < 60 && c64_sid_dirty; i3++)
            os88_onwake(win);
        if (c64_sid_dirty)
            fail("the retry never gave up - a machine whose speaker is held "
                 "asks once a wake for ever");
        if (i3 < 2)
            fail("the retry gave up on the FIRST refusal - that is the "
                 "permanent silence this row exists to catch");
        if (strcmp(last_toast, "The speaker is busy - no SID sound.") != 0)
            fail("the SID gave up SILENTLY - a refusal the user cannot see "
                 "is the shape SPEC.md 47 forbids");
        t0 = n_tone;
        for (i3 = 0; i3 < 10; i3++)
            os88_onwake(win);
        if (n_tone != t0)
            fail("the driver went on asking for the tone after it had given "
                 "up - the bound is not a bound");
        /* ...and the NEXT SID register write re-arms it, which is the one
         * event that means the guest still wants a sound. */
        c64_io_wr(0xD401, 0x30);
        if (!c64_sid_dirty)
            fail("a SID write after a give-up did not re-arm the retry");
        h_tone_refuse = 0;
        os88_onwake(win);
        if (c64_sid_dirty)
            fail("the re-armed retry did not take the grant");
        c64_msg[0] = 0;
        c64_st_ok = 0;
        c64_sh_inval();
        dmg_whole = 1;
        do_paint(win);
    }

    /* --- A JAM SAYS SO IN VICE'S OWN WORDS, AND GOES ON SAYING IT (4.5,
     * 10.1). src/maincpu.c:612 formats `"   " CPU_STR ": JAM at $%04X   "`
     * with CPU_STR = `Main CPU` (src/6510core.c:45); the padding is the
     * D'OH! dialog's, the line is not ours to invent. And it is a PERMANENT
     * row state beside `C64.ROM missing`, not a five-second message: the
     * glass showed a jammed machine and an idle one identically. */
    {
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;
        h_cpu_mode = H_CPU_JAM;
        c64_m.pc = 0xE5CF;
        os88_onwake(win);
        h_cpu_mode = H_CPU_IDLE;
        if (c64_state != C64_ST_JAM)
            fail("a JAM did not stop the machine");
        if (strcmp(c64_jamline, "Main CPU: JAM at $E5CF") != 0)
            fail("the JAM line is not VICE's own (src/maincpu.c:612 + "
                 "src/6510core.c:45) - it was typed rather than read");
        /* AND IT IS NOT ALSO A MESSAGE. Routed through c64_say the same 22
         * glyphs went up as msg == 1, and five seconds later the deadline
         * cleared them, msg became 3, `msg != c64_st_shown` forced the full
         * path, and the row was filled BLACK and re-lettered with THE SAME
         * LINE AT THE SAME PLACE - ~21 ms that change no pixel, and on the
         * glass a row that blanks and re-letters five seconds after the
         * event (PERFORMANCE.md rule 2's erase-then-letter). */
        if (c64_msg[0] != 0)
            fail("the JAM line went up as a five-second MESSAGE as well as "
                 "the permanent row state - the row is then drawn TWICE, "
                 "identically, with a black fill between");
        if (strcmp(last_toast, c64_jamline) != 0)
            fail("the JAM line did not take §9.8's second route - c64_say "
                 "was carrying the toast and it went with it");
        c64_st_ok = 0;
        c64_sh_inval();
        dmg_whole = 1;
        h_last_run[0] = 0;
        h_runs_clear();
        do_paint(win);
        {
            const char *line = h_run_at(c64_gox);
            if (line == 0 || strcmp(line, c64_jamline) != 0)
                fail("the status row does not carry the jam line - a jammed "
                     "machine reads exactly like an idle one, and a permanent "
                     "condition is a LINE and not a message (§1.4's rule, "
                     "§4.5)");
            /* ...AND THE TWO LAMPS ARE STILL ON THE ROW UNDER IT (§10.1). A
             * message used to own the whole row, so `P` - the one indicator
             * that reports the PAUSE state - was off the glass for exactly as
             * long as the machine was paused. */
            if (h_run_at(c64_gox + C64_ST_WRPX) == 0
                || h_run_at(c64_gox + C64_ST_PAUX) == 0)
                fail("a permanent row LINE hid the warp and pause lamps - "
                     "the row's 40 field cells are the message's, the two "
                     "lamp cells are not (§10.1)");
        }
        /* ...AND IT IS DRAWN ONCE. Five seconds of nothing happening must
         * cost nothing at all on this row. */
        cost_begin();
        the_ticks += 200;
        flush_now();
        if (n_fill - b_fill != 0 || n_run - b_run != 0)
            fail("the jam row repainted itself when five seconds it never "
                 "used were up - 1 fill + 22 identical glyph cells, ~21 ms "
                 "for no pixel");
        /* THE NEGATIVE CONTROL: put the row in exactly the state the message
         * route left it in - drawn as msg == 1, then found as msg == 3 - and
         * show that it DOES repaint, so the row above proves something. */
        cost_begin();
        c64_st_shown = 1;
        c64_dirty_any = 1;
        flush_now();
        if (n_fill - b_fill == 0 || n_run_cells - b_run_cells < 22)
            fail("the negative control did not repaint: the second draw this "
                 "row exists to catch is no longer reachable, so the row "
                 "above is not measuring it");
        /* ...and Advance frame is GREYED on a jammed machine (§47) */
        if (c64_pref_items[C64_I_ADVANCE][0] != OS88_MENU_DIS)
            fail("Advance frame stayed LIVE on a JAMMED machine - a black "
                 "item whose body returns in silence is the one shape "
                 "SPEC.md 47 forbids");
        /* ...AND THE CHORD DOES NOT WALK ROUND THE GREYING. The kernel never
         * dispatches a disabled item, but os88_onkey dispatches Alt+Shift+P
         * itself (§7.5) - and it used to reach ovl_advance_frame's state
         * guard and return in silence, which is §47's shape by the other
         * route. */
        c64_adv = 0;
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_LSHIFT] = 1;
        os88_gfx_lock(); os88_onkey(0, 0x19, win); os88_gfx_unlock();
        if (c64_adv)
            fail("Alt+Shift+P reached Advance frame while the item was "
                 "GREYED - the chord must not dispatch what the menu will "
                 "not (SPEC.md 47)");
        memset(h_keydown, 0, sizeof(h_keydown));

        /* ...AND EDIT > PASTE IS GREYED ON A JAM WHILE EDIT > COPY IS NOT.
         * c64_paste_feed runs only from the C64_ST_RUN arm of os88_onwake and
         * c64_wants_wake answers 0 here, so a paste on a jammed machine is
         * bytes queued in silence that NOTHING will ever type - for the rest
         * of the session. Copy stays live because the frozen screen is real
         * and VICE's Copy copies what is displayed. */
        if (c64_edit_items[C64_I_PASTE][0] != OS88_MENU_DIS)
            fail("Edit > Paste stayed LIVE on a JAMMED machine - the queue is "
                 "drained only from the C64_ST_RUN arm of os88_onwake, so it "
                 "is a silent no-op (SPEC.md 47)");
        if (c64_edit_items[C64_I_COPY][0] == OS88_MENU_DIS)
            fail("Edit > Copy was greyed on a JAM - the frozen screen is real "
                 "and VICE copies what is displayed");
        /* ...and the CHORD does not walk round it either (§7.5's Alt+Insert
         * is dispatched by os88_onkey where the BIOS passes it). */
        c64_paste_n = 0;
        h_clip_n = 8;
        memcpy(h_clip, "abcdefgh", 8);
        os88_gfx_lock(); os88_onkey(0, 0xA2, win); os88_gfx_unlock();
        if (c64_paste_n != 0)
            fail("Alt+Insert queued a paste into a JAMMED machine while the "
                 "item was GREYED");
        h_clip_n = -1;
        c64_msg[0] = 0;

        /* ...AND THE PRODUCT'S OWN WAY OUT OF A JAM IS THE ONE THAT HAS TO
         * UN-GREY IT. This row used to set c64_state itself and then call
         * c64_menu_state() by hand, so it modelled the fix rather than the
         * program: File > Reset machine CPU cleared C64_ST_JAM - and with it
         * the permanent line that was the FACT behind the greying - and left
         * Advance frame black for the rest of the session, un-greying itself
         * later only when some unrelated latch happened to call
         * c64_menu_state. A greying whose fact is off the glass is exactly
         * what SPEC.md 47 forbids. */
        do_reset(C64_I_RESETCPU, win);
        if (c64_state != C64_ST_RUN)
            fail("File > Reset machine CPU did not clear the JAM");
        if (c64_pref_items[C64_I_ADVANCE][0] == OS88_MENU_DIS)
            fail("Advance frame stayed GREYED after a Reset cleared the JAM "
                 "- and the jam LINE, which is the fact that greyed it, had "
                 "already left the row (SPEC.md 47)");
        h_runs_clear();
        c64_st_ok = 0;
        c64_dirty_any = 1;
        flush_now();
        if (h_run_at(c64_gox) != 0
            && strcmp(h_run_at(c64_gox), c64_jamline) == 0)
            fail("the jam line survived the reset that cleared the jam");
        c64_msg[0] = 0;
        c64_st_ok = 0;
    }

    /* --- Preferences > Advance frame is LIVE (§11.1): it runs to the next
     * VIC frame end and stops, which is what the alarm model this wave
     * landed made possible - and it is why the item stopped being greyed. */
    {
        int i2;
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_adv = 0;
        c64_frame_cyc = 5000;
        c64_frames_lo = 0;
        c64_msg[0] = 0;
        /* FROM A RUNNING MACHINE THE ITEM ONLY PAUSES, AND ADVANCES NOTHING.
         * VICE's action is the whole of src/arch/gtk3/actions-speed.c:72-80
         * (identically src/arch/gtk3/ui.c:2735-2743):
         *   if (ui_pause_active()) vsyncarch_advance_frame(); else
         *   ui_pause_enable();
         * The first draft set the request unconditionally, so from a running
         * machine this port ran 19,656 emulated cycles and THEN paused -
         * invented semantics for the one live item wave 2 added, and the one
         * whose body had not been read (LESSONS.md 1). */
        do_cmd(C64_I_ADVANCE, C64_M_PREF, win);
        if (!c64_pause)
            fail("Advance frame did not PAUSE a running machine - VICE's "
                 "action pauses when the machine is running and advances "
                 "only an already-paused one (actions-speed.c:72-80)");
        if (c64_adv)
            fail("Advance frame raised a frame request from a RUNNING "
                 "machine");
        if (c64_frames_lo != 0)
            fail("Advance frame ran emulated cycles from a running machine");
        if (strcmp(last_toast, "Paused.") != 0)
            fail("Advance frame pausing a running machine said nothing");
        /* ...AND FROM THE PAUSED MACHINE IT RUNS ONE FRAME AND STOPS. THE
         * COMMAND RAISES A REQUEST AND RUNS NOTHING: os88_oncmd is dispatched
         * under the gfx lock and a PAL frame is 19,656 emulated cycles, which
         * on the target is a fraction of a second of stopped desktop. The
         * slice driver serves it, in the wall slices it already sizes - so it
         * may take more than one wake, and the driver must go on asking for
         * them while it does. */
        do_cmd(C64_I_ADVANCE, C64_M_PREF, win);
        if (!c64_adv)
            fail("Advance frame did not raise a request from a PAUSED "
                 "machine - it is the only state VICE advances from");
        if (c64_frames_lo != 0)
            fail("Advance frame ran the frame under the menu's gfx lock");
        for (i2 = 0; i2 < 200 && c64_adv; i2++) {
            if (!c64_wants_wake())
                fail("the driver stopped asking for wakes with an Advance "
                     "frame still outstanding - the frame never finishes");
            the_ticks += 1;
            os88_onwake(win);
        }
        if (c64_adv)
            fail("Advance frame never reached the frame end");
        if (!c64_pause)
            fail("Advance frame did not leave the machine paused");
        if (c64_frames_lo != 1)
            fail("Advance frame did not reach the next VIC frame end - it is "
                 "one alarm query, and greying it named a raster accumulator "
                 "that this wave landed (SPEC.md 47)");
        /* ...AND THE CHORD IS THE OTHER ROUTE. The BIOS hands Alt+Shift+P the
         * same ascii/scan pair as Alt+P, so without reading the shift level
         * the chord §11.1 advertises for Advance frame RESUMED a paused
         * machine - the opposite of VICE, in the one state it advances from
         * (§7.5: every chord the menu advertises is dispatched here). */
        c64_adv = 0;
        c64_pause = 1;
        c64_frames_lo = 0;
        c64_frame_cyc = 5000;
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_LSHIFT] = 1;
        os88_onkey(0, 0x19, win);
        if (!c64_pause || !c64_adv)
            fail("Alt+Shift+P resumed the machine instead of advancing one "
                 "frame - it is Advance frame's chord (§11.1)");
        memset(h_keydown, 0, sizeof(h_keydown));
        c64_adv = 0;
        os88_onkey(0, 0x19, win);           /* ...and plain Alt+P still is
                                             * Pause emulation */
        if (c64_pause)
            fail("Alt+P stopped toggling Pause emulation");
        c64_pause = 0;
        c64_adv = 0;
        c64_msg[0] = 0;
        c64_state = C64_ST_RUN;
        c64_kbd_init();
    }

    c64_state = C64_ST_RUN;
    c64_sh_inval();
    dmg_whole = 1;
    do_paint(win);
    audit("after the wave-2 clock and keyboard checks");

    /* ======================================================================
     * WAVE 3 - EDIT > COPY AND EDIT > PASTE (C64-SPEC §7.7, §11.1)
     * ==================================================================== */
    {
        int i3, j3, n3, fed;
        unsigned char h_fed[64];
        const char *ln;

        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;
        h_clip_n = -1;
        h_clip_refuse = 0;



        /* --- Copy: the screen matrix, trimmed, one '\n' a row ------------
         * The letters are the point. PETSCII $41-$5A is the LOWER case
         * (charset.c:156-158), whichever character set the VIC is drawing, so
         * `HELLO` on the glass is `hello` on the clipboard - VICE's answer,
         * transcribed, and the one a remembered version of this gets wrong. */
        for (i3 = 0; i3 < C64_CELLS; i3++)
            c64_wr(c64_mbase + i3, 0x20);   /* screen code 0x20 is a space */
        c64_wr(c64_mbase + 0, 0x08);        /* H */
        c64_wr(c64_mbase + 1, 0x05);        /* E */
        c64_wr(c64_mbase + 2, 0x0C);        /* L */
        c64_wr(c64_mbase + 3, 0x0C);        /* L */
        c64_wr(c64_mbase + 4, 0x8F);        /* O, REVERSED - the inverse bit is
                                             * not a character (charset.c:204) */
        c64_wr(c64_mbase + 5, 0x40);        /* ...AND ONE GRAPHICS CELL, which
                                             * is the arm nothing used to
                                             * drive. Screen code $40 is
                                             * PETSCII $60, which
                                             * petcii_fix_dupes takes to $C0 -
                                             * outside every letter range and
                                             * not printable ASCII - so it is
                                             * the UNMAPPED arm, and VICE's
                                             * unmapped is ASCII_UNMAPPED,
                                             * which is '.' (charset.c:126).
                                             * It is NOT '?': edit_copy_action
                                             * substitutes one only for a byte
                                             * that is neither \r/\n nor
                                             * printable, and '.' is printable,
                                             * so that pass never fires and
                                             * VICE's Copy output cannot
                                             * contain a '?' at all. Screen
                                             * codes $40, $5B-$5F, $60 and
                                             * $7B-$7F reach this arm - the
                                             * graphics cells, i.e. what a user
                                             * copying a game screen or a
                                             * PETSCII drawing hits */
        c64_wr(c64_mbase + 40 + 39, 0x21);  /* row 1, last column: a '!' that
                                             * must survive the trim */
        b_blit = n_blit; b_fill = n_fill; b_run = n_run;
        n_ram_rd = 0;
        cost_begin();
        do_edit(C64_I_COPY, win);
        /* ...AND THE COST ROW IS NOT TAKEN ON THIS FIXTURE. This screen is
         * six characters and a '!' - a clipboard of 71 bytes - and the row is
         * called `of the whole screen`, so it is taken below on a screen that
         * is one. The put's own byte copy is real and is charged, and it does
         * not show on a screen that is empty. */
        /* EDIT > COPY DRAWS NOTHING, and its whole cost is the matrix pull,
         * the per-cell conversion, the clipboard's own byte copy and the
         * overlay bridge - FOUR crossings, counted from the structure because
         * a host build has no overlay:
         *
         *   os88_oncmd  -> ovl_cmd          (the runtime's far entry)
         *   ovl_cmd     -> ovl_copy         (module to module: the shipped
         *                                    build makes this one too -
         *                                    `call far [cc_ovm_ovl_copy]`,
         *                                    build/c64.gen.asm - and the
         *                                    hand-written 3 did not count it)
         *   ovl_copy    -> c64_copy_screen  (out to the resident loop)
         *   ovl_copy    -> os88_clip_put    (out to the SDK cell)
         *
         * The first draft made 2,000 of them, two a cell, and this model
         * charged one near call for the lot (C64-SPEC §9.7). The arithmetic
         * error in 3-vs-4 is 58 us of 83 ms; the term exists so that the NEXT
         * ovl_* that loops cannot be priced at zero, and the enumeration
         * beside each row is how the next author decides what to charge.
         *
         * ALL OF IT IS HELD-LOCK TIME: os88_oncmd is dispatched UNDER the gfx
         * lock (os88.h:566), so this number is the desktop stopped, and it is
         * why the loop is resident and table-driven rather than in the module
         * with the command. */
        if (n_blit != b_blit || n_fill != b_fill || n_run != b_run)
            fail("Edit > Copy drew on the glass - it reads the matrix and "
                 "hands the text to the clipboard, and the window it came "
                 "from has not changed");
        /* THE NEGATIVE CONTROL FOR THE WHOLE FIX: the matrix comes out one
         * ROW per call and the conversion is a table, so a Copy makes 25
         * c64_zcopy_out calls and NOT ONE c64_rd. A loop that goes back to a
         * call per cell - which in the shipped build is a call per cell ACROSS
         * THE SEGMENT BOUNDARY, ~63 us of bridge before any work - lands here
         * as 1,000 reads and fails. */
        if (n_zcopy_out - b_zc != C64_ROWS
            || n_zcopy_bytes - b_zcb != C64_CELLS)
            fail("Edit > Copy did not pull the 40x25 matrix one ROW per "
                 "c64_zcopy_out call");
        if (n_ram_rd != 0)
            fail("Edit > Copy read the matrix a cell at a time again - that "
                 "loop is in the OVERLAY, so every cell is two far calls "
                 "(SPEC.md 73.14, C64-SPEC §7.7)");
        if (h_clip_n <= 0) {
            fail("Edit > Copy put nothing on the clipboard");
        } else {
            if (memcmp(h_clip, "hello.\n", 7) != 0)
                fail("Edit > Copy's first row is not `hello.` + one line "
                     "ending - either the trailing spaces were not trimmed "
                     "(clipboard.c:81-85), or the case is wrong (PETSCII "
                     "$41-$5A is the LOWER case, charset.c:156-158), or the "
                     "graphics cell came back as '?' - VICE's unmapped is "
                     "'.' (ASCII_UNMAPPED, charset.c:126) and its Copy "
                     "output cannot contain a '?' at all, because "
                     "edit_copy_action's pass (actions-clipboard.c:69-76) "
                     "substitutes only for a byte that is neither a line "
                     "ending nor printable");
            /* row 1 keeps all 40 cells, because its last one is not a space */
            if (h_clip_n < 7 + 40 + 1 || h_clip[7 + 39] != '!'
                || h_clip[7 + 40] != '\n')
                fail("Edit > Copy trimmed a row whose last cell is not a "
                     "space, or lost its line ending");
            /* ...and a blank row is one line ending and nothing else */
            if (h_clip[7 + 41] != '\n')
                fail("Edit > Copy did not reduce a blank row to a single "
                     "line ending");
            if (h_clip_n > C64_CELLS + C64_ROWS)
                fail("Edit > Copy wrote past the 40x25 screen plus its 25 "
                     "line endings");
        }
        if (c64_msg[0] != 0)
            fail("Edit > Copy announced itself - VICE says nothing on a "
                 "Copy and the window it came from has not changed");

        /* --- AND NOW THE COST ROW, ON A SCREEN THAT IS FULL, because that
         * is what the row is called and it is the worst case: 1,000 converted
         * cells AND a clipboard of 1,025 bytes (40 cells + one line ending, 25
         * times) for kernel/clip.inc's `rep movsb` to move. On the sparse
         * fixture above the put is 71 bytes and the term the review found
         * missing is invisible - which is how it went missing. */
        for (i3 = 0; i3 < C64_CELLS; i3++)
            c64_wr(c64_mbase + i3, 0x01);   /* screen code $01 is `A`, and no
                                             * cell is a space, so nothing is
                                             * trimmed */
        b_blit = n_blit; b_fill = n_fill; b_run = n_run;
        n_ram_rd = 0;
        cost_begin();
        do_edit(C64_I_COPY, win);
        n_ovlx += 1;                        /* ONE crossing, and the change is
                                             * the point: os88_oncmd -> ovl_cmd
                                             * (the runtime's far entry) and
                                             * back, and that is all. ovl_cmd
                                             * now only LATCHES; the body runs
                                             * from the wake in resident code,
                                             * so ovl_cmd -> ovl_copy,
                                             * ovl_copy -> c64_copy_screen and
                                             * ovl_copy -> os88_clip_put are
                                             * gone. The first draft made 2,000
                                             * of them, two a cell. */
        cost_row("Edit > Copy of the whole screen");
        if (h_clip_n != C64_CELLS + C64_ROWS)
            fail("a Copy of a screen with no space on it is not 40x25 cells "
                 "plus 25 line endings");
        if (n_clip_put_bytes - b_cpb != C64_CELLS + C64_ROWS)
            fail("the clipboard put did not move the whole buffer - that "
                 "`rep movsb` is kernel/clip.inc:118 and it runs inside THIS "
                 "command's gfx lock (C64-SPEC §7.7)");
        if (n_blit != b_blit || n_fill != b_fill || n_run != b_run)
            fail("Edit > Copy of a full screen drew on the glass");
        if (n_zcopy_out - b_zc != C64_ROWS
            || n_zcopy_bytes - b_zcb != C64_CELLS)
            fail("Edit > Copy did not pull the 40x25 matrix one ROW per "
                 "c64_zcopy_out call");
        if (n_ram_rd != 0)
            fail("Edit > Copy read the matrix a cell at a time again");

        /* THE NEGATIVE CONTROL: a clipboard that REFUSES is a fact and is
         * said. kernel/clip.inc:84 leaves it EMPTY rather than stale, so a
         * silent failure here is a user who pastes the PREVIOUS thing. */
        h_clip_refuse = 1;
        c64_msg[0] = 0;
        do_edit(C64_I_COPY, win);
        if (c64_msg[0] == 0)
            fail("a REFUSED clipboard put said nothing - the clipboard is "
                 "left empty and the user is not told (SPEC.md 47)");
        h_clip_refuse = 0;

        /* --- Paste: the clipboard, converted, then typed at the KERNAL's
         * own pace. The line endings are the half a remembered version gets
         * wrong: CRLF is TWO bytes and ONE line end (charset.c:49-63), and
         * without the pair test a document written on a DOS machine types
         * two RETURNs a line. */
        n3 = 0;
        for (i3 = 0; i3 < 20; i3++)
            h_clip[n3++] = (char)('a' + (i3 % 26));
        h_clip[n3++] = '\r';                /* CRLF - ONE line end */
        h_clip[n3++] = '\n';
        for (i3 = 0; i3 < 17; i3++)
            h_clip[n3++] = (char)('A' + (i3 % 26));
        h_clip[n3++] = '\n';                /* LF - one line end */
        h_clip_n = n3;                      /* 40 host bytes -> 39 PETSCII */
        c64_msg[0] = 0;
        /* the whole matrix was just rewritten by the Copy rows above: let the
         * glass settle, so the cost row below prices a paste and not that */
        flush_now();
        flush_now();
        /* ...and the KERNAL has NOT drained its buffer, so the wake the
         * command posts must feed nothing at all */
        c64_wr(C64_KB_NDX, 3);
        cost_begin();
        do_edit(C64_I_PASTE, win);
        flush_now();                        /* ...and this IS the driver: the
                                             * feeder runs inside the wake */
        /* ONE crossing: os88_oncmd -> ovl_cmd (the runtime's far entry) and
         * back. ovl_cmd only LATCHES - the clipboard calls, the claim and the
         * refusals are ovl_clip_service's, which is resident and runs from the
         * wake - so ovl_cmd -> ovl_paste, ovl_paste -> os88_clip_size and
         * ovl_paste -> os88_clip_get are all gone. There is no conversion call
         * in the command either: the byte map is the feeder's, ten a wake with
         * no lock held, which is what the row below prices. */
        n_ovlx += 1;
        cost_row("Edit > Paste of 40 characters");
        if (c64_paste_n != 40 || c64_paste_i != 0)
            fail("Edit > Paste did not queue the 40 host bytes the clipboard "
                 "holds - the queue is the HOST's bytes now and the byte map "
                 "is the feeder's, ten a wake with no lock held (§7.7)");
        if (c64_paste_seg == 0 || last_movable != c64_paste_seg)
            fail("Edit > Paste's queue was not declared movable once it was "
                 "filled (C64-SPEC 3.1.1)");
        if (n_band_cells - b_cells > C64_COLS)
            fail("Edit > Paste composed more than one row's worth of cells - "
                 "the command itself draws nothing at all; what reaches the "
                 "glass is the machine's own echo");
        if (n_kbbuf - b_kb != 0)
            fail("Edit > Paste typed into $0277 from the COMMAND - the KERNAL "
                 "buffer was not empty, so the feeder must have put nothing "
                 "in this wake (kbdbuf.c:383)");

        /* THE PACE. kbdbuf_flush (kbdbuf.c:383) puts NOTHING in while the
         * machine's own buffer is not empty, and never more than the ten
         * bytes it holds. Feeding on a schedule of our own instead would
         * overrun $0277 and lose characters silently - which is the one
         * failure a screendump of a paste cannot show, because what is left
         * on the screen still looks like text. */
        fed = 0;
        c64_paste_feed();
        if (c64_paste_i != 0 || c64_rd(C64_KB_NDX) != 3)
            fail("the paste feeder wrote into a keyboard buffer that was "
                 "NOT empty - kbdbuf_is_empty is the gate (kbdbuf.c:233)");
        c64_wr(C64_KB_NDX, 0);
        /* ...AND THIS IS WHERE A PASTE IS PRICED NOW: ten bytes converted and
         * typed, in os88_onwake, with NO lock held. The command itself is one
         * os88_clip_get (the row above), which is what took the conversion
         * out from under the gfx lock. */
        cost_begin();
        c64_paste_feed();
        cost_row("a wake typing ten pasted bytes");
        if (c64_rd(C64_KB_NDX) != C64_KB_SIZE)
            fail("the paste feeder did not put the buffer's full ten bytes "
                 "in when it was empty");
        if (c64_paste_i != C64_KB_SIZE)
            fail("the paste feeder is not bounded at ten characters a wake");
        /* ...AND WHAT IT PUT THERE IS THE CONVERSION. The queue holds the
         * host's bytes, so this is the only place the byte map is visible -
         * which is the point: it is what the MACHINE receives. */
        for (i3 = 0; i3 < C64_KB_SIZE; i3++)
            h_fed[fed++] = (unsigned char)c64_rd(C64_KB_BUF + i3);
        for (i3 = 0; i3 < 40 && c64_paste_i < c64_paste_n; i3++) {
            c64_wr(C64_KB_NDX, 0);          /* the machine drank them */
            c64_paste_feed();
            j3 = c64_rd(C64_KB_NDX);
            if (j3 > C64_KB_SIZE)
                fail("the paste feeder overran the KERNAL's ten-byte buffer");
            for (n3 = 0; n3 < j3; n3++)
                h_fed[fed++] = (unsigned char)c64_rd(C64_KB_BUF + n3);
        }
        if (c64_paste_n != 0)
            fail("the paste never drained");
        if (fed != 39)
            fail("40 host bytes did not become 39 PETSCII ones - CRLF is TWO "
                 "bytes and ONE line end (charset.c:49-63), and without the "
                 "pair test a document written on a DOS machine types two "
                 "RETURNs a line, which in BASIC is a listing with a blank "
                 "line between every statement");
        if (h_fed[0] != 0x41)
            fail("the paste mapped 'a' to something other than PETSCII $41 "
                 "(charset.c:186-188)");
        if (h_fed[20] != 0x0D)
            fail("the paste's line ending is not PETSCII CR");
        if (h_fed[21] != 0xC1)
            fail("the paste mapped 'A' to something other than PETSCII $C1 - "
                 "the 0x61 duplicates are deliberately not used "
                 "(charset.c:189-192)");
        if (h_fed[38] != 0x0D)
            fail("the paste lost the trailing LF's line ending");

        /* ...AND A RESET EMPTIES THE QUEUE (kbdbuf.c:318). Without it the
         * previous machine's paste types itself into the new one. */
        c64_paste_n = 12;
        c64_paste_i = 0;
        do_reset(C64_I_POWER, win);
        if (c64_paste_n != 0)
            fail("a Power cycle left the previous machine's paste in the "
                 "queue, still typing");
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_msg[0] = 0;

        /* ...AND PASTE GREYS WHILE THE MACHINE IS PAUSED, which is the state
         * the greying was reasoned about and then missed. Pause is not a
         * STATE, it is a flag on C64_ST_RUN: c64.c gates the feeder's arm on
         * `!c64_pause || c64_adv` and c64_wants_wake answers 0 while paused,
         * so a Paste taken here queues up to 2,048 bytes that nothing types
         * and says nothing, until the user happens to resume. The pause latch
         * calls c64_menu_state, so it greys and UN-greys with the state -
         * SPEC.md 47 both ways round. */
        do_cmd(C64_I_PAUSE, C64_M_PREF, win);
        if (!c64_pause)
            fail("Preferences > Pause emulation did not latch");
        if (c64_edit_items[C64_I_PASTE][0] != OS88_MENU_DIS)
            fail("Edit > Paste stayed LIVE on a PAUSED machine - nothing "
                 "drains the queue while paused, so it is the same silent "
                 "no-op the JAM greying exists to stop (SPEC.md 47)");
        if (c64_edit_items[C64_I_COPY][0] == OS88_MENU_DIS)
            fail("Edit > Copy was greyed on a PAUSED machine - the screen is "
                 "there and VICE copies what is displayed");
        /* ...and the CHORD does not walk round it either */
        c64_paste_n = 0;
        h_clip_n = 8;
        memcpy(h_clip, "abcdefgh", 8);
        os88_gfx_lock(); os88_onkey(0, 0xA2, win); os88_gfx_unlock();
        if (c64_paste_n != 0)
            fail("Alt+Insert queued a paste into a PAUSED machine while the "
                 "item was GREYED");
        do_cmd(C64_I_PAUSE, C64_M_PREF, win);
        if (c64_pause)
            fail("Preferences > Pause emulation did not unlatch");
        if (c64_edit_items[C64_I_PASTE][0] == OS88_MENU_DIS)
            fail("Edit > Paste stayed GREYED after the machine resumed - a "
                 "greying must not outlive its fact (SPEC.md 47)");
        h_clip_n = -1;
        c64_msg[0] = 0;

        /* ...AND AN EMPTY CLIPBOARD IS A FACT AND NOT A NO-OP (SPEC.md 47) */
        h_clip_n = -1;
        do_edit(C64_I_PASTE, win);
        if (c64_paste_n != 0)
            fail("Edit > Paste queued something out of an EMPTY clipboard");
        if (c64_msg[0] == 0)
            fail("Edit > Paste of an empty clipboard said nothing - a live "
                 "item that is a silent no-op is what SPEC.md 47 forbids");
        c64_msg[0] = 0;

        /* ...AND A CLIPBOARD BIGGER THAN THE QUEUE IS TRUNCATED, SAID - and
         * the message NAMES the number, so the two have to agree. C64_PASTEMAX
         * is Paste's own bound and not Copy's (C64-SPEC §7.7): a paste is
         * whatever the user put on the clipboard, and sizing its queue from
         * what a COPY can produce was a justification that did not describe
         * anything. */
        for (i3 = 0; i3 < C64_PASTEMAX + 100; i3++)
            h_clip[i3] = 'x';
        h_clip_n = C64_PASTEMAX + 100;
        c64_msg[0] = 0;
        do_edit(C64_I_PASTE, win);
        if (c64_paste_n != C64_PASTEMAX)
            fail("Edit > Paste did not stop at its buffer");
        if (c64_msg[0] == 0)
            fail("a TRUNCATED paste said nothing");
        {
            char want[64];
            sprintf(want, "Pasting the first %d bytes.", C64_PASTEMAX);
            if (strcmp(c64_msg, want) != 0)
                fail("the truncation message and C64_PASTEMAX have drifted "
                     "apart - the message names the number and the number is "
                     "in c64kbd.c");
        }

        /* ...AND A FULL CLIPBOARD IS NOT AN EMPTY ONE. kernel/clip.inc:84
         * refuses only what is strictly ABOVE CLIP_MAXKB * 1024, so a put of
         * exactly 32,768 succeeds and os88_clip_size answers 0x8000 - which
         * the package's 16-bit int reads as -32,768. `sz <= 0` then reported
         * `The clipboard is empty.` on a FULL clipboard, queued nothing, and
         * made the truncation notice false in the same breath: the one
         * message the user got was the untrue one. -1 is the only answer that
         * means empty. */
        h_clip_n = 32 * 1024;
        c64_paste_n = 0;
        c64_paste_i = 0;
        c64_msg[0] = 0;
        do_edit(C64_I_PASTE, win);
        if (c64_paste_n != C64_PASTEMAX)
            fail("a clipboard of exactly CLIP_MAXKB * 1024 pasted nothing - "
                 "32,768 arrives in a 16-bit int as -32,768 (LESSONS: 16-bit "
                 "arithmetic wraps)");
        if (strcmp(c64_msg, "The clipboard is empty.") == 0)
            fail("a FULL clipboard was reported as empty");

        c64_paste_n = 0;
        c64_paste_i = 0;
        h_clip_n = -1;
        c64_msg[0] = 0;
        (void)ln;
    }

    /* ======================================================================
     * WAVE 3 - WARP IS THE SLICE CAP, NOT THE FLUSH RATE (C64-SPEC §4.4)
     * ==================================================================== */
    {
        unsigned t3;
        int i3, seen;

        c64_state = C64_ST_RUN;
        c64_pause = 0;
        c64_warp = 0;
        c64_budget = C64_SLICE_MAX;
        h_cpu_mode = H_CPU_IDLE;
        h_slow_wake = 0;

        /* ...the cap holds without warp: four slices inside one host tick
         * must NOT take the budget past C64_SLICE_MAX */
        for (i3 = 0; i3 < 40; i3++)
            os88_onwake(win);
        if (c64_budget > C64_SLICE_MAX)
            fail("the wall slice passed its cap with warp OFF");

        do_cmd(C64_I_WARP, C64_M_PREF, win);
        if (!c64_warp)
            fail("Preferences > Warp mode did not latch");
        for (i3 = 0; i3 < 40; i3++)
            os88_onwake(win);
        if (c64_budget <= C64_SLICE_MAX)
            fail("Warp mode did not lift the wall slice's cap - warp is the "
                 "only throttle this port has (C64-SPEC §4.4), and a warp "
                 "that only slows the FLUSH down does not run the machine "
                 "one cycle faster, it stops showing what it does");
        if (c64_budget > C64_SLICE_WARP)
            fail("the warped budget passed C64_SLICE_WARP - c64_m.cnt is a "
                 "SIGNED word and a budget over 32,767 arrives negative");

        /* ...AND THE RENDERING IS CAPPED AT 10 fps, WHICH IS THE OTHER HALF
         * OF VICE'S WARP AND NOT A DIFFERENT FEATURE. src/vsync.c:339-340
         * sets warp_render_tick_interval to a tenth of a second under `Limit
         * warp rendering to 10fps` and :634-656 skips frames inside it while
         * warp is on - `makes warp faster`. A pass of this port's review
         * deleted the cap on the reading that drawing less is not warping;
         * the reference says it is half of warping, and on this machine it is
         * the EXPENSIVE half (a full expose is 306 ms against a 16,384-cycle
         * slice, §9.7).
         *
         * TWO ROWS, BECAUSE THE CAP CAN ONLY SLOW A FLUSH DOWN. 10 fps at
         * 18.2 Hz is two host ticks, so on the CPU_8086 tier - which already
         * flushes every OTHER tick for §9.8's own reason - warp moves
         * nothing, and that is why the message on that tier still says warp
         * buys it nothing. On a 286 or a 386 (c64_flush_every = 1) the cap
         * HALVES the drawing, which is what warp is worth there. */
        if (c64_flush_every != 2)
            fail("this block assumes the CPU_8086 tier's flush rate");
        seen = 0;
        c64_last_flush = the_ticks;
        for (i3 = 0; i3 < 20; i3++) {
            the_ticks++;
            c64_dirty_any = 1;
            t3 = c64_last_flush;
            os88_onwake(win);
            if (c64_last_flush != t3)
                seen++;
        }
        if (seen != 10)
            fail("warp changed the flush rate on the CPU_8086 tier - that "
                 "tier already flushes every other tick, which is SLOWER "
                 "than VICE's 10 fps warp cap, so the cap cannot bind "
                 "(C64-SPEC §4.4)");
        /* ...and the faster tiers, where it does bind. The negative control
         * is the same twenty ticks with warp OFF: 20 flushes, not 10. */
        c64_flush_every = 1;
        seen = 0;
        c64_last_flush = the_ticks;
        for (i3 = 0; i3 < 20; i3++) {
            the_ticks++;
            c64_dirty_any = 1;
            t3 = c64_last_flush;
            os88_onwake(win);
            if (c64_last_flush != t3)
                seen++;
        }
        if (seen != 10)
            fail("warp did not cap the rendering at VICE's 10 fps on a tier "
                 "that flushes every tick (vsync.c:339-340, :634-656)");
        c64_warp = 0;
        c64_menu_state();
        seen = 0;
        c64_last_flush = the_ticks;
        for (i3 = 0; i3 < 20; i3++) {
            the_ticks++;
            c64_dirty_any = 1;
            t3 = c64_last_flush;
            os88_onwake(win);
            if (c64_last_flush != t3)
                seen++;
        }
        if (seen != 20)
            fail("the render cap outlived the warp it was granted for");
        c64_warp = 1;
        c64_menu_state();
        c64_flush_every = 2;

        /* ...AND ON THIS CPU IT SAYS SO. C64-SPEC §4.4 is honest that an
         * 8088 never reaches the ceiling warp lifts - the budget adapts DOWN
         * from a slice that overruns a host tick and never walks up to the
         * cap - but the SPEC is not where the user is looking. An item whose
         * whole visible effect is a message that is false is what SPEC.md 47
         * exists to stop, and PERFORMANCE.md's "degrade by tier" gives the
         * answer: os88_cpu() is a fact the code can test. */
        if (strcmp(c64_msg, "Warp mode on - no change.") != 0)
            fail("Warp mode on CPU_8086 announced a speed-up the machine "
                 "does not deliver (§4.4, SPEC.md 47)");
        /* ...AND ITS LENGTH IS CHECKED AT THE SOURCE, NOT ON THE COPY. This
         * used to read `strlen(c64_msg) > C64_MSGCELLS` and could never
         * fail: c64_say copies through os88_strcpy(c64_msg, s,
         * C64_MSGCELLS + 1), and the stub truncates at cap - 1 exactly as
         * the SDK does, so the thing measured was the clamp. The failure the
         * row exists to catch - a message the user reads with its tail cut
         * off - would have gone straight past it. The literals are walked
         * below, once, against the same constant. */

        do_cmd(C64_I_WARP, C64_M_PREF, win);
        if (c64_warp)
            fail("Preferences > Warp mode did not unlatch");
        if (c64_budget > C64_SLICE_MAX)
            fail("leaving warp left the budget above the ordinary cap - the "
                 "only thing that lowers it is a slice that overruns a host "
                 "tick, and one that fits never would");
        /* THE NEGATIVE CONTROL FOR THE TIER: a 386 IS given the cap it can
         * use, and is told the plain thing VICE says. Without this row the
         * message above could be hard-coded and nothing would notice. */
        h_cpu_tier = OS88_CPU_386;
        c64_msg[0] = 0;
        do_cmd(C64_I_WARP, C64_M_PREF, win);
        if (strcmp(c64_msg, "Warp mode on.") != 0)
            fail("a faster tier was told warp buys it nothing - it does: the "
                 "cap is what binds there (§4.4)");
        do_cmd(C64_I_WARP, C64_M_PREF, win);
        h_cpu_tier = OS88_CPU_8086;
        c64_msg[0] = 0;

        c64_budget = C64_SLICE_MIN;
        c64_dirty_any = 0;
    }

    /* ======================================================================
     * EVERY MESSAGE LITERAL, AGAINST THE ROW THAT HAS TO SHOW IT (§10.1)
     *
     * The status row's field area is C64_MSGCELLS = 40 cells and the two
     * lamps keep their own two beyond it. c64_say CLAMPS to that, so a
     * message that is too long does not corrupt anything - it silently loses
     * its tail on the glass, which is a defect a screendump shows only if
     * somebody happens to trigger that message while looking.
     *
     * So the literals are the subject, not c64_msg. The list is every string
     * this package passes to c64_say plus the two permanent lines ovl_status
     * draws itself; a new message that does not fit fails the BUILD.
     *
     * AND THE LIST CANNOT SILENTLY NARROW. Nothing here ties a hand-typed
     * array to the sources, so a c64_say added in a later wave and not copied
     * in was simply not checked - a gate that says nothing rather than a gate
     * that fails, which is the same shape as a stale comment. apps/c64/build.sh
     * extracts every c64_say literal out of apps/c64/*.c mechanically and
     * fails if the set is not exactly the set below. Add a message, add it
     * here, or the build stops.
     * ==================================================================== */
    {
        static const char *msgs[] = {
            "A file dialog is already open.",
            "Not a D64 image.",
            "D64: no heap for it.",
            "D64: cannot read it.",
            "Disk on drive 8.",
            "Disk on drive 8; no room for recompiler.",
            "Recompiler on.", "Recompiler off.",
            "No memory for the copy.",
            "No memory for the paste.",
            "Advanced one frame.",
            "Joysticks normal.",
            "Joysticks swapped.",
            "No bands here - text only.",
            "No square voice here - no SID sound.",
            "PRG over 65533 bytes.",
            "PRG runs past $FFFF.",
            "PRG too short.",
            "PRG: cannot read it.",
            "PRG: no heap for it.",
            "Pasting the first 2048 bytes.",
            "Paused.",
            "Running.",
            "ScrollLock for joystick",
            "The clipboard is empty.",
            "The clipboard refused the screen.",
            "The clipboard refused to be read.",
            "The speaker is busy - no SID sound.",
            "Warp mode off.",
            "Warp mode on - no change.",
            "Warp mode on.",
            "C64.ROM missing - see README.TXT",   /* ovl_status, msg == 2 */
            "Main CPU: JAM at $FFFF"              /* ...and msg == 3 (§4.5) */
        };
        int mi;

        for (mi = 0; mi < (int)(sizeof(msgs) / sizeof(msgs[0])); mi++) {
            if ((int)strlen(msgs[mi]) > C64_MSGCELLS) {
                printf("  the message `%s` is %d cells\n",
                       msgs[mi], (int)strlen(msgs[mi]));
                fail("a status-row message is longer than the row's 40 field "
                     "cells - c64_say would clamp it and the user would read "
                     "it with its tail cut off (§10.1)");
            }
        }
    }

    /* ======================================================================
     * WAVE 3 - THE SOUND CAPABILITY (C64-SPEC §11.4)
     * ==================================================================== */
    {
        int n0;

        if (h_caps_asks == 0)
            fail("os88_snd_caps was never asked - the package calls "
                 "OSAPI_SND_TONE without establishing that the machine has a "
                 "square voice (SPEC.md 73.11)");
        /* THE NEGATIVE CONTROL, and it is the only way this path is
         * reachable at all: kernel/snd.inc:804 ORs SND_CAP_TONE in
         * unconditionally, so every machine this OS boots has it. */
        n0 = n_tone;
        c64_snd_tone = 0;
        c64_sid_said = 0;
        c64_sid_dirty = 1;
        c64_msg[0] = 0;
        c64_state = C64_ST_RUN;
        c64_pause = 0;
        os88_onwake(win);
        if (n_tone != n0)
            fail("a machine with NO square voice was still asked for a tone");
        if (c64_sid_dirty)
            fail("the SID latch was left raised on a machine with no tone - "
                 "it would be re-asked every wake for ever");
        if (c64_msg[0] == 0)
            fail("a machine with no square voice said nothing about its SID "
                 "(SPEC.md 47)");
        /* ...and it is said ONCE a session, not once a wake */
        c64_msg[0] = 0;
        c64_sid_dirty = 1;
        os88_onwake(win);
        if (c64_msg[0] != 0)
            fail("the no-square-voice fact was said a second time");
        c64_snd_tone = 1;
        c64_sid_said = 0;
        c64_sid_dirty = 0;
        c64_msg[0] = 0;
    }

    /* ======================================================================
     * WAVE 3 - §7.6's MESSAGE, AND THE OBSERVABLE THAT ANSWERS IT
     *
     * The SDK cannot be asked "has a mouse spoken" - osapi_mouse tests
     * [mou_seen] and reports x, y and the button (kernel/kernel.asm:3263) -
     * so the package asks the question it CAN answer and that has the same
     * answer: kbm_key intercepts a cursor key exactly when no mouse has
     * spoken and ScrollLock is off, and an intercepted key never reaches
     * os88_onkey. "The down-map says an arrow is held and os88_onkey has
     * never delivered one" IS "the kernel is eating them".
     * ==================================================================== */
    {
        int i3;

        c64_kbd_init();
        c64_msg[0] = 0;
        memset(h_keydown, 0, sizeof(h_keydown));
        h_keydown[KSC_UP] = 1;              /* the stick is deflected... */
        for (i3 = 0; i3 < 3; i3++)
            c64_kbd_poll();                 /* ...and no arrow ever ARRIVED */
        if (strcmp(c64_msg, "ScrollLock for joystick") != 0)
            fail("the stick was held with no cursor key ever delivered to "
                 "os88_onkey and the row said nothing - on a mouseless "
                 "machine those four keys are the kernel's pointer (SPEC.md "
                 "9.6) and every deflection drags it about");
        /* ...ONCE a session */
        c64_msg[0] = 0;
        for (i3 = 0; i3 < 10; i3++)
            c64_kbd_poll();
        if (c64_msg[0] != 0)
            fail("§7.6's message was said a second time");

        /* THE NEGATIVE CONTROL: a machine WITH a mouse delivers the arrow to
         * os88_onkey, and then the message must never appear. */
        c64_kbd_init();
        c64_msg[0] = 0;
        memset(h_keydown, 0, sizeof(h_keydown));
        os88_onkey(0, KSC_UP, win);         /* the kernel passed it: a mouse
                                             * has spoken, or ScrollLock is on */
        h_keydown[KSC_UP] = 1;
        for (i3 = 0; i3 < 10; i3++)
            c64_kbd_poll();
        if (c64_msg[0] != 0)
            fail("§7.6's message went up on a machine that DELIVERS its "
                 "cursor keys - the hint is wrong there and the control is "
                 "what makes the row above mean anything");
        memset(h_keydown, 0, sizeof(h_keydown));
        c64_kbd_init();
        c64_msg[0] = 0;
        c64_state = C64_ST_RUN;
    }

    /* --- File > Exit emulator (Alt+Q): ONE DOOR, AND IT IS NOT wm_destroy
     * ------------------------------------------------------------------
     * The window closes through OSAPI_WM_CLOSE (SPEC.md 75.2) - the kernel's
     * own close path, the same one the close box takes, which is what
     * repaints the dock strip. The version this replaced hired a worker and
     * called os88_wm_destroy: that frees the RECORD and nobody repaints the
     * dock, so Alt+Q left a dead tile that answered neither click nor
     * double-click, four cycles out of four, on both adapters. The old gate
     * asserted the worker's own internals - the destroy, the task_alive - and
     * so passed a package that left a corpse on the strip. It asserts the
     * DOOR now.
     *
     * THE COMMAND MUST NOT CLOSE. os88_oncmd kicks a wake after ovl_cmd
     * returns, and that wake runs a slice and flushes: closing inside the
     * command means drawing after the close, which 75.2 forbids in as many
     * words. So the command latches and the WAKE spends it. */
    closed = 0;
    close_drew = 0;
    do_cmd(C64_I_EXIT, C64_M_FILE, win);
    if (closed != 0)
        fail("Exit emulator closed the window from inside os88_oncmd - "
             "os88_oncmd's own tail then kicks a wake that draws into a "
             "window that is going away (SPEC.md 75.2)");
    if (c64_state == C64_ST_DEAD)
        fail("the command latched C64_ST_DEAD before anything asked for the "
             "close - a wake that never comes then leaves a frozen machine "
             "in a window nobody is closing");

    flush_now();                        /* the wake os88_oncmd kicked */
    if (closed != 1)
        fail("Exit emulator did not close the window - the dock tile stays "
             "and the two claims leak for the session (SPEC.md 75.2)");
    if (c64_state != C64_ST_DEAD)
        fail("the machine is still running into a window that is closing");
    if (close_drew)
        fail("the package DREW after os88_wm_close - 75.2: call it and "
             "RETURN, the window is gone on the next UI pass");
    if (lock_depth != 0)
        fail("the exit wake left the gfx lock held");

    /* ...AND EVERY LATER WAKE MUST BE A NO-OP. The close is deferred, so more
     * wakes can be dispatched before it lands; a second os88_wm_close would
     * name a slot the kernel may already have handed to a new tenant, and the
     * stub fails on it. C64_ST_DEAD is what stops them. */
    flush_now();
    flush_now();
    if (closed != 1)
        fail("a wake after the close asked for the close again");

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
