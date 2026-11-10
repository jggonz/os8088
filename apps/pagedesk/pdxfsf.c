/* ============================================================================
 * os8088 - apps/pagedesk/pdxfsf.c      PDXF: sfnt (TrueType and OpenType)
 *
 * #included by pdxf.c. PAGEDESK-SPEC 20.22.8. The program is in far memory
 * (g_pseg, g_plen bytes); every read is bounded by P8().
 * ==========================================================================*/

/* ---- the program ------------------------------------------------------------ */
static pdw g_pseg, g_plen;          /* the font program, in the workspace       */
static pdb P8(pdw o) { return o < g_plen ? pd_fpeekb(g_pseg, o) : 0; }
static pdw P16(pdw o) { return (pdw)((P8(o) << 8) | P8((pdw)(o + 1))); }
static pdi P16s(pdw o) { return (pdi)P16(o); }
/* a 32-bit field that must fit 16 bits (the program is < 64 KiB): 0xFFFF if not */
static pdw P32(pdw o) { return P16(o) ? 0xFFFFu : P16((pdw)(o + 2)); }

/* 10^e10 / mant, saturating at 32767 (mant 1..9999, e10 0..9): the 1/value of a decimal */
static pdw inv_pow10(pdw mant, pdw e10)
{
    static struct pd_u32 u, t, q;
    pdw i;
    if (!mant || e10 > 9) return 0;
    pdm_u32_set(&u, 0, 1);
    for (i = 0; i < e10; i++) { pdm_u32_shl(&t, &u, 3); pdm_u32_shl(&u, &u, 1); pdm_u32_add(&u, &u, &t); }
    pdm_u32_divmod16(&q, &u, mant);
    return q.hi || q.lo > 32767 ? 32767 : q.lo;
}

/* ---- tables ----------------------------------------------------------------- */
static pdw g_sfbase;                /* the sfnt's start (0 for a bare font)      */
static pdw g_tlen;
static pdw sf_tab(pdw t0, pdw t1)   /* tag as two big-endian words; 0 = absent   */
{
    pdw n = P16((pdw)(g_sfbase + 4)), i, r, off, len;
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) {
        r = (pdw)(g_sfbase + 12 + i * 16);
        if (P16(r) == t0 && P16((pdw)(r + 2)) == t1) {
            off = P32((pdw)(r + 8)); len = P32((pdw)(r + 12));
            if (off == 0xFFFF || off >= g_plen) return 0;
            if (len == 0xFFFF || len > (pdw)(g_plen - off)) len = (pdw)(g_plen - off);
            g_tlen = len;
            return off;
        }
    }
    return 0;
}

static pdw g_sf_glyf, g_sf_glyfl, g_sf_loca, g_sf_lfmt, g_sf_ng, g_sf_hmtx, g_sf_nhm, g_sf_cmap;
static pdw g_cm_uni, g_cm_sym, g_cm_mac;    /* cmap subtable offsets (0 = none)  */
static pdi g_sf_asc, g_sf_desc;             /* head yMax / yMin in font units    */

static pdw sf_open(void)
{
    pdw head, maxp, hhea, loca, cm, n, i, r, pl, enc, off, t, loc_len, otto;
    g_sf_glyf = g_sf_loca = g_sf_hmtx = g_sf_cmap = 0;
    g_cm_uni = g_cm_sym = g_cm_mac = 0;
    t = P16(0);
    otto = (pdw)(t == 0x4F54 && P16(2) == 0x544F);   /* 'OTTO': OpenType with a CFF table */
    if (!otto && !((t == 0x0001 && P16(2) == 0) || (t == 0x7472 && P16(2) == 0x7565))) return 0;   /* 00010000 or 'true' */
    g_sfbase = 0;
    head = sf_tab(0x6865, 0x6164);                  /* 'head' */
    if (!head || g_tlen < 54) return 0;
    g_upem = P16((pdw)(head + 18));
    if (g_upem < 16 || g_upem > 16384) return 0;
    g_sf_lfmt = P16((pdw)(head + 50));
    g_sf_desc = P16s((pdw)(head + 38)); g_sf_asc = P16s((pdw)(head + 42));
    maxp = sf_tab(0x6D61, 0x7870);                  /* 'maxp' */
    if (!maxp) return 0;
    g_sf_ng = P16((pdw)(maxp + 4));
    if (otto) { g_cf_base = sf_tab(0x4346, 0x4620); if (!g_cf_base) return 0; }          /* 'CFF ' */
    else {
        g_sf_glyf = sf_tab(0x676C, 0x7966); g_sf_glyfl = g_tlen;       /* 'glyf' */
        loca = sf_tab(0x6C6F, 0x6361); loc_len = g_tlen;             /* 'loca' */
        if (!g_sf_glyf || !loca) return 0;
        g_sf_loca = loca;
        n = (pdw)(loc_len / (g_sf_lfmt ? 4 : 2));
        if (n > 0) n--;
        if (g_sf_ng > n) g_sf_ng = n;
    }
    hhea = sf_tab(0x6868, 0x6561);                  /* 'hhea' */
    g_sf_hmtx = sf_tab(0x686D, 0x7478);             /* 'hmtx' */
    g_sf_nhm = hhea ? P16((pdw)(hhea + 34)) : 0;
    if (g_sf_nhm * 4u > g_tlen && g_sf_hmtx) g_sf_nhm = (pdw)(g_tlen / 4);
    cm = sf_tab(0x636D, 0x6170);                    /* 'cmap' */
    if (cm) {
        g_sf_cmap = cm;
        n = P16((pdw)(cm + 2)); if (n > 16) n = 16;
        for (i = 0; i < n; i++) {
            r = (pdw)(cm + 4 + i * 8);
            pl = P16(r); enc = P16((pdw)(r + 2)); off = P32((pdw)(r + 4));
            if (off == 0xFFFF || off >= g_tlen) continue;
            off = (pdw)(cm + off);
            if (pl == 3 && (enc == 10 || (enc == 1 && !g_cm_uni))) g_cm_uni = off;
            else if (pl == 0 && !g_cm_uni) g_cm_uni = off;
            else if (pl == 3 && enc == 0) g_cm_sym = off;
            else if (pl == 1 && enc == 0) g_cm_mac = off;
        }
    }
    return otto ? 2 : 1;
}

static pdw sf_adv(pdw gid)          /* font units */
{
    if (!g_sf_hmtx || !g_sf_nhm) return 0;
    if (gid >= g_sf_nhm) gid = (pdw)(g_sf_nhm - 1);
    return P16((pdw)(g_sf_hmtx + gid * 4));
}

/* ---- cmap ------------------------------------------------------------------ */
static pdw cm_get(pdw sub, pdw code)
{
    pdw fmt = P16(sub), lo, hi, mid, n, seg2, end, start, ro, g, p;
    if (!sub) return 0;
    if (fmt == 0) return code < 256 ? P8((pdw)(sub + 6 + code)) : 0;
    if (fmt == 6) {
        start = P16((pdw)(sub + 6)); n = P16((pdw)(sub + 8));
        return code >= start && code - start < n ? P16((pdw)(sub + 10 + (code - start) * 2)) : 0;
    }
    if (fmt == 4) {
        seg2 = P16((pdw)(sub + 6)); n = (pdw)(seg2 >> 1);
        lo = 0; hi = n;
        while (lo < hi) {
            mid = (pdw)((lo + hi) >> 1);
            end = P16((pdw)(sub + 14 + mid * 2));
            if (end < code) lo = (pdw)(mid + 1); else hi = mid;
        }
        if (lo >= n) return 0;
        start = P16((pdw)(sub + 16 + seg2 + lo * 2));
        if (start > code) return 0;
        ro = P16((pdw)(sub + 16 + seg2 * 3 + lo * 2));
        if (!ro) return (pdw)(code + P16((pdw)(sub + 16 + seg2 * 2 + lo * 2)));
        p = (pdw)(sub + 16 + seg2 * 3 + lo * 2 + ro + (code - start) * 2);
        g = P16(p);
        return g ? (pdw)(g + P16((pdw)(sub + 16 + seg2 * 2 + lo * 2))) : 0;
    }
    if (fmt == 12) {
        n = P32((pdw)(sub + 12)); if (n > 4000) n = 4000;
        lo = 0; hi = n;
        while (lo < hi) {
            mid = (pdw)((lo + hi) >> 1);
            p = (pdw)(sub + 16 + mid * 12);
            if (P16(p + 4) || P16((pdw)(p + 6)) < code) lo = (pdw)(mid + 1); else hi = mid;
        }
        if (lo >= n) return 0;
        p = (pdw)(sub + 16 + lo * 12);
        start = P16((pdw)(p + 2));
        if (P16(p) || start > code) return 0;
        return (pdw)(P16((pdw)(p + 10)) + (code - start));
    }
    return 0;
}

/* ---- glyph outlines ---------------------------------------------------------- */
#define SF_NPT 512
static pdi g_gpx[SF_NPT], g_gpy[SF_NPT];
static pdb g_gpf[SF_NPT];
static pdw g_gend[96];
static pdw g_gnc, g_gnp;

static pdw g_sflen;
static pdw sf_loc(pdw gid, pdw *len)    /* glyph offset in the program, 0 = none */
{
    pdw a, b;
    if (gid >= g_sf_ng) return 0;
    if (g_sf_lfmt) { a = P32((pdw)(g_sf_loca + gid * 4)); b = P32((pdw)(g_sf_loca + gid * 4 + 4)); }
    else { a = (pdw)(P16((pdw)(g_sf_loca + gid * 2)) * 2); b = (pdw)(P16((pdw)(g_sf_loca + gid * 2 + 2)) * 2); }
    if (a == 0xFFFF || b == 0xFFFF || b <= a || b > g_sf_glyfl) { *len = 0; return 0; }
    *len = (pdw)(b - a);
    return (pdw)(g_sf_glyf + a);
}

static pdi hmid(pdi a, pdi b) { return (pdi)((a >> 1) + (b >> 1) + (a & b & 1)); }
/* the glyph in g_gp* -> outline ops; points are already transformed */
static void sf_emit(void)
{
    pdw c, s = 0, e, n, f, j, k, have;
    pdi sx, sy, cx = 0, cy = 0, x, y;
    for (c = 0; c < g_gnc; c++) {
        e = g_gend[c]; n = (pdw)(e - s + 1);
        if (n < 2) { s = (pdw)(e + 1); continue; }
        for (f = 0; f < n && !(g_gpf[s + f] & 1); f++) ;
        if (f == n) {                               /* all off-curve: start at a midpoint */
            sx = hmid(g_gpx[s], g_gpx[e]); sy = hmid(g_gpy[s], g_gpy[e]);
            f = 0; k = n;
            ol_move(sx, sy);
            have = 0;
            for (j = 0; j < k; j++) {
                x = g_gpx[s + j]; y = g_gpy[s + j];
                if (have) { ol_quad(cx, cy, hmid(cx, x), hmid(cy, y)); }
                cx = x; cy = y; have = 1;
            }
            ol_quad(cx, cy, sx, sy);
        } else {
            sx = g_gpx[s + f]; sy = g_gpy[s + f];
            ol_move(sx, sy);
            have = 0;
            for (j = 1; j < n; j++) {
                k = (pdw)(s + (f + j) % n);
                x = g_gpx[k]; y = g_gpy[k];
                if (g_gpf[k] & 1) {
                    if (have) { ol_quad(cx, cy, x, y); have = 0; } else ol_line(x, y);
                } else {
                    if (have) ol_quad(cx, cy, hmid(cx, x), hmid(cy, y));
                    cx = x; cy = y; have = 1;
                }
            }
            if (have) ol_quad(cx, cy, sx, sy);
        }
        ol_close();
        s = (pdw)(e + 1);
    }
}

/* decode a simple glyph at `o` (length len) into g_gp* */
static pdw sf_simple(pdw o, pdw len, pdi nc)
{
    pdw i, np, p, fl, rep, lim = (pdw)(o + len);
    pdi v;
    if (nc > 96) return 0;
    g_gnc = (pdw)nc;
    p = (pdw)(o + 10);
    for (i = 0; i < g_gnc; i++) { g_gend[i] = P16(p); p = (pdw)(p + 2); if (i && g_gend[i] < g_gend[i - 1]) return 0; }
    np = g_gnc ? (pdw)(g_gend[g_gnc - 1] + 1) : 0;
    if (np > SF_NPT) return 0;
    g_gnp = np;
    p = (pdw)(p + 2 + P16(p));                          /* skip instructions */
    for (i = 0; i < np; ) {
        if (p >= lim) return 0;
        fl = P8(p++);
        g_gpf[i++] = (pdb)fl;
        if (fl & 8) {
            rep = P8(p++);
            while (rep-- && i < np) g_gpf[i++] = (pdb)fl;
        }
    }
    v = 0;
    for (i = 0; i < np; i++) {
        fl = g_gpf[i];
        if (fl & 2) { pdw d = P8(p++); v = (pdi)(v + ((fl & 16) ? (pdi)d : -(pdi)d)); }
        else if (!(fl & 16)) { v = (pdi)(v + P16s(p)); p = (pdw)(p + 2); }
        g_gpx[i] = v;
    }
    v = 0;
    for (i = 0; i < np; i++) {
        fl = g_gpf[i];
        if (fl & 4) { pdw d = P8(p++); v = (pdi)(v + ((fl & 32) ? (pdi)d : -(pdi)d)); }
        else if (!(fl & 32)) { v = (pdi)(v + P16s(p)); p = (pdw)(p + 2); }
        g_gpy[i] = v;
    }
    return p <= lim + 1;
}

/* the composite stack: pending components with the matrix they inherit */
#define SF_STK 6
static pdw g_stg[SF_STK];
static pdi g_stm[SF_STK][6];        /* a b c d (F2Dot14) dx dy                   */
static pdi g_cm[6];

static void tx_pts(const pdi *m)
{
    pdw i;
    pdi x, y;
    if (m[0] == 16384 && m[1] == 0 && m[2] == 0 && m[3] == 16384) {
        for (i = 0; i < g_gnp; i++) { g_gpx[i] = (pdi)(g_gpx[i] + m[4]); g_gpy[i] = (pdi)(g_gpy[i] + m[5]); }
        return;
    }
    for (i = 0; i < g_gnp; i++) {
        x = g_gpx[i]; y = g_gpy[i];
        g_gpx[i] = (pdi)(pdx_muldiv(x, m[0], 16384) + pdx_muldiv(y, m[2], 16384) + m[4]);
        g_gpy[i] = (pdi)(pdx_muldiv(x, m[1], 16384) + pdx_muldiv(y, m[3], 16384) + m[5]);
    }
}
static void mat_cat(pdi *r, const pdi *inner, const pdi *outer)   /* r = inner then outer */
{
    pdi a = (pdi)(pdx_muldiv(inner[0], outer[0], 16384) + pdx_muldiv(inner[1], outer[2], 16384));
    pdi b = (pdi)(pdx_muldiv(inner[0], outer[1], 16384) + pdx_muldiv(inner[1], outer[3], 16384));
    pdi c = (pdi)(pdx_muldiv(inner[2], outer[0], 16384) + pdx_muldiv(inner[3], outer[2], 16384));
    pdi d = (pdi)(pdx_muldiv(inner[2], outer[1], 16384) + pdx_muldiv(inner[3], outer[3], 16384));
    pdi e = (pdi)(pdx_muldiv(inner[4], outer[0], 16384) + pdx_muldiv(inner[5], outer[2], 16384) + outer[4]);
    pdi f = (pdi)(pdx_muldiv(inner[4], outer[1], 16384) + pdx_muldiv(inner[5], outer[3], 16384) + outer[5]);
    r[0] = a; r[1] = b; r[2] = c; r[3] = d; r[4] = e; r[5] = f;
}

/* the whole glyph (components included) into the recorder; 0 on a refusal */
static pdw sf_glyph(pdw gid)
{
    pdw sp = 1, len, o, p, fl, cg, budget = 64, ok = 1, i;
    pdi nc, a1, a2;
    ol_reset();
    g_stg[0] = gid;
    g_stm[0][0] = 16384; g_stm[0][1] = 0; g_stm[0][2] = 0; g_stm[0][3] = 16384; g_stm[0][4] = 0; g_stm[0][5] = 0;
    while (sp && ok) {
        sp--;
        for (i = 0; i < 6; i++) g_cm[i] = g_stm[sp][i];
        o = sf_loc(g_stg[sp], &g_sflen); len = g_sflen;
        if (!o || len < 10) continue;                       /* empty (a space) */
        nc = P16s(o);
        if (nc >= 0) {
            if (!sf_simple(o, len, nc)) { ok = 0; break; }
            tx_pts(g_cm);
            sf_emit();
        } else {
            p = (pdw)(o + 10);
            do {
                if (p + 4 > (pdw)(o + len) || !budget--) { ok = 0; break; }
                fl = P16(p); cg = P16((pdw)(p + 2)); p = (pdw)(p + 4);
                if (!(fl & 2)) { ok = 0; break; }          /* point matching: not read */
                if (fl & 1) { a1 = P16s(p); a2 = P16s((pdw)(p + 2)); p = (pdw)(p + 4); }
                else { a1 = (pdi)(signed char)P8(p); a2 = (pdi)(signed char)P8((pdw)(p + 1)); p = (pdw)(p + 2); }
                g_t[0] = 16384; g_t[1] = 0; g_t[2] = 0; g_t[3] = 16384; g_t[4] = a1; g_t[5] = a2;
                if (fl & 8) { g_t[0] = g_t[3] = P16s(p); p = (pdw)(p + 2); }
                else if (fl & 0x40) { g_t[0] = P16s(p); g_t[3] = P16s((pdw)(p + 2)); p = (pdw)(p + 4); }
                else if (fl & 0x80) { g_t[0] = P16s(p); g_t[1] = P16s((pdw)(p + 2)); g_t[2] = P16s((pdw)(p + 4)); g_t[3] = P16s((pdw)(p + 6)); p = (pdw)(p + 8); }
                if (sp >= SF_STK) { ok = 0; break; }
                g_stg[sp] = cg;
                mat_cat(g_stm[sp], g_t, g_cm);              /* the component's own, then the parent's */
                sp++;
            } while (fl & 0x20);
        }
    }
    ol_close();
    return ok;
}
