/* ============================================================================
 * os8088 - apps/runcpm/rcterm.c        the terminal: model, parser, glass
 *
 * Part of RUNCPM (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo
 * Dantas / "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT
 * licence, Copyright (c) 2017 Mockba the Borg). #included by runcpm.c: one
 * translation unit (SPEC.md 70.1).
 *
 * WHAT IS RUNCPM'S HERE. RunCPM has no terminal of its own: its console is
 * the host's, and every byte a CP/M program writes goes to it through
 * console.h's _putcon, masked to 7 bits (mask8bit = 0x7F) and otherwise
 * untranslated - CR, LF, BS, BEL, TAB and every ESC sequence are the
 * terminal's business. The terminal RunCPM assumes is an 80x25 VT100/ANSI
 * (abstraction_runvt.h: cols 80, rows 25; _clrscr = ESC[2J ESC[H; the master
 * disk's TE.COM is configured for 25x80). So this file is that terminal: the
 * 7-bit mask is console.h's, the geometry is runvt's, and the escape subset
 * is what the programs on the master disk (TE, WordStar, CLS) actually send -
 * ESC [ H f A B C D J K m L M s u ?25h/l. Everything outside it is swallowed
 * (SPEC.md 71.4: RunCPM carries no terminal emulator to be faithful to).
 * Only reverse video renders: one 8x8 face, and two adapters of three are
 * 1bpp (SPEC.md 71.4).
 *
 * WHAT IS THIS PLATFORM'S: THE GLASS SHADOW (SPEC.md 71.2, 70.12).
 * PERFORMANCE.md prices a redraw by CALLS: 756 us for any gfx_* call and
 * ~900 us per glyph cell on the target machine. A terminal that redrew its
 * screen per byte would take 1.8 s per byte. So:
 *
 *   - the MODEL is 25 rows x 80 columns of characters plus a reverse-video
 *     bitmap, in package bss; the rows are a RING (rc_prow[] maps a logical
 *     row to a physical one), so a scroll or an insert/delete-line is 25
 *     stores and no byte moves;
 *   - the SHADOW is what is on the glass, row for row, in the same shape;
 *   - a FLUSH compares each row that can have changed (dirty, or the cursor
 *     was or is on it) against its shadow row and draws only the differing
 *     span as ONE rc_band + os88_gfx_blit1 (rcband.inc composes the 1bpp band
 *     from the kernel's own glyph table) - the band is the first choice at
 *     EVERY width, because it is cheaper than one font_run even for one cell
 *     (measured, SPEC.md 71.2: 1.03 ms against 1.62), and font_run is only
 *     the fallback when blit1 refuses. The cursor is an inverse cell FOLDED
 *     INTO the row it is on, so it never costs a call of its own and can never
 *     be drawn twice; the one split is a cursor that moved along an unchanged
 *     row by more than the measured crossover, which is two 1-cell bands;
 *   - N whole-screen scrolls since the last flush become ONE os88_gfx_scroll
 *     of the visible cell rectangle, ONE white fill of the rows it vacated
 *     (§5.5 leaves them unspecified; a fill of an 8-px strip is a fifth of the
 *     79-cell band that redrawing them would cost) plus the rows that then
 *     differ - the shadow ring rotates with it, and the vacated rows are known
 *     blank;
 *   - a full repaint - the shadow invalid, the geometry changed - is ONE
 *     white fill of the whole content (it covers the two slivers outside the
 *     cell grid too) and then each row to its last non-blank cell, so a blank
 *     row costs nothing and a prompt screen is a handful of calls; a full
 *     TE screen pays the fill on top of its 25 rows and that is the stated
 *     trade (SPEC.md 71.2). Both refused (blit1 and scroll) is font_run runs.
 *
 * The host harness (hosttest/rcuitest.c) drives this file against a model of
 * the glass and prints the cost table.
 *
 * ROWS SHOWN ARE WHAT THE WINDOW HOLDS: the model is always 25x80; the flush
 * draws min(25, content_h/8) rows and min(80, content_w/8) columns from the
 * live geometry, so a 640-px framed window shows 79 or 78 columns and a CGA
 * framed window 17 rows, and the fullscreen latch shows all of it (71.2).
 * ==========================================================================*/

#define RC_COLS   80
#define RC_ROWS   25
#define RC_ATTRB  10                       /* attribute bytes per row */
#define RC_CELLH  8                        /* the 8x8 face: row pitch */

/* row r's byte offsets: r*80 and r*10 without a multiply (SPEC.md 70.11) */
#define RC_CHOFF(r)  ((((r) << 6) + ((r) << 4)))
#define RC_ATOFF(r)  ((((r) << 3) + ((r) << 1)))

/* --- the model ------------------------------------------------------------ */
static unsigned char rc_ch[RC_ROWS * RC_COLS];    /* characters, 32..126 */
static unsigned char rc_at[RC_ROWS * RC_ATTRB];   /* bit set = reverse video;
                                                    * bit 7 = leftmost cell */
static unsigned char rc_prow[RC_ROWS];             /* logical row -> physical */
static unsigned char rc_dirty[RC_ROWS];            /* logical row changed */
static int rc_dirty_any;                           /* anything to flush? */
static int rc_cx, rc_cy;                           /* cursor; rc_cx == RC_COLS
                                                    * = the pending wrap */
static int rc_curvis;                              /* ESC[?25h/l */
static int rc_rev;                                 /* SGR 7 in force */
static int rc_scr_top;                             /* the region scrolled
                                                    * since the last flush:
                                                    * rows top..24 ... */
static int rc_scr_n;                               /* ...by n rows, + = up
                                                    * (LF, ESC[M), - = down
                                                    * (ESC[L); 0 = none */
static int rc_scr_bad;                             /* two regions or two
                                                    * directions: no one
                                                    * gfx_scroll describes it */
static int rc_esc;                                 /* 0 text, 1 ESC, 2 CSI */
static int rc_prm[4];
static int rc_nprm;
static int rc_priv;                                /* CSI ? */
static int rc_scx, rc_scy;                         /* ESC[s .. ESC[u */
/* rc_bells - BELs since the last flush, one tone each - is declared in
 * runcpm.c ahead of the key ring, which rings it on an overrun */
static int rc_all_blank;                           /* ESC[2J happened
                                                    * and nothing was written
                                                    * since: the flush may
                                                    * clear the glass with ONE
                                                    * fill instead of a row
                                                    * per row */

/* --- the shadow ------------------------------------------------------------ */
static unsigned char rc_sh[RC_ROWS * RC_COLS];    /* what the glass shows;
                                                    * 0 = unknown cell */
static unsigned char rc_sha[RC_ROWS * RC_ATTRB];  /* ...and its attributes,
                                                    * cursor folded in */
static unsigned char rc_shrow[RC_ROWS];            /* logical -> physical */
static int rc_sh_ok;                               /* the shadow describes the
                                                    * glass at all */
static int rc_sh_rows, rc_sh_cols;                 /* the geometry it was
                                                    * drawn for */
static int rc_sh_cy;                               /* the row the cursor was
                                                    * DRAWN on, -1 = none */
static unsigned char rc_bandbuf[RC_COLS * RC_CELLH];   /* rc_band's output */
static unsigned char rc_want_at[RC_ATTRB];         /* a row's wanted attrs */
static char rc_runbuf[RC_COLS + 1];                /* a font_run span */

/* the counters the harness and the debug key read: calls and cells drawn */
static unsigned rc_n_calls, rc_n_cells, rc_n_scroll;

/* --- the assembly this file calls (apps/runcpm/rcband.inc) ---------------- */
void rc_band(unsigned char *dst, const unsigned char *chars,
             const unsigned char *attr, int first, int n);
int  rc_rowdiff(const unsigned char *a, const unsigned char *b, int n);
int  rc_rowdiffl(const unsigned char *a, const unsigned char *b, int n);

/* ==========================================================================
 * THE MODEL
 * ========================================================================*/

static void rc_row_clear(int prow)
{
    os88_memset(rc_ch + RC_CHOFF(prow), ' ', RC_COLS);
    os88_memset(rc_at + RC_ATOFF(prow), 0, RC_ATTRB);
}

static void rc_scr_reset(void)
{
    rc_scr_top = 0;
    rc_scr_n = 0;
    rc_scr_bad = 0;
}

static void rc_sh_inval(void)
{
    rc_sh_ok = 0;
    rc_scr_reset();
    rc_dirty_any = 1;
}

/* the glass can follow a run of scrolls with ONE gfx_scroll only if they all
 * moved the same region the same way; anything else is drawn row by row */
static void rc_scr_note(int top, int dir)
{
    if (rc_scr_bad)
        return;
    if (rc_scr_n == 0) {
        rc_scr_top = top;
        rc_scr_n = dir;
    } else if (top == rc_scr_top && (rc_scr_n > 0) == (dir > 0)) {
        rc_scr_n += dir;
    } else {
        rc_scr_bad = 1;
    }
}

static void rc_term_init(void)
{
    int r;
    for (r = 0; r < RC_ROWS; r++) {
        rc_prow[r] = (unsigned char)r;
        rc_shrow[r] = (unsigned char)r;
        rc_dirty[r] = 1;
        rc_row_clear(r);
    }
    rc_cx = rc_cy = 0;
    rc_curvis = 1;
    rc_rev = 0;
    rc_esc = 0;
    rc_scx = rc_scy = 0;
    rc_sh_cy = -1;
    rc_sh_inval();
}

/* every logical row from `from` down is dirty */
static void rc_dirty_from(int from)
{
    int r;
    for (r = from; r < RC_ROWS; r++)
        rc_dirty[r] = 1;
    rc_dirty_any = 1;
}

/* scroll rows top..RC_ROWS-1 up by one: the top row's storage becomes the
 * new blank bottom row. 25 stores, no byte moves. THE DIRTY FLAGS ROTATE
 * WITH THE ROWS: the shadow ring is rotated the same way by the flush's
 * gfx_scroll, so a row that agreed with its shadow before the scroll agrees
 * after it, and only the vacated row is new - marking every row from `top`
 * dirty here would make the flush compare all of them (rc_rowdiff is a
 * repe cmpsb, ~370 us a row on the 8088: ~9 ms on top of every scrolled
 * line, a fifth of what the line costs). The flush marks every row from
 * `top` dirty ITSELF on the one path where it is true - a gfx_scroll the
 * kernel refused, so the glass did not move (below). */
static void rc_scroll_up(int top)
{
    int r;
    unsigned char p = rc_prow[top];
    for (r = top; r < RC_ROWS - 1; r++) {
        rc_prow[r] = rc_prow[r + 1];
        rc_dirty[r] = rc_dirty[r + 1];
    }
    rc_prow[RC_ROWS - 1] = p;
    rc_row_clear(p);
    rc_dirty[RC_ROWS - 1] = 1;             /* the vacated row */
    rc_dirty_any = 1;
    rc_scr_note(top, 1);                   /* the glass can follow with ONE
                                            * gfx_scroll (SPEC.md 5.5) */
}

/* the mirror image, for ESC[L: rows top.. move DOWN one, top becomes blank */
static void rc_scroll_down(int top)
{
    int r;
    unsigned char p = rc_prow[RC_ROWS - 1];
    for (r = RC_ROWS - 1; r > top; r--) {
        rc_prow[r] = rc_prow[r - 1];
        rc_dirty[r] = rc_dirty[r - 1];
    }
    rc_prow[top] = p;
    rc_row_clear(p);
    rc_dirty[top] = 1;
    rc_dirty_any = 1;
    rc_scr_note(top, -1);
}

static void rc_put_cell(int x, int y, int ch)
{
    int p = rc_prow[y];
    rc_all_blank = 0;
    rc_ch[RC_CHOFF(p) + x] = (unsigned char)ch;
    if (rc_rev)
        rc_at[RC_ATOFF(p) + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
    else
        rc_at[RC_ATOFF(p) + (x >> 3)] &= (unsigned char)~(0x80 >> (x & 7));
    rc_dirty[y] = 1;
    rc_dirty_any = 1;
}

/* erase cells x1..x2 of logical row y (attributes off) */
static void rc_erase(int y, int x1, int x2)
{
    int x;
    for (x = x1; x <= x2; x++)
        rc_put_cell(x, y, ' ');
}

static void rc_move(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= RC_COLS) x = RC_COLS - 1;
    if (y < 0) y = 0;
    if (y >= RC_ROWS) y = RC_ROWS - 1;
    rc_cx = x;
    rc_cy = y;
    rc_dirty_any = 1;                      /* the cursor is on the glass */
}

static void rc_lf(void)
{
    if (rc_cy >= RC_ROWS - 1)
        rc_scroll_up(0);
    else
        rc_cy++;
    rc_dirty_any = 1;
}

/* the state the whole screen has after ESC[2J: nothing on it, home */
static void rc_clear_all(void)   /* ESC[2J */
{
    int r;
    for (r = 0; r < RC_ROWS; r++)
        rc_row_clear(rc_prow[r]);
    rc_dirty_from(0);
    rc_all_blank = 1;
}

/* --- CSI dispatch --------------------------------------------------------- */
static int rc_p(int i, int dflt)
{
    int v = (i < rc_nprm) ? rc_prm[i] : 0;
    return v ? v : dflt;
}

static void rc_csi(int c)
{
    int n, r, x;
    switch (c) {
    case 'H': case 'f':
        rc_move(rc_p(1, 1) - 1, rc_p(0, 1) - 1);
        break;
    case 'A':
        rc_move(rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx, rc_cy - rc_p(0, 1));
        break;
    case 'B':
        rc_move(rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx, rc_cy + rc_p(0, 1));
        break;
    case 'C':
        rc_move((rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx) + rc_p(0, 1), rc_cy);
        break;
    case 'D':
        rc_move((rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx) - rc_p(0, 1), rc_cy);
        break;
    case 'J':
        n = rc_p(0, 0);
        x = rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx;
        if (n == 2) {
            rc_clear_all();
        } else if (n == 1) {
            for (r = 0; r < rc_cy; r++) rc_erase(r, 0, RC_COLS - 1);
            rc_erase(rc_cy, 0, x);
        } else {
            rc_erase(rc_cy, x, RC_COLS - 1);
            for (r = rc_cy + 1; r < RC_ROWS; r++) rc_erase(r, 0, RC_COLS - 1);
        }
        break;
    case 'K':
        n = rc_p(0, 0);
        x = rc_cx == RC_COLS ? RC_COLS - 1 : rc_cx;
        if (n == 2)      rc_erase(rc_cy, 0, RC_COLS - 1);
        else if (n == 1) rc_erase(rc_cy, 0, x);
        else             rc_erase(rc_cy, x, RC_COLS - 1);
        break;
    case 'm':
        /* SGR: 0 off, 7 reverse; bold/underline/colour are parsed and
         * ignored (SPEC.md 71.4). No parameter at all is 0. */
        if (rc_nprm == 0) rc_rev = 0;
        for (r = 0; r < rc_nprm; r++) {
            if (rc_prm[r] == 0) rc_rev = 0;
            else if (rc_prm[r] == 7) rc_rev = 1;
            else if (rc_prm[r] == 27) rc_rev = 0;
        }
        break;
    case 'L':
        for (n = rc_p(0, 1); n > 0; n--) rc_scroll_down(rc_cy);
        break;
    case 'M':
        for (n = rc_p(0, 1); n > 0; n--) rc_scroll_up(rc_cy);
        break;
    case 's':
        rc_scx = rc_cx; rc_scy = rc_cy;
        break;
    case 'u':
        /* the saved cursor comes back EXACTLY, a pending wrap included
         * (rc_scx == RC_COLS): ESC[s at column 80 then ESC[u then a
         * character must wrap the way it would have without the pair.
         * (wave 1 clamped it and lost the wrap: found by the reviewer) */
        rc_cx = rc_scx;
        rc_cy = rc_scy;
        rc_dirty_any = 1;
        break;
    case 'h':
    case 'l':
        if (rc_priv && rc_p(0, 0) == 25) {
            rc_curvis = (c == 'h');
            rc_dirty_any = 1;
        }
        break;
    default:
        break;                             /* swallowed (71.4) */
    }
}

/* rc_putc - one byte from the CP/M side, exactly as console.h's _putcon
 * would hand it to the host terminal: masked to 7 bits, nothing translated. */
static void rc_putc(int c)
{
    c &= rc_mask;                          /* mask8bit, console.h (0x7F, or
                                            * 0xFF after BDOS 230) */

    if (rc_esc == 1) {
        if (c == '[') {
            rc_esc = 2;
            rc_nprm = 0;
            rc_prm[0] = 0;
            rc_priv = 0;
        } else {
            rc_esc = 0;                    /* ESC x outside the subset */
        }
        return;
    }
    if (rc_esc == 2) {
        if (c >= '0' && c <= '9') {
            if (rc_nprm == 0) rc_nprm = 1;
            int v = rc_prm[rc_nprm - 1];
            rc_prm[rc_nprm - 1] = (v << 3) + (v << 1) + (c - '0');
        } else if (c == ';') {
            if (rc_nprm == 0) rc_nprm = 1;
            if (rc_nprm < 4) {
                rc_prm[rc_nprm] = 0;
                rc_nprm++;
            }
        } else if (c == '?') {
            rc_priv = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            rc_esc = 0;
            rc_csi(c);
        } else if (c < 0x20) {
            rc_esc = 0;                    /* a control inside a CSI ends it */
        }
        return;
    }

    switch (c) {
    case 0x1B: rc_esc = 1; return;
    case 0x0D: rc_cx = 0; rc_dirty_any = 1; return;
    case 0x0A: rc_lf(); return;
    case 0x08:
        if (rc_cx == RC_COLS) rc_cx = RC_COLS - 1;
        if (rc_cx > 0) rc_cx--;
        rc_dirty_any = 1;
        return;
    case 0x07: rc_bells++; return;
    case 0x09:
        if (rc_cx == RC_COLS) rc_cx = RC_COLS - 1;
        rc_cx = (rc_cx + 8) & ~7;
        if (rc_cx > RC_COLS - 1) rc_cx = RC_COLS - 1;
        rc_dirty_any = 1;
        return;
    case 0x0C: rc_lf(); return;            /* FF is a line feed, the VT100
                                             * rule and what RunCPM's host
                                             * terminals do (SPEC.md 71.2) */
    default: break;
    }
    if (c < 0x20 || c == 0x7F)
        return;                            /* no glyph, no motion (DEL is
                                            * ignored, the VT100 rule); a
                                            * byte above 0x7F - mask8bit at
                                            * 0xFF - takes a cell and draws
                                            * as blank, since the face is
                                            * 32..126 (rcband.inc) */
    if (rc_cx == RC_COLS) {                /* the pending wrap (VT100) */
        rc_cx = 0;
        rc_lf();
    }
    rc_put_cell(rc_cx, rc_cy, c);
    rc_cx++;                               /* may become RC_COLS: pending */
}

static void rc_puts(const char *s)
{
    while (*s)
        rc_putc(*s++);
}

/* ==========================================================================
 * THE GLASS: the flush
 * ========================================================================*/

/* the glass row is known BLANK (a fill just made it so): spaces, no attribute */
static void rc_sh_row_blank(int r)
{
    os88_memset(rc_sh + RC_CHOFF(rc_shrow[r]), ' ', RC_COLS);
    os88_memset(rc_sha + RC_ATOFF(rc_shrow[r]), 0, RC_ATTRB);
}

/* one uniform-attribute span through font_run: 1 call, n cells */
static void rc_draw_run(int x, int y, const unsigned char *chars, int n,
                        int rev)
{
    os88_memcpy(rc_runbuf, chars, n);
    rc_runbuf[n] = 0;
    if (rev)
        os88_font_run(x, y, rc_runbuf, OS88_WHITE, OS88_BLACK);
    else
        os88_font_run(x, y, rc_runbuf, OS88_BLACK, OS88_WHITE);
    rc_n_calls++;
    rc_n_cells += n;
}

/* the span f..l of a row, split into uniform-attribute runs: ONLY the
 * fallback when blit1 refuses (a kern_small kernel, SPEC.md 5.4.2) */
static void rc_draw_runs(int x0, int y, const unsigned char *chars,
                         const unsigned char *at, int f, int l)
{
    int s = f, i, rev, r2;
    while (s <= l) {
        rev = (at[s >> 3] >> (7 - (s & 7))) & 1;
        i = s;
        while (i < l) {
            r2 = (at[(i + 1) >> 3] >> (7 - ((i + 1) & 7))) & 1;
            if (r2 != rev) break;
            i++;
        }
        rc_draw_run(x0 + (s << 3), y, chars + s, i - s + 1, rev);
        s = i + 1;
    }
}

/* one composed band of n cells from column f of the row: ONE call, whatever
 * n is, at the band's own stride (rc_bandbuf is 80x8, so the blit is handed
 * RC_COLS and n*8 pixels); the runs only if the kernel refuses bands */
static void rc_draw_band(int x0, int y, const unsigned char *mch, int f, int n)
{
    rc_band(rc_bandbuf, mch + f, rc_want_at, f, n);
    rc_n_calls++;
    rc_n_cells += n;
    if (os88_gfx_blit1(rc_bandbuf, RC_COLS, x0 + (f << 3), y, n << 3, RC_CELLH) < 0) {
        rc_n_calls--;                      /* refused: nothing drawn */
        rc_n_cells -= n;
        rc_draw_runs(x0, y, mch, rc_want_at, f, f + n - 1);
    }
}

/* the same-row cursor split's threshold: two 1-cell bands cost 2 x (860 +
 * 173) = 2,066 us; a span of n cells 860 + 173 n; equal near n = 7, so a span
 * of 8 or more cells (the two cells 7 or more apart) is split (measured,
 * SPEC.md 71.2, PERFORMANCE.md Set 65) */
#define RC_SPLIT_SPAN 7

/* rc_flush_row - bring logical row r on the glass up to date */
static void rc_flush_row(int r, int x0, int y, int cols)
{
    unsigned char *mch = rc_ch + RC_CHOFF(rc_prow[r]);
    unsigned char *mat = rc_at + RC_ATOFF(rc_prow[r]);
    unsigned char *sch = rc_sh + RC_CHOFF(rc_shrow[r]);
    unsigned char *sat = rc_sha + RC_ATOFF(rc_shrow[r]);
    int i, f, l, fc, nb, nbits, n;

    /* what this row should show: the model, with the cursor folded in */
    os88_memcpy(rc_want_at, mat, RC_ATTRB);
    if (rc_curvis && rc_cy == r && rc_cx < RC_COLS)
        rc_want_at[rc_cx >> 3] ^= (unsigned char)(0x80 >> (rc_cx & 7));

    fc = f = rc_rowdiff(mch, sch, cols);
    l = (f < 0) ? -1 : rc_rowdiffl(mch, sch, cols);
    /* the attributes, BIT-exact: an attribute byte that differs widens the
     * span only to the cells whose bits differ, so a moved cursor is the
     * two cells it left and reached, not their 8-cell groups; nbits counts
     * the changed cells (the split below wants to know when it is two) */
    nb = (cols + 7) >> 3;
    nbits = 0;
    for (i = 0; i < nb; i++) {
        int d = rc_want_at[i] ^ sat[i];
        if (d) {
            int a = i << 3, b = a + 7, m;
            for (m = 0x80; !(d & m); m >>= 1) a++;      /* highest set bit */
            for (m = 0x01; !(d & m); m <<= 1) b--;      /* lowest set bit */
            for (m = 0x80; m; m >>= 1) if (d & m) nbits++;
            if (b > cols - 1) b = cols - 1;
            if (a > cols - 1) continue;                 /* past the window */
            if (f < 0 || a < f) f = a;
            if (b > l) l = b;
        }
    }
    if (f < 0)
        return;                            /* the glass already agrees */

    n = l - f + 1;
    /* a cursor that moved along an otherwise unchanged row: if the two cells
     * are further apart than the measured crossover, two 1-cell bands are
     * cheaper than the span (SPEC.md 71.2: a band is ~860 us + 173 us a
     * cell, so two 1-cell bands are 2.07 ms and a span passes that at 8
     * cells) */
    if (fc < 0 && nbits == 2 && n > RC_SPLIT_SPAN) {
        rc_draw_band(x0, y, mch, f, 1);
        rc_draw_band(x0, y, mch, l, 1);
    } else {
        rc_draw_band(x0, y, mch, f, n);
    }
    os88_memcpy(sch, mch, RC_COLS);
    os88_memcpy(sat, rc_want_at, RC_ATTRB);
}

/* rc_flush - put the model on the glass, drawing only what differs.
 * The gfx lock is HELD by the caller (a paint callback, or os88_onwake around
 * exactly this call), and the clip region is armed if this is not a paint. */
static void rc_flush(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    int rows, cols, r, x0, y0;
    unsigned char p;

    if (os88_wm_geom(win, &sz) < 0)
        return;                            /* not visible: nothing to do */
    os88_wm_content(win, &org);
    cols = sz.w >> 3;
    if (cols > RC_COLS) cols = RC_COLS;
    rows = sz.h >> 3;
    if (rows > RC_ROWS) rows = RC_ROWS;
    x0 = org.x;
    y0 = org.y;

    if (!rc_sh_ok || rows != rc_sh_rows || cols != rc_sh_cols) {
        /* the whole repaint: ONE white fill of the entire content - it covers
         * the two slivers outside the cell grid as well, so os88_paint fills
         * nothing on this path - and the shadow says "blank", not "unknown",
         * so the row loop below draws each row only to its last non-blank
         * cell and a blank row costs nothing (SPEC.md 71.2: a banner or a
         * prompt screen is a handful of calls; a full TE screen pays this
         * fill on top of its 25 rows, ~70 ms on the model, and that is the
         * trade). The clip is armed if this is not a paint, so nothing of
         * another window's is touched. */
        os88_set_color(OS88_WHITE);
        os88_gfx_fill(x0, y0, x0 + sz.w - 1, y0 + sz.h - 1);
        rc_n_calls++;
        for (r = 0; r < RC_ROWS; r++) {
            rc_sh_row_blank(r);
            rc_dirty[r] = 1;               /* every row is compared: the
                                            * shadow is blank, not unknown */
        }
        rc_sh_rows = rows;
        rc_sh_cols = cols;
        rc_scr_reset();
        rc_all_blank = 0;                  /* the fill just did it */
    } else if (rc_scr_n != 0 && !rc_scr_bad) {
        /* the region rc_scr_top..24 moved by rc_scr_n rows: move the glass
         * the same way with ONE gfx_scroll over the VISIBLE part of it, then
         * rotate the shadow ring to match and mark the vacated rows unknown.
         * A refusal (a clip that does not hold the rect, a scroll of more
         * than the visible region) means the rows are drawn instead. */
        int n = rc_scr_n < 0 ? -rc_scr_n : rc_scr_n;
        int top = rc_scr_top;
        if (top < rows && n < rows - top &&
            os88_gfx_scroll(x0, y0 + (top << 3), x0 + (cols << 3) - 1,
                            y0 + (rows << 3) - 1, rc_scr_n << 3) == 0) {
            int vac = rc_scr_n > 0 ? rows - n : top;   /* the vacated rows */
            rc_n_calls++;
            rc_n_scroll++;
            /* §5.5 leaves the vacated rows unspecified: ONE white fill of the
             * strip makes them blank, and blank is what the model has there
             * unless something was written since - a DIR line's vacated row
             * then differs from its shadow by the cursor cell alone (one
             * 1-cell band, ~1 ms) instead of being redrawn whole (a 79-cell
             * band, ~14 ms) */
            os88_set_color(OS88_WHITE);
            os88_gfx_fill(x0, y0 + (vac << 3), x0 + (cols << 3) - 1,
                          y0 + ((vac + n) << 3) - 1);
            rc_n_calls++;
            /* the row the cursor was DRAWN on moved with the glass: its
             * shadow row (cursor bit and all) is at the new index, and the
             * compare below must visit it there - or a cursor left at row
             * 10 while ESC[M scrolls row 5 stays on the glass at row 9 */
            if (rc_sh_cy >= top && rc_sh_cy < rows) {
                r = rc_scr_n > 0 ? rc_sh_cy - n : rc_sh_cy + n;
                rc_sh_cy = (r < top || r >= rows) ? -1 : r;   /* or scrolled
                                                              * off */
            }
            while (n > 0) {
                if (rc_scr_n > 0) {         /* up: top row's shadow storage
                                             * becomes the vacated bottom */
                    p = rc_shrow[top];
                    for (r = top; r < rows - 1; r++) rc_shrow[r] = rc_shrow[r + 1];
                    rc_shrow[rows - 1] = p;
                    rc_sh_row_blank(rows - 1);
                    rc_dirty[rows - 1] = 1; /* the vacated glass row: the model
                                             * row now there may be anything
                                             * (a row from below a short
                                             * window, its flag long clear) */
                } else {                    /* down: the bottom's becomes the
                                             * vacated top */
                    p = rc_shrow[rows - 1];
                    for (r = rows - 1; r > top; r--) rc_shrow[r] = rc_shrow[r - 1];
                    rc_shrow[top] = p;
                    rc_sh_row_blank(top);
                    rc_dirty[top] = 1;
                }
                n--;
            }
        } else {
            /* refused (SPEC.md 5.5: the clip does not hold the whole
             * rectangle - a window overlapping the terminal - or the region
             * is not the visible one): nothing on the glass moved, so the
             * shadow is still true and nothing here is marked unknown; but
             * every model row from `top` down shifted against it, so every
             * one is compared and drawn to the span where the shifted text
             * differs from what stands. That is the price of an overlapped
             * terminal (SPEC.md 71.2): a scrolled line is a band per row
             * from `top` down - and this is the ONE place that dirties them
             * all (rc_scroll_up rotates the flags with the rows). */
            rc_dirty_from(top);
        }
        rc_scr_reset();
    } else if (rc_scr_bad) {
        rc_dirty_from(0);                  /* two regions or two directions:
                                            * no one gfx_scroll describes it,
                                            * so every row is compared */
        rc_scr_reset();
    }

    /* ESC[2J's price: if the whole model is blank and the glass is not, ONE
     * white fill of the cell rectangle clears it, and the shadow is told so;
     * the row loop below then finds only the cursor's row differing (1 more
     * call). Without this a cleared screen costs a call per row that had
     * anything on it - 25 for a full one. */
    if (rc_all_blank) {
        int blank = 1;
        for (r = 0; r < rows && blank; r++) {
            unsigned char *sc = rc_sh + RC_CHOFF(rc_shrow[r]);
            unsigned char *sa = rc_sha + RC_ATOFF(rc_shrow[r]);
            int i;
            for (i = 0; i < cols; i++)
                if (sc[i] != ' ') { blank = 0; break; }
            for (i = 0; i < RC_ATTRB && blank; i++)
                if (sa[i]) blank = 0;
        }
        if (!blank) {
            os88_set_color(OS88_WHITE);
            os88_gfx_fill(x0, y0, x0 + (cols << 3) - 1, y0 + (rows << 3) - 1);
            rc_n_calls++;
            for (r = 0; r < rows; r++) {
                rc_sh_row_blank(r);
                rc_dirty[r] = 0;
            }
        }
        /* consumed: from here the shadow carries the fact, and the row loop
         * does the rest. Left set, the next flush - a cursor move, ESC[?25l,
         * anything - would find the cursor's row non-blank in the shadow and
         * fill the whole rectangle again, erasing and redrawing the cursor:
         * a full-content fill and a double draw per flush while the screen
         * stays empty. The next ESC[2J / FF sets it again. */
        rc_all_blank = 0;
    }

    for (r = 0; r < rows; r++) {
        if (rc_dirty[r] || r == rc_sh_cy || r == rc_cy ||
            rc_sh[RC_CHOFF(rc_shrow[r]) ] == 0)
            rc_flush_row(r, x0, y0 + (r << 3), cols);
        rc_dirty[r] = 0;
    }
    /* rows below the window are model rows, not lost rows: they stay dirty
     * so that a taller window (fullscreen) draws them when it comes */
    rc_sh_ok = 1;
    rc_sh_cy = (rc_curvis && rc_cy < rows) ? rc_cy : -1;
    rc_dirty_any = 0;
}

/* rc_blank_rect - a rectangle of the content that something else has been
 * drawn over (a kernel repaint's damage rect under WF_OWNBG, os88_paint; the
 * About panel's rectangle, rcabout.c): ONE white fill of it - it covers the
 * two slivers outside the cell grid as far as it reaches them, so they cost
 * no fill of their own - and the cells it covers are then KNOWN BLANK in
 * the shadow, spaces with no attribute, not unknown: rc_flush draws each row
 * only to the span where the model differs, so a panel lifting off a mostly
 * blank terminal costs the fill and the text under it, never a band the
 * width of the rectangle per row (the erase-then-nothing shape; the whole-
 * repaint path made the same choice, SPEC.md 71.2). A partly-covered cell
 * is blanked in the shadow too: if the model has a glyph there it is redrawn
 * whole, and if it has a space the uncovered part was already white. Lock
 * held; the clip is the caller's (the kernel's paint clip, or the one the
 * caller armed), so the fill touches nothing of another window's. */
static void rc_blank_rect(void *win, int x1, int y1, int x2, int y2)
{
    static struct os88_pt org;
    int r, x, c0, c1, r0, r1;

    os88_wm_content(win, &org);
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x1, y1, x2, y2);
    rc_n_calls++;
    c0 = (x1 - org.x) >> 3;   if (c0 < 0) c0 = 0;
    c1 = (x2 - org.x) >> 3;   if (c1 > RC_COLS - 1) c1 = RC_COLS - 1;
    r0 = (y1 - org.y) >> 3;   if (r0 < 0) r0 = 0;
    r1 = (y2 - org.y) >> 3;   if (r1 > RC_ROWS - 1) r1 = RC_ROWS - 1;
    for (r = r0; r <= r1; r++) {
        if (c1 >= c0) {
            unsigned char *sa = rc_sha + RC_ATOFF(rc_shrow[r]);
            os88_memset(rc_sh + RC_CHOFF(rc_shrow[r]) + c0, ' ', c1 - c0 + 1);
            for (x = c0; x <= c1; x++)
                sa[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
        }
        rc_dirty[r] = 1;
    }
    rc_dirty_any = 1;
}

/* rc_bell_service - BEL rings the speaker, one tone per BEL, from OUTSIDE
 * the lock (os88_onwake calls it after the flush): the tone slot is any-
 * context, but nothing that is not drawing belongs inside a lock hold. */
static void rc_bell_service(void)
{
    while (rc_bells > 0) {
        os88_snd_tone(880, 2, 0x40);
        rc_bells--;
    }
}
