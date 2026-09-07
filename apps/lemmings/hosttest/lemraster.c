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
 * SEGMENT, and lem_far() does real paragraph arithmetic on one to reach past
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
 * with every Disk window and driver already open (SPEC.md 92.6.1). This
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

int os88_peek(unsigned seg, unsigned off) { return *h_ptr(seg, off); }
void os88_poke(unsigned seg, unsigned off, int v) { *h_ptr(seg, off) = (unsigned char)v; }

/* --- the modelled glass ---------------------------------------------------- */
#define H_WORLD_W 1600
#define H_WORLD_H  160
#define H_SCR_W    320
#define H_SCR_H    200
#define H_VIEW_H   160

static unsigned char h_solid[H_WORLD_H][H_WORLD_W];
static unsigned char h_world[H_WORLD_H][H_WORLD_W];
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
                                     * 92.7.1), so the call count is a cost in
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
            /* A SET BIT MEANS LEAVE ALONE (Lemmings.ts mask-provider Mask.at) */
            if (((*p >> (7 - (gx & 7))) & 1) == 0)
                lem_clear_pixel(x + gx, y + gy);
        }
}

void lem_m_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags)
{
    int gx, gy, dy, bpr = (w + 7) >> 3;
    const unsigned char *p;

    for (gy = 0; gy < h; gy++) {
        dy = (flags & 4) ? (y + h - 1 - gy) : (y + gy);
        for (gx = 0; gx < w; gx++) {
            p = h_ptr(seg, off + gy * bpr + (gx >> 3));
            if (((*p >> (7 - (gx & 7))) & 1) == 0)
                continue;
            if (flags & 2)
                lem_clear_pixel(x + gx, dy);
            else
                lem_set_pixel(x + gx, dy);
        }
    }
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

void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
                 unsigned shadowseg)
{
    (void)fbseg;
    (void)worldseg;
    (void)shadowseg;
    h_r_kind = kind;
    h_r_scroll = -1;
    h_r_vstart = 0;
}

/* WHAT THE MODEL CAN AND CANNOT SEE HERE, stated because this is where the
 * wave's worst defect lived and the harness did not catch it.
 *
 * On the machine _lem_r_prep() moves the CRTC Offset to 200 bytes a row, sets
 * the line compare and the pan mode, AND points the Start Address at the world
 * (LEM_VTERR). The first version did not do the last of those, so from the
 * mode set until the first lem_r_scroll() - which is in lem_frame_first(),
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

void lem_r_clearworld(void)
{
    memset(h_world, 0, sizeof(h_world));
}

void lem_r_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags)
{
    int gx, gy, dy, p, nib, bpr = (w + 7) >> 3, stride = (w >> 3) * h;
    const unsigned char *b;

    for (gy = 0; gy < h; gy++) {
        dy = (flags & 4) ? (y + h - 1 - gy) : (y + gy);
        if (dy < 0 || dy >= H_WORLD_H)
            continue;
        for (gx = 0; gx < w; gx++) {
            b = h_ptr(seg, off + 3 * stride + gy * bpr + (gx >> 3));
            if (((*b >> (7 - (gx & 7))) & 1) == 0)
                continue;           /* plane 3 IS the transparency mask */
            if (x + gx < 0 || x + gx >= H_WORLD_W)
                continue;
            if ((flags & 8) && h_solid[dy][x + gx])
                continue;           /* no-overwrite, against the mask as it is
                                     * BEFORE this piece - which is why the C
                                     * calls lem_r_piece before lem_m_piece */
            if (flags & 2) {
                h_world[dy][x + gx] = 0;
                h_r_worldpix++;
                continue;
            }
            nib = 8;                /* plane 3 is set, so the nibble is 8..15 */
            for (p = 0; p < 3; p++) {
                b = h_ptr(seg, off + p * stride + gy * bpr + (gx >> 3));
                if ((*b >> (7 - (gx & 7))) & 1)
                    nib |= 1 << p;
            }
            h_world[dy][x + gx] = (unsigned char)nib;
            h_r_worldpix++;
        }
    }
}

void lem_r_derive(void) { }

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
     * compose, which lem_frame_panel() now does so that the level builds in
     * the window it will open in. */
    if (h_r_kind == 0 && h_r_scroll >= 0)
        h_vga_window();
    h_r_presents++;
    lem_r_undirty();
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
