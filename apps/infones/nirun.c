/* ============================================================================
 * os8088 - apps/infones/nirun.c      the frame loop, the interrupts, the pacer
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THIS FILE FOLLOWS: the frame loop's shape - run a line's worth of CPU
 * cycles, call the mapper's HSync hook, draw one line, and resolve sprite-0
 * hit by SPLITTING that line's budget at the sprite's X - is InfoNES's
 * InfoNES_Cycle and InfoNES_HSync (src/InfoNES.cpp:556-751) and
 * InfoNES_GetSprHitY (:1053-1101), read for behaviour and reimplemented. The
 * frame-skip gate - the CPU and the mappers run every frame, only the
 * composer and the present are skipped - is :660-663 and :707-722.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * THE NUMBERS ARE THE CANONICAL NTSC ONES, NOT InfoNES's ROUNDINGS
 * ----------------------------------------------------------------------------
 * 341 dots a line, 262 lines, 113 2/3 CPU cycles a line, 29,780.5 a frame,
 * 60.0988 Hz, 256x240 visible (nesdev's cycle reference chart). InfoNES's own
 * 113 / 29,828 / vblank-at-243 roundings are DELIBERATELY not taken. The
 * fractional line is a ROTATING 113/114/114 TRIPLE, which is exact over three
 * scanlines and needs no accumulator (SPEC.md 91.4.2).
 *
 * WAVE 1 SCOPE. The interrupt plumbing, the reset sequence, the cycle triple
 * and the rate counters are HERE, because the CPU exists in this build and
 * every one of them is about the CPU. The FRAME LOOP itself is wave 2's, with
 * the composer it drives - a loop that runs 262 lines and draws none of them
 * would be code nobody could check.
 * ==========================================================================*/

/* --- the interrupt lines (SPEC.md 91.4.3) --------------------------------
 * IRQ is a LEVEL of exactly three sources - the mapper, the APU frame
 * counter, and nothing else - OR'd into one scratch byte and honoured between
 * instructions whenever I is clear. NMI is an EDGE, latched by the two events
 * that can raise one and cleared BY THE CORE when it takes it. The $2002 read
 * clears the vblank FLAG and never a latched edge. */
#define NI_IRQ_MAP  0x01
#define NI_IRQ_APU  0x02

#define NI_SCRB_IRQ 0x01            /* the scratch byte's bits */
#define NI_SCRB_NMI 0x02

static unsigned char ni_irq_lines;  /* the C's view: which sources assert */

static void ni_irq_poke(void)
{
    int b;

    b = os88_peek(ni_machseg, NI_O_SCR + NI_S_IRQ) & NI_SCRB_NMI;
    if (ni_irq_lines)
        b |= NI_SCRB_IRQ;
    os88_poke(ni_machseg, NI_O_SCR + NI_S_IRQ, b);
}

static void ni_irq_raise(int src)
{
    ni_irq_lines |= (unsigned char)src;
    ni_irq_poke();
}

static void ni_irq_clear(int src)
{
    ni_irq_lines &= (unsigned char)~src;
    ni_irq_poke();
}

static void ni_nmi_raise(void)
{
    os88_poke(ni_machseg, NI_O_SCR + NI_S_IRQ,
              os88_peek(ni_machseg, NI_O_SCR + NI_S_IRQ) | NI_SCRB_NMI);
}

/* ni_halt - end the run at the next instruction boundary, from inside an
 * instruction. The core reads the byte between instructions, so a bus handler
 * that wants the slice to stop (a JAM, or a harness) sets it and returns
 * normally rather than unwinding a stack it does not own. */
static void ni_halt(void)
{
    os88_poke(ni_machseg, NI_O_SCR + NI_S_HALT, 1);
}

/* --- the frame-IRQ counter (SPEC.md 91.4.3) ------------------------------
 * The 4-step frame IRQ IS raised every 29,830 cycles when it is not
 * inhibited, and it is not optional: a game that waits on it hangs forever
 * otherwise. A counter in the C, decremented once a scanline by the frame
 * loop, is enough - the whole point of the number is its period, not its
 * phase. */
#define NI_APU_FRAME_CYC 29830

static int ni_apu_cyc;

static void ni_apu_tick(int cycles)
{
    ni_apu_cyc -= cycles;
    if (ni_apu_cyc <= 0) {
        ni_apu_cyc += NI_APU_FRAME_CYC;
        if (!ni_apu_inhib && !ni_apu_mode) {
            ni_apu_irq = 1;
            ni_irq_raise(NI_IRQ_APU);
        }
    }
}

/* --- the line cadence ----------------------------------------------------
 * 113, 114, 114, repeating: 341 dots over three CPU cycles a dot is 341
 * cycles for three lines exactly, and a rotating triple is what spends them
 * with no fraction and no accumulator (SPEC.md 91.4.2). */
static const unsigned char ni_line_cyc[3] = { 113, 114, 114 };
static int ni_line_phase;

static int ni_line_budget(void)
{
    int c;

    c = (int)ni_line_cyc[ni_line_phase];
    ni_line_phase++;
    if (ni_line_phase >= 3)
        ni_line_phase = 0;
    return c;
}

/* --- the wall clock (SPEC.md 91.6.3) -------------------------------------
 * NOTHING IN THIS OS IS 60.0988 Hz and nothing can be: OSAPI_FSX_WAIT offers
 * FSXW_TICK at 18.2065, FSXW_VSYNC at the CURRENT MODE's retrace (70 in mode
 * 13h, 60 in Mode X and CGA, 50 on Hercules) and FSXW_FRAME at 18.2065, or
 * 54.6195 with FSXF_FASTTICK armed. Pacing on the RETRACE - which the first
 * draft did - runs the game 16.5% fast on a 486 in mode 13h and 17% slow on
 * Hercules, and makes the panel report 116%: an adapter-dependent speed, and
 * invisible on the target only because the target never reaches the clock.
 *
 * So: FSXW_FRAME with FSXF_FASTTICK, and 60.0988 / 54.6195 = 1.1002 emulated
 * frames a sub-tick, carried as `acc += 110; while (acc >= 100) { frame();
 * acc -= 100; }` - one word, no long, 0.02% error.
 *
 * AND IT COUNTS ELAPSED TIME, NOT WAIT CALLS (SPEC.md 91.6.3): the increment
 * is `(os88_ticks() - last) * 330` at 18.2065 Hz, because a machine that
 * cannot keep up does not get to pretend the clock waited for it. The
 * catch-up is BOUNDED at four emulated frames a wake; beyond that the debt is
 * DROPPED and an overload counter is bumped, and the 8088 lives in that
 * branch permanently - which is what "a non-interactive demonstration" means
 * in SPEC.md 91.12's table. Wave 2 spends these; they are here because they
 * are the pacer's contract and not the composer's. */
#define NI_ACC_PER_SUBTICK 110      /* 1.1002 frames a sub-tick, x100 */
#define NI_ACC_FRAME       100
#define NI_ACC_PER_TICK    330      /* ...and per WHOLE tick, for the elapsed
                                     * form: 3.3009 x 100 */
#define NI_CATCHUP_MAX     4

static unsigned ni_acc;
static unsigned ni_last_tick;
static unsigned ni_overload;        /* frames the machine could not keep */

/* --- the two rates (SPEC.md 91.7.1) -------------------------------------- */
static unsigned ni_frames_emu;      /* emulated frames since the last sample */
static unsigned ni_frames_drawn;    /* ...and painted ones */
static unsigned ni_rate_nes;        /* percent of a real NES, a plain unsigned
                                     * (SPEC.md 91.7.1: there is no float and
                                     * no formatter here) */
static unsigned ni_rate_spf;        /* HUNDREDTHS of a second per painted
                                     * frame - tenths made a 486 read
                                     * `0.0 s/frame`, which is a field saying
                                     * nothing rather than saying it is fast */
static int ni_rate_known;           /* ...and whether a session has measured
                                     * them yet: `--` is the honest answer
                                     * until one has */

/* ==========================================================================
 * THE RESET SEQUENCE
 * ========================================================================*/

/* ni_reset_machine - a power-on or a Reset. The 6502's own sequence (SPEC.md
 * 91.4.3): the stack pointer drops by three, I is set, and PC is read from
 * the vector at $FFFC - through the BANKED reader, because the vector is in
 * PRG and the window table is what says where that is.
 *
 * `hard` zeroes the emulated RAM as well, which a power-on does and a Reset
 * does not. */
static void ni_reset_machine(int hard)
{
    if (hard) {
        ni_fill(ni_machseg, NI_O_RAM, 0, 2048);
        ni_fill(ni_machseg, NI_O_SRAM, 0, 8192);
        ni_fill(ni_machseg, NI_O_VRAM, 0, 2048);
        ni_fill(ni_machseg, NI_O_OAM, 0, 256);
        ni_fill(ni_machseg, NI_O_PAL, 0, 32);
        ni_m.a = 0;
        ni_m.x = 0;
        ni_m.y = 0;
        ni_m.s = 0xFD;              /* what the sequence leaves after three
                                     * dummy pushes from $00 */
        ni_ctrl = 0;
        ni_pmask = 0;
        ni_status = 0;
        ni_v = 0;
        ni_t = 0;
        ni_fx = 0;
        ni_wlatch = 0;
        ni_rdbuf = 0;
        ni_oamaddr = 0;
        ni_pad = 0;
        ni_pad_shift = 0;
        ni_pad_strobe = 0;
        ni_apu_mode = 0;
        ni_apu_inhib = 0;
        ni_apu_irq = 0;
        ni_apu_cyc = NI_APU_FRAME_CYC;
        ni_chr_dirty_mask = 0xFF;   /* every bank decodes on the first frame */
    }
    ni_irq_lines = 0;
    os88_poke(ni_machseg, NI_O_SCR + NI_S_IRQ, 0);
    os88_poke(ni_machseg, NI_O_SCR + NI_S_HALT, 0);
    os88_poke(ni_machseg, NI_O_SCR + NI_S_BUS, 0);
    ni_pokew(ni_machseg, NI_O_SCR + NI_S_BLO, 0);
    ni_pokew(ni_machseg, NI_O_SCR + NI_S_BOUND, 0);   /* the first fetch
                                                       * re-biases */
    ni_dma_debt = 0;
    ni_dma_odd = 0;
    ni_line_phase = 0;
    ni_frames_emu = 0;
    ni_frames_drawn = 0;
    ni_overload = 0;
    ni_acc = 0;
    ni_map_reset();
    ni_boot();                      /* nicpu.inc: S -= 3, I set, PC <- $FFFC */
}

/* ==========================================================================
 * WAVE 2 - THE FRAME LOOP, THE PACER AND THE BRACKET'S OWN LOOP
 *
 * This is the half InfoNES_Cycle / InfoNES_HSync (src/InfoNES.cpp:556-751) is
 * read for: run a line's worth of CPU cycles, call the mapper's per-line
 * hook, draw one line, and resolve sprite-0 hit by SPLITTING that line's
 * budget at the sprite's x. The frame-skip gate is :660-663 and :707-722 -
 * THE CPU AND THE MAPPERS RUN EVERY FRAME and only the composer and the
 * present are skipped, because a skipped CPU frame is a different game.
 * ========================================================================*/

/* --- frame skip (the .rc's ﾌﾚｰﾑｽｷｯﾌﾟ, nimenu.c's live item) -------------
 * `Auto` is not a number: it draws the LAST frame of a catch-up burst and
 * skips the rest, so the skip is whatever the machine turns out to need. On a
 * 4.77 MHz 8088 that is three of every four emulated frames, permanently,
 * which is what SPEC.md 91.12's "non-interactive demonstration" means in
 * code. An explicit 1..4 draws every (k+1)th frame whatever the machine is
 * doing, which is what a user picks when they want a STEADY rate rather than
 * the fastest one. */
#define NI_SKIP_AUTO 0
#define NI_SKIP_MAX  4
static int ni_skip = NI_SKIP_AUTO;
static int ni_skipn;                /* how many more to skip before drawing */

/* --- the 240 -> 200 row reduction (SPEC.md 91.6.2) ----------------------
 * In any 200-row mode 240 NES rows do not fit. Crop 8 top and 8 bottom to
 * 224, then drop 24 of those 224 through a table where 0xFF means "this line
 * is not displayed at all".
 *
 * THE TABLE'S SHAPE IS InfoNES's PPU_ScalingTable (gba/src/InfoNES_Advance/
 * InfoNES.cpp:244-261) AND THE ARITHMETIC IS THIS PORT'S: InfoNES's keeps 5
 * of every 7 lines for the GBA's 160-row screen, which is a 224->160
 * reduction and not a 224->200 one.
 *
 * It is computed rather than listed because the arithmetic is the SPEC and a
 * 240-byte literal table is 240 chances to mistype it. The "first source row
 * with this destination" rule keeps EXACTLY 200 of the 224, which is what
 * niuitest asserts and what nimemtest checks the bound of.
 *
 * IT IS `* 25 / 28` AND NOT `* 200 / 224`, WHICH IS THE SAME FRACTION AND
 * NOT THE SAME PROGRAM. `int` is SIXTEEN BITS here (SPEC.md 73.7), so
 * `i * 200` passes 32,767 at i = 164 and every row from there down got a
 * NEGATIVE quotient: the table's last 60 entries were garbage, 36 destination
 * rows were never presented at all, and what a user saw was a picture missing
 * its bottom fifth with the mode's own border showing through. `i * 25` tops
 * out at 5,575.
 *
 * AND IT IS THE SECOND 16-BIT OVERFLOW THIS PACKAGE HAS SHIPPED - nirom.c's
 * length check was the first - so the lesson is written twice: **a 16-bit
 * overflow is invisible to niuitest by construction**, because the host's
 * `int` is thirty-two bits and the product does not wrap there. The answer is
 * arithmetic that cannot overflow rather than a test that cannot see, and
 * niuitest's `rowtab` step now re-computes this table with the multiply
 * TRUNCATED TO SIXTEEN BITS and fails when the two disagree, which is the one
 * shape of that defect a host harness CAN be made to see. */
static unsigned char ni_rowdst[240];

static void ni_rowtab(void)
{
    int i, d, last;

    for (i = 0; i < 240; i++)
        ni_rowdst[i] = 0xFF;
    last = -1;
    for (i = 0; i < 224; i++) {
        d = (i * 25) / 28;          /* 200/224, in numbers that fit */
        if (d != last) {
            ni_rowdst[8 + i] = (unsigned char)d;
            last = d;
        }
    }
}

/* --- sprite-0, and the one-frame latency this port states (R7) ----------
 * The strike is found IN THE MERGE, which runs at the START of a line and
 * therefore knows about the line it is on. What it is USED for is the CPU
 * budget split, and that split is applied on the NEXT frame's same line: a
 * status bar is stable frame to frame, so the x found on frame N is the x
 * frame N+1 wants, and on a SKIPPED frame - where no merge ran - the carried
 * pair is the only thing there is. SPEC.md 91.5.2 records it as the
 * approximation it is. */
static int ni_s0_line = -1;
static int ni_s0_x = -1;
static int ni_s0_newline, ni_s0_newx;

/* --- what the bracket's own keys leave behind --------------------------
 * There is no glass for a sentence inside the bracket - the panel is not on
 * the screen and a toast would land on a bar the user cannot see (SPEC.md
 * 91.6.5's own argument) - so `M` and `C` set this and the panel reads it on
 * the way out. 0 none, 1 the mute fact, 2 the clip fact; nipanel.c's
 * NI_FACT_* constants are declared in a file #included after this one, which
 * is why this is a number and not one of them. */
static int ni_brkfact;
static int ni_quit;                 /* F or Esc: leave at the next boundary */
static int ni_req_reset;            /* R: reset at the next frame boundary */
static int ni_halted;               /* the core asked to stop (a KIL) */
static unsigned ni_rate_t0;         /* the tick the session started on */

/* --- the CPU slice, with the DMA debt subtracted from it (91.4.3) -------
 * `ni_run` may overrun what it was asked for by at most one instruction, and
 * what it did not spend is left NEGATIVE in ni_m.cnt - so the overrun is
 * CARRIED into the next slice rather than dropped, and the frame's total
 * cycle count stays honest over 262 lines. An OAM DMA's 513 or 514 cycles are
 * spent the same way: subtracted from the scanline budget and carried into
 * the following lines WITHOUT executing instructions, because that is what
 * the hardware does with them. */
static int ni_owe;

static void ni_slice(int cyc)
{
    int ran;

    cyc -= ni_owe;
    ni_owe = 0;
    if (cyc <= 0) {
        ni_owe = -cyc;              /* AND THE TEST IS HERE, ABOVE THE DMA
                                     * BLOCK, because the compare below is
                                     * UNSIGNED: ni_dma_debt is `unsigned`, so
                                     * a cyc of -3 casts to 65533, the compare
                                     * reads false, and a slice in which
                                     * nothing elapsed would be charged the
                                     * whole 513-cycle stall. ni_owe can
                                     * exceed a slice whenever the sprite-0
                                     * split hands out a small part (a strike
                                     * at x < 16 gives 0..6) or the core
                                     * overran the last one. From here down
                                     * `cyc` is positive, which is what the
                                     * rest of the function already assumes */
        return;
    }
    if (ni_dma_debt) {
        if ((unsigned)cyc <= ni_dma_debt) {
            ni_dma_debt -= (unsigned)cyc;
            ni_apu_tick(cyc);
            return;
        }
        cyc -= (int)ni_dma_debt;
        ni_apu_tick((int)ni_dma_debt);
        ni_dma_debt = 0;        /* and cyc is still positive here: the
                                 * branch above is taken only when cyc
                                 * EXCEEDS the debt, and both are positive */
    }
    if (ni_halted)
        return;
    if (ni_run(cyc) == NI_RUN_STOP)
        ni_halted = 1;
    ran = cyc - ni_m.cnt;
    if (ni_m.cnt < 0)
        ni_owe = -ni_m.cnt;
    ni_apu_tick(ran);
}

/* --- the pad and the system keys, swept every ~16 scanlines (R6) --------
 * NOT once a frame. An emulated frame is 340 ms or more of wall time on the
 * target and a human press is 80-150 ms, so a once-a-frame poll is a ~3 Hz
 * sample: it misses most presses outright and stretches the ones it catches
 * across twenty emulated frames.
 *
 * WHAT THAT COSTS, ARITHMETIC RATHER THAN AN IMPRESSION: the gate is
 * `(y & 15) == 0` over y = 0..239, so it fires FIFTEEN times, and each sweep
 * makes NINE os88_key_down calls - 135 OSAPI far calls a frame at 46.7 us
 * each (PERFORMANCE.md), about 6.3 ms of a ~340 ms 8088 frame, under 2% -
 * plus fifteen int 16h status polls in ni_sysdrain. The extra over a
 * once-a-frame sweep is ~126 calls, ~5.9 ms. (An earlier note here said "30
 * extra reads - 1.4 ms", which was a fifth of the truth in the file that owns
 * the frame budget.) If that 2% is ever wanted back, the note to leave is
 * that one sweep could read all nine scancodes through a single call if the
 * SDK grew a mask slot - not that it is free today. */
#define NI_SC_UP     0x48
#define NI_SC_DOWN   0x50
#define NI_SC_LEFT   0x4B
#define NI_SC_RIGHT  0x4D
#define NI_SC_X      0x2D
#define NI_SC_Z      0x2C
#define NI_SC_ENTER  0x1C
#define NI_SC_RSHIFT 0x36
#define NI_SC_PGUP   0x49
#define NI_SC_PGDN   0x51

static void ni_sweep(void)
{
    int p;

    p = 0;
    if (os88_key_down(NI_SC_UP))     p |= NI_PAD_UP;
    if (os88_key_down(NI_SC_DOWN))   p |= NI_PAD_DOWN;
    if (os88_key_down(NI_SC_LEFT))   p |= NI_PAD_LEFT;
    if (os88_key_down(NI_SC_RIGHT))  p |= NI_PAD_RIGHT;
    if (os88_key_down(NI_SC_X))      p |= NI_PAD_A;
    if (os88_key_down(NI_SC_Z))      p |= NI_PAD_B;
    if (os88_key_down(NI_SC_ENTER))  p |= NI_PAD_START;
    /* RSHIFT AND NOTHING ELSE. A draft read Space here as a second Select
     * and SPEC.md 91.6.4 credited it to agnes: agnes binds Select to RSHIFT
     * alone (examples/simple_sdl2.c:116), InfoNES binds it to `A` in every
     * front end (sdl:518, linux:236-239), and no reference in this port's
     * three names Space at all. It was on no surface either - not a legend
     * row, not a menu - which is the same defect this port deleted `O` = Open
     * ROM for: what is not in a reference is not on the glass. */
    if (os88_key_down(NI_SC_RSHIFT))
        p |= NI_PAD_SEL;
    ni_pad_live = (unsigned char)p;
    ni_pad = (unsigned char)(ni_pad | p);
}

/* ni_sysdrain - what was TYPED, DRAINED and never sampled (SPEC.md 91.6.4).
 * A queue that is sampled one key a sweep is a machine that ignores you when
 * you type quickly, which on a 2 fps emulator is all the time. */
static void ni_sysdrain(void)
{
    int k, a, s;

    for (;;) {
        k = ni_getkey();
        if (k == -1)
            return;
        a = k & 0xFF;
        s = (k >> 8) & 0xFF;
        if (a == 27 || a == 'f' || a == 'F') {
            ni_quit = 1;            /* SPEC.md 11.2.1's own pair, not
                                     * InfoNES's Q */
            return;
        }
        if (a == 'r' || a == 'R')
            ni_req_reset = 1;       /* at the FRAME boundary: a reset in the
                                     * middle of a line would leave the
                                     * scanline counter and the core
                                     * disagreeing about which line it is */
        else if (a == 'm' || a == 'M')
            ni_brkfact = 1;         /* refused, printing the fact - on the
                                     * panel, when we get back to it */
        else if (a == 'c' || a == 'C')
            ni_brkfact = 2;         /* the clip is FORCED in a 200-row mode
                                     * and there is nothing to toggle */
        else if (s == NI_SC_PGUP) {
            if (ni_skip < NI_SKIP_MAX)
                ni_skip++;
        } else if (s == NI_SC_PGDN) {
            if (ni_skip > 0)
                ni_skip--;
        }
    }
}

/* ==========================================================================
 * ONE FRAME - 262 lines
 * ========================================================================*/

static void ni_frame(int draw)
{
    int y, bud, part, hit, sx;

    /* BOTH OF THESE ARE THE COMPOSER'S AND SO BOTH ARE SKIPPED WITH IT.
     * ni_bgpal/ni_sppal are read only by niband.inc's _ni_bg_line and
     * _ni_spr_line, and the tile cache only by pass 2 of _ni_bg_line, so on a
     * skipped frame every byte they produce is thrown away - and neither is
     * cheap. One dirty CHR bank is ~41,000 cycles (~8.6 ms on the target,
     * nimap.c), a CHR-RAM title dirties banks from its vblank writes on EVERY
     * frame drawn or not, and Auto skip on an 8088 skips three frames per
     * painted one: ~200 ms of discarded decode per picture. ni_pal_build's 25
     * os88_peek NEAR calls (~0.3 ms) go the same way, because a game that
     * rewrites palette RAM every frame leaves ni_pal_dirty set every frame -
     * and that is a THIRD of a millisecond and not the 1.2 an earlier note
     * here claimed: os88_peek is a package-local thunk of three instructions
     * (apps/cc/os88thunk.asm), ~11 us of near call each, not an OSAPI slot at
     * 46.7 (apps/cc/os88.h:953). The saving that pays for this branch is the
     * cache sync, not the palette.
     *
     * IT IS BEHAVIOUR-PRESERVING BY CONSTRUCTION: ni_chr_dirty_mask only ORs
     * bits and is cleared only by ni_cache_sync, ni_pal_dirty only sets and is
     * cleared only by ni_pal_build - so everything dirtied while frames were
     * skipped is still decoded exactly once, on the frame that composes it,
     * from the CHR and the palette as they stand at DRAW time, which is what
     * that frame should show. */
    if (draw) {
        if (ni_pal_dirty)
            ni_pal_build();
        ni_cache_sync();
        ni_oam_grab();
        ni_cs.cacheseg = ni_cacheseg;
        ni_cs.machseg = ni_machseg;
        ni_cs.mirror = ni_mirror;
        ni_cs.sph = (ni_ctrl & 0x20) ? 16 : 8;
        ni_cs.sptile = (ni_ctrl & 0x08) ? 256 : 0;
        ni_cs.bgtile = (ni_ctrl & 0x10) ? 256 : 0;
        ni_s0_newline = -1;
        ni_s0_newx = -1;
    }

    for (y = 0; y < 240; y++) {
        /* dot 257 at line granularity: the HORIZONTAL bits of t into v */
        if (ni_rendering())
            ni_v = (ni_v & 0xFBE0) | (ni_t & 0x041F);

        hit = -1;
        if (draw) {
            ni_cs.v = ni_v;
            ni_cs.line = (unsigned)y;
            ni_cs.fx = ni_fx;
            ni_cs.mask = ni_pmask;
            ni_cs.ovf = 0;
            ni_bg_line();
            hit = ni_spr_line();
            if (ni_cs.ovf)
                ni_status = (unsigned char)(ni_status | 0x20);
            if (hit >= 0 && ni_s0_newline < 0) {
                ni_s0_newline = y;
                ni_s0_newx = hit;
            }
            /* AND FORTY OF THESE 240 LINES ARE COMPOSED AND NEVER SHOWN.
             * The drop table discards 40 source rows (SPEC.md 91.6.2), but
             * ni_bg_line and ni_spr_line run on every one of them - about
             * 11,300 cycles a line once the ~3,300-cycle present is taken
             * out, so ~452,000 cycles, ~95 ms of a ~1,030 ms painted frame on
             * a 4.77 MHz 8088. About 9% of a picture, spent on pixels that by
             * construction reach no framebuffer.
             *
             * IT IS NOT A MECHANICAL CUT and that is why it is a note. A
             * dropped line's ni_spr_line still supplies the ninth-sprite
             * overflow bit and the sprite-0 strike, and the strike test reads
             * ni_line for background opacity, so ni_bg_line cannot simply be
             * skipped with it. The safe subset - skip the background pass on
             * a dropped line only when sprites are disabled or OAM entry 0
             * does not fall on it - is a wave-3 measurement, and SPEC.md
             * 91.6.2 carries the 95 ms so the picture path is priced from a
             * number that includes it. */
            if (ni_rowdst[y] != 0xFF)
                ni_present13((int)ni_rowdst[y]);
        }

        /* WHERE THE STRIKE IS, AND WHICH FRAME KNOWS IT (the plan's R7, with
         * the refinement SPEC.md 91.5.2 records).
         *
         * The X is the CARRIED one - last frame's, on this same line - which
         * is what R7 asks for and what makes the split work across a SKIPPED
         * frame, where no merge ran at all. But the FLAG is raised on the
         * frame the merge found the strike, and that half is not an
         * optimisation: a game that spins on $2002 bit 6 waiting for sprite 0
         * gets nothing at all on the FIRST frame if the flag can only come
         * from a previous one, and what it does with the wait is overrun its
         * vblank and write half a nametable. That was on the glass -
         * Concentration Room's menu appeared and disappeared frame by frame,
         * with a band of it composed from a scroll the game had not finished
         * setting.
         *
         * So: this frame's own hit line raises the flag; the carried x says
         * WHERE in the line, and this frame's own x stands in when there is no
         * carried one. */
        sx = -1;
        if (hit >= 0)
            sx = (y == ni_s0_line && ni_s0_x >= 0) ? ni_s0_x : hit;
        else if (!draw && y == ni_s0_line && ni_s0_x >= 0)
            sx = ni_s0_x;

        bud = ni_line_budget();
        if (sx >= 0) {
            /* THE SPRITE-0 SPLIT: run the cycles up to the sprite's x, raise
             * the flag, run the rest. A status bar that changes the scroll on
             * the strike then changes it at the right place in the line
             * rather than at the start of the next one, which is the whole of
             * why the split exists (InfoNES_GetSprHitY, InfoNES.cpp:1053). */
            part = (bud * sx) / 256;        /* 114 * 255 = 29,070: no fold */
            ni_slice(part);
            ni_status = (unsigned char)(ni_status | 0x40);
            ni_slice(bud - part);
        } else {
            ni_slice(bud);
        }
        ni_map_hsync();
        if (ni_rendering())
            ni_v_incy();
        if ((y & 15) == 0) {
            ni_sweep();
            ni_sysdrain();
        }
    }

    /* line 240: post-render, and nothing happens on it */
    ni_slice(ni_line_budget());

    /* line 241: VBLANK, and the NMI with it if $2000 asked for one */
    ni_status = (unsigned char)(ni_status | NI_ST_VBL);
    if (ni_ctrl & NI_CTRL_NMI)
        ni_nmi_raise();
    ni_slice(ni_line_budget());

    for (y = 242; y < 261; y++)
        ni_slice(ni_line_budget());

    /* line 261: PRE-RENDER. vblank, sprite 0 and the overflow flag all clear
     * at dot 1, and the VERTICAL bits of t go into v at dots 280-304.
     *
     * THE LINE IS SPLIT AT DOT 280 AND THAT IS NOT A DETAIL. A game's vblank
     * routine points $2006 at the nametable, writes its tiles through $2007 -
     * which leaves `v` holding a NAMETABLE ADDRESS, not a scroll - and then
     * sets the scroll back with $2005/$2006 on its way out, which on this
     * machine is usually still inside the pre-render line. Copy t's vertical
     * bits at the START of the line and that restore lands AFTER the copy, so
     * `v` enters the visible frame holding wherever the last $2007 write left
     * it and the picture is composed from the wrong rows.
     *
     * That was on the glass and it is what a scanline model has to get right
     * to be usable at all: Concentration Room's lower half rendered from a
     * scroll about forty rows out, with `Vs. CPU` sitting on the copyright
     * line and `room lite 0.02a` off the screen entirely. Dot 280 of 341 is
     * cycle 93 of the line's 113 or 114, and `bud * 280` tops out at 31,920,
     * so the fold is exact in sixteen bits. */
    ni_status = (unsigned char)(ni_status & 0x1F);
    bud = ni_line_budget();
    part = (bud * 280) / 341;
    ni_slice(part);
    if (ni_rendering())
        ni_v = (ni_v & 0x041F) | (ni_t & 0x7BE0);
    ni_slice(bud - part);
    if (ni_rendering())
        ni_v_incy();

    if (draw) {
        ni_s0_line = ni_s0_newline;
        ni_s0_x = ni_s0_newx;
    }
    ni_frames_emu++;
    if (draw)
        ni_frames_drawn++;
}

/* ni_one - one emulated frame, and the frame-skip decision in front of it.
 * `last` says this is the last frame of the current catch-up burst, which is
 * the whole of what Auto means. */
static void ni_one(int last)
{
    int draw;

    if (ni_skip == NI_SKIP_AUTO) {
        draw = last;
    } else {
        draw = (ni_skipn == 0);
        if (draw)
            ni_skipn = ni_skip;
        else
            ni_skipn--;
    }
    ni_frame(draw);
    if (ni_req_reset) {
        ni_req_reset = 0;
        ni_reset_machine(0);
    }
}

/* --- the two rates, out of integers (SPEC.md 91.7.1) --------------------
 * There is no float here and no formatter, so both are folded by hand.
 *
 *   percent of a NES = frames / (ticks / 18.2065) / 60.0988 * 100
 *                    = frames * 30.29 / ticks, held as * 303 / (ticks * 10)
 *   HUNDREDTHS of a second a painted frame = ticks * 0.054926 * 100 / drawn
 *                    = ticks * 5.4926 / drawn, held as ticks * 55 / (10 *
 *                    drawn) - the field prints it as `n.nn s/frame`
 *
 * The window is CLAMPED before either multiply, because `unsigned` is sixteen
 * bits (SPEC.md 73.7) and frames * 303 passes 65,535 at 217 frames - which is
 * four seconds of a 486 and not an exotic case at all. */
#define NI_RATE_WINDOW 36           /* ~2 seconds of ticks */

static void ni_rate_calc(unsigned now)
{
    unsigned dt, f;

    dt = now - ni_rate_t0;
    if (dt < 5)
        return;                     /* under a quarter second: nothing to say,
                                     * and `--` is the honest answer */
    if (dt > 300)
        dt = 300;                   /* the window is normally 36 ticks; a
                                     * machine that spent minutes inside one
                                     * wake would wrap `dt * 55` otherwise */
    f = ni_frames_emu;
    if (f > 200)
        f = 200;                    /* IT IS A ROLLING WINDOW AND NOT THE
                                     * SESSION, and that is the fix rather
                                     * than the tidying: the first version
                                     * measured over the whole bracket and
                                     * clamped both figures to keep the
                                     * multiplies inside sixteen bits, so a
                                     * minute at full speed came out as
                                     * `NES 10%  0.0 s/frame` - which is
                                     * 200 * 303 / (600 * 10) exactly, the
                                     * clamps dividing by each other, and no
                                     * measurement at all */
    ni_rate_nes = (f * 303U) / (dt * 10U);
    if (ni_frames_drawn) {
        f = ni_frames_drawn;
        if (f > 200)
            f = 200;
        ni_rate_spf = (dt * 55U) / (10U * f);
    } else {
        ni_rate_spf = 0;
    }
    ni_rate_known = 1;
    ni_rate_t0 = now;
    ni_frames_emu = 0;
    ni_frames_drawn = 0;
}

#ifdef NITEST
/* ==========================================================================
 * NITEST - THE WHOLE-EMULATOR ROM GATE (SPEC.md 91.14.4)
 *
 * A build of THE PACKAGE, not a second program: `make nisystest` compiles
 * apps/infones/infones.c a second time with -DNITEST, and everything below is
 * the only difference. What that buys is the thing a separate harness cannot
 * have - the ROM runs through the SHIPPING loader, the SHIPPING core, the
 * SHIPPING PPU and the SHIPPING composer, inside the SHIPPING bracket, on
 * task 0's real 512-byte stack under the kernel's real frames.
 *
 * `make nicputest` cannot do this and that is why this exists: it is
 * nasm-only, so nippu.c, nirun.c and nimap.c - and therefore ppu_vbl_nmi,
 * which tests exactly them - have nowhere to run in it.
 *
 * THE PROTOCOL is blargg's, and it is a bounded state machine rather than a
 * wait: the signature $DE $B0 $61 at $6001-$6003 says the channel is open,
 * $6000 is $80 while the test runs, $81 means "reset me and carry on", and
 * anything below $80 is the result - 0 pass, otherwise the failure number,
 * with a NUL-terminated report at $6004. A ROM that never opens the channel
 * or never finishes is a TIMEOUT, printed as one, because a harness that
 * hangs is a harness nobody runs twice.
 * ========================================================================*/
#define NI_T_START   0              /* the channel has not opened */
#define NI_T_RUN     1              /* ...it has, and $6000 is $80 */
#define NI_T_DONE    2
#define NI_T_TIMEOUT 3
#define NI_TEST_CAP  2400           /* 40 seconds of emulated frames */
#define NI_TEST_RESET 6             /* ~100 ms, in frames */

static int ni_test_state;
static int ni_test_res;
static unsigned ni_test_frames;
static int ni_test_rstc;            /* frames left before the asked-for reset */

static void ni_test_poll(void)
{
    int st;

    if (ni_bread(0x6001) != 0xDE || ni_bread(0x6002) != 0xB0
        || ni_bread(0x6003) != 0x61)
        return;                     /* the channel is not open yet */
    st = ni_bread(0x6000);
    if (st == 0x80) {
        ni_test_state = NI_T_RUN;
        return;
    }
    if (st == 0x81) {
        /* THE RESET REQUEST, and it is a real reset after a real delay: the
         * ROM writes $81 and expects the console's own button about a tenth
         * of a second later. Resetting immediately makes the ROM see the
         * reset it has not finished asking for. */
        if (ni_test_rstc == 0)
            ni_test_rstc = NI_TEST_RESET;
        else if (--ni_test_rstc == 0)
            ni_reset_machine(0);
        return;
    }
    if (ni_test_state == NI_T_RUN && st < 0x80) {
        ni_test_state = NI_T_DONE;
        ni_test_res = st;
        ni_quit = 1;
    }
}
#endif

/* ==========================================================================
 * ni_bracket - THE LOOP INSIDE SPEC.md 53's BRACKET
 *
 * Called by nifsx.inc's ni_fsx_main with the mode already set, the DAC
 * already programmed and the stack sentinel already laid. It returns when the
 * user presses F or Esc, and the kernel then restores the desktop whole.
 *
 * IT COUNTS ELAPSED TIME AND NOT WAIT CALLS (SPEC.md 91.6.3, the plan's R6).
 * `acc += (ticks - last) * 330` at 18.2065 Hz is 3.3009 emulated frames a
 * whole tick, x100; one emulated frame is spent per 100. A machine that
 * cannot keep up does not get to pretend the clock waited for it - so the
 * catch-up is BOUNDED at four frames a wake, and past that the debt is
 * DROPPED and an overload counter is bumped. The 8088 lives in that branch
 * permanently, which is SPEC.md 91.12's whole point.
 *
 * NO ovl_* IS CALLED FROM HERE OR FROM ANYTHING IT CALLS (SPEC.md 91.6.5).
 * ========================================================================*/
void ni_bracket(void)
{
    unsigned t, dt;
    int n;

    ni_quit = 0;
    ni_req_reset = 0;
    ni_halted = 0;
    ni_owe = 0;
    ni_skipn = 0;
    ni_frames_emu = 0;
    ni_frames_drawn = 0;
    ni_overload = 0;
    ni_acc = 0;
    ni_rowtab();                    /* ...and the border fill is NOT here any
                                     * more: nifsx.inc blacks the four DAC
                                     * mirrors of NES $0F and fills the screen
                                     * with it between the mode set and the
                                     * full DAC load, so the screen never
                                     * leaves black. Filling it from here ran
                                     * AFTER ni_dac had made index 0 light
                                     * grey, which is a whole grey screen and
                                     * a 150 ms top-to-bottom wipe on a
                                     * 4.77 MHz 8088 - and sub-millisecond
                                     * under QEMU, which is why no screenshot
                                     * of this port could show it */
    ni_pal_dirty = 1;               /* the LUTs are built on the first frame,
                                     * whatever the last session left */
    t = os88_ticks();
    ni_last_tick = t;
    ni_rate_t0 = t;

#ifdef NITEST
    /* THE HEADLESS RUN: no accumulator, no wait, no keys - frames as fast as
     * the machine will make them, with the $6000 channel polled after each
     * one. The PRESENT still happens, because the point of running inside the
     * bracket at all is that every routine on the stack is the shipping one
     * (SPEC.md 91.4.4's sentinel is measured over exactly this). */
    ni_test_state = NI_T_START;
    ni_test_frames = 0;
    while (!ni_quit) {
        ni_one(1);
        ni_test_poll();
        if (++ni_test_frames >= NI_TEST_CAP) {
            if (ni_test_state != NI_T_DONE)
                ni_test_state = NI_T_TIMEOUT;
            ni_quit = 1;
        }
    }
    ni_rate_calc(os88_ticks());
    return;
#endif

    while (!ni_quit) {
        t = os88_ticks();
        dt = t - ni_last_tick;      /* and the wrap at 65,535 falls out of
                                     * unsigned subtraction, which is why this
                                     * is a difference and not a compare */
        ni_last_tick = t;
        if (dt > 60)
            dt = 60;                /* a floppy access or a first frame can
                                     * leave seconds owing; * 330 would wrap
                                     * sixteen bits at 199 ticks */
        ni_acc += dt * NI_ACC_PER_TICK;

        n = 0;
        while (ni_acc >= NI_ACC_FRAME && n < NI_CATCHUP_MAX && !ni_quit) {
            ni_acc -= NI_ACC_FRAME;
            n++;
            ni_one(ni_acc < NI_ACC_FRAME || n == NI_CATCHUP_MAX);
        }
        if (ni_acc >= NI_ACC_FRAME) {
            ni_acc = 0;             /* the debt is DROPPED, not banked: a
                                     * machine three seconds behind that then
                                     * tried to catch up would never draw
                                     * again */
            ni_overload++;
        }
        if ((unsigned)(t - ni_rate_t0) >= NI_RATE_WINDOW)
            ni_rate_calc(t);        /* a ROLLING window, so a long session
                                     * reports the machine it is on now */
        if (!ni_quit && ni_acc < NI_ACC_FRAME) {
            ni_sweep();             /* one more sweep across the wait, so a
                                     * press that lands inside it is not lost
                                     * for a whole sub-tick */
            ni_sysdrain();
            if (!ni_quit)
                ni_fsx_wait(2);     /* FSXW_FRAME, which YIELDS - never
                                     * FSXW_VSYNC, which is the adapter's rate
                                     * and not the NES's */
        }
    }
    ni_rate_calc(os88_ticks());     /* ...and the last partial window, so a
                                     * short session reports something */

    /* AND THE PANEL'S TEXT IS SETTLED BEFORE THIS RETURNS, not after
     * ni_fsx_go does. SPEC.md 53.6 step 4 runs wm_paint_all under the
     * still-held lock BEFORE OSAPI_FSX_RUN comes back, and that repaint is
     * our own os88_paint - so whatever ni_txt holds at this instant is what
     * the user watches being lettered for the thirteen rows of the restore.
     * Setting it afterwards drew state, rate and fact TWICE and drew the
     * state line wrong the first time (`Running` for a session that had
     * ended). nipanel.c's ni_panel_settle carries the argument. */
    ni_panel_settle();
}
