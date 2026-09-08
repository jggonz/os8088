/* ============================================================================
 * os8088 - apps/c64/c64scr.c        the screen: dirty pages, shadow, flush
 *
 * Part of C64 (C64-SPEC §9). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. What the
 * VIC-II puts on a screen is src/vicii/vicii-draw.c and viciitypes.h; the 16
 * colours are data/C64/vice.vpl. Nothing of VICE's source is vendored.
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * THE MODEL, IN ONE PARAGRAPH
 * ----------------------------------------------------------------------------
 * A shadow of the screen matrix and colour RAM cannot see most of what a C64
 * draws: bitmap data, a RAM character set and sprite bytes all change with no
 * screen code, colour nibble or VIC register changing. So the CORE does the
 * tracking - one bit into a 256-page dirty bitmap on every RAM write (9.2) -
 * and the flush maps those pages through the frame registers onto the cell
 * rows they feed, composes only those rows, compares each composed row
 * against an 8,000-byte 1bpp FRAME SHADOW (9.3) and draws only the differing
 * spans. Because the shadow is PIXELS, it validates the composer as well as
 * the model: if the composed row equals the shadow, nothing is drawn,
 * whatever the model believed.
 *
 * ----------------------------------------------------------------------------
 * THE WRITE WINDOW - a wave-1 refinement, amended into C64-SPEC §9.2
 * ----------------------------------------------------------------------------
 * A 256-byte page is 6.4 character rows, so a page-granular bitmap alone can
 * only ever say "recompose these seven rows, all forty cells" - and 9.7's
 * "one changed cell composes one cell" is then unreachable by construction.
 * So the core keeps TWO MORE WORDS in its scratch beside the bitmap: the
 * LOWEST and HIGHEST addresses written since the last flush. Four more
 * instructions on the write path, four bytes of scratch, and the flush
 * intersects that window with each dirty row's forty matrix bytes to get a
 * CELL span. A one-byte poke then composes one cell, and a scroll still
 * composes everything, which is the honest cost of each.
 *
 * ----------------------------------------------------------------------------
 * MONOCHROME, BY RELATIVE LUMINANCE (9.6)
 * ----------------------------------------------------------------------------
 * 1bpp on EVERY adapter, VGA included - a priced fact, not a shortcut:
 * SPEC.md 5.4.1 keeps the span writer on VGA, so gfx_blit4 prices a band by
 * its colour RUNS at ~215 us each and a C64 text band is a thousand of them.
 *
 * The threshold is RELATIVE and that matters more than it sounds. A C64 comes
 * up light blue on blue - colours 14 and 6, luminance 105 and 45 - and an
 * ABSOLUTE threshold at 128 would resolve both to black and put a blank
 * screen on the glass. Comparing the two puts white text on black, which is
 * what the machine is showing. A cell whose ink and paper have EQUAL
 * luminance is drawn uniform, which is also what the machine is showing.
 * ==========================================================================*/

/* --- what the composer needs from the machine ---------------------------- */
#define C64_BSTRIDE 40                      /* = c64band.inc's, one band row */
#define C64_X2STRIDE 80                     /* ...and c64band.inc's doubled one
                                             * (9.8). Both mirror a NASM equ in
                                             * apps/c64/c64band.inc, which is
                                             * the file that reads them */
#define C64_SHBYTES 8000                    /* 320 x 200 bits (9.3) */
/* The shift test is worth asking when MOST of the rows on the glass are
 * dirty - four fifths of them, which is the 20-of-25 measured in wave 1 and
 * is written as a FRACTION rather than a count because `nrows` is not 25 on
 * every adapter: wm_fit clamps the window on a 200-line desktop, and an
 * absolute 20 there is a threshold the screen can never reach, so no scroll
 * is ever detected on the target machine. See c64_flush. */
#define C64_SHIFT_NUM 4
#define C64_SHIFT_DEN 5

static unsigned char c64_sh[C64_SHBYTES];   /* the frame shadow: the pixels
                                             * last blitted, not a model */
static unsigned char c64_bnd[C64_BSTRIDE * 8];  /* one composed band */
/* ...AND ITS PIXEL-DOUBLED TWIN, WHICH IS THE WHOLE OF FULLSCREEN'S 2x (9.8).
 * The frame shadow stays 320x200 and every compare, signature and span is
 * still in C64 pixels: the doubling happens at BLIT TIME and nowhere else, so
 * nothing above this line knows the picture is twice the size. 1,280 bytes,
 * and they are bss for the life of the app rather than a claim because the
 * flush cannot refuse. */
static unsigned char c64_bx2[C64_X2STRIDE * 16];
/* GLASS pixels per C64 cell: 8 or 16 a side (9.8's tier table). EVERY
 * cell<->pixel conversion in this file and in c64.c reads these two and not
 * the literal 8, which is the whole mechanism. c64_can2x is the TIER's answer
 * (c64_tier_init) and c64_no2x is blit1's. */
static int c64_scw = 8, c64_sch = 8;
static int c64_can2x, c64_no2x;
static unsigned c64_sig[C64_ROWS];          /* this frame's row signatures */
static unsigned c64_shsig[C64_ROWS];        /* ...and the shadow's */
static int c64_sig_ok;                      /* c64_sig[] was filled THIS flush */

/* ----------------------------------------------------------------------------
 * TWO FLAGS THAT MEAN TWO DIFFERENT THINGS, AND THE FIRST DRAFT CONFLATED THEM
 * ----------------------------------------------------------------------------
 * c64_rowd says THE SOURCES CHANGED and the row wants recomposing:
 *      0  clean
 *      1  something inside the write window's span moved (a cell or a few)
 *      2  recompose the WHOLE row - a frame register moved, so every cell of
 *         it may decode differently and the write window says nothing at all
 * c64_force says THE GLASS IS UNKNOWN over cells c64_fc0..c64_fc1, so whatever
 * the shadow claims, those pixels are drawn: a scroll's vacated rows, a
 * partial expose, the rect an About panel just stopped covering.
 *
 * Setting BOTH for a register write - which is what c64_dirty_all used to do -
 * turns every write to $D011, $D016, $D018 or CIA2 $DD00 into 25 forced
 * full-width blits, ~234 ms, with the frame compare that exists to answer
 * "nothing changed, draw nothing" switched off. A register write needs the
 * recompose and not the force: compose the row, then ASK the shadow.
 * -------------------------------------------------------------------------*/
static unsigned char c64_rowd[C64_ROWS];    /* 0 clean / 1 span / 2 whole row */
static unsigned char c64_force[C64_ROWS];   /* the glass is unknown here... */
static unsigned char c64_fc0[C64_ROWS];     /* ...over these cells, first */
static unsigned char c64_fc1[C64_ROWS];     /*    ...and last */

/* c64_blit_said - the one-shot that names the fact when OSAPI_GFX_BLIT1
 * refuses (os88.h:446 - a broken argument, or a kern_small kernel that
 * carries the slot without the body). The refusal itself is not a dead end:
 * 9.5 gives a SECOND PATH through the kernel's own face (c64_row_font), so
 * the row is drawn once and never retried, and the fact goes out on both
 * routes - the toast and the status row (9.8). */
static int c64_blit_said;
static char c64_fbuf[C64_COLS + 1];         /* the fallback's one row of text */

/* the rows an About panel owns: os88_paint sets them and the flush composes
 * and blits NOTHING in them, so nothing under the panel is drawn and then
 * covered (PERFORMANCE.md rule 2). Empty is r0 > r1. */
static int c64_hold_r0 = 1, c64_hold_r1;

/* the frame registers, resolved once per flush */
static unsigned c64_mbase;                  /* the screen matrix */
static unsigned c64_chr_seg;                /* the character generator's */
static unsigned c64_chr_off;                /*   segment and offset */
static unsigned c64_chr_lo, c64_chr_hi;     /* ...and its RAM range, if RAM */
static int c64_chr_isram;
static int c64_mode;                        /* 0 = standard text (5.1) */
static int c64_border_lit;

/* --- the cost counters the harness prints (9.7) --------------------------
 * THEY ARE NOT IN THE SHIPPING IMAGE. Nothing in apps/c64/*.c reads one:
 * hosttest/c64uitest.c does, and it compiles this same C with -DC64_HOST
 * (apps/c64/build.sh). Left unguarded they were ~14 bytes of bss and
 * seventeen `inc word [mem]` in the flush's own path, on a package measured
 * against 61,440 - and neither apps/cword nor apps/runcpm ships any such
 * counter. Every increment below is guarded the same way, and the SmallerC
 * build fails loudly on any that is missed. */
#ifdef C64_HOST
static unsigned c64_n_blit, c64_n_fill, c64_n_scroll, c64_n_cell;
static unsigned c64_n_dirtypg, c64_n_rows, c64_n_x2;
#endif

/* --- the status row's last state, for the delta draw (10) ---------------- */
static int c64_st_joy1, c64_st_joy2;        /* the direction masks last drawn */
static int c64_st_flags;                    /* warp | pause << 1, last drawn */
static int c64_st_shown;                    /* a message was on the row */
static int c64_st_pct = -1;                 /* the two speed figures last */
static int c64_st_fps = -1;                 /*   DRAWN - the row delta-draws */
static char c64_st_cpu[14];                 /* `%7.0f` - the NUMBER only; the
                                             * `% cpu` tail is a constant and
                                             * is drawn once, with the row */
static char c64_st_fpsb[14];                /* `%8.1f`, and ` fps` likewise */
static char c64_st_cpup[14];                /* ...and what is ON THE GLASS, so
                                             * a second only redraws the cells
                                             * that actually changed */
static char c64_st_fpsp[14];
static int c64_st_ok;                       /* the row on the glass is ours */
/* ...AND TWO MORE, BECAUSE A MESSAGE DOES NOT NECESSARILY TOUCH THE WHOLE ROW
 * (see c64_status). The row has two halves - the two SPEED fields at cells
 * 0..24, and the JOYSTICK/drive/lamp widgets from cell 25 on - and a message
 * of 25 cells or fewer stands on the first and not the second:
 *
 *   c64_st_ok     the row's pixels are OURS. Cleared by the three events that
 *                 make them unknown: an expose, a damage rect that reaches
 *                 this row, and c64_sh_inval. NOT by a message.
 *   c64_st_lok    the LEFT half says what we last put there - which a new
 *                 message makes false and nothing else does, so that is what
 *                 c64_say clears.
 *   c64_st_blank  a LONG message is standing where the widgets go, so the
 *                 right half is blank-and-known. It is what tells the flush
 *                 after that message comes down to rebuild them. */
static int c64_st_lok;
static int c64_st_blank;

/* ==========================================================================
 * THE VIC-II's OWN LUMINANCE LADDER (src/vicii/vicii-color.c)
 *
 * THE FIRST DRAFT CITED data/C64/vice.vpl AND VICE DOES NOT USE IT. An
 * external palette file is loaded only when `${CHIP}ExternalPalette` is on,
 * and that resource factory-defaults to 0 (src/video/video-resources.c:393);
 * `pepto-pal` is only the NAME it would take (src/vicii/vicii-resources.c:170).
 * By default VICE GENERATES the palette from a built-in table, and for a PAL
 * C64 - VICII_MODEL_6569, chosen at vicii-color.c:601 for MACHINE_SYNC_PAL -
 * the table the shipped tree compiles is `vicii_colors_6569r5`
 * (vicii-color.c:441), selected at vicii-color.c:672 inside `#ifdef
 * TOBIAS_COLORS`, which is the one of the four palette switches vicii-color.c
 * leaves DEFINED (:52; PEPTO_COLORS, COLODORE_COLORS and MARKO_LUMAS are all
 * commented out at :43-49, which takes the `vicii_colors_6569r3` table and
 * the switch at :651 out of the build entirely).
 *
 * Its first column is the Y level, 0.0..1.0, x 256. Below it is x 255 with
 * white pinned at 255. The order is the C64's: 0 black, 1 white, 2 red,
 * 3 cyan, 4 purple, 5 green, 6 blue, 7 yellow, 8 orange, 9 brown,
 * 10 light red, 11 dark grey, 12 medium grey, 13 light green, 14 light blue,
 * 15 light grey.
 *
 * AND THE REAL CHIP'S LADDER HAS NINE LEVELS FOR SIXTEEN COLOURS, SHARED IN
 * SEVEN PAIRS: blue == brown (0.237), red == dark grey (0.306), purple ==
 * orange (0.363), medium grey == light blue (0.461), green == light red
 * (0.500), cyan == light grey (0.639), yellow == light green (0.763). The
 * .vpl-derived table this file used to carry made all sixteen DISTINCT -
 * yellow 212 against light green 185, cyan 171 against light grey 165 - so
 * the mono glass showed contrast in seven places where a real C64 and VICE
 * show a flat field, and c64_lum_update's `the same colour: a uniform cell`
 * branch below was unreachable code written for exactly this case.
 *
 * The EGA-16 map is kept beside it although nothing reads it yet: the day a
 * kernel blit1 variant takes an ink and a paper (9.6, 15.3), a colour C64
 * band costs exactly what the 1bpp band costs and this table is what it
 * needs. Keeping it is what makes that a drop-in rather than a re-plan.
 * ========================================================================*/
static const unsigned char c64_lum[16] = {
    0, 255, 78, 163, 93, 128, 60, 195, 93, 60, 128, 78, 118, 195, 118, 163
};

static const unsigned char c64_ega[16] = {
    OS88_BLACK,   OS88_WHITE,    OS88_RED,      OS88_LCYAN,
    OS88_MAGENTA, OS88_GREEN,    OS88_BLUE,     OS88_YELLOW,
    OS88_BROWN,   OS88_BROWN,    OS88_LRED,     OS88_DGRAY,
    OS88_LGRAY,   OS88_LGREEN,   OS88_LBLUE,    OS88_LGRAY
};

/* c64_lum_update - rebuild the 16 {and, xor} pairs c64band.inc reads, and the
 * uniform level a mode wave 1 cannot compose falls back to. Called at reset
 * and on every write to $D021 (c64io.c). */
static void c64_lum_update(void)
{
    int fg, bg, lb, wasborder;

    wasborder = c64_border_lit;
    bg = c64_vic[0x21] & 15;
    lb = c64_lum[bg];
    for (fg = 0; fg < 16; fg++) {
        if (c64_lum[fg] > lb) {
            c64_cmask[fg * 2] = 0xFF;       /* ink lit, paper dark */
            c64_cmask[fg * 2 + 1] = 0x00;
        } else if (c64_lum[fg] < lb) {
            c64_cmask[fg * 2] = 0xFF;       /* paper lit, ink dark */
            c64_cmask[fg * 2 + 1] = 0xFF;
        } else {
            c64_cmask[fg * 2] = 0x00;       /* the same colour: a uniform cell,
                                             * which an XOR alone cannot say */
            c64_cmask[fg * 2 + 1] = (unsigned char)(lb >= 128 ? 0xFF : 0x00);
        }
    }
    c64_bgfill = (unsigned char)(lb >= 128 ? 0xFF : 0x00);
    c64_border_lit = c64_lum[c64_vic[0x20] & 15] > lb;
    /* THE BORDER IS FOUR FILLS AND IT IS REDRAWN ONLY WHEN IT WOULD LOOK
     * DIFFERENT. c64_border paints the strips c64_border_lit and nothing
     * else, so a $D020 or $D021 write that leaves that ONE BIT alone - colour
     * 14 to 15 over background 6 is still `lighter than the paper` - changes
     * nothing on the glass and used to cost 3.0 ms of fills anyway. This is
     * the only place that raises the flag from a register write; a partial
     * expose raises it from c64_blank_rect and an invalid shadow makes
     * c64_flush draw the border without any flag at all. */
    if (c64_border_lit != wasborder)
        c64_border_dirty = 1;
}

/* ==========================================================================
 * DIRTINESS
 * ========================================================================*/
static void c64_row_dirty(int row)
{
    if (row >= 0 && row < C64_ROWS) {
        if (c64_rowd[row] == 0)
            c64_rowd[row] = 1;              /* a span; 2 is not narrowed to 1 */
        c64_dirty_any = 1;
    }
}

/* c64_dirty_all - THE SOURCES changed for every row (a frame register, a
 * block move, a bank switch). It does NOT force: the rows are recomposed and
 * then compared against the shadow, which is what makes a $D011 write that
 * writes the same picture cost nothing to draw. */
static void c64_dirty_all(void)
{
    int i;
    for (i = 0; i < C64_ROWS; i++)
        c64_rowd[i] = 2;
    c64_dirty_any = 1;
    /* AND IT DOES NOT TOUCH THE BORDER. $D011, $D016, $D018 and the CIA2 bank
     * bits all reach here and not one of them can change what the border
     * looks like; a smooth-scroll program writing $D016 once a frame paid
     * c64_border's fills every frame for nothing. c64_lum_update raises the
     * flag when the border's own level flips, and c64_flush draws the border
     * unconditionally while !c64_sh_ok, which is the case c64_sh_inval wants. */
}

/* c64_force_rows - THE GLASS is unknown over rows r0..r1, cells c0..c1. */
static void c64_force_rows(int r0, int r1, int c0, int c1)
{
    int i;
    if (r0 < 0)
        r0 = 0;
    if (r1 >= C64_ROWS)
        r1 = C64_ROWS - 1;
    if (c0 < 0)
        c0 = 0;
    if (c1 >= C64_COLS)
        c1 = C64_COLS - 1;
    for (i = r0; i <= r1; i++) {
        if (c64_force[i]) {                 /* two exposes before one flush:
                                             * the union, not the last one */
            if (c0 < (int)c64_fc0[i])
                c64_fc0[i] = (unsigned char)c0;
            if (c1 > (int)c64_fc1[i])
                c64_fc1[i] = (unsigned char)c1;
        } else {
            c64_force[i] = 1;
            c64_fc0[i] = (unsigned char)c0;
            c64_fc1[i] = (unsigned char)c1;
        }
        c64_dirty_any = 1;
    }
}

static void c64_sh_inval(void)
{
    c64_sh_ok = 0;
    c64_st_ok = 0;
    c64_dirty_all();
    c64_force_rows(0, C64_ROWS - 1, 0, C64_COLS - 1);
}

/* c64_dirty_scan - read the core's write window and its 256-page bitmap, mark
 * the cell rows they feed, and CLEAR both under the same lock so nothing is
 * lost between a write and a read of them (9.2).
 *
 * THE MATRIX ROWS ARE THE WINDOW **INTERSECTED WITH** THE PAGES, AND THE
 * INTERSECTION IS THE POINT. A 256-byte page is 6.4 character rows, so the
 * bitmap alone marks seven rows for one changed cell and 9.7's "one changed
 * cell composes one cell" is unreachable by construction. But the window
 * alone is ONE lo/hi pair over the whole matrix, so two pokes in different
 * rows - a score at row 0 and a status line at row 24, which is what a game
 * and the KERNAL both do inside one flush interval - span every row between
 * them and 25 rows recompose 1000 cells for two changed bytes.
 *
 * Row i's forty matrix bytes lie in at most TWO 256-byte pages, so a row is
 * dirty only when the window's row range covers it AND one of its own two
 * pages has its bit set. The two distant pokes then cost the seven rows of
 * page 4 and the six of page 7 rather than all 25, and one changed cell still
 * costs one cell because the window narrows the SPAN inside the row
 * (c64_span_of). Neither structure can do it alone.
 *
 * The page bitmap also sees the sources the window cannot: a RAM character
 * set today, bitmap and sprite data in wave 4. */
static unsigned c64_wlo, c64_whi;
/* THIS FLUSH'S PAGE BITMAP AND WRITE WINDOW, TAKEN IN ONE CALL.
 * dst[0..31] is the bitmap, dst[32..35] the window's two words - the layout
 * c64_dirty_take (c64mem.inc) fills, and the reason it exists: the same 36
 * bytes used to come back through ~50 near calls into the scratch, ~1.8 ms a
 * flush on the target, spent before a single pixel is decided. */
static unsigned char c64_dpg[36];

static int c64_pg_dirty(unsigned page)
{
    return (c64_dpg[page >> 3] >> (7 - (page & 7))) & 1;
}

static void c64_dirty_scan(void)
{
    int b, bit, page, i;
    unsigned a0, a1;
    unsigned v;

    /* THE WHOLE TAKE IS ONE CALL (c64mem.inc). The bitmap, the write window
     * and the ANY byte are read into the package and the scratch is reset in
     * the same pass, under the same lock - so nothing is lost between the
     * core's write and this read (9.2) - and the matrix pass below asks the
     * package's copy per row, at no call at all. */
    c64_dirty_take(c64_dpg);
    c64_wlo = (unsigned)c64_dpg[32] | ((unsigned)c64_dpg[33] << 8);
    c64_whi = (unsigned)c64_dpg[34] | ((unsigned)c64_dpg[35] << 8);

    /* the matrix: the window's row range AND the row's own pages */
    if (c64_whi >= c64_wlo && c64_whi >= c64_mbase
                           && c64_wlo < c64_mbase + C64_CELLS) {
        unsigned f = (c64_wlo > c64_mbase) ? (c64_wlo - c64_mbase) : 0;
        unsigned l = c64_whi - c64_mbase;
        if (l >= C64_CELLS)
            l = C64_CELLS - 1;
        for (i = (int)(f / C64_COLS); i <= (int)(l / C64_COLS); i++) {
            a0 = c64_mbase + C64_X40(i);
            a1 = a0 + C64_COLS - 1;
            if (c64_pg_dirty(a0 >> 8)
                || ((a1 >> 8) != (a0 >> 8) && c64_pg_dirty(a1 >> 8)))
                c64_row_dirty(i);
        }
    }

    for (b = 0; b < 32; b++) {
        v = (unsigned)c64_dpg[b];
        if (v == 0)
            continue;
        for (bit = 0; bit < 8; bit++) {
            if ((v & (0x80 >> bit)) == 0)
                continue;
#ifdef C64_HOST
            c64_n_dirtypg++;
#endif
            page = b * 8 + bit;
            a0 = (unsigned)page << 8;
            a1 = a0 + 255;
            /* a RAM character set: a glyph can be anywhere, so every row */
            if (c64_chr_isram && a1 >= c64_chr_lo && a0 <= c64_chr_hi)
                c64_dirty_all();
        }
    }
}

/* ==========================================================================
 * THE FRAME REGISTERS (5.1)
 *
 * The screen base and the character base come from $D018 AND the CIA2 bank
 * bits, which are inverted as the hardware has them. And in banks 0 and 2 a
 * character base of $1000-$1FFF is where the VIC sees the CHARACTER ROM -
 * which is how a stock machine gets its face, and why the boot screen works
 * with no charset anywhere in RAM.
 * ========================================================================*/
static void c64_frame_regs(void)
{
    unsigned bank, cb, co;

    bank = (unsigned)((~c64_cia[1][0x00]) & 3) << 14;
    c64_mbase = bank + ((unsigned)((c64_vic[0x18] >> 4) & 15) << 10);
    cb = ((unsigned)((c64_vic[0x18] >> 1) & 7) << 11);
    co = cb;
    cb += bank;
    if ((bank == 0x0000 || bank == 0x8000) && co >= 0x1000 && co < 0x2000) {
        c64_chr_isram = 0;
        c64_chr_seg = c64_m.romseg;
        c64_chr_off = C64_ROM_CHARGEN + (co - 0x1000);
        c64_chr_lo = 0;
        c64_chr_hi = 0;
    } else {
        c64_chr_isram = 1;
        c64_chr_seg = c64_m.ramseg;
        c64_chr_off = cb;
        c64_chr_lo = cb;
        c64_chr_hi = cb + 2047;
    }
    /* $D011 bit 5 = BMM, bit 6 = ECM; $D016 bit 4 = MCM. Wave 1 composes
     * mode 0 and c64band.inc fills the rest with the background until wave 4
     * writes them (docs/plans/completed/C64-PORT-PLAN.md wave 4). */
    c64_mode = (((c64_vic[0x11] >> 6) & 1) << 2)
             | (((c64_vic[0x11] >> 5) & 1) << 1)
             | ((c64_vic[0x16] >> 4) & 1);
    c64_watch_set();
}

/* c64_watch_set - tell the core WHERE the write window is worth taking (9.2).
 * Four scratch stores, written wherever the matrix can move: here (every
 * flush), and from c64_io_wr the moment $D011/$D016/$D018 or CIA2 $DD00's bank
 * bits change, so the window never describes the previous matrix. */
static void c64_watch_set(void)
{
    unsigned hi = c64_mbase + C64_CELLS - 1;
    c64_scr_wr(C64_SCR_WATLO, (int)(c64_mbase & 0xFF));
    c64_scr_wr(C64_SCR_WATLO + 1, (int)((c64_mbase >> 8) & 0xFF));
    c64_scr_wr(C64_SCR_WATHI, (int)(hi & 0xFF));
    c64_scr_wr(C64_SCR_WATHI + 1, (int)((hi >> 8) & 0xFF));
}

/* ==========================================================================
 * THE TIER TABLE (9.8), AND WHERE ITS NUMBERS COME FROM
 *
 * `make c64bandbench` builds tests/c64band and boots it under
 * `-icount shift=3,sleep=off`, where one PIT count is 0.359 ms of real
 * 4.77 MHz XT (PERFORMANCE.md Part 4). The bench takes N = 8 iterations a
 * row, so a row's per-operation cost is (counts / 8) x 0.359 ms.
 *
 * MEASURED, 2026-08-21 and RE-TAKEN 2026-08-22 UNDER AN ARMED CLIP, which is
 * the difference between what the bench measured and what this package pays.
 * The bench's rerun draws from W_ONKEY and W_ONCLICK, and the kernel arms a
 * clip region for W_PAINT and for nothing else (SPEC.md 11.3) - so the first
 * draft drew UNCLIPPED, over anything covering it, and timed the kernel's
 * unclipped paths. c64_flush never runs that way: os88_onwake arms the clip
 * before it and a paint arrives with one armed. Measured both ways in the
 * same session, the clip costs 28% of a 40-cell font_run (718 -> 922 counts)
 * and 17% of a 320x8 blit (40 -> 47), and nothing at all of the composer,
 * which is package code and never asks the kernel.
 *
 * The FONT_RUN row is the bar taken in the same run, which is Set 64's rule
 * and the calibration check: 922 counts / 8 x 0.359 = 41.4 ms against
 * PERFORMANCE.md's model of 40 x 900 us + 756 = 36.8 ms for the same forty
 * cells - the model is the machine to 12%, and the 12% is the clip.
 *
 *   FONT_RUN 40 aligned      922 counts   41.4 ms   (the bar, clipped)
 *   BAND1 40 cells           168           7.54 ms
 *   BAND1 1 cell               8           0.36 ms
 *   BLIT1 320x8 stride 40     47           2.11 ms
 *   BAND 40 compose+blit     215           9.65 ms  <- a changed ROW
 *   BAND 1 compose+blit       31           1.39 ms  <- a changed CELL
 *   ROWSPAN 40 equal          35           1.57 ms
 *   ROWSPAN 40 differing      38           1.71 ms
 *   ROWCOPY 40                35           1.57 ms
 *   ROWSIG 40                 24           1.08 ms
 *   BAND_X2 8 rows           450          20.19 ms
 *
 * Solving the two BAND1 rows for the call and the cell: 39 cells cost
 * 20 counts, so a CELL is 0.513 counts = 184 us and the CALL floor is
 * 175 us. Those two are C64BENCH_CELL and C64BENCH_CALL below, and
 * hosttest/c64uitest.c prices its whole cost table from them.
 *
 * THREE DECISIONS FALL OUT OF THAT TABLE, and each is a constant below:
 *
 *  1. A CHANGED CELL IS 1.39 ms AND A CHANGED ROW IS 9.65 ms. That is the
 *     whole reason the composer takes a span and the write window exists
 *     (9.2): without them every keystroke would cost the row.
 *  2. A FULL 25-ROW REPAINT IS ~234 ms - four host ticks. So the 8086 tier
 *     flushes every OTHER tick: a machine that cannot draw a frame inside a
 *     tick gains nothing by being asked twice a tick, and the ordinary case
 *     (one row, 9.4 ms) is unaffected either way.
 *  3. FULLSCREEN 2x IS 20.19 ms FOR EIGHT ROWS - 63 ms for a screen on top
 *     of the compose. That is why the CPU_8086 tier is 1:1 centred and 2x
 *     belongs to the faster ones (9.8).
 *
 * A change that moves one of these rows up is a regression against a
 * documented number. Re-take the bench and change this comment,
 * C64-SPEC §9.7's table and the constants together, or not at all.
 * ========================================================================*/
#define C64BENCH_CALL 175               /* c64_band1's call floor, us */
#define C64BENCH_CELL 184               /* ...and one composed cell */
#define C64BENCH_SPAN 1571              /* c64_rowspan over 40 equal cells */
#define C64BENCH_SIG  1077              /* c64_rowsig over one row's sources */
#define C64BENCH_X2   20190             /* c64_band_x2, eight rows */

/* ...AND THE TWO THE HARNESS PRICES THE SCRATCH FROM, WHICH ARE MODELS AND
 * NOT BENCH ROWS. tests/c64band measures c64band.inc; these two live in
 * c64mem.inc and are computed from PERFORMANCE.md's own constants - 11 us for
 * a near call + ret, and the body's clocks at 0.21 us each on a 4.77 MHz
 * 8088, whose 8-bit bus adds 4 clocks to every word access:
 *   a c64_scr_rd / c64_scr_wr thunk   ~125 clocks of body  ->  ~38 us
 *   c64_dirty_take   rep movsw 16 + rep stosw 16 + shell   -> ~190 us
 * They are here beside the bench figures because §9.7's table, this file's
 * constants and hosttest/c64uitest.c change together or not at all. */
#define C64COST_SCRACC 38               /* one scratch thunk call, us */
#define C64COST_TAKE  190               /* ...and the whole take, in one */

/* ...AND THE FOUR WAVE 3 ADDED, FOR THE SAME REASON AND BY THE SAME METHOD:
 * Edit > Copy and Edit > Paste touch BYTES, and the first model charged the
 * whole of Copy as 1,000 near calls when the shipped loop was making two FAR
 * calls a cell. These are models, not bench rows, and each is written out.
 *
 *   C64COST_OVLCALL  ONE OVERLAY BRIDGE CROSSING. crt0.asm's cc_ovthunk is
 *                    reached by a far call (PERFORMANCE.md: 46.7 us) and makes
 *                    a near call + ret to the body (11 us). Every ovl_* entry
 *                    and every resident thing an ovl_* calls pays one. It is
 *                    charged from the STRUCTURE of a command in the harness,
 *                    because a host build has no overlay to count.
 *   C64COST_ZCALL    c64_zcopy_out's call and shell - 11 us for the near call
 *                    + ret and ~70 clocks of prologue, three segment loads and
 *                    epilogue at 0.21 us.
 *   C64COST_ZBYTE10  ...and one byte of its `rep movsb`, in TENTHS of a us:
 *                    17 clocks a byte on the 8088's 8-bit bus.
 *   C64COST_CPCELL   one cell of Edit > Copy's inner loop, WHICH IS NOW
 *                    ASSEMBLY (c64mem.inc's c64_copy_row). Eleven
 *                    instructions, 22 bytes, ~85 clocks of execution against a
 *                    4.34-clock-a-byte fetch floor of ~95 -> ~95 clocks ->
 *                    20 us. It was 75 while the loop was SmallerC's: 82
 *                    instruction bytes of SS-relative word code reloading
 *                    every local, ~356 clocks. Moving it is docs/C-TOOLCHAIN's
 *                    rule ("the inner loop is assembly") and it took a
 *                    full-screen Copy from ~83 ms to ~26.
 *   C64COST_CPCALL   ...and c64_copy_row's own call and shell, once a ROW: 11
 *                    us for the near call + ret and ~60 clocks of prologue,
 *                    the segment load and the epilogue at 0.21 us.
 *   C64COST_PSBYTE   the same measurement over c64_paste_feed's per-byte arm:
 *                    135 bytes with the CR arm, ~105 on an ordinary character
 *                    -> ~456 clocks -> 96 us, plus c64_wr's near call and its
 *                    body (the window test and the dirty bit) at ~50. It is
 *                    charged per byte the feeder TYPES, and the feeder runs in
 *                    os88_onwake with NO lock held, ten bytes at a time.
 *
 * If either loop is rewritten, re-count the emitted bytes and change these,
 * C64-SPEC §9.7's two rows and the harness together or not at all. */
#define C64COST_OVLCALL 58              /* one bridge crossing, us */
#define C64COST_ZCALL   26              /* c64_zcopy_out's call + shell */
#define C64COST_ZBYTE10 36              /* ...and a byte moved, TENTHS of us */
#define C64COST_CPCELL  20              /* one converted screen cell, ASM */
#define C64COST_CPCALL  24              /* ...and c64_copy_row's own shell */
#define C64COST_PSBYTE 145              /* one byte the paste feeder types */

/* c64_flush_every (declared in c64.c) - host ticks between flushes, from the
 * tier and the numbers above. */
static void c64_tier_init(void)
{
    int b;

    c64_flush_every = (os88_cpu() == OS88_CPU_8086) ? 2 : 1;
    /* ...AND THE WALL SLICE IS SEEDED FROM THE SAME FACT, which C64-SPEC §4.4
     * says and this did not do: the budget always began at C64_SLICE_MIN and
     * walked up four slices at a time, so a 386 spent its first dozen wakes
     * running 256-cycle slices - each with a full alarm query, a c64_run
     * shell and a c64_advance over both CIAs, the VIC and the TOD around
     * about a quarter of a millisecond of emulated work.
     * apps/runcpm/runcpm.c:1215 is the shape: `512 << os88_cpu()` - 512 on an
     * 8086, 1024 on a 286, 2048 on a 386 - clamped into this core's own
     * range, which is narrower because the countdown is a SIGNED word (4.2).
     * The adaptation still owns it from the first exhausted slice. */
    b = 512 << os88_cpu();
    if (b < C64_SLICE_MIN)
        b = C64_SLICE_MIN;
    if (b > C64_SLICE_MAX)
        b = C64_SLICE_MAX;
    c64_budget = b;
    c64_fastn = 0;
    /* ...AND THE FULLSCREEN TIER (9.8's table), decided ONCE and read by
     * c64_geom. c64_band_x2 is 20.19 ms for eight rows on tests/c64band
     * (C64BENCH_X2), so a full 25-row screen doubled is ~63 ms ON TOP of the
     * compose - a fifth of a second a frame on the target 4.77 MHz 8088,
     * against a machine that is already running at a fraction of real speed.
     * The CPU_8086 tier therefore takes the 1:1 centred row, which is a FACT
     * this code tests rather than a guess about speed (CLAUDE.md's rule 7),
     * and every tier above it magnifies. */
    c64_can2x = (os88_cpu() != OS88_CPU_8086);
}

/* ==========================================================================
 * THE STATUS ROW (10)
 *
 * 336 pixels = 42 cells, and that number decides the layout. VICE's two speed
 * strings are 12 cells each with their own field widths - `%7.0f%% cpu`
 * (statusbarspeedwidget.c:572) and `%8.1f fps` (:653) - so 24 of the 42 are
 * spoken for before anything else is on the row. What fits beside them is
 * `Joysticks:` (10 cells), its two 5-dot indicators, the drive 8 number and
 * §10.2's two warp/pause lamps.
 *
 * THE ORDER IS VICE'S, LEFT TO RIGHT, and the first draft had it reversed.
 * uistatusbar.c:2816-2826 appends the SPEED widget first, then the
 * checkboxes, then tape+joysticks, then the drive units - so the field a VICE
 * user reads at the left edge is the speed, and this row put it last. The
 * volume button at the far end is dropped (10.3).
 *
 *   ox+  0   `%7.0f%% cpu`     12 cells   \  the SPEED widget
 *   ox+ 96   `%8.1f fps`       12 cells   /   (uistatusbar.c:2816 appends it
 *                                             first, so it is LEFTMOST)
 *   ox+192   -                  1 cell        THE one separator cell
 *   ox+200   `Joysticks:`      10 cells   \  the JOYSTICK widget
 *   ox+280   port 1's `+`       2 cells    > (uistatusbar.c:1763's label)
 *   ox+296   port 2's `+`       2 cells   /   (draw_joyport_cb, :780)
 *   ox+312   the drive number   1 cell    \  the three single-glyph state
 *   ox+320   the warp lamp `W`  1 cell     > indicators, at the right end
 *   ox+328   the pause lamp `P` 1 cell    /
 *                              ------
 *                              42 cells = 336 px of CONTENT, which is why the
 *                              window is authored 338 (c64.c: wm_geom's
 *                              `sub cx, 2`)
 *
 * EACH INDICATOR IS VICE'S PLUS. draw_joyport_cb (uistatusbar.c:780-860)
 * draws five squares in a `+` - up above, left and right beside, fire in the
 * centre, down below - and the first draft laid the same five bits in a
 * straight horizontal line, so the ten off-dots read as one continuous
 * leader between `Joysticks:` and the drive number and nothing said which dot
 * was which. The plus is three 2-pixel columns inside the same 16 x 3 band,
 * which also puts ~10 px of real gap between the two ports.
 *
 * THE WARP AND PAUSE LAMPS ARE `W` AND `P`, INVERTED WHEN LIT, AND THAT IS
 * VICE'S LED ROW FOLDED INTO TWO CELLS. The first draft cited
 * statusbarspeedwidget.c:406-415 and drew two unlabelled three-pixel dots
 * between `fps` and `Joysticks:`. Those lines are inside two `#if 0` blocks
 * and are not compiled; in VICE 3.10 the LEDs are made at
 * uistatusbar.c:2607-2616 and appended by statusbar_append_led (:2339-2362)
 * into `led_row_grid` - a SEPARATE ROW of the status bar at column 0, not the
 * speed widget's cell - and each one is statusbar_led_widget_create("warp:",
 * ...) / ("pause:", ...), so it carries a TEXT LABEL beside the lamp.
 *
 * `warp:` + a lamp + `pause:` + a lamp is 13 cells and this row has 42, of
 * which the two speed strings take 24, `Joysticks:` 10, the two indicators 4
 * and the drive number 1. So VICE's labels do not fit and the lamp and its
 * label are folded into ONE GLYPH each: the letter is always drawn, and the
 * cell is INVERTED - black on white - while the latch is on. That is a lamp
 * with its label always readable, it costs the same two cells the unlabelled
 * dots cost, and it says the same thing on 1bpp as on VGA, which a grey
 * would not (SPEC.md 39.4). Stated in C64-SPEC §10.2.
 *
 * AND THEY SIT AT THE RIGHT END, WHICH COSTS VICE NOTHING because VICE's LEDs
 * are on a DIFFERENT ROW from the speed widget: folding two rows onto one is
 * this port's decision and there is no VICE order to break. It was measured
 * on the glass: at 192, between `fps` and `Joysticks:`, the row read
 * `0.0 fpsWPJoysticks:` (build/port-shots/w1r2-launch.png) and the two lamps
 * were indistinguishable from the text either side of them. 41 of the 42
 * cells are fields, so there is exactly ONE separator cell to spend; spent
 * after `fps`, it leaves the three single-glyph state indicators - drive,
 * warp, pause - in one cluster at the right end where a status bar's lamps
 * belong.
 *
 * DROPPED, with the width as the fact (10.3): the `Tape:` field, drive 8's
 * TRACK counter ` 18.5`, VICE's `warp:`/`pause:` label TEXT, and - as the
 * SPEC already said - Recording, Volume, CRT and Mixer. The drive NUMBER
 * stays, because the drive is the one of those a user looks for - drawn
 * PLAIN and never lit, because grey does not survive on a black row (see
 * c64_status).
 *
 * AND A MESSAGE OWNS THE ROW'S 40 FIELD CELLS - NOT THE ROW. The alternative,
 * measured above, is a seven-character message area, and the messages this
 * port has to show - `C64.ROM missing - see README.TXT` (1.4),
 * `Unable to load C64.OVL.` (13.3), `Cannot start the closer - try again.` -
 * are 32, 23 and 36. The two LAMPS at 320 and 328 are drawn under a message
 * as well, because `P` is the one indicator that exists to report the pause
 * state and `Paused.` is a message: hiding it left the lamp off the glass for
 * exactly as long as the machine was in the state it reports, and a machine
 * the user stopped keeps its message until the next event (10.1). A message
 * expires after ~5 seconds and the fields come back.
 *
 * TEN FILLS FOR TEN DOTS WAS 7.6 ms, and a whole status redraw was more than
 * two host ticks - for a row wave 2 wants to touch once a second. A dot is
 * three pixels; a gfx_* call is 756 us WHATEVER IT DRAWS (PERFORMANCE.md), so
 * each five-dot group is composed into a 24 x 3 band here and goes down with
 * ONE os88_gfx_blit1. And the row DELTA-DRAWS, which is what the comment used
 * to promise and c64_st_joy1/c64_st_joy2 were declared for: only a change of
 * message, of an indicator or of a latch redraws, and only its own field.
 * ========================================================================*/
#define C64_ST_CPUX 0                       /* 12 cells */
#define C64_ST_FPSX 96                      /* 12 cells */
                                            /* 192: the ONE separator cell */
#define C64_ST_JOYX 200                     /* 10 cells */
#define C64_ST_J1X  280                     /* 2 cells */
#define C64_ST_J2X  296                     /* 2 cells */
#define C64_ST_DRVX 312                     /* 1 cell, drawn PLAIN white and
                                             * never greyed (§10.3, and the
                                             * twenty lines below) */
#define C64_ST_WRPX 320                     /* 1 cell: the warp lamp, `W` */
#define C64_ST_PAUX 328                     /* 1 cell: the pause lamp, `P` */

static unsigned char c64_stb[2 * 3];        /* one indicator band, <= 16 x 3 */

/* c64_dots - compose `n` dots of `wd` pixels at a `pitch`-pixel pitch into
 * c64_stb, starting at bit `x0`; bit set in `mask` = that dot is on. An OFF
 * dot is ONE pixel rather than nothing: the position stays readable, and a
 * band carries no pen for SPEC.md 47's grey to survive in. `x0` is 1 so that
 * the leftmost pixel column is blank and the band does not touch the glyph
 * beside it. */
static void c64_dot(int nb, int p, int r, int on, int wd)
{
    int b;

    if (!on) {
        c64_stb[r * nb + (p >> 3)] |= (unsigned char)(0x80 >> (p & 7));
        return;                             /* an OFF position is ONE pixel:
                                             * the shape stays readable and a
                                             * band carries no pen for 47's
                                             * grey to survive in */
    }
    for (b = 0; b < wd; b++)
        c64_stb[r * nb + ((p + b) >> 3)] |=
            (unsigned char)(0x80 >> ((p + b) & 7));
}

/* c64_plus - VICE's joyport indicator, which is a PLUS and not a row of dots.
 * draw_joyport_cb (uistatusbar.c:780-860) draws five squares arranged in a
 * `+`: up above, left and right beside, fire in the centre, down below. The
 * first draft laid the same five bits in a straight horizontal line at a
 * 3-pixel pitch and started port 2 sixteen pixels after port 1, so on the
 * glass the ten off-dots read as one continuous leader `..........` between
 * `Joysticks:` and the drive number, with nothing saying which dot was a
 * direction, which was fire, or where port 1 ended.
 *
 * The plus fits in the same 16 x 3 band: three columns two pixels wide (so
 * 6 px of shape from x0), which leaves ~10 px of real gap before the next
 * port's band. VICE's bit order is already this port's - 0 up, 1 down,
 * 2 left, 3 right, 4 fire (C64-SPEC §8). */
static void c64_plus(int mask, int nb, int x0)
{
    int i, c;

    for (i = 0; i < nb * 3; i++)
        c64_stb[i] = 0;
    c = x0 + 2;                             /* the centre column */
    c64_dot(nb, c,      0, mask & 0x01, 2); /* up */
    c64_dot(nb, x0,     1, mask & 0x04, 2); /* left */
    c64_dot(nb, c,      1, mask & 0x10, 2); /* fire, in the centre */
    c64_dot(nb, x0 + 4, 1, mask & 0x08, 2); /* right */
    c64_dot(nb, c,      2, mask & 0x02, 2); /* down */
}

static void c64_st_band(int mask, int x, int y, int nb)
{
    c64_plus(mask, nb, 1);                  /* x0 = 1: the leftmost pixel
                                             * column stays blank so the band
                                             * does not touch what is beside
                                             * it */
    if (os88_gfx_blit1(c64_stb, nb, x, y, nb * 8, 3) < 0) {
        /* refused (a broken argument, or a kern_small machine that carries
         * the slot without the body): the second path os88.h asks every
         * caller to have. One fill for the whole group, so the row still
         * says something rather than nothing. */
        os88_set_color(mask ? OS88_WHITE : OS88_DGRAY);
        os88_gfx_fill(x, y + 1, x + nb * 8 - 1, y + 1);
#ifdef C64_HOST
        c64_n_fill++;
#endif
        return;
    }
#ifdef C64_HOST
    c64_n_blit++;
#endif
}

/* c64_status - the row, at `y`, delta-drawn. `y` is the LIVE bottom of the
 * content box and not a fixed offset from the top: on a 200-line adapter
 * wm_fit clamps the window and a fixed offset put this row - which carries
 * §1.4's permanent `C64.ROM missing` fact and every refusal - off the glass
 * entirely (build/port-shots/wave1-cga-launch.png). */
/* c64_st_num - one right-justified unsigned into a fixed field, which is what
 * `%7.0f` and `%8.1f` mean once the float is gone (10.2). `tenths` splits the
 * last digit off behind a point. */
static void c64_st_num(char *dst, int width, int v, int tenths)
{
    int i, d;

    for (i = 0; i < width; i++)
        dst[i] = ' ';
    if (v < 0)
        v = 0;
    i = width - 1;
    if (tenths) {
        d = v % 10;
        v = v / 10;
        dst[i] = (char)('0' + d);
        i--;
        dst[i] = '.';
        i--;
    }
    for (;;) {
        dst[i] = (char)('0' + (v % 10));
        v = v / 10;
        if (v == 0 || i == 0)
            break;
        i--;
    }
}

/* c64_st_field - draw only the cells of `now` that differ from what is on the
 * glass, and remember what was drawn.
 *
 * THE ROW'S NUMBERS CHANGE A DIGIT AND WERE COSTING TWELVE CELLS EACH. Both
 * fields were rebuilt in place and re-run whole every second - 24 glyph cells
 * and 2 call floors, ~23 ms, permanently, about ten times what a keystroke
 * costs - and nine of those cells were the two literal tails that never
 * change. The tails are now drawn once with the row and this walks the span
 * between the first and last differing cell, which is c64_rowspan's idea one
 * size down. A typical second is one or two cells. */
static void c64_st_field(int x, int y, char *now, char *prev, int n, int force)
{
    int i, f, l;
    char save;

    if (force) {
        f = 0;
        l = n - 1;
    } else {
        f = -1;
        l = -1;
        for (i = 0; i < n; i++) {
            if (now[i] != prev[i]) {
                if (f < 0)
                    f = i;
                l = i;
            }
        }
        if (f < 0)
            return;                         /* the field did not move */
    }
    save = now[l + 1];                      /* font_run wants a NUL, and the
                                             * buffer is 14 for an 8-cell
                                             * field, so l+1 is always inside */
    now[l + 1] = 0;
    os88_font_run(x + f * 8, y, now + f, OS88_WHITE, OS88_BLACK);
    now[l + 1] = save;
    for (i = f; i <= l; i++)
        prev[i] = now[i];
}

/* c64_st_lamps - the two labelled lamps, `W` and `P` (§10.2). The letter IS
 * the label and is always drawn; the cell is inverted while the latch is on,
 * and that inversion is the LED.
 *
 * IT IS A ROUTINE OF ITS OWN BECAUSE A MESSAGE MUST NOT HIDE IT. The row used
 * to return straight after drawing a message, so `P` - the one indicator that
 * exists to report the pause state - was off the glass for exactly as long as
 * the machine was paused: Alt+P answers `Paused.`, and a machine the user
 * stopped keeps its message until the next event (§10.1), so the lamp was
 * missing for the whole of the state it reports
 * (build/port-shots/wave2fix2-07-paused-msg-persists.png). The message now
 * owns the row's 40 FIELD cells and the two lamps keep their own two, on both
 * paths. */
static void c64_st_lamps(int ox, int y, int flags)
{
    os88_font_run(ox + C64_ST_WRPX, y + 1, "W",
                  (flags & 1) ? OS88_BLACK : OS88_WHITE,
                  (flags & 1) ? OS88_WHITE : OS88_BLACK);
    os88_font_run(ox + C64_ST_PAUX, y + 1, "P",
                  (flags & 2) ? OS88_BLACK : OS88_WHITE,
                  (flags & 2) ? OS88_WHITE : OS88_BLACK);
}

/* c64_slen - one message's length in CELLS. There is no strlen in this SDK.
 *
 * IT IS BOUNDED, AND THAT IS THE POINT RATHER THAN A MICRO-OPTIMISATION. The
 * header used to say it was asked "once per row REBUILD - not per flush", and
 * the code contradicted it: the caller evaluates it at the TOP of c64_status,
 * above every early return, so it ran on every flush for as long as a message
 * stood - on the path two lines below advertised as answering "nothing on the
 * row moved" in ZERO drawing calls. SmallerC's `while (s[n] != 0) n++;` is
 * ~22 instruction bytes of SS-relative code, fetch-bound at 4.34 clocks a
 * byte, so ~20 us a character; a 33-cell message is ~0.66 ms per flush, and
 * while a message stands the flush gate fires every `fe` ticks, which is
 * 30-60 ms of pure strlen per message on the target for a string that cannot
 * change.
 *
 * The whole question is `len <= C64_MSGSHORT`, so the scan stops at
 * C64_MSGSHORT + 1: the answer is the same and the cost is capped at 26
 * characters whatever the message. Caching the length in c64_say instead
 * would be a word of bss and would have to be right for the two PERMANENT
 * lines as well, which c64_say never sees. */
static int c64_slen(const char *s, int cap)
{
    int n = 0;
    while (n <= cap && s[n] != 0)
        n++;
    return n;
}

/* A MESSAGE THAT FITS LEFT OF THE JOYSTICK WIDGET DOES NOT ERASE IT.
 * C64_ST_JOYX is 200, i.e. cell 25, so a message of 25 cells or fewer occupies
 * only the two speed fields and the separator - and wave 3 made that the
 * difference between a widget and a defect, because c64kbd.c now raises
 * `ScrollLock for joystick` FROM JOYSTICK USE: the first deflection of the
 * stick blanked the two indicators that report the stick, for five seconds.
 * The permanent JAM line (§4.5, 22 cells) is short too, and used to blank them
 * for the rest of the session.
 *
 * A LONGER MESSAGE STILL OWNS THE WHOLE FIELD AREA - `C64.ROM missing - see
 * README.TXT` is 32 cells and there is nowhere else for it to go. */
#define C64_MSGSHORT (C64_ST_JOYX / 8)

static void c64_status(int ox, int y, int w)
{
    const char *t;
    int msg, joy1, joy2, flags, sp, mshort, lfull, rfull, wide, wchg, x2;

    /* THREE PERMANENT ROW STATES, NOT ONE. A message expires after five
     * seconds; `C64.ROM missing` and a JAMMED CPU do not, because neither is
     * a thing that stops being true (§1.4 says so for the first, §4.5 for the
     * second). Without the jam case the glass showed a dead machine and an
     * idle one identically - the ordinary widget row, `0% cpu  0.0 fps`, with
     * nothing saying the CPU had stopped (build/port-shots/wave2-05-jam.png).
     * The message still goes up first: it is how the jam ARRIVES. */
    msg = (c64_msg[0] != 0) ? 1
        : (c64_state == C64_ST_JAM) ? 3
        : 0;                                /* ...and `C64.ROM missing - see
                                             * README.TXT` was msg == 2, until
                                             * the ROM became a PART and
                                             * stopped being a thing that can
                                             * go missing (1.4) */
    t = (msg == 1) ? c64_msg : c64_jamline;
    mshort = (msg != 0 && c64_slen(t, C64_MSGSHORT) <= C64_MSGSHORT) ? 1 : 0;
    joy1 = c64_joyswap ? c64_joy2 : c64_joy1;
    joy2 = c64_joyswap ? c64_joy1 : c64_joy2;
    flags = (c64_warp ? 1 : 0) | (c64_pause ? 2 : 0);

    sp = (c64_pct != c64_st_pct || c64_fps10 != c64_st_fps) ? 1 : 0;
    wchg = (joy1 != c64_st_joy1 || joy2 != c64_st_joy2
            || flags != c64_st_flags) ? 1 : 0;

    /* THE RIGHT HALF IS REBUILT for one of two reasons: the row's pixels are
     * not ours at all, or a LONG message was standing where the widgets go
     * and is not standing there any more. The LEFT half is rebuilt for those,
     * for a new message, and for a message going up or coming down. */
    rfull = (!c64_st_ok || (c64_st_blank && !(msg != 0 && !mshort))) ? 1 : 0;
    lfull = (rfull || !c64_st_lok || msg != c64_st_shown) ? 1 : 0;

    if (!lfull && !rfull) {
        /* nothing on the row moved: the flush calls this every time and THIS
         * is what makes that free. Under a LONG message nothing else is on
         * the glass to move; under a SHORT one the widgets are still there
         * and still delta-draw, and only the speed fields - which the message
         * is standing on - are held. */
        if (msg != 0 && !mshort)
            return;
        if (!wchg && (msg != 0 || !sp))
            return;
    }

    if (lfull) {
        /* THE ONE PLACE AN ERASE IS RIGHT: those pixels are unknown (a fresh
         * window, an expose, a message going up or coming down changes every
         * field it covers at once). The delta path below draws over its own
         * field's cells and never erases first - font_run and blit1 both
         * arrive in final polarity, which is why they exist (os88.h:442).
         *
         * AND IT ERASES WHAT IT IS ABOUT TO REDRAW, WHICH IS NOT ALWAYS THE
         * ROW. A message of 25 cells or fewer reaches cell 24 and stops, so
         * the fill stops there too: the joystick widget, the drive number and
         * the two lamps stay on the glass and keep delta-drawing under it.
         * That is a fix before it is a saving - wave 3 raises `ScrollLock for
         * joystick` FROM JOYSTICK USE (c64kbd.c), so the first deflection of
         * the stick used to blank the two indicators that report the stick,
         * and the permanent JAM line blanked them for the session. */
        wide = (rfull || (msg != 0 && !mshort)) ? 1 : 0;
        x2 = wide ? (ox + w - 1) : (ox + C64_MSGSHORT * 8 - 1);
        if (x2 > ox + w - 1)
            x2 = ox + w - 1;                /* a content box narrower than the
                                             * widget: §9.8's clamp reaches
                                             * this row too */
        os88_set_color(OS88_BLACK);
        os88_gfx_fill(ox, y, x2, y + C64_STATH - 1);
#ifdef C64_HOST
        c64_n_fill++;
#endif
        c64_st_shown = msg;
        c64_st_ok = 1;
        c64_st_lok = 1;
    }

    if (msg != 0) {
        /* THE MESSAGE OWNS THE FIELD CELLS IT NEEDS AND THE LAMPS KEEP THEIR
         * TWO. msg == 2 is §1.4's permanent fact - the disk is what it is
         * until someone changes it - and msg == 3 is a jammed CPU, VICE's own
         * line at 22 glyphs (§4.5); neither expires. Every string here fits
         * 40 cells (c64_say clamps c64_msg to that, and the harness checks
         * every literal against that number at its SOURCE), so nothing
         * reaches the two cells at 320 and 328.
         *
         * It is lettered only on an `lfull`, which is the only time it is not
         * already on the glass: a widget delta under a short message must not
         * re-letter it. */
        if (lfull)
            os88_font_run(ox, y + 1, t, OS88_WHITE, OS88_BLACK);
        if (!mshort) {
            c64_st_lamps(ox, y, flags);
            c64_st_flags = flags;            /* ...so the delta path below
                                              * does not have to redraw them
                                              * when the message comes down */
            c64_st_blank = 1;                /* ...and the widgets are NOT on
                                              * the glass, which is what makes
                                              * the flush after this message
                                              * rebuild them */
            return;
        }
        /* a SHORT message falls through: the widgets right of it are live */
    }

    if (msg == 0 && (lfull || sp)) {
        /* THE SPEED WIDGET'S TWO STRINGS, folded onto this one row and
         * LEFTMOST, as uistatusbar.c appends them - and they are REAL:
         * `% cpu` is emulated cycles over 985,248 and `fps` is emulated VIC
         * FRAMES, so at 100 %% it reads 50.1 (10.2). They delta-draw against
         * what was last on the glass, so a machine holding one speed costs
         * nothing a second. Under a message they are not drawn at all - the
         * message is standing on them, and it is `lfull` that puts them back
         * when it comes down. */
        c64_st_num(c64_st_cpu, 7, c64_pct, 0);
        c64_st_num(c64_st_fpsb, 8, c64_fps10, 1);
        c64_st_field(ox + C64_ST_CPUX, y + 1, c64_st_cpu, c64_st_cpup, 7,
                     lfull);
        c64_st_field(ox + C64_ST_FPSX, y + 1, c64_st_fpsb, c64_st_fpsp, 8,
                     lfull);
        if (lfull) {
            /* the two literal tails, drawn ONCE with the row: they are 9 of
             * the 24 cells this used to repaint every second and not one of
             * them has ever changed */
            os88_font_run(ox + C64_ST_CPUX + 7 * 8, y + 1, "% cpu",
                          OS88_WHITE, OS88_BLACK);
            os88_font_run(ox + C64_ST_FPSX + 8 * 8, y + 1, " fps",
                          OS88_WHITE, OS88_BLACK);
        }
        c64_st_pct = c64_pct;
        c64_st_fps = c64_fps10;
    }
    if (rfull) {
        os88_font_run(ox + C64_ST_JOYX, y + 1, "Joysticks:",
                      OS88_WHITE, OS88_BLACK);      /* uistatusbar.c:1763 */
        /* THE DRIVE NUMBER IS DRAWN PLAIN, AND §10.3 SAYS SO WITH THE
         * ARITHMETIC. It was `os88_gfx_pen(1)` + a DGRAY font_run, and on
         * both 1bpp adapters that cell was EMPTY (w1r2-cga-status.png): the
         * pen's checkerboard is laid in [gfx_disink], which
         * kernel/vga12.inc:3414 sets to CBLACK for every content pen because
         * a window's content is white in both themes - and this row's paper
         * is BLACK, so a greyed glyph is a black stipple on black. font_str
         * would have done exactly the same thing; the pen is not the problem,
         * the surface is. It is the same fact that made §10.2 refuse a grey
         * for the warp and pause lamps and fold them into an inverted glyph
         * instead, and the same one behind an OFF dot being a pixel rather
         * than nothing: THIS ROW HAS NO VOCABULARY FOR GREY. Inverting the
         * cell was the other candidate and is worse - on this row an inverted
         * cell means a LIT LAMP, and a drive that is permanently lit says
         * something false rather than nothing. So the unit number is drawn,
         * white, and never lights; that there is no drive is carried where
         * SPEC.md 47 wants it, on the greyed File > Attach/Detach items. */
        os88_font_run(ox + C64_ST_DRVX, y + 1, "8", OS88_WHITE, OS88_BLACK);
    }

    if (rfull || joy1 != c64_st_joy1)
        c64_st_band(joy1, ox + C64_ST_J1X, y + 3, 2);
    if (rfull || joy2 != c64_st_joy2)
        c64_st_band(joy2, ox + C64_ST_J2X, y + 3, 2);
    if (rfull || flags != c64_st_flags)
        c64_st_lamps(ox, y, flags);
    c64_st_joy1 = joy1;
    c64_st_joy2 = joy2;
    c64_st_flags = flags;
    c64_st_blank = 0;
}

/* ==========================================================================
 * THE GEOMETRY, IN ONE PLACE (9.1, 9.8) - AND FULLSCREEN IS WHY
 *
 * The first draft clamped the drawable width to C64_W_W (336) and anchored
 * the screen at org.x + C64_BORDER, and Preferences > Fullscreen (Alt+D) is a
 * LIVE item. kernel/wm.inc:7295 says wm_draw_win's SPEC.md 11.2 branch fills
 * the WHOLE FRAME white for a WF_FULL window "and has no opt-out", so
 * entering fullscreen on a 640x480 desktop left a 320x200 picture in the
 * top-left corner of a white 640x480 screen with a 336-pixel status strip
 * under it, and nothing in the package read c64_full at all. An item that is
 * live and does the wrong thing is worse than one greyed with its fact.
 *
 * So the screen is CENTRED in whatever content box the window has and the
 * border fills the rest, which is C64-SPEC §9.8's `CPU_8086` tier row - 1:1
 * centred - taken on EVERY tier for now. The 2x rows of that table are not
 * implemented in this wave and §9.8 says so with the arithmetic: c64_band_x2
 * is 20.19 ms for eight rows on the bench, 63 ms a screen ON TOP of the
 * compose, and it needs a second shadow geometry. Nothing here is faked or
 * silently missing: fullscreen works, it does not magnify.
 *
 * `& ~7` on the horizontal offset is not cosmetic. os88_wm_snap put org.x on
 * a cell boundary and OSAPI_GFX_SCROLL refuses a rect whose x1 or x2+1 is not
 * a multiple of 8 (9.4), so the screen's left edge has to stay on one.
 * Vertically nothing needs it: blit1 and scroll take any y.
 * ========================================================================*/
static int c64_gox, c64_goy;                /* the content box's origin */
static int c64_gw, c64_gh;                  /* ...and its live size */
static int c64_gsx, c64_gsy;                /* the C64 screen's top-left */
static int c64_gsty;                        /* the status row's y */
static int c64_gnrows;                      /* cell rows that fit above it */

static int c64_geom(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    int d, n;

    if (os88_wm_geom(win, &sz) < 0)
        return -1;
    os88_wm_content(win, &org);
    c64_gox = org.x;
    c64_goy = org.y;
    c64_gw = sz.w;
    c64_gh = sz.h;
    /* THE STATUS ROW IS ANCHORED TO THE LIVE BOTTOM of the content box, not
     * to a fixed 216 from the top. C64_CONT_H is 226 and that is a 480-line
     * number: a 200-line CGA desktop cannot give it, wm_fit clamps the window,
     * and a fixed offset put this row - which is where §1.4's permanent
     * `C64.ROM missing` fact and every refusal are printed - off the glass
     * with nothing saying so. os88_main asks os88_video() and does not
     * AUTHOR a window taller than the desktop, and this reads what it got. */
    c64_gsty = org.y + sz.h - C64_STATH;

    /* --- 9.8'S TIER TABLE, IN ONE PLACE. 2x is a FULLSCREEN-only thing: a
     * framed window is authored 336 x 226 and there is nothing to fill. The
     * two axes are decided separately because the adapters differ in exactly
     * that way, and the rule is "double it if the box can hold it":
     *
     *   VGA 640x480       640 >= 640 and 470 >= 400  -> 2x both, 640x400
     *                                                   centred, 25 rows
     *   CGA 640x200       640 >= 640, 190 < 400      -> 2x HORIZONTAL only,
     *                                                   640 wide exactly, and
     *                                                   9.1's standing clamp
     *                                                   gives 21 of 25 rows
     *   Hercules 720x348  720 >= 640, 338 < 400      -> 2x horizontal, 640x200
     *                                                   centred, all 25 rows
     *   the CPU_8086 tier                            -> 1:1 centred (c64_can2x)
     *
     * A CGA pixel is already 2:1, so doubling X alone is what makes the
     * picture the RIGHT SHAPE there - it is not half a job. */
    c64_scw = 8;
    c64_sch = 8;
    if (c64_full && c64_can2x && !c64_no2x) {
        if (sz.w >= C64_COLS * 16)
            c64_scw = 16;
        if (c64_scw == 16
            && c64_gsty - org.y >= C64_ROWS * 16 + C64_BORDER * 2)
            c64_sch = 16;
    }

    /* THE BORDER FLOOR IS A 1:1 THING AND MUST NOT SURVIVE INTO 2x. At 1:1
     * the framed window is 336 wide and (336 - 320) / 2 IS C64_BORDER, so the
     * floor changes nothing there and only catches a window narrower than the
     * C64 - where the right edge clips, as it always did. At 2x on a 640-pixel
     * screen the picture is 640 wide and the margin is 0: forcing 8 would slide
     * it right and clip eight pixels off the last column. */
    d = (sz.w - C64_COLS * c64_scw) / 2;
    if (c64_scw == 8) {
        if (d < C64_BORDER)
            d = C64_BORDER;
    } else if (d < 0) {
        d = 0;
    }
    c64_gsx = org.x + (d & ~7);
    d = (c64_gsty - org.y - C64_ROWS * c64_sch) / 2;
    if (c64_sch == 8) {
        if (d < C64_BORDER)
            d = C64_BORDER;
    } else if (d < 0) {
        d = 0;
    }
    c64_gsy = org.y + d;

    n = (c64_gsty - c64_gsy) / c64_sch;
    if (n > C64_ROWS)
        n = C64_ROWS;
    if (n > 0 && c64_gsy + n * c64_sch > c64_gsty - C64_BORDER)
        n = (c64_gsty - C64_BORDER - c64_gsy) / c64_sch; /* keep the bottom
                                                          * border */
    if (n < 0)
        n = 0;
    c64_gnrows = n;
    return 0;
}

/* ==========================================================================
 * THE FLUSH (9.3)
 *
 * The lock is held by the caller and only around this, never around a slice.
 *   1. compose the dirty rows;
 *   2. test for a whole-frame shift FIRST (9.4);
 *   3. otherwise compare each composed row against the shadow and draw only
 *      the differing spans;
 *   4. update the shadow with what was drawn;
 *   5. border fills only if $D020 changed;
 *   6. the status row, delta-drawn.
 * ========================================================================*/
/* c64_border - the four strips around the screen, over the WHOLE live content
 * box. `sbot` is the first pixel row BELOW the last cell row on the glass, so
 * a window wm_fit clamped short still gets a border under whatever it does
 * show, and a FULLSCREEN window gets the whole 640 x 480 of it (9.8).
 *
 * It counts the fills it ISSUES. `c64_n_fill += 4` over a routine that emits
 * between two and four of them over-reported the border in every cost row
 * the harness printed. */
static void c64_border(int ox, int oy, int w, int sbot, int stat_y)
{
    int sx = c64_gsx, sy = c64_gsy;

    os88_set_color(c64_border_lit ? OS88_WHITE : OS88_BLACK);
    if (sy > oy) {
        os88_gfx_fill(ox, oy, ox + w - 1, sy - 1);
#ifdef C64_HOST
        c64_n_fill++;
#endif
    }
    if (sbot <= stat_y - 1) {
        os88_gfx_fill(ox, sbot, ox + w - 1, stat_y - 1);
#ifdef C64_HOST
        c64_n_fill++;
#endif
    }
    if (sbot > sy) {
        if (sx > ox) {
            os88_gfx_fill(ox, sy, sx - 1, sbot - 1);
#ifdef C64_HOST
            c64_n_fill++;
#endif
        }
        if (sx + C64_SCRW < ox + w) {
            os88_gfx_fill(sx + C64_SCRW, sy, ox + w - 1, sbot - 1);
#ifdef C64_HOST
            c64_n_fill++;
#endif
        }
    }
}

/* ==========================================================================
 * THE SECOND PATH: A ROW THROUGH THE KERNEL'S OWN FACE (9.5)
 *
 * os88.h:446 - "Returns -1 REFUSED and nothing drawn ... so TEST IT and have
 * a second path through os88_font_run()." A kern_small kernel (a
 * configuration `make test-full` builds) carries the BLIT1 slot without a
 * body, so on that machine every band this package composes is refused. The
 * first draft discarded the answer and updated the shadow anyway - a
 * permanently blank screen with nothing saying why - and the fix that only
 * kept the row OWED was no better: the wake re-posts, the row recomposes and
 * is refused again, for ever, at ~9 ms a row.
 *
 * So the row is DRAWN, once, with the kernel's 8x8 face: one os88_font_run
 * for the span, which is 1 call + n cells - the same shape RUNCPM's
 * rc_draw_band falls back to (rcterm.c:513) and cword's cwchrome.c:459. What
 * it cannot carry is stated rather than faked: this is not the C64's face, a
 * reverse-video cell is drawn plain, and a graphics cell has no glyph in this
 * face at all and is drawn as a dot. That is why the status row says
 * `No bands here - text only.` the first time it happens.
 *
 * AND THE SHADOW IS STILL UPDATED WITH THE COMPOSED BAND, deliberately. On
 * this path the shadow stops being "the pixels on the glass" and becomes a
 * proxy for THE SOURCES: what it buys is still true - if the composed band
 * equals the shadow, the sources decode the same way, so the font path would
 * draw the identical characters and there is nothing to do. Without it the
 * row is redrawn on every flush. hosttest/c64uitest.c drives the refusal and
 * asserts both halves: the fallback draws, and the flush after it does not.
 * ========================================================================*/
static int c64_ascii_of(int sc)
{
    sc &= 0x7F;                             /* the reverse-video bit: this
                                             * face has no reverse */
    if (sc == 0)
        return '@';
    if (sc <= 26)
        return sc + 64;                     /* 'A'..'Z' */
    if (sc <= 31)
        return '[' + (sc - 27);             /* [ \ ] ^ _ , near enough */
    if (sc <= 63)
        return sc;                          /* space..'?' are their own ASCII */
    if (sc == 96)
        return ' ';                         /* the shifted space */
    return '.';                             /* a graphics cell: no glyph in
                                             * this face, and a dot says
                                             * something is there */
}

static void c64_row_font(int i, int f, int l, int sx, int y)
{
    int c, n = 0;

    for (c = f; c <= l && n < C64_COLS; c++)
        c64_fbuf[n++] = (char)c64_ascii_of(c64_rd(c64_mbase + C64_X40(i) + c));
    c64_fbuf[n] = 0;
    os88_font_run(sx + f * 8, y, c64_fbuf, OS88_WHITE, OS88_BLACK);
}

/* c64_shift_test - a SIGNATURE test on the row sources (9.4), over the rows
 * that are actually ON THE GLASS.
 *
 * `nrows` is not a detail. The flush composes and re-signs rows 0..nrows-1
 * only, so c64_shsig[] past nrows is never written and stays at its power-on
 * 0 while c64_sig[] tracks the live matrix - and the inner compare then fails
 * on those rows for EVERY k, so a test over all 25 can never answer non-zero
 * on any adapter that clamps the window shorter than 25 rows. That is the
 * CGA XT, the target machine: every BASIC scroll would pay the whole-frame
 * path, ~230 ms, instead of one gfx_scroll plus its vacated rows.
 *
 * It also fills c64_sig[] for the compose loop, which used to recompute the
 * identical value per recomposed row - 25 x C64BENCH_SIG = 25.8 ms of pure
 * duplicate on the one path that recomposes everything. The gfx lock is held
 * for the whole flush, so the sources cannot move in between. */
static int c64_shift_test(int nrows)
{
    int i, k, moved;

    for (i = 0; i < nrows; i++)
        c64_sig[i] = c64_rowsig(c64_m.ramseg, c64_mbase + C64_X40(i),
                                c64_col + C64_X40(i), C64_COLS);
    c64_sig_ok = 1;
    moved = 0;
    for (i = 0; i < nrows; i++)
        if (c64_sig[i] != c64_shsig[i])
            moved = 1;
    if (!moved)
        return 0;
    for (k = 1; k <= nrows - 1; k++) {
        int ok = 1;
        for (i = 0; i + k < nrows; i++) {
            if (c64_sig[i] != c64_shsig[i + k]) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return k;
    }
    return 0;
}

/* c64_span_of - row i's changed cell span, out of the write window and the
 * colour RAM's own. Answers 0 when nothing in the row moved. The two results
 * are file statics because an out-parameter cannot be the address of an
 * automatic here (SPEC.md 73: SS != DS).
 *
 * THEY ARE `c64_cspf`/`c64_cspl` AND NOT `c64_spf`/`c64_spl` because
 * c64band.inc already declares `c64_spf: resw 1` / `c64_spl: resw 1` in this
 * same package's .bss for c64_rowspan's scan sentinels. Two storages holding
 * two different things, and the only thing that kept them apart was
 * SmallerC's leading underscore (`_c64_spf` in build/c64.gen.asm): CLAUDE.md's
 * label hygiene is ONE FLAT NAMESPACE, and the next hand-written .inc that
 * wants the C's span, typed without the underscore, would silently take the
 * composer's sentinel. It assembles cleanly either way. */
static int c64_cspf, c64_cspl;

static int c64_span_of(int i)
{
    unsigned r0, r1;
    int mf = C64_COLS, ml = -1;

    r0 = c64_mbase + C64_X40(i);
    r1 = r0 + C64_COLS - 1;
    if (c64_whi >= c64_wlo && c64_whi >= r0 && c64_wlo <= r1) {
        mf = (c64_wlo > r0) ? (int)(c64_wlo - r0) : 0;
        ml = (c64_whi < r1) ? (int)(c64_whi - r0) : C64_COLS - 1;
    }
    r0 = C64_X40(i);
    r1 = r0 + C64_COLS - 1;
    if (c64_chi >= c64_clo && c64_chi >= r0 && c64_clo <= r1) {
        int cf = (c64_clo > r0) ? (int)(c64_clo - r0) : 0;
        int cl = (c64_chi < r1) ? (int)(c64_chi - r0) : C64_COLS - 1;
        if (cf < mf)
            mf = cf;
        if (cl > ml)
            ml = cl;
    }
    if (ml < 0)
        return 0;
    c64_cspf = mf;
    c64_cspl = ml;
    return 1;
}

static void c64_flush(void *win)
{
    int i, k, sp, f, l, df, dl, nrows, w, trust, rc;
    int ox, oy, sx, sy, sty;

    if (c64_geom(win) < 0)
        return;
    ox = c64_gox;
    oy = c64_goy;
    w = c64_gw;
    sx = c64_gsx;
    sy = c64_gsy;
    sty = c64_gsty;
    nrows = c64_gnrows;
    c64_sig_ok = 0;

    /* --- THE MESSAGE DEADLINE, AT THE TOP, BEFORE ANY BRANCH CAN RETURN ---
     * It used to live down beside the status row, AFTER a branch that
     * returned early - the ROM-less machine's four-line notice, which is gone
     * now the ROM is a part (1.4) - so nothing ever cleared c64_msg on that
     * path and the first menu command a user picked owned the row for the
     * rest of the session. It stays at the top: there is exactly one writer
     * of c64_msg (c64_say) and this is its one reader of the clock, so it
     * belongs where EVERY flush passes through it, whatever branches get
     * added below it later.
     *
     * os88_ticks() is a 16-bit 18.2 Hz counter that wraps about once an hour
     * (os88.h:612), so `ticks > until` takes a message down AT ONCE when it
     * was raised in the last five seconds before the wrap - the difference is
     * compared, as c64.c's flush gate compares it. */
    if (c64_msg[0] != 0
        && (unsigned)(os88_ticks() - c64_msg_until) < 0x8000u)
        c64_msg[0] = 0;                     /* and `msg != c64_st_shown` in
                                             * c64_status is what redraws the
                                             * widgets under it */

    /* THE ROM-LESS BANNER WAS HERE, and it is deleted rather than disabled.
     * `C64.ROM is not on this disk.` over four lines, drawn once and repaired
     * on an expose, was what a machine that could not start showed - and with
     * the ROM a PART of C64.O88 (1.4, SPEC.md 20.12) there is no such
     * machine: op_load has the 20KB and the bytes before os88_main runs, or
     * the launch was refused before a sector was spent. A branch kept alive
     * for a state that cannot arise is a claim the code makes and the machine
     * cannot honour, which is SPEC.md 47's rule about a greying said one
     * level up. */


    c64_frame_regs();
    c64_dirty_scan();

    if (c64_border_dirty || !c64_sh_ok) {
        c64_border(ox, oy, w, sy + nrows * c64_sch, sty);
        c64_border_dirty = 0;
    }

    /* --- the shift test, FIRST (9.4) -----------------------------------
     * It is only asked when enough rows are dirty for a scroll to be what
     * happened - a keystroke dirties one row and a 25-row signature pass
     * would cost more than the draw it saves - and only in the mode whose
     * signature means anything. */
    k = 0;
    if (c64_sh_ok && c64_mode == 0 && c64_hold_r0 > c64_hold_r1 && nrows > 1) {
        int nd = 0;
        for (i = 0; i < nrows; i++)
            if (c64_rowd[i])
                nd++;
        /* FOUR FIFTHS OF THE ROWS ON THE GLASS - 20 of 25 - and it is a
         * measured constant, not a taste. A
         * scroll dirties the WHOLE matrix by definition - the KERNAL moves
         * 960 bytes and clears 40 - so every row is dirty and 25 >= 20. A
         * one-row change dirties the rows of the two 256-byte pages it
         * touches, which is 14 of them, and at a threshold of 8 the signature
         * test would run, find a spurious match on a screen with several
         * blank rows, and emit a scroll that the span compare then had to
         * undo: 1 scroll + 24 blits where 1 blit was the answer. Correctness
         * survived that (9.4: the scroll is a hint and the compare is the
         * truth) and the COST did not, which is the failure mode this
         * threshold exists to stop. */
        if (nd * C64_SHIFT_DEN >= nrows * C64_SHIFT_NUM)
            k = c64_shift_test(nrows);
    }
    if (k) {
        /* POSITIVE dy MOVES THE CONTENT UP, and the whole of this block is
         * written for content-up: c64_shift_test matched `c64_sig[i] ==
         * c64_shsig[i + k]` (what was at row i+k is now at row i), the shadow
         * slide below is dst row 0 <- src row k, and the vacated rows forced
         * are the k at the BOTTOM. SPEC.md 5.5, os88.h:437, and
         * apps/runcpm/rcterm.c:631 (`rc_scr_n << 3`, vacated `rows - n`) all
         * say the same thing. The first draft passed `-k * 8` - the one
         * argument negated - so on the real kernel a BASIC scroll slid the
         * picture DOWN, left the top k rows unspecified and never forced
         * them, and the shadow then described a frame that had moved the
         * other way, which makes the span compare answer "nothing changed"
         * for the rest of the session. The harness could not see it because
         * its own stub modelled the opposite convention too; both are fixed
         * together, and hosttest/c64uitest.c now checks the pixels MOVED UP
         * rather than only counting the calls. */
        /* AND IT WORKS AT 2x: the rect and the dy are in GLASS pixels, so a
         * k-row shift is k * c64_sch and the rect is C64_COLS * c64_scw wide.
         * gfx_scroll refuses a rect whose x1 or x2+1 is not a multiple of 8
         * (9.4) and both stay multiples of 8 at either scale, because c64_gsx
         * is snapped and 40 * 16 is 640. A refusal falls back to bands here as
         * it always did. */
        if (os88_gfx_scroll(sx, sy, sx + C64_COLS * c64_scw - 1,
                            sy + nrows * c64_sch - 1,
                            k * c64_sch) == 0) {
#ifdef C64_HOST
            c64_n_scroll++;
#endif
            os88_memcpy(c64_sh, c64_sh + C64_X320(k),
                        C64_X40((nrows - k) * 8));
            /* the k vacated rows: gfx_scroll leaves them for us, so they are
             * drawn WHATEVER the compare says - a stale shadow row that
             * happened to match would leave the glass blank (9.4) */
            c64_force_rows(nrows - k, nrows - 1, 0, C64_COLS - 1);
            for (i = nrows - k; i < nrows; i++) {
                c64_rowd[i] = 2;
                c64_shsig[i] = 0;
            }
            /* the shifted rows' signatures moved with them - and c64_sig[]
             * already holds exactly those values, because that is what the
             * test compared. No c64_rowsig call: the slide is a copy.
             *
             * AND `c64_rowd` IS *NOT* CLEARED FOR THE SHIFTED ROWS, WHICH
             * WAS PROPOSED AND IS WRONG. Clearing it would skip composing and
             * comparing the 16 rows the scroll slid, on the strength of the
             * 16-bit signature alone - and C64-SPEC §9.4 is explicit that the
             * signature is a HINT and that NOTHING RESTS ON IT: "after the
             * scroll is emitted the shadow is moved with it and every row
             * still goes through c64_rowspan against the moved shadow, so a
             * row that did not really shift simply compares unequal and is
             * drawn. The scroll is an optimisation whose failure mode is a
             * redraw, never a wrong screen." A collision is trivial to build
             * - screen code 1 in column 0 and screen code 2 in column 1 with
             * everything else blank sign identically, because the signature
             * is an XOR under a rotate - and skipping the compare turns that
             * from a wasted recompose into WRONG PIXELS THAT NEVER GO AWAY,
             * since the flags are then clear and nothing asks again.
             * hosttest/c64uitest.c drives exactly that pair.
             *
             * So the 25 recomposes a scrolled frame costs are the price of
             * the guarantee, and §9.7's `a k = 9 shift` row is what it costs
             * (256.0 ms). The signature saves the SCROLL - 1 call plus k rows
             * of drawing instead of 25 - not the compare. */
            for (i = 0; i + k < nrows; i++)
                c64_shsig[i] = c64_sig[i];
        } else {
            k = 0;                          /* refused: spans, and the shadow
                                             * stays true - nothing moved */
        }
    }

    /* --- compose, compare, draw ----------------------------------------- */
    for (i = 0; i < nrows; i++) {
        if (i >= c64_hold_r0 && i <= c64_hold_r1)
            continue;                       /* an About panel owns these rows:
                                             * drawing them would be drawing
                                             * under something opaque, and the
                                             * click that closes it forces
                                             * exactly them (c64.c) */
        if (!c64_rowd[i] && !c64_force[i] && c64_sh_ok)
            continue;

        /* THE COMPOSE SPAN, and whether the shadow may be trusted for it.
         *   the shadow is invalid            -> the whole row, drawn
         *   the sources changed wholesale    -> the whole row, COMPARED: a
         *                                       $D011 write that draws the
         *                                       same picture then draws
         *                                       nothing at all
         *   the sources changed in a span    -> that span, compared
         *   the glass is unknown over cells  -> union that in, and draw it */
        trust = c64_sh_ok && !c64_force[i];
        if (!c64_sh_ok || c64_rowd[i] == 2) {
            f = 0;
            l = C64_COLS - 1;
        } else if (c64_rowd[i] == 1 && c64_span_of(i)) {
            f = c64_cspf;
            l = c64_cspl;
        } else if (c64_force[i]) {
            f = c64_fc0[i];
            l = c64_fc1[i];
        } else {
            continue;                       /* the row was marked but nothing
                                             * in it moved */
        }
        if (c64_force[i]) {                 /* the unknown cells are drawn
                                             * whatever else this row wanted */
            if ((int)c64_fc0[i] < f)
                f = c64_fc0[i];
            if ((int)c64_fc1[i] > l)
                l = c64_fc1[i];
        }

        c64_band1(c64_bnd, f, l, c64_m.ramseg,
                  c64_mbase + C64_X40(i),
                  c64_col + C64_X40(i),
                  c64_chr_seg, c64_chr_off, c64_mode, c64_vic[0x21] & 15);
#ifdef C64_HOST
        c64_n_cell += (unsigned)(l - f + 1);
        c64_n_rows++;
#endif
        /* the shadow's SIGNATURE is updated HERE, with the row that was just
         * recomposed, and nowhere else. Re-signing all 25 rows at the end of
         * every flush cost 25 x C64BENCH_SIG = 25.8 ms - half a host tick on
         * the target, on every flush, for a keystroke that touched one row.
         * A row this flush did not recompose did not change its sources
         * either, so its old signature is still true. And when the shift test
         * has already run this flush it computed exactly this value from
         * exactly these bytes - the lock is held throughout - so it is read
         * rather than taken again: 25.8 ms of duplicate on the scroll path. */
        c64_shsig[i] = c64_sig_ok
                     ? c64_sig[i]
                     : c64_rowsig(c64_m.ramseg, c64_mbase + C64_X40(i),
                                  c64_col + C64_X40(i), C64_COLS);
        if (!trust) {
            df = f;
            dl = l;
        } else {
            sp = c64_rowspan(c64_bnd + f, c64_sh + C64_X320(i) + f,
                             l - f + 1);
            if (sp < 0)
                continue;                   /* the composed row equals the
                                             * glass: nothing is drawn */
            df = f + ((sp >> 8) & 0xFF);
            dl = f + (sp & 0xFF);
        }
        /* THE REFUSAL IS TESTED, AND IT HAS A SECOND PATH. os88.h:446 is
         * explicit - blit1 answers -1 with NOTHING DRAWN, for a broken
         * argument or for a kern_small kernel that carries the slot without
         * the body, "so TEST IT and have a second path". The first draft
         * discarded the answer and ran c64_rowcopy anyway, which is worse
         * than a missing row: the shadow then said the row was on the glass,
         * the span compare answered "nothing changed" on every later flush,
         * and the row was never retried - a permanently blank C64 screen with
         * nothing saying why, which is the one outcome SPEC.md 47 exists to
         * prevent. c64_st_band three hundred lines up already tested its own
         * blit, so the file was inconsistent with itself. */
        /* THE ONE PLACE THE PICTURE IS MAGNIFIED (9.8). Everything above is
         * in C64 pixels - the shadow, the signature, the span - and this is
         * where 320x200 becomes what the tier can afford. c64_band_x2 doubles
         * the WHOLE 40-byte row whatever the span is, so a one-cell change in
         * fullscreen costs the full C64BENCH_X2 20.19 ms; that is what a 2x
         * row costs and the cost table says so.
         *
         * ONE ROUTINE SERVES BOTH AXES. c64_band_x2 always emits 2 x rows of
         * C64_X2STRIDE, each a duplicate of its neighbour, so X-ONLY doubling
         * is the same buffer read at TWICE the stride: rows 0, 2, 4 ... are
         * the eight source rows, doubled horizontally and not vertically. No
         * second routine, no second table. */
        if (c64_scw == 8) {
            rc = os88_gfx_blit1(c64_bnd + df, C64_BSTRIDE,
                                sx + df * 8, sy + i * 8,
                                (dl - df + 1) * 8, 8);
        } else {
            c64_band_x2(c64_bx2, c64_bnd, 8);
#ifdef C64_HOST
            c64_n_x2++;
#endif
            rc = os88_gfx_blit1(c64_bx2 + df * 2,
                                (c64_sch == 16) ? C64_X2STRIDE
                                                : C64_X2STRIDE * 2,
                                sx + df * c64_scw, sy + i * c64_sch,
                                (dl - df + 1) * c64_scw, c64_sch);
        }
        if (rc < 0) {
            /* 2x HAS NO FONT FALLBACK, and inventing one would be a second
             * renderer for a case that is a kern_small kernel carrying the
             * slot without the body (os88.h). So the picture comes DOWN to
             * 1:1, where the font path exists, and stays there: c64_no2x is
             * latched, the shadow is thrown away and the next flush lays the
             * screen out again at the smaller scale. */
            if (c64_scw != 8) {
                c64_no2x = 1;
                c64_sh_ok = 0;
                c64_st_ok = 0;
                c64_dirty_any = 1;
                return;                     /* ...and RETURN rather than break:
                                             * the tail of this function clears
                                             * c64_dirty_any and sets
                                             * c64_sh_ok, which is exactly the
                                             * bookkeeping that must NOT stand
                                             * for a screen that was not drawn.
                                             * The next wake lays it out again
                                             * at 1:1 */
            }
            c64_row_font(i, df, dl, sx, sy + i * 8);    /* 9.5's font path */
            if (!c64_blit_said) {
                c64_blit_said = 1;
                c64_say("No bands here - text only.");
            }
        } else {
#ifdef C64_HOST
            c64_n_blit++;
#endif
        }
        c64_rowcopy(c64_sh + C64_X320(i) + df, c64_bnd + df, dl - df + 1);
    }

    for (i = 0; i < C64_ROWS; i++) {
        if (i >= c64_hold_r0 && i <= c64_hold_r1)
            continue;                       /* still owed a draw when the
                                             * panel goes: keep both flags */
        c64_rowd[i] = 0;
        c64_force[i] = 0;
    }
    c64_clo = 0xFFFF;
    c64_chi = 0;
    c64_dirty_any = (c64_hold_r0 <= c64_hold_r1);
    c64_sh_ok = 1;

    /* --- the status row, delta-drawn ------------------------------------
     * The message's deadline was examined at the TOP of this function, before
     * the ROM-less branch could return past it (§10.1). This is the one place
     * a refusal is meant to be readable under a fullscreen window (§9.8's
     * both-routes rule). */
    /* AND IT IS CALLED UNCONDITIONALLY. Gating it on c64_st_dirty made the
     * delta path unreachable from the product: nothing but c64_say, Swap
     * joysticks, c64_blank_rect and c64_sh_inval ever raised that flag, and
     * every one of them raises it for a FULL redraw - so the field compare
     * this row was rebuilt around was only ever exercised by the harness
     * setting the flag by hand. c64_status's own early return answers
     * "nothing on the row moved" in zero drawing calls, which is what the
     * delta was built for; that is the gate. The old `c64_st_dirty` flag is
     * gone with it: every one of its setters either cleared c64_st_ok (a
     * message, an expose, an invalidated shadow) or changed a FIELD the
     * compare below already sees (Swap joysticks, a message expiring), so it
     * only ever said a second time what the row could work out for itself. */
    c64_status(ox, sty, w);
}

/* c64_blank_rect - a rectangle of the content box whose pixels are no longer
 * ours: a partial expose (os88_paint, and WF_OWNBG means nobody whitened it),
 * or the rect an About panel has just stopped covering. The cell ROWS AND
 * COLUMNS it covers are FORCED, and the flush draws exactly them.
 *
 * IT DOES NOT FILL. The first draft filled the damage black and then blitted
 * composed bands over every one of those pixels a moment later - the
 * erase-then-letter pair PERFORMANCE.md's rule 2 names, and the reason
 * os88_font_run and os88_gfx_blit1 exist at all (os88.h:442: "the band
 * arrives in final screen polarity"). The composed band already carries its
 * own background; the border and the status row repaint themselves from
 * c64_border_dirty and c64_st_ok, and each is told only when the rect
 * actually reaches it.
 *
 * It takes a COLUMN span for the same reason: a menu closing over this window
 * is a damage rect about 190 px wide (MENU_MAXCH is 24 glyphs), and forcing
 * those thirteen rows full width composed 13 x 40 cells where 13 x 24 was
 * the answer - 122 ms against 75. */
static void c64_blank_rect(void *win, int x1, int y1, int x2, int y2)
{
    int r0, r1, c0, c1, sx, sy, sty;

    if (c64_geom(win) < 0)
        return;
    sx = c64_gsx;                           /* the CENTRED screen's origin,
                                             * which is what the cell grid is
                                             * measured from in fullscreen */
    sy = c64_gsy;
    sty = c64_gsty;

    /* THE ROM-LESS SURFACE'S EXPOSE REPAIR WAS HERE, and it went with the
     * banner it repaired (1.4): a machine with no ROM cannot exist now the
     * ROM is a part of the package. */
    if (y1 < sy || x1 < sx || x2 >= sx + C64_COLS * c64_scw
        || y2 >= sty - C64_BORDER)
        c64_border_dirty = 1;               /* the rect reaches the border */
    if (y2 >= sty)
        c64_st_ok = 0;                      /* ...and the status row's pixels
                                             * are no longer ours */

    if (y2 < sy || x2 < sx)
        return;                             /* the rect misses the screen */
    /* ...ON THE CELL GRID, WHICH IS 8 OR 16 PIXELS A SIDE (9.8). */
    r0 = (y1 - sy) / c64_sch;
    r1 = (y2 - sy) / c64_sch;
    c0 = (x1 - sx) / c64_scw;
    c1 = (x2 - sx) / c64_scw;
    if (y1 - sy < 0)
        r0 = 0;                             /* C truncates toward zero, so a
                                             * negative offset is not -1 */
    if (x1 - sx < 0)
        c0 = 0;
    if (r1 >= C64_ROWS)
        r1 = C64_ROWS - 1;
    if (c1 >= C64_COLS)
        c1 = C64_COLS - 1;
    if (r1 < r0 || c1 < c0)
        return;
    c64_force_rows(r0, r1, c0, c1);
}
