/* ============================================================================
 * os8088 - apps/infones/nimenu.c      the three FLAT menus (SPEC.md 91.8)
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * EVERY STRING HERE IS A TRANSLATION, AND THE MENU IS AN ADAPTATION
 * ----------------------------------------------------------------------------
 * InfoNES's menu is src/win32/InfoNES_Resource_Win.rc's IDR_MENU, lines
 * 61-105, and the file is SHIFT-JIS: it must be read through
 * `iconv -f CP932` before it says anything at all. THERE IS NO ENGLISH
 * ORIGINAL ANYWHERE IN THE TREE, so every English string below is a
 * TRANSLATION performed by this port, and each carries the .rc line it
 * translates. That is a weaker claim than `apps/cword`'s menus, which were
 * transcribed, and SPEC.md 91.2.1 says so rather than letting a later reader
 * assume otherwise.
 *
 * THE .rc IS THREE POPUPS DEEP AND THIS KERNEL HAS NO SUBMENU. AM_LIST is
 * {AMENU_TITLE, AMENU_ITEMS, AMENU_NITEM} - a flat array of NUL strings, no
 * submenu field and no separator primitive (apps/os88api.inc:1917-1922, and
 * apps/cc/os88.h:236-247 mirrors it in six bytes). So the Options popup's
 * three levels are FLATTENED into one list of six, the Screen > Size
 * submenu is DROPPED with its arithmetic recorded (SPEC.md 91.11), and the
 * File separator is dropped because there is nothing to draw one with.
 *
 * THE KERNEL'S BOUNDS, all met: MENU_APPMAX 5 menus (three here),
 * MENU_POPMAX 11 items a list (six at most here), MENU_MAXCH 24 glyphs an
 * item - and the MENU_DIS byte is skipped before menu_trunc counts
 * (kernel/menu.inc:1977-1983), so a greyed item has its 24 too. The longest
 * string below is `Clip top and bottom` at 19.
 *
 * MENU_DIS MEANS EXACTLY ONE THING IN THIS PACKAGE: UNAVAILABLE. SPEC.md 12.2
 * spends the same dithering on a RADIO MARK as well, where the dithered item
 * is the SELECTED one - and on a 1bpp adapter grey rounds to black, so the
 * two are indistinguishable there (39.4). Every check state in this package
 * is carried in the LABEL instead (ni_skiplab below), which is
 * os88api.inc:1949-1956's own relabelling idiom.
 * ==========================================================================*/

/* The menu and item indices os88_oncmd is handed: positions in the arrays
 * below, and the two must not drift - an item inserted above one of these
 * moves every command under it. */
#define NI_M_FILE  0
#define NI_M_OPT   1
#define NI_M_HELP  2

#define NI_I_OPEN   0               /* File, 4 items */
#define NI_I_RESET  1
#define NI_I_STOP   2
#define NI_I_EXIT   3

#define NI_I_SKIP   0               /* Options, 6 items */
#define NI_I_FASTER 1
#define NI_I_SLOWER 2
#define NI_I_FULL   3
#define NI_I_CLIP   4
#define NI_I_MUTE   5

#define NI_I_ABOUT  0               /* Help, 1 item */

#define D "\001"                    /* OS88_MENU_DIS is 1 (SPEC.md 47) */

/* --- the frame-skip label, which is where its state lives ----------------
 * ONE BUFFER, REWRITTEN, rather than a table of ten spellings. The kernel
 * reads the item through the `items` pointer whenever it draws the pull-down,
 * so pointing that entry at a buffer this file rewrites under the gfx lock is
 * the same relabelling idiom with the strings computed instead of listed -
 * and it is the only way a numeric state fits in 24 glyphs without ten
 * literals. `\001Frame skip: Auto` is 17 of the 24 that are left after the
 * MENU_DIS byte. */
/* NI_SKIP_AUTO, NI_SKIP_MAX and `ni_skip` itself are nirun.c's, beside the
 * pacer that spends them - nirun.c is #included first, and a frame-skip count
 * declared in the menu file and read by the frame loop is a state word owned
 * by whichever file happens to draw it. */
static char ni_skiplab[26];

/* --- File (the .rc's ﾌｧｲﾙ(&F) popup, :63-70) ------------------------------
 * 開く(&O) `Open`, ﾘｾｯﾄ(&R) `Reset`, 停止(&S) `Stop`, SEPARATOR, 終了(&X)
 * `Exit`. `Open ROM` is an ADAPTATION and not a translation - the original is
 * 開く, `Open`, with no ellipsis and no noun - and SPEC.md 91.2.1's
 * flattening list records it beside Stop's, because this kernel's File menu
 * has other Opens in it and a bare `Open` would be the vaguest item on the
 * bar.
 *
 * EVERY `.rc :N` BELOW IS A REAL LINE OF src/win32/InfoNES_Resource_Win.rc
 * AND WAS CHECKED AGAINST IT. The line numbers are the same before and after
 * `iconv -f CP932`; the first draft of this file was off by one on all eight
 * and pointed three of them at a SEPARATOR or an END. */
static const char *ni_file_items[NI_I_EXIT + 1] = {
    "Open ROM",                     /* .rc :65  開く(&O) */
    D "Reset",                      /* .rc :66  ﾘｾｯﾄ(&R) */
    D "Stop",                       /* .rc :67  停止(&S). InfoNES's Stop halts
                                     * the emulation THREAD; there is no
                                     * second task here, so it unloads the ROM
                                     * and returns the panel to No ROM - an
                                     * adaptation, recorded in SPEC.md 91.2.1 */
    "Exit"                          /* .rc :69  終了(&X). Line 68 is
                                     * MENUITEM SEPARATOR, which this kernel
                                     * has no primitive for */
};

/* --- Options (ｵﾌﾟｼｮﾝ(&O), :71-99), FLATTENED ----------------------------
 * ﾌﾚｰﾑｽｷｯﾌﾟ(&F) :73-79 { ｵｰﾄ(&A) CHECKED, 減らす(&D), 増やす(&I) } and
 * 画面(&V) :80-94 { ｻｲｽﾞ(&S) {...}, ｸﾘｯﾌﾟ(&C) { 上端・下端(&V) } } and
 * ｻｳﾝﾄﾞ(&S) :95-98 { ﾐｭｰﾄ(&M) }.
 *
 * 減らす is `decrease` and 増やす is `increase` - OF THE SKIP COUNT, so
 * `increase` is the FASTER direction. The English labels say faster and
 * slower rather than more and fewer for that reason: `Frame skip faster` is
 * the item a user wanting speed picks, and `increase frame skip` is the one
 * they have to stop and think about.
 *
 * THE TWO ARE ALSO IN THE REVERSE OF THE ORIGINAL'S ORDER. The .rc is ｵｰﾄ,
 * 減らす (:77, decrease), 増やす (:78, increase); this list is Auto, faster,
 * slower - so the SPEED direction reads top to bottom, which is the only
 * thing the English labels are about. It is recorded as an adaptation in
 * SPEC.md 91.2.1 with the other five, rather than left for a reader to
 * discover by diffing.
 *
 * `Full screen` is an ADAPTATION with no .rc line at all: InfoNES has no
 * such menu item - its fullscreen is Alt+Enter (SDL:504-506) - and this
 * kernel has no chord surface, so the bracket needs an item. SPEC.md 91.2.1
 * records it as an adaptation rather than presenting it as a traced string. */
static const char *ni_opt_items[NI_I_MUTE + 1] = {
    ni_skiplab,                     /* .rc :73-79  ﾌﾚｰﾑｽｷｯﾌﾟ + ｵｰﾄ(:75),
                                     * folded into ONE live item that cycles */
    D "Frame skip faster",          /* .rc :78  増やす(&I) */
    D "Frame skip slower",          /* .rc :77  減らす(&D) */
    D "Full screen",                /* an adaptation - see above */
    D "Clip top and bottom",        /* .rc :91  上端・下端(&V) under ｸﾘｯﾌﾟ */
    D "Mute (no APU)"               /* .rc :97  ﾐｭｰﾄ(&M). GREYED FOREVER, and
                                     * the short form is in the label because
                                     * 24 glyphs is the item width; the
                                     * SENTENCE is on the panel's fact line,
                                     * which is where a MENU_DIS item's reason
                                     * has to live (SPEC.md 91.7.2) */
};

/* --- Help (ﾍﾙﾌﾟ(&H), :100-104) ------------------------------------------
 * ROM情報(&I) :102 `ROM information` is DROPPED rather than greyed: its seven
 * rows are permanently on the panel, so the item would open a dialog showing
 * what is already on the glass. ﾊﾞｰｼｮﾝ情報(&A) :103 `Version information` is
 * folded into About InfoNES, which is where SPEC.md 12.2 puts it and what
 * os88_about_set() names. Both are recorded in SPEC.md 91.11.
 *
 * THE .rc SPELLS IT ﾊﾞｰｼｮﾝ情報, WITHOUT THE DAKUTEN ON THE ｼ - `ba-shon`,
 * not `ba-jon`. It is the original's own typo and it is quoted here as the
 * file spells it, because a citation a reader cannot grep for is not a
 * citation. */
static const char *ni_help_items[NI_I_ABOUT + 1] = {
    "About InfoNES"                 /* .rc :103  ﾊﾞｰｼｮﾝ情報(&A) */
};

/* The set. `oncmd` is LEFT ZERO: os88_menu_set() writes the runtime's command
 * trampoline into it, which is also why the struct may not be const (os88.h).
 * AM_NAME is `InfoNES` - the product as InfoNES spells it - and not the
 * package's 15-character header name, which is `INFONES` and is what the Disk
 * window and the .OVL stem use. */
static struct os88_menuset ni_menus = {
    "InfoNES", 0, 3,
    {
        { "File",    ni_file_items,  NI_I_EXIT + 1 },
        { "Options", ni_opt_items,   NI_I_MUTE + 1 },
        { "Help",    ni_help_items,  NI_I_ABOUT + 1 },
        { 0, 0, 0 },
        { 0, 0, 0 }
    }
};

/* --- the greying predicates ----------------------------------------------
 * ONE PREDICATE PER ITEM, and the same predicate feeds the greying, the
 * panel's fact line and (from wave 2) the bracket's keyboard refusal. Two
 * copies of a predicate are two answers waiting to disagree.
 *
 * A greying names a FACT and may not outlive it (SPEC.md 47): every row below
 * that says `wave 2` is deleted by the wave that makes it false, and the
 * fact-line sentence goes with it. */

/* ni_have_rom - is there a machine to reset or stop? */
static int ni_have_rom(void)
{
    return (ni_state == NI_ST_READY || ni_state == NI_ST_RUN);
}

/* apps/os88api.inc's `FSXM_VGA13 equ 6`, spelled once here rather than as a
 * bare 6 inside the shift below - and build.sh's `nistruct` row reads BOTH and
 * refuses a build where they disagree. A magic index tied to a symbol by a
 * comment is the same cross-language hole as the composer's field table. */
#define NI_FSXM_VGA13 6

/* ni_have_fsx - does THIS WINDOW'S DISPLAY offer 320x200x256 (mode 13h)?
 * Re-asked at use and NEVER banked: os88api.inc:2438-2449 says an answer
 * banked in an entry proc describes the window you were LAUNCHED FROM,
 * because yours does not exist yet - and on a two-card desktop the answer
 * changes when the window is dragged to the other monitor. */
static int ni_have_fsx(void *win)
{
    /* FSXM_VGA13 (id 6) BY NAME, and not "any mode at all". Mode 13h is the
     * only mode this build's bracket enters (SPEC.md 91.6.1) - the CGA, Mode
     * X and Hercules presents are wave 3's - so a Hercules machine, whose
     * caps mask is non-zero and does not contain bit 6, must grey the item
     * rather than enter a bracket whose present cannot draw. */
    return (win != 0 && (ni_fsx_caps(win) & (1 << NI_FSXM_VGA13)) != 0);
}

/* ni_menu_labels - every label and every greying, in one place, called from
 * os88_main and after anything that can change a predicate. Ten stores and one
 * kernel call (the caps read): the kernel reads the strings through `items`.
 *
 * IT IS SPLIT FROM THE FACT ROTATION so that a caller can refresh a LABEL
 * without advancing the fact line - which the bracket's exit needs: PgUp/PgDn
 * inside the bracket change `ni_skip`, so the Options menu's own label is
 * stale until this runs, but the fact row was already settled inside the
 * bracket (ni_panel_settle) and re-rotating it there would letter that row a
 * second time with a different sentence (SPEC.md 91.6.5). */
static void ni_menu_labels(void)
{
    int rom, runnable;

    rom = ni_have_rom();

    /* THE FRAME-SKIP TRIO IS GREYED ON `Full screen`'S OWN PREDICATE, and not
     * on `rom` alone (SPEC.md 91.10). `Full screen` is the only route into the
     * bracket - a windowed picture is dropped (SPEC.md 91.11) and there is no
     * Full-screen shortcut key - so on CGA, EGA and Hercules, where
     * ni_have_fsx already dithers it, a live `Frame skip faster` changes a
     * number nothing on that machine can ever spend: present, not greyed, not
     * checked and silently ignored, which is exactly what SPEC.md 47 forbids.
     * There must be a MACHINE to run AND a DISPLAY that can run it, and the
     * rotation's NI_FACT_FSX sentence names the second half. */
    runnable = rom && ni_have_fsx(ni_win);

    ni_file_items[NI_I_RESET] = rom ? "Reset" : D "Reset";
    ni_file_items[NI_I_STOP]  = rom ? "Stop"  : D "Stop";

    /* The frame-skip label, and the whole of its state. THE CHECK STATE IS IN
     * THE LABEL and never a MENU_DIS dither, because in this package that
     * byte means UNAVAILABLE and nothing else (the file header's rule).
     * `\001Frame skip: Auto` is 17 of the 24 that are left after the byte. */
    os88_strcpy(ni_skiplab, runnable ? "Frame skip: " : D "Frame skip: ",
                sizeof(ni_skiplab));
    if (ni_skip == NI_SKIP_AUTO)
        ni_app(ni_skiplab, "Auto");
    else
        ni_appnum(ni_skiplab, (unsigned)ni_skip);

    /* THE FOUR LIVE PREDICATES (SPEC.md 91.10), and wave 1's single constant
     * fact is gone with them. Frame skip and Full screen share ONE predicate
     * - a ROM **and** a display that offers mode 13h at all, which is
     * `ni_have_fsx`'s question and is asked of THIS WINDOW'S display every
     * time; Clip is greyed FOREVER in a 200-row mode, which is every
     * mode this build enters (SPEC.md 91.6.1), and Mute is greyed forever
     * full stop. */
    ni_opt_items[NI_I_FASTER] = runnable ? "Frame skip faster"
                                         : D "Frame skip faster";
    ni_opt_items[NI_I_SLOWER] = runnable ? "Frame skip slower"
                                         : D "Frame skip slower";
    ni_opt_items[NI_I_FULL]   = runnable ? "Full screen" : D "Full screen";
    ni_opt_items[NI_I_CLIP]   = D "Clip top and bottom";
    ni_opt_items[NI_I_MUTE]   = D "Mute (no APU)";

}

/* ni_menu_rotate - the one fact row, advanced. */
static void ni_menu_rotate(void)
{
    int rom;

    rom = ni_have_rom();

    /* THE ONE FACT ROW, ADVANCED (SPEC.md 91.7.2). More than one fact is true
     * at once in this build and there is one row for them, so the row rotates
     * on a user action rather than standing on one sentence and hiding the
     * rest - which is how `Mute (no APU)`'s own sentence was reachable only by
     * pressing a key named nowhere on the machine.
     *
     * BOTH PREDICATES ARE THE ITEMS' OWN. `!rom` is what the frame-skip trio
     * and Full screen are dithered by, and until NI_FACT_NOROM existed those
     * four greyings had no sentence anywhere: an empty panel showed four
     * dithered items over a fact line about the clip. */
    ni_fact_rotate(!ni_have_fsx(ni_win), !rom);
    ni_panel_fact();
}

/* ni_menu_state - the labels AND the rotation, which is what every caller but
 * the bracket's exit wants. */
static void ni_menu_state(void)
{
    ni_menu_labels();
    ni_menu_rotate();
}
