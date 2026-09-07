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
 * WAVE 2'S RASTER MODEL IS IN hosttest/lemraster.c, one named function per
 * assembly entry over a byte-per-pixel model of the mask, the world and the
 * screen - and a per-pixel WRITE COUNTER, which is what makes "no pixel is
 * written twice in a frame" an assertion rather than a hope. The claims are
 * modelled with it, because every raster entry takes a SEGMENT and lem_far()
 * does real paragraph arithmetic on one.
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

/* memory beyond the region, and the two far accessors (SPEC.md 50.3) */
unsigned os88_mem_claim(int kb);
int      os88_mem_free(unsigned seg);
unsigned os88_mem_largest_kb(void);
unsigned os88_mem_total_kb(void);
int      os88_peek(unsigned seg, unsigned off);
void     os88_poke(unsigned seg, unsigned off, int value);

/* input, and the fullscreen latch a bracket is stacked on (SPEC.md 11.2) */
void os88_mouse(struct os88_mouse *m);
int  os88_fullscreen(void *win, int enter);
int  os88_fsx_page(int page);
int  os88_fsx_surf(struct os88_rect *r);

/* THE RASTER (SPEC.md 92.4). These are the entries apps/lemmings/lemblit.inc,
 * lemmask.inc and lemfont.inc define in assembly; the harness defines the same
 * names over a byte-per-pixel model in hosttest/lemraster.c, so a shim added
 * without a stub beside it is a LINK error here rather than three steps later
 * (LESSONS.md 4). */
void lem_m_setup(unsigned maskseg);
void lem_m_clear(void);
int  lem_has_pixel(int x, int y);
void lem_set_pixel(int x, int y);
void lem_clear_pixel(int x, int y);
int  lem_probe4(int x, int y, const char *offs);
void lem_m_span(int x, int y, int w, int set);
void lem_m_apply(unsigned seg, unsigned off, int x, int y, int w, int h);
void lem_m_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags);
void lem_m_derive(unsigned dstseg, int kind);
void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
                 unsigned shadowseg);
void lem_r_prep(void);
void lem_r_unprep(void);
void lem_r_pal(unsigned seg, unsigned stdoff, unsigned cusoff);
void lem_r_clearworld(void);
void lem_r_piece(unsigned seg, unsigned off, int x, int y, int w, int h,
                 int flags);
void lem_r_derive(void);
void lem_r_scroll(int x);
void lem_r_dirty(int row, int b0, int b1);
void lem_r_undirty(void);
void lem_r_present(void);
void lem_r_panel(unsigned seg, unsigned off);
void lem_r_rect(int x, int y, int w, int h, int colour, int fill);
void lem_r_batch(int on);
int  lem_f_index(int ch);
void lem_f_cell(unsigned seg, unsigned off, int col, int row, int ch);
void lem_f_run(unsigned seg, unsigned off, int col, int row, const char *s,
               int n);

/* files */
unsigned os88_file_read(const char *name, void *buf, unsigned cap);
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap);
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
