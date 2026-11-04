/* ============================================================================
 * os8088 - apps/c64/c64disk.c        Drive 8: a D64 image, and the KERNAL's
 *                                    LOAD served from it (C64-SPEC 11.5)
 *
 * Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * NOT A 1541. There is no drive CPU, no GCR and no serial bus here: the
 * KERNAL's own LOAD is trapped where its serial half begins (C64_D64_TRAP,
 * just after it has printed SEARCHING FOR), the file is found in the image
 * and copied into RAM by the C, and the machine goes on through the KERNAL's
 * own LOADING message and its own exit - so BASIC sees exactly what a load
 * from drive 8 leaves it. What this cannot run is anything that talks to the
 * drive itself: a fast loader, an OPEN of the drive's channels, a program
 * that reads its disk sector by sector. Those see the bus they always saw:
 * nothing on it, `?DEVICE NOT PRESENT`.
 *
 * The image is held whole, in one claim, while it is attached: 171KB for 35
 * tracks (683 sectors), read once, 32KB at a time (c64_read_at), and given
 * back when another is attached.
 * ==========================================================================*/

#define C64_D64_TRAP     0xF4C4             /* LDA #$60, the serial LOAD's first
                                             * instruction after SEARCHING FOR
                                             * (JSR $F5AF at $F4C1) */
#define C64_D64_TRAPOFF  0x14C4             /* ...its offset in the ROM part,
                                             * whose first 8KB is the KERNAL */
#define C64_D64_LOADING  0xF5D2             /* LOADING / VERIFYING, then RTS */
#define C64_D64_EXIT     0xF5A9             /* CLC / LDX $AE / LDY $AF / RTS */
#define C64_D64_NOTFOUND 0xF704             /* LDA #4: ?FILE NOT FOUND */

static unsigned c64_d64_seg;                /* the image's claim; 0 = no disk */
static unsigned c64_d64_trks;               /* 35 or 40 */

/* ovl_d64_para - the paragraph of track t, sector s, inside the claim; 0 for
 * a sector the image has not got (a link off the disk is a broken file) */
/* the first sector of each track, 1..41 (the 41st is 40's end), in the
 * 1541's zones: 21 sectors to track 17, 19 to 24, 18 to 30, 17 after */
static const unsigned c64_d64_trk0[42] = {
    0,
    0, 21, 42, 63, 84, 105, 126, 147,
    168, 189, 210, 231, 252, 273, 294, 315,
    336, 357, 376, 395, 414, 433, 452, 471,
    490, 508, 526, 544, 562, 580, 598, 615,
    632, 649, 666, 683, 700, 717, 734, 751,
    768
};

static unsigned ovl_d64_para(unsigned t, unsigned s)
{
    if (t < 1 || t > c64_d64_trks)
        return 0;
    if (s >= c64_d64_trk0[t + 1] - c64_d64_trk0[t])
        return 0;
    return c64_d64_seg + ((c64_d64_trk0[t] + s) << 4);
}

/* ovl_d64_attach - File > Attach disk image / Smart attach on a .D64: the
 * image into a claim of its own, the old one given back. Refusals are said on
 * the status row, and the drive keeps what it had until a new image is whole */
static void ovl_d64_attach(const char *name, unsigned size_lo, unsigned size_hi)
{
    unsigned paras, seg, i, trks, got;

    trks = 0;
    if (size_hi == 2)
        if ((size_lo == 0xAB00) | (size_lo == 0xADAB))
            trks = 35;                      /* 174,848, or with error bytes */
    if (size_hi == 3)
        if ((size_lo == 0x0000) | (size_lo == 0x0300))
            trks = 40;                      /* 196,608, or with error bytes */
    if (trks == 0) {
        c64_say("Not a D64 image.");
        return;
    }
    /* the sectors, and 512 bytes over: the last READ_AT moves whole sectors
     * and the image does not end on one */
    paras = (trks == 35 ? 683 : 768) * 16 + 32;
    seg = os88_mem_claim_p(paras);
    if (seg == 0) {
        c64_say("D64: no heap for it.");
        return;
    }
    for (i = 0; i < paras - 32; i += 2048) {    /* 32KB, a cluster multiple */
        got = c64_read_at(name, seg + i, 0x8000u, i << 4, i >> 12);
        if (got == 0) {
            os88_mem_free(seg);
            c64_say("D64: cannot read it.");
            return;
        }
    }
    if (c64_d64_seg != 0)
        os88_mem_free(c64_d64_seg);
    c64_d64_seg = seg;
    c64_d64_trks = trks;
    c64_say("Disk on drive 8.");
}

/* ovl_d64_match - the name a LOAD asked for (PETSCII, at most 16, `?` one
 * character and `*` the rest) against a directory entry's sixteen, which end
 * at their first $A0 */
static int ovl_d64_match(const unsigned char *want, unsigned wn,
                         unsigned para, unsigned off)
{
    unsigned i, c;

    for (i = 0; i < wn; i++) {
        if (want[i] == '*')
            return 1;
        c = (i < 16) ? (unsigned)os88_peek(para, off + i) : 0xA0;
        if (c == 0xA0)
            return 0;
        if (want[i] != '?')
            if (want[i] != c)
                return 0;
    }
    return wn >= 16 || os88_peek(para, off + wn) == 0xA0;
}

/* ovl_d64_put - one byte of a growing listing into RAM at *at */
static unsigned c64_d64_at;
static void ovl_d64_put(int v)
{
    c64_wr(c64_d64_at, v);
    c64_d64_at++;
}

/* ovl_d64_dir - LOAD"$",8: the directory as the BASIC program a 1541 sends,
 * at `at`. Answers the end address */
static unsigned ovl_d64_dir(unsigned at)
{
    unsigned bam, line, para, off, t, s, i, n, free, start, typ;
    static const char *types[] = { "DEL", "SEQ", "PRG", "USR", "REL" };

    c64_d64_at = at;
    bam = ovl_d64_para(18, 0);
    /* line 0: the header, reversed */
    start = c64_d64_at;
    ovl_d64_put(1);
    ovl_d64_put(1);                         /* (relinked below) */
    ovl_d64_put(0);
    ovl_d64_put(0);
    ovl_d64_put(0x12);
    ovl_d64_put('"');
    for (i = 0; i < 16; i++) {
        n = (unsigned)os88_peek(bam, 0x90 + i);
        ovl_d64_put(n == 0xA0 ? ' ' : (int)n);
    }
    ovl_d64_put('"');
    ovl_d64_put(' ');
    ovl_d64_put(os88_peek(bam, 0xA2));
    ovl_d64_put(os88_peek(bam, 0xA3));
    ovl_d64_put(' ');
    ovl_d64_put(os88_peek(bam, 0xA5));
    ovl_d64_put(os88_peek(bam, 0xA6));
    ovl_d64_put(0);
    c64_wr(start, c64_d64_at & 0xFF);
    c64_wr(start + 1, c64_d64_at >> 8);
    /* the files */
    t = 18;
    s = 1;
    for (line = 0; line < 20 && t != 0; line++) {   /* 18 sectors of track 18 */
        para = ovl_d64_para(t, s);
        if (para == 0)
            break;
        for (off = 0; off < 256; off += 32) {
            typ = (unsigned)os88_peek(para, off + 2);
            if ((typ & 7) == 0 && (typ & 0x80) == 0)
                continue;                   /* a scratched entry */
            n = (unsigned)os88_peek(para, off + 30)
              | ((unsigned)os88_peek(para, off + 31) << 8);
            start = c64_d64_at;
            ovl_d64_put(1);
            ovl_d64_put(1);
            ovl_d64_put(n & 0xFF);
            ovl_d64_put(n >> 8);
            if (n < 100)
                ovl_d64_put(' ');
            if (n < 10)
                ovl_d64_put(' ');
            ovl_d64_put('"');
            for (i = 0; i < 16; i++) {
                if (os88_peek(para, off + 5 + i) == 0xA0)
                    break;
                ovl_d64_put(os88_peek(para, off + 5 + i));
            }
            ovl_d64_put('"');
            for (; i < 17; i++)
                ovl_d64_put(' ');
            ovl_d64_put((typ & 0x80) ? ' ' : '*');
            for (i = 0; i < 3; i++)
                ovl_d64_put(types[(typ & 7) < 5 ? (typ & 7) : 0][i]);
            ovl_d64_put((typ & 0x40) ? '<' : ' ');
            ovl_d64_put(0);
            c64_wr(start, c64_d64_at & 0xFF);
            c64_wr(start + 1, c64_d64_at >> 8);
        }
        t = (unsigned)os88_peek(para, 0);
        s = (unsigned)os88_peek(para, 1);
    }
    /* BLOCKS FREE., counted out of the BAM (track 18 is not counted) */
    free = 0;
    for (t = 1; t <= 35; t++)
        if (t != 18)
            free += (unsigned)os88_peek(bam, 4 * t);
    start = c64_d64_at;
    ovl_d64_put(1);
    ovl_d64_put(1);
    ovl_d64_put(free & 0xFF);
    ovl_d64_put(free >> 8);
    for (i = 0; i < 12; i++)
        ovl_d64_put("BLOCKS FREE."[i]);
    for (i = 0; i < 13; i++)
        ovl_d64_put(' ');
    ovl_d64_put(0);
    c64_wr(start, c64_d64_at & 0xFF);
    c64_wr(start + 1, c64_d64_at >> 8);
    ovl_d64_put(0);
    ovl_d64_put(0);
    return c64_d64_at;
}

/* ovl_d64_trap - the KERNAL has stopped on the JAM byte at C64_D64_TRAP.
 * Drive 8 with a disk: the LOAD, here. Anything else: the one instruction
 * the byte replaced, and the KERNAL goes on to its serial bus as before */
static void ovl_d64_trap(void)
{
    static unsigned char want[16];
    unsigned wn, ptr, i, sa, verify, at, t, s, para, off, typ, n, st, end, sp;
    int found;

    if (c64_rd(0xBA) != 8 || c64_d64_seg == 0) {
        c64_m.a = 0x60;                     /* LDA #$60: N and Z clear */
        c64_m.p &= ~0x82u;
        c64_m.pc = C64_D64_TRAP + 2;
        return;
    }
    wn = (unsigned)c64_rd(0xB7);
    ptr = (unsigned)c64_rd16(0xBB);
    if (wn > 16)
        wn = 16;
    for (i = 0; i < wn; i++)
        want[i] = (unsigned char)c64_rd(ptr + i);
    /* "0:NAME" and ":NAME" name drive 0 */
    for (i = 0; i < wn; i++)
        if (want[i] == ':') {
            os88_memcpy(want, want + i + 1, wn - i - 1);
            wn -= i + 1;
            break;
        }
    sa = (unsigned)c64_rd(0xB9);
    verify = (unsigned)c64_rd(0x93);
    st = 0;
    if (wn == 1 && want[0] == '$') {
        at = (sa != 0) ? 0x0401 : ((unsigned)c64_rd(0xC3) | ((unsigned)c64_rd(0xC4) << 8));
        end = ovl_d64_dir(at);
        found = 1;
    } else {
        found = 0;
        t = 18;
        s = 1;
        para = 0;
        off = 0;
        for (n = 0; n < 20 && t != 0 && !found; n++) {
            para = ovl_d64_para(t, s);
            if (para == 0)
                break;
            for (off = 0; off < 256; off += 32) {
                typ = (unsigned)os88_peek(para, off + 2);
                if ((typ & 7) == 2)         /* a PRG, closed or not */
                    if (ovl_d64_match(want, wn, para, off + 5)) {
                        found = 1;
                        break;
                    }
            }
            if (!found) {
                t = (unsigned)os88_peek(para, 0);
                s = (unsigned)os88_peek(para, 1);
            }
        }
        if (!found) {
            c64_m.pc = C64_D64_NOTFOUND;
            return;
        }
        /* the file's chain: two link bytes, then 254 of data - the first two
         * of the first sector the load address */
        t = (unsigned)os88_peek(para, off + 3);
        s = (unsigned)os88_peek(para, off + 4);
        para = ovl_d64_para(t, s);
        if (para == 0) {
            c64_m.pc = C64_D64_NOTFOUND;
            return;
        }
        if (sa != 0)
            at = (unsigned)os88_peek(para, 2) | ((unsigned)os88_peek(para, 3) << 8);
        else
            at = (unsigned)c64_rd(0xC3) | ((unsigned)c64_rd(0xC4) << 8);
        off = 4;                            /* past the load address */
        end = at;
        for (n = 0; n < 800 && para != 0; n++) {
            t = (unsigned)os88_peek(para, 0);
            s = (unsigned)os88_peek(para, 1);
            i = (t == 0) ? s + 1 : 256;     /* the last sector says its end */
            if (i > off) {
                if (verify) {
                    for (; off < i; off++, end++)
                        if (c64_rd(end) != os88_peek(para, off))
                            st |= 0x10;
                } else {
                    c64_zzcopy_in(end, para, off, i - off);
                    end += i - off;
                }
            }
            if (t == 0)
                break;
            para = ovl_d64_para(t, s);
            off = 2;
        }
    }
    c64_dirty_all();                        /* (a block move marks no page) */
    /* what the KERNAL's own serial loop leaves: the end in $AE/$AF and the
     * status in $90 (EOF; and $10 on a VERIFY that differed) */
    c64_wr(0xAE, end & 0xFF);
    c64_wr(0xAF, end >> 8);
    c64_wr(0x90, 0x40 | st);
    /* ...then the KERNAL says LOADING (or VERIFYING) and leaves through its
     * own exit: $F5D2 returns into $F5A9 off a return address pushed here */
    sp = c64_m.s & 0xFF;
    c64_wr(0x100 + sp, (C64_D64_EXIT - 1) >> 8);
    c64_wr(0x100 + ((sp - 1) & 0xFF), (C64_D64_EXIT - 1) & 0xFF);
    c64_m.s = (sp - 2) & 0xFF;
    c64_m.pc = C64_D64_LOADING;
}
