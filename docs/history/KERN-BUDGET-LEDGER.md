# KERN_BUDGET ledger, moves 1-35 (retired)

This is the budget ledger that lived as comments beside the `KERN_BUDGET`
and `KERN_SMALL_BUDGET` equates in `kernel/kernel.asm` until 2026-10-11. It
is HISTORY: true of the trees it was written against and of no tree you can
check out today. It was moved here verbatim (comment columns stripped and
re-flowed) when the budgets were reset, because a history in code is a
history nobody can find and everybody has to scroll past.

**The current rule is not here.** It is `kernel/kernel.asm`'s `KERN_BUDGET`
block and docs/KERNEL-MEMORY.md. In short, as of the reset: kern_big
resides in **100KB** and kern_small in **64KB** at the desktop, both BOOT on
128KB, and branch and integration work may run up to 2KB over a budget
that an upstream merge or a release must then meet exactly.

What the ledger below records is how the old figures got where they were:
kern_big from a shared 72.5KB budget up to 128KB-derived (129,536 bytes),
kern_small from 94,208 to 107,520, thirty-five moves each argued at the
time. By the reset the kernels measured 97,792 and 62,976 bytes - the size
passes had taken both far below those figures, which is why they were
replaced rather than moved a thirty-sixth time.

Section numbers (SPEC.md N) and file references are as they stood when
each entry was written.

## kern_big's arm

The comment on `KERN_BUDGET equ KERN_RESIDENT_KB*1024 - KERNEL_SEG*16`.

= 129,536. DERIVED and no longer a policy figure: the kernel's span starts at KERNEL_SEG and rule 3 says where it ends, so there is nothing left to choose. The ledger below - this comment, move by move, and the only copy of it - stops at move 35 for that reason: a 36th would be a change to the RULE, not to a number.

The static half only. What an assembler cannot see is a claim made during boot and never given back, and tests/kernresident.py is that half: it walks mem_tab at a bare desktop and adds up what is PINNED.

THE THIRTY-FIFTH MOVE, 121,344 -> 122,368, TWO STEPS, ASKED FOR AND GRANTED, ATTRIBUTED TO BUGFIXES AND FINAL GROWTH.

Move 34 below ends by saying the next thing to cross a rung anywhere needs a decision here rather than finding one already taken. This is that decision, taken by the fork owner rather than by the change that ran into it, which is what SPEC.md 1 and docs/KERNEL-MEMORY.md mean by a budget move being a decision and not a build fix.

ONE STEP RESTORES WHAT MOVE 34'S MERGE SPENT and one is headroom for the rest of this cycle - the bugfixes the review of this branch turned up and whatever closing them costs. It is not funding a feature: no line of it is attributed, on purpose, because the thing being bought is the STEP itself.

WHY IT COULD NOT WAIT FOR THE FIRST OVERRUN. Move 34 reasons that an ordinary addition still fits because the image rung it crossed into has 479 bytes standing. That is true of .text and it is not true of the kernel: the binding rung is .cold, at 40,444 of 40,448. FOUR BYTES. Four bytes of cold code assemble and take the rung to zero; a fifth crosses it, spends 512, and fails this guard - measured, not predicted (`nop` x5 in a .cold section -> kernel.asm's guard 1). So the margin move 34 left standing was in the wrong rung to be spent, and the first cold-code bugfix of the cycle would have arrived as a build failure with a budget decision attached to it.

WHAT THIS DOES NOT RELIEVE, and the reason it is only half an answer: A KNOB KERNEL DOES NOT READ THIS GUARD. The %ifndef KERN_KNOB below skips it for any build with a knob on it, so a knob kernel is bound by guard 2 alone - one 64KB segment, .text + .bss, at 64,622 of 65,536 with 914 LEFT. Nobody can raise that one and this move does not touch it. The knob builds stay exactly as tight as they were, and `make test-full`'s matrix is still the only thing that builds them.

THE THIRTY-FOURTH MOVE, 119,808 -> 121,344, THREE STEPS, ALL THREE SPENT.

AND THE STANDING STEP WENT WITH THEM AT THE MERGE, which is move 33's sentence again in the other direction. On the branch this was measured on, KERN_SIZE landed at 120,832 and the 512 the thirty-third left standing was still standing - which was the whole reason a fourth step was not asked for. Merging onto elendilon added SPEC.md 12.8.4's cursor fix and the hard-disk mount work: 38 bytes of .text between them, and the image rung was 51 from crossing. So the merge crossed a rung NEITHER SIDE CROSSED, by 33 bytes, and KERN_SIZE is now 121,344 - the budget exactly, nothing spare.

That is not a build failure and it is not a reason to raise this on its own: the image rung it crossed into has 479 bytes standing, so an ordinary addition still fits. What is gone is the STEP, so the next thing that crosses a rung anywhere needs a decision here rather than finding one already taken. Left deliberately: 512 bytes of every machine's RAM is not something a merge should spend on its own account.

ATTRIBUTED TO ONE THING, measured against the build immediately before it and buildable either way (`make NOPLANE=1`):

```
  +1536 SPEC.md 5.4.1.3 - gfx_blit4's PLANAR
        ROW DECODER on VGA, which is what
        5.4's banner has called "a worthwhile
        future optimisation" since the
        primitive was written. A VGA run
        costs ~1,797 cycles WHATEVER it
        covers, so os8088.gif - whose ground
        is 63's 50% dither - is 18,978 of
        them over 51,260 pixels and SEVEN
        SECONDS of canvas on the target
        machine. The decoder is priced per
        PIXEL instead, at 106.6 cycles of
        them, and draws the identical picture
        in 1.145 s: 6.2x, and 4.9x on the odd
        x that costs it an rcr pass
        (PERFORMANCE.md Set 107).
```

```
        553 bytes of .text and 5 of .bss, which
        crossed the image rung by 507 - and
        1,024 of .lowbss for the 512-byte
        table and the four plane rows, which
        is TWO low steps because the low rung
        had 234 bytes standing. .lowbss and
        not .bss for vid_rowtab's reason
        (39.3.1): the segment guard is the one
        nobody can raise, and 65,019 of 65,536
        is what is left after this.
```

```
        What it does NOT buy is the 1bpp
        adapters, which have had 5.4.1.1's
        decoder since Set 44 and are the
        machine this project is calibrated
        against. This is the VGA half of that
        work, seven sets later.
```

THE THIRTY-THIRD MOVE, 117,248 -> 119,808, FIVE STEPS OF WHICH FOUR ARE SPENT - KERN_SIZE lands at 119,296 and the fifth is the step moves 22/23/24 say to leave, which is the thirty-first's sentence again. AT THE MERGE - ATTRIBUTED TO THREE THINGS THAT WERE MEASURED SEPARATELY AND CROSSED TOGETHER. A MERGE for the SIXTH move running, which is move 28's sentence yet again.

Each was asked for and granted on its own figure against the pre-merge base, and the three are simply additive - none of them shares a rung with another:

```
  +512  SPEC.md 6.1.11 - a run that is off
        the byte grid drawn in ONE pass
        instead of a gfx_fill and a font_str
        over it. +262 bytes of .text, and it
        crossed the rung by 77 of them.
```

```
  +1536 SPEC.md 5.9/5.9.6/11.101 - the
        compose gfx library and wm_title's
        COMPOSED BAR. 1,625 raw bytes,
        attributed per routine in 5.9.1. It
        buys a title bar with NO double-draw
        anywhere in it - docs/plans/completed/TEXT-PLAN.md
        1.1's whole point - at 36.5 ms
        against the fifteen calls' 40.8 on
        Hercules and 37.1 against 42.0 on
        CGA. On VGA it is 38.3 against 30.3,
        the one adapter where it costs time
        rather than saving it, and it ships
        anyway BECAUSE the flicker is worth
        more: over ten window raises the
        primitive bar's worst transient frame
        is 327, 141, 83, 66 and 250 pixels
        and the composed bar's is 26 every
        time (PERFORMANCE.md Set 104).
```

```
        The band itself is a 2KB HEAP CLAIM
        (5.9.2) and is not in that figure -
        but it does come off every machine's
        arena, so a machine small enough to
        refuse it gets the fifteen calls back
        and nothing else changes.
```

```
        THE SHIPPED KERNEL NO LONGER SPENDS
        THIS ARM. 5.9.6 sent the composer
        back to a knob (`make BAND=1`), so
        the bytes above are a `BAND=1`
        kernel's: the default build hands
        1,634 of them back, 1,278 out of
        .cold - the section the THIRTY-FIFTH
        move at the head of this comment was
        taken for - and KERN_SIZE follows,
        121,344 -> 120,320.
```

```
        THE BUDGET IS NOT LOWERED WITH THEM:
        this ladder records moves that were
        granted, and giving a granted move
        back is a decision of its own
        (docs/KERNEL-MEMORY.md), not the
        arithmetic of the change that freed
        the room. What it leaves is 2,048
        bytes under the guard where there
        were 1,024.
```

```
  +512  SPEC.md 39.3.1 - gfx_rowbase's row
        table going from 128 rows to 348,
        which is every row of a Hercules and
        therefore of both 1bpp adapters, so
        on the target machine it never
        misses. 696 bytes of .lowbss where it
        was 256. Sized by BUILDING it at 200,
        348 and 480 and re-profiling four
        applications at each (Set 106): the
        screen saver spends 4.69% of the
        machine in gfx_rowbase and
        gfx_rowbase_calc together on CGA and
        now spends 2.85%, Missile Command
        1.66% -> 0.75%. 200 would have cost
        the SAME step - the low rung rounds
        either to 9,728 - so the choice was
        348 or nothing, and the second step
        that would cover a VGA's 480 is
        priced there and not taken.
```

```
        kern_small does NOT get this one: its
        low rung crosses at any table above
        128 - 200 crosses it too, so a
        smaller table would rescue nothing -
        and KERN_SMALL_BUDGET is the figure
        that has to be defended. It keeps the
        128-row table behind the same
        %ifdef KERN_BIG that WM_ANIM and
        OS88_THEME use.
```

THE THIRTY-FIRST MOVE, 116,224 -> 117,248, 1KB, one step of spare - ATTRIBUTED TO THE PS/2 MOUSE BACKEND SWAP at the squash with upstream: the twenty-ninth move funded a 376-byte backend, upstream wrote the same feature at ~690 bytes (SPEC.md 9.9, the one that ships), and keeping theirs over ours crossed the image rung the union had left 398 bytes from. A MERGE for the FIFTH move running - move 28's sentence yet again. 1KB and not 512 because 512 lands at ZERO spare, which is moves 22/23/24's rule; one step is left for ordinary growth, not an invitation.

THE THIRTIETH MOVE, 115,200 -> 116,224, ASKED FOR AND GRANTED, 1KB, two steps - ATTRIBUTED TO THE ANIMATED SCREEN SAVER (SPEC.md 79) MEETING SPEC.md 18.99's DISK CLONE. kern_big's ALONE: kern_small filters SAVER.DRV out and does not get the feature.

WHAT IT IS FOR is a MERGE, which makes it the twenty-eighth move's sentence again and the FOURTH time that has been the reason - feature branches are long, each is measured against the guard alone, and the guard is crossed by the union. Measured against the merge base (109,522 bytes of kernel-full): the integration branch's eleven commits are +4,279 and the screen saver is +135. The saver ALONE would have moved nothing; what it did was spend the last of two rungs that were both nearly full - the image rung had 398 bytes left and .cold had 466 - so the union crossed BOTH at once, which is why this is 1KB and not 512.

THE ONE RECLAIM WAS SIZED AND REFUSED, and is written down so it is not re-proposed: gfx_line_fast (SPEC.md 5.6.6) is 647 bytes and every one of them is .text, so dropping it clears the IMAGE rung and leaves .cold's 466 exactly where they were. It buys one step of the two, costs a measured 5x on every line the system draws - including the saver's own geometry mode, which IS a resumable line walk - and the budget still has to move 512. Half the win for all of the cost.

THIS MOVE LANDS AT ZERO SPARE, deliberately and against moves 22/23/24's rule, because the owner asked for 1KB with a separate hunt for savings already planned (.cold's disk-clone code is where it is aimed). Zero spare is less brittle here than the rule's wording implies: KERN_SIZE only moves when a RUNG crosses, and after this merge there are 398 bytes of image-rung slack and 466 of .cold to absorb ordinary growth first. The guard fires on the next CROSSING, which is the conversation it exists to force.

THE TWENTY-NINTH MOVE, 114,176 -> 115,200, ASKED FOR AND GRANTED, 1KB, two steps - ATTRIBUTED TO THE PS/2 MOUSE. kern_big's ALONE: kern_small does not take a byte of it and does not get the feature, for the reason written beside its own figure.

WHAT IT IS FOR is the second mouse backend - an 8042 auxiliary device on IRQ12. The move was granted against a 376-byte draft; the backend that ships is SPEC.md 9.9's, written upstream in parallel and kept when the two met (this ledger keeps the move because the feature it funds is the same one): ~690 bytes of .text inside mouse.inc, probe and decode both, ending in mou_apply. It is the FIRST move at the 1KB unit since the twenty-fifth, and 1KB and not 512 is moves 22/23/24's rule again: 114,688 assembles and is ZERO spare, which hands the next byte added anywhere the failure the move exists to repair.

THE FEATURE IS RESIDENT ON A MACHINE THAT CANNOT USE IT, and that is stated here rather than discovered later. The 8088 is this project's primary target and an IBM PC/XT has an 8255 PPI - no auxiliary device, no IRQ12 - so mou_p2_init reads [cpu_tier], finds CPU_8086 and returns having done nothing, and every resident byte sits in the field 5150's RAM unreachable (SPEC.md 9.9.2's first gate). What buys them is that big is ALSO the 286 and 386 build, and by the time those were the machine on the desk the PS/2 port was the standard one - a mouse on those machines is likelier to be PS/2 than serial. It is the twenty-seventh move's wake-mechanism shape (resident everywhere for a machine that is not everywhere) taken deliberately and with a user-visible feature at the end of it, rather than noticed afterwards. If this guard ever has to be DEFENDED rather than raised, this is the second thing to weigh - behind the wake mechanism, because unlike the wake mechanism something ships that uses it.

THE TWENTY-EIGHTH MOVE, 112,128 -> 114,176, ASKED FOR AND GRANTED, 2KB, three steps of headroom - ATTRIBUTED TO WINDOW SIZE ADAPTATION STANDARDIZATION. kern_small takes 512 of it, for the reason written beside its own figure.

WHAT IT IS FOR is a MERGE for the THIRD move running, and at that point it is a property of how this tree works rather than a run of bad luck: feature branches are long, they are each measured against the guard alone, and the guard is crossed by the union. This one is the extended desktop's sizing work (SPEC.md 11.100 - OSAPI_WM_PREFER, OSAPI_WM_MINSIZE and OSAPI_WM_DISPLAY, the per-slot preference offset, minimum and sized-for-adapter arrays, wm_land_fit's one landing sequence and wm_disp_now's one answer to "which display is this window's") meeting the themes and the zoom animation (SPEC.md 76, 11.99) coming the other way. Measured: .text +1,243, .bss +104 against the baseline elendilon blessed, which stood at 512 spare - so neither side crossed and the union crossed by exactly one step.

2KB AND NOT 512 IS MOVES 22/23/24'S RULE APPLIED: 112,640 assembles and is ZERO spare, which hands the next byte added anywhere the identical failure this move repairs. Three steps, one under the standard the fifth move set, so the next feature asks in the usual way rather than finding room here.

THE TWENTY-SEVENTH MOVE, 110,592 -> 112,128, ASKED FOR AND GRANTED, 1.5KB, three steps, ATTRIBUTED TO THE MERGE FROM main - the C toolchain, CWORD, RunCPM and the ten review fixes. kern_small takes 512 of it and no more; the accounts have parted and the reason is written beside its own figure.

WHAT IT IS FOR is a MERGE again, which is the second time running and worth naming as a shape rather than a coincidence: two trees that were never built together each fit their own guard, and the guard is crossed by the union. This one is main's mid-cycle work coming the other way - and most of what it put in the KERNEL is not the C toolchain at all, which is a host-side compiler and costs the kernel nothing. It is RunCPM's WAKE MECHANISM (SPEC.md 74.1): wm_wake, wm_onwake, wm_wake_disp and wm_wkh_slot, the wm_onwk side table and wm_wkq beside it, EVT_WAKE through the event queue and ui_task, and three API cells - plus osapi_file_goto_qm. The ten review fixes are the rest: mem_cp_busy's guard at four sites, mem_reloc_call banking every register, DRV_H_FSV's version test in drv_fs_call and drv_fs_has, dsk_xfer nesting [mem_pinseg], and 11.96.13.1's two-display guard.

THAT WAKE MECHANISM IS RESIDENT ON EVERY MACHINE FOR A PACKAGE THAT SHIPS ON NO FLOPPY, and it is recorded here rather than argued about: RunCPM needs the C toolchain to build and rides a disk of its own, so the 128KB machine and the field 5150 both carry the kernel half of a feature they cannot run. It is small, it is a published ABI the moment a package calls it, and no shipped app uses it yet. If the guard ever has to be defended rather than raised, this is the first thing to weigh - ahead of anything a user can see.

THE PREVIOUS MOVE'S GRANT WAS SPENT BY NETWORKING. The twenty-sixth left 1,024 spare on each build and the browser, Telnet, ETHER.DRV, the socket layer and OS88NET took it down to SIXTY-THREE BYTES of image-rung slack without ever crossing a rung - so the footprint figure never moved and the ledger never said so. That is the shape to watch: a guard reports rungs, and work can fill one almost exactly while every number in the account stays still.

THE TWENTY-SIXTH MOVE, 109,568 -> 110,592, ASKED FOR AND GRANTED, 1KB, two steps, AND THE SAME 1KB WENT TO kern_small - which is unusual and is the whole story of this one. ATTRIBUTED TO THE RAM DISK AND UI STANDARDIZATION.

WHAT IT IS FOR is a MERGE rather than a feature. Two branches spent against these guards independently - the RAM disk, its Control Panel page and SPEC.md 31.9.2's typed field on one side, SPEC.md 13.8's button interactions on the other - and neither crossed its budget alone. Merged, they landed EXACTLY on both: kern_big at 109,568 of 109,568 and kern_small at 103,936 of 103,936, zero spare on each. That builds and boots and cannot take one more byte anywhere, which is not a state to leave a tree in.

kern_small gets the same 1KB because it took the same collision: 13.8's work is kern_big's alone, but 13.10's shared scroll bar and the RAM disk's own arrival are in both, and small was the build with 21 bytes of cold-rung slack before any of it.

THE PREVIOUS MOVE'S TERMS ARE SPENT AND ITS RECORD STANDS BELOW. That grant said the work would be reconsidered rather than the figure raised again, and it was honoured: every conversion in docs/plans/completed/UIHELPERS-PLAN.md 15.4 landed inside the 1KB. This is a different ask for a different reason.

THE TWENTY-FIFTH MOVE, 108,544 -> 109,568, ASKED FOR AND GRANTED, kern_big's alone. 1KB, two steps, ATTRIBUTED TO BUTTON INTERACTIONS - SPEC.md 13.8's down state, 13.8.2's tracking edge and 13.9's one-shot timer, and the conversion of every control that still acts on the PRESS to act on the release instead (docs/plans/completed/UIHELPERS-PLAN.md 15.4).

THE GRANT'S TERMS ARE WORTH KEEPING, because they are narrower than the usual four-step standard on purpose: 1KB, and if the remaining conversion work does not fit in it, the work is reconsidered rather than the figure raised again. It lands at 1,536 spare (three steps) over the 108,032 this arrived at.

AND FROM THIS MOVE ON, THIS FAMILY OF FEATURES IS kern_big's ALONE. Everything above is inside %ifdef KERN_BIG - the three API slots are stc/ret stubs in the small build (gfx_blit1's precedent, SPEC.md 5.4.2), the three window-record words are not there at all, and WIN_SIZE is 34 against small's 28. kern_small is the guard the 128KB machine lives under and it cannot afford the niceties; it was measured AT ZERO SPARE when this split was made, which is what forced the question.

THE TWENTY-FOURTH MOVE, 107,008 -> 108,544, ASKED FOR AND GRANTED, kern_big's alone. 1.5KB, three steps, ATTRIBUTED TO THE FONT SYSTEM - SPEC.md 5.4.2's gfx_blit1 and the proportional type it exists for (6.3-6.5, 19.8), which arrived in the merge from main.

WHAT IT COST AND WHERE: the merged kernel measures 107,520, which is 512 OVER the old figure - one step, not three. The grant is deliberately larger than the overrun, so the landing is 1,024 spare (two steps) rather than the ZERO the smallest honest figure would have given; 107,520 exactly would hand the next byte added anywhere the same build failure this move repairs, which is the trap moves 22 and 23 were both called out for. It is still one step UNDER the four-step standard, so the next feature asks in the usual way rather than finding room here.

kern_small is UNTOUCHED at 103,424 and needs nothing: it carries the slot cell and a stc/ret stub instead of gfx_blit1's body (SPEC.md 5.4.2), which is main's own +10 bytes, and it was re-measured at this merge rather than assumed - the two builds have twice been discovered broken rather than reported broken, because `all` never builds the small one.

THE TWENTY-FIRST MOVE, 104,960 -> 107,008, ASKED FOR AND GRANTED, kern_big's alone - kern_small is untouched at three steps spare and needs nothing. 2KB, four steps, which is the standard headroom the fifth move settled on and which the tree had run out of: merged, the footprint sat at 104,960 of 104,960, ZERO spare, so the next byte anywhere in .text failed the build.

NAMED FOR TWO THINGS, on the grant's own terms: docs/plans/completed/DUAL-DISPLAY-VGA.md's VGA + HERCULES EXTENDED DESKTOP, and BUGFIXES.

WHAT ARRIVED AT ZERO IS WORTH THE LINE, because neither half did it alone and the attribution was measured rather than inferred - origin/elendilon built on its own is 104,448 with 512 spare and 121 bytes left in its image rung, and this branch's +162 of .text landed on that 121 and crossed it. So steps 1-3 (62 bytes) would have fitted and step 4's 100 - SPEC.md 39.20, the reboot path naming the CARD rather than asking int 10h for a mode - is what spent the last step. It is a correctness fix for a bus conflict between a VGA in mode 7 and a real Hercules, so trimming it to stay under was not the trade to make.

THREE MOVES MET IN ONE MERGE, and none of them cancels another: the eighteenth was granted for the network driver and the nineteenth and twentieth for the blit, both against the same 100,352 base and on different branches. Taking the larger of the two answers would have silently REVOKED one grant - the network driver's, whose spending is mostly still ahead of it - so the figure is the base plus all three, and the numbering below follows the integration branch's.

THE EIGHTEENTH MOVE, 100,352 -> 102,400, ASKED FOR AND GRANTED, kern_big's alone and the third of those. 2KB on the thirteenth move's terms - headroom, half a step - for SPEC.md 62's NETWORK DRIVER (docs/plans/completed/NET-PLAN.md).

WHAT IT HAS ALREADY SPENT is stage 1, which is on this branch and is 175 bytes: DV_CLASS and the per-class volume drop (18.7.3), DRVC_NET's publication slot (51.2.1), and the Control Panel window growing 140 -> 151 to hold a third driver page (31.1). That landed INSIDE the rung the merge left, which is why the guard read one step against a standard of four before this move and not because of it.

WHAT IT IS FOR is the rest: stage 2's file REDIRECTOR, estimated at ~400 bytes across twelve branch sites in the file layer, and stage 3's OSAPI_DRV_CALL - the cell that lets a PACKAGE reach a driver at all, which is what mTCP forwarding needs and which nothing else in the tree has ever wanted. On move 10's terms: granted ahead of the work, and whatever those two do not spend is handed back rather than kept. It leaves five steps, one above the standard four, and that extra step is the redirector's estimate plus the margin an estimate wants.

kern_small is deliberately NOT moved, and it needs nothing: on the merged tree stage 1 crosses no rung there - 94,208, four steps, the standard - because its 175 bytes SPEND slack the rung this merge already had open, rather than cost nothing. (Measured on the branch BEFORE the merge it did cross one, 113 -> 114 steps; that figure is a property of the tree it was taken on and not of the change, which is the whole reason a merge re-measures rather than carrying a number across.) The fifteenth move's drift says the small guard should stay the tighter of the two, and at 96,256 against 102,400 it now is by 6KB. If the redirector turns out not to fit there, the question to ask is whether a 128KB machine wants a network drive at all - not whether the guard should follow.

THE NINETEENTH AND TWENTIETH MOVES ARE ONE PIECE OF WORK: BLIT4 RENDERING SPEED.

The nineteenth, 102,400 -> 102,912, was one step and was an ASK rather than a grant: the pair decoder (SPEC.md 5.4.1.1) landed exactly ON the guard, so without it the next byte anywhere failed to build. What that step buys is 512 bytes of PAIR TABLE - a source byte is two pixels and maps to a two-bit destination pattern, so 2 x 256 entries make the inner loop a read, an xlat and a shift.

THE TWENTIETH, 102,912 -> 104,960, IS 2KB ASKED FOR AND GRANTED for the rest of that work. gfx_blit4 is the largest single drawing cost in the system and it is under Paint's canvas, Solitaire's card backs and ARTFULTYPE'S KEYSTROKE - one line of text through one blit, which the field reports as twenty characters in twenty seconds. Measured so far: 5,526 -> 2,431 -> 517 -> 259 ms on a Paint canvas, and CONSTANT in the content (PERFORMANCE.md Sets 41, 42 and 43). 148 of that 2KB went on SPEC.md 5.4.1.2's two aligned bodies, one per destination-x parity, and 417 on 11.96.11's cache band - which is not blit work, and is charged here because this is the step that was open. 1,024 left.

TWO WAYS TO HAND THE FIRST STEP BACK if the footprint is ever wanted, both costed: packing SPEC.md 5.4.1.1's gfx_pairtab0 and gfx_pairtab1 into a single table (256 bytes, two loop variants, ~30 bytes of code back), and dropping the VGA span writer (169 bytes, SPEC.md 5.4.1) at the price of its 1.95x. Neither is a rewrite. (This cited a LAST-DROP.md 3 that has not existed for some time and never carried the claim: that file's 3 was the blit decoder's hybrid, and it split into LAST-DROP-BYTES and LAST-DROP-PERF, neither of which mentions a packed table. The costing above is this comment's own.)

THE SEVENTEENTH MOVE, 98,304 -> 100,352, ASKED FOR AND GRANTED, and the first since the fourteenth to move BOTH guards: 2KB here and 2KB on kern_small, which is deliberate and is the whole argument for the raise. What it buys is WINDOW DRAWING OPTIMIZATIONS - SPEC.md 5.8's partial restore, 11.96.6's cache restoring only what the pass painted, 11.96.8's bounded edge merge, 11.90.1's opt-out fill and 11.90.2's damage rect - and those are not a feature a big machine enjoys and a small one does without. They are what makes the OS FEEL SNAPPY, and the machine that feels a 49 ms restore becoming 23 is the 4.77MHz one at the RAM floor. So kern_small cannot be the build that goes without them, and this is the direction the fifteenth move's drift does not apply in.

WHAT SPENT THE PRIOR STEP is the same work, and it is worth naming because the guard had fallen to ONE step against a standard of four: 11.96.9's fix - a partial draw may not re-bank, the field bug 11.96.6 introduced - crossed the rung the image had 15 bytes left of, so the next .text byte anywhere paid 512 whatever it was for. Granted at 2KB on the thirteenth move's terms (headroom, half a step) with the round's biggest item still to come: a raise restoring only what was COVERED (docs/plans/completed/HANDOFF-REDRAW.md item A), which is a wm_raise change and not a new mechanism, and which turns Paint's 8.7 s canvas into ~0.9 s on the case the reporter described.

THE SIXTEENTH MOVE, 96,256 -> 98,304, ASKED FOR AND GRANTED, and the second that is kern_big's alone. 2KB again, for the rest of SPEC.md 39's dual display - 39.16's union and what follows it - on the fifteenth move's terms.

WHAT SPENT THE FIFTEENTH IS WORTH RECORDING, because the two rounds landed in the same week and the arithmetic reads as one. Dual display took 39.12's context, 39.13's second card, 39.14's split, 39.15's cursor and 39.16's union; the spare it left went to SPEC.md 18.96/22.12's floppy FORMAT and 11.96.3's per-window raise cache, which are other work and are what took the guard from two steps to one. A raise is granted for a feature, so which feature spent the last one is the question the next request has to answer, and it is not always the one asking.

THE FIFTEENTH MOVE, 94,208 -> 96,256, AND THE FIRST THAT IS kern_big's ALONE. 2KB, asked for and granted, for SPEC.md 39's dual display (docs/plans/completed/DUAL-DISPLAY-PLAN.md): the estimate is 1,400-1,900 bytes and the spare had fallen to 1,024 (two steps) after the disk round, so the feature no longer fitted. Granted at 2KB on the thirteenth move's terms - headroom, half a step - and NOT at the 4KB that would pre-authorise another feature's worth.

What is new is which guard moved. Every one of the previous fourteen moved the figure the 128KB machine lives under, because there was only one. This one does not: KERN_SMALL stays at 94,208 and the raise buys room on the machine that has RAM, which is the whole reason the split exists. The fifth move's rule still stands for both - headroom for ordinary growth, not an invitation to spend it - and kern_small's is now the tighter of the two by 2KB, which is the direction it should drift from here.

## kern_small's arm

The comment on kern_small's `KERN_BUDGET equ 107520`.

the whole kernel's FOOTPRINT. Growing past this is not a build detail - see docs/KERNEL-MEMORY.md before raising it.

THE THIRTY-FOURTH MOVE'S OTHER ARM, 107,008 -> 107,520 - ONE move with big's 119,808 -> 121,344 above and not a second one, because the ledger runs one sequence across BOTH kernels (moves 26, 27 and 28 are each labelled `both`) and these two arms are one merge. Numbered separately once, which left two moves called the thirty-fourth arguing opposite things - big's "three steps, all spent" against small's "512 and not 1,024".

512 BYTES, ONE STEP, AT THE SAME MERGE - ATTRIBUTED TO THE CONTROL GLYPH (SPEC.md 25.6) MEETING THE HEAD-SWITCH BOOT FIX (18.93.1/18.93.2). Both arms funded a 512 step to 107,008 against the same 106,496, each measured alone and each fitting; the union is 107,520 and neither is the one to give up. What the glyph buys the small machine is a 12x12 control at 6.7 ms where it was 33.6, and a Sound-page press edge at 6.7 where it was 82.8 (PERFORMANCE.md Set 84) - which is not a thing to take off the 128KB machine to save it half a kilobyte.

IT LANDS AT ZERO SPARE, which is exactly what moves 22, 23 and 24 were each called out for: the next byte added to kern_small fails the build. 512 and not 1,024 because this is the figure that has to be DEFENDED and a 128KB machine's heap is 25.0 KB - the second step is a quarter of what a package has left and it has not been asked for. The next person to add to this arm is being told here that they will be having this conversation.

THE THIRTY-SECOND MOVE, 106,496 -> 107,008, ASKED FOR AND GRANTED, 512 BYTES, ONE STEP, ATTRIBUTED TO THE HEAD-SWITCH BOOT FIX - SPEC.md 18.93.1's canary and 18.93.2's per-track read, which is the last commit of this round and the one that crossed the rung. kern_big does NOT move for it: the thirty-first move left that figure a step, and the same code sits inside it.

THE NUMBERING IS THE TABLE'S, not this arm's own count - the paragraphs below run on a local sequence that has collided with itself twice, and the kern_big arm's sequence above is the one to trust.

THE OVERRUN WAS SIX BYTES and the grant is a whole rung, which is the shape moves 22 and 23 of the ledger both had before it: `.text` + `.bss` came to 55,814 against a rung top of 55,808, so the footprint stepped 106,496 -> 107,008 and `make small` stopped assembling. Trimming the six was costed and REFUSED by the owner: every candidate is a feature this round landed deliberately on the 128KB build - wm.inc's right-click side table says in its own comment that it is in kern_small on purpose, and vga12.inc's `gfx_lm_pre` is SPEC.md 5.6.4.3's correctness fix - so the six bytes would come out of shipped behaviour rather than out of slack.

IT LANDS AT ZERO SPARE, and that is the second half of the decision rather than an oversight: the rung has 506 bytes of slack inside it, so what fails next is the next CROSSING, which is the conversation this guard exists to force. The next ask is 1KB again unless somebody says otherwise.

THE TWENTY-SECOND MOVE, 105,984 -> 106,496, ASKED FOR AND GRANTED, 512 BYTES, ONE STEP, ATTRIBUTED TO DISK IMG WRITE - SPEC.md 18.99.8's `IMG` target and `Write Img...`. kern_big does NOT move for it: that figure had a step in hand and still has 512 spare after the same code, so this is the small build alone catching up to a feature both builds carry.

WHAT IT BUYS is 110 bytes, and the shape of that number is the argument. The feature's whole TRANSFER is in CLONE.DRV, which costs this figure nothing (SPEC.md 2.8); what is resident is the menu item, `fm_c_wimg`, and the dialog's completion proc - 208 bytes of `.cold` against the 98 the rung had left.

MOVING THE COMPLETION PROC INTO THE IMAGE WAS TRIED FIRST AND IS REFUSED BY THE ARCHITECTURE, which is worth writing down because it looks obvious and is not: `fm_clone_res` calls `fm_edit_end`, and fm_edit_end is what DROPS CLONE.DRV (SPEC.md 2.8.3). A body living in the image that called it would free the code it was about to return through. The split that avoids that - the image runs the verb, a resident tail runs the teardown - leaves most of the bytes resident anyway: measured at ~50 of the 110, for two new far entries and a second boundary on the one path in this feature that is already the fiddliest. The rung was crossed either way.

What WAS taken instead is the honest 30: `fm_clone_bad` is now one proc rather than two copies, and `fm_img_done`'s two halves share one repaint tail. Both builds keep it.

THE TWENTY-FIRST MOVE, 105,472 -> 105,984, ASKED FOR AND GRANTED, 512 BYTES, ONE STEP, ATTRIBUTED TO WINDOW SIZE ADAPTATION STANDARDIZATION - kern_big's twenty-eighth move, and a QUARTER of it, the account beside that figure carrying the full story.

WHAT IT REPAIRS IS NOT THIS ROUND'S WORK. Measured at the merge, this build stood at KERN_SIZE 105,472 EXACTLY - zero spare, not one byte - and the sizing work costs it nothing net: the extended desktop is kern_big's (there is one display on a 128KB machine), so what small carries is the three API cells, the per-slot preference and minimum arrays and wm_apply_size's one sequence, against an OSAPI_WM_DISPLAY that is a `mov si`/`jmp osapi_video` there rather than a body. It came out even.

The zero was SPEC.md 76.12.3's, said out loud when the theme landed - "0 spare, 0 steps... the next byte added anywhere fails that build" - and named as a decision owed rather than a build fix. This is that decision, taken at the first merge that had to assemble both kernels. One step and not two, because move 18's judgement stands below and nothing here argues with it: the 128KB machine's kernel is already too big.

THE TWENTIETH MOVE, 104,960 -> 105,472, ASKED FOR AND GRANTED, 512 BYTES, ONE STEP, ATTRIBUTED TO THE MERGE FROM main - and it is a THIRD of what kern_big took in the same round, which is the point of it. The two guards have moved together for four moves running; they part here. Small carries the same review fixes and the same wake mechanism - the API table is one table and a slot cannot exist in one build and not the other (SPEC.md 20.8 rule 4) - but it is the figure the 128KB machine lives under, so it gets what this round actually cost it and not a share of the headroom. It lands at 1,024 spare, two steps, against big's four.

THE NINETEENTH MOVE, 103,936 -> 104,960, ASKED FOR AND GRANTED, 1KB, two steps, ATTRIBUTED TO THE RAM DISK AND UI STANDARDIZATION - kern_big's twenty-sixth move, the same 1KB, the same reason, and the full account is up there beside it.

THE SHORT VERSION, because this is the guard that has to be defended: this is not a feature's raise. Two branches spent against these guards independently and neither crossed alone; merged, they landed EXACTLY on both, zero spare. What reached kern_small is not SPEC.md 13.8's button work - that is kern_big's alone - it is SPEC.md 13.10's shared scroll bar and the RAM disk's own arrival, and this build had 21 bytes of cold-rung slack before either.

THE EIGHTEENTH MOVE'S JUDGEMENT STANDS AND IS WORTH RE-READING BELOW: *this kernel is already too big and wants TRIMMING work*. That has not happened, and a second raise in two rounds is the strongest argument yet that it should - so this one is a 1KB unit rather than the four-step standard, and the next request on this guard should be met with a trim rather than a figure.

THE EIGHTEENTH MOVE, 103,424 -> 103,936, ASKED FOR AND GRANTED, AND IT IS 512 - HALF the 1KB unit the rule below sets, which is the smallest this figure has ever moved by. That is the grant's own judgement and it is worth keeping in the reader's way: *this kernel is already too big and wants TRIMMING work*, which is a future thing, and until it happens the guard is raised by the least that keeps the build alive.

WHAT IT BUYS IS NOT A FEATURE. SPEC.md 13.8/13.8.2/13.9's whole family is kern_big's (18 in docs/plans/completed/UIHELPERS-PLAN.md) and none of it is here. What is here is 26 bytes: the three API CELLS plus one shared stc/ret, which is the floor of keeping ONE SDK - a slot number that exists in one build and not another is an ABI that depends on a knob (20.8 rule 4), so kern_small carries the cells and refuses.

AND IT WAS ALREADY ON THE EDGE, measured at the branch point rather than inferred: 102,912 of 103,424 with THREE BYTES left in its image rung, so the next byte added anywhere would have crossed whatever it was for. This lands at 512 spare, one step.

**AND WHEN IT DOES GROW, IT GROWS BY 1KB - NOT BY THE 2KB kern_big MOVES BY.** A standing rule rather than a property of any one raise, and the point of the split: this is the guard the 128KB machine lives under, so it is the one that has to be defended and it should be asked for in the smallest useful unit. Two 512-byte rungs is enough room for ordinary growth to continue and not enough to pre-authorise a feature. It is also the direction the fifteenth move said this figure should drift in.

THE SEVENTEENTH MOVE, 94,208 -> 96,256, is the first this figure has taken since the two guards were split - the fifteenth and sixteenth were kern_big's alone - and the reason it moves here is stated at kern_big's copy above: what the 2KB buys is WINDOW DRAWING OPTIMIZATIONS, and the machine that feels those most is this one. A 128KB machine is a 4.77MHz machine, where a window restore going 49 ms to 23 and a white flash disappearing are the difference between an OS that feels snappy and one that does not. The fifth move's rule still binds it: headroom for ordinary growth, not an invitation.

THE TWENTY-FIRST MOVE, 96,256 -> 97,280, ASKED FOR AND GRANTED, and it is this figure's first move at the 1KB unit the rule above sets. SPEC.md 11.96.11 had landed the build EXACTLY on the old figure - 0 spare, not one byte - so the next thing added to .text or .bss anywhere would have failed to assemble on kern_small and only there. That is the guard doing its job rather than a build problem to route around, and the ask was made with what the last of the old figure bought already measured (a Paint raise 680.9 -> 451.0 ms, PERFORMANCE.md Set 46).

IT IS ALLOCATED TO WINDOW REDRAW IMPROVEMENTS, which is the seventeenth move's allocation continued rather than a new one: docs/plans/completed/HANDOFF-REDRAW.md's remaining items are 11.91's marking keyed on redrawn REGIONS rather than rects, and 11.96.11.1's general kept region. The argument is the seventeenth's and has not changed - a redraw optimisation is worth most on the SLOWEST machine, so this is not a figure that work may be kept out of - and the fifth move's rule still binds what is left over: headroom for ordinary growth, not an invitation to spend it.

**THAT TIER IS CLOSED, AND ITS NAME IS 'WINDOW REDRAW'.** It is named rather than merely described so the next reader can tell the two tiers below apart at a glance: 96,256 -> 97,280 was spent on window redraw and is done, and the tier above it is not a continuation of it. The seventeenth's allocation ran through the twenty-first and ends there.

THE TWENTY-SECOND MOVE, 97,280 -> 99,328, ASKED FOR AND GRANTED, and it is **2KB rather than the 1KB the rule above sets**. That departure is the owner's and was made explicitly; it is recorded here rather than quietly taken, because a standing rule that can be stepped over without a note is not a standing rule. The rule itself is NOT withdrawn - the next move is 1KB again unless somebody says otherwise.

WHAT IT REPAIRS FIRST: the small build had stopped assembling. Measured at the grant, KERN_SIZE is 98,304 - 1,024 OVER the old figure, so `make small` failed outright - and the tree had been in that state for some time without it being visible, because `all` never builds kern_small. 512 of that overshoot predates the drag-cache round and 512 arrived with it; neither was ever asked for. Against 99,328 the same build is 1,024 spare, two steps, which is the smallest honest landing place rather than a comfortable one.

WHAT IS IN FRONT OF THE REST is the SIZE-CHANGED NOTIFICATION and the straddle rule that goes with it - a window spanning two displays adopting the more restrictive size - which is kernel-side and lands on both guards. It has no SPEC.md number here on purpose: it is not written yet, and a forward reference to a heading that does not exist is what tools/checkdocs.py is for. On move 10's terms: granted ahead of the work, and whatever that does not spend is handed back rather than kept. The fifth move's rule binds the remainder as it binds every other: headroom for ordinary growth, not an invitation.

AND ON THE INTEGRATION BRANCH IT LANDED ON TOP OF A REMOVAL IT DID NOT KNOW ABOUT. SPEC.md 41.11 had just taken the store above 1MB out of this build - .text -1,035, .bss -124, .ovl -386, two whole rungs - so the raise was granted against a one-step figure that had already moved. Small comes out at SEVEN steps, three over the standard, which is the fifth move's "guard switched off" and owes a decision: hand a step or two back on the eleventh move's terms, or spend it on the round the raise was granted for. docs/KERNEL-MEMORY.md's "Where it goes" carries it, because a figure nobody re-asks about is exactly how the fifth move's 2,048 became 512.

THE TWENTY-THIRD MOVE, 99,328 -> 102,400, ASKED FOR AND GRANTED, 3KB, and it is ALLOCATED TO PERFORMANCE AND DISK.

IT REPAIRS THE SAME BREAKAGE THE TWENTY-SECOND DID, WHICH IS THE POINT WORTH TAKING FROM IT. `make small` had stopped assembling again: measured by bisecting the guard, KERN_SIZE is 100,864 - 1,536 over the old figure, three rungs - and once again nobody saw it arrive, because `all` still never builds kern_small and nothing else does either. Twice now this figure has been discovered broken rather than reported broken. That is not the guard failing; it is the guard being the ONLY thing watching, and being asked only when somebody happens to run the target.

WHICH IS WHY IT IS 3KB AND NOT THE 1KB FIRST ASKED FOR. 1KB - 100,352 - does not clear 100,864 at all and would have failed on the next assemble; the smallest figure that assembles is 100,864 EXACTLY, which is 0 spare and hands the next byte added anywhere the same failure. 3KB lands 1,536 spare, three steps, and keeps the whole-KB unit this figure's own rule sets. The twenty-second's 2KB landed two steps and called that the smallest honest landing place; this is that judgement applied to an overshoot half again as large.

The fifth move's rule binds the remainder exactly as before - headroom for ordinary growth, not an invitation to spend it - and the 1KB unit is NOT withdrawn by this move any more than it was by the twenty-second: the next move is 1KB again unless somebody says otherwise.

THE TWENTY-FOURTH MOVE, 102,400 -> 103,424, ASKED FOR AND GRANTED, 1KB, and it is ALLOCATED TO HEAP COMPACTION (SPEC.md 66).

It is the 1KB the rule above sets, back at that unit after the twenty-second's and twenty-third's stated departures - and this time the figure was neither discovered broken nor overshot: SPEC.md 66 landed the build on EXACTLY 102,400, 0 spare, which is the guard doing its job at the moment it is supposed to, reported rather than found. 1KB clears it and lands two steps.

WHAT IT BUYS is a compactor that slides the movable data claims down when a claim would otherwise be refused, and the park handshake (66.5) that lets it reach the claims of a package with a live worker. THE ARGUMENT FOR SPENDING IT HERE RATHER THAN ON kern_big ALONE IS THE SEVENTEENTH MOVE'S: the machine that runs out of a contiguous run first is the SMALL one, so a feature about fragmentation is worth most exactly where this guard binds. A 128KB machine has one arena and no slack in it.

The fifth move's rule binds the remainder as it binds every other - headroom for ordinary growth, not an invitation - and the next move is 1KB again.

It had moved fourteen times before that, every raise asked for and granted: 65,536 -> 71,680 for the SPEC.md 41 store and the two API surfaces that came with it (wm_geom, wm_about_set); 71,680 -> 72,704 for the driver subsystem (SPEC.md 51) and the Control Panel pages that drive it - which BUYS more than it spends, the OPL2 and Sound Blaster code it makes loadable being thousands of lines that would otherwise be resident on a machine with neither card; 72,704 -> 76,800 for SPEC.md 51.5's keyed SYSTEM.CFG, granted in ADVANCE of further work with an optimisation pass to follow, so the slack under that one was temporary rather than an invitation; and 76,800 -> here for the file manager's Cut/Copy/Paste, its recursive paste engine and the drag (SPEC.md 22.3/22.4), which overran the previous figure by 512 bytes with the drag still to come. Asked for and granted with the 4KB it costs the claim heap on every machine named up front - Paint gives up one canvas tier for it, and the 128KB RAM floor is untouched.

The fifth move is the first DOWNWARD one: 80,896 -> 74,240. Every raise above was spent and then some of it handed back by the optimisation passes that followed - the Task Manager leaving for the system disk, the Control Panel into a cold segment, the clock ladder and the glyph table out of the segment - until the kernel sat at 72,192 with 8,704 bytes of budget above it. Slack that large is not headroom, it is the guard switched off: anything short of an 8KB addition passed without a conversation, which is exactly the conversation this constant exists to force. 74,240 leaves 2,048 bytes - enough that an ordinary bug fix does not trip it, small enough that a FEATURE does. It returns no RAM and is not meant to: HEAP_SEG is KERN_END, so the heap has always started where the kernel ACTUALLY ends and never where the budget said it might - which is also why the SIXTH move below costs the machine nothing at all. The slack was never costing memory - it was costing scrutiny, and scrutiny is the only thing this constant has ever bought.

The sixth move, 74,240 -> 76,288, is that scrutiny working. Two features landed in parallel and met at the guard: SPEC.md 5.6's gfx_line, the arbitrary-angle line primitive (512 bytes, one KIMG_PARA step), and the file dialog handing an app the SIZE so a load can be refused for free (SPEC.md 38.6). Either alone fitted; together they overran by 512. Asked for and granted at 2KB rather than the 512 the merge needed, because the fifth move's stated intent was 2,048 bytes of headroom and by the time anyone measured it there were 512 - three features had spent it without the constant being revisited, which is the failure mode a stale number in docs/KERNEL-MEMORY.md let through. That doc now carries the bisect recipe instead of a figure, so the next author measures rather than trusts.

The seventh move, 76,288 -> 78,336, was asked for and granted in ADVANCE, for two plans costed together before either was written: SPEC.md 54's file type associations (docs/plans/completed/ASSOC-PLAN.md, ~1,600) and the disk path (docs/plans/completed/DISK-PERF-PLAN.md, ~200), against the 1,536 that were spare, which the two do not fit. Granted in advance is the fifth move's warning, so the terms were stated with it: the raise lands with the commit that FIRST needs it and not with the documents, and it landed here - SPEC.md 54.4's open path had taken the footprint to exactly 76,288, which passes the guard by a byte and leaves the next change nowhere to go. Two phases of the association work and the whole of the disk work were already spent under the OLD figure before this line moved.

Part of what pays for it is elsewhere: SPEC.md 18.91's batched transfer is 117 bytes that took a directory change from 12 int 13h calls to 5, and mechanism D (docs/plans/completed/DISK-PERF-PLAN.md 5.5) removes 8 of the 13 an APPS/ open still costs.

The eighth move, 78,336 -> 80,384, is the seventh's tail: the association work's own bug reports. SPEC.md 54.4.1's notice - a document whose program is not on the disk now says WHICH program, instead of reporting a Task Manager the user never asked for - needs a name built at run time, and the branding half of that fix costs nothing at all: measured, it lands at 67,016, byte for byte the size before it. The naming half is ~115 bytes and crosses a KIMG_PARA step, which is the whole 512, and the seventh move's 2,048 had 16 bytes left. Asked for and granted with the buffer work that came with it and pays part of it back in TIME rather than bytes: SPEC.md 18.92's diskette parameter table (the batching was returning the wrong head's sectors on real hardware), 18.93's batched boot read (131 int 13h calls to 16, ~31 s to 4-5 s on the target) and 18.4.2's read-side run coalescer (a 116KB load, 244 calls to 34). None of those three cost the footprint a byte - they all landed inside the image's existing 512-byte rounding.

The ninth move, 80,384 -> 82,432, is the first one bought for the OTHER guard. The file load, file save, file manager and file dialog modules went into .cold (SPEC.md 2.6), and the two rungs that swap do not round the same way: the image loses 15.3KB and the cold segment gains it, which lands two 512 steps wide. What it buys is KERN_CODE_MAX, which had 3,519 bytes spare and now has 18,811 - both measured by bisecting the guards, not modelled - against the work that is coming. The footprint ends at 80,384 with 2,048 spare, which is twice the 1,024 it started with: the five modules cost two steps between them and the raise gave four. Asked for and granted on those terms. Note what it does NOT buy: a byte of RAM back for the machine, and not a byte of footprint either. Cold code is resident and counts here exactly like .text's - moving a module cold to fix an overrun of THIS constant is a no-op that looks like a fix (docs/KERNEL-MEMORY.md).

The ninth move landed exactly on guard 5's ceiling, and for a while that made this constant unraisable: the kernel had to end below a boot sector nailed to linear 0x15000, which capped the footprint at 82,432 whatever this line said. The sector is at the top of RAM now (SPEC.md 2.7) and guard 5 is a statement about the smallest supported machine instead, 126,976 - so a tenth move is possible again, and is the same decision the nine above were. What is NOT available is a tenth move that quietly takes the slack: this figure buys scrutiny and nothing else, and 44.5KB of it would be the fifth move's mistake at five times the size.

The tenth move, 82,432 -> 86,528, is the first one taken against that room, and it is 4KB granted IN ADVANCE for incoming quality-of-life work - the same shape as moves 3 and 7, which is why the same terms come with it. The fifth move settled that 2,048 bytes is the right amount of slack: enough that an ordinary bug fix does not trip the guard, small enough that a FEATURE does. This lands at 4,608 - nine 512-byte steps - so until the work it was asked for arrives the guard is looser than the project's own standard, and what the fifth move is on record for is that slack of that size stops being scrutiny. So: spend it on what it was granted for, and if the work lands under it, hand the remainder back the way move 5 did rather than leaving it for the next author to find.

The eleventh move, 86,528 -> 86,016, is the second DOWNWARD one and it is that last sentence being honoured rather than an optimisation pass: SPEC.md 53.6.1's XMS desktop stash was REMOVED, and the step it cost goes back with it. The feature saved the desktop's four planes at the first fsx_mode call and wrote them back at exit instead of repainting - correct for the PIXELS and wrong about the DESKTOP, because a bracket takes real time and what is on screen behind it is not a constant: the menu bar's clock (which the stash path already had to redraw, the one admission that a snapshot goes stale), the Timer's window, a Bounce, any background task that paints - and the exclusive app's OWN window, whose content is usually what the bracket just spent the whole session changing. So the stash restored a photograph of a desktop that had moved on, and wm_paint_all - which asks every window what it looks like NOW - was the correct answer all along. Measured on the removal alone: 84 bytes off .text + .bss (the image rung unmoved) and 190 off .cold, whose rung falls 40 -> 39, so it is worth 84,480 -> 83,968. SPEC.md 22.24's status-line work landed in the same round and spent a step, so the tree stands at 84,480 with 1,536 spare - one step UNDER the fifth move's standard, which is the guard doing its job rather than a fault in either change. The next feature asks. What it does NOT touch is SPEC.md 41: the xm_* slots are a published package ABI (20.8 rule 4) with the Task Manager and sysbench reading them, and this was one kernel-side consumer of that store, not the store itself.

The twelfth move, 86,016 -> 90,112, is 4KB asked for and granted in ADVANCE, on the seventh move's terms and the fifth move's warning: the project is in a growth phase and the raise lands with the commit that first needs it. That commit is SPEC.md 39.11's adapter switching, which took the spare to EXACTLY ZERO - 6 bytes left in the image rung and 155 in the cold one, so the next byte added to .text anywhere failed the build. What it buys immediately is 39.11.4 (blanking the card the machine has just left, so a two-monitor 5150 does not sit with a frozen desktop on the tube nobody is using) and 31.10's hiding of a Display page that has nothing to choose between.

It is granted WITHOUT the usual "and hand back what the optimisation pass saves", because the plan for the small machine has changed shape: the 128KB floor is to be met by a SECOND BUILD of this kernel rather than by keeping one build inside a figure both machines can live with. Once that exists the guard here is kern_big's, and the one that has to be defended byte by byte is kern_small's. Until it exists this is still the only guard there is, so the fifth move's rule stands unchanged: this is headroom for ordinary growth, not an invitation to spend 4KB without a conversation.

The thirteenth move, 90,112 -> 92,160, is 2KB and the twelfth's story again: the spare hit EXACTLY ZERO, and this time from two directions at once - SPEC.md 52's hard-disk installer arriving on the integration branch, and 11.95.1's "a window that grew reveals nothing", which is 193 bytes of .text and 8 of .bss. Asked for and granted at 2KB rather than 4: the project is still in active development, so the answer is headroom, but half a step of it, which puts the guard back within reach of ordinary growth (four steps) without pre-authorising another feature's worth. The fifth move's rule stands, and so does the twelfth's note about kern_small: the day that second build exists, this figure is kern_big's and stops being the one that has to be defended.

The fourteenth move, 92,160 -> 94,208, is 2KB granted in ADVANCE for SPEC.md 18.94.2's finding: a file operation spends over half its disk TIME on work the progress widget never shows, and the reason is that the kernel optimised for SECTORS where the media charges for REVOLUTIONS. Measured over one install, the payload streams at 5.78 sectors per int 13h call and every other phase - the BPB, the FAT window, the root scan, the subdirectory walks - runs at exactly 1.00, because dsk_dirw_next hands out one LBA at a time and every caller reads it with cx = 1 into a single 512-byte buffer. The fixes are a per-volume banked BPB (so a fixed disk, which cannot be swapped, revalidates once ever) and coalescing the directory walks into runs, which needs somewhere bigger than dsk_secbuf to read into. Granted at 2KB on the thirteenth move's terms - headroom, half a step - with the batch bracket and its sector cache still to come; that one is a REFUSABLE heap claim by explicit decision, so it costs this figure nothing and a 128KB machine can still install, just slowly.

## KERN_SMALL_BUDGET, the mirror

The comment on `KERN_SMALL_BUDGET equ 107520`.

...and kern_small's, named separately so it can be REPORTED on a big build rather than only enforced on a small one: the %else arm above is not taken on a kern_big assembly, so without a name here the figure the 128KB machine lives under cannot be quoted at all without building twice and remembering, which is how a number goes stale.

It is the figure that has to be DEFENDED. kern_big has a machine with RAM behind it; this one is the 128KB floor the project was written for, and nothing may be added to kern_small without the conversation every budget move so far has had.

It last moved at the THIRTY-SECOND, 106,496 -> 107,008, 512 bytes, one step, attributed to THE HEAD-SWITCH BOOT FIX (SPEC.md 18.93.1/18.93.2) - and kern_big did not move with it. The overrun was SIX bytes past a rung top and the trim was costed and refused by the owner; the full account is beside the KERN_BUDGET figure this one mirrors.

Before that, at the TWENTY-SECOND, 105,984 -> 106,496, 512 bytes, one step, attributed to DISK IMG WRITE - and kern_big did not move with it either. The full account, including why the completion proc cannot go into CLONE.DRV to avoid it, is beside the KERN_BUDGET figure this one mirrors.

It moved at the seventeenth, 94,208 -> 96,256, and at the TWENTY-FIRST, 96,256 -> 97,280, both for the window drawing optimisations - the argument is at KERN_BUDGET above, and the short form is that a redraw optimisation is worth most on the slowest machine, so this is not a figure that work may be kept out of. The twenty-first is also the first move this figure has taken at the 1KB unit its own rule sets, kern_big having moved by 2KB throughout. **Those two together are the tier named 'WINDOW REDRAW', and it is closed** - 94,208 -> 97,280, spent.

It moved again at the TWENTY-SECOND, 97,280 -> 99,328, which is 2KB and so a stated departure from that 1KB rule rather than an application of it. The full terms are at KERN_BUDGET above; the short form is that the small build had stopped assembling - 1,024 over, invisible because `all` never builds it - and the grant repairs that and leaves two steps in front of the size-changed notification.

And again at the TWENTY-THIRD, 99,328 -> 102,400, 3KB, allocated to performance and disk; the terms are at KERN_BUDGET above. This copy of the figure was left behind by that move - a build reading 99,328 here while assembling against 102,400 - which is what the assertion below now refuses. A figure written twice needs something that says so.

## Guard 1: the knob list

From the comment on guard 1 (`KERN_SIZE > KERN_BUDGET`), where five knob kernels were exempted one at a time before `KERN_KNOB` replaced the list.

THIS USED TO BE A LIST, and the list is the argument for the rule. Five knobs were named here one at a time, each after the same afternoon: a diagnostic that would not assemble, a paragraph explaining that it is not what the guard is steering, and one more %ifdef.

```
  DISK_COUNTERS  `make field` and bench runs, 1,821 bytes of .text and
                 two image rungs once the baked typeface spent the
                 padding it used to land in
  KFZTRACE       765 bytes and one rung, which put `make KFZ=1` 512 over
                 the guard in the DEFAULT configuration - so the one
                 build that could answer the field freeze it was written
                 for was the one build that would not assemble
  SNAPAUDIT      the glyph-alignment histogram, stopped by SPEC.md 7.3's
                 lock handover - ~35 bytes every machine needs
  BOOT_PROFILE   SPEC.md 15.5's phase table, ~1KB, stopped by SPEC.md
                 18.99's disk clone growing the resident image by a rung
  DIRTYRAM       the sharpest of them: FIVE BYTES under, and the five
                 that tipped it were 18.91.1's cylinder bound and 9.4.4's
                 early close - 2.8 seconds of a 12-second boot
```

None of those is any other's problem and the guard cannot say so: it sees one number. The sixth was SPEC.md 12.8.4's cursor fix, where the build that broke was not the instrument at all - `BOOTMARK=1` and `BOOTHALT=20` are +519 and +523 of .text, so a 141-byte fix to the SHIPPED kernel put those two 512 over and the fix had to be cut to 38. That is the guard steering the wrong thing, and it is where the rule replaced the list.

## Guard 5: when it bound knob builds

From the comment on guard 5 (`MIN_RAM_KB`).

This paragraph used to say the opposite - that guard 5 binds a knob build because it is "about a machine rather than a policy" - and the list of five knobs above is what that cost: five afternoons, each ending in a diagnostic that would not assemble. The machine it is about is the one we SHIP to.

## MIN_RAM_KB: one number became two, and then one again

The comment on `MIN_RAM_KB` before the 2026-10-11 reset, when kern_big booted on 196KB. At the reset both kernels went back to booting on 128KB, which kern_big at 97,792 bytes fits with ~24KB to spare.

MIN_RAM_KB - THE MACHINE THIS CONFIGURATION BOOTS ON, and it is not the same machine for both kernels any more (docs/KERNEL-MEMORY.md). The rule set is three sentences and each names a different quantity:

```
  1. kern_small BOOTS on 128KB.
  2. kern_big BOOTS on 196KB.
  3. kern_big fully RESIDES in 128KB once it is at the desktop - every
     non-purgeable byte it has claimed by then, with no drivers loaded and
     no XMS, on VGA.
```

1 and 2 are guard 5 below, which asks whether stage 1 can put its sector and its stack at the top of that machine and still read the kernel AND its stage-2 blob in underneath. 3 is guard 1's KERN_BUDGET, derived from 128KB rather than chosen, plus tests/kernresident.py for the part an assembler cannot see - a claim made at boot and never given back.

**THE TWO WERE ONE NUMBER AND THAT IS WHAT WENT WRONG.** Boot-time reach and resident footprint are different questions, and with one constant answering both, the BOOT ceiling silently became the binding one: measured at the commit this changed, the shipped kernel had 1,670 bytes before guard 5 and 3,584 before KERN_BUDGET, so the budget everything steers by could not be reached and a recovery of ~5KB showed up as slack under a guard that was not the ceiling. kernsize now prints both.
