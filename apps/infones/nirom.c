/* ============================================================================
 * os8088 - apps/infones/nirom.c      ovl_*: the iNES loader (SPEC.md 91.13.2)
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * WHAT THIS FILE FOLLOWS: the iNES header's semantics - the magic, byRomSize
 * in 16KB units, byVRomSize in 8KB units, byInfo1's four low bits (mirroring,
 * SRAM, trainer, four-screen), the two mapper nibbles and the DISKDUDE GUARD
 * that decides whether the high one may be believed - are InfoNES's
 * src/InfoNES.h:263-274 (NesHeader_tag) and src/InfoNES.cpp:358-380. The
 * unsupported-mapper sentence is InfoNES.cpp:439 VERBATIM, with the number in
 * it. The bad-magic wording follows src/sdl/InfoNES_System_SDL.cpp:158-167
 * ("%s isn't a NES format file.").
 *
 * ONE CHECK IS THIS PORT'S OWN AND NONE OF THE THREE REFERENCES HAS IT: THE
 * LENGTH. InfoNES, nofrendo and agnes all trust the header's PRG and CHR
 * counts and read that many bytes; here every byte off a floppy is hostile
 * (SPEC.md 19), so a header claiming 256KB in a 24KB file is refused before a
 * claim is taken rather than read off the end of the staging buffer.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * ovl_*: THIS FILE IS IN INFONES.OVL FROM THE FIRST COMMIT (SPEC.md 91.13.1)
 * ----------------------------------------------------------------------------
 * It runs ONCE PER OPEN, from a callback, which is exactly SPEC.md 73.14's
 * frequency test - and it buys back the resident bytes the four mappers of
 * wave 4 cannot move. Renaming the function to ovl_* is the ENTIRE mechanism:
 * tools/cc8086.py emits its CODE into the module and leaves every global,
 * literal and bss byte it names RESIDENT and DS-relative.
 *
 * ITS REFUSAL PATH IS AN ORDINARY 0-RETURN. An overlay function that cannot
 * be loaded - no heap, no file, a stale module - answers 0 without running,
 * and the caller must treat that as a normal path (LESSONS.md 5). Here the
 * caller is os88_onwake, which puts `Cannot load INFONES.OVL` on the panel's
 * STATE LINE: the user is looking at the panel, not at a toast.
 * ==========================================================================*/

/* The smallest thing that could be a ROM: the 16-byte header and one 16KB PRG
 * bank. os88_onfile refuses below this from the size the dialog already knew,
 * BEFORE the disk is touched (os88.h's own note - 40KB off a floppy is about
 * four seconds of motor the user cannot tell from a load that works). */
#define NI_ROM_MIN   (16 + 16384)
/* THE STAGING CLAIM'S CAP IS THE WORD ITSELF. A ROM is read WHOLE when its
 * size fits ONE unsigned - which is every game this port ships, every blargg
 * single and nestest - and larger ROMs are wave 4's cluster-window path
 * (SPEC.md 91.13.2). There is no `size_lo > 65536` to write here: `unsigned`
 * is 16 bits (SPEC.md 73.7) and SmallerC refuses the constant outright
 * (`Constant too big for 16-bit type`), which is the compiler saying that the
 * test can never be false. The predicate is `size_hi != 0`, and it takes the
 * 65,536-byte ROM with it - that one file is a wave-4 path and not a
 * shipped case. */
#define NI_PRG_MAXKB 256
#define NI_CHR_MAXKB 128
#define NI_CACHE_KB  32

/* ni_rom_free - give back exactly what the last load took. A RELOAD FREES
 * THE OLD ROM'S CLAIMS BEFORE IT TAKES THE NEW LOAD'S, so the peak is one ROM
 * plus the staging claim and never two ROMs (SPEC.md 91.3.1). The machine
 * claim is not one of these: it outlives every ROM. */
static void ni_rom_free(void)
{
    if (ni_prgseg) {
        os88_mem_free(ni_prgseg);
        ni_prgseg = 0;
    }
    if (ni_chrseg) {
        os88_mem_free(ni_chrseg);
        ni_chrseg = 0;
    }
    if (ni_cacheseg) {
        os88_mem_free(ni_cacheseg);
        ni_cacheseg = 0;
    }
    ni_prgkb = 0;
    ni_chrkb = 0;
    ni_cachekb = 0;
    ni_mapper = 0;
    ni_prg16 = 0;
    ni_chr8 = 0;
    ni_mirror = 0;
    ni_sram = 0;
    ni_fourscr = 0;
    ni_trainer = 0;
    ni_romname[0] = 0;
}

/* ni_refuse_kb - RUNCPM's refusal shape: quote what was asked for and what
 * the heap answered (SPEC.md 47). A refusal that says only "out of memory"
 * tells the user nothing they can act on.
 *
 * ONE HELPER FOR EVERY HEAP REFUSAL IN THE PACKAGE, and that is the fix
 * rather than the tidying: three spellings of this one sentence shipped at
 * once - os88_main composed `INFONES wanted 13 KB, largest is 5 KB`, this
 * routine wrote `Wanted 13KB, largest is 5KB`, and SPEC.md 91.10 quoted a
 * third that existed nowhere. The SPEC now quotes THESE TWO STRINGS, which is
 * what §91.10's own opening paragraph promises of every row in its table.
 *
 * TWO FORMS, because the two fields are 40 cells and 24 characters:
 *   state  `INFONES wanted 999KB, largest is 999KB`   38, of 40
 *   toast  `Wanted 999KB, have 999KB`                 24, of 24
 * Both are composed in ni_msg, in that order, so this needs no second buffer
 * - the toast is staged into the kernel's own strip before the state sentence
 * overwrites it (kernel/toast.inc copies on the call). */
static int ni_refuse_kb(unsigned want)
{
    unsigned got;

    got = os88_mem_largest_kb();

    os88_strcpy(ni_msg, "Wanted ", NI_MSGMAX);
    ni_appnum(ni_msg, want);
    ni_app(ni_msg, "KB, have ");
    ni_appnum(ni_msg, got);
    ni_app(ni_msg, "KB");
    os88_toast(ni_msg, 0);

    os88_strcpy(ni_msg, "INFONES wanted ", NI_MSGMAX);
    ni_appnum(ni_msg, want);
    ni_app(ni_msg, "KB, largest is ");
    ni_appnum(ni_msg, got);
    ni_app(ni_msg, "KB");
    ni_setfield(NI_F_STATE, ni_msg);
    return 0;
}

/* ovl_rom_load - read a .NES file and stand the machine up on it.
 *
 * Answers 1 loaded, 0 refused - and every 0 has already said why on the state
 * line. Called from os88_onwake and NEVER from os88_main (there is no
 * instance to resolve a module for yet) or from inside the bracket (SPEC.md
 * 91.6.5). */
static int ovl_rom_load(const char *name, unsigned size_lo, unsigned size_hi)
{
    static unsigned char hdr[16];   /* STATIC, and rule 1 is why: an
                                     * automatic array with a VARIABLE index
                                     * is an address of an automatic, which
                                     * tools/cc8086.py refuses by name
                                     * (SPEC.md 73.5) */
    unsigned stage, kb, got, off, need, need_kb, i;
    unsigned mapper;
    int dirty;

    ni_rom_free();
    ni_state = NI_ST_NOROM;

    if (size_hi != 0) {
        /* Over 65,535 bytes: the whole-file staging read cannot express it
         * and wave 4's cluster-window path is what will (SPEC.md 91.13.2).
         * It is a SIZE refusal and not a format one, and saying so is the
         * difference between "this port cannot yet" and "your file is
         * broken". */
        ni_say("ROM is larger than this port maps.", "ROM over 64KB");
        return 0;
    }
    if (size_lo != 0 && size_lo < NI_ROM_MIN) {
        ni_say("Not a NES format file.", "Not a NES file");
        return 0;
    }

    /* --- the staging claim, and it is TRANSIENT -------------------------
     * SIZE 0 MEANS "NOT KNOWN", which is the ASSOCIATION path: a double-click
     * on a .NES hands this the name and nothing else, where the Standard File
     * dialog always knows the size and uses it to refuse before the motor
     * (SPEC.md 54.10, os88.h's own note). With no size the claim is the whole
     * 64KB the read can answer with, and what comes back IS the size. */
    /* DIVIDE BEFORE ADDING, because `unsigned` is SIXTEEN BITS here: the
     * obvious `(size_lo + 1023) / 1024` wraps for every size in
     * 64,513..65,535 - 65,535 + 1,023 is 66,558, which is 1,022 in sixteen
     * bits, so kb comes out 0, os88_mem_claim(0) is refused (kernel/memory.inc
     * mem_claim_1's `or ax, ax / jz .fail0`) and a file this port could hold
     * is refused with `Wanted 0KB, have 400KB`. No well-formed iNES image lands
     * in that window - the legal sizes step 24,592 / 40,976 / 57,360 / 65,552
     * - but the Standard File dialog takes no extension filter (nicmd.c), so
     * ANY 64,513-65,535-byte file the user picks reaches this line. It is the
     * same "arithmetic that cannot overflow rather than a test that cannot
     * see" the length check sixty lines below is built on. */
    kb = size_lo ? (size_lo / 1024) + ((size_lo & 1023) ? 1 : 0) : 64;
    stage = os88_mem_claim((int)kb);
    if (stage == 0)
        return ni_refuse_kb(kb);

    got = os88_file_read_seg(name, stage, size_lo ? size_lo : 0xFFFFU);
    if (size_lo == 0) {
        /* A file over 65,535 bytes is refused by the read itself rather than
         * mis-read (os88.h: a short buffer is FERR_BIG and nothing is read),
         * so 0 back here means either an unreadable file or one bigger than
         * this port maps - and wave 4's cluster-window path is what changes
         * that. */
        size_lo = got;
        if (got < NI_ROM_MIN) {
            os88_mem_free(stage);
            ni_say("Not a NES file, or over 64KB.", "Not a NES file");
            return 0;
        }
    }
    if (got != size_lo) {
        os88_mem_free(stage);
        ni_say("Cannot read the ROM.", "Cannot read the ROM");
        return 0;
    }

    for (i = 0; i < 16; i++)
        hdr[i] = (unsigned char)os88_peek(stage, i);

    if (hdr[0] != 'N' || hdr[1] != 'E' || hdr[2] != 'S' || hdr[3] != 0x1A) {
        os88_mem_free(stage);
        ni_say("Not a NES format file.", "Not a NES file");
        return 0;
    }

    /* --- the header, InfoNES.cpp:358-380 -------------------------------- */
    ni_prg16 = hdr[4];              /* byRomSize, 16KB banks */
    ni_chr8 = hdr[5];               /* byVRomSize, 8KB banks; 0 = CHR-RAM */
    ni_mirror = (unsigned char)(hdr[6] & 1);
    ni_sram = (unsigned char)((hdr[6] & 2) ? 1 : 0);
    ni_trainer = (unsigned char)((hdr[6] & 4) ? 1 : 0);
    ni_fourscr = (unsigned char)((hdr[6] & 8) ? 1 : 0);

    /* THE DISKDUDE GUARD, InfoNES.cpp:365-373. Header bytes 12-15 are
     * reserved and must be zero; a great many dumps from the 1990s carry the
     * string "DiskDude!" across bytes 7-15 instead, which puts garbage in the
     * mapper's HIGH nibble. So the high nibble is believed only when those
     * four bytes are clean, and otherwise the mapper is the low nibble alone
     * - which is what every such dump actually is. */
    dirty = 0;
    for (i = 12; i < 16; i++)
        if (hdr[i] != 0)
            dirty = 1;
    mapper = (unsigned)(hdr[6] >> 4);
    if (!dirty)
        mapper |= (unsigned)(hdr[7] & 0xF0);
    ni_mapper = (unsigned char)mapper;

    /* --- THE LENGTH CHECK, which is this port's own ----------------------
     *
     * IT IS DONE IN KILOBYTES, AND THAT IS THE WHOLE OF THE FIX FOR A DEFECT
     * THIS PORT SHIPPED FOR ONE AFTERNOON. The first version computed
     * `16 + prg16 * 16384 + chr8 * 8192` in an `unsigned`, which is SIXTEEN
     * BITS here (SPEC.md 73.7): a header claiming 256KB gives 270,352, which
     * wraps to 8,208 - a number SMALLER than the 24KB file it was lying
     * about - so the check passed, the panel read `PRG ROM : 256KB` beside
     * `Ready`, and the loader went on to move 256KB out of a 24KB staging
     * claim. It was photographed on the glass and NOT by the host harness,
     * because the host's `unsigned` is THIRTY-TWO bits and the product does
     * not wrap there: **a 16-bit overflow is invisible to niuitest by
     * construction**, and the answer is arithmetic that cannot overflow
     * rather than a test that cannot see.
     *
     * In KB nothing can wrap: prg16 is at most 16 (256KB) and chr8 at most 16
     * (128KB), so the sum is at most 385. The BYTE arithmetic below runs only
     * after `need_kb <= 64` has been established, which bounds every product
     * in it: prg16 * 16384 <= 49,152 and chr8 * 8192 <= 57,344. */
    if (ni_prg16 == 0 || ni_prg16 > NI_PRG_MAXKB / 16
        || ni_chr8 > NI_CHR_MAXKB / 8) {
        os88_mem_free(stage);
        ni_say("ROM is larger than this port maps.", "ROM over 64KB");
        return 0;
    }
    need_kb = 1 + ni_prg16 * 16 + ni_chr8 * 8;      /* the 16-byte header and
                                                     * the 512-byte trainer
                                                     * both round up into that
                                                     * first KB */
    if (need_kb > 64 || need_kb > (size_lo / 1024) + 1) {
        /* Either the header claims more than the whole-file path can hold -
         * which is wave 4's cluster-window path and not this one - or it
         * claims more than the file actually holds. Both quote both numbers,
         * because a refusal that says only "bad ROM" tells the user nothing
         * they can act on (SPEC.md 47). */
        os88_mem_free(stage);
        /* THE TOAST IS THE FIRST HALF OF THE SENTENCE, and it is composed that
         * way rather than paraphrased: `Header claims 385KB, file is 64KB` is
         * 34 characters and the strip holds 24 (NI_TOASTMAX), so the strip
         * gets the claim and the state line gets both numbers. One buffer,
         * because the toast is copied into the kernel's strip on the call. */
        os88_strcpy(ni_msg, "Header claims ", NI_MSGMAX);
        ni_appnum(ni_msg, need_kb - 1);
        ni_app(ni_msg, "KB");
        os88_toast(ni_msg, 0);
        ni_app(ni_msg, ", file is ");
        ni_appnum(ni_msg, size_lo / 1024);
        ni_app(ni_msg, "KB");
        ni_setfield(NI_F_STATE, ni_msg);
        return 0;
    }
    need = 16;
    if (ni_trainer)
        need += 512;
    need += ni_prg16 * 16384U;
    need += ni_chr8 * 8192U;
    if (need > size_lo) {           /* ...and now the exact byte count, which
                                     * catches the trainer's own 512 */
        os88_mem_free(stage);
        os88_strcpy(ni_msg, "Header claims ", NI_MSGMAX);
        ni_appnum(ni_msg, need / 1024);
        ni_app(ni_msg, "KB");
        os88_toast(ni_msg, 0);          /* the strip's 24, as above */
        ni_app(ni_msg, ", file is ");
        ni_appnum(ni_msg, size_lo / 1024);
        ni_app(ni_msg, "KB");
        ni_setfield(NI_F_STATE, ni_msg);
        return 0;
    }

    if (ni_fourscr) {
        /* Refused with the FLAG NAMED (SPEC.md 91.11): four-screen VRAM is
         * 4KB on the cartridge and this port's nametable claim is 2KB. */
        os88_mem_free(stage);
        ni_say("4 Screen VRAM is unsupported.", "No 4 Screen VRAM");
        return 0;
    }
    if (!ni_map_ok(mapper)) {
        /* InfoNES's own sentence, InfoNES.cpp:439, with the number in it.
         * This is SPEC.md 47's attempted-and-reported half and it CAN toast:
         * the mapper number is not knowable before the header is read, so
         * there was nothing to grey. */
        os88_mem_free(stage);
        os88_strcpy(ni_msg, "Mapper #", NI_MSGMAX);
        ni_appnum(ni_msg, mapper);
        ni_app(ni_msg, " unsupported");  /* 23 at three digits: the strip's */
        os88_toast(ni_msg, 0);
        os88_strcpy(ni_msg, "Mapper #", NI_MSGMAX);
        ni_appnum(ni_msg, mapper);
        ni_app(ni_msg, " is unsupported.");   /* ...and InfoNES's own sentence
                                               * verbatim on the state line */
        ni_setfield(NI_F_STATE, ni_msg);
        return 0;
    }

    /* --- the claims, in the order that makes a refusal cheapest ---------- */
    ni_prgkb = ni_prg16 * 16;
    ni_chrkb = ni_chr8 ? ni_chr8 * 8 : 8;
    ni_cachekb = NI_CACHE_KB;

    ni_prgseg = os88_mem_claim((int)ni_prgkb);
    if (ni_prgseg == 0) {
        os88_mem_free(stage);
        return ni_refuse_kb(ni_prgkb);
    }
    ni_chrseg = os88_mem_claim((int)ni_chrkb);
    if (ni_chrseg == 0) {
        os88_mem_free(stage);
        ni_rom_free();
        return ni_refuse_kb(ni_chrkb);
    }
    ni_cacheseg = os88_mem_claim((int)ni_cachekb);
    if (ni_cacheseg == 0) {
        os88_mem_free(stage);
        ni_rom_free();
        return ni_refuse_kb(ni_cachekb);
    }

    /* --- out of the staging claim, with the header skew ------------------
     * The 16-byte header is exactly why os88_file_read_at cannot be used to
     * read a PRG bank directly: its offset AND its capacity must each be a
     * whole number of clusters or it answers FERR_NAME, and 16 mod any
     * cluster size is not 0 (SPEC.md 91.13.2). Reading the file WHOLE and
     * moving it out costs one extra pass and gets the skew for free. */
    off = 16;
    if (ni_trainer) {
        /* the trainer is 512 bytes and it is LOADED, not skipped: it belongs
         * at $7000, which is 0x1000 into the SRAM window (SPEC.md 91.13.2) */
        ni_move(ni_machseg, NI_O_SRAM + 0x1000, stage, off, 512);
        off += 512;
    }
    ni_move(ni_prgseg, 0, stage, off, ni_prg16 * 16384U);
    off += ni_prg16 * 16384U;
    if (ni_chr8)
        ni_move(ni_chrseg, 0, stage, off, ni_chr8 * 8192U);
    else
        ni_fill(ni_chrseg, 0, 0, 8192);     /* CHR-RAM starts empty */

    os88_mem_free(stage);           /* TRANSIENT: it is gone before this
                                     * function returns, so the peak is one
                                     * ROM plus one staging claim */

    ni_reset_machine(1);
    ni_state = NI_ST_READY;
    return 1;
}
