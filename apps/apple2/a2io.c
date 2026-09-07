/* ============================================================================
 * os8088 - apps/apple2/a2io.c        the soft switches, both directions
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 5). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * **EVERY READ IN $C000-$C0FF IS SIDE-EFFECTING**, AND THAT IS THIS FILE'S
 * FIRST FACT (APPLE2-SPEC section 5.3)
 * ----------------------------------------------------------------------------
 * $C010, $C030, $C050-$C057, $C061-$C063, $C064-$C067 and $C070 are all
 * READ-ACTIVE on an Apple II. A C side that treats a read as pure gives a
 * machine that boots to `]` and then NEVER CHANGES VIDEO MODE - which is the
 * shape that boots and is wrong, and is why `make a2cputest` row 8 exists and
 * is not optional.
 *
 * ----------------------------------------------------------------------------
 * WHAT IS HERE IN WAVE 1
 * ----------------------------------------------------------------------------
 * The VIDEO STATE and nothing else: the four II+ display switches as flags,
 * the mode dispatch's view of them, and the page they select. The switches
 * are what the flush reads, so they exist from the wave that builds the
 * flush; the CALL-OUTS that let the emulated machine touch them arrive in
 * wave 2 with the core that would do the touching, and the keyboard latch,
 * the speaker toggle and the paddles arrive with theirs.
 *
 * THE II+ SUBSET, AND THE FENCE (section 5.1). $C050/$C051 TEXT,
 * $C052/$C053 MIXED, $C054/$C055 PAGE2, $C056/$C057 HIRES - and the //e
 * switches $C000/$C001 80STORE, $C00C/$C00D 80COL, $C00E/$C00F ALTCHARSET and
 * $C05E/$C05F DHIRES ARE NOT ON THIS MACHINE and are not greyed either
 * (section 10.4): greying a //e card on an Apple II+ hands the reader a //e
 * checklist. AppleWin's `IS_APPLE2` gates are the checklist of exactly where
 * that fence is; apple2emu registers the //e status switches over the whole
 * of $C010-$C01F unconditionally and is WRONG for this machine.
 *
 * EVERY VIDEO SWITCH IS GUARDED BY VALUE (section 5.4). A write that sets a
 * flag to the value it already held marks nothing. The C64 measured the
 * alternative at 25 forced full-width blits, ~234 ms, for setting both the
 * source-changed and glass-unknown flags on a register write that changed
 * nothing.
 * ==========================================================================*/

/* --- the II+ display switches, as AppleWin's Video.h:52-71 names them ----- */
static int a2_v_text;                       /* $C050 off / $C051 on */
static int a2_v_mixed;                      /* $C052 / $C053 */
static int a2_v_page2;                      /* $C054 / $C055 */
static int a2_v_hires;                      /* $C056 / $C057 */

/* The renderer the switches select, which is what "a mode change that
 * actually changes the renderer" is asked about. */
#define A2_MODE_TEXT  0
#define A2_MODE_LORES 1
#define A2_MODE_HIRES 2

static int a2_mode_of(void)
{
    if (a2_v_text)
        return A2_MODE_TEXT;
    return a2_v_hires ? A2_MODE_HIRES : A2_MODE_LORES;
}

/* a2_mode_page - the base address of the page the display is reading NOW.
 *
 * Text and lo-res share $0400/$0800; hi-res is $2000/$4000 and is wave 3's,
 * which is why this answers the text page for every wave-1 mode. */
static int a2_mode_page(void)
{
    return a2_v_page2 ? A2_TXT2 : A2_TXT1;
}

/* a2_io_init - what a II+ powers up in: TEXT on, everything else off.
 *
 * AppleWin's VideoResetState clears the flag word and then sets VF_TEXT; the
 * four flags here are that word unpacked, because a bit field is refused by
 * this C (SPEC.md 73) and four ints are what the composer reads anyway. */
static void a2_io_init(void)
{
    a2_v_text = 1;
    a2_v_mixed = 0;
    a2_v_page2 = 0;
    a2_v_hires = 0;
}

/* a2_video_set - one display switch, GUARDED BY VALUE (section 5.4).
 *
 * `which` is 0 TEXT, 1 MIXED, 2 PAGE2, 3 HIRES; `on` is the value the switch
 * pair just selected. Answers 1 if anything moved.
 *
 * WAVE 2 CALLS THIS from the $C050-$C057 read and write paths, both of which
 * are side-effecting. It is here in wave 1 because the guard, the page change
 * and the whole-frame dirty are decisions about the FLUSH, and the flush is
 * this wave's subject.
 */
static int a2_video_set(int which, int on)
{
    int old, page0, mode0;

    page0 = a2_mode_page();
    mode0 = a2_mode_of();
    on = on ? 1 : 0;
    switch (which) {
    case 0: old = a2_v_text;  a2_v_text = on;  break;
    case 1: old = a2_v_mixed; a2_v_mixed = on; break;
    case 2: old = a2_v_page2; a2_v_page2 = on; break;
    default: old = a2_v_hires; a2_v_hires = on; break;
    }
    if (old == on)
        return 0;                           /* THE GUARD: nothing moved, so
                                             * nothing is marked */
    if (a2_mode_page() != page0)
        a2_watch_page();                    /* the write window follows the
                                             * page it is taken over */
    if (a2_mode_of() != mode0 || a2_mode_page() != page0 || which == 1)
        a2_dirty_all();                     /* a change that actually changes
                                             * the renderer, the page or the
                                             * MIXED split marks the whole
                                             * frame; one that does not marks
                                             * nothing */
    return 1;
}
