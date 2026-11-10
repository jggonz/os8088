/* ============================================================================
 * os8088 - apps/pagedesk/pdxgtr.c        PDXG: the ExtGState verb (transparency)
 *
 * PAGEDESK-SPEC 20.22.10.4. #included by PDXG's shell (gfx), which dispatches
 * 0xD0-0xD7 to gt_verb() and provides
 *
 *     pdw xg_dictget(struct pdxb_dictget *q);   0 or a PDE_ code
 *
 * over PDREND's service door: the raw PDENG answer, a final reference NOT
 * followed (the caller wants the /G object's number, not its value). On the
 * host (PD_HOST, blend's harness) pdrxb.c supplies it from the core's dict_get.
 *
 * Interpretation-time work only: the result is cached in the XC cache by the
 * caller, so a later pass needs no module. No recursion; every local is a
 * scalar or a static (SPEC.md 73).
 * ==========================================================================*/

static struct pdxb_dictget gt_q;
static struct pdxb_gs gt_g;

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
    return xg_dictget(&gt_q);
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
        if (xg_dictget(&gt_q)) return 0;
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

/* PDV_GS_PARSE: gt_g holds the request; fills the answer */
static pdw gt_parse(void)
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
    return PDE_OK;
}

/* the shell's dispatch: AL = verb, the XB holds the struct */
static pdw gt_verb(pdw verb)
{
    if (verb != PDV_GS_PARSE) return PDE_RANGE;
    return gt_parse();
}
