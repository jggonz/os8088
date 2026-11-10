/* ============================================================================
 * os8088 - apps/pagedesk/pdrxc.c   PDREND's side of the module slot
 *
 * PAGEDESK-SPEC 20.22.2 (the ladder) and 20.22.7 (rcore: this file's
 * contract). #included by pdrend.c FIRST, so every pdr*.c file and the
 * agents' glue can call it. OWNED BY rcore. The NAMES and SIGNATURES of
 * x_have, x_need, x_call (+ x_cf, x_dx), xc_find, xc_alloc, g_xcseg and the
 * XCK_* kinds are pinned by the lead; x_gone, x_mem and xc_cancel are added.
 *
 * THE LADDER (L2). Work for an absent module calls x_need(id) and is skipped;
 * the pass runs on. pdrv.c's x_resolve() turns what was needed into ONE
 * PDE_NEEDMOD (or PDE_NEEDMEM) at the end of the interpretation pass, and the
 * pass is run again when the primary has answered. A module the primary could
 * not load is REFUSED for the rest of the page view: x_need() then notes
 * PDF_INC_MODULE instead, and x_gone(id) lets a caller draw its degraded form.
 *
 * THE EXTENSION CACHE (PDMEM_XC). Entries are packed from offset 0 of the
 * region: an 8-byte header (struct xc_hdr) and the caller's bytes. An entry
 * is named by (kind, k0, k1). xc_find/xc_alloc answer the offset of the
 * entry's BYTES in g_xcseg (never 0: the header is in front), 0 = absent /
 * no room. AN OFFSET IS GOOD UNTIL THE END OF THE PASS: the cache is flushed
 * whole only between passes, when one filled it; keep keys across passes.
 * ==========================================================================*/

/* Cache kinds in PDMEM_XC. An entry is keyed by (kind, k0, k1): k0/k1 are
 * normally an object number and generation, or a resource-name hash. */
#define XCK_CS     1    /* gfx: a compiled colour space                   */
#define XCK_FN     2    /* gfx: a compiled function                       */
#define XCK_SHADE  3    /* gfx: a compiled shading                        */
#define XCK_PAT    4    /* gfx: a pattern                                 */
#define XCK_LUT    5    /* gfx: an image colour-space LUT (20.22.9)       */
#define XCK_FONT   6    /* fonts: a font record                           */
#define XCK_CMAP   7    /* fonts: a CMap / ToUnicode table                */
#define XCK_GS     8    /* blend: a compiled ExtGState                    */

static void inc_note(pdw bit, const char *name);        /* pdrg.c */

/* ---- the slot ------------------------------------------------------------ */
/* the far call (pdrprim.inc): AL = verb, BX CX DX through the vector the
 * primary gave with PDV_REND_SLOT. AX back; CF and DX in pdr_x_cf / pdr_x_dx */
pdw pdr_xfar(pdw verb, pdw bx, pdw cx, pdw dx);
extern pdw pdr_x_off, pdr_x_seg, pdr_x_cf, pdr_x_dx;
/* the door's CX and DX when a verb answers PDE_NEEDMOD / PDE_NEEDMEM */
extern pdw pdr_out_cx, pdr_out_dx;

static pdw x_res;               /* PDX_* in the slot now (PDV_REND_SLOT)  */
static pdw x_pend;              /* bit (1 << PDX_*): needed, absent, this pass */
static pdw x_ref;               /* refused for this page view (XR_* too)  */
static pdw x_cf, x_dx;          /* the last x_call's carry flag and DX     */
static pdw x_used, x_swaps;     /* pdxb_rstatus.mods / .swaps              */
static pdw x_mwant, x_mkb;      /* a region wanted (0x80 | PDMEM_*, KiB), 0 none: PDMEM_SLOT is 0 */
#define XR_MEM  0x8000u         /* x_ref: the region asked for was refused */
#define XR_GLYPH 0x4000u        /* x_ref: the glyph claim's growth was refused (fonts, 20.22.8) */
static const char *const x_name[PDX_COUNT] = { "", "key module", "graphics module",
    "font module", "image module", "JPX module" };

/* 1 when module `id` is the slot's occupant now. */
static pdw x_have(pdw id)
{
    return id != PDX_NONE && x_res == id;
}

/* 1 when module `id` was refused for this page view: draw without it. */
static pdw x_gone(pdw id)
{
    return (pdw)((x_ref >> id) & 1);
}

/* Record that this pass needed module `id` and skipped work for it (L2). */
static void x_need(pdw id)
{
    if (id == PDX_NONE || id >= PDX_COUNT) return;
    if (x_gone(id)) inc_note(PDF_INC_MODULE, x_name[id]);
    else x_pend |= (pdw)(1u << id);
}
/* module `id` cannot be had for the rest of the page view (pdrv.c) */
static void x_refuse(pdw id)
{
    x_ref |= (pdw)(1u << id);
    inc_note(PDF_INC_MODULE, x_name[id]);
}

/* Ask for a region the primary claims (PDMEM_*, KiB): answered with
 * PDE_NEEDMEM at the end of the pass, which is then run again. 0 = refused
 * for this page view (go on without it). */
static pdw x_mem(pdw region, pdw kb)
{
    if (x_ref & XR_MEM) return 0;
    if (!x_mwant) { x_mwant = (pdw)(0x80 | region); x_mkb = kb; }
    return 1;
}

/* Far-call the occupant's door: AL = verb, BX/CX/DX. Answers AX; the carry
 * flag lands in x_cf and DX in x_dx. Only valid when x_have(the id). */
static pdw x_call(pdw verb, pdw bx, pdw cx, pdw dx)
{
    pdw r;
    if (!x_res || !pdr_x_seg) { x_cf = 1; x_dx = 0; return PDE_NOSLOT; }
    r = pdr_xfar(verb, bx, cx, dx);
    x_cf = pdr_x_cf; x_dx = pdr_x_dx;
    x_used |= (pdw)(1u << x_res);
    return r;
}

/* the raster-time occupants this pass's records need: pdrv.c's x_rneed looks
 * at a band's records only when there is one */
static pdw g_xrm;
#define x_rmark(m) (g_xrm |= (pdw)(1u << (m)))

/* ---- the extension cache ---------------------------------------------------- */
/* pdrprim.inc: the offset of the bytes of the entry (kind, k0, k1) among
 * those below `top` in seg, 0 = none. A scan: an entry is a header of 8 bytes
 * (kind, flags, len, k0, k1; len counts the bytes after it), then its bytes */
pdw pdr_xcfind(pdw seg, pdw top, pdw kind, pdw k0, pdw k1);
static pdw g_xcseg;             /* PDMEM_XC, 0 = none                       */
static pdw g_xccap, g_xctop;    /* bytes it holds / in use                  */
static pdw g_xcfull;            /* an alloc found no room this pass         */
static pdw g_xcflush;           /* flushed this page view (once at most)    */
static struct { pdb kind, flags; pdw len, k0, k1; } g_xch;

static pdw xc_find(pdw kind, pdw k0, pdw k1)
{
    return g_xcseg && kind ? pdr_xcfind(g_xcseg, g_xctop, kind, k0, k1) : 0;
}
static pdw xc_alloc(pdw kind, pdw k0, pdw k1, pdw bytes)
{
    pdw o = g_xctop;
    if (!g_xcseg) { x_mem(PDMEM_XC, PDR_XCKB); return 0; }
    bytes = (pdw)((bytes + 1) & ~1u);
    if (!kind || g_xccap < o + 8 || bytes > g_xccap - o - 8) { g_xcfull = 1; return 0; }
    g_xch.kind = (pdb)kind; g_xch.len = bytes; g_xch.k0 = k0; g_xch.k1 = k1;
    pd_fcopy_out(&g_xch, g_xcseg, o, 8);
    g_xctop = (pdw)(o + 8 + bytes);
    return (pdw)(o + 8);
}
