/* ============================================================================
 * os8088 - apps/pagedesk/pdwser.c      the streaming serialiser (20.5.1-20.5.3)
 *
 * #included by pdwrt.c; nothing here is a verb.
 *
 *   pb_*   compose a line in a near buffer (g_ln); full lines go to pw_n
 *   pw_n   the ONE byte sink: counts (g_mode COUNT) or stages in the writer
 *          buffer's output region and flushes WHOLE CLUSTERS to the file
 *   pw_obj the object table: number, generation, 32-bit offset
 *
 * COUNT mode exists because a stream's /Length precedes its bytes: every
 * generator runs twice, once counting and once emitting, so no stream is ever
 * buffered whole and no length is an indirect object.
 * ==========================================================================*/

/* ---- state -------------------------------------------------------------- */
static pdw g_xb;                 /* the exchange block                       */
static pdw g_eng_off, g_eng_seg;
static pdw g_wseg, g_wkb;        /* the writer buffer claim                  */
static pdw g_outcap;             /* bytes of the output region               */
static pdw g_clu;                /* the volume's cluster, bytes              */
static pdw g_wpos;               /* staged bytes                             */
static struct pd_u32 g_pos;      /* file offset of the next byte (EMIT)      */
static struct pd_u32 g_cnt;      /* bytes counted (COUNT)                    */
static pdw g_mode;               /* 0 EMIT, 1 COUNT                          */
static pdw g_err;                /* sticky PDE_*                             */
static pdw g_tn, g_tmax;         /* object table fill / capacity             */
static pdw g_created;            /* the temp file exists                     */
static pdw g_io;                 /* disk transfers made (a slice ends after one, 15.2) */
static pdw g_crcleft, g_crc;     /* running CRC over the first bytes (ID)    */
static char g_tmp[14];           /* the temp file's name                     */
static char g_ln[PDW_MAXLN + 8]; /* the line under construction              */
static pdw g_lnn;
static char g_nb[16];            /* a number                                 */
static char g_sc[64];            /* scratch for a far peek                   */

char pdw_date[24];
char pdw_author[48];

/* ---- tiny string helpers (there is no C library here) ------------------- */
/* struct pd_u32 assignment is a `rep movsb` through the kernel's ES: copy by field */
static void u32cp(struct pd_u32 *d, const struct pd_u32 *s) { d->lo = s->lo; d->hi = s->hi; }
static pdw pw_len(const char *s) { pdw n = 0; while (s[n]) n++; return n; }
static void pw_cpy(char *d, const char *s, pdw n) { while (n--) *d++ = *s++; }

/* ---- the sink ----------------------------------------------------------- */
static void pw_flush(pdw last)
{
    pdw n = g_wpos, r;
    if (g_err) return;
    if (!last) n -= n % g_clu;
    if (n == 0) return;
    if (g_created) r = pdw_io_append(g_tmp, g_wseg, 0, n);
    else           r = pdw_io_create(g_tmp, g_wseg, 0, n);
    if (r) { g_err = r; return; }
    g_created = 1; g_io++;
    if (g_wpos > n) pd_fcopy(g_wseg, 0, g_wseg, n, (pdw)(g_wpos - n));
    g_wpos = (pdw)(g_wpos - n);
}

static void pw_n(const char *s, pdw n)
{
    pdw room, k;
    if (g_err) return;
    if (g_mode) { pdm_u32_add16(&g_cnt, n); return; }
    if (g_crcleft) {
        k = n < g_crcleft ? n : g_crcleft;
        g_crc = pdm_crc16(g_crc, s, k);
        g_crcleft = (pdw)(g_crcleft - k);
    }
    while (n && !g_err) {
        room = (pdw)(g_outcap - g_wpos);
        if (room == 0) { pw_flush(0); continue; }
        k = n < room ? n : room;
        pd_fcopy_out(s, g_wseg, g_wpos, k);
        g_wpos = (pdw)(g_wpos + k); s += k; n = (pdw)(n - k);
        pdm_u32_add16(&g_pos, k);
    }
}

/* bytes already in a far segment (a Contents stream the primary supplied) */
static void pw_far(pdw seg, pdw off, pdw n)
{
    pdw k;
    if (g_err) return;
    if (g_mode) { pdm_u32_add16(&g_cnt, n); return; }
    while (n && !g_err) {
        if (g_outcap == g_wpos) { pw_flush(0); continue; }
        k = (pdw)(g_outcap - g_wpos); if (k > n) k = n;
        if (g_crcleft) { /* the ID hashes the head of far data only: 64 bytes */
            pdw h = k < 64 ? k : 64;
            pd_fcopy_in(seg, off, g_sc, h);
            g_crc = pdm_crc16(g_crc, g_sc, h);
            g_crcleft = 0;
        }
        pd_fcopy(g_wseg, g_wpos, seg, off, k);
        g_wpos = (pdw)(g_wpos + k); off = (pdw)(off + k); n = (pdw)(n - k);
        pdm_u32_add16(&g_pos, k);
    }
}

/* ---- the line buffer ---------------------------------------------------- */
static void pb_flush(void)
{
    if (g_lnn) { pw_n(g_ln, g_lnn); g_lnn = 0; }
}
static void pb_c(char c)
{
    if (g_lnn >= PDW_MAXLN) pb_flush();
    g_ln[g_lnn++] = c;
}
static void pb_s(const char *s)
{
    while (*s) pb_c(*s++);
}
static void pb_n(const char *s, pdw n)
{
    while (n--) pb_c(*s++);
}
static pdw pw_utoa(char *p, pdw v)
{
    static char t[6]; pdw k = 0, i;
    do { t[k++] = (char)('0' + v % 10); v = v / 10; } while (v);
    for (i = 0; i < k; i++) p[i] = t[k - 1 - i];
    return k;
}
static void pb_u(pdw v)
{
    pdw k = pw_utoa(g_nb, v);
    pb_n(g_nb, k);
}
/* a 32-bit decimal (offsets, lengths): pdm_u32_todec, width zero-pads */
static void pb_u32(const struct pd_u32 *a, pdw width)
{
    static char b[12];
    pdw k = pdm_u32_todec(b, a, width);
    pb_n(b, k);
}
/* a pt16 value as PDF text: n/16 with at most 4 fraction digits, no trailing
   zeros ("12", "0.0625", "-3.5"). Exact, and 16-bit only. */
static void pb_pt(pd_pt v)
{
    pdw a, ip, fr, k;
    if (v < 0) { pb_c('-'); a = (pdw)(-(pdi)v); } else a = (pdw)v;
    ip = (pdw)(a >> 4);
    fr = (pdw)((a & 15) * 625);
    pb_u(ip);
    if (fr) {
        pb_c('.');
        k = 1000;
        while (fr) { pb_c((char)('0' + fr / k)); fr = (pdw)(fr % k); k = (pdw)(k / 10); if (!k) break; }
    }
}
static char pw_hex(pdw v) { v &= 15; return (char)(v < 10 ? '0' + v : 'A' + v - 10); }
static void pb_hex2(pdw v) { pb_c(pw_hex(v >> 4)); pb_c(pw_hex(v)); }
static void pb_hex4(pdw v) { pb_hex2(v >> 8); pb_hex2(v & 255); }

/* a name: '/' then the characters, anything irregular as #xx (PDF 7.3.5) */
static void pb_name(const char *s)
{
    pdb c;
    pb_c('/');
    while (*s) {
        c = (pdb)*s++;
        if (c <= 32 || c >= 127 || c == '#' || c == '/' || c == '%' || c == '(' || c == ')'
            || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}') {
            pb_c('#'); pb_hex2(c);
        } else pb_c((char)c);
    }
}

/* a literal string for CONTENT STREAMS (WinAnsi text shown with a simple
   font): backslash escapes, bytes >= 0x80 and controls as \ooo */
static void pb_wintext(const char *t, pdw n)
{
    pdb c;
    pb_c('(');
    while (n--) {
        c = (pdb)*t++;
        if (c == '(' || c == ')' || c == '\\') { pb_c('\\'); pb_c((char)c); }
        else if (c < 32 || c >= 127) { pb_c('\\'); pb_c((char)('0' + (c >> 6))); pb_c((char)('0' + ((c >> 3) & 7))); pb_c((char)('0' + (c & 7))); }
        else pb_c((char)c);
    }
    pb_c(')');
}

/* Windows-1252 0x80..0x9F in Unicode (the rest of 0x80..0xFF is Latin-1) */
static const pdw pw_cp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };

/* a PDF TEXT STRING (Contents, T, Subj): plain ASCII as a literal, anything
   with a byte >= 0x80 as UTF-16BE with a BOM in a hex string (PDF 7.9.2.2) */
static void pb_textstr(const char *t, pdw n)
{
    pdw i, hi = 0; pdb c; pdw u;
    for (i = 0; i < n; i++) if ((pdb)t[i] >= 0x80) { hi = 1; break; }
    if (!hi) {
        pb_c('(');
        for (i = 0; i < n; i++) {
            c = (pdb)t[i];
            if (c == '(' || c == ')' || c == '\\') { pb_c('\\'); pb_c((char)c); }
            else if (c == '\r') pb_s("\\r");
            else if (c == '\n') pb_s("\\n");
            else if (c == '\t') pb_s("\\t");
            else if (c < 32 || c == 127) { pb_c('\\'); pb_c((char)('0' + (c >> 6))); pb_c((char)('0' + ((c >> 3) & 7))); pb_c((char)('0' + (c & 7))); }
            else pb_c((char)c);
        }
        pb_c(')');
        return;
    }
    pb_s("<FEFF");
    for (i = 0; i < n; i++) {
        c = (pdb)t[i];
        u = (c >= 0x80 && c < 0xA0) ? pw_cp1252[c - 0x80] : (pdw)c;
        pb_hex4(u);
    }
    pb_c('>');
}

/* ---- the object table: the writer buffer past the output region ---------- */
static void pw_tadd(pdw num, pdw gen)
{
    pdw at;
    if (g_mode || g_err) return;
    if (g_tn >= g_tmax) { g_err = PDE_LIMIT; return; }
    at = (pdw)(g_outcap + g_tn * PDW_TENT);
    pd_fpokew(g_wseg, at, num);
    pd_fpokew(g_wseg, (pdw)(at + 2), gen);
    pd_fpokew(g_wseg, (pdw)(at + 4), g_pos.lo);
    pd_fpokew(g_wseg, (pdw)(at + 6), g_pos.hi);
    g_tn++;
}
static void pw_obj(pdw num, pdw gen)
{
    pb_flush();
    pw_tadd(num, gen);
    pb_u(num); pb_c(' '); pb_u(gen); pb_s(" obj\n");
}
static void pw_endobj(void) { pb_s("endobj\n"); }

/* ---- the buffer's geometry: the output region gets 2 clusters or 4 KiB,
   whichever is larger; what follows it is the object table ---------------- */
static pdw pw_setup(pdw wseg, pdw wkb)
{
    pdw cap, tb;
    if (wkb > 63) wkb = 63;
    g_wseg = wseg; g_wkb = wkb;
    g_clu = pdw_io_clu();
    if (g_clu < 512) g_clu = 512;
    cap = PDW_OUTMIN;
    if ((pdw)(2 * g_clu) > cap) cap = (pdw)(2 * g_clu);
    if (g_clu > 16384) return PDE_NOMEM;
    if ((pdw)(cap / 1024 + 1) >= wkb) return PDE_NOMEM;
    g_outcap = cap;
    tb = (pdw)(wkb * 1024u - cap);               /* wkb <= 63 so this fits   */
    g_tmax = (pdw)(tb / PDW_TENT);
    g_wpos = 0; g_lnn = 0; g_tn = 0; g_err = 0; g_mode = 0; g_created = 0;
    g_crcleft = 4096; g_crc = 0xFFFF;
    pdm_u32_set(&g_pos, 0, 0); pdm_u32_set(&g_cnt, 0, 0);
    return PDE_OK;
}
