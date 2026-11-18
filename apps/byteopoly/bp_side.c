/* Byteopoly - the sidebar (SPEC.md 131.8.1, 131.8.4): the players, details,
 * link/collection and log boxes, their signatures, the sidebar's share of the
 * layout and the log ring. Split out of bp_face.c so the board and the
 * sidebar can be owned apart; one translation unit with it (byteoply.c
 * includes this file right after bp_face.c, which declares the entry points),
 * so the fc_ helpers, state and rects are shared as before.
 *
 * FULL draws each box's frame as a kit scene (art/scenes-side.txt, ids
 * BP_A2_SC_SIDE + n) and its live values on top; MID draws the frames with
 * gk_panel and fits the same content; COMPACT keeps four plain boxes. What
 * the boxes SHOW depends on the match (SPEC.md 131.8.4): the details box is
 * the lot during an auction, the connection card when the cable link is not
 * in step, "other PC linked" during a trade over the cable, else the
 * selected space; the third box is the collection of the seat this PC plays,
 * or the paired-save card while the link is down. */
#include "art2.h"
#define SD_S_RP    (BP_A2_SC_SIDE + 0)          /* the players box, FULL */
#define SD_S_CONN  (BP_A2_SC_SIDE + 1)          /* CONNECTION / OTHER PC LINKED */
#define SD_S_DETC  (BP_A2_SC_SIDE + 2)          /* COMPACT's details lines */
#define SD_S_LINKC (BP_A2_SC_SIDE + 3)          /* COMPACT's link line */
#define SD_S_COLL  (BP_A2_SC_SIDE + 4)          /* YOUR COLLECTION / PAIRED SAVE */
#define SD_S_DET   (BP_A2_SC_SIDE + 5)          /* the details card, FULL; MID's is + 2 */
#define SD_S_ROW   (BP_A2_SC_SIDE + 8)          /* a roster row, FULL; MID's is + 1, COMPACT's - 2 */

static const char *const fc_lnk[8] = {TD_OFF, TD_SEARCHING, TD_PEER_FOUND, TD_BAD_PEER,
    TD_LOBBY, TD_LINKED, TD_PAUSED, TD_RESYNC};
static char fc_log[BP_LOG_LINES][BP_LOG_LEN + 1];
static const char *sd_sv[13];
static int sd_iv[5];
static char sd_nb[12][6];
static int sd_w;                        /* the width the boxes are drawn at (a wider column keeps a face margin at the right) */

/* the sidebar's four boxes tile the column exactly (SPEC.md 131.8.1) */
static void fc_sidelayout(void)
{
    static const BPByte hh[2][3] = {{130, 124, 76}, {86, 104, 44}};     /* FULL, MID: players, details, collection */
    int hp, hd, hl, rem, side_h, rh;
    side_h = bp_r_side.h;
    rh = fc_fh + 3;
    hp = 4 * rh + 4; hl = rh + 4; hd = (side_h - hp - hl) / 2;          /* COMPACT */
    if (bp_layout != BP_LAY_COMPACT) { hp = hh[bp_layout][0]; hd = hh[bp_layout][1]; hl = hh[bp_layout][2]; }
    rem = side_h - hp - hd - hl;
    if (rem < 0) { hd += rem; rem = 0; }
    sd_w = bp_layout == BP_LAY_FULL ? 192 : bp_layout == BP_LAY_MID ? 208 : bp_r_side.w;
    if (sd_w > bp_r_side.w) sd_w = bp_r_side.w;
    fc_rp.x = fc_rd.x = fc_rl.x = fc_rg.x = bp_r_side.x;
    fc_rp.w = fc_rd.w = fc_rl.w = fc_rg.w = bp_r_side.w;
    fc_rp.y = bp_r_side.y; fc_rp.h = hp;
    fc_rd.y = fc_rp.y + hp; fc_rd.h = hd;
    fc_rl.y = fc_rd.y + hd; fc_rl.h = hl;
    fc_rg.y = fc_rl.y + hl; fc_rg.h = rem;
}

/* ---- what the boxes show -------------------------------------------------- */
/* the round shown: completed + 1, held at a Quick Match's limit */
static unsigned ovl_fc_rnd(void)
{
    unsigned n;
    n = bp_game.round + 1;
    if (bp_game.rounds_limit && n > bp_game.rounds_limit) n = bp_game.rounds_limit;
    return n;
}
/* an auction is running */
static int sd_auc(void)
{
    return fc_live() && bp_game.status == BP_ST_PLAY && bp_game.phase == BP_PH_AUCTION && bp_game.auc_space < BP_SPACES;
}
/* the cable link is not in step: the details box is the connection card */
static int sd_lost(void)
{
    return fc_live() && bp_mode != BP_MODE_LOCAL && bp_link_state() != BP_L_PLAY;
}
/* the seat whose turn it is (the bidder, in an auction) */
static int sd_turn(void) { return sd_auc() ? bp_game.auc_turn : bp_game.active; }
/* a sprite that is skipped outright when it is off the strip */
static void sd_spr(int x, int y, int id, int h)
{
    if (id >= 0 && bx_size(id) && y + h > fc_sy1 && y <= fc_sy2) bx_spr(x, y, id);
}
/* one box's frame: the ground, then FULL's scene (scm: MID's, else a kit panel
 * with its title); answers the y of the first row under the title bar */
static int sd_frame(const BPRect *r, int sc, int scm, const char *title)
{
    int bar;
    bar = bp_layout == BP_LAY_FULL ? 14 : bp_layout == BP_LAY_MID ? 12 : 0;
    if (bp_layout == BP_LAY_MID) sc = scm;
    bx_fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, gk_role(GK_R_FACE));
    if (sc && bp_layout != BP_LAY_COMPACT) gk_scene(sc, r->x, r->y, sd_sv, sd_iv);
    else gk_panel(r->x, r->y, sd_w, r->h, bar, title, fc_ft, GK_RAISED | GK_PLEFT);
    return r->y + bar + 2;
}
/* a number into a scene slot */
static void sd_num(int slot, unsigned v) { sd_sv[slot] = os88_utoa(v, sd_nb[slot]); }

/* ---- the players box ------------------------------------------------------- */
/* v and " cr" in fc_fit: the cash well */
static const char *sd_cash(const BPU32 *v)
{
    bp_u32_fmt(fc_fit, v);
    fc_ap(fc_fit, os88_strlen(fc_fit), TD_CR);
    return fc_fit;
}
/* an auction roster row's status word */
static const char *sd_astat(int i)
{
    unsigned b;
    b = 1u << i;
    if (bp_game.auc_passed & b) return TD_PASSED;
    if (bp_game.auc_high == i) return TD_HIGH_BID;
    if (bp_game.auc_turn == i) return TD_BIDDING_NOW;
    return (bp_game.auc_open & b) ? TD_IN : "";
}
/* the title: whose turn it is on THIS PC or the other's, the end of the match,
 * the bidding order; before a match "Players" */
static const char *sd_title(void)
{
    int t, n;
    if (!fc_live()) return TD_PLAYERS;
    if (bp_game.status == BP_ST_OVER) return TD_GAME_OVER;
    if (sd_auc()) return TD_BIDDING_ORDER;
    t = bp_game.active;
    n = 0;
    if (bp_sess_mine(t)) { n = fc_ap(fc_buf, 0, TD_PLAYER_SP); fc_buf[n++] = (char)('1' + t); fc_buf[n] = 0; }
    else n = fc_ap(fc_buf, 0, bp_game.name[t]);
    fc_ap(fc_buf, n, bp_sess_mine(t) ? TD_YOUR_TURN : TD_THEIR_TURN);
    return fc_buf;
}
static void fc_players(void)
{
    const BPRect *r;
    int i, y, rh, x1, ox, big, live, t, hi, cw;
    r = &fc_rp;
    live = fc_live();
    big = bp_layout == BP_LAY_FULL;
    ox = r->x;
    t = sd_turn();
    sd_sv[0] = sd_title();
    sd_sv[1] = TD_NO_BID;
    fc_num[0] = 0;
    if (live && bp_game.status == BP_ST_PLAY) { fc_num[0] = 'R'; os88_utoa(ovl_fc_rnd(), fc_num + 1); }
    sd_sv[2] = fc_num;
    cw = t;                                             /* the seat on the cash row's token */
    if (big && live) {
        if (sd_auc()) { cw = bp_game.auc_high; if (cw != BP_NOBODY) sd_sv[1] = sd_cash(&bp_game.auc_bid); }
        else sd_sv[1] = sd_cash(&bp_game.cash[t]);
    }
    sd_frame(r, SD_S_RP, 0, sd_sv[0]);
    if (bp_layout == BP_LAY_MID && live && fc_num[0]) fc_rtext(ox + sd_w - 6, r->y + 5, fc_num, gk_role(GK_R_TITLETX), fc_fs);
    if (big) {
        if (live && cw == BP_NOBODY) sd_spr(ox + 8, r->y + 22, BP_A2_GAVEL, 24);
        else if (live) sd_spr(ox + 6, r->y + 18, BP_A2_TOKL_FLOPPY + bp_game.token[cw], 32);
        y = r->y + 54; rh = 18;
    } else if (bp_layout == BP_LAY_MID) { y = r->y + 16; rh = 17; }
    else { y = r->y + 2; rh = fc_fh + 3; }
    x1 = ox + (big ? 4 : 2);
    if (!live) { fc_t(x1 + 4, y + 2, TD_NO_MATCH_GAME, 0, fc_fb); return; }
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++, y += rh) {
        hi = (bp_game.status == BP_ST_PLAY && t == i && bp_game.alive[i]);
        sd_sv[0] = bp_game.name[i];
        sd_sv[1] = !bp_game.alive[i] ? TD_OUT : sd_auc() ? sd_astat(i) : bp_u32_fmt(fc_fit, &bp_game.cash[i]);
        sd_iv[0] = hi; sd_iv[1] = bp_game.token[i] + bp_mono * 4; sd_iv[2] = !bp_game.alive[i];
        sd_iv[3] = big && i < bp_game.nseats - 1; sd_iv[4] = bp_game.detained[i] && !sd_auc();
        gk_scene(big ? SD_S_ROW : bp_layout == BP_LAY_MID ? SD_S_ROW + 1 : SD_S_ROW - 2, ox + (big ? 4 : 3), y, sd_sv, sd_iv);
        if (bp_layout == BP_LAY_COMPACT) fc_tok(ox + 5, y + (rh - 7) / 2, i, bp_game.token[i], hi ? 15 : 0);
    }
}

/* ---- the details box ------------------------------------------------------- */
static const char *ovl_fc_plain(int s)
{
    switch (bp_space(s)->kind) {
    case BP_K_BOOT: return TD_START_PASSING_BOOT;
    case BP_K_EVENT: return bp_space(s)->group ? TD_DRAW_A_SYSTEM : TD_DRAW_A_FLOPPY;
    case BP_K_CHARGE: return TD_PAY_THE_BANK;
    case BP_K_REPAIR: return TD_VISITING_IS_SAFE;
    case BP_K_SWAP: return TD_A_FREE_REST;
    case BP_K_CRASH: return TD_GO_TO_THE;
    }
    return "";
}
/* a..d joined into fc_buf */
static const char *ovl_fc_cat(const char *a, const char *b, const char *c, const char *d)
{
    int n;
    n = fc_ap(fc_buf, 0, a);
    n = fc_ap(fc_buf, n, b);
    n = fc_ap(fc_buf, n, c);
    fc_ap(fc_buf, n, d);
    return fc_buf;
}
static const char *fc_lvltxt(int lvl, int mort)
{
    static char b[8];
    if (mort) return TD_MORTGAGED;
    if (lvl >= BP_MAX_LEVEL) return TD_WORKSHOP;
    b[0] = (char)('0' + lvl); os88_strcpy(b + 1, " RAM", 6);
    return b;
}
/* the card of a space (the selected one; the lot in an auction, titled as such) */
static void ovl_sd_det(int s, const char *title)
{
    static const BPByte mul[5] = {5, 15, 30, 45, 60};
    const BPRect *r;
    const BPSpace *sp;
    const char *own;
    int y, i, id, prop, lvl, mort, big, ox, w, cx, cw;
    unsigned base;
    r = &fc_rd;
    sp = bp_space(s);
    prop = sp->prop;
    big = bp_layout == BP_LAY_FULL;
    ox = r->x; w = sd_w;
    own = TD_BANK; lvl = 0; mort = 0;
    if (prop != BP_NOBODY && fc_live() && bp_game.owner[prop] < bp_game.nseats) {
        own = bp_game.name[bp_game.owner[prop]]; lvl = bp_game.level[prop]; mort = bp_game.mortgaged[prop];
    }
    base = sp->rent;
    sd_sv[0] = title ? title : fc_fits(sp->name, fc_ft, w - 14);
    sd_iv[0] = prop != BP_NOBODY; sd_iv[1] = sp->kind == BP_K_COMPUTER; sd_iv[2] = sp->kind == BP_K_UTILITY; sd_iv[3] = prop == BP_NOBODY ? 0 : mort ? 1 : 2;
    if (prop != BP_NOBODY && !(bp_layout == BP_LAY_COMPACT)) {
        sd_num(1, sp->price);
        if (mort) { bp_redeem_cost(s, &fc_u); sd_sv[2] = bp_u32_fmt(fc_buf, &fc_u); }
        else sd_num(2, (unsigned)bp_mortgage_value(s));
        sd_sv[3] = own;
        sd_sv[4] = sp->kind == BP_K_UTILITY ? (mort ? TD_MORTGAGED : "-") : fc_lvltxt(lvl, mort);
        sd_sv[12] = bp_group_name(sp->group);
        sd_num(5, base); sd_num(6, base * 2);
        for (i = 0; i < 5; i++) sd_num(7 + i, base * mul[i]);
    }
    y = sd_frame(r, SD_S_DET, SD_S_DET + 2, sd_sv[0]);
    if (bp_layout == BP_LAY_COMPACT) {                  /* three lines */
        sd_sv[0] = sp->name;
        sd_sv[1] = prop == BP_NOBODY ? ovl_fc_plain(s) : ovl_fc_cat(TD_PRICE, " ", os88_utoa(sp->price, sd_nb[0]), TD_CR);
        sd_sv[2] = "";
        if (prop != BP_NOBODY) {
            fc_ap(fc_fit, fc_ap(fc_fit, fc_ap(fc_fit, fc_ap(fc_fit, 0, TD_OWNER), " "), own), lvl || mort ? fc_lvltxt(lvl, mort) : "");
            sd_sv[2] = fc_fit;
        }
        gk_scene(SD_S_DETC, ox, r->y, sd_sv, sd_iv);
        return;
    }
    if (prop == BP_NOBODY) {                            /* a corner, a card, a charge */
        id = fc_specart(sp, s);
        if (id >= 0 && bx_size(id)) { sd_spr(ox + (w - (int)(bx_size(id) & 255)) / 2, y + 4, id, 40); y += (int)(bx_size(id) >> 8) + 8; }
        fc_ctext(ox, ox + w - 1, y, fc_fits(ovl_fc_plain(s), fc_fs, w - 8), 0, fc_fs);
        if (sp->kind == BP_K_CHARGE) fc_t(ox + 6, y + 11, ovl_fc_cat(TD_CHARGE, os88_utoa(sp->price, sd_nb[0]), TD_CR, ""), 0, fc_fs);
        return;
    }
    cw = big ? 60 : 56;                                 /* the picture column: the LARGE art and its bar */
    cx = ox + 4;
    id = BP_A_LARGE(prop);
    sd_spr(cx + (cw - (int)(bx_size(id) & 255)) / 2, y + (big ? 4 : 2), id, 32);
    i = y + (big ? 40 : 36);
    if (i + 6 > fc_sy1 && i <= fc_sy2) fc_rect(cx + 4, i, cw - 8, 6, fc_gcol[sp->group]);
}

/* ---- the link cards -------------------------------------------------------- */
/* the cable's port and speed: "COM2 - 2400 baud" in fc_buf, else "No port held" */
static const char *sd_port(void)
{
    int p;
    p = bp_ck_port();
    if (!p) return TD_NO_PORT;
    fc_buf[0] = 'C'; fc_buf[1] = 'O'; fc_buf[2] = 'M'; fc_buf[3] = (char)('0' + p);
    fc_ap(fc_buf, 4, TD_BAUD);
    return fc_buf;
}
/* the link's state word: "Disconnected" for a paused link, else the state's name */
static const char *sd_lstate(void)
{
    int st;
    st = bp_link_state();
    return st == BP_L_PAUSED ? TD_DISCONNECTED : st < 8 ? fc_lnk[st] : TD_DIVERGED;
}
/* FULL's cards that replace the details box over the cable: CONNECTION while
 * the link is not in step, OTHER PC LINKED during a trade (MID and COMPACT
 * keep the space's card and show the link as a line in the third box) */
static void ovl_sd_conn(void)
{
    int oth;
    oth = !sd_lost();
    sd_iv[0] = oth;
    sd_sv[4] = oth ? TD_OTHER_PC_LINKED : TD_CONNECTION;
    sd_sv[0] = oth ? TD_CABLE_LINKED : bp_mode == BP_MODE_HOST ? TD_BANKER_HOST : TD_JOINED_PC;
    if (oth && bp_sess_busy()) sd_sv[0] = ovl_fc_cat(TD_CABLE_LINKED, TD_WAITING_X, "", "");
    sd_sv[1] = sd_port();
    sd_sv[2] = sd_lstate();
    sd_sv[3] = bp_link_msg();
    sd_frame(&fc_rd, SD_S_CONN, 0, 0);
}
/* the seat this PC plays and whose computers are shown: the active one when
 * it is ours, else our first living seat */
static int sd_cseat(void)
{
    int i;
    if (!fc_live()) return -1;
    if (bp_sess_mine(bp_game.active)) return bp_game.active;
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++)
        if (bp_game.alive[i] && bp_sess_mine(i)) return i;
    return bp_game.active;
}
/* the third box: YOUR COLLECTION (small pictures of the seat's computers; over
 * the cable a status line under them: the LED and "Cable linked • 2400 baud",
 * or the link's state word while it is down), or on FULL PAIRED SAVE while the
 * link is down */
static void ovl_sd_coll(void)
{
    const BPRect *r;
    int y, ox, w, big, seat, p, n, per, rows, cw, ch, lost, cab;
    r = &fc_rl;
    big = bp_layout == BP_LAY_FULL;
    lost = sd_lost();
    ox = r->x; w = sd_w;
    p = bp_sess_pair();
    cab = bp_mode == BP_MODE_LOCAL ? 0 : bp_sess_busy() ? 2 : bp_mono ? 3 : 1;
    sd_iv[0] = cab; sd_iv[2] = sd_iv[3] = 0;
    sd_sv[0] = TD_NO_PORT;
    sd_sv[5] = TD_YOUR_COLLECTION;
    if (lost && big) {                                  /* paired save: its status, and the button's state */
        sd_iv[0] = 0; sd_iv[1] = p == BP_PAIR_DONE ? GK_B_NORMAL : GK_B_DISABLED; sd_iv[2] = 1; sd_iv[3] = p == BP_PAIR_DONE;
        sd_sv[5] = TD_PAIRED_SAVE;
        sd_sv[0] = p == BP_PAIR_DONE ? TL_PAIRED_SAVE_COMPLETE : p == BP_PAIR_FAIL ? TD_PAIR_FAILED : p == BP_PAIR_BUSY ? TD_PAIR_SAVING : TD_WAITING_LINK;
    } else if (cab) {                                   /* "Cable linked • 2400 baud" */
        fc_ap(fc_buf, fc_ap(fc_buf, 0, TD_CABLE_LINKED), cab == 2 ? TD_WAITING_X : TD_BAUD);
        sd_sv[0] = lost ? sd_lstate() : fc_buf;
    }
    y = sd_frame(r, SD_S_COLL, 0, TD_YOUR_COLLECTION);
    if (lost && big) return;
    seat = sd_cseat();
    if (big) { per = 5; cw = 36; ch = 26; rows = cab ? 1 : 2; }
    else { per = 9; cw = 22; ch = 14; rows = 1; }
    y += big ? 3 : 2;
    n = 0;
    if (seat >= 0)
        for (p = 0; p < BP_PROPS; p++) {
            if (bp_game.owner[p] != seat) continue;
            if (n < per * rows) sd_spr(ox + 6 + (n % per) * cw, y + (n / per) * ch, big ? BP_A_SMALL(p) : fc_mono(BP_A_TINY(p)), big ? 24 : 12);
            n++;
        }
    if (!n) fc_t(ox + 8, y + 4, TD_NO_COMPUTERS, 0, fc_fb);
    else if (n > per * rows) {                          /* more than the room: the count of the rest at the end */
        fc_num[0] = '+'; os88_utoa(n - per * rows, fc_num + 1);
        fc_rtext(ox + w - 7, y + (rows - 1) * ch + (big ? 8 : 1), fc_num, 0, fc_fb);
    }
    if (!big && cab) {                                  /* MID: the link line under the pictures */
        sd_spr(ox + 8, r->y + 30, bp_mono ? BP_A2_M_LED_RED : lost ? BP_A2_LED_RED : cab == 2 ? BP_A2_LED_YELLOW : BP_A2_LED_GREEN, 10);
        fc_t(ox + 24, r->y + 30, fc_fits(sd_sv[0], fc_fb, w - 32), 0, fc_fb);
    }
}
/* COMPACT keeps one line for the link: the round and the state */
static void ovl_sd_clink(void)
{
    int st;
    st = bp_link_state();
    fc_buf[0] = 0;
    if (fc_live()) fc_ap(fc_buf, fc_ap(fc_buf, fc_ap(fc_buf, 0, "R"), os88_utoa(ovl_fc_rnd(), sd_nb[0])), " ");
    fc_ap(fc_buf, os88_strlen(fc_buf), bp_mode == BP_MODE_LOCAL ? TD_SAME_PC : st < 8 ? fc_lnk[st] : "?");
    sd_sv[0] = fc_buf;
    sd_iv[0] = st == BP_L_PAUSED || st == BP_L_BADPEER;
    sd_frame(&fc_rl, 0, 0, 0);
    gk_scene(SD_S_LINKC, fc_rl.x, fc_rl.y, sd_sv, sd_iv);
}

/* ---- the entry points bp_face.c calls ---------------------------------------- */
static void ovl_fc_details(void)
{
    if (bp_layout == BP_LAY_FULL && (sd_lost() || (fc_live() && bp_game.phase == BP_PH_TRADE && bp_mode != BP_MODE_LOCAL))) ovl_sd_conn();
    else if (sd_auc()) ovl_sd_det(bp_game.auc_space, TD_THE_LOT);
    else ovl_sd_det(bp_sel, 0);
}
static void fc_link(void)
{
    if (bp_layout == BP_LAY_COMPACT) ovl_sd_clink();
    else ovl_sd_coll();
}
static void fc_logbox(void)
{
    const BPRect *r;
    int n, i, y, rh, x1, ox, w;
    r = &fc_rg;
    ox = r->x; w = sd_w;
    bx_fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, gk_role(GK_R_FACE));
    gk_box(ox, r->y, w, r->h, bp_layout == BP_LAY_COMPACT ? GK_SUNKEN : GK_SUNKEN2, gk_role(GK_R_FIELD));
    rh = fc_fh + 2;
    x1 = ox + 6;
    n = (r->h - 6) / rh;
    if (n > BP_LOG_LINES) n = BP_LOG_LINES;
    y = r->y + r->h - 4 - rh;
    for (i = 0; i < n; i++, y -= rh)                    /* newest at the bottom */
        if (fc_log[i][0] && y + 13 >= fc_sy1 && y <= fc_sy2) fc_t(x1, y, fc_fits(fc_log[i], fc_fb, w - 12), 0, fc_fb);
}

/* ---- signatures of the sidebar's sections ------------------------------- */
static unsigned fc_hb(unsigned h, const void *p, int n)
{
    const BPByte *q;
    for (q = (const BPByte *)p; n > 0; n--) h = fc_mix(h, *q++);
    return h;
}
static unsigned fc_sigplayers(void)
{
    unsigned h;
    h = fc_live() ? 7 : 3;
    if (!fc_live()) return h;
    h = fc_mix(fc_mix(fc_mix(h, bp_game.round), bp_game.active * 4 + bp_game.status), bp_game.phase);
    h = fc_hb(h, &bp_game.auc_space, 5);                /* the auction: space, open, passed, turn, high */
    h = fc_mix(h, bp_game.auc_bid.lo ^ bp_game.auc_bid.hi);
    h = fc_hb(h, bp_game.name, sizeof(bp_game.name) + 3 * BP_SEATS);    /* names, tokens, machines, alive */
    h = fc_hb(h, bp_game.cash, sizeof(bp_game.cash));
    return fc_hb(h, bp_game.detained, BP_SEATS);
}
/* the link's state, for the cards that show it */
static unsigned sd_sigl(unsigned h)
{
    const char *m;
    h = fc_mix(fc_mix(fc_mix(h, bp_link_state()), bp_sess_busy()), bp_ck_port() * 4 + bp_mode);
    h = fc_mix(h, bp_sess_pair() * 4 + sd_lost());
    if (sd_lost()) for (m = bx_str(bp_link_msg()); *m; m++) h = fc_mix(h, *m);
    return h;
}
static unsigned fc_sigdetails(void)
{
    unsigned h;
    h = fc_mix(bp_sel, fc_live());
    if (!fc_live()) return h;
    h = fc_mix(fc_mix(h, bp_game.phase), bp_game.auc_space);
    h = fc_hb(h, bp_game.owner, 3 * BP_PROPS);          /* owner, level, mortgaged */
    h = fc_hb(h, bp_game.name, sizeof(bp_game.name));
    return bp_mode == BP_MODE_LOCAL ? h : sd_sigl(h);
}
static unsigned fc_siglink(void)
{
    unsigned h;
    h = sd_sigl(fc_live());
    if (bp_layout == BP_LAY_COMPACT) return fc_mix(h, bp_game.round);     /* its one line: round, state */
    if (!fc_live()) return h;
    return fc_hb(fc_mix(h, bp_game.active), bp_game.owner, BP_PROPS);
}
/* ---- the log ------------------------------------------------------------ */
void bp_log_add(const char *line)
{
    int i;
    for (i = BP_LOG_LINES - 1; i > 0; i--) os88_strcpy(fc_log[i], fc_log[i - 1], BP_LOG_LEN + 1);
    os88_strcpy(fc_log[0], bx_str(line), BP_LOG_LEN + 1);
    fc_logver++;
    fc_dirty |= BP_D_LOG;
}
const char *bp_log_line(int i) { if (i < 0 || i >= BP_LOG_LINES) return ""; return fc_log[i]; }
