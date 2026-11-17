/* Byteopoly - checked two-word arithmetic and the PRNG (SPEC.md 131.4.1,
 * 131.4.7). Lane: rules. Original.
 *
 * A BPU32 is two 16-bit words. Every intermediate below is cast back to
 * BPWord before it is stored or compared and every carry is read from a
 * 16-bit result, so the host (where BPWord promotes to a 32-bit int)
 * computes exactly what the 8088 does. Every mutator leaves *r unchanged
 * when it answers an error. */

static BPU32 bp_mt1, bp_mt2;            /* bp_u32_mul's partial products */

void bp_u32_set(BPU32 *r, BPWord lo, BPWord hi) { r->lo = lo; r->hi = hi; }
void bp_u32_copy(BPU32 *r, const BPU32 *a) { r->lo = a->lo; r->hi = a->hi; }

int bp_u32_add(BPU32 *r, const BPU32 *a)
{
    BPWord lo, hi, c;
    lo = (BPWord)(r->lo + a->lo);
    c = (BPWord)(lo < a->lo);
    hi = (BPWord)(r->hi + a->hi);
    if (hi < a->hi) return BP_EOVERFLOW;
    if (c) {
        if (hi == 0xFFFFu) return BP_EOVERFLOW;
        hi = (BPWord)(hi + 1);
    }
    r->lo = lo;
    r->hi = hi;
    return BP_OK;
}

int bp_u32_addw(BPU32 *r, BPWord v)
{
    BPWord lo;
    lo = (BPWord)(r->lo + v);
    if (lo < v) {
        if (r->hi == 0xFFFFu) return BP_EOVERFLOW;
        r->hi = (BPWord)(r->hi + 1);
    }
    r->lo = lo;
    return BP_OK;
}

int bp_u32_cmp(const BPU32 *a, const BPU32 *b)
{
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

int bp_u32_cmpw(const BPU32 *a, BPWord v)
{
    if (a->hi) return 1;
    if (a->lo != v) return a->lo < v ? -1 : 1;
    return 0;
}

int bp_u32_sub(BPU32 *r, const BPU32 *a)
{
    BPWord b;
    if (bp_u32_cmp(r, a) < 0) return BP_EFUNDS;
    b = (BPWord)(r->lo < a->lo);
    r->lo = (BPWord)(r->lo - a->lo);
    r->hi = (BPWord)(r->hi - a->hi - b);
    return BP_OK;
}

int bp_u32_subw(BPU32 *r, BPWord v)
{
    if (!r->hi && r->lo < v) return BP_EFUNDS;
    if (r->lo < v) r->hi = (BPWord)(r->hi - 1);
    r->lo = (BPWord)(r->lo - v);
    return BP_OK;
}

/* a x b from four byte partial products: each is at most 255 x 255 =
 * 65,025, so it fits a word on the 8088 and on the host alike */
int bp_u32_mulw(BPU32 *r, BPWord a, BPWord b)
{
    BPWord al, ah, bl, bh, lo, hi, p, t;
    al = (BPWord)(a & 0xFF);
    ah = (BPWord)(a >> 8);
    bl = (BPWord)(b & 0xFF);
    bh = (BPWord)(b >> 8);
    lo = (BPWord)(al * bl);
    hi = (BPWord)(ah * bh);
    p = (BPWord)(al * bh);
    t = (BPWord)(lo + (BPWord)(p << 8));
    if (t < lo) hi = (BPWord)(hi + 1);
    lo = t;
    hi = (BPWord)(hi + (p >> 8));
    p = (BPWord)(ah * bl);
    t = (BPWord)(lo + (BPWord)(p << 8));
    if (t < lo) hi = (BPWord)(hi + 1);
    lo = t;
    hi = (BPWord)(hi + (p >> 8));
    r->lo = lo;
    r->hi = hi;
    return BP_OK;
}

int bp_u32_mul(BPU32 *r, const BPU32 *a, BPWord b)
{
    BPWord hi;
    bp_u32_mulw(&bp_mt1, a->lo, b);
    bp_u32_mulw(&bp_mt2, a->hi, b);
    if (bp_mt2.hi) return BP_EOVERFLOW;
    hi = (BPWord)(bp_mt1.hi + bp_mt2.lo);
    if (hi < bp_mt1.hi) return BP_EOVERFLOW;
    r->lo = bp_mt1.lo;
    r->hi = hi;
    return BP_OK;
}

int bp_u32_iszero(const BPU32 *a) { return a->lo == 0 && a->hi == 0; }

/* decimal, a comma every three digits; long division by ten a byte at a
 * time keeps every intermediate below 2,560 */
static char bp_fdig[10];
char *bp_u32_fmt(char *buf, const BPU32 *a)
{
    BPWord hi, lo, rem, x, q;
    int n, i, j;
    hi = a->hi;
    lo = a->lo;
    n = 0;
    do {
        rem = (BPWord)(hi % 10);
        hi = (BPWord)(hi / 10);
        x = (BPWord)((rem << 8) | (lo >> 8));
        q = (BPWord)(x / 10);
        rem = (BPWord)(x % 10);
        x = (BPWord)((rem << 8) | (lo & 0xFF));
        lo = (BPWord)((q << 8) | (x / 10));
        bp_fdig[n++] = (char)('0' + x % 10);
    } while ((hi | lo) && n < 10);
    j = 0;
    for (i = n - 1; i >= 0; i--) {
        buf[j++] = bp_fdig[i];
        if (i && i % 3 == 0) buf[j++] = ',';
    }
    buf[j] = 0;
    return buf;
}

static BPU32 bp_pv;
int bp_u32_parse(BPU32 *r, const char *s)
{
    char ch, last;
    bp_pv.lo = 0;
    bp_pv.hi = 0;
    if (*s < '0' || *s > '9') return BP_EARG;
    last = 0;
    for (; *s; s++) {
        ch = *s;
        last = ch;
        if (ch == ',') continue;
        if (ch < '0' || ch > '9') return BP_EARG;
        if (bp_u32_mul(&bp_pv, &bp_pv, 10)) return BP_EOVERFLOW;
        if (bp_u32_addw(&bp_pv, (BPWord)(ch - '0'))) return BP_EOVERFLOW;
    }
    if (last == ',') return BP_EARG;
    r->lo = bp_pv.lo;
    r->hi = bp_pv.hi;
    return BP_OK;
}

/* xorshift32 (13, 17, 5) over two words; the answer is the high word of
 * (new high word x bound), 0..bound-1, or the high word itself for 0 */
BPWord bp_rand_next(BPU32 *st, BPWord bound)
{
    BPWord lo, hi, tl, th;
    lo = st->lo;
    hi = st->hi;
    if (!lo && !hi) { lo = 0x8CA2u; hi = 0x92D6u; }
    th = (BPWord)((BPWord)(hi << 13) | (lo >> 3));      /* x ^= x << 13 */
    tl = (BPWord)(lo << 13);
    hi = (BPWord)(hi ^ th);
    lo = (BPWord)(lo ^ tl);
    lo = (BPWord)(lo ^ (hi >> 1));                      /* x ^= x >> 17 */
    th = (BPWord)((BPWord)(hi << 5) | (lo >> 11));      /* x ^= x << 5 */
    tl = (BPWord)(lo << 5);
    hi = (BPWord)(hi ^ th);
    lo = (BPWord)(lo ^ tl);
    st->lo = lo;
    st->hi = hi;
    if (!bound) return hi;
    bp_u32_mulw(&bp_mt1, hi, bound);
    return bp_mt1.hi;
}
