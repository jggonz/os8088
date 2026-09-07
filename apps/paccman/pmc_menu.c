/* ============================================================================
 * os8088 - apps/paccman/pmc_menu.c   the menu, the About card, the key table
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; the key SEMANTICS below are pacman.c 782-817
 * and 926-946. See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * THE MENU IS PLATFORM CHROME AND IS NAMED AS SUCH
 * ---------------------------------------------------------------------------
 * The reference has no menu, no dialog and no status line: sokol_main() at
 * pacman.c 716-730 asks for a bare 448x576 window. Every item below is
 * therefore an ADDITION, and the shape it takes is the one the shipped
 * assembly Pac-Man already set on this system (apps/pacman/pacman.asm's
 * pm_items - New Game, Pause, Full Screen), plus Sound.
 *
 * SOUND IS GREYED, AND THE FACT IS THIS BUILD (SPEC.md 47 greys a FACT, and
 * rule 3 makes a greyed label say WHY NOT). pmc_snd.c is a wave-3 stub, so
 * there is no sound code in the image at all; a control that changes its own
 * label and nothing else is worse than one that says why it cannot act, so it
 * carries MENU_DIS and the reason - and the kernel refuses a click on a
 * MENU_DIS item before os88_oncmd() is reached (kernel/menu.inc), which the
 * early return there says a second time. It is NOT the fact an earlier draft
 * named: kernel/snd.inc's osapi_snd_caps answers a constant on every kernel
 * this OS boots, so the MACHINE is never the reason; the build is.
 *
 * THE ITEM READS "Sound (No Sound Yet)", AND BOTH HALVES ARE CORRECTIONS.
 * Wave 1 wrote "(No Game)" as ONE fact shared by Pause and Sound, because
 * that build drew the arcade field and ran nothing. Wave 2 gave the build a
 * game, so with Pac-Man moving on the glass an item still asserting there is
 * no game is a greyed label saying something FALSE - which is the one thing
 * SPEC.md 47 rule 3 exists to prevent. The two facts were never really one:
 * Pause had nothing to stop, and Sound has no code to silence. "(No Audio)"
 * was the other candidate and is not taken, because it reads as a claim about
 * the MACHINE and the machine is never the reason here.
 * The word in FRONT of the parenthesis was the second correction: "Sound Off"
 * is an imperative that asserts sound is currently ON, in an image with no
 * sound code at all, so the parenthesis said "not yet" while the label said
 * "it is on". The item names the subject and claims no state.
 *
 * PAUSE IS LIVE FROM WAVE 2, because wave 2 is the wave that gave it a tick
 * loop to stop - and a live Pause needs its STATE on the glass, which is what
 * pmc_pause_item() below is for. Its marker byte and its reason are gone and
 * nothing else here moved, which is exactly the shape SPEC.md 91 said the
 * un-greying would take.
 *
 * THE SET'S NAME IS THE WINDOW'S TITLE, the same literal in both places. The
 * package header says PACCMAN because that is what the Disk window labels the
 * file with; the kernel bar reads AM_NAME (SPEC.md 12.2), and a bar reading
 * PACCMAN over a window titled PaccMan is the drift LESSONS.md 8 records.
 * ==========================================================================*/

#define PMC_TITLE  "PaccMan"

/* The Pause item is the one string that changes, so the items array is not
 * const: os88_menu_set() writes the set's oncmd field anyway, and a set in
 * .rodata takes that patch silently and wrongly (see struct os88_menuset).
 *
 * "\x01" is MENU_DIS (apps/os88api.inc): an item whose string BEGINS with it
 * is drawn through the disabled pen and takes no click. New Game, Pause and
 * Full Screen all act in this build and carry no marker.
 *
 * THE SOUND ITEM CLAIMS NO STATE, and that is the correction wave 2's review
 * made. It used to read "Sound Off (...)", which is an IMPERATIVE label - it
 * says the action on offer is to turn sound OFF, i.e. that sound is currently
 * ON - in an image with no sound code in it at all. SPEC.md 47 rule 3 is
 * satisfied by the parenthesis and contradicted by the three characters in
 * front of it, which is the same defect this wave had just corrected in
 * "(No Game)" one word along. The pair of labels was also DEAD: MENU_DIS
 * makes the kernel refuse the click before os88_oncmd is reached, so nothing
 * could ever have flipped it. One static string, and wave 3 gives it a body
 * and a state together. */
static const char *pmc_items[] = {
    "New Game",
    "Pause",                            /* pmc_pause_item() swaps this one */
    "\x01" "Sound (No Sound Yet)",
    "Full Screen"
};

static struct os88_menuset pmc_mset = {
    PMC_TITLE, 0, 1,
    { { "Game", pmc_items, 4 } }
};

#define PMC_CMD_NEW    0
#define PMC_CMD_PAUSE  1
#define PMC_CMD_SOUND  2
#define PMC_CMD_FULL   3

/* --- the About card (SPEC.md 20.5.1.1) ------------------------------------
 * Product, version, what this port is, the attribution, and the keys - and
 * NOTHING about how the build renders or sounds. The three-voice reduction,
 * the fade cut and what the reference itself lacks are facts about the BUILD
 * and live in SPEC.md 91 and the README (LESSONS.md 8).
 *
 * TEN SHORT LINES, AND THE SHAPE IS A COMPATIBILITY CONSTANT. The card is
 * measured from its strings and then CLAMPED to the live content box
 * (apps/os88ui.inc), and this window's content is the arcade field: 224
 * pixels wide and, on CGA, 144 rows deep. So the width bound is ~24
 * characters at 8 pixels a cell, and the height bound is 144 / OS88UI_ABLH =
 * twelve lines with the frame still to fit inside them.
 *
 * The plan drafted this as six long lines and every one of them was CUT OFF
 * MID-WORD on the glass - "A C reimplementation of pa" - which is
 * LESSONS.md 8's rule ("a dialog is TWELVE rows, which is a 640x200 number")
 * arriving one control along. The FACTS are unchanged and none may be
 * dropped: the product, the reference and its commit, the author's copyright
 * and licence, the arcade ROM data's owner, the Dossier, and the keys. Only
 * the line breaks moved. Count the characters before adding a word.
 *
 * ALL THREE CHROME KEYS ARE ON THE LAST LINE, and that is a requirement
 * rather than a nicety. PaccMan has no status line - its content is the
 * 224-pixel arcade field - so this card is the ONLY place inside the program
 * a key can be discovered, and the precedent it takes its menu from
 * advertises the same three in its footer (apps/pacman/pacman.asm). N was
 * bound, documented in the README and reachable from nowhere in the program
 * until the line was reflowed to 23 characters to carry it. */
static const char *pmc_about_lines[] = {
    "PaccMan for os8088",
    "A C port of pacman.c,",
    "commit 0f5ec5a",
    "(c) 2020 A. Weissflog,",
    "MIT. floooh/pacman.c",
    "Tiles/sprites: Pac-Man",
    "arcade ROMs (Namco)",
    "Rules: Pac-Man Dossier",
    "Arrows/WASD move.",
    "F full. P pause. N new.",
    0
};

/* --- scancodes (SPEC.md 9) ------------------------------------------------
 * The arcade stick is a LEVEL, not an event: pacman.c's input_dir reads
 * state.input.up and friends on EVERY game tick and takes the current
 * direction as its default (pacman.c 926-946), which is why these are asked
 * of os88_key_down() once a frame rather than accumulated from os88_onkey().
 * The one divergence this port states is the press LATCH below: a frame here
 * is 3-11 game ticks long, so a tap shorter than a frame would otherwise be
 * lost entirely (SPEC.md 91). */
#define PMC_SC_UP     0x48
#define PMC_SC_DOWN   0x50
#define PMC_SC_LEFT   0x4B
#define PMC_SC_RIGHT  0x4D
#define PMC_SC_W      0x11
#define PMC_SC_A      0x1E
#define PMC_SC_S      0x1F
#define PMC_SC_D      0x20
#define PMC_SC_F      0x21
#define PMC_SC_P      0x19
#define PMC_SC_N      0x31
#define PMC_SC_ESC    0x01
#define PMC_SC_SPACE  0x39

/* --- state the chrome owns ------------------------------------------------ */
static int pmc_full;

/* pmc_paused is NOT here, and pmc_about_up is not either: both are defined in
 * paccman.c, because pmc_game.c and pmc_draw.c read them and are #included
 * first - and a static has no forward declaration in C (SPEC.md 73.1). */

/* --- the arcade stick ----------------------------------------------------
 * Four levels, read once a frame and applied to every game tick of it. The
 * priority is the reference's: up beats down beats right beats left, with the
 * CURRENT direction as the default so a stick at rest keeps Pac-Man running
 * (pacman.c 926-946).
 *
 * pmc_latch is the port's ONE input divergence and paccman.c's os88_onkey
 * carries the reasoning: a frame here is 3 to 11 game ticks, so a press is
 * remembered until the next poll folds it in. */
#define PMC_IN_UP     1
#define PMC_IN_DOWN   2
#define PMC_IN_LEFT   4
#define PMC_IN_RIGHT  8

static unsigned char pmc_latch;         /* presses since the last poll     */
static unsigned char pmc_held;          /* ...OR'd with what is held now   */

static int pmc_input_dir(int def)
{
    if (pmc_held & PMC_IN_UP)    return PMC_DIR_UP;
    if (pmc_held & PMC_IN_DOWN)  return PMC_DIR_DOWN;
    if (pmc_held & PMC_IN_RIGHT) return PMC_DIR_RIGHT;
    if (pmc_held & PMC_IN_LEFT)  return PMC_DIR_LEFT;
    return def;
}

/* pmc_poll_input - ONCE a frame, from the worker. os88_key_down takes no
 * lock, touches no port and reads no VRAM, which is why SPEC.md 9.7 says in
 * so many words that it is legal from a worker task - "which is where a game
 * loop that needs it actually runs". It is ADVICE and not an oracle: a break
 * code the ISR missed leaves a key reading down, and all a wrong yes can do
 * here is steer. */
static void pmc_poll_input(void)
{
    int h;

    if (!pmc_input_on) {
        pmc_held = 0;
        pmc_latch = 0;
        return;
    }
    h = 0;
    if (os88_key_down(PMC_SC_UP)    || os88_key_down(PMC_SC_W)) h |= PMC_IN_UP;
    if (os88_key_down(PMC_SC_DOWN)  || os88_key_down(PMC_SC_S)) h |= PMC_IN_DOWN;
    if (os88_key_down(PMC_SC_LEFT)  || os88_key_down(PMC_SC_A)) h |= PMC_IN_LEFT;
    if (os88_key_down(PMC_SC_RIGHT) || os88_key_down(PMC_SC_D)) h |= PMC_IN_RIGHT;
    pmc_held = (unsigned char) (h | pmc_latch);
    pmc_latch = 0;
}

/* pmc_about_up, the card's flag, is DEFINED IN paccman.c and not here, beside
 * the prototypes: pmc_draw.c reads it and is #included ahead of this file, and
 * a static has no forward declaration in C. Its comment is there with it.
 *
 * pmc_abdismiss - answers 1 when the key or the command was spent taking the
 * card down. apps/weave/weave.c's w_abdismiss is the house precedent and
 * apps/pacman/pacman.asm's pm_dismiss_body the one this port's menu comes
 * from: ANY key takes it down and starts nothing, which is also the
 * reference's own "any key" posture (pacman.c 782-817).
 *
 * The card was OPAQUE over its own rect, so what it covered is re-marked;
 * pmc_flush owns the clip region (nothing has armed one for a key - SPEC.md
 * 11.3 - and it arms one when something is covering us) and REFUSES while the
 * flag is up, which is why the flag is cleared BEFORE the flush and not
 * after.
 *
 * WHAT IT COVERED, AND NOT THE WHOLE FIELD. pmc_dirty_all is 36 bands at full
 * width - 36 x 69.78 ms, about 2.5 s of XT - reached from an ordinary
 * keystroke, and the card cannot cover 36 bands: apps/os88ui.inc measures it
 * as `lines * OS88UI_ABLH + 2 * OS88UI_ABPADY`, clamps that to the content box
 * and centres it, so ten lines is 134 rows of 288 on VGA - about 17 bands.
 * Nineteen full-width bands, ~1.3 s, were being recomposed for pixels the card
 * never touched.
 *
 * PMC_AB_LH AND PMC_AB_PADY MIRROR apps/os88ui.inc, which is two copies of one
 * fact - so the range is deliberately an OVER-APPROXIMATION with a band of
 * slack each side, and its only failure mode is drawing a band more than it
 * had to. It cannot leave a hole in the other direction unless the widget's
 * pitch grows by a whole band (8 rows on a 12-row pitch), and a layout that
 * has not run yet or a window that has gone answers pmc_dirty_all as before. */
#define PMC_AB_LH    12         /* = OS88UI_ABLH: the card's line pitch      */
#define PMC_AB_PADY   7         /* = OS88UI_ABPADY: its air above and below  */

static void pmc_ab_mark(void *win)
{
    const char **l;
    int n, h, d0, d1, ty0, ty1;

    if (!pmc_layout(win) || pmc_ch <= 0) {
        pmc_dirty_all();
        return;
    }
    n = 0;
    for (l = pmc_about_lines; *l; l++)
        n++;
    h = (n << 3) + (n << 2) + 2 * PMC_AB_PADY;  /* n * 12: tools/cc8086.py
                                         * refuses `imul ax, ax, 12` when it
                                         * cannot prove a scratch register
                                         * dead, and 12 is 8 + 4 (LESSONS.md
                                         * 3). PMC_AB_LH is the FACT and this
                                         * is the encoding of it;
                                         * tests/unit/t_paccman.py checks the
                                         * two against each other and both
                                         * against apps/os88ui.inc's own equs
                                         * in the fast tier */
    if (h > pmc_ch)                     /* the widget clamps, so this does  */
        h = pmc_ch;
    d0 = pmc_cy + ((pmc_ch - h) >> 1) - pmc_fy;         /* card top, in FIELD */
    d1 = d0 + h - 1;                                    /* rows              */
    if (d1 < 0) {
        pmc_dirty_all();                /* wholly above the field: cannot
                                         * happen with a centred card, and if
                                         * it ever does, draw everything */
        return;
    }
    if (d0 < 0)
        d0 = 0;
    ty0 = (d0 >> pmc_rsh) - 1;          /* a band of slack each side, because
                                         * the two constants above are a
                                         * mirror of somebody else's file    */
    ty1 = (d1 >> pmc_rsh) + 1;
    if (ty0 < 0)
        ty0 = 0;
    if (ty1 > PMC_TILES_Y - 1)
        ty1 = PMC_TILES_Y - 1;
    while (ty0 <= ty1) {
        pmc_mark_span(0, PMC_TILES_X - 1, ty0);
        ty0++;
    }
}

static int pmc_abdismiss(void *win)
{
    if (!pmc_about_up)
        return 0;
    pmc_about_up = 0;
    pmc_ab_mark(win);
    pmc_flush(win, 0);
    return 1;
}

/* pmc_pause_item - THE ONLY PLACE A PAUSED WINDOW SAYS SO.
 *
 * Pause went live this wave, and a paused PaccMan is otherwise pixel-identical
 * to a hung one: the kernel has no check-mark marker (MENU_DIS is the only one
 * in apps/os88api.inc), this package deliberately has no status line - its
 * content is the 224-pixel arcade field - and the title bar does not change.
 * The precedent the menu is copied from character-for-character does not leave
 * it unsaid: apps/pacman/pacman.asm carries `pm_s_pause` - 'PAUSED - P OR
 * SPACE TO RESUME' - in its footer, so on that program the state is always
 * readable. Here the item label is the surface that is left, so it says what
 * choosing it would DO: "Pause" while running, "Resume" while stopped.
 *
 * SWAPPING THE POINTER IS ENOUGH because the kernel reads the item array at
 * DROP time and not at os88_menu_set time (kernel/menu.inc's `mov si,
 * [es:bx]`, in both the measure pass and the draw pass), which is why the
 * array is not const.
 *
 * Every write of pmc_paused calls this: the P key and the menu command in
 * paccman.c, and pmc_new_game's clear in pmc_game.c. */
static void pmc_pause_item(void)
{
    pmc_items[PMC_CMD_PAUSE] = pmc_paused ? "Resume" : "Pause";
}
