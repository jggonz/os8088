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
 * THE 6502 ARRIVES IN WAVE 2. Until it does there is nothing writing the
 * display page, so a2_selftext() below lays one down at launch - scaffolding
 * that is named as such, that wave 2 deletes, and that exists because the
 * only way to look at a composer is to look at what it composed.
 *
 * NO SIZE LINE IS QUOTED THIS WAVE and APPLE2-SPEC section 15 says why: a
 * core with no opcodes in it is not an honest measurement of a core, and
 * quoting one would set a budget against a number nobody can reproduce.
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

/* --- a2cpu.inc: the core (section 4) -------------------------------------- */
int  a2_run(unsigned cycles);           /* -> A2_RUN_*, and a2_m.cnt is what
                                         * was NOT spent, so the caller's
                                         * `ran = asked - cnt` is exact */
void a2_cut(void);                      /* end the run in progress, exactly */
void a2_rebias(void);                   /* the map moved: no instruction is
                                         * fetched under a stale bias */

/* --- a2band.inc: the composers (section 7.3) ------------------------------ */
void a2_band_text(unsigned char *dst, int g0, int g1,
                  unsigned mseg, unsigned moff, int fmask);
int  a2_rowflash(unsigned mseg, unsigned moff, int n);
int  a2_rowspan(const unsigned char *a, const unsigned char *b, int n);
void a2_rowcopy(unsigned char *dst, const unsigned char *src, int n);
unsigned a2_rowsig(unsigned mseg, unsigned moff, int n);
void a2_x2init(void);
void a2_band_x2(unsigned char *dst, const unsigned char *src, int rows);

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

/* --- the display pages (section 7.2) -------------------------------------- */
#define A2_TXT1 0x0400                      /* text/lo-res page 1 */
#define A2_TXT2 0x0800                      /* ...and page 2 */
#define A2_PGLEN 0x0400

/* --- what the machine is doing -------------------------------------------- */
#define A2_ST_HALT 0                        /* not started: the state a2_state
                                             * holds until os88_main has the
                                             * ROM and the claims */
#define A2_ST_RUN  1
#define A2_ST_JAM  2
#define A2_ST_DEAD 3                        /* the close is in flight (75.2) */

static int a2_state = A2_ST_HALT;
static void *a2_win;
static int a2_kick;                         /* a wake is wanted */
static int a2_exit_req;                     /* File > Quit asked; spent at the
                                             * top of the next wake (75.2) */
static int a2_ovl_asked;                    /* the first-wake probe ran... */
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
static int a2_have_cpu;                     /* WAVE 2 SETS THIS. Until then
                                             * every command that needs a 6502
                                             * is greyed rather than live and
                                             * useless - SPEC.md 47's rule
                                             * that nothing is live that only
                                             * toasts a refusal */
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
static int a2_sh_ok;                        /* the shadow describes the glass */
static int a2_border_dirty;                 /* the border wants filling */
static int a2_full;                         /* the fullscreen latch is ours */
/* ...and the About panel's three, which a2about.c WRITES and a2scr.c's flush
 * READS. One definition, here, so the two cannot drift into two copies of the
 * same fact (which is what the whole of this block exists to prevent). */
static int a2_abt_up;                       /* the panel is on the glass... */
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

/* THE 7-BIT REVERSE TABLE (section 7.3). 128 entries of one byte, ONE table
 * in ONE place, built here and RESIDENT - hi-res source bytes carry bit 0 as
 * the LEFTMOST pixel and the framebuffer wants MSB first, so every hi-res
 * byte goes through this in wave 3. THE CHARACTER GENERATOR IS THE OTHER WAY
 * UP and needs no reversal at all, which is the one thing about this machine
 * most likely to be got backwards. */
static unsigned char a2_rev[128];

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
static int  a2_geom(void *win);
static void a2_tier_init(void);
static void a2_watch_page(void);
static void a2_menu_state(void);
static void a2_selftext(void);
static int  a2_mode_page(void);
static void a2_fullscreen_toggle(void *win);
static void a2_flash_force(void);
static void a2_line_force(int line);
static void a2_key(int ascii, int scan, void *win);
static void a2_io_init(void);
static void a2_about_close(void *win);
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
static int  ovl_a2_prog(int mode, const char *name, unsigned size_lo);

/* THE REST OF THE TRANSLATION UNIT (SPEC.md 73.1: one .c, because `nasm -f
 * bin` has no notion of an external symbol, so a C package is one file with
 * the parts #included into it). Every one of them is a written prerequisite
 * in the Makefile, because make cannot see through #include. */
#include "a2io.c"                           /* the soft switches, both
                                             * directions - a stub with the
                                             * video state in it until wave 2 */
#include "a2kbd.c"                          /* the II+ byte map - a stub until
                                             * wave 2 */
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
    a2_say("No APPLE2.OVL yet.");
    return 0;
}

/* ==========================================================================
 * WAVE 1'S TEXT PAGE - SCAFFOLDING, AND NAMED AS SUCH
 * ========================================================================*/
/* a2_selftext - lay a text page down at launch.
 *
 * WAVE 2 DELETES THIS FUNCTION. The 6502 arrives then and the Autostart
 * Monitor writes the display page itself, which is what a screendump of this
 * port is supposed to be a photograph of.
 *
 * It exists because wave 1's whole deliverable is the redraw path and there
 * is no way to look at a composer except by looking at what it composed. What
 * it lays down is chosen to exercise the three things the composer decides
 * per cell and nothing else:
 *
 *   - NORMAL text        ($80-$FF), which is the glyph as the ROM has it;
 *   - an INVERSE run     ($00-$3F), the per-cell XOR mask at 0x7F;
 *   - a FLASHING run     ($40-$7F), the same mask driven by the phase - the
 *     one thing on the glass the damage model cannot see (section 7.6);
 *   - a full 40-column ruler, so that all FIVE eight-cell groups are composed
 *     and a group boundary that packed wrong is visible rather than plausible.
 *
 * The Apple's screen encoding, which is what makes this three lines and not a
 * table: normal is `ascii | 0x80`, inverse is `ascii & 0x3F`, flashing is
 * `(ascii & 0x3F) | 0x40`, and the glyph index is `byte & 0x3F` in all three.
 */
static void a2_puts(unsigned base, int col, const char *s, int form)
{
    int c;
    unsigned a;

    a = base + (unsigned)col;
    while (*s) {
        c = *s & 0x7F;
        if (form == 0)
            c |= 0x80;                      /* normal */
        else if (form == 1)
            c &= 0x3F;                      /* inverse */
        else
            c = (c & 0x3F) | 0x40;          /* flashing */
        a2_wr(a, c);
        a++;
        s++;
    }
}

static void a2_selftext(void)
{
    static const char *ruler =
        "0123456789012345678901234567890123456789";
    unsigned base = A2_TXT1;
    int i;

    for (i = 0; i < A2_PGLEN; i++)
        a2_wr(base + (unsigned)i, 0xA0);    /* a normal space */

    a2_puts(base + a2_tbase[1] - A2_TXT1, 4, "APPLE ][ PLUS   OS8088 APPLE2", 0);
    a2_puts(base + a2_tbase[3] - A2_TXT1, 4, "NORMAL  ABCDEFGHIJKLMNOPQRSTUVW", 0);
    a2_puts(base + a2_tbase[5] - A2_TXT1, 4, "INVERSE ABCDEFGHIJKLMNOPQRSTUVW", 1);
    a2_puts(base + a2_tbase[7] - A2_TXT1, 4, "FLASH   ABCDEFGHIJKLMNOPQRSTUVW", 2);
    a2_puts(base + a2_tbase[9] - A2_TXT1, 0, ruler, 0);
    /* NO `]` PROMPT AND NO FLASHING CURSOR. In all three references that pair
     * is the ONE signal that the machine is at the Applesoft prompt and will
     * accept typing - and a2_key() drops every keystroke this wave, so the
     * page would be making a promise the keyboard breaks (a2kbd.c's own
     * header argues the same thing about a stub that stored a byte in the
     * latch). The page states the fact instead. The flash phase loses nothing
     * by it: the FLASH row two lines up is what exercises it, and the
     * INVERSE row the other half of the mask. */
    a2_puts(base + a2_tbase[11] - A2_TXT1, 0,
            "NO 6502 IN THIS BUILD - WAVE 2", 0);
    a2_dirty_all();
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
 * compares, the 505.4 ms of section 7.9.1 - under the desktop's gfx lock, so
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

    if (a2_abt_up) {                        /* the panel is modal: any key
                                             * closes it and nothing reaches
                                             * the machine */
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
    ovl_about_show(win);
}

void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win)
{
    if (size_hi) {                          /* a file this machine's 48K
                                             * cannot hold, refused by SIZE
                                             * before anything is claimed */
        a2_say("Too large for a 48K Apple.");
        return;
    }
    if (!a2_ovl_ready(win))
        return;
    ovl_a2_prog(mode, name, size_lo);
    a2_kick = 1;
    os88_wm_wake(win);
}

/* ==========================================================================
 * THE WAKE - the one callback dispatched WITHOUT the gfx lock (SPEC.md 74.1)
 * ========================================================================*/
/* WAVE 1 HAS NO SLICE IN IT, because it has no core to run. What it does have
 * is everything that surrounds one, and every piece of it is what wave 2 will
 * drive: the latches spent at the top with no lock held, the overlay probe on
 * the FIRST wake (the .OVL cannot be resolved from os88_main - there is no
 * instance yet to resolve a module for, LESSONS.md 13), the flash phase, and
 * the flush under the lock and only around itself.
 *
 * `r = a2_run(budget)` goes in between the probe and the flush in wave 2, and
 * nothing above or below it moves. */

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
        return (a2_state == A2_ST_RUN) ? 1 : 0;
    if (a2_dirty_any || a2_border_dirty || a2_st_dirty || !a2_sh_ok)
        return 1;
    if (a2_state == A2_ST_RUN)
        return 1;                           /* WAVE 2: and a paused machine
                                             * answers 0 here, which is the
                                             * half c64 had to go back for */
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

    /* --- the overlay probe, on the FIRST wake ----------------------------- */
    if (!a2_ovl_asked) {
        a2_ovl_asked = 1;
        a2_ovl_res = ovl_a2_init();
        if (!a2_ovl_res)
            a2_say("Unable to load APPLE2.OVL.");
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
    if ((a2_dirty_any || !a2_sh_ok || a2_border_dirty || a2_st_dirty)
        && (!a2_flushed || t != a2_fltick)) {
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
static void a2_refuse_kb(const char *what)
{
    static char line[42];
    static char num[8];

    os88_strcpy(line, what, sizeof(line));
    os88_utoa(os88_mem_largest_kb(), num);
    os88_strcpy(line + os88_strlen(line), num, 8);
    os88_strcpy(line + os88_strlen(line), "KB free.", 10);
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
        a2_refuse_kb("Apple II+: 64KB wanted, ");
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
        os88_toast("Apple II+: the ROM claim is too low in memory", 0);
        return 0;
    }

    /* --- THE CHARGEN DECODE AND THE REVERSE TABLE, RESIDENT AND HERE ------
     * Decided once and never moved (APPLE2-SPEC section 7.3): op_load has
     * already put the ROM in memory, both tables are on the DISPLAY path, and
     * an overlay that can be absent may not own what the flush cannot run
     * without. A disk with no APPLE2.OVL must be a program whose MENUS
     * refuse, not a window that draws nothing. The negative control for that
     * is "both tables exist after os88_main and BEFORE any wake":
     * hosttest/a2uitest.c asserts it on the host today, and
     * `tests/apple2part.py` WILL assert it on the machine in WAVE 4
     * (docs/APPLE2-PORT-PLAN.md). That file is not in this tree yet - written
     * in the present tense it would be exactly the drift this decision is
     * trying to protect against. */
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
    for (i = 0; i < A2_ROWS; i++)
        a2_tbase[i] = (unsigned)(1024 + 256 * ((i / 2) % 4)
                                      + 128 * (i % 2)
                                      + A2_X40((i / 8) % 4));

    a2_x2init();                            /* the pixel-doubling table, once */
    a2_tier_init();                         /* the flush rate, off os88_cpu() */
    a2_io_init();                           /* the soft-switch state: TEXT on,
                                             * MIXED/PAGE2/HIRES off, which is
                                             * what a II+ powers up in */
    a2_scratch_clear();
    a2_zfill(0, 0, A2_SCR_BASE);            /* WAVE 1 ZEROES THE RAM. AppleWin's
                                             * `FF FF 00 00` power-on pattern
                                             * and its three pokes are wave 2's
                                             * (section 4.5), with the reset
                                             * that needs them */
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
    a2_selftext();                          /* WAVE 1 SCAFFOLDING - see above */
    a2_menu_state();
    a2_sh_inval();

    /* ASK THE ADAPTER, DO NOT ASSUME (os88.h, SPEC.md 39). A2_CONT_H is 218
     * and that is a 480-line number: on a 200-line CGA desktop dock_top is
     * 176 and the window cannot have it. The window asks for what the desktop
     * can give, and a2_geom anchors what it got to the BOTTOM of the Apple
     * frame - which is where the `]` cursor line and MIXED's four text rows
     * both are (APPLE2-SPEC section 7.1's bottom anchor). */
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
    /* THE FLASH PHASE'S HEARTBEAT (SPEC.md 13.9), installed and armed - and
     * the arm is a TEST: kern_small carries WM_TIMER's slot and not its body,
     * so a refusal here is what puts the phase back on the wake's poll
     * (a2_wants_wake). One-shot, re-armed inside os88_ontimer. */
    os88_wm_ontimer(win);
    a2_tmr_ok = os88_wm_timer(win, A2_FLASH_TICKS) == 0;
    a2_state = A2_ST_HALT;                  /* WAVE 2 puts the 6502 out of
                                             * reset here and sets A2_ST_RUN */
    a2_kick = 1;
    return win;                             /* the first paint kicks */
}
