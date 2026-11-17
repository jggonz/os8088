/* ============================================================================
 * os8088 - apps/pagedesk/pdxfz.c      PDXF: a Flate stream inflated in place
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8, "Streams without a Flate slot". PDENG decodes a Flate stream
 * in a Flate slot, and a machine that has ONE slot gives it to the page's
 * content stream while PDREND interprets it: a font module's STRM_OPEN of a
 * second Flate stream then answers PDE_LIMIT (pdeflate.c fl_attach). Every
 * Quartz, Word and MuPDF page is a Flate content stream and every embedded
 * font program a Flate stream, so on such a machine no embedded font drew
 * (QA R13, F19). Here the stream's bytes are copied with RAW_COPY PDRC_PLAIN
 * (decrypted, PDENG's own hook) to the TOP of a region of the workspace and
 * inflated from there into its BOTTOM. The decoder needs no window - the
 * output is whole in the region, so a match is a copy within it - and the
 * input is read through a 512-byte near buffer, so output may overwrite any
 * input byte already taken. Output that would reach unread input, or pass the
 * region, is "too large" (PDFE_SIZE, the workspace growth of 20.22.8).
 * The decoder is puff's (zlib's contrib/puff, Mark Adler): canonical Huffman,
 * one bit at a time - small, and run once per font activation.
 * ==========================================================================*/
/* g_zseg .. g_zerr are declared in pdxf.c, above pdx_sopen */
/* the near input buffer and the literal/length tables borrow the TrueType glyph's point
 * arrays (pdxfsf.c): a font activates between glyphs, never inside one */
#define g_zbuf  g_gpf
#define g_zlsym ((pdw *)g_gpx)
#define g_zlen  ((pdb *)g_gpy)
static pdw g_zbp, g_zbn, g_zbit, g_zbc;

static pdw z_byte(void)
{
    pdw n;
    if (g_zbp >= g_zbn) {
        n = (pdw)(g_zinend - g_zin);
        if (n > sizeof g_zbuf) n = sizeof g_zbuf;
        if (!n) { g_zerr = 1; return 0; }
        pd_fcopy_in(g_zseg, g_zin, g_zbuf, n);
        g_zin = (pdw)(g_zin + n); g_zbn = n; g_zbp = 0;
    }
    return g_zbuf[g_zbp++];
}
static pdw z_bits(pdw need)          /* need <= 13 */
{
    pdw v = 0, s = 0, k;
    while (need) {
        if (!g_zbc) { g_zbit = z_byte(); g_zbc = 8; }
        k = need < g_zbc ? need : g_zbc;
        v |= (pdw)((g_zbit & ((1u << k) - 1)) << s);
        g_zbit >>= k; g_zbc = (pdw)(g_zbc - k); need = (pdw)(need - k); s = (pdw)(s + k);
    }
    return v;
}
/* room for n more output bytes: below the region's end and below unread input */
static pdw z_room(pdw n)
{
    if ((pdw)(g_zlim - g_zout) < n || (pdw)(g_zin - g_zout) < n) { g_zmore = 1; return 0; }
    return 1;
}

/* Huffman tables: counts by length, symbols in canonical order */
static pdw g_zlcnt[16], g_zdcnt[16], g_zdsym[30];
static pdw g_zoffs[16];
static pdw z_build(pdw *cnt, pdw *sym, pdw from, pdw n)
{
    pdw i;
    pdi left;
    for (i = 0; i < 16; i++) cnt[i] = 0;
    for (i = 0; i < n; i++) cnt[g_zlen[from + i]]++;
    if (cnt[0] == n) return 0;
    left = 1;
    for (i = 1; i < 16; i++) { left = (pdi)(left << 1); left = (pdi)(left - (pdi)cnt[i]); if (left < 0) return 1; }
    g_zoffs[1] = 0;
    for (i = 1; i < 15; i++) g_zoffs[i + 1] = (pdw)(g_zoffs[i] + cnt[i]);
    for (i = 0; i < n; i++) if (g_zlen[from + i]) sym[g_zoffs[g_zlen[from + i]]++] = i;
    return 0;
}
static pdw z_dec(pdw *cnt, pdw *sym)
{
    pdw code = 0, first = 0, index = 0, len, c;
    for (len = 1; len < 16; len++) {
        if (!g_zbc) { g_zbit = z_byte(); g_zbc = 8; }
        code |= (pdw)(g_zbit & 1); g_zbit >>= 1; g_zbc--;
        c = cnt[len];
        if (code < (pdw)(first + c)) return sym[index + (code - first)];
        index = (pdw)(index + c); first = (pdw)((first + c) << 1); code <<= 1;
    }
    g_zerr = 1;
    return 0;
}
static const pdw z_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const pdb z_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const pdw z_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const pdb z_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const pdb z_clord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* one block's codes; 1 at its end, 0 when the output stops (g_zmore / g_zerr) */
static pdw z_codes(void)
{
    pdw s, len, dist, src, k;
    for (;;) {
        s = z_dec(g_zlcnt, g_zlsym);
        if (g_zerr) return 0;
        if (s < 256) {
            if (!z_room(1)) return 0;
            pd_fpokeb(g_zseg, g_zout, (pdb)s); g_zout++;
            continue;
        }
        if (s == 256) return 1;
        s = (pdw)(s - 257);
        if (s >= 29) { g_zerr = 1; return 0; }
        len = (pdw)(z_lbase[s] + z_bits(z_lext[s]));
        s = z_dec(g_zdcnt, g_zdsym);
        if (s >= 30) g_zerr = 1;
        if (g_zerr) return 0;
        dist = (pdw)(z_dbase[s] + z_bits(z_dext[s]));
        if (dist > (pdw)(g_zout - g_zstart)) { g_zerr = 1; return 0; }
        if (!z_room(len)) return 0;
        src = (pdw)(g_zout - dist);
        while (len) {                               /* the period's copies double: no overlap, phase kept */
            k = (pdw)(g_zout - src);
            if (k > len) k = len;
            pd_fcopy(g_zseg, g_zout, g_zseg, src, k);
            g_zout = (pdw)(g_zout + k); len = (pdw)(len - k);
        }
    }
}

/* the zlib stream at g_zin..g_zinend inflated to g_zout (whose start is the
 * region's): the bytes written; g_zmore when it did not all fit */
static pdw z_inflate(void)
{
    pdw last, type, i, nlen, ndist, ncode, s, rep, v;
    g_zbp = g_zbn = g_zbc = 0; g_zerr = 0; g_zmore = 0;
    if ((z_byte() & 15) != 8) return 0;             /* CMF: deflate */
    z_byte();                                       /* FLG */
    do {
        last = z_bits(1); type = z_bits(2);
        if (type == 0) {                            /* stored */
            g_zbc = 0;
            i = z_byte(); i |= (pdw)(z_byte() << 8);
            z_byte(); z_byte();
            while (i-- && !g_zerr) {
                v = z_byte();
                if (!z_room(1)) break;
                pd_fpokeb(g_zseg, g_zout, (pdb)v); g_zout++;
            }
            if (g_zmore || g_zerr) break;
            continue;
        }
        if (type == 1) {                            /* fixed */
            for (i = 0; i < 288; i++) g_zlen[i] = (pdb)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            for (i = 0; i < 30; i++) g_zlen[288 + i] = 5;
            z_build(g_zlcnt, g_zlsym, 0, 288); z_build(g_zdcnt, g_zdsym, 288, 30);
        } else if (type == 2) {                     /* dynamic */
            nlen = (pdw)(z_bits(5) + 257); ndist = (pdw)(z_bits(5) + 1); ncode = (pdw)(z_bits(4) + 4);
            if (nlen > 286 || ndist > 30) break;
            for (i = 0; i < 19; i++) g_zlen[z_clord[i]] = (pdb)(i < ncode ? z_bits(3) : 0);
            if (z_build(g_zlcnt, g_zlsym, 0, 19)) break;
            i = 0;
            while (i < nlen + ndist && !g_zerr) {
                s = z_dec(g_zlcnt, g_zlsym);
                if (s < 16) { g_zlen[i++] = (pdb)s; continue; }
                v = 0;
                if (s == 16) { if (!i) { g_zerr = 1; break; } v = g_zlen[i - 1]; rep = (pdw)(3 + z_bits(2)); }
                else if (s == 17) rep = (pdw)(3 + z_bits(3));
                else rep = (pdw)(11 + z_bits(7));
                if (i + rep > nlen + ndist) { g_zerr = 1; break; }
                while (rep--) g_zlen[i++] = (pdb)v;
            }
            if (g_zerr) break;
            for (i = ndist; i--; ) g_zlen[288 + i] = g_zlen[nlen + i];     /* backwards: the two may overlap */
            if (z_build(g_zlcnt, g_zlsym, 0, nlen) || z_build(g_zdcnt, g_zdsym, 288, ndist)) break;
        } else break;
        if (!z_codes()) break;
    } while (!last);
    return (pdw)(g_zout - g_zstart);
}

/* the stream (obj, gen), one /FlateDecode, inflated into the workspace at
 * [off, g_pcap): its length, 0xFFFF when it does not fit, 0 when it cannot
 * be read here (another filter, a damaged stream) */
static pdw pdx_zstrm(pdw obj, pdw gen, pdw off)
{
    static struct pdxb_rawcopy rw;
    pdw room = (pdw)(g_pcap - off), top, i, n, c;
    if (wd_get(obj, gen, "Filter", 0, 0)) return 0;
    if (g_dg.kind == PDK_ARR && g_dg.count == 1 && wd_get(obj, gen, "Filter", "[0]", 0)) return 0;
    if (!fd_name_is("FlateDecode")) return 0;
    pdx_zero(&rw, sizeof rw);
    rw.objnum = obj; rw.gen = gen; rw.dseg = g_ws; rw.doff = off; rw.dmax = room; rw.mode = PDRC_PLAIN;
    pd_fcopy_out(&rw, g_xb, 0, sizeof rw);
    if (pdx_eng(PDV_ENG_RAW_COPY, 0, 0, 0)) return 0;
    pd_fcopy_in(g_xb, 0, &rw, sizeof rw);
    n = rw.got;
    if (n == room) {                                    /* one byte more is "too large" */
        rw.dseg = g_stgseg; rw.doff = g_stgoff; rw.dmax = 1;
        pd_fcopy_out(&rw, g_xb, 0, sizeof rw);
        if (!pdx_eng(PDV_ENG_RAW_COPY, 0, 0, 0)) { pd_fcopy_in(g_xb, 0, &rw, sizeof rw); if (rw.got) return 0xFFFF; }
    }
    top = (pdw)(g_pcap - n);
    pd_fcopy(g_ws, top, g_ws, off, n);                  /* the object to the region's top */
    for (i = 6; i + 1 < n && i < 4096; i++)             /* its data: after "stream" EOL, not "endstream" */
        if (pd_fpeekb(g_ws, (pdw)(top + i - 6)) == 's' && pd_fpeekb(g_ws, (pdw)(top + i - 1)) == 'm' &&
            pd_fpeekb(g_ws, (pdw)(top + i - 5)) == 't' && pd_fpeekb(g_ws, (pdw)(top + i - 4)) == 'r' &&
            pd_fpeekb(g_ws, (pdw)(top + i - 2)) == 'a' &&
            (i == 6 || pd_fpeekb(g_ws, (pdw)(top + i - 7)) != 'd')) {
            c = pd_fpeekb(g_ws, (pdw)(top + i));
            if (c == '\r') { i++; c = pd_fpeekb(g_ws, (pdw)(top + i)); }
            if (c != '\n') continue;
            g_zseg = g_ws; g_zin = (pdw)(top + i + 1); g_zinend = g_pcap; g_zout = g_zstart = off; g_zlim = g_pcap;
            n = z_inflate();
            if (g_zmore) return 0xFFFF;
            return g_zerr && !n ? 0 : n;
        }
    return 0;
}
