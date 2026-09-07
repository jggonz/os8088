/* ============================================================================
 * os8088 - apps/paccman/pmc_draw.c     the redraw path (SPEC.md 91)
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a. Nothing of the reference's own renderer is
 * ported - it is a GPU tile pipeline through sokol_gfx - but the 28x36 field,
 * the tile and colour codes it composes and the arcade ROM tables it composes
 * them from are the reference's, and the tables are Namco's arcade ROM data.
 * See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * THE SHAPE, AND WHERE IT CAME FROM
 * ---------------------------------------------------------------------------
 * From the shipped assembly Pac-Man (SPEC.md 89.2), as a PRECEDENT and not as
 * code: dirty 8-row tile bands composed in the package's own RAM by assembly
 * inner loops and sent with ONE blit per band; 1bpp packed bands on a
 * monochrome adapter; alternate-row sampling where the display is short.
 *
 * What is NOT carried from it is its 28,160-byte persistent canvas. The damage
 * span of a band is exactly the set of tiles that would have to be recomposed
 * into a canvas anyway, so a 960-byte band scratch does the same work in 3%
 * of the memory (SPEC.md 91).
 *
 * ---------------------------------------------------------------------------
 * THREE BLIT PATHS, ONE PER FRAME, AND THE CHOICE IS ASKED NOT ASSUMED
 * ---------------------------------------------------------------------------
 *   PMC_P_BLITP  bpp 4, nothing covering us, and the probe says yes: the band
 *                is repacked into four bitplanes and goes down as four
 *                rep movsb a row (SPEC.md 5.4.3). THIS IS THE WHOLE OF THE
 *                "maybe more performant on XTs" hypothesis - and at THIS
 *                wave's arithmetic it is a 1.9% loss, because the repack
 *                costs more than the blit saves. pmc_pick_path says why it
 *                is chosen anyway and what wave 2 removes to bank it.
 *   PMC_P_BLIT4  bpp 4 and BLITP refused - a covering window, a straddle, a
 *                kern_small kernel. The packed band goes down unchanged,
 *                because the composer's own format IS os88_gfx_blit4's.
 *                At 224 pixels wide this ALWAYS takes kernel/vga12.inc's
 *                planar decoder rather than its run path, which is the
 *                expensive case and is why the fallback is measured and not
 *                merely present.
 *   PMC_P_BLIT1  bpp 1: the band is packed to one bit a pixel through the
 *                monochrome class table, with os88_gfx_blit4 behind it if
 *                os88_gfx_blit1 refuses (a kern_small kernel carries the slot
 *                without the body).
 *
 * The probe is SPEC.md 5.4.3.2's: plane_step | OS88_BLITP_PROBE decides every
 * refusal against the rect alone, reads no planes and writes no pixel. It is
 * asked once a frame on the WHOLE field, so a straddle anywhere in the field
 * is caught and not just one in the first band, and a refusal changes nothing
 * so asking again next frame is the intended use.
 * ==========================================================================*/

/* --- the scratch (SPEC.md 91: 960 + 896 + 224 bytes, all bss) -------------
 * pmc_band's rows are PMC_BAND_ROW apart and the FIELD starts at
 * + PMC_BAND_PAD: a sprite at the tunnel mouth hangs eight pixels off each
 * end of the field and the slack is what lets _pmc_sprite write its sixteen
 * pixels without testing a bound (pmcband.inc). Nothing outside this file
 * sees it - pmc_face is what the composer, the packers and the blits are all
 * given. */
static unsigned char pmc_band[8 * PMC_BAND_ROW];        /* packed 4bpp       */
static unsigned char pmc_planes[4 * PMC_PL_STEP];       /* four bitplanes    */
static unsigned char pmc_bits[8 * PMC_PL_STRIDE];       /* 1bpp              */

#define pmc_face  (pmc_band + PMC_BAND_PAD)             /* the field's own   */

/* pmc_brev - one source byte with its four 2-bit pixel fields reversed, which
 * is the whole of _pmc_sprite's flipx (pmcband.inc). Built once rather than
 * generated: it is a lowering of THIS composer and not anything the reference
 * has, so it does not belong in the generated pmc_rom.c. */
static unsigned char pmc_brev[256];

static void pmc_brev_init(void)
{
    int b;

    for (b = 0; b < 256; b++)
        pmc_brev[b] = (unsigned char) (((b & 3) << 6) | ((b & 0x0C) << 2)
                                       | ((b >> 2) & 0x0C) | ((b >> 6) & 3));
}

/* --- the layout, re-derived every frame ---------------------------------- */
static int pmc_fx, pmc_fy;      /* the field's top-left, absolute screen px  */
/* THE THREE BAND-GEOMETRY WORDS ARE INITIALISED, and to the TALL layout.
 * pmc_spr_band - which the sprite markers ask before a frame is composed -
 * reads them, and pmc_rows = 0 (which is what bss would give) answers "this
 * sprite is on no band at all" for every sprite. The tall values make an
 * unlaid-out marker a SUPERSET, which is the direction a marker is allowed to
 * be wrong in. Eight bytes of .data against a silent no-op. */
static int pmc_step = 1;        /* source rows a screen row: 1, or 2 on CGA  */
static int pmc_rows = 8;        /* a band's SCREEN rows: 8, or 4 on CGA      */
static int pmc_rsh = 3;         /* ...as a SHIFT, 3 or 2. `ty * pmc_rows` is
                                 * a variable multiply, which on an 8086 is a
                                 * helper call the compiler would have to
                                 * bring in; the band pitch is a power of two
                                 * by construction, so it is a shift */
static int pmc_fh;              /* the field's SCREEN height, 288 or 144     */
static int pmc_ssh;             /* pmc_step as a SHIFT, 0 or 1 - a sprite's
                                 * first band row is a divide by the step and
                                 * a variable divide is a `div` (SPEC.md 91) */
static int pmc_bpp;             /* of the display THIS WINDOW is on          */
static int pmc_path;            /* PMC_P_*                                   */
static int pmc_cw, pmc_ch;      /* the live content box                      */
static int pmc_cx, pmc_cy;      /* ...and its origin                         */

/* The harness's cost counters. Nothing in the shipping build reads one; they
 * are how hosttest/pmcuitest.c prices a frame in the bench's measured terms
 * (SPEC.md 91), and PMC_HOST is defined only by that harness's compile. */
#ifdef PMC_HOST
static unsigned pmc_n_calls, pmc_n_tiles, pmc_n_bands, pmc_n_rows;
static unsigned pmc_n_rc;       /* sum of rows x columns over the bands -
                                 * the packers and the blits are both
                                 * priced per row AT A WIDTH, so a
                                 * partial span costs its share */
static unsigned pmc_n_fillpx;   /* PIXELS filled, and rows of them. A fill
                                 * priced at the call floor alone reports
                                 * 64,512 overdrawn pixels as 0.8 ms and
                                 * hides exactly the double-draw that put
                                 * them there, so the harness prices these
                                 * from PERFORMANCE.md's measured gfx_fill
                                 * rates instead */
static unsigned pmc_n_fillrows;
static unsigned pmc_n_pkpl;     /* rows x columns sent through pmc_pack_pl  */
static unsigned pmc_n_spr;      /* sprite-bands composed, and their rows -
                                 * the frame's second-largest term after the
                                 * repack, and the one the plan could not
                                 * price before wave 2 measured it */
static unsigned pmc_n_sprow;
static unsigned pmc_n_gtick;    /* game_tick()s run for this frame - the C
                                 * logic is the fourth of SPEC.md 91's four
                                 * costs and the only one that is not drawing */
static unsigned pmc_n_pk1;      /* ...and through pmc_pack_1. PRICED FROM
                                 * HERE and not from whether the blit that
                                 * followed succeeded, because packing whose
                                 * output is then discarded is the whole cost
                                 * a latched refusal saves: unlatched, a
                                 * kern_small repaint packs 36 bands to 1bpp
                                 * and blits none of them */
#define PMC_COUNT(v, n) ((v) += (n))
#else
#define PMC_COUNT(v, n) ((void) 0)
#endif

/* pmc_layout - ask, never assume (SPEC.md 39). Returns 0 if the window is not
 * visible, in which case there is nothing to draw.
 *
 * os88_wm_display() rather than os88_video(): os88_video() answers about the
 * PRIMARY, and on a two-card machine this window can be dragged onto the other
 * display, where the bpp that decides the whole drawing path is different
 * (SPEC.md 39.16.4).
 *
 * The x inset is rounded DOWN to a multiple of 8 because os88_gfx_blitp and
 * os88_gfx_blit1 both refuse an unaligned x, and os88_wm_snap() has already
 * put the content origin on one - so what is left to align is our own inset
 * (SPEC.md 11.94). */
static int pmc_layout(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    static struct os88_video vd;
    int inset;

    if (os88_wm_geom(win, &sz) < 0)
        return 0;
    os88_wm_content(win, &org);
    os88_wm_display(win, &vd);

    pmc_bpp = vd.bpp;
    pmc_cw = sz.w;
    pmc_ch = sz.h;
    pmc_cx = org.x;
    pmc_cy = org.y;

    /* Every source row where the content can hold all 288 of them, alternate
     * rows where it cannot - which on CGA's 200-line screen it never can. */
    if (sz.h >= PMC_FIELD_H) {
        pmc_step = 1;
        pmc_rows = 8;
        pmc_rsh  = 3;
        pmc_ssh  = 0;
        pmc_fh   = PMC_FIELD_H;
    } else {
        pmc_step = 2;
        pmc_rows = 4;
        pmc_rsh  = 2;
        pmc_ssh  = 1;
        pmc_fh   = PMC_FIELD_H / 2;
    }

    inset = sz.w - PMC_FIELD_W;
    if (inset < 0)
        inset = 0;
    pmc_fx = org.x + ((inset >> 1) & ~7);

    inset = sz.h - pmc_fh;
    if (inset < 0)
        inset = 0;
    pmc_fy = org.y + (inset >> 1);
    return 1;
}

/* pmc_pick_path - one probe a frame, on the whole field.
 *
 * THE BLITP ARM IS A MEASURED LOSS TODAY AND IS KEPT DELIBERATELY. SPEC.md
 * 90's bench prices a colour band composed-packed-REPACKED-and-BLITP'd at
 * 69.78 ms against 68.49 for the same band composed and sent straight through
 * BLIT4: ~1.3 ms a band, ~46 ms on the 2,512 ms full repaint, ~1.9%, plus one
 * os88_wm_obscured and one real OSAPI_GFX_BLITP probe (a gfx call, ~813 us) on
 * every frame that has anything to draw. So the comment block at the top of
 * this file describes the hypothesis and NOT the arithmetic of this wave.
 *
 * It is not switched to BLIT4 because the loss is entirely the REPACK - the
 * blit itself is 7.36 ms against 48.33, which is 6.6x and is the whole premise
 * - and wave 2 composes into planar DIRECTLY on the colour path, at which
 * point the 41.78 ms repack disappears and this arm wins by 5.9x on the blit.
 * The path has to stay chosen, exercised and measured until then, or the wave
 * that removes the repack has nothing to remove it from. */
static void pmc_pick_path(void *win)
{
    if (pmc_bpp == 1) {
        pmc_path = PMC_P_BLIT1;
        return;
    }
    pmc_path = PMC_P_BLIT4;
    if (os88_wm_obscured(win))
        return;
    if (os88_gfx_blitp((void *) 0, OS88_BLITP_PROBE, 0, pmc_fx, pmc_fy,
                       PMC_FIELD_W, pmc_fh) == 0)
        pmc_path = PMC_P_BLITP;
}

/* ==========================================================================
 * THE SPRITE LAYER
 *
 * The arcade board composites a tile layer and then a sprite layer over it,
 * and pacman.c's renderer does the same; so does this, band by band. Colour
 * index 0 is the transparent one, which is why a sprite is merged into the
 * band rather than written over it (pmcband.inc's _pmc_sprite).
 * ========================================================================*/

/* k * PMC_BAND_ROW without a multiply: 120 is not a power of two, and an
 * `imul` by a constant is a shift/add chain tools/cc8086.py refuses whenever
 * it cannot prove a scratch register dead (docs/C-TOOLCHAIN.md). */
static const unsigned int pmc_rowoff[8] = {
    0, PMC_BAND_ROW, 2 * PMC_BAND_ROW, 3 * PMC_BAND_ROW,
    4 * PMC_BAND_ROW, 5 * PMC_BAND_ROW, 6 * PMC_BAND_ROW, 7 * PMC_BAND_ROW
};

/* pmc_spr_band - does a sprite whose top source row is `sy` put a single
 * PIXEL on band `ty`, given the layout pmc_layout has chosen?
 *
 * IT IS THE RENDERER'S OWN TEST AND EVERY MARKER USES IT. On the short (CGA)
 * layout a band SAMPLES source rows base, base+2, base+4, base+6, so a sprite
 * whose top row is base+7 lands on no sampled row at all and draws nothing -
 * while a plain source-row overlap test (`sy <= base + 7`) says it does. That
 * disagreement cost one whole band composed and blitted for zero visible
 * change, on one sprite vertical phase in eight.
 *
 * The markers must stay a SUPERSET of what pmc_band_sprites draws, which is
 * why this is the identical arithmetic and not an approximation of it. */
static int pmc_spr_band(int sy, int ty)
{
    int d, k0, k1;

    d = sy - (ty << 3);
    if (d > 7 || d < -15)               /* wholly below / wholly above */
        return 0;
    k0 = d > 0 ? ((d + pmc_step - 1) >> pmc_ssh) : 0;
    k1 = (d + 15) >> pmc_ssh;           /* d + 15 >= 0 here, so the shift is
                                         * never a negative one */
    if (k1 > pmc_rows - 1)
        k1 = pmc_rows - 1;
    return k0 <= k1;
}

/* pmc_band_sprites - every enabled sprite that lands on band `ty` AND on the
 * span being composed, merged over the tiles already in the scratch. `c0` is
 * the span's first column, so band pixel 0 is field pixel c0 * 8 - 8.
 *
 * IT NEVER CLIPS SIDEWAYS and it does not have to: pmc_widen has already
 * grown this span to contain every sprite that overlaps it, and the band
 * scratch carries PMC_BAND_PAD bytes of slack at each end for the eight
 * pixels a sprite hangs off the field at the tunnel mouth.
 *
 * THE OVERLAP TEST IS NOT AN OPTIMISATION. A row can carry two spans now, and
 * a sprite that overlaps the other one is NOT contained in this one - writing
 * it would run off the end of the scratch. The predicate here and pmc_widen's
 * are the same one, so a sprite is either widened into this span or skipped
 * from it, never neither. */
static void pmc_band_sprites(int ty, int c0, int c1)
{
    int i, sx, sy, d, k0, k1, srow, sinc, bx0, bp2, flags, base, sc0, sc1;

    base = ty << 3;                     /* the band's first SOURCE row */
    for (i = 0; i < PMC_NSPR; i++) {
        if (!pmc_sp_on[i])
            continue;
        sx = pmc_sp_x[i];
        sy = pmc_sp_y[i];

        sc0 = sx >> 3;
        if (sc0 < 0)
            sc0 = 0;
        sc1 = (sx + 15) >> 3;
        if (sc1 > PMC_TILES_X - 1)
            sc1 = PMC_TILES_X - 1;
        if (sc1 < c0 || sc0 > c1)
            continue;

        /* Which band ROWS this sprite lands on. Band row k samples source row
         * base + (k << pmc_ssh), so on CGA half the sprite's rows are not
         * sampled at all - the alternate-row layout doing to a sprite exactly
         * what it does to a tile. pmc_spr_band above is this same arithmetic
         * reduced to a yes/no, and the markers ask it. */
        d = sy - base;
        k0 = d > 0 ? ((d + pmc_step - 1) >> pmc_ssh) : 0;
        k1 = (sy + 15 - base) >> pmc_ssh;
        if (k1 > pmc_rows - 1)
            k1 = pmc_rows - 1;
        if (k0 > k1)
            continue;

        srow = (base + (k0 << pmc_ssh)) - sy;        /* 0..15 */
        sinc = 4 << pmc_ssh;
        if (pmc_sp_flip[i] & 2) {                    /* flipy: walk back up */
            srow = 15 - srow;
            sinc = -sinc;
        }

        /* The destination byte and its nibble. bx0 is the sprite's left edge
         * in BAND pixels and can be as low as -8; + 8 makes both shifts
         * unsigned, and PMC_BAND_PAD is exactly those 8 pixels' 4 bytes. */
        bx0 = sx - (c0 << 3);
        bp2 = bx0 + 8;
        flags = (bp2 & 1) | (pmc_sp_flip[i] & 1 ? 2 : 0);

        PMC_COUNT(pmc_n_spr, 1);
        PMC_COUNT(pmc_n_sprow, (unsigned) (k1 - k0 + 1));
        pmc_sprite(pmc_sprites + (pmc_sp_tile[i] << 6) + (srow << 2),
                   pmc_pal + ((pmc_sp_col[i] & 31) << 2),
                   pmc_band + (bp2 >> 1) + pmc_rowoff[k0],
                   sinc, k1 - k0 + 1, flags, pmc_brev);
    }
}

/* --- the sprite shadow ----------------------------------------------------
 * What each sprite looked like the last time a frame was composed. A sprite
 * whose position, tile, colour, flip or enablement differs from its shadow
 * marks BOTH rectangles - where it was and where it is - because the tiles
 * under the old one have to be put back.
 *
 * This is the whole reason a play frame is ten bands and not thirty-six. */
static unsigned char pmc_shon[PMC_NSPR];
static int           pmc_shx[PMC_NSPR];
static int           pmc_shy[PMC_NSPR];
static unsigned char pmc_shtile[PMC_NSPR];
static unsigned char pmc_shcol[PMC_NSPR];
static unsigned char pmc_shflip[PMC_NSPR];

/* pmc_mark_rect - the bands a 16x16 sprite at (sx, sy) is DRAWN on.
 *
 * A RANGE, not two point marks: a row carries two spans now and columns c0
 * and c1 of an odd-aligned sprite are two apart, so marking the ends alone
 * would open two spans and leave the column between them undrawn.
 *
 * pmc_spr_band and not a source-row overlap, for the reason written above it:
 * on the short layout the marker would otherwise claim a band the renderer
 * skips, and one band composed and blitted for nothing is ~17 ms of XT. */
static void pmc_mark_rect(int sx, int sy)
{
    int c0, c1, t0, t1, ty;

    c0 = sx >> 3;
    if (c0 < 0)
        c0 = 0;
    c1 = (sx + 15) >> 3;
    if (c1 > PMC_TILES_X - 1)
        c1 = PMC_TILES_X - 1;
    if (c0 > c1)
        return;
    t0 = sy >> 3;
    if (t0 < 0)
        t0 = 0;
    t1 = (sy + 15) >> 3;
    if (t1 > PMC_TILES_Y - 1)
        t1 = PMC_TILES_Y - 1;
    for (ty = t0; ty <= t1; ty++)
        if (pmc_spr_band(sy, ty))
            pmc_mark_span(c0, c1, ty);
}

static void pmc_mark_sprites(void)
{
    int i, ch;

    for (i = 0; i < PMC_NSPR; i++) {
        ch = pmc_sp_on[i] != pmc_shon[i];
        if (!ch && pmc_sp_on[i])
            ch = pmc_sp_x[i] != pmc_shx[i]
              || pmc_sp_y[i] != pmc_shy[i]
              || pmc_sp_tile[i] != pmc_shtile[i]
              || pmc_sp_col[i] != pmc_shcol[i]
              || pmc_sp_flip[i] != pmc_shflip[i];
        if (!ch)
            continue;
        if (pmc_shon[i])
            pmc_mark_rect(pmc_shx[i], pmc_shy[i]);
        if (pmc_sp_on[i])
            pmc_mark_rect(pmc_sp_x[i], pmc_sp_y[i]);
        pmc_shon[i]   = pmc_sp_on[i];
        pmc_shx[i]    = pmc_sp_x[i];
        pmc_shy[i]    = pmc_sp_y[i];
        pmc_shtile[i] = pmc_sp_tile[i];
        pmc_shcol[i]  = pmc_sp_col[i];
        pmc_shflip[i] = pmc_sp_flip[i];
    }
}

/* pmc_widen - a dirty span must CONTAIN every sprite that OVERLAPS it.
 *
 * A band can be dirty for a reason that has nothing to do with a sprite - the
 * score strip, a pill blinking, a menu's damage rect - and a sprite standing
 * over the part being recomposed still has to be composed back into it, or it
 * vanishes from that band for a frame. So the span grows to the sprite's
 * columns before anything is composed, which also relieves _pmc_sprite of
 * every sideways bound test it would otherwise make ~2,000 times a frame.
 *
 * OVERLAPS, not "lands on the band". With two spans a row, a sprite sitting
 * over span 2 has nothing to do with span 1 and widening span 1 to reach it
 * would swallow the whole gap the second span exists to avoid. The predicate
 * is exactly pmc_band_sprites' - overlap, and pmc_spr_band vertically - so a
 * sprite is either grown into a span or skipped from it, never neither.
 *
 * IT ITERATES. Growing span 1 can bring it under a sprite it did not overlap
 * before, and that sprite must then be contained too. The loop is bounded by
 * PMC_NSPR passes because each pass that changes nothing ends it and a span
 * can only grow. */
static int pmc_widen_span(unsigned char *lo, unsigned char *hi, int c0, int c1)
{
    int chg = 0;

    if (*lo > *hi)
        return 0;                       /* clean: nothing to contain */
    if (c1 < (int) *lo || c0 > (int) *hi)
        return 0;                       /* no overlap: not this span's sprite */
    if (c0 < (int) *lo) {
        *lo = (unsigned char) c0;
        chg = 1;
    }
    if (c1 > (int) *hi) {
        *hi = (unsigned char) c1;
        chg = 1;
    }
    return chg;
}

static void pmc_widen(int ty)
{
    int i, pass, chg, c0, c1;

    for (pass = 0; pass < PMC_NSPR; pass++) {
        chg = 0;
        for (i = 0; i < PMC_NSPR; i++) {
            if (!pmc_sp_on[i])
                continue;
            if (!pmc_spr_band(pmc_sp_y[i], ty))
                continue;
            c0 = pmc_sp_x[i] >> 3;
            if (c0 < 0)
                c0 = 0;
            c1 = (pmc_sp_x[i] + 15) >> 3;
            if (c1 > PMC_TILES_X - 1)
                c1 = PMC_TILES_X - 1;
            chg |= pmc_widen_span(&pmc_dmin[ty], &pmc_dmax[ty], c0, c1);
            chg |= pmc_widen_span(&pmc_dmin2[ty], &pmc_dmax2[ty], c0, c1);
        }
        if (!chg)
            break;
    }

    /* the two may have met: one band is one gfx call fewer than two */
    if (pmc_dmin2[ty] <= pmc_dmax2[ty]
        && (int) pmc_dmin2[ty] <= (int) pmc_dmax[ty] + 1 + PMC_DGAP) {
        if (pmc_dmax2[ty] > pmc_dmax[ty])
            pmc_dmax[ty] = pmc_dmax2[ty];
        pmc_dmin2[ty] = PMC_TILES_X;
        pmc_dmax2[ty] = 0;
    }
}

/* pmc_draw_band - compose tile row `ty`'s columns c0..c1 and send them with
 * ONE blit. The tiles are composed at (c - c0) so that the span starts at the
 * scratch's own first byte and neither packer needs an offset.
 *
 * THE GFX LOCK IS THE CALLER'S. Everything below is bounded by (c1 - c0 + 1)
 * tiles, which is at most 28, so the hold is a stated count (SPEC.md 73.11's
 * rule that no C between lock and unlock may be unbounded). */
static void pmc_draw_band(int ty, int c0, int c1)
{
    int c, i, cols, px, x, y;

    cols = c1 - c0 + 1;
    for (c = c0; c <= c1; c++) {
        i = (ty << PMC_VSHIFT) + c;
        pmc_tile(pmc_tiles + (pmc_vram[i] << 4),
                 pmc_pairs + ((pmc_cram[i] & 31) << 4),
                 pmc_face + ((c - c0) << 2), pmc_step);
    }
    PMC_COUNT(pmc_n_tiles, cols);
    pmc_band_sprites(ty, c0, c1);
    PMC_COUNT(pmc_n_bands, 1);
    PMC_COUNT(pmc_n_rows, pmc_rows);
    PMC_COUNT(pmc_n_rc, pmc_rows * cols);

    px = cols << 3;
    x = pmc_fx + (c0 << 3);
    y = pmc_fy + (ty << pmc_rsh);

    if (pmc_path == PMC_P_BLITP) {
        pmc_pack_pl(pmc_face, pmc_planes, pmc_rows, pmc_planar, cols);
        PMC_COUNT(pmc_n_pkpl, (unsigned) (pmc_rows * cols));
        PMC_COUNT(pmc_n_calls, 1);
        if (os88_gfx_blitp(pmc_planes, PMC_PL_STEP, PMC_PL_STRIDE,
                           x, y, px, pmc_rows) == 0)
            return;
        /* Refused after the probe said yes - a window moved between the two.
         * Fall through: the packed band is still exactly what blit4 wants. */
        pmc_path = PMC_P_BLIT4;
    } else if (pmc_path == PMC_P_BLIT1) {
        pmc_pack_1(pmc_face, pmc_bits, pmc_rows, pmc_mono2,
                   ty << pmc_rsh, cols);
        PMC_COUNT(pmc_n_pk1, (unsigned) (pmc_rows * cols));
        PMC_COUNT(pmc_n_calls, 1);
        if (os88_gfx_blit1(pmc_bits, PMC_PL_STRIDE, x, y, px, pmc_rows) == 0)
            return;
        /* A kern_small kernel carries the slot without the body (SPEC.md
         * 5.4.2). The packed band is still there, so blit4 draws the same
         * picture - in 4bpp pixels the 1bpp decoder will threshold.
         *
         * LATCH IT, exactly as the BLITP arm above does. This refusal is a
         * fact about the KERNEL and not about this rect, so it will refuse
         * for every one of the remaining bands too - and pmc_pack_1 is 18.13
         * ms of packing whose output blit4 then ignores. Unlatched, a repaint
         * pays that 36 times over (653 ms) for nothing. pmc_pick_path asks
         * again next frame, which is what makes latching safe. */
        pmc_path = PMC_P_BLIT4;
    }
    PMC_COUNT(pmc_n_calls, 1);
    os88_gfx_blit4(pmc_face, PMC_BAND_ROW, x, y, px, pmc_rows);
}

/* pmc_dirty_any - is there a single dirty band? SCAN FIRST, ASK THE KERNEL
 * SECOND. Most frames of a paused game, of the freeze between a life lost and
 * READY!, and of the whole time the About card is up write nothing at all, and
 * pmc_layout's three thunks plus pmc_pick_path's obscured test and its PROBE -
 * a real gfx call - came to ~1.1 ms charged to a frame that had nothing to
 * draw. 36 bytes of bss compared costs nothing beside it. */
static int pmc_dirty_any(void)
{
    int ty;

    for (ty = 0; ty < PMC_TILES_Y; ty++)
        if (pmc_dmin[ty] <= pmc_dmax[ty])
            return 1;
    return 0;
}

/* pmc_flush_laid - every dirty band, then clean, with the layout ALREADY
 * taken. It is split from pmc_flush because pmc_repaint needs the layout
 * itself (to turn a damage rect into tiles) and re-entering pmc_layout would
 * spend os88_wm_geom + os88_wm_content + os88_wm_display a second time for an
 * answer that cannot have changed under one lock hold.
 *
 * The caller holds the gfx lock.
 *
 * ---------------------------------------------------------------------------
 * `brk` - MAY THIS DROP THE LOCK PART WAY THROUGH?
 * ---------------------------------------------------------------------------
 * A worker takes the gfx lock "for a SHORT BURST" and a worker that computes
 * under it wedges the machine with no watchdog able to break it (apps/cc/
 * os88.h, SPEC.md 20.6 rule 3). An ordinary play frame is ten bands, ~70 ms
 * each on a 4.77 MHz 8088, and that is a burst.
 *
 * THE ROUND-WON FLASH IS NOT. game_update_tiles recolours the whole playfield
 * every time `since(WON) & 0x10` flips, which marks all 31 playfield bands at
 * their full width; the flag flips about eleven times over the four seconds
 * between after(WON, 60) and the READY! re-arm, and one flip is 31 x ~70 ms =
 * ~2.2 SECONDS of one uninterruptible lock hold. Nothing else on the machine
 * can draw for that long, eleven times over.
 *
 * So the WORKER's flush breaks its hold every PMC_HOLD_BANDS bands: unlock,
 * yield, lock again. What that costs is one task switch (693 us) per chunk;
 * what it buys is a machine whose menus, dock and other windows still answer
 * during the flash.
 *
 * ONLY THE WORKER. A key, a menu command and os88_paint all arrive INSIDE a
 * kernel callback that holds the lock on our behalf - dropping it there would
 * hand the glass away in the middle of the kernel's own paint pass - so those
 * callers pass brk = 0 and take the whole loop in one hold, exactly as they
 * did before.
 *
 * AFTER A RE-LOCK NOTHING IS ASSUMED. The clip region died at the unlock
 * (SPEC.md 11.3), the window may have been moved, resized, covered or sent
 * behind, so the layout, the region and the blit path are all taken again -
 * and a refusal RETURNS with the remaining spans still dirty, which is the
 * same contract pmc_flush's own refusal has. A band that has been drawn is
 * cleaned as it is drawn rather than in one pass at the end, so an early
 * return leaves exactly what is still owed.
 *
 * AND pmc_about_up IS RE-TESTED FIRST, ahead of the layout. It is the ONLY
 * thing keeping the field off the About card, pmc_flush's entry guard is its
 * one other reader, and the card is drawn by os88_about - a UI callback that
 * runs in precisely the window this break opens. Worker breaks at band 4; the
 * UI task takes the lock to drop the menu; the user picks About PaccMan; the
 * flag goes up, the card is painted, the callback returns and the lock is
 * released; the worker re-locks. Without this test it blits bands 5..35
 * straight through the card and nothing repaints it, because the kernel sends
 * no W_PAINT for a package's own overdraw - a card sitting holed until it is
 * dismissed. The exposure is not rare: the flush that breaks at all is the
 * LONG one (round-won flash, New Game, first paint), which is exactly the
 * multi-second window a menu click lands in. Returning costs nothing:
 * pmc_abdismiss re-marks what the card covered. */
#define PMC_HOLD_BANDS  4

static void pmc_flush_laid(void *win, int brk)
{
    int ty, n;

    pmc_pick_path(win);
    n = 0;
    for (ty = 0; ty < PMC_TILES_Y; ty++) {
        if (pmc_dmin[ty] > pmc_dmax[ty])
            continue;

        if (brk && n >= PMC_HOLD_BANDS) {
            n = 0;
            os88_gfx_unlock();
            os88_task_yield();
            os88_gfx_lock();
            if (pmc_about_up)
                return;
            if (!pmc_layout(win))
                return;
            if (os88_wm_obscured(win) && os88_wm_clip_set(win) != 0)
                return;
            pmc_pick_path(win);
        }

        pmc_widen(ty);
        pmc_draw_band(ty, pmc_dmin[ty], pmc_dmax[ty]);
        n++;
        if (pmc_dmin2[ty] <= pmc_dmax2[ty]) {
            pmc_draw_band(ty, pmc_dmin2[ty], pmc_dmax2[ty]);
            n++;
        }
        pmc_clean_row(ty);
    }
}

/* pmc_flush - the ordinary frame path, and the one that must ARM THE CLIP
 * REGION. The caller holds the gfx lock.
 *
 * A KEY, A MENU COMMAND AND (wave 2) A WORKER FRAME ALL ARRIVE WITH NO REGION
 * ARMED (SPEC.md 11.3, apps/cc/os88.h): the kernel arms one only around
 * W_PAINT, inside its own damage pass. So without this call an `N` or a
 * Game > New Game taken with the Disk window over us writes 36 band blits
 * straight across that window's pixels - 2,512 ms of XT spent corrupting
 * somebody else's glass, which the kernel then has to repaint. The precedent
 * this port takes its shape from does exactly this and no less:
 * apps/pacman/pacman.asm's pm_redraw calls OSAPI_WM_CLIP_SET first and returns
 * on CF, from its paint and from its worker alike.
 *
 * NOT in pmc_repaint. os88_paint is entered with the kernel's own region
 * already armed and re-arming it would throw that paint's damage rect away,
 * which is the whole reason os88_about_card_d exists.
 *
 * AND ONLY WHEN SOMETHING IS ACTUALLY COVERING US, WHICH IS NOT A SAVING BUT
 * THE PORT'S WHOLE PREMISE. SPEC.md 5.4.3.3: an armed clip region is one of
 * OSAPI_GFX_BLITP's six refusals and it has to be, because a plane byte
 * carries eight x's - and it is BINDING when the package armed it itself (the
 * kernel's own W_PAINT cull is the advisory one). So arming a region on every
 * frame would send every non-paint frame down BLIT4 for ever: 48.33 ms a band
 * against 7.36, and wave 2's planar-direct composer - the thing this whole
 * package is an experiment about - would have nothing left to win. When
 * os88_wm_obscured answers 0 there is nothing to cut against: we hold the gfx
 * lock, every pixel of our content is ours and visible, and the blit is safe
 * unclipped. When it answers 1 the region is armed, the fragments are cut, and
 * pmc_pick_path's own obscured test has already chosen BLIT4 for exactly the
 * same reason - so the two answers cannot disagree.
 *
 * The veto-vs-region trade is SPEC.md's worker rule 5, and it reads the other
 * way there ("it vetoes the whole frame for one covered pixel, which for a
 * worker that spends minutes on a frame is the wrong trade"). This worker
 * spends 70 ms on a band, so the region is affordable here and it is the
 * region that is taken; what is not affordable is arming one when nothing is
 * covering us.
 *
 * The region dies at the caller's next os88_gfx_unlock, so there is nothing to
 * undo. A refusal LEAVES THE SPANS DIRTY: not one pixel of us shows, and the
 * bands are owed again the moment the window is uncovered.
 *
 * IT ALSO REFUSES WHILE THE ABOUT CARD IS UP. The card is opaque over its own
 * rect and this path draws bands, not the card - so a New Game taken with the
 * card raised would blit the field through it and leave a hole. Nothing is
 * lost by refusing: the spans stay marked and pmc_abdismiss re-marks the whole
 * field anyway, so the picture the user asked for is drawn whole the moment
 * the card comes down. */
static void pmc_flush(void *win, int brk)
{
    if (pmc_about_up)
        return;
    if (!pmc_dirty_any())
        return;
    if (!pmc_layout(win))
        return;
    if (os88_wm_obscured(win) && os88_wm_clip_set(win) != 0)
        return;
    pmc_flush_laid(win, brk);
}

/* pmc_fill_clip - one black strip, cut to the rect we actually owe. An empty
 * strip costs no call at all. */
static void pmc_fill_clip(int x1, int y1, int x2, int y2,
                          int cx1, int cy1, int cx2, int cy2)
{
    if (x1 < cx1) x1 = cx1;
    if (y1 < cy1) y1 = cy1;
    if (x2 > cx2) x2 = cx2;
    if (y2 > cy2) y2 = cy2;
    if (x1 > x2 || y1 > y2)
        return;
    os88_gfx_fill(x1, y1, x2, y2);
    PMC_COUNT(pmc_n_calls, 1);
    PMC_COUNT(pmc_n_fillpx, (unsigned) ((x2 - x1 + 1) * (y2 - y1 + 1)));
    PMC_COUNT(pmc_n_fillrows, (unsigned) (y2 - y1 + 1));
}

/* pmc_letterbox - the black border around the field, cut to the owed rect.
 *
 * THE LETTERBOX, NOT THE CONTENT. os88_wm_ownbg(win, 1) traded away the
 * kernel's white fill, so the margin around a 224-wide field in a wider window
 * is ours to paint, and it must be BLACK because that is what the arcade
 * field's own border is. Filling the WHOLE content instead is the
 * erase-then-draw double-draw PERFORMANCE.md rule 2 names: every one of those
 * pixels is inside a band that covers it again a moment later.
 *
 * AT THE SHIPPED SIZE THERE IS NO LETTERBOX AT ALL. PMC_WIN_W gives a content
 * 224 wide = PMC_FIELD_W and PMC_WIN_H a content 288 deep = PMC_FIELD_H (144
 * against pmc_fh on the short layout), and os88_wm_minsize pins the window at
 * that size - so both insets pmc_layout computes are zero, all four strips are
 * empty, and the default window pays nothing here rather than 64,512
 * overdrawn pixels. A window the user has GROWN pays for the border it really
 * has and not a pixel more. */
static void pmc_letterbox(int cx1, int cy1, int cx2, int cy2)
{
    int l, r, t, b, fb, fr;

    l  = pmc_cx;
    r  = pmc_cx + pmc_cw - 1;
    t  = pmc_cy;
    b  = pmc_cy + pmc_ch - 1;
    fb = pmc_fy + pmc_fh - 1;               /* the field's last row  */
    fr = pmc_fx + PMC_FIELD_W - 1;          /* ...and its last column */

    if (pmc_fy <= t && fb >= b && pmc_fx <= l && fr >= r)
        return;                             /* no border: the shipped size */

    os88_set_color(OS88_BLACK);
    pmc_fill_clip(l, t, r, pmc_fy - 1, cx1, cy1, cx2, cy2);       /* above */
    pmc_fill_clip(l, fb + 1, r, b, cx1, cy1, cx2, cy2);           /* below */
    pmc_fill_clip(l, pmc_fy, pmc_fx - 1, fb, cx1, cy1, cx2, cy2); /* left  */
    pmc_fill_clip(fr + 1, pmc_fy, r, fb, cx1, cy1, cx2, cy2);     /* right */
}

/* pmc_repaint - os88_paint's body, and it ASKS WHAT IT OWES.
 *
 * os88_wm_ownbg(win, 1) is the whole precondition SPEC.md 11.90.2 puts on a
 * partial answer, and this window sets it - so throwing the answer away and
 * recomposing all 36 bands would be 36 x 69.78 ms = 2.51 SECONDS of XT for a
 * menu that dropped over the top three tile rows and owes 209 ms. That is
 * PERFORMANCE.md's "visible redraw" at the top of its scale, and an ordinary
 * menu close, a drag of another window across a corner or a toast dismissal
 * all reach it (kernel/menu.inc repaints through wm_paint_dmg).
 *
 * A damage rect is ABSOLUTE and INCLUSIVE and may be EMPTY, which means draw
 * nothing at all. Turning it into tiles is two shifts: the field's origin is
 * pmc_fx/pmc_fy, a column is 8 pixels and a band is 1 << pmc_rsh screen rows.
 * The two pmc_mark calls UNION the span with whatever play has already
 * dirtied rather than replacing it, which is what makes this safe to run on a
 * frame that is also mid-animation.
 *
 * IT ANSWERS whether this paint owed anything at all, which is what os88_paint
 * hangs the About card's redraw on: an empty rect is 11.90.2's "draw nothing",
 * and the card costs ~12 gfx calls and ~200 glyph cells whether or not there
 * was a frame under it. */
static int pmc_repaint(void *win)
{
    static struct os88_rect d;
    int whole, ty, ty0, ty1, c0, c1, x1, y1, x2, y2;

    if (!pmc_layout(win))
        return 0;

    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return 0;                           /* nothing to draw (11.90.2) */

    if (whole) {
        d.x1 = pmc_cx;
        d.y1 = pmc_cy;
        d.x2 = pmc_cx + pmc_cw - 1;
        d.y2 = pmc_cy + pmc_ch - 1;
        pmc_dirty_all();
    } else {
        /* the owed rect, intersected with the FIELD, in field pixels */
        x1 = d.x1 - pmc_fx;
        y1 = d.y1 - pmc_fy;
        x2 = d.x2 - pmc_fx;
        y2 = d.y2 - pmc_fy;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > PMC_FIELD_W - 1) x2 = PMC_FIELD_W - 1;
        if (y2 > pmc_fh - 1) y2 = pmc_fh - 1;
        if (x1 <= x2 && y1 <= y2) {
            c0  = x1 >> 3;
            c1  = x2 >> 3;
            ty0 = y1 >> pmc_rsh;
            ty1 = y2 >> pmc_rsh;
            /* A RANGE, not two point marks. A row carries two spans now
             * (pmc_vid.c), so marking the two ENDS of a damage rect would
             * open one span at each end and leave every column between them
             * undrawn - which is a menu dismissed over the top three rows
             * leaving a 26-column hole in each of them. */
            for (ty = ty0; ty <= ty1; ty++)
                pmc_mark_span(c0, c1, ty);
        }
    }

    pmc_letterbox(d.x1, d.y1, d.x2, d.y2);
    if (pmc_dirty_any())
        pmc_flush_laid(win, 0);     /* inside the kernel's own paint pass, with
                                     * its region armed: the lock is not ours
                                     * to drop */
    return 1;
}
