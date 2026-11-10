/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdr_test.c       PDREND host harness driver
 *
 *   pdr_test render <pdf> <page> <zoom> <rot> <bpp> <asp_n/asp_d> <W> <H> <ox> <oy> <out.ppm> <bank> [dlkb]
 *   pdr_test text   <pdf> <page> <bank>
 *   pdr_test thumb  <pdf> <page> <w> <h> <out.pbm> <bank>
 * PDR_SEQ="FILE:PAGE[:R|:Fn] ..." makes `render` a SESSION (verify-perf): one
 * process, the pages in turn, writing <out>.<k>.ppm and a status line each.
 * A new FILE is the primary's open (both modules INIT); the same FILE again
 * keeps PDREND's per-document caches; :R is the primary's re-open of the same
 * document after a save (PDENG INIT + OPEN, no PDREND INIT); :Fn (the real
 * engine only) fails the nth read from there and every read after it until
 * that render ends - a disk taken out - and the session goes on.
 * Exits non-zero on a harness-level failure; the render checks are made by
 * the python wrapper (hosttest/pdr_check.py) over the files written.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdrend.h"
#include "pdr_host.h"

extern unsigned char pd_host_mem[];
extern pdw pd_host_ds;
extern unsigned char pdrh_fb[];
extern int pdrh_fbw, pdrh_fbh, pdrh_blits;
extern int pdrh_open(const char *path);
void pdb_dump_write(const char *path, int w, int h);
pdw pdr_verb(pdw verb, pdw bx, pdw cx, pdw dx);
/* every verb is called through here: PDREND must hand the working folder back
 * where it found it (the document's), and no read by name may have been made
 * from the wrong folder - the shell only stands back between wakes */
static pdw vcall(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r = pdr_verb(verb, bx, cx, dx);
    /* PDR_SHELLBACK=1 does what the shell does between wakes (stands back in the
     * document's folder) instead of failing, so only a read by name from the wrong
     * folder INSIDE one verb is caught - the negative control for that case */
    if (!pdrh_in_doc() && getenv("PDR_SHELLBACK")) { pdrh_here_clus = PDRH_DOC_CLUS; pdrh_here_vol = PDRH_DOC_VOL; }
    if (!pdrh_in_doc()) { fprintf(stderr, "FOLDER: verb 0x%x left the working folder at %u:%d\n", verb, pdrh_here_clus, pdrh_here_vol); exit(6); }
    if (pdrh_wrongdir) { fprintf(stderr, "FOLDER: %ld read(s) by name from the wrong folder (verb 0x%x)\n", pdrh_wrongdir, verb); exit(6); }
    return r;
}

#define SEG_XB   0x2000
#define SEG_DL   0x3000
#define SEG_BAND 0x4000
#define SEG_GLY  0x5000
#define SEG_BANK 0x6000
#define SEG_TXT  0x4800
#define SEG_CELL 0x4C00
#define SEG_X8   0xB000          /* the 8-bit band, layers and mask (PDMEM_X8) */
#define SEG_EDIT 0x9000          /* the edit store (PAGEDESK-SPEC 13.1.1) when PDR_EDITS names a file */
#define MEM(seg, off) (pd_host_mem + (((unsigned long)(seg) << 4) + (off)))
pdw pdrh_xbseg(void) { return SEG_XB; }

static const unsigned char vga[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255} };

extern const char *pdrh_fdir;
extern int pdrh_cluster_sectors;
extern long pdrh_reads, pdrh_readbytes;
static int g_glykb = 16;
/* "bank" is now a DIRECTORY holding PDRES.BIN, PAGEDESK.O88 (a pad, then PDRES.BIN:
 * the part offset is not zero) and optionally PDFONTX.RES - what the shell lays out.
 * The INDEX (directory kind 7's length) is read into the claim the primary would
 * hand over, and PDV_REND_FONTBANK is sent exactly as D3 says. */
static int load_bank(const char *dir)
{
    char path[600]; FILE *f; unsigned char hdr[200]; long n; unsigned idxlen; struct pdxb_fontbank fb;
    pdrh_fdir = dir;
    snprintf(path, sizeof path, "%s/PDRES.BIN", dir);
    f = fopen(path, "rb"); if (!f) return 0;
    fread(hdr, 1, 200, f);
    { int i; idxlen = 0; for (i = 0; i < 7; i++) if (hdr[8 + i * 12] == 7) idxlen = hdr[8 + i * 12 + 8] | (hdr[8 + i * 12 + 9] << 8); }
    fseek(f, 0, SEEK_SET); n = (long)fread(MEM(SEG_BANK, 0), 1, idxlen, f); fclose(f);
    memset(&fb, 0, sizeof fb);
    strcpy(fb.pkgname, "PAGEDESK.O88"); fb.place_clus = PDRH_PKG_CLUS; fb.place_vol = PDRH_PKG_VOL;
    fb.part_off.lo = 1536; fb.part_off.hi = 0;
    fb.index_seg = SEG_BANK; fb.index_len = (pdw)idxlen;
    memcpy(MEM(SEG_XB, 0), &fb, sizeof fb);
    return n == (long)idxlen ? 1 : 0;
}
static int send_fontbank(void)
{
    return (int)vcall(PDV_REND_FONTBANK, 0, 0, 0);
}
/* ---- the primary's side of the ladder (PAGEDESK-SPEC 20.22.2, 20.22.7) ----
 * PDE_NEEDMOD: "load" the stand-in occupant (hosttest/pdr_xmod.c) unless
 * PDR_NOSLOT lists its id ("all" = every one), and tell PDREND with
 * PDV_REND_SLOT. PDE_NEEDMEM for PDMEM_XC: claim it (PDR_XCKB overrides the
 * size; PDR_NOXC refuses) and re-INIT with it, as the primary does. Either
 * way the verb is called again. */
#define SEG_XC   0xA000
#define SEG_GLY2 0xC000             /* where PDR_GLYPHGROW (fonts, 20.22.8) and PDR_GLYMOVE (20.22.7.11) move the glyph claim */
static unsigned g_glyold;           /* the moved claim's old size: its old place must stay 0xCD */
extern pdw pdrh_slot, pdr_out_cx, pdr_out_dx;
static struct pdxb_init g_lastinit;
static int g_loads, g_memasks;
static unsigned g_clsize[5];        /* the claims' sizes, for the canaries below */
static int ladder(pdw e)
{
    static unsigned char xb[1024];
    const char *no = getenv("PDR_NOSLOT");
    if (e == PDE_NEEDMOD) {
        pdw id = pdr_out_dx;
        g_loads++;
        if (g_loads > 64) { fprintf(stderr, "LADDER: more than 64 module answers\n"); exit(9); }
        /* PDR_XCDROP: the primary's release-before-refuse (plat's pdu_slot_load):
         * the slot's claim is short, so XC is released and PDREND re-INITed with
         * xc_seg 0 under this NEEDMOD answer, mid-view (20.22.7.11) */
        /* PDR_GLYMOVE: PDXF's GLYPH grant (plat): the glyph claim moves to a new,
         * bigger region under this answer, once, mid-view (20.22.7.11) */
        if (getenv("PDR_GLYMOVE") && g_lastinit.glyph_seg == SEG_GLY) {
            memcpy(xb, MEM(SEG_XB, 0), sizeof xb);
            g_lastinit.glyph_seg = SEG_GLY2; g_lastinit.glyph_kb = 40;
            memset(MEM(SEG_GLY2, 0), 0xA5, 40u << 10);   /* a new region holds anything */
            memset(MEM(SEG_GLY, 0), 0xCD, 8192);         /* the old one is gone: a write there is a canary */
            g_clsize[3] = 0;
            memcpy(MEM(SEG_XB, 0), &g_lastinit, sizeof g_lastinit);
            if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "re-init failed\n"); exit(2); }
            memcpy(MEM(SEG_XB, 0), xb, sizeof xb);
        }
        if (getenv("PDR_XCDROP") && g_lastinit.xc_seg) {
            memcpy(xb, MEM(SEG_XB, 0), sizeof xb);
            g_lastinit.xc_seg = 0; g_lastinit.xc_kb = 0;
            g_lastinit.flate_kb = 43; g_lastinit.cache_kb += 1;   /* PDENG's words move with it (plat) */
            g_clsize[4] = 0;                            /* no claim: the canary from byte 0 */
            memset(MEM(SEG_XC, 0), 0xCD, 8192);         /* = CANARY, below */
            memcpy(MEM(SEG_XB, 0), &g_lastinit, sizeof g_lastinit);
            if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "re-init failed\n"); exit(2); }
            memcpy(MEM(SEG_XB, 0), xb, sizeof xb);
        }
        if (!no || (strcmp(no, "all") && !strchr(no, '0' + (int)id))) { pdrh_slot = id; vcall(PDV_REND_SLOT, id, 0x0008, 0x9800); }
        return 1;
    }
    if (e == PDE_NEEDMEM && pdr_out_cx == PDMEM_X8) {      /* blend: the region (PDX8 is linked in) */
        memcpy(xb, MEM(SEG_XB, 0), sizeof xb);
        g_memasks++;
        g_lastinit.x8_seg = SEG_X8; g_lastinit.x8_kb = pdr_out_dx > 63 ? 63 : pdr_out_dx;
        memcpy(MEM(SEG_XB, 0), &g_lastinit, sizeof g_lastinit);
        if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "re-init failed\n"); exit(2); }
        memcpy(MEM(SEG_XB, 0), xb, sizeof xb);
        return 1;
    }
    if (e == PDE_NEEDMEM) {
        g_memasks++;
        if (g_memasks > 64) { fprintf(stderr, "LADDER: more than 64 memory answers\n"); exit(9); }
        if (pdr_out_cx == PDMEM_XC && !getenv("PDR_NOXC")) {
            memcpy(xb, MEM(SEG_XB, 0), sizeof xb);
            g_lastinit.xc_seg = SEG_XC;
            g_lastinit.xc_kb = getenv("PDR_XCKB") ? (pdw)atoi(getenv("PDR_XCKB")) : pdr_out_dx;
            if (g_lastinit.xc_kb > 63) g_lastinit.xc_kb = 63;
            g_clsize[4] = (unsigned)g_lastinit.xc_kb << 10;
            memset(MEM(SEG_XC, g_clsize[4]), 0xCD, 0x10000u - g_clsize[4] < 8192 ? 0x10000u - g_clsize[4] : 8192);
            memcpy(MEM(SEG_XB, 0), &g_lastinit, sizeof g_lastinit);
            if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "re-init failed\n"); exit(2); }
            memcpy(MEM(SEG_XB, 0), xb, sizeof xb);
        }
        /* fonts (20.22.8): PDR_GLYPHGROW regrows the glyph claim to what PDREND asked AND MOVES it, as
         * OSAPI_MEM_REGROW may; the old place is filled with 0xCD and checked at the end (canary_check) */
        if (pdr_out_cx == PDMEM_GLYPH && getenv("PDR_GLYPHGROW") && g_lastinit.glyph_seg == SEG_GLY) {
            unsigned old = (unsigned)g_lastinit.glyph_kb << 10;
            memcpy(xb, MEM(SEG_XB, 0), sizeof xb);
            memcpy(MEM(SEG_GLY2, 0), MEM(SEG_GLY, 0), old);
            memset(MEM(SEG_GLY, 0), 0xCD, old); g_glyold = old;
            g_lastinit.glyph_seg = SEG_GLY2; g_lastinit.glyph_kb = pdr_out_dx > 63 ? 63 : pdr_out_dx;
            memcpy(MEM(SEG_XB, 0), &g_lastinit, sizeof g_lastinit);
            if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "re-init failed\n"); exit(2); }
            memcpy(MEM(SEG_XB, 0), xb, sizeof xb);
        }
        return 1;
    }
    return 0;
}
static void do_init(int bpp, int bankkb, int dlkb)
{
    struct pdxb_init in; static unsigned char keep[sizeof(struct pdxb_fontbank)];
    memcpy(keep, MEM(SEG_XB, 0), sizeof keep);
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.dl_seg = SEG_DL; in.dl_kb = (pdw)dlkb;
    in.eng_seg = 0xF000;    /* never called on the host (pdr_host.c twins PDENG), but nonzero as on the
                             * machine: verb_init reads 0 as "never INITed" and a NEEDMEM's re-INIT would
                             * otherwise start a new document instead of keeping the page view */
    in.band_seg = SEG_BAND; in.band_bytes = 9600;
    in.glyph_seg = SEG_GLY; in.glyph_kb = (pdw)g_glykb;
    in.bank_seg = 0; in.bank_kb = 0; (void)bankkb;
    in.bpp = (pdw)bpp; in.adapter = 0;
    /* the engine's vector, nonzero as the primary's is: verb_init takes a 0 one
     * for the FIRST INIT, so without it a re-INIT after PDE_NEEDMEM restarted the
     * view instead of adding the region (the host's pdr_eng never reads it) */
    in.eng_off = 0x0008; in.eng_seg = 0x9000;
#ifdef PDR_X8
    if (!getenv("PDB_NOX8")) { in.x8_seg = SEG_X8; in.x8_kb = 60; }          /* blend (20.22.10.2) */
#endif
    if (getenv("PDR_XCKB0")) { in.xc_seg = SEG_XC; in.xc_kb = (pdw)atoi(getenv("PDR_XCKB0")); }   /* the claim from the start */
    g_lastinit = in;
    memcpy(MEM(SEG_XB, 0), &in, sizeof in);
    if (vcall(PDV_REND_INIT, SEG_XB, 0, 0)) { fprintf(stderr, "init failed\n"); exit(2); }
    memcpy(MEM(SEG_XB, 0), keep, sizeof keep);
    if (send_fontbank()) { fprintf(stderr, "fontbank failed\n"); exit(2); }
}
/* the claims are laid out with 8 KiB of 0xCD behind each (every arena is at least that far from the
 * next); a renderer that writes past its claim shows up here - 8 KiB, not a few bytes, because the
 * workspace's regions start up to 6 KiB in, so an overrun need not touch a claim's first bytes */
#define CANARY 8192
static const struct { pdw seg; unsigned size; const char *name; } g_cl[5] = {
    { SEG_XB, 1024, "exchange block" }, { SEG_DL, 0, "display list" }, { SEG_BAND, 9600, "band" }, { SEG_GLY, 0, "glyph claim" },
    { SEG_XC, 0, "extension cache" } };
static unsigned g_clsize[5];
static unsigned canary_len(int i) { unsigned room = 0x10000u - g_clsize[i]; return room < CANARY ? room : CANARY; }
static void canary_set(int dlkb)
{
    int i;
    g_clsize[0] = 1024; g_clsize[1] = dlkb >= 63 ? 0xFC00u : (unsigned)dlkb << 10; g_clsize[2] = 9600; g_clsize[3] = (unsigned)g_glykb << 10;
    g_clsize[4] = g_lastinit.xc_seg ? (unsigned)g_lastinit.xc_kb << 10 : 0xFC00u;   /* ladder() moves it to a grant */
    for (i = 0; i < 5; i++) memset(MEM(g_cl[i].seg, g_clsize[i]), 0xCD, canary_len(i));
}
static int canary_check(void)
{
    int i, j, bad = 0;
    for (i = 0; i < 5; i++) for (j = 0; j < canary_len(i); j++) if (MEM(g_cl[i].seg, g_clsize[i])[j] != 0xCD) { fprintf(stderr, "CANARY: %s overrun at +%d\n", g_cl[i].name, j); bad = 1; break; }
    for (j = 0; j < (int)g_glyold; j++) if (MEM(SEG_GLY, 0)[j] != 0xCD) { fprintf(stderr, "CANARY: the moved glyph claim's old place written at +%d\n", j); bad = 1; break; }
    return bad;
}
static void write_ppm(const char *path, int x0, int y0, int w, int h)
{
    FILE *f = fopen(path, "wb"); int x, y;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) fwrite(vga[pdrh_fb[(y0 + y) * pdrh_fbw + x0 + x] & 15], 1, 3, f);
    fclose(f);
}
static void report(void)
{
    struct pdxb_rstatus rs;
    vcall(PDV_REND_STATUS, 0, 0, 0);
    memcpy(&rs, MEM(SEG_XB, 0), sizeof rs);
    printf("status incomplete=0x%04x first='%s' ops=%u passes=%u blits=%d mods=0x%x swaps=%u loads=%d\n", rs.incomplete, rs.first, rs.ops, rs.passes, pdrh_blits,
           rs.mods, rs.swaps, g_loads);
    if (getenv("PDR_XSHADE")) { extern long pdrh_xshades, pdrh_xstepmax; printf("xshade calls=%ld stepmax=%ld\n", pdrh_xshades, pdrh_xstepmax); }
}

/* one page's text map to `o`. PDR_TMSTALE: a slice the ladder answered is sent again with the
 * block as it was sent, as the primary's Find and selection passes do (QA R17) */
static int text_run(int page, FILE *o)
{
    int guard = 0, total = 0, i; struct pdxb_textmap tq; pdw e;
    memset(&tq, 0, sizeof tq); tq.page = (pdw)page; tq.dseg = SEG_TXT; tq.doff = 0; tq.dmax = 1200;
    for (;;) {
        struct pdtm_glyph *g; int n;
        memcpy(MEM(SEG_XB, 0), &tq, sizeof tq);
        e = vcall(PDV_REND_TEXTMAP, 0, 0, 0);
        if (ladder(e)) { if (!getenv("PDR_TMSTALE")) memcpy(&tq, MEM(SEG_XB, 0), sizeof tq); continue; }
        if (e) { fprintf(stderr, "textmap -> %u\n", e); return 3; }
        memcpy(&tq, MEM(SEG_XB, 0), sizeof tq);
        g = (struct pdtm_glyph *)MEM(SEG_TXT, 0);
        n = tq.count;
        for (i = 0; i < n; i++) {
            if ((g[i].flags & 1) && total + i) fputc('\n', o);
            fputc(g[i].code, o);
        }
        total += n;
        if (tq.done || ++guard > 100000) break;
    }
    fputc('\n', o);
    return 0;
}

int pdrh_noshcull, pdrh_dlhist;      /* PDR_NOSHCULL: every shading asked of PDXG, 32 bands (pdrxg.c xg_steps' A/B); PDR_DLHIST: the list per pass */
int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    pdrh_noshcull = getenv("PDR_NOSHCULL") != 0;
    pdrh_dlhist = getenv("PDR_DLHIST") ? atoi(getenv("PDR_DLHIST")) : 0;
    pd_host_ds = 0x1000;
    if (getenv("PDR_CLUSTER")) pdrh_cluster_sectors = atoi(getenv("PDR_CLUSTER"));
    if (getenv("PDR_GLYPHKB")) g_glykb = atoi(getenv("PDR_GLYPHKB"));
    if (!strcmp(argv[1], "render") && argc >= 14) {
        int page = atoi(argv[3]), zoom = atoi(argv[4]), rot = atoi(argv[5]), bpp = atoi(argv[6]);
        int an = 1, ad = 1, W = atoi(argv[8]), H = atoi(argv[9]), ox = atoi(argv[10]), oy = atoi(argv[11]);
        int dlkb = argc > 14 ? atoi(argv[14]) : 29, kb, steps = 0, np;
        struct pdxb_render rq; struct pdxb_pageinfo pi; pdw e; long esz = 0; static unsigned char ekeep[65536];
        static char seqbuf[8192], curf[600], outbase[600], outp[700];
        char *seqp = 0;
        int seqk = 0;
        sscanf(argv[7], "%d/%d", &an, &ad);
        if (getenv("PDR_SEQ")) {
            strncpy(seqbuf, getenv("PDR_SEQ"), sizeof seqbuf - 1); seqp = seqbuf;
            strncpy(outbase, argv[12], sizeof outbase - 1);
        }
    seq_next:
        if (seqp) {
            char *ent, *c, *mode;
            while (*seqp == ' ') seqp++;
            if (!*seqp) return 0;
            ent = seqp; while (*seqp && *seqp != ' ') seqp++;
            if (*seqp) *seqp++ = 0;
            c = strrchr(ent, ':'); mode = 0;
            if (c && (c[1] == 'R' || c[1] == 'F')) { mode = c; *c = 0; c = strrchr(ent, ':'); }
            if (!c) { fprintf(stderr, "PDR_SEQ: %s is not FILE:PAGE\n", ent); return 1; }
            *c = 0; page = atoi(c + 1);
            if (strcmp(curf, ent) || (mode && mode[1] == 'R')) {
                np = pdrh_open(ent);
                if (np <= 0) { fprintf(stderr, "cannot open %s\n", ent); return 2; }
                if (!mode || mode[1] != 'R') { kb = load_bank(argv[13]); do_init(bpp, kb, dlkb); }
                strncpy(curf, ent, sizeof curf - 1);
            }
#ifdef PDR_REAL_ENG
            { extern unsigned long pdh_reads, pdh_failat, pdh_failto;
              pdh_failat = mode && mode[1] == 'F' ? pdh_reads + (unsigned long)atol(mode + 2) : 0;
              pdh_failto = pdh_failat ? (unsigned long)-1 : 0; }
#endif
            snprintf(outp, sizeof outp, "%s.%d.ppm", outbase, seqk++);
            argv[12] = outp;
            pdrh_reads = 0; pdrh_readbytes = 0; pdrh_blits = 0;
        } else {
            np = pdrh_open(argv[2]);
            if (np <= 0) { fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
            kb = load_bank(argv[13]);
            do_init(bpp, kb, dlkb);
        }
        steps = 0;
        canary_set(dlkb);
        memset(pdrh_fb, 0, 1024 * 1024);
        /* the engine's page info -> the effective box */
        memset(MEM(SEG_XB, 0), 0, sizeof pi);
        if (pdr_eng(PDV_ENG_PAGEINFO, (pdw)page, 0, 0)) { fprintf(stderr, "no page %d\n", page); return 2; }
        memcpy(&pi, MEM(SEG_XB, 0), sizeof pi);
        memset(&rq, 0, sizeof rq);
        rq.view.box.x0 = pi.media.x0 > pi.crop.x0 ? pi.media.x0 : pi.crop.x0;
        rq.view.box.y0 = pi.media.y0 > pi.crop.y0 ? pi.media.y0 : pi.crop.y0;
        rq.view.box.x1 = pi.media.x1 < pi.crop.x1 ? pi.media.x1 : pi.crop.x1;
        rq.view.box.y1 = pi.media.y1 < pi.crop.y1 ? pi.media.y1 : pi.crop.y1;
        rq.view.rot = (pdw)((pi.rotate + rot) % 360);
        rq.view.zoom = (pdw)zoom; rq.view.asp_num = (pdw)an; rq.view.asp_den = (pdw)ad;
        rq.view.ox = (pd_px)ox; rq.view.oy = (pd_px)oy;
        rq.view.clip_x0 = 0; rq.view.clip_y0 = 0; rq.view.clip_x1 = (pd_px)(W - 1); rq.view.clip_y1 = (pd_px)(H - 1);
        rq.page = (pdw)page; rq.win_off = 0x1234;
        /* wave 2 (PAGEDESK-SPEC 20.12): PDR_RFLAGS = PDRF_* , PDR_EDITS = a store file (records at 0, then the
         * heaps), PDR_ECOUNT its record count (default: the file's records), PDR_HIDE, PDR_MONO = mono_mark */
        if (getenv("PDR_RFLAGS")) rq.rflags = (pdw)atoi(getenv("PDR_RFLAGS"));
        if (getenv("PDR_HIDE")) rq.hide = (pdw)atoi(getenv("PDR_HIDE"));
        if (getenv("PDR_MONO")) rq.mono_mark = (pdw)atoi(getenv("PDR_MONO"));
        if (getenv("PDR_EDITS")) {
            FILE *ef = fopen(getenv("PDR_EDITS"), "rb");
            if (!ef) { fprintf(stderr, "cannot open the store %s\n", getenv("PDR_EDITS")); return 2; }
            memset(MEM(SEG_EDIT, 0), 0, 65536);
            esz = (long)fread(MEM(SEG_EDIT, 0), 1, 65536, ef); fclose(ef);
            memcpy(ekeep, MEM(SEG_EDIT, 0), 65536);
            rq.edits_seg = SEG_EDIT;
            rq.edits_count = getenv("PDR_ECOUNT") ? (pdw)atoi(getenv("PDR_ECOUNT")) : (pdw)(esz / 48 > 255 ? 255 : esz / 48);
        }
        memcpy(MEM(SEG_XB, 0), &rq, sizeof rq);
        e = vcall(PDV_REND_BEGIN, 0, 0, 0);
        if (e && seqp) { printf("status begin %u\n", e); goto seq_next; }
        if (e) { fprintf(stderr, "begin -> %u\n", e); return 3; }
        do { e = vcall(PDV_REND_STEP, 0, 0, 0); steps++; if (getenv("PDB_TRACE")) fprintf(stderr, "step e=%u cx=%u dx=%u\n", e, pdr_out_cx, pdr_out_dx); if (ladder(e)) e = PDE_BUSY; } while (e == PDE_BUSY && steps < 100000);
        if (e && seqp) { vcall(PDV_REND_ABORT, 0, 0, 0); printf("status step %u\n", e); goto seq_next; }
        if (e) { fprintf(stderr, "step -> %u\n", e); return 3; }
        printf("steps=%d reads=%ld readbytes=%ld\n", steps, pdrh_reads, pdrh_readbytes);
        report();
        if (canary_check()) return 5;
        if (esz && memcmp(ekeep, MEM(SEG_EDIT, 0), 65536)) { fprintf(stderr, "EDITS: PDREND wrote into the edit store\n"); return 7; }
        write_ppm(argv[12], 0, 0, W, H);
        if (!seqp && getenv("PDR_TEXTAFTER")) {
            /* the page's text right after drawing it, the font module refused (QA R17: Find under the
             * overlay on a 640 KB machine): the strike blocks the draw left carry it */
            FILE *o = fopen(getenv("PDR_TEXTAFTER"), "w"); int r;
            if (!o) return 2;
            setenv("PDR_NOSLOT", "3", 1); setenv("PDR_TMSTALE", "1", 1);
            pdrh_slot = 0; vcall(PDV_REND_SLOT, 0, 0, 0);        /* the slot given back after the draw */
            r = text_run(page, o); fclose(o);
            if (r) return r;
        }
#ifdef PDR_X8
        if (getenv("PDB_DUMP")) pdb_dump_write(getenv("PDB_DUMP"), W, H);
#endif
        if (seqp) { fflush(stdout); goto seq_next; }
        return 0;
    }
    if (!strcmp(argv[1], "viewchk")) {
        /* the renderer's base matrix against pdm_view_fwd (PAGEDESK-SPEC 5.3, 20.3.1) */
        extern void pdr_test_fwd(pd_pt, pd_pt, pdi *, pdi *);
        extern void pdr_test_base(const struct pd_view *);
        static const int rots[4] = { 0, 90, 180, 270 };
        static const int zooms[7] = { 25, 50, 75, 100, 150, 200, 400 };
        static const int asp[4][2] = { { 1, 1 }, { 5, 12 }, { 29, 45 }, { 35, 48 } };
        int ri, zi, ai, pi, worst = 0, n = 0;
        static const pd_pt pts[9][2] = { {0,0}, {16000,0}, {0,16000}, {16000,16000}, {8000,8000}, {1600,800}, {12345,2345}, {-1600,3200}, {6400,9600} };
        for (ri = 0; ri < 4; ri++) for (zi = 0; zi < 7; zi++) for (ai = 0; ai < 4; ai++) for (pi = 0; pi < 9; pi++) {
            struct pd_view v; pd_px fx, fy; pdi qx, qy; int dx, dy;
            memset(&v, 0, sizeof v);
            v.box.x0 = -1600; v.box.y0 = -800; v.box.x1 = 16000; v.box.y1 = 12800;   /* nonzero origin */
            v.rot = (pdw)rots[ri]; v.zoom = (pdw)zooms[zi]; v.asp_num = (pdw)asp[ai][0]; v.asp_den = (pdw)asp[ai][1];
            v.ox = 17; v.oy = 23;
            pdr_test_base(&v);
            pdr_test_fwd(pts[pi][0], pts[pi][1], &qx, &qy);
            pdm_view_fwd(&fx, &fy, &v, pts[pi][0], pts[pi][1]);
            dx = (qx + 2) / 4 - fx; if (qx + 2 < 0) dx = -((-(qx + 2) + 3) / 4) - fx;
            dy = (qy + 2) / 4 - fy; if (qy + 2 < 0) dy = -((-(qy + 2) + 3) / 4) - fy;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (dx > worst) worst = dx; if (dy > worst) worst = dy;
            n++;
        }
        printf("viewchk points=%d worst_px=%d\n", n, worst);
        return worst <= 1 ? 0 : 4;
    }
    if (!strcmp(argv[1], "text") && argc >= 5) {
        int page = atoi(argv[3]), kb, np;
        np = pdrh_open(argv[2]); if (np <= 0) return 2;
        kb = load_bank(argv[4]); do_init(1, kb, 22);
        return text_run(page, stdout);
    }
    if (!strcmp(argv[1], "thumb") && argc >= 8) {
        int page = atoi(argv[3]), w = atoi(argv[4]), h = atoi(argv[5]), kb, np, guard = 0, y, x, bytes;
        struct pdxb_thumb tq; pdw e; FILE *f;
        np = pdrh_open(argv[2]); if (np <= 0) return 2;
        kb = load_bank(argv[7]); do_init(1, kb, 22);
        memset(&tq, 0, sizeof tq); tq.page = (pdw)page; tq.w = (pdw)w; tq.h = (pdw)h; tq.dseg = SEG_CELL; tq.doff = 0;
        memset(MEM(SEG_CELL, 0), 0xAA, 8192);
        do {
            memcpy(MEM(SEG_XB, 0), &tq, sizeof tq);
            e = vcall(PDV_REND_THUMB, 0, 0, 0);
            memcpy(&tq, MEM(SEG_XB, 0), sizeof tq);
            if (ladder(e)) e = PDE_BUSY;
        } while (e == PDE_BUSY && ++guard < 100000);
        if (e) { fprintf(stderr, "thumb -> %u\n", e); return 3; }
        bytes = (w + 7) / 8;
        f = fopen(argv[6], "wb"); fprintf(f, "P5\n%d %d\n255\n", w, h);
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) fputc((MEM(SEG_CELL, 0)[y * bytes + x / 8] >> (7 - x % 8)) & 1 ? 255 : 0, f);
        fclose(f);
        report();
        return 0;
    }
    return 1;
}
