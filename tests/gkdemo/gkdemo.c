/* gkdemo.c - the game kit's gallery (SPEC.md 132): every gk_ widget, the
 * six bevel styles, the drop shadows and one compiled scene, drawn through
 * the band core in strips, so the kit is LOOKED at on VGA and on a 1bpp
 * adapter. A gate package, not shipped: `make gkdemo`. Space scrolls the
 * gallery for a short content box (Hercules, CGA). */
#include "os88.h"
#include "gk.h"
#include "gkd.h"

int bx_rows(int x, int y, int w, int h);
void bx_new(int x, int y, int w, int h, int colour);
void bx_fill(int x1, int y1, int x2, int y2, int colour);
void bx_dither(int x1, int y1, int x2, int y2, int c1, int c2, int pat);
int bx_out(void);
void bx_setup(unsigned seg, void *band, unsigned cap, unsigned artseg, int bpp);
extern unsigned bx_bseg;

#define GD_BANDP 1024                   /* the band claim: 16 KB */

static struct os88_video gd_v;
static unsigned gd_art, gd_band;
static int gd_mono, gd_top;
static void *gd_win;
static struct os88_pt gd_org;
static struct os88_size gd_sz;
static char gd_fallback[2048];
static const char *gd_sv[3];
static int gd_iv[3];
static int gd_r[4];
static int gd_wh[2];
static char gd_key[2];

static struct os88_class gd_cls = {OS88_CLASS_LEN, OS88_WF_SIZABLE | OS88_WF_OWNBG,
    os88_t_paint, os88_t_onkey, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const char *gd_bn[6] = {"Normal", "Focus", "Default", "Disabled", "Pressed", "Hilite"};
static const char *gd_sn[6] = {"Flat", "Raise", "Raise2", "Sunk", "Sunk2", "Etch"};
static const char *gd_kn[3] = {"Tab", "Enter", "Esc"};
static const char gd_word[] = "GAMEKIT";

static void gd_inputs(int x, int y)
{
    y = gk_panel(x, y, 296, 156, 18, "INPUTS", GD_F_TEXT13, GK_RAISED | GK_SHADOW);
    gk_check(x + 10, y + 6, 14, 1, GD_A_TICK);
    gk_text(x + 30, y + 7, 90, "Art tick", 0, GD_F_TEXT10, GK_LEFT);
    gk_check(x + 10, y + 26, 14, 1, 0);
    gk_text(x + 30, y + 27, 90, "Drawn tick", 0, GD_F_TEXT10, GK_LEFT);
    gk_check(x + 10, y + 46, 14, 0, 0);
    gk_text(x + 30, y + 47, 90, "Off", 0, GD_F_TEXT10, GK_LEFT);
    gk_radio(x + 10, y + 68, 14, 1);
    gk_text(x + 30, y + 69, 90, "Classic", 0, GD_F_TEXT10, GK_LEFT);
    gk_radio(x + 10, y + 88, 14, 0);
    gk_text(x + 30, y + 89, 90, "Quick Match", 0, GD_F_TEXT10, GK_LEFT);
    gk_drop(x + 144, y + 4, 140, 22, "This PC", GD_F_TEXT13);
    gk_drop(x + 144, y + 32, 140, 22, "Other PC", GD_F_TEXT13);
    gk_hline(x + 144, y + 62, 140, 0);
    gk_box(x + 168, y + 70, 116, 30, GK_SUNKEN2, gk_role(GK_R_FIELD));
    gk_textbox(x + 172, y + 70, 106, 30, "1,280 cr", 0, GD_F_BIG16, GK_RIGHT);
    gk_sprshadow(x + 140, y + 76, GD_A_DISK, 3);
    gk_banner(x + 10, y + 110, 276, 18, "2-4 players - share either computer", GD_F_TEXT10, -1);
}

static void gd_buttons(int x, int y)
{
    int i;
    gk_panel(x, y, 296, 156, 0, 0, 0, GK_RAISED | GK_SHADOW);
    gk_titlebar(x + 3, y + 3, 290, 18, "BUTTONS AND KEYS", GD_F_TEXT13, GK_LEFT);
    for (i = 0; i < 6; i++)
        gk_button(x + 12 + (i & 1) * 140, y + 30 + (i >> 1) * 30, 128, 22, gd_bn[i], GD_F_TEXT13, i);
    for (i = 0; i < 3; i++)
        gk_keycap(x + 12 + i * 52, y + 124, 46, 22, gd_kn[i], GD_F_TEXT10);
    for (i = 0; i < 4; i++)
        gk_arrow(x + 180 + i * 26, y + 126, 18, 18, i, i == 1 ? 4 : 0);
}

static void gd_styles(int x, int y)
{
    int i;
    gk_panel(x, y, 296, 132, 0, 0, 0, GK_RAISED | GK_SHADOW);
    gk_tab(x + 6, y - 18, 92, 20, "Same PC", GD_F_TEXT13, 0);
    gk_tab(x + 100, y - 18, 96, 20, "Host Cable", GD_F_TEXT13, 1);
    gk_tab(x + 198, y - 18, 92, 20, "Join Cable", GD_F_TEXT13, 0);
    for (i = 0; i < 6; i++) {
        gk_box(x + 10 + i * 46, y + 12, 40, 26, i, gk_role(GK_R_FACE));
        gk_text(x + 8 + i * 46, y + 42, 44, gd_sn[i], 0, GD_F_TEXT10, GK_CENTRE | GK_FIT);
    }
    gk_box(x + 10, y + 60, 40, 26, GK_RAISED | GK_ROUND | GK_SHADOW, gk_role(GK_R_FACE));
    gk_shadow(x + 64, y + 60, 40, 26, 3);
    gk_box(x + 64, y + 60, 40, 26, GK_FLAT, 15);
    gk_vline(x + 116, y + 58, 32, 0);
    gk_titlebar(x + 128, y + 60, 156, 26, "TITLE", GD_F_BIG16, GK_CENTRE);
    for (i = 0; i < 7; i++) {
        gd_key[0] = gd_word[i];
        gk_keycap(x + 10 + i * 30 + (i > 3 ? 12 : 0), y + 96, 28, 30, gd_key, GD_F_BIG16);
    }
}

static void gd_scene(int x, int y)
{
    gd_sv[0] = "LANDING";
    gd_sv[1] = "Breadboard XT";
    gd_sv[2] = "220";
    gd_iv[0] = GK_B_FOCUS;
    gd_iv[1] = GK_B_NORMAL;
    gd_iv[2] = 1;
    if (!gk_scene(GD_S_LANDING, x, y, gd_sv, gd_iv)) {
        gk_text(x, y, 280, "no scene record", 15, GD_F_TEXT13, GK_LEFT);
        return;
    }
    /* the hit rects and size come from the same record */
    if (gk_scene_rect(GD_S_LANDING, 1, gd_r) && gd_r[0] == 64 && gd_r[1] == 88 &&
        gd_r[2] == 96 && gd_r[3] == 22 && gk_scene_size(GD_S_LANDING, gd_wh) &&
        gd_wh[0] == 288 && gd_wh[1] == 128 && !gk_scene_rect(GD_S_LANDING, 3, gd_r))
        gk_banner(x + 120, y + 130, 160, 16, "rects + size OK", GD_F_TEXT10, gk_role(GK_R_FIELD));
    else
        gk_banner(x + 120, y + 130, 160, 16, "rects WRONG", GD_F_TEXT10, 12);
}

static void gd_gallery(int x, int y)
{
    gd_inputs(x + 8, y + 8);
    gd_buttons(x + 320, y + 8);
    gd_styles(x + 8, y + 196);
    gd_scene(x + 320, y + 176);
}

static void gd_paint(void)
{
    int x1, w, y1, y2, rows, n;
    os88_wm_content(gd_win, &gd_org);
    if (os88_wm_geom(gd_win, &gd_sz) != 0) return;
    x1 = gd_org.x & ~7;
    w = (gd_org.x + gd_sz.w - x1) & ~7;
    y1 = gd_org.y;
    y2 = y1 + gd_sz.h - 1;
    rows = bx_rows(x1, y1, w, gd_sz.h);
    if (rows < 1 || w <= 0) return;
    while (y1 <= y2) {
        n = y2 - y1 + 1;
        if (n > rows) n = rows;
        bx_new(x1, y1, w, n, -1);
        if (gd_mono) bx_fill(x1, y1, x1 + w - 1, y1 + n - 1, 7);
        else bx_dither(x1, y1, x1 + w - 1, y1 + n - 1, 3, 8, 2);
        gd_gallery(gd_org.x, gd_org.y - gd_top);
        bx_out();
        y1 += n;
    }
}

void os88_paint(void *win) { (void)win; gd_paint(); }

void os88_onkey(int ascii, int scan, void *win)
{
    (void)scan;
    if (ascii != ' ') return;
    gd_top = gd_top ? 0 : 160;
    if (os88_wm_clip_set(win) == 0) gd_paint();
}

void os88_onmove(unsigned was, unsigned now)
{
    if (gd_band && gd_band == was) { gd_band = now; bx_bseg = now; }
}

void *os88_main(void)
{
    os88_video(&gd_v);
    gd_mono = gd_v.bpp < 4;
    gd_art = os88_part_seg(0);
    gd_band = os88_mem_claim_p(GD_BANDP);
    if (gd_band) {
        os88_mem_movable(gd_band, 1);
        bx_setup(gd_band, 0, GD_BANDP * 16, gd_art, gd_mono ? 1 : 4);
    } else {
        bx_setup(0, gd_fallback, sizeof(gd_fallback), gd_art, gd_mono ? 1 : 4);
    }
    gk_init(gd_v.bpp);
    gd_win = os88_wm_create(0, OS88_MBAR_H, gd_v.w, gd_v.dock_top - OS88_MBAR_H, "Game Kit", &gd_cls);
    return gd_win;
}
