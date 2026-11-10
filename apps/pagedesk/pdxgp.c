/* ============================================================================
 * os8088 - apps/pagedesk/pdxgp.c    PDXG's PDF syntax reader (20.22.9)
 *
 * A non-recursive parser: a stack of open containers and a stack of finished
 * values; every closing bracket turns the top of the second into a node.
 * Nodes, kid lists and string bytes live in fixed tables in the module's bss
 * (reset by every verb), so a hostile object can only fill a table. #included
 * by pdxg.c after pdxgm.c.
 * ==========================================================================*/
#define GN_NIL   0xFFFFu
#define GN_NUM   1
#define GN_NAME  2
#define GN_STR   3
#define GN_ARR   4
#define GN_DICT  5
#define GN_REF   6
#define GN_BOOL  7
#define GN_NULL  8
#define GN_OP    9

#define GX_NODES 320
#define GX_KIDS  640
#define GX_HEAP  2560
#define GX_TB    1100          /* the fetch buffer: one object's syntax    */
#define GX_DEPTH 8
#define GX_VS    96

struct gxn { pdb t; pdb fl; pdw a; pdw b; struct pd_fx n; };
static struct gxn gx_n[GX_NODES];
static pdw gx_nn;
static pdw gx_kid[GX_KIDS];
static pdw gx_nk;
static char gx_heap[GX_HEAP];
static pdw gx_nh;
static char gx_tb[GX_TB];
static pdw gx_ro, gx_rg;        /* the object the last gx_res stopped at   */
static pdw gx_perr;

static void gx_reset(void) { gx_nn = 0; gx_nk = 0; gx_nh = 0; gx_ro = 0; gx_rg = 0; gx_perr = 0; }

static pdw gx_new(pdb t)
{
    pdw i;
    if (gx_nn >= GX_NODES) { gx_perr = 1; return GN_NIL; }
    i = gx_nn++;
    gx_n[i].t = t; gx_n[i].fl = 0; gx_n[i].a = 0; gx_n[i].b = 0;
    gx_n[i].n.hi = 0; gx_n[i].n.lo = 0;
    return i;
}

static int gx_isws(int c) { return c == 32 || c == 9 || c == 10 || c == 13 || c == 12 || c == 0; }
static int gx_isdl(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%';
}
static int gx_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct gxc { pdb t; pdw vs0; };      /* an open container */
static struct gxc gx_cs[GX_DEPTH];
static pdw gx_vs[GX_VS];

/* Parse one value (or the object after "n g obj") out of p[0..len). Answers
 * the node, or GN_NIL. Stops at endobj and at "stream" (a stream object's
 * dictionary is the value). */
static pdw gx_parse(const char *p, pdw len)
{
    pdw i = 0, nvs = 0, ncs = 0, k, nd, h, j;
    int c, depth0;
    while (i < len) {
        c = (pdb)p[i];
        if (gx_isws(c)) { i++; continue; }
        if (c == '%') { while (i < len && p[i] != 10 && p[i] != 13) i++; continue; }
        if (c == '<' && i + 1 < len && p[i + 1] == '<') {
            if (ncs >= GX_DEPTH) return GN_NIL;
            gx_cs[ncs].t = GN_DICT; gx_cs[ncs].vs0 = nvs; ncs++;
            i += 2; continue;
        }
        if (c == '>' && i + 1 < len && p[i + 1] == '>') {
            i += 2;
            if (!ncs || gx_cs[ncs - 1].t != GN_DICT) return GN_NIL;
            ncs--;
            nd = gx_new(GN_DICT);
            if (nd == GN_NIL) return GN_NIL;
            k = nvs - gx_cs[ncs].vs0;
            if (gx_nk + k > GX_KIDS) return GN_NIL;
            gx_n[nd].a = gx_nk; gx_n[nd].b = k & ~1u;
            for (j = 0; j < (k & ~1u); j++) gx_kid[gx_nk++] = gx_vs[gx_cs[ncs].vs0 + j];
            nvs = gx_cs[ncs].vs0;
            if (nvs >= GX_VS) return GN_NIL;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (c == '[') {
            if (ncs >= GX_DEPTH) return GN_NIL;
            gx_cs[ncs].t = GN_ARR; gx_cs[ncs].vs0 = nvs; ncs++;
            i++; continue;
        }
        if (c == ']') {
            i++;
            if (!ncs || gx_cs[ncs - 1].t != GN_ARR) return GN_NIL;
            ncs--;
            nd = gx_new(GN_ARR);
            if (nd == GN_NIL) return GN_NIL;
            k = nvs - gx_cs[ncs].vs0;
            if (gx_nk + k > GX_KIDS) return GN_NIL;
            gx_n[nd].a = gx_nk; gx_n[nd].b = k;
            for (j = 0; j < k; j++) gx_kid[gx_nk++] = gx_vs[gx_cs[ncs].vs0 + j];
            nvs = gx_cs[ncs].vs0;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (c == '/') {
            i++;
            nd = gx_new(GN_NAME);
            if (nd == GN_NIL) return GN_NIL;
            gx_n[nd].a = gx_nh;
            while (i < len && !gx_isws((pdb)p[i]) && !gx_isdl((pdb)p[i])) {
                c = (pdb)p[i];
                if (c == '#' && i + 2 < len && gx_hex((pdb)p[i + 1]) >= 0 && gx_hex((pdb)p[i + 2]) >= 0) {
                    c = gx_hex((pdb)p[i + 1]) * 16 + gx_hex((pdb)p[i + 2]);
                    i += 2;
                }
                if (gx_nh + 1 < GX_HEAP) gx_heap[gx_nh++] = (char)c;
                i++;
            }
            gx_n[nd].b = gx_nh - gx_n[nd].a;
            if (gx_nh < GX_HEAP) gx_heap[gx_nh++] = 0;
            if (nvs >= GX_VS) return GN_NIL;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (c == '(') {
            int lvl = 1;
            i++;
            nd = gx_new(GN_STR);
            if (nd == GN_NIL) return GN_NIL;
            gx_n[nd].a = gx_nh;
            while (i < len && lvl > 0) {
                c = (pdb)p[i++];
                if (c == '\\' && i < len) {
                    c = (pdb)p[i++];
                    if (c == 'n') c = 10; else if (c == 'r') c = 13; else if (c == 't') c = 9;
                    else if (c == 'b') c = 8; else if (c == 'f') c = 12;
                    else if (c >= '0' && c <= '7') {
                        int v = c - '0', q = 0;
                        while (q < 2 && i < len && p[i] >= '0' && p[i] <= '7') { v = v * 8 + (p[i] - '0'); i++; q++; }
                        c = v & 255;
                    } else if (c == 10) continue;
                    else if (c == 13) { if (i < len && p[i] == 10) i++; continue; }
                } else if (c == '(') lvl++;
                else if (c == ')') { lvl--; if (!lvl) break; }
                if (gx_nh < GX_HEAP) gx_heap[gx_nh++] = (char)c;
            }
            gx_n[nd].b = gx_nh - gx_n[nd].a;
            if (nvs >= GX_VS) return GN_NIL;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (c == '<') {
            int hi = -1, v;
            i++;
            nd = gx_new(GN_STR);
            if (nd == GN_NIL) return GN_NIL;
            gx_n[nd].a = gx_nh;
            while (i < len && p[i] != '>') {
                v = gx_hex((pdb)p[i++]);
                if (v < 0) continue;
                if (hi < 0) hi = v;
                else { if (gx_nh < GX_HEAP) gx_heap[gx_nh++] = (char)(hi * 16 + v); hi = -1; }
            }
            if (hi >= 0 && gx_nh < GX_HEAP) gx_heap[gx_nh++] = (char)(hi * 16);
            i++;
            gx_n[nd].b = gx_nh - gx_n[nd].a;
            if (nvs >= GX_VS) return GN_NIL;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (c == ')' || c == '>' || c == '{' || c == '}') { i++; continue; }
        /* a regular token: number or keyword */
        h = i;
        while (i < len && !gx_isws((pdb)p[i]) && !gx_isdl((pdb)p[i])) i++;
        k = i - h;
        c = (pdb)p[h];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            nd = gx_new(GN_NUM);
            if (nd == GN_NIL) return GN_NIL;
            if (c == '+') { h++; k--; }
            if (pdm_fx_parse(&gx_n[nd].n, p + h, k) != PDE_OK) { gx_n[nd].n.hi = 0; gx_n[nd].n.lo = 0; }
            /* an integer: remember it for "n g R" (fl = 1) */
            gx_n[nd].fl = 1;
            for (j = 0; j < k; j++) if (p[h + j] == '.') gx_n[nd].fl = 0;
            gx_n[nd].a = (pdw)gx_n[nd].n.hi;
            if (nvs >= GX_VS) return GN_NIL;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (k == 1 && c == 'R' && nvs >= 2 &&
            gx_n[gx_vs[nvs - 1]].t == GN_NUM && gx_n[gx_vs[nvs - 2]].t == GN_NUM) {
            nd = gx_new(GN_REF);
            if (nd == GN_NIL) return GN_NIL;
            gx_n[nd].a = (pdw)gx_n[gx_vs[nvs - 2]].n.hi;
            gx_n[nd].b = (pdw)gx_n[gx_vs[nvs - 1]].n.hi;
            nvs -= 2;
            gx_vs[nvs++] = nd;
            continue;
        }
        if (k == 3 && c == 'o' && p[h + 1] == 'b' && p[h + 2] == 'j') {     /* n g obj */
            if (!ncs) nvs = 0;
            continue;
        }
        if (k == 6 && c == 's' && p[h + 1] == 't' && p[h + 2] == 'r') break;  /* stream */
        if (k == 6 && c == 'e' && p[h + 1] == 'n' && p[h + 2] == 'd' && p[h + 3] == 'o') break;
        if (k == 4 && c == 't') { nd = gx_new(GN_BOOL); if (nd == GN_NIL) return GN_NIL; gx_n[nd].a = 1; }
        else if (k == 5 && c == 'f') { nd = gx_new(GN_BOOL); if (nd == GN_NIL) return GN_NIL; }
        else if (k == 4 && c == 'n') { nd = gx_new(GN_NULL); if (nd == GN_NIL) return GN_NIL; }
        else { nd = gx_new(GN_OP); if (nd == GN_NIL) return GN_NIL; }
        if (nvs >= GX_VS) return GN_NIL;
        gx_vs[nvs++] = nd;
    }
    depth0 = ncs;
    if (depth0 || !nvs) return GN_NIL;
    return gx_vs[nvs - 1];
}

/* ---- accessors ----------------------------------------------------------- */
static int gx_ty(pdw n) { return n == GN_NIL ? 0 : gx_n[n].t; }

static int gx_nameis(pdw n, const char *s)
{
    pdw k = 0;
    if (gx_ty(n) != GN_NAME) return 0;
    while (s[k]) { if (k >= gx_n[n].b || gx_heap[gx_n[n].a + k] != s[k]) return 0; k++; }
    return k == gx_n[n].b;
}

static pdw gx_dget(pdw d, const char *key)
{
    pdw i, k, e;
    if (gx_ty(d) != GN_DICT) return GN_NIL;
    e = gx_n[d].b;
    for (i = 0; i + 1 < e; i += 2) {
        k = gx_kid[gx_n[d].a + i];
        if (gx_nameis(k, key)) return gx_kid[gx_n[d].a + i + 1];
    }
    return GN_NIL;
}
static pdw gx_alen(pdw a) { return gx_ty(a) == GN_ARR ? gx_n[a].b : 0; }
static pdw gx_aget(pdw a, pdw i)
{
    if (gx_ty(a) != GN_ARR || i >= gx_n[a].b) return GN_NIL;
    return gx_kid[gx_n[a].a + i];
}

/* the object syntax of n g, parsed on top of what is there; GN_NIL on any failure */
static pdw gx_fetch_parse(pdw obj, pdw gen)
{
    pdw got = gx_fetch(obj, gen, gx_tb, GX_TB);
    if (!got) return GN_NIL;
    return gx_parse(gx_tb, got);
}
/* follow references (at most 8 hops); remembers the last object number */
static pdw gx_res(pdw n)
{
    pdw hops = 0;
    gx_ro = 0; gx_rg = 0;
    while (n != GN_NIL && gx_n[n].t == GN_REF) {
        if (hops++ >= 8) return GN_NIL;
        gx_ro = gx_n[n].a; gx_rg = gx_n[n].b;
        n = gx_fetch_parse(gx_ro, gx_rg);
    }
    return n;
}
static pdw gx_dres(pdw d, const char *key) { return gx_res(gx_dget(d, key)); }
static pdw gx_ares(pdw a, pdw i) { return gx_res(gx_aget(a, i)); }

static void gx_num(pdw n, struct pd_fx *r)
{
    r->hi = 0; r->lo = 0;
    if (n == GN_NIL) return;
    if (gx_n[n].t == GN_NUM) { r->hi = gx_n[n].n.hi; r->lo = gx_n[n].n.lo; }
    else if (gx_n[n].t == GN_BOOL) r->hi = (pdi)gx_n[n].a;
}
static pdi gx_int(pdw n, pdi dflt)
{
    if (gx_ty(n) != GN_NUM) return dflt;
    return gx_n[n].n.hi;
}
/* up to `max` numbers of array a (dereferenced elements) into out; answers how many */
static pdw gx_nums(pdw a, struct pd_fx *out, pdw max)
{
    pdw i, n = gx_alen(a);
    if (n > max) n = max;
    for (i = 0; i < n; i++) gx_num(gx_ares(a, i), &out[i]);
    return n;
}
