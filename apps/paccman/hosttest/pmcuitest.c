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

void os88_gfx_lock(void)   { }
void os88_gfx_unlock(void) { }
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

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    (void) title;
    hg_setwin(x, y, w, h);
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
int  os88_key_down(int scan)                       { (void) scan; return 0; }
int  os88_snd_tone(int hz, int t, int p) { (void) hz; (void) t; (void) p; return 0; }
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

void pmc_tile(const unsigned char *src, const unsigned char *pairs,
              unsigned char *dst, int rowstep)
{
    /* `pairs` points into pmc_pairs; recover the colour block from it so the
     * twin can go through pmc_pal instead, which is the whole point. */
    int cblk = (int) (pairs - pmc_pairs) >> 4;
    int rows = 8 / rowstep;
    int r, c, hi, lo;

    for (r = 0; r < rows; r++) {
        const unsigned char *row = src + (r * rowstep) * 2;
        for (c = 0; c < 8; c += 2) {
            hi = pmc_pal[cblk * 4 + pmc_px(row, c)];
            lo = pmc_pal[cblk * 4 + pmc_px(row, c + 1)];
            dst[r * PMC_BAND_STRIDE + (c >> 1)] = (hi << 4) | lo;
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
                    v = band[r * PMC_BAND_STRIDE + (px >> 1)];
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
                v = band[r * PMC_BAND_STRIDE + (px >> 1)];
                v = (px & 1) ? (v & 15) : (v >> 4);
                cls = pmc_mono[v];
                lit = (cls == 2) || (cls == 1 && (((px + y0 + r) & 1) == 0));
                byte |= (lit ? 1 : 0) << (7 - bit);
            }
            bits[r * PMC_PL_STRIDE + c] = byte;
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
    int v = pmc_pal[(pmc_cram[i] & 31) * 4 + pmc_px(row, ic)];
    int cls, lit;

    if (hg_bpp != 1)
        return v;
    cls = pmc_mono[v];
    lit = (cls == 2) || (cls == 1 && (((sx + sy) & 1) == 0));
    return lit ? 1 : 0;
}

static int audit(const char *where)
{
    int sx, sy, want, got, bad = 0, fx = -1, fy = -1, gw = 0, gg = 0;

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
 * LESSONS.md 13). Until that bench has been run on a given host the five
 * composer terms below carry the bench's numbers as recorded in SPEC.md 91;
 * the blit terms are PERFORMANCE.md's.
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

#define T_CALL      756.0   /* us - any gfx_* call, whatever it draws        */
#define T_THUNK      57.7   /* us - one OSAPI far call + the near call       */
#define T_TILE      PMC_T_TILE       /* us - one 8x8 tile composed           */
#define T_PACKPL    PMC_T_PACKPL     /* us - one packed row -> four planes   */
#define T_PACK1     PMC_T_PACK1      /* us - one packed row -> 1bpp          */

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
 * rows' worth of work was that". */
static void cost_row(const char *what)
{
    double ms, rows28 = pmc_n_rc / (double) PMC_PL_STRIDE;

    ms = (hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe)
             * (T_CALL + T_THUNK)
       + pmc_n_fillrows * T_FILL_ROW + pmc_n_fillpx * T_FILL_PX
       + pmc_n_tiles * T_TILE
       + (pmc_n_pkpl / (double) PMC_PL_STRIDE) * T_PACKPL
       + (pmc_n_pk1  / (double) PMC_PL_STRIDE) * T_PACK1
       + (hc_blitp ? rows28 * T_BLITP_R : 0)
       + (hc_blit1 ? rows28 * T_BLIT1_R : 0)
       + (hc_blit4 ? rows28 * T_BLIT4_R : 0);
    ms /= 1000.0;

    printf("  %-26s calls %3d (fill %d blitp %d blit4 %d blit1 %d probe %d/%d)"
           "  tiles %4u  bands %3u  rows %4u  fillpx %5u  %8.1f ms\n",
           what,
           hc_fill + hc_blitp + hc_blit4 + hc_blit1 + hc_probe,
           hc_fill, hc_blitp, hc_blit4, hc_blit1, hc_probe - hc_probe_no,
           hc_probe, pmc_n_tiles, pmc_n_bands, pmc_n_rows, pmc_n_fillpx, ms);
}

static void cost_reset(void)
{
    hc_fill = hc_blitp = hc_blit4 = hc_blit1 = hc_probe = hc_probe_no = 0;
    hc_clip = hc_clip_no = 0;
    hg_clip = 0;                        /* the region dies at the caller's
                                         * next os88_gfx_unlock, and one
                                         * measured step is one lock hold */
    pmc_n_calls = pmc_n_tiles = pmc_n_bands = pmc_n_rows = pmc_n_rc = 0;
    pmc_n_fillpx = pmc_n_fillrows = 0;
    pmc_n_pkpl = pmc_n_pk1 = 0;
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

static unsigned char vec_band[8 * PMC_BAND_STRIDE];
static unsigned char vec_t1[8 * PMC_BAND_STRIDE];
static unsigned char vec_t2[8 * PMC_BAND_STRIDE];
static unsigned char vec_pl[4 * PMC_PL_STEP];
static unsigned char vec_m1[8 * PMC_PL_STRIDE];

static void write_vectors(const char *path)
{
    FILE *f;
    int r, c;

    /* A band with every packed byte value the tiles can produce, plus a
     * deterministic filler, so the packers are not exercised on one colour. */
    for (r = 0; r < 8; r++)
        for (c = 0; c < PMC_BAND_STRIDE; c++)
            vec_band[r * PMC_BAND_STRIDE + c] =
                (unsigned char) ((r * 37 + c * 91 + 13) & 0xFF);

    memset(vec_t1, 0xAA, sizeof vec_t1);
    memset(vec_t2, 0xAA, sizeof vec_t2);
    pmc_tile(pmc_tiles + (VEC_TILE << 4), pmc_pairs + (VEC_COLOR << 4),
             vec_t1, 1);
    pmc_tile(pmc_tiles + (VEC_TILE << 4), pmc_pairs + (VEC_COLOR << 4),
             vec_t2, 2);
    memset(vec_pl, 0, sizeof vec_pl);
    pmc_pack_pl(vec_band, vec_pl, 8, pmc_planar, PMC_PL_STRIDE);
    memset(vec_m1, 0, sizeof vec_m1);
    pmc_pack_1(vec_band, vec_m1, 8, pmc_mono2, 0, PMC_PL_STRIDE);

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
    emit(f, "pv_t1_exp",   vec_t1, 8 * PMC_BAND_STRIDE);
    emit(f, "pv_t2_exp",   vec_t2, 4 * PMC_BAND_STRIDE);
    emit(f, "pv_band",     vec_band, 8 * PMC_BAND_STRIDE);
    emitw(f, "pv_planar",  pmc_planar, 256);
    emit(f, "pv_pl_exp",   vec_pl, 4 * PMC_PL_STEP);
    emit(f, "pv_mono2",    pmc_mono2, 512);
    emit(f, "pv_m1_exp",   vec_m1, 8 * PMC_PL_STRIDE);
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
    pmc_flush(win);
    cost_row("nothing written at all");
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a frame with nothing written still blitted");

    pmc_vid_quad(2, 34, 0x09, 0x20);     /* one life, so the strip exists */
    pmc_flush(win);

    cost_reset();
    pmc_vid_color_text(9, 0, PMC_COLOR_DEFAULT, "HIGH SCORE");
    pmc_vid_color_text(9, 14, 0x05, "PLAYER ONE");
    pmc_vid_color_text(11, 20, 0x09, "READY!");
    pmc_vid_quad(2, 34, 0x09, 0x20);
    pmc_vid_color(1, 6, PMC_COLOR_DOT);
    pmc_vid_color_tile(13, 15, 0x18, PMC_TILE_DOOR);
    if (pmc_dmin[0] <= pmc_dmax[0] || pmc_dmin[6] <= pmc_dmax[6]
        || pmc_dmin[14] <= pmc_dmax[14] || pmc_dmin[15] <= pmc_dmax[15]
        || pmc_dmin[20] <= pmc_dmax[20] || pmc_dmin[34] <= pmc_dmax[34])
        fail("re-writing identical tiles marked a band dirty");
    pmc_flush(win);
    cost_row("identical rewrite");
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a frame that changed nothing still blitted");
    audit("identical rewrite");

    /* ONE TILE. The band it is in, and no other, must go down. */
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win);
    cost_row("one tile changed");
    if (hc_blitp + hc_blit4 + hc_blit1 != 1)
        fail("one changed tile was not exactly one blit");
    audit("one tile changed");

    /* A SPAN. Two tiles far apart in one row coalesce into one blit, which is
     * the pill blink's shape (SPEC.md 91). */
    cost_reset();
    pmc_vid_color(1, 6, 0x00);
    pmc_vid_color(26, 6, 0x00);
    pmc_flush(win);
    cost_row("two ends of one row");
    if (hc_blitp + hc_blit4 + hc_blit1 != 1)
        fail("two tiles in one row were not coalesced into one blit");
    if (pmc_n_tiles != 26)
        fail("the coalesced span did not compose columns 1..26");
    audit("two ends of one row");

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

    /* A NEW GAME UNDER THE RAISED CARD MAY NOT RUB A HOLE IN IT, which is the
     * same defect one call site along from the paint the interlock above
     * fixed: pmc_game_init rewrites both RAMs, so all 36 bands are marked, and
     * pmc_flush composing three of them over the card's rect would leave the
     * card's white ground and black frame with the field showing through the
     * middle. A command takes the card DOWN first and then draws, so what
     * lands is the whole field over the whole card - and the flag has to be
     * cleared BEFORE the flush, because pmc_flush refuses while it is up. */
    cost_reset();
    os88_oncmd(PMC_CMD_NEW, 0, win);
    if (pmc_about_up)
        fail("a menu command did not take the About card down");
    if (pmc_n_bands != PMC_TILES_Y)
        fail("New Game under the card did not recompose the whole field");
    if (pmc_dirty_any())
        fail("New Game under the card left a band owed - the card had a hole");
    audit("New Game under the About card");

    /* ...and pmc_flush REFUSES while the flag is up, which is what protects
     * every other caller of it - wave 2's worker frame above all. */
    os88_about(win);
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win);
    if (hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("pmc_flush drew through the raised About card");
    if (!pmc_dirty_any())
        fail("pmc_flush cleaned a band it refused to draw");
    pmc_about_up = 0;
    pmc_flush(win);                     /* put the tile back before the audit */

    /* ...and ANY KEY takes it down and starts nothing else, which is the
     * reference's own 'any key' posture and apps/pacman's pm_dismiss_body.
     * The whole field is recomposed because the card was opaque over it. */
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
    if (pmc_n_bands != PMC_TILES_Y)
        fail("dismissing the card did not recompose the whole field");
    audit("About card dismissed");

    /* THE CLIP REGION, and only when something is covering us (SPEC.md 11.3,
     * 5.4.3.3). A key or a menu command arrives with NO region armed - the
     * kernel arms one only around W_PAINT - so an unclipped band blit lands on
     * the covering window's pixels. But a region a package arms is BINDING on
     * os88_gfx_blitp, so arming one every frame would cost this port its plane
     * path for ever; os88_wm_obscured is the gate and pmc_pick_path has
     * already chosen BLIT4 on the same answer. */
    cost_reset();
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_DOT);
    pmc_flush(win);
    if (hc_clip != 0)
        fail("an unobscured frame armed a clip region - BLITP refuses on one");
    if (hc_blitp != 1)
        fail("an unobscured frame did not take the plane path");

    cost_reset();
    hg_obscured = 1;
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_PILL);
    pmc_flush(win);
    if (hc_clip != 1)
        fail("an obscured frame drew without arming a clip region");
    if (hc_blit4 != 1)
        fail("an obscured frame did not fall back to BLIT4");

    /* ...and 'not one pixel shows' draws nothing and leaves the spans OWED. */
    cost_reset();
    hg_novis = 1;
    pmc_vid_color_tile(11, 20, 0x09, PMC_TILE_DOT);
    pmc_flush(win);
    if (hc_clip_no != 1 || hc_blitp + hc_blit4 + hc_blit1 != 0)
        fail("a refused clip region still drew");
    if (!pmc_dirty_any())
        fail("a refused clip region cleaned the spans it never drew");
    hg_novis = 0;
    hg_obscured = 0;
    pmc_flush(win);                     /* the uncovered window is owed it */
    audit("redrawn after the clip refused");

    /* The menu set's name is the window's title, the same literal in both
     * places - a kernel bar reading PACCMAN over a window titled PaccMan is
     * the drift LESSONS.md 8 records. */
    if (strcmp(pmc_mset.name, PMC_TITLE))
        fail("the menu set's name is not the window title");
    if (pmc_mset.oncmd == 0)
        fail("os88_menu_set() did not patch the set - is it const?");

    /* PAUSE AND SOUND ARE GREYED WITH THEIR FACT (SPEC.md 47, 90): neither has
     * a body in this build - there is no tick loop and pmc_snd.c is a stub -
     * and a control that changes its own label and nothing else is worse than
     * one that says why it cannot act. The reason is the same for both because
     * it is the same fact. New Game and Full Screen do act and carry no
     * marker. kernel/menu.inc refuses a click on a MENU_DIS item, so the
     * toggle below is checked by calling pmc_sound_item directly. */
    if (strcmp(pmc_items[PMC_CMD_SOUND], "\x01" "Sound Off (No Game)"))
        fail("the Sound item does not start greyed as 'Sound Off (No Game)'");
    pmc_sound_on = 0;
    pmc_sound_item();
    if (strcmp(pmc_items[PMC_CMD_SOUND], "\x01" "Sound On (No Game)"))
        fail("the Sound item did not follow the state");
    pmc_sound_on = 1;
    pmc_sound_item();
    os88_oncmd(PMC_CMD_SOUND, 0, win);          /* refused, and it must be */
    if (strcmp(pmc_items[PMC_CMD_SOUND], "\x01" "Sound Off (No Game)"))
        fail("a greyed Sound item still acted on its command");
    /* THE ABOUT CARD FITS THE NARROWEST WINDOW IT WILL BE DRAWN IN. The card
     * clamps to the live content box, which here is the 224-pixel arcade
     * field, and the plan's first six lines were each cut off mid-word on the
     * glass. 24 cells of 8 pixels is the width and OS88UI_ABLH = 12 the line
     * pitch, so CGA's 144-row content is the height. Checked here because it
     * is a screendump on ONE adapter otherwise, and the narrow one is the
     * adapter nobody looks at (LESSONS.md 8). */
    { const char **l; int n = 0;
      for (l = pmc_about_lines; *l; l++) {
          if (**l == OS88_MENU_DIS)
              fail("an About line begins with the disabled marker");
          if ((int) strlen(*l) > 24)
              printf("pmcuitest: FAIL - About line over 24 cells: \"%s\"\n",
                     *l), hg_fails++;
          n++;
      }
      if (n * 12 > 144 - 12)
          fail("the About card is too tall for a CGA content box"); }
    { int i, dis = 0;
      for (i = 0; i < pmc_mset.menu[0].nitems; i++)
          if (pmc_mset.menu[0].items[i][0] == OS88_MENU_DIS) {
              dis++;
              /* SPEC.md 47 rule 3: a greyed label says WHY NOT, so it is
               * never the bare word the live item would have carried. */
              if (!strchr(pmc_mset.menu[0].items[i], '('))
                  fail("a greyed Game menu item does not say why not");
          }
      if (dis != 2)
          fail("the Game menu does not grey exactly Pause and Sound");
      if (pmc_items[PMC_CMD_NEW][0] == OS88_MENU_DIS
          || pmc_items[PMC_CMD_FULL][0] == OS88_MENU_DIS)
          fail("New Game or Full Screen is greyed - both act in this build");
      if (pmc_items[PMC_CMD_PAUSE][0] != OS88_MENU_DIS
          || pmc_items[PMC_CMD_SOUND][0] != OS88_MENU_DIS)
          fail("Pause or Sound is live - neither has a body in this build"); }

    write_vectors("build/pmcbandvec.inc");

    printf("\npmcuitest: %s (%d failure(s))%s\n",
           hg_fails ? "FAIL" : "PASS", hg_fails,
           PMC_T_BENCHED ? "" :
           "   [composer terms are 0: run `make pmcbandbench`]");
    return hg_fails ? 1 : 0;
}
