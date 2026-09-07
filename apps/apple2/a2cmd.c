/* ============================================================================
 * os8088 - apps/apple2/a2cmd.c    ovl_*: the first-wake probe and the command
 *                                 SHELLS
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 15.5). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * EVERYTHING IN THIS FILE IS `ovl_` AND LIVES IN APPLE2.OVL (SPEC.md 73.14)
 * ----------------------------------------------------------------------------
 * The split is by FREQUENCY and never by size: what a keystroke touches - the
 * composers, the flush, the damage model, every callback - stays resident;
 * what runs once per COMMAND goes out. A menu command is once per command by
 * definition, so its shell is here.
 *
 * TWO THINGS THIS FILE MUST NOT CARRY, and both are named because both were
 * tempting:
 *
 *  - THE CHARGEN DECODE AND THE 7-BIT REVERSE TABLE. They are built in
 *    os88_main and are RESIDENT (section 7.3). A disk with no APPLE2.OVL must
 *    be a program whose MENUS refuse, not a window that draws nothing.
 *    hosttest/a2uitest.c asserts both tables after os88_main and before any
 *    wake TODAY; `tests/apple2part.py` (WAVE 4, docs/APPLE2-PORT-PLAN.md) is
 *    what will assert it on the machine. Neither file exists in this tree
 *    yet and this sentence does not pretend otherwise.
 *  - ANY PER-BYTE LOOP. Only code moves; every global, literal and bss byte
 *    the moved code names stays resident and DS-relative, which is what makes
 *    an overlay possible in C at all - but a loop that runs a thousand times
 *    a command is a loop that should not be crossing a far call.
 *
 * AND NEVER FROM A WORKER. The worker gate is decided from SP and not from a
 * flag, and this package hires no worker anyway (no CC_HAS_WORKER).
 * ==========================================================================*/

/* ovl_a2_init - THE FIRST-WAKE PROBE.
 *
 * The .OVL cannot be loaded from os88_main: there is no instance yet to
 * resolve a module for (LESSONS.md 13). So the first ovl_* call of the
 * session is this one, made from the FIRST WAKE - which holds no lock and may
 * call the file slots by contract (SPEC.md 74.1) - and its whole job is to
 * find out whether the module is there.
 *
 * IT ANSWERS 1 ONLY IF IT RAN. An overlay function that could not be loaded
 * answers 0 without running (the runtime's own refusal path), so `return 1`
 * IS the probe: 1 means the module resolved, 0 means the caller must say so
 * and the next wake must retry. Design every caller so 0 is a normal path. */
static int ovl_a2_init(void)
{
    return 1;
}

/* ovl_a2_cmd - the menu command shells.
 *
 * WAVE 2 REACHES TWO OF THEM - Machine > Control-Reset and Machine >
 * Open-Apple-Control-Reset, whose bodies are section 4.5's reset line. Every
 * other command whose body needs a 6502 is still GREYED (a2menu.c's rule 4,
 * and section 10.3's "temporary and says so in the source"), and the three
 * live in the RESIDENT half - Quit, Toggle Fullscreen and Flashing text - are
 * answered there for reasons a2menu.c states beside each. So this is still
 * the shell the later waves fill:
 *
 *   wave 4  Load/Save Program (a2prog.c), Copy and Paste, Power On WITH
 *           AppleWin's two-row confirmation, Stop/Continue and Warp
 *   the Disk II follow-up  Configure Slots... (a2disk.c)
 *
 * NEITHER RESET RUNS A BODY HERE. os88_oncmd is dispatched under the DESKTOP's
 * gfx lock and a reset re-reads the vector, re-marks the frame and (for Power
 * On) fills 48KB - so this LATCHES and the WAKE, which holds no lock, spends
 * it (apple2.c's a2_reset_service). What is in the overlay is the two compares
 * that decide which latch, which is once per command by definition.
 *
 * It answers 1 when it handled the command. */
static int ovl_a2_cmd(int menu, int item, void *win)
{
    (void)win;
    if (menu == A2_M_MACHINE) {
        if (item == A2_I_RESET) {
            a2_reset_req = A2_RST_CTRL;
            return 1;
        }
        if (item == A2_I_OARESET) {
            a2_reset_req = A2_RST_OACTRL;
            return 1;
        }
    }
    return 0;
}
