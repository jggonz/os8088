# Phase 1: the kernel work that needs no ABI break (final plan)

The final Phase 1 plan from the 2026-11-08 kernel review, rewritten the same
day. Its inputs:

- the 25 first-round area reviews (`areas/*.md`, `data/*.json`);
- the 12 gap reviews (`areas/gap-*.md`, `data/gap-*.json`), written after the
  first synthesis;
- the three adversarial lenses over the top 40 synthesized items
  (`synth/challenge-truth.json`, `challenge-cost.json`, `challenge-fit.json`).

This is the **no-break** half of the owner's two phases. It replaces the first
`phase1.md`, which was written before the gap reviews and before the lenses.
Every WP keeps its old number, so citations from `bugs.md`, `flagday.md`,
`systemic.md` and the lens files still resolve. WP31-WP34 are new.

**What changed from the first draft, in one paragraph.** WP2's headline falls
from about -3,450 to about -2,400 on kern_big: the FDLG span it moved includes
1,089 bytes of the kernel's own `os88ui` copy, which stays resident
(re-measured below). WP2 is now a set of **owner decisions**, not a size pass.
About a hundred findings appear in both this plan and `bugs.md`; each now has
**one owner** (see "Who owns a finding both lists name"), and most of the
fences, IF=0 fixes and disk hazards move to the bug batches. The gap reviews
add four WPs:

- **WP31**, the kernel matrix;
- **WP32**, the flag day's zero-cost pre-positions;
- **WP33**, HDD.DRV's kernel face;
- **WP34**, XMEM.

They also reshape WP2 (drop-to-cache), WP3 (an init-lifetime keyboard mouse
and a PS/2 overlay instead of an AT rung), WP8 (the fault panel and a divide
ratchet) and WP9 (a LEVEL, not a table end).

## How to read this

- **Scope.** Weaknesses, performance, size, consistency, rethinks that land
  without a break, the documentation system and its gates, and module moves.
  Plain bug fixes are `bugs.md`'s. A WP that needs one names its batch under
  **Travels with**, and does not count its bytes.
- **Verdict marks.** **[C]** confirmed, **[K]** corrected (the figure quoted is
  the corrected one), **[U]** unchecked, **[P]** plausible (gap verifiers'
  word). A lens amendment is marked **Amended (lens)**.
- **Bytes** are signed and placed in a section, ESTIMATED unless marked
  MEASURED. A timing is PREDICTED unless marked MEASURED.
  - *Resident* means `.text`, `.bss`, `.cold`, and `.vga` on VGA/EGA. **`.cold`
    is resident on kern_big and kern_small.**
  - *Transient* means `.boot2` (the blob), `.ovl`, the boot sector and boothd.
  - *Module* means a `.DRV` image, and *package* means an `.O88`.
  - kern_mega figures split *conv* (conventional, `MEGA_RESIDENT`) from *high*
    (`.cold`/`.mega` above 1MB, not budgeted).
  - The kern_big column is the VGA gauge `KERN_BUDGET` is taken on, so it
    counts `.vga`. A mono-XT figure is given where it differs.
  - Per CLAUDE.md, rungs are never the argument.
- **Where the lenses disagree on a figure**, the WP says so, gives the range,
  and re-derives it from `build/kernel.map`, `build/smallk/kernel.map` or
  `build/megak/kernel.map` where that settles it.
- **Rows.** "Rows" names the `tests/suite.py` rows (or new rows) that cover a
  WP. Per the owner's testing rule, no WP runs a soak row or `test-full`. Each
  WP verifies on the host and names its rows; the deduplicated union runs
  **once**, after the last WP and the last bug batch. A number marked *measure
  first* decides a design (PERFORMANCE.md rule 4), so the agent choosing takes
  it once and nobody re-takes it.
- **Owner decisions** are marked **OWNER**. Their bytes are in the WP but are
  shown separately in the totals.

## Who owns a finding both lists name

`bugs.md` registers about a hundred of this plan's findings as BR items. The
rule:

1. **A finding with a BR number is the bug pass's.** Its bytes are counted in
   that batch, and a WP that needs it names it under *Travels with*.
2. **Exceptions:** the WP owns it when its remedy is a design the batch would
   otherwise duplicate, or a gate or contract text. Then the batch should be
   read **without** it. Appendix A lists every owner, finding by finding. The
   exceptions, and what they remove from `bugs.md`'s batch sums:

| finding (BR) | owner | remove from `bugs.md` |
|---|---|---|
| ui-3 (BR-110) | WP1 | batch C "all": -19 |
| histsys-2, isr-8 (BR-9); wm-1, wm-13, histsys-11 (BR-8) | WP8 | batch F core: +30 res, +12 `.ovl`; batch C core: BR-8's +5 and the wake fence |
| boot-4 (BR-153), boot-18 (BR-154), boot-6 (BR-25's boothd half) | WP6 | batch A: KERNEL.SYS -400, boothd -9 |
| abi-3 (BR-62's gate half), abi-4 (BR-146), engsys-2/-8/-12/-15 (BR-75/-76/-171/-172), gfx-7 (BR-152), md-12 (BR-170), shell-6 (BR-117), ui-4 (BR-111) | WP10 | batch A's host gates; batch B's BR-152 `.ovl` +8 (now 0: an assembly-time check) |
| abi-8 (BR-22's gate half) | WP19 | none (BR-22's dw-4 fence stays in batch E) |
| text halves of BR-22 (ui-8), BR-77, BR-78, BR-85, BR-104, BR-115, BR-122; abi-5 (BR-145) | WP26 / WP4 | none (0 bytes); one abilock `--update` instead of several |

Three findings were in both first syntheses and had their double count
corrected by the fit lens: ui-3 (BR-110), loader-8 (BR-11: now batch D's) and
WP8's stubs (BR-9).

---

## Summary: the work packages

Resident bytes, signed, for each WP's recommended core. kern_mega is conv /
high. "Batch" is the `bugs.md` batch it travels with.

| # | work package | kern_small | kern_big (gauge) | kern_mega conv / high | risk | batch |
|---|---|---:|---:|---|---|---|
| 1 | Free bytes: dead code, dead data, redundant tests | -165..-188 | -144..-146 | -87..-101 / -49 | low | - |
| 2 | Module lifetimes: drop-to-cache; FILECP/FDLG cached at boot (**OWNER**) | 0 | **-2,383..-2,448** | 0 / +60..+85 | high | C, D first |
| 3 | Residency by hardware: init-lifetime KBMOUSE (**OWNER**), PS/2 overlay, text-7 (**OWNER**) | -290..-380 | -494..-599 (mono XT -675..-800) | -290..-380 / 0 | medium | F |
| 4 | Driver tables diet and contract text | -10 | -87..-147 | -195..-205 / +40..+100 | medium | G |
| 5 | kern_dos images and kern_mega's own dead weight | 0 | 0 (kern_dos image -560) | -40 / -145..-165 | low-med | H |
| 6 | Boot: derived constants and blob room | 0 | 0 (blob -41, boothd -9, KERNEL.SYS ~-403) | 0 | low | A |
| 7 | .bss arrives zeroed: one rule, a gate, the dead stores | 0..-20 | 0..-20 (`.ovl` -45) | 0..-20 / 0 | low | - |
| 8 | Fault panel, divide ratchet, window-pointer doors | +70..+101 | +75..+106 | +30..+46 / 0 | low-med | F, C |
| 9 | A runtime API LEVEL | +8..+9 | +8..+9 | 0 / +8..+9 | low | - |
| 10 | Gates that catch wrong facts; suite hygiene | 0 | 0 | 0 | low | A (host gates) |
| 11 | The KHP round (**OWNER**) | 0 | +286..+289 | 0 | low | B |
| 12 | FAT write path: free count, runs, bracketed flush | 0..+3 | +160..+255 | 0 / +160..+255 | med-high | E (after) |
| 13 | IF=0 budget: rule, gate, instrument | 0 | 0 | 0 | low | F, D |
| 14 | Routine contracts and the liveness lint | 0 | 0 | 0 | low | - |
| 15 | SDK-side work in packages | +24..+32 | +42..+60 | +30..+40 / +12..+20 | medium | D |
| 16 | Read path, directory and listing | +22..+27 | +77..+88 | +6..+12 / +70..+80 | medium | E |
| 17 | Heap placement and the sorted table prototype | 0 | +8 | 0 / +8 | medium | D |
| 18 | Scheduler: UI boost, task_wake, lock honesty | +50..+77 | +50..+77 | +50..+77 / 0 | medium | F |
| 19 | Context tags, the call-graph gate, the checked build | 0 | 0 | 0 | low | - |
| 20 | Graphics hot paths and the button order | +32..+44 | +120..+146 (mono +40..+56) | +24..+36 / +16..+20 | low-med | B |
| 21 | The damage pass | -10 | -10 | -10 / 0 | medium | C |
| 22 | IMR shadow and PS/2 sync | +25 | +25 | +39 / 0 | medium | F, G |
| 23 | Video facts (two zero-byte items) | 0 | 0 | 0 | low | A, B |
| 24 | Documentation: one copy of each fact | 0 | 0 | 0 | low | - |
| 25 | The comment discipline sweep | 0 | 0 | 0 | low | - |
| 26 | Contract text: one abilock update | -4..-8 | -4..-8 | -4..-8 / 0 | low | all (text) |
| 27 | Desk, dialogs and menus: additions | -21..+9 | -15..-25 | -21..-31 / +6 | low | C, E |
| 28 | Control Panel engine; shared module source; ovlchk sees macros | 0 | 0 (CTRL.DRV -1,500..-3,000) | 0 | medium | - |
| 29 | DISKOPS and HIBER module work | 0 | 0 (modules +54..+74) | 0 | low | G |
| 30 | One clock (prototype) | -10..+10 | -10..+10 | -10..+10 / 0 | medium | G |
| 31 | The kernel matrix: four kernels, one honesty | 0..+4 | 0 | 0 | low | G |
| 32 | Flag-day pre-positions: OSCALL rewrite, the cw_ door | 0 | -40..-60 | 0 | low-med | - |
| 33 | HDD.DRV's kernel face | 0 | 0 (HDD.DRV net about -45) | 0 | medium | E |
| 34 | XMEM.DRV, its cells and DOS.O88's XMS | 0 | -54 | 0 | medium | G |

The phase totals per kernel are at the end of this file, with the
recommended order of work.

### The binding constraint: transient homes on kern_big

Both boot-time homes are full on kern_big. With ownership resolved, the ledger
covers this plan's wants and the bug batches' wants together.

- **The blob** (BOOT2_SECS 13, kernel.asm:2111). boot2_end is 0x19FC of 0x1A00
  (build/kernel.map), so **4 bytes are free**. kern_small has 702 free and
  kern_mega 285, so this is kern_big's problem alone.
- **`.ovl`** is held by a hard `%error` at 5,120. It stands at **5,116**, so 4
  are free.

| home (kern_big) | frees | wants (and whose) |
|---|---|---|
| blob | WP6 boot-7 **-41**, WP1 drivers-12 -4, WP7 hiber-17 -9 = **58 with the 4 free**; optional boot-17 -30..-60 | BR-1 +12..+19 (cost lens's xchg/push/pop form +12; truth lens +19 with the KZIP ES adjustment), BR-6 +25..+35 (Wilton's 3BAh toggle; the alias-test form is +60..+110), BR-56 +4, BR-147 +7, BR-155 +6, WP27 shell-8 +10, WP4 drivers-5 +2..+4: **+66..+85** |
| `.ovl` | WP7 **-45** (sched-12 -30, sysmisc-10 -15) = **49 with the 4 free** | WP8 install +20..+30 (cost lens; the first draft's +12 left out the 7-byte `mov word [es:d16],imm16` stores), WP8 gfce-6 +22 (replaces BR-44's +5), BR-18 +6, BR-88 +5, BR-141 +10, BR-157 +8, BR-158 +16, BR-25 +6: **+93..+103** |

**So:**

- **Land WP6 and WP7 first.** Even then, kern_big needs one of two things.
  - **Blob:** boot-17's kmain_o table (-30..-60, measure first), or a 14th blob
    sector. One more sector costs one sector of boot read, 512 bytes of boot
    footprint (guard 5) and no resident byte. That is the owner's call, and it
    is the cheapest growth in the phase.
  - **`.ovl`:** the wants exceed the room by **44..54** even after WP7. Move
    bodies out of `.ovl` as the `%error` text suggests (SPEC 2.5.3.2), at least
    that many bytes. clk_probe's RTC rungs, which take sysmisc-4/5's +24, are
    the first candidate. Taking gfce-6 in BR-44's +5 form instead saves 17
    more.
- kern_small's installs go in `.boot2`, which has room.
- Add a host tally line for both homes to `kernsize` (WP31), so this ledger is
  printed rather than hand-kept.

## Watch list: what moves `apps/os88api.inc`, `drivers/os88drv.inc` or the lock's inputs

Every item is no-break. Each moves a fingerprint, so it needs
`python3 tools/abilock.py --update "why"` with **no bump** unless stated. A
change to `os88api.inc` moves both `PKG_FMT` and `DRV_VER`. Batch them: WP26 is
the one text update, WP19's tags are another, and each cell-adding WP is one.

| WP | item | lock action | the trap |
|---|---|---|---|
| 9 | gfr-5/abi-6/loader-6: `dw OS88_API_LEVEL` at KERNEL_SEG:0006, header byte 31 = needed level, `OS88_NEED level` | update | Byte 31 is the NUL of a legal 15-character name. Cap names at 14 when it is set, and relax `os88pkg.py:122-125`, `os88disk.py:289-293`, `os88pkgsize.py:41`, `os88rate.py:150`, `os88face.py`/`os88vbt.py`. An OLD kernel's Sticky caption (desksc.inc:770-781) measures such a name as 16 and shows its stem. That is harmless, but it is a visible difference |
| 5, 15 | L7/mc-19's PM-clean bit and mem-17's movable bit in **LD_H_CLASS (+15)** bits 5 and 3 | update | The flags byte has no free bit (fit lens: ICON, ASSOC, PARTS, 3-4 compression, GLYPH, DESK, STIK). Bits 6-7 of +15 are OS88_HIGH/HIGHMEM. Bits 3-5 make an old kernel give the largest stack class (instance.inc:2106-2121), which is safe. New kernels mask `and bx,0x07`. `os88pkg.py:211` (`a[15]&0x3F > STK_CLASS_MAX`) must mask the same, and so must sched.inc:2891-2893's `%error` |
| 2 | hm-9/mem-15 `MEMP_PURGEABLE` (optional) | update | **Every shipped kernel answers an unknown OSAPI_MEM_P verb as MEMP_AVAIL with CF clear** (cost lens; on kern_small too). BR-107 (hm-14, batch D) must land first, and a caller must use `OS88_NEED` |
| 15 | text-15 bold run cell; text-16 `MENU_SEP`/`MENU_CHK` | update | An older kernel draws MENU_SEP as a selectable blank, and so does a kern_small without text-16's +12 |
| 16 | dr-5 `OSAPI_FILE_FIND_SEQ`; gi-2 `OSAPI_FILE_GRAIN` | update | Appended cells |
| 18 | sched-15 `OSAPI_TASK_WAKE` | update | Appended cell |
| 19 | abi-8/ctx-7 `; @ctx` tags in every contract | **one** update | Contract lines are fingerprinted, so the whole tagging is one update |
| 8 | wm-13/histsys-11: the window doors and WM_WAKE refuse a bad BX | update | Reverses "BX is trusted, as OSAPI_WM_WAKE" (os88api.inc:1733). SPEC 11 and 20.8 first |
| 22 | D8 phase 1 `OSAPI_IRQ` (**OWNER**, optional) | update | If its common EOI stub is taken, every hooked interrupt pays about +60..+90 clocks. Keep it off IRQ0 and the 8250 paths |
| 26 | all contract text (list in WP26), including mc-5's GFX_LOCK wording, xm-12's context rule, hdk-2's os88api.inc:3490 and gfce-8's class sentence | **one** update | ui-9's "unless the ring overflowed" weakens a promise. That is still no bump, but say so in the reason |
| 27 | ui-11, ui-12, ui-19 cells; shell-3 `AH=0FEh`; files-17 AH/BX | update | Check what an older kernel does with AH=0FEh before shipping a caller |
| 32 | T6 `OSCALL` macro | update | Byte-identical by construction. Prove it with `cmp` over every image |
| 4 | abi-5, drivers-5, isr-11 | DRV_VER update | drivers-5: keep "preserve BX" as a portability sentence ("the kernel banks BX from build N"). Deleting it lets a new driver clobber BX on a DRV_VER-12 kernel (truth lens) |
| 10 | engsys-11 (os88.h, os88thunk.asm into PKG_FMT's files); drivers-16 (VX/XM/HD guards) | re-record the lock's inputs | Not a bump. After this, WP34's XM_ABI_VER 3 and WP33's HD_ABI_VER 8 are checked |
| 5 | md-15 park-header sum (optional) | **KDL_VER 6 -> 7, kern_mega arm** | Private, but the kern_dos image ships on another disk |

---

## WP1. Free bytes: dead code, dead data and redundant tests

**Goal.** Take every saving that needs no design: code that cannot run, a test
that cannot change the answer, a constant held in memory. It is the cheapest
set of bytes in the phase and gives every later WP a smaller base.

**Findings taken.**

| id | what | bytes |
|---|---|---|
| `text-2` [C] | font_ink's mono early-out cannot jump (vid_depth_set sets mono and planes as one pair, viddet.inc:709). ~40 clocks a mono glyph | -14 `.text` big/mega, 0 small |
| `ui-3` [C] (BR-110) | ui_dispatch recognises the Locator by `cmp word [menu_win],0` instead of reading AM_ONCMD unlocked. It is also the hazard fix | -19 `.text` all |
| `wm-10` [C] | wm_lk's lock-flag test is implied by the owner stamp | -7 `.text` all |
| `text-8` [C] | `[vid_popmax]` is a constant held in a word | -3 `.text` all |
| `sched-14` [C] | sch_clsbytes rows 0 and 4 duplicate. Take it only while the file is open | -1 `.text` all |
| `gfx-3` [K] | gfx_blit1 asks CLIPQF twice | -3 `.cold` |
| `files-11` [K] | dsk_ico_stage's `.none` answers CF instead of an OR over 64 bytes | -6 `.cold` |
| `dw-9` [C] | one `test al,08h` covers LFN and label entries | -2 `.cold` |
| `mc-11` [C] | port 70h is write-only, so write 8Dh/0Dh outright | -2 `.cold` -2 `.mega` (mega) |
| `video-7` [K] | kern_small's fsx rows for VGA modes it cannot set (fsx_mtab rows 5-8 MEASURED 56 bytes; capstab VGA row; fsx_page's Mode X arm). **Amended (truth lens):** fsx_bmodes is indexed `fsx_bmodes-1 / xlatb` (fsx.inc:641, 1194-1197). Either keep all four bytes for ids 5-8 (-79..-82) or drop all four and rely on the caps test (-83..-86). Removing the VGA capstab row needs `[bx+fsx_capstab-2]` (0 bytes) | -79..-86 kern_small |
| `drivers-12` [C] | drv_boot_x writes CH and nothing reads it | -4 `.boot2` |
| `cursor:mc-1` [C] | `[cur_dirty]` is written six times and read nowhere | -27..-29 `.text` all |
| `cursor:mc-4` [K] | six `cur_rows=0` guards protect a value cur_geom cannot produce (the optional `[cur_celly]` clamp costs ~+10) | -24 big/mega; -12..-24 small (this synthesis's range: not every guarded arm is checked to assemble there) |
| `cursor:mc-7` [C] | a dead `mov si,bx` in wm_clip_set | -2 `.text` all |
| `os88ui:F3` [C] | os88ui_kbtn rebuilds a constant record every call. Make `os88ui_krec` initialised | -28 big/mega (`.cold`); small -4 res, -24 in each of CTRL.DRV and FDLG.DRV |
| `os88ui:F4` [C] | os88ui_arm_f is a 4-byte shim. Store directly | -4 `.cold` -2 `.text` -2 CTRL.DRV big/mega; 0 small |
| `os88ui:F6` [K] | the dead OS88UI_FILL is loaded at seven sites. **The ctrl.inc:4302 site must hoist the clear above `call cpf_vidok`**, whose CF it would clobber | ~-2 res, ~-5 modules |
| `drivers-11` `wm-14` `text-13` [C] | dead `%else` arms; wm_dmg_bands written twice; two complementary parity nops | 0 bytes, about -200 source lines |

**Owned elsewhere.** `loader-8` is part of BR-11's fix (batch D) and is counted
there.

**Files.** kernel/font.inc, ui.inc, wm.inc, viddet.inc, menu.inc, sched.inc,
vga12.inc, files.inc, diskw.inc, mega.inc, fsx.inc, driver.inc, mouse.inc,
ctrl.inc, fdlg.inc, hiber.inc, shutdown.inc; apps/os88ui.inc.

**Bytes.**
- kern_small **-165..-188** resident.
- kern_big **-144..-146**, plus -4 `.boot2` and about -7 in CTRL.DRV and
  HIBER.DRV.
- kern_mega **-87..-101 conv, -49 high**.

**Risk.** Low. text-2, text-13 and mc-1 touch hot-path parity padding, so
re-balance `t_hotpar`. ui-3: check the Locator with no app menu and with a
package menu. F6: the CF site.

**Rows.** `hotpar`, `kernbudget` (fast); `kernresident`, `small128` (full);
soak `disp*`, `fmarrows`, `navcalls`, `vidfskeys*`, `xorrect`, `dispblit`,
`btnall`, `btncp`, `curshape`, `curbusy`.

**Dependencies.** None. **Land it first**, with WP7.

**Travels with.** Nothing. If batch C is in flight, BR-110 is this WP's.

---

## WP2. Module lifetimes: drop-to-cache, and FILECP/FDLG cached at boot

**Goal.** Give kernel modules the lifetime ONDEMAND-PLAN 7.1/7.2 designed and
never built: **dropped to a purgeable cache, re-adopted on the next need, and
reloaded only if shed.** The pin is the need/drop bracket that `t_moddrop`
already guarantees. Then decide, as the owner, whether kern_big's two largest
resident bodies join it.

**Findings taken.**

- `rt-1` [K] **Drop to a cache; let the need/drop bracket be the pin.** It
  replaces both the first draft's per-thunk pin and HEAP-UNPIN piece B, which
  priced a per-thunk pin over 29 sites; there are 49 today, through 14 slot
  blocks.
  - mod_drop re-tags the claim purgeable and disarms it.
  - mod_need's hit path re-tags it pinned, re-zeroes the module `.bss` and
    re-arms.
  - Cost: +60..+85 `.cold` for every module, and +2 header bytes per module
    image.
  - Corrections the WP carries:
    - (a) It **overturns a recorded decision**: mod_drop's banner and SPEC 2.8.3
      say the pin was deliberately not built. Rewrite SPEC 2.8.3 first. The
      heap still purges only a dropped image, which keeps the banner's reason.
    - (b) On kern_mega, `mod_hi` images above 1MB are freed through
      mega_hifreef, so mem_pg cannot purge them. The cache is conventional-heap
      only there, unless a pool purge is added.
    - (c) A re-adopt **skips `mod_cfix`**. Its idempotence is unproven,
      because os88rom's cfix_prove models one pass.
- `rt-9` [C] **Purge ranks per module, not inherited**: two purgeable tags, or a
  rank field in the mod row. +4..+8, inside rt-1's figure.
- `shell-2` [K] **The first two users:**
  - MOD_SETS (CTRL.DRV's settings core, 3,180 bytes MEASURED) is loaded and
    dropped on every shortcut drag (desksc.inc:437-462);
  - STIKMENU.DRV (7,876-byte image) is loaded on every press
    (stikmenu.inc:60-88).

  Both become a cache hit after the first use.
- `rt-2` [K] with `bytes-3` [C] and `rt-6` [C]: **FILECP and FDLG on kern_big
  as modules cached at boot. OWNER.**
  - **Re-measured, and the first draft's -3,600 was wrong.** fdlg's `.cold`
    span fdlg_ref..desk_zs is 2,080 bytes, but **1,089 of it is `os88ui_*`**,
    the kernel's copy of the shared UI, which stays resident (re-measured on
    build/kernel.map for this plan: fdlg_* 991, os88ui_* 1,089). The movable
    part, less the resident doors kern_small also keeps, is FILECP about 1,657
    and FDLG about 911: **-2,568 movable** (MEASURED spans). With boot glue and
    the prompt (+60..+100) that is **about -2,470..-2,510**.
  - **The boot read does not grow**, because the bytes are already in
    KERNEL.SYS as `.cold`; they move from a resident span to an image the boot
    copies into a claim.
    - The cost lens's +140..+350 ms assumed added sectors and is withdrawn
      under this form.
    - What remains is a copy of about 2.6KB at ~13.3 cycles a byte, about 7 ms
      PREDICTED.
    - The images must be placed above KERN_END in the main read and counted by
      guard 5, the 128KB boot (the cost lens's point).
  - **Skip the preload when `[api_coldseg]` ≥ A000h** (truth, fit and rt-2
    agree). On a ROM or COLDHMA.SYS machine these bodies already cost no
    conventional RAM, so the saving there is 0 and the claim would be pure
    cost. Load from the ROM copy when the dialog opens.
  - **Rank at the top** (MEM_PG_HIGH or rt-9's highest rank), so they shed
    after every package cache (fit lens).
  - **Seam work, not a flag flip** (truth lens). kern_big's fdlg has kern_big-only
    arms: FDH_RECT for PAINT/CLICK/UP (fdlg.inc:70-76, 933-937), FDX
    dskw_czsize (653), DESK.DRV's DKD_KEY hook (746-760) and FDX fm_btn1 (1056).
    A kern_big module arm needs its own entry and FDX set. Its module has more
    entries than kern_small's (fdlg_onup/ondrag; FM_MSEL), so -2,568 is a
    ceiling.
  - **Disk:** a reload after a shed needs FILECP.DRV and FDLG.DRV on all four
    kern_big system-disk geometries, about +4 clusters each, or a re-read of a
    KERNEL.SYS slice.
  - **Runtime:** a reload is 1-2 int 13h (0.4-0.8 s) plus a possible swap, on a
    path that today never touches the disk.
  - **Why OWNER.** kern_big has never had the "Needs Sys Disk A:" failure. The
    records against it are:
    - ONDEMAND-PLAN 1.2: "The chooser cannot be the thing that costs you the
      disk you are choosing on";
    - KERN-SMALL-MODULE-SPLIT.md:13-15: "Big needs those to stay fast, and to
      keep loading via the cyl run kernel read on boot";
    - SPEC 38.0: "On kern_big the glue is NOT a module, on purpose".

    The cached form keeps the cylinder run and narrows the refusal to a
    purged image under pressure. For that case the gap review proposes
    COMMAND.COM's answer, the existing swap prompt, instead of a refusal.
    **Put the trade to the owner with those three quotes. Do not present it as
    settled.** kern_small's own caching (rt-6, +60..+85 there) is a separate
    owner question.
- *Optional:* `hm-9` [K] `mem-15` [C] **package purgeable claims**
  (`MEMP_PURGEABLE`). **Amended (truth and cost lenses):**
  - MC_DMA bit 13 is free only once MC_DMA_HEAD narrows from 0x3FFF to 0x1FFF
    (0 bytes; every head reader masks with it).
  - The purge's proc call cannot run where mem_shed_one runs. It runs inside
    pushf/cli, and its mem_regrow caller may not call a proc. So call the proc
    at IF=1 under sch_lock, as mem_reloc_call does, with mem_cp_busy set across
    it. Either mem_shed_one skips MEMP_PURGEABLE records from mem_regrow, or
    their purge is deferred to the compactor's drop arm.
  - Cost: +50..+80 `.cold` kern_big (not +30..+70), and +4..+6 on kern_small
    for a real refusal.
  - Consumers wait: Skies' title art (-485 ms of exit) and the Text Screen
    shadow (~420 ms to rebuild).

**Owned elsewhere.** These travel first; bytes are not counted here.
- `mem-10`/`hm-3` (BR-4, batch C) and gap `ctx-1`/`ctx-2`: teardown posted to
  the UI task, which is the deferred drop this WP's cache needs. **The lenses
  disagree on BR-4's bytes:**
  - truth: +30..+50;
  - cost: net -15..+16;
  - fit: +15..+30 at the top;
  - systemic cost: +31..+41 `.text`.

  Re-derived for this plan: inst_task_die is 31 bytes on kern_big
  (build/kernel.map 66AD..66CC) and 26 on kern_small. The cost lens's net
  saving reused wm_clreq and app_close_win's task-less arm. The truth and fit
  lenses show that path refuses an I_STATE-2 record (instance.inc:1662-1670)
  and calls the dying package's close negotiator, so the reap needs a drain of
  its own. Expect **about +20..+40 `.text`**, of which ctx-2's owner check is
  12. ctx-2 also deletes kern_mega's 24-byte duplicate in mega_fkill.
- `shell-1` (BR-42, batch C): library lifetime by binder.
- `mem-3`/`hm-8` (BR-53, batch D): a doomed claim must not shed every cache
  before it fails. That is a prerequisite here.

**What to build, in order.**
1. Batch C's BR-4 and batch D's BR-53 (prerequisites).
2. rt-1 drop-to-cache with rt-9's ranks, written SPEC-first.
3. shell-2's two users.
4. **OWNER:** rt-2's FILECP/FDLG arm on kern_big and kern_emu only, with the
   ROM skip.
5. Optionally hm-9.

**Files.** kernel/mod.inc, memory.inc (mem_pg_own, mem_shed_one), filecp.inc,
fdlg.inc, kernel.asm (FCP_MOD/FDLG_MOD on kern_big), boot/boot2.asm (the copy at
mem_unblob), tools/os88mod.py, desksc.inc, stikmenu.inc, tests/moddrop.txt,
Makefile (the four kern_big system disks), SPEC 2.8.3 and 38.0.

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| rt-1 drop-to-cache + rt-9 ranks | 0 (OWNER: +60..+85) | +60..+85 `.cold` | 0 conv / +60..+85 high (conventional-heap modules only) |
| rt-2 FILECP/FDLG (OWNER) | 0 (already modules) | **-2,568 movable (MEASURED) +60..+100 glue = -2,468..-2,508**; 0 where `.cold` is ROM/HMA-adopted | **0. Do not apply**: their `.cold` is already above 1MB |
| shell-2 users | 0 | 0 (inside rt-1) | 0 |
| *opt.* hm-9 | +4..+6 refusal | +50..+80 `.cold` | 0 / +50..+80 |
| **core total** | **0** | **-2,383..-2,448** | **0 / +60..+85** |

Disk: about +4 clusters on each kern_big system disk.

**Risk.** High. It adds a state to every module, it changes what the user sees
on kern_big under pressure, and FDLG's kern_big seam is new code.

**Rows.** `moddrop` (fast; it must recognise the cache as a drop);
`kernresident`, `small128`, `bootsmoke` (full); soak `fcpapi`, `fcpcopy`,
`fcpsmall`, `fcproom`, `fdlggrey`, `fdlgsmall`, `fdlgdrop`, `fdlgup`,
`fdlgchoose`, `stik`, `stikmenu`, `stikshort`, `stiknodesk`, `desksc`,
`deskitem`, `shedrelist`, `regrowshed`, and the `mega-` twins (mega must stay
unchanged). **New rows:**
- shed FILECP under pressure, then Save As shows the prompt;
- Sticky Apps pressed twice gives one disk load;
- the same on a COLDHMA machine, with no preload claim.

**Dependencies.** Batch C (BR-4) and batch D (BR-53) first. WP9 for hm-9's
callers.

**Travels with.** After batches C and D.

---

## WP3. Residency by hardware: the keyboard mouse, the PS/2 runtime, text-7

**Goal.** Code for hardware this machine lacks, or code that is needed only
until something happens, should not stay resident. **Amended (gap review):** the
first draft's two mechanisms are both replaced.
- input-12's load-on-first-press becomes **init lifetime**: hold from boot,
  drop at settle (rt-7).
- bytes-2's AT rung becomes an **owner-loaded overlay** (rt-3), because rungs
  serve nested machine classes only (rt-4, record) and a second rung strands
  the owner's 8088+VGA 5150 #2.

**Findings taken.**

- `rt-7` [K] with `input-12` [C]: **the keyboard mouse is init-lifetime code.
  OWNER.**
  - `[mou_ptr]` is set and never cleared (mouse.inc:6439-6447), so once a mouse
    speaks the code is never needed again.
  - Hold the image from boot, on WP2's boot-cache mechanism, and drop it beside
    every `[mou_ptr]` store: mou_settle on kern_big (mouse.inc:1179-1183),
    kern_small's inline MOU_SETTLE_STORES (3933), and kern_emu's vmm_boot_x
    (vmmouse.inc:339).
  - Keep resident: kbm_isr (IRQ1's handler for the whole kernel), KBM_SCAN's
    keypad-5 latch, kbm_key's gates, kbm_shf and the thunks.
  - There is **no first-press disk read**, which removes the fit lens's
    objection (mouse.inc:2412-2417: "the difference between a usable machine
    and an unusable one at exactly the moment the mouse fails").
  - Hibernate restores `[mou_ptr]` with the kernel, so a dropped keyboard mouse
    stays consistent across a resume.
  - **The cost the first draft missed** (cost lens): a machine with *no* mouse
    keeps the image claimed for the whole session. That is about +450 plus
    paragraph rounding and a claim slot, against 300..380 moved out, so **+85..+165
    of RAM in use, for ever**, on exactly the 128KB mouseless 5150.
  - Saving: -290..-380 static resident (MEASURED spans: kbm_tab..kbm_isr 306,
    kbm_p5spend 16, kbm_pollm..kbm_ui 108, kbm_post 9, state 17).
  - On kern_small the boot-cache copy is new code, unpriced (expect tens of
    `.cold` bytes), and it is decided with WP2's kern_small question.
- `rt-3` [C] **The PS/2 runtime as an owner-loaded overlay**, XMEM.DRV's
  DRVC_OVL shape.
  - mou_p2_* is 251 `.text` MEASURED plus ~14 of state, about 265 in all; its
    init is already `.ovl`.
  - It leaves **every** kern_big machine, so the gauge sees it: **about -220..-235**
    (-265, plus about 30..45 bytes of fronts and fence).
  - mou_p2_off is called unconditionally (mouse.inc:1268, 1435) and needs a
    front.
  - A 286 with a live PS/2 mouse pays a 0.5-1KB heap claim.
  - OSAPI_MOUSE_FEED gains a second caller (ABI-BREAK-LIST T4's note).
  - kern_mega keeps it resident for now, because an overlay ISR there needs
    mega's gate handling. kern_small has no PS/2.
  - WP22's input-10 (+14) then lands inside the overlay.
- `text-7` [K] **font_run_x's once-per-run VGA prologue into `.vga`. OWNER.**
  - `.planar`/`.vxp0` is 144 bytes MEASURED, and fnt_rn_edge's VGA arm is 43
    more.
  - **Amended (cost lens):** the 8086's short-only jcc makes each door
    jcc-over plus near jmp (5 bytes `.text`) and each arm a 3-byte jmp back in
    `.vga`. That is **+16 on the gauge** (not +10), about -165..-185 on a mono
    machine (text.md's -165 against WP3's -185; 144 is the measured part), and
    ~30-40 clocks a run on VGA (<1% of the ~4,300-cycle prologue).
  - The parity pad at font.inc:1839 and `t_hotpar` move with it.
- *Refused:* `bytes-2`'s AT rung. Its XMS half, the bodies past `xm_have`
  (about 110..130 bytes), stays resident and is recorded with WP34.
- *Record:* video-17's mono rung (-1,100..-1,500 on the VGA gauge) is
  complementary to the VGA rung (rt-4). It is the largest resident lever left
  and needs the Display page's CGA row greyed there.

**Files.** kernel/mouse.inc, vmmouse.inc, font.inc, mod.inc (mod_tab rows),
kernel.asm (the slide decision; the boot-cache copy), a new drivers/ps2mouse/
(or a DRVC_OVL row beside XMEM's), tests/vgarefs.txt, tests/moddrop.txt (an
"init lifetime" reason), SPEC 9.6.1.1, 9.12, 2.10.9 (rt-4's nesting rule, also
WP24).

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| rt-7 KBMOUSE (OWNER) | -290..-380 (gross of the kern_small boot copy) | -290..-380 | -290..-380 conv |
| rt-3 PS/2 overlay | n/a | **-220..-235 on every machine** | 0 (kept resident) |
| text-7 (OWNER) | n/a | +16 gauge; -165..-185 mono | 0 |
| **total** | **-290..-380** | **-494..-599 gauge; -675..-800 mono XT** | **-290..-380 conv** |
| modules | KBMOUSE.DRV +400..+450 | KBMOUSE.DRV +400..+450; PS2 overlay ~+265 | KBMOUSE +400..+450 (0 conv) |

KBMOUSE.DRV takes a cluster on every system disk, small360 included.

**Risk.** Medium. The drop sites must cover every `[mou_ptr]` store on every
kernel. The overlay needs its door registry, on the `tests/vgarefs.txt` shape.

**Rows.** `ovlchk`, `moddrop` (fast); `ps2mouse` (full); soak `wheelps2`,
`mega-ps2mouse`, `vmmouse`, `vidresidentcga`, `vidresidentvga`,
`kernresident`, `disptext`, `glyphcost`, `hibernate`. **New row:** a
pointerless desktop driven by arrow keys on a CGA `_gla` machine (no `kbm*` row
exists), and the same after a hibernate resume.

**Dependencies.** WP2's boot-cache mechanism (rt-7). `t_moddrop` must know the
init lifetime (fit lens: the first draft's "WP10 only" was wrong).

**Travels with.** Batch F (mouse.inc) if it is in flight.

---

## WP4. The driver tables diet, and the driver contract text

**Goal.** Shrink what kern_big keeps per driver class, and make
`drivers/os88drv.inc` say what the kernel does, in a single DRV_VER update.

**Findings taken.**

- `histsys-7` [K] with `gfr-6` [P]: **read the six non-SOUND service tables in
  place.**
  - drv_svc is DSV_SIZE × (DRVC_MAX-3) = 252 `.bss` on kern_big, MEASURED
    (B0FE..B1FA). Dropping six copies is -216 `.bss`.
  - Add +12..+18 `.bss` for a per-class table offset and length. The table is
    wherever DRVV_ATTACH's SI pointed, not at a fixed place (truth lens).
  - **Amended (fit lens):** turn drv_cls_svc_x into a cell fetch that answers 0
    past the table. That is one length test, which flagday D4's floor later
    deletes, instead of a test at each of ~14 read sites. Sites that hold ES for
    a transfer bank it around the read.
  - Net **-90..-140** kern_big, ESTIMATED.
  - **kern_small: 0.** drv_svc is inside driver.inc's second OS88_DRIVERS gate
    (3389-6883) and absent from build/smallk/kernel.map. The truth and fit
    lenses' -36/-46 were wrong (cost lens; re-checked for this plan).
  - It makes D1 and drivers-9 moot (gfr-6).
- `N1` (gfr-6, flagday): the two zero-length fallbacks (driver.inc:2981 FSV,
  4072 DSV) fail closed. -11 on kern_big/kern_emu/kern_mega; 0 on kern_small,
  where both sit inside OS88_DRIVERS (re-checked: 1927..3387).
- `drivers-5` [K]: the kernel banks BX in `%1_call` (blob, CTRL.DRV, HIBER.DRV:
  +2..+4 each) and in drv_call_ent **and drv_unload_x** (truth lens: the area
  verifier's correction). +4..+6 resident. **Keep the sentence for
  portability**: drivers ship with their kernel, but a new BX-clobbering driver
  would break on a DRV_VER-12 kernel.
- `drivers-14` [C]: OSAPI_FSX_SPK's open asks snd_str_busy (+8), or SPEC 34.11
  says the bracket coexists with a stream. 0..+8.
- Contract text (one DRV_VER update): `abi-5` [C] (BR-145; the driver preserves
  DI, BP is spent), `isr-11` [C] (DSV_TICK's DS/ES/DF/BP and the nested-tick
  skip), drivers-5's sentence, and gap hdk-9's note that the header name at +16
  is a NUL string to publish rather than copy (WP33).
- **Conditional, not counted:** `drivers-10` [U] with `bytes-8` [K].
  - **Amended (truth lens):** the driver and module file names are not read
    only by drv_load_at. osapi_img_name (kernel.asm:6446-6515, the Task
    Manager's cell) and kern_mega's mega_imgnf copy the NUL string at
    `[row+DRVR_FILE]`/MODR_FILE at any time (re-checked).
  - So on kern_big, drivers-10 (-95) and bytes-8 (-25) need a priced rework of
    both readers, and drivers-10 also needs blob room (the ledger above).
  - **On kern_small, bytes-8 alone is safe**: osapi_img_name is `mod_gone`
    there. That is -10 (five `mod_f_*` names; smallk map 7A9B..7AD7). Stage
    the stem on the stack or in a 13-byte stage of its own, **not cp_sbuf**,
    which the Control Panel uses while CTRL.DRV is open (fit lens).
  - The three name tables (blob, CTRL.DRV, HIBER.DRV) come from one `%include`
    (DRV_TITLES precedent).
- **Measure only:** `drivers-13` [C]. **Amended (cost lens):** drv_svc_call_x
  is 38 bytes MEASURED and near-calls wm_nest_pushd/pop, which are `.cold`.
  From `.text` those become far calls, so the move costs about +8 `.text` and
  +60 cycles. Keep the counter; drop the move.

**Owned elsewhere.** `drivers-7` (BR-39, batch G). Gap `gkm-1`'s two
kern_big fixes (BR-32 classk's row count, BR-38's `.badv`; batch G).

**Files.** kernel/driver.inc, mod.inc, kernel.asm, boot/boot2.asm (`%1_call`),
ctrl.inc, hiber.inc, drivers/os88drv.inc, drivers/sound/sb.inc (comment).

**Bytes.**
- kern_small **-10** (bytes-8).
- kern_big **-87..-147**: histsys-7 -90..-140, N1 -11, drivers-5 +4..+6,
  drivers-14 0..+8.
- kern_mega: **-195..-205 conv** (drv_svc's `.bss` is conventional) and
  **+40..+100 high** (the read code is `.cold`).
- Modules: +2..+4 in each of blob, CTRL.DRV and HIBER.DRV.

**Risk.** Medium. In-place reads see later driver writes, where the publish-time
snapshot did not. HDD.DRV is the only shipped driver with a short table that
reaches an in-place read (fit lens).

**Rows.** `drvovl`, `drvmem`, `mirror`, `abilock` (fast); soak `drvcall`,
`drvmove`, `drvclaim`, `hmadrv`, `lzdrv`, `heapdrv`, `vidsound`, `arcsound`,
`midiracksb`, `usbmouse`, `hibernatedrv`, `cpnames`, `extdmod`, `dockmodule`,
`hddcp`, and the `mega-` twins.

**Dependencies.** WP6 (blob room for drivers-5's +2..+4).

**Travels with.** Batch G (driver.inc).

---

## WP5. kern_dos images and kern_mega's own dead weight

**Goal.** Take kern_mega's and kern_dos's free bytes, and the mega-span
savings the gap review found.

**Findings taken.**

| id | what | bytes |
|---|---|---|
| `md-6` [K] | Both kern_mega kern_dos images carry a resume they can never run (kd_resume opens `jmp .no`). Keep `kd_resume: stc/ret` and kd_stageseg | -1,028 each kern_mega kern_dos image (357+189+459+23, MEASURED on a byte-identical reassembly) |
| `md-7` [C] | kd_glb (512) serves only KD_GATE | -512 `.bss` in kern_big's and both kern_mega kern_dos images |
| `md-13` [C], `md-14` [C] | ten `call X / ret` doors; a no-op mem_free_x | -40, -8 every kern_dos image |
| `md-8` [C] | KDL_PLOW is written and read by nobody. **Amended (cost lens):** -11, not -15 | -11 `.mega` |
| `md-19` [K] | six HIBER.DRV-only scratch cells leave conventional `.text`; mega_hbwk stays | -22 conv, +~40 HIBER.DRV |
| `md-21` [U] / `ul-5` [C] | **eight** A20 bodies, not six (XMEM.DRV and HDD.DRV have their own), with three probe aliases and several budgets. One macro parameterised by alias and budget, in an include the drivers share | 0..-20 |
| `md-20` [C], `md-22` [C] | KDLF_RAHSH -> KDL_RAHSH; kdlayout.inc keeps the current measurement per arm | 0 |
| `mc-19` [U] / **L7** | the "protected-mode clean" bit in LD_H_CLASS bit 5 (see the watch list: os88pkg.py:211 and sched.inc's `%error` mask 0x1F, plus an SDK macro and a lock update) | +12 `.mega` |
| `gms-5` [C] | SEG_TILE: one mega_blkof, refuse when (n-1)×64KB passes the block end, clip only the last tile | -10 `.mega` |
| `gms-8` [K] | mega_dklibfree leaves conventional `.text`: the conventional arm zeroes the word **before** `call mem_free_x` (an `and` after it would clear the caller's CF) | -18 conv, +18 `.mega` |
| `gms-9` [C] | a dead push/mov/pop of BX in mega_imghif | -4 `.mega` |
| `gms-R2` [C] | OSAPI_MEM_SUBSEG as an adapter over mega_segf's SUB arm, keeping subsegf's region-end rule. **After batch H's gms-4 lock fix**, or the adapter inherits the race | -150 `.mega` |
| *opt.* `gms-R1` [C] | a kind byte per LDT entry; each walker tests the kind, folding mega_iswin, mega_wslot and mega_ldt_free's wtab scan | about +600 tail data and -100..-200 code, above 1MB |
| *opt.* `md-15` [U] | the park header's 16-bit sum in md-8's freed word. **KDL_VER 6 -> 7** | +20 `.mega` +35 HMA kern_dos |

**Amended (cost lens): what a kern_dos byte buys.** "Handed to the DOS
program" is false at byte level:
- **HMA arm.** The image is assembled at FFFF:0010, so -1,588 is HMA headroom
  (KD_IMG_KB 39 -> 37) and 0 conventional.
- **LOW arm (KD_LOWDOS).** The program gains through the KIMG_PARA rung:
  37,510 -> 35,922 is **+1KB** (37 -> 36). That is 82 bytes short of 35KB.
- **kern_big's arm.** 34,796 -> 34,236 stays in rung 34. The program gains
  nothing until about 444 more bytes go.

The bytes are real all the same; say which arm.

**Removed from this WP.**
- **`mc-16` is out** (fit lens). Moving the PM master PIC off 20h is option
  (d2) of owner question O7. The owner answered (d1) on 2026-11-01
  (WIN16-ANY-PLAN.md:123), and SPEC 124.31.2 records (d2) as not taken.
  Reopening it is the owner's question, and MEGA-VIDEO-REALMODE-PLAN.md:49
  would join the literal list.
- `mc-9` (BR-67), `mc-13`/`mc-14` (BR-165), `mc-10` (BR-68), `gms-1..-4`,
  `gms-6` and `gms-10` are batch H's.

**Files.** kerndos/kdresume.inc, kdentry.inc, kdosgate.inc, kdback.inc,
kdshim.inc, kdlayout.inc, kdpark.inc, kdhma.inc; kernel/megados.inc,
megares.inc, mega.inc, hiber.inc (md-19), loader.inc/instance.inc (the class
mask); a new shared A20 include used by drivers/xmem/xmem.asm and
drivers/hdd/hdxms.inc; tools/os88pkg.py; apps/dos/dos.asm (md-20).

**Bytes.**
- kern_small 0. kern_big kernel 0; its kern_dos image -560.
- kern_mega **-40 conv** (md-19, gms-8).
- kern_mega **-145..-165 high**: md-8 -11, gms-5 -10, gms-8 +18, gms-9 -4,
  gms-R2 -150, L7 +12, md-21 0..-20.
- Each kern_mega kern_dos image **-1,585..-1,588** (the fit and cost lenses
  differ by 3 on rounding). HIBER.DRV +~40.

**Risk.** Low for the deletions. Medium for gms-R2 (an adapter over a hot SEG
arm) and the A20 macro (eight call sites with different budgets).

**Rows.** `mirror` (fast, once WP10's md-5 adds kerndos); soak `kerndos`,
`kdos`, `kdarena`, `kdbigexe`, `kdhand`, `kdcylrun`, `kdpart`, `kdreturn*`,
`kdkbd`, `kdmouse`, `dosbss`, `dosxms`, `megahiber`, `megadosp`,
`megadosroom`, `megatrap0`, `megagrid`, `win16megatrap`, `xmla`, `xmcheck`,
`hdboot`; `megahigh` (full). Bochs for the SEG arms.

**Dependencies.** WP10's md-5 (t_mirror over kerndos). Batch H's gms-4 before
gms-R2. WP9's header-bit ledger before L7.

**Travels with.** Batch H.

---

## WP6. Boot: let the build choose the constants, and make room

**Goal.** Make three hand-typed constants derived, and free the blob and boothd
bytes the bug pass's boot fixes need. No resident byte moves.

**Findings taken.**

- `boot-7` [C] **Stage 2's copy of the DPT patch is dead.** Stage 1 patches int
  1Eh first (boot.asm:301-334). Nothing else enters boot2_entry; boothd never
  enters stage 2. Exactly 41 bytes by encoding. **-41 blob on every kernel.**
- `boot-6` [K] (BR-25's boothd half): boothd's RAM check becomes stage 1's
  assembly-time `cmp ax, MEMNEED`. -9 boothd, the room boot-5's +8 handoff
  needs.
- `boot-4` [K] (BR-153): move the SPL_RESIDENT guard's label below spl_quiet (0
  bytes). Then derive SPL_RESIDENT as (SPL_RES_SIZE+511)/512 and have the
  Makefile read it from the `ks:` line. **First audit every first-tick store**
  (vid_setmode, vid_wipe, vid_cga_equip, vid_6845_prog, bvs_apply) for an
  address in sectors 5-8. **Amended (cost lens):** under KZIP, moving 2,048
  bytes into the packed tail saves
  - **~-403 on kern_big** (167 -> 166 sectors);
  - **~-383 on kern_mega** (-1 sector);
  - **~-383 on kern_small** (0 sectors: 106 either way).

  It costs ~2KB more LZ4 decode at boot, a few ms PREDICTED. The read is inside
  a coalesced run, so the boot is not measurably faster. The win is disk.
- `boot-10` [C] **Compute KSIG_OFF.** **Amended (fit lens):**
  - The generator must be **independent** of the gate. BR-1 is `crosses()`
    walking from the wrong start while the gate agreed. So
    `tools/os88ksig.py` gets its own implementation that t_canary must not
    import, or the second reader becomes `blobruns` or a booted geometry row.
  - The Makefile rewrites KSIG_OFF per build arm (Makefile:2242 '-512', 2252
    '+1024'), so emit each arm's value.
- `boot-18` [K] (BR-154): guard 5 omits BLOB_LIFT on every shipped kernel
  (ROM_COLD is on unless NO_ROM_COLD, kernel.asm:1131-1132). That under-counts
  the boot footprint by 1,536. Use BLOB_SEG_AT.
- `boot-19` [C]: boot.asm's KERNEL_SECTORS=16 and BOOT2_SECS=2 defaults become
  `%error`.
- `boot-9` [C], `boot-13` [C]: constants' comments carry their invariant and
  guard; the history ledger goes to docs/history.
- *Optional, measure first:* `boot-17` [U]: kmain_o as a blob table, -30..-60
  blob. The ledger above needs it, or a 14th sector.

**Sequencing with BR-1 (fit lens).** BR-1 keeps b2_runmax at the track bound
"until SPL_RESIDENT sectors are aboard". boot-4 lowers SPL_RESIDENT from 9 to 5.
So **land boot-4's first-tick audit before BR-1**, or re-check BR-1 after it.
BR-1's own form is the cost lens's safe `xchg`/`push`/`pop` (+12) or the truth
lens's +19 with the KZIP ES adjustment. It is batch A's, paid from boot-7's -41.

**Files.** boot/boot2.asm, boothd.asm, boot.asm; kernel/splash.inc;
kernel/kernel.asm (guard 5, the SPL guard at 8650-8653); Makefile (SPL_RESIDENT,
KSIGDEF2, KZ_HEAD); tests/unit/t_canary.py; a new tools/os88ksig.py;
docs/history/.

**Bytes.**
- 0 resident on every kernel.
- Blob -41 on every kernel; boothd -9.
- KERNEL.SYS -403 / -383 / -383 (big / mega / small).
- Optional blob -30..-60.

**Risk.** Low. boot-4's audit is the step that must not be skipped.

**Rows.** `canary`, `image`, `diskverify` (fast); `bootsmoke` (full); soak
`blobruns`, `bootfloor`, `bootfloor-ab`, `smallboot`, `splashbar`, `splashspin`,
`hdboot`, `hdtakeboot`, `pravetzboot`, `v20boot`.

**Dependencies.** None. It unblocks batch A's BR-1, BR-6, BR-25 and BR-63.

**Travels with.** **Batch A**, in the same session and first. They share
boot2.asm, splash.inc and kernel.asm's guards.

---

## WP7. .bss arrives zeroed: one rule, a gate and the dead stores

**Goal.** Settle the contradiction between the sources that say `.bss` arrives
zeroed and those that say it does not. That contradiction cost fsx_cur. Then
free the `.ovl` room the ledger needs.

**Findings taken.**
- `boot-8` [C]: in shipped kern_big the file bytes between kernel_text_end and
  `.ovl` are zero, and every loader writes them. Add one `%if` on the layout.
- `histsys-16` [C]: adopt the zeroed rule. Extend t_bsssentinel to assert the
  `.bss` span is all-zero on **all four** budgeted kernels. Drop the resident
  zero stores (~-20, unmeasured).
- `sched-12` [C]: sched_init's stores (-30 `.ovl`). The scheduler already
  depends on the rule (`[sch_chbusy]`).
- `hiber-17` [C] (-9 `.boot2`), `sysmisc-10` [C] (-15 `.ovl`). Their comments
  distinguish kernel `.bss` (zeroed) from a module image's data (not).

**Files.** kernel/kernel.asm, sched.inc, hiber.inc, clock.inc, clockw.inc,
ctrl.inc, events.inc, cpudet.inc, bootprof.inc, mod.inc, splash.inc;
tests/unit/t_bsssentinel.py; SPEC 2.5, 2.6, 8.

**Bytes.** 0..-20 resident on each kernel (mega conv); `.ovl` -45 and `.boot2`
-9 on kern_big.

**Risk.** Low once the gate holds on four kernels.

**Rows.** `bsssentinel` (soak; promote it, since it is a rule about an
artefact), `kernbudget` (fast); `bootsmoke`, `small128`, `kernresident`
(full); soak `megahigh`.

**Dependencies.** None. WP8 and the bug batches' `.ovl` items depend on it.

**Travels with.** WP1 (same session).

---

## WP8. Fault containment: the panel, the divide ratchet, the window-pointer doors

**Goal.** One fatal path. Today:
- **On an IBM 5150/5160 ROM, one divide overflow is a dead machine**: the stub
  masks the 8259 (wm.inc:8268-8275; tests/int0sweep.py).
- On a 286+ it re-runs the DIV for ever.
- GLaBIOS continues with garbage.

The stack-overflow panel exists (174 bytes on kern_big, MEASURED) and handles
only stack overflow. This WP owns BR-8 and BR-9 (fit lens: one owner).

**Findings taken.**

- `gfce-1` [C] = `histsys-2` [K] + `isr-8` [C] (BR-9): **point int 0, and int 6
  on a 286+, at a generalised death panel.**
  - sch_diepanel takes SI = message and AX/DX = the evidence, and prints
    "DIVIDE ERROR TASK nn AT cccc:iiii NAME".
  - Never hook int 5 (PrtSc).
  - Reuse BOOT_MARK's mark_hook *shape*, not its vector list, which includes 5.
  - DOS.O88 banks IVT 00h, and a kernel booted by int 19h reinstalls it.
  - **Amended (cost lens; gfce-1 agrees): +40..+60 resident `.text`, not +30.**
    That covers 4-5-byte stubs, glue that pops IP/CS, loads DS and prints two
    sch_dphex4 values, and the message text (sch_s_over alone is 22 bytes):
    kern_big +45..+60, kern_small +40..+55.
  - **The install is +20..+30, not +12.** sched_init stores the constant
    KERNEL_SEG, not CS (os88ovlchk rule 2b), and each `mov word [es:d16],imm16`
    is 7 bytes. Installing int 6 unconditionally is safe on an 8088 and saves
    the tier test. It lands in `.ovl` on kern_big and in `.boot2` on kern_small,
    where sched_init lives.
  - kern_mega: its IDT already handles faults after the switch; only the
    pre-switch window would need the stub.
- `gfce-2` [C] **OWNER, before gfce-1 ships**: does the panel halt as a bug
  report, or does the faulting package close and the desk live? mega_fkill is
  the precedent. The second form is about +45..+65 more, recorded, and the 286+
  needs a skip decoder.
- `gfce-3` [C] **A divide ratchet**: tests/unit/t_divsites.py (fast) and
  tests/divsites.txt.
  - Register every div/idiv in kernel/ and drivers/ by file and label, with a
    reason: const, fenced <label>, validated <rule>, kernel-only, or **trusted
    by ruling**. FSX_SPK's N is unfenced by contract.
  - Counted: 95 div, 1 idiv and 7 xlatb in the kernel; 48 in drivers. Five
    kernel sites are unfenced today.
  - The list only shrinks. It can carry xlat and computed jumps. 0 bytes.
- `wm-1` [K] + `wm-13` [C] + `histsys-11` [C] (BR-8): **the window doors.**
  - **Amended (truth lens):** clamping before the div only swaps a dead machine
    for silent corruption. A wild BX maps to slot MAX_WIN-1 and the bodies
    still write through it (wm_show_b's `or [bx+W_FLAGS]`; wm_destroy unlinks
    another package's window).
  - So the containment is **wm_isrec with a CF refusal at the SHOW, HIDE,
    FRONT, DESTROY and CLIP_SET package doors** (+4..+6 each plus one shared
    exit: +22..+32), and at wm_wake's entry and MEMC_POST (+6..+12).
  - wm_isrec is bounds-only and does not check alignment (systemic truth lens).
    Say so in SPEC 11.
  - Then move wm_ptr2idx's bound **before** the div and delete the post-div
    clamp (fit lens: net about +2), so a kernel caller's bad BX also cannot
    fault silently.
  - Write the rule SPEC-first: package doors validate, inline setters trust.
    It reverses "BX is trusted, as OSAPI_WM_WAKE", so it is an SDK text change
    (watch list).
- *Measure first, not core:* `wm-2` [K], W_IDX in the record. The lenses
  together price it above its first draft:
  - WIN_SIZE 74 -> 75 makes every other record's word fields odd on kern_big's
    16-bit-bus machines, so pad to 76 (+12 `.bss`);
  - the fill is +10..+15 in the full blob (wm_init is `.boot2` on kern_big);
  - WIN_SIZE is pinned in os88geom.py and strides btndown.py, chromedown.py,
    dragcost.py, deskclip.py and os88dosdbg.py.

  Take it only if a counter shows wm_ptr2idx hot on the damage pass (one
  rebuild).
- `gfce-5` [C]: DESK.DRV's `.make` divides a package BX fenced only by
  plausibility. Run wm_isrec's bound first. +5 DESK.DRV.
- `gfce-6` [K]: **SYSTEM.CFG gets a MAX column** in CFG_MAP. An out-of-domain
  byte **skips** the store so the boot default stands (the verifier's
  correction: 0 is not every key's default).
  - It retires sysmisc-2 (BR-44's +5) in general.
  - Cost: +22 `.ovl` and +22 CTRL.DRV on kern_big; 0 resident.
  - It competes for `.ovl` (see the ledger).
- `gfce-7` [C]: cmz_jsayx tests `cmp ax, FM_NERR` (11), not 16. 0 bytes.
- *Optional:* `sched-9` (record): the panel trusts the table it calls suspect.
  +4.

**Files.** kernel/sched.inc (panel), kernel.asm (install in sched_init),
wm.inc, instance.inc (inst_wchk's same off-by-one comment), deskmod.inc,
driver.inc (CFG_MAP), ctrl.inc, compress.inc; tests/unit/t_divsites.py,
tests/divsites.txt; SPEC 8, 11, 20.8; docs/TESTING.md (int0sweep's meaning
changes from "detector" to "the panel appears"); apps/os88api.inc (WM_WAKE and
door text).

**Bytes.**

| | kern_small | kern_big | kern_mega |
|---|---:|---:|---:|
| panel + stubs (gfce-1) | +40..+55 | +45..+60 | 0 (IDT) |
| doors + wake fence | +28..+44 | +28..+44 | +28..+44 conv |
| bound before div | +2 | +2 | +2 |
| **resident total** | **+70..+101** | **+75..+106** | **+30..+46 conv** |
| transient | +20..+30 `.boot2` | +20..+30 `.ovl`, +22 `.ovl` (gfce-6) | +20..+30 `.ovl` |
| modules | - | DESK.DRV +5, CTRL.DRV +22 | DESK.DRV +5 |

**Value per byte.** It is the clearest growth in the phase. The panel turns a
dead machine into a named report on exactly the machines the project targets.
The doors stop a wild window pointer from unlinking another package's window.

**Risk.** Low-medium. The panel must not trust the scheduler tables. DOS.O88's
vector exchange must round-trip. A door refusal is a new answer on a path some
package may take wrongly, so the package sweep in the final pass matters.

**Rows.** soak `int0sweep`, `mega-int0sweep`, `stkpanel`, `dispclose`,
`dispclose-small`, `zonedmg`, `dmgcull`, `kdos`, `timerrace`, `cfgtrip`;
`mirror` (fast). **New:** a `tests/` package dividing by zero in a callback
gets the panel on a 5150 `_gla` profile and as `box-<row>` on 86Box's 286;
break the install and watch it time out. A wild-pointer WM call under
int0sweep, refused. `divsites` (fast).

**Dependencies.** WP7 (`.ovl` room). gfce-2's answer before the panel ships.

**Travels with.** Batch F (sched.inc). The door half lands in batch C's
session if C is first.

---

## WP9. A runtime API LEVEL

**Goal.** Close the gap SPEC 20.2.0.1 names under "What it does not cover". A
package built against cells newer than the running kernel far-calls past the
table's end, and a newer apps disk in an older machine is the ordinary case.
Only The Wire's own cell fence guards it today.

**Findings taken.** `abi-6` [C], `loader-6` [K], `gfr-5` [C].
**Amended (fit lens; gfr-5; flagday A4).**
- **Publish a LEVEL, not `osapi_table_end`.** Under T5 the rare cells leave the
  table, so a table end would stop describing the API.
  - The word is `dw OS88_API_LEVEL` at **KERNEL_SEG:0006**. That is padding
    today ("0x06..0x07 stay free", kernel.asm:2349-2352), so 0 resident. Name
    it by KERNEL_SEG, because L1 moves the segment.
  - One numbering: The Wire's WIRE_LEVEL/WIRE_CELL_LEVELS (os88wire.py:120-138).
- **Header byte 31 = the needed level.** os88pkg.py computes it as The Wire
  does.
  - The check goes in **ld_check_hdr after its ld_hdr_ok call**
    (loader.inc:428-433), **not in ld_hdr_ok**. ld_hdr_ok is shared with
    drv_check and the re-home path, and a driver's byte 31 is DRV_H_BSSP,
    usually nonzero (cost lens).
  - The check is about 9 bytes `.cold`: `cmp byte [es:si+31],LEVEL` (5), `mov
    al,LD_EVER` (2), `ja` (2). The answer is "Wrong os8088 version".
  - Old packages carry 0 and pass, so **PKG_FMT does not move**.
- **`OS88_NEED level`** for a package that tests at run time. It compares the
  word's value: about 9-10 bytes per opting package.
- **Byte 31 is the NUL of a legal 15-character name** (fit lens, A4). Cap
  names at 14 when the level is set, and update every host reader (watch
  list). No in-tree name is longer than 13.
- **The caveat is closed.** The mouse-instrument word left 0x0006 in 2ae27476
  (2026-08-24). PKG_FMT 10 arrived in b47de145 (2026-10-14), whose kernel.asm
  already says the bytes stay free. So every PKG_FMT-10 kernel reads 0 there
  and refuses `OS88_NEED`.
- **The header-bit ledger** lives here: LD_H_CLASS bits 3-5 are free; bit 5 is
  L7 (WP5) and bit 3 is mem-17 (WP15); new kernels mask the class with 0x07.

**Files.** kernel/kernel.asm (KERNEL_SEG:0006), loader.inc (ld_check_hdr),
instance.inc (the class mask), apps/os88api.inc (level, OS88_NEED, header
macro), tools/os88pkg.py, os88wire.py, os88disk.py, os88pkgsize.py,
os88rate.py, os88face.py, os88vbt.py; SPEC 20.2, 20.2.0.1.

**Bytes.**
- +8..+9 `.cold` on every kernel (kern_mega high); 0 for the word.
- About +9-10 per opting package.

**Risk.** Low.

**Rows.** `api-abi`, `abilock`, `pkg`, `installabi` (fast); soak `abicompat`,
`thewire`, `wire`, `stik`. **New host case in t_pkg:** a too-new package is
refused with LD_EVER; a 14-character name with byte 31 set round-trips through
os88disk.py.

**Dependencies.** None. **Land it before any WP that adds a cell** (WP2's hm-9,
WP15, WP16, WP18, WP22, WP27).

**Travels with.** WP26 (one session, sequential lock updates).

---

## WP10. Gates that catch wrong facts, and suite hygiene

**Goal.** Host-only checks that fail `make` on a class of error the tree is
already making. 0 shipped bytes. Each item is testable red-on-purpose
(WRITING-TESTS rule 1).

**Findings taken.**

- *Documentation gates.*
  - `engsys-2` [C] (BR-75): **heading drift.** Compare SPEC.md's {number:
    title} map at the merge-base against HEAD and list every citation of a
    renumbered title. **Amended (truth lens):** the six §46-for-§47 citations
    were already fixed on this branch by 14428309. Keep only the prevention.
    It must resolve into the `<PKG>-SPEC.md` files once WP24 splits SPEC
    (fit lens).
  - `engsys-3` [C]: a check over SPEC's ~178 fenced `NAME equ N` lines against
    the source. **Amended:** SPEC 3's values are already fixed (re-checked:
    KERNEL_SEG 0x0060, STK0_SIZE 512). SPEC 3 and 15.1 are generated or
    deleted in WP24, not hand-gated here.
  - `engsys-8` [K] (BR-76): basename collisions resolve in the citing file's
    directory first (checkdocs.py:223).
  - `engsys-9` [C]: checkdocs reads `.c`, `.h`, `.txt` and `.sh`, with C64-SPEC's
    bare § resolved per directory.
  - `engsys-6` [C]: a COUNTS table fed by suite.rows() and kernel.map; a prose
    count carries its moment.
  - `isr-12` [K] / `gfx-10` [K]: restore SPEC 1 rule 3's scope ("in anything an
    ISR can call") in CLAUDE.md and AGENTS.md. Optionally a tests/stisites.txt
    ratchet.
- *ABI and mirror gates.*
  - `abi-3` [C] (BR-62's gate half) and `gkm-2` [C]: t_api_abi `--kernel` over
    smallk, emuk and megak, with per-kernel FIXED rows (full tier). Under T5 it
    must also decode the rare table (flagday 3.1).
  - `abi-4` [K] (BR-146): API_RN_DSOFF beside api_rn. It pre-positions T5,
    which moves the frame by FLAGS.
  - `engsys-11` [C]: os88.h into t_mirror with a prefix map; os88.h and
    os88thunk.asm into PKG_FMT's fingerprint files.
  - `drivers-16` [C]: VX/XM/HD versions fingerprinted (XM_ABI_VER for WP34,
    HD_ABI_VER for WP33); XMEM's mirrored pair in one shared include.
  - `md-5` [C]: t_mirror scans kerndos/*.inc and kdos.asm with DIVERGENT rows;
    rename KDP_GDT to MPK_GDT.
  - gap `gi-8` [K]: a t_mirror row for os88m83.inc's legal-character set
    against dskw_syms; an include guard in os88pit.inc.
- *Assembly-time asserts (0 bytes).*
  - `ui-4` (BR-111): ui_cmd's table length.
  - `shell-6` (BR-117): `MENU_FILE_REM` and its `%if`.
  - `text-6`: fpg_warm adjacency.
  - `md-12` (BR-170): mega_dreg_add's worst case.
  - `gfx-7` (BR-152) [C]: **Amended (truth and cost lenses):** VID_APPLY is
    expanded three times (bvs_apply in the blob, vid_apply in CTRL.DRV, ext_apply
    in EXTD.DRV), so a run-time check costs 8..13 bytes in each, 8 of them in
    the full blob. The rows are static (VID_TAB_ROWS), so assert "4 planes
    implies stride == ROW_BYTES" with an assembly-time `%if` (0 bytes). The one
    run-time-written row, VESA.DRV's vid_vxrow, gets its check in the driver if
    a planar row is ever published.
- *Structure gates.*
  - `engsys-12` [C] (BR-171): the last section directive of each kernel .inc
    is the one active at its `%include`.
  - `engsys-13` [U]: a shared ratchet helper with a `warn:` when a registry
    total rises.
  - `engsys-14` [C]: gates read the `ks:`/`ksm:` lines or kernel.map instead of
    regexing source.
- *Suite hygiene.*
  - `engsys-7` [K]: an owner or family on every fast/full row.
  - `engsys-15` [C] (BR-172): refuse a selection covering more than ~90% of the
    tier without `--user-asked` (the threshold is the owner's).
  - `engsys-16` [C]: `builds=` per mode.
  - gap `ul-10` [C]: **register the rows no deferred measurement has:**
    - `stkdie` (BR-4's stack depth, Bounce over a second window);
    - a `uilat --workers N` leg (WP18's boost);
    - `xferfreeze` (WP18's sch_lock release);
    - `ifoff` with `--regrow` and `--mouse` legs (WP13; BR-12 and BR-21).

**Owned elsewhere.** `mc-10` (BR-68, batch H): its counter is +8..+10 `.mega`
plus **+2 conventional `.bss`** for `[mega_kfix]` beside `[mega_infault]`
(cost lens).

**Files.** tools/checkdocs.py, abilock.py, os88test.py, os88soak.py;
tests/unit/t_mirror.py, t_api_abi.py, t_registry.py, t_kernbudget.py, a new
t_sections.py; tests/suite.py; CLAUDE.md and AGENTS.md; kernel/ui.inc, desk.inc,
desksc.inc, fprog.inc, viddet.inc (the `%if`), megados.inc; kerndos/kdpark.inc;
apps/os88pit.inc.

**Bytes.** 0 on every kernel.

**Risk.** Low.

**Rows.** These are rows: `checkdocs`, `docindex`, `mirror`, `api-abi`,
`abilock`, `registry`, `asmrules`, `kernbudget`, `canary` (fast), plus new
`sections`, and api-abi's per-kernel mode in full.

**Dependencies.** None. **Run it first**: WP3, WP5, WP24 and WP31 lean on it.

**Travels with.** Batch A's host-gate list is this WP's.

---

## WP11. The KHP round: take, or move to a patch, every measured knob

**Goal.** No indefinite shelf. LAST-DROP-PERF 7.1 measured these knobs on the
4.77 MHz XT. kern_mega carries rows 1-11; kern_big, the XT's build, carries
none but KHP_LZ. **Amended (fit lens): this is a request to the owner for a
bigger round, not the review's default.** The owner's bar for khp1 was "around
100b" (docs/plans/khp1/LOG.md L).

**Findings taken.** `sysmisc-9` [K], `wm-11` [C], `gfx-6` [C], gap
`cursor:mc-3` [C] (KHP_VGASAVE, the 21,450-cycle VGA pointer), gap `gkm-10` [K]
(each knob is a kern_big-bytes-for-XT-cycles question; LAST-DROP-PERF 7.1
already prices it, so only the decision is left).

| row | knob | bytes (MEASURED) | buys on the XT | recommendation |
|---|---|---:|---|---|
| 2 | KHP_RELIT | +32 | **Amended (cost lens):** about **-42 ms** on CGA over the shipped kernel (672 -> 629). The first draft's -152 included wm-1, which already ships | take |
| 4 | KHP_DMGSTALE | +38 | a drag over a hidden window -12..-32% | take |
| 5 | KHP_CAPTION | +34 | -3.5..-8.5 ms a CGA raise | take |
| 3 | KHP_VGASAVE | +122 `.vga` | VGA raise save 133 -> 99 ms; cur_move 21,450 -> 15,100 cycles (mc-3) | take |
| 6 | KHP_VGAFILL | +39 `.vga` | GFX_FILL 256×128 13.37 -> 10.67 ms | take |
| 8 | KHP_VGAEDGE | +21 `.vga` | VLINE 128 -21%, FILL 64×64 -13% | take |
| 7, 9, 11, 12, 14, 16 | RAISEOL, VGABLITP, FRAME, MONORECT, MONOSCROLL, MONOPAT | +90, +32 `.vga`, +26, +344, +56, +22 | as LAST-DROP-PERF 7.1 | the owner's per row; a refused arm moves to `docs/plans/khp1/patches/`, never just deleted |

**Amended (all three lenses): how to land it.**
- **kern_small.** RELIT (blank.inc:549), DMGSTALE (wm.inc:4951), CAPTION's
  wm-2 half (wm.inc:7810-7890) and RAISEOL are **shared with kern_small**.
  Re-key them as KERN_BIG / `%else`, with the old arm staying as kern_small's
  body (0 kern_small). Deleting the losing arms costs kern_small about +74
  (wm-2 +10, wm-4 +26, wm-6 +38) against its 64KB rule; that is a separate,
  dated owner decision.
- **kern_mega** defines VGASAVE, VGAFILL, VGAEDGE, VGABLITP, FRAME, FPG,
  RELIT, CAPTION, DMGSTALE and RAISEOL (kernel.asm:291-304). An arm kern_mega
  carries cannot be deleted. "kern_mega 0" holds only that way.
- **Owner record.** khp1 LOG L.3: "...we could make them a knob for them, with
  them off for now". A refused 1bpp arm becomes a patch file, the owner's other
  option and disk-4's precedent.
- Add +0..+3 for hot-path parity pads.
- The +182 `.vga` counts against kern_big's VGA window: **6,946 bytes left**,
  which is the binding form of KERN_CODE_MAX (gkm-11).

**Files.** kernel/kernel.asm:252-307 (the KHP block), wm.inc, softgfx.inc,
vga12.inc, mouse.inc (VGASAVE arms in `.vga`), blank.inc:540-582, fsx.inc:500;
docs/plans/LAST-DROP-PERF.md (date each decision), docs/plans/khp1/patches/;
SPEC 5.7.

**Bytes.**
- kern_small 0, with the arms kept under KERN_SMALL.
- kern_big **+104..+107 on every machine, +182 `.vga`, so +286..+289 on the
  gauge**, if the owner takes the round.
- kern_mega 0.

**Risk.** Low. The arms exist and were measured on the target.

**Rows.** `khpknobs`, `knobself` (full); soak `buildmatrix`, `glyphcost`,
`dispsaver`, `dispfsx`, `dispfsxcga`, `dispfsxherc`, `zonedmg`, `dmgcull`,
`dispzoom`, `xorrect`, `vgadrop`, `vgadirty`, `kernresident`. A
`m.flicker()` pointer sweep on `os8088_xt_vga` for VGASAVE. The A/B leg against
the starting commit runs in the final pass.

**Dependencies.** The owner's answer.

**Travels with.** Batch B (display), if in flight.

---

## WP12. FAT write path: free count, contiguous runs, a bracketed flush

**Goal.** Cost disk work in calls. Free space is recomputed by walking the whole
FAT. Allocation fragments one cluster at a time. A multi-file delete pays two
FAT writes per file.

**Findings taken.**

- `histsys-3` [C] **Cache the free-cluster count.**
  - dsk_free_clus_x walks the whole FAT on every DFREE, VOL_STAT and mount:
    ~105 ms CPU on a 20MB disk. FTPD once spent 44% of an upload there.
  - Adjust the count in dskw_setfat, the single FAT writer (diskw.inc:4854).
  - **Amended (cost lens):** dskw_setfat never extracts the old entry, so the
    zero/non-zero transition costs a shift/mask read on both arms.
  - One word cannot survive A: and C: alternating, so keep **a word per
    volume** or a volume tag. Drop it on refat and on the redirected FSV_DFREE
    path.
  - +35..+50 `.cold`.
  - It also makes gi-2's grain cell (WP16) and DFREE cheap.
- `dw-15` [C] **Defer the free-side flush, but only inside multi-op
  brackets.** **Amended (fit lens):**
  - A single delete still flushes once per operation either way. The ~0.8 s
    saving exists only for rmtree, a multi-select delete or a replacing copy.
  - Deferring every free to gfx_unlock would give up dskw_relold's invariant
    "a commit point leaves the window CLEAN (dsk_fatw_park relies on it)"
    (diskw.inc:965-968) on every path.
  - So confine the lazy state to dskw_rmtree and OSAPI_BATCH_BEGIN/END, and
    flush at the bracket's end. That needs no test in the hot gfx_unlock.
  - **Amended (truth lens):** a deferred flush that fails gets **one attempt**:
    drop the dirt (lost clusters, which is safe) or poison as dws does, never a
    retry at every unlock. Rewrite the two written invariants at disk.inc:5786-5790
    and diskw.inc:3566-3570 in the same commit.
  - +15..+25 `.cold`, 0 `.text`.
- `dw-14` [C] **Allocate a contiguous run when N is known.**
  - **SPEC first (fit lens):** SPEC 18.4's "no attempt to defragment while
    writing" (SPEC.md:34844-34848) is rewritten as a dated decision.
  - **Amended (cost lens):** with FAT12 and FAT16 arms, window crossing and the
    first-fit fallback, it is +100..+160, not +70. The FAT12 arm is the one that
    matters: ~100 ms a fragment is a floppy cost.
- `dw-13` [K]: refuse FERR_FULL before the first data sector, as a ceil(rem/spc)
  compare against histsys-3's count. +10..+15 on top of histsys-3 (+18 without
  it).
- `dw-16` [C]: reset the rover only on the non-keepcwd mount arm. 0..+3; the
  optional FAT12 window scan is +40.
- `dw-17` [C] (rmtree finds each file once), `dw-20` [C] (DSKW_PUSH6/POP6; do it
  first): 0 bytes.

**Owned elsewhere.** BR-133 (dw-5, batch E: a failed flush keeps its dirt)
**lands first**. BR-17 (dw-2, batch E) interacts with dw-13 and dw-14.

**kern_small.** It gets none of histsys-3, dw-13 or dw-14, so its whole-file
writes still find FERR_FULL late. Say so in SPEC 18.4.

**Files.** kernel/diskw.inc, disk.inc, fsck.inc (fsk_findrun's idea); SPEC
18.4, 18.4.5, 18.4.10.

**Bytes.**
- kern_small **0..+3** (dw-16).
- kern_big **+160..+255 `.cold`**: histsys-3 +35..+50, dw-15 +15..+25, dw-14
  +100..+160, dw-13 +10..+15, dw-16 0..+3; +40 for the optional FAT12 scan.
- kern_mega 0 conv, +160..+255 high.

**Risk.** Medium-high: the data-integrity path. Verify with `os88disk.py
--verify` and a host mount.

**Rows.** `cswrite` (fast); `sysfree` (full); soak `wseq`, `wseqheld`,
`wsequnclosed`, `wseqinter`, `wseqdeleted`, `wseqcut`, `wseqioerr`, `wseqfull`,
`dtwrite`, `fsckdisk`, `fsckhd`, `bigvol`, `fatwpin`, `ftpd`, `fcpcopy`,
`diskclone`, `mega-wseqfull`, `mega-dtwrite`. **New:**
- a 50-file rmtree's int 13h count, before and after (a counter, not a timer);
- a swap inside a batch;
- a pulled floppy during a deferred flush (one attempt, then quiet).

**Dependencies.** Batch E's BR-133 and BR-17 first.

**Travels with.** After batch E.

---

## WP13. The IF=0 budget: a rule, a gate and an instrument

**Goal.** No IF=0 bound is written anywhere. CLAUDE.md's third defect class,
input overrun, has no number to hold anyone to. **Amended (ownership):** the
fixes (BR-12 regrow at IF=0, BR-21 cursor in the serial ISR, BR-51 spawn fill,
BR-54 planning walks, BR-66 LDT walks, BR-88 PrtSc, BR-155 the spinner) are the
batches'. This WP is the rule that judges them and the instrument that measures
them.

**Findings taken.**

- `isr-14` [C] **The rule and the gate.**
  - SPEC 1: IF=0 lasts at most ~500 µs and never surrounds a caller-sized copy;
    copies hold sch_lock at IF=1.
  - tests/unit/t_ifwin.py (fast) flags rep/loop/mem_b* copies inside
    pushf/cli…popf regions. tests/ifwin.txt only shrinks.
  - Its first catch is BR-12 (mem_regrow path 3's whole copy at IF=0: 86 ms per
    32KB by the kernel's own figure, memory.inc:2046).
- `ctx-4` [C] **Per-class bounds and an instrument.**
  - SPEC 7 gets the bounds: XT 8 ms; SERIAL.DRV at 9,600 on an 8250 documented
    as sharing the CPU with an ISR-drawn arrow. These are arithmetic from the
    8250's one-byte holding register, not measurements.
  - An **IFLAT=1 knob**, in a private tree: sch_isr records max(period -
    latched count) at entry, at divisor 2048 (sch_account already latches the
    PIT).
  - A soak row `ifoff` (ul-10's name) on `os8088_5150_cga_gla` and
    `os8088_xt_vga`: a 32KB regrow, an OSAPI_MEM_AVAIL over 16 claims, and
    continuous mouse motion, asserted against the bound. Pair it with
    `serlease` at divisor 12. Break it by reverting BR-12.
- `isr-10` [C], `mem-11` [C]: correct SPEC 28's "longest IF=0 window" (regrow,
  the driver-image move at ~17-50 ms, cur_move in mou_isr and task_spawn's fill
  are all longer) and the driver-image move's note (~6-7 bytes into a
  1200-baud 8250's one-byte buffer).
- `isr-1` [K]: document the XT Pause's clock loss, or keep IRQ0 open when
  Ctrl+NumLock is the scan (+14, optional).

**Owned elsewhere.** BR-12, BR-21 (measure first: serlease at divisor 12 with
the mouse moving, taken once by whoever decides), BR-51, BR-54, BR-66, BR-88,
BR-155 (batches A, D, F, H). The cursor figures to quote:
- MEASURED: 11,500 cycles CGA and 15,100 VGA (isr.md:670);
- MEASURED: 21,450 kern_big VGA without KHP_VGASAVE;
- systemic truth lens: if BR-21's rethink draws at IF=1 on MOUPRIV, MOU_PSTK
  must be resized for nested IRQs, +64..+128 `.lowbss`, or leave MOUPRIV before
  the `sti` (+~6).

**Files.** SPEC 1, 7, 28, 66.3; PERFORMANCE.md; tests/unit/t_ifwin.py,
tests/ifwin.txt; kernel/sched.inc (the IFLAT knob); tests/suite.py (`ifoff`).

**Bytes.** 0 shipped. isr-1's +14 is optional.

**Risk.** Low.

**Rows.** New `ifwin` (fast), `ifoff` (soak); soak `serlease`, `kbdburst`,
`kbdpreempt`, `kbd4f`, `uilat`, `schacct`.

**Dependencies.** None. It flags BR-12 until batch D lands it.

**Travels with.** Batch F's session (sched.inc), with batch D's BR-12 the first
catch.

---

## WP14. Routine contracts and the liveness lint

**Goal.** Make register discipline (hard rule 3) checkable.

**Findings taken.**
- `engsys-19` [C]: fix a header grammar (in/out/clobbers/flags/lock) over the
  ~1,480 `; out:` and ~900 `; clobbers:` lines, which drift today (wm_content's
  header against its W_X return). Generate an autodoc-style routine reference.
- **Amended (gap `ul-1` [C]):** the payoff the first draft promised (4,791 bytes
  of push/pop and an "AX is scratch" convention) is **refused on evidence**.
  247 of the 512 bytes of AX entry-saves are in routines whose 226 caller sites
  read AX afterwards.
  - A prototype interprocedural liveness pass proves 79 saves (127 bytes) dead
    at every direct caller. Only **~93 are removable**; the rest exit through
    the shared kret ladder.
  - Build `tools/os88live.py` over a per-build `-Le` listing (kern_big,
    kern_small, kern_mega). It models fall-through entries, the kent/kret
    ladders, the OSAPI table and tail jumps.
  - Run it as a **lint**: a soak row over the register-naming `clobbers:`
    headers, which prints the dead-save list. ul-1's verifier notes many of the
    509 headers name only the output, so the lint's real input is smaller.
  - Take the ~93 bytes inside the next size pass, not as a project, and do not
    count them here.

**Files.** tools/os88contract.py (grammar), tools/os88live.py (lint), docs/
(the generated reference, indexed by os88index.py), header fixes the lint
reports.

**Bytes.** 0. The later size pass saves about -93..-127 on kern_big, one time.

**Risk.** Low. Report-only first.

**Rows.** `stkbalance`, `stkwalker` (fast neighbours). A new report-only
`contracts` row in soak, promoted to a fast ratchet once clean.

**Dependencies.** None. WP19's tags reuse the grammar.

**Travels with.** WP19 (one session).

---

## WP15. SDK-side work in packages

**Goal.** Bytes and correctness in the shared package includes, and two small
SDK additions that retire a standing rule violation.

**Findings taken.**

- `mem-17` [K] **Region movability as a header bit.**
  - 69 OS88_REGION_MOVABLE lines in 67 files cost 10-19 bytes each plus a far
    call.
  - **Amended (fit lens): the flags byte has no free bit.** Use LD_H_CLASS bit 3
    (WP9's ledger). An old kernel then gives the package the largest stack
    class, which is safe.
  - **Amended (cost lens):** the loader's bit test plus a sentinel MC_RLOC
    store, plus the sentinel test at every proc-dispatch site (mem_reloc_call
    and kern_mega's lift path), is **+12..+20 `.cold`**. The saving is -10..-19
    per image *unpacked*: under lz4 and cluster rounding the disk gain is often
    0 per file, and the real gain is ~-19 resident per running instance.
  - **Amended (truth lens):** it covers only the `ret`-proc sites. scribe,
    skies and sheet keep their real fix-up proc.
  - The declaration point moves from "when your window exists" to load time.
    The WP must show that a compaction cannot then move a region with a live
    far-call frame during the entry proc or op_load.
  - Point t_movable at the bit.
- `hm-11` [K] **The parts loader stays resident in every non-loader parts
  package.** **Amended (truth and cost lenses):**
  - OSAPI_PKG_REHOME's KEEP form trims the **carve** (loader.inc:1626-1660), not
    the region. A non-re-homing package has no program part to re-launch. So
    OP_INITDROP needs a facility that does not exist: a self-shrink of the
    instance region that also rewrites I_SIZE (the fence at
    loader.inc:1251-1256). That costs kernel bytes, unpriced.
  - The droppable span is small for lazy-row packages. Browser, pixel, video,
    win16, wireflet and midirack keep op_fetch, op_read, op_lazyp and the rest
    resident while a part can still be fetched, so their load-only body is
    op_load's 25 bytes: -0..-25, net positive with the extra call.
  - Only a carve loader like xtmp3 has ~809 load-only bytes (MEASURED).
  - **So: price the self-shrink facility and prototype it on xtmp3 before
    anything else.** The first draft's -0.8..-1.5KB per instance is withdrawn.
- `text-15` [C] **Kernel-synthesised bold in font_run** (+35..+45 `.text`
  kern_big). **Amended (fit and cost lenses):** Word, Texpad, Frotz and cword
  still run on kern_small and on older kernels, so their case-6 overstrike sites
  and tests/textsites.txt rows **stay** until the bold exists on kern_small too
  (+35..+45 there, against its 64KB rule) or the packages refuse with WP9's
  OS88_NEED.
- `text-16` [K] **Real MENU_SEP and MENU_CHK prefixes.** kern_big -5. **Amended
  (cost lens):** kern_small needs menu_drop's +12 too, or new packages show
  selectable blanks there.
- `hm-12` [C]: MEM_OWNER_MAX 10 on kern_big (0 bytes; the table stays 32
  records); lift WIN16's arena allocator into apps/os88lmem.inc.
- `wm-16` [K]: WF_OWNBG is the documented expectation; a ratchet of non-callers.
  0 kernel bytes.
- Gap `gi-3` [C]: with WP16's grain cell, op_clbget banks CX (the READ_AT
  granule) and op_self masks with it. That stops rounding reads to 32KB
  clusters on kern_big, which brought back SPEC 18.4.4.2's cost. ~0 package
  bytes.
- Gap `gi-9` [C]: retire crt0's plain `.OVL` arm (no shipped package uses it),
  or give it the lazy arm's go-home bracket. 0 shipped.
- Gap `gi-10` [C]: os88_wseq_put uses `rep movsb` (+4 package bytes, ~50 -> 17
  clocks a byte).
- Gap `gi-7` [C]: os88pit.inc says pit_now is wrong inside an armed
  FASTTICK/RATE bracket. Docs; the SPEC half is WP26.

**Owned elsewhere.** `hm-7` (BR-59, batch D: +20 per parts image).

**Files.** apps/os88partsbody.inc, os88parts.inc, os88pself.inc, os88wseq.inc,
os88pit.inc, apps/cc/crt0.asm, apps/os88api.inc; kernel/loader.inc,
memory.inc, font.inc, menu.inc; tools/os88pkg.py; tests/unit/t_movable.py,
tests/textsites.txt; a new apps/os88lmem.inc.

**Bytes.**
- kern_small **+24..+32**: mem-17 +12..+20, text-16 +12.
- kern_big **+42..+60**: mem-17 +12..+20, text-15 +35..+45, text-16 -5.
- kern_mega +30..+40 conv, +12..+20 high.
- Packages: mem-17 -10..-19 per image unpacked; hm-11 unpriced pending the
  facility.

**Risk.** Medium. A shared include that ~21 packages build; rebuild and boot
them in the final pass.

**Rows.** `movable`, `textrules`, `pkg`, `abilock` (fast); soak `wdparts`,
`pxparts`, `rehome`, `rehomemove`, `rehomeabort`, `megarehome`, `sheetmsep`,
`assocopen`, and the parts packages' own rows.

**Dependencies.** WP9 (OS88_NEED; the header-bit ledger). WP16's gi-2 for gi-3.

**Travels with.** Batch D (loader.inc).

---

## WP16. Read path, directory and listing

**Goal.** Linear directory enumeration, geometry a caller can ask for, and a
listing that interoperates with modern hosts.

**Findings taken.**
- `dr-5` [C] **OSAPI_FILE_FIND_SEQ** with READ_SEQ's caller-held 16-byte
  cursor. N entries cost N(N+1)/2 visits today. +45 `.cold` +6 cell.
- Gap `gi-2` [C] **OSAPI_FILE_GRAIN.** AX = bytes per cluster, CX = READ_AT's
  granule (512 on kern_big, a cluster on kern_small), with no count and no I/O.
  - Today the only way to learn the cluster size is a full FAT count
    (DFREE/VOL_STAT, ~105 ms on 20MB).
  - It retires the DFREE call in three includes and enables gi-3 (WP15).
  - +14..+16 per kernel.
- `dr-11` [C]: a listing mount flushes only its own volume's chunks. +10.
- `dr-6` [U] / gap `ul-9` [U]: skip dsk_media_ok when the widget is armed. ~0
  bytes, about 4-8% of a cache hit. **Do not quote 250 cycles as measured.**
  Fold it into WP25's dr-16 sweep.
- `files-5` [K]: fm_measure walks the cache in place. 0..-6 `.cold`; ~35 ms per
  300-entry navigation PREDICTED (take the MartyPC figure in the final pass).
- `dw-21` [C]: mask LFN detection in rmtree; the defragmenter asks dsk_img_next
  by cluster. +2 `.cold`, +10 FSCHECK.DRV.
- `dr-10` [K]: correct `.bounce`'s argument; CX=0 to the inner fpg_busy. 0..+3.
- `dw-10` [C] as flagday **F3, phase 1**: the compression mark moves off
  NTRes's case bits. Today every packed file lists in lower case on NT, Linux
  vfat and macOS.
  - A dual-mark reader in dskw_czknow and the listing: +6 `.cold`.
  - os88disk.py writes the new mark. It is a shipped-byte writer (fork rule
    6), so every image is rebuilt.
- *Optional:* `dr-18` [U]: separate mount (validation) from listing. ±10, with
  WP25.

**Owned elsewhere.**
- `dr-3` (BR-61), `dr-14` (BR-141), `dw-11` (BR-136), `dw-12` (BR-60, on the
  owner's word): all batch E.
- Gap `gi-4`, the READ_SEQ cursor stamp, +20 `.cold`: batch E.

**Files.** kernel/disk.inc, diskw.inc, files.inc, fsck.inc, kernel.asm (the
cells), apps/os88api.inc, apps/cc (FIND_SEQ and GRAIN thunks), tools/os88disk.py;
SPEC 18, 19.

**Bytes.**
- kern_small **+22..+27**: gi-2, dw-21, dr-10, F3.
- kern_big **+77..+88**: dr-5 +51, dr-11 +10, files-5 0..-6, dw-21 +2, dr-10
  0..+3, gi-2 +14..+16, F3 +6.
- kern_mega +6..+12 conv (the cells), +70..+80 high.
- FSCHECK.DRV +10.

**Risk.** Medium. F3 touches every shipped image's directory entries; verify
with os88disk.py `--verify` and a host mount.

**Rows.** `diskverify`, `volsig` (fast); soak `assocfind`, `fsckdisk`, `fsckhd`,
`rdmount`, `imgmount`, `fmarrows`, `navcalls`, `curdisk`, `dirwsize`,
`shedrelist`, `fddpage`, `bigvol`, `lzload`. **New host check:** a packed file
lists in upper case on a Linux vfat mount.

**Dependencies.** WP9 (the cells).

**Travels with.** Batch E (disk.inc, diskw.inc, files.inc).

---

## WP17. Heap placement and the sorted-table prototype

**Goal.** Place growth where SPEC 50.2's two-ends rule says it goes, and
prototype the change that removes every n² in memory.inc. **Amended
(ownership):** mem-3/hm-8 (a doomed claim sheds everything; a table-full
refusal compacts first) are BR-53, batch D's, and they are WP2's prerequisite.

**Findings taken.**
- `mem-9` [K]: regrow path 3 puts every grower at the top. Use the lowest fit
  for bottom-up claims. +8 `.cold`.
- *Prototype behind a knob:* `mem-14` [C]: keep mem_tab sorted by base.
  kern_mega's selectors are paired with record INDEX (memory.inc:4376-4396), so
  re-key them or keep mega unsorted. -30..+20, unmeasured; 0 shipped until
  decided.
- Flagday **C1's correctness half** (truth and cost lenses): the KB-to-paragraph
  carry lives in shared KB helpers that the **kernel's own** callers keep:
  - disk.inc:9674, 9922;
  - files.inc:1306, 1821, 6071;
  - hiber.inc:2094, 2605, 2930, 4047;
  - mega.inc:415; font.inc:375;
  - diskops.inc:3195-3197; filecp.inc:801-803;
  - desksc.inc:1183; ascw.inc:122.

  Converting those ~20 sites to paragraphs is kernel-internal and needs no bump.
  It is what makes C1's argument hold at the flag day. Price it per site; it is
  mostly assembly-time constants (~0). mem-5 (+3) and md-16 (+1) are BR-105's,
  batch D.
- Gap `ctx-6` [K]: kern_small lets any task's claim shed, which the kern_big
  gate exists to forbid. SPEC 66.3.1 states the exclusion deliberately for
  *compaction*, but the shed it also stops is not argued. OWNER, in batch D: +14
  `.text` kern_small, or the rule written with the holders that defend
  themselves.

**Files.** kernel/memory.inc, compress.inc, and the C1 caller files above;
SPEC 50, 66.

**Bytes.** kern_small 0; kern_big +8; kern_mega 0 conv, +8 high. mem-14 0
shipped.

**Risk.** Medium: the heap is everyone's.

**Rows.** soak `heapmap`, `heapcheck`, `heapgran` (fast), `regrowshed`,
`megashed`, `cmemmove`, `memp`, `heaphi`, `wdheapmove`, `lzcomp`,
`mega-heaphi`; `tools/heapmap.py` before and after.

**Dependencies.** None.

**Travels with.** Batch D.

---

## WP18. Scheduler: UI boost, a wait primitive, lock honesty

**Goal.** Input should not wait behind every ready worker. A task should be able
to block on something other than time. sch_lock's rules should be numbered.

**Findings taken.**

- `sched-16` [C] with `histsys-4` [K]: **a one-shot UI boost.** sch_switch scans
  round-robin, so a press waits up to N × 55 ms behind N ready workers.
  sch_wake_ui sets `[sch_uiboost]` only when task 0 slept, and sch_switch spends
  it. **Measure first** with ul-10's `uilat --workers 2`. +10..+22 `.text`;
  mind t_hotpar.
- `sched-15` [C] **task_wake(AL = slot)**. ISR-safe; a sticky wake-pending bit in
  T_FLAGS bit 1 (free, zeroed by task_spawn). It enables an IRQ-driven SB refill
  and NIC-driven ETHER service.
  - **The lenses disagree on bytes:** truth +24..+30; cost **+40..+55 `.text`
    +6 cell**.
  - The cost lens counts what the first figure left out: the slot-to-record
    conversion (sch_currec's three shifts, ~10) and task_sleep's new pushf/cli
    test-and-clear (+12..+16).
  - Quote **+40..+55**, about 12 less if sch_wake_ui folds onto it.
- `sched-13` [C]: evq_pop's four `ss movsw` (0 bytes, ~35 clocks cheaper).
- `sched-11` [K]: one macro for the chain swap and one for the hook guard, so
  fsx_rtick gets BR-82's canary too. 0 bytes.
- Gap `ctx-8` [K]: "never block under sch_lock" becomes a numbered rule in SPEC 7.
  The tripwire in gfx_lock's `.block` arm is +7, or 0 as WP19's static gate.
- *Optional, measure first:* `histsys-5` [C] with gap `ctx-3` [C]: **release
  sch_lock across the UI task's floppy int 13h.** Three preconditions (ctx-3):
  1. the mouse ISR's "held by us" arm tests `cmp byte [sch_cur],0` (+7; it
     closes a bug the release would create);
  2. a disk-owner byte in dsk_xfer, so a second task yield-waits (+~16; rule 7's
     first runtime fence);
  3. skip dsk_xfer's inc only for sch_cur = 0, sch_lock = 0, a BIOS floppy row
     and not KERN_MEGA (+10..+15).

  Cost: +33..+40 (+7 `.text`, the rest `.cold`; 0 on kern_mega). The AT int 15h
  AH=90h hook is ~+25 and **must test SS == LOW_SEG and AL = 01h** (systemic
  truth lens). On SeaBIOS's own-stack int 13h the release buys nothing.
  **Measure both** a worker's fps and the UI-side wall time of a 16KB read on
  `os8088_5150_cga_gla`. A preemption between seek and read can cost a
  revolution (~200 ms).

**Owned elsewhere.** `sched-3` (BR-80), `sched-6` (BR-81), `sched-7`/`isr-9`
(BR-82, the ISR stack canary; its seed wants WP7's `.ovl` room): batch F.

**Files.** kernel/sched.inc, events.inc, ui.inc, fsx.inc, disk.inc and
mouse.inc (histsys-5), drivers/sound/sb.inc (refill on wake),
apps/os88api.inc; SPEC 7, 8.

**Bytes.**
- kern_small, kern_big and kern_mega conv each **+50..+77 `.text`**, plus a
  6-byte cell.
- The optional histsys-5 adds +33..+40, and +~25 with the AT hook.

**Risk.** Medium: the hot path, with t_hotpar parity.

**Rows.** `hotpar`, `stkclass` (fast); soak `kbdpreempt`, `kbdburst`,
`timerrace`, `timerbounce`, `stk0water`, `stkpanel`, `splashspin`, `vidsound`,
`midiracksb`, `midiracksbrate`, `skiessound`, `wakedrain`, `uilat`, and
`mega-` twins; new `xferfreeze` for histsys-5.

**Dependencies.** WP9 (the cell). Batch F (BR-80's lock text) in the same file.

**Travels with.** Batch F.

---

## WP19. Context tags, the call-graph gate, the checked build

**Goal.** Turn "UI task only", "lock held", "worker safe" and "ISR safe" into
something a tool reads. **Amended (ownership):** the fences that refuse a
violation are the batches':
- dw-4, BR-22 (E);
- loader-5, BR-30 (D);
- dr-2, BR-37 (E);
- ui-2, BR-28 (C);
- hm-14, BR-107 (D);
- the DF `cld`s, BR-86 (F);
- abi-7, BR-73 (C).

This WP is the model they are fences of.

**Findings taken.**
- `abi-8` [C] with gap `ctx-7` [C] **Machine-readable tags in each contract
  comment.**
  - The vocabulary is ctx-7's matrix: `; @ctx ui|ui-locked|any|worker|isr|hook|bracket`,
    `@blocks`, and `@moves` (the Mac's "may move memory").
  - Generate from the tags: SPEC 20.6's list, the SDK's list (the two disagree
    today), and the interrupt-time table isr-16 asks for (WP24).
  - **The gate**, on stkdepth's call graph: no worker root and no FSXF_RATE
    hook may reach a cell its tag forbids. It extends across the boundary: no
    worker root reaches wm_pkgcall or mod_drop, and no inc..dec span of sch_lock
    reaches a `@blocks` routine (ctx-8, statically).
  - It also answers T5's open question: are any rare cells called with IF=0?
- `abi-15` [C], `engsys-20` [U], gap `gfce-9` [C] **The APICHK=1 knob.**
  - Window-taking cells become same-length padded jump cells into a validating
    stub. The cell macros are 8 and 6 bytes (kernel.asm:2425/2442/2495), so the
    substitution fits.
  - File cells check `[sch_cur]=0`; lock-naming slots assert the lock; DF=0 is
    asserted at dispatch, reporting through SPEC 57's registry.
  - A soak row shaped like `smalllaunch` opens every package on the system and
    apps disks under APICHK and fails on any APICHK E9 line, plus an arm in
    `buildmatrix`.

**Files.** apps/os88api.inc (tags), tools/stkdepth.py, a new
tests/unit/t_apictx.py, tools/os88index.py, kernel/kernel.asm (cell macros under
APICHK); SPEC 20.3, 20.6.

**Bytes.** 0 shipped. The knob is bounded by guard 2.

**Risk.** Low (host side). The knob's soak run will report real package
misuse, which is the point.

**Rows.** `api-abi`, `abilock` (fast); new `apictx` (fast once clean); a new
`apichk` soak row; `buildmatrix`.

**Dependencies.** WP14 (the grammar).

**Travels with.** WP14.

---

## WP20. Graphics hot paths and the button's drawing order

**Goal.** Close the per-adapter holes in the primitives packages call most, and
the one shared control that letters after it fills. Measure each item first on
its own `gfxbench` row.

**Findings taken.**
- `gfx-4` [C]: gfx_points' planar loop in `.vga`. Every VGA point is a whole
  gfx_pixel today (640.87 µs, against 165.96 on the 1bpp loop); 5-10× is
  predicted. +60..+70 `.vga`.
- `text-9` [C]: the VGA ico_pass keeps its per-word column bookkeeping in
  registers: ~4.8 ms per 32×32 icon on the owner's 5150 with a PVGA1A
  (PREDICTED). +20 `.vga`.
- `gfx-9` [C]: the 1bpp XOR interior as a word loop. +8 `.text`.
- `gfx-8` [U]: clamp w to vid_cw - x once for a right-edge clip, and the even
  left clip too. +10..+22 `.text`.
- `gfx-15` [C]: gfx_clip_run stops at the first y1 > y2 under a y-sorted region
  (WP21's sort). +6 `.text`.
- `gfx-13` [C]: one entry macro for the whole-shape class (hide, enter, then
  dispatch), plus a GFXAUDIT assertion. ~0.
- `shell-9` [C]: a damage-rect early-out against the desk grid's x extent. +8
  `.cold`, +6 per DESK_GRID expansion.
- `shell-10` [C]: WIN95.DRV phases button art with `shr ax,cl`. +6 module.
- `gfx-11` [C]: the theme moves to kernel/theme.inc (mind t_hotpar). 0.
- Gap `os88ui:F1` [C] **The standard button fills, frames, then letters.**
  SPEC 13.14.6 rule 1 says otherwise.
  - Reorder: pen, frame and ring; measure and centre the label; then fill the
    interior and run the caption at once. The kernel arm banks the
    `[gfx_color]` caption ink before the fill's ink store, and CX/DX across it.
  - The interval falls to zero drawing calls, although the caption cells are
    still written twice (fill, then opaque run).
  - +8..+12 per copy: kern_big/kern_mega `.cold`, kern_small's module copies,
    and every package carrying os88ui.
- Gap `cursor:mc-6` [C]: ui_volume_service moves the pointer through cur_move
  under pushf/cli instead of hide + show. ~0 bytes.
- Gap `cursor:mc-9` [C]: **prove the lazy-hide protocol**, which is opt-out
  today, and its instrument was never committed.
  - Under GFXAUDIT only: before a restore, recompute (save|white)&~black for
    each banked byte, compare it with VRAM, and log a mismatch to CURLOG with
    the tick and the interrupted CS:IP. Do it on 1bpp.
  - A soak row fails on a non-zero count.
  - +~40 in the knob build, 0 shipped.

**Owned elsewhere.** Batch B's BR-3, BR-14, BR-45, BR-46, BR-57 and the rest.
Gap `cursor:mc-2` (kbm_move's draw gate lacks FPG_ARMED, +7): batch F.

**Files.** kernel/vga12.inc, softgfx.inc, icons.inc, desk.inc, winmod.inc,
mouse.inc, ui.inc, a new kernel/theme.inc, apps/os88ui.inc; SPEC 4, 5, 13.14.6.

**Bytes.**
- kern_small **+32..+44**: gfx-9, gfx-8, gfx-15, shell-9. F1 goes in its module
  copies.
- kern_big **+120..+146 on the VGA gauge**: +24..+36 `.text`, +8 `.cold`, F1
  +8..+12 `.cold`, +80..+90 `.vga`. A mono machine pays +40..+56.
- kern_mega +24..+36 conv, +16..+20 high.
- WIN95.DRV +6; CTRL.DRV and FDLG.DRV +8..+12 each on kern_small.

The `.vga` part counts against kern_big's 6,946-byte VGA window.

**Risk.** Low-medium. Look at a 1bpp adapter before calling it done (SPEC 39.4).

**Rows.** `hotpar` (fast); soak `gfxpoints`, `gfxptsmall`, `xorrect`,
`dispblit`, `dispblitp`, `blitp`, `blitcut`, `gfxlk`, `glyphcost`, `deskbench`,
`dispapps`, `rdicon`, `zonedmg`, `btnall`, `btncp`, `btngesture`, and the games'
rows for gfx-4. `m.flicker()` on a Disk-window header press for F1. A new
`curaudit` soak row for mc-9.

**Dependencies.** WP21's region sort (gfx-15).

**Travels with.** Batch B.

---

## WP21. The damage pass

**Goal.** Tidy the damage pass's implicit calling convention, then measure, and
only then prototype, the redraw-less-than-the-closure rethinks.

**Findings taken.**
- `wm-17` [C]: one flags byte plus the rect words, cleared in one place at
  wm_paint_dmg's exit. It closes the class BR-47 (the empty-rect arm leaks
  one-shot arguments) is one instance of. -10 `.text`.
- `wm-12` [K]: transitive marking redraws windows that were never damaged.
  **Count .mbelow hits on MartyPC first** (one rebuild), then prototype a
  visible-region predicate behind a knob (+40..+80).
- `wm-R1` [C]: exact clipping (visRgn ∩ updateRgn), prototyped behind a knob and
  measured on MartyPC. Keep the region list y-sorted for WP20's gfx-15. 0
  shipped until decided.

**Owned elsewhere.** `wm-4` (BR-48), `wm-6` (BR-50), `wm-7` (BR-109), `wm-9`
(BR-108): batch C.

**Files.** kernel/wm.inc, ui.inc; SPEC 11.

**Bytes.** -10 on kern_small, kern_big and kern_mega conv. Prototypes 0 shipped.

**Risk.** Medium for wm-17: every early-out must clear through the one place.

**Rows.** `hotpar`, `chromesites` (fast); soak `zonedmg`, `dmgcull`,
`dispclose`, `dispzoom`, `dispband`, `sizesnap`, `deskzoom`, `zoomsave`,
`dispapps`, and `mega-` twins. PERFORMANCE.md Part 5's widget-drop row is the
bar for wm-12/R1.

**Dependencies.** None.

**Travels with.** Batch C (wm.inc); wm-17 and BR-47 in the same commit.

---

## WP22. IRQ ownership: the IMR shadow, PS/2 sync, and an optional cell

**Goal.** One kernel-side story for the 8259 mask. **Amended (ownership):** the
IRQ fixes are the batches':
- drivers-6's IVT scan, isr-6's IMR read-modify-writes and isr-17's OCW3 (BR-34,
  batches F/G);
- histsys-6 (BR-95), input-11 (BR-93), mem-7 (BR-52).

**Findings taken.**
- `isr-13` [C] **The IMR shadow alone** (+~25): per-owner bits, MASK/UNMASK
  through it. BR-34's IMR sites then route through the shadow instead of each
  gaining a pushf/cli.
- `input-10` [C]: PS/2 sync resets the phase when `[ticks]` has moved ≥ 2 since
  the last aux byte. +12 `.text` +2 `.bss`. **With WP3's rt-3 it lands in the
  PS/2 overlay**, 0 resident on kern_big; resident on kern_mega.
- *Optional, OWNER:* flagday **D8 phase 1**, `OSAPI_IRQ` set/unset as an
  appended cell.
  - **Amended (cost lens):** +100..+150 resident kern_big (a 16-row table at 7
    bytes a row is 112 `.bss`, plus code), not +100. SOUND.DRV -60..-100
    (MEASURED sbl_hook/unhook/cell spans: 120 bytes).
  - If it includes the common EOI/spurious stub, every hooked interrupt pays
    +60..+90 clocks. Keep that stub optional and off IRQ0 and the 8250 paths.
  - The flag-day saving against today is about 0, not -25. drivers-6's scan does
    not exist yet, and mem_rr_ivt also covers int 33h and package hooks.

**Files.** kernel/mouse.inc, sched.inc, snd.inc, driver.inc;
drivers/sound/sb.inc; SPEC 1, 9, 10.

**Bytes.**
- kern_small +25; kern_big +25 (+14 inside the PS/2 overlay); kern_mega +39
  conv.
- The optional cell: +100..+150 kern_big; SOUND.DRV -60..-100.

**Risk.** Medium. Vectors and masks are where a mistake hangs the machine; look
on 86Box with an AMI or Phoenix AT BIOS.

**Rows.** `drvmem` (fast); `ps2mouse` (full); soak `irqgrab`, `dosirq`,
`sbirq5`, `usbmouse`, `wheelps2`, `serlease`, `drvmove`, `heapdrv`, `dosmouse`,
`kdmouse`, `mega-irqgrab`, `box-ps2mouse`.

**Dependencies.** WP3 (rt-3) for input-10's home.

**Travels with.** Batches F and G (BR-34's halves). The shadow first, so their
IMR sites use it.

---

## WP23. Video facts

**Goal.** What is left once batches A and B own the detection and fullscreen
fixes (video-2 = BR-6; video-4 = BR-56; video-6 = BR-58; video-8 = BR-147;
video-12 = BR-151).

**Findings taken.**
- `video-13` [C]: EGA's caps row gains mode 0Dh, which every EGA has and which
  already has an fsx_mtab row. Gate it on one 86Box EGA run of a mode-0Dh
  consumer. 0 bytes.
- `video-14` [C]: zero-byte macros VIDK_PLANAR and VIDK_MONOCARD for the
  hand-spelled kind-class idiom at ~10 sites.

**Bytes.** 0. **Rows.** soak `vesa`, `vidpage*`, `dispfsx`, `wireballvideo`.
**Dependencies.** None. **Travels with.** Batch B (BR-151's FSIF_FLIP is the
one that needs a CLAUDE.md WireBall edit once the flag exists).

---

## WP24. Documentation: one copy of each fact

**Goal.** Every fact has one authority, and every copy is generated or deleted.
0 shipped bytes.

**Findings taken.**
- `engsys-5` [C] with `abi-10` [C]: the slot contracts exist in up to four
  places. Delete SPEC 20.3's 37-entry block and 53.8's, pointing at the SDK
  names. Cut each kernel cell comment to one line. Fix FULLSCREEN's SDK text in
  WP26.
- `engsys-4` [K]: SPEC's front matter (1-4, 15-17) is rewritten as current,
  with one paragraph and one gate per rule; the narrative moves to
  docs/history. **Gate SPEC 4:** every kernel/*.inc is in its table. **Amended
  (fit lens):** SPEC 3 and 15.1 are generated between markers here (WP10's
  hand-gate is not built over them).
- `engsys-10` [C] with `bytes-10` [C]: AGENTS.md is generated from CLAUDE.md
  with a substitution table and checked like docs/INDEX.md.
- `engsys-18` [C]: SPEC.md's package sections move to `<PKG>-SPEC.md`, keeping
  their numbers (WEAVE/C64/ARCADE precedent), with a generated TOC. **After
  WP10's heading-drift check**, which must resolve into the new files.
- `bytes-9` [K]: KERNEL-MEMORY.md's ladder and Task Manager tables are emitted
  by `--bless` between markers.
- `isr-16` [U]: the "callable at interrupt time" table in docs/INDEX.md,
  generated from WP19's tags.
- `engsys-21` [C]: the Makefile is split by subject into mk/*.mk.
- Gap records written down so they are not re-proposed:
  - gkm-5's merge-the-kernels table goes into KERNEL-MEMORY.md (+5,440 to
    +14,700 refused);
  - rt-4's nesting rule goes into SPEC 2.10.9 and MOVABLE-COLD-PLAN;
  - gkm-8's flatten-kern_small trade;
  - bytes-4's two-segment tax at its floor goes into SPEC 2.6.

**Files.** SPEC.md, the new `<PKG>-SPEC.md` files, AGENTS.md, CLAUDE.md,
tools/checkdocs.py, os88index.py, kernsize.py; docs/INDEX.md,
KERNEL-MEMORY.md, history/; Makefile and mk/*.mk.

**Bytes.** 0. **Risk.** Low, but large diffs. engsys-18 and engsys-21 are each a
session of their own. Verify by checkdocs and an unchanged `make help`.

**Rows.** `checkdocs`, `docindex`, `checkreadme`, `abilock`, `guidelock`
(fast); `guidelock-full` (full).

**Dependencies.** WP10 (heading drift), WP19 (tags, for isr-16).

**Travels with.** Nothing; documentation-only commits owe checkdocs alone.

---

## WP25. The comment discipline sweep

**Goal.** Seven reviewers independently found comments used as archaeology:
"it was X until size pass N", stale contract lines, retired designs narrated.

**Findings taken.** `gfx-12` [C], `wm-15` [C], `ui-15` [K], `sched-10` [C],
`text-14` [C], `video-15` [C] (the per-area sweeps, each with its doc_fixes in
data/*.json); `mem-12` [C] (~450 lines describe ten routines that no longer
exist); `dr-16` [C] (orphan headers, running counts; it absorbs dr-6/ul-9);
`input-16` [C] (22 references to the folded mou_claim); `sysmisc-12` [K] (WF_VIS
for the literal 2 at ~30 sites). The convention goes once into CLAUDE.md,
AGENTS.md and SPEC 1: a header states the current in/out/clobbers/invariants;
history goes to the commit, the SPEC section or the report. The gate: checkdocs
fails a backticked kernel-comment identifier that is neither in the map nor on
an allow-list.

**Files.** Every kernel/*.inc the sweeps name, tools/checkdocs.py, CLAUDE.md,
AGENTS.md.

**Bytes.** 0. **Prove it by `cmp`:** every shipped image is byte-identical at
one commit.

**Risk.** Low. Check the doc fixes the review's own task #2 already applied, so
nothing is done twice. Split by file across sessions.

**Rows.** `checkdocs` (fast). No emulator row can see a comment.

**Dependencies.** WP24 (the convention lands once).

**Travels with.** Nothing.

---

## WP26. Contract text: one abilock update

**Goal.** Make the SDK and SPEC say what the code does, wherever the code is
right and the text is silent or wrong. One `abilock --update`, no bump. This WP
also carries the text halves of the bug batches' SDK items, so the lock moves
once.

| id | the sentence |
|---|---|
| `abi-12` [C] / `abi-13` [U] | the verb-register rule for new doors (AL, or BL when AL is an argument); the naming rule for new cells |
| `abi-14` [K] | an X cell's pointer argument is "SI (in your DS; ES is not read)"; GFX_SPANS says ES:SI |
| `isr-3` [C] (BR-85) | OSAPI_WM_WAKE from an ISR requires SS = LOW_SEG; the SS-independent evq_push (+3, gap ctx-5) is batch C's code |
| `sched-4` [C], `sched-5` [K], `histsys-15` [K] (BR-78) | on the UI task TASK_SLEEP ends at the first input; its range is 1..32,768 ticks (re-derived: the first scan sees ticks+1, so AX = 0x8001 still sleeps its full length) |
| `wm-8` [C] (BR-78) | OSAPI_WM_TIMER: AX ≤ 32,767 ticks |
| `gfx-5` [C], `text-3` [C] (BR-77) | the pen after BLIT4 and after FONT_RUN is unspecified |
| `ui-8` [C] (BR-22) | the clipboard is **not** worker-safe |
| `ui-9` [C] (BR-115) | W_ONMOUSEUP "unless the event ring overflowed" (batch C may instead take the +12 dispatch) |
| `loader-10` [C] (BR-122) | PKG_START: pin a movable source claim across the call |
| `hm-2` [K], `mem-6` [K] (BR-104) | the self-region arm of mem_find_own is deliberate; record the owner's decision either way (closing it is +6..+8, batch D) |
| `histsys-17` [C] | the eight lock-bound display fields become gfx_dc, adjacent, cleared with two or three word stores (-4..-8; ABI-BREAK-LIST R6 gets a home) |
| gap `gfce-8` [C] | SPEC 18.4 gets the error CLASS table (DOS 59h style); the SDK preamble says CF=1 is the only universal refusal and AX=1 means "not on this kernel". **Not** one renumbered space. Flagday R9's contract half stays at the flag day: Recorder reads the stream stub's AX=4 (recorder.asm:754), and the bare-stc stubs preserve AX for OSAPI_SND_FM callers (truth lens) |
| gap `cursor:mc-5` [C] | OSAPI_GFX_LOCK only *promises* to hide the cursor since SPEC 7.1.4; raw framebuffer writers have no way to make it happen (an appended CUR_EXCLUDE cell is recorded) |
| gap `xm-12` [C] | name SPEC 41.8's context rule for XMEM ALLOC/FREE/COPY |
| gap `hdk-2` [K] | os88api.inc:3490: the HDD door reaches a partition erase, by SPEC 117.4's design |
| gap `gi-7` [C] | SPEC 53.2.1 and 72.15.1: pit_now is wrong inside an armed FASTTICK/RATE bracket |
| flagday R2-2 | the wording row |

**Files.** apps/os88api.inc, SPEC 7, 8, 13.9, 18.4, 20.3, 21.5, 41.8, 53.2.1,
55.4, 66, 72.15.1; kernel/vga12.inc (histsys-17).

**Bytes.** 0 for the text. histsys-17 is **-4..-8** on every kernel (kern_mega
conv).

**Risk.** Low.

**Rows.** `abilock`, `checkdocs` (fast); soak `mouseup`.

**Dependencies.** None. Land it after WP19's tags if both are in flight, or with
WP9, so the lock moves as few times as possible.

**Travels with.** WP9.

---

## WP27. Desk, dialogs and menus: additions

**Goal.** The user-visible additions and the one size saving left in this area
once batches C, E and G own its fixes (files-7/8/9/10, shell-7/11/12,
sysmisc-6, ui-16).

**Findings taken.**
- `ui-10` [K]: About's fourth line is composed from `[vid_w]`/`[vid_h]` with
  clock.inc's decimal formatter instead of a per-kind table. The table is 107
  bytes resident on kern_big and 79 on kern_small. It removes the class BR-45 is
  an instance of. -30..-40 `.text` kern_big, ESTIMATED; kern_small 0..-30,
  unestimated. Measure the span.
- `files-17` [U]: the chooser's completion callback gets AH = volume and BX =
  cluster. +6 (kern_small: FDLG.DRV).
- `shell-3` [C]: OSAPI_DESK_ITEM remove with AH=0FEh, "the row whose volume and
  path are this record's". +12 CTRL.DRV, WIN16 -12. It is the additive half of
  flagday C10. **Check what an older kernel does with AH=0FEh.**
- `shell-8` [K]: the boot reader regrows the shortcut claim to ceil(rows/8) KB.
  +10 `.boot2`; it is in the blob ledger.
- `ui-11` [C]: one double-click interval for the machine, read by the kernel's
  four detectors and set from the Mouse page. +5 plus a 4-byte inline cell.
- *Optional:* `ui-12` [U], a clipboard generation count (+4 `.text`, +2 `.bss`,
  +4 cell); `ui-19` [U], W_ONMENU (+14 `.text`, +24 `.bss`, +6 cell).

**Files.** kernel/apps.inc, fdlg.inc, desk.inc, desksc.inc, ctrl.inc, ui.inc,
clip.inc, apps/os88api.inc, apps/win16/w16progm.inc.

**Bytes.**
- kern_small -21..+9.
- kern_big **-15..-25**: ui-10 -30..-40, files-17 +6, ui-11 +9.
- kern_mega -21..-31 conv, +6 high.
- CTRL.DRV +12, `.boot2` +10. The optional cells add +34..+44.

**Risk.** Low.

**Rows.** soak `fdlgchoose`, `fdlgsmall`, `deskitem`, `desksc`, `stikmenu`,
`toast`, `clipkeep`, `mouseup`, the WIN16 shortcut row; `toast` (fast).

**Dependencies.** WP9 (the cells).

**Travels with.** Batches C and E.

---

## WP28. The Control Panel engine, shared module source, a scanner that sees macros

**Goal.** The largest module saving on offer, plus making modules share their
boilerplate as source and the tools see it.

**Findings taken.**
- `sysmisc-15` [K] **Declarative Control Panel pages.** A {species, rect, id,
  getter, setter, predicate} table per page plus a ~500-700-byte engine.
  -1,500..-3,000 CTRL.DRV, ESTIMATED and unprototyped; build/ctrl.drv is 15,405
  bytes. Prototype one page first.
- `shell-4` [K]: one DOCK_PAINT macro with `%if` arms reproduces both dock
  painters (0 bytes).
- `shell-5` [C] with `files-15` [C]: one kernel/modhop.inc for the five far-hop
  copies and two macro dialects (0 bytes).
- `files-16` [C]: os88ovlchk.py and stkbalance.py consume `nasm -E` per build.
  That unlocks ~-10 `.cold` in fm_brect on kern_big.
- Gap `os88ui:F2` [K]: the same tooling gap seen from os88ui.
  - TLABEL accepts a bare `%1:`.
  - `section <macro>` resolves through the includer's `%define`, which retires
    CPXN.
  - UI_HLINE gets a KFAR arm or an `%error` under OS88UI_KFAR (UI_GRAY is
    already a correct absolute far call).
  - Prove it by planting a resident `call os88ui_kbtn` under KERN_SMALL and
    watching make go red.
- `loader-12` [C]: two clear-prefix ladders, not three; fix cmz_cpre's comment.

**Files.** kernel/ctrl.inc, dock.inc, dockmod.inc, extmod.inc, winmod.inc,
stikmenu.inc, fdlg.inc, filecp.inc, a new modhop.inc, compress.inc;
apps/os88ui.inc; tools/os88ovlchk.py, stkbalance.py.

**Bytes.** 0 resident (-10 `.cold` kern_big after files-16). CTRL.DRV
-1,500..-3,000.

**Risk.** Medium for sysmisc-15 (every page); low for the rest.

**Rows.** `ovlchk`, `stkbalance` (fast); soak `cppromise`, `cpup`, `cpcost`,
`cpnames`, `cplistrow`, `cpnameshdd`, `dockmodule`, `dockmark`, `dockpos`,
`extdmod`, `fdlg*`, `fcp*`, `btncp`, and `mega-` twins.

**Dependencies.** None.

**Travels with.** Nothing.

---

## WP29. DISKOPS and HIBER module work

**Goal.** The module-side items left once batch G owns hiber-7 (BR-10),
hiber-11 (BR-101) and hiber-12 (BR-72).

**Findings taken.**
- `hiber-20` [C]: DISKCOPY's "copy another" keeps the claim at CLA_DONE, so the
  next copy is a one-trip clone. +40 DISKOPS.DRV.
- `hiber-14` [C]: kern_big banks the 8259 masks (21h/A1h) into module data and
  restores them at wake step 1, so every kernel has one model. +14 HIBER.DRV.
  **With batch G's BR-96 (hiber-1).**
- `hiber-15` [C]: on kern_mega in PM, hbm_arm's room check uses kern_mega's own
  image size. +20 kern_mega HIBER.DRV.
- `hiber-13` [K]: ~25 bare `[cs:]` module stores become CSDO/CSEG. That is free
  on kern_big and saves a fault per store on mega.
- `loader-11` [C]: cmz_gamma searches its top bit upward (~10-15% of compress
  time, ESTIMATED). Take lzcomp's cycle count once if it decides anything.

**Files.** kernel/hiber.inc, hbstub.inc, diskops.inc, compress.inc.

**Bytes.** 0 resident. DISKOPS.DRV +40; HIBER.DRV +14 (kern_mega's +20 more).

**Risk.** Low.

**Rows.** soak `hibernate`, `hibernatedrv`, `hibernatem`, `p2hiber`,
`hmahiber`, `megahiber`, `megap2hiber`, `stikhiber`, `diskclone`,
`mega-diskclone`, `lzcomp`, `mega-lzcomp`.

**Dependencies.** Batch G's BR-96.

**Travels with.** Batch G.

---

## WP30. One clock (prototype)

**Goal.** One time base. The kernel integrates a clock of its own, while DOS,
kern_dos and the BIOS use 0040:006C.

**Findings taken.** `sysmisc-14` [C].
- Derive the time of day from 0040:006C: keep only the date and the last count,
  derive h:m:s on format, detect midnight as count < last, and write the count
  back with int 1Ah AH=01h.
- A ~45-byte breakdown shared by clk_fmt and osapi_clk_get_x replaces ~55 bytes
  of accumulator and carry: -10..+10, ESTIMATED; prototype.
- **Caveat:** the nested-tick `.tskip` skips the ROM chain but still bumps
  `[ticks]`. Count sch_chskip on the target first.
- It retires the class of BR-15 (the clock gains ~31 s/day), BR-26 (the user's
  setting never reaches 40:6C) and the hibernate catch-up walk. **So fix BR-15
  now** (two immediates) and let this retire the code.

**Owned elsewhere.** `sysmisc-4` (BR-157), `sysmisc-5` (BR-158): batch G. Their
+24 `.ovl` is the ledger's candidate to move out of `.ovl`.

**Files.** kernel/clock.inc, clockw.inc, ctrl.inc, hiber.inc,
mega.inc:1362-1369, sched.inc/fsx.inc (the skip count).

**Bytes.** -10..+10 resident on every kernel.

**Risk.** Medium: midnight, hibernate, and DOS's view of time.

**Rows.** soak `deskclock`, `deskclockherc`, `mega-deskclock`,
`pkgmove-deskclock`, `tickzero`, `hibernate`, `kdos`, and docs/TESTING.md list
item 6's QEMU reboot assertion.

**Dependencies.** Batch G's BR-15, BR-26.

**Travels with.** Batch G.

---

## WP31. The kernel matrix: four kernels, one honesty (new)

**Goal.** The four kernels stay separate build-time products. gkm-5 prices the
merge at +5,440 (kern_small into kern_big) to +14,700 (kern_mega's PM code on
every XT), and refuses it. What is missing is a check that keeps each kernel's
separate code honest. Of the review's 64 code bugs, 32 live in code only one
kernel or kernel group carries, and 5 are true divergence.

**Findings taken.**
- `gkm-1` [K] **A rule:** a fix under `%ifdef KERN_MEGA` says why its cause is
  not in shared code. If the cause is shared, the fix lands unconditionally in
  the same commit. It goes into KERN-MEGA-PLAN's checklist. The two shipped
  instances are batch G's: BR-32 classk's row count (+5 `.cold`) and BR-38's
  `.badv` (+~20 HIBER.DRV on kern_big; the verifier corrected the evidence).
- `gkm-2` [C] **Refusal stubs name their contract.** OSAPI_XREFUSE takes an
  argument naming the cell's refusal shape (KEEP = stc/retf, or AX=FERR_x). It
  is used on four cells, two of whose contracts differ. 0 bytes. The four-kernel
  ABI gate is WP10's abi-3. With it, gap `gi-6` [C]:
  - move dwf_dskw_write_at's label up onto `mov ax, FERR_NAME` (0 bytes);
  - point kern_small's osapi_vol_stat at a stub that answers FERR_NODISK (+4).

  It is the kern_small half of flagday R9, and fixes os88data's bank, which
  never saves a walk on a kern_small write.
- `gkm-3` [K]: tests/smallport.txt and a `small-` prefix that keeps the base
  row's marty need. That needs a parser change in `_ports` (suite.py:21775-21777),
  not only a list. Seed it with desk, diskw, disk, memory, files, wm, fsx and the
  FCP/FDLG seam. kern_small has 21-28 soak rows against kern_mega's ~390-430.
- `gkm-4` [C]: t_hotpar pins `HOT['mega']` when build/megak is current. Replace
  the 15 build-gated pads with one `HOTPAR even|odd` macro on the self-aligning
  precedent (vidext.inc:103, font.inc:620). 0 net.
- `gkm-7` [C]: a ratchet on raw build-name tests (KERN_BIG 638, KERN_SMALL 126,
  KERN_MEGA 406, KERN_EMU 21 sites). Build names may appear in kernel.asm's
  define block and mega*.inc; every other site is registered with a reason and
  moves to a named capability symbol as its file is touched. No bulk conversion.
- Gap `rt-5` [C]: kernsize prints **per-class** resident figures (VGA, mono
  8088, ROM/HMA-adopted) and the blob/`.ovl` tally lines the ledger above wants,
  and `kernresident` gains a mono-machine row in soak. KERN_BUDGET stays the VGA
  ceiling.
- *OWNER, prototype first:* `gkm-6` [C], **retire kern_emu.**
  - Rebuild VMMOUSE.DRV as a DRVC_POINT worker driver on kern_big, the way
    USBMOUSE.DRV works.
  - About +40 kern_big resident (a drv_tab row, a name, a CFG bit; DRV_MAX 6 ->
    7) against -385 kern_emu-only resident and one kernel build retired: make
    emu, build/emuk, emu.img, its kernsize leg and buildmatrix leg.
  - Unproven: that a worker rotation pumps a drag's spin loop as promptly as
    vmm_poll inside task_yield (SPEC 9.11.3). Prototype against the `vmmouse`
    row before removing any kernel code.

**Files.** tests/unit/t_hotpar.py, t_api_abi.py, a new build-name ratchet with
tests/buildnames.txt; tests/suite.py (`_ports`), tests/smallport.txt;
kernel/kernel.asm (OSAPI_XREFUSE), diskw.inc, snd.inc (the stub); tools/kernsize.py,
tests/kernresident.py; docs/plans/KERN-MEGA-PLAN.md.

**Bytes.**
- kern_small 0..+4 (gi-6).
- kern_big 0, or +~40 with gkm-6 (and kern_emu retired).
- kern_mega 0.

**Risk.** Low, except gkm-6 (medium, owner's call).

**Rows.** `hotpar`, `api-abi` (fast); `kernresident` (full, plus its new mono
row); soak `vmmouse`, `buildmatrix`, the seeded `small-` rows.

**Dependencies.** WP10 (abi-3's `--kernel` mode).

**Travels with.** Batch G (the two gkm-1 fixes).

---

## WP32. Flag-day pre-positions: the OSCALL rewrite and the cw_ door (new)

**Goal.** Take now, at no bump, the halves of flag-day rows that need no break,
so the flag day is a macro flip rather than a rewrite. The API level (WP9),
API_RN_DSOFF (WP10), the context tags (WP19), N1 and histsys-7 (WP4), L7 (WP5),
F3 (WP16) and the D8 cell (WP22) are already placed. This WP has the rest.

**Findings taken.**
- Flagday **T6** (gfr-1): **one call macro at every site.** Every `call OSAPI_X`
  (6,954 sites) becomes `OSCALL OSAPI_X`.
  - The rewrite is mechanical and **byte-identical** until T5 flips the macro,
    so prove it with `cmp` over every image.
  - A cell's kind then stops being source ABI.
  - Renames wait for the flag day, with `%define` aliases.
  - 0 bytes; one os88api.inc update.
- Gap `gfr-2` [P] / flagday **P4**: **open the software-interrupt door to the
  kernel's own `cw_` shims first.** It is kernel-private, needs no ABI, and
  rehearses T5. **Amended (cost lens):**
  - Of the 70 `cw_` symbols, 21 are `equ` aliases that already cost 0.
  - The ~49 real shims are 4-byte `call X / retf` in `.text`. The door replaces
    each with a 2-byte table word and adds a 40-60-byte door: **-40..-60
    `.text`**.
  - Plus -2 at each converted call site. That is ~150 sites once the drawing
    shims are excluded: cw_gfx_lock (13 sites), cw_gfx_unlock (11), cw_gfx_fill
    (9), cw_gfx_frame (7) and cw_font_run (6) must stay direct far calls,
    because a repaint calls them by the dozen and the door is +~170 clocks. The
    sites are split between `.cold` (resident) and `.mod*`/`.ovl`/`.boot2`
    (not).
  - **Census first** (truth lens: not every symbol is a shim with call sites).
  - The vector sits above 3Fh, out of windowed DOS's exchange set. Full-screen
    DOS's whole-IVT bank (dos.asm:2970-2977) still has to be designed out.
  - kern_mega would need an IDT gate, so it is excluded here.
- Flagday **V7**: a counter in the rare path (one knob rebuild, 0 shipped). It
  gives the measured frequency T5's renumber must sort by.

**Files.** apps/os88api.inc (OSCALL), every package and driver source
(mechanical), apps/cc thunks; kernel/kernel.asm (the door, the cw_ table),
the call sites in kernel/ and drivers/; tools/t_api_abi.py.

**Bytes.**
- kern_big **-40..-60 `.text`**, plus the per-site `.cold` share, unmeasured
  until the census.
- kern_small and kern_mega 0 here.
- Packages 0 (T6 is byte-identical).

**Risk.** Low for T6 (`cmp`-proved). Medium for the door: an `int` from inside
a windowed DOS world, and IF restore (only when the stacked IF was 1).

**Rows.** `api-abi`, `abilock` (fast); the `cmp` of every image; soak `kdos`,
`dosirq`, `doswmouse`, and a door cycle count on MartyPC in the final pass
(PREDICTED today at +150..+215 clocks a rare call for T5).

**Dependencies.** WP10 (abi-4, the four-kernel decoder), WP19 (the tags
answer T5's IF question).

**Travels with.** Nothing; it is the last phase-1 session before the flag day.

---

## WP33. HDD.DRV's kernel face (new)

**Goal.** The kernel-facing contract of HDD.DRV holds up, but has gaps at the
trust edges and in transport retries. **The image is 4,096 bytes with 2 spare**,
so growth must be paid by its own savings. hdk-11 lands first.

**Findings taken.**

| id | what | bytes (HDD.DRV unless stated) |
|---|---|---|
| `hdk-11` [C] | the seam copies resident state through six verbs. HDT_INIT returns far pointers to hd_devs, hd_vols, hd_ndev, hd_sel and hd_msgc. Only RAW, MOUNT/UNMOUNT(+DEV), GEOM and CFGMARK keep handlers. **HD_ABI_VER 8** (private; WP10's drivers-16 checks it). On kern_mega mega_gpfix serves the HDD/HDDTOOL far calls (L3) | ~-100, +~40 HDDTOOL |
| `hdk-9` [C] | DSV_CPNAME publishes `DRV_ORG+16` instead of a copy of the header name (works in the HMA copy too). Note in os88drv.inc (WP4) | -11 (-28 across three drivers) |
| `hdk-10` [C] | OSAPI_VOL_SYS instead of banking the system volume at DRVV_READY | -4 |
| `hdk-8` [C] | hd_mount_one's accidental CF: delete the redundant `jc` | -2 |
| `hdk-3` [C] | rung 0 accepts an ECC-corrected read (int 13h status 11h): a one-sector run is done; a longer run re-issues one sector at a time. Raise the same rule for the kernel's BIOS loop with batch E's BR-16 (disk.inc has no 11h handling) | +12 (+5 one-sector form) |
| `hdk-4` [C] | rung 1: one re-issue from the failing sector with a counter (SRST optional) | +10 (+12) |
| `hdk-5` [C] | partition status byte not 00h/80h is HMB_BAD (both images); refuse a base equal to a live row's HDV_BASE | +14 both images, +~20 |
| `hdk-7` [C] | the mounted-partition guard moves into hd_mount_one (hdk-5's base check subsumes it) | ~+10 net |
| `hdk-6` [C] (bug) | hd_chs bounds at the drive's own count, not 1,024. A rung-1 row taken from the BIOS keeps one short; set HDD_CYL from IDENTIFY in hd_twins, or document it | 0 |
| `hdk-12` [C] | rung 1 writes with `rep outsw` (into the 2-byte pad); measure on 86Box mr286 in the final pass | +2 |
| `hdk-13` [C] | hd_raw's bounce refuses CX > 127 | +4 |
| `hdk-1` [K] | the stale arm: **do not fence DX against ES** (the Video Player's buffer is a separate heap claim). An arm expires when the READ_SEQ it brackets returns: driver-only, 0 kernel bytes. The wrong-DX half (a DSV_BLK AL tag) is a flag-day contract change, recorded | a few |
| `hdk-2` [K] | five passages still say the door reaches no sector. Four are fixed by the doc pass and os88api.inc:3490 goes to WP26. Policy, OWNER: may a package erase without a tool-owned confirm? The cheap strengthening is an HDDTOOL token from HDI_SLOTS | ~+10 tool |

**Files.** drivers/hdd/hdd.asm, hdcom.inc, mount.inc, hdtool.inc, hdsvc.inc,
hdxms.inc, inst.inc, cppage.inc; the HDDTOOL package; drivers/os88drv.inc
(note); SPEC 51.8.1, 52.1.4, 52.11, 117.4.

**Bytes.** 0 kernel. HDD.DRV about **-45 net** with hdk-11 (about +55 without
it, which does not fit). HDDTOOL +~50.

**Risk.** Medium: partitions and transport retries. 86Box is the instrument
(docs/86BOX.md); whether an XT controller stops at the corrected sector is a
field-machine question.

**Rows.** soak `hdboot`, `hddgeom`, `hdauto`, `hdmap`, `hdsize`, `hdtake`,
`hdtakeboot`, `hddcp`, `hdnoclaim`, `hdbounce`, and their `box-` twins on 86Box.

**Dependencies.** WP10 (drivers-16's HD guard). WP5's A20 include for hdxms.inc.

**Travels with.** Batch E (BR-16's kernel half of the 11h rule).

---

## WP34. XMEM.DRV, its cells, and DOS.O88's XMS (new)

**Goal.** XMEM.DRV's three transports are correct. The systemic hole is
ownership: on kern_big nothing filters int 15h for DOS programs, and the tier-2
copy never re-proves A20. **The image is 2,041 of 2,048 bytes** (7 slack), so
xm-3 and xm-8 compete for the same region.

**Findings taken.**
- `xm-1` [C] **DOS.O88 hooks int 15h for its programs.**
  - AH=88h answers AX=0 CF=0.
  - AH=87h with either base at or above 0x100000 answers CF=1 AH=03h.
  - E801h, E820h, C7h, 8Ah and AH=24h answer CF=1 AH=86h.
  - Everything else chains.

  Run tests/dostrap/xmsprobe.asm under DOS.O88 on a QEMU 386 (QEMU list item
  1). +45..+60 DOS.O88.
- `xm-2` [C]: hoist `call xm_a20_reopen / jc .e1` above the tier branch and
  delete the LOADALL arm's probe. ~0 (net +2), ~40 clocks a copy on a 386.
- `xm-8` [K]: delete xm_arm and xm_gdt (unreal FS/GS is dead weight). Leave
  CPU_F_UNREAL defined and never set, and **give tests/vidxms.py another tier-1
  witness**, because it reads bit 0x04. -20 XMEM.DRV.
- `xm-9` [C]: refuse with AX=2 when the conventional end + length > 0x10FFF0.
  It needs xm-8's room. +12 XMEM.DRV.
- `xm-10` [C]: the kernel's copy cell passes the XMC_ block in registers (private
  overlay ABI, **XM_ABI_VER 3**, under `%ifndef KERN_MEGA`). -54 resident kern_big
  (-42 `.text`, -12 `.bss`; hand estimates against the MEASURED 83-byte span),
  -11 XMEM.DRV. Re-check tests/xmla.py.
- `xm-4` [C]: DOS.O88's XMS 08h computes the largest gap from
  OSAPI_XMEM_WALK. +~50 DOS.O88, package-only. A CAPS register answer would be a
  flag-day change.
- `xm-5` [C]: on kern_big, take dos_xms_fits and its two calls out of `%ifdef
  DOS_MEGA`. +~55 DOS.O88.
- *Record unless room:* `xm-3` [K]. Thirteen block-table entries instead of
  eight (XM_BLKSZ 6) cost no byte alone. With xm-8 the scratch makes the image
  grow by 87, so price the two together. kernel/fsx.inc:3567 is a third
  XM_MAX_BLKS reader.
- *Record:* bytes-2's XMS half (the bodies past `xm_have`, about 110..130 bytes, resident
  on every machine). Price it if XMEM.DRV ever absorbs them.

**Owned elsewhere.** `xm-12`'s SDK sentence is WP26's. md-21/ul-5's shared A20
include is WP5's; XMEM's two bodies join it.

**Files.** drivers/xmem/xmem.asm, kernel/xmem.inc, kernel/kernel.asm (the XM
mirror), apps/dos/dos.asm (dos_ivtab, the 08h query, dos_xms_fits),
tests/vidxms.py, tests/xmla.py, tests/xmcheck.py; SPEC 41.4 (FS/GS rule to
history), 41.8.

**Bytes.**
- kern_big **-54**; kern_small 0 (no XMS); kern_mega 0.
- XMEM.DRV about -17 net (-20 +12 +2 -11).
- DOS.O88 +150..+165.

**Risk.** Medium: A20 and int 15h are where a mistake writes 1MB lower.

**Rows.** soak `xmla`, `xmcheck`, `xmhwalk`, `dosxms`, `msegxms`, `vidxms*`,
`ems`, `hma286`, `hmapool`, `kdos`; a QEMU 386 run of xmsprobe under DOS.O88.
`xm-7`'s overrun prediction needs a field run (docs/FIELD-MACHINES.md), not a
row.

**Dependencies.** WP10 (drivers-16's XM guard), WP5 (A20 include).

**Travels with.** Batch G.

---

## Figures this plan does not quote as measured

Following gap ul-11, how each kind of figure this plan quotes stands:

- MEASURED: the 21,450-cycle VGA pointer and RELIT's -42 ms increment.
- ARITHMETIC, not measured: the 86 ms per 32KB regrow (a figure worked out in
  a `kernel/memory.inc` comment, ul-11).
- Derived from a MEASURED rate: kern_mega's ~16 ms IF-off figure (mc-5), from
  the Victory 286's 0.257 ms/KB.
- PREDICTED: every cycle figure for the door (+150..+215), the boost, the
  sch_lock release, gfx-4's 5-10×, text-9's 4.8 ms and files-5's 35 ms.
- ESTIMATED: histsys-7 (-90..-140), rt-7 (-290..-380) and hm-9 (+50..+80).
  WP2's -2,568 is a MEASURED span, but its glue is ESTIMATED.

---

## Appendix A. Every first-round phase-1 finding, and its owner

All 298 "now" findings that are not `bug` and not refuted. "batch X (BR-n)"
means `bugs.md` owns it and the WP named in the text travels with it. "WPn
(BR-n)" means this plan owns a finding `bugs.md` also registers.

| id | kind | severity | verdict | goes to | title |
|---|---|---|---|---|---|
| abi-3 | hazard | medium | C | WP10 (BR-62) | t_api_abi decodes only kern_big's table |
| abi-4 | hazard | low | K | WP10 (BR-146) | An N-cell body reads the caller's DS out of api_rn's stack frame at a literal offset |
| abi-5 | hazard | low | C | WP4 (BR-145) | OSAPI_DRV_CALL promises DI back, but neither the kernel nor the driver contract keeps it |
| abi-6 | rethink | high | C | WP9 | No runtime API level: a package cannot ask which cells this kernel has |
| abi-7 | weakness | medium | C | batch C (BR-73) | Packages write the window record's geometry, which the SDK says they may not |
| abi-8 | weakness | medium | C | WP19 (BR-22) | Rules about which task or context may call a cell are prose, and SPEC admits one has no enforcement |
| abi-10 | weakness | medium | C | WP24 | Contracts are written in up to four places, and the copies have gone stale |
| abi-12 | consistency | low | C | WP26 | Multiplexed doors take their verb in AL, AH or BL |
| abi-14 | consistency | low | K | WP26 | 'ES:SI' in an X cell's contract actually means DS:SI |
| abi-15 | prior-art | low | C | WP19 | No parameter validation, and no build option that adds it |
| boot-2 | hazard | high | K | batch A (BR-1) | The splash runs code from crossing runs before the canary has verified them, which defeats the canary on kern_small's floppies and kern_big's 1.2MB |
| boot-4 | hazard | low | K | WP6 (BR-153) | The SPL_RESIDENT guard stops four bytes short of the stores it is meant to cover, and the rung is four sectors too big |
| boot-5 | hazard | medium | C | batch A (BR-25) | A hard-disk boot fetches its own BPB with the AH=08h geometry that boothd deliberately stopped trusting |
| boot-6 | weakness | low | K | WP6 (BR-25) | boothd's RAM check asks the question SPEC 2.7.1 retired, and never asks about the blob |
| boot-7 | size | medium | C | WP6 | Stage 2's copy of the DPT patch is dead code in a blob with 4 bytes free |
| boot-8 | consistency | low | C | WP7 | .bss arrives zeroed by the load, while many comments and SPEC passages say it does not |
| boot-9 | weakness | low | C | WP6 | The three hand-set constants carry their history in comments, and the history is now wrong about the present |
| boot-10 | rethink | low | C | WP6 | Let the build CHOOSE KSIG_OFF and SPL_RESIDENT instead of checking hand-typed values afterwards |
| boot-11 | perf | low | C | batch A (BR-155) | sch_isr draws a 28.7 ms spinner frame with IF=0 throughout kmain_o |
| boot-13 | consistency | low | C | WP6 | The floppy handoff leaves BX:CX as residue, while two comments promise zeros |
| boot-17 | rethink | low | U | WP6 | Make kmain_o a table rather than a call sequence |
| boot-18 | weakness | low | K | WP6 (BR-154) | Guard 5 omits BLOB_LIFT on every shipped kernel |
| boot-19 | weakness | low | C | WP6 | boot.asm's default constants would build a wrong sector silently |
| bytes-2 | size | medium | K | WP3 (superseded by rt-3) | An AT rung: the PS/2 runtime and the XMS bodies, slid over on an 8088 |
| bytes-3 | rethink | medium | C | WP2 | FILECP and FDLG on kern_big as boot-preloaded purgeable images (the COMMAND.COM transient model) |
| bytes-8 | size | low | K | WP4 (kern_small; kern_big conditional) | The module names' '.DRV' suffix |
| bytes-9 | consistency | low | K | WP24 | KERNEL-MEMORY.md's hand-copied ladder and Task Manager tables are ~9KB stale |
| bytes-10 | consistency | low | C | WP24 | AGENTS.md has lost CLAUDE.md's byte rules |
| dr-1 | hazard | medium | C | batch A (BR-25) | Boot partition mounted with AH=08h geometry while the volume is addressed with the BPB's |
| dr-2 | hazard | medium | C | batch E (BR-37) | Batch bracket's 'UI is frozen' assertion is not enforced at a door packages can call |
| dr-3 | weakness | medium | C | batch E (BR-61) | A failing read-ahead fill runs the full retry ladder, then the direct read runs it again |
| dr-4 | hazard | medium | C | batch E (BR-16) | Status 06h (media changed) is retried as a generic error and discarded |
| dr-5 | rethink | medium | C | WP16 | OSAPI_FILE_FIND stays quadratic though dsk_dscan already resumes from a raw slot |
| dr-6 | perf | low | U | WP16 | Fixed overhead on every dsk_xfer, cache hits included |
| dr-7 | hazard | low | C | batch E (BR-138) | A run past the buffer's segment wraps to ES:0000 silently |
| dr-8 | hazard | low | K | batch E (BR-139) | dsk_rah_fill computes the track phase on the volume-relative LBA, not LBA+base |
| dr-9 | hazard | low | K | batch E (BR-140) | dsk_find returns a redirected volume's name unterminated when the driver's is 13+ bytes |
| dr-10 | consistency | low | K | WP16 | .bounce's 'no recursion' safety argument contradicts the fill and .img re-entries |
| dr-11 | perf | low | C | WP16 | A listing mount flushes every volume's cached chunks |
| dr-14 | consistency | low | K | batch E (BR-141) | dsk_fdd_probe clears unit 1's calibrated bit whatever unit it probed |
| dr-16 | consistency | medium | C | WP25 | Documentation drift in this area is a defect of its own |
| dr-18 | rethink | low | U | WP16 | Mount and listing are one routine with quiet flags |
| dw-4 | hazard | medium | C | batch E (BR-22) | The UI-task-only rule for every file cell is enforced by nothing |
| dw-5 | hazard | low | C | batch E (BR-133) | A failed FAT flush keeps its dirt, which dsk_fatw_park may later write to another disk |
| dw-6 | hazard | low | C | batch E (BR-134) | WRITE_AT's INSIDE arm writes up to 511 bytes past the caller's buffer onto disk slack |
| dw-9 | consistency | low | C | WP1 | dskw_find matches the volume label, so its name cannot be used |
| dw-11 | weakness | low | C | batch E (BR-136) | Short names with bytes 80h-FFh, or a leading 05h, are listed but cannot be opened, deleted or renamed |
| dw-12 | weakness | medium | K | batch E (BR-60) | Subdirectories never grow: a kernel-made folder on a 512-byte-cluster volume holds 14 entries |
| dw-13 | perf | medium | K | WP12 | A whole-file write discovers FERR_FULL only after writing everything it could |
| dw-14 | rethink | medium | C | WP12 | Allocate a contiguous run when the size is known |
| dw-15 | rethink | medium | C | WP12 | Defer the post-commit FAT flush (frees only) to gfx_unlock, the gate held streams already use |
| dw-16 | perf | low | C | WP12 | The rover resets at every navigation, and FAT12 allocation is three nested calls a cluster |
| dw-17 | perf | low | C | WP12 | rmtree finds every file twice |
| dw-20 | consistency | low | C | WP12 | Pop runs and refusal tails shared across 1,000+ lines, held together by prose |
| dw-21 | consistency | low | C | WP16 | Two 'same question, two predicates' pairs: LFN detection and mounted-image detection |
| drivers-2 | hazard | medium | C | batch G (BR-31) | A driver can be unloaded while another task is inside it, or about to enter it |
| drivers-4 | hazard | medium | C | batch G (BR-33) | snd_release_inst calls DSV_RELINST at IF=0 into code that makes kernel calls |
| drivers-5 | weakness | low | K | WP4 | 'PRESERVE BX' is a driver rule the kernel could enforce at almost no resident cost |
| drivers-6 | hazard | medium | C | batch F (BR-34) | A driver image is freed with no check that its IRQ vectors were unhooked |
| drivers-7 | weakness | low | C | batch G (BR-39) | drv_vol_bank keeps one static pair, and the bracket is known to nest |
| drivers-10 | size | low | U | WP4 (conditional) | Driver file names are resident although every class-row loader is an image |
| drivers-11 | consistency | low | C | WP1 | Dead %else arms and kern_small prose inside the OS88_DRIVERS gate |
| drivers-12 | weakness | low | C | WP1 | drv_boot_x writes CH and nothing reads it |
| drivers-13 | perf | low | C | WP4 (measure only) | Every sound dispatch pays a .text-to-.cold far hop |
| drivers-14 | consistency | low | C | WP4 | OSAPI_FSX_SPK skips the 34.9 stream interlock that its close announces |
| drivers-16 | weakness | low | C | WP10 | The private ABIs (VX/XM/HD) are versioned by hand and are not covered by the ABI lock |
| engsys-2 | hazard | high | C | WP10 (BR-75) | A citation that resolves to the WRONG section passes: six §46 cites mean §47 |
| engsys-3 | weakness | high | C | WP10 | SPEC.md's hand copies of code are ungated and wrong |
| engsys-4 | weakness | high | K | WP24 | SPEC's front matter describes retired designs as current |
| engsys-5 | weakness | medium | C | WP24 | Two copies of the slot contracts, disagreeing in both directions |
| engsys-6 | weakness | medium | C | WP10 | Running counts in contract prose rot |
| engsys-7 | weakness | medium | K | WP10 | Tier admission rules are prose and are broken by about 17 fast rows and 4 full rows |
| engsys-8 | hazard | medium | K | WP10 (BR-76) | Basename collisions silently disable citation checks |
| engsys-9 | weakness | medium | C | WP10 | checkdocs does not read .c, .h, .txt or .sh |
| engsys-10 | weakness | medium | C | WP24 | AGENTS.md has diverged from CLAUDE.md by 726 lines |
| engsys-11 | weakness | medium | C | WP10 | The C SDK's constants and thunks are outside both t_mirror and the ABI lock |
| engsys-12 | hazard | low | C | WP10 (BR-171) | Section switch-back is gated only for nobits sections |
| engsys-13 | weakness | low | U | WP10 | Ratchets are exact-match registers, not one-way; 21 bespoke parsers |
| engsys-14 | weakness | low | C | WP10 | Gates regex NASM source for facts the assembler already emits |
| engsys-15 | hazard | low | C | WP10 (BR-172) | The whole-soak permission refusal is bypassed by -k '*' |
| engsys-16 | weakness | low | C | WP10 | builds= is judged per script, not per mode |
| engsys-18 | rethink | medium | C | WP24 | SPEC.md is two documents: 56% of it is package design notes |
| engsys-19 | rethink | medium | C | WP14 | Machine-readable routine contracts (register discipline is ungated) |
| engsys-20 | prior-art | low | U | WP19 | A checked build at the API boundary |
| engsys-21 | weakness | low | C | WP24 | The Makefile is one 20,872-line file, 53% comments |
| files-4 | hazard | low | C | batch D (BR-11) | The single purge-floor word is clobbered by a paste and lifted by anyone |
| files-5 | perf | low | K | WP16 | fm_measure stages every entry through a 24-byte far copy on every listing |
| files-6 | hazard | low | C | batch E (BR-123) | fmv_owner's 'cannot happen' fallback tags the cache as the menu save-under |
| files-7 | weakness | low | C | batch E (BR-124) | The commit's volume put-back ignores a failed mount |
| files-8 | weakness | low | C | batch E (BR-125) | Opening a chooser ends other windows' prompts without repainting them |
| files-9 | weakness | low | C | batch E (BR-126) | Modality filters presses, not drops |
| files-10 | weakness | low | C | batch E (BR-127) | kern_small keeps a closed window's 2KB cache claimed |
| files-11 | perf | low | K | WP1 | The empty-icon test ORs 64 bytes per row |
| files-13 | hazard | low | C | batch E (BR-129) | Any volume's ASSOC.DAT can exhaust the association slots for the session |
| files-15 | consistency | low | C | WP28 | Three copies of the module far-hop and two macro vocabularies |
| files-16 | rethink | medium | C | WP28 | Source shaped around tools that cannot evaluate %ifdef |
| files-17 | rethink | low | U | WP27 | The completion callback could carry the folder |
| gfx-2 | hazard | medium | C | batch B (BR-64) | The planar decoder's deliberate over-read can #GP on kern_mega |
| gfx-3 | perf | medium | K | WP1 | gfx_blit1 far-calls the clip-region question twice per band |
| gfx-4 | perf | medium | C | WP20 | gfx_points has no planar path: every VGA point is a whole gfx_fill |
| gfx-5 | consistency | medium | C | WP26 (BR-77) | [gfx_color] after gfx_blit4 depends on which path drew |
| gfx-6 | weakness | medium | C | WP11 | Built-and-off knob bodies double the hot code without shipping its wins |
| gfx-7 | hazard | low | C | WP10 (BR-152) | The planar bodies hard-code an 80-byte stride |
| gfx-8 | perf | low | U | WP20 | A right-edge clip forfeits both blit decoders |
| gfx-9 | perf | low | C | WP20 | The shipped 1bpp XOR interior is a byte loop |
| gfx-10 | consistency | low | K | WP10 | gfx_lock's cli...sti is outside CLAUDE.md's short form of the IF rule |
| gfx-11 | consistency | low | C | WP20 | vga12.inc is not the module §4 describes |
| gfx-12 | weakness | medium | C | WP25 | Comments as archaeology bury the current contract |
| gfx-13 | rethink | medium | C | WP20 | 'Asked the depth before entering the display' is a recurring bug class |
| gfx-15 | rethink | low | C | WP20 | gfx_clip_run tests every fragment, with no early exit |
| hiber-5 | hazard | low | K | batch G (BR-97) | Nothing fences heap compaction or shedding during the progressive image write |
| hiber-6 | hazard | low | C | batch G (BR-98) | Resume stub's int 13h calls run through kbm_i15 in the kernel image being overwritten |
| hiber-7 | weakness | medium | C | batch G (BR-10) | Esc on the Resume face discards the hibernated session |
| hiber-11 | weakness | low | C | batch G (BR-101) | Write Img never checks that the target drive can hold the image |
| hiber-12 | weakness | medium | C | batch G (BR-72) | The cloner cannot see 8-sector or single-sided (DOS 1.x, no-BPB) disks |
| hiber-13 | consistency | low | K | WP29 | About 22 bare [cs:] writes sit beside the module's CSDO/CSEG idiom |
| hiber-14 | rethink | low | C | WP29 | kern_big wakes with the fresh boot's masks; kern_mega with the image's |
| hiber-15 | weakness | low | C | WP29 | hbm_arm's room check uses kern_big's image size on kern_mega in protected mode |
| hiber-16 | hazard | low | C | batch G (BR-102) | Two non-atomic BDA tick reads |
| hiber-17 | size | low | C | WP7 | hb_probe_x zeroes three .bss words that arrive zeroed |
| hiber-20 | prior-art | low | C | WP29 | What DISKCOPY did that the cloner does not: write another copy, verify, format while copying |
| hm-2 | hazard | low | K | WP26 (BR-104) | The owner fence's self-match lets OSAPI_MEM_FREE and _REGROW act on the caller's own live REGION, and the damage lands on others |
| hm-3 | hazard | medium | C | batch C (BR-4) | DESK.DRV is dropped on a dying worker while the UI task may be executing inside it |
| hm-4 | hazard | low | K | batch D (BR-35) | MEM_OWNER_MAX also caps the kernel's tags, and every driver image shares MEM_K_DRV |
| hm-5 | perf | low | C | batch D (BR-54) | The O(n^2) planning walks run with IF=0, where [sch_lock] would give the same exclusion |
| hm-7 | hazard | medium | C | batch D (BR-59) | A lazy part is re-fetched by 8.3 name with no check that the file is still the one launched |
| hm-8 | perf | low | C | batch D (BR-53) | A claim refused for lack of a free RECORD runs a full compaction before the shed that would free one |
| hm-9 | rethink | medium | K | WP2 | Package purgeable claims, delivered through the existing MC_RLOC proc with DX = 0 |
| hm-11 | size | medium | K | WP15 | The parts loader stays resident in every non-loader parts package, once per instance |
| hm-12 | weakness | low | C | WP15 | MEM_OWNER_MAX = 8 is the most-negotiated constant in the package tree, and there is no shared sub-allocator |
| hm-13 | weakness | low | C | batch D (BR-11) | The claim floor is stamped per TASK while every package callback runs on task 0, and any teardown lifts it |
| hm-14 | consistency | low | C | batch D (BR-107) | OSAPI_MEM_P treats any unknown verb as MEMP_AVAIL and returns the caller's own CF |
| histsys-1 | hazard | high | C | batch C (BR-4) | A dying worker tears down and repaints other windows on its own 128/192-byte stack slice |
| histsys-2 | hazard | medium | K | WP8 (BR-9) | No int 0 (divide fault) handler on kern_big/kern_small: a 286+ hangs, an 8088 continues with garbage |
| histsys-3 | perf | medium | C | WP12 | The free-cluster count is recomputed (and on FAT16 re-read) on every DFREE, VOL_STAT and mount |
| histsys-4 | weakness | low | K | WP18 | No scheduling preference for the UI task: input waits up to N worker quanta |
| histsys-5 | rethink | medium | C | WP18 | Every int 13h freezes every task for ~400 ms |
| histsys-6 | weakness | low | C | batch F (BR-95) | Keyboard is polled outside the event ring, so key/mouse order is lost across a pass |
| histsys-7 | size | medium | K | WP4 | Seven 36-byte copies of a union driver service table in resident .bss |
| histsys-11 | weakness | low | C | WP8 (BR-8) | OSAPI_WM_WAKE writes through an unvalidated BX from ISR and worker context |
| histsys-15 | hazard | low | K | WP26 (BR-78) | OSAPI_TASK_SLEEP with AX >= 0x8002 returns at the next tick |
| histsys-16 | consistency | low | C | WP7 | Two contradictory rules about .bss at boot |
| histsys-17 | weakness | low | C | WP26 | The implicit display context is eight scattered bytes reset one by one |
| input-2 | hazard | medium | C | batch F (BR-19) | The tick can park a ROM int 09h with IRQ1 still in service, on the paths SPEC 9.8.3 left unmasked |
| input-4 | hazard | low | K | batch F (BR-87) | The serial lease trusts its caller to pin the region the ISR writes into |
| input-5 | hazard | low | C | batch F (BR-88) | Shift+PrtSc runs the ROM's Print Screen inside kbm_isr's IRQ mask |
| input-9 | hazard | low | C | batch F (BR-92) | mou_p2_off writes the 8042 on the UI task with nothing excluding int 09h |
| input-10 | weakness | low | C | WP22 | PS/2 sync rests on byte 0's bit 3 alone |
| input-11 | hazard | low | C | batch F (BR-93) | The hot-plug poller drops DTR on a modem port every 3 s for the whole session |
| input-12 | size | medium | C | WP3 (lifetime per rt-7) | The keyboard mouse is about 456 resident bytes on every machine and qualifies as an on-demand module |
| input-16 | consistency | low | C | WP25 | mou_claim no longer exists, and 22 references remain |
| isr-1 | consistency | low | K | WP13 | Pause stops the BIOS tick count on an XT; SPEC chose that but does not say the time of day falls behind |
| isr-2 | hazard | medium | K | batch F (BR-36) | osapi_mouse_feed is reached from an IRQ handler but swaps SP and not SS |
| isr-3 | hazard | low | C | WP26 (BR-85) | OSAPI_WM_WAKE's 'ISR-safe' has an unstated SS = LOW_SEG precondition |
| isr-4 | hazard | low | C | batch F (BR-86) | DF is never cleared at the IRQ0 callback boundary |
| isr-6 | hazard | low | K | batch F (BR-34) | IMR read-modify-writes at IF=1 on task context |
| isr-7 | hazard | medium | K | batch F (BR-21) | The cursor is drawn at IF=0 inside the serial ISR, which caps SERIAL.DRV's usable rate on an 8250 |
| isr-8 | weakness | medium | C | WP8 (BR-9) | CPU exceptions are left to the BIOS dummy handler (real-mode kernels) |
| isr-9 | hazard | low | C | batch F (BR-82) | SCH_CHSTK is sized without int 1Ch, and the mechanism SPEC gives is the wrong way round |
| isr-10 | consistency | low | C | WP13 | SPEC names the wrong 'longest IF=0 window' |
| isr-11 | consistency | low | C | WP4 | DSV_TICK's environment is documented as the kernel's, and it is not |
| isr-12 | consistency | low | K | WP10 | Bare cli/sti sites are argued in place, and CLAUDE.md's short form of the rule reads as absolute |
| isr-13 | rethink | medium | C | WP22 | One owner for IRQ lines, the IVT and the IMR |
| isr-14 | rethink | medium | C | WP13 | A written interrupt-latency budget, and a ratchet that enforces it |
| isr-16 | weakness | low | U | WP24 | No single 'callable at interrupt time' list |
| isr-17 | consistency | low | C | batch F (BR-34) | The SB driver leaves the master PIC reading its ISR |
| loader-5 | hazard | medium | C | batch D (BR-30) | 'No nested start' and 'UI task only' for OSAPI_PKG_START are prose, not fences |
| loader-6 | prior-art | medium | K | WP9 | No load-time refusal for a package needing newer cells at the same PKG_FMT |
| loader-8 | weakness | low | C | batch D (BR-11) | ld_unreserve is a drifted twin of mem_free_rec (it misses the page-floor lift) |
| loader-10 | hazard | low | C | WP26 (BR-122) | OSAPI_PKG_START's caller image must be pinned across the call; the contract does not say so |
| loader-11 | perf | low | C | WP29 | Compressor's gamma searches its top bit from bit 15 down |
| loader-12 | consistency | low | C | WP28 | Three clear-prefix ladders; the module's comment points at a routine that does not exist |
| mc-2 | hazard | medium | C | batch H (BR-65) | An option-ROM IRQ on an unlent foreign stack is reflected with a selector as the real-mode SS |
| mc-4 | hazard | medium | C | batch H (BR-66) | Linear LDT walks with interrupts off, one per block on a teardown |
| mc-9 | hazard | medium | K | batch H (BR-67) | Global GDT scratch selectors held by prose; the compactor rebases SLD but not BACK |
| mc-10 | weakness | medium | C | batch H (BR-68) | The kernel's own writes through CS are completed silently |
| mc-11 | consistency | low | C | WP1 | The write-only CMOS index port is read back |
| mc-12 | hazard | low | C | batch H (BR-165) | mega_s2segc truncates an off-grid base |
| mc-13 | weakness | low | C | batch H (BR-165) | mega_ustab rows are never freed and can name a moved claim |
| mc-14 | weakness | low | C | batch H (BR-165) | MX_FWIN is set but never cleared |
| mc-15 | hazard | low | K | batch H (BR-166) | mega_leds spins up to 2x65,536 polls inside IRQ1 with IF off; an ACK behind an AUX byte arrives as a scancode |
| mc-16 | rethink | medium | K | out (owner decision d1) | Move the master PIC off 20h; the reflection path never re-initialises it, though several sites hard-code 20h-27h |
| mc-19 | prior-art | low | U | WP5 | A 'protected-mode clean' header bit as the incremental road to L3 |
| md-4 | hazard | low | K | batch H (BR-168) | The snapshot's fallback can build a prefix park below MPK_MIN_KB |
| md-5 | weakness | medium | C | WP10 | t_mirror does not scan kerndos/, so the kernel-to-kern_dos mirrors are unwatched |
| md-6 | size | medium | K | WP5 | Both kern_mega kern_dos images carry the live resume they can never run |
| md-7 | size | low | C | WP5 | kd_glb, a 512-byte gate-only buffer, sits in every shipped kern_dos's .bss |
| md-8 | weakness | low | C | WP5 | KDL_PLOW is written, documented as binding, and read by nobody |
| md-9 | hazard | low | C | batch H (BR-169) | mega_d8042get can bank a keystroke as the 8042 command byte |
| md-10 | hazard | low | C | batch H (BR-71) | kd_unpark and mr_a20 wait only 512 probes for an 8042 A20 gate |
| md-11 | hazard | low | C | batch H (BR-169) | The park stub's 'press a key' accepts a PS/2 mouse byte |
| md-12 | weakness | low | C | WP10 (BR-170) | mega_dreg_add silently drops a region on overflow |
| md-13 | size | low | C | WP5 | Ten kdback.inc doors are call X / ret around routines with the same contract |
| md-14 | size | low | C | WP5 | mem_free_x is a no-op behind a compare, and its comment says otherwise |
| md-15 | rethink | low | U | WP5 | Check the park header before jumping into it |
| md-16 | hazard | low | C | batch D (BR-105) | mem_claim_x's overflow test is the last bit shifted out |
| md-19 | size | low | K | WP5 | HIBER.DRV-only scratch is resident in kern_mega's conventional .text |
| md-20 | consistency | low | C | WP5 | KDLF_RAHSH is a shift count named like a flag bit |
| md-21 | consistency | low | U | WP5 | The A20 code is written out six times with four different budgets |
| md-22 | consistency | low | C | WP5 | kdlayout.inc is a changelog with a definition file inside it |
| mem-3 | weakness | medium | C | batch D (BR-53) | A doomed claim sheds every cache and may park workers once per shed |
| mem-6 | hazard | low | K | WP26 (BR-104) | The self-region widening in mem_find_own also lets a package FREE or REGROW the region it is running in |
| mem-7 | hazard | medium | C | batch D (BR-52) | Nothing bounds the interrupt context any more: blocks an ISR uses move at IF=1 unless they are driver images |
| mem-8 | perf | medium | C | batch D (BR-54) | O(MEM_MAX^2) walks run with interrupts off (mem_avail, mem_hifit, mem_claim_1) |
| mem-9 | weakness | low | K | WP17 | Regrow path 3 puts every grower at the top, whatever door it came in by |
| mem-10 | hazard | low | C | batch C (BR-4) | mod_drop on a worker can free a module the UI task is executing; and the same missing fact pins every module for life |
| mem-11 | weakness | low | C | WP13 | The driver-image IF=0 move's latency note is wrong in both directions |
| mem-12 | consistency | medium | C | WP25 | Orphaned headers of inlined routines are where this file's documentation rots |
| mem-14 | rethink | medium | C | WP17 | Keep mem_tab sorted by base: every walk becomes linear and best fit is free |
| mem-15 | rethink | medium | C | WP2 | Only the kernel can own a cache: no purgeable class for package claims |
| mem-17 | rethink | low | K | WP15 | Region movability by package-header bit instead of 72 call sites |
| mem-19 | hazard | low | K | batch D (BR-106) | kern_mega osapi_mem_regrow_x's move-down copies the OLD size into the NEW claim |
| sched-1 | hazard | medium | C | batch F (BR-51) | task_spawn holds IF=0 across the whole 0xCC slice fill |
| sched-3 | hazard | low | C | batch F (BR-80) | sch_lock has become a general Forbid, and yield/sleep/gfx_lock silently degrade under it |
| sched-4 | hazard | low | C | WP26 (BR-78) | OSAPI_TASK_SLEEP on the UI task ends at the next input, undocumented |
| sched-5 | hazard | low | K | WP26 (BR-78) | task_sleep's range is 1..32768 and unstated |
| sched-6 | hazard | low | C | batch F (BR-81) | evq_push wakes the UI task before the record is in the ring |
| sched-7 | hazard | low | K | batch F (BR-82) | The two private ISR stacks have no canary and sit back to back |
| sched-10 | weakness | medium | C | WP25 | Comment rot is the area's real defect |
| sched-11 | consistency | low | K | WP18 | fsx_rtick is a third copy of the chain swap and the hook guard |
| sched-12 | weakness | low | C | WP7 | sched_init's zero stores rest on an unresolved contradiction about .bss |
| sched-13 | perf | low | C | WP18 | evq_pop banks DS for a prefix hazard that cannot occur |
| sched-14 | size | low | C | WP1 | sch_clsbytes row 0 duplicates row 4 |
| sched-15 | rethink | medium | C | WP18 | A task can block only on time - there is no wait-on-anything primitive |
| sched-16 | rethink | low | C | WP18 | A UI wake gets no priority over ready workers |
| shell-1 | hazard | medium | C | batch C (BR-42) | The Sticky App library can be freed under a widget that has bound it |
| shell-2 | weakness | medium | K | WP2 | Interactive shell paths load and drop modules the shell itself calls too slow |
| shell-3 | rethink | medium | C | WP27 | OSAPI_DESK_ITEM remove is keyed by a zone that the API says is not an identity |
| shell-4 | consistency | low | K | WP28 | The dock painter is hand-copied into DOCK.DRV |
| shell-5 | consistency | low | C | WP28 | Three textual copies of the far-frame helper |
| shell-6 | hazard | low | C | WP10 (BR-117) | Locator File menu item 1 is poked by raw offset from five sites |
| shell-7 | weakness | low | C | batch C (BR-119) | STIKMENU.DRV writes the kernel's service-item record |
| shell-8 | weakness | low | K | WP27 | Boot reader keeps up to 1 KB more shortcut claim than the rows need |
| shell-9 | perf | low | C | WP20 | Every damage pass recomputes every shown cell's rect with div and two muls |
| shell-10 | perf | low | C | WP20 | WIN95.DRV phases button art one bit at a time |
| shell-11 | hazard | low | C | batch C (BR-118) | Basic dock_geom only works at boot or with CTRL.DRV loaded |
| shell-12 | consistency | low | K | batch C (BR-119) | A driver's service record is half-sanitised |
| sysmisc-4 | hazard | low | C | batch G (BR-157) | MM58167 New-Year shadow refreshed only on a Set or a roll, not on each boot |
| sysmisc-5 | weakness | low | C | batch G (BR-158) | RP5C01 claimed or refused depending on the time of day when the chip runs 12-hour |
| sysmisc-6 | hazard | low | C | batch G (BR-159) | Screen Saver settings window outlives the Control Panel; its Customize sibling does not |
| sysmisc-7 | hazard | low | C | batch G (BR-160) | Unloading VESA.DRV redraws the Drivers pane at a stale origin after vid_switch |
| sysmisc-9 | perf | low | K | WP11 | kern_big's saver wake and fsx exit relight is a full wm_paint_all; the damage-pass version is kern_mega-only |
| sysmisc-10 | consistency | low | C | WP7 | Dead boot-overlay stores and comments resting on a false '.bss is not zeroed' |
| sysmisc-12 | consistency | low | K | WP25 | W_FLAGS 'visible' bit is the literal 2 at about thirty sites |
| sysmisc-14 | rethink | medium | C | WP30 | R1: one clock - derive the time of day from 0040:006C |
| sysmisc-15 | rethink | medium | K | WP28 | R2: declarative Control Panel pages driven by one engine |
| text-1 | hazard | medium | C | batch B (BR-46) | fpg_bare banks [wm_clip_n] but not [gfx_hole] |
| text-2 | size | medium | C | WP1 | font_ink's VGA early-out and [gfx_mono1] test are unreachable |
| text-3 | hazard | low | C | WP26 (BR-77) | font_run_x leaves [gfx_color] path-dependent and the contract is silent |
| text-4 | hazard | low | C | batch B (BR-131) | OSAPI_FONT_GLYPHS can return the seam cut's borrowed table |
| text-5 | hazard | low | C | batch B (BR-132) | Menu layout measures package strings with an unbounded font_width_x |
| text-6 | consistency | low | C | WP10 | fprog's one-word-store adjacencies are only half asserted |
| text-7 | size | low | K | WP3 | font_run_x's once-per-run VGA prologue is resident on mono machines |
| text-8 | size | low | C | WP1 | [vid_popmax] is a constant held in a memory word |
| text-9 | perf | low | C | WP20 | VGA ico_pass keeps its per-word column bookkeeping in memory |
| text-11 | hazard | low | C | batch E (BR-129) | An empty volume keeps a stale ASSOC.DAT |
| text-13 | consistency | low | C | WP1 | Two parity nops under complementary %ifdefs are one unconditional nop |
| text-14 | weakness | medium | C | WP25 | Routine headers carry history, and the history goes stale |
| text-15 | rethink | medium | C | WP15 | Kernel-synthesised BOLD in font_run |
| text-16 | rethink | low | K | WP15 | Real separator and check-mark item markers |
| ui-2 | hazard | medium | K | batch C (BR-28) | Content-click, right-click and W_ONDRAG read the handler before gfx_lock and never re-test liveness (SPEC.md 13.9.2's race, unfixed elsewhere) |
| ui-3 | hazard | low | C | WP1 (BR-110) | ui_dispatch decides 'is this Locator' by reading AM_ONCMD = 0 in the owner's segment without the lock |
| ui-4 | hazard | low | C | WP10 (BR-111) | ui_cmd's jump table has no length assert against CMD_LAST |
| ui-7 | consistency | low | K | batch F (BR-86) | DF is defended in toast_show but trusted in clip_put, the slot whose failure corrupts the kernel heap |
| ui-8 | hazard | medium | C | WP26 (BR-22) | The SDK and SPEC.md 55.4 tell authors a worker may call the clipboard; the code says it must not |
| ui-9 | weakness | low | C | WP26 (BR-115) | 'The kernel guarantees delivery' of W_ONMOUSEUP is false when the ring dropped the release |
| ui-10 | size | low | K | WP27 | About's fourth line from [vid_w]/[vid_h] instead of a per-kind string table |
| ui-11 | prior-art | low | C | WP27 | One user-settable double-click interval for the whole machine |
| ui-12 | prior-art | low | U | WP27 | A clipboard generation count |
| ui-15 | weakness | medium | K | WP25 | History in comments is where the drift lives |
| ui-16 | consistency | low | K | batch C (BR-114) | Under TOAST_Q, one raiser's retire takes down another raiser's message |
| ui-17 | hazard | low | K | batch C (BR-116) | ui_sys_open indexes fm_stattab without the bound toast_say exists to carry |
| ui-19 | rethink | low | U | WP27 | A menu 'about to drop' moment for packages |
| video-2 | hazard | high | C | batch A (BR-6) | A plain MDA primary is programmed with Hercules graphics 6845 timings |
| video-4 | weakness | medium | C | batch A (BR-56) | A 64KB EGA is driven planar although AH=12h's BL already says 64KB |
| video-6 | hazard | medium | C | batch B (BR-58) | The planar blank (AC 12h) is used for VID_VESA, whose packed and direct-colour pipeline likely bypasses the Attribute Controller (PLAUSIBLE) |
| video-7 | size | medium | K | WP1 | kern_small carries about 85 resident bytes of fsx tables and code for VGA modes it can never set |
| video-8 | hazard | low | C | batch B (BR-147) | vid_apply builds and arms the row table on VID_VESA, where the mouse stack is allowed to overwrite it |
| video-12 | weakness | low | C | batch B (BR-151) | FSI_PAGES advertises pages that fsx_page refuses; the grant itself is not published |
| video-13 | weakness | low | C | WP23 | EGA's caps row omits mode 0Dh, which every EGA has |
| video-14 | consistency | low | C | WP23 | The kind-class idiom is hand-spelled at about ten sites |
| video-15 | weakness | medium | C | WP25 | Comments narrate history, and about 40 statements in this area have gone stale |
| wm-1 | hazard | high | K | WP8 (BR-8) | wm_ptr2idx raises INT 0 on a wild window pointer; its comment says it is bounded |
| wm-2 | size | medium | K | WP8 | Store the slot index in the record so wm_ptr2idx is a load, not a div |
| wm-4 | hazard | medium | C | batch C (BR-48) | wm_ask_size trusts a W_ONSIZE negotiator to keep BX, which no contract says |
| wm-6 | consistency | medium | C | batch C (BR-50) | wm_resize clamps against the union width and ignores the side-dock band |
| wm-7 | consistency | low | K | batch C (BR-109) | OSAPI_WM_DESTROY and OSAPI_WM_FULLSCREEN draw without taking the gfx lock |
| wm-8 | hazard | low | C | WP26 (BR-78) | OSAPI_WM_TIMER with AX >= 32768 fires at once |
| wm-9 | weakness | low | K | batch C (BR-108) | wm_su_drop ignores mem_free's refusal and loses the claim |
| wm-10 | size | low | C | WP1 | wm_lk's gfx_lock_flag test is redundant |
| wm-11 | weakness | medium | C | WP11 | The XT does not get the redraw knobs built and measured for it |
| wm-12 | rethink | medium | K | WP21 | Transitive marking redraws windows that were never damaged |
| wm-13 | consistency | medium | C | WP8 (BR-8) | Window-pointer validation is inconsistent across the wm doors |
| wm-14 | weakness | low | C | WP1 | wm_dmg_bands and the natural-bank banner exist twice, with dead inner arms |
| wm-15 | weakness | medium | C | WP25 | wm.inc narrates its own history and the narration goes stale |
| wm-16 | weakness | low | K | WP15 | The content white fill is still the default double-draw |
| wm-17 | consistency | low | C | WP21 | One-shot arguments to the damage pass are globals with many lifetimes |
| wm-R1 | rethink | medium | C | WP21 | Exact clipping in the damage pass, dropping the painter's-algorithm closure |

The first draft's Appendix B, the hazards routed to the bug pass, is folded
into the table above: each of those rows now names its batch and BR number.

## Appendix B. Gap-review findings, and their owner

Every non-refuted gap finding in phase "now", plus the flag-day and record ones
this plan places. The mouse-cursor gap review's ids collide with megacore's
`mc-*`, so they carry a `cursor:` prefix here. os88ui's `F*`/`R1` carry
`os88ui:`. Two gap findings were refuted by their verifiers and are dropped:
gfce-4 (FSX_SPK's N is unfenced by contract) and gms-7.

| id | kind | severity | verdict | goes to | title |
|---|---|---|---|---|---|
| gi-1 | bug | medium | C | batch E | os88wseq's documented kern_small path cannot run: OSAPI_VOL_STAT is a bare stc there, so the open fails CF=1 AX=0 |
| gi-2 | weakness | medium | C | WP16 | The cluster size can only be learned through a full free-cluster count |
| gi-3 | weakness | medium | C | WP15 | The parts standard and op_self round reads to CLUSTERS on kern_big, which brings back the cost SPEC 18.4.4.2 removed |
| gi-4 | hazard | low | C | batch E | The READ_SEQ cursor names no file and stays trusted across a quiet folder change |
| gi-6 | weakness | low | C | WP31 | The kern_small stubs leave AX undefined, so shared fallbacks cannot tell 'no door' from a refusal |
| gi-7 | hazard | low | C | WP26 | pit_now is wrong inside an armed FSXF_FASTTICK/FSXF_RATE bracket; only its users say so |
| gi-8 | consistency | low | K | WP10 | Kernel conventions are copied without a mirror gate |
| gi-9 | weakness | low | C | WP15 | crt0's plain .OVL arm ships in no package and keeps the folder bug the lazy arm fixed |
| gi-10 | perf | low | C | WP15 | os88_wseq_put copies byte by byte with a five-instruction loop |
| ctx-1 | rethink | high | C | batch C (BR-4) | Post instance teardown to the UI task, so paint and teardown have one context |
| ctx-2 | hazard | medium | K | batch C | A worker that dies holding the gfx lock deadlocks the desktop on kern_big and kern_small |
| ctx-3 | rethink | medium | C | WP18 | sch_lock can be released across the UI task's floppy int 13h only with three changes; the first fixes a bug the release would create |
| ctx-4 | hazard | medium | C | WP13 | IF=0 has no bound and no instrument; set one per machine class and measure it in the final pass |
| ctx-5 | hazard | medium | C | batch C (BR-85) | Every ISR-callable door assumes SS = LOW_SEG, which the kernel itself says is false |
| ctx-6 | hazard | low | K | batch D | On kern_small any task's claim may shed, which is the case the kern_big gate exists to forbid |
| ctx-7 | weakness | medium | C | WP19 | Context rules should be machine-readable tags and a call-graph gate, not two prose lists |
| ctx-8 | weakness | low | K | WP18 | The lock order 'never block under sch_lock' is obeyed and stated in prose, but not numbered or enforced |
| gfce-1 | hazard | high | C | WP8 | One fatal path: point int 0 (and #UD on a 286+) at the death panel |
| gfce-2 | rethink | medium | C | WP8 (owner decision) | A package's own divide fault should close the package, not the machine |
| gfce-3 | weakness | medium | C | WP8 | The divide fence is a habit; make it a ratchet |
| gfce-5 | hazard | low | C | WP8 | DESK.DRV's .make divides a package BX fenced only by plausibility |
| gfce-6 | weakness | medium | K | WP8 | SYSTEM.CFG gets a domain column, not per-byte patches |
| gfce-7 | consistency | low | C | WP8 | cmz_jsayx tells a code from a string at 16; fm_errtab has 11 entries |
| gfce-8 | weakness | medium | C | WP26 (now half) | One error CLASS, not one error NUMBER |
| gfce-9 | prior-art | low | C | WP19 | APICHK=1: the checked kernel, concretely |
| gfr-2 | rethink | medium | P | WP32 | Open the same door to the kernel's cw_ shims first: no ABI, and it rehearses T5 |
| gfr-5 | weakness | high | C | WP9 | The API level goes in now, in two free places; abi-6's flag-day half and abi-16 dissolve |
| gfr-6 | rethink | medium | P | WP4 | Driver cluster: histsys-7 makes D1 and drivers-9 moot; N1 re-checked for Phase 1; histsys-8 scoped and recorded; histsys-12 costs ~5 s on the XT |
| hdk-1 | hazard | medium | K | WP33 | HDV_ARM redirects any rung-1 read into the armed segment, including the kernel's own metadata reads |
| hdk-2 | consistency | low | K | WP33 | The package door now reaches a partition erase, and five places still say it reaches no sector |
| hdk-3 | weakness | medium | C | WP33 | Rung 0 fails a read the controller ECC-corrected (int 13h status 11h) |
| hdk-4 | weakness | low | C | WP33 | Rung 1 has no retry and no reset |
| hdk-5 | hazard | low | C | WP33 | The partition table is trusted by type and length alone: no status-byte, duplicate or overlap checks |
| hdk-6 | bug | low | C | WP33 | hd_chs bounds the cylinder at 1,024, not at the drive's own count, though its contract says 'past the drive' |
| hdk-7 | hazard | low | C | WP33 | HSV_MOUNT does not refuse a partition that is already mounted |
| hdk-8 | consistency | low | C | WP33 | hd_mount_one's CF is read as a verdict but is undocumented and partly accidental; the read is redundant |
| hdk-9 | size | low | C | WP33 | DSV_CPNAME publishes a copy of the name the header already holds at +16 |
| hdk-10 | rethink | low | C | WP33 | Ask OSAPI_VOL_SYS for the system volume instead of banking one at DRVV_READY |
| hdk-11 | rethink | medium | C | WP33 | The seam copies resident state through six verbs; publish pointers to it at HDT_INIT instead |
| hdk-12 | perf | low | C | WP33 | Rung 1 writes one word at a time where it reads with rep insw |
| hdk-13 | hazard | low | C | WP33 | hd_raw's bounce copies CL x 256 words, silently dropping a count's high byte |
| gkm-1 | hazard | medium | K | WP31 (fixes: batch G) | A fix made on one kernel's code path leaves the shipped kernel with the defect |
| gkm-2 | weakness | medium | C | WP31 | kern_small's separate code fails at refusal stubs and guards, and no gate decodes them |
| gkm-3 | weakness | medium | K | WP31 | Test coverage per kernel is inverse to the product's floor |
| gkm-4 | perf | low | C | WP31 | The parity gate covers kern_big and kern_small but not kern_mega, the only kernel whose every machine has a 16-bit bus |
| gkm-6 | rethink | medium | C | WP31 (owner) | Retire kern_emu: rebuild VMMOUSE.DRV the way USBMOUSE.DRV works |
| gkm-7 | weakness | low | C | WP31 | Build names, not features, are still the main configuration language inside modules |
| gkm-10 | weakness | low | K | WP11 | Budget headroom decides which kernel gets a speed-up |
| gms-1 | bug | medium | C | batch H | The real-mode door's move guard (fsx_rmvck) compares MC_OWN against the slot, so it never sees the package's own claims |
| gms-2 | bug | medium | C | batch H | mega_rgnsubfree frees unbound windows based in an exiting region above 1MB (the 14.80 class again) |
| gms-3 | bug | medium | C | batch H | mega_subsegf's private match scan can return a window or a handle descriptor as a SUB (14.128 on OSAPI_MEM_SUBSEG) |
| gms-4 | bug | medium | C | batch H | SEG_SUB/SUBC/TILE/HND/BWIN read a block base and later write a descriptor with no sch_lock |
| gms-5 | weakness | low | C | WP5 | SEG_TILE proves only that the first and last tile lie in some block |
| gms-6 | hazard | low | C | batch H | mega_mpmovef holds a conventional move as a 16-bit signed paragraph count |
| gms-8 | size | low | K | WP5 | mega_dklibfree is resident .text only for a conventional-arm call that just needs a word zeroed |
| gms-9 | size | low | C | WP5 | A dead push/mov/pop of BX around mega_ldt_alloc2 in mega_imghif |
| gms-10 | perf | low | C | batch H (with BR-66) | The block walkers in this span scan the whole LDT, mega_mpmovef with IF = 0 |
| gms-R1 | rethink | medium | C | WP5 (optional) | A kind byte per LDT entry instead of ownership inferred from geometry plus side tables |
| cursor:mc-3 | weakness | medium | C | WP11 | kern_big moves a VGA pointer through the generic rect machinery at IF=0: 21,450 cycles (about 4.5 ms) a packet, erase-then-draw |
| cursor:mc-5 | hazard | medium | C | WP26 | The SDK and SPEC 7 say OSAPI_GFX_LOCK hides the cursor; since 7.1.4 it only promises to, and raw framebuffer writers have no way to make it happen |
| cursor:mc-9 | weakness | medium | C | WP20 | The lazy-hide protocol is opt-out, and the instrument that proved it is not in the tree |
| cursor:mc-1 | size | low | C | WP1 | [cur_dirty] is written in six places and read in none |
| cursor:mc-2 | hazard | low | C | batch F | kbm_move's draw gate is the pre-12.8.4 two-condition gate (no FPG_ARMED) |
| cursor:mc-4 | size | low | K | WP1 | The cur_rows=0 guards protect a value cur_geom cannot produce |
| cursor:mc-6 | consistency | low | C | WP20 | ui_volume_service moves the pointer as cursor_hide + cursor_show (the pre-7.1.2 double write) |
| cursor:mc-7 | size | low | C | WP1 | `mov si, bx` before cur_lazyck in wm_clip_set is dead |
| os88ui:F1 | weakness | medium | C | WP20 | The kernel's standard button fills its interior, frames it, and letters the caption afterwards |
| os88ui:F2 | hazard | medium | K | WP28 | os88ovlchk cannot see the KBTNIMG/KGLYPH split it is cited as guarding |
| os88ui:F3 | size | low | C | WP1 | os88ui_kbtn rebuilds a fully constant one-button record on every call |
| os88ui:F4 | size | low | C | WP1 | os88ui_arm_f is a 4-byte resident .cold shim both callers can do without |
| os88ui:F5 | hazard | low | C | batch E | The scroll-bar drag identity (x1,y1) assumes bars cannot share a corner; Disk windows can |
| os88ui:F6 | consistency | low | K | WP1 | OS88UI_FILL is a dead flag that seven kernel sites still load |
| rt-1 | rethink | high | K | WP2 | Drop to a cache, and let the need/drop bracket be the pin |
| rt-2 | size | medium | K | WP2 | FILECP and FDLG on kern_big as modules cached at boot: about -2,500 resident, not -3,600 |
| rt-3 | rethink | medium | C | WP3 | The PS/2 runtime as an owner-loaded overlay (XMEM.DRV's shape), not an AT rung |
| rt-5 | consistency | medium | C | WP31 | KERN_BUDGET is a VGA reading; the target machine's figure is never printed |
| rt-6 | rethink | medium | C | WP2 | kern_small's module split as a run-time heap decision on both kernels |
| rt-7 | size | medium | K | WP3 | The keyboard mouse is init-lifetime code: hold it from boot and drop it at settle |
| rt-9 | weakness | low | C | WP2 | Module purge ranks must be decided per module, not inherited |
| ul-1 | weakness | low | C | WP14 | bytes-5 re-derived: ~93-127 provable bytes, the convention change refuted, the gate justified only as a lint |
| ul-5 | consistency | low | C | WP5 | md-21 corrected: eight A20 bodies with three probe aliases, not six |
| ul-9 | perf | low | U | WP16 | dr-6 not re-derived; the saving is about 4-8% of a cache hit |
| ul-10 | consistency | medium | C | WP10 | No row produces any deferred measurement; the rows that would |
| ul-11 | consistency | medium | K | (applied throughout) | Numbers not to quote as measured, and corrected estimates that moved 30% or more |
| xm-1 | hazard | medium | C | WP34 | kern_big does not own int 15h: a DOS program can AH=87h over the pool and the HMA, or shut A20 |
| xm-2 | hazard | medium | C | WP34 | The tier-2 copy never re-proves A20: a gate shut by a third party becomes a silent write 1MB lower |
| xm-3 | weakness | low | K | WP34 (record unless room) | Eight block-table entries for the whole machine; 13 cost no image byte |
| xm-4 | weakness | low | C | WP34 | CAPS has no largest-run answer, and DOS.O88's XMS 08h reports the total as the largest |
| xm-5 | weakness | low | C | WP34 | On kern_big, DOS.O88's XMS move does not check the offset against the handle |
| xm-8 | rethink | low | K | WP34 | xm_arm (unreal FS/GS) is dead weight: one display label reads it, and it leaves the GDTR on zeroed memory |
| xm-9 | weakness | low | C | WP34 | The conventional end is unchecked: ES=FFFFh reaches past 0x10FFEF into the pool's first 32KB |
| xm-10 | size | low | C | WP34 | The kernel's copy cell spends 95 resident bytes folding an XMC_ block a register convention avoids |
| xm-12 | weakness | low | C | WP26 | The cells enforce SPEC.md 41.8's context rule nowhere, and the SDK prose does not state it |

## Appendix C. Gap findings routed to the bug pass

`bugs.md` was written before the gap reviews, so these have no BR number yet.
Each names the batch whose files it touches.

| id | batch | bytes | the fix |
|---|---|---|---|
| ctx-2 [K] | C (with BR-4) | +12 `.text` big/small; -24 `.mega` | inst_task_die releases a gfx lock the dying task owns (`mov al,[sch_cur] / cmp al,[gfx_lock_own] / jne / call gfx_unlock`); delete kern_mega's duplicate in mega_fkill. Inside BR-4's figure if both land |
| ctx-5 [C] | C (with BR-85) | +3 `.text` | evq_push loads ES from LOW_SEG instead of `push ss / pop es`; an ISR-callable door may assume DS and nothing else |
| ctx-6 [K] | D | +14 `.text` kern_small, or 0 | the shed half of kern_big's sch_cur gate on kern_small, or the rule in SPEC 66.3.1 (OWNER) |
| gi-1 [C] | E | +2 package | os88wseq's kern_small path asks OSAPI_FILE_DFREE (BX = sectors/cluster) instead of VOL_STAT and banks FERR_NODISK; REDLINE is its only caller |
| gi-4 [C] | E | +20 `.cold` kern_big | stamp a name sum and the folder's low cluster byte into the READ_SEQ cursor's free bytes 3-5; a mismatch is cold. Fix diskw.inc's banner sentence |
| os88ui:F5 [C] | E | +8 `.cold` kern_big | bank `[fm_vp]` at a scroll-bar grab, or add y2 to the drag identity |
| cursor:mc-2 [C] | F | +7 `.text` | FPG_ARMED in kbm_move's draw gate, or one gate shared with mou_apply |
| gms-1 [C] | H | +10 `.mega` | fsx_rmvck matches MC_OWN against the instance's I_SPTR as well as the slot (mega_bown's pattern); compute `[fsx_rseg]` after fsx_rextf |
| gms-2 [C] | H | -15..-20 `.mega` | mega_rgnsubfree walks with mega_cdin, keeping the region-pair exemption |
| gms-3 [C] | H | -25 `.mega` | mega_subsegf calls mega_dfind instead of its private scan |
| gms-4 [C] | H | +24 `.mega` | inc/dec sch_lock across the read-to-write window in SEG_SUB/SUBC, SEG_TILE, SEG_HND and SEG_BWIN (not SEG_AIM). **Before WP5's gms-R2** |
| gms-6 [C] | H | +4..+8 `.mega` | mega_mpmovef takes the 17th bit from the subtract's borrow |
| gms-10 [C] | H (with BR-66) | 0 | extend BR-66's high-water mark to these walkers |

---

## Appendix D. Record and flag-day findings that bear on a phase-1 decision

These are in no WP and in no total. Each is listed because a phase-1 WP is its
no-break precursor, or would be decided differently with it in view. Gap
corrections are applied.

| id | phase | why it matters here |
|---|---|---|
| video-17 [K], rt-4 [C] | record | The mono renderer is resident on VGA-only kern_big: -1,100..-1,500 on the gauge. It is complementary to the VGA rung (rt-4: rungs serve nested classes only). It is the largest resident lever left, and needs the Display page's CGA row greyed there |
| bytes-5 [U] -> ul-1 [C] | record | The push/pop census holds, but the convention change is refused on evidence. ~93 provable bytes, taken in a size pass with WP14's lint |
| bytes-4 [C], gkm-8 [C], gkm-11 [C] | record | The two-segment tax (~700/540) is at its floor. Flattening kern_small (-540) is refused while its ROM is a product. The near model binds kern_big at the VGA window with 6,946 bytes left |
| gkm-5 [C] | record | Do not merge the kernels at run time: +5,440 to +14,700. Written into KERNEL-MEMORY.md (WP24) |
| bytes-1, gfr-1 [C] | flagday T5 | The rare-cell door: ~-385 kern_big/mega conv, ~-245 kern_small, -2 per rare site, +150..+215 clocks a rare call PREDICTED. The lenses disagree on kern_small: the cost lens finds the same 112 rare cells there and so ~-380. WP32's cw_ door rehearses it; WP10's abi-4 and abi-3 pre-position it |
| abi-11 / bytes-6 / histsys-9, gfr-3 | flagday R14 | The class table: about -215 / -95 / -270, +45..+60 cycles a callback (cost lens). WP8's door refusals and the wm-2 option do not conflict with it |
| drivers-9, D1 | flagday | Moot once WP4's histsys-7 lands (gfr-6) |
| mem-18 / hm-10 / mem-13, C1 | flagday | C1's kernel saving is about -60..-69 on kern_big and kern_small under T5 (cost lens, MEASURED door spans), -12 kern_mega conv. The package side runs the other way: +0.9..+1.7KB inline, or ~+0..+150 with a helper. Its correctness half is WP17's internal-caller conversion plus batch D's BR-105 |
| isr-13's cell / mc-19 / D8, L3 | flagday L3 | WP22 takes the IMR shadow, and the cell only on the owner's word. WP5 takes L7 (mc-19's bit). A driver-only IRQ cell retires only the drivers' share of mega_null0/mega_db, because packages also read the BDA and hook vectors (truth lens) |
| L1, gfr-8 | flagday | The pad is MEASURED at 386 / 309 / 232 today and varies 0-511 by build. It shares T5's flag day |
| dr-19 -> ul-2 [C] | record | Two caches, not three. One pool nets -100..-300 `.cold`, costs kern_small a larger FAT floor, and is an alternative to L1's below-kernel FAT window. Not with WP12 |
| histsys-12 -> ul-3 [C] | flagday | -100..-180, not -200..-350, and it overlaps WP4's drivers-10 (-95). Never add the two |
| gfx-14 [U] | record | A per-display renderer table, about -40 `.text`. Take it the next time the video layer is opened |
| gfx-17 [U] | record | ~61 parity pads serve 16-bit buses. State the trade in t_hotpar's header (WP31's HOTPAR macro) |
| wm-19 [C] | record | band.inc's BAND=1 knob. Retire it under WP11's "no indefinite shelf" if the owner agrees |
| wm-R2 -> ul-7 [C], ctx-9 [C], histsys-10 | record / R10 | Deferred damage, opt-in, at +150..+200 (the per-window rect was omitted). Drain it at wm_close_pass's point. After BR-4 it is a coalescing question, not a safety one |
| wm-R3 -> ul-8 [C] | flagday | A countable break: ~121 field reads in 33 files. Pair it with R6 |
| isr-15 [C], sched-17 [C] | record | Same proposal as WP18's histsys-5; blocking gfx_lock waiters need WP18's task_wake |
| sched-9 [C] | record | Optional in WP8 (+4) |
| input-13 [C] | record | The same as BR-95 (histsys-6). Measure how often it happens first |
| sysmisc-8 [C] | record | Control Panel invariants held by prose; WP28's engine makes them structural |
| hm-15 / loader-14 -> ul-6 [C], rt-8 [K] | record | A clone of a running instance is not pristine, because ld_start does not re-copy initialised data. Shared code across packages is real, but a library does not pay on the target. An image cache for relaunches is +100..+150 `.cold`, recorded |
| mem-16 [C] | record | No recency inside a rank. Watch for it after WP2's cache |
| shell-14 [C] | record | kern_small draws a desk cell two and three times; take a flicker reading first |
| gfce-2 [C] | record -> WP8 | The owner's decision on what the panel is for |
| gfce-10 [C] | record | kern_mega's two fatal panels (~-20 `.mega` to unify after WP8) |
| gfce-11 [C], video-18 [U] | record | No latch for a hung fsx bracket: Ctrl-Alt-Del already exits; correct SPEC 53.1's sentence |
| cursor:mc-8, mc-R1, mc-R2, mc-R3 | record | Multi-display context swaps; per-output exclusion (+20..+30, with CURFIX); planar save-under in off-screen VRAM; the cursor after the EOI (BR-21's rethink) |
| os88ui:F7, F8, R1 | record | 28 bytes of kern_small `.bss` for module-only use; the track greyed under the thumb; the scroll bar as appended cells (+~60 resident per kernel, -500..-840 per bar-using package), an A-section candidate |
| gi-5 [C], gi-11 [C] | flagday / record | A READ_AT-shaped READ_SEQ cell (-76 per carrier, ~13 carriers); os88data exists because no file cell takes a path (C3's consumer) |
| xm-6 [C], xm-7 [C] | record | Owner 0xFF shared by the kernel and every driver; the tier-1 masked window on a 6 MHz AT probably exceeds one 1200-baud mouse byte (PREDICTED; a field run) |
| xm-11 [C] | flagday | Folding XMEM_SEL/_RUN into OSAPI_SEG verbs (-12 table) works against V3 |
| gms-R2's retirement, gms-R3 [C] | flagday / record | Retire OSAPI_MEM_SUBSEG at the flag day (a new ABI-BREAK-LIST L row); one registry of words real mode reads |
| hdk-14 [C] | record | A one-line note on hd_buf_step and in SPEC 51.8.1 |
| gkm-9 [C] | record | One disk set for the 8088 and the 286, after L3 |
| hiber-19 [U] | record | Keep HIBERNAT.IMG allocated (the owner's call) |
| sysmisc-16 [C] | record | An EVT_SETTING if a package ever caches a setting |
| text-10, text-18, ui-14, loader-13, input-18, shell-13, ui-21 | record | Priced and declined by their reviewers |

---

## Phase totals per kernel (resident, the recommended cores, ESTIMATED)

| kernel | every WP, owner decisions taken | without the OWNER items (WP2, WP3's KBMOUSE and text-7, WP11) | context |
|---|---:|---:|---|
| kern_small | **-138..-416** | -36..+152 | blessed KERN_SIZE 59,392 of 64,000 (re-read at the start) |
| kern_big (VGA gauge) | **-2,158..-2,676** | **-150..+210** | blessed 96,768 of 100,864 |
| kern_big on a mono 8088 | about -2,600..-3,150 (WP3's mono column; WP11's and WP20's `.vga` not paid) | about -240..+130 (WP20's `.vga` not paid) | |
| kern_mega conventional | **-387..-626** | -97..-246 | budget 68,096 |
| kern_mega above 1MB | +166..+389 | +106..+304 | not budgeted |

Outside the resident totals:

- **kern_dos:** kern_big's image -560; each kern_mega image -1,585..-1,588.
- **Modules:**
  - CTRL.DRV -1,500..-3,000 (WP28, unprototyped);
  - KBMOUSE.DRV +400..+450 new, and a PS/2 overlay of about +265 new;
  - HDD.DRV about -45 net;
  - XMEM.DRV about -17 net;
  - HIBER.DRV/DISKOPS.DRV +54..+74.
- **Packages:** mem-17 -10..-19 per image; DOS.O88 +150..+165 (WP34).

**What the totals say.**

- **The phase is a net saving on every budgeted kernel only if the owner takes
  WP2.** Without the owner items, kern_big runs from a 150-byte saving to a
  210-byte spend. kern_small runs between -36 and +152 against a 64KB
  residency rule, and kern_mega conventional still saves -97..-246.
- **The growth is concentrated.** It is mostly correctness and speed:
  - WP12 +160..+255 (FAT write path);
  - WP20 +120..+146 (graphics; the `.vga` part only on VGA/EGA);
  - WP8 +75..+106 (fault containment);
  - WP16 +77..+88 and WP18 +50..+77.
- **Where to cut if the owner wants the phase smaller**, in order:
  - WP12's dw-14 (+100..+160);
  - WP20's `.vga` items (+80..+90);
  - WP8's door refusals (+22..+32);
  - WP18's boost (+10..+22);
  - WP27's optional cells.
- **WP2 is no longer the phase's keystone**, because it is the owner's
  decision. Its correctness prerequisites are batch C's BR-4 and batch D's
  BR-53, and they are worth landing whatever WP2's answer is.

## Recommended order of work: one implementation session each

The order respects dependencies and keeps the cheap, enabling work first.
Batches from `bugs.md` are interleaved where a WP needs one first or shares its
files. No session runs a soak row or `test-full` (CLAUDE.md's Testing).

| # | session | why here |
|---|---|---|
| 1 | **WP10** gates | every later WP leans on the four-kernel ABI decoder, t_mirror over kerndos, the section check and the new rows |
| 2 | **WP1 + WP7** | free bytes and the `.bss` rule; `.ovl` -45 for WP8 and the batches |
| 3 | **WP6 + batch A** | boot room, then BR-1/BR-6/BR-25/BR-63 in it; boot-4's audit before BR-1 |
| 4 | **WP14 + WP19** | header grammar, liveness lint, `@ctx` tags, the APICHK knob (one lock update) |
| 5 | **WP9 + WP26** | the API level, then every contract sentence (one more lock update); every cell-adding WP may now follow |
| 6 | **WP8 + batch F** | the fault panel and divide ratchet with sched.inc's batch; gfce-2 answered first |
| 7 | **Batch C + WP21** | BR-4/ctx-1/ctx-2 teardown on the UI task, BR-42, WP8's door half if not done; wm-17 with BR-47 |
| 8 | **Batch D + WP17 + WP13** | BR-53 (WP2's prerequisite), BR-12 (ifwin's first catch), mem-9, C1's internal callers |
| 9 | **WP2a**: rt-1 drop-to-cache, rt-9 ranks, shell-2's users | the mechanism, SPEC 2.8.3 first |
| 10 | **WP2b** (OWNER): FILECP/FDLG arm on kern_big | only on the owner's word; the ROM/HMA skip |
| 11 | **WP3** | KBMOUSE on WP2's boot cache (OWNER), the PS/2 overlay, text-7 (OWNER) |
| 12 | **WP4 + WP22 + batch G (driver half) + WP31** | driver tables, contract text, the IMR shadow, gkm-1's fixes, the matrix gates |
| 13 | **WP5 + batch H** | kern_mega's own; gms-4 before gms-R2 |
| 14 | **Batch E + WP16** | the disk hazards, then the read path and F3 |
| 15 | **WP12** | after batch E's dw-5/dw-2; SPEC 18.4's non-goal rewritten first |
| 16 | **WP18** | measure-first boost; task_wake; histsys-5 only after its three preconditions |
| 17 | **Batch B + WP20 + WP23** | display fixes, then the hot paths and the button order |
| 18 | **WP11** (OWNER) | the KHP round, kern_small arms kept |
| 19 | **WP15** | the SDK includes; hm-11's facility priced on xtmp3 first |
| 20 | **WP27** | the additions, behind OS88_NEED |
| 21 | **WP29 + WP30 + batch G (hiber/clock half)** | modules and the clock prototype |
| 22 | **WP33** | HDD.DRV, hdk-11 first so the rest fits |
| 23 | **WP34** | XMEM and DOS.O88 |
| 24 | **WP28** | the Control Panel engine prototype; the scanner sees macros |
| 25 | **WP32** | OSCALL rewrite (cmp-proved) and the cw_ door census, then the door |
| 26-29 | **WP24** (three sessions: front matter and generated copies; the SPEC split; the Makefile split), **WP25** (split by file) | documentation, proven by checkdocs and `cmp` |

**Then, once:** the deduplicated soak union of every row named above, with the
A/B leg against the starting commit (WP11, WP12, WP16, WP18 and WP20's
measurements are taken there), and `test-full`. A failing final pass re-runs
only what failed.

**Owner decisions to collect before the sessions that need them:** gfce-2
(session 6); WP2b's trade with its three quotes (10); KBMOUSE and text-7 (11);
the WP11 round (18); dw-14's SPEC rewrite (15); a 14th blob sector or boot-17
(3); gkm-6 (12); hdk-2's erase policy (22); kern_small caching (rt-6) and
ctx-6 (8).
