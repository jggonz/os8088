/* ============================================================================
 * os8088 - apps/loom/lmwrite.c
 *
 * THE BUNDLE WRITER (WEAVE-SPEC 2), LOOM.OVL's fifth tenant (WEAVE-SPEC 1.2),
 * and the resolve pass that has to run before it.
 *
 * It lays the nine sections out in ascending type order, 16-byte aligned, no
 * timestamps and nothing environmental (WEAVE-SPEC 2.14 rule 1), and writes
 * the finished image into the output claim. `lm_outlen()` is its size.
 *
 * ---------------------------------------------------------------------------
 * THIS FILE IS WHERE BYTE IDENTITY IS EITHER TRUE OR NOT
 * ---------------------------------------------------------------------------
 * Every other compiler in LOOM.OVL can be right about its own section and
 * still produce a different FILE, because the file's shape is arithmetic:
 * where the first section starts, what the padding is, which offset a blob
 * gets, what the total size means. WEAVE-SPEC 2.3 pins all of it and 2.14
 * removes the last freedom a packer had, so this file is written from those
 * two sections and checked against tools/weavesim.py's `_assemble()` rather
 * than translated from it.
 *
 * Three of those rules are the ones an implementer gets wrong:
 *
 *   - THE FILE ENDS AT THE LAST SECTION'S UNPADDED END (2.3). The padding
 *     exists only BETWEEN sections. A writer that pads after the last one
 *     produces a file whose header size word and whose length disagree, and
 *     every reader believes the header.
 *   - BLOBS FOLLOW ALL BLOCKS (2.14 rule 5), in the order their referencing
 *     records were emitted - not in the order their owners appear. A list's
 *     items and the menu blob interleave by emission, and the app block is
 *     emitted last, so the MENUS blob is last too.
 *   - THE THREE CLAIM-KB BYTES ARE THE WHOLE OF 10.1's REFUSAL. They are
 *     computed here, from the model, and a wrong one is not a cosmetic
 *     difference: it is a runtime that under-asks and dies mid-canvas.
 * ==========================================================================*/

static unsigned lmo_len;
static unsigned lmo_first;          /* where the first section starts */

unsigned lm_outlen(void)
{
    return lmo_len;
}

/* 2.12's default icon: 16 mask words then 16 data words, BIG-endian per word
 * (the icon format's own order, SPEC.md 21) - the one place in a .WAB that is
 * not little-endian, which is why it is a literal table rather than a loop
 * over lm_opw(). tools/weavesim.py's default_icon() renders the same 16x16
 * tile; these are its bytes. */
static const unsigned char lmo_icon[64] = {
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0xFE, 0x40, 0x02, 0x52, 0x4A,
    0x52, 0x4A, 0x52, 0x4A, 0x52, 0x4A, 0x55, 0xAA,
    0x55, 0xAA, 0x48, 0x12, 0x48, 0x12, 0x40, 0x02,
    0x4A, 0x52, 0x40, 0x02, 0x7F, 0xFE, 0x00, 0x00
};

static unsigned lmo_align16(unsigned n)
{
    return (n + 15) & 0xFFF0;
}

/* ==========================================================================
 * THE RESOLVE PASS
 *
 * Events and menu items were kept as NAMES while the WML was read, because a
 * function index does not exist until the script has been collected - which
 * is pack_project()'s own order and therefore the order the atom pool depends
 * on. Here the names become indices, in place.
 * ========================================================================*/

/* ovl_fnfind - a function index by name, comparing the WJS source span the
 * function table kept against a NUL-terminated name in LMW_NAMES. */
static int ovl_fnfind(unsigned nameoff)
{
    int i;

    for (i = 0; i < lm_nfunc; i++) {
        unsigned r = LMW_FUNCS + (unsigned) i * LM_FUNCSZ;
        unsigned off = lm_ww(r + LMF_NAMEOFF);
        unsigned len = lm_wb(r + LMF_NAMELEN);
        unsigned k = 0;

        while (k < len) {
            if (lm_wb(LMW_NAMES + nameoff + k) != lm_sb(LM_SLOT_WJS, off + k))
                break;
            k++;
        }
        if (k == len && lm_wb(LMW_NAMES + nameoff + len) == 0)
            return i;
    }
    return -1;
}

int ovl_resolve(void)
{
    int i, k;

    /* Every PK_FUNC record in a component's block is an event binding whose
     * value is its index in lm_ev[] - lmwml.c's own note says why the record
     * had to be emitted before the answer was known. The app block's `start`
     * is written later, by the writer, and never passes through here. */
    for (i = 0; i < lm_ncomp; i++) {
        int np = (int) lm_wb(LMW_COMPS + (unsigned) i * LM_COMPSZ
                             + LMC_NPROP);
        unsigned pr = lm_ww(LMW_COMPS + (unsigned) i * LM_COMPSZ + LMC_PROP);

        for (k = 0; k < np; k++) {
            unsigned r = LMW_PROPS + (pr + (unsigned) k) * LM_PROPSZ;
            int e;
            int fi;

            if (lm_wb(r + LMP_KIND) != PK_FUNC)
                continue;
            e = (int) lm_ww(r + LMP_VAL);
            fi = ovl_fnfind(lm_evoff[e]);
            if (fi < 0) {
                lmw_msg[0] = 0;
                {
                    int a = (int) lm_evatom[e];
                    const char *nm = "onclick";
                    switch (a) {
                    case WA_ONCLICK:   nm = "onclick";   break;
                    case WA_ONCHANGE:  nm = "onchange";  break;
                    case WA_ONKEY:     nm = "onkey";     break;
                    case WA_ONSELECT:  nm = "onselect";  break;
                    case WA_ONEDIT:    nm = "onedit";    break;
                    case WA_ONCALC:    nm = "oncalc";    break;
                    case WA_ONCOLLIDE: nm = "oncollide"; break;
                    case WA_ONWALL:    nm = "onwall";    break;
                    case WA_ONSCORE:   nm = "onscore";   break;
                    case WA_ONTICK:    nm = "ontick";    break;
                    default:           nm = "oncommand"; break;
                    }
                    lm_cat(lmw_msg, nm);
                }
                lm_cat(lmw_msg, "=\"");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned q;
                    for (q = 0; q < lm_evlen[e] && m + 1 < LM_ERRMAX; q++) {
                        lmw_msg[m] = (char) lm_wb(LMW_NAMES + lm_evoff[e] + q);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, "\": no such function in the script");
                lm_perr(LM_SLOT_WML, lm_evline[e], lmw_msg);
                return 0;
            }
            /* 4.11.1's budget is checked HERE, where the binding and the
             * function meet - which is where pack_project() checks it. */
            if (lm_evatom[e] == WA_ONTICK && !ovl_ontick_ok(fi,
                                                            lm_evline[e]))
                return 0;
            lm_wpw(r + LMP_VAL, (unsigned) fi);
        }
    }

    /* 2.6.2's menu blob: an item with no oncommand is 0xFF - present, inert. */
    for (i = 0; i < lm_nmenu; i++) {
        for (k = 0; k < (int) lm_mcount[i]; k++) {
            int j = i * LM_MAXITEM + k;
            int fi;

            if (lm_mfnoff[j] == 0xFFFF) {
                lm_mfnlen[j] = 0xFF;    /* the marker the writer reads */
                continue;
            }
            fi = ovl_fnfind(lm_mfnoff[j]);
            if (fi < 0) {
                lmw_msg[0] = 0;
                lm_cat(lmw_msg, "oncommand=\"");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    unsigned q;
                    unsigned n = 0;
                    while (lm_wb(LMW_NAMES + lm_mfnoff[j] + n) != 0)
                        n++;
                    for (q = 0; q < n && m + 1 < LM_ERRMAX; q++) {
                        lmw_msg[m] =
                            (char) lm_wb(LMW_NAMES + lm_mfnoff[j] + q);
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                lm_cat(lmw_msg, "\": no such function in the script");
                lm_perr(LM_SLOT_WML, lm_mline[j], lmw_msg);
                return 0;
            }
            lm_mfnlen[j] = (unsigned char) fi;
        }
    }
    return 1;
}

/* ==========================================================================
 * THE SECTIONS
 *
 * Each is built into the OUTPUT image at its final offset, which is knowable
 * because the section COUNT is known before any body is written (2.3: "the
 * first section begins at align16(32 + 8 x count)"). So there is no second
 * staging step and no copy: the writer walks the model once per section.
 * ========================================================================*/

static int      lmo_type[9];
static unsigned lmo_off[9];
static unsigned lmo_slen[9];
static unsigned lmo_extra[9];
static int      lmo_nsec;

static unsigned lmo_at;             /* the write cursor inside a section */
static int      lmo_over;           /* ...and whether it ever left the claim */

/* THE CURSOR KEEPS COUNTING PAST THE CAP AND THE WRITE STOPS, which is the
 * order that matters: 2.1 bounds a bundle at 63,488 bytes and the OUTPUT
 * CLAIM is exactly that, so a project whose sections would overrun it must
 * not write one byte past the claim on its way to finding out. Every section
 * length here is already bounded by its scratch region (11.4), so this cannot
 * fire on anything the compilers will accept - it is the guard that makes
 * that sentence a fact rather than an argument. */
static void lmo_b(unsigned v)
{
    if (lmo_at < W_CAP)
        lm_opb(lmo_at, v);
    else
        lmo_over = 1;
    lmo_at++;
}

static void lmo_w(unsigned v)
{
    lmo_b(v & 0xFF);
    lmo_b((v >> 8) & 0xFF);
}

/* --- 2.5: UISTREAM ------------------------------------------------------- */
static void ovl_sec_ui(unsigned base)
{
    int card, i;

    lmo_at = base;
    lm_nrec = 0;
    for (card = 1; card <= lm_ncard; card++) {
        lmo_b(W_REC_CARD);
        lmo_b((unsigned) card);
        lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0);
        lmo_w(W_NOPROPS);           /* v1 cards carry no props (2.5) */
        lm_nrec++;
        for (i = 0; i < lm_ncomp; i++) {
            unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
            if ((int) lm_wb(r + LMC_CARD) != card)
                continue;
            lmo_b(W_REC_COMP);
            lmo_b(lm_wb(r + LMC_ID));
            lmo_b(lm_wb(r + LMC_CTYPE));
            lmo_b(lm_wb(r + LMC_W));
            lmo_b(lm_wb(r + LMC_H));
            lmo_b(lm_wb(r + LMC_STYLE));
            lmo_b(lm_wb(r + LMC_CFLAGS));
            lmo_b(0);
            lmo_w(lm_ww(r + LMC_SPARE));    /* the block offset, patched in
                                             * by ovl_sec_props() */
            lm_nrec++;
        }
    }
    lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0);
    lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0);    /* REC_END */
    lm_nrec++;
}

/* --- 2.6: PROPS ---------------------------------------------------------
 * The block offsets are computed FIRST, into each component's LMC_SPARE word,
 * because UISTREAM carries them and UISTREAM is written before this section
 * exists. 2.14 rule 5's order: blocks in UISTREAM order, the app block last,
 * gap-free; blobs after all blocks, in the order their referencing records
 * were emitted. */
static int      lmo_appn;           /* the app block's record count */
static unsigned lmo_appat;
static unsigned lmo_blobat;         /* the running blob cursor */
static unsigned lmo_blobw;          /* ...and where the blobs are written */

static void ovl_props_layout(void)
{
    unsigned off = 0;
    int i;

    for (i = 0; i < lm_ncomp; i++) {
        unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
        int np = (int) lm_wb(r + LMC_NPROP);
        if (np == 0) {
            lm_wpw(r + LMC_SPARE, W_NOPROPS);
            continue;
        }
        lm_wpw(r + LMC_SPARE, off);
        off += 4 * (unsigned) (np + 1);
    }
    lmo_appn = 1;                                   /* CARD is always there */
    if (lm_startfn >= 0)
        lmo_appn++;
    if (lm_nmenu > 0)
        lmo_appn++;
    lmo_appat = off;
    off += 4 * (unsigned) (lmo_appn + 1);
    lmo_blobat = off;
}

static void ovl_put_rec(int atom, int kind, unsigned val)
{
    lmo_b((unsigned) atom);
    lmo_b((unsigned) kind);
    lmo_w(val);
}

static void ovl_sec_props(unsigned base)
{
    int i, k;
    unsigned blobs = lmo_blobat;

    lmo_at = base;
    lmo_blobw = base + lmo_blobat;
    for (i = 0; i < lm_ncomp; i++) {
        unsigned r = LMW_COMPS + (unsigned) i * LM_COMPSZ;
        int np = (int) lm_wb(r + LMC_NPROP);
        unsigned pr = lm_ww(r + LMC_PROP);

        if (np == 0)
            continue;
        for (k = 0; k < np; k++) {
            unsigned p = LMW_PROPS + (pr + (unsigned) k) * LM_PROPSZ;
            int kind = (int) lm_wb(p + LMP_KIND);
            unsigned v = lm_ww(p + LMP_VAL);

            if (kind == PK_BLOB) {
                /* 2.6.1: the items blob, count byte then the atom ids. It is
                 * copied out of LMW_NAMES at the blob cursor. */
                unsigned n = lm_wb(LMW_NAMES + v) + 1;
                unsigned q;
                unsigned save = lmo_at;
                lmo_at = base + blobs;
                for (q = 0; q < n; q++)
                    lmo_b(lm_wb(LMW_NAMES + v + q));
                lmo_at = save;
                ovl_put_rec((int) lm_wb(p + LMP_ATOM), PK_BLOB, blobs);
                blobs += n;
                continue;
            }
            ovl_put_rec((int) lm_wb(p + LMP_ATOM), kind, v);
        }
        lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0);     /* the terminator */
    }
    /* the app block (2.6.2), last, sorted by atom: CARD 20, start 40,
     * MENUS 62 */
    ovl_put_rec(WA_CARD, PK_INT, (unsigned) lm_entrycard);
    if (lm_startfn >= 0)
        ovl_put_rec(WA_START, PK_FUNC, (unsigned) lm_startfn);
    if (lm_nmenu > 0) {
        unsigned save = lmo_at;
        unsigned n;
        int m, q;
        lmo_at = base + blobs;
        lmo_b((unsigned) lm_nmenu);
        for (m = 0; m < lm_nmenu; m++) {
            lmo_b(lm_mtitle[m]);
            lmo_b(lm_mcount[m]);
            for (q = 0; q < (int) lm_mcount[m]; q++) {
                int j = m * LM_MAXITEM + q;
                lmo_b(lm_mlabel[j]);
                lmo_b(lm_mfnlen[j]);    /* the index, or 0xFF (see resolve) */
            }
        }
        n = lmo_at - (base + blobs);
        lmo_at = save;
        ovl_put_rec(WA_MENUS, PK_BLOB, blobs);
        blobs += n;
    }
    lmo_b(0); lmo_b(0); lmo_b(0); lmo_b(0);
    lmo_blobat = blobs;             /* the section's length */
}

/* --- 2.8: CODE ----------------------------------------------------------- */
static void ovl_sec_code(unsigned base)
{
    int i;
    unsigned off;

    lmo_at = base;
    lmo_b((unsigned) lm_nfunc);
    lmo_b((unsigned) lm_nglob);
    off = 2 + 4 * (unsigned) lm_nfunc;
    for (i = 0; i < lm_nfunc; i++) {
        unsigned r = LMW_FUNCS + (unsigned) i * LM_FUNCSZ;
        unsigned len;
        lmo_w(off);
        lmo_b(lm_wb(r + LMF_NARGS));
        lmo_b(lm_wb(r + LMF_NLOCALS));
        len = ovl_fnlen(i);
        off += len;
    }
    for (i = 0; i < lm_nfunc; i++) {
        unsigned r = LMW_FUNCS + (unsigned) i * LM_FUNCSZ;
        unsigned c = lm_ww(r + LMF_CODEOFF);
        unsigned len = ovl_fnlen(i);
        unsigned k;
        for (k = 0; k < len; k++)
            lmo_b(lm_wb(LMW_CODE + c + k));
    }
    if (lm_nfunc == 0)
        lmo_b(0);                   /* 2.8: the empty table's one HALT byte */


}

/* --- 2.7: ATOMS ---------------------------------------------------------- */
static void ovl_sec_atoms(unsigned base)
{
    int n = lm_natom();
    int i;
    unsigned pos;

    lmo_at = base;
    lmo_w((unsigned) n);
    pos = 2 + 2 * (unsigned) n;
    for (i = 0; i < n; i++) {
        lmo_w(pos);
        pos += lm_atom_len(WA_APP_FIRST + i) + 2;
    }
    for (i = 0; i < n; i++) {
        unsigned off = lm_atom_off(WA_APP_FIRST + i);
        unsigned len = lm_atom_len(WA_APP_FIRST + i);
        unsigned k;
        lmo_b(len);
        for (k = 0; k < len; k++)
            lmo_b(lm_wb(LMW_ATTXT + off + k));
        lmo_b(0);
    }
}

/* --- 2.9/2.10: FXCODE and CELLS ------------------------------------------ */
static void ovl_sec_fx(unsigned base)
{
    int i;
    unsigned pos;

    lmo_at = base;
    lmo_w((unsigned) lm_nform);
    pos = 2 + 2 * (unsigned) lm_nform;
    for (i = 0; i < lm_nform; i++) {
        lmo_w(pos);
        pos += ovl_formlen(i);
    }
    for (i = 0; i < lm_nform; i++) {
        unsigned off = lm_ww(LMW_FXOFF + (unsigned) i * 2);
        unsigned len = ovl_formlen(i);
        unsigned k;
        for (k = 0; k < len; k++)
            lmo_b(lm_wb(LMW_FX + off + k));
    }
}

static void ovl_sec_cells(unsigned base)
{
    int i;

    lmo_at = base;
    for (i = 0; i < lm_ncell; i++) {
        unsigned r = LMW_CELLS + (unsigned) i * 8;
        unsigned k;
        for (k = 0; k < 8; k++)
            lmo_b(lm_wb(r + k));
    }
}

/* --- 2.11: SPRITES ------------------------------------------------------- */
static void ovl_sec_sprites(unsigned base)
{
    int i;
    unsigned data;

    lmo_at = base;
    lmo_b((unsigned) lm_nspr_art);
    lmo_b(0);
    data = 2 + 8 * (unsigned) lm_nspr_art;
    for (i = 0; i < lm_nspr_art; i++) {
        unsigned r = LMW_SPRD + (unsigned) i * LM_SPRDSZ;
        lmo_b(lm_wb(r + LMS_WB));
        lmo_b(lm_wb(r + LMS_HPX));
        lmo_b(lm_wb(r + LMS_FRAMES));
        lmo_b(0);
        lmo_w(data);
        lmo_w(0);
        data += lm_ww(r + LMS_LEN);
    }
    for (i = 0; i < lm_nspr_art; i++) {
        unsigned r = LMW_SPRD + (unsigned) i * LM_SPRDSZ;
        unsigned off = lm_ww(r + LMS_DATA);
        unsigned len = lm_ww(r + LMS_LEN);
        unsigned k;
        for (k = 0; k < len; k++)
            lmo_b(lm_wb(LMW_SPRB + off + k));
    }
}

/* ==========================================================================
 * THE FILE
 * ========================================================================*/

static unsigned ovl_gridkb(void)
{
    unsigned need;

    if (!lm_hasgrid)
        return 0;
    /* 5.6: max(8, ceil((16 + rows*cols*4)/1024) + 2), capped at 26.
     * 3.3 caps cols x rows at 6,140, so 16 + 6140*4 = 24,576 - the whole
     * arithmetic fits sixteen bits and needs no `long` (SPEC.md 73.7). */
    need = 16 + (unsigned) lm_gridrows * (unsigned) lm_gridcols * 4;
    need = (need + 1023) / 1024 + 2;
    if (need < 8)
        need = 8;
    if (need > 26)
        need = 26;
    return (unsigned) need;
}

static unsigned ovl_canvaskb(void)
{
    unsigned need;
    int hrows;
    int i;
    int nspr = 0;

    if (!lm_hascanvas)
        return 0;
    for (i = 0; i < lm_ncomp; i++)
        if (lm_wb(LMW_COMPS + (unsigned) i * LM_COMPSZ + LMC_CTYPE)
            == WC_SPRITE)
            nspr++;
    /* 6.10.4: 16-byte header, 24 bytes a sprite record, and the 1bpp buffer
     * at the ROUNDED height the runtime derives from the record byte - never
     * the WML `h`. Sizing it from the WML h under-asks by up to seven rows. */
    hrows = (lm_canvh + 7) / 8;
    /* The largest legal canvas is 320x160 with sixteen sprites:
     * 16 + 384 + 40*160 = 6,800. Sixteen bits, no `long`. */
    need = 16 + 24 * (unsigned) nspr
         + (unsigned) (lm_canvw / 8) * (unsigned) (hrows * 8);
    need = (need + 1023) / 1024;
    if (need < 2)
        need = 2;
    if (need > 8)
        need = 8;
    return (unsigned) need;
}

int ovl_write(void)
{
    unsigned at;
    unsigned total;
    int i;
    unsigned nsec;

    lmo_nsec = 0;
    lmo_over = 0;
    lmo_type[lmo_nsec] = W_UISTREAM;
    lmo_nsec++;
    lmo_type[lmo_nsec] = W_PROPS;
    lmo_nsec++;
    lmo_type[lmo_nsec] = W_CODE;
    lmo_nsec++;
    lmo_type[lmo_nsec] = W_ATOMS;
    lmo_nsec++;
    if (lm_hasgrid) {
        lmo_type[lmo_nsec] = W_FXCODE;
        lmo_nsec++;
        lmo_type[lmo_nsec] = W_CELLS;
        lmo_nsec++;
    }
    if (lm_nspr_art > 0) {
        lmo_type[lmo_nsec] = W_SPRITES;
        lmo_nsec++;
    }
    lmo_type[lmo_nsec] = W_ICON;
    lmo_nsec++;
    nsec = (unsigned) lmo_nsec;

    ovl_props_layout();
    lmo_first = lmo_align16(32 + 8 * nsec);

    /* Lay every section out, writing each body at its final offset. The
     * lengths have to be known to place the next one, so each builder answers
     * where its cursor stopped. */
    at = lmo_first;
    for (i = 0; i < lmo_nsec; i++) {
        unsigned len = 0;

        lmo_off[i] = at;
        switch (lmo_type[i]) {
        case W_UISTREAM:
            ovl_sec_ui(at);
            len = 10 * (unsigned) lm_nrec;
            lmo_extra[i] = (unsigned) lm_nrec;
            break;
        case W_PROPS:
            ovl_sec_props(at);
            len = lmo_blobat;
            lmo_extra[i] = lmo_appat;
            break;
        case W_CODE:
            ovl_sec_code(at);
            len = lmo_at - at;
            lmo_extra[i] = 0;
            break;
        case W_ATOMS:
            ovl_sec_atoms(at);
            len = lmo_at - at;
            lmo_extra[i] = 0;
            break;
        case W_FXCODE:
            ovl_sec_fx(at);
            len = lmo_at - at;
            lmo_extra[i] = (unsigned) lm_nform;
            break;
        case W_CELLS:
            ovl_sec_cells(at);
            len = 8 * (unsigned) lm_ncell;
            lmo_extra[i] = (unsigned) lm_ncell;
            break;
        case W_SPRITES:
            ovl_sec_sprites(at);
            len = lmo_at - at;
            lmo_extra[i] = (unsigned) lm_nspr_art;
            break;
        default:                    /* W_ICON */
            {
                unsigned k;
                lmo_at = at;
                for (k = 0; k < 64; k++)
                    lmo_b(lmo_icon[k]);
            }
            len = 64;
            lmo_extra[i] = 0;
            break;
        }
        lmo_slen[i] = len;
        /* 2.3: the next section begins at align16(this offset + this length),
         * and the padding between them is 0x00. */
        {
            unsigned nxt = lmo_align16(at + len);
            if (nxt <= W_CAP)
                lm_ofill(at + len, 0, nxt - (at + len));
            else
                lmo_over = 1;
            at = nxt;
        }
    }
    /* 2.3: the file ends at the LAST section's unpadded end. */
    total = lmo_off[lmo_nsec - 1] + lmo_slen[lmo_nsec - 1];
    if (lmo_over || total > W_CAP) {
        lmw_msg[0] = 0;
        lm_cat(lmw_msg, "bundle is ");
        lm_catu(lmw_msg, total);
        lm_cat(lmw_msg, " bytes; the cap is 63488 - the directory size must "
               "stand for the resident ask");
        lm_perr(LM_SLOT_WML, 1, lmw_msg);
        return 0;
    }

    /* 2.5: the block offsets go into the UISTREAM records, which were written
     * with LMC_SPARE already holding them - so nothing is patched here, and
     * that is worth a line: a writer that patched would have to know where
     * each record landed, and ovl_props_layout() runs first exactly so that
     * it does not. */

    /* --- 2.2: the header --- */
    lm_opb(0, 'W');
    lm_opb(1, 'A');
    lm_opb(2, 'B');
    lm_opb(3, 0x1A);
    lm_opw(4, 1);
    lm_opw(6, total);
    lm_flags = 0;
    if (lm_hasgrid)
        lm_flags |= WABF_GRID;
    if (lm_hascanvas)
        lm_flags |= WABF_CANVAS;
    /* 2.2.1's three causes for WABF_TIMER: timer() is called, the app
     * declares an <input>, or it declares a <grid> - a formula bar is a
     * library-wired input, so a grid blinks a caret too. */
    {
        int hasinput = 0;
        for (i = 0; i < lm_ncomp; i++)
            if (lm_wb(LMW_COMPS + (unsigned) i * LM_COMPSZ + LMC_CTYPE)
                == WC_INPUT)
                hasinput = 1;
        if ((lm_used & (1 << WB_TIMER)) || lm_hasgrid || hasinput)
            lm_flags |= WABF_TIMER;
    }
    if (lm_used & ((1 << WB_SAVE) | (1 << WB_LOAD)))
        lm_flags |= WABF_STATE;
    lm_opw(8, (unsigned) lm_flags);
    lm_opb(10, (unsigned) lm_vmkb);
    lm_opb(11, ovl_gridkb());
    lm_opb(12, nsec);
    lm_opb(13, (unsigned) lm_entrycard);
    lm_opb(14, ovl_canvaskb());
    lm_opb(15, 0);
    {
        /* 2.2: 15 characters and a NUL, and the bytes after the NUL are 0x00
         * - "space-padded before the NUL is not allowed". */
        int n = (int) os88_strlen(lm_appname);
        for (i = 0; i < 16; i++)
            lm_opb(16 + (unsigned) i,
                   i < n ? (unsigned) (unsigned char) lm_appname[i] : 0);
    }

    /* --- 2.3: the section table --- */
    for (i = 0; i < lmo_nsec; i++) {
        unsigned r = 32 + 8 * (unsigned) i;
        lm_opb(r, (unsigned) lmo_type[i]);
        lm_opb(r + 1, 0);
        lm_opw(r + 2, lmo_off[i]);
        lm_opw(r + 4, lmo_slen[i]);
        lm_opw(r + 6, lmo_extra[i]);
    }
    lm_ofill(32 + 8 * nsec, 0, lmo_first - (32 + 8 * nsec));

    lmo_len = total;
    return 1;
}

/* ==========================================================================
 * THE DOOR
 * ========================================================================*/

/* THE ORDER IS tools/weavesim.py's pack_project(), STEP FOR STEP, and it is
 * part of the contract rather than an implementation detail: 2.14 rule 3 pins
 * the interning traversal (WML, then WJS TOKENS, then FX formulas in CELLS
 * order), and 10.5's "which error wins" follows the rest of it. The bodies
 * compile LAST, after the sheet and the sprite art, which is why ovl_wjs()
 * and ovl_wjs_gen() are two doors. */
int ovl_pack_rest(void)
{
    if (!ovl_wjs())                 /* tokens + top-level declarations */
        return 0;
    if (!ovl_sheet())               /* .WFX -> CELLS + FXCODE */
        return 0;
    if (!ovl_sprites())             /* .WSP -> SPRITES */
        return 0;
    if (!ovl_wjs_gen())             /* 4.6's code generation */
        return 0;
    if (!ovl_resolve())             /* events and menu items -> indices */
        return 0;
    return ovl_write();
}

int ovl_pack(void)
{
    lm_clearerr();
    if (!ovl_wml())
        return 0;
    return ovl_pack_rest();
}
