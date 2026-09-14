/* Host-test model of sbmem.inc. Include this when SB_HOST_TEST is defined. */
#ifdef SB_HOST_TEST
#include <string.h>
#include "../sbasic.h"

static unsigned short sbmh_lo[SBM_CELL_CAP];
static short sbmh_hi[SBM_CELL_CAP];
static unsigned char sbmh_str[SBM_STR_CAP];
static unsigned char sbmh_kind[SBM_KIND_BYTES];
static unsigned sbmh_used, sbmh_str_used, sbmh_cap, sbmh_lo_cap;
static int sbmh_hi_live, sbmh_kind_live;
static int sbmh_fail_after;

static int sbmh_claim(void)
{
    if (!sbmh_fail_after) return 1;
    --sbmh_fail_after;
    return sbmh_fail_after != 0;
}

static int sbmh_claim_hi(void)
{
    unsigned i;
    if (sbmh_hi_live) return 0;
    if (!sbmh_claim()) return -1;
    for (i = 0; i < sbmh_used; ++i)
        sbmh_hi[i] = (short)sbmh_lo[i] < 0 ? -1 : 0;
    sbmh_hi_live = 1;
    return 0;
}

static int sbmh_ensure(unsigned need)
{
    unsigned cap;
    if (need <= sbmh_cap) return 0;
    cap = (need + 2047u) & ~2047u;
    if (sbmh_lo_cap < cap) {
        if (!sbmh_claim()) return -1;
        sbmh_lo_cap = cap;
    }
    if (sbmh_hi_live && !sbmh_claim()) return -1;
    sbmh_cap = cap;
    return 0;
}

int sbm_init(void) { sbm_reset(); return 0; }
void sbm_reset(void)
{
    sbmh_used = sbmh_str_used = 0;
    sbmh_cap = sbmh_lo_cap = 0;
    sbmh_hi_live = 0;
    sbmh_kind_live = 0;
    sbmh_fail_after = 0;
    memset(sbmh_kind, 0, sizeof(sbmh_kind));
}
int sbm_make(unsigned n)
{
    unsigned base;
    if (n > SBM_CELL_CAP - sbmh_used) return -1;
    if (sbmh_ensure(sbmh_used + n)) return -1;
    if (!sbmh_kind_live) {
        if (!sbmh_claim()) return -1;
        sbmh_kind_live = 1;
    }
    base = sbmh_used; sbmh_used += n;
    memset(sbmh_lo + base, 0, n * sizeof(*sbmh_lo));
    if (sbmh_hi_live) memset(sbmh_hi + base, 0, n * sizeof(*sbmh_hi));
    return (int)base;
}
int sbm_get(unsigned i) { return (short)sbmh_lo[i]; }
void sbm_set(unsigned i, int v)
{ sbmh_lo[i] = v; if (sbmh_hi_live) sbmh_hi[i] = v < 0 ? -1 : 0; }
unsigned sbm_get_lo(unsigned i) { return sbmh_lo[i]; }
int sbm_get_hi(unsigned i)
{ return sbmh_hi_live ? sbmh_hi[i] : ((short)sbmh_lo[i] < 0 ? -1 : 0); }
int sbm_set_pair(unsigned i, unsigned lo, int hi)
{
    int sign;
    sign = (short)lo < 0 ? -1 : 0;
    if (!sbmh_hi_live && hi != sign && sbmh_claim_hi()) return -1;
    sbmh_lo[i] = lo;
    if (sbmh_hi_live) sbmh_hi[i] = (short)hi;
    return 0;
}
int sbm_kind_get(unsigned i)
{ return (sbmh_kind[i >> 3] >> (i & 7)) & 1; }
void sbm_kind_set(unsigned i, int kind)
{
    unsigned char mask = (unsigned char)(1u << (i & 7));
    if (kind) sbmh_kind[i >> 3] |= mask;
    else sbmh_kind[i >> 3] &= (unsigned char)~mask;
}
unsigned sbm_lo_segment(void) { return 0; }
int sbm_str_set(unsigned i, const char *s, unsigned n)
{
    if (i >= sbmh_used) return -1;
    if (sbmh_claim_hi()) return -1;
    if (sbmh_hi[i] && n <= (unsigned short)sbmh_hi[i]) {
        memcpy(sbmh_str + sbmh_lo[i], s, n); sbmh_hi[i] = n; return 0;
    }
    if (n > SBM_STR_CAP - sbmh_str_used) return -1;
    memcpy(sbmh_str + sbmh_str_used, s, n);
    sbmh_lo[i] = sbmh_str_used; sbmh_hi[i] = n; sbmh_str_used += n;
    return 0;
}
unsigned sbm_str_len(unsigned i) { return (unsigned short)sbmh_hi[i]; }
int sbm_str_copy(unsigned i, char *d, unsigned cap)
{
    unsigned n = sbm_str_len(i);
    if (!cap) return 0;
    if (n >= cap) n = cap - 1;
    memcpy(d, sbmh_str + sbmh_lo[i], n); d[n] = 0; return (int)n;
}
int sbm_str_char(unsigned i, unsigned p)
{ return p < sbm_str_len(i) ? sbmh_str[sbmh_lo[i] + p] : -1; }
#endif
