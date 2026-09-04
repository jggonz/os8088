/* ============================================================================
 * os8088 - apps/loom/lmovl.c
 *
 * LOOM.OVL's remaining tenants (SPEC.md 73.14, WEAVE-SPEC 1.2). #included by
 * apps/loom/loom.c LAST, so that everything it reaches is already declared.
 *
 * WEAVE-SPEC 1.2 names LOOM.OVL's contents as "the WML compiler, the WJS
 * compiler, the FX pre-compiler, the atom interner, the bundle writer" - and
 * those are five whole files. What is left over is here: the two commands
 * that are neither a keystroke's path nor a compiler, and both of them meet
 * SPEC.md 73.14's test exactly ("a keystroke's path stays in, a menu
 * command's can go out, because a menu command may refuse and a keystroke may
 * not").
 *
 * ---------------------------------------------------------------------------
 * WHAT IS **NOT** IN HERE, AND WHY EACH ONE STAYED RESIDENT
 * ---------------------------------------------------------------------------
 *   lm_save / lm_saveall     Save is reachable from the menu, from `^S` AND
 *                            FROM THE CLOSE GUARD'S ALERT. A Save that could
 *                            not happen because the module had gone missing
 *                            would lose the user's work at the exact moment
 *                            the program had just promised not to. The split
 *                            is by frequency and this one's frequency is
 *                            "once, at the worst possible moment".
 *   lm_prefs_read/write      Two file calls and eight bytes. The overlay's
 *                            own load is a file call, so moving these would
 *                            trade one read for two.
 *   the editor, the sidebar  A keystroke's path. 73.14's own words.
 *
 * ---------------------------------------------------------------------------
 * A REFUSED OVERLAY RETURNS 0 (apps/cc/crt0.asm), so every tenant here
 * answers "IT RAN" separately from what it decided - WEAVE-SPEC 1.2's rule,
 * and the reason each of these returns an int where `void` would read more
 * naturally. A missing LOOM.OVL must produce a sentence and not silence.
 * ==========================================================================*/

/* ============================================================================
 * ABOUT (SPEC.md 12.2, 20.4)
 *
 * A TOAST plus THE STATUS ROW, and the pairing is what makes it readable at
 * all. TOAST_MAX is 23 characters (SPEC.md 59.8) and the toast takes itself
 * down after a couple of seconds, so a toast alone can carry a name and
 * nothing else. THREE toasts do not fix that - they REPLACE one another, and
 * the first version of this function fired three and put only the last on the
 * glass, which is what the run showed. So the name goes to the toast, the
 * sentence goes to the status row that KEEPS it, and os88_about()'s repaint
 * puts it there. It is the same shape lm_say() has, said once by hand because
 * the two halves want different words.
 * ==========================================================================*/
/* THE CREDIT GOES IN THE STATUS ROW, and the reason is the segment ceiling
 * rather than taste. WEAVE got SPEC.md 20.5.1.1's standard card; LOOM has
 * 242 bytes of headroom against os88pkg's 0xF000 budget (image 54,982 + bss
 * 6,216 = 61,198 of 61,440) and the card is 546, so it does not fit and
 * finding 304 bytes elsewhere is a size pass, not an attribution. What DOES
 * fit is the name in the sentence this already writes - and the status row
 * KEEPS it (WEAVE-SPEC 10.1) after the toast has retired itself, which is
 * more than a card that a click takes away.
 *
 * lm_status is LM_MSG = 88, so the sentence has 87 characters. The old one
 * used all 87; this one uses 81 and drops '(WEAVE-SPEC 1.2)' to make room -
 * a section number is in the document, and the author's name was nowhere. */
static int ovl_about(void)
{
    os88_toast("LOOM by Jorge Gonzalez", 0);    /* TOAST_MAX is 23 */
    lm_quiet("LOOM - the Weave IDE, by Jorge Gonzalez. "
             "^S saves, ^P packs, ^R reloads in Weave.");
    return 1;
}

/* ============================================================================
 * NEW PROJECT (WEAVE-SPEC 11.2)
 *
 * File > New Project... opens a SAVE dialog, which is the one kernel dialog
 * that names a file that need not exist yet - and "make me a project here" is
 * exactly that. The completion writes a MAIN.WML skeleton at the name the
 * user chose and then opens it through the ordinary path, so there is one
 * project loader and not two (apps/weave/wload.c's "two ways in, one
 * decision", said about creating instead of opening).
 *
 * THE SKELETON IS THE SMALLEST THING WEAVE-SPEC 3 CALLS A PROJECT: an <app>
 * with a name, one <card>, one <label>. It packs, it runs, and it is
 * something to type into rather than an empty file - which on a machine with
 * no other documentation of the language is most of what a template is for.
 * The three committed demo projects (apps/weave/demos/, and this program's
 * floppy carries them) are the worked examples.
 *
 * IF THE NAME ALREADY EXISTS IT IS NOT OVERWRITTEN. The Standard File dialog
 * has already asked about replacing (SPEC.md 38), but a Weave PROJECT is a
 * folder of files rather than one file, and replacing a MAIN.WML while its
 * MAIN.WJS lived on would leave a script bound to elements that no longer
 * exist. So an existing name is opened rather than replaced, and the status
 * row says which of the two happened.
 * ==========================================================================*/
static const char lm_skel[] =
    "<app name=\"New App\" vm=\"16\">\n"
    "  <card>\n"
    "    <label>Hello from Weave.</label>\n"
    "  </card>\n"
    "</app>\n";

static int ovl_newproj(const char *name)
{
    unsigned n;

    /* The name has to be a `.WML`: a project's root always is (WEAVE-SPEC
     * 11.2), and the dialog will happily hand back anything. Rather than
     * refuse a name the user typed, the extension is REPLACED - which is what
     * they meant - and the name that was actually used is in the status row
     * and in lm_argname, which is what the caller opens. */
    ovl_stemof(name);
    if (lm_stem[0] == 0)
        os88_strcpy(lm_stem, "MAIN", sizeof(lm_stem));
    lm_mkname(lm_stem, "WML");
    os88_strcpy(lm_argname, lm_line, sizeof(lm_argname));

    if (ovl_exists(lm_argname)) {
        lm_l0();
        lm_ls(lm_argname);
        lm_ls(" is already here - opening it.");
        lm_say(lm_line);
        return 1;
    }
    n = os88_strlen(lm_skel);
    if (os88_file_write(lm_argname, lm_skel, n) != 0) {
        lm_l0();
        lm_ls(lm_argname);
        if (os88_ferr() == OS88_FERR_WPROT)
            lm_ls(": the disk is write-protected.");
        else if (os88_ferr() == OS88_FERR_FULL)
            lm_ls(": the disk is full.");
        else
            lm_ls(": the disk refused the write.");
        lm_say(lm_line);
        return 1;
    }
    lm_l0();
    lm_ls("Created ");
    lm_ls(lm_argname);
    lm_ls(".");
    lm_say(lm_line);
    return 1;
}
