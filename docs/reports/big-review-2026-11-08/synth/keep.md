# What is good - keep it, protect it, copy it

Kernel review of 2026-11-08, synthesis. This is the review's STRENGTHS
register: the patterns the 25 area and lens reviews found genuinely excellent,
where each one is exemplary, why it is right on a 4.77 MHz 8088 with 128KB to
640KB and a floppy, whether anything but care holds it today, and the places
that do not follow it yet. It ends with a one-line scorecard per area.

It is a dated measurement of one tree (the branch `the-big-review` on the day
of the review), not a maintained document. SPEC.md and the code stay the
authority, and every line cited here can move.

## How to read it

**Inputs.** The `strengths` list and `summary` of each of the 25 files in
`data/`, each report's *Strengths* and *Verdict* sections under `areas/`, and
the *Strengths* sections of the nine gap reports (`gap-*.md`: api-shape
includes `gi-`, context/lock model `ctx-`, fault containment `gfce-`, flag-day
reconcile `gfr-`, kernel matrix `gkm-`, mega unowned spans `gms-`, mouse
cursor `mcur-`, os88ui kernel copy, residency tiers `rt-`). The "where to
spread" lines come from the findings: most reviewers noticed that area X does
a thing right and area Y does not, and the finding that says so is cited.

**Strength ids** are `area:Sn`, the n-th entry (from 0) of that data file's
`strengths` list. **Finding ids** are the data files' own (`dw-` is
diskwrite, `dr-` diskread, `hm-` histmem, `mc-` megacore, `md-` megados,
`mem-` memory). The two refuted findings (isr-5, wm-20) are dropped. A finding
the verifier did not re-derive is marked **(unchecked)**; one it corrected is
cited in its corrected form.

**Bytes** are signed, the reviewers' figures (ESTIMATED unless they said
MEASURED), and say where they land: *resident* (`.text`, `.bss` and `.cold` -
`.cold` IS resident), *.ovl* / *.boot2* (boot-transient), *module* (a `.DRV`
image, paid only while loaded), *.mega* (above 1MB on kern_mega after the
switch). Per the CLAUDE.md banner they are bytes, not rungs.

**Gate** names a `tests/unit/` row, a registry, or the assembler when one
already holds the pattern, and says **prose only** when nothing but review
does. A proposed gate is marked as proposed, and every one of them is host
tooling: 0 shipped bytes.

## The verdict in one paragraph

The kernel's mechanisms are, area after area, better than the period systems
it is measured against, and better for THIS machine rather than in the
abstract: direct far calls into a fixed table with no fixups, an out-of-band
claim map, relocation by callback with a pinned default, a UI task that
really halts with the lost wakeup closed by construction, a commit order that
survives a power cut where DOS could not, and a drawing core priced against
the 8088's fetch floor. Sixteen of the 25 summaries use a superlative
("best-argued", "most mature", "one of the best-engineered", "strongest")
about their own area and the other nine say "careful", "strong" or "sound";
the verifications bore them out, with only two of 469 findings refuted. What is excellent is excellent at ONE site -
the site of the field report that produced it - and the review's defects are,
in the majority, the same pattern missing at the neighbouring site (the
systemic synthesis, `synth/systemic.md`, names this as "solved once, carried
by prose"). So the cheapest high-value work in this tree is not new design:
it is copying the kernel's own best patterns to the places they have not
reached, and turning the ones held by care into gates.

## Part 1. The ten strongest patterns

Ranked by how much of the kernel they hold up, how many live gaps copying them
would close, and how cheaply.

### K1. Hostile input is bounded where it is read, and divide safety is arithmetic

**The pattern.** Every byte off a disk, a package or a network is assumed
hostile. It is range-checked at the door it enters by, before anything is
derived from it, and every `div` whose quotient could overflow is preceded by
the compare that proves it cannot (`cmp dx, divisor / jae refuse`), so a
would-be divide fault becomes a refusal.

**Exemplary.**
- The BPB table names the consumer each rule protects: `kernel/disk.inc:5091-5217`,
  SPEC.md 18.2 (diskread:S2). The quotient guards stay in even after the mount
  validated: `disk.inc:1761-1767`, `5233-5241`, `5269-5275`, and every derived
  layout add is carry-checked (diskread:S1; gfce strength 1).
- `fsk_needcl` saturates on a hostile FileSize instead of faulting
  (`kernel/fsck.inc:1824-1826`, gfce strength 2).
- The loader's carry fence and 32-bit size ladder, shared with the re-home arm
  (`kernel/loader.inc:380-403`, `450-504`, `1609-1622`; loader:S2), and the
  post-read header re-check against a disk swap (`loader.inc:1091-1093`;
  histmem:S6).
- `toast.inc:197-311` (bounded scan under cli, printable only, TTL clamped to
  the signed range; histsys:S11, ui:S5); `assoc.inc:1964-2045` and
  `ascw.inc:384-445` (counts checked against the bytes actually read;
  files:S7, text:S9); `vidsel.inc:382-405` (the 8086 does not mask a rotate
  count, so the kind is range-checked first; video:S12); SYSTEM.CFG's bounded
  `_deser` (`driver.inc:5060-5135`; drivers:S9); `desksc.inc:1752-1872`,
  `335-403` (shell:S5); boothd's two CHS guards (`boot/boothd.asm:378-422`;
  boot:S9); table dispatch bounded at `ui.inc:3022-3028`,
  `clockw.inc:71-76`, `files.inc:2410-2416` (gfce strength 3).

**Why it is right here.** There is no MMU and no protection: a bad length is
a write anywhere. A divide fault is worse than on any later machine. With
`sch_lock` held it freezes every task; on an IBM ROM the dummy INT 0 handler
masks the 8259 and the machine is dead, and on a 286 the faulting `div`
re-executes forever (histsys-2, gfce-1). A compare costs 4 bytes.

**Gate.** **Prose only.** `t_volsig`, `t_diskverify` and `t_image` check the
images the build MAKES, not what the kernel does with a hostile one. There is
no ratchet on divides. **Proposed:** gfce-3's divide registry, every
`div`/`idiv` under `kernel/` listed with the fence that makes it safe, the
shape of `tests/textsites.txt` (0 shipped bytes).

**Where it should spread.** In five of these the fix is already in the tree,
one routine away:
- dw-1 (corrected): two unguarded divides on a hostile FileSize in
  append/write-at and READ_AT. **The truncate arm already has the guard**
  (SPEC.md 18.4.7.5). +~4 and +6-8 `.cold`.
- loader-1 (high), loader-2, loader-3: a compressed image shorter than its
  clear prefix makes the decoder write ~64KB past the region, and The Wire
  feeds that arm network bytes. **The module's `cmz_uncomp` already refuses
  `prefix >= image`** (loader:S8). About +18 `.cold` for loader-1 and -3
  together, +6 for loader-2.
- gfx-1 (high, corrected): gfx_blit4 trusts the sign of its width.
  **gfx_blitp already fixed exactly this**, citing PaccMan. 0 bytes
  (`jz` to `jle`, same encoding).
- text-5: the menu layout measures package strings unbounded.
  **`menu_trunc`'s `repne scasb` already bounds it.** +4..+8 `.text`.
- wm-1 (high, corrected): `wm_ptr2idx`'s 8-bit div raises INT 0 on a wild
  pointer while its comment says it is bounded. +5 `.text`, or wm-2's stored
  index (-8 `.text` +12 `.bss` kern_big).
- Also sysmisc-2 (a hostile 'CH' byte steps a `.bss` byte; +5 `.ovl`),
  ui-17 (`fm_stattab` indexed without `toast_say`'s bound; +4..+6),
  histsys-11 (`OSAPI_WM_WAKE` writes through an unvalidated BX from ISR
  context; +6..+12), gfce-4 (`OSAPI_FSX_SPK` divides by the caller's N with
  interrupts off), gfce-5 (deskmod `.make`), shell-12 (a driver's service
  record half-sanitised), dr-8 (corrected: the read-ahead's phase divide
  could #DE on a large partition; divide as `dsk_xfer` does).
- The backstop for whatever the ratchet misses is K9's refusal model plus
  gfce-1: route INT 0 into the existing death panel (~+30 resident), which
  names the task instead of masking the PIC.

**Price of spreading.** About +53 to +67 resident bytes for the listed
fixes, +5 `.ovl`, and 0 for the gate.

### K2. Facts are checked in the artifact that runs: `%error` at the declaration, the ABI decoded from the binary

**The pattern.** A layout trick (two bytes read as one word, a fall-through
pair, a table that must match an enum) is asserted by `%error` beside the
declaration, so the edit that breaks it fails the build. The ABI is checked
against the bytes `nasm` emitted, not against a description of them.

**Exemplary.**
- 636 `%error` sites across `kernel/` (engsys:S0): the paired stores in
  `sched.inc:1018-1020`, `2345-2347`, `3005-3022` (sched:S8); `ui.inc:1401`,
  `1469`, `3309-3320` (ui:S4); the vga12/softgfx word pairs (gfx:S6); the
  disk cells read as one (`disk.inc:247`, `347`, `2570`, ...; diskread:S10);
  `viddet.inc:309`, `326-331` (video:S3); kern_mega's scratch lines and
  strides (`mega.inc:105-127`; megacore:S5); `wm.inc:124-166` (wm:S1); the
  icon run stream `icons.inc:863-908` (text:S6); the cursor shape tables
  (`CUR_ROW`/`CUR_SHAPE_END`, mcur strength 3).
- Content rules enforced at the line that breaks them: `TOASTSTR`
  (`kernel.asm:340-367`), section-checked macros through `__?SECT?__`, KDS /
  KSEG / CSDO refusing a section where their premise is false
  (`kernel.asm:389-560`; gkm strength 3), the os88ui combinations that mean
  nothing (`os88ui.inc:434-449`).
- `t_api_abi` decodes every cell out of `kernel.bin` by shape and resolves its
  target (`tests/unit/t_api_abi.py:440-579`; abi:S2, engsys:S3);
  `tools/abilock.py` makes a fingerprint move a recorded decision (258
  decisions, 5 bumps; abi:S3); `t_mirror` compares every name defined in more
  than one file, so a new mirror is covered the day it appears (abi:S10,
  engsys:S5); `os88sym` refuses any map that is not byte-identical, and the
  `%warning ks:` line hands `kernsize` the real ladder (engsys:S2).

**Why it is right here.** There is no linker: nothing resolves a symbol
across files, and a reorder of `.bss` that splits a one-word store is
invisible until a tick lands between the halves on the field machine. An
assertion costs 0 bytes.

**Gate.** **Held**: the assembler itself, `t_api_abi`, `t_abilock`,
`t_mirror`, `t_kdfar`, `t_kdapi`, `t_invariants` (the three run-time
invariants no `%if` can express, checked by who writes). What is not held is
the habit of adding the `%error` at a NEW trick.

**Where it should spread.**
- abi-3: `t_api_abi` decodes one kernel of four; gkm-2: kern_small's refusal
  stubs, where abi-2 (FS_OP clobbers AX), dw-8 and video-1 live, are decoded
  by nothing; gkm-4: the parity gate does not cover kern_mega. 0 shipped.
- md-5: `t_mirror` does not scan `kerndos/`; engsys-11: the C SDK's constants
  and thunks are outside both `t_mirror` and the lock; drivers-16: the
  private VX/XM/HD ABIs are versioned by hand; gi-8: package includes copy
  kernel conventions without a mirror gate. 0 shipped.
- Layout tricks still held by prose: mc-9 (nine GDT scratch selectors; the
  compactor rebases SLD but not BACK), input-15 (`mou_b0`/`mou_b1` in other
  tables' high bytes; 0 or +4), text-6 (fprog's adjacencies half asserted),
  ui-4 (`ui_cmd`'s jump table has no length assert against `CMD_LAST`),
  video-11 (the vid_ctx record-0 invariant). 0 bytes each.
- The prose that copies code: engsys-3 (SPEC.md's hand copies of code are
  ungated and wrong; have `t_mirror` read the fenced blocks, or generate them
  between markers as the kernsize baseline is), engsys-19 (1,476 `; out:` and
  804 `; clobbers:` lines nothing parses), boot-10 (let the build CHOOSE
  KSIG_OFF and SPL_RESIDENT instead of checking hand-typed values).
- **The lesson from boot-1 belongs here**: `t_canary` stayed green while the
  canary was inert on two shipped disks because it modelled transfer runs from
  the data area rather than from data + BOOT2_SECS. A gate must model the
  artifact; it inherits every assumption it does not read off the build.

**Price of spreading.** 0 shipped bytes throughout.

### K3. One source of truth, expanded wherever it is needed

**The pattern.** A table, a layout or an algorithm is written ONCE as data or
a macro and expanded into every place that needs it - the counters, the
asserts, the two images, both loaders - so two copies that must agree word for
word cannot drift.

**Exemplary.**
- `SCH_PARTITION` (`sched.inc:198-269`): the slice layout written once,
  expanded into counts, asserts, `sch_stkbase` and `sch_stksize`, with order,
  evenness, minimum and total as build errors (sched:S1). The reviewer calls
  it "the pattern every hand-mirrored table should copy".
- `CFG_MAP`/`CFG_DATA` (`driver.inc:894-1000`): the SYSTEM.CFG parser emitted
  into the boot overlay and CTRL.DRV from one macro, resident in neither
  (bytes:S5, drivers:S4).
- `vid_tab` (`viddet.inc:132-205`): four numbers are the whole difference
  between Hercules and CGA (video:S0); the row table built by iterating
  `gfx_nextrow` (`viddet.inc:968-1042`; video:S1).
- `hbm_ptrfix` (`hiber.inc:5654-5670`): one object written by the writer and
  compared by the reader (hiber:S1); `MPK_SUMLINES` (`megados.inc:121-134`):
  one sum macro, two takers that chunk differently (megados:S3); `KDL_FIELDS`
  (`kerndos/kdlaunch.inc`): the launch block as a wire format, one list walked
  on each side (megados:S7).
- `fm_layout` (`files.inc:3109-3219`): one layout cache for painter and
  hit-tester (files:S8); `cp_probe 0/1/2` (`ctrl.inc:440-449`): one page
  ladder is hit test and action (sysmisc:S4); `cur_geom`, the only place a
  position becomes an address (mcur strength 2); `wm_apply_wh`
  (`wm.inc:1948-1982`): preference, clamp, floor as one sequence (wm:S6);
  `gfx_inktab`, the one colour reduction (gfx:S3).
- Shared as SOURCE, not copied: `kernel/mouproto.inc` with kern_dos
  (input:S11); `hbstage.inc`/`hbstub.inc` between HIBER.DRV and kern_dos
  (hiber:S4); kern_dos assembling `disk*.inc` unchanged (megados:S8); one
  os88ui body in two worlds through the `UI_*` macros (os88ui strength 1);
  `os88la.inc`/`os88ub.inc` (gms strengths).

**Why it is right here.** Four kernels, four geometries, modules and images
that cannot call each other: a second copy is a second place to fix every
field bug, and nothing links them to catch the miss.

**Gate.** **Partly.** `t_mirror` holds constants, `t_kdfar`/`t_kdapi` hold
the kern_dos inclusion, `tools/os88index.py` generates and checks
`docs/INDEX.md`. Code duplication itself is **prose only**.

**Where it should spread.** Every one of these is a second copy that has
already drifted or carries a twin's bug:
- shell-4 (corrected): the dock painter is hand-copied into DOCK.DRV and the
  copies have diverged beyond their geometry; shell-5: three textual copies
  of the far-frame helper; files-15: three copies of the module far-hop and
  two macro vocabularies; wm-14: `wm_dmg_bands` and its banner twice, with
  dead inner arms; sched-11: `fsx_rtick` is a third copy of the chain swap;
  input-17: two copies of one formatter; md-21 (unchecked): the A20 code
  written six times with four budgets (0..-20).
- loader-8: `ld_unreserve` is a drifted twin of `mem_free_rec` and misses the
  page-floor lift (~-3 `.cold` kern_big, ~-20 kern_mega); loader-12: three
  clear-prefix ladders; dw-21: two predicates each for LFN and mounted-image
  detection (+2 `.cold`); md-13: ten kdback doors are `call X / ret` round
  the same contract (-40 each kern_dos image).
- Spellings that should be one name: video-14 (the kind-class idiom at ~10
  sites), video-19 ('which display' four ways; -6 resident), sysmisc-12
  (W_FLAGS 'visible' is the literal 2 at ~30 sites), shell-6 (a Locator item
  poked by raw offset from five sites).
- The documents: abi-10 and engsys-5 (contracts written in up to four places,
  disagreeing both ways - keep the SDK header and the cell comment, which the
  ABI lock fingerprints, and delete the rest); engsys-10 (AGENTS.md has
  diverged from CLAUDE.md by 726 lines: generate it as the INDEX is).

**Price of spreading.** About 0 to -60 resident, -40 per kern_dos image,
mostly source lines.

### K4. One door, one environment: a single site owns the dispatch, the transfer and the end of a transaction

**The pattern.** Everything of one kind goes through one site: every window
callback through `wm_pkgcall` and `ui_bill`, every disk transfer through
`dsk_xfer`, every write's cache invalidation through `disk_write`, every
mouse backend into `mou_apply`, and every drawing transaction ends at
`gfx_unlock`, which resets all the state a caller could leak. A fix made at
the door reaches every caller, and a forgetful caller inherits the safe
value.

**Exemplary.**
- `wm_pkgcall` (`wm.inc:396-505`): re-entrant, far pointer read from the
  record, DS from W_SEG, ES = KERNEL_SEG, pen and depth banked per call
  (wm:S0, abi:S4, histsys:S3); `ui_bill` (`ui.inc:1219-1247`): one
  stamp/bill/call body for nine callback kinds (ui:S0).
- `dsk_xfer`: read and write differ by one byte, so CHS, the cylinder guard,
  DMA bounce, EOT and retries exist once (`disk.inc:1387-1405`, `2458-2532`;
  diskread:S0); the sector cache invalidated at the one write choke point
  (`disk.inc:6654-6713`; diskread:S5).
- `gfx_unlock` (`vga12.inc:5149-5269`): region, pen, mono1, disabled pen,
  display context, progress widget and busy shape all die with the hold -
  EndPaint without an explicit release (gfx:S7). The file-progress widget is
  armed at the disk choke points and torn down only there
  (`fprog.inc:440-520`, `1181-1270`; text:S5); the busy clock likewise
  (mcur strength 6).
- `mou_apply` and `MOU_SETTLE_STORES` (`mouse.inc:4139`, `1162`): serial,
  PS/2, driver and VMware share one back end and one settle (input:S1).
- `drv_call` range-fences BX before trusting DS (`driver.inc:2399-2470`;
  drivers:S3, histsys:S7).

**Why it is right here.** Every rule in these doors came from a field report
(drivers:S2 says so of the dispatcher), and on a machine you cannot attach a
debugger to, a fix that has to be repeated at each caller is a fix that will
be missed at one.

**Gate.** **Prose only** for the doors themselves. `t_btnrules` (a registry
of button sites that own the gesture by hand) and `t_chromesites` are the
closest models.

**Where it should spread.** The live gaps are callers that go round a door
that exists:
- ui-2 (corrected): content-click, right-click and W_ONDRAG read the handler
  before `gfx_lock` and never re-test liveness - the race SPEC.md 13.9.2
  already fixed for timers. Put the re-test in `ui_bill`: +5 `.text`, -5
  kern_big from the `.tp` arm, net about 0. ui-3: `ui_dispatch` identifies
  Locator by reading a package segment without the lock (-19 `.text`).
- wm-13: window-pointer validation differs door by door (-12..+30 `.text`
  for one rule); wm-7 (corrected): `OSAPI_WM_DESTROY` and `_FULLSCREEN` draw
  without the gfx lock (+6..+19).
- text-1: `fpg_bare` banks the clip count but not `[gfx_hole]`, which every
  other bank site does (+13 `.text` kern_big). gfx-5 and text-3:
  `[gfx_color]` after gfx_blit4 and font_run depends on the path that drew
  (+~8 to preserve, or 0 to document). histsys-17: the implicit display
  context is eight scattered bytes reset one by one (-4..-8). gfx-13: each
  whole-shape primitive orders hide, display enter and depth by hand, and the
  comments record five field bugs from getting it wrong (~0, code motion
  into one prologue).
- Context rules no door checks: dw-4 (the UI-task-only rule for every file
  cell; +9 `.text` or +14 `.cold`), loader-5 (no nested start, UI task only;
  +15..+21 `.cold`), dr-2 (the batch bracket's "UI is frozen" assertion at a
  door packages can call; +16 `.cold`). ctx-7 proposes tags plus one gate for
  all of them (0 shipped).

**Price of spreading.** About +40 to +120 resident.

### K5. Post, do not do: work moves to the context that owns it, and a forgotten post is late rather than lost

**The pattern.** A request that must run on a particular context (the UI
task, with the lock taken fresh, with none of the requester's code on the
stack) is POSTED as a bit and reaped there. The post is one OR-byte the tick
also re-sets, so over-setting is free and a setter that forgets costs 55 ms,
never a dead feature.

**Exemplary.**
- `[ui_post]` (`ui.inc:730-752`, `1081-1088`): xchg test-and-clear,
  twenty-six setters, the tick sweep re-sets it (ui:S1, histsys:S2). The
  reviewer: "Copy it for any new deferred flag."
- The deferred close (`ui.inc:869-897`): PostMessage(WM_CLOSE) done right,
  slot-reuse safe, with none of the package's code on the stack (histsys:S4).
- The Standard File dialog ends itself by posting `[fdlg_act]` and closing
  in `fdlg_reap` with the lock taken fresh (`fdlg.inc:531-690`, `946-966`);
  `fdlg_gate`'s ZF/CF lets both filters freeze input between post and reap
  with no tests of their own (files:S1).
- kern_mega's `mega_fkill` posts callback faults to `ui_task` (`mega_fkq`,
  `mega.inc:2659`) and releases a lock the faulting task held
  (`mega.inc:2694-2701`; ctx strengths).
- CTRL.DRV's one deferred SYSTEM.CFG write with retry (`ctrl.inc:349-370`;
  sysmisc:S6); held streams deferring dirt to `gfx_unlock` (the gate dw-15
  proposes to reuse).

**Why it is right here.** Worker slices are 128 and 192 bytes and the task
stacks 384; only the UI task may touch a file or compact the heap. Doing a
teardown or a repaint where the request arose is how a small stack carries a
full repaint chain.

**Gate.** **Prose only**, apart from `t_wakedrain` (every event-queue drain
gives a package's wake back) and `t_drvclaim` (a driver's service task may
not reach a claim door).

**Where it should spread.**
- histsys-1 (high) and ctx-1: a dying worker tears down and repaints other
  packages' windows on its own 128/192-byte slice - a plausible canary trip
  at close. Post the teardown to the UI task as the deferred close already
  does. +15..+30 `.text`/`.cold`.
- ctx-2: a worker that dies holding the gfx lock deadlocks the desktop on
  kern_big and kern_small. **kern_mega's `mega_fkill` already releases it**;
  the same four-instruction owner check, +9 `.text` (and -13 `.mega`).
- hm-3 and mem-10 (one defect): DESK.DRV, or any module, dropped on a worker
  while the UI task may be executing inside it. Defer the drop to the UI
  task: +8..+12 resident.
- drivers-4: `snd_release_inst` calls into SOUND.DRV with interrupts off, and
  that teardown makes kernel calls. 0 bytes (move the block above the cli).
- ui-8: the SDK and SPEC.md 55.4 tell authors a worker may call the
  clipboard, which the code says it must not. 0 bytes (doc).
- dw-15: defer the post-commit FAT flush (frees only) to `gfx_unlock`, the
  gate held streams already use (+15 `.cold` kern_big).

**Price of spreading.** About +32 to +66 resident.

### K6. Publication by a single store, and the lost wakeup closed inside one IF=0 window

**The pattern.** On an 8086 an interrupt can arrive only between
instructions, so one store is the whole atomicity argument: state is
published by writing the deciding word last (or both halves in one word),
and every sleep tests its wake condition and commits to sleeping inside one
interrupts-off span, with `sti`'s shadow covering the `hlt`.

**Exemplary.**
- `T_STATE` written last at spawn (`sched.inc:915-1000`); `sch_wake_ui` a
  one-word store feeding two lost-wakeup guards (`sched.inc:696-724`,
  `750-800`); the UI task's `[sch_uiwake]` test and `task_sleep`'s state
  store in one IF=0 window (`ui.inc:1156-1184`) (isr:S0, isr:S4, sched:S3,
  histsys:S0, ui:S2). The idle measured 96.9% halted.
- `gfx_lock`'s flag and owner in one word store (`vga12.inc:5118-5124`,
  `5262`): an OS/2 RAM semaphore in four bytes, whose `want`/`last` fairness
  debt took press-to-menu latency from 1.4-14.7 s to 37-70 ms
  (`vga12.inc:5082-5136`; histsys:S6, ctx strengths).
- The dock damage span as its own flag, exchanged under pushf/cli/popf
  (`dock.inc:660-730`; shell:S2); the tone router's grant, steal and release
  each in one cli window (`snd.inc:290-346`; drivers:S10); `CURBAR_ON`
  stores `[sch_cur]+1` so a flag an ISR reads is task-scoped
  (`mouse.inc:66-74`); `toast_now` decides on one word read
  (`toast.inc:392-410`; ui:S6); `menu_hover` reads the pointer once under cli,
  closing a race measured at 4 wrong launches in 320 (`menu.inc:2254-2300`;
  text:S8); the event ring's sample/promise rule (`events.inc:95-116`,
  `wm.inc:2343-2370`; histsys:S1, sched:S7, isr:S2).

**Why it is right here.** It is the cheapest correct concurrency there is,
and the only kind that costs nothing on the 18.2 Hz tick path.

**Gate.** **Prose only** for the ordering; the `%error` adjacency guards (K2)
hold the word pairs.

**Where it should spread.**
- sched-6: `evq_push` wakes the UI task before the record is in the ring
  (0 bytes, reorder). sched-2 (corrected): `sch_rhook` returns IF=1 on its
  skip path on kern_big (0 bytes: take the sti after the skip tests).
- isr-6 (corrected): four IMR read-modify-writes at IF=1 on task context
  (+6 `.text`); input-9: `mou_p2_off` writes the 8042 with nothing excluding
  int 09h (+~12); hiber-16: two non-atomic BDA tick reads (+6 HIBER.DRV);
  gms-4: kern_mega's SUB family reads a base and writes a descriptor with no
  `sch_lock` (PLAUSIBLE timing).
- The model's one missing piece, sched-15: a task can block only on time.
  `sch_wake_ui` is a one-task special case of a per-task `task_wake` with a
  sticky pending bit (AmigaOS Signal, SysV wakeup), +24..+30 `.text` and an
  additive cell; it would also retire sched-17's polling gfx_lock waiters.

**Price of spreading.** About +6 to +18 resident for the fixes, +24..+30 for
sched-15.

### K7. Commit order: the deciding record is written last and removed first

**The pattern.** Every mutation that spans more than one write is ordered so
that a crash anywhere leaves either the old state or the new one plus
reclaimable debris, never a record pointing at something half-built. One
order is used everywhere, and the "worst case is lost clusters" argument is
written at each site so it can be checked locally.

**Exemplary.**
- Shadow replace (`diskw.inc:1576-1597`): the old chain is held until the
  entry write and freed after it, so a power cut never costs the user's bytes
  - DOS truncated in place (diskwrite:S0). One order (data, FAT flush, entry,
  free, flush) in mkdir, delete, rmdir, rmtree, truncate, append-grow, held
  streams, fsck repairs and defrag moves (`diskw.inc:666-743`, `906-913`,
  `3838-3879`, `4007-4057`; `fsck.inc:1704-1724`; diskwrite:S1).
- Hibernate deletes the old pointer and image first and writes the pointer
  last (`hiber.inc:1853`, `1928-1938`); the cloner writes LBA 0 last
  (`diskops.inc:2068`) (hiber:S0). The reviewer: "should be copied into
  installers and SYSTEM.CFG writes."
- fsck's stateless-kernel, generation-checked, bounded-step model that moves
  only into free clusters and stops cleanly when the disk moves under it
  (`fsck.inc:20-22`, `fsk_step` `770-787`; diskwrite:S8) - "the pattern for
  any long-running kernel service".
- The same discipline off the disk: W7's anchor is cleared before anything is
  decided, so a recovery that fails is never tried twice
  (`megados.inc:2116-2128`; megados:S6); `kd_xunp` walks the stream dry
  before writing anything (`kerndos/kdxpack.inc:39-44`; megados:S10); the
  live resume tears nothing down until the last moment (`kdresume.inc:208-235`;
  megados:S11).

**Why it is right here.** Floppies are pulled, power is cut, and one
SYSTEM.CFG write is about 2 s of frozen UI: the protocol is what makes the
user's data survive the machine it runs on.

**Gate.** **Prose only** (the soak rows exercise it; no host check reads the
order).

**Where it should spread.**
- dw-2: a failed write on multi-window FAT16 can turn walked free space into
  permanent lost clusters, since the whole-FAT snapshot became a window
  (+12 `.cold` kern_big, +9 kern_small). dw-3: an unreadable FAT window reads
  as 'Disk full' after a retry storm (+4-5). dw-5: a failed FAT flush keeps
  its dirt, which `dsk_fatw_park` may later write to ANOTHER disk (+3).
- dw-13 (corrected): a whole-file write discovers FERR_FULL only after
  writing everything it could; the size is known up front (+18-25 `.cold`).
- SYSTEM.CFG: drivers-17 (records a kernel does not know are dropped at every
  save; +~40 CTRL.DRV) - carry them through, as the keyed-record format
  (drivers:S9) was designed to allow; gfce-6 proposes a domain column.
- hiber-10/hiber-11: Write Img lays sector 0 over a short disk and reports
  'Wrote', and never checks the target can hold the image (+8, +10
  DISKOPS.DRV). hiber-5: nothing fences heap compaction during the
  progressive image write (+10 HIBER.DRV).
- dw-23 (unchecked): a 'volume dirty' mark while a hold or deferred dirt is
  pending (+20 if taken).

**Price of spreading.** About +37 to +45 resident on kern_big, plus module
bytes.

### K8. Teardown by attribution: the kernel believes nothing the package says

**The pattern.** The kernel undoes third-party work from its OWN records:
windows destroyed by their W_SEG stamp, claims freed by an owner word the
package cannot forge, windows before regions, and a call into anything absent
refused rather than jumped through.

**Exemplary.**
- The loader's `.abort` and `.sweep` (`loader.inc:1509-1563`, `1796-1805`):
  windows by stamp, claims by owner word, XMS released, the package's own
  toast re-homed so its reason survives (loader:S3). The reviewer: "Copy this
  shape wherever the kernel undoes third-party work."
- `mem_own` takes the owner from the stub's ES, so it cannot be forged
  (`memory.inc:5894-5994`); teardown frees windows before claims
  (`instance.inc:1716-1724`), so no repaint far-calls into a freed region
  (histmem:S1). The entry proc is an identified dispatch, so grants made there
  are attributed to it (`loader.inc:1318-1356`; loader:S4).
- `mod_gone`: an unloaded module slot is `stc / retf`, never a jump to 0:0
  (`mod.inc:282-284`; histmem:S8, memory:S7); `drv_call` refuses a stray or
  unloaded row before trusting DS (drivers:S3); `mega_ldt_free` nulls every
  row naming a freed selector (gms strengths); kern_mega's fault closes only
  the faulting package, with per-task catch frames and loop caps
  (`mega.inc:2540-2725`; megacore:S2, gfce strength 6).
- Stateless, name-based file calls leave nothing to reclaim at app death
  (`diskw.inc:394-423`; histsys:S9).

**Why it is right here.** No MMU means the kernel's records are the only
protection there is; a teardown that trusts the dying package's view inherits
whatever corrupted it.

**Gate.** **Partly.** `t_pins`, `t_movable` and `t_moddrop` hold who may
move and who must drop; the teardown order itself is **prose only**.

**Where it should spread.** The driver layer is where the discipline stops:
- drivers-1 (high): one global `drv_wcnt` stands in for per-driver worker
  counts, so with USBMOUSE.DRV's life-long worker, unloading ANY other driver
  stalls ~5 s and leaves it detached but still published. Count per row:
  +~25 `.cold`, +DRV_MAX (11 on kern_big) `.bss`.
- drivers-2: nothing counts who is inside a driver (+~12 `.cold`, coarse
  form); drivers-6: a driver image is freed with no check that its IRQ
  vectors were unhooked (+~25 `.cold`; isr-13's one owner for IRQ lines, the
  IVT and the IMR is the full form, about +100 resident net, mandatory at the
  flag day as ABI-BREAK-LIST L3).
- shell-1: the Sticky App library's lifetime is keyed on a proxy (IF_DESK)
  instead of on who bound it - a latent use-after-free (+4..+6 DESK.DRV).
- loader-7 (`[ld_said]` leaks out of unreported loads, silencing the next
  failure; +8 `.cold`), loader-9 (DESK.DRV stays loaded when a widget launch
  meets a full instance table; +~5), mem-4 and files-4 (the purge floor is
  lifted by ANY teardown and its stamp tells no UI-task user apart; +8
  `.cold`), hiber-2 (kern_big's DOS-handoff refusals leak the extent claim,
  **fixed only on kern_mega**; +20 HIBER.DRV).
- histsys-2 / isr-8 / gfce-1 (one item): CPU exceptions go to the BIOS dummy
  handler. Route INT 0 (and #UD on a 286+) into the existing death panel
  (~+30 resident); gfce-2 records kern_mega's close-the-package model as the
  later step.

**Price of spreading.** About +110 to +125 resident, plus isr-13 if taken.

### K9. One predicate decides both the grey and the refusal; refusal is a contract, not a hole

**The pattern.** The question "can this be done here" is answered by ONE
routine that both the greying of the control and the kernel's refusal call,
so they cannot disagree. What a build or machine lacks answers CF=1 rather
than disappearing, and callers branch on capabilities, never on the CPU tier.

**Exemplary.**
- `hb_pick` is THE predicate for greying, probing and the write target
  (`hiber.inc:522-548`; hiber:S2); `fsx_capstab` for greying and refusal
  (`fsx.inc:170-187`, `600-607`; video:S8); `vid_dual_ok`'s three consumers.
- A cell kern_small lacks answers CF=1 (`driver.inc:6719-6769`; abi:S6) -
  OS/2's Family API without a bind step; `OSAPI_XMEM_CAPS`, `SND_CAPS`,
  `FSX_CAPS` answer what is present (abi:S7); XMS's tier-0 refusals are
  byte-identical to what packages already meet on the target (memory:S8).
- Refusal with a probe in the primitives: `gfx_blitp` DI bit 15, `gfx_spans`,
  `gfx_blit1` CF=1, each caller with a correct slower path one branch away
  (`vga12.inc:2927-2940`, `3541-3545`; gfx:S4).
- Disjoint error alphabets so a missing `jc` gives a wrong answer rather than
  a plausible one (`diskops.inc:64-85`; hiber:S7).

**Why it is right here.** SPEC.md 47: grey a fact, never a guess. On the
target a feature that silently costs seconds is the defect; a refusal costs a
compare and makes every caller own a fallback.

**Gate.** **Prose only** (SPEC.md 47 rule 5). `t_api_abi`'s cell-shape
ledgers see the refusal stubs on kern_big only (K2).

**Where it should spread.**
- abi-6 (high) and gfr-5: a package cannot ask which cells the kernel has, so
  a new package on an old kernel with the same PKG_FMT calls past the table.
  Publish an API level in `0060:0006` (free padding, 0 resident) and in
  package-header byte 31 (`os88pkg.py:122` already keeps it 0), with a load
  refusal through LD_EVER (~+8 `.cold`; loader-6). This is the same
  question/refusal pattern lifted to the ABI.
- abi-9 and gfce-8: CF is the only common refusal signal; one error CLASS in
  AX would make refusals comparable (~0 now). gi-6: kern_small's stubs leave
  AX undefined, so the shared fallbacks cannot tell "no door" from a real
  refusal; abi-2: kern_small's FS_OP overwrites AX against the contract (0);
  dw-8: kern_small's WRITE_AT stub answers AX = the caller's offset (0..+3).
- Grey and refusal that disagree today: video-12 (FSI_PAGES advertises pages
  `fsx_page` refuses; +8 kern_big, +4 kern_small), video-13 (EGA's caps row
  omits mode 0Dh, which every EGA has; 0), files-3 (the Save box commits
  names `dskw_name83` refuses; +8 `.cold`, +8 FDLG.DRV), dw-21 (two
  predicates for one question).

**Price of spreading.** About +20 to +30 resident; abi-6's level is 0
resident.

### K10. Pay only while used: on-demand modules behind a one-compare resident gate, boot-only code at the heap floor

**The pattern.** Code that is not needed every second lives in a file and a
heap claim; the resident part is the gate that answers the frequent question
("is anything showing?", "is this hook armed?") in one compare, plus slots
that rest at `mod_gone`. Code needed only at boot lives in the blob, which
sits at the heap FLOOR and is given back and compacted away, and code only
one class of hardware runs is slid away on the others.

**Exemplary.**
- The boot blob at the heap floor (`boot/boot2.asm:135-155`,
  `kernel.asm:6098-6121` `mem_unblob_x`): every boot-only body rejoins the
  free run, where a transient at the top would strand itself above unmovable
  claims - better than DOS's SYSINIT, which was lost memory in practice
  (boot:S0).
- `DRV_ATTACH` and the settings parser expanded only into images
  (`driver.inc:3790-4135`; drivers:S4), `DRV_H_KEEP` as DOS's INIT break
  address on a word that must already be zero (`driver.inc:3834-3859`;
  drivers:S5); the mouse probe split by lifetime with each edge named in
  `tests/ovlrefs.txt` (`mouse.inc:504-583`; input:S7).
- Resident gates: `cp_tick_due_x` answers once-a-second questions so CTRL.DRV
  is never read off a floppy to learn nothing is showing
  (`ctrl.inc:1229-1247`; sysmisc:S3); `fdlg_hook`'s one compare replaced a far
  call measured at 1.12% of the machine (`ui.inc:845-868`; files:S2);
  EXTD.DRV's slots rest at `mod_gone`, so a one-card machine pays a far call
  and nothing resident (`kernel.asm:7091-7121`; video:S10); the shell's two
  principled hook shapes and the zero-resident far frame onto `cw_kretf`
  (`dock.inc:366-383`, `dockmod.inc:1663-1694`; shell:S3, shell:S4).
- Hibernate: a few hundred resident bytes against a 7.8KB module, the cloner
  ~42 against 10KB, with job state in the claim the operation takes anyway
  (`diskops.inc:107-155`; hiber:S6). The `.vga` rung's slide
  (`kernel.asm:8771-8854`; bytes:S3, gfx:S8) and XMEM.DRV as an owner-loaded
  overlay (memory:S8, rt strengths).

**Why it is right here.** kern_big must reside in 100KB and kern_small in
64KB (CLAUDE.md's three rules); a module relieves both `KERN_BUDGET` and
`KERN_CODE_MAX`, `.cold` relieves only the second.

**Gate.** **Held**: `t_moddrop` (every module dropped somewhere;
`tests/moddrop.txt` is empty), `tests/vgarefs.txt` with os88ovlchk rule 2f,
`t_coldsym`, `t_noovl`, `t_bootfloor`, `t_resident`, and guard 1's
`KERN_BUDGET`.

**Where it should spread.** These are the review's largest byte levers, and
all of them keep the gate-in-front shape:
- video-17 (corrected): the mono renderer is resident on VGA-only machines
  that never draw 1bpp: -1,100..-1,500 resident on VGA kern_big at the
  desktop. text-7 (corrected) is the mirror image: font_run_x's VGA prologue
  resident on mono machines belongs in the `.vga` rung (~-185 resident, mono
  kern_big). rt-4 records that the two are complementary.
- input-12 and rt-7: the keyboard mouse (~456 bytes) is init-lifetime code;
  hold it from boot, drop it at settle (-300..-380 resident, both kernels).
- bytes-2 and rt-3: the PS/2 runtime and the XMS bodies, slid or loaded
  away on an 8088 with neither VGA nor EGA (-250..-320; rt-3 prefers an
  owner-loaded overlay to a rung).
- bytes-3, corrected by rt-2: FILECP and FDLG on kern_big as modules cached
  at boot, the COMMAND.COM transient model: about -2,500 resident, not
  -3,600.
- video-7 (corrected; -82..-86 resident kern_small, fsx tables for modes it
  can never set), md-6 (corrected; ~-1,025 per kern_mega kern_dos image, a
  live resume it can never run), md-7 (-512 `.bss` per shipped kern_dos),
  md-19 (corrected; -22 conventional resident on kern_mega), hm-11
  (corrected; the parts loader stays resident in every non-loader parts
  package, -0.8..-1.5KB package per running instance), drivers-10
  (unchecked; driver file names resident, -~95).
- **The caution that comes with it** (shell-2, corrected; rt-1): every
  desktop gesture and every Sticky Apps menu press loads and drops a module,
  which the shell's own comments call too slow elsewhere. rt-1's answer is
  to drop to a purgeable CACHE and let the need/drop bracket be the pin, so a
  second press costs no `int 13h` while the heap has room - the gate stays in
  front and the module stays reclaimable.

**Price of spreading.** Savings, not costs: from a few hundred to about
1.5KB resident per machine class, and they do not simply add (video-17 and
text-7 apply to opposite machines).

## Part 2. Other patterns worth keeping

Each is shorter, either because it is narrower or because it is already well
held. Same four questions.

**K11. Private ISR stacks that set SS as well as SP.** `MOUPRIV` loads SS
with no register (`mouse.inc:395-425`); the int 08h chain moves stacks only
when SS = LOW_SEG and `sch_switch` refuses a foreign SP (`sched.inc:2202-2214`,
`2412-2425`) (input:S3, isr:S1, sched:S5). Takes ISR and ROM-chain depth out
of every task slice and survives SeaBIOS's stack hops. *Gate:* prose only.
*Spread:* isr-2 (corrected; `osapi_mouse_feed` swaps SP and not SS, +~13
`.text`), ctx-5 and isr-3 (every ISR-callable door assumes SS = LOW_SEG),
mc-1 (high, corrected; kern_mega's reflection reads `.lowbss` through a lent
SS, fatal on limit-checking CPUs: read through ES = LOW_SEG by name as
`mega_codesel` does, +16..+40), mc-2, and sched-7 (corrected; the two private
stacks have no canary and sit back to back, +9..+12).

**K12. Instruments ship with the design.** The DD block, the (LBA, run)
trace and the field-labelled failure toast (`disk.inc:593-720`; diskread:S9)
are what made the AL bug and the head-crossing write findable off real iron;
the unconditional 0xCC fill, the canary on every switch including task 0's,
and a death panel that moves off the dead stack first (`sched.inc:984-1006`,
`2588-2720`; sched:S2, histsys:S10, gfce strength 5); the KFZ heartbeat
(`sched.inc:1940-1955`; isr:S6); the 'MO' block asserted by offset
(`mouse.inc:6520-6560`; input:S10); BOOTDIAG naming a missing sector
(boot:S2). *Gate:* `%error` for the MO block; otherwise prose. *Spread:*
isr-14 and ctx-4 (no interrupt-latency budget is written; the longest IF=0
windows are now the compactor's copies and the cursor draw in the serial ISR,
2.4-3.2 ms measured - write the budget and a ratchet, 0 shipped), sched-8
(STKDIAG scans its high waters from the wrong end, knob only), sched-9 (the
death panel trusts the table it calls suspect, +4).

**K13. Probes fail toward safety, and verify the transfer rather than the
table.** Summing a sector a crossing run delivered, not reading back your own
int 1Eh patch (`boot2.asm:401-470`; boot:S1); a range check on DL rather than
a fallback (`boot.asm:266-298`; boot:S4); the presence probe retires a drive
only on the positive absence shape seen in the field (`disk.inc:10700-10971`;
diskread:S8); the RTC ladder runs read-only probes first and claims a chip
only on agreement (`clock.inc:266-394`; sysmisc:S0); ISA-honest video probes
that defeat the floating bus (`vidsel.inc:137-181`; video:S4); the formatter
reach test writes a marker so every test is positive (diskwrite:S9);
`WF_QUIET` leaks toward a dark monitor, not a lit one (`blank.inc:925-946`;
sysmisc:S10). *Gate:* `t_canary` and `t_resident` for the boot half - and
boot-1/boot-2 (both high) show the gate itself drifted from the artifact
(K2). *Spread:* video-2 (high), video-3, video-4 (corrected): detection
trusts first answers, so a plain MDA gets Hercules timings, an MCGA is driven
as a VGA and a 64KB EGA as planar (+25..+35, +8..+12, +4 `.boot2`, all
transient); input-1 (a 16550 with its FIFO already on is treated as an 8250,
+6 `.ovl`); dr-4 (status 06h, the AT's free swap signal, is discarded, +16
`.cold`); md-3 (W7 tests the A20 gate once; poll as `kdh_tchk` does, +8
`.boot2`); md-11 (the park stub's 'press a key' accepts a PS/2 mouse byte;
`megares.inc` already solves it with A7h, +4).

**K14. Tables versioned by their own length, and header bytes that must
already be zero.** `DRV_H_DSV`/`DRV_H_FSV` record how long a driver's tables
were when built, and the kernel zero-fills the rest - Win32's cbSize in one
byte (`driver.inc:4060-4107`; abi:S8, drivers:S1); `LD_H_CLASS` adopted a
byte that was always 0, with an out-of-range value getting the largest class
(`loader.inc:114-126`; loader:S7); SYSTEM.CFG as keyed records with a version
per key (`driver.inc:690-860`; drivers:S9); the launch block's
zero-means-unfilled (megados:S7). gfr's strengths list the three free spots
that can carry a minor version without a bump. *Gate:* `t_abilock` sees the
SDK. *Spread:* gfr-5's API level (K9), abi-11/gfr-3's window class record
(the flag-day replacement for ten handler words and seventeen setter cells,
~-200..-260 resident kern_big), drivers-17's carry-unknown-records rule.

**K15. Plan first, then act; a dry pass before any write.** Compaction plans
and runs in one body whose direction is a mask, and runs only if the plan
funds the claim (`memory.inc:2883-3064`; memory:S3, histmem:S4); `mem_avail`
answers the question `mem_claim` asks (`memory.inc:4867-5040`; memory:S5);
`kd_xunp` and `cmz_uncomp` check before writing (megados:S10, loader:S8). At
about 2.7 ms a KB of memcpy on a 5150, moving only when it pays is the
difference. *Gate:* prose only. *Spread:* mem-3 (a doomed claim sheds every
cache and may park once per shed; precheck +14 `.cold`, +6 for one park per
claim), dw-13 (K7), dw-14 (allocate a contiguous run when the size is known,
+70 `.cold` kern_big), loader-1/-3 (K1).

**K16. The heap's model: an out-of-band record table, relocation by
callback with a pinned default, purgeability in the owner word.** Free is one
word store and an overrun cannot corrupt the map (`memory.inc:1064-1258`,
`6261`; memory:S0, histmem:S0); the purge class and rank ride the owner word
at zero record cost, and the notice is a zeroed master word, the Mac's
purged-handle-is-NIL (`memory.inc:305-351`, `1379-1515`; memory:S1,
histmem:S2-S3); relocation through the holder's own dispatcher, so access is
a plain segment load and only a move pays (`memory.inc:3839-3962`;
memory:S2); 64KB DMA-page safety held in the allocator and across moves
(memory:S6, histmem:S5); the out-of-band map is also what lets kern_mega park
only in-use KB (megados:S4: "a reason never to put headers inside heap
blocks"). *Gate:* `t_movable`, `t_pins`, `t_pgrank`, `t_heapgran`. *Spread:*
hm-9 and mem-15 (the one capability every period GUI had: package purgeable
claims, delivered through the existing MC_RLOC proc with DX = 0, +30..+70
`.cold`), mem-16 (recency within a rank), mem-5 and hm-6 (the DMA head door
still has the KB-shift carry bug that `mem_regrow`'s `cmp ax, 1024` fixed,
+3 `.cold`).

**K17. Every pixel written once, decided once.** `font_run_x` decides a run's
inks, row base, count and plane grouping once and draws row-major from a
pre-walked glyph table (`font.inc:1400-1600`; text:S0); the menu bar's
composition IS the diff (`menu.inc:1021-1290`; text:S2); the desk cell
composed in RAM and put down with one `gfx_blit1` (`desk.inc:1673-1973`;
shell:S1); `dest ^= (pat ^ dest) & mask`, three bus accesses and no stale
write-back (`softgfx.inc:788-792`; gfx:S0); `cur_move_mono` writes each byte
once, proved over 36,000 random moves (mcur strength 1); the damage engine's
exactness tricks (`wm.inc:6242-6375`; wm:S2). *Gate:* `t_textrules` with
`tests/textsites.txt`, `t_btnrules`, the `deskflash` row. *Spread:* gfx:S0's
own instruction ("copy it to every remaining 1bpp RMW"), gfx-9 (the 1bpp XOR
interior is a byte loop, +~8), shell-14 (kern_small still draws a desktop
cell two and three times, +~20), wm-16 (corrected; the content white fill is
still the default double-draw), sysmisc-13, text-12 and shell-15
(unchecked).

**K18. A long-running service as stateless, bounded, generation-checked
steps.** fsck (K7), the file clipboard's step machine whose Replace question
is a trip through the event loop with all state in `.bss` and whose tree walk
is stackless (`filecp.inc` `fcp_step` `910-1012`; files:S4), the iterative
rmtree that re-checks emptiness at every removal (`diskw.inc:977-1203`;
diskwrite:S7), coherence by broadcast instead of re-mount
(`files.inc:2184-2273`; files:S6). *Gate:* prose only. *Spread:* hiber-5
(K7), dw-7 (corrected; `DSKW_RT_MAX` bounds a tree's size, not a cycle's
depth), gi-4 (the READ_SEQ cursor names no file and stays trusted across a
quiet folder change), dw-22 (WRITE_SEQ's one stream record thrashes under two
streams).

**K19. The same-build rule, and stamps instead of guesses.** The resume stub
overwrites the running kernel with byte-identical code, so it needs no
relocation and every other build is refused (`hiber.inc:2372-2383`;
hiber:S3); modules carry a build and a layout stamp (memory:S7); the park
stub is the parked kernel's own build (megados:S2). *Gate:* in the code.
*Spread:* hm-7 (a lazy part is re-fetched by 8.3 name with no check that the
file is the one launched, +20 per parts package), md-15 (unchecked; check the
park header before jumping into it).

**K20. Build-matrix hygiene.** One product name per build with implied
variants and refused combinations (`kernel.asm:190-246`), a configuration
layer resolved once above every include (`kernel.asm:868-1112`, ~700
conditionals on features rather than build names), and kern_big and
kern_small proved byte-identical across every kern_mega-only change (gkm
strengths 1-4). *Gate:* `%error`, `t_buildmatrix`, `t_khpknobs`. *Spread:*
gkm-7 (module configuration still spoken in build names), gkm-1 (a fix made
on one arm leaves the shipped kernel with the defect - see Part 3), gkm-4.

**K21. Cell shape follows heat, and a trade is re-priced per build.**
`OSAPI_ICELL`/`IEND` (52-57 cycles against 159-162 for a slot, with a length
mismatch an assembly error; abi:S1, bytes:S6); `ct_` trampolines that fall
back to a plain far call when a build's site count drops (`kernel.asm:8066-8136`;
bytes:S2); the shared prologue and epilogue ladders, 3 bytes a site, gated
fail-closed by `stkbalance` and `t_asmrules` (`kernel.asm:8449-8560`;
bytes:S0). *Gate:* `t_stkbalance`, `t_asmrules`, `t_api_abi`. *Spread:*
bytes-1 and gfr-1 (the rare-cell door at the flag day, ~-400 resident, at
about twice the cycle cost bytes-1 quoted), gfr-2 (rehearse it on the
kernel's own `cw_` shims now, no ABI).

**K22. State that can only be stale in the safe direction.** Derived rather
than tracked (`wm_zoom`, `wm_tarm`; wm:S3), one-shot arguments cleared by the
consumer (`wm.inc:5468-5481`; wm:S4), `dsk_mgen` odd for ever so a zeroed
cursor never matches, `os88_rseq` re-seeding on any mismatch (gi strengths),
`[ui_post]` over-set (K5). *Gate:* prose only. *Spread:* wm-3 (corrected;
`wm_paint_dmg`'s empty-rect arm leaks every one-shot argument into the next
pass, +8 `.text`), wm-17, ui-9 (W_ONMOUSEUP's "guaranteed delivery" is false
when the ring dropped the release, +12), loader-7 (K8).

**K23. The engineering system's own habits.** Budgets derived from a stated
KB figure (engsys:S1), a generated and checked INDEX (engsys:S6), a harness
whose `check()` prints want/got and the first differing offset (engsys:S7),
tier questions charged in CPU rather than wall clock (engsys:S8), and the
shape of `t_stackprose` - a document that names a size must name the size the
kernel has. *Spread:* engsys-6 (running counts in contract prose rot: the
`t_stackprose` shape fits), engsys-2 (high; a citation that resolves to the
WRONG section passes - check the cited heading's title), engsys-7 (tier
admission broken by ~17 one-package fast rows), engsys-8, engsys-9.

## Part 3. Already solved on one arm - copy it home

gkm-1's finding generalises: kern_mega was built later and more defensively,
and several of its answers are better than what the shipped kernels run.
These are the cheapest copies in the tree, because the design is proven:

| kern_mega already does | the shipped kernels still | finding | bytes on kern_big/kern_small |
|---|---|---|---|
| releases a lock the dying task held (`mega_fkill`, `mega.inc:2694-2701`) | deadlock when a worker dies holding the gfx lock | ctx-2 | +9 `.text` |
| posts callback-fault teardown to `ui_task` (`mega_fkq`) | tear down on the dying worker's slice | histsys-1, ctx-1 | +15..+30 |
| closes the faulting package on a CPU fault | hand INT 0 to the BIOS dummy | histsys-2, isr-8, gfce-1, gfce-2 | ~+30 (to the panel) |
| restores the image's own 8259 masks at wake | wake with the fresh boot's masks, re-arming the losing serial port | hiber-1, hiber-14 | +10..+14 HIBER.DRV |
| unwinds the DOS-handoff refusals | leak the extent claim and strand the user's folder | hiber-2 | +20 HIBER.DRV |
| ships the XT redraw knobs KHP_DMGSTALE/RAISEOL/RELIT/CAPTION | run without them on the XT they were measured for | wm-11, sysmisc-9 | +194 resident if all four (+32 for RELIT alone, MEASURED) |
| guards the dual-card Hercules page (`%ifdef KERN_BIG`) | kern_small enables page 2 on a Hercules + CGA machine | video-1 (high) | ~+13 kern_small |

The rule gkm-1 proposes is the right one: a fix discovered on one arm lands
unconditionally in the same commit unless the arm is the reason for it.

## Part 4. Deliberate designs the reviewers checked and endorsed - do not "fix"

These look odd and have been "fixed" before or will be tempted:

- `cpu_detect`'s no-override `[bp+0]` x87 scratch encoding is the mechanism,
  and a "fix" breaks 8087 detection (`cpudet.inc:117-139`; boot:S10).
- `spl_ifp`, the interrupt's own far pointer, closed a one-boot-in-fifty race
  by construction rather than by cli (`kernel.asm:5242-5254`; boot:S7).
- Explicit lazy loading of parts (`OP_LAZY`), not automatic reload: on a
  one-floppy machine an automatic reload is a disk-swap prompt (histmem:S9).
- The self-region widening in `mem_find_own` is a documented choice that
  `mem_owner_of_x` depends on (mem-6, corrected; tighten FREE and REGROW
  only).
- Do not unify the kernels at run time (gkm-5, priced); do not flatten
  kern_small to one code segment (gkm-8); the two-segment tax (~700 bytes on
  kern_big, ~540 on kern_small) is too thinly spread to cut (bytes-4).
- Refuse a single handle model: windows already have one and memory keeps its
  record (gfr-4); launch keeps the index rather than posting the name
  (loader-15); no shared code library is worth having on the target (rt-8).
- Core settings in battery-backed CMOS: considered and not recommended
  (sysmisc-17, unchecked).
- Pause stopping the BIOS tick on an XT is a documented trade-off; write the
  time-of-day cost down, do not change the behaviour by default (isr-1).
- Parity pads serve the 16-bit buses (gfx-17, unchecked: dropping is not
  recommended), and `t_hotpar` holds them.
- Refuted, so leave them: WireBall's two-instruction queue tail (isr-5) and
  `wm_anim` yielding with the gfx lock (wm-20).

## Part 5. Scorecard, one line per area

*Code* and *docs* are the reviewer's verdict as verified; *biggest risk* is
the highest-severity surviving finding, or the systemic gap where nothing
rated high. "Drift" means comments and SPEC text describing retired designs as
current, which every one of the 25 reports found; the count is the verified
doc fixes in that area's data file.

| area | code | docs | biggest risk |
|---|---|---|---|
| abi | The surface's mechanics are the best part of the kernel and beat every period peer | Contracts in up to four places, stale (26 fixes; abi-10) | No runtime API level: a new package on an old kernel calls past the table (abi-6, high) |
| boot | One of the best-reasoned parts of the tree; boot-only code costs no resident byte | 40+ comments narrate history as current values (45 fixes) | The canary is inert on kern_big's 360KB/720KB disks and the splash runs uncanaried code first elsewhere (boot-1, boot-2, high) |
| bytes | Close to the floor of routine-by-routine work; nothing left in the old shapes | SPEC.md 2.6 and KERNEL-MEMORY.md tables stale (20 fixes) | No live defect; the levers are structural (rare-cell door, residency tiers) and 4,791 bytes of callee-save push/pop |
| diskread | Good as a mechanism: one transfer routine, arithmetic divide safety, a measured cache | The worst drift in the review: headers of deleted routines, SPEC 18's mount sequence (61 fixes; dr-16) | Boot partition mounted with AH=08h geometry while addressed with the BPB's: FIELD-NOTES 33 still live (dr-1) |
| diskwrite | The best-argued subsystem: one commit order, shadow replace | SPEC 18.4 describes a kernel several passes old (39 fixes) | Hostile FileSize divides (dw-1) and the multi-window FAT16 chain leak (dw-2) |
| drivers | One of the most carefully built parts; very little resident | snd.inc's banner describes a speaker-only kernel (40 fixes) | Lifetime: one global worker count stalls every unload and leaves a driver half-published (drivers-1, high) |
| engsys | Strong: 636 `%error`s, derived budgets, the ABI read from the binary | SPEC.md's front matter, the part read first, is the most wrong (40 fixes) | Wrong-section citations pass and the review's own reports once failed `make` (engsys-1, engsys-2, high) |
| files | The most mature subsystem; the 2026 merges left little duplication | SPEC 22 and 54 describe the retired global listing (39 fixes) | A shed inside a kern_small mount can hand the listing's destination to a claimant (files-1, high) |
| gfx | Better than its comments: inner loops priced for the 8088, refusal with fallbacks | Comments describe two dozen things that no longer exist (40 fixes; gfx-12) | gfx_blit4's negative width overruns `vga_pbuf` into the heap; 0-byte fix (gfx-1, high) |
| hiber | Among the best-shaped: tiny resident part, crash-consistent commit | SPEC 87.4-87.7, 18.99 and 51.11.4 drift (26 fixes) | The wake repairs devices one by one instead of restoring the image's masks (hiber-1, hiber-14); unwind leaks (hiber-2) |
| histmem | The memory model is sound and in places better than every peer | Retired names (`OSAPI_MEM_AVAIL_LVL`, `0x0568`) still cited (29 fixes) | Disciplines stop at a neighbour: the IF=0 regrow copy (hm-1) and a module dropped on a worker (hm-3) |
| histsys | Several pieces beat every peer (blocking UI task, drop-oldest ring, deferred close) | SPEC 8's MAX_TASKS and canary text, SPEC 7's "exactly two" (15 fixes) | Mechanisms on the wrong context: a dying worker repaints others on its slice (histsys-1, high); no INT 0 handler (histsys-2) |
| input | One of the strongest; every rule traces to a named field machine | ~30 places: folded names, moved constants, SPEC 9's intro (53 fixes) | The tick can park a ROM int 09h with IRQ1 in service, a predicted floppy timeout under int 13h (input-2) |
| isr | Careful and mostly correct; the typical defect is no longer a missing cli | SPEC names the wrong longest IF=0 window (11 fixes) | Preconditions the kernel's ISRs meet and the driver/package doors do not (isr-2); no latency budget (isr-14); exceptions to the BIOS (isr-8) |
| loader | One of the best-engineered corners; the file is the image | SPEC 21 and 29 describe the v2 pool and things that are gone (55 fixes) | A compressed image shorter than its prefix writes ~64KB past the region, reachable from network bytes (loader-1, high) |
| megacore | Careful, and the model is right; better fault model than Windows 3.0's UAE | The file header and BOCHS.md describe the retired static GDT (17 fixes) | A reflection on the lent stack reads `.lowbss` through SS: fatal #SS on a limit-checking CPU (mc-1, high) |
| megados | The park is among the best-engineered code in the tree | SPEC 87.9's retired resume route; `t_mirror` misses `kerndos/` (25 fixes; md-5) | Rarely-run recovery edges (md-1's check that never runs, md-2/md-3's A20) and ~1KB dead resume per image (md-6) |
| memory | The best-argued subsystem in the kernel by its own reviewer's measure | Orphaned headers of inlined routines; SPEC 66.3's no-park design (40 fixes; mem-12) | Interrupt latency (mem-2, mem-8) and MEM_OWNER_MAX capping the kernel at eight driver images (mem-1) |
| sched | Invariants checked rather than trusted; one frame, one resume path | ~40 stale statements, including the SDK's worker DS = KERNEL_SEG (60 fixes; sched-10) | Small defects (sched-1's IF=0 slice fill, sched-7's canary-less ISR stacks); the systemic gap is no wait-on-anything primitive (sched-15) |
| shell | Among the most carefully engineered: one reconciler, every pixel once | A long tail of pre-size-pass-14 comments (38 fixes) | The Sticky App library can be freed under a widget that bound it (shell-1); per-gesture module load/drop (shell-2) |
| sysmisc | Structurally sound and almost entirely non-resident | SPEC 31/37/64/76/79 describe retired designs (70 fixes, the most) | The clock gains ~31 s a day, 0-byte fix (sysmisc-1); a hostile 'CH' byte (sysmisc-2) |
| text | The strongest 8088 engineering in its part of the kernel | ~45% of the files is comment, much of it history (38 fixes; text-14) | Latent only: `fpg_bare` misses `[gfx_hole]` (text-1); an unbounded menu measure (text-5) |
| ui | The model is sound and in places better than the period systems | ~2 comment lines per code line; SPEC 13's loop is several redesigns old (59 fixes; ui-15) | About indexes past its table on a VESA desktop (ui-1); SPEC 13.9.2's race open on three callback paths (ui-2) |
| video | The geometry core is the area's strongest engineering | ~40 statements in SPEC 39 and 53 stale (42 fixes; video-15) | kern_small enables the Hercules second page on a Hercules + CGA machine (video-1, high); MDA given Hercules timings (video-2, high) |
| wm | The most carefully measured subsystem; Part 5's rows honoured | 52% comment, much per-pass byte history (28 fixes; wm-15) | `wm_ptr2idx` raises INT 0 on a wild pointer (wm-1, high); one-shot arguments leak (wm-3) |

**Across the scorecard.** Code is rated sound to excellent in all 25 areas;
documentation is the weak column in all 25. The one documentation pattern
that is good where it exists and should spread is boot-9's: a comment states
the INVARIANT and the guard that enforces it, and the ledger of past values
goes to `docs/history/` once, as `KERN-BUDGET-LEDGER.md` already does for the
budgets - with a `t_stackprose`-shaped gate for any running count a document
repeats (engsys-6).

## Sources

- `data/*.json`, all 25: every `strengths` entry is cited above as `area:Sn`;
  findings by id.
- `areas/*.md`: the *Strengths* and *Verdict* sections of all 25 area
  reports, and the *Strengths* sections of `gap-api-shape-includes`,
  `gap-context-lock-model`, `gap-fault-containment-error-model`,
  `gap-flagday-reconcile-abilist`, `gap-kernel-matrix-near-model`,
  `gap-mega-unowned-spans`, `gap-mouse-cursor-half`, `gap-os88ui-kernel-copy`
  and `gap-residency-tiers`.
- `synth/systemic.md`, `synth/bugs.md` and `synth/phase1.md` cover the same
  findings as defects, rethinks and work packages; this file is the other side
  of those ledgers and does not repeat their ranking.
