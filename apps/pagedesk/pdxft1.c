/* ============================================================================
 * os8088 - apps/pagedesk/pdxft1.c      PDXF: Type 1 (PFA, PFB and the PDF raw form)
 *
 * #included by pdxf.c after pdxfsf.c. PAGEDESK-SPEC 20.22.8. The eexec section
 * is decrypted IN PLACE in the workspace, then each charstring in place as the
 * table is built; glyphs are indexed by name (gid = index + 1, 0 = none).
 * ==========================================================================*/
#define T1_MAXG 600
#define T1_MAXS 200
static pdw g_t1_ng, g_t1_ns, g_t1_leniv;
static pdw g_t1_gn[T1_MAXG], g_t1_go[T1_MAXG], g_t1_gl[T1_MAXG];       /* name offset, charstring offset, length */
static pdw g_t1_so[T1_MAXS], g_t1_sl[T1_MAXS];
static pdw g_t1_encoff[256];        /* /Encoding array: code -> name offset (0 = none); 1 = StandardEncoding */
static pdw g_t1_std;
static pdw g_t1_clear;              /* end of the cleartext                                    */

static void P8w(pdw o, pdw v) { if (o < g_plen) pd_fpokeb(g_pseg, o, (pdb)v); }

/* find `pat` in [from, to); returns the offset or 0xFFFF */
static pdw t1_find(const char *pat, pdw from, pdw to)
{
    pdw i, k;
    if (to > g_plen) to = g_plen;
    for (i = from; i < to; i++) {
        if (P8(i) != (pdb)pat[0]) continue;
        for (k = 1; pat[k] && i + k < to && P8((pdw)(i + k)) == (pdb)pat[k]; k++) ;
        if (!pat[k]) return i;
    }
    return 0xFFFF;
}
static pdw t1_num(pdw *pp, pdw to)          /* decimal at *pp (spaces skipped); 0xFFFF if none */
{
    pdw p = *pp, v = 0, any = 0, c;
    while (p < to && (P8(p) == ' ' || P8(p) == '\n' || P8(p) == '\r' || P8(p) == '\t')) p++;
    while (p < to && (c = P8(p)) >= '0' && c <= '9') { v = (pdw)(v * 10 + (c - '0')); p++; any = 1; if (v > 6000) break; }
    *pp = p;
    return any ? v : 0xFFFF;
}
static void t1_decrypt(pdw o, pdw n, pdw r, pdw skip)
{
    pdw i, c, p;
    for (i = 0; i < n; i++) {
        c = P8((pdw)(o + i));
        p = (pdw)(c ^ (r >> 8));
        r = (pdw)((c + r) * 52845u + 22719u);
        P8w((pdw)(o + i), p);
    }
    /* the first `skip` bytes are random lead: leave them, callers step over them */
    (void)skip;
}

/* PFB: strip the 6-byte segment headers by moving the data down; returns the new length */
static pdw t1_unpfb(void)
{
    pdw p = 0, w = 0, n, i;
    while (p + 6 <= g_plen && P8(p) == 0x80 && P8((pdw)(p + 1)) != 3) {
        n = (pdw)(P8((pdw)(p + 2)) | (P8((pdw)(p + 3)) << 8));
        if (P16((pdw)(p + 4)) != 0) break;                 /* a segment over 64 KiB: refused */
        p = (pdw)(p + 6);
        if (p + n > g_plen) n = (pdw)(g_plen - p);
        for (i = 0; i < n; i++) P8w((pdw)(w + i), P8((pdw)(p + i)));
        w = (pdw)(w + n); p = (pdw)(p + n);
    }
    return w;
}

static pdw t1_open(void)
{
    pdw e, p, to, n, i, k, hex, b, ci, endc;
    g_t1_ng = g_t1_ns = 0; g_t1_leniv = 4; g_t1_std = 0;
    for (i = 0; i < 256; i++) g_t1_encoff[i] = 0;
    if (P8(0) == 0x80) g_plen = t1_unpfb();                    /* PFB */
    if (P8(0) != '%' || P8(1) != '!') return 0;
    e = t1_find("eexec", 0, g_plen);
    if (e == 0xFFFF) return 0;
    p = (pdw)(e + 5);
    while (p < g_plen && (P8(p) == ' ' || P8(p) == '\r' || P8(p) == '\n' || P8(p) == '\t')) p++;
    g_t1_clear = p;
    /* the cleartext /Encoding */
    ci = t1_find("/Encoding", 0, e);
    if (ci != 0xFFFF) {
        pdw q = (pdw)(ci + 9);
        while (q < e && P8(q) == ' ') q++;
        if (P8(q) == 'S') g_t1_std = 1;
        else {
            for (;;) {                                  /* dup <code> /<name> put */
                pdw d = t1_find("dup", q, e), c2, nm;
                pdw rd = t1_find("readonly", q, e), df;
                if (d == 0xFFFF) break;
                df = t1_find(" def", q, e);
                if (rd != 0xFFFF && rd < d) break;
                q = (pdw)(d + 3);
                c2 = t1_num(&q, e);
                while (q < e && P8(q) == ' ') q++;
                if (c2 != 0xFFFF && c2 < 256 && P8(q) == '/') { nm = (pdw)(q + 1); g_t1_encoff[c2] = nm; }
                (void)df;
            }
        }
    }
    /* binary or hex? the first four bytes of the section */
    hex = 1;
    for (i = 0; i < 4; i++) { b = P8((pdw)(p + i)); if (hexv(b) == 0xFFFF) hex = 0; }
    if (hex) {                                          /* ASCII hex: pack down in place */
        pdw w = p, a, h1, h2;
        for (i = p; i < g_plen; ) {
            while (i < g_plen && hexv(P8(i)) == 0xFFFF) { if (P8(i) == 0 ) break; i++; }
            if (i + 1 >= g_plen) break;
            h1 = hexv(P8(i)); h2 = hexv(P8((pdw)(i + 1)));
            if (h1 == 0xFFFF || h2 == 0xFFFF) break;
            a = (pdw)(h1 * 16 + h2); P8w(w++, a); i = (pdw)(i + 2);
        }
        g_plen = w;
    }
    to = g_plen;
    t1_decrypt(p, (pdw)(to - p), 55665, 4);
    p = (pdw)(p + 4);
    endc = to;
    /* lenIV */
    k = t1_find("/lenIV", p, endc);
    if (k != 0xFFFF) { pdw q = (pdw)(k + 6); n = t1_num(&q, endc); if (n != 0xFFFF && n < 8) g_t1_leniv = n; }
    /* /Subrs */
    k = t1_find("/Subrs", p, endc);
    ci = t1_find("/CharStrings", p, endc);
    if (ci == 0xFFFF) return 0;
    if (k != 0xFFFF && k < ci) {
        pdw q = (pdw)(k + 6), idx, len;
        for (;;) {
            pdw d = t1_find("dup ", q, ci);
            if (d == 0xFFFF) break;
            q = (pdw)(d + 4);
            idx = t1_num(&q, ci); len = t1_num(&q, ci);
            if (idx == 0xFFFF || len == 0xFFFF) break;
            while (q < ci && P8(q) == ' ') q++;
            while (q < ci && P8(q) != ' ') q++;          /* RD or -| */
            q++;
            if (idx >= T1_MAXS || q + len > ci) { q = (pdw)(q + len); continue; }
            t1_decrypt(q, len, 4330, g_t1_leniv);
            g_t1_so[idx] = (pdw)(q + g_t1_leniv); g_t1_sl[idx] = len > g_t1_leniv ? (pdw)(len - g_t1_leniv) : 0;
            if (idx + 1 > g_t1_ns) g_t1_ns = (pdw)(idx + 1);
            q = (pdw)(q + len);
        }
    }
    /* the charstrings: /name len RD <bytes> ND */
    {
        pdw q = (pdw)(ci + 12), len, nm;
        while (q < endc && g_t1_ng < T1_MAXG) {
            while (q < endc && P8(q) != '/' && !(P8(q) == 'e' && P8((pdw)(q + 1)) == 'n' && P8((pdw)(q + 2)) == 'd')) q++;
            if (q >= endc || P8(q) != '/') break;
            nm = (pdw)(q + 1); q = nm;
            while (q < endc && P8(q) != ' ') q++;
            len = t1_num(&q, endc);
            if (len == 0xFFFF) break;
            while (q < endc && P8(q) == ' ') q++;
            while (q < endc && P8(q) != ' ') q++;
            q++;
            if (q + len > endc) break;
            t1_decrypt(q, len, 4330, g_t1_leniv);
            g_t1_gn[g_t1_ng] = nm; g_t1_go[g_t1_ng] = (pdw)(q + g_t1_leniv);
            g_t1_gl[g_t1_ng] = len > g_t1_leniv ? (pdw)(len - g_t1_leniv) : 0;
            g_t1_ng++;
            q = (pdw)(q + len);
        }
    }
    g_upem = 1000;
    {   /* /FontMatrix [0.001 ...]: only its first number matters (1/a) */
        pdw fm = t1_find("/FontMatrix", 0, e);
        if (fm != 0xFFFF) {
            pdw q = (pdw)(fm + 11), c, mant = 0, frac = 0, dot = 0, nd = 0, v;
            while (q < e && P8(q) != '[' && P8(q) != '{') q++;
            q++;
            while (q < e && P8(q) == ' ') q++;
            while (q < e && ((c = P8(q)) == '.' || (c >= '0' && c <= '9'))) {
                if (c == '.') dot = 1;
                else if (nd < 4) { mant = (pdw)(mant * 10 + (c - '0')); if (mant) nd++; if (dot) frac++; }
                q++;
            }
            if (mant && frac <= 9) { v = inv_pow10(mant, frac); if (v >= 16 && v <= 16384) g_upem = v; }
        }
    }
    return g_t1_ng > 0;
}

/* name at offset `o` equals `s` (the name ends at a space) */
static pdw t1_nm_is(pdw o, const char *s)
{
    pdw i = 0;
    while (s[i]) { if (P8((pdw)(o + i)) != (pdb)s[i]) return 0; i++; }
    return P8((pdw)(o + i)) == ' ' || P8((pdw)(o + i)) == '\n' || P8((pdw)(o + i)) == '\r' || P8((pdw)(o + i)) == '/';
}
static pdw t1_find_name(const char *name)       /* gid (index + 1) or 0 */
{
    pdw i;
    for (i = 0; i < g_t1_ng; i++) if (t1_nm_is(g_t1_gn[i], name)) return (pdw)(i + 1);
    return 0;
}
/* a code through the program's own Encoding */
static pdw t1_enc_gid(pdw code)
{
    pdw i, o;
    code &= 255;
    if (g_t1_std) {
        pdw sid = pdx_stdenc[code];
        const char *nm;
        if (!sid) return 0;
        nm = nm_nth(pdx_stdsid, sid);
        return nm ? t1_find_name(nm) : 0;
    }
    o = g_t1_encoff[code];
    if (!o) return 0;
    for (i = 0; i < g_t1_ng; i++) {
        pdw a = g_t1_gn[i], k = 0;
        while (P8((pdw)(o + k)) == P8((pdw)(a + k)) && P8((pdw)(o + k)) != ' ') k++;
        if ((P8((pdw)(o + k)) == ' ' || P8((pdw)(o + k)) == '\n' || P8((pdw)(o + k)) == '\r' || P8((pdw)(o + k)) == '/' ) &&
            (P8((pdw)(a + k)) == ' ')) return (pdw)(i + 1);
    }
    return 0;
}

/* ---- the Type 1 charstring machine ---------------------------------------------- */
#define T1_STK 32
#define T1_DEP 10
static pdi g_ts[T1_STK], g_tps[8];
static pdw g_tsp, g_tpsp, g_tflex, g_tnfp;
static pdi g_tfx[7], g_tfy[7];
static pdi g_tx, g_ty, g_tsbx;
static pdi g_t1_asb;
static pdw g_t1_seac, g_t1_sa, g_t1_sb, g_t1_sax, g_t1_say, g_t1_open;
static pdi g_t1_wid;
static pdi g_t1ox, g_t1oy;

static void t1_pt(pdi *x, pdi *y) { *x = (pdi)(g_tx + g_t1ox); *y = (pdi)(g_ty + g_t1oy); }
static void t1_move(pdi dx, pdi dy)
{
    g_tx = (pdi)(g_tx + dx); g_ty = (pdi)(g_ty + dy);
    if (g_tflex) {
        if (g_tnfp < 7) { g_tfx[g_tnfp] = g_tx; g_tfy[g_tnfp] = g_ty; g_tnfp++; }
        return;
    }
    { pdi x, y; t1_pt(&x, &y); ol_move(x, y); }
}
static void t1_line(pdi dx, pdi dy) { pdi x, y; g_tx = (pdi)(g_tx + dx); g_ty = (pdi)(g_ty + dy); t1_pt(&x, &y); ol_line(x, y); }
static void t1_curve(pdi a, pdi b, pdi c, pdi d, pdi e, pdi f)
{
    pdi x1 = (pdi)(g_tx + a), y1 = (pdi)(g_ty + b), x2 = (pdi)(x1 + c), y2 = (pdi)(y1 + d);
    g_tx = (pdi)(x2 + e); g_ty = (pdi)(y2 + f);
    ol_cubic((pdi)(x1 + g_t1ox), (pdi)(y1 + g_t1oy), (pdi)(x2 + g_t1ox), (pdi)(y2 + g_t1oy), (pdi)(g_tx + g_t1ox), (pdi)(g_ty + g_t1oy));
}

static pdw g_t1_sp[T1_DEP], g_t1_se[T1_DEP];
static pdw t1_run(pdw o, pdw n)
{
    pdw pos = o, end = (pdw)(o + n), fsp = 0, b, op, ok = 1, count = 0, idx;
    pdi v;
    g_tsp = 0; g_tpsp = 0; g_tflex = 0; g_tnfp = 0; g_t1_seac = 0; g_tx = g_ty = 0; g_tsbx = 0; g_t1_wid = 0;
    while (ok) {
        if (pos >= end) { if (!fsp) break; fsp--; pos = g_t1_sp[fsp]; end = g_t1_se[fsp]; continue; }
        if (++count > 6000) { ok = 0; break; }
        b = P8(pos++);
        if (b >= 32) {
            if (g_tsp >= T1_STK) { ok = 0; break; }
            if (b <= 246) v = (pdi)((pdi)b - 139);
            else if (b <= 250) { v = (pdi)(((pdi)b - 247) * 256 + (pdi)P8(pos) + 108); pos++; }
            else if (b <= 254) { v = (pdi)(-((pdi)b - 251) * 256 - (pdi)P8(pos) - 108); pos++; }
            else { v = (pdi)(P16(pos) ? (P16(pos) & 0x8000 ? -32767 : 32767) : P16((pdw)(pos + 2))); pos += 4; }
            g_ts[g_tsp++] = v;
            continue;
        }
        op = b;
        if (b == 12) op = (pdw)(0x100 | P8(pos++));
        switch (op) {
        case 13: if (g_tsp >= 2) { g_tsbx = g_ts[0]; g_t1_wid = g_ts[1]; g_tx = g_tsbx; g_ty = 0; } g_tsp = 0; break;   /* hsbw */
        case 0x107: if (g_tsp >= 4) { g_tsbx = g_ts[0]; g_t1_wid = g_ts[2]; g_tx = g_ts[0]; g_ty = g_ts[1]; } g_tsp = 0; break;   /* sbw */
        case 21: if (g_tsp >= 2) t1_move(g_ts[g_tsp - 2], g_ts[g_tsp - 1]); g_tsp = 0; break;
        case 22: if (g_tsp >= 1) t1_move(g_ts[g_tsp - 1], 0); g_tsp = 0; break;
        case 4:  if (g_tsp >= 1) t1_move(0, g_ts[g_tsp - 1]); g_tsp = 0; break;
        case 5:  if (g_tsp >= 2) t1_line(g_ts[0], g_ts[1]); g_tsp = 0; break;
        case 6:  if (g_tsp >= 1) t1_line(g_ts[0], 0); g_tsp = 0; break;
        case 7:  if (g_tsp >= 1) t1_line(0, g_ts[0]); g_tsp = 0; break;
        case 8:  if (g_tsp >= 6) t1_curve(g_ts[0], g_ts[1], g_ts[2], g_ts[3], g_ts[4], g_ts[5]); g_tsp = 0; break;
        case 30: if (g_tsp >= 4) t1_curve(0, g_ts[0], g_ts[1], g_ts[2], g_ts[3], 0); g_tsp = 0; break;
        case 31: if (g_tsp >= 4) t1_curve(g_ts[0], 0, g_ts[1], g_ts[2], 0, g_ts[3]); g_tsp = 0; break;
        case 9: ol_close(); g_tsp = 0; break;
        case 1: case 3: case 0x100: case 0x101: case 0x102: g_tsp = 0; break;           /* hints, dotsection */
        case 10:                                                         /* callsubr */
            if (!g_tsp || fsp >= T1_DEP) { ok = 0; break; }
            idx = (pdw)g_ts[--g_tsp];
            if (idx >= g_t1_ns || !g_t1_sl[idx]) { ok = 0; break; }
            g_t1_sp[fsp] = pos; g_t1_se[fsp] = end; fsp++;
            pos = g_t1_so[idx]; end = (pdw)(pos + g_t1_sl[idx]);
            break;
        case 11: if (fsp) { fsp--; pos = g_t1_sp[fsp]; end = g_t1_se[fsp]; } break;
        case 14: goto done;
        case 0x10C:                                                      /* div */
            if (g_tsp >= 2 && g_ts[g_tsp - 1]) { g_ts[g_tsp - 2] = pdx_muldiv(g_ts[g_tsp - 2], 1, g_ts[g_tsp - 1]); g_tsp--; }
            break;
        case 0x110: {                                                    /* callothersubr: arg n othersubr# */
            pdw on, na, k;
            if (g_tsp < 2) { ok = 0; break; }
            on = (pdw)g_ts[--g_tsp]; na = (pdw)g_ts[--g_tsp];
            if (na > g_tsp) { ok = 0; break; }
            if (on == 1) { g_tflex = 1; g_tnfp = 0; g_tpsp = 0; }
            else if (on == 2) { /* a flex point was recorded by the rmoveto */ }
            else if (on == 0 && g_tflex) {
                g_tflex = 0;
                if (g_tnfp >= 7) {
                    pdi x1, y1, x2, y2, x3, y3;
                    /* the 7 points: reference, c1, c2, e1, c3, c4, e2 (absolute) */
                    x1 = (pdi)(g_tfx[1] + g_t1ox); y1 = (pdi)(g_tfy[1] + g_t1oy);
                    x2 = (pdi)(g_tfx[2] + g_t1ox); y2 = (pdi)(g_tfy[2] + g_t1oy);
                    x3 = (pdi)(g_tfx[3] + g_t1ox); y3 = (pdi)(g_tfy[3] + g_t1oy);
                    ol_cubic(x1, y1, x2, y2, x3, y3);
                    x1 = (pdi)(g_tfx[4] + g_t1ox); y1 = (pdi)(g_tfy[4] + g_t1oy);
                    x2 = (pdi)(g_tfx[5] + g_t1ox); y2 = (pdi)(g_tfy[5] + g_t1oy);
                    x3 = (pdi)(g_tfx[6] + g_t1ox); y3 = (pdi)(g_tfy[6] + g_t1oy);
                    ol_cubic(x1, y1, x2, y2, x3, y3);
                    g_tx = g_tfx[6]; g_ty = g_tfy[6];
                }
                /* leave y, x for the two pops and setcurrentpoint */
                g_tps[0] = g_ts[g_tsp + 2]; g_tps[1] = g_ts[g_tsp + 1]; g_tpsp = 2;
                na = 0;
            } else if (on == 3) { g_tps[0] = g_ts[g_tsp]; g_tpsp = 1; na = 0; }
            else { for (k = 0; k < na; k++) g_tps[g_tpsp++ & 7] = g_ts[g_tsp + k]; }
            g_tsp = (pdw)(g_tsp);
            break;
        }
        case 0x111: if (g_tpsp && g_tsp < T1_STK) { g_ts[g_tsp++] = g_tps[--g_tpsp]; } break;     /* pop */
        case 0x121: if (g_tsp >= 2) { g_tx = g_ts[g_tsp - 2]; g_ty = g_ts[g_tsp - 1]; } g_tsp = 0; break;   /* setcurrentpoint */
        case 0x106:                                                      /* seac */
            if (g_tsp >= 5) {
                g_t1_seac = 1; g_t1_sax = (pdw)g_ts[g_tsp - 4]; g_t1_say = (pdw)g_ts[g_tsp - 3];
                g_t1_sb = (pdw)g_ts[g_tsp - 2]; g_t1_sa = (pdw)g_ts[g_tsp - 1]; g_t1_asb = g_ts[g_tsp - 5];
            }
            goto done;
        default: g_tsp = 0; break;
        }
    }
done:
    ol_close();
    return ok;
}

/* the whole glyph, seac resolved: 1 ok */
static pdw t1_glyph(pdw gid)
{
    pdw i = (pdw)(gid - 1), r, b, a, sb, sa;
    pdi ax, ay, wid, sbx;
    ol_reset();
    g_t1ox = g_t1oy = 0;
    if (!gid || i >= g_t1_ng) return 0;
    r = t1_run(g_t1_go[i], g_t1_gl[i]);
    wid = g_t1_wid; sbx = g_tsbx;
    if (r && g_t1_seac) {
        pdi asb = g_t1_asb;
        ax = (pdi)g_t1_sax; ay = (pdi)g_t1_say; sb = g_t1_sb; sa = g_t1_sa;
        g_t1_seac = 0;
        b = pdx_stdenc[sb & 255] ? t1_find_name(nm_nth(pdx_stdsid, pdx_stdenc[sb & 255])) : 0;
        a = pdx_stdenc[sa & 255] ? t1_find_name(nm_nth(pdx_stdsid, pdx_stdenc[sa & 255])) : 0;
        if (b) { g_t1ox = g_t1oy = 0; r = t1_run(g_t1_go[b - 1], g_t1_gl[b - 1]); g_t1_seac = 0; }
        if (a && r) { g_t1ox = (pdi)(sbx + ax - asb); g_t1oy = ay; r = t1_run(g_t1_go[a - 1], g_t1_gl[a - 1]); g_t1_seac = 0; }
        g_t1ox = g_t1oy = 0;
    }
    g_t1_wid = wid;
    return r;
}
