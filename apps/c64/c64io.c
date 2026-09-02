/* ============================================================================
 * os8088 - apps/c64/c64io.c        the $D000-$DFFF register files, the alarm
 *                                  scheduler and the two CIAs
 *
 * Part of C64 (docs/C64-SPEC.md). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. The
 * register semantics are src/vicii/vicii-mem.c, src/core/ciacore.c,
 * src/c64/c64cia1.c, src/c64/c64cia2.c, src/sid/sid.c and src/c64/c64pla.c;
 * the alarm model is src/alarm.c and src/maincpu.c; the PAL timing is
 * src/c64/c64.h:35-40 and src/vicii/vicii-timing.h. Nothing of that source is
 * vendored (C64-SPEC §2).
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * THREE INDEPENDENT PHASE ACCUMULATORS, ONE CLOCK (C64-SPEC §6.3)
 * ----------------------------------------------------------------------------
 * The clock is the emulated 6510's cycle count and nothing else (§4.2). Off
 * it run three phases, none derived from another:
 *
 *   the CIA timers   whatever their latches say. The KERNAL writes $4025 =
 *                    16,421 cycles into CIA1 timer A on a PAL machine, so the
 *                    60 Hz jiffy is not a constant of this design - it EMERGES,
 *                    and a program that reprograms timer A behaves
 *   the VIC raster   63 cycles a line, 63 x 312 = 19,656 a frame = 50.123 Hz
 *   the CIA TOD      its own 50 Hz mains accumulator, 19,705 cycles a tick
 *
 * `c64_alarm_next()` answers the cycles to the nearest of them and the slice
 * driver runs the core exactly that far (c64.c); `c64_advance()` then moves
 * every phase by what actually ran. NOTHING QUANTISES TO A JIFFY, and every
 * phase is retained across slices and across wakes.
 *
 * ONE AMENDMENT this wave made to §5.2: the end of a raster LINE is not an
 * alarm. $D012 is computed from the cycle counter when it is read, which is
 * exact and costs nothing, where a per-line alarm would have ended the run
 * 312 times a frame for a register nobody read. The raster COMPARE and the
 * frame end are alarms, so a raster interrupt still fires on the line it was
 * armed for and in the right order against every CIA interrupt.
 * ==========================================================================*/

/* --- the register files -------------------------------------------------- */
/* $D000-$D02E, mirrored every 64 bytes on the real chip; the file is 64 wide
 * and the dispatch masks the address, which is what the mirroring IS. */
static unsigned char c64_vic[64];
static unsigned char c64_sid[32];           /* $D400-$D41C */

/* Colour RAM is 1,024 NIBBLES and lives in the package's bss, not in the RAM
 * claim: it is not RAM the 6510 can bank away, and the composer reads it once
 * per cell (3.1). Bits 4-7 of a read are the open bus on the real machine;
 * this port answers 0 in them and says so. */
static unsigned char c64_col[1024];

/* --- the two CIAs, as parallel two-element arrays ------------------------ *
 * NOT a struct array: a struct of this shape is 40-odd bytes, and an index
 * into it is an `imul` the gate would have to find a scratch register for
 * (LESSONS.md 3). Two elements indexed by a literal 0 or 1 cost a shift.
 * Index 0 is CIA1 ($DC00, the IRQ line and the keyboard), 1 is CIA2 ($DD00,
 * the NMI line and the VIC bank).                                          */
static unsigned char c64_cia[2][16];        /* the written register file */
static unsigned c64_ta[2], c64_tb[2];       /* the live counters */
static unsigned c64_la[2], c64_lb[2];       /* their latches */
static unsigned char c64_icd[2];            /* ICR data (the flags) */
static unsigned char c64_icm[2];            /* ICR mask */
static unsigned char c64_iasrt[2];          /* ...and the ASSERTED OUTPUT, which
                                             * is not the same question: a CIA
                                             * interrupt already raised is
                                             * cleared by an ICR READ and by
                                             * nothing else (ciacore.c:950's
                                             * "Both pending interrupts and
                                             * currently active interrupts are
                                             * never cancelled or cleared",
                                             * and my_set_int(false) at :1345,
                                             * inside the ICR read) */
static unsigned char c64_todt[2], c64_tods[2], c64_todm[2], c64_todh[2];
static int c64_todp[2];                     /* 50 Hz ticks toward a tenth */
/* ...AND THE THREE THINGS A CIA'S TOD HAS THAT A CLOCK DOES NOT (6.1,
 * src/core/ciacore.c). Wave 2 shipped four counting registers and nothing
 * else, so the ALARM could not be set at all, the read could tear across a
 * tick, and the clock never stopped:
 *   - the ALARM registers, selected by CRB bit 7 (cia.h:90's CIA_CRB_ALARM;
 *     ciacore.c:876 writes todalarm[] instead of the clock), whose match
 *     raises ICR bit 2 (:236-242, CIA_IM_TOD = 4);
 *   - the READ LATCH: reading HOURS latches all four registers and reading
 *     TENTHS releases them (:1249-1270), so a program reading hours down to
 *     tenths cannot see the clock tick underneath it;
 *   - STOPPED: writing HOURS stops the clock and writing TENTHS restarts it
 *     (:848's comment and :886-:893), which is how a program sets a time
 *     without it advancing between the four stores. */
static unsigned char c64_alt[2], c64_als[2], c64_alm[2], c64_alh[2];
static unsigned char c64_todlt[2], c64_todls[2], c64_todlm[2], c64_todlh[2];
static unsigned char c64_todlatch[2];       /* the latch is armed */
static unsigned char c64_todstop[2];        /* the clock is stopped */

/* --- the clock (C64-SPEC §4.2, §10.2) ------------------------------- *
 * Two-word counters folded often, as rc_ticks32 does - "no long" is a C rule,
 * not a no-32-bit rule (Decision 19).                                      */
static unsigned c64_cyc_lo, c64_cyc_hi;     /* total emulated 6510 cycles */
static int c64_frame_cyc;                   /* 0..19,655 inside the PAL frame */
static unsigned c64_frames_lo;              /* emulated VIC frames, ONE word.
                                             * The high word used to be kept
                                             * beside it and read by nothing:
                                             * §10.2's fps is the DIFFERENCE
                                             * over a one-second window, taken
                                             * masked to 16 bits, so it is
                                             * exact for any window under
                                             * 65,536 frames - twenty-two
                                             * minutes - and the second word
                                             * was two bytes of bss and an
                                             * increment for nobody */
static int c64_tod_acc;                     /* the TOD's own 50 Hz phase */
static int c64_in_run;                      /* the core is between c64_run's
                                             * entry and its exit: an I/O write
                                             * that raises a line has to CUT
                                             * the run (4.4) */
static int c64_nmi_state;                   /* the NMI line's last level: NMI
                                             * is an EDGE and this is what
                                             * makes it one */

#define C64_PAL_LINE   63
#define C64_PAL_LINES  312
#define C64_PAL_FRAME  19656                /* 63 * 312 = 50.123 Hz */
#define C64_PAL_HZ     985248               /* the PAL 6510's cycles a second */
#define C64_TOD_PERIOD 19705                /* 985248 / 50, the mains tick */
/* line * 63, written as a shift and a subtract. cc8086 refuses an
 * `imul ax, ax, 63` wherever it cannot prove a scratch register dead, and a
 * PAL raster line is 63 cycles and cannot be padded (LESSONS.md 3). */
#define C64_X63(i) ((((unsigned)(i)) << 6) - ((unsigned)(i)))

/* --- the $00/$01 processor port (3.2) ------------------------------------ */
/* They are NOT RAM. $00 is the data-direction register (reset $2F) and $01
 * the data port (reset $37); a write to either re-evaluates the bank map at
 * once, so the core never executes one instruction under a stale map. */
static unsigned char c64_pp_ddr;
static unsigned char c64_pp_data;
static int c64_bank;                        /* $01 & 7 - the row of 3.3 */

/* The seven bank maps, with no cartridge (!exrom = !game = 1), from
 * src/c64/c64meminit.c. Rows 0 and 4 are the same map: the table is eight
 * entries and seven shapes. Each entry is three nibbles - what $A000-$BFFF,
 * $D000-$DFFF and $E000-$FFFF show. */
#define C64_MAP_RAM     0
#define C64_MAP_BASIC   1
#define C64_MAP_KERNAL  2
#define C64_MAP_CHARGEN 3
#define C64_MAP_IO      4

static const unsigned char c64_maps[8][4] = {
    /*  $A000        $D000            $E000        pad   $01 & 7 */
    { C64_MAP_RAM,   C64_MAP_RAM,     C64_MAP_RAM,   0 },  /* 0 */
    { C64_MAP_RAM,   C64_MAP_CHARGEN, C64_MAP_RAM,   0 },  /* 1 */
    { C64_MAP_RAM,   C64_MAP_CHARGEN, C64_MAP_KERNAL, 0 }, /* 2 */
    { C64_MAP_BASIC, C64_MAP_CHARGEN, C64_MAP_KERNAL, 0 }, /* 3 */
    { C64_MAP_RAM,   C64_MAP_RAM,     C64_MAP_RAM,   0 },  /* 4 */
    { C64_MAP_RAM,   C64_MAP_IO,      C64_MAP_RAM,   0 },  /* 5 */
    { C64_MAP_RAM,   C64_MAP_IO,      C64_MAP_KERNAL, 0 }, /* 6 */
    { C64_MAP_BASIC, C64_MAP_IO,      C64_MAP_KERNAL, 0 }  /* 7 */
};
/* the stride is FOUR and not three so that c64_maps[b][n] is a shift and
 * never an `imul` (LESSONS.md 3); the fourth byte is padding and is read by
 * nothing. */

/* ==========================================================================
 * THE CORE'S SCRATCH (3.5)
 * ========================================================================*/
static void c64_scratch_clear(void)
{
    int i;
    for (i = 0; i < C64_SCR_END; i++)
        c64_scr_wr(i, 0);
    /* the write window starts EMPTY, which is lo above hi (9.2) */
    c64_scr_wr(C64_SCR_WLO, 0xFF);
    c64_scr_wr(C64_SCR_WLO + 1, 0xFF);
    /* the watch range starts EMPTY TOO - lo above hi - so that a write before
     * the first c64_frame_regs() widens nothing. c64_watch_set fills it in. */
    c64_scr_wr(C64_SCR_WATLO, 0xFF);
    c64_scr_wr(C64_SCR_WATLO + 1, 0xFF);
}

/* c64_map_publish - the three map bytes the CORE reads on every access above
 * $A000 (c64cpu.inc's MAPA/MAPD/MAPE). They live in the emulated machine's
 * scratch and not in this file's bss because the core reads them DS-relative
 * with no override; this is the one writer. */
static void c64_map_publish(void)
{
    c64_scr_wr(C64_SCR_MAPA, c64_maps[c64_bank][0]);
    c64_scr_wr(C64_SCR_MAPD, c64_maps[c64_bank][1]);
    c64_scr_wr(C64_SCR_MAPE, c64_maps[c64_bank][2]);
    c64_rebank();                           /* ...and no instruction is ever
                                             * fetched under a stale bias */
}

/* ==========================================================================
 * RESET
 *
 * The power-on register state. $D011 = $1B (25 rows, screen on, the default
 * text mode), $D016 = $C8, $D018 = $15 (matrix at $0400, characters at
 * $1000 - which is where the VIC sees the character ROM), $D020 = 14 and
 * $D021 = 6: the light-blue border over the blue screen a C64 comes up with
 * (the VIC-II's own ladder, vicii-color.c). CIA2 PRA = $17 puts the VIC in
 * bank 0.
 * ========================================================================*/
static void c64_reset_regs(void)
{
    int i, k;

    for (i = 0; i < 64; i++)
        c64_vic[i] = 0;
    for (i = 0; i < 32; i++)
        c64_sid[i] = 0;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < 16; i++)
            c64_cia[k][i] = 0;
        c64_ta[k] = 0xFFFF;
        c64_tb[k] = 0xFFFF;
        c64_la[k] = 0xFFFF;
        c64_lb[k] = 0xFFFF;
        c64_icd[k] = 0;
        c64_icm[k] = 0;
        c64_todt[k] = 0;
        c64_tods[k] = 0;
        c64_todm[k] = 0;
        c64_todh[k] = 0;
        c64_todp[k] = 0;
        c64_iasrt[k] = 0;                   /* the asserted line, which only
                                             * an ICR read otherwise clears */
        c64_alt[k] = 0;
        c64_als[k] = 0;
        c64_alm[k] = 0;
        c64_alh[k] = 0;
        c64_todlt[k] = 0;
        c64_todls[k] = 0;
        c64_todlm[k] = 0;
        c64_todlh[k] = 0;
        c64_todlatch[k] = 0;
        c64_todstop[k] = 0;
    }
    c64_vic[0x11] = 0x1B;
    c64_vic[0x16] = 0xC8;
    c64_vic[0x18] = 0x15;
    c64_vic[0x20] = 14;
    c64_vic[0x21] = 6;
    c64_cia[0][0x02] = 0xFF;                /* PRA is all outputs: the
                                             * keyboard columns */
    c64_cia[1][0x00] = 0x17;
    c64_cia[1][0x02] = 0x3F;
    c64_pp_ddr = 0x2F;
    c64_pp_data = 0x37;
    c64_bank = 7;
    c64_frame_cyc = 0;
    c64_tod_acc = 0;
    c64_nmi_state = 0;
    for (i = 0; i < 1024; i++)
        c64_col[i] = 0;
}

/* c64_pp_write - a write to $00 or $01 re-evaluates the bank map AT ONCE
 * (3.2), and with it the core's fetch segment and boundary word - which is
 * why this is one function and not two assignments. */
static void c64_pp_write(int addr, int v)
{
    if (addr == 0)
        c64_pp_ddr = (unsigned char)v;
    else
        c64_pp_data = (unsigned char)v;
    /* a bit the DDR makes an input reads its pulled-up level, so the bank
     * bits are (data & ddr) | (~ddr & 7): an input line floats high */
    c64_bank = ((c64_pp_data & c64_pp_ddr) | (~c64_pp_ddr & 0x07)) & 7;
    c64_map_publish();
}

/* c64_pp_read - $01's read-back (3.2), and it is VICE's own formula for a
 * machine with no datasette on it.
 *
 * `data_read = (data | ~dir) & (data_out | pullup)` (src/c64/c64pla.c:55),
 * with the C64's pullup = $17 (src/c64/c64mem.c:222) - so an INPUT bit reads
 * the pull-up where the board has one and ZERO where it has none:
 *
 *   bits 0-2  the bank lines, pulled up: an input reads 1
 *   bit 3     the cassette WRITE line: no pull-up, so an input reads 0
 *   bit 4     cassette SENSE, pulled up: an input reads 1, and it stays 1
 *             because `tape_sense` is 0 with no datasette (c64pla.c:66)
 *   bit 5     the cassette MOTOR: c64pla.c:61 clears it for an input
 *             UNCONDITIONALLY, so it reads 0
 *   bits 6-7  unconnected: c64mem.c:325-335 takes an input's value from a
 *             capacitor that has discharged, which is 0
 *
 * An OUTPUT bit reads what was written to it, in every one of those columns.
 * This port used `~ddr & 0x1F`, which pulled bits 3 and 5 HIGH as inputs -
 * two bits of a register the KERNAL reads to find the datasette. */
static int c64_pp_read(int addr)
{
    if (addr == 0)
        return c64_pp_ddr;
    return ((c64_pp_data & c64_pp_ddr) | (~c64_pp_ddr & 0x17)) & 0xFF;
}

/* ==========================================================================
 * THE INTERRUPT LINES (C64-SPEC §6.1, §6.2)
 *
 * CIA1's ICR ORed with the VIC's $D019 & $D01A is the IRQ line - a LEVEL, so
 * the core re-tests it every time it can take one. CIA2's ICR is the NMI,
 * which is an EDGE: c64_nmi_state is what makes it one, and RESTORE (7.4)
 * raises the same edge from the keyboard.
 * ========================================================================*/
static void c64_irq_update(void)
{
    int line, nmi, was;

    line = 0;
    /* A CIA INTERRUPT ALREADY RAISED IS NOT CANCELLED BY A MASK CHANGE. The
     * line used to be recomputed from `flags & mask` every time, so
     * `LDA #$01 / STA $DC0D` - clearing the timer-A mask, which is what an
     * IRQ handler that is finished with the timer writes - dropped the IRQ
     * before the handler's `LDA $DC0D` had acknowledged it, and re-enabling
     * CIA2's mask manufactured a second NMI EDGE out of a flag that had never
     * been read. VICE raises the line from the flags and drops it in the ICR
     * READ (ciacore.c:950, :1345). */
    if ((c64_icd[0] & c64_icm[0] & 0x1F) != 0) {
        c64_icd[0] |= 0x80;
        c64_iasrt[0] = 1;
    }
    if (c64_iasrt[0])
        line = 1;
    /* ...AND $D019 BIT 7 IS DERIVED IN BOTH DIRECTIONS. vicii_irq_set_line
     * (vicii-irq.c:42-51) sets it when `irq_status & regs[0x1a]` and CLEARS
     * it - `irq_status &= 0x7f` - when that is empty. This only ever set it,
     * so a bit 7 raised once stayed raised through the ack that cleared every
     * source under it, and $D019 read `$8x` for ever. */
    if ((c64_vic[0x19] & c64_vic[0x1A] & 0x0F) != 0) {
        c64_vic[0x19] |= 0x80;
        line = 1;
    } else
        c64_vic[0x19] &= 0x7F;
    if ((c64_icd[1] & c64_icm[1] & 0x1F) != 0) {
        c64_icd[1] |= 0x80;
        c64_iasrt[1] = 1;
    }
    nmi = c64_iasrt[1] ? 1 : 0;

    was = c64_scr_rd(C64_SCR_IRQ);
    if (nmi && !c64_nmi_state)
        was |= 0x02;                        /* the EDGE, latched for the core */
    c64_nmi_state = nmi;
    if (line)
        was |= 0x01;
    else
        was &= ~0x01;
    c64_scr_wr(C64_SCR_IRQ, was);

    /* A LINE THAT COMES UP INSIDE A RUN HAS TO END IT. Every other way an
     * interrupt becomes due is an alarm, and an alarm is what ended the run,
     * so the core takes it at the next entry with no latency at all. An I/O
     * write is the exception: `STA $DC0D` can unmask a flag that is already
     * set, and without this the machine would not notice until the next
     * alarm. c64_cut() moves the unspent countdown into CARRY, so ending the
     * run early costs nothing in the cycle accounting (4.4). */
    if (c64_in_run && (was & 0x03) != 0)
        c64_cut();
}

/* c64_nmi_raise - RESTORE (7.4). The edge is the point: holding the key does
 * not hold the line. */
static void c64_nmi_raise(void)
{
    int v = c64_scr_rd(C64_SCR_IRQ);
    c64_scr_wr(C64_SCR_IRQ, v | 0x02);
    if (c64_in_run)
        c64_cut();
}

/* ==========================================================================
 * THE ALARM SCHEDULER (C64-SPEC §4.4, src/alarm.c)
 *
 * "Cycles to the next event", over: each CIA's timer A and timer B where they
 * are counting phi2, the VIC raster compare, the frame end and the TOD tick.
 * There is no fixed quantum anywhere in this machine.
 * ========================================================================*/
static int c64_ta_runs(int k)
{
    /* CRA: bit 0 START, bit 5 INMODE (1 = count CNT, which nothing here
     * drives, so the timer does not count) */
    return ((c64_cia[k][0x0E] & 0x21) == 0x01) ? 1 : 0;
}

static int c64_tb_runs(int k)
{
    /* CRB bits 5-6 INMODE: 00 = phi2. 10 = count timer A underflows, which is
     * not a phi2 alarm - it happens when timer A underflows, and that IS an
     * alarm already. */
    return ((c64_cia[k][0x0F] & 0x61) == 0x01) ? 1 : 0;
}

static int c64_rcmp_line(void)
{
    return (int)(((unsigned)c64_vic[0x12]) | (((unsigned)(c64_vic[0x11] & 0x80)) << 1));
}

static int c64_raster_now(void)
{
    return (int)(((unsigned)c64_frame_cyc) / C64_PAL_LINE);
}

static int c64_alarm_next(void)
{
    int d, t, k, line;

    d = C64_PAL_FRAME - c64_frame_cyc;      /* the frame end, always armed */
    t = C64_TOD_PERIOD - c64_tod_acc;
    if (t < d)
        d = t;
    /* A COUNTER AT OR ABOVE $7FFF CANNOT BE THE NEAREST ALARM, so it is
     * SKIPPED rather than cast. `(int)(0xFFFF + 1)` is 0 and any counter in
     * the top half of the range casts NEGATIVE - either way `t < d` won, `d`
     * fell to the floor below, and the slice driver then ran the core ONE
     * CYCLE at a time, each with a full c64_alarm_next + c64_run shell +
     * c64_advance over both CIAs, the VIC and the TOD. The nearest real alarm
     * is never further than the frame end (19,656), so nothing is lost. */
    for (k = 0; k < 2; k++) {
        if (c64_ta_runs(k) && c64_ta[k] < (unsigned)0x7FFF) {
            t = (int)c64_ta[k] + 1;
            if (t < d)
                d = t;
        }
        if (c64_tb_runs(k) && c64_tb[k] < (unsigned)0x7FFF) {
            t = (int)c64_tb[k] + 1;
            if (t < d)
                d = t;
        }
    }
    /* THE RASTER COMPARE IS AN ALARM WHETHER OR NOT IT IS UNMASKED. $D019
     * bit 0 is a STATUS latch: vicii_irq_raster_set (vicii-irq.c:64) sets it
     * unconditionally and only the LINE is gated by $D01A (:42). Scheduling
     * it on the mask meant that a program which polls `LDA $D019 / AND #$01`
     * with interrupts disabled - a common raster wait - never saw the bit at
     * all. It is one extra run boundary a frame. */
    line = c64_rcmp_line();
    if (line < C64_PAL_LINES) {
        t = (int)C64_X63(line) - c64_frame_cyc;
        if (t <= 0)
            t += C64_PAL_FRAME;
        if (t < d)
            d = t;
    }
    if (d < 1)
        d = 1;
    return d;
}

/* c64_tod_alarm - the four registers against the four alarm registers
 * (ciacore.c:236-242). A match raises ICR bit 2, which is CIA_IM_TOD. */
static void c64_tod_alarm(int k)
{
    if (c64_todt[k] == c64_alt[k] && c64_tods[k] == c64_als[k]
        && c64_todm[k] == c64_alm[k] && c64_todh[k] == c64_alh[k]) {
        c64_icd[k] |= 0x04;
        c64_irq_update();
    }
}

/* c64_tod_tick - one tenth of a second on the CIA's own mains accumulator.
 * BCD, as the registers are, and the HOURS are 12-hour BCD with an AM/PM bit
 * that toggles at 12 - ciacore.c:1961-1977, transcribed:
 *
 *   09 -> 10 and 12 -> 01 are the two carries out of the units digit, and
 *   BOTH are `hl = hh; hh ^= 1`; every other hour is `hl + 1`, and reaching
 *   12 that way is what flips PM.
 *
 * The old code did `if (v > 0x12) v = 1` on a plain BCD increment, so it ran
 * 09, 0A..0F, 10 - six hours that do not exist - and never touched AM/PM. */
static void c64_tod_tick(int k)
{
    int v, hl, hh, pm;

    if (c64_todstop[k])
        return;                             /* stopped by a write to HOURS */
    v = c64_todt[k] + 1;
    if (v < 10) {
        c64_todt[k] = (unsigned char)v;
        c64_tod_alarm(k);
        return;
    }
    c64_todt[k] = 0;
    v = c64_tods[k];
    v = ((v & 0x0F) + 1 >= 10) ? ((v & 0xF0) + 0x10) : (v + 1);
    if (v >= 0x60)
        v = 0;
    else {
        c64_tods[k] = (unsigned char)v;
        c64_tod_alarm(k);
        return;
    }
    c64_tods[k] = 0;
    v = c64_todm[k];
    v = ((v & 0x0F) + 1 >= 10) ? ((v & 0xF0) + 0x10) : (v + 1);
    if (v >= 0x60)
        v = 0;
    else {
        c64_todm[k] = (unsigned char)v;
        c64_tod_alarm(k);
        return;
    }
    c64_todm[k] = 0;
    hl = c64_todh[k] & 0x0F;
    hh = (c64_todh[k] >> 4) & 1;
    pm = c64_todh[k] & 0x80;
    if ((hh == 1 && hl == 2) || (hh == 0 && hl == 9)) {
        hl = hh;                            /* 12 -> 01, and 09 -> 10 */
        hh = hh ^ 1;
    } else {
        hl = (hl + 1) & 0x0F;
        if (hh == 1 && hl == 2)
            pm = pm ^ 0x80;                 /* 11 -> 12 flips AM/PM */
    }
    c64_todh[k] = (unsigned char)(hl | (hh << 4) | pm);
    c64_tod_alarm(k);
}

/* c64_timer_step - one CIA's timers, advanced by `n` phi2 cycles. Underflows
 * reload from the latch, set the ICR flag, and cascade into timer B where CRB
 * says timer B counts timer A's underflows. */
static void c64_timer_step(int k, int n)
{
    unsigned c;
    int under, casc, i, rem;

    /* THE TWO TIMERS ARE ADVANCED FROM THE SAME ELAPSED COUNT, INDEPENDENTLY,
     * and that is what `rem` is for. Timer A's loop used to consume and
     * mutate `n`, and the non-cascaded timer B below then advanced by
     * whatever was LEFT of it - so a timer B counting phi2 lost every cycle
     * timer A had already counted, and a one-shot timer A that stopped
     * (`n = 0`) stopped timer B for that whole slice as well. VICE updates
     * each timer from the same `rclk` and neither takes anything from the
     * other (src/core/ciacore.c:937); only the CASCADE mode consumes timer
     * A's underflows, which is the branch below. */
    under = 0;
    if (c64_ta_runs(k)) {
        rem = n;
        c = c64_ta[k];
        /* THE COMPARE IS UNSIGNED, AND THAT IS NOT A STYLE CHOICE. `c` is a
         * 16-bit CIA counter and the reset default latch is $FFFF - the state
         * `LDA #$FF / STA $DC04 / STA $DC05 / STA $DC0E` leaves, which is the
         * standard free-running-timer idiom AND what c64_reset_regs leaves
         * behind. `(int)0xFFFF` is -1, so `n > (int)c` was true for every
         * n >= 0 while `n -= (int)c + 1` subtracted NOTHING: an infinite loop
         * inside os88_onwake, on the UI task, minting an underflow flag for
         * ever. Any latch in $8000..$FFFE was the same defect one step slower
         * - a negative `(int)c` INCREASES n. `n` is > 0 here (c64_advance
         * returns early otherwise) and never more than one frame, so the cast
         * below is safe: it runs only where c < n <= 19,656. LESSONS.md's
         * "16-bit arithmetic wraps", in the one place it could hang. */
        while ((unsigned)rem > c) {
            rem -= (int)c + 1;
            c = c64_la[k];
            under++;
            c64_icd[k] |= 0x01;
            if ((c64_cia[k][0x0E] & 0x08) != 0) {    /* one-shot: stop */
                c64_cia[k][0x0E] &= 0xFE;
                rem = 0;
                break;
            }
        }
        c64_ta[k] = (unsigned)(c - (unsigned)rem);
    }
    casc = ((c64_cia[k][0x0F] & 0x61) == 0x41) ? 1 : 0;
    if (casc) {
        for (i = 0; i < under; i++) {
            if (c64_tb[k] == 0) {
                c64_tb[k] = c64_lb[k];
                c64_icd[k] |= 0x02;
                if ((c64_cia[k][0x0F] & 0x08) != 0) {
                    c64_cia[k][0x0F] &= 0xFE;
                    break;
                }
            } else
                c64_tb[k] = c64_tb[k] - 1;
        }
        return;
    }
    if (c64_tb_runs(k)) {
        rem = n;                            /* the SAME elapsed count timer A
                                             * saw, never what it left over */
        c = c64_tb[k];
        while ((unsigned)rem > c) {         /* unsigned, for timer A's reason */
            rem -= (int)c + 1;
            c = c64_lb[k];
            c64_icd[k] |= 0x02;
            if ((c64_cia[k][0x0F] & 0x08) != 0) {
                c64_cia[k][0x0F] &= 0xFE;
                rem = 0;
                break;
            }
        }
        c64_tb[k] = (unsigned)(c - (unsigned)rem);
    }
}

/* c64_advance - move every device phase by the cycles the core actually ran.
 * The `n` passed here is never more than one frame, because the frame end is
 * always an armed alarm and the slice never runs past its deadline by more
 * than one instruction. */
static void c64_advance(int n)
{
    int prev, now, tline, k;

    if (n <= 0)
        return;

    /* THE TWO-WORD CYCLE COUNTER (10.2), AND THE MASK IS NOT DECORATION.
     * `unsigned` is 16 bits on the 8086 and 32 on the host that runs the
     * harness, so a carry the target takes every 65,536 cycles - fifteen
     * times a second - would never happen in the model, and the harness would
     * be checking arithmetic the machine does not do. */
    c64_cyc_lo = (c64_cyc_lo + (unsigned)n) & 0xFFFFu;
    if (c64_cyc_lo < (unsigned)n)
        c64_cyc_hi = (c64_cyc_hi + 1) & 0xFFFFu;

    /* --- the VIC: the raster compare first, then the frame */
    prev = c64_frame_cyc;
    now = prev + n;
    /* the raster STATUS latches whether or not it is unmasked (vicii-irq.c:64;
     * see c64_alarm_next) - only the line is gated, in c64_irq_update */
    tline = c64_rcmp_line();
    if (tline < C64_PAL_LINES) {
        k = (int)C64_X63(tline);
        if (k <= prev)
            k += C64_PAL_FRAME;
        if (now >= k)
            c64_vic[0x19] |= 0x01;
    }
    while (now >= C64_PAL_FRAME) {
        now -= C64_PAL_FRAME;
        c64_frames_lo = (c64_frames_lo + 1) & 0xFFFFu;
    }
    c64_frame_cyc = now;

    /* --- the CIAs */
    c64_timer_step(0, n);
    c64_timer_step(1, n);

    /* --- the TOD, on its own 50 Hz accumulator */
    c64_tod_acc += n;
    while (c64_tod_acc >= C64_TOD_PERIOD) {
        c64_tod_acc -= C64_TOD_PERIOD;
        for (k = 0; k < 2; k++) {
            c64_todp[k]++;
            if (c64_todp[k] >= 5) {         /* 50 Hz mains -> tenths */
                c64_todp[k] = 0;
                c64_tod_tick(k);
            }
        }
    }

    c64_irq_update();
}

/* ==========================================================================
 * THE I/O DISPATCH
 *
 * Every $D000-$DFFF access in an I/O bank is a direct cdecl call into these
 * two from inside the core's handler (3.4), and so is every access to
 * $0000/$0001, which are the processor port and not RAM (3.2).
 * ========================================================================*/
static int c64_cia_rd(int k, int r)
{
    int v;

    switch (r) {
    case 0x00:
        if (k == 0)
            return c64_kbd_pa();
        /* CIA2 PRA IS THE SERIAL BUS, AND AN EMPTY BUS READS BACK WHAT THE
         * MACHINE ITSELF DRIVES, INVERTED. Bits 3-5 are ATN, CLK and DATA
         * OUT and bits 6-7 are CLK and DATA IN (c64cia2.c:207-213). With no
         * true drive VICE hands the CPU `(iec_fast_1541 & 0x30) << 2`
         * (iecbus.c:212-217) and store_ciapa put `~(PRA | ~DDRA)` there
         * (c64cia2.c:150-162, ciacore.c:805), so CLK IN and DATA IN are the
         * INVERSE of CLK OUT and DATA OUT: release DATA and it reads high,
         * which is a bus with nobody on it.
         *
         * IT USED TO ANSWER THE RAW REGISTER, and that read DATA IN LOW - a
         * device answering. `LOAD"*",8` then sat at `SEARCHING FOR *` for
         * ever instead of timing out, which is the KERNAL doing exactly what
         * it is told: apps/c64/README.TXT and §11.3 both promise
         * `?DEVICE NOT PRESENT  ERROR` and the machine could not give it.
         * Nothing in the port had ever typed a LOAD (build/port-shots/
         * wave2fix-05-devicenotpresent.png is the first). */
        v = (int)(c64_cia[1][0x00] | (unsigned char)~c64_cia[1][0x02]);
        return ((v & 0x3F) | (((~v) & 0x30) << 2)) & 0xFF;
    case 0x01:
        return (k == 0) ? c64_kbd_pb() : (int)c64_cia[1][0x01];
    case 0x04:
        return (int)(c64_ta[k] & 0xFF);
    case 0x05:
        return (int)((c64_ta[k] >> 8) & 0xFF);
    case 0x06:
        return (int)(c64_tb[k] & 0xFF);
    case 0x07:
        return (int)((c64_tb[k] >> 8) & 0xFF);
    /* THE TOD READ LATCH (ciacore.c:1249-1270). Reading HOURS latches all
     * four registers and reading TENTHS releases them, so a program that
     * reads $0B..$08 cannot see the clock tick under it and read 10:59:59.9
     * as 10:00:00.0. The counter itself keeps running either way. */
    case 0x08:
        if (!c64_todlatch[k])
            return c64_todt[k];
        c64_todlatch[k] = 0;                /* TENTHS releases the latch */
        return c64_todlt[k];
    case 0x09:
        return c64_todlatch[k] ? c64_todls[k] : c64_tods[k];
    case 0x0A:
        return c64_todlatch[k] ? c64_todlm[k] : c64_todm[k];
    case 0x0B:
        if (!c64_todlatch[k]) {             /* HOURS arms it */
            c64_todlt[k] = c64_todt[k];
            c64_todls[k] = c64_tods[k];
            c64_todlm[k] = c64_todm[k];
            c64_todlh[k] = c64_todh[k];
            c64_todlatch[k] = 1;
        }
        return c64_todlh[k];
    case 0x0D:
        v = c64_icd[k];                     /* a READ clears the flags AND the
                                             * asserted line, and it is the
                                             * only thing that clears either
                                             * (ciacore.c:1345's
                                             * my_set_int(false) inside the
                                             * ICR read) */
        c64_icd[k] = 0;
        c64_iasrt[k] = 0;
        c64_irq_update();
        return v;
    default:
        return c64_cia[k][r];
    }
}

static void c64_cia_wr(int k, int r, int v)
{
    c64_cia[k][r] = (unsigned char)v;
    switch (r) {
    case 0x04:
        c64_la[k] = (c64_la[k] & 0xFF00) | (unsigned)(v & 0xFF);
        return;
    case 0x05:
        c64_la[k] = (c64_la[k] & 0x00FF) | (((unsigned)(v & 0xFF)) << 8);
        if (!c64_ta_runs(k))                /* a stopped timer loads at once */
            c64_ta[k] = c64_la[k];
        return;
    case 0x06:
        c64_lb[k] = (c64_lb[k] & 0xFF00) | (unsigned)(v & 0xFF);
        return;
    case 0x07:
        c64_lb[k] = (c64_lb[k] & 0x00FF) | (((unsigned)(v & 0xFF)) << 8);
        if (!c64_tb_runs(k))
            c64_tb[k] = c64_lb[k];
        return;
    /* THE FOUR TOD REGISTERS ARE TWO SETS OF FOUR, AND CRB BIT 7 PICKS
     * (cia.h:90's CIA_CRB_ALARM, ciacore.c:876). With the bit set a write
     * lands in the ALARM registers - which is the ONLY way to set an alarm,
     * and this port had no alarm registers at all, so `Cycles`-style code
     * that arms one wrote the time instead and the clock jumped. With it
     * clear the write is the clock: HOURS stops it and TENTHS restarts it
     * (:848, :886-:893), which is how the four stores are made atomic. */
    case 0x08:
        v = v & 0x0F;
        if ((c64_cia[k][0x0F] & 0x80) != 0) {
            c64_alt[k] = (unsigned char)v;
        } else {
            if (c64_todstop[k]) {
                c64_todp[k] = 0;            /* the tick divider is kept clear
                                             * while the clock is stopped */
                c64_todstop[k] = 0;
            }
            c64_todt[k] = (unsigned char)v;
        }
        c64_tod_alarm(k);
        return;
    case 0x09:
        v = v & 0x7F;
        if ((c64_cia[k][0x0F] & 0x80) != 0)
            c64_als[k] = (unsigned char)v;
        else
            c64_tods[k] = (unsigned char)v;
        c64_tod_alarm(k);
        return;
    case 0x0A:
        v = v & 0x7F;
        if ((c64_cia[k][0x0F] & 0x80) != 0)
            c64_alm[k] = (unsigned char)v;
        else
            c64_todm[k] = (unsigned char)v;
        c64_tod_alarm(k);
        return;
    case 0x0B:
        v = v & 0x9F;                       /* bits 5-6 are forced 0 */
        if ((c64_cia[k][0x0F] & 0x80) != 0) {
            c64_alh[k] = (unsigned char)v;
        } else {
            if ((v & 0x1F) == 0x12)         /* writing 12 flips AM/PM, and
                                             * ONLY when writing the time
                                             * (ciacore.c:862-866) */
                v = v ^ 0x80;
            c64_todstop[k] = 1;             /* ...and HOURS stops the clock */
            c64_todh[k] = (unsigned char)v;
        }
        c64_tod_alarm(k);
        return;
    case 0x0D:
        if ((v & 0x80) != 0)
            c64_icm[k] = (unsigned char)(c64_icm[k] | (v & 0x1F));
        else
            c64_icm[k] = (unsigned char)(c64_icm[k] & ~(v & 0x1F));
        c64_irq_update();
        return;
    case 0x0E:
        if ((v & 0x10) != 0)                /* FORCE LOAD */
            c64_ta[k] = c64_la[k];
        c64_cia[k][r] = (unsigned char)(v & 0xEF);  /* ...AND BIT 4 IS A
                                             * STROBE, NOT A BIT: VICE stores
                                             * `byte & 0xEF` (ciacore.c:1056),
                                             * so a program that reads CRA
                                             * back, ORs a start bit in and
                                             * writes it does not force-load
                                             * the timer a second time */
        return;
    case 0x0F:
        if ((v & 0x10) != 0)
            c64_tb[k] = c64_lb[k];
        c64_cia[k][r] = (unsigned char)(v & 0xEF);  /* ...and CRB's (:1097) */
        return;
    default:
        return;
    }
}

static int c64_io_rd(unsigned a)
{
    unsigned p;

    if (a < 2)                              /* $0000/$0001 are the port (3.2) */
        return c64_pp_read((int)a);

    p = a & 0x0F00;
    if (p <= 0x0300) {                      /* $D000-$D3FF: the VIC, mirrored */
        p = a & 0x3F;
        if (p == 0x11)
            return (int)((c64_vic[0x11] & 0x7F)
                         | ((c64_raster_now() & 0x100) ? 0x80 : 0));
        if (p == 0x12)
            return c64_raster_now() & 0xFF;
        if (p == 0x19)
            return (int)(c64_vic[0x19] | 0x70);
        if (p == 0x1A)
            return (int)(c64_vic[0x1A] | 0xF0);
        if (p == 0x1E || p == 0x1F)
            return 0;                       /* the collision registers answer
                                             * 0 and 5.3 says why */
        return c64_vic[p];
    }
    if (p <= 0x0700)                        /* $D400-$D7FF: the SID */
        return c64_sid[a & 0x1F];
    if (p <= 0x0B00)                        /* $D800-$DBFF: colour RAM */
        return c64_col[a & 0x03FF];
    if (p <= 0x0C00)
        return c64_cia_rd(0, (int)(a & 0x0F));
    if (p <= 0x0D00)
        return c64_cia_rd(1, (int)(a & 0x0F));
    return 0xFF;                            /* $DE00-$DFFF: the I/O areas, no
                                             * cartridge - the open bus */
}

/* ==========================================================================
 * A REGISTER WRITE THAT CHANGES NOTHING COSTS NOTHING
 *
 * Five of these registers make the flush recompose the whole screen, and the
 * first draft dirtied on the STORE rather than on the CHANGE. Two ordinary
 * things reach that. A raster interrupt's `LDA #$1B / STA $D011` writes the
 * same byte fifty times a second; a smooth-scroll program writes $D016 once a
 * frame. Either one repainted 25 rows - ~234 ms, four host ticks - forever,
 * on a machine that was showing the same picture. So every one of them is
 * guarded by VALUE, and $DD00 by the two BITS that are the VIC bank: the
 * KERNAL bit-bangs the serial bus through that register's other six, so a
 * LOAD from drive 8 was hundreds of full-screen repaints a second.
 *
 * The guard is a compare and a branch against a repaint measured in host
 * ticks, which is why it is here and not "an optimisation".
 * ========================================================================*/
static void c64_io_wr(unsigned a, int v)
{
    unsigned p;
    unsigned r;
    unsigned char old;

    if (a < 2) {                            /* the processor port (3.2) */
        c64_pp_write((int)a, v);
        return;
    }

    p = a & 0x0F00;
    if (p <= 0x0300) {
        r = a & 0x3F;
        if (r == 0x19) {                    /* the latch is ACKNOWLEDGED, not
                                             * written: a 1 bit clears it */
            c64_vic[0x19] = (unsigned char)(c64_vic[0x19] & ~(v & 0x0F));
            if ((c64_vic[0x19] & c64_vic[0x1A] & 0x0F) == 0)
                c64_vic[0x19] &= 0x7F;
            c64_irq_update();
            return;
        }
        old = c64_vic[r];
        c64_vic[r] = (unsigned char)v;
        if (r == 0x1A) {
            c64_irq_update();
            return;
        }
        if (old == (unsigned char)v)
            return;                         /* nothing moved: nothing to draw */
        /* a FRAME REGISTER changes every row by itself (9.2), and $D021
         * changes what every colour resolves to in 1bpp (9.6) */
        if (r == 0x11 || r == 0x16 || r == 0x18) {
            c64_frame_regs();               /* the matrix may have moved: the
                                             * core's write window watches the
                                             * NEW one from here on (9.2) */
            c64_dirty_all();
        } else if (r == 0x21) {
            c64_lum_update();
            c64_dirty_all();
        } else if (r == 0x20) {
            /* $D020 IS THE BORDER'S OWN LEVEL, AND c64_lum_update DECIDES.
             * It raises c64_border_dirty only when the mono level FLIPS -
             * colour 14 to 15 over background 6 is still `lighter than the
             * paper` and changes no pixel - and this line used to raise the
             * flag unconditionally one statement later, which made that guard
             * dead code and the border four os88_gfx_fill calls (3.0 ms,
             * §9.7) on the next flush after EVERY $D020 write. Rasterbars,
             * loader flashes and $D020 cycling are many writes a frame. */
            c64_lum_update();
            if (c64_border_dirty)
                c64_dirty_any = 1;          /* ...and only a border that will
                                             * LOOK different is a reason to
                                             * flush at all */
        }
        return;
    }
    if (p <= 0x0700) {
        c64_sid[a & 0x1F] = (unsigned char)v;
        c64_sid_dirty = 1;                  /* voice 1 -> the speaker, once a
                                             * slice and only on a change */
        c64_sid_tries = 0;                  /* ...and THIS is what re-arms a
                                             * bounded retry that gave up: a
                                             * refused grant is re-asked from
                                             * the next SID register write
                                             * (11.4), never from a loop */
        return;
    }
    if (p <= 0x0B00) {
        r = a & 0x03FF;
        if (c64_col[r] != (unsigned char)(v & 0x0F)) {
            c64_col[r] = (unsigned char)(v & 0x0F);
            c64_row_dirty((int)(r / C64_COLS));
            if (r < c64_clo)
                c64_clo = r;
            if (r > c64_chi)
                c64_chi = r;
        }
        return;
    }
    if (p <= 0x0C00) {
        c64_cia_wr(0, (int)(a & 0x0F), v);
        return;
    }
    if (p <= 0x0D00) {
        r = a & 0x0F;
        old = c64_cia[1][r];
        c64_cia_wr(1, (int)r, v);
        /* ONLY BITS 0-1 ARE THE VIC BANK. Bits 3-7 are the serial bus and the
         * user port, and the KERNAL bit-bangs CLK and DATA through them for
         * every byte it loads from drive 8 - so dirtying on any write to
         * $DD00 was a full-screen repaint per serial edge. */
        if (r == 0x00 && (((unsigned char)v ^ old) & 3) != 0) {
            c64_frame_regs();               /* the matrix moved with the bank */
            c64_dirty_all();
        }
        return;
    }
}

/* ==========================================================================
 * SID - VOICE 1'S FREQUENCY AND GATE, AND NOTHING ELSE (C64-SPEC §11.4)
 *
 * One square wave on the PC speaker, written once a slice and only when a SID
 * register moved. Voices 2 and 3, the waveforms, ADSR and the filters are
 * greyed with the fact that this machine's whole audio path is one channel of
 * square wave (SPEC.md 34).
 *
 * The SID's oscillator is `freq * phi2 / 2^24`, which is freq * 985248 /
 * 16,777,216 = freq * 0.058725. Written as (freq >> 4) * 15 >> 4 it is
 * 0.05859 - a fifth of a per cent flat, and 16-bit throughout.
 * ========================================================================*/
/* AND IT ANSWERS THE GRANT. os88_snd_tone answers -1 REFUSED - the speaker is
 * stamped to another instance and SPEC.md 34.3 lets it hold it - and every
 * one of the three returns below used to be discarded. The latch that says a
 * SID register moved is raised in exactly ONE place (a write to
 * $D400-$D41C), so one refusal at the wrong moment - a toast tone, another
 * app's grant - silenced the emulated SID until the guest next poked a SID
 * register, which for the ordinary pattern (set the frequency, gate on, leave
 * it) is NEVER. A permanent silence with nothing said about it is the shape
 * SPEC.md 47 forbids; the caller retries a bounded number of wakes and then
 * says so once. */
static int c64_sid_voice1(void)
{
    unsigned f;
    int hz;

    /* THE TWO SILENCE PATHS ANSWER 0 WHATEVER THE GRANT SAYS, and that is not
     * sloppiness: the question the caller asks is "did the emulated SID's
     * audible state reach the speaker?", and a machine with the gate closed
     * has nothing to play. A refusal here can only mean this instance does
     * not hold the grant, so nothing of ours is sounding either - retrying
     * it, and saying the speaker is busy over it, would both be wrong. */
    if ((c64_sid[0x04] & 0x01) == 0) {      /* the gate is closed */
        os88_snd_tone(0, 0, 0x40);
        return 0;
    }
    /* THE SID'S OWN ARITHMETIC, AND IT IS ONE ROUNDING AND NOT TWO. A 6581 at
     * the PAL dot clock sounds `Fn * 985248 / 2^24` Hz - 0.0587257 a step -
     * and this used to be `(raw >> 4) * 15 / 16`, which is the same factor to
     * a quarter of a percent but rounds TWICE, once into a 12-bit number and
     * again on the way out. Near the bottom of the range that is a whole hertz
     * where a hertz is the unit: middle-of-nowhere F = 341 answered 19 and the
     * true figure is 20, which is the difference between the 20 Hz floor below
     * REFUSING the note and playing it.
     *
     * c64_muldiv keeps the product in 32 bits (c64mem.inc), which is what a
     * 16-bit C cannot express: 3848 / 65535 is 0.0587166, within 0.015 % of
     * the real constant across the whole range, and F = $FFFF answers 3848 Hz
     * where the ratio says 3848.6. hosttest/c64uitest.c asserts both ends and
     * the floor.
     *
     * AND THE DIVISOR IS SPELLED 0xFFFF: SmallerC's `int` is 16 bits and
     * SIGNED, so a decimal 65535 in the source is "Constant too big for
     * 16-bit signed type" and stops the build (docs/C-TOOLCHAIN.md's rule 4's
     * near neighbour). The hex form is the unsigned bit pattern and reaches
     * c64_muldiv, whose arithmetic is unsigned, as 65,535. */
#define C64_SIDDIV 0xFFFF
    f = ((unsigned)c64_sid[0x00]) | (((unsigned)c64_sid[0x01]) << 8);
    hz = (int)c64_muldiv(f, 3848, C64_SIDDIV);
    if (hz < 20 || hz > 12000) {
        os88_snd_tone(0, 0, 0x40);
        return 0;
    }
    return os88_snd_tone(hz, 0, 0x40);
}

/* ==========================================================================
 * RESET - THE MACHINE, not just the registers (C64-SPEC §11.1)
 *
 * The 6510 comes out of reset with I set and PC from $FFFC, which is under
 * the KERNAL: c64_rd is RAM by construction (c64mem.inc), so the vector is
 * read through the core's own banked reader.
 * ========================================================================*/
static void c64_reset_cpu(void)
{
    c64_m.a = 0;
    c64_m.x = 0;
    c64_m.y = 0;
    c64_m.s = 0xFD;
    c64_m.p = 0x24;                         /* I set, bit 5 always reads 1 */
    c64_m.cnt = 0;
    c64_m.reason = 0;
    c64_map_publish();
    c64_m.pc = (unsigned)c64_bread(0xFFFC)
             | (((unsigned)c64_bread(0xFFFD)) << 8);
    c64_scr_wr(C64_SCR_IRQ, 0);
    c64_nmi_state = 0;
}
