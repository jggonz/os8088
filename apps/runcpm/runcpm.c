/* ============================================================================
 * os8088 - apps/runcpm/runcpm.c        RUNCPM: a CP/M 2.2 emulator, in C
 *
 * A reimplementation of RunCPM 6.9 by Marcelo Dantas / "Mockba the Borg"
 * (https://github.com/MockbaTheBorg/RunCPM) for os8088, as one C package
 * (SPEC.md 71, built by SPEC.md 70's toolchain). RunCPM is MIT licensed:
 *
 *   Copyright (c) 2017 Mockba the Borg (RunCPM/LICENSE); the source files
 *   read "Copyright (c) 2016 - Marcelo Dantas". This port carries RunCPM's
 *   BEHAVIOUR - its banner and texts, its memory layout, its BIOS and BDOS
 *   contracts, its disk-as-folders model, its console semantics - taken from
 *   the files each part names in its header, and reimplements them in the C
 *   this toolchain compiles plus hand-written 8086 for the one hot loop.
 *   Nothing from RunCPM is vendored (CONTRIBUTING.md 6); the attribution is
 *   here, in every file that carries derived material, and in the About box.
 *
 * WHAT IT IS (SPEC.md 71). A Z80 (rcz80.inc, wave 2) running CP/M 2.2 in a
 * 64KB heap claim, a BDOS and BIOS in C (rccpm.c, wave 3), CP/M drives as
 * folders on the floppy (rcfs.c, wave 4), an 80x25 terminal drawn in a
 * window (rcterm.c, this wave), and DRI's CCP as the command processor. The
 * structural difference from RunCPM is that NOTHING BLOCKS: RunCPM sits in
 * the host's getch(); here the emulator runs on the UI task in bounded
 * slices, re-entered through the kernel's wake event (SPEC.md 71.1 -
 * OSAPI_WM_WAKE / OSAPI_WM_ONWAKE, added for it), so the file slots are
 * always legal, the UI stack is the only stack, and a program waiting on a
 * key costs nothing until os88_onkey pushes one and kicks.
 *
 * THIS TRANSLATION UNIT. `nasm -f bin` has no external symbols (SPEC.md
 * 70.1), so the parts are #included here, in this order, and every one of
 * them is a written prerequisite in the Makefile because make cannot see
 * through a #include:
 *
 *   rcterm.c    the terminal model, the VT100 subset, the glass shadow and
 *               the damage-only flush (SPEC.md 71.2)
 *   rccpm.c     the CP/M image constants; wave 3: BIOS, BDOS, the line editor
 *   rcfs.c      the place (drive,user) -> folder; wave 4: files
 *   rcabout.c   About (wave 5)
 *
 * and the shim (runcpm.asm) %includes rcz80.inc, rcmem.inc and rcband.inc.
 *
 * WHAT WAVE 1 IS. The window, the terminal, the shadow, the banner, the wake
 * round trip and the quiet-goto proof, and the host harness that prices the
 * redraw path (hosttest/rcuitest.c). Two pieces of SCAFFOLDING are here and
 * are marked RC_W1: the wake handler counts its round trips on the glass and
 * echoes typed keys through the terminal (there is no Z80 to feed yet), and
 * the first wake runs rcfs.c's goto_q_mark probe. Wave 2 replaces the first
 * with the slice driver, wave 4 the second with the drive layer.
 *
 * THE FOUR RULES (SPEC.md 70.5-70.8), obeyed visibly: every buffer and every
 * out-parameter is static; no struct is ever copied; there is no long, no
 * float, no printf; every frame is small - the report on every build says
 * how small.
 * ==========================================================================*/

#include "os88.h"

/* --- prototypes: the callbacks the shim declares, and the parts' entry
 * points, so the host harness's clang (stricter than SmallerC about an
 * undeclared function) and the target agree (LESSONS.md 3) ---------------- */
void *os88_main(void);
void  os88_paint(void *win);
void  os88_onkey(int ascii, int scan, void *win);
void  os88_oncmd(int item, int menu, void *win);
void  os88_about(void *win);
void  os88_onwake(void *win);

static void rc_term_init(void);
static void rc_putc(int c);
static void rc_puts(const char *s);
static void rc_flush(void *win);
static void rc_sh_inval(void);
static void rc_bell_service(void);
static void rc_fs_init(void);
static int  rc_fs_probe(void);
static void rc_about(void *win);

#include "rcterm.c"
#include "rccpm.c"
#include "rcfs.c"
#include "rcabout.c"

/* ==========================================================================
 * THE PROGRAM
 * ========================================================================*/

/* The window: authored for the 640x480 reference screen (SPEC.md 39) and
 * clamped onto the live one - 640 wide, 18 rows of title and 200 of content
 * = 25 cell rows, at (7,20): under the menu bar, one cell in. On CGA the
 * band is shorter and the window shows what it holds (SPEC.md 71.2). THE +1:
 * a framed window's content is W_H - TITLE_H - 1 rows (kernel/wm.inc
 * wm_geom: the bottom border is inside W_H), so 18 + 200 showed 24 rows and
 * a 7-px sliver, and the model's row 24 - the row a scrolled cursor lives on
 * - was never on the glass. Found on the glass, not in the harness, whose
 * fake window was 200 rows of content by construction. */
#define RC_WIN_X   7
#define RC_WIN_Y   20
#define RC_WIN_W   640
#define RC_WIN_H   (OS88_TITLE_H + RC_ROWS * RC_CELLH + 1)

static const char rc_title[] = "RunCPM";

/* The kernel bar shows the instance name unless a menu set says otherwise
 * (LESSONS.md 8): an EMPTY set whose name is the product. Six bytes of .data
 * rather than struct os88_menuset's 36 (SPEC.md 70.9); not const, because
 * os88_menu_set() writes oncmd. Its handler can never be called. */
struct rc_kmset {
    const char *name;
    int oncmd;
    int nmenus;
};
static struct rc_kmset rc_kmenus = { "RunCPM", 0, 0 };

static unsigned rc_zseg;                     /* the 64KB Z80 RAM claim */
static int rc_running;                       /* the machine wants wakes */
static int rc_full;                          /* fullscreen latch held */
static void *rc_win;

/* the key ring: os88_onkey pushes, the slice pops (wave 3: BDOS CONIN) */
#define RC_KRING 64
static unsigned char rc_kbuf[RC_KRING];
static int rc_khead, rc_ktail;

static int rc_key_push(int c)
{
    int n = (rc_ktail + 1) & (RC_KRING - 1);
    if (n == rc_khead)
        return -1;                          /* full: input overrun, dropped */
    rc_kbuf[rc_ktail] = (unsigned char)c;
    rc_ktail = n;
    return 0;
}

static int rc_key_pop(void)
{
    int c;
    if (rc_khead == rc_ktail)
        return -1;
    c = rc_kbuf[rc_khead];
    rc_khead = (rc_khead + 1) & (RC_KRING - 1);
    return c;
}

/* --- console helpers, console.h's shape: _puthex16 is lowercase (tohex) --- */
static char rc_num[8];
static void rc_puthex16(unsigned w)
{
    int i;
    for (i = 12; i >= 0; i -= 4) {
        int d = (w >> i) & 15;
        rc_putc(d < 10 ? '0' + d : 'a' + d - 10);
    }
}
static void rc_putdec(unsigned n)
{
    rc_puts(os88_utoa(n, rc_num));
}

/* rc_banner - main.c 66-105, line for line, with the three deviations SPEC.md
 * 71 states: 'Built' is the pinned upstream commit's date (a __DATE__ would
 * break byte-for-byte rebuilds), CPU_IS is this core's own name, and the two
 * Z80estimateClock lines arrive with the core in wave 2 (they are measured on
 * this machine, in 16-bit arithmetic) - and a fourth, stated in SPEC.md 71's
 * authority table: main.c's `FILEBASE is ./` line (every desktop build defines
 * FILEBASE, abstraction_posix.h/abstraction_windows.h) is not printed, because
 * the base here is the launch folder and a package has no path string to name
 * it by. */
static void rc_banner(void)
{
    rc_puts("\033[2J\033[H");                /* _clrscr (abstraction_runvt.h) */
    rc_puts("  CP/M Emulator v" RC_VERSION " by Marcelo Dantas\r\n");
    rc_puts("      Built Jul 21 2026 - 20:43:19\r\n");   /* upstream e698e8a */
    rc_puts("----------------------------------------\r\n");
    rc_puts("CPU is ");
    rc_puts("8086 native");                  /* CPU_IS: not cpu1.h's 'Model 1' */
    rc_puts("\r\n");
    /* wave 2: Z80estimateClock() - '<n> T-states in <n> ms', 'Estimated Z80
     * clock speed: N.NN MHz' */
    rc_puts("BIOS at 0x");
    rc_puthex16(RC_BIOSJMPPAGE);
    rc_puts(" - ");
    rc_puts("BDOS at 0x");
    rc_puthex16(RC_BDOSJMPPAGE);
    rc_puts("\r\n");
    rc_puts("BIOS/BDOS using interrupt handoff method\r\n");   /* INT_HANDOFF */
    rc_puts("CCP " RC_CCPNAME " at 0x");
    rc_puthex16(RC_CCPADDR);
    rc_puts("\r\n");
}

/* ==========================================================================
 * RC_W1 SCAFFOLDING - the wake counter and the local echo. Wave 2's slice
 * driver replaces rc_slice() whole: _rc_run(n), the trap, the console drain.
 * ========================================================================*/
static unsigned rc_wakes;
static int rc_probe_done;
#define RC_W1_ROW  9                        /* the counter's row (1-based) */

static void rc_slice(void)
{
    int c;
    /* the machine's "output" for this wave: typed keys, echoed */
    while ((c = rc_key_pop()) >= 0) {
        if (c == 13) {
            rc_putc(13);
            rc_putc(10);
        } else {
            rc_putc(c);
        }
    }
    /* the round-trip counter, on its own row, cursor put back after */
    if ((rc_wakes & 15) == 1) {
        rc_puts("\033[s\033[");
        rc_putdec(RC_W1_ROW);
        rc_puts(";1H\033[7m wave 1 \033[0m wake round trips: ");
        rc_putdec(rc_wakes);
        rc_puts("   \033[u");
    }
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

/* os88_onwake - THE slice (SPEC.md 71): on the UI task, lock NOT held. Run,
 * then flush under the lock for exactly one rc_flush, then re-post. */
void os88_onwake(void *win)
{
    rc_wakes++;
    rc_slice();
    if (!rc_probe_done) {                   /* RC_W1: the goto_q_mark proof,
                                             * once, from THIS context */
        int n = rc_fs_probe();
        rc_probe_done = 1;
        rc_puts("\033[s\033[8;1HA\\0 probe: ");
        if (n >= 0) {
            rc_putdec((unsigned)n);
            rc_puts(" bytes: ");
            rc_puts(rc_probe_buf);
        } else if (n == -1) {
            rc_puts("no A\\0 folder");
        } else if (n == -2) {
            rc_puts("in A\\0, no RCPROBE.TXT");
        } else {
            rc_puts("read, could not come home");
        }
        rc_puts("\033[K\033[u");
        rc_puts("\033[11;1H");             /* the echo starts below the two
                                             * scaffold rows */
    }
    if (rc_dirty_any) {
        os88_gfx_lock();
        if (os88_wm_clip_set(win) == 0)     /* something of us shows */
            rc_flush(win);
        os88_gfx_unlock();
    }
    rc_bell_service();
    /* RC_W1: the unconditional re-post is scaffolding for the counter. WAVE
     * 2's slice driver re-posts ONLY while the machine has work - the slice
     * ran out with the Z80 still running, or output is pending - and NOT
     * when the Z80 is blocked in CONIN on an empty key ring: then the next
     * kick is os88_onkey's (SPEC.md 71.1's handler contract). Carried
     * forward as it is, the UI task would spin at ~1,400 wakes a second on
     * the target while a program waits for a key. */
    if (rc_running)
        os88_wm_wake(win);                  /* the next slice; a full ring
                                             * is re-kicked by any callback */
}

/* os88_paint - W_PAINT, lock held. WF_OWNBG is set, so the kernel did not
 * whiten the content and os88_wm_damage() says which part needs drawing: the
 * rows (and cell columns) it covers are marked unknown in the shadow and
 * drawn by the same flush everything else uses; a whole repaint is one white
 * fill of the content and then every row to its last non-blank cell (rc_flush
 * does both). The two slivers outside the cell grid are ours to whiten. */
void os88_paint(void *win)
{
    static struct os88_rect d;
    static struct os88_pt org;
    static struct os88_size sz;
    int whole, r, c0, c1, r0, r1, x, y;

    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return;                             /* nothing to draw */
    if (os88_wm_geom(win, &sz) < 0)
        return;
    os88_wm_content(win, &org);

    if (whole || !rc_sh_ok) {
        rc_sh_inval();
    } else {
        /* the damaged cells are unknown; the rest of the shadow still holds */
        c0 = (d.x1 - org.x) >> 3;   if (c0 < 0) c0 = 0;
        c1 = (d.x2 - org.x) >> 3;   if (c1 > RC_COLS - 1) c1 = RC_COLS - 1;
        r0 = (d.y1 - org.y) >> 3;   if (r0 < 0) r0 = 0;
        r1 = (d.y2 - org.y) >> 3;   if (r1 > RC_ROWS - 1) r1 = RC_ROWS - 1;
        for (r = r0; r <= r1; r++) {
            if (c1 >= c0)
                os88_memset(rc_sh + RC_CHOFF(rc_shrow[r]) + c0, 0, c1 - c0 + 1);
            rc_dirty[r] = 1;
        }
        rc_dirty_any = 1;
    }
    /* the slivers: right of the last column, below the last row. On a WHOLE
     * repaint rc_flush's one fill of the entire content covers them (SPEC.md
     * 71.2), so nothing is filled here; on a partial one each is filled only
     * when the damage reaches it, and only the part of it the damage covers:
     * a partial expose over the cells alone costs no fill, and the clip
     * throws nothing away */
    if (!whole && rc_sh_ok) {
        os88_set_color(OS88_WHITE);
        x = org.x + ((sz.w >> 3 > RC_COLS ? RC_COLS : sz.w >> 3) << 3);
        y = org.y + ((sz.h >> 3 > RC_ROWS ? RC_ROWS : sz.h >> 3) << 3);
        if (x < org.x + sz.w && d.x2 >= x)
            os88_gfx_fill(d.x1 < x ? x : d.x1, d.y1, d.x2, d.y2);
        if (y < org.y + sz.h && d.y2 >= y)
            os88_gfx_fill(d.x1, d.y1 < y ? y : d.y1, d.x2, d.y2);
    }
    rc_flush(win);
    if (rc_running)
        os88_wm_wake(win);                  /* every callback kicks (71) */
}

/* os88_onkey - W_ONKEY, lock held. Keys go into the ring for the machine
 * (wave 3: BDOS CONIN, arrows as VT sequences, ^? and DEL); Alt+F is the
 * fullscreen chord in both directions (SPEC.md 71.2, the stated exception to
 * 11.2.1 - a terminal owns F and Esc). */
#define RC_SCAN_ALT_F 0x21
void os88_onkey(int ascii, int scan, void *win)
{
    if (ascii == 0 && scan == RC_SCAN_ALT_F) {
        /* OSAPI_FULLSCREEN paints SYNCHRONOUSLY inside the slot (kernel/
         * wm.inc wm_fullscreen: wm_raise whole on enter, wm_paint_all on
         * exit), so os88_paint has ALREADY run nested here, whole, for the
         * new geometry, and the shadow describes the new glass when this
         * returns. Nothing to invalidate - an rc_sh_inval() here would throw
         * that shadow away and the next wake would draw the identical
         * screen a second time (25 bands: the double-draw class CLAUDE.md
         * names as invisible in an emulator). A refused enter (-1) painted
         * nothing and needs nothing. */
        if (rc_full) {
            os88_fullscreen(win, 0);
            rc_full = 0;
        } else if (os88_fullscreen(win, 1) == 0) {
            rc_full = 1;
        }
        return;
    }
    if (ascii != 0)
        rc_key_push(ascii);
    if (rc_running)
        os88_wm_wake(win);
}

/* the empty menu set's handler: no menus, no item, never called (LESSONS 8) */
void os88_oncmd(int item, int menu, void *win)
{
    (void)item;
    (void)menu;
    (void)win;
}

void os88_about(void *win)
{
    rc_about(win);
}

/* os88_main - the entry (SPEC.md 20.2): claims first, then the window. The
 * launch requirement is CLAIMS, not KB: the 64KB Z80 RAM must be had (wave 2
 * adds the CCP claim), and a refusal quotes what was asked and what there
 * is (SPEC.md 71). */
static char rc_msg[64];
void *os88_main(void)
{
    void *win;

    rc_zseg = os88_mem_claim(64);
    if (rc_zseg == 0) {
        /* a toast is 24 characters (SPEC.md 59, TOAST_MAX) and menu.inc
         * truncates silently: "RunCPM: 64KB? nnnnn KB" is 14 + up to 5 + 3
         * = 22 at most, and quotes what was asked and what there is */
        os88_strcpy(rc_msg, "RunCPM: 64KB? ", sizeof(rc_msg));
        os88_utoa(os88_mem_largest_kb(), rc_num);
        os88_strcpy(rc_msg + os88_strlen(rc_msg), rc_num, 6);
        os88_strcpy(rc_msg + os88_strlen(rc_msg), " KB", 4);
        os88_toast(rc_msg, 0);
        return 0;
    }
    rc_term_init();
    rc_banner();
    rc_status = RC_ST_RUNNING;

    win = os88_wm_create(RC_WIN_X, RC_WIN_Y, RC_WIN_W, RC_WIN_H, rc_title);
    if (win == 0)
        return 0;
    rc_win = win;
    os88_wm_snap(win, 1);                   /* cell columns on byte boundaries:
                                             * blit1, scroll and font_run's
                                             * single-store path all want it */
    os88_wm_ownbg(win, 1);                  /* we paint every pixel, so a
                                             * repaint is not a white flash
                                             * followed by 25 bands */
    os88_menu_set(win, (struct os88_menuset *)&rc_kmenus);
    os88_about_set(win);
    os88_wm_onwake(win);                    /* the slice driver's entry (71.1) */
    rc_fs_init();                           /* bank the launch folder */
    rc_running = 1;                         /* the first paint kicks */
    return win;
}
