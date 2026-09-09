/* ============================================================================
 * os8088 - apps/apple2/apple2.c    APPLE2: an Apple II Plus, written in C
 *
 * A windowed 48K Apple II Plus as an os8088 package: a 6502 in a 64KB heap
 * claim, Applesoft BASIC and the Autostart Monitor read from a ROM PART
 * inside APPLE2.O88, the II+'s four soft-switch display modes composed into
 * 1bpp bands and blitted into a window, and - from wave 5 - a foreign video
 * mode at full screen for colour.
 *
 * ----------------------------------------------------------------------------
 * LICENCE AND ATTRIBUTION (APPLE2-SPEC section 1.3)
 * ----------------------------------------------------------------------------
 * apps/apple2/a2cpu.inc is a DERIVED COPY of apps/c64/c64cpu.inc, which is
 * GPL-2-or-later by way of VICE. **apps/apple2/ is GPL-2-or-later**; the rest
 * of this tree is not, and apps/apple2/COPYING is the licence text the
 * licence requires to accompany copies.
 *
 *   VICE       (C) 1996-2025 the VICE team      GPL-2-or-later
 *   AppleWin   (C) the AppleWin authors        GPL-2-or-later
 *              (four holders over four ranges - COPYING carries the list)
 *   MII        (C) 2023 Michel Pollet           MIT
 *   apple2emu  (C) 2016-2017 Mark Allender      MIT
 *
 * Nothing of any reference tree's SOURCE is vendored (CONTRIBUTING.md 6):
 * what is carried is behaviour, tables and strings, and APPLE2-SPEC section 2
 * names the file every user-visible surface came from. The Apple II+ ROM
 * images are Copyright (C) Apple Computer, Inc.; they are FETCHED at a pin by
 * tools/getapple2rom.py and NEVER committed (section 1.4).
 *
 * THE CONTRACT IS docs/APPLE2-SPEC.md, not a section of SPEC.md, and this
 * file's comments cite it as "APPLE2-SPEC section N" - never with a bare
 * section mark, because tools/checkdocs.py resolves a bare one against
 * SPEC.md as well and an ambiguous citation is what that gate exists to
 * prevent. SPEC.md is always spelled out.
 *
 * ----------------------------------------------------------------------------
 * THE SHAPE (APPLE2-SPEC section 1.1, SPEC.md 74.1)
 * ----------------------------------------------------------------------------
 * The C64's, and RUNCPM's before it: no worker task and nothing blocking. The
 * machine runs on the UI task in wake-driven wall slices (OSAPI_WM_WAKE /
 * OSAPI_WM_ONWAKE), and an Apple II sitting at the `]` prompt costs nothing
 * until a key or a tick arrives. CC_HAS_WORKER is not declared: File > Quit
 * goes through OSAPI_WM_CLOSE.
 *
 * AND THERE IS NO ALARM SCHEDULER AT ALL (section 4.2), which is a real
 * simplification this port states rather than an omission: a bare II+ has no
 * timers and no interrupt source, so the wake loop is `r = a2_run(budget)`
 * with no min() and no advance.
 *
 * ----------------------------------------------------------------------------
 * WHAT WAVE 1 IS (docs/APPLE2-PORT-PLAN.md)
 * ----------------------------------------------------------------------------
 * The redraw path, built before there is anything to draw: the window and its
 * chrome, the ROM part, THE CHARGEN DECODE AND THE 7-BIT REVERSE TABLE
 * RESIDENT IN os88_main, the interleaved page-to-scan-line map, the whole
 * damage model including the flash phase, the 7-pixel text composer, the four
 * menus, the status row, and the whole harness kit.
 *
 * ----------------------------------------------------------------------------
 * WHAT WAVE 2 ADDED (docs/APPLE2-PORT-PLAN.md)
 * ----------------------------------------------------------------------------
 * The 6502, the Apple II memory model, the soft switches in both directions,
 * the II+ keyboard byte map and the reset line, both kinds. Wave 1's
 * a2_selftext() scaffolding is GONE: the Autostart Monitor writes the display
 * page itself now, which is what a screendump of this port is supposed to be a
 * photograph of.
 *
 * THE FIRST HONEST SIZE LINE IS QUOTED IN APPLE2-SPEC section 15.0 and in the
 * plan's wave-2 paragraph: image 26,706 + bss 10,394 = 37,100 resident of
 * 61,440, APPLE2.OVL 883, 8 resident shims, largest C frame 42 of 96. **That
 * is wave 2's figure and not this build's**: the SHIPPING line is section
 * 15.0.5's - image 39,042 + bss 14,344 = 53,386 resident of 61,440,
 * APPLE2.OVL 4,349, 38 resident shims, largest C frame 54 of 96, and the file
 * on disk 54,272 with the ROM part inside it.
 *
 * ----------------------------------------------------------------------------
 * WHAT WAVE 7 ADDED (docs/APPLE2-PORT-PLAN.md)
 * ----------------------------------------------------------------------------
 * The polish: WELCOME.BAS on all four disks and on `make allapps` (section
 * 16.2), `vm/xt-apple2` and `vm/286-apple2`, `tests/apple2part.py`, the
 * measured XT speed (section 16.4.1) - and colour's
 * price said once on the CPU_8086 tier
 * (section 13.1). Nothing in the machine changed.
 *
 * ...AND A WAVE OF ITS OWN TOOK THAT MEASUREMENT UP: **0.54% of a 1.02 MHz
 * Apple at the prompt and 0.64% in a BASIC loop**, the status row still
 * reading 0%. The adaptation is a DUTY-CYCLE CONTROLLER now (section 4.3.1,
 * and the declaration of a2_adn below carries the whole argument): the rule
 * it replaces needed four consecutive clean slices to double against one
 * tick-crossing to halve, and that asymmetry has a fixed point at 14 % of a
 * host tick, so on a 4.77 MHz 8088 the budget never left A2_SLICE_MIN. The
 * flush, not the slice, is where the rest of that machine's second goes.
 *
 * AND THE REVIEW ADDED ONE THING THAT IS NOT POLISH: `a2_fsx_main` had no
 * TIER PACING, so on the very machine the new colour message is about it
 * issued a 140 ms frame between every 256-cycle slice (APPLE2-SPEC section
 * 13.2). It carries `a2_flush`'s term now, at four ticks rather than two,
 * which is the ratio a colour row costs against a windowed one.
 * ==========================================================================*/

#include "os88.h"

/* --- the callbacks the shim declares (apple2.asm's CC_HAS_*) -------------- */
void *os88_main(void);
void  os88_paint(void *win);
void  os88_onkey(int ascii, int scan, void *win);
void  os88_onclick(int x, int y, void *win);
void  os88_oncmd(int item, int menu, void *win);
void  os88_about(void *win);
void  os88_onwake(void *win);
void  os88_ontimer(void *win);
void  os88_onfile(int mode, const char *name,
                  unsigned size_lo, unsigned size_hi, void *win);

/* ==========================================================================
 * THE MACHINE'S SHAPE
 * ========================================================================*/

/* The 6502's register file lives in a2cpu.inc's .bss as _a2_m, because the
 * core loads and stores it with no frame: this is the C's view of the same
 * bytes and the field order IS the layout (APPLE2-SPEC section 4.1). */
struct a2_mach {
    unsigned ramseg;                        /* the 64KB RAM claim */
    unsigned romseg;                        /* the ROM PART's base segment */
    unsigned pc;
    unsigned a, x, y, s, p;
    unsigned cnt;                           /* wall-slice cycles left */
    unsigned reason;                        /* A2_RUN_* of the last return */
};
extern struct a2_mach a2_m;

#define A2_RUN_SLICE 0
#define A2_RUN_JAM   1

/* --- a2mem.inc: the claim accessors and the movers (section 3.4) ---------- */
int  a2_rd(unsigned a);
int  a2_rd16(unsigned a);
void a2_wr(unsigned a, int v);
void a2_dirty(unsigned a);
int  a2_wrote(void);                        /* ONE byte, ONE read: did the
                                             * machine write anything since
                                             * the last flush? The wake asks
                                             * this before it asks anything
                                             * else (section 7.5) */
void a2_watch_set(unsigned lo, unsigned hi);
void a2_scratch_clear(void);
void a2_dirty_take(unsigned char *dst36);   /* the 32-byte page bitmap and the
                                             * write window's two words into
                                             * the package, and the scratch
                                             * reset, in ONE call: ~190 us
                                             * against ~50 near thunks */
int  a2_rom_rd(unsigned off);
void a2_chargen(void *dst, unsigned seg, unsigned off, unsigned n);
void a2_zfill(unsigned a, int v, unsigned n);
void a2_zcopy_in(unsigned a, const void *src, unsigned n);
void a2_zcopy_out(void *dst, unsigned a, unsigned n);
/* ...AND THE FAR-TO-FAR PAIR WAVE 4 ADDED (section 3.4, 6.5, 12): a heap
 * claim to and from the Apple's RAM with neither end a C pointer. Load
 * Program walks its file in a transient claim and moves the ACCEPTED program
 * in with the first; Save Program moves the program out with the second and
 * writes the claim; Edit > Copy composes into a claim of its own. */
void a2_zzcopy_in(unsigned a, unsigned seg, unsigned off, unsigned n);
void a2_zzcopy_out(unsigned seg, unsigned off, unsigned a, unsigned n);
int  a2_copy_row(unsigned dseg, unsigned doff, int n);   /* ONE row of Edit >
                                                          * Copy: the per-BYTE
                                                          * loop, in assembly,
                                                          * called 24 times */
void a2_zpower(unsigned a, unsigned n);     /* AppleWin's FF FF 00 00 power-on
                                             * pattern, as a MOVER: the C form
                                             * is 12,288 iterations of two near
                                             * calls, ~270 ms on the target
                                             * (section 4.5) */

unsigned a2_div32(unsigned hi, unsigned lo, unsigned d);
                                            /* (hi:lo) / d in ONE 32-bit
                                             * division, 0xFFFF on overflow -
                                             * the speaker's hertz, and the
                                             * whole reason it is assembly is
                                             * that ONE rounding and not two is
                                             * what the C64 measured at a whole
                                             * hertz near the floor (section
                                             * 8) */

/* --- a2cpu.inc: the core (section 4) -------------------------------------- */
int  a2_run(unsigned cycles);           /* -> A2_RUN_*, and a2_m.cnt is what
                                         * was NOT spent, so the caller's
                                         * `ran = asked - cnt` is exact */
void a2_cut(void);                      /* end the run in progress, exactly */
void a2_rebias(void);                   /* PC moved under the core's feet: no
                                         * instruction is fetched under a
                                         * stale bias */
int  a2_bread(unsigned a);              /* one byte through the Apple II
                                         * LADDER - the reset vector is in the
                                         * ROM, so a2_rd (RAM by construction)
                                         * cannot read it */
void a2_clk_set(unsigned v);            /* the emulated clock's base for the
                                         * run about to start: `clock +
                                         * budget` (section 5.1) */
int  a2_now(void);                      /* ...and the clock itself, EXACT
                                         * INSIDE A RUN, which is where the
                                         * paddle one-shots are armed and
                                         * read */

/* --- a2band.inc: the composers (section 7.3) ------------------------------ */
void a2_band_text(unsigned char *dst, int g0, int g1,
                  unsigned mseg, unsigned moff, int fmask);
/* ...AND THE OTHER TWO OF THE ONE CLASS (APPLE2-SPEC section 7.3). All three
 * take (dst, g0, g1, mseg, moff) in that order and share a2_pack, the seven-bit
 * shift accumulator, LITERALLY; only text has a sixth argument, because only
 * text has a flash phase. `moff` is the row's forty TEXT-page bytes for lo-res
 * and SCAN LINE 0 of the row group for hi-res, whose other seven lines are
 * $400 apart - the interleave the composer walks itself. */
void a2_band_lores(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff);
void a2_band_hires(unsigned char *dst, int g0, int g1,
                   unsigned mseg, unsigned moff, int s0, int nlines);
int  a2_rowflash(unsigned mseg, unsigned moff, int n);
int  a2_rowspan(const unsigned char *a, const unsigned char *b, int n);
void a2_rowcopy(unsigned char *dst, const unsigned char *src, int n);
unsigned a2_rowsig(unsigned mseg, unsigned moff, int n);
unsigned a2_scan0(unsigned seg, unsigned off, unsigned n);  /* the linked-line
                                                             * walk's inner
                                                             * loop (section
                                                             * 12); $FFFF =
                                                             * no zero byte */
void a2_x2init(void);
void a2_band_x2(unsigned char *dst, const unsigned char *src, int nbytes,
                int rows);

/* --- a2fsx.inc: the foreign video mode (section 13) ------------------------
 * The bracket's own five. a2_fsx_row composes `ncells` cells of ONE scan line
 * starting at cell `cell0` - MII's artifact rule for hi-res, its lo-res CLUT,
 * white on black for text; the range is a2_flush's own group span one
 * geometry along, which is what keeps a COUT's compose to the cells a COUT
 * could have moved - and a2_fsx_put is the span compare one geometry along: it
 * answers 0 for a line that has not moved and writes only the differing run
 * when it has, to the framebuffer AND to the foreign-frame shadow.
 *
 * a2_fsx_key IS THE BRACKET'S WHOLE INPUT PATH and is assembly for the one
 * reason a package ever writes assembly for a slot: there is no int 16h in
 * the C SDK and a bracket may not use the event ladder (SPEC.md 53.1). */
void a2_fsx_row(unsigned char *dst, int mode, unsigned mseg, unsigned moff,
                int line, int fmask, int cell0, int ncells);
int  a2_fsx_put(unsigned fbseg, unsigned fboff, unsigned shseg,
                unsigned shoff, const unsigned char *src, int n);
void a2_fsx_zero(unsigned seg, unsigned off, unsigned n);
void a2_fsx_init(void);                 /* MII's artifact rule, flattened */
void a2_fsx_dac(void);                  /* ...and its palette into the DAC */
unsigned a2_fsx_key(void);              /* the polled int 16h: 0xFFFF for
                                         * nothing, else int 16h's own AX -
                                         * (scan << 8) | ascii.
                                         *
                                         * IT IS `unsigned` AND THE SENTINEL
                                         * IS 0xFFFF, WHICH IS ONE DEFECT AND
                                         * NOT A STYLE. `int` is 16 bits here,
                                         * so an AX whose SCAN CODE has bit 7
                                         * set - Alt+Enter's enhanced 0xA6,
                                         * Alt+0/-/= at 0x81/0x82/0x83 - is a
                                         * NEGATIVE int, and a caller testing
                                         * `>= 0` for "a key arrived" throws
                                         * every one of them away before
                                         * looking at it. The shim already
                                         * answers 0xFFFF in AX (a2fsx.inc);
                                         * what was wrong was the C's reading
                                         * of it, and the harness could never
                                         * see it because a host `int` is 32
                                         * bits and 0xA600 is positive there. */

/* THE DECODED CHARACTER GENERATOR, and it is an ordinary global rather than a
 * static because nasm has to see the label: a2band.inc's phase B indexes
 * `_a2_chr` directly (APPLE2-SPEC section 7.3). 64 glyphs of 8 rows; the
 * inverse and flashing forms are the composer's per-cell XOR mask, which is
 * how 512 bytes cover the ROM's 128 distinct bitmaps. */
unsigned char a2_chr[512];

/* --- the core's scratch, as the C reads it (section 3.3) ------------------ */
#define A2_SCR_BASE  0xCF00
#define A2_SCR_DIRTY 0x00                   /* 32 bytes: the page bitmap */
#define A2_SCR_WLO   0x2C                   /* the write window */
#define A2_SCR_WHI   0x2E
#define A2_SCR_ANY   0x30                   /* "the core wrote something" */
#define A2_SCR_WATLO 0x32                   /* the watch range: the LIVE */
#define A2_SCR_WATHI 0x34                   /*   display page */

/* --- the ROM part (section 1.4, 1.5) -------------------------------------- */
#define A2_ROM_PART   0
#define A2_ROM_MAIN   0x0000                /* 12,288 bytes: $D000-$FFFF */
#define A2_ROM_CHRGEN 0x3000                /*  2,048 bytes: the II+ chargen */
#define A2_ROM_DISK2  0x3800                /*    256 bytes: the P5 boot ROM */
#define A2_ROM_SIZE   14848
/* Block $C0 of the character generator is the NORMAL form of all 64 glyphs
 * and its bit 7 carries nothing, so `& 0x7F` is the whole decode - which is
 * a2_chargen's job. Blocks $40, $80 and $C0 are byte-identical and block $00
 * is their XOR $7F, and tools/a2ref.py --romshape asserts exactly that about
 * the pinned ROM. */
#define A2_CHR_BLOCK  (A2_ROM_CHRGEN + 0xC0 * 8)
/* The fetch bias for a PC in the ROM is `romseg - ($D000 >> 4)`, which WRAPS -
 * and reads somewhere else entirely - if the part's base segment is below
 * $0D00. It never is: the heap starts above the kernel's ~111KB footprint
 * (docs/KERNEL-MEMORY.md). This is the guard that says so out loud rather
 * than the assumption that does not. */
#define A2_ROM_MINSEG 0x0D00

/* --- the window (section 7.1) --------------------------------------------- */
#define A2_SCRW    280                      /* the Apple's visible pixels */
#define A2_SCRH    192
/* THE LETTERBOX IS 16 LEFT AND 24 RIGHT AND THAT IS ARITHMETIC, NOT TASTE.
 * The Apple's 280 pixels sit inside a 320-pixel band so that the band is a
 * whole 40 bytes; 16 px is TWO WHOLE BYTES, so the composer's OUTPUT stays
 * byte aligned even though its 7-pixel cells are not. A 20/20 split would put
 * pixel 0 half way through a byte and every store would straddle. */
#define A2_LBOXL   16
#define A2_LBOXR   24
#define A2_BANDW   (A2_LBOXL + A2_SCRW + A2_LBOXR)          /* 320 */
/* A LITERAL AND NOT `(A2_BANDW / 8)`, AND SO IS A2_GROUPS BELOW. Both are
 * typed out again as `equ`s in a2band.inc, and tests/unit/t_mirror.py is what
 * says the two copies agree - but it compares `#define NAME <value>` against
 * `NAME equ <value>` and can read neither side's ARITHMETIC, so a derived
 * spelling here is a name the mirror silently skips rather than a name it
 * checks. The derivation is not lost: a2uitest asserts it at run time, in the
 * gate that already runs. */
#define A2_BSTRIDE 40                       /* = A2_BANDW / 8 */
#define A2_X2STRIDE 80                      /* ...and of a PIXEL-DOUBLED band
                                             * (section 7.8). A literal again,
                                             * and typed out as an `equ` in
                                             * a2band.inc, for the reason
                                             * above: tests/unit/t_mirror.py
                                             * compares `#define NAME <value>`
                                             * against `NAME equ <value>` and
                                             * can read neither side's
                                             * arithmetic */
#define A2_LBOXB   (A2_LBOXL / 8)                           /* ...2 of them */
#define A2_BORDER  8                        /* on every side */
#define A2_STATH   10                       /* the status row */
#define A2_W_X     7
#define A2_W_Y     20
/* THE FRAME IS TWO PIXELS WIDER THAN THE CONTENT, AND THAT IS THE KERNEL'S
 * ARITHMETIC AND NOT A FUDGE (C64-SPEC §9.1): os88_wm_create authors a FRAME,
 * os88_wm_geom answers the CONTENT box, and the difference is the window's
 * two 1-pixel side borders. So the CONTENT width is authored here and the
 * frame is derived. */
#define A2_CONT_W  (A2_BANDW + A2_BORDER * 2)               /* 336 */
#define A2_W_W     (A2_CONT_W + 2)                          /* 338 framed */
#define A2_CONT_H  (A2_SCRH + A2_BORDER * 2 + A2_STATH)     /* 218 */
/* The content height is W_H - TITLE_H - 1, which is LESSONS.md 13's finding:
 * a window authored TITLE_H + 192 tall shows 191 rows and a sliver, and the
 * model's last scan line is never on the glass. */
#define A2_W_H     (OS88_TITLE_H + A2_CONT_H + 1)           /* 237 */

#define A2_ROWS  24                         /* character rows */
#define A2_COLS  40                         /* ...and cells across */
#define A2_GROUPS 5                         /* = A2_COLS / 8; 8 cells = 56
                                             * bits = 7 bytes. A literal for
                                             * A2_BSTRIDE's reason above */
#define A2_GBYTES 7                         /* ...which is a group's output */
#define A2_SHBYTES (A2_BSTRIDE * A2_SCRH)   /* the frame shadow: 7,680 */

/* i * 40, i * 7 and i * 8, written as shifts. cc8086 refuses an
 * `imul ax, ax, 40` wherever it cannot prove a scratch register dead, and a
 * stride that is not a power of two is exactly the shape that provokes it
 * (LESSONS.md 3, and SPEC.md 73's rule that the 8086 has no cheap general
 * multiply anyway). The Apple's strides are 40 and 7 and cannot be padded. */
#define A2_X40(i) ((((unsigned)(i)) << 5) + (((unsigned)(i)) << 3))
#define A2_X7(i)  ((((unsigned)(i)) << 3) - ((unsigned)(i)))
#define A2_X8(i)  (((unsigned)(i)) << 3)

/* --- the monitor's zero page, where the SCREEN's own state lives ---------- */
/* CV, the cursor's character row (0-23), written by the Autostart ROM's COUT
 * and by VTAB/HOME. It is what the visible band follows on a desktop too
 * short to hold all 192 scan lines (a2scr.c's a2_geom, APPLE2-SPEC section
 * 7.1) - read AS RAM, never through a soft switch. */
#define A2_CV   0x0025

/* --- the display pages (section 7.2) -------------------------------------- */
#define A2_TXT1 0x0400                      /* text/lo-res page 1 */
#define A2_TXT2 0x0800                      /* ...and page 2 */
#define A2_PGLEN 0x0400
/* ...AND THE HI-RES PAGES, WHICH ARE EIGHT TIMES THE SIZE (section 7.2). The
 * write window is taken over the LIVE display page, so its length moves with
 * the mode: 1KB of text or lo-res, 8KB of hi-res. */
#define A2_HGR1 0x2000
#define A2_HGR2 0x4000
#define A2_HGRLEN 0x2000
/* MIXED IS THE TOP 160 SCAN LINES IN THE GRAPHICS MODE AND THE BOTTOM 32 IN
 * TEXT (section 7.4), and 160 is a multiple of 8: the split falls on a
 * CHARACTER ROW boundary, so no row is ever half one renderer and half the
 * other and the flush's row loop needs no partial case. */
#define A2_MIXROW 20

/* --- what the machine is doing -------------------------------------------- */
#define A2_ST_HALT 0                        /* not started: the state a2_state
                                             * holds until os88_main has the
                                             * ROM and the claims. IT IS ONLY
                                             * THE PRE-LAUNCH VALUE - wave 2's
                                             * a2_power_on leaves A2_ST_RUN
                                             * before the window exists, so no
                                             * callback can ever see it and a
                                             * callback that tests for it is
                                             * testing a constant */
#define A2_ST_RUN  1
#define A2_ST_JAM  2
#define A2_ST_DEAD 3                        /* the close is in flight (75.2) */

static int a2_state = A2_ST_HALT;
static void *a2_win;
static int a2_kick;                         /* a wake is wanted */
static int a2_exit_req;                     /* File > Quit asked; spent at the
                                             * top of the next wake (75.2) */
/* THE RESET LATCH (APPLE2-SPEC section 4.5). os88_oncmd and os88_onkey are
 * both dispatched under the DESKTOP's gfx lock, and a power-on is a 48KB fill
 * - so the command LATCHES and the WAKE, which holds no lock, spends it. The
 * C64's wave-3 lesson, one machine along. */
#define A2_RST_CTRL   1                     /* Ctrl-Reset: I set, the jam
                                             * cleared, SP down 3 in page one,
                                             * PC from $FFFC. RAM UNTOUCHED */
#define A2_RST_OACTRL 2                     /* ...and its Open-Apple form: the
                                             * same, with the Autostart
                                             * Monitor's PWREDUP check forced
                                             * to MISMATCH so the ROM takes the
                                             * COLD path. Still no RAM fill -
                                             * that is Power On's */
#define A2_RST_POWER  3                     /* the cold machine: the FF FF 00
                                             * 00 pattern and the three pokes */
static int a2_reset_req;

/* --- THE WAVE-4 LATCHES, AND THEY ARE LATCHES FOR THE RESET LATCH'S REASON -
 * os88_oncmd runs under the DESKTOP's gfx lock (os88.h), so every instruction
 * a command executes is the whole desktop stopped - the mouse, the dock, every
 * other task's drawing. Edit > Copy is 24 movers, 24 row composers and an
 * OSAPI_CLIP_PUT; Edit > Paste is a heap claim and an OSAPI_CLIP_GET; and both
 * clipboard calls reach kernel/clip.inc's mem_claim, which may COMPACT an
 * arena this app has a pinned 64KB sitting in - "a memcpy in tenths of a
 * second" in memory.inc's own words, and a term nobody can bound from a
 * command handler (LESSONS.md 6). So the command latches and ovl_a2_clip_service
 * (a2kbd.c) spends it from the TOP of the next wake with NO LOCK HELD, before
 * the slice, so not one emulated cycle has run between the pick and the work
 * and the screen copied is the screen the user was looking at. It is the
 * C64's wave-3 lesson, where the same pair cost 2,000 bridge crossings and a
 * call-counting cost model charged one. */
static int a2_copy_req;
static int a2_paste_req;

/* --- THE LAUNCH DOCUMENT (SPEC.md 54.5, APPLE2-SPEC section 12) -----------
 * `CC_ASSOC` declares `BAS` from this wave, so a double-click on a tokenised
 * Applesoft program launches THIS package with the file as its argument. That
 * is why the association could not be declared any earlier: an extension the
 * build cannot open launches the emulator and then refuses, which is worse
 * than no association.
 *
 * READ-AND-CLEAR, so it is BANKED in os88_main and SPENT IN THE FIRST WAKE.
 * The name and the folder arrive together and the first caller gets them; the
 * floppy is touched from the wake, which holds no lock and may call the file
 * slots by contract (SPEC.md 54.10, 74.1) - and which is also the only place
 * an ovl_* may be reached from at launch, because os88_main has no instance
 * to resolve a module for. */
static char a2_argname[13];
static struct os88_place a2_argplace;
static int a2_argp;
static unsigned a2_argdl;                   /* ...AND IT WAITS FOR THE MACHINE
                                             * TO REACH `]`. See os88_onwake:
                                             * this is the deadline that stops
                                             * the wait being unbounded */
#define A2_ARGWAIT (18 * 60)                /* one minute of host ticks */
static int a2_pause;                        /* CPU > Stop / Continue - MII's
                                             * SIGNAL_STOP and SIGNAL_RUN
                                             * (mii_mui_menus.c:333,350). It
                                             * is its own flag and not an
                                             * a2_state value, because a
                                             * PAUSED machine is still a
                                             * machine and A2_ST_JAM is not */
static int a2_mute;                         /* Machine > Mute - MII's
                                             * m_audio_menu row, given a body
                                             * by wave 5 (section 8). It is
                                             * declared HERE, above every
                                             * #include, for this block's whole
                                             * reason: a2cmd.c moves it and
                                             * apple2.c's a2_spk_service reads
                                             * it, and two copies of a latch
                                             * are two latches */
static int a2_warp;                         /* CPU > Warp - OURS (section
                                             * 10.1), and on this port it is
                                             * the wall slice's CAP and
                                             * nothing else: there is no
                                             * throttle here to take off */
static int a2_ovl_asked;                    /* the first-wake probe ran... */
static int a2_ovl_told;                     /* ...and its REFUSAL has been said
                                             * once. The probe is re-armed by
                                             * every menu pick on a disk with
                                             * no module (a2_ovl_ready), so
                                             * without this the toast was
                                             * repeated per pick. The ROW is
                                             * said every time, because the row
                                             * is the answer to what the user
                                             * just did; the toast is the
                                             * announcement, and an
                                             * announcement is made once. */
static int a2_ovl_res;                      /* ...AND IT ANSWERED YES, WHICH IS
                                             * A DIFFERENT FACT and the one
                                             * that keeps floppy I/O out of
                                             * the gfx lock. Reaching an ovl_*
                                             * makes the runtime resolve the
                                             * module, and if it is not
                                             * resident that is a MEM_CLAIM
                                             * and a FILE_READ - ~400 ms -
                                             * inside whatever context asked.
                                             * os88_oncmd, os88_about and
                                             * os88_onfile are all dispatched
                                             * UNDER the desktop's gfx lock,
                                             * so a locked caller never
                                             * crosses the bridge unless this
                                             * says the module is already
                                             * there; the WAKE, which holds no
                                             * lock, is what retries the load
                                             * (APPLE2-SPEC section 15.5) */
static int a2_have_cpu = 1;                 /* **WAVE 2 SET IT.** The two
                                             * reset chords have bodies now,
                                             * so a2_menu_state revives them.
                                             * SPEC.md 47's rule is that
                                             * nothing is live that can only
                                             * refuse, and a "not in this
                                             * build yet" toast is exactly
                                             * that */
static int a2_have_cmd = 1;                 /* **WAVE 4 SET IT**, and what it
                                             * MEANS is unchanged: the COMMAND
                                             * has a body. That is the other
                                             * half of a2_have_cpu's rule and
                                             * the reason it is two flags -
                                             * Load and Save Program, Copy,
                                             * Paste, Stop/Continue, Warp and
                                             * Power On had none until this
                                             * wave, and Power On additionally
                                             * owed section 10.2's TWO-ROW
                                             * confirmation before it could be
                                             * live at all, because a
                                             * data-loss row without the
                                             * confirmation its contract names
                                             * is not the item the SPEC
                                             * describes. Wave 4 wrote all
                                             * seven bodies and the
                                             * confirmation, so the greying
                                             * has stopped being true and
                                             * SPEC.md 47 does not let one
                                             * outlive its reason. The flag
                                             * STAYS - it is what a2_menu_state
                                             * rewrites every row from, and
                                             * the Disk II follow-up has rows
                                             * of its own to revive */
static int a2_have_snd;                     /* ...and WAVE 5 SETS THIS, which
                                             * is Machine > Mute's own gate.
                                             * Two flags and not one, because
                                             * they are two different facts
                                             * and a2_menu_state has to be
                                             * able to revive the speaker's
                                             * row without reviving the
                                             * 6502's */

/* The state the parts share. It is declared HERE, above every #include, so
 * that each part has one definition to read and none of them can quietly
 * declare a second copy of a flag the others set. */
static int a2_dirty_any;                    /* something wants composing */
static int a2_st_dirty;                     /* the status row wants redrawing.
                                             * HERE and not in a2scr.c with the
                                             * rest of the row's state: a2io.c
                                             * sets it too - a video switch
                                             * that changes nothing on the
                                             * glass still moves the row's
                                             * MIXED field - and a2io.c is
                                             * #included above a2scr.c */
static int a2_pct;                          /* THE SPEED FIGURE (section 9):
                                             * per cent of a 1.02 MHz Apple
                                             * II, folded once a second by
                                             * a2_speed_fold below and drawn by
                                             * a2scr.c's status row. It is
                                             * declared HERE for this block's
                                             * whole reason - one definition,
                                             * above every #include, so the
                                             * writer and the reader cannot
                                             * drift into two copies */
/* THE JAM LINE, WHICH IS A PERMANENT ROW STATE AND NOT A MESSAGE (section
 * 4.5). It is declared here for this block's reason - a2_jam WRITES it and
 * a2scr.c's status row READS it - and it is not an a2_say() for the reason
 * apps/c64/c64.c:1071-1087 states one machine along: a2_say expires after
 * five seconds and a jammed machine is a PERMANENT condition, so the glass
 * showed a dead machine and an idle one identically once the deadline passed
 * (build/port-shots/wave2fix-16-jam-blank.png is that state - a status row
 * reading `TEXT` and nothing else, five seconds after the machine died).
 * Going through a2_say also drew the same 22 glyphs TWICE: the expiry forces
 * the row's full path and re-letters the identical line at the identical
 * place, ~21 ms that changes not one pixel, which is PERFORMANCE.md rule 2's
 * erase-then-letter in the one place it is free to avoid. */
static char a2_jamline[24];
static int a2_sh_ok;                        /* the shadow describes the glass */
static int a2_border_dirty;                 /* the border wants filling */
static int a2_full;                         /* the fullscreen latch is ours */
/* ...AND WHETHER THE EXCLUSIVE BRACKET IS UP (section 13.3), which is a2scr.c's
 * to SET and this file's to READ - which is why it is here with a2_abt_up and
 * not down there with the rest of the foreign mode.
 *
 * IT IS THE FENCE ON RULE 2, AND IT WAS WRITE-ONLY FOR A WAVE. a2_fsx_main's
 * rule list says "`a2_fsx_up` is what fences the one path that could, a2_jam's
 * os88_toast, which is reachable from the slice" - and nothing read it. A
 * 6502 that JAMs inside the bracket (a $02 opcode, which any wild jump
 * reaches) raised a toast, and kernel/toast.inc's toast_show ends in
 * toast_now, whose predicate - the gfx lock held by task 0 - is EXACTLY the
 * bracket's state, so menu_draw_bar and the panel went down IMMEDIATELY: 640-
 * wide planar desktop chrome written into an A000 the card has just put into
 * chained mode 13h, at a stride of 320, and the foreign shadow then believes
 * those bytes are ours, so it is permanent for the rest of the session
 * (SPEC.md 53.7 names the whole class binding-illegal). Two paths read it now
 * and each says so where it is. */
static int a2_fsx_up;
/* ...and the About panel's three, which a2about.c WRITES and a2scr.c's flush
 * READS. One definition, here, so the two cannot drift into two copies of the
 * same fact (which is what the whole of this block exists to prevent). */
static int a2_abt_up;                       /* the panel is on the glass... */
/* ...AND WHICH PANEL IT IS. Wave 4 gave Machine > Power On the TWO-ROW
 * confirmation its contract names (section 10.2), and a confirmation is the
 * About panel one field along: modal, snapped to the band, holding the Apple
 * scan lines it covers so nothing under it is drawn, and dismissed as DAMAGE
 * rather than as a repaint. So it is the same panel with a KIND rather than a
 * second copy of ovl_about_geom, a second hold range and a second arm in
 * os88_paint - which is what "one definition, so the two cannot drift" means
 * when the second thing is a whole mechanism. */
#define A2_PAN_ABOUT 0
#define A2_PAN_CFM   1                      /* AppleWin's Reboot box
                                             * (source/Windows/WinFrame.cpp
                                             * :1997-2013), MB_YESNO */
static int a2_pan_kind;
/* A2_CFM_UP - the CONFIRMATION is up, and the 6502 is stopped while it is
 * (section 11).
 *
 * THE ABOUT PANEL DOES NOT STOP IT AND THIS ONE DOES, which is the one place
 * the two kinds part company. The About panel's hold range already keeps the
 * glass correct - the flush composes and blits nothing under it - so a
 * machine mid-RUN carries on behind it, and stopping it because somebody
 * opened About would be a behaviour change nobody asked for. The
 * confirmation is the other way round on the redraw budget AND on its
 * subject: it is 52 pixels tall and holds ~6 of the 24 character rows, so a
 * machine that is printing kept composing and blitting the other ~18 on every
 * host tick - ~200 ms of the target per tick - for as long as the box waited
 * for a human to read two lines, which also made the Yes/No click feel lost;
 * and the answer to the question is about to wipe the machine, so there is
 * nothing running behind it worth a pixel. a2_wants_wake carries the same
 * term, so the app IDLES while the box is up instead of re-posting a wake a
 * tick. */
#define A2_CFM_UP() (a2_abt_up && a2_pan_kind == A2_PAN_CFM)
/* ...at THIS rectangle, in screen coordinates. ovl_about_geom writes the
 * four, ovl_about_draw draws to them, and os88_paint TESTS the damage rect
 * against them - which is what stops a two-pixel expose in a corner from
 * repainting the whole card. They live here rather than in a2about.c for the
 * same reason as the three below: one definition, and os88_paint is ahead of
 * that file's #include. */
static int a2_abt_x, a2_abt_y, a2_abt_w, a2_abt_h;
/* ...over these APPLE SCAN LINES, and l0 > l1 means NONE: the flush skips a
 * held line rather than drawing under something opaque, and the close forces
 * exactly them (damage, never a repaint). */
static int a2_hold_l0 = 1;
static int a2_hold_l1;
/* THE CONFIRMATION'S TWO BUTTONS, in screen coordinates, written by the
 * overlay that draws them and read by the RESIDENT hit test - because a click
 * is a callback and a callback is reached by a near offset (a2about.c's own
 * rule). Only code moves; a static an ovl_* writes is resident and
 * DS-relative like every other (SPEC.md 73.14). */
static int a2_cfm_bx[2], a2_cfm_by;
#define A2_CFM_BW 48
#define A2_CFM_BH 13
static int a2_covered;                      /* the last wake's clip_set found
                                             * not one pixel of us on the
                                             * glass - a2_wants_wake's own
                                             * comment says what it is for */
static int a2_tmr_ok;                       /* OSAPI_WM_TIMER answered YES, so
                                             * the flash phase is a TIMER and
                                             * not a wake poll (section 7.6).
                                             * kern_small carries the slot and
                                             * not the body (SPEC.md 13.8.2),
                                             * so this is TESTED and the
                                             * polling arm is what a refusal
                                             * falls back to */

/* --- the interleaved row bases (section 7.2) ------------------------------ */
/* THE 24-ENTRY TABLE IS BUILT AT LAUNCH INTO BSS, NOT SHIPPED IN .data. An
 * initialised array is paid for twice - once in the file and once in the
 * region - and this one is 48 bytes of arithmetic
 * (apple2emu src/video.cpp:687-718, whose own source is "Apple 2 Monitors
 * Peeled, pg 15"):
 *
 *   text_map[i] = 1024 + 256*((i/2)%4) + 128*(i%2) + 40*((i/8)%4)
 *
 * Page 2 is the same plus 1024, which is why there is one table and not two,
 * and hi-res is `0x1C00 + text_map[i]` with the scan line at
 * `hires_map[row] + 1024*sub` - wave 3's business, off this same table. */
static unsigned a2_tbase[A2_ROWS];

/* ...AND THE HI-RES ONE, WHICH IS THAT TABLE PLUS $1C00 (section 7.2). Scan
 * line 8r+sub of hi-res page 1 is at
 *
 *   $2000 + $400*sub + $80*(r & 7) + $28*(r >> 3)
 *
 * and $80*(r&7) + $28*(r>>3) is exactly a2_tbase[r] - $400, so the whole
 * hi-res map is a2_tbase[r] + $1C00 with the eight scan lines $400 apart.
 * Forty-eight bytes of bss built beside the text map rather than an addition
 * in the flush, so the two maps are read the same way and a2_band_hires is
 * handed ONE address. Page 2 is +$2000 here, where text page 2 is +$400. */
static unsigned a2_hbase[A2_ROWS];

/* THE 7-BIT REVERSE TABLE (section 7.3). 128 entries of one byte, ONE table
 * in ONE place, built here and RESIDENT - hi-res source bytes carry bit 0 as
 * the LEFTMOST pixel and the framebuffer wants MSB first, so every hi-res
 * byte goes through this in a2_band_hires. THE CHARACTER GENERATOR IS THE
 * OTHER WAY UP and needs no reversal at all, which is the one thing about
 * this machine most likely to be got backwards.
 *
 * IT IS AN ORDINARY GLOBAL AND NOT A static, for a2_chr's reason one table
 * along: a2band.inc's hi-res phase B indexes `_a2_rev` directly, and nasm can
 * only see a name the compiler emitted as a global. */
unsigned char a2_rev[128];

/* --- THE LO-RES LUMINANCE LADDER (section 7.3) ---------------------------
 * THE WINDOWED PATH IS MONOCHROME and says so as a fact (section 10.3), so a
 * lo-res block is black or white by its colour's LUMINANCE. The figure
 * compared is Rec.601 luma - 299R + 587G + 114B - and the per-mille numbers
 * below are that sum over white's.
 *
 * THE PALETTE IS ONE THE REFERENCE ACTUALLY DISPLAYS, and that is the whole
 * of why it is MII's and not AppleWin's. This ladder first shipped off
 * AppleWin's `PaletteRGB_NTSC` lores block (source/RGBMonitor.cpp:149-164) -
 * whose own first line reads "Note: this is a placeholder. This palette is
 * overwritten by VideoInitializeOriginal()", and it is:
 * VideoInitializeOriginal (RGBMonitor.cpp:1186-1196) memcpy's sixteen
 * NTSC-GENERATED colours over exactly that block and NTSC_VideoInit
 * (NTSC.cpp:2366-2368) calls it at start-up, so AppleWin never puts those
 * literals on a screen. They are not transcribable either: the colours that
 * replace them come out of GenerateBaseColors (NTSC.cpp:2697-2721), which
 * runs a signal-level simulation rather than listing values.
 *
 * SO THE DEFINER IS MII's `palettes[0]` "Color NTSC" (src/mii_video.c:94-113)
 * - a live table, read straight into the CLUT it renders through - taken
 * through MII's OWN lo-res mapping, `mii_base_clut.lores[0]`
 * (src/mii_video.c:173-177). THAT SECOND HALF IS LOAD-BEARING: MII's CI_*
 * enum is not in Apple colour order (CI_PURPLE is 1, and lo-res colour 1 is
 * MAGENTA), so a table read by enum index rather than through the clut is
 * scrambled.
 *
 * apple2emu's Lores_colors (src/video.cpp:100-115, the mrob.com values) is
 * the CROSS-CHECK and not the definer: it is indexed by lo-res colour
 * directly, it agrees with MII exactly on twelve of the sixteen and within a
 * few units on the rest, and - the fact that matters here - IT AGREES ON ALL
 * SIXTEEN LIT/DARK DECISIONS. Two live tables, one threshold, no
 * disagreement about any pixel this build can draw.
 *
 * WHAT THE PLACEHOLDER COST: under it, purple (467 per mille) and medium blue
 * (499) drew BLACK, decided by one part per mille of a palette no emulator
 * shows. Under both live tables purple is 568 and medium blue 613 and both
 * are LIT, which is what `GR : COLOR=3` and `GR : COLOR=6` look like in every
 * reference this port names.
 *
 * WHAT IS STORED IS THE RANK AND NOT THE LUMINANCE, and that is arithmetic
 * rather than taste: a byte's own granularity is 0.39 % and the ladder has
 * pairs closer than that, so any scaling of the figures into a byte ties an
 * ordering the palette has. A monochrome composer asks only "is this lighter
 * than that", so the rank is the whole of what it needs - and it is exactly
 * what tools/a2ref.py --lumcheck checks, over all 256 ordered pairs, against
 * luminances IT computes from the same RGBs, at FULL precision.
 *
 * THE ONE TIE IS THE PALETTE'S OWN. Grey 1 and grey 2 are the same three
 * bytes in MII's table (0x9C,0x9C,0x9C) and in apple2emu's, so they share
 * rank 7; nothing else in the sixteen ties. Ranks are therefore not dense -
 * 8 is unused - which is correct and is what --lumcheck asserts.
 *
 * THE THRESHOLD IS HALF OF WHITE. A colour is LIT when its luminance is at or
 * above 500 per mille, which is the eleven colours from purple (568) up;
 * purple is rank 5, so A2_LUM_LIT is 5. a2ref.py applies the same 500 to its
 * own numbers, so a disagreement about one colour is a bit-for-bit frame
 * mismatch and not a matter of opinion. */
#define A2_LUM_LIT 5
static const unsigned char a2_lum[16] = {
     0,      /*  0 black        0 per mille - rank  0 */
     3,      /*  1 magenta    378            - rank  3 */
     2,      /*  2 dark blue  376            - rank  2 */
     5,      /*  3 purple     568            - rank  5 */
     4,      /*  4 dark green 418            - rank  4 */
     7,      /*  5 grey 1     611            - rank  7 */
     9,      /*  6 med blue   613            - rank  9 */
    12,      /*  7 light blue 806            - rank 12 */
     1,      /*  8 brown      376            - rank  1 */
     6,      /*  9 orange     569            - rank  6 */
     7,      /* 10 grey 2     611            - rank  7 */
    11,      /* 11 pink       760            - rank 11 */
    10,      /* 12 green      614            - rank 10 */
    14,      /* 13 yellow     815            - rank 14 */
    13,      /* 14 aqua       813            - rank 13 */
    15       /* 15 white     1000            - rank 15 */
};

/* ...and the seven-bit pattern each colour contributes, derived from the
 * ladder at launch. THIS path's block is UNIFORM - by the luminance decision
 * above, not because monochrome lo-res has no pattern: MII's mono arm
 * (src/mii_video.c:567-586) draws a per-pixel dot pattern and this port
 * deliberately does not (APPLE2-SPEC section 7.3) - so it is one constant per
 * colour, 0x00 or 0x7F, and a2_band_lores is one table read a nibble. A
 * global for a2_rev's reason. */
unsigned char a2_lopat[16];

/* EDIT > COPY'S TWO ARRAYS, and they are globals for a2_chr's reason: nasm
 * has to see the labels, because a2_copy_row (a2mem.inc) is the per-byte loop
 * and indexes both directly (APPLE2-SPEC section 6.5).
 *
 *  - a2_scrow  ONE character row, brought out of the RAM claim by a single
 *              a2_zcopy_out - 24 calls for the screen, not 960.
 *  - a2_astab  the 128-entry fold of the Apple's SCREEN encoding to ASCII.
 *              The screen byte's top two bits are the ATTRIBUTE - inverse,
 *              flashing, normal - and a II+ character generator holds 64
 *              glyphs, so `and 0x7F` collapses the three forms onto one index
 *              and this table folds what is left: $00-$1F and $40-$5F are
 *              `@A-Z[\]^_`, which is ASCII $40-$5F, and $20-$3F and $60-$7F
 *              are ASCII $20-$3F unchanged. It is built in os88_main and not
 *              in the overlay, because 128 bytes of bss and a nine-line loop
 *              are cheaper than either a .data array paid for twice or a
 *              second reason for a disk with no APPLE2.OVL to behave
 *              differently. */
unsigned char a2_scrow[A2_COLS];
unsigned char a2_astab[128];

static char a2_title[] = "Apple II Plus Emulator";   /* section 16.1's long
                                                      * form: the window title
                                                      * and the About panel's
                                                      * first row. The short
                                                      * form `Apple II+` is
                                                      * AM_NAME's and the Wire
                                                      * record's */

/* --- what the parts define, declared here so no part declares it twice ---- */
static void a2_say(const char *s);
static void a2_flush(void *win);
static void a2_sh_inval(void);
static void a2_dirty_all(void);
static void a2_line_dirty(int line);
static void a2_row_dirty(int row);
static int  ovl_a2_dirty_range(unsigned lo, unsigned hi);
static void a2_dirty_split(void);           /* the MIXED split's four rows,
                                             * and a2_rowwide with them -
                                             * a2scr.c owns a2_rowwide, so
                                             * a2io.c asks for the mark rather
                                             * than reaching into the array */
static int  a2_geom(void *win);
static void a2_tier_init(void);
static void a2_watch_page(void);
static void a2_menu_state(void);
static int  a2_mode_page(void);
static int  a2_page_len(void);
static int  a2_mode_of(void);
static int  a2_row_mode(int r);
static unsigned a2_row_base(int r);
static void a2_kbd_poll(void);
static void a2_fullscreen_toggle(void *win);
static void a2_flash_force(void);
static void a2_line_force(int line);
static void a2_key(int ascii, int scan, void *win);
static void a2_io_init(void);
static void a2_reset_cpu(void);
static void a2_power_on(void);
static void a2_kb_put(int b);
static void a2_reset_service(void);
static void a2_speed_fold(void);
static void a2_warp_set(int on);
/* ...and the three the FOREIGN BRACKET needs. It lives in a2scr.c, beside the
 * dirty-line set it is driven off, and is therefore ABOVE all three
 * definitions (section 13.3).
 *
 * THEY ARE THREE LINES AND ONE COMMENT, AND THE FIRST CUT WAS THREE COMMENTS
 * THAT NEVER CLOSED: a `/*` opened on the first line ate the two declarations
 * under it, the header still compiled, and the only symptom was two
 * `implicit function declaration` errors pointing at a2scr.c. LESSONS.md 11
 * records the same shape eating OS88_FDLG_SAVE one package along. */
static void a2_slice(void);
static void a2_spk_service(void);
static int  a2_flash_step(unsigned t);
static void a2_sound_stop(void);            /* THE ONE PLACE A STOPPED MACHINE
                                             * GOES QUIET (section 8) - and it
                                             * is declared here because five
                                             * of its callers are in the
                                             * #included parts above its
                                             * definition */
static void a2_jam(void);
static void a2_about_close(void *win);
static void a2_panel_close(void *win, int yes);
static int  ovl_a2_clip_service(void);
static void a2_paste_stop(void);
/* ...and the two halves of apple2emu's paste handshake, which a2io.c's soft
 * switches call and a2kbd.c defines - a2io.c is #included first, so the
 * declaration has to be here (section 6.5). THE GUARD IN FRONT OF THEM IS NO
 * LONGER A CALL: a2io.c tests a2_paste_seg, which it declares itself, so the
 * hottest path in the emulator pays a compare rather than a near call and
 * a2_paste_live is gone. */
static void a2_paste_peek(void);
static void a2_paste_take(void);
static int  ovl_about_paint(void *win);
/* AN `ovl_` ANSWERS A STATUS AND 0 MEANS IT DID NOT HAPPEN (SPEC.md 73.14,
 * LESSONS.md 5). Both of the panel's bodies live in APPLE2.OVL, so both can
 * be reached at a moment the module is not resident - and a `void` one hands
 * its caller no way to find out. os88_paint treats 0 as "the card is gone". */
static int  ovl_about_geom(void);
static void a2_about_gone(void);
static int  a2_ovl_ready(void *win);
static int  ovl_a2_init(void);
static int  ovl_a2_cmd(int menu, int item, void *win);
static int  ovl_about_show(void *win);
static int  ovl_a2_prog(int mode, const char *name, unsigned size_lo,
                        void *win);
static int  ovl_a2_confirm(void *win);

/* THE REST OF THE TRANSLATION UNIT (SPEC.md 73.1: one .c, because `nasm -f
 * bin` has no notion of an external symbol, so a C package is one file with
 * the parts #included into it). Every one of them is a written prerequisite
 * in the Makefile, because make cannot see through #include. */
#include "a2io.c"                           /* the soft switches, both
                                             * directions, and the video state
                                             * they set */
#include "a2kbd.c"                          /* the II+ byte map - AppleWin
                                             * asciicode row 0, and both reset
                                             * chords */
#include "a2scr.c"                          /* the damage model, the flash
                                             * phase, the flush, the status
                                             * row and the geometry */
#include "a2menu.c"                         /* the four menus, transcribed */
#include "a2cmd.c"                          /* ovl_*: the command shells */
#include "a2prog.c"                         /* ovl_*: Load and Save Program */
#include "a2disk.c"                         /* ovl_*: the disk dialog */
#include "a2about.c"                        /* ovl_*: the About panel */

/* ==========================================================================
 * THE OVERLAY FENCE (APPLE2-SPEC section 15.5)
 * ========================================================================*/
/* a2_ovl_ready - may a LOCKED callback cross into APPLE2.OVL right now?
 *
 * Only if the module is already resident. Resolving it is an OSAPI_MEM_CLAIM
 * and an OSAPI_FILE_READ - a floppy seek, ~400 ms - and os88_oncmd,
 * os88_about and os88_onfile are all dispatched under the desktop's gfx lock,
 * which is the whole machine stopped. The WAKE holds no lock and may call the
 * file slots by contract (SPEC.md 74.1), so it is what loads it. */
static int a2_ovl_ready(void *win)
{
    (void)win;
    if (a2_ovl_res)
        return 1;
    a2_ovl_asked = 0;                       /* ...so the next wake retries */
    /* ONE SENTENCE FOR ONE CONDITION. This used to say `No APPLE2.OVL yet.`,
     * which no user could ever read: clearing a2_ovl_asked makes the very
     * next wake re-run the probe, and the probe overwrites the row with
     * `Unable to load APPLE2.OVL.` before a tick has passed - so the wave
     * shipped a second string for the same fact that nothing could show. The
     * `yet` was a promise as well, where SPEC.md 47 asks for the fact. */
    a2_say("Unable to load APPLE2.OVL.");
    return 0;
}

/* ==========================================================================
 * THE SPEAKER, THE OTHER HALF (APPLE2-SPEC section 8)
 * ========================================================================*/
/* a2io.c measures the toggle intervals; this is what turns them into ONE far
 * call and what takes the note back down again. The six rules the C64 learned
 * (apps/c64/c64.c's c64_sound_stop and the retry bound) all apply here and
 * each is named where it is obeyed.
 *
 * THE PRIORITY IS THE C64'S 0x40 (SPEC.md 34.3's router): an emulated machine
 * making a noise is an application's sound and must lose to an alert. */
#define A2_SND_CAP_TONE 0x01                /* apps/os88api.inc:370 */
#define A2_SND_PRI      0x40
#define A2_SND_HZMIN    20                  /* the sink's own band, and the
                                             * band A2_SPK_DMIN/DMAX were
                                             * derived from */
#define A2_SND_HZMAX    12000
#define A2_SND_TRIES    8                   /* wakes a refused grant is retried
                                             * for, then dropped with the fact
                                             * said once (rule 4) */
#define A2_SND_ODD      4                   /* ...and wakes of a TOGGLING
                                             * speaker this estimator could
                                             * not turn into a tone before the
                                             * stated fact is said anyway
                                             * (below) */
/* THE SILENCE IS TWO TICKS AND THE SPEC SAYS 1/18 s, and the difference is
 * what a 18.2 Hz counter can express rather than a change of mind: `t -
 * a2_spk_tick >= 1` fires on a tone whose last toggle merely landed on the
 * far side of a tick boundary, which is every other note. Two means the
 * silence measured is between one and two ticks - 55 to 110 ms - and a tone
 * loop that has stopped is still cut off inside a tenth of a second. */
#define A2_SPK_QUIET    2
static int a2_snd_hz;                       /* what is SOUNDING, 0 = nothing */
static int a2_snd_want;                     /* ...and what was last ASKED for,
                                             * which is what bounds the retry:
                                             * a new note restarts the count */
static int a2_snd_tries;
static int a2_snd_said;                     /* the stated fact, said once */
static int a2_snd_busy_said;                /* ...and the busy one, likewise */
static int a2_snd_odd;                      /* consecutive wakes in which the
                                             * speaker TOGGLED and the
                                             * estimator answered nothing
                                             * steady */
static unsigned a2_spk_tick;                /* the host tick a toggle last
                                             * arrived on... */
static unsigned a2_spk_last;                /* ...and the count that dated it */

/* a2_sound_stop - THE ONE PLACE A STOPPED MACHINE GOES QUIET.
 *
 * The tone is played with duration 0, which SPEC.md 34 holds until something
 * takes it down, and the only thing that ever would is the guest toggling
 * again. So every way of STOPPING the machine has to come through here: the
 * silence timeout, CPU > Stop, a JAM, both resets, Power On, CPU > Warp (the
 * C64's rule, and VICE's - a machine at 3,000 % has nothing meaningful to
 * play), Machine > Mute, the About panel and the confirmation. That is the
 * C64's own list one machine along, and it is a list because a duration-0
 * tone that outlives its owner sounds on a desktop the user has gone back to.
 *
 * AND IT RE-ARMS RATHER THAN REMEMBERING. The interval ring is emptied, so a
 * resumed machine plays whatever it is ACTUALLY toggling three toggles later
 * rather than the note that was taken away - which for a program that changed
 * its loop while stopped is the only right answer, and which is the C64's
 * "re-read the current registers" with no registers to re-read. */
static void a2_sound_stop(void)
{
    if (a2_have_snd && a2_snd_hz)
        os88_snd_tone(0, 0, A2_SND_PRI);
    a2_snd_hz = 0;
    a2_snd_want = 0;
    a2_snd_tries = 0;
    a2_snd_odd = 0;
    a2_spk_fill = 0;
}

/* a2_snd_fact - THE STATED FACT (section 8), SAID ONCE AND ON THE ROW.
 *
 * It is not at launch, where it would be a sentence about a feature the user
 * has not reached, and NOT in the About panel, which carries what the port IS
 * and not how this build renders. 23 of the row's 26 cells.
 *
 * IT IS ARMED BY THE FAILURE AS WELL AS BY THE SUCCESS, and that was the
 * whole defect in the first version: the latch sat inside the successful
 * os88_snd_tone arm, so the sentence fired exactly when the emulation was
 * working and never when it was not. The programs the sentence EXISTS for -
 * a click track, Karateka-style waveform synthesis, a Mockingboard - produce
 * toggle intervals that do not agree, so a2_spk_hz answers 0, no tone is ever
 * asked for, and the row stayed blank for the one user who needed it. A
 * caller that saw the speaker TOGGLE and could not turn it into a tone says
 * it too, after A2_SND_ODD such wakes.
 *
 * AND IT IS NOT SPENT WHERE NOBODY CAN READ IT. a2_spk_service runs inside
 * the exclusive bracket as well as from the wake (rule 6 of a2_fsx_main's
 * list, read the right way round: the snd slots stay legal), and the status
 * row is not on the glass there - a2_msg_until would expire five seconds
 * later with the desktop still gone. The LATCH is left unspent, so the first
 * wake after the bracket says it. */
static void a2_snd_fact(void)
{
    if (a2_snd_said || a2_fsx_up)
        return;
    a2_snd_said = 1;
    a2_say("Square-wave tones only.");
}

/* a2_spk_service - ONE far call a wake, on a CHANGE only.
 *
 * Called from the wake AFTER the slice, so the intervals it reads are the
 * ones the emulated machine just made, and BEFORE the flush, so a note and
 * the picture that goes with it reach the user in the same wake. */
static void a2_spk_service(void)
{
    unsigned t;
    int hz, toggled;

    /* A MACHINE WITH NO SQUARE VOICE IS A DIFFERENT FACT FROM A BUSY ONE, so
     * it is a different sentence and it is not retried at all - it is asked
     * ONCE, in os88_main, and every machine this OS boots answers yes
     * (kernel/snd.inc ORs the bit in unconditionally). The guard is written
     * anyway, because a package that calls a slot it never established the
     * machine has is guessing (SPEC.md 73.11), and the harness drives it. */
    if (!a2_have_snd)
        return;

    t = os88_ticks();
    toggled = (a2_spk_n != a2_spk_last) ? 1 : 0;
    if (toggled) {
        a2_spk_last = a2_spk_n;
        a2_spk_tick = t;
    }
    /* Every way of being quiet, in one test each. The silence timeout is
     * first because it is the one that ends an ordinary beep. */
    if ((unsigned)(t - a2_spk_tick) >= A2_SPK_QUIET
        || a2_mute || a2_warp || a2_pause || a2_abt_up
        || a2_state != A2_ST_RUN) {
        if (a2_snd_hz)
            a2_sound_stop();
        a2_snd_odd = 0;                     /* a quiet speaker is not a
                                             * speaker this build failed to
                                             * follow */
        return;
    }
    hz = a2_spk_hz();
    if (hz < A2_SND_HZMIN || hz > A2_SND_HZMAX) {
        /* The intervals do not agree, or they imply a note outside the sink's
         * band. Either way nothing steady is being played, and a held tone
         * that outlives the loop that asked for it is what rule 3 is about.
         *
         * ...AND THIS IS WHERE THE STATED FACT IS EARNED. The speaker moved
         * (`toggled`) and no tone came of it, which is exactly the class of
         * program section 8 names: a click track, waveform synthesis, a
         * Mockingboard. Four consecutive such wakes and the row says what
         * this build can and cannot do, once. */
        if (a2_snd_hz)
            a2_sound_stop();
        if (toggled && ++a2_snd_odd >= A2_SND_ODD)
            a2_snd_fact();
        return;
    }
    a2_snd_odd = 0;
    if (hz == a2_snd_hz)
        return;                             /* already sounding: no call */
    /* ...AND "CHANGED" IS A BAND AND NOT A COMPARE, WHICH IS THE ONE PLACE A
     * MEASURED FREQUENCY IS NOT A REGISTER.
     *
     * The C64's rule is `one far call a wake, on a change only`, and there a
     * change is EXACT: the guest wrote a SID register and the number either
     * moved or it did not. Here the number is MEASURED off a toggle interval,
     * and an Applesoft loop's iterations differ by a cycle or two - a
     * page-crossed branch, a carry - so the estimate walks between
     * neighbouring hertz for ever. Every step of that walk is a far call at
     * 46.7 us, and every one of them also does `out 0x43`, which RESTARTS PIT
     * channel 2's count (kernel/snd.inc's spk_tone) in the middle of a note
     * nobody asked to change. A wake is not 18 Hz - a2_wants_wake re-posts
     * one for as long as the machine has anything to draw - so a running tone
     * loop would re-programme the timer some hundreds of times a second.
     *
     * A SIXTY-FOURTH IS INAUDIBLE AND THE WOBBLE IS SMALLER STILL, which is
     * why the band is narrow rather than the agreement window's eighth. One
     * cycle of interval at the 62 Hz an Applesoft `POKE -16336,0 : GOTO 10`
     * actually makes (8,230 emulated cycles a toggle - MEASURED on the glass)
     * moves the estimate by 0.008 Hz against a band of 0.97; at 1,000 Hz it
     * moves it by 2 against a band of 15. A glide still glides: 0.27 of a
     * semitone is under a fifth of what anyone can hear, where the eighth
     * this was first written with is 1.7 semitones and would have stepped a
     * siren. */
    if (a2_snd_hz) {
        int d = hz - a2_snd_hz;

        if (d < 0)
            d = -d;
        if (d <= (a2_snd_hz >> 6))
            return;
    }
    /* THE BOUND COUNTS CONSECUTIVE REFUSALS AND NOT REFUSALS OF ONE NOTE, and
     * the difference is the whole of whether the bound exists.
     *
     * `a2_snd_want != hz` restarts it, and after a refusal a2_snd_hz is 0 - so
     * the sixty-fourth band above is skipped and every wobble of the measured
     * estimate reads as a NEW note. The wobble is ~2 Hz at 1,000 (this file's
     * own measurement), so a refused high tone loop re-asked a busy kernel on
     * every wake for ever, never reached A2_SND_TRIES, and never said the
     * fact section 8 promises. The same band applied to a2_snd_want is what
     * makes "this note again" mean what it says while nothing is sounding. */
    {
        int d = hz - a2_snd_want;

        if (d < 0)
            d = -d;
        if (d > (a2_snd_want >> 6)) {
            a2_snd_want = hz;               /* a NEW note restarts the bound */
            a2_snd_tries = 0;
        } else if (a2_snd_tries >= A2_SND_TRIES) {
            return;                         /* eight wakes asked for THIS
                                             * note and were refused: dropped,
                                             * and the next note asks again */
        }
    }
    if (os88_snd_tone(hz, 0, A2_SND_PRI) == 0) {
        a2_snd_hz = hz;
        a2_snd_tries = 0;
        a2_snd_fact();                      /* the first time this machine
                                             * actually makes a noise */
    } else if (++a2_snd_tries >= A2_SND_TRIES && !a2_snd_busy_said
               && !a2_fsx_up) {
        /* ...AND NOT INSIDE THE BRACKET EITHER, for a2_snd_fact's reason: the
         * row is not on the glass and the message would expire unseen. The
         * latch is left unspent, so it is said on the first wake after. */
        a2_snd_busy_said = 1;
        a2_say("The speaker is busy.");
    }
}

/* ==========================================================================
 * THE WALL SLICE (APPLE2-SPEC section 4.3)
 * ========================================================================*/
/* THERE IS NO ALARM SCHEDULER AT ALL, and that is a real simplification this
 * port states rather than an omission (section 4.2). A bare II+ has no timers
 * and no interrupt source, so where the C64's slice loop is "run to the next
 * device event, service it, compute the next one", this one is
 * `r = a2_run(budget)` with no min() and no advance. The whole of the C64's
 * c64_alarm_next, c64_advance and the retained device phases are absent.
 *
 * The budget is a RAW CYCLE COUNT seeded from os88_cpu() and adapted only on
 * GENUINELY EXHAUSTED slices - a slice that ended early (jammed, stopped)
 * leaves the estimate alone, which is RUNCPM's lesson: without that rule
 * ordinary idling walks the budget to its cap and the next busy slice is a
 * second of stalled UI task. */
#define A2_SLICE_MIN 256                    /* ~250 us of emulated time */
#define A2_SLICE_MAX 16384                  /* ...and the cap. It is BELOW
                                             * 32,767 on purpose: the core's
                                             * countdown is a SIGNED word
                                             * (section 4.2), so a budget past
                                             * that arrives negative and the
                                             * core expires before its first
                                             * fetch - a machine stopped dead */
#define A2_SLICE_WARP 30000                 /* ...and CPU > Warp's, which is
                                             * the WHOLE of what warp is on
                                             * this port: there is no throttle
                                             * here to take off - a2_slice
                                             * runs a2_budget cycles a wake and
                                             * the status row reports what that
                                             * came to - so the only thing warp
                                             * can lift is the ceiling the
                                             * adaptation walks up to. It is
                                             * still below 32,767 for the
                                             * signed countdown's reason above */
static int a2_budget = A2_SLICE_MIN;

/* a2_slice_cap - the ceiling the adaptation may walk the budget up to, which
 * is the one place the warp latch is read. Two call sites in the wake, and
 * writing the test twice is how the two arms drift apart. */
static int a2_slice_cap(void)
{
    return a2_warp ? A2_SLICE_WARP : A2_SLICE_MAX;
}

/* a2_warp_set - CPU > Warp's latch, and THE CAP COMES BACK DOWN WITH IT.
 * An adapted budget above A2_SLICE_MAX would otherwise outlive the warp it was
 * granted for: the only thing that lowers the budget is a slice that overruns
 * a host tick, and one that already fits never would. It is RESIDENT and the
 * command in the overlay calls it, because a2_budget and the two ceilings
 * belong to the slice driver and a second copy of the ceiling in a2cmd.c is a
 * second ceiling. */
static void a2_warp_set(int on)
{
    a2_warp = on ? 1 : 0;
    if (!a2_warp && a2_budget > A2_SLICE_MAX)
        a2_budget = A2_SLICE_MAX;
    /* ...AND WARP SILENCES THE SPEAKER, which is VICE's rule (vsync.c:181
     * calls sound_suspend on the warp arm) and the C64's: a machine running
     * at some thousands of per cent has nothing meaningful to play, and the
     * intervals it produces are a tone that is not the tone the program
     * meant. a2_spk_service tests a2_warp too, so the note could not survive
     * the next wake either - this is what makes it stop on the PICK. */
    if (a2_warp)
        a2_sound_stop();
}

/* THE ADAPTATION IS A DUTY-CYCLE CONTROLLER, and section 4.3.1 is why.
 *
 * The rule this replaces doubled after FOUR consecutive slices that cost no
 * host tick and halved on ONE that did, and that asymmetry has an equilibrium
 * in it: a slice of length d crosses a tick boundary with probability d/55 (the
 * wake is posted and dispatched, so its phase against the tick drifts freely -
 * MEASURED, a 22.5 ms slice crossed on 0.40 of its wakes against the 0.41 the
 * model predicts).  Solving `(1-p)^4/4 = p` puts the walk's fixed point at
 * p = 0.14, so the budget sat at A2_SLICE_MIN for ever on any machine where a
 * slice is a real fraction of a tick - which is exactly the 4.77 MHz 8088.
 *
 * So the crossing RATE is measured instead of a run of luck being waited for,
 * and it is the duty cycle by definition: over a window of A2_ADAPT_N slices,
 * `a2_adx` of them crossed, and `a2_adx / A2_ADAPT_N` IS the share of a host
 * tick a slice is taking.  The target is A2_ADAPT_LO..A2_ADAPT_HI - six of
 * eight, ~75 % of a tick - and the two steps are the same size, so the fixed
 * point is the middle of the band rather than an artefact of the arithmetic.
 *
 * The old rule's FAST arm survives as `a2_adx == 0`: a window in which nothing
 * cost a tick at all is a machine the budget is nowhere near, and it doubles
 * exactly as it used to, so a 386 still reaches the cap in a fraction of a
 * second. */
#define A2_ADAPT_N   8                      /* slices in a window */
#define A2_ADAPT_LO  5                      /* <= this many crossings: raise */
#define A2_ADAPT_HI  7                      /* >= this many: lower */
static int a2_adn;                          /* slices in this window */
static int a2_adx;                          /* ...of them that cost a host tick */
static unsigned a2_clk;                     /* the emulated clock, a 16-bit
                                             * wrapping cycle count. It is the
                                             * paddles' clock and nothing
                                             * else's, and a2_now() is what
                                             * reads it from inside a run */

/* THE SPEED FIGURE (section 9) IS MEASURED AND NOT A GUESS - the honest-speed
 * posture this port was given at intake. 100 % is a 1.02 MHz Apple II, which
 * is 1,020,484 cycles a second, or 56,070 a host tick at 18.2 Hz.
 *
 * IT IS COUNTED IN 64-CYCLE UNITS WITH THE REMAINDER KEPT, because a second of
 * a real Apple II does not fit in the 16-bit int this C has: 1,020,484 / 64 is
 * 15,945, which does. The remainder is carried rather than dropped, so the
 * count is EXACT and not a truncation that loses up to 63 cycles a slice - on
 * a machine taking sixty slices a second that would be 0.4 % of the figure
 * being reported, drifting the wrong way.
 *
 * ...AND THE UNIT GROWS WHEN THE ACCUMULATOR WOULD NOT HOLD THE WINDOW, WHICH
 * IS THE FIX FOR A FIELD THAT SATURATED AND CALLED IT A MEASUREMENT. The first
 * version simply STOPPED counting at 60,000 units:
 *
 *     if (a2_c64u < 60000u) a2_c64u += a2_crem >> 6;
 *
 * With den = 876 x 18 / 100 = 157 for a one-second window, the largest per
 * cent that arithmetic can produce is 60000 / 157 = 382 - so 380 %, 400 %,
 * 1000 % and 3000 % all printed `383%`, the `pct > 9999` clamp below was
 * unreachable, and 383 is exactly what wave 2's own screendumps show on a host
 * the report itself described as running at some thousands of per cent. The
 * clamp's own comment named the failure it then committed: "a count that
 * wrapped would report a small plausible number". It did not wrap; it
 * saturated, and reported a small plausible number in silence.
 *
 * So the unit DOUBLES instead. a2_c64u counts units of `64 << a2_csh` cycles;
 * when it would pass 32,767 the count is halved and the shift goes up by one,
 * and a2_cru holds the 64-cycle units not yet folded in - so nothing is
 * dropped, at any speed. The fold below undoes the shift EXACTLY, quotient and
 * remainder both, which is why the arithmetic there is `(q << sh) + ((r << sh)
 * / den)` and not a division by a shifted denominator: `den >> 6` is 2 and
 * loses a third of the answer.
 *
 * The ceiling is now the CLAMP and nothing else: a2_csh stops at 6, so a
 * one-second window holds 32,767 x 4,096 = 134 million cycles - 13,300 % of a
 * 1.02 MHz Apple - and the clamp fires first, at 9,999 %. That is a number no
 * host emulating an 8086 can reach, which is the point: the field measures
 * everything a reader can actually produce. */
#define A2_CYC_TICK  876                    /* 56,070 / 64, per host tick */
#define A2_CSH_MAX   6                      /* the unit stops at 4,096 cycles */
static unsigned a2_c64u;                    /* units of (64 << a2_csh) cycles */
static unsigned a2_cru;                     /* 64-cycle units not yet folded */
static unsigned a2_crem;                    /* ...and the cycles left over */
static int a2_csh;                          /* the unit's shift, 0..6 */
static unsigned a2_sp_tick;                 /* the window's start */

static void a2_cyc_add(int ran)
{
    a2_crem += (unsigned)ran;
    a2_cru += a2_crem >> 6;
    a2_crem &= 63u;
    a2_c64u += a2_cru >> a2_csh;
    a2_cru = a2_cru & (unsigned)((1 << a2_csh) - 1);
    while (a2_c64u > 32767u && a2_csh < A2_CSH_MAX) {
        a2_c64u = a2_c64u >> 1;
        a2_csh++;
    }
    if (a2_c64u > 32767u)                   /* a2_csh is at its cap: 13,300 %
                                             * of an Apple in one second, and
                                             * the clamp below has already
                                             * said 9,999. Stopping here
                                             * cannot make the field read low,
                                             * because the clamp is under it */
        a2_c64u = 32767u;
}

static void a2_cyc_zero(void)
{
    a2_c64u = 0;
    a2_cru = 0;
    a2_crem = 0;
    a2_csh = 0;
}

/* a2_speed_fold - the window, folded once a second. */
static void a2_speed_fold(void)
{
    unsigned t, el, den, q, rr;
    int pct;

    t = os88_ticks();
    el = t - a2_sp_tick;
    if (el < 18)                            /* ~1 s at 18.2 Hz */
        return;
    if (el > 36) {                          /* the wakes stopped: this window
                                             * measures nothing, so it is
                                             * restarted and the figure stands */
        a2_sp_tick = t;
        a2_cyc_zero();
        return;
    }
    den = (A2_CYC_TICK * el) / 100u;        /* one per cent of the window, in
                                             * 64-cycle units. `876 * el` is
                                             * at most 31,536 and fits, and el
                                             * >= 18 makes den >= 157 */
    if (den < 1u)
        den = 1u;
    /* THE SHIFT IS UNDONE ON BOTH HALVES OF THE DIVISION. q << 6 is at most
     * 208 << 6 = 13,312 and (r << 6) is at most 314 << 6 = 20,096, so both
     * fit the 16-bit unsigned this C has. */
    q = a2_c64u / den;
    rr = a2_c64u % den;                     /* `%` and not `c64u - q * den`:
                                             * cc8086 refuses a multiply whose
                                             * scratch it cannot prove dead
                                             * (LESSONS.md 3), and the divide
                                             * hands both halves back anyway */
    pct = (int)((q << a2_csh) + ((rr << a2_csh) / den));
    if (pct > 9999)
        pct = 9999;                         /* REACHABLE, and only above
                                             * 9,999 % - the accumulator holds
                                             * 13,300 % of a one-second window */
    a2_sp_tick = t;
    a2_cyc_zero();
    if (pct != a2_pct) {
        a2_pct = pct;
        a2_st_dirty = 1;
        a2_menu_state();                    /* CPU > `Normal: 1MHz` is MARKED
                                             * FROM THIS FIGURE, MII's own rule
                                             * (a2menu.c) - so the mark moves
                                             * when the measurement does */
    }
}

/* a2_hex4 - four hex digits, because every address this program has to name is
 * one and os88_utoa is decimal. */
static void a2_hex4(char *d, unsigned v)
{
    int i, n;

    for (i = 0; i < 4; i++) {
        n = (int)((v >> 12) & 0x0F);
        d[i] = (char)((n < 10) ? ('0' + n) : ('A' + n - 10));
        v = v << 4;
    }
}

/* a2_jam - a JAM opcode. The machine stops and GOES ON SAYING SO.
 *
 * On a real Apple II+ the recovery is Ctrl-Reset, which is exactly what this
 * port offers: a2_reset_cpu CLEARS THE JAMMED STATE (section 4.5), and the
 * menu item is the guaranteed route to it.
 *
 * THE LINE IS THE PORT'S OWN, and section 4.5 pins it. A grep of all three
 * reference trees returns no JAM string at all - a jammed 6502 is not a thing
 * MII, AppleWin or apple2emu says on the glass - so this is modelled on
 * VICE's `Main CPU: JAM at $%04X` (src/maincpu.c:612), the string
 * apps/c64/c64.c carries, with `6502` for the CPU because this machine has
 * one processor and no reason to call it the main one. 18 glyphs into a
 * 42-cell row.
 *
 * IT DOES NOT GO THROUGH a2_say, and a2_jamline's own declaration above says
 * why: a message expires and this condition does not. a2_status draws it as a
 * permanent row state until a2_reset_cpu takes A2_ST_JAM off. */
static void a2_jam(void)
{
    a2_state = A2_ST_JAM;
    a2_sound_stop();                        /* a dead 6502 holds no note */
    os88_strcpy(a2_jamline, "6502: JAM at $", 15);
    a2_hex4(a2_jamline + 14, a2_m.pc);
    a2_jamline[18] = 0;
    a2_dirty_any = 1;                       /* ...or the wake's flush gate
                                             * never opens and the row keeps
                                             * saying what a running machine
                                             * says (c64.c's own note) */
    a2_st_dirty = 1;
    /* SPEC.md 59's second route: the status row is UNDER a WF_FULL window and
     * this machine spends time there.
     *
     * ...AND NOT INSIDE THE EXCLUSIVE BRACKET, WHICH IS RULE 2 (section 13.3).
     * a2_slice is called from a2_fsx_main and a JAM is what a wild jump
     * reaches, so this is the one drawing slot the bracket can arrive at -
     * and a toast is not deferred: toast_now draws the bar and the panel on
     * the spot when the gfx lock is held by task 0, which is precisely the
     * state a bracket is in. Nothing is lost by the fence: a2_jamline is a
     * PERMANENT row state rather than a message, so the exit's own repaint
     * says it, and it says it where the user can read it. */
    if (!a2_fsx_up)
        os88_toast(a2_jamline, 0);
    a2_menu_state();                        /* Stop/Continue re-spelled: there
                                             * is no machine left to stop */
}

/* a2_slice - one run of the core for the wall budget.
 *
 * `a2_m.cnt` is what was NOT spent and is negative by up to one instruction's
 * cost, so `ran = asked - cnt` is exact (section 4.2). */
static void a2_slice(void)
{
    int r, ran;

    a2_clk_set(a2_clk + (unsigned)a2_budget);
    r = a2_run((unsigned)a2_budget);
    ran = a2_budget - (int)a2_m.cnt;
    if (ran < 1)
        ran = 1;
    a2_clk = (unsigned)a2_now();
    a2_cyc_add(ran);
    if (r == A2_RUN_JAM)
        a2_jam();
}

/* a2_reset_service - the reset latches, RUN FROM THE WAKE.
 *
 * The difference between the three is what happens to RAM and to the Autostart
 * Monitor's PWREDUP check, and this port keeps it: Ctrl-Reset touches neither,
 * its Open-Apple form forces the check to MISMATCH so the ROM takes the cold
 * path, and Power On lays the whole power-on pattern first.
 *
 * IT IS HERE AND NOT IN THE COMMAND because the fill is 48KB and os88_oncmd
 * holds the desktop's gfx lock. Nothing about the result changes: the 6502
 * advances only inside a wake and this runs at the top of one, so the machine
 * being reset is the machine the user was looking at. */
static void a2_reset_service(void)
{
    int kind = a2_reset_req;

    a2_reset_req = 0;
    a2_sound_stop();                        /* ...and the note the old machine
                                             * was holding, which nothing else
                                             * will ever take down (section
                                             * 8): a Ctrl-Reset in the middle
                                             * of a tone loop is exactly how a
                                             * duration-0 grant outlives its
                                             * owner */
    /* A RESET EMPTIES THE PASTE QUEUE, which is VICE's kbdbuf_abort one
     * machine along (apps/c64/c64kbd.c's c64_paste_stop) and is right here
     * for a plainer reason: the bytes are being typed at a MACHINE, and after
     * a Power On it is not that machine any more. Without it a cold boot
     * carries on typing the previous session's listing into the new one, and
     * the 2KB claim is held until it finishes. */
    a2_paste_stop();
    if (kind == A2_RST_POWER) {
        a2_power_on();
    } else {
        if (kind == A2_RST_OACTRL) {
            /* The //e's Open-Apple-Ctrl-Reset forces a COLD start, and on a
             * II+ the Autostart Monitor decides that from $03F4 != $03F3 XOR
             * $A5 (section 4.5). So the honest II+ body of MII's row is to
             * break that equality and take the ordinary reset - RAM intact,
             * which is what makes it a different row from Power On. */
            a2_wr(0x03F4, (a2_rd(0x03F3) ^ 0xA5 ^ 0xFF) & 0xFF);
            /* ...AND PB0 IS HELD ACROSS IT, which is the chord's name read in
             * II+ terms (section 6.3). What a //e calls Open-Apple IS the PB0
             * input on this machine, so Ctrl+F3 asserts it for the reset the
             * way holding the key would, and a program that samples $C061 in
             * its start-up sees it. The poll at the top of the wake has
             * already run, which is why this is not overwritten a moment
             * later. */
            a2_btn[0] = 1;
        }
        a2_reset_cpu();
    }
    a2_watch_page();
    a2_menu_state();                        /* a route out of A2_ST_JAM: the
                                             * greying may not outlive the fact
                                             * (SPEC.md 47) */
    a2_dirty_all();                         /* ...and NOT a2_sh_inval(). Nothing
                                             * covered the glass across a
                                             * reset, so the shadow is still
                                             * true and this is the RECOMPOSE;
                                             * forcing would switch off the
                                             * frame compare that answers "the
                                             * picture did not change" */
    a2_dirty_any = 1;
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

/* W_PAINT. THE KERNEL ARMS A CLIP REGION FOR THIS CALLBACK AND FOR NOTHING
 * ELSE (SPEC.md 11.3), and the damage rect it hands us is what we owe.
 *
 * AND IT IS ASKED FOR. WF_OWNBG is set (os88_wm_ownbg below), so the kernel
 * did not whiten the content and os88_wm_damage() says which part needs
 * drawing. Invalidating the whole shadow on every W_PAINT instead forced all
 * 192 scan lines - 24 rows composed, 24 blits, five fills and 192 span
 * compares, the 496.8 ms of section 7.9.1 - under the desktop's gfx lock, so
 * opening and closing a pull-down over this window (kernel/menu.inc closes
 * one through wm_paint_dmg) or dragging another window across a corner of it
 * was half a second of stopped desktop. apps/c64/c64.c's os88_paint is the
 * named precedent. */
void os88_paint(void *win)
{
    static struct os88_rect d;
    int whole;

    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return;                             /* nothing of us is exposed */
    if (a2_geom(win) < 0)
        return;
    a2_covered = 0;                         /* something of us IS exposed -
                                             * that is what a W_PAINT means */

    if (whole || !a2_sh_ok)
        a2_sh_inval();
    else
        a2_blank_rect(d.x1, d.y1, d.x2, d.y2);

    /* THE HOLD RANGE IS RECOMPUTED FROM THE FRESH GEOMETRY, BEFORE THE FLUSH
     * READS IT. a2_hold_l0/l1 were left by the LAST ovl_about_geom, and
     * OSAPI_FULLSCREEN repaints the window whole and SYNCHRONOUSLY - so this
     * paint runs nested inside os88_fullscreen() with the old rect still in
     * those two words while the content box under it has just changed size.
     * The flush would then hold rows against a rectangle that is not there
     * (a full-width strip of stale pixels under WF_OWNBG) and draw rows the
     * panel is about to be painted over (PERFORMANCE.md rule 2). Measuring
     * first costs two divisions and makes both impossible.
     *
     * AND IT IS AN OVERLAY CALL, SO IT IS FENCED AND ITS ANSWER IS READ.
     * ovl_about_geom lives in APPLE2.OVL; a W_PAINT arrives under the
     * desktop's gfx lock, which is the whole machine stopped, so it may not
     * go to the floppy to resolve the module (a2_ovl_ready, section 15.5) -
     * and an `ovl_` that answers 0 has NOT RUN, which for this one means the
     * hold range and the panel's rect are both stale. Either way the card is
     * not going to be redrawn, so it comes DOWN: the latch cleared, the hold
     * range emptied so the flush stops skipping rows for a panel that is not
     * on the glass, and the rect it held handed on as damage before the flush
     * below draws it. */
    if (a2_abt_up && (!a2_ovl_ready(win) || !ovl_about_geom()))
        a2_about_gone();

    a2_flush(win);
    /* THE PANEL OWNS ITS ROWS AND THE FLUSH SKIPPED THEM, so it is redrawn
     * here - BUT ONLY WHEN THE DAMAGE ACTUALLY REACHES IT. Redrawing it on
     * the latch alone cost 1 fill + 2 frames + 8 font_run over 196 glyph
     * cells - ~185 ms - on EVERY expose while it was up, which is more than
     * the 122 ms the hold rows exist to save: a pull-down closing over one
     * corner of the window repainted the whole panel, and a partial expose
     * with the panel up became more expensive than one without it. The four
     * statics are live from the ovl_about_geom above (or from
     * ovl_about_show). apps/c64/c64.c's os88_paint is the precedent.
     *
     * THE RANGE IS **NOT** RESET AFTERWARDS, which is where this port and the
     * C64 part company: c64's os88_onwake returns early while its panel is up
     * and ours does not - the machine keeps running behind the card - so the
     * hold range has to stay true between paints or the next wake's flush
     * composes and blits straight through the panel. a2_about_close is what
     * empties it, and it forces exactly those lines on the way out. */
    if (a2_abt_up
        && (whole || (d.x1 <= a2_abt_x + a2_abt_w - 1 && d.x2 >= a2_abt_x
                      && d.y1 <= a2_abt_y + a2_abt_h - 1
                      && d.y2 >= a2_abt_y))
        && !ovl_about_paint(win)) {
        /* ...AND THE SAME ANSWER ONE CALL LATER. The module can go between
         * the geom above and this - a compaction, a claim that could not be
         * made - and a refusal here means the panel's pixels were never
         * drawn. The rows it held are then owed to the next wake, which is
         * what a2_kick posts. */
        a2_about_gone();
        a2_kick = 1;
    }
    /* ...AND THE FIRST WAKE HAS TO BE ASKED FOR. os88_wm_onwake INSTALLS the
     * handler; os88_wm_wake POSTS the kick, and without this line the machine
     * sits at its reset vector until the user presses a key - and then runs
     * perfectly, which is what makes it look like a reset bug rather than a
     * wake that was never started (LESSONS.md 13, and RUNCPM posts its first
     * wake from its own paint for the same reason). */
    if (a2_kick)
        os88_wm_wake(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    if (a2_state == A2_ST_DEAD)
        return;

    /* THE FULLSCREEN CHORD IS THE FIRST THING TESTED, AND IT IS RESIDENT.
     * kernel/wm.inc draws NO chrome for a `WF_FULL` window - no menu bar - so
     * the menu item that got the user in is not on the glass any more, and a
     * fullscreen window with no chord is a machine with no way back. Wave 1
     * made Machine > Toggle Fullscreen LIVE and scheduled the chords for wave
     * 3, which is a ONE-WAY DOOR: found on the glass, fullscreen entered and
     * `f`, `F`, Esc, Ctrl+F and Alt+Enter all dropped by a2_key(). SPEC.md
     * 11.2.1 is binding on this and C64-SPEC 9.8's paragraph says the same
     * thing one machine along: "that exception only pays for itself if the
     * chord is IMPLEMENTED".
     *
     * The two are APPLE2-SPEC section 6.3's, already pinned there: Alt+Enter
     * is AppleWin's own (`help/keyboard.html`; an Apple II+ has no Alt key,
     * so it collides with nothing) and Ctrl+F is 11.2.1's unconditional door,
     * kept. Ctrl+F arrives as ASCII 6 on every BIOS, which is why it is the
     * one that is not waiting on wave 7's iron - Alt+Enter's scan code is
     * 0x1C in the classic set and 0xA6 in the enhanced one, and BOTH are
     * accepted rather than one being guessed.
     *
     * AND **Esc IS NOT BOUND**, WHICH IS SPEC.md 11.2.1'S STATED EXCEPTION
     * TAKEN THE WAY THE C64 TOOK IT (apps/c64/c64.c's os88_onkey, C64-SPEC
     * 9.8). 11.2.1 binds the bare letter `f` and Esc to enter and leave a
     * fullscreen surface - "the key that got you there is the key that leaves"
     * - and **the Apple II+ owns both**. Esc is a key on its keyboard: the
     * Monitor's ESC-I/J/K/M move the cursor and the Applesoft screen editor
     * reads the same four, so a port that swallowed Esc would be a machine
     * whose screen editor does not work. `f` is a letter, and every letter
     * goes to the machine. So Ctrl+F and Alt+Enter are the WHOLE door, and
     * APPLE2-SPEC section 6.3's chord table carries Esc as a row of its own
     * saying exactly this.
     *
     * The pair is ahead of the About panel deliberately: 11.2.1's door is
     * unconditional, and a modal panel that swallowed it would be the same
     * trap with one more step in it. */
    if ((ascii == 6)
        || (ascii == 0 && (scan == KSC_ENTER || scan == KSC_ALT_ENTER))) {
        a2_fullscreen_toggle(win);
        return;
    }

    if (a2_abt_up) {                        /* the panel is modal: nothing
                                             * reaches the machine while it is
                                             * up */
        if (a2_pan_kind == A2_PAN_CFM) {
            /* AppleWin's box is MB_YESNO and Yes is its default button, but
             * ANY KEY MEANS YES is not a thing to do to a row whose second
             * line is `(All data will be lost!)`. Enter and Y answer yes;
             * every other key - Esc, N, a letter typed at a machine the user
             * had forgotten was behind a card - answers NO and dismisses. A
             * confirmation nobody can dismiss by accident is the whole point
             * of having one. */
            a2_panel_close(win, (ascii == 13 || ascii == 'y' || ascii == 'Y')
                                ? 1 : 0);
            return;
        }
        a2_about_close(win);
        return;
    }
    a2_key(ascii, scan, win);
    a2_kick = 1;
    os88_wm_wake(win);
}

void os88_onclick(int x, int y, void *win)
{
    if (a2_state == A2_ST_DEAD)
        return;
    if (a2_abt_up) {
        if (a2_pan_kind == A2_PAN_CFM) {
            /* THE HIT TEST IS RESIDENT and the RECTS ARE THE OVERLAY'S, which
             * is the split working rather than an exception to it: a click is
             * a callback and a callback is reached by a near offset
             * (a2about.c's header), while the two button rects are statics
             * the drawing code wrote and every static stays resident.
             *
             * A click anywhere else - on the panel, on the picture, on the
             * border - answers NO and dismisses, for the same reason Esc
             * does. The one thing a data-loss box must not do is stay up with
             * no obvious way out. */
            a2_panel_close(win,
                           (y >= a2_cfm_by && y < a2_cfm_by + A2_CFM_BH
                            && x >= a2_cfm_bx[0]
                            && x < a2_cfm_bx[0] + A2_CFM_BW) ? 1 : 0);
            return;
        }
        a2_about_close(win);
        return;
    }
    (void)x;
    (void)y;
    /* A CLICK KICKS THE SLICE DRIVER, so a wake the event ring refused cannot
     * park a running machine (SPEC.md 74.1). */
    a2_kick = 1;
    os88_wm_wake(win);
}

/* The kernel's name pull-down. It arrives UNDER the gfx lock and with NO clip
 * region armed (SPEC.md 11.3), which is why the panel arms its own. */
void os88_about(void *win)
{
    if (!a2_ovl_ready(win))
        return;
    a2_pan_kind = A2_PAN_ABOUT;             /* the kernel's name pull-down
                                             * always means the About panel;
                                             * a confirmation that was
                                             * dismissed left this ABOUT
                                             * already, and setting it here is
                                             * what makes that true even if
                                             * some later route forgets */
    ovl_about_show(win);
}

/* THE FILE COMMAND'S LATCH (APPLE2-SPEC section 12), and the reason it is one.
 *
 * os88_onfile arrives UNDER THE DESKTOP'S GFX LOCK - kernel/fdlg.inc:45-48
 * states it of fdlg_open, the window procs and fdlg_commit alike - and
 * ovl_a2_prog is up to six os88_mem_claims (each of which may COMPACT an
 * arena this package has a pinned 64KB in), a floppy read of up to 46 KB
 * (os88.h prices 116KB at ~ten seconds of motor), the two-pass walk and a
 * 48KB-capable block move; Save is the same shape with os88_file_write_seg,
 * which os88.h flags as stalling every painter for its duration. Run inline
 * that is SECONDS of frozen pointer, frozen dock and every other task's
 * painter blocked in os88_gfx_lock, on the very target this port is for.
 *
 * It also defeated the a2_ovl_ready fence one line above it: the fence exists
 * so a LOCKED caller does not go to the floppy for the 4 KB overlay, and
 * the next line then read 46 KB under the same lock.
 *
 * So the handler copies the name - THE KERNEL REUSES fdlg_name, so the
 * pointer may not be kept - stores the mode and the size, and posts the wake.
 * The wake spends it beside the launch document's arm, which has always taken
 * exactly this route and is the working proof of it. */
static char a2_fname[13];                   /* 8.3 with the dot and the NUL */
static int a2_fmode;
static unsigned a2_fsize;
static int a2_fileq;

void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win)
{
    if (mode == OS88_FDLG_OPEN && size_hi) {    /* a file this machine's 48K
                                             * cannot hold, refused by SIZE
                                             * before anything is claimed -
                                             * and this IS free under the
                                             * lock: one compare */
        a2_say("Too large for a 48K Apple.");
        return;
    }
    os88_strcpy(a2_fname, name, sizeof(a2_fname));
    a2_fmode = mode;
    a2_fsize = size_lo;
    a2_fileq = 1;
    a2_kick = 1;
    os88_wm_wake(win);
}

/* ==========================================================================
 * THE WAKE - the one callback dispatched WITHOUT the gfx lock (SPEC.md 74.1)
 * ========================================================================*/
/* WAVE 2 PUT THE SLICE IN, and nothing above or below it moved: the latches
 * are still spent at the top with no lock held, the overlay probe is still on
 * the FIRST wake (the .OVL cannot be resolved from os88_main - there is no
 * instance yet to resolve a module for, LESSONS.md 13), the flash phase is
 * still the timer's, and the flush is still under the lock and only around
 * itself, at most once per host tick.
 *
 * THE SLICE IS BETWEEN THE PROBE AND THE FLUSH, which is the order that
 * matters: what the 6502 wrote in this wake is what the flush composes in the
 * same one, so a keystroke's echo is one wake and not two. */

/* a2_wants_wake - SPEC.md 74.1's rule in ONE place: "a handler re-posts
 * itself only while it has work". A wake round trip is at least one task
 * switch (693 us), so a handler that always re-posts spins the SHARED UI task
 * at ~1,400 round trips a second - and kernel/ui.inc dispatches every
 * window's events on that task, so the desktop's own menu tracking and window
 * drags queue behind it. apps/c64/c64.c's c64_wants_wake is the precedent.
 *
 * THE FLASH PHASE IS NOT IN HERE AT ALL, AND THAT IS THE POINT. Two arms
 * were tried and both are a constant 1. `a2_fl_ok` is initialised to 1 and
 * only Machine > Flashing text ever clears it; `a2_any_flash()` looks like a
 * real question and is not, because the Apple's cursor IS a flashing space
 * ($60, inside $40-$7F), so a2_flrow[] is permanently non-zero on any screen
 * a user is looking at and the Autostart Monitor writes one from wave 2 on.
 * Either way the handler re-posted for ever - ~1,400 round trips a second of
 * the SHARED UI task, 76 of every 77 of them doing nothing but a2_wrote() and
 * os88_ticks() - to service a phase that flips 3.6 times a second. And it got
 * WORSE in wave 2, not better: `a2_state == A2_ST_RUN` is false for a machine
 * the user pressed Stop on, so the flash arm would keep a deliberately
 * stopped machine spinning, which is c64_wants_wake's "THE PAUSE IS THE HALF
 * THAT WAS MISSING" one machine along.
 *
 * A PERIODIC HEARTBEAT HAS A FIRST-CLASS SLOT HERE: OSAPI_WM_TIMER (SPEC.md
 * 13.9), whose own os88.h comment names "a blinking caret" as the use.
 * os88_ontimer below flips the phase 3.6 times a second and kicks a wake for
 * the flush, and this answers 0 for an idle machine. The polling arm survives
 * as the SECOND PATH the slot's contract demands - kern_small carries
 * WM_TIMER's slot and not its body, os88_wm_timer() hands that back as -1,
 * and a2_tmr_ok is that tested fact rather than a guess (SPEC.md 47).
 *
 * AND A MESSAGE IS NOT WORK, on c64.c's own finding: gating on
 * `a2_msg[0] != 0` re-posts for the whole five-second life of every message
 * with nothing to do inside the wake but re-read os88_ticks(). The deadline
 * is examined at the TOP of a2_flush instead, so a standing message comes
 * down on the next thing the user does - a key, a click, a menu pick or an
 * expose, every one of which flushes. */
static int a2_wants_wake(void)
{
    /* ...AND A WINDOW NOTHING SHOWS OF CANNOT DRAW ITS WAY OUT OF THE DIRTY
     * STATE. clip_set answered -1, the flush was skipped, and every one of
     * these four flags is still set - so asking again is asking 1,400 times a
     * second to be told no. The work is still OWED; W_PAINT is what comes and
     * asks for it, and it clears this. */
    if (a2_covered)
        return (a2_state == A2_ST_RUN && !a2_pause && !A2_CFM_UP()) ? 1 : 0;
    if (a2_dirty_any || a2_border_dirty || a2_st_dirty || !a2_sh_ok)
        return 1;
    if (a2_copy_req || a2_paste_req || a2_fileq)
        return 1;                           /* a latch is a wake's worth of
                                             * work by definition */
    /* **AND a2_argp IS NOT ONE OF THEM, WHICH IS A FIX RATHER THAN A TIDY.**
     * The launch document's arm is a WAIT for the ROM to reach `]`, and it
     * used to sit in the line above - so a machine that cannot ever satisfy
     * it re-posted a wake at full rate for the whole 60-tick deadline. Three
     * such machines are reachable and one was added in this very wave: a
     * JAMMED machine (a2_state == A2_ST_JAM), a machine the user stopped with
     * CPU > Stop, and one sitting behind the Power On confirmation - which
     * A2_CFM_UP() exists precisely to stop re-posting for. Each of those wakes
     * did two a2_rd16s and posted another: the 100 % spin SPEC.md 8.1.2 exists
     * to remove, on the SHARED UI task, starving every other window for a
     * minute. What the arm is waiting for is the 6502, so it rides the 6502's
     * own arm below and a stopped machine idles. */
    if (a2_state == A2_ST_RUN && !a2_pause && !A2_CFM_UP())
        return 1;                           /* WAVE 2: and a paused machine
                                             * answers 0 here, which is the
                                             * half c64 had to go back for -
                                             * WAVE 4 gave the user a way to
                                             * pause one (CPU > Stop), so the
                                             * arm is now reachable */
    if (a2_tmr_ok)
        return 0;                           /* the phase is the timer's */
    return (a2_fl_ok && a2_any_flash()) ? 1 : 0;
}

/* a2_flash_step - the phase, and the ONE place it moves. Called from the
 * timer when the kernel has one and from the wake's poll when it does not, so
 * the two arms cannot drift into two flash phases. Answers 1 when the flip
 * happened, which is what tells the caller a flush is owed. */
static int a2_flash_step(unsigned t)
{
    if (!a2_fl_ok || (unsigned)(t - a2_fl_tick) < A2_FLASH_TICKS)
        return 0;
    a2_fl_tick = t;
    a2_fl_phase ^= 1;
    a2_flash_force();
    return 1;
}

/* W_ONTIMER (SPEC.md 13.9) - the flash phase's heartbeat, and NOT a draw.
 *
 * It arrives in W_ONCLICK's environment (UI task, gfx lock HELD, billed to
 * us), so it could draw - and it deliberately does not: the flush's pacing is
 * one per host tick and lives in the wake, which holds no lock, and a flush
 * taken from here would be a second pacing rule to keep in step with the
 * first. What it does is flip the phase, force the lines that changed pixels
 * without a memory write, and post the wake that draws them.
 *
 * IT IS ONE-SHOT AND IT RE-ARMS ITSELF (os88.h). It also fires while we are
 * MINIMIZED, which costs 24 byte tests and a wake that finds a2_geom's
 * refusal - the same thing the poll would have cost, 3.6 times a second
 * instead of 1,400. */
void os88_ontimer(void *win)
{
    if (a2_state == A2_ST_DEAD)
        return;
    if (a2_flash_step(os88_ticks())) {
        a2_kick = 1;
        os88_wm_wake(win);
    }
    /* AND IT IS NOT RE-ARMED WHILE THE FEATURE IS OFF. Machine > Flashing
     * text is what arms it again (a2menu.c), so a user who turned the phase
     * off is charged nothing at all for it - not even a callback every five
     * ticks. */
    if (a2_fl_ok && os88_wm_timer(win, A2_FLASH_TICKS) != 0) {
        a2_tmr_ok = 0;                      /* it was armed and now refuses:
                                             * the wake's poll takes it back
                                             * over rather than the phase
                                             * simply stopping */
        a2_kick = 1;
        os88_wm_wake(win);
    }
}

void os88_onwake(void *win)
{
    unsigned t;

    if (a2_state == A2_ST_DEAD)
        return;

    /* --- the latches, spent with NO LOCK HELD -----------------------------
     * os88_oncmd runs under the DESKTOP's gfx lock, and a command whose body
     * is a heap claim, a floppy read or a 48KB fill is the whole desktop
     * stopped for an unbounded operation. So the command LATCHES and this is
     * where the work happens (the C64's wave-3 lesson). */
    if (a2_exit_req) {
        a2_state = A2_ST_DEAD;
        os88_wm_close(win);                 /* SPEC.md 75.2: the kernel's own
                                             * close path, no task needed */
        return;
    }

    /* --- THE LEVEL HALF OF THE KEYBOARD (APPLE2-SPEC section 6.3, 6.6) ----
     * The two game buttons and the keyboard-mouse message, from ONE pass of
     * the down-map. It is here rather than inside the soft switch because a
     * button is a LEVEL a game reads in a loop: the BUTTONS are two bridge
     * crossings a wake against two per emulated read of $C061. (The message's
     * own five reads are behind its one-shot and stop the moment it is spent
     * or the user types - a2_kbd_poll says why.)
     *
     * IT IS AHEAD OF THE RESET SERVICE, which is what lets Ctrl+F3 hold PB0
     * ACROSS the reset it asks for (section 6.3): the poll would otherwise
     * overwrite the button a moment after the chord set it. */
    a2_kbd_poll();

    if (a2_reset_req)
        a2_reset_service();

    /* --- the overlay probe, on the FIRST wake ----------------------------- */
    if (!a2_ovl_asked) {
        a2_ovl_asked = 1;
        a2_ovl_res = ovl_a2_init();
        if (!a2_ovl_res) {
            /* THE ROW **AND** THE TOAST, and they are two audiences rather
             * than one message said twice: under a WF_FULL window there is no
             * desktop for a toast to land on (section 9), and a user who
             * never opens a menu still sees the row. SPEC.md 59's second
             * route is exactly this pair.
             *
             * THE ROW IS SAID EVERY TIME AND THE TOAST ONCE (a2_ovl_told's
             * own comment), because a2_ovl_ready re-arms this probe on every
             * menu pick - so the toast was repeated per pick for a fact that
             * had not changed.
             *
             * ...AND THE TOAST NAMES WHAT ACTUALLY REFUSES. `the menu
             * commands will refuse` over-claimed: File > Quit, Machine >
             * Toggle Fullscreen and Machine > Flashing text are answered in
             * the RESIDENT half and work on a disk with no module at all
             * (ovl_a2_cmd's header lists the three). */
            a2_say("Unable to load APPLE2.OVL.");
            if (!a2_ovl_told) {
                a2_ovl_told = 1;
                /* **TOAST_MAX IS 24 CHARACTERS** (`kernel/toast.inc:85`),
                 * and a longer one is TRUNCATED rather than refused. This
                 * said `APPLE2: no APPLE2.OVL beside the program - the menu
                 * commands will refuse` and reached the glass as
                 * `APPLE2: no APPLE2.OVL be` - 76 characters of which a user
                 * could read 24, with the consequence in the half that was
                 * cut. Photographed, not reasoned about. The status ROW has
                 * 26 cells and carries the rest. */
                os88_toast("APPLE2: no APPLE2.OVL", 0);
            }
        }
    }

    /* --- THE LAUNCH DOCUMENT, SPENT HERE (SPEC.md 54.10, section 12) ------
     * Below the overlay probe deliberately: the loader is an ovl_ and a disk
     * with no APPLE2.OVL has to REFUSE this the same way it refuses File >
     * Load Program..., on the row, rather than launching and going quiet.
     *
     * AND THE `goto` GETS ITS OWN ARM. It answers -1 when the folder could
     * not be listed, and with no arm the whole launch failed in silence - the
     * window came up at `]` and a double-click looked like it had done
     * nothing, which is the trap cword.c:2650 records one package along. */
    if (a2_argp) {
        /* **IT WAITS FOR THE MACHINE TO REACH `]` FIRST, AND THAT IS THE
         * WHOLE OF WHY THIS IS NOT ONE LINE.** The first wake happens the
         * moment the window is on the glass, and at that point the 6502 has
         * run a few hundred cycles: the Autostart Monitor has not handed over
         * to Applesoft yet, and Applesoft's cold start ENDS IN A `NEW` - it
         * writes TXTTAB itself and clears the program. A load spent on the
         * first wake lands in $0801 and is wiped a moment later by the ROM,
         * and the glass shows a `]` prompt whose LIST is empty with nothing
         * saying why. That is exactly what the first cut of this did, and it
         * is invisible in any screendump that does not type LIST.
         *
         * THE CONDITION IS THE COLD START'S OWN OUTPUT and not a wall-clock
         * guess: `TXTTAB` = $0801 and `VARTAB` >= $0803 is what Applesoft's
         * NEW leaves (measured on the glass -
         * `PRINT PEEK(103)+PEEK(104)*256` reads 2049 and PEEK(105)/(106)
         * reads 2051), and before it the power-on pattern has $67/$68 =
         * $00/$FF. A wall-clock wait would be wrong on both ends: the target
         * runs the Apple at a few per cent of its own speed, so a cold start
         * that is one emulated second is tens of wall seconds there and a
         * fraction of one here.
         *
         * The wait is BOUNDED, because a machine that never gets to `]` must
         * not leave a load armed for the rest of the session - it would then
         * fire on the user's own NEW. */
        if (a2_rd16(A2_TXTTAB) != A2_PROG
            || a2_rd16(A2_VARTAB) < (unsigned)(A2_PROG + 2)) {
            if ((unsigned)(os88_ticks() - a2_argdl) < 0x8000u) {
                a2_argp = 0;
                a2_say("No ] prompt to load into.");
            }
        } else {
            a2_argp = 0;
            /* ...AND THE `goto` GETS ITS OWN ARM. It answers -1 when the
             * folder could not be listed, and with no arm the whole launch
             * failed in silence - the window came up at `]` and a
             * double-click looked like it had done nothing, which is the trap
             * cword.c:2650 records one package along. */
            if (os88_file_goto(&a2_argplace) != 0)
                a2_say("Cannot open that folder.");
            else if (a2_ovl_ready(win))
                ovl_a2_prog(OS88_FDLG_OPEN, a2_argname, 0, win);
        }
    }

    /* --- FILE > LOAD PROGRAM... / SAVE PROGRAM..., SPENT HERE (section 12) -
     * The picker's answer arrives at os88_onfile UNDER THE GFX LOCK and that
     * handler only latches; this is the same lock-free route the arm above
     * takes, and os88_onfile's own header carries the arithmetic. The overlay
     * fence is asked HERE, where a 400 ms floppy read for the module costs
     * nobody else anything. */
    if (a2_fileq) {
        a2_fileq = 0;
        if (a2_ovl_ready(win))
            ovl_a2_prog(a2_fmode, a2_fname, a2_fsize, win);
    }

    /* --- EDIT > COPY AND EDIT > PASTE, SPENT HERE (section 6.5) -----------
     * At the TOP of the wake, with no lock held and BEFORE the slice, so not
     * one emulated cycle has run since the user picked the item: the text
     * page copied is the text page that was on the glass. Both latches are
     * spent whatever the machine's state, so a paused or jammed machine still
     * services the Copy in front of it. */
    if (a2_copy_req || a2_paste_req) {
        /* ...AND THE BODY IS AN `ovl_` (SPEC.md 73.14, section 15.0.2): what
         * runs once per PICK - the claims, the two clipboard calls, the six
         * refusals - goes out, and only the per-BYTE and per-$C000-read
         * halves (a2_paste_peek / _take / _live / _stop) stay resident. It is
         * legally reachable as one: this is the UI task, no lock is held, and
         * the latch can only have been set through ovl_a2_cmd, so the module
         * has already been resolved once.
         *
         * 0 MEANS THE RUNTIME REFUSED THE MODULE, which is the one thing this
         * caller can act on, and the latch is dropped so the refusal is said
         * once rather than on every wake. */
        if (!ovl_a2_clip_service()) {
            a2_copy_req = 0;
            a2_paste_req = 0;
            a2_say("Unable to load APPLE2.OVL.");
        }
    }

    /* --- the flash phase (APPLE2-SPEC section 7.6), THE SECOND PATH -------
     * 16 frames at 60 Hz is ~267 ms, which is ~5 host ticks at 18.2 Hz. The
     * flip is what force-dirties every scan line whose character row holds a
     * byte in $40-$7F: flashing changes pixels with NO MEMORY WRITE, so
     * nothing write-driven will ever mark those lines and a flashing cursor
     * would simply never blink.
     *
     * ON A KERNEL WITH WM_TIMER THIS IS DEAD CODE and os88_ontimer owns the
     * phase; a2_wants_wake says why. It is reached on kern_small, where the
     * slot answers CF=1, and there the wake polls exactly as it used to. */
    t = os88_ticks();
    if (!a2_tmr_ok)
        a2_flash_step(t);

    /* --- THE SLICE (APPLE2-SPEC section 4.3) ------------------------------
     * No lock is held here and none may be: this is the one callback the
     * kernel dispatches without the desktop's gfx lock (SPEC.md 74.1), and a
     * slice is an unbounded amount of emulated work.
     *
     * ...AND ONLY A GENUINELY EXHAUSTED SLICE ADAPTS THE BUDGET. A slice that
     * ended early - jammed - leaves the estimate alone (LESSONS.md 13). */
    if (a2_state == A2_ST_RUN && !a2_pause && !A2_CFM_UP()) {
        unsigned t0 = os88_ticks();

        a2_slice();
        if (a2_state == A2_ST_RUN) {
            unsigned el = (unsigned)(os88_ticks() - t0);

            if (el >= 2u) {
                /* THE ONE HARD CASE: the slice ran past a WHOLE host tick, so
                 * it is not a duty cycle any more - it is the UI task stopped
                 * for a tick it will never get back. Halve on the spot and
                 * start the window again; nothing here waits for statistics
                 * about a slice that has already overrun. */
                a2_adn = 0;
                a2_adx = 0;
                a2_budget = a2_budget / 2;
            } else {
                a2_adn++;
                if (el)
                    a2_adx++;
                if (a2_adn >= A2_ADAPT_N) {
                    if (a2_adx == 0)
                        a2_budget += a2_budget;      /* nowhere near it yet */
                    else if (a2_adx <= A2_ADAPT_LO)
                        a2_budget += a2_budget / 8;
                    else if (a2_adx >= A2_ADAPT_HI)
                        a2_budget -= a2_budget / 8;
                    a2_adn = 0;
                    a2_adx = 0;
                }
            }
            /* THE CLAMP IS ONE PLACE AND THE TEST IS `<= 0` AS WELL AS `>`:
             * `int` is SIXTEEN BITS here, so a doubling that landed past
             * 32,767 would arrive at the core NEGATIVE and expire before its
             * first fetch - a machine stopped dead by its own speed. */
            if (a2_budget > a2_slice_cap() || a2_budget <= 0)
                a2_budget = a2_slice_cap();
            if (a2_budget < A2_SLICE_MIN)
                a2_budget = A2_SLICE_MIN;
        }
    }
    a2_speed_fold();
    /* --- THE SPEAKER (section 8) -----------------------------------------
     * AFTER the slice, so the intervals read are the ones the machine just
     * made, and BEFORE the flush, so a note and the picture it goes with
     * reach the user in the same wake. One far call, on a change only. */
    a2_spk_service();
    /* THE TICK IS RE-READ AFTER THE SLICE, and that is not tidiness: the flush
     * is paced at most once per HOST tick and a slice can cross one, so a `t`
     * taken before the slice prices the flush against the tick the wake
     * STARTED in - two flushes inside one tick when the slice is short, and a
     * skipped one when it is long. */
    t = os88_ticks();

    /* --- has the machine written anything? ONE byte, ONE read -------------
     * The C's own a2_dirty_any is set by a2_dirty_scan, which runs INSIDE the
     * flush - so without this the flush would only ever run because of
     * something the C side did, and every write the emulated machine made
     * would sit in its memory with the glass stale. */
    if (a2_wrote())
        a2_dirty_any = 1;

    /* --- the flush, under the lock and only around itself ------------------
     * AT MOST ONCE PER HOST TICK, never once per slice (APPLE2-SPEC section
     * 7.7). Two flushes inside one tick can only ever draw the same picture
     * twice, and on the target the second one is the whole cost of the first
     * for no pixels at all. */
    /* ...AND EVERY OTHER TICK ON THE CPU_8086 TIER (section 7.8's tier table,
     * written from `make a2bandbench`). A full repaint measures 496.8 ms and
     * a host tick is 55, so on an 8088 the flush cannot keep up with its own
     * pacing and a second one inside one machine-visible change is the whole
     * cost of the first for pixels that were already right. On every other
     * tier the rate is one per tick, which is what it has always been. */
    if ((a2_dirty_any || !a2_sh_ok || a2_border_dirty || a2_st_dirty)
        && (!a2_flushed
            || (a2_tier_slow ? ((unsigned)(t - a2_fltick) >= 2u)
                             : (t != a2_fltick)))) {
        /* THE CLIP REGION IS OURS TO ARM. The kernel arms one for W_PAINT and
         * for NOTHING ELSE (SPEC.md 11.3, os88.h), and this is a BACKGROUND
         * painter: without it a2_flush's 24 blits, five fills and the
         * status row are drawn straight over whatever window is covering us.
         * It buys the other half too - clip_set answers -1 when not one pixel
         * of us shows, and then the whole ~500 ms flush is SKIPPED instead of
         * being spent on a window nobody can see. apps/c64/c64.c's
         * os88_onwake is the precedent, line for line. */
        os88_gfx_lock();
        /* ...AND A MINIMIZED WINDOW IS THE SAME ANSWER BY A SECOND ROUTE.
         * os88_wm_geom answers -1 for "not visible" and a2_flush returns on
         * it having cleared nothing, so without this test every dirty flag
         * stays set and a2_wants_wake goes on saying yes - the same ~1,400
         * round trips a second, at the one moment the user has said they do
         * not want to look at us. W_ONTIMER fires while minimized too
         * (os88.h), which is what keeps the clock arriving. */
        a2_covered = os88_wm_clip_set(win) < 0 || a2_geom(win) < 0;
        if (!a2_covered)
            a2_flush(win);
        os88_gfx_unlock();
    }

    a2_kick = 0;
    if (a2_wants_wake()) {
        a2_kick = 1;
        os88_wm_wake(win);
    }
}

/* ==========================================================================
 * LAUNCH
 * ========================================================================*/
/* a2_refuse_kb - the launch refusal, with the number in it.
 *
 * **IT IS COMPOSED TO FIT TOAST_MAX = 24** (`kernel/toast.inc:85`), which
 * TRUNCATES rather than refusing: this read `Apple II+: 64KB wanted, 384KB
 * free.` and reached the glass as `Apple II+: 64KB wanted, ` - the whole point
 * of the message, the number, in the half that was cut.
 *
 * AND THE FIRST RECOMPOSITION CUT THE WRONG WORD. `APPLE2 needs 64K, 384K`
 * fits, and reads as TWO REQUIREMENTS - the word that made the second figure
 * mean anything was `free`, and it was what got dropped. `APPLE2: 64K, ` is
 * 13, a three-digit figure is 3 (640 is the most a real-mode heap can answer)
 * and `K free` is 6: 22 of the 24, with the label and both halves of the
 * arithmetic intact.
 *
 * build.sh CHECKS THIS BOUND BY NAME. A composed toast is invisible to a
 * literal-only walk - which is how all three over-long toasts shipped - so
 * the gate's TOAST_COMPOSED table carries `line` at 22 and `a2_jamline` at
 * 18, and a composed toast that is in neither fails the build. */
static void a2_refuse_kb(const char *what)
{
    static char line[26];
    static char num[8];

    os88_strcpy(line, what, sizeof(line));
    os88_utoa(os88_mem_largest_kb(), num);
    os88_strcpy(line + os88_strlen(line), num, 6);
    os88_strcpy(line + os88_strlen(line), "K free", 7);
    os88_toast(line, 0);
}

void *os88_main(void)
{
    static struct os88_video vid;
    void *win;
    int wh, i, j;

    /* --- the claims (APPLE2-SPEC section 3.1) ----------------------------- */
    a2_m.ramseg = os88_mem_claim(64);       /* the Apple's address space is its
                                             * own segment */
    if (a2_m.ramseg == 0) {
        a2_refuse_kb("APPLE2: 64K, ");
        return 0;
    }
    /* THE ROM IS A PART AND IT IS ALREADY HERE (section 1.5, SPEC.md 20.12).
     * crt0 called op_load before this function, which claimed the 14,848
     * bytes and read the ROM into them - or refused the whole launch with a
     * toast naming why, before a sector was spent. So there is no claim here,
     * no read here, and no way for the ROM to be absent. */
    a2_m.romseg = os88_part_seg(A2_ROM_PART);
    if (a2_m.romseg < A2_ROM_MINSEG) {      /* the fetch bias would underflow */
        os88_mem_free(a2_m.ramseg);
        os88_toast("APPLE2: ROM too low", 0);    /* TOAST_MAX = 24: the long
                                                 * form reached the glass as
                                                 * `Apple II+: the ROM claim`
                                                 * and stopped there */
        return 0;
    }

    /* --- THE CHARGEN DECODE AND THE REVERSE TABLE, RESIDENT AND HERE ------
     * Decided once and never moved (APPLE2-SPEC section 7.3): op_load has
     * already put the ROM in memory, both tables are on the DISPLAY path, and
     * an overlay that can be absent may not own what the flush cannot run
     * without. A disk with no APPLE2.OVL must be a program whose MENUS
     * refuse, not a window that draws nothing. The negative control for that
     * is "both tables exist after os88_main and BEFORE any wake":
     * hosttest/a2uitest.c asserts it on the host, and WAVE 4 PROVED IT ON THE
     * MACHINE - a scratch disk with APPLE2.OVL deleted boots to a working
     * Apple II at `]` whose menu commands refuse politely
     * (build/port-shots/wave4-23-noovl.png and the two beside it). It is a
     * driven QMP run and not a registered test row, which is what the wave's
     * done_when asked for. */
    a2_chargen(a2_chr, a2_m.romseg, A2_CHR_BLOCK, sizeof(a2_chr));
    for (i = 0; i < 128; i++) {
        j = 0;
        if (i & 0x01) j |= 0x40;
        if (i & 0x02) j |= 0x20;
        if (i & 0x04) j |= 0x10;
        if (i & 0x08) j |= 0x08;
        if (i & 0x10) j |= 0x04;
        if (i & 0x20) j |= 0x02;
        if (i & 0x40) j |= 0x01;
        a2_rev[i] = (unsigned char)j;
    }

    /* --- the interleaved row bases (section 7.2) -------------------------- */
    /* ...AND EDIT > COPY'S FOLD, HERE FOR THE SAME REASON ONE STEP ALONG.
     * The screen byte's top two bits are the attribute and a II+ character
     * generator holds 64 glyphs, so `and 0x7F` collapses inverse, flashing
     * and normal onto one index and this folds what is left to ASCII:
     * $00-$1F and $40-$5F are `@A-Z[\]^_`, ASCII $40-$5F; everything else is
     * ASCII $20-$3F unchanged (APPLE2-SPEC section 6.5). It is nine lines and
     * 128 bytes of bss against a .data array paid for twice - on the floppy
     * and in the region - which is LESSONS.md 5's own arithmetic. */
    for (i = 0; i < 128; i++) {
        j = i & 0x3F;
        if (j < 0x20)
            j += 0x40;
        a2_astab[i] = (unsigned char)j;
    }

    for (i = 0; i < A2_ROWS; i++) {
        a2_tbase[i] = (unsigned)(1024 + 256 * ((i / 2) % 4)
                                      + 128 * (i % 2)
                                      + A2_X40((i / 8) % 4));
        a2_hbase[i] = a2_tbase[i] + 0x1C00u;        /* ...and the hi-res map,
                                                     * which IS that one plus
                                                     * $1C00 (section 7.2) */
    }

    /* ...and the lo-res patterns, derived from the luminance ladder ONCE.
     * The ladder is the palette's rank and the threshold is half of white's
     * luminance; what the composer reads is one seven-bit constant a colour,
     * because a monochrome block is uniform (section 7.3). */
    for (i = 0; i < 16; i++)
        a2_lopat[i] = (unsigned char)((a2_lum[i] >= A2_LUM_LIT) ? 0x7F : 0x00);

    a2_x2init();                            /* the pixel-doubling table, once */
    a2_tier_init();                         /* the flush rate, off os88_cpu() */
    /* WHAT CAN THIS MACHINE'S SOUND HARDWARE DO? ASKED ONCE, HERE (section
     * 8, and apps/c64/c64.c:1639 one machine along). SPEC.md 34 puts the
     * square voice on OSAPI_SND_TONE, and a package that calls a slot without
     * establishing the capability first is guessing (SPEC.md 73.11). It is
     * also Machine > Mute's own gate: a2_menu_state greys that row off this
     * flag and nothing else. */
    a2_have_snd = (os88_snd_caps() & A2_SND_CAP_TONE) ? 1 : 0;
    a2_io_init();                           /* the soft-switch state: TEXT on,
                                             * MIXED/PAGE2/HIRES off, which is
                                             * what a II+ powers up in */
    a2_scratch_clear();
    /* THE KEY-STATE MAP IS ARMED HERE AND NOWHERE ELSE (section 6.4).
     * OSAPI_KEY_DOWN's FIRST call clears and arms the map and always answers
     * "up", so arming it from the first slice would erase the make os88_onkey
     * had already seen - the first key of the session, silently lost. The
     * answer is ignored on purpose. */
    os88_key_down(KSC_SPACE);
    a2_watch_page();                        /* ...which writes the core's WATCH
                                             * RANGE, so the write window is
                                             * taken over the display page
                                             * from the very first poke */
    /* THE MACHINE IS POWERED ON HERE, and this is where wave 1's a2_selftext()
     * scaffolding used to be: the Autostart Monitor writes the display page
     * itself from now on, which is what a screendump of this port is supposed
     * to be a photograph of.
     *
     * a2_power_on lays AppleWin's FF FF 00 00 pattern with its three
     * compatibility pokes, ASSERTS the PWREDUP mismatch so the ROM takes the
     * cold path, sets A = X = Y = $FF and SP = $01FF and then takes the
     * Ctrl-Reset path - which is what leaves SP at $01FC, the value AppleWin
     * actually starts the ROM on (section 4.5). */
    a2_power_on();
    a2_sp_tick = os88_ticks();
    a2_menu_state();
    a2_sh_inval();

    /* ASK THE ADAPTER, DO NOT ASSUME (os88.h, SPEC.md 39). A2_CONT_H is 218
     * and that is a 480-line number: on a 200-line CGA desktop dock_top is
     * 176 and the window cannot have it. The window asks for what the desktop
     * can give, and a2_geom anchors what it got to the row the Apple's own
     * CURSOR is on, which is the one row a reader needs and is NOT at the
     * bottom of a machine that has not scrolled yet (APPLE2-SPEC section
     * 7.1's cursor anchor). */
    os88_video(&vid);
    wh = A2_W_H;
    if (vid.dock_top > 0 && A2_W_Y + wh > vid.dock_top)
        wh = vid.dock_top - A2_W_Y;
    if (wh < OS88_TITLE_H + A2_STATH + 16)
        wh = OS88_TITLE_H + A2_STATH + 16;
    win = os88_wm_create(A2_W_X, A2_W_Y, A2_W_W, wh, a2_title);
    if (win == 0) {
        os88_mem_free(a2_m.ramseg);
        return 0;
    }
    a2_win = win;
    os88_wm_snap(win, 1);                   /* the content x on a cell
                                             * boundary: blit1 and gfx_scroll
                                             * both want it */
    os88_wm_ownbg(win, 1);                  /* we paint every pixel */
    os88_menu_set(win, (struct os88_menuset *)&a2_menus);
    os88_about_set(win);
    os88_wm_onwake(win);                    /* the slice driver's entry (74.1) */
    /* ...and the document a double-click named (SPEC.md 54.5). BANKED, not
     * read: the wake is where the floppy is touched and where an ovl_ can be
     * reached at all. */
    if (os88_arg_file(a2_argname, &a2_argplace) == 0) {
        a2_argp = 1;
        a2_argdl = os88_ticks() + A2_ARGWAIT;
    }
    /* THE FLASH PHASE'S HEARTBEAT (SPEC.md 13.9), installed and armed - and
     * the arm is a TEST: kern_small carries WM_TIMER's slot and not its body,
     * so a refusal here is what puts the phase back on the wake's poll
     * (a2_wants_wake). One-shot, re-armed inside os88_ontimer. */
    os88_wm_ontimer(win);
    a2_tmr_ok = os88_wm_timer(win, A2_FLASH_TICKS) == 0;
    /* ...AND THE 6502 IS ALREADY OUT OF RESET (a2_power_on above), so the
     * state is set here rather than at the poke: a2_menu_state has already run
     * against it and the first wake is what starts the machine moving. */
    a2_state = A2_ST_RUN;
    a2_kick = 1;
    return win;                             /* the first paint kicks */
}
