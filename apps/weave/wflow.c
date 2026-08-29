/* ============================================================================
 * os8088 - apps/weave/wflow.c
 *
 * THE FLOW WALK (WEAVE-SPEC 7). #included by apps/weave/weave.c.
 *
 * IT IS C ON PURPOSE, and WEAVE-SPEC 1.2 says why: it emits no gfx call, runs
 * over at most 250 records in microseconds (7.2), and has exactly one caller
 * until LOOM exists. The paint and hit-test cores beside it are assembly
 * (wdraw.inc) because they run under the gfx lock; this does not.
 *
 * When LOOM lands (wave 6) it takes THIS walk - moved to a shared include, or
 * called through one - and never a second copy. Two layouts that must agree
 * cell-for-cell is the failure WEAVE-SPEC 11's byte-identity rule exists to
 * prevent, said about code instead of about bundles.
 *
 * DETERMINISM IS THE CONTRACT. `weavesim --render` prints the cell rectangles
 * and the 8086 must reproduce them exactly (WEAVE-SPEC 12), on all three
 * adapters, at the opening size and at every size the user drags to. So
 * nothing here may round differently, take a shortcut for a common case, or
 * read a constant instead of the screen.
 *
 * ---------------------------------------------------------------------------
 * WHAT WAVE 2 LEAVES FOR THE NEXT AGENT, named rather than hidden:
 *
 *   - w_natural_w() / w_natural_h() are complete for every ctype. The `text`
 *     element's WRAPPED row count (7.3's greedy word wrap) is implemented here
 *     too, and is the one part of this file the three demo bundles do NOT
 *     exercise - none of them declares a <text>. Read 7.3 before changing it:
 *     the three things it pins beyond the pseudocode (pending emitted first on
 *     a hard break, the remainder becoming the new pending, an empty remainder
 *     adding no line) are each a case an implementer would otherwise guess.
 *   - PAINTING is apps/weave/wpaint.c's and apps/weave/wdraw.inc's, and this
 *     file's output - the w_lay[] table - is the whole interface between them.
 *     One table, read by the painter and by the hit test alike, so the drawn
 *     control and the clickable control cannot drift: apps/calc/calc.asm's
 *     cal_layout rule (SPEC.md 22's fm_hit discipline).
 * ==========================================================================*/

/* --- the cell grid (WEAVE-SPEC 7.1) ------------------------------------- */

/* w_grid - derive CW x CH and the pixel origin from the LIVE content box.
 * 0 = the window is not visible and there is nothing to lay out.
 *
 * The truth is the live screen and never the VGA reference constants
 * (SPEC.md 39): os88_wm_content() and os88_wm_geom() answer for THIS window
 * on the display it is actually on, which is also what makes this correct on
 * the two-card machine where a window dragged across the seam changes adapter.
 *
 * 7.1.2, and it is a shipped defect class rather than a theory: the origin's
 * x is rounded UP to a multiple of 8 at LAYOUT time, so that every cell column
 * lands on a multiple of 8 and OSAPI_FONT_RUN takes its single-store fast path
 * for every component in the family, at any window position, on any adapter.
 * Rounding DOWN would put the pen inside the frame. The round costs up to 7px
 * of width, which CW has already taken off - the os88line_cols pattern, so
 * nothing downstream of the walk learns about it. y is NOT aligned: font_run's
 * requirement is on x alone. */
static int w_grid(void *win)
{
    if (os88_wm_geom(win, &w_sz) != 0)
        return 0;
    os88_wm_content(win, &w_org);
    w_ox = (w_org.x + 7) & ~7;
    w_oy = w_org.y;
    w_cw = (w_org.x + w_sz.w - w_ox) / 8;
    w_ch = w_sz.h / 8;
    if (w_cw < 1 || w_ch < 1)
        return 0;
    return 1;
}

/* --- property lookup, for the natural sizes ----------------------------- */

/* w_pfind - the value of property `name` in the block at `off`, or 0 with
 * w_pfound left clear.  The block has already been validated (wval.c), so
 * this one may walk it without re-checking a bound: validation is a pass of
 * its own, before anything believes a byte. */
static int w_pfound;

static unsigned w_pfind(unsigned off, unsigned name)
{
    unsigned base, n;

    w_pfound = 0;
    if (off == W_NOPROPS)
        return 0;
    base = w_soff[W_PROPS];
    for (;;) {
        n = w_b(w_seg, base + off);
        if (n == 0)
            return 0;
        if (n == name) {
            w_pfound = 1;
            return w_w(w_seg, base + off + 2);
        }
        if (n > name)
            return 0;                   /* 2.6: names ascend, so we are past it */
        off += 4;
    }
}

/* --- 7.3's greedy word wrap --------------------------------------------- */

/* THE WRAP IS ONE BODY WITH TWO USES, and that is deliberate: the walk asks
 * it how many rows a `text` takes and the painter asks it to draw them, and
 * two implementations of 7.3 that must agree line-for-line is exactly the
 * drift this file's own header warns about one level up. `w_wr_draw` is the
 * only difference between the two calls.
 *
 * The draw context is statics rather than eight more arguments because an
 * out-parameter here is an address, and the address of an automatic is the
 * one thing this toolchain refuses to build (SPEC.md 73.5). Nothing
 * re-enters: the walk runs before the paint, never during it. */
static int w_wr_draw;                   /* 0 = count only */
static int w_wr_x, w_wr_y;              /* the pen, screen px */
static int w_wr_cells, w_wr_ink, w_wr_paper, w_wr_flags;
static int w_wr_max;                    /* rows there is room for */
static int w_wr_n;                      /* ...and rows emitted so far */

static void w_wr_line(unsigned off, unsigned len)
{
    if (w_wr_draw && w_wr_n < w_wr_max)
        w_draw_run(w_wr_x, w_wr_y + (w_wr_n << 3), w_seg, off, len,
                   w_wr_cells, w_wr_ink, w_wr_paper, w_wr_flags);
    w_wr_n++;
}

/* w_wrap - lay `len` bytes at seg:off out at `width` cells; the row count.
 *
 * NORMATIVE HERE rather than by reference (7.3): tools/htmsim.py's wrap() is
 * its one host implementation and the 8086 implements from the text, never
 * from that source. Break at spaces; a word longer than the width hard-breaks,
 * and on a hard break the pending line is emitted FIRST - the over-long word
 * starts a line of its own however much room the line in hand had left. The
 * remainder becomes the new pending line rather than a line of its own, so
 * the tail of a hard-broken word goes on collecting the words after it.
 *
 * The separator between two words is one space whatever whitespace stood there
 * in the source (3.1 collapses it), and `pending + 1 + word <= width` is the
 * whole of the fit test.
 *
 * TWO LENGTHS, NOT ONE: `pending` is the LOGICAL line - 7.3's arithmetic to
 * the letter, so the row count is the oracle's on every bundle - and `pspan`
 * is the run of source bytes that line came from, which is what gets drawn.
 * They differ only where a bundle's own text carries whitespace 3.1 says it
 * cannot, and there the drawn span is clipped to the component's cells by
 * w_draw_run rather than trusted. A hostile bundle gets a short line on the
 * glass; it does not get a different layout, and it does not get a buffer. */
static unsigned w_wrap(unsigned off, unsigned len, unsigned width)
{
    unsigned i, start, wl, pending, pspan, pstart;

    if (width < 1)
        width = 1;
    pending = 0;
    pspan = 0;
    pstart = 0;
    w_wr_n = 0;
    i = 0;
    while (i < len) {
        while (i < len && w_b(w_seg, off + i) == ' ')
            i++;
        if (i >= len)
            break;
        start = i;
        while (i < len && w_b(w_seg, off + i) != ' ')
            i++;
        wl = i - start;
        while (wl > width) {            /* too long for ANY line: break it */
            if (pending != 0) {
                w_wr_line(off + pstart, pspan);
                pending = 0;            /* 1.a.i: the pending line goes first */
            }
            w_wr_line(off + start, width);   /* 1.a.ii: the leading `width` */
            start += width;
            wl -= width;                /* 1.a.iii: and the remainder feeds
                                         * 1.b as the new pending line */
        }
        if (pending == 0) {
            pending = wl;
            pspan = wl;
            pstart = start;
        } else if (pending + 1 + wl <= width) {
            pending += 1 + wl;
            pspan = (start + wl) - pstart;
        } else {
            w_wr_line(off + pstart, pspan);
            pending = wl;
            pspan = wl;
            pstart = start;
        }
    }
    if (pending != 0)
        w_wr_line(off + pstart, pspan);
    if (w_wr_n == 0)
        w_wr_n = 1;                     /* 7.3's floor: a component with no
                                         * content still occupies one row, so
                                         * that it can be seen, hit, and given
                                         * content later from script */
    return w_wr_n;
}

/* --- 7.3's natural sizes ------------------------------------------------ */

/* The answer as a static: an out-parameter is an address, and the address of
 * an automatic is the one thing this toolchain refuses to build (SPEC.md
 * 73.5). */
static unsigned w_nat_w;

/* w_natural_w - 7.3's natural-width column, into w_nat_w. */
static void w_natural_w(int i)
{
    unsigned ct, props, a, n, k, longest, off, len;

    ct = w_lay[i].ctype;
    props = w_lay[i].props;
    w_nat_w = 1;

    switch (ct) {
    case WC_LABEL:
        a = w_pfind(props, WA_TEXT);
        n = w_pfound ? w_atom_len(a) : 0;
        w_nat_w = n > 0 ? n : 1;        /* 7.3's floor of 1 - without it an
                                         * empty label is 0 cells wide and
                                         * 7.2's `x += w + 1` collapses it
                                         * into its neighbour's gutter */
        break;
    case WC_TEXT:
    case WC_RULE:
    case WC_GRID:
        w_nat_w = w_cw;                 /* a full row */
        break;
    case WC_BOX:                        /* declared, and REQUIRED: wval.c
                                         * refuses a box with either at 0 */
    case WC_SPACER:
    case WC_CANVAS:                     /* the record already carries w/8 and
                                         * ceil(h/8), never 0 (2.5) */
        w_nat_w = w_lay[i].cw;
        break;
    case WC_METER:
        w_nat_w = 10;
        break;
    case WC_BUTTON:
    case WC_CHECK:
    case WC_RADIO:
        a = w_pfind(props, WA_LABEL);
        n = w_pfound ? w_atom_len(a) : 0;
        w_nat_w = n + 2;
        break;
    case WC_INPUT:
        n = w_pfind(props, WA_COLS);
        w_nat_w = n + 2;
        break;
    case WC_LIST:
        longest = 0;
        off = w_pfind(props, WA_ITEMS);
        if (w_pfound) {
            off += w_soff[W_PROPS];
            n = w_b(w_seg, off);
            for (k = 0; k < n; k++) {
                len = w_atom_len(w_b(w_seg, off + 1 + k));
                if (len > longest)
                    longest = len;
            }
        }
        if (longest < 1)
            longest = 1;
        w_nat_w = longest + 3;          /* + the scroll bar */
        break;
    default:
        break;
    }
    if (w_nat_w < 1)
        w_nat_w = 1;
}

/* w_natural_h - 7.3's natural-height column, AT THE COMPONENT'S OWN WIDTH.
 *
 * `w` is the width the walk settled on - declared or natural, clamped to CW -
 * and it is an argument rather than a second read of the record because 7.3's
 * `text` row is "wrapped row count AT ITS WIDTH". A <text w="20"> wraps at 20
 * and not at CW, which is what weavesim's natural_h(rt, comp, w, ...) does and
 * what --render therefore diffs against. Reading CW here instead is a layout
 * that agrees with the oracle on every bundle that declares no width and
 * disagrees on the first one that does.
 *
 * 0 means "deferred to pass 2" and only a <grid> answers it - its natural
 * height is CH minus the rows consumed ABOVE it, which is not known until the
 * rows are stacked. */
static unsigned w_natural_h(int i, unsigned w)
{
    unsigned ct, props, a, h;

    ct = w_lay[i].ctype;
    props = w_lay[i].props;

    switch (ct) {
    case WC_TEXT:
        a = w_pfind(props, WA_TEXT);
        if (w_pfound && w_atom_len(a) > 0) {
            w_wr_draw = 0;
            return w_wrap(w_atom_off(a), w_atom_len(a), w);
        }
        return 1;
    case WC_BOX:
    case WC_CANVAS:
        return w_lay[i].ch;
    case WC_BUTTON:
    case WC_CHECK:
    case WC_RADIO:
    case WC_INPUT:
        return 2;
    case WC_LIST:
        h = w_pfind(props, WA_ROWS);
        return h < 1 ? 1 : h;
    case WC_GRID:
        return 0;                       /* deferred: CH - the rows above */
    default:
        return 1;                       /* label, rule, spacer, meter */
    }
}

/* --- 7.2's walk --------------------------------------------------------- */

/* w_flow - lay the entry card out on the current CW x CH grid.
 *
 * One pass over the card's REC_COMP records in UISTREAM order. Sprites are
 * skipped: they live inside their canvas and are not flow components.
 * HIDDEN components still take part - hiding does not reflow, which is what
 * keeps hide/show at one or two primitive calls.
 *
 * It re-runs at open and at EVERY resize. There are no anchor springs, no
 * constraint solver and no baked positions, because the three adapters have
 * three different CW x CH and the user can drag to a fourth. */
static void w_flow(void)
{
    unsigned s, n, i, rec, kind, id, card, ct, w, h;
    unsigned x, row, first, k, sum, cnt, maxh, slack, align, ytop;

    w_nlay = 0;
    w_nrow = 0;
    if (w_state != W_ST_RUN)
        return;

    s = w_soff[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    card = 0;
    x = 0;
    row = 0;
    first = 1;                          /* the current row has no component */

    for (i = 0; i < n; i++) {
        rec = s + W_REC_SIZE * i;
        kind = w_b(w_seg, rec);
        if (kind == W_REC_END)
            break;
        if (kind == W_REC_CARD) {
            card = w_b(w_seg, rec + W_R_ID);
            continue;
        }
        if (card != w_entry)
            continue;                   /* another card: not laid out */
        ct = w_b(w_seg, rec + W_R_CTYPE);
        if (ct == WC_SPRITE)
            continue;
        if (w_nlay >= W_MAXLAY)
            break;

        id = w_nlay;
        w_lay[id].id = w_b(w_seg, rec + W_R_ID);
        w_lay[id].ctype = ct;
        w_lay[id].style = w_b(w_seg, rec + W_R_STYLE);
        w_lay[id].cflags = w_b(w_seg, rec + W_R_CFLAGS);
        w_lay[id].props = w_w(w_seg, rec + W_R_PROPS);
        w_lay[id].cw = w_b(w_seg, rec + W_R_W);
        w_lay[id].ch = w_b(w_seg, rec + W_R_H);

        w_natural_w(id);
        w = w_b(w_seg, rec + W_R_W);
        if (w == 0)
            w = w_nat_w;
        if (w > w_cw)
            w = w_cw;                   /* clamped to CW, so slack is never
                                         * negative (7.2) */
        h = w_b(w_seg, rec + W_R_H);
        if (h == 0)
            h = w_natural_h(id, w);     /* AT THE SETTLED WIDTH (7.3), and 0
                                         * back from it means DEFERRED (a grid) */

        /* Close the row before this component when it asked for a break, or
         * when it will not fit. Closing an EMPTY row is a NO-OP: a row has no
         * height without a component in it, and an implementation that emits
         * one has no max() to take. */
        if ((w_lay[id].cflags & CF_BREAK) || (x > 0 && x + w > w_cw)) {
            if (!first) {
                row++;
                if (row >= W_MAXROW)
                    break;
            }
            x = 0;
            first = 1;
        }

        w_lay[id].row = row;
        w_lay[id].cx = x;
        w_lay[id].cw = w;
        w_lay[id].ch = h;
        x += w + 1;                     /* one gutter cell */
        first = 0;
        w_nlay++;
    }
    w_nrow = first ? row : row + 1;     /* the last row, by the same no-op rule */

    /* --- pass 2: row heights, the alignment shift, and the stacking ------
     * A row of n components carries n-1 gutters, NOT n: the gutter left after
     * the last component is the space before a component that went to the next
     * row, and is not part of this one. */
    ytop = 0;
    for (row = 0; row < w_nrow; row++) {
        sum = 0;
        cnt = 0;
        maxh = 0;
        align = 0;
        for (k = 0; k < w_nlay; k++) {
            if (w_lay[k].row != row)
                continue;
            if (cnt == 0)
                align = (w_lay[k].style & WS_ALIGN) >> WS_ALIGNSH;
            if (w_lay[k].ch == 0)       /* the deferred <grid> height */
                w_lay[k].ch = (w_ch > ytop + 6) ? (w_ch - ytop) : 6;
            sum += w_lay[k].cw;
            cnt++;
            if (w_lay[k].ch > maxh)
                maxh = w_lay[k].ch;
        }
        if (cnt == 0)
            continue;
        slack = w_cw - (sum + (cnt - 1));
        if (align == 1)
            slack = slack / 2;
        else if (align == 0)
            slack = 0;
        for (k = 0; k < w_nlay; k++) {
            if (w_lay[k].row != row)
                continue;
            w_lay[k].cx += slack;       /* ALIGN of the row's FIRST component
                                         * shifts the whole row (7.2) */
            w_lay[k].cy = ytop;         /* components shorter than their row
                                         * top-align within it */
        }
        ytop += maxh;                   /* rows abut: no blank pixel row */
    }

    /* Remember the grid this table was built for, so a repaint that changed
     * nothing does not pay for the walk again (7.2 is microseconds, but a
     * paint that re-derives its layout every time is how a layout and a hit
     * test come to disagree by one resize). */
    w_lay_cw = w_cw;
    w_lay_ch = w_ch;
    w_lay_card = w_entry;
}
