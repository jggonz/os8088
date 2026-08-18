/* ============================================================================
 * os8088 - apps/runcpm/hosttest/rcuitest.c    RUNCPM's terminal, on the host
 *
 * Part of RUNCPM (SPEC.md 71), a reimplementation of RunCPM 6.9 by Marcelo
 * Dantas / "Mockba the Borg" (MIT licence, Copyright (c) 2017 Mockba the
 * Borg): the scripts below expect what RunCPM/cpm.h's C_READSTR, main.c's
 * banner and disk.h's _error print.
 *
 * A BUILD-HOST PROGRAM. It never runs on the 8086, it is not in the package,
 * and apps/runcpm/build.sh compiles and runs it BEFORE the target build; a
 * failure stops the build.
 *
 * WHY. rcterm.c is a damage model (SPEC.md 71.2): a shadow of the glass, a
 * per-row compare, one call per changed span, one gfx_scroll for N scrolled
 * lines. Every one of those decisions is a chance to leave a stale cell or to
 * draw a cell twice, and PERFORMANCE.md is explicit that neither shows in an
 * emulator's screendump. So this stubs the API with a MODEL OF THE GLASS -
 * font_run and blit1 record what they put in each cell, gfx_scroll moves it
 * and fills the vacated rows with GARBAGE (legal: SPEC.md 5.5), gfx_fill
 * whitens - includes the whole program (runcpm.c and its parts) against it,
 * feeds it byte streams the way the Z80 side will - and, from wave 3, drives
 * the BDOS through a SCRIPTED Z80 (rc_run answers a list of handoffs) so the
 * CCP's boot, the C_READSTR line editor, the error path and the exit are
 * exercised key by key against stubs of the claims, the files and the
 * worker - and after every step
 * asserts three things cell for cell: the glass shows the model, the shadow
 * describes the glass, and - for the scripted streams - the row reads what a
 * VT100 would have shown. Then it prints THE COST TABLE: calls and cells are
 * exact, and a 'model ms' column prices them PER PRIMITIVE from
 * PERFORMANCE.md's measurements (print_costs names the rates; the band's is
 * this package's own, from tests/rcband - Set 65) - a model, not a
 * measurement of the whole path, until a counter on the target replaces it.
 *
 * WHAT IT CANNOT SEE. `int` is four bytes here and two there; there is no
 * font - a glyph is 'this cell holds this character', and rc_band (assembly
 * on the target, apps/runcpm/rcband.inc) is modelled as identity: row 0 of
 * the band carries the character, row 1 the attribute, and the stub blit1
 * decodes them back. rc_band's real output is judged on the glass in QEMU.
 *
 * HOW IT IS BUILT. hosttest/os88.h is a STUB of apps/cc/os88.h with this
 * directory ahead of apps/cc on the include path:
 *
 *   cc -O1 -w -I apps/runcpm/hosttest -I apps/runcpm \
 *      -o build/rcuitest apps/runcpm/hosttest/rcuitest.c && build/rcuitest
 * ==========================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>

#define GW 84                    /* cells across: the content starts
                                  * one cell in and is 80 wide */
#define GH 480                   /* pixel rows */

static char gl_ch[GH][GW];       /* the character a call left in the cell
                                  * whose glyph band starts at pixel row y */
static unsigned char gl_rev[GH][GW];

static int n_run, n_blit, n_scroll, n_fill, n_blit_refused;
static int n_frame;              /* gfx_frame calls: FOUR fills each in the
                                  * kernel (two hlines, two vlines, SPEC.md
                                  * 11.95.2), priced so */
static int n_cells;              /* every cell put down, by any primitive */
static int n_run_cells;          /* ...of which font_run lettered */
static int n_blit_cells;         /* ...and blit1 emitted from a band */
static int n_scroll_rows;        /* pixel rows a gfx_scroll moved */
static int n_fill_rows;          /* pixel rows a gfx_fill whitened */
static int fill_w;               /* the last fill's width in pixels */
static int pen;
static int blit1_refuses;        /* model a kern_small machine */
static int scroll_refuses;

static void gclear(void)
{
    memset(gl_ch, ' ', sizeof(gl_ch));
    memset(gl_rev, 0, sizeof(gl_rev));
}

static void callreset(void)
{
    n_run = n_blit = n_scroll = n_fill = n_blit_refused = n_frame = 0;
    n_cells = n_run_cells = n_blit_cells = n_scroll_rows = n_fill_rows = 0;
}

static int calls(void) { return n_run + n_blit + n_scroll + n_fill + n_frame * 4; }

/* --- the fake window ------------------------------------------------------ */
static int cont_x = 8, cont_y = 38, cont_w = 640, cont_h = 200;
static int lock_depth;
static int wakes_posted;
static int fullscreen;
static char last_toast[128];
static int damage_whole = 1;
static struct os88_rect damage_rect;

/* --- a fake folder tree: home -> A (clus 5) -> 0 (clus 9) -> RCPROBE.TXT --- */
static unsigned std_clus;        /* where the machine stands */
static int std_vol;
static int ferr;

#include "os88.h"

void os88_gfx_lock(void)   { lock_depth++; }
void os88_gfx_unlock(void) { lock_depth--; }
void os88_set_color(int c) { pen = c; }

static void need_lock(const char *who)
{
    if (lock_depth != 1) {
        printf("HARNESS: %s called with lock depth %d\n", who, lock_depth);
        exit(1);
    }
}

void os88_gfx_fill(int x1, int y1, int x2, int y2)
{
    int x, y, c;
    need_lock("gfx_fill");
    n_fill++;
    n_fill_rows += y2 - y1 + 1;
    fill_w = x2 - x1 + 1;
    for (c = x1 / 8; c <= x2 / 8 && c < GW; c++)
        for (y = 0; y + 7 < GH; y++)
            if (y >= y1 && y + 7 <= y2) {
                gl_ch[y][c] = (pen == OS88_WHITE) ? ' ' : '#';
                gl_rev[y][c] = 0;
            }
    (void)x;
}

/* a frame is four 1-pixel fills; the fake glass is cell-atomic and cannot
 * hold a line, so it counts and draws nothing - what matters to the audit is
 * that the close's fill covers every cell a frame touched, and it does: the
 * frames are inside the panel's rectangle */
void os88_gfx_frame(int x1, int y1, int x2, int y2)
{
    need_lock("gfx_frame");
    n_frame++;
    (void)x1; (void)y1; (void)x2; (void)y2;
}

int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy)
{
    int y, c;
    need_lock("gfx_scroll");
    if (scroll_refuses) return -1;
    if ((x1 & 7) != 0 || ((x2 + 1) & 7) != 0) {
        printf("HARNESS: gfx_scroll refused: x1=%d x2=%d not byte aligned\n",
               x1, x2);
        return -1;
    }
    n_scroll++;
    n_scroll_rows += y2 - y1 + 1;
    if (dy > 0) {
        for (y = y1; y <= y2 - dy; y++)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = gl_ch[y + dy][c];
                gl_rev[y][c] = gl_rev[y + dy][c];
            }
        for (y = y2 - dy + 1; y <= y2; y++)      /* vacated: GARBAGE (5.5) */
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = '?';
                gl_rev[y][c] = 1;
            }
    } else {
        dy = -dy;
        for (y = y2; y >= y1 + dy; y--)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = gl_ch[y - dy][c];
                gl_rev[y][c] = gl_rev[y - dy][c];
            }
        for (y = y1; y < y1 + dy; y++)
            for (c = x1 / 8; c <= x2 / 8; c++) {
                gl_ch[y][c] = '?';
                gl_rev[y][c] = 1;
            }
    }
    return 0;
}

/* the band is rc_band's (below): row 0 = the character, row 1 = reverse */
int os88_gfx_blit1(const void *bits, int stride, int x, int y, int w, int rows)
{
    const unsigned char *b = bits;
    int c, n = w / 8;
    need_lock("gfx_blit1");
    if (blit1_refuses) { n_blit_refused++; return -1; }
    if ((x & 7) || (w & 7) || rows < 1 || rows > 255) {
        printf("HARNESS: blit1 refused: x=%d w=%d rows=%d\n", x, w, rows);
        return -1;
    }
    if (rows != 8 || stride != 80) {       /* the band's stride is RC_COLS,
                                              * always (rcband.inc) */
        printf("HARNESS: blit1 with rows=%d stride=%d w=%d - not a cell row\n",
               rows, stride, w);
        exit(1);
    }
    n_blit++;
    n_cells += n;
    n_blit_cells += n;
    for (c = 0; c < n; c++) {
        gl_ch[y][x / 8 + c] = (char)b[c];
        gl_rev[y][x / 8 + c] = b[stride + c];
    }
    return 0;
}

void os88_font_run(int x, int y, const char *s, int ink, int paper)
{
    int c = x / 8;
    need_lock("font_run");
    if (x & 7) {
        printf("HARNESS: font_run at x=%d - not on a cell boundary\n", x);
        exit(1);
    }
    n_run++;
    while (*s) {
        gl_ch[y][c] = *s;
        gl_rev[y][c] = (ink == OS88_WHITE && paper == OS88_BLACK);
        n_cells++;
        n_run_cells++;
        c++;
        s++;
    }
}

void *os88_wm_create(int x, int y, int w, int h, const char *title)
{ (void)x; (void)y; (void)w; (void)h; (void)title; return (void *)1; }
void os88_wm_content(void *win, struct os88_pt *o) { o->x = cont_x; o->y = cont_y; }
int  os88_wm_geom(void *win, struct os88_size *s) { s->w = cont_w; s->h = cont_h; return 0; }
int  os88_wm_damage(void *win, struct os88_rect *r)
{
    if (damage_whole) {
        r->x1 = cont_x; r->y1 = cont_y;
        r->x2 = cont_x + cont_w - 1; r->y2 = cont_y + cont_h - 1;
        return 1;
    }
    *r = damage_rect;
    return 0;
}
int  os88_wm_clip_set(void *win) { return 0; }
void os88_wm_snap(void *win, int on) {}
void os88_wm_ownbg(void *win, int on) {}
void os88_wm_onwake(void *win) {}
int  os88_wm_wake(void *win) { wakes_posted++; return 0; }
/* OSAPI_FULLSCREEN paints SYNCHRONOUSLY inside the slot (kernel/wm.inc
 * wm_fullscreen: wm_raise whole on enter, wm_paint_all on exit), under the
 * lock the caller holds - so the stub does what the machine does: the new
 * geometry, then os88_paint whole (LESSONS.md 7: a stub models the machine).
 * Fullscreen is 80x25 on every adapter; the framed geometry comes back on
 * exit. A stub that only recorded the flag measured nothing of the double
 * draw an rc_sh_inval() after the latch would have cost. */
static int fs_w, fs_h;
int  os88_fullscreen(void *win, int enter)
{
    if (enter) {
        if (fullscreen) return 0;
        fs_w = cont_w; fs_h = cont_h;
        cont_w = 640; cont_h = 200;
    } else {
        if (!fullscreen) return 0;
        cont_w = fs_w; cont_h = fs_h;
    }
    fullscreen = enter;
    damage_whole = 1;
    os88_paint(win);                       /* the caller holds the lock: the
                                            * primitives assert depth 1 */
    return 0;
}
void os88_menu_set(void *win, struct os88_menuset *set) {}
void os88_about_set(void *win) {}
int  os88_snd_tone(int hz, int ticks, int prio) { return 0; }
/* the self-close (wave 3): os88_task_spawn wants the lock held (SPEC.md 20.6)
 * and may refuse (the task table full - transient); the worker destroys the
 * window under the lock and dies inside its next os88_task_alive(), which
 * here longjmps back to the script (the real one never returns) */
static int spawn_refuses, spawned, destroyed;
static jmp_buf worker_end;
int  os88_task_spawn(void *win)
{
    need_lock("task_spawn");
    if (spawn_refuses) return -1;
    spawned++;
    return 0;
}
void os88_wm_destroy(void *win) { need_lock("wm_destroy"); destroyed++; }
void os88_task_alive(void *win)
{
    if (lock_depth != 0) { printf("HARNESS: task_alive with the lock held (%d)\n", lock_depth); exit(1); }
    if (destroyed) longjmp(worker_end, 1);
}
void os88_task_sleep(int ticks) {}
int  os88_toast(const char *text, int ticks)
{ strncpy(last_toast, text, sizeof(last_toast) - 1); return 0; }
int  os88_cpu(void) { return OS88_CPU_8086; }
/* the tick: advances by tick_step on every read - one, so an EXHAUSTED
 * slice always spans exactly one tick and the adaptive length holds still
 * (an early-ended slice - blocked, program end, HALT - is not timed at all
 * and the rows below assert that with tick_step = 0); and zero during the
 * clock estimate, where the modelled Z80 advances the tick once per
 * completed BURST instead, so four ticks are four bursts and the two lines
 * are checkable in 32-bit arithmetic here */
static unsigned ticks;
static unsigned tick_step = 1;
unsigned os88_ticks(void) { unsigned t = ticks; ticks += tick_step; return t; }
/* two claims: the Z80's 64KB at 0x2000 and the CCP's 2KB at 0x3000 (wave 3) */
static int n_claims;
unsigned os88_mem_claim(int kb)
{
    n_claims++;
    if (kb == 64) return 0x2000;
    if (kb == 2)  return 0x3000;
    printf("HARNESS: os88_mem_claim(%d): not a claim this program makes\n", kb);
    exit(1);
}
unsigned os88_mem_largest_kb(void) { return 200; }

int os88_ferr(void) { return ferr; }
void os88_file_here(struct os88_place *p) { p->clus = std_clus; p->vol = std_vol; }
int os88_file_goto_q_mark(unsigned clus, int vol)
{
    if (vol != 0 || (clus != 0 && clus != 5 && clus != 9)) { ferr = OS88_FERR_IO; return -1; }
    std_clus = clus; std_vol = vol; ferr = 0;
    return 0;
}
int os88_file_find(int ordinal, struct os88_find *f)
{
    memset(f, 0, sizeof(*f));
    if (std_clus == 0 && ordinal == 0) {
        strcpy(f->name, "RUNCPM.O88"); f->type = OS88_FT_PKG; return 1;
    }
    if (std_clus == 0 && ordinal == 1) {
        strcpy(f->name, "A"); f->type = OS88_FT_DIR; f->clus = 5; return 2;
    }
    if (std_clus == 5 && ordinal == 0) {
        strcpy(f->name, ".."); f->type = OS88_FT_UP; return 1;
    }
    if (std_clus == 5 && ordinal == 1) {
        strcpy(f->name, "0"); f->type = OS88_FT_DIR; f->clus = 9; return 2;
    }
    if (std_clus == 9 && ordinal == 0) {
        strcpy(f->name, "RCPROBE.TXT"); f->type = OS88_FT_FILE; f->size_lo = 26; return 1;
    }
    ferr = OS88_FERR_NOENT;
    return -1;
}
/* AUTOEXEC.TXT lives in the ROOT (the launch folder, clus 0) when the script
 * says so - read ONCE at launch (SPEC.md 71: upstream re-reads it on every
 * warm boot; here that is a directory walk of the launch folder per ^C and
 * buys nothing, so it is cached). The stubs model OSAPI_FILE_READ as the
 * kernel does it: a file LARGER THAN THE CAPACITY is FERR_BIG and NOTHING is
 * read (decided from the directory entry, os88api.inc 0x0128) - never a
 * truncation, which is the convenient thing and the wrong model (LESSONS.md
 * 7): a 125-byte read of a 200-byte AUTOEXEC.TXT would silently ignore it. */
static const char *autoexec_text;
static unsigned n_axreads;
unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    if (strcmp(name, "AUTOEXEC.TXT") == 0) {
        n_axreads++;
        if (std_clus != 0) { printf("HARNESS: AUTOEXEC.TXT looked for in clus %u, not the launch folder\n", std_clus); exit(1); }
        if (autoexec_text) {
            unsigned n = (unsigned)strlen(autoexec_text);
            if (n > cap) { ferr = OS88_FERR_BIG; return 0; }
            memcpy(buf, autoexec_text, n);
            ferr = 0;
            return n;
        }
    }
    ferr = OS88_FERR_NOENT;
    return 0;
}
/* the loader's read: HELLO.COM exists in A\0 (clus 9) only, 40 bytes that
 * LAND in the claim (the claim is the fake Z80's RAM below, seg 0x2000) so
 * the copy down to 0100h is checked; BIG.COM exists there too and is over
 * the cap (FERR_BIG). The stub does what the disk driver does with a base
 * that is not 512-aligned - refuses it, loudly: SPEC.md 2.1.1, and the one
 * defect QEMU cannot show (its BIOS ignores the DMA page). */
static char last_read[16];
static unsigned last_read_seg, n_reads;
static unsigned char zram[65536];
static unsigned char ccpram[2048];       /* the CCP claim, seg 0x3000 */
static int ccp_missing;                  /* the disk without CCP-DR.60K */
static const char hello_bytes[40] = "\016\011\021\011\001\315\005\000\311Hello from the Z80 - 40 bytes.$";
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
{
    strncpy(last_read, name, 15);
    last_read_seg = seg;
    n_reads++;
    if (seg & 0x1F) {
        printf("HARNESS: os88_file_read_seg to %04x: not a 512-aligned base (SPEC.md 2.1.1)\n", seg);
        exit(1);
    }
    if (strcmp(name, "AUTOEXEC.TXT") == 0) {
        /* whole, into the Z80 claim's TPA at the loader's landing (0x2020:
         * 512-aligned), the TPA up to the CCP as the capacity - FERR_BIG
         * above it, as the kernel decides */
        unsigned n;
        n_axreads++;
        if (std_clus != 0) { printf("HARNESS: AUTOEXEC.TXT looked for in clus %u, not the launch folder\n", std_clus); exit(1); }
        if (seg != 0x2020 || cap != 0xE200) { printf("HARNESS: AUTOEXEC.TXT read into %04x cap %04x, wanted the TPA at 2020 cap e200\n", seg, cap); exit(1); }
        if (!autoexec_text) { ferr = OS88_FERR_NOENT; return 0; }
        n = (unsigned)strlen(autoexec_text);
        if (n > cap) { ferr = OS88_FERR_BIG; return 0; }
        memcpy(zram + 0x200, autoexec_text, n);
        ferr = 0;
        return n;
    }
    if (strcmp(name, "CCP-DR.60K") == 0) {
        /* the DRI CCP, from the LAUNCH folder, into the 2KB claim: a fake
         * image whose command buffer is where the real one's is - +6 the
         * max (0x7F), +7 the length (0), +8 the characters (spaces) - so the
         * AUTOEXEC poke and the copy to 0xE400 can be checked */
        unsigned i;
        if (std_clus != 0) { printf("HARNESS: the CCP read from clus %u, not the launch folder\n", std_clus); exit(1); }
        if (seg != 0x3000 || cap != 2048) { printf("HARNESS: the CCP read into %04x cap %u\n", seg, cap); exit(1); }
        if (ccp_missing) { ferr = OS88_FERR_NOENT; return 0; }
        for (i = 0; i < 2048; i++) ccpram[i] = (unsigned char)(0xC0 + (i & 7));   /* a pattern */
        ccpram[6] = 0x7F; ccpram[7] = 0;
        for (i = 8; i < 136; i++) ccpram[i] = ' ';
        ferr = 0;
        return 2048;
    }
    if (std_clus == 9 && strcmp(name, "HELLO.COM") == 0) {
        if (seg < 0x2000 || ((seg - 0x2000) << 4) + 40 > 0x10000) { printf("HARNESS: read outside the claim\n"); exit(1); }
        memcpy(zram + ((seg - 0x2000) << 4), hello_bytes, 40);
        ferr = 0;
        return 40;
    }
    if (std_clus == 9 && strcmp(name, "BIG.COM") == 0) { ferr = OS88_FERR_BIG; return 0; }
    ferr = OS88_FERR_NOENT;
    return 0;
}

/* wave 4's file layer (rcfs.c) is exercised by hosttest/rcfstest.c against a
 * fake folder tree with contents; here the scripts never open a file, so
 * these stubs exist to LINK and to say so if they are ever reached - except
 * mkdir, which the USER 1 script reaches (_SetUser -> _MakeUserDir) and which
 * refuses as a full disk would, so the area stays absent as wave 3's script
 * expects */
static int n_mkdir;
int os88_file_mkdir(const char *name) { n_mkdir++; ferr = OS88_FERR_FULL; return -1; }
int os88_mem_free(unsigned seg) { printf("HARNESS: os88_mem_free(%04x): not here\n", seg); exit(1); }
unsigned os88_mem_regrow(unsigned seg, int kb) { printf("HARNESS: os88_mem_regrow: not here\n"); exit(1); }
int  os88_peek(unsigned seg, unsigned off) { printf("HARNESS: os88_peek: not here\n"); exit(1); }
void os88_poke(unsigned seg, unsigned off, int value) { printf("HARNESS: os88_poke: not here\n"); exit(1); }
int os88_file_write_seg(const char *name, unsigned seg, unsigned count) { printf("HARNESS: os88_file_write_seg(%s): not here\n", name); exit(1); }
int os88_file_delete(const char *name) { printf("HARNESS: os88_file_delete(%s): not here\n", name); exit(1); }
int os88_file_rename(const char *o, const char *n) { printf("HARNESS: os88_file_rename(%s): not here\n", o); exit(1); }
void rc_sfill(unsigned seg, unsigned off, int v, unsigned n) { printf("HARNESS: rc_sfill: not here\n"); exit(1); }

void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
void os88_strcpy(char *dst, const char *src, unsigned cap)
{ strncpy(dst, src, cap - 1); dst[cap - 1] = 0; }
char *os88_utoa(unsigned v, char *dst6) { sprintf(dst6, "%u", v); return dst6; }

/* --- the program ------------------------------------------------------------ */
#include "../runcpm.c"

/* --- the Z80 and its RAM, modelled (apps/runcpm/rcz80.inc, rcmem.inc) -------
 * The core is not run here (rcz80test.sh runs the shipping text in raw QEMU
 * against ZEXDOC); what the harness models is the CONTRACT the C sees: a
 * register file, a 64KB RAM the accessors reach, and rc_run() as a SCRIPT of
 * handoffs - each step sets PC (past the RST), BC and DE and answers a
 * reason; a step the C put PC back onto (the retry, SPEC.md 71.1) is served
 * again. In the clock state the burst is ZCLK_BURST control transfers spent
 * against the budget of each call: a call that runs out mid-burst answers
 * SLICE with PC inside the loop, and the C must come back with THAT PC (a
 * PC of 0000 mid-burst is a restart, counted in clk_restarts); the call
 * that spends the last of it HALTs, PC on the HALT, and advances the tick. */
struct rc_zregs rc_z;
struct zstep { int reason; unsigned pc, bc, de; };
static struct zstep zsteps[32];
static int zsn, zsp, zlast = -1;
static int n_rc_run;
#define ZCLK_BURST 11000
static int zclk_left = ZCLK_BURST;          /* of the burst under way */
static int clk_restarts, clk_slices, clk_halts;
static void zscript_reset(void) { zsn = zsp = 0; zlast = -1; }
static void zstep(int reason, unsigned pc, unsigned bc, unsigned de)
{ zsteps[zsn].reason = reason; zsteps[zsn].pc = pc; zsteps[zsn].bc = bc; zsteps[zsn].de = de; zsn++; }
int rc_run(int n)
{
    struct zstep *st;
    n_rc_run++;
    if (rc_mode == RC_M_CLOCK) {
        if (zclk_left != ZCLK_BURST && rc_z.pc == 0)
            clk_restarts++;                     /* mid-burst, PC reset: WRONG */
        if (zclk_left == ZCLK_BURST && rc_z.pc != 0)
            clk_restarts++;                     /* a new burst not from 0000 */
        if (n < zclk_left) {                    /* the slice runs out inside */
            zclk_left -= n;
            rc_z.pc = 6;                        /* inside the inner loop */
            rc_z.cnt = 0;
            clk_slices++;
            return RC_RUN_SLICE;
        }
        rc_z.cnt = n - zclk_left;
        zclk_left = ZCLK_BURST;
        rc_z.pc = 16;                           /* on the HALT */
        clk_halts++;
        ticks++;                                /* a burst is one tick here */
        return RC_RUN_HALT;
    }
    if (zlast >= 0 && rc_z.pc == zsteps[zlast].pc - 1) {
        st = &zsteps[zlast];                    /* the retry: same trap again */
    } else if (zsp < zsn) {
        st = &zsteps[zsp];
        zlast = zsp++;
    } else {
        rc_z.cnt = 0;                           /* nothing scripted: the slice
                                                 * runs out */
        return RC_RUN_SLICE;
    }
    rc_z.pc = st->pc; rc_z.bc = st->bc; rc_z.de = st->de;
    rc_z.cnt = n - 1;
    return st->reason;
}
int  rc_rd(unsigned a) { return zram[a & 0xFFFF]; }
int  rc_rd16(unsigned a) { return zram[a & 0xFFFF] | (zram[(a + 1) & 0xFFFF] << 8); }
void rc_wr(unsigned a, int v) { zram[a & 0xFFFF] = (unsigned char)v; }
void rc_wr16(unsigned a, int v) { zram[a & 0xFFFF] = (unsigned char)v; zram[(a + 1) & 0xFFFF] = (unsigned char)(v >> 8); }
void rc_zcopy_in(unsigned zaddr, const void *src, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) zram[(zaddr + i) & 0xFFFF] = ((const unsigned char *)src)[i]; }
void rc_zcopy_out(void *dst, unsigned zaddr, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) ((unsigned char *)dst)[i] = zram[(zaddr + i) & 0xFFFF]; }
/* the claim-to-Z80 mover, modelled for the one claim the harness has - the
 * Z80's own (the loader's copy-down, BIOS MOVE): an ASCENDING byte copy, as
 * rcmem.inc's rep movsb is, so an overlap smears exactly the way the machine
 * would */
void rc_zzcopy_in(unsigned zaddr, unsigned seg, unsigned off, unsigned n)
{
    unsigned i;
    if (seg == 0x3000) {                    /* the CCP claim -> the Z80 */
        if (off + n > 2048) { printf("HARNESS: rc_zzcopy_in past the CCP claim\n"); exit(1); }
        for (i = 0; i < n; i++)
            zram[(zaddr + i) & 0xFFFF] = ccpram[off + i];
        return;
    }
    if (seg != 0x2000) { printf("HARNESS: rc_zzcopy_in from seg %04x: not the claim\n", seg); exit(1); }
    for (i = 0; i < n; i++)
        zram[(zaddr + i) & 0xFFFF] = zram[(off + i) & 0xFFFF];
}
void rc_zzcopy_out(unsigned seg, unsigned off, unsigned zaddr, unsigned n) { (void)seg; (void)off; (void)zaddr; (void)n; }
void rc_zfill(unsigned zaddr, int v, unsigned n)
{ unsigned i; for (i = 0; i < n; i++) zram[(zaddr + i) & 0xFFFF] = (unsigned char)v; }

/* --- the assembly, modelled (apps/runcpm/rcband.inc) ------------------------ */
void rc_band(unsigned char *dst, const unsigned char *chars,
             const unsigned char *attr, int first, int n)
{
    int c;
    for (c = 0; c < n; c++) {
        dst[c] = chars[c];
        dst[RC_COLS + c] = (attr[(first + c) >> 3] >> (7 - ((first + c) & 7))) & 1;
    }
}
int rc_rowdiff(const unsigned char *a, const unsigned char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return i;
    return -1;
}
int rc_rowdiffl(const unsigned char *a, const unsigned char *b, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) if (a[i] != b[i]) return i;
    return -1;
}

/* ==========================================================================
 * THE CHECKS
 * ========================================================================*/
static void *win;
static int fails;
static int vis_rows(void) { int r = cont_h / 8; return r > RC_ROWS ? RC_ROWS : r; }
static int vis_cols(void) { int c = cont_w / 8; return c > RC_COLS ? RC_COLS : c; }

static int model_ch(int r, int c) { return rc_ch[RC_CHOFF(rc_prow[r]) + c]; }
static int model_rev(int r, int c)
{
    int a = (rc_at[RC_ATOFF(rc_prow[r]) + (c >> 3)] >> (7 - (c & 7))) & 1;
    if (rc_curvis && rc_cy == r && rc_cx == c) a ^= 1;
    return a;
}
static int glass_ch(int r, int c) { return gl_ch[cont_y + r * 8][cont_x / 8 + c]; }
static int glass_rev(int r, int c) { return gl_rev[cont_y + r * 8][cont_x / 8 + c]; }
static int shadow_ch(int r, int c) { return rc_sh[RC_CHOFF(rc_shrow[r]) + c]; }
static int shadow_rev(int r, int c)
{ return (rc_sha[RC_ATOFF(rc_shrow[r]) + (c >> 3)] >> (7 - (c & 7))) & 1; }

static void dump_row(int r)
{
    int c;
    printf("  model  %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(model_ch(r, c)); printf("|\n");
    printf("  glass  %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(glass_ch(r, c)); printf("|\n");
    printf("  shadow %2d |", r); for (c = 0; c < vis_cols(); c++) putchar(shadow_ch(r, c) ? shadow_ch(r, c) : '~'); printf("|\n");
}

/* glass == model and shadow == glass, every visible cell */
static void audit(const char *step)
{
    int r, c, bad = 0;
    for (r = 0; r < vis_rows(); r++)
        for (c = 0; c < vis_cols(); c++) {
            if (glass_ch(r, c) != model_ch(r, c) || glass_rev(r, c) != model_rev(r, c)) {
                if (!bad) printf("FAIL %s: glass != model at row %d col %d (glass '%c'/%d model '%c'/%d)\n",
                                 step, r, c, glass_ch(r, c), glass_rev(r, c), model_ch(r, c), model_rev(r, c));
                bad++;
            }
            if (shadow_ch(r, c) != glass_ch(r, c) || shadow_rev(r, c) != glass_rev(r, c)) {
                if (!bad) printf("FAIL %s: shadow != glass at row %d col %d\n", step, r, c);
                bad++;
            }
        }
    if (bad) { fails++; dump_row(rc_cy); }
    if (lock_depth != 0) { printf("FAIL %s: lock left held (%d)\n", step, lock_depth); fails++; }
}

/* the row reads exactly this (an INDEPENDENT expectation, hand-written) */
static void expect_row(const char *step, int r, const char *text)
{
    int c, bad = 0;
    if (r >= vis_rows())
        return;                     /* a model row the window does not hold */
    for (c = 0; c < vis_cols(); c++) {
        int want = c < (int)strlen(text) ? text[c] : ' ';
        if (glass_ch(r, c) != want) { bad = 1; break; }
    }
    if (bad) {
        printf("FAIL %s: row %d reads wrong\n  want       |%s|\n", step, r, text);
        dump_row(r);
        fails++;
    }
}

static void expect_cursor(const char *step, int x, int y)
{
    if (rc_cx != x || rc_cy != y) {
        printf("FAIL %s: cursor at (%d,%d), wanted (%d,%d)\n", step, rc_cx, rc_cy, x, y);
        fails++;
    }
}

/* one wake: what the machine does after every slice */
static void wake(void) { os88_onwake(win); }
static void feed(const char *s) { rc_puts(s); }
static void expose(void)
{
    damage_whole = 1;
    lock_depth = 1;                 /* the kernel holds it for a paint */
    os88_paint(win);
    lock_depth = 0;
}

/* --- the cost table ------------------------------------------------------- */
struct cost { const char *what; int calls, cells, scrolls, blits, runs, fills;
              int run_cells, blit_cells, scroll_rows, fill_rows, fill_w, frames; };
static struct cost costs[32];
static int ncosts;
static void cost(const char *what)
{
    struct cost *c = &costs[ncosts++];
    c->what = what; c->calls = calls(); c->cells = n_cells;
    c->scrolls = n_scroll; c->blits = n_blit; c->runs = n_run; c->fills = n_fill;
    c->run_cells = n_run_cells; c->blit_cells = n_blit_cells;
    c->scroll_rows = n_scroll_rows; c->fill_rows = n_fill_rows; c->fill_w = fill_w;
    c->frames = n_frame;
}

/* THE MODEL of what the target pays - calls and cells are exact, the ms
 * column is a MODEL, per primitive, from PERFORMANCE.md's measurements:
 *   font_run   756 us a call + ~900 us a cell           (Part 2)
 *   band       rc_band + gfx_blit1 together, MEASURED on the icount harness
 *              (tests/rcband, `make rcbandbench`, Part 4's 0.359 ms a count):
 *              1 cell 1.03 ms, 11 cells 2.78, 79 cells 14.5 - a line of
 *              860 us a call + 173 us a cell (145 of it the compose, ~31 the
 *              emit; Set 64's blit1 alone is 395 + 31/cell). The first loop
 *              measured 306 us a cell on the same harness.
 *   gfx_scroll 756 us a call + rows x 150 us (VGA, 32-byte rows measured
 *              after §5.5.1: Set 54, 19,194 us / 128 rows; Hercules 269;
 *              an 80-byte row is more, unmeasured)
 *   gfx_fill   756 us a call + rows x (177 us + 0.28 us a pixel)   (Part 2)
 * A DIR row that scrolls the whole 200-row content is therefore ~30 ms of
 * scroll on VGA, not one 756-us call: wave 2's slice pacing (lines per flush)
 * is a performance decision, and 'N lines in one slice = ONE scroll' is the
 * thing that saves seconds on a TYPE. */
#define US_RUN_CALL   756
#define US_RUN_CELL   900
#define US_BAND_CALL  860
#define US_BAND_CELL  173
#define US_SCROLL_ROW 150
#define US_FILL_ROW   177
static double model_ms(const struct cost *c)
{
    double us = 0;
    us += (double)c->runs * US_RUN_CALL + (double)c->run_cells * US_RUN_CELL;
    us += (double)c->blits * US_BAND_CALL + (double)c->blit_cells * US_BAND_CELL;
    us += (double)c->scrolls * 756 + (double)c->scroll_rows * US_SCROLL_ROW;
    us += (double)c->fills * 756 + (double)c->fill_rows * (US_FILL_ROW + 0.28 * c->fill_w);
    us += (double)c->frames * 4 * 756;      /* four 1-px fills each */
    return us / 1000.0;
}
static void print_costs(void)
{
    int i;
    printf("\n  cost table: calls and cells are EXACT; 'model ms' prices them per\n"
           "  primitive (font_run 756+900/cell, Part 2; band 860+173/cell MEASURED on\n"
           "  tests/rcband; scroll 756+150/row VGA; fill 756+177/row+px)\n");
    printf("  %-40s %5s %5s  %8s  %s\n", "operation", "calls", "cells", "model ms", "scroll/blit1/run/fill (rows moved/filled)");
    for (i = 0; i < ncosts; i++) {
        struct cost *c = &costs[i];
        printf("  %-40s %5d %5d  %8.1f  %d/%d/%d/%d (%d/%d)\n", c->what, c->calls, c->cells,
               model_ms(c), c->scrolls, c->blits, c->runs, c->fills,
               c->scroll_rows, c->fill_rows);
    }
}

/* ==========================================================================
 * THE SCRIPTS
 * ========================================================================*/
static char ax_long[201];                /* a 200-byte AUTOEXEC.TXT */
static int ax_len;                       /* what the boot pokes at +7 */
static void run_all(int cols_geom, int rows_geom, int table)
{
    static char line[128];
    int i;
    for (i = 0; i < 200; i++) ax_long[i] = (char)('A' + i % 26);
    ax_long[200] = 0;

    cont_w = cols_geom * 8; cont_h = rows_geom * 8;
    gclear(); callreset(); ncosts = 0; wakes_posted = 0;
    std_clus = 0; std_vol = 0; ferr = 0;
    ticks = 100; tick_step = 1; zscript_reset(); n_rc_run = 0;
    zclk_left = ZCLK_BURST; clk_restarts = clk_slices = clk_halts = 0;
    memset(zram, 0, sizeof(zram));
    memset(ccpram, 0, sizeof(ccpram));
    rc_khead = rc_ktail = 0;
    n_claims = 0; spawn_refuses = 0; spawned = 0; destroyed = 0;
    ccp_missing = 0; n_axreads = 0;
    /* AUTOEXEC.TXT: 'DIR' and a rest behind a CR - and on one geometry a
     * 200-byte line, longer than main.c's 125-byte poke AND than a 125-byte
     * read would carry: the file is read whole and the first 125 bytes are
     * poked (SPEC.md 71) */
    autoexec_text = (cols_geom == 79) ? ax_long : "DIR\r\nrest";
    ax_len = (cols_geom == 79) ? 125 : 3;
    rc_mask = 0x7F; rc_cdrive = rc_odrive = rc_user = 0; rc_rovec = rc_login = 0;
    rc_tk_hi = rc_tk_last = 0;

    /* launch: os88_main builds the model (the banner's first four lines: the
     * clock estimate comes on the wake), the loader shows the window and
     * paints it whole */
    win = os88_main();
    if (!win) { printf("FAIL: os88_main returned 0 (%s)\n", last_toast); fails++; return; }
    if (rc_z.seg != 0x2000) { printf("FAIL: rc_z.seg not the claim\n"); fails++; }
    /* the CCP: its 2KB claim and the read from the launch folder happen in
     * os88_main, before anything moves the instance (SPEC.md 71.3) */
    if (n_claims != 2 || rc_ccpseg != 0x3000 || rc_ccplen != 2048) { printf("FAIL: the CCP claim/read: %d claims, seg %04x, %u bytes\n", n_claims, rc_ccpseg, rc_ccplen); fails++; }
    /* ...and AUTOEXEC.TXT, ONCE, whole, from the launch folder, into the
     * TPA (cleared behind it): its first 125 bytes cached, cut at the first
     * control character */
    if (strcmp(last_read, "AUTOEXEC.TXT") != 0 || n_axreads != 1) { printf("FAIL: os88_main read '%s' (%u AUTOEXEC reads), wanted the CCP then AUTOEXEC.TXT once\n", last_read, n_axreads); fails++; }
    if ((int)rc_axlen != ax_len || memcmp(rc_axbuf, autoexec_text, ax_len) != 0 || rc_axbuf[ax_len] != 0) { printf("FAIL: the AUTOEXEC cache: len %u, wanted %d\n", rc_axlen, ax_len); fails++; }
    if (zram[0x200] != 0 || zram[0x200 + ax_len - 1] != 0) { printf("FAIL: AUTOEXEC.TXT's bytes left in the TPA\n"); fails++; }
    expose();
    audit("expose after launch");
    expect_row("banner 1", 0, "  CP/M Emulator v6.9 by Marcelo Dantas");
    expect_row("banner 2", 1, "      Built Jul 21 2026 - 20:43:19");   /* the
                                  * pinned upstream commit's date (SPEC.md 71) */
    expect_row("banner 3", 2, "----------------------------------------");
    expect_row("banner 4", 3, "CPU is 8086 native");
    expect_cursor("after banner 1", 0, 4);
    if (wakes_posted == 0) { printf("FAIL: the paint did not kick\n"); fails++; }
    if (rc_mode != RC_M_CLOCK) { printf("FAIL: not measuring the clock after launch\n"); fails++; }

    /* the clock estimate: the clock program runs in ORDINARY SLICES, a slice
     * a wake, until four ticks have passed and a burst has completed - the
     * modelled Z80 spends 11,000 transfers a burst against the slice budget
     * (512 on the 8086 the stub reports, doubling as the adaptation sees
     * slices that took no tick), resumes a burst the slice cut (a restart
     * is counted and fails), and advances the tick once per HALT, so four
     * ticks are four bursts and the two lines are checkable in real 32-bit
     * arithmetic here: 4 x 260,319 T-states in 4 x 55 ms, hundredths of MHz
     * = T / (ms * 10) */
    tick_step = 0;
    for (i = 0; i < 200 && rc_mode == RC_M_CLOCK; i++) wake();
    tick_step = 1;
    audit("clock estimate");
    if (rc_mode != RC_M_RUN) { printf("FAIL: the clock estimate did not finish and boot (mode %d after %d wakes)\n", rc_mode, i); fails++; }
    if (clk_restarts) { printf("FAIL: %d clock slice(s) restarted the burst instead of resuming it\n", clk_restarts); fails++; }
    if (clk_slices < 4) { printf("FAIL: the clock ran whole bursts (%d slices cut a burst, wanted several)\n", clk_slices); fails++; }
    if (clk_halts != 4) { printf("FAIL: the clock estimate completed %d bursts, wanted 4\n", clk_halts); fails++; }
    if (i > 40) { printf("FAIL: the clock estimate took %d wakes; the slice did not adapt\n", i); fails++; }
    {
        unsigned long ts = 4UL * 260319UL, ms = 4UL * 55UL, hund = ts / (ms * 10UL);
        sprintf(line, "%lu T-states in %lu ms", ts, ms);
        expect_row("clock 1", 4, line);
        sprintf(line, "Estimated Z80 clock speed: %lu.%02lu MHz", hund / 100, hund % 100);
        expect_row("clock 2", 5, line);
    }
    expect_row("banner 5", 6, "BIOS at 0xfe00 - BDOS at 0xec00");
    expect_row("banner 6", 7, "BIOS/BDOS using interrupt handoff method");
    expect_row("banner 7", 8, "CCP CCP-DR.60K at 0xe400");
    if (n_rc_run != i) { printf("FAIL: the clock ran %d rc_run calls in %d wakes, wanted one a wake\n", n_rc_run, i); fails++; }

    /* THE BOOT (main.c 110-140, rc_boot): the clock's last wake went straight
     * on to CCPHEAD, _PatchCPM, the CCP copied out of its claim to 0xE400,
     * AUTOEXEC.TXT from the launch folder poked at CCPaddr+7/+8, Z80reset,
     * C = DSKByte, PC = CCPaddr, and the machine RUNNING - no idle state
     * between the banner and the prompt */
    expect_row("ccphead blank", 9, "");
    expect_row("ccphead", 10, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
    expect_cursor("after ccphead", 0, 11);
    if (rc_mode != RC_M_RUN || rc_z.pc != 0xE400) { printf("FAIL: after the boot: mode %d pc %04x, wanted RUN at e400\n", rc_mode, rc_z.pc); fails++; }
    if (zram[0xE400] != 0xC0 || zram[0xE406] != 0x7F || zram[0xEBFF] != 0xC7) { printf("FAIL: the CCP was not copied to 0xE400 whole (%02x %02x %02x)\n", zram[0xE400], zram[0xE406], zram[0xEBFF]); fails++; }
    if (zram[0] != 0xC3 || zram[5] != 0xC3 || zram[0xED00] != 0xD7 || zram[0xFF03] != 0xCF) { printf("FAIL: page zero / handoff pages not patched at boot\n"); fails++; }
    if (zram[3] != 0x3D || zram[4] != 0x00) { printf("FAIL: IOBYTE/DSKByte not set on the cold boot\n"); fails++; }
    if ((rc_z.bc & 0xFF) != 0 || rc_z.iff != 0) { printf("FAIL: Z80reset / C = DSKByte: bc %04x iff %u\n", rc_z.bc, rc_z.iff); fails++; }
    if (n_axreads != 1) { printf("FAIL: AUTOEXEC.TXT read %u times by the boot, wanted 1 (at launch)\n", n_axreads); fails++; }
    if (zram[0xE407] != ax_len || memcmp(zram + 0xE408, autoexec_text, ax_len) != 0 || zram[0xE408 + ax_len] != 0) { printf("FAIL: AUTOEXEC.TXT not poked at CCPaddr+7/+8: len %u '%.4s', wanted %d\n", zram[0xE407], zram + 0xE408, ax_len); fails++; }
    if (std_clus != 0) { printf("FAIL: the boot left the instance in clus %u, not the launch folder\n", std_clus); fails++; }

    /* THE CCP AT ITS PROMPT, scripted: the DRI CCP's first calls - reset
     * the disk system (13), select the drive from C (14), print the prompt
     * (2, 2), read a line (10, DE = its buffer at CCPaddr+6) - the last of
     * which BLOCKS on the empty ring: no re-post, the machine costs nothing
     * until a key comes */
    zstep(RC_RUN_BDOS, 0xED01, 0x000D, 0);
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0000);
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, 'A');
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, '>');
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);
    callreset();
    wake();
    audit("the prompt");
    expect_row("prompt", 11, "A>");
    expect_cursor("prompt", 2, 11);
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: the CCP's C_READSTR on an empty ring did not block (mode %d)\n", rc_mode); fails++; }
    if (rc_dma != 0x80 || rc_cdrive != 0 || (rc_login & 1) != 1) { printf("FAIL: DRV_ALLRESET / DRV_SET state: dma %04x cdrive %d login %04x\n", rc_dma, rc_cdrive, rc_login); fails++; }
    if (std_clus != 9) { printf("FAIL: DRV_SET A: did not stand the instance in A\\0 (clus %u)\n", std_clus); fails++; }
    {   int w = wakes_posted;                /* blocked: no wake is wanted */
        wake();
        if (wakes_posted != w) { printf("FAIL: a machine blocked at the prompt re-posted its wake\n"); fails++; }
    }
    if (table) cost("the boot's prompt (CCPHEAD + A>)");

    /* the whole prompt screen, exposed: ONE fill of the content and then only
     * the rows with something on them, each to its last non-blank cell - the
     * nine banner rows, CCPHEAD, and the prompt's row with the cursor */
    callreset();
    { int r, c; for (r = 0; r < 25; r++) for (c = 0; c < 80; c++) gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; }
    expose();
    audit("expose the prompt screen");
    if (n_fill != 1 || n_blit != 11 || calls() != 12) { printf("FAIL: the prompt screen expose cost %d fills / %d bands / %d calls, wanted 1 / 11 / 12\n", n_fill, n_blit, calls()); fails++; }
    if (table) cost("expose: full repaint, prompt screen (fill + 11 rows)");

    /* THE LINE EDITOR (cpm.h C_READSTR, as a state machine): each key is one
     * os88_onkey (the ring, the kick) and one wake (the retried trap pops it,
     * edits, echoes; then blocks again). 'dir' typed: three echoes; ^U:
     * cpm.h's '#', BS, LF then curCol backspaces; a line, then ^? (Ctrl+-
     * = 31): the two help lines and the line retyped; DEL (Ctrl+Backspace =
     * 127) at EOL: BS, space, BS; ^A/^F move; ^G deletes at the cursor
     * (retype + post-backspace); Enter ends: the count and the bytes in the
     * CCP's buffer, HL = the count, a CR. Then ^C on an EMPTY line: '^C', a
     * warm boot - CCPHEAD again. */
#define KEY(ch, sc) do { lock_depth = 1; os88_onkey((ch), (sc), win); lock_depth = 0; wake(); } while (0)
    callreset();
    KEY('d', 0x20); 
    audit("type d");
    if (calls() != 1 || n_cells != 2) { printf("FAIL: one key into the line editor cost %d calls / %d cells, wanted 1 / 2\n", calls(), n_cells); fails++; }
    if (table) cost("one key into C_READSTR (echo + cursor)");
    KEY('i', 0x17); KEY('r', 0x13);
    audit("type dir");
    expect_row("dir typed", 11, "A>dir");
    expect_cursor("dir typed", 5, 11);
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: the editor did not block again after a key (mode %d)\n", rc_mode); fails++; }
    if (memcmp(zram + 0xE408, "dir", 3) != 0) { printf("FAIL: the editor did not put 'dir' in the CCP's buffer\n"); fails++; }
    KEY(21, 0x16);                             /* ^U */
    audit("^U");
    expect_row("^U", 11, "A>dir#");
    expect_cursor("^U", 2, 12);
    KEY('t', 0x14); KEY('y', 0x15); KEY('p', 0x19); KEY('e', 0x12); KEY(' ', 0x39); KEY('x', 0x2D);
    audit("type x");
    expect_row("type x", 12, "  type x");
    KEY(31, 0x0C);                             /* ^? = Ctrl+- : the help */
    audit("help");
    expect_row("help 1", 13, "^A Left  ^B B/EOL ^C Abort ^E N/Lin ^F Right ^G Del@C ^H/Del BackSp");
    expect_row("help 2", 14, "^K DelEOL ^R Retype ^U DelAll ^W Recall ^X DelBOL ^? Help");
    expect_row("help retype", 15, "type x");
    expect_cursor("help", 6, 15);
    KEY(127, 0x0E);                            /* DEL = Ctrl+Backspace, at EOL */
    audit("DEL");
    expect_row("DEL", 15, "type");
    expect_cursor("DEL", 5, 15);
    KEY(1, 0x1E); KEY(1, 0x1E);                /* ^A ^A: back two */
    expect_cursor("^A", 3, 15);
    KEY(7, 0x22);                              /* ^G: delete 'e' at the cursor */
    audit("^G");
    expect_row("^G", 15, "typ");
    expect_cursor("^G", 3, 15);
    KEY(6, 0x21);                              /* ^F: forward one */
    expect_cursor("^F", 4, 15);
    KEY(6, 0x21);                              /* ^F at EOL: refused, a bell */
    if (rc_bells != 0) { printf("FAIL: the editor's bell was not serviced (%d)\n", rc_bells); fails++; }
    KEY(13, 0x1C);                             /* Enter */
    audit("enter");
    expect_cursor("enter", 0, 15);             /* the CR */
    if (rc_mode != RC_M_RUN) { printf("FAIL: Enter did not end the line (mode %d)\n", rc_mode); fails++; }
    if (zram[0xE407] != 4 || memcmp(zram + 0xE408, "typ ", 4) != 0) { printf("FAIL: the line in the CCP's buffer: len %u '%.5s', wanted 4 'typ '\n", zram[0xE407], zram + 0xE408); fails++; }
    if ((rc_z.af & 0xFF) != 4 || rc_z.hl != 4) { printf("FAIL: C_READSTR answered A=%02x HL=%04x, wanted the count 4\n", rc_z.af & 0xFF, rc_z.hl); fails++; }
    if (rs_last[0] != 4 || memcmp(rs_last + 1, "typ ", 4) != 0) { printf("FAIL: the last command was not saved for ^W\n"); fails++; }
    /* the CCP would answer; script its next prompt: a new line, the
     * prompt, the read - and ^W recalls 'typ ' */
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, '\r');
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, '\n');
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, 'A');
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, '>');
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);
    wake();
    expect_row("prompt 2", 16, "A>");
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: the second prompt did not block (mode %d)\n", rc_mode); fails++; }
    KEY(23, 0x11);                             /* ^W */
    audit("^W");
    expect_row("^W", 16, "A>typ");
    expect_cursor("^W", 6, 16);
    KEY(24, 0x2D);                             /* ^X: delete left of the cursor */
    audit("^X");
    expect_row("^X", 16, "A>");
    expect_cursor("^X", 2, 16);
    callreset();
    KEY(3, 0x2E);                              /* ^C on an empty line: '^C', a warm boot */
    audit("^C");
    expect_row("^C", 16, "A>^C");
    /* main.c's loop laps: CCPHEAD, the CCP again, AUTOEXEC POKED again from
     * the cache (BOOTONLY FALSE - and NO disk: the file was read at launch)
     * - and DSKByte kept, IOBYTE kept (a RESTART is not a cold start); the
     * CCP's prompt then ran in the SAME slice (the scripted Z80 had nothing
     * more, so the slice exhausted): CCPHEAD and whatever the CCP prints
     * next are ONE flush */
    expect_row("reboot ccphead", 17, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
    expect_cursor("reboot", 0, 18);
    if (rc_mode != RC_M_RUN || rc_z.pc != 0xE400) { printf("FAIL: ^C did not warm-boot (mode %d pc %04x)\n", rc_mode, rc_z.pc); fails++; }
    if (n_axreads != 1) { printf("FAIL: AUTOEXEC.TXT read %u times after a warm boot, wanted 1 (a warm boot touches no disk)\n", n_axreads); fails++; }
    if (zram[0xE407] != ax_len || memcmp(zram + 0xE408, autoexec_text, ax_len) != 0) { printf("FAIL: the warm boot did not poke AUTOEXEC.TXT from the cache\n"); fails++; }
    if (rs_on) { printf("FAIL: the editor's state survived the warm boot\n"); fails++; }
    zscript_reset();
    if (table) cost("^C at the prompt: the warm boot (CCPHEAD)");
    /* the merged slice: ^C at the BOTTOM of the screen is ONE scroll (by 3:
     * '^C''s row ends, CCPHEAD's blank and text, the CCP's next line) and
     * one fill, not two of each - scripted with the CCP's prompt behind the
     * ^C so the same wake prints CCPHEAD and 'A>' */
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);
    wake();
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: not blocked before the merged-boot row\n"); fails++; }
    zstep(RC_RUN_BDOS, 0xED01, 0x000D, 0);          /* the CCP's boot calls */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0000);
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, 'A');
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, '>');
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);
    feed("\033[25;1H");                            /* the cursor on the last row */
    wake();
    callreset();
    KEY(3, 0x2E);                                    /* ^C: the boot AND the prompt */
    audit("^C merged");
    if (vis_rows() == 25) {
        expect_row("^C merged ^C", 22, "^C");
        expect_row("^C merged head", 23, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
        expect_row("^C merged prompt", 24, "A>");
        expect_cursor("^C merged", 2, 24);
        if (n_scroll != 1 || n_fill != 1) { printf("FAIL: ^C at the bottom cost %d scrolls / %d fills, wanted 1 / 1 (the boot and the prompt in one flush)\n", n_scroll, n_fill); fails++; }
    }
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: the merged boot did not run on to the CCP's read (mode %d)\n", rc_mode); fails++; }
    if (table) cost("^C at the bottom row: warm boot + prompt, one flush");
    zscript_reset();
    rc_mode = RC_M_RUN;                              /* the script is fresh */
    feed("\033[2J\033[19;1H");                       /* back to a clean row 18 */
    wake();

    /* DRV_SET of a drive with no folder: disk.h _error - 'Bdos Err on B:
     * Select', WAIT FOR A KEY, "\r\n", a warm boot; the wait is a blocked
     * state the key ends (the machine costs nothing meanwhile) */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0001);     /* DRV_SET B: */
    callreset();
    wake();
    audit("select error");
    expect_row("select error", 19, "Bdos Err on B: Select");
    if (rc_mode != RC_M_BLOCKED || !rc_errwait) { printf("FAIL: the Select error did not wait for a key (mode %d errwait %d)\n", rc_mode, rc_errwait); fails++; }
    {   int w = wakes_posted; wake();
        if (wakes_posted != w) { printf("FAIL: the error's wait re-posted its wake\n"); fails++; } }
    KEY(' ', 0x39);
    audit("select error key");
    expect_row("select reboot", 21, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
    expect_cursor("select reboot", 0, 22);
    if (rc_mode != RC_M_RUN || rc_cdrive != 0 || (zram[4] & 0x0F) != 0) { printf("FAIL: the error's key did not warm-boot back to A: (mode %d cdrive %d dsk %02x)\n", rc_mode, rc_cdrive, zram[4]); fails++; }
    zscript_reset();
    if (table) cost("Bdos Err on B: Select, then the key");
    /* ...and with a key ALREADY in the ring (typeahead behind 'b:' Enter):
     * the error's wait ends inside the same trap - upstream's _getcon()
     * consumes the queued key - and the warm boot happens on the SAME wake,
     * never a machine parked BLOCKED on a non-empty ring */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0001);     /* DRV_SET B: */
    lock_depth = 1; os88_onkey('d', 0x20, win); lock_depth = 0;   /* queued */
    rc_mode = RC_M_RUN;
    wake();
    audit("select error, typeahead");
    if (rc_mode != RC_M_RUN || rc_errwait || rc_z.pc != 0xE400 || rc_khead != rc_ktail) { printf("FAIL: a Select error with a key queued did not boot on the same wake (mode %d errwait %d pc %04x)\n", rc_mode, rc_errwait, rc_z.pc); fails++; }
    zscript_reset();
    /* a drive that is not a letter: E = 16 is 'Q:' upstream (cDrive = E
     * unmasked, cpm.h 1235; _sys_select stats a folder that cannot exist) -
     * not a silent success on A: */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0010);
    wake();
    audit("select Q:");
    { int r; for (r = 0; r < RC_ROWS; r++) if (model_ch(r, 0) == 'B' && model_ch(r, 12) == 'Q') break;
      if (r == RC_ROWS) { printf("FAIL: DRV_SET 16 did not print 'Bdos Err on Q: Select'\n"); fails++; } }
    if (!rc_errwait) { printf("FAIL: DRV_SET 16 did not err\n"); fails++; }
    KEY(' ', 0x39);
    if (rc_mode != RC_M_RUN || rc_cdrive != 0) { printf("FAIL: after Q:'s key: mode %d cdrive %d\n", rc_mode, rc_cdrive); fails++; }
    zscript_reset();
    /* USER 1, then the warm boot the DRI CCP does with C = DSKByte = 0x10:
     * setuser(1), select(0) - the user FOLDER is absent (the fake tree has
     * A\0 only) and that is NOT a Select error: _sys_select decides from the
     * DRIVE's folder alone (abstraction_posix.h 147-152); the machine stands
     * in A\ (clus 5), where searches answer 'no file', and the prompt comes.
     * (Before this was fixed, USER 1 + ^C looped 'Bdos Err on A: Select' on
     * every key: the harness had no USER script.) */
    zstep(RC_RUN_BDOS, 0xED01, 0x0020, 0x0001);     /* F_USERNUM: set 1 */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0000);     /* DRV_SET A: */
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);     /* the read: the prompt came */
    wake();
    audit("user 1");
    if (rc_user != 1) { printf("FAIL: F_USERNUM 1 left user %d\n", rc_user); fails++; }
    if (rc_errwait || rc_mode != RC_M_BLOCKED) { printf("FAIL: A: under USER 1 (no folder A\\1) erred Select (errwait %d mode %d)\n", rc_errwait, rc_mode); fails++; }
    if (std_clus != 5) { printf("FAIL: A: under USER 1 stands in clus %u, wanted the drive's folder (5)\n", std_clus); fails++; }
    if ((rc_z.af & 0xFF) != 0) { printf("FAIL: DRV_SET A: under USER 1 answered %02x\n", rc_z.af & 0xFF); fails++; }
    zscript_reset();
    zstep(RC_RUN_BDOS, 0xED01, 0x0020, 0x0000);     /* USER 0 again */
    zstep(RC_RUN_BDOS, 0xED01, 0x000E, 0x0000);
    rc_mode = RC_M_RUN;
    wake();
    if (rc_user != 0 || std_clus != 9) { printf("FAIL: back to USER 0: user %d clus %u\n", rc_user, std_clus); fails++; }
    zscript_reset();

    /* the private calls: 250 HostOS 0x08, 251 6.9 BCD, 252 DRI, 253 CCPaddr,
     * 248 uptime in DE:HL, 230 the mask, 254 accepted */
    zstep(RC_RUN_BDOS, 0xED01, 0x00FA, 0); wake();
    if ((rc_z.af & 0xFF) != 0x08) { printf("FAIL: F_HOSTOS answered %02x\n", rc_z.af & 0xFF); fails++; }
    zstep(RC_RUN_BDOS, 0xED01, 0x00FB, 0); wake();
    if ((rc_z.af & 0xFF) != 0x69) { printf("FAIL: F_VERSION answered %02x\n", rc_z.af & 0xFF); fails++; }
    zstep(RC_RUN_BDOS, 0xED01, 0x00FC, 0); wake();
    if ((rc_z.af & 0xFF) != 0x00) { printf("FAIL: F_CCPVERSION answered %02x\n", rc_z.af & 0xFF); fails++; }
    zstep(RC_RUN_BDOS, 0xED01, 0x00FD, 0); wake();
    if (rc_z.hl != 0xE400) { printf("FAIL: F_CCPADDR answered %04x\n", rc_z.hl); fails++; }
    ticks = 1200; tick_step = 0;               /* 1200 x 55 = 66,000 = 0x000101D0 */
    zstep(RC_RUN_BDOS, 0xED01, 0x00F8, 0); wake();
    tick_step = 1;
    if (rc_z.hl != 0x01D0 || rc_z.de != 0x0001) { printf("FAIL: F_UPTIME answered DE:HL %04x:%04x, wanted 0001:01d0\n", rc_z.de, rc_z.hl); fails++; }
    /* ...and it does not wrap at the hour: the kernel's ticks lap at 65,536
     * and the package keeps the high word - 65,500 seen, then 10: 65,546
     * ticks x 55 = 3,605,030 = 0x0037:0226 */
    ticks = 65500; tick_step = 0;
    zstep(RC_RUN_BDOS, 0xED01, 0x00F8, 0); wake();
    ticks = 10;
    zstep(RC_RUN_BDOS, 0xED01, 0x00F8, 0); wake();
    tick_step = 1;
    if (rc_z.hl != 0x0226 || rc_z.de != 0x0037) { printf("FAIL: F_UPTIME across the tick lap answered DE:HL %04x:%04x, wanted 0037:0226\n", rc_z.de, rc_z.hl); fails++; }
    ticks = 1300;
    zstep(RC_RUN_BDOS, 0xED01, 0x00E6, 0xFF); wake();
    if (rc_mask != 0xFF) { printf("FAIL: F_SETMASK did not set the mask\n"); fails++; }
    zstep(RC_RUN_BDOS, 0xED01, 0x00E6, 0x7F); wake();
    zstep(RC_RUN_BDOS, 0xED01, 0x00FE, 100); wake();
    if (rc_mode != RC_M_RUN) { printf("FAIL: F_SETCPUSPEED stopped the machine\n"); fails++; }
    zscript_reset();
    feed("\033[2J\033[H");                    /* the loader tests below want
                                                * a clean screen */
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);   /* the CCP back at its read */
    wake();
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: not blocked at the prompt before the loader tests\n"); fails++; }
    zscript_reset();

    /* THE DEBUG LOADER AND THE HANDOFFS: Alt+L (from a machine BLOCKED at
     * the CCP's prompt: the .COM runs in the CCP's place and its RET
     * warm-boots back), a name, Enter - the loader stands in A\0
     * (rc_fs_cd through the fake tree), reads HELLO.COM into the claim
     * 512-aligned, patches page zero and runs; the scripted Z80 then does
     * BDOS 9 (a string in its RAM), BDOS 2, and a BIOS WBOOT, which ends
     * the program and boots the CCP again (SPEC.md 71) */
    callreset();
    lock_depth = 1;
    os88_onkey(0, 0x26, win);            /* Alt+L */
    lock_depth = 0;
    audit("load prompt");
    expect_row("load prompt", 1, "Load .COM: ");
    if (!rc_ldmode) { printf("FAIL: Alt+L did not open the loader prompt\n"); fails++; }
    lock_depth = 1;
    os88_onkey('h', 0x23, win); os88_onkey('e', 0x12, win); os88_onkey('l', 0x26, win);
    os88_onkey('l', 0x26, win); os88_onkey('o', 0x18, win);
    os88_onkey('x', 0x2D, win); os88_onkey(8, 0x0E, win);      /* a typo, erased */
    lock_depth = 0;
    audit("load name typed");
    expect_row("load name", 1, "Load .COM: HELLO");
    strcpy((char *)zram + 0x500, "Hello from the Z80$");   /* clear of the
                                                  * loader's 0x200 landing */
    zstep(RC_RUN_BDOS, 0xED01, 0x0009, 0x0500);      /* BDOS 9: DE = the string */
    last_read[0] = 0; last_read_seg = 0;
    lock_depth = 1;
    { int w = wakes_posted;
      os88_onkey(13, 0x1C, win);
      lock_depth = 0;
      audit("load enter");
      /* Enter under the lock: the CR/LF echoed and flushed, NO floppy read
       * (several int 13h calls, ~400 ms each on the target: not under the
       * lock W_ONKEY holds - SPEC.md 71.1), a wake posted for it */
      if (last_read[0]) { printf("FAIL: the loader read %s from os88_onkey, under the lock\n", last_read); fails++; }
      if (rc_ldmode != RC_LD_LOAD || wakes_posted == w) { printf("FAIL: Enter did not hand the read to the wake (ldmode %d)\n", rc_ldmode); fails++; }
      expect_cursor("load enter", 0, 2);
    }
    callreset();
    { int w = wakes_posted;
      wake();                                /* the read, lock not held */
      audit("load wake");
      if (rc_ldmode != 0) { printf("FAIL: the load wake left ldmode %d\n", rc_ldmode); fails++; }
      /* the read lands at the first 512-aligned offset at or above 0200h
       * (the claim is 0x2000: that is 0x2020) and is copied down to 0100h */
      if (strcmp(last_read, "HELLO.COM") != 0 || last_read_seg != 0x2020) { printf("FAIL: the loader read '%s' into %04x, wanted HELLO.COM into 2020 (512-aligned)\n", last_read, last_read_seg); fails++; }
      if (memcmp(zram + 0x100, hello_bytes, 40) != 0) { printf("FAIL: the .COM was not copied down to 0100h\n"); fails++; }
      if (calls() != 0) { printf("FAIL: the load wake drew %d calls (nothing changed on the glass)\n", calls()); fails++; }
      if (wakes_posted == w) { printf("FAIL: the load wake did not re-post for the first slice\n"); fails++; }
    }
    if (std_clus != 9) { printf("FAIL: the loader is not standing in A\\0 (clus %u)\n", std_clus); fails++; }
    if (rc_mode != RC_M_RUN) { printf("FAIL: the loader did not start the machine (mode %d)\n", rc_mode); fails++; }
    if (rc_z.pc != 0x0100 || rc_z.sp != 0xE3FE) { printf("FAIL: the program starts at %04x sp %04x\n", rc_z.pc, rc_z.sp); fails++; }
    if (zram[0] != 0xC3 || zram[5] != 0xC3 || zram[0xED00] != 0xD7 || zram[0xFF03] != 0xCF) { printf("FAIL: page zero / handoff pages not patched\n"); fails++; }
    if (zram[0xFF80] != 0x00 || zram[0xFF81] != 0x01 || zram[0xFF82] != 0x05) { printf("FAIL: the DPB is not cpm.h's\n"); fails++; }
    callreset();
    wake();                                  /* the slice: BDOS 9, then the
                                              * script runs out */
    audit("hello slice");
    expect_row("hello", 2, "Hello from the Z80");
    if (rc_mode != RC_M_RUN) { printf("FAIL: the machine stopped after BDOS 9 (mode %d)\n", rc_mode); fails++; }
    if (rc_z.hl != 0 || rc_z.bc != 0x0000 || (rc_z.af & 0xFF) != 0) { printf("FAIL: the BDOS tail (HL=0, C=E, B=H, A=L): hl %04x bc %04x af %04x\n", rc_z.hl, rc_z.bc, rc_z.af); fails++; }
    if (table) cost("a slice: BDOS 9 (18 chars)");
    zstep(RC_RUN_BDOS, 0xED01, 0x0002, 0x0021);      /* BDOS 2: '!' */
    zstep(RC_RUN_BIOS, 0xFF04, 0, 0);                /* BIOS WBOOT (fn 3) */
    wake();
    audit("hello end");
    expect_row("hello!", 2, "Hello from the Z80!");
    /* WBOOT: the program ended, and main.c's loop lapped - CCPHEAD, the CCP
     * at 0xE400 again (the hello's bytes are still at 0100h: nothing clears
     * the TPA), running */
    expect_row("hello reboot", 3, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
    if (rc_mode != RC_M_RUN || rc_z.pc != 0xE400) { printf("FAIL: WBOOT did not boot the CCP again (mode %d pc %04x)\n", rc_mode, rc_z.pc); fails++; }
    if (rc_status != RC_ST_RUNNING) { printf("FAIL: the reboot left status %d\n", rc_status); fails++; }
    zscript_reset();
    zstep(RC_RUN_BDOS, 0xED01, 0x000A, 0xE406);   /* the CCP at its read */
    wake();
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: the rebooted CCP did not block at its read (mode %d)\n", rc_mode); fails++; }
    expect_cursor("after hello reboot", 0, 4);

    /* a .COM that exists in A\0 but is over the TPA: the fact printed is
     * 'too big', and the launch-folder fallback (whose NOENT would hide it)
     * is not tried */
    lock_depth = 1;
    os88_onkey(0, 0x26, win);
    os88_onkey('b', 0x30, win); os88_onkey('i', 0x17, win); os88_onkey('g', 0x22, win);
    os88_onkey(13, 0x1C, win);
    lock_depth = 0;
    n_reads = 0;
    wake();
    audit("big load");
    expect_row("big load", 6, "BIG.COM: too big for the TPA");
    if (n_reads != 1) { printf("FAIL: a too-big .COM was read %u times (the fallback would hide the fact)\n", n_reads); fails++; }
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: a refused load did not go back to the interrupted prompt (mode %d)\n", rc_mode); fails++; }
    /* a key typed BETWEEN Enter and the load wake (the floppy is ~400 ms on
     * the target) goes to the ring and must not park: os88_onkey does not
     * flip BLOCKED->RUN while the loader is active, and the refused load
     * hands the machine back RUNNING so the CCP's trap retries on that key */
    lock_depth = 1;
    os88_onkey(0, 0x26, win);
    os88_onkey('b', 0x30, win); os88_onkey('i', 0x17, win); os88_onkey('g', 0x22, win);
    os88_onkey(13, 0x1C, win);
    os88_onkey('q', 0x10, win);              /* meanwhile */
    lock_depth = 0;
    if (rc_mode != RC_M_BLOCKED || rc_ldmode != RC_LD_LOAD) { printf("FAIL: a key during the pending load flipped the mode (%d) or ended the load (%d)\n", rc_mode, rc_ldmode); fails++; }
    wake();
    if (rc_mode != RC_M_RUN || rc_khead == rc_ktail) { printf("FAIL: a refused load with a key queued left mode %d (wanted RUN so the trap retries)\n", rc_mode); fails++; }
    rc_key_pop();                             /* the 'q' */
    rc_mode = RC_M_BLOCKED;
    /* Esc at the loader's prompt goes back too */
    lock_depth = 1; os88_onkey(0, 0x26, win); os88_onkey(27, 0x01, win); lock_depth = 0;
    if (rc_ldmode != 0 || rc_mode != RC_M_BLOCKED) { printf("FAIL: Esc at the loader's prompt left ldmode %d mode %d\n", rc_ldmode, rc_mode); fails++; }
    feed("\033[2J\033[12;20H");         /* a clean screen, the cursor parked */
    wake();

    /* the blocked read: BDOS 1 on an empty ring stops the machine WITHOUT a
     * re-post; the key kicks it, the trap is retried, the echo lands */
    zscript_reset();
    zstep(RC_RUN_BDOS, 0xED01, 0x0001, 0);           /* BDOS 1: C_READ */
    zstep(RC_RUN_BIOS, 0xFF04, 0, 0);
    rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
    feed("\r\n");
    { int w;
      wake();
      audit("blocked read");
      if (rc_mode != RC_M_BLOCKED) { printf("FAIL: an empty ring did not block the read (mode %d)\n", rc_mode); fails++; }
      if (rc_z.pc != 0xED00) { printf("FAIL: the trap was not put back for the retry (pc %04x)\n", rc_z.pc); fails++; }
      w = wakes_posted;
      wake();
      if (wakes_posted != w) { printf("FAIL: a blocked machine re-posted its wake\n"); fails++; }
      lock_depth = 1;
      os88_onkey('k', 0x25, win);
      lock_depth = 0;
      if (rc_mode != RC_M_RUN || wakes_posted == w) { printf("FAIL: the key did not kick the blocked machine\n"); fails++; }
      callreset();
      wake();
      audit("read retried");
      expect_row("echo", 12, "k");
      if ((rc_z.af & 0xFF) != 'k') { printf("FAIL: C_READ answered %02x\n", rc_z.af & 0xFF); fails++; }
      if (rc_mode != RC_M_RUN || rc_z.pc != 0xE400) { printf("FAIL: the WBOOT after the read did not reboot the CCP (mode %d)\n", rc_mode); fails++; }
    }
    if (table) cost("a key into a blocked C_READ: the echo (+ the reboot)");
    zscript_reset();

    /* echo one character - the keystroke path: the machine waits in C_READ,
     * os88_onkey pushes and kicks, the retried trap pops and echoes, the
     * flush draws: ONE band, the character and the moved cursor */
    feed("\033[2J\033[14;1H");         /* a clean screen, the cursor parked
                                          * on an empty row */
    wake();
    zstep(RC_RUN_BDOS, 0xED01, 0x0001, 0);           /* C_READ, no key yet */
    rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
    wake();
    if (rc_mode != RC_M_BLOCKED) { printf("FAIL: echo test: not blocked\n"); fails++; }
    callreset();
    lock_depth = 1;
    os88_onkey('a', 0x1E, win);
    lock_depth = 0;
    wake();
    audit("echo a");
    expect_row("echo a", 13, "a");
    expect_cursor("echo a", 1, 13);
    if (calls() != 1 || n_cells != 2) { printf("FAIL: echo one character cost %d calls / %d cells, wanted 1 / 2\n", calls(), n_cells); fails++; }
    if (table) cost("echo one character (cell + cursor)");
    rc_mode = RC_M_IDLE; zscript_reset();

    /* THE ADAPTATION IS BLIND TO AN EARLY-ENDED SLICE. Twelve keys into a
     * program that blocks in C_READ after each (TE, MBASIC: every key is one
     * wake, one retried trap, one echo, blocked again) with the tick standing
     * still - the shape that once walked the budget from 512 to the 16,384
     * cap in ~30 keystrokes and handed the next busy slice ~1.6 s of UI task
     * (found by the review) - leaves rc_slice_n where it was; and four
     * EXHAUSTED slices inside one tick still double it. */
    {
        int n0 = 512, k;
        rc_slice_n = n0; rc_sl_fast = 0;         /* the 8086's first guess */
        for (k = 0; k < 13; k++) zstep(RC_RUN_BDOS, 0xED01, 0x0001, 0);   /* C_READ x13 */
        rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
        tick_step = 0;
        wake();                                  /* blocks on the first */
        if (rc_mode != RC_M_BLOCKED) { printf("FAIL: adaptation row: not blocked\n"); fails++; }
        for (k = 0; k < 12; k++) {
            lock_depth = 1; os88_onkey('a' + k, 0x1E, win); lock_depth = 0;
            wake();                              /* the retry, the echo, blocked again */
            if (rc_mode != RC_M_BLOCKED) { printf("FAIL: adaptation row: key %d did not block again (mode %d)\n", k, rc_mode); fails++; break; }
        }
        if (rc_slice_n != n0) { printf("FAIL: 12 keys into a blocked C_READ moved the slice budget %d -> %d (an early-ended slice must not adapt it)\n", n0, rc_slice_n); fails++; }
        zscript_reset();                         /* nothing scripted: the slice
                                                  * EXHAUSTS its budget */
        rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
        rc_sl_fast = 0;
        for (k = 0; k < 4; k++) wake();
        if (rc_slice_n != n0 << 1) { printf("FAIL: four exhausted slices in one tick left the budget at %d (from %d), wanted it doubled\n", rc_slice_n, n0); fails++; }
        for (k = 0; k < 4; k++) wake();
        if (rc_slice_n != n0 << 2) { printf("FAIL: eight exhausted slices in one tick left the budget at %d, wanted x4\n", rc_slice_n); fails++; }
        tick_step = 1;
        wake(); wake();                          /* one tick a slice: holds */
        if (rc_slice_n != n0 << 2) { printf("FAIL: a slice a tick moved the budget to %d\n", rc_slice_n); fails++; }
        tick_step = 2;                           /* two boundaries a slice: halves */
        wake();
        if (rc_slice_n != n0 << 1) { printf("FAIL: a slice across two ticks left the budget at %d, wanted halved\n", rc_slice_n); fails++; }
        tick_step = 1;
        rc_slice_n = n0; rc_sl_fast = 0;
        rc_mode = RC_M_IDLE; zscript_reset();
        feed("\033[14;1H\033[K");               /* the row the echoes landed on */
        wake();
    }

    /* HALT is BOOT's exit (cpu1.h 0x76: Status = STATUS_EXIT): the toast,
     * main.c's "\r\n", then the exit - the worker hired on the wake to close
     * the window; a refused spawn (the task table full) is retried on the
     * next wake, which the machine keeps wanting - and it is not timed */
    {
        int n0 = rc_slice_n, w;
        feed("\033[14;1H");
        wake();                                  /* the cursor move drawn (idle) */
        zstep(RC_RUN_HALT, 0x0123, 0, 0);
        rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
        last_toast[0] = 0;
        spawn_refuses = 1;
        tick_step = 0;
        w = wakes_posted;
        callreset();
        wake();
        tick_step = 1;
        /* main.c's exit "\r\n" goes into the model and is NOT drawn: nothing
         * outlives the window (a flush here would be a scroll destroyed
         * within a tick - the double-draw class); no audit, since glass and
         * model deliberately differ by that line until the destroy */
        if (calls() != 0) { printf("FAIL: the exit's \\r\\n was drawn (%d calls) - a draw nobody can see\n", calls()); fails++; }
        if (lock_depth != 0) { printf("FAIL: halt: lock left held\n"); fails++; }
        if (strcmp(last_toast, "RunCPM: CPU halted") != 0) { printf("FAIL: HALT toasted '%s'\n", last_toast); fails++; }
        if (rc_mode != RC_M_EXIT || rc_status != RC_ST_EXIT) { printf("FAIL: HALT left mode %d status %d, wanted EXIT / EXIT\n", rc_mode, rc_status); fails++; }
        if (wakes_posted == w) { printf("FAIL: an exit whose worker was refused did not re-post\n"); fails++; }
        expect_cursor("halt crlf", 0, 14);
        if (rc_slice_n != n0) { printf("FAIL: a HALT slice adapted the budget\n"); fails++; }
        spawn_refuses = 0;
        wake();                                  /* the retry takes */
        if (rc_mode != RC_M_DEAD || spawned != 1) { printf("FAIL: the exit's worker was not hired on the retry (mode %d spawned %d)\n", rc_mode, spawned); fails++; }
        {   int w2 = wakes_posted; wake();
            if (wakes_posted != w2) { printf("FAIL: a dead machine re-posted its wake\n"); fails++; } }
        /* the worker: destroys under the lock, dies in task_alive (here: the
         * longjmp) - the window is gone, the lock is not held */
        if (setjmp(worker_end) == 0)
            os88_worker(win);
        if (destroyed != 1 || lock_depth != 0) { printf("FAIL: the worker: destroyed %d, lock %d\n", destroyed, lock_depth); fails++; }
        destroyed = 0; spawned = 0;
        zscript_reset();
    }

    /* BIOS MOVE goes through the mover, ascending, and leaves cpm.h's
     * registers (HL and DE past the block, BC = 0xFFFF); BDOS 9 with no '$'
     * anywhere in the 64KB stops after one lap instead of never */
    {
        int k;
        for (k = 0; k < 300; k++) zram[0x3000 + k] = (unsigned char)k;
        zram[0x3100] = 0xAA;                    /* inside the destination: an
                                                 * overlap the ascending copy
                                                 * smears exactly as cpm.h */
        zstep(RC_RUN_BIOS, 0xFF00 + 75 + 1, 300, 0x3000);   /* MOVE: BC=300 (DE)->(HL) */
        rc_z.hl = 0x3100;
        rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
        wake();
        /* ascending, so the overlap smears as cpm.h's loop does: source byte
         * 0x3100 is read at k = 0x100, by which time it holds source byte
         * 0x3000 (= 0), so dest[0x100] = 0 and dest[0x12B] = 0x2B */
        if (zram[0x3100] != 0 || zram[0x3101] != 1 || zram[0x31FF] != 0xFF ||
            zram[0x3200] != 0 || zram[0x322B] != 0x2B) { printf("FAIL: MOVE bytes: %02x %02x %02x %02x %02x\n", zram[0x3100], zram[0x3101], zram[0x31FF], zram[0x3200], zram[0x322B]); fails++; }
        if (rc_z.hl != 0x3100 + 300 || rc_z.de != 0x3000 + 300 || rc_z.bc != 0xFFFF) { printf("FAIL: MOVE registers hl %04x de %04x bc %04x (cpm.h: past the block, BC = FFFF)\n", rc_z.hl, rc_z.de, rc_z.bc); fails++; }
        zscript_reset();
        memset(zram + 0x4000, 'x', 0x100);      /* no '$' anywhere: zram is
                                                 * zeros and 'x' */
        for (k = 0; k < 0x10000; k++) if (zram[k] == '$') zram[k] = 'y';
        zstep(RC_RUN_BDOS, 0xED01, 0x0009, 0x4000);
        rc_mode = RC_M_RUN; rc_status = RC_ST_RUNNING;
        wake();
        audit("bdos 9 no $");
        if (rc_z.de != 0x4001) { printf("FAIL: BDOS 9 with no '$' left DE %04x (one lap and one past)\n", rc_z.de); fails++; }
        zscript_reset(); rc_mode = RC_M_IDLE;
        memcpy(zram + 0x100, hello_bytes, 40);  /* put the hello back */
        feed("\033[2J\033[14;1H");
        wake();
    }

    /* cursor-only move to another row: the old cursor cell and the new */
    callreset();
    feed("\033[14;10H");
    wake();
    audit("cursor move");
    expect_cursor("cursor move", 9, 13);
    if (calls() != 2 || n_cells != 2 || n_blit != 2) { printf("FAIL: cursor move to another row cost %d calls / %d cells / %d bands, wanted 2 / 2 / 2\n", calls(), n_cells, n_blit); fails++; }
    if (table) cost("cursor-only move to another row");

    /* cursor-only move on the SAME row, far: cols 9 -> 19 is a span of 11,
     * past the measured crossover (SPEC.md 71.2: a band is 860 us + 173 a
     * cell, so two 1-cell bands at 2.07 ms beat a span of 8 or more), so it
     * is two 1-cell bands */
    callreset();
    feed("\033[14;20H");
    wake();
    audit("cursor move same row");
    if (calls() != 2 || n_cells != 2) { printf("FAIL: cursor move along the row by 10 cost %d calls / %d cells, wanted 2 / 2 (split)\n", calls(), n_cells); fails++; }
    if (table) cost("cursor-only move on the same row, 10 apart");

    /* ...and near: cols 19 -> 22 is a span of 4, one band */
    callreset();
    feed("\033[14;23H");
    wake();
    audit("cursor move same row near");
    if (calls() != 1 || n_cells != 4) { printf("FAIL: cursor move along the row by 3 cost %d calls / %d cells, wanted 1 / 4 (the span)\n", calls(), n_cells); fails++; }
    if (table) cost("cursor-only move on the same row, 3 apart");

    /* a DIR listing that reaches the bottom and scrolls: 25 rows fed one at
     * a time, each row a wake, so the last ones are the scrolled case */
    feed("\r");
    for (i = 0; i < 20; i++) {
        sprintf(line, "A: FILE%02d   COM : FILE%02d   TXT : FILE%02d   BAS : FILE%02d   ASM\r\n", i, i, i, i);
        callreset();
        feed(line);
        wake();
        audit("dir row");
        if (i == 19) {
            /* the scroll, ONE fill of the vacated 8-px strip, the text row's
             * band and the cursor's 1-cell band on the vacated row (a
             * window of fewer rows shows neither the text row nor the
             * cursor's: model rows 23 and 24 are below a 17-row window) */
            if (vis_rows() == 25 && (n_scroll != 1 || n_fill != 1 || n_blit != 2 || calls() != 4)) { printf("FAIL: a scrolled DIR row cost %d scroll / %d fill / %d bands / %d calls, wanted 1 / 1 / 2 / 4\n", n_scroll, n_fill, n_blit, calls()); fails++; }
            if (table) cost("one DIR row at the bottom (scroll+fill+2 rows)");
        }
    }
    expect_cursor("dir bottom", 0, 24);
    expect_row("dir last", 23, "A: FILE19   COM : FILE19   TXT : FILE19   BAS : FILE19   ASM");
    expect_row("dir first", 4, "A: FILE00   COM : FILE00   TXT : FILE00   BAS : FILE00   ASM");

    /* three lines in one slice: ONE scroll of three */
    callreset();
    feed("line one\r\nline two\r\nline three\r\n");
    wake();
    audit("three lines");
    expect_row("three lines", 23, "line three");
    expect_row("three lines b", 21, "line one");
    if (table) cost("three lines in one slice (one scroll of 3)");

    /* the pending wrap: 80 characters then more */
    callreset();
    for (i = 0; i < 80; i++) rc_putc('0' + (i % 10));
    wake();
    audit("80 chars");
    expect_cursor("80 chars pending", 80, 24);
    feed("X");
    wake();
    audit("wrap");
    expect_row("wrap", 23, "01234567890123456789012345678901234567890123456789012345678901234567890123456789");
    expect_row("wrap x", 24, "X");
    expect_cursor("wrap", 1, 24);

    /* ESC[s at the pending wrap, ESC[u, then a character: the wrap must
     * still happen (the reviewer's row: wave 1 clamped the restored column) */
    feed("\r\n");
    for (i = 0; i < 80; i++) rc_putc('a' + (i % 26));
    feed("\033[s\033[1;1H\033[u");
    expect_cursor("save/restore at the wrap", 80, 24);
    feed("Q");
    wake();
    audit("wrap after restore");
    expect_row("wrap after restore", 24, "Q");
    expect_cursor("wrap after restore", 1, 24);

    /* backspace and CR at the pending wrap */
    feed("\r\n");
    for (i = 0; i < 80; i++) rc_putc('a');
    rc_putc(8); rc_putc('b');           /* BS from the pending wrap: col 78 */
    wake();
    audit("bs at wrap");
    if (vis_rows() == 25 && (glass_ch(24, 78) != 'b' || (vis_cols() == 80 && glass_ch(24, 79) != 'a'))) { printf("FAIL: BS at the pending wrap\n"); fails++; }
    rc_putc(13);
    expect_cursor("cr at wrap", 0, 24);

    /* ESC[2J: one fill and the cursor */
    callreset();
    feed("\033[2J\033[H");
    wake();
    audit("clear");
    expect_cursor("clear", 0, 0);
    if (table) cost("ESC[2J ESC[H (fill + cursor)");
    for (i = 0; i < vis_rows(); i++) expect_row("clear rows", i, "");

    /* ...and a cursor-only move on the still-empty screen must NOT fill it
     * again (rc_all_blank is consumed by the flush that acted on it): the
     * shape of CLS then a cursor position then a wait for a key */
    callreset();
    feed("\033[3;5H");
    wake();
    audit("move after clear");
    expect_cursor("move after clear", 4, 2);
    if (n_fill != 0 || calls() > 2) { printf("FAIL: cursor move after ESC[2J cost %d fills / %d calls\n", n_fill, calls()); fails++; }
    if (table) cost("cursor-only move after ESC[2J (no refill)");
    callreset();
    feed("\033[?25l\033[?25h\033[10;20H");
    wake();
    audit("hide/show/move after clear");
    if (n_fill != 0) { printf("FAIL: ?25l/?25h/move after ESC[2J refilled (%d)\n", n_fill); fails++; }
    feed("\033[H");
    wake();

    /* TE-style full-screen redraw: 24 text rows and a reverse status line */
    callreset();
    feed("\033[2J\033[H");
    for (i = 0; i < 24; i++) {
        sprintf(line, "\033[%d;1HRow %02d of a document being edited in TE, seventy-nine columns of text.......", i + 1, i);
        feed(line);
    }
    feed("\033[25;1H\033[7m TE.COM  A:DOC.TXT  Line 1  Col 1  ^J for help                                 \033[0m");   /* 80 cells: the pending wrap, no scroll */
    feed("\033[1;1H");
    wake();
    audit("te redraw");
    expect_row("te row 0", 0, "Row 00 of a document being edited in TE, seventy-nine columns of text.......");
    if (glass_rev(24, 1) != 1 && vis_rows() == 25) { printf("FAIL: status line not reverse\n"); fails++; }
    if (table) cost("TE-style 25-row full-screen redraw");

    /* the same screen drawn again: nothing */
    callreset();
    for (i = 0; i < 24; i++) {
        sprintf(line, "\033[%d;1HRow %02d of a document being edited in TE, seventy-nine columns of text.......", i + 1, i);
        feed(line);
    }
    feed("\033[1;1H");
    wake();
    audit("te same");
    if (calls() != 0) { printf("FAIL: redrawing an identical screen cost %d calls\n", calls()); fails++; }
    if (table) cost("the identical screen written again");

    /* the cursor DRAWN on row 20, then ESC[M from row 4: the glass row that
     * carried the cursor moved up to 19 with the scroll, and its shadow row
     * (cursor bit and all) with it - the flush must compare row 19, or the
     * old cursor stays on the glass there (the dirty flags now rotate with
     * the rows, so the flush tracks the drawn cursor's row itself) */
    feed("\033[21;5H");
    wake();
    audit("cursor at 20");
    callreset();
    feed("\033[5;1H\033[M");
    wake();
    audit("ESC[M with the cursor elsewhere");
    expect_row("ESC[M elsewhere", 4, "Row 05 of a document being edited in TE, seventy-nine columns of text.......");
    if (vis_rows() == 25 && (n_scroll != 1 || n_fill != 1)) { printf("FAIL: ESC[M elsewhere cost %d scroll / %d fill\n", n_scroll, n_fill); fails++; }
    if (table) cost("ESC[M at row 4, cursor drawn at row 20");
    feed("\033[5;1H\033[L\033[5;1HRow 04 of a document being edited in TE, seventy-nine columns of text.......\033[1;1H");
    wake();
    audit("te restored");

    /* insert and delete line at the cursor (ESC[L / ESC[M) */
    callreset();
    feed("\033[5;1H\033[L");
    wake();
    audit("insert line");
    expect_row("insert line", 4, "");
    expect_row("insert line b", 5, "Row 04 of a document being edited in TE, seventy-nine columns of text.......");
    if (vis_rows() == 25)
        expect_row("insert line c", 23, "Row 22 of a document being edited in TE, seventy-nine columns of text.......");
    feed("\033[M");
    wake();
    audit("delete line");
    expect_row("delete line", 4, "Row 04 of a document being edited in TE, seventy-nine columns of text.......");
    if (table) cost("ESC[L then ESC[M (two wakes)");

    /* erase to end of line, hide the cursor, show it */
    callreset();
    feed("\033[3;10H\033[K");
    wake();
    audit("erase eol");
    expect_row("erase eol", 2, "Row 02 of");
    feed("\033[?25l");
    wake();
    audit("cursor hidden");
    if (glass_rev(2, 9) != 0) { printf("FAIL: cursor still shown after ?25l\n"); fails++; }
    if (model_rev(2, 9) != 0) { printf("FAIL: model cursor still on after ?25l\n"); fails++; }
    feed("\033[?25h");
    wake();
    audit("cursor shown");
    if (glass_rev(2, 9) != 1) { printf("FAIL: cursor not shown after ?25h\n"); fails++; }
    if (table) cost("erase to EOL, hide cursor, show cursor (3 wakes)");

    /* an unknown escape is swallowed whole */
    feed("\033[31;1m\033=\033[6nplain");
    wake();
    audit("unknown esc");
    expect_row("unknown esc", 2, "Row 02 ofplain");

    /* a partial expose: only the damaged rows are drawn */
    callreset();
    damage_whole = 0;
    damage_rect.x1 = cont_x + 16; damage_rect.x2 = cont_x + 100;
    damage_rect.y1 = cont_y + 40; damage_rect.y2 = cont_y + 60;
    /* somebody else drew there */
    { int r, c; for (r = 5; r <= 7; r++) for (c = 2; c <= 12; c++) { gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; gl_rev[cont_y + r*8][cont_x/8 + c] = 1; } }
    lock_depth = 1; os88_paint(win); lock_depth = 0;
    damage_whole = 1;
    audit("partial expose");
    /* ONE fill of the damage and then only the text under it (three rows
     * of TE text here: three bands to their spans), never a band the width
     * of the damage per row over blank cells */
    if (n_fill != 1 || n_blit != 3 || calls() != 4) { printf("FAIL: partial expose cost %d fills / %d bands / %d calls, wanted 1 / 3 / 4\n", n_fill, n_blit, calls()); fails++; }
    if (table) cost("partial expose of 3 rows x 11 cells");
    /* ...and the same damage over BLANK rows costs the fill alone (the
     * scribble covers the two rows the rect holds whole: the fake glass is
     * cell-atomic and cannot show a cell whose bottom three pixel rows were
     * never covered, which the real fill leaves as they were - ours) */
    feed("\033[2J\033[H");
    wake();
    callreset();
    damage_whole = 0;
    { int r, c; for (r = 5; r <= 6; r++) for (c = 2; c <= 12; c++) { gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; gl_rev[cont_y + r*8][cont_x/8 + c] = 1; } }
    lock_depth = 1; os88_paint(win); lock_depth = 0;
    damage_whole = 1;
    audit("partial expose, blank");
    if (n_fill != 1 || calls() != 1) { printf("FAIL: partial expose over blank rows cost %d fills / %d calls, wanted 1 / 1\n", n_fill, calls()); fails++; }
    if (table) cost("partial expose of 3 rows x 11 blank cells");
    for (i = 0; i < 24; i++) {
        sprintf(line, "\033[%d;1HRow %02d of a document being edited in TE, seventy-nine columns of text.......", i + 1, i);
        feed(line);
    }
    feed("\033[25;1H\033[7m TE.COM  A:DOC.TXT  Line 1  Col 1  ^J for help                                 \033[0m\033[1;1H");
    wake();
    audit("te again");

    /* a full expose of the TE screen */
    callreset();
    { int r, c; for (r = 0; r < 25; r++) for (c = 0; c < 80; c++) { gl_ch[cont_y + r*8][cont_x/8 + c] = '#'; } }
    expose();
    audit("full expose");
    if (table) cost("full expose repaint of the TE screen");

    /* THE ABOUT PANEL (rcabout.c, SPEC.md 71.4) over the full TE screen:
     * the kernel's 'About RunCPM' item dispatches os88_about under the lock;
     * the panel is one fill, two frames, a band per label, the button; the
     * machine is PAUSED while it is up (no slice, no wake, no flush - a
     * byte put into the model meanwhile stays there); Esc / Enter / space /
     * a click on OK take it down; any other key or click is swallowed, Alt+F
     * refused; the close is the panel's rectangle as damage - ONE fill and
     * one band per row the panel covered, never the whole screen - and the
     * machine kicked back. The audit runs only when the panel is down: while
     * it is up the glass is deliberately not the terminal's. */
    if (vis_rows() == 25 || vis_rows() == 17) {
        int calls_open, w0;
        rc_mode = RC_M_RUN;
        callreset();
        lock_depth = 1; os88_about(win); lock_depth = 0;
        if (!rc_abt) { printf("FAIL: About did not open\n"); fails++; }
        calls_open = calls();
        if (n_fill != 2 || n_frame != 4 || n_blit != 8 || n_run != 0) { printf("FAIL: the About panel cost %d fills / %d frames / %d bands / %d runs, wanted 2 / 4 / 8 / 0\n", n_fill, n_frame, n_blit, n_run); fails++; }
        /* twelve rows: the OK button's bottom (its box is y-3..y+10) inside
         * the panel, and the panel inside the content box - on every
         * geometry, the 17-row CGA one included (LESSONS.md 8) */
        if (rc_ab_by + 10 > rc_ab_y + rc_ab_h - 2 || rc_ab_y + rc_ab_h > cont_y + cont_h - 1 || rc_ab_x + rc_ab_w > cont_x + cont_w - 1 || rc_ab_x < cont_x || rc_ab_y < cont_y) { printf("FAIL: About: OK at y %d..%d, panel %d..%d, content %d..%d - a control off the panel\n", rc_ab_by - 3, rc_ab_by + 10, rc_ab_y, rc_ab_y + rc_ab_h, cont_y, cont_y + cont_h - 1); fails++; }
        if (rc_ab_x & 7) { printf("FAIL: About: the panel's x %d is not on a byte boundary\n", rc_ab_x); fails++; }
        if (table) cost("About: the panel (fill, frames, 8 bands, OK)");
        /* paused: a wake runs nothing, posts nothing, flushes nothing; a
         * byte fed meanwhile waits in the model */
        w0 = wakes_posted; callreset();
        feed("\033[24;70Hpaused");
        wake();
        if (calls() != 0 || wakes_posted != w0 || n_rc_run != 0 + n_rc_run) { printf("FAIL: About up: a wake cost %d calls, posted %d\n", calls(), wakes_posted - w0); fails++; }
        { int nr = n_rc_run; wake(); if (n_rc_run != nr) { printf("FAIL: About up: the wake ran a slice\n"); fails++; } }
        /* keys and clicks: swallowed, except the ones that press OK; Alt+F
         * refused */
        rc_khead = rc_ktail = 0;
        lock_depth = 1; os88_onkey('x', 0x2D, win); os88_onkey(0, 0x21, win); lock_depth = 0;
        if (rc_khead != rc_ktail) { printf("FAIL: About up: a key reached the ring\n"); fails++; }
        if (fullscreen || !rc_abt) { printf("FAIL: About up: Alt+F was not refused (fs %d abt %d)\n", fullscreen, rc_abt); fails++; }
        lock_depth = 1; os88_onclick(rc_ab_x + 3, rc_ab_y + 3, win); lock_depth = 0;
        if (!rc_abt) { printf("FAIL: About: a click on the panel's frame closed it\n"); fails++; }
        /* Esc: the close - fill + the rows under the panel, then the audit
         * finds model == glass == shadow again, and the machine is kicked */
        callreset(); w0 = wakes_posted;
        lock_depth = 1; os88_onkey(27, 0x01, win); lock_depth = 0;
        if (rc_abt) { printf("FAIL: About: Esc did not close it\n"); fails++; }
        audit("about closed (Esc)");
        expect_row("about closed", 23, "Row 23 of a document being edited in TE, seventy-nine columns of textpaused.");
        {   /* the rows the panel covered, plus the row written while it was
             * up and the cursor's row: nothing else */
            int rows_under = (rc_ab_y + rc_ab_h - cont_y) / 8 - (rc_ab_y - cont_y) / 8 + 1;
            if (n_fill != 1 || n_scroll != 0 || n_blit > rows_under + 2 || n_blit < rows_under - 1) { printf("FAIL: About's close cost %d fills / %d bands / %d scrolls over %d rows under it, wanted 1 / ~%d / 0\n", n_fill, n_blit, n_scroll, rows_under, rows_under); fails++; }
            if (n_fill_rows != rc_ab_h + 1) { printf("FAIL: About's close filled %d rows, wanted the panel's %d - a full repaint?\n", n_fill_rows, rc_ab_h + 1); fails++; }
        }
        if (wakes_posted != w0 + 1) { printf("FAIL: About's close did not kick the paused machine\n"); fails++; }
        if (table) cost("About: Esc (the panel's rect as damage)");
        /* the click on OK, and Enter, do the same */
        lock_depth = 1; os88_about(win); lock_depth = 0;
        callreset();
        lock_depth = 1; os88_onclick(rc_ab_bx + 20, rc_ab_by + 3, win); lock_depth = 0;
        if (rc_abt) { printf("FAIL: About: a click on OK did not close it\n"); fails++; }
        audit("about closed (OK)");
        if (n_fill != 1) { printf("FAIL: About's OK click cost %d fills\n", n_fill); fails++; }
        lock_depth = 1; os88_about(win); lock_depth = 0;
        lock_depth = 1; os88_onkey(13, 0x1C, win); lock_depth = 0;
        if (rc_abt) { printf("FAIL: About: Enter did not close it\n"); fails++; }
        audit("about closed (Enter)");
        /* a kernel repaint while the panel is up draws the panel again over
         * the exposed terminal, from the live geometry */
        lock_depth = 1; os88_about(win); lock_depth = 0;
        callreset();
        expose();
        if (!rc_abt || n_frame != 4) { printf("FAIL: About: a whole repaint did not redraw the panel (%d frames)\n", n_frame); fails++; }
        lock_depth = 1; os88_onkey(27, 0x01, win); lock_depth = 0;
        audit("about closed after expose");
        wake();
        rc_mode = RC_M_IDLE;
        (void)calls_open;
    }

    /* Alt+F: the latch paints us whole INSIDE the slot (the stub does what
     * wm_fullscreen does), and the flush that follows must find nothing to
     * do - the shadow describes the new glass. Both directions. */
    callreset();
    lock_depth = 1;                        /* W_ONKEY is dispatched under it */
    os88_onkey(0, 0x21, win);
    lock_depth = 0;
    if (!fullscreen) { printf("FAIL: Alt+F did not enter fullscreen\n"); fails++; }
    audit("alt-f enter");
    if (table) cost("Alt+F enter: the latch's own whole paint (80x25)");
    callreset();
    wake();
    audit("after alt-f enter");
    if (calls() != 0) { printf("FAIL: the flush after Alt+F enter cost %d calls (a second draw of the same screen)\n", calls()); fails++; }
    if (table) cost("the flush after Alt+F enter");
    callreset();
    lock_depth = 1;
    os88_onkey(0, 0x21, win);
    lock_depth = 0;
    if (fullscreen) { printf("FAIL: Alt+F did not leave fullscreen\n"); fails++; }
    audit("alt-f exit");
    callreset();
    wake();
    audit("after alt-f exit");
    if (calls() != 0) { printf("FAIL: the flush after Alt+F exit cost %d calls\n", calls()); fails++; }
    if (table) cost("the flush after Alt+F exit");

    /* the blit1-refused fallback: font_run per uniform span */
    blit1_refuses = 1;
    callreset();
    feed("\033[10;1H\033[Kfallback \033[7mreverse\033[0m plain again");
    wake();
    audit("blit1 refused");
    if (table) cost("a mixed row with blit1 refused (font_run runs)");
    blit1_refuses = 0;

    /* the scroll-refused fallback: rows redrawn */
    scroll_refuses = 1;
    callreset();
    feed("\033[25;1H\r\nscrolled without gfx_scroll\r\n");
    wake();
    audit("scroll refused");
    if (table) cost("two scrolled lines with gfx_scroll refused");
    scroll_refuses = 0;

    /* the bell rings outside the lock */
    rc_bells = 0;
    feed("\007\007");
    wake();
    if (rc_bells != 0) { printf("FAIL: bells not serviced\n"); fails++; }

    /* a click kicks a running machine (the plan's 'kick from every callback':
     * a wake the full event ring refused is re-posted by the next callback)
     * and leaves an idle one alone */
    { int w = wakes_posted;
      rc_mode = RC_M_RUN;
      lock_depth = 1; os88_onclick(100, 100, win); lock_depth = 0;
      if (wakes_posted != w + 1) { printf("FAIL: a click did not kick the running machine\n"); fails++; }
      rc_mode = RC_M_IDLE;
      lock_depth = 1; os88_onclick(100, 100, win); lock_depth = 0;
      if (wakes_posted != w + 1) { printf("FAIL: a click kicked an idle machine\n"); fails++; }
    }

    /* the keys without an ASCII code go into the ring as the VT100 sequence
     * a host terminal sends RunCPM (SPEC.md 71.2): Up ESC[A, Del ESC[3~;
     * Ctrl+2 is a NUL; a key the table does not name goes nowhere */
    rc_khead = rc_ktail = 0;
    lock_depth = 1;
    os88_onkey(0, 0x48, win); os88_onkey(0, 0x53, win); os88_onkey(0, 0x03, win); os88_onkey(0, 0x3B, win);   /* Up, Del, Ctrl+2, F1 */
    lock_depth = 0;
    {   static const unsigned char want[] = { 27, '[', 'A', 27, '[', '3', '~', 0 };
        int k, bad = 0;
        for (k = 0; k < 8; k++) if (rc_key_pop() != want[k]) bad = 1;
        if (rc_key_pop() != -1) bad = 1;
        if (bad) { printf("FAIL: Up / Del / Ctrl+2 / F1 did not ring as ESC[A ESC[3~ NUL and nothing\n"); fails++; }
    }

    /* input overrun is never silent: the 64-entry ring holds 63 keys, and
     * the key that finds it full RINGS (the PC BIOS's own answer to a full
     * keyboard buffer), at no glass cost */
    rc_khead = rc_ktail = 0; rc_bells = 0;
    lock_depth = 1;
    for (i = 0; i < 63; i++) os88_onkey('a', 0x1E, win);
    if (rc_bells != 0) { printf("FAIL: the ring rang before it was full (%d)\n", i); fails++; }
    os88_onkey('b', 0x30, win);
    if (rc_bells != 1) { printf("FAIL: the 64th key into a full ring did not ring (%d)\n", rc_bells); fails++; }
    lock_depth = 0;
    callreset();
    wake();
    if (calls() != 0) { printf("FAIL: an overrun cost %d calls\n", calls()); fails++; }
    if (rc_bells != 0) { printf("FAIL: the overrun bell was not serviced\n"); fails++; }
    rc_khead = rc_ktail = 0;

    /* A 40KB TYPE (docs/RUNCPM-PORT-PLAN.md wave 5): 512 lines of 78
     * characters put out three lines a slice - the pacing the target's slice
     * length gives a TYPE loop (~2 control transfers a byte, a few hundred
     * bytes a slice on the 8088) - and flushed once a slice. Nothing may
     * repaint more than it changed: ONE scroll and ONE fill of the vacated
     * strip per flush (never per line, never a full-content fill), the
     * three text rows and the cursor's row as bands - 6 calls a slice, 2 a
     * line - and the coalescing holds for the whole file: scrolls == flushes
     * <= lines. QEMU puts hundreds of lines in a slice and gets one scroll
     * for all of them; the count is what SPEC.md 71.2 states. */
    feed("\033[2J\033[H");
    wake();
    rc_mode = RC_M_IDLE;
    callreset();
    {
        int ln, flushes = 0, bytes = 0, full_fills = 0;
        for (ln = 0; ln < 512; ln++) {
            sprintf(line, "%03d: the quick brown fox jumps over the lazy dog - forty kilobytes of it......\r\n", ln);
            feed(line);
            bytes += (int)strlen(line);
            if (ln % 3 == 2) {
                int f0 = n_fill_rows;
                wake();
                flushes++;
                if (n_fill_rows - f0 >= vis_rows() * 8) full_fills++;
            }
        }
        wake();
        audit("40KB TYPE");
        expect_row("40KB TYPE last", 23, "511: the quick brown fox jumps over the lazy dog - forty kilobytes of it......");
        if (bytes < 40000) { printf("FAIL: the TYPE fixture is %d bytes, not 40KB\n", bytes); fails++; }
        /* the first flushes fill the screen without scrolling (rows 0..23
         * of a 25-row window); every one after scrolls ONCE */
        if (n_scroll > 512) { printf("FAIL: 40KB TYPE: %d scrolls for 512 lines - more than one a line\n", n_scroll); fails++; }
        if (n_scroll < flushes - 8) { printf("FAIL: 40KB TYPE: %d scrolls for %d flushes - lines drawn instead of scrolled\n", n_scroll, flushes); fails++; }
        if (full_fills != 0) { printf("FAIL: 40KB TYPE: %d full-content fills - a full repaint\n", full_fills); fails++; }
        if (n_fill > flushes) { printf("FAIL: 40KB TYPE: %d fills for %d flushes\n", n_fill, flushes); fails++; }
        if (calls() > 512 * 2 + 4 * flushes) { printf("FAIL: 40KB TYPE: %d calls for 512 lines / %d flushes - more than a scroll, a fill, the rows and the cursor per flush\n", calls(), flushes); fails++; }
        if (table) cost("40KB TYPE: 512 lines, 3 a slice (171 flushes)");
    }
}

/* a disk without CCP-DR.60K: the launch is not refused (the claims were had);
 * main.c 118's text follows the banner and the machine idles, the window up
 * to show it (SPEC.md 71) - and Alt+L is still there */
static void run_noccp(void)
{
    int i;
    cont_w = 640; cont_h = 200;
    gclear(); callreset(); wakes_posted = 0;
    std_clus = 0; std_vol = 0; ferr = 0;
    ticks = 100; tick_step = 1; zscript_reset(); n_rc_run = 0;
    zclk_left = ZCLK_BURST; clk_restarts = clk_slices = clk_halts = 0;
    memset(zram, 0, sizeof(zram)); memset(ccpram, 0, sizeof(ccpram));
    rc_khead = rc_ktail = 0; n_claims = 0; ccp_missing = 1; autoexec_text = 0;
    rc_mode = RC_M_CLOCK; rc_ldmode = 0; rc_status = 0;
    win = os88_main();
    if (!win) { printf("FAIL: no CCP: os88_main refused the launch (%s)\n", last_toast); fails++; return; }
    if (rc_ccplen != 0) { printf("FAIL: no CCP: rc_ccplen %u\n", rc_ccplen); fails++; }
    expose();
    tick_step = 0;
    for (i = 0; i < 200 && rc_mode == RC_M_CLOCK; i++) wake();
    tick_step = 1;
    audit("no ccp");
    expect_row("no ccp head", 10, "RunCPM Version 6.9 (CCP-DR.60K) - CP/M 2.2");
    expect_row("no ccp 1", 11, "Unable to load CP/M CCP.");
    expect_row("no ccp 2", 12, "CPU halted.");
    if (rc_mode != RC_M_IDLE) { printf("FAIL: no CCP: mode %d, wanted idle\n", rc_mode); fails++; }
    {   int w = wakes_posted; wake();
        if (wakes_posted != w) { printf("FAIL: no CCP: the idle machine re-posted\n"); fails++; } }
    lock_depth = 1; os88_onkey(0, 0x26, win); lock_depth = 0;
    if (rc_ldmode != RC_LD_PROMPT) { printf("FAIL: no CCP: Alt+L refused\n"); fails++; }
    lock_depth = 1; os88_onkey(27, 0x01, win); lock_depth = 0;
    ccp_missing = 0;
}

int main(void)
{
    printf("rcuitest: RUNCPM terminal against a model of the glass\n");
    run_all(80, 25, 1);                 /* fullscreen / Hercules framed */
    print_costs();
    run_all(79, 25, 0);                 /* VGA framed (632 px of content) */
    run_all(78, 17, 0);                 /* CGA framed */
    run_noccp();
    if (fails) {
        printf("rcuitest: %d FAILURE(S)\n", fails);
        return 1;
    }
    printf("rcuitest: all checks passed on 80x25, 79x25 and 78x17\n");
    return 0;
}
