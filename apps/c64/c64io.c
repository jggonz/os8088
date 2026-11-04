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
/* THE DISPLAY LATCH (C64-SPEC 9.9): what the screen is drawn from - the
 * frame registers as they stood at the top of the display window, taken once
 * a frame (c64_latch). The live registers above are what the 6510 reads. */
#define C64_LATCH_LINE 51                   /* the 25-row window's first line */
/* ...AND THE ROWS (9.10): the VIC's registers $D000-$D02E and $DD00 (at
 * C64_RV_DD) as they stood at each character row's first line in the frame
 * last latched, out of a log of every write to them stamped with its raster
 * line - so a raster split (a bitmap above a text scroller, a rasterbar in
 * $D021, a sprite multiplexer moving its eight down the screen) is drawn row
 * by row, and a screen nobody splits costs what it cost. c64_rv0 is the state
 * NOW: the last latch's, with every logged write since applied. */
#define C64_RVN   0x30
#define C64_RV_DD 0x2F
static unsigned char c64_rv[C64_ROWS][C64_RVN];
static unsigned char c64_rv0[C64_RVN];
static int c64_rsplit;                      /* the latched frame's rows differ */
#define C64_DLOG 128
static unsigned c64_dl_d[C64_DLOG];         /* the line, relative to the latch */
static unsigned char c64_dl_r[C64_DLOG], c64_dl_v[C64_DLOG];
static int c64_dln, c64_dl_over;
/* the cycles c64_slice asked of the run in progress, and c64fast.inc's
 * correction for its short accurate runs (whose countdown starts at their
 * own budget, not the run's): the run's elapsed cycles are
 * c64_run_n - DEAD - c64f_adj */
unsigned c64_run_n;
unsigned c64f_adj;
static int c64_dl_tail;                     /* the last latch left writes
                                             * below the display for the next */
static unsigned char c64_rsame[C64_ROWS];   /* row r was row r-1's set */
static void c64_latch(int force);
static unsigned char c64_sid[32];           /* $D400-$D41C */
static unsigned char c64_sid_trig;         /* a gate opened since the sound
                                             * last took the SID (11.4.2) */
static int c64_sid_last;                    /* ...the voice it opened on */

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
static unsigned c64_o3t;                    /* ...when voice 3 was last brought
                                             * up to date (c64_osc3_now) */
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
/* THE PORT'S READ-BACK, PUBLISHED: $00 in the low byte and $01's read-back
 * (c64_pp_read's formula) in the high, for c64fast.inc's .portin, which puts
 * it in RAM $0000/$0001 at the top of every fast-core run. It was two calls
 * of c64_io_rd a run, ~290 runs a second, for two bytes that change only
 * when the port is written - which is the only time this is recomputed. */
unsigned c64_pp_rb;

static void c64_pp_rbset(void)
{
    c64_pp_rb = (unsigned)c64_pp_ddr
              | (((unsigned)((c64_pp_data & c64_pp_ddr) | (~c64_pp_ddr & 0x17)) & 0xFF) << 8);
}

static void c64_map_publish(void)
{
    c64_pp_rbset();
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
static void ovl_reset_regs(void)
{
    int i, k;

    for (i = 0; i < 64; i++)
        c64_vic[i] = 0;
    for (i = 0; i < 32; i++)
        c64_sid[i] = 0;
    c64_osc3(0, 0, 0x08, 0);                /* voice 3 through TEST: the
                                             * accumulator 0 and the noise
                                             * register ALL ONES - a zero LFSR
                                             * shifts in zeros for ever, and
                                             * bss is zero (11.4.1) */
    c64_o3t = c64_cyc_lo;
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
    c64_pp_rbset();
    c64_bank = 7;
    c64_frame_cyc = 0;
    c64_tod_acc = 0;
    c64_nmi_state = 0;
    for (i = 0; i < 1024; i++)
        c64_col[i] = 0;
    c64_latch(1);                           /* (its callers rebuild the screen
                                             * state from it themselves) */
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
    nmi = c64_iasrt[1];                     /* 0 or 1 - its only writers */

    was = c64_scr_rd(C64_SCR_IRQ);
    if (nmi)
        if (!c64_nmi_state)
            was |= 0x02;                    /* the EDGE, latched for the core */
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
    if (c64_in_run)
        if ((was & 0x03) != 0)
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
/* CRA: bit 0 START, bit 5 INMODE (1 = count CNT, which nothing here
 * drives, so the timer does not count).
 *
 * CRB bits 5-6 INMODE: 00 = phi2. 10 = count timer A underflows, which is
 * not a phi2 alarm - it happens when timer A underflows, and that IS an
 * alarm already.
 *
 * MACROS AND NOT FUNCTIONS, and only ever the whole condition of an `if`.
 * They were two functions answering `(...) ? 1 : 0`, called from the alarm
 * scheduler and the timer step on every slice: SmallerC compiled each into a
 * frame, a setcc, a cbw, a second test and a 0/1 select, and the caller then
 * tested the answer again. As the condition of an `if` the compare is one
 * `cmp` and one `jcc` - fewer bytes AND fewer instructions on every path,
 * so the hot path only got shorter (apps size pass 1). */
#define C64_TA_RUNS(k) ((c64_cia[k][0x0E] & 0x21) == 0x01)
#define C64_TB_RUNS(k) ((c64_cia[k][0x0F] & 0x61) == 0x01)
#define C64_TA_STOPPED(k) ((c64_cia[k][0x0E] & 0x21) != 0x01)
#define C64_TB_STOPPED(k) ((c64_cia[k][0x0F] & 0x61) != 0x01)

static int c64_rcmp_line(void)
{
    return (int)(((unsigned)c64_vic[0x12]) | (((unsigned)(c64_vic[0x11] & 0x80)) << 1));
}

/* c64_line_now - the raster line THIS INSTANT is on, 0..311. Inside a run
 * that is the cycle clock plus what the run has spent so far: the run's
 * budget less the countdown, less what c64_cut() moved into CARRY, less
 * c64f_adj while the fast core has handed a short run to the accurate one.
 * $D012 read the line the RUN STARTED on until 9.10 - so a program polling
 * it for a line saw the line only at the next run boundary, and its raster
 * split landed wherever that was: the Christmas demo's text-mode write
 * moved between line 242 and line 53 from frame to frame */
/* c64_run_el - THE CYCLES THE RUN IN PROGRESS HAS SPENT SO FAR: its budget
 * less the countdown, less what c64_cut() moved into CARRY, less c64f_adj
 * while the fast core has handed a short run to the accurate one. 0 outside
 * a run. Every register that is a CLOCK reads through it - the raster line,
 * the CIA timers, SID voice 3's oscillator - because the devices are
 * advanced only between runs (4.4) and a program polling one mid-run must
 * see it move (9.10, 6.3, 11.4) */
static unsigned c64_run_el(void)
{
    unsigned el;
    int dead;

    if (!c64_in_run)
        return 0;
    dead = (int)c64_scr_left();             /* DEAD + CARRY, one call */
    el = c64_run_n - (unsigned)dead - c64f_adj;
    return (el < 0x8000u) ? el : 0;
}

static int c64_line_now(void)
{
    unsigned cyc;

    cyc = (unsigned)c64_frame_cyc + c64_run_el();
    while (cyc >= C64_PAL_FRAME)
        cyc -= C64_PAL_FRAME;
    return (int)(cyc / C64_PAL_LINE);
}

static int c64_raster_now(void)
{
    return c64_line_now();
}

static int c64_alarm_next(void)
{
    int d, t, k, line;

    /* THE DISPLAY LATCH IS ALWAYS ARMED (9.9), once a frame, and it is what
     * bounds a run to one frame (c64_advance relies on it). The frame END is
     * not an alarm: nothing a program can see happens there - $D011/$D012
     * are the beam's line however a run ends, and c64_advance wraps the
     * frame wherever a run crosses it. It was 50 runs a second for nothing,
     * on the machine whose every run is the 286's dearest fixed cost. */
    d = (int)C64_X63(C64_LATCH_LINE) - c64_frame_cyc;
    if (d <= 0)
        d += C64_PAL_FRAME;
    /* ...and the TOD at the tick that CHANGES it: the 50 Hz mains tick only
     * advances a divider, and the registers move every fifth (the tenths) -
     * so that is the alarm, not each tick (40 more runs a second) */
    /* (only the tick that ends a tenth can be the nearest: any other is
     * more than one period away, 19,705 cycles, and the latch is never
     * further than 19,656 - and that product would wrap a 16-bit int) */
    if ((c64_todp[0] == 4) | (c64_todp[1] == 4)) {
        t = C64_TOD_PERIOD - c64_tod_acc;
        if (t < d)
            d = t;
    }
    /* A COUNTER AT OR ABOVE $7FFF CANNOT BE THE NEAREST ALARM, so it is
     * SKIPPED rather than cast. `(int)(0xFFFF + 1)` is 0 and any counter in
     * the top half of the range casts NEGATIVE - either way `t < d` won, `d`
     * fell to the floor below, and the slice driver then ran the core ONE
     * CYCLE at a time, each with a full c64_alarm_next + c64_run shell +
     * c64_advance over both CIAs, the VIC and the TOD. The nearest real alarm
     * is never further than the display latch (19,656), so nothing is lost. */
    for (k = 0; k < 2; k++) {
        if (C64_TA_RUNS(k))
            if (c64_ta[k] < (unsigned)0x7FFF) {
                t = (int)c64_ta[k] + 1;
                if (t < d)
                    d = t;
            }
        if (C64_TB_RUNS(k))
            if (c64_tb[k] < (unsigned)0x7FFF) {
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
    /* NESTED, not one `&&` chain: SmallerC turns every operand of a chain
     * into a 0/1 value and tests it again, where a nested `if` is a compare
     * and a branch (apps size pass 1) */
    if (c64_todt[k] == c64_alt[k])
        if (c64_tods[k] == c64_als[k])
            if (c64_todm[k] == c64_alm[k])
                if (c64_todh[k] == c64_alh[k]) {
                    c64_icd[k] |= 0x04;
                    c64_irq_update();
                }
}

/* ovl_tod_tick - one tenth of a second on the CIA's own mains accumulator.
 * BCD, as the registers are, and the HOURS are 12-hour BCD with an AM/PM bit
 * that toggles at 12 - ciacore.c:1961-1977, transcribed:
 *
 *   09 -> 10 and 12 -> 01 are the two carries out of the units digit, and
 *   BOTH are `hl = hh; hh ^= 1`; every other hour is `hl + 1`, and reaching
 *   12 that way is what flips PM.
 *
 * The old code did `if (v > 0x12) v = 1` on a plain BCD increment, so it ran
 * 09, 0A..0F, 10 - six hours that do not exist - and never touched AM/PM. */
static void ovl_tod_tick(int k)
{
    int v, h, pm;

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
    /* The hours are BCD in bits 0-4 - `hh` is bit 4, `hl` bits 0-3 - and
     * bits 5-6 are forced 0 by the write (0x9F), so the five bits are taken
     * whole and compared as one byte rather than as two digits under a
     * four-term `&&`/`||` (apps size pass 1). */
    h = c64_todh[k] & 0x1F;
    pm = c64_todh[k] & 0x80;
    if (h == 0x12)
        h = 0x01;                           /* 12 -> 01 */
    else if (h == 0x09)
        h = 0x10;                           /* 09 -> 10 */
    else {
        h = (h & 0x10) | ((h + 1) & 0x0F);
        if (h == 0x12)
            pm = pm ^ 0x80;                 /* 11 -> 12 flips AM/PM */
    }
    c64_todh[k] = (unsigned char)(h | pm);
    c64_tod_alarm(k);
}

/* c64_timer_step - one CIA's timers, advanced by `n` phi2 cycles. Underflows
 * reload from the latch, set the ICR flag, and cascade into timer B where CRB
 * says timer B counts timer A's underflows. */
static void c64_timer_step(int k, int n)
{
    unsigned c;
    int under, i, rem;

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
    if (C64_TA_RUNS(k)) {
        rem = n;
        c = c64_ta[k];
        /* THE COMPARE IS UNSIGNED, AND THAT IS NOT A STYLE CHOICE. `c` is a
         * 16-bit CIA counter and the reset default latch is $FFFF - the state
         * `LDA #$FF / STA $DC04 / STA $DC05 / STA $DC0E` leaves, which is the
         * standard free-running-timer idiom AND what ovl_reset_regs leaves
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
    if ((c64_cia[k][0x0F] & 0x61) == 0x41) {  /* cascaded onto timer A */
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
    if (C64_TB_RUNS(k)) {
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
 * The `n` passed here is never more than one frame, because the display latch is
 * always an armed alarm and the slice never runs past its deadline by more
 * than one instruction. */
/* c64_latch - THE DISPLAY LATCH (C64-SPEC 9.9, 9.10). The composer used to
 * take the mode, matrix and colours from the live registers at every write -
 * so a program that splits the screen with raster interrupts (a bitmap above
 * a text scroller, rasterbars in $D020) changed them twice a frame, each
 * change a full repaint, and the flush showed whichever half it happened to
 * sample. Taken once a frame at the top of the display window, out of the
 * frame's log of display-register writes, each ROW gets the set that stood
 * on it - the same every frame for any such program, so only a real change
 * costs a repaint. FORCE copies without acting (a reset, whose callers
 * rebuild the screen state themselves). */
static void c64_latch(int force)
{
    static unsigned char cur[C64_RVN];
    int r, i, k, frame, lum, bord, lim, ch, ap, same, unch, dif0;

    if (force | c64_dl_over) {
        /* a reset (its callers rebuild the screen from here themselves), or
         * a frame that wrote past the log: every row is the live registers */
        for (k = 0; k < C64_RV_DD; k++)
            cur[k] = c64_vic[k];
        cur[C64_RV_DD] = c64_cia[1][0x00];
        c64_dln = 0;
        c64_dl_over = 0;
    } else {
        if ((c64_dln | c64_rsplit | c64_dl_tail) == 0)
            return;                         /* nothing written, nothing split,
                                             * nothing left over: every row is
                                             * what it was */
        os88_memcpy(cur, c64_rv0, C64_RVN);
    }
    frame = 0;
    lum = 0;
    bord = 0;
    i = 0;
    c64_rsplit = 0;
    unch = 1;
    dif0 = 0;
    /* ROW BY ROW, AND A ROW NOTHING WAS WRITTEN AT IS USUALLY FREE. Row r is
     * unchanged when nothing was applied at it, the row above it is
     * unchanged, and it was the same as the row above it LAST frame
     * (c64_rsame) - so a split that is the same every frame compares only
     * the rows its writes land on, not 25 x 48 bytes. The Christmas demo's
     * latch was 9% of a 286 before this */
    for (r = 0; r < C64_ROWS; r++) {
        lim = (r << 3) + 3;                 /* a write in a row's first HALF
                                             * is the row's: it is on most of
                                             * the row's lines (9.10) */
        ap = 0;
        while (i < c64_dln && (int)c64_dl_d[i] <= lim) {
            cur[c64_dl_r[i]] = c64_dl_v[i];
            i++;
            ap = 1;
        }
        same = c64_rsame[r];
        c64_rsame[r] = (unsigned char)((r != 0) & (ap ^ 1));
        if (r != 0)
            if ((ap | (unch ^ 1) | (same ^ 1)) == 0) {
                c64_rsplit |= dif0;         /* (cur, and so dif0, as above) */
                continue;                   /* unch stays 1 */
            }
        unch = 1;
        if (c64_rvdiff(cur, c64_rv[r])) {
            unch = 0;
            ch = 0;
            if (cur[0x11] != c64_rv[r][0x11] || cur[0x16] != c64_rv[r][0x16]
                    || cur[0x18] != c64_rv[r][0x18]
                    || ((cur[C64_RV_DD] ^ c64_rv[r][C64_RV_DD]) & 3))
                ch = 1;
            for (k = 0x21; k <= 0x24; k++)
                if (cur[k] != c64_rv[r][k])
                    ch = 1;
            if (ch) {
                frame = 1;
                if (force == 0)
                    c64_row_whole(r);
            }
            if (r == 0) {
                lum = cur[0x21] != c64_rv[0][0x21];
                bord = cur[0x20] != c64_rv[0][0x20];
            }
            if (cur[0x15] | c64_rv[r][0x15])
                c64_dirty_any = 1;          /* a sprite register: nothing else
                                             * raises the wake's flush gate,
                                             * and c64_spr_dirty is in it */
            os88_memcpy(c64_rv[r], cur, C64_RVN);
            c64_rchg[r] = 1;
        }
        if (r != 0)
            if (ap)
                if (c64_rsplit == 0)        /* (once split, it stays split) */
                    dif0 = c64_rvdiff(cur, c64_rv[0]);
        c64_rsplit |= dif0;
    }
    c64_dl_tail = 0;
    for (; i < c64_dln; i++) {              /* the rest of the frame, and what
                                             * the top of the next was given:
                                             * the NEXT latch's rows, even if
                                             * nothing is written before it */
        cur[c64_dl_r[i]] = c64_dl_v[i];
        c64_dl_tail = 1;
    }
    os88_memcpy(c64_rv0, cur, C64_RVN);
    c64_dln = 0;
    if (force) {
        c64_fr_stale = 1;                   /* (its callers recompute them) */
        return;
    }
    if (frame)
        c64_frame_regs();                   /* the matrix may have moved: the
                                             * core's write window watches the
                                             * NEW one from here on (9.2) */
    if (lum) {
        c64_lum_update();                   /* $D021 changes what every colour
                                             * resolves to in 1bpp (9.6) */
        c64_dirty_all();
    } else if (bord) {
        /* $D020 IS THE BORDER'S OWN LEVEL, AND c64_lum_update DECIDES: it
         * raises c64_border_dirty only when the mono level FLIPS, so only a
         * border that will LOOK different is a reason to flush at all */
        c64_lum_update();
        if (c64_border_dirty)
            c64_dirty_any = 1;
    }
}

/* c64_line_rel - the raster line this instant is on, relative to the
 * display latch's line (0..311): where a logged write lands (9.10) */
static unsigned c64_line_rel(void)
{
    unsigned cyc;

    cyc = (unsigned)c64_line_now();
    return (cyc >= C64_LATCH_LINE) ? cyc - C64_LATCH_LINE
                                   : cyc + C64_PAL_LINES - C64_LATCH_LINE;
}

/* c64_dlog - one display-register write into the frame's log (9.10) */
static void c64_dlog(int reg, int v)
{
    if (c64_dln >= C64_DLOG) {
        c64_dl_over = 1;                    /* the frame is drawn from the live
                                             * registers (c64_latch) */
        return;
    }
    c64_dl_d[c64_dln] = c64_line_rel();
    c64_dl_r[c64_dln] = (unsigned char)reg;
    c64_dl_v[c64_dln] = (unsigned char)v;
    c64_dln++;
}

static void c64_advance(int n)
{
    int prev, now, tline, k, latch;

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
    k = (int)C64_X63(C64_LATCH_LINE);       /* ...and the display latch's line */
    if (k <= prev)
        k += C64_PAL_FRAME;
    latch = now >= k;
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
                ovl_tod_tick(k);
            }
        }
    }

    c64_irq_update();
    if (latch)
        c64_latch(0);
}

/* ==========================================================================
 * THE I/O DISPATCH
 *
 * Every $D000-$DFFF access in an I/O bank is a direct cdecl call into these
 * two from inside the core's handler (3.4), and so is every access to
 * $0000/$0001, which are the processor port and not RAM (3.2).
 * ========================================================================*/
/* c64_tlive - TIMER A (b = 0) OR B OF CIA K, AS IT READS THIS INSTANT: the
 * counter c64_advance last left, less what the run in progress has spent
 * when it counts the clock (6.3). It read the count at the run's START, so a
 * program reading $DC04 mid-run - `LDA $D012 / EOR $DC04 / SBC $DC05`, the
 * Bright Pixel card's random number - saw the same value every frame and
 * every snowflake fell in step. An underflow inside the run reloads from the
 * latch: a counter at $7FFF or above is not an alarm (c64_alarm_next), so a
 * run can cross one */
static unsigned c64_tlive(int k, int b)
{
    unsigned c, rem, la;

    if (b == 0) {
        c = c64_ta[k];
        if (C64_TA_STOPPED(k))
            return c;
        la = c64_la[k];
    } else {
        c = c64_tb[k];
        if (C64_TB_STOPPED(k))
            return c;
        la = c64_lb[k];
    }
    rem = c64_run_el();
    while (rem > c) {
        rem -= c + 1;
        c = la;
    }
    return c - rem;
}

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
        return c64_tlive(k, 0) & 0xFF;      /* (live: c64_tlive) */
    case 0x05:
        return c64_tlive(k, 0) >> 8;
    case 0x06:
        return c64_tlive(k, 1) & 0xFF;
    case 0x07:
        return c64_tlive(k, 1) >> 8;
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
        C64_LOB(c64_la[k]) = (unsigned char)v;  /* the latch's low byte */
        return;
    case 0x05:
        C64_HIB(c64_la[k]) = (unsigned char)v;  /* ...and its high byte */
        if (C64_TA_STOPPED(k))              /* a stopped timer loads at once */
            c64_ta[k] = c64_la[k];
        return;
    case 0x06:
        C64_LOB(c64_lb[k]) = (unsigned char)v;
        return;
    case 0x07:
        C64_HIB(c64_lb[k]) = (unsigned char)v;
        if (C64_TB_STOPPED(k))
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

/* THE RASTER WAIT, TAKEN IN WHOLE PASSES (C64-SPEC 4.7.1).
 *
 * $D011, $D012 and $D019 read c64_frame_cyc, which moves only between runs
 * (c64_advance): inside one run they are constants, so a program polling
 * one of them - `LDA $D012 / CMP #n / BNE` and its kin, the commonest loop a
 * game or demo has - either leaves at the first read or spins until the
 * budget ends. When the fast core makes the read (c64f_iord passes its PC in
 * c64_fpc, 0 from any other caller) and the bytes after it in RAM are such a
 * loop that THIS value keeps spinning, every whole pass that still leaves
 * the countdown above zero is charged at once through DEAD, c64_cut's own
 * door: the core then reads again, at the same point of the loop with the
 * clock where those passes would have put it, and ends the run on the same
 * instruction as before. On a 286 one pass of the call-out costs several
 * times the 11 us a real C64 spends on it. */
unsigned c64_fpc;
static unsigned c64_fpc_pc;                 /* (c64_poll_skip's PC, for the
                                             * limit's CMP operand) */

/* c64_poll_lim - how many cycles from now register A of the VIC keeps the
 * value V a poll loop read (-1: until the run ends). $D012 waiting for
 * EQUALITY (`CMP #n` with the loop going on while it differs) is skipped to
 * the line where its low byte is n; any other $D012 test to the end of the
 * line it is on; $D011 to the next turn of bit 8 (line 256, or the frame's
 * end) */
static int c64_poll_lim(unsigned a, unsigned op, unsigned b0, int cont, int v)
{
    unsigned cyc, line, t, target;
    int n;

    a &= 0x3F;
    if ((a != 0x11) & (a != 0x12))
        return -1;
    cyc = (unsigned)c64_frame_cyc + c64_run_el();
    while (cyc >= C64_PAL_FRAME)
        cyc -= C64_PAL_FRAME;
    line = cyc / C64_PAL_LINE;
    if (a == 0x11)
        target = (line < 256) ? 256 : C64_PAL_LINES;
    else {
        target = line + 1;
        if (op == 0xAD)
            if (b0 == 0xC9)
                if (cont) {                 /* (a CMP loop going on is either
                                             * "not equal yet" or a BCC/BCS
                                             * test; only the first is exact) */
                    n = c64_rd(c64_fpc_pc + 1);
                    if (v != n) {
                        /* the lines whose low byte is n are n and n + 256
                         * (when < 312): the first after this one, or the
                         * first of the next frame */
                        t = (unsigned)n;
                        if (t <= line) {
                            t += 256;
                            if (t <= line || t >= C64_PAL_LINES)
                                t = (unsigned)n + C64_PAL_LINES;
                        }
                        target = t;
                    }
                }
    }
    return (int)(C64_X63(target) - cyc);
}

static void c64_poll_skip(unsigned a, int v)
{
    unsigned pc = c64_fpc, back, op, b0, br, off;
    int per, cont, z, c, dead, k, lim;

    c64_fpc = 0;
    c64_fpc_pc = pc;
    if (pc < 3 || pc > 0x9FF0)              /* RAM, and the loop with it */
        return;
    back = pc - 3;                          /* the read instruction's PC */
    if ((unsigned)c64_rd16(pc - 2) != a)
        return;
    op = (unsigned)c64_rd(back);
    b0 = (unsigned)c64_rd(pc);
    cont = 0;
    per = 0;
    if (op == 0xAD && (b0 == 0xC9 || b0 == 0x29)) {
        /* LDA / CMP #n or AND #m / a branch back to the LDA */
        k = c64_rd(pc + 1);
        br = (unsigned)c64_rd(pc + 2);
        off = (unsigned)c64_rd(pc + 3);
        if (b0 == 0xC9) {
            z = (v == k);
            c = (v >= k);
        } else {
            z = ((v & k) == 0);
            c = 2;                          /* (AND leaves C: no BCC/BCS) */
        }
        if (off == 0xF9) {
            per = 9 + (((pc + 4) ^ back) > 0xFF);
            if (br == 0xD0)
                cont = !z;
            else if (br == 0xF0)
                cont = z;
            else if (br == 0x90 && c != 2)
                cont = !c;
            else if (br == 0xB0 && c != 2)
                cont = c;
        } else if (br == 0xF0 && off == 0x03 && c64_rd(pc + 4) == 0x4C
                   && (unsigned)c64_rd16(pc + 5) == back) {
            per = 11;                       /* ...BEQ out / JMP back */
            cont = !z;
        }
    } else if ((op == 0xAD || op == 0x2C) && c64_rd(pc + 1) == 0xFB) {
        /* LDA or BIT / a branch straight back */
        per = 7 + (((pc + 2) ^ back) > 0xFF);
        if (b0 == 0x10)
            cont = !(v & 0x80);
        else if (b0 == 0x30)
            cont = (v & 0x80) != 0;
        else if (op == 0xAD && b0 == 0xD0)
            cont = (v != 0);
        else if (op == 0xAD && b0 == 0xF0)
            cont = (v == 0);
        else if (op == 0x2C && b0 == 0x50)
            cont = !(v & 0x40);
        else if (op == 0x2C && b0 == 0x70)
            cont = (v & 0x40) != 0;
    }
    if (!cont)
        return;
    dead = c64_scr_rd(C64_SCR_DEAD) | (c64_scr_rd(C64_SCR_DEAD + 1) << 8);
    if (dead <= per)
        return;
    k = (dead - 1) / per;
    /* ...BUT NEVER PAST THE MOMENT THE VALUE CHANGES. $D011 and $D012 read
     * the live line since 9.10 (c64_line_now), and a skip to the run's end
     * jumped clean over the line a loop was waiting for: the Bright Pixel
     * card's `LDA $D012 / CMP #50 / BEQ` caught line 50 only when a run
     * boundary happened to fall on it, and its snow fell two pixels a
     * second. $D019 still moves only between runs. */
    lim = c64_poll_lim(a, op, b0, cont, v);
    if (lim >= 0)
        if (k > lim / per)
            k = lim / per;
    if (k <= 0)
        return;
    dead -= k * per;
    c64_scr_wr(C64_SCR_DEAD, dead & 0xFF);
    c64_scr_wr(C64_SCR_DEAD + 1, (dead >> 8) & 0xFF);
}

/* c64_osc3_now - SID voice 3's waveform output THIS INSTANT (11.4.1): the
 * oscillator advanced by the cycles since it was last asked, at the rate and
 * waveform it has had since then (c64sid.inc). The stamp is the cycle
 * counter's low word - a gap of more than 65,535 cycles between two reads
 * wraps, and what it gets wrong is how far a random sequence has run */
static int c64_osc3_now(void)
{
    unsigned now, dt;

    now = c64_cyc_lo + c64_run_el();
    dt = now - c64_o3t;
    c64_o3t = now;
    return (int)c64_osc3(dt, C64_W(c64_sid + 0x0E), c64_sid[0x12],
                         c64_sid[0x10] | ((c64_sid[0x11] & 15) << 8));
}

static int c64_io_rd(unsigned a)
{
    unsigned p;
    int v;

    if (a < 2)                              /* $0000/$0001 are the port (3.2) */
        return c64_pp_read((int)a);

    p = a & 0x0F00;
    if (p <= 0x0300) {                      /* $D000-$D3FF: the VIC, mirrored */
        p = a & 0x3F;
        if (p == 0x11)
            v = (int)((c64_vic[0x11] & 0x7F)
                      | ((c64_raster_now() >> 1) & 0x80));     /* bit 8 */
        else if (p == 0x12)
            v = c64_raster_now() & 0xFF;
        else if (p == 0x19)
            v = (int)(c64_vic[0x19] | 0x70);
        else
            v = -1;
        if (v >= 0) {
            if (c64_fpc)
                c64_poll_skip(a, v);
            return v;
        }
        if (p == 0x1A)
            return (int)(c64_vic[0x1A] | 0xF0);
        if ((p & 0x3E) == 0x1E)             /* $1E or $1F */
            return 0;                       /* the collision registers answer
                                             * 0 and 5.3 says why */
        return c64_vic[p];
    }
    if (p <= 0x0700) {                      /* $D400-$D7FF: the SID */
        p = a & 0x1F;
        if (p == 0x1B)                      /* voice 3's oscillator (11.4.1) */
            return c64_osc3_now();
        if (p == 0x1C)                      /* ...and its envelope: the
                                             * sustain level while the gate is
                                             * open, 0 once it closes - the
                                             * ADSR's shape is not modelled */
            return (c64_sid[0x12] & 1) ? ((c64_sid[0x14] & 0xF0)
                                          | (c64_sid[0x14] >> 4)) : 0;
        return c64_sid[p];
    }
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
    int k;

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
        /* THE RASTER COMPARE MOVED, AND THE RUN WAS SIZED FOR THE OLD ONE.
         * c64_alarm_next measured this run to the line $D012 named when it
         * started, so a raster IRQ handler that sets up the NEXT line ran on
         * past it - to the display latch, as often as not - and took that
         * interrupt late, at the run's end. The Christmas demo's chain
         * (lines 240 and 10) took its line-10 IRQ at line 52, after the
         * latch, every few frames: its picture flipped to text. Ending the
         * run makes the next one measure to the new line (9.10, 4.4) */
        if (c64_in_run)
            if ((r == 0x12) | ((r == 0x11) & (((old ^ v) & 0x80) != 0)))
                if (old != (unsigned char)v)
                    c64_cut();
        /* (the frame registers reach the screen through the display latch,
         * once a frame - c64_latch, 9.9 - row by row out of this log, 9.10) */
        if (old != (unsigned char)v)
            if (r != 0x12)
                if ((r < 0x19) | (r > 0x1F) | (r == 0x1B) | (r == 0x1C) | (r == 0x1D))
                    if (r <= 0x2E)
                        c64_dlog((int)r, v);
        return;
    }
    if (p <= 0x0700) {
        r = a & 0x1F;
        if ((unsigned)(r - 0x0E) <= 4u)     /* voice 3's rate, width or wave:
                                             * the oscillator is brought up to
                                             * NOW at the old ones first */
            c64_osc3_now();
        if ((r == 0x04) | (r == 0x0B) | (r == 0x12))
            if (v & ~c64_sid[r] & 1) {      /* a GATE OPENING: a note starts */
                k = (r == 0x04) ? 0 : ((r == 0x0B) ? 1 : 2);
                c64_sid_trig |= (unsigned char)(1 << k);
                c64_sid_last = k;
            }
        c64_sid[r] = (unsigned char)v;
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
        if (r == 0x00)                      /* (the bank, too, reaches the
                                             * screen through c64_latch) */
            if ((((unsigned char)v ^ old) & 3) != 0)
                c64_dlog(C64_RV_DD, v);
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
#define C64_X7(v) ((((unsigned)(v)) << 3) - ((unsigned)(v)))   /* a voice's
                                             * registers: 7 a voice */

/* c64_sid_audible - voice V's gate is open, TEST is off and it sounds a
 * tonal waveform (11.4.2) */
static int c64_sid_audible(int v)
{
    unsigned c = c64_sid[C64_X7(v) + 4];

    if ((c & 0x09) != 0x01)                 /* gate open, TEST off */
        return 0;
    return (c & 0x70) != 0;                 /* triangle, sawtooth or pulse */
}

/* ==========================================================================
 * THE THREE VOICES ON AN FM CHIP (C64-SPEC 11.4.3)
 *
 * When SOUND.DRV has an OPL, os88_main CLAIMS it (c64_opl_open, the shim in
 * c64.asm: SNDV_IDENT, SND_FM_INFO, SND_FM_CLAIM - MIDIRack's order) and the
 * SID's three voices go to channels 0, 1 and 2, RealDMG's shape (rwfm.inc):
 * a patch per waveform, loaded when a voice changes waveform, and after that
 * only what changed - an F-number, a key, a level, an envelope - in one
 * SNDV_OPLW batch a wake. A voice's ADSR is mapped onto the carrier's by
 * table, a note whose gate opened since the last batch is RE-KEYED (off then
 * on in the same batch, which restarts the envelope), and the gate closing
 * is a key-off, which plays the release. The chip goes back with the
 * instance: SOUND.DRV releases a claim when its owner dies.
 * ========================================================================*/
static int c64_snd_fm;                      /* the chip is ours */
static unsigned char c64_fmsh[3][6];        /* B0h A0h carTL wave car60h car80h
                                             * as last written; FF unknown */
static unsigned char c64_fmb[96];           /* one batch: register, value */
static int c64_fmn;

/* the patch for each waveform: modulator 20h, its level 40h, the channel's
 * feedback C0h. The carrier is always 21h (sustained, x1). Pulse: the
 * modulator at twice the carrier, odd harmonics - a square (rwfm.inc).
 * Sawtooth: x1 with feedback, every harmonic. Triangle: the modulator
 * silent, near a sine. Noise: x15 at full feedback, as near as two
 * operators come */
static const unsigned char c64_fmpat[4][3] = {
    { 0x22, 30, 0x00 },                     /* pulse */
    { 0x21, 22, 0x0A },                     /* sawtooth */
    { 0x21, 63, 0x00 },                     /* triangle */
    { 0x2F, 0, 0x0E }                       /* noise */
};
/* the SID's attack (2 ms .. 8 s) and decay/release (6 ms .. 24 s) indices
 * as the OPL's rates, whose time halves with each step up */
static const unsigned char c64_fmar[16] = {
    15, 12, 11, 10, 10, 9, 9, 9, 8, 7, 6, 6, 5, 4, 3, 3 };
static const unsigned char c64_fmdr[16] = {
    12, 10, 9, 9, 8, 8, 7, 7, 7, 6, 5, 4, 4, 3, 2, 2 };
/* $D418's volume 0-15 as the carrier's attenuation, 0.75 dB a step
 * (rwfm.inc's table: 20 log10(v/15) / 0.75) */
static const unsigned char c64_fmtl[16] = {
    63, 31, 23, 19, 15, 13, 11, 9, 7, 6, 5, 4, 3, 2, 1, 0 };

static void c64_fm_put(int reg, int val)
{
    c64_fmb[c64_fmn++] = (unsigned char)reg;
    c64_fmb[c64_fmn++] = (unsigned char)val;
}

/* c64_fm_quiet - every voice keyed off: a stop (11.4), its release played */
static void c64_fm_quiet(void)
{
    int v;

    c64_fmn = 0;
    for (v = 0; v < 3; v++)
        if (c64_fmsh[v][0] != 0xFF)
            if (c64_fmsh[v][0] & 0x20) {
                c64_fmsh[v][0] &= 0x1F;
                c64_fm_put(0xB0 + v, c64_fmsh[v][0]);
            }
    if (c64_fmn)
        c64_opl_send(c64_fmb, c64_fmn >> 1);
}

/* ovl_fm_update - the three voices as the SID holds them now, what changed
 * (in the OVERLAY: at most once a wake, SPEC.md 73.14's frequency split)
 * in one batch (11.4.3) */
static void ovl_fm_update(void)
{
    int v, w, b, base, on;
    unsigned f, fn, ctl, x;
    unsigned char *sh;

    c64_fmn = 0;
    for (v = 0; v < 3; v++) {
        sh = c64_fmsh[v];
        base = C64_X7(v);
        ctl = c64_sid[base + 4];
        f = C64_W(c64_sid + base);
        on = (ctl & 0x09) == 0x01;          /* gate open, TEST off */
        if (ctl & 0x80)
            w = 3;
        else if (ctl & 0x40)
            w = 0;
        else if (ctl & 0x20)
            w = 1;
        else if (ctl & 0x10)
            w = 2;
        else {
            w = -1;
            on = 0;                         /* no waveform: silence */
        }
        if ((c64_sid[0x18] & 15) == 0)
            on = 0;
        if (w >= 0)
            if ((unsigned)w != sh[3]) {     /* a new waveform: its patch */
                sh[3] = (unsigned char)w;
                c64_fm_put(0x20 + v, c64_fmpat[w][0]);
                c64_fm_put(0x23 + v, 0x21);
                c64_fm_put(0x40 + v, c64_fmpat[w][1]);
                c64_fm_put(0x60 + v, 0xF0);
                c64_fm_put(0x80 + v, 0x0F);
                c64_fm_put(0xC0 + v, c64_fmpat[w][2]);
            }
        x = (unsigned)(c64_fmar[c64_sid[base + 5] >> 4] << 4)
          | c64_fmdr[c64_sid[base + 5] & 15];       /* attack, decay */
        if (x != sh[4]) {
            sh[4] = (unsigned char)x;
            c64_fm_put(0x63 + v, x);
        }
        x = (unsigned)((15 - (c64_sid[base + 6] >> 4)) << 4)
          | c64_fmdr[c64_sid[base + 6] & 15];       /* sustain, release */
        if (x != sh[5]) {
            sh[5] = (unsigned char)x;
            c64_fm_put(0x83 + v, x);
        }
        x = c64_fmtl[c64_sid[0x18] & 15];           /* the master volume */
        if (x != sh[2]) {
            sh[2] = (unsigned char)x;
            c64_fm_put(0x43 + v, x);
        }
        /* the F-number: F x 985248 / 2^24 Hz is fnum x 49716 / 2^(20-b),
         * so fnum = F x 1.23856 / 2^b = (F x 0.61928) >> (b - 1) */
        fn = c64_muldiv(f, 0x9E89, 0xFFFF);  /* 40,585 (a decimal over 32,767
                                             * is too big for SmallerC's int) */
        b = 1;
        while (fn >= 1024) {
            fn >>= 1;
            b++;
        }
        if (fn == 0)
            on = 0;
        if (fn != 0)
            if ((fn & 0xFF) != sh[1]) {
                sh[1] = (unsigned char)fn;
                c64_fm_put(0xA0 + v, fn & 0xFF);
            }
        x = (unsigned)((b << 2) | (fn >> 8));
        if (c64_sid_trig & (1 << v))
            if (sh[0] != 0xFF)
                if (sh[0] & 0x20)           /* a new note on a held key:
                                             * off first, so it restarts */
                    c64_fm_put(0xB0 + v, sh[0] & 0x1F);
        if (on)
            x |= 0x20;
        if (x != sh[0] || (on && (c64_sid_trig & (1 << v)))) {
            sh[0] = (unsigned char)x;
            c64_fm_put(0xB0 + v, x);
        }
    }
    c64_sid_trig = 0;
    if (c64_fmn)
        c64_opl_send(c64_fmb, c64_fmn >> 1);
}

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
    int hz, v;

    /* THE TWO SILENCE PATHS ANSWER 0 WHATEVER THE GRANT SAYS: the question
     * the caller asks is "did the emulated SID's audible state reach the
     * speaker?", and a SID with nothing gated has nothing to play.
     *
     * WHICH VOICE (11.4.2). This played voice 1 and nothing else, so a tune
     * that holds voice 1 as a pad or a bass while the melody moves on 2 and
     * 3 - both Christmas demos - was one continuous tone. The speaker plays
     * the voice whose note STARTED most recently (c64_sid_last, a gate
     * opening in c64_io_wr) while it is still gated, and otherwise any
     * gated voice, 1 first. A voice is audible when its gate is open, TEST
     * is off and it has a tonal waveform; noise alone is a drum, which a
     * square wave cannot say. The master volume $D418 at 0 is silence. */
    v = -1;
    if (c64_sid[0x18] & 15) {
        if (c64_sid_audible(c64_sid_last))
            v = c64_sid_last;
        else if (c64_sid_audible(0))
            v = 0;
        else if (c64_sid_audible(1))
            v = 1;
        else if (c64_sid_audible(2))
            v = 2;
    }
    c64_sid_trig = 0;
    if (v < 0) {
        os88_snd_tone(0, 0, 0x40);
        return 0;
    }
#define C64_SIDDIV 0xFFFF
    f = C64_W(c64_sid + C64_X7(v));         /* the voice's $D400/$D401 */
    hz = (int)c64_muldiv(f, 3848, C64_SIDDIV);
    if ((unsigned)(hz - 20) > (unsigned)(12000 - 20)) {   /* hz < 20, or
                                             * hz > 12000, in one compare */
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
    c64_m.pc = (unsigned)c64_bread(0xFFFC);
    C64_HIB(c64_m.pc) = (unsigned char)c64_bread(0xFFFD);
    c64_scr_wr(C64_SCR_IRQ, 0);
    c64_nmi_state = 0;
}
