/* ============================================================================
 * os8088 - apps/infones/nippu.c      the PPU register file and the BUS
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THIS FILE FOLLOWS: the CPU memory map's shape - one switch on the top
 * three address bits, four 8KB PRG windows, sixteen 1KB PPU windows, $4014
 * sprite DMA and the $4016/$4017 pad shift - is InfoNES's
 * src/K6502_rw.h:46-186 (read) and :190-470 (write), read for WHAT each
 * region does and reimplemented. The loopy v/t scroll arithmetic and the
 * palette-address mirror are agnes's (agnes.c:1100-1140, :1071-1082,
 * :1312-1380 and :946-949, MIT) rather than nofrendo's, for the licence
 * reason SPEC.md 91.2 records: nofrendo is LGPL-2 and transcribing its
 * expressions is reproduction in part. nofrendo was read for INTENT and
 * nothing of it is here.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * THE BUS CONTRACT IS SPEC.md 91.4.3 AND IT WAS WRITTEN BEFORE THIS FILE
 * ----------------------------------------------------------------------------
 * The core handles $0000-$1FFF, $6000-$7FFF and PRG READS itself (three
 * compares, no call); everything else arrives here. So the two entry points
 * below are the WHOLE of the slow path, they are cdecl and they are called
 * from inside an instruction - which is why nicpu.inc saves the core's
 * registers around them and puts DS and the fetch ES back afterwards.
 *
 * WAVE 1 SCOPE. The register file, the address decode, the scroll pair, the
 * palette mirror, the pad shift and the APU's three registers with behaviour
 * are HERE. What is not here yet, and is wave 2's: the per-line latch and
 * increments that DRIVE v from t, sprite evaluation, and the vblank flag's
 * timing - the frame loop is what raises those, and this build has no frame
 * loop (nirun.c). Every one of them has its marker below.
 * ==========================================================================*/

/* --- the PPU's own registers --------------------------------------------- */
static unsigned char ni_ctrl;       /* $2000 */
static unsigned char ni_pmask;      /* $2001 */
static unsigned char ni_status;     /* $2002, bits 7-5 only */
static unsigned char ni_oamaddr;    /* $2003 */
static unsigned ni_v, ni_t;         /* the loopy pair (agnes:1071-1082) */
static unsigned char ni_fx;         /* fine X, the low 3 bits of the first
                                     * $2005 write */
static unsigned char ni_wlatch;     /* the write toggle SHARED by $2005 and
                                     * $2006, cleared by a $2002 read */
static unsigned char ni_rdbuf;      /* $2007's buffered read */

#define NI_CTRL_NMI   0x80
#define NI_CTRL_INC32 0x04
#define NI_ST_VBL     0x80

/* --- the controller (SPEC.md 91.6.4) -------------------------------------
 * Bit order, from InfoNES's SDL front end (InfoNES_System_SDL.cpp:503-540) BY
 * BIT NUMBER and never by its comments: bit0 A, bit1 B, bit2 Select, bit3
 * Start, bit4 Up, bit5 Down, bit6 Left, bit7 Right. The Linux front end sets
 * the SAME bits with its A and B comments SWAPPED (Linux:237-247), which is
 * exactly the sort of thing that is copied without being read. */
#define NI_PAD_A      0x01
#define NI_PAD_B      0x02
#define NI_PAD_SEL    0x04
#define NI_PAD_START  0x08
#define NI_PAD_UP     0x10
#define NI_PAD_DOWN   0x20
#define NI_PAD_LEFT   0x40
#define NI_PAD_RIGHT  0x80

static unsigned char ni_pad;        /* the STICKY latch the $4016 strobe
                                     * clears (SPEC.md 91.6.4) */
static unsigned char ni_pad_shift;  /* what $4016 is handing out */
static unsigned char ni_pad_strobe;

/* --- the silent APU (SPEC.md 91.4.3) -------------------------------------
 * Three registers have behaviour and the rest are write-only sinks. The frame
 * IRQ is NOT optional: a game that waits on it hangs forever without one. */
static unsigned char ni_apu_mode;   /* $4017 bit 7: the 5-step mode */
static unsigned char ni_apu_inhib;  /* $4017 bit 6: IRQ inhibit */
static unsigned char ni_apu_irq;    /* the frame-IRQ flag $4015 bit 6 reads */

/* --- the open bus (SPEC.md 91.4.3) ---------------------------------------
 * nicpu.inc stores every value THIS FILE answers into the scratch byte after
 * the call, so what is read back here is the previous one - which is what
 * "the last value on the data bus" means. A RAM or PRG read does not update
 * it: that would cost a store on the core's hottest path, and nothing that
 * can observe open bus can observe those reads either. Stated approximation,
 * SPEC.md 91.4.3. */
static int ni_openbus(void)
{
    return os88_peek(ni_machseg, NI_O_SCR + NI_S_BUS);
}

/* ==========================================================================
 * THE PPU'S OWN ADDRESS SPACE
 * ========================================================================*/

/* ni_nt_off - a nametable address to an offset in the machine claim. The
 * four 1KB logical tables fold onto TWO physical ones, and the arrangement is
 * the header's mirroring bit (InfoNES's PPU_MirrorTable, InfoNES.cpp:166-174,
 * as a two-case fold rather than a 6x4 table - four-screen is refused at load
 * and single-screen arrives with mapper 1 in wave 4, which is when the table
 * earns its bytes). */
static unsigned ni_nt_off(unsigned a)
{
    unsigned idx;

    idx = (a >> 10) & 3;
    if (ni_mirror)
        idx = idx & 1;              /* vertical: A, B, A, B */
    else
        idx = (idx >> 1) & 1;       /* horizontal: A, A, B, B */
    return NI_O_VRAM + idx * 1024 + (a & 0x03FF);
}

/* ni_pal_off - the palette's own mirror: $3F10/$3F14/$3F18/$3F1C fold onto
 * $3F00/$3F04/$3F08/$3F0C (agnes.c:946-949's 32-byte map, as the one rule it
 * encodes). */
static unsigned ni_pal_off(unsigned a)
{
    a = a & 0x1F;
    if ((a & 0x13) == 0x10)
        a = a & 0x0F;
    return NI_O_PAL + a;
}

static int ni_ppu_rd(unsigned a)
{
    a = a & 0x3FFF;
    if (a < 0x2000) {
        if (ni_chrseg == 0)
            return 0;
        return os88_peek(ni_chrseg, a);     /* wave 4 makes this the mapper's
                                             * 1KB window table */
    }
    if (a < 0x3F00)
        return os88_peek(ni_machseg, ni_nt_off(a));
    return os88_peek(ni_machseg, ni_pal_off(a));
}

static void ni_ppu_wr(unsigned a, int v)
{
    a = a & 0x3FFF;
    if (a < 0x2000) {
        if (ni_chrseg == 0 || ni_chr8 != 0)
            return;                 /* CHR-ROM is not writable; CHR-RAM is,
                                     * and it is the path robotfindskitten and
                                     * RHDE take (SPEC.md 91.9) */
        os88_poke(ni_chrseg, a, v);
        ni_chr_dirty(a);            /* the tile cache's bank bitmask */
        return;
    }
    if (a < 0x3F00) {
        os88_poke(ni_machseg, ni_nt_off(a), v);
        return;
    }
    os88_poke(ni_machseg, ni_pal_off(a), v & 0x3F);
}

/* ==========================================================================
 * $2000-$2007
 * ========================================================================*/

static int ni_reg_rd(unsigned r)
{
    int val;

    if (r == 2) {
        /* $2002: the read CLEARS the vblank flag and the shared write
         * toggle - and it clears the FLAG only, never a latched NMI edge
         * (SPEC.md 91.4.3). The low five bits are open bus. */
        val = (int)ni_status | (ni_openbus() & 0x1F);
        ni_status = ni_status & (unsigned char)~NI_ST_VBL;
        ni_wlatch = 0;
        return val;
    }
    if (r == 4)
        return os88_peek(ni_machseg, NI_O_OAM + ni_oamaddr);
    if (r == 7) {
        /* the BUFFERED read: everything below the palette answers the
         * PREVIOUS byte, and a palette read answers immediately while still
         * filling the buffer from the nametable underneath it. */
        val = (int)ni_rdbuf;
        ni_rdbuf = (unsigned char)ni_ppu_rd(ni_v);
        if ((ni_v & 0x3FFF) >= 0x3F00)
            val = (int)ni_rdbuf | (ni_openbus() & 0xC0);
        ni_v = (ni_v + ((ni_ctrl & NI_CTRL_INC32) ? 32 : 1)) & 0x7FFF;
        return val;
    }
    return ni_openbus();            /* $2000, $2001, $2003, $2005, $2006 are
                                     * write-only and answer the bus */
}

static void ni_reg_wr(unsigned r, int v)
{
    switch (r) {
    case 0:
        /* THE NMI EDGE ON A LATE ENABLE (SPEC.md 91.4.3): enabling NMI while
         * vblank is ALREADY set latches one. A model that only checks at the
         * start of vblank misses it, and the games that do this hang. */
        if ((v & NI_CTRL_NMI) && !(ni_ctrl & NI_CTRL_NMI)
            && (ni_status & NI_ST_VBL))
            ni_nmi_raise();
        ni_ctrl = (unsigned char)v;
        ni_t = (ni_t & 0xF3FF) | ((unsigned)(v & 3) << 10);
        break;
    case 1:
        ni_pmask = (unsigned char)v;
        break;
    case 3:
        ni_oamaddr = (unsigned char)v;
        break;
    case 4:
        os88_poke(ni_machseg, NI_O_OAM + ni_oamaddr, v);
        ni_oamaddr = (unsigned char)((ni_oamaddr + 1) & 0xFF);
        break;
    case 5:
        if (!ni_wlatch) {
            ni_t = (ni_t & 0xFFE0) | (unsigned)((v >> 3) & 0x1F);
            ni_fx = (unsigned char)(v & 7);
            ni_wlatch = 1;
        } else {
            ni_t = (ni_t & 0x8C1F)
                 | ((unsigned)(v & 7) << 12)
                 | ((unsigned)(v & 0xF8) << 2);
            ni_wlatch = 0;
        }
        break;
    case 6:
        if (!ni_wlatch) {
            ni_t = (ni_t & 0x00FF) | ((unsigned)(v & 0x3F) << 8);
            ni_wlatch = 1;
        } else {
            ni_t = (ni_t & 0xFF00) | (unsigned)(v & 0xFF);
            ni_v = ni_t;
            ni_wlatch = 0;
        }
        break;
    case 7:
        ni_ppu_wr(ni_v, v);
        ni_v = (ni_v + ((ni_ctrl & NI_CTRL_INC32) ? 32 : 1)) & 0x7FFF;
        break;
    default:
        break;
    }
    /* A $2005 or $2006 write MID-LINE takes effect from the NEXT line: this
     * PPU is a SCANLINE state machine and SPEC.md 91.5 says so in a sentence
     * rather than promising dot granularity it does not have. */
}

/* ==========================================================================
 * $4000-$401F - the APU, the pad, and the DMA
 * ========================================================================*/

static int ni_apu_rd(unsigned a)
{
    int val;

    if (a == 0x4015) {
        /* the frame-IRQ flag in bit 6 and zero elsewhere, and READING IT
         * CLEARS IT - which is how a game that polls rather than takes the
         * interrupt makes progress (SPEC.md 91.4.3) */
        val = ni_apu_irq ? 0x40 : 0;
        ni_apu_irq = 0;
        ni_irq_clear(NI_IRQ_APU);
        return val;
    }
    if (a == 0x4016 || a == 0x4017) {
        /* $4017 is controller 2, and there is none: it answers 0 in bit 0,
         * which is a CONNECTED pad with nothing pressed rather than a floating
         * line. Controller 2 is a DROP and not a greying (SPEC.md 91.11) -
         * there is no surface to grey, the legend being two plain rows of
         * keys and not a two-column table - and InfoNES's own Win32 front end
         * sets pad 2 to 0 too (InfoNES_System_Win.cpp:1016), so this drops
         * what the reference itself never had. */
        if (a == 0x4017)
            return 0x40;            /* bits 6-5 are open-bus-ish and read 0x40
                                     * on real hardware most of the time */
        val = (ni_pad_shift & 1) | 0x40;
        ni_pad_shift = (unsigned char)(ni_pad_shift >> 1);
        return val;
    }
    return ni_openbus();
}

static void ni_apu_wr(unsigned a, int v)
{
    if (a == 0x4014) {
        ni_oam_dma(v);
        return;
    }
    if (a == 0x4016) {
        /* the strobe. While it is high the shift register reloads; the
         * falling edge is what latches. THE STICKY LATCH IS CLEARED HERE
         * (SPEC.md 91.6.4): a press that happened anywhere in the frame is
         * delivered exactly once, and the sweep that ORed it in is free to
         * run at whatever rate the bracket sweeps at. */
        if ((v & 1) == 0 && ni_pad_strobe) {
            ni_pad_shift = ni_pad;
            ni_pad = 0;
        }
        ni_pad_strobe = (unsigned char)(v & 1);
        if (ni_pad_strobe)
            ni_pad_shift = ni_pad;
        return;
    }
    if (a == 0x4017) {
        ni_apu_mode = (unsigned char)(v & 0x80);
        ni_apu_inhib = (unsigned char)(v & 0x40);
        if (ni_apu_inhib) {
            ni_apu_irq = 0;
            ni_irq_clear(NI_IRQ_APU);
        }
        return;
    }
    /* every other APU register is a write-only sink, and that is a FACT about
     * this port rather than an omission (SPEC.md 91.4.3) */
}

/* ni_oam_dma - $4014. 513 cycles, 514 when it starts on an odd cycle, and the
 * debt is SUBTRACTED from the scanline budget and carried into the following
 * lines without executing instructions (SPEC.md 91.4.3). */
static unsigned ni_dma_debt;
static unsigned char ni_dma_odd;

static void ni_oam_dma(int page)
{
    unsigned src;

    src = (unsigned)(page & 0xFF) << 8;
    if (src < 0x2000) {
        /* THE FAST PATH, and it is the only one any real game takes: a source
         * wholly inside the 2KB RAM claim is one mover call rather than 256
         * bus reads. The mirror is the same `& 0x07FF` the core applies. */
        ni_oam_move(src & 0x07FF);
    } else {
        int i;
        for (i = 0; i < 256; i++)
            os88_poke(ni_machseg, NI_O_OAM + ((ni_oamaddr + i) & 0xFF),
                      ni_bread(src + (unsigned)i));
    }
    ni_dma_debt += 513 + (ni_dma_odd ? 1 : 0);
    ni_dma_odd = (unsigned char)(ni_dma_odd ^ 1);
}

/* ==========================================================================
 * THE TWO ENTRY POINTS nicpu.inc CALLS (cdecl, from inside an instruction)
 * ========================================================================*/

/* ni_io_rd - a read the core could not answer itself: $2000-$5FFF. */
int ni_io_rd(unsigned a)
{
    if (a < 0x4000)
        return ni_reg_rd(a & 7);    /* $2000-$3FFF mirrors every 8 */
    if (a < 0x4020)
        return ni_apu_rd(a);
    return ni_openbus();            /* $4020-$5FFF is unmapped */
}

/* ni_io_wr - a write the core could not answer itself: $2000-$5FFF, and every
 * write at $8000 and above, which is the MAPPER.
 *
 * IT ANSWERS A FLAG: non-zero means the PRG mapping moved, and the core then
 * clears its fetch bias so the next fetch re-biases ES even though PC never
 * left the cached region (SPEC.md 91.4.3). A UxROM game switching the bank it
 * is executing from is the ordinary case, not the exotic one. */
int ni_io_wr(unsigned a, int v)
{
    if (a < 0x4000) {
        ni_reg_wr(a & 7, v);
        return 0;
    }
    if (a < 0x4020) {
        ni_apu_wr(a, v);
        return 0;
    }
    if (a < 0x8000)
        return 0;                   /* $4020-$5FFF is unmapped, and
                                     * $6000-$7FFF never reaches here - the
                                     * core writes the SRAM window itself */
    return ni_map_wr(a, v);
}
