/* A STUB apps/cc/os88.h for INFONES's host harness
 * (apps/infones/hosttest/niuitest.c). Same names, same shapes, no poisoning
 * of long/float - the host needs printf.
 *
 * ONLY WHAT infones.c AND ITS PARTS USE, and IT GROWS WITH EVERY API THE
 * PROGRAM TOUCHES, in the same edit as the call (LESSONS.md 7): an os88_*
 * added to the package without its declaration here fails to COMPILE, which
 * is the failure you want, three steps earlier than a link error.
 *
 * IT IS DIFFED AGAINST apps/cc/os88.h BY build.sh (SPEC.md 91.14.4). Every
 * prototype below must exist in the real header with the same signature, so
 * a stub that drifts - a wrong argument order, an int where an unsigned goes
 * - fails the build instead of quietly testing a different API than the one
 * the machine has.
 *
 * THE MODEL OF THE GLASS IS NOT HERE - it is in niuitest.c, which defines
 * every function below. This file is only the declarations, so that
 * infones.c compiles against the same shapes on the host and on the 8086. */
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
#define OS88_TITLE_H  18
#define OS88_BLACK     0
#define OS88_LGRAY     7
#define OS88_DGRAY     8
#define OS88_WHITE    15
#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_VID_EGA   3
#define OS88_CPU_8086  0
#define OS88_CPU_286   1
#define OS88_CPU_386   2
#define OS88_MENU_DIS  1
#define OS88_FDLG_OPEN 0
#define OS88_FDLG_SAVE 1
#define OS88_FERR_OK      0
#define OS88_FERR_NOENT   4

void *os88_main(void);
void os88_paint(void *win);
void os88_onkey(int ascii, int scan, void *win);
void os88_onclick(int x, int y, void *win);
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_onwake(void *win);
void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win);

void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int colour);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
void os88_font_run(int x, int y, const char *s, int ink, int paper);

void *os88_wm_create(int x, int y, int w, int h, const char *title);
void os88_wm_content(void *win, struct os88_pt *origin);
int  os88_wm_geom(void *win, struct os88_size *sz);
void os88_wm_snap(void *win, int on);
void os88_wm_minsize(void *win, int w, int h);
void os88_wm_ownbg(void *win, int on);
int  os88_wm_damage(void *win, struct os88_rect *r);
void os88_wm_close(void *win);
void os88_wm_onwake(void *win);
int  os88_wm_wake(void *win);
void os88_menu_set(void *win, struct os88_menuset *set);
void os88_about_set(void *win);

int  os88_toast(const char *text, int ticks);
int  os88_cpu(void);
void os88_video(struct os88_video *v);
unsigned os88_ticks(void);
int  os88_key_down(int scan);

unsigned os88_mem_claim(int kb);
int      os88_mem_free(unsigned seg);
unsigned os88_mem_largest_kb(void);
int  os88_peek(unsigned seg, unsigned off);
void os88_poke(unsigned seg, unsigned off, int value);

unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap);
int  os88_file_dlg(int mode, void *win, const char *defname);
int  os88_assoc_set(const char *ext, const char *stem);
int  os88_arg_file(char *name13, struct os88_place *p);
int  os88_file_goto(struct os88_place *p);

void os88_memset(void *p, int c, unsigned n);
void os88_memcpy(void *dst, const void *src, unsigned n);
unsigned os88_strlen(const char *s);
void os88_strcpy(char *dst, const char *src, unsigned cap);
char *os88_utoa(unsigned v, char *dst6);

#endif
