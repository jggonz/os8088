/* ============================================================================
 * os8088 - apps/weave/wpaint.c
 *
 * THE COMPONENT PAINTER AND THE HIT TEST (WEAVE-SPEC 6). #included by
 * apps/weave/weave.c, after wflow.c: it reads that file's w_lay[] table and
 * nothing else decides where anything goes.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE LINE IS
 * ---------------------------------------------------------------------------
 * This file resolves - which property carries a label, where an atom's bytes
 * are, how many items a list has, which row the selection is on - and
 * apps/weave/wdraw.inc DRAWS. Every call out of here hands wdraw.inc a rect in
 * screen pixels and arguments already reduced to words and pointers. That is
 * WEAVE-SPEC 1.2's seam: the drawing cores are assembly `%include`d by BOTH
 * packages, because LOOM's Preview paints with them and there is no other
 * code-sharing mechanism on this platform (SPEC.md 20.5.1).
 *
 * ---------------------------------------------------------------------------
 * ONE TABLE, TWO READERS
 * ---------------------------------------------------------------------------
 * w_rect[] is apps/calc/calc.asm's cal_layout, in C: the flow walk's CELL
 * rectangles converted once to ABSOLUTE SCREEN pixels, read by the painter and
 * by the hit test alike, so the drawn control and the clickable control cannot
 * drift (SPEC.md 22's fm_hit discipline). It is rebuilt on every edge rather
 * than cached, because the window MOVES: the walk's answer is in cells and
 * survives a move, and the pixels do not.
 *
 * It holds a rect for every laid-out component whether or not that component
 * is on screen, and os88ui_bfind is given a COUNT - so a component the window
 * is too short to show is simply not reachable, and there is no second rule
 * about which rects are valid.
 *
 * ---------------------------------------------------------------------------
 * WHAT WAVE 2 DOES AND DOES NOT DO
 * ---------------------------------------------------------------------------
 * It PAINTS every component of WEAVE-SPEC 6 that has a picture in wave 2, and
 * it makes the two interactions that are wholly NATIVE work: a list scrolls
 * and its selection moves (WEAVE-SPEC 1.2 - "hit-test, widget arm/fire,
 * caret, selection XOR, scroll" are handled entirely natively and only
 * ENQUEUE an event). It does NOT arm a button, edit an input, fire a handler
 * or run one bytecode op: the event ring and the WVM are wave 3, and
 * w_onhit() below is the one place that grows.
 *
 * `grid` and `canvas` are drawn as their FRAME and nothing inside it. Their
 * bodies are the band composer (wave 4) and the sprite compositor (wave 5),
 * each with a gate of its own; a frame at the right rect is the honest wave-2
 * answer and is what makes the layout visible on the glass now.
 * ==========================================================================*/

/* One staged string. wdraw.inc clips independently against its own W_RUNMAX,
 * so the two sizes cannot corrupt each other if they ever differ - the worst
 * a mismatch buys is a shorter line. */
#define W_STRMAX    160

/* The list's scroll bar takes the last TWO cells of the component (16 px),
 * which is what WEAVE-SPEC 7.3's `longest + 3` leaves room for; os88ui's own
 * bars are 14 px and nothing in that file requires a width.
 *
 * W_SB_MINH is the shortest bar os88ui_sbar can draw INSIDE its rect: the two
 * arrow-cell rules sit at y1+10 and y2-10 (OS88UI_SBCELL = 10) and are drawn
 * unconditionally, so a bar shorter than about 22 px puts an hline outside the
 * rect it was given - over the component below it. Three rows is the floor,
 * and a list too short for a bar simply has none. */
#define W_SB_CELLS  2
#define W_SB_MINH   24

static int w_rect[W_MAXLAY * 4];        /* {x1,y1,x2,y2}, inclusive, SCREEN */
static int w_ybot;                      /* the last pixel row of the content */

static char w_str[W_STRMAX];            /* one resolved string at a time */
static int  w_sbblk[7];                 /* os88ui.inc's scroll block */

/* Per-component list state, by comp_id.  `sel` is stored PLUS ONE so that a
 * zeroed .bss is "nothing selected", which is the -1 the model starts a list
 * at (weavesim's Runtime, and 6.8's `sel` surface).
 *
 * Words rather than bytes, and 1KB for the pair is the price: wval.c caps the
 * items blob at 64 (2.6.1) so a byte would fit today, and a count that grows
 * would then wrap the clamp silently rather than clamping. Bss is SPEC.md
 * 73.9's cheap half and this is not the place to spend the saving.
 *
 * It survives the walk, which the layout table does not: a resize re-runs
 * w_flow() and rebuilds w_lay[] from scratch, and a scroll position that came
 * back to the top every time the user dragged the window would be a defect
 * nobody could name. */
/* BYTES, not words, and the cap that makes it safe is the FORMAT's: 2.6.1
 * bounds a list's items at 64 and wval.c refuses a blob over it, so a scroll
 * position is 0..64 and a selection 0..65. Wave 2 spent words here against a
 * count that might grow; it cannot grow without the bundle format changing,
 * and that change would come through wval.c first. 512 bytes of a package
 * whose resident count is the wave's own headline (1.2.1). */
static unsigned char w_lpos[256];
static unsigned char w_lsel1[256];

/* ============================================================================
 * THE COMPONENT STATE THE SCRIPT CAN MOVE (WEAVE-SPEC 6)
 *
 * By comp_id, because that is what WJS names and what the ring records carry
 * (2.5, 4.9). The UISTREAM record is the component's BIRTH state and never
 * changes - the bundle is read-only at run time (2.1) - so everything a
 * `SETP` writes lives here instead.
 *
 * w_ctext IS A GC ROOT (4.8.1). It holds 0 while the component still shows
 * the atom its record named, and a VM string handle once script has written
 * one; wevent.c marks every non-zero entry before a collection, and the
 * paragraph in 4.8.1 is about what happens if it does not.
 *
 * ...and w_cflag is the LIVE hidden/disabled pair, which is why the walk
 * takes those two bits from here rather than from the record it is reading:
 * one predicate, three consumers (SPEC.md 47 rule 4) - it greys the control,
 * refuses its click and answers `.enabled`.
 */
static unsigned char w_ctext[256];      /* a VM string handle, 0 = the atom -
                                         * a BYTE, because 4.8's table is 256
                                         * entries and handle 0 is never
                                         * allocated */
static int           w_cval[256];       /* meter .value / check .checked */
static int           w_cvold[256];      /* ...and what a meter last DREW,
                                         * which 6.4's delta needs */
static unsigned char w_cflag[256];      /* the live CF_HIDDEN | CF_DISABLED */

/* 4.8.1's list-item override pool, shared by every list in the bundle and
 * skipped entirely while it is empty - which is every app that never calls
 * set(). The linear scan is why the empty test comes first: a list paints
 * one font_run a row and a 64-entry walk per row would be real time on the
 * target for a feature most bundles do not use. */
static unsigned char w_lsetc[W_NLSET];  /* comp_id */
static unsigned char w_lseti[W_NLSET];  /* item index */
static unsigned char w_lseth[W_NLSET];  /* the string handle, ditto */
static int           w_nlset;

static void w_pstate(void)              /* a new bundle: forget the old one's */
{
    os88_memset(w_lpos, 0, sizeof(w_lpos));
    os88_memset(w_lsel1, 0, sizeof(w_lsel1));
    os88_memset(w_ctext, 0, sizeof(w_ctext));
    os88_memset(w_cval, 0, sizeof(w_cval));
    os88_memset(w_cvold, 0, sizeof(w_cvold));
    os88_memset(w_cflag, 0, sizeof(w_cflag));
    w_nlset = 0;
}

/* w_lfind - the override for (comp, index), or -1. */
static int w_lfind(int id, int idx)
{
    int k;

    if (w_nlset == 0)
        return -1;
    for (k = 0; k < w_nlset; k++)
        if (w_lsetc[k] == id && w_lseti[k] == idx)
            return k;
    return -1;
}

/* ============================================================================
 * THE RECT TABLE
 * ==========================================================================*/

static void w_rects(void)
{
    int i, j, x, y;

    for (i = 0; i < w_nlay; i++) {
        j = i << 2;
        x = w_ox + (w_lay[i].cx << 3);      /* cell -> pixel, and the origin's
                                             * x was rounded UP to a multiple
                                             * of 8 at LAYOUT time (7.1.2), so
                                             * every column is 8-aligned and
                                             * every font_run takes the
                                             * single-store fast path */
        y = w_oy + (w_lay[i].cy << 3);
        w_rect[j] = x;
        w_rect[j + 1] = y;
        w_rect[j + 2] = x + (w_lay[i].cw << 3) - 1;
        w_rect[j + 3] = y + (w_lay[i].ch << 3) - 1;
    }
}

/* w_layout - geometry, walk, rect table.  0 = the window shows nothing.
 *
 * Every edge runs this before touching the table, which is cal_geom+cal_layout
 * verbatim (apps/calc/calc.asm:496, :551, :599, :760): the window may have
 * moved since the last paint, and a hit test against yesterday's pixels finds
 * the wrong control or none. The WALK is re-run only when the thing it depends
 * on changed - a paint that re-derives its layout every time is how a layout
 * and a hit test come to disagree by one resize. */
static int w_layout(void *win)
{
    if (!w_grid(win))
        return 0;
    w_ybot = w_oy + (w_ch << 3) - 1;
    if (w_state == W_ST_RUN) {
        if (w_lay_cw != w_cw || w_lay_ch != w_ch || w_lay_card != (int)w_entry)
            w_flow();
        w_rects();
    }
    return 1;
}

/* ============================================================================
 * READING A COMPONENT'S PROPERTIES (WEAVE-SPEC 2.6)
 * ==========================================================================*/

/* w_patom - the atom id a PK_ATOM property names, 0 = none.
 * wval.c has already refused a value whose high byte is not 0 and whose low
 * byte is not an atom this bundle can name, so the mask is the record's own
 * shape rather than a guard. */
static unsigned w_patom(unsigned props, unsigned name)
{
    unsigned v;

    v = w_pfind(props, name);
    return w_pfound ? (v & 0xFF) : 0;
}

/* w_pstr - a property's string, into w_str[].  Returns its length.
 * A well-known atom (1..63) has no string table in the runtime (2.7), so
 * w_atom_len answers 0 for one and the field comes out empty rather than
 * reading whatever is at offset 0 of the pool. */
static unsigned w_pstr(unsigned props, unsigned name)
{
    unsigned a, n;

    w_str[0] = 0;
    a = w_patom(props, name);
    n = w_atom_len(a);
    if (n > W_STRMAX - 1)
        n = W_STRMAX - 1;
    if (n)
        w_copy(w_seg, w_atom_off(a), w_str, n);
    w_str[n] = 0;
    return n;
}

/* w_cstr - the component's CURRENT text or label, into w_str[].
 *
 * One reader for two sources: the atom its record named, or the arena string
 * a `SETP` put in w_ctext (4.8.1). The copy costs a few microseconds against
 * a font_run's 756 - PERFORMANCE.md's own rule that a redraw is priced by its
 * primitive calls - and it buys the painter one path instead of two, which is
 * the same argument wdraw.inc makes about taking a descriptor. */
static unsigned w_cstr(int id, unsigned props, unsigned name)
{
    if (w_ctext[id])
        return (unsigned)wvm_str_read((int)w_ctext[id], w_str, W_STRMAX);
    return w_pstr(props, name);
}

static unsigned w_pint(unsigned props, unsigned name, unsigned dflt)
{
    unsigned v;

    v = w_pfind(props, name);
    return w_pfound ? v : dflt;
}

/* The items blob (2.6.1): a count byte, then that many atom-id bytes. */
static unsigned w_it_off, w_it_n;

static void w_items(unsigned props)
{
    unsigned off;

    w_it_off = 0;
    w_it_n = 0;
    off = w_pfind(props, WA_ITEMS);
    if (!w_pfound)
        return;
    w_it_off = w_soff[W_PROPS] + off;
    w_it_n = w_b(w_seg, w_it_off);
}

static unsigned w_item(unsigned k)
{
    return w_b(w_seg, w_it_off + 1 + k);
}

/* ============================================================================
 * THE LIST'S GEOMETRY - derived ONCE, read by the painter and the click
 *
 * Two readers of one arithmetic, for the rect table's reason one level down: a
 * click that computes its own row height finds a different row than the one
 * the painter drew, and the two disagree by a pixel nobody can see until the
 * wrong item selects.
 * ==========================================================================*/

static int w_lg_x1, w_lg_y1, w_lg_x2, w_lg_y2;   /* the component */
static int w_lg_rx2;                             /* the rows' right edge */
static int w_lg_bx1;                             /* the bar's left, 0 = none */
static int w_lg_cells;                           /* cells a row shows */
static int w_lg_vis;                             /* rows that are ON SCREEN */
static int w_lg_id;

static void w_lgeom(int i)
{
    int j, rows;

    j = i << 2;
    w_lg_x1 = w_rect[j];
    w_lg_y1 = w_rect[j + 1];
    w_lg_x2 = w_rect[j + 2];
    w_lg_y2 = w_rect[j + 3];
    w_lg_id = w_lay[i].id;
    w_lg_cells = w_lay[i].cw;
    if (w_lg_y2 > w_ybot)
        w_lg_y2 = w_ybot;               /* 7.4's clip, TAKEN HERE and not left
                                         * to the platform: the painter runs
                                         * under a clip region and W_ONCLICK
                                         * does not, so a bar whose rect ran
                                         * past the content would have its
                                         * thumb translated onto whatever is
                                         * under the window the first time
                                         * somebody paged it. Clamping the
                                         * GEOMETRY rather than the drawing
                                         * keeps os88ui's derived facts - the
                                         * track, the thumb's height, its
                                         * travel - agreeing with the picture */
    w_lg_rx2 = w_lg_x2;
    w_lg_bx1 = 0;
    if (w_lg_cells > W_SB_CELLS && (w_lg_y2 - w_lg_y1 + 1) >= W_SB_MINH) {
        w_lg_bx1 = w_lg_x2 - (W_SB_CELLS << 3) + 1;
        w_lg_rx2 = w_lg_bx1 - 1;
        w_lg_cells -= W_SB_CELLS;
    }
    rows = w_lay[i].ch;                 /* ...and 7.4's clip: rows below the
                                         * content box are not drawn, which is
                                         * the degradation rather than a
                                         * reflow */
    w_lg_vis = (w_ybot - w_lg_y1 + 1) >> 3;
    if (w_lg_vis > rows)
        w_lg_vis = rows;
    if (w_lg_vis < 0)
        w_lg_vis = 0;
}

/* w_lclamp - the scroll position this list can actually hold. */
static unsigned w_lclamp(unsigned pos)
{
    unsigned max;

    max = 0;
    if (w_it_n > (unsigned)w_lg_vis)
        max = w_it_n - w_lg_vis;
    if (pos > max)
        pos = max;
    return pos;
}

/* w_lblk - fill os88ui.inc's seven-word scroll block for this list.
 *
 * THE ONE WRITER, for the rect table's reason: the painter, the hit test and
 * the thumb move all read the block, and everything else about the bar - the
 * arrow cells, the track, the thumb's height and its travel - is derived
 * INSIDE os88ui.inc from these seven words. Two places filling them is two
 * places to get `fit` wrong in. */
static void w_lblk(unsigned pos)
{
    w_sbblk[0] = w_lg_bx1;
    w_sbblk[1] = w_lg_y1;
    w_sbblk[2] = w_lg_x2;
    w_sbblk[3] = w_lg_y2;
    w_sbblk[4] = w_it_n;
    w_sbblk[5] = w_lg_vis;
    w_sbblk[6] = pos;
}

/* w_lrow - draw one visible row, index k of the visible band.
 *
 * ONE opaque os88_font_run per row (WEAVE-SPEC 6.8), padded to the row's cells
 * so the run IS the erase - a row that scrolls into view over the row that
 * left has nothing to blank first (6.2's "padding is the erase").
 *
 * `erase` is the difference between the two callers and it is worth a
 * parameter: a card paint draws onto ground the kernel has just whitened, so a
 * row PAST the last item owes nothing and a call that paints 8 blank cells is
 * ~756 us spent to change no pixel. A scroll's exposed row owes the erase,
 * because what is under it is the row that scrolled away. */
static void w_lrow(int k, unsigned pos, int sel, int erase)
{
    int y, ov;
    unsigned idx, a;

    y = w_lg_y1 + (k << 3);
    idx = pos + k;
    if (idx < w_it_n) {
        ov = w_lfind(w_lg_id, (int)idx);        /* 4.8.1's override, if any -
                                                 * and the scan returns at
                                                 * once while the pool is
                                                 * empty, which is every app
                                                 * that never calls set() */
        if (ov >= 0) {
            wvm_str_read((int)w_lseth[ov], w_str, W_STRMAX);
            w_draw_text(w_lg_x1, y, w_str, w_lg_cells,
                        OS88_BLACK, OS88_WHITE, WD_PAD);
        } else {
            a = w_item(idx);
            w_draw_run(w_lg_x1, y, w_seg, w_atom_off(a), w_atom_len(a),
                       w_lg_cells, OS88_BLACK, OS88_WHITE, WD_PAD);
        }
    } else if (erase) {
        w_draw_text(w_lg_x1, y, "", w_lg_cells, OS88_BLACK, OS88_WHITE,
                    WD_PAD);
    } else
        return;                         /* no item, clean ground: nothing at
                                         * all, and no selection can be on a
                                         * row that has no item */
    if (sel >= 0 && idx == (unsigned)sel)
        wd_xor(w_lg_x1, y, w_lg_x1 + (w_lg_cells << 3) - 1, y + 7);
}

/* ============================================================================
 * THE COMPONENTS (WEAVE-SPEC 6)
 * ==========================================================================*/

static void w_paint_list(int i)
{
    int k, sel;
    unsigned pos;

    w_items(w_lay[i].props);
    w_lgeom(i);
    pos = w_lclamp(w_lpos[w_lg_id]);
    w_lpos[w_lg_id] = pos;
    sel = (int)w_lsel1[w_lg_id] - 1;

    for (k = 0; k < w_lg_vis; k++)
        w_lrow(k, pos, sel, 0);

    if (w_lg_bx1) {
        w_lblk(pos);
        wd_sbar(w_sbblk);               /* ~16 calls, and the only place the
                                         * whole bar is ever drawn: a scroll
                                         * translates the thumb instead */
    }
}

/* w_paint_comp - one component, at the rect the walk produced.
 *
 * THE PEN IS SET ONCE PER COMPONENT and it is SPEC.md 47's, not a colour:
 * os88_gfx_pen() sets CDGRAY *and* the dither flag, and the flag is the entire
 * difference on the two 1bpp adapters this OS is for - CDGRAY alone is solid
 * black there, pixel-identical to a live control, which is a defect this tree
 * has fixed five separate times. Every shape in wdraw.inc inherits it; only
 * os88ui_btn and os88ui_glyph take the flag as an argument as well, because
 * they put the pen back themselves.
 *
 * Rule 2 is why it is per COMPONENT rather than per call: a check box's ring
 * and its label are drawn by two different routines and a black ring beside
 * faint writing reads as a mislabelled live control. */
/* w_padnow - WD_PAD, on an INCREMENTAL repaint only.
 *
 * A card paint draws onto ground the kernel (or w_repaint2) has just
 * whitened, so a run padded to the component's width would spend ~900 us a
 * cell erasing what is already white. A repaint of ONE component after a
 * `.text` write does not: what is under it is the last string, and
 * os88_font_run() letters exactly the cells it is given - so `status.text =
 * "Hi."` over `Recalculated 12 cells.` would leave `ated 12 cells.` behind.
 * The padding IS the erase (WEAVE-SPEC 6.2), and this is the flag that says
 * which of the two cases we are in. */
static int w_padnow;
static int w_bdown = -1;                /* the button drawn PRESSED, -1 none */

static void w_paint_comp(int i)
{
    int x1, y1, x2, y2, dis, ink, paper, flags, cells, j, rows;
    unsigned n, props, a;

    if (w_lay[i].cflags & CF_HIDDEN)
        return;                         /* 7.2: a hidden component still took
                                         * part in the walk and still occupies
                                         * its rect - hiding does not reflow */
    j = i << 2;
    y1 = w_rect[j + 1];
    if (y1 > w_ybot)
        return;                         /* 7.4: wholly below the content box.
                                         * The clip would drop it anyway; not
                                         * asking for it saves the ~756 us a
                                         * refused primitive call still costs */
    x1 = w_rect[j];
    x2 = w_rect[j + 2];
    y2 = w_rect[j + 3];
    props = w_lay[i].props;
    cells = w_lay[i].cw;
    dis = (w_lay[i].cflags & CF_DISABLED) ? 1 : 0;
    os88_gfx_pen(dis);

    ink = OS88_BLACK;
    paper = OS88_WHITE;
    flags = 0;
    if (w_lay[i].style & WS_BOLD)
        flags |= WD_BOLD;
    if (w_lay[i].style & WS_INVERT) {
        /* 6.2: INVERT swaps ink and paper for the run, and the run is PADDED
         * to the component so the bar covers it - no fill-then-letter pair. */
        ink = OS88_WHITE;
        paper = OS88_BLACK;
        flags |= WD_PAD;
    }

    switch (w_lay[i].ctype) {
    case WC_LABEL:
        if (w_ctext[w_lay[i].id]) {
            n = w_cstr(w_lay[i].id, props, WA_TEXT);
            w_draw_text(x1, y1, w_str, cells, ink, paper, flags | w_padnow);
        } else {
            a = w_patom(props, WA_TEXT);
            w_draw_run(x1, y1, w_seg, w_atom_off(a), w_atom_len(a), cells,
                       ink, paper, flags | w_padnow);
        }
        break;

    case WC_TEXT:
        a = w_patom(props, WA_TEXT);
        n = w_atom_len(a);
        rows = w_lay[i].ch;
        if (((w_ybot - y1 + 1) >> 3) < rows)
            rows = (w_ybot - y1 + 1) >> 3;
        if (n && rows > 0) {
            /* 7.3's wrap, DRAWING this time - the same body the walk counted
             * the rows with, so the picture and the height cannot disagree. */
            w_wr_x = x1;
            w_wr_y = y1;
            w_wr_cells = cells;
            w_wr_ink = ink;
            w_wr_paper = paper;
            w_wr_flags = flags;
            w_wr_max = rows;
            w_wr_draw = 1;
            w_wrap(w_atom_off(a), n, cells);
            w_wr_draw = 0;
        }
        break;

    case WC_RULE:
        wd_rule(x1, x2, y1 + 3);        /* centred in its 8px row */
        break;

    case WC_BOX:
        if (y2 > w_ybot)
            y2 = w_ybot;
        wd_box(x1, y1, x2, y2);
        break;

    case WC_SPACER:
        break;                          /* 6.3: draws nothing, and that is the
                                         * whole of it - it occupies cells */

    case WC_METER:
        n = w_pint(props, WA_MAX, 100);
        if (w_padnow)                   /* a repaint over the LAST value: 6.4's
                                         * delta, one call or none */
            wd_mdelta(x1, y1, x2, y2, w_cvold[w_lay[i].id],
                      w_cval[w_lay[i].id], (int)n);
        else                            /* clean ground: the frame and the fill,
                                         * which is 14's two-call row */
            wd_meter(x1, y1, x2, y2, w_cval[w_lay[i].id], (int)n);
        w_cvold[w_lay[i].id] = w_cval[w_lay[i].id];
        break;

    case WC_BUTTON:
        w_cstr(w_lay[i].id, props, WA_LABEL);
        wd_button(w_rect + j, w_str, dis, w_bdown == w_lay[i].id, w_padnow);
                                        /* down = 0: wave 3 owns the pressed
                                         * state, and it is DRAWN from a
                                         * variable this painter reads, never
                                         * XOR-ed (SPEC.md 13.8).
                                         * fill = 0: the kernel whitens a
                                         * window's content before W_PAINT and
                                         * w_repaint2 whitens it on every other
                                         * path, so the ground is already clean.
                                         * A per-control repaint must pass 1 */
        break;

    case WC_CHECK:
    case WC_RADIO:
        wd_glyph(x1 + 2, y1 + 2, w_lay[i].ctype == WC_RADIO,
                 w_cval[w_lay[i].id] != 0, dis);
        os88_gfx_pen(dis);              /* os88ui_glyph PUT THE PEN BACK LIVE,
                                         * so the label needs it again - 47
                                         * rule 2: grey the ring AND the words
                                         * beside it, or the control reads as
                                         * mislabelled rather than disabled */
        n = cells > W_SB_CELLS ? cells - 2 : 1;
        w_cstr(w_lay[i].id, props, WA_LABEL);
        w_draw_text(x1 + 16, y1 + 4, w_str, n, ink, paper,
                    flags | w_padnow);
        break;

    case WC_INPUT:
        if (y2 > w_ybot)
            y2 = w_ybot;
        w_infield(i, x1, y1, x2, y2, dis);
        break;

    case WC_LIST:
        w_paint_list(i);
        break;

    case WC_GRID:
    case WC_CANVAS:
        if (y2 > w_ybot)
            y2 = w_ybot;
        wd_box(x1, y1, x2, y2);         /* WAVE 4 / WAVE 5's SEAM: the band
                                         * composer (6.9) and the sprite
                                         * compositor (6.10) fill this frame.
                                         * Drawing the frame now is what puts
                                         * the walk's arithmetic on the glass
                                         * where it can be looked at */
        break;

    default:
        break;
    }
}

/* w_paint_card - the entry card, component by component, in UISTREAM order.
 *
 * THE CLIP IS ARMED HERE and it is WEAVE-SPEC 7.4's, said in the platform's
 * own words: "components clip against the content box through the platform
 * clip (WM_CLIP) - the walk never produces overlaps, and clipping is the
 * degradation, not reflow-below-minimum". A card taller than the window is the
 * ordinary case on CGA's 17 rows, and without this every primitive below draws
 * in ABSOLUTE screen coordinates over whatever is beneath the window - the
 * kernel arms nothing around W_PAINT (kernel/wm.inc: the chrome's region dies
 * before the title bar, and nothing re-arms one).
 *
 * IT MUST BE CLEARED. The region lives until the next gfx_unlock, and the
 * kernel draws the grow box AFTER W_PAINT returns - inside our lock hold and
 * outside our content. */
static void w_paint_card(void *win)
{
    int i, clipped;

    clipped = os88_wm_clip_set(win) == 0;
    for (i = 0; i < w_nlay; i++)
        w_paint_comp(i);
    os88_gfx_pen(0);                    /* 47 rule 7 again, outward: a pen left
                                         * disabled draws the next string in
                                         * this lock hold as a checkerboard,
                                         * and the next string is the kernel's */
    if (clipped)
        os88_wm_clip_clear();
}

/* w_find_lay - the layout index of comp_id `id`, or -1.
 *
 * Linear over at most 250 records and called from GETP/SETP - which is a
 * handful of times a handler, not per op - so a 256-entry reverse table would
 * be 512 bytes of bss to save microseconds nobody is waiting for. */
static int w_find_lay(int id)
{
    int i;

    for (i = 0; i < w_nlay; i++)
        if (w_lay[i].id == id)
            return i;
    return -1;
}

/* w_wipe_one - give one component's rect back to the paper.  What `hidden`
 * costs: ONE gfx_fill, ~756 us, and no reflow (7.2). */
static void w_wipe_one(int i)
{
    int j, y2;

    j = i << 2;
    if (w_rect[j + 1] > w_ybot)
        return;
    y2 = w_rect[j + 3];
    if (y2 > w_ybot)
        y2 = w_ybot;
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(w_rect[j], w_rect[j + 1], w_rect[j + 2], y2);
}

/* w_repaint_one - one component, now, under its own lock hold and clip.
 *
 * The arm/fire path's painter: a press repaints the pressed control and
 * nothing else (WEAVE-SPEC 6.5, ~2 calls + the label). It is called from a
 * CALLBACK, where the gfx lock is already held - so it takes none. The
 * deferred path (wevent.c's w_flush) is the one that has to. */
static void w_repaint_one(int i)
{
    int clipped;

    if (i < 0 || i >= w_nlay)
        return;
    clipped = os88_wm_clip_set(w_win) == 0;
    w_padnow = WD_PAD;                  /* over the LAST state, so the run IS
                                         * the erase (6.2) */
    if (w_cflag[w_lay[i].id] & CF_HIDDEN)
        w_wipe_one(i);
    else
        w_paint_comp(i);
    w_padnow = 0;
    os88_gfx_pen(0);
    if (clipped)
        os88_wm_clip_clear();
}

/* ============================================================================
 * THE HIT TEST AND THE TWO NATIVE INTERACTIONS
 * ==========================================================================*/

/* w_lscroll - move a list's view.  ONE GFX_SCROLL and the rows it exposed.
 *
 * PERFORMANCE.md Part 5's contract row, and WEAVE-SPEC 6.8's: a full repaint
 * of a CGA card is 1.24 s. An end-stop scroll draws NOTHING AT ALL - the
 * clamp answers the position it is already at and this returns before any
 * call, which is the Disk window's 266 ms measured down to 0 frames.
 *
 * The blit's span is x1 .. rx2, and rx2+1 is x1 + cells*8: both multiples of
 * 8, because the origin was rounded up at layout time. The vertical span stops
 * at the last WHOLE 8px row on screen, never at the content bottom - a height
 * with a remainder otherwise leaves a one-pixel band of the previous row's
 * descenders behind for good, clean on VGA and broken on Hercules
 * (apps/notepad/notepad.asm:952). Here every row is exactly 8px and w_lg_vis
 * is a count of whole ones, so that band cannot exist by construction. */
static void w_lscroll(int newpos)
{
    int old, dy, k, sel;
    unsigned pos;

    old = (int)w_lpos[w_lg_id];
    if (newpos < 0)
        newpos = 0;
    pos = w_lclamp((unsigned)newpos);
    if ((int)pos == old || w_lg_vis < 1)
        return;
    w_lpos[w_lg_id] = pos;
    sel = (int)w_lsel1[w_lg_id] - 1;
    dy = ((int)pos - old) << 3;

    if (dy < (w_lg_vis << 3) && -dy < (w_lg_vis << 3) &&
        os88_gfx_scroll(w_lg_x1, w_lg_y1, w_lg_rx2,
                        w_lg_y1 + (w_lg_vis << 3) - 1, dy) == 0) {
        if (dy > 0)
            for (k = w_lg_vis - (dy >> 3); k < w_lg_vis; k++)
                w_lrow(k, pos, sel, 1);
        else
            for (k = 0; k < (-dy) >> 3; k++)
                w_lrow(k, pos, sel, 1);
    } else {
        /* Refused, or a step of a whole page: the blit would move nothing
         * worth moving, so every visible row is re-lettered instead. */
        for (k = 0; k < w_lg_vis; k++)
            w_lrow(k, pos, sel, 1);
    }

    if (w_lg_bx1) {
        /* The block already holds the rest of the bar's facts. Three drawing
         * calls, and NONE AT ALL when the step is too small to move the thumb
         * - os88ui_sbmove compares the two tops and returns. */
        w_sbblk[6] = pos;
        wd_sbmove(w_sbblk, old);
    }
}

/* w_lclick - a press inside a list.  The bar scrolls, a row selects.
 *
 * Both are NATIVE (WEAVE-SPEC 1.2): they change what is on the glass and, in
 * wave 3, enqueue an `onselect` record - they never run a bytecode op under
 * the gfx lock. Wave 2 does the drawing half and leaves the ring for wave 3.
 *
 * The selection is an XOR bar (6.8): one call to take it off the old row and
 * one to put it on the new, ~1.6 ms, and the painter re-applies it from
 * `sel` on every paint - which is what makes an XOR legal here where SPEC.md
 * 13.8 forbids one for a pressed state (calc's cal_restore rule: a row is one
 * opaque run over exactly its own rect). */
static void w_lclick(int i, int x, int y)
{
    int part, sel, k, idx, vis;
    unsigned pos;

    w_items(w_lay[i].props);
    w_lgeom(i);
    pos = w_lclamp(w_lpos[w_lg_id]);
    w_lpos[w_lg_id] = pos;
    vis = w_lg_vis;
    if (vis < 1)
        return;

    if (w_lg_bx1 && x >= w_lg_bx1) {
        w_lblk(pos);
        part = wd_sbhit(w_sbblk, x, y);
        if (part == WD_SB_UP)
            w_lscroll((int)pos - 1);
        else if (part == WD_SB_DOWN)
            w_lscroll((int)pos + 1);
        else if (part == WD_SB_PGUP)
            w_lscroll((int)pos - vis);
        else if (part == WD_SB_PGDN)
            w_lscroll((int)pos + vis);
        return;                         /* the thumb is wave 3's: dragging it
                                         * is a press, a stream of movements
                                         * and a release (os88ui_sbgrab, behind
                                         * OS88UI_SBDRAG) */
    }

    k = (y - w_lg_y1) >> 3;
    if (k < 0 || k >= vis)
        return;
    idx = (int)pos + k;
    if (idx >= (int)w_it_n)
        return;                         /* past the last item: no selection to
                                         * move, and nothing is drawn */
    sel = (int)w_lsel1[w_lg_id] - 1;
    if (sel == idx)
        return;                         /* os88ui_adn's rule: a setter that
                                         * does not compare re-letters a row
                                         * for nothing */
    if (sel >= (int)pos && sel < (int)pos + vis)
        wd_xor(w_lg_x1, w_lg_y1 + ((sel - (int)pos) << 3),
               w_lg_x1 + (w_lg_cells << 3) - 1,
               w_lg_y1 + ((sel - (int)pos) << 3) + 7);
    w_lsel1[w_lg_id] = idx + 1;
    wd_xor(w_lg_x1, w_lg_y1 + (k << 3),
           w_lg_x1 + (w_lg_cells << 3) - 1, w_lg_y1 + (k << 3) + 7);
    w_enq(w_lg_id, WA_ONSELECT, idx, 0);    /* 3.4: onselect carries the new
                                             * index. The PICTURE moved above,
                                             * natively; the script hears
                                             * about it a slice later (1.2) */
}

/* w_onhit - a press landed on component `i`.  WAVE 3's SEAM.
 *
 * Wave 3 arms here (os88ui_arm with the index + 1, which is exactly what
 * wd_hit answers), draws the pressed control on the way down, fires on a
 * release inside the same rect, and enqueues an 8-byte record into the VM ring
 * (WEAVE-SPEC 4.9) - it never runs bytecode under the gfx lock. Wave 2 does
 * the two interactions that need no ring at all and nothing else, rather than
 * doing something plausible. */
static void w_onhit(int i, int x, int y)
{
    w_press(i, x, y);                   /* wact.c: the press half of SPEC.md
                                         * 13.7's gesture. Wave 2's seam,
                                         * spent. */
}
