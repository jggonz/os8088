/* ============================================================================
 * os8088 - apps/cword/hosttest/cwuitest.c      CWORD's redraw, on the host
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and apps/cword/build.sh compiles and runs it BEFORE the target build.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * cword's redraw path is a damage model: it keeps a shadow of what is on the
 * glass, lays out only the rows that can have changed, stops the layout the
 * moment the rest of the page provably did not move, and draws only the
 * columns that differ (see the head of apps/cword/cword.c for the numbers
 * that force it). Every one of those decisions is a chance to leave a cell
 * un-repainted - and PERFORMANCE.md is explicit that a stale cell and a
 * double draw are two of the three defects an emulator cannot show you. A
 * screenshot of QEMU proves nothing about them.
 *
 * So this stubs the whole os8088 API with a MODEL OF THE GLASS: os88_font_run()
 * records the character it put in a cell and clears that cell whole, as an
 * opaque run does; os88_font_str_xparent() records overstrike ink at the pixel level;
 * os88_gfx_hline() and os88_gfx_fill() clear or set it; os88_gfx_scroll()
 * moves it and fills the vacated rows with GARBAGE, which is legal (SPEC.md
 * 5.5 says their content is unspecified) and is what catches an app that
 * assumes they came up blank.
 *
 * Then it drives the program the way a user does - keystrokes, arrows, menu
 * commands, clicks, a save and a reopen - and after every single one rebuilds
 * what the screen OUGHT to show from the document, using a word wrap written
 * separately from the app's, and compares it cell for cell and pixel row for
 * pixel row.
 *
 * TWO REAL DEFECTS CAME OUT OF IT, and neither would have shown in a
 * screendump: an attribute strip (the 1-px rows above and below the glyph
 * band that italic and underline draw in) surviving a Clear All, and a pair
 * of white lines on the scroll path that were drawing pixels a later step
 * drew again.
 *
 * IT ALSO COUNTS THE CALLS, which is the cost model the file header of
 * cword.c states. The table it prints on every build is where those numbers
 * come from; PERFORMANCE.md's two constants (756 us a call, ~900 us a glyph
 * cell) turn them into milliseconds on the target machine.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT CANNOT SEE, so that nobody reads more into a pass than is there
 * ---------------------------------------------------------------------------
 *  - `int` is four bytes here and two on the target. This is the LOGIC, not
 *    the arithmetic widths (cwrtfio.c's byte-at-a-time property accessors
 *    exist for the same reason).
 *  - There is no font. A glyph is modelled as "the cell holds this character",
 *    and overstrike ink as "these pixels are inked". The one place that
 *    matters is the single pixel column a bold overstrike could push into the
 *    next cell: the kernel's face is the IBM ROM 8x8 set, whose printable
 *    glyphs leave column 7 blank, so the model inks seven columns and the
 *    check below asserts that NOTHING leaks across a cell boundary.
 *  - The caret is XOR ink and lives in its own layer here; the harness counts
 *    its two calls per keystroke but does not check where it lands.
 *  - It runs one window geometry (640x480, 69x24 cells). The 1bpp adapters
 *    are 640x200 and would give fewer rows; nothing here is adapter specific,
 *    but nothing here proves that either.
 *
 * ---------------------------------------------------------------------------
 * HOW IT IS BUILT
 * ---------------------------------------------------------------------------
 * apps/cword/hosttest/os88.h is a STUB of apps/cc/os88.h: the same structs,
 * the same constants and the prototypes cword.c actually calls, without the
 * poisoning of long/float that the target header does (the host needs
 * printf). It is a second copy of an interface and it will drift - and when
 * it does, this fails to COMPILE, which is the failure you want. The compile
 * line puts this directory ahead of apps/cc on the include path:
 *
 *     cc -DCW_RTF_SELFCHECK=1 -I apps/cword/hosttest -I apps/cword \
 *        -o build/cwuitest apps/cword/hosttest/cwuitest.c && build/cwuitest
 *
 * CW_RTF_SELFCHECK is defined for one reason: it is what tells cwrtftbl.c to
 * skip the two assertions that a table row is 6 and 4 bytes, which are true
 * of the target and false of a 64-bit host.
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GW 80                   /* cells across (640 px) */
#define GH 480                  /* pixel rows */

static char cellch[GW][GH];     /* the character font_run left in a cell whose
                                 * glyph band starts at that pixel row */
static unsigned char pixover[GW * 8][GH];   /* ink from font_str */
static unsigned char pixline[GW * 8][GH];   /* ink from hline/frame */

static int n_font_run, n_font_str, n_hline, n_fill, n_frame, n_xor, n_scroll;
static int n_cells;
static int pen;

static void gclear(void)
{
    memset(cellch, ' ', sizeof(cellch));
    memset(pixover, 0, sizeof(pixover));
    memset(pixline, 0, sizeof(pixline));
}

static void callreset(void)
{
    n_font_run = n_font_str = n_hline = n_fill = n_frame = n_xor = n_scroll = 0;
    n_cells = 0;
}

static int calls(void)
{
    return n_font_run + n_font_str + n_hline + n_fill + n_frame + n_xor +
           n_scroll;
}

/* --- the fake window ------------------------------------------------------ */
static int win_x = 80, win_y = 28, win_w = 592, win_h = 344;   /* frame */
static int cont_x, cont_y, cont_w, cont_h;
static int obscured;
static char last_toast[128];
static char last_title[64];

/* --- a one-file fake disk ------------------------------------------------- */
static char disk_name[16];
static char disk_data[40000];
static int  disk_len;
static int  ferr;

#include "os88.h"

void os88_set_color(int c) { pen = c; }

void os88_gfx_hline(int x1, int x2, int y)
{
    int x;
    n_hline++;
    if (y < 0 || y >= GH) return;
    for (x = x1; x <= x2; x++) {
        if (x < 0 || x >= GW * 8) continue;
        if (pen == OS88_WHITE) { pixline[x][y] = 0; pixover[x][y] = 0; }
        else pixline[x][y] = 1;
    }
}

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int x, y, c;
    n_fill++;
    for (y = y1; y <= y2; y++) {
        if (y < 0 || y >= GH) continue;
        for (x = x1; x <= x2; x++) {
            if (x < 0 || x >= GW * 8) continue;
            if (pen == OS88_WHITE) { pixline[x][y] = 0; pixover[x][y] = 0; }
            else pixline[x][y] = 1;
        }
    }
    /* a fill also destroys any glyph whose band it wholly covers */
    for (c = x1 / 8; c <= x2 / 8 && c < GW; c++)
        for (y = 0; y + 7 < GH; y++)
            if (y >= y1 && y + 7 <= y2)
                cellch[c][y] = (pen == OS88_WHITE) ? ' ' : '#';
}

void os88_gfx_frame(int x1, int y1, int x2, int y2)
{
    n_frame++;
    (void)x1; (void)y1; (void)x2; (void)y2;
}

void os88_gfx_xor_fill(int x1, int y1, int x2, int y2)
{
    n_xor++;
    (void)x1; (void)y1; (void)x2; (void)y2;   /* the caret: its own layer */
}

int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int x, y, c, by;
    n_scroll++;
    if ((x1 & 7) != 0 || ((x2 + 1) & 7) != 0) {
        printf("HARNESS: gfx_scroll refused: x1=%d x2=%d not byte aligned\n",
               x1, x2);
        return -1;
    }
    if (dy > 0) {
        for (y = y1; y <= y2 - dy; y++)
            for (x = x1; x <= x2; x++) {
                pixover[x][y] = pixover[x][y + dy];
                pixline[x][y] = pixline[x][y + dy];
            }
        for (by = y1; by <= y2 - dy; by++)
            for (c = x1 / 8; c <= x2 / 8; c++)
                cellch[c][by] = cellch[c][by + dy];
        /* the vacated rows are UNSPECIFIED (SPEC.md 5.5). Fill them with
         * garbage, which is legal and is what catches an app that assumes
         * they came up blank. */
        for (y = y2 - dy + 1; y <= y2; y++)
            for (x = x1; x <= x2; x++) { pixover[x][y] = 1; pixline[x][y] = 1; }
        for (by = y2 - dy + 1; by <= y2; by++)
            for (c = x1 / 8; c <= x2 / 8; c++) cellch[c][by] = '?';
    } else {
        dy = -dy;
        for (y = y2; y >= y1 + dy; y--)
            for (x = x1; x <= x2; x++) {
                pixover[x][y] = pixover[x][y - dy];
                pixline[x][y] = pixline[x][y - dy];
            }
        for (by = y2; by >= y1 + dy; by--)
            for (c = x1 / 8; c <= x2 / 8; c++)
                cellch[c][by] = cellch[c][by - dy];
        for (y = y1; y < y1 + dy; y++)
            for (x = x1; x <= x2; x++) { pixover[x][y] = 1; pixline[x][y] = 1; }
        for (by = y1; by < y1 + dy; by++)
            for (c = x1 / 8; c <= x2 / 8; c++) cellch[c][by] = '?';
    }
    return 0;
}

void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int i, c, xx, yy;
    n_font_run++;
    (void)ink; (void)paper;
    for (i = 0; s[i]; i++) {
        n_cells++;
        c = x / 8 + i;
        if (c < 0 || c >= GW || y < 0 || y + 7 >= GH) continue;
        cellch[c][y] = s[i];
        for (yy = y; yy <= y + 7; yy++)
            for (xx = c * 8; xx < c * 8 + 8; xx++) {
                pixover[xx][yy] = 0;    /* opaque: the cell is repainted whole */
                pixline[xx][yy] = 0;
            }
    }
}

void os88_font_str_xparent(int x, int y, const char *s)
{
    int i, xx, yy, px;
    n_font_str++;
    for (i = 0; s[i]; i++) {
        n_cells++;
        px = x + i * 8;
        for (yy = y; yy <= y + 7; yy++) {
            if (yy < 0 || yy >= GH) continue;
            /* SEVEN columns, not eight: the kernel's face is the IBM ROM 8x8
             * set, whose printable glyphs leave column 7 blank as the spacing
             * column (underscore is the exception). An overstrike at x+1
             * therefore stays inside its own cell, which is what makes the
             * bleed check below an assertion that nothing leaks rather than a
             * measurement of a font this harness does not have. */
            for (xx = px; xx < px + 7; xx++) {
                if (xx < 0 || xx >= GW * 8) continue;
                pixover[xx][yy] = 1;
            }
        }
    }
}

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{
    win_x = x; win_y = y; win_w = w; win_h = h;
    cont_x = ((x + 1) + 7) & ~7;        /* snapped to a multiple of 8 */
    cont_y = y + 18;
    cont_w = w - 2;
    cont_h = h - 19;
    strncpy(last_title, title, sizeof(last_title) - 1);
    return (void *)0x1234;
}
void os88_wm_content(void *win, struct os88_pt *o) { (void)win; o->x = cont_x; o->y = cont_y; }
int  os88_wm_geom(void *win, struct os88_size *s) { (void)win; s->w = cont_w; s->h = cont_h; return 0; }
int  os88_wm_obscured(void *win) { (void)win; return obscured; }
void os88_wm_title(void *win, const char *s)
{ (void)win; strncpy(last_title, s, sizeof(last_title) - 1); }
void os88_wm_snap(void *win, int on) { (void)win; (void)on; }
void os88_menu_set(void *win, struct os88_menuset *set) { (void)win; (void)set; }
void os88_about_set(void *win) { (void)win; }

/* The API the Word port added on top of what the demonstrator used. The
 * mouse, the tasks and the BIOS byte are stubbed to a machine where nothing
 * moves and nothing is pressed, which is what a keystroke harness wants: the
 * press-drag-release tracker returns immediately and the worker never runs. */
void os88_wm_sizable(void *win, int on) { (void)win; (void)on; }
void os88_wm_destroy(void *win) { (void)win; }
void os88_wm_hide(void *win) { (void)win; }
void os88_wm_onmouseup(void *win) { (void)win; }
void os88_mouse(struct os88_mouse *m) { m->x = 0; m->y = 0; m->btn = 0; }
void os88_task_yield(void) { }
void os88_gfx_lock(void) { }
void os88_gfx_unlock(void) { }
void os88_task_sleep(int ticks) { (void)ticks; }
int  os88_task_spawn(void *win) { (void)win; return -1; }
void os88_task_alive(void *win) { (void)win; }
int  os88_peek(unsigned seg, unsigned off) { (void)seg; (void)off; return 0; }
void os88_gfx_pen(int disabled) { (void)disabled; }
void os88_gfx_vline(int x, int y1, int y2) { os88_gfx_fill(x, y1, x, y2); }
/* The arbitrary-angle line (SPEC.md 5.6), which the menu check is two of. The
 * model is a Bresenham walk rather than a bounding fill: the harness audits the
 * glass cell for cell, and a stub that blackened the whole box would report a
 * check mark as damage over the label beside it. */
void os88_gfx_line(int x1, int y1, int x2, int y2, int dilate)
{
    int dx, dy, sx, sy, err, e2;

    (void)dilate;
    n_fill++;                           /* one drawing call, whatever it draws */
    dx = x2 > x1 ? x2 - x1 : x1 - x2;
    dy = y2 > y1 ? y2 - y1 : y1 - y2;
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        if (x1 >= 0 && x1 < GW * 8 && y1 >= 0 && y1 < GH)
            pixline[x1][y1] = 1;
        if (x1 == x2 && y1 == y2)
            break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}
void os88_gfx_pixel(int x, int y) { os88_gfx_fill(x, y, x, y); }
void os88_font_char_xparent(int x, int y, int ch)
{ char b[2]; b[0] = (char)ch; b[1] = 0; os88_font_str_xparent(x, y, b); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
/* os88_gfx_blit1 - ONE call for a band the app composed itself (SPEC.md
 * 5.4.2). Modelled rather than refused, because the ruler's scale goes through
 * it and a stub that always refuses measures the FALLBACK path - 71 drawing
 * calls where the machine makes one - and reports it as the cost of the
 * program. It checks the alignment the real primitive requires, which is the
 * other half of what a stub is for. */
int  os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *p = (const unsigned char *)bits;
    int r, c;

    n_fill++;                           /* one drawing call, whatever it draws */
    if ((x & 7) != 0 || (w & 7) != 0) {
        printf("HARNESS: gfx_blit1 refused: x=%d w=%d not byte aligned\n", x, w);
        return -1;
    }
    for (r = 0; r < rows; r++) {
        for (c = 0; c < w; c++) {
            int px = x + c;
            int py = y + r;
            if (px < 0 || px >= GW * 8 || py < 0 || py >= GH) continue;
            if (p[r * stride + (c >> 3)] & (0x80 >> (c & 7)))
                pixline[px][py] = 1;
        }
    }
    return 0;
}

void os88_video(struct os88_video *v)
{ v->w = 640; v->h = 480; v->dock_top = 440; v->kind = OS88_VID_VGA; v->bpp = 4; }

unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    unsigned n;
    if (strcmp(name, disk_name) != 0) { ferr = OS88_FERR_NOENT; return 0; }
    n = (unsigned)disk_len;
    if (n > cap) n = cap;
    memcpy(buf, disk_data, n);
    ferr = 0;
    return n;
}
int os88_file_write(const char *name, const void *buf, unsigned count)
{
    strncpy(disk_name, name, sizeof(disk_name) - 1);
    memcpy(disk_data, buf, count);
    disk_len = (int)count;
    ferr = 0;
    return 0;
}
int os88_ferr(void) { return ferr; }
int os88_file_dlg(int mode, void *win, const char *defname)
{ (void)mode; (void)win; (void)defname; return 0; }
int os88_arg_file(char *name13, struct os88_place *p)
{ (void)name13; (void)p; return -1; }
int os88_file_goto(struct os88_place *p) { (void)p; return 0; }
int os88_assoc_set(const char *e, const char *s) { (void)e; (void)s; return 0; }

static char clipbuf[4096];
static int clipn = -1;
int os88_clip_put(const void *t, unsigned len)
{ memcpy(clipbuf, t, len); clipn = (int)len; return 0; }
int os88_clip_get(void *buf, unsigned cap)
{ unsigned n = (unsigned)(clipn < 0 ? 0 : clipn); if (n > cap) n = cap;
  memcpy(buf, clipbuf, n); return (int)n; }
int os88_clip_size(void) { return clipn; }
int os88_toast(const char *text, int ticks)
{ (void)ticks; strncpy(last_toast, text, sizeof(last_toast) - 1); return 0; }

void os88_strcpy(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    if (cap == 0) return;
    while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
char *os88_utoa(unsigned v, char *dst)
{
    char t[8]; int i = 0, j = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 6);
    while (i > 0) dst[j++] = t[--i];
    dst[j] = 0;
    return dst;
}

/* the one hand-written routine, in C for the host */
void cw_memmove(void *dst, const void *src, unsigned n) { memmove(dst, src, n); }

/* ==========================================================================
 * THE TYPE LIBRARY (apps/cword/cwtype.inc), STUBBED AS FACE 0
 *
 * The library is apps/os88type.inc, hand-written 8086 assembly that reads a
 * `.F88` off a floppy and composes proportional glyphs into a 1bpp band. None
 * of that can run here, and none of it needs to: FACE 0 IS THE KERNEL'S 8x8
 * CELL AND IS ALWAYS OPEN (SPEC.md 6.5), so a host that answers "one family,
 * which is the fixed one" exercises exactly the code path this harness is for
 * - the fixed-face layout, whose pixel output it audits cell for cell.
 *
 * WHAT THIS DOES AND DOES NOT PROVE, stated because a pass here is easy to
 * read as more than it is. It proves the proportional conversion did not
 * change the FIXED face: every `cw_cx()` / `cw_xc()` site is walked with
 * cw_prop clear, and a shift replaced by a lookup that answers differently
 * shows up as a cell in the wrong column. It proves NOTHING about a chosen
 * face - the band, the compose and the blit are all on the target - which is
 * why SPEC.md 73.12.2's own testing is on three adapters with a face on each.
 *
 * cw_ty_scan() answering 0 is the honest model of a machine with no FONTS/
 * folder, and it is what keeps the Font list's refusal path exercised on every
 * build rather than only on a disk somebody remembered to break.
 * ========================================================================*/
void cw_ty_init(void) { }
int  cw_ty_rows(void) { return 8; }
int  cw_ty_lead(void) { return 0; }
int  cw_ty_adv(int ch) { (void)ch; return 8; }
int  cw_ty_use(int h, int style) { return (h == 0 && style == 0); }
void cw_ty_cache(void) { }
void cw_ty_band(int rows) { (void)rows; }
int  cw_ty_putn(int pen, const char *s, int n)
{ (void)s; return pen + n * 8; }
int  cw_ty_flush(int x, int y, int w, int rows)
{ (void)x; (void)y; (void)w; (void)rows; return 0; }   /* never drawn here */
/* ty_fit and ty_hit, which in the fixed face are the division and the
 * half-cell they replaced. Written out rather than answered as `px / 8` so
 * that the harness walks the same shape the library does - the count is
 * bounded by n at both ends, which is where an off-by-one would live. */
int  cw_ty_fit(const char *s, int n, int px)
{
    int i, w = 0;
    (void)s;
    for (i = 0; i < n; i++) {
        if (w + 8 > px) break;
        w += 8;
    }
    return i;
}
int  cw_ty_hit(const char *s, int n, int px)
{
    int i;
    (void)s;
    for (i = 0; i < n; i++) {
        if (px < i * 8 + 4) break;
    }
    return i;
}
void cw_ty_pen(const char *s, int n, int *px)
{
    int i;
    (void)s;
    for (i = 0; i <= n; i++) px[i] = i * 8;
}
int  cw_ty_scan(void) { return 0; }                    /* no FONTS/ folder */
void cw_ty_name(int i, char *dst) { (void)i; dst[0] = 0; }
int  cw_ty_open(int i) { (void)i; return 0; }          /* always refused */
void cw_ty_close(int h) { (void)h; }

#include "cword.c"

/* ==========================================================================
 * THE INDEPENDENT MODEL: what the screen ought to show
 * ========================================================================*/

static char  exp_ch[CW_ROWS_MAX][CW_COLS_MAX];
static unsigned char exp_at[CW_ROWS_MAX][CW_COLS_MAX];

/* A SECOND word wrap, written from the description and not from cw_wrap(), so
 * that a wrap bug is a disagreement rather than a shared assumption. */
static int mwrap(int s, int *pend)
{
    int i, lim;
    lim = s + cw_cols;
    if (lim > cw_len) lim = cw_len;
    for (i = s; i < lim; i++)
        if (cw_buf[i] == '\n') { *pend = i; return i + 1; }
    if (lim >= cw_len) { *pend = cw_len; return -1; }
    if (cw_buf[lim] == '\n' || cw_buf[lim] == ' ') { *pend = lim; return lim + 1; }
    for (i = lim - 1; i > s; i--)
        if (cw_buf[i] == ' ') { *pend = i; return i + 1; }
    *pend = lim;
    return lim;
}

static void model(void)
{
    int r, s, e, i, n;
    memset(exp_ch, ' ', sizeof(exp_ch));
    memset(exp_at, 0, sizeof(exp_at));
    s = cw_top;
    for (r = 0; r < cw_rows; r++) {
        if (s < 0) continue;
        n = 0;
        e = 0;
        {
            int nx = mwrap(s, &e);
            for (i = s; i < e && n < cw_cols; i++) {
                exp_ch[r][n] = cw_buf[i];
                exp_at[r][n] = cw_att[i];
                n++;
            }
            s = nx;
        }
    }
}

static int fails;

/* THE SHADOW IS A CLAIM ABOUT THE GLASS, and this checks it directly rather
 * than through the document. The two can disagree with no visible symptom -
 * the glass is right, the shadow merely believes something it did not draw -
 * and the damage model then SKIPS a real change later, somewhere else, for a
 * reason nothing at that moment explains. Reported once, because after the
 * first divergence everything downstream is a consequence. */
static int shadow_lied;

static void shadow_audit(const char *what)
{
    int r, c, y;

    if (shadow_lied)
        return;
    for (r = 0; r < cw_rows; r++) {
        y = cw_ty + r * cw_pitch;
        for (c = 0; c < cw_cols; c++) {
            char g = cellch[(cw_tx + c * 8) / 8][y];
            char sh = cw_sh[r * CW_SH_STRIDE + c];
            if (sh == (char)0xFF)
                continue;               /* deliberately impossible: a row the
                                         * app has marked for full redraw */
            if (g != sh) {
                printf("SHADOW %s: row %d col %d - glass '%c', shadow '%c'\n",
                       what, r, c, g, sh);
                shadow_lied = 1;
                return;
            }
        }
    }
}

static void verify(const char *what)
{
    int r, c, y, x, bad = 0;
    int gb, gi, gu, wb, wi, wu;

    model();
    shadow_audit(what);
    for (r = 0; r < cw_rows && !bad; r++) {
        y = cw_ty + r * cw_pitch;
        for (c = 0; c < cw_cols; c++) {
            x = cw_tx + c * 8;
            if (cellch[x / 8][y] != exp_ch[r][c]) {
                printf("FAIL %s: row %d col %d shows '%c' (%d), expected '%c'\n",
                       what, r, c, cellch[x / 8][y], cellch[x / 8][y],
                       exp_ch[r][c]);
                {   int k;
                    printf("     glass: [");
                    for (k = 0; k < cw_cols; k++)
                        putchar(cellch[(cw_tx + k * 8) / 8][y]);
                    printf("]\n     model: [");
                    for (k = 0; k < cw_cols; k++)
                        putchar(exp_ch[r][k]);
                    printf("]\n     rowspan %d..%d pap=%d w=%d first=%d showp=%d\n",
                           cw_ls[r], cw_le[r], cw_lp[r],
                           cw_pap_width(cw_lp[r], 1), 
                           (cw_ls[r] == 0 || cw_buf[cw_ls[r]-1] == 10), cw_showp);
                    cw_wrap(cw_ls[r], cw_lp[r]);
                    printf("     re-wrap says end=%d next=%d; from=%d stop=%d shstart=%d shend=%d\n",
                           cw_w_end, cw_w_next, cw_from, cw_stop,
                           cw_sh_start[r], cw_sh_end[r]);
                    printf("     shadow: [");
                    for (k = 0; k < cw_cols; k++)
                        putchar(cw_sh[r * CW_SH_STRIDE + k]);
                    printf("]\n     doc: ");
                    for (k = cw_ls[r]; k <= cw_le[r] && k < cw_len; k++)
                        printf("%c/%d ", cw_buf[k], cw_att[k]);
                    printf("\n");
                }
                bad = 1;
                break;
            }
            gi = pixover[x + 1][y - 1];
            gb = pixover[x + 1][y + 7];
            gu = pixline[x][y + 8];
            wb = (exp_at[r][c] & CW_A_BOLD) != 0;
            wi = (exp_at[r][c] & CW_A_ITAL) != 0;
            wu = (exp_at[r][c] & CW_A_ULINE) != 0;
            /* THE BLEED. A bold overstrike at x+1 puts ink in the FIRST
             * pixel column of the cell to its right, so cell c's leftmost
             * column at row y+7 is ink if and only if cell c-1 is bold. If a
             * repaint leaves that behind, this is what sees it. */
            if (c > 0) {
                int gbl = pixover[x][y + 7];
                /* Ink in cell c's first pixel column comes from cell c-1's
                 * bold overstrike - but only survives if the two cells were
                 * drawn in the same run, because the NEXT run's opaque
                 * font_run repaints cell c whole and wipes it. So the correct
                 * expectation is "the cell to the left is bold AND has the
                 * same formatting". A repaint that leaves bleed where the
                 * formatting differs is the artefact cw_render_row()'s
                 * one-column widening exists to remove. */
                int wbl = 0;            /* nothing may leak across a cell */
                if (gbl != wbl) {
                    printf("FAIL %s: row %d col %d bleed from the left is %d, "
                           "expected %d (left cell '%c' att %d, this cell '%c' "
                           "att %d)\n",
                           what, r, c, gbl, wbl, exp_ch[r][c - 1],
                           exp_at[r][c - 1], exp_ch[r][c], exp_at[r][c]);
                    bad = 1;
                    break;
                }
            }
            if (gb != wb || gi != wi || gu != wu) {
                printf("FAIL %s: row %d col %d ('%c') ink b=%d i=%d u=%d, "
                       "expected b=%d i=%d u=%d\n",
                       what, r, c, exp_ch[r][c], gb, gi, gu, wb, wi, wu);
                bad = 1;
                break;
            }
        }
    }
    if (bad) {
        fails++;
        printf("  (cw_len=%d cur=%d top=%d cols=%d rows=%d)\n",
               cw_len, cw_cur, cw_top, cw_cols, cw_rows);
    }
}

/* --- driving it ----------------------------------------------------------- */

static void *win;

static void key(int ascii, int scan) { os88_onkey(ascii, scan, win); }

static void typestr(const char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        key(s[i] == '\n' ? 13 : s[i], 0);
}

/* --- the cost table, which is the other half of what this program is for ---
 * Each row is a real keystroke on a real document, counted. The milliseconds
 * are PERFORMANCE.md's constants applied to the counts: 756 us for any gfx_*
 * call whatever it draws, ~900 us for one 8x8 glyph cell, on the 4.77 MHz
 * 8088 this system targets. */
static void cost(const char *what)
{
    printf("  %-44s %2d calls %5d cells  %6d us\n", what, calls(), n_cells,
           calls() * 756 + n_cells * 900);
}

static void cost_table(void)
{
    int i;

    cw_do(win, CWA_NEW);
    if (cw_dlg) { key(9, 0); key(13, 0); }   /* the save-changes prompt: No */
    gclear();
    os88_paint(win);

    printf("\nWHAT A KEYSTROKE COSTS ON THE TARGET (%d columns x %d rows)\n",
           cw_cols, cw_rows);
    callreset(); os88_paint(win);
    cost("a full repaint (W_PAINT)");

    /* A DOCUMENT THAT IS NOT AT THE CAP. Filling it to CW_DOC_MAX made every
     * line of the table below a measurement of the REFUSAL path - cw_type()
     * toasts and returns without inserting - so "typing at the end of the
     * document" was timing a keystroke that did not happen. 30 lines is about
     * two thirds of the document. */
    cw_do(win, CWA_NEW);
    if (cw_dlg) { key(9, 0); key(13, 0); }
    for (i = 0; i < 30; i++)
        typestr("The quick brown fox jumps over the lazy dog again and again "
                "and again. ");
    verify("the cost table's document");

    callreset(); key('x', 0);   cost("typing at the end of the document");
    callreset(); key(8, 0);     cost("backspace at the end of the document");

    key(0, CW_K_HOME);
    for (i = 0; i < 4; i++) key(0, CW_K_UP);
    key(0, CW_K_HOME);
    callreset(); key('x', 0);   cost("typing at the START of a full line");
    callreset(); key(8, 0);     cost("backspace there");
    key(0, CW_K_END);
    callreset(); key(13, 0);    cost("Enter in the middle of a paragraph");
    callreset(); key(8, 0);     cost("...and undoing it with a backspace");
    callreset(); key(0, CW_K_LEFT);
    cost("an arrow key that does not scroll");

    for (i = 0; i < 400; i++) key(0, CW_K_UP);
    for (i = 0; i < cw_rows - 1; i++) key(0, CW_K_DOWN);
    callreset(); key(0, CW_K_DOWN);
    cost("a caret move that scrolls one row");
    for (i = 0; i < cw_rows - 1; i++) key(0, CW_K_UP);
    callreset(); key(0, CW_K_UP);
    cost("...and scrolling back up");
    verify("after the cost table's scrolling");

    key(0, CW_K_END);
    ovl_chp_apply(win, CW_A_BOLD, 0);
    callreset(); key('B', 0);   cost("typing one BOLD character");
    ovl_chp_apply(win, CW_A_ULINE, 0);
    callreset(); key('U', 0);   cost("...bold and underlined");
    verify("after the cost table's formatting");
}

int main(void)
{
    int i;
    int worst;
    int n;

    gclear();
    win = os88_main();
    if (!win) { printf("FAIL: os88_main returned 0\n"); return 1; }
    os88_paint(win);
    printf("layout: cols=%d rows=%d tx=%d ty=%d  (content %dx%d at %d,%d)\n",
           cw_cols, cw_rows, cw_tx, cw_ty, cont_w, cont_h, cont_x, cont_y);
    callreset();
    os88_paint(win);
    printf("full repaint: %d calls, %d glyph cells  (= %d ms on a 4.77MHz "
           "8088)\n", calls(), n_cells,
           (int)((calls() * 756 + n_cells * 900) / 1000));
    verify("after the first paint");

    /* 1. typing at the end of a line: the cost claim in the file header */
    worst = 0;
    for (i = 0; i < 40; i++) {
        callreset();
        key('a' + (i % 26), 0);
        if (calls() > worst) worst = calls();
        verify("typing");
    }
    printf("typing 40 characters: worst keystroke %d calls\n", worst);
    callreset();
    key('!', 0);
    printf("one ordinary keystroke: %d calls (%d font_run, %d xor), "
           "%d glyph cells\n", calls(), n_font_run, n_xor, n_cells);
    verify("typing");

    /* 2. a paragraph long enough to wrap several times */
    typestr("\nThe quick brown fox jumps over the lazy dog, and then it does "
            "it again and again until the paragraph has to wrap at least "
            "three times across the width of this window. ");
    verify("wrapped paragraph");

    /* 3. typing in the MIDDLE of a wrapped paragraph - the expensive case,
     *    and the one where a wrap boundary moves */
    for (i = 0; i < 60; i++) key(0, CW_K_LEFT);
    callreset();
    key('Z', 0);
    printf("insert in the middle of a wrapped paragraph: %d calls, %d cells\n",
           calls(), n_cells);
    verify("insert mid-paragraph");
    for (i = 0; i < 20; i++) { key(0, CW_K_LEFT); verify("arrow left"); }
    for (i = 0; i < 25; i++) { key(0, CW_K_RIGHT); verify("arrow right"); }
    for (i = 0; i < 3; i++)  { key(0, CW_K_UP); verify("arrow up"); }
    for (i = 0; i < 3; i++)  { key(0, CW_K_DOWN); verify("arrow down"); }
    key(0, CW_K_HOME); verify("home");
    key(0, CW_K_END);  verify("end");

    /* 4. backspace, including over a paragraph end */
    for (i = 0; i < 30; i++) { key(8, 0); verify("backspace"); }
    key(0, CW_K_DEL); verify("delete");

    /* 5. formatting: bold, then italic, then underline, then plain */
    ovl_chp_apply(win, CW_A_BOLD, 0);
    typestr("BOLD ");
    verify("bold text");
    ovl_chp_apply(win, CW_A_ITAL, 0);
    typestr("bold+italic ");
    verify("bold and italic");
    ovl_chp_apply(win, CW_A_BOLD, 0);
    ovl_chp_apply(win, CW_A_ULINE, 0);
    typestr("italic+under ");
    verify("italic and underline");
    ovl_chp_apply(win, 0, 2);
    typestr("plain again and some more text to push the formatted run around ");
    verify("back to plain");

    /* 6. an edit BEFORE a formatted run, so the run moves along the row and
     *    the attribute ink has to move with it */
    for (i = 0; i < 90; i++) key(0, CW_K_LEFT);
    for (i = 0; i < 5; i++) { key('x', 0); verify("insert before a bold run"); }
    for (i = 0; i < 5; i++) { key(8, 0); verify("delete before a bold run"); }

    /* 6b. an edit exactly AT the start of a display row, and at a wrap
     *     boundary - the case the early stop's `s >= hi` guard is about */
    key(0, CW_K_HOME);
    for (i = 0; i < 3; i++) key(0, CW_K_UP);
    key(0, CW_K_HOME);
    for (i = 0; i < 6; i++) { key('Q', 0); verify("typing at a row start"); }
    for (i = 0; i < 6; i++) { key(8, 0); verify("deleting at a row start"); }
    key(0, CW_K_DOWN);
    key(0, CW_K_HOME);
    for (i = 0; i < 4; i++) { key('W', 0); verify("typing at a wrap boundary"); }
    for (i = 0; i < 4; i++) { key(8, 0); verify("deleting at a wrap boundary"); }

    /* 6c. THE UNDERLINE AND ITALIC STRIPS ARE OUTSIDE THE GLYPH BAND, so a
     *     full repaint that only lays down glyphs leaves them behind. Put
     *     formatted text on the first visible row, then empty the document
     *     while looking at it. */
    {
        static char keep[CW_DOC_MAX];
        static unsigned char keepa[CW_DOC_MAX];
        int keepn = cw_len, ki;
        memcpy(keep, cw_buf, keepn);
        memcpy(keepa, cw_att, keepn);

        cw_do(win, CWA_NEW);
        if (cw_dlg) { key(9, 0); key(13, 0); }
        ovl_chp_apply(win, CW_A_ITAL, 0);
        ovl_chp_apply(win, CW_A_ULINE, 0);
        typestr("italic and underlined on the very first row");
        verify("formatted text on row 0");
        /* File > New on a DIRTY document asks Word's question - "Do you want
         * to save changes to <name>?" - and this is the answer. The
         * demonstrator armed and discarded on two presses of one menu item;
         * this is the product's prompt, and the harness has to work it: the
         * focus opens on Yes, Tab moves to No, Enter presses it. Leaving the
         * prompt up instead was silent and expensive - the panel covered the
         * text band, the next paint drew the document and then drew the panel
         * over it, and the shadow went on claiming cells the glass no longer
         * had. */
        cw_do(win, CWA_NEW);
        if (cw_dlg) { key(9, 0); key(13, 0); }
        verify("Clear All must take the attribute strips with it");
        ovl_chp_apply(win, 0, 2);

        for (ki = 0; ki < keepn; ki++)
            ovl_put(keep[ki], keepa[ki]);
        cw_cur = cw_len;
        cw_top = 0;
        os88_paint(win);
        verify("document restored");
    }

    /* 7. enough paragraphs to scroll, one row at a time */
    key(0, CW_K_END);
    for (i = 0; i < cw_rows + 6; i++) {
        callreset();
        key(13, 0);
        typestr("line");
        verify("scrolling");
    }
    callreset();
    key(13, 0);
    printf("a keystroke that scrolls the view: %d calls (%d scroll, %d "
           "font_run, %d hline), %d cells\n",
           calls(), n_scroll, n_font_run, n_hline, n_cells);
    verify("after a scroll");
    for (i = 0; i < cw_rows + 10; i++) { key(0, CW_K_UP); verify("scroll back up"); }

    /* 8. clicking to place the caret */
    os88_onclick(cw_tx + 3 * 8 + 2, cw_ty + 2 * cw_pitch + 3, win);
    verify("click");
    os88_onclick(cw_tx + 900, cw_ty + 4000, win);   /* far outside */
    verify("click outside");

    /* 9. save, then clear, then open it back - the whole round trip through
     *    the real reader and writer, driven the way a user drives it */
    {
        int savedlen = cw_len;
        static char saved[CW_DOC_MAX];
        static unsigned char saveda[CW_DOC_MAX];
        memcpy(saved, cw_buf, savedlen);
        memcpy(saveda, cw_att, savedlen);

        os88_onfile(OS88_FDLG_SAVE, "TEST.RTF", 0, 0, win);
        verify("after save");
        if (disk_len <= 0) { printf("FAIL: nothing was written\n"); fails++; }

        cw_do(win, CWA_NEW);
        if (cw_dlg) { key(9, 0); key(13, 0); }
        verify("after New");
        if (cw_len != 0) { printf("FAIL: New did not empty the document\n"); fails++; }

        os88_onfile(OS88_FDLG_OPEN, "TEST.RTF", (unsigned)disk_len, 0, win);
        verify("after open");
        if (cw_len != savedlen) {
            printf("FAIL: reopened document is %d characters, saved %d\n",
                   cw_len, savedlen);
            fails++;
        } else {
            for (i = 0; i < savedlen; i++) {
                if (cw_buf[i] != saved[i] || cw_att[i] != saveda[i]) {
                    printf("FAIL: reopened document differs at %d: '%c'/%d vs "
                           "'%c'/%d\n", i, cw_buf[i], cw_att[i], saved[i],
                           saveda[i]);
                    fails++;
                    break;
                }
            }
        }
        printf("save/reopen: %d characters -> %d bytes of RTF -> %d "
               "characters, formatting intact\n", savedlen, disk_len, cw_len);
    }

    /* 9b. the clipboard: Copy All, move, Paste. The text arrives in the
     *     caret's current formatting, which is what the Edit menu's toast
     *     says, and the pasted run has to appear on the glass. */
    {
        int before = cw_len;

        /* SELECT FIRST, which is a real change from the demonstrator: it had
         * `Copy All` on its Edit menu, and this is Word's Edit menu, where Cut
         * and Copy act on the selection and are greyed without one. F8 arms
         * extend-selection and the arrows extend it, which is the keyboard
         * route a user actually has (SPEC.md 68.2). */
        cw_cur = cw_len;                /* the previous step left it at 0,
                                         * and a selection that starts and
                                         * ends there copies nothing */
        cw_show(win, -2, 0, 0);
        key(0, CW_K_F8);
        for (i = 0; i < 30; i++) key(0, CW_K_LEFT);
        cw_do(win, CWA_COPY);
        cw_sel_on = 0;
        cw_ext = 0;
        cw_show(win, -1, 0, 0);
        printf("clipboard: %d characters copied, \"%s\"\n",
               os88_clip_size(), last_toast);
        for (i = 0; i < 20; i++) key(0, CW_K_LEFT);
        cw_do(win, CWA_PASTE);
        verify("after a paste");
        if (cw_len <= before) {
            printf("FAIL: the paste added nothing (%d -> %d)\n",
                   before, cw_len);
            fails++;
        }
        while (cw_len > before) { key(8, 0); verify("undo-paste backspace"); }
        verify("after undoing the paste by hand");
    }

    /* 9c. the keyboard shortcuts that are not menu commands */
    ovl_chp_apply(win, 0, 2);
    key(CW_C_BOLD, 0);
    if (!(cw_attr & CW_A_BOLD)) { printf("FAIL: Ctrl-B did not set bold\n"); fails++; }
    key(CW_C_ULINE, 0);
    if (!(cw_attr & CW_A_ULINE)) { printf("FAIL: Ctrl-U did not underline\n"); fails++; }
    key(CW_C_BOLD, 0);
    key(CW_C_ULINE, 0);
    if (cw_attr != 0) { printf("FAIL: the shortcuts do not toggle off\n"); fails++; }
    verify("after the shortcuts");

    /* 10. refusing a file that is too big, and one that is not RTF */
    last_toast[0] = 0;
    n = cw_len;
    os88_onfile(OS88_FDLG_OPEN, "TEST.RTF", 99999, 0, win);
    printf("too-big refusal: \"%s\"\n", last_toast);
    if (cw_len != n) { printf("FAIL: a refused open changed the document\n"); fails++; }
    verify("after a refused open");

    strcpy(disk_name, "PLAIN.TXT");
    strcpy(disk_data, "just some text, not RTF at all");
    disk_len = 30;
    last_toast[0] = 0;
    os88_onfile(OS88_FDLG_OPEN, "PLAIN.TXT", 30, 0, win);
    printf("not-RTF refusal: \"%s\"\n", last_toast);
    if (cw_len != n) { printf("FAIL: a non-RTF open changed the document\n"); fails++; }
    verify("after a non-RTF open");

    /* 11. filling the document to its cap */
    while (cw_len < CW_DOC_MAX) key('q', 0);
    last_toast[0] = 0;
    key('q', 0);
    printf("full-document refusal: \"%s\" (len %d)\n", last_toast, cw_len);
    if (cw_len != CW_DOC_MAX) { printf("FAIL: typed past the cap\n"); fails++; }
    verify("at the cap");

    /* 12. the About panel, and the repaint that takes it down.
     *
     * ESC AND NOT A CLICK, which is a behaviour change worth stating: the
     * panel is a MODAL dialog now (it lives in the overlay, SPEC.md 73.14),
     * so a click on its background goes nowhere - that is what modal means,
     * and it is what the product does. Dismissing it with a click was how the
     * demonstrator's About worked, and leaving that here did not fail HERE:
     * it left the panel up, and the next fourteen steps typed into a dialog
     * instead of into the document, reporting a stale screen at each one. A
     * harness that cannot get out of a dialog reports the whole rest of the
     * run as broken. */
    os88_about(win);
    key(27, 0x01);
    verify("after the About panel");

    /* 13. obscured: the app must not draw, and must recover on the next paint */
    obscured = 1;
    callreset();
    key('w', 0);
    if (calls() != 0) { printf("FAIL: drew %d calls while obscured\n", calls()); fails++; }
    obscured = 0;
    os88_paint(win);
    verify("after being uncovered");

    cost_table();

    if (fails) { printf("\ncwordui: %d FAILURE(S)\n", fails); return 1; }
    printf("\ncwordui: all checks passed\n");
    return 0;
}
