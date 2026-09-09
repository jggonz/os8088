/* ============================================================================
 * os8088 - apps/apple2/a2prog.c   ovl_*: Load Program and Save Program
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 12). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * THESE TWO ROWS HAVE NO DEFINER IN ANY REFERENCE, AND THAT IS RECORDED
 * ----------------------------------------------------------------------------
 * MII's `Load & Run Binary...` is `.disabled = 1` with its handler commented
 * out; apple2emu and AppleWin reach a program through a disk. So these are
 * THIS PORT'S OWN rows, and section 12 states the reason: there is no Disk II
 * in this PR and a listing has to get in somehow.
 *
 * They are written from AppleWin's `bin/A2_BASIC.SYM`, which names its own
 * source (Bob Sander-Cederlof's S-C DocuMentor: Applesoft):
 *
 *     TXTTAB $67/$68   VARTAB $69/$6A   ARYTAB $6B/$6C   STREND $6D/$6E
 *     FRETOP $6F/$70   MEMSIZ $73/$74   PRGEND $AF/$B0
 *
 * and program text begins at $0801 on a II+.
 *
 * ----------------------------------------------------------------------------
 * WHAT IS ACCEPTED IS THE PROGRAM'S OWN STRUCTURE, NOT A LENGTH-PREFIX SNIFF
 * ----------------------------------------------------------------------------
 * A tokenised Applesoft program is a chain of lines - `next-line pointer (2)`,
 * `line number (2)`, tokens, `$00` - ending with a next of `$0000`, and the
 * ACCEPTANCE IS THE WALK (section 12):
 *
 *  - the two-byte header must be inside the image, and the line's `$00` must
 *    be found inside it, so a truncated or wrong-typed file is refused rather
 *    than walked off the end of a 48K claim;
 *  - line numbers must be 0..63999 and must not go backwards;
 *  - the recomputed link must be greater than the line's own address and
 *    below `MEMSIZ`;
 *  - the walk ends at the terminator, and THE ADDRESS IT ENDS ON is what the
 *    four pointers are written from - not the file's length, which a trailing
 *    byte can make wrong.
 *
 * THE 2-BYTE LENGTH PREFIX IS A HINT AND NOTHING ELSE. This document once
 * called it "documented" and cited `A2_BASIC.SYM`, which establishes the
 * pointer addresses above and nothing about a file format - so the sniff was
 * a heuristic wearing a citation. If word 0 equals `filesize - 2` the two
 * bytes are skipped; otherwise the file is headerless. Either way the walk
 * decides.
 *
 * **AND A HINT THAT CANNOT BE SECOND-GUESSED IS NOT ONE.** The test fires on
 * a HEADERLESS file whenever word 0 - which is then the first line's link,
 * `$0801 + len(line 1)` - happens to equal `filesize - 2`, i.e. whenever the
 * lines after the first total exactly 2,051 bytes. That file loads and RUNs
 * on a real Apple II and this port used to refuse it, with the wrong sentence
 * (`Not an Applesoft program.`), on files ITS OWN Save wrote - Save is
 * headerless. So the walk is run at the hinted base and, if it fails, AT ZERO,
 * and only a file that walks at neither is refused.
 *
 * That is why the walk is TWO PASSES and not one: the repairing pass writes
 * links INTO the claim, so a failed walk at base 2 has already overwritten
 * the bytes a walk at base 0 would read as the first line's NUMBER. The
 * validating pass (`fix` = 0) touches nothing, so it can be run twice; the
 * repairing pass runs once, on the base that was accepted.
 *
 * AND THE LINKS ARE REPAIRED RATHER THAN TRUSTED, which is what Applesoft
 * itself does: `FIX.LINKS` at `$D4F2` (AppleWin/bin/A2_BASIC.SYM:224) rebuilds
 * the chain from the line LENGTHS, so a program saved from a machine whose
 * TXTTAB was not `$0801` loads and RUNs here. Every link is recomputed from
 * where the line actually lands, IN THE TRANSIENT CLAIM, and
 * **nothing is written into the machine's memory until the walk has passed**:
 * a half-loaded program is a `]` prompt that crashes on RUN with nothing
 * saying why.
 *
 * ----------------------------------------------------------------------------
 * THE INNER LOOP IS NOT HERE
 * ----------------------------------------------------------------------------
 * The line terminator is found by `a2_scan0` (a2mem.inc), one call a LINE.
 * Written in C it is one `os88_peek` a BYTE - ~235 ms for a 5KB program.
 * docs/C-TOOLCHAIN.md's rule, and the bound this file can state: four peeks,
 * one scan and two pokes a LINE, twice over (the two passes above).
 *
 * AND NONE OF IT RUNS UNDER THE GFX LOCK. `os88_onfile` is dispatched with
 * the DESKTOP's lock held - kernel/fdlg.inc:45-48 says so of fdlg_commit and
 * every window proc around it - so the whole of this, six claims, a floppy
 * read of up to 46 KB and the walk, would be seconds of stopped desktop on a
 * 4.77 MHz XT with every other task's painter blocked behind it. The handler
 * LATCHES a name and the WAKE spends it (apple2.c), which is the same route
 * Edit > Copy, Edit > Paste and the reset service already take and the same
 * one the launch document has always taken.
 *
 * ...and `CC_ASSOC` declares `BAS` in this wave (a2assoc.inc), so a `.BAS`
 * beside the package opens on the FIRST double-click of a cold boot.
 * ==========================================================================*/

/* AppleWin bin/A2_BASIC.SYM, and the II+'s program floor and ceiling */
#define A2_TXTTAB  0x0067
#define A2_VARTAB  0x0069
#define A2_ARYTAB  0x006B
#define A2_STREND  0x006D
#define A2_PRGEND  0x00AF
#define A2_PROG    0x0801                   /* program text begins here */
#define A2_PROGTOP 0xC000                   /* MEMSIZ on a 48K II+ with no
                                             * DOS: the first address that is
                                             * not RAM */
#define A2_PRGMAX  (A2_PROGTOP - A2_PROG)   /* 47,103 - and the comment used
                                             * to say 47,615, which is why the
                                             * association arm's 47 KB claim
                                             * looked like a match for it and
                                             * was 1,025 bytes over */
#define A2_PRGKB   46                       /* ...and the CLAIM that covers it:
                                             * 46 KB is 47,104, the first whole
                                             * kilobyte at or above A2_PRGMAX.
                                             * 47 would be 48,128 - a ceiling
                                             * ABOVE the one the size-known arm
                                             * refuses on, which is how a file
                                             * the machine cannot hold reached
                                             * the walk (see ovl_a2_load) */
#define A2_LNMAX   (32000u + 31999u)        /* 63999 - Applesoft's own
                                             * line-number cap, WRITTEN AS A
                                             * SUM because the literal is
                                             * `Constant too big for 16-bit
                                             * signed type` in this C, the same
                                             * refusal `-32768` gets and with
                                             * the same fix (LESSONS.md 3) */
#define A2_NOZERO  ((unsigned)-1)           /* a2_scan0's $FFFF sentinel. The
                                             * literal `0xFFFF` is
                                             * `Constant too big for 16-bit
                                             * signed type` in this C, exactly
                                             * as `-32768` is (LESSONS.md 3) */

/* ovl_a2_wr16 - a zero-page pointer, little-endian, through the RAM claim.
 *
 * `ovl_` BECAUSE IT IS A PER-COMMAND BODY (APPLE2-SPEC section 15.0.2). It is
 * reachable from ovl_a2_load and from nothing else, so resident it cost its
 * own ~55 bytes of IMAGE and a resident shim, for code that runs once per
 * File > Load Program.
 *
 * **THE CALLS ARE STILL FAR, AND THIS COMMENT USED TO CLAIM OTHERWISE.**
 * `Both callers are in .modc, so the calls are near now` is not what
 * tools/cc8086.py emits: an `ovl_` -> `ovl_` call goes through the module's
 * own ENTRY VECTOR whatever segment the caller sits in, so
 * build/apple2.gen.asm has five `call far [cc_ovm_ovl_a2_wr16]` in
 * ovl_a2_load and pays five bridge crossings a load exactly as before. What
 * the rename buys is the resident bytes; the crossings are unchanged, and
 * five of them once per Load Program is 234 us on the target.
 *
 * IT ANSWERS 1, on LESSONS.md 5's rule that an overlay function answers a
 * status and 0 means it did not happen. Nothing outside the module can reach
 * it today, so nothing can be told 0 - but the rule is what the next reader
 * copies. */
static int ovl_a2_wr16(unsigned a, unsigned v)
{
    a2_wr(a, (int)(v & 0xFF));
    a2_wr(a + 1, (int)((v >> 8) & 0xFF));
    return 1;
}

/* ovl_a2_named - "<verb> <name>" on the status row (26 cells; a FAT12 name is
 * at most 12, and `Loaded ` is 7). `ovl_` for ovl_a2_wr16's reason.
 *
 * a2_progmsg STAYS RESIDENT and must: bss is never moved by the overlay split
 * (SPEC.md 73.14 - the code goes out, every byte it names stays), and a2_say
 * keeps the pointer.
 *
 * ITS VERB IS A LITERAL AT EVERY CALL SITE, and build.sh's message-length
 * gate now reads them: the gate walks `a2_say(` AND `ovl_a2_named(` and fails
 * on a call whose first argument is not a literal, because a composed message
 * that never reaches the corpus is a gate that has stopped looking. */
static char a2_progmsg[28];

static void ovl_a2_named(const char *verb, const char *name)
{
    os88_strcpy(a2_progmsg, verb, sizeof(a2_progmsg));
    os88_strcpy(a2_progmsg + os88_strlen(a2_progmsg), name, 13);
    a2_say(a2_progmsg);
}

/* ovl_a2_walk - THE CHAIN WALK, and the ONE place a file's structure is
 * judged (section 12). It runs entirely inside the transient claim and never
 * touches the machine.
 *
 * `fix` = 0 VALIDATES AND WRITES NOTHING; `fix` = 1 does the same walk and
 * REPAIRS each link as it goes (Applesoft's own FIX.LINKS at $D4F2). The
 * separation is what makes the length-prefix hint recoverable - this file's
 * header carries the argument - and it costs one extra pass of four peeks and
 * one a2_scan0 a LINE on the accepted base, which is once per Load Program.
 *
 * The answer is the program's length INCLUDING its two terminating zero
 * bytes, or 0 for "this is not an Applesoft program". Every `break` is a
 * refusal. */
static unsigned ovl_a2_walk(unsigned seg, unsigned base, unsigned end, int fix)
{
    unsigned p, q, addr, next, ln, prev, plen, z;

    p = base;
    addr = A2_PROG;
    prev = 0;
    plen = 0;
    for (;;) {
        if (p + 2 > end)
            break;                          /* no terminator inside the image */
        next = (unsigned)os88_peek(seg, p)
             | ((unsigned)os88_peek(seg, p + 1) << 8);
        if (next == 0) {                    /* THE END OF THE PROGRAM, and the
                                             * two zero bytes are part of it:
                                             * VARTAB points past them */
            plen = (p + 2) - base;
            break;
        }
        if (p + 4 > end)
            break;
        ln = (unsigned)os88_peek(seg, p + 2)
           | ((unsigned)os88_peek(seg, p + 3) << 8);
        if (ln > (unsigned)A2_LNMAX || ln < prev)
            break;
        prev = ln;
        z = a2_scan0(seg, p + 4, end - (p + 4));
        if (z == A2_NOZERO)
            break;                          /* the line never terminates */
        q = z + 1;                          /* one past the line's own $00 */
        next = addr + (q - p);              /* FIX.LINKS: where it LANDED */
        if (next <= addr || next >= (unsigned)A2_PROGTOP)
            break;
        if (fix) {
            os88_poke(seg, p, (int)(next & 0xFF));
            os88_poke(seg, p + 1, (int)((next >> 8) & 0xFF));
        }
        addr = next;
        p = q;
    }
    return plen;
}

/* ovl_a2_load - File > Load Program..., once the picker has a name.
 *
 * `size_lo` IS 0 WHEN THE SIZE IS NOT KNOWN, which is the ASSOCIATION path: a
 * double-click hands the package a name and a folder and no size, where the
 * Standard File dialog reads one out of the mount snapshot and hands it over
 * so the program can refuse before the motor spins (os88.h). With no size the
 * claim is what the machine can spare and the READ's own answer is the size.
 * cword.c:2640 states the same distinction one package along.
 *
 * **BOTH ARMS END AT ONE CEILING, AND THAT IS A FIX RATHER THAN A TIDY.** The
 * association arm used to claim `min(largest, 47) KB` and read `kb << 10`
 * bytes into it - 48,128, which is 1,025 bytes ABOVE the A2_PRGMAX the
 * size-known arm refuses on, because the constant's own comment said 47,615
 * when it is 47,103. A `.BAS` between 47,104 and 48,128 bytes then reached
 * the walk, and the walk's only ceiling is `next < A2_PROGTOP`: a program
 * whose terminator landed at $BFFF passed, one byte was written at $C000 -
 * outside the 48K the a2_wr fence protects - and TXTTAB..PRGEND were set to
 * $C001, above MEMSIZ. The status row said `Loaded` for a program the machine
 * cannot RUN, which is the outcome the walk exists to make impossible. `cap`
 * is clamped to A2_PRGMAX on both arms now, so `end` is bounded and the two
 * arms refuse the same file.
 *
 * **AND THE CLAIM STEPS DOWN.** Asking for the whole 46 KB whatever the file
 * is asks a 640KB desktop for a 46 KB PINNED claim on top of this package's
 * already-pinned 64 KB, and the wave's own done_when file is 45 bytes: the
 * commonest outcome of the fixed ceiling was `No heap for the program.` about
 * one cluster. The claim halves until it is taken, so a small file loads into
 * whatever the heap had.
 *
 * **AND THE READ ITSELF IS WHAT SAYS THE FILE DID NOT FIT.** The first cut of
 * this decided it from `got == cap` after the walk had failed, on the belief
 * that a read can be CUT OFF by a short buffer. No os8088 kernel does that:
 * kernel/diskw.inc:1836-1845 compares the directory entry's 32-bit size
 * against the caller's capacity BEFORE any data I/O and answers FERR_BIG with
 * the destination untouched, which os88.h states in words. So an oversized
 * file arrives here as `got` = 0 with `os88_ferr()` = FERR_BIG, the arm below
 * names which ceiling bound it, and the walk never sees it at all. (The old
 * arm was not merely unreachable: `got < 4` fired first, so the association
 * path refused an over-large program as `Cannot read the file.` The gate that
 * said otherwise was passing on a HOST STUB that truncated - the harness's
 * own note now carries that lesson.)
 *
 * It answers 1 whether it loaded or refused, because a refusal has ALREADY
 * been said on the status row: 0 at the call site can then only mean the
 * runtime refused the module itself (a2cmd.c's header rule), which is the one
 * thing the caller could act on. */
static int ovl_a2_load(const char *name, unsigned size_lo)
{
    unsigned seg, kb, got, cap, base, end, next, plen;

    if (size_lo > (unsigned)A2_PRGMAX) {
        a2_say("Too large for a 48K Apple.");
        return 1;
    }
    if (size_lo != 0) {
        if (size_lo < 4) {
            a2_say("Not an Applesoft program.");
            return 1;
        }
        /* THE CEILING IN 16 BITS, and it is not `(size + 1023) >> 10`:
         * `unsigned` is sixteen bits here, so that addition wraps for a file
         * over 64,512 bytes. The shift-then-round form cannot overflow
         * (c64load.c's finding). */
        kb = (size_lo >> 10) + ((size_lo & 1023) != 0);
        cap = size_lo;
        seg = os88_mem_claim((int)kb);
    } else {
        kb = os88_mem_largest_kb();
        if (kb > (unsigned)A2_PRGKB)
            kb = (unsigned)A2_PRGKB;
        /* HALVE UNTIL IT IS TAKEN. os88_mem_largest_kb answers the largest
         * free block, so the first ask normally succeeds; what this covers is
         * a heap that moved between the question and the claim, and a desktop
         * with no 46 KB hole in it but plenty of room for a listing. */
        seg = 0;
        while (kb >= 1) {
            seg = os88_mem_claim((int)kb);
            if (seg != 0)
                break;
            kb >>= 1;
        }
        cap = kb << 10;
        if (cap > (unsigned)A2_PRGMAX)      /* ...AND THE SAME CEILING AS THE
                                             * ARM ABOVE (this file's header) */
            cap = (unsigned)A2_PRGMAX;
    }
    if (seg == 0) {
        a2_say("No heap for the program.");
        return 1;
    }
    got = os88_file_read_seg(name, seg, cap);
    if (got == 0 && os88_ferr() == OS88_FERR_BIG) {
        /* THE FILE IS LARGER THAN THE CLAIM AND THE KERNEL READ NOTHING. Two
         * different things to be told and the user can act on the second:
         * `cap` is A2_PRGMAX exactly when the claim reached the machine's own
         * ceiling, and anything less means the HEAP is what bound it. TWO
         * CALLS AND NOT A TERNARY, on a2cmd.c's reason: a literal behind a
         * `?` is one build.sh's gate never sees. */
        os88_mem_free(seg);
        if (cap >= (unsigned)A2_PRGMAX)
            a2_say("Too large for a 48K Apple.");
        else
            a2_say("Too large for free memory.");
        return 1;
    }
    if (size_lo != 0 ? (got != size_lo) : (got < 4)) {
        os88_mem_free(seg);
        a2_say("Cannot read the file.");
        return 1;
    }
    size_lo = got;

    /* the HINT: a 2-byte little-endian length prefix in front of the program */
    base = 0;
    end = size_lo;
    next = (unsigned)os88_peek(seg, 0) | ((unsigned)os88_peek(seg, 1) << 8);
    if (next == size_lo - 2)
        base = 2;

    /* --- THE WALK, VALIDATING ONLY, AT THE HINTED BASE AND THEN AT ZERO ---
     * `plen` stays 0 until a terminator is reached, so ONE test below says
     * whether the file was accepted and there is exactly one place the
     * machine's memory is touched. The retry is this file's header: the hint
     * is a hint, and a headerless file whose lines after the first total
     * 2,051 bytes trips it. */
    plen = ovl_a2_walk(seg, base, end, 0);
    if (plen == 0 && base != 0) {
        base = 0;
        plen = ovl_a2_walk(seg, base, end, 0);
    }
    if (plen == 0) {
        os88_mem_free(seg);
        a2_say("Not an Applesoft program.");
        return 1;
    }
    /* ...and NOW the links are repaired, on the base that was accepted. The
     * pass cannot fail: it is the walk that just answered. */
    ovl_a2_walk(seg, base, end, 1);

    /* ...AND ONLY NOW DOES THE MACHINE'S MEMORY MOVE (section 12). */
    a2_zzcopy_in((unsigned)A2_PROG, seg, base, plen);
    os88_mem_free(seg);
    ovl_a2_wr16(A2_TXTTAB, (unsigned)A2_PROG);
    ovl_a2_wr16(A2_VARTAB, (unsigned)A2_PROG + plen);
    ovl_a2_wr16(A2_ARYTAB, (unsigned)A2_PROG + plen);
    ovl_a2_wr16(A2_STREND, (unsigned)A2_PROG + plen);
    ovl_a2_wr16(A2_PRGEND, (unsigned)A2_PROG + plen);
    /* the block move set no dirty bits - the core's own write path sets them
     * one at a time and this went round it (section 7.5) - so the rows the
     * move ACTUALLY REACHED are marked by hand, and no others. It was
     * a2_dirty_all(), which recomposed the whole page for a listing not one
     * displayed byte of which had moved; ovl_a2_dirty_range's header in
     * a2scr.c carries the arithmetic. */
    ovl_a2_dirty_range((unsigned)A2_PROG,
                       (unsigned)A2_PROG + plen - 1);
    ovl_a2_named("Loaded ", name);
    return 1;
}

/* ovl_a2_save - File > Save Program..., headerless, `$0801` to `VARTAB - 1`
 * (section 12). VARTAB is the end of the program INCLUDING the two zero bytes
 * of its terminating link, which is why a Save and a Load round-trip byte for
 * byte: `NEW` leaves VARTAB at `$0803` and a saved empty program is two zero
 * bytes. */
static int ovl_a2_save(const char *name)
{
    unsigned seg, kb, vartab, len;

    vartab = (unsigned)a2_rd(A2_VARTAB)
           | ((unsigned)a2_rd(A2_VARTAB + 1) << 8);
    if (vartab <= (unsigned)A2_PROG
        || vartab > (unsigned)A2_PROGTOP) {
        a2_say("Bad program pointers.");
        return 1;
    }
    len = vartab - (unsigned)A2_PROG;
    if (len < 3) {                          /* the two terminator bytes and
                                             * nothing else is `NEW` */
        a2_say("No program to save.");
        return 1;
    }
    kb = (len >> 10) + ((len & 1023) != 0);
    seg = os88_mem_claim((int)kb);
    if (seg == 0) {
        a2_say("No heap for the program.");
        return 1;
    }
    a2_zzcopy_out(seg, 0, (unsigned)A2_PROG, len);
    if (os88_file_write_seg(name, seg, len) != 0) {
        os88_mem_free(seg);
        a2_say("Cannot write the file.");
        return 1;
    }
    os88_mem_free(seg);
    ovl_a2_named("Saved ", name);
    return 1;
}

/* ovl_a2_prog - the body os88_onfile calls, once the picker has a name.
 *
 * The RESIDENT half (os88_onfile in apple2.c) does the size refusal and the
 * overlay fence; this is the part that runs once per command and therefore
 * goes out (SPEC.md 73.14). */
static int ovl_a2_prog(int mode, const char *name, unsigned size_lo,
                       void *win)
{
    (void)win;
    if (mode == OS88_FDLG_SAVE)
        return ovl_a2_save(name);
    return ovl_a2_load(name, size_lo);
}
