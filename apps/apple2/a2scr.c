/* ============================================================================
 * os8088 - apps/apple2/a2scr.c    the damage model, the flush and the screen
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 7). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING. What a II+ puts on
 * a scan line is reimplemented from MII's src/mii_video.c, apple2emu's
 * src/video.cpp and AppleWin's NTSC_CharSet.cpp; nothing of any of them is
 * vendored.
 *
 * ----------------------------------------------------------------------------
 * WHY ANY OF THIS EXISTS (PERFORMANCE.md, and its rule 1)
 * ----------------------------------------------------------------------------
 * A redraw is priced by how many primitive calls it makes, not by how many
 * pixels it covers: 756 us a gfx_* call on the target. A full 280 x 192
 * repaint is what this file exists to AVOID, and every mechanism in it is one
 * step of narrowing:
 *
 *   a write        -> one bit in a 32-byte page bitmap, and the write window
 *   the window     -> a CELL SPAN within a character row
 *   the span       -> a composed band
 *   the band       -> compared against the frame SHADOW, which is the glass
 *   what differs   -> merged into runs and blitted, once per run
 *
 * THE SHADOW IS THE GLASS AND NOT THE MODEL. If a composed line equals the
 * shadow, nothing is drawn, whatever the model believed.
 *
 * ----------------------------------------------------------------------------
 * WHAT IS DIFFERENT FROM THE C64, AND IT IS THE ADDRESSING
 * ----------------------------------------------------------------------------
 * C64 rows are linear, which makes "window intersect pages -> rows" two
 * divisions. APPLE II ROWS ARE INTERLEAVED (section 7.2): character row i
 * lives at `1024 + 256*((i/2)%4) + 128*(i%2) + 40*((i/8)%4)`, so a dirty
 * 256-byte page maps to a SCATTERED set of scan lines and not to a run.
 *
 * THIS FILE MAPS ROW-WARD RATHER THAN ADDRESS-WARD, which is the same rule
 * read from the other end and is cheaper: MII walks an address to a line
 * through `group = (a>>7)&7`, `gline = (a & 0x7f)/40` with a screen-hole test
 * `(a & 0x7f) > 0x77`; this walks the 24 rows and asks whether the dirty
 * pages and the write window reach each one's forty bytes. The SCREEN HOLE
 * TEST IS THEN IMPLICIT AND EXACT - the eight bytes at the end of each
 * 128-byte group belong to no row's range, so a write there marks nothing -
 * and it costs 24 range tests instead of an arithmetic chain per write.
 * ==========================================================================*/

/* ==========================================================================
 * THE STATE
 * ========================================================================*/

/* ==========================================================================
 * THE COST COUNTERS - -DA2_HOST only (section 16.6)
 *
 * Nothing in apps/apple2/*.c reads one, the Makefile's smlrcc line never
 * defines A2_HOST, and hosttest/a2uitest.c is their only reader. The build
 * fails loudly on any increment left unguarded, which is the point.
 * ========================================================================*/
#ifdef A2_HOST
static unsigned a2_n_blit, a2_n_fill, a2_n_scroll, a2_n_run, a2_n_cell;
static unsigned a2_n_band, a2_n_group, a2_n_span, a2_n_sig, a2_n_take;
/* ...and the k-row shift test's own PROBES, which the harness reads: the
 * flush's per-line compare goes through the same a2_rowspan, so the two
 * cannot be told apart from outside and the miss path's cost - the whole
 * subject of A2_SHIFT_PROBES - would be unmeasurable. */
static unsigned a2_n_probe;
#endif


/* THE FRAME SHADOW: 40 x 192 = 7,680 bytes, exactly the pixels last blitted.
 * It is bss and not a claim because THE FLUSH CANNOT REFUSE. */
static unsigned char a2_sh[A2_SHBYTES];

/* One composed band: eight pixel rows of one character row. Its letterbox
 * bytes (0-1 and 37-39) are never written by the composer and the loader
 * zeroed them, so the letterbox is dark, is part of every blit and every
 * compare, and costs no second code path (a2band.inc's header). */
static unsigned char a2_bnd[A2_BSTRIDE * 8];

/* THE DOUBLED BAND (section 7.8), and it is bss rather than a heap claim
 * because THE FLUSH CANNOT REFUSE - the same reason the frame shadow is. It
 * is 1,280 bytes: sixteen rows of A2_X2STRIDE, which is one character row's
 * eight scan lines doubled on both axes. 2x HORIZONTAL reads the same buffer
 * at twice the stride, which is what makes one routine serve both axes.
 *
 * It is LEVER 1 of section 15.4 if the budget ever needs it - a claim taken
 * at the fullscreen LATCH, where a refusal is legal - and the size line at
 * the end of this wave is what says whether it does. */
static unsigned char a2_x2b[A2_X2STRIDE * 16];

/* THE DIRTY SET, one BIT per scan line - 192 of them in 24 bytes each.
 *   a2_lnd  the SOURCES changed: recompose this line
 *   a2_lnf  the GLASS is unknown here: draw it whatever the compare says
 * Two flags meaning two different things. Setting both on a switch write that
 * changed nothing is the C64's measured 25 forced full-width blits, ~234 ms
 * (APPLE2-SPEC section 7.7). */
static unsigned char a2_lnd[A2_SCRH / 8];
static unsigned char a2_lnf[A2_SCRH / 8];

/* ...and one byte per CHARACTER ROW saying whether anything in it FLASHES.
 * Filled by a2_rowflash at compose time, read by a2_flash_force on a phase
 * flip. A row that was not recomposed did not change its sources either, so
 * its byte is still true. */
static unsigned char a2_flrow[A2_ROWS];

/* THE SOURCE SHADOW, for the k-row scroll test: this frame's forty source
 * bytes per character row, and THE FORTY THE GLASS WAS COMPOSED FROM.
 *
 * IT IS FORTY BYTES A ROW AND NOT A SIXTEEN-BIT SIGNATURE, and the reason is
 * that the test now carries the DRAW DECISION and not just the scroll. A
 * verified shift CLEARS a2_lnd for the rows that moved (the flush's own
 * comment), so a row that compares equal is never composed again - and a hint
 * that decides what is not drawn is an oracle whether or not it is called
 * one. The old signature was `xor al, b / rol ax, 1`, which is LINEAR over
 * GF(2) and has a 320-bit domain in a 16-bit range: two cells SIXTEEN APART
 * changing by the same XOR delta cancel exactly, which is not a one-in-65,536
 * accident but a shape an ordinary text screen makes (a table redrawn, a bar
 * of dashes overwritten by stars). Forty bytes compared is EXACT, and it is
 * also CHEAPER than what it replaces: a2_zcopy_out of a row is ~0.17 ms
 * against a2_rowsig's 0.673, and the k-loop's compare is a2_rowspan's
 * 0.31 ms (APPLE2-SPEC section 7.9.1).
 *
 * 1,920 bytes of bss for a test that was 96 - and the flush it saves is
 * 406 ms (section 7.9.1's one-row scroll row). */
static unsigned char a2_src[A2_ROWS * A2_COLS];
static unsigned char a2_shsrc[A2_ROWS * A2_COLS];
static int a2_sig_ok;                       /* a2_src[] was filled THIS flush */
static int a2_src_r0;                       /* ...from THIS row up, and below
                                             * it a2_src[] holds nothing */

/* a2_dirty_take's target: the 32-byte page bitmap then the window's two
 * words, in one call. */
static unsigned char a2_dpg[36];
static unsigned a2_wlo, a2_whi;
/* ...and THE WATCH RANGE the core took that window over, kept on this side
 * too. It is the LIVE DISPLAY PAGE and nothing else, so in a MIXED graphics
 * mode the four TEXT rows are OUTSIDE it: their page bits still arrive, and a
 * window taken over the hi-res page says nothing whatever about them. A row
 * outside the range is therefore marked from the page bitmap alone and
 * composed WHOLE, which is a2_scan_range's else arm. */
static unsigned a2_wat_lo, a2_wat_hi;
/* ...and PER CHARACTER ROW, "the write window says nothing useful about THIS
 * row this flush: compose it whole".
 *
 * IT IS PER ROW AND NOT PER FLUSH, which is apps/c64/c64scr.c's c64_rowd
 * shape. One flush-wide flag is set by the row that needed it and read by all
 * 24: a phase flip on the row holding the `]` cursor made every other row's
 * compose full width as well, and a2_dirty_scan's empty-window arm did the
 * same for a PAGE2 switch. Twenty-four bytes of bss buy the narrowing back
 * for every row that did not ask for it - 2.434 ms a group on the target. */
static unsigned char a2_rowwide[A2_ROWS];

/* THE FORCED BAND-BYTE RANGE (apps/c64/c64scr.c's c64_blank_rect, which takes
 * a COLUMN span for this reason). A forced row is composed and drawn WHOLE -
 * all forty bytes, letterbox included - because the glass over it is unknown;
 * but "unknown" is only true of the pixels the damage rect actually covered.
 * A menu closing over this window is a rect about 190 px wide (MENU_MAXCH is
 * 24 glyphs), and forcing those rows full width composed 40 cells where 24
 * was the answer. The C64 measured that pair at 122 ms against 75.
 *
 * It is ONE range for the flush rather than one per row: a2_blank_rect unions
 * every rect into it, anything that forces a row for a reason the rect model
 * cannot narrow widens it back to the whole band, and a2_flush resets it on
 * the way out. */
static int a2_fx0, a2_fx1;                  /* band bytes, 0..A2_BSTRIDE-1 */
static int a2_fx_any;                       /* ...and whether a RECT set it */

/* --- the geometry, in one place (section 7.1) ----------------------------- */
static int a2_gox, a2_goy;                  /* the content box's origin */
static int a2_gw, a2_gh;                    /* ...and its live size */
static int a2_gsx, a2_gsy;                  /* the band's top-left ON THE GLASS */
/* THE MAGNIFICATION, AND IT IS DECIDED IN ONE PLACE (section 7.8): a2_geom,
 * off the LIVE content box, so the tier table is arithmetic about the box
 * rather than a list of adapters. 1 or 2 on each axis, and 1 everywhere but
 * full screen. */
static int a2_scw = 1, a2_sch = 1;
static int a2_gbw, a2_gbh;                  /* ...and what the band therefore
                                             * measures ON THE GLASS: 320 or
                                             * 640 wide, a2_gnl or twice it
                                             * tall. Every fill, rect and
                                             * scroll reads these two and not
                                             * A2_BANDW */
static int a2_gsty;                         /* the status row's y */
static int a2_gl0;                          /* the FIRST Apple scan line that
                                             * is on the glass - THE CURSOR
                                             * ANCHOR (section 7.1) */
static int a2_gnl;                          /* ...and how many of them fit */
static int a2_gl0_moved;                    /* ...and it MOVED, so every line
                                             * of the glass now shows a
                                             * different Apple line and the
                                             * shadow describes none of them
                                             * (a2_flush consumes this) */

/* --- the flash phase (section 7.6) ---------------------------------------- */
/* 16 frames at 60 Hz is ~267 ms, which is ~5 host ticks at 18.2 Hz. */
#define A2_FLASH_TICKS 5
static int a2_fl_ok = 1;                    /* the tier allows the phase.
                                             * WAVE 3 WRITES THE TIER TABLE
                                             * from tests/a2band's measured
                                             * numbers and refuses the phase
                                             * on CPU_8086 with the MEASURED
                                             * cost in the greying - refusing
                                             * on a guess would be the guess
                                             * SPEC.md 47 forbids */
static int a2_fl_phase;                     /* 0 normal, 1 swapped */
/* ...AND THE PHASE THE SHADOW WAS COMPOSED AT. The frame shadow is pixels and
 * the SOURCE shadow is forty bytes a row; neither records the second input
 * a2_band_text takes, which is this. One int, read by the scroll's clean and
 * written once a flush - see a2_flush. */
static int a2_sh_phase;
/* ...AND THE MODE IT WAS COMPOSED AT, which is the same hole one input along
 * and the one APPLE2-SPEC section 7.7 step 2 said the wave that adds a
 * composer owns. a2_shsrc[] records forty SOURCE bytes and nothing else, so
 * equal sources prove equal pixels only while the RENDERER that turned them
 * into pixels is the same one. A lo-res screen and a text screen can hold
 * byte-for-byte identical rows, and the shift test would then mark rows clean
 * over pixels composed by the other composer. The key carries the renderer,
 * MIXED and PAGE2, is written once at the end of every flush, and the shift
 * test is refused on the one flush after a change - which is exactly the
 * flush a2_dirty_all has already marked every row of, so every a2_shsrc row
 * is rewritten inside it. */
static int a2_sh_mkey = -1;
static unsigned a2_fl_tick;

/* --- the flush's own pacing ----------------------------------------------- */
static unsigned a2_fltick;                  /* the tick the last flush ran on */
static int a2_flushed;                      /* ...and whether one ever did */

/* --- the status row (section 9) ------------------------------------------- */
#define A2_STCELLS (A2_CONT_W / 8)          /* 42 cells of 8 pixels */
static char a2_st_now[A2_STCELLS + 1];      /* what the row should say... */
static char a2_st_glass[A2_STCELLS + 1];    /* ...and what is ON THE GLASS, so
                                             * a field whose text has not
                                             * changed is not redrawn */
static char a2_st_tmp[A2_STCELLS + 1];
static int  a2_st_ok;                       /* the row on the glass is ours */
/* a2_st_dirty - "...and it wants redrawing" - is declared in apple2.c with the
 * rest of the state the parts share, because a2io.c sets it (a video switch
 * that changed nothing on the glass still moves the MIXED field) and a2io.c is
 * #included ABOVE this file. */
static char a2_msg[A2_STCELLS + 1];
static char a2_pctbuf[8];                   /* the speed field's digits and
                                             * its `%` (section 9) */
static unsigned a2_msg_until;
static int  a2_blit_said;                   /* the blit1 refusal was named once */

/* --- the About panel's two facts, read by the flush ----------------------- */
/* They are declared in apple2.c above every #include, because this file reads
 * them and a2about.c writes them. */

/* ==========================================================================
 * THE MESSAGE AREA
 * ========================================================================*/
/* a2_say - put a line on the status row for five seconds.
 *
 * EVERY LITERAL PASSED TO THIS IS WALKED BY apps/apple2/build.sh AGAINST THE
 * ROW'S CELL CAP, with an explicit expected MINIMUM (section 9), so a corpus
 * of zero literals is a failure and not a pass. */
static void a2_say(const char *s)
{
    os88_strcpy(a2_msg, s, sizeof(a2_msg));
    a2_msg_until = os88_ticks() + 5 * 18;
    a2_st_dirty = 1;
}

/* ==========================================================================
 * THE DIRTY SET
 * ========================================================================*/
#define A2_LBIT(l) (0x80 >> ((l) & 7))

static void a2_line_dirty(int line)
{
    if (line < 0 || line >= A2_SCRH)
        return;
    a2_lnd[line >> 3] |= (unsigned char)A2_LBIT(line);
    a2_dirty_any = 1;
}

static void a2_line_force(int line)
{
    if (line < 0 || line >= A2_SCRH)
        return;
    a2_lnd[line >> 3] |= (unsigned char)A2_LBIT(line);
    a2_lnf[line >> 3] |= (unsigned char)A2_LBIT(line);
    a2_dirty_any = 1;
}

static int a2_line_is(const unsigned char *m, int line)
{
    return m[line >> 3] & A2_LBIT(line);
}

static void a2_row_dirty(int row)
{
    int s, l;

    l = (int)A2_X8(row);
    for (s = 0; s < 8; s++)
        a2_line_dirty(l + s);
}

/* a2_force_wide - the forced band-byte range is the WHOLE band again, and no
 * rect owns it. Everything that forces a row for a reason a damage rect
 * cannot describe calls this. */
static void a2_force_wide(void)
{
    a2_fx0 = 0;
    a2_fx1 = A2_BSTRIDE - 1;
    a2_fx_any = 0;
}

static void a2_dirty_all(void)
{
    int i;

    for (i = 0; i < A2_SCRH / 8; i++)
        a2_lnd[i] = 0xFF;
    a2_dirty_any = 1;
    for (i = 0; i < A2_ROWS; i++)
        a2_rowwide[i] = 1;                  /* the window cannot narrow a
                                             * wholesale mark */
    a2_force_wide();
}

/* a2_dirty_split - THE MIXED SPLIT'S OWN ROWS, and nothing else (section 7.4).
 * Flipping MIXED inside a GRAPHICS mode moves the renderer for rows
 * A2_MIXROW..23 and for NO other row: a2_row_mode(r) for r < A2_MIXROW does
 * not read a2_v_mixed at all, a2_row_base does not move and neither does the
 * page. a2_dirty_all() there would recompose twenty rows from identical
 * sources with the identical composer to produce identical pixels - priced
 * off section 7.9.2, ~417 ms of hi-res compose plus 192 span compares to draw
 * four rows that owed ~70 - which is the defect a2io.c's else-arm diagnoses
 * for MIXED-in-TEXT, one condition along on the arm where the switch really
 * does something. POKE -16302,0 / POKE -16301,0 in a graphics mode is
 * ordinary, and a program that flips the split per frame paid it every frame.
 *
 * BOTH MARKS ARE NEEDED. a2_row_dirty is explicit rather than left to
 * a2_dirty_scan, because the rows arriving from the OTHER page - the text
 * page on MIXED-on, the graphics page on MIXED-off - may have no page bit set
 * at all; and a2_rowwide is needed because the write window was taken over a
 * page it says nothing about, so it may not narrow the compose.
 *
 * IT IS SAFE FOR THE SHIFT TEST for the same reason a2_dirty_all is: a2_shsrc
 * is written only for TEXT rows and the test is refused unless a2_v_text, and
 * every transition INTO a2_v_text moves a2_mode_of() and so takes the full
 * arm. */
static void a2_dirty_split(void)
{
    int r;

    for (r = A2_MIXROW; r < A2_ROWS; r++) {
        a2_row_dirty(r);
        a2_rowwide[r] = 1;
    }
    a2_st_dirty = 1;
}

/* ovl_a2_dirty_range - THE ROWS A BLOCK MOVE ACTUALLY REACHED, and no others
 * (section 7.5).
 *
 * File > Load Program writes [$0801, $0801+plen) straight into the RAM claim
 * with a2_zzcopy_in, which goes round the core's own write path and so sets
 * no page bit and no write window - the mark has to be made by hand. It was
 * a2_dirty_all(), and that is a2_dirty_split's defect one call along: a
 * five-line listing is ~45 bytes and, with text page 1 or either hi-res page
 * on the glass, NOT ONE DISPLAYED BYTE MOVED - so all 192 lines were marked,
 * all 24 rows widened, and the next flush recomposed the whole page at full
 * width to produce the identical pixels. That is ~301 ms of compose on the
 * target (section 7.9.2) for nothing, and it is reachable at LAUNCH: a cold
 * double-click of a `.BAS` runs the same loader, so 301 ms was the first
 * thing that happened after the wait for `]`.
 *
 * THE TEST IS PER ROW AND PER SCAN LINE, because that is how the source is
 * laid out: a text or lo-res row is ONE forty-byte range at a2_row_base(r),
 * and a hi-res row is EIGHT of them $400 apart (section 7.2) - which is the
 * same distinction a2_dirty_scan makes, for the same reason. So a load
 * displayed on text page 2 marks the four rows $0801 lands in, a load long
 * enough to reach $2000 marks the hi-res lines it reached, and the ordinary
 * listing marks nothing at all.
 *
 * a2_rowwide GOES WITH THE MARK for a2_dirty_split's reason: the write window
 * says nothing about a range that never went through it, so it may not narrow
 * the compose. a2_force_wide is called ONLY IF SOMETHING WAS MARKED - it
 * widens the forced band-byte range, and doing that for a load nothing shows
 * of would hand the next flush a full-width band for free.
 *
 * `ovl_` because it runs once per File > Load Program (section 15.0.2), and
 * it ANSWERS 1 on LESSONS.md 5's rule that an overlay function answers a
 * status and 0 means it did not happen. Nothing outside the module reaches it
 * today, so nothing can be told 0 - but the rule is what the next reader
 * copies, and ovl_a2_wr16 carries the same note one file along. */
static int ovl_a2_dirty_range(unsigned lo, unsigned hi)
{
    int r, s, any;
    unsigned b, l;

    any = 0;
    for (r = 0; r < A2_ROWS; r++) {
        b = a2_row_base(r);
        if (a2_row_mode(r) == A2_MODE_HIRES) {
            for (s = 0; s < 8; s++) {
                l = b + ((unsigned)s << 10);
                if (l <= hi && l + (A2_COLS - 1) >= lo) {
                    a2_line_dirty((int)A2_X8(r) + s);
                    a2_rowwide[r] = 1;
                    any = 1;
                }
            }
        } else if (b <= hi && b + (A2_COLS - 1) >= lo) {
            a2_row_dirty(r);
            a2_rowwide[r] = 1;
            any = 1;
        }
    }
    if (any)
        a2_force_wide();
    return 1;
}

/* a2_sh_inval - the glass is unknown EVERYWHERE. The kernel has painted the
 * window's background over whatever was there, or a panel has owned it. */
static void a2_sh_inval(void)
{
    int i;

    for (i = 0; i < A2_SCRH / 8; i++) {
        a2_lnd[i] = 0xFF;
        a2_lnf[i] = 0xFF;
    }
    a2_sh_ok = 0;
    a2_dirty_any = 1;
    a2_st_ok = 0;
    a2_st_dirty = 1;
    a2_border_dirty = 1;
    for (i = 0; i < A2_ROWS; i++)
        a2_rowwide[i] = 1;
    /* THE SOURCE SHADOW IS EMPTIED, not left stale. Nothing compares it while
     * a2_sh_ok is 0 - every row is composed and rewrites its own forty bytes -
     * but a row that has NEVER been composed (one wholly above a2_gl0 on a
     * clipped band) would carry the previous geometry's bytes into the first
     * shift test after the window grew. The old signature array was zeroed
     * here for exactly this reason. */
    for (i = 0; i < A2_ROWS * A2_COLS; i++)
        a2_shsrc[i] = 0;
    a2_force_wide();
}

/* a2_flash_force - the phase flipped: force every scan line whose character
 * row holds a byte in $40-$7F (section 7.6).
 *
 * This is the ONE thing on the glass the damage model cannot see. Flashing
 * changes pixels with no memory write, so nothing write-driven will ever mark
 * these lines and the `]` cursor would simply never blink. */
static void a2_flash_force(void)
{
    int r;

    for (r = 0; r < A2_ROWS; r++)
        if (a2_flrow[r]) {
            a2_row_dirty(r);
            /* AND THE WRITE WINDOW SAYS NOTHING ABOUT WHICH CELLS FLASHED, so
             * it may not narrow the compose. a2_row_dirty marks lines and
             * nothing else, so [a2_wlo,a2_whi] went on clamping the group
             * span: a cursor flashing at row 11 cell 2 while the machine
             * wrote row 11 cell 30 in the same slice gave g0 = g1 = 3, the
             * compare never looked at the cursor's group, and the line was
             * then marked clean - a cursor that stops blinking exactly while
             * a program is printing, which is the whole of an Applesoft
             * session. a2_dirty_all sets every row's flag for the same
             * reason. What it
             * costs is a full-width COMPOSE of the flashing rows on flip
             * flushes; the DRAW stays narrow, because the span compare still
             * decides what is blitted.
             *
             * AND IT IS THIS ROW'S FLAG AND NOT THE FLUSH'S. One flush-wide
             * flag was set by the row holding the `]` and read by all 24, so
             * a blinking cursor composed every OTHER dirty row full width
             * too - 2.434 ms a group it never needed. */
            a2_rowwide[r] = 1;
        }
}

/* a2_any_flash - does ANYTHING on the glass flash? Read by a2_wants_wake:
 * a phase flip that changes no pixel is not work, so the handler must not
 * re-post itself for one. 24 byte tests, against the ~1,400 wake round trips
 * a second the alternative spends (apple2.c). */
static int a2_any_flash(void)
{
    int r;

    for (r = 0; r < A2_ROWS; r++)
        if (a2_flrow[r])
            return 1;
    return 0;
}

/* a2_watch_page - the write window is taken over the LIVE DISPLAY PAGE and
 * nothing else (section 7.5). Without it the window degenerates inside one
 * slice: a JSR writes the stack at $01xx and a BASIC statement writes zero
 * page, so one slice leaves the window spanning the whole page and the
 * per-row intersection always answers "all forty cells". */
static void a2_watch_page(void)
{
    unsigned base = (unsigned)a2_mode_page();

    /* ...AND ITS LENGTH MOVES WITH THE MODE: 1KB of text or lo-res, 8KB of
     * hi-res (section 7.2). A hi-res page watched as 1KB would leave seven
     * eighths of the picture with no window at all. */
    a2_wat_lo = base;
    a2_wat_hi = base + (unsigned)a2_page_len() - 1;
    a2_watch_set(a2_wat_lo, a2_wat_hi);
}

/* a2_row_watched - is this row's forty bytes inside the range the write
 * window was taken over? A row group lives wholly within one page - forty
 * text bytes inside 1KB, eight hi-res scan lines inside 8KB - so the base
 * answers for the whole of it. */
static int a2_row_watched(int r)
{
    unsigned b = a2_row_base(r);

    return (b >= a2_wat_lo && b <= a2_wat_hi) ? 1 : 0;
}

/* a2_dirty_scan - the page bitmap and the write window in ONE call, mapped
 * ROW-WARD onto scan lines (this file's header says why). */
/* a2_scan_range - ONE forty-byte source range: is it dirty, and can the write
 * window narrow it?
 *
 * `watched` says whether the window was taken over a range that CONTAINS this
 * one. A MIXED screen's four text rows are the case that is not: the window
 * is on the hi-res page, so it can say nothing about a write to $0400, and
 * the row is marked from the page bitmap alone and composed WHOLE. */
static void a2_scan_range(int r, unsigned base, int line0, int nlines,
                          int watched)
{
    unsigned last;
    int p0, p1, s;

    last = base + A2_COLS - 1;
    p0 = (int)(base >> 8);
    p1 = (int)(last >> 8);
    if (!(a2_dpg[p0 >> 3] & (0x80 >> (p0 & 7)))
        && !(a2_dpg[p1 >> 3] & (0x80 >> (p1 & 7))))
        return;                             /* neither of the range's pages
                                             * was written */
    if (watched && a2_wlo <= a2_whi) {
        /* ...and the window has to REACH the range, or the page bit was set
         * by a write somewhere else in the same 256 bytes - the stack, zero
         * page, a variable. That is the whole point of the window. */
        if (a2_whi < base || a2_wlo > last)
            return;
    } else {
        /* an EMPTY window (a2_dirty() marks a page with no write at all,
         * which is what a PAGE2 switch needs), or a range the window was not
         * taken over: nothing here can narrow the compose, and it is THIS
         * row's flag, so the rows a real window did narrow keep their span */
        a2_rowwide[r] = 1;
    }
    for (s = 0; s < nlines; s++)
        a2_line_dirty(line0 + s);
}

static void a2_dirty_scan(void)
{
    unsigned base;
    int r, s, w;

    a2_dirty_take(a2_dpg);
    a2_wlo = (unsigned)a2_dpg[32] | ((unsigned)a2_dpg[33] << 8);
    a2_whi = (unsigned)a2_dpg[34] | ((unsigned)a2_dpg[35] << 8);
    /* AN EMPTY WINDOW - WLO above WHI - means nothing was written since the
     * last flush. A page bit can still be set: a2_dirty() marks a page with
     * no write at all, which is what a PAGE2 switch needs. So an empty window
     * cannot narrow anything and the rows it marks are marked WHOLE. */
    /* ...AND THE ROW'S THREE QUESTIONS ARE ASKED ONCE. a2_row_watched calls
     * a2_row_base, which calls a2_row_mode; asking watched, base and mode
     * separately was five near calls and three evaluations of the same branch
     * per row, for all 24 rows, on EVERY flush - ~2-3 ms of the target before
     * a single dirty bit is examined, and the one-key flush paid it as fully
     * as a whole repaint. base answers `watched` on its own (a2_row_watched's
     * own body), so it is inlined here rather than called. */
    for (r = 0; r < A2_ROWS; r++) {
        base = a2_row_base(r);
        w = (base >= a2_wat_lo && base <= a2_wat_hi) ? 1 : 0;
        if (a2_row_mode(r) == A2_MODE_HIRES) {
            /* PER SCAN LINE, because a hi-res row group's eight lines are
             * eight SEPARATE forty-byte ranges $400 apart (section 7.2): one
             * HPLOT touches one of them, and marking all eight would compose
             * and compare eight times the pixels the machine wrote.
             *
             * AND THE COMPOSER HONOURS IT. It did not for a wave: the per-line
             * loop in the flush skipped clean lines for the COMPARE and then
             * called a2_band_hires for all eight anyway, so this narrowing
             * delivered the compare and not the compose. The composer takes a
             * scan-line range now (a2band.inc) and the flush hands it the
             * union of this row's dirty-or-forced lines. */
            for (s = 0; s < 8; s++)
                a2_scan_range(r, base + ((unsigned)s << 10),
                              (int)A2_X8(r) + s, 1, w);
        } else {
            a2_scan_range(r, base, (int)A2_X8(r), 8, w);
        }
    }
}

/* ==========================================================================
 * THE TIER (section 7.8)
 * ========================================================================*/
static int a2_tier_slow;                    /* the CPU_8086 tier */

static void a2_tier_init(void)
{
    a2_tier_slow = (os88_cpu() == OS88_CPU_8086) ? 1 : 0;
    /* --- WAVE 3'S TIER TABLE, WRITTEN FROM `make a2bandbench` (section 7.8;
     * every figure below is section 7.9.2's, which is where a bench number
     * lives - nothing here is a second measurement)
     *
     * The measurement is a GROUP at 2.434 ms and a text row at 12.52, taken
     * on the icount harness where one PIT count is 0.359 ms of a real 4.77
     * MHz XT. Three things fall out of it and they are all here:
     *
     *  - THE FLASH PHASE IS REFUSED ON THE CPU_8086 TIER, and the greying
     *    carries the measured cost (section 10.3). A flip force-composes
     *    every row that holds a byte in $40-$7F; the harness measures one at
     *    43.1 ms with two flashing rows, and at 3.64 flips a second that is
     *    157 ms in every second of an 8088 spent on the phase rather than on
     *    the 6502. It is a2_fl_ok that is cleared, which is the same byte
     *    Machine > Flashing text moves, so there is one mechanism and not
     *    two - and a2_menu_state greys the row off the SAME flag.
     *  - THE FLUSH RUNS EVERY OTHER TICK there (os88_onwake), because a full
     *    repaint is 496.8 ms and a tick is 55: the pacing that costs nothing
     *    on a 386 is a queue on an 8088, and the second flush inside one
     *    machine-visible change draws the same picture twice.
     *  - FULL SCREEN IS 1:1 (a2_geom): a2_band_x2 is 29.500 counts - 10.59 ms
     *    for eight rows, which is 253 ms added to a 192-line repaint for
     *    pixels that are twice the size and no more informative. */
    if (a2_tier_slow)
        a2_fl_ok = 0;
}

/* ==========================================================================
 * THE GEOMETRY - AND THE ANCHOR IS A CORRECTNESS REQUIREMENT
 * ========================================================================*/
/* On a 640x200 desktop os88_video().dock_top is 176, so the window is clamped
 * to 137 rows of content and only 111 of the Apple's 192 scan lines fit - so
 * SOMETHING has to be chosen, and which 111 is a correctness question rather
 * than a preference (APPLE2-SPEC section 7.1).
 *
 * IT FOLLOWS THE CURSOR, and the fixed BOTTOM anchor this replaced is what
 * made the port look DEAD on the one adapter class it most needs to work on.
 * The reasoning that shipped was "the `]` cursor line and MIXED's four text
 * rows are both at the bottom", and the first half of that is FALSE of a
 * machine that has not scrolled yet: the Autostart ROM's cold start prints
 * `APPLE ][` on row 0 and leaves the cursor around row 2, so on a 640x200
 * desktop - where a2_gl0 was 81 and the glass began at row 10 - the banner,
 * the prompt and every line of a short session were composed, blitted and
 * landed off the visible band. The window was BLACK on launch, `PRINT 6*7`
 * answered into pixels nobody could see, and the status row went on reading
 * TEXT and 2,000% beside it. It took thirty Returns to scroll the prompt down
 * to row 23 before one character appeared
 * (build/port-shots/cga-diag-returns.png), which is also why wave 3's own CGA
 * evidence looked fine: its `scrolled` shot had run a FOR loop first.
 *
 * So the band holds the CURSOR ROW, which is the one row a reader needs, and
 * it MOVES ONLY WHEN IT HAS TO. CV ($25) is the monitor's own cursor row and
 * is read as RAM; while row CV is wholly inside the band the anchor does not
 * move at all, and when it leaves, the anchor moves the LEAST it can to bring
 * it back - which is what makes an ordinary session pay for it about once.
 * A fresh boot keeps a2_gl0 at 0 and shows rows 0-13; a session that scrolls
 * walks the cursor to row 23 and the anchor arrives at 81, where the old code
 * started, and stays. It costs a full repaint of the band each time it moves
 * (~290 ms on the target for the 14 visible rows), because the shadow is
 * indexed by APPLE scan line and a moved anchor puts every one of them at a
 * different screen y - so `HOME : VTAB 20 : PRINT` in a loop pays it twice an
 * iteration. That is the price of showing the user what they typed, and the
 * shape that pays it is a program the reader is not typing at.
 *
 * IT IS A TEXT-MODE RULE. There is no cursor in lo-res or hi-res, and in
 * MIXED the interactive part is the four text rows at the bottom, so both
 * keep the bottom anchor - which for MIXED is the same row range the cursor
 * would have chosen.
 *
 * `& ~7` on the horizontal offset is not cosmetic: os88_wm_snap put the
 * content origin on a cell boundary and OSAPI_GFX_SCROLL refuses a rect whose
 * x1 or x2+1 is not a multiple of 8, so the band's left edge has to stay on
 * one. Vertically nothing needs it - blit1 and scroll take any y. */
static int a2_geom(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    int d, avail, lim, ngl0, cl0;

    if (os88_wm_geom(win, &sz) < 0)
        return -1;
    os88_wm_content(win, &org);
    a2_gox = org.x;
    a2_goy = org.y;
    a2_gw = sz.w;
    a2_gh = sz.h;
    /* THE STATUS ROW IS ANCHORED TO THE LIVE BOTTOM of the content box, not
     * to a fixed offset from the top: a fixed one puts the row - which is
     * where every refusal is printed - off the glass on a 200-line desktop
     * with nothing saying so. */
    a2_gsty = org.y + sz.h - A2_STATH;

    /* --- SECTION 7.8'S TIER TABLE, IN ONE PLACE, AND IT IS ARITHMETIC ABOUT
     * THE LIVE BOX rather than a list of adapters. 2x is a FULLSCREEN-only
     * thing - a framed window is authored 336 wide and there is nothing to
     * fill - and the two axes are decided separately, because the adapters
     * differ in exactly that way. A WF_FULL window's content IS its frame
     * (kernel/wm.inc's wm_geom), so the box below is the whole screen:
     *
     *   VGA 640x480       640 >= 640 and 454 >= 384  -> 2x both, 640x384
     *   CGA 640x200       640 >= 640, 174 < 384      -> 2x HORIZONTAL only
     *   Hercules 720x348  720 >= 640, 322 < 384      -> 2x horizontal
     *   the CPU_8086 tier                            -> 1:1 centred
     *
     * A CGA pixel is already 2:1, so doubling X alone is what makes the
     * picture the RIGHT SHAPE there rather than half a job; and the 8086 tier
     * is 1:1 because a2_band_x2 measures 10.59 ms for a whole character row,
     * 254 ms on a
     * whole-frame repaint that is already 497 (sections 7.9.1, 7.9.2). */
    avail = a2_gsty - org.y - A2_BORDER * 2;
    if (avail < 0)
        avail = 0;
    a2_scw = 1;
    a2_sch = 1;
    if (a2_full && !a2_tier_slow) {
        if (sz.w >= A2_BANDW * 2)
            a2_scw = 2;
        if (a2_scw == 2 && avail >= A2_SCRH * 2)
            a2_sch = 2;
    }
    a2_gbw = (a2_scw == 2) ? A2_BANDW * 2 : A2_BANDW;

    /* THE BORDER FLOOR IS A 1:1 THING AND MUST NOT SURVIVE INTO 2x
     * (apps/c64/c64scr.c's own note). At 1:1 the framed window is 336 wide
     * and (336 - 320) / 2 IS A2_BORDER, so the floor changes nothing there
     * and only catches a window narrower than the Apple. At 2x on a
     * 640-pixel screen the picture is 640 wide and the margin is 0: forcing
     * 8 would slide it right and clip eight pixels off the last column. */
    d = (sz.w - a2_gbw) / 2;
    if (a2_scw == 1) {
        if (d < A2_BORDER)
            d = A2_BORDER;
    } else if (d < 0) {
        d = 0;
    }
    a2_gsx = org.x + (d & ~7);

    a2_gnl = (a2_sch == 2) ? (avail >> 1) : avail;
    if (a2_gnl > A2_SCRH)
        a2_gnl = A2_SCRH;

    /* THE CURSOR ANCHOR, and this file's header is why it is not the bottom
     * one. `lim` is the lowest legal anchor, so a band that holds the whole
     * frame has exactly one answer and never reads CV at all. */
    lim = A2_SCRH - a2_gnl;
    ngl0 = a2_gl0;
    if (ngl0 > lim)
        ngl0 = lim;                         /* the window grew */
    if (ngl0 < 0)
        ngl0 = 0;
    if (lim > 0) {
        if (a2_v_text) {
            cl0 = a2_rd(A2_CV) & 0xFF;      /* CV: the monitor's own cursor
                                             * row, read AS RAM */
            if (cl0 >= A2_ROWS)
                cl0 = A2_ROWS - 1;          /* a program that put junk in $25 */
            cl0 = (int)A2_X8(cl0);
            /* ...AND WHEN IT HAS TO MOVE, THE CURSOR ROW GOES TO THE BOTTOM
             * OF THE BAND, whichever way the cursor went. Putting it at the
             * TOP when it went up is the obvious other spelling and it is
             * wrong for this machine: what a reader wants beside the cursor
             * is the HISTORY, which is above it. On a cold start CV is 2 and
             * the ROM's `APPLE ][` banner is on row 0, so a top-anchored
             * move hid the banner one row up while showing thirteen blank
             * rows below - and the clamp turns the same arithmetic into
             * "line 0, show everything" for free. */
            if (cl0 < ngl0 || cl0 + 8 > ngl0 + a2_gnl)
                ngl0 = cl0 + 8 - a2_gnl;
            if (ngl0 > lim)
                ngl0 = lim;
            if (ngl0 < 0)
                ngl0 = 0;
        } else {
            ngl0 = lim;                     /* no cursor in lo-res or hi-res,
                                             * and MIXED's four text rows are
                                             * at the bottom anyway */
        }
    }
    if (ngl0 != a2_gl0) {
        a2_gl0 = ngl0;
        a2_gl0_moved = 1;                   /* every visible line now shows a
                                             * different Apple line: a2_flush
                                             * turns this into a2_sh_inval */
    }
    a2_gbh = (a2_sch == 2) ? (a2_gnl << 1) : a2_gnl;
    a2_gsy = org.y + A2_BORDER + (avail - a2_gbh) / 2;
    return 0;
}

/* a2_border - the strips of the content box the band does not cover.
 *
 * It counts the fills it ISSUES and not four every time: over-reporting the
 * border puts phantom milliseconds in every cost row the harness prints. */
static void a2_border_fill(void)
{
    int sbot = a2_gsy + a2_gbh;

    os88_set_color(OS88_BLACK);
    if (a2_gsy > a2_goy) {
        os88_gfx_fill(a2_gox, a2_goy, a2_gox + a2_gw - 1, a2_gsy - 1);
#ifdef A2_HOST
        a2_n_fill++;
#endif
    }
    if (sbot <= a2_gsty - 1) {
        os88_gfx_fill(a2_gox, sbot, a2_gox + a2_gw - 1, a2_gsty - 1);
#ifdef A2_HOST
        a2_n_fill++;
#endif
    }
    if (sbot > a2_gsy) {
        if (a2_gsx > a2_gox) {
            os88_gfx_fill(a2_gox, a2_gsy, a2_gsx - 1, sbot - 1);
#ifdef A2_HOST
            a2_n_fill++;
#endif
        }
        if (a2_gsx + a2_gbw < a2_gox + a2_gw) {
            os88_gfx_fill(a2_gsx + a2_gbw, a2_gsy,
                          a2_gox + a2_gw - 1, sbot - 1);
#ifdef A2_HOST
            a2_n_fill++;
#endif
        }
    }
}

/* a2_blank_rect - a rectangle of the content box whose pixels are no longer
 * ours, from a PARTIAL expose (SPEC.md 11.90's damage rect).
 *
 * WF_OWNBG is set, so the kernel did NOT whiten the content and did not paint
 * anything: what a partial expose costs us is exactly the pixels the rect
 * covers, and the rest of the frame is still on the glass and still described
 * by the shadow. Invalidating the whole shadow instead is the 496.8 ms full
 * repaint of section 7.9.1 - 24 rows composed, 24 blits, five fills
 * and 192 span compares - run under the desktop's gfx lock, so opening and
 * closing a pull-down over the window would stop the desktop for half a
 * second. apps/c64/c64.c's c64_blank_rect is the named precedent.
 *
 * IT TAKES A COLUMN SPAN AS WELL AS A ROW ONE, into the flush-wide forced
 * band-byte range those two words above describe. Without it every damage
 * rect forced whole scan lines and the flush's `rowf` arm widened the compose
 * to all forty cells: a menu closing over this window is a rect about 190 px
 * wide (MENU_MAXCH is 24 glyphs), so thirteen rows were composed at 40 cells
 * where 24 was owed - the pair apps/c64/c64scr.c measured at 122 ms against
 * 75, and about 55 ms of it on every menu close here.
 *
 * The rect is in SCREEN coordinates, as the kernel gives it. */
static void a2_blank_rect(int x1, int y1, int x2, int y2)
{
    int l0, l1, l, sbot, c0, c1, d;

    sbot = a2_gsy + a2_gbh;                 /* one past the band's last line */

    /* the border strips, and only when the rect actually reaches one */
    if (y1 < a2_gsy || y2 >= sbot
        || x1 < a2_gsx || x2 >= a2_gsx + a2_gbw)
        a2_border_dirty = 1;
    if (y2 >= a2_gsty)
        a2_st_ok = 0;                       /* the status row's pixels are no
                                             * longer ours */

    if (y2 < a2_gsy || y1 >= sbot
        || x2 < a2_gsx || x1 >= a2_gsx + a2_gbw)
        return;                             /* the rect misses the band */

    /* THE COLUMNS, IN BAND BYTES, UNIONED WITH ANY EARLIER RECT THIS FLUSH.
     * The letterbox is redrawn wherever the range reaches it, because the
     * range is in BAND bytes and byte 0 is the letterbox's first - the
     * widening in the flush's `rowf` arm is what keeps a group's own bytes
     * whole around it. `d` is clamped before the shift: a rect that starts
     * left of the band gives a negative offset. */
    /* ...AND THE RECT IS IN SCREEN PIXELS, WHICH AT 2x ARE HALF AN APPLE
     * PIXEL EACH. The shadow, the compare and the band bytes are all in Apple
     * pixels (section 7.8: the doubling is at BLIT time and nowhere else), so
     * every screen coordinate that arrives from outside is divided by the
     * magnification here, at the one place they cross. */
    d = x1 - a2_gsx;
    if (d < 0)
        d = 0;
    if (a2_scw == 2)
        d = d >> 1;
    c0 = d >> 3;
    d = x2 - a2_gsx;
    if (d < 0)
        d = 0;
    if (a2_scw == 2)
        d = d >> 1;
    c1 = d >> 3;
    if (c0 > A2_BSTRIDE - 1)
        c0 = A2_BSTRIDE - 1;
    if (c1 > A2_BSTRIDE - 1)
        c1 = A2_BSTRIDE - 1;
    if (!a2_fx_any) {
        a2_fx0 = c0;
        a2_fx1 = c1;
        a2_fx_any = 1;
    } else {
        if (c0 < a2_fx0)
            a2_fx0 = c0;
        if (c1 > a2_fx1)
            a2_fx1 = c1;
    }

    /* ...and the Apple scan lines it covers, in the shadow's coordinates. */
    l0 = y1 - a2_gsy;
    l1 = y2 - a2_gsy;
    if (l0 < 0)
        l0 = 0;
    if (l1 < 0)
        l1 = 0;
    if (a2_sch == 2) {
        l0 = l0 >> 1;
        l1 = l1 >> 1;
    }
    l0 += a2_gl0;
    l1 += a2_gl0;
    if (l0 < a2_gl0)
        l0 = a2_gl0;
    if (l1 > A2_SCRH - 1)
        l1 = A2_SCRH - 1;
    for (l = l0; l <= l1; l++)
        a2_line_force(l);
}

/* a2_about_gone - the About panel is not on the glass any more, and the rows
 * it held are DAMAGE.
 *
 * The three statements are the same three every route out of the panel owes -
 * a click, a key, a geometry change, and an overlay that has stopped
 * answering - so they are written once here rather than three times: the
 * latch down, the hold range EMPTIED (l0 > l1, which is what the flush
 * tests), and the panel's own rect handed to a2_blank_rect. RESIDENT, because
 * a click is a callback and a callback is reached by a near offset. */
static void a2_about_gone(void)
{
    a2_abt_up = 0;
    a2_hold_l0 = 1;
    a2_hold_l1 = 0;
    a2_blank_rect(a2_abt_x, a2_abt_y,
                  a2_abt_x + a2_abt_w - 1, a2_abt_y + a2_abt_h - 1);
}

/* ==========================================================================
 * THE SECOND PATH: A ROW THROUGH THE KERNEL'S OWN FACE (section 7.7)
 *
 * os88.h is explicit - blit1 answers -1 with NOTHING DRAWN, for a broken
 * argument or for a kern_small kernel that carries the slot without the body,
 * "so TEST IT and have a second path". Discarding the answer and updating the
 * shadow anyway is worse than a missing row: the shadow then says the row is
 * on the glass, the span compare answers "nothing changed" on every later
 * flush, and the row is never retried - a permanently blank Apple screen with
 * nothing saying why, which is the one outcome SPEC.md 47 exists to prevent.
 *
 * AND NOTE THE SEVEN-PIXEL CELL HERE TOO. os88_font_run draws on an EIGHT
 * pixel grid, so this path is an APPROXIMATION - a legibility floor for a
 * refusing kernel, not a second correct renderer - and the status row says so
 * the first time it happens. A 40-column Apple row is 320 pixels wide here
 * against the machine's 280, which is exactly the letterbox's width, so the
 * text lands inside the band and nothing spills.
 * ========================================================================*/
static char a2_fbuf[A2_COLS + 1];

static void a2_row_font(int r, int y)
{
    unsigned base;
    int c, b;

    base = a2_row_base(r);
    for (c = 0; c < A2_COLS; c++) {
        b = a2_rd(base + (unsigned)c) & 0x3F;
        if (b < 0x20)
            b += 0x40;                      /* the Apple's screen encoding
                                             * folded to ASCII: $00-$1F is
                                             * @A-Z[\]^_ */
        a2_fbuf[c] = (char)b;
    }
    a2_fbuf[A2_COLS] = 0;
    os88_font_run(a2_gsx, y, a2_fbuf, OS88_WHITE, OS88_BLACK);
#ifdef A2_HOST
    a2_n_run++;
    a2_n_cell += A2_COLS;
#endif
    if (!a2_blit_said) {
        a2_blit_said = 1;
        a2_say("No bands here - text only.");
    }
}

/* ...AND A GRAPHICS ROW HAS NO SECOND RENDERER AT ALL, only a second
 * STATEMENT. os88_font_run letters ASCII; a lo-res block and a hi-res scan
 * line are pixels, and lettering their source bytes would draw forty
 * arbitrary glyphs and call it a picture. So the run's rectangle is filled
 * black - which is what SPEC section 7.7 names as the graphics arm of the
 * refusal - and the status row's `No bands here - text only.` is the fact
 * (SPEC.md 47): on a kernel with no OSAPI_GFX_BLIT1 body this machine has
 * text and nothing else. */
static void a2_row_blank(int x, int y, int w, int rows)
{
    os88_set_color(OS88_BLACK);
    os88_gfx_fill(x, y, x + w - 1, y + rows - 1);
#ifdef A2_HOST
    a2_n_fill++;
#endif
    if (!a2_blit_said) {
        a2_blit_said = 1;
        a2_say("No bands here - text only.");
    }
}

/* ==========================================================================
 * THE STATUS ROW (section 9), DELTA-DRAWN
 * ========================================================================*/
static void a2_st_put(int col, const char *s)
{
    int i;

    for (i = 0; s[i] && col + i < A2_STCELLS; i++)
        a2_st_now[col + i] = s[i];
}

static void a2_status(void)
{
    int i, f, l;

    for (i = 0; i < A2_STCELLS; i++)
        a2_st_now[i] = ' ';
    a2_st_now[A2_STCELLS] = 0;

    /* the video mode, which is the live one and not a guess */
    if (a2_v_text)
        a2_st_put(0, "TEXT");
    else if (a2_v_hires)
        a2_st_put(0, "HIRES");
    else
        a2_st_put(0, "LORES");
    if (a2_v_mixed)
        a2_st_put(6, "MIXED");
    if (a2_v_page2)
        a2_st_put(12, "PG2");               /* `PG2` AND NOT `PAGE2`, AND IT
                                             * IS ARITHMETIC (section 9): the
                                             * message area starts at cell 16
                                             * because `Unable to load
                                             * APPLE2.OVL.` is 26 glyphs and
                                             * the row is 42, so the mode
                                             * fields have cells 0-15. HIRES
                                             * takes 0-4 and MIXED 6-10, which
                                             * leaves 12-15 - four cells, and
                                             * PAGE2 is five */
    /* THE SPEED FIELD (section 9), MEASURED AND NOT A GUESS - the honest-speed
     * posture this port was given at intake. It is the percentage of a
     * 1.02 MHz Apple II that the last one-second window actually ran, folded
     * in a2_speed_fold.
     *
     * IT SITS AT THE RIGHT-HAND END AND YIELDS TO A MESSAGE. The row is 42
     * cells; the mode fields hold 0-15 and the message area starts at 16
     * because `Unable to load APPLE2.OVL.` is 26 glyphs and 16 + 26 = 42
     * exactly. So there is no column a message and a widget can both have,
     * and the choice is which one loses: a message is transient and a speed
     * figure is not news, so the figure is drawn only when the message area
     * is clear. That also keeps the message-length gate's cap at 26 rather
     * than narrowing it to 19 and making the OVL refusal - the one message a
     * user most needs to read whole - the one that would be truncated. */
    if (a2_msg[0]) {
        a2_st_put(16, a2_msg);
    } else if (a2_state == A2_ST_JAM) {
        /* A JAMMED MACHINE IS A PERMANENT ROW STATE, NOT A FIVE-SECOND
         * MESSAGE (section 4.5, and apps/c64/c64.c:1071-1075 one machine
         * along). A2_ST_JAM used to be set and read by nothing here: the
         * sentence went up through a2_say, expired, and the row then said
         * `TEXT` and nothing else about a machine that was dead - a dead
         * machine and an idle one drawn identically, which is the defect the
         * C64 port already found and wrote up. Blanking the SPEED field was
         * right and losing the sentence that explains it was not. It sits
         * ABOVE the A2_ST_RUN arm so the two cannot both draw, and below the
         * message arm so a transient line (the OVL refusal) still wins for
         * its five seconds. */
        a2_st_put(16, a2_jamline);
    } else if (a2_state == A2_ST_RUN) {
        /* `== A2_ST_RUN` AND NOT `!= A2_ST_HALT`. A2_ST_HALT is 0 and is now
         * only the PRE-LAUNCH value - a2_power_on sets A2_ST_RUN inside
         * os88_main, before the window exists, and a2_status only ever runs
         * from a flush - so the old test was always true. What it let through
         * is a JAMMED machine still drawing a speed percentage: the last
         * folded window's figure, frozen because a2_wants_wake stops
         * re-posting, which is SPEC.md 47's "grey a fact, never a guess"
         * inverted - a number about a machine that is not running. The field
         * goes blank instead, and the JAM line in the message area is what
         * says why. */
        os88_utoa((unsigned)a2_pct, a2_pctbuf);
        l = os88_strlen(a2_pctbuf);
        a2_pctbuf[l] = '%';
        a2_pctbuf[l + 1] = 0;
        a2_st_put(A2_STCELLS - (l + 1), a2_pctbuf);
    }

    if (a2_st_ok) {
        f = -1;
        l = -1;
        for (i = 0; i < A2_STCELLS; i++)
            if (a2_st_now[i] != a2_st_glass[i]) {
                if (f < 0)
                    f = i;
                l = i;
            }
        if (f < 0) {
            /* NOTHING CHANGED: the row is not redrawn, which is the whole of
             * "delta-drawn" - AND IT IS CLEAN, which has to be said here.
             * a2_wants_wake reads a2_st_dirty, so leaving it set re-posted
             * the wake for ever: a2_say() handed a string identical to the
             * one already on the glass (two picks of Toggle Fullscreen while
             * another window holds it, two menu picks on a disk with no
             * APPLE2.OVL) recomputed an identical row, took this branch, and
             * spun the shared UI task for the whole five-second life of the
             * message. `f < 0` IS the statement that the row is up to date,
             * which is the same fact the drawn path records below. */
            a2_st_dirty = 0;
            return;
        }
    } else {
        /* THE GLASS OVER THE ROW IS UNKNOWN, SO THE WHOLE STRIP IS ERASED
         * FIRST - AND THE STRIP IS TALLER THAN THE LETTERING.
         *
         * A2_STATH is 10 and os88_font_run draws EIGHT rows, so scan lines
         * a2_gsty+8 and a2_gsty+9 were never painted by anything at all:
         * a2_border_fill stops at a2_gsty-1 and the run stops at a2_gsty+7.
         * With WF_OWNBG the kernel paints no background either, so those two
         * rows kept whatever was under the window - the Disk window's white
         * list at launch, and in FULLSCREEN a white bar the width of the
         * screen, because the run only reaches A2_STCELLS*8 = 336 pixels of a
         * 638-pixel content box. Found on the glass (build/port-shots/
         * wave1-verify-10-strip.png) and invisible to the harness, whose own
         * font_run is eight rows too.
         *
         * ONE FILL, ON THE FULL-REDRAW ARM ONLY. apps/c64/c64scr.c:1014 is
         * the precedent and states the rule: the DELTA path never erases,
         * because font_run arrives in final polarity and an erase there is
         * exactly PERFORMANCE.md rule 2's erase-then-letter pair. */
        os88_set_color(OS88_BLACK);
        os88_gfx_fill(a2_gox, a2_gsty, a2_gox + a2_gw - 1,
                      a2_gsty + A2_STATH - 1);
#ifdef A2_HOST
        a2_n_fill++;
#endif
        f = 0;
        l = A2_STCELLS - 1;
    }
    for (i = f; i <= l; i++)
        a2_st_tmp[i - f] = a2_st_now[i];
    a2_st_tmp[l - f + 1] = 0;
    os88_font_run(a2_gox + f * 8, a2_gsty, a2_st_tmp, OS88_WHITE, OS88_BLACK);
#ifdef A2_HOST
    a2_n_run++;
    a2_n_cell += (unsigned)(l - f + 1);
#endif
    for (i = 0; i < A2_STCELLS; i++)
        a2_st_glass[i] = a2_st_now[i];
    a2_st_glass[A2_STCELLS] = 0;
    a2_st_ok = 1;
    a2_st_dirty = 0;
}

/* ==========================================================================
 * THE K-ROW SCROLL TEST (section 7.7 step 2)
 * ========================================================================*/
/* It is only asked when enough rows are dirty for a scroll to be what
 * happened - a keystroke dirties one row and a 24-row source pass would cost
 * more than the draw it saves - and only in the mode whose sources mean
 * anything.
 *
 * FOUR FIFTHS OF THE ROWS, and it is the C64's measured constant rather than
 * a taste: a scroll dirties the whole page by definition, while a one-row
 * change dirties the rows of the two pages it touches, and at a low threshold
 * the test runs on screens a scroll did not touch and spends its own cost for
 * nothing. It cannot emit a scroll that is WRONG - the compare is exact - but
 * it can spend 24 row reads to be told so. */
#define A2_SHIFT_NUM 4                      /* ...of A2_SHIFT_DEN, and the
                                             * denominator is spelled as a
                                             * shift sum at the one place it
                                             * is used */
#define A2_SHIFT_DEN 5

/* ...AND THE MISS PATH IS BOUNDED, WHICH THE FIRST VERSION WAS NOT.
 *
 * The k loop is `sum(k=1..23) of (24-k)` = 276 forty-byte compares in the
 * worst case, and a compare is 0.31 ms (section 7.9.1's ROWSPAN row, which
 * measures the equal and the differing case at the same 0.875 counts): 87 ms
 * to be told nothing scrolled. The shape that reaches it is ordinary rather
 * than contrived - a screen with a long run of IDENTICAL rows above the
 * content, every row dirty. `HOME : VTAB 20 : PRINT ...` in a loop is exactly
 * that: HOME writes all 24 rows so the 4/5 threshold is met on every
 * iteration, and each k walks the blank run to its end before the first
 * content row breaks it.
 *
 * IT IS THE EQUAL PROBES THAT COST, and that is what sizes the budget rather
 * than any prefilter. A DIFFERING pair is the loop's cheap terminator -
 * `repe cmpsb` stops at the first differing byte - so a hash or signature
 * compared ahead of a2_rowspan can only cheapen the probe that was already
 * cheap: a run of blank rows has EQUAL signatures and would take the full
 * compare anyway. The counter below is asked once a probe, and answering "no
 * shift" early costs only the optimisation.
 *
 * NINETY-SIX, AND A TRUE SHIFT NEVER REACHES IT. Confirming a shift of k
 * costs 24-k probes, at most 23; the k' < k that fail before it are failing
 * on a screen whose content HAS moved, so each breaks in one or two. A k=8
 * scroll is ~30 probes. What the budget refuses is the screen with many
 * identical rows, which is the screen that did not scroll. 96 x 0.31 is
 * 30 ms, a tenth of the ~301 ms whole-page compose the test exists to save. */
#define A2_SHIFT_PROBES 96

/* a2_shift_test - `k` character rows up, or 0. EXACT: forty source bytes a
 * row, compared against the forty the glass was composed from.
 *
 * `r0` IS THE FIRST ROW THE GLASS HAS EVER SHOWN, and it is a parameter
 * rather than 0 because a2_shsrc[] only exists for rows the flush has
 * COMPOSED. On a 640x200 desktop a2_gl0 reaches 81 once a session has
 * scrolled, and the anchor then puts the first TEN character rows entirely off
 * the top of the content box (row 10 is the partial one and is composed);
 * their shadow sources are permanently 0,
 * and comparing them against live memory made the test answer "no shift" on
 * every screen - which is the CGA arm of the very case this routine exists
 * for. */
static int a2_shift_test(int r0)
{
    unsigned base;
    int i, k, ok, probes;

    /* THE ROWS ABOVE r0 ARE NOT READ AT ALL. They have no visible scan line,
     * so the flush never composes one and a2_shsrc[] for them is permanently
     * 0 - reading them is 0.17 ms a row spent comparing a live value against
     * a sentinel it can never equal. On a 640x200 desktop that is TEN of the
     * twenty-four. a2_src[] below r0 is left alone for the same reason: the
     * scroll's own copy reads it from r0 up, and the compose loop asks
     * a2_sig_ok only for rows it composes, which are rows the glass shows. */
    for (i = r0; i < A2_ROWS; i++) {
        base = a2_row_base(i);
        a2_zcopy_out(a2_src + A2_X40(i), base, A2_COLS);
#ifdef A2_HOST
        a2_n_sig++;
#endif
    }
    a2_sig_ok = 1;
    a2_src_r0 = r0;
    probes = 0;
    for (k = 1; k + r0 < A2_ROWS; k++) {
        ok = 1;
        for (i = r0; i + k < A2_ROWS; i++) {
            if (++probes > A2_SHIFT_PROBES)
                return 0;                   /* the budget above: this screen
                                             * is expensive to ask about and
                                             * has not answered yes */
#ifdef A2_HOST
            a2_n_probe++;
#endif
            if (a2_rowspan(a2_src + A2_X40(i), a2_shsrc + A2_X40(i + k),
                           A2_COLS) >= 0) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return k;                       /* what was at row i+k is now at
                                             * row i: the content moved UP */
    }
    return 0;
}

/* ==========================================================================
 * THE COMPOSE SPAN, UNIONED OVER A ROW'S RANGES
 * ========================================================================*/
/* The composer takes ONE group span for a whole row group. In text and lo-res
 * a row is one forty-byte range and the span is the window's intersection
 * with it; in hi-res it is EIGHT ranges $400 apart (section 7.2), and the
 * span is the union of what the window reaches in each of them.
 *
 * IT IS AN INTERSECTION AND NOT A CONTAINMENT, and the difference is the
 * ordinary case rather than an edge one - see the flush's own note where this
 * is called. */
static int a2_sg0, a2_sg1, a2_sgany;

static void a2_span_of(unsigned base)
{
    unsigned last = base + A2_COLS - 1;
    int g0, g1;

    if (a2_whi < base || a2_wlo > last)
        return;
    g0 = (a2_wlo > base) ? (int)((a2_wlo - base) >> 3) : 0;
    g1 = (a2_whi < last) ? (int)((a2_whi - base) >> 3) : A2_GROUPS - 1;
    if (!a2_sgany) {
        a2_sg0 = g0;
        a2_sg1 = g1;
        a2_sgany = 1;
        return;
    }
    if (g0 < a2_sg0)
        a2_sg0 = g0;
    if (g1 > a2_sg1)
        a2_sg1 = g1;
}

/* ==========================================================================
 * THE FLUSH (section 7.7)
 *
 * The lock is held by the caller and only around this, never around a slice.
 *   1. compose the dirty scan lines;
 *   2. test for a whole-frame shift FIRST;
 *   3. otherwise compare each composed line against the shadow and draw only
 *      the differing spans, merged into runs;
 *   4. update the shadow with what was drawn;
 *   5. the border, only when it changed;
 *   6. the status row, delta-drawn.
 * ========================================================================*/
static int a2_run_l0, a2_run_n, a2_run_x0, a2_run_x1, a2_run_row;
/* ...and the CHARACTER ROW the blit1 refusal already drew through the kernel
 * face this flush. It is per ROW and not per RUN, because a2_row_font draws
 * all forty cells: a row that produced three runs drew 120 glyph cells - 108
 * ms on the target - and drew two of them at the RUN's own y, up to seven
 * pixels below where the row belongs, each overwriting the last. -1 is "no
 * row yet", reset at the top of every flush. */
static int a2_font_row = -1;

static void a2_emit(void)
{
    int i, line, s, w, rc, fy, ey;

    if (a2_run_n <= 0)
        return;
    w = a2_run_x1 - a2_run_x0 + 1;
    s = a2_run_l0 - (int)A2_X8(a2_run_row);
    /* THE DOUBLING HAPPENS HERE AND NOWHERE ELSE (section 7.8). Everything
     * above this line - the band, the shadow, the span compare, the source
     * reads - is in APPLE pixels, so no compare path has a second version and
     * nothing else in this file knows the magnification exists. 2x on both
     * axes reads the doubled band at A2_X2STRIDE and 2x HORIZONTAL at twice
     * that, which is the one routine serving both axes.
     *
     * IT IS THE RUN'S OWN RECTANGLE AND NOT THE ROW'S, which is the second
     * half of "at blit time" and the half wave 3 first shipped wrong. It used
     * to sit on the COMPOSE side, next to a2_band_text, charging 10.59 ms to
     * every recomposed row whether or not one pixel was blitted - 24 x 10.59
     * = 253 ms on a flush that draws nothing, and the cases are ordinary: the
     * reset recompose, a mode switch that draws the same picture (section
     * 7.9.3 measures it at ZERO blits), a rect-forced row whose pixels turn
     * out identical, a MIXED flip. Moving it here fixed that and left the
     * other end alone: it still doubled all forty bytes and all eight lines
     * for a run of seven bytes on one line. A keystroke owed 1.85 ms and
     * spent 10.59; a single-scan-line HPLOT owed 0.40 and spent 10.59.
     *
     * PER RUN NEEDS NO LATCH, and that is the point of doing it this way: the
     * runs of a row are DISJOINT rectangles, so doubling each one costs at
     * most their union - the "three runs must not double three times"
     * property the per-row latch kept by hand is now structural. The
     * destination offset is the blit's own, computed once below and once
     * here from the same three terms. */
    if (a2_scw == 2)
        a2_band_x2(a2_x2b + (unsigned)((s << 1) * A2_X2STRIDE)
                          + (unsigned)(a2_run_x0 << 1),
                   a2_bnd + A2_X40(s) + a2_run_x0, w, a2_run_n);
    ey = a2_run_l0 - a2_gl0;
    if (a2_sch == 2)
        ey = ey << 1;
    ey += a2_gsy;
    if (a2_scw == 2) {
        rc = os88_gfx_blit1(a2_x2b + (unsigned)((s << 1) * A2_X2STRIDE)
                                   + (unsigned)(a2_run_x0 << 1),
                            (a2_sch == 2) ? A2_X2STRIDE : A2_X2STRIDE * 2,
                            a2_gsx + (a2_run_x0 << 4), ey,
                            w << 4,
                            (a2_sch == 2) ? (a2_run_n << 1) : a2_run_n);
    } else {
        rc = os88_gfx_blit1(a2_bnd + A2_X40(s) + a2_run_x0, A2_BSTRIDE,
                            a2_gsx + (a2_run_x0 << 3), ey,
                            w << 3, a2_run_n);
    }
#ifdef A2_HOST
    if (rc == 0)
        a2_n_blit++;
#endif
    if (rc != 0) {
        /* THE REFUSAL, TESTED. One font_run for the whole character row - the
         * approximation this file's second-path comment describes - and the
         * shadow is still updated with the COMPOSED band, deliberately: on
         * this path the shadow stops being "the pixels on the glass" and
         * becomes a proxy for THE SOURCES, and what it buys is still true. If
         * the composed band equals the shadow, the sources decode the same
         * way and the font path would draw the identical characters. Without
         * it the row is redrawn on every flush, for ever.
         *
         * ONCE PER ROW AND AT THE ROW'S OWN y. The run is a span of scan
         * lines and the font path draws CHARACTERS, so a second run inside
         * the same row has nothing new to draw and no y of its own to draw
         * it at. */
        if (a2_row_mode(a2_run_row) != A2_MODE_TEXT) {
            a2_row_blank(a2_gsx + ((a2_scw == 2) ? (a2_run_x0 << 4)
                                                 : (a2_run_x0 << 3)),
                         ey,
                         (a2_scw == 2) ? (w << 4) : (w << 3),
                         (a2_sch == 2) ? (a2_run_n << 1) : a2_run_n);
        } else if (a2_font_row != a2_run_row) {
            a2_font_row = a2_run_row;
            fy = (int)A2_X8(a2_run_row);
            if (fy < a2_gl0)
                fy = a2_run_l0;             /* the anchor cut this row's
                                             * top off the glass */
            fy = fy - a2_gl0;
            if (a2_sch == 2)
                fy = fy << 1;
            a2_row_font(a2_run_row, a2_gsy + fy);
        }
    }
    for (i = 0; i < a2_run_n; i++) {
        line = a2_run_l0 + i;
        s = line - (int)A2_X8(a2_run_row);
        a2_rowcopy(a2_sh + A2_X40(line) + a2_run_x0,
                   a2_bnd + A2_X40(s) + a2_run_x0, w);
    }
    a2_run_n = 0;
}

static void a2_flush(void *win)
{
    unsigned base;
    int r, s, line, g0, g1, b0, b1, sp, df, dl;
    int i, k, nd, nf, nb, trust, drew, rowf, ux0, ux1, r0, nvis;
    int rmode, mkey, ls0, ls1;

    if (a2_geom(win) < 0)
        return;
    /* THE ANCHOR MOVED, so the shadow describes NOTHING on the glass: it is
     * indexed by Apple scan line and every one of them is now drawn at a
     * different screen y. a2_geom is called twice a wake - once for the
     * covered test in os88_onwake and once here - so the flag is STICKY and
     * this is the one place that spends it. */
    if (a2_gl0_moved) {
        a2_gl0_moved = 0;
        a2_sh_inval();
    }
    a2_sig_ok = 0;
    a2_src_r0 = A2_ROWS;
    a2_font_row = -1;

    /* --- THE MESSAGE DEADLINE, AT THE TOP, BEFORE ANY BRANCH CAN RETURN.
     * os88_ticks() is a 16-bit 18.2 Hz counter that wraps about once an hour,
     * so the DIFFERENCE is compared and not the values. */
    if (a2_msg[0] != 0
        && (unsigned)(os88_ticks() - a2_msg_until) < 0x8000u) {
        a2_msg[0] = 0;
        a2_st_dirty = 1;
    }

    a2_dirty_scan();

    if (a2_border_dirty || !a2_sh_ok) {
        a2_border_fill();
        a2_border_dirty = 0;
    }

    /* --- the shift test, FIRST --------------------------------------------
     * IT IS NOT GATED ON THE WHOLE FRAME BEING VISIBLE, and that mattered on
     * the one adapter class this port most needs it on. The guard used to be
     * `a2_gnl == A2_SCRH`, which on a 640x200 CGA desktop is never true -
     * dock_top 176 clamps the window, the content box is 137 tall, 111 scan
     * lines fit and a2_gl0 is 81 - so a scrolling Applesoft session, the
     * ORDINARY case, took the span path for every scrolled line: ~14 visible
     * rows each fully composed and blitted, ~210 ms A LINE on a 4.77 MHz
     * 8088 against ~34 for the scroll.
     *
     * Nothing tied it to full visibility but the shadow memcpy below, which
     * shifted all 192 lines: the gfx_scroll rect was ALREADY the visible band
     * and lines under a2_gl0 are never compared or drawn. So the shadow is
     * shifted from a2_gl0 down, and both the row scan and the source compare
     * start at the first row the glass has ever shown - a row that was never
     * composed has no shadow source to compare (a2_shift_test's own
     * header). */
    k = 0;
    r0 = a2_gl0 >> 3;                       /* the first row with any visible
                                             * scan line: 8r+7 >= a2_gl0 */
    nvis = A2_ROWS - r0;
    /* ...AND NOT WHILE ANY VISIBLE LINE'S GLASS IS UNKNOWN. a2_lnf says
     * "somebody else painted here"; gfx_scroll would move that paint UP by k
     * rows while the flag stayed where it was, and the shifted shadow - which
     * holds what we composed, not what the menu drew - then compares EQUAL
     * over the garbage and leaves it on the glass for the rest of the
     * session. It is one scan of 24 bytes against a defect no still
     * screendump taken after the covering window has gone can show. The span
     * path draws those rows whole, which is what a2_lnf is for.
     *
     * IT IS ASKED OF THE VISIBLE LINES ONLY, and that is not tidiness: the
     * lines BELOW a2_gl0 are never drawn, so nothing ever clears their forced
     * bit - a2_sh_inval sets all 192 and the flush's per-line loop skips
     * exactly those - and a whole-array scan therefore answers "unknown" for
     * ever on any clipped band. hosttest/a2uitest.c's CGA row is what said
     * so, in the one word it can: the clipped scroll went back to 70 groups
     * and the harness failed it.
     *
     * IT IS ASKED LAST, AFTER THE DIRTY-ROW THRESHOLD. A keystroke dirties one
     * row and never reaches this scan at all; only a flush that already looks
     * like a scroll pays its ~31 tests. */
    /* THE MODE KEY (this file's a2_sh_mkey): the renderer, MIXED and PAGE2 in
     * one int, so the shift test can ask whether the pixels the shadow holds
     * were made by the same composer as the pixels it is about to prove
     * something about.
     *
     * MIXED IS IN THE KEY ONLY WHERE IT CAN REACH A PIXEL, which is the same
     * statement a2_video_set's else-arm makes: in TEXT mode the split does
     * not exist - a2_row_mode returns A2_MODE_TEXT before it reads
     * a2_v_mixed, a2_row_base does not move, a2_mode_page does not read it -
     * so `POKE -16302,0` marks no row and must not invalidate the shadow
     * either. It used to, and the flush that carried a scroll then refused
     * the shift test over a shadow exactly as valid as it had been a moment
     * before: a 24-row recompose and 24 blits, ~497 ms on the target, instead
     * of one os88_gfx_scroll at ~100. This is EXACT and not a loosening -
     * every transition that makes MIXED visible also moves a2_mode_of(),
     * which is in the key already and takes the a2_dirty_all arm anyway. */
    mkey = a2_mode_of() | ((!a2_v_text && a2_v_mixed) ? 0x10 : 0)
         | (a2_v_page2 ? 0x20 : 0);
    /* ...AND THE SHIFT TEST IS A TEXT-MODE TEST, which is `a2_v_text` and not
     * `!a2_v_hires`: a LO-RES screen has forty source bytes a row too and
     * they scroll like anything else, but they are composed by a different
     * routine and a2_shsrc records no such thing. The test is also refused on
     * the ONE flush after any mode change, because a2_shsrc then describes
     * pixels the other composer drew - and that flush is by construction the
     * one a2_dirty_all marked every row of, so every row of a2_shsrc is
     * rewritten inside it and the flush after is exact again. */
    if (a2_sh_ok && a2_v_text && mkey == a2_sh_mkey && !a2_abt_up
        && nvis > 0) {
        nd = 0;
        for (r = r0; r < A2_ROWS; r++)
            if (a2_line_is(a2_lnd, (int)A2_X8(r)))
                nd++;
        /* `nd * 5` as a shift sum: cc8086 refuses an `imul ax, ax, 5` where
         * it cannot prove a scratch register dead, and a threshold with a
         * five in it is exactly that shape (LESSONS.md 3). The denominator is
         * the VISIBLE row count, so "four fifths of the rows" means four
         * fifths of the rows a user can see. */
        /* `nvis * A2_SHIFT_NUM` is `nvis << 2` because A2_SHIFT_NUM is 4,
         * and hosttest/a2uitest.c is what says the two still agree - the
         * multiplicand is a VARIABLE now, so it is a real multiply rather
         * than the constant fold this used to be. */
        if (nd + (nd << 2) >= (nvis << 2)) {
            nf = 0;
            nb = (a2_gl0 + 7) >> 3;         /* the first WHOLE byte at or
                                             * above a2_gl0 */
            for (line = a2_gl0; line < (int)A2_X8(nb); line++)
                if (a2_line_is(a2_lnf, line))
                    nf = 1;
            for (r = nb; r < A2_SCRH / 8; r++)
                if (a2_lnf[r])
                    nf = 1;
            if (!nf)
                k = a2_shift_test(r0);
        }
    }
    if (k) {
        if (os88_gfx_scroll(a2_gsx, a2_gsy,
                            a2_gsx + a2_gbw - 1, a2_gsy + a2_gbh - 1,
                            (a2_sch == 2) ? ((int)A2_X8(k) << 1)
                                          : (int)A2_X8(k)) == 0) {
#ifdef A2_HOST
            a2_n_scroll++;
#endif
            /* ONLY THE VISIBLE PART OF THE SHADOW MOVES. gfx_scroll moved the
             * pixels inside the band's own rect, which starts at Apple scan
             * line a2_gl0; lines above it are neither compared nor drawn, so
             * shifting them would be work on bytes nothing reads. On a
             * 480-line desktop a2_gl0 is 0 and this is the whole shadow,
             * which is what it always was. */
            os88_memcpy(a2_sh + A2_X40(a2_gl0),
                        a2_sh + A2_X40(a2_gl0 + (int)A2_X8(k)),
                        A2_X40(A2_SCRH - a2_gl0 - (int)A2_X8(k)));
            /* the k vacated rows: gfx_scroll leaves them for us, so they are
             * drawn WHATEVER the compare says - a stale shadow row that
             * happened to match would leave the glass blank */
            for (r = A2_ROWS - k; r < A2_ROWS; r++) {
                for (s = 0; s < 8; s++)
                    a2_line_force((int)A2_X8(r) + s);
                for (s = 0; s < A2_COLS; s++)
                    a2_shsrc[A2_X40(r) + s] = 0;
                a2_rowwide[r] = 1;
            }
            /* a2_flrow[] IS NOT ZEROED HERE, AND THAT IS THE WHOLE OF A
             * DEFECT THIS BLOCK SHIPPED WITH. The shift below reads
             * a2_flrow[i+k] for i+k up to A2_ROWS-1, so zeroing rows
             * A2_ROWS-k..A2_ROWS-1 FIRST hands rows A2_ROWS-2k..A2_ROWS-k-1 a
             * zero whatever they were flashing - row 22 at k=1, rows 8..15 at
             * k=8 - and the same pass then marks them clean, so nothing ever
             * recomposes them and a2_rowflash never rewrites the flag. On the
             * glass that is flashing text which scrolled up out of the bottom
             * k rows and STOPPED FLASHING for the rest of the session, which
             * is precisely the defect the shift below was written to fix,
             * surviving at its own boundary. The vacated rows are zeroed
             * AFTER the shift instead, which costs nothing and keeps both
             * statements true. */
            /* ...AND THE FORCED BAND-BYTE RANGE GOES BACK TO THE WHOLE BAND.
             * gfx_scroll left garbage across the vacated rows' FULL WIDTH, so
             * a range a damage rect had narrowed earlier in the same flush
             * would leave the letterbox and the far cells of those rows
             * holding it. Nothing else in the flush forces a line. */
            a2_force_wide();
            /* --- AND THE SHIFTED ROWS ARE CLEAN, WHICH IS THE WHOLE WIN.
             *
             * The test just PROVED, forty bytes a row, that row i's sources
             * are the ones row i+k's pixels were composed from; gfx_scroll
             * has moved those pixels to row i and the shadow was moved with
             * them. So the glass at row i is already right, and the flush
             * owes it nothing.
             *
             * WITHOUT THIS CLEAR THE SCROLL SAVED THE SCROLL AND NOTHING
             * ELSE. A ROM scroll writes all 23 source rows, so a2_dirty_scan
             * had marked every row and the write window spanned the whole
             * page: the loop below then composed all 24 rows at FULL WIDTH -
             * 120 groups, 292 ms on the target - to discover that 23 of them
             * were byte-identical to the shadow it had just shifted.
             * APPLE2-SPEC section 7.9.1 measured that at 406.2 ms, and
             * 238.1 on the clipped CGA band, against ~210 ms a line for the
             * span path it replaced: 20 x 238 ms is 4.8 s, so on its own
             * numbers the scroll was no better than the thing it was
             * introduced to beat. PERFORMANCE.md rule 5 exactly - the shape
             * of the optimisation survived and the reason did not.
             *
             * ONLY THE VISIBLE LINES ARE CLEARED. Lines below a2_gl0 were not
             * shifted in the shadow (the memcpy starts there), so their
             * shadow is stale; they are never drawn and never compared, and
             * leaving their bits set costs nothing but keeps the shadow's own
             * statement true.
             *
             * a2_lnf IS NOT TOUCHED and does not need to be: the shift is
             * refused outright while any line's glass is unknown. */
            for (i = r0; i + k < A2_ROWS; i++) {
                /* the shifted rows' shadow sources moved with them, and
                 * a2_src[] already holds exactly those bytes */
                a2_rowcopy(a2_shsrc + A2_X40(i), a2_src + A2_X40(i), A2_COLS);
                /* AND THE FLASH FLAGS MOVE TOO. a2_flrow[] is what a phase
                 * flip forces off, and it was correct before this clear only
                 * by accident - every row was being recomposed, so
                 * a2_rowflash rewrote it. The moment the composes stop,
                 * flashing text that has scrolled stops flashing and a row
                 * that no longer flashes is force-composed on every flip. */
                a2_flrow[i] = a2_flrow[i + k];
                if (a2_flrow[i] && a2_sh_phase != a2_fl_phase) {
                    /* --- AND A ROW WHOSE CONTENT FLASHES IS RECOMPOSED,
                     * NEVER MARKED CLEAN, WHEN THE PHASE HAS MOVED UNDER IT.
                     * THE SOURCE SHADOW IS ONLY A PROOF WHILE THE PHASE IS
                     * UNCHANGED.
                     *
                     * a2_band_text takes a2_fl_phase as a SECOND input
                     * (`a2_fl_phase ? 0x7F : 0x00`) and a2_shsrc[] records
                     * the forty SOURCE bytes and nothing else. The shadow row
                     * was composed in an EARLIER frame, so equal sources do
                     * not imply the moved pixels are right for the phase the
                     * flush is composing at now. os88_ontimer flips the phase
                     * and a2_flash_force marks the flashing rows dirty for a
                     * reason no source compare can see; the very next flush
                     * consumes that mark, and during scrolling output that
                     * flush is a SCROLL flush - so the clear below would eat
                     * it and the glass would keep the previous phase. The
                     * next flip composes back at the old phase, the span
                     * compare says equal, nothing is drawn, and only the flip
                     * after that redraws: a scrolled flashing cell holds one
                     * phase for three half-periods, ~825 ms instead of 275.
                     * A cursor stutter during exactly the scrolling output
                     * the scroll path exists for, and invisible in a still
                     * screendump.
                     *
                     * TWO CONDITIONS, AND BOTH ARE NEEDED. A non-flashing
                     * row's pixels are phase-INDEPENDENT, so marking it clean
                     * stays exact whatever the phase did; and a flashing row
                     * on a flush whose phase has NOT moved is already right,
                     * by the invariant below. So the recompose is paid only
                     * on a flush that carries a scroll AND a flip - one or
                     * two rows, ~5 groups each, against the 120 groups the
                     * clean exists to save. On the ordinary scrolling wake it
                     * costs nothing, which is what keeps the one-row scroll
                     * at section 7.9.1's 41.9 ms: the blunt form of this test
                     * (recompose every shifted flashing row, every scroll)
                     * is equally correct and MEASURES 91.1 ms and 20 groups
                     * on that row against 41.9 and 5 - PERFORMANCE.md's
                     * standing budget going backwards, and the harness's own
                     * `the ONE vacated row owes 5` assertion fires on it.
                     *
                     * THE INVARIANT a2_sh_phase KEEPS: at the end of every
                     * flush, every visible row whose a2_flrow is set shows
                     * pixels composed at a2_fl_phase. It holds because a flip
                     * runs a2_flash_force, which marks exactly those rows
                     * dirty, and this is the one place that mark could be
                     * thrown away. A row that BECOMES flashing by being
                     * shifted inherits row i+k's glass, which the invariant
                     * already covers. a2_sh_phase is written once a flush,
                     * AFTER this block reads it; a flush that returns early
                     * leaves it stale, which can only over-recompose.
                     *
                     * THE SAME HOLE OPENED ON MODE, AND WAVE 3 CLOSED IT:
                     * a2_band_lores and a2_band_hires are a third input
                     * a2_shsrc does not record. The gate that ships is
                     * `a2_v_text` - NOT `!a2_v_hires`, which is true of a
                     * lo-res screen and so lets one through - plus one
                     * flush's refusal after any renderer / MIXED / PAGE2
                     * change, keyed on a2_sh_mkey. a2_dirty_all runs on
                     * exactly that change, so every a2_shsrc row is rewritten
                     * inside the refused flush and the flush after is exact
                     * again. The gate itself is below, with its own note. */
                    for (s = 0; s < 8; s++) {
                        line = (int)A2_X8(i) + s;
                        if (line >= a2_gl0)
                            a2_lnd[line >> 3] |= (unsigned char)A2_LBIT(line);
                    }
                    a2_rowwide[i] = 1;      /* a2_flash_force's reason: the
                                             * write window says nothing about
                                             * which cells flashed */
                    continue;
                }
                for (s = 0; s < 8; s++) {
                    line = (int)A2_X8(i) + s;
                    if (line >= a2_gl0)
                        a2_lnd[line >> 3] &= (unsigned char)~A2_LBIT(line);
                }
            }
            /* ...AND NOW the vacated rows' flash flags, after every read of
             * them the shift above makes (the note at the vacated-row loop). */
            for (r = A2_ROWS - k; r < A2_ROWS; r++)
                a2_flrow[r] = 0;
        } else {
            k = 0;                          /* refused: spans, and the shadow
                                             * stays true - nothing moved */
        }
    }

    /* --- compose, compare, draw ------------------------------------------- */
    /* THE PHASE THIS FLUSH COMPOSES AT, recorded HERE - after the shift block
     * has read the previous one and before the first a2_band_text call takes
     * it. It is the second input to the composition and the source shadow
     * does not hold it; see the shift's clean above for the invariant it
     * keeps. */
    a2_sh_phase = a2_fl_phase;
    a2_run_n = 0;
    for (r = 0; r < A2_ROWS; r++) {
        drew = 0;
        rowf = 0;
        /* ...AND THE SCAN-LINE RANGE, in the same pass, because a2_band_hires
         * takes one (section 7.4). `drew` is exactly "this line will be read
         * out of the band below" - dirty, or forced, or the shadow is not
         * trusted at all - so the union of the lines that set it is the
         * union the composer owes, FORCED LINES INCLUDED: the !trust arm
         * below draws b0..b1 straight out of the band with no compare, and a
         * band row this flush never wrote would be last flush's pixels. */
        ls0 = 8;
        ls1 = -1;
        for (s = 0; s < 8; s++) {
            line = (int)A2_X8(r) + s;
            if (line < a2_gl0)
                continue;                   /* the anchor: this line is not
                                             * on the glass */
            if (a2_line_is(a2_lnf, line))
                rowf = 1;
            if (a2_line_is(a2_lnd, line) || a2_line_is(a2_lnf, line)
                || !a2_sh_ok) {
                drew = 1;
                if (s < ls0)
                    ls0 = s;
                ls1 = s;
            }
        }
        if (!drew) {
            /* nothing of this row is both dirty and visible: clear whatever
             * marks it has, so the wake can go idle */
            for (s = 0; s < 8; s++) {
                line = (int)A2_X8(r) + s;
                a2_lnd[line >> 3] &= (unsigned char)~A2_LBIT(line);
                a2_lnf[line >> 3] &= (unsigned char)~A2_LBIT(line);
            }
            continue;
        }

        /* A PANEL OWNS THIS WHOLE ROW: skip it BEFORE composing it. The test
         * used to live in the per-scan-line loop below, which is after
         * a2_band_text has already run for the row - so every one of the
         * fifteen character rows the About panel covers was fully composed
         * and then thrown away, ~182 ms of pure waste on every expose while
         * it is up. apps/c64/c64scr.c puts the identical test at the top of
         * its ROW loop for exactly this reason.
         *
         * BOTH FLAGS STAY SET. The row is still owed a draw when the panel
         * goes, and a2_about_close forces exactly these lines. A row the
         * panel covers only PARTLY keeps the per-line path below. */
        if (a2_abt_up && (int)A2_X8(r) >= a2_hold_l0
            && (int)A2_X8(r) + 7 <= a2_hold_l1)
            continue;

        rmode = a2_row_mode(r);
        base = a2_row_base(r);

        /* THE COMPOSE SPAN, in GROUPS of eight cells - which is what makes it
         * byte-aligned at both ends (a2band.inc's header). */
        /* IT IS AN INTERSECTION AND NOT A CONTAINMENT, and the difference is
         * the ordinary case rather than an edge one: the window is taken over
         * a whole SLICE, and an Applesoft COUT that prints a character and
         * then moves the cursor to the next line writes into TWO display
         * rows. A containment test (`wlo >= base && whi <= last`) fails for
         * BOTH of them and composes all forty cells of each - 24.3 ms where
         * 4.9 was owed, on every such wake. apps/c64/c64scr.c's c64_span_of
         * is the precedent and clamps. */
        g0 = 0;
        g1 = A2_GROUPS - 1;
        if (a2_sh_ok && !a2_rowwide[r] && a2_wlo <= a2_whi
            && a2_row_watched(r)) {
            /* IN HI-RES THE ROW IS EIGHT SEPARATE RANGES and the span is the
             * UNION of what the window reaches in each: the composer takes
             * one group span for the whole row group, so a write on line 3
             * and a write on line 6 give the groups both of them need and
             * nothing wider. a2_span_of is that union, and its `any` is what
             * says the window reached none of them - which cannot happen
             * here, because a2_scan_range only marked the row when it did. */
            a2_sgany = 0;
            if (rmode == A2_MODE_HIRES) {
                for (s = 0; s < 8; s++)
                    a2_span_of(base + ((unsigned)s << 10));
            } else {
                a2_span_of(base);
            }
            if (a2_sgany) {
                g0 = a2_sg0;
                g1 = a2_sg1;
            }
        }
        b0 = A2_LBOXB + (int)A2_X7(g0);
        b1 = A2_LBOXB + (int)A2_X7(g1) + A2_GBYTES - 1;
        if (!a2_sh_ok || rowf) {
            /* THE GLASS IS UNKNOWN OVER THIS ROW, SO THE LETTERBOX IS UNKNOWN
             * TOO AND GOES DOWN WITH THE BAND. Found on the glass and nowhere
             * else: the About panel is exactly the BAND's width, so closing
             * it left two white strips - band bytes 0-1 and 37-39, the 16 and
             * 24 letterbox pixels - and the tail of the panel's own text
             * standing in them, because a forced redraw was still drawing
             * only the seven-byte GROUPS the composer had been asked for. A
             * group span is the right answer for a SOURCE change and the
             * wrong one for a glass that somebody else painted on.
             *
             * AND THE COMPOSE SPAN WIDENS WITH THE DRAW SPAN. Widening b0/b1
             * alone left g0/g1 wherever the write window had just narrowed
             * them, so a2_band_text composed only those groups while the
             * compare and the blit read all forty bytes - and up to 32 of
             * them still held the PREVIOUS character row's pixels, because
             * a2_bnd is ONE buffer reused for every row. That needs a write
             * and a force in the SAME flush, which is what a blinking `]`
             * makes ordinary; the band that goes down whole is composed
             * whole.
             *
             * AND "WHOLE" IS THE FORCED RANGE'S WIDTH, NOT ALWAYS THE BAND'S.
             * a2_blank_rect knows which COLUMNS the damage covered, so a menu
             * closing over 190 px of this window forces the rows it covered
             * over [a2_fx0, a2_fx1] and nothing wider. The byte range is
             * WIDENED to whole groups, because a group is seven bytes of
             * eight seven-pixel cells and a2_band_text cannot compose half of
             * one - and then widened back to the rect's own bytes, so the
             * letterbox bytes the groups do not cover are still in it. Every
             * force the rect model cannot describe (a scroll's vacated rows,
             * a wholesale mark, an invalidated shadow) has already called
             * a2_force_wide, so this is the whole band there. */
            g0 = (a2_fx0 - A2_LBOXB) / A2_GBYTES;
            if (g0 < 0)
                g0 = 0;
            if (g0 > A2_GROUPS - 1)
                g0 = A2_GROUPS - 1;         /* a rect in the RIGHT letterbox
                                             * alone: byte 39 is past the last
                                             * group and (39-2)/7 is 5 */
            g1 = (a2_fx1 - A2_LBOXB) / A2_GBYTES;
            if (g1 > A2_GROUPS - 1)
                g1 = A2_GROUPS - 1;
            if (g1 < g0)
                g1 = g0;
            b0 = A2_LBOXB + (int)A2_X7(g0);
            if (a2_fx0 < b0)
                b0 = a2_fx0;
            b1 = A2_LBOXB + (int)A2_X7(g1) + A2_GBYTES - 1;
            if (a2_fx1 > b1)
                b1 = a2_fx1;
        }

        /* --- THE DISPATCH, AND IT IS THE WHOLE OF WHAT MIXED MEANS HERE
         * (section 7.4). One 40-byte-stride frame, one shadow, one band
         * buffer; which routine fills it is a per-ROW question, because the
         * MIXED split falls on a character-row boundary (160 scan lines is
         * 20 rows) and no row is ever half one renderer and half the other. */
        /* HI-RES IS COMPOSED OVER THE DIRTY SCAN LINES AND NOT ALL EIGHT,
         * which is the other half of a2scr.c's own statement that hi-res is
         * marked ONE LINE AT A TIME. Text and lo-res make all eight pixel
         * rows out of one source byte a cell and have nothing to narrow; a
         * hi-res row group is eight separate 40-byte source rows $400 apart,
         * so composing all eight for a one-line HPLOT was eight times the
         * work the machine asked for - 2.434 ms a group against the 0.30 that
         * was owed, on every row the plot crossed. */
        if (rmode == A2_MODE_HIRES)
            a2_band_hires(a2_bnd, g0, g1, a2_m.ramseg, base,
                          ls0, ls1 - ls0 + 1);
        else if (rmode == A2_MODE_LORES)
            a2_band_lores(a2_bnd, g0, g1, a2_m.ramseg, base);
        else
            a2_band_text(a2_bnd, g0, g1, a2_m.ramseg, base,
                         a2_fl_phase ? 0x7F : 0x00);
        /* ...AND ONLY A TEXT ROW CAN FLASH. A lo-res byte of $60 is two
         * colour blocks and a hi-res byte of $60 is three pixels; asking
         * a2_rowflash about either would force-compose the row 3.6 times a
         * second for a phase that changes not one pixel of it. */
        a2_flrow[r] = (rmode == A2_MODE_TEXT)
                    ? (unsigned char)a2_rowflash(a2_m.ramseg, base, A2_COLS)
                    : (unsigned char)0;
        /* ...and the DOUBLED copy is NOT taken here. It is a2_emit's, on the
         * DRAW side and PER RUN: a row that composes and draws nothing owes
         * no doubling at all, and a row that draws seven bytes of one scan
         * line owes seven bytes of one scan line (section 7.8). */
#ifdef A2_HOST
        a2_n_band++;
        a2_n_group += (unsigned)(g1 - g0 + 1);
        a2_n_cell += (unsigned)((g1 - g0 + 1) * 8);
#endif
        /* THE SHADOW'S SOURCES are updated HERE, with the row that was just
         * recomposed, and nowhere else: a row this flush did not recompose
         * did not change its sources either, so its old forty bytes are still
         * true. And when the shift test has already run this flush it read
         * exactly these bytes - the lock is held throughout - so they are
         * copied rather than fetched again. */
        /* ...AND ONLY FOR A TEXT ROW, because the shift test is a text-mode
         * test: forty source bytes of a hi-res row group describe ONE of its
         * eight scan lines, so recording them would be 0.31 ms a row spent on
         * a proof nothing is allowed to use. A mode change is what makes them
         * true again, through the mkey refusal above. */
        if (rmode == A2_MODE_TEXT) {
            if (a2_sig_ok && r >= a2_src_r0) {
                a2_rowcopy(a2_shsrc + A2_X40(r), a2_src + A2_X40(r), A2_COLS);
            } else {
                a2_zcopy_out(a2_shsrc + A2_X40(r), base, A2_COLS);
#ifdef A2_HOST
                a2_n_sig++;
#endif
            }
        }

        a2_run_row = r;
        for (s = 0; s < 8; s++) {
            line = (int)A2_X8(r) + s;
            if (line < a2_gl0)
                continue;
            if (a2_abt_up && line >= a2_hold_l0 && line <= a2_hold_l1) {
                /* a panel owns these lines: drawing them would be drawing
                 * under something opaque, and the click that closes it
                 * forces exactly them */
                a2_emit();
                continue;
            }
            trust = a2_sh_ok && !a2_line_is(a2_lnf, line);
            if (!a2_line_is(a2_lnd, line) && trust)
                continue;
            if (!trust) {
                df = b0;
                dl = b1;
            } else {
                sp = a2_rowspan(a2_bnd + A2_X40(s) + b0,
                                a2_sh + A2_X40(line) + b0, b1 - b0 + 1);
#ifdef A2_HOST
                a2_n_span++;
#endif
                if (sp < 0) {
                    /* THE COMPOSED LINE EQUALS THE GLASS: nothing is drawn -
                     * and the line is CLEAN, which has to be recorded here or
                     * it stays dirty for ever and is recomposed on every
                     * flush from now on. Found by the host harness: a row
                     * that had been drawn once went on composing itself in
                     * every later wake, and the only symptom was a cost row
                     * reading ten groups where the window had narrowed the
                     * work to five. */
                    a2_lnd[line >> 3] &= (unsigned char)~A2_LBIT(line);
                    a2_emit();              /* a clean line breaks the run */
                    continue;
                }
                df = b0 + ((sp >> 8) & 0xFF);
                dl = b0 + (sp & 0xFF);
            }
            a2_lnd[line >> 3] &= (unsigned char)~A2_LBIT(line);
            a2_lnf[line >> 3] &= (unsigned char)~A2_LBIT(line);

            /* THE RUN MERGE. A blit is 756 us of floor whatever it covers, so
             * consecutive lines go down in ONE call - and the rule for
             * extending is that the union may never cost more BYTES than two
             * separate blits would: `union <= this + next`. A full repaint is
             * then one blit per character row rather than one per scan line,
             * which is 24 calls against 192. */
            if (a2_run_n > 0 && line == a2_run_l0 + a2_run_n) {
                ux0 = (df < a2_run_x0) ? df : a2_run_x0;
                ux1 = (dl > a2_run_x1) ? dl : a2_run_x1;
                if (ux1 - ux0 + 1
                    <= (a2_run_x1 - a2_run_x0 + 1) + (dl - df + 1)) {
                    a2_run_x0 = ux0;
                    a2_run_x1 = ux1;
                    a2_run_n++;
                    continue;
                }
            }
            a2_emit();
            a2_run_l0 = line;
            a2_run_n = 1;
            a2_run_x0 = df;
            a2_run_x1 = dl;
        }
        a2_emit();
    }

    a2_dirty_any = 0;
    a2_sh_ok = 1;
    a2_sh_mkey = mkey;                      /* what the glass was composed BY,
                                             * beside a2_sh_phase, which is
                                             * what it was composed AT */
    for (r = 0; r < A2_ROWS; r++)
        a2_rowwide[r] = 0;
    /* ...AND THE FORCED RANGE IS THE NEXT FLUSH'S TO NARROW, not this one's
     * to leave behind. Every line the range described has been drawn; a rect
     * that arrives before the next flush starts the union again. */
    a2_force_wide();
    a2_status();
    a2_flushed = 1;
    a2_fltick = os88_ticks();
}

/* ==========================================================================
 * THE FOREIGN VIDEO MODE - COLOUR (APPLE2-SPEC section 13)
 * ========================================================================*/
/* SPEC.md 53's exclusive bracket, which is a DIFFERENT thing from the
 * OSAPI_FULLSCREEN latch above it: there the app is a window the size of the
 * screen and the desktop's mode, primitives, cursor and event ladder all stay
 * live; here the app borrows the machine and the video mode is its own.
 *
 * **THIS IS WHERE THE PORT IS IN COLOUR, AND IT IS THE ONLY PLACE IT CAN BE.**
 * A II+ makes fifteen colours out of a 280 x 192 raster and the windowed path
 * has a 1bpp band: no width and no cleverness gets artifact colour onto a
 * monochrome surface, and SPEC.md 47 says a fact and not a promise, which is
 * why Machine > Color NTSC greyed with `The window is monochrome.` for four
 * waves. FSXM_VGA13 is 320x200x256 with the Apple's 280x192 centred at
 * (20, 4), ONE BYTE A PIXEL, and MII's "Color NTSC" palette straight into the
 * DAC.
 *
 * ----------------------------------------------------------------------------
 * WHAT THE BENCH SAID, AND WHAT IT CUT (section 13.4, `make a2bandbench`)
 * ----------------------------------------------------------------------------
 * FSXM_CGA640 and FSXM_HERC were a SPEED claim - the Apple's native
 * monochrome geometry, whole, on a machine whose window can only show 111 of
 * its 192 scan lines - and the wave measured them before writing them.
 * **THEY LOST AND THEY ARE CUT**: 695.4 ms a frame against the windowed
 * path's 632.6, and 34.8 ms a character row against 26.4. The reason is not
 * the raster the SPEC argued about: both paths compose with a2_band_text and
 * double with a2_band_x2, byte for byte the same, and what differs is the
 * EMIT - one os88_gfx_blit1 of 640x16 at 8.875 counts against sixteen
 * a2_fsx_put compares at 2.0 each. A band that is going down WHOLE does not
 * need a span compare, and the kernel's blit is the cheaper call.
 *
 * So on CGA and Hercules this row greys with that number, and colour is a
 * statement about a VGA-class machine - which is what section 13.1 says
 * rather than implying an XT gets colour.
 *
 * ----------------------------------------------------------------------------
 * THE COST, MEASURED, AND WHY THE FRAME IS DIRTY-LINE DRIVEN
 * ----------------------------------------------------------------------------
 * A whole VGA13 frame is 3,365 ms on a 4.77 MHz 8088 and one character row is
 * 140. A per-frame raster write would pay the first of those numbers sixty
 * times a second, which is what the dirty-page bitmap, the write window, the
 * scan-line map and the span compare exist to avoid (PERFORMANCE.md Part 5).
 * So a foreign frame is driven off THE SAME a2_lnd/a2_lnf SET THE WINDOWED
 * FLUSH COMPUTES, against a foreign-frame shadow in a heap claim: an ordinary
 * keystroke composes and compares eight scan lines and answers "nothing
 * moved" for the other 184 at 1.08 ms each.
 * ========================================================================*/
#define A2_FSXW      280                    /* the Apple's raster, one byte a
                                             * pixel. It is a2fsx.inc's
                                             * A2_FSXW and the mirror check in
                                             * hosttest/a2uitest.c is what says
                                             * the two still agree */
#define A2_FSX13_W   320                    /* ...inside mode 13h's frame */
#define A2_FSX13_H   200
#define A2_FSX13_X   20                     /* (320 - 280) / 2 */
#define A2_FSX13_Y   4                      /* (200 - 192) / 2 */
#define A2_FSX_KB    53                     /* 280 x 192 = 53,760 bytes */
#define A2_FSX_BYTES 53760u                 /* ...and the byte count itself,
                                             * written out rather than as
                                             * A2_SCRH * A2_FSXW: 192 x 280
                                             * does not fit the SIGNED int
                                             * this C folds constants in, and
                                             * a silent -11,776 handed to a
                                             * `rep stosb` is a CX of 53,760
                                             * anyway - right by accident on
                                             * this one and not a thing to
                                             * leave in the file */

static int a2_fsx_id = -1;                  /* the FSXM_* this display can
                                             * give, or -1 for none */
/* a2_fsx_up - whether we are INSIDE the bracket, which is what fences the
 * drawing slots reachable from a slice - is declared in apple2.c above every
 * #include, because THAT is the file that reads it (a2_jam, a2_spk_service)
 * and this one that writes it. */
static int a2_fsx_ok;                       /* the foreign shadow describes
                                             * the foreign glass */
static unsigned a2_fsx_tick;                /* the tick the last foreign frame
                                             * ran on - a2_fltick's twin, and
                                             * read only while a2_fsx_ok is 1
                                             * (the first frame of a session
                                             * is forced and stamps it) */
static unsigned a2_fsx_sh;                  /* the shadow's claim... */
static unsigned a2_fsx_seg;                 /* ...and the framebuffer's */
static int a2_fsx_stride;
static void *a2_fsx_win;
static int a2_fsx_told;                     /* the CPU_8086 tier's price, said
                                             * once a run (a2_fsx_enter) */
static unsigned char a2_fsxbuf[A2_FSXW];    /* the composed scan line, which
                                             * is the compare's left side.
                                             * bss and not a claim because it
                                             * is one line and is spent inside
                                             * one call; the SHADOW is 53,760
                                             * bytes that outlive the frame */

/* a2_fsx_avail - IS THERE A FOREIGN COLOUR MODE ON THE DISPLAY THIS WINDOW IS
 * ON? A FACT and not a guess (SPEC.md 47): os88_fsx_caps answers the bitmask
 * of ids that are settable on THAT display, and fsx_mode refuses on the same
 * bit - one predicate for the greying and for the refusal.
 *
 * IT IS ASKED WHERE THE ANSWER IS USED and never banked, because on a
 * two-display desktop (SPEC.md 39.18.2) this is a question about a DISPLAY
 * and a window moves between them: an answer taken in os88_main describes the
 * window we were LAUNCHED from, which does not exist yet. */
static int a2_fsx_avail(void *win)
{
    static int kind;                        /* an out-parameter, so a STATIC:
                                             * `&local` is a stack offset
                                             * dereferenced through the
                                             * package segment (SPEC.md 73.5) */
    int mask;

    a2_fsx_id = -1;
    if (win == 0)
        return 0;
    /* THERE IS NO `mask < 0` TEST HERE AND THERE MUST NOT BE ONE. It stood
     * for a wave and could never fire: os88_fsx_caps is the one of the four
     * slots that does NOT answer through CF - SPEC.md 53.4 makes it callable
     * from any context, lock held or not, precisely so a mode row can be
     * greyed BEFORE a bracket is entered - so the thunk returns the MASK
     * verbatim, the widest mask SPEC.md 53.4 defines is VGA's 0x1EF, and bit
     * 15 is never set. A guard that cannot fire reads as a refusal being
     * handled and is not one; "no foreign mode here" is a mask of 0, which
     * the test below answers correctly on its own. */
    mask = os88_fsx_caps(win, &kind);
    if (mask & (1 << OS88_FSXM_VGA13))
        a2_fsx_id = OS88_FSXM_VGA13;
    /* ...AND THERE IS NO SECOND ARM HERE. FSXM_CGA640 and FSXM_HERC were cut
     * on the bench (this file's header), so a CGA or a Hercules answers -1
     * and the row greys with the measured number rather than entering a mode
     * that is slower than the window it came from. */
    return (a2_fsx_id >= 0) ? 1 : 0;
}

/* a2_fsx_frame - ONE foreign frame, off the dirty-line set.
 *
 * It is a2_flush's own loop with TWO substitutions and NOTHING ELSE: the
 * composer writes 280 palette indices instead of 40 band bytes, and the emit
 * is a2_fsx_put instead of os88_gfx_blit1. a2_dirty_scan, the per-ROW mode
 * dispatch, the group span, the flash phase's mask, the flash flag, the
 * per-line marks and - since the wave-5 review - THE ROW'S EARLY-OUT are all
 * a2_flush's, taken line for line rather than re-derived. That last one was a
 * THIRD substitution nobody meant to make: this loop ran the whole row
 * prologue for all 24 rows before discovering that 23 of them had no marked
 * line, where a2_flush computes `drew` first and returns out. Section 13.2.
 *
 * IT IS ALSO GATED BY ITS CALLER, which is the other half of a2_flush's own
 * shape: a2_fsx_main reads a2_wrote() and calls this only when something is
 * owed, exactly as os88_onwake does.
 *
 * THE WINDOWED SHADOW IS NOT UPDATED HERE and must not be: a2_sh describes
 * PIXELS ON THE DESKTOP'S GLASS and the desktop is not on the glass. The
 * marks this loop consumes are the windowed flush's too, so the exit
 * invalidates a2_sh outright - which costs one full repaint, once, against
 * carrying a second damage model for the length of a fullscreen session. */
static void a2_fsx_frame(void)
{
    int r, s, line, rmode, rowf, c0, c1, nc, nb, drew, ls0, ls1;
    unsigned base, fboff, shoff, off;

    a2_dirty_scan();
    for (r = 0; r < A2_ROWS; r++) {
        /* --- ONE PASS OVER THE EIGHT LINES, AND IT IS FIRST. THAT ORDER IS
         * a2_flush's OWN (its `drew`/`rowf` pass at the top of the row) AND
         * IT WENT MISSING HERE, which is the third substitution the header
         * above claims there are only two of.
         *
         * The prologue below - a2_row_mode, a2_row_base, the span predicate,
         * a2_row_watched and up to eight a2_span_of calls, the c0/c1/off/nb
         * arithmetic - is between fifteen and twenty-three near calls, and it
         * ran for ALL TWENTY-FOUR ROWS before the per-line loop discovered
         * that twenty-three of them had not one marked line. On the ordinary
         * change - one character row dirty - that is ~5 ms in text and ~10 in
         * hi-res of a 4.77 MHz 8088 spent proving nothing moved, against the
         * 15.7 / 31.2 ms of real work the narrowing was written to buy.
         *
         * `ls0`/`ls1` are the same pass's other answer, a2_flush's again: the
         * range of scan lines that will be read, so the loop below starts at
         * the first marked line instead of at 0. Its own per-line test stays,
         * because a gap inside the range is legal (a HPLOT touching lines 0
         * and 7 marks neither of the six between them). */
        rowf = 0;
        drew = 0;
        ls0 = 8;
        ls1 = -1;
        for (s = 0; s < 8; s++) {
            line = (int)A2_X8(r) + s;
            if (a2_line_is(a2_lnf, line))
                rowf = 1;
            if (!a2_fsx_ok || a2_line_is(a2_lnd, line)
                || a2_line_is(a2_lnf, line)) {
                drew = 1;
                if (s < ls0)
                    ls0 = s;
                ls1 = s;
            }
        }
        if (!drew) {
            /* THE WHOLESALE MARK IS STILL SPENT ON THE SKIPPED PATH, or the
             * narrowing never re-engages: a2_rowwide[] is set by the damage
             * model and by the scroll's vacated rows, and a row that leaves
             * it standing composes full width for the rest of the session. */
            a2_rowwide[r] = 0;
            continue;
        }
        rmode = a2_row_mode(r);
        base = a2_row_base(r);

        /* --- THE COLUMN SPAN, WHICH IS a2_flush's OWN AND NOT A SECOND ONE.
         * a2_dirty_scan has just filled a2_wlo/a2_whi, a2_rowwide[] and the
         * page bitmap for this frame, and the first version read NONE of it:
         * every row composed all forty cells and every line compared all 280
         * bytes. On the ordinary change - a COUT writing one or two cells -
         * that is 8 x 15.39 ms of compose and 8 x 2.15 of compare against the
         * ~16 ms the edit could have moved, in the one mode whose per-line
         * cost is the highest this port has (section 13.2).
         *
         * The predicate is a2_flush's, term for term with a2_fsx_ok standing
         * in for a2_sh_ok, so the two paths cannot narrow differently: a
         * shadow that describes the glass, a row the write window is allowed
         * to speak for, a window that is not empty, and a row on the watched
         * page. `rowf` is the fourth term the windowed path spells as
         * `!a2_sh_ok || rowf` - a line somebody else painted over is a glass
         * this row knows nothing about, so it goes down whole. */
        c0 = 0;
        c1 = A2_COLS - 1;
        if (a2_fsx_ok && !rowf && !a2_rowwide[r] && a2_wlo <= a2_whi
            && a2_row_watched(r)) {
            a2_sgany = 0;
            if (rmode == A2_MODE_HIRES) {
                for (s = 0; s < 8; s++)
                    a2_span_of(base + ((unsigned)s << 10));
            } else {
                a2_span_of(base);
            }
            if (a2_sgany) {
                c0 = a2_sg0 * 8;
                c1 = a2_sg1 * 8 + 7;
                /* ...AND HI-RES PADS BY ONE CELL EACH SIDE, which the 1bpp
                 * band does not and must not. Artifact colour is decided by a
                 * pixel's NEIGHBOURS (a2fsx.inc's eleven-bit window), so a
                 * write inside cell k moves pixels in cells k-1 and k+1; the
                 * windowed composer has no artifact colour at any width and
                 * owes no such padding. Without this the span would be
                 * correct for the source and wrong for the picture, and the
                 * wrong pixel would then be recorded in the shadow and stay
                 * for the session. */
                if (rmode == A2_MODE_HIRES) {
                    if (c0 > 0)
                        c0--;
                    if (c1 < A2_COLS - 1)
                        c1++;
                }
            }
        }
        nc = c1 - c0 + 1;
        /* A CELL IS SEVEN BYTES, AND SEVEN IS 8 - 1. `c0 * 7` is an
         * `imul ax, ax, 7`, which tools/cc8086.py refuses on an 8086 for want
         * of a provably dead scratch register (docs/C-TOOLCHAIN.md); the
         * shift-and-subtract is what a2fsx.inc's own prologue does with the
         * same number. */
        off = ((unsigned)c0 << 3) - (unsigned)c0;
        nb = (nc << 3) - nc;

        for (s = ls0; s <= ls1; s++) {
            line = (int)A2_X8(r) + s;
            if (a2_fsx_ok
                && !a2_line_is(a2_lnd, line) && !a2_line_is(a2_lnf, line))
                continue;
            if (rmode == A2_MODE_HIRES)
                a2_fsx_row(a2_fsxbuf, 2, a2_m.ramseg,
                           base + ((unsigned)s << 10), 0, 0, c0, nc);
            else if (rmode == A2_MODE_LORES)
                a2_fsx_row(a2_fsxbuf, 1, a2_m.ramseg, base, s, 0, c0, nc);
            else
                a2_fsx_row(a2_fsxbuf, 0, a2_m.ramseg, base, s,
                           a2_fl_phase ? 0x7F : 0x00, c0, nc);
            fboff = (unsigned)(A2_FSX13_Y + line) * (unsigned)a2_fsx_stride
                  + A2_FSX13_X + off;
            shoff = (unsigned)line * A2_FSXW + off;
            a2_fsx_put(a2_fsx_seg, fboff, a2_fsx_sh, shoff,
                       a2_fsxbuf + off, nb);
            a2_lnd[line >> 3] &= (unsigned char)~A2_LBIT(line);
            a2_lnf[line >> 3] &= (unsigned char)~A2_LBIT(line);
        }

        /* --- THE FLASH FLAG IS MAINTAINED HERE, EXACTLY AS a2_flush DOES IT.
         * a2_flrow[] is written at COMPOSE time and read by a2_flash_force,
         * and a2_flush is the only other place that composes - so for a wave
         * the whole bracket ran on flags taken before it was entered. Three
         * things came of it, none visible in an emulator: entering colour
         * from a hi-res screen (the natural thing, colour being the point)
         * left every flag zero, so a program returning to TEXT had a `]` that
         * never blinked again; entering from text and scrolling left the flag
         * on a row the cursor had moved off, so the cursor froze AND the
         * wrong eight lines were recomposed 3.64 times a second for ever; and
         * entering from text and running HGR recomposed eight stale text rows
         * as hi-res on every flip - ~480 ms of every second of a 4.77 MHz
         * 8088 spent proving to a2_fsx_put that nothing had changed.
         *
         * ROWS THIS FRAME DID NOT COMPOSE KEEP THEIR FLAG, which is correct
         * here for the reason it is correct in a2_flush: a row nothing wrote
         * holds the same source bytes and therefore the same answer. */
        /* ...and it is UNCONDITIONAL here, where it used to be `if (drew)`:
         * the row that composed nothing took the `continue` above and never
         * reaches this line, so the two statements are the same one. */
        a2_flrow[r] = (rmode == A2_MODE_TEXT)
                    ? (unsigned char)a2_rowflash(a2_m.ramseg, base, A2_COLS)
                    : (unsigned char)0;
        a2_rowwide[r] = 0;                  /* ...and the wholesale mark is
                                             * spent, or the narrowing above
                                             * would never engage again after
                                             * the entry's own a2_sh_inval */
    }
    a2_fsx_ok = 1;
    a2_fsx_tick = os88_ticks();             /* ...and a2_flush's a2_fltick,
                                             * for the pacing in a2_fsx_main */
    a2_dirty_any = 0;
    a2_force_wide();                        /* a2_flush's own last two lines,
                                             * for a2_flush's own reason: the
                                             * forced range belongs to the
                                             * NEXT frame to narrow */
}

/* ==========================================================================
 * a2_fsx_main - THE BRACKET, AND THIS COMMENT IS ITS RULE LIST
 * ==========================================================================
 * NOTHING IN THE TOOLCHAIN ENFORCES ANY OF THE SEVEN RULES BELOW (SPEC.md
 * 53.7, APPLE2-SPEC section 13.3), which is why the whole session lives in
 * ONE function and the list lives on top of it. Every one of them is obeyed
 * by hand and each is named where it is obeyed:
 *
 *  1. THE ENTRY IS A PLAIN RESIDENT FUNCTION WHOSE ADDRESS IS TAKEN, and
 *     never an `ovl_`: tools/cc8086.py refuses that address BY NAME, because
 *     the bracket has parked the machine that would load the module. This
 *     function, a2_fsx_frame, a2_fsx_key and everything they call are in
 *     a2scr.c, a2io.c, a2kbd.c, a2band.inc, a2cpu.inc and a2fsx.inc - all
 *     resident (APPLE2-SPEC section 15.5).
 *  2. AFTER THE FIRST os88_fsx_mode EVERY DRAWING SLOT IS OFF-LIMITS until
 *     this returns: they render DESKTOP geometry into a foreign framebuffer.
 *     Nothing below calls one - and `a2_fsx_up` (apple2.c, beside a2_abt_up)
 *     is what fences the one path that could, a2_jam's os88_toast, which is
 *     reachable from the slice. IT IS READ, in a2_jam and in a2_spk_service's
 *     two say-once latches; a fence a rule list describes and no line of code
 *     tests is a sentence, and this one was exactly that for a wave.
 *     a2_say and a2_menu_state are the two other things the slice can reach
 *     from in here and NEITHER DRAWS: a2_say copies a string and stamps a
 *     deadline, a2_menu_state assigns item pointers the kernel reads when a
 *     pull-down is next opened. They are legal, and what they are not is
 *     VISIBLE - which is why the say-once latches are deferred rather than
 *     spent (a2_snd_fact's own note).
 *  3. KEYS COME FROM A POLLED int 16h (a2_fsx_key, a2fsx.inc) and never from
 *     the event ladder: NO EVENTS ARE DISPATCHED in here, the queue simply
 *     fills and is drained by the kernel at exit. Which means every latch the
 *     wake would spend has to be spent in the loop below by hand: the flash
 *     phase and THE RESET CHORDS, both named where they are spent.
 *  4. THE MOUSE WOULD COME FROM os88_mouse AND THIS BRACKET READS NONE. An
 *     Apple II+ has no mouse, the kernel's pointer is parked for the whole
 *     session (the gfx lock is held from before fsx_run to after it), and the
 *     only input this machine owes is its own keyboard and the two exit
 *     chords. The rule is written here so the next bracket does not reach for
 *     the event queue instead.
 *  5. FRAMES ARE PACED WITH os88_fsx_wait AND NEVER os88_task_sleep, which
 *     degenerates to an immediate return because nothing else is eligible.
 *  6. NOTHING TOUCHES PIT CHANNEL 0, THE SOUND PORTS OR AN int 10h MODE SET.
 *     The DAC is programmed directly and that is legal and stated: SPEC.md
 *     53.7 gives the app the video hardware while a foreign mode is up, and
 *     the exit mode set reprograms everything a2_fsx_dac touched.
 *  7. THE EXIT IS THIS FUNCTION RETURNING, on Ctrl+F and Alt+Enter - which
 *     are THIS PORT'S fullscreen chords (section 6.3) and not SPEC.md
 *     11.2.1's bare `f` and Esc, for 11.2.1's own stated exception: the Apple
 *     II+ owns both of those keys. `f` is a letter and every letter goes to
 *     the machine; Esc is the Monitor's and the Applesoft screen editor's.
 *     A port that swallowed either would be a machine you cannot type at, in
 *     the one mode whose whole point is looking at it.
 *
 * AND THE SPEAKER STAYS LIVE, which is rule 6 read the right way round: the
 * snd slots are legal throughout (SPEC.md 53.7) and a2_spk_service is one far
 * call on a change. A machine that went silent the moment it went to colour
 * would be a worse machine.
 * ========================================================================*/
static void a2_fsx_main(void)
{
    static char fsi[OS88_FSI_SIZE];
    unsigned k;                             /* int 16h's AX, and UNSIGNED: see
                                             * the test below */

    if (os88_fsx_mode(a2_fsx_id, fsi) < 0)
        return;                             /* refused, and nothing was drawn:
                                             * the desktop's mode never
                                             * changed and there is nothing to
                                             * put back */
    /* The block is BYTES and is read as bytes: a C struct over it would have
     * to promise nasm's alignment, and two 8-bit reads are what the machine
     * does anyway. */
    a2_fsx_seg = (unsigned)(unsigned char)fsi[OS88_FSI_SEG]
               | (((unsigned)(unsigned char)fsi[OS88_FSI_SEG + 1]) << 8);
    a2_fsx_stride = (int)((unsigned)(unsigned char)fsi[OS88_FSI_STRIDE]
                   | (((unsigned)(unsigned char)fsi[OS88_FSI_STRIDE + 1]) << 8));
    a2_fsx_up = 1;
    a2_fsx_ok = 0;                          /* no line may be skipped on the
                                             * first pass: all 192 are owed */
    /* ...AND THE SHADOW IS MADE TRUE RATHER THAN LEFT UNKNOWN, which is a
     * different statement and the one that matters. SPEC.md 53.4 is binding
     * that the mode set CLEARS the screen, so a zeroed shadow describes the
     * glass exactly and every compare from here on is sound.
     *
     * `a2_fsx_ok = 0` alone is not enough and this shipped believing it was.
     * It defeats the per-LINE skip above; the defect is one level down, in
     * a2_fsx_put's per-BYTE compare, which has no idea the frame it is
     * comparing against was thrown away. The heap gives this 53KB claim back
     * with whatever was in it, and after a free and a same-size claim that is
     * very often the LAST session's shadow - so the compare answered "nothing
     * moved" and wrote nothing, leaving those lines as the mode set left
     * them. SEEN ON THE GLASS: entering Machine > Color NTSC a second time on
     * an unchanged hi-res screen drew three of its six lines and a truncated
     * pair of verticals. It is also the cheaper arm - the black parts of the
     * picture are already black and are not written. */
    a2_fsx_zero(a2_fsx_sh, 0, A2_FSX_BYTES);
    /* THE WINDOWED SHADOW IS A LIE FROM THIS INSTRUCTION ON, AND IT IS SAID
     * HERE RATHER THAN AT THE EXIT (section 13.3). The desktop's glass is
     * gone and the frames below consume the a2_lnd/a2_lnf marks the windowed
     * flush would have used, so a2_sh describes pixels nobody can see.
     *
     * SAYING IT AT THE EXIT COST A SECOND WHOLE-WINDOW REPAINT. SPEC.md 53.6
     * step 4 runs a full wm_paint_all BEFORE fsx_run returns, and os88_paint
     * answers a whole-window W_PAINT with exactly this call plus one flush -
     * 192 lines composed and blitted, the border and the status row, ~633 ms
     * of a 4.77 MHz 8088. Invalidating again AFTER that put every line mark
     * back on pixels the kernel's own paint had just made correct, and the
     * next wake composed and blitted all 192 of them a second time, for
     * nothing. Here the paint that is already owed finds a2_sh_ok = 0, does
     * the one repaint, and leaves the shadow describing the glass; and it
     * costs the bracket nothing, because the first foreign frame writes all
     * 192 lines whatever these marks say.
     *
     * IT IS PAST THE MODE SET, so the arm that never entered a mode never
     * pays it: a refused os88_fsx_mode returns above with the desktop
     * untouched, and a refused os88_fsx_run never reaches this function at
     * all. a2_sh_inval calls no drawing slot - it is marks, flags and one
     * memset of the source shadow - so rule 2 is intact. */
    a2_sh_inval();
    a2_fsx_dac();                           /* MII's palette into the DAC */
    for (;;) {
        k = a2_fsx_key();
        /* THE SENTINEL IS 0xFFFF AND THE TEST IS UNSIGNED, and this ONE
         * COMPARE was the whole of a defect that shut one of the two doors
         * rule 7 pins. `int` is 16 bits here, so an AX whose scan code has
         * bit 7 set is a NEGATIVE int: AH=0xA6 - Alt+Enter in the ENHANCED
         * set, which is the very code the comment below says is taken rather
         * than guessed - is k = -22528, and `if (k >= 0)` threw it away four
         * lines above the branch that reads it. The KSC_ALT_ENTER arm was
         * dead code, and Alt+0/-/= (AH 0x81/0x82/0x83) were dropped with it.
         * The shim always answered 0xFFFF in AX; nothing about a2fsx.inc
         * changed. AND NO HARNESS COULD SEE IT: a host `int` is 32 bits, so
         * 0xA600 is positive there - which is why hosttest/a2uitest.c now
         * queues 0xA600 and asserts the bracket exits on it, a row that fails
         * against the signed test and passes against this one. */
        if (k != 0xFFFFu) {
            /* Ctrl+F is ASCII 6 on every BIOS; Alt+Enter is scan 0x1C in the
             * classic set and 0xA6 in the enhanced one, and BOTH are taken
             * rather than one being guessed - os88_onkey's own pair, tested
             * the same way one input path along. */
            if ((k & 0xFF) == 6
                || ((k & 0xFF) == 0
                    && (((k >> 8) & 0xFF) == KSC_ENTER
                        || ((k >> 8) & 0xFF) == KSC_ALT_ENTER)))
                break;
            /* ...AND EVERY OTHER KEY GOES TO THE MACHINE, through the same
             * a2_key the event path uses: the II+ byte map, the Ctrl folds,
             * the two arrows and the two reset chords, one map and not two.
             * It draws nothing - it puts a byte in the keyboard latch and
             * sets the reset latch the loop below spends - which is what
             * makes it legal in here at all. */
            a2_key((int)(k & 0xFFu), (int)((k >> 8) & 0xFFu), a2_fsx_win);
        }
        /* AND THE RESET LATCH IS SPENT HERE, WHERE THE WAKE WOULD SPEND IT.
         * a2_key answers Ctrl+F2 and Ctrl+F3 by setting a2_reset_req, and the
         * ONLY other place that is read is os88_onwake - which is an EVENT,
         * and no event is dispatched in a bracket (rule 3). So for a wave the
         * two reset chords were inert for the whole colour session and then
         * fired the instant the user left, resetting a machine they thought
         * they had reset a minute ago. Nothing about a2_reset_service is
         * illegal in here: it stops the note, empties the paste queue, fills
         * the RAM claim, re-takes the write window, re-spells the menu rows
         * and marks lines - not one drawing slot, and the frame below then
         * draws the reset machine in the mode the user is looking at. */
        if (a2_reset_req)
            a2_reset_service();
        /* THE FLASH PHASE IS POLLED HERE, because W_ONTIMER is an EVENT and
         * no event is dispatched inside a bracket (rule 3). It is the same
         * a2_flash_step the wake uses on a kernel with no timer slot, it
         * draws nothing, and without it a flashing cursor would simply stop
         * for the length of the session. */
        a2_flash_step(os88_ticks());
        if (a2_state == A2_ST_RUN && !a2_pause)
            a2_slice();
        a2_spk_service();
        /* --- AND THE FRAME IS GATED, WHICH IS os88_onwake's OWN GATE ONE
         * PATH ALONG. The windowed flush reads ONE byte (a2_wrote) and then
         * asks whether anything at all is owed before it composes; this loop
         * called a2_fsx_frame unconditionally, so os88_fsx_wait's 18.2 ticks
         * a second each paid the whole skeleton - a2_dirty_scan's 24 row
         * probes plus, before the hoist above, a fifteen-to-twenty-three call
         * prologue per row - to discover that the 6502 had written nothing.
         * Counted out of build/apple2.raw.asm at the 20-25 us a call the
         * a2_dirty_scan header is calibrated on, that was 17-21 ms a tick in
         * text and 23-29 in hi-res: a third of a 4.77 MHz 8088, eighteen
         * times a second, in the one mode whose frame is the most expensive
         * this port has.
         *
         * THE GATE IS EXACT rather than a heuristic. a2_wrote() is the one
         * term the C side cannot see - it is a byte in the emulated
         * machine's own memory, set by the write path - and EVERY other
         * producer already sets a2_dirty_any, because they all go through
         * a2_line_dirty or a2_line_force: a2_flash_force, a2_dirty_all,
         * a2_dirty_split and a2_reset_service included. `!a2_fsx_ok` is the
         * first frame of a session, which is owed all 192 lines whatever the
         * marks say. a2_fsx_frame clears a2_dirty_any itself, exactly as
         * a2_flush does.
         *
         * An idle colour session is now one byte read and fsx_wait's hlt.
         *
         * ...AND IT IS PACED BY TIER TOO, WHICH IS a2_flush's SECOND TERM ONE
         * PATH ALONG (section 7.8, and section 13.4 for the arithmetic). The
         * windowed flush is at most once a host tick and, on the CPU_8086
         * tier, at most once every SECOND tick, because a 496.8 ms repaint
         * cannot keep up with a 55 ms tick and a second pass inside one
         * machine-visible change is the whole cost of the first for pixels
         * that were already right. The foreign frame is MORE expensive, not
         * less - 140.4 ms for one character row against the windowed row's
         * 26.4, a 5.3x - and it shipped with no such term at all: on an 8088
         * a one-row frame is 2.5 ticks during which no slice runs, fsx_wait
         * then returns at once, and the loop spends the session at one
         * A2_SLICE_MIN slice per ~200 ms instead of one per 55. So the tier
         * term is FOUR ticks here where the window takes two - the ratio the
         * row costs - and what it buys is slices, at the same pixels: the
         * dirty set is exact and accumulates across the skipped ticks, so a
         * frame that is deferred draws the same picture later rather than a
         * different one. What it costs is latency, which is the trade
         * section 7.8 already took for the window.
         *
         * `!a2_fsx_ok` is outside the pacing on both arms, for the reason it
         * is outside the gate above: the first frame of a session is owed all
         * 192 lines and is what stamps a2_fsx_tick in the first place. */
        if (a2_wrote())
            a2_dirty_any = 1;
        if ((a2_dirty_any || !a2_fsx_ok)
            && (!a2_fsx_ok
                || (unsigned)(os88_ticks() - a2_fsx_tick)
                       >= (a2_tier_slow ? 4u : 1u)))
            a2_fsx_frame();
        os88_fsx_wait(OS88_FSXW_TICK);
    }
    a2_fsx_up = 0;
}

/* a2_fsx_enter - Machine > Color NTSC.
 *
 * IT IS RESIDENT, like Machine > Toggle Fullscreen and for a sharper version
 * of the same reason: the claim below is 53KB and a heap claim can COMPACT
 * the arena, so taking it from inside ovl_a2_cmd would be moving the module
 * whose code is executing. The command works on a disk with no APPLE2.OVL,
 * which is the same property File > Quit and Toggle Fullscreen have.
 *
 * THE CLAIM IS TAKEN AT THE LATCH AND A REFUSAL IS LEGAL HERE (section 13.2),
 * which is the whole reason the shadow is a claim and not bss: the FLUSH may
 * never refuse, and this may. */
static void a2_fsx_enter(void *win)
{
    if (!a2_fsx_avail(win)) {
        /* The row is greyed on such a machine, so the kernel does not
         * dispatch it - but a2_menu_state runs on picks, not on displays, and
         * a window can move between them on a two-display desktop. One test,
         * so the greying and the refusal cannot disagree. */
        a2_say("No colour on this screen.");
        a2_st_dirty = 1;
        return;
    }
    a2_fsx_sh = os88_mem_claim(A2_FSX_KB);
    if (a2_fsx_sh == 0) {
        a2_say("No memory for colour.");
        a2_st_dirty = 1;
        return;
    }
    a2_fsx_init();                          /* MII's artifact rule, flattened.
                                             * HERE and not in os88_main: it
                                             * is the foreign mode's alone, so
                                             * a machine that never enters
                                             * colour never spends it */
    /* The shadow is a fresh claim and its contents are not promised, so the
     * frame is declared unknown rather than assumed blank - a2_fsx_main
     * clears a2_fsx_ok and the first pass writes all 192 lines whatever the
     * claim happened to hold. */
    a2_fsx_win = win;
    if (os88_fsx_run(a2_fsx_main, win, 0) < 0) {
        a2_say("The screen refused it.");
        a2_st_dirty = 1;
    } else if (a2_tier_slow && !a2_fsx_told) {
        /* THE PRICE OF COLOUR ON THE CPU_8086 TIER, SAID ONCE (section 13.1).
         * a2_fsx_avail asks os88_fsx_caps about the DISPLAY and nothing about
         * the CPU, so an 8088 with a VGA enters FSXM_VGA13 and the row stays
         * LIVE there - a stated decision, priced rather than greyed, because
         * colour is a cost a user asks for by picking a menu row where the
         * flash phase is one a timer spends for them. What is not left to
         * inference is the number.
         *
         * AND THE NUMBER IS THE RECURRING ONE, NOT THE ENTRY ONE. Section
         * 7.9.4 measures three costs on this tier - 3,365.3 ms for a whole
         * first frame, 140.4 ms for one character row, and 1,747.2 ms for a
         * SCROLL. The first is paid once and is already behind the reader by
         * the time this row exists; the last is what the next session in
         * colour will actually feel, and is therefore the one that can change
         * what the reader does next. A fact that cannot inform the decision
         * it is about is furniture.
         *
         * ...AND IT NAMES THE OPERATION, NOT AN EVENT THAT SOMETIMES CAUSES
         * IT, WHICH IS A REVIEW CORRECTION. The first draft read `Colour:
         * 1.7 s per RETURN.` and that is true only of a full 24-row TEXT
         * page. `GR` and `HGR` set the Apple's text window to rows 20-23
         * (section 7.4, and WELCOME.BAS selects exactly that), so a RETURN in
         * MIXED scrolls FOUR rows - 32 scan lines, ~304 ms by section 7.9.4's
         * own 7.34 + 2.15 per line - and a RETURN that is not on the bottom
         * line at all is the 140.4 ms of one row. 1,747.2 ms is a SCROLL of
         * the whole page, so that is the word: `a scroll` is true in every
         * mode where `per RETURN` was 5.7x high in the two modes half of
         * this port's own demo lives in.
         *
         * IT IS SAID ON THE WAY OUT AND NOT ON THE WAY IN, and that is the
         * status row's own arithmetic rather than a preference: a2_say stamps
         * a2_msg_until five seconds ahead, the bracket owns the whole screen
         * for as long as the reader stays in colour, and there is no status
         * row under it to read - so a message raised before os88_fsx_run has
         * expired unread by the time one is on the glass again. This is the
         * first tick at which the row exists, and the reader has just spent
         * the 3.4 seconds of the first frame the row no longer names.
         *
         * ONCE PER RUN, on `Square-wave tones only.`'s shape (section 8): a
         * fact is news the first time and furniture the second. */
        a2_fsx_told = 1;
        a2_say("Colour: 1.7 s a scroll.");
    }
    os88_mem_free(a2_fsx_sh);
    a2_fsx_sh = 0;
    a2_fsx_up = 0;
    /* THE DESKTOP IS BACK, AND THE REPAINT IT OWES HAS ALREADY HAPPENED: the
     * kernel repaints every window before fsx_run returns (SPEC.md 53.6 step
     * 4) and the shadow was invalidated on the way IN, so that one paint is
     * the whole cost of the session. Nothing is invalidated here - doing it
     * again is the double repaint section 13.3 prices at a second ~633 ms.
     *
     * WHAT IS STILL OWED IS THE WAKE. The bracket dispatched no events, so
     * the chain that keeps the 6502 running has to be re-posted by hand - one
     * API call, no pixels. A window that was COVERED at step 4 got no paint,
     * a2_sh_ok is still 0 from the entry, and the first W_PAINT after it
     * uncovers does the repaint then. */
    a2_kick = 1;
    os88_wm_wake(win);
}
