/* Byteopoly - the application: callbacks, window, menus, timer, sound,
 * files and close (SPEC.md 131.9.1-131.9.6). Lane: app. Every os88_*
 * callback enters here and is routed: a global key, then the door (setup
 * screen or game panel), then the board. Only the session (bp_link.c)
 * changes the game; this file turns its results into log lines, dirty
 * marks and sounds. Resident: the callbacks the board's every click, key
 * and tick reaches. Everything reached by a menu, a file, a lobby or a
 * commit is overlay code (ovl_*, SPEC.md 131.11). */
int bp_ask(const char *message, void *window, int buttons);
void bp_alertdone(int button);

BPByte bp_ap_mode, bp_opt_sound, bp_opt_fast;
static void *bp_win;
static struct os88_video bp_ap_video;
static unsigned bp_ap_artseg, bp_ap_bandseg;
static BPByte bp_ap_timer_ok, bp_ap_unsaved, bp_ap_need, bp_ap_after, bp_ap_askset;
static BPByte bp_ap_asking, bp_ap_closing, bp_ap_sndok, bp_ap_gone, bp_ap_wait;
static BPByte bp_ap_lastpos[BP_SEATS];
static const unsigned *bp_ap_cue;
static BPState bp_ap_scratch;
static char bp_ap_name[13];
static char bp_ap_ln[BP_LOG_LEN + 1];
static unsigned bp_ap_ll;
#define BP_AP_BANDP 1024        /* the band claim, paragraphs: 16 KB */

/* ---- menus: titles total 26 characters so all five fit (131.9.4) ------- */
static const char bp_ap_s_new[] = "New Game...";
static const char bp_ap_s_load[] = "\001Load...  F3";
static const char bp_ap_s_save[] = "\001Save...  F2";
static const char bp_ap_s_psave[] = "\001Paired Save";
static const char bp_ap_s_resume[] = "\001Resume Pair";
static const char bp_ap_s_close[] = "Close";
static const char bp_ap_s_prop[] = "\001Propose Trade...";
static const char bp_ap_s_coll[] = "\001My Collection...";
static const char bp_ap_s_stand[] = "\001Standings";
static const char bp_ap_s_diag[] = "\001Diagnostics";
static const char bp_ap_s_rules[] = "\001Rules  F1";
static const char bp_ap_s_keys[] = "\001Keys";
static const char bp_ap_s_chat[] = "\001Chat...";
static const char *bp_ap_game[6], *bp_ap_trade[1], *bp_ap_assets[2],
    *bp_ap_opts[3], *bp_ap_help[3];
static struct os88_menuset bp_ap_menus = {"Byteopoly", 0, 5,
    {{"Game", bp_ap_game, 6}, {"Trade", bp_ap_trade, 1}, {"Assets", bp_ap_assets, 2},
     {"Options", bp_ap_opts, 3}, {"Help", bp_ap_help, 3}}};
static const char *bp_ap_about[] = {"Byteopoly 1.0", "Collect vintage computers",
    "2-4 players, one PC or two", "over a null-modem cable", "Original os8088 game, written in C", 0};

static struct os88_class bp_ap_class = {OS88_CLASS_LEN, OS88_WF_SIZABLE | OS88_WF_OWNBG,
    os88_t_paint, os88_t_onkey, os88_t_onclick, os88_t_about, 0,
    os88_t_onclose, 0, os88_t_onresize, 0, 0, os88_t_ontimer, 0, 0};

/* ---- sound cues: (Hz, ticks) notes, ticks 0 ends; indexed by BP_SND_* --- */
static const unsigned bp_ap_c0[] = {880, 1, 660, 1, 990, 1, 0, 0};
static const unsigned bp_ap_c1[] = {523, 2, 784, 3, 0, 0};
static const unsigned bp_ap_c2[] = {330, 2, 262, 3, 0, 0};
static const unsigned bp_ap_c3[] = {659, 1, 784, 1, 988, 2, 0, 0};
static const unsigned bp_ap_c4[] = {196, 2, 147, 2, 110, 4, 0, 0};
static const unsigned bp_ap_c5[] = {294, 3, 220, 3, 147, 6, 0, 0};
static const unsigned bp_ap_c6[] = {523, 2, 659, 2, 784, 2, 1047, 6, 0, 0};
static const unsigned bp_ap_c7[] = {110, 3, 0, 0};
static const unsigned *bp_ap_cues[] = {bp_ap_c0, bp_ap_c1, bp_ap_c2, bp_ap_c3,
    bp_ap_c4, bp_ap_c5, bp_ap_c6, bp_ap_c7};

static void bp_ap_cue_step(void)
{
    unsigned tk;
    if (!bp_ap_cue) return;
    if (bp_ap_wait) { bp_ap_wait--; if (bp_ap_wait) return; }
    tk = bp_ap_cue[1];
    if (!tk) { bp_ap_cue = 0; return; }
    os88_snd_tone(bp_ap_cue[0], tk, 0x40);
    bp_ap_wait = (BPByte)tk;
    bp_ap_cue += 2;
}
void bp_sound(int cue)
{
    if (!bp_opt_sound || !bp_ap_sndok || cue < 0 || cue > 7) return;
    bp_ap_cue = bp_ap_cues[cue];
    bp_ap_wait = 0;
    bp_ap_cue_step();
}

/* ---- overlay: everything a menu, file, lobby or commit reaches --------- */
static void ovl_bp_ap_say(const char *t)
{
    static char tb[25];
    os88_strcpy(tb, t, sizeof(tb));
    os88_toast(tb, 0);
    bp_log_add(t);
}
static void ovl_bp_ap_fail(int e)
{
    ovl_bp_ap_say(bp_err_text(e));
    bp_sound(BP_SND_ERROR);
}
static const char *ovl_bp_ap_it(const char *s, int on) { return on ? s + 1 : s; }

static void ovl_bp_ap_menus(void)
{
    int g, play, cab;
    g = bp_ap_mode == BP_M_GAME;
    play = g && bp_game.status == BP_ST_PLAY;
    cab = bp_mode != BP_MODE_LOCAL;
    bp_ap_game[0] = bp_ap_s_new;
    bp_ap_game[1] = ovl_bp_ap_it(bp_ap_s_load, !cab);
    bp_ap_game[2] = ovl_bp_ap_it(bp_ap_s_save, play && !cab);
    bp_ap_game[3] = ovl_bp_ap_it(bp_ap_s_psave, play && cab);
    bp_ap_game[4] = ovl_bp_ap_it(bp_ap_s_resume, bp_ap_mode == BP_M_LOBBY && bp_link_state() == BP_L_LOBBY);
    bp_ap_game[5] = bp_ap_s_close;
    bp_ap_trade[0] = ovl_bp_ap_it(bp_ap_s_prop, play);
    bp_ap_assets[0] = ovl_bp_ap_it(bp_ap_s_coll, g);
    bp_ap_assets[1] = ovl_bp_ap_it(bp_ap_s_stand, g);
    bp_ap_opts[0] = bp_opt_sound ? "Sound: On" : "Sound: Off";
    bp_ap_opts[1] = bp_opt_fast ? "Fast Animation: On" : "Fast Animation: Off";
    bp_ap_opts[2] = ovl_bp_ap_it(bp_ap_s_diag, g && cab);
    bp_ap_help[0] = ovl_bp_ap_it(bp_ap_s_rules, g);
    bp_ap_help[1] = ovl_bp_ap_it(bp_ap_s_keys, g);
    bp_ap_help[2] = ovl_bp_ap_it(bp_ap_s_chat, g && cab);
}

/* a match began, or was adopted, on this machine */
static void ovl_bp_ap_started(void)
{
    int i;
    bp_ap_mode = BP_M_GAME;
    bp_ap_unsaved = 0; bp_ap_gone = 0;
    for (i = 0; i < BP_SEATS; i++) bp_ap_lastpos[i] = bp_game.pos[i];
    bp_face_dirty(BP_D_ALL);
    bp_ap_need = 1;
}

static void ovl_bp_ap_newscreen(void)
{
    if (bp_mode != BP_MODE_LOCAL) {
        bp_link_leave();
        bp_sess_local();
        bp_ap_gone = 1;
    }
    bp_ap_mode = BP_M_SETUP;
    ovl_su_open();
    bp_face_dirty(BP_D_ALL);
    bp_ap_need = 1;
}

static void ovl_bp_ap_back(void)
{
    if (bp_ap_mode == BP_M_LOBBY) {
        bp_link_leave();
        bp_ap_mode = BP_M_SETUP;
    } else if (bp_game.status == BP_ST_PLAY && bp_mode == BP_MODE_LOCAL && !bp_ap_gone)
        bp_ap_mode = BP_M_GAME;
    else return;
    bp_face_dirty(BP_D_ALL);
    bp_ap_need = 1;
}

static void ovl_bp_ap_ask(const char *m, int set, int act)
{
    if (bp_ap_asking) return;
    if (bp_ask(m, bp_win, set) == 0) { bp_ap_asking = 1; bp_ap_after = (BPByte)act; bp_ap_askset = (BPByte)set; }
    else ovl_bp_ap_say("Question unavailable");
}

static void ovl_bp_ap_load(void)
{
    if (os88_file_dlg(OS88_FDLG_OPEN, bp_win, 0) != 0) ovl_bp_ap_say("File dialog busy");
}
static void ovl_bp_ap_save(void)
{
    if (bp_game.status != BP_ST_PLAY || bp_mode != BP_MODE_LOCAL) { ovl_bp_ap_say("Nothing to save"); return; }
    if (os88_file_dlg(OS88_FDLG_SAVE, bp_win, bp_ap_name[0] ? bp_ap_name : "BYTEOPLY.BPS") != 0)
        ovl_bp_ap_say("File dialog busy");
}

static int ovl_bp_ap_close(void)
{
    if (bp_ap_asking) return 0;
    if (!bp_ap_closing && bp_game.status == BP_ST_PLAY) {
        if (bp_mode != BP_MODE_LOCAL) {
            ovl_bp_ap_ask("Leave the cable game?", 1, BP_ACT_CLOSE);
            if (bp_ap_asking) return 0;
        } else if (bp_ap_unsaved) {
            ovl_bp_ap_ask("Save the match first?", 2, BP_ACT_CLOSE);
            if (bp_ap_asking) return 0;
        }
    }
    bp_link_leave();
    os88_wm_timer(bp_win, 0);
    return 1;
}

/* the answer is yes (asked, or nothing to ask): leave and close. */
static void ovl_bp_ap_exit(void)
{
    bp_ap_closing = 1;
    ovl_bp_ap_close();
    os88_wm_close(bp_win);
}

static void ovl_bp_ap_do(int act)
{
    if (act == BP_ACT_START) ovl_su_go();
    else if (act == BP_ACT_LOAD) ovl_bp_ap_load();
    else if (act == BP_ACT_NEW) ovl_bp_ap_newscreen();
    else if (act == BP_ACT_CLOSE) ovl_bp_ap_exit();
}

/* replacing an unsaved local match asks first */
static void ovl_bp_ap_guard(int act)
{
    if (bp_game.status == BP_ST_PLAY && bp_mode == BP_MODE_LOCAL && bp_ap_unsaved
        && (act == BP_ACT_START || act == BP_ACT_LOAD)) {
        ovl_bp_ap_ask("Discard the unsaved match?", 3, act);
        return;
    }
    ovl_bp_ap_do(act);
}

/* the alert was answered: set 2 Save/Discard/Cancel, 3 Cancel/Proceed, 1 Yes/No */
static void ovl_bp_ap_answer(int act, int set, int btn)
{
    if (act == BP_ACT_CLOSE) {
        if (set == 2 && btn == 0) { bp_ap_after = BP_ACT_SAVECLOSE; ovl_bp_ap_save(); }
        else if ((set == 2 && btn == 1) || (set == 1 && btn == 0)) ovl_bp_ap_do(BP_ACT_CLOSE);
    } else if (act == BP_ACT_NEW) {
        if (btn == 0) ovl_bp_ap_do(BP_ACT_NEW);
    } else if (set == 3 && btn == 1) ovl_bp_ap_do(act);
}
void bp_alertdone(int button)
{
    int a;
    a = bp_ap_after;
    bp_ap_asking = 0; bp_ap_after = 0;
    ovl_bp_ap_answer(a, bp_ap_askset, button);
    bp_ap_need = 1;
}

static void ovl_bp_ap_file(int mode, const char *name, unsigned lo, unsigned hi)
{
    int e, act;
    act = bp_ap_after; bp_ap_after = 0;
    if (mode == OS88_FDLG_SAVE) {
        e = bp_save_local(name, &bp_game);
        if (e != BP_OK) { ovl_bp_ap_fail(e); return; }
        bp_ap_unsaved = 0;
        os88_strcpy(bp_ap_name, name, sizeof(bp_ap_name));
        ovl_bp_ap_say("Match saved");
        if (act == BP_ACT_SAVECLOSE) ovl_bp_ap_exit();
        return;
    }
    if (hi || lo > BP_SAVE_MAX) { ovl_bp_ap_fail(BP_EFORMAT); return; }
    e = bp_load_local(name, &bp_ap_scratch);
    if (e == BP_OK) e = bp_sess_load_local(&bp_ap_scratch);
    if (e != BP_OK) { ovl_bp_ap_fail(e); return; }
    os88_strcpy(bp_ap_name, name, sizeof(bp_ap_name));
    ovl_bp_ap_started();
    ovl_bp_ap_say("Match loaded");
}

/* the game-over panel's choice: 2 New Game, 3 Exit */
static void ovl_bp_ap_over(int c)
{
    if (c == 2) ovl_bp_ap_newscreen();
    else if (c == 3) ovl_bp_ap_exit();
}

static void ovl_bp_ap_report(int e, const char *ok)
{
    if (e == BP_OK) ovl_bp_ap_say(ok); else ovl_bp_ap_fail(e);
}

static int ovl_bp_ap_cmd(int item, int menu, void *win)
{
    bp_ap_after = 0;
    if (menu == 0) {
        if (item == 0) {
            if (bp_mode != BP_MODE_LOCAL && bp_game.status == BP_ST_PLAY)
                ovl_bp_ap_ask("Leave the cable game?", 1, BP_ACT_NEW);
            else ovl_bp_ap_newscreen();
        } else if (item == 1) ovl_bp_ap_guard(BP_ACT_LOAD);
        else if (item == 2) ovl_bp_ap_save();
        else if (item == 3) ovl_bp_ap_report(bp_sess_save_pair(), "Pair saved");
        else if (item == 4) ovl_bp_ap_report(bp_sess_resume_pair(), "Resuming pair");
        else if (item == 5) { if (ovl_bp_ap_close()) os88_wm_close(win); }
    } else if (bp_ap_mode == BP_M_GAME) {
        if (menu == 1) bp_gp_open(BP_GP_TRADE);
        else if (menu == 2) bp_gp_open(item ? BP_GP_STANDINGS : BP_GP_MANAGE);
        else if (menu == 4) bp_gp_open(item == 0 ? BP_GP_RULES : item == 1 ? BP_GP_KEYS : BP_GP_CHAT);
        else if (menu == 3 && item == 2) bp_gp_open(BP_GP_DIAG);
    }
    if (menu == 3) {
        if (item == 0) { bp_opt_sound ^= 1; if (bp_opt_sound) bp_sound(BP_SND_BUY); }
        else if (item == 1) bp_opt_fast ^= 1;
    }
    ovl_bp_ap_menus();
    bp_ap_need = 1;
    return 1;
}

/* a global key the app owns: 1 = used */
static int ovl_bp_ap_hotkey(int scan)
{
    if (scan == 0x3B && bp_ap_mode == BP_M_GAME) { bp_gp_open(BP_GP_RULES); return 1; }
    if (scan == 0x3C) { bp_ap_after = 0; ovl_bp_ap_save(); return 1; }
    if (scan == 0x3D && bp_mode == BP_MODE_LOCAL) { bp_ap_after = 0; ovl_bp_ap_guard(BP_ACT_LOAD); return 1; }
    return 0;
}

/* ---- the event log: one bounded line per event (131.9.1) --------------- */
static void ovl_bp_ap_c(const char *s)
{
    while (*s && bp_ap_ll < BP_LOG_LEN) bp_ap_ln[bp_ap_ll++] = *s++;
    bp_ap_ln[bp_ap_ll] = 0;
}
static void ovl_bp_ap_amt(const BPU32 *a)
{
    static char b[14];
    bp_u32_fmt(b, a);
    ovl_bp_ap_c(b);
    ovl_bp_ap_c(" cr");
}
static void ovl_bp_ap_nm(int seat)
{
    if (seat < BP_SEATS) ovl_bp_ap_c(bp_game.name[seat]);
    else ovl_bp_ap_c("Bank");
}
static void ovl_bp_ap_sp(int space) { ovl_bp_ap_c(bp_space(space % BP_SPACES)->shortname); }

/* the line for one event, or 0 when it has none */
static int ovl_bp_ap_line(const BPEvent *e)
{
    static char d[3];
    int k;
    k = e->kind;
    bp_ap_ll = 0; bp_ap_ln[0] = 0;
    if (k == BP_V_ROLL) {
        ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" rolled ");
        d[0] = (char)('0' + e->other); d[1] = '+'; d[2] = 0; ovl_bp_ap_c(d);
        d[0] = (char)('0' + e->space); d[1] = 0; ovl_bp_ap_c(d);
    } else if (k == BP_V_MOVE) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" to "); ovl_bp_ap_sp(e->space); }
    else if (k == BP_V_ORDER || k == BP_V_FIRST) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" goes first"); }
    else if (k == BP_V_SALARY) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" salary +"); ovl_bp_ap_amt(&e->amount); }
    else if (k == BP_V_BUY || k == BP_V_WON) {
        ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(k == BP_V_BUY ? " bought " : " won ");
        ovl_bp_ap_sp(e->space); ovl_bp_ap_c(" for "); ovl_bp_ap_amt(&e->amount);
    } else if (k == BP_V_RENT) {
        ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" paid "); ovl_bp_ap_nm(e->other);
        ovl_bp_ap_c(" "); ovl_bp_ap_amt(&e->amount);
    } else if (k == BP_V_CHARGE) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" paid "); ovl_bp_ap_amt(&e->amount); }
    else if (k == BP_V_CARD) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" drew: "); ovl_bp_ap_c(bp_card(e->other & 15)->title); }
    else if (k == BP_V_GAIN) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" gets "); ovl_bp_ap_amt(&e->amount); }
    else if (k == BP_V_DETAIN) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" to the Repair Shop"); }
    else if (k == BP_V_RELEASE) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" left the Repair Shop"); }
    else if (k == BP_V_AUCTION) { ovl_bp_ap_c("Auction: "); ovl_bp_ap_sp(e->space); }
    else if (k == BP_V_BID) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" bids "); ovl_bp_ap_amt(&e->amount); }
    else if (k == BP_V_PASS) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" passes"); }
    else if (k == BP_V_UNSOLD) { ovl_bp_ap_sp(e->space); ovl_bp_ap_c(" unsold"); }
    else if (k == BP_V_UPGRADE || k == BP_V_SELL) {
        ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(k == BP_V_UPGRADE ? " upgraded " : " sold RAM on ");
        ovl_bp_ap_sp(e->space);
    } else if (k == BP_V_MORTGAGE || k == BP_V_REDEEM) {
        ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(k == BP_V_MORTGAGE ? " mortgaged " : " redeemed ");
        ovl_bp_ap_sp(e->space);
    } else if (k == BP_V_OFFER) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" offers "); ovl_bp_ap_nm(e->other); ovl_bp_ap_c(" a trade"); }
    else if (k == BP_V_TRADED) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" and "); ovl_bp_ap_nm(e->other); ovl_bp_ap_c(" traded"); }
    else if (k == BP_V_REJECTED) ovl_bp_ap_c("Trade rejected");
    else if (k == BP_V_CANCELLED) ovl_bp_ap_c("Trade withdrawn");
    else if (k == BP_V_DEBT) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" owes "); ovl_bp_ap_amt(&e->amount); }
    else if (k == BP_V_SETTLED) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" settled the debt"); }
    else if (k == BP_V_BANKRUPT) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" is bankrupt"); }
    else if (k == BP_V_TURN) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c("'s turn"); }
    else if (k == BP_V_DOUBLES) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" rolls again"); }
    else if (k == BP_V_DISK) { ovl_bp_ap_nm(e->seat); ovl_bp_ap_c(" keeps a Recovery Disk"); }
    else if (k == BP_V_ROUND) { ovl_bp_ap_c("Round complete"); }
    else if (k == BP_V_OVER) ovl_bp_ap_c("Game over!");
    else return 0;
    return 1;
}

/* the cue a kind earns, -1 for none; BP_SND_* ranks upward (131.9.1) */
static int ovl_bp_ap_cue(int k)
{
    if (k == BP_V_OVER) return BP_SND_WIN;
    if (k == BP_V_BANKRUPT) return BP_SND_BANKRUPT;
    if (k == BP_V_DETAIN) return BP_SND_CRASH;
    if (k == BP_V_CARD) return BP_SND_CARD;
    if (k == BP_V_RENT || k == BP_V_CHARGE) return BP_SND_RENT;
    if (k == BP_V_BUY || k == BP_V_WON) return BP_SND_BUY;
    if (k == BP_V_ROLL) return BP_SND_DICE;
    return -1;
}

static void ovl_bp_ap_committed(const BPResult *r)
{
    int i, k, best, c;
    unsigned dirty;
    const BPEvent *e;
    best = -1;
    dirty = BP_D_PLAYERS | BP_D_LOG | BP_D_HINT | BP_D_CENTER | BP_D_DETAILS;
    for (i = 0; i < r->n && i < BP_EV_MAX; i++) {
        e = &r->ev[i];
        k = e->kind;
        c = ovl_bp_ap_cue(k);
        if (c > best) best = c;
        if (ovl_bp_ap_line(e)) bp_log_add(bp_ap_ln);
        bp_gp_event(e);
        if (k == BP_V_BUY || k == BP_V_WON || k == BP_V_UPGRADE || k == BP_V_SELL
            || k == BP_V_MORTGAGE || k == BP_V_REDEEM || k == BP_V_UNSOLD || k == BP_V_AUCTION)
            bp_face_dirty_space(e->space % BP_SPACES);
        else if (k == BP_V_TRADED || k == BP_V_ESTATE || k == BP_V_BANKRUPT || k == BP_V_OVER)
            dirty |= BP_D_BOARD;
        if (k == BP_V_OVER) bp_ap_unsaved = 0;
    }
    for (i = 0; i < bp_game.nseats && i < BP_SEATS; i++)
        if (bp_game.pos[i] != bp_ap_lastpos[i]) {
            bp_face_dirty_space(bp_ap_lastpos[i]);
            bp_face_dirty_space(bp_game.pos[i]);
            bp_ap_lastpos[i] = bp_game.pos[i];
        }
    bp_face_dirty(dirty);
    if (bp_ap_mode != BP_M_GAME) {
        if (bp_game.status == BP_ST_PLAY) ovl_bp_ap_started();
    } else if (bp_mode == BP_MODE_LOCAL && bp_game.status == BP_ST_PLAY) bp_ap_unsaved = 1;
    if (best >= 0) bp_sound(best);
    bp_ap_need = 1;
}

static void ovl_bp_ap_link(void)
{
    bp_face_dirty(BP_D_LINK);
    if (bp_ap_mode != BP_M_GAME) {
        if (bp_link_state() == BP_L_PLAY && bp_game.status == BP_ST_PLAY) ovl_bp_ap_started();
        else ovl_su_sync();
    }
    ovl_bp_ap_menus();
    bp_ap_need = 1;
}

static void ovl_bp_ap_chat(int machine, const char *text)
{
    static char cb[BP_LOG_LEN + 1];
    os88_strcpy(cb, machine == bp_here ? "You: " : "Peer: ", sizeof(cb));
    bp_ap_ll = os88_strlen(cb);
    os88_strcpy(cb + bp_ap_ll, text, sizeof(cb) - bp_ap_ll);
    bp_log_add(cb);
    bp_gp_chat(machine, text);
    bp_ap_need = 1;
}

/* Launch-once code is overlay code (SPEC.md 131.11). */
static int ovl_bp_ap_start(void)
{
    os88_video(&bp_ap_video);
    bp_ap_artseg = 0;
    if (os88_part_lazyok(1) && os88_part_fetch(1) == 0) bp_ap_artseg = os88_part_seg(1);
    bp_ap_bandseg = os88_mem_claim_p(BP_AP_BANDP);
    if (bp_ap_bandseg) os88_mem_movable(bp_ap_bandseg, 1);
    bp_face_init(0, bp_ap_artseg, bp_ap_bandseg, BP_AP_BANDP * 16, bp_ap_video.bpp);
    bp_sess_local();
    bp_ap_mode = BP_M_SETUP;
    bp_opt_sound = 1;
    bp_ap_sndok = os88_snd_caps() != 0;
    ovl_su_open();
    ovl_bp_ap_menus();
    bp_win = os88_wm_create(0, OS88_MBAR_H, bp_ap_video.w, bp_ap_video.dock_top - OS88_MBAR_H,
                            "Byteopoly", &bp_ap_class);
    if (!bp_win) return 0;
    os88_wm_minsize(bp_win, 560, 160);
    os88_menu_set(bp_win, &bp_ap_menus);
    bp_ap_timer_ok = os88_wm_timer(bp_win, 1) == 0;
    bp_log_add("Welcome to Byteopoly.");
    bp_log_add("Choose players, then Start.");
    return 1;
}

/* ---- resident: the callbacks every click, key and tick reaches --------- */
/* the face's door: setup screens in setup/lobby mode, else the game panels */
void bp_pn_draw(void) { if (bp_ap_mode) ovl_su_draw(); else bp_gp_draw(); }
int bp_pn_covers(void) { return bp_ap_mode ? BP_PN_FULL : bp_gp_covers(); }
int bp_pn_key(int ascii, int scan) { return bp_ap_mode ? ovl_su_key(ascii, scan) : bp_gp_key(ascii, scan); }
int bp_pn_click(int x, int y) { return bp_ap_mode ? ovl_su_click(x, y) : bp_gp_click(x, y); }
const char *bp_pn_hint(int i) { return bp_ap_mode ? ovl_su_hint(i) : bp_gp_hint(i); }
void bp_pn_sync(void)
{
    if (bp_ap_mode == BP_M_GAME) bp_gp_sync();
    ovl_bp_ap_menus();
    bp_ap_need = 1;
}
void bp_ui_committed(const BPResult *r) { ovl_bp_ap_committed(r); bp_pn_sync(); }
void bp_ui_link(void) { ovl_bp_ap_link(); }
void bp_ui_chat(int machine, const char *text) { ovl_bp_ap_chat(machine, text); }

void os88_onmove(unsigned was, unsigned now)
{
    if (bp_ap_bandseg && bp_ap_bandseg == was) { bp_ap_bandseg = now; bx_bseg = now; }
}
/* after a key or click: did the end panel ask for something? */
static void bp_ap_poll(void)
{
    int c;
    if (bp_ap_mode != BP_M_GAME || bp_game.status != BP_ST_OVER) return;
    c = bp_gp_overchoice();
    if (c >= 2) ovl_bp_ap_over(c);
}
/* a callback's repaint: armed clip, then only what is dirty */
static void bp_ap_repaint(void *win)
{
    bp_ap_need = 0;
    if (bp_ap_mode) bp_face_dirty(BP_D_CENTER);     /* setup draws in the centre */
    if (os88_wm_clip_set(win) == 0) bp_face_paint(win, 0);
}
void os88_paint(void *win) { bp_ap_need = 0; bp_face_paint(win, 1); }
void os88_onresize(int w, int h, void *win) { (void)w; (void)h; (void)win; bp_face_dirty(BP_D_ALL); }
void os88_onkey(int ascii, int scan, void *win)
{
    int used;
    used = 0;
    if (ascii == 0 && ovl_bp_ap_hotkey(scan)) used = 1;
    else if (bp_pn_key(ascii, scan)) used = 1;
    else if (bp_ap_mode == BP_M_GAME && bp_face_key(ascii, scan)) used = 1;
    bp_ap_poll();
    if (used || bp_ap_need) bp_ap_repaint(win);
}
void os88_onclick(int x, int y, void *win)
{
    int s;
    if (bp_pn_click(x, y)) { bp_ap_poll(); bp_ap_repaint(win); return; }
    if (bp_ap_mode) return;
    s = bp_face_hit_space(x, y);
    if (s >= 0) { bp_face_select(s); bp_ap_repaint(win); }
}
void os88_oncmd(int item, int menu, void *win)
{
    ovl_bp_ap_cmd(item, menu, win);
    if (bp_ap_need) bp_ap_repaint(win);
}
void os88_about(void *win) { os88_about_card(win, bp_ap_about); }
int os88_onclose(void *win) { (void)win; return ovl_bp_ap_close(); }
void os88_onfile(int mode, const char *name, unsigned lo, unsigned hi, void *win)
{
    ovl_bp_ap_file(mode, name, lo, hi);
    if (bp_ap_need) bp_ap_repaint(win);
}
void os88_ontimer(void *win)
{
    bp_sess_tick();
    bp_ap_cue_step();
    if (bp_ap_mode == BP_M_GAME) bp_gp_tick();
    if (bp_ap_need || bp_face_isdirty()) bp_ap_repaint(win);
    if (os88_wm_timer(win, 1) != 0) bp_ap_timer_ok = 0;
}
void *os88_main(void)
{
    if (!ovl_bp_ap_start()) return 0;
    return bp_win;
}
