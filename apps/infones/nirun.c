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
