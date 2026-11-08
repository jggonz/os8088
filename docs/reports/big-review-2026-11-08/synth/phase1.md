# Phase 1: the kernel work that needs no ABI break

Synthesis of the 2026-11-08 kernel review, written 2026-11-08. It covers the
25 area and lens reports (`areas/*.md`) and their verified findings
(`data/*.json`). This is the **no-break** half of the owner's two phases. It
covers weaknesses, performance, size, consistency, rethinks that land without
a break, the documentation system and its gates, and on-demand module moves.
**Plain bug fixes are excluded**, because another synthesist has them.
Appendix B lists every hazard handed to that pass, so nothing falls between
the two documents. Flag-day and record-only findings are out of scope. The few
that bear on a phase-1 decision are in Appendix C.

## How to read this

- **Input.** There are 469 findings. Two are refuted and dropped. 365 are
  phase "now", and of those 66 are `bug` (the bug pass), which leaves **298**.
  Each of the 298 is placed exactly once: in a work package (WP), or routed to
  the bug pass (Appendix B). Appendix A is the index, generated from the data.
- **Verdict marks.** After each finding id: **[C]** confirmed, **[K]**
  corrected (the byte and line figures quoted here are the corrected ones),
  **[U]** unchecked (lower confidence; it is named as such where it carries a
  decision).
- **Bytes** are quoted as the reviewers quoted them: signed, ESTIMATED unless
  marked measured, and placed in a section.
  - *Resident* means `.text`, `.bss` or `.cold`. **`.cold` is resident on
    kern_big and kern_small.**
  - *Module* means a `.DRV` image, and *transient* means `.boot2`, `.ovl`,
    the boot sector or the blob.
  - On **kern_mega**, `.cold` and `.mega` are above 1MB and do not count
    toward `MEGA_RESIDENT`. kern_mega figures therefore separate *conv*
    (conventional, budgeted) from *high*.
  - `.vga` is resident only on VGA/EGA machines, and the `KERN_BUDGET` gauge
    is taken on VGA, so the kern_big column counts `.vga` unless it says
    otherwise.
  - Per CLAUDE.md, rungs are never the argument. Every figure is a byte
    figure.
- **Ranking.** The WPs are ordered by value per byte. A saving ranks high. A
  zero-byte gate ranks by what it stops. Growth has to be paid for by
  something measurable on a 4.77 MHz 8088.
- **Rows.** "Rows" names the `tests/suite.py` rows that cover a WP. Per the
  owner's testing rule, a WP verifies on the host and names its rows, and the
  deduplicated soak union runs **once after the last WP**. A number marked
  *measure first* is one that decides a design (PERFORMANCE.md rule 4). The
  agent choosing takes it once.

## Summary: the work packages, ranked

Figures are resident bytes, signed, for the recommended core of each WP.
Optional parts are listed inside the WP and are not in these totals.

| # | work package | kern_small | kern_big | kern_mega (conv / high) | risk | depends on |
|---|---|---:|---:|---|---|---|
| 1 | Free bytes: dead code, dead data, redundant tests | -126..-130 | -58 | -44 / -35 | low | - |
| 2 | Module lifetimes: pin, preload, discard | 0..+12 | **-3,408..-3,492** | 0 / +48..92 | high | WP17 (shed policy); bug pass histsys-1 |
| 3 | Hardware the machine lacks is not resident | -300..-380 | -290..-370 (gauge); -735..-885 on a mono XT | -300..-380 / 0 | medium | WP10 (ovlchk registry) |
| 4 | Driver tables diet | -46 | -202..-262 | ≈ kern_big, unmeasured | medium | WP6 (blob room) |
| 5 | kern_dos images and kern_mega's own dead weight | 0 | 0 (kern_dos image -560..-580) | -22 / -69; each kern_dos image -1,585 | low | WP10 (md-5 mirror scan) |
| 6 | Boot: let the build choose the constants, and make room | 0 | 0 (blob -41, boothd -9, KERNEL.SYS ~-400 on disk) | 0 | low | - (unblocks bug pass boot-1/2/5) |
| 7 | .bss arrives zeroed: one rule, a gate, the dead stores | 0..-20 | 0..-20 | 0..-20 / 0 | low | - (frees .ovl room for WP18) |
| 8 | Fault containment: INT 0/6 and the window-pointer divide | +28 | +34 | +4..+34 / 0 | low | WP7 (`.ovl` room) |
| 9 | A runtime API level | +8 | +8 | 0 / +8 | low | - |
| 10 | Gates that catch wrong facts, and suite hygiene | 0 | 0 | 0 / +8 | low | - |
| 11 | The KHP shelf: take or delete every measured knob | 0 | +286 (+104 every machine, +182 `.vga`) | 0 (already carried) | low | - |
| 12 | FAT write path: allocation and flush | 0..+3 | +105..+108 | 0..+3 / +105..108 | med-high | bug pass dw-2, dw-5 |
| 13 | An interrupt-latency budget and its gate | -2..+4 | -2..+4 | -2..+4 / +20..30 | low-med | bug pass mem-2/hm-1 (first catches) |
| 14 | Machine-readable routine contracts (the liveness gate) | 0 | 0 | 0 | low | - |
| 15 | SDK-side savings in packages | +8 | +38..+48 | +35..45 / +8 | medium | WP9 (level) |
| 16 | Read path, directory and listing | +10 | +165..+174 | 0 / +165..174 | medium | WP7 (`.ovl`, dr-14) |
| 17 | Heap claim and shed policy | 0..+8 | +28..+34 | 0 / +28..34 | medium | - |
| 18 | Scheduler: UI priority, a wait primitive, lock honesty | +56..+77 | +56..+77 | +56..77 / 0 | medium | WP7 (.ovl room) |
| 19 | API boundary: context tags, cheap fences, a checked build | +67..+85 | +62..+80 | +15..20 / +48..62 | medium | WP9, WP14 |
| 20 | Graphics hot paths | +32..+44 | +112..+134 (incl. `.vga` +80..90) | +24..36 / +8 | low-med | WP21 (region sort) |
| 21 | The damage pass | -8..+11 | +2..+21 | +2..21 / 0 | medium | - |
| 22 | Interrupt ownership and input robustness | +45..+50 | +96..+101 | +50..55 / +46 | medium | - |
| 23 | Video detection and fullscreen facts | +4 | +20..+22 (+36..46 `.boot2`) | +20..22 / 0 | medium | WP6 (blob room) |
| 24 | Documentation: one copy of each fact | 0 | 0 | 0 | low | WP10, WP19 tags |
| 25 | The comment discipline sweep | 0 | 0 | 0 | low | WP24 |
| 26 | Contract clarifications (one abilock update) | -8..-4 | -8..-4 | -8..-4 / 0 | low | - |
| 27 | Desk, dialogs and menus: user-visible facts | -16..+4 | +5..+35 | 0..20 / +5..15 | low | WP9 for new cells |
| 28 | The Control Panel engine and shared module source | 0 (CTRL.DRV -1,500..-3,000) | 0 | 0 | medium | - |
| 29 | DISKOPS and HIBER modules | 0 | 0 (modules +98..118) | 0 | low | bug pass hiber-1 |
| 30 | One clock | -10..+10 | -10..+10 | -10..+10 / 0 | medium | bug pass sysmisc-1/2/3; WP7 |

### Phase totals per kernel (resident, the recommended cores, ESTIMATED)

| kernel | savings | growth | **net** | net without WP2 | context |
|---|---:|---:|---:|---:|---|
| kern_small | -476..-620 | +258..+366 | **-110..-362** | -122..-362 | blessed KERN_SIZE 59,392 of 64,000 |
| kern_big (VGA gauge) | -3,962..-4,222 | +1,017..+1,176 | **-2,786..-3,205** | **+287..+622** | blessed 96,768 of 100,864 |
| kern_big on a CGA/Hercules XT | WP3's rungs save 445..515 more than on the gauge, and WP11's and WP20's `.vga` (+262..272) is not paid | | ≈ -3,490..-3,990 | | |
| kern_mega conventional (`MEGA_RESIDENT`) | -572..-748 | +206..+347 | **-225..-542** | same | budget 68,096 |
| kern_mega above 1MB | -104 | +497..+593 | +393..+489 | +345..+397 | not budgeted |
| kern_dos images | kern_big's -560..-580; each kern_mega image -1,585 | | | | the DOS program's memory |
| modules | CTRL.DRV -1,500..-3,000 (WP28, unprototyped); KBMOUSE +~450 new; DISKOPS/HIBER +98..118; blob/CTRL/HIBER +~100 each (WP4) | | | | not resident |

**What the totals say.** The phase is a net saving on every budgeted kernel,
but on kern_big almost all of it is WP2. Every other WP together is
**+287..+622 net on kern_big**: WP3, WP4 and WP1 save about 550..690, and
everything else spends about +1,000..+1,180. If WP2 slips, phase 1 still fits
the blessed slack (4,096 bytes), but it spends up to a sixth of it (7-15%). So **WP2 is the
phase's keystone, and its risk is the phase's risk**. The order below is
chosen to land it early, after WP17's shed policy, which it needs. The growth
items are each small and each pays for itself, but per the banner none of
them is free. They are listed in the order to drop them if the owner wants
the phase smaller: WP16 dw-12 (+90), WP11's `.vga` rows (+182), WP22's input
half (+53), WP27's new cells.

### The binding constraint is boot-time space, not the resident budget

On kern_big **both transient homes are full**. Several WPs below want bytes in
them, so they need a ledger of their own.

- **`.ovl`** is held by a hard `%error`. The window half must fit the FAT
  window plus dsk_secbuf (kernel.asm:8938-8944), which is 5,120 bytes on
  kern_big. The blessed kernsize block has `.ovl` at **5,116** on kern_big and
  kern_emu, and 5,115 on kern_mega. sched-7's verification says the same:
  "5,119 of its 5,120-byte bound". kern_small's `.ovl` is 1,474, so it is not
  constrained.
- **The blob** is BOOT2_SECS = 13 sectors on kern_big (kernel.asm:2111).
  boot-7 measured boot2_end at 0x19FC of 0x1A00 on build/kernel.map: **4 bytes
  free**. The blessed block's `boot2` 6,455 is older, so re-measure at the
  start.

| | frees | wants |
|---|---|---|
| `.ovl` (kern_big) | WP7 **-45** (sched-12 -30, sysmisc-10 -15) | isr-8 +12 (WP8), gfx-7 +8 (WP10), input-5 +5 (WP13), dr-14 +10 (WP16), sysmisc-4 +8 and sysmisc-5 +16 (WP30): **+59** |
| blob (kern_big) | WP6 boot-7 **-41**, drivers-12 -4, hiber-17 -9; optional boot-17 -30..-60 | drivers-10's staged name table +~100 (WP4), drivers-5 +2, boot-11 +6, video-2 +25..35, video-4 +4, video-8 +7, shell-8 +10, plus the bug pass's boot-2 +8 (it needs boot-7's room): **+162..172** |

**So: land WP6 and WP7 before anything that grows a transient section.**
Then, on kern_big:

- **`.ovl`.** Take isr-8, gfx-7, input-5 and dr-14 (+35 of the 45 freed).
  Put sysmisc-4/5's +24 in the blob, or move a body out of `.ovl` as the
  `%error` text itself suggests (SPEC.md 2.5.3.2).
- **The blob.** It cannot absorb drivers-10's table and video-2 together
  without a 14th sector, unless boot-17's kmain_o table lands. One more blob
  sector costs one more sector in the boot read, and no resident byte. It
  also moves guard 5's boot footprint by 512, and boot-18 is about to make
  that guard honest. That is the owner's call, and it is the cheapest
  "growth" in the phase. A host tally line for both sections in `kernsize`
  would keep the ledger honest.

## Watch list: what moves `apps/os88api.inc`, `drivers/os88drv.inc` or the lock's inputs

Every item below is **no-break**. It still moves an abilock fingerprint, so it
needs `python3 tools/abilock.py --update "why"`, with **no bump** unless the
item says otherwise. A change to `os88api.inc` moves **both** `PKG_FMT` and
`DRV_VER`, because DRV_VER's surface includes the SDK. The work should batch
these into as few `--update`s as possible: one per WP, and WP26 is a single
update.

| WP | item | file | lock action | the trap |
|---|---|---|---|---|
| 9 | abi-6/loader-6: header byte 31 = API LEVEL, `OS88_NEED` macro, `dw osapi_table_end` at 0060:0006 | os88api.inc | update, no bump | Before relying on "an old kernel reads 0", confirm that no PKG_FMT-10 kernel predates SPEC.md §57 (a mouse-instrument word once lived at 0060:0006, SPEC.md ~14071). Also check every other reader of the name field's 16th byte (mount harvest, Sticky Apps). |
| 2 | hm-9/mem-15: `MEMP_PURGEABLE` verb on OSAPI_MEM_P; a reloc proc called with DX = 0 means "purged" | os88api.inc | update, no bump | **On an older kernel, an unknown OSAPI_MEM_P verb falls through to MEMP_AVAIL and returns the caller's own CF** (hm-14, memory.inc:5582-5644). A new package would believe its claim is purgeable. Land hm-14's refusal (WP19) first, and have packages use `OS88_NEED` (WP9). |
| 15 | mem-17: a header class bit, "region movable" | os88api.inc | update, no bump | Safe direction: on an older kernel a new package simply runs pinned. |
| 15 | text-15: a one-shot run-style cell (bold) | os88api.inc | update, no bump | Appended cell. |
| 15 | text-16: `MENU_SEP` = 2 and `MENU_CHK` = 3 item prefixes | os88api.inc | update, no bump | An older kernel draws MENU_SEP as a **selectable blank item**. That is still "runs", but packages should pair it with `OS88_NEED`. |
| 16 | dr-5: `OSAPI_FILE_FIND_SEQ` and its 16-byte cursor | os88api.inc | update, no bump | Appended cell. |
| 18 | sched-15: `OSAPI_TASK_WAKE` | os88api.inc | update, no bump | Appended cell. |
| 19 | abi-7: geometry without a repaint | os88api.inc | update, no bump **only as an appended cell** | Do it as a new cell, **not** as CX's top bit on OSAPI_WM_RESIZE. ABI-BREAK-LIST A1's own precedent counts a flag bit on an existing cell as a re-contract, and so a bump. |
| 19 | abi-8: `; @ctx` tags in every contract comment | os88api.inc | update, no bump | Contract lines are fingerprinted, so the whole tagging is one update. |
| 23 | video-12: `FSIF_FLIP` (FSI_FLAGS bit 3) | os88api.inc | update, no bump | An older kernel leaves it 0, so "0" must keep meaning "probe OSAPI_FSX_PAGE". CLAUDE.md's WireBall advice stays true for old kernels. |
| 27 | ui-11 double-click cell, ui-12 clipboard generation, ui-19 W_ONMENU slot | os88api.inc | update, no bump | Appended cells and slot. |
| 27 | shell-3: OSAPI_DESK_ITEM remove with AH = 0FEh (by record) | os88api.inc | update, no bump | **Check what an older kernel does with AH = 0FEh** before shipping a caller. If it reads 0FEh as a zone, the call must sit behind `OS88_NEED`. |
| 27 | files-17: the completion callback gets AH = volume and BX = cluster | os88api.inc | update, no bump | Additive, because nothing is promised in AH or BX today (AH is 0 at the call). |
| 5 | mc-19: a "protected-mode clean" class bit | os88api.inc | update, no bump | kern_mega only. |
| 24, 26, 21 | contract text: FULLSCREEN (engsys-5), TASK_SLEEP on the UI task and its range (sched-4, sched-5, histsys-15), WM_TIMER range (wm-8), the clipboard is not worker-safe (ui-8), WM_WAKE's SS precondition (isr-3), the pen after BLIT4/FONT_RUN is unspecified (gfx-5, text-3), a negotiator may clobber what any callback may (wm-4), destroy/fullscreen rationale (wm-7), the MOUSEUP wording if weakened (ui-9) | os88api.inc | **one** update, no bump | Clarifications. ui-9's "unless the ring overflowed" weakens a promise; that is still no bump, but say so in the reason. |
| 4 | abi-5 (preserve DI, BP is spent), drivers-5 (delete "PRESERVE BX", now that the kernel saves it), isr-11 (DSV_TICK's real DS/ES/DF/BP) | os88drv.inc | DRV_VER update, no bump | drivers-5 is a relaxation, and lands only with the kernel push/pop. |
| 4 | histsys-7: read DSV cells in the driver's segment | (kernel) | none | **Use the per-read DRV_H_DSV length test.** The alternative, a "minimum table length" rule at DRV_VER, IS a bump: an older, shorter-table driver would read past its table into its name. |
| 10 | engsys-11: add os88.h and os88thunk.asm to PKG_FMT's files. drivers-16: new guards for vxabi.inc and the XM and HD ABI blocks | tools/abilock.py | re-record the lock; new VX/XM/HD guards | It moves the lock's **inputs**, not the SDK. No PKG_FMT or DRV_VER bump. |
| 5 | md-15: the park-header sum moves into KDL_PLOW's word | kernel ↔ kern_dos launch block | **KDL_VER 6 -> 7, kern_mega arm only** | A private-ABI bump: both sides ship together, but the kern_dos image is on a different disk from the kernel, so it is still a version decision. |
| 8 | wm-2: W_IDX grows the window record by a byte | (kernel) | none | WIN_SIZE is unpublished (os88api.inc:4185 says why). `tools/os88geom.py`'s mirror moves, and t_mirror checks it. |
| 22 | isr-13: an `OSAPI_IRQ` cell | os88api.inc / os88drv.inc | update, no bump **if added now** | Recommended: add only the IMR shadow now, and leave the cell for the flag day as ABI-BREAK-LIST L3. |

---

## WP1. Free bytes: dead code, dead data and redundant tests

**Goal.** Take every saving the reviewers found that needs no design: code
that can never run, a test that cannot change the answer, a constant held in
memory. Every item is local. Most are under 20 bytes, and together they are
the cheapest bytes in the phase.

**Findings.**

| id | what | bytes |
|---|---|---|
| `text-2` [C] | font_ink's `[vid_mono]`/`[gfx_mono1]` early-out cannot jump. Its only caller runs on a 1bpp display (font.inc:603-610, 789, 949-951, 1006-1008). It also saves ~40 clocks per mono glyph. | -14 `.text` big/mega, 0 small |
| `ui-3` [C] | ui_dispatch decides "is this Locator" by reading AM_ONCMD = 0 unlocked. `cmp word [menu_win],0` says it directly (ui.inc:2512-2536, menu.inc:497-503), and `or bx,bx` drops a 0 handler. This is also a hazard fix: a package's item 0 can close the front window. | -19 `.text` both |
| `wm-10` [C] | wm_lk's `gfx_lock_flag` test is implied by the owner stamp (wm.inc:3268-3307, mouse.inc:5381-5386) | -7 `.text` |
| `text-8` [C] | `[vid_popmax]` is a constant held in a word (viddet.inc:362, menu.inc:1764-1766) | -3 `.text` |
| `sched-14` [C] | sch_clsbytes rows 0 and 4 duplicate (sched.inc:2872-2878). Take it only while the file is open anyway. | -1 `.text` |
| `gfx-3` [K] | gfx_blit1 asks CLIPQF twice and nothing in between changes the answer. Carry ZF instead (vga12.inc:6019-6068). About -170 clocks a band on kern_big. | -3 `.cold` |
| `files-11` [K] | The empty-icon test ORs 64 bytes a row (~1,500 cycles). Have dsk_ico_stage's `.none` arm answer CF instead (files.inc:7741-7766). | -6 `.cold` |
| `dw-9` [C] | `test al,08h` skips both LFN and label entries (diskw.inc:4407-4410), which also lets a file share the label's name | -2 `.cold` |
| `loader-8` [C] | Write I_KIND/I_SPTR at step 5, so ld_unreserve becomes mem_free_rec_x. That also gives it the page-floor lift its twin forgot (loader.inc:216-262). | -3 `.cold` big, -20 `.cold` mega |
| `mc-11` [C] | Port 70h is write-only. Write 8Dh/0Dh outright (mega.inc:476-478, 14893-14895). | -2 `.cold` -2 `.mega` (mega) |
| `video-7` [K] | kern_small assembles fsx rows for VGA modes it cannot set: mtab rows 5-8, bmodes 5-7, the VGA caps row, and fsx_page's Mode X arm (smallk map: fsx_mtab 89DE..8A5C). Keep bmodes' id-8 byte, as fsx.inc:1194-1203 deliberately does. | -82..-86 kern_small |
| `drivers-12` [C] | drv_boot_x writes CH, and nothing reads it (driver.inc:5799, 5820) | -4 `.boot2` |
| `drivers-11` [C] `wm-14` [C] `text-13` [C] | Dead `%else` arms under OS88_DRIVERS ⇒ KERN_BIG; wm_dmg_bands written twice (DOCK_OPT == KERN_BIG, wm.inc:813-1104); two complementary parity nops that are one nop | 0 bytes, ~-200 source lines |

**Files.** kernel/font.inc, ui.inc, wm.inc, viddet.inc, menu.inc, sched.inc,
vga12.inc, files.inc, diskw.inc, loader.inc, mega.inc, fsx.inc, driver.inc.

**Bytes.** kern_small **-126..-130**, kern_big **-58**, kern_mega **-44 conv,
-35 high**; also -4 `.boot2` on kern_big.

**Risk.** Low. text-2 and text-13 touch hot-path parity padding, so they need
`t_hotpar` re-balanced (fast row `hotpar`). ui-3 changes how the Locator menu
is recognised, so check Locator with no app menu set and with a package set.

**Rows.** `hotpar`, `kernbudget` (fast); `kernresident`, `small128` (full);
soak `disp*`, `fmarrows`, `navcalls`, `vidfskeys*` (video-7), `rehome*`
(loader-8), `xorrect`, `dispblit`.

**Dependencies.** None. Land it first: it gives every later WP a smaller base
to measure against.

---

## WP2. Module lifetimes: pin, preload and discard

**Goal.** Give kernel modules the third lifetime that ONDEMAND-PLAN 7.1/7.2
designed and never built: **loaded, pinned while entered, kept as a
purgeable claim afterwards, and reloaded only if shed**. Then use it for the
two largest bodies that kern_big still holds resident.

**Findings.**

- `bytes-3` [C] **FILECP and FDLG on kern_big become boot-preloaded purgeable
  images**, on COMMAND.COM's transient model. kern_big keeps filecp.inc
  (1,657 `.cold` + 152 `.bss`) and fdlg.inc (35 `.text` + 2,080 `.cold` + 84
  `.bss`) resident, about 4.0KB measured. kern_small already ships the same
  bodies as FILECP.DRV/FDLG.DRV (kernel.asm:1093-1103). ONDEMAND-PLAN 1.2/1.3
  refused on-demand loading, because the load needs the system disk at the
  moment the user's disk is in the drive. That objection applies to loading
  on demand, not to an image that rides in KERNEL.SYS's one contiguous boot
  read (~6 packed sectors, about +120 ms on a 5150, ESTIMATED). Copy it into a
  `MEM_PG_LOW` claim at `mem_unblob`, and reload only if it was shed, through
  the refusal path kern_small already ships ("Needs Sys Disk A:", SPEC.md
  2.8.4). KERN_BUDGET counts every non-purgeable byte, so the gauge honestly
  drops by ~3.6KB.
- `mem-10` [C] `hm-3` [C] **The missing fact: "is a frame standing in this image".**
  Today inst_task_die → desk_drop_x → mod_drop can free DESK.DRV on a dying
  worker while the UI task is preempted inside `dkd_hover`. That call is
  made with no lock (instance.inc:1938-1940, ui.inc:1006,
  mod.inc:880-886). Nothing then faults: the UI task resumes in freed
  memory. The same missing fact is why MEM_K_MOD is pinned for life
  (memory.inc:2049-2050). The now-fix is to never free a module from a
  non-UI task: post the drop (in inst_launch_post's shape,
  instance.inc:2237) and service it at ui_task step 0. That deferred drop
  **is** the pin's foundation. The rethink is to bracket module thunks with
  wm_nest_pushd/pop, as xm_dsp already does (xmem.inc:434-438), so modules can
  later move like driver images.
- `shell-2` [K] The first two DISCARDABLE users, both UI-task-only with no
  re-entry. MOD_SETS (CTRL.DRV's settings core, 3,180 bytes measured) is
  loaded and dropped on every shortcut drag, delete or new link
  (desksc.inc:437-462). STIKMENU.DRV (7,876-byte image, 8KB claim) is loaded,
  rescans STIKAPPS\ and is dropped on every press (stikmenu.inc:60-88).
  desksc.inc:13-15 already admits that "2-3 s to load is a menu that does not
  come up".
- `shell-1` [C] Library lifetime by binder: a STIK_LIB widget can have the
  library freed under it (deskmod.inc:118-134, loader.inc:1458-1466). Set
  IF_LIB (I_FLAGS bit 3, free) on the LIB verb, test IF_DESK|IF_LIB in
  desk_drop_x, and let only the record's free clear it. This is AmigaOS's
  OpenLibrary/CloseLibrary.
- *Optional, same mechanism:* `hm-9` [K] `mem-15` [C] **package purgeable claims**.
  Add a `MEMP_PURGEABLE` verb, set MC_DMA bit 13, rank at TRIV, and call the
  claim's existing reloc proc with DX = 0 when it is purged. The known
  consumers are waiting: Skies' title art (-485 ms of exit,
  HANDOFF-APPS-SHARED.md:153) and the Text Screen shadow (~420 ms to
  rebuild). Correction [K]: bit 13 is free only once MC_DMA_HEAD narrows from
  0x3FFF to 0x1FFF (memory.inc:160), and every head reader masks with it.
  **See the watch list: this one needs WP19's hm-14 first.**

**What to build, in order.** (1) The deferred drop (mem-10/hm-3). (2) The
per-module entry count, held open-to-close for the dialog and across a copy.
(3) The discard arm in mod_drop plus a purge tag. Take shell-2's two modules
first. (4) The boot preload for FILECP/FDLG on **kern_big and kern_emu
only**. (5) Optionally, package purgeable claims.

**Files.** kernel/mod.inc, deskmod.inc, instance.inc, memory.inc
(mem_pg_own, mem_shed_one), filecp.inc, fdlg.inc, kernel.asm (FCP_MOD/FDLG_MOD
on kern_big), boot/boot2.asm (the read), tools/os88mod.py, desksc.inc,
stikmenu.inc, apps/os88stik*.inc, tests/moddrop.txt.

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| preload FILECP/FDLG (bytes-3) | 0 (already modules) | -3,600 resident; +60..100 `.cold` (pin, boot copy) | **0. Do not apply.** On kern_mega their `.cold` is already above 1MB, so moving them into a conventional heap claim would *cost* MEGA_RESIDENT. |
| discard tier (shell-2) | 0 | +40..80 | +40..80 high |
| deferred drop (mem-10/hm-3) | 0..+12 | +8..12 | +8..12 high |
| *opt.* package purgeable (hm-9) | refused (no compaction) | +50..70 `.cold` | +50..70 high |
| **core total** | **0..+12** | **-3,408..-3,492** | **0 conv / +48..92 high** |

**Risk.** High, the highest in the phase. It adds a new state to every module
call. The boot read grows by about 6 sectors. A purge under pressure brings
back the swap problem, and it is answered only by the refusal toast, which
bytes-3's own verification acknowledges. The ESTIMATED -3,600 assumes
MEM_PG_LOW counts as purgeable to `tests/kernresident.py`, so check that
first. Correctness of the drop deferral is easy to test (it is a post);
correctness of the pin is not.

**Rows.** `moddrop` (fast, and it will want the discard tier recognised as a
drop); `kernresident`, `small128`, `bootsmoke` (full); soak `fcpapi`,
`fcpcopy`, `fcpsmall`, `fcproom`, `fdlggrey`, `fdlgsmall`, `fdlgdrop`,
`fdlgup`, `fdlgchoose`, `stik`, `stikmenu`, `stikshort`, `stiknodesk`,
`desksc`, `deskitem`, `shedrelist`, `regrowshed`, and their `mega-` twins
(mega must stay unchanged). **New rows:** "shed FILECP under pressure, then
Save As → the refusal", and "Sticky Apps pressed twice → one disk load".

**Dependencies.** WP17's mem-3 must land first: a doomed claim must not shed
every purgeable module before it fails. The bug pass's histsys-1 (worker
teardown moves to the UI task) is the same post-to-UI shape as mem-10, so
whichever lands first provides the mechanism.

---

## WP3. Hardware the machine lacks is not resident

**Goal.** Finish the pattern that COLD_SLIDE started for VGA: code for
hardware this machine cannot have should not occupy its resident memory.
That means a rung slid over at boot, or an on-demand module.

**Findings.**

- `input-12` [C] **The keyboard mouse becomes a KBMOUSE module.** It is 456
  resident bytes on both kernels, measured: kbm_tab..kbm_isr (306, less
  mou_clamp), kbm_p5spend (16), kbm_pollm..kbm_ui (108), kbm_post (9), and 17
  bytes of state (build/kernel.map 0x43E9-0x455F; smallk 0x3054-0x31A6). On
  most machines a mouse retires it seconds after boot (SPEC 9.6.1.1). Its own
  header calls it "a FIRST CANDIDATE TO REMOVE" (mouse.inc:2402-2427).
  - Keep resident: kbm_key's two gates, KBM_SCAN's keypad-5 latch, kbm_shf,
    and the thunks.
  - Load the rest on the first claimed key, and drop it when [mou_ptr] is
    set. t_moddrop requires the drop.
  - **The owner's call** [C]: the first press costs one system-disk read
    (~400 ms on an XT) at the moment the mouse has failed, which is exactly
    the case CLAUDE.md's module rule asks about.
- `bytes-2` [K] **An AT rung**, slid over on any 8088 with neither VGA nor
  EGA. It would hold the PS/2 runtime (251 bytes `.text` measured:
  mou_p2wr..kbm_tab 0x28EE..0x29E9) and the XMS bodies past their `xm_have`
  gate (~110-130 bytes; the corrected figure, because the TIER-0 refusals and
  xm_release_rec's EMS/NET halves run on every machine). Gate it on
  [cpu_tier] with a registry of entry doors, the way `tests/vgarefs.txt`
  works (mouse.inc:1642-1646 banner, 1839-1841; xmem.inc:414-730). The
  PS/2 `.cold` init bytes can join it.
- `text-7` [K] **font_run_x's once-per-run VGA prologue moves into `.vga`.**
  `.planar`/`.vxp0` is 144 bytes (map 0x3E65..0x3EF5) and fnt_rn_edge's VGA
  arm is 43 more (0x3F78..0x3FA3). Both are dead on CGA/Hercules/MDA. On VGA
  it costs one entry jump per **run** (~15 clocks against a ~4,300-cycle
  prologue) and **+~10 on the gauge**, so it is the owner's call.
- *Optional, record-phase:* video-17 [K] is the mirror image: a **mono rung**
  slid over on planar-only, Hercules-less machines. It is about
  -1,100..-1,500 on the VGA gauge (sw_* is 1,108 bytes measured), with the
  Display page's CGA row greyed there (SPEC §47), because the VGA's own mode-6
  row draws through the 1bpp renderer. It is not in the totals: the reviewer
  filed it as record-only "if budget pressure returns". It is the largest
  single resident lever left after WP2.

**Files.** kernel/mouse.inc, xmem.inc, font.inc, kernel.asm (slide
decision 5757-5760, cfx_slide 8771-8854), kernel/mod.inc (mod_tab row),
tests/vgarefs.txt plus a new tests/atrefs.txt, tests/moddrop.txt,
tools/os88ovlchk.py (the door registry).

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| input-12 | -300..-380 | -300..-380 | -300..-380 conv |
| bytes-2 | n/a | 0 on the gauge; **-250..-320** on a CGA/Herc/MDA 8088 | n/a (always 286+) |
| text-7 | n/a | **+10** on the gauge; **-185** on mono | 0 |
| **total** | **-300..-380** | **-290..-370 gauge; -735..-885 on a mono XT** | **-300..-380 conv** |
| modules | KBMOUSE.DRV +~450 | +~450 | +~450 |

**Risk.** Medium. Every door into a slid rung must be registered, or a mono
machine runs `.cold`'s bytes; CLAUDE.md's `.vga` hard rule is the precedent.
The KBMOUSE claim path runs inside the keyboard path, so the first press must
not draw inside the ISR.

**Rows.** `ovlchk` (fast: the vgarefs registry, which needs an AT twin),
`moddrop` (fast); `ps2mouse` (full); soak `wheelps2`, `mega-ps2mouse`,
`msegxms`, `vidxms*`, `dosxms`, `vidresidentcga`, `vidresidentvga`,
`kernresident`, `disptext`, `glyphcost`. **New row:** a pointerless desktop
driven by arrow keys, which has no row today (no `kbm*` row exists), on a CGA
`_gla` machine.

**Dependencies.** WP10's door-registry gate shape (os88ovlchk). No others.

---

## WP4. The driver tables diet

**Goal.** Shrink what kern_big keeps per driver class, and make the driver
contract say what the kernel actually does. This WP is the one session
allowed to touch `drivers/os88drv.inc`'s contract text, in a single DRV_VER
`--update` with no bump.

**Findings.**

- `histsys-7` [K] **Seven 36-byte copies of the union DSV table** sit in
  resident `.bss`: drv_svc = DSV_SIZE × (DRVC_MAX-3) = 252 bytes on kern_big,
  72 on kern_small (driver.inc:6173). Only the SOUND copy is read by name on a
  hot path (20 sites); the others go through drv_cls_svc_x (~14 callers),
  and DSV_FS is already read through the driver's segment
  (driver.inc:2832). Keep class 1's copy and read the rest in place at
  [es:di+DSV_*]. **The correction matters:** drv_publish zeroes then copies
  only DRV_H_DSV bytes (driver.inc:4040-4100). So every in-place read needs a
  DRV_H_DSV length test, or it reads into a short driver's name string. The
  correction also re-prices the saving to -90..-140, not -150.
- `drivers-10` [U] with `bytes-8` [K] **Driver and module file names leave
  `.text`.** About 100 bytes of `*.DRV` names are read only by drv_load_at,
  and every caller that loads a class row is an image: the blob, CTRL.DRV or
  HIBER.DRV (driver.inc:1179-1226, 3486). The `.DRV` suffix is 36 bytes over
  9 mod_f_* names on kern_big and 20 bytes over 5 on kern_small
  (mod.inc:175-197, 431-432). Stage the stem through cp_sbuf, the CFGN_STAGE
  idiom. **Price it before committing** [U]: drv_load_at is also reached from
  blank.inc:683 and xmem.inc:285 for overlay rows, and the name block
  interleaves fdlg_s_drive and shares PCIETHER's tail.
- `drivers-13` [C] **Measure first**: every sound verb pays a
  `.text`→`.cold` far hop (~300 cycles, ~65 µs predicted;
  kernel.asm:8311-8312). If a counter confirms it, move drv_svc_call_x (~40
  bytes) into `.text`. That is -4 resident and +36 against KERN_CODE_MAX,
  which has 17,012 left.
- `drivers-5` [K] Make "PRESERVE BX" the kernel's job. Push/pop BX in
  `%1_call` (expanded in the blob, CTRL.DRV and HIBER.DRV: the blob is "full
  to the byte", so its +2 comes from WP6's -41) and in drv_call_ent. Then
  delete the rule from the driver contract. USBMOUSE.DRV was already bitten
  by it.
- `drivers-7` [C] drv_vol_bank pushes its pair onto the caller's stack (the kentc
  idiom), and deskmod's nesting workaround goes (drvvol.inc:135-182,
  deskmod.inc:79-100).
- `drivers-14` [C] OSAPI_FSX_SPK's open skips the 34.9 stream interlock that its
  close announces. Either ask snd_str_busy at the open (+8), or write in
  SPEC 34.11 that the bracket coexists with a stream.
- Contract text (os88drv.inc, one DRV_VER update with no bump):
  - `abi-5` [C]: the driver preserves DI and BP is spent. A kernel push/pop would
    cost +2 on ETHER's 272/384-byte worker chain, so the doc form is the right
    one.
  - `isr-11` [C]: DSV_TICK runs with DS = the driver's segment and ES = the
    kernel's. Also state DF, BP and the nested-tick skip (driver.inc:2538-2546,
    sb.inc:2555).

**Files.** kernel/driver.inc, mod.inc, drvvol.inc, deskmod.inc, snd.inc,
kernel.asm, boot/boot2.asm (via %1_call), kernel/ctrl.inc,
kernel/hiber.inc, drivers/os88drv.inc, drivers/sound/sb.inc (comment).

**Bytes.** kern_small **-46** (histsys-7 -36, bytes-8 -10). kern_big
**-202..-262**: histsys-7 -90..-140, drivers-10 + bytes-8 -120, drivers-13 -4,
drivers-5 +2..4, drivers-14 0..+8. kern_mega: about the same as kern_big; it
is mostly `.bss` and `.text`, so mostly conventional, and unmeasured.
Modules: +~100 in each of blob, CTRL.DRV and HIBER.DRV, and +2 in each for
drivers-5.

**Risk.** Medium. histsys-7 trades a publish-time snapshot for reads that see
later driver writes; the reviewer's verification flags that. drivers-10 is
unchecked, and its +~100 in the blob does not fit the blob's 4 free bytes
without WP6's room plus boot-17's, or a 14th blob sector. See "The binding
constraint" above.

**Rows.** `drvovl`, `drvmem`, `mirror`, `abilock` (fast); soak `drvcall`,
`drvmove`, `drvclaim`, `hmadrv`, `lzdrv`, `heapdrv`, `vidsound`, `arcsound`,
`midiracksb`, `usbmouse`, `hibernatedrv`, `cpnames`, `extdmod`,
`dockmodule`, and `mega-` twins.

**Dependencies.** WP6's boot-7 provides the blob room for drivers-5.

---

## WP5. kern_dos images and kern_mega's own dead weight

**Goal.** Recover memory for DOS programs, and take kern_mega's free bytes.
Every saving in a kern_dos image is a byte handed to the DOS program on that
machine.

**Findings.**

| id | what | bytes |
|---|---|---|
| `md-6` [K] | **Both kern_mega kern_dos images carry a live resume they can never run.** kd_resume opens with `jmp .no` on KD_MEGA, but it, kd_extents, the strings, the kdr bss and hbs_stub are all assembled. Measured on a re-assembly byte-identical to build/megak/kerndosl.bin: kd_resume 357 + kd_extents 189 + hbs_stub 459 + 23 `.bss`. Keep `kd_resume: stc/ret` and kd_stageseg. The low arm's predicted KD_IMG_KB goes from 37 to 36. | **-1,025 each kern_mega kern_dos image** |
| `md-7` [C] | `kd_glb` (512 bytes) is reserved in every shipped kern_dos. Only KD_GATE uses it (kdentry.inc:1119-1121, kdosgate.inc:18-111). | **-512 `.bss`** in kern_big's and both kern_mega kern_dos images |
| `md-13` [C] | Ten kdback.inc doors are `call X / ret` around targets with the same contract. Name the targets in kdb_betab, or use equ aliases. | -40 every kern_dos image |
| `md-14` [C] | kern_dos's mem_free_x is a no-op behind a compare. Alias it to kd_nothing and fix the comment. | -8 kern_dos |
| `md-8` [C] | KDL_PLOW is written, called binding, and read by nobody (megados.inc:1323-1330). Free the word, or give it to md-15's header sum. | -15 `.mega` |
| `md-19` [K] | Six HIBER.DRV-only scratch cells live in kern_mega's conventional `.text` (map 4C66h..4C80h). They move to MPS_* offsets. **mega_hbwk stays**: it must travel inside the image. | **-22 conv** kern_mega, +~40 HIBER.DRV |
| `mc-16` [K] | **Reopen SPEC 124.31.2's refusal and move the master PIC off 20h.** The reflection path never reinitialises the PIC, so a 50h base deletes mega_i21's per-keystroke ISR probe and the 21h special cases. The correction: mega_ivt_sync and mega_idt_gate also hard-code 20h/22h..28h, so the literals must become symbolic too. Check megados's mega_dropr and hibernate. | about **-70 `.mega` net** |
| `mc-9` [K] | mx_cpmove rebases SLD alone, so BACK (aimed by lz_match) keeps a stale base when another task packs that block mid-match. Rebase the whole K0..SLD range: same instruction lengths, 0 bytes. The rethink (one LDT selector per stream) waits for mc-4's allocator. | 0 |
| `md-21` [U] | The A20 code is written six times with four retry budgets. Use one macro, so the budget is one number (MPK_SUMLINES precedent). | 0..-20 |
| `md-20` [C] `md-22` [C] | KDLF_RAHSH is a shift count named like a flag, so rename it KDL_RAHSH. kdlayout.inc is a 274-line changelog around ~30 definitions: keep the current measurement per arm. | 0 |
| *opt.* `md-15` [U] | Check the park header's 16-bit sum before jumping into it, using md-8's freed word. **This is a KDL_VER 6→7 bump on kern_mega's arm** (see the watch list). | +20 `.mega` +35 HMA kern_dos |
| *opt.* `mc-19` [U] | A "protected-mode clean" class bit, so mega_gpw can close a clean package on a CS store. This is the staged road to ABI-BREAK-LIST L3. | +12 `.mega` |
| `mc-13` [C] `mc-14` [C] | mega_ustab rows are never freed: limit useg to segments at A000h and above, or free rows (+6). MX_FWIN is never cleared, which holds the FSXF_RATE hook off forever after a move (+10). | +16 `.mega` |

**Files.** kerndos/kdresume.inc, kdentry.inc, kdosgate.inc, kdback.inc,
kdshim.inc, kdlayout.inc, kdlaunch.inc, kdpark.inc, kdhma.inc,
kernel/megados.inc, megares.inc, mega.inc, hiber.inc (md-19), apps/dos/dos.asm
(md-20 rename).

**Bytes.** kern_small 0. kern_big kernel 0, and its kern_dos image
**-560..-580**. kern_mega **-22 conv**, about **-85 high** (md-8, mc-16) plus
+16 high for mc-13/14, so **-69 high** (mc-10's counter is WP10's). **Each kern_mega kern_dos image -1,585.**
HIBER.DRV +~40. md-7 alone "buys no whole KB" on kern_big's arm (34,796 of
34,816). Per the banner, that is not an argument: the 512 bytes are real.

**Risk.** Low for the deletions. Medium for mc-16, which reopens a recorded
decision with the corrected list of literals.

**Rows.** `mirror` (fast, once md-5 adds kerndos); soak `kerndos`, `kdos`,
`kdarena`, `kdbigexe`, `kdhand`, `kdcylrun`, `kdpart`, `kdreturn*`,
`kdkbd`, `kdmouse`, `dosbss`, `dosxms`, `megahiber`, `mega-kdkbd`,
`mega-kdmouse`, `megahigh` (full), `mega-int0sweep`; Bochs and 86Box legs for
mc-16 (docs/BOCHS.md: Bochs is a 386, so the 286 reset path needs 86Box).

**Dependencies.** WP10's md-5 (t_mirror scans kerndos/) should land first, so
the mirrored constants these edits touch are watched.

---

## WP6. Boot: let the build choose the constants, and make room

**Goal.** Make three hand-typed constants derived instead of re-derived by
hand, and free the bytes in the blob and boot sector that the bug pass's
canary and geometry fixes need. **No resident byte moves.**

**Findings.**

- `boot-7` [C] **Stage 2's copy of the DPT patch is dead.** Stage 1 patches int
  1Eh before its first multi-sector read, and its own comment calls stage 2's
  copy "idempotent rather than necessary". Nothing else enters boot2_entry
  (boot.asm:300-334, boot2.asm:262-284). kern_big's blob ends at 0x19FC of
  0x1A00, which leaves 4 bytes free. **-41 blob on every kernel.** That is
  the room boot-2 and boot-5 (bug pass) need.
- `boot-6` [K] boothd's RAM check asks the question SPEC 2.7.1 retired, and
  never asks about the blob. Do what stage 1 does: a `cmp ax, MEMNEED`
  computed at assembly time (boothd.asm:121-129). **-9 boothd**. The built
  sector has 7 bytes free, and boot-5's handoff needs 8.
- `boot-4` [K] The SPL_RESIDENT guard measures at spw_resident_end, four
  bytes short of the first-tick stores. Move the label below spl_quiet (0
  bytes). Then **derive SPL_RESIDENT** as (SPL_RES_SIZE+511)/512 and have the
  Makefile read it from the `ks:` line instead of sed-ing splash.inc. It is 9
  sectors against a measured need of 5 on kern_big, so under KZIP that is
  **~-400 bytes of KERNEL.SYS** (73,904/92,082 = 80.3%). First audit every
  first-tick store (vid_setmode, vid_wipe, vid_cga_equip, vid_6845_prog,
  bvs_apply) for an address in sectors 5-8.
- `boot-10` [C] Have one host tool (t_canary's `crosses()`) **compute KSIG_OFF**
  from the shipped geometries and emit `-DKSIG_OFF=`, keeping t_canary as the
  independent second reader. KSIG_OFF has moved half a dozen times by manual
  re-derivation, and boot-1 (bug pass: the canary is inert on 360/720) is what
  one wrong model costs.
- `boot-18` [K] Guard 5 omits BLOB_LIFT on every shipped kernel: ROM_COLD is
  defined unless NO_ROM_COLD (kernel.asm:1121-1123), so it under-counts the
  boot footprint by 1,536 bytes. Use BLOB_SEG_AT. Stage 1 refuses correctly at
  run time, so this is the guard's honesty, not a mis-accept.
- `boot-19` [C] boot.asm's KERNEL_SECTORS=16 and BOOT2_SECS=2 defaults would
  build a wrong sector silently. Replace both with `%error` on the kernel arm.
- `boot-9` [C] `boot-13` [C] The comments on these constants carry their history:
  KSIG_OFF lists eight past values and a wrong band. Each comment becomes the
  invariant plus its guard, and the ledger moves to docs/history. Document the
  floppy handoff's BX:CX residue once, with boot-5's handoff, rather than
  spending blob bytes zeroing them.
- *Optional:* `boot-17` [U] makes kmain_o a blob-resident table. That is
  -30..-60 blob, mostly a readability win; measure first.

**Files.** boot/boot2.asm, boot/boothd.asm, boot/boot.asm,
kernel/splash.inc, kernel/kernel.asm (guards 5 and 1's companions, footer),
Makefile (SPL_RESIDENT, KSIGDEF2), tests/unit/t_canary.py, a new
tools/os88ksig.py, docs/history/.

**Bytes.** 0 resident on every kernel. Blob -41, boothd -9, KERNEL.SYS about
-400 on disk on kern_big (KZIP), boot read one sector shorter. Optional blob
-30..-60.

**Risk.** Low. boot-4's audit of first-tick stores is the one step that must
not be skipped.

**Rows.** `canary`, `image`, `diskverify` (fast); `bootsmoke` (full); soak
`bootfloor`, `bootfloor-ab`, `smallboot`, `splashbar`, `splashspin`,
`hdtakeboot`, `pravetzboot`, `v20boot`, and all four geometries via
`blobruns`.

**Dependencies.** None. **It unblocks the bug pass's boot-1, boot-2 and
boot-5/dr-1**, so land it before that pass's boot work.

---

## WP7. .bss arrives zeroed: one rule, a gate and the dead stores

**Goal.** Settle a contradiction that has cost a defect (fsx_cur) and a pile of
defensive stores.

- *Says zeroed:* events.inc:61-65, blank.inc:120-122, vga12.inc:304-306,
  SPEC 12.8.5.1 (measured 0x00 on a live machine), and kmain_o, which
  *relies* on it (kernel.asm:5894-5900).
- *Says not:* sched.inc:341-363, SPEC 8, cpudet.inc:29, bootprof.inc:43 and
  clock.inc.

**Findings.**

- `boot-8` [C] The fact: in shipped kern_big the file bytes between
  kernel_text_end (0xB788) and `.ovl` (0xCE00) are all zero, and `.bss` maps
  entirely into that gap. Every loader writes it: stage 2, boothd and
  kz_expand. Add one `%if`: the first file section after `.text` starts at or
  past BOOT2_PAD + KTEXT_SIZE + KBSS_SIZE.
- `histsys-16` [C] Adopt the zeroed rule, and extend t_bsssentinel to assert that
  the `.bss` span is all-zero in the image on **all four** budgeted kernels.
  Then drop the resident zero-stores (~-20 resident, unmeasured).
- `sched-12` [C] sched_init's stores (-30 `.ovl`). **The scheduler already
  depends on the rule**: nothing stores `[sch_chbusy]` before sch_isr's first
  test of it. So either keep the stores and fix their comments, or drop them
  and give the overlay its room. sched_init's `.ovl` is 5,119 of a 5,120-byte
  bound (kernel.asm:8942), so this is the room WP18's sched-7 seed needs.
- `hiber-17` [C] hb_probe_x zeroes three words that arrive zeroed (-9 `.boot2`).
  `sysmisc-10` [C] clk_probe does the same (-15 `.ovl`). Their comments
  distinguish kernel `.bss` (zeroed) from a module image's data (not).

**Files.** kernel/kernel.asm, sched.inc, hiber.inc, clock.inc, clockw.inc,
ctrl.inc, events.inc, cpudet.inc, bootprof.inc, mod.inc, splash.inc,
tests/unit/t_bsssentinel.py, SPEC.md 2.5/2.6/8.

**Bytes.** kern_small **0..-20**, kern_big **0..-20**, kern_mega **0..-20
conv**; `.ovl` -45, `.boot2` -9. The `.ovl` room is what matters.

**Risk.** Low, once the gate holds on all four kernels. A kernel built without
KZIP, or a future loader that does not write the gap, is what the gate
exists to refuse.

**Rows.** `bsssentinel` (soak; consider promoting it, since it is a rule about
an artefact), `kernbudget` (fast); `bootsmoke`, `small128`, `kernresident`
(full); soak `megahigh`.

**Dependencies.** None. WP18 depends on it.

---

## WP8. Fault containment: INT 0 and INT 6, and the window-pointer divide

**Goal.** Today the real-mode kernels install no divide or invalid-opcode
handler. **On an IBM 5150/5160 ROM, one divide overflow is a dead machine**:
the vector is the BIOS stub that masks the 8259 (docs/TESTING.md:187,
tests/int0sweep.py). On a 286+ running kern_big, it re-executes the DIV
forever, a hang that reads as "froze". GLaBIOS alone continues with garbage.
SPEC records real field defects this would have turned into a report: SKIES'
ADI walked over its own code after an int 0 (SPEC.md ~165883), and a ZMODEM
size. CLAUDE.md's bug classes demand a report, not a dead box.

**Findings.**

- `histsys-2` [K] and `isr-8` [C] are the same finding from two angles.
  Generalise sch_diepanel into a system-error panel that takes a message
  pointer and the faulting CS:IP (sched.inc:2601-2720). Ship two ~4-byte
  stubs: int 0 always, and int 6 when cpu_tier ≥ 286. **Never hook int 5,
  which is PrtSc on a PC.** Install them from `.ovl` at sched_init. DOS.O88
  banks IVT 00h, and a kernel booted by int 19h reinstalls them. A later step
  kills only the instance when CS is a package segment on a worker
  (kern_mega's mega_fkill shape).
- `wm-1` [K] wm_ptr2idx is `sub ax,wm_wins / div cl`. The quotient overflows
  for any BX below wm_wins or at or above 256 × WIN_SIZE past it: 18,944 on
  kern_big, 16,640 on kern_small (corrected from 255×). OSAPI_WM_DESTROY
  reaches it twice unvalidated (wm.inc:7278-7301, instance.inc:1975-1979,
  wm.inc:8280-8292).
- `wm-2` [K] **Store the slot index in the record, so the divide disappears.**
  Add W_IDX at the record's end, outside wm_create's zero run, written once by
  wm_init in the boot overlay. wm_ptr2idx becomes a bounded 11-byte load
  instead of a 19-byte body. That is ~-80 clocks a call on every damage
  pass's wm_dmg_bit, and a wild BX reads one byte instead of faulting.
  WIN_SIZE goes 74→75 / 65→66 (unpublished; os88geom.py's mirror moves).
- *Optional:* sched-9 (record): the death panel trusts the table it calls
  suspect (+4).

**Files.** kernel/sched.inc (panel), kernel.asm (install in sched_init's
`.ovl`), wm.inc, instance.inc (inst_wchk carries the same off-by-one),
tools/os88geom.py, SPEC.md §8 and §11, docs/TESTING.md (int0sweep's
meaning changes from "detector" to "the panel appears").

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| INT 0/6 stubs + panel generalisation | +30 | +30 | 0..+30 (the PM IDT already handles faults; only the pre-switch window) |
| wm-2 (-8 `.text`, +12/+6 `.bss`) | -2 | +4 | +4 |
| `.ovl` | +12 | +12 | +12 |
| **total** | **+28** | **+34** | **+4..+34 conv** |

**Value per byte.** It is the clearest growth in the phase: ~30 bytes turn
"dead machine" into a named report on exactly the machines the project
targets, and wm-2 pays for itself in the hottest wm loop.

**Risk.** Low. The panel must not trust the scheduler tables (sched-9's
point). DOS.O88's vector exchange must still round-trip.

**Rows.** soak `int0sweep`, `mega-int0sweep`, `stkpanel`, `mega-stkpanel`,
`dispclose`, `dispclose-small`, `zonedmg`, `dmgcull`, `kdos` (the IVT 00h
exchange); `mirror` (fast, WIN_SIZE); a new row: a package that divides by
zero gets the panel on a 5150 `_gla` profile and on 86Box's 286.

**Dependencies.** WP7: the stubs install from `.ovl`, which has 4 bytes
free on kern_big until WP7 frees 45. See "The binding constraint" above.

---

## WP9. A runtime API level

**Goal.** Close the gap SPEC 20.2.0.1 names under "What it does not cover"
and gives no remedy for. A package built against cells newer than the running
kernel far-calls past the table's end. Apps disks and system disks are
separate floppies, so **a newer apps disk in an older machine is the ordinary
case**, and only The Wire's own cell fence guards it (tools/os88wire.py:132-138,
WIRE_CELL_LEVELS).

**Findings.**

- `abi-6` [C] The table's end already works as a minor version, because the
  table has only grown since format 10.
  - Put `dw osapi_table_end` at 0060:0006 (padding today,
    kernel.asm:2346-2360), at 0 resident bytes.
  - Add an SDK macro, `OS88_NEED cell`, for an entry proc: `cmp word
    [es:6], cell+1 / jb refuse`. An older kernel reads 0 there and refuses.
  - os88pkg.py warns when a package calls past the last released level
    without it.
- `loader-6` [K] Add a load-time refusal. Header byte 31 (the name field's
  16th byte, which os88pkg.py already keeps 0 by refusing 16-character names)
  carries an API LEVEL, numbered from The Wire's cell levels. ld_check_hdr
  compares it against the kernel's level and answers LD_EVER ("Wrong os8088
  version"). Old packages carry 0 and pass, so **PKG_FMT does not move**.
  Doing it now protects every kernel from this point on. Doing it later
  protects fewer.

**Files.** kernel/kernel.asm (0060:0006), loader.inc (ld_hdr_ok),
apps/os88api.inc (header byte 31, OS88_NEED), tools/os88pkg.py,
tools/os88wire.py (share its level numbering), SPEC.md 20.2.0.1.

**Bytes.** +8 `.cold` on every kernel (kern_mega: high), 0 for the table-end
word, ~10 bytes per opting package. **os88api.inc moves: abilock --update, no
bump** (see the caveats in the watch list).

**Risk.** Low. The two caveats are a historical kernel that held a different
word at 0060:0006, and any other reader of byte 31.

**Rows.** `api-abi`, `abilock`, `pkg`, `installabi` (fast); soak
`abicompat`, `thewire`, `wire`; a new host case in t_pkg: a package whose
level exceeds the kernel's is refused with LD_EVER.

**Dependencies.** None. WP2's package purgeable claims, WP15's text-16 and
WP27's new cells should all use OS88_NEED, so land this first.

---

## WP10. Gates that catch wrong facts, and suite hygiene

**Goal.** Host-only checks that fail `make` on a class of error the tree is
already making. 0 shipped bytes, and each stops something live.

**Findings: documentation gates.**

- `engsys-2` [C] **A citation that resolves to the wrong section passes today.**
  Six SPEC.md lines cite §46 (ArtfulType) meaning §47 (greying), including
  hard rule 8, which binds packages (SPEC.md:161, 26453, 26463, 26555, 55757,
  76640). Fix the six. Add a heading-drift check: compare SPEC.md's
  {number: title} map at the merge-base against HEAD, and list every
  citation of a number whose title changed.
- `engsys-3` [C] SPEC.md's hand copies of code are ungated and wrong. §3 has
  KERNEL_SEG 0x0800 (it is 0x0060), KERN_BUDGET 74240, STK0_SIZE 1024 (it is
  512). §15.1 reproduces guard 1 without KERN_GRACE. §62.9.1 gives DSV_SIZE
  as 26/28 (it is 36). Of 182 literal `NAME equ N` lines in SPEC fences, 4
  disagree. Have t_mirror read SPEC's fenced nasm blocks, or generate §3 and
  §15.1 between markers.
- `engsys-8` [K] 29 basenames are shared, and checkdocs' stems dict keeps the
  last path (checkdocs.py:204). Resolve in the citing file's own directory
  first, and fail an ambiguous qualifier.
- `engsys-9` [C] checkdocs does not read .c, .h, .txt or .sh, which leaves 1,579
  citations in .c, 138 in .h and 205 in .txt unchecked. One is known broken
  (pmc_menu.c:121 cites a subsection 14 of SPEC.md 91, which has no subsections). apps/c64 writes 32 bare § meaning C64-SPEC.
- `engsys-6` [C] Running counts rot: the table ledger, "the fifteen", the tier
  counts in TESTING.md, PERFORMANCE.md's line count. Generalise SITES/SITES_HIST
  into a COUNTS table fed by suite.rows() and kernel.map. A prose count either
  carries its moment or is not written.
- `isr-12` [K] `gfx-10` [K] CLAUDE.md's short form "pushf/cli … popf, never
  cli/sti" drops SPEC §1 rule 3's scope ("in anything an ISR can call"), so
  correct sites (gfx_lock, task_exit, the idle loop) read as violations.
  Restore the scope in CLAUDE.md and AGENTS.md. Optionally register the
  sanctioned bare `sti` sites in a tests/stisites.txt ratchet, for the 27
  bare sti in kernel/ that engsys-13 counts.

**Findings: ABI and mirror gates.**

- `abi-3` [C] t_api_abi decodes only kern_big's table. Thirteen `%if` arms
  produce kern_small, kern_emu and kern_mega shapes that are never decoded,
  and the FIXED ledger has no rows for kern_small's two XREFUSE cells, so the
  gate would fail there today (t_api_abi.py:458, 164-168). Add a `--kernel`
  mode and a cross-kernel comparison of cell offsets (full tier).
- `abi-4` [K] dwsf reads the caller's DS at a hand-counted `[bp+10]`
  (diskw.inc:3397-3414). Define API_RN_DSOFF beside api_rn and name this
  reader in ABI-BREAK-LIST T1's row.
- `engsys-11` [C] The C SDK's os88.h (80 numeric defines, 57 pairing with SDK
  names) is outside t_mirror and outside the lock. Add it with a prefix map,
  and add os88.h and os88thunk.asm to PKG_FMT's fingerprint files.
- `drivers-16` [C] The private ABIs (VX/XM/HD) are versioned by hand. Fingerprint
  vxabi.inc and the XMEM and HDD blocks against VX_ABI_VER, XM_ABI_VER and
  HD_ABI_VER, and move XMEM's mirrored pair to one shared include.
- `md-5` [C] t_mirror scans no kerndos/ file, yet kdlayout.inc and hiber.inc name
  it as their guard. The constants that cross between artefacts on different
  disks are exactly the unwatched ones. Add kerndos/*.inc and kdos.asm with
  DIVERGENT rows, and rename KDP_GDT to MPK_GDT.

**Findings: assembly-time asserts.** All are 0 bytes unless noted.

- `ui-4` [C]: ui_cmd's jump table gets a length `%error` against CMD_LAST
  (ui.inc:3023-3045).
- `shell-6` [C]: `MENU_FILE_REM equ menu_items_file+2`, with an `%if
  MENU_FILE_N != 2`, used at all five sites.
- `text-6` [C]: hoist the fpg_warm adjacency `%if` out of FPG_PARK, and add
  `%if fpg_px - fpg_pat != 1`.
- `md-12` [C]: an `%if` on mega_dreg_add's worst case.
- `gfx-7` [C]: in vid_apply, 4 planes implies stride == ROW_BYTES, else refuse
  the mode (+8 `.ovl`). The planar bodies hard-code ROW_BYTES at 13 sites.
- `mc-10` [C]: count kernel-CS fixups in [mega_kfix] and add a Bochs row
  asserting 0 (+8 `.mega`).

**Findings: structure gates.**

- `engsys-12` [C]: a host check that each kernel .inc's last section directive is
  the section active at its `%include`. NASM refuses only an open nobits
  section; a progbits section left open is silent.
- `engsys-13` [U]: a shared tests/unit/ratchet.py (parse, require a reason,
  compare). The 21 registries are exact-match today; it adds a `warn:` when a
  registry total rises against the merge-base.
- `engsys-14` [C]: gates read derived facts off the `ks:`/`ksm:` lines or
  kernel.map instead of regexing source (t_kernbudget.py:72-98).

**Findings: suite hygiene.**

- `engsys-7` [K]: fast has grown to 76 rows and 56.85 s declared, against a
  30 s CPU budget. About 17 one-package rows plus chromesites carry no owner
  exemption. Add `Row(family=…)` or `owner='<word, date>'`, have t_registry
  fail a fast/full row with neither, and let the owner move or exempt each.
- `engsys-15` [C]: `-k '*'` bypasses the whole-soak refusal
  (os88test.py:1318, os88soak.py:1050). Refuse any selection covering more
  than ~90% of the tier without `--user-asked`. That rule is the owner's.
- `engsys-16` [C]: `builds=` is judged per script, not per mode
  (brcapacity-fixtures).

**Files.** tools/checkdocs.py, tools/abilock.py, tools/os88test.py,
tools/os88soak.py, tests/unit/t_mirror.py, t_api_abi.py, t_registry.py,
t_kernbudget.py, a new tests/unit/t_sections.py, t_ratchet support,
tests/suite.py, SPEC.md (six cites, §3, §15.1, §62.9.1), CLAUDE.md and
AGENTS.md, kernel/ui.inc, desk.inc, desksc.inc, fprog.inc, megados.inc,
vidsel/viddet (gfx-7), kerndos/kdpark.inc, mega.inc (kfix).

**Bytes.** 0 resident on every kernel. `.ovl` +8 (gfx-7), `.mega` +8 (mc-10).

**Risk.** Low. Each new gate is red-on-purpose testable, per WRITING-TESTS
§1: break the thing and watch it fail.

**Rows.** These *are* rows. `checkdocs`, `docindex`, `mirror`, `api-abi`,
`abilock`, `registry`, `asmrules`, `kernbudget`, `canary` (fast); new
`sections`, plus api-abi's per-kernel mode in full.

**Dependencies.** None. Several later WPs lean on it (WP3, WP5, WP24).

---

## WP11. The KHP shelf: take or delete every measured knob

**Goal.** No indefinite shelf. LAST-DROP-PERF.md 7.1 measured these knobs on
the 4.77 MHz XT. kern_mega, the 286/386 build where those milliseconds matter
least, carries rows 1-11 (7.3). kern_big, the XT's build, carries none of
them except KHP_LZ. Meanwhile wm.inc, softgfx.inc and vga12.inc carry both
arms of each, and the shelf already produces false docs (softgfx.inc:445,
SPEC 5.7 items 9-10). t_khpknobs proves the arms assemble, not that they draw.

**Findings.** `sysmisc-9` [K] (row 2), `wm-11` [C] (rows 2, 4, 5, 7), `gfx-6` [C]
(rows 3, 6, 8, 9, 11, 12, 14, 16).

| row | knob | bytes (measured `[+N]`) | what it buys on the XT (measured unless P) | recommendation |
|---|---|---:|---|---|
| 2 | KHP_RELIT | +32 | fullscreen exit and saver wake: CGA 781→629 ms, Herc 984→727, VGA 1,203→706 | **take** (sysmisc-9: "cheap for what it buys") |
| 4 | KHP_DMGSTALE | +38 | a drag over a hidden window -12..-32% | **take** |
| 5 | KHP_CAPTION | +34 | -3.5..-8.5 ms a CGA raise | **take** |
| 3 | KHP_VGASAVE | +122 `.vga` | VGA raise save 133→99 ms; `cur_move` 21,450→15,100 cycles (which also shortens WP13's isr-7 window) | **take** (gfx-6 (a): `.vga` bytes are slid away on 1bpp machines) |
| 6 | KHP_VGAFILL | +39 `.vga` | GFX_FILL 256×128 13.37→10.67 ms | **take** |
| 8 | KHP_VGAEDGE | +21 `.vga` | GFX_VLINE 128 -21%, GFX_FILL 64×64 -13% (one-row lines +1.9%) | **take** |
| 7 | KHP_RAISEOL | +90 | CGA raise -5..-14 ms | decide: measured, but the largest of the cheap set |
| 9 | KHP_VGABLITP | +32 `.vga` | Paint canvas ~-53 ms **P** | measure once in the final pass, then decide |
| 11 | KHP_FRAME | +26 | ~-0.27 ms a raise | delete the arm (small for its bytes) unless the owner wants it |
| 12 | KHP_MONORECT | +344 (+16 mono-5) | GFX_FILL 64×64 **-51%** on CGA/Herc; CGA raise -9 ms; menu -5 ms; XOR drag 49→40 ms | **owner's call**: the biggest win on the calibration machine (the 5150's CGA), and the biggest growth. `.text`, so it is also resident on VGA. |
| 14 | KHP_MONOSCROLL | +56 | GFX_SCROLL 256×128 -26%, mostly overlapping files-2/3 (~-4 ms left) | delete unless MONORECT is taken |
| 16 | KHP_MONOPAT | +22 | GFX_FILL_PAT 64×64 -28% | take with MONORECT, else delete |

Rows 10, 13, 15, 17 and 18 were not raised by these reviews and keep their
LAST-DROP-PERF status. 15 (ICONWORD) waits on LOG O5, and 18 (LAZYEND) on O4.

**What to do.** Turn each taken knob's arm unconditional on KERN_BIG, delete
its other arm, and delete the arm of each refused row. **Date the decisions in
LAST-DROP-PERF 7.1.**

**Files.** kernel/kernel.asm:243-297 (the KHP block), wm.inc (wm_dmg_stale,
wm_raise, wm_draw_win, wm_cov_rect arms), softgfx.inc:442-680, vga12.inc's
knob arms, blank.inc:540-582, fsx.inc:500, docs/plans/LAST-DROP-PERF.md,
SPEC 5.7.

**Bytes.** kern_small 0 (kern_big-only knobs). kern_big: the recommended
"take" set is **+104 on every machine, +182 `.vga`, so +286 on the VGA
gauge**. With RAISEOL +90, MONORECT/MONOPAT +382 and VGABLITP +32 as well, it
would be up to +790 on the gauge. kern_mega **0**, if the arm kept is the one
mega already assembles.

**Risk.** Low. The arms exist and were measured on the target. RELIT changes
transients (fewer).

**Rows.** `khpknobs`, `knobself` (full); soak `buildmatrix`, `glyphcost`,
`dispsaver`, `dispfsx`, `dispfsxcga`, `dispfsxherc`, `zonedmg`, `dmgcull`,
`dispzoom`, `xorrect`, `vgadrop`, `vgadirty`, `kernresident`.

**Dependencies.** None. The A/B leg against the starting commit runs in the
final pass, per the owner's rule.

---

## WP12. FAT write path: allocation and flush

**Goal.** Every replace, delete, rmdir and truncate costs two extra int 13h
calls. Free space is recomputed by walking the whole FAT. Allocation
fragments one cluster at a time. On the XT, each of these costs **seconds**.
The rule "cost disk work in calls, not sectors" is where this WP earns its
bytes.

**Findings.**

- `dw-15` [C] **Defer the post-commit FAT flush (frees only) to gfx_unlock**,
  the gate held streams already commit at (diskw.inc:3366-3372, 6798-6801).
  - Today dskw_relold flushes FAT1 and FAT2 after every operation: about
    0.8 s each on the XT, and 100 calls for a 50-file rmtree
    (diskw.inc:962-974).
  - A free is always safe to lose: the result is lost clusters, which are
    unreachable on disk. The one danger is a floppy swap, and gfx_unlock
    covers "every way the user can reach the drive".
  - kern_small keeps the immediate flush.
- `histsys-3` [C] **Cache the free-cluster count.** dsk_free_clus_x walks the
  whole FAT on every DFREE, VOL_STAT and mount. That is ~105 ms CPU on a 20MB
  disk, plus FAT window re-reads on FAT16 (disk.inc:6337-6470). FTPD once
  spent 44% of an upload here. Set [dsk_nfree] = FFFFh at mount, let the first
  walk fill it, and adjust it in dskw_setfat (the single FAT writer,
  diskw.inc:4845) on a zero/non-zero transition. Keep it across remounts of a
  fixed disk. Drop it on refat and on redirected volumes (verification note).
- `dw-14` [C] **Allocate a contiguous run when N is known** (whole-file write,
  non-held grow). Today it is first-fit, one cluster per call
  (diskw.inc:4686-4790), so "a full disk fragments exactly the way DOS's
  did", and each fragment is a transfer plus a seek of ~100 ms. Search the
  window once for the lowest free run of N at or after the rover, with a
  repne scasw variant on FAT16 and fsk_findrun's idea (fsck.inc:2800-2856),
  then fall back to first-fit. A FAT16 run that crosses windows needs its own
  handling.
- `dw-13` [K] Refuse FERR_FULL **before** the first data sector: compare
  ceil(rem/spc) with a goal-bounded free count. Today it is discovered after
  writing everything that fits. **It comes free with dw-14's run search**, and
  costs +18..25 alone. On FAT16 the count reads FAT windows, so the goal bound
  is what keeps it cheap.
- `dw-16` [C] Reset the rover only on the non-keepcwd mount arm: every chdir
  re-walks the used front of the FAT (disk.inc:4130-4140), ~0.8 s on a nearly
  full 1.44MB disk after each navigation. Optionally add a FAT12 whole-window
  scan (+40).
- `dw-17` [C] rmtree finds every file twice. Enter the delete past its dskw_find
  when dskw_raw is already staged (0 bytes).
- `dw-20` [C] Shared pop runs and refusal tails span 1,000+ lines, held together
  by prose. A DSKW_PUSH6/POP6 macro pair defines the order once (0 bytes). Do
  it first, because the edits above touch those runs.

**Files.** kernel/diskw.inc, disk.inc, fsck.inc (shared run idea),
SPEC.md 18.4/18.4.5/18.4.10.

**Bytes.** kern_small **0..+3** (dw-16's rover; the rest is kern_big's).
kern_big **+105..+108** `.cold` resident: histsys-3 +20, dw-15 +15, dw-14
+70, dw-13 0 with dw-14, dw-16 0..3. Add +40 for the optional FAT12 scan.
kern_mega the same, all high.

**Risk.** Medium-high: this is the data-integrity path. dw-15's deferred dirt
is exactly what dsk_fatw_park carries across a swap, so **the bug pass's dw-5
fix ("a failed flush keeps its dirt") must land first**, and the gfx_unlock
flush must land before any path that can park. dw-2 (the FAT16 leak on a
failed write) interacts with dw-13 and dw-14.

**Rows.** `cswrite` (fast); `sysfree` (full); soak `wseq`, `wseqheld`,
`wsequnclosed`, `wseqinter`, `wseqdeleted`, `wseqcut`, `wseqioerr`,
`wseqfull`, `dtwrite`, `fsckdisk`, `fsckhd`, `bigvol`, `fatwpin`, `ftpd`,
`fcpcopy`, `diskclone`, `mega-wseqfull`, `mega-dtwrite`. **New rows:** a
50-file rmtree's int 13h count before and after (a counter, not a timer), and
a swap between a delete and gfx_unlock.

**Dependencies.** The bug pass's dw-5 (first) and dw-2.

---

## WP13. An interrupt-latency budget and its gate

**Goal.** No IF=0 bound is written anywhere (isr-14). The windows that exist
argue against 8.3 ms, 7.5 ms or nothing, and copies grew past 50 ms
unremarked. That is CLAUDE.md's third defect class, input overrun, with no
number to hold anyone to.

**Findings.**

- `isr-14` [C] The rule: SPEC §1 states that IF=0 lasts at most ~500 µs and
  never surrounds a caller-sized copy; copies hold sch_lock at IF=1. **The
  gate:** tests/unit/t_ifwin.py (fast) flags rep/loop/mem_b* copies inside
  pushf/cli…popf regions, with a tests/ifwin.txt reasons file that only
  shrinks. Its first catches will be the bug pass's mem-2/hm-1 (mem_regrow
  path 3's whole copy at IF=0).
- `isr-10` [C] `mem-11` [C] Correct the texts.
  - SPEC 28's "longest IF=0 window" (sys_snapshot, 1.8-2.2 ms) is wrong:
    regrow copies, the driver-image move (~17-50 ms), cur_move in mou_isr
    (2.4-3.2 ms) and task_spawn's fill are all longer.
  - The driver-image move's note is wrong both ways. 50 ms is under one tick,
    but it is ~6-7 bytes into a 1200-baud 8250's one-byte buffer
    (memory.inc:3080-3088).
- `sched-1` [C] task_spawn holds IF=0 across the whole 0xCC slice fill, ~676 µs
  for a 384 slice (sched.inc:914-989). Reserve the slot (T_STATE=3) under the
  cli, fill at the caller's IF, then publish (+8 `.text`). First check that
  nothing shows a package T_STATE 3 (the Task Manager's snapshot).
- `hm-5` [C] `mem-8` [C] mem_avail_lvl_p's O(n²) planning walk, and
  osapi_mem_compact_x's what-if, run inside cli: ~8-15 ms by arithmetic on a
  busy table (memory.inc:4927-5023, 5753-5772). Packages size themselves with
  them and the Task Manager polls them. **Measure first** on a 20-claim heap;
  if over ~5 ms, use inc/dec [sch_lock] instead (0..+6 `.cold`). WP17's
  mem-14 would remove the n² entirely.
- `boot-11` [C] sch_isr draws a 28.7 ms spinner frame at IF=0 every ~220 ms
  through kmain_o, blocking IRQ6 and IRQ4. Have the ISR set a "frame due"
  byte and draw on the main line from spl_step (-10 `.text`, +6 blob).
  Measure with os88boot.py first.
- `input-5` [C] Shift+PrtSc runs the ROM's Print Screen inside kbm_isr's IRQ mask:
  seconds with no printer, with the clock, scheduler, serial mouse and floppy
  masked. Set the print-screen status byte at 0050:0000 to 1 in mouse_init
  (DOS uses the same byte), and again after a hibernate resume (+5 `.ovl`).
- `isr-1` [K] An XT's Pause stops the tick count, and the BIOS time of day
  falls behind by the pause. SPEC 9.8.3 chose that. Document the clock loss,
  or keep IRQ0 open (mask 0xFC) when Ctrl+NumLock is the scan (+14).
- `isr-7` [K] **Measure first**: the cursor is drawn at IF=0 inside the
  serial ISR. cur_move is 11,500 cycles on CGA and 15,100 on VGA
  (PERFORMANCE.md:1356), against the serial lease's 9600-baud default (1.04
  ms a byte, and an 8250 overruns in 1-2 byte-times). Run tests/serlease.py
  at divisor 12 with the mouse moving and count LSR overrun bits. Then either
  default the lease to 2400 on an 8250 and document the ceiling, or take the
  rethink: EOI, sch_lock, sti, draw (+30..40). WP11's VGASAVE already cuts
  the VGA case.
- `mc-4` [C] kern_mega's LDT walks run at IF=0, once per block on a teardown
  (~5 ms each on an 8 MHz 286 with ~1,500 entries). Keep a high-water mark,
  and move mx_dfree out of mx_free's and mx_relall's cli (+20..30 `.mega`).

**Files.** SPEC.md §1, §8, §28, §66.3; PERFORMANCE.md; tests/unit/t_ifwin.py,
tests/ifwin.txt (new); kernel/sched.inc, memory.inc, splash.inc,
boot/boot2.asm, mouse.inc (input-5, isr-1), kernel/serial.inc or SPEC 115
(isr-7), mega.inc (mc-4).

**Bytes.** kern_small and kern_big **-2..+4** resident (sched-1 +8, hm-5
0..+6, boot-11 -10), plus `.ovl` +5 and blob +6. isr-1's +14 and isr-7's
+30..40 are optional. kern_mega the same, plus **+20..30 `.mega`**.

**Risk.** Low for the rule and gate. Low-medium for sched-1 and boot-11.

**Rows.** New `ifwin` (fast); soak `serlease`, `kbdburst`, `kbdpreempt`,
`kbd4f`, `splashspin`, `splashbar`, `tankspawn`, `heapmap`, `megahigh`,
`mega-kbdburst`. The isr-7 overrun count is a number that **decides** the
lease default, so the agent choosing takes it once.

**Dependencies.** None for the rule. It will flag the bug pass's mem-2/hm-1
until that pass lands.

---

## WP14. Machine-readable routine contracts (the liveness gate)

**Goal.** Build the instrument that unlocks the largest systematic size tax
left. Ship 0 bytes now.

**Findings.**

- `engsys-19` [C] Hard rule 3 (register discipline) has no gate: the stack walker
  checks balance, not clobbers. The kernel already has ~1,480 `; out:` and
  ~900 `; clobbers:` header lines that nothing parses, and they drift
  (wm_content's header says W_X+1, but the routine returns W_X for a
  flush-left window, wm.inc:5697 vs 5705-5707). Fix a header grammar
  (in/out/clobbers/flags/lock). Generate an autodoc-style routine reference.
  Add a conservative **report-only** checker that flags a register written
  but neither listed nor saved and restored.
- The payoff it unlocks (record-phase bytes-5 [U], Appendix C): resident code
  holds **4,791 bytes of push/pop** (.text 2,438, .cold 2,203, .vga 150),
  about 6% of resident code. AX is pushed 514 times. Size passes 16, 17 and 18
  found "callee-saves no caller reads" to be the bulk of their bytes. A
  kernel-internal default of "AX and flags are scratch unless the header says
  preserves" would stop the class at its source. It is safe **only behind a
  liveness gate** (every caller checked against its callee's header), and
  this WP is that gate's first half. It is unpriced: "plausibly a few hundred
  over time".

**Files.** tools/ (a new os88contract.py, next to stkbalance.py/stkdepth.py),
docs/ (a generated routine reference, indexed by os88index.py), header fixes
where the checker reports.

**Bytes.** 0 on every kernel.

**Risk.** Low. Report-only first, a ratchet second.

**Rows.** `stkbalance`, `stkwalker` (fast neighbours). New: a report-only
`contracts` row in soak, promoted to a fast ratchet once it is clean.

**Dependencies.** None. WP19's `@ctx` tags reuse the same grammar.

---

## WP15. SDK-side savings in packages

**Goal.** Bytes saved in every running package, not in the kernel, plus three
small SDK additions that retire a standing rule violation.

**Findings.**

- `hm-11` [K] **The parts loader stays resident in every non-loader parts
  package, once per instance.** 21 packages carry the body (os88partsbody.inc).
  The loader-shaped ones (Word, Scribe, Skies, DOS) discard it by re-homing;
  the others keep op_load, op_size, op_claim, op_read, op_unpack and op_xload
  for life. Measured symbol spans: browser ~1.2KB, pixel ~1.36KB, video
  ~1.1KB, dosp ~0.77KB. Add an `OP_INITDROP` option: assemble the load-time
  half at the image tail, put the standard's bss below it, and after op_load
  cut the region through OSAPI_PKG_REHOME's KEEP form. That form keeps I_SIZE
  true; a bare MEMP_REGROW would leave loader.inc:1251-1256's bound stale.
  **-0.8..-1.5KB per running instance.**
- `hm-7` [C] Same file: a lazy part is re-fetched by 8.3 name and checked only
  for length (os88partsbody.inc:789-794, 829-831). After a swap, a Wire, FTPD
  or a host copy, a different build is far-called. Bank size and first
  cluster at op_load and compare them at op_fetch, with a "Disk changed"
  toast (+20 per parts image). It is also a bug-pass candidate, so land it
  once.
- `mem-17` [K] **Region movability as a header bit.** 69 OS88_REGION_MOVABLE
  lines in 67 files cost 10-19 bytes each plus a far call. The loader reads a
  class bit and sets MC_RLOC to a "kernel half only" sentinel, which
  mem_reloc_call already runs unconditionally (memory.inc:3869-3871). +8
  `.cold` in the kernel, -10..-19 per package, ~-1KB of disk tree-wide.
  Old kernels run new packages pinned, which is the safe direction. Point
  t_movable at the bit.
- `hm-12` [C] (a) Raise MEM_OWNER_MAX to 10 on kern_big. It is 0 bytes: the table
  stays 32 records, and SPEC records Sheet at 8 of 8 six times. (b) Lift
  WIN16's arena allocator into a shared apps/os88lmem.inc, the system's
  second-level allocator. Sheet, DOS, WIN16 and WEAVE each hand-roll one
  today.
- `text-15` [C] **Kernel-synthesised bold in font_run.** Overstrike is the only
  structural reason left for a text pixel to be written twice (SPEC 6.6.2
  case 6: Word, Texpad, Frotz ×2, cword). STIKMENU.DRV already does
  `row | row>>1` (stikmenu.inc:1387-1400). Add a one-shot run-style cell and
  a bold row loop on kern_big (+35..45 `.text`), and the case-6 registry rows
  retire.
- `text-16` [K] Real MENU_SEP and MENU_CHK item prefixes. Separators today
  are hand-counted dash strings that go short when a wider item arrives, and
  MENU_DIS doubles as a radio mark that reads as "disabled". Net -5 on
  kern_big (a MENU_SEP string is 2 bytes against 19), plus package strings.
  **An older kernel draws MENU_SEP as a selectable blank**, so pair it with
  WP9's OS88_NEED.
- `wm-16` [K] Make WF_OWNBG the documented expectation: the default content
  fill under W_PAINT is the canonical erase-then-draw. Add a ratchet list of
  non-callers (about 29 of ~70 package directories call it today). 0 kernel
  bytes.

**Files.** apps/os88partsbody.inc, apps/os88parts.inc, apps/os88api.inc
(mem-17 bit, text-15 cell, text-16 prefixes), kernel/loader.inc,
memory.inc, font.inc, menu.inc, tests/unit/t_movable.py, tests/textsites.txt,
docs/INDEX.md and the porting skills, a new apps/os88lmem.inc.

**Bytes.** kern_small **+8** (mem-17). kern_big **+38..+48** (mem-17 +8,
text-15 +35..45, text-16 -5). kern_mega +35..45 conv, +8 high. Packages:
**-0.8..-1.5KB per running parts-package instance**, -10..-19 per image for
mem-17, +20 per parts image for hm-7.

**Risk.** Medium. hm-11 changes a shared include that 21 packages build, so
every one must be rebuilt and booted in the final pass. The SDK moves (watch
list) need one update.

**Rows.** `movable`, `textrules`, `pkg`, `abilock` (fast); soak `wdparts`,
`pxparts`, `mega-wdparts`, `rehome`, `rehomemove`, `rehomeabort`,
`megarehome`, `sheetmsep`, the parts packages' own rows (browser, pixel,
video, dos), `assocopen`.

**Dependencies.** WP9 (OS88_NEED for text-15/16 callers).

---

## WP16. Read path, directory and listing

**Goal.** Linear directory enumeration, a read-ahead that fails fast, and
folders that grow.

**Findings.**

- `dr-5` [C] **OSAPI_FILE_FIND stays quadratic** although dsk_dscan already
  resumes from a raw slot. Today N entries cost N(N+1)/2 visits
  (disk.inc:7886-8038), ~2.5 guest seconds in DISK-CPU-PLAN 3.1's figure.
  Append a FILE_FIND_SEQ cell with READ_SEQ's caller-held 16-byte cursor
  (folder cluster, raw slot, [dsk_mgen]). Because it is caller-held, it
  survives the find/read/write/find loop that dsk_find's header rejected a
  kernel cursor for (+45 `.cold` +6 cell, kern_big).
- `dw-12` [K] **Subdirectories never grow.** A kernel-made folder on a
  512-byte-cluster volume (1.44MB, 1.2MB, the 360KB system disk) holds 14
  entries, and on 1KB clusters 30. RunCPM's save failed on the glass because
  of it (SPEC.md:133703-133709). DOS 2.0 grew subdirectories from day one.
  Add dskw_dirgrow at dskw_find's no-slot exit: alloc, zero (mkdir's loop
  factored out), flush, link, flush. A crash after the link leaves an
  harmless empty cluster (+90 `.cold` on kern_big, optional on kern_small).
  It is a functional gap, which is why it ranks here despite its bytes.
- `dr-3` [C] A failing read-ahead fill runs the full retry ladder, and then the
  direct read runs it again, a ladder per good sector ahead of a bad tail
  (disk.inc:7206-7208, 2025-2073). Mark a fill distinctly and fail it at its
  first error (+10).
- `dr-11` [C] A listing mount flushes **every** volume's cached chunks. Drop by
  volume, and stamp only that volume's records (+10).
- `dr-6` [U] Every dsk_xfer pays ~600-700 cycles of fixed overhead, cache
  hits included. Skip dsk_media_ok when the widget is armed, and hand back the
  row (~0 bytes, ~-250 cycles per transfer).
- `files-5` [K] fm_measure stages every entry through a 24-byte far copy to
  read two words. Walk the cache in place (0..-6 `.cold`, ~35 ms per
  300-entry navigation, PREDICTED; take the MartyPC figure in the final pass).
- `dw-11` [C] Short names with bytes 80h-FFh, or a leading 05h, are listed but
  cannot be opened, deleted or renamed: a file a DOS machine wrote is visible
  and untouchable (+8).
- `dw-21` [C] Two predicates answer the same question twice. Mask LFN detection
  in rmtree (an LFN slot with high attribute bits is deleted as a file today),
  and have the defragmenter ask dsk_img_next by cluster rather than *.IMG by
  name (+2 `.cold`, +10 FSCHECK.DRV).
- `dr-14` [K] dsk_fdd_probe clears unit 1's calibrated bit whatever unit it
  probed. Derive the mask from [fdd_unit] (+10 `.ovl`).
- `dr-10` [K] Correct .bounce's no-recursion argument, and pass CX=0 to the
  inner fpg_busy for a fill (0..+3).
- *Optional:* `dr-18` [U] separate mount (validation) from listing. That is
  OS/2's and the Mac's split, ±10, and it goes with WP25's dr-16 sweep.

**Files.** kernel/disk.inc, diskw.inc, files.inc, fsck.inc, kernel.asm (the
cell), apps/os88api.inc, apps/cc (a thunk for FIND_SEQ), SPEC.md 18/19.

**Bytes.** kern_small **+10** (dw-11, dw-21), plus +90 if dw-12 is taken
there, and `.ovl` +10. kern_big **+165..+174** `.cold` resident. kern_mega the
same, high.

**Risk.** Medium. dw-12 writes directory structure. Verify it with
os88disk.py `--verify` and with a host OS mounting the image.

**Rows.** `diskverify`, `volsig` (fast); soak `assocfind`, `fsckdisk`,
`fsckhd`, `rdmount`, `imgmount`, `fmarrows`, `navcalls`, `curdisk`,
`dirwsize`, `shedrelist`, `fddpage`, `bigvol`, plus RunCPM's save row. **New
row:** create 40 files in a kernel-made folder on 1.44MB, then verify.

**Dependencies.** None.

---

## WP17. Heap claim and shed policy

**Goal.** Make the heap refuse quickly rather than destructively, and place
growth where SPEC 50.2's two-ends rule says it goes.

**Findings.**

- `mem-3` [C] **A doomed claim sheds every cache, and may park workers once per
  shed.** kern_big's retry loop is compact → shed one → retry, until there
  is nothing left (memory.inc:952-989), and mem_compact clears [mem_parked]
  each time (INST_PARKW = 4 ticks per park). A claim larger than the whole
  heap destroys MEM_P_DIRW (priced at "seconds") and the FAT windows before
  answering CF=1. cmz_wclaim probes largest-first, so it does exactly this.
  (a) Before the first compaction, compare the request against
  mem_avail_lvl_p's exact total at the claimant's rank. The model is exact,
  so this never refuses falsely (+14 `.cold`). (b) cmz_wclaim sizes its first
  try from mem_avail. (c) Optionally, park once per claim (+6).
- `hm-8` [C] A claim refused for lack of a free **record** runs a full compaction,
  which can never create one, before the shed that would. Report "no record"
  distinctly (DX=1) and go straight to mem_shed_one (+6 `.cold`).
- `mem-9` [K] Regrow path 3 puts every grower at the top. mem_hifit keeps the
  highest candidate, so a bottom-up data claim that grows can land in a hole
  among the ceiling's regions with MC_DMA_HI clear: the mid-heap split SPEC
  50.2 was written against. Honour the door: lowest fit for bottom-up claims
  (+8 `.cold`).
- *Prototype behind a knob:* `mem-14` [C] **keep mem_tab sorted by base.** Every
  n² in memory.inc comes from the table being unordered (mem_cp_next,
  mem_claim_1's restart, mem_hifit/mem_runat, mem_bigrun). Compaction
  preserves order, insert and remove are a ≤320-byte rep movsw on rare
  operations, and SPEC 50.3.1's unbuilt best fit becomes nearly free. The
  blocker is kern_mega's selectors being paired with record INDEX
  (memory.inc:4376-4396): re-key them, or keep mega unsorted. -30..+20,
  unmeasured.

**Files.** kernel/memory.inc, compress.inc (cmz_wclaim), SPEC.md 50/66.

**Bytes.** kern_small **0..+8** (it has no compaction; mem-9 only, if
reached). kern_big **+28..+34**. kern_mega +28..34 high. mem-14 is 0 shipped
until its knob is decided.

**Risk.** Medium: the heap is everyone's.

**Rows.** soak `heapmap`, `heapcheck`, `heapgran` (fast), `regrowshed`,
`megashed`, `cmemmove`, `memp`, `heaphi`, `wdheapmove`, `lzcomp`,
`mega-heaphi`; HEAP-CLAIMS.md's `tools/heapmap.py` for the before and after.

**Dependencies.** None. WP2 depends on mem-3.

---

## WP18. Scheduler: UI priority, a wait primitive, lock honesty

**Goal.**

- Input should not wait behind every ready worker.
- A task should be able to block on something other than time.
- The text should describe what `sch_lock` has become.

**Findings.**

- `sched-16` [C] with `histsys-4` [K]: **a one-shot UI boost.** sch_switch
  scans round-robin from cur+1 (sched.inc:2468-2502), so a press waits up to
  N × 55 ms behind N ready workers. SPEC 7.3 fixed the lock half and
  explicitly left "the UI task does not run more often" open. sch_wake_ui sets
  [sch_uiboost] only when task 0 was sleeping. sch_switch spends it by picking
  slot 0 when task 0 is READY and fsx_task = 0xFF, so worker starvation is
  bounded at one UI pass per input. **Measure first** with two drawing
  workers, using 7.3's breakpoint method (+10..22 `.text`; mind t_hotpar).
- `sched-15` [C] **task_wake(AL = slot)**, a wait-on-anything primitive.
  - If T_STATE == 2, make it 1 and post sch_idlewake. Otherwise set a sticky
    wake-pending bit (T_FLAGS bit 1, free). task_sleep tests and clears the
    bit under its own cli.
  - It is ISR-safe and grows no record. A rare cell is appended (+24..30
    `.text`, +6 cell; os88api.inc update, no bump).
  - It enables IRQ-driven SB refill (today sbl_refill_task polls every tick,
    sb.inc:2539-2542), NIC-driven ETHER service, and later blocking gfx_lock
    waiters (sched-17, record).
- `sched-3` [C] sch_lock has become a general Forbid: it is raised in disk,
  diskw, memory, xmem, fsx, hiber, mega, megados and snd, while SPEC 7 says
  exactly two routines raise it. Under it, task_yield self-resumes (so a
  gfx_lock owned elsewhere spins forever), and task_sleep stores T_STATE=2
  and returns at once. Rewrite SPEC 7's paragraph, and make task_sleep a
  no-op under the lock (+7). A knob assertion goes in task_yield's locked arm.
- `sched-7` [K] **The two private ISR stacks have no canary and sit back to
  back**: mou_pstack is at LOW_SEG:06D6..0756 and sch_chstack starts at 0756.
  sch_chstack hosts code the kernel does not own: the ROM's int 08h, its int
  1Ch chain and option ROMs. Put SCH_MAGIC at its bottom and check it once
  per tick after the chain's `mov sp,bx` (not per switch). The seed goes in
  the `.ovl` room WP7 frees, or in a resident init (+9..12 `.text`).
  `isr-9` [C]: SPEC 8.5.1 has the ROM's EOI/sti order backwards (the IBM TIMER_INT
  stis at its head and EOIs after int 1Ch). Correct it, and take a STKDIAG
  reading on a field machine with option ROMs before anyone shrinks
  SCH_CHSTK.
- `sched-6` [C] evq_push wakes the UI task before the record is in the ring. Move
  the call after the cli (0 bytes; a bug-pass candidate as well).
  `sched-13` [C]: evq_pop banks DS for a prefix hazard that applies only to REP
  string ops; four `ss movsw` are the same 4 bytes and ~35 clocks cheaper
  (0 bytes). `sched-11` [K]: one macro for the chain swap and one for the
  hook guard, so fsx_rtick gets sched-7's canary too (0 bytes).
- *Optional, measure first:* `histsys-5` [C] **release sch_lock across the
  floppy int 13h on the UI task.**
  - Today every int 13h freezes every task for ~400 ms (disk.inc:1462), the
    SB refill and drain workers included.
  - The XT BIOS polls IRQ6 with IF=1, and on an AT int 15h AH=90h can
    task_sleep the UI task until AH=91h.
  - The hazards are named: cur_inxfer, the retry lock dance, kern_mega and
    hibernate keep the lock, and diskw.inc:6347 is a second site.
  - Take one MartyPC number (a worker's fps during a 16KB read) first:
    +20..40 `.text`, +~25 for the AT hook. isr-15 (record) is the same
    proposal; take it if SB underruns during disk reads are reported.

**Files.** kernel/sched.inc, events.inc, ui.inc, fsx.inc, stkdiag.inc,
drivers/sound/sb.inc (refill on wake), apps/os88api.inc, SPEC.md 7, 8, 8.5.1.

**Bytes.** kern_small **+56..+77**, kern_big **+56..+77**, kern_mega
**+56..+77 conv** (`.text`). The optional histsys-5 adds +45..65.

**Risk.** Medium: the scheduler is the hot path, and t_hotpar parity applies.

**Rows.** `hotpar`, `stkclass` (fast); soak `kbdpreempt`, `kbdburst`,
`timerrace`, `timerbounce`, `stk0water`, `stkpanel`, `splashspin`,
`vidsound`, `midiracksb`, `midiracksbrate`, `skiessound`, `wakedrain`,
`mega-timerrace`, `mega-stk0water`.

**Dependencies.** WP7 (the `.ovl` room for sched-7's seed).

---

## WP19. API boundary: context tags, cheap fences, a checked build

**Goal.** Turn the API's prose rules ("UI task only", "lock held", "worker
safe", "ISR safe") into something a tool reads. Fence the doors where a
violation corrupts shared state silently, at single-digit bytes each.
Everything else goes behind a knob.

**Findings.**

- `abi-8` [C] **Machine-readable context tags.** Add `; @ctx ui|any|worker|hook|lock`
  in each contract comment, where abilock already fingerprints it. Add a host
  gate on stkdepth's call graph: no worker root (OSAPI_TASK_SPAWN target,
  OSAPI_DRV_TASK) and no FSXF_RATE hook may reach a cell its tag forbids.
  docs/INDEX.md prints the tag (WP24's isr-16 table is generated from it).
  SPEC 20.3 calls the file slots' rule "an author rule with no enforcement
  behind it", and what it protects is the single api_name/api_name2 staging
  buffer and [api_sysfp].
- **Fences that refuse instead of corrupting:**
  - `dw-4` [C]: `cmp byte [sch_cur],0 / jne refuse` once in the file cells'
    N-stub (+9 `.text`), or in dskw_mounted/dskw_gate (+14 `.cold`). A worker
    inside a file cell while the UI task is in another one cross-links the
    FAT silently. **First prove that kerndos, HIBER and DOS.O88's worker
    (dvw_start, dos.asm:2010) make their file calls on task 0.**
  - `loader-5` [C]: OSAPI_PKG_START's "no nested start" and "UI task only". A
    nested start overwrites ld_rec/ld_base/ld_need/ld_lname and publishes
    I_SPTR=0 on a published record (+15..21 `.cold`).
  - `dr-2` [C]: dsk_batch_begin does nothing unless the caller holds the gfx lock
    (fpg_arm's test, fprog.inc:616-620). A package-opened batch survives
    until the next unlock, trusting banked BPB, FAT and cache across a swap
    (+16 `.cold`).
  - `ui-2` [K]: **the liveness funnel.** Content-click, right-click and
    W_ONDRAG read the handler before gfx_lock and never re-test liveness,
    which is SPEC 13.9.2's race, fixed only for the timer. `test byte
    [si+W_FLAGS],1 / jz .out` at ui_bill's head covers every caller. Drop
    .tp_locked's own re-test (net 0 kern_big, +5 kern_small). Add a
    `clickrace` row modelled on timerrace.
  - `histsys-11` [C]: wm_isrec at wm_wake's entry and at MEMC_POST, so a stale
    worker BX cannot scribble kernel DS (+6..12).
  - `hm-14` [C]: OSAPI_MEM_P refuses an unknown verb instead of answering
    MEMP_AVAIL with the caller's CF (+4). **This is a prerequisite for WP2's
    MEMP_PURGEABLE.**
  - DF at boundaries. `ui-7` [K]: `cld` in clip_put/clip_get (+1..2). A std
    package today makes rep movsb walk down from clip_seg:FFFF through up to
    64KB above the claim. `isr-4` [C]: `cld` once in sch_isr after KDS, and in
    kern_mega's fsx_rtick (+1 each). The splash's rep movsb, DSV_TICK and the
    FSXF_RATE hook all inherit the interrupted DF today.
- `wm-13` [C] **Write the rule for window pointers into SPEC 11.** Some wm doors
  validate BX (wm_saveu, wm_noanim, wm_quiet, wm_ownseg, two inst_wchk slots),
  most do not, and the inline setters cannot. Recommendation: BX is trusted
  at every door, as SPEC 20.8's trust model says. Remove the inconsistent
  checks only where they are free, and put validation behind the checked
  build below. WP8's wm-2 makes the shared funnel safe either way.
- `abi-15` [C] with `engsys-20` [U]: **one checked-build knob** (APICHK=1, 0
  shipped bytes, bounded by guard 2).
  - Window-taking cells become same-length padded jump cells into a
    validating stub: BX in the window table, W_SEG = the caller's DS.
  - File cells check [sch_cur]=0, and the slots whose contract names a lock
    assert it.
  - DF=0 is asserted at the SLOT/RSLOT dispatch, reporting through §57's
    debug registry.
  - One soak row drives the package sweep on that kernel. This is Windows
    3.1's debug kernel, which found most Win16 misuse.
- `abi-7` [C] Packages write the window record's geometry, which the SDK says they
  may not (Paint's pt_wfix, the Sticky library's hide-write-show). The writes
  exist because no door sets geometry without repainting. Add one as an
  **appended cell** (see the watch list: not CX's top bit), and a host lint
  for `[es:..+W_*]` stores under apps/ with an allow-list (+10 `.text`).

**Files.** apps/os88api.inc (tags, abi-7 cell), tools/stkdepth.py, a new
tests/unit/t_apictx.py, tools/os88index.py, kernel/kernel.asm (N-stub, cell
table under APICHK), diskw.inc, loader.inc, disk.inc, ui.inc, wm.inc,
memory.inc, clip.inc, sched.inc, fsx.inc, SPEC.md 11, 18.4, 20.3, 21.5.4.

**Bytes.** kern_small **+67..+85**. kern_big **+62..+80**: dw-4 +9..14,
loader-5 +15..21, dr-2 +16, ui-2 0, histsys-11 +6..12, hm-14 +4, DF +2..3,
abi-7 +10. kern_mega +15..20 conv, +48..62 high. The APICHK knob: 0 shipped.

**Risk.** Medium. Each fence is a new refusal on a path some package may
already take wrongly, so the final pass must run the whole package sweep, not
the kernel rows only.

**Rows.** `api-abi`, `abilock` (fast); soak `timerrace`, a new `clickrace`,
`wseq*`, `dtwrite`, `deskitem`, `clipkeep`, `fcpapi`, `thewire` (nested
start), `kdos` (DOS.O88's worker), `dosmem`, and a new `apichk` row on the
knob kernel.

**Dependencies.** WP9 (the level, for abi-7's new cell). WP14 (the header
grammar) is useful but not required.

---

## WP20. Graphics hot paths

**Goal.** Close the remaining per-adapter holes in the primitives packages
call most. Each item is measured first on its own `gfxbench` row
(os8088_xt_vga, or a CGA `_gla` twin) before the bytes are spent.

**Findings.**

- `gfx-4` [C] **gfx_points has no planar path.** Every VGA point is a whole
  gfx_pixel (640.87 µs) against 165.96 per point on the 1bpp loop (SPEC
  5.13). Cyclone, Missile, Tank, os88gfx's GFXE_POINTS sink and the C SDK's
  os88_gfx_points get none of the 3.9×. A fourth loop body in `.vga`
  (Set/Reset armed once) is predicted at 5-10× per point (+60..70 `.vga`).
- `text-9` [C] The VGA ico_pass keeps its per-word column bookkeeping in memory:
  ~90 clocks a word, ~4.8 ms per 32×32 desktop icon on the owner's 5150 with
  a PVGA1A (PREDICTED). Do one test per icon for "fits in the row" and use a
  compare-free copy, which gives the off-right early-out free (+20 `.vga`).
- `gfx-9` [C] The shipped 1bpp XOR interior is a byte loop. Every Disk-window
  selection band and menu highlight on CGA/Hercules passes through it. The
  knob arm's word loop (sw_k_xrow) is ~40% cheaper per byte (+8 `.text`, ~1
  ms per band PREDICTED).
- `gfx-8` [U] A right-edge clip forfeits both blit decoders and sends the
  block to the per-run path at ~0.5 ms a run. Clamp w to vid_cw − x once
  (+10 `.text`), and the even left clip too (+~12).
- `gfx-15` [C] gfx_clip_run tests all 16 fragments with no early exit (~0.5 ms
  per primitive under a full region). If wm's region builder keeps the list
  y-sorted, stop at the first y1 > y2 (+6 `.text`; the sort is WP21's).
- `gfx-13` [C] "Asked the depth before entering the display" is a recurring bug
  class (gfx_xor_rect's reboot, the menu save-under, gfx_blit1's pen,
  gfx_blitp, gfx_points). Give the whole-shape class one entry macro that
  hides, enters and only then dispatches, plus a GFXAUDIT assertion. It is
  ~0 bytes of code motion.
- `shell-9` [C] Every damage pass recomputes every shown desk cell's rect with a
  div and two muls (~150 µs a zone). Add one early-out of the damage rect
  against the grid's x extent (+8 `.cold`, +6 per DESK_GRID expansion).
- `shell-10` [C] WIN95.DRV phases button art one bit at a time (~0.5-1 ms CPU per
  title bar). Use `shr ax,cl` (+6 module).
- `gfx-11` [C] The theme moves out of vga12.inc into kernel/theme.inc (mind
  t_hotpar parity), and §4 is corrected (0 bytes).

**Files.** kernel/vga12.inc, softgfx.inc, icons.inc, desk.inc, winmod.inc,
a new kernel/theme.inc, SPEC.md §4, §5.

**Bytes.** kern_small **+32..+44** (gfx-9, gfx-8, gfx-15, shell-9). kern_big
**+112..+134 on the VGA gauge**: +24..36 `.text`, +8 `.cold`, +80..90 `.vga`;
a mono machine pays +32..44. kern_mega +24..36 conv, +8 high. WIN95.DRV +6.

**Risk.** Low-medium. Look at a 1bpp adapter before calling it done (§39.4).

**Rows.** `hotpar` (fast); soak `gfxpoints`, `gfxptsmall`, `xorrect`,
`dispblit`, `dispblitp`, `blitp`, `blitcut`, `gfxlk`, `glyphcost`,
`deskbench`, `dispapps`, `rdicon`, `zonedmg`, plus the games' rows (cyclone,
missile, tank) for gfx-4.

**Dependencies.** WP21's region sort, for gfx-15's early exit.

---

## WP21. The damage pass

**Goal.** Tidy the damage pass's implicit calling convention, then measure,
and only then prototype, the redraw-less-than-the-closure rethinks.

**Findings.**

- `wm-17` [C] **The one-shot arguments to the damage pass are globals with many
  lifetimes**: wm_dmg_mine cleared by wm_raise, wm_dmg_rzwin by wm_resize_nb,
  dk/stwin/su_son by wm_dmg_wins, and more (wm.inc:2592-2595, 3588,
  5285-5506, 11083-11087). Use one flags byte plus the rect words, cleared in
  one place at wm_paint_dmg's exit (-10 `.text`). This closes the class that
  the bug pass's wm-3 (the empty-rect arm leaks every one-shot argument) is
  one instance of.
- `wm-12` [K] Transitive marking redraws windows that were never damaged: a
  window is marked when it overlaps any marked window below, because the
  lower one is redrawn whole (wm.inc:5384-5420). **Count .mbelow hits on
  MartyPC first (one rebuild).** Then prototype a visible-region predicate
  behind a knob (+40..80, unmeasured).
- `wm-R1` [C] The rethink behind it: exact clipping (visRgn ∩ updateRgn, as on Mac
  and Windows). The closure is forced only by the cull rounding a cell
  outward and by gfx_blit4/gfx_scroll taking no clip (SPEC 11.3's two named
  holes). Prototype behind a knob, measured on MartyPC, and drop `.mbelow`
  if it wins (+100..250 estimated; 0 shipped until decided). Keep the region
  list y-sorted while there, for WP20's gfx-15.
- `wm-6` [C] wm_resize clamps against [vid_w], the union width, and ignores the
  side-dock band. An app's OSAPI_WM_RESIZE can straddle the seam or cover a
  side dock. Clamp with wm_fit's band words, and decide whether the union
  width is intended on two displays (+10 kern_big).
- `wm-4` [C] wm_ask_size trusts a W_ONSIZE negotiator to keep BX. Push/pop BX
  (+2), and say in the SDK that a negotiator may clobber what any callback
  may.
- `wm-7` [K] OSAPI_WM_DESTROY's SDK rationale ("as OSAPI_WM_HIDE wants it")
  is stale, because hide takes the lock itself now. Fix the text. For
  FULLSCREEN, either give it the wm_lk_go door and refuse a hidden window's
  raise (+6..19), or keep the contract and fix the text.
- `wm-9` [K] wm_su_drop zeroes the slot and ignores mem_free's refusal. SPEC
  66.3 rule 3 already forbids the only path (a reloc proc calling
  CLIP_SET/SAVEU), so clear the slot only on CF=0, or state the rule at the
  door (0..+3).

**Files.** kernel/wm.inc, ui.inc, extmod.inc (EXK wm_ask_size),
apps/os88api.inc (wm-4, wm-7 text), SPEC.md 11.

**Bytes.** kern_small **-8..+11**, kern_big **+2..+21**, kern_mega +2..21
conv. The prototypes are 0 shipped.

**Risk.** Medium for wm-17 (every early-out must clear through the one
place). The prototypes are knob-only.

**Rows.** `hotpar`, `chromesites` (fast); soak `zonedmg`, `dmgcull`,
`dispclose`, `dispzoom`, `dispband`, `sizesnap`, `deskzoom`, `zoomsave`,
`dispapps`, and `mega-` twins. The widget-drop row in PERFORMANCE.md Part 5
is the bar for wm-12/R1.

**Dependencies.** None.

---

## WP22. Interrupt ownership and input robustness

**Goal.** One kernel-side story for IRQ vectors and the 8259 mask. Plus the
input-path weaknesses that are not bugs.

**Findings.**

- `isr-13` [C] Six owners write vectors and masks, each its own way: the kernel,
  SOUND.DRV, USBMOUSE, XMEM, os88spk packages and DOS.O88. The kernel's
  restores (mouse_unhook, sched_stop, spk_off) write back without checking
  that the vector is still theirs (sched.inc:639-664); only USBMOUSE checks.
  **Take the IMR shadow alone now (+~25)**: per-owner bits, MASK/UNMASK
  through it. The OSAPI_IRQ cell (HOOK, LIFO-checked UNHOOK, common EOI and
  spurious-7/15 stub) waits for the flag day as ABI-BREAK-LIST L3.
- `isr-6` [K] The IMR read-modify-writes at IF=1 on task context:
  mou_lockon, mou_p2_off, mouse_unhook, and SOUND.DRV's sbl_hook/sbl_cell. A
  switch between the in and the out writes back a stale mask. The likeliest
  other writer is a windowed DOS program on a preempted worker, because
  DOS.O88 exchanges vectors but not the IMR. Wrap each in pushf/cli…popf
  (+6 `.text`, +6 SOUND.DRV), or route them through the shadow. It is also a
  bug-pass candidate, so land it once.
- `drivers-6` [C] **A driver image is freed with no check that its IRQ vectors
  were unhooked** (driver.inc:4232-4351). The next claim in that memory then
  runs as an ISR. After DETACH, or a refused ATTACH, scan the 16 hardware
  vectors for the image segment; drv_hi_ivt already does this scan on
  kern_mega (driver.inc:4410-4455). On a match, leave the image claimed (a
  leak, not a crash) and bank a code (+~25 `.cold`).
- `mem-7` [C] **Nothing bounds the interrupt context any more.** SPEC 66.3 bounds
  ISRs by "the MC_DMA pin", which no longer exists. Only MEM_K_DRV moves at
  IF=0 (memory.inc:2974-2976), yet mem_region_reloc's IVT rows rewrite
  vectors naming **any** moved block. DOS chains int 08h with its own CS, and
  its region is movable (dos.asm:11445-11446). So move at IF=0 whenever the
  block's base appears in any of the 17 mem_rr_ivt segment words, and state
  an ISR author rule in SPEC 66.3 (+12 `.cold`).
- `isr-17` [C] The SB driver leaves the master PIC reading its ISR register
  (OCW3 0Bh at sb.inc:937-942 and 3294-3298 is never re-selected). Re-select
  0Ah, or write "every IRR read selects 0Ah first" into SPEC 1 (+8
  SOUND.DRV).
- **Input.**
  - `histsys-6` [C] with input-13 (record): ui_task takes one int 16h key per
    pass, then drains the whole mouse ring. A click on B followed by typing
    can send the first key to A. kbm_isr pushes a payload-less EVT_KEY marker
    when the BIOS tail moved, and the drain fetches the key at the marker.
    Keep the poll as the fallback (+25..30 `.text`). **Measure how often it
    happens first** (input-13's ask).
  - `input-10` [C]: PS/2 sync rests on byte 0's bit 3 alone, so a misaligned
    stream reads phantom clicks. Reset the phase when [ticks] has moved ≥ 2
    since the last aux byte (+12 `.text`, +2 `.bss`, kern_big).
  - `input-11` [C]: the hot-plug poller drops DTR on every unleased port every
    3 s for the whole session on a mouseless machine, hanging up a modem.
    Skip ports whose MSR shows DSR or DCD, the lease's own rule (+14 `.text`).

**Files.** kernel/mouse.inc, sched.inc, snd.inc, driver.inc, memory.inc,
events.inc, ui.inc, kbddiag.inc (kbm_isr), drivers/sound/sb.inc, SPEC.md 1,
9, 10, 66.3.

**Bytes.** kern_small **+45..+50** (isr-6, histsys-6, input-11; plus +25 with
the shadow). kern_big **+96..+101**: core drivers-6 +25, mem-7 +12, isr-6 +6,
plus input +53..58; +25 with the shadow. kern_mega +50..55 conv, +46 high.
SOUND.DRV +14.

**Risk.** Medium. Vectors and masks are where a mistake hangs the machine.
86Box with an AMI or Phoenix AT BIOS is the place to look.

**Rows.** `drvmem` (fast); `ps2mouse` (full); soak `irqgrab`, `dosirq`,
`sbirq5`, `usbmouse`, `wheelps2`, `mouseup`, `kbd4f`, `kbdburst`,
`serlease`, `wirefleetnomouse`, `drvmove`, `heapdrv`, `dosmouse`, `kdmouse`,
`mega-irqgrab`, `box-ps2mouse`.

**Dependencies.** None. The bug pass's input-2 (no switch while another IRQ
is in service) and isr-2 (the feed's SS) touch the same files, so sequence
the two passes there.

---

## WP23. Video detection and fullscreen facts

**Goal.** Remove two of SPEC 39.1's "documented scope cuts", because the
answers are already in registers the probe reads. Make the blanker and the
fullscreen capabilities tell the truth.

**Findings.**

- `video-2` [C] **A plain MDA primary is programmed with Hercules graphics
  timings.** vid_detect makes every mono-flagged machine VID_HERC, then writes
  R0=35h. A real MDA ignores 3BFh and stays on 9-dot clocks, so the result is
  ~33 kHz horizontal against a fixed 18.4 kHz IBM 5151: a dead screen at
  best, and period documentation warns about the monitor. In vid_detect's
  mono arm, write 3BFh=1 and run an alias test (vid_memchk's idiom) or
  Wilton's 3BAh bit-7 loop. On an MDA, probe B800 and choose VID_CGA; on an
  MDA alone, halt with a message. vidsel.inc:145-152 and 231-253 already
  discriminate MDA from Hercules (+25..35 `.boot2`; it may cost a blob
  sector of disk on kern_big).
- `video-4` [C] A 64KB EGA is driven planar although AH=12h's BL already says
  64KB (viddet.inc:558-562). `or bl,bl / jz .equip`: a 64KB EGA does mode 6
  perfectly (+4 `.boot2`).
- `video-6` [C, PLAUSIBLE] The planar blank (AC 12h, Color Plane Enable) is
  used for VID_VESA, whose 8bpp and 16bpp pipeline likely bypasses it, so the
  idle blanker may do nothing on a VESA desktop. Give the VGA and VESA kinds
  SR01 bit 5 (Screen Off) by read-modify-write, and keep AC 12h for the EGA,
  whose SR01 is write-only. **Test under QEMU at 8 and 16 bpp first**
  (+12..14 resident kern_big).
- `video-8` [C] vid_apply builds and arms the row table on VID_VESA, where the
  mouse stack may overwrite it. Skip the build and leave vid_rowmax 0 when
  [vid_planes] > 4: the invariant becomes structural, and it saves ~4.6 ms
  per switch (+7 per VID_APPLY expansion: +7 `.boot2`, +14 modules).
- `video-12` [C] FSI_PAGES advertises pages that fsx_page refuses (VGA mode 0Dh,
  dual-card Hercules page 1). The hardware count is honest; publish the grant
  as FSIF_FLIP (FSI_FLAGS bit 3). Then CLAUDE.md's "probe the slot" advice
  becomes a flag test (+8 kern_big, +4 kern_small; os88api.inc, no bump).
- `video-13` [C] EGA's caps row omits mode 0Dh, which every EGA has and which
  already has an fsx_mtab row. Gate it on one 86Box EGA run of a mode-0Dh
  consumer (0 bytes).
- `video-14` [C] Zero-byte macros VIDK_PLANAR and VIDK_MONOCARD for the
  hand-spelled kind-class idiom at ~10 sites (0 bytes).

**Files.** kernel/viddet.inc, vidsel.inc, fsx.inc, mouse.inc (row-table
comment), apps/os88api.inc (FSIF_FLIP), SPEC.md 39.1, 53.4, 64, 120.5,
CLAUDE.md (the WireBall paragraph, once the flag exists).

**Bytes.** kern_small **+4**, kern_big **+20..+22**, kern_mega +20..22 conv;
`.boot2` +36..46; modules +14.

**Risk.** Medium. video-2 is a detection change on the oldest hardware, so
86Box's xt profiles and MartyPC's `_vga_mda` and `_vga_herc` machines are
the instruments. MartyPC models no plain MDA alone. Say so if it cannot be
shown.

**Rows.** soak `vesa`, `vesa8`, `vesacp`, `vesafsx`, `savervga`,
`dispsaver`, `vidpage*`, `vidpageherc`, `vidfskeysflip`, `dispfsxherc`,
`dispfsxcga`, `wireballvideo`, `wireballmono`, `deskclockherc`, and
`lemdat` (fast) for the MDA tables.

**Dependencies.** None.

---

## WP24. Documentation: one copy of each fact

**Goal.** Every fact has one authority, and every copy is either generated
or deleted. 0 shipped bytes.

**Findings.**

- `engsys-5` [C] with `abi-10` [C]: **the slot contracts exist in up to four places.**
  - The SDK `%define` comments are the fingerprinted authority.
  - SPEC 20.3's 37-entry block (SPEC.md 44800-44960) is a second copy, read
    by no tool, and stale both ways. SND_PLAY is stale in SPEC. FULLSCREEN is
    stale in the SDK, whose comment says BX is used on enter and only enter
    refuses, while the code binds BX on exit too (wm.inc:4287-4313).
  - SPEC 53.8 is a third copy and kernel.asm's table comments a fourth.
  - Delete §20.3's and §53.8's blocks, pointing at the SDK names. Cut each
    kernel cell comment to one line (SDK symbol + §). Fix FULLSCREEN with
    abilock `--update`, no bump.
- `engsys-4` [K] **SPEC's front matter describes retired designs as
  current.**
  - §1 rule 2 gives the wrong segments and says "no far code", while 380
    `call COLD_SEG:` and 22 COLDCALL sites exist.
  - §1 rule 4 says "four sections" (about 30 exist). §33 says cold code is
    near-called.
  - §4 lists a splash entry at a free address, and its table misses 35 of
    74 kernel files.
  - Rewrite §1-§4 and §15-§17 as current front matter (one paragraph per
    rule, plus its gate), and move the narrative to docs/history/. **Gate
    §4:** every kernel/*.inc appears in its table.
  - Do it with `gfx-11` [C]'s §4 correction (WP20).
- `engsys-10` [C] with `bytes-10` [C]: **AGENTS.md has diverged from CLAUDE.md by 726
  lines.** It lacks the RUNGS banner, "Design for BYTES", ".cold IS RESIDENT"
  and the package-region ratchet, so a Codex session is never told the byte
  rules. Generate it from CLAUDE.md with a substitution table, and check it in
  checkdocs the way docs/INDEX.md is checked.
- `engsys-18` [C]: **SPEC.md is two documents**: 137,687 of its 245,112 lines are
  package design notes. Move each package section to `<PKG>-SPEC.md`, keeping
  its numbers. That is a mechanical citation rewrite checkdocs already
  resolves, and WEAVE/C64/ARCADE are the precedent. Add a generated
  number-to-line TOC to INDEX.
- `bytes-9` [K]: KERNEL-MEMORY.md's ladder and Task Manager tables are hand
  copies, ~9KB stale (KERN_SIZE 105,984 against 96,768 today). Have `--bless`
  emit both between markers.
- `isr-16` [U]: a generated "callable at interrupt time" table in
  docs/INDEX.md (which slots an ISR, a rate hook or DSV_TICK may call, with
  SS/DS/ES/DF), built from WP19's `@ctx` tags.
- `engsys-21` [C]: split the Makefile (20,872 lines, 53% comments, one include) by
  subject into mk/*.mk on apps/cc/Makefile.inc's precedent, and move the long
  incident narratives to the doc that owns each subject.

**Files.** SPEC.md, the new `<PKG>-SPEC.md` files, AGENTS.md, CLAUDE.md,
tools/checkdocs.py, tools/os88index.py, tools/kernsize.py, docs/INDEX.md,
docs/KERNEL-MEMORY.md, docs/history/, Makefile and mk/*.mk,
apps/os88api.inc (FULLSCREEN).

**Bytes.** 0.

**Risk.** Low, but large diffs. engsys-18 and engsys-21 are each a session of
their own, and mechanical. Verify by checkdocs and an unchanged `make help`.

**Rows.** `checkdocs`, `docindex`, `checkreadme`, `abilock`, `guidelock`
(fast); `guidelock-full` (full).

**Dependencies.** WP10 (engsys-2's heading-drift check should exist before
SPEC is split). WP19 (tags, for isr-16).

---

## WP25. The comment discipline sweep

**Goal.** Seven area reviewers found the same defect independently:
**comments as archaeology.** Code headers carry "it was X until size pass N",
the original contract line stands while a later pass appends a paragraph, and
the history is where the drift collects.

- vga12.inc is ~7,200 lines with ~2,000 lines of instructions, and 25+
  statements about code that is gone.
- wm.inc is 12,041 lines, 6,235 of them pure comment, with 22 passages
  describing retired designs.
- ui.inc has ~1,890 comment or blank lines against ~1,150 of code. sched.inc
  has 3,092 lines for ~700 of code.

**Findings.**

- `gfx-12` [C] `wm-15` [C] `ui-15` [K] `sched-10` [C] `text-14` [C] `video-15` [C] are the
  per-area sweeps, each with its doc_fixes list in data/*.json.
- `mem-12` [C]: ~450 lines of headers describe ten routines that no longer exist
  as bodies (mem_frameless, mem_busy_seg, mem_cp_*, mem_own_drv, mem_dmaok,
  mem_total_kb).
- `dr-16` [C]: about ten orphan contract headers in disk.inc, and running counts.
- `input-16` [C]: mou_claim was folded away in size pass 17, and 22 references
  remain.
- `sysmisc-12` [K]: W_FLAGS "visible" is the literal 2 at about thirty sites.
  Add WF_VIS (0 bytes).
- **The convention**, written once into CLAUDE.md, AGENTS.md and SPEC §1:
  - A routine header states the current in/out/clobbers/invariants and the
    reason.
  - History goes into the commit message (`git log -L` finds it), the SPEC
    section, or the pass's report, leaving a one-line pointer.
  - A derived or hand-set constant's comment states its invariant and its
    guard (boot-9's rule, WP6).
  - A prose count carries its moment or is not written (engsys-6, WP10).
- **The gate that keeps it** (gfx-12, input-16, mem-12): checkdocs fails a
  backticked identifier in a kernel comment that is neither a symbol in the
  build's map nor on an allow-list, and refuses an "INLINE since size pass N"
  header whose name is neither a label nor in SPEC.

**Files.** Every kernel/*.inc the sweeps name: vga12, softgfx, wm, ui,
sched, stkdiag, font, menu, fprog, viddet, vidsel, fsx, memory, disk,
mouse, hiber, vmmouse. Also tools/checkdocs.py, CLAUDE.md, AGENTS.md.

**Bytes.** 0. **Prove it by `cmp`**: every shipped image must be
byte-identical at one commit (CLAUDE.md fork rule 6's md5sum method).

**Risk.** Low, provided the cmp proof is taken. It is a large diff, so split
it by file across sessions if needed. The doc_fixes already applied by this
review's task #2 should be checked first so nothing is done twice.

**Rows.** `checkdocs` (fast). No emulator row can see a comment, by
construction.

**Dependencies.** WP24 (the convention lands in CLAUDE.md there or here,
once).

---

## WP26. Contract clarifications (one abilock update)

**Goal.** Make the SDK say what the code does, wherever the code is right and
the text is silent or wrong. Batch it into **one** `abilock --update` with no
bump.

| id | the sentence to add or fix |
|---|---|
| `abi-12` [C] | A rule for the next multiplexed door: the verb goes in AL unless AL is an argument, then BL. Today it is AL, AH or BL by local history. |
| `abi-14` [K] | An X cell's pointer argument is written "SI (in your DS; ES is not read)". OSAPI_GFX_SPANS says ES:SI. |
| `isr-3` [C] | OSAPI_WM_WAKE from an ISR requires SS = LOW_SEG: evq_push addresses the ring through SS. DOS.O88's dvw_irq0 already moves to a LOW_SEG slice first. Optionally make evq_push SS-independent (+3). |
| `sched-4` [C] | On the UI task, OSAPI_TASK_SLEEP ends at the first input (sch_wake_ui stores T_STATE=1 unconditionally, sched.inc:722). Loop on OSAPI_GET_TICKS for a minimum. |
| `sched-5` [K] / `histsys-15` [K] | Document TASK_SLEEP's range as 1..32,768 ticks; 0 and 1 both mean the next tick. **The two verifications disagree by one.** Re-reading sched.inc:1166-1168 (T_WAKE = ticks+AX) against 2274 (`inc word [ticks]` before the scan) and 2376-2377 (`cmp dx,[bx+T_WAKE] / js`), the first scan sees ticks+1, so AX = 0x8001 still sleeps its full length and only AX ≥ 0x8002 wakes at the next tick. histsys-15 has it right and sched-5's "0x8001.. at once" is one too strict. 32,768 is the round contract either way. |
| `wm-8` [C] | OSAPI_WM_TIMER: AX ≤ 32,767 ticks (30 minutes); larger fires within a tick. |
| `gfx-5` [C] / `text-3` [C] | The pen after OSAPI_GFX_BLIT4 and after OSAPI_FONT_RUN is **unspecified**: it depends on adapter, alignment, run density, theme and clip. Preserving it would cost +8 each; document instead. |
| `ui-8` [C] | The clipboard is **not** worker-safe. The SDK and SPEC 55.4 say it is, but clip.inc says it is not, and it claims memory. Pricing worker access is +16 (a sch_lock bracket), if ever wanted. |
| `ui-9` [C] | W_ONMOUSEUP: either "unless the event ring overflowed" (0 bytes), or dispatch it at the drop when [ui_armr] = 0 (+12). |
| `loader-10` [C] | OSAPI_PKG_START: a movable source claim must be pinned across the call, because ld_alloc can compact (The Wire already does this, thewire.asm:3234-3236). |
| `hm-2` [K] / `mem-6` [K] | The self-region arm of mem_find_own, which lets a package free or regrow the region it runs in, is a **deliberate**, documented choice (loader.inc:1690-1695, SPEC ~120292). Record the owner's decision either way. Closing it costs +3..8 in the package doors only. |
| `histsys-17` [C] | Name the eight lock-bound display-context fields gfx_dc, make them adjacent, and clear them with two or three word stores (-4..-8; ABI-BREAK-LIST R6 gets a home). |

**Files.** apps/os88api.inc, SPEC.md 8, 13.9, 20.3, 21.5, 55.4, 66,
kernel/vga12.inc (histsys-17), events.inc (isr-3 optional).

**Bytes.** 0 for the text. histsys-17 is **-4..-8** on every kernel (kern_mega
conv), which makes it the recommended set's whole figure. ui-9's +12 and
isr-3's +3 are optional.

**Risk.** Low.

**Rows.** `abilock`, `checkdocs` (fast); soak `mouseup` (ui-9).

**Dependencies.** None. Batch it after WP19's tags if both are in flight, so
there is one update.

---

## WP27. Desk, dialogs and menus: user-visible facts

**Goal.** Small fixes and additions where the user sees the kernel being
inconsistent, plus the one size saving in that area.

**Findings.**

- `ui-10` [K] **About's fourth line is composed from [vid_w]/[vid_h]**
  instead of a per-kind string table: 107 bytes resident on kern_big, 79 on
  kern_small, and a row needed per new kind. The missing VESA row is the bug
  pass's ui-1, so this removes that class (-30..-40 `.text`, ESTIMATED).
  Reuse clock.inc's decimal formatter and measure the span.
- `files-7` [C]: the Save box's commit ignores a failed put-back mount. Kernel
  requesters (drv_dlg_done_x, fm_img_done_x, Uncompress To, the clone to
  image) then act at the root with the write gate shut. On CF, skip the
  callback with a beep (+2..5).
  `files-17` [U]: also hand the callback AH = volume and BX = cluster (+6,
  additive).
- `files-8` [C]: opening a chooser ends other windows' rename and new-folder
  prompts without repainting them. Mark each FSD_PIXELS (+4).
  `files-9` [C]: modality filters presses, not drops, so a file dragged out of a
  Save chooser moves into a Disk window behind it. In fm_dgdrop, refuse any
  target that is not the chooser (+10). `files-10` [C]: kern_small keeps a closed
  window's 2KB cache claimed until the next tenant. Free it at close (+10,
  kern_small).
- `shell-3` [C]: OSAPI_DESK_ITEM remove is keyed by a zone the API says is not an
  identity, so WIN16 probes every zone, each probe a module load. Add AH=0FEh,
  "the row whose volume and path are this record's" (+12 CTRL.DRV, WIN16
  -12). **Check what an older kernel does with AH=0FEh.**
- `shell-7` [C]: STIKMENU.DRV writes the kernel's service-item record. Hand
  ui_svc_open the file and name, or launch by name as stm_a_launch does
  (0..+4). `shell-12` [K]: optionally terminate a driver's file and name too
  (+10 or 0).
- `shell-8` [K]: the boot reader keeps up to 1KB more shortcut claim than the
  rows need. mem_regrow to ceil(rows/8) KB after the row move (+10 `.boot2`;
  frees heap for the session).
- `shell-11` [C]: basic dock_geom only works at boot or with CTRL.DRV loaded.
  State the invariant, or fall back to dkb_band (0..+3).
- `sysmisc-6` [C]: the Screen Saver settings window outlives the Control Panel,
  so its Ok's save waits for the next panel close or a reboot, and a
  power-off loses it. Close it from the panel's teardown with an SSV_* verb
  (kernel-private, t_mirror-gated), or flush when no panel is open (+12
  `.cold`, +15 SAVER.DRV).
- `ui-16` [K]: under TOAST_Q, one raiser's "take my message down" retires
  another's. Retire only when DH = [toast_inst] or the caller is the kernel
  (+6..10 kern_big).
- *Additive cells* (WP9's OS88_NEED for callers):
  - `ui-11` [C]: one double-click interval for the machine. The kernel has four
    detectors that are "deliberately the same number", and packages carry
    their own. One `.text` byte, read by all four and set from the Mouse page
    (+5, plus a 4-byte inline cell).
  - `ui-12` [U]: a clipboard generation count (+4 `.text`, +2 `.bss`, +4
    cell).
  - `ui-19` [U]: W_ONMENU, a menu "about to drop" moment for packages, so
    they can grey on facts (SPEC §47 rule 5) without re-pointing items at
    every state change. WIN16 already synthesises WM_INITMENU (+14 `.text`,
    +24 `.bss`, +6 cell). **Optional.**

**Files.** kernel/apps.inc, fdlg.inc, files.inc, desk.inc, desksc.inc,
stikmenu.inc, dock.inc, blank.inc, ctrl.inc, toast.inc, clip.inc, ui.inc,
apps/os88api.inc, apps/win16/w16progm.inc.

**Bytes.** kern_small **-16..+4** resident (ui-10, ui-11, files-9, files-10;
on kern_small files-7/8/17 are FDLG.DRV bytes, +12..15). kern_big
**+5..+35** core (ui-10 -30..-40 against about +38..+70 of the rest).
kern_mega 0..20 conv, +5..15 high. Optional: ui-19 +44, ui-12 +10. Modules:
CTRL.DRV +12, SAVER.DRV +15, `.boot2` +10.

**Risk.** Low.

**Rows.** soak `fdlggrey`, `fdlgdrop`, `fdlgup`, `fdlgchoose`, `fdlgsmall`,
`deskitem`, `desksc`, `stikmenu`, `stikregister`, `dispsaver`, `saver`,
`toast`, `toastbar`, `clipkeep`, `dockpos`, `deskdock`, and the WIN16 shortcut
row; `toast` (fast).

**Dependencies.** WP9 for the new cells.

---

## WP28. The Control Panel engine and shared module source

**Goal.** The largest module saving on offer, plus making modules share
their boilerplate as source. 0 resident.

**Findings.**

- `sysmisc-15` [K] **Declarative Control Panel pages, driven by one engine.**
  The static pages are ~150 hand-placed equates, a paint and a click ladder
  each, per-page control painters, and three prose-held invariants:
  probe-before-act, painter id = click id, and one greying predicate (the
  record-phase sysmisc-8). Every page uses the same five species: radio
  group, check, button, drop-down and labelled value. A {species, rect, id,
  getter, setter, predicate} table per page plus a ~500-700-byte engine would
  replace them. **-1,500..-3,000 CTRL.DRV**, ESTIMATED and unprototyped.
  build/ctrl.drv is 15,405 bytes now (the correction), so prototype one page
  first. The driver-page ABI (DSV_CPPAINT/CPCLICK → tables) stays on the
  flag-day list until the engine has proven itself.
- `shell-4` [K]: the dock painter is hand-copied into DOCK.DRV and has
  diverged: dkx_paint banks gfx_hole/wm_clip_n and clears dock_drew; the
  prologues differ. One DOCK_PAINT macro with `%if` arms reproduces both, so a
  fix lands once (0 bytes, if the arms reproduce the divergences).
- `shell-5` [C] with `files-15` [C]: five textual copies of the module far-hop
  (exk_go, wnk_go, smk_go; fdx_go/fdt_go, fcpx_go) and two macro dialects
  (FDBODY/FDX/… and FCPBODY/FCPX/…). Use one kernel/modhop.inc, parameterised
  by the image's section and how DS is reached (0 bytes).
- `files-16` [C]: source shaped around tools that cannot evaluate `%ifdef`.
  FDH_RECT exists only because os88ovlchk files fdlg.inc as `.modd` on both
  builds, and FDENT is spelled `call %1` to hide a word from the scan. Have
  os88ovlchk.py and stkbalance.py consume `nasm -E` per build. That unlocks
  ~-10 `.cold` in fm_brect on kern_big.
- `loader-12` [C]: three clear-prefix ladders, and cmz_cpre's comment names an
  `ld_cpre` that does not exist. Keep two, and fix the comment (0 bytes).

**Files.** kernel/ctrl.inc, dock.inc, dockmod.inc, extmod.inc, winmod.inc,
stikmenu.inc, fdlg.inc, filecp.inc, a new kernel/modhop.inc, compress.inc,
tools/os88ovlchk.py, tools/stkbalance.py.

**Bytes.** 0 resident (-10 `.cold` on kern_big once files-16 lands). CTRL.DRV
**-1,500..-3,000**.

**Risk.** Medium for sysmisc-15 (every page). Low for the rest.

**Rows.** `ovlchk`, `stkbalance` (fast); soak `cppromise`, `cpup`, `cpcost`,
`cpnames`, `cplistrow`, `cpnameshdd`, `dockmodule`, `dockmark`, `dockpos`,
`extdmod`, `fdlg*`, `fcp*`, the Control Panel page rows, and `mega-` twins.

**Dependencies.** None.

---

## WP29. DISKOPS and HIBER modules

**Goal.** The module-side weaknesses, at 0 resident bytes.

**Findings.**

- `hiber-7` [C]: **Esc on the Resume face discards the hibernated session**,
  deleting both files with no confirmation (hiber.inc:1366-1382). In RESUME
  mode, Esc closes the window and keeps the files, as the close box already
  does (+0..4 HIBER.DRV).
- `hiber-12` [C]: the cloner cannot see 8-sector or single-sided disks (160, 180
  and 320KB), the commonest 5150-era disks without a BPB and the case the
  module exists for. Add 8 to the probe ladder, retry with 1 head before 40
  cylinders, and give clo_imgtab a heads column with three rows (+30
  DISKOPS.DRV).
- `hiber-11` [C]: Write Img never checks that the target drive can hold the
  image, so a 1.44MB image to a 720KB drive fails 720 sectors in. Gate the
  same-drive shortcut, and call clo_fits (+10).
- `hiber-20` [C]: DISKCOPY's "copy another" is cheap after a one-trip clone,
  because the whole disk is in the buffer. Keep the claim at CLA_DONE and
  re-arm the target (+40). Verify and format-while-copying stay record.
- `hiber-14` [C]: kern_big wakes with the fresh boot's 8259 masks, kern_mega with
  the image's. Bank 21h/A1h into module data and restore them at wake step 1,
  so there is one model on every kernel (+14 HIBER.DRV). **Do it with the bug
  pass's hiber-1.**
- `hiber-15` [C]: on kern_mega in PM, hbm_arm's room check uses kern_big's image
  size, so a short room passes, sweeps and writes, then fails for the wrong
  reason (+20 kern_mega HIBER.DRV).
- `hiber-13` [K]: ~25 bare `[cs:]` module stores fault through mega_gpw on
  kern_mega in PM, on every repaint among other places. Convert them to
  CSDO/CSEG. That is free on kern_big and a fault per store saved on mega.
- `loader-11` [C]: the compressor's cmz_gamma searches its top bit from bit 15
  down, ~10-15% of compress time ESTIMATED. Search upward, and pass the
  probe's head slot to the fill (~0, DISKOPS.DRV). Take lzcomp's cycle count
  once, if it decides anything.

**Files.** kernel/hiber.inc, hbstub.inc, diskops.inc, compress.inc.

**Bytes.** 0 resident. DISKOPS.DRV about +80, HIBER.DRV +18..+38 (kern_mega's
+20 more).

**Risk.** Low.

**Rows.** soak `hibernate`, `hibernatedrv`, `hibernatem`, `p2hiber`,
`hmahiber`, `megahiber`, `megap2hiber`, `stikhiber`, `win16hiber`,
`diskclone`, `mega-diskclone`, `lzcomp`, `mega-lzcomp`.

**Dependencies.** The bug pass's hiber-1 (it pairs with hiber-14).

---

## WP30. One clock

**Goal.** One time base. The kernel integrates a clock of its own (clk_last,
clk_acc, a per-second carry, and a hibernation catch-up that walks back 32,768
ticks a pass), while DOS, kern_dos and the BIOS use 0040:006C. sch_isr keeps
40:6C current by chaining the ROM every tick.

**Findings.**

- `sysmisc-14` [C]: **derive the time of day from 0040:006C.** Keep only the date
  and the last count seen, derive h:m:s on format, detect midnight as count <
  last, and write the count back via int 1Ah AH=01h. A ~45-byte breakdown
  shared by clk_fmt and osapi_clk_get_x replaces ~55 bytes of accumulator and
  carry (-10..+10 resident, ESTIMATED; prototype). OSAPI_CLK_GET's outputs
  are unchanged. **The caveat [C]:** sched.inc's and fsx.inc's nested-tick
  `.tskip` skips the ROM chain but still bumps [ticks]. Count sch_chskip on
  the target before relying on the ROM's count.
- **It removes the class of three bug-pass items**:
  - sysmisc-1: the clock gains ~31 s/day, because it divides by 18.2 Hz
    instead of 18.2065.
  - sysmisc-3: the user's clock setting never reaches 40:6C, so DOS disagrees
    with the menu bar.
  - the hibernate catch-up walk.

  So sequence with that pass: fix sysmisc-1 now (two immediates), and let R1
  retire the code.
- `sysmisc-4` [C]: the MM58167 New-Year shadow is refreshed only on a Set or a
  roll. Refresh it whenever it differs from [clk_tb_mo], so the failure
  window becomes "off 12+ months" (+8 `.ovl`).
- `sysmisc-5` [C]: an RP5C01 running 12-hour is claimed or refused depending on
  the time of day. Also compare the 12-hour decode when the raw hour is above
  23 (+16 `.ovl`).

**Files.** kernel/clock.inc, clockw.inc, ctrl.inc (the AH=01h writer, in
CTRL.DRV), hiber.inc, mega.inc:1362-1369, sched.inc/fsx.inc (the skip
count).

**Bytes.** kern_small, kern_big and kern_mega **-10..+10** resident; `.ovl`
+24.

**Risk.** Medium: midnight, hibernate, and DOS's own view of time.

**Rows.** soak `deskclock`, `deskclockherc`, `mega-deskclock`,
`pkgmove-deskclock`, `tickzero`, `hibernate`, `kdos` (DOS's time), plus
TESTING.md list item 6's QEMU reboot assertion for the RTC write half.

**Dependencies.** The bug pass's sysmisc-1/2/3.

---

## Order of work

The ranking is value per byte. The order of work also respects dependencies
and keeps the big risk early:

1. **WP1, WP6, WP7, WP10**: free bytes, the boot room, the .bss rule and the
   gates. Low risk; they make the base honest and give the bug pass its boot
   room.
2. **WP17 → WP2**: the shed policy, then module lifetimes, the phase's
   keystone. Then **WP3** and **WP4**.
3. **WP5, WP8, WP9**: kern_dos, fault containment and the API level. WP9
   lands before any WP that adds a cell (WP15, WP16, WP18, WP19, WP23,
   WP27).
4. **WP11, WP12, WP13, WP16**: the measured knobs, then the disk paths, with
   WP12 after the bug pass's dw-5/dw-2.
5. **WP18–WP23, WP26–WP30** in any order, as sessions allow.
6. **WP14, WP24, WP25** run alongside. They are documentation and tooling,
   proven by `cmp` and checkdocs.

Then, once: the deduplicated soak union of every row named above, with the
A/B leg against the starting commit (WP11, WP12, WP16, WP18 and WP20's
measurements are taken there), and `test-full`. No WP runs a soak row or
`test-full` on its own (CLAUDE.md's Testing section).


---

## Appendix A. Every phase-1 finding, and where it went

All 298 "now" findings that are not `bug` and not refuted, by area. "bug pass" is Appendix B.

| id | kind | severity | verdict | goes to | title |
|---|---|---|---|---|---|
| abi-3 | hazard | medium | C | WP10 | t_api_abi decodes only kern_big's table |
| abi-4 | hazard | low | K | WP10 | An N-cell body reads the caller's DS out of api_rn's stack frame at a literal offset |
| abi-5 | hazard | low | C | WP4 | OSAPI_DRV_CALL promises DI back, but neither the kernel nor the driver contract keeps it |
| abi-6 | rethink | high | C | WP9 | No runtime API level: a package cannot ask which cells this kernel has |
| abi-7 | weakness | medium | C | WP19 | Packages write the window record's geometry, which the SDK says they may not |
| abi-8 | weakness | medium | C | WP19 | Rules about which task or context may call a cell are prose, and SPEC admits one has no enforcement |
| abi-10 | weakness | medium | C | WP24 | Contracts are written in up to four places, and the copies have gone stale |
| abi-12 | consistency | low | C | WP26 | Multiplexed doors take their verb in AL, AH or BL |
| abi-14 | consistency | low | K | WP26 | 'ES:SI' in an X cell's contract actually means DS:SI |
| abi-15 | prior-art | low | C | WP19 | No parameter validation, and no build option that adds it |
| boot-2 | hazard | high | K | bug pass | The splash runs code from crossing runs before the canary has verified them, which defeats the canary on kern_small's floppies and kern_big's 1.2MB |
| boot-4 | hazard | low | K | WP6 | The SPL_RESIDENT guard stops four bytes short of the stores it is meant to cover, and the rung is four sectors too big |
| boot-5 | hazard | medium | C | bug pass | A hard-disk boot fetches its own BPB with the AH=08h geometry that boothd deliberately stopped trusting |
| boot-6 | weakness | low | K | WP6 | boothd's RAM check asks the question SPEC 2.7.1 retired, and never asks about the blob |
| boot-7 | size | medium | C | WP6 | Stage 2's copy of the DPT patch is dead code in a blob with 4 bytes free |
| boot-8 | consistency | low | C | WP7 | .bss arrives zeroed by the load, while many comments and SPEC passages say it does not |
| boot-9 | weakness | low | C | WP6 | The three hand-set constants carry their history in comments, and the history is now wrong about the present |
| boot-10 | rethink | low | C | WP6 | Let the build CHOOSE KSIG_OFF and SPL_RESIDENT instead of checking hand-typed values afterwards |
| boot-11 | perf | low | C | WP13 | sch_isr draws a 28.7 ms spinner frame with IF=0 throughout kmain_o |
| boot-13 | consistency | low | C | WP6 | The floppy handoff leaves BX:CX as residue, while two comments promise zeros |
| boot-17 | rethink | low | U | WP6 | Make kmain_o a table rather than a call sequence |
| boot-18 | weakness | low | K | WP6 | Guard 5 omits BLOB_LIFT on every shipped kernel |
| boot-19 | weakness | low | C | WP6 | boot.asm's default constants would build a wrong sector silently |
| bytes-2 | size | medium | K | WP3 | An AT rung: the PS/2 runtime and the XMS bodies, slid over on an 8088 |
| bytes-3 | rethink | medium | C | WP2 | FILECP and FDLG on kern_big as boot-preloaded purgeable images (the COMMAND.COM transient model) |
| bytes-8 | size | low | K | WP4 | The module names' '.DRV' suffix |
| bytes-9 | consistency | low | K | WP24 | KERNEL-MEMORY.md's hand-copied ladder and Task Manager tables are ~9KB stale |
| bytes-10 | consistency | low | C | WP24 | AGENTS.md has lost CLAUDE.md's byte rules |
| dr-1 | hazard | medium | C | bug pass | Boot partition mounted with AH=08h geometry while the volume is addressed with the BPB's |
| dr-2 | hazard | medium | C | WP19 | Batch bracket's 'UI is frozen' assertion is not enforced at a door packages can call |
| dr-3 | weakness | medium | C | WP16 | A failing read-ahead fill runs the full retry ladder, then the direct read runs it again |
| dr-4 | hazard | medium | C | bug pass | Status 06h (media changed) is retried as a generic error and discarded |
| dr-5 | rethink | medium | C | WP16 | OSAPI_FILE_FIND stays quadratic though dsk_dscan already resumes from a raw slot |
| dr-6 | perf | low | U | WP16 | Fixed overhead on every dsk_xfer, cache hits included |
| dr-7 | hazard | low | C | bug pass | A run past the buffer's segment wraps to ES:0000 silently |
| dr-8 | hazard | low | K | bug pass | dsk_rah_fill computes the track phase on the volume-relative LBA, not LBA+base |
| dr-9 | hazard | low | K | bug pass | dsk_find returns a redirected volume's name unterminated when the driver's is 13+ bytes |
| dr-10 | consistency | low | K | WP16 | .bounce's 'no recursion' safety argument contradicts the fill and .img re-entries |
| dr-11 | perf | low | C | WP16 | A listing mount flushes every volume's cached chunks |
| dr-14 | consistency | low | K | WP16 | dsk_fdd_probe clears unit 1's calibrated bit whatever unit it probed |
| dr-16 | consistency | medium | C | WP25 | Documentation drift in this area is a defect of its own |
| dr-18 | rethink | low | U | WP16 | Mount and listing are one routine with quiet flags |
| drivers-2 | hazard | medium | C | bug pass | A driver can be unloaded while another task is inside it, or about to enter it |
| drivers-4 | hazard | medium | C | bug pass | snd_release_inst calls DSV_RELINST at IF=0 into code that makes kernel calls |
| drivers-5 | weakness | low | K | WP4 | 'PRESERVE BX' is a driver rule the kernel could enforce at almost no resident cost |
| drivers-6 | hazard | medium | C | WP22 | A driver image is freed with no check that its IRQ vectors were unhooked |
| drivers-7 | weakness | low | C | WP4 | drv_vol_bank keeps one static pair, and the bracket is known to nest |
| drivers-10 | size | low | U | WP4 | Driver file names are resident although every class-row loader is an image |
| drivers-11 | consistency | low | C | WP1 | Dead %else arms and kern_small prose inside the OS88_DRIVERS gate |
| drivers-12 | weakness | low | C | WP1 | drv_boot_x writes CH and nothing reads it |
| drivers-13 | perf | low | C | WP4 | Every sound dispatch pays a .text-to-.cold far hop |
| drivers-14 | consistency | low | C | WP4 | OSAPI_FSX_SPK skips the 34.9 stream interlock that its close announces |
| drivers-16 | weakness | low | C | WP10 | The private ABIs (VX/XM/HD) are versioned by hand and are not covered by the ABI lock |
| dw-4 | hazard | medium | C | WP19 | The UI-task-only rule for every file cell is enforced by nothing |
| dw-5 | hazard | low | C | bug pass | A failed FAT flush keeps its dirt, which dsk_fatw_park may later write to another disk |
| dw-6 | hazard | low | C | bug pass | WRITE_AT's INSIDE arm writes up to 511 bytes past the caller's buffer onto disk slack |
| dw-9 | consistency | low | C | WP1 | dskw_find matches the volume label, so its name cannot be used |
| dw-11 | weakness | low | C | WP16 | Short names with bytes 80h-FFh, or a leading 05h, are listed but cannot be opened, deleted or renamed |
| dw-12 | weakness | medium | K | WP16 | Subdirectories never grow: a kernel-made folder on a 512-byte-cluster volume holds 14 entries |
| dw-13 | perf | medium | K | WP12 | A whole-file write discovers FERR_FULL only after writing everything it could |
| dw-14 | rethink | medium | C | WP12 | Allocate a contiguous run when the size is known |
| dw-15 | rethink | medium | C | WP12 | Defer the post-commit FAT flush (frees only) to gfx_unlock, the gate held streams already use |
| dw-16 | perf | low | C | WP12 | The rover resets at every navigation, and FAT12 allocation is three nested calls a cluster |
| dw-17 | perf | low | C | WP12 | rmtree finds every file twice |
| dw-20 | consistency | low | C | WP12 | Pop runs and refusal tails shared across 1,000+ lines, held together by prose |
| dw-21 | consistency | low | C | WP16 | Two 'same question, two predicates' pairs: LFN detection and mounted-image detection |
| engsys-2 | hazard | high | C | WP10 | A citation that resolves to the WRONG section passes: six §46 cites mean §47 |
| engsys-3 | weakness | high | C | WP10 | SPEC.md's hand copies of code are ungated and wrong |
| engsys-4 | weakness | high | K | WP24 | SPEC's front matter describes retired designs as current |
| engsys-5 | weakness | medium | C | WP24 | Two copies of the slot contracts, disagreeing in both directions |
| engsys-6 | weakness | medium | C | WP10 | Running counts in contract prose rot |
| engsys-7 | weakness | medium | K | WP10 | Tier admission rules are prose and are broken by about 17 fast rows and 4 full rows |
| engsys-8 | hazard | medium | K | WP10 | Basename collisions silently disable citation checks |
| engsys-9 | weakness | medium | C | WP10 | checkdocs does not read .c, .h, .txt or .sh |
| engsys-10 | weakness | medium | C | WP24 | AGENTS.md has diverged from CLAUDE.md by 726 lines |
| engsys-11 | weakness | medium | C | WP10 | The C SDK's constants and thunks are outside both t_mirror and the ABI lock |
| engsys-12 | hazard | low | C | WP10 | Section switch-back is gated only for nobits sections |
| engsys-13 | weakness | low | U | WP10 | Ratchets are exact-match registers, not one-way; 21 bespoke parsers |
| engsys-14 | weakness | low | C | WP10 | Gates regex NASM source for facts the assembler already emits |
| engsys-15 | hazard | low | C | WP10 | The whole-soak permission refusal is bypassed by -k '*' |
| engsys-16 | weakness | low | C | WP10 | builds= is judged per script, not per mode |
| engsys-18 | rethink | medium | C | WP24 | SPEC.md is two documents: 56% of it is package design notes |
| engsys-19 | rethink | medium | C | WP14 | Machine-readable routine contracts (register discipline is ungated) |
| engsys-20 | prior-art | low | U | WP19 | A checked build at the API boundary |
| engsys-21 | weakness | low | C | WP24 | The Makefile is one 20,872-line file, 53% comments |
| files-4 | hazard | low | C | bug pass | The single purge-floor word is clobbered by a paste and lifted by anyone |
| files-5 | perf | low | K | WP16 | fm_measure stages every entry through a 24-byte far copy on every listing |
| files-6 | hazard | low | C | bug pass | fmv_owner's 'cannot happen' fallback tags the cache as the menu save-under |
| files-7 | weakness | low | C | WP27 | The commit's volume put-back ignores a failed mount |
| files-8 | weakness | low | C | WP27 | Opening a chooser ends other windows' prompts without repainting them |
| files-9 | weakness | low | C | WP27 | Modality filters presses, not drops |
| files-10 | weakness | low | C | WP27 | kern_small keeps a closed window's 2KB cache claimed |
| files-11 | perf | low | K | WP1 | The empty-icon test ORs 64 bytes per row |
| files-13 | hazard | low | C | bug pass | Any volume's ASSOC.DAT can exhaust the association slots for the session |
| files-15 | consistency | low | C | WP28 | Three copies of the module far-hop and two macro vocabularies |
| files-16 | rethink | medium | C | WP28 | Source shaped around tools that cannot evaluate %ifdef |
| files-17 | rethink | low | U | WP27 | The completion callback could carry the folder |
| gfx-2 | hazard | medium | C | bug pass | The planar decoder's deliberate over-read can #GP on kern_mega |
| gfx-3 | perf | medium | K | WP1 | gfx_blit1 far-calls the clip-region question twice per band |
| gfx-4 | perf | medium | C | WP20 | gfx_points has no planar path: every VGA point is a whole gfx_fill |
| gfx-5 | consistency | medium | C | WP26 | [gfx_color] after gfx_blit4 depends on which path drew |
| gfx-6 | weakness | medium | C | WP11 | Built-and-off knob bodies double the hot code without shipping its wins |
| gfx-7 | hazard | low | C | WP10 | The planar bodies hard-code an 80-byte stride |
| gfx-8 | perf | low | U | WP20 | A right-edge clip forfeits both blit decoders |
| gfx-9 | perf | low | C | WP20 | The shipped 1bpp XOR interior is a byte loop |
| gfx-10 | consistency | low | K | WP10 | gfx_lock's cli...sti is outside CLAUDE.md's short form of the IF rule |
| gfx-11 | consistency | low | C | WP20 | vga12.inc is not the module §4 describes |
| gfx-12 | weakness | medium | C | WP25 | Comments as archaeology bury the current contract |
| gfx-13 | rethink | medium | C | WP20 | 'Asked the depth before entering the display' is a recurring bug class |
| gfx-15 | rethink | low | C | WP20 | gfx_clip_run tests every fragment, with no early exit |
| hiber-5 | hazard | low | K | bug pass | Nothing fences heap compaction or shedding during the progressive image write |
| hiber-6 | hazard | low | C | bug pass | Resume stub's int 13h calls run through kbm_i15 in the kernel image being overwritten |
| hiber-7 | weakness | medium | C | WP29 | Esc on the Resume face discards the hibernated session |
| hiber-11 | weakness | low | C | WP29 | Write Img never checks that the target drive can hold the image |
| hiber-12 | weakness | medium | C | WP29 | The cloner cannot see 8-sector or single-sided (DOS 1.x, no-BPB) disks |
| hiber-13 | consistency | low | K | WP29 | About 22 bare [cs:] writes sit beside the module's CSDO/CSEG idiom |
| hiber-14 | rethink | low | C | WP29 | kern_big wakes with the fresh boot's masks; kern_mega with the image's |
| hiber-15 | weakness | low | C | WP29 | hbm_arm's room check uses kern_big's image size on kern_mega in protected mode |
| hiber-16 | hazard | low | C | bug pass | Two non-atomic BDA tick reads |
| hiber-17 | size | low | C | WP7 | hb_probe_x zeroes three .bss words that arrive zeroed |
| hiber-20 | prior-art | low | C | WP29 | What DISKCOPY did that the cloner does not: write another copy, verify, format while copying |
| histsys-1 | hazard | high | C | bug pass | A dying worker tears down and repaints other windows on its own 128/192-byte stack slice |
| histsys-2 | hazard | medium | K | WP8 | No int 0 (divide fault) handler on kern_big/kern_small: a 286+ hangs, an 8088 continues with garbage |
| histsys-3 | perf | medium | C | WP12 | The free-cluster count is recomputed (and on FAT16 re-read) on every DFREE, VOL_STAT and mount |
| histsys-4 | weakness | low | K | WP18 | No scheduling preference for the UI task: input waits up to N worker quanta |
| histsys-5 | rethink | medium | C | WP18 | Every int 13h freezes every task for ~400 ms |
| histsys-6 | weakness | low | C | WP22 | Keyboard is polled outside the event ring, so key/mouse order is lost across a pass |
| histsys-7 | size | medium | K | WP4 | Seven 36-byte copies of a union driver service table in resident .bss |
| histsys-11 | weakness | low | C | WP19 | OSAPI_WM_WAKE writes through an unvalidated BX from ISR and worker context |
| histsys-15 | hazard | low | K | WP26 | OSAPI_TASK_SLEEP with AX >= 0x8002 returns at the next tick |
| histsys-16 | consistency | low | C | WP7 | Two contradictory rules about .bss at boot |
| histsys-17 | weakness | low | C | WP26 | The implicit display context is eight scattered bytes reset one by one |
| hm-2 | hazard | low | K | WP26 | The owner fence's self-match lets OSAPI_MEM_FREE and _REGROW act on the caller's own live REGION, and the damage lands on others |
| hm-3 | hazard | medium | C | WP2 | DESK.DRV is dropped on a dying worker while the UI task may be executing inside it |
| hm-4 | hazard | low | K | bug pass | MEM_OWNER_MAX also caps the kernel's tags, and every driver image shares MEM_K_DRV |
| hm-5 | perf | low | C | WP13 | The O(n^2) planning walks run with IF=0, where [sch_lock] would give the same exclusion |
| hm-7 | hazard | medium | C | WP15 | A lazy part is re-fetched by 8.3 name with no check that the file is still the one launched |
| hm-8 | perf | low | C | WP17 | A claim refused for lack of a free RECORD runs a full compaction before the shed that would free one |
| hm-9 | rethink | medium | K | WP2 | Package purgeable claims, delivered through the existing MC_RLOC proc with DX = 0 |
| hm-11 | size | medium | K | WP15 | The parts loader stays resident in every non-loader parts package, once per instance |
| hm-12 | weakness | low | C | WP15 | MEM_OWNER_MAX = 8 is the most-negotiated constant in the package tree, and there is no shared sub-allocator |
| hm-13 | weakness | low | C | bug pass | The claim floor is stamped per TASK while every package callback runs on task 0, and any teardown lifts it |
| hm-14 | consistency | low | C | WP19 | OSAPI_MEM_P treats any unknown verb as MEMP_AVAIL and returns the caller's own CF |
| input-2 | hazard | medium | C | bug pass | The tick can park a ROM int 09h with IRQ1 still in service, on the paths SPEC 9.8.3 left unmasked |
| input-4 | hazard | low | K | bug pass | The serial lease trusts its caller to pin the region the ISR writes into |
| input-5 | hazard | low | C | WP13 | Shift+PrtSc runs the ROM's Print Screen inside kbm_isr's IRQ mask |
| input-9 | hazard | low | C | bug pass | mou_p2_off writes the 8042 on the UI task with nothing excluding int 09h |
| input-10 | weakness | low | C | WP22 | PS/2 sync rests on byte 0's bit 3 alone |
| input-11 | hazard | low | C | WP22 | The hot-plug poller drops DTR on a modem port every 3 s for the whole session |
| input-12 | size | medium | C | WP3 | The keyboard mouse is about 456 resident bytes on every machine and qualifies as an on-demand module |
| input-16 | consistency | low | C | WP25 | mou_claim no longer exists, and 22 references remain |
| isr-1 | consistency | low | K | WP13 | Pause stops the BIOS tick count on an XT; SPEC chose that but does not say the time of day falls behind |
| isr-2 | hazard | medium | K | bug pass | osapi_mouse_feed is reached from an IRQ handler but swaps SP and not SS |
| isr-3 | hazard | low | C | WP26 | OSAPI_WM_WAKE's 'ISR-safe' has an unstated SS = LOW_SEG precondition |
| isr-4 | hazard | low | C | WP19 | DF is never cleared at the IRQ0 callback boundary |
| isr-6 | hazard | low | K | WP22 | IMR read-modify-writes at IF=1 on task context |
| isr-7 | hazard | medium | K | WP13 | The cursor is drawn at IF=0 inside the serial ISR, which caps SERIAL.DRV's usable rate on an 8250 |
| isr-8 | weakness | medium | C | WP8 | CPU exceptions are left to the BIOS dummy handler (real-mode kernels) |
| isr-9 | hazard | low | C | WP18 | SCH_CHSTK is sized without int 1Ch, and the mechanism SPEC gives is the wrong way round |
| isr-10 | consistency | low | C | WP13 | SPEC names the wrong 'longest IF=0 window' |
| isr-11 | consistency | low | C | WP4 | DSV_TICK's environment is documented as the kernel's, and it is not |
| isr-12 | consistency | low | K | WP10 | Bare cli/sti sites are argued in place, and CLAUDE.md's short form of the rule reads as absolute |
| isr-13 | rethink | medium | C | WP22 | One owner for IRQ lines, the IVT and the IMR |
| isr-14 | rethink | medium | C | WP13 | A written interrupt-latency budget, and a ratchet that enforces it |
| isr-16 | weakness | low | U | WP24 | No single 'callable at interrupt time' list |
| isr-17 | consistency | low | C | WP22 | The SB driver leaves the master PIC reading its ISR |
| loader-5 | hazard | medium | C | WP19 | 'No nested start' and 'UI task only' for OSAPI_PKG_START are prose, not fences |
| loader-6 | prior-art | medium | K | WP9 | No load-time refusal for a package needing newer cells at the same PKG_FMT |
| loader-8 | weakness | low | C | WP1 | ld_unreserve is a drifted twin of mem_free_rec (it misses the page-floor lift) |
| loader-10 | hazard | low | C | WP26 | OSAPI_PKG_START's caller image must be pinned across the call; the contract does not say so |
| loader-11 | perf | low | C | WP29 | Compressor's gamma searches its top bit from bit 15 down |
| loader-12 | consistency | low | C | WP28 | Three clear-prefix ladders; the module's comment points at a routine that does not exist |
| mc-2 | hazard | medium | C | bug pass | An option-ROM IRQ on an unlent foreign stack is reflected with a selector as the real-mode SS |
| mc-4 | hazard | medium | C | WP13 | Linear LDT walks with interrupts off, one per block on a teardown |
| mc-9 | hazard | medium | K | WP5 | Global GDT scratch selectors held by prose; the compactor rebases SLD but not BACK |
| mc-10 | weakness | medium | C | WP10 | The kernel's own writes through CS are completed silently |
| mc-11 | consistency | low | C | WP1 | The write-only CMOS index port is read back |
| mc-12 | hazard | low | C | bug pass | mega_s2segc truncates an off-grid base |
| mc-13 | weakness | low | C | WP5 | mega_ustab rows are never freed and can name a moved claim |
| mc-14 | weakness | low | C | WP5 | MX_FWIN is set but never cleared |
| mc-15 | hazard | low | K | bug pass | mega_leds spins up to 2x65,536 polls inside IRQ1 with IF off; an ACK behind an AUX byte arrives as a scancode |
| mc-16 | rethink | medium | K | WP5 | Move the master PIC off 20h; the reflection path never re-initialises it, though several sites hard-code 20h-27h |
| mc-19 | prior-art | low | U | WP5 | A 'protected-mode clean' header bit as the incremental road to L3 |
| md-4 | hazard | low | K | bug pass | The snapshot's fallback can build a prefix park below MPK_MIN_KB |
| md-5 | weakness | medium | C | WP10 | t_mirror does not scan kerndos/, so the kernel-to-kern_dos mirrors are unwatched |
| md-6 | size | medium | K | WP5 | Both kern_mega kern_dos images carry the live resume they can never run |
| md-7 | size | low | C | WP5 | kd_glb, a 512-byte gate-only buffer, sits in every shipped kern_dos's .bss |
| md-8 | weakness | low | C | WP5 | KDL_PLOW is written, documented as binding, and read by nobody |
| md-9 | hazard | low | C | bug pass | mega_d8042get can bank a keystroke as the 8042 command byte |
| md-10 | hazard | low | C | bug pass | kd_unpark and mr_a20 wait only 512 probes for an 8042 A20 gate |
| md-11 | hazard | low | C | bug pass | The park stub's 'press a key' accepts a PS/2 mouse byte |
| md-12 | weakness | low | C | WP10 | mega_dreg_add silently drops a region on overflow |
| md-13 | size | low | C | WP5 | Ten kdback.inc doors are call X / ret around routines with the same contract |
| md-14 | size | low | C | WP5 | mem_free_x is a no-op behind a compare, and its comment says otherwise |
| md-15 | rethink | low | U | WP5 | Check the park header before jumping into it |
| md-16 | hazard | low | C | bug pass | mem_claim_x's overflow test is the last bit shifted out |
| md-19 | size | low | K | WP5 | HIBER.DRV-only scratch is resident in kern_mega's conventional .text |
| md-20 | consistency | low | C | WP5 | KDLF_RAHSH is a shift count named like a flag bit |
| md-21 | consistency | low | U | WP5 | The A20 code is written out six times with four different budgets |
| md-22 | consistency | low | C | WP5 | kdlayout.inc is a changelog with a definition file inside it |
| mem-3 | weakness | medium | C | WP17 | A doomed claim sheds every cache and may park workers once per shed |
| mem-6 | hazard | low | K | WP26 | The self-region widening in mem_find_own also lets a package FREE or REGROW the region it is running in |
| mem-7 | hazard | medium | C | WP22 | Nothing bounds the interrupt context any more: blocks an ISR uses move at IF=1 unless they are driver images |
| mem-8 | perf | medium | C | WP13 | O(MEM_MAX^2) walks run with interrupts off (mem_avail, mem_hifit, mem_claim_1) |
| mem-9 | weakness | low | K | WP17 | Regrow path 3 puts every grower at the top, whatever door it came in by |
| mem-10 | hazard | low | C | WP2 | mod_drop on a worker can free a module the UI task is executing; and the same missing fact pins every module for life |
| mem-11 | weakness | low | C | WP13 | The driver-image IF=0 move's latency note is wrong in both directions |
| mem-12 | consistency | medium | C | WP25 | Orphaned headers of inlined routines are where this file's documentation rots |
| mem-14 | rethink | medium | C | WP17 | Keep mem_tab sorted by base: every walk becomes linear and best fit is free |
| mem-15 | rethink | medium | C | WP2 | Only the kernel can own a cache: no purgeable class for package claims |
| mem-17 | rethink | low | K | WP15 | Region movability by package-header bit instead of 72 call sites |
| mem-19 | hazard | low | K | bug pass | kern_mega osapi_mem_regrow_x's move-down copies the OLD size into the NEW claim |
| sched-1 | hazard | medium | C | WP13 | task_spawn holds IF=0 across the whole 0xCC slice fill |
| sched-3 | hazard | low | C | WP18 | sch_lock has become a general Forbid, and yield/sleep/gfx_lock silently degrade under it |
| sched-4 | hazard | low | C | WP26 | OSAPI_TASK_SLEEP on the UI task ends at the next input, undocumented |
| sched-5 | hazard | low | K | WP26 | task_sleep's range is 1..32768 and unstated |
| sched-6 | hazard | low | C | WP18 | evq_push wakes the UI task before the record is in the ring |
| sched-7 | hazard | low | K | WP18 | The two private ISR stacks have no canary and sit back to back |
| sched-10 | weakness | medium | C | WP25 | Comment rot is the area's real defect |
| sched-11 | consistency | low | K | WP18 | fsx_rtick is a third copy of the chain swap and the hook guard |
| sched-12 | weakness | low | C | WP7 | sched_init's zero stores rest on an unresolved contradiction about .bss |
| sched-13 | perf | low | C | WP18 | evq_pop banks DS for a prefix hazard that cannot occur |
| sched-14 | size | low | C | WP1 | sch_clsbytes row 0 duplicates row 4 |
| sched-15 | rethink | medium | C | WP18 | A task can block only on time - there is no wait-on-anything primitive |
| sched-16 | rethink | low | C | WP18 | A UI wake gets no priority over ready workers |
| shell-1 | hazard | medium | C | WP2 | The Sticky App library can be freed under a widget that has bound it |
| shell-2 | weakness | medium | K | WP2 | Interactive shell paths load and drop modules the shell itself calls too slow |
| shell-3 | rethink | medium | C | WP27 | OSAPI_DESK_ITEM remove is keyed by a zone that the API says is not an identity |
| shell-4 | consistency | low | K | WP28 | The dock painter is hand-copied into DOCK.DRV |
| shell-5 | consistency | low | C | WP28 | Three textual copies of the far-frame helper |
| shell-6 | hazard | low | C | WP10 | Locator File menu item 1 is poked by raw offset from five sites |
| shell-7 | weakness | low | C | WP27 | STIKMENU.DRV writes the kernel's service-item record |
| shell-8 | weakness | low | K | WP27 | Boot reader keeps up to 1 KB more shortcut claim than the rows need |
| shell-9 | perf | low | C | WP20 | Every damage pass recomputes every shown cell's rect with div and two muls |
| shell-10 | perf | low | C | WP20 | WIN95.DRV phases button art one bit at a time |
| shell-11 | hazard | low | C | WP27 | Basic dock_geom only works at boot or with CTRL.DRV loaded |
| shell-12 | consistency | low | K | WP27 | A driver's service record is half-sanitised |
| sysmisc-4 | hazard | low | C | WP30 | MM58167 New-Year shadow refreshed only on a Set or a roll, not on each boot |
| sysmisc-5 | weakness | low | C | WP30 | RP5C01 claimed or refused depending on the time of day when the chip runs 12-hour |
| sysmisc-6 | hazard | low | C | WP27 | Screen Saver settings window outlives the Control Panel; its Customize sibling does not |
| sysmisc-7 | hazard | low | C | bug pass | Unloading VESA.DRV redraws the Drivers pane at a stale origin after vid_switch |
| sysmisc-9 | perf | low | K | WP11 | kern_big's saver wake and fsx exit relight is a full wm_paint_all; the damage-pass version is kern_mega-only |
| sysmisc-10 | consistency | low | C | WP7 | Dead boot-overlay stores and comments resting on a false '.bss is not zeroed' |
| sysmisc-12 | consistency | low | K | WP25 | W_FLAGS 'visible' bit is the literal 2 at about thirty sites |
| sysmisc-14 | rethink | medium | C | WP30 | R1: one clock - derive the time of day from 0040:006C |
| sysmisc-15 | rethink | medium | K | WP28 | R2: declarative Control Panel pages driven by one engine |
| text-1 | hazard | medium | C | bug pass | fpg_bare banks [wm_clip_n] but not [gfx_hole] |
| text-2 | size | medium | C | WP1 | font_ink's VGA early-out and [gfx_mono1] test are unreachable |
| text-3 | hazard | low | C | WP26 | font_run_x leaves [gfx_color] path-dependent and the contract is silent |
| text-4 | hazard | low | C | bug pass | OSAPI_FONT_GLYPHS can return the seam cut's borrowed table |
| text-5 | hazard | low | C | bug pass | Menu layout measures package strings with an unbounded font_width_x |
| text-6 | consistency | low | C | WP10 | fprog's one-word-store adjacencies are only half asserted |
| text-7 | size | low | K | WP3 | font_run_x's once-per-run VGA prologue is resident on mono machines |
| text-8 | size | low | C | WP1 | [vid_popmax] is a constant held in a memory word |
| text-9 | perf | low | C | WP20 | VGA ico_pass keeps its per-word column bookkeeping in memory |
| text-11 | hazard | low | C | bug pass | An empty volume keeps a stale ASSOC.DAT |
| text-13 | consistency | low | C | WP1 | Two parity nops under complementary %ifdefs are one unconditional nop |
| text-14 | weakness | medium | C | WP25 | Routine headers carry history, and the history goes stale |
| text-15 | rethink | medium | C | WP15 | Kernel-synthesised BOLD in font_run |
| text-16 | rethink | low | K | WP15 | Real separator and check-mark item markers |
| ui-2 | hazard | medium | K | WP19 | Content-click, right-click and W_ONDRAG read the handler before gfx_lock and never re-test liveness (SPEC.md 13.9.2's race, unfixed elsewhere) |
| ui-3 | hazard | low | C | WP1 | ui_dispatch decides 'is this Locator' by reading AM_ONCMD = 0 in the owner's segment without the lock |
| ui-4 | hazard | low | C | WP10 | ui_cmd's jump table has no length assert against CMD_LAST |
| ui-7 | consistency | low | K | WP19 | DF is defended in toast_show but trusted in clip_put, the slot whose failure corrupts the kernel heap |
| ui-8 | hazard | medium | C | WP26 | The SDK and SPEC.md 55.4 tell authors a worker may call the clipboard; the code says it must not |
| ui-9 | weakness | low | C | WP26 | 'The kernel guarantees delivery' of W_ONMOUSEUP is false when the ring dropped the release |
| ui-10 | size | low | K | WP27 | About's fourth line from [vid_w]/[vid_h] instead of a per-kind string table |
| ui-11 | prior-art | low | C | WP27 | One user-settable double-click interval for the whole machine |
| ui-12 | prior-art | low | U | WP27 | A clipboard generation count |
| ui-15 | weakness | medium | K | WP25 | History in comments is where the drift lives |
| ui-16 | consistency | low | K | WP27 | Under TOAST_Q, one raiser's retire takes down another raiser's message |
| ui-17 | hazard | low | K | bug pass | ui_sys_open indexes fm_stattab without the bound toast_say exists to carry |
| ui-19 | rethink | low | U | WP27 | A menu 'about to drop' moment for packages |
| video-2 | hazard | high | C | WP23 | A plain MDA primary is programmed with Hercules graphics 6845 timings |
| video-4 | weakness | medium | C | WP23 | A 64KB EGA is driven planar although AH=12h's BL already says 64KB |
| video-6 | hazard | medium | C | WP23 | The planar blank (AC 12h) is used for VID_VESA, whose packed and direct-colour pipeline likely bypasses the Attribute Controller (PLAUSIBLE) |
| video-7 | size | medium | K | WP1 | kern_small carries about 85 resident bytes of fsx tables and code for VGA modes it can never set |
| video-8 | hazard | low | C | WP23 | vid_apply builds and arms the row table on VID_VESA, where the mouse stack is allowed to overwrite it |
| video-12 | weakness | low | C | WP23 | FSI_PAGES advertises pages that fsx_page refuses; the grant itself is not published |
| video-13 | weakness | low | C | WP23 | EGA's caps row omits mode 0Dh, which every EGA has |
| video-14 | consistency | low | C | WP23 | The kind-class idiom is hand-spelled at about ten sites |
| video-15 | weakness | medium | C | WP25 | Comments narrate history, and about 40 statements in this area have gone stale |
| wm-1 | hazard | high | K | WP8 | wm_ptr2idx raises INT 0 on a wild window pointer; its comment says it is bounded |
| wm-R1 | rethink | medium | C | WP21 | Exact clipping in the damage pass, dropping the painter's-algorithm closure |
| wm-2 | size | medium | K | WP8 | Store the slot index in the record so wm_ptr2idx is a load, not a div |
| wm-4 | hazard | medium | C | WP21 | wm_ask_size trusts a W_ONSIZE negotiator to keep BX, which no contract says |
| wm-6 | consistency | medium | C | WP21 | wm_resize clamps against the union width and ignores the side-dock band |
| wm-7 | consistency | low | K | WP21 | OSAPI_WM_DESTROY and OSAPI_WM_FULLSCREEN draw without taking the gfx lock |
| wm-8 | hazard | low | C | WP26 | OSAPI_WM_TIMER with AX >= 32768 fires at once |
| wm-9 | weakness | low | K | WP21 | wm_su_drop ignores mem_free's refusal and loses the claim |
| wm-10 | size | low | C | WP1 | wm_lk's gfx_lock_flag test is redundant |
| wm-11 | weakness | medium | C | WP11 | The XT does not get the redraw knobs built and measured for it |
| wm-12 | rethink | medium | K | WP21 | Transitive marking redraws windows that were never damaged |
| wm-13 | consistency | medium | C | WP19 | Window-pointer validation is inconsistent across the wm doors |
| wm-14 | weakness | low | C | WP1 | wm_dmg_bands and the natural-bank banner exist twice, with dead inner arms |
| wm-15 | weakness | medium | C | WP25 | wm.inc narrates its own history and the narration goes stale |
| wm-16 | weakness | low | K | WP15 | The content white fill is still the default double-draw |
| wm-17 | consistency | low | C | WP21 | One-shot arguments to the damage pass are globals with many lifetimes |

---

## Appendix B. Routed to the bug-fix pass

These are phase-1 hazards (one is a weakness) whose remedy is a local correctness fix, so the other synthesist owns them. They are listed so that nothing falls between the two documents. Where a WP above depends on one, the WP says so (boot-2 and boot-5/dr-1 need WP6's room; dw-5 must precede WP12; histsys-1 shares WP2's post-to-UI mechanism; hm-13 goes with the bug pass's mem-4 and files-4 as one purge-floor fix; input-2 and isr-2 share files with WP22). The 66 findings of kind `bug` are not repeated here.

| id | kind | severity | verdict | bytes (as estimated) | title |
|---|---|---|---|---|---|
| boot-2 | hazard | high | K | +8 blob (.boot2, transient); needs boot-7's room on kern_big | The splash runs code from crossing runs before the canary has verified them, which defeats the canary on kern_small's floppies and kern_big's 1.2MB |
| boot-5 | hazard | medium | C | +8 boothd (boot sector); ~-20 .ovl (transient) | A hard-disk boot fetches its own BPB with the AH=08h geometry that boothd deliberately stopped trusting |
| dr-1 | hazard | medium | C | +8 boothd; +6 .ovl now, -28 .ovl once AH=08h goes; 0 resident | Boot partition mounted with AH=08h geometry while the volume is addressed with the BPB's |
| dr-4 | hazard | medium | C | +16 .cold | Status 06h (media changed) is retried as a generic error and discarded |
| dr-7 | hazard | low | C | +6 .cold | A run past the buffer's segment wraps to ES:0000 silently |
| dr-8 | hazard | low | K | ~+14 .cold | dsk_rah_fill computes the track phase on the volume-relative LBA, not LBA+base |
| dr-9 | hazard | low | K | +5 .cold | dsk_find returns a redirected volume's name unterminated when the driver's is 13+ bytes |
| drivers-2 | hazard | medium | C | +~12 .cold (coarse form) | A driver can be unloaded while another task is inside it, or about to enter it |
| drivers-4 | hazard | medium | C | 0 (reorder) | snd_release_inst calls DSV_RELINST at IF=0 into code that makes kernel calls |
| dw-5 | hazard | low | C | +3 .cold resident | A failed FAT flush keeps its dirt, which dsk_fatw_park may later write to another disk |
| dw-6 | hazard | low | C | +25 .cold resident kern_big (kern_small has no INSIDE arm) | WRITE_AT's INSIDE arm writes up to 511 bytes past the caller's buffer onto disk slack |
| files-4 | hazard | low | C | ~+6 at each of three sites, .cold | The single purge-floor word is clobbered by a paste and lifted by anyone |
| files-6 | hazard | low | C | 0 | fmv_owner's 'cannot happen' fallback tags the cache as the menu save-under |
| files-13 | hazard | low | C | ~+10 .cold kern_big | Any volume's ASSOC.DAT can exhaust the association slots for the session |
| gfx-2 | hazard | medium | C | +~20 .text kern_mega only (0 on kern_big/kern_small) | The planar decoder's deliberate over-read can #GP on kern_mega |
| hiber-5 | hazard | low | K | +10 HIBER.DRV, 0 resident | Nothing fences heap compaction or shedding during the progressive image write |
| hiber-6 | hazard | low | C | +6 HIBER.DRV | Resume stub's int 13h calls run through kbm_i15 in the kernel image being overwritten |
| hiber-16 | hazard | low | C | +6 HIBER.DRV | Two non-atomic BDA tick reads |
| histsys-1 | hazard | high | C | +15 to +30 .text/.cold | A dying worker tears down and repaints other windows on its own 128/192-byte stack slice |
| hm-4 | hazard | low | K | +5 .cold resident | MEM_OWNER_MAX also caps the kernel's tags, and every driver image shares MEM_K_DRV |
| hm-13 | weakness | low | C | +6 to +8 .cold resident | The claim floor is stamped per TASK while every package callback runs on task 0, and any teardown lifts it |
| input-2 | hazard | medium | C | +~14-16 .text resident (sched.inc), all kernels; ~40 cycles on the switch path only, ESTIMATED | The tick can park a ROM int 09h with IRQ1 still in service, on the paths SPEC 9.8.3 left unmasked |
| input-4 | hazard | low | K | (a) +~20 SERIAL.DRV module, 0 resident | The serial lease trusts its caller to pin the region the ISR writes into |
| input-9 | hazard | low | C | +~12 .text resident kern_big | mou_p2_off writes the 8042 on the UI task with nothing excluding int 09h |
| isr-2 | hazard | medium | K | about +13 .text resident (kern_big, OS88_DRIVERS): 4 + 5 + 4 bytes of SS bank/load/restore | osapi_mouse_feed is reached from an IRQ handler but swaps SP and not SS |
| mc-2 | hazard | medium | C | (a) +20 .mega, +128..192 .lowbss resident conventional (owner's call); (c) 0 | An option-ROM IRQ on an unlent foreign stack is reflected with a selector as the real-mode SS |
| mc-12 | hazard | low | C | +4 .cold | mega_s2segc truncates an off-grid base |
| mc-15 | hazard | low | K | +4 .mega | mega_leds spins up to 2x65,536 polls inside IRQ1 with IF off; an ACK behind an AUX byte arrives as a scancode |
| md-4 | hazard | low | K | +8 .mega (above 1MB) | The snapshot's fallback can build a prefix park below MPK_MIN_KB |
| md-9 | hazard | low | C | +5 .mega | mega_d8042get can bank a keystroke as the 8042 command byte |
| md-10 | hazard | low | C | -1 HMA kern_dos, -1 HIBER.DRV | kd_unpark and mr_a20 wait only 512 probes for an 8042 A20 gate |
| md-11 | hazard | low | C | +4 HIBER.DRV | The park stub's 'press a key' accepts a PS/2 mouse byte |
| md-16 | hazard | low | C | +1 to +4 | mem_claim_x's overflow test is the last bit shifted out |
| mem-19 | hazard | low | K | +4 .cold (kern_mega) | kern_mega osapi_mem_regrow_x's move-down copies the OLD size into the NEW claim |
| sysmisc-7 | hazard | low | C | -1 to +2 CTRL.DRV | Unloading VESA.DRV redraws the Drivers pane at a stale origin after vid_switch |
| text-1 | hazard | medium | C | +13 .text kern_big only | fpg_bare banks [wm_clip_n] but not [gfx_hole] |
| text-4 | hazard | low | C | +4 resident .bss, +~6 in each writer (.boot2/HIBER.DRV/.cold) | OSAPI_FONT_GLYPHS can return the seam cut's borrowed table |
| text-5 | hazard | low | C | +4 to +8 .text both kernels | Menu layout measures package strings with an unbounded font_width_x |
| text-11 | hazard | low | C | +~10 FORMAT.DRV, 0 resident | An empty volume keeps a stale ASSOC.DAT |
| ui-17 | hazard | low | K | +4 (an in-line bound) or ~+6 (the toast_say route) .text resident | ui_sys_open indexes fm_stattab without the bound toast_say exists to carry |

---

## Appendix C. Record and flag-day findings that bear on a phase-1 decision

These are not in any WP or in the totals. Each is listed because a phase-1 WP
either is its no-break precursor, or would be decided differently with it in
view.

| id | phase | why it matters here |
|---|---|---|
| video-17 [K] | record | The mono renderer is resident on VGA-only kern_big: **-1,100..-1,500 on the gauge** (sw_* is 1,108 bytes measured). After WP2 it is the largest resident lever left. It is WP3's mirror image, and it needs the Display page's CGA row greyed on those machines. Cite MONO-RECLAIM-PLAN. |
| bytes-5 [U] | record | 4,791 bytes of push/pop in resident code. WP14 is the gate that would make a register-convention change safe. |
| bytes-4 [C] | record | The two-segment tax is about 700 bytes on kern_big and 540 on kern_small, spread over 74 and 81 targets, so a per-site migration pass is at its floor. **Do not spend a size pass on it.** Record the trade in SPEC 2.6 (WP24). |
| bytes-7 [C] | record | KERNEL-BYTE-QUEUE's inline-word trampoline, priced: about zero. Close the row. |
| bytes-1 | flagday | The rare cells behind one int door: about -400 resident on kern_big and kern_mega, and it supersedes T1. WP10's abi-4 (API_RN_DSOFF) names the hidden frame reader that T1 or bytes-1 must re-prove. |
| abi-11 / bytes-6 / histsys-9 | flagday | The window record's handler words move to a per-class table: about -200..-260 resident kern_big. WP19's abi-7 lint and WP8's W_IDX do not conflict with it. |
| drivers-9 | flagday | The flag-day form of WP4's histsys-7, at -98 `.bss`. Take histsys-7 now and this row shrinks to the renumber alone. |
| mem-18 / hm-10 / mem-13 | flagday | C1 (KB doors), claims named by record index, and the four meanings of the owner word: the structural cure for the carry bugs the bug pass is fixing one by one (mem-5, hm-6). |
| isr-13's cell / mc-19 | flagday (L3) | WP22 takes only the IMR shadow. WP5's mc-19 is the staged, no-break road toward L3. |
| dr-19 [U] | record | Three caches where DOS has one buffer pool: about -300 `.cold`, but a large write-path rework. Not now, and not with WP12. |
| gfx-14 [U] | record | A per-display renderer table instead of per-primitive compares, about -40 `.text`. Take it the next time the video layer is opened, possibly with video-17. |
| gfx-17 [U] | record | ~61 PARITY pads serve 16-bit buses, not the 8088. State the trade in t_hotpar's header (WP25); this is not a removal. |
| wm-19 [C] | record | band.inc is 1,634 bytes, knob only, and slower on VGA. Retire BAND=1 and its buildmatrix row, or keep it as a named reference. It fits WP11's "no indefinite shelf" if the owner agrees. |
| wm-R2 [U] | record | Deferred invalidation, opt-in per window (OS/2's CS_SYNCPAINT road). Write a plan after WP21's measurements. |
| isr-15 [C] | record | The same proposal as WP18's optional histsys-5. Take it if SB underruns during disk reads are reported. |
| sched-17 [C] | record | Blocking gfx_lock waiters. It needs WP18's sched-15 first. |
| sched-9 [C] | record | The death panel trusts the table it calls suspect (+4). Take it with WP8 if convenient. |
| input-13 [C] | record | The same as WP22's histsys-6. Measure how often it happens first. |
| sysmisc-8 [C] | record | Control Panel invariants held by prose. WP28's engine makes them structural. |
| hm-15 / loader-14 [U] | record | Instances share no code. WP15's hm-11 is the cheaper win until a multi-instance package with a large code part exists. |
| mem-16 [C] | record | No recency inside a rank. If small-heap redraw cost shows up after WP2 adds purgeable modules, shed the deepest window's raise cache first. |
| shell-14 [C] | record | kern_small draws a desktop cell two and three times (+~20 resident to fix partly). Take a flicker measurement before deciding. |
| text-10, text-18, ui-14, loader-13, input-18, shell-13, ui-21 | record | Priced and declined by their reviewers. They are recorded so nobody re-derives them. |
| hiber-19 [U] | record | Keep HIBERNAT.IMG allocated and rewrite it in place. It is the owner's call (disk space held permanently). |
| video-18 [U] | record | A Ctrl+Alt+Esc latch for a hung fullscreen bracket (+15). |
| sysmisc-16 [C] | record | A setting change reaches packages only as a world repaint. Add an EVT_SETTING if a package ever caches a setting. |

