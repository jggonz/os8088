/* Speedy Basic: native os8088 window, editor and retained runtime display.
 * The parser/runner is sbcore.c; this file owns platform policy only. */

#include "os88.h"
#include "sbasic.h"

/* These buffers are fixed heap claims owned by this app. Avoid an interrupt
 * per source character/pixel; BASIC-visible addresses are checked separately. */
int sb_far_peek(unsigned seg, unsigned off);
void sb_far_poke(unsigned seg, unsigned off, int value);
#define os88_peek sb_far_peek
#define os88_poke sb_far_poke
#define sbu_editor_paint ovl_sbu_editor_paint
#define sbu_editor_key ovl_sbu_editor_key

static unsigned sb_source_seg;
static unsigned sb_source_len;
static char sb_file_name[13];
static void *sb_win;

#include "sbscreen.c"
#include "sbui.c"
#include "sbcore.c"

static struct sb_host sb_host = {
    sbs_host_mode,
    sbs_host_clear,
    sbs_host_text_put,
    sbs_host_text_cursor,
    sbs_host_pixel,
    sbs_host_line,
    sbs_host_sound,
    sbs_host_ticks,
    sbs_host_changed
};

static const char sb_welcome[] =
    "' SPEEDY BASIC FOR OS8088\n"
    "CLS\n"
    "COLOR 15, 1\n"
    "PRINT \"Speedy Basic is ready.\"\n"
    "PRINT \"Press F5 to run.\"\n";

static void sb_source_default(void)
{
    unsigned i;

    sb_source_len = (unsigned)os88_strlen(sb_welcome);
    for (i = 0; i < sb_source_len; i++)
        os88_poke(sb_source_seg, i, sb_welcome[i]);
    os88_poke(sb_source_seg, sb_source_len, 0);
    os88_strcpy(sb_file_name, "UNTITLED.BAS", sizeof(sb_file_name));
    sbu_cursor = 0;
}

void *os88_main(void)
{
    static struct os88_video v;
    void *win;
    int h;

    sb_source_seg = os88_mem_claim(SB_SOURCE_KB);
    if (sb_source_seg == 0) {
        os88_toast("Speedy: needs 32K.", 0);
        return 0;
    }
    sb_source_default();
    sbs_init();                         /* graphics may degrade; text remains */
    if (sb_init(&sb_host) < 0) {
        os88_toast("Speedy: core init failed.", 0);
        return 0;
    }

    os88_video(&v);
    h = v.dock_top - OS88_MBAR_H - 1;
    win = os88_wm_create(0, OS88_MBAR_H, v.w, h, "Speedy Basic");
    if (win == 0)
        return 0;
    sb_win = win;
    os88_wm_sizable(win, 1);
    os88_wm_snap(win, 1);
    os88_wm_ownbg(win, 1);
    os88_wm_minsize(win, 320, 160);
    os88_menu_set(win, (struct os88_menuset *)&sbu_menus);
    os88_about_set(win);
    os88_wm_onresize(win);
    os88_wm_onwake(win);
    os88_wm_ontimer(win);
    os88_key_down(0);                   /* arm make/break map before first key */
    return win;
}

void os88_paint(void *win)
{
    sbui_paint(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    sbui_key(ascii, scan, win);
}

void os88_onclick(int x, int y, void *win)
{
    sbui_click(x, y, win);
}

void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;
    (void)win;                          /* the following W_PAINT lays out live */
}

void os88_about(void *win)
{
    sbu_about = 1;
    os88_about_card(win, sbu_about_lines);
}

void os88_oncmd(int item, int menu, void *win)
{
    sbui_cmd(item, menu, win);
}

void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    unsigned got;

    if (mode == OS88_FDLG_SAVE) {
        if (os88_file_write_seg(name, sb_source_seg, sb_source_len) != 0) {
            os88_toast("Speedy: save failed.", 0);
            return;
        }
        os88_strcpy(sb_file_name, name, sizeof(sb_file_name));
        sbu_repaint(win);
        return;
    }

    if (size_hi || size_lo > SB_SOURCE_MAX) {
        os88_toast("Speedy: program too big.", 0);
        return;
    }
    sb_stop();
    got = os88_file_read_seg(name, sb_source_seg, SB_SOURCE_KB << 10);
    if (got == 0 && os88_ferr() != OS88_FERR_OK) {
        os88_toast("Speedy: open failed.", 0);
        return;
    }
    sb_source_len = got;
    os88_poke(sb_source_seg, sb_source_len, 0);
    os88_strcpy(sb_file_name, name, sizeof(sb_file_name));
    sbu_cursor = 0;
    sbu_scroll = 0;
    sbu_view = SBU_VIEW_EDIT;
    sbu_running = 0;
    sb_stop();
    sbu_repaint(win);                   /* dialog exposed an arbitrary region */
}

void os88_onwake(void *win)
{
    static int last_state = -1;
    static unsigned last_paint;
    int state;
    unsigned now;

    state = sb_state();
    if (sbu_running && (state == SB_STATE_READY || state == SB_STATE_RUNNING ||
        state == SB_STATE_WAITING))
        state = sb_run_slice(SB_SLICE_OPS);

    now = os88_ticks();
    if (state != last_state || (sbs_dirty &&
        (state != SB_STATE_RUNNING || (unsigned)(now-last_paint) >= 3))) {
        os88_gfx_lock();
        sbu_repaint(win);
        os88_gfx_unlock();
        last_state = state;
        last_paint = now;
    }
    if (sbu_running && state == SB_STATE_RUNNING)
        os88_wm_wake(win);
    else if (sbu_running && state == SB_STATE_WAITING)
        os88_wm_timer(win, 1);
}

void os88_ontimer(void *win)
{
    os88_wm_wake(win);
}
