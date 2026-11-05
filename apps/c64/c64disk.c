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
 * The image is held whole while it is attached: 171KB for 35 tracks (683
 * sectors), read once and given back when another is attached. IN EXTENDED
 * MEMORY where there is room (C64-SPEC 11.5.1) - read 8KB at a time through
 * a buffer given back after, and a sector at a time copied into one of two
 * 256-byte slots when it is asked for - so a 640KB machine keeps its
 * conventional memory for the recompiler. Otherwise in one conventional
 * claim, 32KB at a time (c64_read_at).
 * ==========================================================================*/

#define C64_D64_TRAP     0xF4C4             /* LDA #$60, the serial LOAD's first
                                             * instruction after SEARCHING FOR
                                             * (JSR $F5AF at $F4C1) */
#define C64_D64_TRAPOFF  0x14C4             /* ...its offset in the ROM part,
                                             * whose first 8KB is the KERNAL */
#define C64_D64_LOADING  0xF5D2             /* LOADING / VERIFYING, then RTS */
#define C64_D64_EXIT     0xF5A9             /* CLC / LDX $AE / LDY $AF / RTS */
#define C64_D64_NOTFOUND 0xF704             /* LDA #4: ?FILE NOT FOUND */

static unsigned c64_d64_seg;                /* the image's claim, or the
                                             * slots' when it is extended;
                                             * 0 = no disk */
static unsigned c64_d64_trks;               /* 35 or 40 */
static unsigned c64_d64_xms;                /* 1: the image is in extended
                                             * memory, its token... */
static unsigned c64_d64_tok[2];
static unsigned c64_d64_inj;                /* ...and its slots are in the
                                             * recompiler's segment, which
                                             * owns them (11.5.1) */
static unsigned c64_d64_slot[2];            /* ...and the sector + 1 each
                                             * slot holds: 0 the others, 1
                                             * the BAM (18,0), which the
                                             * directory reads beside them */

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
    unsigned i, k;

    if (t < 1 || t > c64_d64_trks)
        return 0;
    if (s >= c64_d64_trk0[t + 1] - c64_d64_trk0[t])
        return 0;
    i = c64_d64_trk0[t] + s;
    if (c64_d64_xms == 0)
        return c64_d64_seg + (i << 4);
    k = (t == 18 && s == 0) ? 1 : 0;
    if (c64_d64_slot[k] != i + 1) {
        if (!c64x_copy(c64_d64_seg, k << 8, c64_d64_tok[0], c64_d64_tok[1],
                       (i & 0xFF) << 8, i >> 8, 256, 1))
            return 0;
        c64_d64_slot[k] = i + 1;
    }
    return c64_d64_seg + (k << 4);
}

/* ovl_d64_release - the image attached now, given back */
static void ovl_d64_release(void)
{
    if (c64_d64_seg != 0 && !c64_d64_inj)
        os88_mem_free(c64_d64_seg);
    c64_d64_inj = 0;
    if (c64_d64_xms)
        c64x_free(c64_d64_tok[0], c64_d64_tok[1]);
    c64_d64_seg = 0;
    c64_d64_xms = 0;
}

/* ovl_d64_xattach - the image into extended memory: 1 done, 0 not (no pool,
 * no room in it, no buffer, or a read that failed - the caller then tries
 * the conventional way, which says what went wrong) */
static int ovl_d64_xattach(const char *name, unsigned trks)
{
    unsigned nch, k, n, per, ck, stage, tmp, inj;
    int ok;

    nch = (trks == 35) ? 22 : 24;           /* 8KB chunks: 174,848, 196,608 */
    if (c64x_caps() < nch * 8)
        return 0;
    inj = (c64_fjit != 0);
    if (inj) {
        /* IN THE RECOMPILER'S SEGMENT. A package holds at most eight claims
         * (SPEC.md 50.3, MEM_OWNER_MAX) and with the recompiler the C64
         * holds all eight, so the two slots are the last 512 bytes of its
         * claim, which it leaves alone for them (c64jit.asm's jv_end), and
         * the image is read through its cache, 4KB at a time - every block
         * is thrown away after, as a flush */
        stage = c64_fjit + c64_jit_kb * 64 - 32;
        tmp = c64_fjit + C64J_CACHE_PARA;
        ck = 0x1000;
    } else {
        stage = os88_mem_claim_p(32);       /* the two sector slots */
        if (stage == 0)
            return 0;
        /* A BUFFER TO READ THROUGH, as big as the heap has: 8KB, or down
         * to one 512-byte sector - a smaller one is only more reads */
        tmp = 0;
        for (ck = 0x2000; ck >= 0x200 && tmp == 0; ck >>= 1)
            tmp = os88_mem_claim_p(ck >> 4);
        ck <<= 1;
        if (tmp == 0) {
            os88_mem_free(stage);
            return 0;
        }
    }
    if (!c64x_alloc((nch & 7) << 13, nch >> 3)) {
        if (!inj) {
            os88_mem_free(tmp);
            os88_mem_free(stage);
        }
        return 0;
    }
    ok = 1;
    /* THE CHUNKS THE SECTORS FILL: a read wholly past the image's end
     * answers nothing, and the block is whole 8KBs, so stop where the
     * sectors stop (an error-byte tail after them is not read) */
    n = ((trks == 35 ? 683 : 768) + (ck >> 8) - 1) / (ck >> 8);
    per = (0x8000u / ck) * 2;               /* chunks a 64KB holds */
    for (k = 0; k < n && ok; k++) {
        if (c64_read_at(name, tmp, ck, (k % per) * ck, k / per) == 0)
            ok = 0;
        else if (!c64x_copy(tmp, 0, c64_xnew[0], c64_xnew[1],
                            (k % per) * ck, k / per, ck, 0))
            ok = 0;
    }
    if (inj)
        os88_poke(c64_fjit, C64J_FLUSHREQ, 2);  /* (the cache was the read's) */
    else
        os88_mem_free(tmp);
    if (!ok) {
        c64x_free(c64_xnew[0], c64_xnew[1]);
        if (!inj)
            os88_mem_free(stage);
        return 0;
    }
    ovl_d64_release();
    c64_d64_seg = stage;
    c64_d64_inj = inj;
    c64_d64_xms = 1;
    c64_d64_tok[0] = c64_xnew[0];
    c64_d64_tok[1] = c64_xnew[1];
    c64_d64_slot[0] = 0;
    c64_d64_slot[1] = 0;
    c64_d64_trks = trks;
    if (!inj)                               /* THE SLOTS MAY MOVE (SPEC.md 66):
                                             * held for as long as the disk is,
                                             * read only through c64_d64_seg,
                                             * which os88_onmove follows */
        os88_mem_movable(stage, 1);
    return 1;
}

/* ovl_d64_attach - File > Attach disk image / Smart attach on a .D64 (a
 * tape image of a D64's size is told by its name in ovl_open): the
 * image into a claim of its own, the old one given back. Refusals are said on
 * the status row, and the drive keeps what it had until a new image is whole */
static void ovl_d64_attach(const char *name, unsigned size_lo, unsigned size_hi)
{
    unsigned paras, seg, i, trks, got;
    int gave;

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
    /* EXTENDED FIRST. It wants only a few hundred bytes of conventional
     * memory, and beside a 64KB recompiler a 640KB machine may not have
     * those: then the recompiler steps aside for the attach and comes back
     * whole, the image being above 1MB */
    gave = ovl_d64_xattach(name, trks);
    if (!gave && c64_fjit != 0 && c64x_caps() >= 192) {
        c64_jit_drop();
        gave = ovl_d64_xattach(name, trks);
        ovl_jit_init();                     /* back - whole if the image went
                                             * up, and the conventional way
                                             * below asks again if not */
    }
    if (gave) {
        c64_menu_state();
        c64_ld_ok = 1;
        c64_say("Disk on drive 8.");
        return;
    }
    /* the sectors, and 512 bytes over: the last READ_AT moves whole sectors
     * and the image does not end on one */
    paras = (trks == 35 ? 683 : 768) * 16 + 32;
    gave = 0;
    for (;;) {                              /* (ONE claim site, which the pin
                                             * gate follows to its movable
                                             * declaration below) */
        seg = os88_mem_claim_p(paras);
        if (seg != 0 || c64_fjit == 0)
            break;
        c64_jit_drop();                     /* (the recompiler is optional) */
        gave = 1;
    }
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
    ovl_d64_release();
    c64_d64_seg = seg;
    c64_d64_trks = trks;
    os88_mem_movable(seg, 1);               /* (the reads into it are done:
                                             * from here only c64_d64_seg names
                                             * it, which os88_onmove follows) */
    ovl_jit_init();                         /* (back, smaller, if it gave way) */
    c64_menu_state();
    if (gave && c64_fjit == 0) {
        c64_ld_ok = 1;
        c64_say("Disk on drive 8; no room for recompiler.");
        return;
    }
    c64_ld_ok = 1;
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

/* ovl_open - os88_onfile's whole body (11.3): a D64 by its SIZE - the dialog
 * does not filter by extension (SPEC.md 38.9), and a .PRG of over 64KB is
 * refused anyway - onto drive 8, anything else a tape image or a .PRG; and
 * from Smart attach, the autostart (11.3.2). A .PRG or tape is loaded by the
 * autostart itself, at the prompt; its refusals by size come first, so a
 * file that cannot load does not reset the machine */
static void ovl_open(const char *name, unsigned size_lo, unsigned size_hi)
{
    int d64 = (size_hi == 2 || size_hi == 3) && !ovl_is_t64(name);

    if (c64_auto)
        ovl_au_end();                       /* a new attach replaces it */
    c64_ld_ok = 0;
    if (d64) {
        ovl_d64_attach(name, size_lo, size_hi);
        if (c64_smart && c64_ld_ok)
            ovl_auto_begin(name, size_lo, size_hi, 1);
        return;
    }
    if (!c64_smart || (!ovl_is_t64(name)
            && (size_hi != 0 || size_lo > 0xFFFDu || size_lo < 3))) {
        ovl_attach(name, size_lo, size_hi); /* (which says why, if it is
                                             * refused) */
        return;
    }
    ovl_auto_begin(name, size_lo, size_hi, 0);
}
