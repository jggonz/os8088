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
 * (SPEC.md 92.12).
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

int os88_toast(const char *text, int ticks)
{
    (void)ticks;
    h_toasts++;
    strncpy(h_lasttoast, text, sizeof(h_lasttoast) - 1);
    h_lasttoast[sizeof(h_lasttoast) - 1] = 0;
    return 0;
}

int os88_fsx_caps(void *win, unsigned char *kind)
{
    (void)win;
    *kind = (unsigned char)h_vidkind;
    return h_caps;
}
/* THE BRACKET IS DRIVEN AND NOT REFUSED. A stub that answers -1 measures the
 * refusal path, which is LESSONS.md 7's first harness trap: os88_fsx_run()
 * CALLS os88_fsx_main() the way the kernel does, os88_fsx_mode() answers a
 * modelled FSI, and os88_fsx_key() replays a scripted key list so the loop
 * runs a bounded number of ticks and leaves. h_fsx_refuse turns the refusal
 * back on for the one row that is about it. */
static int h_fsx_refuse;
static int h_fsx_runs;
static int h_fsx_ticks;
static int h_fsx_maxticks = 4;
static int h_fsx_keys[8];
static int h_fsx_keyn;
static int h_fsx_keyi;
static int h_fullscreen;

int os88_fsx_run(void *w, int f)
{
    (void)f;
    if (h_fsx_refuse)
        return -1;
    h_fsx_runs++;
    h_fsx_ticks = 0;
    h_fsx_keyi = 0;
    os88_fsx_main(w);
    return 0;
}

int os88_fsx_mode(int id, struct os88_fsi *f)
{
    if (h_fsx_refuse || (h_caps & (1 << id)) == 0)
        return -1;
    f->seg = 0xA000;
    f->w = (id == OS88_FSXM_HERC) ? 720 : 320;
    f->h = (id == OS88_FSXM_HERC) ? 348 : 200;
    f->stride = 40;
    f->flags = 2;
    f->bpp = 4;
    f->banks = 1;
    f->pages = 1;
    f->bstep = 0;
    f->mode = (unsigned char)id;
    f->rsvd = 0;
    return 0;
}

int os88_fsx_wait(int k)
{
    (void)k;
    h_fsx_ticks++;
    return 0;
}

int os88_fsx_key(int wait)
{
    (void)wait;
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
    (void)win;
    h_fullscreen = enter;
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

int os88_file_write(const char *name, const void *buf, unsigned count)
{
    h_writes++;
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
#include "lemmings.c"

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

int main(void)
{
    int i, seen, seenrows, tc, tn, tf;

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

    /* the first wake: the module, and the progress file */
    os88_onwake(lem_win);
    if (lem_ovl != 1)
        fail("the module did not come resident on the first wake");
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
    lem_sel = 1;
    glass_clear();
    repaint("the greyed row selected");
    if (glass_attr(LEM_LISTTOP + 1) != LEM_AT_GREY)
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

    /* --- Play on a playable row shows the preview screen ------------------- */
    lem_sel = 0;
    counters_reset();
    lem_play(lem_win);
    printf("  list -> preview %2d calls %4d cells (%d fill)\n",
           g_calls, g_cells, g_fills);
    if (g_fills != 1)
        fail("Play did not blank the glass before the preview screen");
    repaint("the preview screen");
    if (lem_screen != LEM_SC_PREVIEW)
        fail("Play did not show the preview screen");
    if (!glass_has("Fixture one"))
        fail("the preview screen does not name the level");
    if (!glass_has("Press mouse button to continue"))
        fail("the preview screen has no footer");
    os88_onkey(27, 0, lem_win);
    if (lem_screen != LEM_SC_LIST)
        fail("Esc did not leave the preview screen");

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
     * and SPEC.md 92.8's Save Progress cell says of the refusing case that "the
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
             "recorded by the RESULT (SPEC.md 92.8), not by opening the "
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
     * WAVE 2: THE RASTER (SPEC.md 92.4)
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
     * SPEC.md 92.6.1: claim RECORDS are a budget too, MEM_MAX is 20 on a
     * kern_small machine and it is SYSTEM-WIDE. */
    if (!lem_level_load(0))
        fail("the level's banks did not load off the fixture");
    printf("  claims: %d live, %d peak, %d KB of the harness's %d; the "
           "arithmetic the refusal quotes is %d KB\n",
           h_claims_now, h_claims_peak, h_mem_kb_used, H_MEM_KB, lem_need_kb());
    if (h_claims_peak > 6)
        fail("more than six claim records - SPEC.md 92.6.1 says three on a "
             "VGA and five on the two 1bpp adapters, and MEM_MAX is 20 on a "
             "kern_small machine SYSTEM-WIDE");
    if (lem_need_kb() != h_mem_kb_used)
        fail("the KB the refusal quotes is not the KB actually claimed - a "
             "sentence that is wrong is worse than no sentence (SPEC.md 47)");

    /* --- the composer puts the terrain where the RECORD says ---------------
     * The fixture's level 0 carries two terrain entries, both graphic 0 - the
     * 32x16 slab whose four planes are all 0xFF - at world (80, 120) and
     * (240, 120). The coordinates are rebuilt HERE from the format document
     * rather than read out of the program: x = ((v>>16) & 0x0FFF) - 16 and
     * y = ((v>>7) & 0x1FF) - 4. */
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);
    lem_world_clear();
    lem_compose_level(0);
    {
        int bad = 0, x, y, want, got;

        for (y = 118; y < 138; y++)
            for (x = 70; x < 290; x++) {
                want = (y >= 120 && y < 136 &&
                        ((x >= 80 && x < 112) || (x >= 240 && x < 272)));
                got = lem_has_pixel(x, y);
                if (want != got)
                    bad++;
            }
        if (bad)
            fail("the composed terrain is not where the level record says "
                 "(the two 32x16 slabs at world 80,120 and 240,120)");
        printf("  composer: %d world pixels drawn, %d mask pixels checked, "
               "%d wrong\n", h_r_worldpix, 20 * 220, bad);
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
    lem_frame_panel();
    /* THE PANEL AND A WORD BEFORE THE 25-SECOND WAIT (SPEC.md 92.7.1
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
    lem_frame_first();
    if (panel_ink(0, 14))
        fail("LOADING is still on the status line after the first frame");
    printf("  first frame: %d screen pixels, %d presents, %d written twice "
           "(the status line over the panel)\n",
           h_r_pixels, h_r_presents, h_twice());
    if (h_r_pixels == 0)
        fail("the first frame drew nothing at all");
    if (h_r_presents == 0)
        fail("the first frame never presented");

    h_r_reset();
    lem_frame_step();
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
     * lem_view_rect() now erases only what the new outline will not cover, so
     * the bound is ZERO and a real double-draw anywhere on the screen has
     * nothing to hide behind. */
    {
        int k, worst = 0, moved = 0, wcells = 0, wrects = 0;

        for (k = 0; k < 8; k++) {
            h_r_reset();
            lem_view_move(4);
            lem_frame_step();
            if (h_twice() > worst)
                worst = h_twice();
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
        /* THE CALL COUNT IS A COST (SPEC.md 92.7.1: 137 us of cdecl against
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
        if (wcells > 4 * (LEM_MM_H - 2) + 4)
            fail("the view rectangle moved more cells than its own two "
                 "columns and the leading cells of its two rows - the whole "
                 "outline is being redrawn again");
        if (moved != 0)
            fail("a VGA scroll wrote screen pixels - it is four `out`s to the "
                 "start address and the pel pan, and nothing else");
    }

    /* --- ...and the same frame on a CGA, where the scroll IS a copy --------
     * The two 1bpp backends have no hardware pan, so the identical frame costs
     * a 12,800-byte windowed compose and a blit of the rows that changed
     * (lemblit.inc). The point of running it here is that the ROW is the same
     * C: lemdraw.c never asks which card it is on. */
    /* THE C HAS TO BELIEVE IT TOO, and until this round it did not: the arm
     * set the RASTER's kind and left lem_kind_id() answering VGA, so the
     * "CGA" frame ran C that thought it was on a VGA. Nothing depended on it
     * while lemdraw.c never asked which card it was on; §92.4.4 rule 5's
     * minimap class and lem_frame_panel()'s scroll guard both ask now, and
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
    lem_world_clear();
    lem_frame_panel();
    lem_compose_level(0);
    lem_frame_first();
    frame_shows_world("first frame, CGA");
    printf("  first frame, CGA: %d screen pixels, %d presents\n",
           h_r_pixels, h_r_presents);
    h_r_reset();
    lem_view_move(4);
    lem_frame_step();
    printf("  scrolled frame, CGA: %d screen pixels, %d written twice\n",
           h_r_pixels, h_twice());
    if (h_r_pixels < 320 * 160)
        fail("a CGA scroll did not compose the whole 320x160 window - there "
             "is no hardware pan on that card");
    if (h_twice() != 0)
        fail("a CGA scrolling frame wrote a pixel twice");
    h_caps = (1 << OS88_FSXM_VGA0D) | (1 << OS88_FSXM_CGA320);
    lem_caps = h_caps;
    lem_vidkind = OS88_VID_VGA;
    lem_r_setup(LEM_RKIND_VGA, 0xA000, 0, 0);

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

    /* --- the bracket runs, and the launch refuses OUTSIDE it ---------------
     * SPEC.md 92.5: everything that can refuse happens in a window, because a
     * kernel toast in a foreign mode paints desktop geometry into the game's
     * framebuffer. */
    /* IT IS DEFERRED TO THE WAKE, and that is the largest of the four errands
     * os88_onwake() carries: LEMMAIN.LEM is 52,224 bytes and a style bank up
     * to 78,848 more, ~131 KB and tens of seconds of floppy at
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
    lem_play(lem_win);
    if (lem_screen != LEM_SC_PREVIEW)
        fail("Play did not reach the preview screen");
    h_reads = 0;
    h_wakes = 0;
    h_fsx_runs = 0;
    h_toasts = 0;
    counters_reset();
    os88_onkey(13, 0, lem_win);
    if (h_reads != 0)
        fail("a launch keystroke read a file - ~131 KB of band with the gfx "
             "lock HELD, which is SPEC.md 92.6's first table row put back one "
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
    lem_launch(lem_win);
    if (h_fsx_runs != 1)
        fail("the launch did not enter the bracket");
    if (!h_fsx_ticks)
        fail("the bracket never waited for a tick - the input poll and the "
             "cursor are once per TICK and unconditional");
    if (h_fullscreen)
        fail("the SPEC.md 11.2 fullscreen window was not taken down on the "
             "way out");

    /* ...and with no memory it refuses in the window, with the arithmetic. */
    lem_free_all();
    {
        unsigned hog[8];
        int k, n = 0;

        for (k = 0; k < 8; k++) {
            hog[k] = os88_mem_claim(64);
            if (hog[k])
                n++;
        }
        h_toasts = 0;
        h_fsx_runs = 0;
        lem_launch(lem_win);
        if (h_fsx_runs != 0)
            fail("the launch entered the bracket with no memory for the level");
        if (h_toasts == 0)
            fail("the launch refused silently - SPEC.md 47 wants the fact and "
                 "the arithmetic, said in a WINDOW");
        else
            printf("  refusal: %s\n", h_lasttoast);
        for (k = 0; k < 8; k++)
            if (hog[k])
                os88_mem_free(hog[k]);
        (void)n;
    }

    if (g_bad) {
        printf("lemtest: %d FAILURES\n", g_bad);
        return 1;
    }
    printf("lemtest: ok\n");
    return 0;
}
