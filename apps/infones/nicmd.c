/* ============================================================================
 * os8088 - apps/infones/nicmd.c      ovl_*: the menu command bodies
 *
 * Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
 * Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
 * InfoNES fe3295c0, restructured for 8086 real mode.
 *
 * #included into apps/infones/infones.c - ONE translation unit (73.1).
 *
 * ----------------------------------------------------------------------------
 * ovl_*: IN INFONES.OVL FROM THE FIRST COMMIT, AND SPLIT BY FREQUENCY
 * ----------------------------------------------------------------------------
 * A menu command runs ONCE PER PICK. Nothing a keystroke or a frame touches is
 * here, which is SPEC.md 73.14's test - and its counterpart: `Full screen` is
 * answered in the RESIDENT half, because it is the one command that must work
 * on a disk whose .OVL is missing (SPEC.md 91.6.5, apps/c64's reason).
 *
 * NO REFUSAL TOASTS FOR GREYED ITEMS. The kernel never lets a MENU_DIS item be
 * highlighted or selected (os88api.inc:1945-1972), so there is no click to
 * answer and a handler written for one is unreachable code. What a greyed item
 * says is on the panel's FACT LINE (SPEC.md 91.7.2), and it is put there by
 * ni_menu_state, not from here.
 *
 * WHICH IS WHY THIS FILE HAS THREE BODIES AND NOT NINE. In this build every
 * Options item is greyed - there is no picture until the composer lands
 * (nipanel.c's NI_FACT_PIC) - so the frame-skip, clip and fullscreen bodies
 * would be code nothing can reach. They arrive in the wave that un-greys their
 * items, with the greying and the fact-line sentence deleted in the same edit.
 * ==========================================================================*/

/* ovl_probe - SPEC.md 91.13.1's FIRST `ovl_*` CALL, made from the first wake.
 *
 * It does nothing and that is the whole of it: what is wanted is the SIDE
 * EFFECT of reaching a function that lives in the module, which is the
 * runtime resolving INFONES.OVL - one OSAPI_MEM_CLAIM and one
 * OSAPI_FILE_READ. Doing it here means it happens on the UI task with NO GFX
 * LOCK HELD, once, instead of under whichever locked callback happened to be
 * the first menu pick. It answers 1 because it ran at all; the runtime
 * answers 0 for it when the module could not be loaded (SPEC.md 73.14), and
 * that is the only 0 this can produce. */
static int ovl_probe(void)
{
    return 1;
}

/* ovl_cmd - a menu pick's body.
 *
 * Answers 1 when it did something the caller should repaint after, 0 when it
 * did not - AND 0 IS ALSO WHAT THE RUNTIME ANSWERS WHEN THE MODULE COULD NOT
 * BE LOADED, having already toasted the reason (SPEC.md 73.14). The caller is
 * written so that 0 is a normal path (LESSONS.md 5). */
static int ovl_cmd(int item, int menu, void *win)
{
    if (menu == NI_M_FILE) {
        if (item == NI_I_OPEN) {
            /* The Standard File dialog does NOT block: it returns with the
             * dialog on screen and calls os88_onfile much later; Cancel calls
             * nothing at all (SPEC.md 38). -1 is "one is already up", which
             * is a normal answer and not a failure. */
            os88_file_dlg(OS88_FDLG_OPEN, win, 0);
            /* THE SHADOW IS NOT DROPPED HERE EITHER (infones.c's os88_onfile
             * carries the whole argument): the kernel's damage rect is
             * authoritative about what the dialog covered and cannot
             * under-report (SPEC.md 11.90.2), so wm_destroy's own repaint
             * repairs exactly those rows off our own paper when the dialog
             * closes. Dropping it here could only turn the rows the dialog
             * NEVER touched into padded 40-cell runs. */
            return 0;               /* nothing to repaint yet - os88_onfile
                                     * will */
        }
        if (item == NI_I_RESET) {
            /* InfoNES's ﾘｾｯﾄ(&R), and the R key inside the bracket is the
             * same command (InfoNES's own add_key, Linux:262-266). A SOFT
             * reset: the 6502's reset sequence runs, the emulated RAM is left
             * alone, which is what the console's button does. */
            ni_reset_machine(0);
            ni_state = NI_ST_READY;
            ni_panel_state();
            /* THE STATE LINE KEEPS THE STATE AND THE TOAST CARRIES THE NOTE.
             * A draft called ni_say here, which set NI_F_STATE to `Reset.` -
             * a note, sitting on the machine's state row for the rest of the
             * session, with a ROM loaded and the field documented as
             * "No ROM / Ready / Running / a refusal" (nipanel.c). The row
             * reads `Ready` and the strip says what just happened, which is
             * what a strip with a TTL is for (apps/c64 gives its message row
             * an expiry instead; here there is nothing to expire). */
            os88_toast("Reset.", 0);
            return 1;
        }
        if (item == NI_I_STOP) {
            /* InfoNES's 停止(&S) halts the emulation THREAD. There is no
             * second task here - the machine runs only inside the bracket -
             * so Stop UNLOADS the ROM and returns the panel to its No ROM
             * state. Recorded as an adaptation in SPEC.md 91.2.1 ("Stop keeps
             * its position and changes meaning"), which is where nimenu.c
             * points too - NOT in 91.11, which is the DROP list. */
            ni_rom_free();
            ni_state = NI_ST_NOROM;
            ni_panel_rom();
            ni_panel_state();
            return 1;
        }
    }
    if (menu == NI_M_OPT) {
        /* THE FRAME-SKIP ITEMS, and they are here rather than resident for
         * SPEC.md 73.14's frequency test: a menu pick runs once. `Full
         * screen` is the counterpart and is answered in infones.c, because it
         * is the one command that must work on a disk whose .OVL is missing
         * (SPEC.md 91.6.5).
         *
         * 減らす is DECREASE and 増やす is INCREASE - of the SKIP COUNT - so
         * `increase` is the FASTER direction, which is why the English labels
         * say faster and slower (nimenu.c records the adaptation). */
        if (item == NI_I_SKIP) {
            ni_skip++;              /* the live item cycles Auto, 1, 2, 3, 4 */
            if (ni_skip > NI_SKIP_MAX)
                ni_skip = NI_SKIP_AUTO;
            return 1;
        }
        if (item == NI_I_FASTER) {
            if (ni_skip < NI_SKIP_MAX)
                ni_skip++;
            return 1;
        }
        if (item == NI_I_SLOWER) {
            if (ni_skip > NI_SKIP_AUTO)
                ni_skip--;
            return 1;
        }
    }
    return 0;
}
