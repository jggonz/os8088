/* ============================================================================
 * os8088 - tests/lembench/lembench.c      THE LEMMINGS RASTER'S BENCH (SPEC.md 92.7)
 *
 * PERFORMANCE.md rule 4: measure before redesigning, and a counter is not a
 * timer. This is the timer. It puts MICROSECONDS on the glass for every
 * primitive apps/lemmings/lemblit.inc, lemmask.inc and lemfont.inc define -
 * on whichever backend the machine it is running on uses - so that wave 3's
 * frame budget is arithmetic over measured numbers rather than a guess.
 *
 * IT TIMES THE SHIPPING CODE. tests/lembench/lembench.asm %includes the package's
 * own three .inc files rather than carrying a copy, so the routine this bench
 * measures is the routine that ships (WEAVE-SPEC 1.2's rule, and the same
 * reason). What it synthesises is only the DATA: a 32x16 terrain piece and a
 * 38-glyph font, poked into a claim, so the bench needs no converted band and
 * runs on a disk with two files on it.
 *
 * THE CLOCK IS THE BIOS TICK, 18.2 Hz, AND THAT IS ENOUGH. Each row runs its
 * primitive enough times to take several ticks and divides; a tick is 54,945
 * microseconds, so a row that takes 10 ticks over 20,000 iterations is 27 us
 * an iteration with the quantisation error under 5%. Programming the PIT would
 * buy a decimal place this bench has no use for - what it is measuring is
 * whether a probe is 20 us or 200.
 *
 * IT RUNS INSIDE SPEC.md 53's BRACKET, because half of what it times is VRAM
 * and port I/O and neither exists outside one. The results are stored in bss
 * and LETTERED IN THE WINDOW afterwards, which is why this file needs no font
 * of its own: a kernel drawing slot is refused in there and legal out here.
 *
 * WHAT THE NUMBERS ARE NOT. There are no lemmings in this build - the eighteen
 * action handlers and the sprite path are wave 3 - so the per-frame row is the
 * SCROLL AND THE PRESENT and says so. A frame with sixty lemmings on it is
 * that number plus sixty times the sprite row, and the sprite row arrives with
 * the sprites.
 * ==========================================================================*/

#include "os88.h"

/* the raster, declared exactly as apps/lemmings/lemmings.c declares it */
void lem_m_setup(unsigned maskseg);
void lem_m_clear(void);
int  lem_has_pixel(int x, int y);
void lem_set_pixel(int x, int y);
int  lem_probe4(int x, int y, const char *offs);
void lem_m_span(int x, int y, int w, int set);
void lem_m_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags);
void lem_m_derive(unsigned dstseg, int kind);
void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
                 unsigned shadowseg);
void lem_r_prep(void);
void lem_r_unprep(void);
void lem_r_clearworld(void);
void lem_r_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags);
void lem_r_scroll(int x);
void lem_r_present(void);
void lem_r_rect(int x, int y, int w, int h, int colour, int fill);
void lem_f_run(unsigned seg, unsigned off, int col, int row, const char *s,
               int n);

static void *lb_win;
static struct os88_pt lb_org;
static struct os88_size lb_sz;

/* the claims: the mask, a scratch that holds the synthetic piece and the font,
 * and - on the two 1bpp backends - the world and the shadow */
static unsigned lb_mask, lb_data, lb_world, lb_shadow;
static int lb_kindnow;
static int lb_ready;

#define LB_ROWS 12
static unsigned lb_ticks[LB_ROWS];
static unsigned lb_iters[LB_ROWS];
static const char *lb_name[LB_ROWS];
static int lb_n;

/* The synthetic data, poked into lb_data:
 *   0     a 32x16 terrain piece, four planes of 64 bytes, plane 3 solid
 *   256   38 glyphs of 8x16 3bpp - 1,824 bytes of alternating bits
 * Both are the SHAPE the real bank has; what they carry is this file's own. */
#define LB_PIECE_OFF   0
#define LB_FONT_OFF  256
#define LB_PIECE_W    32
#define LB_PIECE_H    16

static const char lb_off4[8] = { 0, 0, 1, 0, 2, 0, 3, 0 };
static const char lb_field[6] = { '1', '2', '3', '4', '5', 0 };

/* --- the clock -------------------------------------------------------------
 * os88_ticks() is the 18.2 Hz BIOS tick. Waiting for it to CHANGE before
 * starting is what removes the half-tick the first sample would otherwise
 * carry, which on a ten-tick row is 5%. */
static unsigned lb_t0;

static void lb_start(void)
{
    unsigned t = os88_ticks();

    while (os88_ticks() == t)
        ;
    lb_t0 = os88_ticks();
}

static void lb_stop(const char *name, unsigned iters)
{
    if (lb_n >= LB_ROWS)
        return;
    lb_ticks[lb_n] = os88_ticks() - lb_t0;
    lb_iters[lb_n] = iters;
    lb_name[lb_n] = name;
    lb_n++;
}

/* --- the bench itself, inside the bracket ----------------------------------
 *
 * IT SIZES ITS OWN ROWS. A row runs its primitive `n` times, and if that took
 * fewer than LB_MINTICKS ticks it runs again with eight times as many - up to
 * three doublings. That is what makes one bench readable on a 4.77 MHz 8088
 * AND on the machine that is a thousand times faster: a fixed iteration count
 * is either minutes on the target or zero ticks under QEMU, and the first
 * version of this file was both (build/port-shots/wave2-lembench-vga.png reads
 * "0 us" on every row but one).
 * ==========================================================================*/
#define LB_MINTICKS 6
#define LB_GROW     3

static void lb_do(int row, unsigned n)
{
    unsigned i, j;

    switch (row) {
    case 0:                                     /* one probe */
        for (i = 0; i < n; i++)
            lem_has_pixel(100 + (int)(i & 255), 40 + (int)(i & 63));
        break;
    case 1:                                     /* four, batched */
        for (i = 0; i < n; i++)
            lem_probe4(100 + (int)(i & 255), 40 + (int)(i & 63), lb_off4);
        break;
    case 2:                                     /* a terrain piece -> mask */
        for (i = 0; i < n; i++)
            lem_m_piece(lb_data, LB_PIECE_OFF, (int)(i & 511), (int)(i & 127),
                        LB_PIECE_W, LB_PIECE_H, 0);
        break;
    case 3:                                     /* ...and -> the picture */
        for (i = 0; i < n; i++)
            lem_r_piece(lb_data, LB_PIECE_OFF, (int)(i & 511), (int)(i & 127),
                        LB_PIECE_W, LB_PIECE_H, 0);
        break;
    case 4:                                     /* the whole world derived */
        for (i = 0; i < n; i++)
            lem_m_derive(lb_world, lb_kindnow - 1);
        break;
    case 5:                                     /* a scroll step */
        for (i = 0; i < n; i++)
            lem_r_scroll((int)((i & 63) * 4));
        break;
    case 6:                                     /* ...and the present with it */
        for (i = 0; i < n; i++) {
            lem_r_scroll((int)((i & 63) * 4));
            lem_r_present();
        }
        break;
    case 7:                                     /* one minimap cell, BARE */
        for (i = 0; i < n; i++)
            lem_r_rect(208 + (int)(i % 104), 19 + (int)(i % 18), 1, 1, 7, 1);
        break;
    case 8:                                     /* ...and 104 of them BATCHED */
        for (i = 0; i < n; i++) {
            lem_r_batch(1);
            for (j = 0; j < 104; j++)
                lem_r_rect(208 + (int)j, 19 + (int)(i % 18), 1, 1, 7, 1);
            lem_r_batch(0);
        }
        break;
    case 9:                                     /* the view rectangle's OUTLINE
                                                 * - two 18-cell columns and
                                                 * two 25-cell rows, which is
                                                 * what a move draws */
        for (i = 0; i < n; i++)
            lem_r_rect(208 + (int)(i % 79), 18, 25, 20, 3, 0);
        break;
    default:                                    /* a five-cell status field */
        for (i = 0; i < n; i++)
            lem_f_run(lb_data, LB_FONT_OFF, 18, 0, lb_field, 5);
        break;
    }
}

static void lb_row(int row, const char *name, unsigned n)
{
    int grow;

    for (grow = 0; grow <= LB_GROW; grow++) {
        lb_start();
        lb_do(row, n);
        if (os88_ticks() - lb_t0 >= LB_MINTICKS || grow == LB_GROW)
            break;
        if (n > 4000u)
            break;                  /* the count is an unsigned and 500 << 9
                                     * is not: 256,000 wraps to 59,392 and the
                                     * row would divide by the wrong number */
        /* THE MULTIPLY IS A SHIFT AND IT HAS TO BE WRITTEN AS ONE. tools/
         * cc8086.py refuses `n *= 8` here - "an 8086 multiply-by-constant
         * costs a shift/add chain, which writes FLAGS, and FLAGS is live" -
         * because the loop's own test is what follows it (LESSONS.md 3). */
        n = n << 3;
    }
    lb_stop(name, n);
}

void os88_fsx_main(void *win)
{
    static struct os88_fsi fsi;
    int mode;

    (void)win;
    mode = OS88_FSXM_CGA320;
    if (lb_kindnow == 0)
        mode = OS88_FSXM_VGA0D;
    else if (lb_kindnow == 2)
        mode = OS88_FSXM_HERC;
    if (os88_fsx_mode(mode, &fsi) != 0)
        return;

    lem_r_setup(lb_kindnow, fsi.seg, lb_world, lb_shadow);
    lem_r_prep();
    lem_m_setup(lb_mask);
    lem_m_clear();
    lem_r_clearworld();

    /* 0-1  THE PROBE, single and batched. This pair is the number the whole of
     *      lemmask.inc's design rests on: sixty lemmings at six probes is 360
     *      of them a tick, and a tick is 54,945 us. */
    lb_row(0, "probe, one", 500);
    lb_row(1, "probe, four batched", 500);

    /* 2-3  A TERRAIN PIECE, into the mask and into the picture. 32x16 is 64
     *      source bytes, which is the five sets' 273 pieces to within a factor
     *      of two, and a level is up to 400 of them. On the 1bpp backends row
     *      3 is nothing at all - their whole world is row 4. */
    lb_row(2, "terrain piece -> mask", 100);
    lb_row(3, "terrain piece -> picture", 50);

    /* 4    THE 1bpp/2bpp WORLD out of the mask, 32,000 bytes in. Once per
     *      level, and nothing on VGA. */
    if (lb_kindnow != 0)
        lb_row(4, "world derive (32,000 B)", 4);

    /* 5-6  THE PER-FRAME COST. Four `out`s on VGA and a 12,800-byte compose
     *      plus a blit on the other two. THERE ARE NO LEMMINGS IN THIS BUILD:
     *      a frame with sixty of them is this plus sixty times the sprite row,
     *      and the sprite row arrives with the sprites (wave 3). */
    lb_row(5, "scroll step", 200);
    lb_row(6, "scroll + present", 100);

    /* 7-10 THE PANEL'S LIVE THINGS. Row 7 is ONE minimap cell with no bracket
     *      around it - the worst case, and the number wave 2's review found
     *      the status line and the view rectangle were both built on. Row 8 is
     *      the same cell inside one lem_r_batch(), which is how lem_mm_row()
     *      actually calls it: the VGA's write mode and map mask are set once
     *      for the run instead of once a pixel, so the two rows together are
     *      the batch's whole worth. Row 9 is the VIEW RECTANGLE's outline -
     *      2 x 18 column cells and 2 x 25 row cells, which is what a scroll
     *      step redraws - and row 10 the five-cell status field an ordinary
     *      tick letters (the OUT count, the IN count and the clock). */
    lb_row(7, "minimap cell, bare", 200);
    lb_row(8, "minimap row, 104 cells", 20);
    lb_row(9, "view rect outline", 100);
    lb_row(10, "status field, 5 cells", 20);

    lem_r_unprep();
}

/* --- the window ----------------------------------------------------------- */
static char lb_line[72];
static char lb_num[8];

static void lb_cat(char *dst, const char *s, unsigned cap)
{
    unsigned i = 0, j = 0;

    while (dst[i] && i < cap - 1)
        i++;
    while (s[j] && i < cap - 1)
        dst[i++] = s[j++];
    dst[i] = 0;
}

static void lb_pad(char *dst, unsigned to, unsigned cap)
{
    unsigned i = 0;

    while (dst[i])
        i++;
    while (i < to && i < cap - 1)
        dst[i++] = ' ';
    dst[i] = 0;
}

/* lb_muldiv - a * b / d in SIXTEEN BITS, which is the whole arithmetic this
 * dialect has (docs/C-TOOLCHAIN.md: no long, no float).
 *
 * The obvious form overflows and the first version of this file shipped it:
 * `ticks * 54945 / iters` for 14 ticks over 4,000 iterations is 769,230 as a
 * numerator, which wrapped and printed 12 us for a probe that is 192
 * (build/port-shots/wave2-lembench-overflow.png - every row wrong and every
 * one plausible, which is the worst way for a number to be wrong).
 *
 * So `b` is reduced by `d` FIRST and accumulated `a` times: the quotient adds
 * b/d each pass and the remainder carries b%d, which is strictly less than d -
 * so the running remainder never exceeds 2d and the widest intermediate is
 * bounded by the ITERATION COUNT rather than by the product. `a` is the tick
 * count, so the loop is a few hundred passes at print time and none at all
 * while anything is being measured. */
static unsigned lb_muldiv(unsigned a, unsigned b, unsigned d)
{
    unsigned i, q, r, bq, br;

    if (d == 0)
        return 0;
    bq = b / d;
    br = b - bq * d;
    q = 0;
    r = 0;
    for (i = 0; i < a; i++) {
        q += bq;
        r += br;
        if (r >= d) {
            r -= d;
            q++;
        }
    }
    return q;
}

/* lb_calc - the per-iteration cost, in MICROSECONDS or - past a whole tick
 * each - in MILLISECONDS, because 393 ms does not fit sixteen bits of
 * microseconds and a bench that cannot say its slowest number is no bench. */
static unsigned lb_val;
static int lb_isms;

static void lb_calc(unsigned ticks, unsigned iters)
{
    lb_isms = 0;
    lb_val = 0;
    if (iters == 0)
        return;
    if (ticks >= iters) {
        lb_isms = 1;
        lb_val = lb_muldiv(ticks, 55u, iters);
        return;
    }
    lb_val = lb_muldiv(ticks, 54945u, iters);
}

static void lb_paint(void)
{
    int i, y;

    os88_wm_content(lb_win, &lb_org);
    if (os88_wm_geom(lb_win, &lb_sz) != 0)
        return;
    os88_set_color(OS88_BLACK);

    lb_line[0] = 0;
    lb_cat(lb_line, "LEMBENCH - the raster on this machine, ", sizeof(lb_line));
    lb_cat(lb_line, lb_kindnow == 0 ? "VGA mode 0Dh"
                  : (lb_kindnow == 2 ? "Hercules" : "CGA mode 4"),
           sizeof(lb_line));
    os88_font_run(lb_org.x + 2, lb_org.y + 2, lb_line, OS88_BLACK, OS88_WHITE);

    y = lb_org.y + 2 + 18;
    for (i = 0; i < lb_n; i++) {
        lb_line[0] = 0;
        lb_cat(lb_line, lb_name[i], sizeof(lb_line));
        lb_pad(lb_line, 26, sizeof(lb_line));
        lb_calc(lb_ticks[i], lb_iters[i]);
        os88_utoa(lb_val, lb_num);
        lb_cat(lb_line, lb_num, sizeof(lb_line));
        lb_cat(lb_line, lb_isms ? " ms  (" : " us  (", sizeof(lb_line));
        os88_utoa(lb_iters[i], lb_num);
        lb_cat(lb_line, lb_num, sizeof(lb_line));
        lb_cat(lb_line, " in ", sizeof(lb_line));
        os88_utoa(lb_ticks[i], lb_num);
        lb_cat(lb_line, lb_num, sizeof(lb_line));
        lb_cat(lb_line, " ticks)", sizeof(lb_line));
        os88_font_run(lb_org.x + 2, y, lb_line, OS88_BLACK, OS88_WHITE);
        y += 9;
    }
    if (!lb_ready) {
        os88_font_run(lb_org.x + 2, y + 9, "Press SPACE to run the bench.",
                      OS88_BLACK, OS88_WHITE);
    }
}

void os88_paint(void *win)
{
    (void)win;
    lb_paint();
}

void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii != ' ')
        return;
    lb_n = 0;
    os88_fullscreen(win, 1);
    os88_fsx_run(win, 0);
    os88_fullscreen(win, 0);
    lb_ready = 1;
}

/* --- the entry proc ------------------------------------------------------- */
static void lb_poke_data(void)
{
    unsigned i;

    /* a 32x16 piece: four planes of 64 bytes. Planes 0..2 alternate and plane
     * 3 - the transparency mask, and the high colour bit - is solid, so every
     * pixel of it is drawn and the mask arm has the most work it can have. */
    for (i = 0; i < 64 * 3; i++)
        os88_poke(lb_data, LB_PIECE_OFF + i, (int)((i & 1) ? 0xAA : 0x55));
    for (i = 0; i < 64; i++)
        os88_poke(lb_data, LB_PIECE_OFF + 192 + i, 0xFF);
    /* 38 glyphs of 8x16, three planes of 16 bytes */
    for (i = 0; i < 38u * 48u; i++)
        os88_poke(lb_data, LB_FONT_OFF + i, (int)((i & 1) ? 0x3C : 0x18));
}

void *os88_main(void)
{
    static struct os88_video vid;
    static unsigned char kind;
    int caps;

    lb_win = os88_wm_create(40, 40, 448, 160, "LEMBENCH");
    if (lb_win == 0)
        return 0;
    os88_video(&vid);
    kind = (unsigned char)vid.kind;
    caps = os88_fsx_caps(lb_win, &kind);
    lb_kindnow = 1;
    if (kind == OS88_VID_HERC)
        lb_kindnow = 2;
    else if (caps > 0 && (caps & (1 << OS88_FSXM_VGA0D)))
        lb_kindnow = 0;

    lb_mask = os88_mem_claim(32);
    lb_data = os88_mem_claim(4);
    if (lb_kindnow != 0) {
        lb_world = os88_mem_claim(lb_kindnow == 1 ? 63 : 32);
        lb_shadow = os88_mem_claim(16);
    }
    if (lb_mask == 0 || lb_data == 0)
        return lb_win;
    lb_poke_data();
    return lb_win;
}
