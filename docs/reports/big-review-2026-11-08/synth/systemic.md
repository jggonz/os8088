# The systemic view: what to approach differently, and what the period systems did better

Kernel review of 2026-11-08, final systemic synthesis. Inputs: the 25
first-round area reviews under `areas/` with their verified data under
`data/`; the twelve gap reviews (`gap-*.md` / `gap-*.json`), of which six are
systemic by nature and are folded in theme by theme (context-lock-model,
residency-tiers, kernel-matrix-near-model, fault-containment-error-model,
api-shape-includes, flagday-reconcile-abilist) and six supply evidence
(unchecked-loadbearing, mouse-cursor-half, os88ui-kernel-copy,
xmem-driver-and-cells, hdd-driver-kernel-face, mega-unowned-spans); and the
three adversarial lenses over the top 40 synthesized items
(`synth/challenge-truth.json`, `challenge-cost.json`, `challenge-fit.json`).
This file replaces the first systemic synthesis, which was written before the
gap reviews and the lenses existed.

This is a dated measurement report. It describes one tree (HEAD `ad3798da`
when written) and goes stale on purpose; SPEC.md and the code remain the
authority.

## How to read this

- **Phases.** *now* = no ABI break (an appended cell or a contract
  clarification is an `abilock --update` with no bump). *flag day* = batched
  with the next `PKG_FMT`/`DRV_VER` break that `synth/flagday.md` plans.
  *record* = written down so it is not re-derived, not proposed.
  *owner* = a decision a recorded owner ruling or plan already speaks to; the
  item names the ruling and does not presume the answer.
- **Bytes** are signed and say where they land: *.text* / *.bss* / *.cold*
  (all three RESIDENT; `.cold` is a cold path, not a cold lifetime),
  *.ovl* / *blob* (boot-transient), *module* (a `.DRV` image, paid while
  loaded), *.mega* (above 1MB on kern_mega after the switch, 0 conventional),
  *package* (a package image). Per the CLAUDE.md banner they are bytes, not
  rungs.
- **MEASURED** means a span read off `build/kernel.map`,
  `build/smallk/kernel.map` or `build/megak/kernel.map`, or a figure a row or
  the field produced. Everything else is ESTIMATED from encodings, and every
  time or cycle figure not marked MEASURED is PREDICTED.
- **Corrections win.** Where a verifier or a lens corrected a figure, only the
  corrected figure is quoted. Where the lenses disagree with each other, the
  item says so and either re-derives the figure from the maps and the code or
  states the range and why; the cases are collected in *Where the lenses
  disagreed*, near the end.
- **Confidence.** Every finding cited was verified (*confirmed* or
  *corrected*, the correction folded in) unless marked **(unchecked)**.
  Refuted findings are dropped: isr-5, wm-20, gfce-4 (the `OSAPI_FSX_SPK`
  divide is unfenced by the owner's ruling, SPEC.md 34.11.1) and gms-7.
- **IDs.** `area-N` are the data files' finding ids. Gap prefixes: `ctx-`
  (context-lock-model), `rt-` (residency-tiers), `gkm-` (kernel matrix),
  `gfce-` (fault containment), `gi-` (API shape and includes), `gfr-`
  (flag-day reconciliation), `ul-` (unchecked load-bearing), `mcur-`
  (mouse-cursor half; its own ids are `mc-`, renamed here to keep them apart
  from megacore's `mc-`), `ouk-` (os88ui kernel copy, its `F`/`R` ids),
  `xm-`, `hdk-`, `gms-`. `WPn` are `synth/phase1.md`'s work packages; `BR-n`
  are `synth/bugs.md`'s; the flag-day rows (`T5`, `R14`, `C1`, `L1`, `L3`,
  `R9`, `D4`, `D8`, `A4`) are `synth/flagday.md`'s. **Theme numbers 1-12 are
  the ones the lenses call T1-T12**; theme 13 is new. Note that flagday.md's
  own `T5` (the rare-cell door) and `T6` (the call macro) are unrelated to
  themes 5 and 6.
- Lens amendments are tagged **[truth]**, **[cost]** or **[fit]** where they
  change what an item says.

## Summary: the themes ranked by value

Value here is what a theme changes for the machine and its maintainers per
byte spent and per decision it needs, not its size. The ranking is a
judgement; the columns are the evidence.

| rank | theme | the change of approach | now (kern_big resident) | flag day | why this rank |
|---|---|---|---|---|---|
| 1 | 1. Execution contexts and locks | teardown runs on the UI task; a dying owner releases its lock; context rules become tags and a gate | **+11..+48** (ctx-1 with ctx-2 ~+43, ui-3 -19) | none; deferred damage stays record (+150..+200) | one move removes the root of five findings (histsys-1, hm-3, ctx-2, mem-10's worker arm, the worker reset of `[mem_pg_floor]`) and the context-lock review's one HIGH |
| 2 | 4. Faults and the error model | every CPU fault reaches one named stop; refusal is CF, and AX means something only inside a named family; errors carry a class | **+45..+60** .text, +20..+30 transient | the "not on this kernel" value (open: AX=0 or AX=1) | turns today's dead machine (1981 ROM), endless re-execute (286+) or silent garbage (later 8088 ROMs) into a named stop for wm-1, dw-1, gfce-5 and every unfenced divide, but changes what a package divide does on an 8088, so it ships with the package-close half or a kernel-CS test |
| 3 | 7. Residency and lifetime | a third lifetime (a dropped module becomes a cache); init-lifetime code held from boot and dropped at settle; class code in owner-loaded overlays, not more rungs | **~-2,380..-2,450** (FILECP/FDLG cached), **-290..-380** (keyboard mouse), **-220** (PS/2 overlay) | - | the largest byte return in the review, all no-ABI; needs two owner decisions (the system-disk trade on kern_big, and the module test for the keyboard mouse) |
| 4 | 9. Versioning and capability | a runtime API LEVEL in padding that is free today | **0** word, **+7..+12** .cold | dissolves (gfr-5) | the cheapest item here, and the precondition for every appended cell in themes 1-7 and 13 |
| 5 | 2. Interrupts and the IF=0 budget | one owner for IVT, IMR and int 15h; a written latency budget with a ratchet and rows | **+77..+91** (+100..+150 for the full IRQ cell, owner's call) | IRQ cell mandatory for drivers (L3/D8) | no IF=0 bound exists; the longest windows (a grow copy, ~170 ms for 64KB PREDICTED) dwarf the one SPEC names |
| 6 | 5. Validation at the boundary | hostile FILES are owned by the reader that understands them; package pointers are checked only in a checked build | **+41..+55** (about half .cold) | - | contains a HIGH (loader-1) and respects the owner's "packages are trusted" ruling instead of eroding it |
| 7 | 11. The kernel matrix | kernels fork, packages must not; arm honesty by gate | **+5** .cold, +~20 HIBER.DRV | L3, staged | 5 true divergences, and coverage inverse to importance (21-28 kern_small soak rows against 390-430 kern_mega, and `test-full` never boots kern_mega); the gates are 0 shipped bytes |
| 8 | 6. The memory model | keep claims and the relocation proc; add package-purgeable claims through that proc, a shared sub-allocator in source, and parts init-drop | **+64..+119** .cold | C1 (kernel -45..-69) | the one capability every period GUI had (a package-owned cache); hm-11 saves 0.8-1.5KB per running parts instance at 0 kernel bytes |
| 9 | 3. Waiting and I/O | block on events, not time; a disk wait that freezes nothing, after its preconditions | **+40..+55** .text +6 cell (the wait primitive) | caller-held write cursor (dw-22) | the wait primitive is the "one change of model" the scheduler review names; the disk-freeze release is real only on ROMs that keep the caller's stack and needs four preconditions |
| 10 | 12. Contract and documentation system | one source per fact, the rest generated; a committed heading lock | **0** | 0 | round 1 already applied 946 fixes; what remains is the gate that stops the shape recurring |
| 11 | 8. Dispatch and the call surface | rare cells behind one index door; the handler table in a window class | -40..-60 (`cw_` rehearsal, after the vector question) | T5 **-325..-390**, R14 **~-300** | the largest flag-day bytes, but the owner declined both smaller forms of the door, it costs +150..+300 clocks a rare call PREDICTED, and its vector collides with DOS |
| 12 | 10. The kernel-driver contract | read service tables in place; a request block only where registers ran out | **-90..-140** (0 kern_small) | D6 request block (record) | a sure no-ABI saving, mostly bookkeeping beyond it |
| 13 | 13. The API shape and the includes | a stateless kernel whose efficiency is won back by caller-held tokens; fix the leaks in that shape, do not build a library | **+34..+36** (grain cell, cursor name stamp) | READ_AT-shaped stream cell; withdraw `os88_rseq` | small, and one of its items makes a documented kern_small path run at all |

**Totals (ESTIMATED).** The core now-phase items net to about **-2.46KB to
-2.84KB resident on kern_big** (about -2.98KB to -3.19KB of savings, almost
all of it theme 7's three residency items, against +0.35KB to +0.52KB of
growth), or -2.19KB to -2.58KB with theme 3's file-layer items. On a kern_big
whose `.cold` a ROM or COLDHMA.SYS adopted, the FILECP/FDLG cache saves
nothing and is skipped. kern_small nets about -0.16KB to +0.05KB (growth of
+0.22KB to +0.34KB against the keyboard mouse's -0.29KB to -0.38KB). The byte
ledger near the end itemises both. None of it needs a `PKG_FMT` or `DRV_VER`
bump.

## The one pattern under all thirteen themes

Read across 37 reports, os8088's defects have one shape, named five ways:

- histmem: "the defects are mostly where one mechanism's discipline stops
  short of a neighbour";
- isr: "a precondition that the kernel's own ISRs meet and the driver- and
  package-facing entry points do not";
- histsys: "mechanisms running on the WRONG CONTEXT";
- gap-fault-containment: "one good example of each thing ... and none of them
  is applied as a rule";
- gap-context-lock-model: "the context rules are prose in two lists that
  disagree, and none is enforced".

Each problem is solved **once, very well, at the point of the field report
that found it**: the BPB's 17-rule table, the stack-overflow panel,
`gfx_lock`'s owner-stamped fair semaphore, `CURBAR_ON`'s task-scoped flag, the
fsck commit order, `t_moddrop`'s drop gate. The rule then travels to the next
site by prose and by care. The period systems that aged well differ in one
way: they turned the rule into a **mechanism at a single choke point**.
Windows 3.1 validated at the USER/GDI door; OS/2 1.x put every IRQ through
`DevHlp_SetIRQ`; the Mac published which routines "may move or purge memory";
DOS 3 gave every error a class and an action. os8088 already has the choke
points (`ui_bill`, the N-stub of the file cells, `wm_pkgcall`,
`ld_check_hdr`, `sch_diepanel`, `mem_reloc_call`, `mod_need`/`mod_drop`) and
the tooling that turns prose into gates (636 assembly-time assertions,
`t_api_abi`, `abilock`, a dozen former prose rules now ratchets). **Most of
what follows is "move the rule to the funnel, then gate it"**.

The counterweight is just as consistent. Where a period system had a
mechanism os8088 lacks, os8088 usually has a cheaper or more honest one, and
several period mechanisms are wrong for a 4.77MHz, one-floppy, 128KB machine.
The gap reviews sharpened this: the residency review found the purgeable tier
is *one site* because the drop gate already brackets every use (rt-1), and the
API-shape review found the includes are already os8088's libc over an
NFSv2-style stateless kernel (gi verdict). "What we would NOT copy" is near
the end and is not short.

---

## 1. Execution contexts and locks: who runs what, on which stack, under which lock

**Where os8088 stands.** gap-context-lock-model wrote the matrix the code
actually obeys. It has eleven contexts: the UI task with the gfx lock free
(U0), held (U1) or under `sch_lock` (UL); a worker free (W0) or holding the
lock (W1); a **dying worker** (WD); a hardware ISR (I); the `FSXF_RATE` hook
(H); the exclusive fsx bracket (X); a kern_mega fault (M); and the fatal
panels (F). It has three real locks, `gfx_lock` (a 4-byte owner-stamped RAM
semaphore with a one-debt fairness rule), `sch_lock` (a counted pre-emption
disable) and IF=0, plus at least eight ad-hoc context bytes (`cur_inxfer`,
`cur_barok`, `mem_cp_busy`, `wm_pkgd`/`wm_tcnt`, `sch_rbusy`, `sch_chbusy`,
`fsx_task`, `dsk_batch`), each argued correctly at its point of use. The
matrix's cells that matter:

| service | U1 (UI, lock held) | UL (UI, `sch_lock`) | W0/W1 (worker) | WD (dying worker) | I (ISR) |
|---|---|---|---|---|---|
| draw | ok | `fprog` only | rule (prose) / ok | **ok, and it paints OTHER packages** | arrow only |
| take `gfx_lock` | deadlock (not reentrant) | livelock if contended | ok / deadlock | ok | no |
| yield, sleep | ok | **returns at once** (`sched.inc:1881`) | ok | `task_exit` | no |
| `mem_claim` | ok, compacts and sheds others | compactor only | first fit (kern_big, code); **first fit and shed on kern_small** (ctx-6) | `mem_free_rec`, which writes the global `[mem_pg_floor]` | never |
| file / int 13h | ok | nests | **SPEC.md 20.6 rule 7, not enforced** | rule | never |
| `wm_destroy` (repaints) | ok | no | rule 7 | **yes**, `inst_unwin` on the worker | no |
| `mod_drop` | ok | - | - | **yes**, `desk_drop_x` (hm-3) | - |

Read off the matrix, three things are missing at the model level:

- **Work runs on whatever task caused it.** A dying worker runs
  `inst_task_die` on its own 128/192-byte slice: `gfx_lock`, `inst_unwin`
  (`wm_destroy`, which repaints and far-calls survivors' procs), the far
  `desk_drop_x`, `gfx_unlock`, `mem_free_rec`, `task_exit`
  (`kernel/instance.inc:1929-1953`). The same decision produces hm-3
  (DESK.DRV dropped on the worker while the UI task may be inside
  `DKD_HOVER`, `kernel/ui.inc:1003-1010`, which `kernel/mod.inc:879-886`
  itself admits), mem-10 (any `mod_drop` on a worker), ctx-2 (a worker that
  dies holding the gfx lock spins for ever in `gfx_lock`, whose `.retry`
  never compares the owner, `kernel/vga12.inc:5082-5104`), the heap's "dying
  worker's repaint" special case (`kernel/memory.inc:974-978`) and the worker
  reset of the global purge floor. The UI task already drains six kinds of
  posted work (`inst_launch`, `wm_clreq`, `mega_fkq`, the posted compaction,
  `ui_rebootq`, `wm_tarm`); **teardown is the one that is not posted**.
- **Repaint is synchronous and nested** inside whoever caused the damage
  (histsys-10; `kernel/wm.inc:3162-3168`).
- **`sch_lock` is a big kernel lock with six meanings and about forty
  takers** (int 13h not re-entered and "no other task is mid-primitive"; an ES
  banked across a FAT walk survives a shed; the heap map is stable across a
  plan; a PCM clip owns the CPU; the xmem copy is not interleaved; hibernate
  freezes state), where SPEC.md 7 once said "exactly two" (ctx-3, sched-3).
  Its one expensive meaning, int 13h, cannot be released alone (theme 3).
- **The lock order is obeyed by hand.** "Never block under `sch_lock`" is now
  a sentence in SPEC.md 7 but is not a numbered rule and nothing enforces it
  (ctx-8 corrected); the compactor and `dsk_xfer` drop the lock round their
  waits by hand (`kernel/memory.inc:4101-4103`, `kernel/disk.inc:2030-2035`);
  exiting with it raised kills pre-emption for the session (sched-18).
- **The context rules are two prose lists that disagree** (ctx-7): SPEC.md
  20.6 rule 7 lists `wm_saveu` and not `OSAPI_GFX_PEN`/`OSAPI_WM_CLIP_TEST`,
  the SDK's rule 7 the reverse, and SPEC's paragraph names `OSAPI_DRV_CALL`
  as on a list that does not contain it. The XMEM cells enforce SPEC.md
  41.8's context rule nowhere and the SDK does not state it (xm-12). The
  cursor contract has the same shape: the SDK and SPEC.md 7 still say
  `OSAPI_GFX_LOCK` hides the cursor, which since 7.1.4 it only *promises*,
  and the lazy-hide protocol is opt-out with its proof instrument never
  committed (mcur-5, mcur-9).
- **Identity decisions read package bytes**: `ui_dispatch` decides "is this
  Locator" by reading `AM_ONCMD` in a foreign segment without the lock, when
  `[menu_win]` already says (ui-3, `kernel/ui.inc:2518-2531`).

**What the period systems did, at the model level.**
- *Win16 and Mac System 5-7*: a window's paint and destruction ran only on its
  owner's event loop. Windows kept a per-window update region and synthesised
  `WM_PAINT` only when the queue was empty; the Mac's Window Manager
  accumulated `updateRgn` and the Event Manager delivered `updateEvt` after
  every other event. No application's paint ever ran inside another's
  handler, and `DestroyWindow` ran on the caller's own queue.
- *OS/2 1.x*: RAM semaphores (`DosSemRequest`/`DosSemClear`) and system
  semaphores whose waiters are told `ERROR_SEM_OWNER_DIED` when the holder
  exits; per-device serialisation inside the driver model rather than a global
  pre-emption disable around the disk.
- *AmigaOS*: two nestable counters with a written rule - `Forbid()` (no task
  switch) for anything long, `Disable()` (no interrupts) only for a few
  instructions - and a defined meaning for `Wait()` inside `Forbid()` (the
  forbid is broken for the wait and restored after).
- *Inside Macintosh*: an appendix of "routines that may move or purge memory"
  and a stated list of what may be called at interrupt time. The *list* was the
  contract, and the debugger-era tools checked it.

**What os8088 already does better.** `gfx_lock` *is* an OS/2 RAM semaphore,
with a fairness rule OS/2's lacked: it took press-to-menu latency from
1.4-14.7 s to 37-70 ms (SPEC.md 7.3). The nest stack is task-aware. Only the
UI task compacts or sheds on kern_big, stated and enforced in code
(`kernel/memory.inc:955-965`). The deferred close (SPEC.md 75.2) and the
self-healing posted-work byte are cleaner than Win16's flags. kern_mega's
`mega_fkill` already does owner-died release before its jump into
`inst_task_die` (`kernel/mega.inc:2706-2721`), so the model fits this kernel.

**Recommendation.**
- **Now - teardown on the UI task** (ctx-1, histsys-1, BR-4). The worker keeps
  the shallow, lock-free steps (`inst_rel_rec`, `toast_owner_gone`), releases
  a lock it owns (ctx-2), then **inside one IF=0 window** marks the record
  task-less (`I_TASK` = 0xFF), posts it and `task_exit`s with BX = 0 (no
  release byte, the `.orphan` precedent at `kernel/instance.inc:2234-2235`),
  so the UI task can never free a record whose task is still running. The UI
  task runs the deep half - `inst_unwin`, `mem_free_rec`, `desk_drop_x`,
  `I_STATE` = 0 - on its 1KB stack, under `gfx_lock`, skipping `wm_ask_close`
  and re-testing `I_STATE` = 2 and `I_TASK` = 0xFF first so a stale post
  cannot hit a reused slot [truth]. It must not reuse `app_close_win`'s entry
  as is: that runs the package's close negotiator and sends an `I_STATE` = 2
  record to `.hide`, leaking the region (`kernel/instance.inc:1656-1670`)
  [truth, fit]. A windowless worker record needs its own reap bit, because
  `wm_clreq` is per window slot [truth]. **+30..+60 .text kern_big, about
  +43 for the cheapest shape that works, ctx-2's owner check included;
  about 5 less on kern_small** (no `desk_drop_x`); **-24 .mega** (kern_mega's
  own owner-died block goes, `kernel/mega.inc:2710-2716`). The lenses
  disagree on this figure; the derivation is under *Where the lenses
  disagreed*. SPEC.md 20.6's "the ONLY code that can free its instance record
  is the worker's own `task_exit`" and SPEC.md 11.103.10's "a Sticky App on
  its worker arm closes HERE, on that worker" are rewritten first [fit]. Once
  it lands every damage source is on the UI task for packages that obey rule
  7 (which nothing enforces, hence the tags item below) [truth]; hm-3's own
  fix (+8..12) becomes unnecessary, but **mem-10's deferred drop is still
  WP2's foundation**: ctx-1 supplies the post-to-UI mechanism, WP2 still
  builds the "frame standing in this image" count, and a one-line assert
  that `mod_drop` runs on task 0 replaces the deleted arm [fit]. Leave
  NO-PINNING-PLAN's built `sch_cur` gate in place; it still covers driver
  verbs [fit]. Covers the kern_mega fault-kill path, which also jumps to
  `inst_task_die` [truth]. Measurement: the stack depth of today's dying
  128-byte worker has no producing row; register `stkdie` (ul-10).
- **Now - liveness at the funnel** (ui-2): test `W_FLAGS` once at
  `ui_bill`'s head; net 0 kern_big, +5 kern_small. Every future callback
  kind inherits the check, and R14's class-table helper is where it belongs
  at the flag day [fit].
- **Now - identity from kernel state** (ui-3): `cmp word [menu_win],0`,
  **-19 .text**, both kernels.
- **Now - the context model as tags and a gate** (abi-8, ctx-7, xm-12,
  mcur-5): a machine-readable `; @ctx ui|any|worker|isr|hook|lock` in each
  SDK contract (the abilock fingerprint moves: an `--update`, no bump
  [cost]), a host gate over `stkdepth`'s call graph that no worker root or
  rate hook reaches a cell its tag forbids, the interrupt-callable list
  generated into `docs/INDEX.md`, and the one list replacing SPEC.md 20.6
  rule 7's and the SDK's. **0 shipped.** dw-4's retail refusal (`cmp byte
  [sch_cur],0` in the file cells' N-stub, +9) is **not** counted here: it is
  the same check theme 5's checked build carries at 0 shipped bytes, and the
  owner's ruling ("packages are trusted ... a check that exists only to
  defend the kernel from its caller is not a reason to keep a byte",
  docs/plans/LAST-DROP-BYTES.md 7.11) applies. Put it to the owner as a
  named exception (a cross-linked FAT survives the reboot that ends every
  other package fault) or leave it in the checked build [fit].
- **Now - write the lock order** (ctx-8, sched-3, sched-18): number it in
  SPEC.md 7: `sch_lock` is a nestable Forbid; nothing inside may yield, sleep
  or take `gfx_lock`; no task exits holding it. 0 bytes, or +7 .text for
  sched-3's `task_sleep` tripwire.
- **Now - kern_small's shed gate** (ctx-6, low): +14 .text kern_small, against
  SPEC.md 66.3.1's deliberate exclusion, whose stated reason covers
  compaction but not the shed. Owner's call.
- **Record - deferred damage** (histsys-10, ctx-9, wm-R2): after ctx-1 it is a
  coalescing and stack-depth question, not a safety one. Re-priced at
  **+150..+200 resident** (ul-7: the per-window owed rect is 96 `.bss` on
  kern_big, 48 on kern_small, on top of +60..+100 of drain code), and it must
  drain at `wm_close_pass`'s point rather than add a fourth deferral queue
  beside `wm_clreq` and the `WKQ_*` ring. OS/2 PM's `CS_SYNCPAINT` inverted -
  synchronous by default, a window opts into deferral - is the shape if a
  measurement ever shows double paints that matter.
- **Record** (ctx R1, R2): one UI work queue in `evq` instead of seven post
  bytes (the ring is lossy by design; the masks are cheaper); `sch_lock`
  split by meaning (buys little beyond theme 3's int 13h owner byte).

---

## 2. Interrupts: one owner for the IVT, the IMR and int 15h, and a written IF=0 budget

**Where os8088 stands.** No single owner controls the IRQ lines, the IVT and
the IMR: six owners write them their own way (the kernel, SOUND.DRV,
USBMOUSE, XMEM, os88spk packages and DOS.O88), and the kernel's
restores do not check what they overwrite (isr-13; `sched_stop` `movsw`s the
saved vector back unconditionally, `kernel/sched.inc:646-662`). A driver
image is freed with no check that its vectors were unhooked (drivers-6,
`kernel/driver.inc:4219-4330`). USBMOUSE re-derived the DOS TSR rule "unhook
only if still yours" on its own. On kern_big nothing filters int 15h for DOS
programs, so one can `AH=87h` over the XMS pool and the HMA or shut A20, and
the tier-2 copy never re-proves A20 (xm-1, xm-2). No interrupt-latency budget
exists anywhere (isr-14) and no suite row measures IF=0 duration (ctx-4,
ul-10). The longest IF=0 windows are not the one SPEC names:

- `mem_regrow` is one `pushf`/`cli` window that contains path 3's whole
  `mem_bcopy` (`kernel/memory.inc:4579-4664`), where the compactor copies the
  same bytes at IF=1 under `sch_lock`. A 64KB grow is ~32K words at ~25 cycles,
  **~170 ms at IF=0, PREDICTED** (hm-1, mem-2; ul-11 lists it as arithmetic);
- the heap's O(n^2) planning walks run at IF=0 (~7 ms PREDICTED) where
  `sch_lock` gives the same exclusion (hm-5);
- the cursor is drawn inside the serial ISR at IF=0: **15,100 cycles (~3.2 ms)
  on VGA and 11,500 on CGA, MEASURED** (`areas/isr.md`), and **21,450 cycles
  (~4.5 ms) a packet on kern_big VGA without `KHP_VGASAVE`, MEASURED**,
  through the generic rect machinery and still erase-then-draw (isr-7,
  mcur-3). The baud ceilings derived from those spans are PREDICTED (ul-11);
- kern_mega moves 64KB compaction pieces at IF=0, ~16 ms on the Victory 286
  (mc-5, derived from that machine's MEASURED 0.257 ms/KB, ul-11), and walks
  the LDT linearly under `cli` per block freed (mc-4).

ISR-facing preconditions are met by the kernel's own ISRs and not by the
driver- and package-facing doors: SS = `LOW_SEG` for `evq_push` and the WAKE
door (ctx-5; the USB-mouse feed swaps SP but not SS, and the kernel itself
records SeaBIOS hopping stacks), DF = 0 at the IRQ0 callback boundary. The
tick can park a ROM int 09h with IRQ1 still in service on the int 15h and
kern_mega paths SPEC.md 9.8.3 left unmasked (input-2).

**What the period systems did.**
- *OS/2 1.x*: the kernel owns the 8259 and the IRQ chains. Drivers claim a
  line with `DevHlp_SetIRQ` (refused if held unless both ends say shared),
  release it with `UnSetIRQ`, and signal the end of service through
  `DevHlp_EOI`; work that must leave interrupt time is handed to a task with
  `ProcRun`.
- *Windows 3.x enhanced mode*: VPICD virtualises the PIC for every VM; a short
  documented list of calls is legal at interrupt time (`PostMessage`, some
  MMSYSTEM timer calls), and anything an ISR touches must be FIXED or
  page-locked.
- *Mac System 1-7*: the cursor is drawn from the vertical-retrace task at
  68000 interrupt level 1 (the VIA), beneath the SCC serial chip at level 2,
  so a serial byte always pre-empts the cursor; the Deferred Task Manager
  (Inside Macintosh V) takes work out of interrupt level.
- *AmigaOS*: `AddIntServer`/`SetIntVector` with prioritised server chains,
  and the `Forbid`/`Disable` rule above.

**What os8088 already does better.** Private ISR stacks that set SS as well
as SP on a 4.77MHz part; wake coalescing with lost-record recovery; the mouse
ISR's `cur_inxfer` reasoning about when it may draw is explicit and gated,
where the Mac's cursor corrupted drawing in known cases; `mem_rr_ivt` makes a
moved driver's vectors follow it, which Windows never did; the timed ROM
chain survives stack-hopping BIOSes.

**Recommendation.**
- **Now - write the budget and gate it** (isr-14, ctx-4; WP13): a
  per-machine-class IF=0 bound (ctx-4's arithmetic: ~8 ms is achievable on
  the XT for the 1,200-baud mouse; SERIAL.DRV's 9,600-baud ~1 ms is not
  while the arrow is drawn in the ISR - both PREDICTED), plus
  `tests/unit/t_ifwin.py` and `tests/ifwin.txt` registering every `pushf/cli`
  window over N instructions with its reason. The measurement rows do not
  exist: register `ifoff` with `--regrow` and `--mouse` legs (ul-10) and run
  them in the final pass. **0 shipped.**
- **Now - long copies at IF=1 under `sch_lock`** (hm-1/mem-2: +10..+16
  .cold, keeping IF=0 for a `MEM_K_DRV` or ISR-written block exactly as the
  compactor does; hm-5: 0..+6 .cold).
- **Now - one generic in-service rule** (input-2): `sch_isr` refuses a switch
  while any IRQ other than IRQ0 is in service (OCW3 0Bh, the sequence the
  heartbeat already uses at `kernel/sched.inc:1958-1960`). **+14..+16 .text**,
  55-65 cycles on an 8088 [cost], every tick if it sits ahead of the switch
  decision. It replaces per-path masking with the rule AmigaOS, OS/2 and
  well-behaved TSRs obeyed: never reschedule out of an un-EOI'd hardware
  interrupt. **It lands before theme 3's disk-wait release**, which widens
  the windows in which a parked int 09h can be switched away from [fit].
- **Now - the ISR-door preconditions at the door** (ctx-5: +3 .text, `mov
  ax,LOW_SEG / mov es,ax` in place of `push ss / pop es`; the USB feed's SS;
  `cld` at the IRQ0 callback boundary).
- **Now - ownership, in order**: the IMR shadow (isr-13, ~+25), drivers-6's
  IVT scan at release (+25 .cold), then **owner's call** - an appended
  `OSAPI_IRQ` cell with LIFO-checked restore, **+100..+150 resident** kern_big
  (16 rows of 7 bytes is 112 `.bss` plus code, isr-13), -60..-100 SOUND.DRV
  (its hook and unhook are 120 bytes, MEASURED in `build/sound.hma.map`)
  [cost]. A common EOI stub inside the cell would cost every hooked interrupt
  +60..+90 clocks PREDICTED, so it is optional and kept off IRQ0 and the 8250
  paths [cost]. **DOS.O88 is an owner too**: full-screen DOS snapshots and
  restores ports 21h/A1h wholesale and windowed DOS restores 21h selectively
  (`apps/dos/dos.asm:2989-3022`, `apps/dos/dosw.inc:848-850`, `:1043-1046`),
  so a kernel shadow goes stale across a DOS bracket unless it re-syncs from
  port 21h afterwards or DOS's restore goes through the cell [fit]. phase1's
  WP22 and flagday's D8 must say the same thing about when the cell lands
  (shadow and scan now; the cell either as an owner's-call append or at the
  flag day) [fit].
- **Now - int 15h has an owner on kern_big** (xm-1): DOS.O88 filters `AH=87h`
  and the A20 calls for its programs, **+45..+60 package**, 0 kernel; the
  tier-2 copy re-proves A20 (xm-2, ~0 XMEM.DRV image).
- **Measure first - the cursor after the EOI, coalesced** (isr-7 R, mcur-R3):
  decode at IF=0, EOI, raise `sch_lock` and a busy byte, `sti`, redraw while
  the position moved. It breaks a written invariant: `mou_isr` "NEEDS NO
  RE-ENTRANCY GUARD ... never stis" (`kernel/mouse.inc:357-366`), and its
  128-byte private stack is sized on that basis. So the change must add a
  register-free busy test before the stack swap (`cmp byte [cs:busy],0`),
  and either size `MOU_PSTK` for nested IRQ0, int 09h and floppy frames
  (**+64..+128 .lowbss**) or leave the private stack before the `sti`
  (~+6), and reword SPEC.md 7 and 9 [truth, fit]. **+30..+40 .text plus
  that.** isr-7's cheaper first answer comes before it: a documented ceiling,
  with the serial lease defaulted to 2,400 baud on a UART the probe calls an
  8250 [fit]. mcur-R2's off-screen latch save-under (~0 net, +50 `.vga`,
  ~6,500 cycles against 21,450 PREDICTED) is the VGA half, recorded with
  mcur-R1's per-output exclusion (Win 3.x driver exclusion, QuickDraw
  `ShieldCursor`; +20..+30, record).
- **Now, docs** (mcur-5, mcur-9): SDK and SPEC.md 7 say `gfx_lock`
  *promises* to hide the cursor; commit the lazy-hide instrument as a
  `GFXAUDIT` knob (~+40, knob build only).
- **Flag day** - the IRQ cell mandatory for drivers (L3/D8). It replaces the
  image-move IVT walk **for driver IRQ lines only**: `mem_rr_ivt` also
  covers int 33h (USBMOUSE's PicoMEM vector) and vectors held by moved
  package regions, so the walk stays unless the cell's scope grows to
  packages, which is a `PKG_FMT` clause [truth]. Against today's tree the
  flag day saves about 0 on kern_big and kern_small (the "-25 scan" is
  drivers-6's proposed +25) and about -210 above 1MB on kern_mega [cost].
  Adopt the Windows `GlobalFix` / Mac `HLock` contract as a per-claim
  "ISR-reachable" flag so every other block moves with IF on (mc-5, +20
  .mega once the contract exists).
- **Record** - the Mac Deferred Task Manager as a queue the scheduler would
  own (sched rethinks); xm-7's tier-1 masked windows on a 6 MHz AT (PREDICTED
  to exceed one 1,200-baud byte). isr-15's BIOS mutex is **not** recorded
  here any more: it is the same mechanism as theme 3's disk-owner byte, and
  lives there with its caveat [fit].

---

## 3. Waiting and I/O: block on events, a disk wait that freezes nothing, a file layer that uses what it knows

**Where os8088 stands.** A task can block only on time (sched-15). Every
other wait is a sleep-poll or a yield-spin: SOUND.DRV's refill sleeps one
tick and polls (`drivers/sound/sb.inc:2532-2541`), `gfx_lock`'s waiters `sti /
task_yield / retry` (sched-17), and `sch_wake_ui` is a one-task special case
of a missing per-task wake. The UI task has no scheduling preference
(histsys-4 LOW, sched-16). **Every int 13h freezes every task for ~400 ms**,
because `dsk_xfer` raises `sch_lock` across the BIOS call
(`kernel/disk.inc:1462`). The file layer is stateless by name (theme 13),
recomputes - and on FAT16 re-reads - the free-cluster count on every ask
(histsys-3), makes `OSAPI_FILE_FIND` quadratic although `dsk_dscan` already
resumes from a raw slot (dr-5), keeps one kernel-wide `WRITE_SEQ` stream
record (dw-22), and leaves the whole-file write model's foreknowledge of the
size unused (dw-13, dw-14). The cursor-against-token split between
`READ_SEQ` and `WRITE_SEQ` is deliberate (SPEC.md 18.4.8 and 18.4.9 say so in
their titles); only the opposite buffer register pair is a wart (ul-4
(unchecked), correcting histsys-13).

**What the period systems did.**
- *AmigaOS Exec*: 32 signal bits per task, `Signal()`/`Wait()`, the cheapest
  general wait there is; `DoIO`/`SendIO` with a reply port for async device
  I/O.
- *Unix System V*: `sleep(chan)`/`wakeup(chan)` on any kernel address.
- *OS/2 1.x*: `DosSemWait`, `ProcBlock`/`ProcRun` for drivers, priority
  classes with foreground and I/O-completion boosts.
- *The AT BIOS itself*: int 15h AH=90h (device busy) and AH=91h (interrupt
  complete), added by IBM so a multitasker could block a task on the floppy
  instead of spinning in the ROM.
- *Mac System 5-7*: every File Manager call took a ParamBlock with
  `ioResult` and `ioCompletion`, which is how the Mac got async I/O; HFS
  `PBAllocContig` and clumps allocated contiguously from a known size.
- *DOS*: FindFirst/FindNext keeps the search state in the caller's DTA; DOS 4's
  DPB caches the free-cluster count (FFFFh = unknown) and the allocator
  maintains it; a handle's SFT entry holds an O(1) sequential position.

**What os8088 already does better.** The idle model beats every peer: the UI
task blocks with the lost wakeup closed in one `cli` span and the machine is
~97% halted at idle, where Win3.x spun (int 2Fh 1680h only under WIN386) and
System 6's `SystemTask` spun. Stateless files by name leak nothing when a
program dies, which DOS handles did. The motor-timeout media test is a
physical fact where DOS's XT path guessed with a 2-second rule. Shadowed
replace and the ordered commit mean a power cut never costs the user's bytes.
Cylinder-run batching made 140 int 13h calls against DOS's 325 on the same
workload.

**Recommendation.**
- **Now - one wait primitive** (sched-15; WP18): `task_wake(AL=slot)` with a
  sticky wake-pending bit in `T_FLAGS` bit 1 (free: only `TF_SERVICE` = bit 0
  is defined, and `task_spawn`'s word store clears it,
  `kernel/sched.inc:70-75`, `:1027-1031`), tested and cleared by `task_sleep`
  under its own `cli`; ISR-safe, no record growth, plus a rare cell. **+40..+55
  .text and a 6-byte cell** [cost: the slot-to-record conversion, the
  `T_STATE` test and the post are ~30-38 bytes, and `task_sleep`'s
  test-and-clear ~12-16; the first figure of +24..+30 left them out], less
  about 12 if `sch_wake_ui` folds onto it. It enables IRQ-driven SB refill,
  NIC-driven ETHER service and sched-17's blocking `gfx_lock` waiters (+12,
  record until measured). It is Amiga's one signal bit with the slot as
  SysV's channel.
- **Measure first, with four preconditions - release the disk freeze**
  (histsys-5, ctx-3): release `sch_lock` across the UI task's floppy int 13h
  only (drivers, hibernate, kern_mega and the debug door keep it). The
  preconditions:
  1. **ctx-3's first change**: the mouse ISR's int 13h arm reads `[sch_cur]`
     at interrupt time to decide "held by us" (`kernel/mouse.inc:4230-4265`),
     so `cur_inxfer` must become task-scoped the way `CURBAR_ON` is;
  2. **theme 1's ctx-1**, per the ordering below [fit];
  3. **theme 2's input-2**, which must land first [fit];
  4. **windowed DOS's IVT exchange**: it banks 08h-0Fh and 10h-3Fh while its
     worker runs (`apps/dos/dosw.inc:89-93`, `dvw_xflip` at `:160-170`), so
     with the lock released a tick can switch into a DOS worker mid-transfer
     and the floppy completion (0Eh) or the AT hook (15h) vectors through the
     program's table. Keep the lock while any DOS world can be swapped in,
     or exclude 0Eh and 15h from the exchange and prove it [fit].

  isr-15's caveat moves here with it: every "nothing else runs" invariant
  must be re-proved, the gate at `kernel/mouse.inc:4232` first, and nothing
  enforces SPEC.md 20.6 rule 7, the only thing that stops a second task
  entering `dsk_xfer` while the UI task is in the ROM [truth, fit]. Price:
  **+33..+38** (ctx-3: +7 .text in the mouse ISR, the rest `.cold`, because
  `dsk_xfer` is `.cold` on all three maps), 0 on kern_mega [cost]. The AT
  int 15h AH=90h hook adds **+25..+30** and must act only on AL = 01h
  (diskette; AH=90h is issued for the keyboard too) and only when SS =
  `LOW_SEG`, otherwise returning to the BIOS's own wait: `sch_switch` refuses
  a switch on a foreign stack (`kernel/sched.inc` ~2425, SPEC.md 8.5), so on
  a BIOS that serves int 13h on its own stack (SeaBIOS) the release buys
  nothing [truth]. The benefit is real on the IBM/XT ROMs, which keep the
  caller's stack. Take two numbers first on `os8088_5150_cga_gla`: a worker's
  frame rate during a 16KB read, and **the UI task's own wall time for that
  read**, because a pre-emption between the BIOS's seek and its read command
  can cost a revolution, ~200 ms [cost]. Neither row exists: register
  `xferfreeze` (ul-10).
- **Measure first - a one-shot UI boost** (sched-16, +22 .text +1 .bss;
  histsys-4's ~+10 form): the cheap half of OS/2's and GEOS's interactive
  priority, measured with a `uilat --workers N` leg that does not exist yet
  (ul-10).
- **Now - the file layer uses what it knows** (WP12, WP16): cache the free
  count the way DOS 4's DPB did (histsys-3, **+25..+35 .cold**: the cache
  must also drop on remount, on refat and on the redirected `FSV_DFREE` path
  [cost]); `FILE_FIND_SEQ` over the existing resume (dr-5, +45 .cold, +6
  cell, DOS's DTA model); contiguous allocation from the known size (dw-14,
  +70 .cold, Mac `PBAllocContig`); lazily flush only the droppable frees at
  the `gfx_unlock` gate held streams already use (dw-15, +15 .cold - safer
  than SmartDrive because only frees wait); report media-changed 06h as a
  fact, not a retry (dr-4, +16 .cold); folders grow, the one DOS 2.0 FAT
  behaviour missing (dw-12, ~+90, which also edits SPEC.md's "the kernel does
  not grow a directory" sentences [fit]; the first item to drop if phase 1
  must shrink).
- **Flag day** - the write stream takes a caller-held cursor like
  `READ_SEQ`'s (dw-22, 0..-20), and histsys-8's request block for
  `DSV_BLK`/`FSV_*` carries a completion word = 0, leaving the Mac's async
  door open for free (theme 10, recorded as D6).
- **Record** - dr-20 (unchecked): ask for the volume by name instead of
  silently dropping owed state (Mac offline VCB "Please insert the disk"; +40);
  dr-19 corrected by ul-2: there are **two** caches, not three (SPEC.md 18.95
  already absorbed the directory window); one DOS-style pool nets only
  **-100..-300 .cold** kern_big, costs kern_small a larger FAT floor and
  cannot be combined with L1.

---

## 4. Faults and the error model: one fatal path, one refusal sign, error classes

**Where os8088 stands.** One fatal panel exists, for one cause: stack
overflow. It is well built - SP moves first, no lock, `font_run`, names the
culprit - and costs **174 bytes on kern_big and 173 on kern_small, MEASURED**
(`sch_stkdie` 53AE to `sch_stkbase` 545C, strings included). **Nothing writes
IVT 0 or 6 on kern_big or kern_small** outside the `BOOT_MARK` knob
(gfce-1). What a divide overflow then does depends on the ROM, and the review
first stated it wrongly [truth]:

- *the 1981 IBM 5150 ROM*: its dummy handler writes 0FFh to the IMR, so one
  divide overflow is a dead machine;
- *the 10/27/82 ROM* (`rom_set ibm5150_82_v4` in the MartyPC configs, the
  `vm/pc5150` profile, and per docs/FIELD-MACHINES.md the owner's 5150 #1) and
  *GLaBIOS*: the dummy reads the ISR, finds nothing in service, sets
  `INTR_FLAG` and IRETs - on an 8088 the CPU has already stepped past the
  `div`, so execution continues with garbage in AX;
- *every 286 and later*: #DE pushes the address OF the `div`, so the dummy's
  IRET re-executes it for ever.

The `kernel/wm.inc` comment that says the 5150 masks every IRQ is corrected
the same way; `tests/int0sweep.py` breakpoints int 0 and never observes a mask
[truth]. And the review's top findings keep producing divides: `wm_ptr2idx`'s
8-bit `div` on a wild window pointer (wm-1), hostile FileSize into two
unguarded divides on both kernels (dw-1), DESK.DRV's `.make` (gfce-5). 5 of
about 90 kernel divide sites are unfenced and all 5 trust a value that crossed
a boundary; one of them, `OSAPI_FSX_SPK`'s N, is unfenced by the owner's
ruling (SPEC.md 34.11.1), so it is registered as trusted, not as a gap
(gfce-3; gfce-4 refuted). The divide fence is a habit, not a ratchet. The
only common refusal signal is CF; what AX means is up to each cell
(abi-9): `DRV_CALL`'s `.none` answers AX = 0, XREFUSE AX = 1, kern_small's
file cells `FERR_PROT`, kern_small's `VOL_STAT` and `WRITE_AT` a bare `stc`
with AX untouched (gi-6), kern_mega's real-mode bracket a bare `stc` sled.
Four error-code spaces (`FERR_*`, `DRVE_*`, `LD_*`, `FSKE_*`, plus `EMSE_*`)
reuse small numbers for unrelated meanings, but no consumer mixes families
(gfce-8). kern_mega already has the full two-tier model, and two panels with
two vocabularies (gfce-10).

**What the period systems did.**
- *MS-DOS*: its int 0 handler printed "Divide overflow" and terminated the
  *program*, not the machine; int 24h handed a critical error to a handler
  with Abort/Retry/Ignore (and Fail, from DOS 3.3) - an ACTION column; int 21h
  AH=59h (DOS 3.0) returned the extended error with its CLASS, suggested
  ACTION and LOCUS; error 1 has meant "invalid function" since DOS 2 and was
  never renumbered.
- *Mac System 1-7*: one `OSErr` number space for every manager, and one
  `SysError(id)` path drawing the bomb box with an ID.
- *Windows 3.0/3.1*: 3.0's UAE killed the faulting application and often left
  the system unstable; 3.1 added the GP-fault dialog with "Close", parameter
  validation in retail USER/GDI, and the debug KERNEL that RIPs and
  continues.
- *OS/2 1.x and Unix System V*: the two-tier split - a fault in user code is
  the process's (the trap screen and kill; SIGFPE, core, exit), a fault in
  system code is the system's (a trap screen; `panic()`).

**What os8088 already does better.** kern_mega's fault containment beats
Windows 3.0's UAE: per-task catch frames pushed by `wm_pkgcall` and the loader,
a close attributed to one instance, loop protection, a kernel fault kept fatal
(`kernel/mega.inc:2504-2560`). The death panel is safer to draw than the Mac's
bomb box. Refusal as a normal path - CF=1 with a probe and a greyed control
(SPEC.md 47) instead of a silent slow path - has no period equivalent. Windowed
DOS already banks IVT 00h per world (`apps/dos/dosw.inc:89-111`), and
`BOOT_MARK` already wrote the 4-byte stub shape (`kernel/kernel.asm:7720-7752`).

**Recommendation.**
- **Now - one fatal path for every CPU fault** (gfce-1, isr-8, histsys-2;
  WP8): generalise `sch_diepanel` into a system-error panel taking a message
  pointer and printing CS:IP; two stubs (int 0 always, int 6 when
  `[cpu_tier]` is 286 or more; never int 5, which is PrtSc on a PC; reuse
  `BOOT_MARK`'s stub shape, not its vector list, which includes 5 [fit]). The
  stub loads **DS = `KERNEL_SEG` and SS = `LOW_SEG`** before the panel,
  whose contract needs both and whose `mov sp,STK0_TOP` assumes SS
  (`kernel/sched.inc:2649-2650`, `:2699`); a package's `div` runs with DS =
  its own segment [truth]. The stub's `cli` is redundant (-1) [cost]. **+45..+60
  .text kern_big, +35..+45 kern_small, kern_mega conventional .text too**
  [cost; the first +30..+40 was isr-8's int-0-only price, without int 6, the
  CS:IP print and the new strings]. The install is **+20..+30 of transient
  room** (two `mov word [es:d16],imm16` at 7 bytes each storing `KERNEL_SEG`
  as a constant, os88ovlchk rule 2b, and a `[cpu_tier]` test), in `.ovl` on
  kern_big and `.boot2` on kern_small; kern_big's `.ovl` (45 freed by WP7,
  35 already spent) and blob (4 bytes free) are both full, so the install
  waits on WP6/WP7 or takes a two-row table [cost]. Installing int 6
  unconditionally is safe on an 8088 and saves the test. **Behaviour change,
  stated**: on an 8088 a package divide that today continues with garbage
  would halt the machine, and SPEC.md records a shipped package dividing to
  overflow on every chase tick and a kernel one in `kernel/ctrl.inc`. So this
  lands **with** the package-close item below, or the real-mode stub is
  fatal only when the faulting CS is `KERNEL_SEG` or the live cold segment and
  IRETs otherwise (8088 only) [truth]. Two DOS-side changes ride with it: the
  DOS layer gives its world its own int 0, so a DOS program's overflow says
  "Divide overflow" and ends the program rather than the desktop [truth]; and
  windowed DOS banks vector 06h too (+6 bytes of DOS.O88 table), because a DOS
  program that hooks int 6 (the usual invalid-opcode CPU probe) would
  otherwise replace the kernel's handler for every task and leave it pointing
  into a freed world [fit]. `tests/int0sweep.py` arms an interrupt-type
  breakpoint, so a kernel handler does not blind it [fit]. phase1's WP8 and
  bugs.md's BR-8/BR-9 price the same stubs; one owner counts them [fit].
- **Now - the divide fence as a ratchet** (gfce-3): a registry of every
  `div`/`idiv` site with its proof, on the `tests/textsites.txt` precedent.
  **0 bytes.** It is what keeps the next wm-1 from landing unproven.
- **Now - error CLASS, not error NUMBER** (gfce-8, abi-9): write DOS 3's
  class and action columns into SPEC.md 18.4 beside the FERR list (media and
  user action, hardware and retry, not found, exists, out of resource, bad
  format, locked, application error, internal), and one SDK sentence: "CF=1
  is the only universal refusal; AX is a code only where the cell names its
  family". **0 bytes.** The kern_small stubs that leave AX undefined get a
  defined answer now: move `dwf_dskw_write_at`'s label up onto the shared
  `mov ax, FERR_NAME` (0 bytes) and point `osapi_vol_stat` at a stub that
  answers `FERR_NODISK` (+4), which also fixes kern_small's broken
  `OSAPI_VOL_STAT` contract (gi-6; R9 [cost]).
- **Flag day, value open - the "not on this kernel" answer** (abi-9, R9).
  The first proposal, AX = 1 everywhere, does not survive the lenses: 1 is
  already a real refusal in four families (`FERR_NODISK` "no mounted volume",
  `EMSE_ROOM` returned through `OSAPI_DRV_CALL` and asserted by
  `tests/emstest`, `DRVE_DISK`, `LD_EDISK`), so on `DRV_CALL` "no EMS driver"
  and "no run that long" would become one answer [fit]; the sound stream stub's
  AX = 4 is deliberate and Recorder reads it [truth]; and turning the bare
  `stc` stubs into `mov ax,1` clobbers the AL that `OSAPI_SND_FM`'s 50
  callers keep live across a refusal [truth]. AX = 0 with CF = 1 is unused in
  every family (0 is "OK" in FERR, DRVE and LD) and is what `DRV_CALL`
  already answers [fit]; choosing it moves XREFUSE and needs a census of
  XREFUSE's AX = 1 readers. Either way the census covers callers that keep a
  live AX across a refusable call, kern_mega's FSX sled is excluded (a
  multi-byte tail can be entered mid-instruction, and calls inside a bracket
  are outside SPEC.md 53.7's legal set) [cost], and the past-the-end answer of
  theme 8's door returns the same value [fit]. **About +4..+10 resident**
  kern_big and kern_small. Until then only the doc half stands.
- **Record - a package fault closes the package, not the machine** (gfce-2,
  owner's decision): on an 8088 the CPU has already stepped past a faulting
  `div`, so the real-mode kernels can get kern_mega's two tiers for the cause
  that matters without catch frames. **+100..+120 resident in total**
  (gfce-1's ~+55 plus gfce-2's +45..+65), naming the package in the panel
  needing gfce-2's CS-to-instance scan (~+20) [cost]. Also recorded: kern_mega's
  two panels unified (gfce-10, ~-20 .mega); gfce-11's note that a hung fsx
  bracket already has an exit (Ctrl-Alt-Del), which makes video-18's latch
  (unchecked, +15) a nicety.
- **Not adopted**: a single renumbered error space (gfce-8). Its only buyer is
  one text door, its price is every FERR-checking package rebuilt, and each
  family's strings are already the one copy.

---

## 5. Validation at the boundary: hostile files owned by the reader, package pointers in a checked build

**Where os8088 stands.** SPEC.md 20.8's trust model says packages are trusted
and resident validation against them is not paid for, and the owner has ruled
on it in as many words (docs/plans/LAST-DROP-BYTES.md 7.11, applied in SPEC.md
34.11.1 and elsewhere). That line is defensible on a real-mode machine where a
misbehaving package takes the machine down whatever the kernel refuses. The
reference implementation of hostile *input* - the BPB's 17-rule table with a
per-call quotient guard at every geometry divide (`kernel/disk.inc:5025-5300`)
- is better than anything DOS shipped, and the hard-disk driver puts every
partition BPB through the same rules (hdk verdict). The weakness is that the
rest of the hostile-input line is drawn site by site:

- *hostile files*: a package whose header IMAGE size is smaller than its
  clear prefix (up to 320 bytes) wraps the decoder's output bound and writes
  ~64KB past the region (loader-1, HIGH; `kernel/loader.inc:1067`; a FILE
  shorter than the prefix wraps CX while DX still bounds the output, the half
  SPEC.md 20.13.8.1 argues is safe [truth]); the compressed read can overrun
  by up to 512 (loader-3); hostile FileSize into two divides (dw-1); a hostile
  `SYSTEM.CFG` `CH` byte steps a `.bss` byte 254 past `clk_sec` (sysmisc-2),
  one case of the settings unpack copying every mapped byte raw (gfce-6); a
  13+-byte driver volume name comes back unterminated (dr-9); the partition
  table is trusted on type and length alone (hdk-5);
- *package-supplied pointers and lengths*: `wm_ptr2idx` divides an
  unvalidated BX (wm-1); `OSAPI_WM_WAKE` writes `[bx+W_WKQ]` at IF=0 with "BX
  is trusted" (histsys-11); the entry proc's returned BX is never checked
  (loader-4); `gfx_blit4` trusts the sign of its width, sweeping 64KB from the
  336-byte `vga_pbuf` on VGA (gfx-1); packages write the record's geometry
  the SDK calls read-only (abi-7); `OSAPI_MEM_P` answers any unknown verb as
  `MEMP_AVAIL` with CF clear (hm-14); the owner fence's self-match lets FREE
  and REGROW act on the caller's own live region (hm-2, which SPEC.md 66.6.1.1
  accepts as self-harm); `OSAPI_XMEM_COPY` writes anywhere below 1MB on
  kern_mega and reaches past 0x10FFEF on kern_big (mc-17, xm-9); `HDV_ARM`
  takes the caller's segment unchecked and nothing disarms an arm whose owner
  died (hdk-1).

**What the period systems did.**
- *Windows 3.0 to 3.1*: 3.0 validated almost nothing and UAE'd; 3.1 added
  retail parameter validation at the USER/GDI door (`IsWindow` on every HWND)
  and the debug KERNEL that RIPs - which is what found most Win16 bugs.
- *Mac System 5-7*: no validation; a bad handle was a bomb.
- *OS/2 1.x*: full validation behind the protection boundary.
- *DOS*: trusted the MZ header's sizes and every BPB field.

**What os8088 already does better.** The claim table is out of band, so a
stray write cannot corrupt the allocator the way DOS's MCB headers could; the
owner word comes from the stub's ES and cannot be forged. The loader's header
fence, its re-check after the full read and teardown by owner stamp and
`W_SEG` sweep are better attribution than Win16 or AmigaOS managed. Table
dispatch is bounded wherever it was read (`ui_cmd`, `toast_say`,
`osapi_ferr_text`), and future settings bytes clamp.

**Recommendation.** The fit lens split this theme in two, and the split is
the recommendation: **hostile FILES are the threat model and are fenced in the
retail kernel; package POINTERS are trusted by ruling and are checked in a
checked build** [fit].
- **Now - make hostility a property of the reader, not of the call site**
  (gfce rethink; WP8, WP19): the BPB works because one routine validates and
  every consumer trusts proven fields. Apply the same shape to the other
  readers that own a domain:
  - the loader computes the clear prefix once in `ld_check_hdr` and refuses,
    before anything is claimed, unless both the image and the file exceed it
    (loader-1 + loader-3, **+18 .cold**, fixes a HIGH) [truth];
  - directory-entry staging clamps FileSize once at `dsk_get_dir` (**+8..+12
    .cold**), with dw-1's two guards (**+10..+12 .cold on both kernels**,
    both divides being assembled on kern_small) as the belt [cost];
  - the settings unpack gets a domain column in `CFG_MAP` whose out-of-domain
    byte **skips the store**, so the boot default stands as the map header
    promises (gfce-6 corrected: ~+22 .ovl and ~+22 CTRL.DRV, 0 resident;
    sysmisc-2's 5-byte `.ovl` clamp is its first row and competes for the
    same exhausted `.ovl` room as theme 4's install [cost]);
  - the partition table gets a status-byte, duplicate and overlap check
    (hdk-5, +14 in each HDD image and ~+20 in HDD.DRV's resident part for
    the base duplicate; 0 kernel);
  - gfx-1's `jz` to `jle` (**0 bytes, 0 cycles**, same short encoding,
    `kernel/vga12.inc:1897`, `:1899`) [cost], and dr-9 (+5).
- **Now - the window slot index stored in the record** (wm-2): `wm_ptr2idx`
  becomes a bounded load instead of a `div`, 19 bytes to 11 by encoding,
  ~-100 clocks a call [cost]; the record grows one byte (`WIN_SIZE` is
  unpublished; only `tools/os88geom.py`'s mirror moves). On kern_big that
  makes the stride odd (74 to 75), which costs a bus cycle per word field on
  the 16-bit-bus machines kern_big also runs on, so pad to 76 (**+12 .bss
  more**) or state the trade [truth, fit]. The `W_IDX` initialiser (~10-13
  bytes) lands in kern_big's and kern_mega's **blob**, which has 4 bytes
  free, and in kern_small's `.ovl` [cost]. The non-growing alternative is
  wm-1's `cmp ax, MAX_WIN*WIN_SIZE / jae` bound alone, +5 [fit]. **Net about
  +4 .text kern_big, -2 kern_small, plus the pad and the initialiser.**
- **Now - `OSAPI_MEM_P`'s unknown verbs refuse** (hm-14, +4): argued as
  forward compatibility - an older kernel answering a newer SDK's verb as
  AVAIL - which is theme 9's concern, not defence [fit].
- **Now - a checked build, not a checked kernel** (abi-15, engsys-20
  (unchecked), gfce-9): an `APICHK=1` knob - window-taking cells become
  same-length padded jump cells into a validating stub (BX in the window
  table **and aligned to a record**, `W_SEG` = the caller's DS), file cells
  check `[sch_cur]` = 0 - with one soak row driving the package sweep on it.
  **0 shipped bytes**, bounded by guard 2. **histsys-11's `wm_isrec` at the
  ISR-callable doors, loader-4's entry-BX check, hm-2's exact fence and
  dw-4's worker refusal move here** from the retail list: each exists only to
  defend the kernel from a trusted caller, which the owner's ruling declines,
  and hm-2 would also reverse an explicit SPEC.md decision [fit]. A retail
  `wm_isrec` would in any case be partial: it is bounds-only
  (`kernel/wm.inc:11655-11661`), and an unaligned in-table BX still stamps
  another record [truth]. Any of them can still go to the owner as a named
  exception with its own argument. This is Windows 3.1's debug KERNEL without
  3.1's retail cost.
- **Now - abi-7's lint** for stores to `[es:..+W_*]` under `apps/` with an
  allow-list, and a no-repaint mode for `OSAPI_WM_RESIZE` (~+10) so the two
  packages that write geometry have a legal door (Win16 `SWP_NOREDRAW`). The
  Mac's lasting regret was exposing `WindowRecord`; Carbon had to retrofit
  accessors.
- **Record** - mc-17/xm-9's ownership check on `XMEM_COPY`'s conventional
  end (+30 .mega; +12 XMEM.DRV image; HIMEM.SYS never validated it either);
  hdk-1's disarm on owner death, a `DSV_BLK` contract change.
- **Not adopted** - window handles in place of record pointers (wm-R3, refused
  by gfr-4). The breakage is countable rather than vast - **~121 field reads
  in 33 asm files**, the C reads all through one thunk family (ul-8 corrects
  "hundreds") - and a handle that *is* the validated record offset would leave
  those reads unchanged, but every inline setter cell would become a
  translating stub and the checked build gives the validation without a
  break. Re-raise only with R6 (content coordinates).

---

## 6. The memory model: claims, not handles, plus the tier every period GUI had

**Where os8088 stands.** The heap is the best-argued subsystem in the kernel:
an out-of-band record table that *is* the allocator, two-ended placement,
purgeability encoded in the owner word at zero record cost, relocation by a
callback through the holder's own dispatcher (no handle double indirection on
an 8088), one walk body for plan and run, and `mem_avail` defined as the
question `mem_claim` answers. The systemic gaps:

- **Only the kernel can own a cache.** There is no purgeable class for a
  package claim (hm-9, mem-15; `mem_pg_own` is a closed kernel list,
  `kernel/memory.inc:1318-1335`). histmem calls it "the one systemic
  capability every period GUI had and os8088 lacks".
- **No system sub-allocator.** `MEM_OWNER_MAX` = 8 (12 on kern_mega) is the
  most-negotiated constant in the package tree, and WIN16, Sheet and Weave
  each hand-roll a free list (hm-12).
- **The owner word means four things**, and the slot/segment split costs code
  in five places (mem-13); `MEM_OWNER_MAX` also caps the kernel's single
  `MEM_K_DRV` tag (hm-4); the purge floor is one word stamped per TASK while
  every callback runs on task 0, and any teardown lifts it (hm-13, files-4).
- **Aimed stores are protected by audit, not mechanism**: files-1 and files-2
  are both "the aimed destination moved or vanished during the mount".
- **The parts loader stays resident** in every non-loader parts package, once
  per instance: ~0.8-1.5KB per running instance (hm-11, corrected).
- The KB doors carried the same carry bug three times (mem-5, hm-6, mem-18).
  The lenses show the cure is kernel-internal: every shift that carried it
  lives in a shared KB helper the kernel's own callers keep (`mem_claim_x`,
  `mem_regrowp`'s CF=1 arm, `mem_claim_dma_x`), so withdrawing the package
  cells (C1) removes none of them; only converting the ~20 internal KB
  callers does, and that is Phase 1 work with no ABI [truth, cost, fit].
- **kern_mega infers descriptor ownership from geometry.** Ten kinds of LDT
  descriptor are told apart by where their base lies plus side tables, and at
  least seven walkers each re-derive which kinds they may touch; two in the
  real-mode bracket's span miss a rule the others enforce (gms-2, gms-3), and
  the move guard is blind to the package's own claims (gms-1).

**What the period systems did.**
- *Windows 3.0/3.1*: handles over a moveable/discardable global heap;
  `GlobalAlloc(GMEM_MOVEABLE|GMEM_DISCARDABLE)`, `GlobalLock` to get a
  segment, lock counts, LRU discard and `GMEM_NOTIFY` callbacks; the NE
  segment table's MOVEABLE/DISCARDABLE/PRELOAD/LOADONCALL flags; `LocalAlloc`
  as the per-module sub-allocator. In real mode it moved code with return
  addresses on the stack by walking BP chains.
- *Mac System 5-7*: master pointers (handles, double indirection),
  `HLock`/`HUnlock`, `HPurge`/`HNoPurge`, `MoveHHi`, and a `GrowZone` proc an
  application installs to free memory when a request fails; purgeable
  resources.
- *GEOS (the 8088 peer)*: a handle table for everything, blocks
  movable/discardable/swappable and locked around use, LMem chunk heaps inside
  blocks, VM files, swap drivers to EMS/XMS/disk.
- *OS/2 1.x*: LDT selectors per process, `DosAllocSeg`/`DosReallocSeg`,
  `DosSubAlloc`; motion transparent behind descriptors, and every descriptor
  owned by an explicit allocation record rather than inferred from its base.
- *DOS*: an in-band MCB chain, owner = PSP, first/best/last fit (AH=58h).

**What os8088 already does better.** No double indirection on every access,
which on an 8088 is a real per-access tax GEOS and the Mac paid; a five-level
purge rank priced in user wait, where the Mac had one purgeable bit and
Windows LRU; compaction that plans before it copies; DMA heads kept inside one
64KB page by the allocator; owner reaping at teardown (AmigaOS leaked
everything on a crash); no relocation records and no stack walking. kern_mega
already has OS/2's model - it rebases selectors instead of telling holders
(`kernel/memory.inc:2995-2998`).

**Recommendation.**
- **Now - package purgeable claims through the proc that already exists**
  (hm-9, mem-15; WP17): a `MEMP_PURGEABLE` verb appended to `OSAPI_MEM_P` (AX
  = proc), rank TRIV, the proc called with **DX = 0 = purged** after
  `mem_can_move` agrees - the Mac's `HPurge` and its `GrowZone` notification in
  one call. Two corrections make it buildable [truth, cost, fit]:
  1. *the flag bit*: `MC_DMA` bit 13 is part of the DMA head today
     (`MC_DMA_HEAD` equ 0x3FFF, every reader masks with it,
     `kernel/memory.inc:169`), so the mask narrows to 0x1FFF and the contract
     comment at `:89` is rewritten in the same diff (0 bytes);
  2. *where the proc runs*: `mem_shed_one` runs entirely inside `pushf/cli`
     and its notice is a word store, a written contract
     (`kernel/disk.inc:6097`, `:6165`), and one caller is inside `mem_regrow`'s
     single IF=0 window, which "may not call a relocation proc". So the
     package proc runs **outside the cli, at IF=1 under `sch_lock`**, as the
     compactor's `mem_reloc_call` does, with `mem_cp_busy` set across it so a
     proc that claims or regrows is refused; `mem_shed_one` skips purgeable
     package records when reached from `mem_regrow`, or their purge is
     deferred to the compactor's drop arm.

  **+35..+80 .cold kern_big** (the low end defers the purge to the drop arm
  only, the high end restructures `mem_shed_one`'s scan and done arms); the
  lenses' +35..+75 and +50..+80 bracket the same design. kern_small has no
  compactor and must refuse the verb, which is **+4..+6** because today
  `osapi_mem_p` answers every unknown verb as AVAIL with CF clear [cost].
  kern_mega either refuses as kern_small does or the purge invalidates the
  claim's selector, since a stale selector would otherwise reach a reused
  block; the second needs REALDMG-HIGHCACHE-PLAN's "a package's claim is not a
  cache and is not shed" amended [fit]. Additive, a lock update without a
  bump, but a caller needs theme 9's level first.
- **Now - one system sub-allocator in source** (hm-12b): lift WIN16's arena
  allocator into `apps/os88lmem.inc` - GEOS LMem, Win16 `LocalAlloc`, OS/2
  `DosSubAlloc` as an include. **0 kernel bytes.** It reaches the asm
  packages (WIN16, Sheet, DOS); WEAVE's grid is C and cannot `%include` an asm
  include, and WEAVE-SPEC's share-as-source rule forbids a copy, so a C twin
  or a thunk is a separate item [fit]. Raising `MEM_OWNER_MAX` to 10 on
  kern_big (hm-12a, 0 bytes) is a policy decision, SPEC.md 50.2 justifying 8;
  exempting the kernel tags from the owner cap is hm-4 (+5 .cold).
- **Now - small structural fixes**: the floor per instance, not per task
  (hm-13, +6..+8 .cold); a one-byte aimed-destination pin both `mem_shed_one`
  and the mover refuse (files RT2, **+12..+20 kern_big, +6..+10 kern_small**,
  where only the shed half applies) [cost]; "no record" reported distinctly
  so a claim skips the pointless compaction (hm-8, +6).
- **Now - parts init-drop** (hm-11): an `OP_INITDROP` option that cuts the
  region to the program after `op_load`. On kern_big and kern_mega through
  `OSAPI_PKG_REHOME`'s KEEP form (legal only from the entry proc, which is
  where `op_load` runs); on kern_small, where that cell is a bare refusal,
  through `MEMP_REGROW`'s shrink; either way the dropped body sits at the
  carve's tail with the bss chain moved below it [fit]. **-0.8..-1.5KB per
  running instance of each parts package, 0 kernel** (DOS TSR install-code
  discard; Windows discardable INIT segments).
- **Now - region movability by a header bit** (mem-17) instead of 69
  `OS88_REGION_MOVABLE` lines in 67 files: **+15..+20 kernel** (`.cold`
  mostly), **about -19 resident per running instance**; the "~-1KB of disk"
  was unpacked image bytes, and after lz4 and cluster rounding the floppy
  saving is often 0 per file [cost]. CLAUDE.md's rule, `t_movable` and
  `tests/movable.txt` change in the same diff [fit]. Bank the file's size and
  first cluster so a lazy part is not re-fetched from a different build of the
  same name (hm-7, +20 per parts package).
- **Now, kern_mega - an explicit kind per LDT entry** (gms-R1): a kind byte
  per entry in place of ownership inferred from geometry plus side tables, the
  OS/2 shape. **~+600 tail data above 1MB, -100..-200 .mega code, 0
  conventional** (ESTIMATED); gms-1..gms-5's local fixes first, most of them
  saving `.mega` bytes; `OSAPI_MEM_SUBSEG` as an adapter over `OSAPI_SEG`
  (gms-R2, -150 .mega now, the cell retired at a flag day).
- **Now, prototype first** - keep `mem_tab` sorted by base so every walk is
  linear and best fit is free (mem-14, -30..+20 .cold, blocked on kern_mega's
  index-keyed selector pairing).
- **Phase 1, then flag day - C1** (the KB doors withdrawn). The correctness
  half is now: convert the kernel's internal KB callers to paragraphs and take
  mem-5/hm-6's +3 fix whatever happens to C1; there are three
  package-reachable doors, not four, and two are already guarded, and md-16
  is kern_dos's private shim, which no package reaches [truth, fit]. The
  flag-day half is **-45..-69 kern_big and kern_small** (the six cells are 36
  bytes of `.text`, 12 under theme 8's door, plus ~57 bytes of `.cold` door
  bodies MEASURED in `build/kernel.map`; the -45 omitted the bodies) and **-12
  kern_mega conventional** [cost]. The package side goes the other way: a
  converted site gains a verb and a bank, **+0.9..+1.7KB unpacked inlined, or
  ~+0..+150 with a per-package helper**, the same arithmetic gfr-7 used to
  refuse C3 [cost]. The -45..-69 is conditional on theme 8's door, which the
  owner has declined once; the non-door figure is ABI-BREAK-LIST's ~-50..-70
  [fit]. mem-13's owner = instance slot (-20..-40 .cold) rides it only if the
  Task Manager is touched anyway.
- **Record** - handles at the API (hm-10 / R2-6): gfr-4 corrects the "one
  conversion, not two" argument (C1 converts about 221 claim/avail/regrow
  sites; a handle touches 367 FREE/MOVABLE sites, +2..+4 bytes each), and
  kern_mega's "several hundred" bytes are `.cold` above 1MB. hm-2's fence buys
  the correctness half in the checked build (theme 5). Also recorded: recency
  inside a rank (mem-16); shared code between instances (hm-15) and the
  decoded-image cache (theme 13); XMS handles before the 8-block pool grows
  (mem-21 (unchecked)); xm-6's owner 0xFF shared by the kernel and every
  driver, never swept.

---

## 7. Residency and lifetime: a third lifetime, init-lifetime code, and class code off the gauge

**Where os8088 stands.** On-demand modules are one of the tree's best ideas
(SPEC.md 2.8): kernel code shipped as a file, stamped with the kernel's own
layout, read into a claim when asked for and freed when done, and gated by
`t_moddrop`. The loader is small and refuses unsafe states (about 378 `.cold`
+ 179 `.text` + 48 `.bss` for 11 rows on kern_big; it disarms before it
frees, and an unloaded slot is `stc/retf` rather than a far call to 0:0)
(rt strength). The boot blob rejoins the heap floor, so boot-only code costs
no resident byte. The VGA slide is nearly free resident. Class code already
leaves the 8088 through owner-loaded overlays: XMEM.DRV was "reserving about
1.2KB of kernel image forever for a feature it can never reach"
(`kernel/xmem.inc:1-30`). What is missing at the model level:

- **Only two lifetimes.** A byte is resident or a module, and every module use
  is a disk read. Every desktop gesture and every Sticky Apps menu press loads
  and drops a module that the shell's own comments call too slow elsewhere
  (shell-2; MOD_SETS is 3,180 bytes MEASURED, not 2,206, and SPEC.md 110.12
  prices the load at ~2-3 calls, about a second; `kernel/desksc.inc:437-462`,
  `kernel/stikmenu.inc:60-88`), and kern_big carries FILECP and FDLG resident
  because ONDEMAND-PLAN refused the swap. The pin a purgeable module needs is
  designed and unbuilt, by a recorded decision (SPEC.md 2.8.3).
- **Residency is shaped by build, not by machine**, outside the VGA rung: the
  PS/2 runtime is ~265 resident bytes on every kern_big machine although its
  init is already `.ovl` (rt-3); the keyboard mouse is 456 bytes resident on
  every machine although `[mou_ptr]` is set by every backend's settle and
  never cleared, so after any pointer settles it is dead for the session
  (rt-7); `font_run_x`'s once-per-run VGA prologue is resident on mono
  machines (text-7); the mono renderer is resident on VGA-only machines
  (video-17, ~1,108 bytes MEASURED span).
- **`KERN_BUDGET` is a VGA reading, and the target machine's figure is never
  printed** (rt-5): rule 1 is stated on VGA and `tests/kernresident.py` boots a
  VGA desktop, so wins on the 8088 mono target are invisible to the gauge
  (the slide's 2,032 bytes, text-7's -165..-185 which shows as +16).
- **Lifetimes by hope, not by count.** One global `drv_wcnt` stands in for
  per-driver worker counts, so since USBMOUSE.DRV keeps a worker for life,
  unloading any other driver stalls ~5 s (91 ticks) and leaves it detached but
  published (drivers-1, HIGH; NO-PINNING K1b deferred per-row counts "until a
  second driver spawns a worker", and USBMOUSE now does [fit]); nothing counts
  who is *inside* a driver (drivers-2); the Sticky App library's lifetime is
  keyed on a proxy (`IF_DESK`) instead of on who bound it (shell-1).

**What the period systems did.**
- *MS-DOS*: COMMAND.COM's transient portion at the top of memory,
  overwritable, reloaded from `COMSPEC` with a checksum and a disk prompt;
  TSRs keeping only their resident paragraphs; OS/2 1.2's mini-FSD, a
  disposable boot-time reader.
- *Windows 3.0/3.1*: discardable code segments reloaded through INT 3Fh
  thunks in real mode, with lock counts; one DISPLAY.DRV / KEYBOARD.DRV /
  MOUSE.DRV per machine, every primitive a far call; installable drivers
  with open counts.
- *Mac System 5-7*: purgeable CODE and other resources; `LoadSeg`/`UnloadSeg`.
- *AmigaOS*: libraries and devices with open counts, `Expunge`d lazily at open
  count zero when memory runs low.
- *GEOS*: almost everything outside a small fixed kernel in movable,
  discardable resources; one video driver resident per adapter;
  `GeodeUseDriver`/`GeodeFreeDriver` reference counts.

**What os8088 already does better.** Modules are assembled with the kernel and
need no export table or fixups; the module ABI is versioned by construction
(build and layout stamps, build-time-proven `mod_cfix`). Explicit
`op_fetch`/`op_drop` and `mod_need`/`mod_drop` are the right policy for a
one-floppy XT, the Mac's `UnloadSeg` honesty (see "would NOT copy"). The
`.vga` slide achieves GEOS's one-renderer residency with near calls, and the
drop gate makes caching a one-site change rather than a pin per thunk (rt
strength).

**Recommendation.** gap-residency-tiers priced the residency findings
together and arrived at a smaller and cheaper design than "a generalised rung
plus a purgeable tier" (WP2, WP3 as amended).
- **Now - drop to a cache: the need/drop bracket IS the pin** (rt-1, rt-9;
  supersedes HEAP-UNPIN piece B's per-thunk pin, which was priced on 29
  far-call sites; there are 49 today through 14 slot blocks). `mod_drop`
  disarms as today, then re-tags the claim as a purgeable module tag;
  `mod_need`'s hit path re-tags it pinned, **re-zeroes the module bss from a
  new kernel-private `MOD_H_BSS` header word** (today every load is a read, so
  bss is zero by construction), re-arms the slots from `[MODR_SEG]` and
  **skips `mod_cfix`** rather than rescanning bytes it already fixed (its
  idempotence on a re-adopt is unproven) [rt-1 verify]. `mem_pg_forget` gains
  a `.mod` arm that disarms before it zeroes the word; `mod_tab` splits into
  two word arrays so one `mem_pg_own` row names every image. Purge ranks per
  module, not inherited (rt-9): the Control Panel, Format, DISKOPS, HIBER,
  DESK and STIKMENU are wanted with the system disk at hand, so LOW; FILECP
  and FDLG are wanted with the user's disk in the drive, so the HIGHEST rank,
  shed after every package cache. **+60..+85 .cold kern_big**, +2 header
  bytes per module image (not resident); on kern_mega, `mod_hi` images above
  1MB are freed through `mega_hifreef`, so the cache is conventional-heap only
  there unless that arm gains a pool purge. This overturns SPEC.md 2.8.3's
  recorded "the heap never does", whose reason survives because only a
  *dropped* image is purged; SPEC first [fit]. It answers shell-2 for every
  module and is ONDEMAND-PLAN 7.2 with 7.1's pin already supplied.
- **Now, owner's decision - FILECP and FDLG on kern_big as modules cached at
  boot** (rt-2, rt-6, bytes-3 corrected; WP2). **bytes-3 overstated the
  saving**: its 2,080-byte fdlg span is fdlg's own 987 plus **1,089 bytes of
  the kernel's `os88ui_*` copy** (`os88ui_btn` at 0x17A64 to `fdf_fdlg_open`
  at 0x17EA5 in `build/kernel.map`, re-measured here), which stays resident for
  `files.inc` (SPEC.md 38.0). The movable part, net of the resident halves
  kern_small already keeps, is FILECP 1,657 and FDLG 911, **-2,568 MEASURED
  spans**. Keep both images in KERNEL.SYS's contiguous read after `.cfx` (the
  bytes are already in KERNEL.SYS today as `.cold`, so the read does not
  grow); claim and check them at `mem_unblob`, then `mod_drop` them into the
  rt-1 cache. With +60..+100 of boot glue and the swap prompt, and rt-1's
  +60..+85, **about -2,380..-2,450 resident kern_big** where `.cold` lives in
  conventional RAM; **0 where a ROM or COLDHMA.SYS adopted `.cold`** (`ROM_ADOPT`
  is the shipped default), so the boot step is skipped when `[api_coldseg]`
  is at or above A000h, and on those machines the dialog loads from ROM with
  no disk [truth, fit]; **0 kern_mega conventional**; about +4KB of purgeable
  claim and ~+4 clusters per kern_big system disk. Three costs are stated
  rather than hidden [truth, cost, fit]:
  1. **the system-disk trade**: once these claims are shed under pressure, a
     kern_big Save As or copy on a one-drive XT asks for the system disk with
     the data disk in the drive, which kern_big has never done and which is
     ONDEMAND-PLAN 1.2's objection word for word ("The chooser cannot be the
     thing that costs you the disk you are choosing on"). The highest purge
     rank narrows it; the formatter's existing swap prompt (FS_EDIT mode 6)
     answers it the way COMMAND.COM's transient reload did; whether that is
     acceptable is the owner's, against KERN-SMALL-MODULE-SPLIT's "Big needs
     those to stay fast". A row is owed: "shed FILECP under pressure, then
     Save As";
  2. **FDLG on kern_big is not kern_small's body**: it has kern_big-only arms
     (`FDH_RECT`, `dskw_czsize`, DESK.DRV's key hook, `fm_btn1`), so a
     kern_big module arm is new seam work, and one arm for both kernels is
     rt-6's unification [truth];
  3. **the transient and the time**: ~+4KB more unpacked at boot on the 128KB
     machine (guard 5), and +140..+350 ms of boot read and copy, ESTIMATED;
     a reload after a shed is 1-2 int 13h calls (~0.4-0.8 s) [cost].

  Caching them on kern_small too (rt-6) would let the 128KB machine open Save
  As on a data floppy until heap pressure, at rt-1's +60..+85 against its
  diet: the owner's trade.
- **Now - the keyboard mouse is init-lifetime code: hold it from boot, drop it
  at settle** (rt-7, input-12 inverted; WP3). Ride the image in KERNEL.SYS's
  read (rt-2's mechanism), arm it as a held module, and `mod_drop` it **beside
  every `[mou_ptr]` store**: `mou_settle` exists only on kern_big, kern_small
  inlines `MOU_SETTLE_STORES` at its one settle site and kern_emu's
  `vmm_boot_x` stores the byte itself [rt-7 verify]. A machine with a mouse
  regains the bytes seconds after boot; a machine without one keeps the
  claim and never needs the disk, so `tests/small128.py` must allow that one
  pinned claim. **-290..-380 static resident on both kernels and kern_mega
  conventional**, ESTIMATED (the `kbm_*` `.text` is 599 on kern_big and 550 on
  kern_small, MEASURED; the low end carries the new `mod_tab` row and its
  4-byte slot, the post-to-UI path and the drop [cost]); +~400..+450 module, a
  cluster on every system disk. `kbm_isr` and the keypad-5 latch stay
  resident: `kbm_isr` is IRQ1's handler for the whole kernel. This replaces
  load-on-first-press, which fails CLAUDE.md's module test at exactly the
  moment the mouse has failed and, on a pointerless 128KB machine, *raises*
  RAM in use by +85..+165 once loaded [truth, cost, fit].
- **Now - the PS/2 runtime as an owner-loaded overlay, not an AT rung** (rt-3,
  supersedes bytes-2's rung). The mouse boot loads `PS2MOU.DRV` (a
  `DRVC_OVL` image, XMEM.DRV's shape) when `[cpu_tier]` is 286 or more and an
  aux port is reported; its IRQ12 handler builds the packet and applies it
  through `OSAPI_MOUSE_FEED`. **About -220 resident on every kern_big machine,
  and the VGA gauge sees it** (-265 MEASURED spans, +30..+45 of fronts and
  fence); a 286 with a live PS/2 mouse pays 0.5-1KB as a heap claim; 0
  kern_small. bytes-2's rung saved only on an 8088 with neither VGA nor EGA,
  0 on the gauge and 0 on the owner's 8088+VGA 5150 #2.
- **Now, owner's call - `font_run_x`'s VGA prologue into `.vga`** (text-7):
  **-165..-185 on mono kern_big, +16 on the VGA gauge** (two `jcc`-over plus
  near-`jmp` doors, 5 bytes each, and the `.vga` jumps back [cost]; text.md's
  -165 and WP3's -185 differ by what is counted as the edge arm [fit]). Per-run
  VGA cost ~30-40 clocks, under 1% of the ~4,300-cycle prologue. The parity
  pad and `t_hotpar` move with it.
- **Now - report residency per machine class** (rt-5): `kernsize` prints a
  classes line (VGA; EGA less `.vgabuf`; mono with the slide less `.vga` and
  `.vgabuf`; ROM/HMA adoption) from constants it already reads, and a soak
  twin of `kernresident` runs on a `_herc_gla` machine. **0 shipped.** Whether
  to gate the mono figure is the owner's call.
- **Now - lifetimes by count**: per-row worker counts in a `DRV_MAX`-byte
  table, keeping the global `[drv_wcnt]` that `kernel/memory.inc:2143`'s
  "ANY driver worker exists" test reads, and on timeout clearing the class's
  publication (drivers-1); a bounded wait for `[wm_tcnt]` = 0 before DETACH,
  refusing with `DRVE_BUSY` (drivers-2). **+43..+49 .cold, +11 .bss** [cost].
  Library lifetime by binder: an `IF_LIB` bit set by the LIB verb and tested
  by `desk_drop_x` (shell-1, 0 resident, +4..+6 DESK.DRV; AmigaOS
  `OpenLibrary`/`CloseLibrary`).
- **Record - do not generalise the rung** (rt-4): `cfx_slide` moves only
  `.cold`, down by one delta, and `.vga`'s labels are fixed `KERNEL_SEG`
  offsets that `.text` near-calls, so a rung can be given back only as a
  contiguous suffix directly under `.cold`. CPU class and adapter form a
  lattice, mono and VGA are complementary, and whichever sits lower can never
  be dropped; the 1bpp path is a direct `jb` to a fixed address on the hottest
  path, so a general mechanism would need a relocating loader. The working
  split is **one rung for planar hot code, `DRVC_OVL` overlays for class code
  on a cold cadence, and modules for features**. Write the nesting rule into
  SPEC.md 2.10.9 and price any second-rung proposal against it. video-17's
  mono rung on planar-only machines (-1.1..-1.5KB on VGA kern_big) stays
  record for exactly this reason.

---

## 8. Dispatch and the call surface: an index for rare cells, a class in the package

**Where os8088 stands.** The surface's mechanics are the best part of the
kernel and beat every period peer on an 8088: direct far calls into a fixed
table with no relocation and no loader fixups; three cell shapes chosen by call
frequency (the inline cell measured at 52-57 cycles against 159-162 for a
SLOT); callbacks that are plain near procs because the kernel loads DS from
`W_SEG` (Win16 needed `MakeProcInstance`); ES = `KERNEL_SEG` on entry to every
callback; `t_api_abi` and the fingerprint lock. What lags is the model around
them:

- **The rare half pays for fixed addresses twice**: 112 rare cells at 6 bytes
  each, the same 112 on kern_big and kern_small and 119 on kern_mega (decoded
  from all three `kernel-full.bin` images [cost]), in a table that is
  0x10..0x4FB, **1,259 bytes MEASURED, on all three maps**; and no way to
  refuse a cell the kernel does not have (bytes-1).
- **The window record holds 13 handler words** on kern_big and kern_mega (W_PAINT,
  ONKEY, ONCLICK, ONSIZE, ONMOUSEUP, ONDRAG, ONTIMER, ONSZ, ONWK, ONCL, ONRC,
  ABOUT, ONWH; `kernel/wm.inc:26-127`), **312 bytes of `.bss` over 12
  windows**; 10 words over 6 windows on kern_small. No package reads a handler
  word through the record; they go through ten setter cells, all of them
  5-6-byte inline cells (~55 bytes on kern_big) [cost]. The handler words are
  interleaved with contract-readable fields (`W_DISP`/`W_SEG` at 20-22,
  `W_TIMER` at 32, the `W_SIDE` block) [fit].
- Verbs arrive in AL, AH or BL depending on the door; names collide across
  subsystems (three meanings each of CLIP, FS and DESK; abi-12, abi-13
  (unchecked)).
- "Preserve every register" is re-derived by ul-1: resident push/pop is
  4,633-4,791 bytes without CS/SS; entry callee-saves are 2,459 bytes in 612
  routines; **79 saves (127 bytes) are dead at every direct caller and only
  ~93 bytes are removable** (the rest exit through the shared `kret` ladder).
  **The "AX is scratch" convention is refuted on evidence**: 247 of the 512
  bytes of AX entry-saves are in routines whose 226 caller sites read AX
  after the call, so the change would move saves to callers, not delete them.
  C callers already treat everything as clobbered (abi-17).

**What the period systems did.** Every generation of 8086 system software that
had to be both small and versioned converged on a call site that NAMES a
function rather than JUMPING to it (bytes rethink 1): DOS's AH with one table
word per function and an unknown-function refusal; the Mac's A-line trap word
through a RAM trap table (2 bytes at the site, `_Unimplemented` for absent
traps); AmigaOS's 6-byte library vectors at negative offsets with
`SetFunction`; Win16's ordinal imports resolved at load, a missing import
failing the LOAD. For callbacks: Win16 `WNDCLASS.lpfnWndProc` once per class,
styles at `CreateWindow`, `SetWindowLong` for runtime changes; GEOS class
method tables; OS/2's `DosVerbNoun` naming and the Pascal convention (AX-DX
and ES volatile).

**What os8088 already does better.** No fixup pass, so a 4.77MHz load stays a
read; per-call cost ~5x below DOS's int 21h; the package dispatcher does once
what `MakeProcInstance` did per callback; the per-event vector means a package
pays nothing for events it never asked for, which is why the class table below
keeps vectors rather than adopting a message switch. No trap patching by
design.

**Recommendation.**
- **Flag day, the owner's decision - T5, the rare-cell door** (bytes-1, gfr-1).
  Rare cells behind one software-interrupt door with an inline index byte
  (`int nn` / `db idx`, 5 bytes to 3 at the site); hot SLOTs and inline cells
  unchanged. It supersedes T1 and K17-1, both of which the owner declined
  ("an ABI break on main"; "refused once"), so it goes to the owner as a
  flag-day decision at about twice the cycle price, not as a default [fit].
  **-325..-390 .text on kern_big and on kern_small** (the same 112 cells; the
  first synthesis's -240 for kern_small had no derivation [cost]), **about
  -355..-415 kern_mega conventional** (119 cells); the range is the door's
  unbuilt size (see *Where the lenses disagreed*). **-2 a rare package site:
  2,641..2,767 sites by two censuses, ~-5.3..-5.5KB unpacked.** **+150..+300
  clocks a rare call, 31-63 us, PREDICTED**; a rare call holds two more stack
  bytes for the body's life, and IRQs stay off ~100 clocks until IF is
  restored [cost]. `OSAPI_SEG`, `OSAPI_FS_OP`'s step, `OSAPI_GFX_FILL_GRAY`
  and `OSAPI_WM_CURSOR` are read for frequency before they are left rare.
  What the door must get right [truth, fit]:
  1. *the vector*: being outside windowed DOS's exchange set does not protect
     it, it makes it shared - DOS's `AH=25h` writes the live IVT for any
     vector, so a windowed program that hooks the door captures every
     package's rare call on the whole desktop. Virtualising the door vector in
     DOS's `AH=25h`/`AH=35h` paths, windowed and full-screen, is part of T5.
     The vector cannot be in 40h-5Fh (BIOS data and tables), 60h-80h (the DOS
     packet driver takes the first free vector there, `apps/dos/dospktabi.inc`),
     67h (EMS) or 70h-77h (the AT slave PIC; the kernel hooks 74h), which
     leaves 81h-FFh; name it in SPEC.md and in the interrupt guide's anchors;
  2. *IF*: restore it by pushing the stacked FLAGS and `popf`, the form the
     critical-section rule allows, not a conditional `sti`; on kern_mega use a
     trap gate, which leaves IF alone;
  3. *the bodies*: each starts with the cell-specific `pop bp / mov bp,[cs:bp]`,
     so the door enters past it, and `api_x.done` is shared with every hot X
     cell, so rare X needs its own `retf 2` tail (~+13) and `api_rs.tail`
     becomes `retf 2` (+2), after which no non-door path may enter it;
  4. *refusal covers rare cells only*: a newer HOT cell called on an older
     kernel is still a wild far call past the table end, so theme 9's level is
     still needed for hot cells;
  5. *kern_mega*: the real-mode FSX bracket today refuses every non-kept cell
     by filling the table with `stc/retf`; under T5 it must also point the door
     vector at a refusal ending in `retf 2`, and the IDT (built from the IVT)
     needs the gate specified.
- **Now, only after the vector is chosen - rehearse the door on a private
  ABI** (gfr-2). Opening it first to the kernel's own `cw_` shims costs nothing
  to abandon, but it is **not** ABI-free as first stated: it makes kernel-private
  dispatch depend on an IVT vector, which DOS.O88 must then virtualise in the
  same change [fit]. The census is smaller than first counted: 45 real shim
  bodies and 23 zero-byte aliases (21 `cw_X equ apic_X`), and the drawing shims
  (`cw_gfx_lock`, `cw_gfx_unlock`, `cw_gfx_fill`, `cw_gfx_frame`,
  `cw_font_run`, ~46 call sites) stay direct far calls because a repaint calls
  them by the dozen (`kernel/kernel.asm:7811-7816`). **About -40..-60 .text
  kern_big**, plus -2 at each converted site in `.cold` (resident) or in
  modules and `.ovl` (not resident) [cost, fit].
- **Flag day - the window class in the package** (abi-11, histsys-9, bytes-6;
  gfr-3, R14): one class pointer per record (a length byte, a style word, the
  handler table), the creation-time flags becoming the style word, K17-2
  surviving as one runtime `OSAPI_WM_FLAG` door. **About -300 kern_big (range
  -260..-360: -288 `.bss` for 12 words a record, ~-55 for the ten inline
  setters, which are not rare cells and save that much with or without T5, and
  +~80 of dispatch and tables), about -350 kern_mega conventional, -95..-120
  kern_small** [cost]; the first -215/-95/-270 counted 10 words. In packages,
  +~1.3KB of class tables replacing ~2.6KB of setter code. **+40..+90 clocks a
  callback PREDICTED** (a `W_CLASS` load, a length-byte bound check and the
  entry load, plus a near call if the lookup is a helper), not +20 [truth,
  cost]. Three requirements: kernel windows' class tables are read through the
  live cold segment word `[api_coldseg]`, which the slide moves, not through
  `COLD_SEG` or `W_SEG` = 0 [cost, fit]; kern_small's refusal signal survives
  - `OSAPI_WM_ONTIMER` and `OSAPI_WM_ONDRAG` answer CF=1 there and packages
  test it (`apps/1942/1942.asm:56-60`), so `OSAPI_WM_CREATE` answers how many
  class slots this kernel dispatches, or the class's slot count is checked,
  for a few bytes, or a package arms a timer that never fires, the silent
  failure SPEC.md 47 forbids [fit]; and the length byte covers every slot the
  largest kernel reads, so a short table cannot silently drop ONWH [truth].
  The offsets that move are legal to move at a `PKG_FMT` bump, but take
  abi-7's `W_*` reader census first [fit]. Six of T2-2's ten kern_mega ICW
  stubs go with it.
- **Flag day - the call macro and free renames** (abi-12, abi-13, gfr's T6).
  T5 forces every call site through one macro, which makes renames and
  verb-register moves free in that one rebuild. The rewrite is 0 binary bytes
  (6,959-6,998 `call OSAPI_` sites, no `jmp`/`dw` uses, so `cmp` proves the
  images), but **not 0 work**: `t_textrules`, `t_movable`, `t_stkclass`,
  `t_megahigh`, `t_pins`, `t_drvclaim`, `t_toast` and others parse the
  literal `call OSAPI_X` and would go vacuously green - `t_textrules`, whose
  count "can only go down", would pass while seeing nothing - so each learns
  the macro and is broken on purpose in the same commit [truth, fit];
  macro-wrapped sites (`call %1`) are invisible to a textual rewrite, so
  `os88pkg`'s far-call scan refuses any surviving far call to a rare offset
  before the flip [cost]; and after the flip a rare name must stop being a far
  address, so out-of-tree source fails to assemble rather than calling into
  the renumbered table [truth]. A cheaper road is worth checking first: keep
  `call OSAPI_X` everywhere and overload the mnemonic once in `os88api.inc`
  (`%imacro call 1` guarded with `%ifid`, testing a per-cell kind), which
  touches no package source and keeps every gate's regex valid, if NASM's
  warning for an instruction-overriding macro does not trip `-w+error` [fit].
  Now: a rule for new doors (verb in AL unless AL is an argument, then BL; a
  `DosVerbNoun`-style name). 0 bytes.
- **Flag day - V1 as a separate clobbering entry** (gfx-16, abi-17): kernel
  callers depend on `gfx_fill` preserving registers, so V1 is a package-facing
  entry, C SDK first. -2 resident, -120 clocks a rect call.
- **Record, convention refused - the scratch-register change** (bytes-5,
  engsys-19, ul-1). Take the ~93 provable bytes inside the next size pass;
  build `tools/os88live.py` over a per-build listing (modelling fall-through
  entries, the `kent`/`kret` ladders, the API table and tail jumps) as a
  **correctness lint** over the `clobbers:` headers, run as a soak row. It
  does not pay as a size pass, and the Win16/Mac Pascal convention is not
  adopted.

---

## 9. Versioning and capability discovery: a level, now, in bytes that are already free

**Where os8088 stands.** `PKG_FMT` is an equality test (`ld_hdr_ok`,
`kernel/loader.inc:292-296`), the right major number. There is no minor: a
package cannot learn at runtime which cells this kernel has, so a new package
on an old kernel with the same `PKG_FMT` calls past the end of the table
(abi-6, loader-6). SPEC.md 20.2.0.1's "What it does not cover" names exactly
this gap. The private ABIs (`VX_ABI_VER`, `XM_ABI_VER`, `HD_ABI_VER`) are
versioned by hand and outside the lock (drivers-16); the C SDK's `os88.h` and
`os88thunk.asm` are outside both `t_mirror` and the lock (engsys-11).
kern_mega's package arms have no way for a converted binary to declare itself
(mc-19 (unchecked)).

**What the period systems did.** GEOS geodes carried a protocol number per
imported library - major must match, minor must be at least the one built
against - checked at load; AmigaOS `OpenLibrary(name, version)` refused a
too-old library; DOS had int 21h AH=30h; Win16's NE header carried an
expected-Windows version, and ordinal imports failed the load naming the
missing entry; the Mac had `Gestalt` (System 6.0.4) for capabilities; Windows
3.0's NE `PROTMODE` flag let a converted binary declare itself.

**What os8088 already does better.** The ABI lock with a written reason for
every fingerprint move and a binary decode of the table are stronger than any
period project's discipline; "refusals, not holes" and greying a fact
(SPEC.md 47) are Gestalt's discipline applied per control. The deliberate
renumbering in alpha (SPEC.md 20.8) is right for a tree that rebuilds every
caller.

**Recommendation.**
- **Now - publish a LEVEL, not an address** (gfr-5, A4; WP9): `dw
  OS88_API_LEVEL` at **`KERNEL_SEG:0006`** - written that way, never as a
  literal 0060, because L1 moves `KERNEL_SEG` [truth, cost, fit]. 0x06-0x07 is
  padding today (`kernel/kernel.asm:2349-2352`): **0 resident**. `os88pkg.py`
  writes the needed level into package-header byte 31; `ld_check_hdr` compares
  `<=` and answers `LD_EVER`: **+7..+12 .cold** kern_big and kern_small, 0
  conventional on kern_mega, where `.cold` is above 1MB. Three corrections:
  1. *where the compare goes*: in `ld_check_hdr`, never in `ld_hdr_ok`, which
     is shared with the driver loader and the re-home path, and in a driver
     header byte 31 is the bss paragraph count, nonzero for most drivers
     [truth, cost];
  2. *byte 31 is the NUL of a legal 15-character name*: `os88pkg.py` must cap
     names at 14 when it stamps a level and exempt byte 31 from its
     after-terminator check, and SPEC.md 20.2's header contract says 14. No
     in-tree name is longer than 13 ("Desk Calendar", "Pixelstein 3D"); the
     kernel's readers are already bounded at 15 (`inst_set_name_x`, the
     loader's 12-byte `LD_NAMELEN`), and host readers that split on NUL
     (`os88disk.py`, `os88pkgsize.py`, three test rows) are covered by the
     cap rather than changed [truth, fit]. Byte 31 is visible to an old
     kernel's Sticky caption length (gfr verify), so "ignored by old kernels"
     is nearly but not quite true;
  3. *one design going forward*: phase1's WP9 still publishes `dw
     osapi_table_end` and keys `OS88_NEED` on a cell address; it switches to
     the level, because theme 8's door takes rare cells out of the table and
     every `OS88_NEED` compiled against a table end would change meaning at
     the flag day [fit].

  The level has one source of truth, shared with The Wire's
  `WIRE_CELL_LEVELS` (`tools/os88wire.py`), and `t_abilock` fails an appended
  cell that does not raise it, or the level drifts the way bumps did before the
  lock [fit]; after T5 it is computed from the `int`/index sites as well as the
  far-call immediates [cost]. `OS88_NEED level` costs ~8 bytes in a callback
  (`cmp word [es:6],L / jb`, ES = `KERNEL_SEG` there) and ~13-15 where ES has
  to be loaded [cost]. The open caveat is closed: the mouse-instrument word
  left 0006 before `PKG_FMT` 10 arrived (b47de145, 2026-10-14), so every
  `PKG_FMT`-10 kernel reads 0 there and refuses `OS88_NEED` as designed; the
  two lenses cite different commits for the move (2e8e2924 and 2ae27476) but
  agree it predates `PKG_FMT` 10 [cost, fit]. **abi-6's flag-day half
  dissolves**.
- **Now - the PM-clean declaration, in a free flag bit** (mc-19 (unchecked),
  megacore rethink 4, L7). Not header +15 bit 5: `LD_H_CLASS` is a 6-bit
  index (`and bx,0x3F` at `kernel/instance.inc:2107`, asserted by an
  `%error` in `kernel/sched.inc:2891-2893`), so a bit-5 package launches with
  the largest stack class on every kernel shipped so far, and `os88pkg.py`
  refuses it outright (`a[15] & 0x3F > STK_CLASS_MAX`) [truth, cost, fit].
  Use a free `LD_H_FLAGS` bit (bits 1-4 and 6 are unassigned,
  `kernel/loader.inc:88-93`) [cost]. `mega_gpw` then *closes* a clean package
  on a CS store instead of fixing it up, and `t_megapkg` ratchets the census:
  **+12 .mega**, packages only. Drivers need a bit of their own, because a
  driver's +15 is `DRV_H_DSV`, whose 36-byte default already sets bit 5; that
  is a `DRV_VER` contract and rides D8 [cost]. The bit's contract names the
  fault classes it forbids: `mega_gpw` also completes legal writes through
  ES = `KERNEL_SEG` on callbacks (Paint, `os88stik.inc`), and package BDA and
  IVT reads need an SDK path before a clean package can forbid the
  segment-0 fixup [truth, fit]. It is the staged road to L3 that does not
  wait for The Wire to serve two builds.
- **Now - every ABI under the lock** (drivers-16, engsys-11): fingerprint
  `vxabi.inc` and the XMEM/HDD ABI blocks against their versions, and add
  `os88.h` / `os88thunk.asm` to `PKG_FMT`'s inputs (Microsoft's H2INC, which
  generated the MASM include from the C header, made the two unable to
  disagree). 0 bytes; an `--update` without a bump.
- **Record** - at a future header change, one bit for "an extension list
  follows the clear prefix" (type, length, data), the NE/PE idea, so future
  blocks take no bit (abi-16, ~+20 .cold; abi-16's "the header has no room"
  is refuted for now by byte 31 and the free flag bits).

---

## 10. The kernel-driver contract: read in place, a request block only where registers ran out

**Where os8088 stands.** The driver layer is one of the most carefully built
parts of the kernel: a header matching a package header, a publication slot
per class, the service table copied so IRQ0 reads it with no segment load, a
length byte that makes short tables safe, the attach half and the
`SYSTEM.CFG` parser in images. The hard-disk driver's kernel face is better
defended than it first appears: every partition BPB goes through the kernel's
rules, and its 32-bit `HDV_LEN` fence is the one bound that knows where a
partition ends (hdk verdict). The model-level costs:

- seven 36-byte copies of a union service table in resident `.bss` on
  kern_big, **252 bytes MEASURED** (`drv_svc` B0FE..B1FA), of which the
  sound-only cells are copied for every class (histsys-7, drivers-9);
  **kern_small has none** - `drv_svc` and `drv_owner` are absent from
  `build/smallk/kernel.map`, checked here - so the first "~-36 kern_small"
  was wrong [cost];
- a register-only verb ABI for block and file service that has run out of
  registers and produced three dispatch bodies (histsys-8);
- a class that is both a service type and a single slot, so two drivers of one
  type are mutually exclusive (drivers-15);
- the driver names compiled into the kernel as a row table (histsys-12,
  corrected by ul-3: the titles are already not resident, so the saving is
  -100..-180, not -200..-350, and it overlaps drivers-10's -95; discovery
  costs ~5 s on the XT, gfr-6);
- `SYSTEM.CFG` drops records it does not know at every save (drivers-17);
- state copied across the hard-disk seam through six verbs instead of
  published once (hdk-11), and transport retries missing (rung 0 fails an
  ECC-corrected read, status 11h; rung 1 never retries; hdk-3, hdk-4).

**What the period systems did.** DOS: the `DEVICE=` chain, strategy/interrupt
entries and a request header at ES:BX, `INIT` returning the break address.
Mac: the Device Manager's unit table, `Open/Prime/Control/Status/Close`, a
ParamBlock with `ioResult` and `ioCompletion`, and cdev Control Panels. OS/2
1.x: request packets through a strategy routine, `DevHlp` services, init code
discarded. Windows 3.x: installable drivers (`DriverProc`), MMSYSTEM device
IDs (many devices per type), `SYSTEM.INI` keeping unknown keys. GEOS: drivers
as geodes with reference counts and a refusable `DR_SUSPEND` with a reason.

**What os8088 already does better.** Load and unload at runtime, typed and
versioned classes, a failure banked instead of a hang; the `DSV_CP*` page ABI
*is* the cdev / `DRV_CONFIGURE` model at a fraction of the cost; the movable
image with the kernel holding the relocation is simpler than OS/2's explicit
locking; `OSAPI_DRV_SUSPEND` is GEOS's `DR_SUSPEND` with the hardware facts
recorded; `DRV_H_KEEP` is DOS's INIT break address, proven.

**Recommendation.**
- **Now - read the tables in place** (histsys-7; WP4): keep class 1's copy
  (the IRQ0 reader); for every other class keep the SI that `DRVV_ATTACH`
  returned - the table is wherever that SI pointed, so a per-class offset and
  length table costs **+12..+18 `.bss`** [truth, cost] - and read the
  published segment. Sites that hold ES for a transfer bank it round the read.
  The length test goes in **one** place: the ~14 non-sound readers already go
  through `drv_cls_svc_x`, so that accessor becomes a cell fetch that answers
  0 past the table [fit]. **-90..-140 resident kern_big, 0 kern_small,
  kern_mega conventional similar to kern_big** (`drv_svc` is conventional
  `.bss` there, `build/megak/kernel.map`), unmeasured. This makes D1 and
  drivers-9 moot (gfr-6). drivers-10 and bytes-8 do **not** ride it as first
  written: `osapi_img_name` (the Task Manager's names) and kern_mega's
  `mega_imgnf` read `DRVR_FILE`/`MODR_FILE` at any time, so the names stay
  resident or those readers are reworked first, and drivers-10's ~+100 in the
  blob is conditional on the owner's 14th-sector decision; the three name
  copies (blob, CTRL.DRV, HIBER.DRV) come from one `%include` [truth, fit].
  drivers-13 is dropped: moving `drv_svc_call_x` into `.text` swaps one far
  round trip for two [cost].
- **Phase 1, not flag day - the table floor** (D4). No shipped driver pads:
  HDD.DRV and EMS.DRV declare full 36-byte tables, and every driver the
  `DRV_VER` 12 equality test admits already carries full `DSV`/`FSV` tables,
  so the floor already holds and writing it down is a contract clarification
  (`--update`, no bump) plus a build-time `%error` in `OS88_DRIVER` [truth,
  cost, fit]. Its resident value is the length tests histsys-7 then need not
  make; the zero-fill and clamp themselves live in the three `DRV_ATTACH`
  expansions (blob, CTRL.DRV, HIBER.DRV, ~36 each, none resident) and stay,
  because the clamp keeps a newer SDK's longer table out of an older kernel's
  copy [cost, fit].
- **Now** - lifetime by count and the IRQ registry (themes 7 and 2); the
  hard-disk seam's state published by pointer at `HDT_INIT` (hdk-11, ~-100
  in HDD.DRV's resident part, ~+40 in its tool), the ECC-corrected read
  accepted (hdk-3, +12) and one rung-1 retry (hdk-4, +10), and
  `OSAPI_VOL_SYS` asked instead of a banked system volume (hdk-10, -4) - all
  HDD.DRV bytes, 0 kernel.
- **Flag day (`DRV_VER` only), recorded as D6** - a request block for
  `DSV_BLK` and `FSV_*` only (histsys-8, scoped by gfr-6): ES:BX -> {verb,
  class/unit, status, LBA/count or name/offset, buffer far pointer,
  completion word = 0}; `OSAPI_FILE_*` registers unchanged. Kernel ~-60..-120
  (ESTIMATED, unverifiable). **The drivers reviewer refuses a request packet
  for every verb, and is right**: building a packet per call is dearer than the
  direct near-proc table on an 8088. The two positions agree once the block is
  confined to the verbs that already stage their arguments in `.bss`.
- **Record** - typed slots instead of classes (drivers-15, D5, ~0..-40;
  MMSYSTEM device IDs); driver discovery from `SYSTEM\*.DRV` headers with
  `SYSTEM.CFG` keyed by name (histsys-12, D7, -100..-180 resident, +~120 boot
  overlay, ~5 s on the XT); unknown `SYSTEM.CFG` records copied through
  (drivers-17, +~40 CTRL.DRV); a registered "parking" callback in place of more
  `hb_drvmask` special cases if more drivers grow trip-sensitivity (the DOS 5
  Task Switcher API, int 2Fh AX=4Bxxh); the eight A20 bodies with three probe
  addresses folded (ul-5, 0..-20).

---

## 11. The kernel matrix: kernels fork, packages must not

**Where os8088 stands.** kern_small, kern_big, kern_mega and kern_emu are
build-time products off one source, and gkm-5 prices run-time unification out
of the question: carrying kern_small's tables inside kern_big costs **+5,440
bytes** before any code (task stacks 960 to 2,816, FAT window 1,024 to 4,608)
against kern_small's 5,120 of headroom, and carrying kern_mega's arms puts
~12-14.7KB of protected-mode code on every XT against kern_big's 4,096.
Packages are one binary across kern_big and kern_small. What is missing is
what keeps the arms honest:

- **one-arm fixes**: drivers-3 (`OSAPI_DRV_CLASSK` misreports an HMA driver on
  kern_big) and hiber-2 (kern_big's DOS-handoff refusals leak the extent
  claim) were fixed on kern_mega and left on kern_big (gkm-1; hiber-2's
  `.badv` sits inside the `KERN_MEGA` block, so the fix assembles it on
  kern_big too);
- **refusal stubs nobody decodes**: three of four kern_small-only bugs are a
  stub or guard written apart from the full body - abi-2 (`OSAPI_FS_OP`
  overwrites AX), dw-8, video-1 (Hercules page 1 enabled on a CGA+Hercules
  machine because the dual-card guard is `%ifdef KERN_BIG`) - and the API-shape
  review found two more of the shape (gi-1, gi-6); `t_api_abi` decodes one
  kernel of four (abi-3, gkm-2);
- **coverage inverse to importance**: the 128KB floor has 21-28 soak rows
  depending on attribution, kern_mega 390-430, and `test-full` never boots
  kern_mega; `t_hotpar` covers kern_big and kern_small, not kern_mega, whose
  every machine has the 16-bit bus where parity matters (gkm-3, gkm-4);
- **build names, not features, are the configuration language**: 638
  `KERN_BIG`, 126 `KERN_SMALL`, 406 `KERN_MEGA`, 21 `KERN_EMU` conditional lines
  across `kernel/` (gkm-7, MEASURED census);
- **budget headroom decides which kernel gets a speed-up**: kern_mega carries
  the first eleven perf knobs because "its conventional budget had room"
  (gkm-10; the ten remaining for kern_big total +452, LAST-DROP-PERF 7.1
  already prices them row by row).

Of the review's 64 code bugs, 32 live on one arm or arm-set (8 kern_mega, 4
kern_small, 20 the kern_big feature set), but only 5 are true divergence. **The
near model no longer limits capacity on kern_small (32,565 free in the window)
or kern_mega (14,056); it binds kern_big at the VGA rung, with 6,946 bytes
left, not the 19,842 `KERN_CODE_MAX`'s line implies** (gkm-11, MEASURED by
`kernsize`). Inside the kernel it is now a placement rule rather than a
capacity one. `.cold`'s ~700/~540 bytes of crossings buy the HMA, a 5150 ROM,
the slide over the VGA rung and 46KB above 1MB on kern_mega.

**What the period systems did.** Windows 3.0 answered the same question the
same way: KERNEL.EXE (real mode), KRNL286.EXE and KRNL386.EXE were three
binaries off shared source with WIN.COM choosing, while USER, GDI, drivers and
applications were one binary with a `PROTMODE` flag. Windows 3.1 then dropped
real mode, a legitimate move recorded here so it is a choice rather than
drift. DOS 5/6 ran one binary from 8088 to 486 by run-time adoption
(`DOS=HIGH`, HIMEM). SVR3/SVR4 configured kernels from `master`/`system`
files selecting whole objects at link time, so there was no `#ifdef` forest
inside shared files. The Mac raised its RAM floors per release and dropped
tiers rather than forking.

**What os8088 already does better.** Segment macros make ~400 sites
protected-mode clean at assembly time and byte-identical off kern_mega, where
Windows relied on written rules; ROM/HMA adoption is `DOS=HIGH` done at run
time; the 128KB floor is real; build names that imply each other and refuse
bad combinations, resolved once above the includes, already read like a SysV
configuration file.

**Recommendation.**
- **Now - arm honesty as gates, not culture** (gkm-1/2/3/4/7): the two one-arm
  fixes (drivers-3 **+5 .cold** kern_big/kern_emu; hiber-2 **~+20 HIBER.DRV**,
  module, 0 resident); a four-kernel `t_api_abi` with per-kernel FIXED rows and
  decoders for kern_mega's SLOT and ICW cells, extended to the refusal stubs,
  each stub naming its contract (abi-3; abi-2 0, dw-8 0..+3, **video-1 +13
  kern_small**, corrected from ~+6); a `smallport.txt` MartyPC port list for
  the floor, which needs a parser change because `_ports` drops the marty need
  (gkm-3 corrected); `t_hotpar` over kern_mega and a self-aligning parity
  macro (0 net, the `%if ($ - label) & 1` precedent is in the tree); a ratchet
  on raw build names in shared code (SysV's discipline: build names only in the
  configuration); and the rule that a fix in shared code lands on every arm
  that carries the code.
- **Now, unproven - retire kern_emu** (gkm-6): it exists because a polled
  device once needed kernel poll sites, and USBMOUSE.DRV proved a worker driver
  feeding `OSAPI_MOUSE_FEED` needs none. A USBMOUSE-shaped VMMOUSE.DRV, **+~40
  kern_big**, -385 kern_emu-only, one kernel build retired. Unproven: that a
  worker rotation pumps a drag's spin loop as promptly as `vmm_poll` inside
  `task_yield`.
- **Flag day / staged - L3 is the matrix's package half.** Every armed package
  in `$(MEGAPKGS)` is matrix cost paid outside the kernel; theme 9's clean bit
  retires them one at a time. L3's own figure is **-1,271 .mega (MEASURED
  spans: `mega_gpfix` 602, `mega_gpw` 526, `mega_null0` 74, `mega_db` 69) to
  -1,627 .mega with `strcal`/`strfix`, plus -17 (MEASURED `mega_tramp`) to -60
  conventional `.text`**; the first "1,190-1,400 MEASURED" was neither [cost].
  A driver-only IRQ cell retires only the drivers' share of `mega_null0`,
  which traps every segment-0 access including package BDA reads [truth].
- **Record** - do not unify at run time (gkm-5); do not flatten kern_small to
  one code segment while its ROM is a product (gkm-8, saves 540, costs the
  ROM); the one-disk 8088/286 product, WIN.COM's half, after L3 (gkm-9:
  +60..+120 `.boot2` transient, +172KB on the 1.2/1.44MB system disks) is an
  **open question for the owner**, not a property of L3, because no release
  artefact carries kern_mega's disks today [fit]; push more kern_mega `.text`
  into `.cold` above 1MB through the segment macros when kern_mega grows; and
  gkm-10's per-row decisions on LAST-DROP-PERF 7.1 are the owner's, not a new
  record.

---

## 12. The contract and documentation system: one source per fact, the rest generated

**Where os8088 stands.** The engineering system is strong: 636 assembly-time
assertions (re-counted [truth]), budgets derived from a stated KB rule, a
binary decode of the API table, an ABI lock with written reasons, a dozen
former prose rules now gates, and a generated "does this exist?" index. None
of the period projects had a gate on their own documentation at all. The weak
half is what is still prose, and it fails in three repeatable shapes. The
examples below were **found by this review and fixed in its round 1** (commit
14428309, "Docs and comments brought into line with the code", 946 fixes), and
they are cited as evidence that the shape recurs, not as live defects [truth,
cost, fit]:

- **hand copies of code**: SPEC.md 3 had `KERNEL_SEG` 0x0800 against the
  code's 0x0060 and `STK0_SIZE` 1024 against 512 (now corrected; engsys-3);
  slot contracts exist in up to four places that disagree in both directions
  (abi-10, engsys-5); the gap reviews added more of the kind - 13 mismatches
  around the includes (gi), 22 around XMEM (xm), 26 around the hard-disk
  driver (hdk), twenty stale places about the kernel's `os88ui` copy (ouk);
- **running counts**: the fast tier's "37" was 76 rows; "exactly two"
  `sch_lock` raisers (engsys-6, ctx-3); "TWO USERS" of `os88pit.inc`, which has
  eight shipped carriers (gi);
- **section numbers that resolve to the wrong section**: six citations of the
  greying standard said 46 and meant 47, and passed checkdocs (engsys-2, now
  fixed); basename collisions silently disable citation checks (engsys-8);
  `.c`, `.h`, `.txt` and `.sh` are not scanned (engsys-9).

Still open: comments and SPEC text narrate history ("it used to") instead of
stating invariants, and SPEC.md's front matter (1-4, 15-17) describes retired
designs as current (engsys-4); 56% of SPEC.md is package design notes
(engsys-18); register discipline - the hard rule "preserve every register" - is
ungated (engsys-19, ul-1); AGENTS.md still differs from CLAUDE.md by about
675-705 diff lines (engsys-10; round 1 restored its byte rules); no
heading-drift check exists yet (round 1 only pruned `RULE_REFS`); the tier
admission rules are broken by ~17 one-package fast rows (engsys-7); and **no
row produces any of the measurements this review defers** (ul-10, below).

**What the period systems did.** Inside Macintosh had a chapter per manager
with a closing Summary of every constant, record and routine, and later
volumes *marked* what they superseded instead of rewriting the old chapter;
Microsoft's H2INC generated the MASM include from the C header; GEOS routine
headers read `PASS:`/`RETURN:`/`DESTROYED:`; AmigaOS extracted the reference
manual from source headers (autodocs); the DOS Programmer's Reference gave
every function the same Entry/Return/Notes shape with a version gate
("2.0+"); Inside Macintosh cited by chapter NAME, which cannot resolve to the
wrong section.

**What os8088 already does better.** Content assertions at assembly time, the
ABI lock with reasons, the suite registry with `why=`, and the generated index
- each beyond anything the era had.

**Recommendation (all 0 kernel bytes; WP10, WP24, WP25, WP14).**
- **One source per fact, the rest generated** (engsys-3, -5, -6, -10, -18,
  -19): the tree already proves the model three times (`os88index.py`, the
  kernsize baseline block, the `ks:` facts line). Fix the four values still
  wrong now, add a general check over SPEC.md's ~178 other `NAME equ N` lines,
  and generate or delete SPEC.md 3 and 15.1 inside WP24's front-matter rewrite
  rather than gating a hand copy that WP24 is about to replace [fit]; delete
  SPEC.md 20.3's and 53.8's contract copies in favour of the SDK names; a
  COUNTS table for prose counts ("a prose count either carries its moment or is
  not written"); AGENTS.md generated from CLAUDE.md; a routine reference
  generated from a fixed header grammar (in/out/clobbers/flags/lock). Generating
  contract blocks into `apps/os88api.inc` or `drivers/os88drv.inc` moves the
  abilock fingerprint, so the first generation is a written `--update` and the
  generator must then be byte-stable [cost].
- **Names over numbers, with a committed heading lock** (engsys-2, -17): a
  heading-drift check against a **committed `{number: title}` lock updated
  deliberately with a reason**, the way `tools/abi.lock.json` is, not against
  `git merge-base`: on a shallow clone merge-base returns a confidently wrong
  base without failing, upstream has no `elendilon` branch, and a release tree
  may have no git at all [fit]. It lands before engsys-18's per-package SPEC
  split and resolves citations into the new files, or the split blinds it.
  Stable `[G-BUDGET]`-style tags for guards.
- **History in its place**: rewrite SPEC.md 1-4 and 15-17 as a short current
  front matter, one paragraph per rule plus the gate that enforces it, and move
  narrative to `docs/history/` (engsys-4); finish the comment-discipline sweep
  - round 1 applied most of the recorded fixes, and what remains is the
  history-to-invariant rewrite and the per-pass byte narratives (WP25). Split
  package sections into per-package SPECs keeping their numbers (engsys-18),
  the Inside Macintosh chapter-per-manager shape, on the WEAVE-SPEC and
  ARCADE-SPEC precedent.
- **Admission by declaration** (engsys-7): a `family=` or dated `owner=` field
  on each fast/full row, and `t_registry` fails a row with neither. One ratchet
  library (engsys-13 (unchecked)) instead of 21 bespoke parsers. Mirror gates
  for the conventions the includes copy (gi-8: `m83_syms` against
  `dskw_syms`; an include guard on `os88pit.inc`), and teach `os88ovlchk` the
  `%1:` bodies and `section OS88UI_KGLYPH` it is cited as guarding (ouk-F2).
- **Rows for the deferred measurements** (ul-10): register `stkdie` (the dying
  128-byte worker's watermark), `xferfreeze` (a worker's slices during a
  floppy transfer), `ifoff` with `--regrow` and `--mouse` legs, and a
  `uilat --workers N` leg, and run their union once in the final pass.
- **Do not let the review break the build**: engsys-1 found this review's own
  in-progress reports failing checkdocs, and so `make`; checkdocs should warn
  rather than fail in `docs/reports/` and `docs/history/`.

---

## 13. The API shape and the includes: a stateless kernel, caller-held tokens, and no library

This theme is new in the final view; gap-api-shape-includes is its source,
with rt-8's measurement of shared package code and the os88ui kernel-copy
review.

**Where os8088 stands.** The includes work as os8088's libc. The kernel's file
API is **stateless and takes a name on every call**, NFSv2's model, and each
efficiency is won back by a token the caller holds: the `READ_SEQ` cursor, the
`WRITE_SEQ` token, `os88data`'s banked folder, `op_clb`. That keeps kernel
state and resident bytes near zero, and every include states in its header the
kernel limitation it compensates for. Staleness takes a cold path, never an
error; `os88data` verifies its own bank; `pit_now` and `sch_account` share one
coherence rule; `crt0` meets every rule checked (SS against DS, ES on entry,
register saves, DF, CF after the pops, the worker park, claim ownership). The
weaknesses are four leaks in the shape:

- **Geometry leaks into every caller, and the only way to learn it is a full
  FAT count.** The cluster size comes only from `OSAPI_FILE_DFREE` or
  `OSAPI_VOL_STAT`, which count every FAT entry: **~105 ms on a 20MB disk,
  MEASURED**, ~55 ms modelled on a 1.44MB floppy. `op_clbget`, `op_self` and
  `os88_wseq_open` pay that count for a byte the kernel holds in `[dsk_spc]`
  (gi-2). The parts standard then rounds every read to *clusters* even on
  kern_big, where `READ_AT` takes sectors, bringing back the cost SPEC.md
  18.4.4.2 removed: a 4KB lazy part on a 16KB-cluster partition claims and
  reads up to ~32KB (gi-3).
- **The kern_small stubs answer inconsistently** (theme 4): some give
  `FERR_NAME`, `VOL_STAT` and `WRITE_AT` a bare `stc` with the caller's AX. So
  `os88wseq.inc`'s documented kern_small APPEND path cannot run - its open
  calls `VOL_STAT` first and fails CF=1 with AX = 0, and REDLINE's Inspect
  report, its only user, ships on `util360` (gi-1; whether REDLINE otherwise
  runs on kern_small is unchecked) - and `os88data`'s bank never saves a walk
  on a write there (gi-6).
- **The `READ_SEQ` cursor names no file** and stays trusted across a quiet
  folder change on a trusted floppy or a fixed disk, which returns the wrong
  clusters with CF=0 (gi-4; a documented caller contract, no live bug shown),
  and its register shape needs a 76-byte shim (`os88_rseq`, MEASURED in
  `build/ftpd.map`) in about 13 carriers (gi-5).
- **`pit_now` is wrong inside an armed `FSXF_FASTTICK`/`FSXF_RATE` bracket**: its
  low half laps N times a tick; its users know, the include does not say so,
  and SPEC.md 53.2.1 says nothing outside `sched.inc` can tell (gi-7).

And shared package code is real duplication: of the 941,780 image bytes in the
35 packages that have listings, **at least 83,970 (8.9%) is emitted from
`apps/os88*.inc` source, at least 60,596 of it duplicated, median 1,740 a
package** (MEASURED from the listings, a lower bound and a sample of the 60
packages built, rt-8). `os88ui` alone is 29,193 bytes over 31 packages.
RAM pays only for concurrently running packages, about 3-5KB in a typical trio.
The kernel's own `os88ui` copy (1,089 `.cold` on kern_big and kern_mega) has
the one visible-redraw fault the hard rules name: its standard button fills
its interior, frames it and letters the caption one or two calls later
(ouk-F1, against SPEC.md 13.14.6 rule 1).

**What the period systems did.**
- *Sun NFSv2* (1985): stateless reads and writes, each carrying a file handle
  that names the file - the cursor's one missing field.
- *DOS*: from 2.0, a path in every file call and a handle with a position;
  AH=32h (Get DPB) returns sectors per cluster without counting, and only
  AH=36h counts.
- *Unix System V*: user-space buffering is stdio's `fwrite` in libc, never in
  the kernel.
- *Shared code*: OS/2 1.x and Windows put shared code in DLLs at the price of
  the DGROUP problem (`MakeProcInstance`, `SS != DS` for DLLs) and far calls;
  GEOS geode libraries were swap-heavy on floppy; the Mac put its shared code
  in ROM.
- *Windows 3.x enhanced mode*: VTD virtualised the PIT, so `GetTickCount`
  and `timeGetTime` stayed right under `timeBeginPeriod`.

**What os8088 already does better.** A stateless kernel leaks nothing when a
program dies and costs no resident bytes for state, which DOS's SFT and
OS/2's per-process handles did not manage; the tokens recover the efficiency
in user space, where stdio keeps it. One body per convention, each with a
guard, is what made the drift in this theme findable at all.

**Recommendation.**
- **Now - a granule cell** (gi-2, gi-3): `OSAPI_FILE_GRAIN` behind
  `inst_vol_enter`: AX = bytes per cluster (`[dsk_spc]`*512, at most 32,768),
  CX = `READ_AT`'s granule (512 on kern_big, a cluster on kern_small). No
  count and no I/O. **+14..+16 resident per kernel**, a 6-byte rare cell
  included. It retires the DFREE call in three includes, and `op_clbget`
  banking CX lets the parts standard read in sectors on kern_big for ~0 package
  bytes, saving up to ~2 clusters of RAM and read per fetch on large-cluster
  disks. Until it lands, `os88wseq` asks `OSAPI_FILE_DFREE` (present on both
  kernels, same cost) and banks a real `FERR_NODISK` when the stat refuses (gi-1,
  +2 package bytes).
- **Now - the cursor names its file** (gi-4): stamp an 8-bit name sum and the
  current folder's low cluster byte into the cursor's kernel-private bytes 3-5,
  a mismatch read as cold. **+20 .cold kern_big, 0 kern_small**, no ABI change;
  fix the `os88api.inc` sentence and the kernel banner that contradicts
  `[dsk_mgen]`'s rule.
- **Now, docs** (gi-7): state `pit_now`'s bracket limitation in the include
  and in SPEC.md 53.2.1 and 72.15.1 (0 bytes); a kernel `OSAPI_PIT_NOW` that
  knows `[sch_fast]` is recorded (+20 resident; its 46.7 us far call against ~30
  us inline matters to profilers).
- **Now - the kernel button draws each pixel once** (ouk-F1): the caption's
  cells go down with `font_run`, which draws ground and glyph in one pass, and
  only the interior outside them is filled, so the caption is never
  momentarily blank; +8..+12 per copy (kern_big/kern_mega `.cold`, kern_small's
  module copies, each package that carries `os88ui`), 0 extra calls. And
  ouk-F3/F4/F6's ~-35 resident on kern_big with no ABI change (a constant
  one-button record, the `arm_f` shim, the dead `OS88UI_FILL` loads).
- **Now** - `os88_wseq_put` copies with `rep movsb` round a DS/ES swap (gi-10,
  +4 package bytes, ~85 ms to ~30 ms per 8KB report PREDICTED, the int 13h
  calls still dominating); `crt0`'s plain `.OVL` arm retired or given the lazy
  arm's go-home bracket (gi-9, 0 shipped; no shipped package uses it).
- **Now, then flag day - a `READ_AT`-shaped stream cell** (gi-5): append a cell
  with `READ_AT`'s registers plus a caller cursor that re-seeds itself and is a
  `READ_AT` alias on kern_small (**+14 .cold kern_big, +3 kern_small**), then
  withdraw `OSAPI_FILE_READ_SEQ` at the flag day with the C2/C3 batch:
  **-76 per carrier, ~-1KB of package and driver bytes**. It is the read-side
  twin of dw-22's caller-held write cursor (theme 3).
- **Record - a path-resolving cell** (gi-11): `os88data` is ~397 bytes in FTPD
  and ~20 carriers, almost all of it the root, SYSTEM, APPDATA walk; a "resolve
  a relative path to (drive, folder cluster)" cell would be +80 resident and
  -~250 a carrier, but the bank already makes the steady state one GOTO_QM.
  If C3 ever lands, `od_qm` and `crt0`'s lazy home are its consumers.
- **Record - a decoded-image cache for relaunches** (rt-8, loader-14): kept by
  the loader before the entry proc's first write, keyed with a header checksum
  because `os88disk` pins serials and timestamps; +100..+150 .cold, recorded
  until relaunch frequency is measured. A clone of a *running* instance is not
  viable: its initialised data is already modified (ul-6).
- **Record - export the scroll bar as appended cells** (ouk-R1, plausible):
  about +60 resident per kernel against -500..-840 per bar-using package
  image, the one place a shared-code cell might pay.
- **Not adopted** (gi verdict, rt-8): absorbing `os88wseq` into the kernel
  (stdio's `fwrite` belongs in user space); moving the parts body into a module
  (a module comes off the system disk, a part off the package's own disk); and
  a shared-library tier (below).

---

## What we would NOT copy, and why

Each of these is a period mechanism a reviewer considered against this machine
and refused. They are recorded so nobody re-derives them as improvements.

- **Automatic demand loading and LRU discard of package code** (Windows' INT
  3Fh reload thunks, GEOS swap drivers). On a one-floppy XT every automatic
  reload can be a disk-swap prompt in the middle of a gesture - it is what
  made Windows miserable on floppies. os8088's explicit `op_fetch`/`op_drop`
  and `mod_need`/`mod_drop`, the Mac's `UnloadSeg` honesty, are the right
  policy. Theme 7's cache is the safe subset, and the residency review checked
  why: reload happens only at a `mod_need` entry, never mid-gesture, and a
  purged FDLG wanted with the system disk out gets the existing swap prompt,
  COMMAND.COM's answer (rt verdict 7).
- **Master pointers / handles on every access** (Mac, GEOS, Win16
  `GlobalLock`). The double indirection is a per-access tax on an 8088, and
  `HLock` discipline bugs were the Mac's chronic disease; the relocation proc
  moves the cost to the rare event. Memory handles stay recorded (R2-6);
  window handles are refused while R6 is shelved (gfr-4), their breakage being
  ~121 field reads in 33 files rather than hundreds (ul-8).
- **Code movement by stack walking** (Win16 real mode's BP-chain walk with
  odd-BP far frames). Packages follow no frame convention, and the `INC BP`
  prologue would cost bytes in every far proc.
- **One window procedure plus a message number in place of per-event vectors**
  (Win16 `WndProc`, GEOS methods). On an 8088 a package pays nothing for events
  it never asked for, and the kernel needs no dispatch in the package. The
  flag-day class table keeps the vectors and only moves where they are stored.
- **A Pascal-style scratch-register convention** (Win16, the Mac Toolbox; ul-1).
  Refused on evidence: half the AX saves it would remove are in routines whose
  callers read AX afterwards, so it moves saves rather than deleting them; the
  liveness tool pays only as a lint.
- **A single renumbered error space** (Mac `OSErr`; gfce-8). No consumer mixes
  families; the class column is what a program acts on.
- **A DOS request packet for every driver verb** (drivers rethinks). Dearer
  than the near-proc table on an 8088, and the IRQ0 read depends on the table;
  only BLK/FS, which already stage in `.bss`, get a block.
- **Trap patching** (Mac `SetTrapAddress`, AmigaOS `SetFunction`, chained DOS
  vectors). A bad patch is a dead desktop. os8088's single-owner vector tables
  (`dkv`/`thv`), whose owner restores the kernel's entries on the way out,
  avoid the INIT-ordering and tail-patch bugs chained Mac patches suffered. The
  SDK should say in one line that a package never writes `KERNEL_SEG`'s first
  0x500 bytes.
- **A shared-library tier for package code** (OS/2 and Windows DLLs, GEOS
  libraries; rt-8). The duplication is real (at least 60.6KB in the 35 measured
  listings), but RAM pays only for packages running together, ~3-5KB in a
  typical trio, and a library costs the DGROUP redesign, 46.7 us far calls
  against 11 us near on hot paths, and either a disk swap on a one-floppy
  machine or resident kernel bytes. Parts init-drop and a relaunch image cache
  are the levers that pay.
- **A general rung mechanism** (GEOS's and Windows' one DISPLAY.DRV per
  adapter worked only because every primitive was a far call; rt-4). Here it
  would need a relocating loader for `rel16` displacements on the hottest path.
- **Absorbing `os88wseq` into the kernel, and the parts body into a module**
  (gi verdict). User-space buffering is stdio's, and System V keeps it in libc
  for the same reasons; a module is read from the system disk, a part from the
  package's own.
- **Run-time unification of the kernels** (DOS's one binary; gkm-5), and
  flattening kern_small to one code segment (gkm-8) - priced in theme 11.
- **A full priority field in the scheduler** (OS/2 priority classes, AmigaOS
  priorities). A byte per task, a second scan key and an inversion story for 14
  slots; the one-shot boost buys the interactive half. Likewise copying
  AmigaOS's per-task `Forbid` save so `Wait()` is legal inside it (~12 hot-path
  bytes; writing the rule down is enough).
- **Per-thread message queues** (OS/2 `WinCreateMsgQueue`). 12 tasks times even
  an 8-record queue is most of 1KB; `W_WKQ` is the one-message queue that buys
  most of it. Likewise a lossless UI work queue in `evq` (ctx R1).
- **Proportional fonts** (QuickDraw, GDI raster fonts, GEOS BSW). The 8x8 cell
  is what makes `font_run`'s one store per cell row, the cell-diffed menu bar
  and the row-major run possible. Take the style synthesis (text-15, kernel
  bold) and leave the metrics.
- **Write-behind caching on removable media** (SmartDrive). Only droppable frees
  may wait (dw-15).
- **Delayed clipboard rendering** (Win16 `WM_RENDERFORMAT`). Wrong for "copy,
  close, paste" on 128KB; take the generation count and a reserved format byte
  instead (ui-12, ui-13, both unchecked).
- **Desktop shortcuts as `.LNK` files in a `DESKTOP\` folder** (Windows 95).
  Every icon would cost a directory walk and a read at boot, ~400 ms each on the
  XT, where today's cached 64-byte picture costs none.
- **Settings in battery-backed CMOS** (Mac PRAM; sysmisc-17 (unchecked)). An XT
  has no RTC, and one `SYSTEM.CFG` with a deferred single write fits a machine
  where a floppy write is seconds.
- **GEOS-style state files instead of a hibernate image**, a full-memory
  hibernate checksum, and Windows 2000's per-page sums (hiber rethinks). The
  first needs an object system; the others refuse good live-written images or
  solve a paging problem os8088 does not have.
- **OS/2 1.x's no-copy DOS box** (megados rethinks). kern_mega cannot fence a
  fixed low region while its kernel is conventional, and the runs park gives the
  program ~621KB against OS/2's ~500KB.
- **Patching known EXEPACK stubs in EXEC** (MS-DOS 5; md-25 (unchecked)).
  `kd_xunp` unpacks any variant it can validate and keeps the 621KB arena.
- **A unified DOS-style buffer pool** (dr-19, corrected by ul-2: two caches,
  -100..-300, costs kern_small and cannot combine with L1) and **a keyboard path
  into menus** (text-19, unchecked, +150..+250 .cold): recorded, the first for
  its write-path risk and its kern_small cost, the second as outside the "no
  huge growth" brief.

---

## Byte ledger by theme

kern_big resident unless stated; signed; ESTIMATED except where marked
MEASURED. "Now, core" is the recommended set; owner's calls,
measure-first items and the theme 3 file layer are listed separately. It
overlaps `synth/phase1.md`'s WPs, which are the authority for ordering; where a
WP's figure differs from this ledger, the lens-corrected figure here is the
later one.

| theme | now, core: kern_big | now, core: kern_small | now: elsewhere | separately (owner, measure first, optional) | flag day (big / small / mega) |
|---|---|---|---|---|---|
| 1 contexts | **+11..+48** (ctx-1 with ctx-2 +30..+60, ui-2 0, ui-3 -19, sched-3 0..+7) | about the same (ctx-1 ~5 less, ui-2 +5) | -24 .mega | dw-4 +9 (owner exception, else checked build); ctx-6 +14 small | none; deferred damage +150..+200 (record) |
| 2 interrupts | **+77..+91** (IMR shadow +25, IVT scan +25 .cold, hm-1 +10..+16 .cold, hm-5 0..+6 .cold, input-2 +14..+16, ctx-5 +3) | shadow +25, input-2, ctx-5 | xm-1 +45..+60 DOS.O88; mc-4 +20..+30 .mega | full `OSAPI_IRQ` +100..+150 (SOUND.DRV -60..-100); cursor after EOI +30..+40 and +64..+128 .lowbss or +6 | ~0 big/small; ~-210 .mega |
| 3 waiting and I/O | **+40..+55** .text, +6 cell (sched-15) | same | | disk release +33..+38 (+7 .text) and AT hook +25..+30; sched-16 +22 +1 .bss; file layer +261..+271 and two cells (histsys-3 +25..+35, dr-5 +45, dw-14 +70, dw-15 +15, dr-4 +16, dw-12 ~+90) | dw-22 0..-20; D6 recorded |
| 4 faults and errors | **+45..+60** .text, +20..+30 transient (no room today) | +35..+45, +4 (`VOL_STAT` stub) | +6 DOS.O88 (06h bank) | gfce-2 to +100..+120 in total | R9 +4..+10 (value open) |
| 5 validation | **+41..+55** (loader-1/3 +18 .cold, dw-1 +10..+12 .cold, clamp +8..+12 .cold, wm-2 +4, hm-14 +4, dr-9 +5, gfx-1 0) | wm-2 -2, rest similar | gfce-6 ~+22 .ovl and ~+22 CTRL.DRV; hdk-5 +14 per HDD image and ~+20 HDD.DRV; APICHK 0 shipped | wm-2 pad +12 .bss; W_IDX initialiser ~+10..+13 blob | - |
| 6 memory | **+64..+119** .cold (hm-9 +35..+80, hm-4 +5, hm-13 +6..+8, files RT2 +12..+20, hm-8 +6) | +27..+35 (hm-9 refused +4..+6, RT2 +6..+10) | hm-11 -0.8..-1.5KB per running parts instance; gms-R1 ~+600 data above 1MB and -100..-200 .mega code; gms-R2 -150 .mega | mem-17 +15..+20 kernel, ~-19 per running instance | C1 -45..-69 / -45..-69 / -12 conv; packages +0..+1.7KB |
| 7 residency | **-2,380..-2,450** (FILECP/FDLG cache, 0 on ROM/HMA machines), **-290..-380** (keyboard mouse), **-220** (PS/2 overlay), drivers-1/2 **+43..+49 .cold +11 .bss** | **-290..-380** (keyboard mouse) | KBMOUSE module +400..+450; PS2MOU.DRV heap claim 0.5-1KB on a 286 with PS/2; kern_mega -290..-380 conv | text-7 -165..-185 mono / +16 VGA gauge; rt-6 on kern_small +60..+85 | - |
| 8 dispatch | 0 | 0 | | `cw_` rehearsal -40..-60 .text after the vector question | T5 -325..-390 / -325..-390 / ~-355..-415 conv; R14 ~-300 / -95..-120 / ~-350 conv; V1 -2; packages -2 a rare site (~-5.3..-5.5KB unpacked), +1.3KB tables for -2.6KB setters |
| 9 versioning | **+7..+12** .cold, 0 for the word | same | +12 .mega (clean bit); ~8..15 per opting package | | dissolves |
| 10 drivers | **-90..-140** (histsys-7, incl. +12..+18 .bss) | 0 (no `drv_svc` on kern_small) | hdk-11 ~-100 HDD.DRV, +40 tool; hdk-3 +12, hdk-4 +10 HDD.DRV | drivers-10 ~-95 only with the blob sector | D6 ~-60..-120 (recorded); D7/histsys-12 -100..-180, +~120 overlay (recorded) |
| 11 matrix | **+5** .cold | video-1 +13, dw-8 0..+3 | hiber-2 ~+20 HIBER.DRV | kern_emu retired: +~40 big, -385 emu-only | L3 -1,271..-1,627 .mega, -17..-60 conv |
| 12 docs | 0 | 0 | 0 | | 0 |
| 13 API shape | **+34..+36** (grain cell +14..+16, cursor stamp +20 .cold); ouk-F1/F3/F4/F6 net -24..-28 | grain +14..+16; ouk -4 | gi-1 +2, gi-10 +4 package; ouk-F1 +8..+12 per package copy | gi-5 +14 .cold (+3 small) now, -76 per carrier at the flag day | `READ_SEQ` withdrawn, ~-1KB package and driver bytes |

**Now, summed (ESTIMATED, kern_big).** Growth in the core rows is about
+350..+517; savings about -2,980..-3,190 (theme 7's three residency items and
theme 10). **Net about -2.46KB to -2.84KB resident**, or **-2.19KB to -2.58KB**
with theme 3's file-layer items. On a machine whose `.cold` a ROM or
COLDHMA.SYS adopted, the FILECP/FDLG cache saves nothing and is skipped, so
there the net is about **+0.2KB to +0.4KB** before the keyboard mouse and PS/2
items. kern_small: growth about +0.22KB to +0.34KB against the keyboard
mouse's -0.29KB to -0.38KB, **net about -0.16KB to +0.05KB**. No row needs a
`PKG_FMT` or `DRV_VER` bump.

**The whole flag day.** gfr-10 reconciled it at ~-1,078 kern_big, ~-685
kern_small and ~-957 kern_mega conventional, about a third of each being L1's
pad. That figure predates three lens corrections that move it: T5's kern_small
saving rises to kern_big's (the same 112 cells); R14 rises to ~-300; and L1's
pad is a saving on kern_big (-386 bytes of pad MEASURED, a full 512 off
`KERN_SIZE` today, 0-511 by build) but on kern_small only if the 7 sectors that
a shared `KERNEL_SEG` leaves below its image are given fixed-size content
(otherwise up to +3,275), and on kern_mega about -128 against its 128-byte
selector granule, or 0 if it keeps 512-byte rungs [truth, cost, fit]. Treat
the flag day as roughly -1KB on kern_big and take per-kernel figures from
`synth/flagday.md`'s rows as amended.

---

## Where the lenses disagreed, and the figure used

| item | truth | cost | fit | used here | why |
|---|---|---|---|---|---|
| teardown on the UI task (BR-4, ctx-1, ctx-2) | +30..+50 | net -15..+16 (BR-4); +25..+41 with the owner check (theme 1) | top of +15..+30; ctx-1's verified +16..+26 | **+30..+60, ~+43** | Re-derived from `build/kernel.map` and the code. `inst_task_die` is 31 bytes today (66AD..66CC). The worker side that every lens accepts (keep `inst_rel_rec` and `toast_owner_gone`, the owner check 12, `cli`, `I_TASK` = 0xFF 4, `wm_close_req` 6 with `mov bx,[di+I_WIN]`, `xor bx,bx`, `jmp task_exit`) is ~34-41. The cost lens's net -15..+16 reuses `app_close_win`'s entry, which runs the negotiator only if `.cl_slot` had not retired it and sends an `I_STATE` 2 record to `.hide` (`kernel/instance.inc:1656-1670`), so it needs a 2-and-task-less arm (~+12) and a separate reap for windowless records (~+28 and a byte), ~+43 in all. A drain of its own shaped like `.chk_close` (58 bytes, `ui_task.chk_close` 9341..937B) is the top of the range. ctx-1's +16..+26 counted neither |
| `task_wake` (sched-15) | +24..+30 | +40..+55 | - | **+40..+55** | the cost lens itemised the slot-to-record conversion and `task_sleep`'s test-and-clear |
| disk-freeze release (histsys-5, ctx-3) | +16..+40, +~30 AT | +33..+40, +25 AT | +33..+38 | **+33..+38, +25..+30 AT** | ctx-3's three changes are the floor; the AT hook gains two guards |
| one fatal path (gfce-1, WP8) | WP8: +34 big, +28 small | +45..+60 big, +35..+45 small; WP8 +45..+65; transient +20..+30 | WP8 "as stated" | **+45..+60 / +35..+45, +20..+30 transient** | the cost lens counted int 6, the CS:IP print and the strings against the MEASURED 174-byte panel; WP8's +34 bundles wm-2 and the int-0-only stub |
| validation total (theme 5) | - | +61..+81 (the parts sum; the headline +65..+90 did not add up) | move histsys-11, loader-4, hm-2 to APICHK, -20..-38 | **+41..+55** | the cost lens's sum less the fit lens's moves, plus the `dsk_get_dir` clamp |
| hm-9 purgeable claims | +35..+75 | +50..+80 | - | **+35..+80** | the same design priced at its two ends (purge deferred to the drop arm, or a restructured `mem_shed_one`) |
| FILECP/FDLG cache (WP2, rt-2) | -3,408..-3,492 accepted | -3,408..-3,492 "ceiling" | same, ~0 on ROM/HMA | **-2,380..-2,450** | rt-2's correction, re-measured here: fdlg's 2,080-byte span holds 1,089 bytes of the resident `os88ui` copy (`os88ui_btn` 0x17A64 to `fdf_fdlg_open` 0x17EA5); no lens re-split the span |
| keyboard mouse (WP3, rt-7) | spans confirmed | -290..-360 small | -300..-380 | **-290..-380** | the cost lens's low end carries the module row, slot and drop |
| text-7 | - | -177 mono, +16 VGA | -165 or -185 (two counts) | **-165..-185, +16** | the two counts differ in what is counted as the edge arm |
| rare-cell door (flagday T5, bytes-1) | -385 big/mega, -245 small | -330..-350 all (door 75-95, tails +15); systemic lens -380..-400 (door 50-70) | - | **-325..-390 big and small; ~-355..-415 mega** | -448 for the cells and +12 promotions are agreed; the door (45 to 95) and tails (6 to 15) are unbuilt, so the range spans them. kern_small's -245 had no derivation and both cost readings decoded 112 rare cells there |
| rare-call cycles | - | +200..+300 (fetch floor) | - | **+150..+300 PREDICTED** | gfr-1's +150..+215 against the itemised fetch-floor count |
| `cw_` rehearsal (gfr-2) | - | -40..-60 | ~-45 | **-40..-60** | 45 real shims, not 70; the drawing shims stay direct |
| class table (R14) | -215 / -95 / -270 | ~-300 / -95..-120 / ~-350 | - | **~-300 / -95..-120 / ~-350** | 13 handler words on kern_big, not 10; the setters are inline cells |
| C1 | three doors, internal callers | -60..-69 big and small (+57 .cold bodies MEASURED) | -35..-55 (T5) or -50..-70 | **-45..-69** | the -45 omitted the door bodies; the low end is the conservative reading |
| `drv_svc` in place, kern_small (histsys-7) | -46 (WP4) | 0 | -36 | **0** | `drv_svc` and `drv_owner` are absent from `build/smallk/kernel.map`, checked here |
| "not on this kernel" value (R9) | AX = 1 with a census | AX = 1, +4..+10 | AX = 0, or the doc half only | **open** | AX = 1 collides with four families; AX = 0 collides with none but moves XREFUSE |
| `OSAPI_IRQ` (D8) | - | +100..+150 | wording to reconcile | **+100..+150** | 112 `.bss` of rows plus code |

---

## Ordering across themes

The dependencies that cross themes, so the phases can be cut:

1. **Theme 1's teardown (ctx-1) first.** It precedes theme 7's module cache and
   theme 3's disk release: both widen what may run while the UI task waits, and
   both assume teardown no longer runs on a worker. SPEC.md 20.6 and 11.103.10
   are rewritten before it.
2. **The disk release last of its group**: theme 2's input-2, ctx-3's
   task-scoped `cur_inxfer`, the windowed-DOS IVT precondition and the
   `xferfreeze` measurement all come before it.
3. **rt-1 before rt-2 and rt-7**: the drop-to-cache is the mechanism the
   FILECP/FDLG cache and the held keyboard mouse both use; WP17's shed policy
   and rt-9's ranks land with it, and SPEC.md 2.8.3 is rewritten first.
4. **WP6 and WP7 (blob and `.ovl` room) before anything that grows a
   transient section**: theme 4's install, wm-2's `W_IDX` initialiser (blob on
   kern_big and kern_mega), gfce-6/sysmisc-2 (`.ovl`) and drivers-10 (blob).
   phase1 shows both homes full on kern_big.
5. **Theme 9's level before any new appended cell** (themes 1, 3, 6, 13), so
   the first package that calls one refuses on an old kernel; phase1's WP9
   switches from a table end to the level first.
6. **Theme 4's stubs ship with the package-close half or the kernel-CS test**,
   and with DOS's own int 0 and its 06h bank.
7. **C1's internal conversion (Phase 1) before C1's withdrawal** (flag day).
8. **Theme 8's vector before either door**: chosen in 81h-FFh, virtualised in
   DOS's `AH=25h`/`AH=35h`, before the `cw_` rehearsal or T5; the
   source-scanning gates learn the call macro before the flip.
9. **Theme 12's context tags, header grammar and committed heading lock before
   the flag day** and before the per-package SPEC split, so the rebuilt tree is
   checked by them (abi rethink 6).

## What this view rests on that is not measured

- **No row produces the deferred measurements** (ul-10): the dying worker's
  stack depth (`stkdie`), the worker freeze per int 13h (`xferfreeze`), the
  IF=0 spans of a regrow and of the mouse ISR (`ifoff`), and UI latency with N
  workers (`uilat --workers`). Theme 1, 2 and 3's "measure first" items wait on
  them, and they run once, in the final pass.
- **Predicted, not measured** (ul-11): the regrow copy's 86-170 ms; isr-7's baud
  ceilings (the `cur_move` spans themselves are MEASURED); histsys-4's N x 55
  ms; loader-14's 0.2 s; every cycle figure for the door, the class table and
  input-2; the boot-time cost of the FILECP/FDLG cache. kern_mega's ~16 ms
  compaction piece is derived from the Victory 286's MEASURED 0.257 ms/KB.
- **MEASURED and quoted as such**: the 174/173-byte death panel; the 1,259-byte
  API table; the fdlg/`os88ui` split of the 2,080-byte span; `kbm_*` 599/550;
  the PS/2 runtime's 265 bytes; `drv_svc`'s 252 bytes and its absence on
  kern_small; the cursor's 15,100/11,500/21,450 cycles; the near-model window's
  6,946 bytes; DFREE's ~105 ms on a 20MB disk; L3's `.mega` spans; C1's door
  bodies; the shared-source census over 35 listings.
