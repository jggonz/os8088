/* CGA 320x200 four-colour FSX face, BORATYPE-SPEC 3.
 * No desktop drawing is legal here. Compose forty 8px text cells per row
 * in the idle persistence buffer; the assembly commit compares against the
 * borrowed drawing band's shadow and writes ONLY changed cells to VRAM.
 * The native UI, timing, input rules, audio and profiles remain the same.
 * F6 returns through FSX; Escape opens/resumes the pause screen. */
static BTByte bt_cga_fsi[OS88_FSI_SIZE];
static BTByte bt_cga_font[760];
static unsigned bt_cga_seg, bt_cga_stride, bt_cga_bstep;
static int bt_cga_exit, bt_cga_oldpose = -1;
static char bt_cga_num[8];
static BTWord bt_cga_last;
#define bt_cga_grid ((BTWord *)bt_io)
void bt_cga_clear_shadow(void);
void bt_cga_flush(void);
void bt_cga_portrait(int id, int x, int y);

static void bt_cga_text(int x, int y, const char *s, int ink, int paper)
{
    int at;
    if (y < 0 || y >= 25 || x < 0 || x >= 40) return;
    at = y * 40 + x;
    while (*s && x < 40) {
        bt_cga_grid[at++] = (BTByte)*s++ | ((ink | (paper << 4)) << 8);
        x++;
    }
}
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
static void bt_cga_keyboard(void)
{
    static const char *rows[4] = {"1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./"};
    int r, c, x, target, wrong, shifted;
    const char *s;
    target = bt_session.pos < bt_session.length ? bt_session.passage[bt_session.pos] : 0;
    shifted = target >= 'A' && target <= 'Z';
    if (shifted) target += 32;
    s = "!@#$%^&*()_+{}|:\"<>?";
    c = 0;
    while (s[c]) { if (target == s[c]) { target = "1234567890-=[]\\;',./"[c]; shifted = 1; break; } c++; }
    wrong = bt_session.last_wrong;
    if (wrong >= 'A' && wrong <= 'Z') wrong += 32;
    for (r = 0; r < 4; r++) {
        s = rows[r]; x = r == 1 ? 0 : 2 + (r > 1 ? r - 2 : 0);
        for (c = 0; s[c]; c++) {
            bt_cga_grid[(16 + r) * 40 + x] = (s[c] == target ? '>' : '[') | 0x0100;
            bt_cga_grid[(16 + r) * 40 + x + 1] = (BTByte)s[c] | (s[c] == target ? 0x0300 : s[c] == wrong ? 0x0200 : 0x0100);
            bt_cga_grid[(16 + r) * 40 + x + 2] = (s[c] == wrong ? '!' : ']') | 0x0100;
            x += 3;
        }
    }
    bt_cga_text(4, 20, shifted ? "[SHIFT*] [    SPACE    ] [BACK]" : "[SHIFT ] [    SPACE    ] [BACK]", 1, 0);
    if (target == ' ') bt_cga_text(15, 20, ">SPACE<", 3, 0);
}
static void bt_cga_paint(void)
{
    int i, start, at, limit, pose;
    BTProfile *p;
    for (i = 0; i < 1000; i++) bt_cga_grid[i] = 0x0020;
    p = &bt_store.profile[bt_store.selected];
    bt_cga_text(0, 0, "      BORAT TEACHES TYPING       F6 EXIT", 0, 1);
    bt_cga_text(1, 1, "L Lessons  P Practice  C Test  R Records", 1, 0);
    if (bt_screen == BT_S_PLAY || bt_screen == BT_S_PAUSE) {
        bt_cga_text(1, 3, bt_session.mode == BT_CHALLENGE ? "TIMED CHALLENGE" : bt_lessons[bt_session.lesson].name, 3, 0);
        if (bt_session.status == BT_READY) {
            bt_cga_text(10, 6, "GET READY", 3, 0);
            bt_cga_number(23, 6, bt_session.countdown);
        } else {
            start = (bt_session.pos / 72) * 72;
            limit = start + 72;
            if (limit > bt_session.length) limit = bt_session.length;
            for (i = start; i < limit; i++) {
                at = (5 + (i - start) / 36) * 40 + 2 + (i - start) % 36;
                bt_cga_grid[at] = (BTByte)bt_session.passage[i] | (i < bt_session.pos ? 0x0100 : i == bt_session.pos ? 0x3000 : 0x0300);
            }
            bt_cga_text(1, 9, "YOU:", 1, 0);
            start = bt_session.pos > 31 ? bt_session.pos - 31 : 0;
            for (i = start; i < bt_session.pos; i++) bt_cga_grid[9 * 40 + 6 + i - start] = (BTByte)bt_session.passage[i] | 0x0100;
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
    } else if (bt_screen == BT_S_SETTINGS) {
        bt_cga_text(1, 4, "SOUND AND DISPLAY", 3, 0);
        bt_cga_text(1, 7, "F3 master sound toggles immediately.", 1, 0);
        bt_cga_text(1, 10, "F6 returns to native settings.", 1, 0);
        bt_cga_text(1, 13, "CGA 320x200 - four colors", 3, 0);
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
        bt_cga_text(1, 17, "F6 returns to your desktop window.", 1, 0);
    }
    bt_cga_wrap(1, 22, bt_caption(), 38, 2, 3);
    bt_cga_text(0, 24, "F1 Help F2 Lessons F3 Sound F4 Pause Esc", 0, 1);
    bt_cga_flush();
    /* A portrait occupies the rightmost32x40 region only on non-typing
     * menus. Invalidate that region's shadow before returning to text. */
    pose = bt_expression();
    if (bt_screen == BT_S_HOME && pose != bt_cga_oldpose) {
        bt_cga_portrait(BT_ART_CGA + pose, 280, 40);
        bt_cga_oldpose = pose;
    } else if (bt_screen != BT_S_HOME && bt_cga_oldpose >= 0) {
        bt_cga_oldpose = -1; bt_cga_clear_shadow(); bt_cga_flush();
    }
}
void bt_cga_entry(void)
{
    int k;
    unsigned now;
    if (os88_fsx_mode(OS88_FSXM_CGA320, bt_cga_fsi) != 0) return;
    bt_cga_seg = bt_cga_fsi[0] | ((unsigned)bt_cga_fsi[1] << 8);
    bt_cga_stride = bt_cga_fsi[6] | ((unsigned)bt_cga_fsi[7] << 8);
    bt_cga_bstep = bt_cga_fsi[12] | ((unsigned)bt_cga_fsi[13] << 8);
    if (bt_cga_fsi[2] != 64 || bt_cga_fsi[3] != 1 || bt_cga_fsi[4] != 200 || bt_cga_fsi[5] ||
        bt_cga_fsi[9] != 2 || bt_cga_stride != 80 || bt_cga_bstep != 8192) return;
    bt_cga_exit = 0; bt_guard_scan = 0; bt_last_scan = 0;
    bt_cga_oldpose = -1;
    bt_cga_clear_shadow(); bt_cga_paint();
    bt_cga_last = os88_ticks();
    while (!bt_cga_exit) {
        k = os88_fsx_key(0);
        if (k) {
            if (((unsigned)k >> 8) == 0x40) bt_cga_exit = 1;
            else { bt_key_route(k & 255, (unsigned)k >> 8); bt_cga_paint(); }
        }
        now = os88_ticks();
        if ((BTWord)(now - bt_cga_last) >= 2) {
            bt_events(bt_tick(now));
            bt_audio_tick(now, !bt_readonly);
            bt_cga_last = now;
            bt_cga_paint();
        }
        os88_fsx_wait(OS88_FSXW_VSYNC);
    }
    if (bt_session.status == BT_ACTIVE || bt_session.status == BT_READY) {
        bt_pause(os88_ticks()); bt_screen = BT_S_PAUSE;
    }
}
