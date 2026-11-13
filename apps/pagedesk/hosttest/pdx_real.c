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
    struct pdxb_init in; struct pdxb_open op; struct pdxb_reslookup rl;
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
    memset(&rl, 0, sizeof rl); rl.page = getenv("PDX_PAGE") ? (pdw)atoi(getenv("PDX_PAGE")) : 0; strcpy(rl.category, "Font");
    strcpy(rl.name, getenv("PDX_FONT") ? getenv("PDX_FONT") : "F1");     /* another font of a real file (QA R13) */
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
    /* the PDXF side: one GLYPH call (info only) per code does everything from the font dictionary */
    memset(&in, 0, sizeof in); in.xb_seg = SEG_XB; in.slot_seg = SEG_WS; in.slot_kb = PDXF_WS_KB; in.glyph_seg = SEG_OUT; in.glyph_kb = 40;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    r = pdxf_verb(PDV_X_INIT, SEG_XB, 0, 0);
    if (r) { printf("INIT %u\n", r); return 1; }
    (void)ffobj; (void)ffgen; (void)tuobj; (void)tugen; (void)cmobj; (void)cmgen; (void)symbolic; (void)is0; (void)dobj; (void)dgen;
    for (i = 3; (int)i < argc; i++) {
        struct pdxb_fglyph gl;
        memset(&gl, 0, sizeof gl); gl.objnum = fobj; gl.gen = fgen; gl.code = (pdw)atoi(argv[i]); gl.flags = PDFG_INFO;
        pd_fcopy_out(&gl, SEG_XB, 0, sizeof gl);
        r = call(PDV_FNT_GLYPH);
        pd_fcopy_in(SEG_XB, 0, &gl, sizeof gl);
        if (i == 3) printf("KIND %u RC %u ERR %u FLAGS %u\n", gl.kind, r, gl.err, gl.oflags);
        if (r) { printf("GID %s 65535\n", argv[i]); continue; }
        printf("GID %s %u\n", argv[i], gl.gid);
        printf("UNI %s %u\n", argv[i], gl.oflags & PDFG_LATIN ? gl.lat : 0);
        printf("WID %u %u\n", gl.cid, gl.adv1000);
        printf("CIDMAP %s %u\n", argv[i], gl.cid);
    }
    if (getenv("PDX_PX")) {                           /* the strike block: slots, info and bitmaps */
        pdw px = (pdw)atoi(getenv("PDX_PX")), hdr = (pdw)(40u * 1024 - 4 * PDFB_SIZE - PDFB_HDR), blk = 0, k;   /* INIT's 40 KiB claim: (40 - 24) / 4 = 4 blocks */
        pd_ffill(SEG_OUT, hdr, 0, PDFB_HDR + PDFB_SIZE * 4);
        for (i = 3; (int)i < argc; i++) {
            struct pdxb_fglyph gl;
            memset(&gl, 0, sizeof gl); gl.objnum = fobj; gl.gen = fgen; gl.code = (pdw)atoi(argv[i]); gl.flags = PDFG_BLOCK;
            gl.em64 = (pdw)(px * 64); gl.dseg = SEG_OUT; gl.doff = hdr; gl.cap = 40; gl.pass = 1;   /* the claim INIT named (segment, KiB) */
            pd_fcopy_out(&gl, SEG_XB, 0, sizeof gl);
            r = call(PDV_FNT_GLYPH);
            pd_fcopy_in(SEG_XB, 0, &gl, sizeof gl);
            if (r || !gl.slot) { printf("BLK %s none\n", argv[i]); continue; }
            if (gl.doff != hdr) { printf("BLK %s header at %u, not %u\n", argv[i], gl.doff, hdr); continue; }
            blk = hdr + PDFB_HDR + gl.blk * PDFB_SIZE;
            {
                pdw rows = px + (px >> 2) + 1;
                pdw rp = blk + (gl.slot - 1) * 4, w = pd_fpeekb(SEG_OUT, rp), bo = pd_fpeekw(SEG_OUT, rp + 2), nb = ((w + 7) / 8) * rows;
                pdw ip = blk + PDFB_INFO + (gl.slot - 1) * 4;
                printf("BLK %s slot %u w %u lb %d adv %u lat %u fl %u key %u ", argv[i], gl.slot, w, (int)(signed char)pd_fpeekb(SEG_OUT, rp + 1),
                       pd_fpeekw(SEG_OUT, ip), pd_fpeekb(SEG_OUT, ip + 2), pd_fpeekb(SEG_OUT, ip + 3), pd_fpeekw(SEG_OUT, blk + PDFB_KEYS + (gl.slot - 1) * 2));
                for (k = 0; k < nb && w; k++) printf("%02x", pd_fpeekb(SEG_OUT, blk + 512 + bo + k));
                printf("\n");
            }
        }
    }
    (void)n; (void)nb;
    return 0;
}
