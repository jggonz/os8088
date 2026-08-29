/* A STUB apps/cc/os88.h for the host harness. Same names, same shapes, no
 * poisoning of long/float (the host needs printf). Only what cword.c uses. */
#ifndef OS88_H
#define OS88_H

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };
struct os88_video { int w, h, dock_top, kind, bpp; };
struct os88_place { unsigned clus; int vol; };
struct os88_mouse  { int x, y, btn; };

#define OS88_MENU_MAX 5
struct os88_menu    { const char *title; const char **items; int nitems; };
struct os88_menuset { const char *name; int oncmd; int nmenus;
                      struct os88_menu menu[OS88_MENU_MAX]; };

#define OS88_MBAR_H   20
#define OS88_BLACK     0
#define OS88_WHITE    15
#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_FDLG_OPEN 0
#define OS88_FDLG_SAVE 1
#define OS88_MENU_DIS  1
#define OS88_FERR_OK      0
#define OS88_FERR_NODISK  1
#define OS88_FERR_IO      2
#define OS88_FERR_NAME    3
#define OS88_FERR_NOENT   4
#define OS88_FERR_EXIST   5
#define OS88_FERR_FULL    6
#define OS88_FERR_DIRFULL 7
#define OS88_FERR_PROT    8
#define OS88_FERR_WPROT   9
#define OS88_FERR_BIG    10

void *os88_main(void);
void os88_paint(void *win);
void os88_onkey(int ascii, int scan, void *win);
void os88_onclick(int x, int y, void *win);
void os88_onmouseup(int x, int y, void *win);
void os88_worker(void *win);
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_onfile(int mode, const char *name, unsigned size_lo,
                 unsigned size_hi, void *win);

void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int c);
void os88_gfx_hline(int x1, int x2, int y);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
void os88_gfx_xor_fill(int x1, int y1, int x2, int y2);
int  os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy);
void os88_font_run(int x, int y, const char *s, int ink, int paper);
void os88_font_str_xparent(int x, int y, const char *s);
void os88_font_char_xparent(int x, int y, int ch);
void os88_gfx_vline(int x, int y1, int y2);
void os88_gfx_line(int x1, int y1, int x2, int y2, int dilate);
void os88_gfx_pixel(int x, int y);
void os88_gfx_pen(int disabled);
int  os88_gfx_blit1(const void *bits, int stride, int x, int y,
                    int w, int rows);

void *os88_wm_create(int x, int y, int w, int h, const char *title);
void os88_wm_content(void *win, struct os88_pt *o);
int  os88_wm_geom(void *win, struct os88_size *s);
int  os88_wm_obscured(void *win);
void os88_wm_title(void *win, const char *s);
void os88_wm_snap(void *win, int on);
void os88_wm_sizable(void *win, int on);
void os88_wm_destroy(void *win);
void os88_wm_hide(void *win);
void os88_wm_onmouseup(void *win);
void os88_menu_set(void *win, struct os88_menuset *set);
void os88_about_set(void *win);

void os88_video(struct os88_video *v);
void os88_mouse(struct os88_mouse *m);
void os88_task_yield(void);
void os88_task_sleep(int ticks);
int  os88_task_spawn(void *win);
void os88_task_alive(void *win);
int  os88_peek(unsigned seg, unsigned off);
unsigned os88_file_read(const char *name, void *buf, unsigned cap);
int os88_file_write(const char *name, const void *buf, unsigned count);
int os88_ferr(void);
int os88_file_dlg(int mode, void *win, const char *defname);
int os88_arg_file(char *name13, struct os88_place *p);
int os88_file_goto(struct os88_place *p);
int os88_assoc_set(const char *ext, const char *stem);
int os88_clip_put(const void *text, unsigned len);
int os88_clip_get(void *buf, unsigned cap);
int os88_clip_size(void);
int os88_toast(const char *text, int ticks);
void os88_strcpy(char *dst, const char *src, unsigned cap);
char *os88_utoa(unsigned v, char *dst6);
unsigned os88_strlen(const char *s);

#endif
