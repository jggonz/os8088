/* ============================================================================
 * os8088 - apps/c64/c64menu.c        the four menus, from VICE's own source
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
 *  1. AN ITEM THIS BUILD CAN NEVER ENABLE IS ABSENT, NOT GREYED (the
 *     owner's rule, C64-SPEC 11.1): a greyed item nothing can ungrey is a
 *     slot spent on telling the user about a feature they cannot have.
 *     VICE's tape, cartridge, printer, monitor and drive-reset items, the
 *     whole Snapshot menu, and the Preferences and Help items about a
 *     window, mouse, resources file and manual this OS does not have are
 *     gone; C64-SPEC 11.2 lists each with the fact that removed it.
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
 *  3. AN ITEM GREYED BY STATE WEARS OS88_MENU_DIS AND THE FACT THAT
 *     GREYS IT IS IN A COMMENT BESIDE IT (SPEC.md 47), and NOTHING IS LIVE
 *     THAT ONLY TOASTS A REFUSAL. Greyed means "not now": Paste on a
 *     paused machine, Advance frame on a jammed one, the Recompiler with
 *     no memory for it.
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

/* (The menu and item indices are c64.c's, ahead of every include: the
 * colour bracket in c64scr.c dispatches three of them, C64-SPEC 9.12.) */

#define OS88_MENU_DIS_STR "\001"            /* OS88_MENU_DIS is 1 (SPEC.md 47) */
#define D OS88_MENU_DIS_STR                 /* the disabled prefix, as text */

/* Rule 4's two spellings of a check item's label. The marker is TWO glyphs so
 * that both spellings are the same width and the label does not shuffle; the
 * longest of them, `* Pause emulation  Alt+P`, is exactly MENU_MAXCH = 24
 * (the MENU_DIS byte is skipped before menu_trunc counts, kernel/menu.inc:
 * 1977-1983, so a greyed check item has its 24 too). */
#define ON  "* "
#define OFF "  "
/* ...AND EVERY OTHER ITEM ON A MENU THAT HAS CHECK ITEMS STARTS WITH OFF'S
 * TWO BLANKS TOO, so the labels line up in one column: Preferences' plain
 * items (Restore display state, Advance frame, Emulation speed, Mouse grab,
 * Settings...) once started two cells left of their neighbours. */

/* --- File (uimachinemenu.c file_menu_head / _tape / _cart / _printer /
 *     _tail and reset_submenu) ------------------------------------------- */
static const char *c64_file_items[] = {
    "Smart attach...  Alt+A",               /* :279  + hotkeys.vhk <Alt>a */
    "Attach disk image...",                 /* :286, a submenu in VICE whose
                                             * Alt+8 item attaches to drive 8 -
                                             * here the one thing it does: a
                                             * D64 onto drive 8, through the
                                             * same Standard File dialog as
                                             * Smart attach (os88_onfile tells
                                             * a D64 by its size). Drive 8 is
                                             * not a 1541 but the KERNAL's
                                             * LOAD served from the image
                                             * (C64-SPEC 11.5) */
    "Reset machine CPU Alt+F9",             /* :245  + hotkeys.vhk <Alt>F9 */
    "Power cycle machine",                  /* :249. <Alt>F12's caption does
                                             * not fit 24 glyphs, and an
                                             * 83-key XT has no F12 anyway
                                             * (C64-SPEC §7.5) */
    "Exit emulator  Alt+Q"                  /* :527  + hotkeys.vhk <Alt>q */
};

/* --- Edit (edit_menu) ----------------------------------------------------
 * BOTH ITEMS ARE GREYED BY STATE, in c64_menu_state() below, and the two
 * spellings are here for the same reason c64_t_adv's are (rule 4's machinery,
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
/* ONE STRING EACH, and the greyed spelling is the WHOLE of it while the
 * live one starts a byte in: the MENU_DIS prefix is one byte, so `+ 1` skips
 * it. It was two literals and a third copy in the item table - SmallerC
 * merges no string - for two spellings that differ by their first byte
 * (apps size pass 1). Copy has no greyed spelling at all any more: it is
 * always live (c64_menu_state says why), and its item is the one literal
 * below. */
static char c64_t_paste[] = D "Paste  Alt+Insert";

/* NOT const: c64_menu_state() writes these entries, exactly as it writes
 * c64_pref_items'. */
static const char *c64_edit_items[] = {
    "Copy  Alt+Delete",                     /* :540  + hotkeys.vhk edit-copy */
    c64_t_paste + 1                         /* :544  + hotkeys.vhk edit-paste.
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


/* --- Preferences (settings_menu_head / _speed / _mouse / _joy_swap /
 *     _non_vsid / _tail; VICE's top-level label is "Preferences") --------- */
/* the six CHECK items' two spellings (rule 4). They are file-scope strings
 * rather than literals inside the array because the array's entries are
 * REPOINTED at run time by c64_menu_state(). */
/* ...AND THEY ARE ONE STRING EACH NOW, whose first byte c64_menu_state
 * writes: ON and OFF differ in that byte alone (`* ` against two spaces),
 * and the kernel reads the text through the pointer at draw time, so
 * rewriting the byte in place is the same event as repointing the entry -
 * the pointer below simply never moves. It was two literals per item plus a
 * third in the table, three copies of the same 20-odd bytes (apps size
 * pass 1). */
static char c64_t_full[] = OFF "Fullscreen  Alt+D";
static char c64_t_warp[] = OFF "Warp mode  Alt+W";
static char c64_t_paus[] = OFF "Pause emulation  Alt+P";
static char c64_t_swap[] = OFF "Swap joysticks  Alt+J";
static char c64_t_fs0[] = OFF "Draw every frame";
static char c64_t_fs1[] = OFF "Skip frames as needed";
static char c64_t_fs2[] = OFF "Skip most frames";
static const char c64_mark[2] = { ' ', '*' };   /* OFF[0], ON[0] */
/* ...and ONE item that is greyed by STATE rather than checked by it. Advance
 * frame is live while there is a machine to advance and greyed when there is
 * not - a JAMMED CPU - because ovl_advance_frame answers that state by doing
 * nothing at all, and SPEC.md 47 forbids exactly that shape: a live black
 * item that is a silent no-op is worse than one that says "not yet". The FACT
 * that greys it is already on the glass and permanent: `Main CPU: JAM at
 * $xxxx` (§4.5), on the status row, which is the window's bottom row and is
 * always drawn. (A disk with no C64.ROM used to be the other state; the ROM
 * is a part of the package now, §1.4.) */
static char c64_t_adv[] = D OFF "Advance frame";   /* live at + 1 (Paste's) */
/* ...and THE RECOMPILER (C64-SPEC 4.9.5): a CHECK while there is one, and
 * greyed when there is none - no 48KB the heap could give it, a D64 that
 * took its memory (which says so on the status row as it happens), or
 * kern_mega - live at + 1, its mark the byte after the MENU_DIS one */
static char c64_t_jit[] = D OFF "Recompiler";

/* NOT const: c64_menu_state() writes these entries. The kernel keeps a COPY
 * of the SET (SPEC.md 12.2) but reads the ITEM STRINGS through the `items`
 * pointer at draw time (kernel/menu.inc:1977), which is why swapping a
 * pointer here is enough and os88_menu_set() is not called again -
 * apps/solitaire/solitaire.asm:3435's sol_dealmenu, exactly. */
static const char *c64_pref_items[] = {
    c64_t_full,                             /* :682  + hotkeys.vhk <Alt>d.
                                             * UI_MENU_TYPE_ITEM_CHECK, so
                                             * rule 4's marker.
                                             * A stated exception to SPEC.md
                                             * 11.2.1, the way SPEC.md 74.2
                                             * takes Alt+F: the C64 owns F and
                                             * Esc (C64-SPEC §9.8) */
    c64_t_warp,                             /* :704  + hotkeys.vhk <Alt>w.
                                             * CHECK, and the warp latch also
                                             * has a lamp on the status row
                                             * (C64-SPEC §10.2) */
    c64_t_paus,                             /* :708  + hotkeys.vhk <Alt>p.
                                             * CHECK, and the second lamp.
                                             * `* Pause emulation  Alt+P` is
                                             * exactly 24 glyphs */
    c64_t_adv + 1,                          /* :712. <Alt><Shift>p is 11
                                             * glyphs and does not fit, so the
                                             * caption is dropped by rule 2 and
                                             * the chord is in C64-SPEC §11.1's
                                             * live-items table - os88_onkey
                                             * dispatches it, reading the
                                             * shift level to tell it from
                                             * Alt+P, which the BIOS delivers
                                             * identically. Greyed on a JAM */
    c64_t_jit,                              /* :719's place - VICE's
                                             * "Emulation speed" - the
                                             * RECOMPILER (C64-SPEC 4.9.5),
                                             * the speed setting this port
                                             * has */
    c64_t_swap,                             /* :771  + hotkeys.vhk <Alt>j.
                                             * CHECK, and the swapped state is
                                             * also on the status row's two
                                             * joystick indicators */
    c64_t_fs0,                              /* THE FRAME SKIP, three RADIO */
    c64_t_fs1,                              /* items (C64-SPEC 9.13): VICE's */
    c64_t_fs2                               /* is a Settings refresh rate, and
                                             * here a music player and a game
                                             * want opposite answers, so the
                                             * user picks: every frame the
                                             * host can draw, the adaptive
                                             * skip, or the fewest */
};

/* --- Help (help_menu) ---------------------------------------------------- */
static const char *c64_help_items[] = {
    "About VICE..."                         /* :988. Browse manual, Command
                                             * line options, Compile time
                                             * features and Hotkeys are absent
                                             * (rule 1): no manual on the
                                             * floppy, no command line, and
                                             * the hotkeys are the captions */
};

/* The set. `oncmd` is LEFT ZERO: os88_menu_set() writes the runtime's command
 * trampoline into it, which is also why the struct may not be const. AM_NAME
 * is `VICE` - the product, not the package's 15-character header name, which
 * is `C64` and is what the Disk window and the .OVL stem use. */
static struct os88_menuset c64_menus = {
    "VICE", 0, 4,
    {
        { "File",        c64_file_items,  5 },
        { "Edit",        c64_edit_items,  2 },
        { "Preferences", c64_pref_items,  9 },
        { "Help",        c64_help_items,  1 }
    }
};

/* c64_menu_state - rule 4, in one place. Called from os88_main and after every
 * latch that ovl_cmd flips, so the four check items' labels always say what
 * the machine is doing AFTER the five-second toast has expired. Four stores;
 * no kernel call, because the kernel reads the strings through `items`. */
static void c64_menu_state(void)
{
    /* every latch is 0 or 1, so it indexes the mark directly */
    c64_t_full[0] = c64_mark[c64_full];
    c64_t_warp[0] = c64_mark[c64_warp];
    c64_t_paus[0] = c64_mark[c64_pause];
    c64_t_swap[0] = c64_mark[c64_joyswap];
    c64_t_fs0[0] = c64_mark[c64_fs_mode == 0];
    c64_t_fs1[0] = c64_mark[c64_fs_mode == 1];
    c64_t_fs2[0] = c64_mark[c64_fs_mode == 2];
    /* ...and the fifth entry is a GREYING, not a check (§47): with no machine
     * running there is nothing to advance, and the reason is already the
     * permanent line on the status row. c64_jam() and os88_main both call
     * this, which is what makes the two states reachable here. */
    c64_t_jit[1] = c64_mark[c64_jit_on];
    c64_pref_items[C64_I_JIT] = c64_fjit ? c64_t_jit + 1 : c64_t_jit;
    c64_pref_items[C64_I_ADVANCE] = c64_t_adv;      /* greyed... */
    if (c64_state != C64_ST_JAM)
        c64_pref_items[C64_I_ADVANCE] = c64_t_adv + 1;  /* ...or live */
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
    /* COPY IS ALWAYS LIVE, so nothing here writes its entry. It was greyed
     * with `no C64.ROM` too, and the ROM is a part now (1.4): a greying may
     * not outlive its reason (SPEC.md 47), so the one fact left is none. */
    c64_edit_items[C64_I_PASTE] = c64_t_paste;      /* greyed... */
    if (c64_state != C64_ST_JAM)
        if (!c64_pause)
            c64_edit_items[C64_I_PASTE] = c64_t_paste + 1;  /* ...or live */
}
