/* ============================================================================
 * os8088 - apps/lemmings/hosttest/lemraster.c   (#included by lemtest.c)
 *
 * A MODEL OF THE RASTER, not a set of refusing stubs. LESSONS.md 7's first
 * harness trap is that "a stub that always refuses measures the fallback path"
 * - cword's stub gfx_blit1 refused and the cost table priced the 71-call
 * fallback for a whole session - so every entry apps/lemmings/lemblit.inc,
 * lemmask.inc and lemfont.inc defines has a NAMED FUNCTION here that does what
 * the assembly does, on a byte-per-pixel model of the glass, and counts what
 * it did.
 *
 * WHAT IT MODELS, and why each one is worth the code:
 *
 *   h_solid[160][1600]   the solid mask, one byte a pixel. The composer, the
 *                        probe and the destruction stencils all write it, and
 *                        the harness asserts the terrain the fixture's record
 *                        describes is exactly where the record says.
 *   h_world[160][1600]   the world PICTURE, one byte a pixel, 0..15. On the
 *                        machine this is four VGA planes or a claim in the
 *                        adapter's own form; here it is one array, because
 *                        what the harness is checking is WHAT was drawn and
 *                        where, and the plane arithmetic is the assembly's own
 *                        business (it has a raw-x86 gate of its own).
 *   h_screen[200][320]   the composed screen: the world window above row 160
 *                        and the panel below it.
 *   h_wrote[200][320]    HOW MANY TIMES each screen pixel was written since
 *                        the last counters_reset(). This is what makes "no
 *                        pixel is written twice in a frame" an assertion
 *                        rather than a hope - it is one of PERFORMANCE.md's
 *                        three defects that never show in a screendump.
 *
 * THE CLAIMS ARE MODELLED TOO, and they have to be: every raster entry takes a
 * SEGMENT, and ovl_far() does real paragraph arithmetic on one to reach past
 * 64 KB. h_ptr() turns a (segment, offset) pair back into a byte of a modelled
 * claim and FAILS THE BUILD on one that is not inside a live claim, which is
 * the check no screendump can make.
 * ==========================================================================*/

/* The world, stated here as well as in apps/lemmings/lemmings.c and in
 * lemmask.inc's own `equ`, because this file is compiled BEFORE the program
 * (the program calls it) and a model that took the number from the thing it is
 * modelling would agree with a wrong one. tests/unit/t_lemdat.py holds the
 * converter to the same 1,584 x 160 from a third direction. */
#define LEM_WORLD_W 1584
#define LEM_WORLD_H  160

/* --- the modelled claims -------------------------------------------------- */
#define H_CLAIMS      8
#define H_CLAIMBYTES  (128 * 1024)
#define H_CLAIMPARAS  0x2000            /* 128 KB in paragraphs */
#define H_CLAIMBASE   0x1000

static unsigned char h_mem[H_CLAIMS][H_CLAIMBYTES];
static int h_claim_live[H_CLAIMS];
static int h_claim_kb[H_CLAIMS];
static int h_claims_now;
static int h_claims_peak;
static unsigned char h_void[16];        /* where a bad pointer lands, once the
                                         * failure has been recorded */

/* THE MACHINE'S OWN CEILING, and the harness holds the program to it:
 * kernel/memory.inc gives MEM_MAX = 20 on kern_small, SYSTEM-WIDE and shared
 * with every Disk window and driver already open (SPEC.md 116.6.1). This
 * program may not be most of it. */
#define H_MEM_KB      512               /* what a 640 KB machine has free */
#define H_MEM_MAX      20

static int h_mem_kb_used;

static unsigned h_claim(int kb)
{
    int i;

    if (kb <= 0)
        return 0;
    if (h_claims_now >= H_MEM_MAX)
        return 0;
    if (h_mem_kb_used + kb > H_MEM_KB)
        return 0;
    if (kb * 1024 > H_CLAIMBYTES) {
        fail("a claim bigger than the harness models - grow H_CLAIMBYTES");
        return 0;
    }
    for (i = 0; i < H_CLAIMS; i++)
        if (!h_claim_live[i]) {
            h_claim_live[i] = 1;
            h_claim_kb[i] = kb;
            h_mem_kb_used += kb;
            h_claims_now++;
            if (h_claims_now > h_claims_peak)
                h_claims_peak = h_claims_now;
            memset(h_mem[i], 0, H_CLAIMBYTES);
            return (unsigned)(H_CLAIMBASE + i * H_CLAIMPARAS);
        }
    fail("more claims than the harness models - grow H_CLAIMS");
    return 0;
}

static int h_claim_index(unsigned seg)
{
    unsigned d;

    if (seg < H_CLAIMBASE)
        return -1;
    d = seg - H_CLAIMBASE;
    return (int)(d / H_CLAIMPARAS);
}

static unsigned char *h_ptr(unsigned seg, unsigned off)
{
    int i = h_claim_index(seg);
    unsigned b;

    if (i < 0 || i >= H_CLAIMS || !h_claim_live[i]) {
        fail("a raster call was given a segment that is not a live claim");
        return h_void;
    }
    b = ((seg - H_CLAIMBASE) % H_CLAIMPARAS) * 16 + off;
    if (b >= (unsigned)(h_claim_kb[i] * 1024)) {
        fail("a raster call read past the end of its claim");
        return h_void;
    }
    return &h_mem[i][b];
}

unsigned os88_mem_claim(int kb)      { return h_claim(kb); }
unsigned os88_mem_largest_kb(void)   { return (unsigned)(H_MEM_KB - h_mem_kb_used); }
unsigned os88_mem_total_kb(void)     { return H_MEM_KB; }

int os88_mem_free(unsigned seg)
{
    int i = h_claim_index(seg);

    if (i < 0 || i >= H_CLAIMS || !h_claim_live[i]) {
        fail("os88_mem_free on a segment that is not a live claim");
        return -1;
    }
    h_claim_live[i] = 0;
    h_mem_kb_used -= h_claim_kb[i];
    h_claims_now--;
    return 0;
}

/* THE BIOS DATA AREA's one byte this program reads: 0040:0096, whose bit 4
 * says the keyboard has F11 and F12 (lemui.c lem_kbd101, wave 5). Anything
 * else at segment 0x40 is a read this program has no business making. */
static unsigned char h_bda96 = 0x10;    /* an AT keyboard, as QEMU's */

int os88_peek(unsigned seg, unsigned off)
{
    if (seg == 0x40) {
        if (off != 0x96)
            fail("os88_peek read a BIOS data byte other than 0040:0096");
        return h_bda96;
    }
    return *h_ptr(seg, off);
}
void os88_poke(unsigned seg, unsigned off, int v) { *h_ptr(seg, off) = (unsigned char)v; }

/* --- the modelled glass ---------------------------------------------------- */
#define H_WORLD_W 1600
#define H_WORLD_H  160
#define H_SCR_W    320
#define H_SCR_H    200
#define H_VIEW_H   160

static unsigned char h_solid[H_WORLD_H][H_WORLD_W];
static unsigned char h_world[H_WORLD_H][H_WORLD_W];
static unsigned char h_t[H_WORLD_H][H_WORLD_W];
static unsigned char h_screen[H_SCR_H][H_SCR_W];
static unsigned short h_wrote[H_SCR_H][H_SCR_W];

static int h_r_kind = -1;
static int h_r_prepped;
static int h_r_scroll = -1;
static int h_r_presents;
static int h_r_pixels;              /* screen pixels written since the reset */
static int h_r_worldpix;            /* world pixels written since the reset */
static int h_r_probes;              /* lem_has_pixel + 4 per lem_probe4 */
static int h_r_probecalls;          /* ...and the CALLS those took */
static int h_r_pal;
static int h_r_hwpan;               /* VGA scrolls done by the CRTC alone */
static int h_r_vstart;              /* VGA: the CRTC Start Address points at
                                     * the WORLD rather than at VRAM 0 (see
                                     * lem_r_prep below) */
static int h_r_rects;               /* lem_r_rect CALLS since the last reset -
                                     * the SmallerC cdecl boundary is three
                                     * quarters of a minimap cell (SPEC.md
                                     * 116.7.1), so the call count is a cost in
                                     * its own right and not a proxy */
static unsigned char h_r_dirty[H_SCR_H];

static void h_r_reset(void)
{
    memset(h_wrote, 0, sizeof(h_wrote));
    h_r_pixels = 0;
    h_r_worldpix = 0;
    h_r_probes = 0;
    h_r_probecalls = 0;
    h_r_presents = 0;
    h_r_hwpan = 0;
    h_r_rects = 0;
}

static void h_scr(int x, int y, int c)
{
    if (x < 0 || x >= H_SCR_W || y < 0 || y >= H_SCR_H)
        return;
    h_screen[y][x] = (unsigned char)c;
    h_wrote[y][x]++;
    h_r_pixels++;
    if (y < H_SCR_H)
        h_r_dirty[y] = 1;
}

/* h_twice - how many screen pixels were written more than once since the last
 * reset. PERFORMANCE.md's double-draw flash, made countable. */
static int h_twice(void)
{
    int x, y, n = 0;

    for (y = 0; y < H_SCR_H; y++)
        for (x = 0; x < H_SCR_W; x++)
            if (h_wrote[y][x] > 1)
                n++;
    return n;
}

/* ============================================================================
 * lemmask.inc, modelled
 * ==========================================================================*/
static unsigned h_mask_seg;

void lem_m_setup(unsigned maskseg)
{
    h_mask_seg = maskseg;
    (void)h_ptr(maskseg, 0);            /* it has to be a live claim */
}

void lem_m_clear(void)
{
    memset(h_solid, 0, sizeof(h_solid));
}

int lem_has_pixel(int x, int y)
{
    h_r_probes++;
    h_r_probecalls++;
    if (x < 0 || x >= LEM_WORLD_W || y < 0 || y >= LEM_WORLD_H)
        return 0;
    return h_solid[y][x] ? 1 : 0;
}

void lem_set_pixel(int x, int y)
{
    if (x >= 0 && x < LEM_WORLD_W && y >= 0 && y < LEM_WORLD_H)
        h_solid[y][x] = 1;
}

void lem_clear_pixel(int x, int y)
{
    if (x >= 0 && x < LEM_WORLD_W && y >= 0 && y < LEM_WORLD_H)
        h_solid[y][x] = 0;
}

int lem_probe4(int x, int y, const char *offs)
{
    int i, r = 0, px, py;

    h_r_probecalls++;
    for (i = 0; i < 4; i++) {
        h_r_probes++;
        px = x + offs[2 * i];
        py = y + offs[2 * i + 1];
        if (px >= 0 && px < LEM_WORLD_W && py >= 0 && py < LEM_WORLD_H &&
            h_solid[py][px])
            r |= 1 << i;
    }
    return r;
}

void lem_m_span(int x, int y, int w, int set)
{
    int i;

    for (i = 0; i < w; i++)
        if (set)
            lem_set_pixel(x + i, y);
        else
            lem_clear_pixel(x + i, y);
}

void lem_m_apply(unsigned seg, unsigned off, int x, int y, int w, int h)
{
    int gx, gy, bpr = (w + 7) >> 3;
    const unsigned char *p;

    for (gy = 0; gy < h; gy++)
        for (gx = 0; gx < w; gx++) {
            p = h_ptr(seg, off + gy * bpr + (gx >> 3));
            /* A SET BIT REMOVES (Lemmix CombineMask, src/Game.pas:1812 - and
             * the data: the explosion stencil's set bits are the crater) */
            if ((*p >> (7 - (gx & 7))) & 1)
                lem_clear_pixel(x + gx, y + gy);
        }
}

int lem_m_dig(int x, int y, int w)
{
    int i, hit = 0;

    if (y < 0)
        y = 0;
    for (i = 0; i < w; i++)
        if (x + i >= 0 && x + i < LEM_WORLD_W && y < LEM_WORLD_H &&
            h_solid[y][x + i]) {
            h_solid[y][x + i] = 0;
            hit = 1;
        }
    return hit;
}

/* the OBJECT MAP, modelled INSIDE the claim the program hands it, so a write
 * the program makes outside its 15,840 bytes is a read-past-the-claim failure
 * here rather than a silent overwrite */
static unsigned h_o_seg;

void lem_o_setup(unsigned seg) { h_o_seg = seg; (void)h_ptr(seg, 15839); }

void lem_o_clear(void)
{
    int i;

    for (i = 0; i < 396 * 40; i++)
        *h_ptr(h_o_seg, (unsigned)i) = 128;
}

static int h_o_idx(int x, int y)
{
    int cx = x >> 2, cy = y >> 2;       /* an arithmetic shift: floor */

    if (x < 0)
        cx = -((-x + 3) >> 2);
    if (y < 0)
        cy = -((-y + 3) >> 2);
    if (cx < 0 || cx >= 396 || cy < 0 || cy >= 40)
        return -1;
    return cy * 396 + cx;
}

int lem_o_get(int x, int y)
{
    int i = h_o_idx(x, y);

    return i < 0 ? 128 : *h_ptr(h_o_seg, (unsigned)i);
}

void lem_o_set(int x, int y, int v)
{
    int i = h_o_idx(x, y);

    if (i >= 0)
        *h_ptr(h_o_seg, (unsigned)i) = (unsigned char)v;
}

void lem_o_fill(int x, int y, int w, int h, int v)
{
    int px, py;

    for (py = y; py < y + h; py++)
        for (px = x; px < x + w; px++)
            lem_o_set(px, py, v);
}

void lem_m_derive(unsigned dstseg, int kind)
{
    (void)dstseg;
    (void)kind;
    /* On the machine this writes the 1bpp/2bpp world claim out of the mask.
     * The model already HAS the world picture as bytes, so what it checks is
     * the invariant that makes the real one legal: every solid mask pixel is a
     * terrain-class pixel of the picture, and nothing else is. */
}

/* ============================================================================
 * lemblit.inc, modelled
 * ==========================================================================*/
/* h_vga_window - the world's current window into h_screen. See lem_r_present. */
static void h_vga_window(void)
{
    int y, i;

    for (y = 0; y < H_VIEW_H; y++)
        for (i = 0; i < H_SCR_W; i++)
            h_screen[y][i] = h_world[y][h_r_scroll + i];
}

static void h_cur_forget(void);

void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
                 unsigned shadowseg)
{
    (void)fbseg;
    (void)worldseg;
    (void)shadowseg;
    h_r_kind = kind;
    h_r_scroll = -1;
    h_r_vstart = 0;
    h_cur_forget();                 /* lb_curn0 = lb_curn1 = 0, as the asm */
}

/* WHAT THE MODEL CAN AND CANNOT SEE HERE, stated because this is where the
 * wave's worst defect lived and the harness did not catch it.
 *
 * On the machine _lem_r_prep() moves the CRTC Offset to 200 bytes a row, sets
 * the line compare and the pan mode, AND points the Start Address at the world
 * (LEM_VTERR). The first version did not do the last of those, so from the
 * mode set until the first lem_r_scroll() - which is in ovl_frame_first(),
 * AFTER the ~25-second compose - the card displayed VRAM 0 with a 200-byte
 * row: the skill panel twice and world x 0 under it
 * (build/port-shots/wave2f-vga-loading.png).
 *
 * THIS MODEL DOES NOT EXECUTE THE ASSEMBLY, so it cannot see a missing `out`
 * and the flag below is not evidence that the asm sets the register - that is
 * emulator-only evidence and lemblit.inc's header says so. What the flag DOES
 * catch is the ORDER on the C side: nothing may present a world frame before
 * lem_r_prep() has established the window it is presented in. */
void lem_r_prep(void)
{
    h_r_prepped = 1;
    if (h_r_kind == 0)
        h_r_vstart = 1;
}

void lem_r_unprep(void)
{
    h_r_prepped = 0;
    h_r_vstart = 0;
}

void lem_r_pal(unsigned seg, unsigned stdoff, unsigned cusoff)
{
    (void)h_ptr(seg, stdoff);
    (void)h_ptr(seg, cusoff + 23);
    h_r_pal = 1;
}

static int h_r_vclears;                 /* the VRAM half's runs */

void lem_r_clearworld(int vram)
{
    if (vram) {
        memset(h_world, 0, sizeof(h_world));
        h_r_vclears++;
    }
    memset(h_t, 0, sizeof(h_t));
}

/* --- wave 3's composer: T and the mask, in RAM (lemblit.inc lem_r_wpiece) ---
 * h_t is T, the terrain picture (VGA) - the background a destroyed or animated
 * rectangle is put back from. On VGA h_world is VRAM: T + objects + sprites.
 * On the shadow backends h_world is W: the mask by class + the objects. */
static unsigned h_wp0, h_wp3, h_tseg;

void lem_r_wsetup(unsigned p0, unsigned p3) { h_wp0 = p0; h_wp3 = p3; }
void lem_r_tsetup(unsigned t) { h_tseg = t; }

/* THE VGA COMPOSE (lemsprite.inc lem_r_cband): between lem_r_cband and
 * lem_r_cend the machine draws into a VRAM SCRATCH a band of the world's rows
 * high, and copies each rectangle on in one latch pass (lem_r_clatch). So the
 * model keeps the scratch as a world-sized picture of its own - h_cs - and a
 * compose writes THERE, never the world and never the screen; only a latch
 * moves pixels into the world, and the screen with it. The BAND clips a
 * sprite and the pointer (rows only); the CLIP rectangle clips an object, the
 * latch and the pointer's save update, which is the machine's arithmetic. */
static unsigned char h_cs[H_WORLD_H][H_WORLD_W];
static int h_comp, h_comp_y0, h_comp_h, h_comp_b0, h_comp_b1;
static int h_band_y0, h_band_h;
static int h_r_bgglass;             /* VGA lem_r_bgrect calls made straight
                                     * onto the glass, outside a compose */
static int h_r_comps;               /* bands and rectangles begun */
static int h_r_direct;              /* VGA sprite/object/background draws made
                                     * straight onto the glass, outside a
                                     * compose - the defect wave 3's second
                                     * review found: a lemming off the glass
                                     * from its restore to its draw */
static int h_r_latches;             /* lem_r_clatch calls */

/* h_wpx - one pixel of the VGA world, which IS the screen inside the view -
 * or, composing, one pixel of the scratch, inside the band */
static void h_wpx(int x, int y, int c)
{
    if (x < 0 || x >= H_WORLD_W || y < 0 || y >= H_WORLD_H)
        return;
    if (h_comp) {
        if (y < h_band_y0 || y >= h_band_y0 + h_band_h)
            return;                 /* the scratch holds the band and no more */
        h_cs[y][x] = (unsigned char)c;
        return;
    }
    h_world[y][x] = (unsigned char)c;
    h_r_worldpix++;
    if (h_r_kind == 0 && h_r_scroll >= 0 && x >= h_r_scroll &&
        x < h_r_scroll + H_SCR_W) {
        h_screen[y][x - h_r_scroll] = (unsigned char)c;
        h_wrote[y][x - h_r_scroll]++;
        h_r_pixels++;
    }
}

/* h_inclip - is world pixel (x, y) inside the compose's CLIP rectangle? */
static int h_inclip(int x, int y)
{
    return y >= h_comp_y0 && y < h_comp_y0 + h_comp_h &&
           (x >> 3) >= h_comp_b0 && (x >> 3) <= h_comp_b1;
}

void lem_r_wpiece(unsigned seg, unsigned off, int x, int y, int w, int h,
                  int flags)
{
    int gx, gy, dy, p, nib, m, bpr = (w + 7) >> 3, stride = (w >> 3) * h;
    const unsigned char *b;

    for (gy = 0; gy < h; gy++) {
        dy = (flags & 4) ? (y + h - 1 - gy) : (y + gy);
        if (dy < 0 || dy >= H_WORLD_H)
            continue;
        for (gx = 0; gx < w; gx++) {
            if (x + gx < 0 || x + gx >= H_WORLD_W)
                continue;
            b = h_ptr(seg, off + 3 * stride + gy * bpr + (gx >> 3));
            m = (*b >> (7 - (gx & 7))) & 1;   /* plane 3 IS the mask */
            if (!m)
                continue;
            /* Lemmix's precedence: no-overwrite, then erase, then draw */
            if (flags & 8) {
                if (h_solid[dy][x + gx])
                    continue;
            } else if (flags & 2) {
                h_solid[dy][x + gx] = 0;
                h_t[dy][x + gx] = 0;
                continue;
            }
            nib = 8;
            for (p = 0; p < 3; p++) {
                b = h_ptr(seg, off + p * stride + gy * bpr + (gx >> 3));
                if ((*b >> (7 - (gx & 7))) & 1)
                    nib |= 1 << p;
            }
            h_solid[dy][x + gx] = 1;
            if (h_wp0)
                h_t[dy][x + gx] = (unsigned char)nib;
            h_r_worldpix++;
        }
    }
}

void lem_r_tmask(void) { }          /* T's plane 3 is the nibble's bit 3 here */

void lem_r_tload(void)
{
    int x, y;

    if (h_r_kind != 0)
        return;
    for (y = 0; y < H_WORLD_H; y++)
        for (x = 0; x < H_WORLD_W; x++)
            h_world[y][x] = h_t[y][x];
    if (h_r_scroll >= 0)
        h_vga_window();
}

void lem_r_derive(void)
{
    int x, y;

    if (h_r_kind == 0)
        return;
    for (y = 0; y < H_WORLD_H; y++)
        for (x = 0; x < H_WORLD_W; x++)
            h_world[y][x] = h_solid[y][x] ? 8 : 0;
}

void lem_r_scroll(int x)
{
    int y, i;

    if (x < 0)
        x = 0;
    if (x > LEM_WORLD_W - 320)
        x = LEM_WORLD_W - 320;
    if (h_r_kind == 1)
        x &= ~3;                    /* CGA snaps to a byte: 4 pixels of 2bpp */
    else if (h_r_kind == 2)
        x &= ~7;                    /* Hercules: 8 pixels of 1bpp */
    if (x == h_r_scroll)
        return;                     /* the view did not move */
    h_r_scroll = x;

    /* A VGA SCROLL COSTS NO PIXELS AND THE MODEL HAS TO SAY SO. On the machine
     * it is four `out`s - the CRTC Start Address and the Attribute
     * Controller's pel pan - and not one byte of VRAM moves; on the two 1bpp
     * backends it is a 12,800-byte windowed copy into the shadow. The model
     * keeps the screen correct either way, but only the 1bpp arm goes through
     * h_scr(), so h_r_pixels counts what the machine would actually write and
     * "a VGA scroll writes no screen pixels" is an assertion rather than a
     * claim. */
    if (h_r_kind == 0) {
        h_r_hwpan++;
        h_vga_window();
        return;
    }
    for (y = 0; y < H_VIEW_H; y++)
        for (i = 0; i < H_SCR_W; i++)
            h_scr(i, y, h_world[y][x + i]);
}

void lem_r_dirty(int row, int b0, int b1)
{
    (void)b0;
    (void)b1;
    if (row >= 0 && row < H_SCR_H)
        h_r_dirty[row] = 1;
}

void lem_r_undirty(void)
{
    memset(h_r_dirty, 0, sizeof(h_r_dirty));
}

void lem_r_present(void)
{
    /* ON VGA THE SCREEN IS THE WORLD, LIVE. The machine does not COPY a window
     * out of the world - the world is video memory and the CRTC displays a
     * window of it, so anything composed after the scroll is on the glass the
     * instant it is written. A model that only refreshes h_screen inside
     * lem_r_scroll() gets that wrong the moment a caller scrolls BEFORE the
     * compose, which ovl_frame_panel() now does so that the level builds in
     * the window it will open in. */
    if (h_r_kind == 0 && h_r_scroll >= 0)
        h_vga_window();
    h_r_presents++;
    lem_r_undirty();
}

/* the buttons lb_msample latches inside a long compose or present: the model
 * has no long draws, so a row sets h_r_mlatch to stand for one */
static int h_r_mlatch;

int lem_r_mlatch(void)
{
    int v = h_r_mlatch;

    h_r_mlatch = 0;
    return v;
}

void lem_r_panel(unsigned seg, unsigned off)
{
    int x, y, p, nib;
    const unsigned char *b;

    for (y = 0; y < 40; y++)
        for (x = 0; x < 320; x++) {
            nib = 0;
            for (p = 0; p < 4; p++) {
                b = h_ptr(seg, off + p * 1600 + y * 40 + (x >> 3));
                if ((*b >> (7 - (x & 7))) & 1)
                    nib |= 1 << p;
            }
            h_scr(x, H_VIEW_H + y, nib);
        }
}

void lem_r_rect(int x, int y, int w, int h, int colour, int fill)
{
    int i, j;

    h_r_rects++;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (!fill && j != 0 && j != h - 1 && i != 0 && i != w - 1)
                continue;
            if (x + i < 0 || x + i >= 320 || y + j < 0 || y + j >= 40)
                continue;
            h_scr(x + i, H_VIEW_H + y + j, colour);
        }
}

/* lem_r_batch - the VGA state bracket, modelled as the DEPTH it is. The
 * assembly's version sets write mode 2 and the map mask once per run instead of
 * once per pixel; there is no such state here, so what the model checks is that
 * it is balanced - an unclosed bracket would leave the real machine in write
 * mode 2 and the next composer pass would write colours where it means bytes. */
static int h_r_batch;

void lem_r_batch(int on)
{
    if (on) {
        h_r_batch++;
        return;
    }
    if (h_r_batch == 0)
        fail("lem_r_batch(0) with no bracket open - the VGA would be left in "
             "write mode 2 for the composer");
    else
        h_r_batch--;
}

/* ============================================================================
 * lemsprite.inc, modelled (SPEC.md 116.4.6)
 * ==========================================================================*/

/* the records: the shadow backends' rectangles, put back from W. VGA keeps
 * none - its frame is composed (SPEC.md 116.4.6) */
#define H_SPMAX 168
unsigned char lem_sprec[H_SPMAX * 6];  /* lemsprite.inc's lb_sprec, which on
                                        * VGA holds the C's boxes */
static int h_sp_x[H_SPMAX], h_sp_y[H_SPMAX], h_sp_w[H_SPMAX], h_sp_h[H_SPMAX];
static int h_spn;
static int h_r_sprites;             /* lem_r_sprite calls since the reset */
static int h_r_objs;                /* lem_r_obj calls */
static int h_r_bgs;                 /* lem_r_bgrect calls */

/* h_sprite_pix - one source pixel's colour, or -1 transparent */
static int h_sprite_pix(unsigned seg, unsigned off, int w, int h, int np,
                        int ink, int gx, int gy)
{
    int p, c = 0, bpr = (w + 7) >> 3, ps = bpr * h;
    const unsigned char *b;

    for (p = 0; p < np; p++) {
        b = h_ptr(seg, off + p * ps + gy * bpr + (gx >> 3));
        if ((*b >> (7 - (gx & 7))) & 1)
            c |= 1 << p;
    }
    if (c == 0)
        return -1;
    return np == 1 ? ink : c;
}

void lem_r_sprite(unsigned seg, unsigned off, int x, int y, int w, int h,
                  int mode)
{
    int gx, gy, c, np = mode & 15, ink = mode >> 4, k;

    h_r_sprites++;
    if (h_r_kind == 0) {
        /* VGA: NO SAVE-UNDER AND NO RECORD - composed into the scratch, or
         * (the first frame, 'f') straight onto the world */
        if (!h_comp)
            h_r_direct++;
        for (gy = 0; gy < h; gy++)
            for (gx = 0; gx < w; gx++) {
                c = h_sprite_pix(seg, off, w, h, np, ink, gx, gy);
                if (c < 0 || y + gy < 0 || y + gy >= H_VIEW_H ||
                    x + gx >= 1600)
                    continue;
                h_wpx(x + gx, y + gy, c);
            }
        return;
    }
    if (h_spn >= H_SPMAX)
        return;
    if (w > 32 || h > 32) {
        fail("a sprite bigger than the record model holds");
        return;
    }
    k = h_spn++;
    h_sp_x[k] = x;
    h_sp_y[k] = y;
    h_sp_w[k] = w;
    h_sp_h[k] = h;
    for (gy = 0; gy < h; gy++)
        for (gx = 0; gx < w; gx++) {
            c = h_sprite_pix(seg, off, w, h, np, ink, gx, gy);
            if (c < 0 || y + gy < 0 || y + gy >= H_VIEW_H)
                continue;
            if (h_r_scroll >= 0)
                h_scr(x + gx - h_r_scroll, y + gy, 3);
        }
}

/* the shadow backends' records, put back out of W; VGA keeps none */
void lem_r_srest(void)
{
    int k, gx, gy, x, y;

    while (h_spn > 0) {
        k = --h_spn;
        for (gy = 0; gy < h_sp_h[k]; gy++)
            for (gx = 0; gx < h_sp_w[k]; gx++) {
                x = h_sp_x[k] + gx;
                y = h_sp_y[k] + gy;
                if (x < 0 || x >= H_WORLD_W || y < 0 || y >= H_VIEW_H)
                    continue;
                if (h_r_scroll >= 0 && x - h_r_scroll >= 0 &&
                    x - h_r_scroll < H_SCR_W)
                    h_scr(x - h_r_scroll, y, h_world[y][x]);
            }
    }
}

void lem_r_sforget(void) { h_spn = 0; }

/* lem_r_spray - one frame of the explosion's spray (lemsprite.inc): the 80
 * signed pairs at seg:off, either byte -128 not drawn, inside the C's box.
 * VGA writes the original's colour for p mod 16 into the scratch or the
 * world (h_wpx clips to the band as the machine does) and erases nothing;
 * the shadow cards draw the solid class and ERASE A WHOLE BYTE - four world
 * pixels - out of W, which is what lb_wcopy moves. */
static const unsigned char h_pcol[16] = {
    4, 15, 14, 13, 12, 11, 10, 9, 8, 11, 10, 9, 8, 7, 6, 2
};
static int h_r_sprays;              /* lem_r_spray calls */
static int h_r_spraypix;            /* ...and the particles they drew */
static int h_r_spraywalks;          /* ...of which the BIN walks (erase 2, 3):
                                     * 80 pairs each whatever the box holds,
                                     * the cost lemtest's nuke leg holds down */
static int h_r_sprayonebox;         /* NEGATIVE CONTROL: the bins answer the
                                     * whole box in every band they occupy -
                                     * the area the one-box-a-spray build
                                     * composed (lemtest spray_area) */

/* erase 2, 3: THE BINS (lemsprite.inc .bin) - ten 16-row bands of {byte lo,
 * byte hi, row lo, row hi} after the 80 boxes in the same records, lo 0xFF
 * empty; 2 empties them first. Nothing is drawn. */
#define H_PBINS (lem_sprec + 80 * 4)

void lem_r_spray(unsigned seg, unsigned off, int x, int y, int b0, int b1,
                 int r0, int r1, int erase)
{
    int p, dx, dy, px, py, sx, k;
    unsigned char *q;

    h_r_sprays++;
    if (b0 < 0 || b1 > 199 || r0 < 0 || r1 > H_VIEW_H - 1)
        fail("lem_r_spray was given a box outside the world - the C keeps it "
             "inside (ovl_pbox)");
    if (erase >= 2) {
        if (h_r_kind != 0) {
            /* the asm returns at the door; the C must never knock */
            fail("lem_r_spray's bin modes were called on a shadow card - "
                 "lb_pbins is that card's live sprite records (lemsprite.inc)");
            return;
        }
        h_r_spraywalks++;
        sx = 0;
        if (erase == 2)
            for (k = 0; k < 10; k++) {
                H_PBINS[k * 4] = 0xFF;
                H_PBINS[k * 4 + 1] = 0;
                H_PBINS[k * 4 + 2] = 0xFF;
                H_PBINS[k * 4 + 3] = 0;
            }
        for (p = 0; p < 80; p++) {
            dx = (signed char)*h_ptr(seg, off + (unsigned)p * 2);
            dy = (signed char)*h_ptr(seg, off + (unsigned)p * 2 + 1);
            if (dx == -128 || dy == -128)
                continue;
            px = x + dx;
            py = y + dy;
            if (py < r0 || py > r1 || px < 0 || (px >> 3) < b0 ||
                (px >> 3) > b1)
                continue;
            q = &H_PBINS[(py >> 4) * 4];
            if ((px >> 3) < q[0]) q[0] = (unsigned char)(px >> 3);
            if ((px >> 3) > q[1]) q[1] = (unsigned char)(px >> 3);
            if (py < q[2]) q[2] = (unsigned char)py;
            if (py > q[3]) q[3] = (unsigned char)py;
            sx = 1;                 /* a particle was binned */
        }
        if (h_r_sprayonebox && sx == 1)
            /* the one-box build: every band the box crosses, the box's
             * whole width - which sums to the box itself */
            for (k = r0 >> 4; k <= r1 >> 4; k++) {
                q = &H_PBINS[k * 4];
                if (b0 < q[0]) q[0] = (unsigned char)b0;
                if (b1 > q[1]) q[1] = (unsigned char)b1;
                dy = r0 > k * 16 ? r0 : k * 16;
                if (dy < q[2]) q[2] = (unsigned char)dy;
                dy = r1 < k * 16 + 15 ? r1 : k * 16 + 15;
                if (dy > q[3]) q[3] = (unsigned char)dy;
            }
        return;
    }
    if (h_r_kind == 0 && erase)
        return;
    if (h_r_kind == 0 && !h_comp)
        h_r_direct++;
    for (p = 0; p < 80; p++) {
        dx = (signed char)*h_ptr(seg, off + (unsigned)p * 2);
        dy = (signed char)*h_ptr(seg, off + (unsigned)p * 2 + 1);
        if (dx == -128 || dy == -128)
            continue;
        px = x + dx;
        py = y + dy;
        if (py < r0 || py > r1 || px < 0 || (px >> 3) < b0 || (px >> 3) > b1)
            continue;
        if (h_r_kind == 0) {
            h_wpx(px, py, h_pcol[p & 15]);
            h_r_spraypix++;
            continue;
        }
        sx = px - h_r_scroll;
        if (h_r_scroll < 0 || sx < 0 || sx >= H_SCR_W)
            continue;
        if (erase) {
            for (k = sx & ~3; k < (sx & ~3) + 4; k++)
                h_scr(k, py, h_world[py][h_r_scroll + k]);
        } else {
            h_scr(sx, py, 3);
            h_r_spraypix++;
        }
    }
}

int lem_r_cband(int y0, int h)
{
    if (h_r_kind != 0 || h <= 0 || h > 125)
        return 0;
    if (h_comp)
        fail("lem_r_cband inside a compose - the scratch is one band");
    if (y0 < 0 || y0 + h > H_VIEW_H)
        fail("a compose band outside the world's 160 rows");
    h_comp = 1;
    h_band_y0 = y0;
    h_band_h = h;
    h_comp_y0 = y0;
    h_comp_h = h;
    h_comp_b0 = 0;
    h_comp_b1 = 199;
    h_r_comps++;
    return 1;
}

int lem_r_cbegin(int y0, int h, int b0, int b1)
{
    if (!lem_r_cband(y0, h))
        return 0;
    h_comp_b0 = b0;
    h_comp_b1 = b1;
    return 1;
}

void lem_r_cclip(int y0, int h, int b0, int b1)
{
    if (!h_comp)
        fail("lem_r_cclip outside a compose");
    if (y0 < h_band_y0 || y0 + h > h_band_y0 + h_band_h)
        fail("a compose clip reaches outside its band - the latch would "
             "copy scratch rows nothing composed");
    h_comp_y0 = y0;
    h_comp_h = h;
    h_comp_b0 = b0;
    h_comp_b1 = b1;
}

void lem_r_clatch(void)
{
    int x, y;

    if (!h_comp)
        return;
    h_r_latches++;
    for (y = h_comp_y0; y < h_comp_y0 + h_comp_h; y++)
        for (x = h_comp_b0 * 8; x < h_comp_b1 * 8 + 8; x++) {
            if (x < 0 || x >= H_WORLD_W || y < 0 || y >= H_VIEW_H)
                continue;
            h_world[y][x] = h_cs[y][x];
            if (h_r_scroll >= 0 && x >= h_r_scroll &&
                x < h_r_scroll + H_SCR_W) {
                h_screen[y][x - h_r_scroll] = h_cs[y][x];
                h_wrote[y][x - h_r_scroll]++;
                h_r_pixels++;
            }
        }
}

void lem_r_cend(void) { h_comp = 0; }

/* lem_r_shit - does a SHADOW record cross the screen rectangle? The model's
 * records are pixel rectangles; the machine's are whole bytes, so the model
 * widens by a byte and answers yes at least as often. VGA keeps none. */
int lem_r_shit(int sx0, int sy0, int sx1, int sy1)
{
    int k, x0, x1;

    for (k = 0; k < h_spn; k++) {
        x0 = h_sp_x[k] - h_r_scroll - 8;
        x1 = h_sp_x[k] + h_sp_w[k] - h_r_scroll + 8;
        if (x0 <= sx1 && sx0 <= x1 && h_sp_y[k] <= sy1 &&
            sy0 <= h_sp_y[k] + h_sp_h[k] - 1)
            return 1;
    }
    return 0;
}

/* lem_r_vstart / lem_r_vpan - the VGA scroll in two halves: the model's view
 * moves on the first and the second is the pel pan, which writes nothing */
static int h_r_vpans;

void lem_r_vstart(int x)
{
    if (x < 0)
        x = 0;
    if (x > LEM_WORLD_W - 320)
        x = LEM_WORLD_W - 320;
    if (h_r_kind != 0)
        fail("lem_r_vstart on a shadow backend - it is the CRTC's");
    h_r_scroll = x;
    h_r_hwpan++;
    h_vga_window();
}

void lem_r_vpan(void) { h_r_vpans++; }

/* the background rectangle: VGA syncs T with the mask (a lost pixel cleared,
 * a gained one a colour-7 brick) and copies T in; the shadow re-derives W */
void lem_r_bgrect(int x, int y, int w, int h, int sync)
{
    int gx, gy, x0, x1;

    h_r_bgs++;
    if (h_r_kind == 0 && !h_comp && sync != 2) {
        h_r_bgglass++;
        h_r_direct++;
    }
    x0 = x & ~7;
    x1 = (x + w + 7) & ~7;
    for (gy = y; gy < y + h; gy++) {
        if (gy < 0 || gy >= H_VIEW_H)
            continue;
        for (gx = x0; gx < x1; gx++) {
            if (gx < 0 || gx >= H_WORLD_W)
                continue;
            if (h_r_kind == 0) {
                if (sync) {
                    if (!h_solid[gy][gx])
                        h_t[gy][gx] = 0;
                    else if (h_t[gy][gx] == 0)
                        h_t[gy][gx] = 7;
                }
                if (sync != 2)      /* 2: the sync alone */
                    h_wpx(gx, gy, h_t[gy][gx]);
            } else {
                h_world[gy][gx] = h_solid[gy][gx] ? 8 : 0;
            }
        }
    }
}

void lem_r_show(int x, int y, int w, int h)
{
    int gx, gy;

    if (h_r_kind == 0 || h_r_scroll < 0)
        return;
    for (gy = y; gy < y + h; gy++)
        for (gx = x; gx < x + w; gx++)
            if (gy >= 0 && gy < H_VIEW_H && gx >= h_r_scroll &&
                gx < h_r_scroll + H_SCR_W && gx < H_WORLD_W)
                h_scr(gx - h_r_scroll, gy, h_world[gy][gx]);
}

void lem_r_obj(unsigned seg, unsigned off, int x, int y, int w, int h,
               int flags)
{
    int gx, gy, sy, c, p, m, bpr = (w + 7) >> 3, ps = bpr * h;
    const unsigned char *b;

    h_r_objs++;
    if (h_r_kind == 0 && !h_comp)
        h_r_direct++;
    for (gy = 0; gy < h; gy++) {
        sy = (flags & 2) ? h - 1 - gy : gy;
        for (gx = 0; gx < w; gx++) {
            if (x + gx < 0 || x + gx >= H_WORLD_W || y + gy < 0 ||
                y + gy >= H_VIEW_H)
                continue;
            b = h_ptr(seg, off + 4 * ps + sy * bpr + (gx >> 3));
            m = (*b >> (7 - (gx & 7))) & 1;
            if (!m)
                continue;
            if ((flags & 1) && !h_solid[y + gy][x + gx])
                continue;
            if (!(flags & 1) && (flags & 4) && h_solid[y + gy][x + gx])
                continue;
            c = 0;
            for (p = 0; p < 4; p++) {
                b = h_ptr(seg, off + p * ps + sy * bpr + (gx >> 3));
                if ((*b >> (7 - (gx & 7))) & 1)
                    c |= 1 << p;
            }
            if (h_r_kind == 0) {
                if (!h_comp || h_inclip(x + gx, y + gy))
                    h_wpx(x + gx, y + gy, c);
            } else {
                h_world[y + gy][x + gx] = (unsigned char)c;
            }
        }
    }
}

/* the pointer: saved under on every card, the world part on VGA in the
 * world at the view's offset and the panel part on the screen.
 *
 * THE SAVE-UNDER IS WHOLE BYTES, AS THE MACHINE'S IS (wave 6, carried finding
 * A). lemsprite.inc lem_r_cursor clips the 2-byte bitmap with lb_clipx, which
 * always adds the TAIL byte the shift can spill into - so the world piece and
 * the panel piece each save and restore THREE planar bytes a row (24 pixels
 * from x & ~7) on VGA, and the shadow piece FIVE 2bpp bytes (20 pixels from
 * x & ~3) on the two shadow cards, whichever pixels of the 14x14 shape are
 * lit. The model used to save only the lit pixels, so a compose that brought
 * the save-under up to date over the 14-pixel box and not over the tail byte
 * (lemdraw.c ovl_vframe's lem_cmv rectangle) left the tail column of the save
 * stale and the harness could not see it: restored on the glass it is an
 * 8x14 block of the wrong picture, which wave 5's verifier found on VGA when
 * the pointer left the world for the panel. */
#define H_CSW 24                    /* the widest piece, in pixels */
static unsigned char h_cur_save[14 * H_CSW];   /* the world / shadow piece */
static unsigned char h_cur_psave[14 * H_CSW];  /* the VGA panel piece */
static int h_cur_wy0, h_cur_wn, h_cur_wx0, h_cur_ww;  /* rows y0.., n of them;
                                                       * columns x0.., w */
static int h_cur_py0, h_cur_pn, h_cur_px0, h_cur_pw;
static int h_cur_x = -1000, h_cur_y, h_cur_on;
static int h_cur_sc;                /* the scroll it was drawn at: the asm takes
                                     * it off at the VRAM offset it saved
                                     * (lb_cur0), not at the view's new one */
static int h_r_curdraws;
static const unsigned char *h_cur_last;

/* the pointer's two planes (lemdraw.c lem_cur0): p0 | p1 << 1, opaque
 * where either is set */
static int h_cur_col(const unsigned char *bits, int gx, int gy)
{
    int k = gy * 2 + (gx >> 3), sh = 7 - (gx & 7);

    return ((bits[k] >> sh) & 1) | (((bits[28 + k] >> sh) & 1) << 1);
}

static int h_cur_bits_was(int gx, int gy)
{
    return h_cur_last && h_cur_col(h_cur_last, gx, gy) != 0;
}

/* VGA draws the colour; the shadow cards their solid class (3) */
static int h_cur_ink(int gx, int gy)
{
    return h_r_kind == 0 ? h_cur_col(h_cur_last, gx, gy) : 3;
}

/* h_floor - a floor division by a power of two, for negative x too (the asm's
 * `sar`) */
static int h_floor(int x, int n)
{
    return x >= 0 ? x / n : -((-x + n - 1) / n);
}

/* h_cur_span - one piece's rectangle, the asm's arithmetic: rows y0..y0+13
 * clipped to [r0, r1), whole bytes of ppb pixels from floor(x / ppb), `nb` of
 * them, clipped to [0, cols). Answers 0 when nothing of it is on. */
static int h_cur_span(int x, int y0, int r0, int r1, int ppb, int nb, int cols,
                      int *py0, int *pn, int *px0, int *pw)
{
    int a = h_floor(x, ppb) * ppb, e = a + nb * ppb, b = y0, f = y0 + 14;

    if (b < r0)
        b = r0;
    if (f > r1)
        f = r1;
    if (a < 0)
        a = 0;
    if (e > cols)
        e = cols;
    if (b >= f || a >= e)
        return 0;
    *py0 = b;
    *pn = f - b;
    *px0 = a;
    *pw = e - a;
    return 1;
}

/* h_cur_place - both pieces' rectangles for a pointer at screen (sx, sy) */
static void h_cur_place(int sx, int sy)
{
    h_cur_wn = h_cur_pn = 0;
    if (h_r_kind == 0) {
        if (h_r_scroll >= 0 &&
            !h_cur_span(h_r_scroll + sx, sy, 0, H_VIEW_H, 8, 3, H_WORLD_W,
                        &h_cur_wy0, &h_cur_wn, &h_cur_wx0, &h_cur_ww))
            h_cur_wn = 0;
        if (!h_cur_span(sx, sy, H_VIEW_H, H_SCR_H, 8, 3, H_SCR_W, &h_cur_py0,
                        &h_cur_pn, &h_cur_px0, &h_cur_pw))
            h_cur_pn = 0;
    } else if (!h_cur_span(sx, sy, 0, H_SCR_H, 4, 5, H_SCR_W, &h_cur_wy0,
                           &h_cur_wn, &h_cur_wx0, &h_cur_ww)) {
        h_cur_wn = 0;
    }
}

/* h_cur_forget - nothing of the pointer is on a glass a mode set has just
 * cleared (lemblit.inc lem_r_setup) */
static void h_cur_forget(void)
{
    h_cur_wn = h_cur_pn = 0;
}

void lem_r_cursor(const unsigned char *bits, int sx, int sy)
{
    int gx, gy;

    if (h_cur_on) {
        /* ...the whole saved span goes back, lit pixels or not */
        for (gy = 0; gy < h_cur_wn; gy++)
            for (gx = 0; gx < h_cur_ww; gx++) {
                if (h_r_kind == 0)
                    h_wpx(h_cur_wx0 + gx, h_cur_wy0 + gy,
                          h_cur_save[gy * H_CSW + gx]);
                else
                    h_scr(h_cur_wx0 + gx, h_cur_wy0 + gy,
                          h_cur_save[gy * H_CSW + gx]);
            }
        for (gy = 0; gy < h_cur_pn; gy++)
            for (gx = 0; gx < h_cur_pw; gx++)
                h_scr(h_cur_px0 + gx, h_cur_py0 + gy,
                      h_cur_psave[gy * H_CSW + gx]);
        h_cur_wn = h_cur_pn = 0;
        h_cur_on = 0;
    }
    if (bits == 0)
        return;
    h_r_curdraws++;
    h_cur_last = bits;
    h_cur_place(sx, sy);
    for (gy = 0; gy < h_cur_wn; gy++)
        for (gx = 0; gx < h_cur_ww; gx++)
            h_cur_save[gy * H_CSW + gx] =
                h_r_kind == 0 ? h_world[h_cur_wy0 + gy][h_cur_wx0 + gx]
                              : h_screen[h_cur_wy0 + gy][h_cur_wx0 + gx];
    for (gy = 0; gy < h_cur_pn; gy++)
        for (gx = 0; gx < h_cur_pw; gx++)
            h_cur_psave[gy * H_CSW + gx] =
                h_screen[h_cur_py0 + gy][h_cur_px0 + gx];
    for (gy = 0; gy < 14; gy++)
        for (gx = 0; gx < 14; gx++) {
            int x = sx + gx, y = sy + gy;

            if (!h_cur_col(bits, gx, gy) || x < 0 || x >= H_SCR_W || y < 0 ||
                y >= H_SCR_H)
                continue;
            if (h_r_kind == 0 && y < H_VIEW_H) {
                if (h_r_scroll >= 0)
                    h_wpx(h_r_scroll + x, y, h_cur_ink(gx, gy));
            } else {
                h_scr(x, y, h_cur_ink(gx, gy));
            }
        }
    h_cur_sc = h_r_scroll;
    h_cur_x = sx;
    h_cur_y = sy;
    h_cur_on = 1;
}

/* lem_r_cplace - the place and the shape, set and NOT drawn: nothing taken
 * off the glass and nothing saved (the VGA compose rebuilds the old box and
 * lem_r_csave / lem_r_cdraw do the rest, lemsprite.inc). The world piece's
 * rectangle is placed and its save is NOT touched - so whatever the compose
 * does not bring up to date stays what it was, as on the machine; there is no
 * panel piece (the caller keeps both boxes in the world). */
void lem_r_cplace(const unsigned char *bits, int sx, int sy)
{
    h_cur_last = bits;
    h_cur_place(sx, sy);
    h_cur_pn = 0;
    h_cur_sc = h_r_scroll;
    h_cur_x = sx;
    h_cur_y = sy;
    h_cur_on = 1;
}

/* lem_r_csave - the CLIP's share of the lit pointer's world piece - every
 * pixel of its whole-byte span, lit or not - saved again out of the scratch
 * (the picture the glass is about to hold) */
void lem_r_csave(void)
{
    int gx, gy, x, y;

    if (!h_comp || !h_cur_on || h_r_kind != 0)
        return;
    for (gy = 0; gy < h_cur_wn; gy++)
        for (gx = 0; gx < h_cur_ww; gx++) {
            x = h_cur_wx0 + gx;
            y = h_cur_wy0 + gy;
            if (!h_inclip(x, y))
                continue;
            h_cur_save[gy * H_CSW + gx] = h_cs[y][x];
        }
}

/* lem_r_cdraw - the lit pointer drawn INTO the scratch, inside the band */
void lem_r_cdraw(void)
{
    int gx, gy;

    if (!h_comp || !h_cur_on || h_r_kind != 0)
        return;
    for (gy = 0; gy < 14; gy++)
        for (gx = 0; gx < 14; gx++)
            if (h_cur_bits_was(gx, gy) && h_cur_x + gx >= 0 &&
                h_cur_x + gx < H_SCR_W && h_cur_y + gy >= 0 &&
                h_cur_y + gy < H_VIEW_H)
                h_wpx(h_r_scroll + h_cur_x + gx, h_cur_y + gy,
                      h_cur_ink(gx, gy));
}

void lem_r_glyph8(const unsigned char *bits, int x, int y, int h, int ink,
                  int paper)
{
    int gx, gy;

    for (gy = 0; gy < h; gy++)
        for (gx = 0; gx < 8; gx++)
            h_scr(x + gx, H_VIEW_H + y + gy,
                  ((bits[gy] >> (7 - gx)) & 1) ? ink : paper);
}

void lem_r_panrect(unsigned seg, unsigned off, int x, int y, int w, int h)
{
    int gx, gy, p, nib;
    const unsigned char *b;

    for (gy = y; gy < y + h; gy++)
        for (gx = x; gx < x + w; gx++) {
            if (gy != y && gy != y + h - 1 && gx != x && gx != x + w - 1)
                continue;
            nib = 0;
            for (p = 0; p < 4; p++) {
                b = h_ptr(seg, off + p * 1600 + gy * 40 + (gx >> 3));
                if ((*b >> (7 - (gx & 7))) & 1)
                    nib |= 1 << p;
            }
            h_scr(gx, H_VIEW_H + gy, nib);
        }
}

/* ============================================================================
 * lemfont.inc, modelled - and the INDEX MAP is the part worth modelling
 * ==========================================================================*/
int lem_f_index(int ch)
{
    if (ch >= 'a' && ch <= 'z')
        ch -= 32;
    if (ch == '%')
        return 0;
    if (ch >= '0' && ch <= '9')
        return ch - '0' + 1;
    if (ch == '-')
        return 11;
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 12;
    return -1;                      /* the fourth arm: an 8x16 black cell */
}

void lem_f_cell(unsigned seg, unsigned off, int col, int row, int ch)
{
    int gx, gy, p, nib, g = lem_f_index(ch);
    const unsigned char *b;

    for (gy = 0; gy < 16; gy++)
        for (gx = 0; gx < 8; gx++) {
            nib = 0;
            if (g >= 0)
                for (p = 0; p < 3; p++) {
                    b = h_ptr(seg, off + g * 48 + p * 16 + gy);
                    if ((*b >> (7 - gx)) & 1)
                        nib |= 1 << p;
                }
            h_scr(col * 8 + gx, H_VIEW_H + row + gy, nib);
        }
}

void lem_f_run(unsigned seg, unsigned off, int col, int row, const char *s,
               int n)
{
    int i;

    for (i = 0; i < n; i++) {
        lem_f_cell(seg, off, col + i, row, (s && s[i]) ? s[i] : 0);
        if (s && !s[i])
            s = 0;
    }
}

/* ============================================================================
 * WAVE 5: the two screens' surface (lemfont.inc lem_p_*), modelled
 *
 * In the ORIGINAL'S coordinates, 640x350, one byte a pixel: 0 black, 1 the
 * brown tile (a pixel the mono cards light), 2 the picture, 3 the load bar,
 * 0x40 + n a purple glyph of character n. What is asserted from it is the
 * layout - which row each line sits on, how wide it is, that nothing is
 * lettered on a row the card does not show - and that every byte the assembly
 * would read is inside a live claim (h_ptr).
 * ==========================================================================*/
#define H_P_W 640
#define H_P_H 350
static unsigned char h_p[H_P_H][H_P_W];
static int h_p_kind = -1;
static int h_p_h;                       /* the rows this card shows */
static int h_p_clears, h_p_tiles, h_p_rects, h_p_minis, h_p_pals;
static int h_p_texts, h_p_glyphs;
static int h_p_lowest;                  /* the lowest glyph row lettered + 1 */
static int h_p_clipped;                 /* glyph rows the card does not show */
static int h_p_wide;                    /* glyphs past column 639 */
#define H_P_LINES 24
static char h_p_line[H_P_LINES][48];    /* this screen's text calls, in order */
static int  h_p_lx0[H_P_LINES], h_p_lx1[H_P_LINES], h_p_ly[H_P_LINES];
static int  h_p_nl;

void lem_p_setup(int kind, unsigned fbseg)
{
    (void)fbseg;
    h_p_kind = kind;
    h_p_h = kind == 2 ? 348 : kind == 1 ? 200 : 350;
}

static int h_p_clrows;                  /* the surface rows cleared */
static int h_p_margins;                 /* VGA margin clears (wave 6: only
                                         * after a mode set that happened) */

void lem_p_clear(int y0, int y1, int margins)
{
    int y;

    if (margins && h_p_kind == 0)
        h_p_margins++;
    for (y = y0; y < y1 && y < H_P_H; y++)
        if (y >= 0) {
            memset(h_p[y], 0, H_P_W);
            h_p_clrows++;
        }
    h_p_clears++;
    h_p_nl = 0;
    h_p_lowest = 0;
}

void lem_p_rect(int x, int y, int w, int h, int colour)
{
    int gx, gy;

    h_p_rects++;
    if ((x & 7) || (w & 7))
        fail("lem_p_rect was given an x or a width that is not byte aligned");
    for (gy = y; gy < y + h; gy++)
        for (gx = x; gx < x + w; gx++)
            if (gy >= 0 && gy < h_p_h && gx >= 0 && gx < H_P_W)
                h_p[gy][gx] = colour ? 3 : 0;
}

void lem_p_tile(unsigned seg, unsigned off, int y0, int y1)
{
    int gx, gy;

    h_p_tiles++;
    (void)h_ptr(seg, off + 2 * (320 * 104 / 8) - 1);
    for (gy = y0; gy < y1 && gy < h_p_h; gy++)
        for (gx = 0; gx < H_P_W; gx++)
            h_p[gy][gx] = 1;
}

void lem_p_text(unsigned seg, unsigned off, int x, int y, const char *s,
                int n)
{
    int i, gx, gy, c, k;

    h_p_texts++;
    (void)h_ptr(seg, off + 94 * 96 - 1);
    if (x & 7)
        fail("lem_p_text was given an x that is not a multiple of 8");
    k = h_p_nl < H_P_LINES ? h_p_nl++ : H_P_LINES - 1;
    for (i = 0; i < n && i < 47 && s[i]; i++)
        h_p_line[k][i] = s[i];
    h_p_line[k][i] = 0;
    h_p_lx0[k] = -1;
    h_p_lx1[k] = -1;
    h_p_ly[k] = y;
    for (i = 0; i < n && s[i]; i++, x += 16) {
        c = (unsigned char)s[i];
        if (c == ' ')
            continue;
        if (c < '!' || c > '~') {
            x -= 16;                /* advances nothing (DrawPurpleText) */
            continue;
        }
        if (x < 0 || x > 624) {
            h_p_wide++;
            continue;
        }
        h_p_glyphs++;
        if (h_p_lx0[k] < 0)
            h_p_lx0[k] = x;
        h_p_lx1[k] = x + 15;
        for (gy = y; gy < y + 16; gy++) {
            if (gy < 0 || gy >= h_p_h) {
                h_p_clipped++;
                continue;
            }
            if (gy + 1 > h_p_lowest)
                h_p_lowest = gy + 1;
            for (gx = x; gx < x + 16; gx++)
                h_p[gy][gx] = (unsigned char)(0x40 + (c & 0x3F));
        }
    }
}

/* one row of the picture: world row srow, 2:5 across (lemfont.inc lp_samp) */
void lem_p_mini(int y, int srow)
{
    int gx;

    h_p_minis++;
    if (srow < 0 || srow >= LEM_WORLD_H)
        fail("lem_p_mini was asked for a world row outside the world");
    if (y < 0 || y >= h_p_h)
        return;
    for (gx = 0; gx < H_P_W; gx++)
        h_p[y][gx] = ((gx * 5) >> 1) < LEM_WORLD_W &&
                     h_solid[srow < 0 ? 0 : srow % LEM_WORLD_H][(gx * 5) >> 1]
                     ? 2 : 0;
}

void lem_p_pal(unsigned mseg, unsigned moff, unsigned bseg, unsigned coff)
{
    h_p_pals++;
    (void)h_ptr(mseg, moff + 47);
    (void)h_ptr(bseg, coff + 23);
}

/* h_p_find - the text call whose line starts with `what`, or -1 */
static int h_p_find(const char *what)
{
    int k;

    for (k = 0; k < h_p_nl; k++)
        if (strncmp(h_p_line[k], what, strlen(what)) == 0)
            return k;
    return -1;
}
