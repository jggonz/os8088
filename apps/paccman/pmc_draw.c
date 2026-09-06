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
 * into a canvas anyway, so an 896-byte band scratch does the same work in 3%
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

/* --- the scratch (SPEC.md 91: 896 + 896 + 224 bytes, all bss) ------------- */
static unsigned char pmc_band[8 * PMC_BAND_STRIDE];     /* packed 4bpp       */
static unsigned char pmc_planes[4 * PMC_PL_STEP];       /* four bitplanes    */
static unsigned char pmc_bits[8 * PMC_PL_STRIDE];       /* 1bpp              */

/* --- the layout, re-derived every frame ---------------------------------- */
static int pmc_fx, pmc_fy;      /* the field's top-left, absolute screen px  */
static int pmc_step;            /* source rows a screen row: 1, or 2 on CGA  */
static int pmc_rows;            /* a band's SCREEN rows: 8, or 4 on CGA      */
static int pmc_rsh;             /* ...as a SHIFT, 3 or 2. `ty * pmc_rows` is
                                 * a variable multiply, which on an 8086 is a
                                 * helper call the compiler would have to
                                 * bring in; the band pitch is a power of two
                                 * by construction, so it is a shift */
static int pmc_fh;              /* the field's SCREEN height, 288 or 144     */
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
        pmc_fh   = PMC_FIELD_H;
    } else {
        pmc_step = 2;
        pmc_rows = 4;
        pmc_rsh  = 2;
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
                 pmc_band + ((c - c0) << 2), pmc_step);
    }
    PMC_COUNT(pmc_n_tiles, cols);
    PMC_COUNT(pmc_n_bands, 1);
    PMC_COUNT(pmc_n_rows, pmc_rows);
    PMC_COUNT(pmc_n_rc, pmc_rows * cols);

    px = cols << 3;
    x = pmc_fx + (c0 << 3);
    y = pmc_fy + (ty << pmc_rsh);

    if (pmc_path == PMC_P_BLITP) {
        pmc_pack_pl(pmc_band, pmc_planes, pmc_rows, pmc_planar, cols);
        PMC_COUNT(pmc_n_pkpl, (unsigned) (pmc_rows * cols));
        PMC_COUNT(pmc_n_calls, 1);
        if (os88_gfx_blitp(pmc_planes, PMC_PL_STEP, PMC_PL_STRIDE,
                           x, y, px, pmc_rows) == 0)
            return;
        /* Refused after the probe said yes - a window moved between the two.
         * Fall through: the packed band is still exactly what blit4 wants. */
        pmc_path = PMC_P_BLIT4;
    } else if (pmc_path == PMC_P_BLIT1) {
        pmc_pack_1(pmc_band, pmc_bits, pmc_rows, pmc_mono2,
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
    os88_gfx_blit4(pmc_band, PMC_BAND_STRIDE, x, y, px, pmc_rows);
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
 * The caller holds the gfx lock. */
static void pmc_flush_laid(void *win)
{
    int ty;

    pmc_pick_path(win);
    for (ty = 0; ty < PMC_TILES_Y; ty++) {
        if (pmc_dmin[ty] > pmc_dmax[ty])
            continue;
        pmc_draw_band(ty, pmc_dmin[ty], pmc_dmax[ty]);
    }
    pmc_clean();
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
static void pmc_flush(void *win)
{
    if (pmc_about_up)
        return;
    if (!pmc_dirty_any())
        return;
    if (!pmc_layout(win))
        return;
    if (os88_wm_obscured(win) && os88_wm_clip_set(win) != 0)
        return;
    pmc_flush_laid(win);
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
            for (ty = ty0; ty <= ty1; ty++) {
                pmc_mark(c0, ty);
                pmc_mark(c1, ty);
            }
        }
    }

    pmc_letterbox(d.x1, d.y1, d.x2, d.y2);
    if (pmc_dirty_any())
        pmc_flush_laid(win);
    return 1;
}
