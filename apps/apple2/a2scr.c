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
#endif


/* THE FRAME SHADOW: 40 x 192 = 7,680 bytes, exactly the pixels last blitted.
 * It is bss and not a claim because THE FLUSH CANNOT REFUSE. */
static unsigned char a2_sh[A2_SHBYTES];

/* One composed band: eight pixel rows of one character row. Its letterbox
 * bytes (0-1 and 37-39) are never written by the composer and the loader
 * zeroed them, so the letterbox is dark, is part of every blit and every
 * compare, and costs no second code path (a2band.inc's header). */
static unsigned char a2_bnd[A2_BSTRIDE * 8];

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

/* The row signatures for the k-row scroll test: this frame's, and the
 * shadow's. */
static unsigned a2_sig[A2_ROWS];
static unsigned a2_shsig[A2_ROWS];
static int a2_sig_ok;                       /* a2_sig[] was filled THIS flush */

/* a2_dirty_take's target: the 32-byte page bitmap then the window's two
 * words, in one call. */
static unsigned char a2_dpg[36];
static unsigned a2_wlo, a2_whi;
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
static int a2_gsty;                         /* the status row's y */
static int a2_gl0;                          /* the FIRST Apple scan line that
                                             * is on the glass - THE BOTTOM
                                             * ANCHOR (section 7.1) */
static int a2_gnl;                          /* ...and how many of them fit */

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
static int  a2_st_dirty;                    /* ...and it wants redrawing */
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
    for (i = 0; i < A2_ROWS; i++) {
        a2_shsig[i] = 0;
        a2_rowwide[i] = 1;
    }
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

    a2_watch_set(base, base + A2_PGLEN - 1);
}

/* a2_dirty_scan - the page bitmap and the write window in ONE call, mapped
 * ROW-WARD onto scan lines (this file's header says why). */
static void a2_dirty_scan(void)
{
    unsigned base, last;
    int r, p0, p1;

    a2_dirty_take(a2_dpg);
    a2_wlo = (unsigned)a2_dpg[32] | ((unsigned)a2_dpg[33] << 8);
    a2_whi = (unsigned)a2_dpg[34] | ((unsigned)a2_dpg[35] << 8);
    /* AN EMPTY WINDOW - WLO above WHI - means nothing was written since the
     * last flush. A page bit can still be set: a2_dirty() marks a page with
     * no write at all, which is what a PAGE2 switch needs. So an empty window
     * cannot narrow anything and the rows it marks are marked WHOLE. */
    for (r = 0; r < A2_ROWS; r++) {
        base = a2_tbase[r] + (unsigned)(a2_mode_page() - A2_TXT1);
        last = base + A2_COLS - 1;
        p0 = (int)(base >> 8);
        p1 = (int)(last >> 8);
        if (!(a2_dpg[p0 >> 3] & (0x80 >> (p0 & 7)))
            && !(a2_dpg[p1 >> 3] & (0x80 >> (p1 & 7))))
            continue;                       /* neither of the row's pages was
                                             * written */
        /* ...and the window has to REACH the row, or the page bit was set by
         * a write somewhere else in the same 256 bytes - the stack, zero
         * page, a variable. That is the whole point of the window. */
        if (a2_wlo <= a2_whi && (a2_whi < base || a2_wlo > last))
            continue;
        a2_row_dirty(r);
        if (a2_wlo > a2_whi)
            a2_rowwide[r] = 1;              /* a page was marked with no window
                                             * to narrow it by - and it is
                                             * THIS row's flag, so the rows a
                                             * real window did narrow keep
                                             * their span */
    }
}

/* ==========================================================================
 * THE TIER (section 7.8)
 * ========================================================================*/
static int a2_tier_slow;                    /* the CPU_8086 tier */

static void a2_tier_init(void)
{
    a2_tier_slow = (os88_cpu() == OS88_CPU_8086) ? 1 : 0;
    /* AND NOTHING IS DECIDED FROM IT IN WAVE 1. The tier table - the flush
     * rate, the flash phase's refusal and the fullscreen magnification - is
     * written in WAVE 3 from `make a2bandbench`'s measured microseconds, and
     * PERFORMANCE.md rule 4 is explicit that a constant sized while looking
     * at an emulator encodes the wrong range. The flag is read here so that
     * the wave which measures has one place to write. */
}

/* ==========================================================================
 * THE GEOMETRY - AND THE BOTTOM ANCHOR IS A CORRECTNESS REQUIREMENT
 * ========================================================================*/
/* On a 640x200 desktop os88_video().dock_top is 176, so the window is clamped
 * to about 156 rows of content and only ~114 of the Apple's 192 scan lines
 * fit. The visible band therefore anchors to the BOTTOM of the Apple frame,
 * because the `]` cursor line and MIXED's four text rows are both there
 * (APPLE2-SPEC section 7.1). Anchoring to the top would show the Apple's
 * empty upper screen and hide the prompt, which is the one thing a reader
 * needs to see.
 *
 * `& ~7` on the horizontal offset is not cosmetic: os88_wm_snap put the
 * content origin on a cell boundary and OSAPI_GFX_SCROLL refuses a rect whose
 * x1 or x2+1 is not a multiple of 8, so the band's left edge has to stay on
 * one. Vertically nothing needs it - blit1 and scroll take any y. */
static int a2_geom(void *win)
{
    static struct os88_pt org;
    static struct os88_size sz;
    int d, avail;

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

    d = (sz.w - A2_BANDW) / 2;
    if (d < A2_BORDER)
        d = A2_BORDER;
    a2_gsx = org.x + (d & ~7);

    avail = a2_gsty - org.y - A2_BORDER * 2;
    if (avail < 0)
        avail = 0;
    a2_gnl = (avail > A2_SCRH) ? A2_SCRH : avail;
    a2_gl0 = A2_SCRH - a2_gnl;              /* THE BOTTOM ANCHOR */
    a2_gsy = org.y + A2_BORDER + (avail - a2_gnl) / 2;
    return 0;
}

/* a2_border - the strips of the content box the band does not cover.
 *
 * It counts the fills it ISSUES and not four every time: over-reporting the
 * border puts phantom milliseconds in every cost row the harness prints. */
static void a2_border_fill(void)
{
    int sbot = a2_gsy + a2_gnl;

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
        if (a2_gsx + A2_BANDW < a2_gox + a2_gw) {
            os88_gfx_fill(a2_gsx + A2_BANDW, a2_gsy,
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
 * by the shadow. Invalidating the whole shadow instead is the 505.4 ms full
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

    sbot = a2_gsy + a2_gnl;                 /* one past the band's last line */

    /* the border strips, and only when the rect actually reaches one */
    if (y1 < a2_gsy || y2 >= sbot
        || x1 < a2_gsx || x2 >= a2_gsx + A2_BANDW)
        a2_border_dirty = 1;
    if (y2 >= a2_gsty)
        a2_st_ok = 0;                       /* the status row's pixels are no
                                             * longer ours */

    if (y2 < a2_gsy || y1 >= sbot
        || x2 < a2_gsx || x1 >= a2_gsx + A2_BANDW)
        return;                             /* the rect misses the band */

    /* THE COLUMNS, IN BAND BYTES, UNIONED WITH ANY EARLIER RECT THIS FLUSH.
     * The letterbox is redrawn wherever the range reaches it, because the
     * range is in BAND bytes and byte 0 is the letterbox's first - the
     * widening in the flush's `rowf` arm is what keeps a group's own bytes
     * whole around it. `d` is clamped before the shift: a rect that starts
     * left of the band gives a negative offset. */
    d = x1 - a2_gsx;
    if (d < 0)
        d = 0;
    c0 = d >> 3;
    d = x2 - a2_gsx;
    if (d < 0)
        d = 0;
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
    l0 = y1 - a2_gsy + a2_gl0;
    l1 = y2 - a2_gsy + a2_gl0;
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

    base = a2_tbase[r] + (unsigned)(a2_mode_page() - A2_TXT1);
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
    } else if (a2_state != A2_ST_HALT) {
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
 * happened - a keystroke dirties one row and a 24-row signature pass would
 * cost more than the draw it saves - and only in the mode whose signature
 * means anything.
 *
 * FOUR FIFTHS OF THE ROWS, and it is the C64's measured constant rather than
 * a taste: a scroll dirties the whole page by definition, while a one-row
 * change dirties the rows of the two pages it touches, and at a low threshold
 * the test runs, finds a spurious match on a screen with several blank rows,
 * and emits a scroll the span compare then has to undo. Correctness survives
 * that - the signature is a HINT - and the cost does not. */
#define A2_SHIFT_NUM 4                      /* ...of A2_SHIFT_DEN, and the
                                             * denominator is spelled as a
                                             * shift sum at the one place it
                                             * is used */
#define A2_SHIFT_DEN 5

/* a2_shift_test - `k` character rows up, or 0.
 *
 * `r0` IS THE FIRST ROW THE GLASS HAS EVER SHOWN, and it is a parameter
 * rather than 0 because a2_shsig[] only exists for rows the flush has
 * COMPOSED. On a 640x200 desktop a2_gl0 is 81, so the bottom anchor puts the
 * first TEN character rows entirely off the top of the content box (row 10 is
 * the partial one and is composed); their signatures are permanently 0, and
 * comparing them against live memory made the test answer "no shift" on every
 * screen - which is the CGA arm of the very case this routine exists for. */
static int a2_shift_test(int r0)
{
    unsigned base;
    int i, k, ok;

    /* THE ROWS ABOVE r0 ARE NOT SIGNED, THEY ARE ZEROED. They have no visible
     * scan line, so the flush never composes one and a2_shsig[] for them is
     * permanently 0 - signing them is 0.673 ms a row spent comparing a live
     * value against a sentinel it can never equal. On a 640x200 desktop that
     * is TEN of the twenty-four, 6.7 ms off every shift test. 0 is written
     * rather than left stale because the scroll's own copy below reads
     * a2_sig[] from 0, and a stale value there is a signature that lies. */
    for (i = 0; i < r0; i++)
        a2_sig[i] = 0;
    for (i = r0; i < A2_ROWS; i++) {
        base = a2_tbase[i] + (unsigned)(a2_mode_page() - A2_TXT1);
        a2_sig[i] = a2_rowsig(a2_m.ramseg, base, A2_COLS);
#ifdef A2_HOST
        a2_n_sig++;
#endif
    }
    a2_sig_ok = 1;
    for (k = 1; k + r0 < A2_ROWS; k++) {
        ok = 1;
        for (i = r0; i + k < A2_ROWS; i++)
            if (a2_sig[i] != a2_shsig[i + k]) {
                ok = 0;
                break;
            }
        if (ok)
            return k;                       /* what was at row i+k is now at
                                             * row i: the content moved UP */
    }
    return 0;
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
    int i, line, s, w, rc, fy;

    if (a2_run_n <= 0)
        return;
    w = a2_run_x1 - a2_run_x0 + 1;
    s = a2_run_l0 - (int)A2_X8(a2_run_row);
    rc = os88_gfx_blit1(a2_bnd + A2_X40(s) + a2_run_x0, A2_BSTRIDE,
                        a2_gsx + a2_run_x0 * 8,
                        a2_gsy + (a2_run_l0 - a2_gl0),
                        w * 8, a2_run_n);
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
        if (a2_font_row != a2_run_row) {
            a2_font_row = a2_run_row;
            fy = (int)A2_X8(a2_run_row);
            if (fy < a2_gl0)
                fy = a2_run_l0;             /* the bottom anchor cut this
                                             * row's top off the glass */
            a2_row_font(a2_run_row, a2_gsy + (fy - a2_gl0));
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
    int i, k, nd, trust, drew, rowf, ux0, ux1, r0, nvis;

    if (a2_geom(win) < 0)
        return;
    a2_sig_ok = 0;
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
     * 8088 against ~26 for the scroll. A twenty-line LIST was ~4 s instead of
     * ~0.5 s.
     *
     * Nothing tied it to full visibility but the shadow memcpy below, which
     * shifted all 192 lines: the gfx_scroll rect was ALREADY the visible band
     * and lines under a2_gl0 are never compared or drawn. So the shadow is
     * shifted from a2_gl0 down, and both the row scan and the signature
     * compare start at the first row the glass has ever shown - a row that
     * was never composed has no signature to compare (a2_shift_test's own
     * header). */
    k = 0;
    r0 = a2_gl0 >> 3;                       /* the first row with any visible
                                             * scan line: 8r+7 >= a2_gl0 */
    nvis = A2_ROWS - r0;
    if (a2_sh_ok && !a2_v_hires && !a2_abt_up && nvis > 0) {
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
        if (nd + (nd << 2) >= (nvis << 2))
            k = a2_shift_test(r0);
    }
    if (k) {
        if (os88_gfx_scroll(a2_gsx, a2_gsy,
                            a2_gsx + A2_BANDW - 1, a2_gsy + a2_gnl - 1,
                            (int)A2_X8(k)) == 0) {
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
                a2_shsig[r] = 0;
                a2_rowwide[r] = 1;
            }
            /* ...AND THE FORCED BAND-BYTE RANGE GOES BACK TO THE WHOLE BAND.
             * gfx_scroll left garbage across the vacated rows' FULL WIDTH, so
             * a range a damage rect had narrowed earlier in the same flush
             * would leave the letterbox and the far cells of those rows
             * holding it. Nothing else in the flush forces a line. */
            a2_force_wide();
            /* the shifted rows' signatures moved with them, and a2_sig[]
             * already holds exactly those values. THE ROWS ARE STILL
             * COMPOSED AND COMPARED: the signature is a hint and nothing
             * rests on it, so a collision costs a redraw and never a wrong
             * screen. */
            for (i = 0; i + k < A2_ROWS; i++)
                a2_shsig[i] = a2_sig[i];
        } else {
            k = 0;                          /* refused: spans, and the shadow
                                             * stays true - nothing moved */
        }
    }

    /* --- compose, compare, draw ------------------------------------------- */
    a2_run_n = 0;
    for (r = 0; r < A2_ROWS; r++) {
        drew = 0;
        rowf = 0;
        for (s = 0; s < 8; s++) {
            line = (int)A2_X8(r) + s;
            if (line < a2_gl0)
                continue;                   /* the bottom anchor: this line is
                                             * not on the glass */
            if (a2_line_is(a2_lnf, line))
                rowf = 1;
            if (a2_line_is(a2_lnd, line) || a2_line_is(a2_lnf, line)
                || !a2_sh_ok)
                drew = 1;
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

        base = a2_tbase[r] + (unsigned)(a2_mode_page() - A2_TXT1);

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
            && a2_whi >= base && a2_wlo <= base + A2_COLS - 1) {
            g0 = (a2_wlo > base) ? (int)((a2_wlo - base) >> 3) : 0;
            g1 = (a2_whi < base + A2_COLS - 1)
               ? (int)((a2_whi - base) >> 3) : A2_GROUPS - 1;
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

        a2_band_text(a2_bnd, g0, g1, a2_m.ramseg, base,
                     a2_fl_phase ? 0x7F : 0x00);
        a2_flrow[r] = (unsigned char)a2_rowflash(a2_m.ramseg, base, A2_COLS);
#ifdef A2_HOST
        a2_n_band++;
        a2_n_group += (unsigned)(g1 - g0 + 1);
        a2_n_cell += (unsigned)((g1 - g0 + 1) * 8);
#endif
        /* the shadow's SIGNATURE is updated HERE, with the row that was just
         * recomposed, and nowhere else: a row this flush did not recompose
         * did not change its sources either, so its old signature is still
         * true. And when the shift test has already run this flush it
         * computed exactly this value from exactly these bytes - the lock is
         * held throughout - so it is read rather than taken again. */
        a2_shsig[r] = a2_sig_ok ? a2_sig[r]
                                : a2_rowsig(a2_m.ramseg, base, A2_COLS);
#ifdef A2_HOST
        if (!a2_sig_ok)
            a2_n_sig++;
#endif

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
