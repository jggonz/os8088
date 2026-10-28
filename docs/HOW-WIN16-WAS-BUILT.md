# How WIN16 was built

This is the development diary of WIN16, os8088's layer for running original
Windows 3.x programs (SPEC.md §124). It records what was actually found,
decided, built and checked, in the order it happened, for later reuse. The
delivery plan, [WIN16-PLAN.md](plans/WIN16-PLAN.md), keeps planned work
separate from completed work; nothing here is a promise.

## 2026-10-28 — the plan, and the three things that were already true

**The ask.** Run the original Windows 3.x Minesweeper and Calculator on
os8088, through a "Wine"-like layer that is loaded on demand. Use Wine and
winevdm as sources, and build it in user stories.

**What was read.** Three sources fed the plan:

- **os8088's own surface:** docs/INDEX.md, the package model (SPEC.md §20),
  parts (§20.12), workers (§20.6), the association table (§54) and DOS.O88
  (§96, `docs/plans/DOS-EXEC-PLAN.md`).
- **Wine** at `0738d528`: its NE loader `dlls/krnl386.exe16/ne_segment.c`
  and `ne_module.c`, its four `.spec` ordinal tables, and `tools/winedump/ne.c`.
- **winevdm** at `b88d7f87`: its architecture and its issues on these two
  programs.

**Three things were already true, and they set the design.**

1. **The kernel already runs a task safely on a foreign stack.** A Windows
   program runs with SS = its own data segment, and every os8088 task shares
   `SS = LOW_SEG`, with a switch that saves only SP. That looked like a kernel
   change until `kernel/sched.inc` was read: `sch_switch` already declines to
   switch when SS is not `LOW_SEG` (line 2389). It learned that for ROMs that
   hop stacks inside `int 16h`. The mouse ISR banks SS:SP for the same reason
   (`kernel/mouse.inc` line 396). So a Windows program on its own stack is
   simply not pre-empted, which is Windows 3.x's cooperative model, at zero
   kernel bytes.

2. **A worker task is the wrong home.** It may not touch a file, and a paint
   delivered to it would be drawn after `W_PAINT` had returned. A coroutine on
   the UI task, resumed from each callback and suspended in `GetMessage`, is
   both simpler and how Windows itself scheduled programs.

3. **DOS.O88 already owns `.EXE` and already reads the whole file.** The
   launch is a four-byte sniff at offset 3Ch and a forward, not a new
   association.

**Two things are not known, and the first story exists to measure them.**

- **The CPU.** Windows 3.1 had no real mode, and Microsoft advised its
  developers to build for the 286. So the 3.1 binaries very likely contain
  286 instructions, which an 8088 silently mis-executes.
- **Floating point.** Calculator very likely uses the MS C x87 emulator
  sequences.

Story S1's census of the owner's own copies decides both, and decisions D1
and D5 wait for its numbers.

**What was tested.** Nothing ran. This entry is the plan, the SPEC.md §124
skeleton, and a draft PR.
