/* Speedy Basic: native os8088 window, editor and retained runtime display.
 * The parser/runner is sbcore.c; this file owns platform policy only. */

#include "os88.h"
#include "sbasic.h"
#include "sbcompile.h"

static unsigned sb_source_seg;
static unsigned sb_source_len;
static unsigned sb_source_kb;
static char sb_file_name[13];
static void *sb_win;
static char sb_ui_text[81];

static int ovl_sb_source_resize(unsigned need);

#include "sbscreen.c"
#include "sbui.c"
#include "sbcore.c"
#define sbw_bind         ovl_sbw_bind
#define sbw_header       ovl_sbw_header
#define sbw_stack_class  ovl_sbw_stack_class
#define sbw_name         ovl_sbw_name
#define sbw_write        ovl_sbw_write
#define sbw_last_error   ovl_sbw_last_error
#define sbw_file_error   ovl_sbw_file_error
#define sbw_fail         ovl_sbw_fail
#define sbw_byte         ovl_sbw_byte
#define sbw_get          ovl_sbw_get
#define sbw_word         ovl_sbw_word
#define sbw_get_word     ovl_sbw_get_word
#define sbw_assoc_ok     ovl_sbw_assoc_ok
#define sbw_name_ok      ovl_sbw_name_ok
#define sbw_final_ok     ovl_sbw_final_ok
#include "compiler/writer.c"
#include "compiler/aot.c"
#include "compiler/guest.c"
#undef sbw_bind
#undef sbw_header
#undef sbw_stack_class
#undef sbw_name
#undef sbw_write
#undef sbw_last_error
#undef sbw_file_error
#undef sbw_fail
#undef sbw_byte
#undef sbw_get
#undef sbw_word
#undef sbw_get_word
#undef sbw_assoc_ok
#undef sbw_name_ok
#undef sbw_final_ok

static struct sb_host sb_host = {
    sbs_host_mode,
    sbs_host_clear,
    sbs_host_text_put,
    sbs_host_text_cursor,
    sbs_host_text_scroll,
    sbs_host_pixel,
    sbs_host_line,
    sbs_host_point,
    sbs_host_circle,
    sbs_host_paint,
    sbs_host_gfx_get,
    sbs_host_gfx_put,
    sbs_host_video_peek,
    sbs_host_video_poke,
    sbs_host_port_out,
    sbs_host_palette,
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

/* Load the cold half before the first W_PAINT.  Editor/status painting calls
 * into it, so a missing overlay refuses launch rather than doing file I/O
 * from inside the window manager's paint bracket. */
static int ovl_speedy_ready(void)
{
    return 1;
}

static int ovl_sb_source_resize(unsigned need)
{
    unsigned blocks, kb, seg, cap;

    blocks = (need + 4095u) >> 12;
    if (!blocks) blocks = 1;
    kb = blocks << 2;
    if (kb == sb_source_kb) return 0;
    if (sb_source_seg) seg = os88_mem_regrow(sb_source_seg, (int)kb);
    else seg = os88_mem_claim((int)kb);
    if (!seg) return -1;
    sb_source_seg = seg;
    sb_source_kb = kb;
    cap = kb << 10;
    if (sb_source_len >= cap) {
        sb_source_len = cap - 1;
        os88_poke(sb_source_seg, sb_source_len, 0);
    }
    return 0;
}

static void ovl_sb_source_default(void)
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

    if (!ovl_speedy_ready())
        return 0;
    sb_compile_set_home();
    if (ovl_sb_source_resize(1) < 0) {
        os88_toast("Speedy: needs 4K.", 0);
        return 0;
    }
    ovl_sb_source_default();
    ovl_sbs_init();
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
    ovl_sbui_paint(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    ovl_sbui_key(ascii, scan, win);
}

void os88_onclick(int x, int y, void *win)
{
    ovl_sbui_click(x, y, win);
}

void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;
    (void)win;                          /* the following W_PAINT lays out live */
}

static void ovl_speedy_about(void *win)
{
    sbu_about = 1;
    os88_about_card(win, sbu_about_lines);
}

void os88_about(void *win)
{
    ovl_speedy_about(win);
}

void os88_oncmd(int item, int menu, void *win)
{
    ovl_sbui_cmd(item, menu, win);
}

static void ovl_speedy_onfile(int mode, const char *name, unsigned size_lo,
                             unsigned size_hi, void *win)
{
    unsigned got;

    if (mode == OS88_FDLG_SAVE) {
        if (sbu_dialog == SBU_DLG_PACKAGE_SAVE) {
            sbu_dialog = SBU_DLG_NONE;
            os88_strcpy(sbu_package_name, name, sizeof(sbu_package_name));
            sbu_build = SBU_BUILD_READY;
            os88_wm_wake(win);
            ovl_sbu_repaint(win);
            return;
        }
        sbu_dialog = SBU_DLG_NONE;
        if (os88_file_write_seg(name, sb_source_seg, sb_source_len) != 0) {
            os88_toast("Speedy: save failed.", 0);
            return;
        }
        os88_strcpy(sb_file_name, name, sizeof(sb_file_name));
        ovl_sbu_repaint(win);
        return;
    }

    if (size_hi || size_lo > SB_SOURCE_MAX) {
        os88_toast("Speedy: program too big.", 0);
        return;
    }
    sb_stop();
    if (ovl_sb_source_resize(size_lo + 1) < 0) {
        os88_toast("Speedy: program memory unavailable.", 0);
        return;
    }
    got = os88_file_read_seg(name, sb_source_seg, sb_source_kb << 10);
    if (got == 0 && os88_ferr() != OS88_FERR_OK) {
        os88_toast("Speedy: open failed.", 0);
        return;
    }
    sb_source_len = got;
    os88_poke(sb_source_seg, sb_source_len, 0);
    os88_strcpy(sb_file_name, name, sizeof(sb_file_name));
    ovl_sbs_release_graphics();
    sbu_cursor = 0;
    sbu_scroll = 0;
    sbu_view = SBU_VIEW_EDIT;
    if (sb_load_seg(sb_source_seg, sb_source_len) < 0) {
        sbu_view = SBU_VIEW_RUN;
        os88_toast("Speedy: load failed.", 0);
    }
    ovl_sbu_repaint(win);               /* dialog exposed an arbitrary region */
}

void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win)
{
    ovl_speedy_onfile(mode, name, size_lo, size_hi, win);
}

void os88_onwake(void *win)
{
    static int last_state = -1;
    static int paint_div;
    int state;

    if (sbu_build == SBU_BUILD_READY) {
        sb_stop();
        sbu_build = SBU_BUILD_RUNNING;
        sbu_build_error[0] = 0;
        os88_gfx_lock();
        ovl_sbu_repaint(win);
        os88_gfx_unlock();
        if (ovl_sbg_compile(sb_source_seg, sb_source_len,
                            sbu_package_name, sbu_build_error,
                            sizeof(sbu_build_error)) == 0) {
            sbu_build = SBU_BUILD_DONE;
            os88_toast("Speedy: package built.", 0);
        } else {
            sbu_build = SBU_BUILD_ERROR;
            os88_toast(sbu_build_error[0] ? sbu_build_error :
                       "Speedy: build failed.", 0);
        }
        os88_gfx_lock();
        ovl_sbu_repaint(win);
        os88_gfx_unlock();
    }

    state = sb_state();
    if (state == SB_STATE_READY || state == SB_STATE_RUNNING ||
        state == SB_STATE_WAITING)
        state = sb_run_slice(SB_SLICE_OPS);

    paint_div++;
    if ((sbs_dirty && (sbs_mode != SB_MODE_GRAPHICS || !(paint_div & 7))) ||
        state != last_state) {
        os88_gfx_lock();
        ovl_sbu_repaint(win);
        os88_gfx_unlock();
        last_state = state;
    }
    if (state == SB_STATE_RUNNING) {
        if (sbs_mode == SB_MODE_GRAPHICS) os88_wm_timer(win, 1);
        else os88_wm_wake(win);
    }
    else if (state == SB_STATE_WAITING)
        os88_wm_timer(win, 1);
}

void os88_ontimer(void *win)
{
    os88_wm_wake(win);
}
