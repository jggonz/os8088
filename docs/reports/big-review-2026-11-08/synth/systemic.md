# The systemic view: what to approach differently, and what the period systems did better

Kernel review of 2026-11-08, synthesis. Inputs: the 25 area and lens reports
under `areas/` with their verified data under `data/`, and the six gap
reports (`gap-context-lock-model`, `gap-fault-containment-error-model`,
`gap-flagday-reconcile-abilist`, `gap-kernel-matrix-near-model`,
`gap-mouse-cursor-half`, `gap-os88ui-kernel-copy`). The primary lenses were
histmem, histsys, abi, engsys and bytes, but every area's Rethinks and Prior
art section was read and is cited below.

This is a dated measurement report. It describes one tree and goes stale on
purpose; SPEC.md and the code remain the authority.

## How to read this

- **Phases.** *now* = no ABI break (an appended cell or a contract
  clarification is a lock update with no bump). *flag day* = batched with the
  next `PKG_FMT`/`DRV_VER` break, the one `synth/phase1.md` and
  `gap-flagday-reconcile-abilist.md` (gfr) already plan as `PKG_FMT` 10 -> 13,
  `DRV_VER` 12 -> 14. *record* = written down so it is not re-derived, not
  proposed.
- **Bytes** are signed and say where they land: *resident* (`.text`, `.bss`
  and `.cold`, which IS resident), *.ovl* / *blob* (boot-transient), *module*
  (a `.DRV` image, paid only while loaded), *.mega* (above 1MB on kern_mega
  after the switch, so not conventional), *package* (a package image).
  Figures are the reviewers', ESTIMATED unless marked MEASURED, and per the
  CLAUDE.md banner they are bytes, not rungs.
- **Confidence.** Every finding cited was verified (*confirmed* or
  *corrected*, with the correction folded in) unless it is marked
  **(unchecked)**, which means the verifier did not re-derive it. The two
  refuted findings (isr-5, wm-20) are dropped.
- **IDs.** `area-N` are the data files' finding ids; `ctx-`, `gfce-`, `gfr-`,
  `gkm-` and `mc-` (mouse-cursor gap, written `mcur-` here to keep it apart
  from megacore's `mc-`) are the gap reports'. `WPn` points at
  `synth/phase1.md`'s work packages, where the now-phase items are already
  costed and ordered.

## The one pattern under all twelve themes

Read across the 25 reports, os8088's defects have one shape, named four
different ways by four reviewers:

- histmem: "the defects are mostly where one mechanism's discipline stops
  short of a neighbour";
- isr: "a precondition that the kernel's own ISRs meet and the driver- and
  package-facing entry points do not";
- histsys: "mechanisms running on the WRONG CONTEXT";
- engsys / abi-8 / dw-4: "rules about which task or context may call a cell
  are prose", and SPEC.md calls one of them "an author rule with no
  enforcement behind it".

Each problem in this kernel is solved **once, very well, at the point of the
field report that found it**: the BPB's 17-rule table, the stack-overflow
panel, `gfx_lock`'s owner-stamped fair semaphore, `CURBAR_ON`'s task-scoped
flag, the fsck commit order. The rule is then carried to the next site by
prose and by care. The period systems that aged well differ in exactly one
way: they turned the rule into a **mechanism at a single choke point**.
Windows 3.1 validated at the USER/GDI door; OS/2 1.x put every IRQ through
`DevHlp_SetIRQ`; the Mac published which routines "may move memory" and
which may run at interrupt time; DOS 3 gave every error a class and an
action. os8088 already has the choke points (`ui_bill`, the N-stub of the
file cells, `wm_pkgcall`, `ld_check_hdr`, `sch_diepanel`, `mem_reloc_call`)
and already has the tooling to make prose into gates (636 assembly-time
assertions, `t_api_abi`, `abilock`, a dozen former prose rules now ratchets).
**Most of what follows is "move the rule to the funnel, then gate it"** -
cheap in bytes and, as the period systems show, the difference between a
discipline and a property.

The second pattern is the honest counterweight: on almost every axis where a
period system had a *mechanism* os8088 lacks, os8088 has a *cheaper* or *more
honest* one, and several of the period mechanisms are wrong for a 4.77MHz,
one-floppy, 128KB machine. "What we would NOT copy" is at the end, and it is
not short.

## Summary

| # | theme | the change of approach | now (kern_big resident) | flag day | top 10 |
|---|---|---|---|---|---|
| 1 | Execution contexts and locks | teardown and paint run only on the UI task; locks get owners-died and written nesting | ~+2..+27 | deferred damage, opt-in (~+80..+120) | yes |
| 2 | Interrupts and the IF=0 budget | one owner for PIC/IVT/IMR; a written latency budget and a ratchet | ~+77..+91 (+100 for the full cell, owner's call) | IRQ cell mandatory (L3); ISR-reachable claim flag | yes |
| 3 | Waiting and I/O | block on events, not on time; a disk wait that freezes nothing; a file layer that uses what it knows | ~+60..+130 (most optional) | caller-held write cursor; completion word in the BLK/FS block (recorded) | yes |
| 4 | Faults and the error model | one fatal path for every CPU fault; one refusal shape; error CLASS not number | ~+35..+45, +12 .ovl | AX=1 "not here" everywhere (+5) | yes |
| 5 | Validation at the boundary | hostility owned by the reader; fences at funnels; a checked build | ~+65..+90 | none needed | yes |
| 6 | The memory model | keep claims, not handles; add the package-purgeable tier and a sub-allocator | ~+50..+95 kernel; -0.8..-1.5KB per parts package instance | C1 (-45) | yes |
| 7 | Residency and lifetime | a third lifetime (discardable), machine-shaped residency, lifetimes by count | ~-3,400..-3,500 (WP2) plus -300..-380 (KBMOUSE) | - | yes |
| 8 | Dispatch and the call surface | an index door for rare cells; a window class in the package | ~-140 (rehearsal on cw_ shims) | ~-1,078 big / -685 small / -957 mega (whole phase 2) | yes |
| 9 | Versioning and capability | a runtime API level now, in bytes that are already free | ~+8 .cold | dissolves (gfr-5) | yes |
| 10 | The kernel-driver contract | read tables in place; request block only for BLK/FS; slots not classes | ~-90..-140 | request block, typed slots, discovery (recorded) | |
| 11 | The kernel matrix | kernels fork, packages must not; arm honesty by gate | ~+6..+12 (+~40 if kern_emu retires) | L3 (staged) | |
| 12 | The contract and documentation system | one source per fact, the rest generated | 0 | 0 | yes |

---

## 1. Execution contexts and locks: who runs what, on which stack, under which lock

**Where os8088 stands.** The kernel has three real locks - `gfx_lock` (a
four-byte RAM semaphore with owner, waiter flag and a one-debt fairness rule),
`sch_lock` (a counted pre-emption disable) and IF=0 - plus at least eight
ad-hoc context bytes (`cur_inxfer`, `cur_barok`, `mem_cp_busy`,
`wm_pkgd`/`wm_tcnt`, `sch_rbusy`, `sch_chbusy`, `fsx_task`, `dsk_batch`), each
argued correctly at its point of use (gap-context-lock-model Verdict). What is
missing is the model:

- **Work runs on whatever task caused it.** A dying worker runs
  `inst_task_die` on its own 128/192-byte slice: `gfx_lock`, `wm_destroy`,
  `wm_paint_dmg` and other packages' `W_PAINT`, then `desk_drop_x`
  (histsys-1, ctx-1; `kernel/instance.inc:1921-1947`, `:2196-2210`;
  `kernel/wm.inc:3071-3170`). The same decision produces hm-3 (DESK.DRV
  dropped on the worker while the UI task may be inside `DKD_HOVER`,
  `kernel/ui.inc:1006`), mem-10 (any `mod_drop` on a worker,
  `kernel/mod.inc:880-894`), ui-2 (content-click, right-click and `W_ONDRAG`
  read the handler before the lock, SPEC.md 13.9.2's race unfixed elsewhere,
  `kernel/ui.inc:579,592,1569`) and ctx-2 (a worker that dies holding the
  gfx lock deadlocks the desktop on kern_big and kern_small). The UI task
  already drains six kinds of posted work (`inst_launch`, `wm_clreq`,
  `mega_fkq`, the posted compaction, `ui_rebootq`, `wm_tarm`); **teardown is
  the one that is not posted**.
- **Repaint is synchronous and nested** inside whoever caused the damage
  (histsys-10, wm-R2 (unchecked); `kernel/wm.inc:3162-3168`, `:389-394`).
- **`sch_lock` is a big kernel lock with six meanings** (int 13h, the heap,
  the FAT window, PCM clips, hibernate, kern_mega pool work) and about forty
  takers, where SPEC.md 7 says "exactly two" (ctx-3, sched-3). Because it is
  one byte, its expensive meaning cannot be released (theme 3).
- **Lock order is obeyed and unwritten**: "never block under `sch_lock`"
  holds everywhere and is written nowhere (ctx-8); yield, sleep and
  `gfx_lock` silently degrade under it (sched-3,
  `kernel/sched.inc:1881-1882`); exiting with it raised kills pre-emption for
  the session (sched-18).
- **Identity decisions read package bytes**: `ui_dispatch` decides "is this
  Locator" by reading `AM_ONCMD` in a foreign segment without the lock, when
  `[menu_win]` already says (ui-3, `kernel/ui.inc:2512-2536`).

**What the period systems did, at the model level.**
- *Win16 and Mac System 5-7*: a window's paint and destruction ran only on
  its owner's event loop. Windows kept a per-window update region and
  synthesised `WM_PAINT` only when the queue was empty; the Mac's Window
  Manager accumulated `updateRgn` and the Event Manager delivered `updateEvt`
  last. No application's paint ever ran inside another's handler.
- *OS/2 1.x*: RAM semaphores (`DosSemRequest`/`DosSemClear`) and system
  semaphores that report `ERROR_SEM_OWNER_DIED`; a per-device semaphore, not a
  global pre-emption disable, around the disk.
- *AmigaOS*: two nestable counters with a written rule - `Forbid()` (no task
  switch) for anything long, `Disable()` (no interrupts) only for a few
  instructions - and a defined meaning for `Wait()` inside `Forbid()`.
- *Inside Macintosh*: a per-routine "may move or purge memory" column and an
  interrupt-time-callable table. The *list* was the contract.

**What os8088 already does better.** `gfx_lock` *is* an OS/2 RAM semaphore,
with a fairness rule OS/2's lacked: it took press-to-menu latency from
1.4-14.7 s to 37-70 ms (SPEC.md 7.3). The nest stack is task-aware
(`wm_nest_pushd`, `kernel/wm.inc:11735-11754`). Only the UI task compacts or
sheds on kern_big, stated and enforced in code (`kernel/memory.inc:955-965`).
The deferred close (SPEC.md 75.2) and the self-healing posted-work byte are
cleaner than Win16's flags. kern_mega's `mega_fkill` already does owner-died
release and posted teardown (`kernel/mega.inc:2694-2701`, `:2659`), so the
model fits this kernel.

**Recommendation.**
- **Now - teardown on the UI task** (ctx-1 / histsys-1): `inst_task_die`
  marks the record task-less, posts an `inst_reap` bit and `task_exit`s; the
  UI task runs `app_close_win`'s task-less arm on its 1KB stack. **+12..+30
  resident.** Once it lands, every damage source in the machine is on the UI
  task by construction, and hm-3 and mem-10's worker-drop arm stop existing
  (their own fixes, +8..12 and +12, are then unnecessary). Add a stkwater
  reading of Bounce (`OS88_STACK_128`) closed over a second window. phase1
  routes this to the bug pass (BR-4).
- **Now - owner-died release** (ctx-2): +9 resident, -13 .mega.
- **Now - liveness at the funnel** (ui-2): test `W_FLAGS` once at `ui_bill`'s
  head and drop `.tp_locked`'s own re-test: net 0 kern_big, +5 kern_small.
  The general rule: every future callback kind inherits the check.
- **Now - identity from kernel state** (ui-3): `cmp word [menu_win],0`:
  **-19 resident**, both kernels.
- **Now - the context model as tags and a gate** (abi-8, ctx-7, isr-16
  (unchecked), dw-4): a machine-readable `; @ctx ui|any|worker|isr|hook|lock`
  in each SDK contract (fingerprinted by abilock already), a host gate over
  `stkdepth`'s call graph that no worker root or rate hook reaches a cell its
  tag forbids, and the interrupt-callable list generated into
  `docs/INDEX.md`. **0 shipped.** dw-4's runtime refusal (`cmp byte
  [sch_cur],0` in the file cells' N-stub, **+9 resident**) is the one tag
  worth enforcing in the binary, after DOS.O88's worker is shown to call files
  on task 0.
- **Now - write the lock order** (ctx-8, sched-3, sched-18): `sch_lock` is a
  nestable Forbid; nothing inside may yield, sleep or take `gfx_lock`; no
  task exits holding it. 0 bytes, or +7 for sched-3's `task_sleep` guard.
- **Flag day or opt-in - deferred damage** (histsys-10, ctx-9, wm-R2
  (unchecked)): a short global rect list painted at the top of the UI pass.
  ctx-9 reconciles it as a **coalescing and stack-depth optimisation, not a
  safety one** once ctx-1 lands, so it belongs either at the flag day or as
  OS/2 PM's `CS_SYNCPAINT` shape inverted: default synchronous, a window opts
  into deferred. ~+80..+120. **Record** until a measurement shows double
  paints that matter.
- **Record** (ctx R1, R2): one UI work queue in `evq` instead of seven post
  bytes (the ring is lossy by design, so a teardown would need `EVT_WAKE`'s
  exemption; the masks are cheaper); `sch_lock` split by meaning (buys little
  beyond theme 3's int 13h byte).

---

## 2. Interrupts: one owner for the PIC and the IVT, and a written IF=0 budget

**Where os8088 stands.** No single owner controls the IRQ lines, the IVT and
the IMR: six owners write them their own way, and the kernel's restores do
not check what they overwrite (isr-13; `sched_stop` `movsw`s the saved vector
back unconditionally, `kernel/sched.inc:639-664`). A driver image is freed
with no check that its vectors were unhooked (drivers-6,
`kernel/driver.inc:4232-4351`). USBMOUSE re-derived the DOS TSR rule "unhook
only if still yours" on its own. No interrupt-latency budget exists anywhere
(isr-14: a grep finds only one snapshot sentence), and no suite row measures
IF=0 duration (ctx-4). The longest IF=0 windows are no longer the one SPEC
names:

- `mem_regrow`'s path-3 copy runs the whole block at IF=0, where the
  compactor copies the same bytes at IF=1 under `sch_lock` (hm-1 / mem-2,
  `kernel/memory.inc:4559-4664`);
- the heap's O(n^2) planning walks run at IF=0 (~7 ms predicted) where
  `sch_lock` gives the same exclusion (hm-5, `:4927-4928`, `:5753`);
- the cursor is drawn inside the serial ISR at IF=0: about 2.4-3.2 ms
  measured, and on kern_big VGA 21,450 cycles (~4.5 ms) a packet through the
  generic rect machinery, still erase-then-draw (isr-7, mcur-3;
  PERFORMANCE.md's `cur_move` rows);
- kern_mega moves 64KB compaction pieces at IF=0, ~16 ms on the Victory 286,
  already in KERN-MEGA-PLAN 14.48.3 (mc-5), and walks the LDT linearly under
  `cli` per block freed (mc-4);
- `task_spawn`'s IF=0 window grew to include the slice fill (~676 us at 384
  bytes, sched summary).

ISR-facing preconditions are met by the kernel's own ISRs and not by the
driver- and package-facing doors: SS = `LOW_SEG` (ctx-5; the USB-mouse feed
swaps SP but not SS), DF = 0 at the IRQ0 callback boundary, the private mouse
stack (isr summary). And the tick can park a ROM int 09h with IRQ1 still in
service on the int 15h and kern_mega paths SPEC.md 9.8.3 left unmasked
(input-2), which under int 13h's `sch_lock` becomes a predicted floppy
timeout.

**What the period systems did.**
- *OS/2 1.x*: the kernel owns the 8259. Drivers call `DevHlp_SetIRQ` /
  `UnSetIRQ` / `EOI`; the kernel refuses a line already held unless shared;
  handlers run after the kernel's EOI with interrupts on; `ArmCtxHook` defers
  work to task time.
- *Windows 3.x enhanced mode*: VPICD virtualises the PIC; a short published
  list of interrupt-safe calls (`PostMessage`), and interrupt-touched memory
  must be FIXED or page-locked.
- *Mac System 1-7*: the cursor drawn by the VBL task at 68000 level 1, beneath
  the SCC serial chip at level 4, so drawing could never cost a serial byte;
  the Deferred Task Manager for work that must leave interrupt level.
- *AmigaOS*: `AddIntServer` / `SetIntVector` with prioritised server chains,
  and the `Forbid` versus `Disable` rule.

**What os8088 already does better.** Private ISR stacks that set SS as well as
SP on a 4.77MHz part; wake coalescing with lost-record recovery; the mouse
ISR's `cur_inxfer` reasoning about when it may draw is explicit and gated,
where the Mac's cursor corrupted drawing in known cases; `mem_rr_ivt` makes a
moved driver's vectors follow it, which Windows never did; the timed ROM
chain survives stack-hopping BIOSes.

**Recommendation.**
- **Now - write the budget and gate it** (isr-14, ctx-4): a per-machine-class
  IF=0 bound (ctx-4: ~8 ms is achievable on the XT for the 1,200-baud mouse;
  SERIAL.DRV's 9,600-baud ~1 ms is not while the arrow is drawn in the ISR)
  plus `tests/unit/t_ifwin.py` and `tests/ifwin.txt` registering every
  `pushf/cli` window over N instructions with its reason; the
  measurement lives in the final pass. **0 shipped.** WP13.
- **Now - long copies at IF=1 under `sch_lock`** (hm-1/mem-2: +10..16 .cold,
  keeping IF=0 for a `MEM_K_DRV` record exactly as the compactor does; hm-5:
  0..+6 .cold).
- **Now - one generic in-service rule** (input-2): `sch_isr` refuses a switch
  while any IRQ other than IRQ0 is in service (OCW3 0Bh, the sequence
  `khb_*` already uses at `kernel/sched.inc:1947-1953`). **+14..16 resident**,
  ~40 cycles on the switch path only. It replaces per-path masking with the
  rule AmigaOS, OS/2 and well-behaved TSRs obeyed: never reschedule out of an
  un-EOI'd hardware interrupt.
- **Now - the ISR-door preconditions at the door** (ctx-5: +3; the USB feed's
  SS; `cld` at the IRQ0 callback boundary).
- **Now - the IMR shadow first** (isr-13: ~+25), then **an appended
  `OSAPI_IRQ` cell** with LIFO-checked restore (~+100 resident net on
  kern_big, -60 SOUND.DRV; the owner's call - phase1 leaves it out of the
  totals), and drivers-6's IVT scan at release (+25 .cold) in the meantime.
- **Now, after measuring - the cursor after the EOI, coalesced** (isr-7 R,
  mcur-R3): decode at IF=0, EOI, raise `sch_lock` and a busy byte, `sti`,
  redraw while the position moved. +30..40 resident. This recovers the Mac's
  ordering for the one ISR whose body is long. mcur-R2's off-screen latch
  save-under (~0 resident net, +50 `.vga`, ~6,500 cycles against 21,450
  PREDICTED) is the VGA half, recorded.
- **Flag day** - make the IRQ cell mandatory (ABI-BREAK-LIST L3), and adopt
  the Windows `GlobalFix` / Mac `HLock` contract as a per-claim
  "ISR-reachable" flag so every other block moves with IF on (mc-5, +20 .mega
  once the contract exists).
- **Record** - isr-15's BIOS mutex in place of `sch_lock` across int 13h
  (PC/GEOS `SysLockBIOS`), +40..80, only if SB underruns during disk reads
  are reported; the Mac Deferred Task Manager as a queue the scheduler would
  own (sched rethinks).

---

## 3. Waiting and I/O: block on events, a disk wait that freezes nothing, a file layer that uses what it knows

**Where os8088 stands.** A task can block only on time (sched-15). Every
other wait is a sleep-poll or a yield-spin: SOUND.DRV's refill sleeps one
tick and polls (`drivers/sound/sb.inc:2539-2542`), `gfx_lock`'s waiters
`sti / task_yield / retry` (sched-17, `kernel/vga12.inc:5082-5132`), and
`sch_wake_ui` is a one-task special case of a missing per-task wake with a
hand-made pending-wake guard (`kernel/ui.inc:1169-1185`). The UI task has no
scheduling preference (histsys-4 lowered to LOW, sched-16: SPEC.md 7.3 found
the quantum half inside the noise with one worker). **Every int 13h freezes
every task for ~400 ms**, because `dsk_xfer` raises `sch_lock` across the
BIOS call (histsys-5, ctx-3, isr-15; `kernel/disk.inc:1462`). The file layer
is stateless by name, with two cursor shapes of opposite buffer convention
(histsys-13 (unchecked)), `OSAPI_FILE_FIND` quadratic although `dsk_dscan`
already resumes from a raw slot (dr-5), one kernel-wide `WRITE_SEQ` stream
record (dw-22), the free-cluster count recomputed - and on FAT16 re-read - on
every ask (histsys-3), and the whole-file write model's foreknowledge of the
size unused (dw-13, dw-14).

**What the period systems did.**
- *AmigaOS Exec*: 32 signal bits per task, `Signal()`/`Wait()`, the cheapest
  general wait there is; `DoIO`/`SendIO` with a reply port for async device
  I/O.
- *Unix System V*: `sleep(chan)`/`wakeup(chan)` on any address.
- *OS/2 1.x*: `DosSemWait`, `ProcBlock`/`ProcRun` for drivers, priority
  classes with dynamic foreground and I/O-completion boosts; it hooked the AT
  BIOS's int 15h AH=90h/91h device-busy calls so a thread blocked on the
  floppy while others ran (so did NetWare and WIN386).
- *Mac System 5-7*: every File Manager call took a ParamBlock with
  `ioResult` and `ioCompletion`, which is how the Mac got async I/O; HFS
  `PBAllocContig` and clumps allocated contiguously from a known size.
- *DOS*: FindFirst/FindNext keeps the search state in the caller's DTA; the
  DPB carries a free-cluster count (0xFFFF = unknown) maintained by the
  allocator; handles (SFT) hold an O(1) sequential position.

**What os8088 already does better.** The idle model beats every peer: the UI
task blocks with the lost wakeup closed in one `cli` span and the machine is
~97% halted at idle, where Win3.x spun (int 2Fh 1680h only under WIN386) and
System 6's `SystemTask` spun. Stateless files by name leak nothing when a
program dies, which DOS handles did. The motor-timeout media test is a
physical fact where DOS's XT path guessed with a 2-second rule. Shadowed
replace and the ordered commit mean a power cut never costs the user's bytes,
which DOS cannot claim (diskwrite summary). Cylinder-run batching made 140
int 13h calls against DOS's 325 on the same workload.

**Recommendation.**
- **Now - one wait primitive** (sched-15): `task_wake(AL=slot)` with a sticky
  wake-pending bit in `T_FLAGS` bit 1, tested-and-cleared by `task_sleep`
  under its own `cli`; ISR-safe, no record growth, plus a rare cell appended.
  **+24..30 resident, +6 cell.** It enables IRQ-driven SB refill, NIC-driven
  ETHER service, and sched-17's blocking `gfx_lock` waiters (+12, record
  until measured). The sched reviewer calls this "the one change of model
  worth making"; it is Amiga's one signal bit with the slot as SysV's channel.
- **Now, after one measurement - release the disk freeze** (histsys-5,
  ctx-3): release `sch_lock` across the UI task's BIOS floppy int 13h only
  (drivers, hibernate, kern_mega and the debug door keep it), on an AT hook
  int 15h AH=90h to `task_sleep` the UI task until AH=91h. ctx-3 shows it
  needs three changes, the first fixing a real bug the release would create:
  the mouse ISR's "held by us" arm reads `[sch_cur]` at interrupt time
  (`kernel/mouse.inc:4226-4271`), so `cur_inxfer` must become task-scoped the
  way `CURBAR_ON` already is. ~+16 (the owner byte) to +40, +~25 for the AT
  hook. Take one MartyPC number first (a worker's fps during a 16KB read).
- **Now, measured first - a one-shot UI boost** (sched-16 +22 resident, +1
  .bss; histsys-4's ~+10 form): the cheap form of OS/2's and GEOS's
  interactive priority. A full priority field is not recommended (14 slots;
  a byte per task, a second scan key and an inversion story).
- **Now - the file layer uses what it knows**: cache `[dsk_nfree]` the way
  the DPB did (histsys-3, ~+20 resident); `FILE_FIND_SEQ` over the existing
  resume (dr-5, +45 .cold, +6 cell, DOS's DTA model); contiguous allocation
  from the known size (dw-14, +70 .cold, Mac `PBAllocContig`); lazily flush
  only the droppable frees at the `gfx_unlock` gate held streams already use
  (dw-15, +15 .cold - safer than SmartDrive's write-behind because only frees
  wait); report media-changed 06h as a fact, not a retry (dr-4, +16 .cold);
  folders grow, the one DOS 2.0 FAT behaviour missing (dw-12, ~+90, the
  first item to drop if phase 1 must shrink). WP12, WP16.
- **Flag day** - the write stream takes a caller-held cursor like
  `READ_SEQ`'s (dw-22, 0..-20; gfr's C9, recorded), and histsys-8's request
  block for `DSV_BLK`/`FSV_*` carries a completion word = 0, leaving the
  Mac's async door open for free (theme 10; recorded as D6).
- **Record** - dr-20 (unchecked): ask for the volume by name instead of
  silently dropping owed state (Mac offline VCB "Please insert the disk",
  C64 GEOS disk ID; +40); dr-19 (unchecked): one DOS-style buffer pool in
  place of three caches (maybe -300 .cold, large rework, and it would cost
  the FAT window's guarantee that a straddling FAT12 entry is resident).

---

## 4. Faults and the error model: one fatal path, one refusal shape, error classes

**Where os8088 stands.** One fatal panel exists, for one cause: stack
overflow. It is very well built - SP moves first, no lock, `font_run`, names
the culprit, 174 bytes `.text` + 4 `.bss` on kern_big (gfce strength 5,
`kernel/sched.inc:2635-2724`). **There is no int 0 handler on kern_big or
kern_small** (histsys-2 corrected, isr-8, gfce-1): on the IBM 5150/5160 ROM
the dummy vector masks the 8259, so one divide overflow is a dead machine
(`kernel/wm.inc:8280-8292`, `tests/int0sweep.py`); on GLaBIOS it continues
with garbage; on every 286+ it re-executes the `div` forever. And the review's
top findings keep producing divides: `wm_ptr2idx`'s 8-bit `div` on a wild
window pointer (wm-1, `kernel/wm.inc:7278-7301`), hostile FileSize into two
unguarded divides (dw-1, `kernel/diskw.inc:3930`, `:3168`),
`OSAPI_FSX_SPK`'s caller N at IF=0 (gfce-4), the desk service's `.make`
(gfce-5). The divide fence is a habit, not a ratchet (gfce-3). The only common
refusal signal is CF; what AX means is up to each cell's author (abi-9):
`DRV_CALL`'s `.none` answers AX=0 (`kernel/driver.inc:6655-6659`), XREFUSE
AX=1, kern_small's file cells `FERR_PROT`, kern_mega's real-mode bracket a
bare `stc` with AX untouched (`kernel/fsx.inc:2341-2351`). Four error-code
spaces (`FERR_*`, `DRVE_*`, `LD_*`, `FSKE_*`) reuse the same numbers for
unrelated meanings, but no consumer ever mixes families (gfce-8, histsys-14
(unchecked)). kern_mega already has the full two-tier model and two panels
with two vocabularies (gfce-10).

**What the period systems did.**
- *MS-DOS*: its int 0 handler printed "Divide overflow" and terminated the
  *program*, not the machine; int 24h handed a critical error to a handler
  with Abort/Retry/Ignore/Fail - an ACTION column; int 21h AH=59h (DOS 3.0)
  returned extended error, CLASS, ACTION and LOCUS; CF + AX with error 1 =
  invalid function, never renumbered.
- *Mac System 1-7*: one `OSErr` number space for every manager, and one
  `SysError(id)` path drawing the bomb box with an ID.
- *Windows 3.0/3.1*: 3.0's UAE killed the faulting app (and often left the
  system unstable); 3.1 added parameter validation and a debug KERNEL that
  RIPs and continues.
- *OS/2 1.x and Unix System V*: the two-tier split - a fault in user code is
  the process's (trap screen and kill; SIGFPE, core and exit), a fault in
  system code is the system's (trap screen; `panic()`).

**What os8088 already does better.** kern_mega's fault containment beats
Windows 3.0's UAE: per-task catch frames pushed by `wm_pkgcall` and the
loader, a close attributed to one instance, loop protection
(`MEGA_FKCAS`/`MEGA_FKGAP`), a kernel fault kept fatal
(`kernel/mega.inc:2504-2560`, `:3023-3100`). The death panel's construction is
safer to draw than the Mac's bomb box. Refusal as a normal path - CF=1 with a
probe and a greyed control (SPEC.md 47) instead of a silent slow path - has no
period equivalent. The DOS layer already banks IVT 00h per world
(`apps/dos/dosw.inc:89-110`), so a kernel int 0 handler needs no change to
DOS, and `BOOT_MARK` already wrote the 4-byte stub shape
(`kernel/kernel.asm:7659-7671`).

**Recommendation.**
- **Now - one fatal path for every CPU fault** (gfce-1, isr-8, histsys-2):
  generalise `sch_diepanel` into a system-error panel taking a message
  pointer; two 4-byte stubs (int 0 always, int 6 when `cpu_tier` >= 286,
  never int 5, which is PrtSc on a PC) installed from `.ovl` at `sched_init`.
  **+30..40 resident, +12 .ovl** (it needs WP7's `.ovl` room). Turns six
  present findings from hang, corruption or silent continue into a named
  stop. WP8.
- **Now - the divide fence as a ratchet** (gfce-3): a registry of every
  `div`/`idiv` site with its proof, on the `tests/textsites.txt` precedent.
  **0 bytes.** It is what keeps the next wm-1 from landing unproven.
- **Now - error CLASS, not error NUMBER** (gfce-8, abi-9): write DOS 3's
  class and action columns into SPEC.md 18.4 beside the FERR list (the mapping
  is total and short: media/user action, hardware/retry, not found, exists,
  out of resource, bad format, locked, application error, internal), and one
  SDK sentence: "CF=1 is the only universal refusal; AX is a code only where
  the cell names its family; AX=1 always means 'not on this kernel' in a cell
  that names XREFUSE". **0 bytes.**
- **Flag day - one refusal shape** (abi-9, gfr's R9): AX=1 = "no such
  service/verb on this kernel" everywhere; XREFUSE, `FERR_NODISK` and
  `FSKE_NOTHERE` already answer 1, so only `DRV_CALL`'s `xor ax,ax` and the
  bare `stc` stubs move (+3 a stub that keeps its body, 0 where it shares
  XREFUSE). **~+5 per kernel.**
- **Record - a package fault closes the package, not the machine** (gfce-2):
  on an 8088 the CPU has already stepped past a faulting `div`, so the
  real-mode kernels can get kern_mega's two tiers for the cause that matters
  without catch frames, ~+100 resident in total. Also record: kern_mega's two
  panels unified (gfce-10); gfce-11's note that a hung fsx bracket already
  has an exit (Ctrl-Alt-Del), which makes video-18's Ctrl+Alt+Esc latch
  (unchecked, +15) a nicety.
- **Not adopted**: a single renumbered error space (gfce-8). Its only buyer is
  one text door, the price is every FERR-checking package rebuilt, and each
  family's strings are already the one copy.

---

## 5. Validation at the boundary: hostility owned by the reader, fences at the funnels, a checked build

**Where os8088 stands.** SPEC.md 20.8's trust model says packages are trusted
and resident validation is not paid for. That is a defensible line, and the
reference implementation of hostile input - the BPB's 17-rule table with a
per-call quotient guard at every geometry divide (`kernel/disk.inc:5025-5300`,
`:1755-1767`) - is better than anything DOS shipped. But the line is drawn
site by site, not at the door, and it has holes on both sides of it:

- *package-supplied pointers and lengths*: `wm_ptr2idx` divides an
  unvalidated BX (wm-1); `OSAPI_WM_WAKE` writes `[bx+W_WKQ]` from ISR and
  worker context with "BX is trusted" (histsys-11,
  `kernel/wm.inc:2343-2369`); the entry proc's returned BX is never checked
  (loader-4, `kernel/loader.inc:1460-1495`); `gfx_blit4` trusts the sign of
  its width, sweeping 64KB from the 336-byte `vga_pbuf` on VGA (gfx-1,
  `kernel/vga12.inc:1886-1889`); the wm doors validate inconsistently (wm-13);
  packages write the record's geometry the SDK calls read-only (abi-7,
  `apps/paint/paint.asm:12831-12870`, `apps/os88stik.inc:2376-2407`);
  `OSAPI_MEM_P` treats any unknown verb as `MEMP_AVAIL` (hm-14); the owner
  fence's self-match lets FREE and REGROW act on the caller's own live region
  (hm-2, accepted in SPEC.md 66.6.1.1 as self-harm, but the damage lands on
  others); `OSAPI_XMEM_COPY` writes anywhere below 1MB on kern_mega (mc-17);
- *hostile files*: a compressed package whose image is shorter than its clear
  prefix wraps the expected length and the decoder writes ~64KB past the
  region (loader-1, `kernel/loader.inc:1066-1067`), and the compressed read
  can overrun by up to 512 (loader-3); hostile FileSize (dw-1); a hostile
  `SYSTEM.CFG` `CH` byte steps a `.bss` byte 254 past `clk_sec` (sysmisc-2,
  `kernel/driver.inc:4964-4972`); a 13+-byte driver volume name comes back
  unterminated (dr-9).

**What the period systems did.**
- *Windows 3.0 to 3.1*: 3.0 validated almost nothing and UAE'd; 3.1 added
  retail parameter validation at the USER/GDI door (`IsWindow` on every HWND)
  and the debug KERNEL that RIPs - which is what found most Win16 bugs.
- *Mac System 5-7*: no validation; a bad handle was a bomb.
- *OS/2 1.x*: full validation behind the protection boundary.
- *DOS*: trusted the MZ header's sizes and every BPB field.

**What os8088 already does better.** The claim table is out of band, so a
stray write cannot corrupt the allocator the way DOS's MCB headers could; the
owner word comes from the stub's ES and cannot be forged (histmem summary).
The loader's header fence, its re-check after the full read and teardown by
owner stamp and `W_SEG` sweep are better attribution than Win16 or AmigaOS
managed (loader summary). Table dispatch is bounded wherever it was read
(`ui_cmd`, `toast_say`, `osapi_ferr_text`; gfce strength 3), and future
settings bytes clamp (`kernel/dockmod.inc:292-295`).

**Recommendation.**
- **Now - make hostility a property of the reader, not of the call site**
  (gfce rethink): the BPB works because ONE routine validates and every
  consumer trusts proven fields. Apply the same shape to the two other
  readers that own a domain: the settings unpack gets a domain column in
  `CFG_MAP` (gfce-6, 0 resident; sysmisc-2's 5-byte `.ovl` clamp is its first
  row), and directory-entry staging clamps FileSize once at `dsk_get_dir`
  (dw-1's two guards, +10..12 .cold, become the belt). The loader computes
  the clear prefix once in `ld_check_hdr` and asks every compressed bound
  there before anything is claimed (loader-1 + loader-3, **+18 .cold**, fixes
  a HIGH).
- **Now - fences at the funnels, not at each door**: `wm_isrec` at the
  ISR-callable doors (histsys-11, +6..12; a range check is the right 80/20
  because `WIN_SIZE` 74 is not a power of two and an alignment check would
  need a `div` at interrupt time, ctx prior art); the slot index stored in the
  record so `wm_ptr2idx` is a bounded load instead of a divide (wm-2, net +4
  kern_big / -2 kern_small, ~-80 clocks a call; or wm-1's +5 bound alone);
  loader-4's BX check (+8..18 .cold); gfx-1's `jz` -> `jle` (**0 bytes**);
  hm-2's exact fence (+6..8); hm-14 (+4); dr-9 (+5). Then write wm-13's rule
  into SPEC.md 11 once. WP8, WP19.
- **Now - a checked build, not a checked kernel** (abi-15, engsys-20
  (unchecked), gfce-9): an `APICHK=1` knob - window-taking cells become
  same-length padded jump cells into a validating stub (BX in the window
  table, `W_SEG` = the caller's DS), file cells check `[sch_cur]`=0 - with one
  soak row driving the package sweep on it. **0 shipped bytes**, bounded by
  guard 2. This is Windows 3.1's debug KERNEL without 3.1's retail cost, which
  SPEC.md 20.8 rightly declines.
- **Now - abi-7's lint** for stores to `[es:..+W_*]` under `apps/` with an
  allow-list, and a no-repaint mode for `OSAPI_WM_RESIZE` (~+10) so the two
  packages that write geometry have a legal door (Win16 `SWP_NOREDRAW`).
  The Mac's lasting regret was exposing `WindowRecord`; Carbon had to
  retrofit accessors.
- **Record** - mc-17's ownership check on `XMEM_COPY`'s conventional end
  (+30 .mega; HIMEM.SYS never validated it either).
- **Not adopted** - window handles in place of record pointers (wm-R3
  (unchecked), refused by gfr-4): the record is readable by contract in
  hundreds of package sites, every inline setter cell would become a
  translating stub, and `wm_isrec` gives the validation without the break.
  Re-raise only with R6 (content coordinates).

---

## 6. The memory model: claims, not handles, plus the tier every period GUI had

**Where os8088 stands.** The heap is the best-argued subsystem in the kernel
(memory summary): an out-of-band record table that *is* the allocator,
two-ended placement, purgeability encoded in the owner word at zero record
cost, relocation by callback through the holder's own dispatcher (no handle
double-indirection on an 8088), one walk body for plan and run, and
`mem_avail` defined as the question `mem_claim` answers. The systemic gaps:

- **Only the kernel can own a cache.** There is no purgeable class for a
  package claim (hm-9, mem-15; `mem_pg_own` is a closed kernel list,
  `kernel/memory.inc:1306-1331`). histmem calls it "the one systemic
  capability every period GUI had and os8088 lacks".
- **No system sub-allocator.** `MEM_OWNER_MAX` = 8 is the most-negotiated
  constant in the package tree (Sheet sits at 8 of 8 repeatedly in SPEC.md),
  and WIN16, Sheet and Weave each hand-roll a free list (hm-12).
- **The owner word means four things**, and the slot/segment split costs code
  in five places (mem-13); `MEM_OWNER_MAX` also caps the kernel's single
  `MEM_K_DRV` tag, so a ninth conventional driver image is refused over free
  heap (hm-4); the purge floor is one word stamped per TASK while every
  callback runs on task 0, and any teardown lifts it (hm-13, files-4).
- **Aimed stores are protected by audit, not mechanism**: files-1 (kern_small
  shed hands the listing's destination to the read-ahead or FAT window) and
  files-2 (kern_big compaction leaves `FS_VSEG` stale) are both "the aimed
  destination moved or vanished during the mount" (files RT2).
- **The parts loader stays resident** in every non-loader parts package, once
  per instance: ~0.77-1.36KB measured per package (hm-11).
- The KB doors keep regrowing the same carry bug (mem-5, hm-6, mem-18), which
  is the correctness argument for ABI-BREAK-LIST C1.

**What the period systems did.**
- *Windows 3.0/3.1*: handles over a moveable/discardable global heap;
  `GlobalAlloc(GMEM_MOVEABLE|GMEM_DISCARDABLE)`, `GlobalLock` to get a
  segment, lock counts, LRU discard and `GMEM_NOTIFY` callbacks; the NE
  segment table's MOVEABLE/DISCARDABLE/PRELOAD/LOADONCALL flags and a
  separately discardable resource table; `LocalAlloc` as the per-module
  sub-allocator. In real mode it moved code with return addresses on the
  stack by walking BP chains.
- *Mac System 5-7*: master pointers (handles, double indirection),
  `HLock`/`HUnlock`, `HPurge`/`HNoPurge`, `MoveHHi`, and a `GrowZone` proc an
  application installs to free memory when a request fails; the Resource
  Manager's purgeable resources.
- *GEOS (the 8088 peer)*: a handle table for everything, blocks
  movable/discardable/swappable and locked around use, LMem chunk heaps
  inside blocks, VM files, swap drivers to EMS/XMS/disk.
- *OS/2 1.x*: LDT selectors per process, `DosAllocSeg`/`DosReallocSeg`,
  `DosSubAlloc`; motion transparent behind descriptors.
- *DOS*: an in-band MCB chain, owner = PSP, first/best/last fit (AH=58h).

**What os8088 already does better.** No double indirection on every access,
which on an 8088 is a real per-access tax GEOS and the Mac paid; a five-level
purge rank priced in user wait, where the Mac had one purgeable bit and
Windows LRU; compaction that plans before it copies; DMA heads kept inside one
64KB page by the allocator; owner reaping at teardown (AmigaOS leaked
everything on a crash); no relocation records and no stack walking (packages
follow no frame convention, and `INC BP` prologues would cost bytes in every
far proc). kern_mega already has OS/2's model - it rebases selectors instead
of telling holders (`kernel/memory.inc:2995-2998`).

**Recommendation.**
- **Now - package purgeable claims through the proc that already exists**
  (hm-9, mem-15): a `MEMP_PURGEABLE` verb appended to `OSAPI_MEM_P` (AX =
  proc), `MC_DMA` bit 13 (free once heads stay at or under 0x1000), rank TRIV,
  and `mem_shed_one` plus the compactor's drop arm call the proc with **DX =
  0 = purged** after `mem_can_move` agrees. That one call is the Mac's
  `HPurge` *and* its `GrowZone` notification. **+30..70 .cold kern_big**;
  kern_small refuses the verb. Additive, lock update without a bump. WP17.
- **Now - one system sub-allocator in source** (hm-12b): lift WIN16's arena
  allocator into `apps/os88lmem.inc`, GEOS LMem / Win16 `LocalAlloc` /
  OS/2 `DosSubAlloc` as an include, **0 kernel bytes**; and raise
  `MEM_OWNER_MAX` to 10 on kern_big (hm-12a, 0 bytes, a policy decision since
  SPEC.md 50.2 justifies 8). Exempt the kernel tags from the owner cap (hm-4,
  +5 .cold).
- **Now - small structural fixes**: floor per instance, not per task (hm-13,
  +6..8 .cold); a one-byte aimed-destination pin both `mem_shed_one` and the
  mover refuse (files RT2, ~+12 resident, both kernels); "no record" reported
  distinctly so a claim skips the pointless compaction (hm-8, +6).
- **Now - init-drop for the parts body** (hm-11): an `OP_INITDROP` option
  that cuts the region to the program after `op_load` through
  `OSAPI_PKG_REHOME`'s KEEP form. **-0.8..-1.5KB per running instance of each
  parts package, 0 kernel** (DOS TSR install-code discard; Windows
  discardable INIT segments). And region movability by header bit instead of
  69 `OS88_REGION_MOVABLE` lines in 67 files (mem-17, +8 kernel, ~-1KB of disk
  tree-wide). Bank the file's size and first cluster so a lazy part is not
  re-fetched from a different build of the same name (hm-7, +20 per parts
  package).
- **Now, prototype first** - keep `mem_tab` sorted by base so every walk is
  linear and best fit is free (mem-14, -30..+20 .cold, blocked on kern_mega's
  index-keyed selector pairing).
- **Flag day** - C1 (the KB doors withdrawn: -45 kern_big, -45 kern_small,
  -12 kern_mega; gfr-7 "survives on correctness"); mem-13's owner = instance
  slot (-20..-40 .cold) if the Task Manager is touched anyway.
- **Record** - handles at the API (hm-10 / R2-6): gfr-4 corrects the "one
  conversion, not two" argument (C1 converts 221 claim/avail/regrow sites; a
  handle touches 367 FREE/MOVABLE sites, +2..4 bytes each), and kern_mega's
  "several hundred" bytes are `.cold` above 1MB, not conventional. hm-2's
  now fence buys the correctness half. Also recorded: recency inside a rank
  (mem-16), shared code between instances (hm-15, loader-14 (unchecked)),
  XMS handles before the 8-block pool grows (mem-21 (unchecked)).

---

## 7. Residency and lifetime: a third lifetime, machine-shaped residency, lifetimes by count

**Where os8088 stands.** On-demand modules are one of the tree's best ideas
(SPEC.md 2.8): kernel code shipped as a file, stamped with the kernel's own
layout, read into a claim when the feature is asked for, and gated by
`t_moddrop` so it is freed when done. The boot blob rejoins the heap floor, so
boot-only code costs no resident byte - better than DOS's SYSINIT, Windows'
init code or System 6 INITs (boot summary). The VGA rung proved that
"resident" can mean "resident on machines that can run it". Three things are
missing at the model level:

- **Only two lifetimes.** A byte is resident (always paid) or a module (a disk
  read on every use, which needs the system disk). Every desktop gesture and
  every Sticky Apps menu press loads and drops a module that the shell's own
  comments call too slow elsewhere (shell-2, `kernel/desksc.inc:437-462`,
  `kernel/stikmenu.inc:60-88`), and kern_big carries FILECP and FDLG
  resident because ONDEMAND-PLAN refused the swap (bytes-3; spans `.cold`
  1,657 + 2,080 MEASURED). The pin that a purgeable module needs is designed
  and unbuilt (SPEC.md 2.8.3, ONDEMAND-PLAN 7.1/7.2).
- **Residency is shaped by build, not by machine**, outside the VGA rung: the
  keyboard mouse is ~456 bytes resident on every machine although a mouse
  retires it seconds after boot (input-12); the PS/2 runtime and XMS bodies
  are resident on an 8088 (bytes-2); `font_run_x`'s once-per-run VGA prologue
  is resident on mono machines (text-7); the mono renderer is resident on
  VGA-only machines (video-17, ~1,108 bytes MEASURED span).
- **Lifetimes by hope, not by count.** One global `drv_wcnt` stands in for
  per-driver worker counts, so since USBMOUSE.DRV keeps a worker for life,
  unloading any other driver stalls ~5 s and then leaves it detached but
  still published (drivers-1, HIGH); nothing counts who is *inside* a driver
  (drivers-2); the Sticky App library's lifetime is keyed on a proxy
  (`IF_DESK`) instead of on who bound it, a latent use-after-free (shell-1);
  a module can be freed on a worker while the UI task executes in it
  (mem-10, folded into theme 1).

**What the period systems did.**
- *MS-DOS*: COMMAND.COM's transient portion at the top of memory,
  overwritable, reloaded from `COMSPEC` with a checksum; per-OEM IBMBIO for
  hardware; OS/2 1.x's mini-FSD - a disposable boot-time reader.
- *Windows 3.0/3.1*: discardable code segments reloaded through INT 3Fh
  thunks; one DISPLAY.DRV / KEYBOARD.DRV / MOUSE.DRV per machine;
  installable drivers and MMSYSTEM with open counts.
- *Mac System 5-7*: purgeable CODE and other resources; `LoadSeg`/`UnloadSeg`.
- *AmigaOS*: libraries and devices with open counts, `Expunge`d lazily at
  open count zero when memory runs low.
- *GEOS*: almost everything outside a small fixed kernel lived in movable,
  discardable resources; one video driver resident per adapter;
  `GeodeUseDriver`/`GeodeFreeDriver` reference counts.

**What os8088 already does better.** Modules are assembled with the kernel and
need no export table or fixups; DOS never knew what its own resident bytes
bought. The module ABI is versioned by construction (build and layout stamps,
build-time-proven `mod_cfix`). Explicit `op_fetch`/`op_drop` and
`mod_need`/`mod_drop` are the right policy for a one-floppy XT, the same
conclusion the Mac reached with `UnloadSeg` (see "would NOT copy"). The `.vga`
slide achieves GEOS's one-renderer residency with near calls.

**Recommendation.**
- **Now - the third lifetime: DISCARDABLE** (bytes-3, shell-2, mem-10's pin;
  WP2): build the module pin once, then (a) on kern_big preload FILECP and
  FDLG into purgeable claims during the boot read, COMMAND.COM's transient
  model, **~-3,600 resident kern_big** against +60..100 .cold for the pin and
  boot copy and ~6 more sectors in the boot read; (b) keep MOD_SETS and
  MOD_STIKM after use as low-priority purgeable claims shed by `mem_claim`'s
  shed-and-retry (+40..80 resident), so a second visit on a roomy machine
  costs no disk. phase1 puts WP2 at **-3,408..-3,492 kern_big** and calls it
  the phase's keystone and its risk. It depends on theme 6's shed policy
  (WP17) and theme 1's teardown.
- **Now - machine-shaped residency as a rule** (bytes rethink 2; WP3): the
  keyboard mouse as a `KBMOUSE` module dropped when `[mou_ptr]` is set
  (input-12, **-300..-380 resident on both kernels**, +~450 module, the first
  press costs ~400 ms; it matters most at 128KB); an AT rung below `.vga`
  gated on `[cpu_tier]` for the PS/2 runtime and the XMS bodies (bytes-2,
  **-250..-320 on an 8088 with neither VGA nor EGA**, 0 on the gauge; the XMS
  half is overstated, since tier-0 refusal code an 8088 runs must stay);
  `font_run_x`'s VGA prologue into `.vga` (text-7, ~-185 on mono kern_big,
  ~+10 on the VGA gauge; owner's call). Each new door goes in a registry like
  `tests/vgarefs.txt`. text's placement rule is worth stating generally:
  per-CELL VGA arms stay in `.text`, per-RUN or per-OBJECT ones go to `.vga`.
- **Now - lifetimes by count**: per-row worker counts in a `DRV_MAX`-byte
  table (drivers-1, +~25 .cold, +11 .bss; on timeout also clear the class's
  publication); a bounded wait for `[wm_tcnt]==0` before DETACH, refusing
  with `DRVE_BUSY` (drivers-2, +~12 .cold; GEOS counting is the model,
  ~40 bytes for both); library lifetime by binder - an `IF_LIB` bit set by
  the LIB verb and tested by `desk_drop_x` (shell-1, 0 resident, +4..6
  DESK.DRV; AmigaOS `OpenLibrary`/`CloseLibrary`).
- **Record** - the mono rung on planar-only machines (video-17, -1.1..-1.5KB
  on VGA kern_big, expensive to build; MONO-RECLAIM-PLAN is the opposite
  direction); nest-bracketed module thunks so modules can move like driver
  images (mem-10's rethink, priced per thunk site).

---

## 8. Dispatch and the call surface: an index for rare cells, a class in the package

**Where os8088 stands.** The surface's mechanics are the best part of the
kernel and beat every period peer on an 8088 (abi summary): direct far calls
into a fixed table with no relocation and no loader fixups; three cell shapes
chosen by call frequency (the inline cell measured at 52-57 cycles against
159-162 for a SLOT); callbacks that are plain near procs because the kernel
loads DS from `W_SEG` (Win16 needed `MakeProcInstance`); ES = `KERNEL_SEG` on
entry to every callback; `t_api_abi` and the fingerprint lock. What lags is
the model around them:

- **The rare half pays for fixed addresses twice**: 112 rare cells at 6 bytes
  each (672 bytes) and no way to refuse a cell the kernel does not have
  (bytes-1; table `0x10..0x4FB` = 1,259 bytes MEASURED, identical on all three
  maps).
- **The window record holds 13 handler words** on kern_big (312 bytes of
  `.bss` over 12 windows; 10 words, 120 bytes on kern_small), set through ten
  handler setters and seven flag setters (histsys-9, abi-11, bytes-6;
  `kernel/wm.inc:24-131`). bytes-6's grep found **no package code reads a
  handler word through the record**, so moving them breaks templates and
  setter calls but no data access.
- Verbs arrive in AL, AH or BL depending on the door; names collide across
  subsystems (three meanings each of CLIP, FS and DESK; abi-12, abi-13
  (unchecked)).
- "Preserve every register" generated the dead-callee-save class every recent
  size pass harvested - 4,791 bytes of push/pop in resident code (bytes-5
  (unchecked)) - while C callers already treat everything as clobbered and
  throw the work away (abi-17, `apps/cc/os88thunk.asm:58-66`).

**What the period systems did.** Every generation of 8086 system software
that had to be both small and versioned converged on a call site that NAMES a
function rather than JUMPING to it (bytes rethink 1): DOS's AH with one table
word per function and an unknown-function refusal; the Mac's A-line trap
word through a RAM trap table (2 bytes at the site, `_Unimplemented` for
absent traps); AmigaOS's 6-byte library vectors at negative offsets with
`SetFunction`; Win16's ordinal imports resolved at load, with a missing import
failing the LOAD. For callbacks: Win16 `WNDCLASS.lpfnWndProc` once per class,
styles at `CreateWindow`, `SetWindowLong` for runtime changes; GEOS class
method tables; OS/2's `DosVerbNoun` naming and Pascal convention (AX-DX and
ES volatile).

**What os8088 already does better.** No fixup pass, so a 4.77MHz load stays a
read; per-call cost ~5x below DOS's int 21h; the package dispatcher does once
what `MakeProcInstance` did per callback; the per-event vector means a
package pays nothing for events it never asked for (ui rethinks, which is why
the class table below keeps vectors rather than adopting a message switch).
No trap patching by design - extension is by driver class and service table.

**Recommendation.**
- **Flag day - T5, the rare-cell door** (bytes-1, reconciled by gfr-1): rare
  cells behind one software-interrupt door with an inline index byte; hot
  SLOTs and inline cells unchanged; the existing `api_rs`/`api_rsc`/`api_rx`/
  `api_rxc`/`api_rn` bodies are already the door's back half (they take the
  target in BP). **-380 kern_big, -240 kern_small, -380 kern_mega resident;
  -2 per rare call site (2,767 sites, ~-5.5KB unpacked across packages).**
  gfr-1 corrects the cycle price to **+150..215 clocks a rare call on the
  8088 (30-45 us), not +80..100**, and asks for a frequency reading on four
  cells (`OSAPI_GFX_FILL_GRAY`, `OSAPI_SEG`, `OSAPI_FS_OP`'s step,
  `OSAPI_WM_CURSOR`) before the renumber. It supersedes T1 and K17-1 (T1 stays
  the fallback). A call to a cell newer than the kernel becomes a refusal for
  free. OS/2's lesson rides with it: the gate is where kern_mega's ring 3
  (L2) would want exactly one door.
- **Now - rehearse the door on a private ABI** (gfr-2): open it first to the
  kernel's own `cw_` shims, which are versioned with the kernel. **~-140
  .text kern_big**, and a mechanism proven there costs nothing to abandon -
  T1 was declined twice partly because it could only be tried by breaking
  every package.
- **Flag day - the window class in the package** (abi-11, histsys-9,
  bytes-6; gfr-3): one class pointer per record (a length byte, a style word,
  the handler table), the creation-time flags becoming the style word, and
  K17-2 surviving as one runtime `OSAPI_WM_FLAG` door. **-215 kern_big, -95
  kern_small, -270 kern_mega resident**; in packages +~1.3KB of class tables
  replacing ~2.6KB of setter code. It replaces K17-3, and six of T2-2's ten
  kern_mega ICW stubs go with it for free. Survey packages that change a
  handler after `WM_CREATE` first.
- **Flag day - free renames** (abi-12, abi-13, gfr's T6): T5 forces every call
  site through one macro, which makes renames and verb-register moves cost
  nothing in that one rebuild. Now: a rule for new doors (verb in AL unless AL
  is an argument, then BL; a `DosVerbNoun`-style naming rule). 0 bytes.
- **Flag day - V1 as a separate clobbering entry** (gfx-16, abi-17): kernel
  callers depend on `gfx_fill` preserving registers (`gfx_hline`'s `mov bx,
  cx`), so V1 is a package-facing entry, C SDK first, lint for asm. -2
  resident, -120 clocks a rect call.
- **Record** - a scratch-register convention behind a liveness gate (bytes-5,
  engsys-19), the one place the Win16/Mac Pascal convention would pay, and
  only with a gate in the `t_stkbalance` mould; a surface-wide switch would
  break hand-written asm silently.

---

## 9. Versioning and capability discovery: a level, now, in bytes that are already free

**Where os8088 stands.** `PKG_FMT` is an equality test (`ld_hdr_ok`,
`kernel/loader.inc:292-300`), the right major number. There is no minor: a
package cannot learn at runtime which cells this kernel has, so a new package
on an old kernel with the same `PKG_FMT` calls past the end of the table
(abi-6, loader-6). SPEC.md 20.2.0.1's "What it does not cover" names exactly
this gap and leaves only The Wire to fence it. The private ABIs (`VX_ABI_VER`,
`XM_ABI_VER`, `HD_ABI_VER`) are versioned by hand and outside the lock
(drivers-16); the C SDK's `os88.h` and `os88thunk.asm` are outside both
`t_mirror` and the lock (engsys-11). kern_mega's package arms have no way for
a converted binary to declare itself (mc-19 (unchecked)).

**What the period systems did.** GEOS geodes carried a protocol number per
imported library - major must match, minor must be >= - checked at load;
AmigaOS `OpenLibrary(name, version)` refused a too-old library; DOS had
int 21h AH=30h; Win16's NE header carried an expected-Windows version and
ordinal imports failed the load naming the missing entry; the Mac had
`Gestalt` (System 6.0.4) for capabilities; Windows 3.0's NE `PROTMODE` flag
let a converted binary declare itself so the loader stopped excusing it.

**What os8088 already does better.** The ABI lock with a written reason for
every fingerprint move and a binary decode of the table are stronger than any
period project's discipline (engsys prior art); "refusals, not holes" and
greying a fact (SPEC.md 47) are Gestalt's discipline applied per control. The
deliberate renumbering in alpha (SPEC.md 20.8) is better for a tree that
rebuilds every caller.

**Recommendation.**
- **Now - publish a LEVEL, not an address** (gfr-5, reconciling abi-6 and
  loader-6): `dw OS88_API_LEVEL` at `0060:0006` (free padding today,
  `kernel/kernel.asm:2346-2360`; **0 resident**); `os88pkg.py` writes the
  needed level - numbered from The Wire's existing `WIRE_CELL_LEVELS`,
  `tools/os88wire.py:120-138` - into package-header byte 31, which every
  package ever built has as 0 because `os88pkg.py:122` refuses a 16-character
  name; `ld_check_hdr` compares `<=` and answers `LD_EVER` (**~+8 .cold**);
  an `OS88_NEED level` SDK macro for a package that wants to test at run time
  (~10 bytes per opting package). A level rather than `osapi_table_end`
  because T5 takes rare cells out of the table, and a table end would stop
  describing the API at the flag day. First confirm no `PKG_FMT`-10 kernel
  predates SPEC.md 57's registry, which moved a mouse-instrument word off
  `0060:0006`. **abi-6's flag-day half dissolves**, and abi-16's "the header
  has no room" is refuted for now (byte 31 and +15's bits 3-5 are free; an
  out-of-range class launches with the largest stack,
  `kernel/instance.inc:2106-2110`). WP9.
- **Now - the PM-clean bit** (mc-19 (unchecked), megacore rethink 4): one
  class bit at +15 bit 5, `mega_gpw` then *closes* a clean package on a CS
  store instead of fixing it up, and `t_megapkg` ratchets the census. **+12
  .mega**, additive, the staged road to L3 that does not wait for The Wire to
  serve two builds.
- **Now - every ABI under the lock** (drivers-16, engsys-11): fingerprint
  `vxabi.inc` and the XMEM/HDD ABI blocks against their versions, and add
  `os88.h` / `os88thunk.asm` to `PKG_FMT`'s inputs (Microsoft's H2INC made the
  C and MASM headers unable to disagree). 0 bytes.
- **Record** - at a future header change, one bit for "an extension list
  follows the clear prefix" (type, length, data), the NE/PE idea, so future
  blocks take no bit (abi-16, ~+20 .cold).

---

## 10. The kernel-driver contract: read in place, a request block only where registers ran out

**Where os8088 stands.** The driver layer is one of the most carefully built
parts of the kernel (drivers summary): a header matching a package header, a
publication slot per class, the service table copied so IRQ0 reads it with no
segment load, a length byte that makes short tables safe, the attach half and
the `SYSTEM.CFG` parser in images. The model-level costs:

- seven 36-byte copies of a union service table in resident `.bss` (252 bytes
  on kern_big), of which seven sound-only cells are copied for every class
  (histsys-7, drivers-9; `kernel/driver.inc:6173`);
- a register-only verb ABI for block and file service that has run out of
  registers ("no register left to say which class",
  `kernel/driver.inc:2577-2581`, with a history of eaten arguments at
  `:2525-2535`) and produced three dispatch bodies (histsys-8);
- a class that is both a service type and a single slot, so two drivers of
  one type are mutually exclusive (drivers-15);
- the set of drivers compiled into the kernel as a row table (histsys-12
  (unchecked));
- `SYSTEM.CFG` drops records it does not know at every save (drivers-17).

**What the period systems did.** DOS: the `DEVICE=` chain, strategy/interrupt
entries and a request header at ES:BX, `INIT` returning the break address.
Mac: the Device Manager's unit table, `Open/Prime/Control/Status/Close`, a
`ParamBlockRec` with `ioResult` and `ioCompletion`, and cdev Control Panels.
OS/2 1.x: request packets through a strategy routine, `DevHlp` services, init
code discarded. Windows 3.x: installable drivers (`DriverProc`), MMSYSTEM
device IDs (many devices per type), `SYSTEM.INI` keeping unknown keys. GEOS:
drivers as geodes with reference counts and a refusable `DR_SUSPEND` with a
reason.

**What os8088 already does better.** Load and unload at runtime, typed and
versioned classes, a failure banked instead of a hang; the `DSV_CP*` page ABI
*is* the cdev / `DRV_CONFIGURE` model at a fraction of the cost; the movable
image with the kernel holding the relocation is simpler than OS/2's explicit
locking; `OSAPI_DRV_SUSPEND` is GEOS's `DR_SUSPEND` with the hardware facts
recorded on the way past; `DRV_H_KEEP` is DOS's INIT break address, proven.

**Recommendation.**
- **Now - read the tables in place** (histsys-7): keep class 1's copy (the
  IRQ0 reader), and for every other class keep the SI that `DRVV_ATTACH`
  returned and read `[es:di+DSV_*]` from the published segment; works for the
  HMA and kern_mega selectors; each in-place read needs a `DRV_H_DSV` length
  test because `drv_publish` zeroes the cells a short table does not supply.
  **~-90..-140 resident kern_big, ~-36 kern_small.** gfr-6: this makes D1 and
  drivers-9 moot - with one copy left, `drv_svcix` has nothing to index.
  WP4.
- **Now** - lifetime by count and the IRQ registry (themes 7 and 2).
- **Flag day (DRV_VER only), recorded as D6** - a request block for
  `DSV_BLK` and `FSV_*` only (histsys-8, scoped by gfr-6): ES:BX -> {verb,
  class/unit, status, LBA/count or name/offset, buffer far pointer,
  completion word = 0}: one dispatcher, one status channel, the async door
  left open; `OSAPI_FILE_*` registers unchanged. Kernel ~-60..-120 resident
  (ESTIMATED, unverifiable). **The drivers reviewer refuses a request packet
  for every verb, and is right**: building a packet per call is dearer than
  the direct near-proc table on an 8088, and the IRQ0 read depends on the
  table. The two positions agree once the block is confined to the verbs that
  already stage their arguments in `.bss`.
- **Record** - typed slots instead of classes (drivers-15, D5, ~0..-40;
  MMSYSTEM device IDs, the Mac unit table); driver discovery from
  `SYSTEM\*.DRV` headers with `SYSTEM.CFG` keyed by name (histsys-12
  (unchecked), D7, ~-200..-350 resident, +~120 boot overlay); unknown
  `SYSTEM.CFG` records copied through (drivers-17, +~40 CTRL.DRV; Windows'
  INI kept unknown keys); a registered "parking/unparked" callback in place of
  more `hb_drvmask` special cases if more drivers grow trip-sensitivity (the
  DOS 5 Task Switcher API, int 2Fh AX=4Bxxh; megados prior art).

---

## 11. The kernel matrix: kernels fork, packages must not

**Where os8088 stands.** kern_small, kern_big, kern_mega and kern_emu are
build-time products off one source, and gkm-5 prices run-time unification out
of the question: carrying kern_small's tables inside kern_big costs +5,440
bytes before any code against kern_small's 5,120 of headroom, and carrying
kern_mega's arms puts ~12-14.7KB on every XT against kern_big's 4,096. Packages
are one binary across kern_big and kern_small. What is missing is what keeps
the arms honest (gap-kernel-matrix-near-model Verdict):

- **one-arm fixes**: drivers-3 (`OSAPI_DRV_CLASSK` misreports an HMA driver on
  kern_big) and hiber-2 (kern_big's DOS-handoff refusals leak the extent claim)
  were fixed on kern_mega and left on kern_big (gkm-1);
- **refusal stubs nobody decodes**: three of four kern_small-only bugs are a
  stub or guard written apart from the full body - abi-2 (`OSAPI_FS_OP`
  overwrites AX), dw-8, video-1 (Hercules page 1 enabled on a CGA+Hercules
  machine because the dual-card guard is `%ifdef KERN_BIG`) - and
  `t_api_abi` decodes one kernel of four (abi-3, gkm-2);
- **coverage inverse to importance**: the 128KB floor has 28 soak rows,
  kern_mega ~430, and `test-full` never boots kern_mega; `t_hotpar` covers
  kern_big and kern_small, not kern_mega (gkm-3, gkm-4).

Of the review's 64 code bugs, 32 live on one arm or arm-set, but only 5 are
true divergence (one contract implemented twice, disagreeing). The near model
no longer limits capacity on kern_small (32,565 free in the window) or
kern_mega (14,056); it binds kern_big at the VGA rung, by 6,946 bytes, not the
19,842 `KERN_CODE_MAX`'s line implies (gkm-11). `.cold`'s ~700/~540 bytes of
crossings (bytes-4) buy the HMA, a 5150 ROM, the slide over the VGA rung and
46KB above 1MB on kern_mega.

**What the period systems did.** Windows 3.0 answered the same question the
same way: KERNEL.EXE, KRNL286 and KRNL386 were three binaries off shared
source with WIN.COM choosing, while USER, GDI, drivers and applications were
one binary with a `PROTMODE` flag. Windows 3.1 then dropped real mode - a
legitimate move, recorded so it is a choice rather than drift. DOS 5/6 ran one
binary from 8088 to 486 by run-time adoption (DOS=HIGH, HIMEM). SVR3/SVR4
configured kernels from a `master`/`system` file selecting whole objects at
link time, so there was no `#ifdef` forest inside shared files. The Mac raised
its RAM floors per release and dropped tiers rather than forking.

**What os8088 already does better.** Segment macros make ~400 sites
protected-mode clean at assembly time and byte-identical off kern_mega, where
Windows relied on written rules; ROM/HMA adoption is DOS=HIGH done at run
time; the 128KB floor is real; the feature-symbol layer already reads like a
SysV configuration file.

**Recommendation.**
- **Now - arm honesty as gates, not culture** (gkm-1/2/3/4/7; ~+6..12 resident
  kern_big for the two one-arm fixes, drivers-3 +5 .cold and hiber-2 +20
  HIBER.DRV): a four-kernel `t_api_abi` with per-kernel FIXED rows and
  decoders for kern_mega's SLOT and ICW cells, extended to the refusal stubs
  (abi-3, 0 bytes, full tier); a `smallport.txt` MartyPC port list for the
  floor; `t_hotpar` over kern_mega; a ratchet on raw build names in shared
  code (SysV's discipline: build names only in the configuration); and the
  rule that a fix in shared code lands on every arm that carries the code.
- **Now - retire variants that exist for one feature** (gkm-6): kern_emu
  exists because a polled device once needed kernel poll sites, and
  USBMOUSE.DRV proved it does not: a USBMOUSE-shaped VMMOUSE.DRV, ~+40
  kern_big.
- **Flag day / staged - L3 is the matrix's package half.** Every armed package
  in `$(MEGAPKGS)` is matrix cost paid outside the kernel; mc-19's clean bit
  (theme 9) retires them one at a time.
- **Record** - do not unify at run time (gkm-5); do not flatten kern_small to
  one code segment unless its ROM is dropped (gkm-8, saves 540, costs the
  ROM); the one-disk 8088/286 product, WIN.COM's half, after L3 (gkm-9); push
  more kern_mega `.text` into `.cold` above 1MB through the KSW macros when
  kern_mega grows (gkm rethink 3); and gkm-10's warning that which kernel has
  budget slack is deciding which kernel gets a speed-up (several XT redraw
  wins - KHP_DMGSTALE/RAISEOL/RELIT/CAPTION - ship only on kern_mega, wm
  summary; gfx-6 wants a dated take-or-delete per KHP row, WP11).

---

## 12. The contract and documentation system: one source per fact, the rest generated

**Where os8088 stands.** The engineering system is strong (engsys summary):
636 assembly-time assertions, budgets derived from a stated KB rule, a binary
decode of the API table, an ABI lock with written reasons, a dozen former
prose rules now gates, and a generated "does this exist?" index. None of the
period projects had a gate on their own documentation at all. The weak half
is what is still prose, and it fails in three repeatable shapes:

- **hand copies of code**: SPEC.md 3's constants and 15.1's guards are wrong
  (`KERNEL_SEG` 0x0800 vs 0x0060, `STK0_SIZE` 1024 vs 512, `DSV_SIZE` 26/28 vs
  36; engsys-3), and slot contracts exist in up to four places that disagree
  in both directions (abi-10, engsys-5);
- **running counts**: the fast tier's "37" is 76 rows, the table guard's
  tallies, "exactly two" `sch_lock` raisers (engsys-6, ctx-3);
- **section numbers that resolve to the wrong section**: six citations of the
  greying standard say 46 (ArtfulType) and mean 47, and pass checkdocs
  (engsys-2); basename collisions silently disable citation checks
  (engsys-8); `.c`, `.h`, `.txt` and `.sh` are not scanned (engsys-9).

And the history problem, which every area reviewer hit: **956 doc fixes were
filed across the 25 data files**. Comments and SPEC text narrate history
("it used to") instead of stating invariants: boot found more than forty
statements of wrong current values; `wm.inc` is 52% comment, `ui.inc` two
comment lines per code line, the text files ~45%; SPEC.md's front matter
(1-4, 15-17) describes retired designs as current (engsys-4), and 56% of the
245,112-line SPEC.md is package design notes (engsys-18). Register discipline
- the hard rule "preserve every register" - is ungated: ~1,480 `; out:` and
~900 clobbers lines exist and nothing parses them (engsys-19). AGENTS.md has
diverged from CLAUDE.md by 726 lines, including all four byte rules
(engsys-10, bytes-10). The tier admission rules are broken by ~17 one-package
fast rows (engsys-7).

**What the period systems did.** Inside Macintosh had a chapter per manager
with a closing Summary of every constant, record and routine, and later
volumes *marked* what they superseded instead of rewriting the old chapter;
OS/2 generated language bindings from one definition (H2INC); GEOS routine
headers read `PASS:`/`RETURN:`/`DESTROYED:` and the Esp assembler checked
conventions; AmigaOS extracted the reference manual from source headers
(autodocs); the DOS Programmer's Reference gave every function the same
Entry/Return/Notes shape with a version gate ("2.0+"); Inside Macintosh and
the NT design workbook cited by chapter NAME, which cannot resolve to the
wrong section.

**What os8088 already does better.** Content assertions at assembly time, the
ABI lock with reasons, the suite registry with `why=`, and the generated index
- each beyond anything the era had.

**Recommendation (all 0 kernel bytes; WP10, WP24, WP25, WP14).**
- **One source per fact, the rest generated** (engsys-3, -5, -6, -10, -18,
  -19): the tree already proves the model three times (`os88index.py`, the
  kernsize baseline block, the `ks:` facts line). Extend it to SPEC.md 3's
  constants and 15.1's guards between markers, `t_mirror` over SPEC.md's
  fenced nasm blocks, delete SPEC.md 20.3's and 53.8's contract copies in
  favour of the SDK names, a COUNTS table for prose counts ("a prose count
  either carries its moment or is not written"), AGENTS.md generated from
  CLAUDE.md, and a routine reference generated from a fixed header grammar
  (in/out/clobbers/flags/lock) with a report-only liveness checker - the
  precondition for theme 8's register convention.
- **Names over numbers** (engsys-2, -17): a heading-drift check against the
  merge-base's {number: title} map, and stable `[G-BUDGET]`-style tags for
  guards.
- **History in its place**: rewrite SPEC.md 1-4 and 15-17 as a short current
  front matter, one paragraph per rule plus the gate that enforces it, and
  move narrative to `docs/history/` (engsys-4); the comment-discipline sweep
  that applies the 956 recorded fixes and replaces per-pass byte history with
  invariants (WP25). Split package sections into per-package SPECs keeping
  their numbers (engsys-18), the Inside Macintosh chapter-per-manager shape.
- **Admission by declaration** (engsys-7): a `family=` or dated `owner=`
  field on each fast/full row, and `t_registry` fails a row with neither.
  And one ratchet library (engsys-13 (unchecked)) instead of 21 bespoke
  parsers.
- **Do not let the review break the build**: engsys-1 found this review's own
  in-progress reports failing checkdocs, and so `make`; checkdocs should warn
  rather than fail in `docs/reports/` and `docs/history/`.

---

## What we would NOT copy, and why

Each of these is a period mechanism a reviewer considered against this
machine and refused. They are recorded so nobody re-derives them as
improvements.

- **Automatic demand loading and LRU discard of package code** (Windows'
  INT 3Fh reload thunks, GEOS swap drivers; histmem rethinks). On a
  one-floppy XT every automatic reload can be a disk-swap prompt in the middle
  of a gesture - it is what made Windows miserable on floppies (loader prior
  art). os8088's explicit `op_fetch`/`op_drop` and `mod_need`/`mod_drop`, the
  Mac's `UnloadSeg` honesty, are the right policy. Theme 7's DISCARDABLE
  lifetime is the safe subset: kernel modules, entered only from the UI task,
  dropped only when memory is short.
- **Master pointers / handles on every access** (Mac, GEOS, Win16
  `GlobalLock`). The double indirection is a per-access tax on an 8088, and
  `HLock` discipline bugs were the Mac's chronic disease; the relocation proc
  moves the cost to the rare event. Memory handles stay recorded (R2-6), and
  window handles are refused while R6 is shelved (gfr-4).
- **Code movement by stack walking** (Win16 real mode's BP-chain walk with
  odd-BP far frames). Packages follow no frame convention, and the `INC BP`
  prologue would cost bytes in every far proc (memory prior art).
- **One window procedure plus a message number in place of per-event
  vectors** (Win16 `WndProc`, GEOS methods; ui rethinks). On an 8088 a
  package pays nothing for events it never asked for, and the kernel needs no
  dispatch in the package. The flag-day class table keeps the vectors and
  only moves where they are stored.
- **A single renumbered error space** (Mac `OSErr`; gfce-8). No consumer mixes
  families; the class column is what a program acts on.
- **A DOS request packet for every driver verb** (drivers rethinks). Dearer
  than the near-proc table on an 8088, and the IRQ0 read depends on the
  table; only BLK/FS, which already stage in `.bss`, get a block.
- **Trap patching** (Mac `SetTrapAddress`, AmigaOS `SetFunction`, chained DOS
  vectors; abi rethinks, shell prior art). A bad patch is a dead desktop.
  os8088's single-owner vector tables (`dkv`/`thv`), whose owner restores the
  kernel's entries on the way out, avoid the INIT-ordering and tail-patch bugs
  chained Mac patches suffered. The SDK should say in one line that a package
  never writes `KERNEL_SEG`'s first 0x500 bytes.
- **Run-time unification of the kernels** (DOS's one binary; gkm-5), and
  flattening kern_small to one code segment (gkm-8) - priced above.
- **A full priority field in the scheduler** (OS/2 priority classes, AmigaOS
  priorities; sched rethinks). A byte per task, a second scan key and an
  inversion story for 14 slots; the one-shot boost buys the interactive half.
  Likewise copying AmigaOS's per-task `Forbid` save so `Wait()` is legal
  inside it (~12 hot-path bytes; writing the rule down is enough).
- **Per-thread message queues** (OS/2 `WinCreateMsgQueue`; ctx prior art). 12
  tasks times even an 8-record queue is most of 1KB; `W_WKQ` is the
  one-message queue that buys most of it. Likewise a lossless UI work queue in
  `evq` (ctx R1): the ring is lossy by design and the post bytes are cheaper.
- **Proportional fonts** (QuickDraw, GDI raster fonts, GEOS BSW; text prior
  art). The 8x8 cell is what makes `font_run`'s one store per cell row, the
  cell-diffed menu bar and the row-major run possible. Take the style
  synthesis (text-15, kernel bold) and leave the metrics.
- **Write-behind caching on removable media** (SmartDrive; diskread,
  diskwrite prior art). Only droppable frees may wait (dw-15).
- **Delayed clipboard rendering** (Win16 `WM_RENDERFORMAT`; ui prior art).
  Wrong for "copy, close, paste" on 128KB; take the generation count and a
  reserved format byte instead (ui-12, ui-13, both unchecked).
- **Desktop shortcuts as `.LNK` files in a `DESKTOP\` folder** (Windows 95;
  shell rethinks). Every icon would cost a directory walk and a read at boot,
  ~400 ms each on the XT, where today's cached 64-byte picture costs none.
- **Settings in battery-backed CMOS** (Mac PRAM; sysmisc-17 (unchecked)). An
  XT has no RTC, and one `SYSTEM.CFG` with a deferred single write fits a
  machine where a floppy write is seconds.
- **GEOS-style state files instead of a hibernate image**, a full-memory
  hibernate checksum, and Windows 2000's per-page sums (hiber rethinks). The
  first needs an object system; the others refuse good live-written images or
  solve a paging problem os8088 does not have.
- **OS/2 1.x's no-copy DOS box** (megados rethinks). kern_mega cannot fence a
  fixed low region while its kernel is conventional, and the runs park gives
  the program ~621KB against OS/2's ~500KB.
- **Patching known EXEPACK stubs in EXEC** (MS-DOS 5; md-25 (unchecked)).
  `kd_xunp` unpacks any variant it can validate and keeps the 621KB arena.
- **A unified DOS-style buffer pool** (dr-19, unchecked) and **a keyboard path
  into menus** (text-19, unchecked, +150..250 .cold): recorded, the first for
  its write-path risk, the second as outside the "no huge growth" brief.

---

## Byte ledger by theme

kern_big resident unless stated; signed; ESTIMATED except where a theme
quotes a MEASURED span. "Now" counts the recommended cores, not the optional
or measure-first items; it overlaps `synth/phase1.md`'s WPs, which are the
authority for ordering and totals.

| theme | now: kern_big | now: kern_small | now: elsewhere | flag day (big / small / mega) |
|---|---|---|---|---|
| 1 contexts | +2..+27 (ctx-1 +12..30, ctx-2 +9, ui-2 0, ui-3 -19, sched-3 0..+7) | ~+7..+32 | -13 .mega (ctx-2) | opt-in deferred damage +80..120 (record) |
| 2 interrupts | +77..+91 (IMR shadow +25, IVT scan +25 .cold, hm-1 +10..16, hm-5 0..+6, input-2 +14..16, ctx-5 +3); full `OSAPI_IRQ` ~+100 net instead of the shadow, owner's call | similar, shadow ~+25 | mc-4 +20..30 .mega; SOUND.DRV -60 with the cell | IRQ cell mandatory (L3); mc-5 +20 .mega |
| 3 waiting and I/O | +24..30 +6 cell (sched-15); optional: histsys-5 +16..40 (+25 AT), sched-16 +22, histsys-3 +20, dr-5 +45, dw-14 +70, dw-15 +15, dr-4 +16, dw-12 +90 | +24..30 core; file items mostly kern_big | | dw-22 0..-20; D6 recorded |
| 4 faults and errors | +30..40, +12 .ovl | ~+28 | | R9 +5 each |
| 5 validation | ~+65..+90 (loader-1/3 +18, loader-4 +8..18, histsys-11 +6..12, wm-2 +4, dw-1 +10..12, hm-2 +6..8, hm-14 +4, dr-9 +5, gfx-1 0) | wm-2 -2; rest similar | sysmisc-2 +5 .ovl; APICHK 0 shipped | - |
| 6 memory | +50..+95 (hm-9 +30..70, hm-4 +5, hm-13 +6..8, files RT2 +12, hm-8 +6); mem-17 +8 | files RT2 +12; refuses hm-9 | parts packages -0.8..-1.5KB each per instance (hm-11); ~-1KB disk (mem-17); +20 per parts package (hm-7) | C1 -45 / -45 / -12; mem-13 -20..-40 .cold |
| 7 residency and lifetime | WP2 -3,408..-3,492; KBMOUSE -300..-380; drivers-1/2 +37 .cold +11 .bss | KBMOUSE -300..-380 | KBMOUSE module +~450; AT rung -250..-320 on a non-VGA 8088 (0 on the gauge); text-7 -185 mono / +10 VGA | - |
| 8 dispatch | gfr-2 ~-140 | 0 | | T5 -380 / -240 / -380; class table -215 / -95 / -270; V1 -2; packages -2 a rare site, +1.3KB tables for -2.6KB setters |
| 9 versioning | +8 .cold | +8 | +12 .mega (clean bit); ~10 per opting package | none (dissolved) |
| 10 drivers | histsys-7 -90..-140 | ~-36 | | D6 -60..-120 (recorded); D7 -200..-350, +120 overlay (recorded) |
| 11 matrix | +6..12 (+~40 if kern_emu retires) | few | +20 HIBER.DRV (hiber-2) | L3, staged |
| 12 docs | 0 | 0 | 0 | 0 |

Whole flag day as gfr reconciles it (themes 4, 6, 8 and the rows that ride
them, plus L1's measured pad): **~-1,078 kern_big, ~-685 kern_small, ~-957
kern_mega conventional**, about a third of each being L1's pad, which varies
0-511 from build to build.

## Ordering across themes

The dependencies that cross themes, so the phases can be cut:

1. Theme 1's teardown-on-UI (ctx-1) before theme 7's DISCARDABLE lifetime and
   before theme 3's disk-wait release: both widen what may run while the UI
   task waits, and both assume teardown no longer runs on a worker.
2. Theme 3's `cur_inxfer` change (ctx-3's first item) before the `sch_lock`
   release across int 13h; it fixes the bug the release would create.
3. Theme 6's shed policy (WP17) before theme 7's purgeable modules (WP2).
4. WP6 and WP7 (blob and `.ovl` room) before theme 4's stubs and anything
   else that grows a transient section; phase1 shows both homes full on
   kern_big.
5. Theme 9's API level before any new appended cell in themes 1-7, so the
   first package that calls one can refuse on an old kernel.
6. Theme 12's context tags and header grammar before the flag day, so the
   flag day's rebuilt tree is checked by them (abi rethink 6).
7. gfr-2's rehearsal on the `cw_` shims before T5.
