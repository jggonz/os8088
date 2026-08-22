/* ============================================================================
 * os8088 - apps/c64/c64load.c        Smart attach - OUT OF LINE
 *
 * Part of C64 (C64-SPEC §11.3, 13.1). Derived from VICE 3.10,
 * Copyright (C) 1996-2025 the VICE team, GPL-2-or-later - see
 * apps/c64/COPYING. The autostart sequence is src/autostart-prg.c's
 * RAM-injection mode; nothing of that source is vendored.
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ovl_*: the body runs once per Smart attach, so it lives in C64.OVL. The
 * CALLBACK that reaches it - os88_onfile - is RESIDENT, because the runtime
 * reaches a callback by a near offset (13.3), and it refuses by SIZE before
 * the disk is touched.
 *
 * ----------------------------------------------------------------------------
 * WHY A SCRATCH CLAIM AND NOT A DIRECT READ
 * ----------------------------------------------------------------------------
 * A .PRG lands at the two-byte load address in its own first two bytes, which
 * is any address at all - and os88_file_read_seg needs a 512-aligned base
 * (SPEC.md 2.1.1; int 13h answers a transfer straddling a 64KB physical
 * boundary with error 09h, and only starting 512-aligned prevents it). So the
 * file is read into a transient claim of ceil(size / 1KB), moved into the RAM
 * claim at the load address with c64_zzcopy_in - far to far, neither side a C
 * pointer (3.6) - and the claim is freed. OSAPI_FILE_READ_AT exists and is
 * unwrapped, and it offers nothing this needs: NO NEW SLOT (15.2).
 *
 * ----------------------------------------------------------------------------
 * WAVE 1 LANDS THE FILE; WAVE 4 STARTS IT
 * ----------------------------------------------------------------------------
 * What is here now is the whole landing - claim, read, move, free, the dirty
 * bits - and it is real: the bytes are in the C64's RAM at the address the
 * file names. What is not here is the AUTOSTART state machine, because there
 * is no core to run it: reset, wait for READY. in screen RAM, inject,
 * mem_set_basic_text(start, end) unconditionally (autostart-prg.c:383, on
 * EVERY load and not only at $0801), then RUN\r into $0277 with the count at
 * $C6. That is wave 4 (docs/C64-PORT-PLAN.md wave 4), and until then a load
 * says what it did and nothing claims to have run anything.
 * ==========================================================================*/

/* AND ITS 0 MEANS ONE THING ONLY: THE RUNTIME REFUSED THE LOAD.
 * c64cmd.c's header rule - every body returns 1, so a 0 at a call site can
 * only be no C64.OVL on the disk, a stale module or no heap - applies here
 * too, and this function used to break it: its four own refusals, each of
 * which has ALREADY been said on the status row, also answered 0. A caller
 * therefore could not act on the answer even if it read it, and os88_onfile
 * did not read it at all - so a runtime refusal on Smart attach reached the
 * toast only, under a WF_FULL window where the toast is not where the user is
 * looking (9.8, 13.3). */
static int ovl_load_prg(const char *name, unsigned size_lo)
{
    static char msg[C64_MSGMAX];
    unsigned seg, kb, got, load, n;

    if (size_lo < 3) {
        c64_say("PRG too short.");
        return 1;                            /* SAID. 0 is the runtime's
                                             * refusal alone (the header) */
    }
    /* CEILING IN 16 BITS, and this is not the obvious `(size + 1023) >> 10`:
     * `size_lo` is `unsigned` and that is SIXTEEN bits on the target, so the
     * addition wraps for every PRG over 64,512 bytes and asks for a 0KB or a
     * 1KB claim for a file up to 65,533 - which os88_onfile lets through, and
     * which then reads 64KB into a 1KB claim. The shift-then-round form
     * cannot overflow. */
    kb = (int)(size_lo >> 10) + ((size_lo & 1023) != 0);
    seg = os88_mem_claim((int)kb);
    if (seg == 0) {
        c64_say("PRG: no heap for it.");
        return 1;                            /* SAID. 0 is the runtime's
                                             * refusal alone (the header) */
    }
    got = os88_file_read_seg(name, seg, size_lo);
    if (got != size_lo) {
        os88_mem_free(seg);
        c64_say("PRG: cannot read it.");
        return 1;                            /* SAID. 0 is the runtime's
                                             * refusal alone (the header) */
    }
    /* the two-byte load address, little-endian, and the refusal 11.3 states:
     * a file whose end passes $FFFF is refused with the fact */
    load = (unsigned)os88_peek(seg, 0) | ((unsigned)os88_peek(seg, 1) << 8);
    n = size_lo - 2;
    /* ...AND THE END TEST IS ON THE LAST BYTE, NOT ON ONE PAST IT. The first
     * draft was `load + n > 0xFFFF || load + n < load`: the first half can
     * never be true in 16-bit unsigned arithmetic, and the second mistook the
     * legal exclusive end $10000 for a wrap - so a PRG whose LAST byte is
     * $FFFF (load $FFFF, one byte; or the KERNAL-vector patchers that end
     * there) was refused with a message about running past $FFFF when it does
     * not. `n - 1 > 0xFFFF - load` asks the question about the last byte and
     * is exact in 16 bits. n is at least 1 here (size_lo >= 3 above), and the
     * test says so anyway. */
    if (n != 0 && (unsigned)(n - 1) > (unsigned)(0xFFFFu - load)) {
        os88_mem_free(seg);
        c64_say("PRG runs past $FFFF.");
        return 1;                            /* SAID. 0 is the runtime's
                                             * refusal alone (the header) */
    }
    c64_zzcopy_in(load, seg, 2, n);
    os88_mem_free(seg);
    /* every page the move touched (9.2). The core's own write path sets these
     * one at a time; a block move sets them by hand. */
    c64_dirty_all();

    os88_strcpy(msg, "Loaded ", sizeof(msg));
    os88_strcpy(msg + os88_strlen(msg), name, 14);
    c64_say(msg);
    return 1;
}
