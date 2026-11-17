/* Byteopoly - canonical encoding, validation, digest and the save-file
 * image (SPEC.md 131.7.1-131.7.3). Original. Lane: save.
 *
 * OS-free: the host harness (hosttest/t_save.c) builds this exact source.
 * Resident: the CRC, the encoder, the digest and the fingerprint (the link
 * calls them every transaction). Everything else is an ovl_* function: the
 * decoder, every validation rule and the file image, reached only on the UI
 * task from a menu command. The overlay shares this file's statics, so the
 * scratch is declared here once. No struct is copied, no address of an
 * automatic is taken, no recursion (SPEC.md 73). */

static BPByte bpc_io[BP_SAVE_MAX];       /* the codec's scratch: digest and file I/O */
static BPByte *bpc_ep;                   /* encoder cursor */
static const BPByte *bpc_dp;             /* decoder cursor */
static unsigned bpc_acc_crc, bpc_acc_s1, bpc_acc_s2;   /* the 32-bit digest accumulator */
static BPU32 bpc_t1, bpc_t2;              /* header temporaries */
static BPByte bpc_am, bpc_sm;             /* alive mask, seat mask of the state being checked */
static int bpc_why;                      /* the rule of the last validation refusal */

/* ---- CRC-16/CCITT-FALSE and the digest ---------------------------------- */
static unsigned bpc_crc_step(unsigned c, unsigned b)
{
    int k;
    c ^= (b & 0xFFu) << 8;
    for (k = 0; k < 8; k++)
        c = (c & 0x8000u) ? (((c << 1) ^ 0x1021u) & 0xFFFFu) : ((c << 1) & 0xFFFFu);
    return c;
}
BPWord bp_crc16(const BPByte *p, unsigned n, BPWord crc)
{
    unsigned c = crc;
    while (n--) c = bpc_crc_step(c, *p++);
    return (BPWord)c;
}
static void bpc_acc_init(void) { bpc_acc_crc = 0xFFFFu; bpc_acc_s1 = 0; bpc_acc_s2 = 0; }
static void bpc_acc(unsigned b)
{
    b &= 0xFFu;
    bpc_acc_crc = bpc_crc_step(bpc_acc_crc, b);
    bpc_acc_s1 += b; if (bpc_acc_s1 >= 255) bpc_acc_s1 -= 255;
    bpc_acc_s2 += bpc_acc_s1; if (bpc_acc_s2 >= 255) bpc_acc_s2 -= 255;
}
static void bpc_acc16(unsigned v) { bpc_acc(v); bpc_acc(v >> 8); }
static void bpc_acc_out(BPU32 *r)
{
    r->lo = (BPWord)bpc_acc_crc;
    r->hi = (BPWord)((bpc_acc_s2 << 8) | bpc_acc_s1);
}
static void bpc_digest_bytes(const BPByte *p, unsigned n, BPU32 *out)
{
    bpc_acc_init();
    while (n--) bpc_acc(*p++);
    bpc_acc_out(out);
}
void bp_fingerprint(BPU32 *out)
{
    int i;
    const BPSpace *sp;
    const BPCard *cd;
    bpc_acc_init();
    bpc_acc16(BP_RULES_VER);
    for (i = 0; i < BP_SPACES; i++) {
        sp = bp_space(i);
        bpc_acc(sp->kind); bpc_acc(sp->group); bpc_acc(sp->prop);
        bpc_acc16(sp->price); bpc_acc16(sp->rent);
    }
    for (i = 0; i < BP_DECKS * BP_DECK_CARDS; i++) {
        cd = bp_card(i);
        bpc_acc(cd->id); bpc_acc(cd->deck); bpc_acc(cd->effect); bpc_acc(cd->arg);
        bpc_acc16(cd->amount);
    }
    bpc_acc_out(out);
}

/* ---- the encoder (SPEC.md 131.7.1): little-endian, field by field -------- */
static void bpc_p8(unsigned v) { *bpc_ep++ = (BPByte)v; }
static void bpc_p16(unsigned v) { bpc_p8(v); bpc_p8(v >> 8); }
static void bpc_p32(const BPU32 *v) { bpc_p16(v->lo); bpc_p16(v->hi); }
static void bpc_pn(const BPByte *p, unsigned n) { while (n--) bpc_p8(*p++); }

unsigned bp_encode(const BPState *s, BPByte *buf, unsigned cap)
{
    int i, j, z;
    unsigned c;
    if (cap < BP_ENC_LEN) return 0;
    bpc_ep = buf;
    bpc_p8(BP_ENC_VER);
    bpc_p8(s->status); bpc_p8(s->rules); bpc_p8(s->rounds_limit); bpc_p8(s->nseats);
    bpc_p32(&s->match_id); bpc_p32(&s->rev);
    bpc_p16(s->turn); bpc_p16(s->round);
    bpc_p8(s->round_left); bpc_p8(s->phase); bpc_p8(s->active); bpc_p8(s->decider);
    for (i = 0; i < BP_SEATS; i++) {
        z = 0;
        for (j = 0; j < BP_NAMELEN; j++) {
            c = (BPByte)s->name[i][j];
            if (z || !c) { z = 1; c = 0; }
            bpc_p8(c);
        }
    }
    bpc_pn(s->token, BP_SEATS); bpc_pn(s->machine, BP_SEATS);
    bpc_pn(s->alive, BP_SEATS); bpc_pn(s->pos, BP_SEATS);
    for (i = 0; i < BP_SEATS; i++) bpc_p32(&s->cash[i]);
    bpc_pn(s->detained, BP_SEATS); bpc_pn(s->tries, BP_SEATS); bpc_pn(s->disks, BP_SEATS);
    bpc_pn(s->owner, BP_PROPS); bpc_pn(s->level, BP_PROPS); bpc_pn(s->mortgaged, BP_PROPS);
    bpc_p8(s->d1); bpc_p8(s->d2); bpc_p8(s->doubles); bpc_p8(s->rolled); bpc_p8(s->pending_move);
    for (i = 0; i < BP_DECKS; i++) bpc_pn(s->deck[i], BP_DECK_CARDS);
    bpc_pn(s->deck_n, BP_DECKS); bpc_pn(s->deck_top, BP_DECKS);
    bpc_p8(s->card); bpc_p8(s->ret_phase); bpc_p8(s->depth);
    bpc_p32(&s->rng);
    bpc_p8(s->auc_space); bpc_p8(s->auc_open); bpc_p8(s->auc_passed);
    bpc_p8(s->auc_turn); bpc_p8(s->auc_high);
    bpc_p32(&s->auc_bid);
    bpc_p8(s->debt_to); bpc_p8(s->debt_reason);
    bpc_p32(&s->debt_amount);
    bpc_p16(s->offer_id);
    bpc_p8(s->offer_from); bpc_p8(s->offer_to); bpc_p8(s->offer_disks); bpc_p8(s->offer_payer);
    bpc_p32(&s->offer_give); bpc_p32(&s->offer_get); bpc_p32(&s->offer_cash);
    for (i = 0; i < BP_SEATS; i++) bpc_p32(&s->score[i]);
    bpc_p8(s->winners);
    return BP_ENC_LEN;
}

void bp_digest(const BPState *s, BPU32 *out)
{
    unsigned n = bp_encode(s, bpc_io, BP_SAVE_MAX);
    bpc_digest_bytes(bpc_io, n, out);
}

int bp_invalid_rule(void) { return bpc_why; }

/* ---- the decoder ---------------------------------------------------------- */
static unsigned bpc_g8(void) { return *bpc_dp++; }
static unsigned bpc_g16(void)
{
    unsigned v = bpc_g8();
    return v | (bpc_g8() << 8);
}
static void bpc_g32(BPU32 *r) { r->lo = (BPWord)bpc_g16(); r->hi = (BPWord)bpc_g16(); }
static void bpc_gn(BPByte *d, unsigned n) { while (n--) *d++ = (BPByte)bpc_g8(); }

/* a validation rule that fails returns -(its number); ovl_bp_validate maps it */

/* ---- validation (SPEC.md 131.7.2), one function per group of rules ------- */
static int ovl_bp_v_head(const BPState *s)
{
    int i, n;
    if (s->status != BP_ST_PLAY && s->status != BP_ST_OVER) return -1;
    if (s->rules > BP_RULES_QUICK) return -1;
    if (s->nseats < 2 || s->nseats > BP_SEATS) return -1;
    if (s->rules == BP_RULES_CLASSIC) {
        if (s->rounds_limit) return -1;
    } else {
        if (s->rounds_limit != 10 && s->rounds_limit != 20 && s->rounds_limit != 30)
            return -1;
        if (s->round > s->rounds_limit) return -1;
    }
    if (s->phase < BP_PH_MANAGE || s->phase > BP_PH_OVER) return -2;
    if ((s->status == BP_ST_OVER) != (s->phase == BP_PH_OVER)) return -2;
    bpc_sm = (BPByte)((1 << s->nseats) - 1);
    bpc_am = 0; n = 0;
    for (i = 0; i < BP_SEATS; i++) {
        if (s->alive[i] > 1) return -3;
        if (s->alive[i]) {
            if (i >= s->nseats) return -3;
            bpc_am |= (BPByte)(1 << i); n++;
        }
    }
    if (n < (s->status == BP_ST_PLAY ? 2 : 1)) return -3;
    if (s->active >= s->nseats || s->decider >= s->nseats) return -3;
    if (s->round_left & ~bpc_sm) return -3;
    if (s->status == BP_ST_PLAY) {
        if (!s->alive[s->active] || !s->alive[s->decider]) return -3;
        if (s->round_left & ~bpc_am) return -3;
        if (s->winners) return -15;
    } else {
        if (!s->winners || (s->winners & ~bpc_sm)) return -15;
    }
    return BP_OK;
}

static int ovl_bp_v_seats(const BPState *s)
{
    int i, j, k, z;
    unsigned c;
    for (i = 0; i < BP_SEATS; i++) {
        if (i >= s->nseats) {
            if (s->token[i] | s->machine[i] | s->pos[i] | s->detained[i] | s->tries[i]
                | s->disks[i] | s->cash[i].lo | s->cash[i].hi | s->score[i].lo | s->score[i].hi)
                return -4;
            for (j = 0; j <= BP_NAMELEN; j++)
                if (s->name[i][j]) return -4;
            continue;
        }
        z = 0;
        for (j = 0; j < BP_NAMELEN; j++) {
            c = (BPByte)s->name[i][j];
            if (z) { if (c) return -4; }
            else if (!c) { z = 1; if (!j) return -4; }
            else if (c < 0x20 || c > 0x7E) return -4;
        }
        if (s->name[i][BP_NAMELEN]) return -4;
        if (s->token[i] >= 4 || s->machine[i] > 1) return -4;
        for (k = 0; k < i; k++)
            if (s->token[k] == s->token[i]) return -4;
        if (s->pos[i] >= BP_SPACES) return -5;
        if (s->detained[i] > 1 || (s->detained[i] && s->pos[i] != BP_REPAIR_SPACE))
            return -5;
        if (s->tries[i] > 3 || (s->tries[i] && !s->detained[i])) return -5;
        if (s->disks[i] & ~3) return -5;
        if (s->cash[i].hi >= 0x4000u) return -5;
        if (!s->alive[i] && (s->cash[i].lo | s->cash[i].hi | s->detained[i]
                             | s->tries[i] | s->disks[i]))
            return -5;
    }
    return BP_OK;
}

static BPByte bpc_gmin[BP_GROUPS], bpc_gmax[BP_GROUPS], bpc_gown[BP_GROUPS];
static BPByte bpc_gmix[BP_GROUPS], bpc_gmort[BP_GROUPS];

static int ovl_bp_v_props(const BPState *s)
{
    int p, g;
    unsigned o;
    const BPSpace *sp;
    for (g = 0; g < BP_GROUPS; g++) {
        bpc_gmin[g] = 255; bpc_gmax[g] = 0; bpc_gown[g] = 0xFE; bpc_gmix[g] = 0; bpc_gmort[g] = 0;
    }
    for (p = 0; p < BP_PROPS; p++) {
        o = s->owner[p];
        if (o != BP_NOBODY && (o >= s->nseats || !s->alive[o])) return -6;
        if (s->level[p] > BP_MAX_LEVEL || s->mortgaged[p] > 1) return -6;
        if (o == BP_NOBODY && (s->level[p] || s->mortgaged[p])) return -6;
        sp = bp_space(bp_prop_space(p));
        if (!sp) return -6;
        if (sp->kind == BP_K_UTILITY) {
            if (s->level[p]) return -7;
            continue;
        }
        g = sp->group;
        if (g >= BP_GROUPS) return -6;
        if (s->level[p] < bpc_gmin[g]) bpc_gmin[g] = s->level[p];
        if (s->level[p] > bpc_gmax[g]) bpc_gmax[g] = s->level[p];
        if (bpc_gown[g] == 0xFE) bpc_gown[g] = (BPByte)o;
        else if (bpc_gown[g] != o) bpc_gmix[g] = 1;
        if (s->mortgaged[p]) bpc_gmort[g] = 1;
    }
    for (g = 0; g < BP_GROUPS; g++)
        if (bpc_gmax[g] && (bpc_gmix[g] || bpc_gown[g] == BP_NOBODY || bpc_gmort[g]
                           || bpc_gmax[g] - bpc_gmin[g] > 1))
            return -7;
    return BP_OK;
}

static int ovl_bp_v_decks(const BPState *s)
{
    int d, i, j, k, held;
    unsigned seen, c;
    for (d = 0; d < BP_DECKS; d++) {
        held = 0;
        for (i = 0; i < BP_SEATS; i++)
            if ((s->disks[i] >> d) & 1) held++;
        if (held > 1) return -9;
        if (s->deck_n[d] > BP_DECK_CARDS || s->deck_n[d] + held != BP_DECK_CARDS)
            return -9;
        if (s->deck_top[d] >= s->deck_n[d] && (s->deck_n[d] || s->deck_top[d]))
            return -9;
        seen = 0;
        for (j = 0; j < s->deck_n[d]; j++) {
            c = s->deck[d][j];
            if ((c >> 3) != d || ((seen >> (c & 7)) & 1)) return -9;
            seen |= 1u << (c & 7);
        }
        if (held)
            for (k = 0; k < BP_DECK_CARDS; k++)
                if (bp_card(d * BP_DECK_CARDS + k)->effect == BP_E_KEEP && ((seen >> k) & 1))
                    return -9;
    }
    if (s->card != BP_NOBODY && s->card >= BP_DECKS * BP_DECK_CARDS) return -9;
    return BP_OK;
}

static int ovl_bp_bit(const BPU32 *m, int p)
{
    return p < 16 ? (m->lo >> p) & 1 : (m->hi >> (p - 16)) & 1;
}

static int ovl_bp_v_auction(const BPState *s)
{
    unsigned h;
    if (s->auc_space >= BP_PROPS || s->owner[s->auc_space] != BP_NOBODY) return -11;
    if ((s->auc_open & s->auc_passed) || !s->auc_open
        || (BPByte)(s->auc_open | s->auc_passed) != bpc_am) return -11;
    if (s->auc_turn >= BP_SEATS || !((s->auc_open >> s->auc_turn) & 1)) return -11;
    h = s->auc_high;
    if (h == BP_NOBODY) {
        if (s->auc_bid.lo || s->auc_bid.hi) return -11;
        return BP_OK;
    }
    if (h >= BP_SEATS || !((s->auc_open >> h) & 1)) return -11;
    if (!(s->auc_bid.lo | s->auc_bid.hi)) return -11;
    if (((s->auc_bid.hi % 10) * 6 + s->auc_bid.lo % 10) % 10) return -11;
    if (s->auc_bid.hi > s->cash[h].hi
        || (s->auc_bid.hi == s->cash[h].hi && s->auc_bid.lo > s->cash[h].lo))
        return -11;
    return BP_OK;
}

static int ovl_bp_v_card(const BPState *s)
{
    int j, d, dest;
    const BPCard *cd;
    if (s->card >= BP_DECKS * BP_DECK_CARDS) return -12;
    d = s->card >> 3;
    for (j = 0; j < s->deck_n[d]; j++)
        if (s->deck[d][j] == s->card) break;
    if (j == s->deck_n[d]) return -12;
    cd = bp_card(s->card);
    dest = -1;
    if (cd->effect == BP_E_TO) dest = cd->arg;
    else if (cd->effect == BP_E_FORWARD) dest = (s->pos[s->active] + cd->arg) % BP_SPACES;
    else if (cd->effect == BP_E_BACK) dest = (s->pos[s->active] + BP_SPACES - cd->arg) % BP_SPACES;
    if (dest >= 0 && bp_space(dest)->kind == BP_K_UTILITY && !s->rolled) return -12;
    return BP_OK;
}

static int ovl_bp_v_trade(const BPState *s)
{
    int p;
    if (s->ret_phase != BP_PH_MANAGE && s->ret_phase != BP_PH_REPAIR
        && s->ret_phase != BP_PH_AFTER) return -14;
    if (s->offer_from != s->active || s->offer_to >= s->nseats
        || s->offer_to == s->offer_from || !s->alive[s->offer_to]) return -14;
    if (s->offer_payer > 1 || (s->offer_disks & ~0x33)) return -14;
    if ((s->offer_disks & 3) & ~s->disks[s->offer_from]) return -14;
    if (((s->offer_disks >> 4) & 3) & ~s->disks[s->offer_to]) return -14;
    if (s->offer_give.hi >= 4 || s->offer_get.hi >= 4) return -14;
    for (p = 0; p < BP_PROPS; p++) {
        if (ovl_bp_bit(&s->offer_give, p) && s->owner[p] != s->offer_from) return -14;
        if (ovl_bp_bit(&s->offer_get, p) && s->owner[p] != s->offer_to) return -14;
    }
    if (!(s->offer_give.lo | s->offer_give.hi | s->offer_get.lo | s->offer_get.hi
          | s->offer_cash.lo | s->offer_cash.hi | s->offer_disks)) return -14;
    return BP_OK;
}

static int ovl_bp_v_phase(const BPState *s)
{
    const BPSpace *sp;
    if (s->d1 > 6 || s->d2 > 6 || (s->d1 == 0) != (s->d2 == 0)) return -8;
    if (s->rolled > 1 || (s->rolled && !s->d1) || s->doubles > 2) return -8;
    if (s->pending_move > 1 || (s->pending_move && (s->phase != BP_PH_DEBT || !s->rolled)))
        return -8;
    switch (s->phase) {
    case BP_PH_REPAIR:
        if (!s->detained[s->active]) return -10;
        break;
    case BP_PH_BUY:
        sp = bp_space(s->pos[s->active]);
        if ((sp->kind != BP_K_COMPUTER && sp->kind != BP_K_UTILITY)
            || sp->prop >= BP_PROPS || s->owner[sp->prop] != BP_NOBODY) return -10;
        break;
    case BP_PH_AUCTION:
        return ovl_bp_v_auction(s);
    case BP_PH_CARD:
        return ovl_bp_v_card(s);
    case BP_PH_DEBT:
        if (!(s->debt_amount.lo | s->debt_amount.hi)) return -13;
        if (s->debt_to != BP_NOBODY
            && (s->debt_to >= s->nseats || !s->alive[s->debt_to] || s->debt_to == s->active))
            return -13;
        if (s->ret_phase != BP_PH_MANAGE && s->ret_phase != BP_PH_REPAIR
            && s->ret_phase != BP_PH_BUY && s->ret_phase != BP_PH_CARD
            && s->ret_phase != BP_PH_AFTER) return -13;
        break;
    case BP_PH_TRADE:
        return ovl_bp_v_trade(s);
    default:
        break;
    }
    return BP_OK;
}

static int ovl_bp_validate(const BPState *s)
{
    int r;
    r = ovl_bp_v_head(s);
    if (!r) r = ovl_bp_v_seats(s);
    if (!r) r = ovl_bp_v_props(s);
    if (!r) r = ovl_bp_v_decks(s);
    if (!r) r = ovl_bp_v_phase(s);
    bpc_why = -r;
    return r ? BP_EINVALID : BP_OK;
}
int bp_validate(const BPState *s) { return ovl_bp_validate(s); }

static int ovl_bp_decode(const BPByte *buf, unsigned n, BPState *out)
{
    int i;
    if (!n) return BP_EFORMAT;
    if (buf[0] != BP_ENC_VER) return BP_EVERSION;
    if (n != BP_ENC_LEN) return BP_EFORMAT;
    os88_memset(out, 0, sizeof(BPState));
    bpc_dp = buf + 1;
    out->status = (BPByte)bpc_g8(); out->rules = (BPByte)bpc_g8();
    out->rounds_limit = (BPByte)bpc_g8(); out->nseats = (BPByte)bpc_g8();
    bpc_g32(&out->match_id); bpc_g32(&out->rev);
    out->turn = (BPWord)bpc_g16(); out->round = (BPWord)bpc_g16();
    out->round_left = (BPByte)bpc_g8(); out->phase = (BPByte)bpc_g8();
    out->active = (BPByte)bpc_g8(); out->decider = (BPByte)bpc_g8();
    for (i = 0; i < BP_SEATS; i++) bpc_gn((BPByte *)out->name[i], BP_NAMELEN);
    bpc_gn(out->token, BP_SEATS); bpc_gn(out->machine, BP_SEATS);
    bpc_gn(out->alive, BP_SEATS); bpc_gn(out->pos, BP_SEATS);
    for (i = 0; i < BP_SEATS; i++) bpc_g32(&out->cash[i]);
    bpc_gn(out->detained, BP_SEATS); bpc_gn(out->tries, BP_SEATS); bpc_gn(out->disks, BP_SEATS);
    bpc_gn(out->owner, BP_PROPS); bpc_gn(out->level, BP_PROPS); bpc_gn(out->mortgaged, BP_PROPS);
    out->d1 = (BPByte)bpc_g8(); out->d2 = (BPByte)bpc_g8(); out->doubles = (BPByte)bpc_g8();
    out->rolled = (BPByte)bpc_g8(); out->pending_move = (BPByte)bpc_g8();
    for (i = 0; i < BP_DECKS; i++) bpc_gn(out->deck[i], BP_DECK_CARDS);
    bpc_gn(out->deck_n, BP_DECKS); bpc_gn(out->deck_top, BP_DECKS);
    out->card = (BPByte)bpc_g8(); out->ret_phase = (BPByte)bpc_g8(); out->depth = (BPByte)bpc_g8();
    bpc_g32(&out->rng);
    out->auc_space = (BPByte)bpc_g8(); out->auc_open = (BPByte)bpc_g8();
    out->auc_passed = (BPByte)bpc_g8(); out->auc_turn = (BPByte)bpc_g8();
    out->auc_high = (BPByte)bpc_g8();
    bpc_g32(&out->auc_bid);
    out->debt_to = (BPByte)bpc_g8(); out->debt_reason = (BPByte)bpc_g8();
    bpc_g32(&out->debt_amount);
    out->offer_id = (BPWord)bpc_g16();
    out->offer_from = (BPByte)bpc_g8(); out->offer_to = (BPByte)bpc_g8();
    out->offer_disks = (BPByte)bpc_g8(); out->offer_payer = (BPByte)bpc_g8();
    bpc_g32(&out->offer_give); bpc_g32(&out->offer_get); bpc_g32(&out->offer_cash);
    for (i = 0; i < BP_SEATS; i++) bpc_g32(&out->score[i]);
    out->winners = (BPByte)bpc_g8();
    return ovl_bp_validate(out);
}
int bp_decode(const BPByte *buf, unsigned n, BPState *out) { return ovl_bp_decode(buf, n, out); }

/* ---- the save-file image (SPEC.md 131.7.3) -------------------------------- */
static unsigned ovl_bp_file_build(const BPCkpt *m, const BPState *s, BPByte *buf, unsigned cap)
{
    unsigned n, c;
    if (cap < BP_FILE_HDR + BP_ENC_LEN + 2) return 0;
    if (m->role > 2 || m->state < 1 || m->state > 2) return 0;
    n = bp_encode(s, buf + BP_FILE_HDR, cap - BP_FILE_HDR);
    if (!n) return 0;
    bpc_digest_bytes(buf + BP_FILE_HDR, n, &bpc_t1);
    bp_fingerprint(&bpc_t2);
    bpc_ep = buf;
    bpc_p8('B'); bpc_p8('P'); bpc_p8('S'); bpc_p8('V');
    bpc_p8(1); bpc_p8(m->role); bpc_p8(m->state); bpc_p8(0);
    bpc_p16(BP_RULES_VER); bpc_p32(&bpc_t2);
    bpc_p32(&s->match_id); bpc_p32(&m->ckpt_id); bpc_p32(&s->rev); bpc_p32(&bpc_t1);
    bpc_p32(&m->host_id); bpc_p32(&m->client_id);
    bpc_p16(n);
    bpc_ep = buf + BP_FILE_HDR + n;
    c = bp_crc16(buf, BP_FILE_HDR + n, 0xFFFFu);
    bpc_p16(c);
    return BP_FILE_HDR + n + 2;
}
unsigned bp_file_build(const BPCkpt *m, const BPState *s, BPByte *buf, unsigned cap)
{
    return ovl_bp_file_build(m, s, buf, cap);
}

static int ovl_bp_file_peek(const BPByte *buf, unsigned n, BPCkpt *m)
{
    unsigned len, c;
    if (n < BP_FILE_HDR + 2) return BP_EFORMAT;
    if (buf[0] != 'B' || buf[1] != 'P' || buf[2] != 'S' || buf[3] != 'V') return BP_EFORMAT;
    if (buf[4] != 1) return BP_EVERSION;
    len = buf[38] | ((unsigned)buf[39] << 8);
    if (len > BP_STATE_MAX || n != BP_FILE_HDR + len + 2) return BP_EFORMAT;
    c = bp_crc16(buf, n - 2, 0xFFFFu);
    if (c != (buf[n - 2] | ((unsigned)buf[n - 1] << 8))) return BP_ECRC;
    if (buf[5] > 2 || buf[6] < 1 || buf[6] > 2) return BP_EFORMAT;
    bpc_dp = buf + 8;
    if (bpc_g16() != BP_RULES_VER) return BP_EVERSION;
    bpc_g32(&bpc_t1);
    bp_fingerprint(&bpc_t2);
    if (bpc_t1.lo != bpc_t2.lo || bpc_t1.hi != bpc_t2.hi) return BP_EMATCH;
    bpc_g32(&m->match_id); bpc_g32(&m->ckpt_id); bpc_g32(&m->rev); bpc_g32(&m->digest);
    bpc_g32(&m->host_id); bpc_g32(&m->client_id);
    m->role = buf[5]; m->state = buf[6];
    return BP_OK;
}
int bp_file_peek(const BPByte *buf, unsigned n, BPCkpt *m) { return ovl_bp_file_peek(buf, n, m); }

static int ovl_bp_file_parse(const BPByte *buf, unsigned n, BPCkpt *m, BPState *out)
{
    int r, i;
    unsigned mm;
    r = ovl_bp_file_peek(buf, n, m);
    if (r) return r;
    r = ovl_bp_decode(buf + BP_FILE_HDR, n - BP_FILE_HDR - 2, out);
    if (r) return r;
    bpc_digest_bytes(buf + BP_FILE_HDR, n - BP_FILE_HDR - 2, &bpc_t1);
    if (m->match_id.lo != out->match_id.lo || m->match_id.hi != out->match_id.hi
        || m->rev.lo != out->rev.lo || m->rev.hi != out->rev.hi
        || m->digest.lo != bpc_t1.lo || m->digest.hi != bpc_t1.hi) return BP_EFORMAT;
    mm = 0;
    for (i = 0; i < out->nseats; i++) mm |= 1u << out->machine[i];
    if (mm != (m->role ? 3u : 1u)) return BP_EMATCH;
    return BP_OK;
}
int bp_file_parse(const BPByte *buf, unsigned n, BPCkpt *m, BPState *out)
{
    return ovl_bp_file_parse(buf, n, m, out);
}
