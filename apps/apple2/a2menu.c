/* ============================================================================
 * os8088 - apps/apple2/a2menu.c      the four menus, from the references' own
 *                                    source
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 10). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * EVERY item string below is a `.title` field of MII's
 * `ui_gl/mii_mui_menus.h` or a string of apple2emu's `src/interface.cpp` or
 * AppleWin's, and each is named beside it. Nothing here was typed from memory
 * (LESSONS.md 1: "When you find yourself typing a menu string you did not
 * just read, stop"). The FIVE rows that have NO definer in any reference -
 * `Load Program...`, `Save Program...`, `Copy`, `Warp` and `Flashing text` -
 * are marked OURS with the reason, which for the first two is APPLE2-SPEC
 * section 12's own reasoning: there is no Disk II in this PR and a listing
 * has to get in somehow. Every other row is a transcription, INCLUDING
 * `Power On`, which used to read `Power cycle` - a phrase that is in
 * AppleWin's C++ identifiers and comments and in no reference's UI.
 *
 * ----------------------------------------------------------------------------
 * THE THREE KERNEL LIMITS THAT SHAPE THIS FILE
 * ----------------------------------------------------------------------------
 *  1. A pull-down is at most MENU_POPMAX = 11 items (kernel/menu.inc:208) and
 *     each item is truncated to MENU_MAXCH = 24 glyphs (:236). Both are facts
 *     about the machine measured on the SMALLEST screen, not preferences.
 *  2. THE BAR HAS NO SUBMENU MECHANISM AT ALL. MII builds four menus with
 *     Video and Audio as SUBMENUS of Machine (`mii_mui_menus.h:118-121`); the
 *     os8088 bar is a flat list of at most five titles, so those two
 *     submenus' rows are folded INTO Machine (APPLE2-SPEC section 10.1).
 *  3. **MACHINE THEREFORE DOES NOT FIT AND IS FOLDED**, on c64menu.c's rule:
 *     A SECTION THAT IS ENTIRELY UNAVAILABLE BECOMES ONE ITEM, AND THAT ITEM
 *     IS THE SECTION'S FIRST. MII's five video-tint rows (`Color NTSC`,
 *     `Color NTSC (Alt)`, `Color Mega2`, `Green`, `Amber`) are ALL
 *     unavailable on the windowed path - the window is monochrome - so they
 *     are one greyed `Color NTSC`; MII's `Louder`/`Quieter` pair is likewise
 *     one greyed `Louder`. That is 11 items exactly, with ONE separator.
 *     The count is measured, not estimated: five titles, eleven rows.
 *
 *  4. AN ITEM THAT IS PRESENT BUT CANNOT WORK WEARS OS88_MENU_DIS AND THE
 *     FACT THAT GREYS IT IS IN A COMMENT BESIDE IT (SPEC.md 47). Nothing is
 *     silently missing, nothing is faked, and NOTHING IS LIVE THAT ONLY
 *     TOASTS A REFUSAL - an item the user can pick and that answers "not yet"
 *     is the one thing 47 exists to stop. In wave 1 that covers every command
 *     that needs a 6502, because there is not one yet: a2_menu_state() greys
 *     them off `a2_have_cpu`, and wave 2 sets that flag and they come alive
 *     with nothing else moving.
 *  5. A CHECK ITEM'S STATE IS A `*` IN THE LABEL and the item pointer is
 *     swapped between the two spellings. MII reads its state from a TICK
 *     glyph (`MUI_GLYPH_TICK`); this kernel's menu has no check mark and its
 *     face has no glyph for one (LESSONS.md 8), and apps/tracker's `*` is the
 *     idiom. It is NOT the MENU_DIS twin, because MENU_DIS is 47's "you
 *     cannot have this": greying the item that is ON would report the feature
 *     as unavailable AND make it impossible to turn off.
 *
 *     AND EVERY ROW OF A MARKED GROUP OWNS THE COLUMN, whatever its state.
 *     `Fast: 3.5MHz` shipped without the two-glyph slot while its two radio
 *     partners `Normal: 1MHz` and `Warp` had it, and the sixteen missing
 *     pixels are VISIBLE: one label in the pull-down starting two cells left
 *     of the others. MII ticks `mhz1` and `mhz3` from the same
 *     MUI_MENUBAR_ACTION_PREPARE arm (mii_mui_menus.c:165-175), so both rows
 *     own a tick slot there too; the same was true of `Color NTSC` and `Mute`
 *     against `Flashing text` in Machine, where MII ticks `vdc0`
 *     (:139-146) and `aud0` (:151-156) from that same arm. It is also what
 *     lets wave 5 revive `Mute` without re-widening the row and shuffling
 *     every label under it.
 *
 * AND THE ELLIPSIS IS THREE DOTS. MII writes `…` (U+2026) and the kernel face
 * is ASCII 32..126, which draws nothing for it - the same rule that spells
 * MII's `][+` and Open-Apple glyphs out in words.
 * ==========================================================================*/

/* The menu and item indices os88_oncmd is handed. They are positions in the
 * arrays below and the two must not drift: an item inserted above one of
 * these moves every command under it. */
#define A2_M_FILE    0
#define A2_M_EDIT    1
#define A2_M_MACHINE 2
#define A2_M_CPU     3

#define A2_I_LOAD    0                      /* File, 4 items */
#define A2_I_SAVE    1
#define A2_I_QUIT    3

#define A2_I_COPY    0                      /* Edit, 2 items */
#define A2_I_PASTE   1

#define A2_I_OARESET 0                      /* Machine, 11 items */
#define A2_I_RESET   1
#define A2_I_POWER   2
#define A2_I_SLOTS   3
#define A2_I_JOY     4
#define A2_I_FULLSCR 6
#define A2_I_TINT    7
#define A2_I_FLASH   8
#define A2_I_MUTE    9
#define A2_I_LOUDER  10

#define A2_I_NORMAL  0                      /* CPU, 8 items */
#define A2_I_FAST    1
#define A2_I_WARP    2
#define A2_I_STOP    4
#define A2_I_RUN     5
#define A2_I_STEP    6
#define A2_I_NEXT    7

#define D "\001"                            /* OS88_MENU_DIS is 1 (SPEC.md 47) */
#define ON  "* "                            /* rule 5's two spellings: TWO */
#define OFF "  "                            /* glyphs, so the label does not
                                             * shuffle when the state moves */

/* The separator is a MENU_DIS item whose label is a rule, which is the
 * kernel's own spelling of one (kernel/menu.inc:2595). It occupies an item
 * index, which is why Machine can afford exactly one. */
static const char a2_sep[] = D "-----------------";

/* --- File ---------------------------------------------------------------- */
/* MII's m_file_menu is `No Drives Installed…` (disabled), `Load & Run
 * Binary…` (disabled, handler commented out) and `Quit`. The first is the
 * Disk II row and belongs to Machine > Configure Slots... here, which is
 * where MII itself puts the drives; the second has no working definer
 * anywhere. So Quit is MII's and the two Program rows are OURS, with
 * APPLE2-SPEC section 12's reason: there is no Disk II in this PR, and a
 * listing has to get in somehow. */
static const char *a2_file_items[] = {
    D "Load Program...",                    /* OURS (section 12). Greyed until
                                             * there is a 6502 to load a
                                             * program INTO */
    D "Save Program...",                    /* OURS (section 12) */
    a2_sep,
    "Quit"                                  /* mii_mui_menus.h m_file_menu.
                                             * LIVE, and answered in the
                                             * RESIDENT half: it is the one
                                             * command that must work on a
                                             * disk whose APPLE2.OVL is
                                             * missing (section 10.1) */
};

/* --- Edit ---------------------------------------------------------------- */
/* MII has NO Edit menu at all; apple2emu's is one row, `Paste`
 * (src/interface.cpp:363-369). Copy is OURS - nothing in any reference copies
 * the text page out - and it is here because a machine you can paste INTO and
 * not out of is half a clipboard. */
static const char *a2_edit_items[] = {
    D "Copy",                               /* OURS. Greyed: no text page to
                                             * copy until there is a machine */
    D "Paste"                               /* interface.cpp:365 */
};

/* --- Machine ------------------------------------------------------------- */
static const char *a2_mach_items[] = {
    D "Open-Apple-Control-Reset",           /* mii_mui_menus.h m_machine_menu:
                                             * MUI_GLYPH_OAPPLE "-Control-
                                             * Reset", with the glyph spelled
                                             * out. 24 glyphs, exactly
                                             * MENU_MAXCH */
    D "Control-Reset",                      /* m_machine_menu */
    D "Power On",                           /* AppleWin help/keyboard.html:15,
                                             * `F2 (Power On)` - its own
                                             * user-visible name for the cold
                                             * start, and MII has no such row
                                             * at all. NOT `Power cycle`: that
                                             * phrase is in AppleWin's C++
                                             * identifiers and comments
                                             * (Memory.cpp, CardManager.cpp)
                                             * and in no reference's UI, so it
                                             * would be typed from memory.
                                             * Its confirmation is
                                             * WinFrame.cpp:2002-2012's two
                                             * rows, `Are you sure you want to
                                             * reboot?` / `(All data will be
                                             * lost!)` */
    D "Configure Slots...",                 /* m_machine_menu. THE FACT:
                                             * No Disk II in this build.
                                             * (section 10.3, and Disk II is a
                                             * follow-up PR.) THE SECOND
                                             * SENTENCE IS WAVE 4'S - `Load
                                             * Program reads an Applesoft
                                             * program, and Paste types a
                                             * listing in` is untrue of a
                                             * build whose File > Load
                                             * Program... and Edit > Paste are
                                             * both greyed, and SPEC.md 47's
                                             * rule 5 is that a greying states
                                             * a FACT, never a guess about a
                                             * later wave */
    D "Joystick...",                        /* m_machine_menu, and it is
                                             * `.disabled = 1` in MII's OWN
                                             * table - the authentic grey.
                                             * THE FACT: The paddles answer
                                             * centre and there are no buttons
                                             * in this build. The F1/F2
                                             * sentence - with its `a
                                             * departure from AppleWin's
                                             * Left-Alt / Right-Alt` - returns
                                             * in WAVE 3, which is the wave
                                             * that reads the buttons */
    a2_sep,
    "Toggle Fullscreen",                    /* m_video_menu. LIVE, and
                                             * RESIDENT: a WF_FULL window has
                                             * no menu bar, so a chord that
                                             * had to load APPLE2.OVL would
                                             * refuse on a bar the user cannot
                                             * see (section 6.3) */
    D OFF "Color NTSC",                     /* m_video_menu, FOLDING `Color
                                             * NTSC (Alt)`, `Color Mega2`,
                                             * `Green` and `Amber` by rule 3.
                                             * THE FACT: The window is
                                             * monochrome. (section 10.3.) THE
                                             * SECOND SENTENCE IS WAVE 5'S -
                                             * `Colour is in the foreign video
                                             * mode` points at a mode this
                                             * build does not have, and a
                                             * greying that names a route the
                                             * reader cannot take is rule 5's
                                             * guess */
    ON "Flashing text",                     /* OURS: the flash phase's own
                                             * control (section 7.6). LIVE -
                                             * the phase exists from wave 1.
                                             * WAVE 3 greys it on the
                                             * CPU_8086 tier with the MEASURED
                                             * cost of one flip in the fact */
    D OFF "Mute",                           /* m_audio_menu. THE FACT: There
                                             * is no speaker in this build.
                                             * (section 10.3) - and it is
                                             * a2_have_snd that greys it, so
                                             * wave 5 revives it with nothing
                                             * else moving */
    D "Louder"                              /* m_audio_menu, FOLDING `Quieter`
                                             * by rule 3. THE FACT: The
                                             * Apple's speaker is a one-bit
                                             * toggle. There is no volume on
                                             * it. (section 10.3) */
};

/* --- CPU ----------------------------------------------------------------- */
/* MII's m_cpu_menu whole, plus Warp. MII ticks `Normal: 1MHz` when the speed
 * is ~1 and `Fast: 3.5MHz` when it is over 2, and retitles Stop -> Stopped
 * and Running -> Continue from its MUI_MENUBAR_ACTION_PREPARE arm
 * (mii_mui_menus.c:130-205), which is what a2_menu_state does here. */
static const char *a2_cpu_items[] = {
    D ON "Normal: 1MHz",                    /* m_cpu_menu. The radio that is
                                             * always selected on this machine
                                             * - it is what turns Warp OFF.
                                             * Greyed off a2_have_cpu like
                                             * every other CPU row, NOT
                                             * permanently: the `D` here is
                                             * only the wave-1 spelling and
                                             * a2_menu_state rewrites it */
    D OFF "Fast: 3.5MHz",                   /* m_cpu_menu. THE FACT: This
                                             * machine runs at 1.02 MHz. Warp
                                             * is this port's speed control
                                             * and is beside it.
                                             * (section 10.3) */
    D OFF "Warp",                           /* OURS: this port's speed control,
                                             * which is what the row above
                                             * points at. Greyed off
                                             * a2_have_cpu, and rewritten by
                                             * a2_menu_state - a row whose `D`
                                             * is baked into the literal and
                                             * never rewritten is a row wave 2
                                             * cannot revive */
    a2_sep,
    D "Stop",                               /* m_cpu_menu, retitled `Stopped` */
    D "Running",                            /* m_cpu_menu, retitled `Continue` */
    D "Step",                               /* m_cpu_menu. THE FACT: There is
                                             * no debugger in this port. */
    D "Next"                                /* m_cpu_menu, same fact */
};

/* AM_NAME IS THE SHORT PRODUCT NAME (section 16.1). The kernel bar shows the
 * instance name unless a menu set says otherwise, and the header's 15-char
 * name is also the .OVL stem and the association - so it cannot say
 * `Apple II Plus Emulator`. Two strings with a stated rule, not three that
 * drift: the long form is the window title and the About panel's first row,
 * and this is the short one. */
static struct os88_menuset a2_menus = {
    "Apple II+", 0, 4,
    {
        { "File",    a2_file_items,  4 },
        { "Edit",    a2_edit_items,  2 },
        { "Machine", a2_mach_items, 11 },
        { "CPU",     a2_cpu_items,   8 }
    }
};

/* ==========================================================================
 * THE STATE OF THE ITEMS
 * ========================================================================*/
/* a2_menu_state - point each item at the spelling its state calls for.
 *
 * WAVE 1 IS THE `a2_have_cpu` HALF ONLY. Every command whose body needs a
 * 6502 is greyed until wave 2 sets that flag: rule 4 forbids a live item that
 * can only refuse, and a "not in this build yet" toast is exactly that. MII's
 * dynamic retitling (Stop -> Stopped, Running -> Continue) is wired here and
 * takes effect the moment there is a machine to be stopped. */
static void a2_menu_state(void)
{
    a2_file_items[A2_I_LOAD] = a2_have_cmd ? "Load Program..."
                                           : D "Load Program...";
    a2_file_items[A2_I_SAVE] = a2_have_cmd ? "Save Program..."
                                           : D "Save Program...";
    a2_edit_items[A2_I_COPY] = a2_have_cmd ? "Copy"  : D "Copy";
    a2_edit_items[A2_I_PASTE] = a2_have_cmd ? "Paste" : D "Paste";
    /* THE TWO RESET CHORDS ARE THE ROWS WAVE 2 REVIVES, and they are the only
     * ones: their bodies are section 4.5's reset line, which is this wave's
     * own subject, and neither touches RAM. Power On is the third of the trio
     * and stays on a2_have_cmd because section 10.2 gives it a TWO-ROW
     * CONFIRMATION - `Are you sure you want to reboot?` / `(All data will be
     * lost!)`, AppleWin WinFrame.cpp:2002-2012 - which arrives with the rest
     * of the commands in wave 4. A data-loss row shipped without the
     * confirmation the contract gives it is not the item the SPEC describes. */
    a2_mach_items[A2_I_OARESET] = a2_have_cpu ? "Open-Apple-Control-Reset"
                                              : D "Open-Apple-Control-Reset";
    a2_mach_items[A2_I_RESET] = a2_have_cpu ? "Control-Reset"
                                            : D "Control-Reset";
    a2_mach_items[A2_I_POWER] = a2_have_cmd ? "Power On" : D "Power On";
    a2_mach_items[A2_I_FLASH] = a2_fl_ok ? ON "Flashing text"
                                         : OFF "Flashing text";
    /* THE THREE THAT USED TO BE GREYED FOR EVER. `D` baked into a literal
     * that a2_menu_state never rewrites is a row no later wave can revive,
     * and a greyed row with no fact is what SPEC.md 47 forbids; both facts
     * are section 10.3's.
     *
     * `Fast: 3.5MHz` AND `Color NTSC` KEEP THEIR BAKED `D` ON PURPOSE, and
     * that is the rule's other half rather than an exception to it: the fact
     * that greys each is a property of the MACHINE and of the windowed PATH
     * ("this machine runs at 1.02 MHz", "the window is monochrome"), not of a
     * flag some later wave sets, so there is nothing for a rewrite to read.
     * What they DID need is the two-glyph column, which rule 5 above has, and
     * they carry it in the literal. Mute is the opposite case and is
     * rewritten below, because a2_have_snd is exactly such a flag. */
    a2_cpu_items[A2_I_NORMAL] = a2_have_cmd ? ON "Normal: 1MHz"
                                            : D ON "Normal: 1MHz";
    a2_cpu_items[A2_I_WARP] = a2_have_cmd ? OFF "Warp" : D OFF "Warp";
    a2_mach_items[A2_I_MUTE] = a2_have_snd ? OFF "Mute" : D OFF "Mute";
    a2_cpu_items[A2_I_STOP] = a2_have_cmd
        ? ((a2_state == A2_ST_RUN) ? "Stop" : ON "Stopped")
        : D "Stop";
    a2_cpu_items[A2_I_RUN] = a2_have_cmd
        ? ((a2_state == A2_ST_RUN) ? ON "Running" : "Continue")
        : D "Running";
}

/* ==========================================================================
 * THE DISPATCHER - two compares and then an ovl_, except the two that cannot
 * be (APPLE2-SPEC section 15.5)
 * ========================================================================*/
/* os88_oncmd IS DISPATCHED UNDER THE DESKTOP'S GFX LOCK, so nothing here runs
 * a body: File > Quit sets a latch the WAKE spends, and everything else goes
 * through the overlay fence, which refuses politely rather than going to the
 * floppy for 400 ms with the whole desktop stopped behind it. */
void os88_oncmd(int item, int menu, void *win)
{
    if (a2_state == A2_ST_DEAD)
        return;

    if (menu == A2_M_FILE && item == A2_I_QUIT) {
        /* THE ONE COMMAND ANSWERED IN THE RESIDENT HALF, because it is the
         * one that must work on a disk whose APPLE2.OVL is missing. It goes
         * through OSAPI_WM_CLOSE, spent from the WAKE and not from here. */
        a2_exit_req = 1;
        a2_kick = 1;
        os88_wm_wake(win);
        return;
    }
    if (menu == A2_M_MACHINE && item == A2_I_FULLSCR) {
        a2_fullscreen_toggle(win);          /* RESIDENT for section 6.3's
                                             * reason: the way BACK has to
                                             * work on a bar that is not there */
        return;
    }
    if (menu == A2_M_MACHINE && item == A2_I_FLASH) {
        a2_fl_ok = !a2_fl_ok;
        if (!a2_fl_ok && a2_fl_phase) {
            a2_fl_phase = 0;                /* turning it off leaves the text
                                             * in its NORMAL form, not frozen
                                             * inverse */
            a2_flash_force();
        }
        /* AND THE HEARTBEAT IS RE-ARMED HERE. os88_ontimer stops re-arming
         * itself while the phase is off, so this is the other half of that
         * sentence: without it the phase comes back on and never flips again
         * on a kernel that HAS the timer. The refusal is tested, as it is at
         * launch (SPEC.md 13.8.2). */
        if (a2_fl_ok && a2_tmr_ok) {
            a2_fl_tick = os88_ticks();
            a2_tmr_ok = os88_wm_timer(win, A2_FLASH_TICKS) == 0;
        }
        a2_menu_state();
        a2_kick = 1;
        os88_wm_wake(win);
        return;
    }
    if (!a2_ovl_ready(win))
        return;
    ovl_a2_cmd(menu, item, win);
    a2_kick = 1;
    os88_wm_wake(win);
}

/* ==========================================================================
 * FULL SCREEN (APPLE2-SPEC section 7.8)
 * ========================================================================*/
/* OSAPI_FULLSCREEN is SPEC.md 11.2's WINDOW LATCH and not SPEC.md 53's
 * exclusive bracket - the frame becomes the whole screen, menu bar and dock
 * included, until we let go.
 *
 * IT REPAINTS SYNCHRONOUSLY IN BOTH DIRECTIONS, so the success arm does
 * nothing: a shadow-invalidate there is pure double-draw, because os88_paint
 * has already run and already invalidated. It is the REFUSED arm that owes a
 * repaint, and it owes one because the kernel did nothing at all.
 *
 * WAVE 1 IS 1:1 AND SAYS SO. The magnification table - 2x on VGA, 2x
 * horizontal on CGA and Hercules, 1:1 on the CPU_8086 tier - is written in
 * wave 3 FROM tests/a2band's measured numbers (section 7.8), and a2_band_x2
 * and its table are already here and already benched. Nothing is faked and
 * nothing is silently missing: fullscreen works, it does not magnify. */
static void a2_fullscreen_toggle(void *win)
{
    /* AND THE PANEL DOES NOT SURVIVE A GEOMETRY CHANGE. This is reachable
     * with the About panel UP - os88_about is the kernel's NAME pull-down and
     * a menu-bar click is not a W_ONCLICK, so nothing has closed it - and
     * OSAPI_FULLSCREEN repaints the window whole and SYNCHRONOUSLY, so
     * os88_paint runs NESTED inside the call below. os88_paint recomputes the
     * hold range from the fresh geometry now, which is half of it; the other
     * half is here, because a panel measured for the old box would be
     * REPLACED at its new place over rows the flush had just composed and
     * blitted - ~226 ms of pure double-draw (PERFORMANCE.md rule 2),
     * invisible in any screendump.
     *
     * So the panel comes down FIRST, and its rect is DAMAGE the way a click's
     * is: a2_blank_rect forces the lines it covered, so the nested repaint
     * draws them, and if the latch below is REFUSED nothing repaints us at
     * all - which is why those rows are still owed to the next wake.
     * apps/c64/c64.c's c64_fullscreen_toggle carries this fix with both
     * halves named. */
    if (a2_abt_up) {
        a2_about_gone();                    /* the latch down, the hold range
                                             * EMPTIED - nothing holds a row
                                             * against a rect that is not on
                                             * the glass any more - and the
                                             * rect handed on as damage */
        a2_dirty_any = 1;
    }
    if (os88_fullscreen(win, a2_full ? 0 : 1) < 0) {
        a2_say("Another window has it.");
        a2_st_dirty = 1;
        a2_kick = 1;                        /* ...and the REFUSAL owes the
                                             * panel's rows a draw, because
                                             * the kernel did nothing at all */
        os88_wm_wake(win);
        return;
    }
    a2_full = !a2_full;
}
