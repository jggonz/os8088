/* A STUB apps/cc/os88.h for APPLE2's host harness (apps/apple2/hosttest/
 * a2uitest.c). Same names, same shapes, no poisoning of long/float (the host
 * needs printf). Only what apple2.c and its parts use, and IT GROWS WITH
 * EVERY API THE PROGRAM TOUCHES, IN THE SAME EDIT AS THE THUNK
 * (LESSONS.md 7): a shim added to the package without its stub here fails to
 * LINK, which is the failure you want.
 *
 * It is a SECOND COPY OF AN INTERFACE and it will drift. When it does, the
 * harness fails to COMPILE, which is why the copy is acceptable at all
 * (docs/APPLE2-SPEC.md section 16.6).
 *
 * The MODEL OF THE GLASS is not here - it is in a2uitest.c, which defines
 * every function below. This file is only the declarations, so that apple2.c
 * compiles against the same shapes on the host and on the 8086. */
#ifndef OS88_H
#define OS88_H

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };
struct os88_video { int w, h, dock_top, kind, bpp; };
struct os88_place  { unsigned clus; int vol; };
struct os88_mouse  { int x, y, btn; };

#define OS88_MENU_MAX 5
struct os88_menu    { const char *title; const char **items; int nitems; };
struct os88_menuset { const char *name; int oncmd; int nmenus;
                      struct os88_menu menu[OS88_MENU_MAX]; };

#define OS88_MBAR_H   20
#define OS88_TITLE_H  18
#define OS88_BLACK     0
#define OS88_LGRAY     7
#define OS88_WHITE    15
#define OS88_VID_VGA   0
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2
#define OS88_CPU_8086  0
#define OS88_CPU_286   1
#define OS88_CPU_386   2
#define OS88_MENU_DIS  1
#define OS88_FDLG_OPEN 0
#define OS88_FDLG_SAVE 1
#define OS88_FERR_OK   0
#define OS88_FERR_BIG 10                    /* apps/cc/os88.h:346, and the
                                             * kernel's own value
                                             * (kernel/diskw.inc:68) */

void *os88_main(void);
void os88_paint(void *win);
void os88_onkey(int ascii, int scan, void *win);
void os88_onclick(int x, int y, void *win);
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_onwake(void *win);
void os88_ontimer(void *win);
void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win);

void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int c);
void os88_gfx_pen(int disabled);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
int  os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy);
int  os88_gfx_blit1(const void *bits, int stride, int x, int y, int w,
                    int rows);
void os88_font_run(int x, int y, const char *s, int ink, int paper);
void os88_font_str(int x, int y, const char *s);

void *os88_wm_create(int x, int y, int w, int h, const char *title);
void os88_wm_content(void *win, struct os88_pt *o);
int  os88_wm_geom(void *win, struct os88_size *s);
int  os88_wm_damage(void *win, struct os88_rect *r);
int  os88_wm_clip_set(void *win);
void os88_wm_snap(void *win, int on);
void os88_wm_ownbg(void *win, int on);
void os88_wm_onwake(void *win);
void os88_wm_ontimer(void *win);            /* SPEC.md 13.9 - install... */
int  os88_wm_timer(void *win, int ticks);   /* ...and arm it. 0 = armed,
                                            * -1 = REFUSED (kern_small
                                            * carries the slot and not the
                                            * body): TEST IT */
int  os88_wm_wake(void *win);
void os88_wm_destroy(void *win);
void os88_wm_close(void *win);              /* SPEC.md 75.2 */
int  os88_fullscreen(void *win, int enter);

/* THE EXCLUSIVE BRACKET (SPEC.md 53, APPLE2-SPEC section 13) - the four
 * thunks this port added to the C SDK, stubbed here IN THE SAME EDIT as the
 * thunk and the prototype, because a shim without a host stub is three link
 * failures away (LESSONS.md 7). a2uitest.c models them: os88_fsx_caps
 * answers a MASK the test chooses, os88_fsx_run CALLS the entry, and
 * os88_fsx_mode fills the block and switches the modelled glass over to a
 * foreign framebuffer of its own.
 *
 * THEY ARE BEHIND `%define CC_HAS_FSX` ON THE TARGET (apps/cc/os88.h's gate
 * list, apps/apple2/apple2.asm's own line) and unconditional here, which is
 * right for a harness and is stated so that the two files' difference is a
 * decision and not a drift: the host has no nasm and no image budget, and a
 * package that forgot the %define fails at the shim rather than here.
 *
 * AND THE EXIT CHORD IS Ctrl+F, not SPEC.md 11.2.1's bare `f` with Esc -
 * 11.2.1's own exemption for an app that takes typed text, which is this one:
 * the II+ owns every letter and owns Esc (APPLE2-SPEC section 13.3). */
int  os88_fsx_caps(void *win, int *kind);
int  os88_fsx_run(void (*entry)(void), void *win, int flags);
int  os88_fsx_mode(int id, void *fsi);
int  os88_fsx_wait(int kind);

#define OS88_FSXM_TEXT80 0
#define OS88_FSXM_TEXT40 1
#define OS88_FSXM_CGA320 2
#define OS88_FSXM_CGA640 3
#define OS88_FSXM_HERC   4
#define OS88_FSXM_VGA0D  5
#define OS88_FSXM_VGA13  6
#define OS88_FSXM_VGA12  7
#define OS88_FSXM_MODEX  8
#define OS88_FSXF_KEEPWORKER 1
#define OS88_FSXF_FASTTICK   2
#define OS88_FSXW_TICK  0
#define OS88_FSXW_VSYNC 1
#define OS88_FSXW_FRAME 2
#define OS88_FSI_SEG    0
#define OS88_FSI_W      2
#define OS88_FSI_H      4
#define OS88_FSI_STRIDE 6
#define OS88_FSI_FLAGS  8
#define OS88_FSI_BPP    9
#define OS88_FSI_BANKS  10
#define OS88_FSI_PAGES  11
#define OS88_FSI_BSTEP  12
#define OS88_FSI_MODE   14
#define OS88_FSI_SIZE   16
void os88_menu_set(void *win, struct os88_menuset *set);
void os88_about_set(void *win);

void os88_task_sleep(int ticks);
int  os88_toast(const char *text, int ticks);
int  os88_cpu(void);
void os88_video(struct os88_video *v);
unsigned os88_ticks(void);
/* os88_key_down - SPEC.md 9.7's key-state map. The first call arms it and
 * always answers 0 (APPLE2-SPEC section 6.4's rule: it is asked once, from
 * os88_main). Advice, not an oracle. The harness models both. */
int  os88_key_down(int scan);
int  os88_snd_caps(void);
int  os88_snd_tone(int hz, int ticks, int prio);

int  os88_clip_put(const void *text, unsigned len);
int  os88_clip_get(void *buf, unsigned cap);
int  os88_clip_size(void);
/* ...and the _seg forms, which are the ones Edit > Copy and Edit > Paste use:
 * the staging area is a transient heap CLAIM and never bss (section 6.5). */
int  os88_clip_put_seg(unsigned seg, unsigned off, unsigned len);
int  os88_clip_get_seg(unsigned seg, unsigned off, unsigned cap);

unsigned os88_mem_claim(int kb);
int      os88_mem_free(unsigned seg);
unsigned os88_mem_largest_kb(void);
int  os88_peek(unsigned seg, unsigned off);
void os88_poke(unsigned seg, unsigned off, int value);
unsigned os88_part_seg(int i);
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap);
int  os88_file_write_seg(const char *name, unsigned seg, unsigned count);
int  os88_ferr(void);                       /* apps/cc/os88.h:882 - the FERR_*
                                             * of the last file call, and the
                                             * ONLY thing that tells a 0-byte
                                             * read from a refusal */
int  os88_file_dlg(int mode, void *win, const char *defname);
int  os88_arg_file(char *name13, struct os88_place *p);   /* SPEC.md 54.5 */
int  os88_file_goto(struct os88_place *p);

void os88_memset(void *p, int c, unsigned n);
void os88_memcpy(void *dst, const void *src, unsigned n);
unsigned os88_strlen(const char *s);
void os88_strcpy(char *dst, const char *src, unsigned cap);
char *os88_utoa(unsigned v, char *dst6);

#endif
