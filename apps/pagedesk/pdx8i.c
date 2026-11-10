/* ============================================================================
 * os8088 - apps/pagedesk/pdx8i.c      PDX8: the interpretation side (20.22.10.3-4)
 *
 * #included by pdx8.c. OWNED BY blend. What PDREND asks while it interprets the
 * page, once the region and this part are there: the ExtGState parse and the
 * fold of the gs operators into a tuple, the XSTATE / GROUP / MASKB / MASKE
 * records (answered as bytes in the exchange block for PDREND to put in its
 * list), the mark map. The tuple pool and the map live here, in bss, so they
 * cost PDREND nothing and exist exactly when transparency does.
 *
 * The effective transparency is the FOLD of the gs operators so far, so it is
 * a tuple {ca, CA, bm, smask} in a per-pass pool and PDREND keeps its INDEX in
 * the gstate (0 = opaque Normal, no mask): q, Q and a form's push already save it.
 * ==========================================================================*/

#define GSP_FILL    0x01    /* /ca                                        */
#define GSP_STROKE  0x02    /* /CA                                        */
#define GSP_BM      0x04
#define GSP_SMASK   0x08    /* /SMask (None included)                     */
struct pdxb_gs {
    pdw  objnum, gen;       /* in: the ExtGState object                   */
    pdb  present;           /* out: GSP_* keys the dictionary has         */
    pdb  fill_a, strk_a;    /* out: 0..255 (/ca non-stroking, /CA stroking) */
    pdb  bm;                /* out: 0..15 (20.22.10.3's table)            */
    pdb  smode;             /* out: 0 /None, 1 /Luminosity, 2 /Alpha      */
    pdb  bc;                /* out: /BC as a luminosity 0..255            */
    pdb  trnon;             /* out: /TR present and not /Identity         */
    pdb  pad_;
    pdw  gnum, ggen;        /* out: the mask's /G form                    */
};

static pdi g_vcy0;              /* the viewport's top row                       */
static pdb g_x8map[32];         /* one bit a 16-row band (256 bands)            */
static pdb g_xt[32][4];         /* ca, CA, bm, smask (id + 1); index 0 unused   */
static pdw g_xtn;
static pdb g_xl[3];             /* the last XSTATE written: alpha, bm, smask    */
static pdw g_xmaskpend, g_xmaskid, g_xpkind, g_xpbc, g_xpca, g_xpCA, g_xpbm;
static struct { pdb kind; pdb id; pdi y0, y1; } g_xfr[6];   /* per frame: 1 group, 2 mask */
static struct pdxb_dictget gt_q;
static struct pdxb_gs gt_g;
static pdw g_xnt;
static pdw g_gm[4][4];          /* memo: obj, gen, tuple in, tuple out (0xFFFF = empty); no mask */
static pdw g_gmn;

/* the raw PDENG answer for gt_q, the final reference NOT followed */
static pdw x8_rawget(void)
{
    pdw e;
    pd_fcopy_out(&gt_q, g_xb, 0, sizeof gt_q);
    e = x8_eng(PDV_ENG_DICT_GET, 0, 0, 0);
    if (!e) pd_fcopy_in(g_xb, 0, &gt_q, sizeof gt_q);
    return e;
}

static void x8_zero(void *d, pdw n) { pdb *p = (pdb *)d; while (n--) *p++ = 0; }
/* one key of an object's dictionary (k1 a second path element or 0); a final
 * reference is followed. 0 = gt_q holds the value */
static pdw x8_dget(pdw obj, pdw gen, const char *k0, const char *k1)
{
    pdw i, e;
    for (;;) {
        x8_zero(&gt_q, sizeof gt_q);
        gt_q.objnum = obj; gt_q.gen = gen;
        if (k0) for (i = 0; i < 31 && k0[i]; i++) gt_q.key[0][i] = k0[i];
        if (k0 && k1) for (i = 0; i < 31 && k1[i]; i++) gt_q.key[1][i] = k1[i];
        e = x8_rawget();
        if (e) return e;
        if (gt_q.kind != PDK_REF || !k0) return 0;
        obj = gt_q.refnum; gen = gt_q.refgen; k0 = 0; k1 = 0;
    }
}

/* a 16.16 number in 0..1 as 0..255, rounded; outside the range clamps */
static pdw gt_u8(const struct pd_fx *f)
{
    if (f->hi < 0) return 0;
    if (f->hi >= 1 || f->lo >= 0xFF80u) return 255;
    return (pdw)((f->lo + 128u) >> 8);
}

static void gt_key(const char *k0, const char *k1)
{
    pdw i;
    gt_q.objnum = gt_g.objnum; gt_q.gen = gt_g.gen;
    for (i = 0; i < 32; i++) { gt_q.key[0][i] = 0; gt_q.key[1][i] = 0; gt_q.key[2][i] = 0; }
    for (i = 0; i < 31 && k0[i]; i++) gt_q.key[0][i] = k0[i];
    if (k1) for (i = 0; i < 31 && k1[i]; i++) gt_q.key[1][i] = k1[i];
}
/* the value at k0 / k1, 0 when it exists */
static pdw gt_get(const char *k0, const char *k1)
{
    gt_key(k0, k1);
    return x8_rawget();
}
static pdw gt_is(const char *name)
{
    pdw i = 0;
    if (gt_q.kind != PDK_NAME) return 0;
    while (name[i] && gt_q.text[i] == name[i]) i++;
    return name[i] == 0 && gt_q.text[i] == 0;
}

/* the blend mode names, in the order of 20.22.10.3's codes */
static const char *const gt_bms[16] = {
    "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "ColorDodge", "ColorBurn",
    "HardLight", "SoftLight", "Difference", "Exclusion", "Hue", "Saturation", "Color", "Luminosity" };
static pdw gt_bm(void)
{
    pdw i;
    if (gt_is("Compatible")) return 0;
    for (i = 0; i < 16; i++) if (gt_is(gt_bms[i])) return i;
    return 0xFF;
}

/* /BC as a luminosity: 1 gray, 3 RGB, 4 CMYK components */
static pdw gt_bc(void)
{
    static pdb c[4];
    pdw n, i, r, g, b, k;
    static char idx[4] = { '[', '0', ']', 0 };
    if (gt_get("SMask", "BC") || gt_q.kind != PDK_ARR) return 0;
    n = gt_q.count;
    if (n != 1 && n != 3 && n != 4) return 0;
    for (i = 0; i < n; i++) {
        idx[1] = (char)('0' + i);
        gt_key("SMask", "BC");
        { pdw j; for (j = 0; j < 4; j++) gt_q.key[2][j] = idx[j]; }
        if (x8_rawget()) return 0;
        c[i] = (pdb)gt_u8(&gt_q.num);
    }
    if (n == 1) return c[0];
    if (n == 3) return (pdw)((c[0] * 77u + c[1] * 150u + c[2] * 29u) >> 8);
    k = c[3];
    r = ((255u - c[0]) * (255u - k)) / 255u;
    g = ((255u - c[1]) * (255u - k)) / 255u;
    b = ((255u - c[2]) * (255u - k)) / 255u;
    return (pdw)((r * 77u + g * 150u + b * 29u) >> 8);
}

/* parse the ExtGState in gt_g (objnum, gen) */
static void gt_parse(void)
{
    pdw v;
    gt_g.present = 0; gt_g.fill_a = gt_g.strk_a = 255; gt_g.bm = 0;
    gt_g.smode = 0; gt_g.bc = 0; gt_g.trnon = 0; gt_g.gnum = gt_g.ggen = 0;
    if (gt_get("ca", 0) == 0 && (gt_q.kind == PDK_INT || gt_q.kind == PDK_REAL)) {
        gt_g.fill_a = (pdb)gt_u8(&gt_q.num); gt_g.present |= GSP_FILL;
    }
    if (gt_get("CA", 0) == 0 && (gt_q.kind == PDK_INT || gt_q.kind == PDK_REAL)) {
        gt_g.strk_a = (pdb)gt_u8(&gt_q.num); gt_g.present |= GSP_STROKE;
    }
    if (gt_get("BM", 0) == 0) {
        if (gt_q.kind == PDK_ARR) {                      /* the first name this table has */
            pdw i, n = gt_q.count;
            static char ix[4] = { '[', '0', ']', 0 };
            for (i = 0; i < n && i < 10; i++) {
                ix[1] = (char)('0' + i);
                if (gt_get("BM", ix)) continue;
                v = gt_bm();
                if (v != 0xFF) { gt_g.bm = (pdb)v; gt_g.present |= GSP_BM; break; }
            }
        } else {
            v = gt_bm();
            if (v != 0xFF) { gt_g.bm = (pdb)v; gt_g.present |= GSP_BM; }
        }
    }
    if (gt_get("SMask", 0) == 0) {
        gt_g.present |= GSP_SMASK;
        if (gt_q.kind == PDK_DICT) {
            if (gt_get("SMask", "S") == 0 && gt_is("Alpha")) gt_g.smode = 2;
            else gt_g.smode = 1;
            if (gt_get("SMask", "G") == 0 && gt_q.kind == PDK_REF) { gt_g.gnum = gt_q.refnum; gt_g.ggen = gt_q.refgen; }
            else gt_g.smode = 0;                            /* no group: no mask */
            if (gt_g.smode == 1) gt_g.bc = (pdb)gt_bc();
            if (gt_get("SMask", "TR") == 0 && !gt_is("Identity")) gt_g.trnon = 1;
        }
    }
}

/* ---- the pool, the map, the pass ------------------------------------------ */
static void x8d_trivial(void) { g_xl[0] = 255; g_xl[1] = 0; g_xl[2] = 0; }

static void x8d_pass(pdw vcy0)
{
    pdw i;
    g_vcy0 = (pdi)vcy0;
    g_xtn = 0; g_nmask = 0; g_xmaskpend = 0; g_xnt = 0;
    for (i = 0; i < 4; i++) g_gm[i][3] = 0xFFFF;
    x8d_trivial();
    for (i = 0; i < 32; i++) g_x8map[i] = 0;
    for (i = 0; i < 6; i++) g_xfr[i].kind = 0;
}

/* the rows a transparent mark touches: its 16-row bands are drawn in 8 bits */
static void x8d_rows(pdi y0, pdi y1)
{
    pdw b0, b1, b;
    if (y0 < g_vcy0) y0 = g_vcy0;
    if (y1 < y0) return;
    b0 = (pdw)((y0 - g_vcy0) >> 4); b1 = (pdw)((y1 - g_vcy0) >> 4);
    for (b = b0; b <= b1 && b < 256; b++) g_x8map[b >> 3] |= (pdb)(1 << (b & 7));
}
/* does the band of rows lo..hi hold a transparent mark */
static pdw x8d_want(pdi lo, pdi hi)
{
    pdw b0 = (pdw)((lo - g_vcy0) >> 4), b1 = (pdw)((hi - g_vcy0) >> 4), b;
    for (b = b0; b <= b1; b++)
        if (b > 255 || (g_x8map[b >> 3] & (1 << (b & 7)))) return 1;
    return 0;
}

/* a tuple's index (made if new); 0xFFFF = the pool is full */
static pdw x8d_tuple(pdw ca, pdw CA, pdw bm, pdw sm)
{
    pdw i;
    if (ca == 255 && CA == 255 && !bm && !sm) return 0;
    for (i = 1; i <= g_xtn; i++)
        if (g_xt[i][0] == ca && g_xt[i][1] == CA && g_xt[i][2] == bm && g_xt[i][3] == sm) return i;
    if (g_xtn >= 31) { g_x8f |= X8F_LIMIT; return 0xFFFF; }
    i = ++g_xtn;
    g_xt[i][0] = (pdb)ca; g_xt[i][1] = (pdb)CA; g_xt[i][2] = (pdb)bm; g_xt[i][3] = (pdb)sm;
    return i;
}

/* ---- the gs operator -------------------------------------------------------- */
/* BX = the tuple in force, CX/DX = the ExtGState. AX = the new tuple, CF + PDE_LIMIT
 * when it cannot be had (PDREND leaves the gstate alone). x8_ocx = 1: a soft mask's /G
 * is at XB+0 (obj, gen) and PDREND interprets it as a Form next; x8_odx = X8F_* bits. */
static pdw x8d_gs(pdw cur, pdw obj, pdw gen)
{
    pdw ca = 255, CA = 255, bm = 0, sm = 0, id, t, i;
    for (i = 0; i < 4; i++)
        if (g_gm[i][3] != 0xFFFF && g_gm[i][0] == obj && g_gm[i][1] == gen && g_gm[i][2] == cur) { x8_odx = 0; return g_gm[i][3]; }
    if (cur) { ca = g_xt[cur][0]; CA = g_xt[cur][1]; bm = g_xt[cur][2]; sm = g_xt[cur][3]; }
    g_x8f = 0;
    gt_g.objnum = obj; gt_g.gen = gen;
    gt_parse();
    if (gt_g.present & GSP_FILL)   ca = gt_g.fill_a;
    if (gt_g.present & GSP_STROKE) CA = gt_g.strk_a;
    if (gt_g.present & GSP_BM)     bm = gt_g.bm;
    if (gt_g.trnon) g_x8f |= X8F_TRANSP;
    if (gt_g.present & GSP_SMASK) {
        if (!gt_g.smode || !gt_g.gnum || g_nmask >= 16) {
            if (gt_g.smode && g_nmask >= 16) g_x8f |= X8F_LIMIT;
            sm = 0;
        } else {                                        /* define the mask: its /G as MASKB ... MASKE */
            id = g_nmask++; sm = (pdw)(id + 1);
            t = x8d_tuple(ca, CA, bm, sm);
            if (t == 0xFFFF) { g_nmask--; x8_cf = 1; x8_odx = g_x8f; return PDE_LIMIT; }
            g_xmaskpend = 1; g_xmaskid = id; g_xpkind = (pdw)(gt_g.smode - 1); g_xpbc = gt_g.bc;
            g_xpca = ca; g_xpCA = CA; g_xpbm = bm;
            pd_fpokew(g_xb, 0, gt_g.gnum); pd_fpokew(g_xb, 2, gt_g.ggen);
            x8_ocx = 1; x8_odx = g_x8f;
            return t;
        }
    }
    t = x8d_tuple(ca, CA, bm, sm);
    x8_odx = g_x8f;
    if (t == 0xFFFF) { x8_cf = 1; return PDE_LIMIT; }
    if (!g_x8f) {                                       /* a clean answer is remembered (4 entries, round robin) */
        i = g_gmn++ & 3;
        g_gm[i][0] = obj; g_gm[i][1] = gen; g_gm[i][2] = cur; g_gm[i][3] = t;
    }
    return t;
}

/* ---- the record writers ------------------------------------------------------ */
/* the 8-byte list header at XB+0: type aux len y0 y1 */
static void x8d_hdr(pdw type, pdw aux, pdw len, pdi y0, pdi y1)
{
    static pdb h[DLH];
    h[0] = (pdb)type; h[1] = (pdb)aux; h[2] = (pdb)len; h[3] = (pdb)(len >> 8);
    h[4] = (pdb)y0; h[5] = (pdb)((pdw)y0 >> 8); h[6] = (pdb)y1; h[7] = (pdb)((pdw)y1 >> 8);
    pd_fcopy_out(h, g_xb, 0, DLH);
}

static pdw x8d_state(pdw stroke, pdw tuple)
{
    static pdb b[4];
    pdb *t;
    if (tuple) {
        t = g_xt[tuple];
        b[0] = stroke ? t[1] : t[0]; b[1] = t[2]; b[2] = t[3];
    } else { b[0] = 255; b[1] = 0; b[2] = 0; }
    b[3] = 0;
    g_xnt = b[0] != 255 || b[1] || b[2];
    x8_odx = (pdw)(g_xnt | ((g_xnt || tuple) ? 2 : 0));
    if (g_xl[0] == b[0] && g_xl[1] == b[1] && g_xl[2] == b[2]) return 0;
    g_xl[0] = b[0]; g_xl[1] = b[1]; g_xl[2] = b[2];
    x8d_hdr(DLR_XSTATE, 0, DLH + 4, 0, 0);
    pd_fcopy_out(b, g_xb, DLH, 4);
    return DLH + 4;
}

/* a Form begins: a soft mask's /G, or a transparency group worth a layer */
static pdw x8d_group(void)
{
    static pdb b[4];
    static struct pdxb_x8grp q;
    pdw f, iso, ko, triv;
    pd_fcopy_in(g_xb, 0, &q, sizeof q);
    f = q.frame;
    if (f > 5) return 0;
    g_xfr[f].kind = 0;
    if (q.maskpend) {
        if (!g_xmaskpend) return 0;
        g_xmaskpend = 0;
        b[0] = (pdb)g_xpbc; b[1] = 0;
        g_mboff[g_xmaskid] = q.dllen;
        x8d_hdr(DLR_MASKB, g_xpkind, DLH + 2, 0, 0x7FFF);
        pd_fcopy_out(b, g_xb, DLH, 2);
        g_xfr[f].kind = 2; g_xfr[f].id = (pdb)g_xmaskid;
        x8d_trivial();                                   /* the block opens with its own STATE */
        x8_ocx = 3;
        return DLH + 2;
    }
    if (x8_dget(q.obj, q.gen, "Group", "S") || gt_q.kind != PDK_NAME || gt_q.text[0] != 'T') return 0;
    iso = x8_dget(q.obj, q.gen, "Group", "I") == 0 && (gt_q.num.hi || gt_q.num.lo);
    ko = x8_dget(q.obj, q.gen, "Group", "K") == 0 && (gt_q.num.hi || gt_q.num.lo);
    triv = !q.tuple || (g_xt[q.tuple][0] == 255 && !g_xt[q.tuple][2] && !g_xt[q.tuple][3]);
    if (triv && !iso && !ko) return 0;
    if (q.y1 < q.y0) return 0;
    if (q.tuple) { b[0] = g_xt[q.tuple][0]; b[1] = g_xt[q.tuple][2]; b[2] = g_xt[q.tuple][3]; }
    else { b[0] = 255; b[1] = 0; b[2] = 0; }
    b[3] = 0;
    x8d_hdr(DLR_GROUP, (pdw)(iso | (ko << 1)), DLH + 4, q.y0, q.y1);
    pd_fcopy_out(b, g_xb, DLH, 4);
    if (!triv) x8d_rows(q.y0, q.y1);
    g_xfr[f].kind = 1; g_xfr[f].y0 = q.y0; g_xfr[f].y1 = q.y1;
    x8d_trivial();                                       /* inside the group: opaque Normal, no mask */
    x8_ocx = 2;
    return DLH + 4;
}

/* a Form frame ends (before the state is restored): the group's END or the mask's MASKE */
static pdw x8d_gend(pdw f, pdw dllen)
{
    pdw k, id;
    if (f > 5) return 0;
    k = g_xfr[f].kind;
    g_xfr[f].kind = 0;
    if (k == 1) {
        x8d_hdr(DLR_GROUP, 0x80, DLH, g_xfr[f].y0, g_xfr[f].y1);
        x8d_trivial();
        return DLH;
    }
    if (k == 2) {
        id = g_xfr[f].id;
        x8d_hdr(DLR_MASKE, 0, DLH, 0, 0x7FFF);
        x8_ocx = (pdw)(g_mboff[id] + 2);                 /* the block's length: a normal walk jumps it */
        x8_odx = (pdw)(dllen + DLH - g_mboff[id]);
        x8d_trivial();
        return DLH;
    }
    return 0;
}

static pdw x8d_def(pdw op, pdw cx, pdw dx)
{
    pdw t;
    switch (op) {
    case X8D_STATE: return x8d_state(cx, dx);
    case X8D_MARK:  x8d_rows((pdi)cx, (pdi)dx); return 0;
    case X8D_GROUP: return x8d_group();
    case X8D_GEND:  return x8d_gend(cx, dx);
    case X8D_NOMASK:
        g_nmask--; g_xmaskpend = 0;
        t = x8d_tuple(g_xpca, g_xpCA, g_xpbm, 0);
        return t == 0xFFFF ? 0 : t;
    }
    x8_cf = 1;
    return PDE_RANGE;
}
