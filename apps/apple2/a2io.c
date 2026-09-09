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
 * WHAT IS HERE
 * ----------------------------------------------------------------------------
 * The whole of $C000-$C0FF in BOTH DIRECTIONS - a2_io_rd and a2_io_wr, which
 * the core's read and write ladders call out to (a2cpu.inc) - plus the reset
 * line, both kinds (section 4.5), which is here for the reason c64io.c's
 * c64_reset_cpu is: a reset is what the register file and the switch state
 * are FOR.
 *
 * **MOST OF THESE ADDRESSES ARE RANGES AND NOT SINGLE BYTES** (section 5.1).
 * A II+ decodes only the low four bits of the page, so $C000-$C00F ALL read
 * the keyboard, $C010-$C01F ALL clear the strobe and $C030-$C03F ALL toggle
 * the speaker (AppleWin Memory.cpp:564-566, :579-584, :636-650). Code in the
 * wild uses the aliases - `LDA $C030` and `LDA $C03F` are the same click - so
 * the dispatch is on the address's low NIBBLE within each block and never on
 * an equality. $C060-$C06F is the one block that decodes three bits rather
 * than four: `addr & 0x7`, "address bit 4 is ignored (UTAIIe:7-5)"
 * (Memory.cpp:726-741), so $C068-$C06F mirror $C060-$C067.
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

/* THE FLOATING BUS IS REFUSED, WITH THE ARITHMETIC (section 5.2). An unmapped
 * read answers $FF, which is apple2emu's own posture. AppleWin's true floating
 * bus needs a cycle-exact video scanner - 262 x 65 = 17,030 cycles a frame and
 * eighteen bit extractions on every unmapped read - to compute a byte almost
 * nothing reads, and it is the one place a bug is invisible. Every "answer the
 * floating bus" below is this constant, and the reads that carry a bit 7 of
 * their own (the buttons, the paddle timers) mask it off that. */
#define A2_FLOAT 0xFF

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

/* a2_mode_page - the base address of the page the display is reading NOW, and
 * a2_page_len - how long it is. Text and lo-res share $0400/$0800; hi-res is
 * $2000/$4000 and is eight times the size (APPLE2-SPEC section 7.2).
 *
 * THIS IS THE PAGE THE WRITE WINDOW IS TAKEN OVER (section 7.5) and it is the
 * GRAPHICS page in a graphics mode, MIXED or not: the four text rows of a
 * MIXED screen are outside it, and a2_dirty_scan marks them from the page
 * bitmap alone with the row widened, because a window taken somewhere else
 * says nothing about them. Widening the watch range to cover both pages
 * instead would span $0400-$3FFF - which is where an Applesoft program and
 * every one of its variables live - and the per-row intersection would then
 * answer "all forty cells" for every row of every flush, which is the whole
 * thing the window exists to prevent. */
static int a2_mode_page(void)
{
    if (!a2_v_text && a2_v_hires)
        return a2_v_page2 ? A2_HGR2 : A2_HGR1;
    return a2_v_page2 ? A2_TXT2 : A2_TXT1;
}

static int a2_page_len(void)
{
    return (!a2_v_text && a2_v_hires) ? A2_HGRLEN : A2_PGLEN;
}

/* a2_row_mode - WHICH RENDERER OWNS CHARACTER ROW r (section 7.4).
 *
 * MIXED is the top 160 scan lines in the graphics mode and the bottom 32 in
 * TEXT, and 160 is 20 character rows exactly - so the split is a row test and
 * never a scan-line one, and no row is ever half one renderer and half the
 * other. */
static int a2_row_mode(int r)
{
    if (a2_v_text)
        return A2_MODE_TEXT;
    if (a2_v_mixed && r >= A2_MIXROW)
        return A2_MODE_TEXT;                /* ...and it reads the TEXT page,
                                             * PAGE2 and all: a real II+
                                             * selects the second page for
                                             * both halves with one switch */
    return a2_v_hires ? A2_MODE_HIRES : A2_MODE_LORES;
}

/* a2_row_base - the row's forty SOURCE bytes.
 *
 * For text and lo-res that is the interleaved text-page row; for hi-res it is
 * SCAN LINE 0 of the row group, whose other seven lines a2_band_hires walks
 * at $400 apart. Both maps are built in os88_main and page 2 is +$400 on one
 * and +$2000 on the other, which is why this is one function and not an
 * offset the callers add. */
static unsigned a2_row_base(int r)
{
    if (a2_row_mode(r) == A2_MODE_HIRES)
        return a2_hbase[r] + (unsigned)(a2_v_page2 ? A2_HGR2 - A2_HGR1 : 0);
    return a2_tbase[r] + (unsigned)(a2_v_page2 ? A2_TXT2 - A2_TXT1 : 0);
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
    if (a2_mode_of() != mode0 || a2_mode_page() != page0) {
        a2_dirty_all();                     /* a change that actually changes
                                             * the RENDERER or the PAGE really
                                             * does change every row, so the
                                             * whole frame is marked */
    } else if (which == 1 && !a2_v_text) {
        /* ...BUT THE MIXED SPLIT MOVES FOUR ROWS AND NOT TWENTY-FOUR, and
         * a2_dirty_all() here was the same waste the else-arm below diagnoses
         * one condition along - on the arm where the switch really does
         * something. a2_row_mode(r) for r < A2_MIXROW never reads a2_v_mixed,
         * a2_row_base does not move and neither does the page, so twenty of
         * the twenty-four rows were recomposed from identical sources by the
         * identical composer to produce identical pixels: ~417 ms of hi-res
         * compose plus 192 span compares to draw four rows that owed ~70
         * (section 7.9.2's per-group figures). a2_dirty_split marks exactly
         * the split's rows, WHOLE - a2scr.c owns why both marks are needed.
         *
         * THE STATUS ROW moves with them: a2_dirty_split sets a2_st_dirty for
         * the else-arm's reason, the MIXED field being on it. */
        a2_dirty_split();
    } else {
        /* ...AND MIXED IN TEXT MODE IS THE `changes nothing` CASE, NOT THE
         * `changes the split` ONE. The split only exists in a GRAPHICS mode:
         * while a2_v_text is 1 the whole screen is text whatever MIXED says,
         * so a2_dirty_all() here set a2_rowwide[] on all 24 rows and called
         * a2_force_wide, the flush composed 24 x 5 = 120 groups (~292 ms on
         * the target), and the span compare then drew ZERO pixels because the
         * glass was never unknown. It is reachable and ordinary: Applesoft's
         * GR sets MIXED and TEXT does not clear it, so the switch is left on
         * and any later POKE -16302,0 / -16301,0 pays it. This is the C64's
         * measured 25 forced blits / ~234 ms (section 7.7) one condition
         * along from where it was guarded.
         *
         * THE STATUS ROW STILL HAS TO MOVE, and it is marked here rather than
         * left to the running machine's next write: the MIXED field is on it,
         * and a program that flips the switch and then loops without touching
         * the display page would leave the row stale. */
        a2_st_dirty = 1;
    }
    return 1;
}

/* ==========================================================================
 * THE KEYBOARD LATCH AND THE STROBE (section 6.2)
 * ========================================================================*/
/* $C000-$C00F read the keycode with bit 7 set while a key is WAITING;
 * $C010-$C01F clear the waiting flag and answer the floating bus. Nothing
 * else - the //e's status reads at $C011-$C01F are not on this machine
 * (section 10.4), and apple2emu registers them over that whole range
 * unconditionally and is WRONG for a II+.
 *
 * THE READ AT $C000 REPORTS AND DOES NOT CONSUME (section 5.3). This document
 * used to say every read in the page was side-effecting, which is false and
 * is the sharper half of the rule: what is true is that a READ MAY BE A
 * WRITE, at $C010-$C01F, $C030-$C03F, $C050-$C05F and $C070-$C07F. */
static int a2_kb_code;                      /* the Apple byte, bit 7 clear */
static int a2_kb_ready;                     /* ...and the strobe */

static void a2_kb_put(int b)
{
    a2_kb_code = b & 0x7F;
    a2_kb_ready = 1;
}

/* ==========================================================================
 * THE SPEAKER - COUNTED AND SILENT THIS WAVE (section 8)
 * ========================================================================*/
/* $C030-$C03F toggles the one-bit speaker on a read OR a write. The toggle
 * interval estimator that turns those toggles into a tone is WAVE 5's, and it
 * exists in no reference - all three synthesize - so it is this port's own
 * design and is labelled as such. What is here is the count, because the
 * toggle is a real side effect of a read and the ladder has to take it. */
static unsigned a2_spk_n;

/* ==========================================================================
 * THE GAME CONNECTOR (section 5.1)
 * ========================================================================*/
/* AN0-AN3, the four ANNUNCIATOR outputs at $C058-$C05F, even address off and
 * odd on. **They are a II+'s own** - only the DHIRES reading of $C05E/$C05F is
 * //e (AppleWin Memory.cpp:667-682, :876-886) - and they are STATE-ONLY here:
 * this machine has nothing on the game connector, so what the port owes is
 * that the four bits move and that a program writing them is not answered by
 * the unmapped $FF path. */
static int a2_an[4];

/* THREE digital inputs at $C061-$C063, not two (Memory.cpp:723-726 dispatches
 * all three to JoyReadButton). PB0 and PB1 are the two a game reads and F1
 * and F2 are on them (section 6.3). They REPORT a level and change nothing -
 * reading $C061 no more presses a button than reading $C000 consumes a key
 * (section 5.3) - and the level is refreshed ONCE A WAKE by a2_kbd_poll
 * rather than per emulated read, because a game reads $C061 in a loop and
 * each read would otherwise be a bridge crossing.
 *
 * **$C063 IS THE SHIFT-KEY MOD, AND IT IS ACTIVE WHEN SHIFT IS *UP*.** This
 * is the one of the three that is a fact about the MACHINE rather than a host
 * convenience, and the port answered it 0 - the inverse of the default state
 * - for a wave. AppleWin's JoyReadButton (Joystick.cpp:640-651) is the
 * authority and it is explicit: on a II/II+ with no joystick,
 * `pressed = !(GetKeyState(VK_SHIFT) < 0)`, cited in its own comment to
 * Sather, *Understanding The Apple II* p7-36. So a program probing the mod
 * on a stock II+ reads $80 with Shift up and $00 with it held, and that is
 * what this machine does.
 *
 * IT IS POLLED ONLY ONCE A PROGRAM HAS ASKED. Shift needs TWO os88_key_down
 * calls (left and right), 93 us a wake on the target, for a level almost
 * nothing reads - so a2_btn2_want latches on the first $C063 read and the
 * poll is free until then. a2_btn[2] starts at 1 because Shift up IS the
 * resting state, so even the very first read - the one before any poll has
 * seen the key - answers what the reference answers. */
static int a2_btn[3] = { 0, 0, 1 };
static int a2_btn2_want;

/* THE PADDLE ONE-SHOTS. A read or a write anywhere in $C070-$C07F arms all
 * four (Memory.cpp:753-756, :783-786) - and AppleWin leaves each timer that is
 * STILL RUNNING exactly as it is (Joystick.cpp:725-729), so a program that
 * strobes the trigger inside its own count loop, which is what every PDL()
 * read does, is not handed a timer that never expires. $C064-$C067 then answer
 * bit 7 set until the deadline passes, and READING ONE DOES NOT RESTART IT.
 *
 * THE SCALE IS 2816/255 = ~11.04 EMULATED CYCLES PER UNIT (Joystick.cpp:677,
 * :703-705), not exactly 11: MII's `value * 11` (mii_analog.c:69-77) is an
 * approximation and is named as one here rather than copied as a fact. This
 * port's paddles answer CENTRE (section 10.3), so the deadline is a constant:
 * 127 * 2816 / 255 = 1,402 cycles, computed once here rather than every arm.
 *
 * THE CLOCK IS a2_now(), WHICH IS EXACT INSIDE A RUN, and that is the whole
 * reason it exists: the arm at $C070 and the read at $C064 are both reached
 * from INSIDE a2_run, where the C's own per-slice counter has not moved yet.
 * a2cpu.inc computes it as A2_SCR_CLKB - A2_SCR_DEAD. */
#define A2_PDL_CENTRE  127
#define A2_PDL_CYCLES  1402                 /* 127 * 2816 / 255 */
static int a2_pdl_on[4];
static int a2_pdl_end[4];

static void a2_pdl_trigger(void)
{
    int i, now;

    now = a2_now();
    for (i = 0; i < 4; i++) {
        if (a2_pdl_on[i] && (int)(a2_pdl_end[i] - now) > 0)
            continue;                       /* still running: the strobe has
                                             * NO EFFECT (GH#985) */
        a2_pdl_on[i] = 1;
        a2_pdl_end[i] = now + A2_PDL_CYCLES;
    }
}

static int a2_pdl_rd(int i)
{
    int now;

    now = a2_now();
    if (a2_pdl_on[i]) {
        if ((int)(a2_pdl_end[i] - now) > 0)
            return A2_FLOAT;                /* bit 7 SET: still counting */
        a2_pdl_on[i] = 0;
    }
    return A2_FLOAT & 0x7F;
}

/* ==========================================================================
 * $C050-$C05F - the four video switches, and the four annunciators
 * ========================================================================*/
/* THE VIDEO SWITCHES ARE GUARDED BY VALUE (section 5.4) and a2_video_set above
 * is where that guard lives: a write that sets a flag to the value it already
 * held marks nothing. The C64 measured the alternative at 25 forced full-width
 * blits, ~234 ms, for a register write that changed nothing. */
static void a2_c05x(int lo)
{
    int n;

    n = lo & 0x0F;
    if (n < 8)
        a2_video_set(n >> 1, n & 1);        /* $C050..$C057, pair by pair */
    else
        a2_an[(n - 8) >> 1] = n & 1;        /* $C058..$C05F, AN0-AN3 */
}

/* $C060-$C06F, on `addr & 7` because bit 4 is ignored (UTAIIe:7-5) */
static int a2_c06x(int lo)
{
    int n;

    n = lo & 7;
    if (n == 0)
        return A2_FLOAT;                    /* $C060 TAPEIN - no cassette */
    if (n < 4) {
        if (n == 3)
            a2_btn2_want = 1;               /* the shift-key mod has a reader
                                             * now: a2_kbd_poll starts asking */
        return a2_btn[n - 1] ? A2_FLOAT : (A2_FLOAT & 0x7F);
    }
    return a2_pdl_rd(n - 4);
}

/* ==========================================================================
 * THE TWO CALL-OUTS (section 5.1) - the core's ladders reach these
 * ========================================================================*/
/* A C SIDE THAT TREATS A READ AS PURE GIVES A MACHINE THAT BOOTS TO `]` AND
 * THEN NEVER CHANGES VIDEO MODE. That is `make a2cputest` row 8 and it is not
 * optional: the ROM sets TEXT with `LDA $C051`, and a read that did not take
 * the switch would leave a booted machine that never leaves the mode it
 * started in. */
static int a2_io_rd(unsigned a)
{
    int lo;

    lo = (int)(a & 0x00FF);
    switch (lo >> 4) {
    case 0x0:                               /* $C000-$C00F, the latch */
        return a2_kb_code | (a2_kb_ready ? 0x80 : 0);
    case 0x1:                               /* $C010-$C01F, the strobe */
        a2_kb_ready = 0;
        return A2_FLOAT;
    case 0x3:                               /* $C030-$C03F, the speaker */
        a2_spk_n++;
        return A2_FLOAT;
    case 0x5:
        a2_c05x(lo);
        return A2_FLOAT;
    case 0x6:
        return a2_c06x(lo);
    case 0x7:                               /* $C070-$C07F, the trigger */
        a2_pdl_trigger();
        return A2_FLOAT;
    default:
        /* $C020 the cassette output, $C040 the utility strobe, $C080 the
         * Language Card and $C0E0 the Disk II controller (the follow-up PR,
         * section 14). None is on this machine, and an unmapped read answers
         * the floating bus. */
        return A2_FLOAT;
    }
}

static void a2_io_wr(unsigned a, int v)
{
    int lo;

    (void)v;                                /* NOTHING IN THIS PAGE TAKES A
                                             * VALUE on a II+: every switch is
                                             * addressed, not written */
    lo = (int)(a & 0x00FF);
    switch (lo >> 4) {
    case 0x1:
        a2_kb_ready = 0;
        break;
    case 0x3:
        a2_spk_n++;
        break;
    case 0x5:
        a2_c05x(lo);
        break;
    case 0x7:
        a2_pdl_trigger();
        break;
    default:
        /* $C000-$C00B ON WRITES are the //e's paging switches - 80STORE,
         * RAMRD, RAMWRT, ALTZP and the rest - and $C00C-$C00F 80COL and
         * ALTCHARSET. NONE OF THEM IS ON THIS MACHINE and none is greyed
         * either (section 10.4): greying a //e card on an Apple II+ hands the
         * reader a //e checklist. AppleWin's IS_APPLE2 gates are the
         * checklist of exactly where that fence is. */
        break;
    }
}

/* ==========================================================================
 * RESET, BOTH KINDS (section 4.5)
 * ========================================================================*/
/* THE RESET LINE IS THE NMOS 6502'S AND NOT A CONVENIENCE
 * (AppleWin source/CPU.cpp:798-812):
 *
 *   - **I is SET.** A reset that leaves interrupts enabled runs the ROM's
 *     initialisation with IRQ live.
 *   - **The JAM is cleared.** A jammed core that is never un-jammed makes
 *     Ctrl-Reset - the one recovery a user has - do nothing at all, which
 *     reads as the port having frozen.
 *   - **D is NOT cleared by an NMOS reset**, and it is tempting to clear it.
 *     AppleWin clears it only for a 65C02 (`if (GetMainCpu() == CPU_65C02)`),
 *     and this machine is a 6502.
 *   - **SP wraps WITHIN PAGE ONE.** `SP - 3` is a byte subtraction; the stack
 *     pointer is a page-one offset and cannot leave that page.
 */
static void a2_reset_cpu(void)
{
    a2_m.p = (a2_m.p | 0x24) & 0xFF;        /* I set; bit 5 always reads 1.
                                             * D IS NOT TOUCHED */
    a2_m.s = (a2_m.s - 3) & 0xFF;           /* ...within page one */
    a2_m.cnt = 0;
    a2_m.reason = 0;
    a2_m.pc = (unsigned)a2_bread(0xFFFC)
            | (((unsigned)a2_bread(0xFFFD)) << 8);
    a2_rebias();                            /* PC moved under the core's feet */
    if (a2_state != A2_ST_DEAD)
        a2_state = A2_ST_RUN;               /* THE JAM IS CLEARED */
}

/* a2_power_on - the cold machine (section 4.5).
 *
 * AppleWin initialises A = X = Y = $FF and SP = $01FF and THEN calls its
 * reset, so what the ROM actually starts on is **$01FC** (CPU.cpp:769-774).
 * That is the value reproduced here, and it is the power-on row taking the
 * Ctrl-Reset path rather than a second constant.
 *
 * THE `FF FF 00 00` FILL IS A CHOSEN DETERMINISTIC APPROXIMATION and is named
 * as one. AppleWin's corresponding pattern additionally RANDOMISES offsets
 * $28/$29/$68/$69 in every 512-byte block (Memory.cpp:2340-2358); the three
 * compatibility pokes below are exact at :2416-2436. This port takes the
 * repeating fill WITHOUT the randomisation on purpose - a deterministic
 * power-on is what makes a2cputest and the screendumps reproducible, and a
 * program that depends on uninitialised RAM is depending on a machine nobody
 * can reproduce either.
 *
 * `$03F2`/`$03F3` = `$55 $55` IS AN EMULATOR TRICK AND THE REQUIRED CONDITION
 * IS A MISMATCH. $03F2/$03F3 is SOFTEV, the Monitor's warm-start vector, and
 * **$03F4 is PWREDUP** (AppleWin bin/APPLE2E.SYM:65-66). The II+ ROM at
 * $FA85-$FA8E runs `LDA $03F3; EOR #$A5; CMP $03F4; BNE ...`, so the POWER-UP
 * path is taken when $03F4 != $03F3 XOR $A5. The $55 $55 poke works only
 * because a freshly filled page leaves $03F4 = $FF while $55 XOR $A5 is $F0 -
 * and it is that INEQUALITY the port has to guarantee, not the two $55s.
 * Writing the pattern and then ASSERTING the mismatch is one line and makes
 * the fill's choice irrelevant; assuming the $55s are the signature is a
 * machine that cold-boots or does not depending on what happened to be in one
 * byte. */
static void a2_power_on(void)
{
    a2_zpower(0, 0xC000);                   /* FF FF 00 00 over $0000-$BFFF */
    /* RNDL/RNDH forced NON-ZERO, because "Pooyan" reads them on a cold boot
     * (Memory.cpp:2416-2421). The two statements are laid out plainly rather
     * than beside a comment that opens on the first of them: c64.c's
     * c64_reset_service carries the scar of exactly that, where a `c64_paste_
     * req = 0;` sat INSIDE the block comment the line above it opened and
     * compiled. */
    a2_wr(0x004E, 0x20);
    a2_wr(0x004F, 0x20);
    a2_wr(0x620B, 0x00);                    /* ...:2426-2430 */
    a2_wr(0xBFFD, 0x00);                    /* ...:2434-2436 */
    a2_wr(0xBFFE, 0x00);
    a2_wr(0xBFFF, 0x00);
    if (a2_rd(0x03F4) == ((a2_rd(0x03F3) ^ 0xA5) & 0xFF))
        a2_wr(0x03F4, a2_rd(0x03F4) ^ 0xFF);        /* THE MISMATCH, asserted
                                                     * rather than assumed */
    a2_m.a = 0xFF;                          /* CpuInitialize, CPU.cpp:771 */
    a2_m.x = 0xFF;
    a2_m.y = 0xFF;
    a2_m.s = 0xFF;                          /* $01FF - and the reset below
                                             * pulls it to $01FC */
    a2_m.p = 0x20;
    a2_io_init();                           /* the switches back to TEXT */
    a2_kb_ready = 0;
    a2_spk_n = 0;
    a2_reset_cpu();
}
