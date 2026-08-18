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
 * folders on the floppy with whole files in heap claims (rcfs.c, wave 4),
 * an 80x25 terminal drawn in a window (rcterm.c, wave 1), and DRI's CCP as
 * the command processor. The
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
 *   rccpm.c     the CP/M image constants, BIOS, BDOS, the line editor
 *   rcfs.c      the disk layer: (drive,user) -> folder, the directory cache,
 *               the open-file table over claims, every disk function
 *               (SPEC.md 71.3)
 *   rcabout.c   the About panel (SPEC.md 71.4)
 *
 * and the shim (runcpm.asm) %includes rcz80.inc, rcmem.inc and rcband.inc,
 * and icon.inc through CC_ICON (the 16x16 terminal icon, SPEC.md 71.5).
 *
 * WHAT WAVES 2 AND 3 ARE. The Z80 (rcz80.inc), the movers (rcmem.inc), the
 * slice driver in os88_onwake, the boot state machine - the banner, the
 * measured clock estimate, then main.c's loop: CCPHEAD, _PatchCPM, DRI's CCP
 * copied to 0xE400 out of the 2KB claim it was read into at launch,
 * AUTOEXEC.TXT poked into its command buffer, C = DSKByte, PC = CCPaddr, run;
 * a warm boot (BIOS WBOOT, BDOS 0, ^C at the prompt, a disk error) is that
 * lap again, and BIOS BOOT (EXIT.COM) or HALT is main.c's exit: "\r\n" and
 * the process ends, which here is the self-close (RC_M_EXIT and the worker)
 * - the console side of the BDOS and BIOS with the C_READSTR line editor as
 * a state machine (rccpm.c), the key mapping (arrows and the editing keys as
 * VT sequences, ^? = Ctrl+-, DEL = Ctrl+Backspace), and the debug loader -
 * Alt+L asks for a name and runs that .COM from A\0 (or the launch folder)
 * at 0100h, which is how ZEXDOC and the hello are run before wave 4's disk
 * layer lets the CCP load them itself (it stays: `make rczex` uses it).
 * Wave 1's scaffolding (the wake counter, the local echo, the goto_q_mark
 * probe) is gone: the loader standing in A\0 through rc_fs_cd() is the
 * living proof of the quiet goto, and the slice loop is the wake's client.
 *
 * THE SLICE (SPEC.md 71, 71.1). One wake runs rc_run() for rc_slice_n control
 * transfers (os88_cpu() sets the first guess, ~50 ms on the target; then it
 * adapts to the ticks - and ONLY a slice that spent its whole budget says
 * anything about the machine's speed: a slice that ended early, in a
 * console read on an empty ring, at a program's end or at a HALT, leaves the
 * estimate alone, so a typist's keys cannot walk the budget up to the cap
 * and hand the next busy slice seconds of UI task), servicing every RST
 * handoff in between; then flushes the terminal ONCE under the lock; then
 * re-posts ONLY while the machine has work - never while it is blocked in a
 * console read on an empty key ring, when os88_onkey's kick is the next one.
 * Flushing once per slice, not per line, is the pacing decision SPEC.md 71.2
 * asks for: a scrolled line is ~47 ms of glass on the target and N lines in
 * one slice are ONE gfx_scroll - so a keystroke is answered within one slice
 * PLUS one flush, and the flush is at most one scroll + one fill +
 * min(lines, RC_ROWS) bands. The clock estimate runs through the same
 * slices, and so does the debug loader's floppy read: NOTHING that takes
 * disk time or Z80 time runs under the gfx lock a W_ONKEY holds.
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
void  os88_onclick(int x, int y, void *win);
void  os88_worker(void *win);

static void rc_term_init(void);
static void rc_putc(int c);
static void rc_puts(const char *s);
static void rc_flush(void *win);
static void rc_sh_inval(void);
static void rc_bell_service(void);
static void rc_fs_init(void);
static int  rc_fs_cd(int d, int u);
static int  rc_fs_home(void);
static int  rc_fs_drive_exists(int d);
static void rc_fs_flush_all(void);
static unsigned ovl_fs_open(void);
static unsigned ovl_fs_close(void);
static unsigned ovl_fs_make(void);
static unsigned ovl_search_first(unsigned fcbaddr, int isdir);
static unsigned ovl_fs_snext(void);
static unsigned ovl_fs_delete(void);
static unsigned ovl_fs_rename(void);
static unsigned rc_fs_readseq(void);
static unsigned rc_fs_writeseq(void);
static unsigned rc_fs_readrand(void);
static unsigned rc_fs_writerand(void);
static unsigned ovl_fs_size(void);
static unsigned ovl_fs_randrec(void);
static void ovl_fs_setuser(int u);
static unsigned ovl_fs_makedisk(void);
static unsigned ovl_fs_checksub(void);
static void rc_fs_capture(int lst, int c);
static int  ovl_about_show(void *win);       /* module code: 0 = not loaded */
static void rc_about_close(void *win);
static int  rc_about_hit_ok(int x, int y);
static void rc_blank_rect(void *win, int x1, int y1, int x2, int y2);
static int  rc_wants_wake(void);
static int  rc_key_pop(void);
static int  ovl_patch_cpm(void);            /* module code: 0 = not loaded */
static int rc_full;                          /* fullscreen latch held (Alt+F):
                                              * rcfs.c's rc_say reads it - a
                                              * toast is the bar's, and the
                                              * bar is under a fullscreen
                                              * window (SPEC.md 71.4) */
static void *rc_win;                         /* our window, for rc_say_now */
static int rc_abt;                           /* the About panel is up
                                              * (rcabout.c): the machine is
                                              * PAUSED - no slice, no wake,
                                              * no flush - and every key and
                                              * click is the panel's until it
                                              * comes down (SPEC.md 71.4) */
static void rc_con_reset(void);
static int  rc_bios(void);
static int  rc_bdos(void);
static void rc_boot(void);

/* the hand-written half (SPEC.md 70.11): rcz80.inc and rcmem.inc in the
 * shim. Near cdecl; the register file is rc_z (rccpm.c). The host harness
 * models them in C. */
int  rc_run(int n);
int  rc_rd(unsigned a);
int  rc_rd16(unsigned a);
void rc_wr(unsigned a, int v);
void rc_wr16(unsigned a, int v);
void rc_zcopy_in(unsigned zaddr, const void *src, unsigned n);
void rc_zcopy_out(void *dst, unsigned zaddr, unsigned n);
void rc_zzcopy_in(unsigned zaddr, unsigned seg, unsigned off, unsigned n);
void rc_zzcopy_out(unsigned seg, unsigned off, unsigned zaddr, unsigned n);
void rc_zfill(unsigned zaddr, int v, unsigned n);
void rc_sfill(unsigned seg, unsigned off, int v, unsigned n);   /* another
                                              * claim: a file's gap (wave 4) */

/* the key ring: os88_onkey pushes, the BDOS/BIOS console reads pop
 * (rccpm.c); the debug loader's prompt reads it too */
#define RC_KRING 64
static unsigned char rc_kbuf[RC_KRING];
static int rc_khead, rc_ktail;

static int rc_bells;                        /* BELs since the last flush,
                                             * one tone each (rcterm.c's
                                             * parser counts them; so does
                                             * the ring, below) */
static int rc_mask = 0x7F;                  /* console.h mask8bit: 0x7F masks
                                             * the 8th bit of every byte to
                                             * the console (the standard CP/M
                                             * behaviour), 0xFF passes it -
                                             * BDOS 230 (CONSOLE7.COM /
                                             * CONSOLE8.COM, rccpm.c) sets it;
                                             * rcterm.c's rc_putc applies it */
/* the tick count, 32 bits: the kernel's os88_ticks() is 16 bits and laps
 * every 65,536 ticks (~60 min); rc_ticks32() samples it and bumps the high
 * word when it is seen to go backwards - sampled on every slice (os88_onwake)
 * and on every F_UPTIME (rccpm.c, BDOS 248: upstream's millis() is 32-bit),
 * so a machine that has been running for a day still answers a count that
 * only grows. An IDLE machine (no wake for over an hour) sees one lap as
 * one, which is the most any sampling scheme can promise. */
static unsigned rc_tk_hi, rc_tk_last;
static unsigned rc_ticks32(void)
{
    unsigned t = os88_ticks();
    if (t < rc_tk_last)
        rc_tk_hi++;
    rc_tk_last = t;
    return t;
}

static int rc_key_push(int c)
{
    int n = (rc_ktail + 1) & (RC_KRING - 1);
    if (n == rc_khead) {
        /* full: input overrun. Never silent - one of the three defects
         * CLAUDE.md names as invisible in an emulator - so it does what the
         * PC BIOS does with a full keyboard buffer: rings. Zero glass cost,
         * one tone from rc_bell_service outside the lock. */
        rc_bells++;
        return -1;
    }
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

#include "rcterm.c"
#include "rccpm.c"
#include "rcfs.c"
#include "rcabout.c"

/* ==========================================================================
 * THE PROGRAM
 * ========================================================================*/

/* The window: authored for the 640x480 reference screen (SPEC.md 39) and
 * clamped onto the live one - 80 cells + the two border columns wide, 18
 * rows of title and 200 of content = 25 cell rows, at (7,20): under the menu
 * bar, one cell in. On CGA the band is shorter and the window shows what it
 * holds (SPEC.md 71.2). THE +1: a framed window's content is W_H - TITLE_H
 * - 1 rows (kernel/wm.inc wm_geom: the bottom border is inside W_H), so
 * 18 + 200 showed 24 rows and a 7-px sliver, and the model's row 24 - the
 * row a scrolled cursor lives on - was never on the glass. Found on the
 * glass, not in the harness, whose fake window was 200 rows of content by
 * construction. THE +2 (wave 5): wm_fit clamps the width to the screen's, so
 * on a 640-px screen the window is 640 wide, flush and borderless (SPEC.md
 * 11.95.2) with 639 px = 79 cells of content, exactly as before; on
 * Hercules' 720 px it stays 642 wide at x = 7 with its border, and its 640
 * px of content hold all 80 columns FRAMED - the plan's claim for that
 * adapter, which the first four waves' 640-wide window did not deliver
 * (seen on the Hercules glass in wave 5: 'Len:4' for 'Len:47'). */
#define RC_WIN_X   7
#define RC_WIN_Y   20
#define RC_WIN_W   (RC_COLS * 8 + 2)
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

/* THE MACHINE'S STATE - what a wake does, and whether the next one is
 * wanted (SPEC.md 71.1: re-post only while there is work) */
#define RC_M_CLOCK   0                       /* measuring the clock estimate */
#define RC_M_IDLE    1                       /* nothing runs: no CCP could be
                                              * loaded, or the loader refused
                                              * - Alt+L is the way back in */
#define RC_M_RUN     2                       /* a program runs in slices */
#define RC_M_BLOCKED 3                       /* ...and waits for a key */
#define RC_M_EXIT    4                       /* main.c's exit: the worker is
                                              * hired on the next wake and
                                              * closes the window (retried
                                              * while the task table is full) */
#define RC_M_DEAD    5                       /* the worker is closing us */
static int rc_mode = RC_M_CLOCK;
static unsigned rc_ccpseg;                   /* the 2KB CCP claim (SPEC.md
                                              * 71.3): CCP-DR.60K, read ONCE
                                              * at launch from the launch
                                              * folder, before any folder
                                              * move, and copied to 0xE400 on
                                              * every warm boot */
static unsigned rc_ccplen;                   /* bytes read: 0 = no CCP */
static unsigned char rc_axbuf[126];          /* AUTOEXEC.TXT's first 125 bytes
                                              * (main.c 128), cut at the
                                              * first control character and
                                              * NUL-ended: read ONCE at
                                              * launch (os88_main), poked on
                                              * every boot (rc_boot) */
static unsigned rc_axlen;                    /* ...its length: 0 = none */
static int rc_slice_n;                       /* control transfers a slice */
static unsigned rc_sl_fast;                  /* slices in a row that took no
                                              * tick: the length adapts */

/* --- console helpers, console.h's shape: _puthex16 is lowercase (tohex) --- */
static char rc_num[8];
static char rc_msg[64];                      /* a toast being built (the
                                              * launch refusals, Alt+C) */
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
    /* Z80estimateClock() prints its two lines here - measured in slices
     * (rc_clock_step), so the rest of the banner follows from there */
}

static int ovl_banner2(void)                 /* module code (SPEC.md 70.14):
                                              * once a launch; 0 = the module
                                              * refused - rc_boot says so */
{
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
    return 1;
}

/* ==========================================================================
 * THE CLOCK ESTIMATE - cpu_mhz.h Z80estimateClock(), in slices and 16-bit
 * words. Upstream loads a 17-byte loop at 0000 (LD DE,1000 / LD BC,10000 /
 * the inner DEC BC-LD A,B-OR C-JR NZ / the outer DEC DE-LD A,D-OR E-JR NZ /
 * HALT), times it with millis() and prints '<T-states> T-states in <ms> ms'
 * and 'Estimated Z80 clock speed: <n> MHz' from a 64-bit sum. Two stated
 * deviations (SPEC.md 71): the burst is SMALLER (BC = 1000, DE = 10: 260,319
 * T-states by cpu_mhz.h's own T-state table, against 260 million - the
 * upstream burst is 40 million instructions, minutes on the target) and is
 * RUN REPEATEDLY until at least RC_CLK_TICKS ticks have passed, so the same
 * code measures a 4.77 MHz 8088 and QEMU; the T-states are bursts x 260,319
 * in two words, the ms are ticks x 55, and the speed is printed with two
 * decimals ('%u MHz' would print 0 on the XT). It runs on the wake in the
 * ordinary slices (rc_clock_step), so the window is live while it measures.
 * ========================================================================*/
#define RC_CLK_TICKS 4                       /* ~220 ms of slices at least,
                                              * and one whole burst */
static const unsigned char rc_clkcode[17] = {
    0x11, 0x0A, 0x00,                        /* LD DE,10   (upstream 1000) */
    0x01, 0xE8, 0x03,                        /* LD BC,1000 (upstream 10000) */
    0x0B, 0x78, 0xB1, 0x20, 0xFB,            /* DEC BC / LD A,B / OR C / JR NZ */
    0x1B, 0x7A, 0xB3, 0x20, 0xF3,            /* DEC DE / LD A,D / OR E / JR NZ */
    0x76                                     /* HALT */
};
/* T-states of one burst, cpu_mhz.h's arithmetic with DE = 10, BC = 1000:
 * inner body 26, exit 21; total_inner = 10 + 999*26 + 21 = 26,005; outer
 * body 26,031, exit 26,026; total = 10 + 9*26,031 + 26,026 + 4 = 260,319 */
#define RC_CLK_TS_HI 0x0003
#define RC_CLK_TS_LO 0xF8DF                  /* 260,319 = 0x0003F8DF */
static unsigned rc_clk_t0, rc_clk_bursts;
static int rc_clk_started;                   /* t0 taken: the first slice ran */

/* --- two words as one number: the few operations the estimate needs. The
 * masks are no-ops on the target (unsigned IS 16 bits) and are what keeps
 * the host harness, whose unsigned is 32 bits, computing the same thing
 * (LESSONS.md 7) --------------------------------------------------------- */
static unsigned rc_u32_hi, rc_u32_lo;        /* the accumulator */
static void rc_u32_add(unsigned hi, unsigned lo)
{
    unsigned l = (rc_u32_lo + lo) & 0xFFFF;
    if (l < rc_u32_lo)
        rc_u32_hi++;
    rc_u32_lo = l;
    rc_u32_hi = (rc_u32_hi + hi) & 0xFFFF;
}
/* the accumulator >= (hi,lo)? */
static int rc_u32_ge(unsigned hi, unsigned lo)
{
    if (rc_u32_hi != hi)
        return rc_u32_hi > hi;
    return rc_u32_lo >= lo;
}
static void rc_u32_sub(unsigned hi, unsigned lo)
{
    if (rc_u32_lo < lo)
        rc_u32_hi--;
    rc_u32_lo = (rc_u32_lo - lo) & 0xFFFF;
    rc_u32_hi = (rc_u32_hi - hi) & 0xFFFF;
}
/* the accumulator, in decimal, through the terminal: powers of ten as word
 * pairs, each subtracted while it fits */
static const unsigned rc_pow10_hi[10] = {
    0x3B9A, 0x05F5, 0x0098, 0x000F, 0x0001, 0, 0, 0, 0, 0 };
static const unsigned rc_pow10_lo[10] = {
    0xCA00, 0xE100, 0x9680, 0x4240, 0x86A0, 10000, 1000, 100, 10, 1 };
static void rc_putdec32(void)
{
    int i, d, started = 0;
    for (i = 0; i < 10; i++) {
        d = 0;
        while (rc_u32_ge(rc_pow10_hi[i], rc_pow10_lo[i])) {
            rc_u32_sub(rc_pow10_hi[i], rc_pow10_lo[i]);
            d++;
        }
        if (d || started || i == 9) {
            rc_putc('0' + d);
            started = 1;
        }
    }
}

static void rc_clock_begin(void)
{
    rc_zcopy_in(0x0000, rc_clkcode, 17);
    rc_z.pc = 0;
    rc_z.sp = 0xE3FE;
    rc_z.iff = 0;
    rc_clk_bursts = 0;
    rc_clk_started = 0;
    rc_mode = RC_M_CLOCK;
}

/* one SLICE of the clock program a wake - the ordinary slice budget, never
 * a whole burst: a burst is ~11,000 control transfers, ~20 slices (~1 s)
 * on the target, and a wake that ran it whole would hold the UI task for
 * that second with no key, click or paint dispatched (the stall the slices
 * exist to prevent). A slice that runs out mid-burst leaves PC where the
 * Z80 stopped and the next wake RESUMES it; PC goes back to 0000 only after
 * a HALT, and only completed bursts count. t0 is the first slice's start,
 * so the ms include the wake round trips - the estimate is what the machine
 * actually delivers here. On the target the first HALT comes after the four
 * ticks and the estimate is one burst; under QEMU a burst is a slice or
 * less and dozens complete. When the ticks are in: the two lines, the rest
 * of the banner, and the machine goes idle (wave 3: loads the CCP). Answers
 * 1 when the slice spent its whole budget (the adaptation may read its
 * timing), 0 when a HALT ended it early. */
static int rc_clock_step(void)
{
    unsigned dt;
    int r;
    if (!rc_clk_started) {
        rc_clk_started = 1;
        rc_clk_t0 = os88_ticks();
    }
    r = rc_run(rc_slice_n);
    if (r != RC_RUN_HALT)
        return 1;                            /* mid-burst: resumed next wake */
    rc_clk_bursts++;
    rc_z.pc = 0;                             /* the next burst starts over */
    dt = os88_ticks() - rc_clk_t0;
    if (dt < RC_CLK_TICKS)
        return 0;
    /* T-states = bursts x 260,319 */
    rc_u32_hi = rc_u32_lo = 0;
    while (rc_clk_bursts-- > 0)
        rc_u32_add(RC_CLK_TS_HI, RC_CLK_TS_LO);
    {
        unsigned ts_hi = rc_u32_hi, ts_lo = rc_u32_lo;
        unsigned ms = dt * 55;               /* 18.2 Hz: 54.9 ms a tick */
        unsigned q, cnt;
        rc_putdec32();
        rc_puts(" T-states in ");
        rc_putdec(ms);
        rc_puts(" ms\r\n");
        /* hundredths of MHz = T-states / (ms * 10): counted by subtraction
         * of ms*10 - at most a few thousand rounds on any machine that runs
         * this (QEMU is ~2,000; the target ~10) */
        rc_u32_hi = ts_hi;
        rc_u32_lo = ts_lo;
        {
            unsigned d = ms * 10;
            q = 0;
            cnt = 0;
            while (rc_u32_ge(0, d) && ++cnt < 30000) {
                rc_u32_sub(0, d);
                q++;
            }
        }
        rc_puts("Estimated Z80 clock speed: ");
        rc_putdec(q / 100);
        rc_putc('.');
        rc_putc('0' + (q % 100) / 10);
        rc_putc('0' + q % 10);
        rc_puts(" MHz\r\n");
    }
    ovl_banner2();
    rc_boot();                               /* main.c 110: the loop's first lap */
    return 0;
}

/* ==========================================================================
 * THE COMMAND PROCESSOR: main.c's `while (TRUE)` loop, 110-140, one lap per
 * boot. CCPHEAD; _PatchCPM; Status = 0; the CCP copied into place (main.c
 * _RamLoad(CCPname, CCPaddr) - here out of the claim it was read into at
 * launch, so a warm boot costs no disk); AUTOEXEC.TXT POKED on EVERY boot
 * (BOOTONLY is FALSE upstream, globals.h 305) - its first 125 bytes at
 * CCPaddr+8, cut at the first control character, a NUL after it and its
 * length at CCPaddr+7, which is where the DRI CCP keeps its command buffer,
 * so the CCP runs it before showing a prompt; Z80reset (PC, IFF, I, R
 * cleared, cpu1.h 1032); C = DSKByte; PC = CCPaddr; run. THE FILE IS READ
 * ONCE, at launch (os88_main, rc_ax_load), and cached: upstream re-reads it
 * from FILEBASE on every lap, which is a host fopen there and a directory
 * walk of the launch folder here (one int 13h per directory sector, ~400 ms
 * each on the target, and on every shipped disk the file is ABSENT so the
 * whole root is walked to say so) on every ^C, RET and WBOOT - and it buys
 * nothing: the launch folder is outside CP/M's A\0..P\F, so no CP/M program
 * can change AUTOEXEC.TXT during the session, and 'read on every boot' and
 * 'read once, poke every boot' are the same observable behaviour here. A
 * warm boot is therefore a 2KB claim-to-claim copy and no disk at all, which
 * is what lets rc_slice run the CCP's prompt in the same slice.
 * 'Unable to load CP/M CCP.' is main.c 118's text for a missing CCP; upstream
 * breaks out of the loop and exits - here the machine goes idle instead
 * (SPEC.md 71: the window stays so the message can be read; the close box
 * ends it, and Alt+L still loads a .COM).
 * ========================================================================*/
static void rc_boot(void)
{
    rc_puts(RC_CCPHEAD);
    if (!ovl_patch_cpm()) {                  /* cold: IOBYTE and DSKByte too.
                                              * THE MODULE'S GATE (SPEC.md
                                              * 71): the disk layer's per-
                                              * command half is RUNCPM.OVL,
                                              * loaded by this first call
                                              * from the launch folder; when
                                              * it cannot be, no CP/M can run
                                              * - the fact on the terminal
                                              * (the reason is cc_ovneed's
                                              * toast), the window kept up
                                              * to show it, as for the CCP */
        rc_puts("Unable to load RUNCPM.OVL.\r\nCPU halted.\r\n");
        rc_mode = RC_M_IDLE;
        return;
    }
    rc_status = RC_ST_RUNNING;
    rc_con_reset();                          /* no line half-edited, no error
                                              * waiting: a warm boot */
    if (rc_ccplen == 0) {
        rc_puts("Unable to load CP/M CCP.\r\nCPU halted.\r\n");
        rc_mode = RC_M_IDLE;
        return;
    }
    rc_zzcopy_in(RC_CCPADDR, rc_ccpseg, 0, rc_ccplen);
    if (rc_axlen) {                          /* AUTOEXEC.TXT, from the cache */
        rc_zcopy_in(RC_CCPADDR + 8, rc_axbuf, rc_axlen + 1);
        rc_wr(RC_CCPADDR + 7, rc_axlen);
    }
    rc_z.pc = 0;                             /* Z80reset() */
    rc_z.iff = 0;
    rc_z.i = rc_z.r = 0;
    rc_z.bc = (rc_z.bc & 0xFF00) | rc_rd(RC_DSKBYTE);   /* C = drive/user */
    rc_z.pc = RC_CCPADDR;
    rc_mode = RC_M_RUN;
}

/* ==========================================================================
 * THE SLICE DRIVER
 * ========================================================================*/

/* rc_program_end - the machine came back to us: EXIT (BIOS BOOT / EXIT.COM,
 * and HALT - cpu1.h 0x76 sets Status = STATUS_EXIT exactly as BOOT does),
 * RESTART (WBOOT, BDOS 0, ^C at the prompt, a disk error) or RETURN (USERF).
 * main.c 141-160: EXIT leaves the loop, prints "\r\n" and the process ends -
 * here the window closes (RC_M_EXIT: the worker is hired on the wake);
 * anything else is the loop's next lap, CCPHEAD and the CCP again. */
static void rc_program_end(void)
{
    rc_fs_flush_all();                       /* the program's open files
                                              * written back and released
                                              * (SPEC.md 71.3), before the
                                              * loop laps or the window goes
                                              * - the file slots are legal:
                                              * this is the wake, no lock */
    if (rc_status == RC_ST_EXIT) {
        rc_puts("\r\n");
        rc_mode = RC_M_EXIT;
        return;
    }
    rc_boot();
}

/* rc_slice - one wake's worth of Z80: run until the slice budget is spent,
 * servicing every handoff on the way; a service that cannot complete (a
 * console read on an empty ring) leaves PC on the RST and blocks the
 * machine until a key comes. Answers 1 when the budget was EXHAUSTED - the
 * only kind of slice whose duration measures the machine - and 0 when the
 * slice ended early (blocked, program end, HALT). */
static int rc_slice(void)
{
    int n = rc_slice_n, r;
    while (n > 0) {
        r = rc_run(n);
        n = rc_z.cnt;
        if (r == RC_RUN_SLICE)
            return 1;
        if (r == RC_RUN_HALT) {
            /* cpu1.h 0x76: Status = STATUS_EXIT, exactly as BIOS BOOT - so
             * main.c's exit path follows (the "\r\n", wave 3's self-close);
             * '::CPU HALTED::' is DEBUG-only upstream, and the one stated
             * deviation is that the fact is toasted (SPEC.md 71.4) */
            os88_toast("RunCPM: CPU halted", 0);
            rc_status = RC_ST_EXIT;
            rc_program_end();
            return 0;
        }
        if (r == RC_RUN_BDOS ? rc_bdos() : rc_bios())
            continue;
        /* the machine stopped: why? */
        if (rc_status == RC_ST_RUNNING) {
            rc_mode = RC_M_BLOCKED;          /* a console read waits for a key:
                                              * os88_onkey kicks (71.1) */
            return 0;
        }
        rc_program_end();
        if (rc_mode == RC_M_RUN)
            continue;                        /* a WARM BOOT: no disk (rc_boot
                                              * pokes from caches), so the
                                              * CCP's prompt runs in what is
                                              * left of THIS slice and
                                              * CCPHEAD and 'A>' are ONE
                                              * flush - one gfx_scroll, not
                                              * two, when ^C lands at the
                                              * bottom of the screen */
        return 0;                            /* EXIT or IDLE */
    }
    return 1;
}

/* ==========================================================================
 * THE DEBUG LOADER (docs/RUNCPM-PORT-PLAN.md wave 2): Alt+L asks for a name
 * on the terminal, reads NAME.COM from the floppy into the Z80 claim and
 * runs it at 0100h the way the CCP would: page zero patched, C = the
 * disk/user byte, SP under the CCP with 0000 on top so a RET warm-boots. It
 * looks in A\0 (the CP/M disk, through the same rc_fs_cd() the drive layer
 * will use) and then in the launch folder. Always compiled: it is how ZEXDOC
 * is run by tests/rczex.py, and it costs one prompt string.
 *
 * THE READ LANDS 512-ALIGNED AND IS COPIED DOWN. Every disk-visible base is
 * 512-byte aligned (SPEC.md 2.1.1): int 13h moves whole sectors and
 * kernel/disk.inc's dsk_runcap can split a run at a 64KB physical page only
 * at sector granularity, so a transfer that STARTS 256 mod 512 - a read
 * straight to 0100h in a claim - has one sector straddling the page whenever
 * the claim's internal page boundary falls inside the file, and the DMA
 * controller answers that with error 09h on real hardware ("Disk error" on
 * ~13% of ZEXDOC launches, ~38% of MBASIC's, by where a 64KB claim lands).
 * QEMU's BIOS ignores the page and never shows it. So the read goes to the
 * first 512-aligned offset at or above 0200h (a claim's base is KB-aligned
 * by the kernel's own guard, kernel.asm 6b; the offset is computed from the
 * segment anyway, so nothing here leans on it) and rc_zzcopy_in moves it to
 * 0100h - destination below source, ascending rep movsb, overlap-safe.
 * Nothing else in this program reads a file into the Z80 claim off 0000h;
 * wave 4's whole-file claims read at offset 0. The rule is stated at
 * os88_file_read_seg in os88.h and in SPEC.md 71.
 * ========================================================================*/
#define RC_TPA_TOP   RC_CCPADDR              /* the TPA ends where the CCP
                                              * begins: 0100h..E3FFh */
static char rc_ldname[16];
static int  rc_ldlen;
static int  rc_ldprev;                       /* the mode the prompt
                                              * interrupted (a CCP blocked
                                              * at its prompt, or idle): Esc,
                                              * an empty name and a refused
                                              * load go back to it */
#define RC_LD_PROMPT 1                       /* the prompt is up */
#define RC_LD_LOAD   2                       /* Enter: the read is on the next
                                              * wake, where the lock is NOT
                                              * held (SPEC.md 71.1) - a .COM
                                              * is several int 13h calls,
                                              * ~400 ms each on the target */
static int  rc_ldmode;

/* rc_ld_back - the loader hands the machine back to the state it interrupted
 * (a refused load, Esc, an empty name). If that state was BLOCKED (the CCP's
 * trap waiting for a key) and a key arrived MEANWHILE - typed between Enter
 * and the wake that read the floppy - the trap must be retried now: os88_onkey
 * does not flip BLOCKED->RUN while the loader is active (the key would only
 * have been pushed), so nothing else would, and the CCP would sit on a
 * non-empty ring until the NEXT keystroke. */
static void rc_ld_back(void)
{
    rc_mode = (rc_ldprev == RC_M_BLOCKED && rc_khead != rc_ktail)
              ? RC_M_RUN : rc_ldprev;
}

static void rc_run_program(void)
{
    rc_status = RC_ST_RUNNING;
    ovl_patch_cpm();
    rc_con_reset();                          /* the CCP's half-typed line, if
                                              * the loader interrupted one,
                                              * is not the program's */
    rc_z.af = 0;
    rc_z.bc = rc_rd(RC_DSKBYTE);             /* C = the drive/user (main.c) */
    rc_z.de = 0;
    rc_z.hl = 0;
    rc_z.ix = rc_z.iy = 0;
    rc_z.iff = 0;
    rc_z.sp = RC_TPA_TOP - 2;
    rc_wr16(RC_TPA_TOP - 2, 0x0000);         /* a RET warm-boots */
    rc_z.pc = 0x0100;
    rc_wr(0x0080, 0);                        /* an empty command tail */
    rc_dma = 0x0080;
    rc_mode = RC_M_RUN;
}

/* the first 512-aligned Z80-claim offset at or above 0200h: 0200h plus the
 * bytes from the claim's physical base up to the next 512 boundary */
static unsigned rc_zoff512(void)
{
    return 0x200 + ((0x200 - ((rc_zseg << 4) & 0x1FF)) & 0x1FF);
}

/* rc_ax_load - AUTOEXEC.TXT, ONCE, at launch, from the launch folder (main.c
 * _RamLoad(AUTOEXEC, cmd, 125): the first 125 bytes of a file of ANY size).
 * OSAPI_FILE_READ answers FERR_BIG and reads NOTHING when the file is larger
 * than the buffer (decided from the directory entry, os88api.inc 0x0128), so
 * a 125-byte read would silently ignore any AUTOEXEC.TXT over 125 bytes; the
 * file is read WHOLE instead, into the one place that is empty at launch,
 * 512-aligned and rewritten by everything that follows - the Z80 claim's
 * TPA, at the loader's own landing (rc_zoff512) with the TPA up to the CCP
 * as the capacity (~57KB: a file over that is FERR_BIG and ignored, stated
 * in SPEC.md 71) - and the first 125 bytes come out into rc_axbuf, cut at
 * the first control character. Called before the clock code, before any
 * folder move; the bytes left in the TPA are cleared. */
static void rc_ax_load(void)
{
    unsigned n, off = rc_zoff512(), blen;
    n = os88_file_read_seg("AUTOEXEC.TXT", rc_zseg + (off >> 4), RC_CCPADDR - off);
    rc_axlen = 0;
    if (n == 0)
        return;
    blen = n > 125 ? 125 : n;
    rc_zcopy_out(rc_axbuf, off, blen);
    rc_zfill(off, 0, n);
    n = blen;
    blen = 0;
    while (blen < n && rc_axbuf[blen] > 31)
        blen++;
    rc_axbuf[blen] = 0;
    rc_axlen = blen;
}

static int ovl_load_go(void)                 /* module code: 0 = the module
                                              * refused, nothing loaded */
{
    unsigned n, off, big;
    off = rc_zoff512();
    os88_strcpy(rc_ldname + rc_ldlen, ".COM", 5);
    n = 0;
    big = 0;
    if (rc_fs_cd(0, 0) == 0) {               /* A\0 first: the CP/M disk */
        n = os88_file_read_seg(rc_ldname, rc_zseg + (off >> 4), RC_TPA_TOP - off);
        big = (n == 0 && os88_ferr() == OS88_FERR_BIG);
    }
    if (n == 0 && !big) {                    /* then the launch folder - but a
                                              * file that EXISTS in A\0 and is
                                              * too big is not looked for
                                              * again: the fallback's NOENT
                                              * would hide the true fact */
        rc_fs_home();
        n = os88_file_read_seg(rc_ldname, rc_zseg + (off >> 4), RC_TPA_TOP - off);
        big = (n == 0 && os88_ferr() == OS88_FERR_BIG);
    }
    if (n == 0) {
        rc_puts(rc_ldname);
        rc_puts(big ? ": too big for the TPA\r\n" : ": not found\r\n");
        rc_ld_back();                        /* back to the prompt, or idle */
        return 1;
    }
    rc_zzcopy_in(0x0100, rc_zseg, off, n);   /* down to the TPA: dst < src,
                                              * ascending, overlap-safe */
    rc_run_program();
    return 1;
}

/* the prompt's keys: printable into the name (upper-cased, 8 at most), BS,
 * Enter echoes the CR/LF and hands the read to the wake, Esc cancels */
static void ovl_load_key(int c)
{
    if (c == 13) {
        rc_putc(13);
        rc_putc(10);
        rc_ldmode = 0;
        if (rc_ldlen == 0) {
            rc_ld_back();
            return;
        }
        rc_ldmode = RC_LD_LOAD;              /* os88_onwake: ovl_load_go() */
    } else if (c == 27) {
        rc_puts("\r\n");
        rc_ldmode = 0;
        rc_ld_back();
    } else if (c == 8 || c == 127) {
        if (rc_ldlen > 0) {
            rc_ldlen--;
            rc_puts("\010 \010");
        }
    } else if (c > 32 && c < 127 && rc_ldlen < 8) {
        if (c >= 'a' && c <= 'z')
            c -= 32;
        rc_ldname[rc_ldlen++] = (char)c;
        rc_putc(c);
    }
}

static void rc_load_prompt(void)
{
    rc_ldlen = 0;
    rc_ldmode = RC_LD_PROMPT;
    rc_ldprev = rc_mode;
    rc_puts("\r\nLoad .COM: ");
}

/* does the machine want the next wake? (SPEC.md 71.1: only while it works -
 * a slice, a clock slice, the loader's pending read, or an exit whose
 * worker is not hired yet) */
static int rc_wants_wake(void)
{
    if (rc_abt)
        return 0;                            /* paused under the About panel */
    return rc_mode == RC_M_CLOCK || rc_mode == RC_M_RUN ||
           rc_mode == RC_M_EXIT || rc_ldmode == RC_LD_LOAD;
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

/* os88_onwake - THE slice (SPEC.md 71): on the UI task, lock NOT held. Do
 * the loader's pending read, or run one slice (of the program or of the
 * clock estimate); flush the terminal ONCE under the lock; ring what rang;
 * re-post only while the machine has work. */
void os88_onwake(void *win)
{
    if (rc_abt)
        return;                              /* the About panel is up: the
                                              * machine is paused, the glass
                                              * is the panel's; its close
                                              * re-kicks (rcabout.c) */
    if (rc_ldmode == RC_LD_LOAD) {
        rc_ldmode = 0;
        if (!ovl_load_go())                  /* the floppy, with no lock held */
            rc_ld_back();                    /* the module refused: as typed
                                              * nothing (the toast says why) */
    } else if (rc_mode == RC_M_CLOCK || rc_mode == RC_M_RUN) {
        /* THE SLICE LENGTH ADAPTS to the machine it runs on: os88_cpu() set
         * the first guess, and from there a slice that ends inside the tick
         * it began in four times running doubles the budget (to a cap), and
         * one that spans two tick boundaries halves it (to a floor). That
         * holds a slice between ~14 and ~55 ms on the 8088, the 386 and
         * QEMU alike, and the wake round trip (a task switch, the flush) is
         * paid a dozen times a second rather than a thousand. ONLY A SLICE
         * THAT EXHAUSTED ITS BUDGET IS TIMED: one that ended early - a
         * program blocking in C_READ after a few hundred instructions (each
         * key into TE or MBASIC is one such wake), a program end, a HALT -
         * took no time because the machine did no work, and counting it as
         * "fast" would double the budget every four keystrokes: ~30 keys of
         * ordinary typing walked it from 512 to the 16,384 cap, and the
         * next busy slice (Enter into a compute loop) then ran ~1.6 s of UI
         * task with nothing dispatched - found by the review, and the harness
         * now asserts both halves. The bound on a keystroke's echo is honest:
         * one slice PLUS one flush, and the flush is unbounded by this loop
         * - at most one gfx_scroll + one fill + min(lines, RC_ROWS) bands
         * (SPEC.md 71.1: ~50 + up to ~400 ms on the target under a program
         * that scrolls a screenful a slice). The clock's slices are exhausted
         * ones and adapt the same way, so the first program slice starts
         * where the estimate left the budget. */
        unsigned t = rc_ticks32(), dt;   /* ...and the 32-bit count sampled */
        int ex;
        if (rc_mode == RC_M_CLOCK)
            ex = rc_clock_step();
        else
            ex = rc_slice();
        dt = os88_ticks() - t;
        if (!ex) {
            /* an early end says nothing about the machine's speed */
        } else if (dt == 0) {
            if (++rc_sl_fast >= 4) {
                rc_sl_fast = 0;
                if (rc_slice_n < 0x4000)
                    rc_slice_n <<= 1;
            }
        } else {
            rc_sl_fast = 0;
            if (dt >= 2 && rc_slice_n > 256)
                rc_slice_n >>= 1;
        }
    }
    if (rc_dirty_any && rc_mode != RC_M_EXIT && rc_mode != RC_M_DEAD) {
        /* NOT when the machine is leaving: main.c's exit "\r\n" is for a
         * host shell that outlives the process, and here nothing outlives
         * the window - the worker destroys it within a tick, so a flush
         * now (a gfx_scroll of the content when the cursor is on the last
         * row, ~30-50 ms on the target) is a draw nobody can see, the
         * double-draw class. The model has the line; the glass never
         * needs it. */
        os88_gfx_lock();
        if (os88_wm_clip_set(win) == 0)     /* something of us shows */
            rc_flush(win);
        os88_gfx_unlock();
    }
    rc_bell_service();
    if (rc_mode == RC_M_EXIT) {
        /* main.c's exit, AFTER the "\r\n" is on the glass: hire the worker
         * that closes the window (there is no self-close slot - cword's
         * File > Close idiom, os88_worker below). os88_task_spawn wants the
         * gfx lock held (SPEC.md 20.6); a refusal - the 12-slot task table
         * full - is transient, and RC_M_EXIT keeps wanting the wake until
         * it takes. */
        os88_gfx_lock();
        if (os88_task_spawn(win) == 0)
            rc_mode = RC_M_DEAD;
        os88_gfx_unlock();
    }
    if (rc_wants_wake())
        os88_wm_wake(win);                  /* the next slice; a full ring
                                             * is re-kicked by any callback */
}

/* os88_worker - hired ONLY to close the window (SPEC.md 71: EXIT.COM / BIOS
 * BOOT / HALT), so it costs nothing until then. cword's idiom: the destroy
 * needs the lock and os88_task_alive() forbids it, so they cannot share a
 * bracket; the destroy is what makes task_alive() die - the kernel's own
 * teardown, which frees the task, the instance, the region and every claim
 * (the Z80's 64KB and the CCP's 2KB). It never returns. */
void os88_worker(void *win)
{
    for (;;) {
        if (rc_mode == RC_M_DEAD) {
            os88_gfx_lock();
            os88_wm_destroy(win);
            os88_gfx_unlock();
        }
        os88_task_alive(win);               /* never returns once destroyed */
        os88_task_sleep(4);
    }
}

/* os88_paint - W_PAINT, lock held. WF_OWNBG is set, so the kernel did not
 * whiten the content and os88_wm_damage() says which part needs drawing: a
 * partial expose is ONE white fill of the damage and the cells it covers
 * marked KNOWN BLANK in the shadow, then the same flush everything else
 * uses draws the text that belongs there; a whole repaint is one white fill
 * of the content and then every row to its last non-blank cell (rc_flush
 * does both). The two slivers outside the cell grid are ours to whiten, and
 * either fill covers them. */
void os88_paint(void *win)
{
    static struct os88_rect d;
    static struct os88_size sz;
    int whole;

    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return;                             /* nothing to draw */
    if (os88_wm_geom(win, &sz) < 0)
        return;

    if (whole || !rc_sh_ok) {
        rc_sh_inval();
    } else {
        /* the partial expose: ONE white fill of the damage rect (nobody
         * whitened it - WF_OWNBG) and the damaged cells KNOWN BLANK in the
         * shadow, so rc_flush draws only the text under it (rc_blank_rect,
         * rcterm.c - the About panel's close uses the same routine). The
         * clip is the kernel's paint clip, so the fill touches nothing of
         * another window's. */
        rc_blank_rect(win, d.x1, d.y1, d.x2, d.y2);
    }
    rc_flush(win);
    if (rc_abt)
        ovl_about_show(win);                /* the panel was over all of
                                             * that (a menu dropped over the
                                             * window, a drag): drawn again
                                             * from the live geometry */
    if (rc_wants_wake())
        os88_wm_wake(win);                  /* every callback kicks (71) */
}

/* os88_onkey - W_ONKEY, lock held. Keys go into the ring for the machine
 * (BDOS 1/6/10, BIOS CONIN): a key with an ASCII code goes as the BIOS
 * delivers it - Ctrl+letter is 1..26, Ctrl+- is 31 (^?, the editor's help),
 * Ctrl+Backspace is 127 (DEL), Enter 13, BkSp 8, Tab 9, Esc 27 (the BIOS
 * folds Ctrl-H/I/M into BkSp/Tab/Enter: identical bytes); a key with none
 * is looked up in the scan table below and goes as the VT100 sequence a
 * host terminal would send RunCPM (SPEC.md 71.2). Alt+F is the fullscreen
 * chord in both directions (the stated exception to 11.2.1 - a terminal
 * owns F and Esc); Alt+L the debug loader. */
#define RC_SCAN_ALT_F 0x21
#define RC_SCAN_ALT_L 0x26
#define RC_SCAN_ALT_C 0x2E                  /* the flush counters, toasted
                                             * (docs/RUNCPM-PORT-PLAN.md
                                             * 'Verification': calls / cells /
                                             * scrolls since the last press) */
/* the keys without an ASCII code, by scan code, and what a VT100 sends for
 * them (the xterm sequences RunCPM sees through a posix terminal): up
 * ESC[A, down ESC[B, right ESC[C, left ESC[D, Home ESC[H, End ESC[F, PgUp
 * ESC[5~, PgDn ESC[6~, Ins ESC[2~, Del ESC[3~; Ctrl+2 (scan 3) is NUL */
static const unsigned char rc_vtscan[10] = {
    0x48, 0x50, 0x4D, 0x4B, 0x47, 0x4F, 0x49, 0x51, 0x52, 0x53 };
static const char rc_vtseq[10][4] = {
    "[A", "[B", "[C", "[D", "[H", "[F", "[5~", "[6~", "[2~", "[3~" };
static void rc_key_vt(int scan)
{
    int i;
    const char *s;
    if (scan == 3) {                        /* Ctrl+2 / Ctrl+@: NUL */
        rc_key_push(0);
        return;
    }
    for (i = 0; i < 10; i++) {
        if (rc_vtscan[i] == scan) {
            rc_key_push(27);
            for (s = rc_vtseq[i]; *s; s++)
                rc_key_push(*s);
            return;
        }
    }
}
/* ovl_dbg_counters - Alt+C: the terminal's flush counters since the last
 * press, as a toast: 'gfx <calls>/<cells> scr <n>' (<= 24 characters,
 * TOAST_MAX). The instrument the plan's cost pass names - a counter in the
 * primitive, multiplied by PERFORMANCE.md's table - and it costs the machine
 * nothing until pressed. Module code (once per press). */
static int ovl_dbg_counters(void)
{
    os88_strcpy(rc_msg, "gfx ", sizeof(rc_msg));
    os88_utoa(rc_n_calls, rc_num);
    os88_strcpy(rc_msg + os88_strlen(rc_msg), rc_num, 6);
    os88_strcpy(rc_msg + os88_strlen(rc_msg), "/", 2);
    os88_utoa(rc_n_cells, rc_num);
    os88_strcpy(rc_msg + os88_strlen(rc_msg), rc_num, 6);
    os88_strcpy(rc_msg + os88_strlen(rc_msg), " scr ", 6);
    os88_utoa(rc_n_scroll, rc_num);
    os88_strcpy(rc_msg + os88_strlen(rc_msg), rc_num, 6);
    os88_toast(rc_msg, 0);
    rc_n_calls = rc_n_cells = rc_n_scroll = 0;
    return 1;
}

void os88_onkey(int ascii, int scan, void *win)
{
    if (rc_abt) {
        /* the About panel owns every key: Esc, Enter and space press OK,
         * anything else is swallowed - Alt+F included, because the latch
         * would repaint the window whole under the panel (rcabout.c) */
        if (ascii == 27 || ascii == 13 || ascii == ' ')
            rc_about_close(win);
        return;
    }
    if (ascii == 0 && scan == RC_SCAN_ALT_C) {
        ovl_dbg_counters();
        return;
    }
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
    if (ascii == 0 && scan == RC_SCAN_ALT_L) {
        /* the debug loader: when nothing runs, or the machine waits for a
         * key (the CCP at its prompt - the .COM runs in its place and a
         * RET warm-boots back to it), and no read is pending */
        if ((rc_mode == RC_M_IDLE || rc_mode == RC_M_BLOCKED) && rc_ldmode == 0) {
            rc_load_prompt();
            rc_flush(win);                  /* the lock is held here */
        }
        return;
    }
    if (rc_ldmode == RC_LD_PROMPT) {        /* the loader's prompt owns the
                                             * keys until Enter or Esc */
        if (ascii != 0) {
            ovl_load_key(ascii);
            rc_flush(win);                  /* the echo (Enter's CR/LF too),
                                             * under the held lock: ONE flush
                                             * before the read, which is the
                                             * next wake's */
        }
    } else {
        if (ascii != 0)
            rc_key_push(ascii);
        else
            rc_key_vt(scan);
        if (rc_mode == RC_M_BLOCKED && rc_khead != rc_ktail && rc_ldmode == 0)
            rc_mode = RC_M_RUN;             /* the trap is retried (71.1) -
                                             * not while the loader's read is
                                             * pending: the load takes the
                                             * machine, or rc_ld_back hands
                                             * it back and retries then */
    }
    if (rc_wants_wake())
        os88_wm_wake(win);
}

/* os88_onclick - W_ONCLICK, lock held. The terminal has no mouse; a click is
 * one more callback that KICKS (SPEC.md 71): os88_wm_wake answers -1 when
 * the 16-record event ring is full of other events and nothing was posted,
 * and a machine mid-run with no key wanted (ZEXDOC) would otherwise stall
 * until the next paint or key - so every callback that can run re-posts. */
void os88_onclick(int x, int y, void *win)
{
    if (rc_abt) {                           /* modal: OK, or nowhere */
        if (rc_about_hit_ok(x, y))
            rc_about_close(win);
        return;
    }
    if (rc_wants_wake())
        os88_wm_wake(win);
}

/* the empty menu set's handler: no menus, no item, never called (LESSONS 8) */
void os88_oncmd(int item, int menu, void *win)
{
    (void)item;
    (void)menu;
    (void)win;
}

/* the kernel bar's 'About RunCPM' (OSAPI_ABOUT_SET): the panel, drawn under
 * the lock this handler holds; the machine pauses until it comes down. A
 * refused module (0) opens nothing and pauses nothing - cc_ovneed's toast
 * says why. */
void os88_about(void *win)
{
    ovl_about_show(win);
}

/* os88_main - the entry (SPEC.md 20.2): claims first, then the window. The
 * launch requirement is CLAIMS, not KB: the 64KB Z80 RAM and the 2KB CCP
 * claim must be had, and a refusal quotes what was asked and what there is
 * (SPEC.md 71). */
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
    rc_z.seg = rc_zseg;
    /* the CCP: a 2KB claim, and CCP-DR.60K read into it NOW - from the
     * launch folder, before any folder move (SPEC.md 71.3), the way an .OVL
     * would be. The claim's base is KB-aligned, so the _seg read is legal.
     * A missing file is not a refused launch: main.c prints 'Unable to load
     * CP/M CCP.' on the terminal (rc_boot), and this port keeps the window
     * up to show it. */
    rc_ccpseg = os88_mem_claim(2);
    if (rc_ccpseg == 0) {
        os88_strcpy(rc_msg, "RunCPM: 2KB? ", sizeof(rc_msg));
        os88_utoa(os88_mem_largest_kb(), rc_num);
        os88_strcpy(rc_msg + os88_strlen(rc_msg), rc_num, 6);
        os88_strcpy(rc_msg + os88_strlen(rc_msg), " KB", 4);
        os88_toast(rc_msg, 0);
        return 0;
    }
    rc_ccplen = os88_file_read_seg(RC_CCPNAME, rc_ccpseg, RC_CCPSIZE);
    rc_ax_load();                           /* AUTOEXEC.TXT, once, from here */
    rc_term_init();
    rc_banner();
    rc_status = RC_ST_RUNNING;
    /* the slice's first length: ~50 ms of Z80 on the machine this runs on
     * (SPEC.md 71) - a control transfer is ~5 instructions of ~20 us on the
     * 8088; os88_onwake then adapts it to what the ticks say */
    rc_slice_n = 512 << os88_cpu();          /* 8086 512, 286 1024, 386+ 2048 */
    rc_clock_begin();

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
    return win;                             /* the first paint kicks */
}
