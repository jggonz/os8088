/* ---- pdxjhdr.c: containers, main-header markers, tile geometry (20.22.13.1) --- */

static pdw cdiv(pdw v, pdw s) { return (pdw)((v + ((1u << s) - 1u)) >> s); }

/* ---- the tile and its resolutions ------------------------------------------- */
static pdw g_tx0, g_tx1, g_ty0, g_ty1;
static pdw g_rx0[JX_MAXRES], g_rx1[JX_MAXRES], g_ry0[JX_MAXRES], g_ry1[JX_MAXRES];
static pdw g_npx[JX_MAXRES], g_npy[JX_MAXRES];

static void tile_geom(pdw tx, pdw ty)
{
    pdw res, lv, v;
    g_tx0 = tx * g_xts; g_ty0 = ty * g_yts;
    v = g_tx0 + g_xts; g_tx1 = v < g_xs ? v : g_xs;
    v = g_ty0 + g_yts; g_ty1 = v < g_ys ? v : g_ys;
    for (res = 0; res <= g_nl; res++) {
        lv = g_nl - res;
        g_rx0[res] = cdiv(g_tx0, lv); g_rx1[res] = cdiv(g_tx1, lv);
        g_ry0[res] = cdiv(g_ty0, lv); g_ry1[res] = cdiv(g_ty1, lv);
        g_npx[res] = g_rx1[res] > g_rx0[res] ? cdiv(g_rx1[res], g_ppx[res]) - (g_rx0[res] >> g_ppx[res]) : 0;
        g_npy[res] = g_ry1[res] > g_ry0[res] ? cdiv(g_ry1[res], g_ppy[res]) - (g_ry0[res] >> g_ppy[res]) : 0;
    }
}

/* one band of one precinct: its rectangle in band coordinates, its code-block grid
 * and where the band sits in the in-place plane */
struct jx_pb {
    pdw bx0, by0, bx1, by1;             /* clipped to the band                  */
    pdw tbx0, tby0;                     /* the band's origin                    */
    pdw gx0, gy0, ncx, ncy;             /* first code-block column/row, counts  */
    pdw ox, oy;                         /* the band's offset in the plane       */
    pdb cw, ch, orient;
};
static struct jx_pb g_pbs[3];

static void pb_geom(struct jx_pb *q, pdw res, pdw p, pdw b)
{
    pdw nb, ppx, ppy, i, j, px0, px1, py0, py1, tbx1, tby1, half, x1, y1, xob, yob;
    q->orient = res ? (pdb)(b + 1) : 0;
    xob = (q->orient == 1 || q->orient == 3);
    yob = (q->orient >= 2);
    nb = res ? (pdw)(g_nl - res + 1) : (pdw)g_nl;
    half = nb ? (1u << (nb - 1)) : 0;
    q->tbx0 = g_tx0 >> nb; q->tby0 = g_ty0 >> nb;
    x1 = g_tx1; y1 = g_ty1;
    if (xob) x1 = x1 > half ? x1 - half : 0;
    if (yob) y1 = y1 > half ? y1 - half : 0;
    tbx1 = cdiv(x1, nb); tby1 = cdiv(y1, nb);
    ppx = g_ppx[res]; ppy = g_ppy[res];
    i = p % g_npx[res]; j = p / g_npx[res];
    px0 = ((g_rx0[res] >> ppx) + i) << ppx; px1 = px0 + (1u << ppx);
    py0 = ((g_ry0[res] >> ppy) + j) << ppy; py1 = py0 + (1u << ppy);
    if (res) { px0 >>= 1; px1 >>= 1; py0 >>= 1; py1 >>= 1; }
    q->bx0 = px0 > q->tbx0 ? px0 : q->tbx0;
    q->by0 = py0 > q->tby0 ? py0 : q->tby0;
    q->bx1 = px1 < tbx1 ? px1 : tbx1;
    q->by1 = py1 < tby1 ? py1 : tby1;
    q->ncx = q->ncy = 0; q->gx0 = q->gy0 = 0;
    q->cw = (pdb)(ppx - (res ? 1 : 0)); if (q->cw > g_xcb) q->cw = g_xcb;
    q->ch = (pdb)(ppy - (res ? 1 : 0)); if (q->ch > g_ycb) q->ch = g_ycb;
    if (q->bx1 > q->bx0 && q->by1 > q->by0) {
        q->gx0 = q->bx0 >> q->cw; q->gy0 = q->by0 >> q->ch;
        q->ncx = cdiv(q->bx1, q->cw) - q->gx0;
        q->ncy = cdiv(q->by1, q->ch) - q->gy0;
    }
    q->ox = q->oy = 0;
    if (res) {
        if (xob) q->ox = g_rx1[res - 1] - g_rx0[res - 1];
        if (yob) q->oy = g_ry1[res - 1] - g_ry0[res - 1];
    }
}

/* ---- the main header ---------------------------------------------------------- */
static void siz(void)
{
    pdw rsiz, xo, yo, xto, yto, i, a;
    rsiz = sg16();
    g_xs = sg32s(); g_ys = sg32s();
    xo = sg32s(); yo = sg32s();
    g_xts = sg32s(); g_yts = sg32s();
    xto = sg32s(); yto = sg32s();
    g_nc = sg16();
    if (g_trunc) return;
    if (rsiz & 0x8000u) { g_why = PDJ_R_PART2; return; }
    if (g_xs == 0xFFFF || g_ys == 0xFFFF || g_xts == 0xFFFF || g_yts == 0xFFFF
        || xo || yo || xto || yto || !g_xs || !g_ys || !g_xts || !g_yts
        || !g_nc || g_nc > JX_MAXC) { g_why = PDJ_R_GEOM; return; }
    for (i = 0; i < g_nc; i++) {
        a = sg8();
        g_dep[i] = (pdb)((a & 0x7F) + 1); g_sgn[i] = (pdb)(a >> 7);
        if (g_dep[i] > 12) g_why = PDJ_R_DEPTH;
        if (sg8() != 1 || sg8() != 1) g_why = PDJ_R_GEOM;
    }
    g_ntx = (pdw)((g_xs + g_xts - 1) / g_xts);
    g_nty = (pdw)((g_ys + g_yts - 1) / g_yts);
    g_ntile = g_ntx * g_nty;
    if (g_ntx > 255 || g_nty > 255) g_why = PDJ_R_LIMIT;
    g_havesiz = 1;
}

/* the SPcod/SPcoc part shared by COD and COC. Returns bytes appended to g_codref
 * from `at`. precincts: Scod/Scoc bit 0 */
static void spcod(pdw prec, pdw store)
{
    pdw i, b, tr;
    g_nl = (pdb)sg8();
    g_xcb = (pdb)(sg8() + 2); g_ycb = (pdb)(sg8() + 2);
    g_sty = (pdb)sg8();
    tr = sg8();
    if (store) {
        g_codref[1] = g_nl; g_codref[2] = (pdb)(g_xcb - 2); g_codref[3] = (pdb)(g_ycb - 2);
        g_codref[4] = g_sty; g_codref[5] = (pdb)tr; g_codlen = 6;
    }
    if (g_nl > JX_MAXNL) { g_why = PDJ_R_GEOM; g_nl = 0; }
    if (g_xcb > 6 || g_ycb > 6 || g_xcb + g_ycb > 12) g_why = PDJ_R_GEOM;
    if (g_sty & 0x05) g_why = PDJ_R_STYLE;
    if (tr > 1) g_why = PDJ_R_PART2;
    g_rev = (pdb)tr;
    for (i = 0; i <= g_nl; i++) {
        if (prec) {
            b = sg8();
            g_ppx[i] = (pdb)(b & 15); g_ppy[i] = (pdb)(b >> 4);
            if (i && (!g_ppx[i] || !g_ppy[i])) g_why = PDJ_R_GEOM;
            if (store && g_codlen < 48) g_codref[g_codlen++] = (pdb)b;
        } else { g_ppx[i] = g_ppy[i] = 15; }
    }
}
static void cod(void)
{
    pdw scod = sg8();
    g_prog = (pdb)sg8();
    g_nlay = sg16();
    g_mct = (pdb)sg8();
    g_sop = (pdb)((scod >> 1) & 1); g_eph = (pdb)((scod >> 2) & 1);
    g_codref[0] = (pdb)(scod & 1);
    spcod(scod & 1, 1);
    if (g_prog > 4) g_why = PDJ_R_PART2;
    if (!g_nlay || g_nlay > 255) g_why = PDJ_R_LIMIT;
    g_havecod = 1;
}
static void coc(void)
{
    pdw n = 0, b;
    if (!g_havecod) { g_why = PDJ_R_TRUNC; return; }
    sg8();                              /* Ccoc (Csiz <= 16: one byte)         */
    b = sg8();
    /* the rest is compared with COD's, byte for byte (20.22.13.1, PERTILE) */
    if ((b & 1) != g_codref[0]) g_why = PDJ_R_PERTILE;
    while (g_sg && !g_trunc) {
        b = sg8(); n++;
        if (n >= g_codlen || g_codref[n] != (pdb)b) g_why = PDJ_R_PERTILE;
    }
    if (n + 1 != g_codlen) g_why = PDJ_R_PERTILE;
}
static void qcd(void)
{
    pdw n = 0, b;
    b = sg8();
    g_qsty = (pdb)(b & 0x1F); g_grd = (pdb)(b >> 5);
    g_qcdref[0] = (pdb)b;
    if (g_qsty > 2) { g_why = PDJ_R_PART2; return; }
    while (g_sg && !g_trunc) {
        b = sg8();
        if (n < 79) g_qcdref[++n] = (pdb)b;
        else { g_why = PDJ_R_LIMIT; }
    }
    g_qcdlen = n + 1;
    g_haveqcd = 1;
}
static void qcc(void)
{
    pdw n = 0, b;
    if (!g_haveqcd) { g_why = PDJ_R_TRUNC; return; }
    sg8();
    while (g_sg && !g_trunc) {
        b = sg8();
        if (n >= g_qcdlen || g_qcdref[n] != (pdb)b) g_why = PDJ_R_PERTILE;
        n++;
    }
    if (n != g_qcdlen) g_why = PDJ_R_PERTILE;
}

/* eps and mu of every band from QCD (20.22.13.4); Mb = guard + eps - 1 <= 15 */
static pdw derive_q(void)
{
    pdw bi, nbands, k, v, e, mu, i;
    nbands = 3u * g_nl + 1;
    for (bi = 0; bi < nbands; bi++) {
        if (g_qsty == 1) {
            v = ((pdw)g_qcdref[1] << 8) | g_qcdref[2];
            e = v >> 11; mu = v & 0x7FF;
            k = bi ? (bi - 1) / 3 : 0;
            e = e > k ? e - k : 0;
        } else if (g_qsty == 0) {
            i = bi + 1;
            if (i >= g_qcdlen) return refuse(PDJ_R_TRUNC);
            e = g_qcdref[i] >> 3; mu = 0;
        } else {
            i = 2 * bi + 1;
            if (i + 1 >= g_qcdlen) return refuse(PDJ_R_TRUNC);
            v = ((pdw)g_qcdref[i] << 8) | g_qcdref[i + 1];
            e = v >> 11; mu = v & 0x7FF;
        }
        g_qe[bi] = (pdb)e; g_qm[bi] = mu;
        if (g_grd + e < 1) return refuse(PDJ_R_DEPTH);
    }
    return 0;
}

/* ---- the tile-part reader (td_*) ---------------------------------------------- */
static pdw g_pend;                      /* a parsed SOT is waiting             */
static pdw g_pt, g_pphi, g_pplo, g_ptp; /* its tile, Psot, TPsot               */
static pdw g_rmhi, g_rmlo, g_unb;       /* bytes left in the current tile-part */
static pdw g_cur;                       /* the tile whose parts are being read */
static pdw g_eoc;                       /* EOC or the end of the file was seen */
static int g_back[2]; static pdw g_nback;

static int td_byte(void);
static pdw sot_read(void)               /* after FF90 */
{
    pdw l = rd16(), t = rd16(), hi = rd16(), lo = rd16(), tp;
    int a = s_byte(), b = s_byte();
    if (g_trunc || a < 0 || b < 0 || l != 10) { g_eoc = 1; return 1; }
    tp = (pdw)a;
    g_pt = t; g_pphi = hi; g_pplo = lo; g_ptp = tp; g_pend = 1;
    return 0;
}
/* consume the pending SOT: the tile-part header up to SOD. 0 = ok */
static pdw tp_begin(void)
{
    pdw m, l, used, hdr = 12 + 2;
    g_pend = 0;
    for (;;) {
        m = rd16();
        if (g_trunc) { g_eoc = 1; return 1; }
        if (m == 0xFF93) break;                         /* SOD                  */
        l = rd16();
        if (g_trunc || l < 2) { g_eoc = 1; return 1; }
        if (m == 0xFF52 || m == 0xFF53 || m == 0xFF5C || m == 0xFF5D) { g_why = PDJ_R_PERTILE; return 2; }
        if (m == 0xFF5E) { g_why = PDJ_R_ROI; return 2; }
        if (m == 0xFF5F || m == 0xFF61) { g_why = PDJ_R_PACKED; return 2; }
        hdr += l + 2;
        if (s_skip32(0, l - 2)) { g_eoc = 1; return 1; }
    }
    g_unb = (g_pphi == 0 && g_pplo == 0);
    if (!g_unb) {
        /* rem = Psot - hdr (12 for SOT, the markers, 2 for SOD) */
        used = hdr;
        if (g_pphi == 0 && g_pplo < used) { g_eoc = 1; return 1; }
        g_rmhi = g_pphi; g_rmlo = g_pplo;
        if (g_rmlo < used) g_rmhi--;
        g_rmlo -= used;
    }
    return 0;
}
/* the next marker after a tile-part's data: 1 = another tile-part of the current
 * tile began, 0 = nothing more for it */
static pdw tp_next(void)
{
    pdw m, r;
    if (g_eoc) return 0;
    m = rd16();
    if (g_trunc || m == 0xFFD9) { g_eoc = 1; return 0; }
    if (m != 0xFF90) { g_eoc = 1; return 0; }
    if (sot_read()) return 0;
    if (g_pt != g_cur) return 0;                        /* another tile's: leave pending */
    r = tp_begin();
    if (r) { g_eoc = 1; return 0; }
    return 1;
}
static int td_byte(void)
{
    if (g_nback) return g_back[--g_nback];
    for (;;) {
        if (g_unb || g_rmhi || g_rmlo) break;
        if (!tp_next()) return -1;
    }
    if (!g_unb) { if (!g_rmlo) g_rmhi--; g_rmlo--; }
    return s_byte();
}
static void td_back(int b) { if (g_nback < 2) g_back[g_nback++] = b; }
/* skip n bytes of tile data; 1 = the data ended first */
static pdw td_skip(pdw hi, pdw lo)
{
    pdw k, lim;
    while (hi || lo) {
        if (g_nback) { g_nback--; if (!lo) hi--; lo--; continue; }
        if (!g_unb && !g_rmhi && !g_rmlo) { if (!tp_next()) return 1; continue; }
        /* skip the smaller of what is wanted and what the tile-part has */
        k = lo; lim = 0xFFFF;
        if (hi) k = 0xFFFF;
        if (!g_unb) { if (!g_rmhi) lim = g_rmlo; }
        if (k > lim) k = lim;
        if (s_skip32(0, k)) return 1;
        if (!g_unb) { if (g_rmlo < k) g_rmhi--; g_rmlo -= k; }
        if (lo < k) hi--;
        lo -= k;
    }
    return 0;
}
/* copy n bytes of tile data to a far address; 1 = the data ended first */
static pdw td_copy(pdw seg, pdw off, pdw n)
{
    pdw k;
    while (n) {
        if (g_nback) {
            pd_fpokeb(seg, off, (pdb)td_byte()); off++; n--; continue;
        }
        if (!g_unb && !g_rmhi && !g_rmlo) { if (!tp_next()) return 1; continue; }
        k = n;
        if (!g_unb && !g_rmhi && g_rmlo < k) k = g_rmlo;
        if (s_copy(seg, off, k)) return 1;
        if (!g_unb) { if (g_rmlo < k) g_rmhi--; g_rmlo -= k; }
        off += k; n -= k;
    }
    return 0;
}

/* ---- JP2 boxes and the codestream's start -------------------------------------- */
static pdw jp2_boxes(void);
static pdw open_container(void)
{
    int a;
    pdw w0, w1, m;
    g_alpha = 0xFFFF; g_jp2 = 0;
    w0 = rd16();
    if (g_trunc) return refuse(PDJ_R_NOTJPX);
    if (w0 == 0xFF4F) {                                 /* a bare codestream    */
        m = rd16();
        if (m != 0xFF51) return refuse(PDJ_R_TRUNC);
        return 0;
    }
    w1 = rd16();
    if (w0 != 0 || w1 != 12) return refuse(PDJ_R_NOTJPX);
    if (rd16() != 0x6A50 || rd16() != 0x2020 || rd16() != 0x0D0A || rd16() != 0x870A || g_trunc)
        return refuse(PDJ_R_NOTJPX);
    g_jp2 = 1;
    a = jp2_boxes();
    if (a) return a;
    /* inside jp2c: SOC then SIZ */
    if (rd16() != 0xFF4F || rd16() != 0xFF51) return refuse(PDJ_R_TRUNC);
    return 0;
}
/* 32-bit subtract of a small k; 1 = it would go negative */
static pdw sub32(pdw *hi, pdw *lo, pdw k)
{
    if (*lo < k) { if (!*hi) return 1; (*hi)--; }
    *lo -= k;
    return 0;
}
/* walk the top-level boxes up to the start of jp2c's payload */
static pdw jp2_boxes(void)
{
    static pdw hi, lo;                                  /* sub32 takes their addresses */
    pdw t0, t1, rem, k, n, i, typ, hdr, cnt, cn, ty, as;
    for (;;) {
        hi = rd16(); lo = rd16(); t0 = rd16(); t1 = rd16();
        if (g_trunc) return refuse(PDJ_R_NOTJPX);
        hdr = 8;
        if (hi == 0 && lo == 1) {                       /* the 64-bit length    */
            if (rd16() || rd16()) return refuse(PDJ_R_NOTJPX);
            hi = rd16(); lo = rd16(); hdr = 16;
        } else if (hi == 0 && lo == 0) {                /* to the end           */
            if (t0 == 0x6A70 && t1 == 0x3263) return 0;
            return refuse(PDJ_R_NOTJPX);
        }
        if (g_trunc || sub32(&hi, &lo, hdr)) return refuse(PDJ_R_NOTJPX);
        if (t0 == 0x6A70 && t1 == 0x3263) return 0;     /* jp2c                 */
        if (t0 == 0x6A70 && t1 == 0x3268 && hi == 0 && lo <= 4096) {      /* jp2h */
            rem = lo;
            while (rem >= 8) {
                cnt = rd16(); k = rd16(); typ = rd16(); n = rd16();
                if (g_trunc || cnt || k < 8 || k > rem) return refuse(PDJ_R_TRUNC);
                rem -= k; k -= 8;
                if (typ == 0x7063 && n == 0x6C72) return refuse(PDJ_R_PALETTE);   /* pclr */
                if (typ == 0x636D && n == 0x6170) return refuse(PDJ_R_PALETTE);   /* cmap */
                if (typ == 0x6364 && n == 0x6566 && k >= 2) {                     /* cdef */
                    cnt = rd16(); k -= 2;
                    for (i = 0; i < cnt && k >= 6; i++, k -= 6) {
                        cn = rd16(); ty = rd16(); as = rd16();
                        if ((ty == 1 || ty == 2) && as == 0 && g_alpha == 0xFFFF) g_alpha = cn;
                    }
                }
                if (s_skip32(0, k)) return refuse(PDJ_R_TRUNC);
            }
            if (rem && s_skip32(0, rem)) return refuse(PDJ_R_TRUNC);
            continue;
        }
        if (s_skip32(hi, lo)) return refuse(PDJ_R_NOTJPX);
    }
}

/* the whole main header; the stream is at SIZ's marker length */
static pdw parse_main(void)
{
    pdw m, l, r;
    g_havesiz = g_havecod = g_haveqcd = g_trunc = 0;
    g_why = 0;
    for (m = 0xFF51;;) {
        l = rd16();
        if (g_trunc || l < 2) return refuse(PDJ_R_TRUNC);
        g_sg = l - 2;
        switch (m) {
        case 0xFF51: siz(); break;
        case 0xFF52: if (!g_havesiz) g_why = PDJ_R_TRUNC; else cod(); break;
        case 0xFF53: coc(); break;
        case 0xFF5C: if (!g_havesiz) g_why = PDJ_R_TRUNC; else qcd(); break;
        case 0xFF5D: qcc(); break;
        case 0xFF5E: g_why = PDJ_R_ROI; break;
        case 0xFF5F: case 0xFF60: g_why = PDJ_R_PACKED; break;
        default: break;                                 /* TLM PLM CRG COM ...  */
        }
        if (g_trunc) return refuse(PDJ_R_TRUNC);
        if (g_why) return PDE_FILTER;
        if (g_sg && s_skip32(0, g_sg)) return refuse(PDJ_R_TRUNC);
        m = rd16();
        if (g_trunc) return refuse(PDJ_R_TRUNC);
        if (m == 0xFF90) break;                         /* SOT                  */
        if ((m >> 8) != 0xFF || m == 0xFFD9) return refuse(PDJ_R_TRUNC);
    }
    if (!g_havesiz || !g_havecod || !g_haveqcd) return refuse(PDJ_R_TRUNC);
    if (g_ntile > 1 && (((g_xts | g_yts) & ((1u << g_nl) - 1u)) != 0)) return refuse(PDJ_R_GEOM);
    r = derive_q();
    if (r) return r;
    g_pend = 0; g_eoc = 0; g_nback = 0;
    return sot_read() ? refuse(PDJ_R_TRUNC) : 0;
}
