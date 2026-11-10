/* ============================================================================
 * os8088 - apps/pagedesk/pdrx8.c      PDREND: the hooks of PDX8 (transparency)
 *
 * PAGEDESK-SPEC 20.22.10. #included by pdrend.c after pdrd.c (the sinks it
 * hooks) and before pdri.c. OWNED BY blend.
 *
 * The raster compositor is PDX8 (pdx8.c), a part in the X8 region. What stays
 * here is what only PDREND can know: the walker over the display list (clips,
 * glyphs, edges and images are PDREND's), the mark map, and the far calls.
 * A band a transparent mark touches is walked in strips of at most 8 rows; each
 * strip is begun and ended in PDX8, and the walker paints through it: a span
 * (PDV_X8_SPAN), an image row (PDV_X8_IMG), a transparency record (PDV_X8_REC).
 * ==========================================================================*/

/* ---- the region ------------------------------------------------------------ */
static pdw g_x8seg, g_x8kb;     /* PDV_REND_INIT's x8_seg / x8_kb               */
static pdw g_x8on;              /* the region fits this view's band             */
static pdw g_x8need;            /* KiB the view wants (PDX8_RES + planes)       */
static pdw g_x8ask;             /* PDE_NEEDMEM answered once this view          */
static pdw g_wst, g_wsave;      /* the last STATE record's offset + 1, and the one a mask block interrupted */
static pdw g_wmend;             /* a mask block is being walked: its end        */
static pdw g_woff;              /* the walker's position in the list            */

static void x8_bits(pdw v)
{
    if (v & X8F_LIMIT)  inc_note(PDF_INC_LIMIT, "transparency limits");
    if (v & X8F_TRANSP) inc_note(PDF_INC_TRANSP, "transparency");
}

/* ---- images: the strip's mask and the rows --------------------------------- */
static void x8_imgset(const pdb *b, pdw flags, pdi xl, pdi yt, pdi wq, pdi hq, pdi x0, pdi x1, pdi ry0, pdi ry1)
{
    static struct pdxb_x8img s;
    if (!(flags & 0x60)) return;
    s.obj = (pdw)r16(b + 12); s.gen = (pdw)r16(b + 14); s.flags = flags;
    s.xl = xl; s.yt = yt; s.wq = wq; s.hq = hq; s.x0 = x0; s.x1 = x1; s.ry0 = ry0; s.ry1 = ry1;
    pd_fcopy_out(&s, g_xb, 0, sizeof s);
    x8_call(PDV_X8_IMG, X8I_SET, 0, 0);
    x8_bits(x8_ocx);
}
static void x8_imgrow(pdi y)
{
    pd_fcopy_out(&pdr_ip.x0, g_xb, 0, sizeof(struct pdxb_x8row));
    x8_call(PDV_X8_IMG, X8I_ROW, (pdw)y, 0);
}

/* ---- the band walker (pdrd.c's first version, with the transparency records) ---- */
static pdw x8_want(void)
{
    if (!g_x8on || g_thumb) return 0;
    return x8_call(PDV_X8_CTL, X8C_WANT, (pdw)g_wlo, (pdw)g_whi);
}

/* the clip, the colour index and the pattern of the STATE record at `off` */
static void x8_state(pdw off)
{
    pd_fcopy_in(g_dls, off + DLH, g_stl, 14);
    g_scx0 = r16(g_stl); g_scy0 = r16(g_stl + 2);
    g_scx1 = r16(g_stl + 4); g_scy1 = r16(g_stl + 6);
    g_sclid = g_stl[8]; g_sc4 = g_stl[9];
    pdr_cpy(g_spat, g_stl + 10, 4);
}

static void walk_core(void)
{
    pdw type, len, ne, nx;
    pdi y0, y1, a, yy0, yy1, x0, x1;
    static pdb pl[6];
    g_scx0 = g_vcx0; g_scy0 = g_vcy0; g_scx1 = g_vcx1; g_scy1 = g_vcy1;
    g_sclid = 0xFF; g_sc4 = 0; pat_make(g_spat, 0, 1);
    g_woff = 0; g_wst = 0; g_wmend = 0;
    for (;;) {
        if (g_wmend && g_woff >= g_wmend) {          /* the mask block ended: PDX8 turns it into MK */
            g_woff = x8_call(PDV_X8_CTL, X8C_MKEND, 0, 0); g_wmend = 0;
            g_wst = g_wsave;
            if (g_wst) x8_state((pdw)(g_wst - 1));
            continue;
        }
        if (g_woff >= g_dllen) break;
        pd_fcopy_in(g_dls, g_woff, g_hdr, DLH);
        type = g_hdr[0]; len = (pdw)r16(g_hdr + 2);
        if (len < DLH || len > g_dllen - g_woff) break;     /* only whole records */
        y0 = r16(g_hdr + 4); y1 = r16(g_hdr + 6);
        if (type == DLR_STATE) {
            x8_state(g_woff);
            g_wst = (pdw)(g_woff + 1);
            if (g_xs) x8_call(PDV_X8_CTL, X8C_LUM, g_hdr[1], 0);
        } else if (type == DLR_XSTATE || (type == DLR_GROUP && y1 >= g_wlo && y0 <= g_whi)) {
            if (g_xs) {
                nx = x8_call(PDV_X8_REC, g_woff, 0, 0);
                g_wmend = x8_odx;
                if (x8_ocx) g_wsave = g_wst;         /* a block begins: the clip to come back to */
                g_woff = nx;
                continue;
            }
        } else if (y1 >= g_wlo && y0 <= g_whi) {
            yy0 = y0 < g_wlo ? g_wlo : y0;
            yy1 = y1 > g_whi ? g_whi : y1;
            if (type == DLR_RECT) {
                pd_fcopy_in(g_dls, g_woff + DLH, pl, 4);
                x0 = r16(pl); x1 = r16(pl + 2);
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 0, x0, x1, 0, 0); }
                else for (a = yy0; a <= yy1; a++) span_out(a, x0, x1);
            } else if (type == DLR_FILL) {
                pd_fcopy_in(g_dls, g_woff + DLH, pl, 6);
                ne = (pdw)r16(pl + 4);
                if (ne > PDR_NEDGE) ne = PDR_NEDGE;
                g_fe = (pdw)(g_woff + DLH + 6);       /* the edges are scanned in the list itself */
                if (g_sclid < PDR_NCLIP && g_mkok) { for (a = yy0; a <= yy1; a++) shape_clipped(a, 1, 0, 0, ne, g_hdr[1]); }
                else rast_edges(g_dls, g_fe, ne, g_hdr[1], yy0, yy1);
            } else if (type == DLR_GLYPHS) {
                draw_glyphs(g_woff, len, yy0, yy1);
            } else if (type == DLR_IMAGE) {
                draw_image(g_woff, len, yy0, yy1);
                if (g_xs) x8_call(PDV_X8_IMG, X8I_END, 0, 0);
            }
        }
        g_woff += len;
    }
}

#ifdef PD_HOST
#include <stdio.h>
/* the harness reads the strips before they are quantised: PDB_DUMP names a file the
 * canvas' 8-bit rows are written to at exit (hosttest/pdr_test.c); 255 = never drawn 8-bit.
 * x8_dump reads PDX8's planes, which live at x8_seg:PDX8_RES */
static pdb pdb_img[2048][1024];
static pdw pdb_w, pdb_h;
static pdb pdb_row[2048];
static void pdb_dump_strip(pdi y0, pdi y1)
{
    pdi y;
    pdw x;
    for (y = y0; y <= y1; y++) if (y >= 0 && y < 2048) pdb_row[y] = 1;
    for (y = y0; y <= y1; y++)
        for (x = 0; x < (pdw)g_bw && (pdw)(g_bx0 + x) < 1024 && y >= 0 && y < 2048; x++) {
            pdb_img[y][g_bx0 + x] = pd_fpeekb(g_x8seg, (pdw)(PDX8_RES + (pdw)(y - y0) * g_bw + x));
            if ((pdw)(g_bx0 + x + 1) > pdb_w) pdb_w = (pdw)(g_bx0 + x + 1);
            if ((pdw)(y + 1) > pdb_h) pdb_h = (pdw)(y + 1);
        }
}
void pdb_dump_write(const char *path, int w, int h)
{
    FILE *f = fopen(path, "wb");
    int y;
    for (y = 0; y < h; y++) fwrite(pdb_img[y], 1, (size_t)w, f);
    fwrite(pdb_row, 1, (size_t)h, f);                 /* then one flag a row: drawn in 8 bits */
    fclose(f);
}
#endif
static void x8_strips(void)
{
    static struct pdxb_x8band b;
    pdi lo = g_wlo, hi = g_whi, y;
    b.dls = g_dls; b.dllen = g_dllen; b.bx0 = g_bx0; b.by0 = g_by0; b.bw = (pdw)g_bw;
    b.bseg = g_bseg; b.bstride = g_bstride; b.bpp = g_bpp;
    b.pgx0 = g_pgx0; b.pgy0 = g_pgy0; b.pgx1 = g_pgx1; b.pgy1 = g_pgy1;
    pd_fcopy_out(&b, g_xb, 0, sizeof b);
    x8_call(PDV_X8_CTL, X8C_BAND, 0, 0);
    for (y = lo; y <= hi; y = (pdi)(y + 8)) {
        g_wlo = y; g_whi = (pdi)(y + 7) > hi ? hi : (pdi)(y + 7);
        g_xs = 1;
        x8_call(PDV_X8_CTL, X8C_BEGIN, (pdw)g_wlo, (pdw)g_whi);
        walk_core();
#ifdef PD_HOST
        pdb_dump_strip(g_wlo, g_whi);
#endif
        x8_call(PDV_X8_CTL, X8C_END, 0, 0);
        x8_bits(x8_ocx);
        g_xs = 0;
    }
    g_wlo = lo; g_whi = hi;
}

static void walk_band(pdi ry0, pdi ry1)
{
    (void)ry0; (void)ry1;
    if (x8_want()) x8_strips();
    else walk_core();
}

/* ---- the view: the region, its size, and the question to the primary --------- */
static void x8_init(pdw seg, pdw kb)
{
    g_x8seg = seg; g_x8kb = kb > 63 ? 63 : kb;
#ifndef PD_HOST
    if (g_x8seg && pd_fpeekw(g_x8seg, 0) != PDM_MAGIC_X8) g_x8seg = 0;       /* PDX8 is not what was loaded there */
#endif
    if (!g_x8seg) g_x8kb = 0;
    g_x8on = g_x8need && g_x8seg && g_x8kb >= g_x8need;     /* a re-INIT after PDE_NEEDMEM */
    g_x8rdy = g_x8seg != 0;
    if (g_x8seg) {
#ifndef PD_HOST
        pdr_x8bind(g_x8seg);
#endif
        x8_call(PDV_X8_INIT, g_x8seg, g_x8kb, g_xb);
    }
}
static void x8_view(void)
{
    pdw need;
    g_x8ask = 0; g_x8on = 0;
    if (g_bw > 768) { g_x8need = 0; return; }
    need = (pdw)(PDX8_RES + 6u * 8u * (pdw)g_bw + 1024u);
    g_x8need = (pdw)((need + 1023u) >> 10);
    g_x8on = g_x8seg && g_x8kb >= g_x8need;
}
/* the end of interpretation, before any band of the pass is drawn (20.22.10.2) */
static pdw xb_needmem(void)
{
    if (!g_x8any || g_x8on) return 0;
    if (!g_x8need) { inc_note(PDF_INC_APPROX, "band too wide for transparency"); return 0; }
    if (g_x8ask) { inc_note(PDF_INC_APPROX, "no RAM for transparency"); return 0; }
    g_x8ask = 1;
    return g_x8need;
}
