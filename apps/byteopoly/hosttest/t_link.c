/* Byteopoly host harness: the cable protocol and the session (SPEC.md
 * 131.5, 131.6, 131.7.5, 131.12). Lane: link.
 *
 * TWO ENDPOINTS IN ONE PROCESS. bp_link.c keeps every byte of session state
 * in a fixed set of statics (bpl, bp_game, bp_mode, bp_here, bp_diag, the
 * scratch state, the snapshot buffer and the message line); tl_use(i) swaps
 * that set out and endpoint i's in, so the SHIPPING session code runs on
 * both sides, unmodified, against a fake bp_ck_* pipe. The pipe models what
 * apps/os88cable.inc guarantees (a body arrives whole with its type's
 * length, or not at all) and injects faults deterministically: drop,
 * duplicate, delay (reorder), corrupt (the framer drops it and counts),
 * cut (a pulled cable), and raw injection that bypasses the framer, to
 * prove the session's own checks. The relay never chooses dice, applies a
 * rule or repairs state.
 *
 * The engine: the real rules and codec (byteoply.c) once those lanes have
 * landed - BP_TL_REAL. Until then a TOY engine and codec below stand in
 * (BP_TL_TOY): small, deterministic, with an RNG in the state, which is all
 * the protocol needs; the complete-game test runs only on the real one.
 * The checkpoint files are a fake per machine (one disk each), with
 * injectable write/commit failures. */
#define BP_LINK_FAKE
#include "bphost.h"
#include "bp.h"

#ifndef BP_HAVE_CKPT_LOAD_ID            /* (bp.h has it once save lands) */
#define BP_HAVE_CKPT_LOAD_ID 1
int bp_ckpt_load_id(int role, const BPU32 *ckpt_id, BPCkpt *meta, BPState *out);
#endif

#ifndef BP_TL_REAL
#define BP_TL_TOY 1
#endif

#ifdef BP_TL_TOY
#include "bp_board.c"
#include "bp_cards.c"
#include "bp_math.c"
/* ---- the toy engine: enough rules to move money, own things and trade ---- */
static const char *bp_errs[BP_ECOUNT] = {
    "OK", "Not now", "Not your decision", "Out of date - try again",
    "Not enough cash", "Not yours", "The rules refuse that", "Too large",
    "Bad request", "Out of the game", "Invalid game state", "Too deep",
    "Waiting for the other PC", "Link paused", "Played on the other PC",
    "Disk error", "Bad file", "Wrong version", "Damaged data", "Wrong match",
    "Disk full or protected"
};
const char *bp_err_text(int code) { return (code < 0 || code >= BP_ECOUNT) ? "?" : bp_errs[code]; }
void bp_setup_default(BPSetup *su)
{
    int i;
    memset(su, 0, sizeof *su);
    su->nseats = 2;
    for (i = 0; i < BP_SEATS; i++) { su->token[i] = (BPByte)i; snprintf(su->name[i], 9, "P%d", i + 1); }
}
int bp_setup_check(const BPSetup *su) { return (su->nseats >= 2 && su->nseats <= 4) ? BP_OK : BP_EARG; }
static int toy_bit(const BPU32 *m, int p) { return p < 16 ? (m->lo >> p) & 1 : (m->hi >> (p - 16)) & 1; }
static unsigned toy_r(BPState *s, unsigned n)
{
    s->rng.lo = (BPWord)(s->rng.lo * 25173u + 13849u);
    s->rng.hi = (BPWord)(s->rng.hi ^ s->rng.lo);
    return (s->rng.lo >> 4) % n;
}
static void toy_ev(BPResult *r, int k, int seat, int other, int space, unsigned amt)
{
    if (r->n >= BP_EV_MAX) return;
    r->ev[r->n].kind = (BPByte)k; r->ev[r->n].seat = (BPByte)seat;
    r->ev[r->n].other = (BPByte)other; r->ev[r->n].space = (BPByte)space;
    r->ev[r->n].amount.lo = (BPWord)amt; r->ev[r->n].amount.hi = 0;
    r->n++;
}
int bp_new_match(BPState *s, const BPSetup *su, const BPU32 *seed, BPResult *r)
{
    int i;
    memset(s, 0, sizeof *s);
    memset(r, 0, sizeof *r);
    if (bp_setup_check(su)) return BP_EARG;
    s->status = BP_ST_PLAY; s->rules = su->rules; s->nseats = su->nseats;
    for (i = 0; i < su->nseats; i++) {
        memcpy(s->name[i], su->name[i], 9); s->token[i] = su->token[i];
        s->machine[i] = su->machine[i]; s->alive[i] = 1; s->cash[i].lo = BP_START_CASH;
    }
    memset(s->owner, BP_NOBODY, sizeof s->owner);
    s->rng = *seed;
    s->match_id.lo = (BPWord)(seed->lo ^ 0xBEEF); s->match_id.hi = seed->hi;
    s->rev.lo = 1; s->phase = BP_PH_MANAGE; s->card = BP_NOBODY;
    r->card = BP_NOBODY;
    return BP_OK;
}
static int toy_owns(const BPState *s, int seat, const BPU32 *m)
{
    int p;
    for (p = 0; p < BP_PROPS; p++) if (toy_bit(m, p) && s->owner[p] != seat) return 0;
    return 1;
}
int bp_check(const BPState *s, const BPCmd *c)
{
    int t = c->type;
    if (s->status != BP_ST_PLAY) return BP_EOUT;
    if (c->rev.lo != s->rev.lo || c->rev.hi != s->rev.hi) return BP_ESTALE;
    if (c->seat != s->decider) return BP_ESEAT;
    switch (s->phase) {
    case BP_PH_MANAGE: if (t != BP_C_ROLL && t != BP_C_PROPOSE) return BP_EPHASE; break;
    case BP_PH_BUY: if (t != BP_C_BUY && t != BP_C_DECLINE) return BP_EPHASE; break;
    case BP_PH_AFTER: if (t != BP_C_END_TURN && t != BP_C_PROPOSE) return BP_EPHASE; break;
    case BP_PH_TRADE: if (t != BP_C_ACCEPT && t != BP_C_REJECT) return BP_EPHASE; break;
    default: return BP_EPHASE;
    }
    if (t == BP_C_BUY && s->cash[c->seat].lo < bp_space(s->pos[c->seat])->price) return BP_EFUNDS;
    if (t == BP_C_PROPOSE) {
        if (c->to >= s->nseats || c->to == c->seat || c->payer > 1) return BP_EARG;
        if (!toy_owns(s, c->seat, &c->give) || !toy_owns(s, c->to, &c->get)) return BP_EOWNER;
        if (c->amount.lo > s->cash[c->payer ? c->to : c->seat].lo) return BP_EFUNDS;
    }
    return BP_OK;
}
int bp_apply(BPState *s, const BPCmd *c, BPResult *r)
{
    int e, me, p, sp, o, to;
    unsigned rent;
    memset(r, 0, sizeof *r);
    r->card = BP_NOBODY;
    e = bp_check(s, c);
    if (e) return e;
    me = c->seat;
    switch (c->type) {
    case BP_C_ROLL:
        r->d1 = s->d1 = (BPByte)(toy_r(s, 6) + 1);
        r->d2 = s->d2 = (BPByte)(toy_r(s, 6) + 1);
        toy_ev(r, BP_V_ROLL, me, r->d1, r->d2, 0);
        sp = s->pos[me] + r->d1 + r->d2;
        if (sp >= BP_SPACES) { sp -= BP_SPACES; s->cash[me].lo += BP_SALARY; toy_ev(r, BP_V_SALARY, me, 0, 0, BP_SALARY); }
        s->pos[me] = (BPByte)sp;
        toy_ev(r, BP_V_MOVE, me, 0, sp, 0);
        p = bp_space(sp)->prop;
        s->phase = BP_PH_AFTER;
        if (p != BP_NOBODY) {
            o = s->owner[p];
            if (o == BP_NOBODY) s->phase = BP_PH_BUY;
            else if (o != me) {
                rent = bp_space(sp)->rent;
                if (rent > s->cash[me].lo) rent = s->cash[me].lo;
                s->cash[me].lo -= (BPWord)rent; s->cash[o].lo += (BPWord)rent;
                toy_ev(r, BP_V_RENT, me, o, sp, rent);
            }
        }
        break;
    case BP_C_BUY:
        sp = s->pos[me];
        s->owner[bp_space(sp)->prop] = (BPByte)me;
        s->cash[me].lo -= bp_space(sp)->price;
        toy_ev(r, BP_V_BUY, me, 0, sp, bp_space(sp)->price);
        s->phase = BP_PH_AFTER;
        break;
    case BP_C_DECLINE: s->phase = BP_PH_AFTER; break;
    case BP_C_END_TURN:
        s->active = (BPByte)((s->active + 1) % s->nseats);
        s->decider = s->active; s->phase = BP_PH_MANAGE; s->turn++;
        toy_ev(r, BP_V_TURN, s->active, 0, 0, 0);
        break;
    case BP_C_PROPOSE:
        s->offer_from = (BPByte)me; s->offer_to = c->to; s->offer_give = c->give; s->offer_get = c->get;
        s->offer_cash = c->amount; s->offer_payer = c->payer; s->offer_disks = c->disks;
        s->offer_id++; s->ret_phase = s->phase; s->phase = BP_PH_TRADE; s->decider = c->to;
        toy_ev(r, BP_V_OFFER, me, c->to, 0, 0);
        break;
    case BP_C_ACCEPT:
        to = s->offer_to; o = s->offer_from;
        for (p = 0; p < BP_PROPS; p++) {
            if (toy_bit(&s->offer_give, p)) s->owner[p] = (BPByte)to;
            if (toy_bit(&s->offer_get, p)) s->owner[p] = (BPByte)o;
        }
        if (s->offer_payer) { s->cash[to].lo -= s->offer_cash.lo; s->cash[o].lo += s->offer_cash.lo; }
        else { s->cash[o].lo -= s->offer_cash.lo; s->cash[to].lo += s->offer_cash.lo; }
        toy_ev(r, BP_V_TRADED, o, to, 0, s->offer_cash.lo);
        s->phase = s->ret_phase; s->decider = s->active;
        break;
    case BP_C_REJECT:
        toy_ev(r, BP_V_REJECTED, me, 0, 0, 0);
        s->phase = s->ret_phase; s->decider = s->active;
        break;
    }
    if (!++s->rev.lo) s->rev.hi++;
    return BP_OK;
}
int bp_can(const BPState *s, int seat, int type, int arg)
{
    static BPCmd c;
    memset(&c, 0, sizeof c);
    c.type = (BPByte)type; c.seat = (BPByte)seat; c.arg = (BPByte)arg; c.rev = s->rev;
    return bp_check(s, &c);
}
/* ---- the toy codec: the harness's own, never a wire format ---------------- */
BPWord bp_crc16(const BPByte *p, unsigned n, BPWord crc)
{
    int i;
    while (n--) {
        crc ^= (BPWord)(*p++ << 8);
        for (i = 0; i < 8; i++) crc = (BPWord)(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}
unsigned bp_encode(const BPState *s, BPByte *buf, unsigned cap)
{
    if (cap < sizeof *s) return 0;
    memcpy(buf, s, sizeof *s);
    return sizeof *s;
}
int bp_validate(const BPState *s) { return s->status <= BP_ST_OVER && s->nseats <= BP_SEATS ? BP_OK : BP_EINVALID; }
int bp_decode(const BPByte *buf, unsigned n, BPState *out)
{
    if (n != sizeof *out) return BP_EFORMAT;
    memcpy(out, buf, n);
    return bp_validate(out);
}
void bp_digest(const BPState *s, BPU32 *out)
{
    static BPByte b[BP_STATE_MAX];
    unsigned n = bp_encode(s, b, sizeof b);
    out->lo = bp_crc16(b, n, 0xFFFF);
    out->hi = bp_crc16(b, n, 0x1D0F);
}
void bp_fingerprint(BPU32 *out) { out->lo = 0x7357; out->hi = BP_RULES_VER; }
#include "bp_link.c"
#else
#include "byteoply.c"
#endif

/* ---- the two endpoints ---------------------------------------------------- */
#define TL_Q 96
typedef struct { BPByte n, b[BP_CK_MAX]; unsigned at; } TFrame;
typedef struct { int has; BPCkpt m; BPState s; } TSlot;
typedef struct {
    /* the swap list: exactly the statics bp_link.c keeps */
    BPLink l; BPState g, scr; BPByte mode, here; BPDiag d;
    BPByte snap[BP_STATE_MAX]; char line[40];
    /* the fake machine around it */
    TFrame q[TL_Q]; int qn;             /* frames in flight TO this endpoint */
    int open, crc, commits, links, chats;
    char chat[BP_CHAT_LEN + 1];
    TSlot cand, cur, prev;              /* checkpoint files on its own disk */
    int wfail, cfail;
    long bank;                          /* cash the events say came from/to the bank */
    int salaries, buys, trades, rents;
} TEnd;
static TEnd E[2];
static int tl_cur = 0;
static unsigned tl_rand = 12345;
/* faults, per direction (index = the SENDER) */
static struct {
    int cut;                            /* a pulled cable: everything lost */
    int drop_type, drop_n;              /* drop the next n frames of this type */
    int dup_type, dup_n;
    int delay_type, delay_n, delay_ticks;
    int corrupt_type, corrupt_n;        /* the framer's CRC drops it */
    int full_n;                         /* bp_ck_send answers "no room" n times */
} F[2];

static void tl_swap_out(int i)
{
    TEnd *e = &E[i];
    e->l = bpl; e->g = bp_game; e->scr = bpl_scr; e->mode = bp_mode; e->here = bp_here; e->d = bp_diag;
    memcpy(e->snap, bpl_snap, sizeof bpl_snap); memcpy(e->line, bpl_line, sizeof bpl_line);
}
static void tl_swap_in(int i)
{
    TEnd *e = &E[i];
    bpl = e->l; bp_game = e->g; bpl_scr = e->scr; bp_mode = e->mode; bp_here = e->here; bp_diag = e->d;
    memcpy(bpl_snap, e->snap, sizeof bpl_snap); memcpy(bpl_line, e->line, sizeof bpl_line);
}
static BPLink *tl_l(int i) { return i == tl_cur ? &bpl : &E[i].l; }
static BPState *tl_g(int i) { return i == tl_cur ? &bp_game : &E[i].g; }
static void tl_use(int i)
{
    if (i == tl_cur) return;
    tl_swap_out(tl_cur);
    tl_swap_in(i);
    tl_cur = i;
}

/* ---- the fake bridge ------------------------------------------------------ */
static void tl_push(int to, const BPByte *b, unsigned n, unsigned at)
{
    TEnd *e = &E[to];
    if (e->qn >= TL_Q) return;          /* the lease ring overflowed: lost */
    e->q[e->qn].n = (BPByte)n;
    memcpy(e->q[e->qn].b, b, n);
    e->q[e->qn].at = at;
    e->qn++;
}
/* a frame straight into endpoint `to`'s receive path, past the framer */
static void tl_inject(int to, const BPByte *b, unsigned n) { tl_push(to, b, n, 0); }

int bp_ck_open(int port) { (void)port; E[tl_cur].open = 1; return BP_OK; }
void bp_ck_close(void) { E[tl_cur].open = 0; E[tl_cur].qn = 0; }
void bp_ck_tick(void) { bp_diag.crc = (BPWord)E[tl_cur].crc; }
int bp_ck_heard(void) { return 1; }
int bp_ck_port(void) { return E[tl_cur].open ? 2 : 0; }
const char *bp_ck_msg(void) { return "Looking for the cable..."; }
BPWord bp_ck_rand(void) { tl_rand = tl_rand * 1103515245u + 12345u; return (BPWord)(tl_rand >> 8); }
int bp_ck_send(const BPByte *body, unsigned n)
{
    int s = tl_cur, t = body[0], to = 1 - tl_cur;
    BP_CHECK(n >= 3 && n <= BP_CK_MAX && t < BPF_N && n + 2 == bpl_lens[t], "sent a frame of the wrong length");
    if (!E[s].open) return BP_EPENDING;
    if (F[s].full_n > 0) { F[s].full_n--; return BP_EPENDING; }
    if (F[s].cut || !E[to].open) return BP_OK;          /* on the wire, and lost */
    if (F[s].drop_n > 0 && (F[s].drop_type == t || F[s].drop_type == 0)) { F[s].drop_n--; return BP_OK; }
    if (F[s].corrupt_n > 0 && (F[s].corrupt_type == t || F[s].corrupt_type == 0)) {
        F[s].corrupt_n--; E[to].crc++; return BP_OK;
    }
    if (F[s].delay_n > 0 && F[s].delay_type == t) {
        F[s].delay_n--; tl_push(to, body, n, bp_host_ticks + (unsigned)F[s].delay_ticks); return BP_OK;
    }
    tl_push(to, body, n, 0);
    if (F[s].dup_n > 0 && (F[s].dup_type == t || F[s].dup_type == 0)) { F[s].dup_n--; tl_push(to, body, n, 0); }
    return BP_OK;
}
int bp_ck_recv(BPByte *body)
{
    TEnd *e = &E[tl_cur];
    int i, n;
    for (i = 0; i < e->qn; i++) {
        if (e->q[i].at > bp_host_ticks) continue;       /* delayed: others overtake */
        n = e->q[i].n;
        memcpy(body, e->q[i].b, (size_t)n);
        memmove(&e->q[i], &e->q[i + 1], sizeof(TFrame) * (size_t)(e->qn - i - 1));
        e->qn--;
        return n;
    }
    return 0;
}

/* ---- the UI callbacks: counters and the economic invariants ---------------- */
static long tl_cash(const BPState *s)
{
    long t = 0;
    int i;
    for (i = 0; i < BP_SEATS; i++) t += (long)s->cash[i].lo + 65536L * s->cash[i].hi;
    return t;
}
static long tl_prev_cash[2];
void bp_ui_committed(const BPResult *r)
{
    TEnd *e = &E[tl_cur];
    int i, p, owners;
    long bank = 0;
    e->commits++;
    for (i = 0; i < r->n; i++) {
        long a = (long)r->ev[i].amount.lo + 65536L * r->ev[i].amount.hi;
        switch (r->ev[i].kind) {
        case BP_V_SALARY: case BP_V_GAIN: case BP_V_SELL: case BP_V_MORTGAGE: bank += a; if (r->ev[i].kind == BP_V_SALARY) e->salaries++; break;
        case BP_V_BUY: case BP_V_CHARGE: case BP_V_UPGRADE: case BP_V_REDEEM: case BP_V_WON: bank -= a; if (r->ev[i].kind == BP_V_BUY) e->buys++; break;
        case BP_V_RENT: e->rents++; break;
        case BP_V_TRADED: e->trades++; break;
        default: break;
        }
    }
    /* cash moves only by the bank events the result names (an adopted
     * snapshot or resume reports no events and resets the base) */
    if (r->n && bp_game.status == BP_ST_PLAY)
        BP_CHECK(tl_cash(&bp_game) - tl_prev_cash[tl_cur] == bank, "cash changed outside a documented bank event");
    tl_prev_cash[tl_cur] = tl_cash(&bp_game);
    e->bank += bank;
    for (p = 0; p < BP_PROPS; p++) {
        owners = bp_game.owner[p];
        BP_CHECK(owners == BP_NOBODY || owners < bp_game.nseats, "an asset with no valid owner");
    }
}
void bp_ui_link(void) { E[tl_cur].links++; }
void bp_ui_chat(int machine, const char *text)
{
    (void)machine;
    E[tl_cur].chats++;
    snprintf(E[tl_cur].chat, sizeof E[tl_cur].chat, "%s", text);
}

/* ---- the fake checkpoint files (one disk per machine) ----------------------- */
int bp_ckpt_write(const BPCkpt *m, const BPState *s)
{
    TEnd *e = &E[tl_cur];
    if (e->wfail) return e->wfail;
    e->cand.has = 1; e->cand.m = *m; e->cand.m.state = 1; e->cand.s = *s;
    return BP_OK;
}
int bp_ckpt_commit(const BPCkpt *m)
{
    TEnd *e = &E[tl_cur];
    if (e->cfail) return e->cfail;
    if (!e->cand.has || e->cand.m.ckpt_id.lo != m->ckpt_id.lo || e->cand.m.ckpt_id.hi != m->ckpt_id.hi) return BP_EIO;
    if (e->cur.has) e->prev = e->cur;
    e->cur = e->cand; e->cur.m.state = 2; e->cand.has = 0;
    return BP_OK;
}
static int tl_give(const TSlot *t, int role, BPCkpt *m, BPState *out)
{
    if (!t->has || t->m.role != role) return BP_EIO;
    *m = t->m; *out = t->s;
    return BP_OK;
}
int bp_ckpt_load(int role, BPCkpt *m, BPState *out) { return tl_give(&E[tl_cur].cur, role, m, out); }
int bp_ckpt_load_id(int role, const BPU32 *id, BPCkpt *m, BPState *out)
{
    TEnd *e = &E[tl_cur];
    if (e->cur.has && bpl_eq(&e->cur.m.ckpt_id, id)) return tl_give(&e->cur, role, m, out);
    if (e->prev.has && bpl_eq(&e->prev.m.ckpt_id, id)) return tl_give(&e->prev, role, m, out);
    if (e->cand.has && bpl_eq(&e->cand.m.ckpt_id, id)) return tl_give(&e->cand, role, m, out);
    return BP_EIO;
}

/* ---- driving ---------------------------------------------------------------- */
static void tl_step(void)
{
    int i;
    bp_host_ticks++;
    for (i = 0; i < 2; i++) { tl_use(i); bp_sess_tick(); }
}
static void tl_run(int n) { while (n-- > 0) tl_step(); }
static void tl_dump(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        BPLink *l = tl_l(i);
        BPState *g = tl_g(i);
        tl_use(i);
        printf("  [%d] st=%d ph=%d pend=%d cpend=%d tries=%d div=%d snap=%d ck=%d rev=%u hw=%u cseq=%u quiet=%u qn=%d msg=%s\n",
               i, bp_link_state(), l->ph, l->pend, l->cpend, l->tries, l->diverged, l->snap, l->ck,
               g->rev.lo, l->hw, l->cseq, (unsigned)(BPWord)(l->clk - l->heard), E[i].qn, l->msg ? l->msg : "-");
        { int j; for (j = 0; j < E[i].qn; j++) printf("     q type %d at %u (now %u)\n", E[i].q[j].b[0], E[i].q[j].at, bp_host_ticks); }
        printf("     tx=%u rx=%u frame=%u\n", tl_l(i) == &bpl ? bp_diag.tx : E[i].d.tx, tl_l(i) == &bpl ? bp_diag.rx : E[i].d.rx, tl_l(i) == &bpl ? bp_diag.frame : E[i].d.frame);
    }
}
#define TL_UNTIL(cond, max, what) do { int k_ = 0; while (!(cond) && k_++ < (max)) tl_step(); if (!(cond)) tl_dump(); BP_CHECK(cond, what); } while (0)
static int tl_state(int i) { tl_use(i); return bp_link_state(); }
static unsigned tl_rev(int i) { tl_use(i); return bp_game.rev.lo; }
static int tl_same(void)
{
    BPU32 a, b;
    tl_use(0); bp_digest(&bp_game, &a);
    tl_use(1); bp_digest(&bp_game, &b);
    return bpl_eq(&a, &b) && tl_g(0)->rev.lo == tl_g(1)->rev.lo;
}
static int tl_idle(void)
{
    int b0, b1;
    tl_use(0); b0 = bp_sess_busy();
    tl_use(1); b1 = bp_sess_busy();
    return !b0 && !b1;
}
static void tl_reset_all(void)
{
    int i;
    for (i = 0; i < 2; i++) { tl_use(i); bp_sess_local(); }
    tl_use(0);
    memset(F, 0, sizeof F);
    for (i = 0; i < 2; i++) { E[i].qn = 0; E[i].crc = 0; }
}
/* host and join, agree a setup with `nh` host seats and `nc` client seats,
 * both ready, start; returns 1 when both play in sync */
static int tl_connect(int nh, int nc)
{
    static BPSetup su;
    int i;
    tl_reset_all();
    tl_use(0); BP_CHECK(bp_link_host(0) == BP_OK, "host opens");
    tl_use(1); BP_CHECK(bp_link_join(0) == BP_OK, "join opens");
    TL_UNTIL(tl_state(0) == BP_L_LOBBY && tl_state(1) == BP_L_LOBBY, 200, "both reach the lobby");
    tl_use(0);
    bp_setup_default(&su);
    su.nseats = (BPByte)(nh + nc);
    for (i = 0; i < su.nseats; i++) su.machine[i] = (BPByte)(i >= nh);
    BP_CHECK(bp_lobby_set(&su) == BP_OK, "host sets the roster");
    TL_UNTIL(tl_l(1)->lseq == tl_l(0)->lseq && tl_l(1)->lob.nseats == su.nseats, 100, "client has the roster");
    tl_use(1);
    for (i = 0; i < su.nseats; i++) BP_CHECK(bp_sess_mine(i) == (i >= nh), "client seat ownership in the lobby");
    BP_CHECK(bp_lobby_ready(1) == BP_OK, "client ready");
    tl_use(0);
    BP_CHECK(bp_lobby_ready(1) == BP_OK, "host ready");
    TL_UNTIL(tl_l(0)->rmask == 3, 100, "host sees both ready");
    tl_use(1);
    TL_UNTIL((tl_use(1), bp_lobby_peer_ready()), 100, "client sees the host ready");
    tl_use(0);
    BP_CHECK(bp_lobby_start() == BP_OK, "host starts");
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY && tl_idle(), 200, "both play");
    return tl_same();
}
/* the decider's machine submits cmd type `t` (arg) for the decider */
static int tl_submit_as_decider(int type, int arg)
{
    static BPCmd c;
    int m, e;
    tl_use(0);
    m = bp_game.machine[bp_game.decider];
    tl_use(m);
    memset(&c, 0, sizeof c);
    c.type = (BPByte)type; c.seat = bp_game.decider; c.arg = (BPByte)arg; c.rev = bp_game.rev;
    e = bp_sess_submit(&c);
    return e;
}
/* a simple policy move for the toy engine: whatever the phase wants */
static int tl_policy_move(void)
{
    int ph;
    tl_use(0);
    ph = bp_game.phase;
    if (ph == BP_PH_MANAGE) return tl_submit_as_decider(BP_C_ROLL, 0);
    if (ph == BP_PH_BUY) return tl_submit_as_decider((bp_game.turn & 1) ? BP_C_DECLINE : BP_C_BUY, 0);
    if (ph == BP_PH_AFTER) return tl_submit_as_decider(BP_C_END_TURN, 0);
    if (ph == BP_PH_TRADE) return tl_submit_as_decider(BP_C_ACCEPT, 0);
    return BP_EPHASE;
}
static void tl_one_move(const char *what)
{
    unsigned r0 = tl_rev(0);
    int e = tl_policy_move();
    BP_CHECK(e == BP_OK || e == BP_EPENDING, what);
    TL_UNTIL(tl_idle() && tl_rev(0) == r0 + 1 && tl_rev(1) == r0 + 1, 400, what);
    BP_CHECK(tl_same(), "digests agree after the move");
}

/* ---- the tests ------------------------------------------------------------------ */
static void t_splits(void)
{
    static const int sp[6][2] = { {1,1}, {1,2}, {2,1}, {2,2}, {1,3}, {3,1} };
    static BPCmd c;
    int k, s, e;
    for (k = 0; k < 6; k++) {
        BP_CHECK(tl_connect(sp[k][0], sp[k][1]), "split connects in sync");
        for (s = 0; s < sp[k][0] + sp[k][1]; s++) {
            int owner = s >= sp[k][0];
            tl_use(owner); BP_CHECK(bp_sess_mine(s), "seat is mine on its machine");
            tl_use(1 - owner); BP_CHECK(!bp_sess_mine(s), "seat is not mine on the other");
            memset(&c, 0, sizeof c);
            c.type = BP_C_ROLL; c.seat = (BPByte)s; c.rev = bp_game.rev;
            e = bp_sess_submit(&c);
            BP_CHECK(e == BP_ENOTMINE, "the other machine's seat is refused before any traffic");
        }
        /* play two full rounds of the toy: every seat's turn passes through
         * its own machine */
        for (s = 0; s < 6 * (sp[k][0] + sp[k][1]); s++) tl_one_move("a move in every split");
    }
}

/* a forged INTENT naming a host seat, sent from the client: refused, nothing changes */
static void t_forged_seat(void)
{
    static BPByte b[BP_CK_MAX];
    unsigned r;
    BP_CHECK(tl_connect(1, 1), "1+1 for forging");
    r = tl_rev(0);
    memset(b, 0, sizeof b);
    b[0] = BPF_INTENT;
    b[1] = (BPByte)tl_l(1)->me.lo; b[2] = (BPByte)(tl_l(1)->me.lo >> 8);
    b[3] = 99; b[4] = 0;                /* seq 99 */
    b[5] = BP_C_ROLL; b[6] = 0;         /* seat 0: the host's */
    b[9] = (BPByte)r;
    tl_inject(0, b, 27);
    tl_run(20);
    BP_CHECK(tl_rev(0) == r && tl_rev(1) == r && tl_same(), "a forged seat changed nothing");
}

static void t_faults(void)
{
    int k;
    unsigned r;
    BPWord hw;
    BP_CHECK(tl_connect(1, 1), "1+1 for faults");
    /* make the client the decider: host ends its first turn */
    tl_one_move("host rolls");
    for (k = 0; k < 20 && (tl_use(0), bp_game.decider) != 1; k++) tl_one_move("to the client's turn");

    /* lost INTENT: the retry is applied, once */
    r = tl_rev(0);
    F[1].drop_type = BPF_INTENT; F[1].drop_n = 1;
    tl_policy_move();
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1, 400, "lost intent recovered");
    BP_CHECK(tl_rev(0) == r + 1 && tl_same(), "lost intent applied exactly once");

    /* the result lost AFTER the host committed: the retained copy, once */
    r = tl_rev(0);
    F[0].drop_type = BPF_RESULT; F[0].drop_n = 2;
    tl_policy_move();
    tl_run(3);
    BP_CHECK(tl_rev(0) == r + 1 && tl_rev(1) == r, "host committed, client has not");
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1, 400, "lost result recovered");
    BP_CHECK(tl_rev(0) == r + 1 && tl_same(), "no second application after a lost result");

    /* the ACK lost: the host retries, the client only re-acknowledges */
    r = tl_rev(0);
    F[1].drop_type = BPF_ACK; F[1].drop_n = 2;
    tl_policy_move();
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1, 400, "lost ack recovered");
    BP_CHECK(tl_rev(0) == r + 1 && tl_same(), "a lost ack applied nothing twice");

    /* duplicated and reordered results */
    r = tl_rev(0);
    F[0].dup_type = BPF_RESULT; F[0].dup_n = 3;
    F[0].delay_type = BPF_RESULT; F[0].delay_n = 1; F[0].delay_ticks = 60;
    tl_policy_move();
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1, 400, "duplicated/delayed result");
    tl_run(80);                         /* the delayed copy arrives late */
    BP_CHECK(tl_rev(0) == r + 1 && tl_rev(1) == r + 1 && tl_same(), "duplicates and a late copy applied once");

    /* a stale intent replayed (an old seq) and a duplicated intent */
    r = tl_rev(0);
    tl_use(0); hw = bpl.hw;
    F[1].dup_type = BPF_INTENT; F[1].dup_n = 2;
    tl_policy_move();
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1, 400, "duplicated intent");
    BP_CHECK(tl_rev(0) == r + 1 && tl_same(), "a duplicated intent applied once");
    BP_CHECK(tl_l(0)->hw == hw + (tl_use(1), (BPWord)(bpl.cseq - hw)), "high-water mark tracks the seq");

    /* corrupted frames (the framer drops them) are counted and retried */
    r = tl_rev(0);
    F[0].corrupt_type = 0; F[0].corrupt_n = 2;
    tl_policy_move();
    TL_UNTIL(tl_idle() && tl_rev(1) == r + 1 && tl_rev(0) == r + 1, 400, "corrupt frames recovered");
    tl_use(1); BP_CHECK(bp_diag.crc >= 1, "the framer's drops reach bp_diag.crc");
    BP_CHECK(tl_same(), "in sync after corruption");
}

/* malformed, wrong-tag, wrong-type, wrong-direction, wrong-match frames:
 * bounded and non-mutating */
static void t_malformed(void)
{
    static BPByte b[BP_CK_MAX];
    BPWord f0;
    unsigned r;
    int i;
    BP_CHECK(tl_connect(2, 2), "2+2 for malformed frames");
    tl_run(5);
    r = tl_rev(0);
    tl_use(1); f0 = bp_diag.frame;
    /* a RESULT of the wrong length */
    memset(b, 0, sizeof b); b[0] = BPF_RESULT;
    b[1] = (BPByte)tl_l(0)->me.lo; b[2] = (BPByte)(tl_l(0)->me.lo >> 8);
    tl_inject(1, b, 20);
    /* a well-formed RESULT from the wrong peer */
    b[1] ^= 0x55;
    b[6] = (BPByte)(r + 1);
    tl_inject(1, b, 25);
    /* an unknown type, a zero type */
    b[0] = 40; tl_inject(1, b, 10);
    b[0] = 0; tl_inject(1, b, 3);
    /* a host-only frame sent to the client */
    b[0] = BPF_INTENT; b[1] = (BPByte)tl_l(0)->me.lo; b[2] = (BPByte)(tl_l(0)->me.lo >> 8);
    tl_inject(1, b, 27);
    /* a SNAPHEAD for another match (the client is not adopting: refused) */
    b[0] = BPF_SNAPHEAD; b[3] = 0x77; tl_inject(1, b, 17);
    /* noise: a burst of random frames of valid lengths and the right tag */
    for (i = 0; i < 40; i++) {
        int t = 1 + (int)(bp_ck_rand() % (BPF_N - 1)), j;
        memset(b, 0, sizeof b);
        b[0] = (BPByte)t;
        for (j = 3; j < BP_CK_MAX; j++) b[j] = (BPByte)bp_ck_rand();
        if (t == BPF_RESULT || t == BPF_START || t == BPF_CKCOMMIT || t == BPF_CKBEGIN
            || t == BPF_SNAPDATA || t == BPF_SNAPHEAD || t == BPF_RESUME || t == BPF_BYE
            || t == BPF_HELLO || t == BPF_PING || t == BPF_TERMS) continue;  /* (those are judged by revision/digest/id below) */
        b[1] = (BPByte)tl_l(0)->me.lo; b[2] = (BPByte)(tl_l(0)->me.lo >> 8);
        tl_inject(1, b, (unsigned)(bpl_lens[t] - 2));
    }
    tl_run(30);
    tl_use(1);
    BP_CHECK(bp_diag.frame > f0, "malformed frames counted");
    BP_CHECK(tl_rev(0) == r && tl_rev(1) == r && tl_same(), "malformed frames changed nothing");
    BP_CHECK(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY, "still playing after noise");
    /* a stranger's HELLO mid-match */
    memset(b, 0, sizeof b);
    b[0] = BPF_HELLO; memcpy(b + 1, "BYTO", 4); b[5] = BPL_VER; b[6] = BP_MODE_CLIENT;
    b[7] = 0x11; b[8] = 0x22; b[9] = 0x33; b[10] = 0x44;
    b[11] = 0x57; b[12] = 0x73; b[13] = BP_RULES_VER;
    tl_inject(0, b, 24);
    tl_run(5);
    tl_use(0);
    BP_CHECK(bpl.msg && strstr(bpl.msg, "not this match"), "a stranger's HELLO is named, not bound");
    BP_CHECK(tl_same() && tl_state(0) == BP_L_PLAY, "the stranger changed nothing");
    tl_one_move("play goes on after the stranger");
}

/* a pulled cable at each point of a transaction: pause, keep it, finish once */
static void t_pulled(void)
{
    int at, k;
    unsigned r;
    for (at = 0; at < 4; at++) {
        BP_CHECK(tl_connect(1, 2), "1+2 for the pulled cable");
        for (k = 0; k < 20 && (tl_use(0), bp_game.decider) == 0; k++) tl_one_move("to a client seat");
        r = tl_rev(0);
        if (at == 0) { F[1].cut = 1; F[0].cut = 1; }    /* before the intent */
        tl_policy_move();
        if (at == 1) { tl_run(2); F[0].cut = F[1].cut = 1; }    /* host committed, result in flight */
        if (at == 2) { F[0].cut = 0; F[1].drop_type = BPF_ACK; F[1].drop_n = 1; tl_run(4); F[0].cut = F[1].cut = 1; }
        if (at == 3) { tl_run(1); F[0].cut = F[1].cut = 1; }
        tl_run(400);
        BP_CHECK(tl_state(0) == BP_L_PAUSED && tl_state(1) == BP_L_PAUSED, "both pause on silence");
        tl_use(1);
        {
            static BPCmd c;
            memset(&c, 0, sizeof c); c.type = BP_C_ROLL; c.seat = bp_game.decider; c.rev = bp_game.rev;
            BP_CHECK(bp_sess_submit(&c) == BP_EPAUSED, "input refused while paused");
        }
        BP_CHECK(tl_rev(0) <= r + 1 && tl_rev(1) <= r + 1, "nothing advanced twice while paused");
        F[0].cut = F[1].cut = 0;
        printf("  pulled at %d\n", at);
        TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY && tl_idle(), 600, "resumes after the cable returns");
        BP_CHECK(tl_rev(0) == r + 1 && tl_rev(1) == r + 1 && tl_same(), "the pending transaction finished exactly once");
    }
}

/* a forced digest mismatch, resolved by the client adopting the banker's board */
static void t_diverge(void)
{
    int k;
    BP_CHECK(tl_connect(2, 1), "2+1 for divergence");
    tl_one_move("a move before the damage");
    tl_use(1);
    bp_game.cash[0].lo += 7;            /* the client's board is now wrong */
    for (k = 0; k < 60; k++) { tl_step(); if (getenv("TLDBG")) { printf("t%d\n", k); tl_dump(); } }                         /* a PING at an equal revision says so */
    BP_CHECK(tl_state(1) == BP_L_DIVERGED, "client sees the divergence");
    BP_CHECK(tl_state(0) == BP_L_DIVERGED, "host sees it too");
    {
        static BPCmd c;
        tl_use(0);
        memset(&c, 0, sizeof c); c.type = BP_C_ROLL; c.seat = bp_game.decider; c.rev = bp_game.rev;
        BP_CHECK(bp_sess_submit(&c) != BP_OK, "economic input stopped on the host");
    }
    tl_use(1);
    BP_CHECK(bp_sess_adopt() == BP_OK, "client adopts");
    F[0].drop_type = BPF_SNAPDATA; F[0].drop_n = 1;     /* and a chunk is lost */
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY && tl_idle(), 600, "snapshot adopted");
    BP_CHECK(tl_same(), "boards agree after the snapshot");
    tl_use(1); BP_CHECK(bp_diag.resync >= 1, "resync counted");
    tl_one_move("play after adoption");
    /* a divergence found at a RESULT (the client's state damaged mid-turn) */
    tl_use(1);
    bp_game.rng.lo ^= 0x1234;
    for (k = 0; k < 20 && (tl_use(0), bp_game.phase) != BP_PH_MANAGE; k++) tl_one_move("to a roll");
    tl_policy_move();
    TL_UNTIL(tl_state(1) == BP_L_DIVERGED, 200, "a result's dice disagree");
    tl_use(1); BP_CHECK(bp_sess_adopt() == BP_OK, "adopt again");
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY && tl_idle(), 600, "second adoption");
    BP_CHECK(tl_same(), "agree after the second adoption");
}

static void t_chat(void)
{
    BP_CHECK(tl_connect(1, 1), "1+1 for chat");
    tl_use(1);
    BP_CHECK(bp_chat_send("hello banker, roll 7 now please and more") == BP_OK, "chat sent");
    BP_CHECK(bp_chat_send("again") == BP_EPENDING, "chat rate-limited");
    F[1].dup_type = BPF_CHAT; F[1].dup_n = 2;
    F[0].drop_type = BPF_CHATACK; F[0].drop_n = 1;
    tl_run(80);
    BP_CHECK(E[0].chats == 1 && !strcmp(E[0].chat, "hello banker, roll 7 now"), "chat shown once, cut to 24");
    tl_use(1); BP_CHECK(!bpl.cbusy, "chat acknowledged");
    tl_one_move("chat does not disturb play");
}

static void t_ckpt(void)
{
    BPU32 c1;
    unsigned r;
    BP_CHECK(tl_connect(2, 2), "2+2 for checkpoints");
    tl_one_move("a move");
    /* a paired save asked by the client, behind nothing */
    tl_use(1);
    BP_CHECK(bp_sess_save_pair() == BP_OK, "client asks for a paired save");
    TL_UNTIL((tl_use(0), bp_sess_pair()) == BP_PAIR_DONE && (tl_use(1), bp_sess_pair()) == BP_PAIR_DONE, 300, "paired save complete on both");
    BP_CHECK(E[0].cur.has && E[1].cur.has && bpl_eq(&E[0].cur.m.ckpt_id, &E[1].cur.m.ckpt_id), "a committed pair");
    BP_CHECK(bpl_eq(&E[0].cur.m.digest, &E[1].cur.m.digest) && E[0].cur.m.role == 1 && E[1].cur.m.role == 2, "same digest, distinct roles");
    c1 = E[0].cur.m.ckpt_id;
    /* a save asked while a transaction is outstanding waits behind it */
    tl_one_move("another move");
    r = tl_rev(0);
    tl_policy_move();
    tl_use(0);
    BP_CHECK(bp_sess_save_pair() == BP_OK, "host queues a save behind the transaction");
    TL_UNTIL((tl_use(0), bp_sess_pair()) == BP_PAIR_DONE && (tl_use(1), bp_sess_pair()) == BP_PAIR_DONE, 400, "queued save completes");
    BP_CHECK(tl_rev(0) == r + 1 && E[0].cur.m.rev.lo == r + 1, "the save is after the transaction, which ran once");
    BP_CHECK(bpl_eq(&E[0].prev.m.ckpt_id, &c1), "the previous pair is kept");
    /* the client cannot write: neither commits, the previous pair stands */
    c1 = E[0].cur.m.ckpt_id;
    E[1].wfail = BP_EFULL;
    tl_use(0); BP_CHECK(bp_sess_save_pair() == BP_OK, "host saves");
    TL_UNTIL((tl_use(0), bp_sess_pair()) == BP_PAIR_FAIL, 300, "host reports the client's failure");
    BP_CHECK((tl_use(1), bp_sess_pair()) == BP_PAIR_FAIL, "client reports it too");
    BP_CHECK(bpl_eq(&E[0].cur.m.ckpt_id, &c1) && bpl_eq(&E[1].cur.m.ckpt_id, &c1), "no side committed: the last pair stands");
    E[1].wfail = 0;
    BP_CHECK(tl_idle() && tl_state(0) == BP_L_PLAY, "play continues after a failed save");
    tl_one_move("a move after the failed save");
}

/* close both applications and resume: the plain case, the roll-forward
 * (host committed, client did not), refusals */
static void tl_reopen(int host_resume, int client_resume)
{
    int i;
    for (i = 0; i < 2; i++) { tl_use(i); bp_sess_local(); bp_game.status = BP_ST_EMPTY; bp_game.rev.lo = 0; }
    memset(F, 0, sizeof F);
    tl_use(0); BP_CHECK(bp_link_host(0) == BP_OK, "host reopens");
    if (host_resume) BP_CHECK(bp_sess_resume_pair() == BP_OK, "host loads its side");
    tl_use(1); BP_CHECK(bp_link_join(0) == BP_OK, "client reopens");
    if (client_resume) BP_CHECK(bp_sess_resume_pair() == BP_OK, "client loads its side");
}
static void t_resume(void)
{
    static BPState saved;
    static BPCkpt m;
    BP_CHECK(tl_connect(1, 1), "1+1 for resume");
    tl_one_move("a move");
    tl_use(0); BP_CHECK(bp_sess_save_pair() == BP_OK, "save");
    TL_UNTIL((tl_use(1), bp_sess_pair()) == BP_PAIR_DONE && (tl_use(0), bp_sess_pair()) == BP_PAIR_DONE, 300, "saved");
    tl_use(0); saved = bp_game;
    tl_one_move("a move after the save (lost on reopen, by design)");
    tl_reopen(1, 1);
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY, 300, "resumed from the pair");
    tl_use(0);
    BP_CHECK(!memcmp(&bp_game, &saved, sizeof saved) && tl_same(), "the checkpoint's state on both");
    tl_one_move("play after resume");

    /* the roll-forward: host commits, the CKCOMMIT never reaches the client */
    tl_use(0); BP_CHECK(bp_sess_save_pair() == BP_OK, "save again");
    F[0].drop_type = BPF_CKCOMMIT; F[0].drop_n = 100;
    tl_run(100);
    BP_CHECK(E[0].cur.has && E[1].cand.has && !bpl_eq(&E[0].cur.m.ckpt_id, &E[1].cur.m.ckpt_id), "host committed, client holds a candidate");
    BP_CHECK((tl_use(1), bp_sess_pair()) != BP_PAIR_DONE && (tl_use(0), bp_sess_pair()) != BP_PAIR_DONE, "neither says complete");
    saved = E[0].cur.s;
    tl_reopen(1, 1);
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY, 300, "resume rolls the client forward");
    BP_CHECK(bpl_eq(&E[1].cur.m.ckpt_id, &E[0].cur.m.ckpt_id), "the client's candidate committed");
    tl_use(1); BP_CHECK(!memcmp(&bp_game, &saved, sizeof saved) && tl_same(), "both at the host's checkpoint");

    /* the fall-back: the client lost its candidate, the host goes back */
    tl_use(0); BP_CHECK(bp_sess_save_pair() == BP_OK, "a third save");
    F[0].drop_type = BPF_CKCOMMIT; F[0].drop_n = 100;
    tl_run(100);
    E[1].cand.has = 0;                  /* (the client's disk lost it) */
    saved = E[1].cur.s;
    tl_reopen(1, 1);
    TL_UNTIL(tl_state(0) == BP_L_PLAY && tl_state(1) == BP_L_PLAY, 300, "resume falls back to the previous pair");
    tl_use(0); BP_CHECK(!memcmp(&bp_game, &saved, sizeof saved) && tl_same(), "both at the older pair");

    /* refusals: swapped roles find no file; another match's save is not a pair */
    tl_use(0); bp_sess_local();
    tl_use(1); bp_sess_local();
    tl_use(1); BP_CHECK(bp_link_host(0) == BP_OK, "the client's PC hosts");
    BP_CHECK(bp_sess_resume_pair() != BP_OK, "swapped role: no host file on the client's disk");
    m = E[1].cur.m;
    E[1].cur.m.match_id.lo ^= 1;        /* a different match on the client's disk */
    tl_reopen(1, 1);
    tl_run(200);
    BP_CHECK(tl_state(0) != BP_L_PLAY && tl_state(1) != BP_L_PLAY, "another match's save never resumes");
    E[1].cur.m = m;
}

static void t_leave_and_misc(void)
{
    static BPCmd c;
    int k;
    BP_CHECK(tl_connect(1, 1), "1+1 for leave");
    tl_use(1);
    bpl.cseq = 0xFFFF;
    for (k = 0; k < 20 && (tl_use(0), bp_game.decider) != 1; k++) tl_one_move("to the client");
    tl_use(1);
    memset(&c, 0, sizeof c); c.type = BP_C_ROLL; c.seat = bp_game.decider; c.rev = bp_game.rev;
    BP_CHECK(bp_sess_submit(&c) == BP_EOVERFLOW, "sequence exhaustion refuses, never wraps");
    tl_use(1); bp_link_leave();
    BP_CHECK(bp_mode == BP_MODE_LOCAL && bp_game.status == BP_ST_EMPTY, "leave ends the match here (D3)");
    tl_run(10);
    tl_use(0);
    BP_CHECK(bp_link_state() == BP_L_PAUSED && bpl.left, "the other PC is told and pauses");
    /* incompatible peers: both Host, different fingerprint */
    tl_reset_all();
    tl_use(0); bp_link_host(0);
    tl_use(1); bp_link_host(0);
    tl_run(30);
    BP_CHECK(tl_state(0) == BP_L_BADPEER && tl_state(1) == BP_L_BADPEER, "two hosts are refused");
}

int main(void)
{
    t_splits();
    t_forged_seat();
    t_faults();
    t_malformed();
    t_pulled();
    t_diverge();
    t_chat();
    t_ckpt();
    t_resume();
    t_leave_and_misc();
    printf("t_link: %s (%d failure%s)\n", bp_host_fail ? "FAILED" : "ok", bp_host_fail, bp_host_fail == 1 ? "" : "s");
    return bp_host_fail ? 1 : 0;
}
