/* Byteopoly - the New Game screen and the cable lobby (SPEC.md 131.9.2,
 * 131.9.3). Lane: lobby. Drawn over the whole content (BP_PN_FULL) while
 * bp_app.c's mode says so. Every function is UI-task code reached by menu,
 * key, click or the link: overlay (ovl_*), SPEC.md 131.11.
 *
 * The screen is DATA: art/scenes-lobby.txt (SPEC.md 133.6) draws every frame,
 * table, label and picture, and this file fills its slots (ovl_su_slots).
 * Every widget's hit rect is read back from the scene's own #tag
 * (gk_scene_rect; tag = widget id + 1), so the draw, the click and the key
 * paths cannot disagree about where a widget is. */
#include "art2.h"
static void ovl_bp_ap_back(void);
static void ovl_bp_ap_guard(int act);
static void ovl_bp_ap_started(void);

/* widget ids; the order is the focus order. Tag = id + 1 */
#define SU_TAB    0     /* 0..2 Same PC, Host Cable, Join Cable */
#define SU_CNTM   3
#define SU_CNTP   4
#define SU_NAME   5     /* 5..8 */
#define SU_TOK    9     /* 9..12 */
#define SU_MACH   13    /* 13..16 */
#define SU_READY  17
#define SU_RULE   18    /* 18 Classic, 19 Quick Match, 20 its rounds (21 unused) */
#define SU_PORT   22
#define SU_LINK   23
#define SU_START  24
#define SU_BACK   25
#define SU_N      26
/* pseudo tags of the scene: the status line, and the panels (damage) */
#define SUT_STAT  27
#define SUT_ROST  28
#define SUT_RULES 29
#define SUT_LINK  30

static BPSetup su_cfg;
static BPByte su_tab, su_foc, su_port, su_edit, su_en, su_ready, su_sent, su_fresh;
static char su_eb[BP_NAMELEN + 2];
static char su_msg[41];
static char su_tb[104];
static const char *su_sv[25];
static int su_iv[27], su_sc, su_ox, su_oy;
static BPRect su_rc;

static const char *su_toks[] = {TS_FLOPPY, TS_MOUSE, TS_JOYSTICK, TS_CASSETTE};
static const char *su_ports[] = {TS_PORT_AUTO, TS_PORT_COM1, TS_PORT_COM2};
static const char *su_hints[] = {TS_TAB_NEXT, TS_ARROWS_CHANGE, TS_ENTER_EDIT,
    TS_S_START, TS_ESC_BACK, 0};

/* the scene's origin and id for this layout (131.9.2): FULL has its own,
 * MID and COMPACT share the short one, centred across and a third down */
static void ovl_su_geom(void)
{
    static int wh[2];
    su_sc = bp_layout == BP_LAY_FULL ? BP_A2_SC_LOBBY : BP_A2_SC_LOBBY_C;
    gk_scene_size(su_sc, wh);
    su_ox = bp_r_center.x;
    if (bp_r_center.w > wh[0]) su_ox += (bp_r_center.w - wh[0]) / 2;
    su_oy = bp_r_center.y;
    if (bp_r_center.h > wh[1]) su_oy += (bp_r_center.h - wh[1]) / (bp_layout == BP_LAY_FULL ? 2 : 3);
}

/* the rect of a scene tag, in screen coordinates; 0 when the scene lacks it */
static int ovl_su_tagrect(int tag, BPRect *r)
{
    ovl_su_geom();
    if (!gk_scene_rect(su_sc, tag, (int *)r)) return 0;     /* BPRect is four ints, x y w h */
    r->x += su_ox; r->y += su_oy;
    return 1;
}

static void ovl_su_rect(int id, BPRect *r)
{
    r->x = r->y = r->w = r->h = 0;
    ovl_su_tagrect(id + 1, r);
    if (id == SU_RULE || id == SU_RULE + 1) r->w += 70;     /* the label is part of the radio */
}

/* name the damage (SPEC.md 131.8.5): a widget's rect, a tagged panel's, or
 * (tag 0) the whole screen; the pad covers the focus ring */
static void ovl_su_dt(int tag, int pad)
{
    if (tag) {
        if (!ovl_su_tagrect(tag, &su_rc)) return;
        su_rc.x -= pad; su_rc.y -= pad; su_rc.w += 2 * pad; su_rc.h += 2 * pad;
    } else { su_rc.x = bp_r_center.x; su_rc.y = bp_r_center.y; su_rc.w = bp_r_center.w; su_rc.h = bp_r_center.h; }
    bp_face_dirty_rect(&su_rc);
}

static void ovl_su_say(const char *s)
{
    os88_strcpy(su_msg, bx_str(s), sizeof(su_msg));
    ovl_su_dt(SUT_STAT, 0);
}

static void ovl_su_defname(int s)
{
    os88_strcpy(su_cfg.name[s], "Player 1", BP_NAMELEN + 1);
    su_cfg.name[s][7] = (char)('1' + s);
}

static void ovl_su_defaults(void)
{
    int i;
    os88_memset(&su_cfg, 0, sizeof(su_cfg));
    su_cfg.nseats = 2;
    for (i = 0; i < BP_SEATS; i++) { ovl_su_defname(i); su_cfg.token[i] = (BPByte)i; }
    su_tab = 0; su_foc = SU_START; su_port = 0; su_edit = 0;
    su_ready = 0; su_sent = 0; su_msg[0] = 0;
}

static int ovl_su_vis(int id)
{
    if (id <= 2) return 1;
    if (id <= SU_CNTP) return su_tab != 2;
    if (id < SU_READY) {
        if (((id - SU_NAME) & 3) >= su_cfg.nseats) return 0;
        if (id >= SU_MACH) return su_tab != 0;
        return 1;
    }
    if (id == SU_READY || id == SU_PORT || id == SU_LINK) return su_tab != 0;
    if (id == SU_START) return su_tab != 2;
    if (id == SU_RULE + 3) return 0;
    if (id == SU_RULE + 2) return su_cfg.rules == BP_RULES_QUICK;
    return 1;
}

static int ovl_su_ena(int id)
{
    int lobby;
    lobby = bp_ap_mode == BP_M_LOBBY;
    if (id <= 2) return !lobby;
    if (id == SU_BACK || id == SU_LINK) return 1;
#ifdef BP_SETUP_DEMO
    if (id == SU_START) return 1;
#endif
    if (id == SU_PORT) return !lobby;
    if (id == SU_READY) return lobby && bp_link_state() == BP_L_LOBBY;
    if (id == SU_START)
        return su_tab == 0 || (lobby && bp_link_state() == BP_L_LOBBY);
    if (su_tab == 2) return 0;      /* roster, counts, rules: a client only reads */
    if (id == SU_CNTP) return su_cfg.nseats < BP_SEATS;
    if (id == SU_CNTM) return su_cfg.nseats > 2;
    return 1;
}

static int ovl_su_mine(int s) { return su_cfg.machine[s] == (su_tab == 2 ? 1 : 0); }

/* the edit buffer -> the name; an empty name reverts to its default */
static void ovl_su_endedit(int keep)
{
    int s;
    if (!su_edit) return;
    s = su_edit - 1;
    if (keep) {
        su_eb[su_en] = 0;
        if (su_en) os88_strcpy(su_cfg.name[s], su_eb, BP_NAMELEN + 1);
        else ovl_su_defname(s);
    }
    su_edit = 0;
    ovl_su_dt(SU_NAME + s + 1, 3);
}

/* a host's change in the lobby goes out whole, and unreadies both sides */
static void ovl_su_push(void)
{
    int e;
    if (bp_ap_mode != BP_M_LOBBY || su_tab != 1 || bp_link_state() != BP_L_LOBBY) return;
    e = bp_lobby_set(&su_cfg);
    if (e != BP_OK) ovl_su_say(bp_err_text(e));
    if (su_ready) { su_ready = 0; bp_lobby_ready(0); ovl_su_dt(0, 0); }
}

static int ovl_su_check(void)
{
    int i, j, e, here;
    for (i = 0; i < su_cfg.nseats; i++)
        for (j = i + 1; j < su_cfg.nseats; j++)
            if (su_cfg.token[i] == su_cfg.token[j]) {
                ovl_su_say(TS_EACH_PLAYER_NEEDS);
                return 0;
            }
    if (su_tab != 0) {
        here = 0;
        for (i = 0; i < su_cfg.nseats; i++) here += su_cfg.machine[i];
        if (here == 0 || here == su_cfg.nseats) {
            ovl_su_say(TS_EACH_PC_NEEDS);
            return 0;
        }
    }
    e = bp_setup_check(&su_cfg);
    if (e != BP_OK) { ovl_su_say(bp_err_text(e)); return 0; }
    return 1;
}

/* the app calls this once any "discard?" question is answered */
static void ovl_su_go(void)
{
    int e, i;
    if (su_tab == 0) {
        for (i = 0; i < BP_SEATS; i++) su_cfg.machine[i] = 0;
        e = bp_sess_new(&su_cfg);
        if (e != BP_OK) { ovl_su_say(bp_err_text(e)); bp_sound(BP_SND_ERROR); return; }
        if (bp_ap_mode != BP_M_GAME) ovl_bp_ap_started();
    } else {
        e = bp_lobby_start();
        if (e != BP_OK) { ovl_su_say(bp_err_text(e)); bp_sound(BP_SND_ERROR); return; }
        ovl_su_say(TS_STARTING);
    }
}

static void ovl_su_start(void)
{
    if (!ovl_su_ena(SU_START)) { ovl_su_say(TS_THE_LINK_IS); return; }
    if (!ovl_su_check()) return;
    ovl_bp_ap_guard(BP_ACT_START);
}

static void ovl_su_link(void)
{
    int e;
    const char *m;
    ovl_su_dt(0, 0);
    if (bp_ap_mode == BP_M_LOBBY) {
        bp_link_leave();
        bp_ap_mode = BP_M_SETUP; su_ready = 0; su_sent = 0;
        ovl_su_say(TS_LINK_CLOSED);
        return;
    }
    e = su_tab == 1 ? bp_link_host(su_port) : bp_link_join(su_port);
    if (e == BP_OK) { bp_ap_mode = BP_M_LOBBY; su_sent = 0; su_ready = 0; su_msg[0] = 0; return; }
    m = bp_ck_msg();
    ovl_su_say(m && m[0] ? m : bp_err_text(e));
    bp_sound(BP_SND_ERROR);
}

/* the link changed (bp_ui_link) or a tick passed in the lobby */
static void ovl_su_sync(void)
{
    int st;
    if (bp_ap_mode != BP_M_LOBBY) return;
    ovl_su_dt(0, 0);                    /* the LED, the status, the ticks: all follow the link */
    st = bp_link_state();
    if (st == BP_L_OFF) {
        bp_ap_mode = BP_M_SETUP; su_ready = 0; su_sent = 0;
        ovl_su_say(bp_link_msg());
        return;
    }
    if (st != BP_L_LOBBY) { su_sent = 0; return; }
    if (su_tab == 1 && !su_sent) { su_sent = 1; bp_lobby_set(&su_cfg); }
    else if (su_tab == 2 && bp_lobby_setup()) os88_memcpy(&su_cfg, bp_lobby_setup(), sizeof(su_cfg));
}

static void ovl_su_tab(int k)
{
    int i, n;
    if (k < 0 || k > 2 || bp_ap_mode == BP_M_LOBBY) return;
    su_tab = (BPByte)k;
    n = 0;
    for (i = 0; i < su_cfg.nseats; i++) n += su_cfg.machine[i];
    if (k == 0) for (i = 0; i < BP_SEATS; i++) su_cfg.machine[i] = 0;
    else if (n == 0 || n == su_cfg.nseats) su_cfg.machine[1] = 1;
    su_msg[0] = 0;
    ovl_su_dt(0, 0);
}

/* one widget's action. dir 0 = click/Enter/Space, +1/-1 = Right/Left */
static void ovl_su_act(int id, int dir)
{
    int k, n, was;
    was = su_msg[0];
    if (id <= 2) { ovl_su_tab(dir ? su_tab + dir : id); return; }
    if (id == SU_CNTM || id == SU_CNTP) {
        n = su_cfg.nseats + (id == SU_CNTP ? 1 : -1);
        if (n < 2 || n > BP_SEATS) return;
        su_cfg.nseats = (BPByte)n;
        ovl_su_dt(SUT_ROST, 3);
        ovl_su_dt(id + 1, 3);
        ovl_su_dt(id == SU_CNTP ? SU_CNTM + 1 : SU_CNTP + 1, 3);
        ovl_su_push();
    } else if (id >= SU_NAME && id < SU_TOK) {
        k = id - SU_NAME;
        su_edit = (BPByte)(k + 1);
        os88_strcpy(su_eb, su_cfg.name[k], sizeof(su_eb));
        su_en = (BPByte)os88_strlen(su_eb);
        su_fresh = 1;           /* the first typed character replaces the name */
        ovl_su_dt(id + 1, 3);
    } else if (id >= SU_TOK && id < SU_MACH) {
        k = id - SU_TOK;
        su_cfg.token[k] = (BPByte)((su_cfg.token[k] + (dir < 0 ? 3 : 1)) & 3);
        ovl_su_dt(id + 1, 3);
        ovl_su_push();
    } else if (id >= SU_MACH && id < SU_READY) {
        k = id - SU_MACH;
        su_cfg.machine[k] ^= 1;
        ovl_su_dt(SUT_ROST, 3);
        ovl_su_push();
    } else if (id >= SU_RULE && id < SU_PORT) {
        if (id == SU_RULE + 2) {
            su_cfg.rounds = (BPByte)(su_cfg.rounds >= 30 ? 10 : su_cfg.rounds + 10);
        } else {                    /* Classic or Quick Match; Quick keeps its rounds, 10 at first */
            k = dir ? su_cfg.rules == BP_RULES_CLASSIC : id == SU_RULE + 1;
            su_cfg.rules = k ? BP_RULES_QUICK : BP_RULES_CLASSIC;
            su_cfg.rounds = (BPByte)(k ? (su_cfg.rounds ? su_cfg.rounds : 10) : 0);
        }
        ovl_su_dt(SUT_RULES, 3);
        ovl_su_push();
    } else if (id == SU_PORT) {
        su_port = (BPByte)((su_port + 1) % 3);
        ovl_su_dt(SUT_LINK, 3);
    } else if (id == SU_LINK) {
        ovl_su_link();
    } else if (id == SU_READY) {
        su_ready ^= 1;
        k = bp_lobby_ready(su_ready);
        if (k != BP_OK) { su_ready = 0; ovl_su_say(bp_err_text(k)); }
        ovl_su_dt(0, 0);
    } else if (id == SU_START) {
        ovl_su_start();
    } else if (id == SU_BACK) {
        ovl_bp_ap_back();
    }
    if (id != SU_START && id != SU_LINK && id != SU_BACK) {
        su_msg[0] = 0;
        if (was) ovl_su_dt(SUT_STAT, 0);
    }
}

static void ovl_su_setfoc(int id)
{
    ovl_su_dt(su_foc + 1, 3);
    su_foc = (BPByte)id;
    ovl_su_dt(id + 1, 3);
}

static void ovl_su_focus(int d)
{
    int i, id;
    id = su_foc;
    for (i = 0; i < SU_N; i++) {
        id += d;
        if (id < 0) id = SU_N - 1;
        if (id >= SU_N) id = 0;
        if (ovl_su_vis(id) && ovl_su_ena(id)) { ovl_su_setfoc(id); return; }
    }
}

static int ovl_su_cycler(int id)
{
    if (id <= 2) return 1;
    if (id >= SU_TOK && id < SU_READY) return 1;
    return id >= SU_RULE && id <= SU_PORT;
}

static int ovl_su_key(int ascii, int scan)
{
    if (su_edit) {
        if (ascii == 27) { su_edit = 0; ovl_su_dt(SU_NAME + 1, 0); ovl_su_dt(SU_NAME + 2, 0); ovl_su_dt(SU_NAME + 3, 0); ovl_su_dt(SU_NAME + 4, 0); }
        else if (ascii == 13) { ovl_su_endedit(1); ovl_su_push(); }
        else if (ascii == 8) { if (su_en) su_en--; su_fresh = 0; }
        else if (ascii >= 32 && ascii < 127) {
            if (su_fresh) { su_en = 0; su_fresh = 0; }
            if (su_en < BP_NAMELEN) su_eb[su_en++] = (char)ascii;
        }
        if (su_edit) ovl_su_dt(SU_NAME + su_edit, 3);
        return 1;
    }
    if (ascii == 27) { ovl_bp_ap_back(); return 1; }
    if (ascii == 's' || ascii == 'S') { ovl_su_start(); return 1; }
    if (ascii == 9 || (ascii == 0 && scan == 0x50)) { ovl_su_focus(1); return 1; }
    if (ascii == 0 && (scan == 0x48 || scan == 0x0F)) { ovl_su_focus(-1); return 1; }
    if (ascii == 0 && (scan == 0x4B || scan == 0x4D)) {
        if (ovl_su_cycler(su_foc)) ovl_su_act(su_foc, scan == 0x4D ? 1 : -1);
        else ovl_su_focus(scan == 0x4D ? 1 : -1);
        return 1;
    }
    if (ascii == 13 || ascii == ' ') { ovl_su_act(su_foc, 0); return 1; }
    return 0;
}

static int ovl_su_click(int x, int y)
{
    int id;
    for (id = 0; id < SU_N; id++) {
        if (!ovl_su_vis(id) || !ovl_su_ena(id)) continue;
        ovl_su_rect(id, &su_rc);
        if (x >= su_rc.x && x < su_rc.x + su_rc.w && y >= su_rc.y && y < su_rc.y + su_rc.h) {
            if (su_edit) ovl_su_endedit(1);
            if (su_foc != id) ovl_su_setfoc(id);
            ovl_su_act(id, 0);
            return 1;
        }
    }
    if (su_edit) ovl_su_endedit(1);
    return 1;
}

/* ---- the slots the scene reads (the list is at the top of the scene file) -- */
/* the state a scene button takes: flat in its panel, navy when focused, and
 * the kit's greyed one when it cannot be used */
static int ovl_su_bst(int id)
{
    return 0x10 | (!ovl_su_ena(id) ? 3 : su_foc == id ? 1 : 0);
}

/* "1 player" / "3 players" at p; answers the end */
static char *ovl_su_cnt(char *p, int n)
{
    *p++ = (char)('0' + n);
    os88_strcpy(p, bx_str(n == 1 ? TS_PLAYER_ONE : TS_PLAYER_MANY), 12);
    return p + os88_strlen(p);
}

static char *ovl_su_cat(char *p, const char *s)
{
    os88_strcpy(p, bx_str(s), 28);
    return p + os88_strlen(p);
}

static void ovl_su_slots(void)
{
    int s, i, st, ps, here, lobby, pr;
    char *p;
    st = bp_link_state();
    lobby = bp_ap_mode == BP_M_LOBBY;
    pr = bp_lobby_peer_ready();
#ifdef BP_SETUP_DEMO                    /* look-only: a cable lobby with a peer (never committed defined) */
    st = BP_L_LOBBY; lobby = 1; pr = 1; su_ready = 1;
    if (!su_tab) su_tab = 1;
    su_cfg.nseats = 4;
    for (i = 0; i < 4; i++) { su_cfg.machine[i] = (BPByte)(i & 1); su_cfg.token[i] = (BPByte)i; }
    os88_strcpy(su_cfg.name[0], "George", 9); os88_strcpy(su_cfg.name[1], "Ada", 9);
    os88_strcpy(su_cfg.name[2], "Alan", 9); os88_strcpy(su_cfg.name[3], "Grace", 9);
#endif
    for (i = 0; i < 3; i++) su_iv[i] = su_tab == i;
    su_iv[3] = ovl_su_bst(SU_START); su_iv[4] = ovl_su_bst(SU_BACK);
    su_iv[5] = ovl_su_bst(SU_PORT); su_iv[6] = ovl_su_bst(SU_LINK);
    su_iv[7] = ovl_su_bst(SU_CNTM); su_iv[8] = ovl_su_bst(SU_CNTP);
    here = 0; ps = -1;
    for (s = 0; s < BP_SEATS; s++) {
        su_iv[9 + s] = s < su_cfg.nseats ? (su_tab ? 2 : 1) : 0;
        su_iv[13 + s] = BP_A2_TOK_FLOPPY + (su_cfg.token[s] & 3);
        su_iv[17 + s] = ovl_su_mine(s) ? su_ready : pr;
        su_sv[s] = su_cfg.name[s];
        su_sv[6 + s] = ovl_su_mine(s) ? TS_THIS_PC : TS_OTHER_PC;
        su_sv[20 + s] = su_toks[su_cfg.token[s] & 3];
        if (s < su_cfg.nseats) {
            if (ovl_su_mine(s)) here++;
            else if (ps < 0) ps = s;
        }
    }
    if (su_edit) {                      /* the name being typed, with its caret */
        su_eb[su_en] = '_'; su_eb[su_en + 1] = 0;
        su_sv[su_edit - 1] = su_eb;
    }
    su_sv[24] = lobby ? TS_TITLE_LOBBY : TS_TITLE_NEW;
    su_sv[4] = su_tab == 0 ? TS_DESC_SAME : su_tab == 1 ? TS_DESC_HOST : TS_DESC_JOIN;
    /* the roster footer: the count, and in a cable game how it is split */
    p = su_tb;
    if (su_tab == 0) p = ovl_su_cnt(p, su_cfg.nseats);
    else {
        p = ovl_su_cnt(p, here);
        p = ovl_su_cat(p, TS_HERE);
        p = ovl_su_cnt(p, su_cfg.nseats - here);
        p = ovl_su_cat(p, TS_THERE);
    }
    su_sv[5] = su_tb;
    /* the rules: the radio, its rounds and what they mean */
    su_iv[21] = su_cfg.rules == BP_RULES_CLASSIC;
    su_iv[22] = !su_iv[21];
    su_sv[11] = TS_RULE_CASH;
    p = su_tb + 36;                     /* "20 rounds" */
    p[0] = (char)('0' + su_cfg.rounds / 10); p[1] = '0'; p[2] = 0;
    ovl_su_cat(p + 2, TS_ROUNDS);
    su_sv[10] = p;
    if (su_iv[21]) su_sv[12] = TS_RULE_LAST;
    else {                              /* "Richest after 20 rounds." */
        p = su_tb + 48;
        su_sv[12] = p;
        p = ovl_su_cat(p, TS_RULE_Q1);
        p[0] = (char)('0' + su_cfg.rounds / 10); p[1] = '0'; p[2] = 0;
        ovl_su_cat(p + 2, TS_RULE_Q2);
    }
    /* the link box */
    su_iv[23] = su_tab == 0 ? 0 : st >= BP_L_LOBBY ? 2 : 1;
    su_iv[24] = st >= BP_L_LOBBY ? BP_A2_LED_GREEN :
                (st == BP_L_SEARCH || st == BP_L_PEER) ? BP_A2_LED_YELLOW :
                (bp_mono && bx_size(BP_A2_M_LED_RED)) ? BP_A2_M_LED_RED : BP_A2_LED_RED;
    su_sv[13] = lobby ? bp_link_msg() : TS_NOT_CONNECTED;
#ifdef BP_SETUP_DEMO
    su_sv[13] = "Other PC found";
#endif
    su_sv[14] = su_tab == 1 ? TS_ROLE_HOST : TS_ROLE_GUEST;
    su_sv[15] = su_ports[su_port % 3];
    su_sv[18] = su_ports[su_port % 3];
    su_sv[19] = lobby ? TS_LEAVE : TS_CONNECT;
    su_iv[26] = 0;
    su_sv[16] = "";
    if (ps >= 0 && st >= BP_L_LOBBY) {  /* the other computer, by its first player */
        su_iv[26] = BP_A2_TOK_FLOPPY + (su_cfg.token[ps] & 3);
        p = su_tb + 76;
        os88_strcpy(p, su_cfg.name[ps], BP_NAMELEN + 1);
        p = ovl_su_cat(p + os88_strlen(p), TS_PEER_PC);
        ovl_su_cat(p, pr ? TS_READY_2 : TS_WAITING);
        su_sv[16] = su_tb + 76;
    }
    /* the line in the bottom bar */
    if (su_msg[0]) su_sv[17] = su_msg;
    else if (su_tab == 0) su_sv[17] = TS_PLAYERS_TAKE_TURNS;
    else if (lobby) su_sv[17] = su_ready && pr && su_tab == 1 && ovl_su_ena(SU_START) ? TS_ALL_READY :
                                su_tab == 1 ? TS_SET_THE_ROSTER : TS_WAITING_FOR_THE;
    else su_sv[17] = TS_PRESS_CONNECT_TO;
}

/* the keyboard focus on a widget the scene draws plainly (a button shows it
 * as its navy face): a black ring inside a yellow one, which reads on the
 * white table, the grey panel and the teal ground alike (and on 1bpp) */
static void ovl_su_ring(int id)
{
    int i, x, y, w, h, c;
    if (id == SU_CNTM || id == SU_CNTP || (id >= SU_PORT && id <= SU_BACK)) return;
    ovl_su_rect(id, &su_rc);
    for (i = 2; i > 0; i--) {
        x = su_rc.x - i; y = su_rc.y - i; w = su_rc.w + 2 * i; h = su_rc.h + 2 * i;
        c = i == 2 ? 14 : 0;
        bx_fill(x, y, x + w - 1, y, c); bx_fill(x, y + h - 1, x + w - 1, y + h - 1, c);
        bx_fill(x, y, x, y + h - 1, c); bx_fill(x + w - 1, y, x + w - 1, y + h - 1, c);
    }
}

/* bp_pn_draw: the ground, the scene, the Same PC half, the focus ring. It
 * runs once per strip, clipped by the band; the slots are cheap to refill */
static void ovl_su_draw(void)
{
    ovl_su_geom();
    bx_dither(bp_r_center.x, bp_r_center.y, bp_r_center.x + bp_r_center.w - 1,
              bp_r_center.y + bp_r_center.h - 1, 3, 11, 2);
    ovl_su_slots();
    gk_scene(su_sc, su_ox, su_oy, su_sv, su_iv);
    if (su_tab == 0) gk_scene(su_sc + 1, su_ox, su_oy, su_sv, su_iv);
    ovl_su_ring(su_foc);
}

static const char *ovl_su_hint(int i)
{
    if (i < 0 || i > 4) return 0;
    return su_hints[i];
}

/* the New Game screen is entered (launch, Game > New Game) */
static void ovl_su_open(void)
{
    if (su_cfg.nseats == 0) ovl_su_defaults();
    su_edit = 0;
    su_msg[0] = 0;
}
