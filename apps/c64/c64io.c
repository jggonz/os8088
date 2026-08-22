/* ============================================================================
 * os8088 - apps/c64/c64io.c        the $D000-$DFFF register files
 *
 * Part of C64 (docs/C64-SPEC.md). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. The
 * register semantics are src/vicii/vicii-mem.c, src/core/ciacore.c,
 * src/c64/c64cia1.c, src/c64/c64cia2.c, src/sid/sid.c and src/c64/c64pla.c;
 * nothing of that source is vendored (C64-SPEC §2).
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * WAVE 1 IS THE REGISTER FILES AND THE RESET STATE
 * ----------------------------------------------------------------------------
 * What is here now: the VIC, SID and two CIA register files, colour RAM, the
 * $00/$01 processor port and the bank-map table, their power-on values, and
 * the read/write dispatch the core will call on its slow path. What is NOT
 * here yet is the part that needs a clock: the alarm scheduler and
 * cycles-to-the-next-event, the CIA timers, TOD, the raster compare and the
 * IRQ/NMI lines. Those are wave 2, with the core that drives them
 * (docs/C64-PORT-PLAN.md wave 2), and this file's job in wave 1 is to be the
 * one place the frame registers live so the composer reads them and not a
 * copy of them.
 * ==========================================================================*/

/* --- the register files -------------------------------------------------- */
/* $D000-$D02E, mirrored every 64 bytes on the real chip; the file is 64 wide
 * and the dispatch masks the address, which is what the mirroring IS. */
static unsigned char c64_vic[64];
static unsigned char c64_sid[32];           /* $D400-$D41C */
static unsigned char c64_cia1[16];          /* $DC00 */
static unsigned char c64_cia2[16];          /* $DD00 */

/* Colour RAM is 1,024 NIBBLES and lives in the package's bss, not in the RAM
 * claim: it is not RAM the 6510 can bank away, and the composer reads it once
 * per cell (3.1). Bits 4-7 of a read are the open bus on the real machine;
 * this port answers 0 in them and says so. */
static unsigned char c64_col[1024];

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

static const unsigned char c64_maps[8][3] = {
    /*  $A000        $D000            $E000        $01 & 7 */
    { C64_MAP_RAM,   C64_MAP_RAM,     C64_MAP_RAM    },  /* 0 */
    { C64_MAP_RAM,   C64_MAP_CHARGEN, C64_MAP_RAM    },  /* 1 */
    { C64_MAP_RAM,   C64_MAP_CHARGEN, C64_MAP_KERNAL },  /* 2 */
    { C64_MAP_BASIC, C64_MAP_CHARGEN, C64_MAP_KERNAL },  /* 3 */
    { C64_MAP_RAM,   C64_MAP_RAM,     C64_MAP_RAM    },  /* 4 */
    { C64_MAP_RAM,   C64_MAP_IO,      C64_MAP_RAM    },  /* 5 */
    { C64_MAP_RAM,   C64_MAP_IO,      C64_MAP_KERNAL },  /* 6 */
    { C64_MAP_BASIC, C64_MAP_IO,      C64_MAP_KERNAL }   /* 7 */
};

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

/* ==========================================================================
 * RESET
 *
 * The power-on register state. $D011 = $1B (25 rows, screen on, the default
 * text mode), $D016 = $C8, $D018 = $15 (matrix at $0400, characters at
 * $1000 - which is where the VIC sees the character ROM), $D020 = 14 and
 * $D021 = 6: the light-blue border over the blue screen a C64 comes up with
 * (data/C64/vice.vpl's colours 14 and 6). CIA2 PRA = $17 puts the VIC in
 * bank 0.
 * ========================================================================*/
static void c64_reset_regs(void)
{
    int i;

    for (i = 0; i < 64; i++)
        c64_vic[i] = 0;
    for (i = 0; i < 32; i++)
        c64_sid[i] = 0;
    for (i = 0; i < 16; i++) {
        c64_cia1[i] = 0;
        c64_cia2[i] = 0;
    }
    c64_vic[0x11] = 0x1B;
    c64_vic[0x16] = 0xC8;
    c64_vic[0x18] = 0x15;
    c64_vic[0x20] = 14;
    c64_vic[0x21] = 6;
    c64_cia1[0x02] = 0xFF;                  /* PRA is all outputs: the
                                             * keyboard columns */
    c64_cia2[0x00] = 0x17;
    c64_cia2[0x02] = 0x3F;
    c64_pp_ddr = 0x2F;
    c64_pp_data = 0x37;
    c64_bank = 7;
    for (i = 0; i < 1024; i++)
        c64_col[i] = 0;
}

/* c64_pp_write - a write to $00 or $01 re-evaluates the bank map AT ONCE
 * (3.2). Wave 2's core also recomputes the fetch segment and the boundary
 * word here, which is why this is one function and not two assignments. */
static void c64_pp_write(int addr, int v)
{
    if (addr == 0)
        c64_pp_ddr = (unsigned char)v;
    else
        c64_pp_data = (unsigned char)v;
    /* a bit the DDR makes an input reads its pulled-up level, so the bank
     * bits are (data & ddr) | (~ddr & 7): an input line floats high */
    c64_bank = ((c64_pp_data & c64_pp_ddr) | (~c64_pp_ddr & 0x07)) & 7;
}

/* c64_pp_read - $01's read-back (3.2). Bits 0-2 read what was written where
 * $00 makes them outputs and the pulled-up level otherwise; bit 4 is cassette
 * sense and reads 1, there being no datassette in this build (11.2); bits 6-7
 * are unconnected and read 0. Stated, because a program that reads $01 to
 * discover the bank sees this table and not a real 6510's decay behaviour. */
static int c64_pp_read(int addr)
{
    int v;
    if (addr == 0)
        return c64_pp_ddr;
    v = (c64_pp_data & c64_pp_ddr) | (~c64_pp_ddr & 0x1F);
    v |= 0x10;                              /* cassette sense: no tape */
    return v & 0x3F;
}

/* ==========================================================================
 * THE I/O DISPATCH
 *
 * Every $D000-$DFFF access in an I/O bank is a direct cdecl call into these
 * two from inside the core's handler (3.4). Wave 1 has no core, so the only
 * caller is this file's own C - which is exactly why they exist now: the
 * register files have ONE owner from the first commit.
 * ========================================================================*/
static int c64_io_rd(unsigned a)
{
    unsigned p = a & 0x0F00;

    if (p <= 0x0300)                        /* $D000-$D3FF: the VIC, mirrored */
        return c64_vic[a & 0x3F];
    if (p <= 0x0700)                        /* $D400-$D7FF: the SID */
        return c64_sid[a & 0x1F];
    if (p <= 0x0B00)                        /* $D800-$DBFF: colour RAM */
        return c64_col[a & 0x03FF];
    if (p <= 0x0C00)
        return c64_cia1[a & 0x0F];
    if (p <= 0x0D00)
        return c64_cia2[a & 0x0F];
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
    unsigned p = a & 0x0F00;
    unsigned r;
    unsigned char old;

    if (p <= 0x0300) {
        r = a & 0x3F;
        old = c64_vic[r];
        c64_vic[r] = (unsigned char)v;
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
            c64_lum_update();               /* $D020 is the border's own level */
            c64_border_dirty = 1;
            c64_dirty_any = 1;              /* ...so the next wake flushes */
        }
        return;
    }
    if (p <= 0x0700) {
        c64_sid[a & 0x1F] = (unsigned char)v;
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
        c64_cia1[a & 0x0F] = (unsigned char)v;
        return;
    }
    if (p <= 0x0D00) {
        r = a & 0x0F;
        old = c64_cia2[r];
        c64_cia2[r] = (unsigned char)v;
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
