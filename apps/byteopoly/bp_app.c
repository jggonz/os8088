/* Byteopoly - the application: callbacks, window, menus, timer, close
 * (SPEC.md 131.9.1-131.9.6). Lane: app. Every os88_* callback enters here and is
 * routed: panels first, then the board. Only the session (bp_link.c)
 * changes the game; this file turns its results into log lines and sounds.
 * FOUNDATION SKELETON: window, menus, About, timer and a selectable board. */
static void *bp_win;
static struct os88_video bp_ap_video;
static unsigned bp_ap_artseg, bp_ap_bandseg;
static BPByte bp_ap_timer_ok;
#define BP_AP_BANDP 1024        /* the band claim, paragraphs: 16 KB */

static const char *bp_ap_game[] = {"New Game...", "Load...", "Save...", "Close"};
static const char *bp_ap_trade[] = {"Propose Trade..."};
static const char *bp_ap_coll[] = {"My Collection", "Upgrade", "Sell Upgrade", "Mortgage", "Redeem"};
static const char *bp_ap_opts[] = {"Sound", "Fast Animation", "Link Settings..."};
static const char *bp_ap_help[] = {"Rules", "Keys", "Link Status"};
static struct os88_menuset bp_ap_menus = {"Byteopoly", 0, 5,
    {{"Game", bp_ap_game, 4}, {"Trade", bp_ap_trade, 1}, {"Collection", bp_ap_coll, 5},
     {"Options", bp_ap_opts, 3}, {"Help", bp_ap_help, 3}}};
static const char *bp_ap_about[] = {"Byteopoly 1.0", "Collect vintage computers",
    "2-4 players, one PC or two", "over a null-modem cable", "Original os8088 game, written in C", 0};

static BPByte bp_ap_mode;     /* 0 game/board, 1 setup, 2 lobby */
/* the face's door: setup screens in setup/lobby mode, else the game panels */
void bp_pn_draw(void) { if (bp_ap_mode) bp_su_draw(); else bp_gp_draw(); }
int bp_pn_covers(void) { return bp_ap_mode ? BP_PN_FULL : bp_gp_covers(); }
int bp_pn_key(int ascii, int scan) { return bp_ap_mode ? bp_su_key(ascii, scan) : bp_gp_key(ascii, scan); }
int bp_pn_click(int x, int y) { return bp_ap_mode ? bp_su_click(x, y) : bp_gp_click(x, y); }
const char *bp_pn_hint(int i) { return bp_gp_hint(i); }
void bp_pn_sync(void) { bp_gp_sync(); }
void bp_sound(int cue) { (void)cue; }
void bp_ui_committed(const BPResult *r) { (void)r; bp_face_dirty(BP_D_ALL); bp_pn_sync(); }
void bp_ui_link(void) { bp_face_dirty(BP_D_LINK); }
void bp_ui_chat(int machine, const char *text) { (void)machine; bp_log_add(text); }

void os88_onmove(unsigned was, unsigned now)
{
    if (bp_ap_bandseg && bp_ap_bandseg == was) { bp_ap_bandseg = now; bx_bseg = now; }
}
void os88_paint(void *win) { bp_face_paint(win, 1); }
void os88_onresize(int w, int h, void *win) { (void)w; (void)h; (void)win; bp_face_dirty(BP_D_ALL); }
void os88_onkey(int ascii, int scan, void *win)
{
    if (bp_pn_key(ascii, scan) || bp_face_key(ascii, scan)) bp_face_paint(win, 0);
}
void os88_onclick(int x, int y, void *win)
{
    int s;
    if (bp_pn_click(x, y)) { bp_face_paint(win, 0); return; }
    s = bp_face_hit_space(x, y);
    if (s >= 0) { bp_face_select(s); bp_face_paint(win, 0); }
}
/* Menu commands are infrequent: overlay code (SPEC.md 73.14, 131.11). */
static int ovl_bp_ap_cmd(int item, int menu, void *win)
{
    if (menu == 0 && item == 3) { os88_wm_close(win); return 1; }
    return 1;
}
void os88_oncmd(int item, int menu, void *win) { ovl_bp_ap_cmd(item, menu, win); }
void os88_about(void *win) { os88_about_card(win, bp_ap_about); }
int os88_onclose(void *win) { (void)win; return 1; }
void os88_ontimer(void *win)
{
    bp_sess_tick();
    if (os88_wm_timer(win, 1) != 0) bp_ap_timer_ok = 0;
}
static struct os88_class bp_ap_class = {OS88_CLASS_LEN, OS88_WF_SIZABLE | OS88_WF_OWNBG,
    os88_t_paint, os88_t_onkey, os88_t_onclick, os88_t_about, 0,
    os88_t_onclose, 0, os88_t_onresize, 0, 0, os88_t_ontimer, 0, 0};
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
    bp_win = os88_wm_create(0, OS88_MBAR_H, bp_ap_video.w, bp_ap_video.dock_top - OS88_MBAR_H,
                            "Byteopoly", &bp_ap_class);
    if (!bp_win) return 0;
    os88_wm_minsize(bp_win, 320, 160);
    os88_menu_set(bp_win, &bp_ap_menus);
    bp_ap_timer_ok = os88_wm_timer(bp_win, 1) == 0;
    bp_log_add("Welcome to Byteopoly.");
    bp_log_add("Game > New Game to begin.");
    return 1;
}
void *os88_main(void)
{
    if (!ovl_bp_ap_start()) return 0;
    return bp_win;
}
