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
 *     is the one thing 47 exists to stop. IT IS TWO FLAGS AND NOT ONE, and
 *     the split is WHERE THE BODIES ARE (section 10.3): `a2_have_cpu` says
 *     there is a 6502, which wave 2 gave, and revives the two rows whose
 *     bodies are wave 2's own subject - `Control-Reset` and
 *     `Open-Apple-Control-Reset`. `a2_have_cmd` says the COMMAND has a body,
 *     which wave 4 gives, and holds Load Program..., Save Program..., Copy,
 *     Paste, Power On, Stop/Continue and Warp. A row greyed off either flag
 *     has no user-visible fact beside it, because there is no user of a wave
 *     - what ships is the PR - and the alternative, a live row that can only
 *     answer "not yet", is what 47 exists to stop.
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
    D "Load Program...",                    /* OURS (section 12). Greyed off
                                             * a2_have_cmd and not a2_have_cpu
                                             * (section 10.3), because what it
                                             * waited for was the BODY - and
                                             * wave 4 wrote it. The `D` here is
                                             * only the launch spelling;
                                             * a2_menu_state rewrites it */
    D "Save Program...",                    /* OURS (section 12). a2_have_cmd,
                                             * for the row above's reason */
    a2_sep,
    "Quit"                                  /* mii_mui_menus.h m_file_menu.
                                             * LIVE, and answered in the
                                             * RESIDENT half: it is the one
                                             * command that must work on a
                                             * disk whose APPLE2.OVL is
                                             * missing (section 10.1).
                                             * IT DOES NOT CONFIRM AND MII
                                             * DOES (mii_mui_menus.c:243-249,
                                             * `Do you really want to quit the
                                             * emulator?`), which section 10.2
                                             * records as a stated departure:
                                             * the OS owns the close, a close
                                             * box is one click away with no
                                             * confirmation at all, so a
                                             * confirming Quit is the slower
                                             * of two routes to the same loss
                                             * and does not protect the fast
                                             * one. Power On confirms because
                                             * nothing in the OS stands behind
                                             * it */
};

/* --- Edit ---------------------------------------------------------------- */
/* MII has NO Edit menu at all; apple2emu's is one row, `Paste`
 * (src/interface.cpp:363-369). Copy is OURS - nothing in any reference copies
 * the text page out - and it is here because a machine you can paste INTO and
 * not out of is half a clipboard. */
static const char *a2_edit_items[] = {
    D "Copy",                               /* OURS. Greyed off a2_have_cmd
                                             * (section 10.3) - the reader that
                                             * walks the text page is wave 4's
                                             * and is written. The `D` is the
                                             * launch spelling only */
    D "Paste"                               /* interface.cpp:365 */
};

/* --- Machine ------------------------------------------------------------- */
static const char *a2_mach_items[] = {
    D "Open-Apple-Control-Reset",           /* mii_mui_menus.h m_machine_menu:
                                             * MUI_GLYPH_OAPPLE "-Control-
                                             * Reset", with the glyph spelled
                                             * out. 24 glyphs, exactly
                                             * MENU_MAXCH */
    D "Control-Reset  Ctrl+F2",             /* m_machine_menu, AND THE ONE ROW
                                             * ON THE BAR THAT CAN CARRY ITS
                                             * CHORD. MII supplies `.kcombo`
                                             * on this row and on three others
                                             * here (mii_mui_menus.h:98-104,
                                             * :48-52) and c64menu.c captions
                                             * every live row it has from
                                             * VICE's hotkeys.vhk - so the
                                             * question is per-row arithmetic
                                             * against MENU_MAXCH = 24 and not
                                             * a policy. `Control-Reset` is 13
                                             * glyphs, the two-space separator
                                             * c64menu.c uses is 2 and
                                             * `Ctrl+F2` is 7: 22 of 24, and
                                             * it fits. `Open-Apple-Control-
                                             * Reset` is ALREADY 24 and
                                             * `Toggle Fullscreen  Ctrl+F` is
                                             * 25, so those two cannot, which
                                             * section 10.1 records with the
                                             * arithmetic. The chord itself is
                                             * section 6.3's, LIVE since this
                                             * wave (a2kbd.c) */
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
                                             * lost!)`.
                                             * WHY IT IS GREYED, WHICH EVERY
                                             * OTHER ROW HERE SAYS AND THIS
                                             * ONE DID NOT: the body exists
                                             * and works - a2_power_on - but
                                             * the confirmation above does
                                             * not, and the dialog machinery
                                             * lands in wave 4. A row that
                                             * wipes 48KB of the user's RAM
                                             * one menu pick away, shipped
                                             * without the confirmation its
                                             * contract names, is not the item
                                             * section 10.2 describes. So it
                                             * is on a2_have_cmd with the
                                             * rest, and the two reset chords
                                             * - neither of which touches RAM
                                             * - are the recovery a real II+
                                             * user has */
    D "Configure Slots...",                 /* m_machine_menu. THE FACT, WHOLE
                                             * AGAIN NOW THAT BOTH ROUTES
                                             * EXIST (section 10.3):
                                             * No Disk II in this build. Load
                                             * Program reads an Applesoft
                                             * program, and Paste types a
                                             * listing in.
                                             * Wave 1 shortened it to the
                                             * first sentence because the two
                                             * routes it names were themselves
                                             * greyed, and a greying that
                                             * points the reader at something
                                             * they cannot do is the guess
                                             * SPEC.md 47's rule 5 forbids.
                                             * WAVE 4 wrote both, so the
                                             * shortening has stopped being
                                             * true. Disk II is still a
                                             * follow-up PR */
    D "Joystick...",                        /* m_machine_menu, and it is
                                             * `.disabled = 1` in MII's OWN
                                             * table - the authentic grey.
                                             * THE FACT, WHOLE AGAIN NOW THAT
                                             * THE BUTTONS ARE READ (section
                                             * 10.3): The paddles answer
                                             * centre. The game buttons PB0
                                             * and PB1 are F1 and F2 - a
                                             * departure from AppleWin's
                                             * Left-Alt / Right-Alt.
                                             * Wave 1 shortened it to the
                                             * first half because the second
                                             * named a control the build did
                                             * not have, which is the guess
                                             * SPEC.md 47 rule 5 forbids; this
                                             * is the wave that reads F1 and
                                             * F2 as LEVELS through
                                             * os88_key_down (section 6.3), so
                                             * the sentence is a fact again.
                                             * They are GAME BUTTONS and not
                                             * //e Apple keys: a II+ has no
                                             * Open-Apple key at all, it has
                                             * three inputs at $C061-$C063 */
    a2_sep,
    OFF "Toggle Fullscreen",                /* m_video_menu. LIVE, and
                                             * RESIDENT: a WF_FULL window has
                                             * no menu bar, so a chord that
                                             * had to load APPLE2.OVL would
                                             * refuse on a bar the user cannot
                                             * see (section 6.3).
                                             * THE TWO-GLYPH COLUMN IS RULE 5,
                                             * and this row-group is where it
                                             * was ragged: `Color NTSC`,
                                             * `Flashing text` and `Mute` all
                                             * carry the prefix and these two
                                             * did not, so three labels in one
                                             * group started two cells right of
                                             * the two around them - the rule's
                                             * own words for the defect, and
                                             * visible in wave 4's own
                                             * evidence. 19 of MENU_MAXCH 24 */
    D OFF "Color NTSC",                     /* m_video_menu, FOLDING `Color
                                             * NTSC (Alt)`, `Color Mega2`,
                                             * `Green` and `Amber` by rule 3.
                                             *
                                             * **WAVE 5 GAVE IT A BODY**, and
                                             * it is the row that enters the
                                             * FOREIGN VIDEO MODE (section
                                             * 13): on a VGA it is LIVE and
                                             * takes the machine into
                                             * FSXM_VGA13, where lo-res's
                                             * sixteen colours and hi-res's
                                             * artifact colours are what the
                                             * reader sees. The `D` here is
                                             * the launch spelling only;
                                             * a2_menu_state rewrites it from
                                             * os88_fsx_caps, which is a FACT
                                             * about the display this window
                                             * is on and not a guess.
                                             *
                                             * THE FACT WHERE IT IS GREYED,
                                             * WHICH IS CGA AND HERCULES
                                             * (section 10.3):
                                             * The window is monochrome, and
                                             * the foreign modes this screen
                                             * has measured SLOWER than it -
                                             * 695 ms a frame against 632.
                                             * Wave 1 shortened this to the
                                             * first clause because the second
                                             * named a mode the build did not
                                             * have; wave 5 wrote the mode AND
                                             * measured the two 1bpp writers
                                             * out of it (section 13.4), so
                                             * both halves are facts now.
                                             *
                                             * IT IS NOT A CHECK ITEM. MII
                                             * ticks its five tint rows
                                             * because they are a persistent
                                             * MODE; this is a bracket that
                                             * RETURNS, so there is no state
                                             * to tick and a tick would
                                             * describe a screen that is not
                                             * on the glass. The two-glyph
                                             * column stays, because rule 5's
                                             * other half is that every row of
                                             * a marked group owns it */
    ON "Flashing text",                     /* OURS: the flash phase's own
                                             * control (section 7.6). LIVE on
                                             * every tier but one.
                                             * THE FACT, ON THE CPU_8086 TIER
                                             * ONLY (section 10.3): Flashing
                                             * forces a text repaint 3.6 times
                                             * a second. On a 4.77 MHz 8088
                                             * that is 43.1 ms each time and
                                             * the machine would spend it on
                                             * the phase rather than on the
                                             * 6502. The 43.1 is MEASURED -
                                             * a2uitest's own ONE FLASH PHASE
                                             * FLIP row, priced from
                                             * `make a2bandbench`'s 2.434 ms a
                                             * group - and 3.64 flips a second
                                             * makes it 157 ms in every second
                                             * of an 8088. a2_tier_init clears
                                             * a2_fl_ok there, so the row is
                                             * greyed AND unmarked together -
                                             * and it is a2_tier_slow that
                                             * GREYS it, a2_fl_ok that MARKS
                                             * it, which a2_menu_state's own
                                             * note spells out: greying off
                                             * a2_fl_ok alone would grey the
                                             * row for a user who simply
                                             * switched flashing off */
    D OFF "Mute",                           /* m_audio_menu. THE FACT: There
                                             * is no speaker in this build.
                                             * (section 10.3) - and it is
                                             * a2_have_snd that greys it, so
                                             * wave 5 revives it with nothing
                                             * else moving */
    D OFF "Louder"                          /* m_audio_menu, FOLDING `Quieter`
                                             * by rule 3. THE FACT: The tone
                                             * sink has no volume. This
                                             * machine plays a bare square
                                             * wave. (section 10.3)
                                             *
                                             * AND THE FACT IS THE OS's, NOT
                                             * THE APPLE'S. It used to read
                                             * `The Apple's speaker is a
                                             * one-bit toggle. There is no
                                             * volume on it.` - a true
                                             * sentence about the wrong
                                             * machine, and one that implied
                                             * MII's own row was meaningless.
                                             * In MII this row is LIVE:
                                             * ui_gl/mii_mui_menus.c:294-302
                                             * calls mii_audio_volume(&mii->
                                             * speaker.source, volume +/- 1),
                                             * a 0..10 sample multiplier
                                             * (src/mii_audio.c:80-89), and it
                                             * is greyed only at the ends of
                                             * that range (menus.c:157-161).
                                             * Host playback volume has
                                             * nothing to do with the speaker
                                             * being one bit. What is true
                                             * HERE became true in wave 5:
                                             * os88_snd_tone(hz, ticks, prio)
                                             * has no amplitude argument, so
                                             * the sink is a bare PC-speaker
                                             * square-wave gate with nothing
                                             * to turn up.
                                             * ...and the column, for the row
                                             * above's reason. 8 of 24 */
};

/* --- CPU ----------------------------------------------------------------- */
/* MII's m_cpu_menu whole, plus Warp. MII ticks `Normal: 1MHz` when the speed
 * is ~1 and `Fast: 3.5MHz` when it is over 2, and retitles Stop -> Stopped
 * and Running -> Continue from its MUI_MENUBAR_ACTION_PREPARE arm
 * (mii_mui_menus.c:130-205), which is what a2_menu_state does here.
 *
 * AND MII'S TICK ON `Normal: 1MHz` IS A MEASUREMENT, NOT A MODE LABEL:
 * mii_mui_menus.c:165-170 sets the mark only while `mii->speed <= 1.1 &&
 * >= 0.9` and CLEARS it otherwise. This port ticked it unconditionally, which
 * put `* Normal: 1MHz` in the pull-down directly above a status row reading
 * 383 % - the contradiction visible in one screendump. a2_menu_state marks it
 * from a2_pct now, on MII's own band. */
static const char *a2_cpu_items[] = {
    D ON "Normal: 1MHz",                    /* m_cpu_menu. Its MARK is the
                                             * measurement (MII's rule above)
                                             * and its GREYING was a2_have_cmd,
                                             * because the row's body - the
                                             * thing that turns Warp off - is
                                             * WAVE 4'S AND IS WRITTEN
                                             * (a2cmd.c). The `D` and the `ON`
                                             * here are only the launch
                                             * spelling; a2_menu_state rewrites
                                             * both */
    D OFF "Fast: 3.5MHz",                   /* m_cpu_menu. THE FACT: There is
                                             * no 3.5MHz mode. CPU > Warp is
                                             * this port's speed control and
                                             * is beside it. (section 10.3)
                                             *
                                             * THE SECOND SENTENCE IS BACK,
                                             * AND ITS REASON EXPIRED IN WAVE
                                             * 4. Wave 2 struck it because the
                                             * row it pointed at was greyed
                                             * too, so it named a route the
                                             * reader could not take; `Warp`
                                             * is LIVE now, so the fact is a
                                             * fact again. A greying may not
                                             * outlive its reason, and neither
                                             * may the removal of one - the
                                             * same test this wave applied to
                                             * `Configure Slots...` */
    D OFF "Warp",                           /* OURS: the speed control the row
                                             * above points at, and wave 4's.
                                             * Greyed off a2_have_cmd, and
                                             * rewritten by a2_menu_state - a
                                             * row whose `D` is baked into the
                                             * literal and never rewritten is a
                                             * row no later wave can revive */
    a2_sep,
    D OFF "Stop",                           /* m_cpu_menu, retitled `Stopped`.
                                             * THE COLUMN IS IN EVERY SPELLING
                                             * (rule 5): MII keeps `mark` a
                                             * field of its own and only fills
                                             * or empties the glyph, so the
                                             * title never moves */
    D OFF "Running",                        /* m_cpu_menu, retitled `Continue` */
    D OFF "Step",                           /* m_cpu_menu. THE FACT: There is
                                             * no debugger in this port.
                                             * THE TWO-GLYPH COLUMN IS RULE 5
                                             * ONE ROW-GROUP ALONG: it was
                                             * given to Stop and Running and
                                             * not to these two, so the CPU
                                             * pull-down had five labels
                                             * starting at one column and two
                                             * at another - `one label in the
                                             * pull-down starting two cells
                                             * left of the others`, which is
                                             * the rule's own words for the
                                             * defect. MII draws the mark
                                             * INSIDE a margin every item gets
                                             * (mui_menus_draw.c:73-89: `an
                                             * icon shifts the title right, a
                                             * 'mark' doesn't`, and
                                             * `title.l += margin_left` is
                                             * unconditional), so marked and
                                             * unmarked titles start on the
                                             * same x there. os8088 has no
                                             * margin, so the column is spelled
                                             * into the label - and here it is
                                             * free: the longest CPU label is
                                             * 12 glyphs and the cap is 24 */
    D OFF "Next"                            /* m_cpu_menu, same fact, same
                                             * column */
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
 * IT READS FOUR THINGS: `a2_have_cpu` (there is a 6502 - wave 2 set it, and it
 * revives the two reset chords, whose bodies are that wave's own subject),
 * `a2_have_cmd` (the command has a body - wave 4 sets it, and it holds
 * everything else that needs the machine), `a2_have_snd` (wave 5's speaker),
 * and THE MEASURED SPEED, which is what marks `Normal: 1MHz`. Rule 4 forbids
 * a live item that can only refuse, and a "not in this build yet" toast is
 * exactly that. MII's dynamic retitling (Stop -> Stopped, Running ->
 * Continue) is wired here and takes effect the moment there is a machine to
 * be stopped. */
/* a2_running - MII's `mii->state == MII_RUNNING` (mii_mui_menus.c:180-196),
 * and it is TWO facts on this port rather than one: a jammed machine is not
 * running and neither is a stopped one, and both spell the pair Stopped /
 * Continue. It is written once because the retitling arm asks it twice and
 * two copies of a condition are two conditions. */
static int a2_running(void)
{
    return (a2_state == A2_ST_RUN && !a2_pause) ? 1 : 0;
}

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
    a2_mach_items[A2_I_RESET] = a2_have_cpu ? "Control-Reset  Ctrl+F2"
                                            : D "Control-Reset  Ctrl+F2";
    a2_mach_items[A2_I_POWER] = a2_have_cmd ? "Power On" : D "Power On";
    /* ...AND ON THE CPU_8086 TIER THE ROW IS GREYED WITH ITS MEASURED COST
     * (section 10.3). It is `a2_tier_slow` that greys it and `a2_fl_ok` that
     * marks it, which is two different questions about one row: `you cannot
     * have this here` and `it is on`. Greying it while it is MARKED would say
     * the feature is unavailable and leave a tick beside it, so the slow tier
     * clears a2_fl_ok in a2_tier_init and the row is greyed and unmarked
     * together. */
    a2_mach_items[A2_I_FLASH] = a2_tier_slow
        ? D OFF "Flashing text"
        : (a2_fl_ok ? ON "Flashing text" : OFF "Flashing text");
    /* THE THREE THAT USED TO BE GREYED FOR EVER. `D` baked into a literal
     * that a2_menu_state never rewrites is a row no later wave can revive,
     * and a greyed row with no fact is what SPEC.md 47 forbids; both facts
     * are section 10.3's.
     *
     * `Fast: 3.5MHz` AND `Color NTSC` KEEP THEIR BAKED `D` ON PURPOSE, and
     * that is the rule's other half rather than an exception to it: the fact
     * that greys each is a property of the BUILD and of the windowed PATH
     * ("there is no speed control in this build", "the window is
     * monochrome"), not of a flag some later wave sets, so there is nothing
     * for a rewrite to read. `Fast: 3.5MHz` USED TO SAY "this machine runs at
     * 1.02 MHz", which is a claim about the MACHINE that the status row's own
     * measured figure refutes by 3.8x on the host this is developed on: there
     * is no throttle here at all, and a2_slice simply runs a2_budget cycles a
     * wake.
     * What they DID need is the two-glyph column, which rule 5 above has, and
     * they carry it in the literal. Mute is the opposite case and is
     * rewritten below, because a2_have_snd is exactly such a flag. */
    /* `Normal: 1MHz` IS MARKED FROM THE MEASUREMENT, which is MII's own rule
     * (mii_mui_menus.c:165-170: the mark goes on only while the measured speed
     * is between 0.9 and 1.1). There is no throttle in this build, so on the
     * host this is developed on the row is UNMARKED and the status row's
     * figure is why; on the 4.77 MHz XT the port targets it will be unmarked
     * the other way. Ticking it unconditionally asserted a speed the same
     * screen refuted. a2_speed_fold calls this routine when a2_pct moves. */
    a2_cpu_items[A2_I_NORMAL] = (a2_pct >= 90 && a2_pct <= 110)
        ? (a2_have_cmd ? ON "Normal: 1MHz" : D ON "Normal: 1MHz")
        : (a2_have_cmd ? OFF "Normal: 1MHz" : D OFF "Normal: 1MHz");
    /* WARP IS A CHECK ITEM, on rule 5's two spellings and NOT on MENU_DIS:
     * greying the row that is ON would report the feature as unavailable and
     * make it impossible to turn off. It is one of the three radio partners
     * that own the two-glyph column (`Normal: 1MHz`, `Fast: 3.5MHz`, `Warp`),
     * so both spellings carry it and the label does not jump two cells when
     * the state changes. */
    a2_cpu_items[A2_I_WARP] = a2_have_cmd
        ? (a2_warp ? ON "Warp" : OFF "Warp")
        : D OFF "Warp";
    /* MUTE IS A CHECK ITEM ON A CAPABILITY, which is two questions about one
     * row and not one: `a2_have_snd` says the machine HAS a square voice - the
     * fact that greyed the row for four waves - and `a2_mute` says the user
     * has switched it off. Greying the row that is ON would report the
     * feature as unavailable AND make it impossible to un-mute, which is rule
     * 5's own sentence, so the mark and the greying are separate. */
    /* COLOR NTSC IS GREYED ON A DISPLAY WITH NO FOREIGN COLOUR MODE, and the
     * predicate is os88_fsx_caps itself - the same bit os88_fsx_mode would
     * refuse on, so the greying and the refusal are ONE fact (SPEC.md 47).
     * It is asked here and not banked at launch because on a two-display
     * desktop (SPEC.md 39.18.2) it is a question about a DISPLAY and a window
     * moves between them; one far call at 46.7 us, on a routine that runs
     * about once a second. */
    a2_mach_items[A2_I_TINT] = a2_fsx_avail(a2_win)
        ? OFF "Color NTSC" : D OFF "Color NTSC";
    a2_mach_items[A2_I_MUTE] = a2_have_snd
        ? (a2_mute ? ON "Mute" : OFF "Mute")
        : D OFF "Mute";
    /* ...AND SO DO Stop AND Running, WHICH IS RULE 5 AGAIN. The first version
     * gave the marked spelling the two-glyph prefix and the unmarked one
     * nothing, so each label would have jumped two cells left and right as the
     * machine started and stopped - invisible today only because a2_have_cmd
     * is never set and all four rows are greyed. MII does not do this: it
     * keeps `mark` a field of its own and only fills or empties the glyph. */
    /* ...AND A JAMMED MACHINE GREYS BOTH, which is a2_jam's own sentence -
     * "there is no machine left to stop" - acted on rather than merely
     * written. The core never runs again after A2_ST_JAM, so `Continue` was a
     * live item that could only change a flag nothing reads, and its
     * `Running.` overwrote the JAM line that says why the machine is dead
     * (a2cmd.c carries the whole account). THE FACT IS ALREADY ON THE GLASS
     * and is permanent: a2_status draws `6502: JAM at $xxxx` in the message
     * area for as long as the state lasts, which is what SPEC.md 47 asks of a
     * greyed row and is why these two need no sentence of their own. The
     * launch spelling comes back with the `D`, because `Stopped` / `Continue`
     * would describe a machine that could be continued. */
    a2_cpu_items[A2_I_STOP] = (a2_have_cmd && a2_state != A2_ST_JAM)
        ? (a2_running() ? OFF "Stop" : ON "Stopped")
        : D OFF "Stop";
    a2_cpu_items[A2_I_RUN] = (a2_have_cmd && a2_state != A2_ST_JAM)
        ? (a2_running() ? ON "Running" : OFF "Continue")
        : D OFF "Running";
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
    if (menu == A2_M_MACHINE && item == A2_I_TINT) {
        /* Machine > Color NTSC - THE FOREIGN VIDEO MODE (section 13), and it
         * is RESIDENT for a sharper version of Toggle Fullscreen's reason:
         * the bracket's entry proc must be a plain resident function whose
         * address is taken (tools/cc8086.py refuses an ovl_ address by name),
         * and the 53KB shadow claim it takes first can COMPACT the arena - so
         * taking it from inside ovl_a2_cmd would be moving the module whose
         * code is running. It works on a disk with no APPLE2.OVL, which File
         * > Quit and Toggle Fullscreen also do. */
        a2_fsx_enter(win);
        return;
    }
    if (menu == A2_M_MACHINE && item == A2_I_FLASH) {
        /* ...AND THE CPU_8086 TIER'S REFUSAL IS NOT UNDOABLE FROM HERE. The
         * row is greyed there, so the kernel does not dispatch it and this
         * arm is unreachable in the ordinary way - but a2_fl_ok is the byte
         * the tier CLEARED (section 7.8), and a command that toggled it back
         * would turn on a 157 ms/s phase the greying beside it says the
         * machine will not spend. One test, so the two cannot disagree. */
        if (a2_tier_slow)
            return;
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
 * THE MAGNIFICATION IS NOT DECIDED HERE. a2_geom decides it, in one place,
 * off the LIVE content box - 2x on both axes on VGA, 2x horizontal on CGA and
 * Hercules, 1:1 on the CPU_8086 tier - from tests/a2band's measured numbers
 * (sections 7.8, 7.9.2). What this function owes that table is the LATCH:
 * a2_full moves BEFORE the call and rolls back on a refusal, because
 * OSAPI_FULLSCREEN repaints synchronously and a2_geom therefore runs NESTED
 * inside it. Latching after the call measured the new box at 1:1, drew 192
 * lines, and let the next flush draw them again at 2x - half a second of
 * double-draw, invisible in a still dump. */
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
    /* THE LATCH MOVES **BEFORE** THE CALL AND ROLLS BACK ON A REFUSAL, which
     * is apps/c64/c64.c's own order and is a correctness requirement rather
     * than a tidiness one now that there is a magnification to decide.
     * OSAPI_FULLSCREEN repaints the window whole and SYNCHRONOUSLY, so
     * os88_paint - and a2_geom under it - runs NESTED inside the call below:
     * with the latch still down, that repaint measures the new full-screen
     * box at 1:1, draws all 192 lines, and the next flush then draws them
     * again at 2x. Half a second of pure double-draw (PERFORMANCE.md rule 2)
     * on every entry, and invisible in a still screendump because the second
     * picture is the right one. */
    a2_full = !a2_full;
    if (os88_fullscreen(win, a2_full ? 1 : 0) < 0) {
        a2_full = !a2_full;                 /* refused: the latch rolls back */
        a2_say("Another window has it.");
        a2_st_dirty = 1;
        a2_kick = 1;                        /* ...and the REFUSAL owes the
                                             * panel's rows a draw, because
                                             * the kernel did nothing at all */
        os88_wm_wake(win);
    }
}
