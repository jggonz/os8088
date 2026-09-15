/* Parser-free runtime used by host-compiled Speedy BASIC programs.
 * The generated translation unit defines sbp_program() and calls the
 * small sbr_* surface below; BASIC control flow and expressions are compiled
 * by SmallerC into ordinary 8086 instructions. */

#include "os88.h"
#include "runtime.h"

#define SB_MODE_TEXT 0
#define SB_MODE_GRAPHICS 1
#ifndef SB_PROGRAM_TITLE
#define SB_PROGRAM_TITLE "Compiled BASIC"
#endif

static void *sb_win;
static char sb_ui_text[81];
static int rt_row, rt_col, rt_fg, rt_bg;
static int rt_full;
static int rt_done;
static int rt_last_ascii;
static unsigned rt_defseg;
static int rt_reg_ax;

int sbm_init(void);

/* The editor keeps cold screen helpers in SPEEDYBA.OVL.  A compiled program
 * is one standalone .O88, so compile the shared screen source under resident
 * names in this translation unit. */
#define ovl_sbs_abs               sbr_sbs_abs
#define ovl_sbs_circle            sbr_sbs_circle
#define ovl_sbs_claim_graphics    sbr_sbs_claim_graphics
#define ovl_sbs_clear             sbr_sbs_clear
#define ovl_sbs_gfx_get           sbr_sbs_gfx_get
#define ovl_sbs_gfx_put           sbr_sbs_gfx_put
#define ovl_sbs_init              sbr_sbs_init
#define ovl_sbs_line              sbr_sbs_line
#define ovl_sbs_mode              sbr_sbs_mode
#define ovl_sbs_nearest           sbr_sbs_nearest
#define ovl_sbs_paint             sbr_sbs_paint
#define ovl_sbs_paint_surface     sbr_sbs_paint_surface
#define ovl_sbs_palette           sbr_sbs_palette
#define ovl_sbs_point             sbr_sbs_point
#define ovl_sbs_port_out          sbr_sbs_port_out
#define ovl_sbs_release_graphics  sbr_sbs_release_graphics
#define ovl_sbs_sound             sbr_sbs_sound
#define ovl_sbs_text_paint        sbr_sbs_text_paint
#define ovl_sbs_text_scroll       sbr_sbs_text_scroll
#define ovl_sbs_ticks             sbr_sbs_ticks
#define ovl_sbs_video_peek        sbr_sbs_video_peek
#define ovl_sbs_video_poke        sbr_sbs_video_poke

#define SB_PUT_PSET    0
#define SB_PUT_PRESET  1
#define SB_PUT_XOR     2
#define SB_PUT_OR      3
#define SB_PUT_AND     4

#include "sbscreen.c"

static void sbr_newline(void)
{
    rt_col = 1;
    if (++rt_row > 25) {
        sbs_host_text_scroll();
        rt_row = 25;
    }
    sbs_host_text_cursor(rt_row, rt_col);
}

static void sbr_putc(int ch)
{
    if (ch == '\n' || ch == '\r') {
        sbr_newline();
        return;
    }
    if (rt_col > 80) sbr_newline();
    sbs_host_text_put(rt_row, rt_col++, ch, rt_fg, rt_bg);
    sbs_host_text_cursor(rt_row, rt_col);
}

void sbr_print_str(const char *s)
{
    while (*s) sbr_putc((unsigned char)*s++);
}

static void sbr_print_int(int value)
{
    static char buf[8];
    int n, i;
    unsigned v;

    n = 0;
    if (value < 0) {
        sbr_putc('-');
        v = (unsigned)(-(value + 1)) + 1u;
    } else v = (unsigned)value;
    do {
        buf[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < (int)sizeof(buf));
    for (i = n - 1; i >= 0; --i) sbr_putc(buf[i]);
}

static void sbr_print_zone(void)
{
    int next, zones;
    zones = (rt_col / 14) + 1;
    next = 0;
    while (zones-- > 0) next += 14;
    while (rt_col < next && rt_col < 80) sbr_putc(' ');
}

void sbr_screen(int mode)
{
    int w, h;
    w = 320; h = 200;
    if (mode == 9) { w = 640; h = 350; }
    else if (mode == 13) { w = 320; h = 200; }
    else if (mode == 0) { w = 640; h = 400; }
    sbs_host_mode(mode, w, h);
    rt_row = rt_col = 1;
}

void sbr_cls(void)
{
    sbs_host_clear(rt_bg);
    rt_row = rt_col = 1;
    sbs_host_text_cursor(1, 1);
}

static void sbr_color(int fg, int bg)
{
    if (fg >= 0) rt_fg = fg & 15;
    if (bg >= 0) rt_bg = bg & 7;
}

static void sbr_locate(int row, int col)
{
    if (row > 0) rt_row = row;
    if (col > 0) rt_col = col;
    if (rt_row < 1) rt_row = 1;
    if (rt_row > 25) rt_row = 25;
    if (rt_col < 1) rt_col = 1;
    if (rt_col > 80) rt_col = 80;
    sbs_host_text_cursor(rt_row, rt_col);
}

void sbr_pset(int x, int y, int color)
{
    sbs_host_pixel(x, y, color);
}

void sbr_line(int x1, int y1, int x2, int y2, int color)
{
    sbs_host_line(x1, y1, x2, y2, color);
}

static void sbr_circle(int x, int y, int radius, int color)
{
    sbs_host_circle(x, y, radius, color);
}

static void sbr_paint(int x, int y, int color, int border)
{
    sbs_host_paint(x, y, color, border);
}

static int sbr_point(int x, int y)
{
    return sbs_host_point(x, y);
}

int sbr_inkey(void)
{
    int ch;
    ch = rt_last_ascii;
    rt_last_ascii = 0;
    return ch;
}

void sbr_reg(int reg, int value)
{
    if (reg == 1) rt_reg_ax = value;
}

void sbr_interrupt(int vector)
{
    int mode;
    if (vector != 0x10 || (rt_reg_ax & 0xff00)) return;
    mode = rt_reg_ax & 255;
    /* SCREEN uses decimal 13 for the emulated VGA surface; BIOS INT 10h
     * names the same mode with AL=13h.  Mode 03h restores 80x25 text. */
    if (mode == 0x13) sbr_screen(13);
    else if (mode == 3 || mode == 7) sbr_screen(0);
}

void sbr_out(int port, int value)
{
    sbs_host_port_out((unsigned)port, value);
}

void sbr_defseg(unsigned segment)
{
    rt_defseg = segment;
}

void sbr_poke(unsigned offset, int value)
{
    sbs_host_video_poke(rt_defseg, offset, value);
}

void sbr_delay(unsigned ticks)
{
    (void)ticks;
    /* The PC state machine returns to os88 after each bounded slice.  Its
     * one-tick window timer supplies the cooperative delay without spinning
     * inside a package callback. */
}

void *os88_main(void)
{
    static struct os88_video v;
    void *win;
    int h;

    ovl_sbs_init();
    rt_row = rt_col = 1;
    rt_fg = 15;
    rt_bg = 0;
    rt_done = 0;
    rt_last_ascii = 0;
    rt_defseg = 0;
    rt_reg_ax = 0;
    if (sbm_init() < 0) {
        os88_toast("Compiled BASIC: needs array memory.", 0);
        return 0;
    }
    os88_video(&v);
    h = v.dock_top - OS88_MBAR_H - 1;
    win = os88_wm_create(0, OS88_MBAR_H, v.w, h, SB_PROGRAM_TITLE);
    if (!win) return 0;
    sb_win = win;
    os88_wm_sizable(win, 1);
    os88_wm_snap(win, 1);
    os88_wm_ownbg(win, 1);
    os88_wm_minsize(win, 320, 160);
    os88_wm_onresize(win);
    os88_wm_onwake(win);
    os88_wm_ontimer(win);
    os88_key_down(0);
    os88_wm_wake(win);
    return win;
}

void sbr_print_num(void)
{
    /* Integer formatting is exact for the common 16-bit case.  The numeric
     * support module replaces this with full LONG/Q16.16 formatting as those
     * values are lowered by the generator. */
    sbr_print_int((int)sbn_alo);
}

void sbr_print_nl(void)
{
    sbr_newline();
}

void sbr_error(const char *message)
{
    rt_done = 1;
    os88_toast(message, 0);
}

void os88_paint(void *win)
{
    static struct os88_pt org;
    static struct os88_size size;
    if (os88_wm_geom(win, &size) < 0) return;
    os88_wm_content(win, &org);
    if (sbs_mode == SB_MODE_GRAPHICS)
        ovl_sbs_paint_surface(org.x, org.y, size.w, size.h);
    else {
        os88_set_color(OS88_BLACK);
        os88_gfx_fill(org.x, org.y, org.x + size.w - 1,
                      org.y + size.h - 1);
        ovl_sbs_text_paint(org.x + 8, org.y + 8);
        sbs_dirty = 0;
    }
    os88_wm_grow(win);
}

static void sbr_repaint(void *win)
{
    if (os88_wm_clip_set(win) < 0) return;
    os88_paint(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    rt_last_ascii = ascii;
    if (ascii == 6) {
        rt_full = !rt_full;
        if (os88_fullscreen(win, rt_full) < 0) rt_full = !rt_full;
        else sbs_dirty = 1;
    }
    if (!rt_done) os88_wm_wake(win);
}

void os88_onresize(int w, int h, void *win)
{
    (void)w; (void)h;
    sbs_dirty = 1;
    os88_wm_wake(win);
}

void os88_onwake(void *win)
{
    static int paint_div;
    if (!rt_done) rt_done = sbp_program(256);
    ++paint_div;
    if (sbs_dirty && (sbs_mode != SB_MODE_GRAPHICS || !(paint_div & 7))) {
        os88_gfx_lock();
        sbr_repaint(win);
        os88_gfx_unlock();
    }
    if (!rt_done) {
        if (sbs_mode == SB_MODE_GRAPHICS) os88_wm_timer(win, 1);
        else os88_wm_wake(win);
    }
}

void os88_ontimer(void *win)
{
    os88_wm_wake(win);
}
