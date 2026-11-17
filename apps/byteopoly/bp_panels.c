/* Byteopoly - the in-game panels (SPEC.md 131.9.7-131.9.11). Lane: panels.
 *
 * ONE decision, one panel: gp_spec() maps (bp_game, the link, the panel
 * the player opened) to a panel KIND and its row of buttons, and everything
 * else reads that - draw, key, click and the hint bar - so a button, its
 * key, its mouse rectangle and its hint entry cannot disagree. Nothing here
 * writes bp_game: a decision is a BPCmd through bp_sess_submit (131.3.5).
 * Every function but the thin doors is an ovl_* (131.11: eager overlay);
 * only the data is resident. */

int bx_artstr(int id, int n, char *buf);
/* #define BP_PANELS_DEMO 1 */   /* looking only: hand-made states, F5 steps; never shipped */

#define GP_MAXB 6
/* panel kinds */
#define GK_NONE    0
#define GK_IDLE    1    /* MANAGE / AFTER: a bar over the bottom of the centre */
#define GK_BUY     2
#define GK_AUC     3
#define GK_CARD    4
#define GK_REPAIR  5
#define GK_DEBT    6
#define GK_TRADE   7    /* the frozen offer, for review */
#define GK_OVER    8
#define GK_REVIEW  9    /* match over, board shown: a bar */
#define GK_HAND    10
#define GK_PAUSE   11
#define GK_MANAGE  12
#define GK_COMPOSE 13
#define GK_CHAT    14
#define GK_DIAG    15
#define GK_RULES   16
#define GK_KEYS    17
#define GK_STAND   18
/* keys: navigation scancodes, delivered with ascii 0 */
#define GS_ESC  0x01
#define GS_TAB  0x0F
#define GS_ENT  0x1C
#define GS_UP   0x48
#define GS_LT   0x4B
#define GS_RT   0x4D
#define GS_DN   0x50
#define GS_PGUP 0x49
#define GS_PGDN 0x51
#define GP_CHATMAX 24

/* ---- resident state ----------------------------------------------------- */
static BPRect gp_r, gp_br[GP_MAXB], gp_xm, gp_xp, gp_lr[2];
static BPByte gp_bkey[GP_MAXB], gp_bwhy[GP_MAXB], gp_nb, gp_foc, gp_kind, gp_drawn;
static char gp_blab[GP_MAXB][12];
static int gp_font, gp_fh, gp_pitch, gp_bh, gp_tb, gp_x, gp_y, gp_btop, gp_xr, gp_cw, gp_lrow;
static char gp_line[52];
static char gp_hb[GP_MAXB + 1][14];
static const char *gp_err;
static BPByte gp_over;          /* a panel the player opened (BP_GP_*) */
static BPByte gp_conf;          /* a confirmation is showing (bankrupt, leave) */
static BPByte gp_review;        /* match over: board shown instead of the panel */
static BPByte gp_oc;            /* the end's choice for the app: 1 review 2 new 3 exit */
static BPByte gp_ack, gp_lastd; /* the local decider the handoff last covered */
static BPWord gp_mid, gp_trev, gp_trevh;
static BPByte gp_auto;          /* seats passed for want of a legal raise */
static BPByte gp_help;          /* rules/keys page */
static BPU32 gp_bid, gp_u1, gp_u2;
static BPByte gp_typed;
static BPByte gp_mc;            /* manage cursor */
/* handoff */
static BPByte gp_hand, gp_hmap[16], gp_hcur[16], gp_hmapok;
static unsigned gp_hlast, gp_hsince;
/* trade composer */
static BPByte gp_tto, gp_tpay, gp_tcol, gp_tcur[2], gp_ttop[2], gp_in[2], gp_it[2][BP_PROPS + 2];
static BPU32 gp_tgive, gp_tget;
static BPByte gp_tdk;
static BPWord gp_tcash;
/* chat */
static char gp_cbuf[GP_CHATMAX + 1], gp_cl[3][30];
static BPByte gp_cn;

#ifdef BP_PANELS_DEMO
static BPByte gp_demo;
static void ovl_gp_demoset(int n);
#define bp_can(s, se, t, a) 0
#endif

/* ---- text builders ------------------------------------------------------ */
static void ovl_gp_lb(void) { gp_line[0] = 0; }
static void ovl_gp_la(const char *s)
{
    unsigned n;
    n = os88_strlen(gp_line);
    os88_strcpy(gp_line + n, s, sizeof(gp_line) - n);
}
static void ovl_gp_lc(int c)
{
    unsigned n;
    n = os88_strlen(gp_line);
    if (n < sizeof(gp_line) - 1) { gp_line[n] = (char)c; gp_line[n + 1] = 0; }
}
static void ovl_gp_lu(unsigned v)
{
    static char nb[8];
    ovl_gp_la(os88_utoa(v, nb));
}
static void ovl_gp_lv(const BPU32 *v)
{
    static char mb[14];
    ovl_gp_la(bp_u32_fmt(mb, v));
}
static void ovl_gp_lm(const BPU32 *v) { ovl_gp_lv(v); ovl_gp_la(" cr"); }
static const char *ovl_gp_name(int seat)
{
    if (seat < 0 || seat >= BP_SEATS) return "the bank";
    return bp_game.name[seat];
}
static int ovl_gp_bit(const BPU32 *m, int p)
{
    if (p < 16) return (m->lo >> p) & 1;
    return (m->hi >> (p - 16)) & 1;
}
static void ovl_gp_tog(BPU32 *m, int p)
{
    if (p < 16) m->lo ^= (BPWord)(1u << p); else m->hi ^= (BPWord)(1u << (p - 16));
}
static int ovl_gp_isempty(const BPU32 *m) { return m->lo == 0 && m->hi == 0; }
static const char *ovl_gp_pname(int prop) { return bp_space(bp_prop_space(prop))->shortname; }
/* the keys' names, for a hint entry and a button */
static void ovl_gp_kn(char *dst, int key)
{
    if (key == ' ') os88_strcpy(dst, "Space", 8);
    else if (key == 13) os88_strcpy(dst, "Enter", 8);
    else if (key == 27) os88_strcpy(dst, "Esc", 8);
    else { dst[0] = (char)(key >= 'a' && key <= 'z' ? key - 32 : key); dst[1] = 0; }
}

/* ---- who may act, and which panel ---------------------------------------- */
static int ovl_gp_mine(void) { return bp_sess_mine(bp_game.decider); }
static int ovl_gp_acts(void) { return ovl_gp_mine() && !bp_sess_busy() && !gp_hand; }
static int ovl_gp_nlocal(void)
{
    int s, n;
    n = 0;
    for (s = 0; s < bp_game.nseats; s++)
        if (bp_game.alive[s] && bp_sess_mine(s)) n++;
    return n;
}
/* the reason a button is grey: busy beats the engine's own answer */
static int ovl_gp_can(int type, int arg)
{
    if (!ovl_gp_mine()) return BP_ENOTMINE;
    if (bp_sess_busy()) return BP_EPENDING;
    return bp_can(&bp_game, bp_game.decider, type, arg);
}
static void ovl_gp_add(int key, const char *lab, int why)
{
    if (gp_nb >= GP_MAXB) return;
    gp_bkey[gp_nb] = (BPByte)key;
    os88_strcpy(gp_blab[gp_nb], lab, sizeof(gp_blab[0]));
    gp_bwhy[gp_nb] = (BPByte)why;
    gp_nb++;
}
static int ovl_gp_minbid(BPU32 *out)
{
    if (bp_game.auc_high == BP_NOBODY) { bp_u32_set(out, 10, 0); return 0; }
    bp_u32_copy(out, &bp_game.auc_bid);
    bp_u32_addw(out, 10);
    return 0;
}
/* a bid is legal when >= the minimum, a multiple of 10 and within the cash */
static int ovl_gp_bidwhy(void)
{
    int m10;
    ovl_gp_minbid(&gp_u1);
    if (bp_u32_cmp(&gp_bid, &gp_u1) < 0) return BP_EARG;
    m10 = (int)((gp_bid.lo % 10 + (gp_bid.hi % 10) * 6) % 10);
    if (m10) return BP_EARG;
    if (bp_u32_cmp(&gp_bid, &bp_game.cash[bp_game.decider]) > 0) return BP_EFUNDS;
    return BP_OK;
}
static void ovl_gp_bidinit(void)
{
    ovl_gp_minbid(&gp_bid);
    gp_typed = 0;
}
/* the seat the Collection panel shows: the decider if local, else a local one */
static int ovl_gp_mseat(void)
{
    int s;
    if (bp_sess_mine(bp_game.decider)) return bp_game.decider;
    for (s = 0; s < bp_game.nseats; s++)
        if (bp_game.alive[s] && bp_sess_mine(s)) return s;
    return bp_game.decider;
}
static int ovl_gp_mlist(BPByte *out)
{
    int p, n, seat;
    seat = ovl_gp_mseat();
    n = 0;
    for (p = 0; p < BP_PROPS; p++)
        if (bp_game.owner[p] == seat) out[n++] = (BPByte)p;
    return n;
}
static BPByte gp_ml[BP_PROPS];
static int gp_mn;

/* a computer is tradable only when no member of its collection is upgraded */
static int ovl_gp_tradable(int p)
{
    int q, g;
    g = bp_space(bp_prop_space(p))->group;
    if (bp_space(bp_prop_space(p))->kind != BP_K_COMPUTER) return 1;
    for (q = 0; q < BP_PROPS; q++)
        if (bp_space(bp_prop_space(q))->kind == BP_K_COMPUTER &&
            bp_space(bp_prop_space(q))->group == g && bp_game.level[q]) return 0;
    return 1;
}
static void ovl_gp_tbuild(void)
{
    int c, p, d, n, seat;
    for (c = 0; c < 2; c++) {
        seat = c ? gp_tto : bp_game.decider;
        n = 0;
        for (p = 0; p < BP_PROPS; p++)
            if (bp_game.owner[p] == seat) gp_it[c][n++] = (BPByte)p;
        for (d = 0; d < BP_DECKS; d++)
            if ((bp_game.disks[seat] >> d) & 1) gp_it[c][n++] = (BPByte)(32 + d);
        gp_in[c] = (BPByte)n;
        if (gp_tcur[c] >= n) gp_tcur[c] = (BPByte)(n ? n - 1 : 0);
    }
}
static int ovl_gp_tsel(int c, int idx)
{
    int it;
    it = gp_it[c][idx];
    if (it >= 32) return (gp_tdk >> ((it - 32) + (c ? 4 : 0))) & 1;
    return ovl_gp_bit(c ? &gp_tget : &gp_tgive, it);
}
static void ovl_gp_ttoggle(int c, int idx)
{
    int it;
    if (!gp_in[c]) return;
    it = gp_it[c][idx];
    if (it >= 32) { gp_tdk ^= (BPByte)(1 << ((it - 32) + (c ? 4 : 0))); return; }
    if (!ovl_gp_tradable(it)) { gp_err = "Upgraded sets cannot be traded"; return; }
    ovl_gp_tog(c ? &gp_tget : &gp_tgive, it);
}
static int ovl_gp_tdefault(void)
{
    int s, n;
    n = 0;
    for (s = 1; s <= bp_game.nseats; s++) {
        n = (bp_game.decider + s) % bp_game.nseats;
        if (n != bp_game.decider && bp_game.alive[n]) return n;
    }
    return bp_game.decider;
}
static void ovl_gp_topen(void)
{
    gp_tto = (BPByte)ovl_gp_tdefault();
    gp_tgive.lo = gp_tgive.hi = gp_tget.lo = gp_tget.hi = 0;
    gp_tdk = 0; gp_tcash = 0; gp_tpay = 0; gp_tcol = 0;
    gp_tcur[0] = gp_tcur[1] = gp_ttop[0] = gp_ttop[1] = 0;
    gp_over = BP_GP_TRADE;
}

/* the panel the state asks for, and its buttons */
static void ovl_gp_spec(void)
{
    int ph, d, ls, i, why, dk;
    gp_nb = 0; gp_kind = GK_NONE;
    if (gp_over == BP_GP_CHAT) gp_kind = GK_CHAT;
    else if (gp_over == BP_GP_DIAG) gp_kind = GK_DIAG;
    else if (gp_over == BP_GP_RULES) gp_kind = GK_RULES;
    else if (gp_over == BP_GP_KEYS) gp_kind = GK_KEYS;
    else if (gp_over == BP_GP_STANDINGS) gp_kind = GK_STAND;
    ph = bp_game.phase; d = bp_game.decider;
    ls = bp_link_state();
    if (!gp_kind) {
        if (bp_game.status != BP_ST_PLAY && bp_game.status != BP_ST_OVER) return;
        if (bp_mode != BP_MODE_LOCAL && (ls == BP_L_PAUSED || ls == BP_L_RESYNC)) gp_kind = GK_PAUSE;
        else if (gp_hand) gp_kind = GK_HAND;
        else if (gp_over == BP_GP_MANAGE) gp_kind = GK_MANAGE;
        else if (gp_over == BP_GP_TRADE) {
            if ((ph == BP_PH_MANAGE || ph == BP_PH_REPAIR || ph == BP_PH_AFTER) && ovl_gp_mine())
                gp_kind = GK_COMPOSE;
            else gp_over = 0;
        }
    }
    if (!gp_kind) {
        if (bp_game.status == BP_ST_OVER || ph == BP_PH_OVER) gp_kind = gp_review ? GK_REVIEW : GK_OVER;
        else if (ph == BP_PH_BUY) gp_kind = GK_BUY;
        else if (ph == BP_PH_AUCTION) gp_kind = GK_AUC;
        else if (ph == BP_PH_CARD) gp_kind = GK_CARD;
        else if (ph == BP_PH_REPAIR) gp_kind = GK_REPAIR;
        else if (ph == BP_PH_DEBT) gp_kind = GK_DEBT;
        else if (ph == BP_PH_TRADE) gp_kind = GK_TRADE;
        else if (ph == BP_PH_MANAGE || ph == BP_PH_AFTER) gp_kind = GK_IDLE;
        else return;
    }
    switch (gp_kind) {
    case GK_IDLE:
        if (ovl_gp_mine()) {
            ovl_gp_add(' ', "Roll", ovl_gp_can(BP_C_ROLL, 0));
            ovl_gp_add('e', "End Turn", ovl_gp_can(BP_C_END_TURN, 0));
            ovl_gp_add('t', "Trade", ovl_gp_can(BP_C_PROPOSE, 0));
        }
        ovl_gp_add('m', "Manage", BP_OK);
        break;
    case GK_BUY:
        if (ovl_gp_mine()) {
            ovl_gp_add('b', "Buy", ovl_gp_can(BP_C_BUY, 0));
            ovl_gp_add('a', "Auction", ovl_gp_can(BP_C_DECLINE, 0));
        }
        break;
    case GK_AUC:
        if (ovl_gp_mine()) {
            why = ovl_gp_can(BP_C_BID, 0);
            if (!why) why = ovl_gp_bidwhy();
            ovl_gp_lb(); ovl_gp_la("Bid "); ovl_gp_lv(&gp_bid);
            ovl_gp_add('b', gp_line, why);
            ovl_gp_add('p', "Pass", ovl_gp_can(BP_C_PASS, 0));
        }
        break;
    case GK_CARD:
        if (ovl_gp_mine()) ovl_gp_add(13, "OK", ovl_gp_can(BP_C_ACK_CARD, 0));
        break;
    case GK_REPAIR:
        if (ovl_gp_mine()) {
            dk = bp_game.disks[d];
            ovl_gp_add('p', "Pay 50", ovl_gp_can(BP_C_PAY_BAIL, 0));
            if (dk & 1) ovl_gp_add('f', "Disk Find", ovl_gp_can(BP_C_USE_DISK, 0));
            if (dk & 2) ovl_gp_add('s', "Disk Error", ovl_gp_can(BP_C_USE_DISK, 1));
            ovl_gp_add('r', "Roll", ovl_gp_can(BP_C_ROLL, 0));
        }
        ovl_gp_add('m', "Manage", BP_OK);
        break;
    case GK_DEBT:
        if (ovl_gp_mine()) {
            if (gp_conf) {
                ovl_gp_add('k', "Yes, bankrupt", ovl_gp_can(BP_C_BANKRUPT, 0));
                ovl_gp_add(27, "No", BP_OK);
            } else {
                ovl_gp_add('m', "Manage", BP_OK);
                ovl_gp_add('s', "Settle", ovl_gp_can(BP_C_SETTLE, 0));
                ovl_gp_add('k', "Bankrupt", ovl_gp_can(BP_C_BANKRUPT, 0));
            }
        } else ovl_gp_add('m', "Manage", BP_OK);
        break;
    case GK_TRADE:
        if (ovl_gp_mine()) {
            ovl_gp_add('a', "Accept", ovl_gp_can(BP_C_ACCEPT, bp_game.offer_id & 255));
            ovl_gp_add('r', "Reject", ovl_gp_can(BP_C_REJECT, 0));
        }
        if (bp_sess_mine(bp_game.offer_from))
            ovl_gp_add('c', "Cancel", bp_sess_busy() ? BP_EPENDING :
                       bp_can(&bp_game, bp_game.offer_from, BP_C_CANCEL, 0));
        break;
    case GK_OVER:
        ovl_gp_add('r', "Review", BP_OK);
        ovl_gp_add('n', "New Game", BP_OK);
        ovl_gp_add('x', "Exit", BP_OK);
        break;
    case GK_REVIEW:
        ovl_gp_add('s', "Standings", BP_OK);
        break;
    case GK_HAND:
        ovl_gp_add(13, "Ready", BP_OK);
        break;
    case GK_PAUSE:
        if (gp_conf) {
            ovl_gp_add('x', "Yes, leave", BP_OK);
            ovl_gp_add(27, "No", BP_OK);
        } else {
            ovl_gp_add('r', "Retry", BP_OK);
            ovl_gp_add('l', "Load Pair", BP_OK);
            ovl_gp_add('d', "Details", BP_OK);
            ovl_gp_add('x', "Leave", BP_OK);
        }
        break;
    case GK_MANAGE:
        gp_mn = ovl_gp_mlist(gp_ml);
        if (gp_mc >= gp_mn) gp_mc = (BPByte)(gp_mn ? gp_mn - 1 : 0);
        i = gp_mn ? bp_prop_space(gp_ml[gp_mc]) : 0;
        why = gp_mn ? 0 : BP_EOWNER;
        ovl_gp_add('u', "Upgrade", why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_UPGRADE, i)));
        ovl_gp_add('s', "Sell", why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_SELL, i)));
        ovl_gp_add('m', "Mortgage", why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_MORTGAGE, i)));
        ovl_gp_add('r', "Redeem", why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_REDEEM, i)));
        ovl_gp_add(27, "Back", BP_OK);
        break;
    case GK_COMPOSE:
        why = (ovl_gp_isempty(&gp_tgive) && ovl_gp_isempty(&gp_tget) && !gp_tdk && !gp_tcash) ? BP_EARG
              : ovl_gp_can(BP_C_PROPOSE, 0);
        ovl_gp_add(13, "Propose", why);
        ovl_gp_add('n', "Next", BP_OK);
        ovl_gp_add('p', "Payer", BP_OK);
        ovl_gp_add(27, "Cancel", BP_OK);
        break;
    case GK_CHAT:
        ovl_gp_add(13, "Send", gp_cn ? BP_OK : BP_EARG);
        ovl_gp_add(27, "Close", BP_OK);
        break;
    case GK_RULES:
    case GK_KEYS:
        ovl_gp_add('b', "Prev", BP_OK);
        ovl_gp_add('n', "Next", BP_OK);
        ovl_gp_add(27, "Back", BP_OK);
        break;
    default:
        ovl_gp_add(27, "Back", BP_OK);
        break;
    }
    if (gp_foc >= gp_nb) gp_foc = 0;
}

/* ---- the handoff and the per-state bookkeeping ---------------------------- */
static void ovl_gp_hpoll(void)
{
    static BPByte cur[16];
    int i;
    if (!gp_hmapok) return;
    if (os88_key_map(cur) != 0) { gp_hmapok = 0; return; }
    for (i = 0; i < 16; i++) gp_hmap[i] &= cur[i];
}
static void ovl_gp_track(void)
{
    int d;
#ifdef BP_PANELS_DEMO
    if (!gp_demo) { gp_demo = 1; ovl_gp_demoset(0); }
#endif
    if (bp_game.match_id.lo != gp_mid) {
        gp_mid = bp_game.match_id.lo;
        gp_ack = BP_NOBODY; gp_lastd = BP_NOBODY; gp_hand = 0; gp_over = 0; gp_conf = 0;
        gp_review = 0; gp_auto = 0; gp_oc = 0; gp_err = 0;
        gp_trev = (BPWord)(bp_game.rev.lo ^ 1);
    }
    if (bp_game.status != BP_ST_PLAY) {
        if (bp_game.status == BP_ST_EMPTY) { gp_ack = BP_NOBODY; gp_hand = 0; }
        return;
    }
    d = bp_game.decider;
    if (bp_game.rev.lo != gp_trev || bp_game.rev.hi != gp_trevh) {
        gp_trev = bp_game.rev.lo; gp_trevh = bp_game.rev.hi;
        gp_err = 0; gp_conf = 0;
        if (bp_game.phase == BP_PH_AUCTION) ovl_gp_bidinit(); else gp_auto = 0;
    }
    if (d != gp_lastd) {
        gp_lastd = (BPByte)d;
        if (gp_over == BP_GP_MANAGE || gp_over == BP_GP_TRADE) gp_over = 0;
    }
    if (!gp_hand && d < BP_SEATS && d != gp_ack && bp_sess_mine(d)) {
        gp_ack = (BPByte)d;
        if (ovl_gp_nlocal() >= 2) {
            gp_hand = (BPByte)(d + 1);
            gp_hlast = gp_hsince = os88_ticks();
            gp_hmapok = 1;
            os88_memset(gp_hmap, 0, sizeof(gp_hmap));
            if (os88_key_map(gp_hmap) != 0) gp_hmapok = 0;
        }
    }
}
/* a fresh key press, not a held one: the key map says it was released since
 * the panel appeared; without the map (kern_small) a long quiet gap says so */
static int ovl_gp_hkey(int a, int sc)
{
    unsigned now;
    int fresh;
    now = os88_ticks();
    ovl_gp_hpoll();
    fresh = !(gp_hmapok && OS88_KEYMAP_HELD(gp_hmap, sc));
    if (!fresh && now - gp_hlast >= 14) fresh = 1;
    if (!gp_hmapok && now - gp_hlast < 8) fresh = 0;
    if (now - gp_hsince < 6) fresh = 0;
    gp_hlast = now;
    if (fresh && (a == 13 || a == ' ' || sc == GS_ENT)) gp_hand = 0;
    return 1;
}

/* ---- geometry and drawing helpers ------------------------------------------ */
static void ovl_gp_geom(void)
{
    gp_font = bp_layout == BP_LAY_FULL ? BP_F_TEXT13 : bp_layout == BP_LAY_MID ? BP_F_TEXT10 : BP_F_TEXT08;
    gp_fh = bp_w_fonth(gp_font);
    if (gp_fh < 8) gp_fh = 8;
    gp_pitch = gp_fh + (bp_layout == BP_LAY_COMPACT ? 1 : 2);
    gp_bh = gp_fh + (bp_layout == BP_LAY_COMPACT ? 4 : 6);
    gp_tb = gp_fh + (bp_layout == BP_LAY_COMPACT ? 5 : 6);
    gp_cw = bp_w_textw("0123456789", gp_font) / 10;
    if (gp_cw < 4) gp_cw = 4;
    gp_r.x = bp_r_center.x + 2; gp_r.y = bp_r_center.y + 2;
    gp_r.w = bp_r_center.w - 4; gp_r.h = bp_r_center.h - 4;
}
/* a status line is reserved only when there is something to say */
static int ovl_gp_needstatus(void)
{
    if (gp_err || bp_sess_busy()) return 1;
    return gp_kind >= GK_IDLE && gp_kind <= GK_TRADE && !ovl_gp_mine();
}
static void ovl_gp_frame(const char *title)
{
    bp_w_panel(&gp_r, title);
    gp_x = gp_r.x + 6; gp_xr = gp_r.x + gp_r.w - 6;
    gp_y = gp_r.y + gp_tb + 2;
    gp_btop = gp_r.y + gp_r.h - gp_bh - 3 - (ovl_gp_needstatus() ? gp_pitch : 0);
}
static void ovl_gp_t(const char *s, int col)
{
    if (gp_y + gp_fh <= gp_btop) bp_w_text(gp_x, gp_y, s, col, gp_font);
    gp_y += gp_pitch;
}
static void ovl_gp_tc(const char *s, int col)
{
    int w, x;
    w = bp_w_textw(s, gp_font);
    x = gp_r.x + (gp_r.w - w) / 2;
    if (gp_y + gp_fh <= gp_btop) bp_w_text(x, gp_y, s, col, gp_font);
    gp_y += gp_pitch;
}
/* the line builder's text, in black */
static void ovl_gp_tl(void) { ovl_gp_t(gp_line, 0); }
/* word-wrapped, at the average glyph width plus a tenth for the capitals */
static void ovl_gp_para(const char *s, int col)
{
    static char b[52];
    int cpl, n, last;
    cpl = ((gp_xr - gp_x) * 10) / (gp_cw * 11);
    if (cpl > 50) cpl = 50;
    if (cpl < 8) cpl = 8;
    while (*s == ' ') s++;
    while (*s) {
        n = 0; last = -1;
        while (s[n] && n < cpl) { if (s[n] == ' ') last = n; n++; }
        if (s[n] && last > 0) n = last;
        os88_memcpy(b, s, n); b[n] = 0;
        ovl_gp_t(b, col);
        s += n;
        while (*s == ' ') s++;
    }
}
static void ovl_gp_status(void)
{
    const char *m;
    m = 0;
    if (gp_err) { ovl_gp_lb(); ovl_gp_la("! "); ovl_gp_la(gp_err); m = gp_line; }
    else if (bp_sess_busy()) m = "Sent - waiting for the other PC to confirm";
    else if (gp_kind >= GK_IDLE && gp_kind <= GK_TRADE && !ovl_gp_mine()) {
        ovl_gp_lb(); ovl_gp_la("Waiting for "); ovl_gp_la(ovl_gp_name(bp_game.decider));
        ovl_gp_la(" on the other PC"); m = gp_line;
    }
    if (!m) return;
    bp_w_text(gp_x, gp_btop, m, 0, gp_font);
}
/* the button row, bottom of the panel; records the mouse rectangles */
static void ovl_gp_brow(void)
{
    static char lab[10];
    static int bw[GP_MAXB];
    int i, sum, gap, x, y, bare;
    if (!gp_nb) { gp_drawn = gp_kind; return; }
    bare = 0;
    for (;;) {
        sum = 0;
        for (i = 0; i < gp_nb; i++) {
            bw[i] = bp_w_textw(gp_blab[i], gp_font) + 8 + (bp_mono ? gp_cw : 0);
            if (!bare) { ovl_gp_kn(lab, gp_bkey[i]); bw[i] += bp_w_textw(lab, gp_font) + 3 * gp_cw; }
            sum += bw[i];
        }
        if (bare || sum + 2 * (gp_nb + 1) + 8 <= gp_r.w) break;
        bare = 1;                   /* too wide: labels alone; the hint bar names the keys */
    }
    gap = (gp_r.w - 8 - sum) / (gp_nb + 1);
    if (gap < 2) gap = 2;
    x = gp_r.x + 4 + gap;
    y = gp_r.y + gp_r.h - gp_bh - 4;
    for (i = 0; i < gp_nb; i++) {
        gp_br[i].x = x; gp_br[i].y = y; gp_br[i].w = bw[i]; gp_br[i].h = gp_bh;
        gp_line[0] = 0;
        if (!bare) {
            lab[0] = '[';
            ovl_gp_kn(lab + 1, gp_bkey[i]);
            ovl_gp_la(lab); ovl_gp_la("] ");
        }
        if (bp_mono && i == gp_foc) ovl_gp_la(">");
        ovl_gp_la(gp_blab[i]);
        bp_w_button(&gp_br[i], gp_line,
                    (gp_bwhy[i] ? BP_W_DISABLED : 0) | (i == gp_foc ? BP_W_FOCUS : 0));
        x += bw[i] + gap;
    }
    gp_drawn = gp_kind;
}

/* ---- the panels ---------------------------------------------------------------- */
static const char *gp_lstate[] = {"off", "searching", "peer found", "bad peer", "lobby",
                                  "in sync", "PAUSED", "resyncing"};
static const char *gp_phn[] = {"", "move", "release", "buy", "bid", "card", "debt", "trade", "end turn", "over"};

static int ovl_gp_sprite(int art)
{
    int w;
    w = bp_w_sprite(gp_x, gp_y, art);
    return w;
}
static void ovl_gp_dbuy(void)
{
    const BPSpace *sp;
    int seat, sw;
    seat = bp_game.active;
    sp = bp_space(bp_game.pos[seat]);
    ovl_gp_lb(); ovl_gp_la("Buy "); ovl_gp_la(sp->name); ovl_gp_la("?");
    ovl_gp_frame(gp_line);
    sw = ovl_gp_sprite(BP_A_LARGE(sp->prop));
    if (sw) gp_x += sw + 8;
    ovl_gp_lb(); ovl_gp_la("Price: "); bp_u32_set(&gp_u1, sp->price, 0); ovl_gp_lm(&gp_u1); ovl_gp_tl();
    bp_u32_copy(&gp_u2, &bp_game.cash[seat]);
    ovl_gp_lb();
    if (bp_u32_subw(&gp_u2, sp->price) == BP_OK) { ovl_gp_la("Cash after: "); ovl_gp_lm(&gp_u2); }
    else { ovl_gp_la("Cash: "); ovl_gp_lm(&gp_u2); ovl_gp_la(" - too little"); }
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("Base rent: "); bp_u32_set(&gp_u1, sp->rent, 0); ovl_gp_lm(&gp_u1);
    if (sp->kind == BP_K_UTILITY) { ovl_gp_lb(); ovl_gp_la("Utility: rent follows the dice"); }
    ovl_gp_tl();
    if (sp->kind == BP_K_COMPUTER) {
        ovl_gp_lb(); ovl_gp_la("Set: "); ovl_gp_la(bp_group_name(sp->group));
        ovl_gp_la(" ("); ovl_gp_lc(bp_group_letter(sp->group)); ovl_gp_la(")"); ovl_gp_tl();
    }
    ovl_gp_para("Decline puts it up for auction.", 0);
}
static void ovl_gp_dauc(void)
{
    const BPSpace *sp;
    int sw, bidder, top, w, bx, by, cmp;
    cmp = bp_layout == BP_LAY_COMPACT;
    sp = bp_space(bp_game.auc_space);
    bidder = bp_game.decider;
    ovl_gp_lb(); ovl_gp_la("Auction: "); ovl_gp_la(sp->name);
    ovl_gp_frame(gp_line);
    top = gp_y;
    sw = 0;
    if (!cmp) { sw = ovl_gp_sprite(BP_A_LARGE(sp->prop)); gp_x += sw + 8; }
    bp_u32_set(&gp_u1, sp->price, 0);
    ovl_gp_lb();
    if (cmp) {
        ovl_gp_la("High: ");
        if (bp_game.auc_high == BP_NOBODY) ovl_gp_la("none"); else { ovl_gp_lv(&bp_game.auc_bid); ovl_gp_la(" "); ovl_gp_la(ovl_gp_name(bp_game.auc_high)); }
        ovl_gp_la("  List "); ovl_gp_lv(&gp_u1);
        ovl_gp_tl();
    } else {
        ovl_gp_la("List price: "); ovl_gp_lm(&gp_u1); ovl_gp_tl();
        ovl_gp_lb(); ovl_gp_la("High bid: ");
        if (bp_game.auc_high == BP_NOBODY) ovl_gp_la("none yet"); else ovl_gp_lm(&bp_game.auc_bid);
        ovl_gp_tl();
        if (bp_game.auc_high != BP_NOBODY) {
            ovl_gp_lb(); ovl_gp_la("by "); ovl_gp_la(ovl_gp_name(bp_game.auc_high)); ovl_gp_tl();
        }
    }
    if (gp_auto) {
        ovl_gp_lb();
        for (w = 0; w < BP_SEATS; w++)
            if ((gp_auto >> w) & 1) { ovl_gp_la(ovl_gp_name(w)); ovl_gp_la(" "); }
        ovl_gp_la("passed: no raise left");
        ovl_gp_tl();
    }
    gp_x = gp_r.x + 6;
    if (!cmp && gp_y < top + 42) gp_y = top + 42;
    gp_y += 1;
    /* whose bid */
    if (gp_y + gp_fh <= gp_btop) {
        bp_w_fill(gp_x, gp_y - 1, gp_xr, gp_y + gp_fh, 14);
        ovl_gp_lb(); ovl_gp_la(ovl_gp_name(bidder)); ovl_gp_la(" - your bid");
        bp_w_text(gp_x + 3, gp_y, gp_line, 0, gp_font);
        ovl_gp_lb(); ovl_gp_la("cash "); ovl_gp_lm(&bp_game.cash[bidder]);
        bp_w_text(gp_xr - 3 - bp_w_textw(gp_line, gp_font), gp_y, gp_line, 0, gp_font);
    }
    gp_y += gp_pitch + 1;
    if (!ovl_gp_mine()) return;
    /* the editor: [-] amount [+] */
    bx = gp_bh;
    by = gp_y;
    gp_xm.w = 0; gp_xp.w = 0;
    if (by + bx <= gp_btop + gp_pitch) {
        w = gp_xr - gp_x - 2 * (bx + 6);
        gp_xm.x = gp_x; gp_xm.y = by; gp_xm.w = bx; gp_xm.h = bx;
        gp_xp.x = gp_xr - bx; gp_xp.y = by; gp_xp.w = bx; gp_xp.h = bx;
        bp_w_button(&gp_xm, "-", 0);
        bp_w_button(&gp_xp, "+", 0);
        gp_lr[0].x = gp_x + bx + 6; gp_lr[0].y = by; gp_lr[0].w = w; gp_lr[0].h = bx;
        bp_w_box(&gp_lr[0], 15, 0);
        ovl_gp_lb(); ovl_gp_lm(&gp_bid);
        bp_w_text(gp_lr[0].x + (w - bp_w_textw(gp_line, gp_font)) / 2, by + (bx - gp_fh) / 2, gp_line, 0, gp_font);
        gp_y = by + bx + 1;
    }
    ovl_gp_minbid(&gp_u1);
    ovl_gp_lb(); ovl_gp_la("Min "); ovl_gp_lv(&gp_u1); if (!cmp) ovl_gp_la(", steps of 10");
    bp_u32_copy(&gp_u2, &bp_game.cash[bidder]);
    if (bp_u32_sub(&gp_u2, &gp_bid) == BP_OK) { ovl_gp_la(cmp ? ". Left " : ". Win: "); ovl_gp_lv(&gp_u2); ovl_gp_la(cmp ? "" : " cr left"); }
    ovl_gp_tl();
}
static void ovl_gp_dcard(void)
{
    const BPCard *cd;
    int sw;
    cd = bp_card(bp_game.card);
    if (!cd) { ovl_gp_frame("Card"); return; }
    ovl_gp_frame(cd->deck ? "System Error" : "Floppy Find");
    if (bp_layout == BP_LAY_FULL) {
        sw = ovl_gp_sprite(cd->deck ? BP_A_CARD_SE : BP_A_CARD_FF);
        if (sw && sw < 100) gp_x += sw + 8;
    }
    ovl_gp_t(cd->title, 0);
    ovl_gp_para(cd->text, 0);
}
static void ovl_gp_drepair(void)
{
    int d;
    d = bp_game.decider;
    ovl_gp_lb(); ovl_gp_la(ovl_gp_name(d)); ovl_gp_la(" - Repair Shop");
    ovl_gp_frame(gp_line);
    ovl_gp_lb(); ovl_gp_la("Detained. Failed tries: "); ovl_gp_lu(bp_game.tries[d]); ovl_gp_la(" of 3");
    ovl_gp_tl();
    ovl_gp_para("Pay 50 cr, use a Recovery Disk, or roll for doubles.", 0);
    ovl_gp_lb(); ovl_gp_la("Cash: "); ovl_gp_lm(&bp_game.cash[d]); ovl_gp_tl();
    if (bp_game.disks[d]) {
        ovl_gp_lb(); ovl_gp_la("Disks held: ");
        if (bp_game.disks[d] & 1) ovl_gp_la("Floppy Find ");
        if (bp_game.disks[d] & 2) ovl_gp_la("System Error");
        ovl_gp_tl();
    }
    ovl_gp_para("You still collect rent, bid, trade and manage.", 0);
}
static void ovl_gp_ddebt(void)
{
    int d;
    d = bp_game.decider;
    ovl_gp_lb(); ovl_gp_la(ovl_gp_name(d)); ovl_gp_la(" - debt");
    ovl_gp_frame(gp_line);
    ovl_gp_lb(); ovl_gp_la("You owe "); ovl_gp_la(ovl_gp_name(bp_game.debt_to)); ovl_gp_la(": ");
    ovl_gp_lm(&bp_game.debt_amount); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("Your cash: "); ovl_gp_lm(&bp_game.cash[d]); ovl_gp_tl();
    bp_u32_copy(&gp_u1, &bp_game.debt_amount);
    if (bp_u32_sub(&gp_u1, &bp_game.cash[d]) == BP_OK && !bp_u32_iszero(&gp_u1)) {
        ovl_gp_lb(); ovl_gp_la("Short by: "); ovl_gp_lm(&gp_u1); ovl_gp_tl();
        ovl_gp_para("Sell upgrades or mortgage to raise cash.", 0);
    } else ovl_gp_t("You can pay now: Settle.", 0);
    if (gp_conf) {
        ovl_gp_t("Declare bankruptcy? You leave the game", 0);
        ovl_gp_lb(); ovl_gp_la("and your estate goes to "); ovl_gp_la(ovl_gp_name(bp_game.debt_to)); ovl_gp_tl();
    }
}
/* names of a prop mask (and a disk byte's side) into gp_line */
static void ovl_gp_names(const BPU32 *m, int disks)
{
    int p, first;
    first = 1;
    for (p = 0; p < BP_PROPS; p++)
        if (ovl_gp_bit(m, p)) {
            if (!first) ovl_gp_la(", ");
            first = 0;
            ovl_gp_la(ovl_gp_pname(p));
            if (bp_game.mortgaged[p]) ovl_gp_la("*");
        }
    if (disks & 1) { if (!first) ovl_gp_la(", "); first = 0; ovl_gp_la("Disk FF"); }
    if (disks & 2) { if (!first) ovl_gp_la(", "); first = 0; ovl_gp_la("Disk SE"); }
    if (first) ovl_gp_la("nothing");
}
/* cash after a payment: *out; BP_EFUNDS when the payer cannot */
static int ovl_gp_after(int seat, int pays, const BPU32 *amt, BPU32 *out)
{
    bp_u32_copy(out, &bp_game.cash[seat]);
    if (pays) return bp_u32_sub(out, amt);
    return bp_u32_add(out, amt);
}
static void ovl_gp_aftline(int from, int to, const BPU32 *amt, int payer)
{
    ovl_gp_lb();
    ovl_gp_la(ovl_gp_name(from)); ovl_gp_la(" ");
    ovl_gp_lv(&bp_game.cash[from]); ovl_gp_la(">");
    if (ovl_gp_after(from, payer == 0, amt, &gp_u1) == BP_OK) ovl_gp_lv(&gp_u1); else ovl_gp_la("short");
    ovl_gp_la("   "); ovl_gp_la(ovl_gp_name(to)); ovl_gp_la(" ");
    ovl_gp_lv(&bp_game.cash[to]); ovl_gp_la(">");
    if (ovl_gp_after(to, payer != 0, amt, &gp_u1) == BP_OK) ovl_gp_lv(&gp_u1); else ovl_gp_la("short");
    ovl_gp_tl();
}
static void ovl_gp_dtrade(void)
{
    int f, t;
    f = bp_game.offer_from; t = bp_game.offer_to;
    ovl_gp_lb(); ovl_gp_la("Trade offer: "); ovl_gp_la(ovl_gp_name(f)); ovl_gp_la(" to "); ovl_gp_la(ovl_gp_name(t));
    ovl_gp_frame(gp_line);
    ovl_gp_lb(); ovl_gp_la(ovl_gp_name(f)); ovl_gp_la(" gives: ");
    ovl_gp_names(&bp_game.offer_give, bp_game.offer_disks & 15);
    ovl_gp_para(gp_line, 0);
    ovl_gp_lb(); ovl_gp_la(ovl_gp_name(t)); ovl_gp_la(" gives: ");
    ovl_gp_names(&bp_game.offer_get, (bp_game.offer_disks >> 4) & 15);
    ovl_gp_para(gp_line, 0);
    if (bp_u32_iszero(&bp_game.offer_cash)) ovl_gp_t("No cash changes hands.", 0);
    else {
        ovl_gp_lb(); ovl_gp_la("PLUS "); ovl_gp_lm(&bp_game.offer_cash); ovl_gp_la(" from ");
        ovl_gp_la(ovl_gp_name(bp_game.offer_payer ? t : f)); ovl_gp_tl();
    }
    if (bp_layout != BP_LAY_COMPACT) ovl_gp_t("After this trade (cash):", 0);
    ovl_gp_aftline(f, t, &bp_game.offer_cash, bp_game.offer_payer);
    ovl_gp_lb(); ovl_gp_la("Offer #"); ovl_gp_lu(bp_game.offer_id); ovl_gp_la(" locked. * = mortgaged. ");
    ovl_gp_la(ovl_gp_name(t)); ovl_gp_la(" decides.");
    ovl_gp_para(gp_line, 0);
}
/* standings: cash + assets = score; the winners marked when the match is over */
static void ovl_gp_dstand(int over)
{
    static char nb[8];
    int s, w, c1, c2, c3, n;
    ovl_gp_frame(over ? "Game over" : "Standings");
    if (over) {
        n = 0;
        for (s = 0; s < BP_SEATS; s++) n += (bp_game.winners >> s) & 1;
        ovl_gp_lb(); ovl_gp_la(n > 1 ? "Shared win: " : "Winner: ");
        for (s = 0, n = 0; s < BP_SEATS; s++)
            if ((bp_game.winners >> s) & 1) {
                if (n) ovl_gp_la(", ");
                ovl_gp_la(ovl_gp_name(s)); n++;
            }
        ovl_gp_tl();
    }
    if (bp_game.rules == BP_RULES_QUICK && bp_layout != BP_LAY_COMPACT) {
        ovl_gp_lb(); ovl_gp_la("Quick Match: round "); ovl_gp_lu(bp_game.round); ovl_gp_la(" of ");
        ovl_gp_lu(bp_game.rounds_limit); ovl_gp_tl();
    }
    w = gp_xr - gp_x;
    c1 = gp_x + w * 36 / 100; c2 = gp_x + w * 58 / 100; c3 = gp_x + w * 80 / 100;
    if (gp_y + gp_fh <= gp_btop) {
        bp_w_text(c1, gp_y, "cash", 0, gp_font);
        bp_w_text(c2, gp_y, "assets", 0, gp_font);
        bp_w_text(c3, gp_y, "score", 0, gp_font);
    }
    gp_y += gp_pitch;
    for (s = 0; s < bp_game.nseats; s++) {
        if (gp_y + gp_fh <= gp_btop) {
            if (over && ((bp_game.winners >> s) & 1)) bp_w_fill(gp_x - 2, gp_y - 1, gp_xr, gp_y + gp_fh, 14);
            ovl_gp_lb(); ovl_gp_la(ovl_gp_name(s)); if (!bp_game.alive[s]) ovl_gp_la(" (out)");
            bp_w_text(gp_x, gp_y, gp_line, 0, gp_font);
            if (over) bp_u32_copy(&gp_u1, &bp_game.score[s]); else bp_score_of(&bp_game, s, &gp_u1);
            bp_u32_copy(&gp_u2, &gp_u1);
            if (bp_u32_sub(&gp_u2, &bp_game.cash[s]) != BP_OK) bp_u32_set(&gp_u2, 0, 0);
            ovl_gp_lb(); ovl_gp_lv(&bp_game.cash[s]); bp_w_text(c1, gp_y, gp_line, 0, gp_font);
            ovl_gp_lb(); ovl_gp_lv(&gp_u2); bp_w_text(c2, gp_y, gp_line, 0, gp_font);
            ovl_gp_lb(); ovl_gp_lv(&gp_u1); bp_w_text(c3, gp_y, gp_line, 0, gp_font);
        }
        gp_y += gp_pitch;
    }
    (void)nb;
    if (over && bp_layout != BP_LAY_COMPACT) ovl_gp_para("Score = cash + property - mortgages + upgrades.", 0);
}
static void ovl_gp_dhand(void)
{
    int s, w, x;
    s = gp_hand - 1;
    ovl_gp_frame("Pass the keyboard");
    gp_y += gp_pitch / 2;
    w = bp_w_sprite(gp_r.x + 10, gp_y, BP_A_TOKEN(bp_game.token[s]));
    (void)w;
    ovl_gp_tc("Pass to", 0);
    ovl_gp_lb(); ovl_gp_la(ovl_gp_name(s));
    x = bp_w_textw(gp_line, BP_F_BIG16);
    if (bp_layout != BP_LAY_COMPACT && gp_y + bp_w_fonth(BP_F_BIG16) <= gp_btop) {
        bp_w_text(gp_r.x + (gp_r.w - x) / 2, gp_y, gp_line, 0, BP_F_BIG16);
        gp_y += bp_w_fonth(BP_F_BIG16) + 4;
    } else ovl_gp_tc(gp_line, 0);
    ovl_gp_lb(); ovl_gp_la("Next: "); ovl_gp_la(gp_phn[bp_game.phase]); ovl_gp_tc(gp_line, 0);
    ovl_gp_tc("Press Enter when ready. A held key", 0);
    ovl_gp_tc("does not count - let go first.", 0);
}
static void ovl_gp_dpause(void)
{
    ovl_gp_frame(bp_link_state() == BP_L_RESYNC ? "RESYNCING" : "CABLE LINK LOST");
    ovl_gp_tc("Game paused", 0);
    ovl_gp_lb(); ovl_gp_la("Waiting for the other PC: "); ovl_gp_la(bp_link_msg());
    ovl_gp_para(gp_line, 0);
    ovl_gp_t("LAST ACTION RETAINED:", 0);
    ovl_gp_para(bp_log_line(0), 0);
    ovl_gp_t("Reconnect to continue this turn.", 0);
    if (gp_conf) ovl_gp_t("Leaving abandons this match. Sure?", 0);
}
static void ovl_gp_ditem(int p, char *dst)
{
    int l;
    l = bp_game.level[p];
    os88_strcpy(dst, ovl_gp_pname(p), 8);
    ovl_gp_lb();
    if (l == BP_MAX_LEVEL) ovl_gp_la(" Workshop"); else if (l) { ovl_gp_la(" RAM "); ovl_gp_lu(l); }
    if (bp_game.mortgaged[p]) ovl_gp_la(" MORT");
    os88_strcpy(dst + os88_strlen(dst), gp_line, 16);
}
static void ovl_gp_dmanage(void)
{
    static char row[24];
    int i, n, top, y, p, sp, seat;
    seat = ovl_gp_mseat();
    ovl_gp_lb(); ovl_gp_la("Collection - "); ovl_gp_la(ovl_gp_name(seat));
    ovl_gp_frame(gp_line);
    n = gp_mn;
    ovl_gp_lb(); ovl_gp_la("Cash "); ovl_gp_lm(&bp_game.cash[seat]); ovl_gp_tl();
    if (!n) { ovl_gp_t("You own no computers yet.", 0); return; }
    /* list rows: what is left after 3 detail lines */
    i = (gp_btop - gp_y) / gp_pitch - 2;
    if (i < 2) i = 2;
    if (i > n) i = n;
    top = gp_mc - i + 1;
    if (top < 0) top = 0;
    gp_lr[0].x = gp_x; gp_lr[0].y = gp_y; gp_lr[0].w = gp_xr - gp_x; gp_lr[0].h = i * gp_pitch;
    gp_lrow = gp_pitch;
    gp_ttop[0] = (BPByte)top;
    y = gp_y;
    for (n = top; n < top + i; n++) {
        p = gp_ml[n];
        if (n == gp_mc) bp_w_fill(gp_x - 2, y - 1, gp_xr, y + gp_fh, 14);
        ovl_gp_ditem(p, row);
        ovl_gp_lb(); ovl_gp_la(n == gp_mc ? "> " : "  "); ovl_gp_la(row);
        bp_w_text(gp_x, y, gp_line, 0, gp_font);
        ovl_gp_lb(); ovl_gp_la("[");
        ovl_gp_lc(bp_space(bp_prop_space(p))->kind == BP_K_COMPUTER ? bp_group_letter(bp_space(bp_prop_space(p))->group) : 'U');
        ovl_gp_la("]");
        bp_w_text(gp_xr - 4 * gp_cw, y, gp_line, 0, gp_font);
        y += gp_pitch;
    }
    gp_y = y;
    p = gp_ml[gp_mc]; sp = bp_prop_space(p);
    ovl_gp_lb();
    if (bp_space(sp)->kind != BP_K_COMPUTER) ovl_gp_la("Utilities are not upgraded");
    else {
        ovl_gp_la("Upgrade -"); ovl_gp_lu(bp_group_upgrade_cost(bp_space(sp)->group));
        ovl_gp_la("  Sell +"); ovl_gp_lu((unsigned)(bp_group_upgrade_cost(bp_space(sp)->group) / 2));
    }
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("Mortgage +"); ovl_gp_lu((unsigned)bp_mortgage_value(sp));
    bp_redeem_cost(sp, &gp_u2);
    ovl_gp_la("  Redeem -"); ovl_gp_lv(&gp_u2);
    ovl_gp_tl();
}
static void ovl_gp_dcompose(void)
{
    static char row[24];
    int c, nl, i, idx, y, hw, x, it, me;
    me = bp_game.decider;
    ovl_gp_lb(); ovl_gp_la("Trade: "); ovl_gp_la(ovl_gp_name(me)); ovl_gp_la(" with "); ovl_gp_la(ovl_gp_name(gp_tto));
    ovl_gp_frame(gp_line);
    ovl_gp_tbuild();
    hw = (gp_xr - gp_x) / 2;
    for (c = 0; c < 2; c++) {
        x = gp_x + c * hw;
        if (c == gp_tcol) bp_w_fill(x, gp_y - 1, x + hw - 4, gp_y + gp_fh, 14);
        ovl_gp_lb(); ovl_gp_la(ovl_gp_name(c ? gp_tto : me)); ovl_gp_la(" gives");
        bp_w_text(x + 2, gp_y, gp_line, 0, gp_font);
    }
    gp_y += gp_pitch;
    nl = (gp_btop - gp_y) / gp_pitch - 2;
    if (nl < 1) nl = 1;
    gp_lrow = gp_pitch;
    for (c = 0; c < 2; c++) {
        x = gp_x + c * hw;
        if (gp_tcur[c] < gp_ttop[c]) gp_ttop[c] = gp_tcur[c];
        if (gp_tcur[c] >= gp_ttop[c] + nl) gp_ttop[c] = (BPByte)(gp_tcur[c] - nl + 1);
        gp_lr[c].x = x; gp_lr[c].y = gp_y; gp_lr[c].w = hw - 4; gp_lr[c].h = nl * gp_pitch;
        y = gp_y;
        if (!gp_in[c] && gp_y + gp_fh <= gp_btop) bp_w_text(x + 2, y, "(none)", 0, gp_font);
        for (i = 0; i < nl; i++) {
            idx = gp_ttop[c] + i;
            if (idx >= gp_in[c]) break;
            it = gp_it[c][idx];
            if (c == gp_tcol && idx == gp_tcur[c]) bp_w_fill(x, y - 1, x + hw - 4, y + gp_fh, 14);
            ovl_gp_lb();
            ovl_gp_la(ovl_gp_tsel(c, idx) ? "[x] " : (it < 32 && !ovl_gp_tradable(it) ? "[-] " : "[ ] "));
            if (it >= 32) ovl_gp_la(it == 32 ? "Disk FF" : "Disk SE");
            else { ovl_gp_la(ovl_gp_pname(it)); if (bp_game.mortgaged[it]) ovl_gp_la("*"); }
            bp_w_text(x + 2, y, gp_line, 0, gp_font);
            y += gp_pitch;
        }
    }
    gp_y += nl * gp_pitch;
    ovl_gp_lb(); ovl_gp_la("Cash "); ovl_gp_lu(gp_tcash); ovl_gp_la(" cr paid by ");
    ovl_gp_la(ovl_gp_name(gp_tpay ? gp_tto : me));
    ovl_gp_tl();
    bp_u32_set(&gp_u2, gp_tcash, 0);
    /* the preview: both cash balances before > after */
    c = bp_game.offer_from; (void)c;
    ovl_gp_lb();
    ovl_gp_la(ovl_gp_name(me)); ovl_gp_la(" "); ovl_gp_lv(&bp_game.cash[me]); ovl_gp_la(">");
    if (ovl_gp_after(me, gp_tpay == 0, &gp_u2, &gp_u1) == BP_OK) ovl_gp_lv(&gp_u1); else ovl_gp_la("short");
    ovl_gp_la("  "); ovl_gp_la(ovl_gp_name(gp_tto)); ovl_gp_la(" "); ovl_gp_lv(&bp_game.cash[gp_tto]); ovl_gp_la(">");
    if (ovl_gp_after(gp_tto, gp_tpay != 0, &gp_u2, &gp_u1) == BP_OK) ovl_gp_lv(&gp_u1); else ovl_gp_la("short");
    ovl_gp_tl();
}
static void ovl_gp_dchat(void)
{
    int i;
    ovl_gp_frame("Chat");
    for (i = 0; i < 3; i++) ovl_gp_t(gp_cl[i], 0);
    ovl_gp_lb(); ovl_gp_la("> "); ovl_gp_la(gp_cbuf); ovl_gp_la("_");
    ovl_gp_t(gp_line, 0);
    ovl_gp_lb(); ovl_gp_lu(gp_cn); ovl_gp_la("/"); ovl_gp_lu(GP_CHATMAX); ovl_gp_la(" characters. Letters type here, not play.");
    ovl_gp_para(gp_line, 0);
}
static void ovl_gp_ddiag(void)
{
    int ls, pt;
    ovl_gp_frame("Link diagnostics");
    ls = bp_link_state();
    pt = bp_ck_port();
    ovl_gp_lb(); ovl_gp_la("Role: "); ovl_gp_la(bp_mode == BP_MODE_HOST ? "banker host" : bp_mode == BP_MODE_CLIENT ? "client" : "same PC");
    ovl_gp_la("   Link: "); ovl_gp_la(gp_lstate[ls & 7]); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("Port: ");
    if (pt) { ovl_gp_la("COM"); ovl_gp_lu(pt); ovl_gp_la(", 2400 baud"); } else ovl_gp_la("none");
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("Revision "); ovl_gp_lv(&bp_game.rev); ovl_gp_la(bp_sess_busy() ? ", pending" : ", idle");
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("CRC "); ovl_gp_lu(bp_diag.crc); ovl_gp_la("  retry "); ovl_gp_lu(bp_diag.retry);
    ovl_gp_la("  frame "); ovl_gp_lu(bp_diag.frame); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("overrun "); ovl_gp_lu(bp_diag.overrun); ovl_gp_la("  resync "); ovl_gp_lu(bp_diag.resync);
    ovl_gp_la("  chat lost "); ovl_gp_lu(bp_diag.chatdrop); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la("rx "); ovl_gp_lu(bp_diag.rx); ovl_gp_la("  tx "); ovl_gp_lu(bp_diag.tx); ovl_gp_tl();
    ovl_gp_para(bp_link_msg(), 0);
}
/* help text: the art record's lines; a "#KEYS" line starts the keys section */
static int ovl_gp_hline(int n, char *buf)
{
    if (!bx_artstr(BP_A_HELP, n, buf)) return 0;
    return 1;
}
static void ovl_gp_dhelp(int keys)
{
    static char buf[52];
    static int kidx;
    int n, lpp, i, base;
    ovl_gp_frame(keys ? "Keys" : "Rules");
    if (!ovl_gp_hline(0, buf)) { ovl_gp_t("The help text is part of the art file.", 0); return; }
    if (!kidx) {
        for (n = 0; ovl_gp_hline(n, buf); n++)
            if (buf[0] == '#' && buf[1] == 'K') { kidx = n + 1; break; }
        if (!kidx) kidx = 30000;
    }
    base = keys ? kidx : 0;
    lpp = (gp_btop - gp_y) / gp_pitch;
    if (lpp < 1) lpp = 1;
    for (i = 0; i < lpp; i++) {
        n = base + gp_help * lpp + i;
        if (!ovl_gp_hline(n, buf) || buf[0] == '#') break;
        ovl_gp_t(buf, 0);
    }
    if (i == 0 && gp_help) gp_help = 0;
}
static void ovl_gp_didle(void)
{
    int d;
    d = bp_game.decider;
    gp_r.h = gp_pitch + gp_bh + 14;
    gp_r.y = bp_r_center.y + bp_r_center.h - gp_r.h - 2;
    bp_w_box(&gp_r, 7, 0);
    gp_x = gp_r.x + 6; gp_xr = gp_r.x + gp_r.w - 6;
    gp_btop = gp_r.y + 4;
    gp_y = gp_btop;
    ovl_gp_lb();
    if (gp_err) { ovl_gp_la("! "); ovl_gp_la(gp_err); }
    else if (bp_sess_busy()) ovl_gp_la("Sent - waiting for the other PC");
    else if (!ovl_gp_mine()) { ovl_gp_la("Waiting for "); ovl_gp_la(ovl_gp_name(d)); ovl_gp_la(" on the other PC"); }
    else {
        ovl_gp_la(ovl_gp_name(d));
        if (bp_game.phase == BP_PH_AFTER) ovl_gp_la(bp_game.doubles ? ": doubles - roll again" : ": end your turn");
        else ovl_gp_la(": roll the dice");
    }
    bp_w_text(gp_x, gp_y, gp_line, 0, gp_font);
}
static void ovl_gp_dreview(void)
{
    gp_r.h = gp_pitch + gp_bh + 14;
    gp_r.y = bp_r_center.y + bp_r_center.h - gp_r.h - 2;
    bp_w_box(&gp_r, 7, 0);
    gp_x = gp_r.x + 6; gp_xr = gp_r.x + gp_r.w - 6;
    gp_btop = gp_r.y + 4;
    bp_w_text(gp_x, gp_btop, "Game over - reviewing the board.", 0, gp_font);
}
/* the whole panel: one call per strip, so it must cost nothing but drawing */
static void ovl_gp_draw(void)
{
    ovl_gp_track();
    ovl_gp_spec();
    if (gp_kind == GK_NONE) return;
    ovl_gp_geom();
    switch (gp_kind) {
    case GK_IDLE: ovl_gp_didle(); break;
    case GK_REVIEW: ovl_gp_dreview(); break;
    case GK_BUY: ovl_gp_dbuy(); break;
    case GK_AUC: ovl_gp_dauc(); break;
    case GK_CARD: ovl_gp_dcard(); break;
    case GK_REPAIR: ovl_gp_drepair(); break;
    case GK_DEBT: ovl_gp_ddebt(); break;
    case GK_TRADE: ovl_gp_dtrade(); break;
    case GK_OVER: ovl_gp_dstand(1); break;
    case GK_STAND: ovl_gp_dstand(0); break;
    case GK_HAND: ovl_gp_dhand(); break;
    case GK_PAUSE: ovl_gp_dpause(); break;
    case GK_MANAGE: ovl_gp_dmanage(); break;
    case GK_COMPOSE: ovl_gp_dcompose(); break;
    case GK_CHAT: ovl_gp_dchat(); break;
    case GK_DIAG: ovl_gp_ddiag(); break;
    case GK_RULES: ovl_gp_dhelp(0); break;
    case GK_KEYS: ovl_gp_dhelp(1); break;
    default: break;
    }
    if (gp_kind != GK_IDLE && gp_kind != GK_REVIEW) ovl_gp_status();
    ovl_gp_brow();
}

/* ---- actions ---------------------------------------------------------------------- */
static BPCmd gp_c;
static void ovl_gp_prep(int type, int arg, int seat)
{
    os88_memset(&gp_c, 0, sizeof(gp_c));
    gp_c.type = (BPByte)type; gp_c.seat = (BPByte)seat; gp_c.arg = (BPByte)arg;
    bp_u32_copy(&gp_c.rev, &bp_game.rev);
}
static int ovl_gp_submit(void)
{
    int e;
    e = bp_sess_submit(&gp_c);
    gp_err = (e == BP_OK || e == BP_EPENDING) ? 0 : bp_err_text(e);
    return e;
}
static int ovl_gp_go(int type, int arg)
{
    ovl_gp_prep(type, arg, bp_game.decider);
    return ovl_gp_submit();
}
static void ovl_gp_openmanage(void) { gp_over = BP_GP_MANAGE; gp_mc = 0; gp_conf = 0; }
static void ovl_gp_chatsend(void)
{
    int e;
    if (!gp_cn) return;
    e = bp_chat_send(gp_cbuf);
    if (e != BP_OK && e != BP_EPENDING) { gp_err = bp_err_text(e); return; }
    bp_gp_chat(bp_here, gp_cbuf);
    gp_cn = 0; gp_cbuf[0] = 0;
}
static int ovl_gp_chatkey(int a, int sc)
{
    if (a == 27) { if (gp_cn) { gp_cn = 0; gp_cbuf[0] = 0; } else gp_over = 0; return 1; }
    if (a == 13) { ovl_gp_chatsend(); return 1; }
    if (a == 8) { if (gp_cn) gp_cbuf[--gp_cn] = 0; return 1; }
    if (a >= 32 && a < 127) {
        if (gp_cn < GP_CHATMAX) { gp_cbuf[gp_cn++] = (char)a; gp_cbuf[gp_cn] = 0; }
        return 1;
    }
    if (a == 0 && sc >= 0x3B && sc <= 0x44) return 0;   /* the F keys still reach the app */
    return 1;
}
/* what a button does; the key that is also its shortcut */
static int ovl_gp_act(int key)
{
    int sp, p;
    switch (gp_kind) {
    case GK_IDLE:
        if (key == ' ') ovl_gp_go(BP_C_ROLL, 0);
        else if (key == 'e') ovl_gp_go(BP_C_END_TURN, 0);
        else if (key == 't') { if (!ovl_gp_can(BP_C_PROPOSE, 0)) ovl_gp_topen(); else gp_err = bp_err_text(ovl_gp_can(BP_C_PROPOSE, 0)); }
        else if (key == 'm') ovl_gp_openmanage();
        return 1;
    case GK_BUY:
        if (key == 'b') ovl_gp_go(BP_C_BUY, 0); else if (key == 'a') ovl_gp_go(BP_C_DECLINE, 0);
        return 1;
    case GK_AUC:
        if (key == 'b') {
            ovl_gp_prep(BP_C_BID, 0, bp_game.decider);
            bp_u32_copy(&gp_c.amount, &gp_bid);
            ovl_gp_submit();
        } else if (key == 'p') ovl_gp_go(BP_C_PASS, 0);
        return 1;
    case GK_CARD: ovl_gp_go(BP_C_ACK_CARD, 0); return 1;
    case GK_REPAIR:
        if (key == 'p') ovl_gp_go(BP_C_PAY_BAIL, 0);
        else if (key == 'f') ovl_gp_go(BP_C_USE_DISK, 0);
        else if (key == 's') ovl_gp_go(BP_C_USE_DISK, 1);
        else if (key == 'r') ovl_gp_go(BP_C_ROLL, 0);
        else if (key == 'm') ovl_gp_openmanage();
        return 1;
    case GK_DEBT:
        if (key == 'm') ovl_gp_openmanage();
        else if (key == 's') ovl_gp_go(BP_C_SETTLE, 0);
        else if (key == 'k') { if (gp_conf) { gp_conf = 0; ovl_gp_go(BP_C_BANKRUPT, 0); } else gp_conf = 1; }
        else if (key == 27) gp_conf = 0;
        return 1;
    case GK_TRADE:
        if (key == 'a') ovl_gp_go(BP_C_ACCEPT, bp_game.offer_id & 255);
        else if (key == 'r') ovl_gp_go(BP_C_REJECT, 0);
        else if (key == 'c') { ovl_gp_prep(BP_C_CANCEL, 0, bp_game.offer_from); ovl_gp_submit(); }
        return 1;
    case GK_OVER:
        if (key == 'r') gp_review = 1; else if (key == 'n') gp_oc = 2; else if (key == 'x') gp_oc = 3;
        return 1;
    case GK_REVIEW: gp_review = 0; return 1;
    case GK_HAND: return 1;
    case GK_PAUSE:
        if (key == 'r') { if (bp_mode == BP_MODE_HOST) bp_link_host(bp_ck_port()); else bp_link_join(bp_ck_port()); }
        else if (key == 'l') { sp = bp_sess_resume_pair(); if (sp != BP_OK && sp != BP_EPENDING) gp_err = bp_err_text(sp); }
        else if (key == 'd') gp_over = BP_GP_DIAG;
        else if (key == 'x') { if (gp_conf) { gp_conf = 0; bp_link_leave(); } else gp_conf = 1; }
        else if (key == 27) gp_conf = 0;
        return 1;
    case GK_MANAGE:
        if (key == 27) { gp_over = 0; return 1; }
        if (!gp_mn) return 1;
        p = gp_ml[gp_mc]; sp = bp_prop_space(p);
        if (key == 'u') ovl_gp_go(BP_C_UPGRADE, sp);
        else if (key == 's') ovl_gp_go(BP_C_SELL, sp);
        else if (key == 'm') ovl_gp_go(BP_C_MORTGAGE, sp);
        else if (key == 'r') ovl_gp_go(BP_C_REDEEM, sp);
        return 1;
    case GK_COMPOSE:
        if (key == 27) gp_over = 0;
        else if (key == 'n') {
            p = gp_tto;
            do p = (p + 1) % bp_game.nseats; while (p == bp_game.decider || !bp_game.alive[p]);
            gp_tto = (BPByte)p; gp_tget.lo = gp_tget.hi = 0; gp_tdk &= 15;
            gp_tcur[1] = gp_ttop[1] = 0;
        } else if (key == 'p') gp_tpay ^= 1;
        else if (key == 13) {
            ovl_gp_prep(BP_C_PROPOSE, 0, bp_game.decider);
            gp_c.to = gp_tto; bp_u32_copy(&gp_c.give, &gp_tgive); bp_u32_copy(&gp_c.get, &gp_tget);
            gp_c.disks = gp_tdk; gp_c.payer = gp_tpay; bp_u32_set(&gp_c.amount, gp_tcash, 0);
            if (ovl_gp_submit() == BP_OK || !gp_err) gp_over = 0;
        }
        return 1;
    case GK_CHAT: if (key == 13) ovl_gp_chatsend(); else gp_over = 0; return 1;
    case GK_RULES:
    case GK_KEYS:
        if (key == 'n') gp_help++; else if (key == 'b') { if (gp_help) gp_help--; } else gp_over = 0;
        return 1;
    default:
        gp_over = 0; return 1;
    }
}
static const char *ovl_gp_whytext(int why)
{
    if (why == BP_EARG && gp_kind == GK_AUC) return "Bid at least the minimum, in steps of 10";
    if (why == BP_EARG && gp_kind == GK_COMPOSE) return "Choose something to trade first";
    if (why == BP_EARG && gp_kind == GK_CHAT) return "Type a message first";
    return bp_err_text(why);
}
static int ovl_gp_press(int i)
{
    gp_foc = (BPByte)i;
    if (gp_bwhy[i]) { gp_err = ovl_gp_whytext(gp_bwhy[i]); return 1; }
    return ovl_gp_act(gp_bkey[i]);
}
/* keys that are not buttons: the editors and the lists */
static int ovl_gp_edit(int a, int nav)
{
    int d, n;
    switch (gp_kind) {
    case GK_AUC:
        if (!ovl_gp_acts()) return 0;
        ovl_gp_minbid(&gp_u1);
        if (a == '+' || a == '=' || nav == GS_UP) { bp_u32_addw(&gp_bid, 10); gp_typed = 0; return 1; }
        if (a == '-' || nav == GS_DN) {
            bp_u32_copy(&gp_u2, &gp_u1); bp_u32_addw(&gp_u2, 10);
            if (bp_u32_cmp(&gp_bid, &gp_u2) >= 0) bp_u32_subw(&gp_bid, 10);
            gp_typed = 0; return 1;
        }
        if (a >= '0' && a <= '9') {
            if (!gp_typed) { bp_u32_set(&gp_bid, 0, 0); gp_typed = 1; }
            if (bp_u32_mul(&gp_u2, &gp_bid, 10) == BP_OK) { bp_u32_copy(&gp_bid, &gp_u2); bp_u32_addw(&gp_bid, a - '0'); }
            return 1;
        }
        if (a == 8) { gp_typed = 1; bp_u32_set(&gp_bid, gp_bid.hi ? 0 : gp_bid.lo / 10, 0); return 1; }
        return 0;
    case GK_MANAGE:
        if (nav == GS_UP || nav == GS_LT) { if (gp_mc) gp_mc--; return 1; }
        if (nav == GS_DN || nav == GS_RT) { if (gp_mc + 1 < gp_mn) gp_mc++; return 1; }
        return 0;
    case GK_COMPOSE:
        ovl_gp_tbuild();
        if (nav == GS_LT || nav == GS_RT) { gp_tcol ^= 1; ovl_gp_tbuild(); return 1; }
        if (nav == GS_UP) { if (gp_tcur[gp_tcol]) gp_tcur[gp_tcol]--; return 1; }
        if (nav == GS_DN) { if (gp_tcur[gp_tcol] + 1 < gp_in[gp_tcol]) gp_tcur[gp_tcol]++; return 1; }
        if (a == ' ') { ovl_gp_ttoggle(gp_tcol, gp_tcur[gp_tcol]); return 1; }
        if (a == '+' || a == '=') { if (gp_tcash < 64000u) gp_tcash += 10; return 1; }
        if (a == '-') { gp_tcash = gp_tcash >= 10 ? gp_tcash - 10 : 0; return 1; }
        if (a >= '0' && a <= '9') { if (gp_tcash < 6000) gp_tcash = gp_tcash * 10 + (a - '0'); return 1; }
        if (a == 8) { gp_tcash /= 10; return 1; }
        return 0;
    case GK_RULES:
    case GK_KEYS:
        if (nav == GS_PGDN || nav == GS_DN) { gp_help++; return 1; }
        if (nav == GS_PGUP || nav == GS_UP) { if (gp_help) gp_help--; return 1; }
        return 0;
    default: break;
    }
    (void)d; (void)n;
    return 0;
}
static int ovl_gp_key(int a, int sc)
{
    int nav, i, k;
    ovl_gp_track();
    ovl_gp_spec();
#ifdef BP_PANELS_DEMO
    if (a == 0 && sc == 0x3F) { ovl_gp_demoset(gp_demo++); return 1; }
#endif
    if (gp_kind == GK_NONE) return 0;
    nav = (a == 0 || a == 0xE0) ? sc : 0;
    if (gp_kind == GK_HAND) return ovl_gp_hkey(a, sc);
    if (gp_kind == GK_CHAT) return ovl_gp_chatkey(a, sc);
    gp_err = 0;
    if (ovl_gp_edit(a, nav)) return 1;
    if (a == 9 || nav == GS_TAB) { gp_foc = (BPByte)((gp_foc + 1) % (gp_nb ? gp_nb : 1)); return 1; }
    if (a == 13 && gp_kind != GK_COMPOSE) { if (gp_nb) return ovl_gp_press(gp_foc); return 0; }
    k = a;
    if (k >= 'A' && k <= 'Z') k += 32;
    if (nav == GS_ESC) k = 27;
    if (k == 'c' && (gp_kind == GK_IDLE || gp_kind == GK_REPAIR || gp_kind == GK_DEBT)) k = 'm';
    for (i = 0; i < gp_nb; i++)
        if (gp_bkey[i] == k) return ovl_gp_press(i);
    if (k == 13 && gp_nb) return ovl_gp_press(0);
    return 0;
}
static int ovl_gp_in(const BPRect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
static int ovl_gp_click(int x, int y)
{
    int i, row;
    ovl_gp_track();
    ovl_gp_spec();
    if (gp_kind == GK_NONE || gp_drawn != gp_kind) return 0;
    for (i = 0; i < gp_nb; i++)
        if (ovl_gp_in(&gp_br[i], x, y)) return ovl_gp_press(i);
    if (gp_kind == GK_AUC && ovl_gp_acts()) {
        if (ovl_gp_in(&gp_xp, x, y)) { bp_u32_addw(&gp_bid, 10); gp_typed = 0; return 1; }
        if (ovl_gp_in(&gp_xm, x, y)) { ovl_gp_edit('-', 0); return 1; }
    }
    if (gp_kind == GK_MANAGE && ovl_gp_in(&gp_lr[0], x, y)) {
        row = gp_ttop[0] + (y - gp_lr[0].y) / gp_lrow;
        if (row < gp_mn) gp_mc = (BPByte)row;
        return 1;
    }
    if (gp_kind == GK_COMPOSE)
        for (i = 0; i < 2; i++)
            if (ovl_gp_in(&gp_lr[i], x, y)) {
                row = gp_ttop[i] + (y - gp_lr[i].y) / gp_lrow;
                gp_tcol = (BPByte)i;
                ovl_gp_tbuild();
                if (row < gp_in[i]) { gp_tcur[i] = (BPByte)row; ovl_gp_ttoggle(i, row); }
                return 1;
            }
    if (gp_kind == GK_IDLE || gp_kind == GK_REVIEW) return 0;
    return ovl_gp_in(&gp_r, x, y);
}
static int ovl_gp_covers(void)
{
    ovl_gp_track();
    ovl_gp_spec();
    return gp_kind != GK_NONE && gp_kind != GK_IDLE && gp_kind != GK_REVIEW;
}
static const char *ovl_gp_hint(int i)
{
    static const char *none[] = {"Arrows Select", "F1 Rules"};
    ovl_gp_track();
    ovl_gp_spec();
    if (gp_kind == GK_NONE) return i >= 0 && i < 2 ? none[i] : 0;
    if (i < 0 || i >= gp_nb) return 0;
    ovl_gp_kn(gp_hb[i], gp_bkey[i]);
    os88_strcpy(gp_hb[i] + os88_strlen(gp_hb[i]), " ", 2);
    os88_strcpy(gp_hb[i] + os88_strlen(gp_hb[i]), gp_blab[i], sizeof(gp_hb[0]) - os88_strlen(gp_hb[i]));
    return gp_hb[i];
}
static void ovl_gp_open(int which)
{
    int why;
    ovl_gp_track();
    gp_err = 0; gp_conf = 0;
    if (which == BP_GP_NONE) { gp_over = 0; return; }
    if (which == BP_GP_MANAGE) { if (bp_game.status == BP_ST_PLAY) ovl_gp_openmanage(); return; }
    if (which == BP_GP_TRADE) {
        if (bp_game.status != BP_ST_PLAY) return;
        why = ovl_gp_can(BP_C_PROPOSE, 0);
        if (why) gp_err = bp_err_text(why); else ovl_gp_topen();
        return;
    }
    gp_over = (BPByte)which;
    gp_help = 0;
    if (which == BP_GP_CHAT) { gp_cn = 0; gp_cbuf[0] = 0; }
}
static void ovl_gp_chat(int machine, const char *text)
{
    int i;
    for (i = 0; i < 2; i++) os88_strcpy(gp_cl[i], gp_cl[i + 1], sizeof(gp_cl[0]));
    os88_strcpy(gp_cl[2], machine == bp_here ? "You: " : "Them: ", sizeof(gp_cl[0]));
    os88_strcpy(gp_cl[2] + os88_strlen(gp_cl[2]), text, sizeof(gp_cl[0]) - os88_strlen(gp_cl[2]));
}

#ifdef BP_PANELS_DEMO
/* hand-made states to look at the panels before the engine lands: F5 steps */
static void ovl_gp_demoset(int n)
{
    int s;
    n = n % 15;
    os88_memset(&bp_game, 0, sizeof(bp_game));
    bp_game.status = BP_ST_PLAY; bp_game.nseats = 3; bp_game.match_id.lo = 77;
    bp_game.rev.lo = (BPWord)(n + 5);
    os88_strcpy(bp_game.name[0], "George", 9); os88_strcpy(bp_game.name[1], "Ada", 9);
    os88_strcpy(bp_game.name[2], "Alan", 9);
    for (s = 0; s < 3; s++) { bp_game.alive[s] = 1; bp_game.token[s] = (BPByte)s; bp_u32_set(&bp_game.cash[s], 1500 - 200 * s, 0); }
    for (s = 0; s < BP_PROPS; s++) bp_game.owner[s] = BP_NOBODY;
    bp_game.owner[0] = 0; bp_game.owner[1] = 0; bp_game.level[0] = 2; bp_game.level[1] = 1;
    bp_game.owner[2] = 1; bp_game.owner[11] = 1; bp_game.mortgaged[11] = 1; bp_game.owner[12] = 1;
    bp_game.owner[14] = 0; bp_game.owner[3] = 1; bp_game.disks[0] = 3;
    bp_game.active = 0; bp_game.decider = 0; bp_game.pos[0] = 4;
    bp_game.phase = BP_PH_MANAGE;
    gp_mid = 77; gp_trev = bp_game.rev.lo; gp_trevh = 0; gp_ack = 0; gp_lastd = 0; gp_hand = 0;
    gp_over = 0; gp_conf = 0; gp_err = 0; gp_auto = 0; gp_review = 0;
    if (n == 0) { bp_game.phase = BP_PH_BUY; bp_game.pos[0] = 4; }
    else if (n == 1) {
        bp_game.phase = BP_PH_AUCTION; bp_game.auc_space = 4; bp_game.auc_high = 2; bp_u32_set(&bp_game.auc_bid, 240, 0);
        bp_game.decider = 1; bp_game.auc_turn = 1; gp_auto = 1; ovl_gp_bidinit();
    }
    else if (n == 2) { bp_game.phase = BP_PH_CARD; bp_game.card = 11; }
    else if (n == 3) { bp_game.phase = BP_PH_REPAIR; bp_game.detained[0] = 1; bp_game.tries[0] = 1; }
    else if (n == 4) {
        bp_game.phase = BP_PH_DEBT; bp_game.debt_to = 2; bp_u32_set(&bp_game.debt_amount, 400, 0);
        bp_u32_set(&bp_game.cash[0], 120, 0);
    }
    else if (n == 5) {
        bp_game.phase = BP_PH_TRADE; bp_game.offer_from = 0; bp_game.offer_to = 1; bp_game.decider = 1; gp_ack = 1; gp_lastd = 1;
        bp_game.offer_give.lo = 1 << 14; bp_game.offer_get.lo = 1 << 2; bp_u32_set(&bp_game.offer_cash, 100, 0);
        bp_game.offer_id = 12; bp_game.offer_get.lo |= 1 << 11;
    }
    else if (n == 6) bp_game.phase = BP_PH_AFTER;
    else if (n == 7) ovl_gp_openmanage();
    else if (n == 8) ovl_gp_topen();
    else if (n == 9) { bp_game.status = BP_ST_OVER; bp_game.phase = BP_PH_OVER; bp_game.winners = 2; bp_game.rules = BP_RULES_QUICK; bp_game.rounds_limit = 20; bp_game.round = 20; }
    else if (n == 10) { gp_hand = 2; gp_hlast = gp_hsince = os88_ticks(); gp_hmapok = 0; bp_game.decider = 1; bp_game.phase = BP_PH_AUCTION; bp_game.auc_space = 4; }
    else if (n == 11) { gp_over = BP_GP_CHAT; os88_strcpy(gp_cl[1], "Them: hi from the other PC", 30); os88_strcpy(gp_cbuf, "good luck", 25); gp_cn = 9; }
    else if (n == 12) gp_over = BP_GP_DIAG;
    else if (n == 13) gp_over = BP_GP_RULES;
    else if (n == 14) gp_over = BP_GP_STANDINGS;
    if (n != 10) gp_ack = gp_lastd = bp_game.decider;
}
#endif

/* ---- the doors: resident, thin -------------------------------------------------------- */
void bp_gp_draw(void) { ovl_gp_draw(); }
int bp_gp_covers(void) { return ovl_gp_covers(); }
int bp_gp_key(int ascii, int scan) { return ovl_gp_key(ascii, scan); }
int bp_gp_click(int x, int y) { return ovl_gp_click(x, y); }
const char *bp_gp_hint(int i) { return ovl_gp_hint(i); }
void bp_gp_sync(void) { ovl_gp_track(); }
void bp_gp_open(int which) { ovl_gp_open(which); }
void bp_gp_chat(int machine, const char *text) { ovl_gp_chat(machine, text); }
void bp_gp_event(const BPEvent *e)
{
    if (e->kind == BP_V_PASS && e->other == 1) gp_auto |= (BPByte)(1 << e->seat);
    else if (e->kind == BP_V_AUCTION) gp_auto = 0;
}
void bp_gp_tick(void) { if (gp_hand && gp_hmapok) ovl_gp_hpoll(); }
int bp_gp_overchoice(void) { int c; c = gp_oc; gp_oc = 0; return c; }
