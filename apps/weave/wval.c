/* ============================================================================
 * os8088 - apps/weave/wval.c
 *
 * THE HOSTILE-BUNDLE READER (WEAVE-SPEC 2, 10.4). #included by
 * apps/weave/weave.c; one translation unit (SPEC.md 73.1).
 *
 * EVERY BYTE READ OFF A DISK IS HOSTILE (SPEC.md 19), and a `.WAB` on a disk
 * need never have been through a packer at all - so nothing here may reason
 * "the packer would not have written that". WEAVE-SPEC 10.4 lists what refuses
 * and names the sentence:
 *
 *     <NAME>.WAB is not a Weave bundle (<field>).
 *
 * with the field named, and the runtime never guesses past a bad header.
 *
 * THE ORDER IS THE POINT. Nothing is believed before the thing that bounds it:
 * the header before the section table, the table before any section, the atom
 * pool before any atom id, the function count before any PK_FUNC. Every
 * routine below returns 0 for "valid" or the field name for the sentence, and
 * every caller stops at the first refusal.
 *
 * STRICTNESS IS SET BY tests/unit/t_wab.py, not by tools/weavesim.py.
 * WEAVE-SPEC 12.2 makes t_wab the independent second reader, written from the
 * contract and sharing no code with any packer, and it is stricter than the
 * model in several places (packed atom offsets, no tail padding, section
 * packing, the flag/section couplings). The runtime is at least as strict as
 * t_wab; where only t_wab checks something, the comment says so.
 *
 * THE HEADER IS VALIDATED SOMEWHERE ELSE FROM EVERYTHING ELSE, and that is
 * structural rather than untidy: WEAVE-SPEC 10.1 requires the memory refusal
 * to happen BEFORE the bundle is read, so the header is read out of a
 * one-cluster probe into this segment (w_probe[]) and the rest out of the
 * claim once there is one. ovl_val_header() therefore reads a C array and every
 * other routine here reads the claim through wblob.inc's accessors.
 * ==========================================================================*/

/* --- THE FIELD NAMES, EACH SPELLED ONCE (WEAVE-SPEC 10.4) ---------------- */
/* Every routine below answers with the name of the field that refused, and
 * `section table` alone is the answer at eighteen sites. A C string literal
 * is emitted once PER SITE by this toolchain - it is not pooled - and a
 * literal named by an ovl_ function is RESIDENT even though the code that
 * names it is not (SPEC.md 73.14: the code moves, the literal stays). So the
 * eighteen copies were 238 bytes of the package image, against SPEC.md 20.1s
 * 61,440 for image and bss together, and buying nothing: the sentence is the
 * same sentence. Named once here, they cost their own length and one word per
 * site, which is what the `return` already cost.
 *
 * THE TEXT IS THE CONTRACT. WEAVE-SPEC 10.4 names each of these in the
 * refusal it prescribes, so a name changed here changes a sentence the spec
 * pins - change 10.4 first. */
static const char w_f_sectab   [] = "section table";
static const char w_f_props    [] = "prop block";
static const char w_f_prange   [] = "property range";
static const char w_f_atoms    [] = "atom pool";
static const char w_f_ftab     [] = "function table";
static const char w_f_reqprop  [] = "required property";
static const char w_f_atomid   [] = "atom id";
static const char w_f_reckind  [] = "record kind";
static const char w_f_sprcount [] = "sprite count";
static const char w_f_cellrec  [] = "cell record";
static const char w_f_cardidx  [] = "card index";
static const char w_f_propkind [] = "prop kind";
static const char w_f_header   [] = "header";
static const char w_f_fxcount  [] = "formula count";
static const char w_f_recend   [] = "REC_END";
static const char w_f_total    [] = "total size";
static const char w_f_appname  [] = "app name";
static const char w_f_sprdesc  [] = "sprite descriptor";
static const char w_f_magic    [] = "magic";
static const char w_f_flags    [] = "flags";
static const char w_f_ctype    [] = "ctype";
static const char w_f_entry    [] = "entry card";
static const char w_f_style    [] = "style byte";
static const char w_f_compid   [] = "comp_id";

/* --- the header, off the probe (WEAVE-SPEC 2.2) ------------------------- */

/* ovl_val_header - the 32 bytes, every field ranged, from the probe buffer.
 *
 * `w_fsize` is the size the DIRECTORY entry gave, which is what the memory
 * refusal was going to be computed from - so the total-size word is checked
 * against it here rather than against the bytes read. A bundle whose header
 * understates its file is the case that would let the refusal be computed
 * from one number and the read cost paid for another. */
static const char *ovl_val_header_x(void)
{
    unsigned t;

    if (w_probelen < W_HDR_SIZE)
        return w_f_magic;               /* nothing to look at: not a bundle */
    if (w_probe[0] != 'W' || w_probe[1] != 'A' || w_probe[2] != 'B' ||
        w_probe[3] != 0x1A)
        return w_f_magic;

    if (w_probe[W_H_VERSION] != 1 || w_probe[W_H_VERSION + 1] != 0)
        return "version";               /* 2.2: any other value refuses */

    t = w_probe[W_H_TOTAL] | (w_probe[W_H_TOTAL + 1] << 8);
    if (t != w_fsize || t > W_CAP || t < W_HDR_SIZE)
        return w_f_total;
    w_size = t;

    w_flags = w_probe[W_H_FLAGS] | (w_probe[W_H_FLAGS + 1] << 8);
    if (w_flags & ~WABF_KNOWN)
        return w_f_flags;               /* 2.2.1: a set unknown bit refuses */

    w_vmkb = w_probe[W_H_VMKB];
    if (w_vmkb < 16 || w_vmkb > 32)
        return w_f_header;
    w_gridkb = w_probe[W_H_GRIDKB];
    if (w_gridkb != 0 && (w_gridkb < 8 || w_gridkb > 26))
        return w_f_header;
    w_canvaskb = w_probe[W_H_CANVASKB];
    if (w_canvaskb != 0 && (w_canvaskb < 2 || w_canvaskb > 8))
        return w_f_header;
    if (w_probe[W_H_RSVD] != 0)
        return w_f_header;

    /* 2.2 tabulates the section count as 1..9 and 2.4 leaves no bundle with
     * fewer than five: UISTREAM, PROPS, CODE and ATOMS are mandatory and ICON
     * is always present, so those five rows are named by the format itself.
     * A count below five is a header the format cannot produce. */
    w_nsec = w_probe[W_H_NSEC];
    if (w_nsec < 5 || w_nsec > 9)
        return w_f_header;

    w_entry = w_probe[W_H_ENTRY];
    if (w_entry < 1 || w_entry > W_MAXCARD)
        return w_f_entry;

    /* 2.2: 15 characters then a NUL, printable before it, 0x00 after. Space
     * padding before the NUL is explicitly not allowed, which is why the
     * bytes after it are checked rather than trimmed. */
    for (t = 0; t < 16; t++)
        if (w_probe[W_H_NAME + t] == 0)
            break;
    if (t < 1 || t > 15)
        return w_f_appname;
    w_applen = t;
    for (t = 0; t < w_applen; t++)
        if (w_probe[W_H_NAME + t] < 0x20 || w_probe[W_H_NAME + t] > 0x7E)
            return w_f_appname;
    for (t = w_applen; t < 16; t++)
        if (w_probe[W_H_NAME + t] != 0)
            return w_f_appname;

    return 0;
}

/* --- the section table (WEAVE-SPEC 2.3) --------------------------------- */

static unsigned w_align16(unsigned n)
{
    return (n + 15) & 0xFFF0;
}

/* ovl_val_sections - the table, then the couplings the flags word implies.
 *
 * Sections are checked for EXACT packing, not just for bounds: 2.3 says each
 * begins at align16(previous offset + length) and the file ends at the last
 * section's unpadded end, and the padding between them is 0x00 (2.1). A gap
 * is a different file than the contract describes and t_wab refuses it, so
 * this does too - bytes the format does not account for are exactly where a
 * hostile bundle would put something. */
static const char *ovl_val_sections(void)
{
    unsigned i, row, t, off, len, extra, prev, expect;

    for (i = 0; i < W_NSECTYPE; i++) {
        w_soff[i] = 0;
        w_slen[i] = 0;
        w_sextra[i] = 0;
        w_shave[i] = 0;
    }

    if (W_HDR_SIZE + W_ROW_SIZE * w_nsec > w_size)
        return w_f_sectab;

    prev = 0;
    expect = w_align16(W_HDR_SIZE + W_ROW_SIZE * w_nsec);
    for (i = 0; i < w_nsec; i++) {
        row = W_HDR_SIZE + W_ROW_SIZE * i;
        t = w_b(w_seg, row);
        if (t < W_UISTREAM || t > W_SOURCE)
            return w_f_sectab;
        if (w_b(w_seg, row + 1) != 0)
            return w_f_sectab;
        if (t <= prev)                  /* strictly ascending: one row per type */
            return w_f_sectab;
        prev = t;
        off = w_w(w_seg, row + 2);
        len = w_w(w_seg, row + 4);
        extra = w_w(w_seg, row + 6);
        if (off & 15)
            return w_f_sectab;
        if (off != expect)
            return w_f_sectab;
        if (off > w_size || len > w_size - off)
            return w_f_sectab;
        w_soff[t] = off;
        w_slen[t] = len;
        w_sextra[t] = extra;
        w_shave[t] = 1;
        expect = w_align16(off + len);
        /* 2.1: the padding between two sections is 0x00. `off + len` to the
         * next section's start is at most 15 bytes. */
        if (i + 1 < w_nsec && !w_zero(w_seg, off + len, expect - (off + len)))
            return w_f_sectab;
        w_lastend = off + len;
    }
    /* 2.3: the file ends at the last section's unpadded end - no tail
     * padding follows it. (t_wab pins this; the model does not.) */
    if (w_lastend != w_size)
        return w_f_sectab;
    /* ...and the gap between the table and the first section, if any. */
    off = W_HDR_SIZE + W_ROW_SIZE * w_nsec;
    if (!w_zero(w_seg, off, w_align16(off) - off))
        return w_f_sectab;

    /* 2.4: the mandatory four, and ICON always. */
    if (!w_shave[W_UISTREAM] || !w_shave[W_PROPS] || !w_shave[W_CODE] ||
        !w_shave[W_ATOMS] || !w_shave[W_ICON])
        return w_f_sectab;
    if (w_slen[W_ICON] != 64 || w_sextra[W_ICON] != 0)
        return w_f_sectab;
    if (w_sextra[W_CODE] != 0 || w_sextra[W_ATOMS] != 0)
        return w_f_sectab;

    /* The flag/section couplings (t_wab's, entirely). Each of these is a
     * bundle claiming one thing in its header and carrying another, which is
     * how a reader is talked into indexing a section that is not there. */
    if (w_shave[W_FXCODE] != ((w_flags & WABF_GRID) != 0) ||
        w_shave[W_CELLS] != ((w_flags & WABF_GRID) != 0) ||
        (w_gridkb != 0) != ((w_flags & WABF_GRID) != 0))
        return w_f_sectab;
    if ((w_canvaskb != 0) != ((w_flags & WABF_CANVAS) != 0))
        return w_f_sectab;
    if (w_shave[W_SOURCE] != ((w_flags & WABF_SOURCE) != 0))
        return w_f_sectab;
    if (w_shave[W_SPRITES] && !(w_flags & WABF_CANVAS))
        return w_f_sectab;              /* sprites live inside a canvas (2.11) */
    if (w_shave[W_SOURCE] && w_sextra[W_SOURCE] > w_slen[W_SOURCE])
        return w_f_sectab;              /* 2.13: extra is the WML length */
    return 0;
}

/* --- the atom pool (WEAVE-SPEC 2.7) ------------------------------------- */

/* ovl_val_atoms - the pool walked end to end, and it must END exactly.
 *
 * Ids 1..63 are well-known and are NOT stored here; row i is atom 64 + i. The
 * offsets are checked against the PACKED positions rather than merely for
 * bounds (t_wab's rule): strings pack without gaps, so an offset that is
 * merely in range names bytes the format does not account for. */
static const char *ovl_val_atoms(void)
{
    unsigned s, len, i, count, off, pos, l;

    s = w_soff[W_ATOMS];
    len = w_slen[W_ATOMS];
    if (len < 2)
        return w_f_atoms;
    count = w_w(w_seg, s);
    if (count > 187)                    /* 2.7: ids 64..250 */
        return w_f_atomid;
    w_natoms = count;
    if (2 + 2 * count > len)
        return w_f_atoms;

    pos = 2 + 2 * count;                /* 2.7: atom 64 begins here */
    for (i = 0; i < count; i++) {
        off = w_w(w_seg, s + 2 + 2 * i);
        if (off != pos)
            return w_f_atoms;
        if (off + 1 > len)
            return w_f_atoms;
        l = w_b(w_seg, s + off);
        if (l < 1)
            return w_f_atoms;
        if (off + 1 + l + 1 > len)
            return w_f_atoms;
        if (!w_print(w_seg, s + off + 1, l))
            return w_f_atoms;           /* 3.1's fold admits 0x20..0x7E only */
        if (w_b(w_seg, s + off + 1 + l) != 0)
            return w_f_atoms;           /* length byte and NUL must agree */
        pos = off + 1 + l + 1;
    }
    if (pos != len)
        return w_f_atoms;               /* no trailing bytes: the section
                                         * length is part of the format */
    return 0;
}

/* w_atom_ok - is `a` an atom this bundle can name?  0 is "none" and is the
 * caller's business, so it is refused here. */
static int w_atom_ok(unsigned a)
{
    if (a >= 1 && a <= 63)
        return 1;                       /* well-known (2.7.1) */
    return a >= WA_APP_FIRST && a < WA_APP_FIRST + w_natoms;
}

/* w_atom_len / w_atom_off - an app atom's bytes in the claim.  THE BODIES ARE
 * IN apps/weave/watom.c, #included here at the point they used to stand:
 * LOOM's Preview module needs the pair without the rest of this reader
 * (WEAVE-SPEC 1.2.4), and WEAVE-SPEC 1.2's rule is that what the two packages
 * share they share as source. That file's header says the rest. */
#include "watom.c"

/* --- CODE (WEAVE-SPEC 2.8) ---------------------------------------------- */

/* ovl_val_code - the function table only.
 *
 * The BYTECODE is not walked here and that is deliberate: nothing in wave 2
 * executes an opcode, and a jump-target walk that no interpreter follows would
 * be a second, unexercised reader of the same bytes. The WVM lands with its
 * own walk (WEAVE-SPEC 4.5, and t_wab's walk_function() is the shape). What
 * IS checked is everything an offset or a count could make this file index
 * out of bounds with. */
static const char *ovl_val_code(void)
{
    unsigned s, len, i, f, g, ofs, na, nl;

    s = w_soff[W_CODE];
    len = w_slen[W_CODE];
    if (len < 3)
        return w_f_ftab;
    f = w_b(w_seg, s);
    g = w_b(w_seg, s + 1);
    if (f > 128 || g > 128)
        return w_f_ftab;
    w_nfunc = f;
    if (2 + 4 * f > len)
        return w_f_ftab;
    for (i = 0; i < f; i++) {
        ofs = w_w(w_seg, s + 2 + 4 * i);
        na = w_b(w_seg, s + 2 + 4 * i + 2);
        nl = w_b(w_seg, s + 2 + 4 * i + 3);
        if (ofs < 2 + 4 * f || ofs >= len)
            return w_f_ftab;
        if (na > 8 || nl > 16 || nl < na)
            return w_f_ftab;
    }
    /* 2.8: a scriptless bundle still carries the section, and its body is
     * exactly one HALT byte - so the length is exactly 3. (t_wab's.) */
    if (f == 0 && len != 3)
        return w_f_ftab;
    return 0;
}

/* --- PROPS (WEAVE-SPEC 2.6) --------------------------------------------- */

/* Which of the properties a component is REQUIRED to carry have been seen,
 * and the values whose ranges 10.4 bounds. Statics, because there is one
 * block in flight at a time and because an out-parameter is an address (the
 * SDK's rule 1). */
#define WP_COLS   0x01
#define WP_ROWS   0x02
#define WP_GROUP  0x04
static unsigned w_pv_seen;
static unsigned w_pv_cols, w_pv_rows;

/* ovl_val_block - one property block, for the component of type `ctype`
 * (0 = the app block, which 2.6.2 bounds separately).
 *
 * Records are 4 bytes and a block ends at four zero bytes; names ascend
 * strictly and appear at most once. 10.4 is explicit that a property outside
 * its range is MALFORMED and never clamped, and that a required property
 * absent is malformed and never defaulted - "a bundle missing one lays the
 * card out around a number nobody wrote". */
static const char *ovl_val_block(unsigned off, unsigned ctype)
{
    unsigned base, len, name, kind, val, prev, blob, n, i;

    base = w_soff[W_PROPS];
    len = w_slen[W_PROPS];
    w_pv_seen = 0;
    w_pv_cols = 0;
    w_pv_rows = 0;
    if (off == W_NOPROPS)
        return 0;
    prev = 0;
    for (;;) {
        if (off + 4 > len)
            return w_f_props;
        name = w_b(w_seg, base + off);
        kind = w_b(w_seg, base + off + 1);
        val = w_w(w_seg, base + off + 2);
        if (name == 0) {                /* the terminator is FOUR zero bytes */
            if (kind != 0 || val != 0)
                return w_f_props;
            return 0;
        }
        if (name <= prev)
            return w_f_props;           /* 2.6: sorted, and at most once */
        prev = name;
        if (!w_atom_ok(name))
            return w_f_atomid;
        if (kind > PK_MAX)
            return w_f_propkind;

        switch (kind) {
        case PK_ATOM:
            if (!w_atom_ok(val & 0xFF) || (val >> 8) != 0)
                return w_f_atomid;
            break;
        case PK_BLOB:
            if (val >= len)
                return w_f_props;
            break;
        case PK_FUNC:
            if (val >= w_nfunc)
                return w_f_props;
            break;
        case PK_SPRITE:
            if (val >= w_nsprite)
                return w_f_props;
            break;
        default:
            break;                      /* PK_INT: any word is a legal word */
        }

        /* An event binding is an ordinary record whose name is an event atom
         * and whose kind is PK_FUNC (2.6). The pairing is part of the format,
         * so a bundle that binds an event to an integer is malformed. */
        if (name >= 48 && name <= 60 && kind != PK_FUNC)
            return w_f_propkind;
        if (name == WA_MENUS && ctype != 0)
            return w_f_props;           /* MENUS lives in the app block only */
        if (name == WA_ITEMS && ctype != WC_LIST)
            return w_f_props;
        if (name == WA_ITEMS) {
            if (kind != PK_BLOB)
                return w_f_propkind;
            blob = val;
            if (blob + 1 > len)
                return w_f_props;
            n = w_b(w_seg, base + blob);
            if (n > 64 || blob + 1 + n > len)
                return w_f_props;       /* 2.6.1: at most 64 items */
            for (i = 0; i < n; i++)
                if (!w_atom_ok(w_b(w_seg, base + blob + 1 + i)))
                    return w_f_atomid;
        }

        /* 10.4's `property range`, checked as the block is read and before any
         * of it reaches the walk. */
        if (ctype == WC_METER && name == WA_MAX &&
            (val < 1 || val > 32000))
            return w_f_prange;
        if (ctype == WC_INPUT && name == WA_COLS &&
            (val < 2 || val > 60))
            return w_f_prange;
        if (ctype == WC_LIST && name == WA_ROWS && (val < 1 || val > 40))
            return w_f_prange;
        if (ctype == WC_GRID && name == WA_COLS) {
            if (val < 1 || val > 26)
                return w_f_prange;
            w_pv_cols = val;
            w_pv_seen |= WP_COLS;
        }
        if (ctype == WC_GRID && name == WA_ROWS) {
            if (val < 1 || val > 256)
                return w_f_prange;
            w_pv_rows = val;
            w_pv_seen |= WP_ROWS;
        }
        if (name >= WA_COLOR && name <= WA_PAPER && val > 15)
            return w_f_prange;          /* 6.10.7's palette is 0..15; the load
                                         * path masks as well, but a colour
                                         * outside the sixteen is a bundle no
                                         * packer wrote and 10.4 says so out
                                         * loud rather than quietly clamping */
        if (name == WA_GROUP)
            w_pv_seen |= WP_GROUP;

        off += 4;
    }
}

/* ovl_val_app - the app block (2.6.2): CARD, MENUS and `start`, and nothing
 * else may appear in it. */
static const char *ovl_val_app(void)
{
    unsigned base, len, off, name, kind, val, blob, nm, ni, i, j, fn;
    const char *e;

    base = w_soff[W_PROPS];
    len = w_slen[W_PROPS];
    off = w_sextra[W_PROPS];
    if (off >= len)
        return w_f_props;
    e = ovl_val_block(off, 0);
    if (e)
        return e;
    for (;;) {
        if (off + 4 > len)
            return w_f_props;
        name = w_b(w_seg, base + off);
        if (name == 0)
            break;
        kind = w_b(w_seg, base + off + 1);
        val = w_w(w_seg, base + off + 2);
        if (name != WA_CARD && name != WA_START && name != WA_MENUS)
            return w_f_props;
        if (name == WA_CARD && (kind != PK_INT || val != w_entry))
            return w_f_props;           /* 2.6.2: a mirror of the header's */
        if (name == WA_START && (kind != PK_FUNC || val + 1 != w_nfunc))
            return w_f_props;           /* ...and it names the LAST function */
        if (name == WA_MENUS) {
            if (kind != PK_BLOB)
                return w_f_propkind;
            blob = val;
            if (blob + 1 > len)
                return w_f_props;
            nm = w_b(w_seg, base + blob);
            if (nm < 1 || nm > 5)       /* MENU_APPMAX is the kernel's own
                                         * bound (SPEC.md 12.2) */
                return w_f_props;
            j = blob + 1;
            for (i = 0; i < nm; i++) {
                if (j + 2 > len)
                    return w_f_props;
                if (!w_atom_ok(w_b(w_seg, base + j)))
                    return w_f_atomid;
                ni = w_b(w_seg, base + j + 1);
                if (ni < 1 || ni > 8)
                    return w_f_props;
                j += 2;
                if (j + 2 * ni > len)
                    return w_f_props;
                for (fn = 0; fn < ni; fn++) {
                    if (!w_atom_ok(w_b(w_seg, base + j + 2 * fn)))
                        return w_f_atomid;
                    val = w_b(w_seg, base + j + 2 * fn + 1);
                    if (val != 0xFF && val >= w_nfunc)
                        return w_f_props;
                }
                j += 2 * ni;
            }
        }
        off += 4;
    }
    return 0;
}

/* --- UISTREAM (WEAVE-SPEC 2.5), and every component's block with it ------ */

static const char *ovl_val_uistream(void)
{
    unsigned s, len, n, i, rec, kind, id, ctype, style, cflags, props, w, h;
    unsigned cards, ended, grids, canvases, lastct;
    const char *e;

    s = w_soff[W_UISTREAM];
    len = w_slen[W_UISTREAM];
    n = w_sextra[W_UISTREAM];
    if (len != W_REC_SIZE * n || n == 0)
        return "record count";

    cards = 0;
    ended = 0;
    grids = 0;
    canvases = 0;
    lastct = 0;
    w_ncomp = 0;

    for (i = 0; i < n; i++) {
        rec = s + W_REC_SIZE * i;
        kind = w_b(w_seg, rec);
        if (ended)
            return w_f_recend;          /* exactly one, and it is last */
        if (kind == W_REC_END) {
            if (!w_zero(w_seg, rec + 1, W_REC_SIZE - 1))
                return w_f_recend;
            if (i + 1 != n)
                return w_f_recend;
            ended = 1;
            continue;
        }
        if (kind == W_REC_CARD) {
            id = w_b(w_seg, rec + W_R_ID);
            if (id != cards + 1 || id > W_MAXCARD)
                return w_f_cardidx;
            if (!w_zero(w_seg, rec + 2, 6))
                return w_f_cardidx;     /* 2.5: bytes +2..+7 are 0 (t_wab) */
            if (w_w(w_seg, rec + W_R_PROPS) != W_NOPROPS)
                return w_f_cardidx;     /* v1 cards carry no props */
            cards++;
            lastct = 0;
            continue;
        }
        if (kind != W_REC_COMP)
            return w_f_reckind;

        if (cards == 0)
            return "component before any card";
        id = w_b(w_seg, rec + W_R_ID);
        ctype = w_b(w_seg, rec + W_R_CTYPE);
        w = w_b(w_seg, rec + W_R_W);
        h = w_b(w_seg, rec + W_R_H);
        style = w_b(w_seg, rec + W_R_STYLE);
        cflags = w_b(w_seg, rec + W_R_CFLAGS);
        props = w_w(w_seg, rec + W_R_PROPS);

        if (id < 1 || id > 250)
            return w_f_compid;
        if (id != w_ncomp + 1)
            return w_f_compid;          /* unique across the bundle AND in
                                         * document order (2.5, 2.14 rule 2;
                                         * t_wab pins it). ONE test does both:
                                         * ids that are exactly 1..n in order
                                         * cannot repeat, so the 256-byte
                                         * seen[] that used to stand here was
                                         * dead weight refusing the same
                                         * bundles with the same word */
        w_ncomp++;
        if (ctype < WC_LABEL || ctype > WC_MAX)
            return w_f_ctype;           /* 0x0F+ is unassigned (2.5.1) */
        if (style & ~WS_KNOWN)
            return w_f_style;
        if (((style & WS_ALIGN) >> WS_ALIGNSH) == 3)
            return w_f_style;           /* ALIGN 3 is refused by the packer */
        if (cflags & ~CF_KNOWN)
            return "cflags";
        if (w_b(w_seg, rec + 7) != 0)
            return w_f_reckind;         /* 2.5: byte +7 is 0 */

        if (ctype == WC_SPRITE) {
            /* 2.5: a sprite record follows its canvas directly and carries
             * 0/0 - its geometry lives in SPRITES. */
            if (lastct != WC_CANVAS && lastct != WC_SPRITE)
                return w_f_reckind;
            if (w != 0 || h != 0)
                return w_f_reckind;
        } else {
            if (w > 160 || h > 40)
                return w_f_reckind;     /* t_wab's bound: nothing on any
                                         * adapter's grid is wider */
        }
        if (ctype == WC_GRID && ++grids > 1)
            return w_f_ctype;
        if (ctype == WC_CANVAS) {
            if (++canvases > 1)
                return w_f_ctype;
            /* 2.5: a canvas carries its pixel size already divided down,
             * never 0, and 10.4 bounds the pixels at 64..320 by 8 and
             * 32..160 - which is 8..40 and 4..20 here. */
            if (w < 8 || w > 40 || h < 4 || h > 20)
                return w_f_prange;
        }
        if (ctype == WC_BOX && (w == 0 || h == 0))
            return w_f_reqprop; /* 7.3 reads both out of the record */
        if (ctype == WC_SPACER && w == 0)
            return w_f_reqprop;

        e = ovl_val_block(props, ctype);
        if (e)
            return e;
        if (ctype == WC_RADIO && !(w_pv_seen & WP_GROUP))
            return w_f_reqprop; /* 3.3 requires `group` on a radio */
        if (ctype == WC_GRID) {
            if ((w_pv_seen & (WP_COLS | WP_ROWS)) != (WP_COLS | WP_ROWS))
                return w_f_reqprop;
            if (w_pv_cols * w_pv_rows > 6140)
                return w_f_prange;        /* 5.6: the cell store's cap */
        }
        lastct = ctype;
    }

    if (!ended)
        return w_f_recend;
    if (cards == 0)
        return w_f_cardidx;
    if (w_entry > cards)
        return w_f_entry;
    w_ncard = cards;

    /* The header's flags must agree with what the display list actually
     * holds - a bundle that claims a grid and carries none has a grid claim
     * asked for and nothing to put in it (t_wab's coupling). */
    if (((w_flags & WABF_GRID) != 0) != (grids != 0))
        return w_f_flags;
    if (((w_flags & WABF_CANVAS) != 0) != (canvases != 0))
        return w_f_flags;
    return 0;
}

/* --- FXCODE, CELLS, SPRITES (WEAVE-SPEC 2.9-2.11) ----------------------- */

/* ovl_val_sprites - the count and the descriptors.  Read EARLY, because a
 * PK_SPRITE property's value is an index into this table and the props walk
 * needs the bound before it can check one. */
static const char *ovl_val_sprites(void)
{
    unsigned s, len, i, n, wb, h, fr, off, need;

    w_nsprite = 0;
    if (!w_shave[W_SPRITES])
        return 0;
    s = w_soff[W_SPRITES];
    len = w_slen[W_SPRITES];
    if (len < 2)
        return w_f_sprcount;
    n = w_b(w_seg, s);
    if (n < 1 || n > 16 || n != w_sextra[W_SPRITES])
        return w_f_sprcount;
    if (w_b(w_seg, s + 1) != 0)
        return w_f_sprcount;
    if (2 + 8 * n > len)
        return w_f_sprcount;
    for (i = 0; i < n; i++) {
        wb = w_b(w_seg, s + 2 + 8 * i);
        h = w_b(w_seg, s + 2 + 8 * i + 1);
        fr = w_b(w_seg, s + 2 + 8 * i + 2);
        if (w_b(w_seg, s + 2 + 8 * i + 3) != 0 ||
            w_b(w_seg, s + 2 + 8 * i + 6) != 0 ||
            w_b(w_seg, s + 2 + 8 * i + 7) != 0)
            return w_f_sprdesc;
        if (wb < 1 || wb > 8 || h < 1 || h > 64 || fr < 1 || fr > 8)
            return w_f_sprdesc;
        off = w_w(w_seg, s + 2 + 8 * i + 4);
        /* 2.11: per frame, h*wb bytes of image then h*wb of AND mask. */
        need = 2 * fr * h * wb;
        if (off > len || need > len - off)
            return "sprite data";
    }
    w_nsprite = n;
    return 0;
}

static const char *ovl_val_extras(void)
{
    unsigned s, len, n, i, off, kind;

    if (w_shave[W_FXCODE]) {
        s = w_soff[W_FXCODE];
        len = w_slen[W_FXCODE];
        if (len < 2)
            return w_f_fxcount;
        n = w_w(w_seg, s);
        if (n != w_sextra[W_FXCODE] || 2 + 2 * n > len)
            return w_f_fxcount;
        for (i = 0; i < n; i++)
            if (w_w(w_seg, s + 2 + 2 * i) >= len)
                return w_f_fxcount;
        w_nformula = n;
    }
    if (w_shave[W_CELLS]) {
        s = w_soff[W_CELLS];
        len = w_slen[W_CELLS];
        n = w_sextra[W_CELLS];
        if (len != 8 * n)
            return "cell record count";
        for (i = 0; i < n; i++) {
            off = s + 8 * i;
            if (w_b(w_seg, off + 1) > 25)
                return w_f_cellrec;     /* 2.10: col 0..25 */
            if (w_b(w_seg, off + 3) != 0)
                return w_f_cellrec;
            kind = w_b(w_seg, off + 2);
            if (kind < 1 || kind > 3)
                return w_f_cellrec;
            if (kind == 2 && !w_atom_ok(w_w(w_seg, off + 4)))
                return w_f_atomid;
            if (kind == 3 && w_w(w_seg, off + 4) >= w_nformula)
                return w_f_cellrec;
        }
    }
    return 0;
}

/* --- the one door ------------------------------------------------------- */

/* ovl_validate - everything past the header, in the order nothing is believed
 * before the thing that bounds it.  0 = the bundle may be trusted. */
static const char *ovl_validate_x(void)
{
    const char *e;

    /* The claim was filled by a SECOND read of the same name; the header came
     * out of a probe read before it. A swapped disk between the two is the one
     * case where those bytes disagree, and it costs four compares to notice. */
    if (w_b(w_seg, 0) != 'W' || w_b(w_seg, 1) != 'A' ||
        w_b(w_seg, 2) != 'B' || w_b(w_seg, 3) != 0x1A)
        return w_f_magic;
    if (w_w(w_seg, W_H_TOTAL) != w_size)
        return w_f_total;

    e = ovl_val_sections();
    if (e)
        return e;
    e = ovl_val_atoms();
    if (e)
        return e;
    e = ovl_val_code();
    if (e)
        return e;
    e = ovl_val_sprites();
    if (e)
        return e;
    e = ovl_val_uistream();
    if (e)
        return e;
    e = ovl_val_extras();
    if (e)
        return e;
    return ovl_val_app();
}

/* ============================================================================
 * THE OVERLAY SEAM (SPEC.md 73.14, WEAVE-SPEC 1.2)
 *
 * EVERYTHING ABOVE IS `ovl_*` AND SHIPS IN WEAVE.OVL. Validation is the
 * runtime's largest single body of code and it runs EXACTLY ONCE per bundle,
 * at open, on the UI task - which is 1.2's own test for a tenant ("nothing an
 * event handler needs mid-run lives here") and 73.14's ("split by FREQUENCY -
 * a keystroke's path stays in, a menu command's can go out"). Wave 3 crossed
 * 1.2's 55,000-byte split trigger and this is the body that moved; the
 * pre-named candidates were already spent.
 *
 * THE INVERTED ANSWER IS THE WHOLE OF THE CARE HERE. A refused overlay load
 * returns 0 (apps/cc/crt0.asm), and the validator's natural answer is a
 * `const char *` where 0 MEANS VALID - so a missing or stale WEAVE.OVL would
 * have made every malformed bundle look perfect. The two entry points
 * therefore answer 1 for "checked, and it is sound" and 0 for anything else,
 * with the field name in a RESIDENT static that the wrapper below primes
 * before the call. A refused load then reads back as `WEAVE.OVL`, which is
 * exactly what went wrong.
 *
 * The atom accessors stay resident: the PAINTER calls them on every row of
 * every list, which is mid-run by any definition.
 * ==========================================================================*/

static const char *w_valfield;

static int ovl_val_header(void)
{
    w_valfield = ovl_val_header_x();
    return w_valfield == 0;
}

static int ovl_validate(void)
{
    w_valfield = ovl_validate_x();
    return w_valfield == 0;
}

/* The two resident wrappers.  `WEAVE.OVL` is written BEFORE the call, so a
 * refused load leaves it standing and 10.4's sentence names the real cause. */
static const char *w_val_header(void)
{
    w_valfield = "WEAVE.OVL";
    return ovl_val_header() ? 0 : w_valfield;
}

static const char *w_validate(void)
{
    w_valfield = "WEAVE.OVL";
    return ovl_validate() ? 0 : w_valfield;
}
