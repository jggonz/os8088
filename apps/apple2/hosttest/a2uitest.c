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
    n_blit++;
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
        h_win_x = 0; h_win_y = 0; h_win_w = 640; h_win_h = 480;
    } else {
        h_win_x = h_norm_x; h_win_y = h_norm_y;
        h_win_w = h_norm_w; h_win_h = h_norm_h;
    }
    h_cont_x = (h_win_x + 1 + 7) & ~7;
    h_cont_y = h_win_y + OS88_TITLE_H;
    h_cont_w = h_win_w - 2;
    h_cont_h = h_win_h - OS88_TITLE_H - 1;
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

int os88_toast(const char *text, int ticks)
{
    (void)ticks;
    strncpy(last_toast, text, sizeof(last_toast) - 1);
    last_toast[sizeof(last_toast) - 1] = 0;
    return 0;
}

int os88_cpu(void) { return OS88_CPU_386; }

void os88_video(struct os88_video *v)
{
    v->w = 640;
    v->h = 480;
    v->dock_top = 448;
    v->kind = OS88_VID_VGA;
    v->bpp = 1;
}

unsigned os88_ticks(void) { return the_ticks; }

int os88_key_down(int scan)
{
    (void)scan;
    if (!h_key_armed) {
        h_key_armed = 1;
        h_key_first_in_main = h_in_main;
        return 0;                       /* the FIRST call always answers up */
    }
    return 0;
}

int os88_snd_caps(void) { return 1; }
int os88_snd_tone(int hz, int t, int p) { (void)hz; (void)t; (void)p; return 0; }

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

/* ==========================================================================
 * THE CLAIMS - the Apple's 64KB and the ROM part
 * ========================================================================*/
#define H_RAMSEG 0x2000u
#define H_ROMSEG 0x1000u                /* >= A2_ROM_MINSEG (0x0D00), which
                                         * the package guards for */
static unsigned char h_ram[65536];
static unsigned char h_rom[14848];
static int claims_live;

static unsigned char *segbase(unsigned seg)
{
    if (seg == H_RAMSEG)
        return h_ram;
    if (seg == H_ROMSEG)
        return h_rom;
    fail("a segment nobody claimed");
    exit(1);
}

unsigned os88_mem_claim(int kb)
{
    if (kb != 64) {
        fail("a claim that is not the Apple's 64KB");
        return 0;
    }
    claims_live++;
    return H_RAMSEG;
}

int os88_mem_free(unsigned seg) { (void)seg; claims_live--; return 0; }
unsigned os88_mem_largest_kb(void) { return 200; }
unsigned os88_part_seg(int i) { return i == 0 ? H_ROMSEG : 0; }
int os88_peek(unsigned seg, unsigned off) { return segbase(seg)[off]; }
void os88_poke(unsigned seg, unsigned off, int v)
{ segbase(seg)[off] = (unsigned char)v; }

unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{ (void)name; (void)seg; (void)cap; return 0; }
int os88_file_dlg(int mode, void *win, const char *defname)
{ (void)mode; (void)win; (void)defname; return -1; }

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

void a2_zcopy_out(void *dst, unsigned a, unsigned n)
{ memcpy(dst, h_ram + (a & 0xFFFF), n); }

int a2_wrote(void) { return h_scr[A2_SCR_ANY]; }

int a2_run(unsigned cycles) { (void)cycles; return A2_RUN_JAM; }
void a2_cut(void) { }
void a2_rebias(void) { }

/* --- the composer, transcribed (a2band.inc) ------------------------------ */
static int n_band, n_group;

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

void a2_band_x2(unsigned char *dst, const unsigned char *src, int rows)
{
    int r, c;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < A2_BSTRIDE; c++) {
            dst[r * 2 * 80 + c * 2] = h_x2tab[src[r * A2_BSTRIDE + c] * 2];
            dst[r * 2 * 80 + c * 2 + 1] =
                h_x2tab[src[r * A2_BSTRIDE + c] * 2 + 1];
        }
        memcpy(dst + (r * 2 + 1) * 80, dst + r * 2 * 80, 80);
    }
}

/* ==========================================================================
 * THE AUDIT: THE GLASS SHOWS WHAT THE SHADOW SAYS IT SHOWS
 * ========================================================================*/
static int audit(const char *where)
{
    int line, byte, bit, want, got, bad = 0;

    for (line = a2_gl0; line < A2_SCRH; line++) {
        if (a2_abt_up && line >= a2_hold_l0 && line <= a2_hold_l1)
            continue;                   /* a panel owns these lines */
        for (byte = 0; byte < A2_BSTRIDE; byte++)
            for (bit = 0; bit < 8; bit++) {
                want = (a2_sh[line * A2_BSTRIDE + byte] >> (7 - bit)) & 1;
                got = glass[a2_gsy + line - a2_gl0][a2_gsx + byte * 8 + bit];
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
 *   ROWSIG 40            1.875 counts    0.67 ms
 *   ROWFLASH 40          2.875 counts    1.03 ms
 *   BAND_X2 8 rows      25.625 counts    9.20 ms
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
#define MS_BLIT     1.750               /* one 320x8 blit1 */
#define MS_SPAN     0.314
#define MS_COPY     0.314
#define MS_SIG      0.673
#define MS_FLASH    1.032
#define MS_TAKE     0.190               /* a2_dirty_take, section 7.5 */

static int c_blit, c_fill, c_frame, c_run, c_cells, c_scroll;
static int c_band, c_group, c_span, c_sig, c_flash, c_copy, c_take;

static void cost_mark(void)
{
    c_blit = n_blit; c_fill = n_fill; c_frame = n_frame; c_run = n_run;
    c_cells = n_cells; c_scroll = n_scroll; c_band = n_band;
    c_group = n_group; c_span = n_span; c_sig = n_sig; c_flash = n_flash;
    c_copy = n_copy; c_take = n_take;
}

static void cost_row(const char *what)
{
    double ms = (n_blit - c_blit) * (MS_GFXCALL + MS_BLIT)
              + (n_fill - c_fill) * MS_GFXCALL
              + (n_frame - c_frame) * MS_GFXCALL
              + (n_scroll - c_scroll) * MS_GFXCALL
              + (n_run - c_run) * MS_GFXCALL
              + (n_cells - c_cells) * MS_GLYPH
              + (n_band - c_band) * MS_BAND
              + (n_group - c_group) * MS_GROUP
              + (n_span - c_span) * MS_SPAN
              + (n_sig - c_sig) * MS_SIG
              + (n_flash - c_flash) * MS_FLASH
              + (n_copy - c_copy) * MS_COPY
              + (n_take - c_take) * MS_TAKE;

    printf("  %-38s %8.1f ms   %2d blit %2d fill %2d scroll %3d group\n",
           what, ms, n_blit - c_blit, n_fill - c_fill, n_scroll - c_scroll,
           n_group - c_group);
    cost_mark();
}

/* ==========================================================================
 * THE SCRIPT
 * ========================================================================*/
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

static void do_cmd(int menu, int item)
{
    os88_gfx_lock();                    /* AM_ONCMD is dispatched under the
                                         * desktop's lock, like a click */
    os88_oncmd(item, menu, the_win);
    os88_gfx_unlock();
}

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
    st[0] = 0;                          /* mode: TEXT */
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

int main(void)
{
    FILE *f;
    void *win;
    int i, r, before, n0, whole_blits, rect_blits, straddle_groups;
    int abt_runs, abt_cells, wake0, fire0;
    int rect_groups, narrow_groups, wide_groups, sigs;

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
    wake0 = h_wake_posted;
    fire0 = h_tmr_fires;
    do_wake();
    cost_row("an idle wake");
    if (n_blit != c_blit)
        fail("an idle wake drew something");
    if (h_wake_posted != wake0 && h_tmr_fires == fire0)
        fail("an idle wake re-posted itself - the wake handler is spinning "
             "the shared UI task");

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
    for (r = 0; r < A2_ROWS - 1; r++)
        for (i = 0; i < A2_COLS; i++)
            a2_wr(a2_tbase[r] + (unsigned)i,
                  (unsigned)a2_rd(a2_tbase[r + 1] + (unsigned)i));
    for (i = 0; i < A2_COLS; i++)
        a2_wr(a2_tbase[A2_ROWS - 1] + (unsigned)i, 0xA0);
    cost_mark();
    before = n_scroll;
    do_wake();
    cost_row("a one-row scroll");
    if (n_scroll == before)
        fail("a whole-page shift was not turned into a gfx_scroll");
    else if (n_scroll_dy != 8)
        fail("the scroll moved the wrong way or the wrong distance - "
             "POSITIVE dy moves the content UP (SPEC.md 5.5)");
    audit("after the scroll");

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
    a2_say("Another window has it.");
    do_wake();
    a2_say("Another window has it.");   /* the IDENTICAL string */
    do_wake();
    {
        int w0 = h_wake_posted, f0 = h_tmr_fires;

        do_wake();
        if (h_wake_posted != w0 && h_tmr_fires == f0)
            fail("an identical status message left the row dirty - the wake "
                 "re-posts for the whole life of the message");
    }
    audit("after the same status message twice");

    /* --- A WINDOW NOTHING SHOWS OF DRAWS NOTHING, AND STOPS ASKING -------
     * clip_set answers -1 when not one pixel of us is on the glass. The flush
     * is then SKIPPED - ~500 ms not spent on a window nobody can see - and
     * the wake must not go on re-posting to be told the same thing 1,400
     * times a second: the work is still OWED, and W_PAINT is what comes and
     * asks for it. */
    h_clip_refuse = 1;
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
    do_paint();                         /* ...and the exposure is what pays */
    audit("after the window came back out from under");

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
            "No 6502 in this build.",
            "Too large for a 48K Apple.",
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
