/* Byteopoly - the face: layouts, the board, the sidebar, the widget kit
 * (SPEC.md 131.8). Lane: face. Drawing goes through Breadboard XT's band
 * core (apps/breadxt/draw.inc, shared as source, SPEC.md 130.6.2): every
 * repaint is strips, each one band composed and put down with one call.
 * FOUNDATION SKELETON: a plain ring of 28 labelled cells, the centre and a
 * sidebar, enough to prove the bridge. The face lane replaces this file. */
#include "art.h"

int bx_rows(int x, int y, int w, int h);
void bx_new(int x, int y, int w, int h, int colour);
void bx_fill(int x1, int y1, int x2, int y2, int colour);
void bx_dither(int x1, int y1, int x2, int y2, int c1, int c2, int pat);
void bx_dots(int x1, int y1, int x2, int y2, int step, int colour);
unsigned bx_size(int id);
int bx_spr(int x, int y, int id);
int bx_artstr(int id, int n, char *buf);
int bx_text(int x, int y, const char *s, int colour, int font);
int bx_twidth(const char *s, int font);
int bx_fonth(int font);
int bx_out(void);
void bx_setup(unsigned seg, void *band, unsigned cap, unsigned artseg, int bpp);
extern unsigned bx_bseg;

BPRect bp_r_board, bp_r_center, bp_r_side, bp_r_hint;
BPByte bp_layout, bp_mono, bp_sel;
static BPRect bp_fc_all;
static unsigned bp_fc_dirty;
static char bp_fc_log[BP_LOG_LINES][BP_LOG_LEN + 1];
static struct os88_pt bp_fc_org;
static struct os88_size bp_fc_size;

int bp_face_init(void *win, unsigned artseg, unsigned bandseg, unsigned bandcap, int bpp)
{
    static BPByte fallback[2048];
    (void)win;
    bp_mono = bpp < 4;
    if (bandseg) bx_setup(bandseg, 0, bandcap, artseg, bpp >= 4 ? 4 : 1);
    else bx_setup(0, fallback, sizeof(fallback), artseg, bpp >= 4 ? 4 : 1);
    bp_fc_dirty = BP_D_ALL;
    return BP_OK;
}
static void bp_fc_cell(int space, BPRect *r)
{
    int cw, ch, i;
    cw = bp_r_board.w / 8; ch = bp_r_board.h / 8;
    if (space <= 7) { i = space; r->x = bp_r_board.x; r->y = bp_r_board.y + (7 - i) * ch; }
    else if (space <= 14) { i = space - 7; r->x = bp_r_board.x + i * cw; r->y = bp_r_board.y; }
    else if (space <= 21) { i = space - 14; r->x = bp_r_board.x + 7 * cw; r->y = bp_r_board.y + i * ch; }
    else { i = space - 21; r->x = bp_r_board.x + (7 - i) * cw; r->y = bp_r_board.y + 7 * ch; }
    r->w = cw; r->h = ch;
}
void bp_face_layout(int w, int h)
{
    int side;
    side = w / 3;
    bp_layout = h >= 400 ? BP_LAY_FULL : h >= 300 ? BP_LAY_MID : BP_LAY_COMPACT;
    bp_r_board.x = bp_fc_org.x; bp_r_board.y = bp_fc_org.y;
    bp_r_board.w = (w - side) & ~7; bp_r_board.h = h - 12;
    bp_r_center.x = bp_r_board.x + bp_r_board.w / 8; bp_r_center.y = bp_r_board.y + bp_r_board.h / 8;
    bp_r_center.w = bp_r_board.w * 6 / 8; bp_r_center.h = bp_r_board.h * 6 / 8;
    bp_r_side.x = bp_r_board.x + bp_r_board.w; bp_r_side.y = bp_r_board.y;
    bp_r_side.w = w - bp_r_board.w; bp_r_side.h = bp_r_board.h;
    bp_r_hint.x = bp_fc_org.x; bp_r_hint.y = bp_fc_org.y + h - 12; bp_r_hint.w = w; bp_r_hint.h = 12;
    bp_fc_all.x = bp_fc_org.x; bp_fc_all.y = bp_fc_org.y; bp_fc_all.w = w; bp_fc_all.h = h;
}
void bp_face_dirty(unsigned mask) { bp_fc_dirty |= mask; }
void bp_face_dirty_space(int space) { (void)space; bp_fc_dirty |= BP_D_BOARD; }
static void bp_fc_scene(void)
{
    static BPRect r;
    int s, i;
    const BPSpace *sp;
    bx_fill(bp_fc_all.x, bp_fc_all.y, bp_fc_all.x + bp_fc_all.w - 1, bp_fc_all.y + bp_fc_all.h - 1, 7);
    for (s = 0; s < BP_SPACES; s++) {
        bp_fc_cell(s, &r); sp = bp_space(s);
        bx_fill(r.x, r.y, r.x + r.w - 1, r.y + r.h - 1, 0);
        bx_fill(r.x + 1, r.y + 1, r.x + r.w - 2, r.y + r.h - 2, s == bp_sel ? 14 : 15);
        bx_text(r.x + 3, r.y + 3, sp->shortname, 0, -1);
    }
    bx_fill(bp_r_center.x, bp_r_center.y, bp_r_center.x + bp_r_center.w - 1, bp_r_center.y + bp_r_center.h - 1, 3);
    bx_text(bp_r_center.x + 8, bp_r_center.y + 8, "BYTEOPOLY", 15, -1);
    bp_pn_draw();
    bx_fill(bp_r_side.x + 4, bp_r_side.y, bp_r_side.x + bp_r_side.w - 1, bp_r_side.y + bp_r_side.h - 1, 15);
    for (i = 0; i < BP_LOG_LINES; i++)
        bx_text(bp_r_side.x + 8, bp_r_side.y + 8 + i * 10, bp_fc_log[i], 0, -1);
    bx_fill(bp_r_hint.x, bp_r_hint.y, bp_r_hint.x + bp_r_hint.w - 1, bp_r_hint.y + bp_r_hint.h - 1, 1);
    for (i = 0; bp_pn_hint(i); i++) bx_text(bp_r_hint.x + 8 + i * 120, bp_r_hint.y + 2, bp_pn_hint(i), 15, -1);
}
void bp_face_paint(void *win, int full)
{
    int x1, y1, x2, y2, w, rows, h;
    (void)full;
    os88_wm_content(win, &bp_fc_org);
    os88_wm_geom(win, &bp_fc_size);
    bp_face_layout(bp_fc_size.w, bp_fc_size.h);
    x1 = bp_fc_all.x & ~7; y1 = bp_fc_all.y;
    x2 = (bp_fc_all.x + bp_fc_all.w - 1) | 7; y2 = bp_fc_all.y + bp_fc_all.h - 1;
    w = x2 - x1 + 1;
    rows = bx_rows(x1, y1, w, y2 - y1 + 1);
    if (rows < 1) return;
    while (y1 <= y2) {
        h = y2 - y1 + 1; if (h > rows) h = rows;
        bx_new(x1, y1, w, h, -1); bp_fc_scene(); bx_out();
        y1 += h;
    }
    bp_fc_dirty = 0;
}
int bp_face_hit_space(int x, int y)
{
    static BPRect r;
    int s;
    for (s = 0; s < BP_SPACES; s++) {
        bp_fc_cell(s, &r);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return s;
    }
    return -1;
}
void bp_face_select(int space) { if (space >= 0 && space < BP_SPACES) { bp_sel = (BPByte)space; bp_fc_dirty |= BP_D_BOARD | BP_D_DETAILS; } }
int bp_face_key(int ascii, int scan) { (void)ascii; (void)scan; return 0; }
void bp_w_panel(const BPRect *r, const char *title)
{
    bx_fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, 0);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + r->h - 2, 7);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + 12, 1);
    bx_text(r->x + 4, r->y + 3, title, 15, -1);
}
void bp_w_box(const BPRect *r, int fill, int edge)
{
    bx_fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, edge);
    bx_fill(r->x + 1, r->y + 1, r->x + r->w - 2, r->y + r->h - 2, fill);
}
void bp_w_button(const BPRect *r, const char *label, int state)
{
    bp_w_box(r, state & BP_W_FOCUS ? 14 : 7, 0);
    bx_text(r->x + 4, r->y + 3, label, state & BP_W_DISABLED ? 8 : 0, -1);
}
int bp_w_text(int x, int y, const char *s, int colour, int font) { return bx_text(x, y, s, colour, font); }
int bp_w_textw(const char *s, int font) { return bx_twidth(s, font); }
int bp_w_fonth(int font) { return bx_fonth(font); }
int bp_w_sprite(int x, int y, int art_id) { return bx_spr(x, y, art_id); }
unsigned bp_w_spritesz(int art_id) { return bx_size(art_id); }
void bp_w_fill(int x1, int y1, int x2, int y2, int colour) { bx_fill(x1, y1, x2, y2, colour); }
void bp_log_add(const char *line)
{
    int i;
    for (i = BP_LOG_LINES - 1; i > 0; i--) os88_strcpy(bp_fc_log[i], bp_fc_log[i - 1], BP_LOG_LEN + 1);
    os88_strcpy(bp_fc_log[0], line, BP_LOG_LEN + 1);
    bp_fc_dirty |= BP_D_LOG;
}
const char *bp_log_line(int i) { if (i < 0 || i >= BP_LOG_LINES) return ""; return bp_fc_log[i]; }
