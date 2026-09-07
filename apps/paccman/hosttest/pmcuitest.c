/* ============================================================================
 * os8088 - apps/paccman/hosttest/pmcuitest.c
 *
 * PACCMAN's host harness (SPEC.md 91), run by apps/paccman/build.sh before
 * anything is built for the 8086, and it stops the build. It #includes the
 * WHOLE program against the stub os88.h beside it, drives it the way the
 * kernel does, models the glass as PIXELS, and after every frame rebuilds
 * what the screen ought to show from an INDEPENDENT recomposition of
 * video_ram and color_ram - so a dirty span the program forgot to mark is a
 * failure here rather than a stale tile nobody notices on the emulator.
 *
 * It exists because the three defects that cost this project bug after bug -
 * a visible redraw, a double-draw flash and input overrun - are invisible in
 * an emulator (PERFORMANCE.md). What a screendump cannot show, a call count
 * and a pixel audit can.
 *
 * FOUR THINGS IT CHECKS
 *   1. the whole translation unit COMPILES under clang, which is stricter
 *      about prototypes than SmallerC (LESSONS.md 3);
 *   2. the glass equals an independent recomposition of the two RAMs, after
 *      every frame, on every layout - 0 differing pixels or FAIL;
 *   3. the three assembly composers agree with C twins written a DIFFERENT
 *      way: the twins go through pmc_pal, the bit values and pmc_mono, where
 *      pmcband.inc goes through pmc_pairs, pmc_planar and pmc_mono2 - so the
 *      three generated lookup tables are checked as well as the loops;
 *   4. the cost of each frame in CALLS, tiles, packed rows and row-planes,
 *      priced from the TERMS table - whose values come from
 *      tests/pmcband/pmcbandbench.asm under QEMU -icount shift=3 and from
 *      nowhere else
 *      (LESSONS.md 13: a per-cell guess was 7x wrong once).
 *
 * It also WRITES THE VECTORS the boot-sector gate compares against
 * (build/pmcbandvec.inc, read by hosttest/pmcbandtest.asm), so the two halves
 * of the composer's verification cannot disagree about what the right answer
 * is.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PMC_HOST 1

/* The STUB SDK, ahead of the program: the stubs below define the API and need
 * its shapes first. paccman.c includes the same file again lower down and the
 * guard makes that a no-op - and the -I order in apps/paccman/build.sh is what
 * makes "os88.h" resolve to the stub beside this file rather than to the real
 * apps/cc/os88.h. */
#include "os88.h"

/* --- the glass ------------------------------------------------------------
 * One byte a pixel: a 0..15 colour index on a 4bpp display, 0 or 1 on a 1bpp
 * one. 0xEE is "never written", so an audit can tell a wrong pixel from an
 * unpainted one. */
#define HG_MAXW 720
#define HG_MAXH 480
#define HG_VIRGIN 0xEE
static unsigned char hg_px[HG_MAXH][HG_MAXW];
static int hg_w, hg_h, hg_bpp, hg_kind, hg_dock;
static int hg_pen;
static int hg_obscured;
static int hg_clip;                     /* a REAL armed clip region          */
static int hg_dmg_whole = 1;            /* what os88_wm_damage answers ...   */
static int hg_dmg_x1, hg_dmg_y1, hg_dmg_x2, hg_dmg_y2;   /* ...when partial  */
static int hg_blit1_refuse;             /* model a kern_small kernel         */

/* the window */
static int hg_wx, hg_wy, hg_ww, hg_wh;  /* the FRAME                         */
static int hg_cx, hg_cy, hg_cw, hg_ch;  /* ...and the content it implies     */
static int hg_snap;                     /* SPEC.md 11.94 armed               */
static int hg_full;
static struct os88_menuset *hg_mset;

/* counters the stubs keep, beside the package's own PMC_COUNT ones */
static int hc_fill, hc_blitp, hc_blit4, hc_blit1, hc_probe, hc_probe_no;
static int hc_toast;
static const char *hc_last_toast;

static int hg_fails;

static void fail(const char *what)
{
    printf("pmcuitest: FAIL - %s\n", what);
    hg_fails++;
}

/* --- the screen and the window -------------------------------------------
 * The window geometry is the KERNEL's arithmetic, not a guess: kernel/wm.inc's
 * wm_geom takes one border pixel off each side and TITLE_H + 1 off the top and
 * bottom, so the content of a W x H frame is (W - 2) x (H - TITLE_H - 1). Get
 * that wrong here and the harness would agree with a program that is wrong on
 * the machine. */
static void hg_screen(int w, int h, int bpp, int kind)
{
    hg_w = w;
    hg_h = h;
    hg_bpp = bpp;
    hg_kind = kind;
    hg_dock = h - 24;                   /* DOCK_H, kernel/dock.inc */
    memset(hg_px, HG_VIRGIN, sizeof hg_px);
    hg_obscured = 0;
    hg_clip = 0;
    hg_blit1_refuse = 0;
    hg_full = 0;
    hg_snap = 0;
    hg_dmg_whole = 1;
}

static void hg_setwin(int x, int y, int w, int h)
{
    hg_wx = x; hg_wy = y; hg_ww = w; hg_wh = h;
    hg_cx = x + 1;
    hg_cy = y + OS88_TITLE_H;
    hg_cw = w - 2;
    hg_ch = h - OS88_TITLE_H - 1;
    /* SPEC.md 11.94: with the snap armed the kernel MOVES THE WINDOW so that
     * the CONTENT origin lands on a multiple of 8. Modelling that is not
     * optional here - without it nothing is aligned, os88_gfx_blitp and
     * os88_gfx_blit1 both refuse every band, and the harness would be testing
     * a machine that does not exist while quietly measuring the fallback
     * (LESSONS.md 7's trap, one device along). */
    if (hg_snap) {
        int shift = hg_cx & 7;
        hg_cx -= shift;
        hg_wx -= shift;
    }
}

static void hg_put(int x, int y, int v)
{
    if (x < 0 || y < 0 || x >= hg_w || y >= hg_h)
        return;                         /* off the screen: skipped, not drawn */
    hg_px[y][x] = v;
}

/* --- the API stubs, each modelling what the machine does ----------------- */

/* THE LOCK IS COUNTED and the region really DIES at an unlock (SPEC.md 11.3).
 * pmc_flush_laid's worker arm drops the lock between chunks of bands, and
 * without the second half of that here the harness would model a region that
 * survived the drop - which is exactly the bug the arm has to not have. */
static unsigned hc_lock, hc_unlock, hc_yield;

/* THE LOCK HOLD IS TIMED, NOT ONLY COUNTED (SPEC.md 91). What matters about a
 * worker's hold is how long the machine cannot draw, and the cost model
 * already knows how long everything between two calls took - so the lock stubs
 * bracket it. `hc_hold_at` is the model's clock when the lock was taken and
 * `hc_hold_worst` the largest hold since cost_reset, which for the worker's
 * own frame is the answer to "does the chunking bound what I think it bounds":
 * pmc_frame runs the TICK inside the same bracket as the first four bands, so
 * the first chunk is the game logic plus four bands and not four bands. */
static double hc_last_ms(void);
static double hc_hold_at, hc_hold_worst;

void os88_gfx_lock(void)   { hc_lock++; hc_hold_at = hc_last_ms(); }
void os88_gfx_unlock(void)
{
    double d = hc_last_ms() - hc_hold_at;

    hc_unlock++;
    hg_clip = 0;
    if (d > hc_hold_worst)
        hc_hold_worst = d;
}
void os88_set_color(int c) { hg_pen = c; }

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int x, y, v;
    hc_fill++;
    v = (hg_bpp == 1) ? (hg_pen >= 8 ? 1 : 0) : hg_pen;
    for (y = y1; y <= y2; y++)
        for (x = x1; x <= x2; x++)
            hg_put(x, y, v);
}

/* os88_gfx_blit4 - two pixels a byte, high nibble leftmost, runs coalesced.
 * On a 1bpp adapter the kernel's own decoder thresholds; that path is modelled
 * but never audited, because the program only reaches it if os88_gfx_blit1
 * refused, which is a kern_small kernel. */
void os88_gfx_blit4(const void *pix, int stride, int x, int y, int w, int h)
{
    const unsigned char *p = (const unsigned char *) pix;
    int r, c, v;
    hc_blit4++;
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++) {
            v = p[r * stride + (c >> 1)];
            v = (c & 1) ? (v & 15) : (v >> 4);
            hg_put(x + c, y + r, (hg_bpp == 1) ? (v >= 8 ? 1 : 0) : v);
        }
}

/* os88_gfx_blit1 - bit 7 leftmost, 1 = lit. x and w must both be multiples of
 * 8 and rows 1..255; a broken argument REFUSES and draws nothing, and so does
 * a kern_small kernel (hg_blit1_refuse). The stub must model the alignment
 * check rather than merely refusing everything: a stub that always refuses
 * measures the fallback path, which is LESSONS.md 7's trap. */
int os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *p = (const unsigned char *) bits;
    int r, c, b;
    if (hg_blit1_refuse)
        return -1;
    if ((x & 7) || (w & 7) || rows < 1 || rows > 255)
        return -1;
    hc_blit1++;
    for (r = 0; r < rows; r++)
        for (c = 0; c < w; c++) {
            b = (p[r * stride + (c >> 3)] >> (7 - (c & 7))) & 1;
            hg_put(x + c, y + r, b);
        }
    return 0;
}

/* os88_gfx_blitp - the six refusals of SPEC.md 5.4.3, each modelled, and the
 * PROBE form of 5.4.3.2: bit 15 of plane_step decides every one of them
 * against the RECT alone, reads no planes and writes no pixel. */
int os88_gfx_blitp(const void *planes, int plane_step, int stride,
                   int x, int y, int w, int rows)
{
    const unsigned char *p = (const unsigned char *) planes;
    int probe = (plane_step & OS88_BLITP_PROBE) != 0;
    int step = plane_step & ~OS88_BLITP_PROBE;
    int r, c, pl, v, byte, bit;

    if (probe)
        hc_probe++;
    if (hg_bpp == 1                     /* four planes is not a Hercules    */
        || (x & 7)                      /* x off the byte grid              */
        || w <= 0 || rows <= 0
        || x < 0 || x + w > hg_w        /* off either side in x             */
        || hg_clip) {                   /* a REAL armed clip region         */
        if (probe)
            hc_probe_no++;
        return -1;
    }
    if (probe)
        return 0;                       /* not one pixel written */
    hc_blitp++;
    for (r = 0; r < rows; r++)
        for (c = 0; c < w; c++) {
            v = 0;
            for (pl = 0; pl < 4; pl++) {
                byte = p[pl * step + r * stride + (c >> 3)];
                bit = (byte >> (7 - (c & 7))) & 1;
                v |= bit << pl;
            }
            hg_put(x + c, y + r, v);
        }
    return 0;
}

/* --- the window ---------------------------------------------------------- */
static char hg_win_tag;
static void *hg_topwin;                 /* os88_wm_create makes the new
                                         * window the frontmost one, which is
                                         * what the kernel does and what
                                         * pmc_frame's focus test reads */

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    (void) title;
    hg_setwin(x, y, w, h);
    hg_topwin = &hg_win_tag;
    return &hg_win_tag;
}
void os88_wm_content(void *win, struct os88_pt *o)
{
    (void) win;
    o->x = hg_full ? 0 : hg_cx;
    o->y = hg_full ? 0 : hg_cy;
}
int os88_wm_geom(void *win, struct os88_size *s)
{
    (void) win;
    s->w = hg_full ? hg_w : hg_cw;
    s->h = hg_full ? hg_h : hg_ch;
    return 0;
}
int  os88_wm_obscured(void *win)          { (void) win; return hg_obscured; }

/* os88_wm_clip_set - SPEC.md 11.3. The region cuts every drawing call to the
 * part of our content nothing is covering, it dies at the next
 * os88_gfx_unlock, and -1 means not one pixel shows.
 *
 * ARMING IT IS WHAT MAKES os88_gfx_blitp REFUSE (SPEC.md 5.4.3.3: a plane byte
 * carries eight x's, and a region a PACKAGE armed is binding where the
 * kernel's own W_PAINT cull is advisory). hg_clip above is that refusal and it
 * was in this harness before there was anything to set it; this is the setter.
 *
 * The fragment cutting itself is NOT modelled - hg_novis is the only thing
 * this harness can say about a covering window - so a row that arms a region
 * asserts the CALLS and never audits the pixels. */
static int hc_clip, hc_clip_no;
static int hg_novis;                    /* clip_set answers "nothing shows" */
int  os88_wm_clip_set(void *win)
{
    (void) win;
    hc_clip++;
    if (hg_novis) {
        hc_clip_no++;
        return -1;
    }
    hg_clip = 1;
    return 0;
}

/* os88_wm_damage - SPEC.md 11.90.2. Answers 1 with the whole content, or 0
 * with the part that actually needs drawing, which may be EMPTY (x1 > x2).
 * Absolute and inclusive, like every rect the kernel hands out. Modelled
 * because a repaint that ignores it is a full 36-band recomposition - 2.5 s
 * of XT - for a menu that covered three tile rows, and nothing else in this
 * harness can see the difference: the pixel audit compares only the final
 * frame, and by then both answers have drawn the same picture. */
int os88_wm_damage(void *win, struct os88_rect *r)
{
    (void) win;
    if (hg_dmg_whole) {
        r->x1 = hg_full ? 0 : hg_cx;
        r->y1 = hg_full ? 0 : hg_cy;
        r->x2 = r->x1 + (hg_full ? hg_w : hg_cw) - 1;
        r->y2 = r->y1 + (hg_full ? hg_h : hg_ch) - 1;
        return 1;
    }
    r->x1 = hg_dmg_x1;
    r->y1 = hg_dmg_y1;
    r->x2 = hg_dmg_x2;
    r->y2 = hg_dmg_y2;
    return 0;
}
void os88_wm_snap(void *win, int on)
{
    (void) win;
    hg_snap = on;
    hg_setwin(hg_wx, hg_wy, hg_ww, hg_wh);   /* it APPLIES, not just registers */
}
void os88_wm_keeph(void *win, int on)     { (void) win; (void) on; }
void os88_wm_ownbg(void *win, int on)     { (void) win; (void) on; }
void os88_wm_minsize(void *w, int a, int b) { (void) w; (void) a; (void) b; }
void os88_wm_display(void *win, struct os88_video *v)
{
    (void) win;
    v->w = hg_w; v->h = hg_h; v->dock_top = hg_dock;
    v->kind = hg_kind; v->bpp = hg_bpp;
}
void os88_video(struct os88_video *v)     { os88_wm_display(0, v); }

void os88_menu_set(void *win, struct os88_menuset *set)
{
    (void) win;
    hg_mset = set;
    set->oncmd = 1;                      /* the kernel writes this */
}
void os88_about_set(void *win)                     { (void) win; }
/* The card only DRAWS - the app keeps the flag and the dismissal - so the two
 * stubs COUNT rather than paint. The pixel audit rebuilds the field from the
 * two RAMs, and a card modelled as pixels would make every audit after an
 * About fail against a model that does not know about it; what matters here
 * is that a paint taken while the card is up redraws the WHOLE card, which is
 * what stops a partial expose rubbing a hole in it. */
static int hc_about, hc_about_d;
void os88_about_card(void *win, const char **l)    { (void) win; (void) l;
                                                     hc_about++; }
void os88_about_card_d(void *win, const char **l)  { (void) win; (void) l;
                                                     hc_about_d++; }
int  os88_fullscreen(void *win, int enter)
{
    (void) win;
    hg_full = enter;
    return 0;
}
/* --- the keyboard, as a LEVEL plus the port's one-frame press latch -------
 * hg_down is what os88_key_down answers; the harness sets it to model a HOLD
 * and calls os88_onkey to model a PRESS, so a tap and a hold are both driven
 * (SPEC.md 91). */
static int hg_down[256];
int  os88_key_down(int scan)
{
    if (scan == 0)
        return 0;                       /* the arming call */
    return hg_down[scan & 255];
}

/* --- tasks and time -------------------------------------------------------
 * The worker is never RUN here - it is an endless loop - so os88_task_spawn
 * only records that it was asked for. The harness drives pmc_frame() itself,
 * which is the worker's whole body, and hg_ticks is the clock it advances. */
static int hg_spawned, hg_spawn_refuse;
static unsigned hg_ticks_v;

void *os88_wm_top(void)        { return hg_topwin; }
int   os88_task_spawn(void *w) { (void) w; if (hg_spawn_refuse) return -1;
                                 hg_spawned++; return 0; }
void  os88_task_alive(void *w) { (void) w; }
void  os88_task_sleep(int t)   { (void) t; }
/* hg_on_yield - WHAT THE UI TASK DOES while the worker has let go of the gfx
 * lock. The worker's chunked flush unlocks, yields and re-locks, and the whole
 * point of the window it opens is that somebody else can draw in it - so the
 * break rows below hang the About card off this hook and check that the worker
 * notices when it comes back. It is null everywhere else. */
static void (*hg_on_yield)(void);
void  os88_task_yield(void)    { hc_yield++;
                                 if (hg_on_yield) hg_on_yield(); }
unsigned os88_ticks(void)      { return hg_ticks_v; }
/* --- the speaker ----------------------------------------------------------
 * THE ONE VOICE THE THREE BECOME. os88_snd_tone is the whole of the sound API
 * a package gets (SPEC.md 34): one square wave, no volume, no waveform. What
 * the model records is what was ASKED for, so the sound rows below can say
 * which of the three arcade voices reached the speaker on a given frame - the
 * thing no capture of the real machine can tell you, because by then it is one
 * tone either way. */
static int hc_tone;
static int hg_tone_hz = -1, hg_tone_ticks, hg_tone_prio;
int  os88_snd_tone(int hz, int t, int p)
{
    hc_tone++;
    hg_tone_hz = hz;
    hg_tone_ticks = t;
    hg_tone_prio = p;
    return 0;
}
int  os88_toast(const char *text, int ticks)
{
    (void) ticks;
    hc_toast++;
    hc_last_toast = text;
    return 0;
}

/* --- THE PROGRAM --------------------------------------------------------- */
#include "paccman.c"

/* ==========================================================================
 * THE THREE COMPOSERS, WRITTEN A SECOND TIME AND A DIFFERENT WAY
 *
 * These are the C twins of apps/paccman/pmcband.inc, and they are what the
 * host build links against. They are deliberately NOT transcriptions: the
 * assembly reaches its answer through the three generated lookup tables
 * (pmc_pairs, pmc_planar, pmc_mono2) and these reach it through the pixel
 * values, pmc_pal and pmc_mono - so agreement between the two checks the
 * generated tables as well as the loops.
 *
 * They also WRITE THE VECTORS hosttest/pmcbandtest.asm compares the shipping
 * assembly against on a real x86, which is what stops the two halves of the
 * verification from disagreeing about the right answer.
 * ========================================================================*/

/* one 2-bit pixel out of a tile or sprite row */
static int pmc_px(const unsigned char *rowbase, int col)
{
    int b = rowbase[col >> 2];
    return (b >> (6 - 2 * (col & 3))) & 3;
}

/* ONE tile pixel, with the CGA row MERGE folded in where it applies. The
 * assembly reaches this through pmc_zmask and one XLAT a source byte; the
 * twin reaches it by asking whether the even row's pixel is 0 and taking the
 * odd row's if it is, which is the SENTENCE the table encodes rather than the
 * table. (SPEC.md 91: an OR would invent a colour, because a tile pixel is a
 * 2-bit index and 1 | 2 is 3.) */
static int pmc_px_merged(const unsigned char *row, int col, int merge)
{
    int v = pmc_px(row, col);
    if (v == 0 && merge)
        v = pmc_px(row + 2, col);
    return v;
}

/* THE ARM THE HARNESS DRIVES. pmc_draw_band passes pmc_zmask unconditionally,
 * so the SHIPPING CGA picture is the merged one and pv_merge is 1 for every
 * assertion in this file. It is a variable rather than a constant so that the
 * cost table can price the same CGA frame BOTH ways - which is what makes
 * SPEC.md 91's "the sampled arm is still reachable, so the look question can
 * be re-opened with numbers" a statement about a frame and not only about a
 * bench row. Setting it to 0 is exactly what passing zmask = 0 does. */
static int pv_merge = 1;

void pmc_tile(const unsigned char *src, const unsigned char *pairs,
              unsigned char *dst, int rowstep, const unsigned char *zmask)
{
    /* `pairs` points into pmc_pairs; recover the colour block from it so the
     * twin can go through pmc_pal instead, which is the whole point. */
    int cblk = (int) (pairs - pmc_pairs) >> 4;
    int rows = 8 / rowstep;
    int merge = (rowstep == 2 && zmask != 0 && pv_merge);
    int r, c, hi, lo;

    for (r = 0; r < rows; r++) {
        const unsigned char *row = src + (r * rowstep) * 2;
        for (c = 0; c < 8; c += 2) {
            hi = pmc_pal[cblk * 4 + pmc_px_merged(row, c, merge)];
            lo = pmc_pal[cblk * 4 + pmc_px_merged(row, c + 1, merge)];
            dst[r * PMC_BAND_ROW + (c >> 1)] = (hi << 4) | lo;
        }
    }
}

void pmc_pack_pl(const unsigned char *band, unsigned char *planes, int rows,
                 const unsigned int *planar, int cols)
{
    int r, c, bit, pl, v, byte;
    (void) planar;                       /* the twin does not use the table */
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            for (pl = 0; pl < 4; pl++) {
                byte = 0;
                for (bit = 0; bit < 8; bit++) {
                    int px = c * 8 + bit;
                    v = band[r * PMC_BAND_ROW + (px >> 1)];
                    v = (px & 1) ? (v & 15) : (v >> 4);
                    byte |= ((v >> pl) & 1) << (7 - bit);
                }
                planes[pl * PMC_PL_STEP + r * PMC_PL_STRIDE + c] = byte;
            }
}

void pmc_pack_1(const unsigned char *band, unsigned char *bits, int rows,
                const unsigned char *mono2, int y0, int cols)
{
    int r, c, bit, v, cls, lit, byte;
    (void) mono2;                        /* the twin goes through pmc_mono  */
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            byte = 0;
            for (bit = 0; bit < 8; bit++) {
                int px = c * 8 + bit;
                v = band[r * PMC_BAND_ROW + (px >> 1)];
                v = (px & 1) ? (v & 15) : (v >> 4);
                cls = pmc_mono[v];
                lit = (cls == 2) || (cls == 1 && (((px + y0 + r) & 1) == 0));
                byte |= (lit ? 1 : 0) << (7 - bit);
            }
            bits[r * PMC_PL_STRIDE + c] = byte;
        }
}

/* The sprite layer's twin. It flips by ARITHMETIC where the assembly flips
 * through pmc_brev and a backwards byte walk, so agreeing checks the table as
 * well as the loop; and it reaches its colour through pal4 exactly as the
 * assembly's xlat does, because there is nothing else a 2-bit index can go
 * through. Colour index 0 leaves the band's own byte alone - that is what
 * makes a sprite transparent. */
void pmc_sprite(const unsigned char *src, const unsigned char *pal4,
                unsigned char *dst, int sinc, int rows, int flags,
                const unsigned char *brev, const unsigned char *zmask)
{
    int r, j, v, c, col, ph, lastb;
    int merge = (zmask != 0 && pv_merge);
    const unsigned char *row;
    unsigned char *d;

    (void) brev;

    /* THE BOUND _pmc_sprite DOES NOT TEST, TESTED HERE.
     *
     * The assembly never clips and never tests a bound (pmcband.inc), and
     * what makes that safe is PMC_BAND_PAD plus one invariant: a sprite's x
     * is a WRAPPED actor centre less 8, so it is in [-8, PMC_FIELD_W - 9].
     * The worst legal case - Pac-Man at the left tunnel mouth, pmc_ax = 223,
     * sp_x = 215 - puts the sprite's last byte at offset 119 of a 120-byte
     * row, and on band row 7 that is byte 959 of a 960-byte scratch. So the
     * pad is EXACT and not generous, and one more pixel of x writes into
     * pmc_planes with nothing to catch it.
     *
     * This is the twin, so it is where the arithmetic can be checked at all:
     * on the target the same call is a silent overwrite. A future caller that
     * positions a sprite by something other than a wrapped actor centre - the
     * wave-3 intro is exactly that code - trips this line instead. The test is
     * on a dst INSIDE pmc_band, because emit_vectors below calls this same
     * twin on scratch buffers of its own that have nothing to do with the
     * renderer's band. */
    lastb = (flags & 1) ? 8 : 7;
    if (dst >= pmc_band && dst < pmc_band + sizeof pmc_band
        && dst + (rows - 1) * PMC_BAND_ROW + lastb
             >= pmc_band + sizeof pmc_band)
        fail("a sprite wrote outside the band scratch (PMC_BAND_PAD is exact)");
    for (r = 0; r < rows; r++) {
        row = src + r * sinc;
        d = dst + r * PMC_BAND_ROW;
        ph = flags & 1;
        for (j = 0; j < 16; j++) {
            col = (flags & 2) ? 15 - j : j;
            v = (row[col >> 2] >> (6 - 2 * (col & 3))) & 3;
            /* THE ROW MERGE, on the sprite layer: where this row is
             * transparent the DROPPED row shows through instead. The twin
             * asks the sentence; the assembly asks pmc_zmask and one XLAT a
             * source byte (SPEC.md 91). `sinc / 2` is the +-4 bytes to the
             * partner, flip and all, exactly as _pmc_sprite computes it. */
            if (v == 0 && merge) {
                const unsigned char *o = row + sinc / 2;
                v = (o[col >> 2] >> (6 - 2 * (col & 3))) & 3;
            }
            if (v) {
                c = pal4[v];
                if (ph)
                    *d = (unsigned char) ((*d & 0xF0) | c);
                else
                    *d = (unsigned char) ((*d & 0x0F) | (c << 4));
            }
            ph ^= 1;
            if (!ph)
                d++;
        }
    }
}

/* ==========================================================================
 * THE AUDIT: the glass against an independent recomposition
 * ========================================================================*/

/* What SHOULD be at field pixel (sx, sy) - sy being a SCREEN row of the
 * field, so the source row is sy * pmc_step. Built straight from the two RAMs,
 * the tile ROM and pmc_pal: it shares no line with pmc_draw.c. */
static int truth_px(int sx, int sy)
{
    int tc = sx >> 3, ic = sx & 7;
    int srow = sy * pmc_step;
    int tr = srow >> 3, ir = srow & 7;
    int i = (tr << PMC_VSHIFT) + tc;
    const unsigned char *row = pmc_tiles + (pmc_vram[i] << 4) + ir * 2;
    int v = pmc_pal[(pmc_cram[i] & 31) * 4
                    + pmc_px_merged(row, ic, pmc_step == 2 && pv_merge)];
    int cls, lit, k, sc, sr, pv;

    /* ...and then the SPRITE layer over it, in the composer's own order, so
     * the last enabled sprite that covers this pixel wins. Written from the
     * sprite state and the ROM alone: it shares no line with pmc_draw.c. */
    for (k = 0; k < PMC_NSPR; k++) {
        int sru, sr2;
        if (!pmc_sp_on[k])
            continue;
        sc = sx - pmc_sp_x[k];
        sru = srow - pmc_sp_y[k];               /* the sprite's OWN row      */
        if (sc < 0 || sc > 15 || sru < 0 || sru > 15)
            continue;
        if (pmc_sp_flip[k] & 1) sc = 15 - sc;
        sr = (pmc_sp_flip[k] & 2) ? 15 - sru : sru;
        pv = pmc_px(pmc_sprites + (pmc_sp_tile[k] << 6) + sr * 4, sc);
        /* THE ROW MERGE ON THE SPRITE LAYER (SPEC.md 91). The pair is always
         * (u, u + 1) in the sprite's own rows - flipy walks them backwards, so
         * its (r, r - 1) is the same pair - and row 15 has no partner, which
         * is the one row pmc_band_sprites splits off and asks for unmerged. */
        if (pv == 0 && pmc_step == 2 && pv_merge && sru < 15) {
            sr2 = (pmc_sp_flip[k] & 2) ? 15 - (sru + 1) : sru + 1;
            pv = pmc_px(pmc_sprites + (pmc_sp_tile[k] << 6) + sr2 * 4, sc);
        }
        if (pv)
            v = pmc_pal[(pmc_sp_col[k] & 31) * 4 + pv];
    }

    if (hg_bpp != 1)
        return v;
    cls = pmc_mono[v];
    lit = (cls == 2) || (cls == 1 && (((sx + sy) & 1) == 0));
    return lit ? 1 : 0;
}

static int audit(const char *where)
{
    int sx, sy, want, got, bad = 0, fx = -1, fy = -1, gw = 0, gg = 0;

    /* THE SPRITE-X INVARIANT, on every audited frame. PMC_BAND_PAD is exactly
     * exhausted by a sprite at PMC_FIELD_W - 9 (see pmc_sprite above), so an
     * enabled sprite outside [-8, PMC_FIELD_W - 9] is a scratch overrun on
     * the target whether or not this frame happened to compose its band. */
    for (sx = 0; sx < PMC_NSPR; sx++)
        if (pmc_sp_on[sx]
            && (pmc_sp_x[sx] < -8 || pmc_sp_x[sx] > PMC_FIELD_W - 9)) {
            printf("pmcuitest: FAIL - %s: sprite %d at x = %d is outside "
                   "[-8, %d], which PMC_BAND_PAD does not cover\n",
                   where, sx, pmc_sp_x[sx], PMC_FIELD_W - 9);
            hg_fails++;
            return 1;
        }

    for (sy = 0; sy < pmc_fh; sy++)
        for (sx = 0; sx < PMC_FIELD_W; sx++) {
            want = truth_px(sx, sy);
            got = hg_px[pmc_fy + sy][pmc_fx + sx];
            if (want != got) {
                if (!bad) { fx = sx; fy = sy; gw = want; gg = got; }
                bad++;
            }
        }
    if (bad) {
        printf("pmcuitest: FAIL - %s: %d differing pixel(s); first at field "
               "(%d,%d) = tile (%d,%d): want %d, glass %d\n",
               where, bad, fx, fy, fx >> 3, (fy * pmc_step) >> 3, gw, gg);
        hg_fails++;
    }
    return bad;
}

/* ==========================================================================
 * THE COST TABLE
 *
 * Every term is MEASURED, by tests/pmcband/pmcbandbench.asm under QEMU
 * -icount shift=3 (SPEC.md 91's table says so), and nothing here may be a guess (SPEC.md 91,
 * LESSONS.md 13). apps/paccman/build.sh passes each one in with a -D and the
 * #ifndef fallbacks below are ZERO, not the bench's numbers: a term nobody
 * measured must cost nothing and say so on the closing line, rather than
 * stand as a plausible figure. The blit and fill terms are PERFORMANCE.md's.
 * ========================================================================*/
/* The COMPOSER's three terms come from tests/pmcband/pmcbandbench.asm under
 * QEMU -icount shift=3 and from nowhere else. They are passed in by
 * apps/paccman/build.sh (-DPMC_T_TILE=... and the two beside it) so that the
 * measured numbers live in ONE place - build.sh - and a rebuild after a new
 * bench run is one edit rather than four. A build with none of them defined
 * prints the row and says the terms are unmeasured, rather than quietly
 * pricing a frame from zeros. */
#ifndef PMC_T_TILE
#define PMC_T_TILE    0.0
#define PMC_T_PACKPL  0.0
#define PMC_T_PACK1   0.0
#define PMC_T_BENCHED 0
#else
#define PMC_T_BENCHED 1
#endif
/* A TILE HAS THREE PRICES, NOT ONE, and for a while this harness knew only
 * the first - which made every CGA cost row it printed ~60% high on tiles and,
 * worse, made the row MERGE invisible to the model that is supposed to catch a
 * composer regression. The bench measures all three (SPEC.md 91):
 *
 *   PMC_T_TILE    rowstep 1, eight output rows - every adapter but CGA
 *   PMC_T_TILE2   rowstep 2, four rows, the plain alternate-row SAMPLE
 *   PMC_T_TILE2M  ...and the same four rows with the row merge, which is what
 *                 SHIPS on CGA (pmc_draw_band passes pmc_zmask always)
 *
 * pmc_draw_band counts step-2 tiles into pmc_n_tiles2, so cost_row prices each
 * tile at the term for the layout it was actually composed in. */
#ifndef PMC_T_TILE2
#define PMC_T_TILE2   0.0
#define PMC_T_TILE2M  0.0
#define PMC_T2_BENCHED 0
#else
#define PMC_T2_BENCHED 1
#endif
/* THE TWO WAVE-2 TERMS, BOTH NOW TAKEN (SPEC.md 91). PMC_T_SPRROW is the mean
 * of the bench's two SPRITE rows over their eight rows - the even-nibble case
 * and the odd-nibble-plus-flipx one differ by 6% and Pac-Man spends about half
 * his frames at each. PMC_T_LOGIC is NOT the bench's: the bench is a
 * standalone assembly package and cannot call a C function, so one game_tick()
 * is bracketed by tests/paccman.py on MartyPC's cycle counter instead.
 * build.sh passes both in unconditionally; the zero fallbacks below exist so
 * that a build with them stripped prices the sprite layer at nothing and SAYS
 * SO on the closing line, rather than letting a plausible number stand. */
#ifndef PMC_T_SPRROW
#define PMC_T_SPRROW  0.0       /* one sprite ROW merged into a band       */
#define PMC_SPR_BENCHED 0
#else
#define PMC_SPR_BENCHED 1
#endif
/* ...AND A SPRITE ROW HAS TWO PRICES for the tile's reason: the CGA layout
 * merges the dropped source row into every drawn one (SPEC.md 91), which is a
 * second read and a second table pass a source byte. pmc_draw_band counts the
 * merged rows into pmc_n_sprowm and cost_row prices those at this term - so
 * the sprite half of the short-display question is as visible to the model as
 * the tile half. Zero here means the bench's merged rows were not taken, and
 * the closing line says so. */
#ifndef PMC_T_SPRROWM
#define PMC_T_SPRROWM 0.0       /* ...with the CGA row merge on            */
#define PMC_SPRM_BENCHED 0
#else
#define PMC_SPRM_BENCHED 1
#endif
#ifndef PMC_T_LOGIC
#define PMC_T_LOGIC   0.0       /* one game_tick() with five actors        */
#endif

#define T_CALL      756.0   /* us - any gfx_* call, whatever it draws        */
#define T_THUNK      57.7   /* us - one OSAPI far call + the near call       */
#define T_TILE      PMC_T_TILE       /* us - one 8x8 tile composed           */
#define T_TILE2     PMC_T_TILE2      /* us - ...at rowstep 2, sampled        */
#define T_TILE2M    PMC_T_TILE2M     /* us - ...at rowstep 2, row-merged     */
#define T_PACKPL    PMC_T_PACKPL     /* us - one packed row -> four planes   */
#define T_PACK1     PMC_T_PACK1      /* us - one packed row -> 1bpp          */
#define T_SPRROW    PMC_T_SPRROW     /* us - one 16-pixel sprite row merged  */
#define T_SPRROWM   PMC_T_SPRROWM    /* us - ...with the CGA row merge on    */
#define T_LOGIC     PMC_T_LOGIC      /* us - one game_tick, five actors      */

/* THE THREE BLIT TERMS ARE MEASURED TOO, and they had to be. The first draft
 * priced OSAPI_GFX_BLITP from Paint's 64x64 and 256x16 shapes - 756 us a call
 * plus 114 us a row-plane - which makes a 224x8 band 4.4 ms; the bench says
 * 7.36. A 224-px band is a shape neither of those two covers and the model was
 * 40% light on it, which is exactly the 20%-either-way caveat SPEC.md 91
 * carried. These are the bench's own rows, minus the 756 us call floor and
 * divided by the band's 8 rows, so a partial span scales by its columns.
 *
 *   BLITP 224x8   7.36 ms   -> (7360 - 756) / 8 = 826 us a row at 28 columns
 *   BLIT4 224x8  48.33 ms   -> (48330 - 756) / 8 = 5947 us a row  ...
 *   BLIT1 224x8   1.21 ms   -> (1210 - 756) / 8 = 57 us a row     ... */
/* THE WORKER'S WORST UNINTERRUPTIBLE LOCK HOLD, in ms of 4.77 MHz 8088 - the
 * game logic plus PMC_HOLD_BANDS full-width VGA bands, which is what one chunk
 * of a round-won flash frame really is (SPEC.md 91). It is a BUDGET and not a
 * measurement: the row below prints what the model says and fails over this. */
#define PMC_HOLD_MS   460

#define T_BLITP_R   826.0   /* us - one band row, four planes, 28 columns    */
#define T_BLIT4_R  5947.0   /* us - ...through the planar decoder, which a
                             *      band 224 px wide ALWAYS takes            */
#define T_BLIT1_R    57.0   /* us - ...and one bit a pixel                   */

/* A FILL IS PRICED BY ITS AREA, and it has to be. Priced at the call floor
 * alone, a whole-content 224x288 fill - 64,512 pixels every one of which a
 * band covers again a moment later - reports as 0.8 ms, so the double-draw
 * that PERFORMANCE.md calls invisible in an emulator was invisible in this
 * harness too. These are PERFORMANCE.md's own measured gfx_fill rates
 * ("Measured - drawing"): 177 us a scan line and 0.28 us a pixel on
 * Hercules, 182 and 0.33 on CGA. They are the 1bpp figures because those are
 * the ones that were measured; VGA writes four planes and is not cheaper, so
 * using them here is a LOWER bound on the cost and never an excuse. */
#define T_FILL_ROW  177.0   /* us - one scan line of a gfx_fill              */
#define T_FILL_PX     0.28  /* us - ...and one pixel of it                   */

/* Every per-row term above was measured at the band's full 28 columns, so a
 * narrower damage span is priced by its share of them: pmc_n_rc is the sum of
 * rows x columns over the frame's bands, and rc / 28 is "how many full-width
 * rows' worth of work was that". cost_row asserts that the three per-kind
 * counts below sum to it.
 *
 * AND IT IS SPLIT BY WHICH BLIT TOOK IT. A frame can use more than one - a
 * BLITP band refused after its probe, or a BLIT1 band on a kern_small kernel,
 * falls through to BLIT4 - and the stubs only count a call that SUCCEEDED, so
 * both counters end non-zero. Charged one shared row count per blit kind that
 * appeared, such a frame paid its whole count at 826 us AND again at 5,947:
 * a ~7x overcharge on the one path these counters exist to price. pmc_n_rc_p,
 * pmc_n_rc_1 and pmc_n_rc_4 are counted where the blit is issued. */
/* The most expensive frame the whole drive produced, kept so the report ends
 * with the number that actually matters on a 4.77 MHz 8088. */
static double hc_worst_ms;
static char   hc_worst[160];

static void cost_row(const char *what)
{
    double ms;

    /* THE THREE PER-KIND ROW COUNTS MUST SUM TO THE TOTAL, which is what keeps
     * pmc_n_rc load-bearing now that nothing prices from it: a band whose rows
     * were charged to no blit kind (an early return that skipped a counter) or
     * to two would leave the model silently cheap or dear, and this is one
     * addition on a row that is printed anyway. */
    if (pmc_n_rc_p + pmc_n_rc_1 + pmc_n_rc_4 != pmc_n_rc)
        fail("a band's rows were priced by no blit kind, or by two");

    ms = (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe)
             * (T_CALL + T_THUNK)
       + pmc_n_fillrows * T_FILL_ROW + pmc_n_fillpx * T_FILL_PX
       + (pmc_n_tiles - pmc_n_tiles2) * T_TILE
       + pmc_n_tiles2 * (pv_merge ? T_TILE2M : T_TILE2)
       + (pmc_n_pkpl / (double) PMC_PL_STRIDE) * T_PACKPL
       + (pmc_n_pk1  / (double) PMC_PL_STRIDE) * T_PACK1
       + (pmc_n_sprow - pmc_n_sprowm) * T_SPRROW
       + pmc_n_sprowm * (pv_merge ? T_SPRROWM : T_SPRROW)
       + pmc_n_gtick * T_LOGIC
       + (pmc_n_rc_p / (double) PMC_PL_STRIDE) * T_BLITP_R
       + (pmc_n_rc_1 / (double) PMC_PL_STRIDE) * T_BLIT1_R
       + (pmc_n_rc_4 / (double) PMC_PL_STRIDE) * T_BLIT4_R;
    ms /= 1000.0;

    printf("  %-26s calls %3d (fill %d blitp %d blit4 %d blit1 %d probe %d/%d)"
           "  tiles %4u  bands %3u  spr %2u/%3u  gt %2u  %8.1f ms\n",
           what,
           hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe,
           hc_fill, hc_blitp, hc_blit4, hc_blit1, hc_probe - hc_probe_no,
           hc_probe, pmc_n_tiles, pmc_n_bands, pmc_n_spr, pmc_n_sprow,
           pmc_n_gtick, ms);
    if (ms > hc_worst_ms) {
        hc_worst_ms = ms;
        snprintf(hc_worst, sizeof hc_worst,
                 "%s: %d calls, %u tiles, %u bands, %u sprite-band(s) of %u "
                 "row(s), %u game tick(s)",
                 what, hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe,
                 pmc_n_tiles, pmc_n_bands, pmc_n_spr, pmc_n_sprow,
                 pmc_n_gtick);
    }
}

static double hc_last_ms(void)
{
    return (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe)
               * (T_CALL + T_THUNK)
         + pmc_n_fillrows * T_FILL_ROW + pmc_n_fillpx * T_FILL_PX
         + (pmc_n_tiles - pmc_n_tiles2) * T_TILE
         + pmc_n_tiles2 * (pv_merge ? T_TILE2M : T_TILE2)
         + (pmc_n_pkpl / (double) PMC_PL_STRIDE) * T_PACKPL
         + (pmc_n_pk1  / (double) PMC_PL_STRIDE) * T_PACK1
         + (pmc_n_sprow - pmc_n_sprowm) * T_SPRROW
         + pmc_n_sprowm * (pv_merge ? T_SPRROWM : T_SPRROW)
         + pmc_n_gtick * T_LOGIC
         + (pmc_n_rc_p / (double) PMC_PL_STRIDE) * T_BLITP_R
         + (pmc_n_rc_1 / (double) PMC_PL_STRIDE) * T_BLIT1_R
         + (pmc_n_rc_4 / (double) PMC_PL_STRIDE) * T_BLIT4_R;
}

/* ...in MILLISECONDS, which is what cost_row prints and what every budget
 * below is written in. The two spellings differing by a factor of a thousand
 * is a mistake that reads as a passing test, so there is one conversion. */
static double hc_last_ms_ms(void)
{
    return hc_last_ms() / 1000.0;
}

static void cost_reset(void)
{
    hc_hold_at = hc_hold_worst = 0.0;
    hc_fill = hc_blitp = hc_blit4 = hc_blit1 = hc_probe = hc_probe_no = 0;
    hc_clip = hc_clip_no = 0;
    hc_lock = hc_unlock = hc_yield = 0;
    hg_clip = 0;                        /* the region dies at the caller's
                                         * next os88_gfx_unlock, and one
                                         * measured step is one lock hold */
    pmc_n_calls = pmc_n_tiles = pmc_n_bands = pmc_n_rows = pmc_n_rc = 0;
    pmc_n_tiles2 = 0;
    pmc_n_rc_p = pmc_n_rc_1 = pmc_n_rc_4 = 0;
    pmc_n_sprowm = 0;
    pmc_n_fillpx = pmc_n_fillrows = 0;
    pmc_n_pkpl = pmc_n_pk1 = 0;
    pmc_n_spr = pmc_n_sprow = pmc_n_gtick = 0;
}

/* ==========================================================================
 * THE VECTORS the boot-sector gate compares against
 * ========================================================================*/
static void emit(FILE *f, const char *name, const unsigned char *p, int n)
{
    int i;
    fprintf(f, "%s:\n", name);
    for (i = 0; i < n; i++) {
        if ((i & 15) == 0)
            fprintf(f, "%s    db ", i ? "\n" : "");
        fprintf(f, "0x%02X%s", p[i], ((i & 15) == 15 || i == n - 1) ? "" : ", ");
    }
    fprintf(f, "\n");
}

static void emitw(FILE *f, const char *name, const unsigned int *p, int n)
{
    int i;
    fprintf(f, "%s:\n", name);
    for (i = 0; i < n; i++) {
        if ((i & 7) == 0)
            fprintf(f, "%s    dw ", i ? "\n" : "");
        fprintf(f, "0x%04X%s", p[i], ((i & 7) == 7 || i == n - 1) ? "" : ", ");
    }
    fprintf(f, "\n");
}

#define VEC_TILE   0xE7                 /* a maze corner: all four pixel
                                         * values appear in it              */
#define VEC_COLOR  0x10                 /* COLOR_DOT, the playfield's       */
#define VEC_SPR    34                   /* a ghost facing down: all four
                                         * pixel values AND a wide
                                         * transparent border, which is what
                                         * makes the merge testable at all */
#define VEC_SCOL   0x03                 /* COLOR_PINKY                      */

static unsigned char vec_band[8 * PMC_BAND_ROW];
static unsigned char vec_t1[8 * PMC_BAND_ROW];
static unsigned char vec_t2[8 * PMC_BAND_ROW];
static unsigned char vec_t3[8 * PMC_BAND_ROW];
static unsigned char vec_pl[4 * PMC_PL_STEP];
static unsigned char vec_m1[8 * PMC_PL_STRIDE];
static unsigned char vec_sb0[8 * PMC_BAND_ROW];
static unsigned char vec_s1[8 * PMC_BAND_ROW];
static unsigned char vec_s2[8 * PMC_BAND_ROW];
static unsigned char vec_s3[8 * PMC_BAND_ROW];
static unsigned char vec_s4[8 * PMC_BAND_ROW];

static void write_vectors(const char *path)
{
    FILE *f;
    int r, c;

    /* A band with every packed byte value the tiles can produce, plus a
     * deterministic filler, so the packers are not exercised on one colour. */
    for (r = 0; r < 8; r++)
        for (c = 0; c < PMC_BAND_ROW; c++)
            vec_band[r * PMC_BAND_ROW + c] =
                (unsigned char) ((r * 37 + c * 91 + 13) & 0xFF);

    memset(vec_t1, 0xAA, sizeof vec_t1);
    memset(vec_t2, 0xAA, sizeof vec_t2);
    memset(vec_t3, 0xAA, sizeof vec_t3);
    pmc_tile(pmc_tiles + (VEC_TILE << 4), pmc_pairs + (VEC_COLOR << 4),
             vec_t1, 1, pmc_zmask);
    /* rowstep 2 with NO table is the plain alternate-row sample, and it is
     * kept as a vector of its own: the merge is a look decision this section
     * records with its cost, so the arm it replaced has to stay measurable
     * and testable rather than being deleted by the change. */
    pmc_tile(pmc_tiles + (VEC_TILE << 4), pmc_pairs + (VEC_COLOR << 4),
             vec_t2, 2, 0);
    pmc_tile(pmc_tiles + (VEC_TILE << 4), pmc_pairs + (VEC_COLOR << 4),
             vec_t3, 2, pmc_zmask);
    memset(vec_pl, 0, sizeof vec_pl);
    pmc_pack_pl(vec_band, vec_pl, 8, pmc_planar, PMC_PL_STRIDE);
    memset(vec_m1, 0, sizeof vec_m1);
    pmc_pack_1(vec_band, vec_m1, 8, pmc_mono2, 0, PMC_PL_STRIDE);

    /* THE SPRITE LAYER, over a band that already holds something, because a
     * merge that overwrote its background would pass every test written
     * against an empty one. Two cases, and the second is the awkward one:
     * an ODD destination nibble, flipx, and a NEGATIVE row step (flipy on the
     * CGA layout's alternate rows) all at once. */
    pmc_brev_init();
    for (r = 0; r < 8; r++)
        for (c = 0; c < PMC_BAND_ROW; c++)
            vec_sb0[r * PMC_BAND_ROW + c] =
                (unsigned char) ((r * 53 + c * 17 + 7) & 0xFF);

    memcpy(vec_s1, vec_sb0, sizeof vec_s1);
    pmc_sprite(pmc_sprites + (VEC_SPR << 6), pmc_pal + VEC_SCOL * 4,
               vec_s1 + 8, 4, 8, 0, pmc_brev, 0);

    memcpy(vec_s2, vec_sb0, sizeof vec_s2);
    pmc_sprite(pmc_sprites + (VEC_SPR << 6) + 15 * 4, pmc_pal + VEC_SCOL * 4,
               vec_s2 + 9, -8, 4, 3, pmc_brev, 0);

    /* ...AND THE SAME TWO WITH THE ROW MERGE ON, which is what the CGA layout
     * ships (SPEC.md 91). They are vectors of their own and not replacements:
     * the sampled arm stays reachable - pmc_band_sprites asks for it on the
     * one row that has no partner, and the whole sampled picture is still one
     * pv_merge away - so both arms have to stay testable.
     *
     * The second is the awkward one twice over: an odd destination nibble,
     * flipx and a NEGATIVE row step, so the merge's +-4 to the dropped row is
     * exercised with its sign the other way. Neither reaches source row 15's
     * missing partner: forwards the last drawn row is 6 and backwards it is 9. */
    memcpy(vec_s3, vec_sb0, sizeof vec_s3);
    pmc_sprite(pmc_sprites + (VEC_SPR << 6), pmc_pal + VEC_SCOL * 4,
               vec_s3 + 8, 8, 4, 0, pmc_brev, pmc_zmask);

    memcpy(vec_s4, vec_sb0, sizeof vec_s4);
    pmc_sprite(pmc_sprites + (VEC_SPR << 6) + 15 * 4, pmc_pal + VEC_SCOL * 4,
               vec_s4 + 9, -8, 4, 3, pmc_brev, pmc_zmask);

    f = fopen(path, "w");
    if (!f) {
        fail("cannot write the composer vectors");
        return;
    }
    fprintf(f,
        "; GENERATED by apps/paccman/hosttest/pmcuitest.c - do not edit.\n"
        "; The composer's inputs and the answers its C twins give, for\n"
        "; apps/paccman/hosttest/pmcbandtest.asm to compare the SHIPPING\n"
        "; apps/paccman/pmcband.inc against on a real x86 with SS != DS.\n"
        "; Tile 0x%02X in colour block 0x%02X; a deterministic filler band.\n\n",
        VEC_TILE, VEC_COLOR);
    emit(f, "pv_tile_src", pmc_tiles + (VEC_TILE << 4), 16);
    emit(f, "pv_pairs",    pmc_pairs + (VEC_COLOR << 4), 16);
    emit(f, "pv_t1_exp",   vec_t1, 8 * PMC_BAND_ROW);
    emit(f, "pv_t2_exp",   vec_t2, 4 * PMC_BAND_ROW);
    emit(f, "pv_zmask",    pmc_zmask, 256);
    emit(f, "pv_t3_exp",   vec_t3, 4 * PMC_BAND_ROW);
    emit(f, "pv_band",     vec_band, 8 * PMC_BAND_ROW);
    emitw(f, "pv_planar",  pmc_planar, 256);
    emit(f, "pv_pl_exp",   vec_pl, 4 * PMC_PL_STEP);
    emit(f, "pv_mono2",    pmc_mono2, 512);
    emit(f, "pv_m1_exp",   vec_m1, 8 * PMC_PL_STRIDE);
    emit(f, "pv_spr_src",  pmc_sprites + (VEC_SPR << 6), 64);
    emit(f, "pv_spr_pal",  pmc_pal + VEC_SCOL * 4, 4);
    emit(f, "pv_brev",     pmc_brev, 256);
    emit(f, "pv_spr_band", vec_sb0, 8 * PMC_BAND_ROW);
    emit(f, "pv_s1_exp",   vec_s1, 8 * PMC_BAND_ROW);
    emit(f, "pv_s2_exp",   vec_s2, 8 * PMC_BAND_ROW);
    emit(f, "pv_s3_exp",   vec_s3, 8 * PMC_BAND_ROW);
    emit(f, "pv_s4_exp",   vec_s4, 8 * PMC_BAND_ROW);
    fclose(f);
    printf("pmcuitest: wrote the composer vectors to %s\n", path);
}

/* ==========================================================================
 * THE TABLE CHECKS - the generated file against what it claims to be
 * ========================================================================*/
static void check_tables(void)
{
    int i, dots = 0, pills = 0, doors = 0;

    for (i = 0; i < 31 * 28; i++) {
        if (pmc_maze[i] == PMC_TILE_DOT)  dots++;
        if (pmc_maze[i] == PMC_TILE_PILL) pills++;
        if (pmc_maze[i] == PMC_TILE_DOOR) doors++;
    }
    if (dots != 240)  fail("the maze does not hold 240 dots");
    if (pills != 4)   fail("the maze does not hold 4 energizer pills");
    if (doors != 2)   fail("the maze does not hold the 2 ghost-house doors");

    /* The prelude's MELODY is voice 1 of the dump and its bass voice 0, and
     * the two have been the wrong way round once. Assert the melody's first
     * two notes BY NAME so they cannot be swapped back silently. */
    if (pmc_snd_prelude_hz1[0] != 539 || pmc_snd_prelude_hz1[8] != 1078)
        fail("the prelude MELODY is not 539 then 1078 Hz on voice 1");
    if (pmc_snd_prelude_hz0[0] != 67)
        fail("the prelude BASS is not 67 Hz on voice 0");

    /* Pixel 0 of every colour block is the arcade's transparent index and is
     * black here; nothing else in a block may be, or a tile would vanish. */
    for (i = 0; i < 32; i++)
        if (pmc_pal[i * 4] != 0) {
            fail("a colour block's pixel 0 is not black");
            break;
        }
    if (strcmp(PMC_ROM_PIN, "0f5ec5a384c1988d9889046d92e615219e1cf3b4"))
        fail("pmc_rom.c does not carry the pinned reference commit");
}

/* ==========================================================================
 * THE DRIVE
 * ========================================================================*/
static void drive_layout(const char *name, int w, int h, int bpp, int kind,
                         int obscured, int expect_path, int expect_step)
{
    void *win;

    hg_screen(w, h, bpp, kind);
    win = os88_main();
    if (win == 0) {
        fail("os88_main() refused the window");
        return;
    }
    hg_obscured = obscured;

    /* THE LAYOUT BEFORE THE FIRST WRITE, because pmc_text_ink reads pmc_bpp and
     * pmc_bpp is pmc_layout's. On the machine the kernel's first W_PAINT always
     * precedes any pmc_new_game (which only a key or a menu command reaches),
     * so this models the real order rather than relaxing it: without it
     * pmc_game_init here writes PLAYER ONE against the PREVIOUS
     * configuration's depth and the identical-rewrite row below disagrees with
     * it. It marks nothing and is idempotent, so no cost row moves. */
    pmc_layout(win);

    /* THE LAYOUT ROWS WANT THE MAZE and the program opens on the attract
     * screen, so the game is started here the way `N` starts it. These rows
     * are about the RENDERER - the busiest picture the program has, the
     * compare-then-write rule against the score and lives strips, and the
     * three blit paths - and the attract screen is 36 mostly-empty bands.
     * drive_intro() below is what exercises the screen this opens on. */
    pmc_new_game();

    /* PLAYER ONE IS ONE OF THE TWO COLOURED LABELS ON THE GAME SCREEN and it
     * takes the attract screen's rule: the reference writes it in INKY's cyan
     * 5, which the mono class table resolves to a 50% checkerboard, so on a
     * 1bpp display it goes out in COLOR_DEFAULT (pmc_text_ink). This row is
     * here because the rule shipped covering ONE screen - drive_intro_mono
     * asserted the four ghost labels and nothing asserted these two, so
     * PLAYER ONE and GAME OVER stayed dithered on both 1bpp adapters. Checked
     * on every configuration, so "white on 1bpp" and "the arcade's colour
     * everywhere else" are both under test. GAME OVER is drive_intro_mono's,
     * which is the row that can reach it. */
    if (pmc_cram[(14 << PMC_VSHIFT) + 9] != (bpp == 1 ? PMC_COLOR_DEFAULT
                                                      : 0x05))
        fail("PLAYER ONE is not in pmc_text_ink's colour for this display");

    cost_reset();
    os88_paint(win);

    printf("\n%s: %dx%d bpp %d -> field at (%d,%d), %d screen rows, "
           "step %d, band %d rows, path %s\n",
           name, w, h, bpp, pmc_fx, pmc_fy, pmc_fh, pmc_step, pmc_rows,
           pmc_path == PMC_P_BLITP ? "BLITP"
             : pmc_path == PMC_P_BLIT1 ? "BLIT1" : "BLIT4");

    if (pmc_path != expect_path)
        fail("the frame took the wrong blit path");
    if (pmc_step != expect_step)
        fail("the frame chose the wrong row step");
    if (pmc_fx & 7)
        fail("the field's x is not a multiple of 8 - BLITP and BLIT1 refuse");

    cost_row("full repaint");
    audit("full repaint");

    /* THE DEFAULT WINDOW HAS NO LETTERBOX AND MUST FILL NOTHING. PMC_WIN_W
     * gives a content exactly PMC_FIELD_W wide and PMC_WIN_H one exactly
     * pmc_fh deep, and os88_wm_minsize pins it there - so every pixel of the
     * content is inside a band. Filling the whole content "once, before the
     * bands" still writes 64,512 pixels that a band covers a moment later,
     * which is PERFORMANCE.md rule 2's double-draw and is invisible in an
     * emulator. It is visible here, in the fillpx column. */
    if (pmc_n_fillpx != 0)
        fail("the default window filled a letterbox it does not have");

    /* THE DAMAGE MODEL, and it is the compare-then-write rule of SPEC.md 91
     * that is under test: game_update_tiles rewrites the score strip, the
     * lives row and the fruit list EVERY GAME TICK with the values they
     * already hold, so a write that changes nothing must mark nothing or four
     * bands ride in every frame for free.
     *
     * NOT a whole re-init: pmc_game_init() clears both RAMs and then redraws
     * them, so it passes THROUGH a state that really is different and really
     * does dirty 32 bands. That is correct and it happens once a game. */
    cost_reset();
    pmc_flush(win, 0);
    cost_row("nothing written at all");
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a frame with nothing written still blitted");

    pmc_vid_quad(2, 34, 0x09, 0x20);     /* one life, so the strip exists */
    pmc_flush(win, 0);

    cost_reset();
    pmc_vid_color_text(9, 0, PMC_COLOR_DEFAULT, "HIGH SCORE");
    pmc_vid_color_text(9, 14, pmc_text_ink(0x05), "PLAYER ONE");
    pmc_vid_color_text(11, 20, 0x09, "READY!");
    pmc_vid_quad(2, 34, 0x09, 0x20);
    pmc_vid_color(1, 6, PMC_COLOR_DOT);
    pmc_vid_color_tile(13, 15, 0x18, PMC_TILE_DOOR);
    if (pmc_dmin[0] <= pmc_dmax[0] || pmc_dmin[6] <= pmc_dmax[6]
        || pmc_dmin[14] <= pmc_dmax[14] || pmc_dmin[15] <= pmc_dmax[15]
        || pmc_dmin[20] <= pmc_dmax[20] || pmc_dmin[34] <= pmc_dmax[34])
        fail("re-writing identical tiles marked a band dirty");
    pmc_flush(win, 0);
    cost_row("identical rewrite");
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a frame that changed nothing still blitted");
    audit("identical rewrite");

    /* ONE TILE. The band it is in, and no other, must go down. */
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win, 0);
    cost_row("one tile changed");
    if (hc_blitp + hc_blit4 + hc_blit1 != 1)
        fail("one changed tile was not exactly one blit");
    audit("one tile changed");

    /* TWO SPANS. Two tiles twenty-five columns apart in one row are the
     * energizer blink's own shape (columns 1 and 26 of rows 6 and 26), and a
     * row carries TWO spans precisely so that they are two narrow bands and
     * not one 26-column one: 24 recomposed columns that did not change is
     * ~33 ms of a ~110 ms 1bpp play frame (SPEC.md 91, pmc_vid.c's PMC_DGAP
     * block). Two calls of one tile each, never one call of twenty-six. */
    cost_reset();
    pmc_vid_color(1, 6, 0x00);
    pmc_vid_color(26, 6, 0x00);
    pmc_flush(win, 0);
    cost_row("two ends of one row");
    if (hc_blitp + hc_blit4 + hc_blit1 != 2)
        fail("the two ends of one row were not two separate blits");
    if (pmc_n_tiles != 2)
        fail("the two spans composed more than their own two tiles");
    audit("two ends of one row");

    /* ...and a gap of ONE clean column is swallowed instead, because a second
     * gfx call (814 us) costs more than the one wasted column (~1,050 us on
     * the 1bpp path) does not - PMC_DGAP is where the two arms cross. */
    cost_reset();
    pmc_vid_color(10, 8, 0x00);
    pmc_vid_color(12, 8, 0x00);
    pmc_flush(win, 0);
    cost_row("a one-column gap");
    if (hc_blitp + hc_blit4 + hc_blit1 != 1)
        fail("a one-column gap was not swallowed into one band");
    if (pmc_n_tiles != 3)
        fail("the swallowed gap did not compose columns 10..12");
    audit("a one-column gap");

    /* A PARTIAL EXPOSE - a menu dropped over the top three tile rows and was
     * dismissed. This is the ORDINARY case (kernel/menu.inc repaints through
     * wm_paint_dmg, and so does a drag of another window across a corner or a
     * toast going away), and it is the whole reason os88_paint asks
     * os88_wm_damage at all: throwing the answer away recomposes all 36
     * bands, which the bench prices at 36 x 69.78 = 2.51 SECONDS of XT for
     * three rows that owe 209 ms. WF_OWNBG is set, so a partial answer is
     * what the kernel really gives this window (SPEC.md 11.90.2). */
    cost_reset();
    hg_dmg_whole = 0;
    hg_dmg_x1 = pmc_fx;
    hg_dmg_y1 = pmc_fy;
    hg_dmg_x2 = pmc_fx + PMC_FIELD_W - 1;
    hg_dmg_y2 = pmc_fy + (3 << pmc_rsh) - 1;
    os88_paint(win);
    cost_row("partial expose, 3 rows");
    if (pmc_n_bands != 3)
        fail("a partial expose did not draw exactly the three damaged bands");
    audit("partial expose, 3 rows");

    /* ...and one that lands on a single tile is a single band. */
    cost_reset();
    hg_dmg_x1 = pmc_fx + 8 * 5;
    hg_dmg_y1 = pmc_fy + (7 << pmc_rsh);
    hg_dmg_x2 = hg_dmg_x1 + 7;
    hg_dmg_y2 = hg_dmg_y1 + (1 << pmc_rsh) - 1;
    os88_paint(win);
    cost_row("partial expose, 1 tile");
    if (pmc_n_bands != 1 || pmc_n_tiles != 1)
        fail("a one-tile expose was not one band of one tile");
    audit("partial expose, 1 tile");

    /* ...and an EMPTY rect is SPEC.md 11.90.2's "draw nothing at all". */
    cost_reset();
    hg_dmg_x1 = 1;
    hg_dmg_x2 = 0;                      /* x1 > x2 */
    os88_paint(win);
    cost_row("empty damage rect");
    if (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe != 0)
        fail("an empty damage rect still drew something");
    hg_dmg_whole = 1;
}

/* ==========================================================================
 * THE TICK PATH, DRIVEN
 *
 * pmc_frame() IS the worker's whole body (paccman.c), so the harness can run
 * the game by advancing the modelled OS clock and calling it - no task, no
 * sleep, and every frame boundary exactly where the machine puts one.
 * ========================================================================*/
static void tick_n(void *win, int n, const char *where)
{
    while (n-- > 0) {
        hg_ticks_v++;
        pmc_frame(win);
        if (audit(where))
            return;                     /* one report is enough */
    }
}

/* drive_play - a whole round from the prelude to open play, auditing EVERY
 * frame. A missed dirty mark - a sprite that moved and did not mark its old
 * rectangle, a tile the compare-then-write rule let through - is a pixel
 * difference here and is invisible in a screendump (PERFORMANCE.md). */
/* hold_dir - what a player's hand does: exactly one direction key down.
 *
 * Pac-Man with NOTHING held runs until a wall and then STOPS, because
 * input_dir's default is his current direction and can_move refuses it - and
 * that is the reference's behaviour, not a defect. So a drive that wants dots
 * eaten has to steer, which is also what makes the drive exercise the input
 * path rather than only the renderer. */
static void hold_dir(int d)
{
    int k;

    for (k = 0; k < 256; k++)
        hg_down[k] = 0;
    hg_down[d == PMC_DIR_UP ? PMC_SC_UP : d == PMC_DIR_DOWN ? PMC_SC_DOWN
            : d == PMC_DIR_LEFT ? PMC_SC_LEFT : PMC_SC_RIGHT] = 1;
}

static void drive_play(void *win)
{
    int i, guard;
    double repaint_ms;

    printf("\n play, VGA:\n");

    /* the prelude and READY!, then the round proper. The first frame draws
     * the whole field, which is what every play frame below is priced
     * against. */
    cost_reset();
    tick_n(win, 1, "first frame");
    cost_row("first frame (prelude)");
    repaint_ms = hc_last_ms_ms();

    for (guard = 0; guard < 400 && (pmc_freeze & PMC_FZ_READY) == 0; guard++)
        tick_n(win, 1, "waiting for READY");
    if (guard >= 400)
        fail("the round never reached READY!");
    cost_reset();
    tick_n(win, 1, "READY frame");
    cost_row("READY! frame");

    for (guard = 0; guard < 400 && pmc_freeze; guard++)
        tick_n(win, 1, "waiting for the round");
    if (guard >= 400)
        fail("the round never unfroze");

    /* ...and now the ordinary case: five actors moving, the score strip and
     * the pill blink rewritten every game tick, and only the bands that
     * changed going down.
     *
     * THE BUDGET IS A SHARE OF THE FULL REPAINT AND NOT A ROUND NUMBER.
     * What a play frame must never do is approach the cost of redrawing the
     * whole field - that is the regression this row exists to catch, and a
     * fixed millisecond ceiling would have to be re-derived every time a
     * bench term moves. The share is generous on purpose: the expensive play
     * frames are the ones where the energizer pills blink, which the two-span
     * damage model turns into four narrow bands rather than two full-width
     * ones (SPEC.md 91).
     *
     * THE STRUCTURAL BOUND IS THE TILE COUNT, and it is the tile count
     * BECAUSE a row carries two spans: splitting one wide band into two
     * narrow ones RAISES the band count and lowers the work, which is the
     * whole point of the split, so a band ceiling alone would now punish the
     * cheaper frame. A whole-field recompose is 1,008 tiles and 36 bands; a
     * play frame that has lost a damage mark and gone whole-field trips the
     * tile bound first, at 140. The band and call ceilings stay as the guard
     * against the OTHER failure - a row split into spans without bound - and
     * 72 is what two spans on all 36 rows would be, so 24 is a third of the
     * way there and well above the 18 an energizer frame really draws. */
    {
        double budget = repaint_ms * 0.30;
        double worst = 0.0;
        int worst_i = -1;

        for (i = 0; i < 24; i++) {
            cost_reset();
            tick_n(win, 1, "play frame");
            if (i == 12)
                cost_row("play frame");
            if (hc_last_ms_ms() > worst) {
                worst = hc_last_ms_ms();
                worst_i = i;
            }
            if (pmc_n_bands > 24)
                fail("a play frame drew more than twenty-four bands");
            if (pmc_n_tiles > 140)
                fail("a play frame composed more than 140 tiles");
            if (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe > 26)
                fail("a play frame made more than twenty-six gfx calls");
        }
        printf("  %-26s frame %d at %.1f ms, of a %.1f ms budget "
               "(30%% of a full repaint)\n",
               "worst of 24", worst_i, worst, budget);
        if (worst > budget)
            fail("a play frame cost more than 30% of a whole repaint");
    }

    /* THE COMPARE-THEN-WRITE RULE, under the thing that stresses it: the pill
     * blink flips columns 1 and 26 of two rows every eight game ticks and the
     * score strip is rewritten every tick with the value it already holds. A
     * frame that marks the score band without changing it is four bands of
     * pure waste, and this is where it would show. */
    {
        unsigned bands = 0;
        for (i = 0; i < 18; i++) {
            cost_reset();
            tick_n(win, 1, "pill blink window");
            bands += pmc_n_bands;
        }
        printf("  %-26s %u band(s) over 18 frames\n", "bands drawn", bands);
        if (bands > 18 * 14)
            fail("a play frame draws more than fourteen bands on average");
    }

    /* A DOT EATEN clears a maze tile and moves the score strip, which is a
     * band of its own two rows from the top. */
    {
        int dots, d;
        for (i = 0; i < 60; i++) {
            if (!pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC],
                              pmc_adir[PMC_A_PAC], 1))
                for (d = 0; d < 4; d++)
                    if (pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC],
                                     d, 1)) {
                        hold_dir(d);
                        break;
                    }
            dots = pmc_dots_eaten;
            cost_reset();                       /* ONE frame is priced, so
                                                 * the row is a frame's cost
                                                 * and not a run's */
            tick_n(win, 1, "eating a dot");
            if (pmc_dots_eaten != dots)
                break;
        }
        if (i >= 60)
            fail("sixty steered frames ate no dot at all");
        else
            cost_row("the frame a dot went in");
        printf("  %-26s %d dot(s) eaten in the round so far\n",
               "dots", pmc_dots_eaten);
        for (d = 0; d < 256; d++)
            hg_down[d] = 0;
    }

    /* THE TWO BOTTOM STRIPS ARE GATED ON THEIR OWN STATE (pmc_game.c's
     * pmc_shlives/pmc_shround), so what has to be checked is that the gate
     * still REPAINTS them when the state really moves. The audit cannot see
     * this one: it recomposes the glass from the two RAMs, so a strip that
     * was never written into the RAM at all agrees with the glass perfectly.
     *
     * A life lost must take the last lit reserve quad out and must mark the
     * band it is on; putting the life back must bring it back. */
    {
        int col = 2 + 2 * (pmc_lives - 1);
        int at  = (34 << PMC_VSHIFT) + col;

        if (pmc_lives < 1)
            fail("the drive reached the reserve-strip check with no lives");
        if (pmc_cram[at] != PMC_COLOR_PACMAN)
            fail("the reserve-life strip was never drawn at all");

        pmc_lives--;
        cost_reset();
        tick_n(win, 1, "a life lost");
        if (pmc_cram[at] != 0)
            fail("a life lost did not take its reserve quad out");
        if (pmc_n_bands == 0)
            fail("a life lost marked no band at all");

        pmc_lives++;
        tick_n(win, 1, "the life back");
        if (pmc_cram[at] != PMC_COLOR_PACMAN)
            fail("the reserve quad did not come back with the life");
    }
}

/* drive_lockbreak - THE WORKER'S FLUSH MAY DROP THE GFX LOCK, and this is the
 * only thing that exercises the arm that does it.
 *
 * The round-won flash recolours all 31 playfield bands at their full width
 * about eleven times over four seconds, and one flip in ONE lock hold is
 * 31 x ~70 ms = ~2.2 seconds during which nothing else on the machine can
 * draw (SPEC.md 20.6 rule 3, apps/cc/os88.h). The worker's arm therefore
 * unlocks, yields and re-locks every PMC_HOLD_BANDS bands. What is checked:
 *
 *  - it still draws EVERY band it was owed, so a chunked flush and a whole
 *    one paint the same picture (the audit is what says so);
 *  - it really breaks the hold, and the number of breaks is the band count
 *    over PMC_HOLD_BANDS rather than a single token unlock;
 *  - the CALLBACK arm does not, because a key, a menu command and os88_paint
 *    all arrive inside a kernel callback that holds the lock on our behalf;
 *  - and the clip region is RE-ARMED after each re-lock, because it died at
 *    the unlock and a band drawn without it would go across a covering
 *    window's pixels. */
/* What the UI task does in the window the break opens: raise the About card,
 * the way Apple > About PaccMan does. It is os88_about's own half of the
 * interlock (the widget only draws; the flag is ours), reduced to the byte. */
static void hg_raise_about(void)
{
    pmc_about_up = 1;
}

static void drive_lockbreak(void *win)
{
    unsigned breaks, want;

    printf("\n the worker's lock hold:\n");

    /* the whole field, taken the way a CALLBACK takes it: one hold. */
    hg_obscured = 0;
    pmc_dirty_all();
    cost_reset();
    pmc_flush(win, 0);
    if (pmc_n_bands != 36)
        fail("a callback flush did not draw all 36 bands");
    if (hc_unlock != 0 || hc_yield != 0)
        fail("a callback flush dropped the gfx lock it does not own");
    audit("callback flush, one hold");
    printf("  %-26s %u band(s), %u break(s)\n",
           "callback (brk = 0)", pmc_n_bands, hc_unlock);

    /* ...and the way the WORKER takes it: chunked. */
    pmc_dirty_all();
    cost_reset();
    pmc_flush(win, 1);
    breaks = hc_unlock;
    want = (36 - 1) / PMC_HOLD_BANDS;   /* the break BEFORE each chunk after
                                         * the first: 8 for 36 bands of 4 */
    if (pmc_n_bands != 36)
        fail("a worker flush did not draw all 36 bands");
    if (breaks != want)
        fail("the worker flush did not break its hold once per chunk");
    if (hc_lock != breaks || hc_yield != breaks)
        fail("a break did not lock, unlock and yield in equal numbers");
    audit("worker flush, chunked");
    printf("  %-26s %u band(s), %u break(s) of %d\n",
           "worker (brk = 1)", pmc_n_bands, breaks, PMC_HOLD_BANDS);

    /* AND THE REGION IS RE-ARMED AFTER EVERY RE-LOCK. Covered, so pmc_flush
     * arms one; the region dies at each unlock, so a flush that did not ask
     * again would draw its remaining bands unclipped. */
    hg_obscured = 1;
    pmc_dirty_all();
    cost_reset();
    pmc_flush(win, 1);
    if (hc_clip < hc_unlock + 1)
        fail("the worker flush did not re-arm the clip region after a break");
    printf("  %-26s %u arm(s) for %u break(s)\n",
           "clip re-armed", hc_clip, hc_unlock);
    hg_obscured = 0;
    pmc_dirty_all();
    cost_reset();
    pmc_flush(win, 0);
    audit("after the covered flush");

    /* AND THE BREAK RE-TESTS pmc_about_up, which is the whole reason the flag
     * is re-read after a re-lock and not only on entry. The card is drawn by
     * os88_about - a UI callback - and the break is precisely the window it
     * can run in: worker unlocks at band 4, the UI task takes the lock to drop
     * the menu, the user picks About PaccMan, the card is painted, the lock is
     * released, the worker re-locks. Without the test it blits bands 5..35
     * through the card and nothing repaints it, because the kernel sends no
     * W_PAINT for a package's own overdraw. Driven from the yield hook, which
     * is exactly where the UI task gets in. */
    pmc_about_up = 0;
    pmc_dirty_all();
    cost_reset();
    hg_on_yield = hg_raise_about;
    pmc_flush(win, 1);
    hg_on_yield = 0;
    if (pmc_n_bands != PMC_HOLD_BANDS)
        fail("the worker went on blitting after the About card went up");
    if (!pmc_dirty_any())
        fail("the stopped flush cleaned bands it never drew");
    printf("  %-26s %u band(s) of 36 before the card stopped it\n",
           "About raised mid-break", pmc_n_bands);
    pmc_about_up = 0;
    pmc_dirty_all();
    pmc_flush(win, 0);
    audit("after the card stopped a break");

    /* WHAT THE FIRST CHUNK ACTUALLY HOLDS, IN MILLISECONDS, and it is not four
     * bands. os88_worker brackets the WHOLE of pmc_frame in one
     * lock/unlock - poll, up to PMC_CATCHUP_MAX OS ticks of game, the sound
     * frame, and only then the flush - and pmc_flush_laid breaks after
     * PMC_HOLD_BANDS bands, so chunk one is the game logic PLUS four bands.
     * At 18.6 ms a game_tick and up to 3.3 game ticks an OS tick that is ~130
     * ms of computation on top of ~279 ms of four full-width VGA bands.
     *
     * apps/cc/os88.h's rule 3 - "a worker that computes under the lock wedges
     * the machine" - is about exactly this, and the chunking answers only the
     * drawing half of it. The number is printed and gated here rather than
     * argued in a comment; SPEC.md 91 quotes this row. The round-won flash is
     * the case, so the whole field is owed. */
    {
        double hold;

        pmc_about_up = 0;
        pmc_paused = 0;
        pmc_dirty_all();
        cost_reset();
        hg_ticks_v += PMC_CATCHUP_MAX;  /* THE WORST CASE, not the ordinary
                                         * one: a frame that is late carries
                                         * PMC_CATCHUP_MAX OS ticks of game -
                                         * 3.3 game ticks each - and that is
                                         * the half of the hold this row is
                                         * for. An on-time frame reads ~332 ms
                                         * where this reads ~410 */
        os88_gfx_lock();                /* os88_worker's own bracket */
        pmc_frame(win);
        os88_gfx_unlock();
        hold = hc_hold_worst / 1000.0;
        if (pmc_n_gtick == 0)
            fail("the worker's frame ran no game tick, so the hold is not the "
                 "one the machine sees");
        if (hold < pmc_n_gtick * T_LOGIC / 1000.0)
            fail("the measured lock hold is under the logic it contains - the "
                 "bracket is not around the tick");
        if (hold > PMC_HOLD_MS)
            fail("the worker's worst uninterruptible lock hold is over "
                 "SPEC.md 91's stated bound");
        printf("  %-26s %.1f ms  (%u game tick(s) + %d band(s), bound %d ms)\n",
               "worst hold, one chunk", hold, pmc_n_gtick, PMC_HOLD_BANDS,
               PMC_HOLD_MS);
        pmc_dirty_all();
        pmc_flush(win, 0);
        audit("after the timed worker frame");
    }
}

/* drive_input - the one divergence, driven from both sides.
 *
 * A TAP is os88_onkey alone: no key is held when the frame polls, so only the
 * latch carries it. A HOLD is hg_down, which is what os88_key_down answers.
 * The reference's rule is that neither BUFFERS: a hold released before the
 * junction is forgotten, and this is where a buffered turn would show up. */
static void drive_input(void *win)
{
    int d, want, guard;

    printf("\n input:\n");

    /* a TAP: one press, nothing held, one frame - and it must turn him. */
    want = -1;
    for (d = 0; d < 4; d++)
        if (d != pmc_adir[PMC_A_PAC]
            && pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC], d, 1))
            want = d;
    if (want < 0) {
        fail("no legal turn to tap - the drive cannot test the latch");
        return;
    }
    os88_onkey(0, want == PMC_DIR_UP ? PMC_SC_UP
                : want == PMC_DIR_DOWN ? PMC_SC_DOWN
                : want == PMC_DIR_LEFT ? PMC_SC_LEFT : PMC_SC_RIGHT, win);
    if (pmc_latch == 0)
        fail("os88_onkey did not latch the press");
    tick_n(win, 1, "tap");
    if (pmc_adir[PMC_A_PAC] != want)
        fail("a one-frame tap did not turn Pac-Man");
    if (pmc_latch != 0)
        fail("the latch outlived the poll that folded it in");
    if (pmc_held == 0)
        fail("the poll did not fold the latched press into the held state");
    printf("  tap %s -> Pac-Man turned, latch cleared\n",
           want == PMC_DIR_UP ? "up" : want == PMC_DIR_DOWN ? "down"
           : want == PMC_DIR_LEFT ? "left" : "right");

    /* ...and the next frame with nothing held forgets it: a tap is exactly
     * ONE frame of held, which is the whole of the divergence. */
    tick_n(win, 1, "after the tap");
    if (pmc_held != 0)
        fail("a tap lasted more than one frame");

    /* A HOLD RELEASED BEFORE THE JUNCTION IS FORGOTTEN. Hold a direction that
     * is blocked here, let go, and run on: he must never take it. A buffered
     * turn - which apps/pacman has and this port deliberately does not - would
     * turn him the moment the way opened. */
    for (d = 0; d < 4; d++)
        if (!pmc_can_move(pmc_ax[PMC_A_PAC], pmc_ay[PMC_A_PAC], d, 1))
            break;
    if (d == 4) {
        printf("  (no blocked direction here to test the release with)\n");
        return;
    }
    hg_down[d == PMC_DIR_UP ? PMC_SC_UP : d == PMC_DIR_DOWN ? PMC_SC_DOWN
            : d == PMC_DIR_LEFT ? PMC_SC_LEFT : PMC_SC_RIGHT] = 1;
    tick_n(win, 1, "held into a wall");
    if (pmc_adir[PMC_A_PAC] == d)
        fail("Pac-Man turned into a blocking tile");
    hg_down[d == PMC_DIR_UP ? PMC_SC_UP : d == PMC_DIR_DOWN ? PMC_SC_DOWN
            : d == PMC_DIR_LEFT ? PMC_SC_LEFT : PMC_SC_RIGHT] = 0;
    for (guard = 0; guard < 40; guard++) {
        tick_n(win, 1, "after the release");
        if (pmc_adir[PMC_A_PAC] == d) {
            fail("a released hold was BUFFERED - the port has no buffered turn");
            return;
        }
    }
    printf("  hold released before a junction -> never taken (no buffer)\n");
}

/* ==========================================================================
 * THE ATTRACT SCREEN, TICK BY TICK
 *
 * intro_tick draws at FOURTEEN event ticks and never in between, so what has
 * to be checked is not "does it look right" but "did each of the fourteen
 * things appear on its own tick and not before". The harness steps the game
 * clock one tick at a time through pmc_step_tick() - which is the worker's own
 * per-tick dispatch and not a copy of it (paccman.c) - and reads the two RAMs
 * directly.
 *
 * THE TILE CODES ARE WRITTEN OUT AGAIN HERE, not read from the program's own
 * conv_char: this is meant to be an independent second reader of the reference
 * (pacman.c 1031-1042), so a conv_char that lost a case would agree with
 * itself and fail here.
 * ========================================================================*/
static int tile_at(int x, int y) { return pmc_vram[(y << PMC_VSHIFT) + x]; }
static int col_at(int x, int y)  { return pmc_cram[(y << PMC_VSHIFT) + x]; }

static int hg_conv(int c)
{
    if (c == ' ')  return 0x40;
    if (c == '/')  return 58;
    if (c == '-')  return 59;
    if (c == '"')  return 38;
    if (c == '!')  return 'Z' + 1;
    return c;
}

/* text_is - does row y read `s` from column x, and (when colour >= 0) in that
 * colour? */
static int text_is(int x, int y, const char *s, int colour)
{
    while (*s) {
        if (tile_at(x, y) != hg_conv((unsigned char) *s))
            return 0;
        if (colour >= 0 && col_at(x, y) != colour)
            return 0;
        s++;
        x++;
    }
    return 1;
}

static void want_text(int x, int y, const char *s, int colour, const char *what)
{
    if (!text_is(x, y, s, colour)) {
        printf("pmcuitest: FAIL - %s is not on the attract screen at (%d,%d)"
               " (tile %02X)\n", what, x, y, tile_at(x, y));
        hg_fails++;
    }
}

/* one modelled frame of exactly one game tick: the poll a frame does, then the
 * tick. Splitting them the way pmc_frame does is what makes the any-key latch
 * and the direction latch behave here exactly as they do on the machine. */
static void intro_step(void)
{
    pmc_poll_input();
    pmc_step_tick();
}

/* run until `to` ticks have elapsed since the attract screen started.
 *
 * IT COUNTS TICKS AND DOES NOT ASK pmc_since(), which is the same trap this
 * file is here to catch one level up: pmc_since saturates at 0x7FFE, so a
 * drive that waited on `since() < 40000` would spin until its guard fired and
 * report the wrong thing. The absolute target is the trigger plus the delay. */
static void intro_to(unsigned to)
{
    unsigned target = pmc_trg_lo[PMC_T_INTRO] + to;
    unsigned guard = 0;

    while (pmc_tick_lo != target) {
        intro_step();
        if (++guard > 100000) {
            fail("the attract screen never reached its event tick");
            return;
        }
    }
}

static void drive_intro(void *win)
{
    static const char *names[4] = { "-SHADOW", "-SPEEDY", "-BASHFUL",
                                    "-POKEY" };
    static const char *nicks[4] = { "BLINKY", "PINKY", "INKY", "CLYDE" };
    int i, colour, y, blank, lit;
    unsigned d;

    printf("\n the attract screen:\n");

    if (pmc_mode != PMC_MODE_INTRO)
        fail("the program did not open on the attract screen");

    /* the screen the reference draws on `now(intro.started)` (pacman.c
     * 2328-2341), before a single event tick */
    want_text(3, 0, "1UP   HIGH SCORE   2UP", PMC_COLOR_DEFAULT, "the header");
    want_text(7, 5, "CHARACTER / NICKNAME", PMC_COLOR_DEFAULT, "the heading");
    want_text(3, 35, "CREDIT  0", PMC_COLOR_DEFAULT, "the credit line");
    want_text(5, 1, "00", PMC_COLOR_DEFAULT, "the 1UP score");

    /* THE HISCORE FIELD IS ABSENT ON A FRESH INSTANCE, which is the machine's
     * own behaviour: the reference draws it only when the hiscore is above
     * zero (pacman.c 2336-2338). Nothing but spaces from column 11 to 16. */
    if (!text_is(11, 1, "      ", -1))
        fail("a fresh instance drew a hiscore field on the attract screen");

    /* ...and the four reveals, each on its own tick and not one tick early. */
    d = 30;
    for (i = 0; i < 4; i++) {
        colour = 2 * i + 1;
        y = 3 * i + 6;

        d += 30;
        intro_to(d - 1);
        if (tile_at(4, y) != PMC_TILE_SPACE)
            fail("a ghost's picture appeared a tick early");
        intro_to(d);
        if (tile_at(4, y) != PMC_TILE_GHOST || tile_at(5, y + 2) != 0xB5
            || col_at(4, y) != colour)
            fail("the 2x3 ghost block is not the reference's six tiles");

        d += 60;
        intro_to(d - 1);
        if (text_is(7, y + 1, names[i], colour))
            fail("a ghost's name appeared a tick early");
        intro_to(d);
        want_text(7, y + 1, names[i], colour, "a ghost's name");

        d += 30;
        intro_to(d);
        want_text(17, y + 1, nicks[i], colour, "a ghost's nickname");
        printf("  tick %3u/%3u/%3u  block, %-8s %s\n",
               d - 90, d - 30, d, names[i], nicks[i]);
    }

    /* the scoring legend: a dot for 10 and an energizer for 50, with the
     * arcade font's own 0x5D-0x5F "PTS" glyphs after each number. */
    d += 60;
    intro_to(d);
    if (tile_at(10, 24) != PMC_TILE_DOT || col_at(10, 24) != PMC_COLOR_DOT)
        fail("the 10 PTS dot is missing from the legend");
    if (tile_at(10, 26) != PMC_TILE_PILL || col_at(10, 26) != PMC_COLOR_DOT)
        fail("the 50 PTS energizer is missing from the legend");
    if (tile_at(12, 24) != '1' || tile_at(15, 24) != 0x5D
        || tile_at(17, 24) != 0x5F)
        fail("the '10 PTS' line is not the reference's tiles");
    if (tile_at(12, 26) != '5' || tile_at(15, 26) != 0x5D)
        fail("the '50 PTS' line is not the reference's tiles");
    printf("  tick %3u       the 10/50 PTS legend\n", d);

    /* THE PROMPT BLINKS, and it blinks off `since & 0x20` - which is why the
     * two stamps below are 630 and 662 and not 630 and 631. At 630 bit 5 of
     * the elapsed count is SET, so the reference's own first frame of the
     * prompt is the BLANK one; the text arrives at 640 and is still there at
     * 662. A prompt written against the saturating pmc_since() would show
     * whichever of the two 0x7FFE picks and then never change again. */
    d += 60;
    intro_to(d);
    blank = text_is(3, 31, "                       ",
                    PMC_COLOR_DEFAULT);
    intro_to(662);
    lit = text_is(3, 31, "PRESS ANY KEY TO START!", 3);
    if (!blank || !lit)
        fail("the PRESS ANY KEY prompt is not blinking on since & 0x20");
    printf("  tick %3u/662   PRESS ANY KEY TO START! (blank, then lit)\n", d);

    /* ...AND IT IS STILL BLINKING NINE MINUTES LATER. pmc_since() saturates at
     * 0x7FFE so that every compare in the program is cheap, and 0x7FFE & 0x20
     * is a constant: a prompt written against it freezes after 32,766 ticks -
     * about nine minutes of one attract screen, which is exactly the sort of
     * thing a machine left running in a corner does. pmc_since_lo() wraps, and
     * this run is what says the right one was used. */
    intro_to(40000);
    blank = 0;
    lit = 0;
    for (i = 0; i < 64; i++) {
        intro_step();
        if (text_is(3, 31, "PRESS ANY KEY TO START!", 3))
            lit = 1;
        if (text_is(3, 31, "                       ", PMC_COLOR_DEFAULT))
            blank = 1;
    }
    if (!lit || !blank)
        fail("the prompt stopped blinking - since() saturated (pmc_time.c)");
    printf("  tick 40000     still blinking after 40,000 ticks\n");
    pmc_flush(win, 0);
    audit("the attract screen, 40,000 ticks in");

    /* F IS NEVER THE ANY KEY. The reference gives it its own switch case with
     * no `anykey` beside it (pacman.c 806-810), so Full Screen on the attract
     * screen makes the window bigger and does not start a round. */
    hg_full = 0;
    os88_onkey(0, PMC_SC_F, win);
    if (!hg_full)
        fail("F on the attract screen did not toggle full screen");
    if (pmc_anykey)
        fail("F counted as the any key - the reference gives it its own case");
    intro_step();
    if (pmc_mode != PMC_MODE_INTRO)
        fail("F started a game from the attract screen");
    os88_onkey(0, PMC_SC_F, win);       /* ...and back */

    /* ...AND EVERY OTHER KEY IS. SPACE is an ordinary any-key here and not the
     * pause it is in play, which is the one binding where this port and
     * apps/pacman differ (SPEC.md 91). */
    os88_onkey(0, PMC_SC_SPACE, win);
    if (!pmc_anykey)
        fail("SPACE on the attract screen did not latch the any key");
    if (pmc_paused)
        fail("SPACE paused the attract screen - it is an any-key here");
    intro_step();
    if (pmc_input_on)
        fail("the any key did not disable input for the fade");
    intro_step();
    if (!pmc_black)
        fail("the any key did not start the fade-out");

    /* THE FADE IS A CUT and the black is ONE fill, however long it lasts. */
    cost_reset();
    pmc_flush(win, 0);
    if (hc_fill != 1 || hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("the fade drew bands instead of one black fill");
    cost_row("the fade-out's black");
    cost_reset();
    pmc_flush(win, 0);
    pmc_flush(win, 0);
    if (hc_fill != 0)
        fail("the black fill was sent again on a frame that was already black");

    /* ...and the game starts FADE_TICKS later, still black, and the field
     * arrives whole when the fade-in ends. */
    for (i = 0; i < PMC_FADE_TICKS + 4 && pmc_mode == PMC_MODE_INTRO; i++)
        intro_step();
    if (pmc_mode != PMC_MODE_GAME)
        fail("the any key did not start a game after the fade");
    if (!pmc_black)
        fail("the field was shown again before the fade-in ended");
    cost_reset();
    for (i = 0; i < 4 * PMC_FADE_TICKS && pmc_black; i++) {
        intro_step();
        pmc_flush(win, 0);
    }
    if (pmc_black)
        fail("the fade-in never lifted");
    pmc_flush(win, 0);
    if (pmc_n_bands < PMC_TILES_Y)
        fail("the fade-in did not repaint the whole field");
    printf("  fade cut: %u band(s) when it lifted, %d fill(s)\n",
           pmc_n_bands, hc_fill);
    audit("the field after the fade-in");
}

/* drive_intro_mono - THE SAME REVEAL WHERE COLOUR CANNOT BE CARRIED.
 *
 * Two of the four ghost colours - BLINKY's red 1 and INKY's cyan 5 - land in
 * the mono class table's 50% checkerboard, which is a fine ghost and an
 * unreadable letter (SPEC.md 39.4). So on a 1bpp display the NAME and the
 * NICKNAME are written in COLOR_DEFAULT and the ghost PICTURE keeps the
 * arcade's colour. This row is here because the alternative is a screendump
 * on the adapter nobody looks at (LESSONS.md 8) - which is how the defect got
 * as far as a wave-3 photograph in the first place. */
static void drive_intro_mono(void)
{
    void *win;
    int i;

    printf("\n the attract screen, 1bpp:\n");
    hg_screen(640, 200, 1, OS88_VID_CGA);
    win = os88_main();
    os88_paint(win);
    if (pmc_bpp != 1)
        fail("the CGA layout did not report one bit a pixel");

    intro_to(510);                      /* every name and nickname is up */
    for (i = 0; i < 4; i++) {
        int y = 3 * i + 6;
        if (col_at(7, y + 1) != PMC_COLOR_DEFAULT
            || col_at(17, y + 1) != PMC_COLOR_DEFAULT)
            fail("a ghost's label is in a colour a 1bpp screen dithers");
        if (col_at(4, y) != 2 * i + 1)
            fail("the ghost PICTURE lost its colour - only labels change");
    }
    pmc_flush(win, 0);
    audit("the attract screen on CGA");
    printf("  labels in COLOR_DEFAULT, pictures still in the arcade's four\n");

    /* ...AND THE GAME SCREEN'S TWO LABELS, which is where the rule was missing.
     * The reference colours PLAYER ONE with INKY's cyan 5 and GAME OVER with
     * BLINKY's red 1, and both of those resolve to the mono class table's 50%
     * checkerboard - so on Hercules, where the window is FULL height and there
     * is no row halving to blame, GAME OVER was a full-size smear, and it is
     * the one message the player most needs to read. GAME OVER is reached by
     * firing its trigger directly the way this file already pokes pmc_lives:
     * pmc_start() arms it for the next tick and pmc_game_tick writes the row.
     */
    pmc_new_game();
    intro_step();                       /* PMC_T_GAME fires on this tick, and
                                         * pmc_game_init disables every
                                         * sequence trigger - T_OVER included -
                                         * so it has to be armed AFTER it */
    if (!text_is(9, 14, "PLAYER ONE", PMC_COLOR_DEFAULT))
        fail("PLAYER ONE is not white on a 1bpp screen (pmc_text_ink)");
    pmc_start(PMC_T_OVER);
    intro_step();
    if (!text_is(9, 20, "GAME  OVER", PMC_COLOR_DEFAULT))
        fail("GAME  OVER is not white on a 1bpp screen (pmc_text_ink)");
    printf("  PLAYER ONE and GAME  OVER in COLOR_DEFAULT too\n");
}

/* ==========================================================================
 * THREE ARCADE VOICES, ONE SPEAKER
 *
 * The register values are asserted BY NAME - the constants the reference's own
 * effects write - and not against the committed pmc_rom.c, so a re-extraction
 * that decoded the wrong column could not make this row agree with it. That is
 * the trap wave 1 fell into once already: the prelude's melody is voice 1 and
 * its bass voice 0, and a first cut played the bass.
 * ========================================================================*/
static void want_hz(int voice, unsigned hz, const char *what)
{
    if (pmc_v_hz[voice] != hz) {
        printf("pmcuitest: FAIL - %s: voice %d is %u Hz, want %u\n",
               what, voice, pmc_v_hz[voice], hz);
        hg_fails++;
    }
}

static void drive_sound(void)
{
    int t;

    printf("\n sound:\n");

    /* THE PRELUDE'S MELODY IS VOICE 1: 539 Hz for four ticks, silent for four,
     * 1078 for four. Those numbers are the tune, and they are written here
     * rather than read from the table. */
    pmc_snd_clear();
    pmc_snd_start(0, PMC_SK_PRELUDE);
    for (t = 0; t < 12; t++) {
        pmc_snd_tick();
        if (t < 4)
            want_hz(1, 539, "the prelude's first note");
        else if (t < 8)
            want_hz(1, 0, "the rest after the prelude's first note");
        else
            want_hz(1, 1078, "the prelude's second note");
        if (t < 4 && pmc_v_hz[0] != 67)
            fail("the prelude's BASS is not 67 Hz on voice 0");
    }
    printf("  prelude        voice 1: 539, rest, 1078 Hz;  voice 0: 67 Hz\n");

    /* THE MELODY OUTRANKS ITS OWN BASS. Both voices are sounding on tick 0 -
     * bass 67 Hz at volume 14, melody 539 at volume 15 - and one speaker takes
     * the higher-numbered voice. */
    pmc_snd_clear();
    pmc_snd_start(0, PMC_SK_PRELUDE);
    pmc_snd_tick();
    pmc_snd_last = 1;                   /* force the call, so the row reads */
    pmc_snd_frame();
    if (hg_tone_hz != 539)
        fail("the speaker took the prelude's bass over its melody");
    if (hg_tone_ticks != PMC_SND_HOLD || hg_tone_prio != PMC_SND_PRIO)
        fail("the tone was not asked for with the frame's own duration");

    /* THE BASS IS HEARD WHEN THE MELODY RESTS, and that needs the VOLUME to be
     * read as well as the frequency: the bass decays 14..0 over fifteen ticks
     * while its frequency stands still, so a sampler that looked only at the
     * frequency would hold it through the whole prelude. */
    for (t = 1; t < 5; t++)
        pmc_snd_tick();
    pmc_snd_frame();
    if (hg_tone_hz != 67)
        fail("the bass was not heard through the melody's rest");
    for (t = 5; t < 15; t++)
        pmc_snd_tick();                 /* tick 14: the melody is resting AND
                                         * the bass envelope has reached 0, so
                                         * the machine is genuinely silent */
    pmc_snd_frame();
    if (hg_tone_hz != 0)
        fail("a voice at volume 0 was still sounding - volume is not read");
    printf("  priority       melody over bass, and volume 0 is silence\n");

    /* AN EFFECT INTERRUPTS THE TUNE. Voice 2 is the effects' and outranks
     * both. */
    pmc_snd_clear();
    pmc_snd_start(1, PMC_SK_WEEOOH);
    pmc_snd_start(2, PMC_SK_EATFRUIT);
    pmc_snd_tick();
    pmc_snd_frame();
    if (hg_tone_hz != pmc_v_hz[2] || pmc_v_hz[2] == 0)
        fail("an effect on voice 2 did not interrupt the siren on voice 1");

    /* THE SIX EFFECTS, each against the reference's own registers. */
    pmc_snd_clear();
    pmc_snd_start(2, PMC_SK_EATDOT1);
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x1500)
        fail("eatdot1 does not start at 0x1500");
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x1200)
        fail("eatdot1 does not fall by 0x300 a tick");
    for (t = 2; t < 6; t++)
        pmc_snd_tick();
    if (pmc_sk[2] != PMC_SK_NONE || pmc_v_hz[2] != 0)
        fail("eatdot1 did not stop and silence voice 2 after five ticks");

    pmc_snd_start(2, PMC_SK_EATDOT2);
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x0700)
        fail("eatdot2 does not start at 0x0700");
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x0A00)
        fail("eatdot2 does not rise by 0x300 a tick");
    pmc_snd_clear();

    pmc_snd_start(2, PMC_SK_EATGHOST);
    for (t = 0; t < 33; t++)
        pmc_snd_tick();
    if (pmc_sk[2] != PMC_SK_NONE)
        fail("eatghost did not stop after 32 ticks");

    pmc_snd_start(2, PMC_SK_EATFRUIT);
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x1600)
        fail("eatfruit does not start at 0x1600");
    for (t = 1; t < 11; t++)
        pmc_snd_tick();
    if (pmc_v_f[2] != 0x1600 - 10 * 0x200)
        fail("eatfruit does not fall for its first eleven ticks");
    pmc_snd_tick();
    if (pmc_v_f[2] != 0x1600 - 9 * 0x200)
        fail("eatfruit does not rise again after tick 11");
    pmc_snd_clear();

    /* THE SIREN IS A TRIANGLE OF PERIOD 24 AND IT NEVER STOPS, which is why
     * its phase cannot be `cur_tick & 31`: the slot's tick wraps at 65,536,
     * which is not a multiple of 24. Driven past that wrap here. */
    pmc_snd_start(1, PMC_SK_WEEOOH);
    pmc_snd_tick();
    if (pmc_v_f[1] != 0x1000)
        fail("the siren does not start at 0x1000");
    for (t = 1; t < 12; t++)
        pmc_snd_tick();
    if (pmc_v_f[1] != 0x1000 + 11 * 0x200)
        fail("the siren does not rise for twelve ticks");
    for (t = 12; t < 25; t++)
        pmc_snd_tick();                 /* twelve up and twelve down: the
                                         * period is 24 and it closes on the
                                         * TWENTY-FIFTH tick, because tick 0
                                         * only set the starting register */
    if (pmc_v_f[1] != 0x1000)
        fail("the siren does not come back to where it started");
    for (t = 0; t < 70000; t++)
        pmc_snd_tick();
    if (pmc_sk[1] != PMC_SK_WEEOOH)
        fail("the siren stopped - it is the one effect that never does");
    if (pmc_v_f[1] < 0x1000 || pmc_v_f[1] > 0x1000 + 12 * 0x200)
        fail("the siren wandered out of its range past the tick wrap");
    printf("  siren          triangle of 24 held across a 65,536-tick wrap\n");

    pmc_snd_clear();
    pmc_snd_start(1, PMC_SK_FRIGHT);
    pmc_snd_tick();
    if (pmc_v_f[1] != 0x0180)
        fail("the frightened warble does not start at 0x0180");
    for (t = 1; t < 8; t++)
        pmc_snd_tick();
    if (pmc_v_f[1] != 0x0180 * 8)
        fail("the frightened warble does not rise by 0x180 a tick");
    pmc_snd_tick();
    if (pmc_v_f[1] != 0x0180)
        fail("the frightened warble does not reset every eight ticks");

    /* THE DEATH TUNE IS 90 TICKS ON VOICE 2 and stops itself. */
    pmc_snd_clear();
    pmc_snd_start(2, PMC_SK_DEAD);
    for (t = 0; t < 91; t++)
        pmc_snd_tick();                 /* 90 ticks of dump, and the 91st is
                                         * the one that finds cur_tick == 90
                                         * and stops the slot */
    if (pmc_sk[2] != PMC_SK_NONE || pmc_v_hz[2] != 0)
        fail("the death tune did not stop after its 90 ticks");
    printf("  effects        eatdot1/2, eatghost, eatfruit, fright, death\n");

    /* GAME > SOUND IS A PLAIN TOGGLE, and turning it off silences the speaker
     * NOW rather than letting the granted tone run out. */
    pmc_snd_clear();
    pmc_snd_start(1, PMC_SK_WEEOOH);
    pmc_snd_tick();
    pmc_snd_frame();
    if (hg_tone_hz == 0)
        fail("the siren did not reach the speaker");
    pmc_snd_on = 0;
    pmc_snd_frame();
    if (hg_tone_hz != 0)
        fail("Game > Sound off did not silence the speaker");
    t = hc_tone;
    pmc_snd_frame();
    pmc_snd_frame();
    if (hc_tone != t)
        fail("silence is re-sent every frame - one call is enough");
    pmc_snd_on = 1;
    pmc_snd_frame();
    if (hg_tone_hz == 0)
        fail("Game > Sound on did not bring the siren back");
    pmc_snd_clear();
    pmc_snd_frame();
    printf("  toggle         off silences at once, on brings the tune back\n");
}

/* drive_cga_arms - THE ROW MERGE PRICED ON A FRAME, both ways (SPEC.md 91).
 *
 * The keep/revert bound the merge shipped under is "the CGA play frame stays
 * within 25% of the sampled path's cost", and until this row existed the only
 * arithmetic behind it was tests/pmcband/pmcbandbench.asm's BAND row - one
 * band of 28 tiles, with no packer, no blit, no sprite layer and no damage
 * model around it. A frame is what the bound is written about, so a frame is
 * what it is checked on: the same CGA instance, the same round, driven twice
 * with pv_merge apart.
 *
 * The audit runs on BOTH arms - truth_px follows pv_merge - so this also says
 * the sampled arm still draws its own correct picture, which is what makes
 * "it is still reachable" a statement somebody could act on. */
static void drive_cga_arms(void)
{
    void *win;
    double full[2], worst[2], last;
    int arm, i;

    printf("\nCGA, the row merge priced on a FRAME:\n");
    for (arm = 0; arm < 2; arm++) {
        pv_merge = arm;                 /* 0 = the alternate-row sample     */
        hg_screen(640, 200, 1, OS88_VID_CGA);
        win = os88_main();
        hg_obscured = 0;
        hg_ticks_v = 0;
        pmc_new_game();
        cost_reset();
        os88_paint(win);
        full[arm] = hc_last_ms_ms();
        if (pmc_step != 2)
            fail("the CGA arms row is not on the alternate-row layout");

        for (i = 0; i < 400 && pmc_freeze; i++)
            tick_n(win, 1, "CGA, waiting for the round");
        worst[arm] = 0.0;
        for (i = 0; i < 24; i++) {
            cost_reset();
            tick_n(win, 1, "CGA play frame");
            last = hc_last_ms_ms();
            if (last > worst[arm])
                worst[arm] = last;
        }
        printf("  %-26s full repaint %8.1f ms   worst play frame %7.1f ms\n",
               arm ? "MERGED (what ships)" : "sampled", full[arm], worst[arm]);
    }
    pv_merge = 1;                       /* every row after this one is the
                                         * shipping picture again           */
    printf("  %-26s full repaint %+7.1f%%          play frame %+11.1f%%\n",
           "the merge costs", 100.0 * (full[1] / full[0] - 1.0),
           100.0 * (worst[1] / worst[0] - 1.0));
    if (worst[1] > worst[0] * 1.25)
        fail("the row merge costs a CGA play frame more than 25% - "
             "SPEC.md 91's keep/revert bound");
    if (full[1] > full[0] * 1.25)
        fail("the row merge costs a CGA full repaint more than 25%");
}

int main(void)
{
    void *win;
    int n;

    printf("pmcuitest: PACCMAN's host harness (SPEC.md 91)\n");
    check_tables();

    /* VGA: every source row, and the plane path the whole performance
     * hypothesis rests on. */
    drive_layout("VGA", 640, 480, 4, OS88_VID_VGA, 0, PMC_P_BLITP, 1);

    /* ...the same machine with a window over us: BLITP is refused and the
     * packed band goes down through the decoder instead. The fallback must be
     * CORRECT and not merely present. */
    drive_layout("VGA, obscured", 640, 480, 4, OS88_VID_VGA, 1, PMC_P_BLIT4, 1);

    /* EGA: 350 rows, so the 307-row frame still holds all 288 field rows and
     * hangs one row over the dock (SPEC.md 11.93's WF_KEEPH). */
    drive_layout("EGA", 640, 350, 4, OS88_VID_EGA, 0, PMC_P_BLITP, 1);

    /* Hercules: 1bpp, every source row, the class table and the checkerboard. */
    drive_layout("Hercules", 720, 348, 1, OS88_VID_HERC, 0, PMC_P_BLIT1, 1);

    /* CGA: 200 rows, so alternate source rows and a 4-row band. */
    drive_layout("CGA", 640, 200, 1, OS88_VID_CGA, 0, PMC_P_BLIT1, 2);
    /* ...and drive_cga_arms(), at the END of main, prices these same CGA
     * frames with the row merge and without it - it plays a round, and a
     * round leaves a hiscore that the attract-screen rows below are entitled
     * to find absent. */

    /* ...and a kern_small kernel, where os88_gfx_blit1 carries the slot
     * without the body: the packed band still draws through blit4. */
    hg_screen(640, 200, 1, OS88_VID_CGA);
    win = os88_main();
    hg_blit1_refuse = 1;
    cost_reset();
    os88_paint(win);
    if (hc_blit4 == 0)
        fail("a refused os88_gfx_blit1 did not fall back to os88_gfx_blit4");
    printf("\nkern_small (blit1 refuses):\n");
    cost_row("full repaint, fallback");

    /* A GROWN WINDOW: the letterbox exists, so it is painted - and it is the
     * BORDER that is painted and not the content. The window is 64 px wider
     * and 32 rows deeper than the field, so the four strips are two sides of
     * 32 x 288 and a top and bottom of 288 x 16 - 2 x 9,216 + 2 x 4,608 =
     * 27,648 pixels, against the 92,160 a whole-content fill would write, of
     * which the field's own 64,512 would be overdrawn by a band. */
    hg_screen(640, 480, 4, OS88_VID_VGA);
    win = os88_main();
    hg_setwin(PMC_WIN_X, PMC_WIN_Y, PMC_WIN_W + 64, PMC_WIN_H + 32);
    cost_reset();
    os88_paint(win);
    printf("\ngrown window (%d x %d content):\n", hg_cw, hg_ch);
    cost_row("full repaint, letterbox");
    if (hc_fill != 4)
        fail("a grown window did not paint four letterbox strips");
    if (pmc_n_fillpx != 2 * (32 * 288) + 2 * ((PMC_WIN_W - 2 + 64) * 16))
        fail("the letterbox painted something other than the border");
    audit("full repaint, letterbox");

    /* THE ABOUT CARD OWNS THE GLASS - AND IT MUST COME BACK DOWN. os88_about_
     * card only draws; the flag AND THE DISMISSAL are the package's
     * (apps/cc/os88.h). A paint taken while it is up must redraw the WHOLE
     * card after the bands, or a partial expose recomposes the tile rows under
     * it and leaves a hole in it - and if nothing ever clears the flag, that
     * interlock turns a draw-once widget into a modal one covering the arcade
     * field for the life of the instance, with the close box as the only way
     * out. The plain entry arms the clip and belongs in os88_about(); the _d
     * entry does not and is the only correct one inside a paint. */
    hg_screen(640, 480, 4, OS88_VID_VGA);
    win = os88_main();
    cost_reset();
    os88_paint(win);
    if (hc_about_d != 0)
        fail("a paint with no About card up still drew one");
    os88_about(win);
    if (hc_about != 1 || hc_about_d != 0)
        fail("os88_about() did not draw the card through the plain entry");

    /* ...AND ON THE ATTRACT SCREEN IT PAUSES NOTHING, which is the half that
     * shipped wrong. The program OPENS on the attract screen, P and SPACE are
     * ordinary any-keys there (this port's stated binding), so a card that
     * paused unconditionally froze the reveal with no key able to unfreeze it -
     * and the presses meant to unfreeze it sat in the any-key latch and started
     * a round the moment the menu's Resume was chosen. The card is modal either
     * way while it is up: pmc_frame's guard tests pmc_about_up as well. */
    if (pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Pause"))
        fail("About paused the attract screen, which no key there can undo");
    pmc_abdismiss(win);

    /* READING THE CARD PAUSES THE GAME, which is apps/pacman/pacman.asm's
     * pm_about_body to the byte (it sets `pm_pause` beside `pm_abon`) and the
     * reason is a player's: whoever opened the About box is not watching the
     * maze, and a game that ran on behind the card would be four ghosts closer
     * when it came down. */
    pmc_new_game();
    os88_about(win);
    if (!pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Resume"))
        fail("About did not pause the game and say so in the menu");
    hg_dmg_whole = 0;                   /* a menu over the top three rows */
    hg_dmg_x1 = pmc_fx;
    hg_dmg_y1 = pmc_fy;
    hg_dmg_x2 = pmc_fx + PMC_FIELD_W - 1;
    hg_dmg_y2 = pmc_fy + (3 << pmc_rsh) - 1;
    os88_paint(win);
    if (hc_about_d != 1)
        fail("a partial expose under the About card did not redraw the card");

    /* AN EMPTY RECT COSTS NO CARD EITHER. SPEC.md 11.90.2's empty answer means
     * draw nothing at all, and the card is ~12 gfx calls and ~200 glyph cells
     * - about 210 ms of XT - on a paint the kernel has just said owes
     * nothing. */
    cost_reset();
    n = hc_about_d;
    hg_dmg_x1 = 1;
    hg_dmg_x2 = 0;
    os88_paint(win);
    if (hc_about_d != n)
        fail("an empty damage rect still redrew the About card");
    if (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe != 0)
        fail("an empty damage rect drew something under the raised card");
    hg_dmg_whole = 1;

    /* A WHOLE REPAINT UNDER THE RAISED CARD DRAWS THE CARD'S COMPLEMENT.
     *
     * This is the row that was missing, and its absence is why the defect
     * lived: the only card-up paint driven here set hg_dmg_whole = 0 first, so
     * a whole rect under a raised card was never costed. Unnarrowed it is
     * 1,008 tiles - 2,480.6 ms of VGA XT - of which the card covers all but
     * four pixels at each edge on VGA and all but two tile columns on CGA:
     * drawn, and then immediately covered by os88_about_card_d.
     *
     * The expected tile count is recomputed HERE from the four words
     * pmc_ab_box answered with, so this row checks pmc_dirty_not_card's loop
     * and not merely that something was spared. */
    {
        int ty, want_t = 0;

        cost_reset();
        n = hc_about_d;
        os88_paint(win);
        if (hc_about_d != n + 1)
            fail("a whole repaint under the raised card did not redraw it");
        for (ty = 0; ty < PMC_TILES_Y; ty++) {
            if (ty < pmc_ab_ty0 || ty > pmc_ab_ty1) {
                want_t += PMC_TILES_X;
                continue;
            }
            if (pmc_ab_c0 > 0)
                want_t += pmc_ab_c0;
            if (pmc_ab_c1 < PMC_TILES_X - 1)
                want_t += PMC_TILES_X - 1 - pmc_ab_c1;
        }
        if (want_t >= PMC_TILES_X * PMC_TILES_Y)
            fail("the About card spared no tile at all, so this row is empty");
        if ((int) pmc_n_tiles != want_t)
            fail("a whole repaint under the raised card did not compose "
                 "exactly the card's complement");
        cost_row("whole repaint, card up");
    }

    /* ...AND THE TWO HALVES COVER THE FIELD BETWEEN THEM. The complement left
     * the card's own rect undrawn on purpose; pmc_ab_mark is what owes it back
     * when the card comes down, and the audit is what says the two rectangles
     * meet. It is also the only check on pmc_ab_box's slack being the RIGHT
     * WAY ROUND: a subset that was really a superset leaves a hole here. */
    pmc_abdismiss(win);
    pmc_flush(win, 0);
    audit("the card's complement plus what the card covered");
    os88_about(win);                    /* put it back for the rows below */

    /* A NEW GAME UNDER THE RAISED CARD MAY NOT RUB A HOLE IN IT, which is the
     * same defect one call site along from the paint the interlock above
     * fixed: pmc_game_init rewrites both RAMs, so all 36 bands are marked, and
     * pmc_flush composing three of them over the card's rect would leave the
     * card's white ground and black frame with the field showing through the
     * middle. A command takes the card DOWN first, and the flag has to be
     * cleared before anything draws because pmc_flush refuses while it is up.
     *
     * BUT THE COMMAND ITSELF DRAWS NOTHING NOW. A callback's gfx lock is the
     * kernel's and not ours to drop, so 36 bands composed in here is one
     * uninterruptible ~2.5 s hold; the command marks and the WORKER's next
     * frame draws it chunked. Both halves are asserted - nothing drawn in the
     * callback, everything owed after it, and the whole field drawn by one
     * frame. */
    cost_reset();
    os88_oncmd(PMC_CMD_NEW, 0, win);
    if (pmc_about_up)
        fail("a menu command did not take the About card down");
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("New Game drew from inside the callback - the worker owns that");
    if (!pmc_dirty_any())
        fail("New Game left nothing owed - the worker would draw nothing");
    {
        int ty, owed = 0;

        /* WHAT IS OWED, counted independently before the flush. game_init
         * rewrites both RAMs but every write compares first, so a row the
         * fresh game happens to agree with is not marked and the answer is
         * not always 36 - which is the whole reason this is counted rather
         * than assumed. */
        for (ty = 0; ty < PMC_TILES_Y; ty++)
            if (pmc_dmin[ty] <= pmc_dmax[ty])
                owed++;
        if (owed < PMC_TILES_Y / 2)
            fail("New Game marked almost nothing - the field cannot have moved");
        cost_reset();
        pmc_flush(win, 1);              /* the worker's own flush, which is
                                         * what pmc_frame reaches; taken here
                                         * without a game tick so this row does
                                         * not move the clock drive_play prices
                                         * itself against */
        if ((int) pmc_n_bands < owed)
            fail("the worker flush after New Game left a marked band undrawn");
        if (pmc_dirty_any())
            fail("the flush after New Game left a band owed - a hole in the card");
        if (hc_unlock == 0)
            fail("the worker's New Game flush did not chunk its lock hold");
        printf("  %-26s %d owed, %u drawn, %u break(s)\n",
               "New Game deferred", owed, pmc_n_bands, hc_unlock);
    }
    audit("New Game under the About card");

    /* ...and pmc_flush REFUSES while the flag is up, which is what protects
     * every other caller of it - wave 2's worker frame above all. */
    os88_about(win);
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win, 0);
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("pmc_flush drew through the raised About card");
    if (!pmc_dirty_any())
        fail("pmc_flush cleaned a band it refused to draw");
    pmc_about_up = 0;
    pmc_flush(win, 0);                     /* put the tile back before the audit */

    /* A CARD TAKEN DOWN MID-FADE OWES THE BLACK AGAIN, BY EITHER ROUTE.
     * pmc_shblack says the black fill is already on the glass so that ~60
     * ticks of fade cost ONE gfx call; the card is the only thing that
     * overdraws it, so the shadow is a lie until the fill is sent again.
     * pmc_ab_mark is where the byte is cleared, because it is exactly the two
     * card-down paths and nothing else - and the first version cleared it in
     * pmc_abdismiss alone, so a MENU command's dismissal (Sound, Pause, a
     * refused Full Screen) reached pmc_flush with the shadow still claiming
     * black, pmc_fade_black returned at its first test, and the card's
     * rectangle stayed on the glass. A paused window still FLUSHES - pmc_frame
     * puts that call outside its pause test - so the repair is the worker's
     * and the callback's job is the byte; both halves are asserted, and by
     * both routes. */
    {
        int route;

        for (route = 0; route < 2; route++) {
            pmc_black = 1;
            pmc_shblack = 0;
            pmc_flush(win, 0);          /* the fade's one fill */
            if (!pmc_shblack)
                fail("the fade's fill did not set its own shadow");
            os88_about(win);            /* ...and the card overdraws it */
            cost_reset();
            if (route == 0)
                os88_oncmd(PMC_CMD_SOUND, 0, win);
            else
                os88_onkey(0, PMC_SC_N, win);
            if (pmc_about_up)
                fail("the card did not come down mid-fade");
            if (hc_fill + hc_blitp + hc_blit4 + hc_blit1 != 0)
                fail("a card taken down mid-fade DREW, in a callback");
            if (pmc_shblack)
                fail("a card taken down mid-fade kept the black shadow set");
            cost_reset();
            pmc_flush(win, 1);          /* ...and the WORKER's next flush is
                                         * what repairs it, chunked           */
            if (hc_fill == 0)
                fail("the worker after a mid-fade dismissal left its rect on "
                     "the black");
            if (route == 0)
                os88_oncmd(PMC_CMD_SOUND, 0, win);   /* sound back on */
        }
        pmc_black = 0;
        pmc_shblack = 0;
        pmc_dirty_all();
        pmc_flush(win, 0);
        printf("  %-26s both routes re-sent the fade's fill\n",
               "card down mid-fade");
    }

    /* NEITHER DIRECTION FLUSHES, AND A STOPPED WINDOW STILL DRAWS. The first
     * version of this wave paused on os88_about and then had the PAUSE
     * direction compose the card's rect itself, on the argument that a stopped
     * worker draws nothing. That argument was answered by moving pmc_frame's
     * flush OUTSIDE its pause test instead: a paused window runs no game and
     * still draws what it owes, chunked and interruptible, one OS tick later.
     * So the callback's whole job is the MARK - a brk = 0 compose here is 20
     * bands on VGA (~1.3 s of XT) and the WHOLE FIELD on a short display, with
     * no unlock and no yield, inside a callback whose gfx lock is the
     * kernel's. Both directions are driven, and the PAUSE one is driven on to
     * the frame that repairs it while the window is still stopped. */
    pmc_paused = 0;
    pmc_pause_item();
    os88_about(win);                    /* pauses, and covers the field */
    cost_reset();
    os88_oncmd(PMC_CMD_PAUSE, 0, win);  /* ...Resume */
    if (pmc_paused)
        fail("the menu's Resume did not un-pause");
    if (hc_blitp + hc_blit4 + hc_blit1 + hc_fill != 0)
        fail("Resume drew the card's rect in the callback - the worker owns it");
    if (!pmc_dirty_any())
        fail("Resume left nothing owed - the worker would draw nothing");
    cost_reset();
    pmc_frame(win);                     /* ...and the worker's next frame does */
    if (pmc_n_bands == 0)
        fail("the frame after Resume drew none of the card's bands");
    /* ...and the PAUSE direction, which in GAME mode the card cannot reach on
     * its own (os88_about has already paused): it is the ATTRACT screen's
     * route, where About pauses nothing and the menu's Pause is what stops the
     * window. Modelled by clearing the byte the card would not have set. */
    os88_about(win);
    pmc_paused = 0;
    pmc_pause_item();
    cost_reset();
    os88_oncmd(PMC_CMD_PAUSE, 0, win);
    if (!pmc_paused)
        fail("the menu's Pause did not pause");
    if (hc_blitp + hc_blit4 + hc_blit1 + hc_fill != 0)
        fail("Pause drew the card's rect in the callback - the worker owns it");
    if (!pmc_dirty_any())
        fail("Pause left nothing owed - the worker would draw nothing");
    cost_reset();
    pmc_frame(win);                     /* ...WHILE STILL PAUSED */
    if (!pmc_paused)
        fail("a frame un-paused the window");
    if (pmc_n_bands == 0)
        fail("a PAUSED frame drew none of the card's bands - a stopped window "
             "still flushes");
    printf("  %-26s neither draws; the stopped worker does (%u band(s))\n",
           "card down by Pause/Resume", pmc_n_bands);
    pmc_paused = 0;
    pmc_pause_item();
    audit("the card down by Pause and by Resume");

    /* ...and ANY KEY takes it down and starts nothing else, which is the
     * reference's own 'any key' posture and apps/pacman's pm_dismiss_body.
     *
     * THE KEY MARKS AND THE WORKER COMPOSES, and what is composed is what the
     * card COVERED - this row being the independent second reader of
     * pmc_ab_mark's arithmetic: the widget's own measurement (apps/os88ui.inc -
     * lines x OS88UI_ABLH + 2 x OS88UI_ABPADY, clamped to the content box and
     * centred) is written out again here from the live layout, and both bounds
     * are asserted. Too few bands is a hole left in the field where the card
     * was; all 36 is the 2.5 s full repaint pmc_ab_mark exists to avoid. The
     * frame that draws them runs with the window still PAUSED, because
     * dismissing the card does not start the game again. */
    os88_paint(win);                    /* put the field back, card down */
    os88_about(win);
    if (!pmc_about_up)
        fail("os88_about() did not raise the flag");
    cost_reset();
    pmc_round = 99;                     /* a key that STARTED a game would
                                         * put this back to 0 */
    os88_onkey(0, PMC_SC_N, win);
    if (pmc_about_up)
        fail("a key did not take the About card down");
    if (pmc_round != 99)
        fail("the dismissing key was not swallowed - it started a new game");
    if (hc_blitp + hc_blit4 + hc_blit1 + hc_fill != 0)
        fail("dismissing the card DREW, in a callback");
    if (!pmc_dirty_any())
        fail("dismissing the card marked nothing - its rect would stay");
    cost_reset();
    pmc_frame(win);                     /* the worker, still paused, repairs it */
    {
        const char **l;
        int n = 0, h, d0, d1, b0, b1;

        for (l = pmc_about_lines; *l; l++)
            n++;
        h = n * 12 + 2 * 7;
        if (h > pmc_ch)
            h = pmc_ch;
        d0 = pmc_cy + (pmc_ch - h) / 2 - pmc_fy;
        d1 = d0 + h - 1;
        if (d0 < 0)
            d0 = 0;
        b0 = d0 >> pmc_rsh;
        b1 = d1 >> pmc_rsh;
        if (b1 > PMC_TILES_Y - 1)
            b1 = PMC_TILES_Y - 1;
        if ((int) pmc_n_bands < b1 - b0 + 1)
            fail("dismissing the card left a band of its rect undrawn");
        if (pmc_n_bands >= PMC_TILES_Y)
            fail("dismissing the card recomposed the WHOLE field");
        if (pmc_dirty_any())
            fail("dismissing the card left a marked band owed");
        cost_row("About card dismissed");
        printf("  %-26s %u band(s) for the card's %d\n",
               "  ...of 36 for the field", pmc_n_bands, b1 - b0 + 1);
    }
    audit("About card dismissed");

    /* ...AND DISMISSING IT LEAVES PLAY PAUSED. apps/pacman's pm_dismiss_body
     * clears `pm_abon` and nothing else, so the pause the card put on survives
     * it and P (or the menu) is what starts the game moving again. A game that
     * resumed itself the instant the card came down would resume with the
     * ghosts wherever they were when the player stopped looking. */
    if (!pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Resume"))
        fail("dismissing the About card un-paused the game");
    os88_onkey(0, PMC_SC_P, win);       /* the player starts it again */
    if (pmc_paused)
        fail("P did not resume after the About card");

    /* THE CLIP REGION, and only when something is covering us (SPEC.md 11.3,
     * 5.4.3.3). A key or a menu command arrives with NO region armed - the
     * kernel arms one only around W_PAINT - so an unclipped band blit lands on
     * the covering window's pixels. But a region a package arms is BINDING on
     * os88_gfx_blitp, so arming one every frame would cost this port its plane
     * path for ever; os88_wm_obscured is the gate and pmc_pick_path has
     * already chosen BLIT4 on the same answer. */
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_DOT);
    pmc_flush(win, 0);
    if (hc_clip != 0)
        fail("an unobscured frame armed a clip region - BLITP refuses on one");
    if (hc_blitp != 1)
        fail("an unobscured frame did not take the plane path");

    cost_reset();
    hg_obscured = 1;
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win, 0);
    if (hc_clip != 1)
        fail("an obscured frame drew without arming a clip region");
    if (hc_blit4 != 1)
        fail("an obscured frame did not fall back to BLIT4");

    /* ...and 'not one pixel shows' draws nothing and leaves the spans OWED. */
    cost_reset();
    hg_novis = 1;
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_DOT);
    pmc_flush(win, 0);
    if (hc_clip_no != 1 || hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a refused clip region still drew");
    if (!pmc_dirty_any())
        fail("a refused clip region cleaned the spans it never drew");
    hg_novis = 0;
    hg_obscured = 0;
    pmc_flush(win, 0);                     /* the uncovered window is owed it */
    audit("redrawn after the clip refused");

    /* The menu set's name is the window's title, the same literal in both
     * places - a kernel bar reading PACCMAN over a window titled PaccMan is
     * the drift LESSONS.md 8 records. */
    if (strcmp(pmc_mset.name, PMC_TITLE))
        fail("the menu set's name is not the window title");
    if (pmc_mset.oncmd == 0)
        fail("os88_menu_set() did not patch the set - is it const?");

    /* SOUND IS LIVE NOW, and its label is the ACTION on offer: "Sound Off"
     * while sound is on. That wording is not decoration - an imperative label
     * asserts the state it would leave, which is precisely why the greyed
     * version of it was wrong for two waves, and it is the only surface the
     * toggle has (the kernel's one marker is MENU_DIS; there is no check
     * mark, this package has no status line, and the title does not change). */
    if (!pmc_snd_on)
        fail("sound does not start on");
    if (strcmp(pmc_items[PMC_CMD_SOUND], "Sound Off"))
        fail("a program with sound ON does not offer 'Sound Off'");
    os88_oncmd(PMC_CMD_SOUND, 0, win);
    if (pmc_snd_on || strcmp(pmc_items[PMC_CMD_SOUND], "Sound On"))
        fail("Game > Sound did not turn sound off and swap the label");
    os88_oncmd(PMC_CMD_SOUND, 0, win);
    if (!pmc_snd_on || strcmp(pmc_items[PMC_CMD_SOUND], "Sound Off"))
        fail("Game > Sound did not turn sound back on");

    /* PAUSE SAYS WHICH STATE IT IS IN, and the item label is the ONLY place
     * it can: the kernel has no check-mark marker, this package has no status
     * line and the title does not change, so without the swap a paused window
     * is pixel-identical to a hung one. The precedent does not leave it unsaid
     * either - apps/pacman/pacman.asm carries 'PAUSED - P OR SPACE TO RESUME'
     * in its footer. Driven through the two real call sites, the key and the
     * command, plus New Game's clear. */
    if (strcmp(pmc_items[PMC_CMD_PAUSE], "Pause"))
        fail("a running game's Pause item does not read 'Pause'");
    os88_oncmd(PMC_CMD_PAUSE, 0, win);
    if (!pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Resume"))
        fail("Game > Pause did not turn the item into 'Resume'");
    os88_onkey(0, PMC_SC_SPACE, win);           /* SPACE resumes, as on
                                                 * apps/pacman */
    if (pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Pause"))
        fail("SPACE did not resume and put the item back");
    os88_onkey(0, PMC_SC_P, win);
    if (!pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Resume"))
        fail("the P key did not pause and swap the item");
    os88_oncmd(PMC_CMD_NEW, 0, win);
    if (pmc_paused || strcmp(pmc_items[PMC_CMD_PAUSE], "Pause"))
        fail("New Game cleared pmc_paused without putting the item back");
    /* THE ABOUT CARD FITS THE NARROWEST WINDOW IT WILL BE DRAWN IN. The card
     * clamps to the live content box, which here is the 224-pixel arcade
     * field, and the plan's first six lines were each cut off mid-word on the
     * glass. 24 cells of 8 pixels is the width and OS88UI_ABLH = 12 the line
     * pitch. THE HEIGHT GATE IS THE WIDGET'S OWN ARITHMETIC and not a rounded
     * version of it: apps/os88ui.inc:2470 measures the card as
     * `lines * OS88UI_ABLH + 2 * OS88UI_ABPADY` = n * 12 + 14, and CGA's
     * content box is 144 rows (PMC_WIN_H_LO 163 = 144 + OS88_TITLE_H + 1), so
     * ten lines is 134 and ELEVEN IS 146 - over, clamped, and the last line
     * cut off. The first version of this row wrote `n * 12 > 144 - 12`, which
     * is 132 > 132 at eleven lines and let the very card the wave had just
     * refused straight through. Checked here because it is a screendump on
     * ONE adapter otherwise, and the narrow one is the adapter nobody looks at
     * (LESSONS.md 8). */
    { const char **l; int n = 0;
      for (l = pmc_about_lines; *l; l++) {
          if (**l == OS88_MENU_DIS)
              fail("an About line begins with the disabled marker");
          if ((int) strlen(*l) > 24)
              printf("pmcuitest: FAIL - About line over 24 cells: \"%s\"\n",
                     *l), hg_fails++;
          n++;
      }
      if (n * 12 + 2 * 7 > 144)
          fail("the About card is too tall for a CGA content box"); }
    /* NOTHING IN THE GAME MENU IS GREYED ANY MORE, and that is the assertion
     * rather than a count: all four items act, so none of them may carry
     * MENU_DIS. Wave 1 greyed Pause with "(No Game)" and waves 1-2 Sound with
     * "(No Sound Yet)", each because the BUILD could not act; each un-greying
     * was the deletion of one marker byte and its reason. Should a future wave
     * ever grey one again, SPEC.md 47 rule 3 makes the label say why not - the
     * parenthesis check below is kept for that day. */
    { int i, dis = 0;
      for (i = 0; i < pmc_mset.menu[0].nitems; i++)
          if (pmc_mset.menu[0].items[i][0] == OS88_MENU_DIS) {
              dis++;
              if (!strchr(pmc_mset.menu[0].items[i], '('))
                  fail("a greyed Game menu item does not say why not");
          }
      if (dis != 0)
          fail("a Game menu item is greyed - all four act in this build"); }

    /* --- THE ATTRACT SCREEN AND THE SOUND, on a fresh instance ----------- */
    hg_screen(640, 480, 4, OS88_VID_VGA);
    win = os88_main();
    hg_obscured = 0;
    hg_ticks_v = 0;
    os88_paint(win);
    drive_intro(win);
    drive_intro_mono();
    drive_sound();

    /* --- THE TICK PATH, on a fresh instance ------------------------------ */
    hg_screen(640, 480, 4, OS88_VID_VGA);
    win = os88_main();
    hg_obscured = 0;
    hg_ticks_v = 0;
    pmc_new_game();                     /* drive_play is about a ROUND, and
                                         * the program opens on the attract
                                         * screen: this is the N key */
    os88_paint(win);
    if (!pmc_hired)
        fail("the first paint did not hire the worker");
    drive_play(win);
    drive_lockbreak(win);
    drive_input(win);

    /* NEW GAME CLEARS PAUSE, which is apps/pacman's own behaviour (`pm_new`
     * clears `pm_pause`). Without it, Game > New Game taken while paused
     * draws the fresh maze, PLAYER ONE and READY! and then never moves again:
     * no score, no sprites, no reserve strip, because pmc_frame's guard skips
     * the whole tick path. Found on the glass, and it looks like the program
     * has hung. */
    {
        unsigned before;

        pmc_paused = 1;
        os88_oncmd(PMC_CMD_NEW, 0, win);
        if (pmc_paused)
            fail("New Game left the game paused");
        before = pmc_tick_lo;
        tick_n(win, 6, "after New Game while paused");
        if (pmc_tick_lo == before)
            fail("the tick never advanced after New Game while paused");
        printf("\n %-27s %u tick(s) after New Game while paused\n",
               "unpaused:", pmc_tick_lo - before);
    }

    /* THE ROUND-WON FLASH IS SHADOWED, and this row is the only thing that can
     * see it. pmc_vid_color_playfield is 31 rows x 28 columns = 868 calls to
     * pmc_vid_color, each of which calls pmc_ok again, and the colour it
     * writes only changes when bit 4 of since(WON) flips - once every SIXTEEN
     * game ticks - so ungated, fifteen ticks in sixteen spend ~25 ms proving
     * nothing changed, for ~4.5 s of XT over the 180-tick flash. Compare-
     * then-write keeps every one of those passes out of the DAMAGE, so the
     * cost table prices them at zero and the play-frame budget never enters
     * this state at all. Driven straight at game_update_tiles, because
     * reaching a won round through the rules is a hundred thousand ticks. */
    {
        unsigned n0, ran;
        int i, elig = 0, want;

        pmc_start(PMC_T_WON);
        n0 = pmc_n_flash;
        for (i = 0; i < 240; i++) {
            pmc_tick_inc();
            if (pmc_after(PMC_T_WON, 60))
                elig++;
            pmc_update_tiles();
        }
        ran = pmc_n_flash - n0;
        if (ran == 0)
            fail("the round-won flash never recoloured the playfield");
        if ((int) ran > elig / 8)
            fail("the round-won flash recoloured the playfield every tick");
        want = (pmc_since_lo(PMC_T_WON) & 0x10) ? PMC_COLOR_DOT
                                                : PMC_COLOR_WHITE_BORDER;
        if (pmc_cram[(20 << PMC_VSHIFT) + 11] != (unsigned char) want)
            fail("the shadowed flash left the playfield the wrong colour");
        printf(" %-27s %u recolour(s) of %d flash tick(s)\n",
               "round-won flash:", ran, elig);
    }
    /* THE ROW MERGE ON A FRAME, last because it plays a round of its own. */
    drive_cga_arms();

    printf("\n  worst frame: %s\n               %.1f ms of 4.77 MHz 8088\n",
           hc_worst, hc_worst_ms);

    write_vectors("build/pmcbandvec.inc");

    printf("\npmcuitest: %s (%d failure(s))%s%s\n",
           hg_fails ? "FAIL" : "PASS", hg_fails,
           PMC_T_BENCHED
             ? (PMC_SPR_BENCHED ? ""
                : "   [the SPRITE and LOGIC terms are 0: every play row above "
                  "understates its frame]")
             : "   [composer terms are 0: run `make pmcbandbench`]",
           PMC_T2_BENCHED
             ? (PMC_SPRM_BENCHED ? ""
                : "   [the MERGED sprite-row term is 0: every CGA row above "
                  "understates its sprite layer]")
             : "   [the two rowstep-2 TILE terms are 0: every CGA row above "
               "understates its tiles]");
    return hg_fails ? 1 : 0;
}
