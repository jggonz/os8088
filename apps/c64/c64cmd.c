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

/* ovl_conv_init - C64-SPEC §13.3's FIRST `ovl_*` CALL, made from the first
 * wake, AND the two conversion tables Copy and Paste index.
 *
 * TWO JOBS IN ONE FUNCTION, AND BOTH ARE THE SAME FACT ABOUT FREQUENCY.
 *
 *  - The .OVL cannot be loaded from os88_main (LESSONS.md 13: there is no
 *    instance yet to resolve a module for), so the module's availability is
 *    not known until some callback needs it - and the first callback that
 *    needs it is a menu command, which is exactly the moment when finding out
 *    is too late to be useful. So the first wake asks. A 0 is the runtime
 *    refusing the load - no C64.OVL on the disk, a stale module,
 *    no heap - and c64.c prints `Unable to load C64.OVL.` in the status row as
 *    well as toasting it, because a toast under a fullscreen window is not
 *    where the user is looking (§9.8).
 *  - And what it does while it is here is the ONE thing in this program that
 *    runs exactly once: VICE's three transcribed conversions, called 128 and
 *    256 times into c64_sctab and c64_pettab (c64kbd.c). They used to be
 *    three resident functions - ~275 emitted instructions, ~700 bytes of
 *    image - sitting beside the loops that index their output a thousand
 *    times a Copy. SPEC.md 73.14 splits by FREQUENCY, and once-per-launch is
 *    the side that goes out; the TABLES are bss and stay resident, which is
 *    what makes the move legal (only code moves).
 *
 * THE TWO LOOPS ARE WRITTEN OUT INLINE AND NOT AS THREE FUNCTIONS, and that
 * is a boundary decision rather than a style one: a function an ovl_* calls
 * is either resident (and this move buys nothing) or in the module (and every
 * one of the 384 calls is `call far [cc_ovm_...]`, ~58 us of bridge before
 * any work). One function, two loops, no calls.
 *
 * IT IS THE ONE PLACE VICE'S ARITHMETIC IS WRITTEN, and the tables are still
 * derived from it rather than being magic arrays somebody typed. */
static int ovl_conv_init(void)
{
    int i, c;

    /* --- screen code -> host ASCII, which is three of VICE's functions
     * composed in the order src/clipboard.c:73 composes them:
     *
     *   charset_screencode_to_petscii  (charset.c:202-210)
     *   petcii_fix_dupes               (charset.c:112-120)
     *   charset_p_toascii              (charset.c:132-162, WITH ctrl codes)
     *
     * edit_copy_action's own final pass (actions-clipboard.c:69-76) is NOT a
     * fourth step here, and the first draft of this port thought it was. That
     * pass replaces a byte only when it is neither `\r`/`\n` NOR printable
     * ASCII - and every byte charset_p_toascii can answer on this path is one
     * of those, so it never fires. See the unmapped arm below.
     *
     * The index is 0..127 because the reverse-video bit is not a character
     * (charset.c:204) and the copy loop masks it off. */
    for (i = 0; i < 128; i++) {
        if (i <= 0x1F)
            c = i + 0x40;
        else if (i >= 0x40 && i <= 0x5F)
            c = i + 0x20;
        else
            c = i;
        if (c >= 0x60 && c <= 0x7F)         /* petcii_fix_dupes: the CHROUT
                                             * duplicates onto the proper
                                             * codes (charset.c:114-115) */
            c = c - 0x60 + 0xC0;
        if (c == 0x0D)
            c = '\n';
        else if (c == 0x0A)
            c = '\r';
        else if (c == 0xA0)                 /* PETSCII shifted space */
            c = ' ';
        else if (c >= 0xC1 && c <= 0xDA)
            c = 'A' + (c - 0xC1);
        else if (c >= 0x41 && c <= 0x5A)
            c = 'a' + (c - 0x41);           /* ...and this is the LOWER case,
                                             * charset.c:156-158 */
        else if (c < 0x20 || c > 0x7E)
            /* ASCII_UNMAPPED IS '.', NOT '?' (charset.c:126), AND THE COMMENT
             * THAT USED TO SAY OTHERWISE READ THE REFERENCE BACKWARDS.
             * charset.c:122-127 gives the reason in VICE's own words - these
             * bytes end up in host FILENAMES, so a wildcard is the one thing
             * they must not become - and edit_copy_action's mangle
             * (actions-clipboard.c:69-76) never touches a '.', because '.' is
             * printable. VICE's Copy output cannot contain a '?' at all.
             *
             * WHICH CELLS ARE THESE? After the two steps above, screen codes
             * $40, $5B-$5F, $60 and $7B-$7F (and their reverse twins) - the
             * GRAPHICS cells, which is exactly what a user copying a game
             * screen or a PETSCII drawing hits, so it is not a corner. The
             * port's own font fallback already answers '.' for the same cells
             * (c64scr.c:1146) and the two now agree. */
            c = '.';
        c64_sctab[i] = (unsigned char)c;
    }

    /* --- host byte -> PETSCII: charset_p_topetscii (charset.c:174-199). The
     * line endings are NOT part of the map - charset_petconvstring tests for
     * them FIRST (:72-74) - so the pair test is c64_paste_feed's and the
     * single-byte answer is written over the map below. */
    for (i = 0; i < 256; i++) {
        if (i <= 0x1F)
            c = 0x3F;                       /* PETSCII_UNMAPPED, charset.c:172
                                             * - and here it IS '?', because
                                             * this direction is the one whose
                                             * comment (charset.c:164-171)
                                             * says '.' breaks `foobar~1.prg`
                                             * style names */
        else if (i == '`')
            c = 0x27;
        else if (i >= 'a' && i <= 'z')
            c = 0x41 + (i - 'a');
        else if (i >= 'A' && i <= 'Z')
            c = 0xC1 + (i - 'A');           /* NOT the 0x61 duplicates
                                             * (charset.c:190-191) */
        else if (i >= 0x7B)
            c = 0x3F;
        else if (i >= 0x60 && i <= 0x7F)    /* petcii_fix_dupes again, which is
                                             * what :199 returns through */
            c = i - 0x60 + 0xC0;
        else
            c = i;
        c64_pettab[i] = (unsigned char)c;
    }
    /* ...AND THE TWO LINE ENDINGS ARE OVERWRITTEN, because they are not byte
     * map entries at all: charset_petconvstring tests for a line ending
     * BEFORE the map (charset.c:72-74) and answers ONE PETSCII CR for CR, for
     * LF and for the CR LF pair. charset_p_topetscii sends both to
     * PETSCII_UNMAPPED, which is right for the map and wrong for the string,
     * so the string's answer goes in here and the feeder tests only for the
     * PAIR. */
    c64_pettab[0x0D] = 0x0D;
    c64_pettab[0x0A] = 0x0D;

    c64_conv_ok = 1;                        /* ...and THIS is what the two
                                             * commands check, not the probe's
                                             * `asked once` flag: a load that
                                             * was refused for a TRANSIENT
                                             * reason (no heap) and succeeded
                                             * later would otherwise reach
                                             * ovl_copy with 128 zero bytes of
                                             * table and put a screenful of
                                             * NULs on the user's clipboard */
    return 1;
}

/* ==========================================================================
 * EDIT > COPY AND EDIT > PASTE (11.1, 7.7)
 *
 * ONLY THE COMMAND SHELL IS HERE. The clipboard calls, the refusals and the
 * truncation notice run once per pick and belong in the module; the 40x25
 * walk, the table LOOKUPS and the CRLF fold run PER BYTE and are resident
 * (c64kbd.c's c64_copy_screen / c64_paste_feed), because SPEC.md 73.14
 * splits by FREQUENCY and every iteration of a loop written here would cross
 * the segment boundary - twice a cell, as the first draft of this file did:
 * 2,000 far-call round trips for one Copy, ~110 ms, all of it under the gfx
 * lock. c64kbd.c's own header carries the whole finding.
 *
 * The two TABLES those lookups index are bss and resident; the code that
 * FILLS them is ovl_conv_init above, because filling them happens once a
 * launch and the same rule sends it the other way.
 *
 * Both conversions are VICE's, transcribed from src/charset.c, and neither is
 * a table this port invented. The one thing worth knowing before reading them
 * is that the answer is NOT the letters you can see: PETSCII $41-$5A is the
 * LOWER case (charset_p_toascii, charset.c:156-158) and $C1-$DA the upper
 * (:153-155), whichever character set the VIC is drawing - so a Copy of the
 * boot screen puts `**** commodore 64 basic v2 ****` on the clipboard, in
 * lower case, exactly as VICE does. That is transcribed and not chosen, and
 * C64-SPEC §7.7 states it so nobody files it as a defect.
 * ========================================================================*/

/* ovl_copy / ovl_paste - Edit > Copy (Alt+Delete) and Edit > Paste
 * (Alt+Insert): clipboard_read_screen_output (src/clipboard.c:41-100) and
 * paste_callback (actions-clipboard.c:96-110).
 *
 * BOTH ARE A LATCH AND NOTHING ELSE. os88_oncmd is dispatched under the
 * DESKTOP's gfx lock, and neither body is bounded by a count that can be
 * stated there: Copy is ~103 ms of conversion, Paste is a 2KB heap claim and
 * an os88_clip_get, and both reach kernel/clip.inc's mem_claim, which may
 * compact an arena holding this app's pinned 64KB and pinned 20KB. The
 * bodies are c64_clip_service (c64kbd.c), run from the top of the next wake
 * with no lock held; that function's header carries the whole argument, and
 * the wake that spends the latch is the one os88_oncmd posts after this
 * returns.
 *
 * COPY SAYS NOTHING ON SUCCESS, because VICE says nothing: the result is on
 * the clipboard and the window it came from has not changed. Every refusal -
 * no heap for the staging claim, the clipboard over CLIP_MAXKB, an empty
 * clipboard, a truncated paste - is a fact and is said, on the row, by the
 * service.
 *
 * COPY IS NOT REACHABLE WITH NO MACHINE and PASTE is greyed with no machine,
 * on a JAM and while PAUSED. c64menu.c's c64_menu_state carries each fact:
 * on a machine that never started the matrix holds nothing but
 * c64_ram_pattern's factory fill
 * and the clipboard is KERNEL-OWNED and outlives this app (SPEC.md 55.3), so
 * a Copy there would destroy what the user copied elsewhere in exchange for a
 * screen they cannot see. Copy stays LIVE on a JAM - the frozen screen is
 * real, and VICE copies what is displayed - while a paste there would be
 * bytes queued in silence for the rest of the session, because c64_paste_feed
 * is called only from the C64_ST_RUN, not-paused arm of os88_onwake. */

/* ovl_cmd - one command. The kernel never dispatches a disabled item, so
 * every case below is one of C64-SPEC §11.1's LIVE items; the greyed
 * ones carry their fact in c64menu.c beside the string. */
static int ovl_cmd(int menu, int item, void *win)
{
    if (menu == C64_M_FILE) {
        if (item == C64_I_ATTACH) {
            /* File > Smart attach... (Alt+A) on .PRG, through the Standard
             * File dialog. It does not block: the answer arrives at
             * os88_onfile (11.3).
             *
             * AND THE REFUSAL IS READ. os88_file_dlg answers -1 when another
             * modal dialog already owns the screen (os88.h) - one dialog at a
             * time, machine-wide - and the first draft discarded it, so
             * picking Smart attach... with somebody else's Save box up did
             * nothing at all and said nothing at all, which is the silent
             * no-op SPEC.md 47 forbids. */
            if (os88_file_dlg(OS88_FDLG_OPEN, win, "*.PRG") < 0)
                c64_say("A file dialog is already open.");
            return 1;
        }
        if (item == C64_I_RESETCPU || item == C64_I_POWER) {
            /* Reset machine CPU vs Power cycle machine: on a real machine the
             * difference is the RAM, and this port keeps that difference.
             *
             * THE BODY IS A LATCH, for the reason Copy's and Paste's are.
             * Power cycle's RAM fill is 65,536 bytes through c64_zfill - two
             * calls per 128 bytes of VICE's factory pattern - and os88_oncmd
             * is dispatched under the DESKTOP's gfx lock, so doing it here was
             * about a quarter of a second of every other task's drawing, the
             * mouse and the dock stopped dead for a command that draws
             * nothing. c64_reset_service (c64.c) runs the whole thing from the
             * top of the next wake with no lock held, and no emulated cycle
             * runs in between, so the machine the user reset is the machine
             * that is reset. */
            c64_reset_req = (item == C64_I_POWER) ? 2 : 1;
            return 1;
        }
        return 1;
    }

    if (menu == C64_M_EDIT) {
        /* LIVE AS OF WAVE 3. Both were greyed with the fact that greyed them
         * - neither conversion table nor the $0277 feeder was written - and
         * this wave wrote all three, so SPEC.md 47 does not let the greying
         * outlive its reason. The captions are still VICE's and the MENU ITEM
         * is still the guaranteed route: an AT BIOS int 16h AH=0 drops the
         * enhanced codes Alt+Delete and Alt+Insert carry (7.5), and where a
         * BIOS does pass them os88_onkey dispatches them straight here. */
        /* ...AND THE TABLES ARE THERE, WHICH IS NOT THE SAME QUESTION AS
         * `the module loaded once`. ovl_conv_init runs on the first wake and
         * its refusal is a fact the user has already been told (§13.3), but a
         * load refused for a TRANSIENT reason - no heap at that moment - and
         * granted later would reach the loops below with 128 zero bytes of
         * table and put a screenful of NULs on the user's clipboard. One
         * compare, and the retry costs one bridge crossing in a case nobody
         * will meet: if we are running at all, this module is resident, so
         * this call cannot itself be refused. */
        if (!c64_conv_ok)
            ovl_conv_init();
        if (item == C64_I_COPY)
            c64_copy_req = 1;               /* the WAKE does it (c64kbd.c) */
        else if (item == C64_I_PASTE)
            c64_paste_req = 1;
        return 1;
    }

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
            /* WARP IS TWO THINGS IN VICE AND IT IS THE SAME TWO HERE
             * (C64-SPEC §4.4). The throttle comes off - here that is the wall
             * slice's ceiling, 16,384 -> 30,000, because nothing in this port
             * paces the machine against a wall clock and the ceiling is the
             * only throttle there is - AND THE RENDERING IS CAPPED AT 10 fps.
             *
             * THE SECOND HALF IS VICE'S, IN VICE'S WORDS, and one pass of
             * this review deleted it on the reading that a warp which draws
             * less is `a different feature with the same name`. src/vsync.c
             * says otherwise twice: :339-340 sets
             * `warp_render_tick_interval = tick_per_second() / 10.0` under
             * `Limit warp rendering to 10fps`, and :634-656 skips frames
             * while `warp_enabled` under `Limit rendering fps if we're in
             * warp mode. It's ugly enough for dqh to weep but makes warp
             * faster.` Drawing less IS half of what VICE's warp does, and on
             * this machine it is the expensive half: §9.7 prices a full
             * expose at 306 ms against a slice of 16,384 emulated cycles.
             * c64.c's flush gate is where the cap lives - 18.2 Hz / 10 = 1.82
             * ticks, so two - and it can only ever SLOW the flush down, which
             * is why the 8086 tier (already every other tick, 9.1 Hz) is
             * unmoved by it and the message below is still true there.
             *
             * AND THE CAP COMES BACK DOWN WITH IT. An adapted budget above
             * C64_SLICE_MAX would otherwise outlive the warp it was granted
             * for - the only thing that lowers it is a slice that overruns a
             * host tick, and one that fits never would. */
            c64_warp = !c64_warp;
            if (c64_warp)
                c64_sound_stop();           /* VICE suspends the sound on the
                                             * way into warp (src/vsync.c:181
                                             * -> src/sound.c:1819): a machine
                                             * running at 3,000 % has nothing
                                             * meaningful to play, and the note
                                             * it was holding would simply
                                             * stand for the whole warp */
            if (!c64_warp && c64_budget > C64_SLICE_MAX)
                c64_budget = C64_SLICE_MAX;
            c64_menu_state();
            /* ...AND ON THE TARGET IT SAYS SO, AND THE RENDER CAP DOES NOT
             * CHANGE THAT. BOTH of warp's halves are no-ops on CPU_8086, for
             * two separate reasons, and C64-SPEC §4.4 states each:
             *
             *  - the SLICE cap is never reached. At 4.77 MHz a 16,384-cycle
             *    slice is far more than one host tick of work, so the
             *    adaptation's halving arm settles the budget near its
             *    256-cycle floor and the ceiling is not what binds. Lifting a
             *    ceiling the machine never reaches changes nothing.
             *  - the RENDER cap is 10 fps, and this tier already flushes
             *    every OTHER host tick - 9.1 Hz, SLOWER than VICE's cap, for
             *    §9.8's own reason (a full repaint here is ~300 ms). The cap
             *    is max(c64_flush_every, 2) and cannot speed anything up, so
             *    on this tier it changes nothing either.
             *
             * BOTH DO BIND ON A 286 OR A 386, which is why the item is not
             * greyed: there c64_flush_every is 1, so the cap halves the
             * drawing, and the budget does walk up to the ceiling. A message
             * that announced a speed-up the machine does not deliver is the
             * shape SPEC.md 47 exists to stop, and PERFORMANCE.md's "degrade
             * by tier" gives the port the means to answer: os88_cpu() is a
             * FACT the code can test, and c64_tier_init has read it before
             * the menu is first drawn.
             *
             * THE THREE SENTENCES BELOW ARE THIS PORT'S OWN, not VICE's, and
             * saying otherwise was drift: VICE reports warp with a LED
             * labelled `warp:` (uistatusbar.c:2607-2616) and has no status
             * message at all. The only `Warp mode` sentences in the reference
             * are a log line (autostart.c:703) and the monitor's
             * `Warp mode is on.` (mon_parse.y:290). These follow the status
             * row's own convention, the one `Paused.` / `Running.` two items
             * below already set. */
            if (!c64_warp)
                c64_say("Warp mode off.");
            else if (os88_cpu() == OS88_CPU_8086)
                c64_say("Warp mode on - no change.");
            else
                c64_say("Warp mode on.");
            return 1;
        }
        if (item == C64_I_PAUSE) {
            c64_pause = !c64_pause;
            c64_sound_stop();               /* both directions: quiet going in,
                                             * and coming out the wake re-reads
                                             * the SID and plays what the
                                             * machine actually holds (11.4) */
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
