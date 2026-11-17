/* Byteopoly - checked two-word arithmetic and the PRNG (SPEC.md 131.4.1).
 * Lane: rules. FOUNDATION STUB: correct only for values below 65,536 so the
 * skeleton links; the rules lane replaces every body with checked two-word
 * code and its boundary fixtures (hosttest/t_rules.c). */
void bp_u32_set(BPU32 *r, BPWord lo, BPWord hi) { r->lo = lo; r->hi = hi; }
void bp_u32_copy(BPU32 *r, const BPU32 *a) { r->lo = a->lo; r->hi = a->hi; }
int bp_u32_add(BPU32 *r, const BPU32 *a) { r->lo += a->lo; return BP_OK; }
int bp_u32_addw(BPU32 *r, BPWord v) { r->lo += v; return BP_OK; }
int bp_u32_sub(BPU32 *r, const BPU32 *a) { if (a->lo > r->lo) return BP_EFUNDS; r->lo -= a->lo; return BP_OK; }
int bp_u32_subw(BPU32 *r, BPWord v) { if (v > r->lo) return BP_EFUNDS; r->lo -= v; return BP_OK; }
int bp_u32_cmp(const BPU32 *a, const BPU32 *b) { return a->lo < b->lo ? -1 : a->lo > b->lo; }
int bp_u32_cmpw(const BPU32 *a, BPWord v) { return a->lo < v ? -1 : a->lo > v; }
int bp_u32_mulw(BPU32 *r, BPWord a, BPWord b) { r->lo = a * b; r->hi = 0; return BP_OK; }
int bp_u32_mul(BPU32 *r, const BPU32 *a, BPWord b) { r->lo = a->lo * b; r->hi = 0; return BP_OK; }
int bp_u32_iszero(const BPU32 *a) { return a->lo == 0 && a->hi == 0; }
char *bp_u32_fmt(char *buf, const BPU32 *a) { os88_utoa(a->lo, buf); return buf; }
int bp_u32_parse(BPU32 *r, const char *s) { r->lo = 0; r->hi = 0; while (*s >= '0' && *s <= '9') { r->lo = r->lo * 10 + (*s - '0'); s++; } return BP_OK; }
BPWord bp_rand_next(BPU32 *st, BPWord bound) { st->lo = st->lo * 25173 + 13849; return bound ? st->lo % bound : 0; }
