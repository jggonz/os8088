/* pdx_hosteng.c: pdr_hosteng.c with FLATE slots in the engine (the fixtures' font programs are
 * Flate streams) - hosttest/pdx_page.py builds it. ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdr_hosteng.c   PDREND against the REAL PDENG
 *
 * Linked INSTEAD of the mini engine in pdr_host.c (-DPDR_REAL_ENG): pdr_eng()
 * calls pde_verb() - PDENG.MOD's own C - and the document is opened through the
 * engine's own OPEN, so every verb PDREND relies on (the stream stack, "[n]"
 * paths, final references, saturated tokens, inline-image reads) is exercised
 * against what the module will really answer. The file calls are one stub: the
 * document (PDENG's pde_host.c, READ_SEQ contract enforced) or a font file
 * (PDREND's pdr_font_read_seq), told apart by name.
 *
 *   cc -DPD_HOST -DPDR_REAL_ENG -I apps/pagedesk -I <engine dir> ... pdr_hosteng.c <engine dir>/pdeng.c
 * ==========================================================================*/
#include <stdio.h>
#include <string.h>
#define os88_file_read_seq pde_doc_read_seq
#define os88_disk_cluster_sectors pde_doc_cluster
#include "hosttest/pde_host.c"                 /* the engine's file layer (-I <engine dir>) */
#undef os88_file_read_seq
#undef os88_disk_cluster_sectors
#include "../pdrend.h"

extern unsigned pde_fail, pde_dx;
extern unsigned pde_verb(unsigned verb, unsigned bx, unsigned cx, unsigned dx);
extern int pdrh_cluster_sectors;
extern unsigned pdrh_here_clus; extern int pdrh_here_vol; extern long pdrh_wrongdir;
extern int pdrh_in_doc(void);
extern unsigned pdr_font_read_seq(const char *name, unsigned seg, unsigned off, unsigned cap, unsigned *cursor);

int os88_disk_cluster_sectors(void) { return pdrh_cluster_sectors; }
unsigned os88_file_read_seq(const char *name, unsigned seg, unsigned off, unsigned cap, unsigned *cursor)
{
    pdh_cluster = pdrh_cluster_sectors;
    if (!strcmp(name, pdh_name)) {
        if (!pdrh_in_doc()) {
            fprintf(stderr, "FOLDER: the document read by name from %u:%d, not its own folder\n", pdrh_here_clus, pdrh_here_vol);
            pdrh_wrongdir++; return 0;
        }
        return pde_doc_read_seq(name, (pdw)seg, (pdw)off, (pdw)cap, (pdw *)cursor);
    }
    return pdr_font_read_seq(name, seg, off, cap, cursor);
}

pdw pdr_eng(pdw verb, pdw bx, pdw cx, pdw dx)
{
    unsigned r = pde_verb(verb, bx, cx, dx);
    pdr_eng_ax = 0; pdr_eng_dx = 0;
    if (pde_fail) return (pdw)pde_fail;
    pdr_eng_ax = (pdw)r; pdr_eng_dx = (pdw)pde_dx;
    return 0;
}

static int g_np;
#define SEG_XB    0x2000
#define SEG_ECACHE 0x7000
#define SEG_EARENA 0x7800
#define SEG_ETBL   0x7C00
/* open `path` through the engine: INIT (claims 16 + 8 + 11 KiB), OPEN until it stops answering BUSY */
int pdrh_open(const char *path)
{
    struct pdxb_init in; struct pdxb_open op; unsigned r; int n = 0;
    pdh_cluster = pdrh_cluster_sectors;
    if (pdh_load(path)) return 0;
    memset(&in, 0, sizeof in);
    in.xb_seg = SEG_XB; in.cache_seg = SEG_ECACHE; in.cache_kb = 16; in.arena_seg = SEG_EARENA; in.arena_kb = 8;
    in.tables_seg = SEG_ETBL; in.tables_kb = 11; in.flate_seg = 0xD000; in.flate_kb = 86; in.flags = PDXI_FLATE;
    pd_fcopy_out(&in, SEG_XB, 0, sizeof in);
    pde_verb(PDV_ENG_INIT, SEG_XB, 0, 0);
    if (pde_fail) { fprintf(stderr, "engine INIT failed %u\n", pde_fail); return 0; }
    memset(&op, 0, sizeof op); strcpy(op.name, "DOC.PDF");
    for (;;) {
        pd_fcopy_out(&op, SEG_XB, 0, sizeof op);
        r = pde_verb(PDV_ENG_OPEN, 0, 0, 0);
        pd_fcopy_in(SEG_XB, 0, &op, sizeof op);
        /* D11: more to do is CF = 0 (pde_fail 0) with AX = PDE_BUSY */
        if (!pde_fail && r == PDE_BUSY) { if (++n > 5000) return 0; continue; }
        if (pde_fail) { fprintf(stderr, "engine OPEN failed %u (obj %u detail %u)\n", r, op.err_obj, op.err_detail); return 0; }
        break;
    }
    g_np = op.npages;
    return g_np;
}
int pdrh_npages(void) { return g_np; }
int pdrh_nstreams(void) { return 0; }

/* PDXF's way into the same engine (PAGEDESK-SPEC 20.22.8) */
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
    if (!d) return 0;
    q = (p + d / 2) / d;
    if (q > 32767) q = 32767;
    return (pdi)(neg ? -q : q);
}
