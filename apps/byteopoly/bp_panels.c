/* Byteopoly - the in-game panels (SPEC.md 131.9.7-131.9.11). Lane: panels.
 *
 * ONE decision, one panel: gp_spec() maps (bp_game, the link, the panel
 * the player opened) to a panel KIND and its row of buttons, and everything
 * else reads that - draw, key, click and the hint bar - so a button, its
 * key, its mouse rectangle and its hint entry cannot disagree. Nothing here
 * writes bp_game: a decision is a BPCmd through bp_sess_submit (131.3.5).
 * Every function but the thin doors is an ovl_* (131.11: eager overlay);
 * only the data is resident. */

#include "art2.h"                       /* BP_A2_* pictures and scenes (panb lane) */
int bx_artstr(int id, int n, char *buf);
#include "art2.h"                       /* the scenes and pictures (131.9.12) */
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
static char gp_hb[GP_MAXB + 1][14];   /* hint entries; between them, the scene slots' pool */
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
static BPByte gp_hand;
/* the quiet clock (131.9.8): restarted when a handoff is raised or drawn,
 * by every key at it, and at the END of every key's or click's handling */
static unsigned gp_hq;
#define GP_HQUIET 20            /* ticks: longer than any typematic delay or period */
/* focus (131.9.7): the identity it was reset for, the revision it was checked at */
static unsigned gp_fid;
static BPWord gp_frev;
/* damage (131.9.7): the topmost line a key can change, from the last draw;
 * the panel's description before and after a key */
static int gp_vy;
#define GP_SN 7
static unsigned gp_s0[GP_SN], gp_s1[GP_SN];
static BPByte gp_hpages;        /* rules/keys pages, from the last draw (0 = not yet) */
static int gp_tlv;              /* link state and busy, last tick */
/* the uplifted panels are kit scenes (131.9.12): the scene the buttons'
 * rects come from (0 = laid out), the button row's top (damage), the slots */
static int gp_scn, gp_brt;
static const char *gp_sv[21];
static int gp_iv[15];
static char *gp_pp;
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
static BPByte gp_dls;               /* a link state to show (15: the paused panel) */
static int ovl_gp_dls(void) { return gp_dls ? gp_dls : bp_link_state(); }
#define bp_link_state ovl_gp_dls
static void ovl_gp_demoset(int n);
#if BP_PANELS_DEMO == 2
static void ovl_gp_demonew(void);
#endif
#if BP_PANELS_DEMO != 2
#define bp_can(s, se, t, a) 0
#endif
#endif

/* ---- text builders ------------------------------------------------------ */
static void ovl_gp_lb(void) { gp_line[0] = 0; }
static void ovl_gp_la(const char *s)
{
    unsigned n;
    n = os88_strlen(gp_line);
    os88_strcpy(gp_line + n, bx_str(s), sizeof(gp_line) - n);
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
static void ovl_gp_lm(const BPU32 *v) { ovl_gp_lv(v); ovl_gp_la(TP_CR); }
static const char *ovl_gp_name(int seat)
{
    if (seat < 0 || seat >= BP_SEATS) return TP_THE_BANK;
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
    os88_strcpy(gp_blab[gp_nb], bx_str(lab), sizeof(gp_blab[0]));
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
    if (!ovl_gp_tradable(it)) { gp_err = TP_UPGRADED_SETS_CANNOT; return; }
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

/* what a panel IS, for the focus and the stale-key rule: its kind, a
 * confirmation, the deciding seat */
static int ovl_gp_ident(void) { return gp_kind | (gp_conf << 5) | ((bp_game.decider & 7) << 6); }
/* the button a panel opens on: No in a confirmation, else the first enabled */
static int ovl_gp_first(void)
{
    int i;
    for (i = 0; i < gp_nb; i++)
        if (gp_conf ? gp_bkey[i] == 27 : !gp_bwhy[i]) return i;
    return 0;
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
        if (bp_mode != BP_MODE_LOCAL && (ls == BP_L_PAUSED || ls == BP_L_RESYNC || ls == 8)) gp_kind = GK_PAUSE;
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
            ovl_gp_add(' ', TP_ROLL, ovl_gp_can(BP_C_ROLL, 0));
            ovl_gp_add('e', TP_END_TURN, ovl_gp_can(BP_C_END_TURN, 0));
            ovl_gp_add('t', TP_TRADE, ovl_gp_can(BP_C_PROPOSE, 0));
        }
        ovl_gp_add('m', TP_MANAGE, BP_OK);
        break;
    case GK_BUY:
        if (ovl_gp_mine()) {
            ovl_gp_add('b', TP_BUY, ovl_gp_can(BP_C_BUY, 0));
            ovl_gp_add('a', TP_AUCTION, ovl_gp_can(BP_C_DECLINE, 0));
        }
        break;
    case GK_AUC:
        if (ovl_gp_mine()) {
            why = ovl_gp_can(BP_C_BID, 0);
            if (!why) why = ovl_gp_bidwhy();
            ovl_gp_lb(); ovl_gp_la(TP_BID); ovl_gp_lv(&gp_bid);
            ovl_gp_add('b', gp_line, why);
            ovl_gp_add('p', TP_PASS, ovl_gp_can(BP_C_PASS, 0));
        }
        break;
    case GK_CARD:
        if (ovl_gp_mine()) ovl_gp_add(13, "OK", ovl_gp_can(BP_C_ACK_CARD, 0));
        break;
    case GK_REPAIR:
        if (ovl_gp_mine()) {
            dk = bp_game.disks[d];
            ovl_gp_add('p', TP_PAY_50, ovl_gp_can(BP_C_PAY_BAIL, 0));
            if (dk & 1) ovl_gp_add('f', TP_DISK_FIND, ovl_gp_can(BP_C_USE_DISK, 0));
            if (dk & 2) ovl_gp_add('s', TP_DISK_ERROR, ovl_gp_can(BP_C_USE_DISK, 1));
            ovl_gp_add('r', TP_ROLL, ovl_gp_can(BP_C_ROLL, 0));
        }
        ovl_gp_add('m', TP_MANAGE, BP_OK);
        break;
    case GK_DEBT:
        if (ovl_gp_mine()) {
            if (gp_conf) {
                ovl_gp_add('k', TP_GO_BANKRUPT, ovl_gp_can(BP_C_BANKRUPT, 0));
                ovl_gp_add(27, "No", BP_OK);
            } else {
                ovl_gp_add('m', TP_MANAGE, BP_OK);
                ovl_gp_add('s', TP_SETTLE, ovl_gp_can(BP_C_SETTLE, 0));
                ovl_gp_add('k', TP_BANKRUPT, ovl_gp_can(BP_C_BANKRUPT, 0));
            }
        } else ovl_gp_add('m', TP_MANAGE, BP_OK);
        break;
    case GK_TRADE:
        if (ovl_gp_mine()) {
            ovl_gp_add('a', TP_ACCEPT, ovl_gp_can(BP_C_ACCEPT, bp_game.offer_id & 255));
            ovl_gp_add('r', TP_REJECT, ovl_gp_can(BP_C_REJECT, 0));
        }
        if (bp_sess_mine(bp_game.offer_from))
            ovl_gp_add('c', TP_CANCEL, bp_sess_busy() ? BP_EPENDING :
                       bp_can(&bp_game, bp_game.offer_from, BP_C_CANCEL, 0));
        break;
    case GK_OVER:
        ovl_gp_add('r', TP_REVIEW, BP_OK);
        ovl_gp_add('n', TP_NEW_GAME, BP_OK);
        ovl_gp_add('x', TP_EXIT, BP_OK);
        break;
    case GK_REVIEW:
        ovl_gp_add('s', TP_STANDINGS, BP_OK);
        break;
    case GK_HAND:
        ovl_gp_add(13, TP_READY, BP_OK);
        break;
    case GK_PAUSE:
        if (gp_conf) {
            ovl_gp_add('x', TP_YES_LEAVE, BP_OK);
            ovl_gp_add(27, "No", BP_OK);
        } else {
            if (ls == 8) { if (bp_mode == BP_MODE_CLIENT) ovl_gp_add('u', TP_USE_HOST, BP_OK); }
            else ovl_gp_add('r', TP_RETRY_LINK, BP_OK);
            if (ls != 8) ovl_gp_add('l', TP_LOAD_PAIR, BP_OK);
            ovl_gp_add('d', TP_DETAILS, BP_OK);
            ovl_gp_add('x', TP_LEAVE, BP_OK);
        }
        break;
    case GK_MANAGE:
        gp_mn = ovl_gp_mlist(gp_ml);
        if (gp_mc >= gp_mn) gp_mc = (BPByte)(gp_mn ? gp_mn - 1 : 0);
        i = gp_mn ? bp_prop_space(gp_ml[gp_mc]) : 0;
        why = gp_mn ? 0 : BP_EOWNER;
        ovl_gp_add('u', TP_UPGRADE, why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_UPGRADE, i)));
        ovl_gp_add('s', TP_SELL, why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_SELL, i)));
        ovl_gp_add('m', TP_MORTGAGE, why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_MORTGAGE, i)));
        ovl_gp_add('r', TP_REDEEM, why ? why : (ovl_gp_mseat() != d ? BP_ESEAT : ovl_gp_can(BP_C_REDEEM, i)));
        ovl_gp_add(27, TP_BACK, BP_OK);
        break;
    case GK_COMPOSE:
        why = (ovl_gp_isempty(&gp_tgive) && ovl_gp_isempty(&gp_tget) && !gp_tdk && !gp_tcash) ? BP_EARG
              : ovl_gp_can(BP_C_PROPOSE, 0);
        ovl_gp_add(13, TP_PROPOSE, why);
        ovl_gp_add('n', TP_NEXT, BP_OK);
        ovl_gp_add('p', TP_PAYER, BP_OK);
        ovl_gp_add(27, TP_CANCEL, BP_OK);
        break;
    case GK_CHAT:
        ovl_gp_add(13, TP_SEND, gp_cn ? BP_OK : BP_EARG);
        ovl_gp_add(27, TP_CLOSE, BP_OK);
        break;
    case GK_RULES:
    case GK_KEYS:
        ovl_gp_add('b', TP_PREV, gp_help ? BP_OK : BP_EARG);
        ovl_gp_add('n', TP_NEXT, gp_hpages && gp_help + 1 >= gp_hpages ? BP_EARG : BP_OK);
        ovl_gp_add(27, TP_BACK, BP_OK);
        break;
    default:
        ovl_gp_add(27, TP_BACK, BP_OK);
        break;
    }
    /* focus never carries between panels; a confirmation opens on No; a
     * commit that greys the focused button moves it on (131.9.7) */
    i = ovl_gp_ident();
    if (i != (int)gp_fid) { gp_fid = (unsigned)i; gp_frev = bp_game.rev.lo; gp_foc = (BPByte)ovl_gp_first(); }
    else if (gp_frev != bp_game.rev.lo || (gp_kind != GK_AUC && gp_kind != GK_MANAGE)) {
        gp_frev = bp_game.rev.lo;   /* never rests on a grey button (Qqa3-2), but for */
        if (gp_foc >= gp_nb || gp_bwhy[gp_foc]) gp_foc = (BPByte)ovl_gp_first();
    }                               /* a typed bid or a list cursor between commits */
    if (gp_foc >= gp_nb) gp_foc = 0;
}

/* ---- the handoff and the per-state bookkeeping ---------------------------- */
static void ovl_gp_track(void)
{
    int d;
#ifdef BP_PANELS_DEMO
    if (!gp_demo) { gp_demo = 1;
#if BP_PANELS_DEMO == 2
        ovl_gp_demonew();
#else
        ovl_gp_demoset(0);
#endif
    }
#endif
    /* a new match, or a Load of an earlier position of this one (the
     * revision went backwards): nothing the panels held still applies */
    if (bp_game.match_id.lo != gp_mid || bp_game.rev.hi < gp_trevh ||
        (bp_game.rev.hi == gp_trevh && bp_game.rev.lo < gp_trev)) {
        gp_mid = bp_game.match_id.lo;
        gp_ack = BP_NOBODY; gp_lastd = BP_NOBODY; gp_hand = 0; gp_over = 0; gp_conf = 0;
        gp_review = 0; gp_auto = 0; gp_oc = 0; gp_err = 0;
        gp_trev = (BPWord)(bp_game.rev.lo ^ 1); gp_trevh = bp_game.rev.hi;
    }
    if (bp_game.rev.lo != gp_trev || bp_game.rev.hi != gp_trevh) {
        gp_trev = bp_game.rev.lo; gp_trevh = bp_game.rev.hi;
        gp_err = 0; gp_conf = 0;
        if (bp_game.phase == BP_PH_AUCTION) ovl_gp_bidinit(); else gp_auto = 0;
    }
    if (bp_game.status != BP_ST_PLAY) {
        if (bp_game.status == BP_ST_EMPTY) { gp_ack = BP_NOBODY; gp_hand = 0; }
        return;
    }
    d = bp_game.decider;
    if (d != gp_lastd) {
        gp_lastd = (BPByte)d;
        if (gp_over == BP_GP_MANAGE || gp_over == BP_GP_TRADE) gp_over = 0;
    }
    /* a handoff follows the decider it was raised for (Qqa1-6) */
    if (gp_hand && gp_hand - 1 != d) { gp_hand = 0; gp_ack = BP_NOBODY; }
    if (!gp_hand && d < BP_SEATS && d != gp_ack && bp_sess_mine(d)) {
        gp_ack = (BPByte)d;
        if (ovl_gp_nlocal() >= 2) {
            gp_hand = (BPByte)(d + 1);
            gp_hq = os88_ticks();
        }
    }
}
/* the handoff takes Enter or Space only after GP_HQUIET ticks with no key
 * event, no end of a key's handling and no draw of the handoff (131.9.8):
 * a held key's repeats and a key queued during the repaint both fail it.
 * The key map is never armed (D8). */
static int ovl_gp_hkey(int a, int sc)
{
    unsigned now;
    now = os88_ticks();
    if (now - gp_hq >= GP_HQUIET && (a == 13 || a == ' ' || sc == GS_ENT)) gp_hand = 0;
    gp_hq = now;
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
    gp_cw = bp_w_textw(TP_0123456789, gp_font) / 10;
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
    gp_vy = gp_r.y;
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
    s = bx_str(s);
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
/* the status line's text, or 0 when it has nothing to say; builds into
 * gp_line, which the caller has emptied */
static const char *ovl_gp_stxt(void)
{
    if (gp_err) { ovl_gp_la("! "); ovl_gp_la(gp_err); return gp_line; }
    if (bp_sess_busy()) return TP_SENT_WAITING_FOR;
    if (gp_kind >= GK_IDLE && gp_kind <= GK_TRADE && !ovl_gp_mine()) {
        ovl_gp_la(TP_WAITING_FOR); ovl_gp_la(ovl_gp_name(bp_game.decider));
        ovl_gp_la(TP_ON_THE_OTHER); return gp_line;
    }
    return 0;
}
static void ovl_gp_srect(int tag, BPRect *r);
/* button i's label into gp_line: "[K] Label", ">" on the focus on 1bpp */
static void ovl_gp_blabel(int i, int bare)
{
    static char lab[10];
    gp_line[0] = 0;
    if (!bare) {
        lab[0] = '[';
        ovl_gp_kn(lab + 1, gp_bkey[i]);
        ovl_gp_la(lab); ovl_gp_la("] ");
    }
    if (bp_mono && i == gp_foc) ovl_gp_la(">");
    ovl_gp_la(gp_blab[i]);
}
/* the button row: label-wide buttons spread evenly along the panel's foot
 * or - on a scene panel (131.9.12) - stretched to fill the row the scene
 * tags #202, 4 px apart; records the mouse rectangles, the very ones drawn, and
 * the row's top (damage) */
static void ovl_gp_brow(void)
{
    static int bw[GP_MAXB];
    BPRect *r;
    int i, sum, gap, x, y, w, h, bare;
    gp_brt = gp_r.y + gp_r.h;
    gp_drawn = gp_kind;
    if (!gp_nb) return;
    r = &gp_br[GP_MAXB - 1];        /* the row: a scene's #202, else the panel's foot */
    if (gp_scn) ovl_gp_srect(202, r);
    else { r->x = gp_r.x + 4; r->w = gp_r.w - 8; r->h = gp_bh; r->y = gp_r.y + gp_r.h - gp_bh - 4; }
    x = r->x; y = r->y; w = r->w; h = r->h;
    for (bare = 0; bare < 2; bare++) {  /* too wide: labels alone; the hint bar names the keys */
        sum = 0;
        for (i = 0; i < gp_nb; i++) {
            ovl_gp_blabel(i, bare);
            bw[i] = bp_w_textw(gp_line, gp_font) + 8;
            sum += bw[i];
        }
        if (sum + 2 * (gp_nb + 1) <= w) break;
    }
    gap = (w - sum) / (gp_nb + 1);
    if (gp_scn) {                   /* a scene's row is filled, as on the boards */
        gap = (gap * (gp_nb + 1) - 4 * (gp_nb - 1)) / gp_nb;
        if (gap > 0) for (i = 0; i < gp_nb; i++) bw[i] += gap;
        gap = 4;
    } else x += gap;
    if (gap < 2) gap = 2;
    gp_brt = y;
    for (i = 0; i < gp_nb; i++) {
        r = &gp_br[i];
        r->x = x; r->y = y; r->w = bw[i]; r->h = h;
        x += bw[i] + gap;
        ovl_gp_blabel(i, bare);
        bp_w_button(r, gp_line, (gp_bwhy[i] ? BP_W_DISABLED : 0) | (i == gp_foc ? BP_W_FOCUS : 0));
    }
}

/* ---- the uplifted panels' slots (131.9.12) ---------------------------------
 * Scene panel n is BP_A2_SC_PANA + 2n on FULL, the next ID on MID, and
 * BP_A2_SC_PANAC + n on COMPACT;
 * its dynamic text arrives through gp_sv (strings, pooled in gp_line after
 * the status line) and gp_iv (numbers). */
/* consecutive tx lines are consecutive handles; the host's are literals */
#ifdef BP_HOST
static const char *const gp_hram[4] = { TP_RAM1, TP_RAM2, TP_RAM3, TP_RAM4 };
static const char *const gp_hpair[4] = { TP_PAIR_NONE, TP_PAIR_BUSY, TP_PAIR_DONE, TP_PAIR_FAIL };
#define GP_RAM(n) gp_hram[(n) - 1]
#define GP_PAIR(p) gp_hpair[p]
#else
#define GP_RAM(n) (TP_RAM1 + (n) - 1)
#define GP_PAIR(p) (TP_PAIR_NONE + (p))
#endif
#define GP_SC(n) (bp_layout == BP_LAY_COMPACT ? BP_A2_SC_PANAC + (n) : \
                  BP_A2_SC_PANA + 2 * (n) + (bp_layout == BP_LAY_MID))
static const BPByte gp_gcol[BP_GROUPS] = {1, 2, 3, 4, 5, 8};    /* the face's collection colours */
static int ovl_gp_gc(const BPSpace *sp)
{
    if (bp_mono) return 0;
    return gp_gcol[sp->kind == BP_K_UTILITY ? BP_GROUPS - 1 : sp->group];
}
/* The POOL the slot strings are built in is the hint entries' buffer: a
 * panel's draw consumes its slots before it returns, and the face asks for
 * each hint entry afresh and uses it at once, so the two never overlap. A
 * string that does not fit is cut, and the pool never overflows. */
#define GP_PEND ((char *)gp_hb + sizeof(gp_hb))
/* the string built at gp_pp becomes slot k; the next starts after it */
static void ovl_gp_pk(int k)
{
    gp_sv[k] = gp_pp;
    gp_pp += os88_strlen(gp_pp) + 1;
    if (gp_pp >= GP_PEND) gp_pp = GP_PEND - 1;
    *gp_pp = 0;
}
static void ovl_gp_pa(const char *s)
{
    int n;
    n = os88_strlen(gp_pp);
    os88_strcpy(gp_pp + n, bx_str(s), GP_PEND - gp_pp - n);
}
/* money, " cr" on it (14 bytes at most), after what the string holds */
static void ovl_gp_pm(int k, const BPU32 *v)
{
    char *d;
    d = gp_pp + os88_strlen(gp_pp);         /* after what is there ("Value: ") */
    if (GP_PEND - d > 14) { bp_u32_fmt(d, v); ovl_gp_pa(TP_CR); }
    ovl_gp_pk(k);
}
static void ovl_gp_pw(int k, unsigned w) { bp_u32_set(&gp_u2, w, 0); ovl_gp_pm(k, &gp_u2); }
/* in capitals: the titles and the banners */
static void ovl_gp_pu(int k, const char *s)
{
    char *d;
    ovl_gp_pa(s);
    for (d = gp_pp; *d; d++) if (*d >= 'a' && *d <= 'z') *d -= 32;
    ovl_gp_pk(k);
}
/* a scene panel begins: every slot empty, %0 = 0 (the placeholders'
 * "never"), $1 the status line and %1 whether it has anything to say */
static void ovl_gp_sbeg(void)
{
    int i;
    const char *m;
    ovl_gp_lb();
    m = ovl_gp_stxt();
    gp_pp = (char *)gp_hb;
    *gp_pp = 0;
    GP_PEND[-1] = 0;                /* the pool's last byte is only ever a terminator */
    for (i = 0; i < 21; i++) gp_sv[i] = GP_PEND - 1;
    for (i = 0; i < 15; i++) gp_iv[i] = 0;
    if (m) { gp_sv[1] = m; gp_iv[1] = 1; }
}
/* a rect the drawn scene tags, on the screen; w 0 when it has none */
static void ovl_gp_srect(int tag, BPRect *r)
{
    if (!gk_scene_rect(gp_scn, tag, (int *)r)) r->w = 0;
    r->x += bp_r_center.x; r->y += bp_r_center.y;
}
/* draw scene n and take the panel's rects from it */
static void ovl_gp_scene(int n)
{
    gp_scn = GP_SC(n);
    gk_scene(gp_scn, bp_r_center.x, bp_r_center.y, gp_sv, gp_iv);
    ovl_gp_srect(200, &gp_r);
    gp_vy = gp_btop = gp_r.y;
    ovl_gp_srect(201, &gp_xm);      /* scratch: the auction sets its own after */
    if (gp_xm.w) gp_btop = gp_xm.y;
}

/* ---- the panels ---------------------------------------------------------------- */
static const char *gp_lstate[] = {TP_OFF, TP_SEARCHING, TP_PEER_FOUND, TP_BAD_PEER, TP_LOBBY,
                                  TP_IN_SYNC, TP_PAUSED, TP_RESYNCING, TP_BOARDS_DIFFER};
static const char *gp_pairs[] = {TP_IDLE, TP_SAVING_OR_RESUMING, TP_COMPLETE, TP_FAILED_OLD_PAIR};
static const char *gp_phn[] = {"", TP_MOVE, TP_RELEASE, TP_BUY_2, TP_BID_2, TP_CARD, TP_DEBT, TP_TRADE_2, TP_END_TURN_2, TP_OVER};

/* the landing (131.9.12): scene 0, on FULL and MID a bottom panel under
 * the centre art */
static void ovl_gp_dbuy(void)
{
    const BPSpace *sp;
    sp = bp_space(bp_game.pos[bp_game.active]);
    ovl_gp_sbeg();
    gp_sv[2] = sp->name;
    gp_iv[2] = BP_A_LARGE(sp->prop);
    gp_iv[3] = ovl_gp_gc(sp);
    ovl_gp_pw(3, sp->price);
    if (sp->kind == BP_K_UTILITY) gp_sv[4] = TP_FOLLOWS_DICE; else ovl_gp_pw(4, sp->rent);
    gp_iv[7] = !gp_iv[1]; gp_iv[8] = gp_iv[1];
    ovl_gp_scene(0);
}
/* the auction (131.9.12): scene 1 */
static void ovl_gp_dauc(void)
{
    const BPSpace *sp;
    int bidder, w, hb;
    sp = bp_space(bp_game.auc_space);
    bidder = bp_game.decider;
    hb = bp_game.auc_high;
    ovl_gp_sbeg();
    ovl_gp_pu(2, sp->name);
    gp_iv[2] = BP_A_LARGE(sp->prop);
    gp_iv[3] = ovl_gp_gc(sp);
    ovl_gp_pw(3, sp->price);
    if (hb != BP_NOBODY) {
        ovl_gp_pm(4, &bp_game.auc_bid);
        gp_sv[5] = ovl_gp_name(hb);
        gp_iv[4] = BP_A2_TOK_FLOPPY + bp_game.token[hb];
    }
    ovl_gp_pu(6, ovl_gp_name(bidder));
    gp_iv[5] = BP_A2_TOK_FLOPPY + bp_game.token[bidder];
    if (ovl_gp_mine()) {
        gp_iv[6] = 1;
        ovl_gp_pm(7, &gp_bid);
        ovl_gp_minbid(&gp_u1); ovl_gp_pm(8, &gp_u1);
        ovl_gp_pm(9, &bp_game.cash[bidder]);
        bp_u32_copy(&gp_u1, &bp_game.cash[bidder]);
        if (bp_u32_sub(&gp_u1, &gp_bid) == BP_OK) { gp_iv[7] = 1; ovl_gp_pm(10, &gp_u1); }
    }
    if (gp_auto) {
        for (w = 0; w < BP_SEATS; w++)
            if ((gp_auto >> w) & 1) { ovl_gp_pa(ovl_gp_name(w)); ovl_gp_pa(" "); }
        ovl_gp_pa(TP_PASSED_NO_RAISE);
        ovl_gp_pk(11);
        gp_iv[8] = 1;
    }
    ovl_gp_scene(1);
    gp_xm.w = 0; gp_xp.w = 0;
    if (gp_iv[6]) {                 /* the editor: a bid step changes it and below */
        ovl_gp_srect(45, &gp_xm);
        ovl_gp_srect(43, &gp_xp);
        gp_vy = gp_xm.y;
    }
}
/* ---- panb: the kit-drawn panels (SPEC.md 131.9.13) ---------------------------- */
#define GP_NOSPR 383            /* an art ID with no record: draws nothing */
static char gp_ub[3][14];       /* numbers for them */
static int gp_ox, gp_oy;        /* the scene's origin, for the text the code adds */
/* scene n (a CORE one h rows tall, centred in the free area; COMPACT's is
 * drawn at the top) with variant sel */
static void ovl_gp_usc(int n, int h, int sel)
{
    gp_ox = gp_r.x + (gp_r.w - 332) / 2;
    gp_oy = gp_y;
    if (bp_layout == BP_LAY_COMPACT) sel++;
    else if (gp_btop - gp_y > h) gp_oy += (gp_btop - gp_y - h) / 2;
    gp_iv[0] = sel;
    gk_scene(BP_A2_SC_PANB + n, gp_ox, gp_oy, gp_sv, gp_iv);
}
/* string slot n = the number v, with its commas */
static void ovl_gp_ns(int n, const BPU32 *v)
{
    ovl_gp_lb(); ovl_gp_lv(v);
    os88_strcpy(gp_ub[n % 3], gp_line, 14);
    gp_sv[n] = gp_ub[n % 3];
}
/* the labelled cell scene: a box at (x, y) w x h holding label over gp_line */
static void ovl_gp_ucell(int x, int y, int w, int h, const char *label)
{
    gp_sv[0] = label; gp_sv[1] = gp_line;
    gp_iv[1] = w; gp_iv[2] = h; gp_iv[3] = gp_fh + 4; gp_iv[4] = gp_font;
    if (h < 2 * gp_fh) { gp_sv[0] = ""; gp_iv[3] = (h - gp_fh) / 2; }
    gk_scene(BP_A2_SC_PANB + 4, x, y, gp_sv, gp_iv);
}
static void ovl_gp_dcard(void)
{
    const BPCard *cd;
    int x, xr, y;
    cd = bp_card(bp_game.card);
    if (!cd) { ovl_gp_frame(TP_CARD_2); return; }
    ovl_gp_frame(cd->deck ? TP_SYSTEM_ERROR : TP_FLOPPY_FIND);
    gp_sv[0] = ovl_gp_name(bp_game.decider); gp_sv[1] = cd->title;
    gp_iv[1] = BP_A2_CARD_FF_T + cd->deck;
    ovl_gp_usc(0, 150, 0);
    x = gp_x; xr = gp_xr; y = gp_y;
    if (bp_layout == BP_LAY_COMPACT) { gp_y += gp_pitch + 3; }
    else { gp_x = gp_ox + 122; gp_xr = gp_ox + 320; gp_y = gp_oy + 64; }
    ovl_gp_para(cd->text, 0);
    gp_x = x; gp_xr = xr; gp_y = y;
}
static void ovl_gp_drepair(void)
{
    int d, i;
    d = bp_game.decider;
    ovl_gp_frame(TB_REPAIR_SHOP);
    gp_sv[0] = ovl_gp_name(d);
    ovl_gp_ns(1, &bp_game.cash[d]);
    bp_u32_set(&gp_u1, bp_game.tries[d], 0); ovl_gp_ns(2, &gp_u1);
    for (i = 0; i < 3; i++) gp_iv[1 + i] = bp_game.tries[d] > i;
    gp_iv[4] = bp_game.disks[d] & 1 ? BP_A_CARD_FF_S : GP_NOSPR;
    gp_iv[5] = bp_game.disks[d] & 2 ? BP_A_CARD_SE_S : GP_NOSPR;
    ovl_gp_usc(1, 150, 0);
}
static void ovl_gp_ddebt(void)
{
    int d, sh;
    d = bp_game.decider;
    ovl_gp_frame(TB_DEBT);
    gp_sv[0] = ovl_gp_name(d); gp_sv[1] = ovl_gp_name(bp_game.debt_to);
    ovl_gp_ns(2, &bp_game.debt_amount);
    ovl_gp_ns(3, &bp_game.cash[d]);
    bp_u32_copy(&gp_u1, &bp_game.debt_amount);
    sh = bp_u32_sub(&gp_u1, &bp_game.cash[d]) == BP_OK && !bp_u32_iszero(&gp_u1);
    ovl_gp_ns(4, &gp_u1);
    gp_iv[1] = bp_layout == BP_LAY_COMPACT ? 0 : sh ? 1 : 2;
    gp_iv[2] = bp_mono ? 0 : 4;
    ovl_gp_usc(2, 150, gp_conf ? 2 : 0);
}
/* cash after a payment: *out; BP_EFUNDS when the payer cannot */
static int ovl_gp_after(int seat, int pays, const BPU32 *amt, BPU32 *out)
{
    bp_u32_copy(out, &bp_game.cash[seat]);
    if (pays) return bp_u32_sub(out, amt);
    return bp_u32_add(out, amt);
}
/* the collection a trade completes for seat s, which gets `in` and gives
 * `out`: every member owned after it, one of them new; -1 none */
static int ovl_gp_tset(int s, const BPU32 *in, const BPU32 *out)
{
    const BPSpace *sp;
    int g, p, all, got;
    for (g = 0; g < BP_GROUPS; g++) {
        all = 1; got = 0;
        for (p = 0; p < BP_PROPS; p++) {
            sp = bp_space(bp_prop_space(p));
            if (sp->kind != BP_K_COMPUTER || sp->group != g) continue;
            if (ovl_gp_bit(in, p)) got = 1;
            else if (bp_game.owner[p] != s || ovl_gp_bit(out, p)) all = 0;
        }
        if (all && got) return g;
    }
    return -1;
}
/* the trade review (131.9.12): scene 2. Per side, a lone computer is the
 * boards' card (slots); anything else is a list of names the code wraps in
 * the side's area (several computers, disks, nothing) */
static void ovl_gp_dtrade(void)
{
    const BPU32 *m;
    const BPSpace *sp;
    int f, t, g, s, p, j, c, n, k, dk, mk;
    f = bp_game.offer_from; t = bp_game.offer_to;
    ovl_gp_sbeg();
    ovl_gp_pu(2, ovl_gp_name(f)); ovl_gp_pu(3, ovl_gp_name(t));
    gp_iv[2] = ovl_gp_mine();
    ovl_gp_pm(12, &bp_game.offer_cash);
    mk = 0;
    for (c = 0; c < 2; c++) {
        s = c ? t : f;
        gp_sv[4 + c] = ovl_gp_name(s);
        ovl_gp_pm(6 + c, &bp_game.cash[s]);
        j = (bp_game.offer_payer != 0) == c && !bp_u32_iszero(&bp_game.offer_cash);
        gp_iv[13 + c] = j;
        if (ovl_gp_after(s, j, &bp_game.offer_cash, &gp_u1) == BP_OK) ovl_gp_pm(8 + c, &gp_u1);
        else { ovl_gp_pa(TP_SHORT); ovl_gp_pk(8 + c); }
        m = c ? &bp_game.offer_get : &bp_game.offer_give;
        dk = (bp_game.offer_disks >> (c ? 4 : 0)) & 3;
        n = 0;
        for (p = BP_PROPS - 1; p >= 0; p--) if (ovl_gp_bit(m, p)) { n++; k = p; }
        if ((n == 1 && !dk) || (n && bp_layout != BP_LAY_COMPACT)) {
            /* the card: the side's (first) computer; alone, its facts,
             * else the rest are "+ names" under it (after the scene) */
            sp = bp_space(bp_prop_space(k));
            gp_iv[7 + c] = 1;
            gp_iv[9 + c] = BP_A_LARGE(k);
            gp_iv[11 + c] = ovl_gp_gc(sp);
            gp_sv[13 + c] = sp->name;
            mk |= bp_game.mortgaged[k] << c;
            if (n == 1 && !dk) {
                mk |= 4 << c;       /* alone: the card says it all */
                if (bp_layout != BP_LAY_COMPACT) ovl_gp_pa(TP_VALUE_);
                ovl_gp_pw(15 + c, sp->price);
                gp_sv[17 + c] = bp_game.mortgaged[k] ? TP_MORTGAGED : TP_NO_MORTGAGE;
                n = bp_game.level[k];   /* RAM 1..4, Workshop: five handles in a row */
                if (!j) gp_sv[19 + c] = n ? GP_RAM(n) : TP_NO_UPGRADES;   /* else the PLUS box */
            }
        }
    }
    s = f; g = ovl_gp_tset(f, &bp_game.offer_get, &bp_game.offer_give);
    if (g < 0) { s = t; g = ovl_gp_tset(t, &bp_game.offer_give, &bp_game.offer_get); }
    if (g >= 0) {
        gp_iv[3] = 1;
        gp_sv[10] = ovl_gp_name(s);
        gp_sv[11] = bp_group_name(g);
        for (p = 0, j = 4; p < BP_PROPS && j < 7; p++) {
            sp = bp_space(bp_prop_space(p));
            if (sp->kind == BP_K_COMPUTER && sp->group == g)
                gp_iv[j++] = bp_layout == BP_LAY_FULL ? BP_A_SMALL(p) : bp_mono ? BP_A_MTINY(p) : BP_A_TINY(p);
        }
    }
    ovl_gp_scene(2);
    g = gp_btop;
    for (c = 0; c < 2; c++) {
        if ((mk >> c) & 1) {        /* the card's computer is mortgaged: the mark */
            ovl_gp_srect(6 + c, &gp_xm);
            if (gp_xm.w) bp_w_sprite(gp_xm.x, gp_xm.y, bp_mono ? BP_A_M_MORTGAGE : BP_A_MORTGAGE);
        }
        if ((mk >> 2 >> c) & 1) continue;
        /* the names, wrapped: under the card the rest ("+ ..."; #8+c when
         * the PLUS box shortens it), with no card the side's all (#2+c) */
        m = c ? &bp_game.offer_get : &bp_game.offer_give;
        k = -1;
        ovl_gp_lb();
        if (gp_iv[7 + c]) {
            ovl_gp_la(TP_PLUS_);
            for (k = 0; !ovl_gp_bit(m, k); k++) ;
            ovl_gp_srect(8 + c, &gp_lr[c]);
            if (!gp_iv[13 + c] || !gp_lr[c].w) ovl_gp_srect(4 + c, &gp_lr[c]);
        } else ovl_gp_srect(2 + c, &gp_lr[c]);
        gp_x = gp_lr[c].x + 2; gp_xr = gp_lr[c].x + gp_lr[c].w - 2;
        gp_y = gp_lr[c].y; gp_btop = gp_y + gp_lr[c].h;
        n = 0;                      /* "nothing" while n stays 0 */
        for (p = 0; p < BP_PROPS + 2; p++) {
            if (p == k || (p < BP_PROPS ? !ovl_gp_bit(m, p)
                           : !((bp_game.offer_disks >> (c ? 4 : 0) >> (p - BP_PROPS)) & 1))) continue;
            if (n++) ovl_gp_la(", ");
            ovl_gp_la(p < BP_PROPS ? ovl_gp_pname(p) : p == BP_PROPS ? TP_DISK_FF : TP_DISK_SE);
            if (p < BP_PROPS && bp_game.mortgaged[p]) ovl_gp_la(TP_MORTGAGED_P);
        }
        if (!n) ovl_gp_la(TP_NOTHING);
        ovl_gp_para(gp_line, 0);
    }
    gp_btop = g;
}
/* standings: cash + assets = score; the winners marked, with a victory block, when over */
/* standings: cash + assets = score; the winners marked, with a victory block, when over */
/* standings: cash + assets = score; the winners marked, with a victory block, when over */
/* standings: cash + assets = score; the winners marked, with a victory block, when over */
/* standings: cash + assets = score; the winners marked, with a victory block, when over */
static void ovl_gp_dstand(int over)
{
    int s, w, n, rh, cw, lay;
    ovl_gp_frame(over ? TP_GAME_OVER : TP_STANDINGS);
    w = gp_xr - gp_x;
    lay = bp_layout;
    if (over) {
        ovl_gp_lb();
        for (s = 0, n = 0; s < BP_SEATS; s++)
            if ((bp_game.winners >> s) & 1) {
                if (n) ovl_gp_la(", ");
                ovl_gp_la(ovl_gp_name(s)); n++;
            }
        os88_strcpy(gp_ub[0], gp_line, 14); gp_sv[0] = gp_ub[0];
        gp_sv[1] = n > 1 ? TP_SHARED_WIN : TP_WINNER;
        if (lay == BP_LAY_FULL) { gk_scene(BP_A2_SC_PANB + 8, gp_x, gp_y, gp_sv, gp_iv); gp_y += 90; }
        else {
            ovl_gp_lb(); ovl_gp_la(gp_sv[1]); ovl_gp_la(gp_ub[0]);
            gk_banner(gp_x, gp_y, w, gp_fh + 6, gp_line, gp_font, -1); gp_y += gp_fh + 10;
        }
    }
    rh = lay == BP_LAY_FULL ? 28 : lay == BP_LAY_MID ? 20 : 12;
    cw = w / 5;
    gp_iv[1] = w; gp_iv[6] = cw - 6; gp_iv[3] = w - 3 * cw; gp_iv[4] = gp_iv[3] + cw; gp_iv[5] = gp_iv[4] + cw;
    gp_iv[2] = gp_iv[3] - (lay == BP_LAY_FULL ? 44 : 30);
    gp_iv[0] = lay + 3;
    gk_scene(BP_A2_SC_PANB + 5, gp_x, gp_y, gp_sv, gp_iv);
    gp_y += rh + 4;
    gp_iv[0] = lay;
    for (s = 0; s < bp_game.nseats; s++) {
        if (over) bp_u32_copy(&gp_u1, &bp_game.score[s]); else bp_score_of(&bp_game, s, &gp_u1);
        bp_u32_copy(&gp_u2, &gp_u1);
        if (bp_u32_sub(&gp_u2, &bp_game.cash[s]) != BP_OK) bp_u32_set(&gp_u2, 0, 0);
        ovl_gp_ns(1, &bp_game.cash[s]); ovl_gp_ns(2, &gp_u2); ovl_gp_ns(3, &gp_u1);
        ovl_gp_lb(); ovl_gp_la(ovl_gp_name(s)); if (!bp_game.alive[s]) ovl_gp_la(TP_OUT);
        gp_sv[0] = gp_line;
        gp_iv[7] = over && ((bp_game.winners >> s) & 1) ? 14 : 15;
        gp_iv[8] = (lay == BP_LAY_FULL ? BP_A2_TOK_FLOPPY : BP_A_TOK_FLOPPY) + bp_game.token[s];
        gk_scene(BP_A2_SC_PANB + 5, gp_x, gp_y, gp_sv, gp_iv);
        gp_y += rh + 1;
    }
    gp_y += 3;
    if (bp_game.rules == BP_RULES_QUICK && lay != BP_LAY_COMPACT) {
        ovl_gp_lb(); ovl_gp_la(TP_QUICK_MATCH_ROUND); ovl_gp_lu(bp_game.round); ovl_gp_la(TP_OF);
        ovl_gp_lu(bp_game.rounds_limit); ovl_gp_tc(gp_line, 0);
    }
    if (over && lay == BP_LAY_FULL) ovl_gp_para(TP_SCORE_CASH_PROPERTY, 0);
}
static void ovl_gp_dhand(void)
{
    int s;
    s = gp_hand - 1;
    ovl_gp_frame(TP_PASS_THE_KEYBOARD);
    gp_sv[0] = ovl_gp_name(s);
    ovl_gp_lb(); ovl_gp_la(TP_NEXT_2); ovl_gp_la(gp_phn[bp_game.phase]);
    os88_strcpy(gp_ub[0], gp_line, 14); gp_sv[1] = gp_ub[0];
    gp_iv[1] = BP_A2_TOKL_FLOPPY + bp_game.token[s];
    ovl_gp_usc(9, 150, 0);
}
/* the link lost / paused (131.9.12): scene 3, only what the session knows -
 * the link state, whose PC is awaited, the last log line, whether a move is
 * waiting to be confirmed, the paired save's state */
static void ovl_gp_dpause(void)
{
    int ls, s, pr;
    ls = bp_link_state();
    ovl_gp_sbeg();
    if (!gp_iv[1]) gp_sv[1] = gp_conf ? TP_LEAVING_ABANDONS_THIS : ls == 8 ? TP_THE_CLIENT_TAKES : TP_RECONNECT_TO_CONTINUE;
    gp_sv[2] = ls == BP_L_RESYNC ? TP_RESYNCING_2 : ls == 8 ? TP_BOARDS_DIFFER_2 : TP_CABLE_LINK_LOST;
    gp_sv[3] = ls == 8 ? TP_INPUT_STOPPED : TP_GAME_PAUSED;
    gp_sv[4] = TP_WAITING_OTHER;
    if (ls == 8) gp_sv[4] = bp_link_msg();
    else for (s = 0; s < bp_game.nseats; s++)
        if (bp_game.alive[s] && !bp_sess_mine(s)) {
            ovl_gp_pa(TP_WAITING_FOR); ovl_gp_pa(ovl_gp_name(s)); ovl_gp_pa(TP_S_PC); ovl_gp_pk(4);
            break;
        }
    gp_sv[5] = bp_log_line(0);
    gp_iv[3] = BP_A2_TOKL_FLOPPY + bp_game.token[bp_game.active];
    gp_sv[6] = bp_sess_busy() ? TP_WAIT_CONFIRM : TP_NOTHING_WAITING;
    pr = bp_sess_pair() & 3;
    gp_iv[2] = pr == BP_PAIR_DONE;
    gp_sv[7] = GP_PAIR(pr);   /* four handles in BP_PAIR_* order */
    ovl_gp_scene(3);
}
static const char *const gp_dl[4] = {TP_UPGRADE, TP_SELL, TP_MORTGAGE, TP_REDEEM};
static void ovl_gp_dmanage(void)
{
    int seat, n, i, top, y, p, sp, rh, w, x, lay, cw, dh;
    seat = ovl_gp_mseat();
    lay = bp_layout;
    ovl_gp_lb(); ovl_gp_la(TP_COLLECTION); ovl_gp_la(ovl_gp_name(seat));
    ovl_gp_frame(gp_line);
    n = gp_mn; w = gp_xr - gp_x; x = gp_x;
    ovl_gp_lb(); ovl_gp_la(TB_CASH); ovl_gp_la(" "); ovl_gp_lm(&bp_game.cash[seat]);
    dh = 2 * gp_fh + 8;
    ovl_gp_ucell(x, gp_y, w, gp_fh + 7, 0);
    gp_y += gp_fh + 11;
    if (!n) { ovl_gp_para(TB_NO_PROPS, 0); return; }
    rh = lay == BP_LAY_FULL ? 26 : lay == BP_LAY_MID ? 14 : 9;
    if (lay == BP_LAY_COMPACT) dh = 0;
    i = (gp_btop - gp_y - dh - 10) / rh;
    if (i < 2) i = 2;
    if (i > n) i = n;
    top = gp_mc - i + 1;
    if (top < 0) top = 0;
    gk_box(x, gp_y, w, i * rh + 4, GK_SUNKEN2, 15);
    gp_lr[0].x = x + 2; gp_lr[0].y = gp_y + 2; gp_lr[0].w = w - 4; gp_lr[0].h = i * rh;
    gp_lrow = rh;
    gp_ttop[0] = (BPByte)top;
    gp_vy = gp_y - 1;               /* a cursor move changes the list and below */
    y = gp_y + 2;
    gp_iv[0] = lay; gp_iv[1] = w - 4; gp_iv[2] = w - 34; gp_iv[3] = w - 70;
    for (n = top; n < top + i; n++) {
        p = gp_ml[n]; sp = bp_prop_space(p);
        gp_sv[0] = bp_space(sp)->shortname;
        gp_iv[4] = n == gp_mc ? 1 + lay : 0;
        gp_iv[5] = lay == BP_LAY_FULL ? BP_A_SMALL(p) : BP_A_TINY(p);
        gp_iv[6] = gp_iv[7] = GP_NOSPR;
        ovl_gp_lb();
        cw = bp_game.level[p];
        if (cw) { gp_iv[6] = cw == BP_MAX_LEVEL ? BP_A_WORKSHOP : BP_A2_CHIP; if (cw < BP_MAX_LEVEL) { ovl_gp_lc('x'); ovl_gp_lu(cw); } else ovl_gp_la("WS"); }
        if (bp_game.mortgaged[p]) { if (lay == BP_LAY_COMPACT) ovl_gp_la(" M"); else gp_iv[7] = BP_A_MORTGAGE; }
        os88_strcpy(gp_ub[1], gp_line, 14); gp_sv[1] = gp_ub[1];
        gp_ub[2][0] = bp_space(sp)->kind == BP_K_COMPUTER ? bp_group_letter(bp_space(sp)->group) : 'U'; gp_ub[2][1] = 0;
        gp_sv[2] = gp_ub[2];
        gk_scene(BP_A2_SC_PANB + 3, x + 2, y, gp_sv, gp_iv);
        y += rh;
    }
    gp_y = y + 6;
    if (!dh) return;
    p = gp_ml[gp_mc]; sp = bp_prop_space(p);
    bp_redeem_cost(sp, &gp_u2);
    cw = w / 4;
    for (i = 0; i < 4; i++) {
        ovl_gp_lb();
        if (i < 2 && bp_space(sp)->kind == BP_K_COMPUTER) { ovl_gp_lc(i ? '+' : '-'); ovl_gp_lu((unsigned)bp_group_upgrade_cost(bp_space(sp)->group) / (1 + i)); }
        else if (i == 2) { ovl_gp_lc('+'); ovl_gp_lu((unsigned)bp_mortgage_value(sp)); }
        else if (i == 3) { ovl_gp_lc('-'); ovl_gp_lv(&gp_u2); }
        else ovl_gp_lc('-');
        ovl_gp_ucell(x + i * cw, gp_y, cw - 2, dh, gp_dl[i]);
    }
}
static void ovl_gp_dcompose(void)
{
    int c, nl, i, idx, y, hw, x, it, me, rh, big, cmp, w, ly, lay, k;
    BPRect *r;
    me = bp_game.decider;
    lay = bp_layout;
    cmp = lay == BP_LAY_COMPACT;
    ovl_gp_lb(); ovl_gp_la(TP_TRADE_3); ovl_gp_la(ovl_gp_name(me)); ovl_gp_la(TP_WITH); ovl_gp_la(ovl_gp_name(gp_tto));
    ovl_gp_frame(gp_line);
    ovl_gp_tbuild();
    big = gp_fh + 8;
    w = gp_xr - gp_x;
    hw = (w - (cmp ? 6 : 34)) / 2;
    rh = lay == BP_LAY_FULL ? 18 : lay == BP_LAY_MID ? 14 : 9;
    gp_vy = gp_y - 1;               /* column, cursor, toggles, cash: the lists and below */
    for (c = 0; c < 2; c++) {
        x = gp_x + c * (w - hw);
        ovl_gp_lb(); if (c == gp_tcol) ovl_gp_la("> "); ovl_gp_la(ovl_gp_name(c ? gp_tto : me)); ovl_gp_la(TP_GIVES_2);
        if (c == gp_tcol) gk_banner(x, gp_y, hw, gp_fh + 5, gp_line, gp_font, -1);
        else gk_titlebar(x, gp_y, hw, gp_fh + 5, gp_line, gp_font, GK_CENTRE);
    }
    gp_y += gp_fh + 7;
    nl = (gp_btop - gp_y - (cmp ? 2 * gp_pitch + 8 : big + gp_fh + 22)) / rh;
    if (nl < 1) nl = 1;
    gp_lrow = rh;
    ly = gp_y;
    gp_iv[0] = lay; gp_iv[1] = hw - 4; gp_iv[2] = hw - 52;
    for (c = 0; c < 2; c++) {
        x = gp_x + c * (w - hw);
        if (gp_tcur[c] < gp_ttop[c]) gp_ttop[c] = gp_tcur[c];
        if (gp_tcur[c] >= gp_ttop[c] + nl) gp_ttop[c] = (BPByte)(gp_tcur[c] - nl + 1);
        r = &gp_lr[c]; r->x = x + 2; r->y = ly + 2; r->w = hw - 4; r->h = nl * rh;
        gk_box(x, ly, hw, nl * rh + 4, GK_SUNKEN2, 15);
        y = ly + 2;
        for (i = 0; i < nl; i++) {
            idx = gp_ttop[c] + i;
            if (idx >= gp_in[c]) break;
            it = gp_it[c][idx];
            k = ovl_gp_tsel(c, idx);
            ovl_gp_lb();
            if (cmp) ovl_gp_la(k ? TP_X_2 : TP_X_4);
            if (it >= 32) ovl_gp_la(it == 32 ? TP_DISK_FF : TP_DISK_SE);
            else { ovl_gp_la(ovl_gp_pname(it)); if (bp_game.mortgaged[it]) ovl_gp_la("*"); }
            gp_sv[0] = gp_line;
            gp_iv[4] = c == gp_tcol && idx == gp_tcur[c] ? 1 + lay : 0;
            gp_iv[5] = k;
            gp_iv[6] = it < 32 ? BP_A_TINY(it) : GP_NOSPR;
            gp_iv[7] = it < 32 && !ovl_gp_tradable(it) ? 8 : 0;
            gk_scene(BP_A2_SC_PANB + 6, x + 2, y, gp_sv, gp_iv);
            y += rh;
        }
                if (!gp_in[c]) bp_w_text(x + 8, ly + 2, TP_NONE_2, 0, gp_font);
    }
    gp_y = ly + nl * rh + 8;
    bp_u32_set(&gp_u2, gp_tcash, 0);
    ovl_gp_lb(); ovl_gp_lv(&gp_u2); ovl_gp_la(TP_CR); ovl_gp_la(TB_PAID_BY); ovl_gp_la(ovl_gp_name(gp_tpay ? gp_tto : me));
    if (!cmp) { ovl_gp_ucell(gp_x, gp_y, w, gp_fh + 7, 0); gp_y += gp_fh + 11; }
    else ovl_gp_tl();
    ovl_gp_lb();
    for (c = 0; c < 2; c++) {
        k = c ? gp_tto : me;
        if (c) ovl_gp_la("  ");
        ovl_gp_la(ovl_gp_name(k)); ovl_gp_la(" "); ovl_gp_lv(&bp_game.cash[k]); ovl_gp_la(">");
        if (ovl_gp_after(k, (gp_tpay != 0) == c, &gp_u2, &gp_u1) == BP_OK) ovl_gp_lv(&gp_u1); else ovl_gp_la(TP_SHORT);
    }
    if (!cmp) gk_textbox(gp_x, gp_y, w, gp_fh + 6, gp_line, 0, gp_font, GK_CENTRE | GK_FIT);
    else ovl_gp_tl();
}
static void ovl_gp_dchat(void)
{
    int i;
    ovl_gp_frame(TP_CHAT);
    gk_box(gp_x, gp_y, gp_xr - gp_x, 4 * gp_pitch + 6, GK_SUNKEN2, 15);
    gp_x += 5; gp_y += 3;
    for (i = 0; i < 3; i++) ovl_gp_t(gp_cl[i], 0);
    gp_vy = gp_y - 1;               /* typing changes the field and below */
    ovl_gp_lb(); ovl_gp_la("> "); ovl_gp_la(gp_cbuf); ovl_gp_la("_");
    ovl_gp_t(gp_line, 0);
    ovl_gp_lb(); ovl_gp_lu(gp_cn); ovl_gp_la("/"); ovl_gp_lu(GP_CHATMAX); ovl_gp_la(TP_CHARACTERS_LETTERS_TYPE);
    ovl_gp_para(gp_line, 0);
    gp_x -= 5;
}
static void ovl_gp_ddiag(void)
{
    int ls, pt;
    ovl_gp_frame(TP_LINK_DIAGNOSTICS);
    gk_box(gp_x, gp_y, gp_xr - gp_x, gp_btop - gp_y, GK_SUNKEN2, 15);
    gp_x += 6; gp_y += 4;
    ls = bp_link_state();
    pt = bp_ck_port();
    ovl_gp_lb(); ovl_gp_la(TP_ROLE); ovl_gp_la(bp_mode == BP_MODE_HOST ? TP_BANKER_HOST : bp_mode == BP_MODE_CLIENT ? TP_CLIENT : TP_SAME_PC);
    ovl_gp_la(TP_LINK); ovl_gp_la(gp_lstate[ls > 8 ? 0 : ls]); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_PORT);
    if (pt) { ovl_gp_la(TP_COM); ovl_gp_lu(pt); ovl_gp_la(TP_2400_BAUD); } else ovl_gp_la(TP_NONE);
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_REVISION); ovl_gp_lv(&bp_game.rev); ovl_gp_la(bp_sess_busy() ? TP_PENDING : TP_IDLE_2);
    ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_CRC); ovl_gp_lu(bp_diag.crc); ovl_gp_la(TP_RETRY_2); ovl_gp_lu(bp_diag.retry);
    ovl_gp_la(TP_FRAME); ovl_gp_lu(bp_diag.frame); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_OVERRUN); ovl_gp_lu(bp_diag.overrun); ovl_gp_la(TP_RESYNC); ovl_gp_lu(bp_diag.resync);
    ovl_gp_la(TP_CHAT_LOST); ovl_gp_lu(bp_diag.chatdrop); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_RX); ovl_gp_lu(bp_diag.rx); ovl_gp_la(TP_TX); ovl_gp_lu(bp_diag.tx); ovl_gp_tl();
    ovl_gp_lb(); ovl_gp_la(TP_PAIRED_SAVE); ovl_gp_la(gp_pairs[bp_sess_pair() & 3]); ovl_gp_tl();
    ovl_gp_para(bp_link_msg(), 0);
    gp_x -= 6;
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
    static int kidx, ktot;
    int n, lpp, i, base, cnt, pg;
    ovl_gp_frame(keys ? TP_KEYS : TP_RULES);
    if (!ktot) {                    /* once: where #KEYS is, and how many lines in all */
        for (n = 0; ovl_gp_hline(n, buf); n++)
            if (!kidx && buf[0] == '#' && buf[1] == 'K') kidx = n + 1;
        ktot = n;
        if (!kidx) kidx = ktot + 1;
    }
    /* each page shows only its own section; paging stops at the last (Qqa2-5) */
    gk_box(gp_x, gp_y, gp_xr - gp_x, gp_btop - gp_y, GK_SUNKEN2, 15);
    gp_x += 6; gp_y += 4;
    base = keys ? kidx : 0;
    cnt = keys ? ktot - kidx : kidx - 1;
    if (cnt < 1) cnt = 1;
    lpp = (gp_btop - gp_y - 3) / gp_pitch;
    if (lpp < 1) lpp = 1;
    pg = (cnt + lpp - 1) / lpp;
    if (pg > 255) pg = 255;
    gp_hpages = (BPByte)pg;
    if (gp_help >= pg) gp_help = (BPByte)(pg - 1);
    for (i = 0; i < lpp && gp_help * lpp + i < cnt; i++) {
        n = base + gp_help * lpp + i;
        if (!ovl_gp_hline(n, buf)) break;
        ovl_gp_t(buf, 0);
    }
    gp_x -= 6;
}
/* the one-line bar over the bottom of the centre: light grey, white on 1bpp
 * where grey is a dither under black text (Qqa2-2) */
static void ovl_gp_bar(void)
{
    gp_r.h = gp_pitch + gp_bh + 14;
    gp_r.y = bp_r_center.y + bp_r_center.h - gp_r.h - 2;
    bp_w_box(&gp_r, bp_mono ? 15 : 7, 0);
    gp_x = gp_r.x + 6; gp_xr = gp_r.x + gp_r.w - 6;
    gp_btop = gp_r.y + 4;
    gp_vy = gp_r.y;
}
/* the turn bar (131.9.12): scene 4, a bottom panel - the mover's token and
 * whose move (or the status line), Roll / End Turn / Trade / Manage */
static void ovl_gp_didle(void)
{
    int d;
    d = bp_game.decider;
    ovl_gp_sbeg();
    if (!gp_iv[1]) {
        ovl_gp_pa(ovl_gp_name(d));
        ovl_gp_pa(bp_game.phase != BP_PH_AFTER ? TP_ROLL_THE_DICE :
                  bp_game.doubles ? TP_DOUBLES_ROLL_AGAIN : TP_END_YOUR_TURN);
        ovl_gp_pk(1);
    }
    gp_iv[6] = BP_A2_TOK_FLOPPY + bp_game.token[d];
    ovl_gp_scene(4);
}
static void ovl_gp_dreview(void)
{
    ovl_gp_bar();
    bp_w_text(gp_x, gp_btop, TP_GAME_OVER_REVIEWING, 0, gp_font);
}
/* the whole panel: one call per strip, so it must cost nothing but drawing */
static void ovl_gp_draw(void)
{
    const char *m;
    ovl_gp_track();
    ovl_gp_spec();
    if (gp_kind == GK_NONE) return;
    if (gp_kind == GK_HAND) gp_hq = os88_ticks();   /* drawn now: a key queued meanwhile is not fresh */
    ovl_gp_geom();
    gp_scn = 0;
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
    if (!gp_scn && gp_kind != GK_IDLE && gp_kind != GK_REVIEW) {    /* the status line */
        ovl_gp_lb();
        if ((m = ovl_gp_stxt()) != 0) bp_w_text(gp_x, gp_btop, m, 0, gp_font);
    }
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
/* rules/keys paging stops at either end (131.9.11) */
static void ovl_gp_page(int dir)
{
    if (dir < 0) { if (gp_help) gp_help--; }
    else if (!gp_hpages || gp_help + 1 < gp_hpages) gp_help++;
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
    case GK_HAND: gp_hand = 0; return 1;    /* only a click on Ready reaches here */
    case GK_PAUSE:
        if (key == 'r') { if (bp_mode == BP_MODE_HOST) bp_link_host(bp_ck_port()); else bp_link_join(bp_ck_port()); }
        else if (key == 'l') { sp = bp_sess_resume_pair(); if (sp != BP_OK && sp != BP_EPENDING) gp_err = bp_err_text(sp); }
        else if (key == 'd') gp_over = BP_GP_DIAG;
#ifdef BP_L_DIVERGED
        else if (key == 'u') { sp = bp_sess_adopt(); if (sp != BP_OK && sp != BP_EPENDING) gp_err = bp_err_text(sp); }
#endif
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
        if (key == 'n') ovl_gp_page(1); else if (key == 'b') ovl_gp_page(-1); else gp_over = 0;
        return 1;
    default:
        gp_over = 0; return 1;
    }
}
static const char *ovl_gp_whytext(int why)
{
    if (why == BP_EARG && gp_kind == GK_AUC) return TP_BID_AT_LEAST;
    if (why == BP_EARG && gp_kind == GK_COMPOSE) return TP_CHOOSE_SOMETHING_TO;
    if (why == BP_EARG && gp_kind == GK_CHAT) return TP_TYPE_A_MESSAGE;
    if (why == BP_EARG && (gp_kind == GK_RULES || gp_kind == GK_KEYS)) return TP_NO_MORE_PAGES;
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
        if (nav == GS_PGDN || nav == GS_DN) { ovl_gp_page(1); return 1; }
        if (nav == GS_PGUP || nav == GS_UP) { ovl_gp_page(-1); return 1; }
        return 0;
    default: break;
    }
    (void)d; (void)n;
    return 0;
}
/* ---- damage: what a key, click or open changed (131.9.7) ---------------------------- */
static unsigned gp_h;
static void ovl_gp_mx(unsigned v) { gp_h = gp_h * 33 + v; }
static void ovl_gp_mz(const char *s) { s = bx_str(s); while (*s) ovl_gp_mx((BPByte)*s++); }
static void ovl_gp_mb(const void *p, int n)
{
    const BPByte *q;
    for (q = (const BPByte *)p; n > 0; n--) ovl_gp_mx(*q++);
}
/* the panel as drawn: 0 identity, 1 focus, 2 buttons, 3 hint, 4 status,
 * 5 whole-panel text, 6 the lines from gp_vy down (a sent chat line is
 * dirtied by bp_gp_chat itself) */
static void ovl_gp_snap(unsigned *s)
{
    int i, e;
    s[0] = (unsigned)ovl_gp_ident();
    s[1] = gp_foc | (ovl_gp_needstatus() << 8);    /* 8: a status line is reserved */
    for (e = 0; e < 2; e++) {
        gp_h = gp_nb + e;
        for (i = 0; i < gp_nb; i++)
            if (!e || !gp_bwhy[i]) { ovl_gp_mx(gp_bkey[i] + (e ? 0 : gp_bwhy[i] << 8)); ovl_gp_mz(gp_blab[i]); }
        s[2 + e] = gp_h;
    }
    gp_h = bp_sess_busy();
    if (gp_err) ovl_gp_mz(gp_err);
    s[4] = gp_h;
    s[5] = gp_tto + gp_help * 16;
    gp_h = gp_mc + gp_tcol * 64 + gp_tpay * 128 + gp_tdk * 256;
    ovl_gp_mx(gp_tcash);
    ovl_gp_mb(gp_tcur, 2);
    ovl_gp_mb(&gp_bid, 4); ovl_gp_mb(&gp_tgive, 4); ovl_gp_mb(&gp_tget, 4);
    ovl_gp_mz(gp_cbuf);
    s[6] = gp_h;
}
/* compare with gp_s0: CENTER for a new panel, else one band from the
 * topmost changed line to the panel's bottom; HINT when the hint moved */
static void ovl_gp_damage(void)
{
    static BPRect r;
    int y, t, bot;
    ovl_gp_track();
    ovl_gp_spec();
    ovl_gp_snap(gp_s1);
    if (gp_s1[3] != gp_s0[3] || gp_s1[0] != gp_s0[0]) bp_face_dirty(BP_D_HINT);
    if (gp_kind == GK_NONE && gp_s0[0] == gp_s1[0]) return;
    if (gp_s1[0] != gp_s0[0] || gp_s1[5] != gp_s0[5] || gp_drawn != gp_kind) {
        bp_face_dirty(BP_D_CENTER);
        return;
    }
    bot = gp_r.y + gp_r.h;
    y = bot;
    if (gp_s1[6] != gp_s0[6]) y = gp_vy;
    /* a status line appearing or going moves every line sized from gp_btop
     * (the composer's and Collection's lists, the lines under them): from
     * gp_vy, a line boundary; a status text change alone: its own line
     * (Qqa3-1: a band top inside a line erased the line's lower half) */
    if (gp_s1[4] != gp_s0[4] || ((gp_s1[1] ^ gp_s0[1]) >> 8)) {
        t = (gp_kind == GK_IDLE || gp_kind == GK_REVIEW) ? gp_r.y :
            ((gp_s1[1] ^ gp_s0[1]) >> 8) ? gp_vy : gp_btop;
        if (t < y) y = t;
    }
    if (gp_s1[1] != gp_s0[1] || gp_s1[2] != gp_s0[2]) {
        t = gp_brt;                             /* the button row's own top */
        if (t < y) y = t;
    }
    if (y >= bot) return;
    if (y < gp_r.y) y = gp_r.y;
    r.x = gp_r.x; r.y = y; r.w = gp_r.w; r.h = bot - y;
    bp_face_dirty_rect(&r);
}

/* a key's press: a confirmation's Yes counts only after GP_HQUIET quiet
 * ticks, so the key that armed it, held or typed twice, cannot answer it
 * (131.9.8); otherwise it is consumed and nothing happens */
static int ovl_gp_kpress(int i)
{
    if (gp_conf && gp_bkey[i] != 27 && os88_ticks() - gp_hq < GP_HQUIET) { gp_err = TP_LET_GO_THEN; return 1; }
    return ovl_gp_press(i);
}
static int ovl_gp_key1(int a, int sc)
{
    int nav, i, k;
#ifdef BP_PANELS_DEMO
    if (a == 0 && sc == 0x3F) { ovl_gp_demoset(gp_demo++); return 1; }
#endif
    if (gp_kind == GK_NONE) return 0;
    nav = (a == 0 || a == 0xE0) ? sc : 0;
    if (gp_kind == GK_HAND) return ovl_gp_hkey(a, sc);
    if (gp_kind == GK_CHAT) return ovl_gp_chatkey(a, sc);
    gp_err = 0;
    if (ovl_gp_edit(a, nav)) return 1;
    if (a == 9 || nav == GS_TAB) {          /* the next ENABLED button */
        for (i = 1; i <= gp_nb; i++) {
            k = (gp_foc + i) % gp_nb;
            if (!gp_bwhy[k]) { gp_foc = (BPByte)k; break; }
        }
        return 1;
    }
    if (a == 13 && gp_kind != GK_COMPOSE) { if (gp_nb) return ovl_gp_kpress(gp_foc); return 0; }
    k = a;
    if (k >= 'A' && k <= 'Z') k += 32;
    if (nav == GS_ESC) k = 27;
    if (k == 'c' && (gp_kind == GK_IDLE || gp_kind == GK_REPAIR || gp_kind == GK_DEBT)) k = 'm';
    for (i = 0; i < gp_nb; i++)
        if (gp_bkey[i] == k) return ovl_gp_kpress(i);
    if (k == 13 && gp_nb) return ovl_gp_kpress(0);
    return 0;
}
/* a key, then the damage it did (131.9.7) */
static int ovl_gp_key(int a, int sc)
{
    int r;
    ovl_gp_track();
    ovl_gp_spec();
    ovl_gp_snap(gp_s0);
    r = ovl_gp_key1(a, sc);
    ovl_gp_damage();
    return r;
}
static int ovl_gp_in(const BPRect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
static int ovl_gp_click1(int x, int y)
{
    int i, row;
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
/* a click, then the damage it did; a click is deliberate, never a repeat */
static int ovl_gp_click(int x, int y)
{
    int r;
    ovl_gp_track();
    ovl_gp_spec();
    ovl_gp_snap(gp_s0);
    r = ovl_gp_click1(x, y);
    ovl_gp_damage();
    return r;
}
static int ovl_gp_covers(void)
{
    ovl_gp_track();
    ovl_gp_spec();
    if (gp_kind == GK_BUY) return bp_layout == BP_LAY_COMPACT;  /* a bottom panel (131.9.12) */
    return gp_kind != GK_NONE && gp_kind != GK_IDLE && gp_kind != GK_REVIEW;
}
/* the hint bar: the ENABLED buttons (Qqa2-23). Callers walk i = 0, 1, ...
 * so the panel is worked out once, at 0 */
static const char *ovl_gp_hint(int i)
{
    static const char *none[] = {TP_ARROWS_SELECT, TP_F1_RULES};
    int j, n;
    if (i <= 0) { ovl_gp_track(); ovl_gp_spec(); }
    if (gp_kind == GK_NONE) return i >= 0 && i < 2 ? none[i] : 0;
    if (i < 0 || i >= gp_nb) return 0;
    for (j = 0, n = 0; j < gp_nb; j++)
        if (!gp_bwhy[j]) { if (n == i) break; n++; }
    if (j >= gp_nb) return 0;
    ovl_gp_kn(gp_hb[i], gp_bkey[j]);
    os88_strcpy(gp_hb[i] + os88_strlen(gp_hb[i]), " ", 2);
    os88_strcpy(gp_hb[i] + os88_strlen(gp_hb[i]), gp_blab[j], sizeof(gp_hb[0]) - os88_strlen(gp_hb[i]));
    return gp_hb[i];
}
static void ovl_gp_open1(int which)
{
    int why;
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
    gp_help = 0; gp_hpages = 0;
    if (which == BP_GP_CHAT) { gp_cn = 0; gp_cbuf[0] = 0; }
}
/* a menu or F1 opened a panel: it names its own damage (Qqa1-1 rest) */
static void ovl_gp_open(int which)
{
    ovl_gp_track();
    ovl_gp_spec();
    ovl_gp_snap(gp_s0);
    ovl_gp_open1(which);
    ovl_gp_damage();
}
static void ovl_gp_chat(int machine, const char *text)
{
    int i;
    for (i = 0; i < 2; i++) os88_strcpy(gp_cl[i], gp_cl[i + 1], sizeof(gp_cl[0]));
    os88_strcpy(gp_cl[2], machine == bp_here ? "You: " : "Them: ", sizeof(gp_cl[0]));
    os88_strcpy(gp_cl[2] + os88_strlen(gp_cl[2]), text, sizeof(gp_cl[0]) - os88_strlen(gp_cl[2]));
    if (gp_over == BP_GP_CHAT) bp_face_dirty(BP_D_CENTER);
}
/* every timer tick: the link pausing or resuming, or a pending command
 * settling, changes the panel without a commit (stale "Sent - waiting") */
static void ovl_gp_tick(void)
{
    int v;
    if (bp_mode == BP_MODE_LOCAL) return;
    v = bp_link_state() * 2 + (bp_sess_busy() ? 1 : 0);
    if (v != gp_tlv) { gp_tlv = v; bp_face_dirty(BP_D_CENTER | BP_D_HINT); }
}

#ifdef BP_PANELS_DEMO
#if BP_PANELS_DEMO == 2
static void ovl_gp_demonew(void)
{
    static BPSetup su;
    bp_setup_default(&su);
    su.nseats = 3;
    os88_strcpy(su.name[0], "George", 9); os88_strcpy(su.name[1], "Ada", 9); os88_strcpy(su.name[2], "Alan", 9);
    su.token[1] = 1; su.token[2] = 2;
    bp_sess_new(&su);
}
#endif
/* hand-made states to look at the panels before the engine lands: F5 steps */
static void ovl_gp_demoset(int n)
{
    int s;
    n = n % 16;
    gp_dls = 0; bp_mode = BP_MODE_LOCAL;
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
    else if (n == 10) { gp_hand = 2; gp_hq = os88_ticks(); bp_game.decider = 1; bp_game.phase = BP_PH_AUCTION; bp_game.auc_space = 4; }
    else if (n == 11) { gp_over = BP_GP_CHAT; os88_strcpy(gp_cl[1], "Them: hi from the other PC", 30); os88_strcpy(gp_cbuf, "good luck", 25); gp_cn = 9; }
    else if (n == 12) gp_over = BP_GP_DIAG;
    else if (n == 13) gp_over = BP_GP_RULES;
    else if (n == 14) gp_over = BP_GP_STANDINGS;
    else if (n == 15) {             /* a cable match whose link went quiet */
        bp_mode = BP_MODE_HOST; gp_dls = BP_L_PAUSED; bp_game.active = 2;
        bp_log_add("Alan bought C64 for 250 cr");
    }
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
void bp_gp_tick(void) { ovl_gp_tick(); }
/* the end of a key's or click's handling, after its repaint (131.9.8) */
void bp_gp_keyend(void) { gp_hq = os88_ticks(); }
int bp_gp_overchoice(void) { int c; c = gp_oc; gp_oc = 0; return c; }
