/* ============================================================================
 * os8088 - apps/pagedesk/pdrv.c      PDREND: images at raster time, the pass
 *                                    machine, and the verbs
 *
 * #included by pdrend.c. PAGEDESK-SPEC 10.2, 10.7, 10.8, 15.2, 20.3.
 * ==========================================================================*/

/* ---- images, drawn from the engine's stream as the bands go by ------------ */
static pdi g_icur;                  /* source row held in the buffer            */
static pdw g_iobj, g_igen, g_ibytes, g_iopen;
static pdw g_icodec;                /* the open image is an occupant's (PDX_IMG/JPX), 0 the engine's */
static struct pdxb_img g_xi;        /* IMG_OPEN's answer (20.22.7)              */
static const pdb dpal4[4] = { 0, 8, 7, 15 };

static void img_close(void)
{
    if (!g_iopen) return;
    if (!g_icodec) strm_close();
    else if (x_have(g_icodec)) x_call(PDV_IMG_CLOSE, 0, 0, 0);
    g_iopen = 0;
}
static pdw img_reopen(void)
{
    img_close();
    if (strm_open(g_iobj, g_igen, 0, 0)) return 0;
    g_iopen = 1; g_icur = -1; g_icodec = 0;
    return 1;
}
#ifdef PDR_X8
/* AN IMAGE'S MASK UNDER ONE FLATE SLOT (20.22.7.12): PDX8 opens the image's
 * /SMask (or /Mask) stream while the core holds the image's own, so a Flate
 * image with a Flate mask needs two slots. The core opens the mask once
 * itself to ask: refused, it closes its own stream - the rows are read again
 * from the first (img_reopen) after PDX8 is done - and answers 1. A stack of
 * streams (PDENG's STRM_CLOSE pops), so the probe leaves the image's alone */
static pdw img_mask_park(pdw obj, pdw gen, pdw flags)
{
    pdw n, g;
#ifdef PD_HOST
    if (pdrh_nopark) return 0;
#endif
    if (!g_iopen || g_icodec) return 0;
    pdr_zero(&g_dg, sizeof g_dg);
    g_dg.objnum = obj; g_dg.gen = gen;
    pdr_scpy(g_dg.key[0], (flags & 0x40) ? "Mask" : "SMask", 32);
    if (eng_call(PDV_ENG_DICT_GET, &g_dg, sizeof g_dg, 0, 0, 0) || g_dg.kind != PDK_REF) return 0;
    n = g_dg.refnum; g = g_dg.refgen;
    n = strm_open(n, g, 0, 0);
    if (!n) { strm_close(); return 0; }
    if (n != PDE_LIMIT) return 0;
    img_close();
    return 1;
}
#endif
/* read the next source row into the row buffer; 0 on a short stream */
static pdw img_row(void)
{
    pdw got = 0, r;
    while (got < g_ibytes) {
        if (pdr_eng(PDV_ENG_STRM_READ, g_gseg, got, g_ibytes - got)) return 0;
        r = pdr_eng_ax;
        if (!r) return 0;
        got += r;
    }
    g_icur++;
    return 1;
}
/* IMG_OPEN at the occupant `m` for the record body b (20.22.7); 0 = refused.
 * What it answers is hostile until checked: sizes the DDA takes, and a row
 * of at least the bytes its mode reads, inside the 1 KiB row buffer */
typedef char pdr_ck_imgf[PDIMG_MASK == 1 && PDIMG_INVERT == 2 ? 1 : -1];  /* = the record's flags 1, 2 */
/* A decoder whose workspace is short answers IMG_OPEN with PDE_NEEDMEM, CF
 * clear, DX = the KiB (PDXI's JBIG2 asks 63). That is a raster-time ask for
 * PDMEM_SLOT: the band is drawn again after the primary's answer (step's
 * PDR_RAST arm), once a page view. It used to read as a refusal - "image
 * decoder" and a blank page on a 640 KiB VGA whose slot workspace came out
 * at 21 KiB (QA F15) */
static pdw g_imem, g_imask;             /* asked this band / once a page view */
static pdw img_xopen(pdw m, const pdb *b, pdi wq, pdi hq)
{
    pdw need, r;
    img_close();
    pdr_zero(&g_xi, sizeof g_xi);
    g_xi.objnum = g_iobj; g_xi.gen = g_igen; g_xi.codec = b[19];
    pdr_cpy(&g_xi.w, b + 8, 4);
    g_xi.bpc = b[16]; g_xi.ncomp = b[17];
    g_xi.want_w = (pdw)((wq + 3) >> 2); g_xi.want_h = (pdw)((hq + 3) >> 2);
    g_xi.flags = b[18] & 3;
    pd_fcopy_out(&g_xi, g_xb, 0, sizeof g_xi);
    r = x_call(PDV_IMG_OPEN, g_xb, 0, 0);
    if (x_cf) return 0;
    if (r == PDE_NEEDMEM) {
        if (!g_imask && x_mem(PDMEM_SLOT, x_dx)) g_imem = g_imask = 1;
        return 0;
    }
    pd_fcopy_in(g_xb, 0, &g_xi, sizeof g_xi);
    g_iopen = 1; g_icodec = m; g_icur = -1;
    need = g_xi.out_mode == PDIM_BITS1 ? (pdw)((g_xi.out_w + 7) >> 3)
         : (pdw)(g_xi.out_w * (g_xi.out_mode == PDIM_N8 ? g_xi.out_ncomp : 1));
    if (!g_xi.out_w || g_xi.out_w > 4096 || !g_xi.out_h || g_xi.out_h > 0x7FFF || g_xi.out_mode > PDIM_N8
        || (pdw)(g_xi.out_ncomp - 1) > 3 || need > g_xi.rowbytes || g_xi.rowbytes > 1024) { img_close(); return 0; }
    return 1;
}

/* ---- the image stash (PAGEDESK-SPEC 20.22.7.10) ------------------------------
 * One slot holds one decoder, and a band can need two (a JPEG and a JPX whose
 * rows meet). Before such a band is walked, the decoder IN the slot decodes
 * the rows the band shows of each of its images into the extension cache,
 * above what the cache holds; then the other decoder is asked for and the
 * band is walked once, in the list's order, its images read from the stash.
 * One module load a band, alternating, and the walker is not touched. */
#define PDR_NIK 6                       /* stashed images a band               */
#define IK_BOTH ((1u << PDX_IMG) | (1u << PDX_JPX))
static struct pdr_ik { pdw off, soff, w, h, rb, mode, nc, err; pdi y0, y1; } g_iks[PDR_NIK];
static pdw g_ikn, g_ikb, g_ikdec;       /* entries; band + 1 they are for (0 none); their decoder */
static pdw g_ikask;                     /* the XC size last asked for it, this page view */
static pdw g_bcur;                      /* the band being drawn (the pass machine's)    */
static pdw ik_find(pdw off)
{
    pdw i;
    if (g_ikb != (pdw)(g_bcur + 1)) return PDR_NIK;
    for (i = 0; i < g_ikn; i++) if (g_iks[i].off == off) return i;
    return PDR_NIK;
}

/* one row of the image through pdr_ip: the 8-bit strip's twin, or the band's */
static void img_out(pdi y)
{
#ifdef PDR_X8
    if (g_xs) { x8_imgrow(y); return; }
#endif
    pdr_imgrow();
}
/* a row under a polygon clip (a `W n` that is not a rectangle): the walker's own mask test
 * (shape_clipped's: the row's span in mask 0, each clip polygon ANDed in) and the image
 * row drawn over each run of what is left, its source phase moved to the run's first column */
static void img_clipped(pdi y, pdi x0, pdi x1, pdi delta, pdw W, pdi wq)
{
    pdi x, xs, xe = (pdi)(x1 - g_bx0), xb = (pdi)(x0 - g_bx0);
    pdb *p = g_mk[0];
    mask_clear(0);
    g_rmode = 1;
    span_out(y, x0, x1);
    clips_and(y);
    g_rmode = 0;
    for (x = xb; x <= xe;) {
        if (p[x >> 3] & (0x80 >> (x & 7))) {
            xs = x;
            while (x <= xe && (p[x >> 3] & (0x80 >> (x & 7)))) x++;
            pdr_ip.x0 = xs; pdr_ip.x1 = (pdi)(x - 1);
            pdr_ip.xhi = (pdw)pdr_muldiv((pdi)(delta + (xs - xb) * 4), (pdi)W, wq);
            img_out(y);
        } else x++;
    }
}
static void draw_image(pdw off, pdw len, pdi ry0, pdi ry1)
{
    static pdb b[DLI_BODY];
    static struct pd_u32 u, q;
    pdi xl, yt, xr, yb, wq, hq, y, sr, x0, x1, delta;
    pdw W, H, bpc, ncomp, flags, rb, sm, obj, gen, inl, m, k = PDR_NIK, mode, onc;
    (void)len;
    pd_fcopy_in(g_dls, off + DLH, b, DLI_BODY);
    xl = r16(b); yt = r16(b + 2); xr = r16(b + 4); yb = r16(b + 6);
    W = (pdw)r16(b + 8); H = (pdw)r16(b + 10);
    obj = (pdw)r16(b + 12); gen = (pdw)r16(b + 14);
    bpc = b[16]; ncomp = b[17]; flags = b[18];
    m = b[19] ? (b[19] == PDCD_JPX ? PDX_JPX : PDX_IMG) : 0;    /* a codec: the occupant decodes it */
    inl = flags & 16;                          /* an inline image: its rows follow the body in the list */
    if (!inl && (!g_gseg || g_gkb < 2)) { inc_note(PDF_INC_IMAGE, "no image buffer"); return; }
    wq = (pdi)(xr - xl); hq = (pdi)(yb - yt);
    if (wq < 1) wq = 1;
    if (hq < 1) hq = 1;
    if (!W || !H || W > 4096 || H > 0x7FFF) return;
    if (m) {
        k = ik_find(off);
        if (k < PDR_NIK) {                     /* decoded before the walk (20.22.7.10) */
            if (g_iks[k].err) {
                if (g_iks[k].err == 1) inc_note(PDF_INC_IMAGE, "image decoder");
                else if (g_iks[k].err == 2) inc_note(PDF_INC_LIMIT, "image stash");
                return;
            }
            W = g_iks[k].w; H = g_iks[k].h; rb = g_iks[k].rb; mode = g_iks[k].mode; onc = g_iks[k].nc;
        } else {
            if (!x_have(m)) {                  /* refused; or no room to stash the band's other decoder */
                if (g_ikb == (pdw)(g_bcur + 1) && g_ikdec == m) inc_note(PDF_INC_LIMIT, "image stash");
                else inc_note(PDF_INC_MODULE, x_name[m]);
                return;
            }
            if (!g_iopen || g_icodec != m || g_iobj != obj || g_igen != gen) {
                g_iobj = obj; g_igen = gen;
                if (!img_xopen(m, b, wq, hq)) { if (!g_imem) inc_note(PDF_INC_IMAGE, "image decoder"); return; }
            }
            W = g_xi.out_w; H = g_xi.out_h; rb = g_xi.rowbytes; mode = g_xi.out_mode; onc = g_xi.out_ncomp;
        }
        sm = mode == PDIM_GREY8 ? 1 : mode == PDIM_BITS1 ? ((flags & 1) ? 3 : 0) : 4;
        if (onc != ncomp) flags &= ~8;         /* a LUT is for the space's own samples */
        ncomp = onc; bpc = 8;
        flags &= ~2;                           /* the occupant applied /Decode */
    } else {
        /* the row in bytes, without a 16-bit product that can wrap: W x ncomp
         * <= 16,384 samples of 1 to 16 bits (do_image's bounds) */
        rb = (pdw)(W * ncomp);
        rb = (pdw)((rb >> 3) * bpc + (((rb & 7) * bpc + 7) >> 3));
        if (rb > 1024) { inc_note(PDF_INC_LIMIT, "image row size"); return; }
        /* the stream stays open from band to band while the same image is drawn
         * further down: each band reads on from the row the last one stopped at,
         * instead of decoding the image from its first row again */
        if (!inl && (!g_iopen || g_icodec || g_iobj != obj || g_igen != gen || g_ibytes != rb)) {
            g_iobj = obj; g_igen = gen; g_ibytes = rb;
            if (!img_reopen()) { inc_note(PDF_INC_FILTER, "image stream"); return; }
        }
        sm = (flags & 1) ? 3 : ((flags & 8) || ncomp == 4 || (bpc != 8 && bpc != 1)) ? 4
           : (ncomp == 3 ? 2 : (bpc == 1 ? 0 : 1));
    }
    /* destination columns: first/last pixel the rectangle and the clip share */
    x0 = q_lo(xl); x1 = q_hi(xr);
    if (x1 < x0) x1 = x0;
    if (x0 < g_scx0) x0 = g_scx0;
    if (x1 > g_scx1) x1 = g_scx1;
    if (x0 < g_bx0) x0 = g_bx0;
    if (x1 > (pdi)(g_bx0 + g_bw - 1)) x1 = (pdi)(g_bx0 + g_bw - 1);
    if (x1 < x0) return;
    delta = (pdi)(x0 * 4 + 2 - xl);
    if (delta < 0) delta = 0;
    pdm_u32_set(&u, (pdw)(4 * W), 0);
    pdm_u32_divmod16(&q, &u, (pdw)wq);
#ifdef PDR_X8
    if (g_xs) {
        pdw rp = (flags & 0x60) && !m && !inl && img_mask_park(obj, gen, flags);
        x8_imgset(b, flags, xl, yt, wq, hq, x0, x1, ry0, ry1);   /* blend: this strip's image mask */
        if (rp && !img_reopen()) { inc_note(PDF_INC_FILTER, "image stream"); return; }
    }
#endif
    pdr_ip.nc = ncomp; pdr_ip.bpc = bpc;
    pdr_ip.lseg = (flags & 8) ? g_xcseg : 0; pdr_ip.loff = (pdw)r16(b + 24);
    pdr_ip.lflags = (flags & 8) ? b[26] : (ncomp == 4);
    for (y = ry0; y <= ry1; y++) {
        pdi qy = (pdi)(y * 4 + 2);
        if (y < g_scy0 || y > g_scy1) continue;
        if (qy <= yt) sr = 0;
        else if (qy >= yb) sr = (pdi)(H - 1);
        else sr = pdr_muldiv((pdi)(qy - yt), (pdi)H, hq);
        if (sr >= (pdi)H) sr = (pdi)(H - 1);
        if (k < PDR_NIK) {
            if (y < g_iks[k].y0 || y > g_iks[k].y1) continue;
        } else if (m) {
            if (sr < g_icur && !img_xopen(m, b, wq, hq)) return;
            if (sr != g_icur) {
                x_call(PDV_IMG_ROW, g_gseg, 0, (pdw)sr);
                if (x_cf) { img_close(); inc_note(PDF_INC_IMAGE, "image decoder"); return; }
                g_icur = sr;
            }
        } else if (!inl) {
            if (sr < g_icur) { if (!img_reopen()) return; }
            while (g_icur < sr) if (!img_row()) { img_close(); return; }
            if (g_icur < 0 && !img_row()) { img_close(); return; }
        }
        {
            pdi s0 = pdr_muldiv(delta, (pdi)W, wq);
            pdr_ip.bseg = g_bseg;
            pdr_ip.boff = (pdw)(y - g_by0) * g_bstride;
            pdr_ip.x0 = (pdi)(x0 - g_bx0); pdr_ip.x1 = (pdi)(x1 - g_bx0);
            if (inl) { pdr_ip.sseg = g_dls; pdr_ip.soff = (pdw)(off + DLH + DLI_BODY + (pdw)sr * rb); }
            else if (k < PDR_NIK) { pdr_ip.sseg = g_xcseg; pdr_ip.soff = (pdw)(g_iks[k].soff + (pdw)(y - g_iks[k].y0) * rb); }
            else { pdr_ip.sseg = g_gseg; pdr_ip.soff = 0; }
            pdr_ip.sw = W;
            pdr_ip.xlo = 0; pdr_ip.xhi = (pdw)s0;
            pdr_ip.slo = q.lo; pdr_ip.shi = q.hi;
            pdr_ip.smode = sm | ((flags & 4) ? 0x100 : 0);
            pdr_ip.inv = (flags & 2) ? 1 : 0;
            pdr_ip.dmode = g_bpp;
            pdr_ip.dyrow = (pdw)(y & 3);
            pdr_ip.col = g_bpp == 1 ? (g_spat[0] & 1) : g_sc4;
            if (g_sclid < PDR_NCLIP && g_mkok) img_clipped(y, x0, x1, delta, W, wq);   /* a polygon clip */
            else img_out(y);
        }
    }
}

/* ============================================================================
 * THE PASS MACHINE
 * ==========================================================================*/
static pdw g_ph;                    /* PDR_*                                    */
static pdw g_nb, g_b0, g_b1, g_gw;   /* g_bcur: above, with the image stash */
static struct pdxb_render g_rq;
static struct pdxb_init g_ini;
static struct pdxb_textmap g_tmq;
static struct pdxb_thumb g_thq;
static struct pdxb_pageinfo g_pi;
static pdw g_flatscale;

/* ---- the ladder (PAGEDESK-SPEC 20.22.2 L2, 20.22.7) ------------------------
 * ONE answer at a time: a region (NEEDMEM) first, then the highest-numbered
 * module needed (fonts before graphics, so the graphics module - whose dash
 * work is never cached - is the one left resident), and the pass is run
 * again after each. A codec image asks before the first band it touches is
 * drawn. A page view makes at most PDR_XASKS answers. */
static pdw x_ask;                   /* the last answer: PDX_*, or 0x80 | PDMEM_*; 0 none */
static pdw x_asks;                  /* answers this page view                   */
static pdw g_xph;                   /* after it: PDR_INTERP re-runs the pass, PDR_RAST draws the band */
static pdw g_xclost;                /* the XC went from under an open view (20.22.7.11): re-run the pass */
static pdw x_answer(pdw what, pdw cx, pdw dx, pdw ph)
{
    x_ask = what; pdr_out_cx = cx; pdr_out_dx = dx; x_asks++;
    g_xph = ph; g_ph = PDR_XWAIT;
    return (what & 0x80) ? PDE_NEEDMEM : PDE_NEEDMOD;
}
static void ik_size(void);
/* the end of an interpretation pass: something to ask for, or 0 */
static pdw g_tmref;                 /* x_ref bits a text pass keeps until the next render (QA R17) */
static pdw x_resolve(void)
{
    pdw id, w = x_pend & ~x_ref;
    if ((g_xrm & IK_BOTH) == IK_BOTH) ik_size();
    /* a text pass needs only the font module and the glyph claim: colour, shadings and images draw no
     * text, and asking for them on every page of a Find swapped PDXG and PDXF per page until the job's
     * PDU_LADMAX ("exceeds one of PageDesk's limits", minutes on QEMU: QA R17) */
    if (g_mode == PDR_TEXT) {
        w &= (pdw)(1u << PDX_FNT);
        if (x_mwant != (0x80 | PDMEM_GLYPH) && x_mwant != (0x80 | PDMEM_SLOT)) x_mwant = 0;
    }
    if (x_mwant && !(x_ref & XR_MEM)) {
        if (x_asks < PDR_XASKS) return x_answer(x_mwant, (pdw)(x_mwant & 0x7F), x_mkb, PDR_INTERP);
        x_ref |= XR_MEM;
    }
    for (id = PDX_COUNT - 1; id; id--) if (w & (1u << id)) {
        if (x_asks < PDR_XASKS) return x_answer(id, 0, id, PDR_INTERP);
        x_refuse(id);
    }
    return 0;
}
/* the primary has answered: what did not come is refused for the page view */
static void x_answered(void)
{
    pdw a = x_ask;
    x_ask = 0; x_mwant = 0;
    /* a refused XC or X8 refuses regions for the page view; the glyph claim (fonts, 20.22.8) is refused
     * alone, and PDMEM_SLOT is the occupant's own question (it asks once per stream or font) */
    if (a & 0x80) {
        if (a == (0x80 | PDMEM_GLYPH)) {
            if (!syn_cap() || g_gkb < x_mkb) { x_ref |= XR_GLYPH; g_gnogrow = g_gkb; }   /* not the size asked: not again at this size */
        }
        else if (a != (0x80 | PDMEM_SLOT) && !(a == (0x80 | PDMEM_XC) ? g_ini.xc_seg : g_ini.x8_seg)) x_ref |= XR_MEM;
    }
    else if (a && !x_have(a)) { x_refuse(a); if (g_mode == PDR_TEXT) g_tmref |= (pdw)(1u << a); }   /* not asked again page by page */
}
/* the stash (20.22.7.10): the resident decoder `d` decodes, into the XC above
 * g_xctop, the rows y0..y1 show of each of its images, one row per device row
 * (the row draw_image would ask for). A failed open is err 1 (drawn as
 * "image decoder"), no room err 2 ("image stash", PDF_INC_LIMIT), a record
 * draw_image would not draw err 3 */
static void ik_fill(pdw d, pdi y0, pdi y1)
{
    static pdb h[DLH], b[DLI_BODY];
    static struct pdr_ik *e;
    pdw off = 0, len, top = (pdw)((g_xctop + 1) & ~1u), n, H, W;
    pdi wq, hq, y, sr, last, yt, yb;
    g_ikn = 0; g_ikb = (pdw)(g_bcur + 1); g_ikdec = d;
    while (off < g_dllen && g_ikn < PDR_NIK) {
        pd_fcopy_in(g_dls, off, h, DLH);
        len = (pdw)r16(h + 2);
        if (len < DLH || len > g_dllen - off) break;
        if (h[0] == DLR_IMAGE && r16(h + 6) >= y0 && r16(h + 4) <= y1
            && (n = pd_fpeekb(g_dls, (pdw)(off + DLH + 19))) != 0 && (n == PDCD_JPX ? PDX_JPX : PDX_IMG) == d) {
            pd_fcopy_in(g_dls, off + DLH, b, DLI_BODY);
            e = &g_iks[g_ikn++];
            pdr_zero(e, sizeof *e);
            e->off = off; e->err = 3;
            e->y0 = r16(h + 4) < y0 ? y0 : r16(h + 4);
            e->y1 = r16(h + 6) > y1 ? y1 : r16(h + 6);
            yt = r16(b + 2); yb = r16(b + 6);
            wq = (pdi)(r16(b + 4) - r16(b)); hq = (pdi)(yb - yt);
            if (wq < 1) wq = 1;
            if (hq < 1) hq = 1;
            W = (pdw)r16(b + 8); H = (pdw)r16(b + 10);
            if (!W || !H || W > 4096 || H > 0x7FFF || (b[18] & 16)) goto next;
            g_iobj = (pdw)r16(b + 12); g_igen = (pdw)r16(b + 14);
            e->err = 1;
            if (!img_xopen(d, b, wq, hq)) { if (g_imem) e->err = 3; goto next; }
            e->w = g_xi.out_w; e->h = H = g_xi.out_h; e->rb = g_xi.rowbytes;
            e->mode = g_xi.out_mode; e->nc = g_xi.out_ncomp;
            n = (pdw)(e->y1 - e->y0 + 1);
            e->err = 2;
            if (!g_xcseg || g_xccap < top || (pdw)(g_xccap - top) / n < e->rb) { img_close(); goto next; }
            e->soff = top; e->err = 0; last = -1;
            for (y = e->y0; y <= e->y1; y++) {
                pdi qy = (pdi)(y * 4 + 2);
                if (qy <= yt) sr = 0;
                else if (qy >= yb) sr = (pdi)(H - 1);
                else sr = pdr_muldiv((pdi)(qy - yt), (pdi)H, hq);
                if (sr >= (pdi)H) sr = (pdi)(H - 1);
                if (sr == last) pd_fcopy(g_xcseg, top, g_xcseg, (pdw)(top - e->rb), e->rb);
                else {
                    x_call(PDV_IMG_ROW, g_xcseg, top, (pdw)sr);
                    if (x_cf) { e->err = 1; break; }
                    last = sr;
                }
                top = (pdw)(top + e->rb);
            }
            img_close();
            top = (pdw)((top + 1) & ~1u);
        }
    next:
        off = (pdw)(off + len);
    }
}
/* the XC the stash may need (20.22.7.10), asked at the end of a pass that
 * marked both decoders: the larger decoder's bound above what the XC holds */
static void ik_size(void)
{
    pdw kb = g_iknd[0] > g_iknd[1] ? g_iknd[0] : g_iknd[1];
    kb = (pdw)(kb + ((g_xcseg ? g_xctop : 0) + 1023 >> 10));
    if (kb > 63) kb = 63;
    if (kb > (g_xcseg ? g_xccap >> 10 : 0) && kb != g_ikask) { g_ikask = kb; x_mem(PDMEM_XC, kb); }
}
/* before band g_bcur is drawn: the occupant its codec images need (20.22.7).
 * The band's DLR_IMAGE records say which; when the resident occupant is one
 * of them the band is drawn with it, else the lowest is asked for. A band
 * whose images need BOTH decoders (20.22.7.10) has the resident one's
 * stashed, then the other asked for: one load a band, outside PDR_XASKS
 * (each makes a band's progress), and the walk draws them all in order */
static pdw x_rneed(void)
{
    static pdb h[DLH];
    pdw off = 0, need = 0, len, id, r;
    pdi y0 = (pdi)(g_vcy0 + (pdi)g_bcur * PD_BANDROWS), y1 = (pdi)(y0 + PD_BANDROWS - 1);
    if (!g_xrm) return 0;
    while (off < g_dllen) {
        pd_fcopy_in(g_dls, off, h, DLH);
        len = (pdw)r16(h + 2);
        if (len < DLH || len > g_dllen - off) break;
        if (h[0] == DLR_IMAGE && r16(h + 6) >= y0 && r16(h + 4) <= y1 && (id = pd_fpeekb(g_dls, (pdw)(off + DLH + 19))) != 0)
            need |= (pdw)(1u << (id == PDCD_JPX ? PDX_JPX : PDX_IMG));
        off = (pdw)(off + len);
    }
    need &= ~x_ref;
    if (g_ikb == (pdw)(g_bcur + 1)) need &= ~(1u << g_ikdec);    /* stashed for this band */
    if (need == IK_BOTH && ((need >> x_res) & 1)) {
        id = x_res == PDX_IMG ? PDX_JPX : PDX_IMG;
        if (!g_xcseg) {                 /* nowhere to stash: the resident draws, the other is noted */
            g_ikn = 0; g_ikb = (pdw)(g_bcur + 1); g_ikdec = id;
            return 0;
        }
        ik_fill(x_res, y0, y1);
        img_close();
        r = x_answer(id, 0, id, PDR_RAST);
        x_asks--;
        return r;
    }
    if (!need || (need >> x_res) & 1) return 0;
    id = (need & (1u << PDX_IMG)) ? PDX_IMG : PDX_JPX;
    if (x_asks >= PDR_XASKS) { x_refuse(id); return 0; }
    img_close();
    return x_answer(id, 0, id, PDR_RAST);
}
static void close_streams(void)
{
    img_close();
    while (g_nfr) { if (!g_fr[g_nfr].park) strm_close(); g_fr[g_nfr].park = 0; g_fr[g_nfr].ok = 0; g_nfr--; }
    if (g_pgopen && !g_fr[0].park) strm_close();
    g_fr[0].park = 0;
    g_pgopen = 0; g_idone = 1; g_await = 0;
}

/* a pass interprets the content once for the rows of bands [g_b0, g_b1) */
static pdw g_subm, g_subm_w;            /* sub-row mode: one band, composed in pieces */
static pdw g_gceil, g_sceil;            /* group_grow's ceilings: bands / rows of a band in pieces */
static pdw pass_begin(void)
{
    img_close();                        /* a held image is not the top stream any more */
    g_eylo = (pdi)(g_gy0 * 4); g_eyhi = (pdi)(g_gy1 * 4 + 3);
    g_dllen = 0; g_dlfull = 0; g_lstv = 0; g_ndl = 0; g_ncl = 0;
#ifdef PD_HOST
    { extern long pdrh_clipnow, pdrh_clipn; pdrh_clipnow = 0; pdrh_clipn = 0; }
#endif
    x_pend = 0; x_mwant = 0; g_xrm = 0; g_xcfull = 0;
    g_iknd[0] = g_iknd[1] = 0; g_ikb = 0;
    g_passes++; g_pstamp++;
    gs_init();
    xb_pass();
    g_ph = PDR_INTERP;
    return interp_start(g_rq.page);
}
static pdw pass_start(void)
{
    pdi last = (pdi)(g_vcy0 + (pdi)g_b1 * PD_BANDROWS - 1);
    g_subm = 0;
    g_gy0 = (pdi)(g_vcy0 + (pdi)g_b0 * PD_BANDROWS);
    g_gy1 = last > g_vcy1 ? g_vcy1 : last;
    return pass_begin();
}

static void view_base(const struct pd_view *v)
{
    static struct pd_fx sx, sy, a, b;
    pdm_fx_int(&a, (pdi)(v->zoom * 4)); pdm_fx_int(&b, 100);
    pdm_fx_div(&sx, &a, &b);
    pdm_fx_int(&a, (pdi)v->asp_num); pdm_fx_int(&b, (pdi)v->asp_den);
    pdm_fx_mul(&sy, &sx, &a);
    pdm_fx_div(&sy, &sy, &b);
    base_build(&v->box, v->rot, &sx, &sy, v->ox, v->oy);
}

static void mode_reset(void)
{
    g_inc = 0; g_incname[0] = 0; g_ops = 0; g_passes = 0;
    g_ierr = 0; g_ndl = 0;
    g_pkfam = 0xFF; g_aflags = 0;
    x_ref = 0; x_asks = 0; x_ask = 0; x_used = 0; x_swaps = 0; g_xcflush = 0; g_ikask = 0;
    g_fioerr = 0;                       /* the strikes are read again (QA F21) */
    g_imem = g_imask = 0;
}

static pd_px vb_w, vb_h;
static pdw verb_begin(void)
{
    pdw err;
    pd_fcopy_in(g_xb, 0, &g_rq, sizeof g_rq);
    if (!g_dls || !g_bseg) return PDE_NOMEM;
    close_streams();
    mode_reset(); g_tmref = 0;
    g_mode = PDR_INTERP; g_thumb = 0; g_bpp = g_ini.bpp == 4 ? 4 : 1;
    g_aflags = g_rq.rflags;
    g_winoff = g_rq.win_off;
    g_vcx0 = g_rq.view.clip_x0; g_vcy0 = g_rq.view.clip_y0;
    g_vcx1 = g_rq.view.clip_x1; g_vcy1 = g_rq.view.clip_y1;
    g_ydone = g_vcy0;
    if (g_vcx1 < g_vcx0 || g_vcy1 < g_vcy0) { g_ph = PDR_DONE; return PDE_OK; }
    pdm_view_size(&vb_w, &vb_h, &g_rq.view);
    g_pgx0 = g_rq.view.ox; g_pgy0 = g_rq.view.oy;
    g_pgx1 = (pdi)(g_pgx0 + vb_w - 1); g_pgy1 = (pdi)(g_pgy0 + vb_h - 1);
    if (!band_geometry()) return PDE_NOMEM;
    x8_view();
    view_base(&g_rq.view);
    g_nb = band_count();
    g_gw = g_nb; g_b0 = 0; g_b1 = g_nb; g_gceil = g_nb;
    g_deskwhite = 0;
    err = pass_start();
    if (err) { g_ph = PDR_DONE; return err; }
    return PDE_OK;
}

/* how many units (bands, or rows of a band composed in pieces) from g_gy0
 * the next pass should take, read off the list that just filled (QA R15).
 * Every record carries its rows, so the bytes a pass over the first k units
 * would have held SO FAR are those of the records whose first row is in
 * them; the rest of the page adds to that, so the pass takes the most units
 * whose share is at most half the list - never more than half of n
 * (halving's own progress) and never less than one. Halving alone took
 * SYNTH.PDF's 7-band group to one band in three wasted passes, and its
 * dense band to one row in four more. */
#define GP_MAX 32
#define g_gpb ((pdw *)g_iks)                /* the image stash's room: read only while a band is drawn,
                                             * and the pass_begin after this clears it (g_ikb) */
typedef char pdr_ck_gpb[GP_MAX * 2 <= sizeof g_iks ? 1 : -1];
static pdw group_plan(pdw n, pdw unit)
{
    static pdb h[DLH];
    pdw off = 0, len, k, sum, lim, st = 0;
    pdi y;
    lim = (n + 1) >> 1;
    if (lim > GP_MAX) lim = GP_MAX;
    pdr_zero(g_gpb, GP_MAX * 2);
    while (off < g_dllen) {
        pd_fcopy_in(g_dls, off, h, DLH);
        len = (pdw)r16(h + 2);
        if (len < DLH || len > g_dllen - off) break;
        off += len;
        if (h[0] == DLR_STATE) { st += len; continue; }     /* emitted for the mark after it: its rows */
        y = r16(h + 4);
        k = y < g_gy0 ? 0 : (pdw)((pdw)(y - g_gy0) / unit);
        if (k < GP_MAX) g_gpb[k] += len + st;
        st = 0;
    }
    sum = 0;
    for (k = 0; k < lim; k++) {
        sum += g_gpb[k];
        if (k && sum > (g_dlcap >> 1)) break;
    }
    return k ? k : 1;
}
/* a pass that left most of the list unused: the next one is wider (QA R15),
 * but never past the ceiling - half the last width that filled the list -
 * which only a pass that used under a quarter of it AT the ceiling raises:
 * growing straight back to a width that overflowed traded every pass it
 * saved for a wasted one */
static pdw group_grow(pdw w, pdw most, pdw *ceil)
{
    if (g_dllen < (g_dlcap >> 2)) {
        if (w >= *ceil) *ceil <<= 1;
        w <<= 2;
    } else if (g_dllen < (g_dlcap >> 1)) w <<= 1;
    if (w > *ceil) w = *ceil;
    return w > most ? most : w;
}

#ifdef PD_HOST
extern int pdrh_dlhist;
#include <stdio.h>
static void dl_hist(const char *why)
{
    static pdb h[DLH];
    pdw off = 0, len; long n[16] = {0}, b[16] = {0};
    if (!pdrh_dlhist) return;
    while (off < g_dllen) {
        pd_fcopy_in(g_dls, off, h, DLH);
        len = (pdw)r16(h + 2);
        if (len < DLH || len > g_dllen - off) break;
        if (pdrh_dlhist > 1 && n[0] < 400) {
            static pdb bb[22]; pdw q, m = len - DLH > 22 ? 22 : len - DLH;
            pd_fcopy_in(g_dls, off + DLH, bb, m);
            fprintf(stderr, "REC %u %u %u %d-%d ", off, h[0], h[1], r16(h + 4), r16(h + 6));
            for (q = 0; q < m; q++) fprintf(stderr, "%02x", bb[q]);
            fprintf(stderr, "\n"); n[0]++;
        }
        n[h[0] & 15]++; b[h[0] & 15] += len; off += len;
    }
    fprintf(stderr, "DLHIST %s pass=%u b=%u-%u gy=%d-%d subm=%u w=%u len=%u cap=%u ops=%u st %ld/%ld rect %ld/%ld fill %ld/%ld gly %ld/%ld img %ld/%ld x %ld/%ld\n",
        why, g_passes, g_b0, g_b1, g_gy0, g_gy1, g_subm, g_subm_w, g_dllen, g_dlcap, g_ops,
        n[1], b[1], n[2], b[2], n[3], b[3], n[4], b[4], n[5], b[5], n[6] + n[7] + n[8] + n[9], b[6] + b[7] + b[8] + b[9]);
}
#define DL_HIST(w) dl_hist(w)
#else
#define DL_HIST(w)
#endif
/* the DL filled up: narrow the group and rescan (PAGEDESK-SPEC 20.3.2) */
static void group_narrow(void)
{
    pdw n = g_b1 - g_b0, rows;
    if (n > 1) {
        g_gceil = (n + 1) >> 1;
        g_gw = group_plan(n, PD_BANDROWS);
        close_streams();
        g_b1 = g_b0 + g_gw;
        pass_start();
    } else if (g_subm ? g_subm_w > 1 : (g_gy1 - g_gy0) > 0) {
        /* one band and still too much: compose it in sub-passes of fewer rows,
         * onto the same band buffer, and blit once (PAGEDESK-SPEC 20.3.2) */
        rows = g_subm ? g_subm_w : (pdw)(g_gy1 - g_gy0 + 1);
        g_sceil = (rows + 1) >> 1;
        g_subm_w = group_plan(rows, 1);
        close_streams();
        if (!g_subm) { g_subm = 1; band_open(g_b0); band_init(); }
        g_gy1 = (pdi)(g_gy0 + g_subm_w - 1);
        pass_begin();
    } else {
        close_streams();
        inc_note(PDF_INC_LIMIT, "display list full");
        g_ph = PDR_RAST; g_bcur = g_b0;
    }
}

static pdw verb_step(void)
{
    pdw done, err;
    if (g_ph == PDR_DONE || g_ph == PDR_IDLE) return PDE_OK;
    if (g_ph == PDR_XWAIT) {            /* the primary answered (20.22.7) */
        x_answered();
        g_ph = g_xph;
        if (g_ph == PDR_INTERP || g_xclost) {   /* lost XC: nothing in the list may name it */
            g_xclost = 0;
            err = pass_begin();
            if (err) { g_ph = PDR_DONE; return err; }
            return PDE_BUSY;
        }
    }
    if (g_ph == PDR_INTERP) {
        done = interp_run(PD_SLICE_OPS);
        if (g_dlfull) { DL_HIST("full"); group_narrow(); return PDE_BUSY; }
        if (done) {
            DL_HIST("done");
            close_streams();
            xb_needmem();                                   /* blend: PDX8 for a transparent page (20.22.10.2) */
            /* the extension cache filled: empty it and run the pass again, once a view */
            if (g_xcfull && !g_xcflush) {
                g_xcflush = 1; g_xctop = 0;
                err = pass_begin();
                if (err) { g_ph = PDR_DONE; return err; }
                return PDE_BUSY;
            }
            if (g_xcfull) inc_note(PDF_INC_LIMIT, "extension cache");
            err = x_resolve();
            if (err) return err;
            g_ph = PDR_RAST; g_bcur = g_b0;
        }
        return PDE_BUSY;
    }
    if (g_ph == PDR_RAST && (err = x_rneed()) != 0) return err;
    if (g_ph == PDR_RAST && g_subm) {
        /* one sub-range of the band is interpreted: draw its rows into the band */
            g_wlo = g_gy0; g_whi = g_gy1;
        walk_band(0, 0);
        if (g_gy1 >= (pdi)(g_by0 + g_brows - 1)) {
            g_wlo = g_by0; g_whi = (pdi)(g_by0 + g_brows - 1);
            band_present();
            g_subm = 0;
            g_bcur = g_b1;                      /* the group (one band) is done */
        } else {
            pdi hi = (pdi)(g_by0 + g_brows - 1);
            g_subm_w = group_grow(g_subm_w, g_brows, &g_sceil);
            g_gy0 = (pdi)(g_gy1 + 1);
            g_gy1 = (pdi)(g_gy0 + g_subm_w - 1);
            if (g_gy1 > hi) g_gy1 = hi;
            err = pass_begin();
            if (err) { g_ph = PDR_DONE; return err; }
            return PDE_BUSY;
        }
        if (g_bcur >= g_b1) goto groupend;
        return PDE_BUSY;
    }
    if (g_ph == PDR_RAST) {
        band_draw(g_bcur);
        if (g_imem) {                   /* a decoder asked for workspace: this band again after the answer */
            g_imem = 0;
            if (x_asks < PDR_XASKS) { img_close(); return x_answer(x_mwant, 0, x_mkb, PDR_RAST); }
            inc_note(PDF_INC_IMAGE, "image decoder");
        }
        g_bcur++;
        if (g_bcur >= g_b1) {
        groupend:
            if (g_b1 >= g_nb) { img_close(); g_ph = PDR_DONE; return PDE_OK; }
            g_gw = group_grow(g_gw, g_nb, &g_gceil);      /* a narrowed width held for the rest of the page */
            g_b0 = g_b1;
            g_b1 = g_b0 + g_gw; if (g_b1 > g_nb) g_b1 = g_nb;
            err = pass_start();
            if (err) { g_ph = PDR_DONE; return err; }
        }
        return PDE_BUSY;
    }
    return PDE_OK;
}

/* 20.19.1: CX = 1 also writes, as XB word 0, the first view row the render
 * did not blit (its clip's top when none was): the primary lays paper from
 * there instead of leaving the old pixels under a cancelled page */
static pdw verb_abort(pdw cx)
{
    if (cx) pd_fcopy_out(&g_ydone, g_xb, 0, 2);
    close_streams();
    g_ph = PDR_IDLE; g_mode = PDR_IDLE;
    return PDE_OK;
}

static pdw verb_status(void)
{
    static struct pdxb_rstatus rs;
    pdr_zero(&rs, sizeof rs);
    rs.incomplete = g_inc;
    pdr_scpy(rs.first, g_incname, 32);
    rs.ops = g_ops; rs.passes = g_passes;
    rs.mods = x_used; rs.swaps = x_swaps;
    pd_fcopy_out(&rs, g_xb, 0, sizeof rs);
    return PDE_OK;
}

/* ---- the text map ----------------------------------------------------------- */
/* A text pass that needs a module (20.22.7) answers before it hands over the
 * slice that skipped the work, and is then run again from the page's start,
 * dropping the g_tmdel glyphs the primary already has: they came out before
 * the need, so the second pass makes the same ones */
static pdw g_tmdel, g_tmskip, g_tmpg;
static pdw tm_start(void)
{
    static struct pd_fx s16;
    close_streams();
    g_tmpg = g_tmq.page; g_tmunread = 0;
    g_mode = PDR_TEXT; g_ph = PDR_TEXT;
    g_tmwr = g_tmrd = g_tmrun = 0; g_tmoverflow = 0; g_tmfirst = 1;
    pdm_fx_int(&s16, 16);
    pdr_zero(&g_base, sizeof g_base);
    pdr_cpy(&g_base.a, &s16, 4); pdr_cpy(&g_base.d, &s16, 4);
    g_vcx0 = 0; g_vcy0 = 0; g_vcx1 = 32000; g_vcy1 = 32000;
    g_pgx0 = g_pgy0 = -32000; g_pgx1 = g_pgy1 = 32000;
    g_gy0 = -32000; g_gy1 = 32000;
    gs_init();
    GS.cx0 = GS.cy0 = -32000; GS.cx1 = GS.cy1 = 32000;
    x_pend = 0; x_mwant = 0;
    return interp_start(g_tmq.page);
}
static pdw verb_textmap(void)
{
    pdw n, room, err;
    pd_fcopy_in(g_xb, 0, &g_tmq, sizeof g_tmq);
    if (!g_dls) return PDE_NOMEM;
    if (g_tmq.dmax < 12) return PDE_RANGE;          /* no room for one record: it would never finish */
    err = 0;
    /* a slice the ladder answered (20.22.2.6) comes back with the state the primary
     * sent first: the pass waiting for its module on this page goes on (QA R17) */
    if (g_tmq.state == 0 && !(g_ph == PDR_XWAIT && g_mode == PDR_TEXT && g_tmq.page == g_tmpg)) {
        mode_reset(); x_ref = g_tmref;
        g_tmdel = g_tmskip = 0;
        err = tm_start();
    } else if (g_ph == PDR_XWAIT) {
        x_answered();
        g_tmskip = g_tmdel;
        err = tm_start();
    }
    g_tmq.state = 1;
    if (err) { g_tmq.done = 1; g_tmq.count = 0; pd_fcopy_out(&g_tmq, g_xb, 0, sizeof g_tmq); return err; }
    if (g_ph != PDR_TEXT) return PDE_CANCEL;
    /* drain what is waiting, else interpret one more slice */
    if (g_tmrd >= g_tmwr && !g_idone) {
        g_tmrd = g_tmwr = 0;
        interp_run(PD_SLICE_OPS);
        if ((err = x_resolve()) != 0) {
            g_tmq.count = 0; g_tmq.done = 0;
            pd_fcopy_out(&g_tmq, g_xb, 0, sizeof g_tmq);
            return err;
        }
    }
    n = g_tmwr - g_tmrd;
    if (g_tmskip) { if (n > g_tmskip) n = g_tmskip; g_tmrd += n; g_tmskip -= n; }
    room = g_tmq.dmax / 12;
    n = g_tmwr - g_tmrd;
    if (n > room) n = room;
    if (n) {
        pd_fcopy(g_tmq.dseg, g_tmq.doff, g_dls, (pdw)((g_tmrd << 3) + (g_tmrd << 2)), (pdw)((n << 3) + (n << 2)));
        g_tmrd += n; g_tmdel += n;
    }
    g_tmq.count = n;
    g_tmq.done = (g_idone && g_tmrd >= g_tmwr) ? (pdw)(1 | g_tmunread) : 0;
    if (g_tmq.done) { close_streams(); g_ph = PDR_IDLE; g_mode = PDR_IDLE; }
    if (g_tmoverflow) inc_note(PDF_INC_LIMIT, "text map");
    pd_fcopy_out(&g_tmq, g_xb, 0, sizeof g_tmq);
    return PDE_OK;
}

/* ---- thumbnails ------------------------------------------------------------- */
static struct pd_view g_tv;
static pdw verb_thumb(void)
{
    pdw err, dw, dh, zw, zh;
    pdi pw, ph;
    pd_fcopy_in(g_xb, 0, &g_thq, sizeof g_thq);
    if (g_thq.state == 0) {
        if (!g_dls || !g_bseg || !g_thq.w || !g_thq.h || g_thq.w > 720 || g_thq.h > 720) return PDE_NOMEM;
        close_streams();
        mode_reset();
        err = eng_call(PDV_ENG_PAGEINFO, &g_pi, sizeof g_pi, g_thq.page, 0, 0);
        if (err) return err;
        pdr_zero(&g_tv, sizeof g_tv);
        g_tv.box.x0 = pdr_max(g_pi.media.x0, g_pi.crop.x0); g_tv.box.y0 = pdr_max(g_pi.media.y0, g_pi.crop.y0);
        g_tv.box.x1 = pdr_min(g_pi.media.x1, g_pi.crop.x1); g_tv.box.y1 = pdr_min(g_pi.media.y1, g_pi.crop.y1);
        g_tv.rot = g_pi.rotate;
        if (g_tv.box.x1 - g_tv.box.x0 < 160 || g_tv.box.y1 - g_tv.box.y0 < 160) return PDE_RANGE;
        dw = (pdw)(g_tv.box.x1 - g_tv.box.x0); dh = (pdw)(g_tv.box.y1 - g_tv.box.y0);
        if (g_tv.rot == 90 || g_tv.rot == 270) { zw = dw; dw = dh; dh = zw; }
        /* the zoom, in percent, at which the page fits the cell: px / (pt16 / 16) * 100 */
        zw = (pdw)pdr_muldiv((pdi)g_thq.w, 1600, (pdi)dw);
        zh = (pdw)pdr_muldiv((pdi)g_thq.h, 1600, (pdi)dh);
        g_tv.zoom = zw < zh ? zw : zh;
        if (g_tv.zoom < 1) g_tv.zoom = 1;
        g_tv.asp_num = g_tv.asp_den = 1;
        pdm_view_size(&vb_w, &vb_h, &g_tv);
        pw = vb_w; ph = vb_h;
        if (pw < 1) pw = 1;
        if (ph < 1) ph = 1;
        if (pw > (pdi)g_thq.w) pw = (pdi)g_thq.w;
        if (ph > (pdi)g_thq.h) ph = (pdi)g_thq.h;
        g_tv.ox = (pdi)(((pdi)g_thq.w - pw) >> 1); g_tv.oy = (pdi)(((pdi)g_thq.h - ph) >> 1);
        view_base(&g_tv);
        g_mode = PDR_INTERP; g_thumb = 1; g_bpp = 1; g_deskwhite = 1;
        g_winoff = 0;
        g_vcx0 = 0; g_vcy0 = 0; g_vcx1 = (pdi)(g_thq.w - 1); g_vcy1 = (pdi)(g_thq.h - 1);
        g_pgx0 = g_tv.ox; g_pgy0 = g_tv.oy; g_pgx1 = (pdi)(g_tv.ox + pw - 1); g_pgy1 = (pdi)(g_tv.oy + ph - 1);
        g_tdseg = g_thq.dseg; g_tdoff = g_thq.doff; g_tstride = (pdw)((g_thq.w + 7) >> 3);
        if (!band_geometry()) return PDE_NOMEM;
        g_rq.page = g_thq.page;
        g_nb = band_count(); g_gw = g_nb; g_b0 = 0; g_b1 = g_nb; g_gceil = g_nb;
        err = pass_start();
        g_thq.state = 1;
        pd_fcopy_out(&g_thq, g_xb, 0, sizeof g_thq);
        if (err) { g_ph = PDR_DONE; return err; }
        return PDE_BUSY;
    }
    err = verb_step();
    if (err == PDE_OK) { g_thumb = 0; g_thq.state = 2; }
    pd_fcopy_out(&g_thq, g_xb, 0, sizeof g_thq);
    return err;
}

/* ---- INIT and the dispatcher -------------------------------------------------- */
/* the extension cache's region from g_ini (20.22.7); a new region is empty */
static void xc_adopt(void)
{
    pdw kb = g_ini.xc_kb > 63 ? 63 : g_ini.xc_kb, seg = kb ? g_ini.xc_seg : 0;
    if (seg != g_xcseg) g_xctop = 0;
    g_xcseg = seg; g_xccap = (pdw)(kb << 10);
    if (g_xctop > g_xccap) g_xctop = 0;
}
#define INI_OLDW 24                     /* pdxb_init's words before slot_seg (20.22.1) */
typedef char pdr_ck_ini[sizeof(struct pdxb_init) == INI_OLDW * 2 + 26 ? 1 : -1];
/* the words of pdxb_init that are PDREND's own claims and doors (20.22.7.11):
 * xb, the engine's vector, dl, band, bank, adapter, bpp. A re-INIT that keeps
 * them is the same document; PDENG's, PDWRT's and the Flate slots' words may
 * change under it (the primary shrinks INIT's Flate slots), and so may the
 * glyph claim and the XC, which are taken up again (glyph_fresh, the pass) */
static const pdb ini_mine[] = { 0, 1, 2, 9, 10, 11, 12, 15, 16, 19, 20 };
typedef char pdr_ck_inimine[(pdw)&((struct pdxb_init *)0)->bpp == 40 && (pdw)&((struct pdxb_init *)0)->dl_seg == 18 ? 1 : -1];
/* the glyph claim is new (FONTBANK, or a re-INIT that moved or grew it): what
 * PDREND keeps in it - the strike cache, PDFONTX.RES's INDEX at GC_IDX (D5),
 * the transformed-glyph slots - starts again from the claim itself */
static void glyph_fresh(void)
{
    pdw cb, cap, got, n, seg;
    g_scn = 0; g_scbump = SC_BASE; g_bk_seg[1] = 0; g_bk_n[1] = 0; g_fxok = 0;
    if (g_bankok && g_gseg && g_gkb >= 8) {
        cb = file_enter();
        if (cb) {
            cap = (pdw)((2048 + cb - 1) & ~(cb - 1));
            got = file_read("PDFONTX.RES", 0, 0, (pdw)(g_gseg + (GC_IDX >> 4)), cap);
            file_leave();
            if (got > cap) got = cap;
            if (got > GC_XBASE - GC_IDX) got = GC_XBASE - GC_IDX;     /* what the region holds */
            if (got >= 96) {
                seg = index_bank((pdw)(g_gseg + (GC_IDX >> 4)), got, &g_bn_tmp);
                n = g_bn_tmp;
                if (seg) { g_bk_seg[1] = seg; g_bk_n[1] = n; g_fxok = 1; }
            }
        }
    }
    /* the transformed-glyph slots hold whatever the new claim held: empty them
     * wherever glyph_xform may use them (a claim of 5 KiB or more) */
    if (g_gseg && g_gkb >= 5) pd_ffill(g_gseg, GC_XBASE, 0, SC_BASE - GC_XBASE);
    g_pkfam = 0xFF;
}
static pdw verb_init(pdw xbseg)
{
    pdw bound = pdr_eng_seg, i, o;      /* 0 on the FIRST INIT: the bss */
    /* a re-INIT under a ladder answer (20.22.7, 20.22.7.11): the same claims,
     * a region added (PDE_NEEDMEM) or the XC released to make room for a slot
     * occupant (PDE_NEEDMOD). The page view and the document's caches go on;
     * an XC that went from under the view is the cache AND the list's LUT
     * offsets and the band's stash, so the pass runs again without it, and a
     * released one is not asked for again this view */
    if (bound && x_ask) {
        for (i = 0; i < sizeof ini_mine; i++)
            if (pd_fpeekw(xbseg, (pdw)(ini_mine[i] << 1)) != ((pdw *)&g_ini)[ini_mine[i]]) break;
        if (i == sizeof ini_mine) {
            o = g_xcseg;
            pd_fcopy_in(xbseg, 0, &g_ini, sizeof g_ini); xc_adopt();
            if (o && g_xcseg != o && x_ask != (0x80 | PDMEM_XC)) {
                g_xclost = 1; g_ikb = 0;
                if (!g_xcseg) x_ref |= XR_MEM;
            }
            x8_init(g_ini.x8_seg, g_ini.x8_kb);
            /* the glyph claim moved or grew (PDXF's GLYPH grant, a moved X):
             * fonts' synthetic blocks follow it (syn_adopt), and the row buffer
             * and the caches start again from the new claim, which need not
             * hold the old bytes; the pass runs again */
            o = g_ini.glyph_seg ? (g_ini.glyph_kb > 63 ? 63 : g_ini.glyph_kb) : 0;
            i = g_ini.glyph_seg != g_gseg || o != g_gkb;
            syn_adopt(g_ini.glyph_seg, g_ini.glyph_kb);
            if (i) { glyph_fresh(); g_xclost = 1; }
            return PDE_OK;
        }
    }
    g_xb = xbseg;
    /* the engine's vector first, before anything can call the engine. The
     * first INIT then closes nothing: its g_idone = 0 is the bss, not an open
     * content stream, and close_streams() through the 0000:0000 it found was
     * rendfix's launch hang (pdr_eng now refuses a 0 vector as well) */
    pd_fcopy_in(g_xb, 0, &g_ini, sizeof g_ini);
    pdr_eng_off = g_ini.eng_off; pdr_eng_seg = g_ini.eng_seg;
    if (bound) close_streams();
    g_ph = PDR_IDLE; g_mode = PDR_IDLE;
    g_dls = g_ini.dl_seg;
    g_dlcap = g_ini.dl_kb ? (pdw)(g_ini.dl_kb >= 63 ? 0xFC00u : (pdw)(g_ini.dl_kb << 10)) : 0;
    /* the workspace is the last PDR_WSB bytes of the claim: the DL gets the rest.
     * A claim with no room for both is NO claim (D13): BEGIN, TEXTMAP and THUMB
     * refuse PDE_NOMEM rather than put the workspace past its end */
    if (g_dls && g_dlcap >= PDR_WSB + PDR_MINDL) { g_dlcap -= PDR_WSB; g_ws = (pdw)(g_dls + (g_dlcap >> 4)); }
    else { g_dlcap = 0; g_dls = 0; g_ws = 0; }
    g_bseg = g_ini.band_seg; g_bbytes = g_ini.band_bytes;
    g_gseg = g_ini.glyph_seg; g_gkb = g_ini.glyph_kb > 63 ? 63 : g_ini.glyph_kb;   /* << 10 fits 16 bits */
    if (!g_gseg) g_gkb = 0;
    g_bankok = 0; g_bankseg = 0; g_bk_seg[0] = g_bk_seg[1] = 0; g_pkfam = 0xFF;
    g_scn = 0; g_scbump = SC_BASE; g_fxok = 0;
    g_bpp = g_ini.bpp == 4 ? 4 : 1;
    x8_init(g_ini.x8_seg, g_ini.x8_kb);
    g_nfr = 0; g_idone = 1; g_pgopen = 0;
    /* perf: an INIT is a new document (D2): its fonts and Tf names are new */
    g_fnnext = 0; pdr_zero(g_fk, sizeof g_fk); g_fwi = 0xFF; g_fwdirty = 0; g_rcn = 0; g_gnogrow = 0; g_tmref = 0;
    if (syn_cap()) pd_ffill(g_gseg, syn_hdr(), 0, PDFB_HDR);   /* and its strike blocks: object numbers repeat across files (QA R24) */
    g_xctop = 0; xc_adopt();            /* a new document: an empty extension cache */
    return PDE_OK;
}

/* PDV_REND_SLOT (20.22.2, 20.22.7): BX = the PDX_* in the slot now, CX:DX its
 * door (off:seg); 0 = none. An occupant that leaves under an open image of
 * its own leaves that image's ONE engine stream on top of the engine's stack:
 * the core closes it itself (the occupant cannot any more) */
static pdw verb_slot(pdw id, pdw off, pdw seg)
{
    if (id >= PDX_COUNT || !seg) id = 0;
    if (id != x_res || seg != pdr_x_seg) {
        if (g_iopen && g_icodec) { strm_close(); g_iopen = 0; }
        if (id) x_swaps++;
    }
    x_res = id; pdr_x_off = off; pdr_x_seg = id ? seg : 0;
    return PDE_OK;
}
static pdw xb_seg(void) { return g_xb; }

/* PDV_REND_FONTBANK (D3): the primary's INDEX claim of PDRES, where the package
 * is, and where the PDRES part starts in it. The optional PDFONTX.RES beside
 * the package gets its own INDEX read into the glyph claim (D5). The primary
 * sends this after every PDV_REND_INIT that changed the claims. */
static struct pdxb_fontbank g_fbq;
static pdw verb_fontbank(void)
{
    pd_fcopy_in(g_xb, 0, &g_fbq, sizeof g_fbq);
    bank_attach(g_fbq.index_seg, g_fbq.index_len);
    pdr_scpy(g_pkgname, g_fbq.pkgname, 14);
    g_pclus = g_fbq.place_clus; g_pvol = g_fbq.place_vol;
    g_poff_lo = g_fbq.part_off.lo; g_poff_hi = g_fbq.part_off.hi;
    if (!g_bankok) { g_scn = 0; g_scbump = SC_BASE; g_bk_seg[1] = 0; g_bk_n[1] = 0; g_fxok = 0; return PDE_FONT; }
    glyph_fresh();
    return PDE_OK;
}

pdw pdr_verb(pdw verb, pdw bx, pdw cx, pdw dx)
{
    switch (verb) {
    case PDV_REND_SLOT:     return verb_slot(bx, cx, dx);
    case PDV_REND_INIT:     return verb_init(bx);
    case PDV_REND_BEGIN:    return verb_begin();
    case PDV_REND_STEP:     return verb_step();
    case PDV_REND_ABORT:    return verb_abort(cx);
    case PDV_REND_TEXTMAP:  return verb_textmap();
    case PDV_REND_THUMB:    return verb_thumb();
    case PDV_REND_STATUS:   return verb_status();
    case PDV_REND_FONTBANK: return verb_fontbank();
    }
    return PDE_RANGE;
}

#ifdef PD_HOST
/* host test hook: the base matrix applied to a page point (pt16), qpx result */
void pdr_test_fwd(pd_pt x, pd_pt y, pdi *qx, pdi *qy)
{
    static struct pd_fx fx, fy;
    pdm_pt16_to_fx(&fx, x); pdm_pt16_to_fx(&fy, y);
    pdm_mat_apply(&fx, &fy, &g_base);
    *qx = pdm_fx_round(&fx); *qy = pdm_fx_round(&fy);
}
void pdr_test_base(const struct pd_view *v) { view_base(v); }
#endif
