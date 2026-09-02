/* ============================================================================
 * os8088 - apps/weave/wgrid.c
 *
 * THE <grid> COMPONENT (WEAVE-SPEC 6.9, 5.5, 5.6). #included by
 * apps/weave/weave.c after wact.c - whose field pool the formula bar comes
 * out of - and before wevent.c, which drives the sliced recalculation.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS HERE AND WHAT IS NEXT DOOR
 * ---------------------------------------------------------------------------
 * This file owns the CELL STORE - WEAVE-SPEC 5.6's dense array and its
 * bump-allocated pool, in a claim of its own - and everything that decides:
 * which band a click landed in, what a cell displays, what Enter commits, how
 * far a selection scrolls, which rows a recalculation dirtied. Two assembly
 * cores do the work C on this machine cannot:
 *
 *   apps/weave/wfx.inc    section 5's RPN machine and its 16.16 arithmetic.
 *                         There is no `long` in this toolchain (SPEC.md 73.7)
 *   apps/weave/wband.inc  the band composer. A 79-cell row lettered by
 *                         OSAPI_FONT_RUN is ~60 ms on the target and composed
 *                         + blitted is 14.5 ms (WEAVE-SPEC 14)
 *
 * ...and apps/weave/wfxc.c is the resident formula compiler (6.9.2), a file
 * of its own because it is the ONE text parser in the runtime and 9.4's
 * carve-out should be findable rather than buried in a commit path.
 *
 * ---------------------------------------------------------------------------
 * NOTHING HERE DRAWS OUTSIDE A LOCK IT WAS GIVEN
 * ---------------------------------------------------------------------------
 * The recalculation runs in ONWAKE slices, which do NOT hold the gfx lock
 * (WEAVE-SPEC 4.10), so it marks rows dirty and draws nothing; wevent.c's
 * flush repaints them under one lock hold at the end of the slice, exactly as
 * a SETP's dirty component is repainted. The click and key paths run inside a
 * callback where the lock is already held, and draw at once.
 * ==========================================================================*/

/* --- WEAVE-SPEC 6.9.1's pinned geometry --------------------------------- */
#define WG_GUT     4            /* the row-number gutter, in cells */
#define WG_COLW    8            /* every data column, in cells - FIXED, and
                                 * 6.9.1 says why: a fitted width turns a
                                 * one-cell edit into a re-compose of every
                                 * band, and two implementations would have to
                                 * fit identically or the diff is noise */
#define WG_BAR     2            /* the formula bar, in 8-px rows */
#define WG_HDR     1            /* ...and the column-header band */
#define WG_CHROME  (WG_BAR + WG_HDR)
#define WG_BARCOLS 60           /* the bar's buffer: os88line's own cap (6.7),
                                 * so a formula is 60 characters and the
                                 * display window scrolls within the band */

/* 5.6's cell record and its flags, mirrored from wfx.inc - which is not the
 * contract either (docs/WEAVE-SPEC.md is), and a disagreement between the two
 * shows as a cell that reads one kind and writes another. */
#define WG_CHDR     16          /* the claim's header, before the array */
#define WG_CELL     4
#define WGK_EMPTY   0
#define WGK_INT     1
#define WGK_NUM     2
#define WGK_ATOM    3
#define WGK_BFORM   4           /* a BUNDLE formula: an FXCODE index */
#define WGK_STR     5
#define WGK_RFORM   6           /* ...and one 6.9.2 compiled into the pool */
#define WGF_CIRC    1
#define WGF_DIRTY   2
#define WGF_ERR     4
#define WGF_ERRWAS  8
#define WGF_CIRCWAS 16

/* A formula cell's pool slot (5.6): the index or length word, the cached
 * value, the PRE-WALK value, and - for a runtime formula - the RPN. */
#define WGS_LEN     0
#define WGS_CACHED  2
#define WGS_PREV    6
#define WGS_RPN     10

/* 5.6's header words, by offset. */
#define WG_H_COLS   0
#define WG_H_ROWS   2
#define WG_H_NEXT   4           /* the pool's bump pointer */
#define WG_H_END    6           /* ...and the claim's last byte + 1 */

/* One display string, and one band's worth of cells. A cell shows at most
 * WG_COLW-1 characters; WG_STRIDE is wband.inc's, sized from the widest
 * content grid the family has (Hercules' 89 cells, WEAVE-SPEC 7.1.1). */
#define WG_DMAX     16
#define WG_BANDMAX  WG_STRIDE

static unsigned w_gseg;                 /* the grid claim, 0 = none held */
static unsigned w_gkb;
static int      w_gcols, w_grows;       /* the sheet's own dimensions */
static int      w_gtop, w_gleft;        /* 6.9.1's scroll origin, 0-based */
static int      w_gbar = -1;            /* the formula bar's field block */
static int      w_gredraw;              /* the flush owes the dirty rows */

/* THE BAND BUFFER IS THE TAIL OF THE HEADER PROBE, and that is a union
 * rather than a coincidence (WEAVE-PLAN 2.9).
 *
 * w_probe[] is one CLUSTER, 1,024 bytes, because os88_file_read_at's capacity
 * has to be a whole number of them (10.1's refuse-before-read) - and the only
 * thing anything ever reads out of it is the 32-byte header at offset 0
 * (wval.c's ovl_val_header_x, and every reference there is W_H_* < 32). The
 * remaining 992 bytes have never been looked at by anything.
 *
 * So the 720-byte band lives at the END of that cluster, 304 bytes clear of
 * the header, and the two cannot collide even if a later wave gives the
 * header a second reader: they overlap nowhere. Wave 5 needed the bytes and
 * this was 720 of them, at the cost of one #define and this paragraph. */
#define w_gband (w_probe + W_PROBE - WG_BANDMAX * 8)
static char          w_gtext[WG_BANDMAX + 1];
static char          w_gdisp[WG_DMAX];  /* one cell's display string */
static char          w_gold[WG_DMAX];   /* ...and what it displayed before */
static int           w_gv[3];           /* wfx_eval's answer: type, lo, hi */
static int           w_gmag[2];         /* ...and one magnitude */

/* 5.5.1's walk. `pass` is 0 idle, 1 and 2 the two passes; `cur` is the next
 * cell index in row-major order. */
static int w_gpass, w_gcur, w_gchanged, w_gpend;
static unsigned char w_gdirty[32];      /* 256 grid rows, one bit each */

/* ============================================================================
 * SMALL HELPERS - this toolchain's string library is strlen/strcpy/utoa and
 * nothing else (apps/cc/os88.h), and both of these are three lines.
 * ==========================================================================*/

static void w_gcat(char *d, const char *s, unsigned cap)
{
    unsigned n;

    n = os88_strlen(d);
    if (n < cap)
        os88_strcpy(d + n, s, cap - n);
}

static int w_gsame(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* ============================================================================
 * THE STORE (WEAVE-SPEC 5.6)
 * ==========================================================================*/

static unsigned w_gcellat(int r, int c)
{
    return WG_CHDR + (unsigned)((r * w_gcols + c) * WG_CELL);
}

static unsigned w_gkind(int r, int c)
{
    return w_b(w_gseg, w_gcellat(r, c));
}

static unsigned w_gflags(int r, int c)
{
    return w_b(w_gseg, w_gcellat(r, c) + 1);
}

static void w_gsetflags(int r, int c, unsigned f)
{
    w_pb(w_gseg, w_gcellat(r, c) + 1, f);
}

static unsigned w_gpay(int r, int c)
{
    return w_w(w_gseg, w_gcellat(r, c) + 2);
}

static void w_gput(int r, int c, unsigned kind, unsigned flags, unsigned pay)
{
    unsigned a;

    a = w_gcellat(r, c);
    w_pb(w_gseg, a, kind);
    w_pb(w_gseg, a + 1, flags);
    w_pw(w_gseg, a + 2, pay);
}

static int w_gisform(unsigned k)
{
    return k == WGK_BFORM || k == WGK_RFORM;
}

/* w_galloc - `n` bytes of the pool, or 0 when it is spent.
 *
 * BUMP-ALLOCATED AND NEVER FREED (5.6's own rule), so re-typing a formula
 * into one cell leaks its last slot. 5.6 states that rather than leaving it
 * to be discovered: a session that edits one formula five hundred times meets
 * `grid pool full.` and one that edits a handful does not notice, and the
 * alternative is a free list in a 2KB pool - more machinery than the whole
 * component. */
static unsigned w_galloc(unsigned n)
{
    unsigned p;

    if (w_gseg == 0)
        return 0;
    n = (n + 1) & 0xFFFE;               /* slots stay even */
    p = w_w(w_gseg, WG_H_NEXT);
    if (p + n > w_w(w_gseg, WG_H_END) || p + n < p)
        return 0;
    w_pw(w_gseg, WG_H_NEXT, p + n);
    return p;
}

/* w_growdirty - 5.5.1's damage, one bit a grid row. */
static void w_growdirty(int r)
{
    if (r >= 0 && r < 256)
        w_gdirty[r >> 3] |= (unsigned char)(1 << (r & 7));
}

static int w_gisdirty(int r)
{
    if (r < 0 || r > 255)
        return 0;
    return (w_gdirty[r >> 3] & (1 << (r & 7))) != 0;
}

/* ============================================================================
 * READING A CELL (WEAVE-SPEC 5.2.1's display)
 * ==========================================================================*/

/* w_gshowv - 5.2.1, exactly, from a VALUE rather than from a cell, into
 * w_gdisp[].
 *
 * It takes the value and its two display bits rather than a (row, col)
 * because 5.5.1's `changed` count compares what a cell shows NOW against what
 * it showed before the walk - and the before is a dword in a pool slot with
 * no cell of its own. The pinned conversion: the sign first, the integer
 * part, then two TRUNCATED cents with one trailing zero removed. The
 * truncation is 5.2.1's own - rounding here would make 0.999 display as 1
 * while `=A1=1` stayed false, which is the class of disagreement a
 * spreadsheet must not have. */
static void w_gshowv(int lo, int hi, int iserr, int iscirc)
{
    unsigned cents;
    int neg;

    if (iscirc) {
        os88_strcpy(w_gdisp, "#CIRC", WG_DMAX);
        return;
    }
    if (iserr) {
        os88_strcpy(w_gdisp, "#DIV0", WG_DMAX);
        return;
    }
    neg = wfx_mag(lo, hi, w_gmag);
    w_gdisp[0] = 0;
    if (neg)
        os88_strcpy(w_gdisp, "-", WG_DMAX);
    os88_utoa((unsigned)w_gmag[1], w_num);      /* the integer part */
    w_gcat(w_gdisp, w_num, WG_DMAX);
    cents = wfx_cents((unsigned)w_gmag[0]);
    if (cents == 0)
        return;
    w_num[0] = '.';
    w_num[1] = (char)('0' + (cents / 10) % 10);
    w_num[2] = (char)('0' + cents % 10);
    w_num[3] = 0;
    if (w_num[2] == '0')
        w_num[2] = 0;                   /* 5.2.1: one trailing zero removed */
    w_gcat(w_gdisp, w_num, WG_DMAX);
}

/* w_gshow - ...and from a cell, which is every kind of 5.6. */
static void w_gshow(int r, int c)
{
    unsigned k, f, a;
    int n;

    w_gdisp[0] = 0;
    if (w_gseg == 0)
        return;
    k = w_gkind(r, c);
    f = w_gflags(r, c);
    if (k == WGK_EMPTY)
        return;
    if (k == WGK_ATOM) {
        a = w_gpay(r, c);
        n = (int)w_atom_len(a);
        if (n > WG_DMAX - 1)
            n = WG_DMAX - 1;
        if (n > 0)
            w_copy(w_seg, w_atom_off(a), w_gdisp, (unsigned)n);
        w_gdisp[n] = 0;
        return;
    }
    if (k == WGK_STR) {
        a = w_gpay(r, c);
        n = (int)w_b(w_gseg, a);
        if (n > WG_DMAX - 1)
            n = WG_DMAX - 1;
        if (n > 0)
            w_copy(w_gseg, a + 1, w_gdisp, (unsigned)n);
        w_gdisp[n] = 0;
        return;
    }
    n = wfx_cell(r, c, w_gmag);          /* 0 empty/label, 1 number, 2 error */
    w_gshowv(w_gmag[0], w_gmag[1], n == 2,
             w_gisform(k) && (f & WGF_CIRC) != 0);
}

/* w_gleft_just - 6.9.1 justifies a LABEL left and everything else - a number,
 * an empty cell, an error - right. */
static int w_gleftjust(int r, int c)
{
    unsigned k;

    k = w_gkind(r, c);
    return k == WGK_ATOM || k == WGK_STR;
}

/* ============================================================================
 * THE BANDS (WEAVE-SPEC 6.9.1)
 * ==========================================================================*/

static int w_gw, w_gh;                  /* the component's rect, in cells */
static int w_gvc, w_gvr;                /* ...and 6.9.1's VC x VR */

/* w_ggeom - the grid's geometry out of the walk's rect.  0 = there is no grid
 * laid out on this card, which every caller below tests first; otherwise the
 * layout index PLUS ONE, so that index 0 is not "no". */
static int w_ggeom(void)
{
    int i;

    if (w_gid == 0 || w_gseg == 0)
        return 0;
    i = w_find_lay(w_gid);
    if (i < 0)
        return 0;
    w_gw = w_lay[i].cw;
    w_gh = w_lay[i].ch;
    if (w_gw > WG_BANDMAX)
        w_gw = WG_BANDMAX;
    w_gvc = (w_gw - WG_GUT) / WG_COLW;
    if (w_gvc < 1)
        w_gvc = 1;
    if (w_gvc > w_gcols)
        w_gvc = w_gcols;
    w_gvr = w_gh - WG_CHROME;
    if (w_gvr < 0)
        w_gvr = 0;
    if (w_gvr > w_grows)
        w_gvr = w_grows;
    return i + 1;
}

/* w_gbandtext - 6.9.1's band, as exactly w_gw characters in w_gtext[].
 * `band` is -1 for the column header, else the band index 0..VR-1. */
static void w_gbandtext(int band)
{
    int k, c, r, n, at, j;

    for (k = 0; k < w_gw; k++)
        w_gtext[k] = ' ';
    w_gtext[w_gw] = 0;
    if (band < 0) {
        for (k = 0; k < w_gvc; k++) {
            c = w_gleft + k;
            at = WG_GUT + k * WG_COLW + 3;
            if (c < w_gcols && at < w_gw)
                w_gtext[at] = (char)('A' + c);
        }
        return;
    }
    r = w_gtop + band;
    if (r >= w_grows)
        return;
    os88_utoa((unsigned)(r + 1), w_num);        /* right-justified in 3 */
    n = (int)os88_strlen(w_num);
    if (n > 3)
        n = 3;
    for (j = 0; j < n; j++)
        w_gtext[3 - n + j] = w_num[j];
    for (k = 0; k < w_gvc; k++) {
        c = w_gleft + k;
        if (c >= w_gcols)
            break;
        w_gshow(r, c);
        n = (int)os88_strlen(w_gdisp);
        if (n > WG_COLW - 1)
            n = WG_COLW - 1;
        at = WG_GUT + k * WG_COLW;
        if (!w_gleftjust(r, c))
            at += (WG_COLW - 1) - n;
        for (j = 0; j < n && at + j < w_gw; j++)
            w_gtext[at + j] = w_gdisp[j];
    }
}

/* w_gonscreen - is (r1, c1), 1-based, inside the visible window? */
static int w_gonscreen(int r1, int c1)
{
    if ((r1 - 1) < w_gtop || (r1 - 1) >= w_gtop + w_gvr)
        return 0;
    if ((c1 - 1) < w_gleft || (c1 - 1) >= w_gleft + w_gvc)
        return 0;
    return 1;
}

/* w_gemit - compose one band and put it down with ONE gfx_blit1.
 *
 * The selection's cell is inverted WITHIN its band, which is what makes the
 * incremental XOR path and a full re-compose land on the same pixels -
 * `weavegrid`'s tpdraw identity. */
static void w_gemit(int i, int band)
{
    int x, y, inv0, inv1;

    w_rectof(i);
    x = w_rect[0];
    y = w_rect[1] + ((band < 0 ? WG_BAR : WG_BAR + WG_HDR + band) << 3);
    if (y + 7 > w_ybot)
        return;                         /* 7.4's clip, taken here: a refused
                                         * primitive still costs ~756 us */
    w_gbandtext(band);
    inv0 = 0;
    inv1 = 0;
    if (band < 0)
        inv1 = w_gw;                    /* the header band, wholly inverted */
    else if (w_gonscreen(w_gsel_r, w_gsel_c)
             && (w_gsel_r - 1) - w_gtop == band) {
        inv0 = WG_GUT + ((w_gsel_c - 1) - w_gleft) * WG_COLW;
        inv1 = inv0 + WG_COLW;
        if (inv1 > w_gw)
            inv1 = w_gw;
    }
    wg_band(w_gband, w_gtext, w_gw, inv0, inv1);
    if (os88_gfx_blit1(w_gband, WG_STRIDE, x, y, w_gw << 3, 8) != 0)
        w_draw_text(x, y, w_gtext, w_gw, OS88_BLACK, OS88_WHITE, WD_PAD);
                                        /* GFX_BLIT1 REFUSED - kern_small
                                         * carries the slot and not the body,
                                         * and os88.h asks every caller of it
                                         * to have a second path. ~60 ms a row
                                         * against 14.5, which is the price of
                                         * that machine and not a defect */
}

/* w_gxorcell - one cell's span, XOR-ed.  6.9.1's selection fast path: two of
 * these move the selection for ~1.5 ms where two re-composed bands are 5-10.
 * XOR-ing an inverted cell restores it and a plain one inverts it, so the
 * incremental path lands where a re-compose would. */
static void w_gxorcell(int i, int r1, int c1)
{
    int x, y, b, cx;

    if (!w_gonscreen(r1, c1))
        return;
    b = (r1 - 1) - w_gtop;
    cx = WG_GUT + ((c1 - 1) - w_gleft) * WG_COLW;
    w_rectof(i);
    x = w_rect[0] + (cx << 3);
    y = w_rect[1] + ((WG_BAR + WG_HDR + b) << 3);
    if (y + 7 > w_ybot)
        return;
    wd_xor(x, y, x + (WG_COLW << 3) - 1, y + 7);
}

/* w_gpaint - the whole component: the formula bar, the header, every data
 * band.  6.9.1's full repaint, and 14's `full 20-row page` row. */
static void w_gpaint(int i)
{
    int k, x1, y1, x2;

    if (!w_ggeom())
        return;
    w_rectof(i);
    x1 = w_rect[0];
    y1 = w_rect[1];
    x2 = w_rect[2];
    if (w_gbar >= 0) {
        w_fld[w_gbar][LNW_X1] = x1;
        w_fld[w_gbar][LNW_Y1] = y1;
        w_fld[w_gbar][LNW_X2] = x2;
        w_fld[w_gbar][LNW_Y2] = y1 + (WG_BAR << 3) - 1;
        if (w_fld[w_gbar][LNW_Y2] <= w_ybot)
            wd_ldraw(w_fld[w_gbar]);
    }
    w_gemit(i, -1);
    for (k = 0; k < w_gvr; k++)
        w_gemit(i, k);
}

/* w_gflush - the rows 5.5.1's walk dirtied, one blit each and nothing else on
 * the card touched.  Called from wevent.c's flush, under its lock hold. */
static void w_gflush(void)
{
    int i, k, r;

    w_gredraw = 0;
    i = w_ggeom();
    if (!i)
        return;
    i--;
    for (k = 0; k < w_gvr; k++) {
        r = w_gtop + k;
        if (w_gisdirty(r))
            w_gemit(i, k);
    }
    os88_memset(w_gdirty, 0, sizeof(w_gdirty));
}

/* ============================================================================
 * THE FORMULA BAR AND THE SELECTION (WEAVE-SPEC 6.9.3, 6.9.4)
 * ==========================================================================*/

/* w_gload - 6.9.3 run backwards: the selected cell's SOURCE into the bar.
 *
 * A BUNDLE formula loads as `=?` and that is the contract's own answer rather
 * than a shortfall: 2.9 carries compiled RPN and no formula text, and
 * decompiling it would be a third implementation of 5.1 to keep in step with
 * the other two. Committing over it replaces it, which is the operation the
 * user was reaching for. */
static void w_gload(void)
{
    int r, c, n;
    unsigned k, p;

    if (w_gbar < 0 || w_gseg == 0)
        return;
    r = w_gsel_r - 1;
    c = w_gsel_c - 1;
    w_str[0] = 0;
    k = w_gkind(r, c);
    if (k == WGK_BFORM)
        os88_strcpy(w_str, "=?", W_STRMAX);
    else if (k == WGK_RFORM) {
        /* The SOURCE, which 5.6's kind-6 slot carries after the RPN - one
         * length byte and its characters. It is stored because 6.9.3 has to
         * be able to show a typed formula again, and the alternative is
         * decompiling the RPN. */
        p = w_gpay(r, c);
        n = (int)w_b(w_gseg, p + WGS_RPN + w_w(w_gseg, p + WGS_LEN));
        if (n > W_STRMAX - 3)
            n = W_STRMAX - 3;
        w_str[0] = '=';
        if (n > 0)
            w_copy(w_gseg, p + WGS_RPN + w_w(w_gseg, p + WGS_LEN) + 1,
                   w_str + 1, (unsigned)n);
        w_str[n + 1] = 0;
    } else {
        w_gshow(r, c);
        os88_strcpy(w_str, w_gdisp, W_STRMAX);
    }
    wd_lset(w_fld[w_gbar], w_str);
}

/* w_gselect - 6.9.4's ONE body for the click, the arrow key and select().
 *
 * It scrolls by the MINIMUM that keeps the new cell visible, moves the
 * picture the cheap way when both cells are on screen, reloads the bar and
 * enqueues onselect exactly once. Three copies of a scroll clamp would be
 * three places to get the minimum wrong in - which is why the model has one
 * too (weavesim's gselect). */
static void w_gselect(int r1, int c1, int fire)
{
    int i, oldr, oldc, scrolled;

    i = w_ggeom();
    if (!i)
        return;
    i--;
    if (r1 < 1)
        r1 = 1;
    if (r1 > w_grows)
        r1 = w_grows;
    if (c1 < 1)
        c1 = 1;
    if (c1 > w_gcols)
        c1 = w_gcols;
    oldr = w_gsel_r;
    oldc = w_gsel_c;
    scrolled = 0;
    if ((r1 - 1) < w_gtop) {
        w_gtop = r1 - 1;
        scrolled = 1;
    } else if (w_gvr && (r1 - 1) >= w_gtop + w_gvr) {
        w_gtop = r1 - w_gvr;
        scrolled = 1;
    }
    if ((c1 - 1) < w_gleft) {
        w_gleft = c1 - 1;
        scrolled = 1;
    } else if ((c1 - 1) >= w_gleft + w_gvc) {
        w_gleft = c1 - w_gvc;
        scrolled = 1;
    }
    w_gsel_r = r1;
    w_gsel_c = c1;
    if (scrolled)
        w_gpaint(i);                    /* the window moved: every band */
    else if (oldr != r1 || oldc != c1) {
        w_gxorcell(i, oldr, oldc);      /* 14's 2-XOR row, ~1.5 ms */
        w_gxorcell(i, r1, c1);
    }
    w_gload();
    if (w_gbar >= 0 && !scrolled)
        wd_ldraw(w_fld[w_gbar]);
    if (fire)
        w_enq(w_gid, WA_ONSELECT, r1, c1);
}

/* w_gsay - 6.9.2's refusal, which has no file and no line to name. */
static void w_gsay(const char *s)
{
    w_l0();
    w_ls("Formula: ");
    w_ls(s);
    os88_strcpy(w_serr, w_line, sizeof(w_serr));
    os88_toast("Formula refused - see Info", 0);
}

/* ============================================================================
 * WRITING A CELL
 * ==========================================================================*/

static void w_gtrigger(void)
{
    w_gpend = 1;                        /* collapse to one; the walk restarts
                                         * at pass 1 (5.5.1) */
    w_kick();
}

/* w_gsetnum - 5.6: kind 1 when the value is a whole number in int range (no
 * pool cost at all), kind 2 otherwise. */
static int w_gsetnum(int r, int c, int lo, int hi)
{
    unsigned p;

    if (lo == 0) {
        w_gput(r, c, WGK_INT, 0, (unsigned)hi);
        return 1;
    }
    p = w_galloc(4);
    if (p == 0)
        return 0;
    w_pw(w_gseg, p, (unsigned)lo);
    w_pw(w_gseg, p + 2, (unsigned)hi);
    w_gput(r, c, WGK_NUM, 0, p);
    return 1;
}

static int w_gsetstr(int r, int c, const char *s)
{
    unsigned n, p;

    n = os88_strlen(s);
    if (n > 255)
        n = 255;
    p = w_galloc(n + 1);
    if (p == 0)
        return 0;
    w_pb(w_gseg, p, n);
    w_pcopy((char *)s, w_gseg, p + 1, n);
    w_gput(r, c, WGK_STR, 0, p);
    return 1;
}

/* ovl_gsetform - a runtime formula (5.6 kind 6): the length word, the cached
 * and pre-walk values, then the RPN 6.9.2 compiled, all in one pool slot.
 * `src` is the SOURCE text, kept so the bar can show it again (6.9.3). */
static int ovl_gsetform(int r, int c, const unsigned char *rpn, unsigned n,
                      const char *src)
{
    unsigned p, sn;

    sn = os88_strlen(src);
    if (sn > 255)
        sn = 255;
    p = w_galloc(WGS_RPN + n + 1 + sn);
    if (p == 0)
        return 0;
    w_pw(w_gseg, p + WGS_LEN, n);
    w_pw(w_gseg, p + WGS_CACHED, 0);
    w_pw(w_gseg, p + WGS_CACHED + 2, 0);
    w_pw(w_gseg, p + WGS_PREV, 0);
    w_pw(w_gseg, p + WGS_PREV + 2, 0);
    w_pcopy((char *)rpn, w_gseg, p + WGS_RPN, n);
    w_pb(w_gseg, p + WGS_RPN + n, sn);          /* the source, after the RPN */
    w_pcopy((char *)src, w_gseg, p + WGS_RPN + n + 1, sn);
    w_gput(r, c, WGK_RFORM, 0, p);
    return 1;
}

/* ============================================================================
 * 5.5's RECALCULATION, SLICED
 * ==========================================================================*/

/* w_gformrpn - the (segment, offset, length) of a formula cell's RPN.
 * Kind 4's lives in the read-only bundle claim and kind 6's in the grid
 * claim's own pool, which is the ONLY difference between the two kinds. */
static unsigned w_gr_seg, w_gr_off, w_gr_len;

static int w_gformrpn(int r, int c)
{
    unsigned k, p, idx, base, off;

    k = w_gkind(r, c);
    p = w_gpay(r, c);
    if (k == WGK_RFORM) {
        w_gr_seg = w_gseg;
        w_gr_len = w_w(w_gseg, p + WGS_LEN);
        w_gr_off = p + WGS_RPN;
        return 1;
    }
    if (k != WGK_BFORM)
        return 0;
    idx = w_w(w_gseg, p + WGS_LEN);
    if (idx >= w_nformula)
        return 0;
    /* 2.9 stores per-formula OFFSETS and no lengths, so a stream runs to the
     * next formula's offset or to the section's end - a subtraction, not a
     * field. */
    base = w_soff[W_FXCODE];
    off = w_w(w_seg, base + 2 + idx * 2);
    w_gr_seg = w_seg;
    w_gr_off = base + off;
    if (idx + 1 < w_nformula)
        w_gr_len = w_w(w_seg, base + 2 + (idx + 1) * 2) - off;
    else
        w_gr_len = w_slen[W_FXCODE] - off;
    return 1;
}

/* w_gone - evaluate one formula cell into w_gv[].  Answers the FX ops it
 * cost, and always at least 1. */
static int w_gone(int r, int c)
{
    int ops;

    w_gv[0] = 2;
    w_gv[1] = 0;
    w_gv[2] = 0;
    if (!w_gformrpn(r, c))
        return 1;
    ops = wfx_eval(w_gr_seg, w_gr_off, w_gr_len, w_gv);
    if (ops > 0)
        return ops;
    /* Not a formula: a `.WAB` that never went through a packer (10.4). The
     * cell shows #DIV0 - the only display the format has for "no answer" -
     * and the app carries on. */
    w_gv[0] = 2;
    return 1;
}

/* w_gcache - write a formula cell's cached value and its error flag. */
static void w_gcache(int r, int c, int type, int lo, int hi, unsigned f)
{
    unsigned p;

    p = w_gpay(r, c);
    if (type == 2) {
        f |= WGF_ERR;
        lo = 0;
        hi = 0;
    } else
        f = f & ~WGF_ERR;
    w_gsetflags(r, c, f);
    w_pw(w_gseg, p + WGS_CACHED, (unsigned)lo);
    w_pw(w_gseg, p + WGS_CACHED + 2, (unsigned)hi);
}

/* w_gstep - one slice's worth of 5.5's two passes.  Answers the FX ops spent,
 * which wevent.c charges against 4.10's budget one for one.
 *
 * The walk is resumable at every cell because its whole state is 5.5.1's four
 * fields and one pool slot per formula - none of it on a stack a slice does
 * not own. */
static int w_gstep(int budget)
{
    int spent, r, c, n, type, v1lo, v1hi, wascirc;
    unsigned f, p;

    if (w_gseg == 0)
        return 0;
    if (w_gpend) {
        w_gpend = 0;
        w_gpass = 1;                    /* 5.5.1: a trigger restarts the walk */
        w_gcur = 0;
        w_gchanged = 0;
    }
    if (w_gpass == 0)
        return 0;
    spent = 0;
    n = w_grows * w_gcols;
    while (w_gcur < n) {
        if (spent >= budget)
            return spent;
        r = w_gcur / w_gcols;
        c = w_gcur - r * w_gcols;
        w_gcur++;
        if (!w_gisform(w_gkind(r, c)))
            continue;
        p = w_gpay(r, c);
        f = w_gflags(r, c);
        if (w_gpass == 1) {
            /* Save the PRE-WALK value and the two display bits its dword
             * cannot carry, then write pass 1's answer over the cached one -
             * where pass 2 reads it as its own comparand (5.5.1). */
            f = f & ~(WGF_ERRWAS | WGF_CIRCWAS);
            if (w_gflags(r, c) & WGF_ERR)
                f |= WGF_ERRWAS;
            if (w_gflags(r, c) & WGF_CIRC)
                f |= WGF_CIRCWAS;
            w_pw(w_gseg, p + WGS_PREV, w_w(w_gseg, p + WGS_CACHED));
            w_pw(w_gseg, p + WGS_PREV + 2, w_w(w_gseg, p + WGS_CACHED + 2));
            spent += w_gone(r, c);
            w_gcache(r, c, w_gv[0], w_gv[1], w_gv[2], f);
            continue;
        }
        /* Pass 2. The cached value is still pass 1's, which is exactly the
         * comparand 5.5's step 2 asks for. */
        v1lo = (int)w_w(w_gseg, p + WGS_CACHED);
        v1hi = (int)w_w(w_gseg, p + WGS_CACHED + 2);
        wascirc = (f & WGF_ERR) ? 1 : 0;        /* pass 1 ended in #DIV0 */
        spent += w_gone(r, c);
        type = w_gv[0];
        f = f & ~WGF_CIRC;
        if (type == 2) {
            if (!wascirc)
                f |= WGF_CIRC;          /* a number became the error value */
        } else if (wascirc || w_gv[1] != v1lo || w_gv[2] != v1hi)
            f |= WGF_CIRC;
        /* What it showed before the walk, out of the pre-walk slot... */
        w_gshowv((int)w_w(w_gseg, p + WGS_PREV),
                 (int)w_w(w_gseg, p + WGS_PREV + 2),
                 (f & WGF_ERRWAS) != 0, (f & WGF_CIRCWAS) != 0);
        os88_strcpy(w_gold, w_gdisp, WG_DMAX);
        w_gcache(r, c, type, w_gv[1], w_gv[2], f);
        w_gshow(r, c);                  /* ...and what it shows now */
        if (!w_gsame(w_gold, w_gdisp)) {
            w_gchanged++;
            w_growdirty(r);
        }
    }
    if (w_gpass == 1) {
        w_gpass = 2;
        w_gcur = 0;
        return spent;
    }
    w_gpass = 0;
    w_gredraw = 1;                      /* wevent.c's flush draws the rows */
    w_enq(w_gid, WA_ONCALC, w_gchanged, 0);
    return spent;
}

static int w_gbusy(void)
{
    return w_gpass != 0 || w_gpend != 0;
}

/* ============================================================================
 * THE COMMIT (WEAVE-SPEC 6.9.3)
 * ==========================================================================*/

/* ovl_gcommit - classify the bar's text and store it, in 6.9.3's pinned order.
 *
 * The order IS the contract: `=` first means a formula is never mistaken for
 * a label, and the number test before the label means `12` is twelve and not
 * the word. A formula that will not compile commits NOTHING and leaves the
 * text in the bar, so it can be fixed rather than retyped. */
static void ovl_gcommit(void)
{
    /* IT RAN. A refused overlay returns 0 and answers nothing at all
     * (apps/cc/crt0.asm), so the caller sets this to 0 first and reads it
     * after - WEAVE-SPEC 1.2's rule that a tenant says "it ran" separately
     * from what it decided. Without it, Enter on a machine whose WEAVE.OVL
     * went away would do NOTHING, silently, which is the one failure this
     * family refuses to have (SPEC.md 47). */
    const char *s;
    int r, c, ok;

    w_gcommitran = 1;
    if (w_gbar < 0 || w_gseg == 0)
        return;
    r = w_gsel_r - 1;
    c = w_gsel_c - 1;
    s = (const char *)w_fld[w_gbar][LNW_BUF];
    while (*s == ' ')
        s++;
    if (*s == 0) {
        w_gput(r, c, WGK_EMPTY, 0, 0);
        ok = 1;
    } else if (*s == '=') {
        if (!ovl_fxc(s + 1, w_gcols, w_grows)) {
            w_gsay(w_fxc_err);
            return;                     /* 6.9.3: nothing is committed */
        }
        ok = ovl_gsetform(r, c, w_fxc_out, w_fxc_n, s + 1);
    } else if (ovl_gnumber(s))
        ok = w_gsetnum(r, c, w_gnum_lo, w_gnum_hi);
    else
        ok = w_gsetstr(r, c, s);
    if (!ok) {
        w_gsay("the cell pool is full.");
        return;
    }
    w_growdirty(r);
    w_gredraw = 1;
    w_gload();                          /* what the cell says it is now - a
                                         * typed `3.50` reads back `3.5`, and
                                         * a label keeps its case */
    wd_ldraw(w_fld[w_gbar]);
    w_enq(w_gid, WA_ONEDIT, r + 1, c + 1);
    w_gtrigger();
}

/* ============================================================================
 * THE CLICK AND THE KEYS (WEAVE-SPEC 6.9.4)
 * ==========================================================================*/

/* w_gclick - a press inside the grid.  Answers 1 when it landed in the
 * formula bar, so wact.c can arm the field. */
static int w_gclick(int i, int x, int y)
{
    int row, col, r, c;

    if (!w_ggeom())
        return 0;
    w_gactive = 1;
    w_rectof(i);
    row = (y - w_rect[1]) >> 3;
    if (row < WG_BAR)
        return 1;                       /* the bar: wact.c arms it */
    if (row < WG_CHROME)
        return 0;                       /* the header band does nothing */
    col = (x - w_rect[0]) >> 3;
    if (col < WG_GUT)
        return 0;                       /* ...and neither does the gutter */
    r = w_gtop + (row - WG_CHROME);
    c = w_gleft + (col - WG_GUT) / WG_COLW;
    if (r >= w_grows || c >= w_gcols)
        return 0;
    if (r + 1 == w_gsel_r && c + 1 == w_gsel_c)
        return 0;                       /* os88ui_adn's rule: a setter that
                                         * does not compare redraws for
                                         * nothing */
    w_gselect(r + 1, c + 1, 1);
    return 0;
}

/* w_gkey - 6.9.4's arrows.  Answers 1 when the key was used. */
static int w_gkey(int ascii, int scan)
{
    if (w_gid == 0 || w_gseg == 0 || !w_gactive)
        return 0;
    if (ascii != 0)
        return 0;
    if (scan == 0x48)
        w_gselect(w_gsel_r - 1, w_gsel_c, 1);
    else if (scan == 0x50)
        w_gselect(w_gsel_r + 1, w_gsel_c, 1);
    else if (scan == 0x4B)
        w_gselect(w_gsel_r, w_gsel_c - 1, 1);
    else if (scan == 0x4D)
        w_gselect(w_gsel_r, w_gsel_c + 1, 1);
    else
        return 0;
    return 1;
}

/* ============================================================================
 * THE WJS SURFACE (WEAVE-SPEC 6.9's `Surface:` line)
 * ==========================================================================*/

/* w_gcellint - `g.cell(r,c)` in 5.2's terms: the integer part, truncated
 * toward zero, and out of int range is a script error rather than a wrap. */
static int w_gcellerr;                  /* 0 ok, 1 #DIV0, 2 out of range */

static int w_gcellint(int r, int c)
{
    int t, neg;

    w_gcellerr = 0;
    t = wfx_cell(r, c, w_gv);           /* TWO ints, low then high - and they
                                         * go in slots 0 and 1 because no
                                         * wfx_eval is running to own them */
    if (t == 0)
        return 0;                       /* empty and label read as 0, which
                                         * is the model's answer too */
    if (t == 2) {
        w_gcellerr = 1;
        return 0;
    }
    neg = wfx_mag(w_gv[0], w_gv[1], w_gmag);
    /* The magnitude's HIGH word is the truncated integer part, and it is read
     * unsigned: 32,768 is a legal answer only when the value is negative, and
     * as a signed int it reads as -32,768 and would pass a signed test that
     * the positive case must fail (5.2's "out of range is a script error"). */
    if ((unsigned)w_gmag[1] > (neg ? 32768u : 32767u)) {
        w_gcellerr = 2;
        w_gshowv(w_gv[0], w_gv[1], 0, 0);   /* the sentence names the value */
        return 0;
    }
    return neg ? -(int)(unsigned)w_gmag[1] : w_gmag[1];
}

/* w_gclear - 6.9's `clear()`: every NON-formula cell emptied. */
static void w_gclear(void)
{
    int r, c;

    for (r = 0; r < w_grows; r++)
        for (c = 0; c < w_gcols; c++)
            if (!w_gisform(w_gkind(r, c)) && w_gkind(r, c) != WGK_EMPTY) {
                w_gput(r, c, WGK_EMPTY, 0, 0);
                w_growdirty(r);
            }
    w_gredraw = 1;
}

/* ============================================================================
 * BIRTH AND DEATH
 * ==========================================================================*/

static void w_gfree(void)
{
    wfx_bind(0, 0, 0);
    if (w_gseg) {
        os88_mem_free(w_gseg);
        w_gseg = 0;
    }
    w_gkb = 0;
    w_gid = 0;
    w_gcols = 0;
    w_grows = 0;
    w_gtop = 0;
    w_gleft = 0;
    w_gbar = -1;
    w_gactive = 0;
    w_gpass = 0;
    w_gpend = 0;
    w_gcur = 0;
    w_gchanged = 0;
    w_gredraw = 0;
    w_gsel_r = 1;
    w_gsel_c = 1;
    os88_memset(w_gdirty, 0, sizeof(w_gdirty));
}
