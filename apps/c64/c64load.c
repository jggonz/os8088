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
 * ovl_*: the body runs once per Smart attach, so it lives in the overlay -
 * part 1 of C64.O88 since C64-SPEC 13.3, read at launch with the ROM. The
 * CALLBACK that reaches it - os88_onfile - is RESIDENT, because the runtime
 * reaches a callback by a near offset (13.3), and it refuses by SIZE before
 * the disk is touched.
 *
 * ----------------------------------------------------------------------------
 * WHY A BUFFER AND NOT A DIRECT READ
 * ----------------------------------------------------------------------------
 * A .PRG lands at the two-byte load address in its own first two bytes, which
 * is any address at all - and a read needs a 512-aligned base (SPEC.md 2.1.1;
 * int 13h answers a transfer straddling a 64KB physical boundary with error
 * 09h, and only starting 512-aligned prevents it). So the file is read
 * through a buffer a chunk at a time and moved into the RAM claim with
 * c64_zzcopy_in - far to far, neither side a C pointer (3.6). The buffer and
 * why it is not a claim the size of the file: THE BUFFER A LOAD IS READ
 * THROUGH, below.
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
 * $C6. That is wave 4 (docs/plans/completed/C64-PORT-PLAN.md wave 4), and until then a load
 * says what it did and nothing claims to have run anything.
 * ==========================================================================*/

/* ovl_land - N bytes from SSEG:SOFF into the C64's RAM at LOAD, and every
 * page they touch marked (9.2). $FFC0-$FFF9 IS THE SCRATCH (C64-SPEC 3.5):
 * the block move stops short of it, and what lies there or past it goes a
 * byte at a time through c64_wr, which keeps the program's own copy and
 * writes $FFFA-$FFFF */
static void ovl_land(unsigned load, unsigned sseg, unsigned soff, unsigned n)
{
    unsigned k, m;

    if (n == 0)
        return;
    if (load < 0xFFC0u && (unsigned)(n - 1) >= (unsigned)(0xFFC0u - load)) {
        m = 0xFFC0u - load;
        c64_zzcopy_in(load, sseg, soff, m);
        for (k = m; k < n; k++)
            c64_wr(load + k, os88_peek(sseg, soff + k));
    } else if (load >= 0xFFC0u) {
        for (k = 0; k < n; k++)
            c64_wr(load + k, os88_peek(sseg, soff + k));
    } else {
        c64_zzcopy_in(load, sseg, soff, n);
    }
    c64_dirty_all();
}

/* ovl_load_ptrs - what BASIC's LOAD leaves behind in direct mode, whatever the
 * address (11.3): the KERNAL's end of the load at $AE/$AF, and BASIC's end of
 * program and start of variables and arrays, $2D-$32, at that end. A BASIC
 * program injected without them has its variables written over its own
 * text; one at $C000 wants a NEW before BASIC is used again, as on the
 * machine. $2B/$2C, the start of BASIC, is left alone: LOAD does not move it */
static void ovl_load_ptrs(unsigned end)
{
    int i;

    c64_wr(0xAE, end & 0xFF);
    c64_wr(0xAF, end >> 8);
    for (i = 0x2D; i <= 0x31; i += 2) {
        c64_wr(i, end & 0xFF);
        c64_wr(i + 1, end >> 8);
    }
}

/* ==========================================================================
 * THE BUFFER A LOAD IS READ THROUGH (C64-SPEC 11.3)
 *
 * NOT A CLAIM THE SIZE OF THE FILE. A package holds at most eight claims
 * (SPEC.md 50.3, MEM_OWNER_MAX) and with the recompiler the C64 holds all
 * eight, so a load that claims fails on exactly the machine that runs the
 * program fastest - it did, "no heap for it" on every Smart attach of a PRG
 * or a T64 with the recompiler on. So the file is STREAMED through one
 * buffer, a cluster-aligned chunk at a time (c64_read_at's offset and length
 * are whole clusters), and each chunk's share is landed at once: with the
 * recompiler that buffer is its cache, 4KB, and every translation is thrown
 * away after as a flush (the D64's way, c64disk.c); without it a claim of
 * 8KB, or down to one 512-byte sector.
 *
 * The chunk last read is remembered, so the header a load inspects first is
 * not read twice when its data starts in the same chunk.
 * ========================================================================*/
static unsigned c64_ldbuf, c64_ldck;        /* the buffer and its size */
static unsigned c64_ldlo, c64_ldhi, c64_ldgot;  /* what is in it (got 0: none) */

static int ovl_buf_get(void)
{
    unsigned ck;

    c64_ldgot = 0;
    if (c64_fjit) {
        c64_ldbuf = c64_fjit + C64J_CACHE_PARA;
        c64_ldck = 0x1000;
        return 1;
    }
    c64_ldbuf = 0;
    for (ck = 0x2000; ck >= 0x200 && c64_ldbuf == 0; ck >>= 1)
        c64_ldbuf = os88_mem_claim_p(ck >> 4);
    c64_ldck = ck << 1;
    return c64_ldbuf != 0;
}

static void ovl_buf_put(void)
{
    if (c64_fjit)
        os88_poke(c64_fjit, C64J_FLUSHREQ, 2);  /* (the cache was the read's) */
    else if (c64_ldbuf)
        os88_mem_free(c64_ldbuf);
    c64_ldbuf = 0;
}

/* ovl_buf_read - the chunk at file offset HI:LO (a multiple of the chunk)
 * into the buffer, unless it is there already: the bytes it holds */
static unsigned ovl_buf_read(const char *name, unsigned lo, unsigned hi)
{
    if (c64_ldgot == 0 || lo != c64_ldlo || hi != c64_ldhi) {
        c64_ldgot = c64_read_at(name, c64_ldbuf, c64_ldck, lo, hi);
        c64_ldlo = lo;
        c64_ldhi = hi;
    }
    return c64_ldgot;
}

/* ovl_stream - the file's bytes [HI:LO, +LEN) into the C64's RAM at DEST
 * (DEST + LEN no further than $10000): 1 done, 0 a read came up short */
static int ovl_stream(const char *name, unsigned lo, unsigned hi,
                      unsigned len, unsigned dest)
{
    unsigned rlo, k, n;

    rlo = lo & (0u - c64_ldck);             /* (the chunk is a power of two) */
    k = lo - rlo;
    while (len != 0) {
        n = c64_ldck - k;
        if (n > len)
            n = len;
        if (ovl_buf_read(name, rlo, hi) < k + n)
            return 0;
        ovl_land(dest, c64_ldbuf + (k >> 4), k & 15, n);
        dest += n;
        len -= n;
        k = 0;
        rlo += c64_ldck;
        if (rlo == 0)
            hi++;
    }
    return 1;
}

/* IT ANSWERS NOTHING. Every refusal it has of its own - too short, no heap,
 * a short read, an end past $FFFF - is said on the status row (9.8's
 * both-routes rule, through c64_say), and the module it runs in is an EAGER
 * part that was in memory before os88_main ran (C64-SPEC 13.3), so there is
 * no runtime refusal left for a caller to read either. */
static void ovl_load_prg(const char *name, unsigned size_lo)
{
    char *msg = c64_msg;                    /* (built in place: c64_say's
                                             * copy onto itself is a no-op) */
    const char *why;
    unsigned load, n;

    if (size_lo < 3) {
        c64_say("PRG too short.");
        return;
    }
    why = "PRG: no heap for it.";
    if (!ovl_buf_get())
        goto out;
    why = "PRG: cannot read it.";
    if (ovl_buf_read(name, 0, 0) < 2)
        goto out;
    /* the two-byte load address, little-endian, and the refusal 11.3 states:
     * a file whose end passes $FFFF is refused with the fact */
    load = (unsigned)os88_peek(c64_ldbuf, 0)
         | ((unsigned)os88_peek(c64_ldbuf, 1) << 8);
    n = size_lo - 2;
    /* ...AND THE END TEST IS ON THE LAST BYTE, NOT ON ONE PAST IT:
     * `n - 1 > 0xFFFF - load` is exact in 16 bits, and a PRG whose last byte
     * is $FFFF is legal. n is at least 1 (size_lo >= 3 above) */
    if ((unsigned)(n - 1) > (unsigned)(0xFFFFu - load)) {
        why = "PRG runs past $FFFF.";
        goto out;
    }
    if (!ovl_stream(name, 2, 0, n, load))
        goto out;
    ovl_load_ptrs(load + n);
    os88_strcpy(msg, "Loaded ", C64_MSGMAX);
    os88_strcpy(msg + os88_strlen(msg), name, 14);
    why = msg;
    c64_ld_ok = 1;
out:
    ovl_buf_put();
    c64_say(why);
}

/* ==========================================================================
 * A TAPE IMAGE - .T64 (C64-SPEC 11.3.1)
 *
 * The container VICE reads in src/tape/t64.c: a 64-byte header that starts
 * "C64" (the magic VICE accepts is "C64" at offset 0 - "C64 tape image file"
 * and "C64S tape file" both), its used-entry count at $24, and from $40 a
 * 32-byte directory entry per file: [0] the entry type (0 free), [1] the C64
 * file type, [2..3] the start address, [4..5] the end, [8..11] the offset of
 * its data in the image, [16..31] its name. There is no tape here (11.2),
 * so the image is not mounted: its FIRST file is loaded where it says, as
 * VICE's autostart of a tape image loads it, and BASIC's LOAD pointers set.
 *
 * THE END ADDRESS IS NOT TRUSTED. Many images in circulation carry a wrong
 * one (VICE's t64.c "fixes" it): the length is the distance to the next
 * file's data in the image, or to the image's end, and the end field only
 * narrows it.
 *
 * The image is streamed like a PRG (above): the directory is the buffer's
 * first chunk - 4KB, 126 entries, with the recompiler; as few as 14 when the
 * heap gave only a sector, whose bound on the length is then the image's end
 * and the end field - and the file is landed out of the chunks it spans.
 * ========================================================================*/

static void ovl_load_t64(const char *name, unsigned size_lo, unsigned size_hi)
{
    char *msg = c64_msg;                    /* (built in place: c64_say's
                                             * copy onto itself is a no-op) */
    const char *why;
    unsigned dir, got, i, n, used, start, end, olo, ohi;
    unsigned nlo, nhi, len, k, e;

    why = "T64: no heap for it.";
    if (!ovl_buf_get())
        goto out;
    dir = c64_ldbuf;
    why = "Not a T64 tape image.";
    got = ovl_buf_read(name, 0, 0);
    if (got < 0x60 || os88_peek(dir, 0) != 'C' || os88_peek(dir, 1) != '6'
            || os88_peek(dir, 2) != '4')
        goto out;
    /* the first entry in use, of the directory's slots that were read */
    why = "T64: no program in it.";
    n = (got - 0x40) >> 5;
    e = 0xFFFF;
    for (i = 0; i < n && e == 0xFFFF; i++)
        if (os88_peek(dir, 0x40 + (i << 5)) != 0)
            e = i;
    if (e == 0xFFFF)
        goto out;
    k = 0x40 + (e << 5);
    start = (unsigned)os88_peek(dir, k + 2) | ((unsigned)os88_peek(dir, k + 3) << 8);
    end = (unsigned)os88_peek(dir, k + 4) | ((unsigned)os88_peek(dir, k + 5) << 8);
    olo = (unsigned)os88_peek(dir, k + 8) | ((unsigned)os88_peek(dir, k + 9) << 8);
    ohi = (unsigned)os88_peek(dir, k + 10) | ((unsigned)os88_peek(dir, k + 11) << 8);
    /* THE LENGTH: to the nearest data after this file's - another entry's,
     * or the image's end */
    nlo = size_lo;
    nhi = size_hi;
    for (i = 0; i < n; i++) {
        unsigned q = 0x40 + (i << 5), xlo, xhi;
        if (i == e || os88_peek(dir, q) == 0)
            continue;
        xlo = (unsigned)os88_peek(dir, q + 8) | ((unsigned)os88_peek(dir, q + 9) << 8);
        xhi = (unsigned)os88_peek(dir, q + 10) | ((unsigned)os88_peek(dir, q + 11) << 8);
        if (xhi > ohi || (xhi == ohi && xlo > olo))
            if (xhi < nhi || (xhi == nhi && xlo < nlo)) {
                nlo = xlo;
                nhi = xhi;
            }
    }
    os88_strcpy(msg, "Loaded ", C64_MSGMAX);
    used = os88_strlen(msg);
    for (i = 0; i < 16; i++) {              /* the file's name, PETSCII's
                                             * printable half as it stands */
        int c = os88_peek(dir, k + 16 + i);
        if (c == 0xA0)
            c = ' ';
        if (c < 0x20 || c > 0x5F)
            c = '?';
        msg[used + i] = (char)c;
    }
    i = 16;
    while (i > 0 && msg[used + i - 1] == ' ')
        i--;
    msg[used + i] = 0;
    if (nhi < ohi || (nhi == ohi && nlo <= olo))
        goto out;                           /* (its data: none) */
    if (nhi - ohi - (nlo < olo) != 0)
        len = 0xFFFFu;                      /* (64KB and more: capped below) */
    else
        len = nlo - olo;
    if (end > start && (unsigned)(end - start) < len)
        len = end - start;                  /* the end field narrows it */
    if ((unsigned)(len - 1) > (unsigned)(0xFFFFu - start))
        len = 0u - start;                   /* no further than $FFFF (start > 0 here) */
    why = "T64: cannot read it.";
    if (!ovl_stream(name, olo, ohi, len, start))
        goto out;
    ovl_load_ptrs(start + len);
    why = msg;
    c64_ld_ok = 1;
out:
    ovl_buf_put();
    c64_say(why);
}

/* ovl_attach - Smart attach's routing for anything that is not a D64 (which
 * os88_onfile tells by its size): a tape image by its name, else a .PRG -
 * here and not in the resident callback, which is a near call away */
static int ovl_is_t64(const char *name)
{
    int n = (int)os88_strlen(name);

    if (n < 4 || name[n - 4] != '.')
        return 0;
    return ((name[n - 3] | 0x20) == 't') & (name[n - 2] == '6')
         & (name[n - 1] == '4');
}

static void ovl_attach(const char *name, unsigned size_lo, unsigned size_hi)
{
    if (ovl_is_t64(name)) {                 /* (11.3.1: any size) */
        ovl_load_t64(name, size_lo, size_hi);
        return;
    }
    if (size_hi != 0 || size_lo > 0xFFFDu) {
        c64_say("PRG over 65533 bytes.");
        return;
    }
    ovl_load_prg(name, size_lo);
}

/* ==========================================================================
 * AUTOSTART (C64-SPEC 11.3.2)
 *
 * What Smart attach does after the load, as VICE's RAM-injection autostart
 * does it (src/autostart.c, AutostartPrgMode 1): with the machine at BASIC's
 * READY. prompt, a .PRG or a tape's first file is injected and RUN typed; a
 * D64 is mounted and LOAD"*",8,1 typed, then RUN once the load has said
 * READY. again. A machine NOT at the prompt is given a CPU reset first (RAM
 * kept, as VICE's autostart resets), and waited for, in warp - VICE's
 * AutostartWarp - so the KERNAL's RAM test costs a moment, not three
 * seconds. File > Attach disk image... mounts and starts nothing.
 *
 * THE PROMPT is VICE's own test (autostart.c's check()): the cursor at
 * column 0, nothing in the keyboard buffer, and READY. in the screen codes
 * of the line above the cursor, on the screen the KERNAL writes ($0288).
 * The first frames after a reset are not looked at: until RAMTAS clears it,
 * zero page still describes the machine before the reset.
 *
 * THE TYPING goes into the KERNAL's own ten-byte buffer at $0277 with the
 * count at $C6, as VICE's kbdbuf does, ten bytes a wake while the KERNAL
 * drains it - LOAD"*",8,1 and its RETURN are twelve.
 * ========================================================================*/
#define C64_AU_WAIT 1                       /* for READY., then the load */
#define C64_AU_LOAD 2                       /* typing LOAD; then READY., RUN */
#define C64_AU_RUN  3                       /* typing RUN; then done */
#define C64_AU_LATE 1000                    /* frames: 20 s emulated */

static char c64_au_name[16];                /* the file, for the load after
                                             * the reset */
static unsigned c64_au_lo, c64_au_hi, c64_au_fr;
static int c64_au_d64, c64_au_warp;
static const char *c64_au_s;                /* what is left to type */
static unsigned char c64_au_ready[6] = { 18, 5, 1, 4, 25, 46 };  /* READY. */

static int ovl_at_ready(void)
{
    unsigned row, a;
    int i;

    if (c64_rd(0xC6) | c64_rd(0xD3))        /* keys pending, or mid-line */
        return 0;
    row = (unsigned)c64_rd(0xD6);
    if (row == 0 || row > 24)
        return 0;
    a = ((unsigned)c64_rd(0x288) << 8) + (row - 1) * 40;
    for (i = 0; i < 6; i++)
        if (c64_rd(a + i) != c64_au_ready[i])
            return 0;
    return 1;
}

/* the warp an autostart's wait runs in, entered and left as Alt+W does */
static void ovl_au_warp(int on)
{
    if (on) {
        c64_au_warp = c64_warp ^ 1;         /* (ours to take back only if it
                                             * was not the user's already) */
        if (c64_au_warp) {
            c64_warp = 1;
            c64_sound_stop();
        }
    } else if (c64_au_warp) {
        c64_au_warp = 0;
        c64_warp = 0;
        if (c64_budget > C64_SLICE_MAX)
            c64_budget = C64_SLICE_MAX;
    }
    c64_menu_state();
}

static void ovl_au_end(void)
{
    c64_auto = 0;
    c64_au_s = 0;
    ovl_au_warp(0);
}

/* the next stage, with the machine at the prompt */
static void ovl_au_go(void)
{
    c64_au_fr = c64_frames_lo;
    if (c64_auto == C64_AU_WAIT && !c64_au_d64) {
        c64_ld_ok = 0;
        ovl_attach(c64_au_name, c64_au_lo, c64_au_hi);
        if (!c64_ld_ok) {
            ovl_au_end();                   /* (the refusal is on the row) */
            return;
        }
    }
    ovl_au_warp(0);                         /* the program runs at its speed */
    if (c64_auto == C64_AU_WAIT && c64_au_d64) {
        c64_au_s = "LOAD\"*\",8,1\r";
        c64_auto = C64_AU_LOAD;
    } else {
        c64_au_s = "RUN\r";
        c64_auto = C64_AU_RUN;
    }
}

/* ovl_auto_begin - from Smart attach, the file already refused or (for a
 * D64) mounted: start it now if the machine is at the prompt, else reset
 * and wait for it */
static void ovl_auto_begin(const char *name, unsigned lo, unsigned hi, int d64)
{
    os88_strcpy(c64_au_name, name, sizeof(c64_au_name));
    c64_au_lo = lo;
    c64_au_hi = hi;
    c64_au_d64 = d64;
    c64_au_s = 0;
    c64_paste_stop();                       /* (the buffer is the autostart's) */
    c64_auto = C64_AU_WAIT;
    if (c64_state == C64_ST_RUN && !c64_pause && ovl_at_ready()) {
        ovl_au_go();
        return;
    }
    c64_reset_req = 1;                      /* File > Reset machine CPU's */
    c64_au_fr = c64_frames_lo;
    ovl_au_warp(1);
    c64_say("Autostarting...");
}

/* ovl_auto_step - once a wake, before the slice, with the machine running */
static void ovl_auto_step(void)
{
    int n;

    if (c64_au_s != 0) {
        if (*c64_au_s != 0) {
            if (c64_rd(0xC6) != 0)
                return;                     /* the KERNAL has not drained it */
            for (n = 0; n < 10 && *c64_au_s != 0; n++)
                c64_wr(0x277 + n, *c64_au_s++);
            c64_wr(0xC6, n);
            c64_au_fr = c64_frames_lo;
            return;
        }
        c64_au_s = 0;
        if (c64_auto == C64_AU_RUN) {
            ovl_au_end();
            return;
        }
    }
    n = (int)((c64_frames_lo - c64_au_fr) & 0xFFFFu);
    if (n > C64_AU_LATE) {
        ovl_au_end();
        c64_say("Autostart: no READY. prompt.");
        return;
    }
    if (n >= 5 && ovl_at_ready())
        ovl_au_go();
}
