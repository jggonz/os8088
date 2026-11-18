/* Byteopoly - the sidebar (SPEC.md 131.8.1, 131.8.4): the players, details,
 * link and log boxes, their signatures, the sidebar's share of the layout and
 * the log ring. Split out of bp_face.c so the board and the sidebar can be
 * owned apart; one translation unit with it (byteoply.c includes this file
 * right after bp_face.c, which declares the entry points), so the fc_
 * helpers, state and rects are shared as before. */

static const char *const fc_lnk[8] = {TD_OFF, TD_SEARCHING, TD_PEER_FOUND, TD_BAD_PEER,
    TD_LOBBY, TD_LINKED, TD_PAUSED, TD_RESYNC};
static char fc_log[BP_LOG_LINES][BP_LOG_LEN + 1];

/* the sidebar's four boxes tile the column exactly (SPEC.md 131.8.1) */
static void fc_sidelayout(void)
{
    int hp, hl, rem, side_h, rh, th;
    side_h = bp_r_side.h;
    rh = fc_fh + 3;
    th = fc_sh + 4;
    hp = (bp_layout == BP_LAY_COMPACT ? 0 : th) + 4 * rh + 3;
    hl = (bp_layout == BP_LAY_FULL ? 2 * rh : rh) + 3;
    rem = side_h - hp - hl;
    fc_rp.x = fc_rd.x = fc_rl.x = fc_rg.x = bp_r_side.x;
    fc_rp.w = fc_rd.w = fc_rl.w = fc_rg.w = bp_r_side.w;
    fc_rp.y = bp_r_side.y; fc_rp.h = hp;
    fc_rd.y = fc_rp.y + hp;
    fc_rd.h = bp_layout == BP_LAY_FULL ? rem * 3 / 5 : bp_layout == BP_LAY_MID ? rem * 14 / 25 : rem / 2;
    fc_rl.y = fc_rd.y + fc_rd.h; fc_rl.h = hl;
    fc_rg.y = fc_rl.y + hl; fc_rg.h = side_h - hp - fc_rd.h - hl;
}

/* the round shown: completed + 1, held at a Quick Match's limit */
static unsigned ovl_fc_rnd(void)
{
    unsigned n;
    n = bp_game.round + 1;
    if (bp_game.rounds_limit && n > bp_game.rounds_limit) n = bp_game.rounds_limit;
    return n;
}
static void fc_head(const BPRect *r, const char *title, const char *right)
{
    int th;
    th = fc_sh + 4;
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + th - 1, 1);
    bx_text(r->x + 4, r->y + 2, title, 15, fc_fs);
    if (right) fc_rtext(r->x + r->w - 5, r->y + 2, right, 15, fc_fs);
}
static void fc_players(void)
{
    const BPRect *r;
    int i, y, rh, x2, th, cw;
    unsigned hi;
    r = &fc_rp;
    rh = fc_fh + 3;
    th = bp_layout == BP_LAY_COMPACT ? 0 : fc_sh + 4;
    x2 = r->x + r->w - 1;
    if (th) {
        if (fc_live()) {
            fc_buf[0] = 0; i = fc_ap(fc_buf, 0, TD_ROUND);
            fc_ap(fc_buf, i, fc_dec(ovl_fc_rnd()));
            fc_head(r, fc_buf, 0);
            if (bp_game.d1 && bp_game.rolled) {
                fc_num[0] = (char)('0' + bp_game.d1); fc_num[1] = '+'; fc_num[2] = (char)('0' + bp_game.d2); fc_num[3] = 0;
                fc_rtext(x2 - 5, r->y + 2, fc_num, 15, fc_fs);
            }
        } else fc_head(r, TD_PLAYERS, 0);
    } else fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    y = r->y + th + 1;
    if (!fc_live()) { fc_t(r->x + 6, y + 1, TD_NO_MATCH_GAME, 0, fc_fb); return; }
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++, y += rh) {
        hi = (bp_game.status == BP_ST_PLAY && bp_game.active == i && bp_game.alive[i]);
        if (hi) bx_fill(r->x + 1, y, x2 - 1, y + rh - 1, 1);
        fc_tok(r->x + 4, y + (rh - 7) / 2, i, bp_game.token[i], hi ? 15 : 0);
        fc_t(r->x + 15, y + 1, bp_game.name[i], hi ? 15 : 0, fc_fb);
        if (!bp_game.alive[i]) {
            fc_strike(r->x + 15, y + rh / 2, bx_twidth(bp_game.name[i], fc_fb));
            fc_rtext(x2 - 4, y + 1, TD_OUT, 0, fc_fb);
        } else {
            fc_rtext(x2 - 4, y + 1, bp_u32_fmt(fc_fit, &bp_game.cash[i]), hi ? 15 : 0, fc_fb);
            if (bp_game.detained[i]) {
                cw = bx_twidth(bp_u32_fmt(fc_fit, &bp_game.cash[i]), fc_fb);
                fc_rect(x2 - 4 - cw - 12, y + 2, 9, rh - 4, hi ? 15 : 0);
                fc_t(x2 - 4 - cw - 11, y + 1, "R", hi ? 1 : 15, BP_F_TEXT08);
            }
        }
    }
}
/* one "label ....... value" line */
static void ovl_fc_kv(int x1, int x2, int y, const char *k, const char *v)
{
    fc_t(x1, y, k, 0, fc_fb);
    fc_rtext(x2, y, v, 0, fc_fb);
}
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
/* a..d joined into fc_buf; numbers come from ovl_fc_nn's two slots */
static char fc_n[2][8];
static const char *ovl_fc_nn(int k, unsigned v) { return os88_utoa(v, fc_n[k]); }
static const char *ovl_fc_cat(const char *a, const char *b, const char *c, const char *d)
{
    int n;
    fc_buf[0] = 0;
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
static void ovl_fc_details(void)
{
    static const BPByte mul[5] = {5, 15, 30, 45, 60};
    static const char *const rl[5] = {TD_1_RAM, TD_2_RAM, TD_3_RAM, TD_4_RAM, TD_WORKSHOP};
    const BPRect *r;
    const BPSpace *sp;
    const char *own;
    int x1, x2, y, rh, i, id, prop, lvl, mort, cw, cp;
    unsigned base;
    r = &fc_rd;
    sp = bp_space(bp_sel);
    prop = sp->prop;
    rh = fc_fh + 3;
    cp = bp_layout == BP_LAY_COMPACT;
    x1 = r->x + 5; x2 = r->x + r->w - 6;
    own = TD_BANK; lvl = 0; mort = 0;
    if (prop != BP_NOBODY && fc_live() && bp_game.owner[prop] < bp_game.nseats) {
        own = bp_game.name[bp_game.owner[prop]]; lvl = bp_game.level[prop]; mort = bp_game.mortgaged[prop];
    }
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    y = r->y + 2;
    if (!cp) {                                          /* the title bar: the full name */
        bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + bx_fonth(fc_ft) + 3, 1);
        fc_t(x1, r->y + 2, fc_fits(sp->name, fc_ft, r->w - 10), 15, fc_ft);
        y += bx_fonth(fc_ft) + 3;
    }
    if (prop == BP_NOBODY) {
        id = fc_specart(sp, bp_sel);
        if (cp) { fc_t(x1, y, sp->name, 0, fc_fb); y += rh; }
        else if (id >= 0 && bx_size(id)) { bx_spr(r->x + (r->w - (int)(bx_size(id) & 255)) / 2, y, id); y += (int)(bx_size(id) >> 8) + 4; }
        fc_t(x1, y, fc_fits(ovl_fc_plain(bp_sel), fc_fb, r->w - 10), 0, fc_fb);
        if (sp->kind == BP_K_CHARGE) fc_t(x1, y + rh, ovl_fc_cat(TD_CHARGE, ovl_fc_nn(0, sp->price), TD_CR, ""), 0, fc_fb);
        return;
    }
    base = sp->rent;
    if (cp) {                                           /* COMPACT: three lines */
        fc_t(x1, y, ovl_fc_cat(sp->name, "  ", ovl_fc_nn(0, sp->price), TD_CR), 0, fc_fb);
        fc_t(x1, y + rh, sp->kind == BP_K_UTILITY ? TD_RENT_4X_DICE :
                 ovl_fc_cat(TD_RENT, ovl_fc_nn(0, base), TD_SET, ovl_fc_nn(1, base * 2)), 0, fc_fb);
        fc_t(x1, y + rh * 2, fc_fits(ovl_fc_cat(TD_OWNER, own, " ", lvl || mort ? fc_lvltxt(lvl, mort) : ""), fc_fb, r->w - 10), 0, fc_fb);
        return;
    }
    id = bp_layout == BP_LAY_FULL ? BP_A_LARGE(prop) : -1;      /* top block; LARGE art to its right on FULL */
    cw = r->w - 10;
    if (id >= 0 && bx_size(id) && (int)(bx_size(id) & 255) <= r->w / 2) {
        bx_spr(x2 - (int)(bx_size(id) & 255) + 1, y, id);
        cw -= (int)(bx_size(id) & 255) + 4;
    }
    ovl_fc_kv(x1, x1 + cw - 1, y, TD_PRICE, ovl_fc_cat(ovl_fc_nn(0, sp->price), TD_CR, "", ""));
    if (mort) bp_redeem_cost(bp_sel, &fc_u);
    ovl_fc_kv(x1, x1 + cw - 1, y + rh, mort ? TD_REDEEM : TD_MORTGAGE,
              ovl_fc_cat(mort ? bp_u32_fmt(fc_fit, &fc_u) : ovl_fc_nn(0, (unsigned)bp_mortgage_value(bp_sel)), TD_CR, "", ""));
    ovl_fc_kv(x1, x1 + cw - 1, y + rh * 2, TD_OWNER_2, own);
    ovl_fc_kv(x1, x1 + cw - 1, y + rh * 3, TD_LEVEL, sp->kind == BP_K_UTILITY ? (mort ? TD_MORTGAGED : "-") : fc_lvltxt(lvl, mort));
    y += rh * 4 + 3;
    if (sp->kind == BP_K_UTILITY) {
        fc_t(x1, y, TD_RENT_4_X, 0, fc_fb);
        fc_t(x1, y + rh, TD_10_X_WITH, 0, fc_fb);
        return;
    }
    bx_fill(r->x + 1, y - 2, x2 + 4, y - 2, 0);         /* the rent schedule, two columns (131.2.3) */
    cw = (r->w - 10) / 2;
    ovl_fc_kv(x1, x1 + cw - 6, y, TD_BASE, ovl_fc_cat(ovl_fc_nn(0, base), "", "", ""));
    ovl_fc_kv(x1 + cw + 4, x2, y, TD_SET_2, ovl_fc_cat(ovl_fc_nn(0, base * 2), "", "", ""));
    for (i = 0; i < 5; i++) {
        y += (i & 1) == 0 ? rh : 0;
        ovl_fc_kv(i == 4 ? x1 : x1 + (i & 1) * (cw + 4), i == 4 || (i & 1) ? x2 : x1 + cw - 6, y, rl[i],
                  ovl_fc_cat(ovl_fc_nn(0, base * mul[i]), "", "", ""));
    }
}
static void fc_link(void)
{
    static char pre[16], dd[5];
    const BPRect *r;
    int st, loc;
    r = &fc_rl;
    fc_frame(r->x, r->y, r->w, r->h, fc_gr);
    st = bp_link_state();
    loc = bp_mode == BP_MODE_LOCAL;
    pre[0] = 0;
    if (bp_layout != BP_LAY_FULL && fc_live()) {    /* round and dice ride the link row off FULL */
        dd[0] = ' '; dd[1] = (char)('0' + bp_game.d1); dd[2] = '+'; dd[3] = (char)('0' + bp_game.d2); dd[4] = 0;
        os88_strcpy(pre, ovl_fc_cat("R", ovl_fc_nn(0, ovl_fc_rnd()), bp_game.rolled && bp_game.d1 ? dd : "", " "), sizeof(pre));
    }
    fc_t(r->x + 5, r->y + 2, ovl_fc_cat(pre, loc ? TD_SAME_PC : st < 8 ? fc_lnk[st] : "?", bp_sess_busy() ? TD_X : "", ""), 0, fc_fb);
    if (st == BP_L_PAUSED || st == BP_L_BADPEER) fc_strike(r->x + 5, r->y + 1, r->w - 10);
    if (bp_layout == BP_LAY_FULL) fc_t(r->x + 5, r->y + 5 + fc_fh, fc_fits(loc ? TD_CABLE_OFF : bp_link_msg(), fc_fb, r->w - 10), 0, fc_fb);
}
static void fc_logbox(void)
{
    const BPRect *r;
    int n, i, y, rh, th, x1;
    r = &fc_rg;
    rh = fc_fh + 2;
    if (bp_layout == BP_LAY_COMPACT) { fc_frame(r->x, r->y, r->w, r->h, fc_gr); th = 0; }
    else { fc_head(r, TD_LOG, 0); th = fc_sh + 4; }
    x1 = r->x + 4;
    n = (r->h - th - 3) / rh;
    if (n > BP_LOG_LINES) n = BP_LOG_LINES;
    y = r->y + r->h - 2 - rh;
    for (i = 0; i < n; i++, y -= rh)                    /* newest at the bottom */
        if (fc_log[i][0] && y + 13 >= fc_sy1 && y <= fc_sy2) fc_t(x1, y, fc_fits(fc_log[i], fc_fb, r->w - 8), 0, fc_fb);
}

/* ---- signatures of the sidebar's sections ------------------------------- */
static unsigned fc_hb(unsigned h, const void *p, int n)
{
    const BPByte *q;
    for (q = (const BPByte *)p; n > 0; n--) h = fc_mix(h, *q++);
    return h;
}
static unsigned fc_sigscal(unsigned h)
{
    return fc_mix(fc_mix(fc_mix(h, bp_game.round), bp_game.d1 * 8 + bp_game.d2 + bp_game.rolled * 64), bp_game.active * 4 + bp_game.status);
}
static unsigned fc_sigplayers(void)
{
    unsigned h;
    h = fc_live() ? 7 : 3;
    if (!fc_live()) return h;
    h = fc_sigscal(h);
    h = fc_hb(h, bp_game.name, sizeof(bp_game.name));
    h = fc_hb(h, bp_game.cash, sizeof(bp_game.cash));
    h = fc_hb(h, bp_game.alive, BP_SEATS);
    h = fc_hb(h, bp_game.detained, BP_SEATS);
    return fc_hb(h, bp_game.token, BP_SEATS);
}
static unsigned fc_sigdetails(void)
{
    unsigned h;
    h = fc_mix(bp_sel, fc_live());
    if (!fc_live()) return h;
    h = fc_hb(h, bp_game.owner, BP_PROPS);
    h = fc_hb(h, bp_game.level, BP_PROPS);
    h = fc_hb(h, bp_game.mortgaged, BP_PROPS);
    return fc_hb(h, bp_game.name, sizeof(bp_game.name));
}
static unsigned fc_siglink(void)
{
    unsigned h;
    const char *m;
    h = fc_mix(fc_mix(bp_link_state(), bp_sess_busy()), bp_mode);
    if (bp_layout != BP_LAY_FULL) h = fc_sigscal(h);
    for (m = bx_str(bp_link_msg()); *m; m++) h = fc_mix(h, *m);
    return h;
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
