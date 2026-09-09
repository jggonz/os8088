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
 *    wake, and WAVE 4 PROVED IT ON THE MACHINE: a scratch copy of
 *    build/apple2.img with APPLE2.OVL deleted boots to a working Apple II at
 *    `]` whose menu commands say `Unable to load APPLE2.OVL.` on the status
 *    row and toast it on the bar (build/port-shots/wave4-23-noovl.png,
 *    wave4-24-noovl-refuse.png, wave4-25-noovl-toast.png). That was the
 *    wave's own done_when, driven over QMP - there is no registered test row
 *    for it and this sentence does not pretend otherwise.
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

/* ovl_a2_cmd - THE MENU COMMAND SHELLS (APPLE2-SPEC section 10.2).
 *
 * The kernel never dispatches a disabled item, so every case below is one of
 * section 10.2's LIVE rows; the greyed ones carry their fact in a2menu.c
 * beside the string. Three live rows are NOT here and each says why in
 * os88_oncmd: File > Quit (it is the one command that must work on a disk
 * with no APPLE2.OVL), Machine > Toggle Fullscreen (Ctrl+F reaches the same
 * body from os88_onkey, and a keystroke never loads an overlay) and Machine >
 * Flashing text (the phase exists with or without a module).
 *
 * EVERY BODY HERE IS A LATCH OR A FLAG, AND THAT IS THE WHOLE DESIGN.
 * os88_oncmd is dispatched under the DESKTOP's gfx lock, so an instruction
 * executed here is the whole machine stopped: a reset re-reads a vector and
 * re-marks the frame, a Power On fills 48KB, a Copy walks the text page and a
 * Paste takes a claim and reads the clipboard. Each sets one word and the
 * WAKE, which holds no lock, spends it (apple2.c's a2_reset_service,
 * a2kbd.c's ovl_a2_clip_service, a2prog.c's ovl_a2_prog). What stays here is
 * the compares that decide WHICH latch, which is once per command by
 * definition.
 *
 * It answers 1 when it handled the command. */
static int ovl_a2_cmd(int menu, int item, void *win)
{
    if (menu == A2_M_FILE) {
        /* File > Load Program... / Save Program... (section 12). The picker
         * does NOT block: the answer arrives at os88_onfile much later, and a
         * Cancel calls nothing at all.
         *
         * AND THE REFUSAL IS READ. os88_file_dlg answers -1 when another modal
         * dialog already owns the screen - one at a time, machine-wide - and
         * discarding it makes the item a silent no-op, which is the shape
         * SPEC.md 47 exists to stop (c64cmd.c found this on Smart attach). */
        if (item == A2_I_LOAD) {
            /* THE THIRD ARGUMENT IS A DEFAULT NAME AND NOT A FILTER, and
             * OPEN takes 0 for it. `"*.BAS"` was a filter written in a slot
             * that has none: SPEC.md 38.9 says outright that the dialog does
             * no filtering by extension, and 38.5 that Open mode has no field
             * to type in - but the box is still DRAWN with the name in it
             * (kernel/fdlg.inc's fdlg_name_body suppresses the caret and not
             * the string), and fdlg_actok lights the Open button as soon as
             * the name is non-empty. So the literal `*.BAS` sat on the glass
             * and Open was live before anything was selected, committing the
             * name `*.BAS` and ending at `Cannot read the file.` With 0 the
             * box starts empty and SPEC.md 38.10's per-application last-picked
             * name seeds it, which is what the slot is for. cwcmd.c:292 and
             * weave.c:699 both pass 0; c64cmd.c:251's "*.PRG" is the same
             * defect one package along and is that package's to fix. */
            if (os88_file_dlg(OS88_FDLG_OPEN, win, 0) < 0)
                a2_say("A file dialog is open.");
            return 1;
        }
        if (item == A2_I_SAVE) {
            if (os88_file_dlg(OS88_FDLG_SAVE, win, "PROGRAM.BAS") < 0)
                a2_say("A file dialog is open.");
            return 1;
        }
        return 0;
    }

    if (menu == A2_M_EDIT) {
        /* Edit > Copy and Edit > Paste (section 6.5). BOTH ARE A LATCH AND
         * NOTHING ELSE, and a2kbd.c's header carries the whole argument: the
         * bodies reach kernel/clip.inc's mem_claim, which may compact an arena
         * this app has a pinned 64KB in, and that is not a term a command
         * handler can bound (LESSONS.md 6). The wake that spends them is the
         * one os88_oncmd posts after this returns, and no emulated cycle runs
         * in between - so the page copied is the page on the glass.
         *
         * COPY SAYS NOTHING ON SUCCESS: the result is on the clipboard and
         * the window it came from has not changed. Every refusal is said. */
        if (item == A2_I_COPY) {
            a2_copy_req = 1;
            return 1;
        }
        if (item == A2_I_PASTE) {
            a2_paste_req = 1;
            return 1;
        }
        return 0;
    }

    if (menu == A2_M_MACHINE) {
        if (item == A2_I_RESET) {
            a2_reset_req = A2_RST_CTRL;
            return 1;
        }
        if (item == A2_I_OARESET) {
            a2_reset_req = A2_RST_OACTRL;
            return 1;
        }
        if (item == A2_I_POWER) {
            /* Machine > Power On - AppleWin's own wording for the cold boot
             * (help/keyboard.html:15), and its own CONFIRMATION for it:
             * ConfirmReboot (source/Windows/WinFrame.cpp:1997-2013) puts up
             * MB_ICONWARNING|MB_YESNO titled `Reboot` whose first two lines
             * are the two this port draws. The rest of AppleWin's box is
             * about a `Confirm reboot` checkbox in a Configuration dialog
             * this port does not have, so it is not transcribed - a
             * confirmation that points at a control the reader cannot reach
             * is the guess SPEC.md 47 forbids.
             *
             * THE LATCH IS SET BY THE ANSWER, NOT BY THE PICK. Nothing
             * happens here but a panel going up. */
            return ovl_a2_confirm(win);
        }
        if (item == A2_I_MUTE) {
            /* Machine > Mute - MII's m_audio_menu `Mute`, ticked from the
             * same MUI_MENUBAR_ACTION_PREPARE arm as `Color NTSC`
             * (mii_mui_menus.c:151-156), which is why the row already owned
             * the two-glyph column before this wave gave it a body.
             *
             * IT TAKES THE NOTE DOWN ON THE PICK and not on the next wake:
             * a2_spk_service tests a2_mute too, so the tone could not survive
             * a wake either, but the user pressed a menu item called Mute and
             * the 55 ms is audible. Un-muting says nothing and restores
             * nothing - the estimator re-measures, which is a2_sound_stop's
             * own rule (section 8): a machine that stopped toggling while
             * muted is silent, and one that is still toggling is sounding
             * again three toggles later. */
            a2_mute = !a2_mute;
            if (a2_mute)
                a2_sound_stop();
            a2_menu_state();
            if (a2_mute)
                a2_say("Muted.");
            else
                a2_say("Unmuted.");
            return 1;
        }
        return 0;
    }

    if (menu == A2_M_CPU) {
        if (item == A2_I_NORMAL) {
            /* CPU > `Normal: 1MHz` - MII's `mhz1`, which sets the machine's
             * speed back to MII_SPEED_NTSC (mii_mui_menus.c:327-329). This
             * port has no throttle, so what "normal" means here is WARP OFF:
             * `Normal: 1MHz`, `Fast: 3.5MHz` and `Warp` are the same three
             * radio partners MII has, and the two states this machine can be
             * in are the ends of that row.
             *
             * ITS MARK IS NOT ITS STATE, and that is worth saying beside the
             * body: a2_menu_state ticks this row from the MEASURED per cent
             * (MII's own 0.9..1.1 band, :165-170), not from the warp latch, so
             * on a host running the core at 1,700 % the row is unticked with
             * warp off and the status row is why. Picking it on an already
             * un-warped machine does nothing and says nothing, exactly as
             * picking Stop on a stopped machine does - which is MII's own
             * behaviour and not a silent refusal (SPEC.md 47). */
            if (a2_warp) {
                a2_warp_set(0);
                a2_menu_state();
                a2_say("Warp off.");
            }
            return 1;
        }
        if (item == A2_I_STOP || item == A2_I_RUN) {
            /* CPU > Stop / Continue - MII's SIGNAL_STOP and SIGNAL_RUN
             * (mii_mui_menus.c:333, :350), which are TWO ITEMS and not one
             * toggle: Stop stops a running machine and Continue runs a
             * stopped one, so picking Stop on a stopped machine does nothing.
             * The PREPARE arm retitles them (:180-196) and a2_menu_state is
             * where this port does the same.
             *
             * **AND A JAMMED MACHINE IS NEITHER.** The core never runs again
             * after A2_ST_JAM - the slice, a2_wants_wake and the status row
             * all test `a2_state == A2_ST_RUN` - so `Continue` set a2_pause to
             * 0, said `Running.`, and changed nothing. Worse than a
             * five-second lie: a2_status draws the message arm ABOVE the JAM
             * arm, so `Running.` REPLACED `6502: JAM at $xxxx`, the one row
             * that says why the machine is dead, and a jammed machine posts no
             * wake - so nothing was going to expire it. a2_menu_state greys
             * both rows on A2_ST_JAM now, which is a2_jam's own comment
             * ("there is no machine left to stop") finally acted on, and this
             * compare is the second half of it: the kernel does not dispatch a
             * disabled item, but a2_state can change between the pull-down
             * being built and the pick landing. */
            if (a2_state != A2_ST_RUN)
                return 1;                   /* the JAM line stands */
            a2_pause = (item == A2_I_STOP) ? 1 : 0;
            a2_menu_state();
            /* TWO CALLS AND NOT A TERNARY, and that is the message-length
             * gate rather than style: build.sh walks the sources for a call
             * with a LITERAL as its first argument, and a literal behind a
             * `?` is one the gate never sees (section 9, and
             * apps/apple2/build.sh's own note about a gate that silently
             * narrows instead of failing). */
            if (a2_pause)
                a2_say("Stopped.");
            else
                a2_say("Running.");
            return 1;
        }
        if (item == A2_I_WARP) {
            /* CPU > Warp - OURS (section 10.1), and on this machine it is the
             * WALL SLICE'S CAP and nothing else. There is no throttle here to
             * take off: a2_slice runs a2_budget cycles a wake and the status
             * row reports what that came to, so the only thing warp can lift
             * is the ceiling the adaptation walks the budget up to.
             *
             * AND THE CAP COMES BACK DOWN WITH IT. An adapted budget above
             * A2_SLICE_MAX would otherwise outlive the warp it was granted
             * for - the only thing that lowers the budget is a slice that
             * overruns a host tick, and one that fits never would.
             *
             * ON THE TARGET IT SAYS SO, on PERFORMANCE.md's "degrade by tier"
             * and os88_cpu()'s FACT: at 4.77 MHz a 16,384-cycle slice is far
             * more than one host tick of work, so the adaptation settles the
             * budget near its 256-cycle floor and the ceiling is not what
             * binds. Announcing a speed-up the machine does not deliver is
             * what SPEC.md 47 exists to stop. It DOES bind on a 286 or a 386,
             * which is why the row is not greyed. */
            a2_warp_set(!a2_warp);      /* RESIDENT: the budget and its two
                                         * ceilings belong to the slice driver
                                         * (apple2.c), and a second copy of a
                                         * ceiling is a second ceiling */
            a2_menu_state();
            if (!a2_warp)
                a2_say("Warp off.");
            else if (os88_cpu() == OS88_CPU_8086)
                a2_say("Warp on - no change.");
            else
                a2_say("Warp on.");
            return 1;
        }
        return 0;
    }
    return 0;
}

/* ovl_a2_confirm - Machine > Power On's TWO-ROW confirmation (section 10.2).
 *
 * IT IS THE ABOUT PANEL WITH A KIND, and that is a decision this file states
 * rather than a shortcut: a confirmation is modal, snapped to the band,
 * holding the Apple scan lines it covers so nothing under it is drawn, and
 * dismissed as DAMAGE rather than as a repaint - which is the whole of what
 * a2about.c already is. A second copy would be a second ovl_about_geom, a
 * second hold range and a second arm in os88_paint, and the two would drift.
 *
 * The ANSWER is the resident half's (a2_panel_close, a2about.c), because a
 * click and a key are callbacks and a callback is reached by a near offset. */
static int ovl_a2_confirm(void *win)
{
    if (a2_abt_up) {
        /* THE ABOUT PANEL IS ALREADY UP: one panel at a time. And this IS
         * reachable, which the first version of this comment denied - the
         * panel is drawn inside the package's own window and only
         * os88_onclick / os88_onkey swallow input, so the KERNEL'S MENU BAR is
         * untouched and Machine > Power On can be picked with it up. Returning
         * silently made the item a no-op with nothing said, which is the class
         * SPEC.md 47 exists to stop and which the row one item along
         * (`A file dialog is open.`) already handles. **NOT `Close the About
         * panel first.`**, which the review asked for and measures 28 of the
         * row's 26 - it would have been cut on the glass, which is the defect
         * this wave already fixed three times over on the toasts. 22 cells. */
        a2_say("Close the About panel.");
        return 1;
    }
    a2_pan_kind = A2_PAN_CFM;
    if (!ovl_about_show(win)) {
        a2_pan_kind = A2_PAN_ABOUT;         /* not one pixel of us shows, or
                                             * the geometry refused: nothing
                                             * is up and nothing is owed */
        a2_say("The window is covered.");
    }
    return 1;
}
