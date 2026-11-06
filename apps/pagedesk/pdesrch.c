/* ============================================================================
 * pdesrch.c - SEARCH (PAGEDESK-SPEC 9.3, 20.10.1; DECISIONS D14c)
 *
 * The engine owns the matcher; the renderer owns the text (PDV_REND_TEXTMAP,
 * 9.2) and the PRIMARY carries the one to the other: it asks PDREND for a
 * piece of a page's glyph records, writes them into the exchange block at
 * PDE_SRCH_PIECE (PDREND's dseg:doff can BE the XB, so nothing is copied
 * twice) and calls SEARCH with `found` = how many records the piece holds.
 *
 * THE PROTOCOL (pdxb_search, no field added):
 *   in   needle     PD Latin; read when state == 0, ignored after
 *        page       the page the piece belongs to (echoed back on a hit)
 *        glyph      skip records whose `run` is below this: they were
 *                   already matched (a re-call after a hit) or are before
 *                   the point a Find-next resumes from. They still feed the
 *                   gap rule, never the matcher
 *        state      0 = a NEW page stream (the matcher is reset), 1 = the
 *                   next piece of the same page
 *        found      the number of pdtm_glyph records at the XB's piece
 *                   offset (<= PDE_SRCH_MAXREC); it is an INPUT here
 *   out  found      1 = a hit completed inside this piece, else 0
 *        glyph      the run of the hit's first glyph
 *        glyph_end  the run of its last glyph
 *        state      1
 * A hit ends the call. The caller resumes with the SAME piece and
 * glyph = glyph_end + 1; the matcher has been reset, so matches never
 * overlap. A piece with no hit is consumed whole.
 *
 * WHAT MATCHES: case-insensitive over PD Latin (ASCII and the Latin-1 letters
 * fold). Spaces are not always glyphs - a justified line moves its words with
 * TJ numbers and a line ends with no blank at all - so the engine MAKES one
 * between two glyphs that sit a line apart or an em-fifth apart, and
 * collapses runs of blanks. A needle's leading and trailing blanks are
 * dropped. Reading order is the content stream's (9.2).
 *
 * THE MATCHER keeps the last 64 symbols and, on each new one, takes the
 * longest suffix that is a prefix of the needle - KMP's state without its
 * table, because a hit's FIRST glyph must be known and a ring that remembers
 * each symbol's run answers that for free. A needle is at most 63 symbols.
 * ==========================================================================*/
#define PDE_SRCH_PIECE  128     /* XB offset of the glyph records            */
#define PDE_SRCH_MAXREC 74      /* (1024 - 128) / 12                          */
#define PDE_SRCH_NDL    63
#define PDE_SRCH_VIRT   0xFFFF  /* a made blank has no glyph run              */

static pdb  sr_ndl[PDE_SRCH_NDL + 1];
static pdw  sr_nlen;
static pdb  sr_sym[64];
static pdw  sr_run[64];
static pdw  sr_pos;             /* symbols pushed since the page began        */
static pdw  sr_floor;           /* the match window may not reach below this  */
static pdw  sr_k;               /* the matched prefix length                  */
static pdw  sr_have;            /* a previous glyph is known                  */
static pd_pt sr_px1, sr_py0, sr_ph;
static pdw  sr_lastsp;          /* the last symbol pushed was a blank         */

static pdb sr_fold(pdb c)
{
    if (c >= 'A' && c <= 'Z') return (pdb)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (pdb)(c + 32);
    if (c == 0xA0 || c == 9 || c == 10 || c == 13) return ' ';
    return c;
}

static void sr_reset(void)
{
    sr_pos = 0; sr_floor = 0; sr_k = 0; sr_have = 0; sr_lastsp = 1;
}

/* push a symbol; 1 when the needle is now complete */
static pdw sr_push(pdb c, pdw run)
{
    pdw j, i, lim;
    sr_sym[sr_pos & 63] = c;
    sr_run[sr_pos & 63] = run;
    sr_pos++;
    sr_lastsp = (c == ' ');
    lim = (pdw)(sr_pos - sr_floor);
    j = sr_k + 1;
    if (j > lim) j = lim;
    if (j > sr_nlen) j = sr_nlen;
    if (!(j == sr_k + 1 && c == sr_ndl[sr_k])) {
        for (; j > 0; j--) {
            for (i = 0; i < j; i++)
                if (sr_sym[(pdw)(sr_pos - j + i) & 63] != sr_ndl[i]) break;
            if (i == j) break;
        }
    }
    sr_k = j;
    return sr_k == sr_nlen;
}

static pdw sr_abs(pd_pt a, pd_pt b)
{
    return (pdw)(a > b ? a - b : b - a);
}

static pdw v_search(void)
{
    static struct pdxb_search sr;
    static struct pdtm_glyph g;
    pdw i, n, c, j, h, dx, dy;

    pd_fcopy_in(g_xb, 0, &sr, sizeof(struct pdxb_search));
    n = sr.found;
    sr.found = 0;
    if (n > PDE_SRCH_MAXREC) return PDE_RANGE;
    if (sr.state == 0) {
        sr.needle[63] = 0;
        sr_nlen = 0;
        for (i = 0; sr.needle[i]; i++) {
            c = sr_fold((pdb)sr.needle[i]);
            if (c == ' ' && (sr_nlen == 0 || sr_ndl[sr_nlen - 1] == ' ')) continue;
            if (sr_nlen < PDE_SRCH_NDL) sr_ndl[sr_nlen++] = (pdb)c;
        }
        while (sr_nlen && sr_ndl[sr_nlen - 1] == ' ') sr_nlen--;
        if (sr_nlen == 0) return PDE_RANGE;
        sr_reset();
    }
    sr.state = 1;
    for (i = 0; i < n; i++) {
        pd_fcopy_in(g_xb, (pdw)(PDE_SRCH_PIECE + i * 12), &g, 12);
        c = sr_fold(g.code);
        h = sr_abs(g.y1, g.y0);
        /* the made blank: a new line, or a gap of a fifth of the em */
        j = 0;
        if (sr_have && sr_ph) {
            dy = sr_abs(g.y0, sr_py0);
            dx = (pdw)(g.x0 > sr_px1 ? g.x0 - sr_px1 : 0);
            if (dy > sr_ph / 2 || dx > sr_ph / 5) j = 1;
        }
        sr_have = 1; sr_px1 = g.x1; sr_py0 = g.y0; sr_ph = h;
        if (g.run < sr.glyph) continue;
        if (j && c != ' ' && !sr_lastsp)
            if (sr_push(' ', PDE_SRCH_VIRT)) goto hit;
        if (c == ' ' && sr_lastsp) continue;
        if (sr_push((pdb)c, g.run)) goto hit;
        continue;
hit:
        sr.found = 1;
        sr.glyph_end = g.run;
        sr.glyph = sr_run[(pdw)(sr_pos - sr_nlen) & 63];
        sr_floor = sr_pos;
        sr_k = 0;
        break;
    }
    pd_fcopy_out(&sr, g_xb, 0, sizeof(struct pdxb_search));
    return PDE_OK;
}
