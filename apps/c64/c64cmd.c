/* ============================================================================
 * os8088 - apps/c64/c64cmd.c        the menu command bodies - OUT OF LINE
 *
 * Part of C64 (C64-SPEC §11.1, 13.1). Derived from VICE 3.10,
 * Copyright (C) 1996-2025 the VICE team, GPL-2-or-later - see
 * apps/c64/COPYING. Nothing of VICE's source is vendored.
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * EVERYTHING HERE IS ovl_*, AND THAT IS THE WHOLE MECHANISM
 * ----------------------------------------------------------------------------
 * A function named ovl_* has its CODE emitted into C64.OVL, a module with a
 * segment of its own, and is far-called both ways (SPEC.md 73.14). The split
 * is by FREQUENCY, never by size: a keystroke's path stays resident and a
 * menu command's goes out. `CC_HAS_OVL` is on from the first commit
 * (C64-SPEC §13.1) because the alternative is discovering at 55,000
 * bytes that the code is not the kind that can move.
 *
 * TWO RULES BIND EVERY FUNCTION IN THIS FILE:
 *
 *  - IT ANSWERS A STATUS, AND 0 MEANS IT DID NOT HAPPEN. A refused load - no
 *    heap, a disk with no C64.OVL, a stale module, a worker task asking -
 *    toasts the reason and returns 0 without running (SPEC.md 47). The
 *    callers are written so that 0 is a normal path.
 *  - ONLY CODE MOVES. Every global, literal and bss byte named below is still
 *    resident and still a plain DS-relative reference, which is what makes
 *    the mechanism possible in C at all.
 *
 * And the .OVL cannot be loaded from os88_main - there is no instance yet to
 * resolve a module for (LESSONS.md 13) - so the first ovl_ call is made from
 * a callback, and its refusal is printed in the status row AS WELL AS toasted,
 * because a toast under a fullscreen window is not where the user is looking.
 * ==========================================================================*/

/* ovl_probe - C64-SPEC §13.3's FIRST `ovl_*` CALL, made from the first wake.
 *
 * The .OVL cannot be loaded from os88_main (LESSONS.md 13: there is no
 * instance yet to resolve a module for), so the module's availability is not
 * known until some callback needs it - and the first callback that needs it
 * is a menu command, which is exactly the moment when finding out is too
 * late to be useful. So the first wake asks, for nothing: the body is a
 * `return 1` and the whole point is the far call the RUNTIME makes on the way
 * in, which is what loads the module. A 0 is the runtime refusing the load -
 * no C64.OVL on the disk, a stale module, no heap - and c64.c prints
 * `Unable to load C64.OVL.` in the status row as well as toasting it, because
 * a toast under a fullscreen window is not where the user is looking (§9.8).
 * That is §13.3's sentence, implemented. */
static int ovl_probe(void)
{
    return 1;
}

/* ovl_cmd - one command. The kernel never dispatches a disabled item, so
 * every case below is one of C64-SPEC §11.1's LIVE items; the greyed
 * ones carry their fact in c64menu.c beside the string. */
static int ovl_cmd(int menu, int item, void *win)
{
    if (menu == C64_M_FILE) {
        if (item == C64_I_ATTACH) {
            /* File > Smart attach... (Alt+A) on .PRG, through the Standard
             * File dialog. It does not block: the answer arrives at
             * os88_onfile (11.3). */
            os88_file_dlg(OS88_FDLG_OPEN, win, "*.PRG");
            return 1;
        }
        if (item == C64_I_RESETCPU || item == C64_I_POWER) {
            /* Reset machine CPU vs Power cycle machine: on a real machine the
             * difference is the RAM, and this port keeps that difference -
             * the power cycle fills RAM the way a cold machine comes up and
             * the CPU reset does not.
             *
             * AND "THE WAY A COLD MACHINE COMES UP" IS NOT ZEROS. VICE gives
             * the C64 the factory pattern at src/ram.c:169-177 - 00 00 00 00
             * FF FF FF FF, offset by two bytes and inverted every 16K - and
             * c64_ram_pattern (c64.c) is that arithmetic. A zero fill is
             * harmless while there is no core, and wrong the moment there is
             * one: the KERNAL's RAM test and any program that reads
             * uninitialised memory both see it. */
            if (item == C64_I_POWER)
                c64_ram_pattern();
            c64_reset_regs();
            c64_lum_update();
            c64_frame_regs();
            c64_reset_cpu();                /* the 6510 out of reset: I set,
                                             * PC from $FFFC under the KERNAL
                                             * (11.1) - and the KERNAL draws
                                             * its own boot screen from here */
            if (!c64_norom)
                c64_state = C64_ST_RUN;
            /* AND THE MENU IS RE-SPELLED, BECAUSE THIS IS A PATH OUT OF
             * C64_ST_JAM. c64_jam() greys Preferences > Advance frame - there
             * is no machine left to advance (SPEC.md 47) - and the FACT that
             * greys it is the permanent `Main CPU: JAM at $XXXX` line on the
             * status row. A reset clears the state, so c64_status stops
             * drawing that line at once; without this call the greying
             * outlived the fact by the rest of the session, and then un-greyed
             * itself at random, because the only other callers of
             * c64_menu_state are the Warp/Pause/Swap/Fullscreen latches - so
             * whether Advance frame worked depended on which UNRELATED menu
             * item you had last picked. Every path that leaves C64_ST_JAM
             * re-runs it; it is harmless on the c64_norom path, where the
             * state does not change and the greying is still owed. */
            c64_menu_state();
            c64_dirty_all();
            c64_kick = 1;
            /* AND NOT c64_sh_inval(). Nothing covered the glass across a
             * reset, so the shadow is still true, and c64_dirty_all is the
             * RECOMPOSE. sh_inval is the FORCE, and forcing switches off the
             * frame compare that exists to answer "the picture did not
             * change, draw nothing": a reset from the boot screen back to the
             * boot screen is exactly that case, and it cost 25 forced blits,
             * ~266 ms, four host ticks. c64scr.c's own header states the
             * distinction. */
            return 1;
        }
        if (item == C64_I_EXIT) {
            /* Exit emulator (Alt+Q). There is no self-close slot, so this is
             * cword's File > Close idiom: a worker that sleeps and destroys
             * the window (SPEC.md 74).
             *
             * TWO CONTRACTS, AND THE FIRST DRAFT BROKE BOTH.
             *
             *  - os88_task_spawn wants the gfx lock HELD, and it is: we are
             *    reached from os88_oncmd, which the kernel dispatches UNDER
             *    the lock (apps/cc/crt0.asm's cc_oncmd: "you must never take
             *    the lock"). Taking it here spun on a NON-RECURSIVE lock this
             *    task was already standing on - kernel/ui.inc's
             *    ui_post_cmd names the outcome: "hang the machine dead: no
             *    beep, no watchdog, no recovery".
             *  - IT ANSWERS 0 FOR SUCCESS and -1 for a refusal (os88.h). A
             *    refusal is the 12-slot task table being full, which the SDK
             *    calls "NORMAL and transient": the right response is to leave
             *    the state alone so the next Exit retries, not to latch
             *    C64_ST_DEAD on a window nothing is closing. */
            if (os88_task_spawn(win) == 0)
                c64_state = C64_ST_DEAD;    /* the worker owns the window now:
                                             * stop flushing into it */
            else
                c64_say("Cannot start the closer - try again.");
            return 1;
        }
        return 1;
    }

    /* C64_M_EDIT has no live item in this wave: Copy and Paste are GREYED
     * with their fact beside them in c64menu.c (SPEC.md 47), so the kernel
     * never dispatches either and there is no case for them here. An item
     * that is live and then toasts a refusal is exactly what 47 forbids. */

    if (menu == C64_M_PREF) {
        /* THE FOUR LIVE ITEMS HERE ARE VICE'S CHECK ITEMS
         * (UI_MENU_TYPE_ITEM_CHECK at uimachinemenu.c:682, :704, :708, :771 -
         * four of the EIGHT this menu has, c64menu.c rule 4),
         * and in VICE the checkbox IS how a user reads the state. This
         * kernel's menu has no check mark and its face has no glyph for one
         * (LESSONS.md 8), so the state is a `*` in the label and the item
         * pointer is swapped - apps/tracker's idiom and NOT solitaire's
         * MENU_DIS twin, because MENU_DIS means "you cannot have this"
         * (SPEC.md 47) and greying the ON item would make warp impossible to
         * turn OFF. c64_menu_state() is the one place that swaps them, and
         * the warp and pause latches also light their `W` and `P` lamps
         * on the status row
         * (C64-SPEC §10.2). */
        if (item == C64_I_FULLSCR) {
            /* SPEC.md 11.2's latch, on VICE's own Alt+D - a stated exception
             * to SPEC.md 11.2.1, because the C64 owns F and Esc (9.8). The
             * BODY IS RESIDENT (c64.c's c64_fullscreen_toggle) because Alt+D
             * itself reaches it from os88_onkey, which is a keystroke and
             * never loads an overlay - and under WF_FULL there is no menu bar,
             * so the chord is the only door back.
             *
             * AND NEITHER ROUTE TOUCHES THE SHADOW, which is the whole point.
             * os88.h:604 states the contract - "The kernel repaints you whole
             * either way" - and kernel/wm.inc:4147 shows it: wm_fullscreen
             * calls wm_raise with AL = 1, so os88_paint has ALREADY run
             * NESTED INSIDE the call, seen whole damage, invalidated the
             * shadow itself and drawn all 25 rows for the new geometry. An
             * c64_sh_inval() here would throw that shadow away and the next
             * wake would draw the identical picture a second time - 25 bands,
             * ~300 ms and four host ticks of pure double-draw, the class
             * CLAUDE.md names as invisible in an emulator.
             * apps/runcpm/runcpm.c:1086-1095 records this defect in its own
             * words and this port copied the call instead of the lesson. */
            c64_fullscreen_toggle(win);
            return 1;
        }
        if (item == C64_I_WARP) {
            c64_warp = !c64_warp;
            c64_menu_state();
            c64_say(c64_warp ? "Warp mode on." : "Warp mode off.");
            return 1;
        }
        if (item == C64_I_PAUSE) {
            c64_pause = !c64_pause;
            c64_menu_state();
            c64_say(c64_pause ? "Paused." : "Running.");
            return 1;
        }
        if (item == C64_I_ADVANCE) {
            /* LIVE AS OF WAVE 2 (§11.1). It was greyed with the fact that
             * greyed it - "there is no raster accumulator until the alarm
             * model lands with the core" - and this wave landed the alarm
             * model, so the fact stopped being true and SPEC.md 47 does not
             * let a greying outlive its reason. The body raises a request:
             * this handler runs under the gfx lock and a PAL frame is 19,656
             * emulated cycles, which is not a thing to hold the desktop for.
             *
             * AND IT IS NOT A CHECK ITEM: src/arch/gtk3/actions-speed.c:72-80
             * (identically src/arch/gtk3/ui.c:2735-2743) PAUSES a running
             * machine and advances only an already-paused one, so from a
             * running machine this item runs no frame at all. c64.c's
             * c64_advance_frame is that action, transcribed; and with no
             * machine at all - no C64.ROM, or a JAM - c64_menu_state greys
             * the item rather than let it be a silent no-op (§47). */
            c64_advance_frame();
            return 1;
        }
        if (item == C64_I_SWAPJOY) {
            c64_joyswap = !c64_joyswap;  /* the row's two indicators swap,
                                          * which its own field compare sees */
            c64_menu_state();
            c64_say(c64_joyswap ? "Joysticks swapped." : "Joysticks normal.");
            return 1;
        }
        return 1;
    }
    return 1;
}
