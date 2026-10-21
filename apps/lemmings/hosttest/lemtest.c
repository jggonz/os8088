/* ============================================================================
 * os8088 - apps/lemmings/hosttest/lemtest.c
 *
 * THE HOST HARNESS. It #includes the whole of apps/lemmings/lemmings.c against
 * the stub os88.h beside it, drives the launcher like a user, and rebuilds what
 * the screen ought to show from an INDEPENDENTLY WRITTEN layout after every
 * step. Three of cword's real defects came out of a harness this shape and no
 * screendump would have shown any of them (LESSONS.md 7).
 *
 * It runs against the COMMITTED SYNTHETIC FIXTURE in
 * apps/lemmings/hosttest/fixture/ - two levels, one style, the real string band
 * - so it needs no network fetch and no original data, which is what makes
 * `make lemmings` buildable on a tree that has never run tools/getlemmings.py
 * (SPEC.md 116.12).
 *
 * WHAT IT CHECKS, and each one stops the build:
 *
 *   1  THE BAND READS. The manifest, the string band and rating 0, off the
 *      fixture, with the ids the generated build/lemstr.h names.
 *   2  THE LIST PAGES FROM THE LIVE WINDOW at three modelled content heights -
 *      137 (a CGA: 200 - MBAR_H 20 - DOCK_H 24 - TITLE_H 18 - 1), 189 (the
 *      authored window on a VGA) and 100 (smaller than either) - and the row
 *      count, the page and the level on each row are rebuilt here from the
 *      geometry rather than read out of the program.
 *   3  NO ROW FALLS PAST THE BOX. Every cell the program draws ends inside the
 *      content height it was given. This is the CGA defect LESSONS.md 8 names,
 *      asserted rather than looked at.
 *   4  THE SHADOW DESCRIBES THE GLASS after every step. A shadow that starts
 *      lying is what leaves a stale cell on a real screen, and the audit is
 *      what names the step it started at.
 *   5  NOTHING IS DRAWN TWICE IN ONE REPAINT. PERFORMANCE.md's double-draw
 *      flash is invisible in an emulator; a per-cell write counter is how it is
 *      seen at all.
 *   6  EVERY CHARACTER IS IN THE KERNEL FACE, 32..126. os88_font_char(0xFB) for
 *      the IBM check mark drew NOTHING in cword and no checkable item had ever
 *      shown a check (LESSONS.md 8).
 *   7  A GREYED ROW NAMES ITS FACT, with the file name and the cluster
 *      arithmetic substituted out of LEMMAN.LEM's cost table - the fixture's
 *      second level is greyed with LEMS_MISS_SPEC exactly so this path is
 *      exercised rather than skipped.
 *   8  THE COST TABLE is printed on every build: a full repaint and a selection
 *      move, in drawing calls and glyph cells, at all three heights.
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>                   /* the oversize-band case makes a dir */

/* the stub, ahead of apps/cc on the include path - the same header the program
 * below is compiled against, so a stub that drifts from the real one is a
 * COMPILE error here rather than a wrong answer (LESSONS.md 7) */
#include "os88.h"

/* --- the modelled glass -----------------------------------------------------
 * A character, an attribute and a WRITE COUNT per cell, plus the pixel extent
 * of every run so that "no row fell past the box" is a fact rather than a
 * reading of the layout code. */
#define G_COLS 100
#define G_ROWS  40

static char          g_ch[G_ROWS][G_COLS];
static unsigned char g_at[G_ROWS][G_COLS];
static int           g_n[G_ROWS][G_COLS];
static int           g_fills;   /* os88_gfx_fill calls - see the stub */
static int           g_maxy;            /* the lowest pixel any run touched */
static int           g_calls, g_cells;
static int           g_pen;             /* the disabled pen is armed */
static int           g_bad;

static int  h_org_x = 8, h_org_y = 40;
static int  h_w = 512, h_h = 189;

static int  h_toasts;
static char h_lasttoast[256];

static void fail(const char *what)
{
    printf("lemtest: FAIL - %s\n", what);
    g_bad++;
}

static void glass_clear(void)
{
    int r, c;

    for (r = 0; r < G_ROWS; r++)
        for (c = 0; c < G_COLS; c++) {
            g_ch[r][c] = ' ';
            g_at[r][c] = 0;
            g_n[r][c] = 0;
        }
    g_maxy = -1;
    g_calls = 0;
    g_cells = 0;
    g_fills = 0;
}

static void counters_reset(void)
{
    int r, c;

    for (r = 0; r < G_ROWS; r++)
        for (c = 0; c < G_COLS; c++)
            g_n[r][c] = 0;
    g_calls = 0;
    g_cells = 0;
    g_maxy = -1;
    g_fills = 0;
}

/* THE RASTER MODEL, ahead of every stub that uses its claims and ahead of the
 * program that calls it (LESSONS.md 7: a stub that refuses measures the
 * fallback path, so every one of these DOES what the assembly does, on a
 * byte-per-pixel model, and counts it). */
#include "lemraster.c"

/* panel_ink - is there any lit pixel in the status line's cells [c0, c0+n)?
 * The status line is the panel's top 16 rows and a cell is 8 pixels wide, so
 * this reads the modelled screen exactly where lem_f_run() writes. */
static int panel_ink(int c0, int n)
{
    int x, y;

    for (y = H_VIEW_H; y < H_VIEW_H + 16; y++)
        for (x = c0 * 8; x < (c0 + n) * 8; x++)
            if (h_screen[y][x])
                return 1;
    return 0;
}


/* ============================================================================
 * THE STUBS
 * ==========================================================================*/

void os88_gfx_lock(void) {}
void os88_gfx_unlock(void) {}
void os88_set_color(int c) { (void)c; }
/* THE FILL IS MODELLED, and it is the only primitive here that is.
 *
 * lem_abdismiss() white-fills the content box before it repaints, because
 * invalidating the shadow alone re-letters every CELL and a cell is 8 pixels of
 * a 9-pixel pitch: the About card's frame and drop shadow cross the LEADING row
 * between every pair of text rows, and no font_run of ours ever writes those
 * pixels. A stub that dropped the fill would let the shadow/glass audit pass on
 * a dismissal that redrew NOTHING, because the modelled glass would still hold
 * the list the card was drawn over - which is the same blind spot that let the
 * defect ship. So the fill blanks the modelled glass, and the audit then means
 * "every cell that carries something was drawn back". It counts as one drawing
 * call, which is what it costs (756 us). */
void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int r, c;

    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    for (r = 0; r < G_ROWS; r++)
        for (c = 0; c < G_COLS; c++) {
            g_ch[r][c] = ' ';
            g_at[r][c] = 0;
        }
    g_calls++;
    g_fills++;
}
void os88_gfx_frame(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }

void os88_gfx_hline(int x1, int x2, int y)
{
    (void)x1;
    (void)x2;
    if (y > g_maxy)
        g_maxy = y;
    g_calls++;
}

void os88_gfx_vline(int x, int y1, int y2)
{
    (void)x;
    (void)y1;
    if (y2 > g_maxy)
        g_maxy = y2;
    g_calls++;
}

void os88_gfx_pen(int disabled) { g_pen = disabled; }

/* os88_font_run - the one that MODELS rather than refuses. It puts the paper
 * and the glyph down in one pass, which is the whole reason the program calls
 * nothing else, and here it records the cell, the attribute, the write count
 * and the lowest pixel the 8-tall glyph band reached. */
void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int col, row, i, at;

    col = (x - h_org_x - 2) / 8;
    row = (y - h_org_y - 2) / 9;
    if (row < 0 || row >= G_ROWS || col < 0) {
        fail("font_run outside the modelled glass");
        return;
    }
    at = 0;                             /* LEM_AT_TEXT, and the background */
    if (ink == OS88_WHITE && paper == OS88_BLACK)
        at = 1;                         /* LEM_AT_SEL */
    /* A PAPER THIS PROGRAM MAY NOT USE. OS88_LGRAY rounds to BLACK on CGA and
     * Hercules and so does black ink, so a run drawn on it is not on the glass
     * at all there - which the tab strip did, and a 1bpp screendump is what
     * found it (SPEC.md 39.4). */
    if (paper != OS88_BLACK && paper != OS88_WHITE)
        fail("a run was drawn on a paper that rounds to black on 1bpp");
    /* A GREYED RUN IS THE PEN **AND** THE INK, and the harness asserts both:
     * the pen alone left the greyed rows drawn in OS88_BLACK and pixel-
     * identical to a live one on VGA (SPEC.md 47, 6.6.5 - a run takes its ink
     * as an argument and C cannot read os88_set_color back). */
    if (g_pen || ink == OS88_DGRAY) {
        at = 2;                         /* LEM_AT_GREY */
        if (!g_pen)
            fail("a DGRAY run was drawn with no disabled pen armed - on 1bpp "
                 "the colour rounds to black and the FLAG is the whole of it");
        if (ink != OS88_DGRAY)
            fail("the disabled pen was armed but the run's ink is not DGRAY - "
                 "on VGA the flag alone changes nothing");
    }
    g_calls++;
    for (i = 0; s[i]; i++) {
        if (col + i >= G_COLS) {
            fail("font_run ran past the modelled glass");
            return;
        }
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126)
            fail("a character outside the kernel face 32..126 was drawn");
        g_ch[row][col + i] = s[i];
        g_at[row][col + i] = (unsigned char)at;
        g_n[row][col + i]++;
        g_cells++;
    }
    if (y + 7 > g_maxy)
        g_maxy = y + 7;
}

int os88_font_width(const char *s) { return (int)strlen(s) * 8; }

/* --- the window ------------------------------------------------------------ */
static int h_win_tag = 0x4C454D;

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    (void)x; (void)y; (void)w; (void)h; (void)title;
    return &h_win_tag;
}
void os88_wm_content(void *win, struct os88_pt *o)
{
    (void)win;
    o->x = h_org_x;
    o->y = h_org_y;
}
int os88_wm_geom(void *win, struct os88_size *s)
{
    (void)win;
    s->w = h_w;
    s->h = h_h;
    return 0;
}
void os88_wm_minsize(void *w, int a, int b) { (void)w; (void)a; (void)b; }
void os88_wm_sizable(void *w, int on) { (void)w; (void)on; }
void os88_wm_snap(void *w, int on) { (void)w; (void)on; }
void os88_wm_onresize(void *w) { (void)w; }
void os88_wm_onwake(void *w) { (void)w; }

/* THE KICK IS COUNTED, because two of this program's errands are DEFERRED to
 * os88_onwake() - the rating file and the progress write - and a deferral that
 * posts no wake is an errand owed to nobody. */
static int h_wakes;
int  os88_wm_wake(void *w) { (void)w; h_wakes++; return 0; }

/* --- the settle timer, and the clip region ---------------------------------
 * h_timer_ok models the two answers os88_wm_timer() can give: 0 armed, and -1
 * REFUSED on a kern_small machine, which the program must have a second path
 * for (SPEC.md 13.8.2). h_timer counts the ticks it was armed for so that "the
 * pane was DEFERRED and not merely slow" is a fact rather than a reading.
 *
 * os88_wm_clip_set() answers 0 here, and h_clips counts the arms: the kernel
 * arms a region for W_PAINT and for NOTHING ELSE, so every repaint reached from
 * a click, a key or the timer has to arm its own or it letters over whatever is
 * covering the window (SPEC.md 11.3). */
static int h_timer_ok = 1;
static int h_timer;                     /* ticks armed, 0 = disarmed */
static int h_timer_arms;
static int h_clips;

void os88_wm_ontimer(void *w) { (void)w; }

int os88_wm_timer(void *w, int ticks)
{
    (void)w;
    if (!h_timer_ok)
        return -1;
    h_timer = ticks;
    h_timer_arms++;
    return 0;
}

int os88_wm_clip_set(void *w)
{
    (void)w;
    h_clips++;
    return 0;
}
void os88_menu_set(void *w, struct os88_menuset *s) { (void)w; (void)s; }
void os88_about_set(void *w) { (void)w; }

static const char **h_about;
void os88_about_card(void *w, const char **lines) { (void)w; h_about = lines; }
void os88_about_card_d(void *w, const char **lines) { (void)w; h_about = lines; }

/* --- the machine ----------------------------------------------------------- */
static int h_vidkind = OS88_VID_VGA;
static int h_caps    = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_VGA12);

void os88_video(struct os88_video *v)
{
    v->w = 640; v->h = 480; v->dock_top = 456; v->kind = h_vidkind; v->bpp = 4;
}
int os88_snd_caps(void) { return 1; }

/* the speaker, the key map and the clock the bracket's loop reads */
static int h_tones;
int os88_snd_tone(int hz, int ticks, int prio)
{
    (void)hz;
    (void)ticks;
    (void)prio;
    h_tones++;
    return 0;
}
static unsigned char h_keydown[256];
int os88_key_down(int scan) { return h_keydown[scan & 255]; }

int os88_toast(const char *text, int ticks)
{
    (void)ticks;
    h_toasts++;
    strncpy(h_lasttoast, text, sizeof(h_lasttoast) - 1);
    h_lasttoast[sizeof(h_lasttoast) - 1] = 0;
    return 0;
}

int os88_fsx_caps(void *win, int *kind)
{
    (void)win;
    *kind = h_vidkind;
    return h_caps;
}
/* THE BRACKET IS DRIVEN AND NOT REFUSED. A stub that answers -1 measures the
 * refusal path, which is LESSONS.md 7's first harness trap: os88_fsx_run()
 * CALLS the entry it is handed the way the kernel does, os88_fsx_mode() answers a
 * modelled FSI, and os88_fsx_key() replays a scripted key list so the loop
 * runs a bounded number of ticks and leaves. h_fsx_refuse turns the refusal
 * back on for the one row that is about it. */
static int h_fsx_refuse;
static int h_fsx_moderefuse;        /* the BRACKET runs and the mode refuses */
static int h_fsx_runs;
static int h_fsx_ticks;
static int h_fsx_maxticks = 4;
static int h_fsx_keys[8];
static int h_fsx_keyn;
static int h_fsx_keyi;
static int h_fullscreen;
static int lem_onscr;
static int lem_started;
static int h_pv_keys[4] = { 0x1C0D, 0, 0, 0 };  /* Enter: play */
static int h_po_keys[4];                        /* none: Esc, the menu */
static int h_pv_ki, h_po_ki;
static int h_pv_asked, h_po_asked;              /* the screens that ran */
static int h_scr_up;            /* the screen has seen the buffer EMPTY: its
                                 * script keys are typed only from then on -
                                 * a BIOS buffer, where what the last screen
                                 * (or play) left is read first */
static int h_pv_hold;           /* asks the preview gets NOTHING for first -
                                 * the picture asks every eighth row, and a
                                 * key there cuts it short, as it should */

/* THE KERNEL'S OWN PAINTS ON THE WAY OUT ARE MODELLED (wave 5 review): the
 * fsx exit's wm_paint_all inside os88_fsx_run() and os88_fullscreen(win, 0)'s
 * both deliver W_PAINT, over the WF_OWNBG white fill, which is the kernel's
 * and not the program's - so the fill is not counted. A stub that skipped them
 * let the program's own THIRD repaint after the bracket look like the only
 * one. h_kp_calls / h_kp_fills are the counters just after the last kernel
 * paint: anything past them was issued by the program. */
static int h_kpaints, h_kp_calls, h_kp_fills;
/* ...AND WHAT THE PROGRAM DREW INSIDE EACH ONE (wave 5's second review): the
 * counters above move past a kernel paint, so a program that lettered a
 * whole screen INSIDE os88_paint() was never charged for it - and the two
 * paints that land while the window is fullscreen around the bracket were
 * each a full LIST at the fullscreen width (~28 calls, 726 cells). Indexed by
 * h_kpaints, a ring of 16. */
static int h_kp_pcalls[16], h_kp_pcells[16];

static void h_kernel_paint(void *w)
{
    int r, c, c0, n0;

    for (r = 0; r < G_ROWS; r++)
        for (c = 0; c < G_COLS; c++) {
            g_ch[r][c] = ' ';
            g_at[r][c] = 0;
        }
    c0 = g_calls;
    n0 = g_cells;
    os88_paint(w);
    h_kp_pcalls[h_kpaints & 15] = g_calls - c0;
    h_kp_pcells[h_kpaints & 15] = g_cells - n0;
    h_kpaints++;
    h_kp_calls = g_calls;
    h_kp_fills = g_fills;
}

int os88_fsx_run(void (*entry)(void), void *w, int f)
{
    (void)f;
    if (h_fsx_refuse)
        return -1;
    h_fsx_runs++;
    h_fsx_ticks = 0;
    h_fsx_keyi = 0;
    h_pv_ki = 0;
    h_po_ki = 0;
    h_scr_up = 0;
    entry();
    h_kernel_paint(w);              /* SPEC.md 53.6: the restore's paint */
    return 0;
}

static void h_fsi_w(unsigned char *f, int o, unsigned v)
{
    f[o] = (unsigned char)(v & 0xFF);
    f[o + 1] = (unsigned char)(v >> 8);
}

static int h_modesets;                  /* modes SET (each one a page wipe) */

int os88_fsx_mode(int id, void *fsi)
{
    unsigned char *f = (unsigned char *)fsi;

    if (h_fsx_refuse || h_fsx_moderefuse || (h_caps & (1 << id)) == 0)
        return -1;
    h_modesets++;
    memset(f, 0, OS88_FSI_SIZE);
    h_fsi_w(f, OS88_FSI_SEG, 0xA000);
    h_fsi_w(f, OS88_FSI_W, (id == OS88_FSXM_HERC) ? 720 : 320);
    h_fsi_w(f, OS88_FSI_H, (id == OS88_FSXM_HERC) ? 348 : 200);
    h_fsi_w(f, OS88_FSI_STRIDE, 40);
    f[OS88_FSI_FLAGS] = 2;
    f[OS88_FSI_BPP] = 4;
    f[OS88_FSI_BANKS] = 1;
    f[OS88_FSI_PAGES] = 1;
    f[OS88_FSI_MODE] = (unsigned char)id;
    return 0;
}

int os88_fsx_wait(int k)
{
    (void)k;
    h_fsx_ticks++;
    return 0;
}

unsigned os88_ticks(void) { return (unsigned)h_fsx_ticks; }

/* THE TWO SCREENS ARE ANSWERED FROM SCRIPTS OF THEIR OWN (wave 5): the
 * play loop drains EVERY key the BIOS holds each tick, so a key meant for the
 * postview put in the play script would be eaten by the level ('f' would keep
 * it). While the screens' mode is up the stub reads lem_onscr - the program's
 * own flag, declared here ahead of it (a tentative definition; lemmings.c's
 * is the same object) - and answers the preview from h_pv_keys and the
 * postview from h_po_keys, one key each time either asks, and Esc once a
 * script is spent, so every screen is bounded the way the loop is.
 *
 * A SCREEN STARTS ON A BUFFER THE LAST ONE LEFT (wave 5's second review):
 * until a screen has been answered 0 (h_scr_up), the stub hands it what the
 * play script still holds once play has run (the preview before it has not
 * seen play's keys yet) - the typematic Esc queued behind the one that
 * ended play - and then 0. A screen that does not drain the buffer first
 * reads that stale key as its answer. Each screen's own script starts after
 * the 0, and the key that answers a screen ends it, so the next one starts
 * on a buffer again. */

int os88_fsx_key(int wait)
{
    int k;

    (void)wait;
    if (lem_onscr) {
        if (!h_scr_up) {
            if (lem_started && h_fsx_keyi < h_fsx_keyn)
                return h_fsx_keys[h_fsx_keyi++];
            h_scr_up = 1;
            return 0;
        }
        if (!lem_started) {
            h_pv_asked++;
            if (h_pv_hold > 0) {
                h_pv_hold--;
                return 0;
            }
            k = (h_pv_ki < 4 && h_pv_keys[h_pv_ki]) ? h_pv_keys[h_pv_ki++]
                                                    : 0x011B;
        } else {
            h_po_asked++;
            k = (h_po_ki < 4 && h_po_keys[h_po_ki]) ? h_po_keys[h_po_ki++]
                                                    : 0x011B;
        }
        h_scr_up = 0;                   /* every script key ends its screen */
        return k;                       /* Esc: the screen has to be bounded */
    }
    if (h_fsx_keyi < h_fsx_keyn)
        return h_fsx_keys[h_fsx_keyi++];
    if (h_fsx_ticks >= h_fsx_maxticks)
        return 0x0100;                  /* Esc: the loop has to be bounded */
    return 0;
}

int os88_fsx_page(int page) { (void)page; return -1; }
int os88_fsx_surf(struct os88_rect *r)
{
    r->x1 = 0; r->y1 = 0; r->x2 = 639; r->y2 = 479;
    return 0;
}

int os88_fullscreen(void *win, int enter)
{
    int was = h_fullscreen;

    h_fullscreen = enter;
    if (was != enter)
        h_kernel_paint(win);        /* wm_raise drawing it whole / the exit's
                                     * wm_paint_all (kernel/wm.inc) */
    return 0;
}

static struct os88_mouse h_mouse = { 160, 100, 0 };
void os88_mouse(struct os88_mouse *m) { *m = h_mouse; }

/* --- files: the COMMITTED FIXTURE, and a scratch SYSTEM/APPDATA ------------ */
#define FIXDIR "apps/lemmings/hosttest/fixture/"

static int h_in_appdata;                /* the model of where we stand */
static int h_have_appdata = 1;          /* ...and whether that folder exists */
static unsigned char h_sav[64];
static unsigned h_savn;

/* h_fixdir is the folder the whole-file reads resolve in. It is the committed
 * fixture except in the one case below, which points it at a band written to be
 * TOO BIG on purpose. */
static const char *h_fixdir = FIXDIR;
static int h_reads;                     /* whole-file reads - see the deferral
                                         * checks: a KEYSTROKE may make none */
static int h_writes;

/* IT REFUSES A SHORT BUFFER AND READS NOTHING, which is the real slot's
 * contract and NOT what this stub used to do: `fread(buf, 1, cap, f)` silently
 * truncated a file larger than the buffer and reported a successful read of
 * `cap` bytes. os88.h at os88_file_read(): "refuses a short buffer with
 * FERR_BIG and reads nothing" - and the two guards this wave's biggest bss
 * decision rests on are exactly the ones a truncating stub cannot exercise:
 * lem_str_load()'s `n >= LEM_STRBUF` ceiling and lem_data_load()'s
 * `n < LEM_MAN_SIZE`. With the old stub a 512-byte growth of the string band
 * passed here and refused on the glass. */
unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    char path[256];
    FILE *f;
    long size;
    size_t n;

    h_reads++;
    if (h_in_appdata) {
        if (strcmp(name, "LEMMINGS.SAV") != 0 || h_savn == 0)
            return 0;
        if (h_savn > cap)
            return 0;                   /* FERR_BIG */
        memcpy(buf, h_sav, h_savn);
        return h_savn;
    }
    snprintf(path, sizeof(path), "%s%s", h_fixdir, name);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size < 0 || (unsigned long)size > (unsigned long)cap) {
        fclose(f);
        return 0;                       /* FERR_BIG: nothing is read */
    }
    fseek(f, 0, SEEK_SET);
    n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return (unsigned)n;
}

/* os88_file_read_seg - a WHOLE BAND into a modelled claim. The real slot takes
 * a segment, needs a 512-aligned base (SPEC.md 2.1.1) and is capped by `cap`;
 * this checks the same three things, because a part read to a base the machine
 * would refuse is int 13h error 09h ON REAL HARDWARE and nowhere else
 * (LESSONS.md 13). */
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    char path[256];
    FILE *f;
    long size;
    size_t n;
    unsigned char *dst;

    h_reads++;
    if ((seg & 0x1F) != 0) {
        fail("os88_file_read_seg to a base that is not 512-byte aligned - "
             "int 13h answers error 09h on real hardware and QEMU never "
             "shows it (SPEC.md 2.1.1)");
        return 0;
    }
    snprintf(path, sizeof(path), "%s%s", h_fixdir, name);
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || (unsigned long)size > (unsigned long)cap) {
        fclose(f);
        return 0;
    }
    dst = h_ptr(seg, 0);
    n = fread(dst, 1, (size_t)size, f);
    fclose(f);
    return (unsigned)n;
}

static int h_write_fail;                /* the disk refuses (full, protected) */

int os88_file_write(const char *name, const void *buf, unsigned count)
{
    h_writes++;
    if (h_write_fail)
        return -1;
    if (!h_in_appdata || strcmp(name, "LEMMINGS.SAV") != 0)
        return -1;
    if (count > sizeof(h_sav))
        return -1;
    memcpy(h_sav, buf, count);
    h_savn = count;
    return 0;
}

/* The dive model: ordinal 0 is SYSTEM at the root, ordinal 0 is APPDATA inside
 * it. h_have_appdata turns the whole path off, which is the case the Save
 * Progress row greys on. */
static int h_depth;                     /* 0 root, 1 SYSTEM, 2 APPDATA */
static int h_gotos;                     /* REMOUNTS - os88_file_goto() only */

int os88_file_find(int ordinal, struct os88_find *f)
{
    if (!h_have_appdata)
        return -1;
    if (ordinal != 0)
        return -1;
    memset(f, 0, sizeof(*f));
    f->type = OS88_FT_DIR;
    if (h_depth == 0) {
        strcpy(f->name, "SYSTEM");
        f->clus = 100;
    } else if (h_depth == 1) {
        strcpy(f->name, "APPDATA");
        f->clus = 200;
    } else
        return -1;
    return 1;                           /* a next ordinal; -1 ends the walk */
}

void os88_file_here(struct os88_place *p) { p->clus = 0; p->vol = 1; }

static int h_goto(unsigned clus)
{
    if (clus == 0)
        h_depth = 0;
    else if (clus == 100)
        h_depth = 1;
    else if (clus == 200)
        h_depth = 2;
    else
        return -1;
    h_in_appdata = (h_depth == 2);
    return 0;
}

int os88_file_goto(struct os88_place *p)
{
    h_gotos++;
    return h_goto(p->clus);
}

/* The QUIET form, which is what the dive uses: inside the volume it is a word
 * and no disk I/O at all, where os88_file_goto() is documented as "A REMOUNT:
 * real floppy I/O". h_gotos counts only the expensive one, so the harness can
 * assert that a progress read makes NONE of them. */
int os88_file_goto_q_mark(unsigned clus, int vol)
{
    (void)vol;
    return h_goto(clus);
}

int os88_ferr(void) { return OS88_FERR_OK; }

/* --- the runtime's string helpers ------------------------------------------ */
void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }

void os88_strcpy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;

    if (cap == 0)
        return;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

char *os88_utoa(unsigned v, char *dst6)
{
    char rev[8];
    int n = 0, i = 0;

    do {
        rev[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        dst6[i++] = rev[--n];
    dst6[i] = 0;
    return dst6;
}

/* ============================================================================
 * THE PROGRAM ITSELF
 * ==========================================================================*/
#define LEM_HOST 1                      /* the counters the package never
                                         * carries (lemdraw.c lem_c_rcadd) */
#include "lemmings.c"

/* lem_game_frame - one whole frame at once. THE HARNESS'S, and only the
 * harness's: the bracket's loop steps a frame a chunk at a time
 * (ovl_play_loop), so this lived in lemgame.c as ~40 resident bytes nothing on
 * the machine called (wave 3's third review) and lives here now. */
static void lem_game_frame(void)
{
    if (!ovl_frame_begin())
        return;
    lem_frame_chunk(LEM_MAXLEM);
    ovl_frame_end();
}

/* frame_shows_world - the view window of the world IS what is on the screen.
 *
 * THE ASSERTION THE HARNESS DID NOT HAVE, and a wave-2 review defect walked
 * straight through the gap: `lem_r_scroll()` CACHES (`lb_scroll`) and skips the
 * 12,800-byte compose when the view has not moved, so a second call with the
 * same view after the level was composed did NOTHING and the shadow still held
 * the world as it was BEFORE the compose - a correct panel, a correct minimap
 * and a completely black world on a CGA. Every counter the harness had said
 * the frame was fine, because the pixels HAD been written, one call earlier and
 * out of the wrong buffer. */
/* h_twice_nocur - h_twice() outside the pointer's own box, which a tick
 * that changed anything takes off and puts back by design (lemdraw.c
 * ovl_tick_draw): on VGA a scroll moves the box's WORLD pixels four to the
 * left under a pointer that stayed put, so the margin is the box plus four. */
/* h_cur_missing - the lit pointer's pixels that are NOT on the glass. A
 * compose that latched a rectangle over it without drawing it into the
 * scratch first (lem_r_cdraw) leaves the pointer half rubbed out while the
 * harness still believes it lit - glass and world agree, so only this sees
 * it. */
static int lem_aim_dense, lem_aim_sparse;   /* the aiming share, %%
                                             * (printed in the cost table) */
static int h_cur_missing(void)
{
    int gx, gy, sx, sy, n = 0;

    if (!h_cur_on)
        return 0;
    for (gy = 0; gy < 14; gy++)
        for (gx = 0; gx < 14; gx++) {
            if (!h_cur_bits_was(gx, gy))
                continue;
            sx = h_cur_x + gx;
            sy = h_cur_y + gy;
            if (sx < 0 || sx >= H_SCR_W || sy < 0 || sy >= H_SCR_H)
                continue;
            if (h_screen[sy][sx] != h_cur_ink(gx, gy))
                n++;
        }
    return n;
}

static int h_twice_nocur(void)
{
    int x, y, n = 0;

    for (y = 0; y < H_SCR_H; y++)
        for (x = 0; x < H_SCR_W; x++) {
            if (x >= lem_mx - 7 - 8 && x < lem_mx + 7 + 8 &&
                y >= lem_my - 7 && y < lem_my + 7)
                continue;
            if (h_wrote[y][x] > 1)
                n++;
        }
    return n;
}

static void frame_shows_world(const char *where)
{
    int x, y, bad = 0;

    /* THE WINDOW BEFORE THE PIXELS. On VGA the world is composed straight into
     * the framebuffer the card is displaying, so "the right pixels" is only
     * half of it - the card also has to be pointed at them, which
     * lem_r_prep() does and the first version did not (lemraster.c
     * lem_r_prep). This asserts the ORDER; the register itself is
     * emulator-only evidence. */
    if (h_r_kind == 0 && !h_r_vstart)
        fail("the VGA frame was presented before lem_r_prep() pointed the "
             "card at the world - the display window is still VRAM 0");

    for (y = 0; y < H_VIEW_H; y++)
        for (x = 0; x < H_SCR_W; x++)
            if (h_screen[y][x] != h_world[y][lem_view + x])
                bad++;
    if (bad) {
        printf("  %s: %d of %d view pixels do not match the world\n",
               where, bad, H_VIEW_H * H_SCR_W);
        fail("the first frame does not show the level - the world window and "
             "the glass disagree");
    }
}


/* spray_stray - view pixels that are neither the background (T on VGA, W on
 * the shadow cards) nor under something drawn over it on purpose: an object,
 * a live lemming hung generously off its foot (ovl_cur_hit's box), the lit
 * pointer. A spray whose erasure missed a particle leaves exactly such a
 * pixel, and nothing else does - which the count BEFORE the bomb proves. */
static int spray_stray(void)
{
    int x, y, wx, k, n = 0, in, ref, ox, oy, ow, oh;
    struct lem_l *p;

    for (y = 0; y < H_VIEW_H; y++)
        for (x = 0; x < H_SCR_W; x++) {
            wx = h_r_scroll + x;
            ref = h_r_kind == 0 ? h_t[y][wx] : h_world[y][wx];
            if (h_screen[y][x] == ref)
                continue;
            if (h_cur_on && x >= h_cur_x - 8 && x < h_cur_x + 22 &&
                y >= h_cur_y && y < h_cur_y + 14)
                continue;
            in = 0;
            for (k = 0; k < lem_nob && !in; k++) {
                ox = lem_ob[k].x;
                oy = lem_ob[k].y;
                ow = (int)lem_ty[lem_ob[k].id].w;
                oh = (int)lem_ty[lem_ob[k].id].h;
                if (wx >= ox && wx < ox + ow && y >= oy && y < oy + oh)
                    in = 1;
            }
            for (k = 0; k < lem_released && !in; k++) {
                p = &lem_lm[k];
                if (!(p->fl & LF_GONE) && wx >= p->x - 16 && wx <= p->x + 16 &&
                    y >= p->y - 40 && y <= p->y + 8)
                    in = 1;
            }
            if (!in)
                n++;
        }
    return n;
}

/* spray_run - THE EXPLOSION'S SPRAY, played through on one card (wave 6,
 * decision 5): lemming 0 is given the bomb, and the level is played until its
 * spray has run its 52 frames and three more. Every frame the glass is held
 * to the picture as the card defines it; while the spray flies its pixels
 * have to BE there; and once it is over, not one stray pixel may be left
 * anywhere in the view. `name` labels the line. */
static void spray_run(const char *name, int aim)
{
    int f, before, worst = 0, after, frames = 0, bombed = -1, tail = 0;
    int px0, bad = 0, x, y, miss = 0;

    /* a FRESH raster each run, as the machine's mode set is: the model's
     * lem_r_scroll caches its view, so a second run at the same view would
     * inherit the first one's shadow (frame_shows_world's trap) */
    if (h_r_kind == 0)
        lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);
    else
        lem_r_setup(LEM_RKIND_CGA, 0xB800, 0x1000, 0x1000);
    h_r_reset();
    lem_view = 0;
    lem_view_move(lem_ent_u16(0, LEM_E_STARTX));
    lem_view = (lem_view + 4) & ~7;
    ovl_level_load(0);
    lem_world_clear(1);
    ovl_frame_panel();
    lem_compose_level(0);
    lem_mx = 316;
    lem_my = 8;
    ovl_frame_first(0);
    ovl_tick_draw(0, 0);
    before = -1;
    px0 = h_r_spraypix;
    for (f = 0; f < 600 && !lem_finished; f++) {
        if (bombed < 0 && lem_released > 0 && lem_lm[0].act == LA_WALK) {
            before = spray_stray();
            ovl_select_skill(LB_EXPLODE);
            lem_hit_n = 1;
            lem_hit_l1 = 0;
            lem_hit_l2 = -1;
            ovl_assign();
            bombed = f;
        }
        if (aim && bombed >= 0) {
            /* THE PLAYER AIMS INTO IT: the pointer steps over the bomber's
             * foot, a pointer-only tick between frames as well as the
             * frame's own, so the spray meets every pointer path - the
             * compose that carries it, the glass pair, the shadow's
             * save-under (ovl_cur_hit) */
            lem_mx = lem_lm[0].x - lem_view - 6 + (f % 5) * 3;
            lem_my = lem_lm[0].y - 12 + (f % 3) * 4;
            if (lem_mx < 0)
                lem_mx = 0;
            if (lem_mx > 319)
                lem_mx = 319;
            if (lem_my < 0)
                lem_my = 0;
            ovl_tick_draw(0, 0);
            miss += h_cur_missing();
            lem_mx += 2;
        }
        lem_game_frame();
        ovl_tick_draw(1, 0);
        miss += h_cur_missing();
        if (h_r_kind == 0)
            for (y = 0; y < H_VIEW_H; y++)
                for (x = 0; x < H_SCR_W; x++)
                    if (h_screen[y][x] != h_world[y][lem_view + x])
                        bad++;
        if (lem_lm[0].pf) {
            frames++;
            if ((x = spray_stray()) > worst)
                worst = x;
        } else if (frames && ++tail >= 3) {
            break;
        }
    }
    lem_mx = 316;                   /* the pointer back in its corner, so
    lem_my = 8;                      * nothing of it is over the site */
    ovl_tick_draw(0, 0);
    after = spray_stray();
    printf("  the spray on %s%s: bombed at frame %d, %d spray frames drawn, "
           "%d particles, worst %d of the view off the background while it "
           "flew; %d stray pixels before the bomb and %d after it, %d "
           "glass/world mismatches, %d pointer pixels missing\n", name,
           aim ? ", aimed into" : "", bombed, frames, h_r_spraypix - px0,
           worst, before, after, bad, miss);
    if (miss)
        fail("the pointer lost pixels over a spray - a particle drawn or put "
             "back under it without its save-under knowing (SPEC.md 116.4.6)");
    if (bombed < 0)
        fail("the spray row never gave lemming 0 the bomb - it tests nothing");
    if (before != 0)
        fail("the view was off its background before the bomb - spray_stray "
             "counts something other than a lost particle");
    if (frames < LEM_PTIME || h_r_spraypix == px0 || worst == 0)
        fail("the explosion drew no spray, or fewer than its 52 frames - "
             "decision 5's particles are not on the glass (SPEC.md 116.8)");
    if (after != 0)
        fail("a spray left pixels behind after its last frame - its erasure "
             "missed the box it was drawn in (ovl_spray_rects / "
             "ovl_spray_erase)");
    if (bad)
        fail("during the spray the VGA glass is not the world - a compose "
             "latched a byte it never composed");
}

/* spray_area_one - the byte-rows ONE spray asks a VGA game frame to compose,
 * frame by frame through its LEM_PTIME frames, from foot (x, y): exactly what
 * ovl_spray_rects puts in the list (its rectangles are one a 16-row band, so
 * they never merge with each other and the sum is the area). The mean a
 * frame, in byte-rows; *nr the mean rectangles x 10. */
static long spray_area_one(int x, int y, int *nr, long *rows)
{
    struct lem_l *p;
    int f, k, n = 0, rects = 0;
    long a = 0, rw = 0;
    unsigned char *q;

    for (k = 0; k < lem_released; k++) {
        lem_lm[k].pf = 0;
        lem_lm[k].dpf = 0;
    }
    p = &lem_lm[0];
    p->fl |= LF_GONE;
    p->x = x;
    p->y = y;
    p->pf = 1;
    for (f = 0; f <= LEM_PTIME; f++) {
        lem_nrc = 0;
        ovl_spray_rects();
        for (k = 0; k < lem_nrc; k++) {
            q = &lem_rc[k * 4];
            a += (long)(q[1] - q[0] + 1) * (q[3] - q[2] + 1);
            rw += q[3] - q[2] + 1;
        }
        rects += lem_nrc;
        n++;
        if (p->pf && ++p->pf > LEM_PTIME)
            p->pf = 0;
    }
    lem_nrc = 0;
    *nr = n ? rects * 10 / n : 0;
    *rows += n ? rw / n : 0;
    return n ? a / n : 0;
}

/* spray_area - THE SPRAY'S COMPOSED AREA, HELD TO A CEILING (wave 6's second
 * review): one box over the whole cloud, the old frame's joined with the new,
 * recomposed ~3x what the pixels that change need - T copied into the scratch
 * four planes a row and latched back, every object in the strip redrawn - on
 * the adapter where the compose is already the busy frame. The bins make it a
 * rectangle a 16-row band. Played from three feet in the view, against the
 * NEGATIVE CONTROL h_r_sprayonebox (the bins answer the box: the one-box
 * build's area), which must go over the ceiling or the row measures nothing.
 * LEMTEST_PARTICLES names the real PARTICLES.DAT (apps/lemmings/build.sh
 * passes build/lemdata/'s when it was fetched): its 8,160 bytes go over the
 * fixture's invented table, the bounds after it are recomputed by the rule
 * os88lem.py writes them by, and both are put back after. */
#define LEMTEST_SPRAY_CEIL 1200L     /* byte-rows a frame: the bins measure
                                     * 228 on the fixture and 828 on the
                                     * real table, one box 1,475 and 2,961 */

/* spray_area_nuke - THE NUKE'S SHAPE (wave 6's third review): nsp sprays lit
 * a frame apart, as ovl_nuke_step lights one fuse a frame and each flies
 * LEM_PTIME frames, on a list the margin's crowd has ALREADY FILLED - every
 * lemming in the margin is a rectangle of its own in ovl_vframe, so the 40
 * are there before the first spray is cut. The bins were priced for one spray
 * on an empty list: a nuke would have paid their two 80-pair walks and up to
 * ten full-list adds N times a frame, for an area saving the overlapping
 * boxes fold away anyway. Prints the byte-rows, the ovl_rc_add calls and the
 * bin walks a frame, and HOLDS the fallback: no walk, and no more adds than
 * sprays in flight - what one box a spray costs. The NEGATIVE CONTROL runs
 * the bins unconditionally (ovl_spray_bins, the second review's build) and
 * must cost at least one walk and more adds, or the row cannot see the
 * regression. nsp = 1 on the full list is the lone spray that may NOT bin:
 * ten rectangles would run ovl_rc_add's best-fit loop. */
static void spray_area_nuke(const char *label, int nsp)
{
    int f, k, leg, flying, held = 0, rel = lem_released;
    long a[2], adds[2], walks[2], fr[2], flew[2];
    unsigned char *q;
    struct lem_l *p;

    lem_released = LEM_MAXLEM;
    for (leg = 0; leg < 2; leg++) {
        a[leg] = adds[leg] = walks[leg] = fr[leg] = flew[leg] = 0;
        for (k = 0; k < LEM_MAXLEM; k++) {
            lem_lm[k].pf = 0;
            lem_lm[k].dpf = 0;
        }
        for (f = 0; f < nsp + LEM_PTIME; f++) {
            if (f < nsp) {
                p = &lem_lm[f];
                p->fl |= LF_GONE;
                p->x = lem_view + 40 + (f * 5) % 240;
                p->y = 50 + (f % 5) * 22;
                p->pf = 1;
            }
            /* the crowd: 40 lemmings' rectangles, 2 bytes x 11 rows, spread
             * so that none touches another - the list is full first */
            lem_nrc = 0;
            for (k = 0; k < LEM_NRC; k++)
                ovl_rc_add((lem_view >> 3) + (k % 10) * 4,
                           (lem_view >> 3) + (k % 10) * 4 + 1,
                           (k / 10) * 40, (k / 10) * 40 + 10, 1);
            if (lem_nrc != LEM_NRC)
                fail("spray_area_nuke's crowd did not fill the list");
            flying = 0;
            for (k = 0; k < nsp; k++)
                if (lem_lm[k].pf | lem_lm[k].dpf)
                    flying++;
            lem_c_rcadd = 0;
            h_r_spraywalks = 0;
            if (leg == 0) {
                ovl_spray_rects();
            } else {
                for (k = 0; k < nsp; k++)
                    if (lem_lm[k].pf | lem_lm[k].dpf)
                        ovl_spray_bins(&lem_lm[k]);
            }
            adds[leg] += lem_c_rcadd;
            walks[leg] += h_r_spraywalks;
            for (k = 0; k < lem_nrc; k++) {
                q = &lem_rc[k * 4];
                a[leg] += (long)(q[1] - q[0] + 1) * (q[3] - q[2] + 1);
            }
            a[leg] -= LEM_NRC * 2L * 11;    /* the crowd's own byte-rows */
            flew[leg] += flying;
            fr[leg]++;
            if (leg == 0 && flying &&
                (h_r_spraywalks || lem_c_rcadd > flying))
                held++;
            for (k = 0; k < nsp; k++)
                if (lem_lm[k].pf && ++lem_lm[k].pf > LEM_PTIME)
                    lem_lm[k].pf = 0;
        }
    }
    lem_nrc = 0;
    lem_released = rel;
    printf("  the nuke's shape on the %s table, %d spray%s lit a frame apart "
           "on a full list: %ld byte-rows, %ld.%ld adds and %ld.%ld walks a "
           "frame over %ld frames (%ld spray-frames; one box a spray is one "
           "add and no walk), against the bins unconditionally (the negative "
           "control): %ld byte-rows, %ld.%ld adds, %ld.%ld walks\n",
           label, nsp, nsp == 1 ? "" : "s", a[0] / fr[0],
           adds[0] * 10 / fr[0] / 10, adds[0] * 10 / fr[0] % 10,
           walks[0] * 10 / fr[0] / 10, walks[0] * 10 / fr[0] % 10, fr[0],
           flew[0], a[1] / fr[1],
           adds[1] * 10 / fr[1] / 10, adds[1] * 10 / fr[1] % 10,
           walks[1] * 10 / fr[1] / 10, walks[1] * 10 / fr[1] % 10);
    if (held)
        fail("the spray's bins were taken with the list full - every spray "
             "in flight paid a walk or more than one add; ovl_spray_rects "
             "must fall back to one box there (SPEC.md 116.4.6)");
    if (walks[1] == 0 || adds[1] <= adds[0])
        fail("spray_area_nuke's negative control (the bins unconditionally) "
             "cost no walk or no more adds than the fallback - the row "
             "cannot see the regression");
}

/* spray_area_tab - the measurement on the table lem_ptseg holds now */
static void spray_area_tab(const char *label)
{
    int i, nr = 0, nrbox = 0, r, rb;
    long a = 0, abox = 0, rows = 0, rowsbox = 0;

    for (i = 0; i < 3; i++) {
        /* a foot mid-view at three heights, on three bit phases */
        a += spray_area_one(lem_view + 156 + i * 3, 70 + i * 30, &r, &rows);
        h_r_sprayonebox = 1;
        abox += spray_area_one(lem_view + 156 + i * 3, 70 + i * 30, &rb,
                               &rowsbox);
        h_r_sprayonebox = 0;
        nr += r;
        nrbox += rb;
    }
    a /= 3;
    abox /= 3;
    /* the rows are what the compose pays a fixed price for, four planes a
     * row (lem_r_bgrect's per-row call and setup), and the byte-rows what it
     * pays by the byte: both are printed, so SPEC.md 116.7.4 can price them */
    printf("  the spray's VGA compose on the %s table: %ld byte-rows in %ld "
           "rows a game frame, %d.%d rectangles (one a 16-row band), against "
           "%ld in %ld rows, %d.%d, for one box a spray (the negative "
           "control): %ld%% of the area\n",
           label, a, rows / 3, nr / 30, (nr / 3) % 10, abox, rowsbox / 3,
           nrbox / 30, (nrbox / 3) % 10, abox ? a * 100 / abox : 0);
    if (a == 0)
        fail("spray_area: the spray asked for no rectangle - it measures "
             "nothing");
    if (a > LEMTEST_SPRAY_CEIL)
        fail("a spray's VGA rectangles cover more byte-rows a frame than "
             "the ceiling - the bins are not reaching ovl_spray_rects "
             "(SPEC.md 116.4.6)");
    if (abox <= LEMTEST_SPRAY_CEIL)
        fail("spray_area's negative control (one box a spray) did not go "
             "over the ceiling - the row cannot see the regression");
    spray_area_nuke(label, 1);
    spray_area_nuke(label, 20);
    spray_area_nuke(label, 52);
}

static void spray_area(void)
{
    static unsigned char keep[LEM_PFR * LEM_PROW + LEM_PFR * 4];
    static unsigned char real[LEM_PFR * LEM_PROW];
    const char *path = getenv("LEMTEST_PARTICLES");
    unsigned char *t;
    int f, j, dx, dy, x0, x1, y0, y1;
    FILE *fp;

    if (!lem_ptseg) {
        fail("spray_area: the level carries no spray table");
        return;
    }
    spray_area_tab("fixture's invented");
    if (!path)
        return;
    fp = fopen(path, "rb");
    if (fp == NULL || fread(real, 1, sizeof real, fp) != sizeof real) {
        fail("LEMTEST_PARTICLES names no 8,160-byte PARTICLES.DAT");
        if (fp)
            fclose(fp);
        return;
    }
    fclose(fp);
    t = h_ptr(lem_ptseg, lem_ptoff);
    memcpy(keep, t, sizeof keep);
    memcpy(t, real, sizeof real);
    for (f = 0; f < LEM_PFR; f++) {
        x0 = y0 = 127;
        x1 = y1 = -128;
        for (j = 0; j < 80; j++) {
            dx = (signed char)t[f * LEM_PROW + j * 2];
            dy = (signed char)t[f * LEM_PROW + j * 2 + 1];
            if (dx == -128 || dy == -128)
                continue;
            if (dx < x0) x0 = dx;
            if (dx > x1) x1 = dx;
            if (dy < y0) y0 = dy;
            if (dy > y1) y1 = dy;
        }
        t[LEM_PFR * LEM_PROW + f * 4] = (unsigned char)x0;
        t[LEM_PFR * LEM_PROW + f * 4 + 1] = (unsigned char)x1;
        t[LEM_PFR * LEM_PROW + f * 4 + 2] = (unsigned char)y0;
        t[LEM_PFR * LEM_PROW + f * 4 + 3] = (unsigned char)y1;
    }
    spray_area_tab("REAL (LEMTEST_PARTICLES)");
    memcpy(t, keep, sizeof keep);
}

/* ============================================================================
 * AN INDEPENDENT LAYOUT, written from the geometry and not from lemui.c
 * ==========================================================================*/

static int exp_rows(int h)
{
    int r = (h - 2 - 8) / 9 + 1;

    if (r > 20)
        r = 20;
    if (r < 3)
        r = 3;
    return r;
}

static int exp_nlist(int h)
{
    int n = exp_rows(h) - 5 - 1 - 2;    /* four state rows + Play + the gap,
                                         * then the tab strip and its gap */
    return n < 1 ? 1 : n;
}

/* --- reading the modelled glass -------------------------------------------- */
static char g_line[G_COLS + 1];

static const char *glass_row(int row)
{
    int c, last = -1;

    for (c = 0; c < G_COLS; c++) {
        g_line[c] = g_ch[row][c];
        if (g_line[c] != ' ')
            last = c;
    }
    g_line[last + 1] = 0;
    return g_line;
}

/* the attribute of a row's first non-space cell - which is how "this row is
 * drawn with the disabled pen" is asserted rather than looked at */
static int glass_attr(int row)
{
    int c;

    for (c = 0; c < G_COLS; c++)
        if (g_ch[row][c] != ' ')
            return g_at[row][c];
    return 0;
}

static int glass_has(const char *needle)
{
    int r;

    for (r = 0; r < G_ROWS; r++)
        if (strstr(glass_row(r), needle))
            return 1;
    return 0;
}

/* --- the audits ------------------------------------------------------------ */

static void audit_shadow(const char *where)
{
    int r, c;

    for (r = 0; r < lem_rows; r++)
        for (c = 0; c < lem_cols; c++) {
            if (lem_sh[(r << LEM_SH_SHIFT) + c] != g_ch[r][c]) {
                printf("lemtest: shadow != glass at %d,%d ('%c' vs '%c') "
                       "after %s\n", r, c,
                       lem_sh[(r << LEM_SH_SHIFT) + c], g_ch[r][c], where);
                g_bad++;
                return;
            }
            if (lem_sha[(r << LEM_SH_SHIFT) + c] != g_at[r][c]) {
                printf("lemtest: shadow attr != glass at %d,%d (%d vs %d) "
                       "after %s\n", r, c,
                       lem_sha[(r << LEM_SH_SHIFT) + c], g_at[r][c], where);
                g_bad++;
                return;
            }
        }
}

static void audit_once(const char *where)
{
    int r, c;

    for (r = 0; r < G_ROWS; r++)
        for (c = 0; c < G_COLS; c++)
            if (g_n[r][c] > 1) {
                printf("lemtest: cell %d,%d written %d times in one repaint "
                       "(%s)\n", r, c, g_n[r][c], where);
                g_bad++;
                return;
            }
}

static void audit_inside(const char *where)
{
    if (g_maxy >= h_org_y + h_h) {
        printf("lemtest: drawing reached y=%d, past the %d-pixel content box "
               "(%s)\n", g_maxy - h_org_y, h_h, where);
        g_bad++;
    }
}

/* ============================================================================
 * THE RUN
 * ==========================================================================*/

static void repaint(const char *where)
{
    counters_reset();
    os88_paint(lem_win);
    audit_shadow(where);
    audit_once(where);
    audit_inside(where);
}

static void check_page(int h)
{
    int want_rows, want_nlist, i, lvl;
    char want[64];

    h_h = h;
    glass_clear();
    repaint("a full repaint");

    want_rows = exp_rows(h);
    want_nlist = exp_nlist(h);
    if (lem_rows != want_rows) {
        printf("lemtest: %d px box gives %d rows, the model says %d\n",
               h, lem_rows, want_rows);
        g_bad++;
    }
    if (lem_nlist != want_nlist) {
        printf("lemtest: %d px box gives %d list rows, the model says %d\n",
               h, lem_nlist, want_nlist);
        g_bad++;
    }

    /* Every visible level row carries the level the page arithmetic says it
     * should - rebuilt here, not read out of the program. */
    for (i = 0; i < lem_nlist; i++) {
        lvl = lem_top + i;
        if (lvl >= LEM_PERRAT)
            break;
        snprintf(want, sizeof(want), "%d", lvl + 1);
        if (!strstr(glass_row(LEM_LISTTOP + i), want)) {
            printf("lemtest: list row %d does not carry level %d: '%s'\n",
                   i, lvl + 1, glass_row(LEM_LISTTOP + i));
            g_bad++;
        }
    }

    printf("  %3d px box  %2d rows  %2d list rows  full repaint %3d calls "
           "%4d cells %3d rows %4d cols scanned\n", h, lem_rows, lem_nlist,
           g_calls, g_cells, lem_c_rows, lem_c_cols);
}

/* A selection move is TWO damages and they are drawn at two different times:
 * the two level rows on the keystroke, where the user is looking, and the seven
 * preview lines beside them once the arrow has STOPPED (lem_pv_defer,
 * os88_ontimer). The pane is ~140 of the ~180 glyph cells the move used to
 * cost, which at PERFORMANCE.md's ~900 us a cell is ~126 ms of ~168 on a
 * 4.77 MHz 8088 - and at autorepeat the machine fell that far behind on every
 * key held, which is input overrun, the third defect an emulator cannot show
 * you. This prints both halves and asserts the deferral rather than the
 * timing. */
static void check_move(int h)
{
    int c0, n0, c1, n1, r0, k0;

    h_h = h;
    lem_sel = 0;
    lem_top = 0;
    glass_clear();
    repaint("a full repaint");

    h_timer = 0;
    h_timer_arms = 0;
    h_clips = 0;
    counters_reset();
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    c0 = g_calls;
    n0 = g_cells;
    r0 = lem_c_rows;
    k0 = lem_c_cols;
    audit_shadow("a selection move");
    audit_once("a selection move");
    audit_inside("a selection move");
    if (lem_sel != 1) {
        fail("Down did not move the selection");
        return;
    }
    if (h_clips == 0)
        fail("a repaint reached from a KEY armed no clip region - the kernel "
             "arms one for W_PAINT and for nothing else (SPEC.md 11.3)");
    if (h_timer_arms != 1)
        fail("a selection move did not arm the preview pane's settle timer");
    if (lem_nlist >= 1 && glass_has("Level 2"))
        fail("the preview pane was re-lettered on the keystroke - it is "
             "supposed to settle");

    counters_reset();
    os88_ontimer(lem_win);
    c1 = g_calls;
    n1 = g_cells;
    audit_shadow("the settled preview pane");
    audit_once("the settled preview pane");
    audit_inside("the settled preview pane");
    if (lem_pvw > 2 && !glass_has("Level 2"))
        fail("the settle timer did not draw the preview pane");

    printf("  %3d px box  selection move %3d calls %4d cells %2d rows %3d "
           "cols scanned + settled pane %3d calls %4d cells %2d rows %3d "
           "cols\n", h, c0, n0, r0, k0, c1, n1, lem_c_rows, lem_c_cols);

    /* ...AND ON ROWS AND COLUMNS, WHICH IS THE HALF NEITHER OF THE TWO ABOVE
     * CAN SEE. A row that is composed and scanned and draws nothing costs ~64
     * columns of ~300-450 us EACH on a 4.77 MHz 8088 - half a glyph cell apiece
     * to decide not to draw - so a repaint that scans all twenty rows is ~400
     * ms and reads as "3 calls, 78 cells" in the two columns above. That is
     * PERFORMANCE.md rule 5 exactly, and it is what this row exists to stop
     * coming back (lemui.c lem_mark). Two level rows plus the Play control is
     * three; four is the slack.
     *
     * THE COLUMN BOUND IS THE SAME ARGUMENT ONE LEVEL DOWN and it is asserted
     * too, because the rows alone stopped being the whole cost: three rows of
     * the full 63 columns is 189 of them, 57-85 ms of pure scanning against
     * ~63 ms of drawing, on a key that autorepeats every ~100 ms. The two
     * level rows change their LIST half only (lemui.c lem_mark_level), so what
     * has to be scanned is 2 * lem_listw + the Play row's own width. */
    if (r0 > 4) {
        printf("lemtest: a selection move COMPOSED AND SCANNED %d rows (%d "
               "columns); it changes two level rows and the Play control\n",
               r0, k0);
        g_bad++;
    }
    if (k0 > 2 * lem_listw + lem_cols) {
        printf("lemtest: a selection move SCANNED %d columns; the two level "
               "rows are their list half (%d) and the Play row is %d\n",
               k0, 2 * lem_listw, lem_cols);
        g_bad++;
    }
    if (c0 > 32) {
        printf("lemtest: a selection move cost %d drawing calls; the whole "
               "point of the shadow is that it does not\n", c0);
        g_bad++;
    }
    /* ...AND ON CELLS, WHICH IS THE HALF THE CALL COUNT CANNOT SEE. A drawing
     * call is 756 us and a glyph cell ~900, so twelve calls carrying 280 cells
     * is 252 ms of cells and 9 ms of calls - it walks past a call gate
     * untouched and it is the whole of the cost. */
    if (n0 > 2 * lem_cols) {
        printf("lemtest: a selection move drew %d glyph cells; two level rows "
               "is %d, and the pane is deferred\n", n0, 2 * lem_cols);
        g_bad++;
    }

    /* HELD DOWN, THE PANE IS DRAWN ONCE. Three moves with no shot in between
     * arm the timer three times and letter the pane none, and the one shot that
     * follows draws the pane the user actually stopped on. */
    h_timer_arms = 0;
    counters_reset();
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    if (h_timer_arms != 3)
        fail("a repeated arrow did not re-arm the settle timer");
    os88_ontimer(lem_win);
    audit_shadow("three moves and one settle");

    /* ...AND A FULL PAINT BETWEEN THE ARROW AND ITS SHOT LEAVES THE SHOT
     * NOTHING TO DO (wave 6, wave 5's third review). "Select and play" is the
     * commonest gesture: arrow, then Enter inside LEM_PV_SETTLE, and the
     * launch ends in os88_fullscreen(win, 0)'s full paint - pane included -
     * so a shot still owed composed and scanned the pane's seven rows (~189
     * columns, ~57-85 ms on the XT) to draw nothing. An expose is the same. */
    h_timer_arms = 0;
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    if (h_timer_arms != 1)
        fail("the arrow before the expose did not arm the settle timer");
    repaint("an expose inside the settle");
    counters_reset();
    lem_c_rows = 0;
    lem_c_cols = 0;
    os88_ontimer(lem_win);
    printf("  %3d px box  arrow, full paint, then the shot: %d rows composed, "
           "%d cols scanned, %d calls\n", h, lem_c_rows, lem_c_cols, g_calls);
    if (lem_c_rows != 0 || g_calls != 0)
        fail("a settle shot outlived a full repaint that had already drawn "
             "the pane - it composed and scanned rows to draw nothing");

    /* THE SCROLLING MOVE, WHICH IS THE WORST KEYSTROKE THERE IS, AND IT MUST
     * BE RARE. `lem_layout` moves lem_top as soon as the selection leaves the
     * page and lem_pv_hold defers only the PREVIEW columns, never the list
     * half - so the keystroke that scrolls re-letters every list row: on the
     * 189-pixel box, ~12 calls and ~280 glyph cells, 9 ms + 252 ms = ~260 ms
     * against a ~100 ms autorepeat interval. That is input overrun, and the
     * arm above cannot see it: it presses Down at most four times and never
     * past lem_nlist, which is 7 even on the smallest modelled box.
     *
     * The fix it gates is that the page scrolls a PAGE at a time (lem_layout),
     * so the expensive keystroke is paid once per lem_nlist keys instead of on
     * every repeat after the first page. What is asserted is exactly that: at
     * most ONE keystroke in a page-and-a-bit costs more than two rows of
     * cells, and a keystroke that did not move the page never does. */
    h_timer_ok = 1;
    lem_sel = 0;
    lem_top = 0;
    glass_clear();
    repaint("a full repaint");
    {
        int k, keys, worst, over, top0, budget;

        keys = lem_nlist + 2;
        budget = 2 * lem_cols;
        worst = 0;
        over = 0;
        for (k = 0; k < keys; k++) {
            top0 = lem_top;
            counters_reset();
            os88_onkey(0, LEM_SC_DOWN, lem_win);
            audit_shadow("a scrolling selection move");
            audit_once("a scrolling selection move");
            audit_inside("a scrolling selection move");
            if (g_cells > worst)
                worst = g_cells;
            if (g_cells > budget) {
                over++;
                if (lem_top == top0)
                    fail("a keystroke that did not scroll the page cost more "
                         "than two rows of glyph cells");
            }
        }
        printf("  %3d px box  %2d keys down the page: worst %4d cells, %d of "
               "them over %d\n", h, keys, worst, over, budget);
        if (over > 1) {
            printf("lemtest: %d keystrokes of %d re-lettered the whole list; "
                   "the page must scroll a PAGE at a time (lemui.c "
                   "lem_layout), not a row\n", over, keys);
            g_bad++;
        }
        if (worst > (lem_nlist + LEM_STATE_ROWS + 4) * lem_cols)
            fail("the page scroll drew more cells than the whole content box "
                 "holds");
    }

    /* ...and with the timer REFUSED (kern_small), the pane is drawn on the
     * keystroke instead, which is the second path SPEC.md 13.8.2 asks for. */
    h_timer_ok = 0;
    lem_sel = 0;
    lem_top = 0;
    glass_clear();
    repaint("a full repaint");
    counters_reset();
    os88_onkey(0, LEM_SC_DOWN, lem_win);
    if (lem_pvw > 2 && !glass_has("Level 2"))
        fail("with the settle timer refused, the pane was never drawn at all");
    audit_shadow("a selection move with no timer");
    audit_once("a selection move with no timer");
    h_timer_ok = 1;
}

/* ==========================================================================
 * THE MECHANICS REPLAY (SPEC.md 116.12): `lemtest --mech LEVEL SCRIPT FRAMES
 * OUT` loads fixture level LEVEL, composes it exactly as the bracket does, and
 * then runs lem_game_frame() once a tick for up to FRAMES ticks, applying the
 * script's commands at the frame numbers they name and writing one TRACE LINE
 * a frame - the counts, the rate, the clock, MaxNumLemmings (the nuke
 * glitch moves it), and every lemming's x, y,
 * action, frame, flags and fuse. build.sh diffs the file against the trace
 * tools/lemsim.py writes from lemmings_3ds's reading of the same mechanics,
 * and against the committed copy of it.
 *
 * The script is the one tools/lemsim.py reads: `FRAME VERB ARG`, with S n
 * (select button n), A i (assign to lemming i, named), H x y r (the hit test
 * at world (x, y) with the right button r, and the assignment through it - the
 * right-click glitch's road), N (nuke), R d (release-rate direction -1/0/1)
 * and P (pause). A command applies BEFORE that frame runs.
 * ========================================================================*/
static int mech_run(int level, const char *script, int frames, const char *out)
{
    static char line[256];
    FILE *sf, *of;
    static int cf[512], cv[512], ca[512], cb[512], cc[512];
    int nc = 0, n, k, i, pmax = 0, pmaxf = 0, lmax = 0;
    long ptot = 0;
    struct lem_l *p;

    sf = fopen(script, "r");
    if (!sf) {
        printf("lemtest: --mech: cannot read %s\n", script);
        return 1;
    }
    while (fgets(line, sizeof line, sf) && nc < 512) {
        char verb[8];
        int f, a = 0, b = 0, c = 0;
        char *h = strchr(line, '#');

        if (h)
            *h = 0;
        if (sscanf(line, "%d %7s %d %d %d", &f, verb, &a, &b, &c) < 2)
            continue;
        cf[nc] = f;
        cv[nc] = verb[0];
        ca[nc] = a;
        cb[nc] = b;
        cc[nc] = c;
        nc++;
    }
    fclose(sf);

    glass_clear();
    lem_win = os88_main();
    if (!lem_win || !lem_ok)
        return printf("lemtest: --mech: the fixture did not load\n"), 1;
    os88_onwake(lem_win);
    h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_CGA320);
    h_vidkind = OS88_VID_VGA;
    lem_mode_ok = lem_probe_mode(lem_win);
    lem_rating = 0;
    lem_ratpend = 0;
    lem_rating_load(0);
    lem_sel = level;
    if (!ovl_level_load(level))
        return printf("lemtest: --mech: level %d did not load\n", level), 1;
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);
    lem_r_tsetup(lem_cl_t);
    lem_world_clear(1);
    if (!lem_compose_level(level))
        return printf("lemtest: --mech: level %d did not compose\n", level), 1;
    lem_snd = 1;                    /* the speaker answers: every frame's cue
                                     * reaches os88_snd_tone, which counts */
    h_tones = 0;

    of = fopen(out, "w");
    if (!of)
        return printf("lemtest: --mech: cannot write %s\n", out), 1;
    for (n = 1; n <= frames; n++) {
        for (k = 0; k < nc; k++) {
            if (cf[k] != n)
                continue;
            switch (cv[k]) {
            case 'S':
                ovl_select_skill(ca[k]);
                break;
            case 'A':
                lem_hit_n = (ca[k] < lem_released) ? 1 : 0;
                lem_hit_l1 = ca[k];
                lem_hit_l2 = -1;
                ovl_assign();
                break;
            case 'H':
                /* THE HIT TEST REPLAYED: world (x, y) with the right button
                 * as given, and the assignment through it - which is the one
                 * road the RIGHT-CLICK GLITCH takes (lemact.c ovl_assign) */
                ovl_hittest(ca[k], cb[k], cc[k], 1);
                ovl_assign();
                break;
            case 'N':
                ovl_nuke();
                break;
            case 'R':
                lem_rrdir = ca[k];
                break;
            case 'P':
                ovl_toggle_pause();
                break;
            }
        }
        k = h_r_probecalls;
        /* A MID-FRAME COMMAND (h x y r, n): the frame is begun, FIVE lemmings
         * stepped, the press or the nuke made through the bracket's own
         * routines - which QUEUE it, a frame being part-stepped - and the
         * frame finished. lemsim applies it before the NEXT frame, which is
         * where Lemmix makes every assignment (SPEC.md 116.7 point 3). */
        for (i = 0; i < nc; i++)
            if (cf[i] == n && (cv[i] == 'h' || cv[i] == 'n'))
                break;
        if (i < nc && ovl_frame_begin()) {
            lem_frame_chunk(5);
            for (i = 0; i < nc; i++) {
                if (cf[i] != n)
                    continue;
                if (cv[i] == 'h')
                    ovl_view_press(ca[i], cb[i], cc[i]);
                else if (cv[i] == 'n')
                    ovl_nuke_press();
            }
            if (!lem_qhit && !lem_qnuke)
                return printf("lemtest: --mech: a mid-frame command at "
                              "frame %d was applied at once, not queued\n",
                              n), 1;
            lem_frame_chunk(LEM_MAXLEM);
            ovl_frame_end();
        } else {
            lem_game_frame();
        }
        k = h_r_probecalls - k;
        if (k > pmax)
            pmax = k, pmaxf = n;
        ptot += k;
        for (i = 0, k = 0; i < lem_released; i++)
            if (!(lem_lm[i].fl & LF_GONE))
                k++;
        if (k > lmax)
            lmax = k;
        fprintf(of, "%d %d %d %d %d %d %d %d %d", n, lem_released, lem_out,
                lem_saved, lem_removed, lem_rr, lem_min, lem_sec, lem_maxlem);
        for (i = 0; i < lem_released; i++) {
            p = &lem_lm[i];
            fprintf(of, " %d:%d,%d,%d,%d,%d,%d", i, p->x, p->y, p->act,
                    p->frame, p->fl & 0x1F, p->timer);
        }
        fputc('\n', of);
        if (lem_finished)
            break;
    }
    fclose(of);
    if (n > frames)
        n = frames;
    /* THE MECHANICS' SHARE OF A FRAME, priced: a probe CALL is 178 us on a
     * 4.77 MHz 8088 (tests/lembench, SPEC.md 116.7.1 - lem_has_pixel from C,
     * the call included), and the step's own C is not counted, so this is a
     * floor and the report says so. */
    printf("lemtest: --mech level %d, %d frames -> %s; at most %d lemmings "
           "out, probe calls %d a frame worst (frame %d) and %ld mean - "
           "%ld us and %ld us of probes on the XT\n", level, n, out, lmax,
           pmax, pmaxf, ptot / n, (long)pmax * 178, ptot / n * 178);
    /* every script assigns skills and lets the hatch open, so a run with no
     * tone at all is a sound path that came unplugged (SPEC.md 116.11) */
    printf("lemtest: --mech level %d, %d speaker tones\n", level, h_tones);
    if (h_tones == 0)
        return printf("lemtest: --mech: not one tone - the cue never reached "
                      "os88_snd_tone\n"), 1;
    return 0;
}

/* h_inc_equ - the value of `NAME equ N` in an assembly include, or -1. */
static long h_inc_equ(const char *path, const char *name)
{
    FILE *f = fopen(path, "r");
    char ln[256], nm[64];
    long v;

    if (f == 0)
        return -1;
    while (fgets(ln, sizeof ln, f)) {
        if (sscanf(ln, "%63s equ %ld", nm, &v) == 2 && strcmp(nm, name) == 0) {
            fclose(f);
            return v;
        }
    }
    fclose(f);
    return -1;
}

/* check_sprec - THE ONE SHARED LAYOUT THE ASSEMBLY CANNOT CHECK FOR ITSELF.
 * On VGA the C keeps each lemming's last-drawn box in lemsprite.inc's sprite
 * records (lemdraw.c's lem_obox = lem_sprec), LEM_MAXLEM x 4 bytes of them.
 * The .inc's own guard compares against a literal 80, because LEM_MAXLEM is a
 * C #define it cannot see - so a pool raised in lemgame.c would assemble and
 * write past lb_sprec into lb_smk (wave 3's third review). Read both halves
 * here, from the files themselves, so neither side can move alone. */
static void check_sprec(void)
{
    long spmax = h_inc_equ("apps/lemmings/lemsprite.inc", "LEM_SPMAX");
    long sprec = h_inc_equ("apps/lemmings/lemsprite.inc", "LEM_SPREC");

    if (spmax < 0 || sprec < 0) {
        fail("check_sprec: LEM_SPMAX / LEM_SPREC not found in "
             "apps/lemmings/lemsprite.inc");
        return;
    }
    if ((long)LEM_MAXLEM * 4 + LEM_PBINS * 4 > spmax * sprec)
        fail("LEM_MAXLEM x 4 (lemdraw.c lem_obox) and the spray's bins no "
             "longer fit lemsprite.inc's LEM_SPMAX x LEM_SPREC sprite records "
             "- raise LEM_SPMAX");
    if (LEM_MAXLEM * 4 != 80 * 4 ||
        h_inc_equ("apps/lemmings/lemsprite.inc", "LEM_PBINS") != LEM_PBINS)
        fail("lemsprite.inc's lb_pbins (lb_sprec + 80 x 4, LEM_PBINS bands) "
             "and lemdraw.c's lem_pbins (lem_sprec + LEM_MAXLEM x 4) no "
             "longer name the same bytes");
}

int main(int argc, char **argv)
{
    int i, seen, seenrows, tc, tn, tf;

    check_sprec();

    if (argc == 6 && strcmp(argv[1], "--mech") == 0)
        return mech_run(atoi(argv[2]), argv[3], atoi(argv[4]), argv[5]);

    printf("lemtest: the LEMMINGS launcher against the committed fixture\n");

    glass_clear();
    lem_win = os88_main();
    if (lem_win == 0) {
        fail("os88_main() refused");
        return 1;
    }
    if (!lem_ok)
        fail("the fixture band did not load - is "
             FIXDIR " there, and is build/lemstr.h current?");
    if (lem_strn < LEMS_COUNT)
        fail("LEMSTR.LEM carries fewer rows than build/lemstr.h names");
    if (!lem_man_ok)
        fail("LEMMAN.LEM did not validate");
    if (!lem_rat_ok)
        fail("LEMR0.LEM did not load");

    /* the first wake: the progress file (the module is a PART, resident
     * from launch - SPEC.md 73.14.2 - and owes the wake nothing) */
    os88_onwake(lem_win);
    if (!lem_savable)
        fail("SYSTEM/APPDATA answered in the model but the row greys anyway");

    printf("lemtest: cost table (PERFORMANCE.md's prices: 756 us a drawing "
           "call, ~900 us a glyph cell)\n");
    check_page(189);                    /* the authored window on a VGA */
    check_page(137);                    /* a CGA: 200 - 20 - 24 - 18 - 1 */
    check_page(100);                    /* smaller than either */
    check_move(189);
    check_move(137);

    /* --- the level names come off the band, not out of the image ---------- */
    h_h = 189;
    glass_clear();
    lem_sel = 0;
    lem_top = 0;
    repaint("the list");
    if (!glass_has("Fixture one"))
        fail("the first fixture level's name is not on the glass");
    if (!glass_has("Fixture two"))
        fail("the second fixture level's name is not on the glass");

    /* --- the pane's title line is ELIDED, never cut mid-word (wave 4's
     * verifier: "Level 27 Call in the bomb s"). The fixture's second level
     * is 39 characters with its number, wider than the pane. */
    {
        int keep = lem_sel;

        lem_sel = 1;
        lem_pvline(0, lem_line, sizeof(lem_line));
        printf("  the pane's title line, %d columns: '%s'\n", lem_pvw,
               lem_line);
        if ((int)strlen(lem_line) > lem_pvw ||
            ((int)strlen("Level 2 Fixture three - water and steel") >
                 lem_pvw &&
             strcmp(lem_line + strlen(lem_line) - 3, "...") != 0))
            fail("a level title wider than the pane is cut rather than "
                 "elided with three full stops");
        lem_sel = keep;
    }

    /* --- the preview fields, in the original's wording -------------------- */
    if (!glass_has("Number of Lemmings"))
        fail("the preview pane does not carry the original's second line");
    if (!glass_has("To Be Saved"))
        fail("the preview pane does not carry the original's third line");
    if (!glass_has("Rating"))
        fail("the preview pane does not carry the Rating line");

    /* --- a greyed row names its fact (SPEC.md 47) ------------------------- */
    seen = 0;
    for (i = 0; i < LEM_PERRAT; i++)
        if (lem_rat_ok && !lem_ent_here(i))
            seen++;
    if (seen == 0)
        fail("the fixture has no greyed level row, so the fact path is "
             "never exercised");
    lem_sel = 3;                    /* the fixture's greyed row (FIXTURE_LEVELS:
                                     * three playable levels, then it) */
    glass_clear();
    repaint("the greyed row selected");
    if (glass_attr(LEM_LISTTOP + 3) != LEM_AT_GREY)
        fail("the greyed level row is not drawn with the disabled pen");
    counters_reset();
    lem_play(lem_win);
    tc = g_calls;
    tn = g_cells;
    tf = g_fills;
    audit_shadow("the fact screen");
    audit_once("the fact screen");
    audit_inside("the fact screen");
    if (lem_screen != LEM_SC_FACT)
        fail("Play on a greyed row did not show its fact");
    if (!glass_has("LEMSP"))
        fail("the fact does not name the band file that is missing");
    if (!glass_has("Level 4 Fixture two"))
        fail("the greyed level's fact is not headed the way every other "
             "surface names a level - 'Level 4 ...' (LEMS_PV_LEVEL)");
    if (!glass_has("its special picture is"))
        fail("a missing special picture's fact quotes something other than "
             "that ONE picture's cost (wave 4's third review)");
    if (!glass_has("disk"))
        fail("the fact does not name the geometry");
    if (!glass_has("Press mouse button to continue"))
        fail("the fact screen has no footer");
    printf("  list -> fact   %3d calls %4d cells (%d fill)\n", tc, tn, tf);
    /* A SCREEN CHANGE IS A DISMISSED OPAQUE CARD ONE FILE ALONG, and it took
     * the same path only after the wave-1 review. Repainting a whole new screen
     * against the OLD screen's shadow pays every erased character as a ~900 us
     * glyph cell: on the 189px box the twelve level rows each compose to one
     * full-width TEXT run whose shadow still carries the list half AND the
     * preview pane, so the flush's end trims reach almost nothing and the run
     * is ~56 cells - ~860 cells and ~790 ms of frozen glass on one click. One
     * os88_gfx_fill is 756 us and the seeded shadow then letters only what the
     * NEW screen carries (lemui.c lem_blank_glass). The fill count is asserted
     * because it is the whole mechanism. */
    if (tf != 1)
        fail("a screen change did not blank the glass first - it repaints "
             "against a shadow describing a different screen, and every "
             "erased character is paid as a glyph cell (lemui.c "
             "lem_blank_glass)");
    /* THE CELL COUNT IS NOT BUDGETED AND CANNOT BE. What a transition letters
     * is what the NEW screen carries: the fact screen is a wrapped paragraph
     * (196 cells here) and the list is a full screen of text (728, against a
     * W_PAINT's 735), so the number is the content and not the mechanism. The
     * fill above IS the mechanism, and it is what is asserted. */

    counters_reset();
    os88_onclick(0, 0, lem_win);
    printf("  fact -> list   %3d calls %4d cells (%d fill)\n",
           g_calls, g_cells, g_fills);
    if (g_fills != 1)
        fail("leaving the fact screen did not blank the glass first");
    if (lem_screen != LEM_SC_LIST)
        fail("a click did not leave the fact screen");

    /* --- Play on a playable row LAUNCHES (wave 5) ---------------------------
     * The level's preview is the first screen INSIDE the bracket now, in the
     * second foreign mode (SPEC.md 116.4.3), so the keystroke's whole share is
     * the launch flag and a wake: nothing drawn, nothing read. */
    lem_sel = 0;
    lem_launchpend = 0;
    h_reads = 0;
    counters_reset();
    lem_play(lem_win);
    printf("  list -> launch  %2d calls %4d cells (%d fill)\n",
           g_calls, g_cells, g_fills);
    if (!lem_launchpend)
        fail("Play on a playable row did not post the launch");
    if (g_calls != 0 || h_reads != 0)
        fail("Play drew or read on the keystroke - the preview is the "
             "bracket's first screen and the bands are the wake's");
    if (lem_screen != LEM_SC_LIST)
        fail("Play left the list for a screen the window no longer has");
    lem_launchpend = 0;

    /* --- NO KEYSTROKE TOUCHES A FILE (the gfx lock is HELD on all of them) --
     * os88.h pins os88_onkey and os88_onclick as "the UI task, the gfx lock
     * HELD". A whole-file read is a directory walk, a FAT walk and the data run
     * - three int 13h calls, ~400 ms apiece on the target - and the arrow keys
     * autorepeat at ~100 ms, so LEFT and RIGHT used to freeze the whole machine
     * for over a second per press with every other window's painter stopped
     * behind the lock. It is invisible in an emulator, whose floppy answers
     * instantly, which is why it is asserted here instead. */
    h_h = 189;
    lem_rating = 0;
    lem_ratpend = 0;
    lem_rating_load(0);
    lem_sel = 0;
    lem_top = 0;
    glass_clear();
    repaint("the list");

    h_reads = 0;
    h_wakes = 0;
    counters_reset();
    os88_onkey(0, LEM_SC_RIGHT, lem_win);
    if (h_reads != 0)
        fail("a RIGHT arrow read a file - LEMR<n>.LEM is a whole-file read and "
             "os88_onkey is delivered with the gfx lock HELD");
    if (lem_rating != 1)
        fail("the rating key did not move the rating");
    if (h_wakes == 0)
        fail("the rating key deferred its load and posted no wake, so the "
             "read is owed to nobody");
    if (g_calls == 0)
        fail("the rating key drew nothing at all - the tab strip has to move "
             "under it or the press is not acknowledged");
    if (g_cells > 2 * lem_cols)
        fail("the rating key re-lettered more than two rows - the level rows "
             "are HELD until the file lands, or the glass is blanked and then "
             "lettered again, which is a double draw");
    audit_shadow("a rating key");
    audit_once("a rating key");
    audit_inside("a rating key");
    seen = g_cells;
    seenrows = lem_c_rows;
    if (seenrows > 1 + LEM_STATE_ROWS)
        fail("a rating key COMPOSED AND SCANNED more rows than the tab strip "
             "and the four state rows - the level rows are HELD (lemui.c "
             "lem_mark)");

    /* A SECOND RATING KEY RE-ARMS AND DOES NOT STACK: a held LEFT is one file
     * read, not thirty. */
    os88_onkey(0, LEM_SC_RIGHT, lem_win);
    if (h_reads != 0)
        fail("a second rating key read a file");

    counters_reset();
    os88_onwake(lem_win);
    if (h_reads == 0)
        fail("the wake did not read the rating file the key deferred");
    if (lem_rat_have != lem_rating)
        fail("the wake read the wrong rating - the flag is re-armed, so it is "
             "whichever one the user stopped on");
    if (lem_sel != 0 || lem_top != 0)
        fail("the wake did not put the selection at the top of the new rating");
    audit_shadow("the deferred rating load");
    audit_once("the deferred rating load");
    audit_inside("the deferred rating load");
    printf("  rating key %d cells / %d rows scanned (the tab strip and the "
           "state rows) + the wake's load %3d calls %4d cells %2d rows\n",
           seen, seenrows, g_calls, g_cells, lem_c_rows);

    /* --- ...AND PLAY RECORDS NOTHING AT ALL --------------------------------
     * TWO FACTS IN ONE ROW, and the second one replaced the first.
     *
     * (1) ovl_progress_write() is two ordinal walks (os88_file_find has no
     * cursor, so every step between two of them walks directories itself) and
     * then a write that rewrites the FAT and a directory entry - so it may
     * never happen on Enter, Space or a click, all of which are under the lock.
     *
     * (2) AND THIS KEYSTROKE HAS NOTHING TO RECORD. It merely opens the preview
     * screen. In the original an access code is issued on COMPLETING a level,
     * and SPEC.md 116.8's Save Progress cell says of the refusing case that "the
     * session is played and the result is shown, and nothing is recorded" -
     * recording belongs to the RESULT, which is wave 4's postview. The build
     * that advanced lem_prog[] here credited a level nobody had played: "Save
     * Progress  1" on Tricky after one click, with no gameplay in the program.
     *
     * So Play must move NOTHING: not the file, not the in-memory value, and not
     * the deferred-write flag. This row is the guard on both halves - the day
     * wave 4 wires the postview up, the write it posts is still owed to the
     * wake, which the paragraph above is about. */
    lem_rating = 0;
    lem_ratpend = 1;
    os88_onwake(lem_win);
    lem_sel = 0;
    lem_prog[0] = -1;
    lem_savable = 1;
    h_writes = 0;
    h_wakes = 0;
    lem_play(lem_win);
    if (h_writes != 0)
        fail("Play wrote the progress file with the gfx lock held");
    if (lem_prog[0] != -1)
        fail("Play credited a level that has not been played - progress is "
             "recorded by the RESULT (SPEC.md 116.8), not by opening the "
             "preview screen");
    if (lem_progwrite)
        fail("Play posted a progress WRITE for a level that has not been "
             "played");
    os88_onwake(lem_win);
    if (h_writes != 0)
        fail("a wake wrote a progress file no result asked for");
    lem_back(lem_win);
    glass_clear();
    repaint("the list");

    /* --- A PROGRESS WRITE THAT FAILS IS SAID, AND TAKEN BACK (wave 4's
     * second review): a replace needs a free cluster before it frees the old
     * one, and a full or protected disk refuses it. The wake used to drop
     * the status while the glass showed the level as recorded. Now the record
     * goes back to the file's, the row greys, and the toast is its fact. */
    lem_savable = 1;
    lem_prog[1] = 2;
    lem_pw_rat = 1;
    lem_pw_lvl = 5;
    lem_progwrite = 1;
    h_write_fail = 1;
    h_toasts = 0;
    os88_onwake(lem_win);
    h_write_fail = 0;
    printf("  a progress write the disk refused: record %d, savable %d, "
           "%d toast(s)\n", lem_prog[1], lem_savable, h_toasts);
    if (lem_prog[1] != 2)
        fail("a refused progress write left the new level in memory - the "
             "glass shows progress the file does not hold");
    if (lem_savable)
        fail("a refused progress write left Save Progress enabled");
    if (h_toasts != 1 || strcmp(h_lasttoast, lem_str(LEMS_SAV_FAIL)) != 0)
        fail("a refused progress write was not said with its own toast "
             "(LEMS_SAV_FAIL)");
    if (strlen(h_lasttoast) > 24)
        fail("the progress write's toast is longer than TOAST_MAX's 24 - "
             "the kernel cuts it mid-word (kernel/toast.inc)");
    lem_savable = 1;
    lem_progwrite = 1;
    os88_onwake(lem_win);
    if (lem_prog[1] != 5 || h_savn != LEM_SAV_SIZE || h_sav[4 + 1] != 6)
        fail("a progress write the disk took did not record the level");
    lem_prog[1] = -1;
    lem_savable = 1;
    glass_clear();
    repaint("the list");

    /* --- A BAND TOO BIG FOR THE BUFFER IS A REFUSAL, NOT A TRUNCATION ------
     * os88_file_read() "refuses a short buffer with FERR_BIG and reads
     * nothing", so lem_str_load()'s ceiling is what stands between a grown
     * string band and a directory that indexes strings which are not there.
     * The band is 4,096 today against a 5,120 buffer; this writes one that is
     * neither and checks the guard rather than the arithmetic. */
    mkdir("build", 0777);
    mkdir("build/lemover", 0777);
    {
        FILE *f = fopen("build/lemover/LEMSTR.LEM", "wb");
        int k;

        if (!f)
            fail("could not write the oversize band fixture");
        else {
            fputs("LSTR", f);
            for (k = 4; k < LEM_STRBUF + 512; k++)
                fputc(0, f);
            fclose(f);
            h_fixdir = "build/lemover/";
            if (lem_str_load() != 0)
                fail("a string band LARGER than LEM_STRBUF was accepted - "
                     "os88_file_read() refuses a short buffer with FERR_BIG "
                     "and reads nothing, so what was 'loaded' is whatever the "
                     "buffer held before");
            if (lem_strn != 0)
                fail("the refused band left a string count behind");
            h_fixdir = FIXDIR;
            if (!lem_str_load())
                fail("the fixture band no longer loads after the oversize "
                     "case");
        }
    }

    /* --- the About card is twelve rows or fewer (LESSONS.md 8) ------------- */
    h_about = 0;
    os88_about(lem_win);
    if (h_about == 0)
        fail("About drew no card");
    else {
        for (i = 0; h_about[i]; i++)
            ;
        printf("lemtest: the About card is %d rows (the ceiling is 12)\n", i);
        if (i > 12)
            fail("the About card is over twelve rows - on CGA and Hercules "
                 "that puts its OK button on the DESKTOP");
        if (!strstr(h_about[0], "Lemmings"))
            fail("the About card does not name the product first");
    }

    /* --- ...AND DISMISSING IT DRAWS THE CONTENT BACK ----------------------
     * The card is OPAQUE over its own rect and the shadow knows nothing about
     * it (apps/os88ui.inc's os88ui_about_d), so a dismissal that still trusts
     * the shadow finds every run equal to it and issues ZERO drawing calls -
     * the card then sits on the glass until the next expose. It shipped that
     * way and no screendump of the launcher would ever have shown it, because
     * the shot after the dismissal is taken through a repaint. */
    counters_reset();
    os88_onkey(27, 0, lem_win);
    if (lem_abon)
        fail("Esc did not dismiss the About card");
    if (g_calls == 0)
        fail("dismissing the About card drew NOTHING - the card is opaque "
             "over its own rect and stays on the glass");
    if (g_cells < lem_cols)
        fail("dismissing the About card drew fewer cells than one row - the "
             "shadow was still trusted somewhere");
    /* ...AND IT FILLED FIRST. Re-lettering every cell leaves the card's frame
     * and drop shadow in the one-pixel LEADING between rows, which no font_run
     * of ours can reach - a dotted ghost of the border with every character
     * correct around it (build/port-shots/w2-dismiss-zoom.png, before this). */
    if (g_fills == 0)
        fail("dismissing the About card re-lettered the cells but never "
             "white-filled the content - the card's frame survives in the "
             "one-pixel leading between the text rows");
    audit_once("the About card dismissed");
    audit_inside("the About card dismissed");
    audit_shadow("the About card dismissed");
    glass_clear();
    repaint("the list");

    /* --- the preview's third line is a PERCENTAGE (Lemmix Preview.pas:275) - */
    if (!glass_has("% To Be Saved"))
        fail("the '%s To Be Saved' line is not a percentage - Lemmix computes "
             "Percentage(LemmingsCount, RescueCount) and LemmingsPercentages "
             "is in TMiscOptions.DEFAULT");

    /* --- the Mode row greys with fsx_caps' own bit (SPEC.md 47 rule 4) ----- */
    h_caps = (1 << OS88_FSXM_CGA320);   /* an EGA: the CGA-compatible modes */
    h_vidkind = OS88_VID_EGA;
    lem_mode_ok = lem_probe_mode(lem_win);
    if (lem_mode_ok)
        fail("mode 0Dh was claimed on a display whose caps do not carry it");
    if (!lem_state_grey(LEM_ROW_MODE))
        fail("the Mode row does not grey on a display that cannot set 0Dh");
    if (lem_state_fact(LEM_ROW_MODE) != LEMS_GREY_MODE)
        fail("an EGA's greyed Mode row does not name the EGA's own fact");

    /* THE FACT FOLLOWS THE CARD IN FRONT OF THE READER (SPEC.md 47). The one
     * table cell was written for an EGA - "a sixteen-colour card gets four
     * colours" - and a CGA is not a sixteen-colour card. */
    h_caps = (1 << OS88_FSXM_CGA320) | (1 << OS88_FSXM_CGA640);
    h_vidkind = OS88_VID_CGA;
    lem_mode_ok = lem_probe_mode(lem_win);
    if (lem_state_fact(LEM_ROW_MODE) != LEMS_GREY_MODE_CGA)
        fail("a CGA's greyed Mode row names the EGA's fact");
    h_caps = (1 << OS88_FSXM_HERC);
    h_vidkind = OS88_VID_HERC;
    lem_mode_ok = lem_probe_mode(lem_win);
    if (lem_state_fact(LEM_ROW_MODE) != LEMS_GREY_MODE_HERC)
        fail("a Hercules' greyed Mode row names the EGA's fact");

    /* ...and W_ONRESIZE re-probes, because it is delivered on a change of
     * ADAPTER KIND (SPEC.md 39.16.3.3): a launcher dragged from the VGA onto
     * the Hercules on vm/xt-multimon used to keep the answer it was given at
     * launch and advertise a mode that display would refuse. */
    h_caps = (1 << OS88_FSXM_VGA0D);
    h_vidkind = OS88_VID_VGA;
    lem_mode_ok = 0;
    os88_onresize(h_w, h_h, lem_win);
    if (!lem_mode_ok)
        fail("W_ONRESIZE did not re-ask fsx_caps - the Mode row keeps the "
             "answer the display it was LAUNCHED from gave");
    lem_mode_ok = lem_probe_mode(lem_win);
    if (!lem_mode_ok)
        fail("mode 0Dh was refused on a display whose caps carry it");
    glass_clear();
    repaint("back on a VGA");

    /* --- the progress dive costs NO REMOUNTS -------------------------------
     * os88_file_goto() is documented as "A REMOUNT: real floppy I/O" and this
     * dive made four of them, on a path that used to run inside W_PAINT.
     * os88_file_goto_q_mark() is a word inside the volume we are already on. */
    h_gotos = 0;
    lem_savable = ovl_progress_read();
    if (h_gotos != 0)
        fail("the progress dive still uses os88_file_goto() - that call is a "
             "REMOUNT and this walk makes four of them");

    /* --- Save Progress greys when the path refuses ------------------------- */
    h_have_appdata = 0;
    lem_savable = ovl_progress_read();
    if (lem_savable)
        fail("Save Progress did not notice that SYSTEM/APPDATA is absent");
    if (!lem_state_grey(LEM_ROW_SAVE))
        fail("Save Progress does not grey when the write path refuses");
    h_have_appdata = 1;
    lem_savable = ovl_progress_read();

    /* ======================================================================
     * WAVE 2: THE RASTER (SPEC.md 116.4)
     *
     * Everything above is about the launcher's glass. Everything here is about
     * the LEVEL's, and it is driven against hosttest/lemraster.c's model of
     * the mask, the world and the screen - which does what the assembly does
     * rather than refusing, so what these rows measure is the real path
     * (LESSONS.md 7).
     * ====================================================================*/
    h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_CGA320);
    h_vidkind = OS88_VID_VGA;
    lem_mode_ok = lem_probe_mode(lem_win);
    lem_rating = 0;
    lem_ratpend = 0;
    lem_rating_load(0);
    lem_sel = 0;

    /* --- the claims, and MEM_MAX ------------------------------------------
     * SPEC.md 116.6.1: claim RECORDS are a budget too, MEM_MAX is 20 on a
     * kern_small machine and it is SYSTEM-WIDE. */
    if (!ovl_level_load(0))
        fail("the level's banks did not load off the fixture");
    printf("  claims: %d live, %d peak, %d KB of the harness's %d; the "
           "arithmetic the refusal quotes is %d KB\n",
           h_claims_now, h_claims_peak, h_mem_kb_used, H_MEM_KB, ovl_need_kb());
    if (h_claims_peak > 6)
        fail("more than six claim records - SPEC.md 116.6.1 says three on a "
             "VGA and five on the two 1bpp adapters, and MEM_MAX is 20 on a "
             "kern_small machine SYSTEM-WIDE");
    if (ovl_need_kb() != h_mem_kb_used)
        fail("the KB the refusal quotes is not the KB actually claimed - a "
             "sentence that is wrong is worse than no sentence (SPEC.md 47)");

    /* --- the composer puts the terrain where the RECORD says ---------------
     * The fixture's level 0 carries two terrain entries, both graphic 0 - the
     * 32x16 slab whose four planes are all 0xFF - at world (80, 120) and
     * (240, 120). The coordinates are rebuilt HERE from the format document
     * rather than read out of the program: x = ((v>>16) & 0x0FFF) - 16 and
     * y = ((v>>7) & 0x1FF) - 4. */
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);
    lem_r_tsetup(lem_cl_t);
    lem_world_clear(1);
    if (!lem_compose_level(0))
        fail("the fixture's first level did not compose");
    {
        int bad = 0, x, y, want, got;

        /* the fixture's level 0 (tools/os88lem.py FIXTURE_LEVELS): a floor
         * of twenty 32x16 slabs at y = 120 from x = 0 to 639, a 16x64 wall
         * at (560, 56) and a 16x16 block at (200, 104) - rebuilt HERE from
         * that table, not read out of the program */
        for (y = 50; y < 140; y++)
            for (x = 0; x < 700; x++) {
                want = (y >= 120 && y < 136 && x < 640) ||
                       (x >= 560 && x < 576 && y >= 56 && y < 120) ||
                       (x >= 200 && x < 216 && y >= 104 && y < 120);
                got = lem_has_pixel(x, y);
                if (want != got)
                    bad++;
            }
        if (bad)
            fail("the composed terrain is not where the level record says "
                 "(the floor, the wall and the block of fixture level 0)");
        printf("  composer: %d world pixels drawn, %d mask pixels checked, "
               "%d wrong\n", h_r_worldpix, 90 * 700, bad);
    }

    /* --- the batched probe answers what four single probes do -------------
     * lem_probe4() is the one optimisation the plan named before it was
     * measured (lemmask.inc's header), and the only thing that could make it
     * worth having is that it answers the SAME. */
    {
        static const char off4[8] = { 0, 0, 1, 0, 2, 0, 3, 0 };
        int x, y, m, k, bad = 0;

        for (y = 118; y < 138; y += 3)
            for (x = 70; x < 290; x += 5) {
                m = lem_probe4(x, y, off4);
                for (k = 0; k < 4; k++)
                    if (((m >> k) & 1) != lem_has_pixel(x + k, y))
                        bad++;
            }
        if (bad)
            fail("lem_probe4() and lem_has_pixel() disagree - the batched "
                 "probe is the hottest call in the game and answers for four");
    }

    /* --- the first frame, and then a frame that changes nothing ------------
     * The written-twice counter is what makes PERFORMANCE.md's double-draw
     * flash visible at all: it never shows in a screendump. The FIRST frame
     * writes the panel and then the status line over it, which is one pixel
     * written twice ON PURPOSE (the panel's own bitmap is what the status line
     * is lettered over), so the assertion is on the PER-FRAME path - which is
     * where a defect would live and where it would be paid every tick. */
    /* THE LVL WORD IS THE VIEW'S LEFT EDGE (Lemmix GameScreen.Player.pas:857,
     * lemmings_3ds import_level.c:510-527) and the port opened half a screen
     * left of it until this wave's review. The harness asserts the CONVENTION,
     * from the fixture's own number, so the centring cannot come back. */
    lem_view = 0;
    lem_view_move(lem_ent_u16(0, LEM_E_STARTX));
    lem_view = (lem_view + 4) & ~7;
    {
        int want = lem_ent_u16(0, LEM_E_STARTX);

        if (want > LEM_WORLD_W - 320)
            want = LEM_WORLD_W - 320;
        want = (want + 4) & ~7;
        if (lem_view != want)
            fail("the view did not open at the level's own start x - the LVL "
                 "record's 0x0018 is the LEFT EDGE, not the centre");
    }
    h_r_reset();
    ovl_frame_panel();
    /* THE PANEL AND A WORD BEFORE THE 25-SECOND WAIT (SPEC.md 116.7.1
     * conclusion 2). Inside the bracket nothing can be said - every kernel
     * slot is refused - so LOADING is lettered in the game's own font, on the
     * panel, before the compose starts, and taken down with the first frame.
     * The template's own first fourteen columns are all '.', which is
     * lem_f_index's fourth arm and draws a BLACK cell, so ink there is the
     * word and nothing else. */
    if (!panel_ink(0, 14))
        fail("nothing was said before the compose - a level is ~25 s on an XT "
             "and the bracket refuses every kernel slot, so the panel is the "
             "only place a word can go");
    if (h_r_presents == 0)
        fail("the panel and the word were never presented - on the two shadow "
             "backends they went into the shadow and the glass is still the "
             "BIOS's black");
    lem_mx = 7;                     /* the pointer in the top left corner, so
                                     * its own box is excluded from the
                                     * written-twice counts below */
    lem_my = 7;
    ovl_frame_first(0);
    if (panel_ink(0, 14))
        fail("LOADING is still on the status line after the first frame");
    printf("  first frame: %d screen pixels, %d presents, %d written twice "
           "(the status line over the panel)\n",
           h_r_pixels, h_r_presents, h_twice());
    if (h_r_pixels == 0)
        fail("the first frame drew nothing at all");
    if (h_r_presents == 0)
        fail("the first frame never presented");

    ovl_tick_draw(0, 0);            /* the first tick puts the pointer on */
    h_r_reset();
    ovl_tick_draw(0, 0);
    printf("  idle frame: %d screen pixels, %d presents, %d written twice\n",
           h_r_pixels, h_r_presents, h_twice());
    if (h_twice() != 0)
        fail("a frame that changed nothing wrote a pixel twice - that is "
             "PERFORMANCE.md's double-draw flash, and no screendump shows it");
    if (h_r_pixels != 0)
        fail("a frame that changed nothing still wrote screen pixels - the "
             "scroll is four `out`s on VGA and a no-op when the view has not "
             "moved (lemblit.inc)");

    /* --- ...and a frame that scrolls --------------------------------------
     * NOTHING IS WRITTEN TWICE, INCLUDING THE VIEW RECTANGLE, and this row
     * used to allow 2*(LEM_VR_W + LEM_MM_H) because it was: the whole old
     * outline was erased and the whole new one drawn, so on a one-cell step 84
     * of the 86 cells took two writes - PERFORMANCE.md's double-draw flash on
     * the rectangle's top and bottom edges, every fourth tick of a scroll.
     * ovl_view_rect() now erases only what the new outline will not cover, so
     * the bound is ZERO and a real double-draw anywhere on the screen has
     * nothing to hide behind. */
    {
        int k, worst = 0, moved = 0, wcells = 0, wrects = 0;

        for (k = 0; k < 8; k++) {
            h_r_reset();
            lem_view_move(4);
            ovl_tick_draw(0, 0);
            if (h_twice_nocur() > worst)
                worst = h_twice_nocur();
            if (h_r_pixels > wcells)
                wcells = h_r_pixels;
            if (h_r_rects > wrects)
                wrects = h_r_rects;
            if (h_r_pixels > 1000)
                moved++;
            if (h_r_hwpan == 0 && k == 3)
                fail("the fourth 4-pixel step did not reach the CRTC at all");
        }
        printf("  eight scrolled frames: worst %d screen pixels, worst %d "
               "written twice (must be 0), worst %d cells moved in %d "
               "lem_r_rect calls\n",
               h_r_pixels, worst, wcells, wrects);
        /* THE CALL COUNT IS A COST (SPEC.md 116.7.1: 137 us of cdecl against
         * 153 us a plot inside one call), so the erase coalesces RUNS of one
         * colour out of the minimap's shadow. A cell per call would be 38. */
        if (wrects > 24)
            fail("the view rectangle's move took more lem_r_rect calls than "
                 "its runs - the erase is coalescing nothing and the cdecl "
                 "boundary is three quarters of every cell");
        if (worst != 0)
            fail("a scrolling frame wrote a pixel twice - the view rectangle "
                 "erases only what the new outline will not cover, so this is "
                 "the terrain or the panel being drawn twice, which is "
                 "PERFORMANCE.md's double-draw flash and no screendump shows "
                 "it");
        /* ...and the rectangle's own move is BOUNDED. Both columns erased,
         * both drawn, and one cell of each of the top and bottom rows either
         * way: 4*(LEM_MM_H - 2) + 4 for a one-cell step. */
        if (wcells > 4 * (LEM_MM_H - 2) + 4 + 2 * 14 * 14)
            fail("the view rectangle moved more cells than its own two "
                 "columns and the leading cells of its two rows - the whole "
                 "outline is being redrawn again");
        if (moved != 0)
            fail("a VGA scroll wrote screen pixels beyond the pointer and the "
                 "view rectangle - it is four `out`s to the start address and "
                 "the pel pan, and nothing else");
    }

    /* --- ...and the same frame on a CGA, where the scroll IS a copy --------
     * The two 1bpp backends have no hardware pan, so the identical frame costs
     * a 12,800-byte windowed compose and a blit of the rows that changed
     * (lemblit.inc). The point of running it here is that the ROW is the same
     * C: lemdraw.c never asks which card it is on. */
    /* THE C HAS TO BELIEVE IT TOO, and until this round it did not: the arm
     * set the RASTER's kind and left lem_kind_id() answering VGA, so the
     * "CGA" frame ran C that thought it was on a VGA. Nothing depended on it
     * while lemdraw.c never asked which card it was on; §116.4.4 rule 5's
     * minimap class and ovl_frame_panel()'s scroll guard both ask now, and
     * this arm caught the second of them the hour it was written. */
    h_caps = (1 << OS88_FSXM_CGA320);
    lem_caps = h_caps;
    lem_vidkind = OS88_VID_CGA;
    lem_r_setup(LEM_RKIND_CGA, 0xB800, 0x1000, 0x1000);
    h_r_reset();
    lem_view = 0;
    lem_view_move(lem_ent_u16(0, LEM_E_STARTX));
    lem_view = (lem_view + 4) & ~7;
    /* THE WHOLE SEQUENCE AND IN ORDER, because the order is what the review
     * defect was about: clear, panel + LOADING + present, compose, first
     * frame. Composing once for the VGA arm and reusing it here would let a
     * stale shadow pass. */
    lem_world_clear(1);
    ovl_frame_panel();
    lem_compose_level(0);
    ovl_frame_first(0);
    frame_shows_world("first frame, CGA");
    printf("  first frame, CGA: %d screen pixels, %d presents\n",
           h_r_pixels, h_r_presents);
    h_r_reset();
    lem_view_move(4);
    ovl_tick_draw(0, 0);
    printf("  scrolled frame, CGA: %d screen pixels, %d written twice\n",
           h_r_pixels, h_twice_nocur());
    if (h_r_pixels < 320 * 160)
        fail("a CGA scroll did not compose the whole 320x160 window - there "
             "is no hardware pan on that card");
    if (h_twice_nocur() != 0)
        fail("a CGA scrolling frame wrote a pixel twice");
    /* ...and a ONE-PIXEL step (Ctrl+arrow) inside the card's 4-pixel snap
     * scrolls nothing, so it must redraw nothing (wave 4 review: every
     * sprite dropped, drawn again and presented for no pixel) */
    {
        int j, cd, v0 = lem_view & ~15;     /* one minimap step: vr stays */

        lem_view = v0;
        lem_mx = 160;                       /* the pointer lit in the world */
        lem_my = 80;
        ovl_tick_draw(0, 0);
        lem_c_sprites = 0;
        cd = h_r_curdraws;
        for (j = 0; j < 3; j++) {
            lem_view_move(1);
            ovl_tick_draw(0, 0);
        }
        printf("  three 1-px steps inside the CGA snap: %d sprite draws, "
               "%d pointer redraws\n", lem_c_sprites, h_r_curdraws - cd);
        if (lem_c_sprites != 0 || h_r_curdraws != cd)
            fail("a sub-snap scroll on CGA redrew and showed nothing "
                 "(ovl_scroll_due)");
        lem_view_move(1);
        ovl_tick_draw(0, 0);
        if (lem_scrolled != v0 + 4)
            fail("the fourth 1-px step on CGA did not scroll");
    }
    h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_CGA320);
    lem_caps = h_caps;
    lem_vidkind = OS88_VID_VGA;
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);

    /* --- THE EXPLOSION'S SPRAY, on both kinds of card (wave 6) ---------- */
    spray_run("VGA", 0);
    spray_run("VGA", 1);
    spray_area();
    h_caps = (1 << OS88_FSXM_CGA320);
    lem_caps = h_caps;
    lem_vidkind = OS88_VID_CGA;
    lem_r_setup(LEM_RKIND_CGA, 0xB800, 0x1000, 0x1000);
    spray_run("CGA", 0);
    spray_run("CGA", 1);
    h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_CGA320);
    lem_caps = h_caps;
    lem_vidkind = OS88_VID_VGA;
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);

    /* --- A PLAYED STRETCH ON VGA: three hundred game frames, drawn ---------
     * Wave 3's review found the live frame erasing and redrawing straight on
     * the displayed buffer: the pointer off for the whole of every frame
     * (seventeen blinks a second), and every changed object put back as BARE
     * TERRAIN on the glass before the object went over it. So: the glass
     * equals the world after every tick (a compose that copied a stale byte
     * on would show here), no VGA background rectangle is ever written onto
     * the glass outside a compose, the minimap's cleared cells are all drawn
     * by the frame's end, and a pointer parked away from everything is NOT
     * taken off and put back on frames that never write under it. */
    {
        int f, x, y, bad, cur0, curdraws;

        h_r_reset();
        lem_view = 0;
        lem_view_move(lem_ent_u16(0, LEM_E_STARTX));
        lem_view = (lem_view + 4) & ~7;
        ovl_level_load(0);
        lem_world_clear(1);
        ovl_frame_panel();
        lem_compose_level(0);
        lem_mx = 316;               /* the top right corner of the view */
        lem_my = 8;
        ovl_frame_first(0);
        ovl_tick_draw(0, 0);        /* the pointer goes on */
        h_r_bgglass = 0;
        h_r_direct = 0;
        h_r_latches = 0;
        cur0 = h_r_curdraws;
        bad = 0;
        h_r_comps = 0;
        for (f = 0; f < 300 && !lem_finished; f++) {
            if (f == 120 && lem_released > 0) {
                ovl_select_skill(LB_DIGGER);    /* destruction to compose */
                lem_hit_n = 1;
                lem_hit_l1 = 0;
                lem_hit_l2 = -1;
                ovl_assign();
            }
            lem_game_frame();
            ovl_tick_draw(1, 0);
            for (y = 0; y < H_VIEW_H; y++)
                for (x = 0; x < H_SCR_W; x++)
                    if (h_screen[y][x] != h_world[y][lem_view + x])
                        bad++;
            bad += h_cur_missing();
        }
        curdraws = h_r_curdraws - cur0;
        printf("  played on VGA: %d frames, %d pointer redraws, %d bands "
               "and %d rectangles composed off the glass, %d draws onto it, "
               "%d glass/world mismatches\n", f, curdraws, h_r_comps,
               h_r_latches, h_r_direct, bad);
        if (h_r_comps == 0)
            fail("the played stretch composed nothing - no object moved and "
                 "nothing was dug, so the compose went untested");
        if (lem_released == 0)
            fail("the played stretch released nobody - it tests nothing");
        if (bad)
            fail("after a drawn game frame the glass is not the world - a "
                 "compose copied on a byte it never composed, or a sprite "
                 "restore left the wrong one");
        if (h_r_bgglass)
            fail("a VGA background rectangle went straight onto the glass - "
                 "bare terrain on screen until the object is drawn over it "
                 "(wave 3's review: compose it off the glass)");
        if (h_r_direct)
            fail("a VGA game frame drew a sprite, an object or the background "
                 "straight onto the glass - every lemming it covers is off the "
                 "glass until it is drawn again (wave 3's second review: the "
                 "frame is composed and latched on, ovl_vframe)");
        if (lem_mmpend)
            fail("a minimap cell the mechanics cleared was never drawn");
        if (curdraws > f / 4)
            fail("a pointer parked away from every lemming was taken off and "
                 "put back on most game frames - seventeen blinks a second on "
                 "the XT (ovl_cur_hit)");

        /* ...and a pointer PARKED on a lemming is never taken off by a
         * game frame: the compose brings its save-under up to date and draws
         * it into the scratch, so the latch puts it straight back. Wave 3's
         * first fix took it off for the whole frame whenever a lemming came
         * near it - which is exactly while the player aims. */
        {
            int k, i, near = 0;

            for (k = 0; k < lem_released; k++)
                if (!(lem_lm[k].fl & LF_GONE) &&
                    lem_lm[k].x - lem_view >= 8 &&
                    lem_lm[k].x - lem_view < 312)
                    break;
            if (k < lem_released) {
                lem_mx = lem_lm[k].x - lem_view;
                lem_my = lem_lm[k].y - 6;
                ovl_tick_draw(0, 0);    /* it moves there */
                cur0 = h_r_curdraws;
                for (f = 0; f < 40 && !lem_finished; f++) {
                    lem_game_frame();
                    ovl_tick_draw(1, 0);
                    if (h_cur_missing())
                        fail("a game frame rubbed out part of the parked "
                             "pointer - latched over it with nothing drawn "
                             "into the scratch (lem_r_cdraw)");
                    for (i = 0; i < lem_released; i++)
                        if (!(lem_lm[i].fl & LF_GONE) &&
                            lem_lm[i].x - lem_view >= lem_mx - 20 &&
                            lem_lm[i].x - lem_view <= lem_mx + 20 &&
                            lem_lm[i].y >= lem_my - 20 &&
                            lem_lm[i].y <= lem_my + 30)
                            near++;
                }
                printf("  a parked pointer: %d lemming-frames under it, %d "
                       "pointer redraws in %d frames\n", near,
                       h_r_curdraws - cur0, f);
                if (near < 10)
                    fail("the parked pointer had nobody under it - the row "
                         "tests nothing");
                if (h_r_curdraws - cur0 > 2)
                    fail("a pointer parked over walking lemmings was taken "
                         "off and put back by game frames - it blinks while "
                         "the player aims (lem_r_csave / lem_r_cdraw)");
            } else {
                fail("no lemming in view to park the pointer on");
            }
        }

        /* ...and a PAUSED pan shows the lemmings it reaches (wave 3's third
         * review). VRAM holds lemmings only within the view +- 32 (ovl_sbox's
         * margin cull), and a scroll with no game frame used to move the
         * Start Address and draw nothing - terrain with no lemmings, while the
         * status line named the one under the pointer. Cull one away, pause,
         * pan to it, and require it composed. */
        {
            int k, gx, was, pans;

            for (k = 0; k < lem_released; k++)
                if (!(lem_lm[k].fl & LF_GONE))
                    break;
            if (k < lem_released) {
                was = lem_view;
                lem_view = lem_lm[k].x < LEM_WORLD_W / 2
                           ? LEM_WORLD_W - LEM_VIEW_W : 0;
                lem_game_frame();
                ovl_tick_draw(1, 0);        /* it leaves VRAM */
                if (lem_obox[k * 4] != 0xFF)
                    fail("the paused-pan row could not cull its lemming - it "
                         "tests nothing");
                ovl_toggle_pause();
                gx = lem_lm[k].x - LEM_VIEW_W / 2;
                if (gx < 0)
                    gx = 0;
                if (gx > LEM_WORLD_W - LEM_VIEW_W)
                    gx = LEM_WORLD_W - LEM_VIEW_W;
                pans = 0;
                while (lem_view != gx && pans < 200) {
                    int cross, meets, c0, j, q;

                    /* the most a tick moves: LEM_SCROLL_MAX */
                    lem_view_move(gx > lem_view
                                  ? (gx - lem_view > LEM_SCROLL_MAX
                                     ? LEM_SCROLL_MAX : gx - lem_view)
                                  : (lem_view - gx > LEM_SCROLL_MAX
                                     ? -LEM_SCROLL_MAX : gx - lem_view));
                    /* THE COST IS THE CROSSERS' (wave 4 review): the
                     * lemmings whose margin membership changes, and those
                     * that touch their rectangles - never the crowd */
                    cross = 0;
                    for (j = 0; j < lem_released; j++)
                        if ((lem_obox[j * 4] == 0xFF) ==
                            ovl_inmargin(&lem_lm[j]))
                            cross++;
                    c0 = h_r_comps;
                    ovl_tick_draw(0, 0);
                    meets = 0;
                    for (j = 0; j < lem_released; j++)
                        if (lem_obox[j * 4] != 0xFF)
                            for (q = 0; q < lem_nrc; q++)
                                if (lem_obox[j * 4] <= lem_rc[q * 4 + 1] &&
                                    lem_rc[q * 4] <= lem_obox[j * 4 + 1] &&
                                    lem_obox[j * 4 + 2] <= lem_rc[q * 4 + 3] &&
                                    lem_rc[q * 4 + 2] <= lem_obox[j * 4 + 3]) {
                                    meets++;
                                    break;
                                }
                    if (cross == 0 && (h_r_comps != c0 || lem_c_sprites))
                        fail("a paused pan step that took no lemming across "
                             "the margin composed anyway (ovl_vframe(0))");
                    if (cross && lem_c_sprites > 4 * meets) {
                        printf("  pan step: %d crossed, %d meet a rectangle, "
                               "%d sprites drawn\n", cross, meets,
                               lem_c_sprites);
                        fail("a paused pan step composed lemmings that meet "
                             "no rectangle - one crossing recomposed the "
                             "crowd (ovl_vframe(0)'s band loop)");
                    }
                    pans++;
                }
                /* ...and an 8-pixel pan that takes nobody across makes no
                 * raster call at all */
                {
                    int j, d, cross = 1, c0, vw = lem_view;

                    for (d = 0; d < 40 && cross; d++) {
                        lem_view = vw;
                        lem_view_move(d & 1 ? -8 * (d / 2 + 1)
                                            : 8 * (d / 2 + 1));
                        cross = 0;
                        for (j = 0; j < lem_released; j++)
                            if ((lem_obox[j * 4] == 0xFF) ==
                                ovl_inmargin(&lem_lm[j]))
                                cross++;
                    }
                    lem_view = vw;
                    if (!cross) {
                        d--;
                        lem_view_move(d & 1 ? -8 * (d / 2 + 1)
                                            : 8 * (d / 2 + 1));
                        c0 = h_r_comps;
                        ovl_tick_draw(0, 0);
                        printf("  an 8-px-step pan to %d with nobody "
                               "crossing: %d composes, %d sprites\n",
                               lem_view, h_r_comps - c0, lem_c_sprites);
                        if (h_r_comps != c0 || lem_c_sprites)
                            fail("a paused pan with no lemming crossing the "
                                 "margin made raster calls");
                        lem_view = vw;
                        ovl_tick_draw(0, 0);
                    }
                }
                printf("  a paused pan: %d ticks to lemming %d at x %d, its "
                       "box %s\n", pans, k, lem_lm[k].x,
                       lem_obox[k * 4] == 0xFF ? "NOT composed" : "composed");
                if (lem_obox[k * 4] == 0xFF)
                    fail("a paused pan reached a lemming and did not compose "
                         "it - terrain with no lemmings on the glass "
                         "(ovl_tick_draw's scroll arm, ovl_vframe(0))");
                for (y = 0; y < H_VIEW_H; y++)
                    for (x = 0; x < H_SCR_W; x++)
                        if (h_screen[y][x] != h_world[y][lem_view + x])
                            bad++;
                if (bad)
                    fail("after a paused pan the glass is not the world");
                /* ...ONE CROSSER IN A CROWD: the crowd parked on the same
                 * rows at the view's left, one lemming just past the right
                 * margin, and an 8-pixel pan brings it in. Only it (and its
                 * digit) is composed - the crowd meets no rectangle */
                {
                    static struct lem_l keep[LEM_MAXLEM];
                    int j, n = 0, c, vw = lem_view, cy = lem_lm[k].y;
                    int rel = lem_released;

                    memcpy(keep, lem_lm, sizeof keep);
                    if (lem_view > LEM_WORLD_W - LEM_VIEW_W - 16)
                        lem_view = LEM_WORLD_W - LEM_VIEW_W - 16;
                    /* the fixture has released few: the crowd is copies of
                     * lemming k, put back afterwards */
                    while (lem_released < rel + 6 && lem_released < LEM_MAXLEM) {
                        lem_lm[lem_released] = lem_lm[k];
                        lem_obox[lem_released * 4] = 0xFF;
                        lem_released++;
                    }
                    for (j = 0; j < lem_released; j++) {
                        if (lem_lm[j].fl & LF_GONE)
                            continue;
                        lem_lm[j].y = cy;
                        lem_lm[j].x = j == k ? lem_view + LEM_VIEW_W + 36
                                             : lem_view + 40 + 6 * n++;
                    }
                    ovl_tick_draw(1, 0);
                    lem_view_move(8);
                    ovl_tick_draw(0, 0);
                    c = lem_c_sprites;
                    printf("  one crosser beside a crowd of %d: %d sprite "
                           "draws\n", n, c);
                    if (n < 3)
                        fail("the one-crosser row has no crowd - it tests "
                             "nothing");
                    if (lem_obox[k * 4] == 0xFF)
                        fail("the one-crosser row's lemming was not composed");
                    if (c > 2)
                        fail("one lemming crossing the margin recomposed "
                             "the crowd's sprites (ovl_vframe(0)'s band "
                             "loop)");
                    /* the copies off the glass first, while they are
                     * still records, then the pool as it was */
                    for (j = rel; j < lem_released; j++)
                        lem_lm[j].fl |= LF_GONE;
                    ovl_tick_draw(1, 0);
                    lem_released = rel;
                    memcpy(lem_lm, keep, sizeof keep);
                    lem_view = vw;
                    ovl_tick_draw(1, 0);
                }
                ovl_toggle_pause();
                lem_view = was;
                lem_game_frame();
                ovl_tick_draw(1, 0);
            } else {
                fail("no lemming to pan to");
            }
        }

        /* ...and a pointer STEERED through the crowd for sixty frames leaves
         * nothing behind: a stale save-under - not brought up to date from
         * the scratch before the latch, then restored - is the defect to
         * look for. With the pointer off, the view must be exactly what a
         * fresh background, the objects and every lemming drawn over them
         * make. */
        {
            static unsigned char before[H_VIEW_H][H_SCR_W];
            int stale = 0, pairs = 0, moves = 0;

            int k, near = 0;

            for (f = 0; f < 60 && !lem_finished; f++) {
                /* ONTO a live lemming, wobbling, so the box it is lit at is
                 * written under on most frames */
                for (k = 0; k < lem_released; k++)
                    if (!(lem_lm[k].fl & LF_GONE) &&
                        lem_lm[k].x - lem_view >= 8 &&
                        lem_lm[k].x - lem_view < 312)
                        break;
                if (k < lem_released) {
                    lem_mx = lem_lm[k].x - lem_view + (f % 5) - 2;
                    lem_my = lem_lm[k].y - 6 + (f % 3);
                    near++;
                }
                lem_game_frame();
                ovl_tick_draw(1, 0);
                if (h_cur_missing())
                    stale += 1000;  /* reported with the leftovers below */
                /* ...and a move on a tick with NO game frame - most aiming
                 * ticks on an XT - rides a compose too: no glass pair */
                {
                    int cd = h_r_curdraws;

                    lem_mx += (f & 1) ? 2 : -2;
                    ovl_tick_draw(0, 0);
                    if (h_cur_missing())
                        stale += 1000;
                    if (lem_my + 7 <= LEM_VIEWH_PX && h_r_curdraws != cd)
                        pairs++;
                    moves++;
                }
            }
            printf("  a pointer moved between frames: %d of %d moves taken "
                   "off and put back on the glass\n", pairs, moves);
            if (pairs)
                fail("a pointer move with no game frame erased and redrew "
                     "the pointer on the glass - rule 2 on the XT's aiming "
                     "ticks (ovl_vframe(0) with lem_cmv)");
            if (near < 30)
                fail("the steered pointer found nobody to steer through - "
                     "the row tests nothing");
            ovl_cursor_off();
            for (y = 0; y < H_VIEW_H; y++)
                for (x = 0; x < H_SCR_W; x++)
                    before[y][x] = h_world[y][lem_view + x];
            ovl_bg_redraw(lem_view, 0, LEM_VIEW_W, H_VIEW_H, 0);
            ovl_sprites_all();
            for (y = 0; y < H_VIEW_H; y++)
                for (x = 0; x < H_SCR_W; x++)
                    if (before[y][x] != h_world[y][lem_view + x])
                        stale++;
            printf("  a steered pointer: %d frames over a lemming, %d stale "
                   "pixels left behind\n", near, stale);
            if (stale)
                fail("a pointer steered through the crowd left pixels behind "
                     "- a save-under restored over a background that changed "
                     "under the lit pointer");
        }

        /* ...and a pointer that LEAVES THE WORLD leaves nothing behind
         * (wave 5's verifier, carried finding A of wave 6). The save-under is
         * WHOLE BYTES - three planar bytes a row on VGA, the tail the shift
         * spills into included (lemsprite.inc lb_clipx) - so a move that rode
         * a compose (lem_cmv) has to bring all three up to date, not the
         * two the 14-pixel box covers: a box at x & 7 of 0, 1 or 2 has its
         * tail byte outside the box, the compose left that column of the save
         * as the LAST place saved, and the glass pair that takes the pointer
         * into the panel put it back - an 8x14 block of another place's
         * terrain in the air. Each path: lit by the glass pair over terrain at
         * A, ridden by a compose to B (tail outside the box, over different
         * pixels), then diagonally into the panel - and once more, straight
         * into the bottom-right corner - and the world compared with a fresh
         * one. */
        {
            static unsigned char before[H_VIEW_H][H_SCR_W];
            int ax, ay, bx, by, pass, paths = 0, stale = 0, n, gx, gy, d;
            int wa, wb, rides = 0;

            for (pass = 0; pass < 2; pass++)
                for (ay = 10; ay + 7 + 6 <= LEM_VIEWH_PX - 7 && paths < 2 + 2 * pass;
                     ay += 9)
                    for (ax = 8; ax + 40 < 300 && paths < 2 + 2 * pass; ax += 13) {
                        by = ay + 6;
                        bx = ax + 26;
                        bx += (1 - ((lem_view + bx - 7) & 7) + 8) & 7;
                        if (bx > 300)
                            continue;
                        /* the tail column of B's span differs from what the
                         * same column of A's span holds - so a stale save
                         * shows - and A's span has terrain in it */
                        wa = ((lem_view + ax - 7) & ~7) + 16;
                        wb = ((lem_view + bx - 7) & ~7) + 16;
                        d = 0;
                        n = 0;
                        for (gy = 0; gy < 14; gy++)
                            for (gx = 0; gx < 8; gx++) {
                                if (h_world[ay - 7 + gy][wa + gx] !=
                                    h_world[by - 7 + gy][wb + gx])
                                    d++;
                                if (h_world[ay - 7 + gy][wa + gx])
                                    n++;
                            }
                        if (d < 8 || n < 8)
                            continue;
                        ovl_cursor_off();
                        lem_mx = ax;
                        lem_my = ay;
                        ovl_tick_draw(0, 0);    /* the glass pair: lit at A */
                        lem_mx = bx;
                        lem_my = by;
                        d = h_r_curdraws;
                        ovl_tick_draw(0, 0);    /* rides a compose to B */
                        if (h_r_curdraws == d)
                            rides++;
                        if (pass == 0) {
                            lem_mx = bx + 14;   /* ...diagonally into the panel */
                            lem_my = LEM_VIEWH_PX + 20;
                        } else {
                            lem_mx = LEM_VIEW_W - 1;   /* ...the corner */
                            lem_my = 199;
                        }
                        ovl_tick_draw(0, 0);
                        ovl_cursor_off();
                        for (y = 0; y < H_VIEW_H; y++)
                            for (x = 0; x < H_SCR_W; x++)
                                before[y][x] = h_world[y][lem_view + x];
                        ovl_bg_redraw(lem_view, 0, LEM_VIEW_W, H_VIEW_H, 0);
                        ovl_sprites_all();
                        for (y = 0; y < H_VIEW_H; y++)
                            for (x = 0; x < H_SCR_W; x++)
                                if (before[y][x] != h_world[y][lem_view + x])
                                    stale++;
                        paths++;
                    }
            printf("  a pointer leaving the world: %d paths (world -> panel, "
                   "world -> corner), %d rode a compose, %d stale pixels left "
                   "behind\n", paths, rides, stale);
            if (paths < 4 || rides != paths)
                fail("the leaving-the-world pointer row found no terrain to "
                     "lead it over, or its move to B did not ride a compose - "
                     "the row tests nothing");
            if (stale)
                fail("a pointer that left the world for the panel left a "
                     "block of the wrong save-under behind - the compose "
                     "brought the save up to date over the 14-pixel box and "
                     "not over the whole-byte span the glass pair restores");
        }

        /* ...and the minimap cell a digger clears is OWED, not plotted:
         * ovl_mm_clear() is called from the mechanics, mid-frame, before the
         * pointer's bracket - a plot there went under a lit pointer's
         * save-under and was undone by the restore for good */
        h_r_reset();
        lem_mm[2 * LEM_MM_STRIDE + 11] |= 0x04;     /* cell 93, row 2: far
                                                     * from the view's own
                                                     * outline */
        ovl_mm_clear(93 * LEM_MM_XSTEP, 32, 0);
        if (h_r_rects != 0)
            fail("ovl_mm_clear() plotted a cell from the mechanics");
        if (!lem_mmpend)
            fail("ovl_mm_clear() lost the cell it cleared");
        ovl_mm_dots();
        if (h_r_rects == 0 || lem_mmpend)
            fail("ovl_mm_dots() did not draw the owed cell");
        h_r_reset();
        ovl_mm_clear(93 * LEM_MM_XSTEP, 32, 0);     /* again: already bare */
        if (lem_mmpend)
            fail("ovl_mm_clear() owed a cell whose bit did not change");

        /* --- THE ROWS BELOW WANT LIVE LEMMINGS: the level again, from the
         * top, played until a few are walking */
        lem_view = 0;
        lem_view_move(lem_ent_u16(0, LEM_E_STARTX));
        lem_view = (lem_view + 4) & ~7;
        ovl_level_load(0);
        lem_world_clear(1);
        ovl_frame_panel();
        lem_compose_level(0);
        lem_mx = 316;
        lem_my = 8;
        ovl_frame_first(0);
        ovl_tick_draw(0, 0);
        for (f = 0; f < 140 && !lem_finished; f++) {
            lem_game_frame();
            ovl_tick_draw(1, 0);
        }
        if (lem_released < 2 || lem_finished)
            fail("the second stretch has nobody walking - the rows below "
                 "test nothing");

        /* --- a pointer moved IN THE MIDDLE OF A PART-STEPPED FRAME (more
         * than LEM_STEP_MAX lemmings out, lem_infr): the lemmings already
         * stepped have a new x and frame while their boxes still say where
         * the glass shows them. One whose old box meets the pointer's
         * rectangles must be erased and drawn WHOLE at its new state - drawn
         * clipped to the rectangles over a clipped erasure it is torn
         * between two places under the pointer (wave 4's second review). So:
         * every stepped lemming whose old box met a rectangle has its new box
         * as its box, and both boxes inside one rectangle. */
        {
            static struct lem_l keep[LEM_MAXLEM];
            static unsigned char ob[LEM_MAXLEM * 4];
            int j, k, n = 0, rel = lem_released, met = 0, torn = 0, moved = 0;
            int cy, b0, b1, r0, r1, q, fb, comps, ncas, capc, capf;

            memcpy(keep, lem_lm, sizeof keep);
            for (k = 0; k < lem_released; k++)
                if (!(lem_lm[k].fl & LF_GONE))
                    break;
            if (k >= lem_released)
                fail("the part-stepped row found no lemming to copy");
            cy = lem_lm[k].y;
            while (lem_released < 32 && lem_released < LEM_MAXLEM) {
                lem_lm[lem_released] = lem_lm[k];
                lem_obox[lem_released * 4] = 0xFF;
                lem_released++;
            }
            for (j = 0; j < lem_released; j++) {
                if (lem_lm[j].fl & LF_GONE)
                    continue;
                lem_lm[j].y = cy;
                lem_lm[j].x = lem_view + 40 + 5 * n++;
            }
            ovl_tick_draw(1, 0);            /* every box is the glass's */
            if (!ovl_frame_begin())
                fail("the part-stepped row could not begin a frame");
            lem_frame_chunk(LEM_STEP_MAX);
            if (!lem_infr)
                fail("the part-stepped row's frame finished in one chunk - "
                     "it tests nothing");
            /* the pointer onto a STEPPED lemming whose byte box moved */
            for (j = 0; j < lem_cursor; j++) {
                if (lem_obox[j * 4] == 0xFF || !ovl_sbox(j))
                    continue;
                if (lem_bb0 != lem_obox[j * 4] ||
                    lem_bb1 != lem_obox[j * 4 + 1])
                    break;
            }
            if (j >= lem_cursor)
                fail("no stepped lemming changed its box - the row tests "
                     "nothing");
            lem_mx = (lem_obox[j * 4] << 3) + 4 - lem_view;
            lem_my = cy - 6;
            /* THE BOUND (wave 4's third review): a pointer-only tick over
             * stepped lemmings takes the glass pair and composes NOTHING, so
             * ovl_infr_whole's ride across the crowd cannot start */
            fb = lem_c_cmvfb;
            comps = h_r_comps;
            ovl_tick_draw(0, 0);
            capc = h_r_comps - comps;
            capf = lem_c_cmvfb - fb;
            if (capf != 1 || capc != 0)
                fail("a pointer move over STEPPED lemmings of a part-stepped "
                     "frame rode the compose - unbounded across a crowd "
                     "(lemdraw.c LEM_CMV_CAP)");
            if (lem_curx != lem_mx || lem_cury != lem_my)
                fail("the capped pointer move did not put the pointer where "
                     "the mouse is (the glass pair)");
            /* ...and the NEGATIVE CONTROL: the cap lifted, the same kind of
             * move rides the compose, and ovl_infr_whole turns the stepped
             * lemmings it meets into rectangles - the count the cap exists to
             * hold at zero. It must still draw nobody torn. */
            lem_cmv_cap = 99;
            memcpy(ob, lem_obox, sizeof ob);
            lem_mx += 8;
            ovl_tick_draw(0, 0);
            ncas = lem_c_infr;
            lem_cmv_cap = LEM_CMV_CAP;
            printf("  a pointer over a part-stepped crowd: capped, %d glass "
                   "pair and %d composes; uncapped (negative control), %d "
                   "stepped lemmings ridden into the compose\n",
                   capf, capc, ncas);
            if (ncas == 0)
                fail("the negative control's uncapped pointer compose rode no "
                     "stepped lemming - the cap's row tests nothing");
            for (j = 0; j < lem_cursor; j++) {
                if (ob[j * 4] == 0xFF || !ovl_rc_meets(&ob[j * 4]))
                    continue;
                met++;
                if (!ovl_sbox(j)) {
                    if (lem_obox[j * 4] != 0xFF)
                        torn++;
                    continue;
                }
                if (lem_bb0 != ob[j * 4] || lem_bb1 != ob[j * 4 + 1])
                    moved++;
                if (lem_obox[j * 4] != lem_bb0 ||
                    lem_obox[j * 4 + 1] != lem_bb1 ||
                    lem_obox[j * 4 + 2] != lem_br0 ||
                    lem_obox[j * 4 + 3] != lem_br1) {
                    torn++;
                    continue;
                }
                b0 = ob[j * 4] < lem_bb0 ? ob[j * 4] : lem_bb0;
                b1 = ob[j * 4 + 1] > lem_bb1 ? ob[j * 4 + 1] : lem_bb1;
                r0 = ob[j * 4 + 2] < lem_br0 ? ob[j * 4 + 2] : lem_br0;
                r1 = ob[j * 4 + 3] > lem_br1 ? ob[j * 4 + 3] : lem_br1;
                for (q = 0; q < lem_nrc; q++)
                    if (lem_rc[q * 4] <= b0 && b1 <= lem_rc[q * 4 + 1] &&
                        lem_rc[q * 4 + 2] <= r0 && r1 <= lem_rc[q * 4 + 3])
                        break;
                if (q >= lem_nrc)
                    torn++;
            }
            printf("  a pointer moved mid-frame (%d of %d stepped): %d "
                   "stepped lemmings under it, %d moved, %d torn\n",
                   lem_cursor, lem_released, met, moved, torn);
            if (!met || !moved)
                fail("the mid-frame pointer met no stepped lemming that "
                     "moved - the row tests nothing");
            if (torn)
                fail("a pointer move between the passes of a part-stepped "
                     "frame drew a stepped lemming clipped at its new state "
                     "over its old box (ovl_vframe(0), ovl_infr_whole)");
            lem_frame_chunk(LEM_MAXLEM);
            ovl_frame_end();
            ovl_tick_draw(1, 0);
            for (j = rel; j < lem_released; j++)
                lem_lm[j].fl |= LF_GONE;
            ovl_tick_draw(1, 0);
            lem_released = rel;
            memcpy(lem_lm, keep, sizeof keep);
            ovl_tick_draw(1, 0);
        }

        /* --- THE SHARE OF AIMING TICKS THE CAP SENDS TO THE GLASS PAIR (wave
         * 6, owed by SPEC.md 116.4.6: "lemtest's '1 glass pair' is one tick,
         * not a share"). A crowd of 60 walkers - LEM_STEP_MAX 20, so a frame
         * is three passes and two of them end with lem_infr up - is driven
         * pass by pass the way ovl_play_loop drives it, with the pointer
         * AIMING: on one lemming of the crowd, two pixels further each pass,
         * as a hand does. Every pass whose pointer moved is an aiming tick;
         * one the cap sent to ovl_cursor_off/on is an erase-then-draw of the
         * pointer where the player is looking (rule 2). Twice: the crowd
         * walking nose to tail (6 px apart, a queue at a blocker) and spread
         * out (16 px, a line on the march). */
        {
            static struct lem_l keep[LEM_MAXLEM];
            int sp, j, k, n, cy, t, fb0, moves, pairs, aim, rel = lem_released;

            memcpy(keep, lem_lm, sizeof keep);
            for (k = 0; k < lem_released; k++)
                if (!(lem_lm[k].fl & LF_GONE))
                    break;
            if (k >= lem_released)
                fail("the aiming-share row found no lemming to copy");
            cy = lem_lm[k].y;
            for (sp = 6; sp <= 16; sp += 10) {
                lem_released = rel;
                memcpy(lem_lm, keep, sizeof keep);
                while (lem_released < 60 && lem_released < LEM_MAXLEM) {
                    lem_lm[lem_released] = lem_lm[k];
                    lem_obox[lem_released * 4] = 0xFF;
                    lem_released++;
                }
                n = 0;
                for (j = 0; j < lem_released; j++) {
                    if (lem_lm[j].fl & LF_GONE)
                        continue;
                    lem_lm[j].y = cy;
                    lem_lm[j].x = lem_view + 8 + (sp * n++) % 300;
                }
                ovl_tick_draw(1, 0);
                aim = lem_released / 2;     /* one in the middle of the crowd */
                fb0 = lem_c_cmvfb;
                moves = 0;
                for (t = 0; t < 90; t++) {
                    int drew = 0;

                    if (!lem_infr)
                        ovl_frame_begin();
                    if (lem_infr && lem_frame_chunk(LEM_STEP_MAX)) {
                        ovl_frame_end();
                        drew = 1;
                    }
                    if (lem_lm[aim].fl & LF_GONE)
                        break;
                    lem_mx = lem_lm[aim].x - lem_view + ((t & 1) ? 2 : -2);
                    lem_my = lem_lm[aim].y - 6;
                    if (lem_mx < 8)
                        lem_mx = 8;
                    if (lem_mx > 311)
                        lem_mx = 311;
                    if (lem_mx != lem_curx || lem_my != lem_cury)
                        moves++;
                    {
                        int f0 = lem_c_cmvfb;

                        /* WHAT A DRY-PASS BOUND WOULD SEE (SPEC.md 116.4.6):
                         * a tick the cap sends to the pair is re-run in the
                         * harness with the cap lifted and its cascade
                         * counted - ovl_infr_whole's real growth on that
                         * tick, the number a dry pass would bound */
                        if (getenv("LEMTEST_DRYPASS") && !drew && lem_infr &&
                            ovl_cmv_stepped() > 0) {
                            lem_cmv_cap = 99;
                            ovl_tick_draw(drew, 0);
                            lem_cmv_cap = LEM_CMV_CAP;
                            printf("    dry: tick %d cascade %d\n", t,
                                   lem_c_infr);
                            continue;
                        }
                        ovl_tick_draw(drew, 0);
                        (void)f0;
                    }
                }
                pairs = lem_c_cmvfb - fb0;
                printf("  aiming into a crowd of %d, %d px apart: %d of %d "
                       "aiming ticks took the glass pair (%d%%)\n",
                       lem_released, sp, pairs, moves,
                       moves ? pairs * 100 / moves : 0);
                if (sp == 6)
                    lem_aim_dense = moves ? pairs * 100 / moves : 0;
                else
                    lem_aim_sparse = moves ? pairs * 100 / moves : 0;
                if (moves < 60)
                    fail("the aiming-share row aimed at nobody for long "
                         "enough - it measures nothing");
                lem_frame_chunk(LEM_MAXLEM);
                if (lem_infr)
                    ovl_frame_end();
                for (j = rel; j < lem_released; j++)
                    lem_lm[j].fl |= LF_GONE;
                ovl_tick_draw(1, 0);
            }
            lem_released = rel;
            memcpy(lem_lm, keep, sizeof keep);
            ovl_tick_draw(1, 0);
        }

        /* --- A BAND READ IS HELD INSIDE ITS CLAIM (wave 4's second review):
         * the part count is a LEMMAN byte and the files are the disk's, so
         * ovl_parts_read caps every part at what the claim has left and
         * refuses a count that cannot start inside it. LEMMAIN.LEM is 40,960
         * bytes in the fixture: an 8 KB claim must refuse it, its own 60 KB
         * must take it, and no part count of 0 or past the claim may read */
        {
            int r8, r52, r0, rmany;

            ovl_stem_main();
            r8 = ovl_parts_read(1, lem_cl_main, 8);
            ovl_stem_main();
            r52 = ovl_parts_read(1, lem_cl_main, LEM_KB_MAIN);
            ovl_stem_main();
            r0 = ovl_parts_read(0, lem_cl_main, LEM_KB_MAIN);
            ovl_stem_main();
            rmany = ovl_parts_read(200, lem_cl_main, 1);
            printf("  a band read held to its claim: 8 KB %d, 60 KB %d, "
                   "0 parts %d, 200 parts in 1 KB %d\n", r8, r52, r0, rmany);
            if (r8 || !r52 || r0 || rmany)
                fail("ovl_parts_read read past its claim, or refused a band "
                     "that fits it");
        }

        /* --- a VGA frame that does not scroll does not wait for a retrace:
         * the compose needs none, and the wait was up to 14.3 ms of a 59 ms
         * frame on the XT (wave 3's second review) */
        {
            int t0 = h_fsx_ticks;

            lem_game_frame();
            ovl_tick_draw(1, 0);
            if (h_fsx_ticks != t0)
                fail("a VGA game frame that did not scroll waited for a "
                     "retrace");
        }

        /* --- THE RIGHT-CLICK GLITCH'S LEMMING IS RECORDED ONLY WHERE
         * LEMMIX'S OWN HIT TESTS RUN: at a frame boundary, while paused, and
         * in the assignment. The pointer's per-tick test between frames
         * refreshes the word and the shape and leaves it alone. */
        {
            int k, row, x0, y0;
            struct lem_l *p;

            for (k = 0; k < lem_released; k++) {
                p = &lem_lm[k];
                if (!(p->fl & LF_GONE) && !(lem_an[p->act].flags & LAF_PRIO) &&
                    p->x - lem_view > 16 && p->x - lem_view < 300 &&
                    p->y > 20 && p->y < 150)
                    break;
            }
            if (k < lem_released) {
                p = &lem_lm[k];
                row = 0;
                (void)row;
                x0 = p->x - (int)lem_an[p->act].footx;
                y0 = p->y - (int)lem_an[p->act].footy;
                lem_mx = x0 + 6 - lem_view + 3;     /* the game point is the */
                lem_my = y0 + 6 - 2;                /* pointer's (-3, +2) */
                lem_hit_last = -1;
                ovl_tick_draw(0, 0);
                if (lem_hit_n == 0)
                    fail("the glitch row's pointer is not over its lemming");
                if (lem_hit_last != -1)
                    fail("the per-tick hit test between frames recorded the "
                         "right-click glitch's lemming - Lemmix records it "
                         "only at a frame boundary, while paused and in the "
                         "assignment (Game.pas:2646, Player.pas:654, :3683)");
                lem_paused = 1;
                ovl_tick_draw(0, 0);
                lem_paused = 0;
                if (lem_hit_last < 0)
                    fail("the hit test while PAUSED did not record the "
                         "glitch's lemming (Img_MouseMove, Player.pas:654)");

                /* ...and a PRESS on it through the bracket's own mouse path
                 * assigns the selected skill (the desktop pointer mapped the
                 * VGA way: x / 2, y * 5 / 12) */
                {
                    int c0;

                    ovl_select_skill(LB_DIGGER);
                    lem_cnt[LB_DIGGER - LB_CLIMBER] = 5;
                    c0 = lem_cnt[LB_DIGGER - LB_CLIMBER];
                    h_mouse.x = lem_mx * 2;
                    h_mouse.y = (lem_my * 12 + 4) / 5;
                    h_mouse.btn = 0;
                    lem_mbtn = 0;
                    ovl_mouse_input(1);
                    h_mouse.btn = 1;
                    ovl_mouse_input(1);
                    h_mouse.btn = 0;
                    ovl_mouse_input(1);
                    printf("  a press on a walker: pointer (%d, %d), %d under "
                           "it, digger count %d -> %d\n", lem_mx, lem_my,
                           lem_hit_n, c0, lem_cnt[LB_DIGGER - LB_CLIMBER]);
                    if (lem_cnt[LB_DIGGER - LB_CLIMBER] != c0 - 1)
                        fail("a press on a walker did not assign the digger");
                }
            } else {
                fail("no walker in view for the glitch row");
            }
        }

        /* --- the pointer RESTING on the panel is not taken off every frame:
         * only a write inside the bytes it holds takes it off (ovl_pan_hit).
         * A skill button whose count does not change, and a minimap cell far
         * from the view rectangle and from every lemming's dot. */
        {
            int c0, f2, mc;

            lem_mx = LB_PAUSE * 16 + 8;
            lem_my = LEM_VIEWH_PX + 28;
            ovl_tick_draw(0, 0);
            c0 = h_r_curdraws;
            for (f2 = 0; f2 < 30 && !lem_finished; f2++) {
                lem_game_frame();
                ovl_tick_draw(1, 0);
            }
            printf("  a pointer resting on a skill button: %d redraws in %d "
                   "frames\n", h_r_curdraws - c0, f2);
            if (h_r_curdraws - c0 > 1)
                fail("a pointer resting on a skill button was taken off by "
                     "game frames that drew nothing under it - any panel "
                     "change took it off (ovl_pan_hit)");
            mc = lem_view / LEM_MM_XSTEP > 50 ? 8 : 90;
            lem_mx = LEM_MM_X + mc;
            lem_my = LEM_VIEWH_PX + LEM_MM_Y + 10;
            ovl_tick_draw(0, 0);
            c0 = h_r_curdraws;
            for (f2 = 0; f2 < 30 && !lem_finished; f2++) {
                lem_game_frame();
                ovl_tick_draw(1, 0);
            }
            printf("  a pointer resting on the minimap: %d redraws in %d "
                   "frames\n", h_r_curdraws - c0, f2);
            if (h_r_curdraws - c0 > 1)
                fail("a pointer resting on a quiet part of the minimap was "
                     "taken off by every game frame (ovl_pan_hit)");
            lem_mx = 316;
            lem_my = 8;
            ovl_tick_draw(0, 0);
        }

        /* --- THE BUTTONS' AND THE MINIMAP'S HIT BOXES ARE LEMMIX'S, right
         * and bottom exclusive (PtInRectEx on Rect(1,16,15,38) + 16n;
         * DosMiniMapCorners (208,18)-(311,37) under TRect.Contains) */
        {
            int sk = lem_skill;

            ovl_select_skill(LB_CLIMBER);
            ovl_panel_press(LB_DIGGER * 16 + 15, 20);
            if (lem_skill != LB_CLIMBER)
                fail("x = 16n + 15 selected a button - Lemmix's rect is "
                     "right-exclusive");
            ovl_panel_press(LB_DIGGER * 16 + 10, 38);
            if (lem_skill != LB_CLIMBER)
                fail("y = 38 selected a button - Lemmix's rect is "
                     "bottom-exclusive");
            ovl_panel_press(LB_DIGGER * 16 + 14, 37);
            if (lem_skill != LB_DIGGER)
                fail("x = 16n + 14, y = 37 did not select the button");
            lem_mmdrag = 0;
            ovl_panel_press(LEM_MM_X + LEM_MM_W - 1, LEM_MM_Y + 5);
            ovl_panel_press(LEM_MM_X + 5, LEM_MM_Y + LEM_MM_H - 1);
            if (lem_mmdrag)
                fail("x 311 or y 37 started a minimap drag - "
                     "DosMiniMapCorners stops at 310 and 36");
            ovl_panel_press(LEM_MM_X + LEM_MM_W - 2, LEM_MM_Y + LEM_MM_H - 2);
            if (!lem_mmdrag)
                fail("(310, 36) did not start a minimap drag");
            lem_mmdrag = 0;
            ovl_select_skill(sk);
        }

        /* --- OVERLAPPING ANIMATED OBJECTS ARE DRAWN ONCE A FRAME: their
         * rectangles merge (ovl_rc_add's full merge), so an exit and its
         * torch, or a row of water tiles, is one rectangle and each object
         * over it one draw - wave 3 drew each about three times a frame */
        if (lem_nob >= 2) {
            int k, keep, ox, oy, n0;

            ox = lem_ob[1].x;
            oy = lem_ob[1].y;
            lem_ob[1].x = lem_ob[0].x + (int)lem_ty[lem_ob[0].id].w / 2;
            lem_ob[1].y = lem_ob[0].y;
            for (k = 0; k < lem_nob; k++)
                lem_ob[k].drawn = lem_ob[k].frame;
            lem_ob[0].drawn = 0xFE;
            lem_ob[1].drawn = 0xFE;
            lem_view = lem_ob[0].x - 40 < 0 ? 0 : lem_ob[0].x - 40;
            lem_view_move(0);
            lem_ndmg = 0;
            lem_nrc = 0;
            ovl_rc_world();
            n0 = lem_nrc;
            for (k = 0; k < lem_nob; k++)
                lem_ob[k].drawn = lem_ob[k].frame;
            lem_ob[0].drawn = 0xFE;
            lem_ob[1].drawn = 0xFE;
            keep = lem_released;
            lem_released = 0;           /* no lemming rectangles: objects only */
            h_r_objs = 0;
            ovl_frame_draw();
            lem_released = keep;
            printf("  two overlapping animated objects: %d rectangle(s), %d "
                   "object draws\n", n0, h_r_objs);
            if (n0 != 1)
                fail("two overlapping changed objects made two rectangles - "
                     "each is then drawn under the other's too");
            if (h_r_objs > 2 + 1)
                fail("two overlapping animated objects cost more than one "
                     "draw each a frame");
            lem_ob[1].x = ox;
            lem_ob[1].y = oy;
        } else {
            fail("the fixture level has fewer than two objects - the overlap "
                 "row tests nothing");
        }

        /* --- the seventeenth destruction rectangle folds into the slot it
         * grows least, not into slot 15 whatever the distance */
        {
            int k, wide = 0;

            lem_ndmg = 0;
            for (k = 0; k < 16; k++)
                lem_damage(k * 90, 40, 9, 1);
            lem_damage(5, 40, 9, 1);    /* beside slot 0, 1,345 px from 15 */
            for (k = 0; k < 16; k++)
                if (lem_dmg[k * 4 + 2] > 100)
                    wide++;
            if (wide)
                fail("a far destruction rectangle was folded into one the "
                     "width of the level (ovl_dmg_fold)");
            lem_ndmg = 0;
        }
    }

    /* --- the status line's five field offsets (the authority table) --------
     * Lemmix Game.SkillPanel.pas:501-563's SETTERS, not the label-inclusive
     * comment block at :250-255 - and the draft plan took the comment, which
     * puts the IN field one column left of the truth. Column 26 is that
     * field's FIRST cell and column 31 is not in it. */
    {
        const char *tpl = lem_str(LEMS_STATUS_TEMPLATE);
        int n = 0, k;

        while (tpl[n])
            n++;
        if (n != 40)
            fail("the status template is not 40 characters - it is what makes "
                 "the five write offsets mean anything");
        for (k = 0; k < n; k++) {
            /* EVERY CHARACTER MAPS OR DRAWS A BLACK CELL, and there is no
             * third case: the template's own '.' and the space both fall to
             * the fourth arm, which is what Lemmix's DrawNewStr does. */
            if (lem_f_index(tpl[k]) >= 38)
                fail("a status template character maps past the 38-glyph set");
        }
        if (lem_f_index('%') != 0 || lem_f_index('0') != 1 ||
            lem_f_index('9') != 10 || lem_f_index('-') != 11 ||
            lem_f_index('A') != 12 || lem_f_index('Z') != 37)
            fail("the status font's index map is not the four-arm case "
                 "Lemmix DrawNewStr implements");
        if (lem_f_index('a') != lem_f_index('A'))
            fail("the status font does not UpCase - MAIN.DAT section 6 has no "
                 "lowercase glyph and Lemmix's mixed case is undone at draw "
                 "time");
        if (lem_f_index(' ') != -1 || lem_f_index('.') != -1)
            fail("a space or the template's own '.' mapped to a glyph - both "
                 "are the fourth arm's 8x16 BLACK CELL");
    }

    /* --- THE STATUS-STRING AUDIT (SPEC.md 116.12) --------------------------
     * Every line ovl_stat_build() can compose, built here for every action and
     * every climber/floater combination, with a two-digit hit count, the
     * level's largest OUT, 100% IN and a two-digit clock: forty cells, every
     * one a glyph of the 38 or the fourth arm's black cell, the word and its
     * count never clipped by the 14-cell PadRight (Lemmix's longest word plus
     * " 80" is 12), the template's own cells 14-17, 23-25, 31-34 and 37 never
     * written, and each field where its setter writes it. */
    {
        static const unsigned char fl[4] = { 0, LF_CLIMB, LF_FLOAT,
                                             LF_CLIMB | LF_FLOAT };
        const char *tpl = lem_str(LEMS_STATUS_TEMPLATE);
        const char *w;
        int a, f, k, lines = 0, bad = 0;
        static char want[16];

        lem_released = 1;
        lem_maxlem = 80;
        for (a = 0; a < LA_COUNT; a++)
            for (f = 0; f < 4; f++) {
                os88_memset(&lem_lm[0], 0, sizeof(struct lem_l));
                lem_lm[0].act = (unsigned char)a;
                lem_lm[0].fl = fl[f];
                lem_hit_n = 80;
                lem_hit_l1 = 0;
                lem_out = 80;
                lem_saved = 80;
                lem_inpct = 100;
                lem_tmin = 99;
                lem_tsec = 59;
                ovl_stat_build();
                lines++;
                w = lem_str(ovl_word(0));
                snprintf(want, sizeof want, "%s 80", w);
                if (strlen(want) > 14 || strncmp(lem_stnew, want,
                                                 strlen(want)) != 0) {
                    printf("  status: '%.14s' for action %d flags %d, "
                           "the word is '%s'\n", lem_stnew, a, f, w);
                    bad++;
                }
                for (k = 0; k < LEM_STAT_CELLS; k++)
                    if (lem_f_index(lem_stnew[k]) >= 38 ||
                        lem_f_index(lem_stnew[k]) < -1)
                        bad++;
                for (k = 0; k < LEM_STAT_CELLS; k++)
                    if ((k >= 14 && k <= 17) || (k >= 23 && k <= 25) ||
                        (k >= 31 && k <= 34) || k == 37)
                        if (lem_stnew[k] != tpl[k])
                            bad++;
                if (strncmp(lem_stnew + 18, "80   ", 5) != 0 ||
                    strncmp(lem_stnew + 26, "100% ", 5) != 0 ||
                    strncmp(lem_stnew + 35, "99", 2) != 0 ||
                    strncmp(lem_stnew + 38, "59", 2) != 0)
                    bad++;
            }
        /* ...and the quiet line: nobody under the cursor, one minute left */
        lem_hit_n = 0;
        lem_out = 0;
        lem_saved = 0;
        lem_inpct = 0;
        lem_tmin = 1;
        lem_tsec = 5;
        ovl_stat_build();
        lines++;
        if (strncmp(lem_stnew, "              ", 14) != 0 ||
            strncmp(lem_stnew + 18, "0    ", 5) != 0 ||
            strncmp(lem_stnew + 26, "0%   ", 5) != 0 ||
            strncmp(lem_stnew + 35, " 1", 2) != 0 ||
            strncmp(lem_stnew + 38, "05", 2) != 0)
            bad++;
        /* ...and IN is what Lemmix's two setters LAST wrote: a nuke moves
         * MaxNumLemmings (R8) and the field must not move with it - 10
         * lemmings, 1 saved, 3 released, nuked: still 10%, not 33% */
        lem_maxlem = 10;
        lem_saved = 1;
        lem_inpct = 10;
        lem_maxlem = 3;
        ovl_stat_build();
        lines++;
        if (strncmp(lem_stnew + 26, "10%  ", 5) != 0)
            bad++;
        printf("  status audit: %d lines composed, %d wrong cells or fields "
               "(must be 0)\n", lines, bad);
        if (bad)
            fail("a status line ovl_stat_build() can compose is clipped, "
                 "unprintable or off its setter's offset");
        lem_released = 0;
        lem_out = 0;
        lem_hit_n = 0;
        lem_hit_l1 = -1;
    }

    /* --- the bracket runs, and the launch refuses OUTSIDE it ---------------
     * SPEC.md 116.5: everything that can refuse happens in a window, because a
     * kernel toast in a foreign mode paints desktop geometry into the game's
     * framebuffer. */
    /* IT IS DEFERRED TO THE WAKE, and that is the largest of the four errands
     * os88_onwake() carries: LEMMAIN.LEM is 60,416 bytes and a style bank up
     * to 78,848 more, ~136 KB and tens of seconds of floppy at
     * PERFORMANCE.md's ~400 ms an int 13h - and Enter, Space and a Play click
     * are all delivered with the gfx lock HELD, with every other window's
     * painter stopped behind it. The keystroke's whole share is a flag and a
     * wake, and it draws NOTHING: the list it used to letter first was covered
     * by a foreign mode before a pixel of it was seen. */
    lem_screen = LEM_SC_LIST;
    lem_sel = 0;
    lem_top = 0;
    glass_clear();
    repaint("the list, before a launch");
    h_reads = 0;
    h_wakes = 0;
    h_fsx_runs = 0;
    h_toasts = 0;
    counters_reset();
    os88_onkey(13, 0, lem_win);     /* Enter on the list: Play (wave 5 - the
                                     * preview is the bracket's first screen) */
    if (h_reads != 0)
        fail("a launch keystroke read a file - ~136 KB of band with the gfx "
             "lock HELD, which is SPEC.md 116.6's first table row put back one "
             "callback along");
    if (h_fsx_runs != 0)
        fail("a launch keystroke entered SPEC.md 53's bracket straight out of "
             "a callback, before anything could refuse in a window");
    if (g_calls != 0)
        fail("a launch keystroke drew - two full repaints of a list that the "
             "mode change covers before a pixel of it is seen");
    if (h_wakes == 0)
        fail("the launch keystroke deferred and posted no wake, so the launch "
             "is owed to nobody");
    if (lem_screen != LEM_SC_LIST)
        fail("the launching arm did not leave the screen on the list");

    counters_reset();
    os88_onwake(lem_win);
    if (h_reads == 0)
        fail("the wake did not read the bands the launch keystroke deferred");
    if (h_fsx_runs != 1)
        fail("the wake did not run the launch the keystroke deferred");

    h_fsx_runs = 0;
    h_toasts = 0;
    lem_screen = LEM_SC_LIST;
    ovl_launch(lem_win);
    if (h_fsx_runs != 1)
        fail("the launch did not enter the bracket");
    printf("  after Esc: screen %d, exit %d, finished %d (no screens' mode "
           "on this display: the postview is the window's)\n", lem_screen,
           lem_exit, lem_finished);
    if (lem_screen != LEM_SC_POST)
        fail("Esc out of a level, on a display with no screens' mode, did not "
             "reach the WINDOW's postview (Lemmix :463, 'if Key = VK_ESCAPE "
             "then Game.Finish'; ovl_r_mode's refusal is a normal path)");
    if (!h_fsx_ticks)
        fail("the bracket never waited for a tick - the input poll and the "
             "cursor are once per TICK and unconditional");
    if (h_fullscreen)
        fail("the SPEC.md 11.2 fullscreen window was not taken down on the "
             "way out");

    /* --- A REFUSED BRACKET, WITH A STALE VERDICT IN lem_exit ---------------
     * os88_fsx_run() answers -1 without running the entry (already in a
     * bracket, the file dialog up, not task 0, the ownership fence). The Esc
     * above left lem_exit = DONE, which is exactly the stale value the review
     * caught: read without the call's own answer it showed the postview of a
     * level never played and wrote it into LEMMINGS.SAV as reached. */
    if (lem_exit != LEM_X_DONE)
        fail("the Esc row did not leave DONE behind - the next row tests "
             "nothing");
    h_fsx_refuse = 1;
    h_fsx_runs = 0;
    h_toasts = 0;
    lem_progwrite = 0;
    lem_screen = LEM_SC_LIST;
    lem_sel = 0;
    ovl_launch(lem_win);
    h_fsx_refuse = 0;
    printf("  refused bracket: screen %d, toast \"%s\"\n", lem_screen,
           h_lasttoast);
    if (lem_screen == LEM_SC_POST)
        fail("a refused bracket showed the postview - lem_exit still held the "
             "last session's verdict");
    if (lem_progwrite)
        fail("a refused bracket recorded progress for a level never played");
    if (h_toasts == 0 || strcmp(h_lasttoast, lem_str(LEMS_NO_FSX)) != 0)
        fail("a refused bracket did not say so in its own words (LEMS_NO_FSX) "
             "- SPEC.md 47: the fact the action refused on, never a guess");
    if (lem_suspended || lem_playing >= 0)
        fail("a refused first launch kept the level's claims");

    /* ...and the mode refused INSIDE a bracket that ran: its own fact, in
     * the window, and never NO_BANK's 'could not be read' about a read that
     * succeeded */
    h_fsx_moderefuse = 1;
    h_fsx_runs = 0;
    h_toasts = 0;
    lem_progwrite = 0;
    lem_screen = LEM_SC_LIST;
    ovl_launch(lem_win);
    h_fsx_moderefuse = 0;
    if (h_fsx_runs != 1)
        fail("the mode-refusal row did not enter the bracket");
    if (lem_screen != LEM_SC_FACT)
        fail("os88_fsx_mode() refusing inside the bracket did not show "
             "LEMS_NO_MODE in the window");
    if (h_toasts != 0)
        fail("os88_fsx_mode() refusing inside the bracket toasted - the band "
             "reads all succeeded, so NO_BANK would be a guess");
    if (lem_progwrite)
        fail("a mode refusal recorded progress");
    if (!strstr(lem_para, "let go") || strstr(lem_para, "Nothing was loaded"))
        fail("the in-bracket mode refusal on a first launch said nothing was "
             "loaded - the level had been read and claimed (SPEC.md 47: "
             "never a non-fact; NO_MODE ends with MODE_GONE there)");

    /* ...on a RESUME the kept level stays kept, the NO_FSX arm's shape: the
     * preview screen, a toast in its own words, and the claims kept */
    if (!ovl_level_load(0))
        fail("the resume row could not load level 1");
    lem_playing = 0;
    lem_suspended = 1;
    h_fsx_moderefuse = 1;
    h_toasts = 0;
    lem_screen = LEM_SC_LIST;
    ovl_launch(lem_win);
    h_fsx_moderefuse = 0;
    printf("  mode refused on a resume: screen %d, suspended %d, toast "
           "\"%s\"\n", lem_screen, lem_suspended, h_lasttoast);
    if (!lem_suspended || lem_playing != 0)
        fail("a mode refused on a RESUME threw the kept level away");
    if (lem_screen != LEM_SC_PREVIEW)
        fail("a mode refused on a resume did not bring the preview back");
    if (h_toasts == 0 || strcmp(h_lasttoast, lem_str(LEMS_NO_MODE_KEPT)) != 0)
        fail("a mode refused on a resume did not say so in its own words "
             "(LEMS_NO_MODE_KEPT)");
    lem_abandon();

    /* ...and BEFORE the bracket (the caps check) is where "nothing was
     * loaded" is true, and the only place it is said */
    {
        int keep = h_caps;

        h_caps = 0;
        lem_caps = 0;
        lem_screen = LEM_SC_LIST;
        h_fsx_runs = 0;
        ovl_launch(lem_win);
        h_caps = keep;
        lem_caps = keep;
        if (h_fsx_runs != 0)
            fail("a launch the caps refuse entered the bracket");
        if (lem_screen != LEM_SC_FACT ||
            !strstr(lem_para, "Nothing was loaded"))
            fail("the pre-bracket mode refusal lost its true ending "
                 "(MODE_NONE)");
    }

    /* --- THE POSTVIEW'S SPACING (wave 4 review) ----------------------------
     * Lemmix GetScreenText's AddLineFeed(18 - CountChar(CR)) puts the footer
     * on line 18 whatever came above, counting the result texts' own CRs. A
     * 20-row box (the VGA window's) holds it; the threshold used to need 21,
     * which LEM_SH_ROWS never gives, so the branch was dead. */
    {
        int kr = lem_rows, kc = lem_cols, w, j;
        static const int rats[2] = { 0, LEM_RATINGS - 1 };
        static const int lvls[2] = { 0, LEM_PERRAT - 1 };

        lem_rows = LEM_SH_ROWS;
        lem_cols = LEM_SH_COLS;
        lem_nuked = 0;
        lem_timeup = 0;
        for (j = 0; j < 2; j++) {
            lem_saved = 0;
            w = ovl_postview(rats[j], lvls[j], LEM_PO_WIN);
            lem_wrap();
            printf("  postview %s, 20-row box: %d lines, line 18 \"%s\"\n",
                   j ? "of Mayhem 30" : "of Fun 1", lem_wrapn,
                   lem_wrap_line(LEM_PO_FOOT));
            if (strcmp(lem_wrap_line(LEM_PO_FOOT),
                       lem_str(j && w ? LEMS_PO_CONTINUE
                                      : w ? LEMS_PO_NEXTLEVEL
                                          : LEMS_PO_RETRYLEVEL)) != 0)
                fail("the postview's footer is not on line 18 in a box that "
                     "holds it (Lemmix GetScreenText's AddLineFeed)");
        }
        lem_rows = kr;
        lem_cols = kc;
    }

    /* --- THE POSTVIEW'S NEXT LEVEL ACROSS A SECTION'S END ------------------
     * Lemmix Styles.Base.pas:964-968 links a section's last level to the
     * next section's first: Fun 30 won is Tricky 1, and the progress write is
     * for Tricky. Wave 3 replayed Fun 30 under "Press left mouse button for
     * next level". */
    {
        int w;

        lem_rating = 0;
        lem_prog[1] = -1;
        lem_progwrite = 0;
        w = ovl_post_target(1, LEM_PERRAT - 1);
        printf("  Fun 30 won: next is rating %d level %d, progress for "
               "rating %d level %d\n", lem_post_nrat, lem_post_next + 1,
               lem_pw_rat, lem_pw_lvl + 1);
        if (lem_post_nrat != 1 || lem_post_next != 0)
            fail("winning a section's last level does not lead to the next "
                 "section's first (Lemmix TSection.DoAddLevelInfo)");
        if (!w || lem_pw_rat != 1 || lem_pw_lvl != 0)
            fail("winning a section's last level did not record the next "
                 "section's first as reached");
        ovl_post_target(0, LEM_PERRAT - 1);
        if (lem_post_nrat != 0 || lem_post_next != LEM_PERRAT - 1)
            fail("losing a section's last level does not retry it");
        lem_rating = LEM_RATINGS - 1;
        lem_progwrite = 0;
        if (ovl_post_target(1, LEM_PERRAT - 1) || lem_progwrite)
            fail("winning Mayhem 30 posted a progress write for Fun 1, which "
                 "is no level reached");
        if (lem_post_nrat != 0 || lem_post_next != 0)
            fail("continuing from the congratulation is not Fun 1 - Lemmix's "
                 "NextInfo is nil after Mayhem 30 and its preview falls back to "
                 "LevelSystem.FirstLevel (GameScreen.Preview.pas:89-90)");
        lem_rating = 0;
        ovl_post_target(1, 4);
        if (lem_post_nrat != 0 || lem_post_next != 5)
            fail("an ordinary win does not lead to the next level");
        lem_progwrite = 0;
    }

    /* --- A SPECIAL, THEN AN ORDINARY LEVEL OF ITS SET (wave 4 review) ------
     * ovl_spec_compose reads the style bank over the picture and pokes the
     * picture's eight colours over its custom palette. The claims outlive the
     * level (Continue on the postview, a pick from the list), so the next
     * ovl_bank_read of that style must NOT answer "already in" - or an
     * ordinary level is drawn in the special picture's colours. A synthetic
     * LSPB in the bank claim stands in for LEMSP0 (the fixture has none). */
    {
        unsigned char own[24];
        int k, ok;
        unsigned o;

        if (!ovl_level_load(0))
            fail("the special-palette row could not load level 1");
        for (k = 0; k < 24; k++)
            own[k] = (unsigned char)os88_peek(lem_cl_bank, LEM_GR_CUSTOM + k);
        lem_bank_have = -1;             /* what ovl_spec_read leaves */
        os88_poke(lem_cl_bank, 0, 'L');
        os88_poke(lem_cl_bank, 1, 'S');
        os88_poke(lem_cl_bank, 2, 'P');
        os88_poke(lem_cl_bank, 3, 'B');
        os88_poke(lem_cl_bank, LEM_SP_VERSION, LEM_SP_VER);
        os88_poke(lem_cl_bank, LEM_SP_VERSION + 1, 0);
        os88_poke(lem_cl_bank, LEM_SP_INDEX, 0);
        os88_poke(lem_cl_bank, LEM_SP_COLS, 2);
        os88_poke(lem_cl_bank, LEM_SP_ROWS, 4);
        os88_poke(lem_cl_bank, LEM_SP_PW, 480 & 255);
        os88_poke(lem_cl_bank, LEM_SP_PW + 1, 480 >> 8);
        os88_poke(lem_cl_bank, LEM_SP_PH, 40);
        os88_poke(lem_cl_bank, LEM_SP_PH + 1, 0);
        os88_poke(lem_cl_bank, LEM_SP_X, 304 & 255);
        os88_poke(lem_cl_bank, LEM_SP_X + 1, 304 >> 8);
        for (k = 0; k < 24; k++)
            os88_poke(lem_cl_bank, LEM_GR_CUSTOM + k, (own[k] ^ 0x15) & 63);
        for (k = 0; k < 8; k++)         /* eight 480x40 pieces, all clear */
            for (o = 0; o < 4u * 60u * 40u; o++)
                os88_poke(lem_cl_bank + (LEM_SP_HEAD >> 4) +
                          (unsigned)k * (60u * 40u / 4u) + (o >> 4),
                          o & 15, 0);
        if (!ovl_spec_compose(0))
            fail("a well-formed synthetic LSPB was refused by "
                 "ovl_spec_compose");
        if (os88_peek(lem_cl_bank, LEM_GR_CUSTOM) != ((own[0] ^ 0x15) & 63))
            fail("ovl_spec_compose did not carry the picture's palette over "
                 "the style bank (Lemmix AssemblePalette)");
        ovl_bank_read(0);               /* the next ordinary level of set 0 */
        ok = 1;
        for (k = 0; k < 24; k++)
            if (os88_peek(lem_cl_bank, LEM_GR_CUSTOM + k) != own[k])
                ok = 0;
        printf("  special then ordinary, same set: custom palette %s\n",
               ok ? "re-read from the set's band" : "STILL THE PICTURE'S");
        if (!ok)
            fail("after a special level an ordinary level of the same set "
                 "kept the picture's colours - ovl_bank_read answered from a "
                 "bank ovl_spec_compose had poked");
        /* ...and a hostile header with a NEGATIVE width is refused: cols 2 x
         * pw -32288 wraps to 960 in sixteen bits */
        lem_bank_have = -1;
        os88_poke(lem_cl_bank, 0, 'L');
        os88_poke(lem_cl_bank, 1, 'S');
        os88_poke(lem_cl_bank, 2, 'P');
        os88_poke(lem_cl_bank, 3, 'B');
        os88_poke(lem_cl_bank, LEM_SP_PW, (-32288) & 255);
        os88_poke(lem_cl_bank, LEM_SP_PW + 1, ((-32288) >> 8) & 255);
        if (ovl_spec_compose(0))
            fail("an LSPB header with a negative piece width was composed");
        lem_bank_have = -1;
        ovl_bank_read(0);
        lem_abandon();
    }

    /* ...and with no memory it refuses in the window, with the arithmetic. */
    lem_free_all();
    {
        unsigned hog[8];
        int k, n = 0, c0, f0, n0;

        for (k = 0; k < 8; k++) {
            hog[k] = os88_mem_claim(64);
            if (hog[k])
                n++;
        }
        /* ASKED FROM THE LIST, A REFUSAL DRAWS NOTHING (wave 5's second
         * review): the list is already the glass and its shadow says so, so
         * the toast is the whole answer - a blank-and-repaint here was ~674
         * ms and a white flash on every refused Play */
        lem_screen = LEM_SC_LIST;
        lem_repaint(lem_win, 1, 1);
        lem_ask_launch(lem_win);
        lem_launchpend = 0;
        h_toasts = 0;
        h_fsx_runs = 0;
        c0 = g_calls;
        f0 = g_fills;
        n0 = g_cells;
        ovl_launch(lem_win);
        printf("  a refused Play from the list: %d call(s), %d fill(s), %d "
               "cell(s); shadow %s\n", g_calls - c0, g_fills - f0,
               g_cells - n0, lem_sh_ok ? "still the glass" : "INVALIDATED");
        if (g_calls != c0 || g_fills != f0 || g_cells != n0 || !lem_sh_ok)
            fail("a launch refused from the list repainted the list it was "
                 "asked from - only the toast changes");
        if (h_fsx_runs != 0)
            fail("the launch entered the bracket with no memory for the level");
        if (h_toasts == 0)
            fail("the launch refused silently - SPEC.md 47 wants the fact and "
                 "the arithmetic, said in a WINDOW");
        else
            printf("  refusal: %s\n", h_lasttoast);
        if (strlen(h_lasttoast) > 24)
            fail("the launch's refusal toast is longer than TOAST_MAX's 24 "
                 "- the kernel cuts it mid-word (wave 4's third review)");
        for (k = 0; k < 8; k++)
            if (hog[k])
                os88_mem_free(hog[k]);
        (void)n;
    }

    /* ...AND A LAUNCH ASKED FROM A WINDOW SCREEN CLAIMS NOTHING BEFORE THE
     * WAKE (wave 6, wave 5's third review): the glass still shows the
     * paragraph, so the screen and the shadow still say so, and a key typed
     * ahead of the wake - ui_task dispatches one key and then one event a pass
     * - neither repaints the list from an invalid shadow (every cell, blanks
     * included, ~1.1 s on the XT) nor moves the selection the launch is about
     * to read. */
    {
        int c0, n0, sel0;

        lem_screen = LEM_SC_LIST;
        lem_repaint(lem_win, 1, 1);
        lem_sel = 3;
        lem_screen = LEM_SC_PREVIEW;
        lem_wrap();
        lem_repaint(lem_win, 1, 1);
        sel0 = lem_sel;
        lem_ask_launch(lem_win);
        c0 = g_calls;
        n0 = g_cells;
        os88_onkey(0, LEM_SC_DOWN, lem_win);
        os88_onkey(0, LEM_SC_PGDN, lem_win);
        os88_onclick(lem_org.x + 4, lem_org.y + 4, lem_win);
        printf("  typed ahead of a launch from a window preview: screen %d, "
               "sel %d -> %d, %d call(s), %d cell(s)\n", lem_screen, sel0,
               lem_sel, g_calls - c0, g_cells - n0);
        if (lem_screen != LEM_SC_PREVIEW || !lem_sh_ok)
            fail("asking for a launch from a window preview claimed the list "
                 "before the wake - the glass still shows the paragraph");
        if (lem_sel != sel0 || g_calls != c0 || g_cells != n0)
            fail("a key typed ahead of a pending launch moved the selection "
                 "or drew - the launch would play a level not chosen");
        lem_launchpend = 0;
        lem_screen = LEM_SC_LIST;
        lem_sh_ok = 0;
        lem_repaint(lem_win, 1, 1);
    }

    /* ======================================================================
     * WAVE 5: THE ORIGINAL'S TWO SCREENS, INSIDE THE BRACKET (SPEC.md 116.4.3)
     *
     * Driven through the same ovl_launch() the wake runs, against
     * hosttest/lemraster.c's model of the 640x350 surface: which row each
     * line sits on, how wide it is, and that nothing is lettered on a row the
     * card does not show. The screens are answered from their own scripts
     * (os88_fsx_key's h_pv_keys / h_po_keys), so each leaves when the row
     * says.
     * ====================================================================*/
    {
        int k, x0, x1, y, runs, pv, ok, keepcaps, keepkind, before, kp0, cr0;
        static const char congr[] = "Everybody here at DMA Design salutes you";

        keepcaps = h_caps;
        keepkind = h_vidkind;
        h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_VGA12) |
                 (1 << OS88_FSXM_CGA320) | (1 << OS88_FSXM_CGA640);
        h_vidkind = OS88_VID_VGA;
        lem_mode_ok = lem_probe_mode(lem_win);
        lem_rating = 0;
        lem_rating_load(0);

        /* --- the preview, on a VGA: Esc from it is the menu --------------- */
        h_pv_keys[0] = 0;               /* no script: Esc */
        h_pv_hold = 80 / 8;             /* ...once the picture is drawn */
        h_pv_asked = 0;
        h_fsx_runs = 0;
        h_p_minis = 0;
        h_p_pals = 0;
        lem_sel = 0;
        lem_screen = LEM_SC_LIST;
        ovl_launch(lem_win);
        k = h_p_find("Level 1 Fixture one");
        printf("  preview, VGA: title row %d x %d, footer row %d x %d..%d, "
               "lowest glyph row %d, %d picture rows, exit %d\n",
               k >= 0 ? h_p_ly[k] : -1, k >= 0 ? h_p_lx0[k] : -1,
               lem_scr_fy, h_p_find("Press mouse button") >= 0
                   ? h_p_lx0[h_p_find("Press mouse button")] : -1,
               h_p_find("Press mouse button") >= 0
                   ? h_p_lx1[h_p_find("Press mouse button")] : -1,
               h_p_lowest - 1, h_p_minis, lem_exit);
        if (h_fsx_runs != 1 || !h_pv_asked)
            fail("the preview did not run inside the bracket");
        if (k < 0 || h_p_ly[k] != 82 || h_p_lx0[k] != 0)
            fail("the preview's title is not Lemmix's line 0 - x 0, y 82 "
                 "(GameScreen.Preview.pas BuildScreen)");
        k = h_p_find("Number of Lemmings");
        if (k < 0 || h_p_ly[k] != 82 + 3 * 16 || h_p_lx0[k] != 160)
            fail("the preview's value lines are not at aLines[03]'s row with "
                 "the ten-space IndentStr");
        k = h_p_find("Press mouse button to continue");
        if (k < 0 || h_p_ly[k] != 322 || h_p_lx0[k] != 80 ||
            h_p_lx1[k] != 559)
            fail("the preview's footer is not aLines[15] centred at y 322");
        if (h_p_lowest != 338 || h_p_clipped || h_p_wide)
            fail("the preview's lowest glyph row is not 337, or a glyph was "
                 "lettered off the surface");
        if (h_p_minis != 80 || !h_p_pals)
            fail("the preview did not draw its 80-row picture in its own "
                 "palette");
        if (lem_exit != LEM_X_MENU || lem_screen != LEM_SC_LIST ||
            lem_playing >= 0)
            fail("Esc on the preview is the original's menu - the list, the "
                 "level let go (GameScreen.Preview.pas Form_KeyDown)");

        /* --- the session: preview, play, postview, RETRY, preview, menu ---
         * One bracket. The postview's left button (Enter here) on a failure
         * retries: the level is read again INSIDE the bracket (SPEC.md 53.1)
         * and the preview comes back - and Esc there leaves. */
        h_pv_keys[0] = 0x1C0D;          /* Enter: play */
        h_pv_keys[1] = 0;               /* ...then Esc on the second preview */
        h_po_keys[0] = 0x1C0D;          /* Enter on the postview: retry */
        h_po_keys[1] = 0;
        h_fsx_runs = 0;
        h_pv_asked = 0;
        h_po_asked = 0;
        h_reads = 0;
        h_writes = 0;
        h_toasts = 0;
        pv = h_p_clears;
        kp0 = h_kpaints;
        cr0 = h_p_clrows;
        before = h_p_margins;
        lem_sel = 0;
        lem_progwrite = 0;
        lem_screen = LEM_SC_LIST;
        ovl_launch(lem_win);
        runs = h_fsx_runs;
        /* THE PAGE IS CLEARED ONLY WHERE THE TILE DOES NOT GO: the preview's
         * 80-row well twice, the postview not at all (lemfont.inc
         * lem_p_clear) - it was the whole page under every screen */
        printf("  page clears: %d surface rows over %d screens\n",
               h_p_clrows - cr0, h_p_clears - pv);
        if (h_p_clrows - cr0 > 2 * 80)
            fail("a screen cleared rows the brown tile writes anyway");
        /* ...AND A VGA'S MARGINS ONLY AFTER A MODE SET THAT HAPPENED (wave 6,
         * wave 5's third review): preview (set), play, postview (set), the
         * retried preview (no set - the postview's 12h is up) is TWO margin
         * clears, not three - each ~10 KB of ISA VRAM through four planes */
        printf("  VGA margin clears over those screens: %d\n",
               h_p_margins - before);
        if (h_p_kind == 0 && h_p_margins - before >= h_p_clears - pv)
            fail("a screen that followed a screen in the same mode cleared "
                 "the VGA margins the first set's clear had left black");
        printf("  a session: %d bracket(s), %d screens drawn, preview asked "
               "%d, postview asked %d, %d reads, exit %d\n", runs,
               h_p_clears - pv, h_pv_asked, h_po_asked, h_reads, lem_exit);
        if (runs != 1 || !h_po_asked || h_p_clears - pv < 3)
            fail("preview -> play -> postview -> retry -> preview did not run "
                 "as one bracket");
        if (lem_exit != LEM_X_MENU || lem_screen != LEM_SC_LIST)
            fail("Esc on the retried level's preview did not reach the list");
        /* ...AND THE KERNEL'S PAINT IS THE LIST'S ONLY ONE, AND THERE IS ONE
         * KERNEL PAINT (wave 6: no fullscreen latch around the bracket). The
         * screen is chosen INSIDE the bracket (ovl_leave), so the fsx exit's
         * wm_paint_all draws it and the program issues nothing after it
         * (SPEC.md 116.4.6's "leave the bracket" row - it was once a third
         * full repaint, a fill and 28 calls). With the latch there were
         * THREE kernel paints a launch: wm_raise's whole-screen fill on the
         * way in, the fsx exit's with the window still fullscreen, and
         * os88_fullscreen(win, 0)'s - two white flashes nobody needed. */
        printf("  leaving the bracket (Esc): %d kernel paint(s) this launch, "
               "then %d call(s) and %d fill(s) of the program's\n",
               h_kpaints - kp0, g_calls - h_kp_calls, g_fills - h_kp_fills);
        if (g_calls != h_kp_calls || g_fills != h_kp_fills)
            fail("leaving the bracket for the list repainted after the "
                 "kernel's own paint had drawn it");
        for (k = kp0; k < h_kpaints; k++)
            printf("  kernel paint %d of the launch: the program's %d "
                   "call(s), %d cell(s)\n", k - kp0 + 1,
                   h_kp_pcalls[k & 15], h_kp_pcells[k & 15]);
        if (h_kpaints - kp0 != 1)
            fail("a launch cost more than the fsx exit's one kernel paint - "
                 "the fullscreen latch is back, or the harness models "
                 "nothing");
        if (!h_kp_pcells[kp0 & 15])
            fail("the fsx exit's paint drew nothing - the list is not on the "
                 "glass");
        audit_shadow("leaving the bracket (Esc)");
        if (h_toasts != 0)
            fail("the session toasted - nothing may be said inside the "
                 "bracket, and nothing went wrong");
        h_po_keys[0] = 0;

        /* --- A HELD Esc DOES NOT ANSWER THE POSTVIEW (wave 5's second
         * review). The play loop stops at the first exit key; a typematic
         * repeat queued behind it was read by the postview as the menu
         * before the result had been seen. Esc is scan 01, no E0 prefix, so
         * this is not SPEC.md 9.7's doubled-key class. Play ends on the
         * first of two Escs; the postview must drain the second and answer
         * its OWN key (Enter: retry), and the retried preview its own (Esc). */
        h_fsx_keys[0] = 0x011B;
        h_fsx_keys[1] = 0x011B;
        h_fsx_keyn = 2;
        h_pv_keys[0] = 0x1C0D;
        h_pv_keys[1] = 0;
        h_po_keys[0] = 0x1C0D;
        h_po_keys[1] = 0;
        pv = h_p_clears;
        lem_sel = 0;
        lem_progwrite = 0;
        lem_screen = LEM_SC_LIST;
        ovl_launch(lem_win);
        printf("  a held Esc: play read %d of 2 queued, the postview "
               "answered %s, %d screens drawn, exit %d\n", h_fsx_keyi,
               h_po_ki ? "its own key" : "THE STALE Esc", h_p_clears - pv,
               lem_exit);
        if (h_po_ki != 1 || h_fsx_keyi != 2 || h_p_clears - pv < 3)
            fail("a typematic Esc queued behind the one that ended play "
                 "answered the postview - the screens drain the buffer the "
                 "way lem_sbtn = 0xFF guards the buttons");
        h_fsx_keyn = 0;
        h_fsx_keyi = 0;
        h_po_keys[0] = 0;

        /* --- A HERCULES SCREEN AFTER PLAY SETS NO MODE (wave 5's second
         * review): play and the screens are the same FSXM_HERC, and the set
         * was a black frame between the last game frame and the postview and
         * a 32 KB wipe of a surface the tile covers. A VGA's 0Dh -> 12h is a
         * real change and is still set. Play always sets its own. */
        {
            int hm, vm, sm;

            h_caps = (1 << OS88_FSXM_HERC);
            h_vidkind = OS88_VID_HERC;
            lem_mode_ok = lem_probe_mode(lem_win);
            if (!ovl_level_load(0))
                fail("fixture level 0 did not load on a Hercules");
            lem_live_id = -1;           /* lem_fsx_main's two resets */
            lem_onscr = 0;
            sm = h_modesets;
            ovl_r_mode(1);              /* the preview */
            ovl_r_mode(0);              /* play */
            lem_r_unprep();
            ovl_r_mode(1);              /* the postview */
            ovl_r_mode(1);              /* the next preview */
            hm = h_modesets - sm;
            lem_abandon();
            h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_VGA12) |
                     (1 << OS88_FSXM_CGA320) | (1 << OS88_FSXM_CGA640);
            h_vidkind = OS88_VID_VGA;
            lem_mode_ok = lem_probe_mode(lem_win);
            if (!ovl_level_load(0))
                fail("fixture level 0 did not load on a VGA");
            lem_live_id = -1;           /* lem_fsx_main's two resets */
            lem_onscr = 0;
            sm = h_modesets;
            ovl_r_mode(1);
            ovl_r_mode(0);
            lem_r_unprep();
            ovl_r_mode(1);
            ovl_r_mode(1);
            vm = h_modesets - sm;
            lem_abandon();
            lem_onscr = 0;
            printf("  mode sets over preview, play, postview, preview: "
                   "Hercules %d, VGA %d\n", hm, vm);
            if (hm != 2)
                fail("a Hercules screen after play set FSXM_HERC again - the "
                     "mode is up, and the set is a black frame and a 32 KB "
                     "wipe the tile covers anyway");
            if (vm != 3)
                fail("a VGA's play -> postview did not set mode 12h, or a "
                     "screen after a screen set it again");
        }

        /* --- THE MENU AFTER A SUCCESS IS ON THE NEXT LEVEL ----------------
         * Lemmix's GetScreenText moves CurrentLevelInfo to NextInfo before
         * the postview is drawn, so the menu's next Play is the next level;
         * after Mayhem 30 the preview falls back to FirstLevel. The bracket's
         * postview (ovl_post_move) and the window's (ovl_post_menu, whose
         * section crossing is owed to the wake) must both leave it there. */
        lem_rating_load(0);
        lem_rating = 0;
        lem_progwrite = 0;
        (void)ovl_post_target(1, 4);
        lem_progwrite = 0;
        k = ovl_post_move();
        x1 = lem_sel;
        if (k >= 0 || lem_sel != 5 || lem_rating != 0)
            fail("the postview's menu after Fun 5 won is not on Fun 6 "
                 "(Postview.pas GetScreenText: CurrentLevelInfo := NextInfo)");
        lem_rating = 3;
        (void)ovl_post_target(1, LEM_PERRAT - 1);
        lem_progwrite = 0;
        k = ovl_post_move();
        printf("  menu after a success: Fun 5 -> sel %d; Mayhem 30 -> rating "
               "%d sel %d\n", x1, lem_rating, lem_sel);
        if (k >= 0 || lem_rating != 0 || lem_sel != 0 || !lem_rat_ok)
            fail("the menu after Mayhem 30's congratulation is not on Fun 1 "
                 "(Preview.pas :89-90's FirstLevel fallback)");
        /* ...the WINDOW's postview, across a section: selected by the wake,
         * never launched */
        lem_rating = 0;
        (void)ovl_post_target(1, LEM_PERRAT - 1);   /* Fun 30 won: Tricky 1 */
        lem_progwrite = 0;
        lem_po_succ = 1;
        lem_screen = LEM_SC_POST;
        h_fsx_runs = 0;
        os88_onkey(27, 1, lem_win);
        if (lem_screen != LEM_SC_LIST || lem_rating != 1 || !lem_ratpend ||
            lem_post_play != 2)
            fail("Esc on the window's postview after Fun 30 did not owe the "
                 "wake Tricky's first level");
        os88_onwake(lem_win);
        if (lem_sel != 0 || lem_rating != 1 || lem_post_play || h_fsx_runs)
            fail("the wake did not select Tricky 1 without launching it");
        /* ...and within a section, at once */
        lem_rating_load(0);
        lem_rating = 0;
        (void)ovl_post_target(1, 2);
        lem_progwrite = 0;
        lem_po_succ = 1;
        lem_screen = LEM_SC_POST;
        os88_onkey(27, 1, lem_win);
        if (lem_screen != LEM_SC_LIST || lem_sel != 3 || lem_ratpend)
            fail("Esc on the window's postview after Fun 3 won is not on "
                 "Fun 4");
        lem_po_succ = 0;
        lem_sel = 0;

        /* --- the postview, in the original's coordinates ----------------- */
        if (!ovl_level_load(0))
            fail("the postview row could not load level 1");
        lem_playing = 0;
        lem_p_setup(LEM_RKIND_VGA, 0xA000);
        lem_nuked = 0;
        lem_timeup = 0;
        lem_saved = 10;                 /* 10 of 10: text 8 */
        ovl_po_draw(0);
        ok = lem_po_ix == 8 && h_p_find("Superb!") >= 0;
        k = h_p_find("All lemmings accounted for.");
        if (k < 0 || h_p_ly[k] != 16 || h_p_lx0[k] != 320 - 8 * 27)
            fail("the postview's top line is not centred at y 16 "
                 "(Postview.pas BuildScreen)");
        k = h_p_find("Press left mouse button for next level");
        x0 = h_p_find("Press right mouse button for menu");
        printf("  postview, VGA, 10 of 10: text %d, footer row %d, menu row "
               "%d, lowest %d\n", lem_po_ix, k >= 0 ? h_p_ly[k] : -1,
               x0 >= 0 ? h_p_ly[x0] : -1, h_p_lowest - 1);
        if (!ok)
            fail("10 of 10 is not result text 8 (GetResultText: Done = 100)");
        if (k < 0 || h_p_ly[k] != 16 + 18 * 16)
            fail("the postview's footer is not on :189's forced line 18");
        if (x0 < 0 || h_p_ly[x0] != 16 + 19 * 16)
            fail("the bracket's postview does not carry 'Press right mouse "
                 "button for menu' on line 19");
        lem_saved = 2;                  /* 20% of a 50% level: text 1 */
        ovl_po_draw(0);
        if (lem_po_ix != 1 || h_p_find("Better rethink") < 0)
            fail("a fifth of the target is not result text 1 (GetResultText: "
                 "Done < Target div 2)");
        k = h_p_find("Press left mouse button to retry level");
        if (k < 0 || h_p_ly[k] != 16 + 18 * 16 || h_p_clipped)
            fail("the failure footer moved - both branches take the failure "
                 "branch's AddLineFeed(5) (SPEC.md 116.8)");

        /* --- the congratulation: its middle line is the surface's width --- */
        lem_rating = LEM_RATINGS - 1;
        lem_saved = 0;
        ovl_po_draw(LEM_PERRAT - 1);
        k = h_p_find(congr);
        x0 = k >= 0 ? h_p_lx0[k] : -1;
        x1 = k >= 0 ? h_p_lx1[k] : -1;
        printf("  congratulation, VGA: '%s' spans x %d..%d\n", congr, x0, x1);
        if (x0 != 0 || x1 != 639)
            fail("'Everybody here at DMA Design salutes you' is not exactly "
                 "640 pixels of the purple font - the second confirmation of "
                 "the 640x350 surface (SPEC.md 116.1)");

        /* --- a Hercules: the same layout, its lowest glyph row on the glass */
        lem_p_setup(LEM_RKIND_HERC, 0xB000);
        lem_rating = 0;
        ovl_pv_draw(0);
        y = h_p_lowest - 1;
        k = h_p_clipped;
        lem_rating = LEM_RATINGS - 1;
        ovl_po_draw(LEM_PERRAT - 1);
        x0 = h_p_find(congr) >= 0 ? h_p_lx0[h_p_find(congr)] : -1;
        x1 = h_p_find(congr) >= 0 ? h_p_lx1[h_p_find(congr)] : -1;
        printf("  Hercules: preview lowest glyph row %d of 348, %d clipped; "
               "congratulation x %d..%d\n", y, k + h_p_clipped, x0, x1);
        if (y != 337 || k || h_p_clipped || x0 != 0 || x1 != 639)
            fail("the Hercules layout is not the original's, or it letters a "
                 "row past 347");
        lem_rating = 0;

        /* --- a CGA: 12 rows of 16 in 200, the preview's blank lines out --
         * (SPEC.md 116.4.3, LEMS_GREY_EGA): nothing past row 199, every
         * content line at its true width and pitch */
        h_caps = (1 << OS88_FSXM_CGA320) | (1 << OS88_FSXM_CGA640);
        h_vidkind = OS88_VID_CGA;
        lem_mode_ok = lem_probe_mode(lem_win);
        lem_p_setup(LEM_RKIND_CGA, 0xB800);
        ovl_pv_draw(0);
        k = h_p_find("Press mouse button to continue");
        printf("  CGA preview: %d lines, footer row %d, lowest %d of 200, %d "
               "clipped\n", h_p_nl, k >= 0 ? h_p_ly[k] : -1, h_p_lowest - 1,
               h_p_clipped);
        if (k < 0 || h_p_lowest > 200 || h_p_clipped || h_p_nl != 8)
            fail("the CGA preview lost a line or lettered past row 199");
        for (k = 1; k < h_p_nl; k++)
            if (h_p_ly[k] != h_p_ly[k - 1] + 16)
                fail("the CGA preview's lines are not at their 16-pixel "
                     "pitch with the blank lines compressed out");
        lem_saved = 10;
        ovl_po_draw(0);
        printf("  CGA postview: %d lines from row %d, lowest %d of 200, %d "
               "clipped\n", h_p_nl, h_p_nl ? h_p_ly[0] : -1, h_p_lowest - 1,
               h_p_clipped);
        if (h_p_lowest > 200 || h_p_clipped ||
            h_p_find("Press right mouse button for menu") < 0 ||
            h_p_find("Press left mouse button for next level") < 0)
            fail("the CGA postview lost its footers or lettered past row 199");
        lem_rating = LEM_RATINGS - 1;
        ovl_po_draw(LEM_PERRAT - 1);
        lem_rating = 0;
        if (h_p_lowest > 200 || h_p_clipped || h_p_find(congr) < 0 ||
            h_p_find("Now hold your breath") < 0 ||
            h_p_find("Press mouse button to continue") < 0)
            fail("the CGA congratulation lost a line or lettered past row "
                 "199");
        /* ...and a real launch there draws the 64-row picture */
        h_pv_keys[0] = 0;
        h_pv_hold = 64 / 8;
        h_p_minis = 0;
        lem_sel = 0;
        lem_screen = LEM_SC_LIST;
        ovl_launch(lem_win);
        if (h_p_minis != 64)
            fail("the CGA preview's picture is not 64 rows (GREY_EGA)");

        /* --- the Mode row's facts on that CGA, with an 83-key keyboard: all
         * three, and all of them inside the 137-pixel launcher ------------ */
        h_bda96 = 0;
        h_h = 137;
        glass_clear();
        lem_screen = LEM_SC_LIST;
        repaint("a CGA, an XT keyboard");
        if (!lem_state_grey(LEM_ROW_MODE))
            fail("the Mode row is not greyed on a CGA");
        lem_show_fact(lem_win, lem_state_label(LEM_ROW_MODE),
                      lem_state_fact(LEM_ROW_MODE), -1);
        printf("  the Mode row's fact, CGA + 83-key keyboard: %d lines in a "
               "%d-row box (%d bytes)\n", lem_wrapn, lem_rows, lem_paran);
        if (!strstr(lem_para, "a CGA 0x000F") ||
            !strstr(lem_para, "640x350") || !strstr(lem_para, "F11"))
            fail("the CGA Mode row does not name its three facts");
        if (lem_wrapn > lem_rows - 2)
            fail("the Mode row's three facts do not fit a CGA's launcher - "
                 "the last of them is under the footer");
        lem_back(lem_win);
        /* ...AND AT THE SMALLEST WINDOW THERE IS (LEM_MIN_W x LEM_MIN_H, a
         * 286x101 content box): the three facts wrap past the box, and the last
         * row that shows ends in "..." rather than dropping the rest silently
         * (wave 6, wave 5's third review) */
        {
            int r, c, el = 0;
            const char *row;

            h_w = LEM_MIN_W - 2;
            h_h = LEM_MIN_H - 19;
            os88_onresize(h_w, h_h, lem_win);
            lem_show_fact(lem_win, lem_state_label(LEM_ROW_MODE),
                          lem_state_fact(LEM_ROW_MODE), -1);
            r = lem_rows - 2;
            row = &lem_sh[r * LEM_SH_COLS];
            for (c = 0; c + 2 < lem_cols; c++)
                if (row[c] == '.' && row[c + 1] == '.' && row[c + 2] == '.')
                    el = 1;
            printf("  the Mode row's fact at the smallest window (%dx%d): %d "
                   "lines in a %d-row box, %s\n", h_w, h_h, lem_wrapn, lem_rows,
                   lem_wrapn <= lem_rows - 2 ? "all shown"
                   : el ? "the last row shown ends in ..." : "CUT SILENTLY");
            audit_shadow("the smallest window's Mode fact");
            if (lem_wrapn > lem_rows - 2 && !el)
                fail("a greyed fact longer than the smallest window was cut "
                     "with nothing on the glass to say so");
            lem_back(lem_win);
            h_w = 512;
            h_h = 137;
            os88_onresize(h_w, h_h, lem_win);
        }
        /* ...and on a VGA with an AT keyboard the row is not greyed at all;
         * with an XT keyboard it is, for the keyboard alone */
        h_caps = keepcaps | (1 << OS88_FSXM_VGA12);
        h_vidkind = OS88_VID_VGA;
        lem_mode_ok = lem_probe_mode(lem_win);
        h_h = 189;
        if (!lem_state_grey(LEM_ROW_MODE))
            fail("a VGA with an 83-key keyboard does not grey the Mode row");
        lem_show_fact(lem_win, lem_state_label(LEM_ROW_MODE),
                      lem_state_fact(LEM_ROW_MODE), -1);
        if (strstr(lem_para, "0x000F") || !strstr(lem_para, "F11"))
            fail("a VGA's Mode row says a mode sentence that is not true of "
                 "it, or not the keyboard's");
        lem_back(lem_win);
        h_bda96 = 0x10;
        if (lem_state_grey(LEM_ROW_MODE))
            fail("a VGA with an AT keyboard greys the Mode row");
        /* ...and the Music row carries the speaker's fact where it is true */
        lem_snd = 0;
        lem_show_fact(lem_win, lem_state_label(LEM_ROW_MUSIC),
                      lem_state_fact(LEM_ROW_MUSIC), -1);
        if (!strstr(lem_para, "ADLIB.DAT") ||
            !strstr(lem_para, "os88_snd_caps()"))
            fail("the Music row on a machine with no tone path does not name "
                 "both its facts");
        lem_back(lem_win);
        lem_snd = 1;
        lem_show_fact(lem_win, lem_state_label(LEM_ROW_MUSIC),
                      lem_state_fact(LEM_ROW_MUSIC), -1);
        if (strstr(lem_para, "os88_snd_caps()"))
            fail("the Music row names a speaker fact on a machine that has "
                 "the tone path");
        lem_back(lem_win);

        /* --- 'f' on the preview keeps the level; the door back in draws the
         * preview again WITHOUT composing ------------------------------- */
        h_pv_keys[0] = 'f';
        h_pv_keys[1] = 0;
        lem_sel = 0;
        lem_screen = LEM_SC_LIST;
        ovl_launch(lem_win);
        if (lem_exit != LEM_X_SUSPEND || !lem_suspended ||
            lem_screen != LEM_SC_PREVIEW || lem_started)
            fail("'f' on the preview did not keep the level (SPEC.md "
                 "11.2.1's door)");
        before = h_r_worldpix;
        h_pv_keys[0] = 0;
        ovl_launch(lem_win);            /* the door back in, Esc on it */
        printf("  'f' on the preview: kept, then back in with %d world "
               "pixels composed again\n", h_r_worldpix - before);
        if (h_r_worldpix != before)
            fail("bringing a kept preview back composed the level again");
        if (lem_exit != LEM_X_MENU || lem_suspended)
            fail("Esc on the kept level's preview did not let it go");

        /* --- the result is RECORDED inside the bracket; a write the disk
         * refuses is said OUTSIDE it, in 24 characters ------------------ */
        if (!ovl_level_load(0))
            fail("the progress row could not load level 1");
        lem_playing = 0;
        lem_rating = 0;
        lem_started = 1;
        lem_saved = 10;
        lem_prog[0] = -1;
        lem_savable = 1;
        lem_savfail = 0;
        h_writes = 0;
        h_toasts = 0;
        h_po_keys[0] = 0;
        ovl_po_screen();
        if (h_writes != 1 || lem_progwrite || !lem_po_saved || h_toasts)
            fail("the bracket's postview did not record the level while it "
                 "was read, or toasted inside the bracket");
        lem_prog[0] = -1;
        h_write_fail = 1;
        ovl_po_screen();
        h_write_fail = 0;
        if (!lem_savfail || h_toasts)
            fail("a write the disk refused inside the bracket was not held "
                 "for the window, or was toasted into the foreign mode");
        ovl_said_save();
        if (h_toasts != 1 || strcmp(h_lasttoast, lem_str(LEMS_SAV_FAIL)) ||
            lem_savfail)
            fail("the held write failure was not said once, as SAV_FAIL");
        lem_started = 0;
        lem_onscr = 0;
        lem_savable = 1;
        lem_abandon();

        h_caps = keepcaps;
        h_vidkind = keepkind;
        lem_mode_ok = lem_probe_mode(lem_win);
        h_pv_keys[0] = 0x1C0D;
        h_pv_keys[1] = 0;
    }

    if (g_bad) {
        printf("lemtest: %d FAILURES\n", g_bad);
        return 1;
    }
    printf("lemtest: ok\n");
    return 0;
}
