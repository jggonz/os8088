/* ---- pdxjpkt.c: tier-2 - packets, tag trees, progressions (20.22.13.4) ------- */

/* the precinct table: per (component, resolution, precinct) where its persistent
 * state starts (bytes into the state region) and its first code-block record */
static pdw g_ptab[JX_PTAB * 2];
static pdw g_pstart[JX_MAXC * JX_MAXRES];
static pdw g_nent;
static pdw w_state, w_rec, w_plane, w_strip, w_arena;   /* workspace offsets       */
static pdw w_statesz, w_nrec;
static pdw g_arenatop, g_arenafull;
static pdw g_persist;                   /* more than one layer: state persists  */

/* one precinct's state block: code-block bytes, then per band two tag trees */
static pdw g_bcb[3], g_btt[3], g_bnn[3], g_blksz, g_ncbtot;

static pdw tnodes(pdw w, pdw h)
{
    pdw t = 0;
    if (!w || !h) return 0;
    for (;;) {
        t += w * h;
        if (w == 1 && h == 1) return t;
        w = (w + 1) >> 1; h = (h + 1) >> 1;
    }
}
static void blk_geom(pdw res, pdw p)
{
    pdw b, nb = res ? 3 : 1, t;
    g_ncbtot = 0;
    for (b = 0; b < nb; b++) {
        pb_geom(&g_pbs[b], res, p, b);
        g_bcb[b] = g_ncbtot;
        g_ncbtot += g_pbs[b].ncx * g_pbs[b].ncy;
        g_bnn[b] = tnodes(g_pbs[b].ncx, g_pbs[b].ncy);
    }
    t = g_ncbtot;
    for (b = 0; b < nb; b++) { g_btt[b] = t; t += 4 * g_bnn[b]; }
    g_blksz = t;
}

/* build the precinct table for the tile in g_t* and the kept resolutions up to
 * g_rtop; fills w_statesz and w_nrec. 0 or a PDE code (g_why set) */
static pdw tile_layout(void)
{
    pdw c, res, p, np, e = 0, st = 0, rec = 0, mx = 0, keep;
    g_nent = 0;
    for (c = 0; c < g_nc; c++) {
        for (res = 0; res <= g_nl; res++) {
            np = g_npx[res] * g_npy[res];
            g_pstart[c * JX_MAXRES + res] = e;
            keep = g_keep[c] && res <= g_rtop;
            for (p = 0; p < np; p++) {
                if (e >= JX_PTAB) return refuse(PDJ_R_LIMIT);
                blk_geom(res, p);
                if (keep && g_ncbtot > JX_NCBPKT) return refuse(PDJ_R_LIMIT);
                g_ptab[2 * e] = st; g_ptab[2 * e + 1] = rec;
                if (g_persist) {
                    if ((pdw)(st + g_blksz) < st) return refuse(PDJ_R_LIMIT);
                    st += g_blksz;
                } else if (g_blksz > mx) mx = g_blksz;
                if (keep) rec += g_ncbtot;
                e++;
            }
        }
    }
    g_nent = e;
    w_statesz = g_persist ? st : mx;
    w_nrec = rec;
    return 0;
}

/* ---- the packet header's bit reader ------------------------------------------ */
static pdw hb_buf, hb_ct, hb_eof;
static pdw hb_get(void)
{
    int c; pdw prev;
    if (!hb_ct) {
        prev = hb_buf;
        c = td_byte();
        if (c < 0) { hb_eof = 1; c = 0; }
        hb_buf = (pdw)c;
        hb_ct = prev == 0xFF ? 7 : 8;
    }
    hb_ct--;
    return (hb_buf >> hb_ct) & 1;
}
static pdw hb_bits(pdw n)
{
    pdw v = 0;
    while (n--) v = (v << 1) | hb_get();
    return v;
}
static void hb_align(void)
{
    hb_ct = 0;
    if (hb_buf == 0xFF) td_byte();
    hb_buf = 0;
}

/* ---- a tag tree, walked with an explicit stack (no recursion) ---------------- */
static pdw tt_stk[16];
/* 1 when the leaf's value is below thr. A node is two bytes: value+1 (0 = not
 * yet known) and the lower bound. */
static pdw tt_dec(pdw base, pdw ncx, pdw ncy, pdw i, pdw j, pdw thr)
{
    pdw w = ncx, h = ncy, off = base, x = i, y = j, sp = 0, low = 0, n, v1, nl;
    for (;;) {
        tt_stk[sp++] = off + 2 * (y * w + x);
        if ((w == 1 && h == 1) || sp >= 15) break;
        off += 2 * w * h;
        x >>= 1; y >>= 1; w = (w + 1) >> 1; h = (h + 1) >> 1;
    }
    v1 = 0;
    while (sp) {
        n = tt_stk[--sp];
        v1 = pd_fpeekb(g_ws, n); nl = pd_fpeekb(g_ws, n + 1);
        if (low > nl) nl = low; else low = nl;
        while (low < thr && (v1 == 0 || low < v1 - 1)) {
            if (hb_get()) v1 = low + 1; else low++;
            if (hb_eof) break;
        }
        pd_fpokeb(g_ws, n, (pdb)v1); pd_fpokeb(g_ws, n + 1, (pdb)low);
    }
    return v1 != 0 && v1 - 1 < thr;
}

/* ---- the packet --------------------------------------------------------------- */
/* One scratch array serves three phases that never overlap (20.22.13.3): the packet's
 * code-block list (tier-2), the horizontal transform's rows (DWT), and the tier-1
 * magnitudes (t1_m). Each is dead between uses: a packet is parsed and put away before
 * any block decodes, and a block's result is dequantised into the plane before the next
 * stage. */
static pdw jx_scr[64 * 64];
#define pl_rec  (jx_scr)
#define pl_np   (jx_scr + JX_NCBPKT)
#define pl_len  (jx_scr + 2 * JX_NCBPKT)
static pdw g_pls;                       /* a packet's status for the loops      */

static pdw ilog2(pdw v) { pdw k = 0; while (v > 1) { v >>= 1; k++; } return k; }

/* append len bytes of the stream to code-block rec's chain */
static pdw arena_put(pdw rec, pdw np, pdw len)
{
    pdw ro = w_rec + rec * 6, seg, tail, n;
    n = pd_fpeekb(g_ws, ro + 4);
    if (n & 0x80) return td_skip(0, len);                 /* an earlier part was dropped */
    if (!len) { n += np; pd_fpokeb(g_ws, ro + 4, (pdb)(n > 127 ? 127 : n)); return 0; }
    if (g_arenatop + 4u + len < g_arenatop || g_arenatop + 4u + len > g_wsend) {
        g_arenafull = 1;
        pd_fpokeb(g_ws, ro + 4, (pdb)(n | 0x80));
        return td_skip(0, len);
    }
    seg = g_arenatop;
    pd_fpokew(g_ws, seg, 0); pd_fpokew(g_ws, seg + 2, len);
    if (td_copy(g_ws, seg + 4, len)) return 1;
    tail = pd_fpeekw(g_ws, ro + 2);
    if (tail) pd_fpokew(g_ws, tail, seg); else pd_fpokew(g_ws, ro, seg);
    pd_fpokew(g_ws, ro + 2, seg);
    n += np; pd_fpokeb(g_ws, ro + 4, (pdb)(n > 127 ? 127 : n));
    g_arenatop += 4 + len;
    return 0;
}

/* 0 = go on, 1 = the data ended (stop quietly), 2 = a refusal (g_why) */
static pdw packet(pdw c, pdw res, pdw p, pdw l)
{
    pdw e, st0, rec0, b, nbands, i, j, k, keep, sa, tb, nn, inc, np, bits, len, cb;
    pdw tothi = 0, totlo = 0, npl = 0;
    int ch, ch2;
    e = g_pstart[c * JX_MAXRES + res] + p;
    st0 = g_ptab[2 * e]; rec0 = g_ptab[2 * e + 1];
    keep = g_keep[c] && res <= g_rtop;
    blk_geom(res, p);
    nbands = res ? 3 : 1;
    if (!g_persist) pd_ffill(g_ws, w_state, 0, g_blksz);
    if (g_sop) {
        ch = td_byte();
        if (ch == 0xFF) {
            ch2 = td_byte();
            if (ch2 == 0x91) { if (td_skip(0, 4)) return 1; }
            else { if (ch2 >= 0) td_back(ch2); td_back(ch); }
        } else if (ch >= 0) td_back(ch);
    }
    hb_buf = 0; hb_ct = 0; hb_eof = 0;
    if (hb_get()) {
        for (b = 0; b < nbands; b++) {
            nn = g_bnn[b];
            tb = w_state + st0 + g_btt[b];
            for (j = 0; j < g_pbs[b].ncy; j++) {
                for (i = 0; i < g_pbs[b].ncx; i++) {
                    cb = g_bcb[b] + j * g_pbs[b].ncx + i;
                    sa = w_state + st0 + cb;
                    inc = pd_fpeekb(g_ws, sa);
                    if (inc & 0x80) {
                        if (!hb_get()) continue;
                    } else {
                        if (!tt_dec(tb, g_pbs[b].ncx, g_pbs[b].ncy, i, j, l + 1)) continue;
                        for (k = 1; k <= 62; k++)
                            if (tt_dec(tb + 2 * nn, g_pbs[b].ncx, g_pbs[b].ncy, i, j, k) || hb_eof) break;
                        if (k > 62) { g_why = PDJ_R_LIMIT; return 2; }
                        if (keep) pd_fpokeb(g_ws, w_rec + (rec0 + cb) * 6 + 5, (pdb)(k - 1));
                        inc = 0x80;
                    }
                    if (!hb_get()) np = 1;
                    else if (!hb_get()) np = 2;
                    else {
                        np = hb_bits(2);
                        if (np != 3) np += 3;
                        else {
                            np = hb_bits(5);
                            if (np != 31) np += 6;
                            else np = hb_bits(7) + 37;
                        }
                    }
                    while (hb_get() && !hb_eof) inc++;
                    if ((inc & 0x7F) > 28) { g_why = PDJ_R_LIMIT; return 2; }
                    pd_fpokeb(g_ws, sa, (pdb)inc);
                    bits = 3 + (inc & 0x7F) + ilog2(np);
                    if (bits > 16) { g_why = PDJ_R_LIMIT; return 2; }
                    len = hb_bits(bits);
                    if (hb_eof) return 1;
                    if (keep) {
                        pl_rec[npl] = rec0 + cb; pl_np[npl] = np; pl_len[npl] = len; npl++;
                    } else {
                        if (totlo + len < totlo) tothi++;
                        totlo += len;
                    }
                }
            }
        }
    }
    if (hb_eof) return 1;
    hb_align();
    if (g_eph) {
        ch = td_byte();
        if (ch == 0xFF) {
            ch2 = td_byte();
            if (ch2 != 0x92) { if (ch2 >= 0) td_back(ch2); td_back(ch); }
        } else if (ch >= 0) td_back(ch);
    }
    if (keep) {
        for (i = 0; i < npl; i++)
            if (arena_put(pl_rec[i], pl_np[i], pl_len[i])) return 1;
    } else if (td_skip(tothi, totlo)) return 1;
    return 0;
}

/* ---- the progression orders -------------------------------------------------- */
static pdw np_of(pdw r) { return g_npx[r] * g_npy[r]; }

/* the precinct of resolution r that STARTS at reference position (y, x), or 0xFFFF */
static pdw hit(pdw r, pdw y, pdw x)
{
    pdw lv = g_nl - r, shy = g_ppy[r] + lv, shx = g_ppx[r] + lv, py, px, ym, xm;
    if (!g_npx[r] || !g_npy[r]) return 0xFFFF;
    ym = shy >= 15 ? y : (y & ((1u << shy) - 1u));
    xm = shx >= 15 ? x : (x & ((1u << shx) - 1u));
    if ((ym && y != g_ty0) || (xm && x != g_tx0)) return 0xFFFF;
    py = (cdiv(y, lv) >> g_ppy[r]) - (g_ry0[r] >> g_ppy[r]);
    px = (cdiv(x, lv) >> g_ppx[r]) - (g_rx0[r] >> g_ppx[r]);
    if (py >= g_npy[r] || px >= g_npx[r]) return 0xFFFF;
    return py * g_npx[r] + px;
}
static pdw step_sh(pdw horiz)
{
    pdw r, s, m = 15;
    for (r = 0; r <= g_nl; r++) {
        s = (horiz ? g_ppx[r] : g_ppy[r]) + g_nl - r;
        if (s < m) m = s;
    }
    return m;
}

static pdw tile_packets(void)
{
    pdw l, r, c, p, y, x, s, sy, sx;
    sy = step_sh(0); sx = step_sh(1);
    switch (g_prog) {
    case 0:                                             /* LRCP */
        for (l = 0; l < g_nlay; l++)
            for (r = 0; r <= g_nl; r++)
                for (c = 0; c < g_nc; c++)
                    for (p = 0; p < np_of(r); p++) { s = packet(c, r, p, l); if (s) return s; }
        break;
    case 1:                                             /* RLCP */
        for (r = 0; r <= g_nl; r++)
            for (l = 0; l < g_nlay; l++)
                for (c = 0; c < g_nc; c++)
                    for (p = 0; p < np_of(r); p++) { s = packet(c, r, p, l); if (s) return s; }
        break;
    case 2:                                             /* RPCL */
        for (r = 0; r <= g_nl; r++)
            for (y = g_ty0; y < g_ty1; y = ((y >> sy) + 1) << sy)
                for (x = g_tx0; x < g_tx1; x = ((x >> sx) + 1) << sx) {
                    p = hit(r, y, x);
                    if (p == 0xFFFF) continue;
                    for (c = 0; c < g_nc; c++)
                        for (l = 0; l < g_nlay; l++) { s = packet(c, r, p, l); if (s) return s; }
                }
        break;
    case 3:                                             /* PCRL */
        for (y = g_ty0; y < g_ty1; y = ((y >> sy) + 1) << sy)
            for (x = g_tx0; x < g_tx1; x = ((x >> sx) + 1) << sx)
                for (c = 0; c < g_nc; c++)
                    for (r = 0; r <= g_nl; r++) {
                        p = hit(r, y, x);
                        if (p == 0xFFFF) continue;
                        for (l = 0; l < g_nlay; l++) { s = packet(c, r, p, l); if (s) return s; }
                    }
        break;
    default:                                            /* CPRL */
        for (c = 0; c < g_nc; c++)
            for (y = g_ty0; y < g_ty1; y = ((y >> sy) + 1) << sy)
                for (x = g_tx0; x < g_tx1; x = ((x >> sx) + 1) << sx)
                    for (r = 0; r <= g_nl; r++) {
                        p = hit(r, y, x);
                        if (p == 0xFFFF) continue;
                        for (l = 0; l < g_nlay; l++) { s = packet(c, r, p, l); if (s) return s; }
                    }
        break;
    }
    return 0;
}

/* drain whatever is left of the tile's tile-parts, ready for the next SOT */
static void tile_finish(void)
{
    for (;;) {
        if (g_unb) { while (s_byte() >= 0) ; g_eoc = 1; return; }
        if (g_rmhi || g_rmlo) { s_skip32(g_rmhi, g_rmlo); g_rmhi = g_rmlo = 0; }
        if (!tp_next()) return;
    }
}
