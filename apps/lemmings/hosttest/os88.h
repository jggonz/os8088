/* ============================================================================
 * A STUB apps/cc/os88.h for the LEMMINGS host harness.
 *
 * Same names, same shapes, no poisoning of long/float - the host needs printf.
 * apps/lemmings/hosttest is put AHEAD of apps/cc on the include path, so this
 * is the os88.h that resolves when apps/lemmings/hosttest/lemtest.c #includes
 * the whole program.
 *
 * IT IS A SECOND COPY OF AN INTERFACE AND IT WILL DRIFT - and when it does the
 * harness fails to COMPILE, which is the failure you want (LESSONS.md 7). The
 * three times cword's harness failed to link it was a shim added with no stub
 * beside it, and the build told it three steps late.
 *
 * THE STUBS MODEL THE MACHINE, THEY DO NOT REFUSE. cword's stub gfx_blit1
 * refused, so the cost table priced the 71-call fallback for a while
 * (LESSONS.md 7). Here os88_font_run() writes a MODELLED GLASS - a character
 * and an attribute per cell, plus a per-repaint write counter that makes "no
 * pixel is written twice" a real assertion - and the file calls read the
 * COMMITTED SYNTHETIC FIXTURE, so the harness needs no network fetch and no
 * original data.
 *
 * WAVE 2 ADDS one named stub per assembly entry (lem_r_setup / mode / scroll /
 * sprite / present, lem_has_pixel / clear / set, lem_apply_mask, lem_dig_row)
 * and a byte-per-pixel frame model behind them, in the same edit as the .inc
 * files themselves.
 * ==========================================================================*/
#ifndef OS88_H
#define OS88_H

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };
struct os88_video { int w, h, dock_top, kind, bpp; };
struct os88_place { unsigned clus; int vol; };
struct os88_mouse { int x, y, btn; };

struct os88_find {
    char name[13];
    unsigned char attr;
    unsigned type;
    unsigned clus;
    unsigned size_lo, size_hi;
    unsigned rsvd;
};

#define OS88_MENU_MAX 5
struct os88_menu    { const char *title; const char **items; int nitems; };
struct os88_menuset { const char *name; int oncmd; int nmenus;
                      struct os88_menu menu[OS88_MENU_MAX]; };

struct os88_fsi {
    unsigned seg;
    unsigned w, h;
    unsigned stride;
    unsigned char flags;
    unsigned char bpp;
    unsigned char banks;
    unsigned char pages;
    unsigned bstep;
    unsigned char mode;
    unsigned char rsvd;
};

#define OS88_MBAR_H   20
#define OS88_TITLE_H  18
#define OS88_BLACK     0
#define OS88_BLUE      1
#define OS88_GREEN     2
#define OS88_CYAN      3
#define OS88_RED       4
#define OS88_MAGENTA   5
#define OS88_BROWN     6
#define OS88_LGRAY     7
#define OS88_DGRAY     8
#define OS88_WHITE    15

#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_VID_EGA   3

#define OS88_CUR_ARROW 0
#define OS88_CUR_CROSS 1

#define OS88_FT_FILE   0
#define OS88_FT_PKG    1
#define OS88_FT_DIR    2
#define OS88_FT_UP     3

#define OS88_FERR_OK      0
#define OS88_FERR_NOENT   4
#define OS88_FERR_WPROT   9

#define OS88_FSXM_TEXT80  0
#define OS88_FSXM_TEXT40  1
#define OS88_FSXM_CGA320  2
#define OS88_FSXM_CGA640  3
#define OS88_FSXM_HERC    4
#define OS88_FSXM_VGA0D   5
#define OS88_FSXM_VGA13   6
#define OS88_FSXM_VGA12   7
#define OS88_FSXM_MODEX   8

#define OS88_FSXW_TICK    0
#define OS88_FSXW_VSYNC   1
#define OS88_FSXW_FRAME   2

/* the callbacks the shim declares */
void *os88_main(void);
void os88_paint(void *win);
void os88_onkey(int ascii, int scan, void *win);
void os88_onclick(int x, int y, void *win);
void os88_onresize(int w, int h, void *win);
void os88_onwake(void *win);
void os88_ontimer(void *win);
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_fsx_main(void *win);

/* drawing */
void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int c);
void os88_gfx_hline(int x1, int x2, int y);
void os88_gfx_vline(int x, int y1, int y2);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
void os88_gfx_pen(int disabled);
void os88_font_run(int x, int y, const char *s, int ink, int paper);
int  os88_font_width(const char *s);

/* the window */
void *os88_wm_create(int x, int y, int w, int h, const char *title);
void os88_wm_content(void *win, struct os88_pt *o);
int  os88_wm_geom(void *win, struct os88_size *s);
void os88_wm_minsize(void *win, int w, int h);
void os88_wm_sizable(void *win, int on);
void os88_wm_snap(void *win, int on);
void os88_wm_onresize(void *win);
void os88_wm_onwake(void *win);
int  os88_wm_wake(void *win);
void os88_wm_ontimer(void *win);
int  os88_wm_timer(void *win, int ticks);
int  os88_wm_clip_set(void *win);
void os88_menu_set(void *win, struct os88_menuset *set);
void os88_about_set(void *win);
void os88_about_card(void *win, const char **lines);
void os88_about_card_d(void *win, const char **lines);

/* the machine */
void os88_video(struct os88_video *v);
int  os88_snd_caps(void);
int  os88_toast(const char *text, int ticks);
int  os88_fsx_caps(void *win, unsigned char *kind);
int  os88_fsx_run(void *win, int flags);
int  os88_fsx_mode(int id, struct os88_fsi *fsi);
int  os88_fsx_wait(int kind);
int  os88_fsx_key(int wait);

/* files */
unsigned os88_file_read(const char *name, void *buf, unsigned cap);
int os88_file_write(const char *name, const void *buf, unsigned count);
int os88_file_find(int ordinal, struct os88_find *f);
void os88_file_here(struct os88_place *p);
int  os88_file_goto(struct os88_place *p);
int  os88_file_goto_q_mark(unsigned clus, int vol);
int  os88_ferr(void);

/* the runtime's string helpers */
void os88_memset(void *p, int c, unsigned n);
void os88_memcpy(void *dst, const void *src, unsigned n);
unsigned os88_strlen(const char *s);
void os88_strcpy(char *dst, const char *src, unsigned cap);
char *os88_utoa(unsigned v, char *dst6);

#endif
