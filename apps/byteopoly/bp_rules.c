/* Byteopoly - the rules engine (SPEC.md 131.4). Lane: rules. Original.
 *
 * bp_apply is the single mutation door: bp_check (pure) answers first; then
 * the command runs on bp_wk, a scratch copy, and the copy goes back over the
 * caller's record only when every step answered BP_OK. The executors below
 * therefore never undo anything: a failure deep in a chain sets bp_err and
 * the scratch is thrown away. No recursion; every loop is bounded by a
 * board constant (28 spaces, 18 props, 4 seats, 8 cards). */

static const char *bp_errs[BP_ECOUNT] = {
    "OK", TR_NOT_NOW, TR_NOT_YOUR_DECISION, TR_OUT_OF_DATE,
    TR_NOT_ENOUGH_CASH, TR_NOT_YOURS, TR_THE_RULES_REFUSE, TR_TOO_LARGE,
    TR_BAD_REQUEST, TR_OUT_OF_THE, TR_INVALID_GAME_STATE, TR_TOO_DEEP,
    TR_WAITING_FOR_THE, TR_LINK_PAUSED, TR_PLAYED_ON_THE,
    TR_DISK_ERROR, TR_BAD_FILE, TR_WRONG_VERSION, TR_DAMAGED_DATA, TR_WRONG_MATCH,
    TR_DISK_FULL_OR
};

/* phase masks: bit p = BP_PH_p */
#define BP_PM(p)  (1u << (p))
#define BP_PM_MRA (BP_PM(BP_PH_MANAGE) | BP_PM(BP_PH_REPAIR) | BP_PM(BP_PH_AFTER))
static const BPWord bp_cmd_ph[BP_C_COUNT] = {
    0,                                          /* NONE */
    BP_PM_MRA,                                  /* ROLL */
    BP_PM(BP_PH_BUY), BP_PM(BP_PH_BUY),         /* BUY DECLINE */
    BP_PM(BP_PH_AUCTION), BP_PM(BP_PH_AUCTION), /* BID PASS */
    BP_PM(BP_PH_CARD),                          /* ACK_CARD */
    BP_PM(BP_PH_REPAIR), BP_PM(BP_PH_REPAIR),   /* PAY_BAIL USE_DISK */
    BP_PM_MRA,                                  /* UPGRADE */
    BP_PM_MRA | BP_PM(BP_PH_DEBT),              /* SELL */
    BP_PM_MRA | BP_PM(BP_PH_DEBT),              /* MORTGAGE */
    BP_PM_MRA,                                  /* REDEEM */
    BP_PM_MRA,                                  /* PROPOSE */
    BP_PM(BP_PH_TRADE), BP_PM(BP_PH_TRADE),     /* ACCEPT REJECT */
    BP_PM(BP_PH_TRADE),                         /* CANCEL */
    BP_PM(BP_PH_DEBT), BP_PM(BP_PH_DEBT),       /* SETTLE BANKRUPT */
    BP_PM(BP_PH_AFTER)                          /* END_TURN */
};

static BPState bp_wk;           /* the scratch a transaction runs on */
static BPState *bp_s;           /* the record being executed (bp_wk) */
static BPResult *bp_r;
static int bp_err;              /* sticky: the first failure of a chain */
static BPU32 bp_amt, bp_tmp;    /* scratch amounts */
static BPCmd bp_cc;             /* bp_can's command */
static BPCmd bp_oc;             /* the frozen offer, as a command */
/* bp_grp's answers */
static BPByte bp_g_min, bp_g_max, bp_g_mort, bp_g_own, bp_g_up;

static BPWord bp_bit(int i) { return (BPWord)(1u << i); }

/* is prop p in mask m */
static int bp_mbit(const BPU32 *m, int p)
{
    if (p < 16) return (m->lo >> p) & 1;
    return (m->hi >> (p - 16)) & 1;
}

static BPByte bp_alive_mask(const BPState *s)
{
    int i;
    BPByte m;
    m = 0;
    for (i = 0; i < s->nseats; i++) if (s->alive[i]) m |= (BPByte)bp_bit(i);
    return m;
}

/* the collection's facts: lowest and highest level, any mortgage, the seat
 * holding every member (BP_NOBODY if none), any upgrade */
static void bp_grp(const BPState *s, int group)
{
    int p, o;
    const BPSpace *sp;
    bp_g_min = BP_MAX_LEVEL;
    bp_g_max = 0;
    bp_g_mort = 0;
    bp_g_own = 0xFE;
    for (p = 0; p < BP_PROPS; p++) {
        sp = bp_space(bp_prop_space(p));
        if (sp->kind != BP_K_COMPUTER || sp->group != group) continue;
        if (s->level[p] < bp_g_min) bp_g_min = s->level[p];
        if (s->level[p] > bp_g_max) bp_g_max = s->level[p];
        if (s->mortgaged[p]) bp_g_mort = 1;
        o = s->owner[p];
        if (bp_g_own == 0xFE) bp_g_own = (BPByte)o;
        else if (bp_g_own != o) bp_g_own = BP_NOBODY;
    }
    bp_g_up = (BPByte)(bp_g_max != 0);
}

int bp_group_owner(const BPState *s, int group)
{
    if (group < 0 || group >= BP_GROUPS) return BP_NOBODY;
    bp_grp(s, group);
    return bp_g_own;
}

int bp_mortgage_value(int space)
{
    const BPSpace *p;
    p = bp_space(space);
    if (!p || p->prop == BP_NOBODY) return 0;
    return (int)(p->price / 2);
}

void bp_redeem_cost(int space, BPU32 *out)
{
    BPWord m;
    m = (BPWord)bp_mortgage_value(space);
    bp_u32_set(out, (BPWord)(m + (m + 9) / 10), 0);
}

/* the rent the active seat would pay on `space` now (131.4.4) */
static int bp_rent_of(const BPState *s, int space, BPU32 *out)
{
    const BPSpace *sp;
    int p, o, n, i, q;
    static const BPByte mult[6] = {1, 5, 15, 30, 45, 60};
    bp_u32_set(out, 0, 0);
    sp = bp_space(space);
    if (!sp || sp->prop == BP_NOBODY) return BP_EOWNER;
    p = sp->prop;
    o = s->owner[p];
    if (o == BP_NOBODY || o == s->active || s->mortgaged[p]) return BP_EOWNER;
    if (sp->kind == BP_K_UTILITY) {
        n = 0;
        for (i = 0; i < BP_PROPS; i++) {
            q = bp_prop_space(i);
            if (bp_space(q)->kind == BP_K_UTILITY && s->owner[i] == o &&
                !s->mortgaged[i]) n++;
        }
        n = n >= 2 ? 10 : 4;
        if (!s->rolled) { bp_u32_set(out, (BPWord)n, 0); return BP_ERULE; }
        return bp_u32_mulw(out, (BPWord)n, (BPWord)(s->d1 + s->d2));
    }
    if (s->level[p] > BP_MAX_LEVEL) return BP_EINVALID;
    if (s->level[p]) return bp_u32_mulw(out, sp->rent, mult[s->level[p]]);
    bp_grp(s, sp->group);
    return bp_u32_mulw(out, sp->rent,
                       (BPWord)(bp_g_own == o && !bp_g_mort ? 2 : 1));
}

int bp_rent_quote(const BPState *s, int space, BPU32 *out)
{
    return bp_rent_of(s, space, out);
}

void bp_bid_min(const BPState *s, BPU32 *out)
{
    bp_u32_set(out, BP_BID_STEP, 0);
    if (s->auc_high != BP_NOBODY) {
        bp_u32_copy(out, &s->auc_bid);
        bp_u32_addw(out, BP_BID_STEP);  /* a bid is at most a seat's cash */
    }
}

void bp_score_of(const BPState *s, int seat, BPU32 *out)
{
    int p;
    const BPSpace *sp;
    bp_u32_set(out, 0, 0);
    if (seat < 0 || seat >= s->nseats || !s->alive[seat]) return;
    bp_u32_copy(out, &s->cash[seat]);
    for (p = 0; p < BP_PROPS; p++) {
        if (s->owner[p] != seat) continue;
        sp = bp_space(bp_prop_space(p));
        bp_u32_addw(out, s->mortgaged[p] ? (BPWord)(sp->price - sp->price / 2)
                                         : sp->price);
        if (sp->kind == BP_K_COMPUTER)
            bp_u32_addw(out, (BPWord)(s->level[p] *
                                      (bp_group_upgrade_cost(sp->group) / 2)));
    }
}

int bp_seat_assets(const BPState *s, int seat, BPU32 *mask)
{
    int p, n;
    bp_u32_set(mask, 0, 0);
    n = 0;
    for (p = 0; p < BP_PROPS; p++) {
        if (s->owner[p] != seat) continue;
        n++;
        if (p < 16) mask->lo |= bp_bit(p);
        else mask->hi |= bp_bit(p - 16);
    }
    return n;
}

void bp_setup_default(BPSetup *su)
{
    int i;
    os88_memset(su, 0, sizeof(BPSetup));
    su->nseats = 2;
    for (i = 0; i < BP_SEATS; i++) {
        os88_strcpy(su->name[i], "Player 1", BP_NAMELEN + 1);
        su->name[i][7] = (char)('1' + i);
        su->token[i] = (BPByte)i;
    }
}

int bp_setup_check(const BPSetup *su)
{
    int i, j, n;
    unsigned char ch;
    if (su->nseats < 2 || su->nseats > BP_SEATS) return BP_EARG;
    if (su->rules == BP_RULES_CLASSIC) {
        if (su->rounds) return BP_EARG;
    } else if (su->rules == BP_RULES_QUICK) {
        if (su->rounds != 10 && su->rounds != 20 && su->rounds != 30) return BP_EARG;
    } else return BP_EARG;
    for (i = 0; i < su->nseats; i++) {
        if (su->token[i] > BP_T_CASSETTE || su->machine[i] > 1) return BP_EARG;
        for (j = 0; j < i; j++) if (su->token[j] == su->token[i]) return BP_EARG;
        n = 0;
        while (n <= BP_NAMELEN && su->name[i][n]) {
            ch = (unsigned char)su->name[i][n];
            if (ch < 32 || ch > 126) return BP_EARG;
            n++;
        }
        if (n == 0 || n > BP_NAMELEN) return BP_EARG;
    }
    return BP_OK;
}

const char *bp_err_text(int code)
{
    if (code < 0 || code >= BP_ECOUNT) return "?";
    return bp_errs[code];
}

/* ===== the checks (pure) ================================================ */

/* steps 1-7 of SPEC.md 131.4's refusal order */
static int bp_check_common(const BPState *s, const BPCmd *c)
{
    int actor;
    if (c->type == BP_C_NONE || c->type >= BP_C_COUNT) return BP_EARG;
    if (s->status != BP_ST_PLAY) return BP_EOUT;
    if (c->seat >= s->nseats) return BP_ESEAT;
    if (!s->alive[c->seat]) return BP_EOUT;
    if (c->rev.lo != s->rev.lo || c->rev.hi != s->rev.hi) return BP_ESTALE;
    if (s->phase > BP_PH_OVER || !(bp_cmd_ph[c->type] & BP_PM(s->phase)))
        return BP_EPHASE;
    actor = c->type == BP_C_CANCEL ? s->offer_from : s->decider;
    if (c->seat != actor) return BP_ESEAT;
    return BP_OK;
}

/* an asset argument: the prop index of a purchasable space, or -1 */
static int bp_arg_prop(int space)
{
    const BPSpace *sp;
    sp = bp_space(space);
    if (!sp || sp->prop == BP_NOBODY) return -1;
    return sp->prop;
}

static int bp_check_manage(const BPState *s, const BPCmd *c)
{
    int p, seat;
    const BPSpace *sp;
    BPWord cost;
    p = bp_arg_prop(c->arg);
    if (p < 0) return BP_EARG;
    seat = c->seat;
    sp = bp_space(c->arg);
    if (s->owner[p] != seat) return BP_EOWNER;
    if (c->type == BP_C_MORTGAGE) {
        if (s->mortgaged[p]) return BP_ERULE;
        if (sp->kind == BP_K_COMPUTER) {
            bp_grp(s, sp->group);
            if (bp_g_up) return BP_ERULE;
        }
        return BP_OK;
    }
    if (c->type == BP_C_REDEEM) {
        if (!s->mortgaged[p]) return BP_ERULE;
        bp_redeem_cost(c->arg, &bp_tmp);
        return bp_u32_cmp(&s->cash[seat], &bp_tmp) < 0 ? BP_EFUNDS : BP_OK;
    }
    if (sp->kind != BP_K_COMPUTER) return BP_ERULE;
    bp_grp(s, sp->group);
    if (c->type == BP_C_SELL) {
        if (!s->level[p] || s->level[p] != bp_g_max) return BP_ERULE;
        return BP_OK;
    }
    /* UPGRADE */
    if (bp_g_own != seat || bp_g_mort) return BP_ERULE;
    if (s->level[p] >= BP_MAX_LEVEL || s->level[p] != bp_g_min) return BP_ERULE;
    cost = bp_group_upgrade_cost(sp->group);
    return bp_u32_cmpw(&s->cash[seat], cost) < 0 ? BP_EFUNDS : BP_OK;
}

/* the terms of a proposal, against the state it would apply to */
static int bp_check_offer(const BPState *s, const BPCmd *c)
{
    int p, from, to, d, any;
    const BPSpace *sp;
    from = c->seat;
    to = c->to;
    if (to >= s->nseats || to == from || !s->alive[to]) return BP_EARG;
    if ((c->give.hi | c->get.hi) & 0xFFFCu) return BP_EARG;
    if (c->disks & 0xCC) return BP_EARG;
    if (c->payer > 1) return BP_EARG;
    if (bp_u32_iszero(&c->give) && bp_u32_iszero(&c->get) && !c->disks &&
        bp_u32_iszero(&c->amount)) return BP_EARG;
    for (p = 0; p < BP_PROPS; p++) {
        any = 0;
        if (bp_mbit(&c->give, p)) {
            if (s->owner[p] != from) return BP_EOWNER;
            any = 1;
        }
        if (bp_mbit(&c->get, p)) {
            if (s->owner[p] != to) return BP_EOWNER;
            any = 1;
        }
        if (!any) continue;
        sp = bp_space(bp_prop_space(p));
        if (sp->kind == BP_K_COMPUTER) {
            bp_grp(s, sp->group);
            if (bp_g_up) return BP_ERULE;
        }
    }
    for (d = 0; d < BP_DECKS; d++) {
        if ((c->disks & bp_bit(d)) && !(s->disks[from] & bp_bit(d))) return BP_EOWNER;
        if ((c->disks & bp_bit(4 + d)) && !(s->disks[to] & bp_bit(d))) return BP_EOWNER;
    }
    if (bp_u32_cmp(&s->cash[c->payer ? to : from], &c->amount) < 0) return BP_EFUNDS;
    return BP_OK;
}

/* the frozen offer re-checked as the PROPOSE it was */
static int bp_offer_recheck(const BPState *s)
{
    os88_memset(&bp_oc, 0, sizeof(BPCmd));
    bp_oc.seat = s->offer_from;
    bp_oc.to = s->offer_to;
    bp_u32_copy(&bp_oc.give, &s->offer_give);
    bp_u32_copy(&bp_oc.get, &s->offer_get);
    bp_u32_copy(&bp_oc.amount, &s->offer_cash);
    bp_oc.disks = s->offer_disks;
    bp_oc.payer = s->offer_payer;
    if (bp_oc.seat >= s->nseats) return BP_EINVALID;
    return bp_check_offer(s, &bp_oc);
}

static int bp_bid_ok(const BPState *s, const BPCmd *c)
{
    BPWord m;
    bp_bid_min(s, &bp_tmp);
    if (bp_u32_cmp(&c->amount, &bp_tmp) < 0) return BP_ERULE;
    /* 65,536 = 6 (mod 10) */
    m = (BPWord)(((c->amount.hi % 10) * 6 + c->amount.lo % 10) % 10);
    if (m) return BP_ERULE;
    if (bp_u32_cmp(&c->amount, &s->cash[c->seat]) > 0) return BP_EFUNDS;
    return BP_OK;
}

static int bp_check_cmd(const BPState *s, const BPCmd *c)
{
    int seat;
    const BPSpace *sp;
    seat = c->seat;
    switch (c->type) {
    case BP_C_ROLL:
        if (s->phase == BP_PH_AFTER && (!s->doubles || s->detained[seat]))
            return BP_ERULE;
        return BP_OK;
    case BP_C_END_TURN:
        if (s->doubles && !s->detained[seat]) return BP_ERULE;
        return BP_OK;
    case BP_C_BUY:
        sp = bp_space(s->pos[seat]);
        return bp_u32_cmpw(&s->cash[seat], sp->price) < 0 ? BP_EFUNDS : BP_OK;
    case BP_C_BID:
        return bp_bid_ok(s, c);
    case BP_C_PAY_BAIL:
        return bp_u32_cmpw(&s->cash[seat], BP_BAIL) < 0 ? BP_EFUNDS : BP_OK;
    case BP_C_USE_DISK:
        if (c->arg >= BP_DECKS) return BP_EARG;
        return (s->disks[seat] & bp_bit(c->arg)) ? BP_OK : BP_EOWNER;
    case BP_C_UPGRADE: case BP_C_SELL: case BP_C_MORTGAGE: case BP_C_REDEEM:
        return bp_check_manage(s, c);
    case BP_C_PROPOSE:
        return bp_check_offer(s, c);
    case BP_C_ACCEPT:
        if (c->arg != (s->offer_id & 0xFF)) return BP_EARG;
        return bp_offer_recheck(s);
    case BP_C_SETTLE:
        return bp_u32_cmp(&s->cash[seat], &s->debt_amount) < 0 ? BP_EFUNDS : BP_OK;
    case BP_C_ACK_CARD:
        return s->card < BP_DECKS * BP_DECK_CARDS ? BP_OK : BP_EINVALID;
    }
    return BP_OK;   /* DECLINE PASS REJECT CANCEL BANKRUPT */
}

int bp_check(const BPState *s, const BPCmd *c)
{
    int e;
    e = bp_check_common(s, c);
    if (e) return e;
    return bp_check_cmd(s, c);
}

int bp_can(const BPState *s, int seat, int type, int arg)
{
    int e;
    os88_memset(&bp_cc, 0, sizeof(BPCmd));
    bp_cc.type = (BPByte)type;
    bp_cc.seat = (BPByte)seat;
    bp_cc.arg = (BPByte)arg;
    bp_u32_copy(&bp_cc.rev, &s->rev);
    if (type < 0 || type >= BP_C_COUNT) return BP_EARG;
    e = bp_check_common(s, &bp_cc);
    if (e || type == BP_C_PROPOSE) return e;
    if (type == BP_C_BID) bp_bid_min(s, &bp_cc.amount);
    return bp_check_cmd(s, &bp_cc);
}

/* ===== the executors (on bp_s = &bp_wk) ================================= */

static void bp_ev(int kind, int seat, int other, int space, const BPU32 *a)
{
    BPEvent *e;
    if (bp_r->n >= BP_EV_MAX) { if (!bp_err) bp_err = BP_EDEPTH; return; }
    e = &bp_r->ev[bp_r->n++];
    e->kind = (BPByte)kind;
    e->seat = (BPByte)seat;
    e->other = (BPByte)other;
    e->space = (BPByte)space;
    if (a) bp_u32_copy(&e->amount, a);
    else bp_u32_set(&e->amount, 0, 0);
}

static void bp_evw(int kind, int seat, int other, int space, BPWord w)
{
    bp_u32_set(&bp_amt, w, 0);
    bp_ev(kind, seat, other, space, &bp_amt);
}

static void bp_fail(int e) { if (e && !bp_err) bp_err = e; }

static void bp_gain(int seat, const BPU32 *a) { bp_fail(bp_u32_add(&bp_s->cash[seat], a)); }
static void bp_gainw(int seat, BPWord w) { bp_fail(bp_u32_addw(&bp_s->cash[seat], w)); }
static void bp_pay(int seat, const BPU32 *a) { bp_fail(bp_u32_sub(&bp_s->cash[seat], a)); }
static void bp_payw(int seat, BPWord w) { bp_fail(bp_u32_subw(&bp_s->cash[seat], w)); }

static void bp_after(void)
{
    bp_s->phase = BP_PH_AFTER;
    bp_s->decider = bp_s->active;
}

static int bp_roll1(void) { return 1 + (int)bp_rand_next(&bp_s->rng, 6); }

/* the card cycle of deck d (SPEC.md 131.4.4) */
static void bp_deck_take(int d)         /* remove the top card (a disk) */
{
    int i, n;
    n = bp_s->deck_n[d];
    for (i = bp_s->deck_top[d]; i + 1 < n; i++) bp_s->deck[d][i] = bp_s->deck[d][i + 1];
    bp_s->deck[d][n - 1] = 0;
    bp_s->deck_n[d] = (BPByte)(n - 1);
    if (bp_s->deck_top[d] >= n - 1) bp_s->deck_top[d] = 0;
}

static int bp_disk_id(int d) { return d ? 15 : 6; }

static void bp_deck_return(int d)       /* a used disk to the bottom */
{
    int i, n, t;
    n = bp_s->deck_n[d];
    t = bp_s->deck_top[d];
    if (n >= BP_DECK_CARDS) { bp_fail(BP_EINVALID); return; }
    for (i = n; i > t; i--) bp_s->deck[d][i] = bp_s->deck[d][i - 1];
    bp_s->deck[d][t] = (BPByte)bp_disk_id(d);
    bp_s->deck_n[d] = (BPByte)(n + 1);
    bp_s->deck_top[d] = (BPByte)(n ? t + 1 : 0);
}

static void bp_detain(int seat)
{
    bp_s->pos[seat] = BP_REPAIR_SPACE;
    bp_s->detained[seat] = 1;
    bp_s->tries[seat] = 0;
    bp_s->doubles = 0;
    bp_s->pending_move = 0;
    bp_ev(BP_V_DETAIN, seat, 0, BP_REPAIR_SPACE, 0);
    bp_after();
}

/* the active seat owes `a` to cred (a seat, or BP_NOBODY for the bank):
 * paid whole now (1), or DEBT opened and nothing paid (0) */
static int bp_owe(int cred, const BPU32 *a, int reason, int space)
{
    int seat;
    seat = bp_s->active;
    if (bp_u32_cmp(&bp_s->cash[seat], a) >= 0) {
        bp_pay(seat, a);
        if (cred != BP_NOBODY) bp_gain(cred, a);
        bp_ev(cred == BP_NOBODY ? BP_V_CHARGE : BP_V_RENT, seat, cred, space, a);
        return 1;
    }
    bp_s->debt_to = (BPByte)cred;
    bp_u32_copy(&bp_s->debt_amount, a);
    bp_s->debt_reason = (BPByte)reason;
    bp_s->ret_phase = BP_PH_AFTER;
    bp_s->phase = BP_PH_DEBT;
    bp_s->decider = (BPByte)seat;
    bp_ev(BP_V_DEBT, seat, cred, space, a);
    return 0;
}

static void bp_draw(int d)
{
    int seat;
    seat = bp_s->active;
    bp_s->depth++;
    if (bp_s->depth > BP_DEPTH_MAX) { bp_fail(BP_EDEPTH); return; }
    bp_s->card = bp_s->deck[d][bp_s->deck_top[d]];
    bp_r->card = bp_s->card;
    bp_ev(BP_V_CARD, seat, bp_s->card, bp_s->pos[seat], 0);
    bp_s->phase = BP_PH_CARD;
    bp_s->decider = (BPByte)seat;
}

/* the landing dispatcher (SPEC.md 131.4.4): sets the phase, never recurses */
static void bp_land(void)
{
    int seat, pos, p, o;
    const BPSpace *sp;
    seat = bp_s->active;
    pos = bp_s->pos[seat];
    sp = bp_space(pos);
    bp_after();
    switch (sp->kind) {
    case BP_K_COMPUTER: case BP_K_UTILITY:
        p = sp->prop;
        o = bp_s->owner[p];
        if (o == BP_NOBODY) { bp_s->phase = BP_PH_BUY; return; }
        if (o == seat || bp_s->mortgaged[p]) return;
        if (bp_rent_of(bp_s, pos, &bp_tmp)) { bp_fail(BP_EINVALID); return; }
        bp_owe(o, &bp_tmp, BP_DR_RENT, pos);
        return;
    case BP_K_EVENT:
        bp_draw(sp->group);
        return;
    case BP_K_CHARGE:
        bp_u32_set(&bp_tmp, sp->price, 0);
        bp_owe(BP_NOBODY, &bp_tmp, BP_DR_CHARGE, pos);
        return;
    case BP_K_CRASH:
        bp_detain(seat);
        return;
    }
}

static void bp_move(int n, int fwd)
{
    int seat, p;
    seat = bp_s->active;
    p = bp_s->pos[seat];
    if (fwd) {
        p += n;
        if (p >= BP_SPACES) {
            p -= BP_SPACES;
            bp_gainw(seat, BP_SALARY);
            bp_evw(BP_V_SALARY, seat, 0, 0, BP_SALARY);
        }
    } else {
        p -= n;
        if (p < 0) p += BP_SPACES;
    }
    bp_s->pos[seat] = (BPByte)p;
    bp_ev(BP_V_MOVE, seat, 0, p, 0);
    bp_land();
}

static void bp_release(int how)
{
    int seat;
    seat = bp_s->active;
    bp_s->detained[seat] = 0;
    bp_s->tries[seat] = 0;
    bp_ev(BP_V_RELEASE, seat, how, BP_REPAIR_SPACE, 0);
}

static void bp_x_roll(void)
{
    int seat, d1, d2, n;
    seat = bp_s->active;
    d1 = bp_roll1();
    d2 = bp_roll1();
    bp_s->d1 = (BPByte)d1;
    bp_s->d2 = (BPByte)d2;
    bp_s->rolled = 1;
    bp_r->d1 = (BPByte)d1;
    bp_r->d2 = (BPByte)d2;
    bp_ev(BP_V_ROLL, seat, d1, d2, 0);
    n = d1 + d2;
    bp_s->depth = 0;
    if (bp_s->phase == BP_PH_REPAIR) {
        bp_s->doubles = 0;
        if (d1 == d2) { bp_release(2); bp_move(n, 1); return; }
        bp_s->tries[seat]++;
        if (bp_s->tries[seat] < 3) { bp_after(); return; }
        bp_s->pending_move = (BPByte)n;
        bp_u32_set(&bp_tmp, BP_BAIL, 0);
        if (!bp_owe(BP_NOBODY, &bp_tmp, BP_DR_BAIL, BP_REPAIR_SPACE)) return;
        bp_s->pending_move = 0;
        bp_release(3);
        bp_move(n, 1);
        return;
    }
    if (d1 == d2) {
        bp_s->doubles++;
        if (bp_s->doubles >= 3) { bp_detain(seat); return; }
        bp_ev(BP_V_DOUBLES, seat, 0, 0, 0);
    } else bp_s->doubles = 0;
    bp_move(n, 1);
}

static void bp_begin_turn(int seat)
{
    bp_s->active = (BPByte)seat;
    bp_s->decider = (BPByte)seat;
    bp_s->turn = (BPWord)(bp_s->turn + 1);
    bp_s->doubles = 0;
    bp_s->rolled = 0;
    bp_s->d1 = 0;
    bp_s->d2 = 0;
    bp_s->depth = 0;
    bp_s->phase = bp_s->detained[seat] ? BP_PH_REPAIR : BP_PH_MANAGE;
    bp_ev(BP_V_TURN, seat, 0, bp_s->pos[seat], 0);
}

/* the match is over: scores, winners (SPEC.md 131.4.6) */
static void bp_finish(void)
{
    int i, alive;
    BPByte w;
    alive = 0;
    w = 0;
    for (i = 0; i < bp_s->nseats; i++) {
        bp_score_of(bp_s, i, &bp_s->score[i]);
        if (bp_s->alive[i]) { alive++; w = (BPByte)bp_bit(i); }
    }
    if (alive > 1) {
        bp_u32_set(&bp_tmp, 0, 0);
        w = 0;
        for (i = 0; i < bp_s->nseats; i++) {
            if (!bp_s->alive[i]) continue;
            if (bp_u32_cmp(&bp_s->score[i], &bp_tmp) > 0) {
                bp_u32_copy(&bp_tmp, &bp_s->score[i]);
                w = 0;
            }
            if (!bp_u32_cmp(&bp_s->score[i], &bp_tmp)) w |= (BPByte)bp_bit(i);
        }
    }
    bp_s->winners = w;
    bp_s->status = BP_ST_OVER;
    bp_s->phase = BP_PH_OVER;
    bp_s->decider = BP_NOBODY;
    bp_ev(BP_V_OVER, 0, w, 0, 0);
}

/* the active seat's turn is complete (or the seat was eliminated) */
static void bp_next_turn(void)
{
    int a, i, n;
    BPByte live;
    a = bp_s->active;
    live = bp_alive_mask(bp_s);
    bp_s->round_left = (BPByte)(bp_s->round_left & ~bp_bit(a) & live);
    if (!bp_s->round_left) {
        bp_s->round = (BPWord)(bp_s->round + 1);
        bp_evw(BP_V_ROUND, a, 0, 0, bp_s->round);
        if (bp_s->rules == BP_RULES_QUICK && bp_s->round >= bp_s->rounds_limit) {
            bp_finish();
            return;
        }
        bp_s->round_left = live;
    }
    n = a;
    for (i = 0; i < BP_SEATS; i++) {
        n = (n + 1) % bp_s->nseats;
        if (bp_s->alive[n]) break;
    }
    bp_begin_turn(n);
}

/* the auction's next bidder, or its end (SPEC.md 131.4.5) */
static void bp_auc_seek(int from, int incl)
{
    int k, seat, p;
    BPWord b;
    bp_bid_min(bp_s, &bp_amt);
    for (k = 0; k < bp_s->nseats; k++) {
        seat = (from + k + (incl ? 0 : 1)) % bp_s->nseats;
        b = bp_bit(seat);
        if (!(bp_s->auc_open & b) || (bp_s->auc_passed & b) || seat == bp_s->auc_high)
            continue;
        if (bp_u32_cmp(&bp_s->cash[seat], &bp_amt) < 0) {
            bp_s->auc_passed |= (BPByte)b;
            bp_ev(BP_V_PASS, seat, 1, bp_s->auc_space, 0);
            continue;
        }
        bp_s->auc_turn = (BPByte)seat;
        bp_s->decider = (BPByte)seat;
        return;
    }
    seat = bp_s->auc_high;
    p = bp_space(bp_s->auc_space)->prop;
    if (seat != BP_NOBODY) {
        bp_pay(seat, &bp_s->auc_bid);
        bp_s->owner[p] = (BPByte)seat;
        bp_ev(BP_V_WON, seat, 0, bp_s->auc_space, &bp_s->auc_bid);
    } else bp_ev(BP_V_UNSOLD, BP_NOBODY, 0, bp_s->auc_space, 0);
    bp_s->auc_space = BP_NOBODY;
    bp_s->auc_turn = BP_NOBODY;
    bp_s->auc_high = BP_NOBODY;
    bp_s->auc_open = 0;
    bp_s->auc_passed = 0;
    bp_u32_set(&bp_s->auc_bid, 0, 0);
    bp_after();
}

static void bp_offer_clear(void)
{
    bp_s->offer_from = BP_NOBODY;
    bp_s->offer_to = BP_NOBODY;
    bp_s->offer_disks = 0;
    bp_s->offer_payer = 0;
    bp_u32_set(&bp_s->offer_give, 0, 0);
    bp_u32_set(&bp_s->offer_get, 0, 0);
    bp_u32_set(&bp_s->offer_cash, 0, 0);
    bp_s->phase = bp_s->ret_phase;
    bp_s->ret_phase = BP_PH_NONE;
    bp_s->decider = bp_s->active;
}

static void bp_x_trade(void)
{
    int p, from, to, d;
    from = bp_s->offer_from;
    to = bp_s->offer_to;
    for (p = 0; p < BP_PROPS; p++) {
        if (bp_mbit(&bp_s->offer_give, p)) bp_s->owner[p] = (BPByte)to;
        else if (bp_mbit(&bp_s->offer_get, p)) bp_s->owner[p] = (BPByte)from;
    }
    for (d = 0; d < BP_DECKS; d++) {
        if (bp_s->offer_disks & bp_bit(d)) {
            bp_s->disks[from] &= (BPByte)~bp_bit(d);
            bp_s->disks[to] |= (BPByte)bp_bit(d);
        }
        if (bp_s->offer_disks & bp_bit(4 + d)) {
            bp_s->disks[to] &= (BPByte)~bp_bit(d);
            bp_s->disks[from] |= (BPByte)bp_bit(d);
        }
    }
    if (bp_s->offer_payer) { d = from; from = to; to = d; }
    bp_pay(from, &bp_s->offer_cash);
    bp_gain(to, &bp_s->offer_cash);
    bp_ev(BP_V_TRADED, bp_s->offer_from, bp_s->offer_to, 0, &bp_s->offer_cash);
}

static void bp_debt_clear(void)
{
    bp_s->debt_to = BP_NOBODY;
    bp_s->debt_reason = BP_DR_NONE;
    bp_u32_set(&bp_s->debt_amount, 0, 0);
}

static void bp_x_settle(void)
{
    int seat, cred, n;
    seat = bp_s->active;
    cred = bp_s->debt_to;
    bp_pay(seat, &bp_s->debt_amount);
    if (cred != BP_NOBODY) bp_gain(cred, &bp_s->debt_amount);
    bp_ev(BP_V_SETTLED, seat, cred, bp_s->pos[seat], &bp_s->debt_amount);
    bp_ev(cred == BP_NOBODY ? BP_V_CHARGE : BP_V_RENT, seat, cred,
          bp_s->pos[seat], &bp_s->debt_amount);
    bp_debt_clear();
    bp_s->phase = bp_s->ret_phase;
    bp_s->decider = (BPByte)seat;
    bp_s->ret_phase = BP_PH_NONE;
    n = bp_s->pending_move;
    if (n) {
        bp_s->pending_move = 0;
        bp_release(3);
        bp_s->depth = 0;
        bp_move(n, 1);
    }
}

static void bp_x_bankrupt(void)
{
    int seat, cred, p, d, alive;
    const BPSpace *sp;
    seat = bp_s->active;
    cred = bp_s->debt_to;
    for (p = 0; p < BP_PROPS; p++) {
        if (bp_s->owner[p] != seat || !bp_s->level[p]) continue;
        sp = bp_space(bp_prop_space(p));
        bp_u32_mulw(&bp_tmp, bp_s->level[p], (BPWord)(bp_group_upgrade_cost(sp->group) / 2));
        bp_gain(seat, &bp_tmp);
        bp_s->level[p] = 0;
        bp_ev(BP_V_SELL, seat, 0, bp_prop_space(p), &bp_tmp);
    }
    if (cred != BP_NOBODY) bp_gain(cred, &bp_s->cash[seat]);
    bp_ev(BP_V_ESTATE, seat, cred, 0, &bp_s->cash[seat]);
    bp_u32_set(&bp_s->cash[seat], 0, 0);
    for (p = 0; p < BP_PROPS; p++) {
        if (bp_s->owner[p] != seat) continue;
        bp_s->owner[p] = (BPByte)cred;
        if (cred == BP_NOBODY) bp_s->mortgaged[p] = 0;
    }
    if (cred != BP_NOBODY) bp_s->disks[cred] |= bp_s->disks[seat];
    else for (d = 0; d < BP_DECKS; d++)
        if (bp_s->disks[seat] & bp_bit(d)) bp_deck_return(d);
    bp_s->disks[seat] = 0;
    bp_s->alive[seat] = 0;
    bp_s->round_left &= (BPByte)~bp_bit(seat);
    bp_s->detained[seat] = 0;
    bp_s->tries[seat] = 0;
    bp_s->pending_move = 0;
    bp_s->doubles = 0;
    bp_debt_clear();
    bp_s->ret_phase = BP_PH_NONE;
    bp_ev(BP_V_BANKRUPT, seat, cred, 0, 0);
    alive = 0;
    for (d = 0; d < bp_s->nseats; d++) alive += bp_s->alive[d];
    if (alive <= 1) bp_finish();
    else bp_next_turn();
}

static void bp_x_card(void)
{
    int seat, id, d, n, pos;
    const BPCard *cd;
    seat = bp_s->active;
    id = bp_s->card;
    d = id >> 3;
    cd = bp_card(id);
    bp_s->card = BP_NOBODY;
    if (cd->effect == BP_E_KEEP) {
        bp_deck_take(d);
        bp_s->disks[seat] |= (BPByte)bp_bit(d);
        bp_ev(BP_V_DISK, seat, d, bp_s->pos[seat], 0);
        bp_after();
        return;
    }
    bp_s->deck_top[d] = (BPByte)((bp_s->deck_top[d] + 1) % bp_s->deck_n[d]);
    pos = bp_s->pos[seat];
    switch (cd->effect) {
    case BP_E_GAIN:
        bp_gainw(seat, cd->amount);
        bp_evw(BP_V_GAIN, seat, id, pos, cd->amount);
        bp_after();
        return;
    case BP_E_PAY:
        bp_after();
        bp_u32_set(&bp_tmp, cd->amount, 0);
        bp_owe(BP_NOBODY, &bp_tmp, BP_DR_CARD, pos);
        return;
    case BP_E_REPAIRS:
        n = 0;
        for (d = 0; d < BP_PROPS; d++) {
            if (bp_s->owner[d] != seat) continue;
            if (bp_s->level[d] == BP_MAX_LEVEL) n += 100;
            else n += 25 * bp_s->level[d];
        }
        bp_after();
        bp_u32_set(&bp_tmp, (BPWord)n, 0);
        bp_owe(BP_NOBODY, &bp_tmp, BP_DR_CARD, pos);
        return;
    case BP_E_TO:
        n = (cd->arg + BP_SPACES - pos) % BP_SPACES;
        bp_move(n ? n : BP_SPACES, 1);
        return;
    case BP_E_FORWARD:
        bp_move(cd->arg, 1);
        return;
    case BP_E_BACK:
        bp_move(cd->arg, 0);
        return;
    case BP_E_DETAIN:
        bp_detain(seat);
        return;
    }
    bp_fail(BP_EINVALID);
}

static void bp_x_manage(const BPCmd *c)
{
    int seat, p, k;
    const BPSpace *sp;
    BPWord v;
    seat = c->seat;
    sp = bp_space(c->arg);
    p = sp->prop;
    v = bp_group_upgrade_cost(sp->group);
    switch (c->type) {
    case BP_C_UPGRADE:
        bp_payw(seat, v);
        bp_s->level[p]++;
        k = BP_V_UPGRADE;
        break;
    case BP_C_SELL:
        v = (BPWord)(v / 2);
        bp_gainw(seat, v);
        bp_s->level[p]--;
        k = BP_V_SELL;
        break;
    case BP_C_MORTGAGE:
        v = (BPWord)bp_mortgage_value(c->arg);
        bp_gainw(seat, v);
        bp_s->mortgaged[p] = 1;
        k = BP_V_MORTGAGE;
        break;
    default: /* REDEEM */
        bp_redeem_cost(c->arg, &bp_tmp);
        v = bp_tmp.lo;
        bp_payw(seat, v);
        bp_s->mortgaged[p] = 0;
        k = BP_V_REDEEM;
        break;
    }
    bp_evw(k, seat, bp_s->level[p], c->arg, v);
}

static void bp_exec(const BPCmd *c)
{
    int seat;
    const BPSpace *sp;
    seat = c->seat;
    switch (c->type) {
    case BP_C_ROLL:
        bp_x_roll();
        return;
    case BP_C_BUY:
        sp = bp_space(bp_s->pos[seat]);
        bp_payw(seat, sp->price);
        bp_s->owner[sp->prop] = (BPByte)seat;
        bp_evw(BP_V_BUY, seat, 0, bp_s->pos[seat], sp->price);
        bp_after();
        return;
    case BP_C_DECLINE:
        bp_s->auc_space = bp_s->pos[seat];
        bp_s->auc_open = bp_alive_mask(bp_s);
        bp_s->auc_passed = 0;
        bp_s->auc_high = BP_NOBODY;
        bp_u32_set(&bp_s->auc_bid, 0, 0);
        bp_s->phase = BP_PH_AUCTION;
        bp_ev(BP_V_AUCTION, seat, 0, bp_s->auc_space, 0);
        bp_auc_seek(seat, 1);
        return;
    case BP_C_BID:
        bp_u32_copy(&bp_s->auc_bid, &c->amount);
        bp_s->auc_high = (BPByte)seat;
        bp_ev(BP_V_BID, seat, 0, bp_s->auc_space, &c->amount);
        bp_auc_seek(seat, 0);
        return;
    case BP_C_PASS:
        bp_s->auc_passed |= (BPByte)bp_bit(seat);
        bp_ev(BP_V_PASS, seat, 0, bp_s->auc_space, 0);
        bp_auc_seek(seat, 0);
        return;
    case BP_C_ACK_CARD:
        bp_x_card();
        return;
    case BP_C_PAY_BAIL:
        bp_payw(seat, BP_BAIL);
        bp_evw(BP_V_CHARGE, seat, BP_NOBODY, BP_REPAIR_SPACE, BP_BAIL);
        bp_release(0);
        bp_s->phase = BP_PH_MANAGE;
        return;
    case BP_C_USE_DISK:
        bp_s->disks[seat] &= (BPByte)~bp_bit(c->arg);
        bp_deck_return(c->arg);
        bp_release(1);
        bp_s->phase = BP_PH_MANAGE;
        return;
    case BP_C_UPGRADE: case BP_C_SELL: case BP_C_MORTGAGE: case BP_C_REDEEM:
        bp_x_manage(c);
        return;
    case BP_C_PROPOSE:
        bp_s->offer_id = (BPWord)(bp_s->offer_id + 1);
        if (!bp_s->offer_id) bp_s->offer_id = 1;
        bp_s->offer_from = (BPByte)seat;
        bp_s->offer_to = c->to;
        bp_s->offer_disks = c->disks;
        bp_s->offer_payer = c->payer;
        bp_u32_copy(&bp_s->offer_give, &c->give);
        bp_u32_copy(&bp_s->offer_get, &c->get);
        bp_u32_copy(&bp_s->offer_cash, &c->amount);
        bp_s->ret_phase = bp_s->phase;
        bp_s->phase = BP_PH_TRADE;
        bp_s->decider = c->to;
        bp_ev(BP_V_OFFER, seat, c->to, 0, &c->amount);
        return;
    case BP_C_ACCEPT:
        bp_x_trade();
        bp_offer_clear();
        return;
    case BP_C_REJECT:
        bp_ev(BP_V_REJECTED, bp_s->offer_to, bp_s->offer_from, 0, 0);
        bp_offer_clear();
        return;
    case BP_C_CANCEL:
        bp_ev(BP_V_CANCELLED, bp_s->offer_from, bp_s->offer_to, 0, 0);
        bp_offer_clear();
        return;
    case BP_C_SETTLE:
        bp_x_settle();
        return;
    case BP_C_BANKRUPT:
        bp_x_bankrupt();
        return;
    case BP_C_END_TURN:
        bp_s->doubles = 0;
        bp_next_turn();
        return;
    }
    bp_fail(BP_EARG);
}

int bp_apply(BPState *s, const BPCmd *c, BPResult *r)
{
    int e;
    r->n = 0;
    r->d1 = 0;
    r->d2 = 0;
    r->card = BP_NOBODY;
    e = bp_check(s, c);
    if (e) return e;
    os88_memcpy(&bp_wk, s, sizeof(BPState));
    bp_s = &bp_wk;
    bp_r = r;
    bp_err = BP_OK;
    bp_exec(c);
    if (!bp_err) bp_fail(bp_u32_addw(&bp_wk.rev, 1));
    if (bp_err) { r->n = 0; return bp_err; }
    os88_memcpy(s, &bp_wk, sizeof(BPState));
    return BP_OK;
}

/* ===== a new match (SPEC.md 131.4.7, 131.4.8) =========================== */

int bp_new_match(BPState *s, const BPSetup *su, const BPU32 *seed, BPResult *r)
{
    int e, i, j, d, t, best, k;
    BPByte cand, tie;
    r->n = 0;
    r->d1 = 0;
    r->d2 = 0;
    r->card = BP_NOBODY;
    e = bp_setup_check(su);
    if (e) return e;
    bp_s = &bp_wk;
    bp_r = r;
    bp_err = BP_OK;
    os88_memset(&bp_wk, 0, sizeof(BPState));
    bp_wk.status = BP_ST_PLAY;
    bp_wk.rules = su->rules;
    bp_wk.rounds_limit = su->rounds;
    bp_wk.nseats = su->nseats;
    bp_u32_set(&bp_wk.rev, 1, 0);
    for (i = 0; i < su->nseats; i++) {
        os88_memcpy(bp_wk.name[i], su->name[i], BP_NAMELEN + 1);
        bp_wk.name[i][BP_NAMELEN] = 0;
        bp_wk.token[i] = su->token[i];
        bp_wk.machine[i] = su->machine[i];
        bp_wk.alive[i] = 1;
        bp_u32_set(&bp_wk.cash[i], BP_START_CASH, 0);
    }
    for (i = 0; i < BP_PROPS; i++) bp_wk.owner[i] = BP_NOBODY;
    bp_u32_copy(&bp_wk.rng, seed);
    bp_wk.match_id.lo = bp_rand_next(&bp_wk.rng, 0);
    bp_wk.match_id.hi = bp_rand_next(&bp_wk.rng, 0);
    for (d = 0; d < BP_DECKS; d++) {
        for (i = 0; i < BP_DECK_CARDS; i++) bp_wk.deck[d][i] = (BPByte)(d * 8 + i);
        for (i = BP_DECK_CARDS - 1; i > 0; i--) {
            j = (int)bp_rand_next(&bp_wk.rng, (BPWord)(i + 1));
            t = bp_wk.deck[d][i];
            bp_wk.deck[d][i] = bp_wk.deck[d][j];
            bp_wk.deck[d][j] = (BPByte)t;
        }
        bp_wk.deck_n[d] = BP_DECK_CARDS;
    }
    bp_wk.card = BP_NOBODY;
    bp_wk.auc_space = BP_NOBODY;
    bp_wk.auc_turn = BP_NOBODY;
    bp_wk.auc_high = BP_NOBODY;
    bp_wk.debt_to = BP_NOBODY;
    bp_wk.offer_from = BP_NOBODY;
    bp_wk.offer_to = BP_NOBODY;
    cand = bp_alive_mask(&bp_wk);
    bp_wk.round_left = cand;
    /* the roll-off: the tied highest re-roll among themselves */
    for (k = 0; k < 16; k++) {
        best = 0;
        tie = 0;
        for (i = 0; i < su->nseats; i++) {
            if (!(cand & bp_bit(i))) continue;
            j = bp_roll1();
            t = bp_roll1();
            bp_evw(BP_V_ORDER, i, 0, 0, (BPWord)(j * 16 + t));
            t += j;
            if (t > best) { best = t; tie = 0; }
            if (t == best) tie |= (BPByte)bp_bit(i);
        }
        cand = tie;
        if (!(cand & (cand - 1))) break;
    }
    for (i = 0; i < BP_SEATS - 1; i++) if (cand & bp_bit(i)) break;
    bp_ev(BP_V_FIRST, i, 0, 0, 0);
    bp_begin_turn(i);
    if (bp_err) { r->n = 0; return bp_err; }
    os88_memcpy(s, &bp_wk, sizeof(BPState));
    return BP_OK;
}

/* ===== the rules' own description of a legal match (SPEC.md 131.4.9) ==== */

static int ovl_bp_state_check(const BPState *s)
{
    int i, j, p, d, n, held, live, act;
    const BPSpace *sp;
    BPByte seen, m;
    unsigned cards;
    if (s->status != BP_ST_PLAY && s->status != BP_ST_OVER) return BP_EINVALID;
    if (s->nseats < 2 || s->nseats > BP_SEATS) return BP_EINVALID;
    if (s->rules == BP_RULES_CLASSIC) { if (s->rounds_limit) return BP_EINVALID; }
    else if (s->rules != BP_RULES_QUICK || (s->rounds_limit != 10 &&
             s->rounds_limit != 20 && s->rounds_limit != 30)) return BP_EINVALID;
    if (!s->rev.lo && !s->rev.hi) return BP_EINVALID;
    if (s->phase < BP_PH_MANAGE || s->phase > BP_PH_OVER) return BP_EINVALID;
    if ((s->status == BP_ST_OVER) != (s->phase == BP_PH_OVER)) return BP_EINVALID;
    live = bp_alive_mask(s);
    seen = 0;
    n = 0;
    for (i = 0; i < BP_SEATS; i++) {
        if (i >= s->nseats) {
            if (s->alive[i] || !bp_u32_iszero(&s->cash[i]) || s->disks[i]) return BP_EINVALID;
            if (s->detained[i] || s->tries[i] || s->pos[i]) return BP_EINVALID;
            if (s->token[i] || s->machine[i] || s->name[i][0]) return BP_EINVALID;
            continue;
        }
        if (s->alive[i] > 1 || s->token[i] > BP_T_CASSETTE || s->machine[i] > 1)
            return BP_EINVALID;
        if (s->pos[i] >= BP_SPACES || s->detained[i] > 1 || s->tries[i] > 3 ||
            (s->disks[i] & 0xFC)) return BP_EINVALID;
        if (seen & bp_bit(s->token[i])) return BP_EINVALID;
        seen |= (BPByte)bp_bit(s->token[i]);
        for (j = 0; j <= BP_NAMELEN && s->name[i][j]; j++)
            if ((unsigned char)s->name[i][j] < 32 || (unsigned char)s->name[i][j] > 126)
                return BP_EINVALID;
        if (j == 0 || j > BP_NAMELEN) return BP_EINVALID;
        if (s->detained[i] && s->pos[i] != BP_REPAIR_SPACE) return BP_EINVALID;
        if (s->tries[i] == 3 && !(s->phase == BP_PH_DEBT && s->active == i &&
                                  s->debt_reason == BP_DR_BAIL)) return BP_EINVALID;
        if (s->alive[i]) n++;
        else if (!bp_u32_iszero(&s->cash[i]) || s->disks[i] || s->detained[i] ||
                 s->tries[i]) return BP_EINVALID;
    }
    if (!n || (s->status == BP_ST_PLAY && n < 2)) return BP_EINVALID;
    /* properties */
    for (p = 0; p < BP_PROPS; p++) {
        sp = bp_space(bp_prop_space(p));
        if (s->level[p] > BP_MAX_LEVEL || s->mortgaged[p] > 1) return BP_EINVALID;
        if (s->owner[p] == BP_NOBODY) {
            if (s->level[p] || s->mortgaged[p]) return BP_EINVALID;
            continue;
        }
        if (s->owner[p] >= s->nseats || !s->alive[s->owner[p]]) return BP_EINVALID;
        if (sp->kind != BP_K_COMPUTER) { if (s->level[p]) return BP_EINVALID; continue; }
        bp_grp(s, sp->group);
        if (bp_g_up && (bp_g_own != s->owner[p] || bp_g_mort || bp_g_max - bp_g_min > 1))
            return BP_EINVALID;
    }
    /* the decks: each id once, in its own deck, or held */
    cards = 0;
    for (d = 0; d < BP_DECKS; d++) {
        held = 0;
        for (i = 0; i < s->nseats; i++) if (s->disks[i] & bp_bit(d)) held++;
        if (held > 1 || s->deck_n[d] != BP_DECK_CARDS - held) return BP_EINVALID;
        if (s->deck_top[d] >= s->deck_n[d]) return BP_EINVALID;
        if (held) cards |= 1u << bp_disk_id(d);
        for (i = 0; i < BP_DECK_CARDS; i++) {
            j = s->deck[d][i];
            if (i >= s->deck_n[d]) { if (j) return BP_EINVALID; continue; }
            if ((j >> 3) != d || (cards & (1u << j))) return BP_EINVALID;
            cards |= 1u << j;
        }
    }
    if (cards != 0xFFFFu) return BP_EINVALID;
    /* dice and turn */
    if (s->rolled > 1 || s->doubles > 2 || s->depth > BP_DEPTH_MAX) return BP_EINVALID;
    if (s->rolled ? (s->d1 < 1 || s->d1 > 6 || s->d2 < 1 || s->d2 > 6)
                  : (s->d1 || s->d2)) return BP_EINVALID;
    if (s->round_left & ~live) return BP_EINVALID;
    if (s->status == BP_ST_OVER) {
        if (!s->winners || (s->winners & ~live)) return BP_EINVALID;
        return BP_OK;
    }
    if (s->winners) return BP_EINVALID;
    for (i = 0; i < BP_SEATS; i++) if (s->score[i].lo || s->score[i].hi) return BP_EINVALID;
    act = s->active;
    if (act >= s->nseats || !s->alive[act] || !(s->round_left & bp_bit(act)))
        return BP_EINVALID;
    if (s->detained[act] && s->phase == BP_PH_MANAGE) return BP_EINVALID;
    if (!s->detained[act] && s->phase == BP_PH_REPAIR) return BP_EINVALID;
    /* the decider and the phase's own record */
    m = s->phase;
    if (m == BP_PH_AUCTION) { if (s->decider != s->auc_turn) return BP_EINVALID; }
    else if (m == BP_PH_TRADE) { if (s->decider != s->offer_to) return BP_EINVALID; }
    else if (s->decider != act) return BP_EINVALID;
    if ((m == BP_PH_CARD) != (s->card != BP_NOBODY)) return BP_EINVALID;
    if (m == BP_PH_CARD) {
        d = s->card >> 3;
        sp = bp_space(s->pos[act]);
        if (s->card >= 16 || sp->kind != BP_K_EVENT || sp->group != d ||
            s->deck[d][s->deck_top[d]] != s->card || !s->depth) return BP_EINVALID;
    }
    if (m == BP_PH_BUY || m == BP_PH_AUCTION) {
        p = bp_arg_prop(s->pos[act]);
        if (p < 0 || s->owner[p] != BP_NOBODY) return BP_EINVALID;
    }
    if (m == BP_PH_AUCTION) {
        if (s->auc_space != s->pos[act] || (s->auc_open & ~live) ||
            (s->auc_passed & ~s->auc_open) || s->auc_turn >= s->nseats)
            return BP_EINVALID;
        j = bp_bit(s->auc_turn);
        if (!(s->auc_open & j) || (s->auc_passed & j) || s->auc_turn == s->auc_high)
            return BP_EINVALID;
        if (s->auc_high == BP_NOBODY) {
            if (s->auc_bid.lo || s->auc_bid.hi) return BP_EINVALID;
        } else if (s->auc_high >= s->nseats || !(s->auc_open & bp_bit(s->auc_high)) ||
                   bp_u32_cmpw(&s->auc_bid, BP_BID_STEP) < 0 ||
                   bp_u32_cmp(&s->auc_bid, &s->cash[s->auc_high]) > 0)
            return BP_EINVALID;
    } else {
        if (s->auc_space != BP_NOBODY || s->auc_turn != BP_NOBODY ||
            s->auc_high != BP_NOBODY) return BP_EINVALID;
        if (s->auc_open || s->auc_passed || !bp_u32_iszero(&s->auc_bid))
            return BP_EINVALID;
    }
    if (m == BP_PH_DEBT) {
        if (s->ret_phase != BP_PH_AFTER || s->debt_reason < BP_DR_RENT ||
            s->debt_reason > BP_DR_BAIL || bp_u32_iszero(&s->debt_amount))
            return BP_EINVALID;
        if (s->debt_to != BP_NOBODY && (s->debt_to >= s->nseats ||
            s->debt_to == act || !s->alive[s->debt_to])) return BP_EINVALID;
        if ((s->debt_reason == BP_DR_BAIL) !=
            (s->pending_move != 0)) return BP_EINVALID;
        if (s->pending_move && (!s->rolled || s->pending_move != s->d1 + s->d2 ||
                                !s->detained[act])) return BP_EINVALID;
    } else if (s->debt_to != BP_NOBODY || s->debt_reason || s->pending_move ||
               !bp_u32_iszero(&s->debt_amount)) return BP_EINVALID;
    if (m == BP_PH_TRADE) {
        if (s->ret_phase != BP_PH_MANAGE && s->ret_phase != BP_PH_REPAIR &&
            s->ret_phase != BP_PH_AFTER) return BP_EINVALID;
        if (s->offer_from != act || !s->offer_id) return BP_EINVALID;
        if (bp_offer_recheck(s)) return BP_EINVALID;
    } else {
        if (s->offer_from != BP_NOBODY || s->offer_to != BP_NOBODY ||
            s->offer_disks || s->offer_payer) return BP_EINVALID;
        if (!bp_u32_iszero(&s->offer_give) || !bp_u32_iszero(&s->offer_get) ||
            !bp_u32_iszero(&s->offer_cash)) return BP_EINVALID;
        if (m != BP_PH_DEBT && s->ret_phase != BP_PH_NONE) return BP_EINVALID;
    }
    return BP_OK;
}

int bp_state_check(const BPState *s) { return ovl_bp_state_check(s); }
