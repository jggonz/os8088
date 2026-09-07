/* ============================================================================
 * os8088 - apps/infones/infones.c      INFONES: a NES emulator, written in C
 *
 * A native reimplementation of InfoNES, Jay Kumogata's portable Nintendo
 * Entertainment System emulator, as an os8088 package (SPEC.md 91): a 2A03 in
 * hand-written 8086, a per-scanline PPU over a pre-decoded tile cache, five
 * mappers, and a FRONT PANEL window whose game runs fullscreen through
 * SPEC.md 53's exclusive bracket.
 *
 * ----------------------------------------------------------------------------
 * Derived from InfoNES (https://github.com/jay-kumogata/InfoNES) at commit
 * fe3295c0a86bf5bbecc22aeab3b3af11f6f47908, Copyright (c) Jay Kumogata /
 * Jay's Factory, licensed under the Apache License, Version 2.0. You may
 * obtain a copy of the licence at http://www.apache.org/licenses/LICENSE-2.0
 * and in apps/infones/LICENSE.TXT, which ships on every floppy that carries
 * the binary.
 *
 * Section 4(b) modification notice: derived from InfoNES fe3295c0,
 * restructured for 8086 real mode.
 *
 * Nothing of InfoNES's SOURCE is vendored (CONTRIBUTING.md 6): its model is a
 * flat pointer space larger than 64KB, a 122,880-byte RGB frame buffer, a
 * 32,768-byte static ChrBuf, 263-byte automatics and DWORD arithmetic on the
 * hot path - five hard refusals from SPEC.md 73.7's four rules and 33's
 * segment. What is carried is BEHAVIOUR, TABLES AND STRINGS, and SPEC.md 91.2
 * names the InfoNES file every user-visible surface came from.
 *
 * PROVENANCE, WITH ITS LIMIT STATED: InfoNES descends from pNesX
 * (src/K6502.cpp:5 says so), whose authorship and terms are unestablished.
 * The 2019 Apache-2.0 grant covers what Jay Kumogata holds, and this port
 * records that it cannot establish more (SPEC.md 91.1).
 *
 * THE FILE FOR THIS FILE: os88_main, the callbacks and the shared state. What
 * came from where, per part, is in each part's own header.
 * ----------------------------------------------------------------------------
 *
 * THE SHAPE (SPEC.md 91.13.1, 74.1)
 *
 * There is NO WORKER TASK. The emulated machine runs only inside the
 * fullscreen bracket, which is the UI task itself (SPEC.md 53.1), and
 * File > Exit is os88_wm_close (75.2). What the wake is for here is the ROM
 * LOAD: os88_onwake is the one callback dispatched WITHOUT the gfx lock, and
 * therefore the first place an ovl_* may be reached at all - an overlay
 * cannot be resolved from os88_main because there is no instance yet
 * (73.14). So File > Open ROM raises a REQUEST and the wake performs it.
 * ==========================================================================*/

#include "os88.h"

/* --- the callbacks the shim declares (infones.asm's CC_HAS_*) ------------- */
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
 * THE HAND-WRITTEN HALF (SPEC.md 73.11) - every one of these is a cdecl entry
 * point in one of the four .inc files the shim %includes, and every one has a
 * stub of the same shape in hosttest/niuitest.c. A shim added without its
 * host stub fails to LINK, three steps later (LESSONS.md 7).
 * ========================================================================*/

/* --- nicpu.inc: the 2A03 (SPEC.md 91.4) ---------------------------------- */

/* The register file lives in nicpu.inc's .bss as _ni_m, because the core
 * loads and stores it with no frame. This is the C's view of the same bytes
 * and THE FIELD ORDER IS THE LAYOUT (SPEC.md 91.4.1) - the two cannot drift
 * without the C reading the wrong word. */
struct ni_mach {
    unsigned machseg;               /* the 13KB machine claim - DS for the
                                     * whole of a run (SPEC.md 91.3.1) */
    unsigned pc;
    unsigned a, x, y, s, p;         /* low byte of each; p is a REAL 6502 P */
    int      cnt;                   /* cycles LEFT of the last run, signed and
                                     * negative by up to one instruction */
    unsigned reason;                /* NI_RUN_* of the last return */
};
extern struct ni_mach ni_m;

#define NI_RUN_SLICE 0              /* the budget was spent */
#define NI_RUN_STOP  1             /* the core was asked to stop (a KIL, or a
                                     * bus handler that raised ni_halt) */

int  ni_run(int cycles);            /* -> NI_RUN_*; ni_m.cnt is what was NOT
                                     * spent, so `asked - cnt` is exact */
int  ni_bread(unsigned a);          /* ONE bus read for the C: the reset
                                     * vector, and the harness's pokes */
void ni_bwrite(unsigned a, int v);  /* ...and one bus write */
void ni_boot(void);                 /* the RESET sequence: S -= 3, I set, PC
                                     * from $FFFC (SPEC.md 91.4.3) */

/* --- nimem.inc: the movers, and THE ONLY PLACE ES IS LOADED --------------- */
void ni_move(unsigned dseg, unsigned doff,
             unsigned sseg, unsigned soff, unsigned n);
void ni_fill(unsigned dseg, unsigned doff, int val, unsigned n);
unsigned ni_peekw(unsigned seg, unsigned off);
void ni_pokew(unsigned seg, unsigned off, unsigned v);
void ni_oam_move(unsigned ramoff);  /* $4014's fast path: 256 bytes out of the
                                     * CPU RAM claim into OAM, one call rather
                                     * than 256 bus reads (SPEC.md 91.4.3) */

/* --- niband.inc: the tile-cache decoder and the composer (SPEC.md 91.5) --- */
void ni_chr_decode(unsigned dseg, unsigned doff,
                   unsigned sseg, unsigned soff);   /* one 1KB CHR bank -> 4KB
                                                     * of one byte a pixel */

/* --- nifsx.inc: SPEC.md 53's bracket, which is not wrapped for C ---------- */
int  ni_fsx_caps(void *win);        /* the mode mask for THIS WINDOW'S display,
                                     * re-asked at use and NEVER banked:
                                     * os88api.inc:2438-2449 says a banked
                                     * answer describes the window you were
                                     * LAUNCHED FROM */

/* ==========================================================================
 * THE MACHINE'S STATE
 * ========================================================================*/

/* The claim layout, SPEC.md 91.3.1's table as constants. Offsets INSIDE the
 * one machine claim; the core reaches all of them through DS. */
#define NI_MACH_KB   13
#define NI_O_RAM     0x0000         /* 2,048  CPU RAM $0000-$07FF */
#define NI_O_SRAM    0x0800         /* 8,192  the SRAM window $6000-$7FFF */
#define NI_O_VRAM    0x2800         /* 2,048  the two nametables */
#define NI_O_OAM     0x3000         /*   256  OAM */
#define NI_O_PAL     0x3100         /*    32  palette RAM */
#define NI_O_SCR     0x3120         /*   256  the core's scratch (nicpu.inc) */
#define NI_MACH_END  0x3220         /* ...and 480 bytes reserved above it, so
                                     * that a later wave adds a scratch word
                                     * without moving four offsets */

/* The core's scratch, as offsets from NI_O_SCR. nicpu.inc declares the same
 * numbers as NI_S_* and the two are ONE table (SPEC.md 91.3.1) - WHICH MEANS
 * ALL OF IT, INCLUDING THE ENTRIES ONLY THE CORE TOUCHES. This copy was
 * missing five of them (NI_S_T, T2, CSAV, PGC, T3) while the comment above
 * promised it was the whole table, and the C pokes this region by literal
 * offset (nirun.c, nippu.c, nimap.c): a later wave reading only this file
 * would have seen 12..0x7F unclaimed and put its own word on the core's
 * temporary, corrupting an (zp,X) address mid-instruction with no build error
 * and nothing on the glass to see. build.sh's `niscr` row now compares the two
 * files in both directions, so this cannot drift again.
 *
 * THE NEXT FREE BYTE IS 27, and 12..19 (the gap the core left for growth). */
#define NI_S_DEAD    0              /* the cycle countdown, signed */
#define NI_S_BLO     2              /* the fetch bias's low edge... */
#define NI_S_BOUND   4              /* ...and its bound */
#define NI_S_FES     6              /* the cached fetch ES */
#define NI_S_IRQ     8              /* bit 0 IRQ level, bit 1 NMI edge */
#define NI_S_BUS     10             /* the open-bus byte (SPEC.md 91.4.3) */
#define NI_S_HALT    11             /* set to end the run at the next
                                     * instruction boundary */
#define NI_S_T       20             /* nicpu.inc's word temporary - the C must
                                     * not use it: it is live ACROSS the two
                                     * reads that build an (zp,X) pointer */
#define NI_S_T2      22             /* ...its second, nicpu.inc's */
#define NI_S_CSAV    24             /* ...the saved carry, nicpu.inc's */
#define NI_S_PGC     25             /* ...the indexed page carry, nicpu.inc's */
#define NI_S_T3      26             /* ...and its third, the read/write path's
                                     * alone, nicpu.inc's */
#define NI_S_PRG     0x80           /* FOUR of the $8000-$FFFF window
                                     * segments, one per 8KB... */
#define NI_S_PRGSTR  0x20           /* ...AT A STRIDE OF 32 BYTES and not 2,
                                     * and the stride is the point: the window
                                     * index is bits 14-13 of the address,
                                     * which the core produces with one
                                     * `and bh,0x60` and no shift at all - and
                                     * a shift by four would need CL, which is
                                     * X and is never free on the read path.
                                     * 98 bytes of a 256-byte scratch buys the
                                     * four hottest instructions in the core */

/* The emulated machine's state that the C owns. */
static unsigned ni_machseg;         /* claim 1 */
static unsigned ni_prgseg;          /* claim 2 - one claim while a ROM is
                                     * <= 64KB, which is every ROM this port
                                     * ships and every harness fixture */
static unsigned ni_chrseg;          /* claim 3 */
static unsigned ni_cacheseg;        /* claim 4 - the 32KB tile cache */
static unsigned ni_prgkb, ni_chrkb, ni_cachekb;   /* what each was claimed at,
                                                   * so a reload frees exactly
                                                   * what it took */

/* The ROM's header, as the panel reads it (SPEC.md 91.7 rows 1-2). Its order
 * is InfoNES's own Help > ROM info order (InfoNES_System_Win.cpp:497-511). */
static unsigned char ni_mapper;
static unsigned ni_prg16;           /* 16KB banks, as the header counts them */
static unsigned ni_chr8;            /* 8KB banks; 0 means CHR-RAM */
static unsigned char ni_mirror;     /* 0 = horizontal, 1 = vertical */
static unsigned char ni_sram;
static unsigned char ni_fourscr;
static unsigned char ni_trainer;
static char ni_romname[14];

/* The two IRQ sources (SPEC.md 91.4.3: IRQ is a LEVEL of the mapper, the APU
 * frame counter, and nothing else). They are HERE rather than in nirun.c
 * because nippu.c is #included first and clears one of them. */
#define NI_IRQ_MAP  0x01
#define NI_IRQ_APU  0x02

#define NI_ST_NOROM 0
#define NI_ST_READY 1
#define NI_ST_RUN   2
#define NI_ST_ERR   3
static int ni_state;

/* --- MAY A LOCKED CALLBACK CROSS INTO INFONES.OVL? (SPEC.md 91.13.1) -----
 * Reaching an `ovl_*` is what makes the runtime RESOLVE the module, and if it
 * is not already resident that is an OSAPI_MEM_CLAIM and an OSAPI_FILE_READ -
 * a directory walk and a floppy seek, ~400 ms an int 13h on the target
 * (PERFORMANCE.md). os88_oncmd and os88_about are dispatched by the kernel
 * with the DESKTOP'S GFX LOCK HELD, so a first `Help > About InfoNES` on a
 * freshly launched package would stop the whole desktop and the pointer for
 * seconds while the .OVL was fetched.
 *
 * So the module is resolved ONCE, on the first wake, which holds no lock and
 * may call the file slots by contract (SPEC.md 74.1) - and every LOCKED
 * caller passes ni_ovl_ready() first, which answers from the banked result
 * and never crosses the bridge itself. This is apps/c64's shape verbatim
 * (c64.c's c64_ovl_asked / c64_ovl_res / c64_ovl_ready, C64-SPEC 13.3), and
 * without it this file's own header sentence - "os88_onwake is the ONE
 * callback dispatched WITHOUT the gfx lock, and therefore the first place an
 * ovl_* may be reached at all" - is not true of the code below it. */
static int ni_ovl_asked;            /* the first-wake probe has run */
static int ni_ovl_res;              /* ...AND IT ANSWERED YES, which is a
                                     * different fact and the one that keeps
                                     * floppy I/O out of the gfx lock */

/* The pending Open: os88_onfile cannot read a floppy under the gfx lock and
 * cannot reach an ovl_* usefully either, so it records the request and kicks
 * a wake, and os88_onwake does the load (SPEC.md 91.13.1). */
static int ni_req_load;
static unsigned ni_req_lo, ni_req_hi;
static char ni_req_name[14];

/* --- THE LAUNCH DOCUMENT (SPEC.md 54.10) ---------------------------------
 * Double-clicking a .NES launches this program - that is what niassoc.inc's
 * build-time block and os88_assoc_set() between them buy - and until this
 * banking existed that was ALL it did: the association was registered, the
 * program came up, and the ROM the user had just double-clicked was not
 * loaded. `cword` had the identical hole and SPEC.md 54.10 is what closed it.
 *
 * BANKED IN os88_main AND READ IN os88_onwake, which is 54.10's rule for
 * every app in the tree: an entry proc runs under the loader's own lock burst
 * with no window on the glass, so a read there freezes the desktop with the
 * file manager still the only thing on it.
 *
 * File scope and not automatics, because `&local` does not build here
 * (SPEC.md 73.5). */
static struct os88_place ni_argplace;
static int ni_argp;                 /* ...and whether one is pending: 0,0 is a
                                     * REAL place (drive A's root), so the pair
                                     * cannot speak for itself */

/* THE MESSAGE SCRATCH. One buffer, one owner at a time: there is no worker in
 * this package (infones.asm), so nothing can pre-empt a caller of these -
 * which is exactly the property SPEC.md 73.5's `static` idiom costs you when
 * a package DOES have one. */
#define NI_MSGMAX 42
static char ni_msg[NI_MSGMAX];
static char ni_num[8];

/* THE TOAST IS 24 CHARACTERS AND THE STATE LINE IS 40, AND THEY ARE NOT THE
 * SAME SENTENCE (SPEC.md 59.8, kernel/toast.inc's TOAST_MAX = 24).
 * `toast_stage` truncates at 24 with no ellipsis and no error, so the 37-
 * character launch refusal this package shipped read `INFONES wanted 13 KB,
 * la` on the glass - the whole FACT cut off, on the one refusal that has no
 * second channel because os88_main has no window yet. Every message therefore
 * has TWO forms: the sentence on the state line, and a <= 24-character toast.
 * apps/runcpm counts its refusal to 22 characters in a comment for the same
 * reason (runcpm.c:1180-1189), and build.sh's `nitoast` row now refuses a
 * literal over the bound. */
#define NI_TOASTMAX 24

static void *ni_win;                /* our window, for the wake and the
                                     * refusals */

/* --- forward declarations the parts need of one another ------------------
 * clang in the host harness is stricter about these than SmallerC is
 * (LESSONS.md 3), so every cross-part call is declared here, above the
 * #includes, and nowhere else. */
static void ni_say(const char *state, const char *toast);
static int  ni_refuse_kb(unsigned want);
static void ni_setfield(int f, const char *s);
static void ni_panel_state(void);
static void ni_panel_rom(void);
static void ni_repaint(void *win);
static void ni_shadow_drop(void);
static void ni_shadow_paper(void);
static void ni_shadow_paper_rect(int x1, int y1, int x2, int y2);
static void ni_fact_rotate(int nofsx);
static void ni_menu_state(void);
static void ni_rom_free(void);
static int  ni_geom(void *win);
static char *ni_app(char *d, const char *s);
static char *ni_appnum(char *d, unsigned v);
static void ni_panel_paint(void *win, int whole);
static void ni_panel_rate(void);
static void ni_panel_fact(void);
static void ni_panel_init(void);
static void ni_about_close(void *win);
static void ni_chr_dirty(unsigned a);
static int  ni_map_wr(unsigned a, int v);
static int  ni_map_ok(unsigned m);
static void ni_map_reset(void);
static void ni_map_hsync(void);
static void ni_nmi_raise(void);
static void ni_irq_raise(int src);
static void ni_irq_clear(int src);
static void ni_oam_dma(int page);
static void ni_reset_machine(int hard);
static int  ni_io_rd(unsigned a);
static int  ni_io_wr(unsigned a, int v);

static int  ni_ovl_ready(void *win);

/* the overlay's four entry points (SPEC.md 73.14: renaming a function to
 * ovl_* is the ENTIRE mechanism; tools/cc8086.py emits the code into the
 * module and leaves every global it names resident and DS-relative) */
static int ovl_probe(void);
static int ovl_rom_load(const char *name, unsigned size_lo, unsigned size_hi);
static int ovl_cmd(int item, int menu, void *win);
static int ovl_about_show(void *win);

/* ==========================================================================
 * THE PARTS (SPEC.md 73.1: ONE translation unit, split by concern into
 * #included .c files). EVERY ONE IS A WRITTEN PREREQUISITE IN THE MAKEFILE,
 * because make cannot see through #include and the symptom of a missing one
 * is a change that reads as if it did nothing (LESSONS.md 9).
 * ========================================================================*/
#include "nippu.c"                  /* the PPU register file and the bus */
#include "nimap.c"                  /* the mappers, behind one switch */
#include "nirun.c"                  /* the frame loop and the pacer */
#include "nipanel.c"                /* the front panel, its shadow, the
                                     * delta draw and the fact line */
#include "nimenu.c"                 /* the three FLAT menus */
#include "nirom.c"                  /* ovl_*: the iNES loader */
#include "nicmd.c"                  /* ovl_*: the menu command bodies */
#include "niabout.c"                /* ovl_*: the nine-row About panel */

/* ==========================================================================
 * SMALL SHARED HELPERS
 * ========================================================================*/

/* ni_app - append to a NUL-terminated buffer and answer the new end. There is
 * no strcat here (SPEC.md 73.9: no C library), and os88_strcpy always
 * terminates, so appending is "copy at the end". */
static char *ni_app(char *d, const char *s)
{
    unsigned n;

    n = os88_strlen(d);
    os88_strcpy(d + n, s, NI_MSGMAX - n);
    return d;
}

static char *ni_appnum(char *d, unsigned v)
{
    os88_utoa(v, ni_num);
    return ni_app(d, ni_num);
}

/* ni_say - ONE place a refusal or a note is delivered, so that every one of
 * them can be found by grep and length-checked by the harness. It goes to the
 * STATE LINE and to a toast: the state line is permanent and is where the
 * user is looking, and the toast is what catches an eye that is elsewhere
 * (SPEC.md 59, and RUNCPM's rc_say for the shape).
 *
 * TWO STRINGS AND NOT ONE, because the two fields are 40 cells and 24
 * characters and the shorter one truncates in silence (NI_TOASTMAX above).
 * The state line carries the whole sentence; the toast carries the same fact
 * in the room it has. */
static void ni_say(const char *state, const char *toast)
{
    ni_setfield(NI_F_STATE, state);
    os88_toast(toast, 0);
}

/* ni_ovl_ready - "may this LOCKED callback cross into INFONES.OVL?"
 * (SPEC.md 91.13.1, and apps/c64's c64_ovl_ready is the shape).
 *
 * Answering no is not a refusal of the feature; it is a refusal to do FLOPPY
 * I/O with the desktop's gfx lock held. The first wake asks, unlocked, and
 * every locked caller reads that answer; a no clears the probe so the NEXT
 * wake asks again, which is where the retry belongs. The user is told on the
 * STATE LINE and not only in a toast, because the state line is where they
 * are looking and it is still there a minute later. */
static int ni_ovl_ready(void *win)
{
    if (ni_ovl_res)
        return 1;
    ni_say("Unable to load INFONES.OVL.", "No INFONES.OVL");
    ni_ovl_asked = 0;               /* the wake retries it, unlocked */
    os88_wm_wake(win);
    return 0;
}

/* ==========================================================================
 * THE CALLBACKS
 * ========================================================================*/

/* os88_paint - W_PAINT (SPEC.md 11). The gfx lock is ALREADY held.
 *
 * WF_OWNBG IS SET, so the kernel did NOT whiten our content and
 * os88_wm_damage says which part of it needs drawing (SPEC.md 11.90.2). What
 * that buys is the case the flag exists for: a window dragged across a corner
 * of this one exposes two rows, and this repaints two rows instead of
 * thirteen - AND ONLY THE CELLS OF THEM THAT THE FILL COVERED, which is
 * 11.90.2's own "a partial repaint is per ELEMENT, not per pixel" carried to
 * the second axis (nipanel.c's NI_SH_PART). A 48-pixel expose owes 6 cells of
 * a 40-cell row, and lettering the row whole was 13 calls and ~245 cells where
 * 78 was the debt. What it COSTS is a promise - with the flag set the package
 * owes every pixel inside that rect, the 5-pixel margins and the 2-pixel gaps
 * between rows included - and the one fill below is how it is paid.
 *
 * The un-flagged shape was 13 padded runs and a kernel fill on every expose:
 * 431 ms of a 4.77 MHz 8088 to put back two rows (PERFORMANCE.md rule 1, and
 * 11.90.1 calls the kernel fill "the double draw in its purest form"). */
void os88_paint(void *win)
{
    static struct os88_rect d;      /* SPEC.md 73.5 rule 1: an out-parameter
                                     * is a STATIC, never an automatic */
    int whole;

    if (ni_geom(win) < 0)
        return;                     /* not visible: nothing to draw */
    whole = os88_wm_damage(win, &d);
    if (!whole && d.x1 > d.x2)
        return;                     /* an EMPTY rect: draw nothing at all */

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(d.x1, d.y1, d.x2, d.y2);
    if (whole)
        ni_shadow_paper();          /* the whole box is our own paper now */
    else
        ni_shadow_paper_rect(d.x1, d.y1, d.x2, d.y2);   /* ...and only
                                             * these ROWS, at only these CELLS */

    /* THE FIELDS GO ON FIRST, EVEN WITH THE ABOUT PANEL UP, and the banking
     * below is why. ni_panel_paint returns at its first line while `ni_abt` is
     * set - the panel owns the box - so the sequence used to be: whiten the
     * damage, draw NO fields, then put the About panel back. The panel is
     * NI_ABT_W = 280 wide inside a ~324 content box and NI_ABT_H tall inside
     * ~140, so the readout strip beside it and the bands above and below were
     * left BARE WHITE with their text gone until Esc - and WF_OWNBG's promise
     * is that every pixel inside the damage rect is repaid (SPEC.md 11.90.1).
     * Raising this window answers `whole`, so the whole readout went.
     *
     * It costs the cells under the panel one extra draw, once per expose, and
     * that is the cheap side of the trade: no field row is narrower than the
     * panel, so there is no row that could be skipped without leaving the very
     * strip this fixes. Only the rows the fill actually whitened are lettered
     * (ni_shadow_paper_rect marked exactly those), so a two-row expose is two
     * runs whether or not the panel is up. */
    if (ni_abt) {
        ni_abt = 0;
        ni_panel_paint(win, 0);
        ni_abt = 1;
    } else {
        ni_panel_paint(win, 0);     /* 0: the shadow has ALREADY been dropped,
                                     * exactly as far as the fill went */
    }

    /* THE ABOUT PANEL IS MODAL AND A REPAINT MUST PUT IT BACK. Without this,
     * any repaint while it is up - another window dragged across it, a raise,
     * a wm_paint_all from the Control Panel - erased the panel AND the readout
     * under it and drew neither: a blank white box, ni_abt still 1, and the
     * only way out was to guess that a key would close it. `apps/cword` does
     * the same thing in one line (cword.c:2551-2552).
     *
     * ovl_about_show re-banks ni_abt_x/y/w/h from the LIVE content box, so
     * this also fixes the stale rectangle ni_about_close would otherwise fill
     * if the window had moved while the panel was up. IT IS GATED ON
     * ni_ovl_res AND NOT ON A CALL: os88_paint is dispatched under the gfx
     * lock, so this may never be the thing that fetches the module
     * (SPEC.md 91.13.1). The module IS resident here by construction - it is
     * what drew the panel in the first place - so the gate is one compare. */
    if (ni_abt) {
        if (!ni_ovl_res || ovl_about_show(win) == 0)
            ni_abt = 0;             /* it cannot be drawn: do not leave a latch
                                     * over a panel that is not on the glass */
    }
}

/* os88_onkey - the PANEL's keys and nothing else. The game's keys are read
 * inside the bracket, by os88_key_down and an int 16h shim (SPEC.md 91.6.4),
 * and no event is dispatched while it runs. */
void os88_onkey(int ascii, int scan, void *win)
{
    if (ni_abt) {                   /* the About panel is up: any key closes
                                     * it, which is what os88_about's own
                                     * dismissal does */
        ni_about_close(win);
        return;
    }
    /* THERE IS NO `O` HERE, AND THERE WAS. A draft gave `O` = Open ROM, which
     * is a key InfoNES does not have (its load key is `L`,
     * InfoNES_System_Linux.cpp:266-270, and SPEC.md 91.6.4 records that L and
     * I are deliberately not carried), which appears in no menu caption, no
     * legend row and no key table, and which therefore no user could find. An
     * invented convenience is exactly what this port's own rule forbids: what
     * is not in a reference is not on the glass.
     *
     * `R` STAYS AND IS WRITTEN DOWN. It is InfoNES's own reset key
     * (add_key, Linux:262-266), the panel's second legend row already
     * advertises it, and SPEC.md 91.6.4 now says in the table that it answers
     * in the panel window as well as inside the bracket. */
    if (ascii == 'r' || ascii == 'R') {
        if (ni_have_rom())
            os88_oncmd(NI_I_RESET, NI_M_FILE, win);
    } else if (ascii == 'm' || ascii == 'M') {
        /* A GREYED ITEM HAS NO CLICK TO ANSWER, but a keyboard shortcut still
         * does (os88api.inc:1957-1959), so M prints the fact where the user
         * is looking - which is SPEC.md 91.7.2's rule, and the same key
         * InfoNES's own add_key spends on mute (Linux:304). */
        ni_fact = NI_FACT_MUTE;
        ni_panel_fact();
        ni_repaint(win);
    }
    (void)scan;
}

void os88_onclick(int x, int y, void *win)
{
    if (ni_abt) {
        ni_about_close(win);
        return;
    }
    /* Nothing on the panel is a control: it is a readout (SPEC.md 91.7). A
     * click is still worth a kick, because a wake the event ring refused
     * cannot then leave a requested load sitting forever (apps/c64's
     * os88_onclick, and it costs nothing when there is nothing to do). */
    if (ni_req_load)
        os88_wm_wake(win);
}

/* os88_oncmd - the three menus (SPEC.md 91.8). The gfx lock is HELD.
 *
 * Two compares and then an overlay call. Full screen is answered in the
 * RESIDENT half, and it is the only one that is: it is the command that must
 * work on a disk whose .OVL is missing (SPEC.md 91.6.5, apps/c64's reason). */
void os88_oncmd(int item, int menu, void *win)
{
    if (menu == NI_M_HELP) {
        os88_about(win);
        return;
    }
    if (menu == NI_M_FILE && item == NI_I_EXIT) {
        /* SPEC.md 75.2: the kernel's own close path. NOT os88_wm_destroy,
         * which frees the record and leaves a dead dock tile answering no
         * click. It returns and the window goes a moment later, so this
         * draws nothing afterwards. */
        os88_wm_close(win);
        return;
    }
    /* WAVE 2 PUTS `Full screen` HERE, in the resident half, and SPEC.md
     * 91.6.5 says why: it is the one command that must work on a disk whose
     * .OVL is missing, and no ovl_* may be called from inside the bracket at
     * all. There is no branch for it in this build because the item is
     * GREYED - the kernel never lets a MENU_DIS item be selected, so a branch
     * for it would be code nothing can reach. */
    if (!ni_ovl_ready(win)) {       /* SPEC.md 91.13.1: a LOCKED callback may
                                     * not be the thing that fetches the .OVL
                                     * off a floppy. ni_ovl_ready has already
                                     * put the reason on the state line */
        ni_repaint(win);
        return;
    }
    if (ovl_cmd(item, menu, win) == 0)
        return;                     /* the module refused and said so */
    ni_menu_state();
    ni_repaint(win);
}

/* os88_about - the kernel's name pull-down AND Help > About InfoNES open the
 * same panel (SPEC.md 12.2, 91.7.3). */
void os88_about(void *win)
{
    if (ni_abt)
        return;                     /* already up: a second open would draw a
                                     * panel over a panel and leave the close
                                     * with two rects to undo */
    if (!ni_ovl_ready(win)) {       /* ...and this one is the defect's worst
                                     * face: Help > About on a freshly
                                     * launched package, the whole desktop
                                     * stopped for a directory walk and a
                                     * seek (SPEC.md 91.13.1) */
        ni_repaint(win);
        return;
    }
    if (ovl_about_show(win) == 0)
        return;                     /* the module is not here; ovl_about_show
                                     * has already said so, and setting the
                                     * latch over a panel that does not exist
                                     * is what leaves the close with nothing
                                     * to close (apps/c64's own note) */
    ni_abt = 1;
}

/* os88_onfile - File > Open ROM's answer (SPEC.md 38). It records the request
 * and kicks; the WAKE does the read (SPEC.md 91.13.1).
 *
 * THE SIZE IS USED TO REFUSE BEFORE THE DISK IS TOUCHED. It is free - the
 * dialog read it out of the mount snapshot - and reading 40KB off a floppy to
 * then say "not a NES file" is about four seconds of motor the user cannot
 * tell from a load that works (os88.h's own note). */
void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win)
{
    (void)mode;

    /* THE SHADOW IS NOT DROPPED HERE, AND A DRAFT DROPPED IT. By the time this
     * runs the dialog is GONE and the kernel has already repaired us:
     * `fdlg_commit` is `fdlg_close`, the staleness check, THEN the callback
     * (SPEC.md 38.9), `fdlg_close` destroys the dialog window, and wm_destroy
     * ends in `wm_paint_dmg` over the rect it hid (SPEC.md 11.91) - so our
     * W_PAINT has ALREADY run, synchronously, inside it, and re-lettered every
     * row the dialog covered off its own paper. Dropping the shadow threw that
     * away and sent the two ni_repaint calls below down the padded branch for
     * all thirteen fields: 520 cells, ~478 ms of a 4.77 MHz 8088, to change one
     * seven-cell state line, immediately after the kernel's repaint had spent
     * ~325 ms on the same rows. That is PERFORMANCE.md rule 2 at panel scale.
     * The kernel's damage rect cannot under-report (SPEC.md 11.90.2), so it is
     * authoritative about what the dialog covered and there is nothing left
     * here to be unsure about. */
    if (size_hi != 0 || size_lo < NI_ROM_MIN) {
        ni_say("Not a NES file: too short.", "Not a NES file");
        ni_repaint(win);
        return;
    }
    os88_strcpy(ni_req_name, name, sizeof(ni_req_name));
    ni_req_lo = size_lo;
    ni_req_hi = size_hi;
    ni_req_load = 1;
    ni_say("Reading ROM...", "Reading ROM...");       /* ANNOUNCED BEFORE THE LONG WALK, which
                                     * is RUNCPM's lesson: a first read of a
                                     * floppy is seconds on the target and a
                                     * silent one reads as a dead machine */
    ni_repaint(win);
    os88_wm_wake(win);
}

/* os88_onwake - W_ONWAKE (SPEC.md 74.1): the UI task, the gfx lock NOT held.
 * The one callback that may call the file slots and take the lock itself, and
 * therefore the one place a ROM is read. */
void os88_onwake(void *win)
{
    int ok;

    /* SPEC.md 91.13.1'S FIRST `ovl_*` CALL, ON THE FIRST WAKE, AND ABOVE
     * EVERY EARLY RETURN. The .OVL cannot be resolved from os88_main - there
     * is no instance yet (73.14) - so this asks once, for nothing, at the
     * first moment there is one and with no lock held. Its answer is what
     * every LOCKED callback below reads instead of crossing the bridge
     * itself, and a launch that never opens a ROM still gets it. */
    if (!ni_ovl_asked) {
        ni_ovl_asked = 1;
        ni_ovl_res = ovl_probe();
        if (!ni_ovl_res) {
            os88_gfx_lock();
            ni_say("Unable to load INFONES.OVL.", "No INFONES.OVL");
            ni_panel_paint(win, 0);
            os88_gfx_unlock();
        }
    }

    if (!ni_req_load)
        return;                     /* a stale kick: be indifferent to being
                                     * called with nothing to do (os88.h) */
    ni_req_load = 0;

    if (!ni_ovl_res) {              /* the module is not here: the load cannot
                                     * happen and saying so beats a silent
                                     * request that never completes */
        os88_gfx_lock();
        ni_say("Unable to load INFONES.OVL.", "No INFONES.OVL");
        ni_panel_paint(win, 0);
        os88_gfx_unlock();
        return;
    }

    /* THE LAUNCH DOCUMENT'S FOLDER GETS ITS OWN ARM (SPEC.md 54.10): the goto
     * answers -1 when the folder cannot be listed, and without an arm the
     * whole launch fails in silence - the window comes up empty and a
     * double-click looks as if it did nothing. */
    if (ni_argp) {
        ni_argp = 0;
        if (os88_file_goto(&ni_argplace) != 0) {
            os88_gfx_lock();
            ni_say("Could not open that folder.", "Cannot open folder");
            ni_panel_paint(win, 0);
            os88_gfx_unlock();
            return;
        }
    }

    ok = ovl_rom_load(ni_req_name, ni_req_lo, ni_req_hi);

    os88_gfx_lock();                /* the wake is lock-free, so the drawing
                                     * below has to take it - and it is one
                                     * bounded burst, which is what 20.6's
                                     * rule 3 asks of any holder */
    if (ok) {
        os88_strcpy(ni_romname, ni_req_name, sizeof(ni_romname));
        ni_state = NI_ST_READY;
        ni_panel_rom();
        ni_setfield(NI_F_STATE, "Ready");
    } else {
        ni_state = NI_ST_ERR;
        ni_panel_rom();             /* the fields go back to `-`: a refusal
                                     * that leaves the last ROM's numbers on
                                     * the glass is a panel telling a lie */
    }
    ni_menu_state();
    ni_panel_paint(win, 0);
    os88_gfx_unlock();
}

/* ==========================================================================
 * os88_main - the entry (SPEC.md 20.2). The claim first, then the window:
 * a launch is DEFINED BY THE CLAIM succeeding, and the refusal quotes what
 * was asked and what there is (SPEC.md 47, RUNCPM's refusal shape).
 * ========================================================================*/
void *os88_main(void)
{
    static struct os88_video vid;   /* SPEC.md 73.5 rule 1: an out-parameter
                                     * is a STATIC, never an automatic */
    void *win;
    int h, maxh;

    ni_machseg = os88_mem_claim(NI_MACH_KB);
    if (ni_machseg == 0) {
        /* ONE HELPER, ONE WORDING, AND IT IS THE SAME ONE nirom.c's claims
         * refuse with (SPEC.md 91.10). This site used to compose a 37-
         * character sentence of its own and hand it to os88_toast, where the
         * strip is 24 and truncates in silence: what a user read was `INFONES
         * wanted 13 KB, la`, with the whole fact cut off, on the ONE refusal
         * that has no second channel - there is no window and so no state line
         * yet. ni_refuse_kb writes the short form into the strip and the long
         * one into the state field, which on this path is simply never shown. */
        ni_refuse_kb(NI_MACH_KB);
        return 0;                   /* the runtime turns this into the CF the
                                     * loader is owed */
    }
    /* The machine claim is zeroed once, here: the loader zeroes only what a
     * ROM owns, and a stale nametable would otherwise be visible in the first
     * frame of the SECOND ROM opened in a session. */
    ni_fill(ni_machseg, 0, 0, NI_MACH_END);
    ni_m.machseg = ni_machseg;

    /* THE WINDOW IS SIZED OFF THE LIVE SCREEN, never off 640x480 (SPEC.md
     * 39). The panel is thirteen rows at a ten-pixel pitch, which is 136
     * pixels of content and 154 of frame - and a 200-line adapter's desktop
     * is about 148 rows tall, so on CGA and on a 640x350 EGA this window
     * would hang over the dock and its last two fields would be drawn
     * NOWHERE. Ask for what fits and let ni_panel_paint lay out from the
     * geometry it actually got. */
    os88_video(&vid);
    h = NI_PANEL_H + OS88_TITLE_H;
    maxh = vid.dock_top - OS88_MBAR_H - 4;
    if (h > maxh)
        h = maxh;
    win = os88_wm_create(48, OS88_MBAR_H + 12, NI_PANEL_W, h, "InfoNES");
    if (win == 0)
        return 0;
    ni_win = win;

    /* Every content origin on a multiple of 8, so the panel's font runs reach
     * SPEC.md 6.1's aligned fast path on the two 1bpp adapters. */
    os88_wm_snap(win, 1);
    os88_wm_minsize(win, 200, 80);
    /* WF_OWNBG: os88_paint fills the damage itself and draws only the fields
     * under it (SPEC.md 11.90.1/11.90.2). Without this the kernel whitens the
     * whole content before every W_PAINT and the only correct answer to
     * os88_wm_damage is "whole", so a two-row expose costs a whole panel. */
    os88_wm_ownbg(win, 1);

    ni_menu_state();                /* the greying predicates, before the set
                                     * is registered: os88_menu_set publishes
                                     * the strings the kernel will draw */
    os88_menu_set(win, &ni_menus);
    os88_about_set(win);
    os88_wm_onwake(win);            /* installs os88_onwake; CC_HAS_ONWAKE */
    os88_assoc_set("NES", "INFONES");   /* the RUNTIME half of SPEC.md 54.5.
                                         * niassoc.inc is the build-time half
                                         * and the two are not alternatives */

    /* ASK THE KEY MAP ONCE AND IGNORE THE ANSWER (SPEC.md 9.7). Asking is
     * what ARMS it, the first call always answers 0, and arming it from a
     * callback erases the make os88_onkey has already seen. The bracket's
     * D-pad sweep is the reader; this is the arm. */
    (void)os88_key_down(0x39);

    ni_state = NI_ST_NOROM;
    ni_panel_init();

    /* ...and the ROM that double-click named. BANKED, not read (SPEC.md
     * 54.10): the kernel calls os88_onwake once this window is on the glass,
     * and that is where the floppy is touched. */
    if (os88_arg_file(ni_req_name, &ni_argplace) == 0) {
        ni_argp = 1;
        ni_req_load = 1;
        ni_req_lo = 0;              /* NO SIZE IS KNOWN on this path, where the
                                     * Standard File dialog always has one -
                                     * ovl_rom_load's own "0 means unknown" */
        ni_req_hi = 0;
        ni_setfield(NI_F_STATE, "Reading ROM...");
    }

    /* ...AND KICK ONE WAKE, WHICH IS WHAT RESOLVES THE OVERLAY (91.13.1).
     * The kernel posts a first wake ITSELF only on the launch-document path
     * (os88api.inc's OSAPI_WM_ONWAKE: "if your package was started by a
     * double-click on a DOCUMENT, the kernel calls this handler ONCE"), so a
     * plain launch from the Disk window would never get one - and ovl_probe
     * would never run, and every menu pick would then refuse. It costs
     * nothing: the kernel keeps at most one queued wake per window, so this
     * and the kernel's own are the same single wake on the document path
     * (OSAPI_WM_WAKE). It POSTS an event and does no I/O, which is the only
     * thing an entry proc may not do. */
    os88_wm_wake(win);
    return win;
}
