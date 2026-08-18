/* A STUB apps/cc/os88.h for RUNCPM's host harness (apps/runcpm/hosttest/
 * rcuitest.c). Same names, same shapes, no poisoning of long/float (the host
 * needs printf). Only what runcpm.c and its parts use, and it grows with
 * every API the program touches (LESSONS.md 7): a shim added to the package
 * without its stub here fails to LINK, which is the failure you want. */
#ifndef OS88_H
#define OS88_H

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };
struct os88_video { int w, h, dock_top, kind, bpp; };
struct os88_place { unsigned clus; int vol; };
struct os88_mouse  { int x, y, btn; };
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

#define OS88_MBAR_H   20
#define OS88_TITLE_H  18
#define OS88_BLACK     0
#define OS88_WHITE    15
#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_CPU_8086  0
#define OS88_CPU_286   1
#define OS88_CPU_386   2
#define OS88_MENU_DIS  1
#define OS88_FT_FILE   0
#define OS88_FT_PKG    1
#define OS88_FT_DIR    2
#define OS88_FT_UP     3
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
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_onwake(void *win);
void os88_onclick(int x, int y, void *win);
void os88_worker(void *win);

void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int c);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
int  os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy);
int  os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows);
void os88_font_run(int x, int y, const char *s, int ink, int paper);

void *os88_wm_create(int x, int y, int w, int h, const char *title);
void os88_wm_content(void *win, struct os88_pt *o);
int  os88_wm_geom(void *win, struct os88_size *s);
int  os88_wm_damage(void *win, struct os88_rect *r);
int  os88_wm_clip_set(void *win);
void os88_wm_snap(void *win, int on);
void os88_wm_ownbg(void *win, int on);
void os88_wm_onwake(void *win);
int  os88_wm_wake(void *win);
int  os88_fullscreen(void *win, int enter);
void os88_menu_set(void *win, struct os88_menuset *set);
void os88_about_set(void *win);

void os88_wm_destroy(void *win);
int  os88_task_spawn(void *win);
void os88_task_alive(void *win);
void os88_task_sleep(int ticks);
int  os88_snd_tone(int hz, int ticks, int prio);
int  os88_toast(const char *text, int ticks);
int  os88_cpu(void);

unsigned os88_mem_claim(int kb);
int      os88_mem_free(unsigned seg);
unsigned os88_mem_regrow(unsigned seg, int kb);
unsigned os88_mem_largest_kb(void);
int  os88_peek(unsigned seg, unsigned off);
void os88_poke(unsigned seg, unsigned off, int value);
unsigned os88_file_read(const char *name, void *buf, unsigned cap);
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap);
int  os88_file_write_seg(const char *name, unsigned seg, unsigned count);
int  os88_file_delete(const char *name);
int  os88_file_rename(const char *oldname, const char *newname);
int  os88_file_mkdir(const char *name);
unsigned os88_ticks(void);
int  os88_ferr(void);
int  os88_file_find(int ordinal, struct os88_find *f);
void os88_file_here(struct os88_place *p);
int  os88_file_goto_q_mark(unsigned clus, int vol);

void os88_memset(void *p, int c, unsigned n);
void os88_memcpy(void *dst, const void *src, unsigned n);
unsigned os88_strlen(const char *s);
void os88_strcpy(char *dst, const char *src, unsigned cap);
char *os88_utoa(unsigned v, char *dst6);

#endif
