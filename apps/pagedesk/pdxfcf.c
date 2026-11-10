/* ============================================================================
 * os8088 - apps/pagedesk/pdxfcf.c      PDXF: CFF (Type1C, CIDFontType0C, OpenType-CFF)
 *
 * #included by pdxf.c after pdxfsf.c (P8/P16 and the program). PAGEDESK-SPEC
 * 20.22.8. The Type 2 charstring machine is iterative: subroutine calls use an
 * explicit frame stack. Hints are parsed only to count stems and skip hintmask.
 * ==========================================================================*/
static pdw g_cf_cs, g_cf_ng, g_cf_charset, g_cf_enc, g_cf_gsub, g_cf_str, g_cf_cid, g_cf_fdarr, g_cf_fdsel;
static pdw g_cf_psize, g_cf_poff;   /* the Top (or FD) DICT's Private operands    */
static pdw g_cf_lsub, g_cf_nomw, g_cf_defw;     /* the Private DICT in force       */
static pdw g_cf_curfd, g_cf_mat;
static pdw g_is, g_il;              /* the last ix_get: absolute start, length    */
#define CF_NOFD 0xFFFF

/* ---- INDEX ------------------------------------------------------------------ */
static pdw ix_off(pdw idx, pdw os, pdw i)
{
    pdw p = (pdw)(idx + 3 + i * os);
    if (os == 1) return P8(p);
    if (os == 2) return P16(p);
    if (os == 3) return P8(p) ? 0xFFFFu : P16((pdw)(p + 1));
    return P16(p) ? 0xFFFFu : P16((pdw)(p + 2));
}
static pdw ix_count(pdw idx) { return idx ? P16(idx) : 0; }
/* the byte after the INDEX's data (0 on a malformed one) */
static pdw ix_end(pdw idx)
{
    pdw n = P16(idx), os, last;
    if (!n) return (pdw)(idx + 2);
    os = P8((pdw)(idx + 2));
    if (os < 1 || os > 4) return 0;
    last = ix_off(idx, os, n);
    if (last == 0xFFFF || last < 1) return 0;
    return (pdw)(idx + 3 + (n + 1) * os + last - 1);
}
static pdw ix_get(pdw idx, pdw i)
{
    pdw n, os, a, b, base;
    if (!idx) return 0;
    n = P16(idx);
    if (i >= n) return 0;
    os = P8((pdw)(idx + 2));
    if (os < 1 || os > 4) return 0;
    a = ix_off(idx, os, i); b = ix_off(idx, os, (pdw)(i + 1));
    if (a == 0xFFFF || b == 0xFFFF || b < a || a < 1) return 0;
    base = (pdw)(idx + 3 + (n + 1) * os - 1);
    g_is = (pdw)(base + a); g_il = (pdw)(b - a);
    return base + a >= base && (pdw)(g_is + g_il) <= g_plen && g_is + g_il >= g_is;
}

/* ---- DICT ------------------------------------------------------------------- */
static pdi g_dk[24];
static pdw g_dn, g_dreal_is;
static pdw g_dwhich;                /* 0 Top / FD, 1 Private                        */
static pdw g_dsubrs, g_dmatd;

static pdi cf_real(pdw *pp, pdw end)
{
    pdw p = *pp, mant = 0, nd = 0, frac = 0, dot = 0, ex = 0, esign = 0, inexp = 0, nib, k, stop = 0, drop = 0, b;
    pdi e10;
    while (p < end && !stop) {
        b = P8(p++);
        for (k = 0; k < 2 && !stop; k++) {
            nib = k ? (pdw)(b & 15) : (pdw)(b >> 4);
            if (nib <= 9) {
                if (inexp) ex = (pdw)(ex * 10 + nib);
                else if (nd < 4) { mant = (pdw)(mant * 10 + nib); if (mant) nd++; if (dot) frac++; }
                else if (!dot) drop++;
            }
            else if (nib == 10) dot = 1;
            else if (nib == 11) inexp = 1;
            else if (nib == 12) { inexp = 1; esign = 1; }
            else if (nib == 15) stop = 1;
        }
    }
    *pp = p;
    if (!mant) return 0;
    e10 = (pdi)((pdi)frac - (pdi)drop + (esign ? (pdi)ex : -(pdi)ex));      /* 1/value = 10^e10 / mant */
    if (e10 < 0 || e10 > 9) return 0;
    return (pdi)inv_pow10(mant, (pdw)e10);
}

static void cf_dict_op(pdw op)
{
    if (g_dwhich == 1) {
        if (op == 19 && g_dn) g_dsubrs = (pdw)g_dk[0];
        else if (op == 20 && g_dn) g_cf_defw = (pdw)g_dk[0];
        else if (op == 21 && g_dn) g_cf_nomw = (pdw)g_dk[0];
        return;
    }
    switch (op) {
    case 15: if (g_dn) g_cf_charset = (pdw)g_dk[0]; break;
    case 16: if (g_dn) g_cf_enc = (pdw)g_dk[0]; break;
    case 17: if (g_dn) g_cf_cs = (pdw)(g_cf_base + (pdw)g_dk[0]); break;
    case 18: if (g_dn >= 2) { g_cf_psize = (pdw)g_dk[0]; g_cf_poff = (pdw)(g_cf_base + (pdw)g_dk[1]); } break;
    case 0x107: if (g_dn) g_dmatd = (pdw)g_dk[0]; break;
    case 0x11E: g_cf_cid = 1; break;
    case 0x124: if (g_dn) g_cf_fdarr = (pdw)(g_cf_base + (pdw)g_dk[0]); break;
    case 0x125: if (g_dn) g_cf_fdsel = (pdw)(g_cf_base + (pdw)g_dk[0]); break;
    default: break;
    }
}
static pdw cf_dict(pdw o, pdw len, pdw which)
{
    pdw p = o, end = (pdw)(o + len), b;
    pdi v;
    g_dn = 0; g_dwhich = which;
    if (end > g_plen || end < o) return 0;
    while (p < end) {
        b = P8(p++);
        if (b <= 21) {
            if (b == 12) cf_dict_op((pdw)(0x100 | P8(p++))); else cf_dict_op(b);
            g_dn = 0; continue;
        }
        if (b == 28) { v = P16s(p); p += 2; }
        else if (b == 29) { v = (pdi)(P16(p) ? 32767 : P16((pdw)(p + 2))); p += 4; }
        else if (b == 30) v = cf_real(&p, end);
        else if (b >= 32 && b <= 246) v = (pdi)((pdi)b - 139);
        else if (b >= 247 && b <= 250) { v = (pdi)(((pdi)b - 247) * 256 + (pdi)P8(p) + 108); p++; }
        else if (b >= 251 && b <= 254) { v = (pdi)(-((pdi)b - 251) * 256 - (pdi)P8(p) - 108); p++; }
        else continue;
        if (g_dn < 24) g_dk[g_dn++] = v;
    }
    return 1;
}

/* the Private DICT at (psize, poff): subrs, widths. 0 = bad */
static pdw cf_private(pdw psize, pdw poff)
{
    g_cf_lsub = 0; g_cf_nomw = 0; g_cf_defw = 0; g_dsubrs = 0;
    if (!psize || poff >= g_plen) return 1;
    if (!cf_dict(poff, psize, 1)) return 0;
    if (g_dsubrs) g_cf_lsub = (pdw)(poff + g_dsubrs);
    return 1;
}

/* ---- the font --------------------------------------------------------------- */
static pdw g_cf_fdn;
static pdw cf_open(void)
{
    pdw hs, nm, td, top, p;
    g_cf_cs = g_cf_charset = g_cf_enc = g_cf_cid = g_cf_fdarr = g_cf_fdsel = g_cf_psize = g_cf_poff = 0;
    g_cf_curfd = CF_NOFD; g_dmatd = 0;
    if (P8(g_cf_base) != 1) return 0;
    hs = P8((pdw)(g_cf_base + 2));
    nm = (pdw)(g_cf_base + hs);
    td = ix_end(nm);
    if (!td || !ix_get(td, 0)) return 0;
    top = ix_end(td);                                    /* the String INDEX */
    if (!top) return 0;
    g_cf_str = top;
    g_cf_gsub = ix_end(top);
    if (!g_cf_gsub) return 0;
    if (!cf_dict(g_is, g_il, 0)) return 0;
    if (!g_cf_cs || !ix_count(g_cf_cs)) return 0;
    g_cf_ng = ix_count(g_cf_cs);
    g_cf_mat = (g_dmatd >= 16 && g_dmatd < 16384) ? g_dmatd : 1000;
    g_upem = g_cf_mat;
    if (g_cf_cid && g_cf_fdarr) g_cf_fdn = ix_count(g_cf_fdarr); else g_cf_fdn = 0;
    p = g_cf_psize;
    if (!g_cf_cid && !cf_private(p, g_cf_poff)) return 0;
    return 1;
}

/* the FD of a glyph (CID fonts), and make its Private current */
static pdw cf_fd_of(pdw gid)
{
    pdw f = (pdw)(g_cf_fdsel), fmt, n, i, p, fd = 0, first, nxt;
    if (!f) return 0;
    fmt = P8(f);
    if (fmt == 0) fd = P8((pdw)(f + 1 + gid));
    else if (fmt == 3) {
        n = P16((pdw)(f + 1)); p = (pdw)(f + 3);
        for (i = 0; i < n; i++) {
            first = P16((pdw)(p + i * 3)); nxt = P16((pdw)(p + i * 3 + 3));
            if (gid >= first && gid < nxt) { fd = P8((pdw)(p + i * 3 + 2)); break; }
        }
    }
    return fd;
}
static pdw cf_use_fd(pdw gid)
{
    pdw fd;
    if (!g_cf_cid) return 1;
    fd = cf_fd_of(gid);
    if (fd == g_cf_curfd) return 1;
    if (fd >= g_cf_fdn || !ix_get(g_cf_fdarr, fd)) return 0;
    g_cf_psize = g_cf_poff = 0;
    if (!cf_dict(g_is, g_il, 0)) return 0;
    g_cf_curfd = fd;
    return cf_private(g_cf_psize, g_cf_poff);
}

/* ---- charset, names, encoding ------------------------------------------------ */
static pdw cf_sid(pdw gid)                      /* the SID (or CID) of a glyph */
{
    pdw p, fmt, n, first, left, k;
    if (!gid) return 0;
    if (g_cf_charset <= 2) return gid;
    p = (pdw)(g_cf_base + g_cf_charset); fmt = P8(p++);
    n = (pdw)(gid - 1);
    if (fmt == 0) return P16((pdw)(p + n * 2));
    for (k = 0; k < g_cf_ng; k++) {
        first = P16(p); left = fmt == 1 ? P8((pdw)(p + 2)) : P16((pdw)(p + 2));
        p = (pdw)(p + (fmt == 1 ? 3 : 4));
        if (n <= left) return (pdw)(first + n);
        n = (pdw)(n - left - 1);
    }
    return 0;
}
static pdw cf_sid_is(pdw sid, const char *name)
{
    pdw i;
    if (sid < 229) return nm_eq(nm_nth(pdx_stdsid, sid), name);
    if (sid < 391 || !ix_get(g_cf_str, (pdw)(sid - 391))) return 0;
    for (i = 0; i < g_il; i++) if (name[i] != (char)P8((pdw)(g_is + i))) return 0;
    return !name[g_il];
}
static pdw cf_find_name(const char *name)
{
    pdw g;
    if (g_cf_cid) return 0;
    for (g = 1; g < g_cf_ng; g++) if (cf_sid_is(cf_sid(g), name)) return g;
    return 0;
}
static pdw cf_find_sid(pdw sid)
{
    pdw g;
    for (g = 1; g < g_cf_ng; g++) if (cf_sid(g) == sid) return g;
    return 0;
}
static pdw cf_cid_gid(pdw cid)                  /* CID -> glyph */
{
    if (!g_cf_cid) return cid < g_cf_ng ? cid : 0;
    if (!cid) return 0;
    return cf_find_sid(cid);
}
static pdw cf_enc_gid(pdw code)                 /* the font's built-in Encoding */
{
    pdw p, fmt, n, i, gid, first, left;
    if (g_cf_cid) return 0;
    if (g_cf_enc == 0) return pdx_stdenc[code & 255] ? cf_find_sid(pdx_stdenc[code & 255]) : 0;
    if (g_cf_enc == 1) return 0;
    p = (pdw)(g_cf_base + g_cf_enc); fmt = (pdw)(P8(p++) & 0x7F);
    n = P8(p++);
    if (fmt == 0) { for (i = 0; i < n; i++) if (P8((pdw)(p + i)) == code) return (pdw)(i + 1); return 0; }
    gid = 1;
    for (i = 0; i < n; i++) {
        first = P8(p++); left = P8(p++);
        if (code >= first && code <= first + left) return (pdw)(gid + code - first);
        gid = (pdw)(gid + left + 1);
    }
    return 0;
}

/* ---- the Type 2 machine -------------------------------------------------------- */
#define CS_STK 48
#define CS_DEP 10
static pdi g_cs[CS_STK];
static pdw g_csp;
static pdi g_cx, g_cy, g_cw;
static pdw g_csp_pos[CS_DEP], g_csp_end[CS_DEP];
static pdw g_stems, g_haswid, g_ctrans[32];
static pdw g_seac, g_sb, g_sa, g_sax, g_say;       /* a pending seac: base, accent, offset */
static pdi g_wid;                                   /* the glyph's advance, font units */
static pdi g_ox2, g_oy2;                            /* the glyph's own offset (seac)   */

static pdw cf_bias(pdw n) { return n < 1240 ? 107 : n < 33900 ? 1131 : 32768; }

static void cf_mv(pdi dx, pdi dy) { g_cx = (pdi)(g_cx + dx); g_cy = (pdi)(g_cy + dy); }
static void cf_ln(pdi dx, pdi dy) { cf_mv(dx, dy); ol_line((pdi)(g_cx + g_ox2), (pdi)(g_cy + g_oy2)); }
static void cf_cv(pdi a, pdi b, pdi c, pdi d, pdi e, pdi f)
{
    pdi x1 = (pdi)(g_cx + a), y1 = (pdi)(g_cy + b), x2 = (pdi)(x1 + c), y2 = (pdi)(y1 + d);
    g_cx = (pdi)(x2 + e); g_cy = (pdi)(y2 + f);
    ol_cubic((pdi)(x1 + g_ox2), (pdi)(y1 + g_oy2), (pdi)(x2 + g_ox2), (pdi)(y2 + g_oy2), (pdi)(g_cx + g_ox2), (pdi)(g_cy + g_oy2));
}
/* the first stack-clearing operator may carry the width as an extra first argument */
static void cf_wid(pdw extra)
{
    if (g_haswid) return;
    g_haswid = 1;
    if (extra && g_csp) {
        pdw i;
        g_wid = (pdi)(g_cf_nomw + g_cs[0]);
        for (i = 1; i < g_csp; i++) g_cs[i - 1] = g_cs[i];
        g_csp--;
    } else g_wid = (pdi)g_cf_defw;
}

/* run glyph `gid`'s charstring. Returns 1 ok, 0 refusal */
static pdw cf_run(pdw gid)
{
    pdw pos, end, fsp = 0, b, op, i, n, ok = 1, hb, count = 0, bias, nl, ng;
    pdi v, v2, s6;
    if (!cf_use_fd(gid)) return 0;                      /* first: it reads other INDEXes */
    if (!ix_get(g_cf_cs, gid)) return 0;
    pos = g_is; end = (pdw)(g_is + g_il);
    g_csp = 0; g_stems = 0; g_haswid = 0; g_seac = 0;
    g_cx = g_cy = 0; g_wid = 0;
    nl = ix_count(g_cf_lsub); ng = ix_count(g_cf_gsub);
    while (ok) {
        if (pos >= end) {
            if (!fsp) break;
            fsp--; pos = g_csp_pos[fsp]; end = g_csp_end[fsp]; continue;
        }
        if (++count > 6000) { ok = 0; break; }
        b = P8(pos++);
        if (b >= 32 || b == 28) {
            if (g_csp >= CS_STK) { ok = 0; break; }
            if (b == 28) { v = P16s(pos); pos += 2; }
            else if (b <= 246) v = (pdi)((pdi)b - 139);
            else if (b <= 250) { v = (pdi)(((pdi)b - 247) * 256 + (pdi)P8(pos) + 108); pos++; }
            else if (b <= 254) { v = (pdi)(-((pdi)b - 251) * 256 - (pdi)P8(pos) - 108); pos++; }
            else { v = P16s(pos); if (P8((pdw)(pos + 2)) & 0x80) v = (pdi)(v + 1); pos += 4; }
            g_cs[g_csp++] = v;
            continue;
        }
        op = b;
        if (b == 12) op = (pdw)(0x100 | P8(pos++));
        switch (op) {
        case 1: case 3: case 18: case 23:                    /* the stems */
            cf_wid((pdw)(g_csp & 1)); g_stems = (pdw)(g_stems + (g_csp >> 1)); g_csp = 0; break;
        case 19: case 20:                                   /* hintmask / cntrmask */
            cf_wid((pdw)(g_csp & 1)); g_stems = (pdw)(g_stems + (g_csp >> 1)); g_csp = 0;
            pos = (pdw)(pos + ((g_stems + 7) >> 3)); break;
        case 21: cf_wid((pdw)(g_csp > 2)); if (g_csp >= 2) { cf_mv(g_cs[g_csp - 2], g_cs[g_csp - 1]); ol_move((pdi)(g_cx + g_ox2), (pdi)(g_cy + g_oy2)); } g_csp = 0; break;
        case 22: cf_wid((pdw)(g_csp > 1)); if (g_csp >= 1) { cf_mv(g_cs[g_csp - 1], 0); ol_move((pdi)(g_cx + g_ox2), (pdi)(g_cy + g_oy2)); } g_csp = 0; break;
        case 4:  cf_wid((pdw)(g_csp > 1)); if (g_csp >= 1) { cf_mv(0, g_cs[g_csp - 1]); ol_move((pdi)(g_cx + g_ox2), (pdi)(g_cy + g_oy2)); } g_csp = 0; break;
        case 5: for (i = 0; i + 1 < g_csp; i += 2) cf_ln(g_cs[i], g_cs[i + 1]); g_csp = 0; break;
        case 6: case 7:
            hb = (pdw)(op == 6);
            for (i = 0; i < g_csp; i++) { if (hb) cf_ln(g_cs[i], 0); else cf_ln(0, g_cs[i]); hb = (pdw)!hb; }
            g_csp = 0; break;
        case 8: for (i = 0; i + 5 < g_csp; i += 6) cf_cv(g_cs[i], g_cs[i + 1], g_cs[i + 2], g_cs[i + 3], g_cs[i + 4], g_cs[i + 5]); g_csp = 0; break;
        case 24:                                            /* rcurveline */
            for (i = 0; i + 7 < g_csp; i += 6) cf_cv(g_cs[i], g_cs[i + 1], g_cs[i + 2], g_cs[i + 3], g_cs[i + 4], g_cs[i + 5]);
            if (i + 1 < g_csp) cf_ln(g_cs[i], g_cs[i + 1]);
            g_csp = 0; break;
        case 25:                                            /* rlinecurve */
            for (i = 0; i + 7 < g_csp; i += 2) cf_ln(g_cs[i], g_cs[i + 1]);
            if (i + 5 < g_csp) cf_cv(g_cs[i], g_cs[i + 1], g_cs[i + 2], g_cs[i + 3], g_cs[i + 4], g_cs[i + 5]);
            g_csp = 0; break;
        case 26:                                            /* vvcurveto */
            i = 0; v = 0;
            if (g_csp & 1) { v = g_cs[0]; i = 1; }
            for (; i + 3 < g_csp; i += 4) { cf_cv(v, g_cs[i], g_cs[i + 1], g_cs[i + 2], 0, g_cs[i + 3]); v = 0; }
            g_csp = 0; break;
        case 27:                                            /* hhcurveto */
            i = 0; v = 0;
            if (g_csp & 1) { v = g_cs[0]; i = 1; }
            for (; i + 3 < g_csp; i += 4) { cf_cv(g_cs[i], v, g_cs[i + 1], g_cs[i + 2], g_cs[i + 3], 0); v = 0; }
            g_csp = 0; break;
        case 30: case 31:                                   /* vhcurveto / hvcurveto */
            hb = (pdw)(op == 31);
            for (i = 0; i + 3 < g_csp; i += 4) {
                v = (pdi)(g_csp - i == 5 ? g_cs[i + 4] : 0);
                if (hb) cf_cv(g_cs[i], 0, g_cs[i + 1], g_cs[i + 2], v, g_cs[i + 3]);
                else cf_cv(0, g_cs[i], g_cs[i + 1], g_cs[i + 2], g_cs[i + 3], v);
                hb = (pdw)!hb;
            }
            g_csp = 0; break;
        case 10: case 29:                                   /* callsubr / callgsubr */
            if (!g_csp || fsp >= CS_DEP) { ok = 0; break; }
            v = g_cs[--g_csp];
            n = op == 10 ? nl : ng; bias = cf_bias(n);
            i = (pdw)(v + (pdi)bias);
            if (!ix_get(op == 10 ? g_cf_lsub : g_cf_gsub, i)) { ok = 0; break; }
            g_csp_pos[fsp] = pos; g_csp_end[fsp] = end; fsp++;
            pos = g_is; end = (pdw)(g_is + g_il);
            break;
        case 11: if (fsp) { fsp--; pos = g_csp_pos[fsp]; end = g_csp_end[fsp]; } break;
        case 14:                                            /* endchar (4 args: seac) */
            cf_wid((pdw)(g_csp == 1 || g_csp == 5));
            if (g_csp >= 4) { g_seac = 1; g_sax = (pdw)g_cs[g_csp - 4]; g_say = (pdw)g_cs[g_csp - 3]; g_sb = (pdw)g_cs[g_csp - 2]; g_sa = (pdw)g_cs[g_csp - 1]; }
            end = pos; fsp = 0; g_csp = 0; pos = end;
            goto done;
        case 0x122:                                         /* hflex */
            if (g_csp >= 7) {
                cf_cv(g_cs[0], 0, g_cs[1], g_cs[2], g_cs[3], 0);
                cf_cv(g_cs[4], 0, g_cs[5], (pdi)-g_cs[2], g_cs[6], 0);
            }
            g_csp = 0; break;
        case 0x123:                                         /* flex */
            if (g_csp >= 12) {
                cf_cv(g_cs[0], g_cs[1], g_cs[2], g_cs[3], g_cs[4], g_cs[5]);
                cf_cv(g_cs[6], g_cs[7], g_cs[8], g_cs[9], g_cs[10], g_cs[11]);
            }
            g_csp = 0; break;
        case 0x124:                                         /* hflex1 */
            if (g_csp >= 9) {
                cf_cv(g_cs[0], g_cs[1], g_cs[2], g_cs[3], g_cs[4], 0);
                cf_cv(g_cs[5], 0, g_cs[6], g_cs[7], g_cs[8], (pdi)-(g_cs[1] + g_cs[3] + g_cs[7]));
            }
            g_csp = 0; break;
        case 0x125:                                         /* flex1 */
            if (g_csp >= 11) {
                v = (pdi)(g_cs[0] + g_cs[2] + g_cs[4] + g_cs[6] + g_cs[8]);
                v2 = (pdi)(g_cs[1] + g_cs[3] + g_cs[5] + g_cs[7] + g_cs[9]);
                cf_cv(g_cs[0], g_cs[1], g_cs[2], g_cs[3], g_cs[4], g_cs[5]);
                if (pdx_abs(v) > pdx_abs(v2)) cf_cv(g_cs[6], g_cs[7], g_cs[8], g_cs[9], g_cs[10], (pdi)-v2);
                else cf_cv(g_cs[6], g_cs[7], g_cs[8], g_cs[9], (pdi)-v, g_cs[10]);
            }
            g_csp = 0; break;
        case 0x109: if (g_csp) g_cs[g_csp - 1] = pdx_abs(g_cs[g_csp - 1]); break;           /* abs */
        case 0x10A: if (g_csp >= 2) { g_cs[g_csp - 2] = (pdi)(g_cs[g_csp - 2] + g_cs[g_csp - 1]); g_csp--; } break;
        case 0x10B: if (g_csp >= 2) { g_cs[g_csp - 2] = (pdi)(g_cs[g_csp - 2] - g_cs[g_csp - 1]); g_csp--; } break;
        case 0x10C: if (g_csp >= 2 && g_cs[g_csp - 1]) { g_cs[g_csp - 2] = pdx_muldiv(g_cs[g_csp - 2], 1, g_cs[g_csp - 1]); g_csp--; } break;
        case 0x10E: if (g_csp) g_cs[g_csp - 1] = (pdi)-g_cs[g_csp - 1]; break;               /* neg */
        case 0x112: if (g_csp) g_csp--; break;                                               /* drop */
        case 0x118: if (g_csp >= 2) { g_cs[g_csp - 2] = pdx_muldiv(g_cs[g_csp - 2], g_cs[g_csp - 1], 1); g_csp--; } break;
        case 0x11C: if (g_csp >= 2) { v = g_cs[g_csp - 1]; g_cs[g_csp - 1] = g_cs[g_csp - 2]; g_cs[g_csp - 2] = v; } break;
        case 0x11B: if (g_csp && g_csp < CS_STK) { g_cs[g_csp] = g_cs[g_csp - 1]; g_csp++; } break;
        default: g_csp = 0; break;                          /* unknown / unused: clear */
        }
    }
done:
    (void)s6;
    ol_close();
    return ok;
}

/* the whole glyph with seac resolved: 1 ok */
static pdw cf_glyph(pdw gid)
{
    pdw sb, sa, ax, ay, bg, ag, r;
    pdi wid;
    ol_reset();
    g_ox2 = g_oy2 = 0;
    r = cf_run(gid);
    wid = g_wid;
    if (r && g_seac) {
        sb = g_sb; sa = g_sa; ax = g_sax; ay = g_say;
        bg = pdx_stdenc[sb & 255] ? cf_find_sid(pdx_stdenc[sb & 255]) : 0;
        ag = pdx_stdenc[sa & 255] ? cf_find_sid(pdx_stdenc[sa & 255]) : 0;
        g_seac = 0;
        if (bg) { g_ox2 = g_oy2 = 0; if (!cf_run(bg)) return 0; }
        if (ag) { g_ox2 = (pdi)ax; g_oy2 = (pdi)ay; g_seac = 0; if (!cf_run(ag)) return 0; }
    }
    g_wid = wid;
    return r;
}
