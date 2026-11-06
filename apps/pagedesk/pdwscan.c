/* ============================================================================
 * os8088 - apps/pagedesk/pdwscan.c     reading just enough of an object's text
 *                                      to PATCH it (14.2 step 2, 20.5.7)
 *
 * #included by pdwrt.c. An incremental save revises a page (or its /Annots
 * array, or the catalog) by writing the object again with one value changed.
 * PDENG hands over the object's bytes (PDV_ENG_RAW_COPY) and this file finds
 * the value to change: it walks a dictionary's keys at depth 1, skipping
 * every value by its syntax (names, numbers, "n g R", strings with nested
 * parentheses and escapes, hex strings, arrays, dictionaries) without
 * interpreting any of them. Comments count as white space. It never recurses
 * deeper than SC_DEPTH, and every walk is bounded by the buffer: a malformed
 * object ends in 0xFFFF (PDE_SYNTAX), never in a read past the end.
 * ==========================================================================*/
#define SC_BAD   0xFFFFu
#define SC_DEPTH 16

static char g_pg[PDW_PGBUF + 616];        /* the object being patched          */
static pdw  sc_n;                       /* its length                        */
static pdw  sc_kpos, sc_vs, sc_ve, sc_close;   /* dict_find's answers         */

static pdw sc_ws(pdb c) { return c == 32 || c == 9 || c == 10 || c == 13 || c == 12 || c == 0; }
static pdw sc_delim(pdb c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']'
        || c == '{' || c == '}' || c == '/' || c == '%';
}
static pdw sc_skip(pdw p)               /* white space and comments          */
{
    while (p < sc_n) {
        if (sc_ws((pdb)g_pg[p])) p++;
        else if (g_pg[p] == '%') { while (p < sc_n && g_pg[p] != '\n' && g_pg[p] != '\r') p++; }
        else break;
    }
    return p;
}
static pdw sc_tok(pdw p)                /* end of a regular token            */
{
    while (p < sc_n && !sc_ws((pdb)g_pg[p]) && !sc_delim((pdb)g_pg[p])) p++;
    return p;
}
static pdw sc_isnum(pdw a, pdw b)       /* all digits, non-empty             */
{
    if (a >= b) return 0;
    while (a < b) { if (g_pg[a] < '0' || g_pg[a] > '9') return 0; a++; }
    return 1;
}
static pdb sc_stk[SC_DEPTH + 2];        /* open containers: 2 dict, 1 array  */
static pdw sc_val(pdw p, pdw depth)     /* end of the value starting at p    */
{
    pdw q, r, nest, n = 0;
    for (;;) {                          /* iterative (D21): no recursion      */
        if (n) {
            p = sc_skip(p);
            if (p >= sc_n) return SC_BAD;
            if (sc_stk[n - 1] == 2) {
                if (g_pg[p] == '>') {
                    if (!(p + 1 < sc_n && g_pg[p + 1] == '>')) return SC_BAD;
                    p += 2; n--;
                    if (!n) return p;
                    continue;
                }
                if (g_pg[p] != '/') return SC_BAD;
                p = sc_skip(sc_tok((pdw)(p + 1)));
            } else if (g_pg[p] == ']') {
                p++; n--;
                if (!n) return p;
                continue;
            }
        }
        if (p >= sc_n || (pdw)(depth + n) > SC_DEPTH) return SC_BAD;
        if (g_pg[p] == '<' && p + 1 < sc_n && g_pg[p + 1] == '<') { p += 2; sc_stk[n++] = 2; continue; }
        if (g_pg[p] == '[') { p++; sc_stk[n++] = 1; continue; }
        if (g_pg[p] == '<') {
            while (p < sc_n && g_pg[p] != '>') p++;
            if (p >= sc_n) return SC_BAD;
            p++;
        } else if (g_pg[p] == '(') {
            nest = 1; p++;
            while (p < sc_n && nest) {
                if (g_pg[p] == '\\') p++;
                else if (g_pg[p] == '(') nest++;
                else if (g_pg[p] == ')') nest--;
                p++;
            }
            if (nest) return SC_BAD;
        } else if (g_pg[p] == '/') p = sc_tok((pdw)(p + 1));
        else if (sc_delim((pdb)g_pg[p])) return SC_BAD;
        else {
            q = sc_tok(p);
            if (q == p) return SC_BAD;
            if (sc_isnum(p, q)) {                   /* maybe "n g R"             */
                r = sc_skip(q);
                if (sc_isnum(r, sc_tok(r))) {
                    r = sc_skip(sc_tok(r));
                    if (r < sc_n && g_pg[r] == 'R' && (r + 1 >= sc_n || sc_ws((pdb)g_pg[r + 1]) || sc_delim((pdb)g_pg[r + 1])))
                        q = (pdw)(r + 1);
                }
            }
            p = q;
        }
        if (!n) return p;
    }
}
/* the object number and generation of an "n g R" at [a,b), else 0 */
static pdw sc_ref(pdw a, pdw b, pdw *gen)
{
    pdw n = 0, g = 0, big = 0, d;
    while (a < b && g_pg[a] >= '0' && g_pg[a] <= '9') {      /* > 65535 matches nothing */
        d = (pdw)(g_pg[a] - '0');
        if (n > 6553 || (n == 6553 && d > 5)) big = 1;
        n = (pdw)((n << 3) + (n << 1) + d); a++;
    }
    if (big) return 0;
    a = sc_skip(a);
    while (a < b && g_pg[a] >= '0' && g_pg[a] <= '9') { g = (pdw)(g * 10 + (g_pg[a] - '0')); a++; }
    a = sc_skip(a);
    if (a >= b || g_pg[a] != 'R') return 0;
    *gen = g;
    return n;
}
/* the position just past "obj" and the white space after it, else SC_BAD */
static pdw sc_body(void)
{
    pdw p = 0;
    while (p + 2 < sc_n) {
        if (g_pg[p] == 'o' && g_pg[p + 1] == 'b' && g_pg[p + 2] == 'j' && p > 0
            && sc_ws((pdb)g_pg[p - 1])) return sc_skip((pdw)(p + 3));
        if (p > 64) break;
        p++;
    }
    return SC_BAD;
}
static pdw sc_eqname(pdw a, pdw b, const char *k)
{
    pdw i = 0;
    while (a < b && k[i]) { if (g_pg[a] != k[i]) return 0; a++; i++; }
    return a == b && !k[i];
}
/* Find /key in the dictionary that opens at ds. 1 = found (sc_kpos at the '/',
 * sc_vs..sc_ve the value), 0 = absent, SC_BAD = malformed; sc_close is the
 * position of the dictionary's final ">>" either way */
static pdw dict_find(pdw ds, const char *key)
{
    pdw p = (pdw)(ds + 2), np, ne, vs, ve, found = 0;
    sc_close = SC_BAD;
    if (ds + 1 >= sc_n || g_pg[ds] != '<' || g_pg[ds + 1] != '<') return SC_BAD;
    for (;;) {
        p = sc_skip(p);
        if (p >= sc_n) return SC_BAD;
        if (g_pg[p] == '>') { sc_close = p; return found; }
        if (g_pg[p] != '/') return SC_BAD;
        np = p; ne = sc_tok((pdw)(p + 1));
        vs = sc_skip(ne);
        ve = sc_val(vs, 1);
        if (ve == SC_BAD) return SC_BAD;
        if (!found && sc_eqname((pdw)(np + 1), ne, key)) { found = 1; sc_kpos = np; sc_vs = vs; sc_ve = ve; }
        p = ve;
    }
}
