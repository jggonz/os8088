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
 * NOTHING IN THIS MENU IS GREYED, AND THE TWO THAT WERE WENT LIVE THE WAVE
 * THAT GAVE THEM A BODY. Pause was greyed while the image had no tick loop to
 * stop (wave 1) and Sound while it had no sound code to silence (waves 1-2);
 * each un-greying was the deletion of ONE marker byte and its reason, which is
 * exactly the shape SPEC.md 47 predicts. Neither fact was ever the MACHINE:
 * kernel/snd.inc's osapi_snd_caps answers the constant SND_CAP_TONE |
 * SND_CAP_PCM_EXCL on every kernel this OS boots, so a PaccMan that greyed
 * Sound because "there is no speaker" would be greying a guess.
 *
 * THE TWO LIVE ITEMS THAT CARRY STATE SAY WHICH STATE THEY ARE IN, and the
 * label is the only surface either has: the kernel's one marker is MENU_DIS
 * (there is no check mark), this package has no status line - its content is
 * the 224-pixel arcade field - and the title bar does not change. So each
 * label names the ACTION on offer, which is the wording an imperative label
 * has to have to be true: "Pause" while running and "Resume" while stopped,
 * "Sound Off" while sound is on and "Sound On" while it is off. That is the
 * lesson wave 2's greyed "Sound Off (No Sound Yet)" taught from the wrong
 * side - an imperative asserts the state it would leave, so it may not be
 * worn by a control that cannot act.
 *
 * THE SET'S NAME IS THE WINDOW'S TITLE, the same literal in both places. The
 * package header says PACCMAN because that is what the Disk window labels the
 * file with; the kernel bar reads AM_NAME (SPEC.md 12.2), and a bar reading
 * PACCMAN over a window titled PaccMan is the drift LESSONS.md 8 records.
 * ==========================================================================*/

#define PMC_TITLE  "PaccMan"

/* Pause and Sound are the two strings that change, so the items array is not
 * const: os88_menu_set() writes the set's oncmd field anyway, and a set in
 * .rodata takes that patch silently and wrongly (see struct os88_menuset).
 *
 * "\x01" is MENU_DIS (apps/os88api.inc): an item whose string BEGINS with it
 * is drawn through the disabled pen and takes no click. All four items act in
 * this build and NONE of them carries the marker. */
static const char *pmc_items[] = {
    "New Game",
    "Pause",                            /* pmc_pause_item() swaps this one */
    "Sound Off",                        /* ...and pmc_sound_item() this one */
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
 * EVERY CHROME KEY THAT IS THE ONLY WAY TO DO ITS THING IS ON THE LAST TWO
 * LINES, and that is a requirement rather than a nicety. PaccMan has no status
 * line - its content is the 224-pixel arcade field - so this card is the ONLY
 * place inside the program a key can be discovered, and the precedent it takes
 * its menu from advertises its keys in its footer (apps/pacman/pacman.asm). N
 * was bound, documented in the README and reachable from nowhere in the program
 * until the line was reflowed to carry it - and SPACE was in exactly the same
 * position after it was bound as a second Pause key: bound, specified in
 * SPEC.md 91, advertised nowhere, which is why the keys now take two lines
 * rather than one. The precedent spells the pair out too ('PAUSED - P OR SPACE
 * TO RESUME').
 *
 * Esc IS THE ONE BOUND KEY NOT ON THE CARD, and it is the exception the claim
 * is narrowed for rather than an oversight. It leaves full screen, which F
 * already does in both directions, so it is a convenience with a route beside
 * it - where N, P/SPACE and F each have no other key at all (the Game menu
 * offers New Game and Pause, and Full Screen; nothing else offers them). The
 * bound is what makes it a choice: the widest line here is 24 cells, and
 * 'F full/Esc out. P/Space pause.' - the shortest phrasing that carries the
 * pair - is 30. Adding an eleventh line is not the way out either: 11 * 12 + 14
 * is 146 rows against CGA's 144-row content box, so the widget would clamp and
 * cut the last line off (hosttest/pmcuitest.c gates both numbers).
 *
 * THE 24-CELL BOUND IS pmcuitest's AND IT IS THE TIGHTER ONE. The widget's own
 * clamp is `widest * 8 + 2 * OS88UI_ABPADX` against the live content box, which
 * on the 224-pixel arcade field is 25 cells; the row asserts 24 so that one
 * cell of slack survives a font or padding change in somebody else's file. */
static const char *pmc_about_lines[] = {
    "PaccMan for os8088",
    "A C port of pacman.c,",
    "commit 0f5ec5a",
    "(c) 2020 Andre Weissflog",
    "MIT. floooh/pacman.c",
    "Tiles/sprites: Pac-Man",
    "arcade ROMs (Namco)",
    "Rules: Pac-Man Dossier",
    "Arrows/WASD move. N new.",
    "F full. P/Space pause.",
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

/* pmc_anykey and pmc_anyheld are the same pair one screen along, for the
 * attract screen's "press any key"; they are defined in paccman.c because
 * pmc_intro.c reads pmc_anyheld and is #included ahead of this file. */

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
        pmc_anykey = 0;
        pmc_anyheld = 0;
        return;
    }
    h = 0;
    if (os88_key_down(PMC_SC_UP)    || os88_key_down(PMC_SC_W)) h |= PMC_IN_UP;
    if (os88_key_down(PMC_SC_DOWN)  || os88_key_down(PMC_SC_S)) h |= PMC_IN_DOWN;
    if (os88_key_down(PMC_SC_LEFT)  || os88_key_down(PMC_SC_A)) h |= PMC_IN_LEFT;
    if (os88_key_down(PMC_SC_RIGHT) || os88_key_down(PMC_SC_D)) h |= PMC_IN_RIGHT;
    pmc_held = (unsigned char) (h | pmc_latch);
    pmc_latch = 0;

    /* THE ANY KEY IS THE SAME LATCH ONE SCREEN ALONG, and it must be cleared
     * here as well as consumed by the intro: a key pressed during a ROUND
     * would otherwise still be sitting in the byte when GAME OVER hands
     * control back to the attract screen, and would start the next game
     * before the first ghost had been introduced. One frame of "held" is all
     * a press becomes, on either screen. */
    pmc_anyheld = pmc_anykey;
    pmc_anykey = 0;
}

/* pmc_input_dis - input_disable(), pacman.c 917-921. The reference memsets its
 * whole input struct, which clears `enabled` and every level with it; here
 * that is these five bytes. It is what the attract screen's any-key and the
 * GAME OVER sequence both reach for, and what stops the key that started a
 * game from starting a second one thirty ticks later, inside the fade. */
static void pmc_input_dis(void)
{
    pmc_input_on = 0;
    pmc_held = 0;
    pmc_latch = 0;
    pmc_anykey = 0;
    pmc_anyheld = 0;
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
 * The card was OPAQUE over its own rect, so what it covered is re-marked -
 * and MARKING IS ALL EITHER PATH DOES. The flag is cleared first because
 * pmc_flush refuses while it is up, and the worker's next frame is what
 * composes the spans: it owns the clip region (nothing has armed one for a key
 * - SPEC.md 11.3 - and it arms one when something is covering us) and it may
 * break its own lock hold every PMC_HOLD_BANDS bands, which is the whole
 * reason the drawing is left to it.
 *
 * WHAT IT COVERED, AND ON VGA THAT IS NOT THE WHOLE FIELD. pmc_dirty_all is 36
 * bands at full width - 36 x 69.78 ms, about 2.5 s of XT - reached from an
 * ordinary keystroke. apps/os88ui.inc measures the card as
 * `lines * OS88UI_ABLH + 2 * OS88UI_ABPADY`, clamps that to the content box and
 * centres it, so ten lines is 134 rows; this routine turns that y-range into a
 * band range with a band of slack each side.
 *
 * THE SAVING IS THE ADAPTER'S AND ON THIS PATH IT IS ZERO ON TWO OF THE THREE.
 * On VGA and EGA the field is 288 rows of 8-row bands, so 134 rows is bands
 * 8..27 - 20 of 36, and the sixteen the card never touched are ~1.1 s of XT
 * not spent. On CGA and Hercules the content box is 144 rows and a band is 4,
 * so the same 134-row card is d0 = 5, d1 = 139 and the band range is the WHOLE
 * FIELD, because a card 134 rows tall in a 144-row box leaves five rows above
 * and five below. The narrowing buys nothing there and is not claimed to - a
 * 1bpp band is 4 screen rows rather than 8, so the wall clock is not
 * 36 x 69.78 ms, but it is a whole-field compose and that is why NOTHING
 * inside a kernel callback flushes it (paccman.c's pmc_frame). What the CGA
 * case IS narrowed by is the other caller's COLUMNS - pmc_ab_box answers those
 * too, and pmc_repaint marks the complement of the card rather than all 36
 * bands when a whole repaint arrives with it up.
 *
 * PMC_AB_LH, PMC_AB_PADY AND PMC_AB_PADX MIRROR apps/os88ui.inc, which is two
 * copies of one fact - so THIS range is deliberately an OVER-APPROXIMATION
 * with two bands of slack each side, and its only failure mode is drawing a
 * band more than it had to. It cannot leave a hole in the other direction
 * unless the widget's pitch grows by two whole bands, and a layout that has
 * not run yet or a window that has gone answers pmc_dirty_all as before. The
 * arithmetic itself is pmc_ab_box's, one routine below, because the other
 * caller needs the same rectangle with its slack the other way. */
#define PMC_AB_LH    12         /* = OS88UI_ABLH: the card's line pitch      */
#define PMC_AB_PADY   7         /* = OS88UI_ABPADY: its air above and below  */
#define PMC_AB_PADX  12         /* = OS88UI_ABPADX: ...and either side of it,
                                 * which pmc_ab_box needs and pmc_ab_mark
                                 * never did: a COMPLEMENT has columns in it */

/* pmc_ab_box - WHERE THE CARD IS, in FIELD tiles, and it answers 0 when it
 * cannot say. The LAYOUT IS THE CALLER'S - pmc_layout has already run - so
 * this is arithmetic and no thunk.
 *
 * It mirrors apps/os88ui.inc's own `os88ui_about_d`: the card is the widest
 * line plus PMC_AB_PADX either side and n lines of PMC_AB_LH plus PMC_AB_PADY
 * above and below, each CLAMPED to the content box and then centred in it.
 * Three copies of one fact would be two too many, so tests/unit/t_paccman.py
 * reads the two constants out of both files in the fast tier - but the LAYOUT
 * is still somebody else's, which is why both callers keep a margin and why
 * they keep it in OPPOSITE directions:
 *
 *   pmc_ab_mark wants a SUPERSET of what the card covered and adds a band of
 *   slack each side, its only failure mode being a band drawn that need not
 *   have been;
 *   pmc_repaint wants a SUBSET - what the card certainly covers, so that
 *   everything else is drawn - and this routine's answer is that one: the
 *   range is pulled IN by a band and a column at each edge, and an edge that
 *   is already off the field is not pulled in at all because there is nothing
 *   beyond it to leave stale.
 *
 * A card that covers no whole band or no whole column answers 0, and its
 * caller falls back to the whole field. */
static int pmc_ab_box(void)
{
    const char **l;
    const char *p;
    int n, w, mw, h, d0, d1, x0, x1;

    if (pmc_ch <= 0 || pmc_cw <= 0)
        return 0;
    n = 0;
    mw = 0;
    for (l = pmc_about_lines; *l; l++) {
        n++;
        w = 0;
        for (p = *l; *p; p++)            /* OSAPI_FONT_WIDTH is 8 px a cell */
            w += 8;
        if (w > mw)
            mw = w;
    }
    h = (n << 3) + (n << 2) + 2 * PMC_AB_PADY;      /* n * PMC_AB_LH */
    if (h > pmc_ch)
        h = pmc_ch;
    w = mw + 2 * PMC_AB_PADX;
    if (w > pmc_cw)
        w = pmc_cw;

    d0 = pmc_cy + ((pmc_ch - h) >> 1) - pmc_fy;     /* card top, in FIELD  */
    d1 = d0 + h - 1;                                /* rows and columns    */
    x0 = pmc_cx + ((pmc_cw - w) >> 1) - pmc_fx;
    x1 = x0 + w - 1;
    if (d1 < 0 || d0 > pmc_fh - 1 || x1 < 0 || x0 > PMC_FIELD_W - 1)
        return 0;                       /* not over the field at all       */

    pmc_ab_ty0 = (d0 <= 0) ? 0 : ((d0 >> pmc_rsh) + 1);
    pmc_ab_ty1 = (d1 >= pmc_fh - 1) ? PMC_TILES_Y - 1
                                    : ((d1 >> pmc_rsh) - 1);
    pmc_ab_c0  = (x0 <= 0) ? 0 : ((x0 >> 3) + 1);
    pmc_ab_c1  = (x1 >= PMC_FIELD_W - 1) ? PMC_TILES_X - 1 : ((x1 >> 3) - 1);
    if (pmc_ab_ty0 > pmc_ab_ty1 || pmc_ab_c0 > pmc_ab_c1)
        return 0;
    return 1;
}

static void pmc_ab_mark(void *win)
{
    int ty0, ty1;

    /* ...AND WHAT IT COVERED MAY BE THE FADE'S BLACK rather than the field.
     * pmc_shblack says the black fill is already on the glass, and the card
     * has just been drawn over the middle of it, so the shadow is a lie until
     * the fill is sent again. Clearing the byte is the whole fix: the worker's
     * next pmc_flush sends the fill, and the marked spans it leaves behind are
     * cleaned by that fill (pmc_draw.c).
     *
     * IT BELONGS HERE AND NOT AT A CALL SITE. This routine means "what the
     * card covered is no longer on the glass", it is called from the two
     * card-down paths and nowhere else, and the first version cleared the byte
     * in pmc_abdismiss only - so os88_oncmd's own dismissal (a Sound toggle, a
     * Pause, a refused Full Screen, all taken mid-fade) left the shadow still
     * claiming black, pmc_fade_black returned at its first test, and the
     * card's rectangle stayed on the glass for the rest of the fade.
     *
     * It is ABOVE the pmc_layout refusal deliberately: a refused layout takes
     * the pmc_dirty_all path, and that path owes the black fill just as much. */
    if (pmc_black)
        pmc_shblack = 0;

    if (!pmc_layout(win) || !pmc_ab_box()) {
        pmc_dirty_all();
        return;
    }

    /* pmc_ab_box pulled the range IN by a band at each edge, because its other
     * caller needs a SUBSET; this one needs a SUPERSET, so the same band comes
     * back off and one more goes with it. Two bands of slack on a range whose
     * constants are a mirror of somebody else's file, and its only failure
     * mode is a band drawn that need not have been. */
    ty0 = pmc_ab_ty0 - 2;
    ty1 = pmc_ab_ty1 + 2;
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
    pmc_ab_mark(win);           /* which also clears pmc_shblack: what the
                                 * card covered may be the fade's black */
    return 1;                   /* AND IT DOES NOT DRAW. This runs on the
                                 * ORDINARY key path, inside a kernel callback
                                 * whose gfx lock is not ours to drop, so a
                                 * flush here is `brk = 0` over every band the
                                 * card covered: 20 on VGA, ALL 36 on a 1bpp
                                 * adapter (see pmc_ab_mark's arithmetic). The
                                 * worker's next frame composes exactly those
                                 * spans chunked and interruptible, and it does
                                 * so even when the window is PAUSED - which is
                                 * the case that used to make this flush look
                                 * unavoidable, because os88_about pauses a
                                 * game. paccman.c's pmc_frame is where that is
                                 * written out. */
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

/* pmc_sound_item - the same swap for Sound, and for the same two reasons: the
 * kernel has no check-mark marker to show a toggle's state with, and an
 * imperative label asserts the state it would LEAVE. "Sound Off" means "turn
 * it off", so it is what a program with sound ON wears. Every write of
 * pmc_snd_on calls this; there is exactly one, in os88_oncmd. */
static void pmc_sound_item(void)
{
    pmc_items[PMC_CMD_SOUND] = pmc_snd_on ? "Sound Off" : "Sound On";
}
