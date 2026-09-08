/* ============================================================================
 * os8088 - apps/c64/c64.c        C64: a Commodore 64, written in C
 *
 * A reimplementation of VICE 3.10's `x64` - the fast, non-cycle-exact C64 -
 * as an os8088 package: a 6510 in a 64KB heap claim, the KERNAL, BASIC and
 * character ROMs carried as part 0 of the package and claimed at launch (1.4),
 * a VIC-II and two CIAs in the C subset of SPEC.md 73, and the 320x200 screen
 * composed into 1bpp bands and blitted into a window.
 *
 * Derived from VICE (https://vice-emu.sourceforge.io/), Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later. **apps/c64/ is GPL-2-or-later**;
 * the rest of this tree is not, and apps/c64/COPYING is the licence text that
 * the licence requires to accompany copies. Nothing of VICE's SOURCE is
 * vendored (CONTRIBUTING.md 6): what is carried is behaviour, tables and
 * strings, and C64-SPEC §2 names the VICE file every user-visible
 * surface came from. The three Commodore ROM images under apps/c64/rom/ are
 * Commodore's, neither GPL nor ours, and are a stated departure from
 * CONTRIBUTING.md 6 for those three files only (C64-SPEC §1.3).
 *
 * THE CONTRACT IS docs/C64-SPEC.md, not a section of SPEC.md. A bare N.M in
 * this file's comments is a section of THAT document; SPEC.md is always
 * spelled out.
 *
 * ----------------------------------------------------------------------------
 * THE SHAPE (C64-SPEC §1.1, SPEC.md 74.1)
 * ----------------------------------------------------------------------------
 * RUNCPM's: no worker task and nothing blocking. The machine runs on the UI
 * task in wake-driven wall slices (OSAPI_WM_WAKE / OSAPI_WM_ONWAKE), and a
 * C64 sitting at READY. costs nothing until a key or a tick arrives. The one
 * worker this package ever hires is the one that closes the window for
 * File > Exit emulator, because there is no self-close slot.
 *
 * ----------------------------------------------------------------------------
 * THE SLICE DRIVER (C64-SPEC §4.4, SPEC.md 74.1)
 * ----------------------------------------------------------------------------
 * os88_onwake is the whole loop, and it is VICE's alarm model with the
 * service call turned inside out (c64cpu.inc's head says why):
 *
 *     while the wall budget lasts:
 *         n = min(budget, c64_alarm_next())    cycles to the next EVENT
 *         r = c64_run(n)                       the core, in registers
 *         ran = n - c64_m.cnt                  what actually ran, exactly
 *         c64_advance(ran)                     every device phase moves
 *
 * Nothing quantises to a jiffy: the PAL frame is 63 x 312 = 19,656 cycles and
 * the 60 Hz jiffy EMERGES from the KERNAL programming CIA1 timer A itself.
 * The wall budget is 256..16,384 cycles seeded from os88_cpu(), doubled when
 * four slices fit inside one host tick and halved when one spans two - and
 * ONLY A GENUINELY EXHAUSTED SLICE ADAPTS, because without that rule ordinary
 * idling walks the budget to its cap and the next busy slice is a second of
 * stalled UI task (LESSONS.md 13).
 * ==========================================================================*/

#include "os88.h"

/* --- the callbacks the shim declares (C64.asm's CC_HAS_*) ---------------- */
void *os88_main(void);
void  os88_paint(void *win);
void  os88_onkey(int ascii, int scan, void *win);
void  os88_onclick(int x, int y, void *win);
void  os88_oncmd(int item, int menu, void *win);
void  os88_about(void *win);
void  os88_onwake(void *win);
void  os88_onfile(int mode, const char *name,
                  unsigned size_lo, unsigned size_hi, void *win);

/* ==========================================================================
 * THE MACHINE'S SHAPE
 * ========================================================================*/

/* The 6510's register file lives in c64cpu.inc's .bss as _c64_m, because the
 * core loads and stores it with no frame: this is the C's view of the same
 * bytes and the field order IS the layout (C64-SPEC §4.1). */
struct c64_mach {
    unsigned ramseg;                        /* the 64KB RAM claim */
    unsigned romseg;                        /* the 20KB ROM claim */
    unsigned pc;
    unsigned a, x, y, s, p;
    unsigned cnt;                           /* wall-slice cycles left */
    unsigned reason;                        /* C64_RUN_* of the last return */
};
extern struct c64_mach c64_m;

#define C64_RUN_SLICE 0
#define C64_RUN_JAM   1

/* --- c64mem.inc: the claim accessors and the movers (3.6) ---------------- */
int  c64_rd(unsigned a);
int  c64_rd16(unsigned a);
void c64_wr(unsigned a, int v);
void c64_dirty(unsigned a);
int  c64_scr_rd(unsigned off);
void c64_scr_wr(unsigned off, int v);
void c64_dirty_take(unsigned char *dst36);  /* the 32-byte page bitmap and the
                                             * write window's two words into
                                             * the package, and the scratch
                                             * reset, in ONE call: the flush
                                             * used to do it in ~50 (9.2) */
int  c64_rom_rd(unsigned off);
void c64_zcopy_in(unsigned a, const void *src, unsigned n);
void c64_zcopy_out(void *dst, unsigned a, unsigned n);
int  c64_copy_row(unsigned dseg, unsigned doff, int n);
void c64_zzcopy_in(unsigned a, unsigned sseg, unsigned soff, unsigned n);
void c64_zfill(unsigned a, int v, unsigned n);
unsigned c64_muldiv(unsigned a, unsigned b, unsigned d);    /* (a*b)/d in 32
                                             * bits - `frames * 182 / elapsed`
                                             * laps a 16-bit product at five
                                             * seconds */
unsigned c64_div32(unsigned hi, unsigned lo, unsigned d);   /* (hi:lo)/d, and
                                             * 0xFFFF where the quotient will
                                             * not fit - the two-word counters
                                             * of 10.2 need one `div` */

/* --- c64cpu.inc: the core (4.5) ----------------------------------------- */
int  c64_run(unsigned cycles);          /* -> C64_RUN_*, and c64_m.cnt is what
                                         * was NOT spent, so the caller's
                                         * `ran = asked - cnt` is exact */
int  c64_bread(unsigned a);             /* one BANKED byte - the reset vector
                                         * is under the KERNAL (11.1) */
void c64_cut(void);                     /* end the run in progress, exactly */
void c64_rebank(void);                  /* the map moved: no instruction is
                                         * fetched under a stale bias (4.3) */

/* --- c64band.inc: the composers (9.5) ------------------------------------ */
void c64_band1(unsigned char *dst, int first, int last,
               unsigned mseg, unsigned moff,
               const unsigned char *col,
               unsigned gseg, unsigned goff,
               int mode, int bg);
int  c64_rowspan(const unsigned char *a, const unsigned char *b, int n);
void c64_rowcopy(unsigned char *dst, const unsigned char *src, int n);
unsigned c64_rowsig(unsigned mseg, unsigned moff,
                    const unsigned char *col, int n);
void c64_x2init(void);
void c64_band_x2(unsigned char *dst, const unsigned char *src, int rows);

/* The two bytes c64band.inc reads out of the C's own storage. They are
 * ordinary globals and not statics because nasm has to see the label. */
unsigned char c64_cmask[32];        /* 16 {and, xor} pairs, by fg nibble (9.6) */
unsigned char c64_bgfill;           /* the uniform level of a mode wave 1 does
                                     * not compose yet */

/* --- the core's scratch, as the C reads it (3.5) ------------------------- */
#define C64_SCR_DIRTY 0x00          /* 32 bytes: the 256-page dirty bitmap */
#define C64_SCR_BLO   0x20          /* the LOW edge of the biased fetch region
                                     * - the guard is a RANGE, which is what
                                     * makes a backward control transfer safe
                                     * without re-biasing on every branch */
#define C64_SCR_CARRY 0x22          /* cycles c64_cut() took out of the
                                     * countdown, put back at the expiry */
#define C64_SCR_DEAD  0x24          /* THE countdown - the hot counter */
#define C64_SCR_BOUND 0x26
#define C64_SCR_FES   0x28
#define C64_SCR_IRQ   0x2A          /* bit 0 the IRQ LEVEL, bit 1 an NMI EDGE */
/* ...and the WRITE WINDOW, a wave-1 refinement amended into
 * C64-SPEC §9.2: the lowest and highest addresses written since the
 * last flush. A 256-byte page is 6.4 character rows, so a page bitmap alone
 * can only say "recompose seven rows, all forty cells"; these two words turn
 * a one-byte poke back into a one-CELL compose, at four more instructions on
 * the write path and four bytes of scratch. */
#define C64_SCR_WLO   0x2C
#define C64_SCR_WHI   0x2E
/* ...and ONE byte that says the core wrote SOMETHING since the last flush.
 * The wake has to decide whether to flush before it has read the bitmap, and
 * reading 32 bytes of scratch through a near call each to find out would cost
 * more than the decision is worth on an 8088. One `mov byte [es:..],1` on the
 * write path answers it in one read. */
#define C64_SCR_ANY   0x30
/* ...and the WATCH RANGE the window is taken over, which is what keeps it
 * meaningful once there is a core: without it every JSR (the stack at $01xx)
 * and every BASIC statement (zero page, the variables above $0800) widens the
 * window across the whole matrix inside one slice, and the per-row
 * intersection below always answers "all forty cells". The C writes
 * mbase..mbase+999 here from c64_frame_regs. */
#define C64_SCR_WATLO 0x32
#define C64_SCR_WATHI 0x34
/* ...and the three bytes of the bank map the CORE reads on every access above
 * $A000 (3.3). They are here rather than in the package's bss because the
 * core reads them DS-relative, with DS the C64's own RAM, and c64io.c's
 * c64_map_publish is their one writer. */
#define C64_SCR_MAPA  0x36
#define C64_SCR_MAPD  0x37
#define C64_SCR_MAPE  0x38
#define C64_SCR_END   0x3A                  /* $FFC0 + 0x3A - 1 = $FFF9 */

/* --- the ROM PART's fixed layout (1.4) ------------------------------------
 * It was a SIDECAR - C64.ROM, 20,480 bytes beside the package on the disk -
 * and it is part 0 of C64.O88 now (SPEC.md 20.12). The layout is unchanged
 * and so is tools/c64rom.py, which still concatenates the three committed
 * Commodore images and checks their SHA-256; what changed is that os88pkg.py
 * appends the result to the package instead of os88disk.py putting it on the
 * floppy. A file copy can no longer separate the program from the ROM it is
 * useless without, and the whole halted-machine state that existed to say so
 * when it went missing is gone with it. */
#define C64_ROM_PART    0
#define C64_ROM_KERNAL  0x0000
#define C64_ROM_BASIC   0x2000
#define C64_ROM_CHARGEN 0x4000
#define C64_ROM_SIZE    20480               /* what os88pkg.py appends, and
                                             * what op_size claims for it */

/* --- the window (9.1) ---------------------------------------------------- */
#define C64_SCRW   320                      /* the C64's visible pixels */
#define C64_SCRH   200
#define C64_BORDER 8                        /* on every side */
#define C64_STATH  10                       /* the status row */
#define C64_W_X    7
#define C64_W_Y    20
/* THE FRAME IS TWO PIXELS WIDER THAN THE CONTENT, AND THAT IS THE KERNEL'S
 * ARITHMETIC AND NOT A FUDGE. os88_wm_create authors a FRAME; os88_wm_geom
 * answers the CONTENT box and kernel/wm.inc:5442 is `sub cx, 2` - the window's
 * two 1-pixel side borders. A window authored 336 therefore has 334 pixels of
 * content, and wave 1's first draft authored 336 and then laid a 42-cell =
 * 336-pixel status row inside it: the last cell (§10.1's pause lamp `P` at
 * 328..335) needed pixels the box does not have, so on the first paint it
 * spilled two pixels onto the window's right border and on every later
 * flush - when the clip is the CONTENT - it was dropped entirely. `P` was
 * never on the glass in a windowed C64 (build/port-shots/w1r2-lamps-on.png).
 * The centring told the same story from the other side: (334 - 320) / 2 = 7,
 * clamped up to C64_BORDER, so the border was 8 left and 6 right against
 * C64-SPEC §9.1's "an 8-pixel border on every side".
 *
 * So the CONTENT width is authored and the frame is derived. 336 keeps every
 * number in §9.1, §10.1 and §10.3 true - 42 whole cells, an 8-pixel border on
 * both sides - and it keeps OSAPI_GFX_SCROLL's edges on cell boundaries by
 * construction rather than by the `& ~7` clamp (c64_geom): the content origin
 * is on a multiple of 8 (os88_wm_snap), the screen sits 8 inside it and
 * x2 + 1 = the box's right edge, both multiples of 8 (§9.4). */
#define C64_CONT_W (C64_SCRW + C64_BORDER * 2)              /* 336 */
#define C64_W_W    (C64_CONT_W + 2)                         /* 338 framed */
#define C64_CONT_H (C64_SCRH + C64_BORDER * 2 + C64_STATH)  /* 226 */
/* The content height is W_H - TITLE_H - 1, which is LESSONS.md 13's finding:
 * a window authored TITLE_H + 200 tall showed 24 rows and a sliver, and the
 * model's last row was never on the glass. */
#define C64_W_H    (OS88_TITLE_H + C64_CONT_H + 1)          /* 245 */

#define C64_ROWS 25
#define C64_COLS 40
#define C64_CELLS (C64_ROWS * C64_COLS)

/* i * 40 and i * 320, written as shifts. cc8086 refuses an `imul ax, ax, 40`
 * wherever it cannot prove a scratch register dead, and a stride that is not
 * a power of two is exactly the shape that provokes it (LESSONS.md 3, and
 * SPEC.md 73's rule that the 8086 has no cheap general multiply anyway). The
 * C64's strides are 40 and 320 and cannot be padded, so they are shifted. */
#define C64_X40(i)  ((((unsigned)(i)) << 5) + (((unsigned)(i)) << 3))
#define C64_X320(i) ((((unsigned)(i)) << 8) + (((unsigned)(i)) << 6))

/* Alt+D's BIOS scan code (ascii 0). It is the fullscreen door in both
 * directions - SPEC.md 11.2.1, and C64-SPEC §9.8's stated exception to it. */
#define C64_SCAN_ALT_D 0x20

/* --- what the machine is doing ------------------------------------------- */
#define C64_ST_RUN  0
#define C64_ST_JAM  1
#define C64_ST_HALT 2                       /* not started: the state c64_state
                                             * holds until os88_main has the
                                             * ROM and resets the CPU. It was
                                             * also a machine that never
                                             * started because C64.ROM was not
                                             * on the disk - which cannot
                                             * happen now the ROM is a part
                                             * (1.4) */
#define C64_ST_DEAD 3                       /* the close is in flight (75.2) */

static int c64_state = C64_ST_HALT;
static void *c64_win;
static int c64_kick;                        /* a wake is wanted */
static int c64_exit_req;                    /* File > Exit emulator asked;
                                             * spent at the top of the next
                                             * wake (75.2, os88_onwake) */
/* ...and Edit > Copy and Edit > Paste, for the same reason a different way:
 * their bodies are ~103 ms and a heap claim each, and os88_oncmd holds the
 * DESKTOP's gfx lock while a command runs (c64_clip_service, c64kbd.c). */
static int c64_copy_req;
static int c64_paste_req;
static int c64_reset_req;                   /* ...and File > Reset machine CPU
                                             * (1) / Power cycle machine (2),
                                             * whose RAM fill is 64KB under the
                                             * same lock (c64_reset_service) */
static int c64_ovl_asked;                   /* 13.3's first-wake probe ran */
/* ...AND WHETHER IT ANSWERED YES, WHICH IS A DIFFERENT FACT AND THE ONE THAT
 * KEEPS FLOPPY I/O OUT OF THE GFX LOCK. Reaching an `ovl_*` makes the runtime
 * resolve the module, and if it is not resident that is an OSAPI_MEM_CLAIM and
 * an OSAPI_FILE_READ - a floppy seek, ~400 ms a call (PERFORMANCE.md) - inside
 * whatever context asked. os88_oncmd, os88_about and os88_onfile are all
 * dispatched UNDER the desktop's gfx lock, so a first-wake probe that failed
 * for a transient reason (no heap at that moment) left every later menu pick
 * able to go to the disk with the whole machine stopped behind it.
 *
 * So a LOCKED caller never crosses the bridge unless this says the module is
 * already there. It refuses, says so, and clears c64_ovl_asked - and the next
 * wake, which holds no lock and may call the file slots by contract
 * (SPEC.md 74.1), is what retries the load. */
static int c64_ovl_res;
static int c64_conv_ok;                     /* ...and it BUILT the two
                                             * conversion tables (7.7): the
                                             * probe is now ovl_conv_init,
                                             * so the flag that says the
                                             * module answered and the flag
                                             * that says the tables are
                                             * there are two different
                                             * facts */

/* The state the parts share. It is declared HERE, above every #include, so
 * that each part has one definition to read and none of them can quietly
 * declare a second copy of a flag the others set. */
static int c64_dirty_any;                   /* something wants composing */
static int c64_border_dirty;                /* $D020 moved (9.3 step 5) */
static int c64_sh_ok;                       /* the shadow describes the glass */
static unsigned c64_last_flush;             /* the tick the last flush ran in */
static int c64_full;                        /* Alt+D's latch (9.8) */
static int c64_warp;                        /* Alt+W: the wall slice's CAP
                                             * lifted, the flush rate left
                                             * alone (4.4) */
static int c64_pause;                       /* Alt+P */
static int c64_adv;                         /* Preferences > Advance frame: a
                                             * REQUEST the slice driver
                                             * serves, not work done under the
                                             * menu's lock (§11.1) */
/* the colour RAM's own write window - the matrix's is in the core's scratch
 * (9.2). It is here rather than in c64scr.c because c64io.c, which is
 * included first, is what MAINTAINS it. */
static int c64_sid_dirty;                   /* a SID register moved: voice 1
                                             * goes to the speaker once a
                                             * slice, on a change only (11.4) */
static int c64_sid_tries;                   /* ...and a REFUSED grant is not
                                             * permanent: the latch is kept
                                             * and the tone re-asked for up to
                                             * C64_SID_TRIES wakes, then
                                             * dropped until the guest writes
                                             * a SID register again (11.4) */
static int c64_sid_said;                    /* the refusal is stated ONCE a
                                             * session (SPEC.md 47), never
                                             * once a wake */
static int c64_snd_tone;                    /* the machine HAS a square voice:
                                             * os88_snd_caps() & SND_CAP_TONE,
                                             * asked once in os88_main (11.4).
                                             * A capability is a fact to test,
                                             * not a guess (SPEC.md 73.11) */
#define C64_SND_CAP_TONE 0x01               /* apps/os88api.inc:280 */
#define C64_SID_TRIES 8                     /* ~half a second of wakes: long
                                             * enough for a toast tone or
                                             * another app's grant to end,
                                             * bounded so a machine whose
                                             * speaker is held for good costs
                                             * eight far calls and not one a
                                             * wake for ever */
static unsigned c64_clo = 0xFFFF, c64_chi;
static int c64_flush_every = 1;             /* host ticks between flushes,
                                             * from the tier and the bench */
/* --- the wall slice (4.4) ------------------------------------------------ */
#define C64_SLICE_MIN 256                   /* ~3 ms of emulated time: the
                                             * floor is never a whole jiffy */
#define C64_SLICE_MAX 16384
/* ...AND WARP IS THIS CEILING RAISED, WHICH IS THE ONLY THROTTLE THIS PORT
 * HAS (C64-SPEC §4.4). Nothing here paces the machine against a wall clock -
 * SPEC.md 74.4's reasoning, and §11.2 greys Emulation speed for it - so the
 * one thing standing between the 6510 and the host is how many cycles a wake
 * is allowed to run before it gives the UI task back. Warp raises that and
 * changes nothing else: the flush stays at the tier's rate, so a warping
 * machine is still watchable, and the ONLY cost of the higher ceiling is a
 * longer worst-case wake.
 *
 * 30,000 AND NOT 32,767, WHICH WOULD BE THE ARITHMETIC LIMIT. c64_m.cnt is a
 * SIGNED word (4.2) and a budget over 32,767 arrives negative, so the core
 * expires before its first fetch; the margin is for the alarm model, which
 * hands c64_slice a `n` it may round UP by the cycles of the instruction in
 * progress. */
#define C64_SLICE_WARP 30000
/* ...AND THE OTHER HALF OF WARP IS THAT IT DRAWS LESS, WHICH IS VICE'S TOO.
 * src/vsync.c:339-340 sets `warp_render_tick_interval = tick_per_second() /
 * 10.0` under the comment `Limit warp rendering to 10fps`, and :634-656 skips
 * every frame inside that interval while `warp_enabled` - `It's ugly enough
 * for dqh to weep but makes warp faster.` A pass of this port's review
 * deleted the cap on the reading that flushing less often is a different
 * feature with the same name; the reference says it is half of THIS feature,
 * and on this machine it is the expensive half (§9.7: 306 ms for a full
 * expose against a 16,384-cycle slice).
 *
 * 10 fps at 18.2 Hz is 1.82 host ticks, so TWO - and the cap can only ever
 * slow a flush down, never speed one up: the rate is
 * max(c64_flush_every, C64_WARP_FLUSH). On the CPU_8086 tier c64_flush_every
 * is already 2 (§9.8: a full repaint there is ~300 ms, five host ticks), so
 * that tier is already flushing BELOW VICE's warp cap and warp moves nothing
 * at either end - which is why the item still says so on that tier. */
#define C64_WARP_FLUSH 2
static int c64_budget = C64_SLICE_MIN;      /* 6510 cycles per WAKE */
static int c64_fastn;                       /* slices inside one host tick */
/* --- the speed widget's two-word arithmetic (10.2) ----------------------- */
static unsigned c64_sp_tick;                /* the tick the window opened */
static unsigned c64_sp_lo, c64_sp_hi;       /* the cycle counter then */
static unsigned c64_sp_fr;                  /* ...and the frame counter */
static int c64_pct;                         /* `%7.0f%% cpu`  - per cent of a
                                             * real PAL 6510 */
static int c64_fps10;                       /* `%8.1f fps` - EMULATED VIC
                                             * frames a second, in tenths */

/* the message area's text and its deadline (10.1).
 *
 * A MESSAGE OWNS THE ROW'S FIELDS AND NOT THE ROW: the two lamps at cells 40
 * and 41 are drawn under it, so the one indicator that reports the PAUSE
 * state is on the glass in the state it reports (§10.1). 40 cells is the
 * field area, and every message this port shows fits it. The specific longest
 * string is deliberately NOT named here: the sentence that named one went
 * stale inside a wave, twice. hosttest/c64uitest.c's literal walk is what
 * holds the invariant, and apps/c64/build.sh checks that the walk's list is
 * the same set of literals the sources actually contain. */
#define C64_MSGMAX 43
#define C64_MSGCELLS 40
static char c64_msg[C64_MSGMAX];
static unsigned c64_msg_until;

static char c64_jamline[24];                /* `Main CPU: JAM at $E5CF` (4.5) -
                                             * VICE's own line, src/maincpu.c:
                                             * 612 with CPU_STR from
                                             * src/6510core.c:45. 22 glyphs */
static char c64_title[] = "VICE (C64)";     /* ui.c:1842 "VICE (%s)" +
                                             * c64.c:179 machine_name "C64" */

/* forward declarations - the host harness compiles this same C with clang,
 * which is stricter about them than SmallerC (LESSONS.md 3) */
static void c64_say(const char *s);
static void c64_advance_frame(void);        /* ovl_cmd calls it, and ovl_cmd
                                             * is #included above it */
static void c64_hex4(char *d, unsigned v);
static void c64_flush(void *win);
static void c64_sh_inval(void);
static void c64_blank_rect(void *win, int x1, int y1, int x2, int y2);
static void c64_force_rows(int r0, int r1, int c0, int c1);
static void c64_lum_update(void);
static void c64_dirty_all(void);
static void c64_row_dirty(int row);
static void c64_frame_regs(void);
static int  c64_geom(void *win);
static void c64_watch_set(void);
static void c64_menu_state(void);
static void c64_tier_init(void);
static void c64_fullscreen_toggle(void *win);
static void c64_ram_pattern(void);
static void c64_reset_regs(void);
static void c64_reset_cpu(void);
static void c64_irq_update(void);
static void c64_nmi_raise(void);
static void c64_advance(int n);
static int  c64_alarm_next(void);
static int  c64_kbd_pa(void);
static int  c64_kbd_pb(void);
static void c64_kbd_poll(void);
static void c64_paste_stop(void);
static void c64_clip_service(unsigned base);
static void c64_reset_service(void);
static void c64_speed_fold(void);
static void c64_sound_stop(void);
static int  c64_sid_voice1(void);
static int  ovl_about_show(void *win);
static int  ovl_cmd(int menu, int item, void *win);
static int  ovl_conv_init(void);
static int  ovl_load_prg(const char *name, unsigned size_lo);

/* The parts of the one translation unit (SPEC.md 73.1: `nasm -f bin` has no
 * external symbols, so a C package cannot be several .c files linked - it is
 * one file with the parts #included into it). Every one of them is a written
 * prerequisite in the Makefile, because make cannot see through #include. */
#include "c64io.c"                          /* the register files, the alarm
                                             * scheduler, the CIAs and the VIC */
#include "c64kbd.c"                         /* the gtk3_sym.vkm table */
#include "c64scr.c"                         /* the composer's C half, the
                                             * shadow, the flush, the status
                                             * row, the tier table */
#include "c64menu.c"                        /* the five menus, from VICE's
                                             * own uimachinemenu.c */
#include "c64cmd.c"                         /* ovl_*: the command bodies */
#include "c64load.c"                        /* ovl_*: Smart attach */
#include "c64about.c"                       /* ovl_*: the About panel */

/* ==========================================================================
 * THE MESSAGE AREA
 *
 * Every refusal in this port takes BOTH routes - the status row and a toast -
 * because the bar a toast lands on is UNDER a fullscreen window (LESSONS.md
 * 13, C64-SPEC §9.8), and fullscreen is where a C64 will mostly be.
 * ========================================================================*/
/* c64_hex4 - four hex digits, because every address this program has to name
 * is one and os88_utoa is decimal. */
static void c64_hex4(char *d, unsigned v)
{
    int i, n;
    for (i = 0; i < 4; i++) {
        n = (int)((v >> 12) & 0x0F);
        d[i] = (char)((n < 10) ? ('0' + n) : ('A' + n - 10));
        v = v << 4;
    }
}

static void c64_say(const char *s)
{
    os88_strcpy(c64_msg, s, C64_MSGCELLS + 1);  /* the field area is 40 cells
                                                 * wide: the two lamps keep
                                                 * their own two (§10.1) */
    c64_msg_until = os88_ticks() + 90;      /* ~5 s at 18.2 Hz */
    c64_st_lok = 0;                         /* the row's LEFT field cells no
                                             * longer say what we want: the
                                             * status row's delta compares
                                             * FIELDS, and one message
                                             * replacing another is the same
                                             * field with different text. This
                                             * is the one writer of c64_msg,
                                             * so it is the one place that has
                                             * to say so.
                                             *
                                             * AND IT IS c64_st_lok AND NOT
                                             * c64_st_ok, WHICH IS THE WHOLE
                                             * OF THE STATUS ROW'S FIX. A
                                             * message of 25 cells or fewer
                                             * stops at cell 24 and does not
                                             * touch a pixel of the joystick
                                             * widget, the drive number or the
                                             * two lamps; clearing the row's
                                             * own flag erased and rebuilt all
                                             * of them for every message,
                                             * which after wave 3 meant that
                                             * DEFLECTING THE JOYSTICK - which
                                             * raises `ScrollLock for
                                             * joystick` - blanked the two
                                             * indicators that report the
                                             * joystick (c64scr.c). */
    /* ...AND c64_dirty_any, or the status route never happens. The wake's
     * flush is gated on c64_dirty_any (os88_onwake below), so a message
     * written from a menu command or a Smart attach - "Warp mode on.",
     * "Paused.", every ovl_load_prg result - marked the row, found
     * c64_dirty_any == 0, ran no flush, and then cleared c64_kick so no later
     * wake was posted either. Under a fullscreen window the toast is not
     * visible either, and the paragraph above is the whole reason both routes
     * exist. RUNCPM's rc_say_now flushes on the spot; here the wake is one
     * tick away, so raising the flag is enough and stays off the lock. */
    c64_dirty_any = 1;
    os88_toast(s, 0);
}

/* ==========================================================================
 * THE POWER-UP RAM PATTERN (src/ram.c:169-177, and ram_init_with_pattern at
 * :257-336)
 *
 * A C64 does not come up with 64KB of zeros, and VICE knows exactly what it
 * does come up with: ram.c gives the C64 the factory values
 * RAMInitStartValue 0, RAMInitValueInvert 4, RAMInitValueOffset 2,
 * RAMInitPatternInvert 16384, RAMInitPatternInvertValue 255. Put through
 * ram_init_with_pattern's `value = start ^ j ^ k`, where
 *
 *      j = (((offset + 2) / 4) & 1) ? 0xFF : 0x00
 *      k = ((offset / 16384) & 1)   ? 0xFF : 0x00
 *
 * that is the eight-byte period 00 00 FF FF FF FF 00 00, inverted for every
 * other 16K block - so $0000-$3FFF and $8000-$BFFF carry it and $4000-$7FFF
 * and $C000-$FFFF its complement. (VICE's random_chance is 0 by default, so
 * nothing here is random and this is reproducible, which is what makes it
 * testable at all.)
 *
 * It is 256 os88 calls and not 65,536: one page of the pattern is built once
 * in the package's own DS and copied into the claim a page at a time, with
 * the page complemented in place at each 16K boundary. The scratch at
 * $FFC0 is not RAM (3.5) and is left alone, exactly as the c64_zfill this
 * replaces did.
 * ========================================================================*/
static unsigned char c64_rampat[256];

static void c64_ram_pattern(void)
{
    unsigned a;
    int i, p, k, inv, want, n;

    for (i = 0; i < 256; i++) {
        k = i & 7;
        c64_rampat[i] = (unsigned char)((k >= 2 && k <= 5) ? 0xFF : 0x00);
    }
    inv = 0;
    for (p = 0; p < 256; p++) {
        a = ((unsigned)p) << 8;
        want = (a & 0x4000u) ? 1 : 0;       /* inverted every 16,384 bytes */
        if (want != inv) {
            for (i = 0; i < 256; i++)
                c64_rampat[i] = (unsigned char)(c64_rampat[i] ^ 0xFF);
            inv = want;
        }
        n = (p == 255) ? (0xFFC0 - 0xFF00) : 256;
        c64_zcopy_in(a, c64_rampat, (unsigned)n);
    }
}

/* c64_fullscreen_toggle - SPEC.md 11.2's latch, RESIDENT, because both routes
 * to it need it: Preferences > Fullscreen in the overlay and Alt+D on the
 * keystroke path, which never loads an overlay (SPEC.md 73.14's split is by
 * FREQUENCY and a keystroke is the frequent side).
 *
 * IT DOES NOT TOUCH THE SHADOW. OSAPI_FULLSCREEN repaints the window whole,
 * synchronously, in BOTH directions - os88.h:604 says so and
 * kernel/wm.inc:4147 shows it (wm_fullscreen calls wm_raise with AL = 1), so
 * os88_paint has already run nested inside the call and the shadow describes
 * the new glass by the time it returns. */
static void c64_fullscreen_toggle(void *win)
{
    /* AND THE PANEL DOES NOT SURVIVE A GEOMETRY CHANGE. os88_paint computes
     * the hold rows (c64.c's W_PAINT) from c64_abt_x/y/w/h - the rect
     * ovl_about_show left behind - and OSAPI_FULLSCREEN repaints the window
     * whole and SYNCHRONOUSLY, so that paint runs nested inside the call
     * below with the OLD rect still in those four statics while the box under
     * it has just become 640 x 440 (or come back). Two defects fell out at
     * once: the rows the old rect covered were held, so the flush skipped
     * them, and nothing redrew them - os88_onwake returns early on c64_abt
     * and this path never sets c64_kick, so under WF_OWNBG that is a
     * full-width strip of stale pixels; and the rows the NEW rect covers were
     * composed and blitted a moment before the panel was painted over them,
     * which is the double-draw PERFORMANCE.md rule 2 names. Alt+D reaches
     * this with the panel up (os88_onkey does not test c64_abt), and so does
     * Preferences > Fullscreen, because a menu click is not a W_ONCLICK and
     * does not close the panel.
     *
     * The panel comes down first, and its rect is DAMAGE the way a click's is
     * (c64.c's os88_onclick): the rows it covered are forced, so the repaint
     * the kernel is about to do draws them. */
    if (c64_abt) {
        c64_abt = 0;
        c64_blank_rect(win, c64_abt_x, c64_abt_y,
                       c64_abt_x + c64_abt_w - 1,
                       c64_abt_y + c64_abt_h - 1);
        c64_dirty_any = 1;                  /* ...and if the latch below is
                                             * REFUSED, nothing repaints us:
                                             * the next wake owes those rows */
    }
    c64_full = !c64_full;
    if (os88_fullscreen(win, c64_full) < 0) {
        c64_full = !c64_full;               /* refused: the latch rolls back */
        /* ...AND THE REFUSAL OWES A REPAINT. The success arm is paid for by
         * the kernel - OSAPI_FULLSCREEN repaints the window whole, nested,
         * in both directions (9.8) - so nothing here draws. On the REFUSED
         * arm nothing repaints us at all, and the About panel that was just
         * taken down above left a hole the size of the panel. c64_blank_rect
         * marked those rows; this is what makes somebody come and draw them. */
        c64_dirty_any = 1;
        c64_kick = 1;
        os88_wm_wake(win);
    }
    c64_menu_state();
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

/* c64_ovl_ready - "may this LOCKED callback cross into C64.OVL?" (13.3)
 *
 * Answering no is not a refusal of the feature; it is a refusal to do FLOPPY
 * I/O with the desktop's gfx lock held. Every route into the module is a
 * callback the kernel dispatches under it, and the runtime resolves a module
 * on first use - a claim and a file read, ~400 ms a disk call on the target.
 * The first wake asks, unlocked, and every locked caller reads that answer;
 * a no clears the probe so the NEXT wake asks again, which is where the retry
 * belongs. The user is told either way, on the row and not only in a toast,
 * because a toast under a fullscreen window is under a bar they cannot see. */
static int c64_ovl_ready(void *win)
{
    if (c64_ovl_res)
        return 1;
    c64_say("Unable to load C64.OVL.");
    c64_ovl_asked = 0;                      /* the wake retries it, unlocked */
    c64_kick = 1;
    os88_wm_wake(win);
    return 0;
}

/* os88_paint - W_PAINT, the gfx lock is ALREADY held. WF_OWNBG is set, so the
 * kernel did not whiten the content and os88_wm_damage() says which part
 * needs drawing: a whole repaint invalidates the shadow and the flush draws
 * everything; a partial expose whitens the damage and marks the pixels it
 * covers UNKNOWN, so the flush draws only what is under it. */
void os88_paint(void *win)
{
    static struct os88_rect d;
    int whole;

    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return;
    if (c64_geom(win) < 0)
        return;

    if (whole || !c64_sh_ok)
        c64_sh_inval();
    else
        c64_blank_rect(win, d.x1, d.y1, d.x2, d.y2);

    /* THE PANEL OWNS ITS ROWS AND THE FLUSH SKIPS THEM. Composing and
     * blitting a row and then painting an opaque panel over it is the
     * double-draw PERFORMANCE.md rule 2 names and an emulator will never show
     * you: up to 271 ms of C64 screen, of which the middle thirteen rows are
     * covered a moment later. The panel is 336 wide and snapped to the cell
     * grid (c64about.c) precisely so that "the rows it covers" is exact in
     * BOTH directions - no strip of screen beside it and no half-covered row
     * at either end - and the rows stay dirty and forced, so the click that
     * closes it draws them once.
     *
     * The horizontal cover is CHECKED and not assumed: a window narrower than
     * the panel clamps it, and holding a row the panel does not span end to
     * end would leave the uncovered part of it undrawn under WF_OWNBG. */
    c64_hold_r0 = 1;
    c64_hold_r1 = 0;
    if (c64_abt                             /* c64_geom ran above */
        && c64_abt_x <= c64_gsx
        && c64_abt_x + c64_abt_w >= c64_gsx + C64_COLS * c64_scw) {
        /* ...ON THE CELL GRID, WHICH IS 8 OR 16 PIXELS A SIDE (9.8). */
        c64_hold_r0 = (c64_abt_y - c64_gsy) / c64_sch;
        c64_hold_r1 = (c64_abt_y + c64_abt_h - 1 - c64_gsy) / c64_sch;
        if (c64_abt_y - c64_gsy < 0)
            c64_hold_r0 = 0;                /* C truncates toward zero */
    }

    c64_flush(win);
    /* THE PANEL IS REDRAWN ONLY WHEN THE DAMAGE REACHED IT, and its answer is
     * the latch. Redrawing it unconditionally cost 1 fill + 2 frames +
     * 9 font_str over 160 glyph cells - ~153 ms - on every expose, which is
     * MORE than the 266 ms full expose the hold rows exist to beat once the
     * 122 ms they cost is added: a menu closing over one corner of the window
     * repainted the whole panel. And ovl_about_show ANSWERS A STATUS
     * (c64cmd.c's own rule): 0 is a refused overlay load - no C64.OVL on the
     * disk, a stale module, no heap - and the latch has to become that
     * answer, or os88_onwake parks on `if (c64_abt)` with no panel on the
     * glass, the machine looks frozen and the toast that said why is gone. */
    if (c64_abt
        && (whole || (d.x1 <= c64_abt_x + c64_abt_w - 1 && d.x2 >= c64_abt_x
                      && d.y1 <= c64_abt_y + c64_abt_h - 1
                      && d.y2 >= c64_abt_y))) {
        if (!ovl_about_show(win))
            c64_abt = 0;                    /* the rows it held stay dirty and
                                             * forced: the next wake draws
                                             * them, so no strip is left */
    }
    c64_hold_r0 = 1;
    c64_hold_r1 = 0;
    if (c64_kick)
        os88_wm_wake(win);
}

/* os88_onkey - W_ONKEY, the lock is held. IT DOES NOT TOUCH THE MATRIX: the
 * make goes into the ring here and the once-per-wake poll (c64_kbd_poll, 7.2)
 * is what rebuilds the matrix from the whole down-list, so every emulated
 * CIA1 read inside one wake sees one consistent keyboard. Esc is not handled
 * here at all - the C64 owns it as RUN/STOP (7.1). */
void os88_onkey(int ascii, int scan, void *win)
{
    /* ALT+D IS THE DOOR, IN BOTH DIRECTIONS - and under WF_FULL it is the
     * ONLY one. SPEC.md 11.2.1 binds F and Esc to enter and leave a
     * fullscreen surface, "the key that got you there is the key that
     * leaves"; C64-SPEC §9.8 takes the stated exception because the C64 owns
     * F (a letter) and Esc (RUN/STOP), and puts VICE's own Alt+D there
     * instead. That exception only pays for itself if the chord is
     * IMPLEMENTED: kernel/wm.inc draws no chrome at all for a WF_FULL window
     * - no menu bar - so a fullscreen C64 with no chord is a machine with no
     * way back, and the menu item that got you in is not on the glass any
     * more. BIOS Alt+D is ascii 0, scan 0x20. */
    if (ascii == 0 && scan == C64_SCAN_ALT_D) {
        c64_fullscreen_toggle(win);
        return;
    }

    /* AND EVERY OTHER CHORD THIS MENU ADVERTISES IS DISPATCHED HERE, BECAUSE
     * A CAPTION IS NOT AN ACCELERATOR IN THIS KERNEL. c64menu.c prints
     * VICE's own `.vhk` bindings beside the items - Alt+A, Alt+F9, Alt+Q,
     * Alt+W, Alt+P, Alt+J - and SPEC.md 12.2's menu bar does not bind any of
     * them: without this they fell straight into the C64's key ring, which
     * is a printed promise the machine does not keep. §7.5's table is the
     * other half of the same question and lists the three the target's BIOS
     * cannot DELIVER - Alt+F12, Alt+Insert, Alt+Delete - for which the menu
     * item stays the route.
     *
     * They go to os88_oncmd, which is the SAME helper the menu picks reach,
     * so a chord and a menu pick cannot drift apart; and an Alt chord is as
     * rare as the menu command it stands for, so the overlay call it makes is
     * on the right side of SPEC.md 73.14's frequency split. BIOS Alt+letter
     * is ascii 0 with the letter's own make code, and Alt+F9 is 0x70. */
    if (ascii == 0) {
        int mm = -1, mi = 0;
        if (scan == 0x1E) { mm = C64_M_FILE; mi = C64_I_ATTACH; }    /* A */
        else if (scan == 0x70) { mm = C64_M_FILE; mi = C64_I_RESETCPU; }
        else if (scan == 0x10) { mm = C64_M_FILE; mi = C64_I_EXIT; }  /* Q */
        else if (scan == 0x11) { mm = C64_M_PREF; mi = C64_I_WARP; }  /* W */
        else if (scan == 0x19) {                                      /* P */
            /* ...AND ALT+SHIFT+P IS A DIFFERENT ITEM. The BIOS hands
             * Alt+Shift+P the same ascii/scan pair as Alt+P, so without this
             * the chord §11.1's live table advertises for Advance frame
             * RESUMED a paused machine - the exact opposite of VICE, and on
             * the only state (paused) in which VICE's Alt+Shift+P advances
             * anything. The map is already armed, once, in os88_main. */
            mm = C64_M_PREF;
            mi = (os88_key_down(KSC_LSHIFT) || os88_key_down(KSC_RSHIFT))
                 ? C64_I_ADVANCE : C64_I_PAUSE;
            /* ...AND A CHORD DOES NOT REACH A GREYED ITEM. The kernel never
             * dispatches a disabled item, and a chord that walks round that
             * lands in c64_advance_frame's `c64_state != C64_ST_RUN` guard
             * and returns in SILENCE - the one shape SPEC.md 47 forbids,
             * reached by the chord instead of by the pick. It is reachable:
             * on a ROM-less disk or a jammed machine c64_menu_state greys
             * this item, and the FACT is already the permanent line on the
             * status row, so doing nothing here says nothing new. */
            if (mi == C64_I_ADVANCE
                && c64_pref_items[C64_I_ADVANCE][0] == OS88_MENU_DIS)
                return;
        }
        else if (scan == 0x24) { mm = C64_M_PREF; mi = C64_I_SWAPJOY; } /* J */
        /* ...AND THE TWO §7.5 SAYS THE TARGET'S BIOS CANNOT DELIVER, WHICH IS
         * A REASON TO KEEP THE MENU ITEM AND NOT A REASON TO LEAVE THE CHORD
         * OUT. Alt+Delete is scan 0xA3 and Alt+Insert 0xA2 - the enhanced
         * codes an AT `int 16h AH=0` drops and SeaBIOS passes - and neither
         * can be confused with the C64 keys on the same caps: a bare Del is
         * scan 0x53 and a bare Ins 0x52, so the two codes below are reachable
         * by nothing else. `int 16h AH=0` is where every key this package
         * sees comes from: kernel/ui.inc:84-95 is ui_task's own AH=01h peek
         * and AH=00h fetch, and W_ONKEY is dispatched from the same pass. */
        else if (scan == 0xA3) { mm = C64_M_EDIT; mi = C64_I_COPY; }
        else if (scan == 0xA2) { mm = C64_M_EDIT; mi = C64_I_PASTE; }
        /* ...AND NEITHER CHORD REACHES A GREYED ITEM, which is the same rule
         * the Alt+Shift+P guard above states: the kernel never dispatches a
         * disabled item, so a chord that walked round the greying would be
         * the silent no-op SPEC.md 47 forbids - a Paste with no machine
         * queueing bytes nothing will type, a Copy with no machine replacing
         * the system clipboard with the factory RAM pattern. The fact that
         * greys them is already the permanent line on the status row. */
        if (mm == C64_M_EDIT && c64_edit_items[mi][0] == OS88_MENU_DIS)
            return;
        if (mm >= 0) {
            os88_oncmd(mi, mm, win);
            return;
        }
    }
    c64_key_push(ascii, scan);
    c64_kick = 1;
    os88_wm_wake(win);
}

/* os88_onclick - a click KICKS the slice driver, so a wake that the full
 * event ring refused cannot park a running machine until the next key or
 * paint (SPEC.md 74.1). */
void os88_onclick(int x, int y, void *win)
{
    if (c64_abt) {
        /* THE PANEL'S CLOSE IS DAMAGE, NOT A REPAINT (12) - and the first
         * draft said so in this comment and then called c64_sh_inval(),
         * which forces all 25 rows, the border and the status row: ~271 ms,
         * four host ticks, for a panel covering thirteen of them.
         * c64_blank_rect was written for exactly this and the panel's rect is
         * already kept for the hit test. */
        /* AND THE CLIP HAS TO BE ARMED, because this is W_ONCLICK and not
         * W_PAINT. The kernel arms a clip region for a paint only (SPEC.md
         * 11.3), so the fill, the up-to-25 blits and the status row below
         * would otherwise land on top of whatever is covering this window -
         * and c64_flush would then set c64_sh_ok, recording a shadow for
         * pixels we never owned, so the span compare would answer "nothing
         * changed" for the rest of the session. os88_onwake gets this right
         * two callbacks down; apps/runcpm/rcabout.c:170-177 is the same
         * close with the same bracket and the same sh_inval fallback for
         * "nothing of us shows". */
        c64_abt = 0;
        if (os88_wm_clip_set(win) == 0) {
            c64_blank_rect(win, c64_abt_x, c64_abt_y,
                           c64_abt_x + c64_abt_w - 1,
                           c64_abt_y + c64_abt_h - 1);
            c64_flush(win);
        } else {
            c64_sh_inval();                 /* nothing of us is on the glass:
                                             * the next expose repaints */
        }
    }
    c64_kick = 1;
    os88_wm_wake(win);
}

/* os88_oncmd - the five menus (11.1). Two compares and then an overlay call:
 * the command BODIES are in c64cmd.c and out of the resident image, because
 * the split is by frequency and a menu command runs once (SPEC.md 73.14). */
void os88_oncmd(int item, int menu, void *win)
{
    if (menu == C64_M_HELP && item == C64_I_ABOUT) {
        os88_about(win);
        return;
    }
    /* FILE > EXIT EMULATOR IS ANSWERED HERE AND NOT IN THE MODULE, because it
     * is the one command that must work on a disk whose C64.OVL is missing:
     * it is a latch and nothing else (75.2, os88_onwake), and routing it
     * through the fence below would leave a machine the user cannot close by
     * its menu on exactly the disk where every other command already refuses. */
    if (menu == C64_M_FILE && item == C64_I_EXIT) {
        c64_exit_req = 1;
        c64_kick = 1;
        os88_wm_wake(win);
        return;
    }
    if (!c64_ovl_ready(win))
        return;                             /* the module is not resident and
                                             * this is the LOCK: the retry is
                                             * the wake's (c64_ovl_res) */
    if (!ovl_cmd(menu, item, win)) {
        /* A WRAPPER'S 0 IS THE RUNTIME REFUSING THE LOAD, and it must be
         * SAID. Every body in c64cmd.c returns 1, so a 0 here never came from
         * one of them: it is no C64.OVL on the disk, a stale module or no
         * heap. The runtime toasts it; §9.8's both-routes rule wants it on
         * the status row as well, because a toast under a fullscreen window
         * is under the bar the user cannot see (§13.3). */
        c64_say("Unable to load C64.OVL.");
        return;
    }
    c64_kick = 1;
    os88_wm_wake(win);
}

/* os88_about - the kernel's own About item and Help > About VICE... open the
 * same panel (12). */
void os88_about(void *win)
{
    /* THE LATCH IS THE STATUS. ovl_about_show answers 0 for a refused overlay
     * load, which c64cmd.c's header calls a normal path - and latching
     * c64_abt = 1 over it left os88_onwake returning early with nothing on
     * the glass, os88_paint holding rows for a panel that does not exist, and
     * the click that clears it running c64_blank_rect over a rect that was
     * never assigned. The machine looked frozen. */
    /* AND THE CLIP IS ARMED FIRST. This is W_ONCMD or the name menu's About
     * item, not W_PAINT: the kernel arms a clip region for a paint and for
     * nothing else (SPEC.md 11.3), so the panel's fill, two frames and ten
     * runs would otherwise land on top of whatever is covering this window.
     * A refusal means nothing of us is on the glass, and then there is
     * nothing to open: no drawing, and no latch to leave os88_onwake parked
     * on. The clip dies at the kernel's own gfx_unlock, so there is nothing
     * to undo. */
    if (os88_wm_clip_set(win) != 0)
        return;
    if (!c64_ovl_ready(win))
        return;                             /* ...the same fence: this is the
                                             * name menu's About item or
                                             * W_ONCMD, both under the lock */
    c64_abt = ovl_about_show(win);
    if (c64_abt)
        c64_sound_stop();                   /* the panel PAUSES the machine
                                             * (os88_onwake returns on c64_abt),
                                             * so the last note must not go on
                                             * sounding behind it */
    if (!c64_abt)
        c64_say("Unable to load C64.OVL.");  /* 13.3, both routes */
}

/* os88_onfile - the Standard File dialog's answer for File > Smart attach...
 * RESIDENT, because the runtime reaches a callback by a near offset (13.3):
 * it refuses by SIZE before the disk is touched, then calls an already-loaded
 * ovl_ helper to do the work (11.3). */
void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win)
{
    if (mode != OS88_FDLG_OPEN || name == 0 || name[0] == 0)
        return;
    if (size_hi != 0 || size_lo > 0xFFFDu) {
        c64_say("PRG over 65533 bytes.");
        return;
    }
    /* THE ANSWER IS READ, like every other ovl_ call site. Every refusal
     * ovl_load_prg has of its own returns 1 and has already said itself on
     * the row, so a 0 here is the RUNTIME refusing the load - no C64.OVL, a
     * stale module, no heap - and §9.8's both-routes rule wants that on the
     * status row and not only in a toast under a fullscreen window. */
    if (!c64_ovl_ready(win))
        return;                             /* the file dialog's answer is a
                                             * callback under the lock too */
    if (!ovl_load_prg(name, size_lo))
        c64_say("Unable to load C64.OVL.");
    c64_kick = 1;
    os88_wm_wake(win);
}

/* ==========================================================================
 * THE SPEED WIDGET'S ARITHMETIC (C64-SPEC §10.2)
 *
 * `% cpu` is EMULATED CYCLES divided by 985,248 - what one second of a real
 * PAL 6510 is - and `fps` is EMULATED VIC FRAMES, not flushes and not host
 * frames: at 100 %% it reads 50.1, because 19,656 cycles is a PAL frame.
 * Both come off two-word counters folded once a second (Decision 19), and the
 * division that a 16-bit C cannot express is one `div` in c64mem.inc.
 * ========================================================================*/
static void c64_speed_fold(void)
{
    unsigned t, el, dlo, dhi, fr, rawu;
    int raw;

    t = os88_ticks();
    el = t - c64_sp_tick;
    if (el < 18)                            /* ~1 s at 18.2 Hz */
        return;
    if (el > 182) {                         /* the wakes stopped for ten
                                             * seconds: this window measures
                                             * nothing, so it is restarted and
                                             * the figures stand */
        c64_sp_tick = t;
        c64_sp_lo = c64_cyc_lo;
        c64_sp_hi = c64_cyc_hi;
        c64_sp_fr = c64_frames_lo;
        return;
    }
    dlo = (c64_cyc_lo - c64_sp_lo) & 0xFFFFu;
    dhi = (c64_cyc_hi - c64_sp_hi) & 0xFFFFu;
    if (c64_cyc_lo < c64_sp_lo)
        dhi = (dhi - 1) & 0xFFFFu;
    fr = (c64_frames_lo - c64_sp_fr) & 0xFFFFu;
    c64_sp_tick = t;
    c64_sp_lo = c64_cyc_lo;
    c64_sp_hi = c64_cyc_hi;
    c64_sp_fr = c64_frames_lo;

    /* 985,248 cycles a second over 18.2 host ticks is 5,413.45 cycles a
     * tick-hundredth, so `cycles / 5413` scaled by 10/elapsed IS the per cent
     * - exactly, and with no float anywhere. The frame rate is the same shape
     * one step shorter: emulated VIC frames * 182 / elapsed is tenths of a
     * frame a second, and at 100 %% of a PAL machine it reads 50.1. */
    rawu = c64_div32(dhi, dlo, 5413);
    if (rawu > (unsigned)30000)
        rawu = 30000;
    raw = (int)rawu;                        /* THE CLAMP IS BEFORE THE CAST,
                                             * and that is the whole of it:
                                             * c64_div32 answers up to $FFFF,
                                             * so a quotient of 32,768 was
                                             * already NEGATIVE by the time
                                             * `raw > 30000` tested it, sailed
                                             * past, and came back through
                                             * c64_muldiv as a c64_pct that
                                             * c64_st_num clamps to a flat
                                             * `      0% cpu` beside a live
                                             * fps - the pair of numbers that
                                             * could not both be true, one
                                             * cast earlier. `raw` is a 16-bit
                                             * int, and the
                                             * scaling below is c64_muldiv's
                                             * 32-bit product, so the only cap
                                             * needed is the one that keeps
                                             * this variable positive. It is
                                             * not a decorative number: under
                                             * QEMU this core runs at some
                                             * THOUSANDS of per cent and a cap
                                             * of 3,200 clipped the honest
                                             * figure to a wrong one */
    c64_pct = (int)c64_muldiv((unsigned)raw, 10, el);
    /* AND THE SAME CLAMP, BEFORE THE SAME CAST. c64_muldiv answers $FFFF on
     * overflow (c64mem.inc:266) and the cast makes that -1, which c64_st_num
     * prints as a flat `     0.0 fps` beside a live `% cpu` - the pair of
     * numbers that could not both be true, one field to the right of where it
     * was fixed. It catches the overflow answer and any honest quotient past
     * 32,767 while the value is still unsigned, which is the rule the line
     * above already follows. */
    rawu = c64_muldiv(fr, 182, el);
    if (rawu > (unsigned)30000)
        rawu = 30000;
    c64_fps10 = (int)rawu;
}

/* c64_jam - a KIL opcode: the emulated machine ran off into something that is
 * not code, and the ADDRESS is the whole of the message. c64_m.pc points AT
 * the opcode (4.5).
 *
 * THE LINE IS VICE'S, NOT OURS. src/maincpu.c:612 formats
 * `"   " CPU_STR ": JAM at $%04X   "` and CPU_STR is `Main CPU`
 * (src/6510core.c:45); VICE's three leading and trailing spaces are padding
 * for the D'OH! dialog it shows the string in, and are dropped here because
 * this row is not that dialog. 22 glyphs, against a 42-cell row. The first
 * draft typed `CPU jammed at $E5CF.` from memory when the reference had a
 * string to read - LESSONS.md 1.
 *
 * AND THE ROW GOES ON SAYING IT. c64_say expires after five seconds; a jammed
 * machine is a PERMANENT condition, so C64_ST_JAM is a permanent row state in
 * c64_status beside `C64.ROM missing` (§1.4's rule, §10.1). Without that the
 * glass showed a dead machine and an idle one identically
 * (build/port-shots/wave2-05-jam.png).
 *
 * AND IT DOES NOT GO THROUGH c64_say, WHICH DREW IT TWICE. c64_say put the
 * SAME 22 glyphs up as a five-second message (msg == 1); five seconds later
 * the deadline cleared it, msg became 3, `msg != c64_st_shown` forced the
 * `full` path, and c64_status filled the row black and re-lettered the
 * IDENTICAL line at the IDENTICAL place - 1 fill + 1 font_run + 22 cells,
 * ~21 ms that changed not one pixel, and on the glass a row that blanks and
 * re-letters five seconds after the event with nothing having happened
 * (PERFORMANCE.md rule 2's erase-then-letter, in the one place it is free to
 * avoid). So this raises the three things c64_say does that a permanent row
 * state actually needs, and skips the message. */
/* c64_sound_stop - THE ONE PLACE A STOPPED MACHINE GOES QUIET (§11.4).
 *
 * The emulated SID is played as ONE square tone with duration 0 -
 * os88_snd_tone(hz, 0, prio), which SPEC.md 34 holds until something takes it
 * down - and the only thing that ever took it down was the guest closing the
 * gate. So every way of STOPPING the machine left the last note sounding for
 * ever: Alt+P, a JAM, a reset, and the About panel, which pauses the machine
 * and owns the glass. VICE does the opposite at each of those points, and at
 * warp too (sound_suspend on vsync's warp arm, src/vsync.c:181 and
 * src/sound.c:1819 - a machine running at 3,000 % has nothing meaningful to
 * play).
 *
 * AND IT RE-ARMS RATHER THAN REMEMBERING. c64_sid_dirty is the latch that says
 * "the SID's audible state has not reached the speaker", raised by a write to
 * $D400-$D41C; raising it here means the wake re-reads the CURRENT registers
 * on the way out of the stop and plays whatever the machine is actually
 * holding, which is right whether the guest changed them while stopped or not.
 * Keeping a copy of the note to restore would be a second model of the SID. */
static void c64_sound_stop(void)
{
    if (c64_snd_tone)
        os88_snd_tone(0, 0, 0x40);
    c64_sid_dirty = 1;                      /* the resume re-reads the SID */
    c64_sid_tries = 0;
}

static void c64_jam(void)
{
    c64_state = C64_ST_JAM;
    c64_sound_stop();                       /* a dead 6510 holds no note */
    os88_strcpy(c64_jamline, "Main CPU: JAM at $", 19);
    c64_hex4(c64_jamline + 18, c64_m.pc);
    c64_jamline[22] = 0;
    c64_dirty_any = 1;                      /* ...or the wake's flush gate
                                             * never opens and the row keeps
                                             * saying what an idle machine
                                             * says (the window stays up, 4.5) */
    c64_st_lok = 0;                         /* the row's permanent state has
                                             * changed under it - and the jam
                                             * line is 22 cells, so like every
                                             * short message it leaves the
                                             * widgets right of cell 24 alone
                                             * (c64scr.c) */
    os88_toast(c64_jamline, 0);             /* §9.8's second route, which is
                                             * the half c64_say was carrying */
    c64_menu_state();                       /* ...and Preferences > Advance
                                             * frame greys itself: there is no
                                             * machine left to advance (§47) */
}

/* c64_slice - one run of the core, broken at every device event, for `left`
 * cycles of wall budget. VICE's alarm model (4.4): `n` is never a quantum, it
 * is the smaller of what is left and the cycles to the nearest of the two
 * CIAs' timers, the raster compare, the frame end and the TOD tick, so every
 * device phase is retained across slices. Answers what was NOT run, so a
 * caller can tell an exhausted slice from one a JAM cut short. */
static int c64_slice(int left)
{
    int n, d, r, ran;

    while (left > 0) {
        d = c64_alarm_next();
        n = (d < left) ? d : left;
        c64_in_run = 1;
        r = c64_run((unsigned)n);
        c64_in_run = 0;
        ran = n - (int)c64_m.cnt;           /* cnt is what was NOT spent, and
                                             * is negative by up to one
                                             * instruction's cost (4.2) */
        if (ran < 1)
            ran = 1;
        c64_advance(ran);
        left -= ran;
        if (r == C64_RUN_JAM) {
            c64_jam();
            break;
        }
    }
    return left;
}

/* c64_advance_frame - Preferences > Advance frame (§11.1). It is LIVE as of
 * wave 2, and it was greyed before with the fact that greyed it: "there is no
 * raster accumulator until the alarm model lands with the core". The alarm
 * model landed IN THIS WAVE - c64_frame_cyc is that accumulator and
 * C64_PAL_FRAME is the frame end - so the fact stopped being true, and
 * SPEC.md 47 does not let a greying outlive its reason.
 *
 * IT PAUSES A RUNNING MACHINE AND ADVANCES ONLY A PAUSED ONE, WHICH IS WHAT
 * VICE'S ITEM DOES. src/arch/gtk3/actions-speed.c:72-80 (identically
 * src/arch/gtk3/ui.c:2735-2743) is the whole action:
 *
 *     if (ui_pause_active()) { vsyncarch_advance_frame(); }
 *     else                   { ui_pause_enable(); }
 *
 * and vsyncarch_advance_frame (src/arch/gtk3/vsyncarch.c:56-60) is
 * `ui_pause_disable(); pause_pending = 1;` with vsyncarch_postsync re-pausing
 * at the next frame end. The first draft set c64_adv unconditionally, so from
 * a RUNNING machine this port ran 19,656 emulated cycles and then paused -
 * invented semantics for the one live item this wave added, and the one whose
 * body had not been transcribed (LESSONS.md 1).
 *
 * IT RAISES A REQUEST AND RUNS NOTHING. os88_oncmd is dispatched UNDER THE
 * GFX LOCK, and a PAL frame is 19,656 emulated cycles: on the target, where
 * this core runs at a few per cent, that is a fraction of a SECOND of held
 * lock with the whole desktop stopped behind it (LESSONS.md 6, "no C between
 * gfx_lock and gfx_unlock that is not bounded by a count you can state"). The
 * slice driver already runs on the UI task with the lock not held, in wall
 * slices it sizes itself; this asks it for one frame's worth. */
static void c64_advance_frame(void)
{
    if (c64_state != C64_ST_RUN)
        return;                             /* UNREACHABLE FROM THE MENU, and
                                             * that is the point: with no
                                             * machine running the item is
                                             * GREYED by c64_menu_state (§47 -
                                             * a live item that is a silent
                                             * no-op is the one shape 47
                                             * forbids), and the fact that
                                             * greys it is already the
                                             * permanent line on the status
                                             * row - `C64.ROM missing - see
                                             * README.TXT` or the JAM line.
                                             * os88_onkey's chord can still
                                             * arrive here, so the guard
                                             * stays */
    if (!c64_pause) {
        c64_pause = 1;                      /* VICE's else-branch, exactly:
                                             * from a running machine the item
                                             * only PAUSES and advances
                                             * nothing */
        c64_sound_stop();
        c64_menu_state();
        c64_say("Paused.");
        return;
    }
    c64_adv = 1;
}

/* c64_reset_service - File > Reset machine CPU and File > Power cycle machine,
 * RUN FROM THE WAKE (c64cmd.c's latch).
 *
 * The difference between the two is the RAM, and this port keeps it: the power
 * cycle fills RAM the way a cold machine comes up and the CPU reset does not.
 * AND "THE WAY A COLD MACHINE COMES UP" IS NOT ZEROS - VICE gives the C64 the
 * factory pattern at src/ram.c:169-177, 00 00 00 00 FF FF FF FF offset by two
 * bytes and inverted every 16K, and c64_ram_pattern is that arithmetic. A zero
 * fill is harmless while there is no core and wrong the moment there is one:
 * the KERNAL's RAM test and any program that reads uninitialised memory both
 * see it.
 *
 * IT IS HERE AND NOT IN THE COMMAND because the fill is 64KB - about a quarter
 * of a second on the target - and os88_oncmd holds the desktop's gfx lock.
 * Nothing about the result changes: the 6510 advances only inside a wake and
 * this runs at the top of one, so the machine being reset is the machine the
 * user was looking at. */
static void c64_reset_service(void)
{
    int kind = c64_reset_req;

    c64_reset_req = 0;
    if (kind == 2)
        c64_ram_pattern();
    c64_paste_stop();                       /* kbdbuf_abort (src/kbdbuf.c:312-
                                             * 320, from machine_reset at
                                             * src/machine.c:262) empties the
                                             * queue: without it the previous
                                             * machine's paste types itself
                                             * into the new one. VICE's abort
                                             * is conditional on
                                             * kbd_buf_cmdline, which has no
                                             * equivalent here */
    /* ...and a Copy or Paste queued behind this one dies with the machine
     * that asked for it. BOTH flags, and c64_paste_stop() above does not
     * cover the second: that empties VICE's own key queue, where this is the
     * REQUEST the menu left for the wake to spend (c64cmd.c:294-296,
     * c64kbd.c:999-1015).
     *
     * The two statements are laid out plainly rather than aligned beside a
     * comment, because for one commit they were not: `c64_paste_req = 0;` sat
     * INSIDE the block comment the line above it opened, so a Paste queued
     * behind a reset outlived the machine that asked for it - which is
     * exactly what these two lines exist to prevent. It compiled, and nothing
     * but this comment said the pair belonged together. tests/unit/t_swallow.py
     * is the gate that now catches the shape. */
    c64_copy_req = 0;
    c64_paste_req = 0;
    c64_reset_regs();
    c64_lum_update();
    c64_frame_regs();
    c64_reset_cpu();                        /* the 6510 out of reset: I set,
                                             * PC from $FFFC under the KERNAL
                                             * (11.1) - and the KERNAL draws
                                             * its own boot screen from here */
    /* ...AND THE MACHINE COMES OUT OF PAUSE, WHICH IS VICE'S OWN ORDER:
     * machine_reset_action calls ui_pause_disable() straight after
     * machine_trigger_reset() (src/arch/gtk3/actions-machine.c:121). Without
     * it Reset on a paused machine reset the 6510 and then ran nothing - no
     * boot screen, Preferences > Pause emulation still checked, the `P` lamp
     * still standing - and the only clue was the item saying so. */
    c64_pause = 0;
    c64_adv = 0;
    c64_sound_stop();                       /* ...and the note the old machine
                                             * was holding does not survive it */
    c64_state = C64_ST_RUN;
    /* AND THE MENU IS RE-SPELLED, BECAUSE THIS IS A PATH OUT OF C64_ST_JAM.
     * c64_jam() greys Preferences > Advance frame - there is no machine left
     * to advance (SPEC.md 47) - and the FACT that greys it is the permanent
     * `Main CPU: JAM at $XXXX` line on the status row. A reset clears the
     * state, so c64_status stops drawing that line at once; without this call
     * the greying outlived the fact by the rest of the session, and then
     * un-greyed itself at random, because the only other callers of
     * c64_menu_state are the Warp/Pause/Swap/Fullscreen latches - so whether
     * Advance frame worked depended on which UNRELATED menu item you had last
     * picked. It writes item pointers and calls nothing that draws, so it is
     * as legal here as it was under the lock. */
    c64_menu_state();
    /* AND NOT c64_sh_inval(). Nothing covered the glass across a reset, so
     * the shadow is still true, and c64_dirty_all is the RECOMPOSE. sh_inval
     * is the FORCE, and forcing switches off the frame compare that exists to
     * answer "the picture did not change, draw nothing": a reset from the
     * boot screen back to the boot screen is exactly that case, and it cost
     * 25 forced blits, ~266 ms, four host ticks. */
    c64_dirty_all();
    c64_dirty_any = 1;
}

/* c64_wants_wake - SPEC.md 74.1's rule in ONE place: "a handler re-posts
 * itself only while it has work - a wake round trip is at least one task
 * switch (693 us), so a handler that always re-posts spins the UI task at
 * ~1,400 wakes a second". apps/runcpm/runcpm.c:847 is the precedent.
 *
 * THE PAUSE IS THE HALF THAT WAS MISSING. Alt+P sets c64_pause and NOT
 * c64_state, so a paused machine is still C64_ST_RUN: it took the empty
 * branch below, ran nothing, drew nothing - and re-posted anyway, at ~1 ms of
 * pure overhead a round trip (three os88_ticks, the scan, the far call and
 * the kernel's dispatch) for a machine the user deliberately stopped. Every
 * route out of pause re-arms the driver: Alt+P and the menu pick both land in
 * os88_oncmd, which kicks after ovl_cmd, and os88_onkey/os88_onclick kick.
 *
 * AND A MESSAGE IS NOT WORK. `c64_msg[0] != 0` used to be tested FIRST, above
 * both of those - so a machine that was paused, jammed or ROM-less re-posted
 * for the whole five-second life of every message with nothing to do inside
 * the wake but re-read os88_ticks(): ~1,400 round trips a second, ~7,000 of
 * them, about 4.8 seconds of the SHARED UI task (kernel/ui.inc dispatches
 * every window's events on it, so the desktop's own menu tracking and window
 * drags queue behind it) - and this wave made that newly reachable at the
 * worst moment, because Alt+P now genuinely stops the machine and answers
 * with c64_say("Paused."), and on a disk with no C64.ROM EVERY menu command
 * ends in a c64_say.
 *
 * So a machine the user stopped keeps its message until the next event, and
 * §10.1 says so. It cannot be gated on the flush's tick boundary instead -
 * that answers 0 at the moment the boundary has not arrived, no wake is
 * posted, and the message never comes down at all - and the alternative, a
 * worker sleeping a tick to post 18 wakes a second, buys five seconds of
 * expiry for a background task, a second spawn site and a merge with the
 * exit worker. The next event is a keystroke, a click, a menu pick or an
 * expose, and every one of them flushes: c64_flush examines the deadline
 * FIRST THING now (c64scr.c), so the widgets and §1.4's permanent line come
 * back on the next thing the user does. */
static int c64_wants_wake(void)
{
    if (c64_dirty_any)
        return 1;
    if (c64_state != C64_ST_RUN)
        return 0;
    return (!c64_pause || c64_adv) ? 1 : 0; /* ...and an outstanding Advance
                                             * frame is work, paused or not */
}

/* os88_onwake - THE slice driver (SPEC.md 74.1): on the UI task, with the
 * lock NOT held. One wall slice of the 6510 broken at every device event,
 * then a flush AT MOST ONCE PER HOST TICK (9.3) under the lock, then a
 * re-post only while there is work. */
void os88_onwake(void *win)
{
    unsigned t;
    unsigned t0;
    unsigned fr0 = 0;
    int left, n, d, spent, fe;

    if (c64_state == C64_ST_DEAD)
        return;

    /* FILE > EXIT EMULATOR (Alt+Q), SPENT HERE AND NOWHERE ELSE (SPEC.md
     * 75.2). os88_wm_close is the kernel's OWN close path - the same one the
     * close box takes: the negotiator retired, app_close_win, the dock strip
     * repainted, the instance and both claims freed.
     *
     * WHAT IT REPLACED, AND WHY THAT WAS WRONG. There used to be a worker:
     * os88_wm_destroy under the lock, then os88_task_alive outside it, which
     * is cword's File > Close idiom and what every C package here does. It
     * closes the WINDOW and it does not close the APP: wm_destroy frees the
     * record and nothing repaints the dock, so Alt+Q left a dead tile on the
     * strip that answered neither a click nor a double-click, four cycles out
     * of four, on both adapters - the exact failure the worker's own comment
     * claimed to have fixed. The close box was always clean because it goes
     * through app_close_win. There is one door, and this is it.
     *
     * IT IS SPENT FROM THE WAKE AND NOT FROM os88_oncmd. The slot RETURNS and
     * closes on the next UI pass (os88.h) - so it may be called under the
     * command's lock - but its contract is "call it and RETURN, do not draw
     * afterwards", and os88_oncmd's own tail draws: it kicks a wake, which
     * runs a slice and flushes into a window that is going away. Here the
     * return is the last thing that happens, C64_ST_DEAD stops the next wake
     * dead, and the kick os88_oncmd posted is what carries us here. */
    if (c64_exit_req) {
        c64_exit_req = 0;
        c64_state = C64_ST_DEAD;            /* nothing flushes into it again */
        os88_wm_close(win);
        return;                             /* ...and NOTHING after the call */
    }

    /* C64-SPEC §13.3'S FIRST `ovl_*` CALL, ON THE FIRST WAKE. The .OVL cannot
     * be resolved from os88_main - there is no instance yet - so this asks
     * once, for nothing, at the first moment there is one. Its refusal is the
     * fact that every menu command in this program is about to run into, and
     * it is printed where the user is looking rather than only toasted. */
    if (!c64_ovl_asked) {
        c64_ovl_asked = 1;
        c64_ovl_res = ovl_conv_init();
        if (!c64_ovl_res)
            c64_say("Unable to load C64.OVL.");
    }

    /* EDIT > COPY AND EDIT > PASTE, ABOVE THE ABT GATE AND ABOVE THE SLICE.
     * Their bodies are ~103 ms and a heap claim and os88_oncmd holds the
     * desktop's gfx lock, so the commands only latch (c64kbd.c's
     * c64_clip_service carries the argument). Here, no cycle has run since
     * the pick and no lock is held; above the gate, so a jammed, paused or
     * panel-up machine still services the copy of the screen in front of it. */
    if (c64_reset_req)
        c64_reset_service();
    if (c64_copy_req || c64_paste_req)
        c64_clip_service(c64_mbase);

    if (c64_abt)
        return;                             /* the panel is up: the machine is
                                             * paused and the glass is its */

    /* --- THE WALL SLICE (4.4). VICE's alarm model: run to the next device
     * event, service it, compute the next one. `n` is never a quantum - it is
     * the smaller of what is left of this wake's budget and the cycles to the
     * nearest of the two CIAs' timers, the raster compare, the frame end and
     * the TOD tick, so every device phase is retained across slices and the
     * floor is milliseconds rather than a whole emulated jiffy. */
    t0 = os88_ticks();
    left = 0;
    if (c64_state == C64_ST_RUN && (!c64_pause || c64_adv)) {
        c64_kbd_poll();                     /* ONCE PER WAKE (7.2's rule 2),
                                             * and every emulated CIA1 read in
                                             * this wake reads what it leaves */
        c64_paste_feed();                   /* ...and Edit > Paste's ten
                                             * characters, BEFORE the slice, so
                                             * the machine drinks them inside
                                             * this wake. It is kbdbuf_flush's
                                             * once-a-frame call moved onto
                                             * this OS's clock, and it puts
                                             * nothing in while $C6 is not 0 -
                                             * the KERNAL's own pace (7.7) */
        n = c64_budget;
        if (c64_adv) {
            /* Advance frame: never past the frame end, and never more than
             * one wall slice at a time either - the item is one frame of
             * emulated time, not a licence to hold the UI task for it */
            fr0 = c64_frames_lo;
            d = C64_PAL_FRAME - c64_frame_cyc;
            if (d < n)
                n = d;
        }
        left = c64_slice(n);                /* ...and what it did NOT run is
                                             * what tells an exhausted slice
                                             * from one a JAM cut short */
        if (c64_adv && (c64_frames_lo != fr0 || c64_state != C64_ST_RUN)) {
            c64_adv = 0;
            c64_pause = 1;                  /* ...and stop, which is the whole
                                             * point of the item */
            c64_sound_stop();
            c64_menu_state();
            c64_say("Advanced one frame.");
        }
        if (c64_sid_dirty) {
            /* ONE FAR CALL A WAKE, ON A CHANGE ONLY (11.4) - AND THE LATCH IS
             * CLEARED ONLY WHEN THE GRANT TOOK. os88_snd_tone answers -1
             * REFUSED (os88.h, SPEC.md 34.3: the grant is stamped and another
             * instance can hold it), and this cleared the latch BEFORE the
             * call and threw the answer away. c64_sid_dirty is raised in
             * exactly one place - a write to $D400-$D41C - so a single
             * refusal silenced the emulated SID until the guest next poked a
             * SID register, which for `set the frequency, gate on, leave it`
             * is never: the machine then played nothing for the rest of the
             * session and said nothing about it. The retry is BOUNDED - eight
             * wakes, then the latch is dropped and the next SID write re-arms
             * it - so a machine whose speaker is held for good does not ask
             * once a wake for ever, and the fact is said ONCE. */
            if (!c64_snd_tone) {
                /* ...AND A MACHINE WITH NO SQUARE VOICE IS A DIFFERENT FACT
                 * FROM A BUSY ONE, so it is a different sentence and it is
                 * not retried at all. SND_CAP_TONE is asked once in
                 * os88_main; every machine this OS boots answers it
                 * (kernel/snd.inc:804 sets the bit unconditionally), so this
                 * is a guard rather than a path a user will meet - and it is
                 * written, and driven by the harness's stub, because the
                 * alternative is a program that calls a slot it never
                 * established the machine has (SPEC.md 73.11). */
                c64_sid_dirty = 0;
                c64_sid_tries = 0;
                if (!c64_sid_said) {
                    c64_sid_said = 1;
                    c64_say("No square voice here - no SID sound.");
                }
            } else if (c64_sid_voice1() >= 0) {
                c64_sid_dirty = 0;
                c64_sid_tries = 0;
            } else if (++c64_sid_tries >= C64_SID_TRIES) {
                c64_sid_dirty = 0;
                c64_sid_tries = 0;
                if (!c64_sid_said) {
                    c64_sid_said = 1;
                    c64_say("The speaker is busy - no SID sound.");
                }
            }
        }
    }
    /* ...AND ONLY A GENUINELY EXHAUSTED SLICE ADAPTS. A slice that ended
     * early - paused, jammed - leaves the estimate alone: without that rule
     * ordinary idling walks the budget to its cap and the next busy slice is
     * a second of stalled UI task (LESSONS.md 13). */
    if (left <= 0 && c64_state == C64_ST_RUN && !c64_pause) {
        spent = (int)(os88_ticks() - t0);
        if (spent == 0) {
            c64_fastn++;
            if (c64_fastn >= 4) {
                /* ...AND THE DOUBLING IS CLAMPED, WHICH IT DID NOT USED TO
                 * HAVE TO BE. With one cap of 16,384 the test `< cap` made
                 * the last doubling land exactly on it; with warp's 30,000 it
                 * lands on 32,768, and `int` is SIXTEEN BITS here, so the
                 * budget arrives as -32,768 and the core expires before its
                 * first fetch - a warp that stops the machine dead. The
                 * harness found it; nothing on a glass would have. */
                int cap = c64_warp ? C64_SLICE_WARP : C64_SLICE_MAX;
                c64_fastn = 0;
                if (c64_budget < cap) {
                    c64_budget += c64_budget;
                    if (c64_budget > cap || c64_budget <= 0)
                        c64_budget = cap;
                }
            }
        } else {
            c64_fastn = 0;
            if (spent >= 2 && c64_budget > C64_SLICE_MIN)
                c64_budget = c64_budget / 2;
        }
    }
    c64_speed_fold();

    t = os88_ticks();
    if (c64_scr_rd(C64_SCR_ANY))
        c64_dirty_any = 1;                  /* the core wrote something (9.2) */
    /* A MESSAGE ON THE ROW IS A REASON TO FLUSH, because its DEADLINE is
     * examined inside the flush: with nothing else dirty the row would stay
     * up for ever (C64-SPEC §10.1's "the widgets come back"). A flush with
     * nothing dirty composes no rows and c64_status answers "nothing moved"
     * in zero drawing calls, so this costs the scan and no drawing. */
    /* ...AND THE SPEED WIDGET'S OWN DELTA IS PART OF THE GATE. c64_dirty_any
     * comes from C64_SCR_ANY, which the core sets on a RAM WRITE: a machine
     * that runs without writing RAM (an ML poll loop on $D012, a `WAIT`) left
     * the figures frozen at whatever they were when it last wrote, stating a
     * speed that is not the current one - and that is exactly when a user is
     * looking at the widget. It costs nothing when nothing moved: c64_status
     * answers "nothing on the row moved" in zero drawing calls. */
    /* ...AND WARP CAPS THE RATE AT VICE'S OWN 10 fps (vsync.c:339-340,
     * :634-656). It can only slow a flush down - the tier rate stands
     * wherever it is already slower - so the CPU_8086 tier, which flushes
     * every other tick, is unmoved by it (§4.4). */
    fe = c64_flush_every;
    if (c64_warp && fe < C64_WARP_FLUSH)
        fe = C64_WARP_FLUSH;
    if ((c64_dirty_any || c64_msg[0] != 0
         || c64_pct != c64_st_pct || c64_fps10 != c64_st_fps) &&
        (unsigned)(t - c64_last_flush) >= (unsigned)fe) {
        c64_last_flush = t;
        os88_gfx_lock();
        if (os88_wm_clip_set(win) == 0)
            c64_flush(win);
        os88_gfx_unlock();
    }
    /* ...AND A MESSAGE IS NOT A REASON TO ASK FOR ANOTHER WAKE. A running
     * machine already asks, so its messages expire on the ordinary flush
     * cadence; a machine the user stopped keeps its message until the next
     * event, because the alternative is ~1,400 round trips a second of the
     * shared UI task with nothing to do but re-read the clock (c64_wants_wake
     * above, C64-SPEC §10.1). */
    c64_kick = c64_wants_wake();
    if (c64_kick)
        os88_wm_wake(win);
}

/* ==========================================================================
 * os88_main - the entry (SPEC.md 20.2). Claims first, then the file, then the
 * window: launch is DEFINED BY THE CLAIMS AND THE FILE succeeding, not by a
 * free-KB figure, and every refusal quotes what was asked and what there is
 * (3.1) or names the file (1.4).
 * ========================================================================*/
static char c64_num[8];
static char c64_line[C64_MSGMAX];

static void c64_refuse_kb(const char *what)
{
    os88_strcpy(c64_line, what, sizeof(c64_line));
    os88_utoa(os88_mem_largest_kb(), c64_num);
    os88_strcpy(c64_line + os88_strlen(c64_line), c64_num, 7);
    os88_strcpy(c64_line + os88_strlen(c64_line), " KB", 4);
    os88_toast(c64_line, 0);
}

void *os88_main(void)
{
    static struct os88_video vid;
    void *win;
    int wh;

    c64_m.ramseg = os88_mem_claim(64);      /* the C64's RAM is its own
                                             * segment (3.1) */
    if (c64_m.ramseg == 0) {
        c64_refuse_kb("C64: 64KB? ");
        return 0;
    }
    /* THE ROM IS A PART AND IT IS ALREADY HERE (1.4, SPEC.md 20.12). crt0
     * called op_load before this function, which claimed the 20KB and read
     * the ROM into it - or refused the whole launch with a toast naming why,
     * before a sector was spent, because the part table it sized from was
     * already in the image the kernel had read. So there is no claim here, no
     * read here, and no way for the ROM to be absent: os88_part_seg cannot
     * answer 0 for a REQUIRED part after a successful op_load. */
    c64_m.romseg = os88_part_seg(C64_ROM_PART);
    /* THE FETCH BIAS IS SEGMENT ARITHMETIC AND IT CAN UNDERFLOW. A PC in the
     * KERNAL is fetched through `ES = romseg - $0E00` (c64cpu.inc 4.3), which
     * wraps - and reads somewhere else entirely - if the ROM claim's base is
     * below 57,344 bytes. It never is: the heap starts above the kernel's
     * ~111KB footprint (docs/KERNEL-MEMORY.md). This is the guard that says
     * so out loud rather than the assumption that does not. */
    if (c64_m.romseg < 0x0E00) {
        os88_mem_free(c64_m.ramseg);
        os88_toast("C64: the ROM claim is too low in memory", 0);
        return 0;
    }
    /* WHAT CAN THIS MACHINE'S SOUND HARDWARE DO (11.4)? Asked ONCE, here, and
     * tested rather than guessed (SPEC.md 73.11): §11.4 puts SID voice 1's
     * gate and frequency on OSAPI_SND_TONE, and a package that calls a slot
     * without establishing the capability first is guessing. */
    c64_snd_tone = (os88_snd_caps() & C64_SND_CAP_TONE) ? 1 : 0;
    c64_x2init();                           /* the pixel-doubling table, once */
    /* ...AND COPY'S AND PASTE'S TWO CONVERSION TABLES ARE *NOT* BUILT HERE.
     * They are ovl_conv_init's, on the first wake, because the code that
     * fills them runs exactly once and once-per-launch is SPEC.md 73.14's
     * `goes out` side. The .OVL cannot be resolved from os88_main anyway
     * (LESSONS.md 13): there is no instance yet to resolve a module for. The
     * TABLES stay resident and DS-relative, which is what makes it legal. */
    c64_tier_init();                        /* the flush rate, off os88_cpu()
                                             * and tests/c64band's numbers */
    c64_reset_regs();                       /* the VIC/CIA/SID register files */
    c64_lum_update();                       /* the 1bpp masks, off $D021 (9.6) */
    c64_ram_pattern();                      /* power-on RAM is VICE's factory
                                             * pattern and not zeros
                                             * (src/ram.c:169-177), and the
                                             * scratch is left alone (3.5) */
    c64_scratch_clear();
    c64_kbd_init();
    /* THE KEY-STATE MAP IS ARMED HERE AND NOWHERE ELSE (7.2's rule 1).
     * OSAPI_KEY_DOWN's FIRST call clears and arms the map and always answers
     * "up", so arming it from the first slice would erase the make os88_onkey
     * had already seen - the first key of the session, silently lost. The
     * answer is ignored on purpose. */
    os88_key_down(KSC_SPACE);
    c64_frame_regs();                       /* ...which writes the core's WATCH
                                             * RANGE, so the write window is
                                             * taken over the matrix from the
                                             * very first poke (9.2) */
    c64_reset_cpu();                        /* the 6510 out of reset, PC from
                                             * $FFFC under the KERNAL (11.1) */
    c64_state = C64_ST_RUN;                 /* ...and the KERNAL draws its own
                                             * boot screen from the first wake */
    c64_sp_tick = os88_ticks();
    c64_menu_state();                       /* the four check items' labels */
    c64_sh_inval();

    /* ASK THE ADAPTER, DO NOT ASSUME (os88.h). C64_CONT_H is 226 and that is
     * a 480-line number: on a 200-line CGA desktop the window cannot have it,
     * and authoring one that tall left wm_fit to clamp it - with the STATUS
     * ROW, which carries §1.4's permanent fact and every refusal, off the
     * bottom (build/port-shots/wave1-cga-launch.png). The window asks for
     * what the desktop can give; c64_flush anchors the status row to the
     * live bottom of whatever it got, and shows the rows that then fit. */
    os88_video(&vid);
    wh = C64_W_H;
    if (vid.dock_top > 0 && C64_W_Y + wh > vid.dock_top)
        wh = vid.dock_top - C64_W_Y;
    if (wh < OS88_TITLE_H + C64_STATH + 16)
        wh = OS88_TITLE_H + C64_STATH + 16;
    win = os88_wm_create(C64_W_X, C64_W_Y, C64_W_W, wh, c64_title);
    if (win == 0) {
        os88_mem_free(c64_m.ramseg);
        return 0;
    }
    c64_win = win;
    os88_wm_snap(win, 1);                   /* the content x on a cell
                                             * boundary: blit1 and gfx_scroll
                                             * both want it (9.4) */
    os88_wm_ownbg(win, 1);                  /* we paint every pixel */
    os88_menu_set(win, (struct os88_menuset *)&c64_menus);
    os88_about_set(win);
    os88_wm_onwake(win);                    /* the slice driver's entry (74.1) */
    /* ...AND THE FIRST WAKE HAS TO BE ASKED FOR. os88_wm_onwake INSTALLS the
     * handler; os88_wm_wake POSTS the kick (os88.h:585, :592), and os88_paint
     * only re-posts `if (c64_kick)`. Without this line the machine sat at its
     * reset vector with 0 % on the status row until the user pressed a key or
     * clicked - and then booted perfectly, which is what made it look like a
     * reset bug rather than a wake that was never started
     * (build/port-shots/wave2-03-launch.png). RUNCPM posts its first wake
     * from its own paint for the same reason. */
    c64_kick = 1;
    return win;                             /* the first paint kicks */
}
