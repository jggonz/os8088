/* Byteopoly - the New Game screen and the cable lobby (SPEC.md 131.9.2,
 * 131.9.3). Lane: app. Drawn over the whole content (BP_PN_FULL) while
 * bp_app.c's mode says so. Every function is UI-task code reached by menu,
 * key, click or the link: overlay (ovl_*), SPEC.md 131.11. Every rectangle
 * is computed on demand from bp_r_center by ovl_su_rect, so draw, click and
 * key cannot disagree about where a widget is. */
static void ovl_bp_ap_back(void);
static void ovl_bp_ap_guard(int act);
static void ovl_bp_ap_started(void);

/* widget ids; the order is the focus order */
#define SU_TAB    0     /* 0..2 Same PC, Host Cable, Join Cable */
#define SU_CNTM   3
#define SU_CNTP   4
#define SU_NAME   5     /* 5..8 */
#define SU_TOK    9     /* 9..12 */
#define SU_MACH   13    /* 13..16 */
#define SU_READY  17
#define SU_RULE   18    /* 18..21 Classic, Quick 10, 20, 30 */
#define SU_PORT   22
#define SU_LINK   23
#define SU_START  24
#define SU_BACK   25
#define SU_N      26

static BPSetup su_cfg;
static BPByte su_tab, su_foc, su_port, su_edit, su_en, su_ready, su_sent, su_rh;
static char su_eb[BP_NAMELEN + 1];
static char su_msg[41];
static char su_lm[32];
static int su_gx, su_gy, su_gw, su_gh, su_rx, su_rw, su_ty, su_hy, su_r0,
    su_fy, su_my, su_lb;
static BPRect su_rc;

static const char *su_tabs[] = {"Same PC", "Host Cable", "Join Cable"};
static const char *su_toks[] = {"Floppy", "Mouse", "Joystick", "Cassette"};
static const char *su_rules[] = {"Classic", "Quick 10", "Quick 20", "Quick 30"};
static const char *su_ports[] = {"Port Auto", "Port COM1", "Port COM2"};
static const char *su_hints[] = {"Tab Next", "Arrows Change", "Enter Edit",
    "S Start", "Esc Back", 0};

static void ovl_su_say(const char *s) { os88_strcpy(su_msg, s, sizeof(su_msg)); }

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

/* the one geometry every rectangle comes from (see 131.9.2). Compact is the
 * CGA-class content (about 120 px high): no spare row anywhere. */
static void ovl_su_geom(void)
{
    int pw, g;
    su_rh = bp_r_center.h >= 260 ? 16 : 11;
    g = su_rh == 16 ? 3 : 0;
    pw = bp_r_center.w - 16;
    if (pw > 624) pw = 624;
    su_gw = pw;
    su_gx = bp_r_center.x + (bp_r_center.w - pw) / 2;
    su_gy = bp_r_center.y + 2;
    if (bp_r_center.h > 220) su_gy += (bp_r_center.h - 200) / 3;
    su_ty = su_gy + 14 + g;
    su_hy = su_ty + su_rh + 2 + g;
    su_r0 = su_hy + 9 + g;
    su_fy = su_r0 + 4 * (su_rh + 1) + 2 + g;
    su_my = su_fy + su_rh + 4 + g;
    su_gh = su_my - su_gy + 10;
    su_rx = su_gx + 372;
    su_rw = su_gw - 372 - 8;
    su_lb = su_ty + 9 + 2 * (su_rh + 1) + 3 + g;
}

static void ovl_su_rect(int id, BPRect *r)
{
    int h, k, kind;
    h = su_rh;
    r->x = 0; r->y = 0; r->w = 0; r->h = h;
    if (id <= 2) {
        r->x = su_gx + 8 + id * 100; r->y = su_ty; r->w = 96; r->h = h + 1;
    } else if (id <= SU_CNTP) {
        r->x = su_gx + (id == SU_CNTM ? 294 : 326); r->y = su_fy; r->w = 16; r->h = h + 2;
    } else if (id < SU_READY) {
        k = (id - SU_NAME) & 3; kind = (id - SU_NAME) >> 2;
        r->y = su_r0 + k * (h + 1);
        if (kind == 0) { r->x = su_gx + 8; r->w = 84; }
        else if (kind == 1) { r->x = su_gx + 96; r->w = 76; }
        else { r->x = su_gx + 176; r->w = 80; }
    } else if (id == SU_READY) {
        r->x = su_rx; r->y = su_lb + 19 + h + 3; r->w = su_rw;
    } else if (id < SU_PORT) {
        k = id - SU_RULE;
        r->x = su_rx + (k & 1) * (su_rw / 2); r->y = su_ty + 9 + (k >> 1) * (h + 1);
        r->w = su_rw / 2 - 4;
    } else if (id == SU_PORT) {
        r->x = su_rx; r->y = su_lb + 19; r->w = 84;
    } else if (id == SU_LINK) {
        r->x = su_rx + 88; r->y = su_lb + 19; r->w = su_rw - 88;
    } else if (id == SU_START) {
        r->x = su_gx + 8; r->y = su_fy; r->w = 120; r->h = h + 2;
    } else if (id == SU_BACK) {
        r->x = su_gx + 136; r->y = su_fy; r->w = 88; r->h = h + 2;
    }
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
    return 1;
}

static int ovl_su_ena(int id)
{
    int lobby;
    lobby = bp_ap_mode == BP_M_LOBBY;
    if (id <= 2) return !lobby;
    if (id == SU_BACK || id == SU_LINK) return 1;
    if (id == SU_PORT) return !lobby;
    if (id == SU_READY) return lobby && bp_link_state() == BP_L_LOBBY;
    if (id == SU_START)
        return su_tab == 0 || (lobby && bp_link_state() == BP_L_LOBBY);
    return su_tab != 2;     /* roster, counts, rules: a client only reads */
}

static int ovl_su_mine(int s) { return su_cfg.machine[s] == (su_tab == 2 ? 1 : 0); }

static int ovl_su_rule(void)
{
    return su_cfg.rules == BP_RULES_CLASSIC ? 0 : su_cfg.rounds / 10;
}

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
}

/* a host's change in the lobby goes out whole, and unreadies both sides */
static void ovl_su_push(void)
{
    int e;
    if (bp_ap_mode != BP_M_LOBBY || su_tab != 1 || bp_link_state() != BP_L_LOBBY) return;
    e = bp_lobby_set(&su_cfg);
    if (e != BP_OK) ovl_su_say(bp_err_text(e));
    if (su_ready) { su_ready = 0; bp_lobby_ready(0); }
}

static int ovl_su_check(void)
{
    int i, j, e, here;
    for (i = 0; i < su_cfg.nseats; i++)
        for (j = i + 1; j < su_cfg.nseats; j++)
            if (su_cfg.token[i] == su_cfg.token[j]) {
                ovl_su_say("Each player needs a different token.");
                return 0;
            }
    if (su_tab != 0) {
        here = 0;
        for (i = 0; i < su_cfg.nseats; i++) here += su_cfg.machine[i];
        if (here == 0 || here == su_cfg.nseats) {
            ovl_su_say("Each PC needs at least one player.");
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
        ovl_su_say("Starting...");
    }
}

static void ovl_su_start(void)
{
    if (!ovl_su_ena(SU_START)) { ovl_su_say("The link is not ready yet."); return; }
    if (su_tab != 0) {
        if (!ovl_su_check()) return;
        ovl_bp_ap_guard(BP_ACT_START);
        return;
    }
    if (!ovl_su_check()) return;
    ovl_bp_ap_guard(BP_ACT_START);
}

static void ovl_su_link(void)
{
    int e;
    const char *m;
    if (bp_ap_mode == BP_M_LOBBY) {
        bp_link_leave();
        bp_ap_mode = BP_M_SETUP; su_ready = 0; su_sent = 0;
        ovl_su_say("Link closed.");
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
}

/* one widget's action. dir 0 = click/Enter/Space, +1/-1 = Right/Left */
static void ovl_su_act(int id, int dir)
{
    int k, n;
    if (id <= 2) { ovl_su_tab(dir ? su_tab + dir : id); return; }
    if (id == SU_CNTM || id == SU_CNTP) {
        n = su_cfg.nseats + (id == SU_CNTP ? 1 : -1);
        if (n < 2 || n > BP_SEATS) return;
        su_cfg.nseats = (BPByte)n;
        ovl_su_push();
    } else if (id >= SU_NAME && id < SU_TOK) {
        k = id - SU_NAME;
        su_edit = (BPByte)(k + 1);
        os88_strcpy(su_eb, su_cfg.name[k], sizeof(su_eb));
        su_en = (BPByte)os88_strlen(su_eb);
    } else if (id >= SU_TOK && id < SU_MACH) {
        k = id - SU_TOK;
        su_cfg.token[k] = (BPByte)((su_cfg.token[k] + (dir < 0 ? 3 : 1)) & 3);
        ovl_su_push();
    } else if (id >= SU_MACH && id < SU_READY) {
        k = id - SU_MACH;
        su_cfg.machine[k] ^= 1;
        ovl_su_push();
    } else if (id >= SU_RULE && id < SU_PORT) {
        k = dir ? (ovl_su_rule() + dir) & 3 : id - SU_RULE;
        su_cfg.rules = k ? BP_RULES_QUICK : BP_RULES_CLASSIC;
        su_cfg.rounds = (BPByte)(k * 10);
        ovl_su_push();
    } else if (id == SU_PORT) {
        su_port = (BPByte)((su_port + 1) % 3);
    } else if (id == SU_LINK) {
        ovl_su_link();
    } else if (id == SU_READY) {
        su_ready ^= 1;
        k = bp_lobby_ready(su_ready);
        if (k != BP_OK) { su_ready = 0; ovl_su_say(bp_err_text(k)); }
    } else if (id == SU_START) {
        ovl_su_start();
    } else if (id == SU_BACK) {
        ovl_bp_ap_back();
    }
    if (id != SU_START && id != SU_LINK && id != SU_BACK) su_msg[0] = 0;
}

static void ovl_su_focus(int d)
{
    int i, id;
    id = su_foc;
    for (i = 0; i < SU_N; i++) {
        id += d;
        if (id < 0) id = SU_N - 1;
        if (id >= SU_N) id = 0;
        if (ovl_su_vis(id) && ovl_su_ena(id)) { su_foc = (BPByte)id; return; }
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
        if (ascii == 27) su_edit = 0;
        else if (ascii == 13) { ovl_su_endedit(1); ovl_su_push(); }
        else if (ascii == 8) { if (su_en) su_en--; }
        else if (ascii >= 32 && ascii < 127 && su_en < BP_NAMELEN) su_eb[su_en++] = (char)ascii;
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
    ovl_su_geom();
    for (id = 0; id < SU_N; id++) {
        if (!ovl_su_vis(id) || !ovl_su_ena(id)) continue;
        ovl_su_rect(id, &su_rc);
        if (x >= su_rc.x && x < su_rc.x + su_rc.w && y >= su_rc.y && y < su_rc.y + su_rc.h) {
            if (su_edit) ovl_su_endedit(1);
            su_foc = (BPByte)id;
            ovl_su_act(id, 0);
            return 1;
        }
    }
    if (su_edit) ovl_su_endedit(1);
    return 1;
}

static int ovl_su_state(int id)
{
    int st;
    st = 0;
    if (su_foc == id) st |= BP_W_FOCUS;
    if (!ovl_su_ena(id)) st |= BP_W_DISABLED;
    return st;
}

static void ovl_su_draw(void)
{
    int id, s, st, on, cols;
    const char *lab;
    ovl_su_geom();
    bp_w_fill(bp_r_center.x, bp_r_center.y, bp_r_center.x + bp_r_center.w - 1,
              bp_r_center.y + bp_r_center.h - 1, 3);
    su_rc.x = su_gx; su_rc.y = su_gy; su_rc.w = su_gw; su_rc.h = su_gh;
    bp_w_panel(&su_rc, bp_ap_mode == BP_M_LOBBY ? "BYTEOPOLY - CABLE LOBBY" : "BYTEOPOLY - NEW GAME");
    /* tabs */
    for (id = 0; id <= 2; id++) {
        ovl_su_rect(id, &su_rc);
        st = ovl_su_state(id);
        if (su_tab == id) st |= BP_W_DEFAULT;
        bp_w_button(&su_rc, su_tabs[id], st);
    }
    /* roster header and rows */
    bp_w_text(su_gx + 8, su_hy, "PLAYER", 0, -1);
    bp_w_text(su_gx + 96, su_hy, "TOKEN", 0, -1);
    if (su_tab != 0) {
        bp_w_text(su_gx + 176, su_hy, "COMPUTER", 0, -1);
        bp_w_text(su_gx + 262, su_hy, "READY", 0, -1);
    }
    for (s = 0; s < su_cfg.nseats; s++) {
        id = SU_NAME + s;
        ovl_su_rect(id, &su_rc);
        st = ovl_su_state(id);
        if (su_edit == s + 1) {
            su_eb[su_en] = 0;
            su_lm[0] = 0;
            os88_strcpy(su_lm, su_eb, sizeof(su_lm));
            su_lm[su_en] = '_'; su_lm[su_en + 1] = 0;
            bp_w_button(&su_rc, su_lm, st | BP_W_DEFAULT);
        } else bp_w_button(&su_rc, su_cfg.name[s], st);
        id = SU_TOK + s;
        ovl_su_rect(id, &su_rc);
        bp_w_button(&su_rc, su_toks[su_cfg.token[s] & 3], ovl_su_state(id));
        if (su_tab != 0) {
            id = SU_MACH + s;
            ovl_su_rect(id, &su_rc);
            bp_w_button(&su_rc, ovl_su_mine(s) ? "This PC" : "Other PC", ovl_su_state(id));
            bp_w_text(su_gx + 262, su_rc.y + (su_rh - 8) / 2,
                      (ovl_su_mine(s) ? su_ready : bp_lobby_peer_ready()) ? "Ready" : "...", 0, -1);
        }
    }
    /* player count */
    if (su_tab != 2) {
        bp_w_text(su_gx + 236, su_fy + (su_rh - 6) / 2, "Players", 0, -1);
        ovl_su_rect(SU_CNTM, &su_rc);
        bp_w_button(&su_rc, "-", ovl_su_state(SU_CNTM));
        su_lm[0] = (char)('0' + su_cfg.nseats); su_lm[1] = 0;
        bp_w_text(su_gx + 314, su_fy + (su_rh - 6) / 2, su_lm, 0, -1);
        ovl_su_rect(SU_CNTP, &su_rc);
        bp_w_button(&su_rc, "+", ovl_su_state(SU_CNTP));
    }
    /* rules */
    bp_w_text(su_rx, su_ty, "GAME RULES", 0, -1);
    for (id = 0; id < 4; id++) {
        ovl_su_rect(SU_RULE + id, &su_rc);
        st = ovl_su_state(SU_RULE + id);
        if (ovl_su_rule() == id) st |= BP_W_DEFAULT;
        bp_w_button(&su_rc, su_rules[id], st);
    }
    /* the link box */
    if (su_tab != 0) {
        cols = su_rw / 8;
        bp_w_text(su_rx, su_lb, "LINK 2400 8N1", 0, -1);
        lab = bp_ap_mode == BP_M_LOBBY ? bp_link_msg() : "Not connected";
        os88_strcpy(su_lm, lab, cols < (int)sizeof(su_lm) ? cols + 1 : (int)sizeof(su_lm));
        bp_w_text(su_rx, su_lb + 9, su_lm, 0, -1);
        ovl_su_rect(SU_PORT, &su_rc);
        bp_w_button(&su_rc, su_ports[su_port % 3], ovl_su_state(SU_PORT));
        ovl_su_rect(SU_LINK, &su_rc);
        bp_w_button(&su_rc, bp_ap_mode == BP_M_LOBBY ? "Leave" : "Connect", ovl_su_state(SU_LINK));
        ovl_su_rect(SU_READY, &su_rc);
        on = su_ready ? BP_W_DEFAULT : 0;
        bp_w_button(&su_rc, "Ready", ovl_su_state(SU_READY) | on);
    }
    /* footer */
    if (su_tab != 2) {
        ovl_su_rect(SU_START, &su_rc);
        bp_w_button(&su_rc, "[S] Start Game", ovl_su_state(SU_START));
    }
    ovl_su_rect(SU_BACK, &su_rc);
    bp_w_button(&su_rc, "[Esc] Back", ovl_su_state(SU_BACK));
    if (su_msg[0]) lab = su_msg;
    else if (su_tab == 0) lab = "Players take turns on this PC.";
    else if (bp_ap_mode == BP_M_LOBBY) lab = su_tab == 1 ? "Set the roster, then Ready." : "Waiting for the host.";
    else lab = "Press Connect to find the other PC.";
    bp_w_text(su_gx + 8, su_my, lab, 0, -1);
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
