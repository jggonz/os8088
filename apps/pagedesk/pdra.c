/* ============================================================================
 * os8088 - apps/pagedesk/pdra.c      PDREND: annotations on screen
 *
 * #included by pdrend.c after pdrv.c. PAGEDESK-SPEC 20.12 is the authority.
 *
 * Two things are drawn here, both IN BAND - they are display-list records of
 * the same pass as the page, so the opaque band blit needs no second pass:
 *
 *   - the source's annotations (PDRF_ANNOTS): each /AP /N form XObject, placed
 *     in the annotation's /Rect by PDF 1.7 12.5.5 and run through the content
 *     interpreter like any Form. Highlights go FIRST (before the page), because
 *     a band is opaque and a Multiply blend is not available: drawn after the
 *     text, a yellow quad would hide it;
 *   - the edit store's records for the page (PDRF_EDITS, PAGEDESK-SPEC 13.1.1),
 *     in the look of 13.3 / 17.4, built from the same path, fill, stroke, dash
 *     and text machinery as page content (pdri.c, pdrg.c).
 *
 * The phases of one pass, in display-list order:
 *   edit highlights | source highlights | PAGE | other source annotations | edits
 * ==========================================================================*/

#ifdef PD_HOST                       /* the offsets this file reads by number: the host build holds them to the structs */
#include <stddef.h>
typedef char pdra_ck_an[offsetof(struct pdxb_annot, subtype) == 4 && offsetof(struct pdxb_annot, rect) == 20 &&
                        offsetof(struct pdxb_annot, flags) == 52 && offsetof(struct pdxb_annot, has_ap) == 54 ? 1 : -1];
typedef char pdra_ck_ed[offsetof(struct pd_edit, src_num) == 12 && offsetof(struct pd_edit, src_gen) == 14 &&
                        offsetof(struct pdxb_render, edits_seg) > 0 ? 1 : -1];
#endif
static struct pd_edit g_ed;
static struct pd_rect g_ar;         /* the annotation's /Rect (pt16)          */
static pdw g_aob, g_agn;            /* the annotation's object                */
static pdw g_apobj, g_apgen;        /* its appearance stream                  */
#define ED_SZ 48

/* ---- helpers --------------------------------------------------------------- */
/* luminance of the 16 palette colours (D9: the EGA 4-level triple, 6 is brown) */
static const pdb ed_lum[16] = { 0, 19, 99, 118, 51, 70, 100, 170, 85, 104, 184, 203, 136, 155, 235, 255 };
static void ed_lw(pdi w16) { pdm_pt16_to_fx(&GS.lw, (pd_pt)w16); }
/* a point of page space (pt16) to device, into g_dx / g_dy */
static void edev(pdi x, pdi y)
{
    pdm_pt16_to_fx(&t2, (pd_pt)x); pdm_pt16_to_fx(&t3, (pd_pt)y);
    to_dev(&t2, &t3);
}
static void ep(pdi x, pdi y, pdw mv)
{
    edev(x, y);
    if (mv) path_move(g_dx, g_dy); else path_line(g_dx, g_dy);
}
static pdw ebyte(pdw i) { return pd_fpeekb(g_rq.edits_seg, (pdw)(g_ed.text_off + i)); }

/* does a record replace or delete the source annotation obj gen? (src_num, src_gen at +12, +14) */
static pdw ed_replaced(pdw obj, pdw gen)
{
    pdw i, o;
    for (i = 0; i < g_rq.edits_count; i++) {
        o = (pdw)(i * ED_SZ);
        if (pd_fpeekw(g_rq.edits_seg, (pdw)(o + 12)) == obj && pd_fpeekw(g_rq.edits_seg, (pdw)(o + 14)) == gen) return 1;
    }
    return 0;
}

/* ---- the source's appearances ------------------------------------------------ */
/* PDF 1.7 12.5.5: the form's BBox through its Matrix, and the smallest upright
 * rectangle holding the four transformed corners (exact for every matrix - a
 * quarter turn, a skew, a rotation); A maps that to the annotation's /Rect.
 * Sets g_am, 0 = degenerate */
static struct pd_fx fz;
static struct pd_fx g_fx0, g_fy0, g_fx1, g_fy1;     /* the transformed box */
static pdw ap_fit(const struct pd_mat *m, const struct pd_fx *bx0, const struct pd_fx *by0,
                  const struct pd_fx *bx1, const struct pd_fx *by1)
{
    static struct pd_fx x, y, rx, ry, rw, rh;
    pdw i;
    for (i = 0; i < 4; i++) {                       /* the four corners */
        pdr_cpy(&x, i & 1 ? bx1 : bx0, 4); pdr_cpy(&y, i & 2 ? by1 : by0, 4); pdm_mat_apply(&x, &y, m);
        if (!i || pdm_fx_cmp(&x, &g_fx0) < 0) pdr_cpy(&g_fx0, &x, 4);
        if (!i || pdm_fx_cmp(&x, &g_fx1) > 0) pdr_cpy(&g_fx1, &x, 4);
        if (!i || pdm_fx_cmp(&y, &g_fy0) < 0) pdr_cpy(&g_fy0, &y, 4);
        if (!i || pdm_fx_cmp(&y, &g_fy1) > 0) pdr_cpy(&g_fy1, &y, 4);
    }
    pdm_fx_sub(&x, &g_fx1, &g_fx0); pdm_fx_sub(&y, &g_fy1, &g_fy0);     /* the extent */
    pdm_pt16_to_fx(&rx, g_ar.x0); pdm_pt16_to_fx(&ry, g_ar.y0);
    pdm_pt16_to_fx(&rw, g_ar.x1); pdm_pt16_to_fx(&rh, g_ar.y1);
    pdm_fx_sub(&rw, &rw, &rx); pdm_fx_sub(&rh, &rh, &ry);
    if (pdm_fx_cmp(&x, &fz) <= 0 || pdm_fx_cmp(&y, &fz) <= 0 ||
        pdm_fx_cmp(&rw, &fz) <= 0 || pdm_fx_cmp(&rh, &fz) <= 0) return 0;
    pdm_mat_ident(&g_am);
    pdm_fx_div(&g_am.a, &rw, &x); pdm_fx_div(&g_am.d, &rh, &y);
    pdm_fx_mul(&x, &g_am.a, &g_fx0); pdm_fx_sub(&g_am.e, &rx, &x);
    pdm_fx_mul(&y, &g_am.d, &g_fy0); pdm_fx_sub(&g_am.f, &ry, &y);
    return 1;
}

/* DICT_GET without following the final reference (dict_get does) */
static pdw ref_get(pdw obj, pdw gen, const char *k0)
{
    pdr_zero(&g_dg, sizeof g_dg);
    g_dg.objnum = obj; g_dg.gen = gen;
    if (k0) pdr_scpy(g_dg.key[0], k0, 32);
    return eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0);
}
/* /AS, the annotation's appearance state, into g_name: 1 = there is one */
static pdw ap_state(void)
{
    if (dict_get(g_aob, g_agn, "AS", 0, 0) || g_dg.kind != PDK_NAME) return 0;
    pdr_scpy(g_name, g_dg.text, PDR_NNAME);
    return 1;
}
/* the annotation's normal appearance stream, /AP /N: a form (an object with a /BBox -
 * DICT_GET answers a stream's dictionary as a dictionary), or a state dictionary -
 * DIRECT (/N << /Off 5 0 R /Yes 6 0 R >>, a check box's) or by reference - of which
 * /AS names the entry. 1 = g_apobj / g_apgen hold it */
static pdw ap_find(void)
{
    pdw o, g, st = 0;
    for (;;) {
        pdr_zero(&g_dg, sizeof g_dg);
        g_dg.objnum = g_aob; g_dg.gen = g_agn;
        pdr_scpy(g_dg.key[0], "AP", 32); pdr_scpy(g_dg.key[1], "N", 32);
        if (st) pdr_scpy(g_dg.key[2], g_name, 32);
        if (eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0)) return 0;
        if (g_dg.kind == PDK_REF) break;
        if (st || g_dg.kind != PDK_DICT || !ap_state()) return 0;
        st = 1;                                         /* direct states: /AP /N /<AS> */
    }
    o = g_dg.refnum; g = g_dg.refgen;
    if (ref_get(o, g, "BBox")) {                        /* not a form: states by reference */
        if (st || !ap_state() || ref_get(o, g, g_name) || g_dg.kind != PDK_REF) return 0;
        o = g_dg.refnum; g = g_dg.refgen;
        if (ref_get(o, g, "BBox")) return 0;
    }
    g_apobj = o; g_apgen = g;
    return 1;
}

/* the next source annotation of this phase (1 = highlights, 2 = the rest):
 * 1 = its form is open and interpretation goes on, 0 = not this one, 2 = no more.
 * ANNOTS answers in the exchange block (pdxb_annot): objnum 0, gen 2, subtype 4,
 * rect 20, flags 52, has_ap 54 - read before the next call overwrites them */
static pdw annot_try(void)
{
    pdw n, fl, ap, hl;
    if (g_dlfull || pdr_eng(PDV_ENG_ANNOTS, g_page, g_aidx, 0)) return 2;
    g_aidx++;
    g_aob = pd_fpeekw(g_xb, 0); g_agn = pd_fpeekw(g_xb, 2);
    fl = pd_fpeekw(g_xb, 52); ap = pd_fpeekw(g_xb, 54);
    pd_fcopy_in(g_xb, 4, g_name, 16); g_name[15] = 0;
    pd_fcopy_in(g_xb, 20, &g_ar, 8);
    hl = pdr_seq(g_name, "Highlight");
    if (!g_aob || !ap || (fl & 34)) return 0;                                   /* Hidden (2), NoView (32) */
    if ((g_aph == 1) != (hl != 0)) return 0;
    if ((g_aflags & PDRF_EDITS) && g_rq.edits_seg && ed_replaced(g_aob, g_agn)) return 0;
    if (!ap_find()) return 0;
    gs_reset();
    g_resp = g_page; g_reso = 0; g_resg = 0;
    path_reset(); op_clear(); g_inbt = 0; g_tjlive = 0;
    g_inl = 0; g_dictd = 0;               /* a stream that ended inside BI's dictionary or a << >> leaves nothing behind */
    n = g_nfr;
    g_afm = 1; do_form(g_apobj, g_apgen); g_afm = 0;
    return g_nfr > n ? 1 : 0;
}

/* The phase machine (frame_end calls it when a stream ends and nothing is open):
 * 1 = a stream is open, 0 = the page and everything on it is drawn */
static pdw annot_next(void)
{
    pdw r, tries = 0;
    if (g_mode != PDR_INTERP || !g_aflags) return 0;
    for (;;) {
        if (g_aph == 4) return 0;
        if (g_aph == 5 || g_aph == 3) {                     /* the edit store's records, a budget a turn */
            if (annot_edits(g_aph == 3)) break;
            if (g_aph == 3) { g_aph = 4; continue; }
            g_aidx = 0; g_aph = 1;                          /* then the source's highlights, */
            if (g_aflags & PDRF_ANNOTS) continue;
            r = 2;                                          /* or none: straight to the page */
        } else if (g_aph == 0) {                            /* the page is over: the rest */
            g_aph = (g_aflags & PDRF_ANNOTS) ? 2 : 3; g_aidx = 0; g_aedi = 0;
            continue;
        } else {
            if (++tries > 8) break;                         /* 8 ANNOTS (and their DICT_GETs) a turn */
            r = annot_try();
            if (r == 1) return 1;
        }
        if (r == 2) {
            if (g_aph == 1) { g_aph = 0; if (page_open() == 0) return 1; }
            else g_aph = 3;
        }
    }
    g_await = 1;                                            /* a slice stays a slice: on at the next turn */
    return 1;
}

/* ---- the edit store's marks (13.3) ------------------------------------------- */
/* Every mark but the Text tool's box is computed in PDXG (pdxedit.c, 20.22.7):
 * PDV_GFX_EDIT hands back a batch of drawing ops in page space, ending at the
 * first PAINT, and ed_exec runs it through the page's own path and paint. A
 * page view whose PDXG cannot be had draws the Text boxes alone and notes
 * PDF_INC_MODULE (the ladder, x_need). */
#ifndef PDV_GFX_EDIT
#define PDV_GFX_EDIT 0xCF
#endif
static struct pdxb_emark g_xem;
static const pdb ed_na[9] = { 0, 2, 2, 2, 6, 0, 0, 1, 1 };
/* SETB's fields (pdxedit.c XF_*): the state a mark sets */
static pdb *const ed_f[11] = { &GS.slum, &GS.sc4, &GS.flum, &GS.fc4, &GS.dn, &GS.dash[0], &GS.dash[1],
                               (pdb *)&GS.dph, (pdb *)&GS.dph + 1, &GS.cap, &GS.join };
static pdi ed_px, ed_py;                /* the last point op's point (an ink stroke's split) */
static void ed_exec(void)
{
    static pdi v[7];                    /* the op word and up to six arguments */
    static pdw o, n, w, a;
    o = PDXE_OPS; n = g_xem.n;
    while (n && !g_dlfull) {
        pd_fcopy_in(g_xb, o, v, 14);    /* PDXG leaves 14 bytes of the XB past its last op */
        w = (pdw)v[0] & 0xFF; a = (pdw)v[0] >> 8;
        if (w > 8 || n <= ed_na[w]) return;
        n = (pdw)(n - ed_na[w] - 1); o = (pdw)(o + ed_na[w] * 2 + 2);
        if (w == 7) { if (a < 11) *ed_f[a] = (pdb)v[1]; }
        else if (w == 8) ed_lw(v[1]);
        else if (w == 6) paint(a & 1, 0, (pdw)((a >> 1) & 1), (pdw)((a >> 2) & 1));
        else if (w == 5) path_close();
        else if (w == 4) {
            edev(v[1], v[2]); v[1] = g_dx; v[2] = g_dy;
            edev(v[3], v[4]); v[3] = g_dx; v[4] = g_dy;
            edev(v[5], v[6]);
            path_curve(v[1], v[2], v[3], v[4], g_dx, g_dy);
        } else if (w) {
            /* INK: a long stroke draws what is there and goes on FROM its last point */
            if (w == 3 && g_nv >= PDR_NPATH - 3) { paint(0, 0, 1, 0); ep(ed_px, ed_py, 1); }
            ep(v[1], v[2], w == 1); ed_px = v[1]; ed_py = v[2];
        }
    }
}
/* record g_aedi's marks, through PDXG */
static void ed_marks(void)
{
    pdw k;
    if (!x_have(PDX_GFX)) { x_need(PDX_GFX); return; }
    pdr_zero(&g_xem, sizeof g_xem);
    g_xem.eseg = g_rq.edits_seg; g_xem.rec = g_aedi; g_xem.bpp = g_bpp; g_xem.mono = g_rq.mono_mark;
    for (k = 0; k < 4000; k++) {
        pd_fcopy_out(&g_xem, g_xb, 0, sizeof g_xem);
        x_call(PDV_GFX_EDIT, g_xb, 0, 0);
        if (x_cf) return;
        pd_fcopy_in(g_xb, 0, &g_xem, sizeof g_xem);
        ed_exec();
        if (!g_xem.more || g_dlfull) return;
    }
}

/* the Text tool's box: the writer's layout (PDWRT 20.5.6) - 2 pt of padding, leading
 * 1.2 x the size, lines broken at the last space that fits else inside the word,
 * \n \r \r\n paragraphs, no line below the frame - in the PDRES face of the record's
 * family and style, drawn by the page's own text machinery. Widths are kept as
 * whole thousandths-of-an-em in (wh * 1000 + wl), so the breaks are exactly the writer's */
static pdw font_synth(pdw fam, pdw sty)
{
    pdw i, slot = g_fnnext;
    g_fnnext = (slot + 1) % PDR_NFONT;
    font_flush();
    g_fwi = slot; g_fwdirty = 1;
    g_fk[slot].used = 0;
    pdr_zero(&g_fw, sizeof g_fw);
    g_fw.used = 1; g_fw.ok = 1; g_fw.fam = (pdb)fam; g_fw.sty = (pdb)sty;
    for (i = 0; i < 224; i++) { g_fw.wid[i] = 0xFFFF; g_fw.enc[i] = (pdb)(i + 32); }
    return slot;
}
static void ed_textline(pdw st, pdw n, pdi x, pdi y)
{
    pdw k;
    if (!n) return;
    pdm_mat_ident(&g_tm);
    pdm_pt16_to_fx(&g_tm.e, (pd_pt)x); pdm_pt16_to_fx(&g_tm.f, (pd_pt)y);
    pdr_cpy(&g_tlm, &g_tm, sizeof g_tm); g_tdirty = 1;
    text_begin_show();
    for (k = 0; k < n; k++) text_glyph(ebyte((pdw)(st + k)));
    text_end_show();
}
static void ed_text(void)
{
    /* its 54-byte frame is NOT on the deepest chain (verify-annotr's walker: the
     * annotation phase reaches a strike read 160+ bytes shallower than the page's
     * own text does), so the locals stay locals: statics cost image and bss */
    pdw len = g_ed.text_len, size = g_ed.size ? g_ed.size : 12, s16 = (pdw)(size * 16), lead = (pdw)(s16 + s16 / 5), avail;
    pdw fam = (pdw)(g_ed.style >> 4), fi, i, j, st, brk, end, nxt, eop, cw, c, lw, ox;
    pdw wh, wl, bh, bl, th, tl, lh, ll;
    pdi y;
    if (len > 1024) len = 1024;
    if (!len) return;
    if (fam > 2) fam = 0;
    fi = font_synth(fam, (pdw)(g_ed.style & 3));
    GS.flum = ed_lum[g_ed.colour & 15]; GS.fc4 = (pdb)(g_ed.colour & 15);
    GS.font = (pdb)fi; pdm_fx_int(&GS.fs, (pdi)size); GS.tr = 0; fs_changed();
    avail = (pdw)(g_ed.rect.x1 - g_ed.rect.x0);
    avail = avail > 64u ? (pdw)(avail - 64u) : 0;
    lh = (pdw)(avail / s16); ll = (pdw)pdr_muldiv((pdi)(avail % s16), 1000, (pdi)s16);       /* the limit, avail * 1000 / s16 */
    y = (pdi)(g_ed.rect.y1 - 32 - (pdi)(s16 - s16 / 5));
    for (i = 0; i < len && y >= g_ed.rect.y0 && !g_dlfull; ) {
        st = i; brk = 0xFFFF; eop = 0; end = nxt = 0; wh = wl = bh = bl = 0;
        for (j = i; ; j++) {
            if (j >= len) { end = nxt = j; eop = 1; break; }
            c = ebyte(j);
            if (c == '\n' || c == '\r') {
                end = j; nxt = (pdw)(j + 1); eop = 1;
                if (c == '\r' && nxt < len && ebyte(nxt) == '\n') nxt++;
                break;
            }
            cw = font_width(fi, c < 32 ? 32 : c);
            th = wh; tl = (pdw)(wl + cw);
            while (tl >= 1000) { tl = (pdw)(tl - 1000); th++; }
            if (c == ' ') {
                if (th > lh || (th == lh && tl > ll)) { end = j; nxt = (pdw)(j + 1); break; }
                brk = j; bh = wh; bl = wl;
            } else if (j > st && (th > lh || (th == lh && tl > ll))) {
                if (brk != 0xFFFF) { end = brk; wh = bh; wl = bl; nxt = (pdw)(brk + 1); }
                else { end = j; nxt = j; }
                break;
            }
            wh = th; wl = tl;
        }
        if (!eop) while (nxt < len && ebyte(nxt) == ' ') nxt++;
        lw = (pdw)(wh * s16 + (pdw)pdr_muldiv((pdi)wl, (pdi)s16, 1000));      /* the line's width, pt16 */
        ox = 32;
        if (g_ed.align == 1 && avail > lw) ox = (pdw)(32 + (avail - lw) / 2);
        else if (g_ed.align == 2 && avail > lw) ox = (pdw)(32 + avail - lw);
        ed_textline(st, (pdw)(end - st), (pdi)(g_ed.rect.x0 + ox), y);
        y = (pdi)(y - (pdi)lead);
        i = nxt;
    }
}

/* the store's records for this page: the highlights (post = 0), or everything else (post = 1),
 * from record g_aedi on. A turn draws records until about 1,024 points and text bytes are
 * spent (a record is never split), so a big store is a few slices and not one: 1 = more */
static pdw annot_edits(pdw post)
{
    pdw k, work = 0;
    if (!(g_aflags & PDRF_EDITS) || !g_rq.edits_seg) return 0;
    for (; g_aedi < g_rq.edits_count && !g_dlfull; g_aedi++) {
        if (work >= 1024) return 1;
        work += 4;
        pd_fcopy_in(g_rq.edits_seg, (pdw)(g_aedi * ED_SZ), &g_ed, ED_SZ);
        k = g_ed.kind;
        if (g_ed.page != g_page || (g_ed.flags & PDEF_DELETED) || k == 0 || k > PDA_SIGN) continue;
        if (((g_aflags & PDRF_NOEDITID) && g_aedi == g_rq.hide) || (k == PDA_NOTE && g_ed.irt)) continue;
        if ((k == PDA_HIGHLIGHT) == (post != 0)) continue;
        work += (pdw)(16 + (g_ed.pts_n > 4096 ? 4096 : g_ed.pts_n) + (g_ed.text_len > 1024 ? 1024 : g_ed.text_len));
        gs_reset(); path_reset(); g_plimit = 0;
        if (k == PDA_TEXT) ed_text(); else ed_marks();
    }
    path_reset();
    return 0;
}
