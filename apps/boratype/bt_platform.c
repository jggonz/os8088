/* Native lifecycle and command routing. SPEC.md 134 / BORATYPE-SPEC 1-3,6.
 * All state changes run on the UI task. The timer fallback worker only
 * posts a wake; it never scores, draws, plays audio or touches a file. */
int bt_screen, bt_selection, bt_ch_duration, bt_ch_diff, bt_master_sound = 1;
int bt_full, bt_readonly, bt_profile_edit, bt_modifiers, bt_practice_weak, bt_blink_frame;
char bt_notice[64];
static void *bt_win;
static unsigned bt_band, bt_art, bt_soundpart;
static struct os88_video bt_video;
static int bt_timer_ok, bt_worker_live, bt_save_pending, bt_about_up;
static int bt_focused = 1, bt_guard_scan, bt_last_scan, bt_in_cga;
static int bt_cga_exit, bt_quit_pending, bt_closing;
static int bt_return_screen;
static BTByte bt_io[BT_FILE_SIZE]; /* canonical2136B save, also2000B CGA cells */
static BTWord bt_saved_generation;
static const char *bt_slots[2] = {"BORATA.BTT", "BORATB.BTT"};
static const char *bt_about_lines[] = {"Borat Teaches Typing", "Native C typing tutor for os8088",
    "24 lessons, practice and timed tests", "Original jokes and soundtrack", "F1: help   F6: full screen", 0};
static const char *bt_file_items[] = {"Profile...", "Save progress", "Close  Ctrl+Q"};
static const char *bt_lesson_items[] = {"Lessons  F2", "Practice", "Challenge", "Records"};
static const char *bt_view_items[] = {"Pause / Resume  F4", "Full screen  F6", "Settings"};
static const char *bt_sound_items[] = {"Sound On / Off  F3", "Test Music", "Test Voice"};
static const char *bt_help_items[] = {"Help  F1"};
static struct os88_menuset bt_menus = {"Borat Typing", 0, 5,
    {{"File", bt_file_items, 3}, {"Lesson", bt_lesson_items, 4},
     {"View", bt_view_items, 3}, {"Sound", bt_sound_items, 3}, {"Help", bt_help_items, 1}}};

int bt_data_enter(void);
void bt_data_back(void);
int bt_other_instance(void);
void bt_copy_font(void);
int bt_modifier_state(void);
void bx_setart2(unsigned seg);
void bt_cga_entry(void);
void bt_action(int action);

static void bt_message(const char *s) { os88_strcpy(bt_notice, s, sizeof(bt_notice)); }
static void bt_redraw(int all)
{
    if (!bt_win || bt_in_cga) return;
    if (os88_wm_clip_set(bt_win) == 0) {
        bt_face_paint(bt_win, all);
        os88_wm_grow(bt_win);
    }
}

#include "bt_files.c"

static void bt_schedule_save(void)
{
    bt_save_pending = 1;
    if (bt_win && !bt_in_cga) os88_wm_wake(bt_win);
}

static void bt_events(unsigned ev)
{
    if (!ev) return;
    if (ev & BT_EV_FINISH) {
        bt_commit();
        bt_screen = BT_S_RESULTS;
        bt_guard_scan = bt_last_scan;
        bt_schedule_save();
    }
    bt_audio_event(ev, os88_ticks());
    bt_audio_state(bt_screen);
}

static void bt_setscreen(int screen)
{
    if (screen == BT_S_HELP || screen == BT_S_SETTINGS || screen == BT_S_RECORDS) {
        if (bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE) bt_return_screen = BT_S_PAUSE;
        else if (bt_screen != BT_S_HELP && bt_screen != BT_S_SETTINGS && bt_screen != BT_S_RECORDS)
            bt_return_screen = BT_S_HOME;
    }
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY)
        bt_pause(os88_ticks());
    bt_screen = screen;
    bt_about_up = 0;
    bt_guard_scan = bt_last_scan;
    bt_audio_state(screen);
    bt_redraw(1);
}

static void bt_begin(int mode)
{
    bt_start(mode, mode == BT_PRACTICE && bt_practice_weak ? -1 : bt_selection,
        bt_ch_duration, bt_ch_diff, os88_ticks());
    bt_screen = BT_S_PLAY;
    bt_guard_scan = bt_last_scan;
    bt_about_up = 0;
    bt_notice[0] = 0;
    bt_audio_state(bt_screen);
    bt_audio_event(BT_EV_START, os88_ticks());
    bt_redraw(1);
}

static void bt_pause_ui(void)
{
    if (bt_screen == BT_S_PAUSE && bt_session.status == BT_PAUSED) {
        bt_resume(os88_ticks()); bt_screen = BT_S_PLAY;
    } else if (bt_screen == BT_S_PLAY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
    } else return;
    bt_guard_scan = bt_last_scan;
    bt_audio_state(bt_screen);
    bt_redraw(1);
}

static void bt_fullscreen_toggle(void)
{
    int caps, was;
    static int kind; /* API out-parameter: SS is not DS. */
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
    }
    bt_audio_state(bt_screen);
    os88_wm_display(bt_win, &bt_video);
    if (!bt_full && bt_video.kind == OS88_VID_CGA && bt_store.display == 1) {
        caps = os88_fsx_caps(bt_win, &kind);
        if (!(caps & (1 << OS88_FSXM_CGA320))) { bt_message("CGA color mode unavailable."); bt_redraw(0); return; }
        bt_in_cga = 1;
        if (os88_fsx_run(bt_cga_entry, bt_win, 0) != 0) bt_message("Full screen is busy or unavailable.");
        bt_in_cga = 0;
        if (bt_quit_pending) { os88_onclose(bt_win); os88_wm_close(bt_win); return; }
        if (bt_save_pending) os88_wm_wake(bt_win);
        bt_guard_scan = 0x40;
        bt_audio_state(bt_screen);
        bt_redraw(1);
        return;
    }
    was = bt_full;
    bt_full = !bt_full;
    if (os88_fullscreen(bt_win, bt_full) != 0) {
        bt_full = was; bt_message("Another window owns full screen."); bt_redraw(0);
    }
    bt_guard_scan = bt_last_scan;
}

void bt_action(int a)
{
    int n;
    if (a >= BT_A_HINT_BASE) {
        bt_message("Keyboard is a hint. Type using the real keys.");
    } else if (a >= BT_A_LESSON_BASE && a < BT_A_LESSON_BASE + BT_LESSONS) {
        bt_selection = a - BT_A_LESSON_BASE;
    } else if (a >= BT_A_PROFILE_BASE && a < BT_A_PROFILE_BASE + BT_PROFILES) {
        bt_store.selected = a - BT_A_PROFILE_BASE;
        bt_selection = bt_recommend(); bt_schedule_save();
    } else if (a >= BT_A_DURATION_BASE && a < BT_A_DURATION_BASE + 3) {
        bt_ch_duration = a - BT_A_DURATION_BASE;
    } else if (a >= BT_A_DIFFICULTY_BASE && a < BT_A_DIFFICULTY_BASE + BT_DIFFS) {
        bt_ch_diff = a - BT_A_DIFFICULTY_BASE;
    } else if (a == BT_A_START) {
        if (bt_screen == BT_S_HOME) bt_selection = bt_recommend();
        bt_begin(bt_screen == BT_S_CHALLENGE ? BT_CHALLENGE : bt_screen == BT_S_PRACTICE ? BT_PRACTICE : BT_LESSON); return;
    } else if (a == BT_A_LESSONS) { bt_setscreen(BT_S_LESSONS); return;
    } else if (a >= BT_A_PRACTICE_BASE && a < BT_A_PRACTICE_BASE + BT_DIFFS) {
        bt_ch_diff = a - BT_A_PRACTICE_BASE; bt_practice_weak = 0;
    } else if (a == BT_A_PRACTICE || a == BT_A_WEAK) {
        bt_practice_weak = a == BT_A_WEAK; bt_setscreen(BT_S_PRACTICE); return;
    } else if (a == BT_A_CHALLENGE) { bt_setscreen(BT_S_CHALLENGE); return;
    } else if (a == BT_A_RECORDS) { bt_setscreen(BT_S_RECORDS); return;
    } else if (a == BT_A_SETTINGS) { bt_setscreen(BT_S_SETTINGS); return;
    } else if (a == BT_A_HELP) { bt_setscreen(BT_S_HELP); return;
    } else if (a == BT_A_PROFILE) { bt_profile_edit = 0; bt_setscreen(BT_S_PROFILE); return;
    } else if (a == BT_A_RETRY) {
        bt_selection = bt_session.lesson;
        bt_ch_duration = bt_session.duration; bt_ch_diff = bt_session.difficulty;
        bt_begin(bt_session.mode); return;
    } else if (a == BT_A_NEXT) { bt_selection = bt_recommend(); bt_begin(BT_LESSON); return;
    } else if (a == BT_A_BACK) {
        n = bt_screen == BT_S_HELP || bt_screen == BT_S_SETTINGS || bt_screen == BT_S_RECORDS ? bt_return_screen : BT_S_HOME;
        bt_schedule_save(); bt_setscreen(n); return;
    } else if (a == BT_A_PAUSE) { bt_pause_ui(); return;
    } else if (a == BT_A_FULL) { if (!bt_in_cga) bt_fullscreen_toggle(); return;
    } else if (a == BT_A_SAVE) { bt_schedule_save();
    } else if (a == BT_A_TEST_MUSIC || a == BT_A_TEST_VOICE) {
        if (a == BT_A_TEST_MUSIC && !bt_store.music) bt_message("Enable Music to audition the soundtrack.");
        else { bt_audio_test(a == BT_A_TEST_VOICE); if (a == BT_A_TEST_VOICE) bt_message(bt_voice_status()); }
    } else {
        if (a == BT_A_MUSIC) bt_store.music ^= 1;
        else if (a == BT_A_EFFECTS) bt_store.effects ^= 1;
        else if (a == BT_A_VOICE) bt_store.voice ^= 1;
        else if (a == BT_A_KEYCLICK) bt_store.keyclick ^= 1;
        else if (a == BT_A_ROAST) bt_store.roast = (bt_store.roast + 1) % 3;
        else if (a == BT_A_DISPLAY) {
            if (!bt_in_cga) os88_wm_display(bt_win, &bt_video);
            if (bt_video.kind == OS88_VID_CGA) bt_store.display ^= 1;
            else bt_message("Display follows the OS Control Panel selection.");
        } else return;
        bt_audio_state(bt_screen);
        bt_schedule_save();
    }
    n = bt_readonly;
    (void)n;
    bt_redraw(0);
}

static void ovl_bt_menu_key(int ascii, int scan)
{
    char *name;
    unsigned n;
    if (bt_screen == BT_S_SETTINGS) {
        if (ascii >= 'a' && ascii <= 'z') ascii -= 32;
        if (ascii == 'M') bt_action(BT_A_MUSIC);
        else if (ascii == 'E') bt_action(BT_A_EFFECTS);
        else if (ascii == 'V') bt_action(BT_A_VOICE);
        else if (ascii == 'K') bt_action(BT_A_KEYCLICK);
        else if (ascii == 'R') bt_action(BT_A_ROAST);
        else if (ascii == 'D') bt_action(BT_A_DISPLAY);
        else if (ascii == 'T') bt_action(BT_A_TEST_MUSIC);
        else if (ascii == 'A') bt_action(BT_A_TEST_VOICE);
        else if (ascii == 13) bt_action(BT_A_BACK);
        return;
    }
    if (bt_screen == BT_S_PROFILE) {
        if (ascii == 13) { bt_schedule_save(); bt_setscreen(BT_S_HOME); return; }
        if (scan == 0x48 || scan == 0x50) {
            bt_store.selected = (bt_store.selected + (scan == 0x48 ? 3 : 1)) & 3;
            bt_profile_edit = 0;
        } else {
            name = bt_store.profile[bt_store.selected].name;
            n = os88_strlen(name);
            if (ascii == 8 && n) name[n - 1] = 0;
            else if (ascii >= 32 && ascii <= 126) {
                if (!bt_profile_edit) { n = 0; name[0] = 0; bt_profile_edit = 1; }
                if (n < 12) { name[n] = (char)ascii; name[n + 1] = 0; }
            }
        }
        bt_redraw(0); return;
    }
    if (bt_screen == BT_S_LESSONS && (scan == 0x48 || scan == 0x50)) {
        if (scan == 0x48 && bt_selection) bt_selection--;
        if (scan == 0x50 && bt_selection < BT_LESSONS - 1) bt_selection++;
        bt_redraw(0); return;
    }
    if (bt_screen == BT_S_CHALLENGE) {
        if (ascii >= '1' && ascii <= '3') bt_ch_duration = ascii - '1';
        if (scan == 0x48 || scan == 0x50) bt_ch_diff = (bt_ch_diff + (scan == 0x48 ? 3 : 1)) & 3;
    }
    if (bt_screen == BT_S_PRACTICE) {
        if (ascii >= '1' && ascii <= '4') { bt_action(BT_A_PRACTICE_BASE + ascii - '1'); return; }
        if (scan == 0x48 || scan == 0x50) {
            bt_ch_diff = (bt_ch_diff + (scan == 0x48 ? 3 : 1)) & 3;
            bt_practice_weak = 0;
        }
        if (ascii == 'w' || ascii == 'W') { bt_action(BT_A_WEAK); return; }
    }
    if (ascii == 13) {
        bt_action(bt_screen == BT_S_RESULTS ? BT_A_NEXT : bt_screen == BT_S_PAUSE ? BT_A_PAUSE :
            bt_screen == BT_S_HELP || bt_screen == BT_S_SETTINGS || bt_screen == BT_S_RECORDS ? BT_A_BACK : BT_A_START);
        return;
    }
    if (ascii == 'l' || ascii == 'L') bt_action(BT_A_LESSONS);
    else if (ascii == 'p' || ascii == 'P') bt_action(BT_A_PRACTICE);
    else if (ascii == 'c' || ascii == 'C') bt_action(BT_A_CHALLENGE);
    else if (ascii == 'r' || ascii == 'R') bt_action(bt_screen == BT_S_RESULTS || bt_screen == BT_S_PAUSE ? BT_A_RETRY : BT_A_RECORDS);
    else if ((ascii == 'x' || ascii == 'X') && bt_screen == BT_S_PAUSE) bt_action(BT_A_BACK);
    else if (ascii == 's' || ascii == 'S') bt_action(BT_A_SETTINGS);
    else if (ascii == 'n' || ascii == 'N') bt_action(BT_A_PROFILE);
    bt_redraw(0);
}

static void bt_key_route(int ascii, int scan)
{
    bt_last_scan = scan;
    bt_modifiers = bt_modifier_state();
    if (ascii == 17) { /* Ctrl+Q is a control chord, never printable typing. */
        bt_quit_pending = 1;
        if (bt_in_cga) bt_cga_exit = 1;
        else { os88_onclose(bt_win); os88_wm_close(bt_win); }
        return;
    }
    if (bt_guard_scan) {
        if (os88_key_down(bt_guard_scan)) return;
        bt_guard_scan = 0;
    }
    if (bt_about_up) { bt_about_up = 0; bt_redraw(1); return; }
    if (scan == 0x3d) {
        bt_master_sound ^= 1; bt_audio_state(bt_screen); bt_redraw(0); return;
    }
    if (scan == 0x40 || (!ascii && scan == OS88_SCAN_ENTER)) {
        if (!bt_in_cga) bt_fullscreen_toggle(); return;
    }
    if (scan == 0x3b) { bt_action(BT_A_HELP); return; }
    if (scan == 0x3c) { bt_action(BT_A_LESSONS); return; }
    if (scan == 0x3e || ascii == 27) {
        if (bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE) bt_pause_ui();
        else bt_action(BT_A_BACK);
        return;
    }
    if (bt_screen == BT_S_PLAY) {
        bt_events(bt_key(ascii, os88_ticks())); bt_redraw(0); return;
    }
    ovl_bt_menu_key(ascii, scan);
}

static void bt_pulse(void)
{
    int focus;
    BTWord now;
    now = os88_ticks();
    bt_blink_frame = now % 91 < 2;
    bt_modifiers = bt_modifier_state();
    focus = os88_menu_owner() == bt_win;
    if (bt_focused && !focus && (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY)) {
        bt_pause(now); bt_screen = BT_S_PAUSE;
    }
    bt_focused = focus;
    if (bt_guard_scan && !os88_key_down(bt_guard_scan)) bt_guard_scan = 0;
    bt_events(bt_tick(now));
    bt_audio_tick(now, focus && !bt_readonly);
    if (focus) bt_redraw(0);
}

void os88_paint(void *win)
{
    if (bt_about_up) os88_about_card_d(win, bt_about_lines);
    else { bt_face_paint(win, 1); os88_wm_grow(win); }
    if (!bt_timer_ok && !bt_worker_live) {
        if (os88_task_spawn(win) == 0) bt_worker_live = 1;
        else bt_message("Timer busy. Close another app and reopen.");
    }
}
void os88_onkey(int ascii, int scan, void *win) { (void)win; bt_key_route(ascii, scan); }
void os88_onclick(int x, int y, void *win)
{
    (void)win;
    if (bt_about_up) { bt_about_up = 0; bt_redraw(1); return; }
    bt_last_scan = 0;
    bt_action(bt_face_click(x, y));
}
void os88_onresize(int w, int h, void *win)
{
    (void)w; (void)h; (void)win;
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
        bt_audio_state(bt_screen);
    }
    bt_redraw(1);
}
void os88_ontimer(void *win) { bt_pulse(); os88_wm_timer(win, 1); }
void os88_onwake(void *win)
{
    if (bt_save_pending) { bt_save_pending = 0; ovl_bt_save(); }
    os88_gfx_lock();
    if (!bt_timer_ok) bt_pulse();
    else bt_redraw(0);
    os88_gfx_unlock();
    (void)win;
}
void os88_worker(void *win)
{
    for (;;) { os88_task_alive(win); os88_wm_wake(win); os88_task_sleep(1); }
}
static void ovl_bt_about(void *win)
{
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
    }
    bt_audio_state(BT_S_PAUSE);
    bt_about_up = 1; os88_about_card(win, bt_about_lines);
}
static void ovl_bt_oncmd(int item, int menu, void *win)
{
    (void)win;
    bt_last_scan = 0;
    if (menu == 0) {
        if (item == 0) bt_action(BT_A_PROFILE);
        else if (item == 1) bt_action(BT_A_SAVE);
        else { os88_onclose(bt_win); os88_wm_close(bt_win); }
    } else if (menu == 1) bt_action(item == 0 ? BT_A_LESSONS : item == 1 ? BT_A_PRACTICE : item == 2 ? BT_A_CHALLENGE : BT_A_RECORDS);
    else if (menu == 2) bt_action(item == 0 ? BT_A_PAUSE : item == 1 ? BT_A_FULL : BT_A_SETTINGS);
    else if (menu == 3) {
        if (item == 0) { bt_master_sound ^= 1; bt_audio_state(bt_screen); bt_redraw(0); }
        else bt_audio_test(item == 2);
    } else bt_action(BT_A_HELP);
}
int os88_onclose(void *win)
{
    if (bt_closing) return 1;
    bt_closing = 1;
    bt_save_pending = 0;
    os88_wm_timer(win, 0);
    bt_audio_done();
    ovl_bt_save();
    if (bt_full) { bt_full = 0; os88_fullscreen(win, 0); }
    return 1;
}
void os88_onmove(unsigned was, unsigned now)
{
    unsigned artnow;
    if (bt_band == was) bt_band = now;
    bt_face_move(was, now);
    /* The eager parts move as one carve. Its base is not necessarily the
     * segment of either record: reread the runtime's already-relocated rows. */
    artnow = os88_part_seg(0);
    if (artnow != bt_art) { bt_face_move(bt_art, artnow); bt_art = artnow; }
    bt_soundpart = os88_part_seg(1);
    bx_setart2(bt_soundpart);
    bt_audio_move(was, now);
}
static struct os88_class bt_class = {OS88_CLASS_LEN, OS88_WF_SIZABLE | OS88_WF_OWNBG,
    os88_t_paint, os88_t_onkey, os88_t_onclick, os88_t_about, os88_t_onwake,
    os88_t_onclose, 0, os88_t_onresize, 0, 0, os88_t_ontimer, 0, 0};
static void *ovl_bt_main(void)
{
    bt_init();
    bt_readonly = bt_other_instance();
    ovl_bt_load();
    if (bt_readonly) bt_message("Read-only progress: another Borat session is open.");
    bt_selection = bt_recommend();
    bt_screen = BT_S_HOME;
    os88_video(&bt_video);
    os88_key_down(OS88_SCAN_ALT); /* arms the OS Alt+Enter synthesizer */
    os88_task_restartable(1); /* fallback worker only posts idempotent wakes */
    bt_copy_font();
    bt_art = os88_part_seg(0);
    bt_soundpart = os88_part_seg(1);
    bt_band = os88_mem_claim_p(512);
    if (!bt_band || !bt_art || !bt_soundpart) return 0;
    os88_mem_movable(bt_band, 1);
    bt_face_init(bt_art, bt_band, 8192, bt_video.bpp);
    bx_setart2(bt_soundpart);
    bt_audio_init();
    bt_win = os88_wm_create(8, OS88_MBAR_H + 2, bt_video.w - 16,
        bt_video.dock_top - OS88_MBAR_H - 4, "Borat Teaches Typing", &bt_class);
    if (!bt_win) { bt_audio_done(); return 0; }
    os88_wm_minsize(bt_win, 488, 150); /* content480x128, measured font/hitbox minimum */
    os88_menu_set(bt_win, &bt_menus);
    bt_timer_ok = os88_wm_timer(bt_win, 1) == 0;
    bt_audio_state(bt_screen);
    return bt_win;
}

/* Cold startup/native-menu code occupies the embedded second code segment.
 * The callbacks themselves remain resident runtime targets. */
void os88_about(void *win) { ovl_bt_about(win); }
void os88_oncmd(int item, int menu, void *win) { ovl_bt_oncmd(item, menu, win); }
void *os88_main(void) { return ovl_bt_main(); }
