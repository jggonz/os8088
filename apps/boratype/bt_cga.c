/* CGA 320x200 four-colour FSX face, BORATYPE-SPEC 3.
 * No desktop drawing is legal here. Compose forty 8px text cells per row
 * in the idle persistence buffer; the assembly commit compares against the
 * borrowed drawing band's shadow and writes ONLY changed cells to VRAM.
 * The native UI, timing, input rules, audio and profiles remain the same.
 * F6 returns through FSX; Escape opens/resumes the pause screen. */
static BTByte bt_cga_fsi[OS88_FSI_SIZE];
static BTByte bt_cga_font[760];
static unsigned bt_cga_seg, bt_cga_stride, bt_cga_bstep;
static int bt_cga_oldpose = -1;
static char bt_cga_num[8];
static BTWord bt_cga_last;
static struct os88_mouse bt_cga_mouse;
static int bt_cga_mx, bt_cga_my, bt_cga_mdown, bt_cga_pointer_up;
#define bt_cga_grid ((BTWord *)bt_io)
void bt_cga_clear_shadow(void);
void bt_cga_flush(void);
void bt_cga_portrait(int id, int x, int y);
void bt_cga_palette(void);
void bt_cga_pointer(int x, int y);

/* Bounded native row composition and clear, the same character/attribute
 * workspace. Repeated C byte stores cost the XT more than the VRAM commit. */
void bt_cga_text(int x, int y, const char *s, int ink, int paper);
void bt_cga_grid_clear(int keep_keys);
static void bt_cga_number(int x, int y, unsigned n)
{
    os88_utoa(n, bt_cga_num);
    bt_cga_text(x, y, bt_cga_num, 3, 0);
}
static void bt_cga_wrap(int x, int y, const char *s, int width, int rows, int ink)
{
    int col;
    col = 0;
    while (*s && rows) {
        if (col == width) { col = 0; y++; rows--; if (!rows) break; }
        bt_cga_grid[y * 40 + x + col] = (BTByte)*s++ | (ink << 8);
        col++;
    }
}
static void bt_cga_stats(void)
{
    bt_cga_text(1, 13, "WPM", 1, 0);
    if (bt_session.seconds) bt_cga_number(5, 13, bt_session.wpm);
    else bt_cga_text(5, 13, "--", 3, 0);
    bt_cga_text(11, 13, "ACC", 1, 0);
    if (bt_session.attempts) bt_cga_number(15, 13, bt_session.accuracy);
    else bt_cga_text(15, 13, "--", 3, 0);
    bt_cga_text(20, 13, "ERR", 1, 0); bt_cga_number(24, 13, bt_session.errors);
    bt_cga_text(29, 13, "SEC", 1, 0);
    bt_cga_number(33, 13, bt_session.mode == BT_CHALLENGE ? bt_session.remaining : bt_session.seconds);
}
static BTWord bt_cga_keycell[95];
static int bt_cga_keys_cached, bt_cga_target_cell, bt_cga_wrong_cell;
static int bt_cga_old_target, bt_cga_old_wrong, bt_cga_raw_target = -1;
static int bt_cga_shift = -1;
/* Only cold scene setup visits every physical key. The pure native face map
 * supplies case/punctuation normalization, so both presentations teach one
 * physical US keyboard. Values are workspace word indices, never pointers. */
static void ovl_bt_cga_keys_init(void)
{
    static const char *rows[4] = {"`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./"};
    const char *s;
    int r, c, x, at, base;
    for (c = 0; c < 95; c++) bt_cga_keycell[c] = 0;
    for (r = 0; r < 4; r++) {
        s = rows[r]; x = r < 2 ? 0 : r;
        for (c = 0; s[c]; c++) {
            at = (16 + r) * 40 + x;
            bt_cga_keycell[(BTByte)s[c] - 32] = at;
            bt_cga_grid[at] = '[' | 0x0100;
            bt_cga_grid[at + 1] = (BTByte)s[c] | 0x0100;
            bt_cga_grid[at + 2] = ']' | 0x0100;
            x += 3;
        }
    }
    bt_cga_keycell[0] = 811; /* '[' before SPACE at row20,column11 */
    for (c = 32; c < 127; c++) {
        base = bt_key_base(c);
        if (base >= 32) bt_cga_keycell[c - 32] = bt_cga_keycell[base - 32];
    }
    bt_cga_old_target = bt_cga_old_wrong = 0;
    bt_cga_raw_target = bt_cga_shift = -1;
}
static void bt_cga_keypaint(int at)
{
    int ink;
    if (!at) return;
    if (at == 811) {
        bt_cga_text(12, 20, "SPACE", bt_cga_target_cell == at ? 0 : 1,
            bt_cga_target_cell == at ? 3 : 0);
        bt_cga_grid[817] = (bt_cga_wrong_cell == at ? '!' : ']') | 0x0100;
        return;
    }
    ink = at == bt_cga_target_cell ? 3 : at == bt_cga_wrong_cell ? 2 : 1;
    bt_cga_grid[at] = (at == bt_cga_target_cell ? '>' : '[') | 0x0100;
    bt_cga_grid[at + 1] = (bt_cga_grid[at + 1] & 255) | (ink << 8);
    bt_cga_grid[at + 2] = (at == bt_cga_wrong_cell ? '!' : ']') | 0x0100;
}
static void bt_cga_keyboard(void)
{
    int raw, wrong, shifted, initialized;
    initialized = !bt_cga_keys_cached;
    if (initialized) ovl_bt_cga_keys_init();
    raw = bt_session.pos < bt_session.length ? bt_session.passage[bt_session.pos] : 0;
    wrong = bt_session.last_wrong;
    bt_cga_target_cell = raw >= 32 && raw < 127 ? bt_cga_keycell[raw - 32] : 0;
    bt_cga_wrong_cell = wrong >= 32 && wrong < 127 ? bt_cga_keycell[wrong - 32] : 0;
    if (raw != bt_cga_raw_target) {
        bt_cga_text(0, 4, "                                        ", 0, 0);
        bt_cga_text(1, 4, "Finger:", 1, 0);
        bt_cga_text(9, 4, bt_key_finger(raw), 3, 0);
        shifted = bt_key_shift(raw);
        if (shifted) bt_cga_text(22, 4, shifted == 1 ? "+ left Shift" : "+ right Shift", 1, 0);
        if (shifted != bt_cga_shift) {
            bt_cga_text(1, 20, shifted == 1 ? "[LShift*] [SPACE] [RShift ] [BACK]" :
                shifted == 2 ? "[LShift ] [SPACE] [RShift*] [BACK]" :
                "[LShift ] [SPACE] [RShift ] [BACK]", 1, 0);
            bt_cga_keypaint(811);
            bt_cga_shift = shifted;
        }
        bt_cga_raw_target = raw;
    }
    if (initialized || bt_cga_target_cell != bt_cga_old_target || bt_cga_wrong_cell != bt_cga_old_wrong) {
        bt_cga_keypaint(bt_cga_old_target); bt_cga_keypaint(bt_cga_old_wrong);
        bt_cga_keypaint(bt_cga_target_cell); bt_cga_keypaint(bt_cga_wrong_cell);
        bt_cga_old_target = bt_cga_target_cell; bt_cga_old_wrong = bt_cga_wrong_cell;
    }
}

static void bt_cga_paint(void)
{
    int i, start, at, limit, pose, portrait, portrait_x;
    BTProfile *p;
    if (bt_cga_pointer_up) { bt_cga_pointer(bt_cga_mx, bt_cga_my); bt_cga_pointer_up = 0; }
    bt_cga_grid_clear(bt_cga_keys_cached && (bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE));
    p = &bt_store.profile[bt_store.selected];
    bt_cga_text(0, 0, "      BORAT TEACHES TYPING       F6 EXIT", 0, 1);
    bt_cga_text(1, 1, "L Lessons  P Practice  C Test  R Records", 1, 0);
    if (bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE) {
        bt_cga_text(1, 3, bt_session.mode == BT_CHALLENGE ? "TIMED CHALLENGE" : bt_lessons[bt_session.lesson].name, 3, 0);
        if (bt_session.status == BT_READY) {
            bt_cga_text(10, 6, "GET READY", 3, 0);
            bt_cga_number(23, 6, bt_session.countdown);
        } else {
            start = (bt_session.pos / 66) * 66;
            limit = start + 66;
            if (limit > bt_session.length) limit = bt_session.length;
            for (i = start; i < limit; i++) {
                at = (5 + (i - start) / 33) * 40 + 6 + (i - start) % 33;
                bt_cga_grid[at] = (BTByte)bt_session.passage[i] | (i < bt_session.pos ? 0x0100 : i == bt_session.pos ? 0x3000 : 0x0300);
            }
            bt_cga_text(5, 9, "YOU:", 1, 0);
            start = bt_session.pos > 28 ? bt_session.pos - 28 : 0;
            for (i = start; i < bt_session.pos; i++) bt_cga_grid[9 * 40 + 10 + i - start] = (BTByte)bt_session.passage[i] | 0x0100;
            if (bt_screen == BT_S_PAUSE) bt_cga_text(5, 11, "PAUSED - Enter resumes; R retries", 0, 3);
            else bt_cga_text(1, 11, "Target >  Wrong !  Shift*  Back retracts", 1, 0);
        }
        bt_cga_stats();
        bt_cga_keyboard();
    } else if (bt_screen == BT_S_LESSONS) {
        start = (bt_selection / 12) * 12;
        for (i = start; i < start + 12 && i < BT_LESSONS; i++) {
            bt_cga_number(1, 4 + i - start, i + 1);
            bt_cga_text(4, 4 + i - start, bt_lessons[i].name, i == bt_selection ? 0 : 3, i == bt_selection ? 3 : 0);
        }
        bt_cga_text(1, 18, "Up/Down choose. Enter starts lesson.", 1, 0);
    } else if (bt_screen == BT_S_RESULTS) {
        bt_cga_text(1, 3, "GREAT SUCCESS? YOUR RESULT:", 3, 0);
        bt_cga_text(1, 5, "Speed WPM:", 1, 0); bt_cga_number(16, 5, bt_session.wpm);
        bt_cga_text(1, 7, "Accuracy %:", 1, 0); bt_cga_number(16, 7, bt_session.accuracy);
        bt_cga_text(1, 9, "Errors:", 1, 0); bt_cga_number(16, 9, bt_session.errors);
        bt_cga_text(1, 11, "Best streak:", 1, 0); bt_cga_number(16, 11, bt_session.best_streak);
        bt_cga_text(1, 14, bt_session.ranked ? "Uninterrupted result" : "Practice / unranked", 3, 0);
        bt_cga_text(1, 17, "Enter Next  R Retry  P Practice", 1, 0);
    } else if (bt_screen == BT_S_CHALLENGE) {
        bt_cga_text(1, 4, "CHOOSE YOUR CHALLENGE", 3, 0);
        bt_cga_text(1, 7, "1 = 1 min   2 = 3 min   3 = 5 min", 1, 0);
        bt_cga_text(1, 9, "Duration choice:", 3, 0); bt_cga_number(19, 9, bt_ch_duration + 1);
        bt_cga_text(1, 12, "Up/Down difficulty: home/letters/", 1, 0);
        bt_cga_text(1, 13, "digits/mixed. Enter starts.", 1, 0); bt_cga_number(30, 13, bt_ch_diff + 1);
    } else if (bt_screen == BT_S_RECORDS) {
        bt_cga_text(1, 4, "YOUR GREAT SUCCESSES", 3, 0);
        bt_cga_text(1, 7, p->name, 1, 0);
        bt_cga_text(1, 10, "Best streak", 1, 0); bt_cga_number(20, 10, p->best_streak);
        for (i = 0; i < 3; i++) {
            bt_cga_number(2, 13 + i, i == 0 ? 1 : i == 1 ? 3 : 5);
            bt_cga_text(5, 13 + i, "minute WPM", 1, 0); bt_cga_number(20, 13 + i, p->record_wpm[i][bt_ch_diff]);
        }
    } else if (bt_screen == BT_S_PRACTICE) {
        bt_cga_text(1, 4, "PRACTICE YOUR KEYS", 3, 0);
        bt_cga_text(1, 7, "1 Home  2 Letters  3 Digits  4 Mixed", 1, 0);
        bt_cga_text(1, 10, "Group:", 3, 0); bt_cga_number(8, 10, bt_ch_diff + 1);
        bt_cga_text(1, 13, "W weak-key recommendation", 1, 0);
        bt_cga_text(1, 15, bt_practice_weak ? "Weak-key drill selected" : "Group drill selected", 3, 0);
        bt_cga_text(1, 18, "Enter starts. Up/Down changes group.", 1, 0);
    } else if (bt_screen == BT_S_PROFILE) {
        bt_cga_text(1, 4, "YOUR NAME, GREAT TYPIST?", 3, 0);
        for (i = 0; i < BT_PROFILES; i++) bt_cga_text(2, 7 + i * 2, bt_store.profile[i].name,
            i == bt_store.selected ? 0 : 3, i == bt_store.selected ? 3 : 0);
        bt_cga_text(1, 17, "Up/Down chooses profile. Type a name.", 1, 0);
        bt_cga_text(1, 19, "Enter saves and returns to Menu.", 1, 0);
    } else if (bt_screen == BT_S_SETTINGS) {
        bt_cga_text(1, 4, "SOUND AND DISPLAY", 3, 0);
        bt_cga_text(1, 7, bt_store.music ? "M Music ON" : "M Music OFF", 1, 0);
        bt_cga_text(21, 7, bt_store.effects ? "E Effects ON" : "E Effects OFF", 1, 0);
        bt_cga_text(1, 9, bt_store.keyclick ? "K Key ticks ON" : "K Key ticks OFF", 1, 0);
        bt_cga_text(1, 11, "R Roast level:", 1, 0); bt_cga_number(17, 11, bt_store.roast + 1);
        bt_cga_text(1, 13, "T Test music", 1, 0);
        bt_cga_text(1, 15, "Music and effects follow OS output.", 3, 0);
        bt_cga_text(1, 18, "F6 native display; Enter returns Menu.", 1, 0);
    } else if (bt_screen == BT_S_HELP) {
        bt_cga_text(1, 4, "Correct keys advance; wrong keys wait.", 3, 0);
        bt_cga_text(1, 6, "Back retracts, keeps historical errors.", 1, 0);
        bt_cga_text(1, 8, "WPM uses accepted progress and time.", 1, 0);
        bt_cga_text(1, 10, "95% completes mastery. Any lesson open.", 1, 0);
        bt_cga_text(1, 12, "F4/Esc pause; paused tests unranked.", 1, 0);
        bt_cga_text(1, 14, "L/P/C/R menu actions; Enter starts.", 1, 0);
        bt_cga_text(1, 16, "All real letters are input in a lesson.", 1, 0);
    } else {
        bt_cga_text(7, 4, "VERY NICE! LET US TYPE.", 3, 0);
        bt_cga_text(1, 7, "Enter: recommended lesson", 1, 0);
        bt_cga_text(1, 9, "L: lessons     P: practice", 1, 0);
        bt_cga_text(1, 11, "C: challenge   R: records", 1, 0);
        bt_cga_text(1, 13, "F1: help       F3: sound", 1, 0);
        bt_cga_text(1, 15, p->name, 3, 0);
        bt_cga_text(1, 17, "F6 Desktop     Ctrl+Q Quit", 1, 0);
    }
    bt_cga_wrap(1, 22, bt_caption(), 38, 2, 3);
    bt_cga_text(0, 24, "F1 Help F2 Lessons F3 Sound F4 Pause Esc", 0, 1);
    pose = bt_expression();
    portrait = bt_screen == BT_S_HOME || bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE;
    portrait_x = bt_screen == BT_S_HOME ? 280 : 0;
    pose = portrait ? pose + (bt_blink_frame && pose == 1 ? 8 : 0) + (portrait_x ? 32 : 16) : -1;
    if ((pose < 0 && bt_cga_oldpose >= 0) ||
        (pose >= 0 && bt_cga_oldpose >= 0 && ((pose ^ bt_cga_oldpose) & 48))) bt_cga_clear_shadow();
    bt_cga_flush();
    if (portrait && pose != bt_cga_oldpose) {
        i = bt_expression();
        bt_cga_portrait(i == 1 && bt_blink_frame ? BT_ART_BLINK_CGA : BT_ART_CGA + i, portrait_x, 40);
    }
    bt_cga_keys_cached = bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE;
    bt_cga_oldpose = pose;
    bt_cga_pointer(bt_cga_mx, bt_cga_my); bt_cga_pointer_up = 1;
    bt_scene_drawn();
}
static void bt_cga_click(int col, int row)
{
    bt_last_scan = 0;
    if (row == 1) { bt_action(col < 10 ? BT_A_LESSONS : col < 22 ? BT_A_PRACTICE : col < 31 ? BT_A_CHALLENGE : BT_A_RECORDS); return; }
    if (row == 24) {
        if (col < 8) bt_action(BT_A_HELP);
        else if (col < 19) bt_action(BT_A_LESSONS);
        else bt_key_route(col >= 36 ? 27 : 0, col < 28 ? 0x3d : 0x3e);
        return;
    }
    if (bt_screen == BT_S_HOME) {
        if (row == 7) bt_action(BT_A_START);
        else if (row == 9) bt_action(col < 15 ? BT_A_LESSONS : BT_A_PRACTICE);
        else if (row == 11) bt_action(col < 15 ? BT_A_CHALLENGE : BT_A_RECORDS);
        else if (row == 13) { if (col < 15) bt_action(BT_A_HELP); else bt_key_route(0, 0x3d); }
    } else if (bt_screen == BT_S_LESSONS) {
        if (row >= 4 && row < 16) bt_action(BT_A_LESSON_BASE + (bt_selection / 12) * 12 + row - 4);
        else if (row == 18) bt_action(BT_A_START);
    } else if (bt_screen == BT_S_RESULTS && row == 17) {
        bt_action(col < 12 ? BT_A_NEXT : col < 23 ? BT_A_RETRY : BT_A_WEAK);
    } else if (bt_screen == BT_S_PAUSE && row == 11) bt_action(BT_A_PAUSE);
    else if (bt_screen == BT_S_PLAY && row >= 16 && row <= 20) bt_action(BT_A_HINT_BASE + ' ');
    else if (bt_screen == BT_S_CHALLENGE) {
        if (row == 7) bt_action(BT_A_DURATION_BASE + (col < 12 ? 0 : col < 24 ? 1 : 2));
        else if (row == 13) bt_action(BT_A_DIFFICULTY_BASE + (bt_ch_diff + 1) % 4);
        else if (row == 12) bt_action(BT_A_START);
    } else if (bt_screen == BT_S_PRACTICE) {
        if (row == 7) bt_action(BT_A_PRACTICE_BASE + (col < 9 ? 0 : col < 20 ? 1 : col < 30 ? 2 : 3));
        else if (row == 13) bt_action(BT_A_WEAK);
        else if (row == 18) bt_action(BT_A_START);
    } else if (bt_screen == BT_S_PROFILE && row >= 7 && row <= 13) bt_action(BT_A_PROFILE_BASE + (row - 7) / 2);
    else if (bt_screen == BT_S_SETTINGS) {
        if (row == 7) bt_action(col < 20 ? BT_A_MUSIC : BT_A_EFFECTS);
        else if (row == 9) bt_action(BT_A_KEYCLICK);
        else if (row == 11) bt_action(BT_A_ROAST);
        else if (row == 13) bt_action(BT_A_TEST_MUSIC);
    }
}
void bt_cga_entry(void)
{
    int k, mx, my;
    unsigned now;
    if (os88_fsx_mode(OS88_FSXM_CGA320, bt_cga_fsi) != 0) return;
    bt_cga_seg = bt_cga_fsi[0] | ((unsigned)bt_cga_fsi[1] << 8);
    bt_cga_stride = bt_cga_fsi[6] | ((unsigned)bt_cga_fsi[7] << 8);
    bt_cga_bstep = bt_cga_fsi[12] | ((unsigned)bt_cga_fsi[13] << 8);
    if (bt_cga_fsi[2] != 64 || bt_cga_fsi[3] != 1 || bt_cga_fsi[4] != 200 || bt_cga_fsi[5] ||
        bt_cga_fsi[9] != 2 || bt_cga_stride != 80 || bt_cga_bstep != 8192) return;
    bt_cga_palette();
    bt_cga_exit = 0; bt_guard_scan = 0; bt_last_scan = 0;
    bt_cga_pointer_up = 0; bt_cga_mdown = 1; bt_cga_mx = 160; bt_cga_my = 100;
    bt_cga_oldpose = -1; bt_cga_keys_cached = 0;
    bt_cga_clear_shadow(); bt_cga_paint();
    bt_cga_last = os88_ticks();
    while (!bt_cga_exit) {
        k = os88_fsx_key(0);
        if (k) {
            if (((unsigned)k >> 8) == 0x40) bt_cga_exit = 1;
            else { bt_key_route(k & 255, (unsigned)k >> 8); bt_cga_paint(); }
        }
        now = os88_ticks();
        os88_mouse(&bt_cga_mouse);
        mx = bt_muldiv(bt_cga_mouse.x, 320, bt_video.w);
        my = bt_muldiv(bt_cga_mouse.y, 200, bt_video.h);
        if (mx < 2) mx = 2; if (mx > 317) mx = 317;
        if (my < 2) my = 2; if (my > 197) my = 197;
        if (mx != bt_cga_mx || my != bt_cga_my) {
            if (bt_cga_pointer_up) bt_cga_pointer(bt_cga_mx, bt_cga_my);
            bt_cga_mx = mx; bt_cga_my = my;
            bt_cga_pointer(mx, my); bt_cga_pointer_up = 1;
        }
        if ((bt_cga_mouse.btn & 1) && !bt_cga_mdown) {
            bt_cga_click(mx / 8, my / 8); bt_cga_paint();
        }
        bt_cga_mdown = bt_cga_mouse.btn & 1;
        if ((BTWord)(now - bt_cga_last) >= 2) {
            bt_blink_frame = now % 91 < 2;
            bt_events(bt_tick(now));
            bt_audio_tick(now, 1);
            bt_cga_last = now;
            bt_cga_paint();
        }
        os88_fsx_wait(OS88_FSXW_VSYNC);
    }
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
    }
}
