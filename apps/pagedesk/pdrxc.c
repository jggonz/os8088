/* ============================================================================
 * os8088 - apps/pagedesk/pdrxc.c   PDREND's side of the module slot
 *
 * PAGEDESK-SPEC 20.22.2 (the ladder) and 20.22.7 (rcore). #included by
 * pdrend.c after pdrg.c, so every later pdr*.c file and the agents' glue can
 * call it. OWNED BY rcore. The NAMES and SIGNATURES below are pinned by the
 * lead: gfx, blend and fonts build against them in parallel, so rcore changes
 * the bodies, never the interface (a change is a BLOCKED line to the lead).
 *
 * The lead's first version is a STAND-IN: nothing is ever resident, every
 * need is recorded, and the cache refuses every allocation. A caller written
 * against it therefore already takes its "module absent" and "no cache"
 * paths, which every caller must have anyway (L4: refusal is a normal path).
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

static pdw x_res;               /* PDX_* in the slot now (PDV_REND_SLOT)  */
static pdw x_door_off, x_door_seg;
static pdw x_pend;              /* bit (1 << PDX_*): needed, absent        */
static pdw x_cf, x_dx;          /* the last x_call's carry flag and DX     */

/* 1 when module `id` is the slot's occupant now. */
static int x_have(pdw id)
{
    return id != PDX_NONE && x_res == id;
}

/* Record that this pass needed module `id` and skipped work for it (L2). */
static void x_need(pdw id)
{
    x_pend |= (pdw)(1u << id);
}

/* Far-call the occupant's door: AL = verb, BX/CX/DX. Answers AX; the carry
 * flag lands in x_cf and DX in x_dx. Only valid when x_have(the id). */
static pdw x_call(pdw verb, pdw bx, pdw cx, pdw dx)
{
    (void)verb; (void)bx; (void)cx; (void)dx;
    x_cf = 1; x_dx = 0;
    return PDE_NOSLOT;
}

/* The extension cache: answers the entry's offset in the XC segment
 * (g_xcseg below), 0 = absent / no room. Entries survive between pages of
 * one document; the cache may be flushed whole when it fills. */
static pdw g_xcseg;
static pdw xc_find(pdw kind, pdw k0, pdw k1)
{
    (void)kind; (void)k0; (void)k1;
    return 0;
}
static pdw xc_alloc(pdw kind, pdw k0, pdw k1, pdw bytes)
{
    (void)kind; (void)k0; (void)k1; (void)bytes;
    return 0;
}
