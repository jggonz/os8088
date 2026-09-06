/* ============================================================================
 * os8088 - apps/c64/c64menu.c        the five menus, from VICE's own source
 *
 * Part of C64 (C64-SPEC §11). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. EVERY item
 * string below is a `.label` field of src/arch/gtk3/uimachinemenu.c and every
 * hotkey caption a line of data/hotkeys/hotkeys.vhk or one of its !include'd
 * files. Nothing here was typed from memory (LESSONS.md 1: "When you find
 * yourself typing a menu string you did not just read, stop").
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * THE TWO KERNEL LIMITS THAT SHAPE THIS FILE
 * ----------------------------------------------------------------------------
 * A pull-down here is at most MENU_POPMAX = 11 items and each item is
 * truncated to MENU_MAXCH = 24 glyphs (kernel/menu.inc:195, :207). Both are
 * facts about the machine, not preferences, and both are measured on the
 * SMALLEST screen: vid_popmax is (vid_h - 22) / 16, which is 11 on a 200-line
 * CGA and clamped to 11 above it. Three rules follow, and they are stated in
 * C64-SPEC §11.1 rather than left to be rediscovered:
 *
 *  1. A SECTION THAT IS ENTIRELY UNAVAILABLE IS FOLDED INTO ONE ITEM, AND
 *     THAT ITEM IS THE SECTION'S FIRST - its submenu head label where it has
 *     one, otherwise the first item of the section, which is the action the
 *     section exists for. Never a word invented to summarise it, and never
 *     the section's last item: a user looking for tape support must read a
 *     greyed item that names a DATASETTE IMAGE, not a greyed `Datasette
 *     controls`.
 *     A section with a LIVE item in it is not folded at all - it is inlined,
 *     because folding it would take the working item away; that is why
 *     VICE's reset_submenu (:244, head label "Reset" at :521) contributes
 *     three of File's ten slots rather than one.
 *     THE ONE EXCEPTION, and it is taken twice: where the section's first
 *     item does not FIT MENU_MAXCH the fold lands on the first item of the
 *     section that does, because menu_trunc would otherwise print a
 *     SHORTENED label and rule 2 forbids that. `Attach datasette image...`,
 *     `Attach cartridge image...` and `Detach cartridge image(s)` are all 25
 *     glyphs against 24. Each is named where it is taken.
 *  2. THE LABEL IS VICE'S AND IS NEVER SHORTENED. Where the label plus its
 *     `.vhk` caption passes 24 glyphs, the CAPTION is dropped and the label
 *     stands alone; the chord is then in C64-SPEC §11.2's table, which lists
 *     every dropped caption beside the fact that greys its item. (It used to
 *     say §7.5, and §7.5 is a three-row table of the chords the XT/AT BIOS
 *     cannot DELIVER - Alt+F12, Alt+Insert, Alt+Delete - which is a different
 *     question and contains none of them.) Power cycle machine, Load/Save
 *     snapshot image..., Quickload/Quicksave snapshot, Advance frame and
 *     Datasette controls are the FIVE items this costs a caption, which is
 *     the same five C64-SPEC §11.1 rule 2 names.
 *     THE ONE EXCEPTION: where dropping the caption would lose the ONLY
 *     chord a LIVE item has, the SEPARATOR is squeezed from two spaces to one
 *     instead - the label is still VICE's, entire, and it is the gap that
 *     gives. `Reset machine CPU Alt+F9` is 24 glyphs that way and 25 with the
 *     two spaces every other captioned item here uses. It is taken once.
 *  3. AN ITEM THAT IS PRESENT BUT CANNOT WORK WEARS OS88_MENU_DIS AND THE
 *     FACT THAT GREYS IT IS IN A COMMENT BESIDE IT (SPEC.md 47). Nothing is
 *     silently missing and nothing is faked, and NOTHING IS LIVE THAT ONLY
 *     TOASTS A REFUSAL - an item the user can pick and that answers "not yet"
 *     is the one thing 47 exists to stop. VICE's own Debug menu is absent
 *     rather than greyed, because it is `#ifdef DEBUG` in uimachinemenu.c and
 *     this is VICE's rule, not ours.
 *  4. A CHECK ITEM'S STATE IS A `*` IN THE LABEL, and the item pointer is
 *     swapped between the two spellings (c64_menu_state below). EIGHT of
 *     these menus' items are UI_MENU_TYPE_ITEM_CHECK in uimachinemenu.c -
 *     :682 Fullscreen, :692 Show menu/status in fullscreen, :704 Warp mode,
 *     :708 Pause emulation, :732 Show status bar (settings_menu_statusbar_
 *     primary), :757 Mouse grab, :771 Swap joysticks, :786 Allow keyset
 *     joysticks; the count said six and left out :692 and the statusbar one.
 *     In VICE
 *     the checkbox IS how the state is read; this kernel's menu has no check
 *     mark and its face has no glyph for one (LESSONS.md 8). The `*` is
 *     apps/tracker's idiom (tracker.asm:2135-2150) and NOT solitaire's
 *     MENU_DIS twin, for the reason tracker states in that comment: MENU_DIS
 *     is 47's "you cannot have this", so greying the item that is ON would
 *     report the feature as unavailable AND make it impossible to turn off.
 * ==========================================================================*/

/* The menu and item indices os88_oncmd is handed. They are positions in the
 * arrays below and the two must not drift: an item inserted above one of
 * these moves every command under it. */
#define C64_M_FILE  0
#define C64_M_EDIT  1
#define C64_M_SNAP  2
#define C64_M_PREF  3
#define C64_M_HELP  4

#define C64_I_ATTACH   0                    /* File, 10 items */
#define C64_I_RESETCPU 6
#define C64_I_POWER    7
#define C64_I_EXIT     9

#define C64_I_COPY     0                    /* Edit */
#define C64_I_PASTE    1

#define C64_I_FULLSCR  0                    /* Preferences */
#define C64_I_STATBAR  2                    /* ...as VICE orders them */
#define C64_I_WARP     3
#define C64_I_PAUSE    4
#define C64_I_ADVANCE  5
#define C64_I_SWAPJOY  8

#define C64_I_ABOUT    4                    /* Help */

#define OS88_MENU_DIS_STR "\001"            /* OS88_MENU_DIS is 1 (SPEC.md 47) */
#define D OS88_MENU_DIS_STR                 /* the disabled prefix, as text */

/* Rule 4's two spellings of a check item's label. The marker is TWO glyphs so
 * that both spellings are the same width and the label does not shuffle; the
 * longest of them, `* Pause emulation  Alt+P`, is exactly MENU_MAXCH = 24
 * (the MENU_DIS byte is skipped before menu_trunc counts, kernel/menu.inc:
 * 1977-1983, so a greyed check item has its 24 too). */
#define ON  "* "
#define OFF "  "

/* --- File (uimachinemenu.c file_menu_head / _tape / _cart / _printer /
 *     _tail and reset_submenu) ------------------------------------------- */
static const char *c64_file_items[] = {
    "Smart attach...  Alt+A",               /* :279  + hotkeys.vhk <Alt>a */
    D "Attach disk image",                  /* :286. NO CAPTION: it is a
                                             * UI_MENU_TYPE_SUBMENU with a
                                             * .submenu and NO .action, and a
                                             * VICE hotkey binds to an ACTION,
                                             * so VICE prints no chord on it.
                                             * `<Alt>8` is drive-attach-8:0
                                             * (hotkeys-drive.vhk:9), an item
                                             * INSIDE that submenu, and
                                             * captioning the head with it
                                             * tells the reader Alt+8 opens a
                                             * list when in VICE it attaches
                                             * to drive 8.
                                             * No 1541 in this build: a D64
                                             * needs the drive's directory
                                             * walk and the KERNAL serial
                                             * traps. Create, Detach and the
                                             * Flip list fold in below it -
                                             * ONE section between separators
                                             * (:286-:301) is one item by
                                             * rule 1 */
    D "Detach datasette image",              /* :319. No tape emulation in this
                                             * build: T64/TAP are not read.
                                             * RULE 1'S STATED EXCEPTION, and
                                             * it was MEASURED ON THE GLASS:
                                             * the section's first item,
                                             * `Attach datasette image...`
                                             * (:313), is 25 glyphs against
                                             * MENU_MAXCH's 24 and menu_trunc
                                             * printed it `...image..` - a
                                             * shortened label, which rule 2
                                             * forbids. So the fold lands on
                                             * the first item of the section
                                             * that FITS, at 22 glyphs, and it
                                             * still names the medium a reader
                                             * is looking for; Attach, Create
                                             * and the Datasette controls
                                             * submenu (:327) fold into it.
                                             * <Alt>t's caption is dropped by
                                             * rule 2 */
    D "Cartridge freeze  Alt+Z",            /* :402  + hotkeys-cartridge.vhk.
                                             * No cartridge port: the bank
                                             * maps carry the cartridge-less
                                             * seven of VICE's 32.
                                             * RULE 1'S STATED EXCEPTION, the
                                             * same one: `Attach cartridge
                                             * image...` (:393) and `Detach
                                             * cartridge image(s)` (:398) are
                                             * BOTH 25 glyphs against
                                             * MENU_MAXCH's 24, so the fold
                                             * lands on the first item of the
                                             * section that fits - 16 glyphs -
                                             * and the other two fold into
                                             * it */
    D "Printer/plotter",                    /* :486. NO CAPTION, the same
                                             * reason: SUBMENU, no .action.
                                             * `<Alt>4` is printer-formfeed-4
                                             * (hotkeys.vhk:52) and `<Alt>3`
                                             * the userport formfeed (:56) -
                                             * members of that submenu.
                                             * No printer path in this OS */
    D "Activate monitor  Alt+H",            /* :515. No monitor in this build:
                                             * VICE's is 30,000 lines of C */
    "Reset machine CPU Alt+F9",             /* :245  + hotkeys.vhk <Alt>F9 */
    "Power cycle machine",                  /* :249. <Alt>F12's caption does
                                             * not fit 24 glyphs, and an
                                             * 83-key XT has no F12 anyway
                                             * (C64-SPEC §7.5) */
    D "Reset drive #8",                     /* :255. No drives in this build */
    "Exit emulator  Alt+Q"                  /* :527  + hotkeys.vhk <Alt>q */
};

/* --- Edit (edit_menu) ----------------------------------------------------
 * BOTH ITEMS ARE GREYED BY STATE, in c64_menu_state() below, and the two
 * spellings are here for the same reason c64_s_adv's are (rule 4's machinery,
 * rule 3's meaning):
 *
 *  - PASTE greys with no C64.ROM, on a JAM, AND WHILE THE MACHINE IS PAUSED.
 *    c64_paste_feed is called only from the arm of os88_onwake that runs a
 *    slice - C64_ST_RUN, and `!c64_pause || c64_adv` - and c64_wants_wake
 *    answers 0 in every other case, so a paste in any of the three queues
 *    bytes that NOTHING WILL EVER TYPE, which is the one shape SPEC.md 47
 *    exists to stop. The first two last the session; the third self-heals the
 *    moment the user resumes, and is greyed anyway because a command whose
 *    whole visible effect is nothing at all is the same defect either way.
 *  - COPY greys with no C64.ROM ONLY. The clipboard is kernel-owned and
 *    outlives this app (SPEC.md 55.3), and with no machine the matrix holds
 *    c64_ram_pattern's factory fill, so a Copy there would replace whatever
 *    the user had copied elsewhere with 1,025 characters of noise from a
 *    screen they cannot see. On a JAM it stays LIVE: the frozen screen is
 *    real and VICE's Copy always copies what is displayed.
 *
 * THE FACT THAT GREYS EACH, AND THERE ARE NOW THREE OF THEM. Two are the
 * permanent line on the status row - `C64.ROM missing - see README.TXT`
 * (§1.4) or `Main CPU: JAM at $xxxx` (§4.5) - and the third, PAUSED, has no
 * line: `Paused.` is a c64_say and expires after ~5 s. Its permanent facts are
 * §10.2's `P` lamp on the status row and the check beside Preferences > Pause
 * emulation, which stand for as long as the pause does. §11.2's table names
 * all three. This is a deliberate departure from VICE, which queues a paste on
 * a paused machine and delivers it on resume; here nothing would ever drink
 * it, because c64_paste_feed is called only from the running arm of
 * os88_onwake. */
static const char *c64_s_copy[2] = { "Copy  Alt+Delete",
                                     D "Copy  Alt+Delete" };
static const char *c64_s_paste[2] = { "Paste  Alt+Insert",
                                      D "Paste  Alt+Insert" };

/* NOT const: c64_menu_state() writes these entries, exactly as it writes
 * c64_pref_items'. */
static const char *c64_edit_items[] = {
    "Copy  Alt+Delete",                     /* :540  + hotkeys.vhk edit-copy */
    "Paste  Alt+Insert"                     /* :544  + hotkeys.vhk edit-paste.
                                             * LIVE AS OF WAVE 3, and greyed
                                             * before it with the fact that
                                             * greyed them: neither the
                                             * screen-code -> PETSCII -> ASCII
                                             * table nor the $0277 feeder with
                                             * $C6's count was written. All
                                             * three are written now
                                             * (c64cmd.c's ovl_conv_init and
                                             * c64kbd.c's c64_paste_feed), so
                                             * the fact
                                             * stopped being true and SPEC.md
                                             * 47 does not let a greying
                                             * outlive its reason.
                                             * Both captions are still VICE's
                                             * and both MENU ITEMS are still
                                             * the guaranteed route: an AT
                                             * BIOS int 16h AH=0 drops the
                                             * enhanced codes those two chords
                                             * carry (7.5), and where a BIOS
                                             * passes them os88_onkey
                                             * dispatches 0xA3 and 0xA2 to
                                             * these same two items */
};

/* --- Snapshot (snapshot_menu). Every item is greyed by ONE fact: a VSF
 *     carries every chip's state and this machine's chips are not VICE's. -- */
static const char *c64_snap_items[] = {
    D "Load snapshot image...",             /* :556  <Alt>l does not fit */
    D "Save snapshot image...",             /* :560  <Alt>s does not fit */
    D "Quickload snapshot",                 /* :566  <Alt>F10 does not fit */
    D "Quicksave snapshot",                 /* :570  <Alt>F11 does not fit */
    D "Start recording events",             /* :576, AND THE WHOLE EVENT
                                             * SECTION FOLDS INTO IT: :576 to
                                             * :598 is ONE section with no
                                             * separator inside it - Start/Stop
                                             * recording, Start/Stop playing
                                             * back, Set recording milestone,
                                             * Return to milestone - so rule 1
                                             * makes it one item. Keeping
                                             * three of the six carved a
                                             * section VICE does not have */
    D "Save/Record media...",               /* :602 */
    D "Stop media recording",               /* :607 */
    D "Quicksave screenshot"                /* :613  (Pause) */
};

/* --- Preferences (settings_menu_head / _speed / _mouse / _joy_swap /
 *     _non_vsid / _tail; VICE's top-level label is "Preferences") --------- */
/* the six CHECK items' two spellings (rule 4). They are file-scope strings
 * rather than literals inside the array because the array's entries are
 * REPOINTED at run time by c64_menu_state(). */
static const char *c64_s_full[2] = { OFF "Fullscreen  Alt+D",
                                     ON  "Fullscreen  Alt+D" };
static const char *c64_s_warp[2] = { OFF "Warp mode  Alt+W",
                                     ON  "Warp mode  Alt+W" };
static const char *c64_s_paus[2] = { OFF "Pause emulation  Alt+P",
                                     ON  "Pause emulation  Alt+P" };
static const char *c64_s_swap[2] = { OFF "Swap joysticks  Alt+J",
                                     ON  "Swap joysticks  Alt+J" };
/* ...and ONE item that is greyed by STATE rather than checked by it. Advance
 * frame is live while there is a machine to advance and greyed when there is
 * not - a disk with no C64.ROM (C64_ST_HALT) or a JAMMED CPU - because
 * c64_advance_frame answers those states by doing nothing at all, and SPEC.md
 * 47 forbids exactly that shape: a live black item that is a silent no-op is
 * worse than one that says "not yet". The FACT that greys it is already on
 * the glass and permanent: `C64.ROM missing - see README.TXT` (§1.4) or
 * `Main CPU: JAM at $xxxx` (§4.5), on the status row, which is the window's
 * bottom row and is always drawn. */
static const char *c64_s_adv[2] = { "Advance frame",
                                    D "Advance frame" };

/* NOT const: c64_menu_state() writes these entries. The kernel keeps a COPY
 * of the SET (SPEC.md 12.2) but reads the ITEM STRINGS through the `items`
 * pointer at draw time (kernel/menu.inc:1977), which is why swapping a
 * pointer here is enough and os88_menu_set() is not called again -
 * apps/solitaire/solitaire.asm:3435's sol_dealmenu, exactly. */
static const char *c64_pref_items[] = {
    OFF "Fullscreen  Alt+D",                /* :682  + hotkeys.vhk <Alt>d.
                                             * UI_MENU_TYPE_ITEM_CHECK, so
                                             * rule 4's marker.
                                             * A stated exception to SPEC.md
                                             * 11.2.1, the way SPEC.md 74.2
                                             * takes Alt+F: the C64 owns F and
                                             * Esc (C64-SPEC §9.8) */
    D "Restore display state",              /* :687, and :692's
                                             * "Show menu/status in
                                             * fullscreen" folds into it. The
                                             * window is os8088's: the kernel
                                             * places it, and the bar is under
                                             * a fullscreen window */
    D ON "Show status bar",                 /* :732 (settings_menu_statusbar_
                                             * primary), AND IT IS HERE
                                             * BECAUSE THAT IS WHERE VICE PUTS
                                             * IT: ui_machine_menu_bar_create
                                             * (:1153-1160) adds
                                             * settings_menu_head, THEN the
                                             * statusbar section, THEN
                                             * settings_menu_speed - so this
                                             * item sits between "Restore
                                             * display state" and "Warp mode",
                                             * not after "Emulation speed"
                                             * where the first draft had it.
                                             * CHECK, and the same case as
                                             * Allow keyset joysticks below:
                                             * it is ON and it cannot be
                                             * turned off, so it wears rule
                                             * 4's marker ON and rule 3's
                                             * MENU_DIS together. The status
                                             * row is the window's bottom row
                                             * and is always drawn. 17 glyphs
                                             * after the MENU_DIS byte */
    OFF "Warp mode  Alt+W",                 /* :704  + hotkeys.vhk <Alt>w.
                                             * CHECK, and the warp latch also
                                             * has a dot on the status row
                                             * (C64-SPEC §10.2) */
    OFF "Pause emulation  Alt+P",           /* :708  + hotkeys.vhk <Alt>p.
                                             * CHECK, and the second status
                                             * dot. `* Pause emulation  Alt+P`
                                             * is exactly 24 glyphs */
    "Advance frame",                        /* :712. <Alt><Shift>p is 11
                                             * glyphs and does not fit, so the
                                             * caption is dropped by rule 2 and
                                             * the chord is in C64-SPEC §11.1's
                                             * live-items table - os88_onkey
                                             * dispatches it, reading the
                                             * shift level to tell it from
                                             * Alt+P, which the BIOS delivers
                                             * identically. LIVE as of wave 2:
                                             * it was
                                             * greyed because "there is no
                                             * raster accumulator until the
                                             * alarm model lands with the
                                             * core", and the alarm model
                                             * landed - c64_frame_cyc IS that
                                             * accumulator. A greying names a
                                             * FACT (SPEC.md 47) and this one
                                             * stopped being one. */
    D "Emulation speed",                    /* :719. Nothing throttles here:
                                             * the machine delivers what the
                                             * 8088 can and the status bar
                                             * prints it (10.2) */
    D "Mouse grab  Alt+M",                  /* :757  + hotkeys.vhk <Alt>m.
                                             * No 1351 mouse in this build */
    OFF "Swap joysticks  Alt+J",            /* :771  + hotkeys.vhk <Alt>j.
                                             * CHECK, and the swapped state is
                                             * also on the status row's two
                                             * joystick indicators */
    D ON "Allow keyset joysticks",          /* :786. CHECK. The keyset IS the
                                             * joystick here - the only
                                             * joystick source this machine
                                             * has (C64-SPEC §8) - so it wears
                                             * rule 4's marker ON and rule 3's
                                             * MENU_DIS together: it is on, and
                                             * it cannot be turned off. Exactly
                                             * 24 glyphs after the MENU_DIS
                                             * byte */
    D "Settings...  Alt+O"                  /* :800  + hotkeys-settings.vhk,
                                             * and Load/Save/Restore default
                                             * settings fold into it. No
                                             * resources file in this build:
                                             * every setting this port has is
                                             * on this menu */
};

/* --- Help (help_menu) ---------------------------------------------------- */
static const char *c64_help_items[] = {
    D "Browse manual...",                   /* :968. No manual on this floppy */
    D "Command line options...",            /* :973. No command line in this
                                             * OS */
    D "Compile time features...",           /* :978 */
    D "Hotkeys...",                         /* :983. The hotkeys ARE the menu
                                             * captions here */
    "About VICE..."                         /* :988 */
};

/* The set. `oncmd` is LEFT ZERO: os88_menu_set() writes the runtime's command
 * trampoline into it, which is also why the struct may not be const. AM_NAME
 * is `VICE` - the product, not the package's 15-character header name, which
 * is `C64` and is what the Disk window and the .OVL stem use. */
static struct os88_menuset c64_menus = {
    "VICE", 0, 5,
    {
        { "File",        c64_file_items, 10 },
        { "Edit",        c64_edit_items,  2 },
        { "Snapshot",    c64_snap_items,  8 },
        { "Preferences", c64_pref_items, 11 },
        { "Help",        c64_help_items,  5 }
    }
};

/* c64_menu_state - rule 4, in one place. Called from os88_main and after every
 * latch that ovl_cmd flips, so the four check items' labels always say what
 * the machine is doing AFTER the five-second toast has expired. Four stores;
 * no kernel call, because the kernel reads the strings through `items`. */
static void c64_menu_state(void)
{
    c64_pref_items[C64_I_FULLSCR] = c64_s_full[c64_full ? 1 : 0];
    c64_pref_items[C64_I_WARP]    = c64_s_warp[c64_warp ? 1 : 0];
    c64_pref_items[C64_I_PAUSE]   = c64_s_paus[c64_pause ? 1 : 0];
    c64_pref_items[C64_I_SWAPJOY] = c64_s_swap[c64_joyswap ? 1 : 0];
    /* ...and the fifth entry is a GREYING, not a check (§47): with no machine
     * running there is nothing to advance, and the reason is already the
     * permanent line on the status row. c64_jam() and os88_main both call
     * this, which is what makes the two states reachable here. */
    c64_pref_items[C64_I_ADVANCE] =
        c64_s_adv[(c64_state == C64_ST_JAM) ? 1 : 0];
    /* ...and the Edit menu's two, for the reasons written beside their
     * spellings above: Paste is dead in all THREE of its states, Copy only
     * without a machine. Every path that leaves C64_ST_JAM re-runs this
     * (ovl_cmd's reset arm), and c64_jam() enters it through the same call.
     *
     * AND PAUSE IS THE THIRD, WHICH IS THE ONE THE REASONING MISSED. Pause is
     * not a state, it is a FLAG ON C64_ST_RUN - c64.c gates the feeder's arm
     * on `(!c64_pause || c64_adv)` and c64_wants_wake answers 0 while paused
     * - so a Paste taken on a paused machine queues up to 2,048 bytes that
     * nothing types and says nothing, until the user happens to resume. That
     * is the same silent no-op the JAM greying exists to stop; it merely
     * self-heals instead of lasting the session. The pause latch in ovl_cmd
     * already calls this routine, so the item greys and un-greys with the
     * state (SPEC.md 47: a greying must not outlive its fact). */
    c64_edit_items[C64_I_COPY] =
        c64_s_copy[0];                      /* ALWAYS LIVE. It was greyed with
                                             * `no C64.ROM` too, and the ROM
                                             * is a part now (1.4): a greying
                                             * may not outlive its reason
                                             * (SPEC.md 47), so the one fact
                                             * left is none */
    c64_edit_items[C64_I_PASTE] =
        c64_s_paste[(c64_state == C64_ST_JAM || c64_pause)
                    ? 1 : 0];
}
