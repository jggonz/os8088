/* A STUB apps/cc/os88.h for PACCMAN's host harness (apps/paccman/hosttest/
 * pmcuitest.c). Same names, same shapes, no poisoning of long/float (the host
 * needs printf). Only what paccman.c and its parts use, and IT GROWS WITH
 * EVERY API THE PROGRAM TOUCHES, in the same edit as the thunk
 * (LESSONS.md 7): a call added to the package without its declaration here
 * fails to COMPILE, and one added without its DEFINITION in pmcuitest.c fails
 * to link - which are both the failure you want.
 *
 * THE MODEL OF THE GLASS IS NOT HERE - it is in pmcuitest.c, which defines
 * every function below. This file is only the declarations, so that paccman.c
 * compiles against the same shapes on the host and on the 8086. */
#ifndef OS88_H
#define OS88_H

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };
struct os88_video { int w, h, dock_top, kind, bpp; };
struct os88_mouse { int x, y, btn; };

#define OS88_MENU_MAX 5
struct os88_menu    { const char *title; const char **items; int nitems; };
struct os88_menuset { const char *name; int oncmd; int nmenus;
                      struct os88_menu menu[OS88_MENU_MAX]; };

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
#define OS88_LBLUE     9
#define OS88_LGREEN   10
#define OS88_LCYAN    11
#define OS88_LRED     12
#define OS88_LMAGENTA 13
#define OS88_YELLOW   14
#define OS88_WHITE    15
#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_VID_EGA   3
#define OS88_MENU_DIS  1

/* SPEC.md 5.4.3.2 - plane_step's bit 15 asks instead of drawing. */
#define OS88_BLITP_PROBE 0x8000

void  os88_gfx_lock(void);
void  os88_gfx_unlock(void);
void  os88_set_color(int colour);
void  os88_gfx_fill(int x1, int y1, int x2, int y2);
int   os88_gfx_blit1(const void *bits, int stride, int x, int y,
                     int w, int rows);
void  os88_gfx_blit4(const void *pix, int stride, int x, int y, int w, int h);
int   os88_gfx_blitp(const void *planes, int plane_step, int stride,
                     int x, int y, int w, int rows);

void *os88_wm_create(int x, int y, int w, int h, const char *title);
void  os88_wm_content(void *win, struct os88_pt *origin);
int   os88_wm_geom(void *win, struct os88_size *sz);
int   os88_wm_obscured(void *win);
void  os88_wm_snap(void *win, int on);
void  os88_wm_keeph(void *win, int on);
void  os88_wm_ownbg(void *win, int on);
void  os88_wm_minsize(void *win, int w, int h);
void  os88_wm_display(void *win, struct os88_video *v);
int   os88_wm_damage(void *win, struct os88_rect *r);
int   os88_wm_clip_set(void *win);

void  os88_menu_set(void *win, struct os88_menuset *set);
void  os88_about_set(void *win);
void  os88_about_card(void *win, const char **lines);
void  os88_about_card_d(void *win, const char **lines);
int   os88_fullscreen(void *win, int enter);
int   os88_key_down(int scan);
void  os88_video(struct os88_video *v);
int   os88_toast(const char *text, int ticks);
int   os88_snd_tone(int hz, int ticks, int prio);

void *os88_wm_top(void);
int   os88_task_spawn(void *win);
void  os88_task_alive(void *win);
void  os88_task_sleep(int ticks);
void  os88_task_yield(void);
unsigned os88_ticks(void);

/* The callbacks the package DEFINES. Declared so the harness can call them
 * the way the kernel does. */
void *os88_main(void);
void  os88_paint(void *win);
void  os88_about(void *win);
void  os88_onkey(int ascii, int scan, void *win);
void  os88_oncmd(int item, int menu, void *win);
void  os88_worker(void *win);

#endif
