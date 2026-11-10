/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdr_xmod.c  stand-in slot occupants (host)
 *
 * PAGEDESK-SPEC 20.22.7. The PDREND host harness links this in place of the
 * real PDXG / PDXI / PDXJ so the ladder, the slot, the dash walker and the
 * image path can be held to MuPDF before the occupants exist:
 *
 *   PDX_GFX  PDV_GFX_DASH and PDV_GFX_EDIT through the REAL pdxdash.c and
 *            pdxedit.c (what PDXG #includes), so a dashed line and an edit
 *            mark must come out as they did when they were PDREND's own;
 *   PDX_IMG, PDX_JPX  a FAKE decoder: whatever the coded bytes, IMG_OPEN
 *            answers the image's own size (or 1/2^s of it when want_w/h
 *            allow) and IMG_ROW delivers a known ramp - hosttest/pdr_xchk.py
 *            draws the same ramp as a plain image and MuPDF must agree.
 *
 * PDR_XFAKE = grey (default) | n8 (RGB, out_ncomp 3) | bits (1-bit checker)
 * picks the rows; PDR_XHOSTILE = <seed> answers IMG_OPEN and IMG_ROW with
 * hostile sizes, modes and failures, for the sanitizer run.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../pdabi.h"
#include "../pdmath.h"

#include "../pdxdash.c"
#include "../pdxedit.c"

extern unsigned char pd_host_mem[];
extern pdw pdrh_xbseg(void);
#define XMEM(seg, off) (pd_host_mem + (((unsigned long)(seg) << 4) + (off)))

pdw pdrh_slot;                      /* the occupant the harness "loaded"     */
long pdrh_xcalls, pdrh_xopens, pdrh_xrows;
static struct pdxb_img g_im;
static int g_iopen, g_ilast;
static unsigned long g_rng;
static unsigned xr(void) { g_rng = g_rng * 1103515245u + 12345u; return (unsigned)(g_rng >> 16) & 0x7FFF; }

/* the ramp at (x, y) of a w x h image: what pdr_xchk.py also draws */
static unsigned ramp(unsigned x, unsigned y, unsigned w, unsigned h)
{
    unsigned v = (x * 200u) / (w > 1 ? w - 1 : 1) + (y * 55u) / (h > 1 ? h - 1 : 1);
    return v > 255 ? 255 : v;
}

static pdw img_open(pdw *cf)
{
    int s = 0;
    const char *m = getenv("PDR_XFAKE");
    memcpy(&g_im, XMEM(pdrh_xbseg(), 0), sizeof g_im);
    if (!g_im.w || !g_im.h) { *cf = 1; return PDE_SYNTAX; }
    while (s < 3 && (g_im.w >> (s + 1)) >= (g_im.want_w ? g_im.want_w : 1) && (g_im.h >> (s + 1)) >= (g_im.want_h ? g_im.want_h : 1)
           && (g_im.w >> (s + 1)) && (g_im.h >> (s + 1))) s++;
    g_im.scale = (pdw)s;
    g_im.out_w = (pdw)(g_im.w >> s); g_im.out_h = (pdw)(g_im.h >> s);
    if (m && !strcmp(m, "n8")) { g_im.out_mode = PDIM_N8; g_im.out_ncomp = 3; g_im.rowbytes = (pdw)(g_im.out_w * 3); }
    else if (m && !strcmp(m, "bits")) { g_im.out_mode = PDIM_BITS1; g_im.out_ncomp = 1; g_im.rowbytes = (pdw)((g_im.out_w + 7) >> 3); }
    else { g_im.out_mode = PDIM_GREY8; g_im.out_ncomp = 1; g_im.rowbytes = g_im.out_w; }
    if (g_im.rowbytes > 1024) { *cf = 1; return PDE_LIMIT; }
    if (getenv("PDR_XHOSTILE")) {
        g_im.out_w = (pdw)(xr() % 3 ? xr() % 2000 : 0xFFFF - xr() % 4);
        g_im.out_h = (pdw)(xr() % 3 ? xr() % 2000 : xr());
        g_im.out_mode = (pdw)(xr() % 5);
        g_im.out_ncomp = (pdw)(xr() % 9);
        g_im.rowbytes = (pdw)(xr() % 4 ? xr() % 1100 : xr());
        if (xr() % 7 == 0) { *cf = 1; return PDE_FILTER; }
    }
    memcpy(XMEM(pdrh_xbseg(), 0), &g_im, sizeof g_im);
    g_iopen = 1; g_ilast = -1; pdrh_xopens++;
    return 0;
}

static pdw img_row(pdw seg, pdw off, pdw row, pdw *cf)
{
    unsigned x, k, n = g_im.rowbytes > 1024 ? 1024 : g_im.rowbytes;
    unsigned char *d = XMEM(seg, off);
    if (!g_iopen) { *cf = 1; return PDE_RANGE; }
    if ((int)row < g_ilast) { fprintf(stderr, "XMOD: IMG_ROW %u after %d without a new OPEN\n", row, g_ilast); exit(8); }
    g_ilast = (int)row; pdrh_xrows++;
    if (getenv("PDR_XHOSTILE")) {
        if (xr() % 11 == 0) { *cf = 1; return PDE_FILTER; }
        for (k = 0; k < n; k++) d[k] = (unsigned char)xr();
        return 0;
    }
    if (row >= g_im.out_h) { *cf = 1; return PDE_RANGE; }
    if (g_im.out_mode == PDIM_BITS1) {
        memset(d, 0, n);
        for (x = 0; x < g_im.out_w; x++) if (((x >> 2) ^ (row >> 2)) & 1) d[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
    } else if (g_im.out_mode == PDIM_N8) {
        for (x = 0; x < g_im.out_w; x++) {
            unsigned v = ramp(x, row, g_im.out_w, g_im.out_h);
            d[x * 3] = (unsigned char)v; d[x * 3 + 1] = (unsigned char)(255 - v); d[x * 3 + 2] = (unsigned char)(v / 2);
        }
    } else for (x = 0; x < g_im.out_w; x++) d[x] = (unsigned char)ramp(x, row, g_im.out_w, g_im.out_h);
    if (g_im.flags & PDIMG_INVERT) for (k = 0; k < n; k++) d[k] = (unsigned char)~d[k];
    return 0;
}

pdw pdrh_xmod(pdw verb, pdw bx, pdw cx, pdw dx, pdw *cf, pdw *dxo);
/* pdrprim.inc's pdr_xfar, twinned: the far call to the occupant's door */
pdw pdr_x_off, pdr_x_seg, pdr_x_cf, pdr_x_dx, pdr_out_cx, pdr_out_dx;
pdw pdr_xfar(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw cf, d, r = pdrh_xmod(verb, bx, cx, dx, &cf, &d);
    pdr_x_cf = cf; pdr_x_dx = d;
    return r;
}

/* the door: AL = verb, BX CX DX; AX back, CF and DX through the pointers */
pdw pdrh_xmod(pdw verb, pdw bx, pdw cx, pdw dx, pdw *cf, pdw *dxo)
{
    *cf = 0; *dxo = 0; pdrh_xcalls++;
    if (getenv("PDR_XHOSTILE") && !g_rng) g_rng = (unsigned long)atol(getenv("PDR_XHOSTILE")) | 1;
    if (verb == PDV_X_INIT) return 0;
    switch (pdrh_slot) {
    case PDX_GFX:
        if (verb == 0xCE) return xd_verb(bx, cx);
        if (verb == 0xCF) return xe_verb(bx, cx);
        break;
    case PDX_IMG: case PDX_JPX:
        if (verb == PDV_IMG_OPEN) return img_open(cf);
        if (verb == PDV_IMG_ROW) return img_row(bx, cx, dx, cf);
        if (verb == PDV_IMG_CLOSE) { g_iopen = 0; return 0; }
        break;
    }
    fprintf(stderr, "XMOD: verb 0x%x sent to occupant %u\n", verb, pdrh_slot);
    *cf = 1;
    return PDE_RANGE;
}
