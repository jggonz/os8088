/* PDXF through the REAL PDENG (PAGEDESK-SPEC 20.22.8): the whole chain a font takes
 * on the machine - the exchange block, PDENG's stream stack (Flate included), the
 * CMap and /W parsers and the encoding rules - on a fixture PDF.
 *
 *   pdx_real <file.pdf> <nbytes> <code>...     prints, one per line:
 *     KIND k NG n UPEM u FLAGS f
 *     GID code gid           (code through the encoding context, or a CID for Type0)
 *     UNI code u             (the ToUnicode table, 0 = none)
 *     CIDMAP code cid        (the Encoding CMap's table, when the font has one)
 *     WID cid w              (/W, else /DW)
 *   cc -DPD_HOST -I apps/pagedesk -I . pdx_real.c pdmath.c pdeng.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hosttest/pde_host.c"
#include "../pdxf.c"

unsigned char pd_host_mem[1 << 20];
extern unsigned pde_fail, pde_dx;
extern unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
pdw pdx_eng_ax;
pdw pdx_eng(pdw verb, pdw bx, pdw cx, pdw dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    pdx_eng_ax = 0;
    if (pde_fail) return (pdw)pde_fail;
    pdx_eng_ax = (pdw)r;
    return 0;
}
pdi pdx_muldiv(pdi a, pdi b, pdi c)
{
    long long p = (long long)a * b, d = c, q;
    int neg = (p < 0) != (d < 0);
    if (p < 0) p = -p;
    if (d < 0) d = -d;
    q = (p + d / 2) / d;
    if (q > 32767) q = 32767;
    return (pdi)(neg ? -q : q);
}

#define SEG_XB 0x2000
#define SEG_WS 0x3000
#define SEG_OUT 0x6000
#define SEG_ECACHE 0x7000
#define SEG_EARENA 0x7800
#define SEG_ETBL   0x7C00
static pdw call(pdw verb) { return pdxf_verb(verb, SEG_XB, 0, 0); }

static struct pdxb_dictget dg;
static int dget(pdw obj, pdw gen, const char *a, const char *b, const char *c)
{
    memset(&dg, 0, sizeof dg); dg.objnum = obj; dg.gen = gen;
    strcpy(dg.key[0], a); if (b) strcpy(dg.key[1], b); if (c) strcpy(dg.key[2], c);
    pd_fcopy_out(&dg, SEG_XB, 0, sizeof dg);
    pde_verb(PDV_ENG_DICT_GET, 0, 0, 0);
    if (pde_fail) return -1;
    pd_fcopy_in(SEG_XB, 0, &dg, sizeof dg);
    return 0;
}
static pdw cm_lookup(pdw seg, pdw len, pdw code, pdw *found)
{
    pdw n = pd_fpeekw(seg, 2), lo = 0, hi = n, m, e;
    (void)len; *found = 0;
    while (lo < hi) {
        m = (lo + hi) / 2;
        if (pd_fpeekw(seg, 4 + m * 6) <= code) lo = m + 1; else hi = m;
    }
    if (!lo) return 0;
    e = 4 + (lo - 1) * 6;
    if (code <= pd_fpeekw(seg, e + 2)) { *found = 1; return (pdw)(pd_fpeekw(seg, e + 4) + (code - pd_fpeekw(seg, e))); }
    return 0;
}

int main(int argc, char **argv)
{
    struct pdxb_init in; struct pdxb_open op; struct pdxb_reslookup rl; struct pdxb_fload ld; struct pdxb_fenc en;
    struct pdxb_fgid gi; struct pdxb_fcmap cm; struct pdxb_fwid wi;
    pdw fobj, fgen, dobj = 0, dgen = 0, ffobj = 0, ffgen = 0, tuobj = 0, tugen = 0, encobj = 0, encgen = 0, cmobj = 0, cmgen = 0, is0, i, r, nb;
    int cid, symbolic = 0, n = 0;
    if (argc < 4) return 2;
    nb = (pdw)atoi(argv[2]);
    if (pdh_load(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_ECACHE; in.cache_kb = 16; in.arena_seg = SEG_EARENA; in.arena_kb = 8; in.tables_seg = SEG_ETBL; in.tables_kb = 11; in.flate_seg = 0x8000; in.flate_kb = 86; in.flags = PDXI_FLATE;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pde_verb(PDV_ENG_INIT, SEG_XB, 0, 0);
    memset(&op, 0, sizeof op); strcpy(op.name, "DOC.PDF");
    for (;;) {
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = pde_verb(PDV_ENG_OPEN, 0, 0, 0);
        pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        if (!pde_fail && r == PDE_BUSY) continue;
        if (pde_fail) { fprintf(stderr, "open failed %u\n", pde_fail); return 2; }
        break;
    }
    memset(&rl, 0, sizeof rl); rl.page = 0; strcpy(rl.category, "Font"); strcpy(rl.name, "F1");
    pd_fcopy_out(&rl, SEG_XB, 0, sizeof rl);
    pde_verb(PDV_ENG_RES_LOOKUP, 0, 0, 0);
    if (pde_fail) { fprintf(stderr, "no font F1\n"); return 2; }
    pd_fcopy_in(SEG_XB, 0, &rl, sizeof rl);
    fobj = rl.objnum; fgen = rl.gen;
    /* Type0: DescendantFonts [0] -> the CIDFont; else the font itself */
    is0 = 0;
    if (!dget(fobj, fgen, "Subtype", 0, 0) && !strcmp(dg.text, "Type0")) is0 = 1;
    if (is0) {
        if (dget(fobj, fgen, "DescendantFonts", "[0]", 0) || dg.kind != PDK_REF) { if (dg.kind != PDK_REF) {} }
        /* the raw reference of the descendant: the final value is NOT followed by PDENG */
        dobj = dg.refnum; dgen = dg.refgen;
        if (!dget(fobj, fgen, "Encoding", 0, 0) && dg.kind == PDK_REF) { cmobj = dg.refnum; cmgen = dg.refgen; }
        if (!dget(fobj, fgen, "ToUnicode", 0, 0) && dg.kind == PDK_REF) { tuobj = dg.refnum; tugen = dg.refgen; }
    } else {
        dobj = fobj; dgen = fgen;
        if (!dget(fobj, fgen, "ToUnicode", 0, 0) && dg.kind == PDK_REF) { tuobj = dg.refnum; tugen = dg.refgen; }
    }
    (void)encobj; (void)encgen;
    if (!dget(dobj, dgen, "FontDescriptor", "FontFile2", 0) && dg.kind == PDK_REF) { ffobj = dg.refnum; ffgen = dg.refgen; }
    else if (!dget(dobj, dgen, "FontDescriptor", "FontFile3", 0) && dg.kind == PDK_REF) { ffobj = dg.refnum; ffgen = dg.refgen; }
    else if (!dget(dobj, dgen, "FontDescriptor", "FontFile", 0) && dg.kind == PDK_REF) { ffobj = dg.refnum; ffgen = dg.refgen; }
    if (!dget(dobj, dgen, "FontDescriptor", "Flags", 0)) symbolic = (pdm_fx_round(&dg.num) & 4) != 0;
    /* the PDXF side */
    memset(&in, 0, sizeof in); in.xb_seg = SEG_XB; in.slot_seg = SEG_WS; in.slot_kb = PDXF_WS_KB;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    r = pdxf_verb(PDV_X_INIT, SEG_XB, 0, 0);
    if (r) { printf("INIT %u\n", r); return 1; }
    memset(&ld, 0, sizeof ld); ld.objnum = ffobj; ld.gen = ffgen;
    if (!dget(dobj, dgen, "CIDToGIDMap", 0, 0) && dg.kind == PDK_REF) { ld.cidmap_obj = dg.refnum; ld.cidmap_gen = dg.refgen; }
    pd_fcopy_out(&ld, SEG_XB, 0, sizeof ld);
    r = call(PDV_FNT_LOAD);
    pd_fcopy_in(SEG_XB, 0, &ld, sizeof ld);
    printf("KIND %u NG %u UPEM %u FLAGS %u RC %u TOP %u BOT %u\n", ld.kind, ld.nglyphs, ld.upem, ld.flags, r, ld.bbox_top, ld.bbox_bot);
    if (r) return 1;
    if (!is0) {                                       /* the encoding context: base = WinAnsi, which is the identity on 32..255 here */
        memset(&en, 0, sizeof en);
        en.flags = PDFE_FIRST | PDFE_HASENC | (symbolic ? PDFE_SYMBOLIC : 0);
        for (i = 0; i < 224; i++) en.lat[i] = (pdb)(i + 32);
        for (i = 0x80; i < 0xA0; i++) en.lat[i - 32] = (pdb)i;
        en.ndiff = 0;
        if (!dget(fobj, fgen, "Encoding", "Differences", 0) && dg.kind == PDK_ARR) {      /* [127 /flexdemo] */
            pdw k, code = 0, nd = dg.count;
            for (k = 0; k < nd && en.ndiff < 12; k++) {
                char key[8]; sprintf(key, "[%u]", k);
                if (dget(fobj, fgen, "Encoding", "Differences", key)) break;
                if (dg.kind == PDK_INT) code = (pdw)pdm_fx_round(&dg.num);
                else if (dg.kind == PDK_NAME) { en.diff[en.ndiff].code = code++; strncpy(en.diff[en.ndiff].name, dg.text, 21); en.ndiff++; }
            }
        }
        pd_fcopy_out(&en, SEG_XB, 0, sizeof en);
        call(PDV_FNT_ENC);
    }
    for (i = 3; (int)i < argc; i++) {
        cid = atoi(argv[i]);
        memset(&gi, 0, sizeof gi); gi.code = (pdw)cid; gi.kind = is0 ? 1 : 0;
        pd_fcopy_out(&gi, SEG_XB, 0, sizeof gi);
        r = call(PDV_FNT_GID);
        pd_fcopy_in(SEG_XB, 0, &gi, sizeof gi);
        printf("GID %d %u\n", cid, r ? 0xFFFF : gi.gid);
        n++;
    }
    if (tuobj) {
        memset(&cm, 0, sizeof cm); cm.objnum = tuobj; cm.gen = tugen; cm.kind = PDFC_UNI; cm.dseg = SEG_OUT; cm.doff = 0; cm.cap = 4096;
        pd_fcopy_out(&cm, SEG_XB, 0, sizeof cm);
        r = call(PDV_FNT_CMAP);
        pd_fcopy_in(SEG_XB, 0, &cm, sizeof cm);
        printf("TOUNICODE rc %u len %u flags %u\n", r, cm.len, cm.flags);
        for (i = 3; (int)i < argc; i++) { pdw f, v = cm_lookup(SEG_OUT, cm.len, (pdw)atoi(argv[i]), &f); printf("UNI %s %u\n", argv[i], f ? v : 0); }
    }
    if (cmobj) {
        memset(&cm, 0, sizeof cm); cm.objnum = cmobj; cm.gen = cmgen; cm.kind = PDFC_ENC; cm.dseg = SEG_OUT; cm.doff = 0; cm.cap = 4096;
        pd_fcopy_out(&cm, SEG_XB, 0, sizeof cm);
        r = call(PDV_FNT_CMAP);
        pd_fcopy_in(SEG_XB, 0, &cm, sizeof cm);
        printf("ENCCMAP rc %u len %u flags %u\n", r, cm.len, cm.flags);
        for (i = 3; (int)i < argc; i++) { pdw f, v = cm_lookup(SEG_OUT, cm.len, (pdw)atoi(argv[i]), &f); printf("CIDMAP %s %u\n", argv[i], f ? v : 0xFFFF); }
    }
    if (is0) {
        memset(&wi, 0, sizeof wi); wi.objnum = dobj; wi.gen = dgen; wi.dseg = SEG_OUT; wi.doff = 8192; wi.cap = 4096;
        pd_fcopy_out(&wi, SEG_XB, 0, sizeof wi);
        r = call(PDV_FNT_WIDTHS);
        pd_fcopy_in(SEG_XB, 0, &wi, sizeof wi);
        printf("WIDTHS rc %u dw %u len %u more %u\n", r, wi.dw, wi.len, wi.more);
        for (i = 3; (int)i < argc; i++) {
            pdw k, c = (pdw)atoi(argv[i]), w = wi.dw;
            for (k = 0; k < wi.len / 6; k++)
                if (c >= pd_fpeekw(SEG_OUT, 8192 + k * 6) && c <= pd_fpeekw(SEG_OUT, 8192 + k * 6 + 2)) w = pd_fpeekw(SEG_OUT, 8192 + k * 6 + 4);
            printf("WID %s %u\n", argv[i], w);
        }
    }
    (void)n; (void)nb;
    return 0;
}
