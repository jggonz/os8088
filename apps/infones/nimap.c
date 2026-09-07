/* ============================================================================
 * os8088 - apps/infones/nimap.c      the mappers, behind one switch
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THIS FILE FOLLOWS: InfoNES's five mapper files -
 * src/mapper/InfoNES_Mapper_000.cpp (NROM), _001.cpp (MMC1), _002.cpp
 * (UxROM), _003.cpp (CNROM) and _004.cpp (MMC3) - read for BEHAVIOUR and
 * reimplemented. InfoNES's 138-file #include scheme collapses here because
 * there is ONE translation unit (SPEC.md 73.1), so the five become five
 * blocks behind one switch. agnes's mapper4_set_offsets (agnes.c:2425-2583,
 * MIT) is the cross-check for MMC3's bank tables and for MMC1; agnes has NO
 * mapper 3, so InfoNES is the only source for CNROM.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * WAVE 1 IS MAPPER 0, AND THE OTHER FOUR REFUSE AT LOAD
 * ----------------------------------------------------------------------------
 * SPEC.md 91.9 ships five mappers and wave 4 writes the other four. Until
 * then a ROM whose mapper is 1, 2, 3 or 4 is REFUSED AT LOAD with the number
 * in it - InfoNES's own sentence, InfoNES.cpp:439 - because a mapper that is
 * accepted and then does nothing is a black screen with no explanation, which
 * is the exact shape SPEC.md 47 exists to stop. ni_map_ok() is the one
 * predicate, and the wave that writes a mapper adds its number there and
 * nowhere else.
 *
 * ROMPAGE / VROMPAGE become INDEX ARITHMETIC over the window table rather
 * than pointers: the table holds SEGMENT BASES, one per 8KB of $8000-$FFFF
 * (SPEC.md 91.3.1), and a bank number times 8KB is a paragraph count the
 * loader adds to the claim's base.
 *
 * EVERY `%` AGAINST THE BANK COUNT IS KEPT AND NOT 'OPTIMISED' TO A MASK.
 * A 6-bank ROM is why: bank counts are not all powers of two, and a mask
 * silently reads the wrong bank on exactly the ROMs nobody tests with.
 * ==========================================================================*/

/* ni_map_ok - is this mapper number one this build implements? THE ONE
 * PREDICATE: the loader's refusal and any later greying both ask it. */
static int ni_map_ok(unsigned m)
{
    return (m == 0);                /* wave 4 adds 1, 2, 3 and 4 */
}

/* ni_prg_set - point one 8KB window of $8000-$FFFF at a PRG bank.
 *
 * `bank8` is counted in 8KB units from the start of the PRG claim, and it is
 * taken MODULO the number of 8KB banks the ROM actually has, which is what
 * makes a 16KB NROM answer at $C000 as well as at $8000 without a second
 * mechanism. The window holds a SEGMENT: bank8 * 8192 bytes is bank8 * 512
 * paragraphs. */
static void ni_prg_set(int win, unsigned bank8)
{
    unsigned n;

    n = ni_prg16 * 2;               /* the ROM's size in 8KB banks */
    if (n == 0)
        return;
    bank8 = bank8 % n;
    ni_pokew(ni_machseg, NI_O_SCR + NI_S_PRG + (unsigned)win * NI_S_PRGSTR,
             ni_prgseg + bank8 * 512);
}

/* ni_map_reset - the power-on mapping. Mapper 0 is the whole of it here:
 * InfoNES_Mapper_000.cpp's Map0_Init sets $8000 and $C000 to the first and
 * last 16KB bank, which for a one-bank ROM is the same bank twice. */
static void ni_map_reset(void)
{
    unsigned last;

    switch (ni_mapper) {
    case 0:
    default:
        last = (ni_prg16 > 1) ? 2 : 0;      /* the second 16KB bank, or the
                                             * first one again */
        ni_prg_set(0, 0);
        ni_prg_set(1, 1);
        ni_prg_set(2, last);
        ni_prg_set(3, last + 1);
        break;
    }
}

/* ni_map_wr - a write at $8000 or above.
 *
 * The answer is the RE-BIAS FLAG (SPEC.md 91.4.3): non-zero when the PRG
 * mapping moved, so that the core drops its fetch bias even though PC never
 * left the cached region. Mapper 0 has no registers at all - a write there is
 * a game writing to ROM, which is a no-op on the hardware and is one here -
 * so it answers 0 and nothing re-biases. */
static int ni_map_wr(unsigned a, int v)
{
    (void)a;
    (void)v;

    switch (ni_mapper) {
    case 0:
    default:
        return 0;                   /* NROM: no bank registers exist */
    }
}

/* ni_map_hsync - the mapper's per-scanline hook, called once a visible line
 * by the frame loop (nirun.c). MMC3's scanline IRQ lives here in wave 4, and
 * SPEC.md 91.5 states in advance that it is InfoNES's HSync APPROXIMATION of
 * the A12-edge counter - 8x16 sprites and a mid-line pattern-table swap can
 * put it a line out - rather than agnes's faked PA12 edge, whose own comment
 * admits it is at the wrong dot. */
static void ni_map_hsync(void)
{
    /* wave 4 */
}

/* ni_chr_dirty - a CHR-RAM write invalidates one 1KB bank of the tile cache.
 * InfoNES's own `ChrBufUpdate |= 1 << (addr >> 10)` (K6502_rw.h:325), as a
 * word of bits: eight banks of pattern space, one bit each. The DECODE is
 * niband.inc's and happens once a frame, never per write. */
static unsigned ni_chr_dirty_mask;

static void ni_chr_dirty(unsigned a)
{
    ni_chr_dirty_mask |= (unsigned)1 << ((a >> 10) & 7);
}

/* ni_cache_sync - decode every DIRTY 1KB CHR bank into the tile cache, once a
 * frame and never per write (SPEC.md 91.5.1).
 *
 * InfoNES's own invalidation is a bit per 1KB bank set on a CHR-RAM write
 * (`ChrBufUpdate |= 1 << (addr >> 10)`, K6502_rw.h:325), and the decode it
 * gates is 4,096 stores - about 41,000 8088 cycles a bank. A game that writes
 * its whole pattern table through $2007 in one vblank touches all eight banks
 * and dirties eight bits; decoding at the write would decode each bank up to
 * 1,024 times.
 *
 * A power-on sets every bit, so a CHR-ROM title decodes its eight banks on
 * the first frame and never again. */
static void ni_cache_sync(void)
{
    unsigned b;

    if (ni_chr_dirty_mask == 0 || ni_cacheseg == 0 || ni_chrseg == 0)
        return;
    for (b = 0; b < 8; b++)
        if (ni_chr_dirty_mask & ((unsigned)1 << b))
            ni_chr_decode(ni_cacheseg, b * 4096U, ni_chrseg, b * 1024U);
    ni_chr_dirty_mask = 0;
}
