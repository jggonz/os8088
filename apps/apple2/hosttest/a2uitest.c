/* ============================================================================
 * os8088 - apps/apple2/hosttest/a2uitest.c    APPLE2's screen model, on the
 *                                             host
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 16.6). apps/apple2/ is
 * GPL-2-or-later; see apps/apple2/COPYING.
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and apps/apple2/build.sh compiles and runs it BEFORE the target build; a
 * failure stops the build.
 *
 * ----------------------------------------------------------------------------
 * WHY
 * ----------------------------------------------------------------------------
 * a2scr.c is a damage model: dirty pages and a write window narrow a
 * recompose to a group span, a pixel compare against a 7,680-byte shadow
 * narrows the DRAW to a byte span, runs of scan lines are merged into one
 * blit, and a signature test turns a scrolled frame into one gfx_scroll.
 * Every one of those decisions is a chance to leave a stale line, to draw a
 * line twice, or to trust a shadow that has stopped describing the glass -
 * and PERFORMANCE.md is explicit that none of those three shows in an
 * emulator's screendump.
 *
 * So this stubs the API with a PIXEL MODEL OF THE GLASS - gfx_blit1 writes
 * real pixels, gfx_fill fills, gfx_scroll MOVES THE PIXELS AND FILLS THE
 * VACATED ROWS WITH GARBAGE (legal: SPEC.md 5.5, and it is what catches a
 * flush that trusts a stale shadow for a row the scroll vacated) - includes
 * the whole program against it, drives it, and after every step asserts
 *
 *     the glass shows what the shadow says it shows,
 *
 * pixel for pixel over the whole 320x192 band. Then it prints THE COST TABLE
 * IN MILLISECONDS, priced from PERFORMANCE.md's 756 us a drawing call and
 * this package's OWN MEASURED tests/a2band figures, and dumps the machine and
 * the composed frame for tools/a2ref.py - the INDEPENDENT pixel-level
 * compositor - to check bit for bit.
 *
 * ----------------------------------------------------------------------------
 * WHAT IT CANNOT SEE, STATED
 * ----------------------------------------------------------------------------
 * `int` is four bytes here and two there, so a SIXTEEN-BIT WRAP cannot be
 * reproduced by running the code here at all. And the assembly half of the
 * package (a2band.inc, a2mem.inc, a2cpu.inc) cannot run on this host, so the
 * routines below are HOST TRANSCRIPTIONS of what those files do. That makes
 * tools/a2ref.py a check on the ALGORITHM - the character-generator
 * addressing, the per-cell XOR mask, the seven-bit packing - and not on the
 * 8086 encoding of it. The encoding is gated elsewhere and deliberately:
 * hosttest/a2memtest.sh runs the shipping a2mem.inc and a2band.inc on a real
 * x86 under SS != DS, tests/a2band puts the shipping composer's output on the
 * glass beside a known-good line, and the QEMU screendumps are of the
 * shipping composer.
 *
 * ----------------------------------------------------------------------------
 * HOW IT IS BUILT
 * ----------------------------------------------------------------------------
 *   cc -O1 -w -DA2_HOST -I apps/apple2/hosttest -I apps/apple2 \
 *      -o build/a2uitest apps/apple2/hosttest/a2uitest.c && build/a2uitest
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os88.h"                       /* the STUB SDK, ahead of apps/cc on
                                         * the include path */

/* ==========================================================================
 * THE GLASS - one byte a pixel
 * ========================================================================*/
#define GLW 720
#define GLH 520
#define GUNK 0x55                       /* neither lit nor dark: nothing has
                                         * been drawn here */
#define GGARB 0xAA                      /* what gfx_scroll leaves behind */

static unsigned char glass[GLH][GLW];
static int cur_colour = OS88_WHITE, pen_disabled;
static int fails;

/* THE ADAPTER, so the tier table can be driven over more than one of them.
 * The defaults are a VGA desktop; the CGA rows below poke these. */
static int h_scr_w = 640, h_scr_h = 480, h_dock_top = 448;

static int h_win_x, h_win_y, h_win_w, h_win_h;
static int h_cont_x, h_cont_y, h_cont_w, h_cont_h;
static int h_norm_x, h_norm_y, h_norm_w, h_norm_h;  /* the FRAME the window
                                                     * was authored with, and
                                                     * what leaving fullscreen
                                                     * comes back to */
static int lock_depth, clip_armed, h_in_main, h_snap, h_ownbg;
static int h_key_armed, h_key_first_in_main;
static int blit_refuse, scroll_refuse;
static unsigned the_ticks = 100;
static char last_toast[80];
static void *the_win = (void *)0x4321;
static int h_wake_posted, h_closed, h_menu_set, h_about_set, h_onwake_set;

/* the cost counters the glass model keeps (the package's own live in
 * a2scr.c under -DA2_HOST) */
static int n_blit, n_fill, n_frame, n_scroll, n_run, n_cells, n_scroll_dy;
/* ...and the DOUBLED blits among them, told apart by their stride. The stubs
 * are compiled ahead of apple2.c, so this is the literal and main() asserts
 * it against the package's own A2_X2STRIDE - which is what stops it becoming
 * a third copy of a constant tests/unit/t_mirror.py already checks twice. */
#define H_X2STRIDE 80
static int n_blit2;

static void fail(const char *what)
{
    printf("a2uitest: FAIL - %s\n", what);
    fails++;
}

/* ==========================================================================
 * THE GFX LOCK, MODELLED - because the lock is a CONTRACT and not a comment
 *
 * gfx_lock is a NON-RECURSIVE spin released only by the UI task, and every
 * window callback - W_PAINT, W_ONCLICK, AM_ONCMD through the bar - is
 * dispatched WITH IT ALREADY HELD (apps/cc/crt0.asm: "you must never take the
 * lock"). A handler that takes it hangs the machine dead: no beep, no
 * watchdog, no recovery, and nothing on any screendump.
 * ========================================================================*/
void os88_gfx_lock(void)
{
    if (lock_depth)
        fail("os88_gfx_lock while the lock is already held - it is a "
             "NON-RECURSIVE spin and this is the machine hung dead");
    lock_depth++;
}

void os88_gfx_unlock(void)
{
    if (!lock_depth)
        fail("os88_gfx_unlock without the lock");
    lock_depth--;
    clip_armed = 0;                     /* THE CLIP REGION DIES AT UNLOCK
                                         * (SPEC.md 11.3, os88.h: "it dies at
                                         * your next os88_gfx_unlock()"), so
                                         * modelling it as sticky would let
                                         * one lock hold's clip vouch for the
                                         * next one's drawing */
}

/* need_lock - EVERY drawing primitive calls this, and it asks TWO questions
 * because a drawing call has two preconditions and only one of them is
 * loud. The lock is a contract a violation of which hangs the machine dead;
 * the CLIP REGION is a contract a violation of which draws over somebody
 * else's window and shows up in no screendump taken afterwards. The kernel
 * arms one for W_PAINT and for nothing else, so anything drawn from a wake,
 * from os88_about or from any other callback has to have armed its own. */
static void need_lock(const char *what)
{
    if (!lock_depth) {
        printf("a2uitest:   %s drew with NO gfx lock held\n", what);
        fails++;
    }
    if (!clip_armed) {
        printf("a2uitest:   %s drew with NO clip region armed - it lands on "
               "whatever window is covering ours (SPEC.md 11.3)\n", what);
        fails++;
    }
}

void os88_set_color(int c) { cur_colour = c; }
void os88_gfx_pen(int d) { pen_disabled = d; }

static void plot(int x, int y, int v)
{
    if (x < 0 || y < 0 || x >= GLW || y >= GLH)
        return;
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

/* WHICH SCREEN ROWS A BLIT TOUCHED, since the last time this was cleared.
 * The About panel's hold range is in APPLE scan lines and a blit is in SCREEN
 * pixels, and at 2x those are not the same thing - so the test that asks
 * "did anything draw under the card" converts one to the other AFTER the
 * wake, where a2_gsy, a2_gl0 and a2_sch are all in scope. This stub is above
 * `#include "apple2.c"` and cannot read them itself. */
static unsigned char h_blitrow[512];

int os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *b = (const unsigned char *)bits;
    int r, c;

    need_lock("gfx_blit1");
    if (blit_refuse)
        return -1;                      /* NOTHING DRAWN */
    if ((x & 7) || (w & 7) || rows < 1 || rows > 255)
        return -1;
    for (r = 0; r < rows; r++)
        for (c = 0; c < w; c++)
            plot(x + c, y + r, (b[r * stride + (c >> 3)] >> (7 - (c & 7))) & 1);
    for (r = 0; r < rows; r++)
        if ((unsigned)(y + r) < sizeof h_blitrow)
            h_blitrow[y + r] = 1;
    n_blit++;
    /* ...AND A DOUBLED BLIT IS PRICED AS ONE. The stride says which it is -
     * A2_X2STRIDE is the doubled band's and nothing else in this package uses
     * it - and it costs 3.23 ms against 1.75 for four times the pixels
     * (wave 3's bench). Charging every blit the 320x8 figure would understate
     * every fullscreen row in the table below. */
    if (stride == H_X2STRIDE)
        n_blit2++;
    return 0;
}

/* gfx_scroll MOVES the pixels and leaves the vacated rows GARBAGE. SPEC.md
 * 5.5 says the vacated rows are the caller's to repaint and says nothing
 * about what is in them, so this fills them with a value no composer can
 * produce: a flush that trusts a stale shadow for one of those rows fails the
 * audit here rather than on someone's screen.
 *
 * AND POSITIVE dy MOVES THE CONTENT UP. The first draft of the C64's stub
 * implemented the opposite convention and the package was wrong in the same
 * direction, so the step passed (LESSONS.md 7's "verify the stubs model what
 * the machine does"). This one is written from SPEC.md 5.5 and asserted
 * against by the scroll step below. */
int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int x, y;

    need_lock("gfx_scroll");
    n_scroll++;
    n_scroll_dy = dy;
    if (scroll_refuse)
        return -1;
    if ((x1 & 7) || ((x2 + 1) & 7))
        return -1;
    if (x2 < x1 || y2 < y1)
        return -1;
    if (dy == 0 || dy >= y2 - y1 + 1 || -dy >= y2 - y1 + 1)
        return -1;
    if (dy > 0) {
        for (y = y1; y <= y2 - dy; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = glass[y + dy][x];
        for (y = y2 - dy + 1; y <= y2; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = GGARB;
    } else {
        for (y = y2; y >= y1 - dy; y--)
            for (x = x1; x <= x2; x++)
                glass[y][x] = glass[y + dy][x];
        for (y = y1; y < y1 - dy; y++)
            for (x = x1; x <= x2; x++)
                glass[y][x] = GGARB;
    }
    return 0;
}

/* THE KERNEL FACE IS 8x8 AND ASCII 32..126. This models the cells it fills so
 * that the fallback path draws SOMETHING the audit can see - it is not the
 * Apple's face and the package says so (a2scr.c's second-path comment). */
void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int i, r, c;

    need_lock("font_run");
    for (i = 0; s[i]; i++) {
        for (r = 0; r < 8; r++)
            for (c = 0; c < 8; c++)
                plot(x + i * 8 + c, y + r,
                     ((r + c + s[i]) & 3) == 0 ? (ink == OS88_BLACK ? 0 : 1)
                                               : (paper == OS88_BLACK ? 0 : 1));
        n_cells++;
    }
    n_run++;
}

void os88_font_str(int x, int y, const char *s)
{
    os88_font_run(x, y, s, OS88_BLACK, OS88_WHITE);
}

/* ==========================================================================
 * THE WINDOW
 * ========================================================================*/
void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    h_win_x = x;
    h_win_y = y;
    h_win_w = w;
    h_win_h = h;
    /* the kernel's own arithmetic: wm_create authors a FRAME and wm_geom
     * answers the CONTENT box, which is `w - 2` wide (the two 1-pixel side
     * borders) and `h - TITLE_H - 1` tall */
    h_cont_x = (x + 1 + 7) & ~7;        /* os88_wm_snap: on a cell boundary */
    h_cont_y = y + OS88_TITLE_H;
    h_cont_w = w - 2;
    h_cont_h = h - OS88_TITLE_H - 1;
    h_norm_x = x; h_norm_y = y; h_norm_w = w; h_norm_h = h;
    (void)title;
    return the_win;
}

void os88_wm_content(void *win, struct os88_pt *o)
{
    (void)win;
    o->x = h_cont_x;
    o->y = h_cont_y;
}

/* h_minimized is os88_wm_geom's "-1 = not visible" (os88.h) - the answer a
 * MINIMIZED window gets, and a second route to "draw nothing" that clip_set
 * does not cover. */
static int h_minimized;

int os88_wm_geom(void *win, struct os88_size *s)
{
    (void)win;
    if (h_minimized)
        return -1;
    s->w = h_cont_w;
    s->h = h_cont_h;
    return 0;
}

/* WHAT THE NEXT W_PAINT REPORTS. `whole` is the kernel's answer to "the
 * window was exposed entirely"; a partial expose hands back a rect and 0, and
 * WF_OWNBG means the kernel painted NOTHING - so what the package owes is
 * exactly those pixels. do_paint() drives the whole one and do_paint_rect()
 * the partial one. */
static int h_dmg_whole = 1;
static int h_dmg_x1, h_dmg_y1, h_dmg_x2, h_dmg_y2;

int os88_wm_damage(void *win, struct os88_rect *r)
{
    (void)win;
    if (h_dmg_whole) {
        r->x1 = h_cont_x;
        r->y1 = h_cont_y;
        r->x2 = h_cont_x + h_cont_w - 1;
        r->y2 = h_cont_y + h_cont_h - 1;
        return 1;
    }
    r->x1 = h_dmg_x1;
    r->y1 = h_dmg_y1;
    r->x2 = h_dmg_x2;
    r->y2 = h_dmg_y2;
    return 0;
}

/* THE CLIP REGION, MODELLED AS A CONTRACT. The kernel arms one for W_PAINT
 * and for NOTHING else (SPEC.md 11.3), so a BACKGROUND painter - the wake's
 * flush - has to arm its own or every call it makes lands on top of whatever
 * window is covering ours. That defect is invisible in a still screendump
 * taken after the covering window has gone, so it is counted here: n_clip
 * moves on every clip_set, and the wake path asserts it moved.
 *
 * h_clip_refuse models the other half - not one pixel of us shows - which is
 * what makes "the flush is SKIPPED" testable at all. */
static int n_clip, h_clip_refuse;

int os88_wm_clip_set(void *win)
{
    (void)win;
    if (!lock_depth)                    /* the LOCK only: this call is what
                                         * ARMS the region need_lock asks for
                                         * below, so it cannot ask for it */
        fail("os88_wm_clip_set with no gfx lock held - the region is scoped "
             "to the hold and there is no hold");
    n_clip++;
    if (h_clip_refuse)
        return -1;
    clip_armed = 1;
    return 0;
}

void os88_wm_snap(void *win, int on) { (void)win; h_snap = on; }
void os88_wm_ownbg(void *win, int on) { (void)win; h_ownbg = on; }
void os88_wm_onwake(void *win) { (void)win; h_onwake_set = 1; }

/* OSAPI_WM_TIMER (SPEC.md 13.9), ONE-SHOT: h_tmr_at is the tick it is due,
 * and do_wake() fires it when the clock reaches it. h_tmr_refuse is the
 * kern_small arm - the slot without the body - which is the SECOND PATH
 * a2_wants_wake keeps the wake's poll for. */
static int h_ontimer_set, h_tmr_armed, h_tmr_refuse, h_tmr_fires;
static unsigned h_tmr_at;

void os88_wm_ontimer(void *win) { (void)win; h_ontimer_set = 1; }

int os88_wm_timer(void *win, int ticks)
{
    (void)win;
    if (h_tmr_refuse)
        return -1;
    if (ticks == 0) {
        h_tmr_armed = 0;
        return 0;
    }
    h_tmr_armed = 1;
    h_tmr_at = the_ticks + (unsigned)ticks;
    return 0;
}
int os88_wm_wake(void *win) { (void)win; h_wake_posted++; return 0; }
void os88_wm_destroy(void *win) { (void)win; h_closed = 1; }
void os88_wm_close(void *win) { (void)win; h_closed = 1; }
/* OSAPI_FULLSCREEN (SPEC.md 11.2) - the WINDOW LATCH, and the two things
 * about it that a stub answering 0 does not model at all: the content box
 * CHANGES SIZE, and the window is REPAINTED WHOLE AND SYNCHRONOUSLY in both
 * directions, so W_PAINT runs NESTED inside this call with the caller's gfx
 * lock already held. Both are why a2_fullscreen_toggle has to take the About
 * panel down first (a2menu.c). h_fs_refuse is the "another window has it"
 * arm, which is the one that owes a repaint of its own. */
static int h_fs, h_fs_refuse;

int os88_fullscreen(void *win, int enter)
{
    if (h_fs_refuse)
        return -1;
    h_fs = enter;
    if (enter) {
        h_win_x = 0; h_win_y = 0; h_win_w = h_scr_w; h_win_h = h_scr_h;
    } else {
        h_win_x = h_norm_x; h_win_y = h_norm_y;
        h_win_w = h_norm_w; h_win_h = h_norm_h;
    }
    if (enter) {
        /* A WF_FULL WINDOW'S CONTENT **IS** ITS FRAME (kernel/wm.inc's
         * wm_geom and wm_content: no border, no title bar, SPEC.md 11.2).
         * Modelling it as frame-minus-two put the content box at 638 and the
         * tier table's own test - `is there room for 640 doubled pixels` -
         * could then never be true here, so the whole 2x path would have been
         * unreachable in the harness and first seen on the glass. */
        h_cont_x = 0;
        h_cont_y = 0;
        h_cont_w = h_win_w;
        h_cont_h = h_win_h;
    } else {
        h_cont_x = (h_win_x + 1 + 7) & ~7;
        h_cont_y = h_win_y + OS88_TITLE_H;
        h_cont_w = h_win_w - 2;
        h_cont_h = h_win_h - OS88_TITLE_H - 1;
    }
    /* the nested repaint. NO os88_gfx_lock() here - the caller is os88_oncmd
     * and the lock is already held, which is exactly the environment the
     * kernel dispatches this in. */
    h_dmg_whole = 1;
    clip_armed = 1;
    os88_paint(win);
    clip_armed = 0;
    return 0;
}
void os88_menu_set(void *win, struct os88_menuset *set)
{ (void)win; (void)set; h_menu_set = 1; }
void os88_about_set(void *win) { (void)win; h_about_set = 1; }

void os88_task_sleep(int ticks) { the_ticks += (unsigned)ticks; }

/* ...AND IT IS COUNTED, because it is a DRAWING SLOT and the bracket's rule 2
 * is about drawing slots (SPEC.md 53.7). kernel/toast.inc's toast_show ends in
 * toast_now, whose predicate - the gfx lock held, owner 0, task 0 - is exactly
 * the state a bracket is in, so a toast raised in there is not deferred: the
 * menu bar and the panel go down IMMEDIATELY, in desktop geometry, into a
 * framebuffer the card has just put into mode 13h. The rule-2 assertion
 * counted blit/fill/run/scroll/clip and this is in none of them. */
static int n_toast;

int os88_toast(const char *text, int ticks)
{
    (void)ticks;
    n_toast++;
    strncpy(last_toast, text, sizeof(last_toast) - 1);
    last_toast[sizeof(last_toast) - 1] = 0;
    return 0;
}

/* THE TIER, so the CPU_8086 arm of section 7.8's table has a subject here.
 * a2_tier_init reads this once, from os88_main; the row below sets it and
 * calls a2_tier_init again, which is what the machine's own launch does. */
static int h_cpu = OS88_CPU_386;

int os88_cpu(void) { return h_cpu; }

void os88_video(struct os88_video *v)
{
    v->w = h_scr_w;
    v->h = h_scr_h;
    v->dock_top = h_dock_top;
    v->kind = OS88_VID_VGA;
    v->bpp = 1;
}

unsigned os88_ticks(void) { return the_ticks; }

/* THE KEY-STATE MAP, WITH KEYS THE SCRIPT CAN HOLD DOWN. os88_key_down is a
 * LEVEL (SPEC.md 9.7) and the two game buttons and section 6.6's rule are both
 * read through it, so the harness has to be able to hold one. h_down[] is
 * that: a scan code is "held" while its entry is set. */
static unsigned char h_down[256];
static int n_keydown;

int os88_key_down(int scan)
{
    n_keydown++;
    if (!h_key_armed) {
        h_key_armed = 1;
        h_key_first_in_main = h_in_main;
        return 0;                       /* the FIRST call always answers up */
    }
    return h_down[scan & 0xFF] ? 1 : 0;
}

/* THE SPEAKER (APPLE2-SPEC section 8). The stub RECORDS rather than
 * discarding, because what is being tested is arithmetic that no screendump
 * can show: the estimator turns toggle intervals into a hertz through ONE
 * 32-bit division, and a rounding error near the floor is the difference
 * between the sink refusing the note and playing it (the C64 measured that at
 * F = 341 answering 19 against a true 20).
 *
 * h_snd_refuse MODELS THE REFUSAL, which is the other half: os88_snd_tone
 * answers -1 when another instance holds the speaker (SPEC.md 34.3), and the
 * C64 shipped a version that threw that answer away and went silent for the
 * session. A stub that always grants measures the happy path only, which is
 * LESSONS.md 7 with the sign flipped. */
static int h_snd_caps = 1;                  /* SND_CAP_TONE, and the harness
                                             * can take it away */
static int h_snd_hz = -1;                   /* the last hz GRANTED */
static int h_snd_asked = -1;                /* ...and the last one ASKED for */
static int h_snd_calls;
static int h_snd_refuse;                    /* refuse this many grants */

int os88_snd_caps(void) { return h_snd_caps; }
int os88_snd_tone(int hz, int t, int p)
{
    (void)t;
    (void)p;
    h_snd_calls++;
    h_snd_asked = hz;
    if (h_snd_refuse > 0 && hz != 0) {
        h_snd_refuse--;
        return -1;
    }
    h_snd_hz = hz;
    return 0;
}

/* THE EXCLUSIVE BRACKET (SPEC.md 53, APPLE2-SPEC section 13), stubbed IN THE
 * SAME EDIT AS THE THUNK (LESSONS.md 7).
 *
 * THE STUB MODELS THE REFUSALS AND NOT THE CONVENIENCE (LESSONS.md 9's rule
 * with the sign flipped): h_fsx_mask is what THIS machine's display can set,
 * so the harness can be a CGA - where the answer is "no foreign colour mode
 * here" and the menu row must grey with the measured fact - as easily as a
 * VGA; os88_fsx_run REFUSES when it is asked from anywhere but a window
 * callback, which is the fence SPEC.md 53.1 puts on it; and os88_fsx_mode
 * refuses an id that is not in the mask, which is the SAME BIT the caps
 * answer carried, so a package that greys off one and enters on the other
 * cannot disagree with itself. */
#define H_FSX_SEG 0x8000u                   /* the foreign framebuffer's
                                             * fictional segment, which
                                             * segbase() turns back into
                                             * h_fsx_fb.
                                             *
                                             * IT WAS 0x3000 AND THAT IS
                                             * H_SCRSEG0, the first transient
                                             * claim - so segbase() answered
                                             * the framebuffer for the SHADOW
                                             * as well, every compare read the
                                             * glass it had just written, and
                                             * the frame was self-consistent
                                             * and wrong. The letterbox
                                             * assertion below is what caught
                                             * it, which is the argument for
                                             * asserting what a routine must
                                             * NOT have touched. */
static int h_fsx_mask = 1 << OS88_FSXM_VGA13;   /* a VGA, by default */
static int h_fsx_kind = OS88_VID_VGA;
static int h_fsx_in;                        /* inside the bracket */
static int h_fsx_runs;                      /* ...and how many were entered */
static int h_fsx_modes;                     /* ...and how many mode sets */
static int h_fsx_waits;
static int h_fsx_refuse_run;                /* the kernel refuses the bracket */
static unsigned char h_fsx_fb[320 * 200];   /* THE FOREIGN GLASS - a real
                                             * framebuffer, so a2_fsx_put's
                                             * writes can be read back and
                                             * asserted (the windowed model's
                                             * own rule: the glass is pixels
                                             * and not a promise) */
static int h_fsx_keyq[16];                  /* keys the polled int 16h will
                                             * answer with, in order */
static int h_fsx_keyn, h_fsx_keyi;

int os88_fsx_caps(void *win, int *kind)
{
    (void)win;
    if (kind)
        *kind = h_fsx_kind;
    return h_fsx_mask;
}

/* ...AND IT MODELS SPEC.md 53.6 STEP 4, WHICH IS THE HALF THE FIRST STUB LEFT
 * OUT. The kernel does not simply return when the entry proc does: it puts the
 * desktop mode back and runs A FULL wm_paint_all, with the caller's gfx lock
 * still held, BEFORE fsx_run returns. Without that here, the harness could not
 * see the largest redraw defect this package has - an exit that invalidates
 * the windowed shadow on pixels the kernel's own paint has just made correct,
 * and so composes and blits all 192 lines a SECOND time on the next wake, ~633
 * ms of a 4.77 MHz 8088 for nothing. The row that asserted "the windowed
 * shadow was invalidated on the way out" passed on the stub that never
 * repainted, and it was asserting the opposite of what shipped.
 *
 * IT IS THE LOCKED, CLIP-ARMED, WHOLE-WINDOW PAINT and not do_paint(), because
 * the lock is HELD across the whole bracket (53.6's closing paragraph) and a
 * stub that locked again would model a machine this one is not. */
static void h_paint_locked(void);

/* ...AND RULE 2 IS MEASURED HERE AND NOT AROUND do_cmd, now that step 4 is
 * modelled: that repaint calls drawing slots BY DESIGN and after the mode is
 * back, so a count taken across the whole command would read it as the
 * violation it is not. h_fsx_drawn is what the entry proc itself spent. */
static long h_fsx_drawn;

static long h_draws(void)
{
    return (long)n_blit + n_fill + n_run + n_scroll + n_clip + n_toast;
}

int os88_fsx_run(void (*entry)(void), void *win, int flags)
{
    long d0;

    (void)flags;
    if (h_fsx_refuse_run || h_fsx_in)
        return -1;
    h_fsx_runs++;
    h_fsx_in = 1;
    d0 = h_draws();
    entry();
    h_fsx_drawn = h_draws() - d0;
    h_fsx_in = 0;
    if (win)
        h_paint_locked();               /* step 4, before the return */
    return 0;
}

int os88_fsx_mode(int id, void *fsi)
{
    unsigned char *b = (unsigned char *)fsi;

    if (!h_fsx_in || id < 0 || id > 8 || !(h_fsx_mask & (1 << id)))
        return -1;
    h_fsx_modes++;
    memset(h_fsx_fb, 0, sizeof(h_fsx_fb));
    memset(b, 0, OS88_FSI_SIZE);
    /* The framebuffer SEGMENT is a fiction on the host and the harness's
     * segbase() is what turns it back into a pointer, exactly as it does for
     * the Apple's RAM claim. */
    b[OS88_FSI_SEG] = (unsigned char)(H_FSX_SEG & 0xFF);
    b[OS88_FSI_SEG + 1] = (unsigned char)(H_FSX_SEG >> 8);
    b[OS88_FSI_W] = 320 & 0xFF;
    b[OS88_FSI_W + 1] = 320 >> 8;
    b[OS88_FSI_H] = 200;
    b[OS88_FSI_STRIDE] = 320 & 0xFF;
    b[OS88_FSI_STRIDE + 1] = 320 >> 8;
    b[OS88_FSI_BPP] = 8;
    b[OS88_FSI_BANKS] = 1;
    b[OS88_FSI_PAGES] = 1;
    b[OS88_FSI_MODE] = (unsigned char)id;
    return 0;
}

int os88_fsx_wait(int kind)
{
    (void)kind;
    if (!h_fsx_in)
        return -1;
    h_fsx_waits++;
    the_ticks++;                            /* a frame is a tick: the bracket
                                             * paces on this and the flash
                                             * phase is polled off os88_ticks */
    return 0;
}

static char h_clip[4096];
static int h_clip_n = -1;

int os88_clip_put(const void *text, unsigned len)
{
    if (len > sizeof(h_clip))
        len = sizeof(h_clip);
    memcpy(h_clip, text, len);
    h_clip_n = (int)len;
    return 0;
}

int os88_clip_get(void *buf, unsigned cap)
{
    int n = h_clip_n < 0 ? 0 : h_clip_n;

    if ((unsigned)n > cap)
        n = (int)cap;
    memcpy(buf, h_clip, (size_t)n);
    return n;
}

int os88_clip_size(void) { return h_clip_n; }

/* THE _seg FORMS, which are the ones Edit > Copy and Edit > Paste use: the
 * staging area is a transient heap CLAIM and never bss (APPLE2-SPEC section
 * 6.5). h_clip_refuse_put models a clipboard that will not take the screen -
 * over CLIP_MAXKB, or a kernel that refuses - which is a refusal the program
 * has to SAY (SPEC.md 47). */
static int h_clip_refuse_put;
static unsigned char *segbase(unsigned seg);

int os88_clip_put_seg(unsigned seg, unsigned off, unsigned len)
{
    if (h_clip_refuse_put)
        return -1;
    if (len > sizeof(h_clip))
        len = sizeof(h_clip);
    memcpy(h_clip, segbase(seg) + off, len);
    h_clip_n = (int)len;
    return 0;
}

int os88_clip_get_seg(unsigned seg, unsigned off, unsigned cap)
{
    int n = h_clip_n < 0 ? 0 : h_clip_n;

    if ((unsigned)n > cap)
        n = (int)cap;
    memcpy(segbase(seg) + off, h_clip, (size_t)n);
    return n;
}

/* ==========================================================================
 * THE CLAIMS - the Apple's 64KB and the ROM part
 * ========================================================================*/
#define H_RAMSEG 0x2000u
#define H_ROMSEG 0x1000u                /* >= A2_ROM_MINSEG (0x0D00), which
                                         * the package guards for */
static unsigned char h_ram[65536];
static unsigned char h_rom[14848];
static int claims_live;

/* THE TRANSIENT CLAIMS WAVE 4 ADDED - Copy's staging, Paste's queue and Load
 * and Save's file buffer (sections 6.5, 12). They are the point of several
 * assertions below rather than scenery: a claim that is not FREED is a leak
 * the machine would show as an arena that never comes back, and h_scr_live
 * plus claims_live are what see it. */
#define H_SCR_MAX  4
#define H_SCRSEG0  0x3000u
#define H_SCRBYTES 55296                /* 54KB - it was 48 (A2_PRGMAX rounded
                                         * up) until wave 5's foreign-frame
                                         * SHADOW, which is 280 x 192 = 53,760
                                         * bytes and is the largest transient
                                         * claim this package takes (section
                                         * 13.2) */
static unsigned char h_scr_mem[H_SCR_MAX][H_SCRBYTES];
static int h_scr_live[H_SCR_MAX];
static int h_scr_kb[H_SCR_MAX];
static int h_claim_refuse;
static int h_claim_keep;                    /* ...and hand a re-claimed slot
                                             * back with what was in it, which
                                             * is what the heap does after a
                                             * free and a same-size claim */              /* the next transient claim is refused */
static int h_claim_n;                   /* transient claims ever made */

static unsigned char *segbase(unsigned seg)
{
    if (seg == H_RAMSEG)
        return h_ram;
    if (seg == H_FSX_SEG)
        return h_fsx_fb;                /* THE FOREIGN GLASS (section 13) */
    if (seg == H_ROMSEG)
        return h_rom;
    if (seg >= H_SCRSEG0 && seg < H_SCRSEG0 + H_SCR_MAX) {
        int i = (int)(seg - H_SCRSEG0);

        if (!h_scr_live[i]) {
            fail("a claim read or written after it was freed");
            exit(1);
        }
        return h_scr_mem[i];
    }
    fail("a segment nobody claimed");
    exit(1);
}

unsigned os88_mem_claim(int kb)
{
    int i;

    if (kb == 64 && claims_live == 0) {
        claims_live++;
        return H_RAMSEG;                /* the Apple's own address space */
    }
    if (h_claim_refuse) {
        h_claim_refuse = 0;
        return 0;
    }
    if (kb < 1 || kb * 1024 > H_SCRBYTES) {
        fail("a transient claim of an impossible size");
        return 0;
    }
    for (i = 0; i < H_SCR_MAX; i++)
        if (!h_scr_live[i]) {
            h_scr_live[i] = 1;
            h_scr_kb[i] = kb;
            h_claim_n++;
            if (!h_claim_keep)
                memset(h_scr_mem[i], 0xCC, sizeof(h_scr_mem[i]));
            return H_SCRSEG0 + (unsigned)i;
        }
    fail("more transient claims live at once than the harness models - which "
         "on the machine is a claim nobody freed");
    return 0;
}

int os88_mem_free(unsigned seg)
{
    if (seg >= H_SCRSEG0 && seg < H_SCRSEG0 + H_SCR_MAX) {
        int i = (int)(seg - H_SCRSEG0);

        if (!h_scr_live[i])
            fail("a claim freed twice");
        h_scr_live[i] = 0;
        return 0;
    }
    claims_live--;
    return 0;
}

/* h_claims_out - how many transient claims are live. Copy's must be gone
 * inside its own wake; Paste's is held for exactly as long as there are bytes
 * left to type and not a wake longer (section 6.5). */
static int h_claims_out(void)
{
    int i, n = 0;

    for (i = 0; i < H_SCR_MAX; i++)
        n += h_scr_live[i];
    return n;
}
static int h_largest_kb = 200;          /* what the heap says it can spare -
                                         * a fixture, because the association
                                         * arm's ceiling is min(largest,
                                         * A2_PRGKB) and the two refusals it
                                         * can end at are told apart by which
                                         * of those two bound it */
unsigned os88_mem_largest_kb(void) { return (unsigned)h_largest_kb; }
unsigned os88_part_seg(int i) { return i == 0 ? H_ROMSEG : 0; }
int os88_peek(unsigned seg, unsigned off) { return segbase(seg)[off]; }
void os88_poke(unsigned seg, unsigned off, int v)
{ segbase(seg)[off] = (unsigned char)v; }

/* THE ONE HOST FILE (section 12). Load Program reads it and Save Program
 * writes it, so a save-then-load round trip is a real assertion about the
 * bytes and not about a mock. */
static unsigned char h_file[H_SCRBYTES];
static unsigned h_file_n;
static char h_file_name[16];
static int h_file_short;                /* the read comes up short: a media
                                         * error, which the program must SAY */
static int h_write_refuse;
static int h_dlg_up;                    /* another modal dialog owns the
                                         * screen, so os88_file_dlg refuses */
static int h_dlg_mode, h_dlg_n;
static char h_dlg_def[16];
static int h_ferr;

int os88_ferr(void) { return h_ferr; }

/* **THE STUB REFUSES THE WAY THE KERNEL REFUSES, AND THAT IS THE WHOLE
 * POINT OF IT.** It used to TRUNCATE - `if (n > cap) n = cap;` - which is a
 * behaviour no os8088 kernel has: kernel/diskw.inc:1836-1845 compares the
 * directory entry's 32-bit size against the caller's capacity BEFORE any data
 * I/O and answers FERR_BIG with the destination untouched, and apps/cc/os88.h
 * says so in words ("os88_file_read() refuses a short buffer with FERR_BIG
 * and reads nothing").
 *
 * A stub that truncates makes the program's "did the read get cut off?" arm
 * REACHABLE in the harness and unreachable on the machine, which is
 * LESSONS.md 7's "a stub that always refuses measures the fallback path" with
 * the sign flipped: the gate below it passes and the machine does something
 * else entirely. `h_file_short` is the OTHER thing - a media error, a genuine
 * short read - and it stays. */
static int h_read_n;                     /* how many reads have been made - the
                                         * lock gate below counts them */

unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    unsigned n = h_file_n;

    (void)name;
    h_read_n++;
    h_ferr = OS88_FERR_OK;
    if (n > cap) {                          /* the kernel's .toobig: nothing is
                                             * written and nothing is read */
        h_ferr = OS88_FERR_BIG;
        return 0;
    }
    if (h_file_short && n > 0)
        n--;
    memcpy(segbase(seg), h_file, (size_t)n);
    return n;
}

int os88_file_write_seg(const char *name, unsigned seg, unsigned count)
{
    h_read_n++;                         /* a write is a floppy operation too,
                                         * and os88.h says it stalls every
                                         * painter for its duration */
    if (h_write_refuse)
        return -1;
    if (count > sizeof(h_file))
        count = sizeof(h_file);
    memcpy(h_file, segbase(seg), (size_t)count);
    h_file_n = count;
    os88_strcpy(h_file_name, name, sizeof(h_file_name));
    return 0;
}

/* THE LAUNCH DOCUMENT (SPEC.md 54.5). h_arg names the file a double-click
 * would have handed this instance; it is READ-AND-CLEAR on the machine and it
 * is read-and-clear here. */
static char h_arg[13];
static int h_goto_fail, h_goto_n;

int os88_arg_file(char *name13, struct os88_place *p)
{
    if (!h_arg[0])
        return -1;
    os88_strcpy(name13, h_arg, 13);
    p->clus = 7;
    p->vol = 1;
    h_arg[0] = 0;
    return 0;
}

int os88_file_goto(struct os88_place *p)
{
    (void)p;
    h_goto_n++;
    return h_goto_fail ? -1 : 0;
}

int os88_file_dlg(int mode, void *win, const char *defname)
{
    (void)win;
    if (h_dlg_up)
        return -1;
    h_dlg_mode = mode;
    h_dlg_n++;
    os88_strcpy(h_dlg_def, defname ? defname : "", sizeof(h_dlg_def));
    return 0;                           /* it does NOT block: the answer
                                         * arrives at os88_onfile later, which
                                         * the script below delivers by hand */
}

void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memmove(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }

void os88_strcpy(char *d, const char *s, unsigned cap)
{
    unsigned i;

    for (i = 0; i + 1 < cap && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

char *os88_utoa(unsigned v, char *dst6)
{
    sprintf(dst6, "%u", v);
    return dst6;
}

/* ==========================================================================
 * THE PROGRAM
 * ========================================================================*/
#include "apple2.c"

/* ==========================================================================
 * THE ASSEMBLY, TRANSCRIBED FOR THE HOST
 *
 * a2mem.inc, a2band.inc and a2cpu.inc cannot run here. These are what those
 * files do, written from their own headers; tools/a2ref.py is the independent
 * check on the ALGORITHM and hosttest/a2memtest.sh the check on the encoding.
 * ========================================================================*/
struct a2_mach a2_m;

static unsigned char h_scr[256];        /* the core's scratch page, which on
                                         * the machine lives at $CF00 of the
                                         * claim itself */

int a2_rd(unsigned a) { return h_ram[a & 0xFFFF]; }

int a2_rd16(unsigned a)
{ return h_ram[a & 0xFFFF] | (h_ram[(a + 1) & 0xFFFF] << 8); }

static void h_mark(unsigned a)
{
    unsigned page = (a >> 8) & 0xFF;

    h_scr[A2_SCR_DIRTY + (page >> 3)] |= (unsigned char)(0x80 >> (page & 7));
    h_scr[A2_SCR_ANY] = 1;
}

void a2_wr(unsigned a, int v)
{
    unsigned wlo, whi, watlo, wathi;

    a &= 0xFFFF;
    if (a >= 0xC000)
        return;                         /* the write fence (section 3.2) */
    h_ram[a] = (unsigned char)v;
    watlo = h_scr[A2_SCR_WATLO] | (h_scr[A2_SCR_WATLO + 1] << 8);
    wathi = h_scr[A2_SCR_WATHI] | (h_scr[A2_SCR_WATHI + 1] << 8);
    if (a >= watlo && a <= wathi) {
        wlo = h_scr[A2_SCR_WLO] | (h_scr[A2_SCR_WLO + 1] << 8);
        whi = h_scr[A2_SCR_WHI] | (h_scr[A2_SCR_WHI + 1] << 8);
        if (a < wlo) {
            h_scr[A2_SCR_WLO] = (unsigned char)a;
            h_scr[A2_SCR_WLO + 1] = (unsigned char)(a >> 8);
        }
        if (a > whi) {
            h_scr[A2_SCR_WHI] = (unsigned char)a;
            h_scr[A2_SCR_WHI + 1] = (unsigned char)(a >> 8);
        }
    }
    h_mark(a);
}

void a2_dirty(unsigned a) { h_mark(a); }

void a2_watch_set(unsigned lo, unsigned hi)
{
    h_scr[A2_SCR_WATLO] = (unsigned char)lo;
    h_scr[A2_SCR_WATLO + 1] = (unsigned char)(lo >> 8);
    h_scr[A2_SCR_WATHI] = (unsigned char)hi;
    h_scr[A2_SCR_WATHI + 1] = (unsigned char)(hi >> 8);
}

void a2_scratch_clear(void)
{
    memset(h_scr, 0, sizeof(h_scr));
    h_scr[A2_SCR_WLO] = 0xFF;
    h_scr[A2_SCR_WLO + 1] = 0xFF;
}

static int n_take;

void a2_dirty_take(unsigned char *dst36)
{
    memcpy(dst36, h_scr + A2_SCR_DIRTY, 32);
    dst36[32] = h_scr[A2_SCR_WLO];
    dst36[33] = h_scr[A2_SCR_WLO + 1];
    dst36[34] = h_scr[A2_SCR_WHI];
    dst36[35] = h_scr[A2_SCR_WHI + 1];
    memset(h_scr + A2_SCR_DIRTY, 0, 32);
    h_scr[A2_SCR_WLO] = 0xFF;
    h_scr[A2_SCR_WLO + 1] = 0xFF;
    h_scr[A2_SCR_WHI] = 0;
    h_scr[A2_SCR_WHI + 1] = 0;
    h_scr[A2_SCR_ANY] = 0;
    n_take++;
}

int a2_rom_rd(unsigned off) { return h_rom[off % sizeof(h_rom)]; }

void a2_chargen(void *dst, unsigned seg, unsigned off, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned char *s = segbase(seg) + off;
    unsigned i;

    for (i = 0; i < n; i++)
        d[i] = s[i] & 0x7F;
}

void a2_zfill(unsigned a, int v, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        h_ram[(a + i) & 0xFFFF] = (unsigned char)v;
}

void a2_zcopy_in(unsigned a, const void *src, unsigned n)
{ memcpy(h_ram + (a & 0xFFFF), src, n); }

static int n_srcrd;                     /* forty-byte SOURCE reads, below */

/* AND IT IS COUNTED. a2_zcopy_out is how the flush reads a character row's
 * forty SOURCE bytes - the k-row shift test's compare and the shadow source it
 * keeps (a2scr.c) - so leaving it unpriced would have made every cost row
 * below read 0.31 ms a composed row too cheap. It replaced a2_rowsig, which
 * WAS counted, so the table's own history is the check. */
void a2_zcopy_out(void *dst, unsigned a, unsigned n)
{ n_srcrd++; memcpy(dst, h_ram + (a & 0xFFFF), n); }

int a2_wrote(void) { return h_scr[A2_SCR_ANY]; }

/* --- THE WAVE-4 SHIMS (APPLE2-SPEC sections 3.4, 6.5, 12) -----------------
 * A new assembly shim is a new host stub IN THE SAME EDIT (LESSONS.md 4): a
 * shim added to the package without its stub here fails to LINK, which is the
 * failure you want, three steps earlier than the one you do not.
 *
 * a2_copy_row is modelled from the SPECIFICATION and not from the assembly -
 * the fold, the trim and the CR - so the two are an independent pair. The
 * assembly's own gate is hosttest/a2memtest.asm, on a real x86 with SS != DS. */
static int n_copyrow;
static int n_srcrd_copy;                /* ...and the row reads one Copy made */

void a2_zzcopy_in(unsigned a, unsigned seg, unsigned off, unsigned n)
{ memmove(h_ram + (a & 0xFFFF), segbase(seg) + off, n); }

void a2_zzcopy_out(unsigned seg, unsigned off, unsigned a, unsigned n)
{ memmove(segbase(seg) + off, h_ram + (a & 0xFFFF), n); }

unsigned a2_scan0(unsigned seg, unsigned off, unsigned n)
{
    unsigned char *b = segbase(seg);
    unsigned i;

    for (i = 0; i < n; i++)
        if (b[off + i] == 0)
            return off + i;
    return 0xFFFFu;
}

int a2_copy_row(unsigned dseg, unsigned doff, int n)
{
    unsigned char *d = segbase(dseg) + doff;
    int i, last = 0, c;

    n_copyrow++;
    for (i = 0; i < n; i++) {
        c = a2_astab[a2_scrow[i] & 0x7F];
        d[i] = (unsigned char)c;
        if (c != ' ')
            last = i + 1;               /* one past the last non-space */
    }
    d[last] = 0x0D;                     /* the trailing spaces come off and the
                                         * separator is CR */
    return last + 1;
}

void a2_zpower(unsigned a, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        h_ram[(a + i) & 0xFFFF] = (unsigned char)(((i & 3) < 2) ? 0xFF : 0x00);
}

/* a2_bread - the Apple II READ LADDER (APPLE2-SPEC section 3.2), which is what
 * os88_main reads the reset vector through: RAM below $C000, the soft switches
 * at $C000-$C0FF, $FF for the empty slot space, and the ROM part above
 * $CFFF. The C's own a2_rd is RAM by construction and cannot reach $FFFC. */
int a2_bread(unsigned a)
{
    a &= 0xFFFF;
    if (a < 0xC000)
        return h_ram[a];
    if (a < 0xC100)
        return a2_io_rd(a);
    if (a < 0xD000)
        return 0xFF;
    return h_rom[(a - 0xD000) + A2_ROM_MAIN];
}

/* THE CORE ITSELF CANNOT RUN HERE - a2cpu.inc is 8086 assembly and
 * `make a2cputest` is its gate, twelve rows with a negative control each. What
 * the harness models is the SHAPE the wake depends on: a slice spends its
 * whole budget and answers A2_RUN_SLICE, so a2_m.cnt is 0 and `ran = asked -
 * cnt` is the budget. Nothing writes the Apple's memory, which is why every
 * fixture below pokes the text page itself. */
static long h_runs;                     /* ...and HOW MANY SLICES WERE RUN, so
                                         * "the machine is stopped while the
                                         * reboot confirmation is up" is a
                                         * test and not a claim */

/* ...AND TWO HOOKS, because inside the exclusive bracket the loop is the
 * kernel's for a whole session and THE CORE IS THE ONLY THING THE HARNESS CAN
 * DRIVE FROM IN THERE. h_run_jam makes the next slice answer A2_RUN_JAM (a
 * $02 opcode is what a wild jump reaches), which is the one path that can
 * reach a drawing slot from inside a bracket; h_run_wr_in makes the Nth slice
 * WRITE one cell, which is what an Applesoft COUT does and what the column
 * span exists to narrow. Neither is reachable any other way: no event is
 * dispatched in there, so there is no step between two frames. */
static int h_run_jam;
static int h_run_wr_in;                 /* slices until the write; 0 = never */
static unsigned h_run_wr_a;
static int h_run_wr_v;

/* ...AND A SLICE CAN BE GIVEN A WALL-CLOCK PRICE, which is the one thing the
 * slice controller (APPLE2-SPEC section 4.3.1) is about and the one thing a
 * run that returns instantly cannot exercise. `h_run_cost` is MICROTICKS of
 * host clock per emulated cycle - a millionth of an 18.2 Hz tick - and the
 * stub advances `the_ticks` by however many tick boundaries the slice crossed.
 * The phase carries over from slice to slice exactly as it does on the
 * machine, which is what makes the crossing RATE the duty cycle. 0 is the
 * default and is a run that costs nothing, which is every other case here. */
static unsigned h_run_cost;
static unsigned long h_run_ut;

int a2_run(unsigned cycles)
{
    h_runs++;
    a2_m.cnt = 0;
    if (h_run_cost) {
        unsigned long was = h_run_ut / 1000000UL;

        h_run_ut += (unsigned long)cycles * (unsigned long)h_run_cost;
        the_ticks += (unsigned)(h_run_ut / 1000000UL - was);
    }
    if (h_run_wr_in > 0 && --h_run_wr_in == 0)
        a2_wr(h_run_wr_a, h_run_wr_v);
    if (h_run_jam) {
        h_run_jam = 0;
        return A2_RUN_JAM;
    }
    return A2_RUN_SLICE;
}
void a2_cut(void) { }
void a2_rebias(void) { }

/* THE EMULATED CLOCK (APPLE2-SPEC section 5.1). On the machine it is
 * A2_SCR_CLKB - A2_SCR_DEAD, which is exact inside a run as well as outside
 * one; here the run is instantaneous, so the base IS the clock. It is masked
 * to sixteen bits because that is what it is on the target, and the paddle
 * deadlines are compared as wrapping differences. */
static unsigned h_clkb;

void a2_clk_set(unsigned v) { h_clkb = v & 0xFFFFu; }
int  a2_now(void) { return (int)(short)(unsigned short)h_clkb; }

/* a2_div32 (a2mem.inc) - (hi:lo) / d in ONE division, 0xFFFF on overflow.
 * The host has a 32-bit type and the target does not, which is the whole
 * point of the routine; the OVERFLOW GUARD is modelled too, because on an
 * 8086 a quotient that does not fit raises INT 0 and a package that takes
 * INT 0 is a machine that stops - the harness must be able to reach the arm
 * that prevents it. */
unsigned a2_div32(unsigned hi, unsigned lo, unsigned d)
{
    unsigned long n = ((unsigned long)hi << 16) | (unsigned long)lo;

    if (d == 0 || (unsigned long)hi >= (unsigned long)d)
        return 0xFFFFu;
    return (unsigned)(n / (unsigned long)d);
}

/* --- the composer, transcribed (a2band.inc) ------------------------------ */
static int n_band, n_group;
/* ...and PER COMPOSER, because the three do not cost the same: a hi-res group
 * is eight scan lines and measures 3.444 ms against text's 2.434 and lo-res'
 * 1.997 (wave 3's `make a2bandbench`). One counter would price a hi-res
 * repaint at the text composer's figure. */
static int n_band_l, n_group_l, n_band_h, n_group_h, n_x2;
static long n_gl_h, n_line_h;                         /* ...and hi-res GROUP-LINES: the
                                             * composer takes a scan-line
                                             * range, so a group is not one
                                             * unit of work any more */
static long n_x2_u;                         /* ...and the SOURCE BYTE-ROWS it
                                             * was asked for, which is what
                                             * the doubling is priced by now
                                             * that it is per RUN */

void a2_band_text(unsigned char *dst, int g0, int g1,
                  unsigned mseg, unsigned moff, int fmask)
{
    unsigned char *src = segbase(mseg) + moff;
    int g, c, line, j, mask;
    unsigned char gl[8];

    if (g1 < g0)
        return;
    n_band++;
    n_group += g1 - g0 + 1;
    for (line = 0; line < 8; line++) {
        for (g = g0; g <= g1; g++) {
            for (c = 0; c < 8; c++) {
                int b = src[g * 8 + c];

                if (b < 0x40)
                    mask = 0x7F;
                else if (b < 0x80)
                    mask = fmask;
                else
                    mask = 0;
                gl[c] = (unsigned char)((a2_chr[(b & 0x3F) * 8 + line] & 0x7F)
                                        ^ mask);
            }
            /* eight cells of seven bits are seven bytes, MSB first */
            for (j = 0; j < 7; j++)
                dst[line * A2_BSTRIDE + A2_LBOXB + g * 7 + j] =
                    (unsigned char)(((gl[j] << (j + 1)) & 0xFF)
                                    | (gl[j + 1] >> (6 - j)));
        }
    }
}

/* a2_pack, transcribed once: eight cells of seven bits into seven bytes, MSB
 * first. It is ONE routine in a2band.inc and it is one here, for the same
 * reason - the three composers differ only in what each cell contributes. */
static void h_pack(unsigned char *dst, int g0, int g1,
                   const unsigned char *grow, int s0, int nlines)
{
    int g, line, j;

    for (line = s0; line < s0 + nlines; line++)
        for (g = g0; g <= g1; g++)
            for (j = 0; j < 7; j++)
                dst[line * A2_BSTRIDE + A2_LBOXB + g * 7 + j] =
                    (unsigned char)
                    (((grow[line * A2_COLS + g * 8 + j] << (j + 1)) & 0xFF)
                     | (grow[line * A2_COLS + g * 8 + j + 1] >> (6 - j)));
}

static unsigned char h_grow[8 * A2_COLS];

/* LO-RES: two nibbles a byte, the low one the top four scan lines of the
 * character row and the high one the bottom four, each through the luminance
 * ladder's pattern table. A monochrome block is UNIFORM, so its contribution
 * is one seven-bit constant. */
void a2_band_lores(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff)
{
    unsigned char *src = segbase(mseg) + moff;
    int g, c, line;

    if (g1 < g0)
        return;
    n_band_l++;
    n_group_l += g1 - g0 + 1;
    for (line = 0; line < 8; line++)
        for (g = g0; g <= g1; g++)
            for (c = 0; c < 8; c++)
                h_grow[line * A2_COLS + g * 8 + c] =
                    a2_lopat[(src[g * 8 + c] >> ((line < 4) ? 0 : 4)) & 0x0F];
    h_pack(dst, g0, g1, h_grow, 0, 8);
}

/* HI-RES: forty source bytes a SCAN LINE, the lines of the row group $400
 * apart, each byte through the 7-bit reverse table with bit 7 dropped.
 *
 * IT TAKES A SCAN-LINE RANGE, which the other two composers do not: hi-res is
 * the one mode the damage model marks a line at a time (a2band.inc). The
 * counter is therefore GROUPS x LINES and not groups - a one-line HPLOT is an
 * eighth of a whole row group's work and the cost table has to say so. */
void a2_band_hires(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff, int s0, int nlines)
{
    unsigned char *base = segbase(mseg) + moff;
    int g, c, line;

    if (g1 < g0 || nlines <= 0)
        return;
    if (s0 < 0 || s0 + nlines > 8)
        fail("a2_band_hires was asked for scan lines outside the row group");
    /* n_group_h STAYS THE GROUP SPAN and the LINES are counted beside it.
     * Every budget assertion in this file is written in groups - "the split
     * owns four rows, so it owes at most 20" - and folding the lines into
     * that number would make those sentences mean something else. The cost
     * model prices GROUP-LINES, which is what the composer actually does. */
    n_band_h++;
    n_group_h += g1 - g0 + 1;
    n_gl_h += (g1 - g0 + 1) * nlines;
    n_line_h += nlines;
    for (line = s0; line < s0 + nlines; line++)
        for (g = g0; g <= g1; g++)
            for (c = 0; c < 8; c++)
                h_grow[line * A2_COLS + g * 8 + c] =
                    a2_rev[base[line * 0x400 + g * 8 + c] & 0x7F];
    h_pack(dst, g0, g1, h_grow, s0, nlines);
}

static int n_flash, n_span, n_sig, n_copy;

int a2_rowflash(unsigned mseg, unsigned moff, int n)
{
    unsigned char *src = segbase(mseg) + moff;
    int i;

    n_flash++;
    for (i = 0; i < n; i++)
        if (src[i] >= 0x40 && src[i] < 0x80)
            return 1;
    return 0;
}

int a2_rowspan(const unsigned char *a, const unsigned char *b, int n)
{
    int f = -1, l = -1, i;

    n_span++;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) {
            if (f < 0)
                f = i;
            l = i;
        }
    if (f < 0)
        return -1;
    return (f << 8) | l;
}

void a2_rowcopy(unsigned char *dst, const unsigned char *src, int n)
{ n_copy++; memcpy(dst, src, (size_t)n); }

unsigned a2_rowsig(unsigned mseg, unsigned moff, int n)
{
    unsigned char *src = segbase(mseg) + moff;
    unsigned ax = 0;
    int i;

    n_sig++;
    for (i = 0; i < n; i++) {
        ax = (ax & 0xFF00) | ((ax ^ src[i]) & 0xFF);
        ax = ((ax << 1) | (ax >> 15)) & 0xFFFF;
    }
    return ax;
}

static unsigned char h_x2tab[512];

/* ==========================================================================
 * a2fsx.inc, TRANSCRIBED (APPLE2-SPEC section 13)
 *
 * MII's rule is written out here the long way - five branches and three
 * shifts a pixel, exactly as `_mii_line_render_hires` has it - where the
 * package flattens it into a 128-entry table. That is deliberate and it is
 * this harness's whole method: a transcription that agrees with the shipping
 * routine proves the RULE, and the only thing it cannot prove is the
 * FLATTENING, which is why the wave's done_when is a screendump inside
 * FSXM_VGA13 and not this file (LESSONS.md 7's "a transcription that is
 * correct is precisely what makes the real routine's defect invisible").
 * ========================================================================*/
static int n_fsxrow_t, n_fsxrow_l, n_fsxrow_h;
static long n_fsxcell_t, n_fsxcell_l, n_fsxcell_h;
static const unsigned char h_locol[16] = {
    0, 6, 7, 1, 8, 9, 3, 11, 12, 4, 10, 13, 2, 14, 15, 5
};
static const unsigned char h_hicol[10] = { 0, 1, 2, 2, 1, 3, 4, 4, 3, 5 };

void a2_fsx_init(void) { }
void a2_fsx_dac(void) { }

/* ...AND IT TAKES A CELL RANGE, WHICH THIS MODEL HONOURS INDEPENDENTLY. The
 * shipping routine composes `ncells` cells from `cell0`; a model that
 * composed all forty and let the caller's offsets pick a slice out would
 * agree with it on every byte and prove nothing about the one thing that is
 * hard here - a hi-res range's cross-cell state (the byte before it, the byte
 * after it, and the cell's parity), which is what makes a partial compose
 * produce the bytes a whole one would. So the loop starts at cell0, ends at
 * cell0 + ncells, and reads its neighbours out of the SOURCE. */
void a2_fsx_row(unsigned char *dst, int mode, unsigned mseg, unsigned moff,
                int line, int fmask, int cell0, int ncells)
{
    const unsigned char *src = segbase(mseg) + moff;
    int c, i, mask, g, n, b0, b1, b2, run, odd, off, idx, cend;

    if (cell0 < 0 || ncells < 1 || cell0 + ncells > 40) {
        fail("a2_fsx_row was asked for a cell range outside the Apple's "
             "forty cells");
        return;
    }
    cend = cell0 + ncells;
    if (mode == 2) {
        n_fsxrow_h++;
        n_fsxcell_h += (long)ncells;
        b0 = cell0 ? src[cell0 - 1] : 0;
        b1 = src[cell0];
        for (c = cell0; c < cend; c++) {
            b2 = (c == 39) ? 0 : src[c + 1];
            run = ((b0 & 0x60) >> 5) | ((b1 & 0x7F) << 2) | ((b2 & 0x03) << 9);
            odd = (c & 1) << 1;
            off = (b1 & 0x80) >> 5;
            for (i = 0; i < 7; i++) {
                int left = (run >> (1 + i)) & 1;
                int pix = (run >> (2 + i)) & 1;
                int right = (run >> (3 + i)) & 1;

                idx = 0;
                if (pix)
                    idx = (left || right) ? 9 : off + odd + (i & 1) + 1;
                else if (left && right)
                    idx = off + odd + 1 - (i & 1) + 1;
                dst[c * 7 + i] = h_hicol[idx];
            }
            b0 = b1;
            b1 = b2;
        }
        return;
    }
    if (mode == 1)
    {
        n_fsxrow_l++;
        n_fsxcell_l += (long)ncells;
    } else {
        n_fsxrow_t++;
        n_fsxcell_t += (long)ncells;
    }
    for (c = cell0; c < cend; c++) {
        if (mode == 1) {
            n = (line < 4) ? (src[c] & 0x0F) : ((src[c] >> 4) & 0x0F);
            for (i = 0; i < 7; i++)
                dst[c * 7 + i] = h_locol[n];
            continue;
        }
        b0 = src[c];
        mask = (b0 < 0x40) ? 0x7F : ((b0 < 0x80) ? (fmask & 0xFF) : 0x00);
        g = (a2_chr[(b0 & 0x3F) * 8 + line] ^ mask) & 0x7F;
        for (i = 0; i < 7; i++)
            dst[c * 7 + i] = (g & (0x40 >> i)) ? 5 : 0;   /* WHITE on BLACK */
    }
}

/* a2_fsx_put - THE SPAN COMPARE ONE GEOMETRY ALONG, and the harness COUNTS
 * its two answers separately: "nothing moved" is 184 of the 192 scan lines on
 * an ordinary keystroke and is what makes the foreign path affordable at all
 * (section 13.2). A model that always wrote would price the wrong path. */
static int n_fsxput, n_fsxsame;
static long n_fsxbytes;                 /* bytes actually COPIED... */
static long n_fsxcmp_d, n_fsxcmp_s;     /* ...and bytes COMPARED, per arm,
                                         * which is what the cost table
                                         * prices: a2_fsx_put takes a byte
                                         * range now, so a call is a floor
                                         * plus a slope and no longer one
                                         * 280-byte constant */

/* a2_fsx_zero - the shadow made TRUE at entry, because SPEC.md 53.4's mode set
 * clears the screen and a2_fsx_put's compare has no other way to know the
 * frame it is comparing against was thrown away. The claim stub fills with
 * 0xCC on purpose, and `h_claim_keep` makes it hand a re-claimed slot back
 * UNCHANGED - which is what the real heap does after a free and a same-size
 * claim, and is the case that found this. */
void a2_fsx_zero(unsigned seg, unsigned off, unsigned n)
{
    memset(segbase(seg) + off, 0, (size_t)n);
}

int a2_fsx_put(unsigned fbseg, unsigned fboff, unsigned shseg, unsigned shoff,
               const unsigned char *src, int n)
{
    unsigned char *fb = segbase(fbseg) + fboff;
    unsigned char *sh = segbase(shseg) + shoff;
    int f, l;

    for (f = 0; f < n; f++)
        if (src[f] != sh[f])
            break;
    if (f >= n) {
        n_fsxsame++;
        n_fsxcmp_s += (long)n;
        return 0;
    }
    n_fsxcmp_d += (long)n;
    for (l = n - 1; l > f; l--)
        if (src[l] != sh[l])
            break;
    memcpy(fb + f, src + f, (size_t)(l - f + 1));
    memcpy(sh + f, src + f, (size_t)(l - f + 1));
    n_fsxput++;
    n_fsxbytes += (long)(l - f + 1);
    return 1;
}

/* a2_fsx_key - the polled int 16h, from a queue the test fills. 0xFFFF is
 * "the buffer is empty", which is the answer 53.1's bracket sees on almost
 * every frame and is the one that must never block.
 *
 * IT IS `unsigned` AND THE MARKER IS 0xFFFF BECAUSE THE TARGET'S IS, and this
 * stub is exactly where the two used to disagree: a host `int` is 32 bits, so
 * a -1 marker and a signed `k >= 0` test both work here and neither works on
 * the 8086, where AH=0xA6 is k = -22528. A stub whose sentinel is cheaper
 * than the machine's is LESSONS.md 9's rule with the sign flipped - copy the
 * refusal, not the convenience. */
#define A2H_NOKEY 0xFFFF                /* ...and every step below queues THIS
                                         * and not -1, so a test author cannot
                                         * write the marker the target does
                                         * not use */
unsigned a2_fsx_key(void)
{
    if (h_fsx_keyi >= h_fsx_keyn)
        return A2H_NOKEY;
    /* MASKED TO SIXTEEN BITS, because AX is sixteen bits. A host `int` is 32
     * and that width is exactly what hid the defect this stub now models. */
    return (unsigned)(h_fsx_keyq[h_fsx_keyi++] & 0xFFFF);
}

void a2_x2init(void)
{
    int i, b, v;

    for (i = 0; i < 256; i++) {
        v = 0;
        for (b = 7; b >= 0; b--)
            v = (v << 2) | (((i >> b) & 1) ? 3 : 0);
        h_x2tab[i * 2] = (unsigned char)(v >> 8);
        h_x2tab[i * 2 + 1] = (unsigned char)v;
    }
}

void a2_band_x2(unsigned char *dst, const unsigned char *src, int nbytes,
                int rows)
{
    int r, c;

    n_x2++;
    if (nbytes > 0 && rows > 0)
        n_x2_u += (long)nbytes * rows;

    /* THE WINDOW MOVES AND THE STRIDES DO NOT: source rows are A2_BSTRIDE
     * apart and destination rows 80, because what is doubled is a rectangle
     * inside each row (a2band.inc). */
    for (r = 0; r < rows; r++) {
        for (c = 0; c < nbytes; c++) {
            dst[r * 2 * 80 + c * 2] = h_x2tab[src[r * A2_BSTRIDE + c] * 2];
            dst[r * 2 * 80 + c * 2 + 1] =
                h_x2tab[src[r * A2_BSTRIDE + c] * 2 + 1];
        }
        memcpy(dst + (r * 2 + 1) * 80, dst + r * 2 * 80, (size_t)nbytes * 2);
    }
}

/* ==========================================================================
 * THE AUDIT: THE GLASS SHOWS WHAT THE SHADOW SAYS IT SHOWS
 * ========================================================================*/
static int audit(const char *where)
{
    int line, byte, bit, want, got, bad = 0;

    /* THE BAND IS a2_gnl LINES FROM a2_gl0 AND NOT "everything below
     * a2_gl0". That was the same thing while the anchor was the BOTTOM of
     * the Apple frame, and it stopped being one when the band started
     * following the cursor: on a cold start a2_gl0 is 0 and the band ends at
     * line 110, so a loop that ran to 191 sampled the glass eighty lines
     * BELOW the band - other windows' pixels - and reported them as the
     * shadow lying. */
    for (line = a2_gl0; line < a2_gl0 + a2_gnl && line < A2_SCRH; line++) {
        if (a2_abt_up && line >= a2_hold_l0 && line <= a2_hold_l1)
            continue;                   /* a panel owns these lines */
        for (byte = 0; byte < A2_BSTRIDE; byte++)
            for (bit = 0; bit < 8; bit++) {
                want = (a2_sh[line * A2_BSTRIDE + byte] >> (7 - bit)) & 1;
                /* THE SHADOW IS IN APPLE PIXELS AND THE GLASS IS IN SCREEN
                 * PIXELS, and at full screen those are not the same thing
                 * (APPLE2-SPEC section 7.8: the doubling is at BLIT time).
                 * The audit therefore samples the FIRST screen pixel of each
                 * Apple pixel - which is enough, because a2_band_x2 writes
                 * both of a pair from one source bit and the sample below
                 * would catch a doubler that wrote one of them from
                 * somewhere else. */
                got = glass[a2_gsy + ((line - a2_gl0) * a2_sch)]
                           [a2_gsx + (byte * 8 + bit) * a2_scw];
                if (got != want) {
                    if (bad < 4)
                        printf("a2uitest:   %s: scan line %d, band byte %d "
                               "bit %d: the shadow says %d, the glass says "
                               "0x%02X\n",
                               where, line, byte, bit, want, got);
                    bad++;
                }
            }
    }
    if (bad) {
        printf("a2uitest: FAIL - %s: %d pixel(s) where the glass and the "
               "shadow disagree\n", where, bad);
        fails++;
    }
    return bad;
}

/* no_gunk - NOT ONE PIXEL OF THE CONTENT BOX IS STILL UNDRAWN.
 *
 * THE AUDIT ABOVE CANNOT SEE THIS. It compares the band against the shadow,
 * and the band is 320 x 192 of a content box that is 336 x 218 - so the
 * border strips and the STATUS ROW are outside everything it looks at. The
 * glass starts as GUNK, a value no primitive can produce, and WF_OWNBG means
 * the kernel paints no background: a pixel of the content box that is still
 * GUNK after a full paint is a pixel NOBODY draws, and on the machine it
 * holds whatever window was underneath.
 *
 * That is exactly how the status strip shipped. A2_STATH is 10 and
 * os88_font_run draws EIGHT rows, so scan lines a2_gsty+8 and +9 were painted
 * by nothing at all - a2_border_fill stops at a2_gsty-1 - and in FULLSCREEN
 * the strip's 42 cells reach 336 px of a 638-px box, so the untouched region
 * was a white bar the width of the screen (build/port-shots/
 * wave1-verify-28-fullscreen.png). The harness's own font_run stub is eight
 * rows too, which is why modelling the glass was not enough on its own: the
 * question had to be asked. */
static void no_gunk(const char *where)
{
    int x, y, bad = 0, fx = -1, fy = -1;

    for (y = h_cont_y; y < h_cont_y + h_cont_h; y++)
        for (x = h_cont_x; x < h_cont_x + h_cont_w; x++)
            if (glass[y][x] == GUNK) {
                if (bad == 0) {
                    fx = x;
                    fy = y;
                }
                bad++;
            }
    if (bad) {
        printf("a2uitest: FAIL - %s: %d pixel(s) of the content box were "
               "NEVER DRAWN, first at box offset (%d,%d) of %dx%d - the "
               "status strip is A2_STATH=%d rows at box y %d..%d and "
               "font_run draws eight of them\n",
               where, bad, fx - h_cont_x, fy - h_cont_y, h_cont_w, h_cont_h,
               A2_STATH, a2_gsty - h_cont_y,
               a2_gsty - h_cont_y + A2_STATH - 1);
        fails++;
    }
}

/* ==========================================================================
 * THE COST MODEL - MEASURED, not guessed (APPLE2-SPEC section 7.9)
 *
 * PERFORMANCE.md's own prices for the kernel's primitives, and this package's
 * OWN figures for its composer, taken with
 *
 *   make a2bandbench
 *   make test TESTAPPS=build/a2band.img QEMU="qemu-system-i386 -icount shift=3"
 *
 * where ONE PIT COUNT IS 0.359 ms OF A REAL 4.77 MHz XT (PERFORMANCE.md
 * Part 4). The bench's counts, per operation, and what they convert to:
 *
 *   BANDTEXT 5 groups   35.000 counts   12.57 ms   (40 cells)
 *   BANDTEXT 1 group     7.875 counts    2.83 ms   (8 cells)
 *      -> per group 6.781 counts = 2.434 ms, call floor 1.094 = 0.393 ms
 *   BLIT1 320x8          4.875 counts    1.75 ms
 *   ROWSPAN 40           0.875 counts    0.31 ms
 *   ROWCOPY 40           0.875 counts    0.31 ms
 *   ROWSIG 40            1.875 counts    0.67 ms   (off the flush's path now:
 *                                                   the shift test compares
 *                                                   the forty SOURCE bytes)
 *   a 40-byte SOURCE read  = ROWCOPY, 0.31 ms      (the same `rep movsb`)
 *   ROWFLASH 40          2.875 counts    1.03 ms
 *   BAND_X2 8r x 40b    29.500 counts   10.59 ms   (a whole character row of
 *                                                   doubling - what entering
 *                                                   full screen pays per row)
 *   BAND_X2 1r x 7b      1.125 counts    0.40 ms   (...and ONE RUN of one
 *                                                   scan line, which is the
 *                                                   shape the per-run change
 *                                                   exists for)
 *      -> per source byte-row 0.0907 counts = 0.0325 ms,
 *         call floor 0.490 = 0.176 ms
 *
 * THE BANDTEXT ROWS ABOVE ARE WAVE 1'S AND ARE KEPT DELIBERATELY: wave 3's
 * review re-measured `BANDTEXT 5 groups` at 35.000 counts / 12.57 ms - wave
 * 1's figure exactly - and `BANDTEXT 1 group` at 8.125 against 7.875, a
 * quarter of a count, which is the two `mov word [mem], imm` the composers
 * now spend telling a2_pack its pixel-row range. That is 0.09 ms a call on
 * the target and every published row that rests on these stands.
 *
 * BAND_X2 IS REPLACED rather than kept, because it is not one row any more:
 * the routine takes a byte count and is called PER RUN, so it is priced with
 * a floor and a per-source-byte-row term and the two BAND_X2 rows above are
 * the two points that fix them. 0.176 + 320 x 0.0325 = 10.58, which is the
 * whole-row row back to two places.
 *
 * MEASURE THE BAND BEFORE BELIEVING A PER-CELL GUESS: 2.434 ms a GROUP is
 * 304 us a CELL, which is eight times what a naive model of "a table lookup
 * and eight stores" would have said, and it is the number the tier table in
 * wave 3 gets written from.
 * ========================================================================*/
#define MS_GFXCALL  0.756               /* PERFORMANCE.md: any gfx_* call */
#define MS_GLYPH    0.900               /* ...and one 8x8 glyph cell */
#define MS_BAND     0.393               /* a2_band_text's call floor */
#define MS_GROUP    2.434               /* ...and per eight-cell group */
/* ...AND THE OTHER TWO COMPOSERS HAVE THEIR OWN PRICES (wave 3's
 * `make a2bandbench`, the same icount recipe: one PIT count is 0.359 ms of a
 * real 4.77 MHz XT). Pricing a hi-res row at the text composer's figure would
 * be quoting one routine's measurement for another's work, and the two differ
 * by 42 %:
 *
 *   BANDTEXT  5 groups 35.000 counts/op, 1 group  8.125  -> 6.719/grp, 1.41 floor
 *   BANDLORES 5 groups 28.625,           1 group  6.750  -> 5.469/grp, 1.28 floor
 *
 * HI-RES IS THREE ROWS AND NOT TWO, because it takes a SCAN-LINE RANGE and
 * the other two composers do not (a2band.inc): a group is no longer one unit
 * of work, so a two-point fit in groups alone prices a one-line call 23 % too
 * high - and the one-line call is the whole reason the range is there.
 *
 *   BANDHIRES 5 grp x8ln  50.875 counts/op
 *   BANDHIRES 1 grp x8ln  12.375
 *   BANDHIRES 5 grp x1ln   7.125
 *      -> floor 0.875, per LINE 0.234, per GROUP-LINE 1.203 counts
 *         (0.875 + 8 x 0.234 + 40 x 1.203 = 50.875 exactly)
 *
 * The text and lo-res figures are wave 1's and are KEPT: BANDTEXT 5 groups
 * re-measures at wave 1's own 35.000, and the one-group floors moved by a
 * quarter of a count - the two stores that hand a2_pack its row range. */
#define MS_BAND_L   0.539               /* a2_band_lores' call floor */
#define MS_GROUP_L  1.930               /* ...and per eight-cell group */
#define MS_BAND_H   0.314               /* a2_band_hires' call floor... */
#define MS_LINE_H   0.084               /* ...per SCAN LINE, whatever it is
                                         * wide: the source pointer walk and
                                         * a2_pack's own row step */
#define MS_GL_H     0.432               /* ...and per GROUP-LINE, which is the
                                         * work itself. A group is not one
                                         * unit here, because the composer
                                         * takes a scan-line range: a whole
                                         * row group is 0.314 + 8 x 0.084 +
                                         * 40 x 0.432 = 18.27 ms and one line
                                         * of it is 0.918 */
#define MS_X2      10.590               /* a2_band_x2 over a WHOLE character
                                         * row - 8 rows x 40 bytes, which is
                                         * what entering full screen pays per
                                         * row. Kept because the tier table is
                                         * written from it */
#define MS_X2_CALL  0.176               /* ...and the model's two terms, now
                                         * that the routine is called PER RUN:
                                         * the call floor... */
#define MS_X2_B     0.0325              /* ...and per SOURCE BYTE-ROW.
                                         * 0.176 + 320 x 0.0325 = 10.58, which
                                         * is MS_X2 back to two places; the
                                         * bench's two BAND_X2 rows are the two
                                         * points that fix them */
#define MS_BLIT2    3.190               /* ...and the 640x16 blit that puts
                                         * the doubled band down. It is 1.85x
                                         * the 320x8 figure for FOUR times the
                                         * pixels, and it was 111 ms until the
                                         * bench's own window was widened to
                                         * 640: at 632 every one of those
                                         * blits went down the CLIPPED path,
                                         * which is not the path full screen
                                         * takes (tests/a2band's ab_tpl) */
#define MS_BLIT     1.750               /* one 320x8 blit1 */
#define MS_SPAN     0.314
#define MS_COPY     0.314
#define MS_SIG      0.673
#define MS_SRCRD    0.314               /* ONE CHARACTER ROW'S FORTY SOURCE
                                         * BYTES, read by a2_zcopy_out for the
                                         * shift test and the shadow source it
                                         * keeps. IT IS ROWCOPY'S MEASURED
                                         * FIGURE and not a guess of its own:
                                         * both are one `rep movsb` over the
                                         * same forty bytes and differ by the
                                         * two segment loads a2_zcopy_out makes
                                         * (a2mem.inc) - about ten cycles, or
                                         * 0.002 ms on a 4.77 MHz 8088, which
                                         * is under one part in a hundred of
                                         * the figure. It is a MEASURED number
                                         * with a bounded correction on it,
                                         * not a per-byte guess of the kind
                                         * section 7.9 says never to believe */
#define MS_FLASH    1.032
#define MS_TAKE     0.190               /* a2_dirty_take, section 7.5 */
/* THE FOREIGN VIDEO MODE'S FOUR (APPLE2-SPEC section 13.4), all of them wave
 * 5's `make a2bandbench` under -icount shift=3, where one PIT count is
 * 0.359 ms of a real 4.77 MHz XT. They are what makes the harness's foreign
 * row a COST and not a decoration, and the arithmetic checks against the
 * bench's own whole-frame row: 192 x (15.39 + 2.15) = 3,368 ms against the
 * measured 3,365. */
/* BOTH ROUTINES TAKE A RANGE, SO BOTH ARE A FLOOR PLUS A SLOPE, and each is
 * fitted from TWO measured widths rather than divided out of one. A single
 * per-call constant priced every compose at the forty-cell figure, which
 * reports the column narrowing as free - the exact shape of error
 * PERFORMANCE.md's own rule 4 warns about, in the table that is supposed to
 * catch it. */
#define MS_FSXROW_T 0.180               /* a2_fsx_row's call floor, TEXT: the
                                         * 40-cell row is 20.500 counts and
                                         * the 8-cell one 4.500, so the slope
                                         * is 16.000/32 = 0.500 counts a cell
                                         * and the floor is what is left */
#define MS_FSXCELL_T 0.180              /* ...per cell */
#define MS_FSXROW_L 0.213               /* ...LO-RES: 7.000 and 1.875 */
#define MS_FSXCELL_L 0.0575
#define MS_FSXROW_H 0.359               /* ...and HI-RES: 42.875 and 9.375,
                                         * which is MII's five branches a
                                         * pixel over seven pixels a cell */
#define MS_FSXCELL_H 0.3758
#define MS_FSXPUT   0.239               /* a2_fsx_put's call floor when the
                                         * range MOVED: 280 bytes is 6.000
                                         * counts and 70 is 2.000 */
#define MS_FSXBYTE_D 0.006838           /* ...and its per-byte slope - a
                                         * compare and two copies */
#define MS_FSXSAME  0.120               /* ...and when nothing moved: 3.000
                                         * and 1.000, which is 184 of the 192
                                         * lines on an ordinary keystroke and
                                         * is the whole reason the foreign
                                         * path is affordable (section 13.2) */
#define MS_FSXBYTE_S 0.003419           /* ...one `repe cmpsb` a byte */

/* --- EDIT > COPY (APPLE2-SPEC section 6.5) --------------------------------
 * THESE TWO ARE DERIVED AND NOT MEASURED, and the difference is stated
 * because everything else in this table came off tests/a2band's icount
 * harness. a2_copy_row is in a2mem.inc, which that harness does not %include
 * - it would need the register file and a claim segment - so its per-cell
 * figure is the 8088's own INSTRUCTION-FETCH FLOOR over the loop as written,
 * PERFORMANCE.md Part 2's max(clocks, 4.34 x instruction bytes) applied
 * instruction by instruction:
 *
 *   mov bl,[si] 13 | inc si 4.34 | and bl,7F 13.02 | mov al,[tab+bx] 17.36
 *   mov [es:di],al 14 | inc di 4.34 | cmp al,' ' 8.68 | je 8.68
 *   mov dx,di 8.68 | dec cx 4.34 | jnz 16          = 112.4 clocks
 *
 * which at 4.77 MHz is 23.6 us a cell; the call floor is the near call and
 * return (11 us, CLAUDE.md's table) plus a nine-instruction prologue. A
 * MEASURED figure would be better and is what the Disk II wave's own bench
 * should take if a2mem.inc ever joins that harness. */
#define MS_CPYCELL  0.0236              /* ...per folded cell, DERIVED */
#define MS_CPYCALL  0.030               /* ...and per row, DERIVED */

static int c_blit, c_fill, c_frame, c_run, c_cells, c_scroll;
static int c_band, c_group, c_span, c_sig, c_flash, c_copy, c_take, c_srcrd;
static int c_band_l, c_group_l, c_band_h, c_group_h, c_x2, c_blit2;
static int c_fsxrow_t, c_fsxrow_l, c_fsxrow_h, c_fsxput, c_fsxsame;
static long c_fsxcell_t, c_fsxcell_l, c_fsxcell_h;
static long c_fsxcmp_d, c_fsxcmp_s;
static long c_gl_h, c_line_h, c_x2_u;

static void cost_mark(void)
{
    c_blit = n_blit; c_fill = n_fill; c_frame = n_frame; c_run = n_run;
    c_cells = n_cells; c_scroll = n_scroll; c_band = n_band;
    c_group = n_group; c_span = n_span; c_sig = n_sig; c_flash = n_flash;
    c_copy = n_copy; c_take = n_take; c_srcrd = n_srcrd;
    c_band_l = n_band_l; c_group_l = n_group_l;
    c_band_h = n_band_h; c_group_h = n_group_h; c_gl_h = n_gl_h; c_line_h = n_line_h;
    c_x2 = n_x2; c_x2_u = n_x2_u; c_blit2 = n_blit2;
    c_fsxrow_t = n_fsxrow_t; c_fsxrow_l = n_fsxrow_l; c_fsxrow_h = n_fsxrow_h;
    c_fsxcell_t = n_fsxcell_t; c_fsxcell_l = n_fsxcell_l;
    c_fsxcell_h = n_fsxcell_h;
    c_fsxcmp_d = n_fsxcmp_d; c_fsxcmp_s = n_fsxcmp_s;
    c_fsxput = n_fsxput; c_fsxsame = n_fsxsame;
}

static void cost_row(const char *what)
{
    double ms = (n_blit - c_blit - (n_blit2 - c_blit2)) * (MS_GFXCALL + MS_BLIT)
              + (n_blit2 - c_blit2) * (MS_GFXCALL + MS_BLIT2)
              + (n_x2 - c_x2) * MS_X2_CALL
              + (double)(n_x2_u - c_x2_u) * MS_X2_B
              + (n_band_l - c_band_l) * MS_BAND_L
              + (n_group_l - c_group_l) * MS_GROUP_L
              + (n_band_h - c_band_h) * MS_BAND_H
              + (double)(n_line_h - c_line_h) * MS_LINE_H
              + (double)(n_gl_h - c_gl_h) * MS_GL_H
              + (n_fill - c_fill) * MS_GFXCALL
              + (n_frame - c_frame) * MS_GFXCALL
              + (n_scroll - c_scroll) * MS_GFXCALL
              + (n_run - c_run) * MS_GFXCALL
              + (n_cells - c_cells) * MS_GLYPH
              + (n_band - c_band) * MS_BAND
              + (n_group - c_group) * MS_GROUP
              + (n_span - c_span) * MS_SPAN
              + (n_sig - c_sig) * MS_SIG
              + (n_srcrd - c_srcrd) * MS_SRCRD
              + (n_flash - c_flash) * MS_FLASH
              + (n_copy - c_copy) * MS_COPY
              + (n_take - c_take) * MS_TAKE
              + (n_fsxrow_t - c_fsxrow_t) * MS_FSXROW_T
              + (double)(n_fsxcell_t - c_fsxcell_t) * MS_FSXCELL_T
              + (n_fsxrow_l - c_fsxrow_l) * MS_FSXROW_L
              + (double)(n_fsxcell_l - c_fsxcell_l) * MS_FSXCELL_L
              + (n_fsxrow_h - c_fsxrow_h) * MS_FSXROW_H
              + (double)(n_fsxcell_h - c_fsxcell_h) * MS_FSXCELL_H
              + (n_fsxput - c_fsxput) * MS_FSXPUT
              + (double)(n_fsxcmp_d - c_fsxcmp_d) * MS_FSXBYTE_D
              + (n_fsxsame - c_fsxsame) * MS_FSXSAME
              + (double)(n_fsxcmp_s - c_fsxcmp_s) * MS_FSXBYTE_S;

    printf("  %-38s %8.1f ms   %2d blit %2d fill %2d scroll %3d group\n",
           what, ms, n_blit - c_blit, n_fill - c_fill, n_scroll - c_scroll,
           (n_group - c_group) + (n_group_l - c_group_l)
           + (n_group_h - c_group_h));
    cost_mark();
}

/* ==========================================================================
 * THE SCRIPT
 * ========================================================================*/
/* h_paint_locked - a whole-window W_PAINT with the lock ALREADY held, which is
 * what wm_paint_all does at SPEC.md 53.6 step 4 and what os88_fsx_run's stub
 * calls. */
static void h_paint_locked(void)
{
    clip_armed = 1;
    os88_paint(the_win);
    clip_armed = 0;
}

static void do_paint(void)
{
    os88_gfx_lock();
    clip_armed = 1;                     /* W_PAINT is the ONE callback the
                                         * kernel arms a clip for */
    os88_paint(the_win);
    clip_armed = 0;
    os88_gfx_unlock();
}

/* ...and the PARTIAL expose, which is the ordinary one: a pull-down closing
 * over a corner of the window, or another window dragged across it. */
static void do_paint_rect(int x1, int y1, int x2, int y2)
{
    h_dmg_whole = 0;
    h_dmg_x1 = x1;
    h_dmg_y1 = y1;
    h_dmg_x2 = x2;
    h_dmg_y2 = y2;
    os88_gfx_lock();
    clip_armed = 1;
    os88_paint(the_win);
    clip_armed = 0;
    os88_gfx_unlock();
    h_dmg_whole = 1;
}

/* THE TIMER FIRES BEFORE THE WAKE, because that is the order the kernel
 * uses: W_ONTIMER is dispatched from the tick like any other event and the
 * wake it posts is serviced after it. It arrives UNDER THE LOCK - W_ONCLICK's
 * environment exactly (os88.h) - which is what catches a handler that tries
 * to take it. */
static void fire_timer(void)
{
    if (!h_tmr_armed || (int)(h_tmr_at - the_ticks) > 0)
        return;
    h_tmr_armed = 0;
    h_tmr_fires++;
    os88_gfx_lock();
    os88_ontimer(the_win);
    os88_gfx_unlock();
}

static void do_wake(void)
{
    int c0 = n_clip, b0 = n_blit, f0 = n_fill, r0 = n_run, s0 = n_scroll;

    the_ticks++;
    fire_timer();
    os88_onwake(the_win);
    if (lock_depth != 0) {
        fail("a wake left the gfx lock held");
        exit(1);
    }
    /* THE WAKE IS A BACKGROUND PAINTER AND MUST ARM ITS OWN CLIP REGION. If
     * it drew anything at all without asking, it drew over whoever is on top
     * of us (SPEC.md 11.3). */
    if ((n_blit != b0 || n_fill != f0 || n_run != r0 || n_scroll != s0)
        && n_clip == c0)
        fail("a wake DREW without arming a clip region - the flush is a "
             "background painter and the kernel arms one for W_PAINT only");
}

static void do_about(void)
{
    os88_gfx_lock();
    os88_about(the_win);                /* NO clip armed: the panel arms its
                                         * own, which is the whole point of
                                         * modelling it */
    os88_gfx_unlock();
}

/* do_file - the Standard File dialog's ANSWER, which arrives long after the
 * command that opened it and in the same environment as a click: UI task,
 * gfx lock HELD (os88.h, and kernel/fdlg.inc:45-48 of every proc around
 * fdlg_commit). It is the only way into os88_onfile.
 *
 * **AND IT GATES WHAT THE HANDLER DID UNDER THAT LOCK.** os88_onfile used to
 * call ovl_a2_prog inline: up to six heap claims, a floppy read of up to
 * 46 KB and a 48KB-capable block move, with the pointer frozen, the dock
 * frozen and every other task's painter blocked in os88_gfx_lock. The handler
 * LATCHES now and the wake spends it, so the assertion is that not one claim
 * and not one file operation happened before the lock came off - which is the
 * only form of this that an emulator cannot show and a host CAN. */
static void do_file(int mode, const char *name, unsigned size)
{
    int c0 = h_claim_n, r0 = h_read_n;

    os88_gfx_lock();
    os88_onfile(mode, name, size, 0, the_win);
    if (h_claim_n != c0)
        fail("os88_onfile took a heap claim UNDER THE DESKTOP'S GFX LOCK - "
             "mem_claim may compact an arena this package has a pinned 64KB "
             "in, which is a memcpy in tenths of a second with the whole "
             "desktop stopped behind it (section 12)");
    if (h_read_n != r0)
        fail("os88_onfile went to the FLOPPY under the desktop's gfx lock - "
             "seconds of frozen pointer and dock on a 4.77 MHz XT; the "
             "handler latches and the wake spends it (section 12)");
    os88_gfx_unlock();
    do_wake();                          /* ...and THIS is where the work
                                         * happens, with no lock held */
}

static void do_cmd(int menu, int item)
{
    os88_gfx_lock();                    /* AM_ONCMD is dispatched under the
                                         * desktop's lock, like a click */
    os88_oncmd(item, menu, the_win);
    os88_gfx_unlock();
}

/* h_halt / h_go - stop and start the emulated machine.
 *
 * FROM WAVE 2 A RUNNING MACHINE HAS WORK BY DEFINITION, so every "the handler
 * must stop re-posting" assertion below is about a machine that has NONE.
 * a2_wants_wake answers 1 for A2_ST_RUN and that is the design - the slice
 * driver is what advances the 6502 - so the fixture stops the machine the way
 * the machine itself stops, and puts it back afterwards. Using A2_ST_JAM
 * rather than a pause flag is deliberate: Stop/Continue is wave 4's, and the
 * JAM is the one halt this build can actually reach. */
static void h_halt(void) { a2_state = A2_ST_JAM; }
static void h_go(void) { a2_state = A2_ST_RUN; }

static void do_key(int ascii, int scan)
{
    os88_gfx_lock();                    /* W_ONKEY is dispatched under the
                                         * desktop's lock, like a click */
    os88_onkey(ascii, scan, the_win);
    os88_gfx_unlock();
}

static void do_click(int x, int y)
{
    os88_gfx_lock();
    os88_onclick(x, y, the_win);
    os88_gfx_unlock();
}

/* Write a line of text into the Apple's text page the way the machine will:
 * through a2_wr, so the dirty bitmap and the write window see it. */
static void h_puts(int row, int col, const char *s, int form)
{
    unsigned a = a2_tbase[row] + (unsigned)col;
    int c;

    while (*s) {
        c = *s & 0x7F;
        if (form == 0)
            c |= 0x80;
        else if (form == 1)
            c &= 0x3F;
        else
            c = (c & 0x3F) | 0x40;
        a2_wr(a++, c);
        s++;
    }
}

/* check_phase_row - DOES THE GLASS AT CHARACTER ROW r HOLD THE PIXELS ITS
 * SOURCES MAKE AT THE PHASE THE PROGRAM IS CURRENTLY IN?
 *
 * audit() cannot ask this. It compares the glass against a2_sh, and a2_sh is
 * what the flush BELIEVES it drew - so a row the flush decided not to
 * recompose passes the audit while showing the previous flash phase. The
 * reference here is the composer itself, asked for the whole row at
 * a2_fl_phase, which is the same oracle the "flash flip and a narrow write"
 * case uses one screen up.
 *
 * IT IS WHAT THE SCROLL'S CLEAN NEEDED. The k-row shift test proves forty
 * SOURCE bytes and the composition takes the phase as a second input, so a
 * verified shift is only a proof that the moved pixels are right while the
 * phase has not moved under them (APPLE2-SPEC section 7.7 step 2). */
static void check_phase_row(int r, const char *what)
{
    static unsigned char pbnd[A2_BSTRIDE * 8];
    unsigned pbase;
    int i;

    memset(pbnd, 0, sizeof(pbnd));
    pbase = a2_tbase[r] + (unsigned)(a2_mode_page() - A2_TXT1);
    a2_band_text(pbnd, 0, A2_GROUPS - 1, a2_m.ramseg, pbase,
                 a2_fl_phase ? 0x7F : 0x00);
    for (i = 0; i < 8; i++)
        if (memcmp(pbnd + i * A2_BSTRIDE,
                   a2_sh + ((int)A2_X8(r) + i) * A2_BSTRIDE,
                   A2_BSTRIDE) != 0) {
            fail(what);
            return;
        }
}

/* h_scroll_up - the fixture the ROM's own scroll makes: every source row
 * takes the one below it and the bottom row is filled with spaces. It is a
 * function because the scroll is now driven three times - once plain, once
 * with a flash-phase flip in the same wake, and once on the clipped CGA
 * band. */
static void h_scroll_up(void)
{
    int r, i;

    for (r = 0; r < A2_ROWS - 1; r++)
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[r] + (unsigned)i,
                  (unsigned)a2_rd(a2_tbase[r + 1] + (unsigned)i));
    for (i = 0; i < A2_COLS; i++)
        a2_wr(a2_tbase[A2_ROWS - 1] + (unsigned)i, 0xA0);
}

/* h_mode - set the four display switches the way the emulated machine does,
 * through the SAME a2_video_set the soft switch calls: guarded by value, and
 * marking the frame only when the renderer, the page or the MIXED split
 * actually moves (APPLE2-SPEC section 5.4). Answers how many of the four
 * reported a change, which is what the by-value assertions read. */
static int h_mode(int text, int mixed, int page2, int hires)
{
    int n = 0;

    n += a2_video_set(0, text);
    n += a2_video_set(1, mixed);
    n += a2_video_set(2, page2);
    n += a2_video_set(3, hires);
    return n;
}

/* h_lores - a lo-res screen: every one of the sixteen colours, in blocks, on
 * the TEXT page. One byte is two stacked blocks - low nibble on top - so a
 * byte of $F0 is black over white. */
static void h_lores(void)
{
    int r, c;

    for (r = 0; r < A2_ROWS; r++)
        for (c = 0; c < A2_COLS; c++)
            a2_wr(a2_tbase[r] + (unsigned)c,
                  ((c & 0x0F) << 4) | ((c + r) & 0x0F));
}

/* h_hires - a hi-res screen: a diagonal, a vertical rule and a run of solid
 * bytes, written through a2_wr so the page bitmap and the write window see
 * every one of them. `line` is the Apple SCAN LINE and the address is the
 * interleave's own: a2_hbase[line >> 3] + $400 * (line & 7). */
static void h_hplot(int line, int byte, int v)
{
    a2_wr(a2_hbase[line >> 3] + ((unsigned)(line & 7) << 10) + (unsigned)byte,
          v);
}

static void h_hires(void)
{
    int y;

    for (y = 0; y < A2_SCRH; y++) {
        h_hplot(y, y / 5, 0x7F);            /* the diagonal */
        h_hplot(y, 20, 0x2A);               /* ...and a rule down the middle */
    }
}

static void dump_for_a2ref(const char *stem)
{
    char path[256];
    FILE *f;
    unsigned char st[16];

    sprintf(path, "build/%s-state.bin", stem);
    f = fopen(path, "wb");
    if (!f) { fail("cannot write the a2ref state file"); return; }
    fwrite(h_ram, 1, sizeof(h_ram), f);
    fwrite(h_rom + A2_ROM_CHRGEN, 1, 2048, f);
    memset(st, 0, sizeof(st));
    /* THE LIVE MODE, and not a constant. Wave 1 wrote a 0 here because TEXT
     * was the only renderer there was; a dump that says TEXT of a hi-res
     * screen would hand tools/a2ref.py the wrong reference and pass. */
    st[0] = (unsigned char)a2_mode_of();
    st[1] = (unsigned char)a2_v_mixed;
    st[2] = (unsigned char)a2_v_page2;
    st[3] = (unsigned char)a2_fl_phase;
    fwrite(st, 1, sizeof(st), f);
    fclose(f);

    sprintf(path, "build/%s-frame.bin", stem);
    f = fopen(path, "wb");
    if (!f) { fail("cannot write the a2ref frame file"); return; }
    fwrite(a2_sh, 1, sizeof(a2_sh), f);
    fclose(f);
}

/* spk_train - n toggles of the speaker at `per` emulated cycles apart,
 * driven straight at a2_io_rd($C030) - the same entry the 6502 core reaches
 * through its read hook - with the clock stepped between them the way a run
 * steps it (APPLE2-SPEC section 8).
 *
 * IT IS A HELPER AND NOT A LOOP IN THE TEST because every wake in the
 * speaker's script needs one in front of it: the silence timeout is real, so
 * a script that toggles once and then wakes four times is testing the timeout
 * and not the thing it says it is testing. That is exactly how the first cut
 * of the refusal rows failed. */
static unsigned h_spk_clk = 0x1000;

static void spk_train(int per, int n)
{
    int k;

    for (k = 0; k < n; k++) {
        a2_clk_set(h_spk_clk);
        h_spk_clk += (unsigned)per;
        a2_io_rd(0xC030);
    }
}

/* spk_noise - A TOGGLING SPEAKER THAT IS NOT A TONE, which is the case the
 * stated fact exists for: a click track, Karateka-style waveform synthesis, a
 * Mockingboard. The intervals disagree wildly, so a2_spk_hz answers 0 and no
 * tone is ever asked for. */
static void spk_noise(int n)
{
    static int seed = 12345;
    int k;

    for (k = 0; k < n; k++) {
        seed = seed * 31 + 7;
        a2_clk_set(h_spk_clk);
        h_spk_clk += (unsigned)(300 + ((seed >> 3) & 0x1FFF));
        a2_io_rd(0xC030);
    }
}

int main(void)
{
    FILE *f;
    void *win;
    int i, r, before, n0, whole_blits, rect_blits, straddle_groups;
    int scroll_groups, flip_groups, probes;
    unsigned probe0;
    int abt_runs, abt_cells, wake0, fire0;
    int rect_groups, narrow_groups, wide_groups, sigs;
    int lores_groups, hires_groups;

    memset(glass, GUNK, sizeof(glass));

    /* THE ROM, AT ITS PIN. The package is written against ONE character
     * generator and "it drew something" is not the same claim as "it drew the
     * machine this document describes". */
    f = fopen("build/apple2-rom/APPLE2.ROM", "rb");
    if (!f) {
        printf("a2uitest: build/apple2-rom/APPLE2.ROM is not there. Run:\n"
               "    python3 tools/getapple2rom.py\n");
        return 1;
    }
    if (fread(h_rom, 1, sizeof(h_rom), f) != sizeof(h_rom)) {
        printf("a2uitest: build/apple2-rom/APPLE2.ROM is short\n");
        return 1;
    }
    fclose(f);

    h_in_main = 1;
    win = os88_main();
    h_in_main = 0;
    if (win == 0) { fail("os88_main refused with the ROM present"); return 1; }

    /* --- what os88_main owes, and every one of these is a rule ----------- */
    if (!h_key_armed)
        fail("os88_key_down was never asked from os88_main - the key-state "
             "map is unarmed and the first key of the session is lost "
             "(APPLE2-SPEC section 6.4)");
    if (!h_key_first_in_main)
        fail("the key-state map was armed from somewhere other than "
             "os88_main");
    if (!h_snap)
        fail("os88_wm_snap was not asked for - the content origin must be on "
             "a cell boundary or OSAPI_GFX_SCROLL refuses the rect");
    if (!h_ownbg)
        fail("os88_wm_ownbg was not asked for - the kernel would paint its "
             "own background under a window that draws every pixel");
    if (!h_onwake_set || !h_menu_set || !h_about_set)
        fail("os88_main did not install the wake handler, the menus and the "
             "About item");

    /* THE CHARGEN DECODE AND THE REVERSE TABLE ARE RESIDENT AND ARE BUILT
     * HERE - the negative control for keeping them off the overlay
     * (APPLE2-SPEC section 7.3). If this ever fails, a disk with no
     * APPLE2.OVL is a window that draws nothing rather than a program whose
     * menus refuse. */
    for (i = 0; i < 512; i++)
        if (a2_chr[i] != (h_rom[A2_ROM_CHRGEN + 0xC0 * 8 + i] & 0x7F)) {
            fail("the character generator was not decoded in os88_main");
            break;
        }
    for (i = 0; i < 128; i++) {
        int want = 0, b;

        for (b = 0; b < 7; b++)
            if (i & (1 << b))
                want |= 1 << (6 - b);
        if (a2_rev[i] != want) {
            fail("the 7-bit reverse table was not built in os88_main");
            break;
        }
    }
    /* --- THE FLASH PHASE'S HEARTBEAT (SPEC.md 13.9) ----------------------
     * a2_wants_wake answers 0 for an idle machine ONLY because the phase is
     * a timer. If the install or the arm goes missing the wake goes back to
     * ~1,400 round trips a second of the shared UI task and nothing else
     * changes, so it is asserted rather than assumed. */
    if (!h_ontimer_set)
        fail("os88_wm_ontimer was not called from os88_main - the flash "
             "phase has no heartbeat");
    if (!a2_tmr_ok || !h_tmr_armed)
        fail("the flash timer was not armed from os88_main");

    /* ...AND THE OVERLAY IS **NOT** LOADED YET, which is the other half of
     * the same rule: the .OVL cannot be resolved from os88_main at all -
     * there is no instance yet to resolve a module for (LESSONS.md 13). */
    if (a2_ovl_res)
        fail("the overlay was resolved from os88_main");

    /* --- THE TWO CONSTANTS THE MIRROR CAN ONLY SEE AS LITERALS ----------
     * A2_BSTRIDE and A2_GROUPS are typed out again as `equ`s in a2band.inc,
     * and tests/unit/t_mirror.py is what says the copies agree - but it reads
     * `#define NAME <value>`, not arithmetic, so both are spelled as literals
     * in apple2.c and the DERIVATION is checked here instead. Getting one
     * wrong is a stale screen or a band composed short, never an error. */
    if (A2_X2STRIDE != H_X2STRIDE)
        fail("A2_X2STRIDE is not the stride this harness tells a doubled blit "
             "apart by - every fullscreen row would be priced as a 320x8 one");
    if (A2_BSTRIDE != A2_BANDW / 8)
        fail("A2_BSTRIDE is not A2_BANDW / 8 - the band's stride and the "
             "band's width disagree");
    if (A2_GROUPS != A2_COLS / 8)
        fail("A2_GROUPS is not A2_COLS / 8 - the composer would be asked for "
             "the wrong number of eight-cell groups");
    if (A2_SHIFT_NUM != 4)
        fail("A2_SHIFT_NUM is not 4 - the shift test's threshold is spelled "
             "`nvis << 2` in a2scr.c, which is a multiply by four");
    if (A2_LBOXL / 8 + A2_GROUPS * A2_GBYTES + A2_LBOXR / 8 != A2_BSTRIDE)
        fail("the letterbox and the groups do not add up to the band: "
             "2 + 5*7 + 3 = 40 bytes");

    printf("a2uitest: os88_main OK - window %dx%d, content %dx%d at (%d,%d)\n",
           h_win_w, h_win_h, h_cont_w, h_cont_h, h_cont_x, h_cont_y);
    if (h_cont_w != A2_CONT_W)
        fail("the content box is not A2_CONT_W wide - the frame is content + 2 "
             "and the window was authored the wrong way round");

    /* --- the first paint and the first wake ----------------------------- */
    do_paint();
    do_wake();
    if (!a2_ovl_res)
        fail("the FIRST WAKE did not resolve the overlay");
    audit("the first paint");
    no_gunk("the first paint");
    if (a2_gl0 != 0 || a2_gnl != A2_SCRH)
        fail("a 480-line desktop should show all 192 scan lines");

    printf("\na2uitest: THE COST TABLE - milliseconds on a 4.77 MHz 8088,\n"
           "          priced from PERFORMANCE.md and from tests/a2band's own\n"
           "          MEASURED figures (see the header of this file)\n\n");
    cost_mark();

    /* --- a wake with nothing to do -------------------------------------- */
    /* ...AND IT MUST NOT ASK FOR ANOTHER, which is measured on THIS wake and
     * not on a wake of its own: an extra do_wake() here spends a tick, and
     * the tick it spends moves the flash timer into the next row and prices a
     * phase flip as part of it. The flash arm made a2_wants_wake a constant 1
     * - the Apple's cursor IS a flashing space, so a2_flrow[] is never all
     * zero - and a handler that always re-posts spins the SHARED UI task at
     * ~1,400 round trips a second (693 us each), behind which every other
     * window's menu tracking and drags queue. */
    h_halt();                           /* ...on a machine with NO WORK: see
                                         * h_halt's own comment */
    wake0 = h_wake_posted;
    fire0 = h_tmr_fires;
    do_wake();
    cost_row("an idle wake");
    if (n_blit != c_blit)
        fail("an idle wake drew something");
    if (h_wake_posted != wake0 && h_tmr_fires == fire0)
        fail("an idle wake re-posted itself - the wake handler is spinning "
             "the shared UI task");
    h_go();

    /* --- one cell ------------------------------------------------------- */
    h_puts(3, 10, "X", 0);
    do_wake();
    cost_row("one changed cell");
    audit("one changed cell");

    /* --- one row -------------------------------------------------------- */
    h_puts(5, 0, "THE QUICK BROWN FOX JUMPS OVER A DOG", 0);
    do_wake();
    cost_row("one changed character row");
    audit("one changed row");

    /* --- two pokes at opposite ends of the page -------------------------- */
    h_puts(0, 0, "A", 0);
    h_puts(23, 39, "Z", 0);
    do_wake();
    cost_row("two pokes at opposite ends of the page");
    audit("two pokes");

    /* --- THE FLASH PHASE, ACROSS A FLIP (APPLE2-SPEC section 7.6) --------
     * It changes pixels with NO MEMORY WRITE, so nothing write-driven can see
     * it. The assertion is not "something was drawn" - it is that EXACTLY the
     * scan lines whose character rows hold a byte in $40-$7F were forced. */
    /* THREE FLASHING ROWS, WRITTEN BY THE FIXTURE. The cost of a flip is per
     * flashing ROW, so the number below has to be quoted with a row count -
     * and the rows have to be the fixture's own, because a2_selftext() is
     * wave-1 scaffolding that wave 2 deletes. Row 9 is the control: it holds
     * no byte in $40-$7F and must not be marked. */
    h_puts(8, 2, "FLASHING", 2);
    h_puts(9, 2, "STEADY", 0);
    h_puts(10, 2, "ALSO FLASHING", 2);
    do_wake();
    audit("the flashing row, phase 0");
    dump_for_a2ref("a2ref");            /* phase 0, for tools/a2ref.py */

    n0 = a2_fl_phase;
    the_ticks += A2_FLASH_TICKS;
    cost_mark();
    do_wake();
    cost_row("ONE FLASH PHASE FLIP");
    if (a2_fl_phase == n0)
        fail("the flash phase did not flip after A2_FLASH_TICKS");
    audit("the flashing row, phase 1");
    if (!a2_flrow[8])
        fail("the row holding $40-$7F bytes was not marked as flashing");
    if (a2_flrow[9])
        fail("a row with no $40-$7F byte was marked as flashing");
    dump_for_a2ref("a2ref2");           /* phase 1 - the SAME memory, other
                                         * pixels, which is what the flash
                                         * phase means */

    /* --- A FLIP AND A NARROW WRITE IN THE SAME FLUSH ----------------------
     * THE WRITE WINDOW SAYS NOTHING ABOUT WHICH CELLS FLASHED. a2_flash_force
     * marked the flashing rows dirty and left [a2_wlo,a2_whi] alone, so a
     * cursor flashing at one end of a row while the machine printed at the
     * other gave a group span that did not contain the cursor: the compare
     * never looked at it and the line was then marked CLEAN. On the glass
     * that is a cursor that stops blinking exactly while a program prints -
     * the whole of an Applesoft session - and the audit cannot see it,
     * because the shadow is updated from the same narrow band. The reference
     * is the composer itself, asked for the whole row. */
    h_puts(8, 30, "WROTE", 0);          /* the window narrows to the far end */
    /* ...AND A FLASHING FIXTURE ON THE BOTTOM ROW, for the scroll below.
     * a2_flrow[] is SHIFTED, and the vacated rows' flags have to be zeroed
     * AFTER that shift: zeroing them first hands rows A2_ROWS-2k..A2_ROWS-k-1
     * a zero whatever they were flashing - row 22 at k=1 - and the same pass
     * marks them clean, so they are never recomposed and the flag is never
     * rewritten. The fixtures at rows 8 and 10 cannot see it, because at k=1
     * their sources are rows 9 and 11 and the zeroed window is row 23 alone.
     * It has to be planted on the BOTTOM row. */
    h_puts(A2_ROWS - 1, 2, "BOTTOM FLASH", 2);
    the_ticks += A2_FLASH_TICKS;
    do_wake();
    audit("a flash flip and a narrow write in the same flush");
    {
        static unsigned char fbnd[A2_BSTRIDE * 8];
        unsigned fbase;

        memset(fbnd, 0, sizeof(fbnd));
        fbase = a2_tbase[8] + (unsigned)(a2_mode_page() - A2_TXT1);
        a2_band_text(fbnd, 0, A2_GROUPS - 1, a2_m.ramseg, fbase,
                     a2_fl_phase ? 0x7F : 0x00);
        for (i = 0; i < 8; i++)
            if (memcmp(fbnd + i * A2_BSTRIDE,
                       a2_sh + ((int)A2_X8(8) + i) * A2_BSTRIDE,
                       A2_BSTRIDE) != 0) {
                fail("a FLASH FLIP was composed over the write window's "
                     "groups only - the cells that flashed outside it kept "
                     "the last phase's pixels");
                break;
            }
    }

    /* --- a scroll: the whole page moves up one row ------------------------ */
    h_scroll_up();
    cost_mark();
    before = n_scroll;
    do_wake();
    scroll_groups = n_group - c_group;
    cost_row("a one-row scroll");
    if (n_scroll == before)
        fail("a whole-page shift was not turned into a gfx_scroll");
    else if (n_scroll_dy != 8)
        fail("the scroll moved the wrong way or the wrong distance - "
             "POSITIVE dy moves the content UP (SPEC.md 5.5)");
    audit("after the scroll");
    /* THE SCROLL HAS TO SAVE THE COMPOSE AND NOT ONLY THE SCROLL. A ROM
     * scroll writes all 23 source rows, so every row arrives dirty; the shift
     * test proved forty bytes a row that the glass under them is already
     * right, and the flush must therefore compose only the k VACATED rows.
     * Without the clear this read 120 groups and 406.2 ms - the whole
     * optimisation reduced to one gfx_scroll call - and nothing here saw it,
     * because the audit passes either way: recomposing a row that was already
     * correct draws no pixel. */
    if (scroll_groups > A2_GROUPS)
        printf("a2uitest: FAIL - a one-row scroll composed %d groups where "
               "the ONE vacated row owes %d: the shifted rows are still "
               "marked dirty, so the scroll saves the scroll and nothing "
               "else\n", scroll_groups, A2_GROUPS),
        fails++;
    /* ...AND a2_flrow[] MOVES WITH THE ROWS. It is what a phase flip forces
     * off, and it was right before the clear only because every row was being
     * recomposed and a2_rowflash rewrote it. Rows 8 and 10 held the flashing
     * fixtures; after one row of scroll they are rows 7 and 9. */
    if (!a2_flrow[7] || !a2_flrow[9])
        fail("a2_flrow[] was not shifted with the scroll - flashing text that "
             "has scrolled would simply stop flashing");
    if (a2_flrow[8] || a2_flrow[10])
        fail("a2_flrow[] was not shifted with the scroll - a row that no "
             "longer flashes would be force-composed on every phase flip");
    /* ...AND THE BOTTOM ROW'S FLAG SURVIVED THE SHIFT. The rows-8-and-10
     * assertions above pass whether the vacated rows are zeroed before the
     * shift or after it; row 23's is the only one that does not, because at
     * k=1 the zeroed window is row 23 alone and row 22's source IS row 23.
     * A flag lost here is flashing text that scrolled up off the bottom and
     * stopped flashing for the rest of the session - the very defect the
     * shift was written to prevent, surviving at its own boundary. */
    if (!a2_flrow[A2_ROWS - 2])
        fail("a2_flrow[22] was zeroed by the scroll's own vacated-row pass - "
             "the vacated rows must be cleared AFTER the shift reads them, "
             "not before");
    if (a2_flrow[A2_ROWS - 1])
        fail("a2_flrow[23] survived the scroll - the vacated row holds "
             "spaces and nothing in it flashes");

    /* --- A SCROLL AND A FLASH-PHASE FLIP IN THE SAME WAKE -----------------
     * THE SOURCE SHADOW CANNOT SEE THE PHASE, and this is the case that says
     * so. a2_band_text takes a2_fl_phase as a second input; a2_shsrc[] holds
     * forty source bytes. os88_ontimer flips the phase and a2_flash_force
     * marks the flashing rows dirty for a reason no source compare can
     * detect, and the very next flush consumes that mark - which during
     * scrolling output is a SCROLL flush. Clearing a2_lnd for every shifted
     * row on the strength of the source proof therefore ate the flip: the
     * glass kept the old phase, the next flip composed back to the phase
     * already on it and drew nothing, and only the flip after that redrew.
     * One phase for ~825 ms instead of 275, on the cursor, during exactly the
     * printing the scroll path exists for.
     *
     * THE CASE ABOVE CANNOT SEE IT because it does not flip the phase across
     * the scroll, and audit() cannot see it either - the shadow agrees with
     * the glass whatever phase both are in. check_phase_row asks the composer
     * instead. */
    h_scroll_up();
    cost_mark();
    before = n_scroll;
    n0 = a2_fl_phase;
    the_ticks += A2_FLASH_TICKS;        /* the SAME wake carries both */
    do_wake();
    flip_groups = n_group - c_group;
    cost_row("a one-row scroll WITH a flash-phase flip");
    if (a2_fl_phase == n0)
        fail("the flash phase did not flip in the scroll's own wake - the "
             "case is not testing what it says it tests");
    if (n_scroll == before)
        fail("a flash-phase flip stopped the shift test from finding the "
             "scroll");
    audit("after a scroll that carried a phase flip");
    check_phase_row(6, "a SCROLLED FLASHING ROW kept the previous phase's "
                       "pixels - the source shadow is only a proof while the "
                       "phase it was composed at is unchanged");
    check_phase_row(8, "a SCROLLED FLASHING ROW kept the previous phase's "
                       "pixels (the second fixture)");
    check_phase_row(A2_ROWS - 3, "the BOTTOM flashing fixture, scrolled "
                                 "twice, kept the previous phase's pixels");
    /* AND IT IS PAID IN ROWS, NOT IN PAGES. Three fixtures flash and the
     * scroll vacates one row, so four rows are owed a compose: 20 groups.
     * Under A2_GROUPS would mean the flip was eaten (check_phase_row above
     * says the same thing about the pixels); anywhere near 120 would mean the
     * clean had been given up altogether, which is the 406 ms flush this
     * whole path replaced. */
    if (flip_groups <= A2_GROUPS)
        printf("a2uitest: FAIL - a scroll carrying a phase flip composed %d "
               "groups: the flashing rows were marked clean and the flip was "
               "thrown away\n", flip_groups),
        fails++;
    if (flip_groups > 6 * A2_GROUPS)
        printf("a2uitest: FAIL - a scroll carrying a phase flip composed %d "
               "groups where four rows (%d) are owed - the scroll's clean is "
               "not being taken\n", flip_groups, 4 * A2_GROUPS),
        fails++;
    cost_mark();                        /* check_phase_row composed rows of
                                         * its own: they are the harness's,
                                         * not the flush's */

    /* --- THE SHIFT TEST'S MISS PATH IS BOUNDED (APPLE2-SPEC 7.7 step 2) ---
     * The k loop is up to sum(k=1..23) of (24-k) = 276 forty-byte compares,
     * 0.31 ms each: 87 ms to be told nothing scrolled. The shape that reaches
     * it is ordinary - a long run of IDENTICAL rows above the content with
     * every row dirty - and `HOME : VTAB 20 : PRINT` in a loop is exactly it,
     * because HOME writes all 24 rows and meets the 4/5 threshold on every
     * iteration while each k walks the blank run to its end before the
     * content row breaks it. A signature prefilter cannot help: the EQUAL
     * probes are the cost, a differing pair is the loop's cheap terminator,
     * and a run of blank rows has equal signatures. So the budget is a count.
     *
     * THE FIXTURE IS THAT SCREEN, TWICE. The first wake settles the shadow;
     * the second re-writes the same 24 rows with one changed content row, so
     * every row is dirty, the threshold is met, and the test is asked. */
    for (r = 0; r < A2_ROWS; r++)
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[r] + (unsigned)i, 0xA0);
    h_puts(A2_ROWS - 5, 2, "HOME VTAB 20", 0);
    h_puts(A2_ROWS - 1, 2, "]", 0);     /* the prompt on the bottom row, so
                                         * that no LARGE k can match either:
                                         * without it rows 0..3 (blank) equal
                                         * shadow rows 20..23 (blank) and the
                                         * test would answer k=20 - exact, and
                                         * a 20-row scroll with 20 rows
                                         * redrawn behind it */
    do_wake();
    for (r = 0; r < A2_ROWS; r++)
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[r] + (unsigned)i, 0xA0);
    h_puts(A2_ROWS - 5, 2, "HOME VTAB 21", 0);
    h_puts(A2_ROWS - 1, 2, "]", 0);
    probe0 = a2_n_probe;
    before = n_scroll;
    cost_mark();
    do_wake();
    probes = (int)(a2_n_probe - probe0);
    cost_row("a HOME-shaped screen the shift test must REFUSE");
    if (n_scroll != before)
        fail("a gfx_scroll was emitted for a screen whose content did not "
             "move up by any number of rows");
    if (probes > A2_SHIFT_PROBES)
        printf("a2uitest: FAIL - the shift test spent %d probes where the "
               "budget is %d: the miss path is unbounded again\n",
               probes, A2_SHIFT_PROBES),
        fails++;
    if (probes == 0)
        fail("the shift test was never asked on a screen with every row "
             "dirty - the miss-path budget has nothing to bound");
    printf("a2uitest: the shift test refused a HOME-shaped screen in %d "
           "probes (budget %d, worst case without one 276)\n",
           probes, A2_SHIFT_PROBES);
    audit("after a screen the shift test refused");

    /* --- a full repaint --------------------------------------------------- */
    cost_mark();
    do_paint();
    /* THE DELTA IS TAKEN BEFORE cost_row, because cost_row ENDS IN
     * cost_mark(): reading `n_blit - c_blit` after it prints reads zero, and
     * an assertion against zero passes whatever the code does. That is how
     * this row shipped both ways round at once - a real failure on the
     * repaint's own count, and two vacuous passes below it. */
    whole_blits = n_blit - c_blit;
    cost_row("a full repaint (the whole 320x192 band)");
    audit("after a full repaint");

    /* --- A PARTIAL EXPOSE COSTS THE RECT AND NOT THE FRAME ----------------
     * WF_OWNBG is set, so the kernel hands back a damage rect and paints
     * nothing. os88_paint used to call a2_sh_inval() unconditionally and
     * never ask - so closing a pull-down over this window forced all 192
     * scan lines, 505.4 ms of composing and blitting under the desktop's
     * gfx lock. The assertion is a COUNT: a rect two character rows tall
     * must not cost what the whole band costs. */
    cost_mark();
    do_paint_rect(h_cont_x + A2_BORDER, a2_gsy + 40,
                  h_cont_x + A2_BORDER + A2_BANDW - 1, a2_gsy + 55);
    rect_blits = n_blit - c_blit;
    rect_groups = n_group - c_group;
    cost_row("a partial expose, 16 scan lines of the band");
    audit("after a partial expose");
    if (rect_blits >= whole_blits)
        fail("a 16-line expose cost as many blits as the whole band - "
             "os88_paint is not asking os88_wm_damage()");

    /* --- ...AND A NARROW ONE COSTS THE COLUMNS TOO ------------------------
     * A forced row is composed WHOLE because the glass over it is unknown -
     * but "unknown" is only true of the pixels the damage rect covered. A
     * pull-down closing over this window is a rect about 190 px wide
     * (MENU_MAXCH is 24 glyphs), and a2_blank_rect used to throw the column
     * span away and force whole scan lines, so the flush's `rowf` arm widened
     * every one of those rows to all forty cells. apps/c64/c64scr.c takes a
     * column span for exactly this reason and measured the pair at 122 ms
     * against 75.
     *
     * A GROUP IS 56 PIXELS, so 190 px is four groups of the five and the
     * saving is one group a row - the honest number for a menu, and the
     * assertion is against the SAME rect one group wider rather than against
     * a figure. The audit is what says the narrowing did not cost a pixel. */
    cost_mark();
    do_paint_rect(a2_gsx, a2_gsy + 40, a2_gsx + 189, a2_gsy + 55);
    narrow_groups = n_group - c_group;
    cost_row("a partial expose, 16 lines x 190 px (a menu)");
    audit("after a narrow partial expose");
    if (narrow_groups >= rect_groups)
        fail("a 190-px-wide expose composed as many groups as a full-width "
             "one - a2_blank_rect is dropping the damage rect's COLUMNS and "
             "the flush's forced arm is widening to all forty cells");

    /* --- A FORCED ROW AND A NARROW WRITE WINDOW IN THE SAME FLUSH ---------
     * `a2_bnd` is ONE 8x40 buffer reused for every character row. A row the
     * glass is UNKNOWN over is drawn whole - all forty bytes, letterbox
     * included - so it must be COMPOSED whole; composing only the groups the
     * write window narrowed it to leaves up to 32 band bytes holding the
     * PREVIOUS row's pixels, and those get compared and blitted.
     *
     * THE AUDIT CANNOT SEE THIS ONE and that is why it is checked here
     * instead: the shadow is updated from the same wrong band, so the glass
     * and the shadow agree perfectly on the wrong picture. The reference is
     * the composer itself, asked for the whole row. */
    h_puts(20, 0, "0123456789012345678901234567890123456789", 0);
    do_wake();
    audit("the row above the forced one");
    h_puts(21, 8, "ABCDEFGH", 0);       /* the window narrows to ONE group */
    for (i = 0; i < 8; i++)
        a2_line_force((int)A2_X8(21) + i);          /* ...and it is FORCED */
    do_wake();
    audit("a forced row whose write window covered one group");
    {
        static unsigned char refbnd[A2_BSTRIDE * 8];
        unsigned rbase;

        memset(refbnd, 0, sizeof(refbnd));
        rbase = a2_tbase[21] + (unsigned)(a2_mode_page() - A2_TXT1);
        a2_band_text(refbnd, 0, A2_GROUPS - 1, a2_m.ramseg, rbase,
                     a2_fl_phase ? 0x7F : 0x00);
        for (i = 0; i < 8; i++)
            if (memcmp(refbnd + i * A2_BSTRIDE,
                       a2_sh + ((int)A2_X8(21) + i) * A2_BSTRIDE,
                       A2_BSTRIDE) != 0) {
                fail("a FORCED row was composed over its write window's "
                     "groups only - the band's other bytes are the PREVIOUS "
                     "row's pixels, and they were blitted");
                break;
            }
    }

    /* --- A WRITE THAT STRADDLES TWO CHARACTER ROWS ------------------------
     * The window is taken over a whole SLICE, so a COUT that prints a
     * character and then moves the cursor to the next line leaves a window
     * spanning two rows. A CONTAINMENT test fails for both and composes all
     * forty cells of each; an intersection clamps and composes the groups
     * each row actually owns. */
    cost_mark();
    h_puts(0, 32, "Q", 0);
    h_puts(1, 0, "]", 0);
    do_wake();
    straddle_groups = n_group - c_group;
    cost_row("a write straddling two character rows");
    audit("a write straddling two character rows");
    /* THE DERIVATION, ROW BY ROW. The two writes are $420 (row 0, cell 32)
     * and $480 (row 1, cell 0), so the window is $420..$480 and every row of
     * page $04 whose 40 bytes MEET it is dirty. The interleave puts four such
     * rows in that page:
     *
     *   row 0  $400..$427  the window starts at $420      -> groups 4..4   1
     *   row 8  $428..$44F  the window covers it entirely   -> groups 0..4   5
     *   row 16 $450..$477  the window covers it entirely   -> groups 0..4   5
     *   row 1  $480..$4A7  the window ends at $480         -> groups 0..0   1
     *                                                                     --
     *                                                                     12
     *
     * Rows 9 and 17 are in the same page and are NOT dirty: a2_dirty_scan
     * applies the same intersection before it marks a row, so a page bit set
     * by a write somewhere else in those 256 bytes marks nothing.
     *
     * A CONTAINMENT test (`wlo >= base && whi <= last`) is false for all four
     * - including, and this is the whole point, the two rows the writes
     * actually landed in - and composes 5 groups each, 20. The bound is the
     * intersection's own figure, one-sided: fewer groups than this would be
     * an improvement and the audit() above is what catches a row that was not
     * composed at all. */
    if (straddle_groups > 12)
        fail("a two-row write composed more groups than the rows it wrote to "
             "own - the compose span is testing containment rather than "
             "intersection");

    /* --- the About panel: opened, held, closed as DAMAGE ------------------ */
    do_about();
    if (!a2_abt_up)
        fail("the About panel did not come up");
    if (a2_hold_l1 < a2_hold_l0)
        fail("the About panel held no scan lines - the flush will draw under "
             "something opaque");
    do_wake();
    audit("with the About panel up");

    /* AN EXPOSE THAT DOES NOT REACH THE PANEL DOES NOT REPAINT IT. The panel
     * is 1 fill + 2 frames + 8 font_run over 196 glyph cells - ~185 ms on the
     * target - and redrawing it on the latch alone made a partial expose with
     * the panel up MORE expensive than one without it, which inverts the
     * whole point of the hold rows. The rect below is the band's bottom two
     * character rows, which the panel (centred, and clamped above the status
     * strip) does not cover. */
    cost_mark();
    do_paint_rect(h_cont_x + A2_BORDER, a2_gsy + a2_gnl - 16,
                  h_cont_x + A2_BORDER + A2_BANDW - 1, a2_gsy + a2_gnl - 1);
    abt_runs = n_run - c_run;
    abt_cells = n_cells - c_cells;
    cost_row("an expose the About panel does not cover");
    if (a2_gsy + a2_gnl - 16 <= a2_abt_y + a2_abt_h - 1)
        fail("the fixture rect overlaps the panel - this row would prove "
             "nothing");
    else if (abt_runs > 1 || abt_cells > A2_STCELLS)
        fail("an expose clear of the About panel repainted the panel - "
             "os88_paint is not testing the damage rect against it");
    audit("after an expose clear of the About panel");

    cost_mark();
    do_click(100, 100);
    if (a2_abt_up)
        fail("a click did not close the About panel");
    do_wake();
    cost_row("closing the About panel (damage, not a repaint)");
    audit("after the About panel closed");

    /* --- FULLSCREEN WITH THE ABOUT PANEL UP ------------------------------
     * os88_about is the kernel's NAME pull-down and a menu-bar click is not a
     * W_ONCLICK, so Machine > Toggle Fullscreen is reachable with the panel
     * ON THE GLASS. OSAPI_FULLSCREEN repaints the window whole and
     * SYNCHRONOUSLY, so os88_paint runs nested with a rect measured for the
     * OLD box: the rows the old rect covered were HELD, so the flush skipped
     * them and nothing redrew them (a full-width strip of stale panel pixels
     * under WF_OWNBG), and the rows the NEW rect covers were composed and
     * blitted a moment before the panel was painted over them - ~226 ms of
     * pure double-draw, invisible in any screendump. The audit() is what sees
     * the first half; the two assertions are the second. */
    do_about();
    do_wake();
    if (!a2_abt_up)
        fail("the About panel did not come up for the fullscreen fixture");
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    if (a2_abt_up)
        fail("Toggle Fullscreen left the About panel up across a geometry "
             "change - its rect is measured for a box that is gone");
    if (a2_hold_l0 <= a2_hold_l1)
        fail("Toggle Fullscreen left scan lines HELD against a panel that is "
             "not on the glass - the flush will skip them for ever");
    do_wake();
    audit("fullscreen, entered with the About panel up");
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    do_wake();
    audit("...and back out of fullscreen");

    /* --- THE CONTENT BOX CHANGED SIZE WHILE THE PANEL WAS UP -------------
     * The panel's rect and the hold range are both measured by
     * ovl_about_geom, and until this wave they were only ever measured AFTER
     * the flush that reads them - so a W_PAINT that follows a geometry change
     * held the OLD lines (the flush skips them and nothing draws them: a
     * full-width strip of stale pixels under WF_OWNBG) and composed and
     * blitted the NEW ones a moment before the panel was painted over them.
     * os88_paint measures FIRST now. The audit is what sees it: it skips the
     * lines the panel holds, so the strip it catches is the one the flush
     * skipped and the panel is NOT on. */
    do_about();
    do_wake();
    {
        int sav_h2 = h_cont_h;

        h_cont_h -= 48;                 /* a shorter content box */
        do_paint();
        do_wake();
        audit("the content box shrank with the About panel up");
        h_cont_h = sav_h2;
        do_paint();
        do_wake();
        audit("...and grew back with it still up");
    }
    do_click(100, 100);                 /* the panel comes down */
    do_wake();
    audit("after the panel came down again");

    /* --- THE PANEL'S BODIES ARE `ovl_` AND AN `ovl_` CAN REFUSE -----------
     * ovl_about_geom and ovl_about_draw both live in APPLE2.OVL, and an
     * overlay function ANSWERS A STATUS with 0 meaning it did not happen
     * (SPEC.md 73.14). Both used to return void, so os88_paint had no way to
     * find out - and a W_PAINT arrives under the desktop's gfx lock, where
     * resolving the module is a 400 ms floppy seek with the whole machine
     * stopped, so it may not even try (a2_ovl_ready, section 15.5).
     *
     * With the module gone the card cannot be redrawn, and the failure is not
     * a missing panel: the HOLD RANGE stays set, so the flush goes on
     * skipping the rows the panel used to cover - for ever - and under
     * WF_OWNBG nothing else paints them either. The panel therefore comes
     * DOWN, and its rect is damage. */
    do_about();
    do_wake();
    if (!a2_abt_up)
        fail("the About panel did not come up for the overlay-gone fixture");
    a2_ovl_res = 0;                     /* the module is not resident */
    do_paint();
    if (a2_abt_up)
        fail("a W_PAINT with APPLE2.OVL not resident left the About panel "
             "latched - ovl_about_geom cannot run and its answer is what "
             "says so");
    if (a2_hold_l0 <= a2_hold_l1)
        fail("a W_PAINT with APPLE2.OVL not resident left scan lines HELD "
             "against a panel nothing can redraw - the flush skips them for "
             "ever");
    a2_ovl_res = 1;
    do_wake();
    audit("after the About panel's overlay went away under a paint");
    no_gunk("after the About panel's overlay went away");

    /* --- A FULLSCREEN WINDOW HAS A KEY ROUTE BACK (SPEC.md 11.2.1) -------
     * kernel/wm.inc draws NO chrome for a WF_FULL window, so the menu item
     * that got the user in is not on the glass any more. Wave 1 shipped
     * Toggle Fullscreen LIVE with the chords scheduled for wave 3 and
     * a2_key() dropping every key, which is a ONE-WAY DOOR - found by driving
     * QEMU and pressing f, F, Esc, Ctrl+F and Alt+Enter at a fullscreen
     * window that ignored all five. Both of section 6.3's chords are asserted
     * here, and Alt+Enter on BOTH scan codes, because the classic set an XT
     * BIOS delivers and the enhanced one disagree and neither is confirmed on
     * iron until wave 7. */
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    if (!h_fs)
        fail("Toggle Fullscreen did not enter fullscreen");
    do_key(6, 0x21);                    /* Ctrl+F, ASCII 6 on every BIOS */
    if (h_fs)
        fail("Ctrl+F did not leave fullscreen - SPEC.md 11.2.1's "
             "unconditional door, and there is no menu bar to use instead");
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    do_key(0, 0x1C);                    /* Alt+Enter, the classic scan */
    if (h_fs)
        fail("Alt+Enter (classic scan 0x1C) did not leave fullscreen");
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    do_key(0, 0xA6);                    /* ...and the enhanced one */
    if (h_fs)
        fail("Alt+Enter (enhanced scan 0xA6) did not leave fullscreen");
    do_wake();
    audit("after the fullscreen chords");

    /* --- ...AND Esc IS NOT ONE OF THEM (APPLE2-SPEC section 6.3) ----------
     * SPEC.md 11.2.1 binds the bare letter `f` and Esc to enter and leave a
     * fullscreen surface. This machine OWNS BOTH: Esc is a key on an Apple
     * II+ keyboard - the Autostart Monitor's ESC-I/J/K/M move the cursor and
     * the Applesoft screen editor reads the same four - and `f` is a letter.
     * So the exception is taken the way C64-SPEC 9.8 takes it, and the two
     * chords above are the WHOLE door. Binding Esc here would be a machine
     * whose screen editor does not work, which no screendump of a `]` prompt
     * would show. */
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    if (!h_fs)
        fail("Toggle Fullscreen did not enter fullscreen for the Esc row");
    no_gunk("fullscreen");              /* the status strip reaches the frame
                                         * on a 638-px content box too */
    do_key(27, 0x01);                   /* Esc, and it goes to the MACHINE */
    if (!h_fs)
        fail("Esc left fullscreen - the Apple II+ owns Esc (ESC-I/J/K/M in "
             "the Monitor and the Applesoft screen editor) and section 6.3 "
             "takes SPEC.md 11.2.1's stated exception for it");
    do_key(6, 0x21);                    /* ...and Ctrl+F is the way home */
    if (h_fs)
        fail("Ctrl+F did not leave fullscreen after the Esc row");
    do_wake();
    audit("after Esc was passed to the machine in fullscreen");

    /* ...AND THE REFUSED ARM OWES THE PANEL'S ROWS A DRAW, because the kernel
     * did nothing at all: nothing repaints us, so the hole the panel left has
     * to be somebody's. */
    do_about();
    do_wake();
    h_fs_refuse = 1;
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    h_fs_refuse = 0;
    if (a2_abt_up)
        fail("a REFUSED fullscreen left the About panel up");
    do_wake();
    audit("after a refused fullscreen with the panel up");

    /* --- A MINIMIZED WINDOW DRAWS NOTHING AND STOPS ASKING ---------------
     * os88_wm_geom answers -1 for "not visible", on which a2_flush returns
     * having cleared nothing - so every dirty flag stays set and the wake
     * re-posts for ever at the one moment the user has said they do not want
     * to look at us. W_ONTIMER fires while minimized too (os88.h), which is
     * what keeps the clock arriving to be re-posted on. */
    h_minimized = 1;
    h_halt();
    a2_sh_inval();
    cost_mark();
    do_wake();
    if (n_blit != c_blit || n_fill != c_fill || n_run != c_run)
        fail("a minimized window drew something");
    {
        int w0 = h_wake_posted, f0 = h_tmr_fires;

        do_wake();
        if (h_wake_posted != w0 && h_tmr_fires == f0)
            fail("a minimized window went on re-posting wakes it can do "
                 "nothing with");
    }
    h_minimized = 0;
    h_go();
    do_paint();
    do_wake();
    audit("after the window was restored from minimized");

    /* --- THE SAME MESSAGE TWICE -----------------------------------------
     * a2_status's "nothing changed" early return used to leave a2_st_dirty
     * SET, and a2_wants_wake reads that flag - so a2_say() handed a string
     * identical to the one already on the glass re-posted the wake for the
     * whole five-second life of the message: ~7,000 round trips of the SHARED
     * UI task with nothing to do inside them. It is reachable from two picks
     * of Machine > Toggle Fullscreen while another window holds it, and from
     * two menu picks on a disk with no APPLE2.OVL. */
    /* IT IS ASKED OF a2_st_dirty DIRECTLY, and of the wake COUNT only on a
     * machine that is not running. The wake count alone cannot see this: a
     * RUNNING machine wants a wake unconditionally (`a2_state == A2_ST_RUN`
     * is its own arm in a2_wants_wake, because the 6502 has cycles owed to
     * it), so the count moves on every wake whatever the row does and the
     * assertion below then passed or failed on whether the flash timer
     * happened to fire in the same wake - a coin the tick count of every case
     * ABOVE this one flips. The flag is the subject; the two h_halt/h_go
     * brackets on either side of this block are what make the count
     * deterministic, which is what the minimized and covered cases already
     * had. */
    a2_say("Another window has it.");
    do_wake();
    a2_say("Another window has it.");   /* the IDENTICAL string */
    do_wake();
    if (a2_st_dirty)
        fail("an identical status message left a2_st_dirty SET - the wake "
             "re-posts for the whole life of the message");
    h_halt();
    {
        int w0 = h_wake_posted, f0 = h_tmr_fires;

        do_wake();
        if (h_wake_posted != w0 && h_tmr_fires == f0)
            fail("an identical status message left the row dirty - the wake "
                 "re-posts for the whole life of the message");
    }
    h_go();
    audit("after the same status message twice");

    /* --- A WINDOW NOTHING SHOWS OF DRAWS NOTHING, AND STOPS ASKING -------
     * clip_set answers -1 when not one pixel of us is on the glass. The flush
     * is then SKIPPED - ~500 ms not spent on a window nobody can see - and
     * the wake must not go on re-posting to be told the same thing 1,400
     * times a second: the work is still OWED, and W_PAINT is what comes and
     * asks for it. */
    h_clip_refuse = 1;
    h_halt();
    a2_sh_inval();
    cost_mark();
    do_wake();
    if (n_blit != c_blit || n_fill != c_fill || n_run != c_run)
        fail("the flush drew with no clip region - clip_set refused and the "
             "package drew over whatever is covering it");
    {
        int w0 = h_wake_posted, f0 = h_tmr_fires;

        do_wake();
        if (h_wake_posted != w0 && h_tmr_fires == f0)
            fail("a fully covered window went on re-posting wakes it can do "
                 "nothing with");
    }
    h_clip_refuse = 0;
    h_go();
    do_paint();                         /* ...and the exposure is what pays */
    audit("after the window came back out from under");

    /* --- THE LAUNCH SCREEN ON A 640x200 DESKTOP: THE CURSOR ANCHOR -------
     * WAVE 4'S DEFECT, AND NOTHING ELSE IN THIS FILE COULD SEE IT. The band
     * used to anchor to the BOTTOM of the Apple frame unconditionally, on the
     * reasoning that "the ] cursor line is there" - which is false of a
     * machine that has not scrolled yet. The Autostart ROM's cold start puts
     * `APPLE ][` on row 0 and leaves the cursor around row 2, so on a CGA
     * desktop, where a2_gl0 was 81 and the glass began at character row 10,
     * every pixel of a freshly booted screen was composed, blitted and landed
     * off the visible band: the window was BLACK, `PRINT 6*7` answered where
     * nobody could see it, and the status row went on reading TEXT and
     * 2,000% beside it. It took thirty Returns to walk the prompt down to row
     * 23 before one character appeared.
     *
     * Every existing row here passed throughout, because they all write into
     * the rows the anchor happened to be showing. So this one asks the
     * question the glass asks: after the first flush of a COLD START, is
     * there a lit pixel where the banner is?
     */
    {
        int sav_h = h_cont_h, lit0, lit2, x, y, before3;

        h_cont_h = 137;                 /* the CGA content box */
        for (r = 0; r < A2_ROWS; r++)
            for (i = 0; i < A2_COLS; i++)
                a2_wr(a2_tbase[r] + (unsigned)i, 0xA0);
        /* THE BAND STARTS AT THE BOTTOM, which is where a session that has
         * been scrolling leaves it - and where the very first flush of a
         * launch leaves it too, because a2_power_on's FF FF 00 00 pattern is
         * in $25 until the ROM's own COUT writes a cursor row there. So the
         * cold start below is a move UP and not a lucky initial value. */
        a2_wr(A2_CV, A2_ROWS - 1);
        a2_sh_inval();
        do_paint();
        if (a2_gl0 != A2_SCRH - a2_gnl)
            fail("the cold-start fixture did not start from the bottom "
                 "anchor - the move it is about is not being made");
        /* ...AND NOW THE COLD START: the Autostart ROM's banner on row 0, the
         * prompt on row 2 and CV with it. */
        h_puts(0, 16, "APPLE ][", 0);
        h_puts(2, 0, "]", 0);
        a2_wr(A2_CV, 2);
        before3 = n_blit;
        do_wake();
        if (a2_gnl >= A2_SCRH)
            fail("the cold-start fixture did not clip the band at all - the "
                 "case it exists to test is not being tested");
        if (a2_gl0 != 0)
            printf("a2uitest: FAIL - a cold start with the cursor on row 2 "
                   "left the band anchored at scan line %d: the banner and "
                   "the ] prompt are above it and the window is BLACK\n",
                   a2_gl0),
            fails++;
        /* THE CURSOR ROW GOES TO THE BOTTOM OF THE BAND AND NOT THE TOP, so
         * the HISTORY above it comes with it. Anchoring the cursor row at the
         * top is the other spelling of "follow the cursor" and it hides the
         * banner one row above the prompt. */
        if (n_blit == before3)
            fail("a cold start on a clipped band blitted NOTHING");
        /* ...AND THE PIXELS, WHICH IS THE ONLY QUESTION THAT MATTERS. Row 0
         * carries the banner and row 2 the prompt; both are inside the band
         * now, so both owe lit pixels on the glass. */
        lit0 = 0;
        lit2 = 0;
        for (y = 0; y < 8; y++)
            for (x = 0; x < a2_gbw; x++) {
                if (glass[a2_gsy + y][a2_gsx + x] == 1)
                    lit0++;
                if (glass[a2_gsy + 16 + y][a2_gsx + x] == 1)
                    lit2++;
            }
        if (lit0 == 0)
            fail("the APPLE ][ banner is not on the glass after a cold "
                 "start on a 640x200 desktop");
        if (lit2 == 0)
            fail("the ] prompt is not on the glass after a cold start on a "
                 "640x200 desktop");
        audit("a cold start on a clipped band");
        no_gunk("a cold start on a clipped band");

        /* ...AND THE ANCHOR FOLLOWS THE CURSOR DOWN, ONCE. A session that
         * scrolls walks CV to row 23 and the band arrives at the bottom
         * anchor the old code started at - and STAYS there, which is what
         * keeps the move off the ordinary scrolling wake. */
        h_puts(23, 0, "]", 0);
        a2_wr(A2_CV, 23);
        do_wake();
        if (a2_gl0 != A2_SCRH - a2_gnl)
            printf("a2uitest: FAIL - the cursor reached row 23 and the band "
                   "stayed anchored at scan line %d\n", a2_gl0),
            fails++;
        lit2 = 0;
        for (y = 0; y < 8; y++)
            for (x = 0; x < a2_gbw; x++)
                if (glass[a2_gsy + a2_gnl - 8 + y][a2_gsx + x] == 1)
                    lit2++;
        if (lit2 == 0)
            fail("the cursor row is not on the glass after the band "
                 "followed it to the bottom");
        audit("after the anchor followed the cursor down");
        before3 = n_blit;
        do_wake();
        if (n_blit != before3)
            fail("a wake with nothing to draw redrew the band - the anchor "
                 "is moving on every flush");
        h_cont_h = sav_h;
        a2_sh_inval();
        do_paint();
        audit("after the cold-start fixture gave the box back");
    }

    /* --- THE SCROLL TEST ON A CLIPPED BAND (a 640x200 desktop) -----------
     * The guard used to be `a2_gnl == A2_SCRH` - the whole 192-line frame on
     * the glass - which on a CGA desktop is NEVER true: dock_top 176 clamps
     * the window, 111 scan lines fit and a2_gl0 is 81. A scrolling Applesoft
     * session, the ordinary case, then took the span path for every scrolled
     * line: ~210 ms A LINE against ~26 for the scroll. The figures below are
     * that machine's, poked in rather than rebuilt - a2_geom recomputes them
     * from the window on the next flush, so the fixture restores the box
     * afterwards and audits. */
    {
        int sav_h = h_cont_h, before2;

        h_cont_h = 137;                 /* the CGA content box */
        /* ...AND CV SAYS WHERE THE CURSOR IS, because the band FOLLOWS it
         * (a2scr.c's a2_geom). A scrolling Applesoft session has walked the
         * cursor to the last row, which is the shape this row is about and
         * the one that puts the anchor at 81. Leaving $25 to whatever an
         * earlier fixture wrote is how this row went on passing while the
         * cold-start case above was black. */
        a2_wr(A2_CV, A2_ROWS - 1);
        a2_sh_inval();
        do_paint();
        if (a2_gnl != 111 || a2_gl0 != 81)
            printf("a2uitest:   (the clipped band is %d lines from %d, not "
                   "the 111/81 the comment quotes)\n", a2_gnl, a2_gl0);
        if (a2_gnl >= A2_SCRH)
            fail("the CGA fixture did not clip the band at all");
        do_wake();
        for (r = 0; r < A2_ROWS - 1; r++)
            for (i = 0; i < A2_COLS; i++)
                a2_wr(a2_tbase[r] + (unsigned)i,
                      (unsigned)a2_rd(a2_tbase[r + 1] + (unsigned)i));
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[A2_ROWS - 1] + (unsigned)i, 0xA0);
        cost_mark();
        before2 = n_scroll;
        do_wake();
        sigs = n_sig - c_sig;
        cost_row("a one-row scroll on a CLIPPED band (CGA)");
        if (n_scroll == before2)
            fail("a whole-page shift on a clipped band was not turned into a "
                 "gfx_scroll - every scrolled line costs the span path");
        /* THE SHIFT TEST SIGNS ONLY THE ROWS THE GLASS HAS EVER SHOWN. Rows
         * above r0 have no visible scan line, so the flush never composes one
         * and their shadow signature is permanently 0: signing them is
         * 0.673 ms a row spent comparing a live value against a sentinel it
         * cannot equal. On this CGA box a2_gl0 is 81, so that is TEN of the
         * twenty-four rows and 6.7 ms off every shift test. */
        if (a2_gl0 > 0 && sigs > A2_ROWS - (a2_gl0 >> 3))
            printf("a2uitest: FAIL - the shift test took %d row signatures "
                   "on a band whose first visible row is %d - it is signing "
                   "rows with no visible scan line\n",
                   sigs, a2_gl0 >> 3),
            fails++;
        audit("after the scroll on a clipped band");
        h_cont_h = sav_h;
        a2_sh_inval();
        do_paint();
        audit("after the box came back to its full height");
    }

    /* --- THE BLIT REFUSAL, AND ITS SECOND PATH ---------------------------
     * os88.h is explicit: blit1 answers -1 with NOTHING DRAWN on a
     * kern_small kernel, "so TEST IT and have a second path". The failure
     * this catches is not a missing row - it is a shadow that says the row is
     * on the glass, so the compare answers "nothing changed" for ever after. */
    blit_refuse = 1;
    a2_sh_inval();
    before = n_run;
    do_wake();
    if (n_run == before)
        fail("blit1 refused and nothing took its place - the screen is blank "
             "with nothing saying why (SPEC.md 47)");
    if (last_toast[0] == 0 && a2_msg[0] == 0)
        fail("blit1 refused and the status row did not say so");
    blit_refuse = 0;
    a2_sh_inval();
    do_wake();
    audit("after the blit refusal was lifted");

    /* --- A FLASH FLIP WIDENS THE ROWS THAT FLASH, AND ONLY THOSE ----------
     * "The write window says nothing about which cells flashed" is true of a
     * FLASHING ROW and of no other. It used to set one flush-wide flag that
     * all 24 rows read, so a blinking `]` composed every other dirty row full
     * width too - 2.434 ms a group it never needed, on the flip flushes that
     * are already the expensive ones. apps/c64/c64scr.c's c64_rowd is per row
     * for the same reason.
     *
     * THE FIXTURE MAKES THE PAGE STEADY FIRST, because a2_flrow[] is set from
     * the SOURCES and by this point in the script several rows hold bytes in
     * $40-$7F. One flashing row, one narrow write on a row that does not
     * flash, and the flip and the write in the SAME flush. */
    a2_sh_inval();
    do_paint();
    for (r = 0; r < A2_ROWS; r++)
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[r] + (unsigned)i, 0xA0);     /* normal-form space */
    do_wake();
    h_puts(4, 0, "F", 2);               /* ONE flashing cell, on row 4 */
    do_wake();
    for (i = 0, r = 0; i < A2_ROWS; i++)
        if (a2_flrow[i])
            r++;
    if (r != 1)
        fail("the per-row widening fixture has more than one flashing row - "
             "the group count below would not mean anything");
    h_puts(17, 20, "W", 0);             /* a narrow write on a STEADY row */
    the_ticks += A2_FLASH_TICKS;
    cost_mark();
    do_wake();
    wide_groups = n_group - c_group;
    cost_row("a flash flip and a narrow write on another row");
    audit("a flash flip and a narrow write on another row");
    /* row 4 flashes and is composed WHOLE - five groups; row 17's write
     * window narrows it to the ONE group its cell is in. Six. */
    if (wide_groups > A2_GROUPS + 1)
        printf("a2uitest: FAIL - a flash flip on ONE row composed %d groups; "
               "the flashing row owes %d and the narrowly-written row owes 1 "
               "- the widening is per FLUSH rather than per ROW\n",
               wide_groups, A2_GROUPS),
        fails++;


    /* ======================================================================
     * WAVE 3: THE OTHER TWO COMPOSERS, MIXED, PAGE2, AND THE TIER TABLE
     *
     * Everything above this line is a TEXT screen, which is what wave 1 and
     * wave 2 had. From here the fixtures change the renderer, so this block
     * comes last and puts the machine back in TEXT when it is done.
     * ====================================================================*/

    /* --- LO-RES: 40 x 48 blocks of 7 x 4, black or white by luminance ---- */
    h_lores();
    if (h_mode(0, 0, 0, 0) == 0)
        fail("TEXT off did not change the renderer - a2_video_set's by-value "
             "guard is refusing a switch that DID move");
    if (a2_mode_of() != A2_MODE_LORES)
        fail("TEXT off with HIRES off is LO-RES");
    cost_mark();
    do_wake();
    lores_groups = n_group_l - c_group_l;
    cost_row("a full 40x48 lo-res repaint");
    audit("a lo-res screen");
    dump_for_a2ref("a2lores");
    for (i = 0; i < A2_ROWS; i++)
        if (a2_flrow[i]) {
            /* A LO-RES BYTE OF $60 IS TWO COLOUR BLOCKS AND NOT A FLASHING
             * `@`. Asking a2_rowflash about a graphics row would force-
             * compose it 3.64 times a second for a phase that changes not one
             * pixel of it - and the fixture above writes bytes across the
             * whole $00-$FF range, so this is not a vacuous test. */
            fail("a LO-RES row was marked as flashing - the flash phase is a "
                 "TEXT-mode thing and a graphics row has no $40-$7F range");
            break;
        }
    cost_mark();
    a2_wr(a2_tbase[5] + 3, 0x9C);
    do_wake();
    cost_row("one changed lo-res block");
    audit("one changed lo-res block");

    /* --- A MODE SWITCH THAT DRAWS THE SAME PICTURE -----------------------
     * A change that actually changes the renderer marks the whole frame
     * (section 7.4), and the span compare is then what decides that nothing
     * moved. The compose is the cost and the draw is nothing, which is the
     * honest shape of this row: 120 groups and 0 blits. */
    cost_mark();
    before = n_blit;
    h_mode(1, 0, 0, 0);                 /* to TEXT... */
    h_mode(0, 0, 0, 0);                 /* ...and back to LO-RES */
    do_wake();
    cost_row("a mode switch that draws the same picture");
    if (n_blit != before)
        fail("a mode switch that ended where it started drew something - the "
             "span compare is what says the picture did not move");
    audit("a mode switch that drew the same picture");

    /* --- EIGHT SOFT-SWITCH READS THAT CHANGE NOTHING ---------------------
     * Every read in $C050-$C05F is side-effecting (section 5.3) and every one
     * of them is GUARDED BY VALUE (5.4). The C64 measured the unguarded form
     * at 25 forced full-width blits, ~234 ms, for register writes that
     * changed nothing. */
    cost_mark();
    before = n_blit;
    n0 = n_group + n_group_l + n_group_h;
    for (i = 0; i < 8; i++)
        a2_io_rd(0xC050);               /* TEXT off - and it is already off */
    do_wake();
    cost_row("eight soft-switch reads that change nothing");
    if (n_blit != before || n_group + n_group_l + n_group_h != n0)
        fail("eight soft-switch reads that changed nothing composed or drew "
             "something - a2_video_set's by-value guard is not holding");

    /* --- A SCROLL IN A GRAPHICS MODE IS NOT A SCROLL ---------------------
     * The k-row shift test proves forty SOURCE bytes and the composition
     * takes the RENDERER as an input the source shadow does not record, so it
     * is a TEXT-mode test and the mode key refuses it here (section 7.7 step
     * 2). What a lo-res screen scrolling costs is the span path, and this row
     * is what that costs. */
    cost_mark();
    before = n_scroll;
    h_scroll_up();
    do_wake();
    cost_row("a one-row scroll in a graphics mode");
    if (n_scroll != before)
        fail("a gfx_scroll was emitted in a graphics mode - the source "
             "shadow holds forty bytes and says nothing about which composer "
             "turned them into pixels");
    audit("a scroll in a graphics mode");

    /* --- HI-RES: 40 source bytes to 35 output bytes a SCAN LINE ---------- */
    h_hires();
    h_mode(0, 0, 0, 1);
    if (a2_mode_of() != A2_MODE_HIRES)
        fail("HIRES on with TEXT off is hi-res");
    if (a2_mode_page() != A2_HGR1)
        fail("the hi-res display page is $2000, and the write window is "
             "taken over it");
    cost_mark();
    do_wake();
    cost_row("a full 280x192 hi-res repaint");
    audit("a hi-res screen");
    dump_for_a2ref("a2hires");

    /* ...AND ONE CHANGED SCAN LINE COSTS ONE SCAN LINE. A hi-res row group's
     * eight lines are eight SEPARATE forty-byte ranges $400 apart, so the
     * dirty scan asks about each of them: marking the row from one write
     * would blit eight lines where the machine changed one. */
    cost_mark();
    before = n_blit;
    h_hplot(100, 10, 0x55);
    do_wake();
    hires_groups = n_group_h - c_group_h;
    cost_row("one changed hi-res scan line");
    audit("one changed hi-res scan line");
    if (n_blit - before != 1)
        printf("a2uitest: FAIL - one hi-res byte cost %d blits: the dirty "
               "scan is marking the whole row group where the machine wrote "
               "ONE of its eight scan lines\n", n_blit - before),
        fails++;
    if (hires_groups > A2_GROUPS)
        printf("a2uitest: FAIL - one hi-res byte composed %d groups where the "
               "write window narrows it to 1: the span is not being taken "
               "over the row's eight ranges\n", hires_groups),
        fails++;

    /* --- MIXED: the top 160 scan lines graphics, the bottom 32 text ------
     * 160 is TWENTY character rows exactly, so the split is a row test and no
     * row is ever half one renderer and half the other (section 7.4). */
    h_mode(0, 1, 0, 1);
    if (a2_row_mode(A2_MIXROW - 1) != A2_MODE_HIRES
        || a2_row_mode(A2_MIXROW) != A2_MODE_TEXT)
        fail("MIXED does not split at row 20 - 160 scan lines is 20 rows");
    if (a2_row_base(A2_MIXROW) != a2_tbase[A2_MIXROW])
        fail("a MIXED text row reads the TEXT page, whatever the graphics "
             "page is");
    h_puts(A2_MIXROW + 1, 2, "MIXED TEXT AT THE FOOT", 0);
    cost_mark();
    do_wake();
    cost_row("a MIXED screen: hi-res over four text rows");
    audit("a MIXED screen");
    dump_for_a2ref("a2mixed");
    /* THE TEXT ROWS OF A MIXED SCREEN ARE OUTSIDE THE WRITE WINDOW'S RANGE.
     * The window is taken over the hi-res page, so it says nothing whatever
     * about a write to $0400 - and a dirty scan that asked it anyway would
     * answer `the window does not reach this row` for every text write and
     * the four rows would never be drawn at all. a2_scan_range's `watched`
     * arm is what stops that, and this is the case that would catch it: the
     * reference is the composer itself, asked for the whole row. */
    {
        static unsigned char mbnd[A2_BSTRIDE * 8];

        memset(mbnd, 0, sizeof(mbnd));
        a2_band_text(mbnd, 0, A2_GROUPS - 1, a2_m.ramseg,
                     a2_row_base(A2_MIXROW + 1), a2_fl_phase ? 0x7F : 0x00);
        for (i = 0; i < 8; i++)
            if (memcmp(mbnd + i * A2_BSTRIDE,
                       a2_sh + ((int)A2_X8(A2_MIXROW + 1) + i) * A2_BSTRIDE,
                       A2_BSTRIDE) != 0) {
                fail("a MIXED screen's TEXT rows were not drawn - they are "
                     "outside the range the write window was taken over, and "
                     "the page bitmap alone is what has to mark them");
                break;
            }
    }

    /* --- AND THE FLIP ITSELF MOVES FOUR ROWS, NOT TWENTY-FOUR (wave 3's
     * review). a2_row_mode(r) for r < A2_MIXROW never reads a2_v_mixed,
     * a2_row_base does not move and neither does the page, so a2_dirty_all on
     * this arm recomposed twenty rows from identical sources with the
     * identical composer to produce identical pixels - ~417 ms of hi-res
     * compose plus 192 span compares to draw four rows that owed ~70. POKE
     * -16302,0 / POKE -16301,0 in a graphics mode is ordinary, and a program
     * that flips the split per frame paid it every frame. a2_dirty_split is
     * the narrow mark; this row is what says it is narrow, in BOTH
     * directions - the rows arrive from the other page each way. */
    {
        int g0, gx;

        cost_mark();
        g0 = n_group + n_group_l + n_group_h;
        h_mode(0, 0, 0, 1);                 /* MIXED off, still hi-res */
        do_wake();
        gx = (n_group + n_group_l + n_group_h) - g0;
        cost_row("MIXED off in hi-res - the split's four rows");
        if (gx > 4 * A2_GROUPS)
            printf("a2uitest: FAIL - clearing MIXED in a graphics mode "
                   "composed %d groups; the split owns four rows, so it owes "
                   "at most %d\n", gx, 4 * A2_GROUPS),
            fails++;
        audit("MIXED cleared in hi-res");

        cost_mark();
        g0 = n_group + n_group_l + n_group_h;
        h_mode(0, 1, 0, 1);                 /* ...and MIXED back on */
        do_wake();
        gx = (n_group + n_group_l + n_group_h) - g0;
        cost_row("MIXED on in hi-res - the split's four rows");
        if (gx > 4 * A2_GROUPS)
            printf("a2uitest: FAIL - setting MIXED in a graphics mode "
                   "composed %d groups; the split owns four rows, so it owes "
                   "at most %d\n", gx, 4 * A2_GROUPS),
            fails++;
        audit("MIXED set again in hi-res");
    }

    /* --- PAGE2, ON BOTH SIDES OF THE SPLIT ------------------------------
     * One switch selects the second page for both halves: text $0800 and
     * hi-res $4000, which are +$400 and +$2000 on their own maps. */
    h_mode(0, 1, 1, 1);
    if (a2_mode_page() != A2_HGR2)
        fail("PAGE2 in hi-res is $4000");
    if (a2_row_base(0) != a2_hbase[0] + (A2_HGR2 - A2_HGR1))
        fail("PAGE2 did not move the hi-res row base by $2000");
    if (a2_row_base(A2_MIXROW) != a2_tbase[A2_MIXROW] + (A2_TXT2 - A2_TXT1))
        fail("PAGE2 did not move the MIXED text rows to $0800");
    do_wake();
    audit("a MIXED screen on page 2");

    /* --- and back to TEXT, which is where the rest of the script lives --- */
    h_mode(1, 0, 0, 0);
    do_wake();
    audit("back in TEXT after the graphics modes");

    /* --- FULL SCREEN AND THE TIER TABLE (section 7.8) --------------------
     * The magnification is decided in ONE place, a2_geom, off the LIVE
     * content box - and a WF_FULL window's content IS its frame, so the box
     * below is the whole screen. VGA 640x480 holds 640 x 384 and gets 2x on
     * both axes; a 200-line CGA holds the width and not the height and gets
     * 2x horizontal, which is the RIGHT SHAPE there because a CGA pixel is
     * already 2:1; the CPU_8086 tier gets 1:1 whatever the adapter, because
     * a2_band_x2 is 10.59 ms for a whole character row - 254 ms on a whole-frame
     * repaint (APPLE2-SPEC 7.9.1, 7.9.2). */
    cost_mark();
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    cost_row("entering full screen at 2x on VGA");
    if (!h_fs)
        fail("Toggle Fullscreen did not enter fullscreen for the tier row");
    if (a2_scw != 2 || a2_sch != 2)
        printf("a2uitest: FAIL - a 640x480 VGA at full screen is 2x on both "
               "axes and this build chose %dx%d\n", a2_scw, a2_sch),
        fails++;
    do_wake();
    audit("full screen at 2x");
    /* ...AND EVERY APPLE PIXEL IS TWO SCREEN PIXELS, BOTH OF THEM. audit()
     * samples the first of each pair, which a doubler that wrote only one
     * would pass; this is the other half, and it is the bug c64band.inc's
     * header names twice. */
    {
        int y, x, bad2 = 0;

        for (y = 0; y < a2_gnl && bad2 == 0; y++)
            for (x = 0; x < A2_BANDW; x++) {
                if (glass[a2_gsy + y * 2][a2_gsx + x * 2]
                    != glass[a2_gsy + y * 2][a2_gsx + x * 2 + 1]
                    || glass[a2_gsy + y * 2][a2_gsx + x * 2]
                       != glass[a2_gsy + y * 2 + 1][a2_gsx + x * 2]) {
                    printf("a2uitest: FAIL - the doubled band is not doubled "
                           "at Apple pixel (%d,%d)\n", x, y);
                    fails++;
                    bad2 = 1;
                    break;
                }
            }
    }
    no_gunk("full screen at 2x");
    /* ...AND THE DOUBLING IS OWED BY A DRAW AND NOT BY A COMPOSE (wave 3's
     * review). a2_band_x2 used to sit beside a2_band_text on the compose
     * side, so a flush that recomposed the frame and drew NOTHING still spent
     * 24 x 10.59 = 254 ms doubling it. That flush is ordinary: a mode switch
     * that draws the same picture, a rect-forced row whose pixels turn out
     * identical, the reset recompose. It is a2_emit's now, behind a per-row
     * latch - so a row that produces no run costs nothing, and a row that
     * produces three runs still doubles ONCE. */
    {
        int x0, b0;

        cost_mark();
        x0 = n_x2;
        b0 = n_blit;
        h_mode(0, 0, 0, 1);                 /* to hi-res... */
        h_mode(1, 0, 0, 0);                 /* ...and back to TEXT: two
                                             * a2_dirty_all's, one picture */
        do_wake();
        cost_row("a fullscreen recompose that draws nothing");
        if (n_blit != b0)
            fail("the fullscreen zero-blit fixture drew something - it is "
                 "meant to recompose the identical picture");
        if (n_x2 != x0)
            printf("a2uitest: FAIL - a fullscreen flush that blitted nothing "
                   "still called a2_band_x2 %d time(s); the doubling belongs "
                   "on the DRAW side (%.1f ms each)\n", n_x2 - x0, MS_X2),
            fails++;
        audit("a fullscreen recompose that draws nothing");
    }

    /* --- ...AND IT IS OWED BY THE RUN AND NOT BY THE ROW -----------------
     * Wave 3 moved a2_band_x2 to the draw side and left it doubling the WHOLE
     * band - all forty bytes, all eight scan lines - whatever the run about to
     * be blitted covered. An ordinary keystroke at 2x emits ONE run seven band
     * bytes wide over eight lines: 56 source byte-rows of work against the 320
     * it paid for, 2.0 ms against 10.6, more than the rest of the keystroke
     * put together. The bound is the run's own rectangle; anything wider is
     * the per-row version back again. */
    {
        long u0;
        int x0;

        do_wake();                      /* let the flash phase and anything
                                         * the fixtures above left dirty
                                         * settle: this row is about ONE cell */
        do_wake();
        u0 = n_x2_u;
        x0 = n_x2;
        cost_mark();
        h_puts(3, 4, "K", 0);
        do_wake();
        cost_row("one changed cell at 2x - the doubling is the RUN's");
        if (n_x2 == x0)
            fail("a keystroke at 2x doubled nothing at all - the blit reads "
                 "the doubled band, so this cannot be right");
        if (n_x2_u - u0 > 8L * 8L)
            printf("a2uitest: FAIL - one changed cell at 2x doubled %ld "
                   "source byte-rows; the run is at most eight bytes over "
                   "eight scan lines, so it owes at most 64 (%.3f ms each)\n",
                   n_x2_u - u0, MS_X2_B),
            fails++;
        audit("one changed cell at 2x");
    }

    /* --- THE ABOUT PANEL AT 2x, WHICH IS WHERE THE HOLD RANGE WAS WRONG --
     * The panel's rect is in SCREEN pixels and the hold range is in APPLE
     * scan lines, and at 2x those are not the same thing (section 7.8: the
     * doubling is at BLIT time). ovl_about_geom shipped without the divide,
     * so at VGA fullscreen it held lines [131,191] while covering [66,125] -
     * two DISJOINT ranges. Everything under the card was recomposed and
     * blitted straight over it on the next flush, and the bottom third of the
     * picture froze. Neither the fullscreen rows above nor the About rows
     * further up could see it, because they never crossed.
     *
     * THE ASSERTION IS THE CONVERSION DONE BY HAND, and then a wake with the
     * blit stub watching: it mirrors the flush's hold test in Apple-line
     * coordinates (this file's blit1), so a blit inside the range is caught
     * where it happens. */
    {
        int wl0, wl1, t;

        do_about();
        if (!a2_abt_up)
            fail("the About panel did not come up at 2x");
        if (a2_abt_w != a2_gbw)
            printf("a2uitest: FAIL - the About panel is %d px wide against a "
                   "band of %d; a narrower panel leaves live Apple picture "
                   "either side of it and the flush's hold is per ROW, so "
                   "those strips freeze\n", a2_abt_w, a2_gbw),
            fails++;
        /* the panel's rect, converted by hand: an Apple line is a2_sch screen
         * rows, and only a line covered WHOLLY may be held */
        t = a2_abt_y - a2_gsy;
        wl0 = a2_gl0 + (t + a2_sch - 1) / a2_sch;
        t = a2_abt_y + a2_abt_h - a2_gsy;
        wl1 = a2_gl0 + t / a2_sch - 1;
        if (wl1 > A2_SCRH - 1)
            wl1 = A2_SCRH - 1;
        if (a2_hold_l0 != wl0 || a2_hold_l1 != wl1)
            printf("a2uitest: FAIL - the About panel at %dx%d holds Apple "
                   "lines [%d,%d] and covers [%d,%d]\n", a2_scw, a2_sch,
                   a2_hold_l0, a2_hold_l1, wl0, wl1),
            fails++;
        memset(h_blitrow, 0, sizeof h_blitrow);
        a2_dirty_all();                 /* every line asks to be drawn... */
        do_wake();
        {
            int l, k, sy, over = 0;

            for (l = a2_hold_l0; l <= a2_hold_l1; l++)
                for (k = 0; k < a2_sch; k++) {
                    sy = a2_gsy + (l - a2_gl0) * a2_sch + k;
                    if ((unsigned)sy < sizeof h_blitrow && h_blitrow[sy])
                        over++;
                }
            if (over)
                printf("a2uitest: FAIL - %d screen row(s) under the About "
                       "panel were BLITTED with the card up; the picture is "
                       "being drawn over it on every flush\n", over),
                fails++;
        }
        audit("the About panel at 2x");
        do_click(a2_abt_x + 4, a2_abt_y + 4);
        do_wake();
        if (a2_abt_up)
            fail("a click did not close the About panel at 2x");
        audit("the About panel closed at 2x");
    }
    do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
    do_wake();
    audit("back out of full screen");
    if (a2_scw != 1 || a2_sch != 1)
        fail("a framed window is 1:1 - it is authored 336 wide and there is "
             "nothing to fill");

    /* --- THE CGA ARM: 2x HORIZONTAL ONLY --------------------------------- */
    {
        int sw = h_scr_w, sh = h_scr_h;

        h_scr_w = 640;
        h_scr_h = 200;
        do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
        do_wake();
        if (a2_scw != 2 || a2_sch != 1)
            printf("a2uitest: FAIL - a 640x200 CGA at full screen is 2x "
                   "HORIZONTAL only and this build chose %dx%d\n",
                   a2_scw, a2_sch),
            fails++;
        if (a2_gnl >= A2_SCRH)
            fail("a 200-line screen cannot show all 192 Apple scan lines "
                 "with a border and a status row - the band must clip");
        audit("full screen on a 640x200 CGA");
        do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
        h_scr_w = sw;
        h_scr_h = sh;
        do_paint();
        do_wake();
        audit("back out of the CGA fixture");
    }

    /* --- THE CPU_8086 TIER: 1:1, AND THE FLASH PHASE REFUSED ------------
     * The refusal carries the MEASURED cost of a flip (section 10.3), and it
     * is a2_fl_ok that is cleared - the same byte Machine > Flashing text
     * moves - so the refusal and the greying cannot disagree. */
    {
        h_cpu = OS88_CPU_8086;
        a2_tier_init();
        a2_menu_state();
        if (a2_fl_ok)
            fail("the CPU_8086 tier does not refuse the flash phase - a flip "
                 "is 43.1 ms and 3.64 of them a second is 157 ms/s of an "
                 "8088 (section 10.3)");
        if (a2_mach_items[A2_I_FLASH][0] != 1)
            fail("Machine > Flashing text is not GREYED on the CPU_8086 tier "
                 "- an item that is live and can only refuse is what SPEC.md "
                 "47 exists to stop");
        n0 = a2_fl_phase;
        the_ticks += A2_FLASH_TICKS * 2;
        do_wake();
        if (a2_fl_phase != n0)
            fail("the flash phase flipped on the CPU_8086 tier");
        do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
        do_wake();
        if (a2_scw != 1 || a2_sch != 1)
            printf("a2uitest: FAIL - the CPU_8086 tier is 1:1 at full screen "
                   "and this build chose %dx%d\n", a2_scw, a2_sch),
            fails++;
        audit("full screen on the CPU_8086 tier");
        do_cmd(A2_M_MACHINE, A2_I_FULLSCR);
        h_cpu = OS88_CPU_386;
        a2_tier_init();
        a2_fl_ok = 1;
        a2_menu_state();
        do_paint();
        do_wake();
        audit("back off the CPU_8086 tier");
    }

    /* --- THE GAME BUTTONS ARE LEVELS (section 6.3) -----------------------
     * F1 and F2 are PB0 and PB1 at $C061/$C062, polled once a wake through
     * os88_key_down and read from the cache by the soft switch - not one
     * bridge crossing per emulated read, which is what a game polling $C061
     * in a loop would cost. */
    h_down[KSC_F1] = 1;
    do_wake();
    if ((a2_io_rd(0xC061) & 0x80) == 0)
        fail("F1 is PB0 and $C061 answers bit 7 SET while it is held");
    if ((a2_io_rd(0xC062) & 0x80) != 0)
        fail("F2 is PB1 and $C062 must not answer PB0's level");
    if ((a2_io_rd(0xC069) & 0x80) == 0)
        fail("$C060-$C06F decodes THREE address bits - $C069 mirrors $C061 "
             "(UTAIIe 7-5)");
    h_down[KSC_F1] = 0;
    h_down[KSC_F2] = 1;
    do_wake();
    if ((a2_io_rd(0xC061) & 0x80) != 0 || (a2_io_rd(0xC062) & 0x80) == 0)
        fail("the two game buttons are not independent");
    h_down[KSC_F2] = 0;
    do_wake();
    if ((a2_io_rd(0xC061) & 0x80) != 0)
        fail("a released F1 is still reported down - the buttons are LEVELS "
             "and the poll is what takes them back up");
    /* ...AND Ctrl+F3 HOLDS PB0 ACROSS THE RESET IT ASKS FOR, which is what
     * `Open-Apple-Control-Reset` means on a machine with no Open-Apple key
     * (section 6.3). The poll runs at the TOP of the wake and the reset
     * service after it, which is the order that makes this survive. */
    do_key(0, 0x60);
    do_wake();
    if (!a2_btn[0])
        fail("Ctrl+F3 did not hold PB0 across the reset - it is the II+'s "
             "own spelling of AppleWin's Open-Apple-Control-Reset");
    do_wake();
    if (a2_btn[0])
        fail("PB0 stayed down after the reset's own wake - the poll is what "
             "takes it back up");

    /* --- THE KEYBOARD-MOUSE RULE (section 6.6) --------------------------
     * On a machine with no mouse the kernel eats the arrows, Space and Del as
     * its pointer, and ScrollLock hands them back. The package cannot ask
     * `has a mouse spoken`, so it asks a question with the same answer: the
     * down-map says one is held and os88_onkey has never once delivered one.
     * Three consecutive polls AND A2_KBM_TICKS ticks, because a wake posted
     * before a press is dispatched ahead of the key event behind it - and on
     * a fast host that gap is many polls wide, which is why the window is
     * measured in TICKS. do_wake() here advances the tick once per wake,
     * which is the SLOWEST a real machine ever polls; the fixture below asks
     * the other end of that range. */
    a2_msg[0] = 0;
    h_down[KSC_SPACE] = 1;
    do_wake();
    if (a2_msg[0])
        fail("the keyboard-mouse message was said on ONE poll - a wake posted "
             "before the press is dispatched ahead of the key event behind it");
    do_wake();
    do_wake();
    if (a2_msg[0])
        fail("the keyboard-mouse message was said three polls after the "
             "press, inside the window a keystroke's own dispatch takes");
    do_wake();
    do_wake();
    if (!a2_msg[0])
        fail("Space held for A2_KBM_TICKS with no key ever delivered is the "
             "kernel eating it, and section 6.6 says so once a session");
    do_wake();
    do_wake();
    h_down[KSC_SPACE] = 0;

    /* ...AND THE FAST-HOST CASE, WHICH IS THE ONE THAT SHIPPED WRONG. A wake
     * runs about 1,400 times a second, so twenty polls span about fourteen
     * milliseconds - well inside the gap between the ISR setting Space's
     * down-bit and the W_ONKEY carrying that very Space being dispatched. The
     * three-poll rule latched there, and QEMU - which HAS a mouse and was
     * eating nothing - answered the first Space of `PRINT 6*7` by putting
     * `ScrollLock: arrows, Space.` in the status row
     * (build/port-shots/wave4-cga-print.png). do_wake() advances the tick, so
     * the fixture puts it back: that is the whole difference between a poll
     * count and a window. */
    a2_key_typed = 0;
    a2_slock_said = 0;
    a2_key_held = 0;
    a2_msg[0] = 0;
    h_down[KSC_SPACE] = 1;
    {
        unsigned t_hold;

        for (i = 0; i < 20; i++) {
            t_hold = the_ticks;
            do_wake();
            the_ticks = t_hold;
        }
    }
    if (a2_msg[0])
        fail("twenty polls inside ONE tick latched the keyboard-mouse "
             "message - that is a keystroke's own dispatch gap on a fast "
             "host, not a kernel eating the key");
    h_down[KSC_SPACE] = 0;
    do_wake();
    /* ...AND THE LATCH IS ONE WAY. A key the pointer would have eaten has
     * ARRIVED, so it is not eating them - either a mouse has spoken or
     * ScrollLock is on, and neither un-happens. */
    a2_key_typed = 0;
    a2_slock_said = 0;
    a2_key_held = 0;
    a2_msg[0] = 0;
    do_key(32, KSC_SPACE);              /* Space, delivered */
    h_down[KSC_LEFT] = 1;
    do_wake();
    do_wake();
    do_wake();
    do_wake();
    if (a2_msg[0])
        fail("the keyboard-mouse message was said after one of those keys had "
             "been DELIVERED - the latch is one way and this is the whole of "
             "what it observes");
    h_down[KSC_LEFT] = 0;
    do_wake();
    audit("after the keyboard rows");

    /* --- THE LO-RES LUMINANCE LADDER, FOR tools/a2ref.py --lumcheck ------
     * The package's sixteen bytes are the palette's luminance RANK; a2ref
     * derives its own ordering from MII's `palettes[0]` "Color NTSC"
     * (src/mii_video.c:94-113) through MII's own lo-res mapping
     * (src/mii_video.c:173-177) - the palette the reference DISPLAYS, where
     * AppleWin's PaletteRGB_NTSC lores block is a placeholder its own comment
     * disclaims - and compares all 256 ordered pairs. apple2emu's
     * Lores_colors (src/video.cpp:100-115) is the cross-check. Writing the
     * table out is how the two meet. */
    {
        FILE *lf = fopen("build/a2lum.bin", "wb");

        if (!lf)
            fail("cannot write build/a2lum.bin");
        else {
            fwrite(a2_lum, 1, 16, lf);
            fclose(lf);
        }
    }

    /* --- THE SPEED FIELD (APPLE2-SPEC section 9) -------------------------
     * IT IS THE ONE WIDGET ON THE GLASS WHOSE VALUE IS ARITHMETIC, AND IT
     * SHIPPED SATURATED. a2_cyc_add stopped accumulating at 60,000 64-cycle
     * units, so with den = 876 x 18 / 100 = 157 the largest per cent it could
     * ever print was 382: 380 %, 400 %, 1000 % and 3000 % all read `383%`,
     * the `pct > 9999` clamp was unreachable dead code, and 383 is what the
     * wave-2 screendumps show. Nothing here saw it, because the fixtures
     * above never advance the clock far enough to fold a window and no
     * assertion read a2_pct.
     *
     * THE GATE IS THAT TWO VERY DIFFERENT MACHINE SPEEDS DO NOT READ THE
     * SAME. Each case feeds the exact number of cycles a machine running at
     * that per cent would have run in an 18-tick window, one A2_SLICE_MAX
     * slice at a time, and asks the field what it says. The tolerance is one
     * per cent of the figure plus 2, which is the truncation the unit's own
     * shift can cost. */
    {
        static const int want[] = { 3, 100, 383, 400, 1000, 3000, 6000, 0 };
        long cyc;
        int ci, ran, tol, got;

        for (ci = 0; want[ci]; ci++) {
            a2_cyc_zero();
            a2_pct = -1;
            a2_sp_tick = the_ticks;
            /* 18 ticks of a 1.02 MHz Apple is 1,009,260 cycles; `want`
             * per cent of that is what this machine would have run */
            cyc = (long)18 * 56070L * (long)want[ci] / 100L;
            while (cyc > 0) {
                ran = (cyc > (long)A2_SLICE_MAX) ? A2_SLICE_MAX : (int)cyc;
                a2_cyc_add(ran);
                cyc -= (long)ran;
            }
            the_ticks += 18;
            a2_speed_fold();
            got = a2_pct;
            tol = 2 + want[ci] / 100;
            if (got < want[ci] - tol || got > want[ci] + tol) {
                printf("a2uitest: FAIL - the speed field read %d %% for a "
                       "machine running at %d %% of a 1.02 MHz Apple "
                       "(tolerance %d)\n", got, want[ci], tol);
                fails++;
            }
        }
        /* ...AND THE CLAMP IS REACHABLE, which is the other half: a figure
         * the arithmetic cannot produce is a figure nobody has checked. */
        a2_cyc_zero();
        a2_pct = -1;
        a2_sp_tick = the_ticks;
        cyc = (long)18 * 56070L * 12000L / 100L;
        while (cyc > 0) {
            ran = (cyc > (long)A2_SLICE_MAX) ? A2_SLICE_MAX : (int)cyc;
            a2_cyc_add(ran);
            cyc -= (long)ran;
        }
        the_ticks += 18;
        a2_speed_fold();
        if (a2_pct != 9999)
            printf("a2uitest: FAIL - 12,000 %% read %d %% and not the 9999 "
                   "clamp: the clamp is unreachable again\n", a2_pct),
            fails++;
        printf("a2uitest: the speed field tracks 3 %% to 6,000 %% and clamps "
               "at 9,999\n");
        a2_cyc_zero();
        a2_pct = 0;
        a2_sp_tick = the_ticks;
        a2_menu_state();
    }

    /* ======================================================================
     * THE WALL SLICE'S DUTY-CYCLE CONTROLLER (APPLE2-SPEC section 4.3.1)
     *
     * The rule this replaced could not leave `A2_SLICE_MIN` on a 4.77 MHz
     * 8088 and that was arithmetic, not luck - four consecutive clean slices
     * to double against one tick-crossing to halve has a fixed point at 14 %
     * of a host tick. Both arms below are the machine the port cannot ask an
     * emulator about: MartyPC is an 8088 and every profile in the tree is a
     * 5150, so the FAST tier has no host at all here, and the slow one takes
     * ten minutes a reading. `h_run_cost` prices a slice instead and this
     * runs in `build.sh`.
     * ====================================================================*/
    {
        int i, b;
        int st = a2_state, pz = a2_pause;

        a2_state = A2_ST_RUN;
        a2_pause = 0;

        /* --- A MACHINE THE BUDGET IS NOWHERE NEAR: it must reach the cap.
         * This is the old rule's fast arm, kept whole as `a2_adx == 0`, and
         * it is what a 386 and a 486 take. */
        h_run_cost = 0;
        h_run_ut = 0;
        a2_budget = A2_SLICE_MIN;
        a2_adn = 0;
        a2_adx = 0;
        for (i = 0; i < 200; i++)
            do_wake();
        if (a2_budget != A2_SLICE_MAX) {
            printf("a2uitest: FAIL - 200 slices that cost no host tick left "
                   "the budget at %d and not the %d cap: the fast arm is "
                   "gone, so every tier above the 8088 lost its speed\n",
                   a2_budget, A2_SLICE_MAX);
            fails++;
        }

        /* --- ...AND THE 4.77 MHz 8088, at the price MEASURED on MartyPC
         * (APPLE2-SPEC 16.4.1: 256 cycles took 22.5 ms of a 54.9 ms tick, so
         * a cycle is ~1,600 microticks). The band is wide on purpose - the
         * controller OSCILLATES inside its deadband by design - and what it
         * pins is the two things that matter: the budget LEFT the floor, and
         * it stopped a long way short of a slice that overruns a tick. */
        h_run_cost = 1600;
        h_run_ut = 0;
        a2_budget = A2_SLICE_MIN;
        a2_adn = 0;
        a2_adx = 0;
        for (i = 0; i < 600; i++)
            do_wake();
        b = a2_budget;
        if (b <= A2_SLICE_MIN) {
            printf("a2uitest: FAIL - a priced 4.77 MHz slice left the budget "
                   "at %d, which is A2_SLICE_MIN: the controller has the old "
                   "rule's fixed point again and the XT is back to 0.41 %%\n",
                   b);
            fails++;
        } else if (b < 384 || b > 768) {
            printf("a2uitest: FAIL - a priced 4.77 MHz slice settled at %d "
                   "cycles; ~0.75 of a tick is ~469 and the band is "
                   "384..768\n", b);
            fails++;
        } else {
            printf("a2uitest: the slice controller reaches the cap on a free "
                   "machine and settles at %d cycles - %d %% of a host tick - "
                   "on a priced 4.77 MHz one\n",
                   b, (int)((long)b * 1600L / 10000L));
        }

        h_run_cost = 0;
        h_run_ut = 0;
        a2_budget = A2_SLICE_MIN;
        a2_adn = 0;
        a2_adx = 0;
        a2_state = st;
        a2_pause = pz;
        a2_cyc_zero();
        a2_pct = 0;
        a2_sp_tick = the_ticks;
    }

    /* ======================================================================
     * WAVE 4 - THE COMMANDS, THE CLIPBOARD, AND PROGRAM LOAD AND SAVE
     * (APPLE2-SPEC sections 6.5, 10.2, 12)
     *
     * The same script the QMP session drives on the glass, against the model:
     * paste a listing, copy the text page back, save a program, load it
     * again, and refuse the files and the claims that have to be refused.
     * ====================================================================*/
    {
        static const unsigned char prog[] = {
            /* 10 PRINT "HI" : the link, the line number, the tokens, $00 */
            0x0B, 0x08, 0x0A, 0x00, 0xBA, 0x22, 0x48, 0x49, 0x22, 0x00,
            /* 20 GOTO 10 - eight bytes at $080B, so its link is $0813 */
            0x13, 0x08, 0x14, 0x00, 0xAB, 0x31, 0x30, 0x00,
            /* ...and the terminator, which is part of the program */
            0x00, 0x00
        };
        static unsigned char before[sizeof(prog)];
        static char want[2048];
        static char got[2048];
        int k, n, c, r, ok;
        unsigned a, vartab;

        /* --- EDIT > COPY: the text page onto the clipboard ---------------- */
        for (a = 0; a < 24; a++)
            for (k = 0; k < A2_COLS; k++)
                a2_wr(a2_tbase[a] + (unsigned)k, 0xA0);
        h_puts(0, 0, "HELLO", 0);           /* normal */
        h_puts(1, 0, "]", 1);               /* INVERSE, which folds to the same
                                             * letter: what the user sees
                                             * flashing and what the clipboard
                                             * gets are one character */
        h_puts(2, 0, "10 PRINT", 2);        /* flashing */
        do_wake();
        do_paint();

        h_clip_n = -1;
        n = h_claims_out();
        n_copyrow = 0;
        n_srcrd_copy = n_srcrd;
        do_cmd(A2_M_EDIT, A2_I_COPY);
        if (h_clip_n != -1)
            fail("Edit > Copy touched the clipboard from os88_oncmd, which is "
                 "dispatched under the DESKTOP's gfx lock (section 6.5)");
        if (h_claims_out() != n)
            fail("Edit > Copy took a heap claim under the gfx lock");
        do_wake();
        n_srcrd_copy = n_srcrd - n_srcrd_copy;
        if (h_claims_out() != n)
            fail("Edit > Copy's staging claim outlived its own wake");

        /* the expectation, built here from the rule and not from the program */
        k = 0;
        for (r = 0; r < 24; r++) {
            const char *row = (r == 0) ? "HELLO"
                            : (r == 1) ? "]"
                            : (r == 2) ? "10 PRINT" : "";
            while (*row)
                want[k++] = *row++;
            want[k++] = 0x0D;
        }
        /* WHAT THE WHOLE-SCREEN COPY COSTS, AND HOW MANY TIMES IT CROSSES
         * THE SEGMENT BOUNDARY (section 6.5). The bridge count is the point:
         * ONE crossing - os88_oncmd into ovl_a2_cmd and back - because the
         * command is a latch and every per-byte step is resident. The C64's
         * first draft of the same pair crossed 2,000 times, two a cell, and
         * a call-counting cost model charged one. */
        printf("a2uitest:   Edit > Copy of the whole 40x24 text page: "
               "%.1f ms, %d a2_zcopy_out + %d a2_copy_row + 1 clip_put, "
               "ONE bridge crossing\n",
               n_srcrd_copy * MS_SRCRD
               + n_copyrow * (MS_CPYCALL + A2_COLS * MS_CPYCELL)
               + 3 * 0.0467,            /* clip_put, mem_claim, mem_free */
               n_srcrd_copy, n_copyrow);
        if (h_clip_n != k || memcmp(h_clip, want, (size_t)k) != 0) {
            printf("a2uitest: FAIL - Edit > Copy put %d bytes on the "
                   "clipboard where the 24 trimmed rows are %d\n",
                   h_clip_n, k);
            fails++;
        }

        /* ...AND THE REFUSALS ARE SAID. A claim that cannot be had and a
         * clipboard that will not take the screen are both facts (SPEC.md 47),
         * and neither may touch the machine. */
        h_claim_refuse = 1;
        do_cmd(A2_M_EDIT, A2_I_COPY);
        do_wake();
        if (strcmp(a2_msg, "No memory for the copy.") != 0)
            fail("a Copy with no heap for its staging claim said nothing");
        h_clip_refuse_put = 1;
        do_cmd(A2_M_EDIT, A2_I_COPY);
        do_wake();
        if (strcmp(a2_msg, "The clipboard refused it.") != 0)
            fail("a Copy the clipboard refused said nothing");
        h_clip_refuse_put = 0;
        if (h_claims_out() != n)
            fail("a refused Copy leaked its staging claim");

        /* --- EDIT > PASTE: the peek/consume handshake --------------------- */
        /* THE LATCH IS EMPTIED FIRST, AND THAT IS A REAL PRECONDITION rather
         * than harness hygiene: a2_paste_peek presents only into a FREE latch
         * now, so a key left in it by an earlier test is delivered ahead of
         * the queue - which is the whole point of the guard and is asserted
         * on its own two blocks down. */
        a2_io_rd(0xC010);
        os88_clip_put("10 print \"hi\"\r\n20 goto 10\n", 26);
        n = h_claims_out();
        do_cmd(A2_M_EDIT, A2_I_PASTE);
        if (h_claims_out() != n)
            fail("Edit > Paste took its claim under the gfx lock");
        do_wake();
        if (h_claims_out() != n + 1)
            fail("Edit > Paste has no queue claim after the wake that "
                 "serviced it");

        /* THE MACHINE DRINKS IT AT ITS OWN RATE, which is what $C000 and
         * $C010 are: a peek and a consume (apple2emu's keyboard_read and
         * keyboard_clear). Reading $C000 twice without a strobe must present
         * the SAME byte - anything else is input overrun, the third defect no
         * screendump can show. */
        k = 0;
        for (r = 0; r < 200; r++) {
            c = a2_io_rd(0xC000);
            if ((c & 0x80) == 0)
                break;                      /* the queue is drained */
            if (a2_io_rd(0xC000) != c) {
                fail("two $C000 reads with no strobe between them presented "
                     "DIFFERENT bytes - the paste is running ahead of the "
                     "machine (section 6.5)");
                break;
            }
            got[k++] = (char)(c & 0x7F);
            a2_io_rd(0xC010);
        }
        got[k] = 0;
        if (strcmp(got, "10 PRINT \"HI\"\r20 GOTO 10\r") != 0) {
            printf("a2uitest: FAIL - the paste typed \"%s\": the folds are "
                   "LF to CR, CR LF to ONE CR and lower case to upper "
                   "(section 6.5)\n", got);
            fails++;
        }
        if (h_claims_out() != n)
            fail("a drained paste did not give its claim back");

        /* --- A KEY TYPED DURING A PASTE REACHES THE MACHINE, AND COSTS THE
         * QUEUE NOTHING (section 6.5) --------------------------------------
         * The peek used to rewrite the latch on EVERY $C000 read while a
         * paste was live, so a key the user typed was destroyed before the
         * machine could see it: Ctrl-C - the only in-machine way to stop a
         * runaway paste - could never arrive. Presenting only into a free
         * latch fixes that and creates the second half of this test: the
         * strobe that follows the user's key must NOT advance the queue, or
         * every keystroke would swallow one pasted character. */
        os88_clip_put("AB", 2);
        do_cmd(A2_M_EDIT, A2_I_PASTE);
        do_wake();
        if ((a2_io_rd(0xC000) & 0x7F) != 'A')
            fail("the paste did not present its first byte");
        a2_kb_put(0x03);                    /* the user types Ctrl-C */
        if ((a2_io_rd(0xC000) & 0x7F) != 0x03)
            fail("a key typed during a paste was overwritten by the queue - "
                 "Ctrl-C can never reach the emulated program (section 6.5)");
        a2_io_rd(0xC010);                   /* ...and the machine takes IT */
        if ((a2_io_rd(0xC000) & 0x7F) != 'A')
            fail("the strobe after a user keystroke consumed a QUEUED byte - "
                 "one pasted character lost per key");
        a2_io_rd(0xC010);
        if ((a2_io_rd(0xC000) & 0x7F) != 'B')
            fail("the paste did not resume behind the user's key");
        a2_io_rd(0xC010);
        if (a2_io_rd(0xC000) & 0x80)
            fail("the paste queue did not drain");
        if (h_claims_out() != n)
            fail("the interrupted paste did not give its claim back");

        os88_clip_put("AB", 2);
        do_cmd(A2_M_EDIT, A2_I_PASTE);
        do_wake();
        a2_reset_req = A2_RST_CTRL;
        do_wake();                          /* a reset empties the queue */
        if (a2_io_rd(0xC000) & 0x80)
            fail("a reset left the previous machine's paste still typing");
        if (h_claims_out() != n)
            fail("a reset emptied the paste queue and kept its claim");

        h_clip_n = -1;
        do_cmd(A2_M_EDIT, A2_I_PASTE);
        do_wake();
        if (strcmp(a2_msg, "The clipboard is empty.") != 0)
            fail("a Paste of an empty clipboard said nothing");

        /* --- FILE > SAVE PROGRAM..., THEN LOAD IT BACK -------------------- */
        for (k = 0; k < (int)sizeof(prog); k++)
            a2_wr(0x0801 + (unsigned)k, prog[k]);
        vartab = 0x0801 + sizeof(prog);
        a2_wr(0x0069, (int)(vartab & 0xFF));
        a2_wr(0x006A, (int)(vartab >> 8));
        memcpy(before, prog, sizeof(prog));

        h_dlg_n = 0;
        do_cmd(A2_M_FILE, A2_I_SAVE);
        if (h_dlg_n != 1 || h_dlg_mode != OS88_FDLG_SAVE)
            fail("File > Save Program... did not open a SAVE dialog");
        h_file_n = 0;
        do_file(OS88_FDLG_SAVE, "WORK.BAS", 0);
        if (h_file_n != sizeof(prog)
            || memcmp(h_file, prog, sizeof(prog)) != 0) {
            printf("a2uitest: FAIL - Save Program wrote %u bytes where "
                   "$0801 to VARTAB-1 is %u (section 12)\n",
                   h_file_n, (unsigned)sizeof(prog));
            fails++;
        }

        /* the machine forgets, and the file brings it back */
        for (k = 0; k < 64; k++)
            a2_wr(0x0801 + (unsigned)k, 0xEE);
        a2_wr(0x0069, 0);
        a2_wr(0x006A, 0);
        n = h_claims_out();
        do_cmd(A2_M_FILE, A2_I_LOAD);
        if (h_dlg_mode != OS88_FDLG_OPEN)
            fail("File > Load Program... did not open an OPEN dialog");
        do_file(OS88_FDLG_OPEN, "WORK.BAS", h_file_n);
        ok = 1;
        for (k = 0; k < (int)sizeof(prog); k++)
            if (a2_rd(0x0801 + (unsigned)k) != before[k])
                ok = 0;
        if (!ok)
            fail("a Save and a Load did not round-trip the program bytes");
        if (h_claims_out() != n)
            fail("Load Program leaked its file claim");
        for (a = 0x67; a <= 0x6E; a += 2) {
            unsigned v = (unsigned)a2_rd(a) | ((unsigned)a2_rd(a + 1) << 8);

            if (a == 0x67 ? (v != 0x0801) : (v != vartab)) {
                printf("a2uitest: FAIL - after a Load, $%02X reads $%04X: "
                       "TXTTAB is $0801 and VARTAB = ARYTAB = STREND = "
                       "PRGEND = the end (section 12)\n", a, v);
                fails++;
            }
        }
        if (((unsigned)a2_rd(0xAF) | ((unsigned)a2_rd(0xB0) << 8)) != vartab)
            fail("after a Load, PRGEND is not the end of the program");

        /* --- AND THE SAME FILE THROUGH THE ASSOCIATION (SPEC.md 54.5) ----
         * `CC_ASSOC` declares `BAS`, so a double-click hands this package a
         * NAME AND A FOLDER AND NO SIZE - which is the arm the picker never
         * takes. It is spent in the WAKE, because the loader is an ovl_ and
         * os88_main has no instance to resolve a module for. */
        for (k = 0; k < 64; k++)
            a2_wr(0x0801 + (unsigned)k, 0xEE);
        os88_strcpy(h_arg, "WORK.BAS", sizeof(h_arg));
        memcpy(h_file, prog, sizeof(prog));
        h_file_n = sizeof(prog);
        if (os88_arg_file(a2_argname, &a2_argplace) != 0)
            fail("os88_arg_file gave nothing back for a launch document");
        a2_argp = 1;
        a2_argdl = the_ticks + A2_ARGWAIT;
        r = h_goto_n;

        /* ...AND IT WAITS FOR THE MACHINE TO REACH `]` FIRST. On the first
         * wake the 6502 has run a few hundred cycles: the Autostart Monitor
         * has not handed over yet and Applesoft's cold start ENDS IN A NEW,
         * so a load spent here is wiped by the ROM a moment later and LIST
         * comes up empty with nothing saying why. The power-on pattern is
         * what $67/$68 hold until the cold start writes them. */
        a2_wr(0x0067, 0x00);
        a2_wr(0x0068, 0xFF);
        do_wake();
        if (h_goto_n != r)
            fail("the launch document was loaded before the machine had "
                 "cold-started - Applesoft's own NEW is about to wipe it");
        if (!a2_argp)
            fail("the launch document was given up on while the machine was "
                 "still booting");
        a2_wr(0x0067, 0x01);            /* TXTTAB = $0801 ... */
        a2_wr(0x0068, 0x08);
        a2_wr(0x0069, 0x03);            /* ...and VARTAB = $0803, which is
                                         * what the cold start's NEW leaves */
        a2_wr(0x006A, 0x08);
        do_wake();
        if (h_goto_n != r + 1)
            fail("the launch did not stand in the document's own folder "
                 "(SPEC.md 54.9)");
        ok = 1;
        for (k = 0; k < (int)sizeof(prog); k++)
            if (a2_rd(0x0801 + (unsigned)k) != before[k])
                ok = 0;
        if (!ok)
            fail("a .BAS double-click launched the emulator and did NOT load "
                 "the program - which is the `launches and then refuses` this "
                 "association was held back for (section 12)");
        os88_strcpy(h_arg, "WORK.BAS", sizeof(h_arg));
        os88_arg_file(a2_argname, &a2_argplace);
        a2_argp = 1;
        a2_argdl = the_ticks + A2_ARGWAIT;
        h_goto_fail = 1;
        do_wake();
        h_goto_fail = 0;
        if (strcmp(a2_msg, "Cannot open that folder.") != 0)
            fail("a launch whose folder could not be listed went quiet - the "
                 "window comes up at `]` and the double-click looks like it "
                 "did nothing (cword.c:2650)");

        /* ...AND THE WAIT IS BOUNDED. A machine that never reaches `]` must
         * not leave a load armed for the rest of the session: it would then
         * fire on the user's own NEW, minutes later, with no `]` in sight. */
        os88_strcpy(h_arg, "WORK.BAS", sizeof(h_arg));
        os88_arg_file(a2_argname, &a2_argplace);
        a2_argp = 1;
        a2_argdl = the_ticks + 2;
        a2_wr(0x0067, 0x00);
        a2_wr(0x0068, 0xFF);
        the_ticks += 4;
        do_wake();
        if (a2_argp)
            fail("a launch document whose machine never got to `]` stayed "
                 "armed for the rest of the session");
        if (strcmp(a2_msg, "No ] prompt to load into.") != 0)
            fail("giving up on a launch document said nothing");
        a2_wr(0x0067, 0x01);
        a2_wr(0x0068, 0x08);

        /* --- THE LINKS ARE REPAIRED, AS FIX.LINKS DOES (section 12) -------
         * The same program with its chain written for a load address of
         * $1801. Applesoft rebuilds the chain from the line LENGTHS, and so
         * does this: what lands in memory must be the $0801 chain. */
        memcpy(h_file, prog, sizeof(prog));
        h_file[0] = 0x0B;
        h_file[1] = 0x18;                   /* $180B */
        h_file[10] = 0x13;
        h_file[11] = 0x18;                  /* $1813 */
        h_file_n = sizeof(prog);
        do_file(OS88_FDLG_OPEN, "OTHER.BAS", h_file_n);
        if (a2_rd(0x0801) != 0x0B || a2_rd(0x0802) != 0x08
            || a2_rd(0x080B) != 0x13 || a2_rd(0x080C) != 0x08)
            fail("a program whose links name another load address was not "
                 "REPAIRED to $0801 (section 12's FIX.LINKS walk)");

        /* --- AND WHAT IS REFUSED IS REFUSED BEFORE ANYTHING IS WRITTEN ---- */
        for (k = 0; k < 8; k++) {
            h_file[k] = 0xEE;
            a2_wr(0x0801 + (unsigned)k, 0x5A);
        }
        h_file_n = 8;                       /* a link that never terminates */
        do_file(OS88_FDLG_OPEN, "JUNK.BAS", h_file_n);
        if (strcmp(a2_msg, "Not an Applesoft program.") != 0)
            fail("a file that fails the walk was not refused by name");
        for (k = 0; k < 8; k++)
            if (a2_rd(0x0801 + (unsigned)k) != 0x5A)
                fail("a REFUSED program was written into the machine anyway - "
                     "nothing may move before the walk passes (section 12)");
        h_file_short = 1;
        do_file(OS88_FDLG_OPEN, "SHORT.BAS", h_file_n);
        if (strcmp(a2_msg, "Cannot read the file.") != 0)
            fail("a short read was not said");
        h_file_short = 0;
        h_claim_refuse = 1;
        do_file(OS88_FDLG_OPEN, "JUNK.BAS", h_file_n);
        if (strcmp(a2_msg, "No heap for the program.") != 0)
            fail("a Load with no heap for the file said nothing");
        h_dlg_up = 1;
        do_cmd(A2_M_FILE, A2_I_LOAD);
        if (strcmp(a2_msg, "A file dialog is open.") != 0)
            fail("a picker refused because another dialog owns the screen was "
                 "a silent no-op (SPEC.md 47)");
        h_dlg_up = 0;

        /* --- THE ASSOCIATION ARM ENDS AT THE SAME CEILING AS THE DIALOG'S -
         * A `.BAS` OPENED BY DOUBLE-CLICK ARRIVES WITH NO SIZE (section 12.1),
         * and the claim used to be a flat 47 KB = 48,128 bytes - 1,025 ABOVE
         * A2_PRGMAX, whose own comment said 47,615 when $C000-$0801 is 47,103.
         * The file below is 47,104 bytes and is a WELL-FORMED chain: one line
         * whose $00 lands at 47,101, then the $0000 terminator. Every link it
         * makes is under $C000, so the walk PASSED - and `plen` came out
         * 47,104, so a2_zzcopy_in wrote one byte at Apple $C000, outside the
         * 48K the a2_wr fence protects, and TXTTAB..PRGEND were set to $C001,
         * above MEMSIZ. The status row said `Loaded` for a program the machine
         * cannot RUN. With `cap` clamped to A2_PRGMAX the file is one byte
         * larger than the claim, so THE KERNEL REFUSES THE READ ITSELF -
         * kernel/diskw.inc:1836-1845 answers FERR_BIG off the directory
         * entry's size, before any data I/O and with the destination
         * untouched - and the arm that reads os88_ferr() names the ceiling
         * that bound it. This block used to say `the read stops one byte
         * short of the terminator, the walk refuses`, which is a truncating
         * read no os8088 kernel performs: it passed only because THE HOST
         * STUB truncated, which is LESSONS.md 7 with the sign flipped. The
         * stub refuses the way the kernel does now, and this is the gate that
         * proves the program refuses on the same file either way. */
        memset(h_file, 0x41, 47104);
        h_file[0] = 0x00;                   /* the link is repaired anyway */
        h_file[1] = 0x08;
        h_file[2] = 0x0A;                   /* line 10 */
        h_file[3] = 0x00;
        h_file[47101] = 0x00;               /* ...the line's own terminator */
        h_file[47102] = 0x00;               /* ...and the program's */
        h_file[47103] = 0x00;
        h_file_n = 47104;
        a2_wr(0x0801, 0x5A);
        do_file(OS88_FDLG_OPEN, "BIG.BAS", 0);   /* 0 = the ASSOCIATION arm */
        if (strcmp(a2_msg, "Too large for a 48K Apple.") != 0)
            fail("a 47,104-byte .BAS opened by double-click was not refused "
                 "at A2_PRGMAX - the association arm's claim is above the "
                 "ceiling the dialog arm refuses on (section 12)");
        if (a2_rd(0x0801) != 0x5A)
            fail("a program refused for size was written into the machine "
                 "anyway");
        if (a2_rd(0x0067) != 0x01 || a2_rd(0x0068) != 0x08
            || (unsigned)a2_rd(0x0069) + ((unsigned)a2_rd(0x006A) << 8)
               > 0xC000u)
            fail("a program refused for size moved TXTTAB or VARTAB");
        if (h_claims_out() != n)
            fail("a program refused for size leaked its claim");

        /* ...AND THE CLAIM STEPS DOWN RATHER THAN REFUSING (section 12).
         * The association arm asked for the whole ceiling whatever the file
         * was, so this wave's own done_when document - a 45-byte WORK.BAS -
         * asked a busy 640KB desktop for a 46 KB PINNED claim on top of this
         * package's already-pinned 64 KB and was told `No heap for the
         * program.` about one cluster. h_claim_refuse turns the FIRST claim
         * down; the loop halves and the listing loads. */
        memcpy(h_file, prog, sizeof(prog));
        h_file_n = sizeof(prog);
        h_claim_refuse = 1;
        do_file(OS88_FDLG_OPEN, "WORK.BAS", 0);
        if (strcmp(a2_msg, "Loaded WORK.BAS") != 0)
            fail("an association load whose FIRST claim was refused gave up "
                 "instead of asking for less (section 12)");
        if (h_claims_out() != n)
            fail("a stepped-down association load leaked its claim");

        /* ...AND THE OTHER CEILING, WHICH IS THE HEAP AND NOT THE MACHINE.
         * The same 47,104-byte file with a heap that can spare 8 KB: the
         * claim steps to 8,192, the kernel refuses the read with FERR_BIG
         * again, and the sentence changes because what bound it changed.
         * `Too large for free memory.` had no gate at all before this - it
         * was reached, in the version this replaces, only through an arm the
         * machine could never take. */
        memset(h_file, 0x41, 47104);
        h_file[0] = 0x00;
        h_file[1] = 0x08;
        h_file[2] = 0x0A;
        h_file[3] = 0x00;
        h_file[47101] = 0x00;
        h_file[47102] = 0x00;
        h_file[47103] = 0x00;
        h_file_n = 47104;
        h_largest_kb = 8;
        do_file(OS88_FDLG_OPEN, "BIG.BAS", 0);
        h_largest_kb = 200;
        if (strcmp(a2_msg, "Too large for free memory.") != 0)
            fail("a .BAS larger than the claim a BUSY heap could give was "
                 "not told which ceiling bound it - the machine's 48K and "
                 "the desktop's free memory are two different things to be "
                 "told (section 12)");
        if (h_claims_out() != n)
            fail("a load refused by the heap ceiling leaked its claim");

        /* --- THE 2-BYTE LENGTH PREFIX IS A HINT, AND A HINT CAN BE WRONG --
         * The test is `word 0 == filesize - 2`, and a HEADERLESS file trips it
         * whenever its first line's link happens to equal that - which this
         * port's OWN Save can write, because Save is headerless. Such a file
         * loads and RUNs on a real Apple II and used to be refused here as
         * `Not an Applesoft program.`
         *
         * The walk runs at the hinted base, fails, and RE-RUNS AT ZERO, which
         * is only possible because the validating pass writes nothing: a
         * repairing walk at base 2 overwrites the bytes a walk at base 0 reads
         * as the first line's NUMBER. */
        memcpy(h_file, prog, sizeof(prog));
        h_file[0] = (unsigned char)(sizeof(prog) - 2);
        h_file[1] = 0x00;                   /* word 0 == filesize - 2, BY
                                             * ACCIDENT - the link is repaired
                                             * from the line lengths anyway */
        h_file_n = sizeof(prog);
        a2_wr(0x0801, 0x5A);
        do_file(OS88_FDLG_OPEN, "HINT.BAS", h_file_n);
        if (strcmp(a2_msg, "Loaded HINT.BAS") != 0)
            fail("a headerless program whose first word happens to equal "
                 "filesize - 2 was refused - the prefix is a HINT and the "
                 "walk has to be able to fall back to base 0 (section 12)");
        vartab = (unsigned)a2_rd(0x0069)
               | ((unsigned)a2_rd(0x006A) << 8);
        if (vartab != 0x0801 + (unsigned)sizeof(prog))
            fail("the fallback base loaded the wrong length");
        if (a2_rd(0x0801) != 0x0B || a2_rd(0x0802) != 0x08)
            fail("the fallback base did not repair the first link - the "
                 "validating pass must write NOTHING, or the retry reads the "
                 "bytes the first pass clobbered (section 12)");

        /* --- A LOAD MARKS THE ROWS IT REACHED, AND NO OTHERS (section 7.5) -
         * a2_zzcopy_in goes round the core's own write path, so the mark is
         * made by hand - and it was a2_dirty_all(), which marked all 192 scan
         * lines and widened all 24 rows for a 28-byte listing NOT ONE
         * DISPLAYED BYTE OF WHICH HAD MOVED. That is ~301 ms of compose on
         * the target to produce the identical pixels, and it happened at
         * LAUNCH too: a cold double-click of a `.BAS` runs the same loader.
         * The machine is halted for this so the only thing that can mark the
         * screen is the load. */
        h_halt();
        a2_io_rd(0xC051);                   /* TEXT... */
        a2_io_rd(0xC054);                   /* ...PAGE 1: $0400-$07FF, and
                                             * $0801 is nowhere near it */
        do_wake();
        do_wake();
        if (a2_dirty_any)
            fail("the fixture did not reach a clean glass before the load");
        memcpy(h_file, prog, sizeof(prog));
        h_file_n = sizeof(prog);
        /* **THE FLUSH IS HELD OFF WHILE THE LOAD RUNS**, and it has to be:
         * the file latch is spent at the TOP of the wake and the flush is at
         * the BOTTOM of that same wake, so a wake that drew would clear the
         * very marks this assertion is about and the gate would pass on any
         * loader at all. h_clip_refuse is `not one pixel of us shows`, which
         * is the one thing that skips the flush and leaves every flag
         * standing. (Before the load ran in the wake at all, the marks
         * survived by accident - os88_onfile did the work under the lock and
         * no wake had run yet.) */
        h_clip_refuse = 1;
        do_file(OS88_FDLG_OPEN, "WORK.BAS", h_file_n);
        h_clip_refuse = 0;
        /* THE QUESTION IS THE MARKS AND NOT a2_dirty_any, and the difference
         * only appeared once the load ran in the WAKE. a2_dirty_any is the
         * coarse "the machine wrote something" flag, and the loader's five
         * ovl_a2_wr16 calls go through a2_wr - the core's own write path - to
         * set TXTTAB..PRGEND in ZERO PAGE, so it is legitimately 1 and
         * a2_dirty_scan is what would find that none of it is displayed. What
         * this gate is about is the by-hand marks a2_zzcopy_in owes, which
         * were a2_dirty_all()'s 192 lines and are ovl_a2_dirty_range's none. */
        ok = 1;
        for (k = 0; k < A2_SCRH; k++)
            if (a2_line_is(a2_lnd, k))
                ok = 0;
        if (!ok)
            fail("a load that changed no DISPLAYED byte marked a scan line - "
                 "the whole page would be recomposed to produce the identical "
                 "pixels (section 7.5)");
        a2_io_rd(0xC055);                   /* PAGE 2: $0800-$0BFF, which
                                             * $0801 IS inside */
        do_wake();
        do_wake();
        if (a2_dirty_any)
            fail("the page switch did not settle");
        h_clip_refuse = 1;
        do_file(OS88_FDLG_OPEN, "WORK.BAS", h_file_n);
        h_clip_refuse = 0;
        if (!a2_line_is(a2_lnd, 0))
            fail("a load INTO the displayed page marked nothing - row 0 of "
                 "text page 2 is $0800-$0827 and the program starts at $0801");
        if (a2_line_is(a2_lnd, 8))
            fail("a load marked row 1, which is $0880-$08A7 and 96 bytes "
                 "past the end of a 28-byte program (section 7.5)");
        a2_io_rd(0xC054);
        do_wake();
        do_wake();
        h_go();

        /* --- CPU > STOP / CONTINUE, AND WARP ----------------------------- */
        do_cmd(A2_M_CPU, A2_I_STOP);
        if (!a2_pause)
            fail("CPU > Stop left the machine running");
        do_wake();
        do_wake();
        if (a2_wants_wake())
            fail("a STOPPED machine with nothing to draw still asks for "
                 "wakes - c64_wants_wake's `THE PAUSE IS THE HALF THAT WAS "
                 "MISSING`, one machine along");
        if (strcmp(a2_cpu_items[A2_I_STOP], "* Stopped") != 0
            || strcmp(a2_cpu_items[A2_I_RUN], "  Continue") != 0)
            fail("CPU > Stop did not retitle the pair Stopped / Continue "
                 "(MII's PREPARE arm, section 10.1)");
        do_cmd(A2_M_CPU, A2_I_RUN);
        if (a2_pause)
            fail("CPU > Continue did not restart the machine");
        if (strcmp(a2_cpu_items[A2_I_STOP], "  Stop") != 0
            || strcmp(a2_cpu_items[A2_I_RUN], "* Running") != 0)
            fail("CPU > Continue did not retitle the pair Stop / Running");
        do_cmd(A2_M_CPU, A2_I_WARP);
        if (!a2_warp || strcmp(a2_cpu_items[A2_I_WARP], "* Warp") != 0)
            fail("CPU > Warp is not a check item");
        a2_budget = A2_SLICE_WARP;
        do_cmd(A2_M_CPU, A2_I_WARP);
        if (a2_warp || a2_budget > A2_SLICE_MAX)
            fail("warp off left the budget above the ceiling it was granted "
                 "for");

        /* --- THE SPEAKER: A TOGGLE TRAIN BECOMES A NOTE (section 8) -------
         *
         * NONE OF THIS IS VISIBLE IN A SCREENDUMP and none of it is visible
         * to `make test-snd` either beyond "a tone of about the right pitch
         * came out": what is asserted here is the ARITHMETIC - that
         * 1,020,484 / (2 x delta) is one rounding and not two, that the
         * agreement window rejects a train that is not steady, that the
         * silence timeout takes a duration-0 grant down, and that a REFUSED
         * grant is retried a bounded number of times and then dropped with
         * the fact said once. The C64 shipped three of those four wrong.
         *
         * EVERY WAKE HERE HAS A TRAIN IN FRONT OF IT, which is spk_train's
         * own note: the silence timeout is two ticks and a wake is a tick, so
         * a script that toggles once and wakes four times measures the
         * timeout and nothing else. */
        {
            int k;

            do_cmd(A2_M_CPU, A2_I_RUN);     /* running, un-warped, un-muted */
            a2_mute = 0;
            h_snd_refuse = 0;
            h_snd_hz = -1;
            h_snd_asked = -1;

            /* 1 kHz: 510,242 / 1,000 = 510 cycles a half-period, and the
             * estimator's answer is 510,242 / 510 = 1,000 exactly. */
            spk_train(510, A2_SPK_N + 1);
            do_wake();
            if (h_snd_hz != 1000)
                printf("a2uitest: FAIL - a 510-cycle toggle train played %d Hz "
                       "and the arithmetic says 1000 (section 8)\n",
                       h_snd_hz), fails++;
            if (!a2_snd_said)
                fail("the speaker's stated fact was never said - what the "
                     "estimator cannot reproduce is a fact about the build "
                     "and belongs on the row (section 8)");

            /* ...AND ONE FAR CALL ON A CHANGE ONLY. A tone that has not moved
             * must not be re-granted every wake: it is 46.7 us of far call
             * plus the router's work for a note that is already sounding. */
            k = h_snd_calls;
            spk_train(510, A2_SPK_N);
            do_wake();
            if (h_snd_calls != k)
                fail("a steady tone asked the kernel for the note it was "
                     "already playing (section 8)");

            /* --- A WOBBLING ESTIMATE IS ONE NOTE, NOT A RE-GRANT A WAKE --
             * A MEASURED frequency is not a register: the C64's `one far call
             * a wake, on a change only` is exact for a SID write and is not
             * exact for an interval that moves by a cycle. Every step of that
             * walk would be a far call at 46.7 us AND an `out 0x43`, which
             * restarts PIT channel 2's count in the middle of a note nobody
             * asked to change - and a wake here is not 18 Hz, it is however
             * often a2_wants_wake re-posts one.
             *
             * The train below moves by ONE cycle a toggle and the kernel must
             * be asked ONCE. */
            k = h_snd_calls;
            for (i = 0; i < 6; i++) {
                spk_train(510 + (i & 1) - 1, 2);   /* 509, 510, 509, ... */
                do_wake();
            }
            if (h_snd_calls != k)
                printf("a2uitest: FAIL - a toggle train whose interval moved "
                       "by ONE cycle re-granted the tone %d time(s); every "
                       "one is a far call and an `out 0x43` that restarts the "
                       "PIT mid-note (section 8)\n",
                       h_snd_calls - k),
                fails++;
            /* ...AND A REAL CHANGE STILL GETS THROUGH. A sixty-fourth is the
             * band - 0.27 of a semitone, under a fifth of what anyone can
             * hear - and 510 -> 340 cycles is 1,000 -> 1,500 Hz. */
            spk_train(340, A2_SPK_N + 1);
            do_wake();
            if (h_snd_hz != 1500)
                printf("a2uitest: FAIL - a toggle train that moved from 1,000 "
                       "to 1,500 Hz played %d - the hysteresis band swallowed "
                       "a real change (section 8)\n", h_snd_hz),
                fails++;
            spk_train(510, A2_SPK_N + 1);
            do_wake();

            /* THE SILENCE TIMEOUT. Duration 0 means SOMETHING has to take it
             * down, and nothing else will: the guest has stopped toggling and
             * the note would sound for the rest of the session, on a desktop
             * the user has gone back to. */
            the_ticks += A2_SPK_QUIET;
            do_wake();
            if (h_snd_asked != 0)
                fail("a toggle train that stopped left its duration-0 note "
                     "sounding for ever (section 8, rule 3)");

            /* A TRAIN THAT IS NOT STEADY IS NOT A NOTE. Alternating 510 and
             * 900 cycles is a different hertz every toggle and none of them
             * is what the program meant; the agreement window is what makes
             * the answer silence rather than a stream of wrong notes. */
            for (k = 0; k < 8; k++)
                spk_train((k & 1) ? 900 : 510, 1);
            do_wake();
            if (h_snd_asked != 0)
                fail("an unsteady toggle train played a note - the last "
                     "A2_SPK_N intervals have to AGREE (section 8)");

            /* ...AND AN INTERVAL OUTSIDE THE SINK'S BAND IS NOT CLAMPED INTO
             * IT. 40,000 cycles is 12.8 Hz, below the 20 Hz floor; clamping
             * would answer 20 Hz for a machine that had simply stopped. */
            spk_train(26000, A2_SPK_N + 1);     /* 26,000 cycles is 19.6 Hz,
                                                 * one step under the floor -
                                                 * and A2_SPK_DMAX is 25,512,
                                                 * so the TOGGLE handler
                                                 * rejects it and the run
                                                 * never fills */
            do_wake();
            if (h_snd_asked != 0)
                fail("an interval below the sink's 20 Hz floor was clamped "
                     "into the band instead of being refused (section 8)");

            /* A REFUSED GRANT IS RETRIED, AND THE RETRY IS BOUNDED. The C64
             * cleared its latch BEFORE the call and threw the answer away, so
             * one refusal silenced the machine for the session; and an
             * unbounded retry asks once a wake for ever on a speaker somebody
             * else holds for good. */
            a2_sound_stop();
            h_snd_refuse = 3;
            h_snd_hz = -1;
            h_snd_asked = -1;
            spk_train(510, A2_SPK_N + 1);
            do_wake();
            if (h_snd_asked != 1000 || h_snd_hz == 1000)
                fail("the first grant after a refusal was not even attempted");
            for (k = 0; k < 3; k++) {
                spk_train(510, 2);
                do_wake();
            }
            if (h_snd_hz != 1000)
                fail("a REFUSED tone grant was never retried - one refusal "
                     "silenced the machine for the session (section 8, "
                     "rule 4)");

            /* ...AND A SPEAKER HELD FOR GOOD IS DROPPED. 700 cycles is 728 Hz
             * and is a NEW note, so the bound starts again from there. */
            h_snd_refuse = 64;
            a2_sound_stop();
            for (k = 0; k < A2_SND_TRIES + 4; k++) {
                spk_train(700, 2);
                do_wake();
            }
            k = h_snd_calls;
            spk_train(700, 2);
            do_wake();
            spk_train(700, 2);
            do_wake();
            if (h_snd_calls != k)
                fail("a speaker held for good is asked once a wake for ever "
                     "- the retry is bounded at A2_SND_TRIES wakes and then "
                     "dropped (section 8, rule 4)");
            if (!a2_snd_busy_said)
                fail("the speaker being busy was never said - a dropped "
                     "grant is a fact and SPEC.md 47 asks for it once");
            h_snd_refuse = 0;

            /* --- THE STATED FACT IS ARMED BY THE FAILURE TOO --------------
             * It used to latch only inside the successful os88_snd_tone arm,
             * so it fired exactly when the emulation was WORKING and never
             * when it was not - and every program the sentence exists for
             * produces intervals that do not agree, so a2_spk_hz answers 0,
             * no tone is asked for, and the row stayed blank for the one user
             * who needed it. Four consecutive wakes of a speaker that TOGGLED
             * and could not be followed say it (section 8). */
            a2_sound_stop();
            a2_snd_said = 0;
            a2_msg[0] = 0;
            h_snd_asked = -1;
            for (k = 0; k < A2_SND_ODD - 1; k++) {
                spk_noise(A2_SPK_N + 1);
                do_wake();
            }
            if (a2_snd_said)
                fail("the speaker's stated fact was said before A2_SND_ODD "
                     "wakes of a speaker this build could not follow");
            spk_noise(A2_SPK_N + 1);
            do_wake();
            if (!a2_snd_said)
                fail("a TOGGLING speaker this estimator could not turn into a "
                     "tone never said `Square-wave tones only.` - the fact "
                     "fired only when the emulation was working, which is the "
                     "opposite of what section 8 asks for");
            if (strcmp(a2_msg, "Square-wave tones only.") != 0)
                fail("the fact armed by the failure is not section 8's "
                     "sentence");
            if (h_snd_asked != -1)
                fail("a speaker whose intervals do not agree was turned into "
                     "a tone anyway");

            /* MACHINE > MUTE, AND WARP. Both silence it, and warp is VICE's
             * own rule (vsync.c:181's sound_suspend): a machine at some
             * thousands of per cent has nothing meaningful to play. */
            a2_sound_stop();
            h_snd_asked = -1;
            h_snd_hz = -1;
            spk_train(510, A2_SPK_N + 1);
            do_wake();
            if (h_snd_hz != 1000)
                fail("the machine went quiet for good after a dropped grant");
            do_cmd(A2_M_MACHINE, A2_I_MUTE);
            if (!a2_mute || strcmp(a2_mach_items[A2_I_MUTE], "* Mute") != 0)
                fail("Machine > Mute is not a check item (section 10.1)");
            if (h_snd_asked != 0)
                fail("Machine > Mute did not take the note down ON THE PICK");
            do_cmd(A2_M_MACHINE, A2_I_MUTE);
            if (a2_mute || strcmp(a2_mach_items[A2_I_MUTE], "  Mute") != 0)
                fail("Machine > Mute did not un-mute");
            h_snd_hz = -1;
            spk_train(510, A2_SPK_N + 1);
            do_wake();
            if (h_snd_hz != 1000)
                fail("un-muting did not let the machine play again");
            do_cmd(A2_M_CPU, A2_I_WARP);
            if (h_snd_asked != 0)
                fail("CPU > Warp did not silence the speaker (VICE's rule, "
                     "section 8)");
            do_cmd(A2_M_CPU, A2_I_WARP);

            /* A MACHINE WITH NO SQUARE VOICE IS A DIFFERENT FACT, and it is
             * not retried at all: the capability is asked ONCE in os88_main
             * and Machine > Mute is greyed off it. The flag is poked here
             * because os88_main decides it once a launch. */
            a2_have_snd = 0;
            a2_sound_stop();
            a2_menu_state();
            if (strcmp(a2_mach_items[A2_I_MUTE], "\001  Mute") != 0)
                fail("a machine with no square voice does not grey Machine > "
                     "Mute with the fact (section 10.3)");
            k = h_snd_calls;
            spk_train(510, A2_SPK_N + 1);
            do_wake();
            if (h_snd_calls != k)
                fail("a machine with no square voice called the tone slot "
                     "anyway - a package that calls a slot without "
                     "establishing the capability is guessing (73.11)");
            a2_have_snd = 1;
            a2_menu_state();
            a2_sound_stop();
        }

        /* --- THE FOREIGN VIDEO MODE (section 13) -------------------------
         *
         * WHAT IS ASSERTED HERE IS THE DRIVE AND THE FENCE, not the pixels.
         * Whether MII's artifact rule reaches the glass is a SCREENDUMP
         * INSIDE FSXM_VGA13 and cannot be anything else (a2fsx.inc's own
         * note: this file transcribes the rule, so a transcription that
         * agrees proves the rule and not the flattening). What this file CAN
         * see, and what an emulator never could:
         *
         *   - a foreign frame is driven off the SAME dirty-line set as the
         *     windowed flush, so the FIRST frame writes 192 lines and every
         *     frame after it that changes nothing writes NONE. A per-frame
         *     raster write is 3,365 ms of a 4.77 MHz 8088 (section 13.2) and
         *     looks identical on the glass;
         *   - NO DRAWING SLOT is called between the mode set and the return
         *     (rule 2 of a2_fsx_main's list), which is the rule nothing in
         *     the toolchain checks;
         *   - the 53KB shadow claim is taken at the LATCH and FREED, and a
         *     refusal there is legal and says so;
         *   - the menu row greys on a display with no foreign colour mode,
         *     off the same bit os88_fsx_mode would refuse on. */
        {
            int fb0, colour, lit, k;

            h_mode(0, 0, 0, 1);             /* HI-RES, which is the mode with
                                             * artifact colour in it */
            h_hires();
            do_wake();
            do_paint();

            /* THE ROW IS LIVE ON A VGA... */
            a2_menu_state();
            if (strcmp(a2_mach_items[A2_I_TINT], "  Color NTSC") != 0)
                fail("Machine > Color NTSC is greyed on a display whose "
                     "os88_fsx_caps offers FSXM_VGA13 (section 13.1)");

            n_fsxput = 0;
            n_fsxsame = 0;
            n_fsxbytes = 0;
            h_fsx_runs = 0;
            h_fsx_modes = 0;
            fb0 = h_claim_n;
            cost_mark();
            c = n_blit + n_fill + n_run + n_scroll + n_clip + n_toast;
            (void)c;
            h_fsx_keyn = 3;                 /* two frames, then Ctrl+F */
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = A2H_NOKEY;
            h_fsx_keyq[2] = 6;              /* ASCII 6 - Ctrl+F, one of this
                                             * port's two chords (rule 7) */
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_fsx_runs != 1 || h_fsx_modes != 1)
                fail("Machine > Color NTSC did not enter the bracket and set "
                     "a mode exactly once");
            if (h_claim_n != fb0 + 1)
                fail("the foreign-frame shadow was not claimed at the LATCH "
                     "(section 13.2)");
            if (h_claims_out() != 0)
                fail("the foreign-frame shadow was not freed when the "
                     "bracket returned - 53KB of the heap, per entry");
            if (h_fsx_in)
                fail("the bracket did not return on Ctrl+F");
            /* RULE 2: NOT ONE DRAWING SLOT between the mode set and the
             * return. They render DESKTOP geometry into a foreign
             * framebuffer, and what that looks like is not a refusal - it is
             * the desktop's chrome drawn into the middle of the Apple's
             * raster. */
            if (h_fsx_drawn != 0)
                fail("a DRAWING SLOT was called inside the exclusive bracket "
                     "- every one of them renders desktop geometry into a "
                     "foreign framebuffer (SPEC.md 53.7, rule 2)");
            /* THE DRIVE. 192 lines on the first frame because the shadow is a
             * fresh claim and the frame is declared unknown; NONE on the
             * second, because nothing changed and the dirty-line set says so.
             * A per-frame raster write would be 384. */
            if (n_fsxput != A2_SCRH)
                printf("a2uitest: FAIL - the first foreign frame wrote %d "
                       "lines and the raster is %d (section 13.2)\n",
                       n_fsxput, A2_SCRH),
                fails++;
            if (n_fsxsame != 0)
                printf("a2uitest: FAIL - the first foreign frame COMPARED %d "
                       "lines it had never written; the frame is unknown at "
                       "the latch and every line is owed\n", n_fsxsame),
                fails++;
            /* ...AND THE PICTURE IS IN THE FRAME, at the offset section 13.1
             * pins: 280 x 192 centred at (20, 4) of a 320 x 200 mode. */
            colour = 0;
            lit = 0;
            for (i = 0; i < A2_SCRH; i++) {
                int x;

                for (x = 0; x < A2_FSXW; x++) {
                    k = h_fsx_fb[(4 + i) * 320 + 20 + x];
                    if (k != 0)
                        lit++;              /* ...and every non-black one, for
                                             * the second-entry row below */
                    if (k != 0 && k != 5)
                        colour++;
                }
            }
            if (colour == 0)
                fail("a hi-res screen in FSXM_VGA13 put not one coloured "
                     "pixel in the frame - artifact colour is the whole "
                     "reason the foreign mode exists (section 13.1)");
            for (i = 0; i < 200; i++)
                for (k = 0; k < 20; k++)
                    if (h_fsx_fb[i * 320 + k] != 0) {
                        fail("the foreign frame wrote outside the Apple's "
                             "280 x 192 raster - the letterbox is the mode "
                             "set's own clear and nothing else");
                        i = 200;
                        break;
                    }
            cost_row("colour: first frame + SPEC 53.6 step 4's repaint");

            /* --- A SECOND ENTRY ON THE SAME HEAP BLOCK DRAWS THE WHOLE
             * PICTURE, AND FOR A WAVE IT DREW A THIRD OF IT ---------------
             * The 53KB shadow is freed at the exit and claimed again at the
             * next latch, and a free followed by a same-size claim lands on
             * the same block - so the shadow arrives holding the LAST
             * session's frame. `a2_fsx_ok = 0` defeats the per-LINE skip and
             * has no idea a2_fsx_put's per-BYTE compare is about to answer
             * "nothing moved" for every line the picture still agrees with,
             * against a framebuffer SPEC.md 53.4 has just cleared. Found on
             * the glass: entering Machine > Color NTSC a second time on an
             * unchanged hi-res screen drew three of its six lines.
             *
             * a2_fsx_main zeroes the shadow at entry now, which makes it TRUE
             * rather than unknown - the mode set's clear is what it describes.
             * The count of coloured pixels is the assertion, because it is the
             * one thing a compare that wrongly answers EQUAL takes away. */
            h_claim_keep = 1;
            h_fsx_keyn = 2;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            h_claim_keep = 0;
            k = 0;
            for (i = 0; i < A2_SCRH; i++) {
                int x;

                for (x = 0; x < A2_FSXW; x++)
                    if (h_fsx_fb[(4 + i) * 320 + 20 + x] != 0)
                        k++;
            }
            if (k != lit)
                printf("a2uitest: FAIL - a second fullscreen-colour session "
                       "on the same heap block put %d lit pixels in the frame "
                       "where the first put %d. The shadow arrived holding "
                       "the last session's picture and a2_fsx_put answered "
                       "'nothing moved' against a framebuffer the mode set "
                       "had just cleared (section 13.2)\n", k, lit),
                fails++;

            /* --- A SECOND ENTRY WITH NOTHING CHANGED WRITES NOTHING -------
             * The shadow is a FRESH claim each time, so the first frame of
             * the second session writes 192 lines again - which is correct
             * and is the price of not holding 53KB across a session. What is
             * asserted is the frame AFTER it. */
            n_fsxput = 0;
            n_fsxsame = 0;
            h_fsx_keyn = 4;                 /* three frames, then the chord */
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = A2H_NOKEY;
            h_fsx_keyq[2] = A2H_NOKEY;
            h_fsx_keyq[3] = 0x1C00;         /* Alt+Enter, the other chord:
                                             * scan 0x1C, ascii 0 */
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_fsx_in)
                fail("the bracket did not return on Alt+Enter - BOTH chords "
                     "are taken rather than one being guessed (rule 7)");
            if (n_fsxput != A2_SCRH)
                printf("a2uitest: FAIL - three foreign frames with nothing "
                       "changing wrote %d lines; the first owes %d and the "
                       "other two owe NONE. A per-frame raster write is what "
                       "section 13.2 exists to refuse\n",
                       n_fsxput, A2_SCRH),
                fails++;

            /* --- AND ALT+ENTER IN THE *ENHANCED* SET, WHICH IS SCAN 0xA6 --
             * THE ROW THAT WOULD NOT HAVE COMPILED THE DEFECT AWAY. The
             * bracket packed int 16h's AX into a 16-bit `int` and tested
             * `k >= 0`, so every key whose SCAN CODE has bit 7 set was thrown
             * away before it was looked at: AH=0xA6 is k = -22528, and the
             * KSC_ALT_ENTER arm four lines below the test was dead code while
             * its own comment said both codes were taken rather than guessed.
             * Alt+0/-/= (AH 0x81/0x82/0x83) went the same way.
             *
             * THIS FILE COULD NOT SEE IT AND STILL CANNOT SEE IT BY ITSELF: a
             * host `int` is 32 bits, so 0xA600 is positive here whatever the
             * target does. What makes the row real is the pair - the sentinel
             * is 0xFFFF on BOTH sides now and a2_fsx_key answers `unsigned`
             * on both - so a signed test on the target cannot pass this queue
             * and a signed test here cannot pass it either. The old queue
             * only ever held 0x1C00, which is positive in sixteen bits, so
             * every existing row above ran on the ONE of the two codes that
             * worked. */
            h_fsx_keyn = 2;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = 0xA600;         /* Alt+Enter, ENHANCED: scan 0xA6,
                                             * ascii 0 */
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_fsx_in)
                fail("the bracket did not return on Alt+Enter in the "
                     "ENHANCED set (scan 0xA6) - a scan code with bit 7 set "
                     "is a NEGATIVE 16-bit int, and the key test has to be "
                     "unsigned against 0xFFFF (rule 7, section 13.3)");

            /* --- THE WINDOWED SHADOW IS INVALIDATED ON THE WAY *IN*, AND
             * THE EXIT COSTS EXACTLY ONE REPAINT ---------------------------
             * The bracket consumes the marks the windowed flush would have
             * used and a2_sh describes pixels that have been off the glass
             * for a whole session, so a2_sh_ok has to go - but SPEC.md 53.6
             * step 4 runs a full wm_paint_all BEFORE fsx_run returns, and
             * os88_paint answers a whole-window W_PAINT with a2_sh_inval()
             * plus one flush. Invalidating AGAIN after that put every line
             * mark back on pixels the kernel's own paint had just made
             * correct, and the next wake composed and blitted all 192 lines a
             * SECOND time: ~633 ms of a 4.77 MHz 8088, the largest double
             * draw this package has, and PERFORMANCE rule 2 at the largest
             * scale it comes at.
             *
             * So the shadow must be VALID here - step 4 has repainted it -
             * and the wake that follows must draw NOTHING. */
            if (!a2_sh_ok)
                fail("the exit invalidated the windowed shadow AFTER "
                     "SPEC.md 53.6 step 4's own wm_paint_all - the next wake "
                     "then composes and blits all 192 lines a second time "
                     "(section 13.3)");
            h_halt();
            c = n_blit + n_band + n_band_h + n_band_l;
            do_wake();
            if (n_blit + n_band + n_band_h + n_band_l != c)
                fail("the wake after a fullscreen-colour session composed or "
                     "blitted - step 4's repaint already drew the window and "
                     "this is the second one");
            h_go();
            do_wake();
            do_paint();
            audit("after a fullscreen-colour session");

            /* ================================================================
             * RULE 2'S FENCE, ASSERTED RATHER THAN DESCRIBED
             * ==============================================================
             * a2_fsx_main's rule list said "`a2_fsx_up` is what fences the
             * one path that could, a2_jam's os88_toast, which is reachable
             * from the slice" - and for a wave nothing read the flag. This is
             * the path: a2_fsx_main -> a2_slice -> A2_RUN_JAM -> a2_jam ->
             * os88_toast, and kernel/toast.inc's toast_show ends in
             * toast_now, whose predicate (the gfx lock held by task 0) is
             * EXACTLY the bracket's state - so the desktop's menu bar and
             * panel go down immediately, in 640-wide planar geometry, into an
             * A000 the card has just put into chained mode 13h at a stride of
             * 320. The foreign shadow then believes those bytes are ours, so
             * it is permanent for the rest of the session. */
            n_toast = 0;
            h_run_jam = 1;                  /* the first slice in there JAMs */
            h_fsx_keyn = 2;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (a2_state != A2_ST_JAM)
                fail("the core did not JAM inside the bracket - the fence "
                     "below would then be asserting nothing");
            if (n_toast != 0)
                fail("a2_jam raised a TOAST from inside the exclusive "
                     "bracket - os88_toast is a DRAWING SLOT and toast_now "
                     "draws it on the spot when the lock is held by task 0 "
                     "(SPEC.md 53.7, section 13.3 rule 2)");
            if (h_fsx_drawn != 0)
                fail("a JAM inside the bracket reached a drawing slot");
            if (strncmp(a2_jamline, "6502: JAM at $", 14) != 0)
                fail("the JAM line was not left on the status row - the "
                     "fence costs nothing only because the row says it "
                     "anyway when the desktop comes back");
            do_cmd(A2_M_MACHINE, A2_I_RESET);   /* the port's own way out */
            do_wake();
            if (a2_state != A2_ST_RUN)
                fail("Control-Reset did not bring the machine back from the "
                     "JAM this step drove it into");

            /* --- AND THE RESET CHORDS ARE SPENT *INSIDE* THE BRACKET ------
             * a2_key answers Ctrl+F2 and Ctrl+F3 by setting a2_reset_req, and
             * the only other place that is read is os88_onwake - which is an
             * EVENT, and rule 3 says none is dispatched in here. So for a
             * wave the two chords were inert for the whole colour session and
             * then fired the instant the user left, resetting a machine they
             * thought they had reset a minute ago. */
            a2_m.pc = 0x1234;
            h_fsx_keyn = 3;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = 0x5F00;         /* Ctrl+F2, scan 0x5F ascii 0 */
            h_fsx_keyq[1] = A2H_NOKEY;
            h_fsx_keyq[2] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (a2_reset_req != 0)
                fail("Ctrl-Reset typed in a fullscreen-colour session was "
                     "still LATCHED when the bracket returned - it fires on "
                     "the next wake instead, resetting a machine the user "
                     "reset a minute ago (rule 3)");
            if (a2_m.pc == 0x1234)
                fail("Ctrl-Reset inside the bracket never reached the 6502 - "
                     "the reset vector was not taken");
            if (h_fsx_drawn != 0)
                fail("a reset inside the bracket reached a drawing slot");
            do_wake();
            do_paint();

            /* ================================================================
             * THE FLASH FLAGS ARE MAINTAINED INSIDE THE BRACKET
             * ==============================================================
             * a2_flrow[] is written at COMPOSE time, and a2_flush is the only
             * other place that composes - so for a wave the whole colour
             * session ran on flags taken before it was entered, and
             * a2_flash_step forced eight lines 3.64 times a second off them.
             * Three consequences, none visible in an emulator: entering from
             * HI-RES left every flag zero, so a program returning to TEXT had
             * a `]` that never blinked again; entering from TEXT and
             * scrolling left the flag on a row the cursor had moved off; and
             * entering from TEXT and running HGR recomposed eight stale text
             * rows as hi-res on every flip - ~480 ms of every second of a
             * 4.77 MHz 8088 producing no pixel.
             *
             * The flags are ZEROED by hand first, so nothing but the foreign
             * frame can be what set them. */
            h_mode(1, 0, 0, 0);             /* TEXT */
            h_puts(8, 2, "FLASHING", 2);    /* $40-$7F: the flashing form */
            h_puts(9, 2, "STEADY", 0);
            do_wake();
            do_paint();
            for (i = 0; i < A2_ROWS; i++)
                a2_flrow[i] = 0;
            a2_dirty_all();
            h_fsx_keyn = 2;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (!a2_flrow[8])
                fail("a foreign frame composed a TEXT row holding a $40-$7F "
                     "byte and did not record its flash flag - the cursor "
                     "stops blinking for the length of the session "
                     "(section 13.2)");
            if (a2_flrow[9])
                fail("a foreign frame flagged a text row with nothing "
                     "flashing in it - a2_flash_force then forces eight lines "
                     "3.64 times a second on a row that cannot flash");

            /* ...AND A GRAPHICS ROW CLEARS ITS STALE FLAG. Only a TEXT row
             * can flash: a lo-res byte of $60 is two colour blocks and a
             * hi-res byte of $60 is three pixels, and asking a2_rowflash
             * about either would force-compose the row 3.6 times a second for
             * a phase that changes not one pixel of it. */
            for (i = 0; i < A2_ROWS; i++)
                a2_flrow[i] = 1;            /* stale text flags, everywhere */
            h_mode(0, 0, 0, 1);             /* HI-RES */
            h_hires();
            do_wake();
            do_paint();
            h_fsx_keyn = 2;
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            for (i = 0; i < A2_ROWS; i++)
                if (a2_flrow[i])
                    fail("a foreign frame composed a HI-RES row and left a "
                         "stale text flash flag on it - eight text rows are "
                         "then recomposed as hi-res on every phase flip "
                         "(section 13.2)");

            /* ================================================================
             * THE COLUMN SPAN: A ONE-CELL WRITE COMPOSES ONE GROUP
             * ==============================================================
             * a2_dirty_scan fills a2_wlo/a2_whi and a2_rowwide[] for the
             * foreign frame exactly as it does for the windowed flush, and
             * the first version read NONE of it: every row composed all forty
             * cells and every line compared all 280 bytes. On the ordinary
             * change - an Applesoft COUT writing one cell - that is 8 x 15.39
             * ms of compose and 8 x 2.15 of compare where ~16 ms was owed, in
             * the mode whose per-line cost is the highest this port has.
             *
             * The arithmetic is exact and is why this is a count and not a
             * cost row: frame 1 is the fresh claim, so 24 rows x 8 lines x 40
             * cells = 7,680; frame 2 sees one written cell, which is one
             * GROUP of eight cells over that row's eight lines = 64. Without
             * the narrowing frame 2 is 320. */
            h_mode(1, 0, 0, 0);             /* TEXT */
            do_wake();
            do_paint();
            /* THE FLASH PHASE IS PARKED FIRST, so this row measures the
             * WRITE and nothing else. a2_flash_step is polled at the top of
             * every iteration (rule 3), and a flip forces every flashing row
             * WHOLE - which is correct, and is 320 cells a row of arithmetic
             * that has nothing to do with the span being asserted here. The
             * previous fixtures leave two flashing rows on this page, and
             * that is exactly the 640 cells an unparked clock added. */
            a2_fl_tick = os88_ticks();
            n_fsxcell_t = 0;
            n_fsxput = 0;
            n_fsxsame = 0;
            n_fsxcmp_d = 0;
            n_fsxcmp_s = 0;
            cost_mark();
            h_run_wr_in = 2;                /* the SECOND slice writes... */
            h_run_wr_a = a2_tbase[5] + 3;   /* ...one cell of row 5, group 0 */
            h_run_wr_v = 0xC1;              /* 'A', normal form */
            h_fsx_keyn = 3;                 /* two frames, then Ctrl+F */
            h_fsx_keyi = 0;
            h_fsx_keyq[0] = A2H_NOKEY;
            h_fsx_keyq[1] = A2H_NOKEY;
            h_fsx_keyq[2] = 6;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_run_wr_in != 0)
                fail("the fixture write never reached the Apple's memory - "
                     "the step below is asserting nothing");
            if (n_fsxcell_t != (long)A2_ROWS * 8 * A2_COLS + 8 * 8)
                printf("a2uitest: FAIL - a one-cell write inside the bracket "
                       "composed %ld cells; the first frame owes %d and the "
                       "second owes 64, one group over eight lines "
                       "(section 13.2)\n",
                       n_fsxcell_t, A2_ROWS * 8 * A2_COLS),
                fails++;
            if (n_fsxcmp_d + n_fsxcmp_s
                != (long)A2_SCRH * A2_FSXW + 8 * 8 * 7)
                printf("a2uitest: FAIL - the span compare read %ld bytes; "
                       "the first frame owes %d and the second owes 448 - "
                       "a2_fsx_put takes a byte range for the same reason "
                       "the composer takes a cell range\n",
                       n_fsxcmp_d + n_fsxcmp_s, A2_SCRH * A2_FSXW),
                fails++;
            cost_row("colour: one cell written, + that repaint");

            /* --- A REFUSED CLAIM IS LEGAL AT THE LATCH -------------------- */
            h_mode(0, 0, 0, 1);             /* back to HI-RES for the rows
                                             * below, which is where this
                                             * block found the screen */
            h_hires();
            do_wake();
            do_paint();
            h_claim_refuse = 1;
            h_fsx_runs = 0;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_fsx_runs != 0)
                fail("the bracket was entered with no shadow to write into - "
                     "the LATCH may refuse and the FLUSH may not (13.2)");

            /* --- AND SO IS A REFUSED BRACKET ----------------------------- */
            h_fsx_refuse_run = 1;
            fb0 = h_claim_n;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_claims_out() != 0)
                fail("a refused os88_fsx_run leaked the 53KB shadow");
            h_fsx_refuse_run = 0;

            /* --- A DISPLAY WITH NO FOREIGN COLOUR MODE GREYS WITH THE FACT
             * FSXM_CGA640 and FSXM_HERC were CUT on the bench (section 13.4):
             * 695.4 ms a frame against the windowed path's 632.6, and 34.8 ms
             * a character row against 26.4. So a CGA offers this port nothing
             * and the row says so rather than entering a mode that is slower
             * than the window it came from. */
            h_fsx_mask = 0;
            h_fsx_kind = OS88_VID_CGA;
            a2_menu_state();
            if (strcmp(a2_mach_items[A2_I_TINT], "\001  Color NTSC") != 0)
                fail("Machine > Color NTSC is LIVE on a display with no "
                     "foreign colour mode (section 13.1)");
            h_fsx_runs = 0;
            do_cmd(A2_M_MACHINE, A2_I_TINT);
            if (h_fsx_runs != 0)
                fail("Machine > Color NTSC entered a bracket on a display "
                     "whose caps mask has no FSXM_VGA13 bit - the greying and "
                     "the refusal are one predicate (SPEC.md 47)");
            h_fsx_mask = 1 << OS88_FSXM_VGA13;
            h_fsx_kind = OS88_VID_VGA;
            a2_menu_state();
            h_mode(1, 0, 0, 0);             /* back to TEXT, where the rest of
                                             * the script lives */
            do_wake();
            do_paint();
        }

        /* --- MACHINE > POWER ON'S TWO-ROW CONFIRMATION (section 10.2) ----- */
        do_wake();
        do_paint();
        do_cmd(A2_M_MACHINE, A2_I_POWER);
        if (!a2_abt_up || a2_pan_kind != A2_PAN_CFM)
            fail("Machine > Power On did not raise its confirmation");
        if (a2_reset_req != 0)
            fail("Machine > Power On latched the cold boot BEFORE the user "
                 "answered - the confirmation is not decoration");
        if (strcmp(a2_cfm_text[0], "Are you sure you want to reboot?") != 0
            || strcmp(a2_cfm_text[1], "(All data will be lost!)") != 0)
            fail("the confirmation is not AppleWin's two rows "
                 "(WinFrame.cpp:2003-2004)");
        do_click(a2_cfm_bx[1] + 4, a2_cfm_by + 4);      /* No */
        if (a2_abt_up || a2_reset_req != 0)
            fail("No on the reboot confirmation did not dismiss it, or reset "
                 "the machine anyway");
        do_wake();
        audit("after a dismissed reboot confirmation");
        do_cmd(A2_M_MACHINE, A2_I_POWER);
        do_click(a2_cfm_bx[0] + 4, a2_cfm_by + 4);      /* Yes */
        if (a2_abt_up || a2_reset_req != A2_RST_POWER)
            fail("Yes on the reboot confirmation did not latch the cold boot");
        do_wake();
        if (a2_reset_req != 0)
            fail("the wake did not spend the Power On latch");
        do_paint();
        audit("after a confirmed Power On");
        /* ...AND THE MACHINE IS STOPPED WHILE THE BOX WAITS (section 11).
         * The About panel does NOT stop it - its hold range keeps the glass
         * correct and a machine mid-RUN carries on behind it - but the
         * confirmation is 52 pixels tall and holds ~6 of the 24 rows, so a
         * machine that was printing kept composing and blitting the other ~18
         * on every host tick, ~200 ms of the target per tick, for as long as
         * a human took to read two lines. And the answer is about to wipe the
         * machine, so there is nothing behind it worth a pixel. */
        do_cmd(A2_M_MACHINE, A2_I_POWER);
        do_wake();
        c = (int)h_runs;
        do_wake();
        do_wake();
        if ((int)h_runs != c)
            fail("the 6502 kept running behind the reboot confirmation - the "
                 "rows the box does not cover are recomposed on every tick "
                 "while it waits (section 11)");
        if (a2_wants_wake())
            fail("the app re-posted a wake a tick while the reboot "
                 "confirmation waited for a human");
        do_key(27, 1);                      /* Esc answers NO */
        if (a2_abt_up || a2_reset_req != 0)
            fail("Esc on the reboot confirmation did not answer NO");
        do_wake();
        c = (int)h_runs;
        do_wake();
        if ((int)h_runs == c)
            fail("the machine did not start again when the confirmation was "
                 "dismissed");
        do_paint();
        /* THE OPEN PICKER GETS NO DEFAULT NAME (SPEC.md 38.9, 38.10). It was
         * handed `"*.BAS"` - a FILTER written into a slot that is a default
         * NAME - so the literal sat in the box (fdlg draws the name in Open
         * mode and suppresses only the caret) and fdlg_actok lit the Open
         * button before anything was selected: pressing it committed the name
         * `*.BAS` and ended at `Cannot read the file.` */
        h_dlg_def[0] = 'x';
        do_cmd(A2_M_FILE, A2_I_LOAD);
        if (h_dlg_def[0] != 0)
            fail("File > Load Program... seeded the OPEN dialog with a name - "
                 "the third argument is a default NAME and the dialog does no "
                 "filtering by extension (SPEC.md 38.9)");
        do_cmd(A2_M_FILE, A2_I_SAVE);
        if (strcmp(h_dlg_def, "PROGRAM.BAS") != 0)
            fail("File > Save Program... lost its default name, which SAVE "
                 "mode is what the slot is for");

        /* --- MACHINE > POWER ON WITH THE ABOUT PANEL UP (SPEC.md 47) ------
         * ovl_a2_confirm returned 1 in silence here, on the argument that it
         * `cannot happen from a menu the panel is swallowing clicks in front
         * of`. The panel is drawn INSIDE our own window and only os88_onclick
         * / os88_onkey swallow input - the KERNEL'S menu bar is untouched -
         * so the pick lands, and a silent no-op is the shape 47 exists to
         * stop. */
        do_about();
        do_wake();
        a2_msg[0] = 0;
        do_cmd(A2_M_MACHINE, A2_I_POWER);
        if (a2_pan_kind == A2_PAN_CFM)
            fail("Machine > Power On raised a confirmation over the About "
                 "panel - one panel at a time");
        if (strcmp(a2_msg, "Close the About panel.") != 0)
            fail("Machine > Power On with the About panel up was a SILENT "
                 "no-op - the kernel's menu bar is not swallowed by a panel "
                 "drawn inside our own window (SPEC.md 47)");
        do_click(100, 100);
        if (a2_abt_up)
            fail("the About panel did not close after the Power On refusal");
        do_wake();
        do_paint();

        /* --- CPU > CONTINUE ON A JAMMED MACHINE (section 4.5, 10.3) -------
         * The core never runs again after A2_ST_JAM, so `Continue` was a LIVE
         * item that set a flag nothing reads and then said `Running.` - over
         * the top of `6502: JAM at $xxxx`, because a2_status draws the
         * message arm ABOVE the jam arm, and a jammed machine posts no wake
         * so nothing was going to expire it. Both rows are greyed now, and
         * the command refuses in silence if it is dispatched anyway. */
        h_halt();
        a2_menu_state();
        if (a2_cpu_items[A2_I_STOP][0] != 1
            || a2_cpu_items[A2_I_RUN][0] != 1)
            fail("a JAMMED machine still offers CPU > Stop and Continue LIVE "
                 "- there is no machine left to stop, which a2_jam's own "
                 "comment says and a2_menu_state has to act on");
        a2_msg[0] = 0;
        a2_pause = 0;
        do_cmd(A2_M_CPU, A2_I_RUN);         /* the kernel would not dispatch a
                                             * disabled row, but a2_state can
                                             * change between the pull-down
                                             * being built and the pick */
        if (a2_msg[0] != 0)
            fail("CPU > Continue on a JAMMED machine said something - "
                 "`Running.` REPLACES the 6502: JAM line, which is the one "
                 "row that says why the machine is dead (section 4.5)");

        /* ...AND THE LAUNCH DOCUMENT'S WAIT MAY NOT OUTLIVE THE 6502
         * (SPEC.md 8.1.2). a2_argp used to sit in a2_wants_wake's LATCH arm,
         * above the running gate, so a machine that cannot ever reach `]` -
         * jammed, stopped, or behind the reboot confirmation - re-posted a
         * wake at full rate for the whole 60-tick deadline, on the SHARED UI
         * task. It rides the 6502's own arm now. */
        a2_wr(0x0067, 0x00);                /* no `]` yet, and the deadline is
                                             * a long way off, so the arm can
                                             * neither fire nor expire */
        a2_wr(0x0068, 0xFF);
        a2_argp = 1;
        a2_argdl = the_ticks + 1080;
        do_wake();
        if (!a2_argp)
            fail("the fixture's launch document was spent before the test");
        if (a2_wants_wake())
            fail("a launch document waiting for `]` on a machine that is NOT "
                 "RUNNING re-posts a wake at full rate - 60 seconds of the "
                 "shared UI task with nothing able to satisfy it "
                 "(SPEC.md 8.1.2)");
        a2_argp = 0;
        a2_wr(0x0067, 0x01);
        a2_wr(0x0068, 0x08);
        h_go();
        a2_menu_state();
        do_wake();

        printf("a2uitest: the clipboard, program load and save, warp, stop "
               "and the reboot confirmation all behaved\n");
    }

    /* --- WHAT THE ABOUT PANEL CARRIES (APPLE2-SPEC section 11) ------------
     * The row CONTENT is section 11's list and the list is the binding part.
     * Two of its rows are there because something outside this program
     * requires them and nothing in the program would miss them: the LICENCE
     * row, because the floppy is the distributed form of a GPL-2-or-later
     * binary (section 16.2), and the PORTER row, which is
     * apps/c64/c64about.c:46-49's own last row. A row that is dropped in an
     * edit is invisible on any screendump nobody compares. */
    {
        int lic = 0, por = 0, ver = 0, wide = 0;

        for (i = 0; i < A2_ABT_ROWS; i++) {
            if (strstr(a2_abt_text[i], "COPYING"))
                lic = 1;
            if (strstr(a2_abt_text[i], "Ported by"))
                por = 1;
            if (strstr(a2_abt_text[i], "1.0"))
                ver = 1;
            if ((int)strlen(a2_abt_text[i]) * 8 > A2_BANDW)
                wide = 1;
        }
        if (!lic)
            fail("the About panel has no `GPL-2 or later - see COPYING` row "
                 "- this package is GPL-2-or-later and the panel is where a "
                 "reader is told so (section 11)");
        if (!por)
            fail("the About panel has no `Ported by` row (section 11, and "
                 "apps/c64/c64about.c's own last row)");
        if (!ver)
            fail("the About panel has no VERSION row - `APPLE2 for os8088` "
                 "is the product name again, not a version (section 11)");
        if (wide)
            fail("an About panel row is wider than the panel");
    }

    /* --- THE MESSAGE-LENGTH GATE (APPLE2-SPEC section 9) ------------------
     * Every literal a2_say() is given has to fit the status row's cells.
     * apps/apple2/build.sh walks the SOURCES for a2_say() calls and requires
     * each one to be in this array, with an explicit expected MINIMUM - so a
     * corpus of zero literals is a failure and not a pass. */
    {
        static const char *msgs[] = {
            "No APPLE2.OVL yet.",
            "Unable to load APPLE2.OVL.",
            "No bands here - text only.",
            "Another window has it.",
            "6502: JAM at $FFFF",       /* THE WIDEST FORM, and it is here
                                         * although it is no longer an
                                         * a2_say(): the jam is a PERMANENT
                                         * row state now (section 4.5), so
                                         * build.sh's source walk cannot find
                                         * it and this array is the only thing
                                         * that measures it. It is spelled
                                         * with four F's because a2_jam
                                         * appends four hex digits - the bare
                                         * `6502: JAM at $` the gate used to
                                         * hold measured 14 of the 18 that
                                         * reach the glass */
            "Too large for a 48K Apple.",
            "Too large for free memory.",   /* ...the OTHER ceiling: an
                                             * association load whose claim
                                             * stepped down below A2_PRGMAX
                                             * and was exactly filled by the
                                             * read (a2prog.c) */
            /* --- WAVE 4's (sections 6.5, 10.2, 12) --------------------- */
            "A file dialog is open.",
            "Close the About panel.",        /* Machine > Power On with the
                                             * About panel up: the kernel's
                                             * menu bar is NOT swallowed by a
                                             * panel drawn inside our own
                                             * window, so the pick lands and
                                             * has to say something (a2cmd.c) */
            "The window is covered.",
            "No memory for the copy.",
            "The clipboard refused it.",
            "The clipboard is empty.",
            "No memory for the paste.",
            "Cannot read the clipboard.",   /* 26 of 26 - the widest of the
                                             * wave, and it shares the cap
                                             * with `Unable to load
                                             * APPLE2.OVL.` */
            "Pasting 2048 bytes only.",     /* A2_PASTEMAX, spelled out */
            "Not an Applesoft program.",
            "No heap for the program.",
            "Cannot read the file.",
            "Cannot write the file.",
            "Bad program pointers.",
            "No program to save.",
            "Cannot open that folder.",
            "No ] prompt to load into.",
            "Stopped.",
            "Running.",
            "Warp off.",
            "Warp on.",
            "Warp on - no change.",
            /* --- WAVE 5's (sections 8, 13) ----------------------------- */
            "Square-wave tones only.",      /* THE STATED FACT, said once the
                                             * first time this machine makes a
                                             * noise: what the estimator
                                             * cannot reproduce is a fact
                                             * about the BUILD and belongs on
                                             * the row and in the SPEC, never
                                             * in the About panel (section 8) */
            "The speaker is busy.",         /* ...and the refused grant,
                                             * bounded at eight wakes */
            "Muted.",
            "Unmuted.",
            "No colour on this screen.",    /* Machine > Color NTSC on an
                                             * adapter with no foreign mode
                                             * that beat the windowed path
                                             * (section 13) */
            "No memory for colour.",        /* ...and the fullscreen-LATCH
                                             * claim refusing, which is legal
                                             * where the flush's is not */
            "The screen refused it.",
            /* --- WAVE 7's (section 13.1) ------------------------------- */
            "Colour: 1.7 s a scroll.",     /* 23 of 26: the price of a
                                             * SCROLL in FSXM_VGA13 on the
                                             * CPU_8086 tier (section 7.9.4's
                                             * 1,747.2 ms), said once on the
                                             * way OUT of the first colour
                                             * session because there is no
                                             * status row under the bracket to
                                             * read one on the way in. The
                                             * RECURRING cost and not the
                                             * 3,365 ms entry cost, which the
                                             * reader has already paid by the
                                             * time this row exists - and it
                                             * names the SCROLL rather than
                                             * `per RETURN`, which was true
                                             * of a full TEXT page and 5.7x
                                             * high in MIXED, where GR and
                                             * HGR put the text window on
                                             * four rows (section 7.4) */
            "ScrollLock: arrows, Space.",   /* section 6.6, at 25 of 26 cells
                                             * - the wave's widest, with
                                             * `Colour:` above it: the
                                             * kernel is eating the keys this
                                             * machine types with, and this is
                                             * the one sentence that says
                                             * which key hands them back */
            0
        };
        int nm = 0;

        for (i = 0; msgs[i]; i++) {
            nm++;
            if ((int)strlen(msgs[i]) > A2_STCELLS - 16)
                printf("a2uitest: FAIL - the status message %d cells long "
                       "does not fit the row's %d: \"%s\"\n",
                       (int)strlen(msgs[i]), A2_STCELLS - 16, msgs[i]),
                fails++;
        }
        if (nm < 5)
            fail("the message-length gate has fewer than five literals in it "
                 "- a corpus that small is a gate that is not looking");
        printf("\na2uitest: %d status message(s), every one inside the row\n",
               nm);
    }

    if (fails) {
        printf("\na2uitest: %d FAILURE(S)\n", fails);
        return 1;
    }
    printf("a2uitest: OK - the glass showed what the shadow said it showed at "
           "every step\n");
    return 0;
}
