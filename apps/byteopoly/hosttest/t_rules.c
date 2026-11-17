/* Byteopoly host fixtures for the rules engine and its arithmetic (SPEC.md
 * 131.4, 131.4.1). Lane: rules. Built by `make byteopolyhost` over the
 * SHIPPING C with BP_HOST, under ASan/UBSan.
 *
 * INDEPENDENT on purpose: every expected number below was computed by hand
 * from SPEC.md 131.2's text (prices, rents, multipliers, card effects), and
 * the PRNG, the multiply and the decimal format are checked against
 * reference implementations written here with <stdint.h>, never against the
 * engine's own tables or helpers. Each refusal is checked to leave the
 * whole state byte-identical (memcmp). */
#include <stdint.h>
#include "bphost.h"
#include "byteoply.c"

/* the session calls back into the UI; the link lane will supply host
 * no-ops (DECISIONS D10) and these weak ones then lose without a clash */
__attribute__((weak)) void bp_ui_committed(const BPResult *r) { (void)r; }

static int nchk;
#define CK(c, what) do { nchk++; BP_CHECK(c, what); } while (0)

static BPState S, S0;
static BPResult R;
static BPCmd C;
static BPSetup SU;
static BPU32 T, U;

static uint32_t v32(const BPU32 *a) { return ((uint32_t)a->hi << 16) | a->lo; }
static void set32(BPU32 *a, uint32_t v) { a->lo = (BPWord)(v & 0xFFFF); a->hi = (BPWord)(v >> 16); }
static uint32_t cash(int i) { return v32(&S.cash[i]); }
static void setcash(int i, uint32_t v) { set32(&S.cash[i], v); }

/* ---- reference implementations (independent of bp_math.c) ------------- */
static uint32_t ref_step(uint32_t *st)
{
    uint32_t x = *st;
    if (!x) x = 0x92D68CA2u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x;
    return x;
}
static unsigned ref_next(uint32_t *st, unsigned bound)
{
    uint32_t hi = ref_step(st) >> 16;
    return bound ? (unsigned)((hi * (uint32_t)bound) >> 16) : (unsigned)hi;
}
static void ref_fmt(char *out, uint32_t v)
{
    char d[16];
    int n, i, j = 0;
    n = snprintf(d, sizeof d, "%u", (unsigned)v);
    for (i = 0; i < n; i++) {
        out[j++] = d[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) out[j++] = ',';
    }
    out[j] = 0;
}
static uint32_t lcg = 12345;
static uint32_t rnd32(void) { lcg = lcg * 1103515245u + 12345u; return (lcg >> 8) ^ (lcg << 20); }

/* an rng state whose next two dice are a then b */
static void dice(int a, int b)
{
    uint32_t s0, st;
    for (s0 = 1; s0 < 1000000; s0++) {
        st = s0;
        if ((int)ref_next(&st, 6) + 1 == a && (int)ref_next(&st, 6) + 1 == b) {
            set32(&S.rng, s0);
            return;
        }
    }
    CK(0, "dice: no state");
}

/* ---- command helpers --------------------------------------------------- */
static void cmd(int type, int seat, int arg)
{
    memset(&C, 0, sizeof C);
    C.type = (BPByte)type;
    C.seat = (BPByte)seat;
    C.arg = (BPByte)arg;
    C.rev = S.rev;
}
static int run(void) { return bp_apply(&S, &C, &R); }
static int doit(int type, int seat, int arg) { cmd(type, seat, arg); return run(); }
static int bid(int seat, uint32_t amt) { cmd(BP_C_BID, seat, 0); set32(&C.amount, amt); return run(); }

/* C must be refused with `want` and the state must not change */
static void refuse_c(int want, const char *what)
{
    int e;
    memcpy(&S0, &S, sizeof S);
    e = run();
    if (e != want) printf("  %s: got %d want %d\n", what, e, want);
    CK(e == want, what);
    CK(memcmp(&S0, &S, sizeof S) == 0, what);
    CK(R.n == 0, what);
}
static void refuse(int type, int seat, int arg, int want, const char *what)
{
    int e;
    cmd(type, seat, arg);
    refuse_c(want, what);
    /* bp_can answers bp_check; what only execution finds (an overflow, the
     * depth bound, a state that cannot resolve) it cannot predict */
    if (type != BP_C_PROPOSE && type != BP_C_BID && want != BP_EOVERFLOW &&
        want != BP_EDEPTH && want != BP_EINVALID) {
        e = bp_can(&S, seat, type, arg);
        if (e != want) printf("  bp_can %s: got %d want %d\n", what, e, want);
        CK(e == want, "bp_can agrees with the refusal");
    }
}
static void ok(int type, int seat, int arg, const char *what)
{
    int e;
    uint32_t r0 = v32(&S.rev);
    e = bp_can(&S, seat, type, arg);
    if (type != BP_C_PROPOSE) CK(e == BP_OK, what);
    e = doit(type, seat, arg);
    if (e) printf("  %s: refused %d\n", what, e);
    CK(e == BP_OK, what);
    CK(v32(&S.rev) == r0 + 1, "rev + 1");
}
static void okc(const char *what)
{
    int e;
    uint32_t r0 = v32(&S.rev);
    e = run();
    if (e) printf("  %s: refused %d\n", what, e);
    CK(e == BP_OK, what);
    CK(v32(&S.rev) == r0 + 1, "rev + 1");
}
static const BPEvent *ev(int kind)
{
    int i;
    for (i = 0; i < R.n; i++) if (R.ev[i].kind == kind) return &R.ev[i];
    return 0;
}
static int nev(int kind)
{
    int i, n = 0;
    for (i = 0; i < R.n; i++) if (R.ev[i].kind == kind) n++;
    return n;
}
static void valid(const char *what)
{
    int e = bp_state_check(&S);
    if (e) printf("  state_check %s: %d\n", what, e);
    CK(e == BP_OK, what);
}

/* a fresh match with seat 0 to play, in MANAGE, decks in identity order */
static void fresh(int n)
{
    int d, i;
    bp_setup_default(&SU);
    SU.nseats = (BPByte)n;
    set32(&T, 777);
    CK(bp_new_match(&S, &SU, &T, &R) == BP_OK, "new match");
    S.active = S.decider = 0;
    S.round_left = (BPByte)((1 << n) - 1);
    for (d = 0; d < 2; d++) {
        for (i = 0; i < 8; i++) S.deck[d][i] = (BPByte)(d * 8 + i);
        S.deck_top[d] = 0;
        S.deck_n[d] = 8;
    }
}
/* prop index by space id, from SPEC.md 131.2.1's table (not bp_board.c) */
static int P(int space)
{
    static const int props[28] = {-1, 0, -1, 1, 2, 3, -1, -1, 4, 5, -1, 6, 7, 8,
                                  -1, 9, 10, 11, -1, 12, 13, -1, 14, 15, -1, 16, 17, -1};
    return props[space];
}
static void own(int space, int seat, int level, int mort)
{
    S.owner[P(space)] = (BPByte)seat;
    S.level[P(space)] = (BPByte)level;
    S.mortgaged[P(space)] = (BPByte)mort;
}
/* hold deck d's Recovery Disk: out of the cycle (ids 6 and 15) */
static void hold_disk(int seat, int d)
{
    int i, j = 0, id = d ? 15 : 6;
    BPByte tmp[8];
    for (i = 0; i < S.deck_n[d]; i++) if (S.deck[d][i] != id) tmp[j++] = S.deck[d][i];
    memset(S.deck[d], 0, 8);
    memcpy(S.deck[d], tmp, (size_t)j);
    S.deck_n[d] = (BPByte)j;
    S.deck_top[d] = 0;
    S.disks[seat] |= (BPByte)(1 << d);
}

/* ======================================================================= */
static void t_math(void)
{
    static char buf[14], want[20];
    uint32_t a, b, k;
    uint64_t p;
    int i, e;
    /* add: carry from lo into hi, overflow refused with r unchanged */
    set32(&T, 0x0000FFFFu); set32(&U, 1);
    CK(bp_u32_add(&T, &U) == BP_OK && v32(&T) == 0x00010000u, "add carry");
    set32(&T, 0xFFFFFFFFu);
    CK(bp_u32_add(&T, &U) == BP_EOVERFLOW && v32(&T) == 0xFFFFFFFFu, "add overflow at max");
    set32(&T, 0xFFFF0000u); set32(&U, 0x00010000u);
    CK(bp_u32_add(&T, &U) == BP_EOVERFLOW && v32(&T) == 0xFFFF0000u, "add hi overflow");
    set32(&T, 0xFFFEFFFFu); set32(&U, 0x00000001u);
    CK(bp_u32_add(&T, &U) == BP_OK && v32(&T) == 0xFFFF0000u, "add carry to the top");
    set32(&T, 0xFFFF8000u); set32(&U, 0x00008000u);
    CK(bp_u32_add(&T, &U) == BP_EOVERFLOW && v32(&T) == 0xFFFF8000u, "add carry overflow");
    set32(&T, 0xFFFFFFFFu); set32(&U, 0);
    CK(bp_u32_add(&T, &U) == BP_OK && v32(&T) == 0xFFFFFFFFu, "add 0 at max");
    set32(&T, 0x1234FFFFu);
    CK(bp_u32_addw(&T, 1) == BP_OK && v32(&T) == 0x12350000u, "addw carry");
    set32(&T, 0xFFFFFFF0u);
    CK(bp_u32_addw(&T, 0x10) == BP_EOVERFLOW && v32(&T) == 0xFFFFFFF0u, "addw overflow");
    CK(bp_u32_addw(&T, 0x0F) == BP_OK && v32(&T) == 0xFFFFFFFFu, "addw to max");
    /* sub: borrow, and a balance never goes negative */
    set32(&T, 0x00010000u); set32(&U, 1);
    CK(bp_u32_sub(&T, &U) == BP_OK && v32(&T) == 0x0000FFFFu, "sub borrow");
    set32(&T, 0); CK(bp_u32_sub(&T, &U) == BP_EFUNDS && v32(&T) == 0, "sub below 0");
    set32(&T, 0x00010000u); set32(&U, 0x00010001u);
    CK(bp_u32_sub(&T, &U) == BP_EFUNDS && v32(&T) == 0x00010000u, "sub larger");
    set32(&T, 0xFFFFFFFFu); set32(&U, 0xFFFFFFFFu);
    CK(bp_u32_sub(&T, &U) == BP_OK && v32(&T) == 0, "sub max-max");
    set32(&T, 0x00020000u);
    CK(bp_u32_subw(&T, 1) == BP_OK && v32(&T) == 0x0001FFFFu, "subw borrow");
    set32(&T, 5);
    CK(bp_u32_subw(&T, 6) == BP_EFUNDS && v32(&T) == 5, "subw below 0");
    /* compare */
    set32(&T, 0x00010000u); set32(&U, 0x0000FFFFu);
    CK(bp_u32_cmp(&T, &U) == 1 && bp_u32_cmp(&U, &T) == -1 && bp_u32_cmp(&T, &T) == 0, "cmp");
    CK(bp_u32_cmpw(&T, 0xFFFF) == 1 && bp_u32_cmpw(&U, 0xFFFF) == 0 && bp_u32_cmpw(&U, 0) == 1, "cmpw");
    set32(&T, 0); CK(bp_u32_iszero(&T) && !bp_u32_iszero(&U), "iszero");
    /* multiply: the 16x16 product is exact, never overflows */
    CK(bp_u32_mulw(&T, 0xFFFF, 0xFFFF) == BP_OK && v32(&T) == 0xFFFE0001u, "mulw max");
    CK(bp_u32_mulw(&T, 0x100, 0x100) == BP_OK && v32(&T) == 0x10000u, "mulw 256^2");
    CK(bp_u32_mulw(&T, 0, 0xFFFF) == BP_OK && v32(&T) == 0, "mulw 0");
    CK(bp_u32_mulw(&T, 1, 0xFFFF) == BP_OK && v32(&T) == 0xFFFF, "mulw 1");
    CK(bp_u32_mulw(&T, 40, 60) == BP_OK && v32(&T) == 2400, "mulw rent 486 workshop");
    for (i = 0; i < 20000; i++) {
        a = rnd32() & 0xFFFF; b = rnd32() & 0xFFFF;
        bp_u32_mulw(&T, (BPWord)a, (BPWord)b);
        if (v32(&T) != a * b) { CK(0, "mulw random"); break; }
    }
    set32(&U, 0x00010001u);
    CK(bp_u32_mul(&T, &U, 0xFFFF) == BP_OK && v32(&T) == 0xFFFFFFFFu, "mul to max");
    set32(&U, 0x00010002u); set32(&T, 7);
    CK(bp_u32_mul(&T, &U, 0xFFFF) == BP_EOVERFLOW && v32(&T) == 7, "mul overflow unchanged");
    set32(&U, 0x00010000u);
    CK(bp_u32_mul(&T, &U, 0xFFFF) == BP_OK && v32(&T) == 0xFFFF0000u, "mul hi");
    set32(&U, 0x00020000u); set32(&T, 9);
    CK(bp_u32_mul(&T, &U, 0x8000) == BP_EOVERFLOW && v32(&T) == 9, "mul hi overflow");
    for (i = 0; i < 20000; i++) {
        a = rnd32() >> (rnd32() & 31); b = rnd32() & 0xFFFF;
        set32(&U, a); set32(&T, 0x5A5A5A5Au);
        p = (uint64_t)a * b;
        e = bp_u32_mul(&T, &U, (BPWord)b);
        if (p > 0xFFFFFFFFu ? (e != BP_EOVERFLOW || v32(&T) != 0x5A5A5A5Au)
                            : (e != BP_OK || v32(&T) != (uint32_t)p)) { CK(0, "mul random"); break; }
    }
    set32(&T, 0x10); CK(bp_u32_mul(&T, &T, 0x10) == BP_OK && v32(&T) == 0x100, "mul aliased");
    /* format: thousands commas, at most 13 characters */
    {
        static const uint32_t vals[] = {0, 1, 9, 10, 999, 1000, 1280, 9999, 65535, 65536,
                                        99999, 100000, 999999, 1000000, 16777215,
                                        4294967295u, 1500, 1000000000u};
        static const char *strs[] = {"0", "1", "9", "10", "999", "1,000", "1,280", "9,999",
                                     "65,535", "65,536", "99,999", "100,000", "999,999",
                                     "1,000,000", "16,777,215", "4,294,967,295", "1,500",
                                     "1,000,000,000"};
        for (i = 0; i < (int)(sizeof vals / sizeof vals[0]); i++) {
            set32(&T, vals[i]);
            CK(bp_u32_fmt(buf, &T) == buf && !strcmp(buf, strs[i]), strs[i]);
            CK(bp_u32_parse(&U, buf) == BP_OK && v32(&U) == vals[i], "parse round trip");
        }
    }
    for (i = 0; i < 20000; i++) {
        k = rnd32() >> (rnd32() & 31);
        set32(&T, k);
        bp_u32_fmt(buf, &T);
        ref_fmt(want, k);
        if (strcmp(buf, want)) { printf("  fmt %u: %s vs %s\n", (unsigned)k, buf, want); CK(0, "fmt random"); break; }
        if (bp_u32_parse(&U, buf) != BP_OK || v32(&U) != k) { CK(0, "parse random"); break; }
    }
    /* parse refusals leave r unchanged */
    set32(&U, 42);
    CK(bp_u32_parse(&U, "") == BP_EARG && v32(&U) == 42, "parse empty");
    CK(bp_u32_parse(&U, ",1") == BP_EARG && v32(&U) == 42, "parse leading comma");
    CK(bp_u32_parse(&U, "1,") == BP_EARG && v32(&U) == 42, "parse trailing comma");
    CK(bp_u32_parse(&U, "12a") == BP_EARG && v32(&U) == 42, "parse letter");
    CK(bp_u32_parse(&U, "-1") == BP_EARG && v32(&U) == 42, "parse sign");
    CK(bp_u32_parse(&U, " 1") == BP_EARG && v32(&U) == 42, "parse space");
    CK(bp_u32_parse(&U, "4294967296") == BP_EOVERFLOW && v32(&U) == 42, "parse 2^32");
    CK(bp_u32_parse(&U, "99999999999") == BP_EOVERFLOW && v32(&U) == 42, "parse huge");
    CK(bp_u32_parse(&U, "4,294,967,295") == BP_OK && v32(&U) == 0xFFFFFFFFu, "parse max");
    CK(bp_u32_parse(&U, "1,5000") == BP_OK && v32(&U) == 15000, "parse loose commas");
    CK(bp_u32_parse(&U, "007") == BP_OK && v32(&U) == 7, "parse leading zeros");
}

static void t_rand(void)
{
    uint32_t st, seeds[] = {1, 777, 0x12345678u, 0xFFFFFFFFu, 0};
    int i, k, n[7];
    BPWord w;
    for (k = 0; k < 5; k++) {
        st = seeds[k];
        set32(&T, seeds[k]);
        for (i = 0; i < 1000; i++) {
            w = bp_rand_next(&T, (BPWord)(i % 13));
            if (w != ref_next(&st, (unsigned)(i % 13)) || v32(&T) != st) { CK(0, "rand vs reference"); break; }
        }
    }
    set32(&T, 0);
    bp_rand_next(&T, 6);
    CK(!bp_u32_iszero(&T), "zero state replaced");
    memset(n, 0, sizeof n);
    set32(&T, 99);
    for (i = 0; i < 60000; i++) {
        w = bp_rand_next(&T, 6);
        if (w > 5) { CK(0, "die range"); break; }
        n[w]++;
    }
    for (i = 0; i < 6; i++) CK(n[i] > 9500 && n[i] < 10500, "die roughly uniform");
}

/* the roll-off and the shuffles reproduced from SPEC.md 131.4.7's order */
static void t_newmatch(void)
{
    uint32_t st;
    unsigned mlo, mhi;
    int deck[2][8], d, i, j, t, best, first, cand, tie, nseats, seed, rounds, order;
    static BPState A;
    for (seed = 1; seed <= 300; seed++) {
        nseats = 2 + seed % 3;
        bp_setup_default(&SU);
        SU.nseats = (BPByte)nseats;
        set32(&T, (uint32_t)seed * 2654435761u);
        CK(bp_new_match(&S, &SU, &T, &R) == BP_OK, "new match");
        st = (uint32_t)seed * 2654435761u;
        mlo = ref_next(&st, 0);
        mhi = ref_next(&st, 0);
        for (d = 0; d < 2; d++) {
            for (i = 0; i < 8; i++) deck[d][i] = d * 8 + i;
            for (i = 7; i > 0; i--) {
                j = (int)ref_next(&st, (unsigned)i + 1);
                t = deck[d][i]; deck[d][i] = deck[d][j]; deck[d][j] = t;
            }
        }
        cand = (1 << nseats) - 1;
        order = 0;
        for (rounds = 0; rounds < 16; rounds++) {
            best = 0; tie = 0;
            for (i = 0; i < nseats; i++) {
                if (!(cand & (1 << i))) continue;
                d = (int)ref_next(&st, 6) + 1;
                t = (int)ref_next(&st, 6) + 1;
                CK(R.ev[order].kind == BP_V_ORDER && R.ev[order].seat == i &&
                   v32(&R.ev[order].amount) == (uint32_t)(d * 16 + t), "ORDER event");
                order++;
                if (d + t > best) { best = d + t; tie = 0; }
                if (d + t == best) tie |= 1 << i;
            }
            cand = tie;
            if (!(cand & (cand - 1))) break;
        }
        for (first = 0; !(cand & (1 << first)); first++) ;
        CK(S.match_id.lo == mlo && S.match_id.hi == mhi, "match id from the stream");
        CK(v32(&S.rng) == st, "rng state after the roll-off");
        for (d = 0; d < 2; d++) for (i = 0; i < 8; i++) CK(S.deck[d][i] == deck[d][i], "shuffle");
        CK(S.active == first && S.decider == first && S.phase == BP_PH_MANAGE, "first seat");
        CK(R.n == order + 2 && R.ev[order].kind == BP_V_FIRST && R.ev[order].seat == first &&
           R.ev[order + 1].kind == BP_V_TURN, "FIRST, TURN");
        for (i = 0; i < nseats; i++) CK(cash(i) == 1500 && S.pos[i] == 0 && S.alive[i], "1,500 on BOOT");
        for (i = nseats; i < 4; i++) CK(!S.alive[i] && cash(i) == 0, "unused seat");
        for (i = 0; i < 18; i++) CK(S.owner[i] == BP_NOBODY && !S.level[i], "bank owns all");
        CK(v32(&S.rev) == 1 && S.turn == 1 && S.round == 0 && S.status == BP_ST_PLAY, "fresh counters");
        CK(S.round_left == (1 << nseats) - 1, "round mask");
        valid("new match");
        set32(&T, (uint32_t)seed * 2654435761u);
        CK(bp_new_match(&A, &SU, &T, &R) == BP_OK && !memcmp(&A, &S, sizeof A), "same seed, same match");
    }
    /* setup refusals; the target untouched */
    memcpy(&S0, &S, sizeof S);
    bp_setup_default(&SU); SU.nseats = 1;
    CK(bp_new_match(&S, &SU, &T, &R) == BP_EARG && !memcmp(&S0, &S, sizeof S), "1 seat");
    bp_setup_default(&SU); SU.nseats = 5; CK(bp_setup_check(&SU) == BP_EARG, "5 seats");
    bp_setup_default(&SU); SU.rules = 2; CK(bp_setup_check(&SU) == BP_EARG, "rules 2");
    bp_setup_default(&SU); SU.rules = BP_RULES_QUICK; SU.rounds = 15; CK(bp_setup_check(&SU) == BP_EARG, "15 rounds");
    bp_setup_default(&SU); SU.rules = BP_RULES_QUICK; SU.rounds = 20; CK(bp_setup_check(&SU) == BP_OK, "quick 20");
    bp_setup_default(&SU); SU.rounds = 10; CK(bp_setup_check(&SU) == BP_EARG, "classic with rounds");
    bp_setup_default(&SU); SU.token[1] = 0; CK(bp_setup_check(&SU) == BP_EARG, "duplicate token");
    bp_setup_default(&SU); SU.token[1] = 4; CK(bp_setup_check(&SU) == BP_EARG, "token 4");
    bp_setup_default(&SU); SU.name[0][0] = 0; CK(bp_setup_check(&SU) == BP_EARG, "empty name");
    bp_setup_default(&SU); memset(SU.name[0], 'A', 9); CK(bp_setup_check(&SU) == BP_EARG, "9-char name");
    bp_setup_default(&SU); SU.name[0][1] = 7; CK(bp_setup_check(&SU) == BP_EARG, "control char");
    bp_setup_default(&SU); SU.machine[1] = 2; CK(bp_setup_check(&SU) == BP_EARG, "machine 2");
    bp_setup_default(&SU); SU.nseats = 4; CK(bp_setup_check(&SU) == BP_OK, "4 default seats");
}

/* every one of the 28 spaces reached by a 1+2 roll (SPEC.md 131.2.1) */
static void t_landing(void)
{
    /* B start/salary, P property, E event, C charge, R repair, S swap, X crash */
    static const char kind[29] = "BPEPPPCRPPEPPPSPPPEPPXPPEPPC";
    int t, start, cross, top;
    for (t = 0; t < 28; t++) {
        fresh(2);
        start = (t - 3 + 28) % 28;
        cross = start + 3 >= 28;
        S.pos[0] = (BPByte)start;
        dice(1, 2);
        ok(BP_C_ROLL, 0, 0, "roll 3");
        CK(S.pos[0] == t || kind[t] == 'X', "destination");
        CK(S.d1 == 1 && S.d2 == 2 && R.d1 == 1 && R.d2 == 2 && S.rolled, "dice recorded");
        CK(nev(BP_V_SALARY) == cross, "salary iff BOOT reached");
        switch (kind[t]) {
        case 'B': case 'R': case 'S':
            CK(S.phase == BP_PH_AFTER && cash(0) == 1500u + 200u * cross && !S.detained[0], "nothing");
            break;
        case 'P':
            CK(S.phase == BP_PH_BUY && S.decider == 0 && cash(0) == 1500u + 200u * cross, "BUY");
            break;
        case 'E':
            top = (t == 2 || t == 18) ? 0 : 8;
            CK(S.phase == BP_PH_CARD && S.card == top && R.card == top && S.depth == 1, "card shown");
            break;
        case 'C':
            CK(S.phase == BP_PH_AFTER && cash(0) == (t == 6 ? 1400u : 1425u), "charge");
            break;
        case 'X':
            CK(S.pos[0] == 7 && S.detained[0] && S.phase == BP_PH_AFTER && cash(0) == 1500, "CRASH");
            CK(!bp_can(&S, 0, BP_C_END_TURN, 0) && bp_can(&S, 0, BP_C_ROLL, 0) == BP_ERULE, "turn over");
            break;
        }
        valid("after landing");
    }
}

static void t_doubles(void)
{
    fresh(2);
    own(4, 0, 0, 0); own(8, 0, 0, 0);
    dice(2, 2);
    ok(BP_C_ROLL, 0, 0, "doubles 1");
    CK(S.pos[0] == 4 && S.phase == BP_PH_AFTER && S.doubles == 1 && ev(BP_V_DOUBLES), "first double");
    refuse(BP_C_END_TURN, 0, 0, BP_ERULE, "a doubles roll is owed");
    dice(2, 2);
    ok(BP_C_ROLL, 0, 0, "doubles 2");
    CK(S.pos[0] == 8 && S.doubles == 2, "second double");
    dice(3, 3);
    ok(BP_C_ROLL, 0, 0, "doubles 3");
    CK(S.pos[0] == 7 && S.detained[0] && S.tries[0] == 0 && S.doubles == 0 && cash(0) == 1500 &&
       ev(BP_V_DETAIN) && !ev(BP_V_MOVE), "third double: detained before moving");
    refuse(BP_C_ROLL, 0, 0, BP_ERULE, "no roll after detention");
    ok(BP_C_END_TURN, 0, 0, "end turn");
    CK(S.active == 1 && S.phase == BP_PH_MANAGE && S.turn == 2 && S.round_left == 2, "seat 1's turn");
    /* a non-double after a double ends the bonus */
    fresh(2);
    own(4, 0, 0, 0);
    dice(2, 2); ok(BP_C_ROLL, 0, 0, "double");
    dice(1, 2); ok(BP_C_ROLL, 0, 0, "bonus roll");
    CK(S.pos[0] == 7 && !S.detained[0] && S.doubles == 0 && S.phase == BP_PH_AFTER, "visiting 7");
    refuse(BP_C_ROLL, 0, 0, BP_ERULE, "no second bonus");
    CK(S.turn == 1 && S.round_left == 3, "bonus rolls belong to one turn");
    ok(BP_C_END_TURN, 0, 0, "end");
    CK(S.round_left == 2 && S.round == 0, "one turn spent");
    valid("doubles");
}

static void t_repair(void)
{
    /* pay */
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.phase = BP_PH_REPAIR;
    valid("detained");
    refuse(BP_C_END_TURN, 0, 0, BP_EPHASE, "no end turn before choosing");
    refuse(BP_C_USE_DISK, 0, 0, BP_EOWNER, "no disk held");
    refuse(BP_C_USE_DISK, 0, 2, BP_EARG, "deck 2");
    setcash(0, 49);
    refuse(BP_C_PAY_BAIL, 0, 0, BP_EFUNDS, "49 cr");
    setcash(0, 1500);
    ok(BP_C_PAY_BAIL, 0, 0, "pay 50");
    CK(cash(0) == 1450 && !S.detained[0] && S.phase == BP_PH_MANAGE && ev(BP_V_RELEASE)->other == 0, "paid out");
    dice(3, 3);
    ok(BP_C_ROLL, 0, 0, "normal roll");
    CK(S.pos[0] == 13 && S.phase == BP_PH_BUY && S.doubles == 1, "a normal turn, doubles included");
    ok(BP_C_BUY, 0, 0, "buy IIe");
    CK(cash(0) == 1450 - 260 && S.owner[P(13)] == 0 && !bp_can(&S, 0, BP_C_ROLL, 0), "bonus owed after paying");
    /* the disk: back to the bottom of its own deck */
    fresh(2);
    hold_disk(0, 0);
    S.deck_top[0] = 2;      /* cycle 0 1 [2] 3 4 5 7 */
    S.pos[0] = 7; S.detained[0] = 1; S.phase = BP_PH_REPAIR;
    valid("holding a disk");
    ok(BP_C_USE_DISK, 0, 0, "use disk");
    {
        static const BPByte want[8] = {0, 1, 6, 2, 3, 4, 5, 7};
        CK(!memcmp(S.deck[0], want, 8) && S.deck_n[0] == 8 && S.deck_top[0] == 3, "disk at the bottom");
    }
    CK(!S.disks[0] && !S.detained[0] && S.phase == BP_PH_MANAGE && ev(BP_V_RELEASE)->other == 1, "released by disk");
    valid("disk returned");
    /* doubles release: move, no bonus */
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.phase = BP_PH_REPAIR;
    dice(2, 2);
    ok(BP_C_ROLL, 0, 0, "attempt");
    CK(S.pos[0] == 11 && !S.detained[0] && S.phase == BP_PH_BUY && S.doubles == 0 &&
       ev(BP_V_RELEASE)->other == 2, "released by doubles");
    ok(BP_C_DECLINE, 0, 0, "decline");
    ok(BP_C_PASS, 0, 0, "pass"); ok(BP_C_PASS, 1, 0, "pass");
    CK(S.phase == BP_PH_AFTER, "after");
    refuse(BP_C_ROLL, 0, 0, BP_ERULE, "no bonus after a doubles release");
    /* failures: the first two spend the turn */
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.phase = BP_PH_REPAIR;
    dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "fail 1");
    CK(S.pos[0] == 7 && S.detained[0] && S.tries[0] == 1 && S.phase == BP_PH_AFTER, "still detained");
    refuse(BP_C_ROLL, 0, 0, BP_ERULE, "turn spent");
    ok(BP_C_END_TURN, 0, 0, "end");
    CK(S.active == 1 && S.phase == BP_PH_MANAGE, "the turn passed");
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.tries[0] = 2; S.phase = BP_PH_REPAIR;
    dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "fail 3, can pay");
    CK(cash(0) == 1450 && !S.detained[0] && S.tries[0] == 0 && S.pos[0] == 10 &&
       S.phase == BP_PH_CARD && ev(BP_V_RELEASE)->other == 3 && !S.pending_move, "paid and moved");
    CK(!bp_can(&S, 0, BP_C_ACK_CARD, 0), "card waits");
    /* third failure, short of cash: DEBT, then the pending move */
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.tries[0] = 2; S.phase = BP_PH_REPAIR;
    setcash(0, 30); own(1, 0, 0, 0);
    dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "fail 3, short");
    CK(S.phase == BP_PH_DEBT && S.debt_reason == BP_DR_BAIL && v32(&S.debt_amount) == 50 &&
       S.debt_to == BP_NOBODY && S.pending_move == 3 && S.detained[0] && S.tries[0] == 3 &&
       cash(0) == 30, "bail debt");
    valid("bail debt");
    refuse(BP_C_SETTLE, 0, 0, BP_EFUNDS, "cannot settle yet");
    ok(BP_C_MORTGAGE, 0, 1, "mortgage PET");
    CK(cash(0) == 90, "+60");
    ok(BP_C_SETTLE, 0, 0, "settle");
    CK(cash(0) == 40 && !S.detained[0] && S.pos[0] == 10 && S.phase == BP_PH_CARD &&
       !S.pending_move && S.debt_to == BP_NOBODY, "settled, then moved");
    valid("after the pending move");
    /* bankruptcy cancels the pending move */
    fresh(3);
    S.pos[0] = 7; S.detained[0] = 1; S.tries[0] = 2; S.phase = BP_PH_REPAIR;
    setcash(0, 30);
    dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "fail 3, short");
    ok(BP_C_BANKRUPT, 0, 0, "concede");
    CK(!S.alive[0] && S.pos[0] == 7 && !S.pending_move && S.active == 1 &&
       S.phase == BP_PH_MANAGE && !ev(BP_V_MOVE), "no move after bankruptcy");
    valid("after bail bankruptcy");
    /* detained players still collect rent */
    fresh(2);
    S.pos[1] = 7; S.detained[1] = 1; own(4, 1, 0, 0);
    S.pos[0] = 1; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "land on the detained player's C64");
    CK(cash(1) == 1518 && cash(0) == 1482, "rent to a detained owner");
}

static void land_rent(int start, int d1, int d2)
{
    S.pos[0] = (BPByte)start;
    dice(d1, d2);
    ok(BP_C_ROLL, 0, 0, "roll onto rent");
}
static void t_rent(void)
{
    static const uint32_t lv[6] = {0, 90, 270, 540, 810, 1080};
    int L;
    /* C64: base 18 */
    fresh(2); own(4, 1, 0, 0); land_rent(1, 1, 2);
    CK(cash(0) == 1482 && cash(1) == 1518 && ev(BP_V_RENT) && v32(&ev(BP_V_RENT)->amount) == 18 &&
       ev(BP_V_RENT)->seat == 0 && ev(BP_V_RENT)->other == 1 && ev(BP_V_RENT)->space == 4, "stock rent 18");
    fresh(2); own(1, 1, 0, 0); own(3, 1, 0, 0); own(4, 1, 0, 0); land_rent(1, 1, 2);
    CK(cash(0) == 1464 && cash(1) == 1536, "set doubles to 36");
    fresh(2); own(1, 1, 0, 0); own(3, 1, 0, 1); own(4, 1, 0, 0); land_rent(1, 1, 2);
    CK(cash(0) == 1482, "a mortgaged member cancels the doubling");
    fresh(2); own(1, 1, 0, 0); own(3, 1, 0, 0); own(4, 1, 0, 1); land_rent(1, 1, 2);
    CK(cash(0) == 1500 && cash(1) == 1500 && !ev(BP_V_RENT) && S.phase == BP_PH_AFTER, "mortgaged: no rent");
    for (L = 1; L <= 5; L++) {
        fresh(2); own(1, 1, L, 0); own(3, 1, L, 0); own(4, 1, L, 0); land_rent(1, 1, 2);
        CK(cash(0) == 1500 - lv[L] && cash(1) == 1500 + lv[L], "level rent 5/15/30/45/60x");
        CK(bp_rent_quote(&S, 4, &T) == BP_OK && v32(&T) == lv[L], "quote matches the rent");
    }
    fresh(2); own(4, 0, 0, 0); land_rent(1, 1, 2);
    CK(cash(0) == 1500 && S.phase == BP_PH_AFTER, "own property");
    /* 486DX2 workshop: 40 x 60 */
    fresh(2); own(25, 1, 5, 0); own(26, 1, 5, 0); land_rent(23, 1, 2);
    CK(S.phase == BP_PH_DEBT && v32(&S.debt_amount) == 2400 && S.debt_to == 1 &&
       S.debt_reason == BP_DR_RENT && cash(0) == 1500 && cash(1) == 1500, "rent above cash opens DEBT");
    valid("rent debt");
    /* utilities: 4x / 10x the movement total */
    fresh(2); own(5, 1, 0, 0); land_rent(2, 1, 2);
    CK(cash(0) == 1488, "one utility: 4 x 3");
    fresh(2); own(5, 1, 0, 0); own(16, 1, 0, 0); land_rent(2, 1, 2);
    CK(cash(0) == 1470, "both: 10 x 3");
    fresh(2); own(5, 1, 0, 0); own(16, 1, 0, 1); land_rent(2, 1, 2);
    CK(cash(0) == 1488, "mortgaged second utility does not count");
    fresh(2); own(5, 1, 0, 1); own(16, 1, 0, 0); land_rent(2, 1, 2);
    CK(cash(0) == 1500, "mortgaged utility: nothing");
    fresh(2); own(5, 1, 0, 0); own(16, 0, 0, 0); land_rent(2, 1, 2);
    CK(cash(0) == 1488, "the other utility's owner does not count");
    /* quotes */
    fresh(2); own(5, 1, 0, 0); own(16, 1, 0, 0);
    CK(bp_rent_quote(&S, 5, &T) == BP_ERULE && v32(&T) == 10, "utility quote without dice");
    S.rolled = 1; S.d1 = 6; S.d2 = 5;
    CK(bp_rent_quote(&S, 16, &T) == BP_OK && v32(&T) == 110, "utility quote 10 x 11");
    CK(bp_rent_quote(&S, 1, &T) == BP_EOWNER && v32(&T) == 0, "unowned quote");
    CK(bp_rent_quote(&S, 6, &T) == BP_EOWNER, "charge space quote");
    own(19, 1, 0, 0); own(17, 1, 0, 0); own(20, 1, 0, 0);
    CK(bp_rent_quote(&S, 19, &T) == BP_OK && v32(&T) == 56, "PC XT set 2 x 28");
    S.level[P(17)] = S.level[P(19)] = S.level[P(20)] = 3;
    CK(bp_rent_quote(&S, 19, &T) == BP_OK && v32(&T) == 28 * 30, "PC XT level 3");
    /* a card-driven utility landing uses the recorded dice */
    fresh(2); own(16, 1, 0, 0);
    S.deck_top[0] = 4;                      /* Dial Into the BBS */
    S.pos[0] = 15; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "to Floppy Find 18");
    CK(S.phase == BP_PH_CARD && S.card == 4, "BBS card");
    ok(BP_C_ACK_CARD, 0, 0, "ack");
    CK(S.pos[0] == 16 && cash(0) == 1500 + 200 - 12 && cash(1) == 1512, "salary crossing BOOT, 4 x 3 rent");
    /* a state lacking the total is invalid, and refused whole */
    fresh(2); own(16, 1, 0, 0);
    S.deck_top[0] = 4; S.pos[0] = 18; S.phase = BP_PH_CARD; S.card = 4; S.depth = 1;
    refuse(BP_C_ACK_CARD, 0, 0, BP_EINVALID, "card utility landing without dice");
}

static void t_mortgage(void)
{
    fresh(2);
    own(1, 0, 0, 0); own(5, 0, 0, 0); own(3, 1, 0, 0);
    CK(bp_mortgage_value(1) == 60 && bp_mortgage_value(5) == 75 && bp_mortgage_value(26) == 210, "half price");
    CK(bp_mortgage_value(6) == 0 && bp_mortgage_value(0) == 0 && bp_mortgage_value(28) == 0, "not a property");
    bp_redeem_cost(1, &T); CK(v32(&T) == 66, "PET redeem 60 + 6");
    bp_redeem_cost(5, &T); CK(v32(&T) == 83, "POWER redeem 75 + 7.5 rounded up");
    bp_redeem_cost(20, &T); CK(v32(&T) == 187, "PC AT 170 + 17");
    ok(BP_C_MORTGAGE, 0, 1, "mortgage PET");
    CK(cash(0) == 1560 && S.mortgaged[P(1)] && ev(BP_V_MORTGAGE) && v32(&ev(BP_V_MORTGAGE)->amount) == 60, "+60");
    refuse(BP_C_MORTGAGE, 0, 1, BP_ERULE, "twice");
    refuse(BP_C_MORTGAGE, 0, 3, BP_EOWNER, "foreign");
    refuse(BP_C_MORTGAGE, 0, 4, BP_EOWNER, "unowned");
    refuse(BP_C_MORTGAGE, 0, 2, BP_EARG, "event space");
    refuse(BP_C_MORTGAGE, 0, 40, BP_EARG, "space 40");
    refuse(BP_C_MORTGAGE, 1, 3, BP_ESEAT, "not the decider");
    refuse(BP_C_REDEEM, 0, 5, BP_ERULE, "redeem unmortgaged");
    ok(BP_C_MORTGAGE, 0, 5, "mortgage POWER");
    CK(cash(0) == 1635, "+75");
    setcash(0, 82);
    refuse(BP_C_REDEEM, 0, 5, BP_EFUNDS, "82 < 83");
    setcash(0, 83);
    ok(BP_C_REDEEM, 0, 5, "redeem POWER");
    CK(cash(0) == 0 && !S.mortgaged[P(5)], "paid 83");
    setcash(0, 1000);
    ok(BP_C_REDEEM, 0, 1, "redeem PET");
    CK(cash(0) == 934, "paid 66");
    /* a computer mortgages only when the whole collection is at level 0 */
    fresh(2);
    own(1, 0, 1, 0); own(3, 0, 1, 0); own(4, 0, 0, 0);
    refuse(BP_C_MORTGAGE, 0, 4, BP_ERULE, "collection has upgrades");
    refuse(BP_C_MORTGAGE, 0, 1, BP_ERULE, "improved member");
    ok(BP_C_SELL, 0, 1, "sell"); ok(BP_C_SELL, 0, 3, "sell");
    ok(BP_C_MORTGAGE, 0, 4, "now it mortgages");
    refuse(BP_C_UPGRADE, 0, 1, BP_ERULE, "no upgrade with a mortgage in the set");
    valid("mortgage");
}

static void t_upgrade(void)
{
    int L, sp;
    static const int set[3] = {1, 3, 4};
    fresh(2);
    own(1, 0, 0, 0); own(3, 0, 0, 0); own(4, 0, 0, 0); own(5, 0, 0, 0);
    setcash(0, 2000);
    ok(BP_C_UPGRADE, 0, 1, "PET 1");
    CK(cash(0) == 1950 && S.level[P(1)] == 1 && ev(BP_V_UPGRADE)->other == 1, "50 per step");
    refuse(BP_C_UPGRADE, 0, 1, BP_ERULE, "uneven build");
    for (L = 1; L <= 5; L++)
        for (sp = 0; sp < 3; sp++)
            if (L > 1 || sp) ok(BP_C_UPGRADE, 0, set[sp], "even build");
    CK(cash(0) == 2000 - 15 * 50 && S.level[P(4)] == 5, "three Workshops for 750");
    refuse(BP_C_UPGRADE, 0, 1, BP_ERULE, "level 6");
    ok(BP_C_SELL, 0, 1, "Workshop -> 4");
    CK(cash(0) == 1250 + 25 && S.level[P(1)] == 4, "half of 50");
    refuse(BP_C_SELL, 0, 1, BP_ERULE, "uneven sale");
    refuse(BP_C_UPGRADE, 0, 3, BP_ERULE, "uneven rebuild");
    ok(BP_C_UPGRADE, 0, 1, "back to 5");
    refuse(BP_C_UPGRADE, 0, 5, BP_ERULE, "utilities are never upgraded");
    refuse(BP_C_SELL, 0, 5, BP_ERULE, "nothing to sell on a utility");
    fresh(2);
    own(1, 0, 0, 0); own(3, 0, 0, 0); own(4, 1, 0, 0);
    refuse(BP_C_UPGRADE, 0, 1, BP_ERULE, "incomplete collection");
    refuse(BP_C_SELL, 0, 1, BP_ERULE, "level 0");
    fresh(2);
    own(25, 0, 0, 0); own(26, 0, 0, 0);
    setcash(0, 199);
    refuse(BP_C_UPGRADE, 0, 25, BP_EFUNDS, "199 < 200");
    setcash(0, 200);
    ok(BP_C_UPGRADE, 0, 25, "386DX");
    CK(cash(0) == 0, "200 per Power PC step");
    refuse(BP_C_UPGRADE, 1, 26, BP_ESEAT, "not the decider");
    valid("upgrades");
}

static void open_auction(int n)
{
    fresh(n);
    S.pos[0] = 1; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "to C64");
    CK(S.phase == BP_PH_BUY, "buy decision");
}
static void t_auction(void)
{
    /* everyone passes: the bank keeps the lot */
    open_auction(3);
    ok(BP_C_DECLINE, 0, 0, "decline");
    CK(S.phase == BP_PH_AUCTION && S.auc_turn == 0 && S.decider == 0 && S.auc_open == 7 &&
       S.auc_high == BP_NOBODY && ev(BP_V_AUCTION), "landing seat bids first");
    valid("auction open");
    refuse(BP_C_ROLL, 0, 0, BP_EPHASE, "no roll in an auction");
    refuse(BP_C_MORTGAGE, 0, 1, BP_EPHASE, "no mortgage in an auction");
    refuse(BP_C_END_TURN, 0, 0, BP_EPHASE, "no end turn");
    refuse(BP_C_BUY, 0, 0, BP_EPHASE, "no buy");
    refuse(BP_C_PASS, 1, 0, BP_ESEAT, "not seat 1's bid");
    ok(BP_C_PASS, 0, 0, "A passes"); CK(S.auc_turn == 1, "B next");
    ok(BP_C_PASS, 1, 0, "B passes"); CK(S.auc_turn == 2, "C next");
    ok(BP_C_PASS, 2, 0, "C passes");
    CK(S.phase == BP_PH_AFTER && S.decider == 0 && S.owner[P(4)] == BP_NOBODY && ev(BP_V_UNSOLD) &&
       cash(0) == 1500 && cash(1) == 1500 && cash(2) == 1500, "unsold");
    valid("unsold");
    /* outbid, then bid again; a passed seat never bids again */
    open_auction(3);
    ok(BP_C_DECLINE, 0, 0, "decline");
    cmd(BP_C_BID, 0, 0); set32(&C.amount, 5); refuse_c(BP_ERULE, "below 10");
    cmd(BP_C_BID, 0, 0); set32(&C.amount, 15); refuse_c(BP_ERULE, "not a multiple of 10");
    cmd(BP_C_BID, 0, 0); set32(&C.amount, 0); refuse_c(BP_ERULE, "zero");
    CK(bid(0, 10) == BP_OK && S.auc_turn == 1 && S.auc_high == 0, "A 10");
    cmd(BP_C_BID, 1, 0); set32(&C.amount, 10); refuse_c(BP_ERULE, "equal bid");
    CK(bid(1, 20) == BP_OK && S.auc_turn == 2, "B 20");
    ok(BP_C_PASS, 2, 0, "C passes");
    CK(S.auc_turn == 0, "back to A");
    cmd(BP_C_BID, 2, 0); set32(&C.amount, 100); refuse_c(BP_ESEAT, "C passed permanently");
    cmd(BP_C_BID, 0, 0); set32(&C.amount, 25); refuse_c(BP_ERULE, "25 is not +10 in tens");
    CK(bid(0, 30) == BP_OK && S.auc_turn == 1, "A 30 skips C");
    memcpy(&S0, &S, sizeof S);
    ok(BP_C_PASS, 1, 0, "B passes");
    CK(S.phase == BP_PH_AFTER && S.owner[P(4)] == 0 && cash(0) == 1470 && cash(1) == 1500 &&
       ev(BP_V_WON) && v32(&ev(BP_V_WON)->amount) == 30, "A wins at exactly 30");
    /* the duplicate of the winning pass cannot charge twice */
    C.rev = S0.rev;
    refuse_c(BP_ESTALE, "duplicate PASS");
    valid("won");
    /* exact cash, auto-pass with a reason */
    open_auction(3);
    setcash(1, 230); setcash(2, 15);
    ok(BP_C_DECLINE, 0, 0, "decline");
    CK(bid(0, 10) == BP_OK && S.auc_turn == 1, "A 10");
    cmd(BP_C_BID, 1, 0); set32(&C.amount, 240); refuse_c(BP_EFUNDS, "above cash");
    CK(bid(1, 230) == BP_OK, "B bids all its cash");
    CK(S.auc_turn == 0 && ev(BP_V_PASS) && ev(BP_V_PASS)->seat == 2 && ev(BP_V_PASS)->other == 1 &&
       (S.auc_passed & 4), "C cannot reach 240: passed with a reason");
    bp_bid_min(&S, &T); CK(v32(&T) == 240, "next bid 240");
    ok(BP_C_PASS, 0, 0, "A passes");
    CK(S.owner[P(4)] == 1 && cash(1) == 0 && cash(0) == 1500 && cash(2) == 15, "B pays exactly 230");
    /* a landing seat that cannot open is passed for it */
    open_auction(2);
    setcash(0, 5);
    ok(BP_C_DECLINE, 0, 0, "decline, broke");
    CK(S.auc_turn == 1 && (S.auc_passed & 1) && ev(BP_V_PASS)->other == 1, "A auto-passed");
    CK(bid(1, 10) == BP_OK && S.owner[P(4)] == 1 && cash(1) == 1490, "B wins alone");
    /* an eliminated seat has no place in the bidding */
    open_auction(3);
    S.alive[2] = 0; setcash(2, 0); S.round_left &= 3;
    ok(BP_C_DECLINE, 0, 0, "decline");
    CK(S.auc_open == 3, "only the living");
    /* unaffordable purchase: BUY refused, auction instead */
    open_auction(2);
    setcash(0, 219);
    refuse(BP_C_BUY, 0, 0, BP_EFUNDS, "219 < 220");
    setcash(0, 220);
    ok(BP_C_BUY, 0, 0, "buy at 220");
    CK(cash(0) == 0 && S.owner[P(4)] == 0 && S.phase == BP_PH_AFTER, "bought once");
    refuse_c(BP_ESTALE, "repeated BUY");
}

static void t_trade(void)
{
    fresh(2);
    own(1, 0, 0, 1); own(5, 0, 0, 0); own(9, 1, 0, 0);
    hold_disk(0, 0);
    valid("before trade");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1;
    C.give.lo = (1 << P(1)) | (1 << P(5)); C.get.lo = 1 << P(9); C.disks = 1;
    set32(&C.amount, 100);
    okc("propose");
    CK(S.phase == BP_PH_TRADE && S.decider == 1 && S.offer_id == 1 && S.offer_from == 0 &&
       S.ret_phase == BP_PH_MANAGE && ev(BP_V_OFFER), "frozen");
    valid("offer pending");
    refuse(BP_C_ACCEPT, 0, 1, BP_ESEAT, "the proposer cannot accept");
    refuse(BP_C_ACCEPT, 1, 2, BP_EARG, "wrong offer id");
    refuse(BP_C_ROLL, 0, 0, BP_EPHASE, "nothing else while pending");
    refuse(BP_C_MORTGAGE, 0, 5, BP_EPHASE, "no management while pending");
    refuse(BP_C_CANCEL, 1, 0, BP_ESEAT, "only the proposer cancels");
    cmd(BP_C_ACCEPT, 1, 1); C.rev.lo--; refuse_c(BP_ESTALE, "stale acceptance");
    /* revalidation at acceptance */
    setcash(0, 99);
    refuse(BP_C_ACCEPT, 1, 1, BP_EFUNDS, "payer short at acceptance");
    setcash(0, 1500);
    memcpy(&S0, &S, sizeof S);
    ok(BP_C_ACCEPT, 1, 1, "accept");
    CK(S.owner[P(1)] == 1 && S.mortgaged[P(1)] == 1 && S.owner[P(5)] == 1 && S.owner[P(9)] == 0, "props swapped, mortgage kept");
    CK(S.disks[0] == 0 && S.disks[1] == 1 && S.deck_n[0] == 7, "disk transferred, still out of the deck");
    CK(cash(0) == 1400 && cash(1) == 1600 && ev(BP_V_TRADED), "100 paid");
    CK(S.phase == BP_PH_MANAGE && S.decider == 0 && S.offer_from == BP_NOBODY && S.ret_phase == BP_PH_NONE, "back to MANAGE");
    valid("after trade");
    C.rev = S0.rev; refuse_c(BP_ESTALE, "duplicate acceptance");
    /* the recipient pays; reject and cancel change nothing */
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.get.lo = 1 << P(5); C.payer = 1; set32(&C.amount, 0);
    set32(&C.amount, 0); C.give.lo = 1 << P(9);
    okc("swap back");
    memcpy(&S0, &S, sizeof S);
    ok(BP_C_REJECT, 1, 0, "reject");
    CK(!memcmp(S0.owner, S.owner, sizeof S.owner) && !memcmp(S0.cash, S.cash, sizeof S.cash) &&
       S.phase == BP_PH_MANAGE && ev(BP_V_REJECTED), "rejected: nothing moved");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.payer = 1; set32(&C.amount, 500);
    okc("ask for cash");
    CK(S.offer_id == 3, "ids count");
    ok(BP_C_CANCEL, 0, 0, "cancel");
    CK(cash(1) == 1600 && S.phase == BP_PH_MANAGE && ev(BP_V_CANCELLED), "cancelled");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.payer = 1; set32(&C.amount, 500);
    okc("again");
    ok(BP_C_ACCEPT, 1, 4, "gift of cash");
    CK(cash(1) == 1100 && cash(0) == 1900, "recipient paid");
    /* refusals of the terms */
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; refuse_c(BP_EARG, "empty");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 0; set32(&C.amount, 1); refuse_c(BP_EARG, "to self");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 2; set32(&C.amount, 1); refuse_c(BP_EARG, "to an absent seat");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.give.lo = 1 << P(1); refuse_c(BP_EOWNER, "not the proposer's");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.get.lo = 1 << P(9); refuse_c(BP_EOWNER, "not the recipient's");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; set32(&C.amount, 1901); refuse_c(BP_EFUNDS, "more than the payer has");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.give.hi = 4; refuse_c(BP_EARG, "prop 18");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.disks = 4; refuse_c(BP_EARG, "disk bit 2");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.disks = 2; refuse_c(BP_EOWNER, "a disk not held");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.disks = 0x10; okc("ask for B's disk");
    ok(BP_C_CANCEL, 0, 0, "cancel");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; set32(&C.amount, 1); C.payer = 2; refuse_c(BP_EARG, "payer 2");
    cmd(BP_C_PROPOSE, 1, 0); C.to = 0; set32(&C.amount, 1); refuse_c(BP_ESEAT, "not the current player");
    /* no property of an improved collection */
    fresh(2);
    own(1, 0, 1, 0); own(3, 0, 1, 0); own(4, 0, 1, 0);
    own(8, 1, 1, 0); own(9, 1, 1, 0); own(11, 1, 0, 0); own(16, 1, 0, 0);
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.give.lo = 1 << P(4); refuse_c(BP_ERULE, "improved collection");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.get.lo = 1 << P(11); refuse_c(BP_ERULE, "theirs is improved too");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; C.get.lo = 1 << P(16); okc("a utility is fine");
    ok(BP_C_REJECT, 1, 0, "reject");
    /* not past an unresolved decision; allowed when detained */
    S.pos[0] = 12; S.phase = BP_PH_BUY;
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; set32(&C.amount, 1); refuse_c(BP_EPHASE, "not in BUY");
    fresh(2);
    S.pos[0] = 7; S.detained[0] = 1; S.phase = BP_PH_REPAIR;
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; set32(&C.amount, 10); okc("detained may trade");
    ok(BP_C_ACCEPT, 1, 1, "accept");
    CK(S.phase == BP_PH_REPAIR && cash(1) == 1510, "back to REPAIR");
}

/* land seat 0 on the event space with card id on top, then ACK it */
static void card_at(int id, int space)
{
    int d = id >> 3;
    fresh(2);
    S.deck_top[d] = (BPByte)(id & 7);
    S.pos[0] = (BPByte)((space + 25) % 28);
    dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "to the event space");
    CK(S.phase == BP_PH_CARD && S.card == id && ev(BP_V_CARD)->other == id, "card shown");
    valid("card shown");
    refuse(BP_C_ACK_CARD, 1, 0, BP_ESEAT, "only the drawer acknowledges");
    refuse(BP_C_ROLL, 0, 0, BP_EPHASE, "card first");
    memcpy(&S0, &S, sizeof S);
    ok(BP_C_ACK_CARD, 0, 0, "ack");
    C.rev = S0.rev;
    {
        static BPState keep;
        static BPResult keepr;
        memcpy(&keep, &S, sizeof S);
        memcpy(&keepr, &R, sizeof R);
        CK(run() == BP_ESTALE && !memcmp(&keep, &S, sizeof S), "a repeated ack applies nothing");
        memcpy(&R, &keepr, sizeof R);
    }
    if (id != 6 && id != 15)
        CK(S.deck_top[d] == ((id & 7) + 1) % 8 && S.deck[d][id & 7] == id && S.deck_n[d] == 8,
           "returned to the bottom");
}
static void t_cards(void)
{
    card_at(0, 18); CK(cash(0) == 1600 && S.phase == BP_PH_AFTER, "Boxed New Old Stock +100");
    card_at(1, 18); CK(cash(0) == 1550, "Garage Sale +50");
    card_at(2, 18); CK(cash(0) == 1525, "Duplicate Manual +25");
    card_at(7, 18); CK(cash(0) == 1475, "Replacement Cable -25");
    card_at(3, 18); CK(cash(0) == 1700 && S.pos[0] == 0 && S.phase == BP_PH_AFTER, "Reboot to BOOT +200");
    card_at(4, 18); CK(cash(0) == 1700 && S.pos[0] == 16 && S.phase == BP_PH_BUY, "BBS across BOOT");
    card_at(4, 2);  CK(cash(0) == 1500 + 200 && S.pos[0] == 16, "BBS from 2 (the roll from 27 crossed)");
    card_at(5, 2);  CK(S.pos[0] == 5 && S.phase == BP_PH_BUY && cash(0) == 1700, "Turbo to POWER");
    card_at(5, 18); CK(S.pos[0] == 7 && S.detained[0] && cash(0) == 1500, "Turbo into CRASH");
    card_at(6, 18);
    CK(S.disks[0] == 1 && S.deck_n[0] == 7 && S.phase == BP_PH_AFTER && ev(BP_V_DISK), "Recovery Disk kept");
    {
        static const BPByte want[8] = {0, 1, 2, 3, 4, 5, 7, 0};
        CK(!memcmp(S.deck[0], want, 8) && S.deck_top[0] == 6 && S.deck[0][S.deck_top[0]] == 7,
           "out of the cycle; the next card is 7");
    }
    valid("disk held");
    card_at(8, 10);  CK(cash(0) == 1400, "Leaking Battery -100");
    card_at(9, 10);  CK(cash(0) == 1450, "Failed Capacitor -50");
    card_at(10, 24); CK(cash(0) == 1465, "Shipping Damage -35");
    card_at(14, 24); CK(cash(0) == 1550, "Warranty +50");
    card_at(12, 10); CK(S.pos[0] == 7 && S.detained[0] && cash(0) == 1500 && S.phase == BP_PH_AFTER, "GPF");
    card_at(13, 10); CK(S.pos[0] == 7 && !S.detained[0] && S.phase == BP_PH_AFTER, "Roll Back to visiting");
    card_at(13, 24); CK(S.pos[0] == 7 && S.detained[0] && cash(0) == 1500, "Roll Back into CRASH");
    card_at(15, 24); CK(S.disks[0] == 2 && S.deck_n[1] == 7, "deck 1 disk");
    card_at(11, 10); CK(cash(0) == 1500 && S.phase == BP_PH_AFTER, "Bench Service with nothing: 0");
    /* Bench Service: 25 a chip, 100 a Workshop (not 125) */
    fresh(2);
    own(1, 0, 5, 0); own(3, 0, 5, 0); own(4, 0, 4, 0);
    own(8, 1, 3, 0); own(9, 1, 3, 0); own(11, 1, 3, 0);
    S.deck_top[1] = 3; S.pos[0] = 7; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "to System Error");
    ok(BP_C_ACK_CARD, 0, 0, "bench");
    CK(cash(0) == 1200 && ev(BP_V_CHARGE) && v32(&ev(BP_V_CHARGE)->amount) == 300, "100 + 100 + 4 x 25");
    /* Reboot from BOOT itself pays 200 once */
    fresh(2);
    S.pos[0] = 0; S.phase = BP_PH_CARD; S.card = 3; S.deck_top[0] = 3; S.depth = 1;
    ok(BP_C_ACK_CARD, 0, 0, "reboot at BOOT");
    CK(S.pos[0] == 0 && cash(0) == 1700 && nev(BP_V_SALARY) == 1, "200 once");
    /* a card payment into DEBT, settled once */
    fresh(2);
    own(1, 0, 0, 0); setcash(0, 60);
    S.deck_top[1] = 0; S.pos[0] = 7; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "to System Error");
    ok(BP_C_ACK_CARD, 0, 0, "Leaking Battery");
    CK(S.phase == BP_PH_DEBT && S.debt_reason == BP_DR_CARD && v32(&S.debt_amount) == 100 && cash(0) == 60, "card debt");
    ok(BP_C_MORTGAGE, 0, 1, "mortgage");
    ok(BP_C_SETTLE, 0, 0, "settle");
    CK(cash(0) == 20 && S.phase == BP_PH_AFTER, "paid once");
    refuse(BP_C_SETTLE, 0, 0, BP_EPHASE, "settle twice");
    /* both disks held at once: both out of their decks */
    fresh(2);
    hold_disk(0, 0); hold_disk(1, 1);
    valid("both disks held");
    CK(S.deck_n[0] == 7 && S.deck_n[1] == 7, "seven each");
    /* the continuation bound: a draw beyond depth 3 is refused whole */
    fresh(2);
    S.pos[0] = 13; S.phase = BP_PH_CARD; S.card = 13; S.deck_top[1] = 5; S.depth = 3;
    S.rolled = 1; S.d1 = 1; S.d2 = 2;
    refuse(BP_C_ACK_CARD, 0, 0, BP_EDEPTH, "fourth draw");
    S.depth = 1;
    ok(BP_C_ACK_CARD, 0, 0, "second draw allowed");
    CK(S.pos[0] == 10 && S.phase == BP_PH_CARD && S.depth == 2 && S.card == 14, "chained draw");
    /* a card cycle never repeats a card before the others */
    fresh(2);
    {
        int k, seen = 0;
        for (k = 0; k < 8; k++) {
            S.pos[0] = 7; S.phase = BP_PH_MANAGE; S.active = S.decider = 0; S.turn++;
            S.detained[0] = 0; S.doubles = 0;
            dice(1, 2);
            ok(BP_C_ROLL, 0, 0, "draw");
            seen |= 1 << (S.card & 7);
            ok(BP_C_ACK_CARD, 0, 0, "ack");
            if (S.phase == BP_PH_DEBT || S.phase == BP_PH_BUY) break;
        }
        CK(seen == 0xFF, "eight different cards");
    }
}

static void t_debt(void)
{
    fresh(2);
    own(1, 0, 2, 0); own(3, 0, 2, 0); own(4, 0, 2, 0); own(25, 0, 0, 0); own(26, 0, 0, 0);
    own(8, 1, 4, 0); own(9, 1, 4, 0); own(11, 1, 4, 0);
    setcash(0, 100);
    S.pos[0] = 8; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "onto ZX at 4 chips");
    CK(S.phase == BP_PH_DEBT && v32(&S.debt_amount) == 720 && S.debt_to == 1 && cash(0) == 100 &&
       cash(1) == 1500 && ev(BP_V_DEBT), "720 owed, nothing paid");
    valid("debt");
    refuse(BP_C_ROLL, 0, 0, BP_EPHASE, "no roll in debt");
    refuse(BP_C_END_TURN, 0, 0, BP_EPHASE, "no end turn in debt");
    refuse(BP_C_UPGRADE, 0, 1, BP_EPHASE, "no buying in debt");
    refuse(BP_C_REDEEM, 0, 1, BP_EPHASE, "no redeeming in debt");
    refuse(BP_C_SETTLE, 0, 0, BP_EFUNDS, "short");
    refuse(BP_C_SETTLE, 1, 0, BP_ESEAT, "the creditor cannot settle");
    cmd(BP_C_PROPOSE, 0, 0); C.to = 1; set32(&C.amount, 1); refuse_c(BP_EPHASE, "no trade in debt");
    ok(BP_C_SELL, 0, 1, "sell PET");
    CK(cash(0) == 125, "+25");
    refuse(BP_C_SELL, 0, 1, BP_ERULE, "even selling");
    refuse(BP_C_MORTGAGE, 0, 1, BP_ERULE, "upgrades in the set");
    ok(BP_C_SELL, 0, 3, "VIC"); ok(BP_C_SELL, 0, 4, "C64");
    ok(BP_C_SELL, 0, 1, "PET"); ok(BP_C_SELL, 0, 3, "VIC"); ok(BP_C_SELL, 0, 4, "C64");
    CK(cash(0) == 250, "six steps at 25");
    ok(BP_C_MORTGAGE, 0, 25, "386DX +200"); ok(BP_C_MORTGAGE, 0, 26, "486DX2 +210");
    refuse(BP_C_SETTLE, 0, 0, BP_EFUNDS, "660 < 720");
    ok(BP_C_MORTGAGE, 0, 1, "PET +60");
    CK(cash(0) == 720, "exactly enough");
    ok(BP_C_SETTLE, 0, 0, "settle");
    CK(cash(0) == 0 && cash(1) == 2220 && S.phase == BP_PH_AFTER && S.debt_to == BP_NOBODY &&
       ev(BP_V_SETTLED) && ev(BP_V_RENT), "settled once");
    refuse(BP_C_SETTLE, 0, 0, BP_EPHASE, "not again");
    valid("settled");
}

static void t_bankrupt(void)
{
    /* to a player: liquidation, then everything to the creditor */
    fresh(3);
    own(1, 0, 1, 0); own(3, 0, 1, 0); own(4, 0, 1, 0); own(5, 0, 0, 1);
    own(8, 1, 4, 0); own(9, 1, 4, 0); own(11, 1, 4, 0);
    hold_disk(0, 0);
    setcash(0, 100);
    S.pos[0] = 8; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "onto ZX");
    CK(S.phase == BP_PH_DEBT, "debt");
    ok(BP_C_BANKRUPT, 0, 0, "bankrupt");
    CK(!S.alive[0] && cash(0) == 0 && cash(1) == 1500 + 175, "100 + 3 x 25 to B");
    CK(S.owner[P(1)] == 1 && S.owner[P(3)] == 1 && S.owner[P(4)] == 1 && S.owner[P(5)] == 1 &&
       S.mortgaged[P(5)] && !S.level[P(1)] && !S.level[P(4)], "estate to B, mortgage kept");
    CK(S.disks[1] == 1 && S.disks[0] == 0 && S.deck_n[0] == 7, "disk to B");
    CK(nev(BP_V_SELL) == 3 && ev(BP_V_ESTATE)->other == 1 && v32(&ev(BP_V_ESTATE)->amount) == 175 &&
       ev(BP_V_BANKRUPT), "events");
    CK(S.active == 1 && S.phase == BP_PH_MANAGE && S.round_left == 6 && S.status == BP_ST_PLAY, "B's turn");
    valid("after player bankruptcy");
    refuse(BP_C_ROLL, 0, 0, BP_EOUT, "the eliminated cannot act");
    /* to the bank: properties unowned, level 0, unmortgaged; disks home */
    fresh(3);
    own(1, 0, 0, 1); own(12, 0, 0, 0);
    hold_disk(0, 1);
    setcash(0, 10);
    S.pos[0] = 24; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "onto E-waste");
    CK(S.phase == BP_PH_DEBT && S.debt_to == BP_NOBODY && v32(&S.debt_amount) == 75, "owes the bank");
    ok(BP_C_BANKRUPT, 0, 0, "bankrupt");
    CK(S.owner[P(1)] == BP_NOBODY && !S.mortgaged[P(1)] && S.owner[P(12)] == BP_NOBODY, "bank takes back");
    CK(S.deck_n[1] == 8 && S.disks[0] == 0, "disk home");
    {
        static const BPByte want[8] = {15, 8, 9, 10, 11, 12, 13, 14};
        CK(!memcmp(S.deck[1], want, 8) && S.deck_top[1] == 1, "at the bottom of the cycle");
    }
    CK(ev(BP_V_ESTATE)->other == BP_NOBODY && cash(1) == 1500 && cash(2) == 1500, "cash absorbed");
    valid("after bank bankruptcy");
    /* voluntary: the excess follows the estate */
    fresh(2);
    own(4, 1, 0, 0); setcash(0, 10); S.pos[0] = 1; dice(1, 2);
    ok(BP_C_ROLL, 0, 0, "rent 18 > 10");
    setcash(0, 300);
    ok(BP_C_BANKRUPT, 0, 0, "concede anyway");
    CK(cash(1) == 1800 && S.status == BP_ST_OVER && S.phase == BP_PH_OVER && S.winners == 2 &&
       ev(BP_V_OVER) && ev(BP_V_OVER)->other == 2, "all 300 to B; B wins");
    CK(v32(&S.score[1]) == 1800 + 220 && v32(&S.score[0]) == 0, "scores");
    valid("over");
    refuse(BP_C_ROLL, 1, 0, BP_EOUT, "a resolved match refuses everything");
    refuse(BP_C_END_TURN, 1, 0, BP_EOUT, "end turn");
    refuse(BP_C_MORTGAGE, 1, 4, BP_EOUT, "mortgage");
    /* three seats: B goes out on its turn; C plays next, then A */
    fresh(3);
    S.active = S.decider = 1; S.round_left = 6;
    own(4, 2, 0, 0); setcash(1, 5); S.pos[1] = 1; dice(1, 2);
    ok(BP_C_ROLL, 1, 0, "B onto C's C64");
    ok(BP_C_BANKRUPT, 1, 0, "B out");
    CK(S.active == 2 && S.round_left == 4 && cash(2) == 1505, "C next, not skipped");
    own(3, 2, 0, 0); S.pos[2] = 0; dice(1, 2);
    ok(BP_C_ROLL, 2, 0, "C rolls onto its own VIC-20");
    ok(BP_C_END_TURN, 2, 0, "C ends");
    CK(S.round == 1 && S.active == 0 && S.round_left == 5 && ev(BP_V_ROUND), "round 1 done; A starts round 2");
    /* four seats: the last seat goes out, the round closes */
    fresh(4);
    S.active = S.decider = 3; S.round_left = 8;
    own(4, 0, 0, 0); setcash(3, 5); S.pos[3] = 1; dice(1, 2);
    ok(BP_C_ROLL, 3, 0, "D onto A's C64");
    ok(BP_C_BANKRUPT, 3, 0, "D out");
    CK(S.active == 0 && S.round == 1 && S.round_left == 7 && S.status == BP_ST_PLAY, "A, round 1 complete");
    valid("four less one");
}

static void t_rounds(void)
{
    /* Quick Match: the last END_TURN of the limit round scores the match */
    bp_setup_default(&SU);
    SU.rules = BP_RULES_QUICK; SU.rounds = 10;
    set32(&T, 5);
    CK(bp_new_match(&S, &SU, &T, &R) == BP_OK, "quick match");
    S.active = S.decider = 1; S.round = 9; S.round_left = 2; S.phase = BP_PH_AFTER;
    setcash(0, 1000); own(1, 0, 0, 1); own(4, 0, 0, 0);
    setcash(1, 1280);
    valid("last turn of round 10");
    ok(BP_C_END_TURN, 1, 0, "end the last turn");
    CK(S.round == 10 && S.status == BP_ST_OVER && S.phase == BP_PH_OVER && ev(BP_V_ROUND) &&
       v32(&ev(BP_V_ROUND)->amount) == 10, "10 rounds");
    CK(v32(&S.score[0]) == 1000 + 60 + 220 && v32(&S.score[1]) == 1280 && S.winners == 3, "tie: shared win");
    valid("quick over");
    /* score: price less mortgage principal, plus upgrade resale */
    fresh(3);
    own(1, 0, 2, 0); own(3, 0, 2, 0); own(4, 0, 2, 0); own(5, 0, 0, 1);
    hold_disk(0, 0);
    setcash(0, 7);
    bp_score_of(&S, 0, &T);
    CK(v32(&T) == 7 + 120 + 140 + 220 + 75 + 3 * 2 * 25, "cash + prices - principal + resale; disk 0");
    bp_score_of(&S, 1, &T); CK(v32(&T) == 1500, "cash only");
    CK(bp_seat_assets(&S, 0, &T) == 4 && v32(&T) == 0xF, "asset mask");
    CK(bp_group_owner(&S, BP_G_COMMODORE) == 0 && bp_group_owner(&S, BP_G_HOME) == BP_NOBODY, "group owner");
    /* the winner by score */
    bp_setup_default(&SU);
    SU.rules = BP_RULES_QUICK; SU.rounds = 10; SU.nseats = 3;
    CK(bp_new_match(&S, &SU, &T, &R) == BP_OK, "quick 3");
    S.active = S.decider = 2; S.round = 9; S.round_left = 4; S.phase = BP_PH_AFTER;
    setcash(0, 900); setcash(1, 2000); setcash(2, 1999);
    S.alive[0] = 0; setcash(0, 0);
    ok(BP_C_END_TURN, 2, 0, "end");
    CK(S.winners == 2 && v32(&S.score[0]) == 0, "highest score wins alone; the eliminated score 0");
    /* Classic never ends on rounds */
    fresh(2);
    S.round = 200; S.round_left = 1; S.phase = BP_PH_AFTER;
    ok(BP_C_END_TURN, 0, 0, "end");
    CK(S.status == BP_ST_PLAY && S.round == 201, "classic continues");
}

static void t_refusals(void)
{
    fresh(2);
    refuse(BP_C_ROLL, 1, 0, BP_ESEAT, "wrong seat");
    refuse(BP_C_BUY, 0, 0, BP_EPHASE, "wrong phase");
    refuse(BP_C_ACK_CARD, 0, 0, BP_EPHASE, "no card");
    refuse(BP_C_END_TURN, 0, 0, BP_EPHASE, "not before rolling");
    refuse(BP_C_SETTLE, 0, 0, BP_EPHASE, "no debt");
    refuse(BP_C_NONE, 0, 0, BP_EARG, "type 0");
    refuse(BP_C_COUNT, 0, 0, BP_EARG, "type 20");
    refuse(BP_C_ROLL, 7, 0, BP_ESEAT, "seat 7");
    refuse(BP_C_ROLL, 2, 0, BP_ESEAT, "an absent seat");
    cmd(BP_C_ROLL, 0, 0); C.rev.hi = 1; refuse_c(BP_ESTALE, "stale (high word)");
    cmd(BP_C_ROLL, 0, 0); C.rev.lo++; refuse_c(BP_ESTALE, "future revision");
    S.alive[1] = 0; S.decider = 1;
    refuse(BP_C_ROLL, 1, 0, BP_EOUT, "eliminated actor");
    fresh(2);
    S.status = BP_ST_EMPTY;
    refuse(BP_C_ROLL, 0, 0, BP_EOUT, "no match");
    CK(strcmp(bp_err_text(BP_ESTALE), "?") && !strcmp(bp_err_text(99), "?"), "error text");
    /* rev overflow is refused, not wrapped */
    fresh(2);
    set32(&S.rev, 0xFFFFFFFFu);
    own(1, 0, 0, 0);
    refuse(BP_C_MORTGAGE, 0, 1, BP_EOVERFLOW, "revision would wrap");
    /* cash overflow is refused whole */
    fresh(2);
    setcash(0, 0xFFFFFFFFu - 50); own(1, 0, 0, 0);
    refuse(BP_C_MORTGAGE, 0, 1, BP_EOVERFLOW, "cash would wrap");
}

/* ---- a complete scripted game ------------------------------------------ */
static long bank_delta(void)
{
    long d = 0;
    int i;
    for (i = 0; i < R.n; i++) {
        long a = (long)v32(&R.ev[i].amount);
        switch (R.ev[i].kind) {
        case BP_V_SALARY: case BP_V_GAIN: case BP_V_SELL: case BP_V_MORTGAGE: d += a; break;
        case BP_V_BUY: case BP_V_CHARGE: case BP_V_WON: case BP_V_UPGRADE: case BP_V_REDEEM: d -= a; break;
        case BP_V_ESTATE: if (R.ev[i].other == BP_NOBODY) d -= a; break;
        }
    }
    return d;
}
static long total_cash(void)
{
    long t = 0;
    int i;
    for (i = 0; i < 4; i++) t += (long)cash(i);
    return t;
}
static void invariants(void)
{
    int p, g, lo, hi;
    const BPSpace *sp;
    for (p = 0; p < 18; p++) {
        if (S.owner[p] != BP_NOBODY) CK(S.owner[p] < S.nseats && S.alive[S.owner[p]], "owner alive");
    }
    for (g = 0; g < 6; g++) {
        lo = 9; hi = -1;
        for (p = 0; p < 18; p++) {
            sp = bp_space(bp_prop_space(p));
            if (sp->kind != BP_K_COMPUTER || sp->group != g) continue;
            if (S.level[p] < lo) lo = S.level[p];
            if (S.level[p] > hi) hi = S.level[p];
        }
        CK(hi - lo <= 1 || hi == 0, "even levels");
    }
    for (p = 0; p < 4; p++) CK(cash(p) < 10000000u, "no wrapped balance");
    valid("script step");
}
/* bp_can and bp_check agree on every button (SPEC.md 131.4.9) */
static void greying(void)
{
    static const int args[] = {0, 1, 4, 5, 9, 16, 26};
    int t, seat, a;
    for (t = 1; t < BP_C_COUNT; t++) {
        if (t == BP_C_PROPOSE) continue;
        for (seat = 0; seat < 4; seat++)
            for (a = 0; a < 7; a++) {
                cmd(t, seat, args[a]);
                if (t == BP_C_ACCEPT) C.arg = (BPByte)(S.offer_id & 0xFF);
                if (t == BP_C_BID) bp_bid_min(&S, &C.amount);
                if (bp_can(&S, seat, t, C.arg) != bp_check(&S, &C)) {
                    printf("  greying: type %d seat %d arg %d\n", t, seat, C.arg);
                    CK(0, "bp_can == bp_check");
                    return;
                }
            }
    }
}
static long seen_ev[64], seen_cmd[BP_C_COUNT];
static int script_game(int nseats, uint32_t seed, int rules, int rounds, int *steps_out)
{
    int steps, seat, e, p, sp, did, prev_alive, i, b, k;
    static int bids[4];
    unsigned offered = 0;
    long before;
    bp_setup_default(&SU);
    SU.nseats = (BPByte)nseats; SU.rules = (BPByte)rules; SU.rounds = (BPByte)rounds;
    set32(&T, seed);
    CK(bp_new_match(&S, &SU, &T, &R) == BP_OK, "script new match");
    for (steps = 0; steps < 40000 && S.status == BP_ST_PLAY; steps++) {
        seat = S.decider;
        if ((steps & 15) == 0) greying();
        before = total_cash();
        prev_alive = S.alive[S.active];
        b = S.active;
        did = 0;
        switch (S.phase) {
        case BP_PH_MANAGE: case BP_PH_AFTER:
            /* build when rich: cheapest legal upgrade, then redeem */
            if (cash(seat) > 400)
                for (sp = 0; sp < 28 && !did; sp++)
                    if (bp_can(&S, seat, BP_C_UPGRADE, sp) == BP_OK) { cmd(BP_C_UPGRADE, seat, sp); did = 1; }
            if (!did && cash(seat) > 600)
                for (sp = 0; sp < 28 && !did; sp++)
                    if (bp_can(&S, seat, BP_C_REDEEM, sp) == BP_OK) { cmd(BP_C_REDEEM, seat, sp); did = 1; }
            if (!did && S.phase == BP_PH_MANAGE && offered != S.turn) {
                /* complete a collection: buy its last member at twice its
                 * price (the recipient accepts on odd turns) */
                for (k = 0; k < 18 && !did; k++) {
                    const BPSpace *ks = bp_space(bp_prop_space(k));
                    int m, have = 0, size = 0;
                    i = S.owner[k];
                    if (ks->kind != BP_K_COMPUTER || i == BP_NOBODY || i == seat) continue;
                    for (m = 0; m < 18; m++) {
                        const BPSpace *ms = bp_space(bp_prop_space(m));
                        if (ms->kind != BP_K_COMPUTER || ms->group != ks->group) continue;
                        size++;
                        if (S.owner[m] == seat) have++;
                    }
                    if (have != size - 1 || cash(seat) < 2u * ks->price + 100) continue;
                    cmd(BP_C_PROPOSE, seat, 0); C.to = (BPByte)i;
                    if (k < 16) C.get.lo = (BPWord)(1u << k); else C.get.hi = (BPWord)(1u << (k - 16));
                    set32(&C.amount, 2u * ks->price);
                    if (bp_check(&S, &C) == BP_OK) { did = 1; offered = S.turn; }
                }
            }
            if (!did) cmd(bp_can(&S, seat, BP_C_ROLL, 0) == BP_OK ? BP_C_ROLL : BP_C_END_TURN, seat, 0);
            break;
        case BP_PH_REPAIR:
            if (S.tries[seat] >= 1 && bp_can(&S, seat, BP_C_PAY_BAIL, 0) == BP_OK) cmd(BP_C_PAY_BAIL, seat, 0);
            else if (S.disks[seat]) cmd(BP_C_USE_DISK, seat, (S.disks[seat] & 1) ? 0 : 1);
            else cmd(BP_C_ROLL, seat, 0);
            break;
        case BP_PH_BUY:
            cmd(bp_can(&S, seat, BP_C_BUY, 0) == BP_OK ? BP_C_BUY : BP_C_DECLINE, seat, 0);
            memset(bids, 0, sizeof bids);
            break;
        case BP_PH_AUCTION:
            if (!bids[seat] && bp_can(&S, seat, BP_C_BID, 0) == BP_OK) {
                cmd(BP_C_BID, seat, 0); bp_bid_min(&S, &C.amount); bids[seat] = 1;
            } else cmd(BP_C_PASS, seat, 0);
            break;
        case BP_PH_CARD:
            cmd(BP_C_ACK_CARD, seat, 0);
            break;
        case BP_PH_TRADE:
            cmd((S.offer_id % 3) ? BP_C_ACCEPT : BP_C_REJECT, seat, S.offer_id & 0xFF);
            if (bp_check(&S, &C) != BP_OK) cmd(BP_C_REJECT, seat, 0);
            break;
        case BP_PH_DEBT:
            if (bp_can(&S, seat, BP_C_SETTLE, 0) == BP_OK) { cmd(BP_C_SETTLE, seat, 0); break; }
            for (sp = 0; sp < 28 && !did; sp++)
                if (bp_can(&S, seat, BP_C_SELL, sp) == BP_OK) { cmd(BP_C_SELL, seat, sp); did = 1; }
            for (sp = 0; sp < 28 && !did; sp++)
                if (bp_can(&S, seat, BP_C_MORTGAGE, sp) == BP_OK) { cmd(BP_C_MORTGAGE, seat, sp); did = 1; }
            if (!did) cmd(BP_C_BANKRUPT, seat, 0);
            break;
        default:
            CK(0, "unexpected phase");
            return -1;
        }
        e = run();
        if (e) { printf("  script: seed %u step %d phase %d type %d refused %d\n",
                        (unsigned)seed, steps, S.phase, C.type, e); CK(0, "script command"); return -1; }
        if (total_cash() - before != bank_delta()) {
            printf("  script: seed %u step %d cash %ld vs bank events %ld\n",
                   (unsigned)seed, steps, total_cash() - before, bank_delta());
            CK(0, "cash moves only by documented bank events");
            return -1;
        }
        if (prev_alive && !S.alive[b] && S.status == BP_ST_PLAY) {
            /* the next turn is the next living seat after the bankrupt */
            for (p = 1; p < 4; p++) if (S.alive[(b + p) % nseats]) break;
            CK(S.active == (b + p) % nseats, "turn passes to the next living seat");
        }
        for (i = 0; i < R.n; i++) seen_ev[R.ev[i].kind & 63]++;
        if (C.type < BP_C_COUNT) seen_cmd[C.type]++;
        invariants();
        if (bp_host_fail) return -1;
    }
    *steps_out = steps;
    if (S.status != BP_ST_OVER)
        printf("  script: seed %u seats %d not over: round %u cash %u %u %u %u\n", (unsigned)seed,
               nseats, S.round, (unsigned)cash(0), (unsigned)cash(1), (unsigned)cash(2), (unsigned)cash(3));
    CK(S.status == BP_ST_OVER, "the game ends");
    return S.status == BP_ST_OVER ? 0 : -1;
}
static void t_script(void)
{
    int n, steps, games = 0, total = 0;
    uint32_t seed;
    static BPState A;
    for (n = 2; n <= 4; n++)
        for (seed = 1; seed <= 6; seed++) {
            if (script_game(n, seed * 7919u + (uint32_t)n, BP_RULES_CLASSIC, 0, &steps)) return;
            CK(__builtin_popcount(S.winners) == 1, "classic: one survivor");
            games++; total += steps;
        }
    for (n = 2; n <= 4; n++) {
        if (script_game(n, 4242u + (uint32_t)n, BP_RULES_QUICK, 10, &steps)) return;
        CK(S.round == 10 || __builtin_popcount(S.winners) == 1, "quick: 10 rounds or a survivor");
        games++; total += steps;
    }
    /* determinism: the same seed replays the same game */
    script_game(3, 31337, BP_RULES_CLASSIC, 0, &steps);
    memcpy(&A, &S, sizeof A);
    script_game(3, 31337, BP_RULES_CLASSIC, 0, &n);
    CK(n == steps && !memcmp(&A, &S, sizeof A), "a fixed seed replays exactly");
    printf("t_rules: scripted %d complete games, %d commands\n", games, total);
    printf("t_rules: commands");
    for (n = 1; n < BP_C_COUNT; n++) printf(" %d:%ld", n, seen_cmd[n]);
    printf("\nt_rules: events");
    for (n = 1; n <= BP_V_DISK; n++) printf(" %d:%ld", n, seen_ev[n]);
    printf("\n");
    for (n = 1; n < BP_C_COUNT; n++)
        if (n != BP_C_CANCEL) CK(seen_cmd[n] > 0, "the scripted games used every command");
}

int main(void)
{
    t_math();
    t_rand();
    t_newmatch();
    t_landing();
    t_doubles();
    t_repair();
    t_rent();
    t_mortgage();
    t_upgrade();
    t_auction();
    t_trade();
    t_cards();
    t_debt();
    t_bankrupt();
    t_rounds();
    t_refusals();
    t_script();
    printf("t_rules: %d checks, %s\n", nchk, bp_host_fail ? "FAILED" : "ok");
    return bp_host_fail ? 1 : 0;
}
