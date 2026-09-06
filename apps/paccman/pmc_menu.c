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
 * PAUSE AND SOUND ARE GREYED, AND THE FACT IS THIS BUILD (SPEC.md 47 greys a
 * FACT, and rule 3 makes a greyed label say WHY NOT). Neither has a body yet:
 * there is no tick loop until wave 2, so pmc_paused is a byte nothing reads,
 * and pmc_snd.c is a wave-3 stub, so there is no sound code in the image at
 * all. A control that changes its own label and nothing else is worse than one
 * that says why it cannot act, so both carry MENU_DIS and the reason - and the
 * kernel refuses a click on a MENU_DIS item before os88_oncmd() is reached
 * (kernel/menu.inc), which the early return there says a second time.
 *
 * "(No Game)" is the reason and it is the same for both, because it is the
 * same fact: this build draws the arcade field and runs no game. The wave that
 * gives each a body deletes the marker byte and the six characters, and
 * nothing else here moves. It is NOT the fact the earlier draft named -
 * kernel/snd.inc's osapi_snd_caps does answer a constant on every kernel this
 * OS boots, so the machine is never the reason Sound is unavailable; the
 * build is.
 *
 * THE SET'S NAME IS THE WINDOW'S TITLE, the same literal in both places. The
 * package header says PACCMAN because that is what the Disk window labels the
 * file with; the kernel bar reads AM_NAME (SPEC.md 12.2), and a bar reading
 * PACCMAN over a window titled PaccMan is the drift LESSONS.md 8 records.
 * ==========================================================================*/

#define PMC_TITLE  "PaccMan"

/* The Sound item is the one string that changes, so the items array is not
 * const: os88_menu_set() writes the set's oncmd field anyway, and a set in
 * .rodata takes that patch silently and wrongly (see struct os88_menuset).
 *
 * "\x01" is MENU_DIS (apps/os88api.inc): an item whose string BEGINS with it
 * is drawn through the disabled pen and takes no click. New Game and Full
 * Screen both act in this build and carry no marker. */
static const char *pmc_items[] = {
    "New Game",
    "\x01" "Pause (No Game)",       /* greyed: no tick loop until wave 2 */
    "\x01" "Sound Off (No Game)",   /* ...and pmc_sound_item() keeps it so */
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
static int pmc_sound_on = 1;
static int pmc_paused;
static int pmc_full;

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
 * The whole field is re-marked because the card was OPAQUE over its own rect;
 * pmc_flush owns the clip region (nothing has armed one for a key - SPEC.md
 * 11.3 - and it arms one when something is covering us) and REFUSES while the
 * flag is up, which is why the flag is cleared BEFORE the flush and not
 * after. */
static int pmc_abdismiss(void *win)
{
    if (!pmc_about_up)
        return 0;
    pmc_about_up = 0;
    pmc_dirty_all();
    pmc_flush(win);
    return 1;
}

/* pmc_sound_item - the item string follows the state, so the menu will say
 * what choosing it would DO the day it can do anything. Both labels carry
 * MENU_DIS today (see the header): the state is live and the control is not. */
static void pmc_sound_item(void)
{
    pmc_items[PMC_CMD_SOUND] = pmc_sound_on ? "\x01" "Sound Off (No Game)"
                                            : "\x01" "Sound On (No Game)";
}
