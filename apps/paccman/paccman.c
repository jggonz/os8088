/* ============================================================================
 * os8088 - apps/paccman/paccman.c      PACCMAN, the translation unit's root
 *
 * PaccMan: a native reimplementation, in C (SPEC.md 73), of Andre Weissflog's
 * pacman.c - https://github.com/floooh/pacman.c, MIT, (c) 2020 Andre
 * Weissflog - at commit 0f5ec5a. SPEC.md 91 is the contract and
 * docs/PACCMAN-PORT-PLAN.md the design record.
 *
 * DERIVED MATERIAL AND PROVENANCE. The screens, tables, timings and rules are
 * the reference's, taken from its source and not from memory; its code is
 * reimplemented and not compiled (it is C99 against sokol, a GPU tile
 * renderer and nanosecond timing, none of which exists here). The tile,
 * sprite, hardware-colour and palette tables the reference embeds are Pac-Man
 * arcade ROM data (Namco) and its two sound register dumps were captured from
 * an arcade emulator; tools/paccman_assets.py decodes them on the host into
 * the committed apps/paccman/pmc_rom.c. The gameplay follows Jamey Pittman's
 * Pac-Man Dossier, as the reference's own header says. apps/paccman/LICENSE is
 * the reference's licence and is carried inside the image;
 * apps/paccman/README.md carries the full statement. Nothing of the reference
 * is vendored in this tree (CONTRIBUTING.md 6).
 *
 * IT IS NOT PACMAN. apps/pacman (SPEC.md 89) is a different program - the
 * Roklan Atari disk version, in hand-written assembly - and the two share no
 * file, name, image, make target or vm directory, by SPEC.md 73.12's rule.
 *
 * ---------------------------------------------------------------------------
 * WHAT WAVE 1 IS
 * ---------------------------------------------------------------------------
 * The package, the window, the two RAMs, the damage model, the band renderer
 * and its three blit paths, and game_init's first picture on the glass. The
 * tick path (worker, time, input, movement, the four ghosts) is wave 2, the
 * intro and the sound wave 3. Every file of the translation unit exists from
 * this wave so that the Makefile's prerequisite lines are complete from the
 * first commit: make cannot see through a #include, and an undeclared one
 * leaves a stale .o88 that reads exactly like a change that did nothing.
 * ==========================================================================*/

#include "os88.h"

/* --- the field, in tiles and in pixels -----------------------------------
 * 28 x 36 tiles of 8 pixels: the Namco board's own geometry, rotated so that
 * the field is TALLER than it is wide (pacman.c 172-176).
 *
 * PMC_VSTRIDE IS 32 AND NOT 28. `y * 28` is a multiply, and a multiply by a
 * non-power-of-two is what tools/cc8086.py refuses when it cannot prove a
 * scratch register dead (docs/C-TOOLCHAIN.md). Rounding the row stride up to
 * 32 makes every row index a shift, at a cost of 4 unused bytes a row in each
 * of two arrays - 288 bytes of bss, which is the cheap half of the ceiling
 * (SPEC.md 73.9). */
#define PMC_TILES_X   28
#define PMC_TILES_Y   36
#define PMC_VSTRIDE   32
#define PMC_VSHIFT    5
#define PMC_FIELD_W   (PMC_TILES_X * 8)          /* 224 */
#define PMC_FIELD_H   (PMC_TILES_Y * 8)          /* 288 */

/* --- MIRRORED FROM apps/paccman/pmcband.inc ------------------------------
 * Two copies of one fact, which is a thing this tree does deliberately and
 * then guards: the assembly composer computes the same three numbers from its
 * own PMC_BAND_W, and tests/unit/t_paccman.py reads both files in the fast
 * tier and fails if they disagree. If they ever do, pmcband.inc is right. */
#define PMC_BAND_STRIDE  (PMC_FIELD_W / 2)       /* 112 - packed 4bpp        */
#define PMC_BAND_PAD     4                       /* 8 px of slack each side  */
#define PMC_BAND_ROW     (PMC_BAND_STRIDE + 2 * PMC_BAND_PAD)  /* 120: the
                                                  * row PITCH, which is not
                                                  * the row WIDTH. A sprite
                                                  * hangs 8 px off the field
                                                  * at the tunnel mouth and
                                                  * the slack is what lets
                                                  * _pmc_sprite never clip */
#define PMC_PL_STRIDE    (PMC_FIELD_W / 8)       /* 28  - one plane's row    */
#define PMC_PL_STEP      (PMC_PL_STRIDE * 8)     /* 224 - plane to plane     */

/* --- the tile and colour vocabulary (pacman.c 190-247) ------------------- */
#define PMC_TILE_SPACE     0x40
#define PMC_TILE_DOT       0x10
#define PMC_TILE_PILL      0x14
#define PMC_TILE_DOOR      0xCF
#define PMC_COLOR_DEFAULT  0x0F
#define PMC_COLOR_DOT      0x10
#define PMC_NUM_LIVES      3

/* --- which blit a frame goes down through (pmc_draw.c) ------------------- */
#define PMC_P_BLITP  0
#define PMC_P_BLIT4  1
#define PMC_P_BLIT1  2

/* --- the window ----------------------------------------------------------
 * The content is the field: 224 wide, and 288 or 144 rows deep. The kernel's
 * own arithmetic turns that into a frame (kernel/wm.inc's wm_geom): the
 * content is W_W minus one border pixel each side, and W_H minus TITLE_H
 * minus one. So 226 x 307, or 226 x 163 where the screen is short.
 *
 * WF_KEEPH IS WHAT MAKES 307 POSSIBLE (SPEC.md 11.93). At y = OS88_MBAR_H the
 * frame's last row lands 1 row over the dock on EGA, 3 on Hercules and 7 on
 * CGA - all inside 11.93's DOCK_H/2 line - and keeph is the difference between
 * hanging over the dock strip by those few rows and being SHORTENED, which
 * would cost the bottom tile row on three adapters of four. */
#define PMC_WIN_W     (PMC_FIELD_W + 2)          /* 226 */
#define PMC_WIN_H     (PMC_FIELD_H + OS88_TITLE_H + 1)          /* 307 */
#define PMC_WIN_H_LO  ((PMC_FIELD_H / 2) + OS88_TITLE_H + 1)    /* 163 */
#define PMC_WIN_X     48
#define PMC_WIN_Y     OS88_MBAR_H

/* --- prototypes ----------------------------------------------------------
 * Up front, all of them: the host harness compiles this same file with clang,
 * which refuses a call to an undeclared function and a static declaration
 * that follows a non-static one (LESSONS.md 3). */

/* apps/paccman/pmcband.inc - the per-byte half, hand-written 8086 (SPEC.md
 * 73.11). Every table and buffer it touches arrives as a POINTER, so it names
 * no global and the harness can define byte-identical C twins. */
void pmc_tile(const unsigned char *src, const unsigned char *pairs,
              unsigned char *dst, int rowstep);
void pmc_pack_pl(const unsigned char *band, unsigned char *planes, int rows,
                 const unsigned int *planar, int cols);
void pmc_pack_1(const unsigned char *band, unsigned char *bits, int rows,
                const unsigned char *mono2, int y0, int cols);
void pmc_sprite(const unsigned char *src, const unsigned char *pal4,
                unsigned char *dst, int sinc, int rows, int flags,
                const unsigned char *brev);

static void pmc_clean(void);
static void pmc_dirty_all(void);
static void pmc_mark(int x, int y);
static int  pmc_ok(int x, int y);
static void pmc_vid_tile(int x, int y, int tile);
static void pmc_vid_color(int x, int y, int color);
static void pmc_vid_color_tile(int x, int y, int color, int tile);
static void pmc_vid_clear(int tile, int color);
static void pmc_vid_color_playfield(int color);
static int  pmc_conv(int c);
static void pmc_vid_color_text(int x, int y, int color, const char *s);
static void pmc_vid_text(int x, int y, const char *s);
static void pmc_vid_quad(int x, int y, int color, int tile);
static void pmc_init_playfield(void);
static void pmc_vid_color_char(int x, int y, int color, int c);
static void pmc_vid_score(int x, int y, int color, const unsigned char *d);
static void pmc_vid_fruit_score(int fruit);
static void pmc_game_init(void);
static void pmc_new_game(void);
static void pmc_game_tick(void);
static int  pmc_input_dir(int def);
static void pmc_poll_input(void);
static void pmc_mark_sprites(void);
static void pmc_mark_rect(int sx, int sy);
static void pmc_band_sprites(int ty, int c0, int c1);
static void pmc_widen(int ty);
static void pmc_brev_init(void);
static void pmc_frame(void *win);
static void pmc_pause_item(void);
static void pmc_ab_mark(void *win);
static int  pmc_abdismiss(void *win);
static int  pmc_layout(void *win);
static void pmc_pick_path(void *win);
static void pmc_draw_band(int ty, int c0, int c1);
static int  pmc_dirty_any(void);
static void pmc_flush_laid(void *win, int brk);
static void pmc_flush(void *win, int brk);
static void pmc_fill_clip(int x1, int y1, int x2, int y2,
                          int cx1, int cy1, int cx2, int cy2);
static void pmc_letterbox(int cx1, int cy1, int cx2, int cy2);
static int  pmc_repaint(void *win);

/* THE ABOUT CARD OWNS THE GLASS WHILE IT IS UP, and this byte is half of a
 * pair: os88_about_card only DRAWS - "you keep the flag and the dismissal;
 * the widget only draws" (apps/cc/os88.h) - and pmc_menu.c's pmc_abdismiss is
 * the other half.
 *
 * Without the flag, an expose that damages three tile rows recomposes those
 * bands and rubs out the slice of the card sitting over them, leaving a card
 * with a hole in it. Without the dismissal, that interlock turns a draw-once
 * widget into a MODAL one: the card covers the arcade field for the life of
 * the instance and the close box is the only way out. Any key and any menu
 * command take it down (apps/pacman/pacman.asm's pm_dismiss_body,
 * apps/weave/weave.c's w_abdismiss), and pmc_flush refuses while it is up so
 * that nothing blits through it.
 *
 * IT IS DEFINED HERE and not in pmc_menu.c, where the rest of the chrome's
 * state lives, because pmc_draw.c reads it and is #included first - and a
 * static has no forward declaration in C. */
static int pmc_about_up;

/* Whether the game is stopped. It lives HERE and not with the rest of the
 * chrome's state in pmc_menu.c for pmc_about_up's reason: pmc_new_game reads
 * it and pmc_game.c is #included first.
 *
 * NEW GAME CLEARS IT, which is apps/pacman's own behaviour (`pm_new` clears
 * `pm_pause` before `pm_board`) and not a choice made here. Without it,
 * choosing Game > New Game while paused draws the fresh maze, PLAYER ONE and
 * READY! from pmc_game_init and then nothing at all - no score, no sprites,
 * no reserve strip - because pmc_frame's guard skips the whole tick path.
 * Seen on the glass as a window that looks broken; Pause is a state the user
 * set on the game they just discarded. */
static int pmc_paused;

/* Whether os88_worker() is running yet. os88_paint hires it; see the comment
 * there for why it cannot be os88_main. */
static int pmc_hired;

/* --- the translation unit, in dependency order ---------------------------
 * `nasm -f bin` has no notion of an external symbol, so a C package is ONE
 * translation unit and its parts are #included rather than linked
 * (SPEC.md 73.1). EVERY ONE OF THESE IS A MAKE PREREQUISITE of
 * build/paccman.raw.asm, spelled out in the Makefile. */
#include "pmc_rom.c"            /* GENERATED and committed: the arcade tables */
#include "pmc_time.c"           /* wave 2: the two-word tick and the triggers */
#include "pmc_vid.c"            /* video_ram, color_ram and the damage spans  */
#include "pmc_move.c"           /* wave 2: the movement rules                 */
#include "pmc_game.c"           /* the round; wave 2 fills the actors         */
#include "pmc_intro.c"          /* wave 3: the attract screen                 */
#include "pmc_snd.c"            /* wave 3: three arcade voices into one       */
#include "pmc_draw.c"           /* the band renderer and its three blits      */
#include "pmc_menu.c"           /* the menu, the About card, the key table    */

/* ==========================================================================
 * THE OS CALLBACKS
 * ========================================================================*/

/* os88_main - before the window exists and before the instance is published.
 * No drawing here, by contract.
 *
 * os88_video() and not os88_wm_display(): there is no window yet to ask about.
 * The one thing decided from it is the frame HEIGHT, and a machine whose
 * desktop cannot hold 288 rows of field gets the alternate-row layout - which
 * is CGA, and only CGA, on every adapter this OS boots.
 *
 * os88_key_down(0) ARMS the kernel's key map (SPEC.md 9.7) and always answers
 * 0; the answer is ignored on purpose. It has to happen here rather than in a
 * callback, because arming from a callback erases the make os88_onkey has
 * already seen - and the arcade stick this program reads is a LEVEL, so the
 * map is the whole of its steering. */
void *os88_main(void)
{
    static struct os88_video v;
    void *win;
    int h;

    os88_video(&v);
    h = (v.h - OS88_MBAR_H >= PMC_WIN_H) ? PMC_WIN_H : PMC_WIN_H_LO;

    win = os88_wm_create(PMC_WIN_X, PMC_WIN_Y, PMC_WIN_W, h, PMC_TITLE);
    if (win == 0)
        return 0;

    os88_wm_keeph(win, 1);        /* hang over the dock, do not be shortened */
    os88_wm_snap(win, 1);         /* content origin on 8: BLITP and BLIT1
                                   * both refuse an unaligned x (11.94)     */
    os88_wm_ownbg(win, 1);        /* every pixel is ours; skip the white
                                   * fill the arcade field would cover      */
    os88_wm_minsize(win, PMC_WIN_W, h);

    pmc_pause_item();
    os88_menu_set(win, &pmc_mset);
    os88_about_set(win);

    os88_key_down(0);             /* arm the map; the answer means nothing  */

    pmc_brev_init();
    pmc_new_game();
    return win;
}

/* ==========================================================================
 * THE WORKER, AND THE TWO CLOCKS IT STANDS BETWEEN
 *
 * The arcade runs at 60 Hz and this machine's tick is 18.2 (SPEC.md 8), so a
 * frame here is 3.3 game ticks at best and eleven on a 4.77 MHz XT drawing a
 * wide band. The loop below is apps/pacman's pm_worker (SPEC.md 89.2) - sleep
 * to a DEADLINE, re-anchor when late rather than burst - with the accumulator
 * of pmc_time.c on top of it: PMC_CATCHUP_MAX caps a single frame's game time
 * at two OS ticks, so a slow adapter runs the game SLOWLY instead of in jumps
 * and every sprite stays within a tile of where it was last drawn.
 *
 * THE CHAIN IS FLAT ON PURPOSE. os88_worker -> pmc_frame -> pmc_game_tick ->
 * pmc_update_actors -> a ghost step -> pmc_can_move is the deepest it goes,
 * and the DRAW hangs off pmc_frame beside pmc_game_tick rather than under it,
 * so the two never add up. The package declares OS88_STACK_256 and that is
 * what pays for it (SPEC.md 91).
 * ========================================================================*/

static unsigned pmc_last;       /* the OS tick the last frame was taken at */

/* pmc_frame - one drawn frame. The gfx lock is HELD by the worker around it.
 *
 * The focus test is apps/pacman's: a game that keeps running under another
 * window is a game the user cannot see losing lives (SPEC.md 89.2). Time is
 * re-anchored FIRST, so coming back into focus resumes rather than lurches.  */
static void pmc_frame(void *win)
{
    unsigned now, el;

    now = os88_ticks();
    el = now - pmc_last;
    pmc_last = now;

    /* ONE EXIT, AND IT IS AN INSTRUMENT'S REQUIREMENT AS MUCH AS A STYLE.
     * tools/stkdepth.py walks a routine linearly and stops at the first `ret`
     * (its own docstring says so), so an early return above the deep calls
     * hides the whole tick path from it: this same body written with three
     * `return`s priced the worker's chain at 14 bytes instead of 118. The
     * measurement that sizes OS88_STACK_256 has to be able to see the chain
     * it is sizing. */
    if (os88_wm_top() == win && !pmc_paused && !pmc_about_up) {
        if (el > PMC_CATCHUP_MAX)
            el = PMC_CATCHUP_MAX;
        while (el-- > 0)
            pmc_acc += PMC_ACC_PER_OS;

        if (pmc_acc >= PMC_ACC_PER_GAME) {
            pmc_poll_input();
            while (pmc_acc >= PMC_ACC_PER_GAME) {
                pmc_acc -= PMC_ACC_PER_GAME;
                pmc_tick_inc();
                PMC_COUNT(pmc_n_gtick, 1);
                if (pmc_mode == PMC_MODE_GAME)
                    pmc_game_tick();
            }
            pmc_mark_sprites();
        }
        pmc_flush(win, 1);      /* THE WORKER's flush: it may break its own
                                 * lock hold between chunks of bands. Every
                                 * other caller here is inside a kernel
                                 * callback and passes 0. */
    }
}

void os88_worker(void *win)
{
    unsigned due;

    pmc_last = os88_ticks();
    due = pmc_last;
    for (;;) {
        os88_task_alive(win);           /* never under the lock */
        due++;
        {
            unsigned t = os88_ticks();
            int d = (int) (due - t);
            if (d > 0)
                os88_task_sleep(d);
            else
                due = t;                /* late: re-anchor, never burst */
        }
        os88_gfx_lock();
        pmc_frame(win);
        os88_gfx_unlock();
        os88_task_yield();              /* let a waiting UI handler in */
    }
}

/* os88_paint - the gfx lock is held. Everything the window shows is the two
 * RAMs, so a paint recomposes them - but only the bands the kernel says we
 * owe. WF_OWNBG above is the interlock that makes os88_wm_damage answer with a
 * PARTIAL rect (SPEC.md 11.90.2), and a whole-content repaint is 2.5 s of XT
 * against 209 ms for the three tile rows a dropped menu actually covers.
 *
 * THE CARD IS DRAWN ONLY WHEN SOMETHING WAS OWED. pmc_repaint answers 0 for
 * SPEC.md 11.90.2's EMPTY rect - a paint the kernel has just said owes nothing
 * - and redrawing the About card there would spend ~12 gfx calls and ~200
 * glyph cells, about 210 ms of XT, on a frame whose whole point is that it
 * costs nothing. The _d entry and not the plain one: this arrives with the
 * paint's own damage region armed and os88_about_card would throw it away. */
void os88_paint(void *win)
{
    /* THE WORKER IS HIRED HERE and not in os88_main: os88_task_spawn wants a
     * callback with the gfx lock held, and os88_main has neither (apps/cc/
     * os88.h). A refusal is normal and transient - the twelve-slot task table
     * can be full - so the flag is only set once the spawn took, and the next
     * paint asks again. */
    if (!pmc_hired && os88_task_spawn(win) == 0)
        pmc_hired = 1;

    if (pmc_repaint(win) && pmc_about_up)
        os88_about_card_d(win, pmc_about_lines);
}

/* os88_about - the standard card (SPEC.md 20.5.1.1). The plain form, because
 * this arrives with no clip region armed; os88_paint()'s would need the _d
 * one. Wave 3 makes dismissing it leave play paused. */
void os88_about(void *win)
{
    pmc_about_up = 1;
    os88_about_card(win, pmc_about_lines);
}

/* os88_onkey - PRESSES. The steering is NOT here: a direction is a LEVEL the
 * frame reads with os88_key_down() (pacman.c 926-946), and wave 2 adds the
 * one-frame press latch this port states as its single input divergence.
 * What is here is the chrome the reference has no equivalent of, plus the
 * two keys pacman.c 782-817 gives its own meaning:
 *
 *   F    full screen, and it is NEVER an "any key" - the reference gives it
 *        its own switch case with no anykey beside it
 *   Esc  leaves full screen when we hold the latch, and is otherwise an
 *        ordinary "any key" (DBG_ESCAPE governs only leaving the game loop) */
void os88_onkey(int ascii, int scan, void *win)
{
    (void) ascii;

    /* ANY KEY TAKES THE ABOUT CARD DOWN, and starts nothing else. The widget
     * only draws; the flag and the dismissal are ours (apps/cc/os88.h), this
     * package declares no CC_HAS_ONCLICK, and a card nothing can dismiss owns
     * the arcade field for the life of the instance. The key is SWALLOWED
     * rather than also starting a new game, which is apps/weave/weave.c's
     * w_abdismiss and apps/pacman/pacman.asm's pm_dismiss_body both. */
    if (pmc_abdismiss(win))
        return;

    if (scan == PMC_SC_F) {
        pmc_full = !pmc_full;
        if (os88_fullscreen(win, pmc_full) < 0) {
            pmc_full = 0;
            os88_toast("Another window is full screen.", 36);
        }
        return;                   /* the kernel repaints us either way */
    }
    if (scan == PMC_SC_ESC && pmc_full) {
        pmc_full = 0;
        os88_fullscreen(win, 0);
        return;
    }
    /* SPACE RESUMES TOO, which is the precedent's binding and not one made
     * here: apps/pacman/pacman.asm advertises 'PAUSED - P OR SPACE TO RESUME'
     * and accepts both. The About card cannot say so - its lines are bounded
     * at 23 characters by the 224-pixel content box (pmc_menu.c) - so the
     * label swap is where the state is read, and SPACE is the second key a
     * player arriving from PAC-MAN will reach for. The reference binds
     * neither: pacman.c has no pause at all. */
    if (scan == PMC_SC_P || scan == PMC_SC_SPACE) {
        pmc_paused = !pmc_paused;
        pmc_pause_item();       /* the ONLY place a paused window says so */
        return;
    }
    if (scan == PMC_SC_N) {
        pmc_new_game();
        return;                 /* the WORKER draws it: see os88_oncmd's
                                 * PMC_CMD_NEW for why this is not flushed
                                 * from inside the callback */
    }

    /* THE STEERING IS A LEVEL AND THIS IS ITS ONE DIVERGENCE. input_dir reads
     * the four keys' held state on EVERY game tick (pacman.c 926-946) and a
     * frame here is 3 to 11 game ticks long, so a tap shorter than a frame
     * would never be seen by os88_key_down at all. A press therefore LATCHES
     * until the next frame's poll and is then folded into that frame's held
     * state - a tap becomes exactly one frame of 'held', nothing more, and a
     * hold released before a junction is still forgotten as in the reference
     * (SPEC.md 91). There is no buffered turn.
     *
     * ONE BYTE, WRITTEN BY THE UI TASK AND CLEARED BY THE WORKER, which is
     * the only shared state the two have and is why it is a single byte with
     * single-byte writes on both sides. The window between the worker's read
     * and its clear can drop a press that arrives inside it; the cost of that
     * is one lost tap on one frame, and the price of closing it is a lock the
     * worker is not allowed to take. */
    if (scan == PMC_SC_UP || scan == PMC_SC_W)
        pmc_latch |= PMC_IN_UP;
    else if (scan == PMC_SC_DOWN || scan == PMC_SC_S)
        pmc_latch |= PMC_IN_DOWN;
    else if (scan == PMC_SC_LEFT || scan == PMC_SC_A)
        pmc_latch |= PMC_IN_LEFT;
    else if (scan == PMC_SC_RIGHT || scan == PMC_SC_D)
        pmc_latch |= PMC_IN_RIGHT;
}

/* os88_oncmd - the Game menu. The gfx lock is held.
 *
 * A COMMAND TAKES THE ABOUT CARD DOWN TOO, and unlike a key it then goes on to
 * do what was asked. The flag is cleared and the field re-marked WITHOUT
 * drawing, because the command's own pmc_flush is about to draw the field it
 * wants rather than the one the card was covering - dismissing the card the
 * ordinary way here would compose all 36 bands (2,512 ms of XT) and New Game
 * would then compose them a second time. `down` carries the case where no
 * command drew: the card is gone and something has to put the field back.
 *
 * Sound alone is greyed (pmc_menu.c) and kernel/menu.inc refuses a click on a
 * MENU_DIS item before this is reached; the early return says it twice. */
void os88_oncmd(int item, int menu, void *win)
{
    int down;

    (void) menu;

    if (item == PMC_CMD_SOUND)          /* still greyed: wave 3 gives it a
                                         * body, and kernel/menu.inc has
                                         * already refused the click */
        return;

    down = pmc_about_up;
    if (down) {
        pmc_about_up = 0;
        pmc_ab_mark(win);       /* what the card COVERED, not the whole field
                                 * - pmc_menu.c has the arithmetic */
    }

    if (item == PMC_CMD_PAUSE) {
        pmc_paused = !pmc_paused;
        pmc_pause_item();       /* the ONLY place a paused window says so */
        if (down)
            pmc_flush(win, 0);  /* nothing else is coming: a paused window
                                 * runs no frames, so the card's rect would
                                 * stay on the glass */
        return;
    }
    if (item == PMC_CMD_NEW) {
        pmc_new_game();
        return;                 /* THE WORKER DRAWS IT, AND THAT IS THE WHOLE
                                 * POINT OF pmc_flush's `brk`. Flushing here
                                 * would compose all 36 bands under ONE
                                 * uninterruptible hold of the kernel's gfx
                                 * lock - ~2.5 s of XT in which nothing else
                                 * on the machine can draw - and a callback's
                                 * lock is not ours to drop (pmc_draw.c), so
                                 * the chunked path is unavailable in here.
                                 * pmc_new_game has marked the field and
                                 * cleared pmc_paused, `down` has cleared
                                 * pmc_about_up, and a menu command implies we
                                 * are top, so pmc_frame's guard passes and
                                 * the next frame - at most one OS tick, 55 ms
                                 * - draws it chunked and interruptible. The
                                 * two `if (down)` flushes below are the cases
                                 * where NO frame is coming. */
    }
    if (item == PMC_CMD_FULL) {
        pmc_full = !pmc_full;
        if (os88_fullscreen(win, pmc_full) < 0) {
            pmc_full = 0;
            os88_toast("Another window is full screen.", 36);
            if (down)
                pmc_flush(win, 0);   /* refused, so no repaint is coming and the
                                   * card's rect is still on the glass */
        }
        return;                   /* it was taken: the kernel repaints us */
    }
    if (down)
        pmc_flush(win, 0);           /* a command that drew nothing, with the
                                   * card just taken down */
}
