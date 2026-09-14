/* Window chrome, editor and command routing.  Included by sbasic.c. */

#define SBU_TAB_H       14
#define SBU_STATUS_H    12
#define SBU_SC_F5       0x3f
#define SBU_SC_F6       0x40
#define SBU_SC_F8       0x42
#define SBU_SC_LEFT     0x4b
#define SBU_SC_RIGHT    0x4d
#define SBU_SC_UP       0x48
#define SBU_SC_DOWN     0x50
#define SBU_SC_DELETE   0x53

#define SBU_VIEW_EDIT   0
#define SBU_VIEW_RUN    1

#define SBU_FILE_NEW    0
#define SBU_FILE_OPEN   1
#define SBU_FILE_SAVE   2
#define SBU_FILE_CLOSE  3
#define SBU_RUN_RUN     0
#define SBU_RUN_STOP    1
#define SBU_RUN_STEP    2
#define SBU_RUN_RESET   3
#define SBU_VIEW_EDITOR 0
#define SBU_VIEW_OUTPUT 1
#define SBU_VIEW_FULL   2
#define SBU_VIEW_CLEAR  3

struct sbu_menuset {
    const char *name;
    int oncmd;
    int nmenus;
    struct os88_menu menu[3];
};

static const char *sbu_file_items[] = {
    "New", "Open...", "Save As...", "Close"
};
static const char *sbu_run_items[] = {
    "Run  F5", "Stop", "Step  F8", "Reset"
};
static const char *sbu_view_items[] = {
    "Editor  F6", "Output  F6", "Full Screen  Ctrl+F", "Clear Output"
};
static struct sbu_menuset sbu_menus = {
    "Speedy Basic", 0, 3,
    { { "File", sbu_file_items, 4 },
      { "Run", sbu_run_items, 4 },
      { "View", sbu_view_items, 4 } }
};

static const char *sbu_about_lines[] = {
    "Speedy Basic for os8088",
    "Turbo Basic interpreter",
    "",
    "F5 Run  F8 Step  F6 View",
    "Ctrl+F Full Screen",
    0
};

static struct os88_pt sbu_org;
static struct os88_size sbu_size;
static char sbu_line[81];
static char sbu_status[81];
static char sbu_num[8];
static int sbu_view;
static int sbu_full;
static int sbu_about;
static int sbu_running;
static int sbu_scroll;
static unsigned sbu_cursor;
static int sbu_crow, sbu_ccol;

static void sbui_paint(void *win);

int sb_seg_insert(unsigned seg, unsigned len, unsigned pos, int ch);
int sb_seg_delete(unsigned seg, unsigned len, unsigned pos);

static void sbu_repaint(void *win)
{
    if (os88_wm_clip_set(win) < 0)
        return;
    sbui_paint(win);
}

static int sbu_source_byte(unsigned p)
{
    if (p >= sb_source_len)
        return 0;
    return os88_peek(sb_source_seg, p);
}

static unsigned sbu_line_at(int wanted)
{
    unsigned p;
    int line;

    p = 0;
    line = 0;
    while (p < sb_source_len && line < wanted) {
        if (sbu_source_byte(p++) == '\n')
            line++;
    }
    return p;
}

static void sbu_cursor_pos(void)
{
    unsigned p;
    int r, c, ch;

    p = 0;
    r = 0;
    c = 0;
    while (p < sbu_cursor && p < sb_source_len) {
        ch = sbu_source_byte(p++);
        if (ch == '\n') {
            r++;
            c = 0;
        } else if (ch != '\r')
            c++;
    }
    sbu_crow = r;
    sbu_ccol = c;
}

static void sbu_status_make(void)
{
    const char *state;

    sbu_cursor_pos();
    os88_strcpy(sbu_status, sbu_view == SBU_VIEW_EDIT ? "EDIT  " : "OUTPUT  ",
                sizeof(sbu_status));
    if (sbu_view == SBU_VIEW_EDIT) {
        os88_strcpy(sbu_status + os88_strlen(sbu_status), "Ln ", 4);
        os88_utoa((unsigned)(sbu_crow + 1), sbu_num);
        os88_strcpy(sbu_status + os88_strlen(sbu_status), sbu_num, 8);
        os88_strcpy(sbu_status + os88_strlen(sbu_status), " Col ", 6);
        os88_utoa((unsigned)(sbu_ccol + 1), sbu_num);
        os88_strcpy(sbu_status + os88_strlen(sbu_status), sbu_num, 8);
    } else {
        state = "Idle";
        if (sb_state() == SB_STATE_RUNNING) state = "Running";
        else if (sb_state() == SB_STATE_WAITING) state = "Waiting";
        else if (sb_state() == SB_STATE_DONE) state = "Done";
        else if (sb_state() == SB_STATE_ERROR) state = "Error";
        else if (sb_state() == SB_STATE_STOPPED) state = "Stopped";
        os88_strcpy(sbu_status + os88_strlen(sbu_status), state, 16);
        if (sb_state() == SB_STATE_ERROR) {
            os88_utoa((unsigned)sb_line(), sbu_num);
            os88_strcpy(sbu_status + os88_strlen(sbu_status), " Ln ", 5);
            os88_strcpy(sbu_status + os88_strlen(sbu_status), sbu_num, 8);
            os88_strcpy(sbu_status + os88_strlen(sbu_status), ": ", 3);
            os88_strcpy(sbu_status + os88_strlen(sbu_status), sb_error(),
                        sizeof(sbu_status) - os88_strlen(sbu_status));
        }
    }
}

static void sbu_editor_paint(int x, int y, int w, int h)
{
    unsigned p;
    int rows, row, n, ch, crow, ccol;

    rows = h / 8;
    p = sbu_line_at(sbu_scroll);
    for (row = 0; row < rows; row++) {
        n = 0;
        ch = 0;
        while (p < sb_source_len && n < w / 8 && n < 80) {
            ch = sbu_source_byte(p++);
            if (ch == '\n')
                break;
            if (ch != '\r')
                sbu_line[n++] = (char)(ch >= 32 ? ch : ' ');
        }
        while (n < w / 8 && n < 80)
            sbu_line[n++] = ' ';
        sbu_line[n] = 0;
        os88_font_run(x, y + row * 8, sbu_line, OS88_BLACK, OS88_WHITE);
        while (p < sb_source_len && ch != '\n')
            ch = sbu_source_byte(p++);
    }
    sbu_cursor_pos();
    crow = sbu_crow - sbu_scroll;
    ccol = sbu_ccol;
    if (crow >= 0 && crow < rows && ccol >= 0 && ccol < w / 8)
        os88_gfx_xor_fill(x + ccol * 8, y + crow * 8,
                          x + ccol * 8 + 7, y + crow * 8 + 7);
}

static void sbui_paint(void *win)
{
    int body_y, body_h, split;

    if (os88_wm_geom(win, &sbu_size) < 0)
        return;
    os88_wm_content(win, &sbu_org);
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(sbu_org.x, sbu_org.y,
                  sbu_org.x + sbu_size.w - 1, sbu_org.y + sbu_size.h - 1);

    split = sbu_size.w / 2;
    os88_font_run(sbu_org.x, sbu_org.y, " Editor ",
                  sbu_view == SBU_VIEW_EDIT ? OS88_WHITE : OS88_BLACK,
                  sbu_view == SBU_VIEW_EDIT ? OS88_BLACK : OS88_WHITE);
    os88_font_run(sbu_org.x + split, sbu_org.y, " Output ",
                  sbu_view == SBU_VIEW_RUN ? OS88_WHITE : OS88_BLACK,
                  sbu_view == SBU_VIEW_RUN ? OS88_BLACK : OS88_WHITE);
    os88_set_color(OS88_BLACK);
    os88_gfx_hline(sbu_org.x, sbu_org.x + sbu_size.w - 1,
                   sbu_org.y + SBU_TAB_H - 2);

    body_y = sbu_org.y + SBU_TAB_H;
    body_h = sbu_size.h - SBU_TAB_H - SBU_STATUS_H;
    if (body_h < 1)
        body_h = 1;
    if (sbu_view == SBU_VIEW_EDIT)
        sbu_editor_paint(sbu_org.x, body_y, sbu_size.w, body_h);
    else
        sbs_paint_surface(sbu_org.x, body_y, sbu_size.w, body_h);

    sbu_status_make();
    os88_font_run(sbu_org.x, sbu_org.y + sbu_size.h - SBU_STATUS_H,
                  sbu_status, OS88_WHITE, OS88_BLACK);
    if (sbu_about)
        os88_about_card_d(win, sbu_about_lines);
    os88_wm_grow(win);
}

static void sbu_toggle_full(void *win)
{
    sbu_full = !sbu_full;
    if (os88_fullscreen(win, sbu_full) < 0) {
        sbu_full = !sbu_full;
        os88_toast("Another window has it.", 0);
    }
}

static void sbu_run(void *win, int step)
{
    int st;

    st = sb_state();
    if (st != SB_STATE_RUNNING &&
        st != SB_STATE_WAITING) {
        if (sb_load_seg(sb_source_seg, sb_source_len) < 0) {
            sbu_view = SBU_VIEW_RUN;
            sbu_repaint(win);
            return;
        }
    }
    sbu_view = SBU_VIEW_RUN;
    sbu_running = !step;
    os88_wm_timer(win, 0);
    if (step) {
        sb_run_slice(1);
        sbu_repaint(win);
    } else
        os88_wm_wake(win);
}

static void sbu_new(void *win)
{
    sbu_running = 0;
    sb_stop();
    sb_source_len = 0;
    sbu_cursor = 0;
    sbu_scroll = 0;
    sbu_view = SBU_VIEW_EDIT;
    os88_strcpy(sb_file_name, "UNTITLED.BAS", sizeof(sb_file_name));
    sbu_repaint(win);
}

static void sbu_save(void *win)
{
    os88_file_dlg(OS88_FDLG_SAVE, win,
                  sb_file_name[0] ? sb_file_name : "PROGRAM.BAS");
}

static void sbu_editor_key(int ascii, int scan, void *win)
{
    if (scan == SBU_SC_LEFT && sbu_cursor > 0)
        sbu_cursor--;
    else if (scan == SBU_SC_RIGHT && sbu_cursor < sb_source_len)
        sbu_cursor++;
    else if (scan == SBU_SC_UP || scan == SBU_SC_DOWN) {
        sbu_cursor_pos();
        sbu_crow += scan == SBU_SC_UP ? -1 : 1;
        if (sbu_crow < 0) sbu_crow = 0;
        sbu_cursor = sbu_line_at(sbu_crow);
        while (sbu_ccol-- > 0 && sbu_cursor < sb_source_len &&
               sbu_source_byte(sbu_cursor) != '\n')
            sbu_cursor++;
    } else if (scan == SBU_SC_DELETE && sbu_cursor < sb_source_len) {
        sbu_running = 0;
        sb_stop();
        sb_source_len = (unsigned)sb_seg_delete(sb_source_seg, sb_source_len,
                                                sbu_cursor);
    } else if (ascii == 8 && sbu_cursor > 0) {
        sbu_running = 0;
        sb_stop();
        sbu_cursor--;
        sb_source_len = (unsigned)sb_seg_delete(sb_source_seg, sb_source_len,
                                                sbu_cursor);
    } else if ((ascii >= 32 && ascii < 127) || ascii == 13) {
        int c;
        sbu_running = 0;
        sb_stop();
        c = ascii == 13 ? '\n' : ascii;
        if (sb_source_len < SB_SOURCE_MAX) {
            sb_source_len = (unsigned)sb_seg_insert(sb_source_seg,
                                  sb_source_len, sbu_cursor, c);
            sbu_cursor++;
        }
    }
    sbu_cursor_pos();
    if (sbu_crow < sbu_scroll) sbu_scroll = sbu_crow;
    if (sbu_crow >= sbu_scroll + (sbu_size.h - SBU_TAB_H - SBU_STATUS_H) / 8)
        sbu_scroll = sbu_crow - (sbu_size.h - SBU_TAB_H - SBU_STATUS_H) / 8 + 1;
    sbu_repaint(win);
}

static void sbui_key(int ascii, int scan, void *win)
{
    if (ascii == 6) {                    /* Ctrl+F: BASIC owns bare letters */
        sbu_toggle_full(win);
        return;
    }
    if (sbu_about) {
        sbu_about = 0;
        sbu_repaint(win);
        return;
    }
    if (scan == SBU_SC_F5) {
        sbu_run(win, 0);
        return;
    }
    if (scan == SBU_SC_F8) {
        sbu_run(win, 1);
        return;
    }
    if (scan == SBU_SC_F6) {
        sbu_view = !sbu_view;
        sbu_repaint(win);
        return;
    }
    if (sbu_view == SBU_VIEW_EDIT)
        sbu_editor_key(ascii, scan, win);
    else {
        sb_key(ascii, scan);
        os88_wm_wake(win);
    }
}

static void sbui_click(int x, int y, void *win)
{
    if (sbu_about) {
        sbu_about = 0;
        sbu_repaint(win);
        return;
    }
    os88_wm_content(win, &sbu_org);
    os88_wm_geom(win, &sbu_size);
    if (y >= sbu_org.y && y < sbu_org.y + SBU_TAB_H) {
        sbu_view = x < sbu_org.x + sbu_size.w / 2 ?
                   SBU_VIEW_EDIT : SBU_VIEW_RUN;
        sbu_repaint(win);
    }
}

static void sbui_cmd(int item, int menu, void *win)
{
    if (menu == 0) {
        if (item == SBU_FILE_NEW) sbu_new(win);
        else if (item == SBU_FILE_OPEN) os88_file_dlg(OS88_FDLG_OPEN, win, 0);
        else if (item == SBU_FILE_SAVE) sbu_save(win);
        else if (item == SBU_FILE_CLOSE) os88_wm_close(win);
    } else if (menu == 1) {
        if (item == SBU_RUN_RUN) sbu_run(win, 0);
        else if (item == SBU_RUN_STOP) { sb_stop(); sbu_repaint(win); }
        else if (item == SBU_RUN_STEP) sbu_run(win, 1);
        else if (item == SBU_RUN_RESET) { sb_reset(); sbu_repaint(win); }
    } else if (menu == 2) {
        if (item == SBU_VIEW_EDITOR) { sbu_view = SBU_VIEW_EDIT; sbu_repaint(win); }
        else if (item == SBU_VIEW_OUTPUT) { sbu_view = SBU_VIEW_RUN; sbu_repaint(win); }
        else if (item == SBU_VIEW_FULL) sbu_toggle_full(win);
        else if (item == SBU_VIEW_CLEAR) { sbs_host_clear(OS88_BLACK); sbu_repaint(win); }
    }
}
