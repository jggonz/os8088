/* ============================================================================
 * os8088 - apps/pagedesk/hosttest/pdr_host.c
 *
 * The host side of PDREND's tests (PAGEDESK-SPEC 2.5, 20.3.6):
 *   1. the C TWINS of the assembly primitives in pdrprim.inc (the semantics
 *      the asm is held to: pdr_muldiv, pdr_fill1/4, pdr_glyph, pdr_imgrow);
 *   2. a framebuffer standing in for OSAPI_GFX_BLIT1/BLIT4;
 *   3. "PDENG-lite": enough of PDENG's verbs, over a PDF read from disk
 *      (qpdf --qdf --decode-level=all makes any Flate sample readable), for
 *      the interpreter to run against real files. It is a test double: the
 *      real engine's contract is PAGEDESK-SPEC 20.2.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../pdrend.h"

extern unsigned char pd_host_mem[];
#define MEM(seg, off) (pd_host_mem + (((unsigned long)(seg) << 4) + (off)))

/* ---- 1. the twins ----------------------------------------------------------- */
pdi pdr_muldiv(pdi a, pdi b, pdi c)
{
    long long v = (long long)a * b, q;
    if (c == 0) return 32767;
    q = v / c;                                /* truncates toward zero, as the asm's magnitude divide */
    if (q > 32767) q = 32767;
    if (q < -32767) q = -32767;
    return (pdi)q;
}
void pdr_fill1(pdw seg, pdw rowoff, pdi x0, pdi x1, pdw pat)
{
    unsigned char *r = MEM(seg, rowoff); int x;
    for (x = x0; x <= x1; x++) {
        unsigned char m = (unsigned char)(0x80 >> (x & 7));
        if ((pat >> (7 - (x & 7))) & 1) r[x >> 3] |= m; else r[x >> 3] &= (unsigned char)~m;
    }
}
void pdr_fill4(pdw seg, pdw rowoff, pdi x0, pdi x1, pdw col)
{
    unsigned char *r = MEM(seg, rowoff); int x;
    for (x = x0; x <= x1; x++) {
        if (x & 1) r[x >> 1] = (unsigned char)((r[x >> 1] & 0xF0) | (col & 15));
        else       r[x >> 1] = (unsigned char)((r[x >> 1] & 0x0F) | ((col & 15) << 4));
    }
}
void pdr_glyph(void)
{
    pdw r;
    for (r = 0; r < pdr_gp.nrow; r++) {
        unsigned char *dst = MEM(pdr_gp.bseg, pdr_gp.boff + r * pdr_gp.rowstep);
        const unsigned char *src = MEM(pdr_gp.gseg, pdr_gp.goff + r * pdr_gp.wbytes);
        int sh = 0, c, x;
        if (pdr_gp.shear) sh = ((int)(pdr_gp.shear - 1) - (int)(pdr_gp.skip + r) + 8) >> 2;
        for (c = 0; c < (int)pdr_gp.wpx; c++) {
            if (!(src[c >> 3] & (0x80 >> (c & 7)))) continue;
            x = pdr_gp.x + sh + c;
            if (pdr_gp.mode == 2) {
                if (x & 1) dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0xF0) | (pdr_gp.col & 15));
                else       dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0x0F) | ((pdr_gp.col & 15) << 4));
            } else if (pdr_gp.mode == 1) dst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
            else dst[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
        }
    }
}
static const unsigned char bayer_t[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
void pdr_imgrow(void)
{
    unsigned char *dst = MEM(pdr_ip.bseg, pdr_ip.boff);
    const unsigned char *src = MEM(pdr_ip.sseg, pdr_ip.soff);
    unsigned long acc = ((unsigned long)pdr_ip.xhi << 16) | pdr_ip.xlo;
    unsigned long step = ((unsigned long)pdr_ip.shi << 16) | pdr_ip.slo;
    int x, mode = pdr_ip.smode & 0xFF, flip = (pdr_ip.smode & 0x100) != 0;
    static const unsigned char pal[4] = { 0, 8, 7, 15 };
    for (x = pdr_ip.x0; x <= pdr_ip.x1; x++, acc += step) {
        unsigned idx = (unsigned)(acc >> 16), v, bit = 0;
        if (idx >= pdr_ip.sw) idx = pdr_ip.sw - 1;
        if (flip) idx = pdr_ip.sw - 1 - idx;
        if (mode == 0 || mode == 3) { bit = (src[idx >> 3] >> (7 - (idx & 7))) & 1; v = bit ? 255 : 0; }
        else if (mode == 1) v = src[idx];
        else v = (77u * src[idx * 3] + 150u * src[idx * 3 + 1] + 29u * src[idx * 3 + 2]) >> 8;
        if (mode == 3) {
            int paint = pdr_ip.inv ? bit : !bit;
            if (!paint) continue;
            if (pdr_ip.dmode == 1) {
                if (pdr_ip.col & 1) dst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
                else dst[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
            } else {
                if (x & 1) dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0xF0) | (pdr_ip.col & 15));
                else       dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0x0F) | ((pdr_ip.col & 15) << 4));
            }
            continue;
        }
        if (pdr_ip.inv) v = 255 - v;
        {
            unsigned m = bayer_t[(pdr_ip.dyrow << 2) | (x & 3)];
            if (pdr_ip.dmode == 1) {
                unsigned lev = (v * 17u) >> 8;
                if (lev > m) dst[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
                else dst[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
            } else {
                int t = (int)v + (int)(m * 5) - 37, q;
                if (t < 0) t = 0; if (t > 255) t = 255;
                q = (t + 42) / 85; if (q > 3) q = 3;
                if (x & 1) dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0xF0) | pal[q]);
                else       dst[x >> 1] = (unsigned char)((dst[x >> 1] & 0x0F) | (pal[q] << 4));
            }
        }
    }
}

/* ---- 2. the framebuffer ------------------------------------------------------ */
unsigned char pdh_fb[1024 * 1024];          /* one byte a pixel: 0..15 VGA index */
int pdh_fbw = 1024, pdh_fbh = 1024;
int pdh_blits;
void os88_gfx_lock(void) {}
void os88_gfx_unlock(void) {}
int  os88_wm_clip_set(void *win) { (void)win; return 0; }
pdi pdr_blit(pdw kind, pdw seg, pdw stride, pdi x, pdi y, pdw w, pdw rows)
{
    pdw r; int c;
    pdh_blits++;
    for (r = 0; r < rows; r++) {
        const unsigned char *s = MEM(seg, r * stride);
        if (y + (int)r < 0 || y + (int)r >= pdh_fbh) continue;
        for (c = 0; c < (int)w; c++) {
            int px = x + c, v;
            if (px < 0 || px >= pdh_fbw) continue;
            if (kind == 1) v = (s[c >> 3] >> (7 - (c & 7))) & 1 ? 15 : 0;
            else v = (c & 1) ? (s[c >> 1] & 15) : (s[c >> 1] >> 4);
            pdh_fb[(y + r) * pdh_fbw + px] = (unsigned char)v;
        }
    }
    return 0;
}

/* ---- 3. PDENG-lite ----------------------------------------------------------- */
enum { V_NULL, V_BOOL, V_INT, V_REAL, V_STR, V_NAME, V_ARR, V_DICT, V_REF, V_STRM };
struct val {
    int t; double d; int rn, rg;
    char *s; int slen;
    struct val **kid; char **key; int n;
    long soff, slen2;                       /* stream data */
};
static unsigned char *pdf; static long pdflen;
static long objoff[20000]; static struct val *objv[20000]; static int nobj;
static struct val *trailer;
static struct val nullv = { V_NULL };

static void skipws(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    for (;;) {
        while (p < pdf + pdflen && (isspace(*p) || *p == 0)) p++;
        if (p < pdf + pdflen && *p == '%') { while (p < pdf + pdflen && *p != '\n' && *p != '\r') p++; continue; }
        break;
    }
    *pp = p;
}
static int isdelim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }
static struct val *mk(int t) { struct val *v = calloc(1, sizeof *v); v->t = t; return v; }
static void addkid(struct val *v, const char *key, struct val *k)
{
    v->kid = realloc(v->kid, sizeof(void *) * (v->n + 1));
    v->key = realloc(v->key, sizeof(void *) * (v->n + 1));
    v->kid[v->n] = k; v->key[v->n] = key ? strdup(key) : NULL; v->n++;
}
static struct val *parse_val(const unsigned char **pp);
static struct val *parse_val(const unsigned char **pp)
{
    const unsigned char *p; struct val *v;
    skipws(pp); p = *pp;
    if (p >= pdf + pdflen) return mk(V_NULL);
    if (*p == '/') {
        char buf[256]; int n = 0; p++;
        while (p < pdf + pdflen && !isspace(*p) && !isdelim(*p) && n < 255) {
            if (*p == '#' && isxdigit(p[1]) && isxdigit(p[2])) { char h[3] = { p[1], p[2], 0 }; buf[n++] = (char)strtol(h, 0, 16); p += 3; }
            else buf[n++] = (char)*p++;
        }
        buf[n] = 0; v = mk(V_NAME); v->s = strdup(buf); v->slen = n; *pp = p; return v;
    }
    if (*p == '[') {
        v = mk(V_ARR); p++; *pp = p;
        for (;;) {
            skipws(pp); if (**pp == ']') { (*pp)++; break; }
            if (*pp >= pdf + pdflen) break;
            addkid(v, NULL, parse_val(pp));
        }
        return v;
    }
    if (*p == '<' && p[1] == '<') {
        v = mk(V_DICT); p += 2; *pp = p;
        for (;;) {
            struct val *k; skipws(pp);
            if ((*pp)[0] == '>' && (*pp)[1] == '>') { *pp += 2; break; }
            if (*pp >= pdf + pdflen) break;
            k = parse_val(pp);
            if (k->t != V_NAME) continue;
            addkid(v, k->s, parse_val(pp));
        }
        return v;
    }
    if (*p == '(') {
        char *buf = malloc(8192); int n = 0, depth = 1; p++;
        while (p < pdf + pdflen && depth) {
            if (*p == '\\') {
                p++;
                if (*p >= '0' && *p <= '7') { int o = 0, k = 0; while (k < 3 && *p >= '0' && *p <= '7') { o = o * 8 + (*p - '0'); p++; k++; } buf[n++] = (char)o; continue; }
                switch (*p) { case 'n': buf[n++] = '\n'; break; case 'r': buf[n++] = '\r'; break; case 't': buf[n++] = '\t'; break;
                    case 'b': buf[n++] = '\b'; break; case 'f': buf[n++] = '\f'; break; case '\n': break; case '\r': if (p[1] == '\n') p++; break;
                    default: buf[n++] = (char)*p; }
                p++; continue;
            }
            if (*p == '(') depth++;
            if (*p == ')') { depth--; if (!depth) { p++; break; } }
            buf[n++] = (char)*p++;
        }
        v = mk(V_STR); v->s = buf; v->slen = n; *pp = p; return v;
    }
    if (*p == '<') {
        char *buf = malloc(8192); int n = 0, hi = -1; p++;
        while (p < pdf + pdflen && *p != '>') {
            if (isxdigit(*p)) { int d = isdigit(*p) ? *p - '0' : (tolower(*p) - 'a' + 10); if (hi < 0) hi = d; else { buf[n++] = (char)(hi * 16 + d); hi = -1; } }
            p++;
        }
        if (hi >= 0) buf[n++] = (char)(hi * 16);
        if (*p == '>') p++;
        v = mk(V_STR); v->s = buf; v->slen = n; *pp = p; return v;
    }
    if (isdigit(*p) || *p == '-' || *p == '+' || *p == '.') {
        char *e; double d = strtod((const char *)p, &e);
        v = mk(memchr(p, '.', e - (char *)p) ? V_REAL : V_INT); v->d = d; p = (const unsigned char *)e;
        if (v->t == V_INT && d >= 0) {            /* "n g R" ? */
            const unsigned char *q = p; char *e2; long g;
            while (q < pdf + pdflen && isspace(*q)) q++;
            if (isdigit(*q)) {
                g = strtol((const char *)q, &e2, 10); q = (const unsigned char *)e2;
                while (q < pdf + pdflen && isspace(*q)) q++;
                if (*q == 'R' && !isalnum(q[1])) { v->t = V_REF; v->rn = (int)d; v->rg = (int)g; p = q + 1; }
            }
        }
        *pp = p; return v;
    }
    {   /* a keyword */
        char buf[64]; int n = 0;
        while (p < pdf + pdflen && !isspace(*p) && !isdelim(*p) && n < 63) buf[n++] = (char)*p++;
        buf[n] = 0; *pp = p;
        if (!strcmp(buf, "true")) { v = mk(V_BOOL); v->d = 1; return v; }
        if (!strcmp(buf, "false")) { v = mk(V_BOOL); return v; }
        if (!n) { (*pp)++; }
        return mk(V_NULL);
    }
}
static struct val *getobj(int n);
static struct val *resolve(struct val *v)
{
    int guard = 0;
    while (v && v->t == V_REF && guard++ < 16) v = getobj(v->rn);
    return v ? v : &nullv;
}
static struct val *dget(struct val *d, const char *k)
{
    int i; d = resolve(d);
    if (d->t != V_DICT && d->t != V_STRM) return &nullv;
    for (i = 0; i < d->n; i++) if (d->key[i] && !strcmp(d->key[i], k)) return d->kid[i];
    return &nullv;
}
static struct val *getobj(int n)
{
    const unsigned char *p; struct val *v;
    if (n <= 0 || n >= nobj || !objoff[n]) return &nullv;
    if (objv[n]) return objv[n];
    p = pdf + objoff[n];
    while (isdigit(*p)) p++; skipws(&p); while (isdigit(*p)) p++; skipws(&p); p += 3;   /* n g obj */
    v = parse_val(&p);
    skipws(&p);
    if (!strncmp((const char *)p, "stream", 6)) {
        long len = 0; struct val *l = dget(v, "Length");
        p += 6; if (*p == '\r') p++; if (*p == '\n') p++;
        len = (long)resolve(l)->d;
        v->soff = (long)(p - pdf); v->slen2 = len;
        if (v->t == V_DICT) v->t = V_STRM;
    }
    objv[n] = v;
    return v;
}
static int load_pdf(const char *path)
{
    FILE *f = fopen(path, "rb"); long i; const char *t;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); pdflen = ftell(f); fseek(f, 0, SEEK_SET);
    pdf = malloc(pdflen + 16); fread(pdf, 1, pdflen, f); fclose(f); memset(pdf + pdflen, 0, 16);
    for (i = 0; i < pdflen; i++) {
        if ((i == 0 || pdf[i - 1] == '\n' || pdf[i - 1] == '\r') && isdigit(pdf[i])) {
            char *e; long n = strtol((char *)pdf + i, &e, 10), g;
            if (*e == ' ') { g = strtol(e, &e, 10); (void)g; while (*e == ' ') e++;
                if (!strncmp(e, "obj", 3) && n > 0 && n < 20000) { objoff[n] = i; if (n >= nobj) nobj = (int)n + 1; } }
        }
    }
    t = NULL;
    for (i = pdflen - 8; i > 0; i--) if (!memcmp(pdf + i, "trailer", 7)) { t = (const char *)pdf + i + 7; break; }
    if (!t) return 0;
    { const unsigned char *p = (const unsigned char *)t; trailer = parse_val(&p); }
    return 1;
}

/* pages with inherited attributes */
struct pg { struct val *d, *media, *crop, *res, *rot; };
static struct pg pages[2048]; static int npages;
static void walk(struct val *node, struct val *med, struct val *crp, struct val *res, struct val *rot)
{
    struct val *n = resolve(node), *k, *t; int i;
    if (n->t != V_DICT) return;
    t = resolve(dget(n, "MediaBox")); if (t->t == V_ARR) med = t;
    t = resolve(dget(n, "CropBox")); if (t->t == V_ARR) crp = t;
    t = dget(n, "Resources"); if (resolve(t)->t == V_DICT) res = t;
    t = resolve(dget(n, "Rotate")); if (t->t == V_INT) rot = t;
    k = resolve(dget(n, "Kids"));
    if (k->t == V_ARR) { for (i = 0; i < k->n; i++) walk(k->kid[i], med, crp, res, rot); return; }
    if (npages < 2048) { pages[npages].d = n; pages[npages].media = med; pages[npages].crop = crp; pages[npages].res = res; pages[npages].rot = rot; npages++; }
}
int pdh_open(const char *path)
{
    struct val *root;
    memset(objoff, 0, sizeof objoff); memset(objv, 0, sizeof objv); nobj = 0; npages = 0;
    if (!load_pdf(path)) return 0;
    root = resolve(dget(trailer, "Root"));
    walk(dget(root, "Pages"), NULL, NULL, NULL, NULL);
    return npages;
}

/* streams */
struct st { const unsigned char *cur, *end; int inimg; };
static struct st stk[16]; static int nst;
static pdw xbseg;
#define XBP(off) MEM(xbseg, off)
static void put16(unsigned char *p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static int get16(const unsigned char *p) { return p[0] | (p[1] << 8); }

static int open_stream(struct val *s)
{
    s = resolve(s);
    if (s->t != V_STRM) return PDE_SYNTAX;
    if (dget(s, "Filter")->t != V_NULL) return PDE_FILTER;
    if (nst >= 16) return PDE_LIMIT;
    stk[nst].cur = pdf + s->soff; stk[nst].end = pdf + s->soff + s->slen2; nst++;
    return 0;
}
static pdw g_ctxpage;
static int do_strm_open(void)
{
    struct pdxb_strm *q = (struct pdxb_strm *)XBP(0);
    struct val *c, *el;
    if (q->objnum) return open_stream(getobj(q->objnum));
    if (q->page >= (pdw)npages) return PDE_RANGE;
    c = resolve(dget(pages[q->page].d, "Contents"));
    if (c->t == V_ARR) { if (q->index >= (pdw)c->n) return PDE_RANGE; el = c->kid[q->index]; return open_stream(el); }
    if (c->t == V_STRM) { if (q->index) return PDE_RANGE; return open_stream(c); }
    return PDE_RANGE;
}
static int tok_next(void)
{
    struct pdxb_token *t = (struct pdxb_token *)XBP(0);
    struct st *s;
    const unsigned char *p; int n = 0;
    unsigned char tmp[300];
    if (!nst) return PDE_RANGE;
    s = &stk[nst - 1];
    p = s->cur;
    t->len = 0; t->more = 0; memset(&t->num, 0, sizeof t->num);
    for (;;) {
        while (p < s->end && (isspace(*p) || *p == 0)) p++;
        if (p < s->end && *p == '%') { while (p < s->end && *p != '\n' && *p != '\r') p++; continue; }
        break;
    }
    if (p >= s->end) { t->kind = PDT_EOF; s->cur = p; return 0; }
    if (s->inimg) {                          /* skip raw data to EI */
        while (p + 1 < s->end && !(p[0] == 'E' && p[1] == 'I' && (isspace(p[-1])))) p++;
        s->inimg = 0;
    }
    if (*p == '/') {
        p++;
        while (p < s->end && !isspace(*p) && !isdelim(*p) && n < 255) {
            if (*p == '#' && isxdigit(p[1]) && isxdigit(p[2])) { char h[3] = { p[1], p[2], 0 }; tmp[n++] = (unsigned char)strtol(h, 0, 16); p += 3; }
            else tmp[n++] = *p++;
        }
        t->kind = PDT_NAME; t->len = (pdw)n; memcpy(t->text, tmp, n); t->text[n] = 0; s->cur = p; return 0;
    }
    if (*p == '[') { t->kind = PDT_ARRB; s->cur = p + 1; return 0; }
    if (*p == ']') { t->kind = PDT_ARRE; s->cur = p + 1; return 0; }
    if (p[0] == '<' && p[1] == '<') { t->kind = PDT_DICTB; s->cur = p + 2; return 0; }
    if (p[0] == '>' && p[1] == '>') { t->kind = PDT_DICTE; s->cur = p + 2; return 0; }
    if (*p == '(') {
        int depth = 1; p++;
        while (p < s->end && depth) {
            if (*p == '\\') {
                p++;
                if (*p >= '0' && *p <= '7') { int o = 0, k = 0; while (k < 3 && *p >= '0' && *p <= '7') { o = o * 8 + (*p - '0'); p++; k++; } if (n < 255) tmp[n++] = (unsigned char)o; continue; }
                { int ch = *p; switch (ch) { case 'n': ch = '\n'; break; case 'r': ch = '\r'; break; case 't': ch = '\t'; break; case 'b': ch = '\b'; break; case 'f': ch = '\f'; break; case '\n': p++; continue; case '\r': if (p[1] == '\n') p++; p++; continue; }
                  if (n < 255) tmp[n++] = (unsigned char)ch; p++; continue; }
            }
            if (*p == '(') depth++;
            if (*p == ')') { depth--; if (!depth) { p++; break; } }
            if (n < 255) tmp[n++] = *p;
            p++;
        }
        t->kind = PDT_STR; t->len = (pdw)n; memcpy(t->text, tmp, n); s->cur = p; return 0;
    }
    if (*p == '<') {
        int hi = -1; p++;
        while (p < s->end && *p != '>') { if (isxdigit(*p)) { int d = isdigit(*p) ? *p - '0' : tolower(*p) - 'a' + 10; if (hi < 0) hi = d; else { if (n < 255) tmp[n++] = (unsigned char)(hi * 16 + d); hi = -1; } } p++; }
        if (hi >= 0 && n < 255) tmp[n++] = (unsigned char)(hi * 16);
        if (p < s->end) p++;
        t->kind = PDT_STR; t->len = (pdw)n; memcpy(t->text, tmp, n); s->cur = p; return 0;
    }
    {   /* number or keyword */
        const unsigned char *q = p;
        while (q < s->end && !isspace(*q) && !isdelim(*q) && n < 255) tmp[n++] = *q++;
        if (n == 0) { s->cur = p + 1; t->kind = PDT_OP; t->len = 1; t->text[0] = (char)*p; t->text[1] = 0; return 0; }
        tmp[n] = 0; s->cur = q;
        if (isdigit(tmp[0]) || tmp[0] == '-' || tmp[0] == '+' || tmp[0] == '.') {
            struct pd_fx fx; pdm_fx_parse(&fx, (const char *)tmp, (pdw)n);
            t->kind = PDT_NUM; t->num = fx; return 0;
        }
        if (!strcmp((char *)tmp, "true")) t->kind = PDT_TRUE;
        else if (!strcmp((char *)tmp, "false")) t->kind = PDT_FALSE;
        else if (!strcmp((char *)tmp, "null")) t->kind = PDT_NULL;
        else {
            t->kind = PDT_OP; t->len = (pdw)n; memcpy(t->text, tmp, n); t->text[n] = 0;
            if (!strcmp((char *)tmp, "ID")) s->inimg = 1;
        }
        return 0;
    }
}
static void fx_of(struct pd_fx *r, double d)
{
    long long v = (long long)(d * 65536.0 + (d < 0 ? -0.5 : 0.5));
    r->lo = (pdw)(v & 0xFFFF); r->hi = (pdi)((v >> 16) & 0xFFFF);
}
static void rect_of(struct pd_rect *r, struct val *a)
{
    double x0, y0, x1, y1; a = resolve(a);
    if (a->t != V_ARR || a->n < 4) { memset(r, 0, sizeof *r); return; }
    x0 = resolve(a->kid[0])->d; y0 = resolve(a->kid[1])->d; x1 = resolve(a->kid[2])->d; y1 = resolve(a->kid[3])->d;
    r->x0 = (pd_pt)(x0 < x1 ? x0 * 16 : x1 * 16); r->x1 = (pd_pt)(x0 < x1 ? x1 * 16 : x0 * 16);
    r->y0 = (pd_pt)(y0 < y1 ? y0 * 16 : y1 * 16); r->y1 = (pd_pt)(y0 < y1 ? y1 * 16 : y0 * 16);
}
static int do_dict_get(void)
{
    struct pdxb_dictget *q = (struct pdxb_dictget *)XBP(0);
    struct val *cur = q->objnum ? getobj(q->objnum) : trailer;
    int i;
    for (i = 0; i < 3 && q->key[i][0]; i++) {
        cur = resolve(cur);
        if (q->key[i][0] == '#') { int idx = atoi(q->key[i] + 1); if (cur->t != V_ARR || idx >= cur->n) { cur = &nullv; break; } cur = cur->kid[idx]; }
        else cur = dget(cur, q->key[i]);
    }
    cur = resolve(cur);
    q->kind = PDK_NULL; q->len = 0; q->count = 0; memset(&q->num, 0, 4);
    switch (cur->t) {
    case V_BOOL: q->kind = PDK_BOOL; fx_of(&q->num, cur->d); break;
    case V_INT: q->kind = PDK_INT; fx_of(&q->num, cur->d); break;
    case V_REAL: q->kind = PDK_REAL; fx_of(&q->num, cur->d); break;
    case V_NAME: q->kind = PDK_NAME; q->len = (pdw)cur->slen; strncpy(q->text, cur->s, 127); q->text[127] = 0; break;
    case V_STR: q->kind = PDK_STR; q->len = (pdw)cur->slen; memcpy(q->text, cur->s, cur->slen > 127 ? 127 : cur->slen); break;
    case V_ARR: q->kind = PDK_ARR; q->count = (pdw)cur->n; break;
    case V_DICT: q->kind = PDK_DICT; q->count = (pdw)cur->n; break;
    case V_STRM: q->kind = PDK_STRM; q->count = (pdw)cur->n; break;
    }
    return 0;
}
static int do_res_lookup(void)
{
    struct pdxb_reslookup *q = (struct pdxb_reslookup *)XBP(0);
    struct val *res, *cat, *e;
    if (q->page != 0xFFFF) res = q->page < (pdw)npages ? pages[q->page].res : &nullv;
    else res = dget(getobj(q->resobj), "Resources");
    cat = dget(res, q->category);
    e = dget(cat, q->name);
    q->objnum = 0; q->gen = 0; q->kind = PDK_NULL;
    if (e->t == V_REF) { q->objnum = (pdw)e->rn; q->gen = (pdw)e->rg; }
    e = resolve(e);
    if (e->t == V_NULL) { fprintf(stderr, "reslookup miss %s/%s page %u\n", q->category, q->name, q->page); return PDE_NOTFOUND; }
    q->kind = e->t == V_STRM ? PDK_STRM : e->t == V_DICT ? PDK_DICT : PDK_NULL;
    return 0;
}
static int do_pageinfo(pdw page)
{
    struct pdxb_pageinfo *q = (struct pdxb_pageinfo *)XBP(0);
    if (page >= (pdw)npages) return PDE_RANGE;
    memset(q, 0, sizeof *q);
    rect_of(&q->media, pages[page].media);
    if (pages[page].crop) rect_of(&q->crop, pages[page].crop); else q->crop = q->media;
    q->rotate = pages[page].rot ? (pdw)(((int)pages[page].rot->d % 360 + 360) % 360) : 0;
    return 0;
}

struct pdr_gp pdr_gp;
struct pdr_ip pdr_ip;
pdw pdr_eng_ax, pdr_eng_dx, pdr_eng_off, pdr_eng_seg;
pdw pdr_eng(pdw verb, pdw bx, pdw cx, pdw dx)
{
    int e = 0;
    extern pdw pdh_xbseg(void);
    xbseg = pdh_xbseg();
    pdr_eng_ax = 0;
    switch (verb) {
    case PDV_ENG_STRM_OPEN: e = do_strm_open(); break;
    case PDV_ENG_STRM_CLOSE: if (nst) nst--; break;
    case PDV_ENG_TOKEN: e = tok_next(); break;
    case PDV_ENG_DICT_GET: e = do_dict_get(); break;
    case PDV_ENG_RES_LOOKUP: e = do_res_lookup(); break;
    case PDV_ENG_PAGEINFO: e = do_pageinfo(bx); break;
    case PDV_ENG_STRM_READ: {
        struct st *s; long n;
        if (!nst) return PDE_RANGE;
        s = &stk[nst - 1]; n = s->end - s->cur; if (n > (long)dx) n = dx;
        memcpy(MEM(bx, cx), s->cur, n); s->cur += n; pdr_eng_ax = (pdw)n; break; }
    default: e = PDE_RANGE;
    }
    (void)put16; (void)get16;
    return (pdw)e;
}
int pdh_npages(void) { return npages; }
int pdh_nstreams(void) { return nst; }
