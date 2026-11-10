/* ============================================================================
 * os8088 - apps/pagedesk/pdxidct.c          JPEG (DCT) for PDXI (PAGEDESK-SPEC 20.22.11)
 *
 * #included by pdxi.c. Marker parser, Huffman tables, the baseline streaming
 * decoder, the progressive / multi-scan coefficient decoder, the reduction
 * rule, colour and row output. The three inner loops are assembly on the
 * target (pdxidct.inc): pdi_hsym, pdi_bits, pdi_block and pdi_idct; their C
 * twins are pdxitwin.c (host only).
 *
 * Every number from the file is bounded before it is used. No recursion.
 * ==========================================================================*/

#define DJ_EBUF   1100u         /* entropy buffer bytes (unstuffed)           */
#define DJ_PAD    320u          /* zero padding after it                      */
#define DJ_NEED   340           /* bytes a block may consume (+ slack)        */
#define DJ_RBUF   512u
#define DJ_PROW   1088u

/* ---- Huffman: layout pinned for the assembly (pdxidct.inc) ---------------- */
struct dj_huff {
    pdw look[256];              /* +0    (len << 8) | symbol; 0 = longer      */
    pdw limit[17];              /* +512  first code of L, plus its count      */
    pdw valoff[17];             /* +546  index of L's first symbol - its first code */
    pdb vals[256];              /* +580                                       */
    pdb set; pdb pad;           /* +836                                       */
};                              /* 838 bytes                                  */
#define DJ_HUFF_BYTES 838

struct dj_comp {
    pdb id, h, v, tq;
    pdb used, dctab, actab, pad;
    pdw pw, ph;                 /* padded size in blocks (MCUs * h, MCUs * v) */
    pdw bw, bh;                 /* real size in blocks                        */
    pdw base;                   /* stored: coefficient storage in workspace   */
    pdw pbase, pstride;         /* one MCU row of output plane                */
    pdi pred;
};

/* assembly (pdxidct.inc) / twin (pdxitwin.c) */
extern pdb  pdi_ebuf[DJ_EBUF + DJ_PAD];
extern pdw  pdi_pos;
extern const pdb pdi_zz[64];
extern pdi  pdi_coef[64];
pdw  pdi_hsym(struct dj_huff *t);
pdw  pdi_bits(pdw n);
pdi  pdi_block(struct dj_huff *dc, struct dj_huff *ac, pdi *coef);
void pdi_idct(pdi *coef, pdw *qt, pdw mode, pdw dseg, pdw doff, pdw stride);

pdb  pdi_ebuf[DJ_EBUF + DJ_PAD];
pdw  pdi_pos;
pdi  pdi_coef[64];
const pdb pdi_zz[64] = {        /* zigzag index -> natural position           */
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

/* ---- state ---------------------------------------------------------------- */
static struct dj_huff dj_ht[4];         /* 0..1 DC, 2..3 AC (tables 2 and 3 of a file: PDE_FILTER) */
static pdw  dj_qt[4][64];               /* natural order                       */
static pdb  dj_qset[4];
static struct dj_comp dj_c[4];
static pdw  dj_W, dj_H, dj_nc, dj_prog, dj_hmax, dj_vmax, dj_mcux, dj_mcuy;
static pdw  dj_ri, dj_jfif, dj_adobe, dj_atrans, dj_gotsof;
static pdw  dj_xform;                   /* 0 none, 1 YCbCr->RGB, 2 YCCK->CMYK */
static pdw  dj_s, dj_bs, dj_ow, dj_oh, dj_outn, dj_native, dj_invert;
static pdw  dj_nused, dj_mrh, dj_wn;    /* dj_wn: samples a pixel in the working row */           /* output rows per MCU row             */
static pdw  dj_stored;                  /* coefficients kept (else streaming)  */
static pdw  dj_decoded, dj_done;
static pdw  dj_nslot, dj_bstride;       /* stored block: slots, bytes          */
static pdw  dj_dec;                     /* 1/8 only: keep 1 block in 2^dec each way (a coarser grey box) */
static pdw  dj_need, dj_askh, dj_askkb;   /* bytes the last dj_fit laid out; the last PDE_NEEDMEM asked (handle, KiB) */
static pdw  dj_mask;                    /* stored blocks carry the 64-bit nonzero mask (1/2 and 1/4 only) */
static pdb  dj_nmap[64];                /* natural position -> slot, 255 = none */
static pdb  dj_sl2pos[64];
static pdw  dj_maxzz;
static pdi  dj_mready;                  /* MCU row in the planes, -1 none      */
static pdw  dj_err;
/* the scan */
static pdw  dj_nsc, dj_ss, dj_se, dj_ah, dj_al, dj_eobrun, dj_rleft;
static pdb  dj_sci[4];
static pdb  dj_sdc[4], dj_sac[4];
static pdw  dj_first_nsc;
/* the warm key */
static pdw  dj_warm;
static pdw  dj_k[7], dj_wk[7];
static pdw  dj_mnext;
/* raw bytes and entropy bytes */
static pdb  dj_rbuf[DJ_RBUF];
static pdw  dj_rpos, dj_rend, dj_reof;
static pdw  dj_eend, dj_emk, dj_emarker, dj_eeof;
static pdw  dj_seg;
static pdb  dj_prow[DJ_PROW];
static pdb  dj_orow[1024];
static pdi  dj_blk[68];
static struct pd_u32 dj_u;      /* scratch for pdm_u32_mul16: an automatic cannot be addressed */

/* ---- raw bytes ------------------------------------------------------------ */
static pdi dj_rb(void)
{
    if (dj_rpos >= dj_rend) {
        if (dj_reof) return -1;
        dj_rend = pdi_src_get(dj_rbuf, DJ_RBUF);
        dj_rpos = 0;
        if (dj_rend == 0) { dj_reof = 1; return -1; }
    }
    return dj_rbuf[dj_rpos++];
}

/* the next marker byte, or -1 */
static pdi dj_marker(void)
{
    pdi c;
    if (dj_emk) { dj_emk = 0; return (pdi)dj_emarker; }
    for (;;) {
        c = dj_rb();
        if (c < 0) return -1;
        if (c != 0xFF) continue;
        do { c = dj_rb(); } while (c == 0xFF);
        if (c < 0) return -1;
        if (c == 0) continue;
        return c;
    }
}

/* read within a marker segment */
static pdw dj_rd8(void)
{
    pdi c;
    if (dj_seg == 0) { dj_err = PDE_SYNTAX; return 0; }
    c = dj_rb();
    if (c < 0) { dj_err = PDE_TRUNC; dj_seg = 0; return 0; }
    dj_seg--;
    return (pdw)c;
}
static pdw dj_rd16(void)
{
    pdw a;
    a = dj_rd8();
    return (pdw)((a << 8) | dj_rd8());
}
static void dj_segskip(void)
{
    while (dj_seg) { if (dj_rb() < 0) { dj_err = PDE_TRUNC; break; } dj_seg--; }
    dj_seg = 0;
}

/* ---- entropy bytes: unstuffed, stopping at a marker ----------------------- */
static void dj_efill(void)
{
    pdw sh, i, n;
    pdi c;
    sh = (pdw)(pdi_pos >> 3);
    if (sh > dj_eend) sh = dj_eend;
    if (sh) {
        n = (pdw)(dj_eend - sh);
        for (i = 0; i < n; i++) pdi_ebuf[i] = pdi_ebuf[i + sh];
        dj_eend = n;
        pdi_pos = (pdw)(pdi_pos - (sh << 3));
    }
    while (dj_eend < DJ_EBUF && !dj_emk && !dj_eeof) {
        c = dj_rb();
        if (c < 0) { dj_eeof = 1; break; }
        if (c == 0xFF) {
            do { c = dj_rb(); } while (c == 0xFF);
            if (c < 0) { dj_eeof = 1; break; }
            if (c != 0) { dj_emk = 1; dj_emarker = (pdw)c; break; }
            c = 0xFF;
        }
        pdi_ebuf[dj_eend++] = (pdb)c;
    }
    for (i = dj_eend; i < dj_eend + DJ_PAD && i < DJ_EBUF + DJ_PAD; i++) pdi_ebuf[i] = 0;
}

static void dj_ensure(void)
{
    /* past the real bytes (a truncated scan, a marker): stay on the zero padding, so
     * a long run of blocks past the end never walks off the buffer */
    if ((pdw)(pdi_pos >> 3) > dj_eend) pdi_pos = (pdw)(dj_eend << 3);
    if ((pdi)dj_eend - (pdi)(pdi_pos >> 3) < DJ_NEED && !dj_emk && !dj_eeof) dj_efill();
}

static void dj_ereset(void)
{
    dj_eend = 0; pdi_pos = 0; dj_emk = 0; dj_eeof = 0;
    dj_eobrun = 0;
}

/* a restart interval ended: find the marker, eat RSTn, reset predictors */
static void dj_restart(void)
{
    pdw i;
    while (!dj_emk && !dj_eeof) { dj_eend = 0; pdi_pos = 0; dj_efill(); }
    if (dj_emk && dj_emarker >= 0xD0 && dj_emarker <= 0xD7) dj_emk = 0;
    dj_eend = 0; pdi_pos = 0;
    for (i = 0; i < DJ_PAD; i++) pdi_ebuf[i] = 0;
    for (i = 0; i < 4; i++) dj_c[i].pred = 0;
    dj_eobrun = 0;
}

/* a scan we do not decode: drop its entropy bytes up to the next real marker */
static void dj_skip_scan(void)
{
    pdi m;
    for (;;) {
        m = dj_marker();
        if (m < 0) { dj_eeof = 1; return; }
        if (m >= 0xD0 && m <= 0xD7) continue;
        dj_emk = 1; dj_emarker = (pdw)m;
        return;
    }
}

/* end of a decoded scan: park on the marker that ends it */
static void dj_scan_end(void)
{
    if (!dj_emk) dj_skip_scan();
    else if (dj_emarker >= 0xD0 && dj_emarker <= 0xD7) dj_skip_scan();
    dj_eend = 0; pdi_pos = 0;
}

/* ---- tables --------------------------------------------------------------- */
static pdw dj_dht_build(struct dj_huff *t, pdb *cnt, pdw nv)
{
    pdw L, i, code, first, k, n, sh, j, base, c, sym;
    for (i = 0; i < 256; i++) t->look[i] = 0;
    code = 0; k = 0;
    t->limit[0] = 0; t->valoff[0] = 0;
    for (L = 1; L <= 16; L++) {
        first = code;
        n = cnt[L - 1];
        t->valoff[L] = (pdw)(k - first);
        /* over-subscribed (more codes than L bits hold): refuse BEFORE filling the lookup */
        if (L < 16) { if ((pdw)(first + n) > (pdw)(1u << L)) return PDE_SYNTAX; }
        else if ((pdw)(first + n) < first) return PDE_SYNTAX;
        for (i = 0; i < n; i++) {
            c = (pdw)(first + i);
            sym = t->vals[k + i];
            if (L <= 8) {
                sh = 8 - L;
                base = (pdw)(c << sh);
                for (j = 0; j < (pdw)(1u << sh); j++) t->look[base + j] = (pdw)((L << 8) | sym);
            }
        }
        code = (pdw)(first + n);
        if (L < 16) { if (code > (pdw)(1u << L)) return PDE_SYNTAX; }
        else if (code < first) return PDE_SYNTAX;
        t->limit[L] = code;
        k = (pdw)(k + n);
        code = (pdw)(code << 1);
    }
    if (k != nv) return PDE_SYNTAX;
    t->set = 1;
    return PDE_OK;
}

static void dj_dqt(void)
{
    pdw pt, i, v;
    while (dj_seg && !dj_err) {
        pt = dj_rd8();
        if ((pt & 15) > 3 || (pt >> 4) > 1) { dj_err = PDE_SYNTAX; return; }
        for (i = 0; i < 64; i++) {
            v = (pt >> 4) ? dj_rd16() : dj_rd8();
            dj_qt[pt & 15][pdi_zz[i]] = v;
        }
        dj_qset[pt & 15] = 1;
    }
}

static void dj_dht(void)
{
    pdw tc, th, i, nv, tot;
    static pdb cnt[16];
    struct dj_huff *t;
    while (dj_seg && !dj_err) {
        tc = dj_rd8();
        th = tc & 15; tc = tc >> 4;
        if (tc > 1 || th > 3) { dj_err = PDE_SYNTAX; return; }
        if (th > 1) { dj_err = PDE_FILTER; return; }
        tot = 0;
        for (i = 0; i < 16; i++) { cnt[i] = (pdb)dj_rd8(); tot = (pdw)(tot + cnt[i]); }
        if (tot > 256 || dj_err) { dj_err = PDE_SYNTAX; return; }
        t = &dj_ht[tc * 2 + th];
        for (i = 0; i < tot; i++) t->vals[i] = (pdb)dj_rd8();
        if (dj_err) return;
        nv = tot;
        t->set = 0;
        if (dj_dht_build(t, cnt, nv)) { dj_err = PDE_SYNTAX; return; }
    }
}

static void dj_sof(pdw m)
{
    pdw i, np;
    struct dj_comp *c;
    if (dj_gotsof) { dj_err = PDE_SYNTAX; return; }
    dj_gotsof = 1;
    dj_prog = (m == 0xC2);
    np = dj_rd8();
    dj_H = dj_rd16(); dj_W = dj_rd16();
    dj_nc = dj_rd8();
    if (dj_err) return;
    if (np != 8) { dj_err = PDE_FILTER; return; }
    if (dj_W == 0 || dj_H == 0) { dj_err = PDE_SYNTAX; return; }
    if (dj_nc != 1 && dj_nc != 3 && dj_nc != 4) { dj_err = PDE_FILTER; return; }
    for (i = 0; i < dj_nc; i++) {
        c = &dj_c[i];
        c->id = (pdb)dj_rd8();
        np = dj_rd8();
        c->h = (pdb)(np >> 4); c->v = (pdb)(np & 15);
        c->tq = (pdb)dj_rd8();
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4 || c->tq > 3) { dj_err = PDE_SYNTAX; return; }
    }
}

static void dj_sos(void)
{
    pdw i, j, cid, t;
    dj_nsc = dj_rd8();
    if (dj_nsc < 1 || dj_nsc > 4 || dj_nsc > dj_nc) { dj_err = PDE_SYNTAX; return; }
    for (i = 0; i < dj_nsc; i++) {
        cid = dj_rd8(); t = dj_rd8();
        for (j = 0; j < dj_nc; j++) if (dj_c[j].id == cid) break;
        if (j >= dj_nc || dj_err) { dj_err = PDE_SYNTAX; return; }
        dj_sci[i] = (pdb)j;
        dj_sdc[i] = (pdb)(t >> 4); dj_sac[i] = (pdb)(t & 15);
        if (dj_sdc[i] > 3 || dj_sac[i] > 3) { dj_err = PDE_SYNTAX; return; }
        if (dj_sdc[i] > 1 || dj_sac[i] > 1) { dj_err = PDE_FILTER; return; }
    }
    dj_ss = dj_rd8(); dj_se = dj_rd8();
    t = dj_rd8();
    dj_ah = t >> 4; dj_al = t & 15;
    dj_segskip();
    if (dj_err) return;
    if (dj_prog) {
        if (dj_ss > dj_se || dj_se > 63 || dj_al > 13 || (dj_ss == 0 && dj_se != 0) ||
            (dj_ss > 0 && dj_nsc != 1)) { dj_err = PDE_SYNTAX; return; }
    } else {
        dj_ss = 0; dj_se = 63; dj_ah = 0; dj_al = 0;
    }
}

/* one marker. 0 go on, 1 an SOS header is ready, 2 the image ended */
static pdw dj_do_marker(pdi m)
{
    pdw len, a, b;
    if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) return 0;
    if (m == 0xD9) return 2;
    dj_seg = 2;
    len = dj_rd16();
    if (dj_err) return 0;
    if (len < 2) { dj_err = PDE_SYNTAX; return 0; }
    dj_seg = (pdw)(len - 2);
    switch (m) {
    case 0xC0: case 0xC1: case 0xC2:
        dj_sof((pdw)m); break;
    case 0xC4: dj_dht(); break;
    case 0xDB: dj_dqt(); break;
    case 0xDD: dj_ri = dj_rd16(); break;
    case 0xDA: dj_sos(); if (!dj_err) return 1; break;
    case 0xE0:
        if (dj_seg >= 5) {
            a = dj_rd8(); b = dj_rd8();
            if (a == 'J' && b == 'F' && dj_rd8() == 'I' && dj_rd8() == 'F' && dj_rd8() == 0) dj_jfif = 1;
        }
        break;
    case 0xEE:
        if (dj_seg >= 12) {
            a = dj_rd8(); b = dj_rd8();
            if (a == 'A' && b == 'd' && dj_rd8() == 'o' && dj_rd8() == 'b' && dj_rd8() == 'e') {
                dj_rd8(); dj_rd8(); dj_rd8(); dj_rd8(); dj_rd8(); dj_rd8();
                dj_atrans = dj_rd8();
                dj_adobe = 1;
            }
        }
        break;
    case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB:
    case 0xCD: case 0xCE: case 0xCF:
        dj_err = PDE_FILTER; break;
    default: break;
    }
    dj_segskip();
    return 0;
}

/* read markers until an SOS header (1), the end (2) or an error (dj_err) */
static pdw dj_headers(void)
{
    pdi m;
    pdw r;
    for (;;) {
        m = dj_marker();
        if (m < 0) return 2;
        r = dj_do_marker(m);
        if (dj_err) return 2;
        if (r) return r;
    }
}

#define DJ_MAXW 64496u        /* 0xFFF0 less the 1,024-byte landing zone (the 64 KiB slot) */
#ifndef PDMEM_SLOT
#define PDMEM_SLOT 0          /* CX of the answer: CF=0 makes it 0 */
#endif
/* ---- the plan: colour, scale, memory ---------------------------------------- */
static const pdb dj_nslots[4] = { 64, 16, 4, 1 };
static const pdb dj_maxzzs[4] = { 63, 24, 4, 0 };

/* bytes a stored block takes at scale s: its slots, and 4 mask words when `mask`. At 1/1 every
 * coefficient is kept, so "nonzero" is the value; at 1/8 no AC scan is read. Only at 1/2 and 1/4 does a
 * refinement scan need a bit for coefficients it does not keep. */
static pdw dj_bstr(pdw s, pdw mask) { return (pdw)((dj_nslots[s] + (mask ? 4 : 0)) * 2); }

/* does everything fit at scale s? lays the workspace out when it does */
static pdw dj_fit(pdw s, pdw mask, pdw dec)
{
    pdw i, bs, ow, pw, tot, add, nb;
    struct dj_comp *c;
    bs = (pdw)(8 >> s);
    ow = (pdw)((dj_W + ((pdw)(1u << s) << dec) - 1) / ((pdw)(1u << s) << dec));
    if (ow > 1024 || (pdw)(ow * dj_wn) > 1024) return 0;   /* the row buffer holds every sample of a pixel it works on */
    tot = 0;
    if (dj_stored) {
        for (i = 0; i < dj_nc; i++) {
            c = &dj_c[i];
            if (!c->used) continue;
            pdm_u32_mul16(&dj_u, (pdw)((c->pw + (1u << dec) - 1) >> dec), (pdw)((c->ph + (1u << dec) - 1) >> dec));
            if (dj_u.hi) return 0;
            nb = dj_u.lo;
            pdm_u32_mul16(&dj_u, nb, dj_bstr(s, mask));
            if (dj_u.hi) return 0;
            if (dj_u.lo > (pdw)(g_wbytes - tot)) return 0;
            c->base = tot;
            tot = (pdw)(tot + dj_u.lo);
        }
    }
    for (i = 0; i < dj_nc; i++) {
        c = &dj_c[i];
        if (!c->used) continue;
        pw = (pdw)(c->pw * bs);
        if (pw > DJ_PROW) return 0;
        add = (pdw)(pw * c->v * bs);
        if (add > (pdw)(g_wbytes - tot)) return 0;
        c->pstride = pw; c->pbase = tot;
        tot = (pdw)(tot + add);
    }
    dj_need = tot;
    return 1;
}

static pdw dj_plan(void)
{
    pdw i, s, sr, ow, oh, wantw, wanth, used1, rgbids;
    struct dj_comp *c;
    if (!dj_gotsof) return PDE_SYNTAX;
    for (i = 0; i < dj_nc; i++) if (!dj_qset[dj_c[i].tq] && 0) return PDE_SYNTAX;
    if (dj_nc == 1) { dj_c[0].h = 1; dj_c[0].v = 1; }
    dj_hmax = 1; dj_vmax = 1;
    for (i = 0; i < dj_nc; i++) {
        if (dj_c[i].h > dj_hmax) dj_hmax = dj_c[i].h;
        if (dj_c[i].v > dj_vmax) dj_vmax = dj_c[i].v;
    }
    for (i = 0; i < dj_nc; i++)
        if ((dj_hmax % dj_c[i].h) || (dj_vmax % dj_c[i].v)) return PDE_FILTER;
    dj_mcux = (pdw)((dj_W + 8 * dj_hmax - 1) / (8 * dj_hmax));
    dj_mcuy = (pdw)((dj_H + 8 * dj_vmax - 1) / (8 * dj_vmax));
    for (i = 0; i < dj_nc; i++) {
        c = &dj_c[i];
        c->pw = (pdw)(dj_mcux * c->h); c->ph = (pdw)(dj_mcuy * c->v);
        c->bw = (pdw)((((dj_W * c->h + dj_hmax - 1) / dj_hmax) + 7) >> 3);
        c->bh = (pdw)((((dj_H * c->v + dj_vmax - 1) / dj_vmax) + 7) >> 3);
        if (c->bw > c->pw) c->bw = c->pw;
        if (c->bh > c->ph) c->bh = c->ph;
        c->pred = 0;
    }
    /* the colour transform, libjpeg's rules */
    dj_xform = 0;
    if (dj_nc == 3) {
        rgbids = dj_c[0].id == 'R' && dj_c[1].id == 'G' && dj_c[2].id == 'B';
        if (dj_jfif) dj_xform = 1;
        else if (dj_adobe) dj_xform = dj_atrans ? 1 : 0;
        else dj_xform = rgbids ? 0 : 1;
    } else if (dj_nc == 4) {
        dj_xform = (dj_adobe && dj_atrans == 2) ? 2 : 0;
    }
    dj_native = (g_im.flags & PDIMG_NATIVE) ? 1 : 0;
    dj_invert = (g_im.flags & PDIMG_INVERT) ? 1 : 0;
    dj_outn = dj_native ? dj_nc : 1;
    used1 = (!dj_native && (dj_nc == 1 || (dj_nc == 3 && dj_xform == 1)));
    dj_nused = 0;
    for (i = 0; i < dj_nc; i++) {
        dj_c[i].used = (used1 ? i == 0 : 1);
        if (dj_c[i].used) dj_nused++;
    }
    dj_wn = (dj_native || dj_nused > 1) ? dj_nc : 1;
    dj_stored = dj_prog || dj_first_nsc != dj_nc;
    /* the largest reduction that still gives want_w x want_h */
    wantw = g_im.want_w ? g_im.want_w : 0xFFFF;
    wanth = g_im.want_h ? g_im.want_h : 0xFFFF;
    sr = 0;
    for (s = 3; s > 0; s--) {
        ow = (pdw)((dj_W + (1u << s) - 1) >> s);
        oh = (pdw)((dj_H + (1u << s) - 1) >> s);
        if (ow >= wantw && oh >= wanth) { sr = s; break; }
    }
    /* the smallest reduction that fits; at 1/2 and 1/4 first with the nonzero mask (every refinement
     * scan decoded), then without it (the AC refinement scans skipped: a coefficient keeps its first-scan
     * precision, which costs at most one quantiser step in the few coefficients kept) */
    dj_mask = 0; dj_dec = 0;
    /* a progressive image that would fit its finer scale in a bigger slot asks for one (PDE_NEEDMEM,
     * CX = PDMEM_SLOT = 0, DX = KiB); asked once per image, so a refused regrow falls to the loop below */
    if (dj_stored && sr <= 3 && g_wbytes < DJ_MAXW) {
        pdw m = (pdw)(sr == 1 || sr == 2), ok, save, kb;
        ok = dj_fit(sr, m, 0) || (m && dj_fit(sr, 0, 0));
        if (!ok) {
            save = g_wbytes; g_wbytes = DJ_MAXW;
            ok = dj_fit(sr, m, 0) || (m && dj_fit(sr, 0, 0));
            g_wbytes = save;
            kb = (pdw)((dj_need + 2047) >> 10);
            if (ok && !(dj_askh == g_im.h && dj_askkb == kb)) {
                dj_askh = g_im.h; dj_askkb = kb;
                pdi_rdx = kb;
                return PDE_NEEDMEM;
            }
        }
    }
    for (s = sr; s <= 3; s++) {
        if (s == 1 || s == 2) { dj_mask = 1; if (dj_fit(s, 1, 0)) break; }
        dj_mask = 0;
        if (dj_fit(s, 0, 0)) break;
    }
    /* nothing fits even at 1/8: a progressive grey image keeps one block of 2 (then 4) each way, so a
     * 2,400 x 1,800 photograph still draws, as a 150 x 113 (then 75 x 57) picture */
    if (s > 3 && dj_stored && dj_nused == 1 && dj_wn == 1 && dj_c[0].v == dj_vmax) {
        for (i = 1; i <= 2; i++) if (dj_fit(3, 0, i)) { dj_dec = i; s = 3; break; }
    }
    if (s > 3) return PDE_LIMIT;
    dj_s = s; dj_bs = (pdw)(8 >> s);
    dj_ow = (pdw)((dj_W + ((pdw)(1u << s) << dj_dec) - 1) / ((pdw)(1u << s) << dj_dec));
    dj_oh = (pdw)((dj_H + ((pdw)(1u << s) << dj_dec) - 1) / ((pdw)(1u << s) << dj_dec));
    dj_mrh = (pdw)(dj_vmax * dj_bs);
    dj_nslot = dj_nslots[s]; dj_bstride = dj_bstr(s, dj_mask);
    dj_maxzz = dj_maxzzs[s];
    /* the slot maps */
    for (i = 0; i < 64; i++) dj_nmap[i] = 255;
    if (s == 0) { for (i = 0; i < 64; i++) { dj_nmap[i] = (pdb)i; dj_sl2pos[i] = (pdb)i; } }
    else if (s == 1) { for (i = 0; i < 16; i++) { dj_sl2pos[i] = (pdb)(((i >> 2) << 3) + (i & 3)); dj_nmap[dj_sl2pos[i]] = (pdb)i; } }
    else if (s == 2) { for (i = 0; i < 4; i++) { dj_sl2pos[i] = (pdb)(((i >> 1) << 3) + (i & 1)); dj_nmap[dj_sl2pos[i]] = (pdb)i; } }
    else { dj_sl2pos[0] = 0; dj_nmap[0] = 0; }
    return PDE_OK;
}

/* ---- the transform of one block into a plane ---------------------------------- */
static pdi dj_ext(pdw v, pdw s)
{
    if (s == 0) return 0;
    if (s > 14) s = 14;
    if (v < (pdw)(1u << (s - 1))) return (pdi)(v + 1 - (1u << s));
    return (pdi)v;
}

/* DC-only at 1/8: libjpeg's idct_1x1 */
static pdb dj_dc1(pdi dc, pdw q)
{
    pdi v;
    v = (pdi)((pdi)(dc * (pdi)q + 4) >> 3);
    v = (pdi)(v + 128);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (pdb)v;
}

/* the block's coefficients (natural order, undequantised) are in pdi_coef */
static void dj_put(struct dj_comp *c, pdw gbx, pdw by)
{
    pdw off;
    if (dj_s == 3) {
        pd_fpokeb(g_wseg, (pdw)(c->pbase + by * c->pstride + gbx), dj_dc1(pdi_coef[0], dj_qt[c->tq][0]));
        return;
    }
    off = (pdw)(c->pbase + by * dj_bs * c->pstride + gbx * dj_bs);
    pdi_idct(pdi_coef, dj_qt[c->tq], dj_s, g_wseg, off, c->pstride);
}

/* ---- streaming baseline ----------------------------------------------------- */
static void dj_mcurow_stream(pdw xform)
{
    pdw mx, k, i, bx, by;
    struct dj_comp *c;
    pdi d;
    for (mx = 0; mx < dj_mcux; mx++) {
        if (dj_ri) {
            if (dj_rleft == 0) { dj_restart(); dj_rleft = dj_ri; }
            dj_rleft--;
        }
        for (k = 0; k < dj_nsc; k++) {
            c = &dj_c[dj_sci[k]];
            for (by = 0; by < c->v; by++)
                for (bx = 0; bx < c->h; bx++) {
                    dj_ensure();
                    d = pdi_block(&dj_ht[dj_sdc[k]], &dj_ht[2 + dj_sac[k]], pdi_coef);
                    c->pred = (pdi)(c->pred + d);
                    pdi_coef[0] = c->pred;
                    if (xform && c->used) dj_put(c, (pdw)(mx * c->h + bx), by);
                }
        }
    }
    (void)i;
}

/* ---- stored: one block of a scan ---------------------------------------------- */
static pdw dj_isnz(pdi *b, pdw k)
{
    if (dj_mask) return (pdw)b[dj_nslot + (k >> 4)] & (pdw)(1u << (k & 15));
    return (pdw)b[pdi_zz[k]];           /* 1/1: every coefficient is kept */
}
static void dj_setnz(pdi *b, pdw k)
{
    if (dj_mask) b[dj_nslot + (k >> 4)] = (pdi)((pdw)b[dj_nslot + (k >> 4)] | (pdw)(1u << (k & 15)));
}

static void dj_refbit(pdi *b, pdw k, pdi p1)
{
    pdw sl;
    pdi t;
    if (!pdi_bits(1)) return;
    sl = dj_nmap[pdi_zz[k]];
    if (sl == 255) return;
    t = b[sl];
    if ((t & p1) == 0) b[sl] = (pdi)(t >= 0 ? t + p1 : t - p1);
}

static void dj_ac_first(pdi *b, struct dj_huff *act)
{
    pdw k, rs, r, s, sl;
    pdi v;
    if (dj_eobrun) { dj_eobrun--; return; }
    for (k = dj_ss; k <= dj_se; k++) {
        rs = pdi_hsym(act); r = rs >> 4; s = rs & 15;
        if (s) {
            k = (pdw)(k + r);
            if (k > 63) break;
            v = dj_ext(pdi_bits(s), s);
            sl = dj_nmap[pdi_zz[k]];
            if (sl != 255) b[sl] = (pdi)((pdw)v << dj_al);
            dj_setnz(b, k);
        } else {
            if (r == 15) k = (pdw)(k + 15);
            else {
                dj_eobrun = (pdw)(1u << r);
                if (r) dj_eobrun = (pdw)(dj_eobrun + pdi_bits(r));
                dj_eobrun--;
                break;
            }
        }
    }
}

static void dj_ac_refine(pdi *b, struct dj_huff *act)
{
    pdw k, rs, s, sl;
    pdi p1, m1, r, nv;
    p1 = (pdi)(1 << dj_al); m1 = (pdi)(-p1);
    k = dj_ss;
    if (dj_eobrun == 0) {
        for (; k <= dj_se; k++) {
            rs = pdi_hsym(act); r = (pdi)(rs >> 4); s = rs & 15;
            nv = 0;
            if (s) nv = pdi_bits(1) ? p1 : m1;
            else if (r != 15) {
                dj_eobrun = (pdw)(1u << r);
                if (r) dj_eobrun = (pdw)(dj_eobrun + pdi_bits(r));
                break;
            }
            do {
                if (dj_isnz(b, k)) dj_refbit(b, k, p1);
                else { if (--r < 0) break; }
                k++;
            } while (k <= dj_se);
            if (nv && k <= dj_se) {
                sl = dj_nmap[pdi_zz[k]];
                if (sl != 255) b[sl] = nv;
                dj_setnz(b, k);
            }
        }
    }
    if (dj_eobrun) {
        for (; k <= dj_se; k++) if (dj_isnz(b, k)) dj_refbit(b, k, p1);
        dj_eobrun--;
    }
}

static pdw dj_boff(struct dj_comp *c, pdw bx, pdw by)
{
    pdm_u32_mul16(&dj_u, (pdw)((by >> dj_dec) * ((c->pw + (1u << dj_dec) - 1) >> dj_dec) + (bx >> dj_dec)), dj_bstride);
    return (pdw)(c->base + dj_u.lo);
}

static void dj_block(pdw k, pdw bx, pdw by)
{
    struct dj_comp *c;
    pdw o, sl, i, s;
    pdi d;
    struct dj_huff *dct, *act;
    c = &dj_c[dj_sci[k]];
    dct = &dj_ht[dj_sdc[k]]; act = &dj_ht[2 + dj_sac[k]];
    if (dj_dec && ((bx | by) & (pdw)((1u << dj_dec) - 1))) {      /* not kept: the predictor still moves */
        if (dj_ss == 0) {
            if (dj_ah == 0) {
                s = pdi_hsym(dct);
                c->pred = (pdi)(c->pred + (s ? dj_ext(pdi_bits(s > 16 ? 16 : s), s) : 0));
            } else pdi_bits(1);
        }
        return;
    }
    if (!c->used) {                         /* parse and discard */
        if (!dj_prog) { pdi_block(dct, act, pdi_coef); return; }
        if (dj_ss == 0) {
            if (dj_ah == 0) { s = pdi_hsym(dct); if (s) pdi_bits(s > 16 ? 16 : s); }
            else pdi_bits(1);
        }
        return;
    }
    o = dj_boff(c, bx, by);
    pd_fcopy_in(g_wseg, o, dj_blk, dj_bstride);
    if (!dj_prog) {
        d = pdi_block(dct, act, pdi_coef);
        c->pred = (pdi)(c->pred + d);
        pdi_coef[0] = c->pred;
        for (i = 0; i < 64; i++) { sl = dj_nmap[i]; if (sl != 255) dj_blk[sl] = pdi_coef[i]; }
    } else if (dj_ss == 0) {
        if (dj_ah == 0) {
            s = pdi_hsym(dct);
            d = s ? dj_ext(pdi_bits(s > 16 ? 16 : s), s) : 0;
            c->pred = (pdi)(c->pred + d);
            dj_blk[0] = (pdi)((pdw)c->pred << dj_al);
        } else if (pdi_bits(1)) dj_blk[0] = (pdi)(dj_blk[0] | (1 << dj_al));
    } else if (dj_ah == 0) dj_ac_first(dj_blk, act);
    else dj_ac_refine(dj_blk, act);
    pd_fcopy_out(dj_blk, g_wseg, o, dj_bstride);
}

/* one whole scan into the store */
static void dj_scan_stored(void)
{
    pdw i, k, mx, my, bx, by, want, bw, bh;
    struct dj_comp *c;
    want = 0;
    for (i = 0; i < dj_nsc; i++) if (dj_c[dj_sci[i]].used) want = 1;
    if (!want || (dj_prog && dj_ss > dj_maxzz) || (dj_prog && dj_ah && dj_ss && dj_s && !dj_mask)) { dj_skip_scan(); return; }
    for (i = 0; i < 4; i++) dj_c[i].pred = 0;
    dj_eobrun = 0; dj_rleft = dj_ri;
    if (dj_nsc == 1) {
        c = &dj_c[dj_sci[0]];
        bw = c->bw; bh = c->bh;
        for (by = 0; by < bh; by++)
            for (bx = 0; bx < bw; bx++) {
                if (dj_ri) {
                    if (dj_rleft == 0) { dj_restart(); dj_rleft = dj_ri; }
                    dj_rleft--;
                }
                dj_ensure();
                dj_block(0, bx, by);
            }
    } else {
        for (my = 0; my < dj_mcuy; my++)
            for (mx = 0; mx < dj_mcux; mx++) {
                if (dj_ri) {
                    if (dj_rleft == 0) { dj_restart(); dj_rleft = dj_ri; }
                    dj_rleft--;
                }
                for (k = 0; k < dj_nsc; k++) {
                    c = &dj_c[dj_sci[k]];
                    for (by = 0; by < c->v; by++)
                        for (bx = 0; bx < c->h; bx++) {
                            dj_ensure();
                            dj_block(k, (pdw)(mx * c->h + bx), (pdw)(my * c->v + by));
                        }
                }
            }
    }
    dj_scan_end();
}

/* every scan, from the one whose header is parsed to the end */
static pdw dj_decode_all(void)
{
    pdw r;
    pdi m;
    /* zero the stored area: an unreceived coefficient is 0 */
    {
        pdw i;
        for (i = 0; i < dj_nc; i++)
            if (dj_c[i].used) {
                pdm_u32_mul16(&dj_u, (pdw)(((dj_c[i].pw + (1u << dj_dec) - 1) >> dj_dec) * ((dj_c[i].ph + (1u << dj_dec) - 1) >> dj_dec)), dj_bstride);
                pd_ffill(g_wseg, dj_c[i].base, 0, dj_u.lo);
            }
    }
    for (;;) {
        dj_ereset();
        dj_scan_stored();
        if (dj_eeof && !dj_emk) break;
        for (;;) {
            m = dj_marker();
            if (m < 0) { r = 2; break; }
            r = dj_do_marker(m);
            if (dj_err) return dj_err;
            if (r) break;
        }
        if (r == 2) break;
    }
    dj_decoded = 1;
    return PDE_OK;
}

/* ---- one MCU row of planes from the store ----------------------------------- */
static void dj_mcurow_stored(pdw m)
{
    pdw i, bx, by, o, j, sl;
    struct dj_comp *c;
    for (i = 0; i < dj_nc; i++) {
        c = &dj_c[i];
        if (!c->used) continue;
        for (by = 0; by < c->v; by++)
            for (bx = 0; bx < c->pw; bx++) {
                if (dj_dec && ((bx | (pdw)(m * c->v + by)) & (pdw)((1u << dj_dec) - 1))) continue;
                o = dj_boff(c, bx, (pdw)(m * c->v + by));
                if (dj_s == 0) pd_fcopy_in(g_wseg, o, pdi_coef, 128);
                else {
                    pd_fcopy_in(g_wseg, o, dj_blk, dj_bstride);
                    for (j = 0; j < 64; j++) pdi_coef[j] = 0;
                    for (sl = 0; sl < dj_nslot; sl++) pdi_coef[dj_sl2pos[sl]] = dj_blk[sl];
                }
                dj_put(c, bx, by);
            }
    }
}

/* ---- colour and the row out --------------------------------------------------- */
static pdw dj_clamp(pdi v) { return v < 0 ? 0 : (v > 255 ? 255 : (pdw)v); }

static void dj_ycc(pdb *p, pdw k)
{
    pdi y, cb, cr;
    y = p[0]; cb = (pdi)(p[1] - 128); cr = (pdi)(p[2] - 128);
    p[0] = (pdb)dj_clamp((pdi)(y + cr + ((cr * 103 + 128) >> 8)));
    p[1] = (pdb)dj_clamp((pdi)(y - ((cb * 88 + 128) >> 8) - ((cr * 183 + 128) >> 8)));
    p[2] = (pdb)dj_clamp((pdi)(y + cb + cb - ((cb * 58 + 128) >> 8)));
    if (k == 4) { p[0] = (pdb)(255 - p[0]); p[1] = (pdb)(255 - p[1]); p[2] = (pdb)(255 - p[2]); }
}

static pdw dj_mul255(pdw a, pdw b)
{
    pdw t;
    t = (pdw)(a * b + 128);
    return (pdw)((t + (t >> 8)) >> 8);
}

static void dj_outrow(pdw row, pdw seg, pdw off)
{
    pdw m, yy, i, j, x, xs, cnt, rx, n, nc, v, a, b, cc, mm, ky, kk;
    struct dj_comp *c;
    pdb *p;
    row = (pdw)(row << dj_dec);
    m = (pdw)(row / dj_mrh);
    yy = (pdw)(row - m * dj_mrh);
    if (dj_mready != (pdi)m) {
        if (dj_stored) dj_mcurow_stored(m);
        dj_mready = (pdi)m;
    }
    nc = dj_nc;
    if (dj_nused == 1 && dj_outn == 1) {
        c = &dj_c[0];
        for (i = 0; i < nc; i++) if (dj_c[i].used) c = &dj_c[i];
        pd_fcopy_in(g_wseg, (pdw)(c->pbase + (yy / (dj_vmax / c->v)) * c->pstride), dj_prow, c->pstride);
        rx = (pdw)(dj_hmax / c->h);
        if (dj_dec) for (x = 0; x < dj_ow; x++) dj_prow[x] = dj_prow[x << dj_dec];
        if (rx == 1) p = dj_prow;
        else {
            xs = 0; cnt = 0;
            for (x = 0; x < dj_ow; x++) {
                dj_orow[x] = dj_prow[xs];
                if (++cnt == rx) { cnt = 0; xs++; }
            }
            p = dj_orow;
        }
        if (dj_invert) {
            if (p != dj_orow) { for (x = 0; x < dj_ow; x++) dj_orow[x] = dj_prow[x]; p = dj_orow; }
            for (x = 0; x < dj_ow; x++) dj_orow[x] = (pdb)(255 - dj_orow[x]);
        }
        pd_fcopy_out(p, seg, off, dj_ow);
        return;
    }
    for (j = 0; j < nc; j++) {
        c = &dj_c[j];
        pd_fcopy_in(g_wseg, (pdw)(c->pbase + (yy / (dj_vmax / c->v)) * c->pstride), dj_prow, c->pstride);
        rx = (pdw)(dj_hmax / c->h);
        xs = 0; cnt = 0;
        for (x = 0; x < dj_ow; x++) {
            dj_orow[x * nc + j] = dj_prow[xs];
            if (++cnt == rx) { cnt = 0; xs++; }
        }
    }
    if (dj_xform) for (x = 0; x < dj_ow; x++) dj_ycc(&dj_orow[x * nc], nc);
    if (dj_native) { pd_fcopy_out(dj_orow, seg, off, (pdw)(dj_ow * nc)); return; }
    /* grey from RGB or CMYK */
    n = dj_ow;
    for (x = 0; x < n; x++) {
        p = &dj_orow[x * nc];
        if (nc == 3) v = (pdw)((77 * (pdw)p[0] + 150 * (pdw)p[1] + 29 * (pdw)p[2]) >> 8);
        else {
            kk = p[3]; cc = p[0]; mm = p[1]; ky = p[2];
            if (!dj_invert) { kk = (pdw)(255 - kk); cc = (pdw)(255 - cc); mm = (pdw)(255 - mm); ky = (pdw)(255 - ky); }
            a = dj_mul255(cc, kk); b = dj_mul255(mm, kk);
            ky = dj_mul255(ky, kk);
            v = (pdw)((77 * a + 150 * b + 29 * ky) >> 8);
        }
        if (dj_invert && nc == 3) v = (pdw)(255 - v);
        dj_orow[x] = (pdb)v;
    }
    pd_fcopy_out(dj_orow, seg, off, n);
}

/* ---- the codec's three entry points ------------------------------------------- */
void pdi_dct_drop(void) { dj_warm = 0; }

static void dj_reset(void)
{
    pdw i;
    for (i = 0; i < 4; i++) dj_ht[i].set = 0;
    for (i = 0; i < 4; i++) dj_qset[i] = 0;
    dj_gotsof = 0; dj_ri = 0; dj_jfif = 0; dj_adobe = 0; dj_atrans = 0;
    dj_err = 0; dj_decoded = 0; dj_done = 0; dj_mready = -1; dj_mnext = 0;
    dj_rpos = 0; dj_rend = 0; dj_reof = 0; dj_seg = 0;
    dj_ereset();
    for (i = 0; i < DJ_PAD; i++) pdi_ebuf[i] = 0;
}

static void dj_fillout(void)
{
    g_im.out_w = dj_ow; g_im.out_h = dj_oh;
    g_im.out_mode = dj_native ? PDIM_N8 : PDIM_GREY8;
    g_im.out_ncomp = dj_outn;
    g_im.rowbytes = (pdw)(dj_ow * dj_outn);
    g_im.scale = dj_s;
}

pdw pdi_dct_open(void)
{
    pdw r, i, same;
    dj_k[0] = g_im.objnum; dj_k[1] = g_im.gen; dj_k[2] = g_im.w; dj_k[3] = g_im.h;
    dj_k[4] = g_im.want_w; dj_k[5] = g_im.want_h; dj_k[6] = g_im.flags;
    if (dj_warm && g_im.objnum) {
        same = 1;
        for (i = 0; i < 7; i++) if (dj_k[i] != dj_wk[i]) same = 0;
        if (same) { dj_mready = -1; dj_fillout(); return PDE_OK; }
    }
    dj_warm = 0;
    r = pdi_src_open();
    if (r) return r;
    dj_reset();
    for (i = 0; i < 4; i++) dj_c[i].pred = 0;
    r = dj_headers();
    if (dj_err) return dj_err;
    if (r != 1) return PDE_SYNTAX;
    dj_first_nsc = dj_nsc;
    r = dj_plan();
    if (r) return r;
    if (dj_stored) {
        r = dj_decode_all();
        if (r) return r;
        if (g_im.objnum) { for (i = 0; i < 7; i++) dj_wk[i] = dj_k[i]; dj_warm = 1; }
    } else {
        dj_rleft = dj_ri;
    }
    dj_fillout();
    return PDE_OK;
}

pdw pdi_dct_row(pdw seg, pdw off, pdw row)
{
    pdw m;
    if (row >= dj_oh) return PDE_RANGE;
    m = (pdw)(row / dj_mrh);
    if (!dj_stored) {
        if ((pdi)m < dj_mready) return PDE_RANGE;
        while (dj_mnext <= m) {
            dj_mcurow_stream(dj_mnext == m);
            dj_mnext++;
        }
        dj_mready = (pdi)m;
    }
    dj_outrow(row, seg, off);
    return PDE_OK;
}

void pdi_dct_close(void) { }
