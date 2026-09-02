/* ============================================================================
 * os8088 - apps/loom/lmsheet.c
 *
 * THE FX PRE-COMPILER and the sprite renderer - LOOM.OVL's third tenant
 * (WEAVE-SPEC 1.2) and the .WSP half of the pack step.
 *
 * ---------------------------------------------------------------------------
 * THE FX COMPILER IS apps/weave/wfxc.c, INCLUDED
 * ---------------------------------------------------------------------------
 * WEAVE already carries a complete compiler for WEAVE-SPEC 5.1 - the formula
 * bar's, WEAVE-SPEC 9.4's one carve-out - and its own header states the rule
 * this file obeys: "Two grammars for one language is the drift WEAVE-SPEC 11's
 * byte-identity rule exists to prevent, said about a language instead of about
 * a file." So LOOM does not write a second one. It `#include`s that file, the
 * shipping text, exactly as WEAVE-SPEC 1.2 says the two packages share code:
 * as source, never as a copy.
 *
 * TWO CONSEQUENCES, both stated rather than discovered:
 *
 *   1. The RPN buffer moves. wfxc.c puts its output in the middle of WEAVE's
 *      header probe, which LOOM has not got; `w_fxc_out` is a #define with an
 *      `#ifndef` around it and this file sets it first.
 *   2. THE REFUSAL SENTENCES ARE 6.9.2's, NOT weavesim's OLD ONES. A shared
 *      compiler has one vocabulary by construction, so `tools/weavesim.py`
 *      was amended to raise these and WEAVE-SPEC 10.5 records it. The family
 *      now says the same thing about a bad formula whether it was typed into
 *      a cell or packed from a .WFX, which is what 10.5 asks for and what two
 *      wordings could not have given.
 *
 * The .WFX cell parser around it is LOOM's own, because 11.2's line format is
 * not a formula: a cell is a number, a quoted label or an `=`, and the two
 * packers have to disagree about none of the three.
 * ==========================================================================*/

/* The RPN buffer wfxc.c emits into (see 2, above). 256 is W_FXCMAX, which
 * that file sets from the widest formula a person can type into the bar. */
static unsigned char lm_fxbuf[256];
#define w_fxc_out lm_fxbuf

#include "../weave/wfxc.c"

/* --- the temporary cell table -------------------------------------------
 * Cells are parsed in FILE order and emitted in ROW-MAJOR order (2.14 rule 7,
 * and 2.14 rule 3c makes it the interning order too), so the parse pass sorts
 * as it inserts and the emit pass walks the sorted table. Both live in
 * LMW_CELLS: the parse record and the CELLS record are both eight bytes, and
 * the emit pass rewrites each slot in place, ascending, so a record is only
 * ever read before it is overwritten.
 *
 *   parse:  +0 row  +1 col  +2 tag  +3 0  +4 line   +6 rhs offset
 *   emit:   2.10's record
 */
#define LMS_T_NUM   1
#define LMS_T_LABEL 2
#define LMS_T_FORM  3

static unsigned lms_row(int i)
{
    return LMW_CELLS + (unsigned) i * 8;
}

/* --- the .WFX scanner ---------------------------------------------------- */
static unsigned lms_i;
static int      lms_line;

static unsigned lms_at(int slot, unsigned k)
{
    return lm_sb(slot, k);
}

/* One line of the .WFX, [lms_from, lms_to) with the ends already stripped.
 * 0 = EOF. The span comes back in statics rather than through pointers: SS !=
 * DS, so `&local` is a stack offset dereferenced through the package segment
 * (SPEC.md 73.5). */
static unsigned lms_from;
static unsigned lms_to;

static int lms_nextline(int slot)
{
    unsigned n = lm_srclen(slot);
    unsigned s, e;

    for (;;) {
        if (lms_i >= n)
            return 0;
        s = lms_i;
        e = s;
        while (e < n && lms_at(slot, e) != '\n')
            e++;
        lms_i = (e < n) ? e + 1 : n;
        lms_line++;
        while (s < e && lm_isspace((int) lms_at(slot, s)))
            s++;
        while (e > s && lm_isspace((int) lms_at(slot, e - 1)))
            e--;
        if (s == e)
            continue;               /* blank */
        if (lms_at(slot, s) == '#')
            continue;               /* 11.2's comment, which the demo uses */
        lms_from = s;
        lms_to = e;
        return 1;
    }
}

/* --- weavesim's parse_cellref, sentence for sentence --------------------- */
static int lms_r, lms_c;

static int lms_cellref(int slot, unsigned off, unsigned len, int line)
{
    int col;
    unsigned row = 0;
    unsigned k;

    if (len < 2 || len > 4 || !lm_isalpha((int) lms_at(slot, off)))
        goto notref;
    for (k = 1; k < len; k++)
        if (!lm_isdigit((int) lms_at(slot, off + k)))
            goto notref;
    col = lm_lower((int) lms_at(slot, off)) - 'a';
    for (k = 1; k < len; k++)
        row = row * 10 + (lms_at(slot, off + k) - '0');
    /* len <= 4, so row <= 999 and there is nothing here to overflow: the
     * cellref grammar is a letter and one to three digits (5.1). */
    if (row == 0 || col < 0 || col >= lm_gridcols
        || (int) row > lm_gridrows) {
        lmw_msg[0] = 0;
        {
            unsigned m = 0;
            for (k = 0; k < len && m + 1 < LM_ERRMAX; k++) {
                int ch = (int) lms_at(slot, off + k);
                lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, " is outside the ");
        lm_catn(lmw_msg, lm_gridcols);
        lm_cat(lmw_msg, "x");
        lm_catn(lmw_msg, lm_gridrows);
        lm_cat(lmw_msg, " grid");
        lm_perr(slot, line, lmw_msg);
        return 0;
    }
    lms_r = (int) row - 1;
    lms_c = col;
    return 1;

notref:
    lmw_msg[0] = 0;
    lm_cat(lmw_msg, "\"");
    {
        unsigned m = os88_strlen(lmw_msg);
        for (k = 0; k < len && m + 1 < LM_ERRMAX; k++) {
            lmw_msg[m] = (char) lms_at(slot, off + k);
            m++;
        }
        lmw_msg[m] = 0;
    }
    lm_cat(lmw_msg, "\" is not a cell reference (A1..");
    {
        unsigned m = os88_strlen(lmw_msg);
        lmw_msg[m] = (char) (64 + lm_gridcols);
        lmw_msg[m + 1] = 0;
    }
    lm_catn(lmw_msg, lm_gridrows);
    lm_cat(lmw_msg, ")");
    lm_perr(slot, line, lmw_msg);
    return 0;
}

/* --- weavesim's parse_number_16_16, sentence for sentence ---------------- */
static int lms_lo, lms_hi;

static int lms_number(int slot, unsigned off, unsigned len, int line)
{
    unsigned k = 0;
    int neg = 0;
    unsigned ip = 0;
    int ipover = 0;
    unsigned frac = 0;
    unsigned nd = 0;
    unsigned den;
    static const unsigned p10[5] = { 1, 10, 100, 1000, 10000 };

    if (len == 0)
        goto notnum;
    if (lms_at(slot, off) == '-') {
        neg = 1;
        k = 1;
    }
    if (k >= len || !lm_isdigit((int) lms_at(slot, off + k)))
        goto notnum;
    while (k < len && lm_isdigit((int) lms_at(slot, off + k))) {
        if (ip > 3276)
            ipover = 1;         /* past 5.1's |value| < 32768 either way */
        else
            ip = ip * 10 + (unsigned) (lms_at(slot, off + k) - '0');
        k++;
    }
    if (k < len) {
        if (lms_at(slot, off + k) != '.')
            goto notnum;
        k++;
        while (k < len && lm_isdigit((int) lms_at(slot, off + k))) {
            if (nd < 4)
                frac = frac * 10 + (unsigned) (lms_at(slot, off + k) - '0');
            nd++;
            k++;
        }
        if (k < len || nd == 0)
            goto notnum;
    }
    if (ipover || ip > 32767) {
        lmw_msg[0] = 0;
        {
            unsigned m = 0;
            unsigned q;
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lms_at(slot, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, ": |value| < 32768 (WEAVE-SPEC 5.1)");
        lm_perr(slot, line, lmw_msg);
        return 0;
    }
    if (nd > 4) {
        lmw_msg[0] = 0;
        {
            unsigned m = 0;
            unsigned q;
            for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                lmw_msg[m] = (char) lms_at(slot, off + q);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, ": at most 4 fraction digits; 16.16 resolves to "
               "1/65536 (WEAVE-SPEC 5.1)");
        lm_perr(slot, line, lmw_msg);
        return 0;
    }
    den = p10[nd];
    lms_hi = (int) ip;
    lms_lo = nd ? (int) wfx_frac(frac, den) : 0;
    if (neg) {                      /* a 32-bit negate in two words */
        lms_lo = -lms_lo;
        lms_hi = -lms_hi;
        if (lms_lo != 0)
            lms_hi--;
    }
    return 1;

notnum:
    lmw_msg[0] = 0;
    lm_cat(lmw_msg, "\"");
    {
        unsigned m = os88_strlen(lmw_msg);
        unsigned q;
        for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
            lmw_msg[m] = (char) lms_at(slot, off + q);
            m++;
        }
        lmw_msg[m] = 0;
    }
    lm_cat(lmw_msg, "\" is not a number");
    lm_perr(slot, line, lmw_msg);
    return 0;
}

/* --- the formula source, as a C string for wfxc.c ----------------------- */
static char lms_fsrc[256];

static int lms_copysrc(int slot, unsigned off, unsigned len, int line)
{
    unsigned k;

    if (len > 255) {
        lm_perr(slot, line, "formula: too long for one cell.");
        return 0;
    }
    for (k = 0; k < len; k++)
        lms_fsrc[k] = (char) lms_at(slot, off + k);
    lms_fsrc[len] = 0;
    return 1;
}

static unsigned ovl_formlen(int i)
{
    /* 2.9's offsets, plus one END MARK past the last - which is why
     * LM_MAXFORM is 255 and not 256: the table is nform+1 words in a
     * 512-byte region. */
    return lm_ww(LMW_FXOFF + (unsigned) (i + 1) * 2)
         - lm_ww(LMW_FXOFF + (unsigned) i * 2);
}

/* ==========================================================================
 * ovl_sheet - the .WFX (WEAVE-SPEC 11.2) into CELLS and FXCODE
 * ========================================================================*/

int ovl_sheet(void)
{
    int i;
    unsigned fxn = 0;

    lm_ncell = 0;
    lm_nform = 0;
    if (!lm_hasgrid)
        return 1;
    if (lm_srclen(LM_SLOT_WFX) == 0)
        return 1;                   /* a grid with no starting cells is legal */

    /* --- pass 1: parse into row-major order --- */
    lms_i = 0;
    lms_line = 0;
    while (lms_nextline(LM_SLOT_WFX)) {
        unsigned from = lms_from;
        unsigned to = lms_to;
        unsigned refto = from;
        unsigned rhs;
        int r = 0, c = 0;
        int line = lms_line;
        int k;
        int tag;

        while (refto < to && !lm_isspace((int) lms_at(LM_SLOT_WFX, refto))
               && lms_at(LM_SLOT_WFX, refto) != '=')
            refto++;
        rhs = refto;
        while (rhs < to && lm_isspace((int) lms_at(LM_SLOT_WFX, rhs)))
            rhs++;
        if (rhs >= to || lms_at(LM_SLOT_WFX, rhs) != '=') {
            lm_perr(LM_SLOT_WFX, line,
                    "<cellref> = <formula|number|\"label\">");
            return 0;
        }
        rhs++;
        while (rhs < to && lm_isspace((int) lms_at(LM_SLOT_WFX, rhs)))
            rhs++;
        if (rhs >= to) {
            lm_perr(LM_SLOT_WFX, line,
                    "<cellref> = <formula|number|\"label\">");
            return 0;
        }
        if (!lms_cellref(LM_SLOT_WFX, from, refto - from, line))
            return 0;
        r = lms_r;
        c = lms_c;
        if (lms_at(LM_SLOT_WFX, rhs) == '=')
            tag = LMS_T_FORM;
        else if (lms_at(LM_SLOT_WFX, rhs) == '"')
            tag = LMS_T_LABEL;
        else
            tag = LMS_T_NUM;
        /* insert, sorted row-major, and refuse a cell set twice */
        for (k = 0; k < lm_ncell; k++) {
            unsigned rr = lms_row(k);
            int kr = (int) lm_wb(rr);
            int kc = (int) lm_wb(rr + 1);
            if (kr == r && kc == c) {
                lmw_msg[0] = 0;
                {
                    unsigned m = 0;
                    unsigned q;
                    for (q = from; q < refto && m + 1 < LM_ERRMAX; q++) {
                        int ch = (int) lms_at(LM_SLOT_WFX, q);
                        lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z'
                                             ? ch - 32 : ch);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, ": set twice");
                lm_perr(LM_SLOT_WFX, line, lmw_msg);
                return 0;
            }
            if (kr > r || (kr == r && kc > c))
                break;
        }
        if (lm_ncell >= LM_MAXCELL) {
            lm_perr(LM_SLOT_WFX, line, "more than 384 starting cells; this "
                    "machine packs that many (WEAVE-SPEC 11.4)");
            return 0;
        }
        {
            int q;
            for (q = lm_ncell; q > k; q--) {
                unsigned d = lms_row(q);
                unsigned s = lms_row(q - 1);
                unsigned z;
                for (z = 0; z < 8; z++)
                    lm_wpb(d + z, lm_wb(s + z));
            }
        }
        {
            unsigned rr = lms_row(k);
            lm_wpb(rr, (unsigned) r);
            lm_wpb(rr + 1, (unsigned) c);
            lm_wpb(rr + 2, (unsigned) tag);
            lm_wpb(rr + 3, 0);
            lm_wpw(rr + 4, (unsigned) line);
            lm_wpw(rr + 6, rhs);
        }
        lm_ncell++;
    }

    /* --- pass 2: row-major, which is the interning and compiling order --- */
    for (i = 0; i < lm_ncell; i++) {
        unsigned rr = lms_row(i);
        int tag = (int) lm_wb(rr + 2);
        int line = (int) lm_ww(rr + 4);
        unsigned rhs = lm_ww(rr + 6);
        unsigned end = rhs;
        unsigned n = lm_srclen(LM_SLOT_WFX);
        int lo = 0, hi = 0;

        while (end < n && lms_at(LM_SLOT_WFX, end) != '\n')
            end++;
        while (end > rhs && lm_isspace((int) lms_at(LM_SLOT_WFX, end - 1)))
            end--;

        if (tag == LMS_T_NUM) {
            if (!lms_number(LM_SLOT_WFX, rhs, end - rhs, line))
                return 0;
            lo = lms_lo;
            hi = lms_hi;
            lm_wpb(rr + 2, 1);
            lm_wpb(rr + 3, 0);
            lm_wpw(rr + 4, (unsigned) lo & 0xFFFF);
            lm_wpw(rr + 6, (unsigned) hi & 0xFFFF);
        } else if (tag == LMS_T_LABEL) {
            unsigned k;
            int a;
            if (end - rhs < 3 || lms_at(LM_SLOT_WFX, end - 1) != '"') {
                lm_perr(LM_SLOT_WFX, line, "unterminated label");
                return 0;
            }
            lm_sbclear();
            for (k = rhs + 1; k + 1 < end; k++)
                lm_sbfold((int) lms_at(LM_SLOT_WFX, k));
            a = ovl_intern(LM_SLOT_WFX, line);
            if (a == 0)
                return 0;
            lm_wpb(rr + 2, 2);
            lm_wpb(rr + 3, 0);
            lm_wpw(rr + 4, (unsigned) a);
            lm_wpw(rr + 6, 0);
        } else {
            unsigned k;
            if (lm_nform >= LM_MAXFORM) {
                lm_perr(LM_SLOT_WFX, line, "more than 256 formulas; this "
                        "machine packs that many (WEAVE-SPEC 11.4)");
                return 0;
            }
            if (!lms_copysrc(LM_SLOT_WFX, rhs + 1, end - rhs - 1, line))
                return 0;
            if (!ovl_fxc(lms_fsrc, lm_gridcols, lm_gridrows)) {
                lmw_msg[0] = 0;
                lm_cat(lmw_msg, "formula: ");
                lm_cat(lmw_msg, w_fxc_err ? w_fxc_err : "cannot compile.");
                lm_perr(LM_SLOT_WFX, line, lmw_msg);
                return 0;
            }
            if (fxn + w_fxc_n > LM_FXMAX) {
                lm_perr(LM_SLOT_WFX, line, "the compiled formulas are over "
                        "4096 bytes; this machine packs that much "
                        "(WEAVE-SPEC 11.4)");
                return 0;
            }
            lm_wpw(LMW_FXOFF + (unsigned) lm_nform * 2, fxn);
            for (k = 0; k < w_fxc_n; k++)
                lm_wpb(LMW_FX + fxn + k, (unsigned) lm_fxbuf[k]);
            fxn += w_fxc_n;
            lm_wpb(rr + 2, 3);
            lm_wpb(rr + 3, 0);
            lm_wpw(rr + 4, (unsigned) lm_nform);
            lm_wpw(rr + 6, 0);
            lm_nform++;
        }
    }
    /* the end mark ovl_formlen() reads for the last formula's length */
    lm_wpw(LMW_FXOFF + (unsigned) lm_nform * 2, fxn);
    return 1;
}

/* ==========================================================================
 * ovl_sprites - the .WSP (WEAVE-SPEC 3.6) into SPRITES (WEAVE-SPEC 2.11)
 *
 * Every image ships beside its pre-built AND mask, both in FINAL SCREEN
 * POLARITY (1 = ink), widths multiples of 8, and the mask is NOT(coverage):
 * a background pixel inside the sprite's rectangle is transparent, not white.
 * ========================================================================*/

static int lms_nrow;                /* rows accumulated for the open frame */
static int lms_cur;                 /* the sprite being read, -1 = none */
static int lms_nframe;
static unsigned lms_rowoff[64];     /* each row's offset in the .WSP */
static unsigned lms_spb;            /* the SPRB write cursor */

static int lms_close_frame(int line)
{
    unsigned r;
    int h, wb;
    int y, b, k;

    if (lms_cur < 0)
        return 1;
    r = LMW_SPRD + (unsigned) lms_cur * LM_SPRDSZ;
    h = (int) lm_wb(r + LMS_HPX);
    wb = (int) lm_wb(r + LMS_WB);
    if (lms_nrow != h) {
        lmw_msg[0] = 0;
        lm_cat(lmw_msg, "sprite ");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned no = lm_ww(r + LMS_NAMEOFF);
            unsigned nl = lm_wb(r + LMS_NAMELEN);
            for (q = 0; q < nl && m + 1 < LM_ERRMAX; q++) {
                int ch = (int) lm_sb(LM_SLOT_WSP, no + q);
                lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, ": ");
        lm_catn(lmw_msg, lms_nrow);
        lm_cat(lmw_msg, " rows; declared ");
        lm_catn(lmw_msg, h);
        lm_perr(LM_SLOT_WSP, line, lmw_msg);
        return 0;
    }
    if (lms_spb + (unsigned) (h * wb * 2) > LM_SPRBMAX) {
        lm_perr(LM_SLOT_WSP, line, "the sprite art is over 6144 bytes; this "
                "machine packs that much (WEAVE-SPEC 11.4)");
        return 0;
    }
    for (y = 0; y < h; y++) {       /* the image plane, then the mask */
        for (b = 0; b < wb; b++) {
            unsigned ib = 0;
            for (k = 0; k < 8; k++)
                ib = (ib << 1)
                   | (lm_sb(LM_SLOT_WSP,
                            lms_rowoff[y] + (unsigned) (b * 8 + k)) == '#');
            lm_wpb(LMW_SPRB + lms_spb + (unsigned) (y * wb + b), ib);
        }
    }
    for (y = 0; y < h; y++) {
        for (b = 0; b < wb; b++) {
            unsigned mb = 0;
            for (k = 0; k < 8; k++)
                mb = (mb << 1)
                   | (lm_sb(LM_SLOT_WSP,
                            lms_rowoff[y] + (unsigned) (b * 8 + k)) != '#');
            lm_wpb(LMW_SPRB + lms_spb + (unsigned) (h * wb + y * wb + b), mb);
        }
    }
    lms_spb += (unsigned) (h * wb * 2);
    lm_wpw(r + LMS_LEN, lm_ww(r + LMS_LEN) + (unsigned) (h * wb * 2));
    lms_nframe++;
    lms_nrow = 0;
    return 1;
}

/* SPELLED ONCE, for SPEC.md 73.14's reason and lmwml.c's: a literal an
 * ovl_ function names is RESIDENT, and SmallerC emits one per SITE.
 * Four sites and three. */
static const char lm_s_wspline[] =
    "sprite <name> <w_px> <h_px> [<frames>]";
static const char lm_s_wspnum[] = "sprite: numbers expected";

static int lms_close_sprite(int line)
{
    unsigned r;

    if (lms_cur < 0)
        return 1;
    if (!lms_close_frame(line))
        return 0;
    r = LMW_SPRD + (unsigned) lms_cur * LM_SPRDSZ;
    if (lms_nframe != (int) lm_wb(r + LMS_FRAMES)) {
        lmw_msg[0] = 0;
        lm_cat(lmw_msg, "sprite ");
        {
            unsigned m = os88_strlen(lmw_msg);
            unsigned q;
            unsigned no = lm_ww(r + LMS_NAMEOFF);
            unsigned nl = lm_wb(r + LMS_NAMELEN);
            for (q = 0; q < nl && m + 1 < LM_ERRMAX; q++) {
                int ch = (int) lm_sb(LM_SLOT_WSP, no + q);
                lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                m++;
            }
            lmw_msg[m] = 0;
        }
        lm_cat(lmw_msg, ": ");
        lm_catn(lmw_msg, lms_nframe);
        lm_cat(lmw_msg, " frames; declared ");
        lm_catn(lmw_msg, (int) lm_wb(r + LMS_FRAMES));
        lm_perr(LM_SLOT_WSP, line, lmw_msg);
        return 0;
    }
    return 1;
}

/* The .WSP header line's word scanner. The cursor, the span and the parsed
 * number are all file-scope for SPEC.md 73.5's reason - one sprite line is
 * read at a time and nothing here nests. */
static unsigned lms_wp;             /* the cursor */
static unsigned lms_woff, lms_wlen;
static unsigned lms_int_v;

static int lms_word(unsigned to)
{
    while (lms_wp < to && lm_isspace((int) lm_sb(LM_SLOT_WSP, lms_wp)))
        lms_wp++;
    lms_woff = lms_wp;
    while (lms_wp < to && !lm_isspace((int) lm_sb(LM_SLOT_WSP, lms_wp)))
        lms_wp++;
    lms_wlen = lms_wp - lms_woff;
    return lms_wlen > 0;
}

/* 3.6 bounds every number on a sprite line at 64, so an accumulator that
 * STOPS at 1000 refuses everything the grammar refuses and needs no `long`
 * (SPEC.md 73.7 - SmallerC refuses the token). */
static int lms_int(unsigned off, unsigned len)
{
    unsigned k;
    unsigned v = 0;

    if (len == 0)
        return 0;
    for (k = 0; k < len; k++) {
        if (!lm_isdigit((int) lm_sb(LM_SLOT_WSP, off + k)))
            return 0;
        if (v > 1000)
            v = 1001;
        else
            v = v * 10 + (unsigned) (lm_sb(LM_SLOT_WSP, off + k) - '0');
    }
    lms_int_v = v;
    return 1;
}

static int ovl_wsp(void)
{
    unsigned n = lm_srclen(LM_SLOT_WSP);
    unsigned i = 0;
    int line = 0;

    lm_nspr_art = 0;
    lms_cur = -1;
    lms_nrow = 0;
    lms_nframe = 0;
    lms_spb = 0;
    /* The model splits the text on '\n' and rstrips each piece, so a file
     * ending in a newline yields one final empty line that is skipped and
     * `lineno` ends on it - which is the line close_sprite() names. */
    while (i < n) {
        unsigned s = i;
        unsigned e = i;

        while (e < n && lm_sb(LM_SLOT_WSP, e) != '\n')
            e++;
        i = e + 1;
        line++;
        while (e > s && lm_isspace((int) lm_sb(LM_SLOT_WSP, e - 1)))
            e--;                    /* rstrip, which is what the model does */
        if (s == e)
            continue;
        if (lm_srceq(LM_SLOT_WSP, s, 7, "sprite ")) {
            unsigned no, nl, o2, l2, o3, l3;
            unsigned w, h, nf = 1;
            int q;
            unsigned r;

            if (!lms_close_sprite(line))
                return 0;
            lms_wp = s + 6;
            if (!lms_word(e)) {
                lm_perr(LM_SLOT_WSP, line,
                        lm_s_wspline);
                return 0;
            }
            no = lms_woff;
            nl = lms_wlen;
            if (!lms_word(e)) {
                lm_perr(LM_SLOT_WSP, line,
                        lm_s_wspline);
                return 0;
            }
            o2 = lms_woff;
            l2 = lms_wlen;
            if (!lms_word(e)) {
                lm_perr(LM_SLOT_WSP, line,
                        lm_s_wspline);
                return 0;
            }
            o3 = lms_woff;
            l3 = lms_wlen;
            if (lms_word(e)) {
                unsigned o4 = lms_woff;
                unsigned l4 = lms_wlen;
                if (lms_word(e)) {
                    lm_perr(LM_SLOT_WSP, line,
                            lm_s_wspline);
                    return 0;
                }
                if (!lms_int(o4, l4)) {
                    lm_perr(LM_SLOT_WSP, line, lm_s_wspnum);
                    return 0;
                }
                nf = lms_int_v;
            }
            if (!lms_int(o2, l2)) {
                lm_perr(LM_SLOT_WSP, line, lm_s_wspnum);
                return 0;
            }
            w = lms_int_v;
            if (!lms_int(o3, l3)) {
                lm_perr(LM_SLOT_WSP, line, lm_s_wspnum);
                return 0;
            }
            h = lms_int_v;
            if ((w % 8) || w < 8 || w > 64 || h < 1 || h > 64
                || nf < 1 || nf > 8) {
                lmw_msg[0] = 0;
                lm_cat(lmw_msg, "sprite ");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned k;
                    for (k = 0; k < nl && m + 1 < LM_ERRMAX; k++) {
                        int ch = (int) lm_sb(LM_SLOT_WSP, no + k);
                        lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z'
                                             ? ch - 32 : ch);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, ": w a multiple of 8 in 8..64, h 1..64, "
                       "frames 1..8 (WEAVE-SPEC 3.6)");
                lm_perr(LM_SLOT_WSP, line, lmw_msg);
                return 0;
            }
            for (q = 0; q < lm_nspr_art; q++) {
                unsigned rq = LMW_SPRD + (unsigned) q * LM_SPRDSZ;
                unsigned qo = lm_ww(rq + LMS_NAMEOFF);
                unsigned ql = lm_wb(rq + LMS_NAMELEN);
                unsigned k;
                if (ql != nl)
                    continue;
                for (k = 0; k < nl; k++)
                    if (lm_lower((int) lm_sb(LM_SLOT_WSP, qo + k))
                        != lm_lower((int) lm_sb(LM_SLOT_WSP, no + k)))
                        break;
                if (k == nl) {
                    lmw_msg[0] = 0;
                    lm_cat(lmw_msg, "sprite ");
                    {
                        unsigned m = os88_strlen(lmw_msg);
                        for (k = 0; k < nl && m + 1 < LM_ERRMAX; k++) {
                            int ch = (int) lm_sb(LM_SLOT_WSP, no + k);
                            lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z'
                                                 ? ch - 32 : ch);
                            m++;
                        }
                        lmw_msg[m] = 0;
                    }
                    lm_cat(lmw_msg, ": defined twice");
                    lm_perr(LM_SLOT_WSP, line, lmw_msg);
                    return 0;
                }
            }
            if (lm_nspr_art >= LM_MAXSPR) {
                lmw_msg[0] = 0;
                lm_catn(lmw_msg, lm_nspr_art + 1);
                lm_cat(lmw_msg, " sprites; the section count byte caps at 16 "
                       "(WEAVE-SPEC 2.11)");
                lm_perr(LM_SLOT_WSP, line, lmw_msg);
                return 0;
            }
            r = LMW_SPRD + (unsigned) lm_nspr_art * LM_SPRDSZ;
            lm_wfill(r, 0, LM_SPRDSZ);
            lm_wpw(r + LMS_NAMEOFF, no);
            lm_wpb(r + LMS_NAMELEN, nl);
            lm_wpb(r + LMS_WB, (unsigned) (w / 8));
            lm_wpb(r + LMS_HPX, (unsigned) h);
            lm_wpb(r + LMS_FRAMES, (unsigned) nf);
            lm_wpw(r + LMS_DATA, lms_spb);
            lm_wpw(r + LMS_LEN, 0);
            lms_cur = lm_nspr_art;
            lms_nframe = 0;
            lms_nrow = 0;
            lm_nspr_art++;
            continue;
        }
        if (e - s == 1 && lm_sb(LM_SLOT_WSP, s) == '-') {
            if (!lms_close_frame(line))
                return 0;
            continue;
        }
        if (lms_cur < 0) {
            lm_perr(LM_SLOT_WSP, line, "art before any sprite line");
            return 0;
        }
        {
            unsigned r = LMW_SPRD + (unsigned) lms_cur * LM_SPRDSZ;
            int w = (int) lm_wb(r + LMS_WB) * 8;
            unsigned k;
            int bad = ((int) (e - s) != w);
            for (k = s; !bad && k < e; k++) {
                int ch = (int) lm_sb(LM_SLOT_WSP, k);
                if (ch != '#' && ch != '.')
                    bad = 1;
            }
            if (bad) {
                lmw_msg[0] = 0;
                lm_cat(lmw_msg, "sprite ");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned no = lm_ww(r + LMS_NAMEOFF);
                    unsigned nl = lm_wb(r + LMS_NAMELEN);
                    for (k = 0; k < nl && m + 1 < LM_ERRMAX; k++) {
                        int ch = (int) lm_sb(LM_SLOT_WSP, no + k);
                        lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z'
                                             ? ch - 32 : ch);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, ": a row is exactly ");
                lm_catn(lmw_msg, w);
                lm_cat(lmw_msg, " of '#' and '.'");
                lm_perr(LM_SLOT_WSP, line, lmw_msg);
                return 0;
            }
            if (lms_nrow >= 64) {
                lm_perr(LM_SLOT_WSP, line, "a sprite frame is 64 rows at "
                        "most (WEAVE-SPEC 3.6)");
                return 0;
            }
            lms_rowoff[lms_nrow] = s;
            lms_nrow++;
        }
    }
    return lms_close_sprite(line);
}

int ovl_sprites(void)
{
    int i;
    int first = -1;

    lm_nspr_art = 0;
    for (i = 0; i < lm_ncomp; i++)
        if (lm_wb(LMW_COMPS + (unsigned) i * LM_COMPSZ + LMC_CTYPE)
            == WC_SPRITE && first < 0)
            first = i;
    if (first < 0)
        return 1;
    if (lm_srclen(LM_SLOT_WSP) == 0) {
        lm_perr(LM_SLOT_WML,
                (int) lm_ww(LMW_COMPS + (unsigned) first * LM_COMPSZ
                            + LMC_LINE),
                "sprites declared and no .WSP art file beside the .WML");
        return 0;
    }
    if (!ovl_wsp())
        return 0;

    /* 3.3: `img` names a .WSP sprite; the PK_SPRITE record lmwml.c emitted
     * with a 0 placeholder becomes that index here. */
    for (i = 0; i < lm_ncomp; i++) {
        unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
        unsigned imgoff;
        unsigned imglen;
        int np, k;
        unsigned pr;
        int found = -1;
        int q;

        if (lm_wb(r + LMC_CTYPE) != WC_SPRITE)
            continue;
        imgoff = lm_ww(r + LMC_AUXOFF);
        imglen = (unsigned) lm_wb(r + LMC_AUX);
        for (q = 0; q < lm_nspr_art; q++) {
            unsigned rq = LMW_SPRD + (unsigned) q * LM_SPRDSZ;
            unsigned qo = lm_ww(rq + LMS_NAMEOFF);
            unsigned ql = lm_wb(rq + LMS_NAMELEN);
            unsigned z;
            if (ql != imglen)
                continue;
            for (z = 0; z < ql; z++)
                if (lm_lower((int) lm_sb(LM_SLOT_WSP, qo + z))
                    != lm_lower((int) lm_wb(LMW_NAMES + imgoff + z)))
                    break;
            if (z == ql) {
                found = q;
                break;
            }
        }
        if (found < 0) {
            lmw_msg[0] = 0;
            lm_cat(lmw_msg, "sprite: img=\"");
            {
                unsigned m = os88_strlen(lmw_msg);
                unsigned z;
                for (z = 0; z < imglen && m + 1 < LM_ERRMAX; z++) {
                    int ch = (int) lm_wb(LMW_NAMES + imgoff + z);
                    lmw_msg[m] = (char) (ch >= 'a' && ch <= 'z' ? ch - 32
                                                                : ch);
                    m++;
                }
                lmw_msg[m] = 0;
            }
            lm_cat(lmw_msg, "\" is not in the .WSP file");
            lm_perr(LM_SLOT_WML, (int) lm_ww(r + LMC_LINE), lmw_msg);
            return 0;
        }
        np = (int) lm_wb(r + LMC_NPROP);
        pr = lm_ww(r + LMC_PROP);
        for (k = 0; k < np; k++) {
            unsigned pp = LMW_PROPS + (pr + (unsigned) k) * LM_PROPSZ;
            if (lm_wb(pp + LMP_KIND) == PK_SPRITE)
                lm_wpw(pp + LMP_VAL, (unsigned) found);
        }
    }
    return 1;
}
