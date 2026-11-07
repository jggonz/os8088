# ABI-BREAK-LIST - the work list for the next flag day

**Status: A LIST, NOT A DECISION. Nothing here is taken.** Built by kern_mega
size pass 2's `abilist` agent (docs/plans/completed/msp2/BRIEF.md section 4)
on `msp2-abilist`, cut from `ead16c61`, on 2026-10-31. The owner's words:
*"Make or continue listing ABI breaks that would clean up the ABI overall, or
help with sizing. We will not take them this round, but having the list built
gives us a starting point for when we do."*

It consolidates every *WANTS AN ABI BREAK* section and every ABI-shaped
proposal under `docs/plans/` (sources in section 11), re-checked against the
tree at `ead16c61` rather than copied, plus an audit of `apps/os88api.inc`,
`drivers/os88drv.inc`, `apps/cc/os88.h`, `apps/os88mega.inc`,
`tools/abi.lock.json`, the API table in `kernel/kernel.asm` and SPEC.md 20.
**Every byte figure is ESTIMATED unless it says MEASURED**; the measured ones
come off `kernel.map` symbol spans of a kern_mega build at the base
(`/home/user/wt/bootonce/build/megak/kernel.map`, the same tree bar the build
number). No kernel or SDK file was edited and nothing was assembled for this
list. The other msp2 agents' own WANTS AN ABI BREAK rows are folded in by the
coordinator at the merge (section 10 is where they go).

Row status words: **proposed** (nothing against it, waiting for a flag day),
**refused** (an argument against it is on record - re-propose only with a new
one), **superseded** (taken, or made moot, since it was written - the row says
by what), **recorded** (priced so nobody re-derives it, not proposed).

---

## 0. What a flag day must also carry

A flag day is the moment several of these rows land together, because each
one alone pays for the whole procedure. The procedure, and what it touches
beyond the row itself:

1. **The written decision (SPEC.md 20.2.0.1).** `PKG_FMT` is locked to a
   fingerprint of `apps/os88api.inc` and `DRV_VER` to that plus
   `drivers/os88drv.inc`, in `tools/abi.lock.json`; `tests/unit/t_abilock.py`
   (fast tier) fails every `make` that moved either surface without a
   decision. A row that moves or re-contracts a cell, grows a header or a
   record, or renumbers a class **bumps the number** and then runs
   `python3 tools/abilock.py --update "why"`; a row that only adds at the end
   or clarifies a contract runs the same command **without** a bump. Today:
   `PKG_FMT` 10, `DRV_VER` 12.
2. **The numbers must keep fencing the kinds (SPEC.md 20.2.0).** Every
   `'O8'` kind has a number of its own and each loader's EQUALITY test is the
   fence between them: a package never reuses a number a released driver
   carries, and the reverse. With `PKG_FMT` 10 and `DRV_VER` 12 the next
   pair is **`PKG_FMT` 13 and `DRV_VER` 14** if both move (11 and 12 are
   drivers'; 13 is free for a package, 14 then for drivers). A package-only
   bump is 13; a driver-only bump is 13 too (no package has ever carried it)
   and then the package skips to 14 at its next bump. `PKG_FMT` lives in
   `kernel/loader.inc`, `apps/os88api.inc` and `tools/os88pkg.py`, held
   together by `tests/unit/t_mirror.py`.
3. **Every package and every driver is rebuilt** - which `make` already does
   for everything in the tree (SPEC.md 20.8 rule 4: this tree hosts every
   package written for this OS). What it cannot rebuild is a binary on
   somebody's floppy: those meet `Wrong os8088 version` (`LD_EVER`) at load,
   and an old driver fails on its version byte. That is the whole user-facing
   cost and it is the reason to batch.
4. **The C SDK moves in the same commit**: `apps/cc/os88.h` (prototypes and
   the contract prose), `apps/cc/os88thunk.asm` (63 `CC_T_*` thunks - they
   name cells by `OSAPI_*` symbol, so a RENUMBER flows through, but a
   RE-CONTRACT is a thunk edit), `apps/cc/crt0.asm` (the overlay loader's own
   claims), the host-test stubs (`apps/*/hosttest/*.c` re-declare `os88_*`),
   and `tools/cc8086.py` if a rule changes. Then every C package
   (`make cword runcpm c64 apple2 paccman lemmings weave loom`).
5. **The Wire's cell fence resets (SPEC.md 92.2).** `WIRE_CELL_LEVELS` and
   `WIRE_TABLE_END` in `tools/os88wire.py` (and `wcat.inc`'s level) describe
   table ends of the RELEASED kernels at the current `PKG_FMT`; a new number
   starts a new generation, the catalog's `WC_PKGFMT` check is what fences the
   old Wires, and the catalog on os8088.com has to be republished with
   packages of the new format in the same release.
6. **Everything that names an address by hand**: `tests/unit/t_api_abi.py`
   (decodes the table out of `kernel.bin` - it carries the INLINE ledger and
   the shape list), `tools/os88sym.py` and any test that hard-codes a cell,
   `docs/INDEX.md` (`python3 tools/os88index.py`), SPEC.md 20.3's running
   count and `kernel.asm`'s length guard.
7. **Files on disk** where a row changes a format: `SYSTEM.CFG` (row F1),
   the module names (F2), hibernate images (a resume across kernels is
   already refused), the ROMs (`make rom` is cut per kernel anyway) and
   `COLDHMA.SYS`.
8. **Every kernel**: kern_big, kern_small, kern_emu and kern_mega share ONE
   SDK and one table (SPEC.md 20.3's rule 3 for inline cells: a cell's
   length is ABI on every build), so a row is priced on all four and the
   budgets (`make budget-check`) are met at the merge.
9. **The soak**: a flag day changes the meaning of every package at once, and
   the only proof is the suite. That is the owner's to ask for
   (CLAUDE.md, Testing) - which is one more reason the rows are batched.

---

## 1. The ten highest-value rows

Ranked by what they buy against what they break; the ids are the rows below.

**The owner's ranking (2026-11-01): L3 is TOP OF THE LIST**, for the day its
prerequisite lands. kern_mega already has package arms of its own
(`$(MEGAPKGS)`, SPEC.md 20.18) and some shipped packages - the Video Player
- already refuse on it outright, so a kern_mega-only package rule is not a
new kind of break. **The prerequisite is The Wire** (SPEC.md 92): it serves
one `.o88` per catalog entry today and has to learn to serve a kern_mega
build of a package before the fixups can retire, or a kern_mega machine
loses every package the catalog offers. The table below is the audit's
value ranking and is otherwise unchanged.

| # | row | buys |
|---|---|---|
| 1 | **T1** five-byte rare cells | ~-117 `.text` on EVERY kernel - conventional on kern_mega too - ESTIMATED |
| 2 | **L1** the FAT window and sector buffer below `KERNEL_SEG` | the image's 0-511-byte pad (165 at build 477, ~256 average) on every kernel; the ladder becomes one rounding |
| 3 | **C1** the KB memory doors withdrawn for `OSAPI_MEM_P` | ~-50 to -70 per kernel and one memory unit in the SDK |
| 4 | **L3** kern_mega's protected-mode fixups made unnecessary by contract | up to ~1,190 bytes of `.mega` (MEASURED spans), plus the per-write #GP cost |
| 5 | **N1** the `0 = 28` driver-table fallbacks | ~-11 on every kernel that loads drivers, and **needs no bump** |
| 6 | **R2** snapshot slots bounded by the caller's capacity | a buffer overrun made impossible rather than avoided (a correctness row) |
| 7 | **R6** content coordinates, design B | the "draws outside its window" class of bug ended by construction (+~800 bytes - a feature, shelved for its budget) |
| 8 | **D1** `DRVC_*` renumbered: copied classes first | ~-6 to -10 per kernel, `drv_svcix` gone |
| 9 | **R1** `OSAPI_PKG_REHOME`, KEEP form only | ~-16 kern_big |
| 10 | **C2-C4** the file family's duplicate doors | ~-6 to -15 each, three fewer contracts |

---

## T. The API table's shape

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **T1** | **The five-byte rare cell**: `call api_r<kind> / dw target`, the rare body banking BP itself instead of the cell's `push bp`. Every rare cell (116 + `OSAPI_MEM_P` = ~117 on each kernel at `ead16c61`; SPEC.md 20.3's count) loses one byte, so the table renumbers from the first rare cell (`OSAPI_WM_CREATE`) on | every package, driver and C package (renumber); `t_api_abi`'s shape list | **~-117 `.text` on each of kern_big, kern_small, kern_emu and kern_mega**, ESTIMATED (wkcore said -105 at 105 cells). On kern_mega that is CONVENTIONAL memory. Costs a few clocks a rare call (the body moves BP under or around the return address) and the stack shape of the X/N bodies must be re-proved | ksp13/wkcore.md; HANDOFF-KERNEL-SIZE-P14.md 4 (declined then, "an ABI break on main") | **proposed** - the largest table row left, and it pays on the kernel the conventional-byte argument cares about most |
| **T2** | The hot SLOT's two spare bytes on kern_mega: `push ds / call mega_sl_N / pop ds / retf / nop / nop` - 44 cells x 2 = **88 bytes of NOPs** (MEASURED count: 44 `mega_sl_` stubs) and **352 bytes of 8-byte stubs** in `.text` (MEASURED span) | - | Nothing a flag day can buy: a 6-byte hot cell on every kernel means a 5-byte stub per cell in real mode (`push cs / pop ds / jmp`), **+132 on kern_big** to save 88 on kern_mega, and a cell length that differs per kernel is a second ABI (SPEC.md 20.3, rule 3) | msp1/pmfd.md WANTS AN ABI BREAK; this audit | **recorded, not proposed** |
| **T3** | The thirteen cells kern_small answers with a bare refusal (`gfx_spans`, `gfx_blitp`, `wm_band`, `xm_alloc`/`xm_free`, `osapi_snd_fm_x`, `osapi_drv_cfg_x`, `osapi_drv_dlg_x`, `osapi_desk_svc_x`, `osapi_pkg_rehome_x`, `osapi_vol_stat`, `drv_pkg_call_x`, `osapi_mouse_feed` - the 16 of LAST-DROP-BYTES 7.7.7 less the three line cells pass 4 deleted) taken out of kern_small's table | every small-built package on kern_big and every default-arm package on the small disks | ~6 bytes a cell on kern_small, ~78 | LAST-DROP-BYTES.md 7.7.7 | **refused by SPEC.md 20.8 rule 4**: one SDK, one table on every kernel; a cell missing on one kernel turns a refusal into a wild far call, and packages being trusted does not change that. The list is re-derived by a listing walk (`-DKERN_SMALL -l`), never from this row |
| **T4** | The single-caller cells folded into verbs of an existing multiplexed door: `OSAPI_WM_MINIMIZE` (Artful), `OSAPI_CLAIM_SNAPSHOT` and `OSAPI_IMG_NAME` (Task Manager), `OSAPI_SERIAL` (WireFleet), `OSAPI_WM_QUIET` and `OSAPI_DRV_CALL_AT` (FTPD), `OSAPI_MEM_FLOOR` (DOS), `OSAPI_MOUSE_FEED` (`USBMOUSE.DRV`), `OSAPI_DRV_DLG` (`RAMDISK.DRV`) - the caller census of this audit, asm callers outside the shared includes | the one caller each | ~0: a rare cell is 6 bytes and opening a door costs 6-9 (LAST-DROP-BYTES 7.7.7, constraint 2), so the cell is the cheap form | this audit | **recorded, not proposed** - one caller is not a reason; the arithmetic is |

## C. Duplicate doors and consolidations

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **C1** | **The KB memory doors withdrawn** - `OSAPI_MEM_CLAIM`, `_CLAIM_HI`, `_CLAIM_DMA`, `_CLAIM_DMA_HI`, `OSAPI_MEM_AVAIL` and `OSAPI_MEM_REGROW` - in favour of `OSAPI_MEM_P`'s paragraph verbs (SPEC.md 50.3.7), which already carry the head (`CX`), `_HI`, `MEMP_CPU`, `MEMP_CONV`, regrow and avail. SPEC.md 20.3.1 deletes the names | ~34 asm packages, 5 shared includes (`os88partsbody.inc` already uses `MEM_P`), 5 drivers (`SOUND.DRV`'s DMA ring), the C SDK's KB calls (`os88_mem_claim*` become shims or paragraph calls) | six cells (36) + the KB shifts at the doors (~10-14) + `mem_avail`'s round-down KB arm: **~-50 to -70 per kernel**, ESTIMATED; one memory unit in the SDK, and the DISK/CPU class asked at every claim rather than defaulted | HEAP-GRANULARITY-PLAN.md 3.3 (kept the KB doors, no bump); this audit | **proposed**. Each package site decides DISK or CPU as it converts - the per-package half the plan already lists |
| **C2** | `OSAPI_FILE_APPEND` withdrawn: it IS `OSAPI_FILE_WRITE_AT` with the file's size for an offset (`dskw_append_x` sets `DX = 0xFFFF` and joins `dskw_wat`), so a caller writes `WRITE_AT` with that sentinel | 9 apps, the C SDK, `RAMDISK.DRV`; kern_small, which has APPEND and not the offset door, gains the door's entry | cell 6 + `dwf_` thunk 4 + entry ~5: **~-15 kern_big**, ~0 kern_small, ESTIMATED | this audit (SPEC.md 18.4.7.3 calls them one routine) | **proposed, low** |
| **C3** | `OSAPI_FILE_GOTO`, `_GOTO_Q` and `_GOTO_QM` as ONE cell with a mode in AL (show / quiet / quiet-and-move): the three take the same `DX` cluster and `BL` volume | 13 + 1 + 29 asm packages, the shared includes, `HDD.DRV` and `ETHER.DRV`, the C SDK | two cells (12) less a mode dispatch (~6): **~-6 per kernel**, ESTIMATED; one contract instead of three | this audit | **proposed, low** - a cleanliness row |
| **C4** | `OSAPI_FILE_WRITE_SYS` and `OSAPI_FILE_APPEND_SYS` (drivers only, one caller, `HDD.DRV`'s installer) as a flag on the plain cells - AL is unused on input by both | every caller of `OSAPI_FILE_WRITE`/`APPEND` must then zero AL (26 + 9 asm packages) | two 3-byte jump cells and `api_file_append_sys`'s stub: **~-12 to -20 kern_big**, ESTIMATED | this audit | **proposed, low** - the "every caller zeroes a register" cost is the kind KERN-DOS-PLAN's suspend mask paid, and it is real |
| **C5** | `OSAPI_MEM_HMA` as an arm of `OSAPI_MEM_CLAIM_DMA_HI` and `OSAPI_MEM_FREE` | `SOUND.DRV`, the only DMA_HI caller that is a driver | was ~-14 kern_big / -6 kern_small | ksp12/hma.md; HANDOFF-KERNEL-SIZE-P13.md 3 | **refused** (HANDOFF-KERNEL-SIZE-P14.md 4): the DMA_HI arm would put `SOUND.DRV`'s movable pool in the HMA, and the shape that works - both forms on `OSAPI_MEM_FREE` - measured kern_big **+6** |
| **C6** | `OSAPI_ASSOC_FLUSH` folded into `OSAPI_ASSOC_SET` | Sheet, SysCheck, `HDD.DRV`, the C SDK | ~-10 | ksp13/assoc.md | **refused**: ES:SI is the whole argument and no register is left to carry a mode |
| **C7** | `OSAPI_DRV_CALL_AT` folded into `OSAPI_DRV_CALL` with ES always the caller's | FTPD (the one AT caller) and every `DRV_CALL` site, which must then load ES itself | the X cell's `ES = DS` is what 13 apps and 9 drivers rely on; one cell (6) against two bytes at every site | this audit (SPEC.md 20.11.2) | **recorded, not proposed** |
| **C8** | `OSAPI_BATCH_END` withdrawn: no asm caller and no C package calls `os88_batch_end` | nothing in the tree | 6 bytes | this audit | **refused by SPEC.md 20.3.1 rule 1**: zero callers is not a withdrawn contract - the END is optional *by design* (SPEC.md 18.9.3), and a capability somebody is told they have stays |

## R. Register contracts and records

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **R1** | `OSAPI_PKG_REHOME` keeps only its KEEP form (AX = 0, CX = paragraphs kept, SPEC.md 20.12.10.5); the bytes form (AX != 0) and `[ld_rehsz]` go, and the bound is `(dx - 1) >> 4 >= [ld_rehkp]` alone | any `.o88` whose loader was built for the bytes form. **In the tree today every caller is `OS88_REHOME` in `apps/os88parts.inc` (one call site), which uses KEEP** - so the break is an old binary on a disk | ~-10 `.cold` of `ld_start`, -2 `.bss`, ~-4 at the cell: **~-16 kern_big**, ESTIMATED | ksp12/files.md; HANDOFF-KERNEL-SIZE-P13.md 3 | **proposed** (re-checked: `ld_rehsz` and both forms are still in `kernel/loader.inc`) |
| **R2** | `OSAPI_SYS_SNAPSHOT` and `OSAPI_CLAIM_SNAPSHOT` bounded by the CALLER: CX = the records the buffer holds, and the kernel writes `min(CX, its own count)`. Today the package sizes the buffer from `INST_MAX`/`MEM_MAX` compiled into it and the kernel fills to ITS OWN count; `SS_NTASK`/`SS_NINST` and the AX/BX answers are read by **nothing** (`apps/audio/apengine.inc` and the Task Manager use their own constants) - the safety is over-allocation, and a kernel larger than the SDK overruns the package's segment silently | Task Manager, Audio, the other two SYS_SNAPSHOT apps, the C SDK | **+4 to +8 per kernel**, ESTIMATED - this row is CORRECTNESS, not bytes: it turns KERN-SMALL-CUT-PLAN 5.2's "class 1" mirror from directional luck into a refusal | KERN-SMALL-CUT-PLAN.md 5.2, 5.2.1 | **proposed** |
| **R3** | The volume bridge (`SNDV_MIXPOP`): `SOUND.DRV` hands its far pointer in ES:DI instead of filling `ui_volume_bridge`'s 4-byte table | `SOUND.DRV` against an older kernel (it would far-call through whatever DI is) - so `DRV_VER` | -4 kernel, -3 `SOUND.DRV` | ksp14/menubar.md (REFUSED list) | **proposed, fold-in only** - a `DRV_VER` bump for 4 bytes, worth it only inside one |
| **R4** | `OSAPI_FSX_SPK` as a thin door (drop DX:BX, stop writing the caller's block, move PIT/vector work to `os88spk.inc`) | the Video Player, MIDIRack | ~70-80 kern_big | LAST-DROP-BYTES.md 7.11; HANDOFF-KERNEL-SIZE-P7.md 1.1 | **superseded - BUILT** in kernel size pass 8 at -108 `.text` (SPEC.md 34.11.1, `PKG_FMT` 6 -> 8) |
| **R5** | `OSAPI_DRV_SUSPEND` takes a per-class mask so DOS can have the RAM of drivers the sweep would leave | every caller must zero the register | ~25 bytes + 1 `.bss` | KERN-DOS-PLAN.md (the drivers table) | **superseded - BUILT** as BL, the skip list's override (SPEC.md 51.11.4) |
| **R6** | **Content coordinates, design B**: every package-facing coordinate content-relative in and out; `OSAPI_GFX_LOCK` takes BX = the window and arms the clip and DC (98 `WM_CLIP_SET` sites go); `OSAPI_MOUSE` takes BX; `OSAPI_WM_CONTENT` withdrawn or kept as a 4-byte (0,0) inline cell for one cycle; a Control Panel page gets the pane as its DC; `DRV_VER` with it | every package (50 shipped + 43 test assembly packages, 7 C packages, 5 drawing drivers), 42 test scripts that read a package's origin words | **+~800-850 resident per kernel** (ESTIMATED) - a FEATURE: no package can draw outside its content box through any slot, by construction | CONTENT-COORDS-PLAN.md 6 | **proposed, SHELVED by the owner 2026-10-13 for its budget**; the plan's recommendation is design A first and B as its last wave |
| **R7** | The click EVENT carries the modifier state sampled at the press | every producer and the event record of every package | correctness against a race nobody can produce by hand | MULTISELECT-PLAN.md 8 | **refused** |
| **R8** | `inst_parksafe` as an `I_FLAGS` bit | every running declared package: `I_FLAGS` IS `SSI_FLAGS` in the published snapshot | -11 kern_big | ksp10/core.md | **refused**: the bit would leak into an ABI field documented as "bit 0 = minimized" |

## D. Driver header, classes and the driver tables

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **N1** | **Drop the `0 = 28` fallbacks** for `DRV_H_FSV` (`drv_fs_call`'s `or ax, ax / jnz / mov ax, FSV_TAB0`, both HMA arms) and `DRV_H_DSV` (`drv_publish`'s `jnz / mov cl, DSV_TAB0`). Both bytes were added in August (`c31e8734`, `6ac5d148`); `DRV_VER` has moved twice since (11 on 2026-10-14, 12 on 2026-10-31), and **every driver in the tree is built by `OS88_DRIVER`, which always writes a non-zero length** (checked: no hand-built driver header anywhere under `drivers/`, `apps/` or `tests/`). So no driver the equality test admits can carry 0 | nothing that loads today | **~-11 on every kernel that loads drivers** (7 + 4), ESTIMATED | this audit | **proposed - NEEDS NO FLAG DAY.** It is a contract clarified, not moved: an `abilock.py --update` without a bump (the SDK's "was reserved (0), which the kernel reads as 28" prose changes), and it can be taken in any size pass |
| **D1** | **`DRVC_*` renumbered so the classes with a `drv_svc` copy come first**: today 1 SOUND, 2 DISK, 3 retired, 4 NET, 5 FILE, 6 POINT, 7 EMS, 8 FILE2, 9 CD, 10 HTTP, and `drv_svcix` is a 10-byte per-class table because the copied classes are not contiguous. With SOUND, DISK, NET, FILE, FILE2, CD, HTTP as 1..7 and POINT, EMS after them, the index is `class - 1` and the refusal one compare | every driver's header class byte, `RD_CLASS` (`drivers/ramdisk/rdpkg.inc`, outside the locked surface - SPEC.md 20.2.0's own lesson), every package that names a class (`OSAPI_DRV_CALL`'s AL), the Drivers page, `SYSTEM.CFG`'s per-row records if keyed by class | `drv_svcix` (10) less a compare: **~-6 to -10 per kernel**, ESTIMATED | ksp12/files.md (priced before CD and HTTP joined the file classes - this row is refreshed); HANDOFF-KERNEL-SIZE-P13.md 3 | **proposed**, "worth folding into the next bump" as the source says |
| **D2** | `DRVC_POINT` keeps no `drv_svc` slot (`USBMOUSE.DRV` publishes `DSV_NAME` alone) | - | -38 `.bss` | LAST-DROP-BYTES.md 7.7.8 | **superseded - BUILT**: `drv_svcix` answers 0 for classes 6 and 7 behind `t_clscf`'s gate |
| **D3** | The ten-byte `'O8'` header fields no class reads: `DRV_H_FSV` is written by every class and read only for the file classes; the link base at +4 is `DRV_ORG` | - | 0 | this audit | **recorded, not proposed** - the header is 32 bytes either way and the dispatcher at +12 fixes the layout |

## F. Files on disk and names

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **F1** | `SYSTEM.CFG`'s `'VC'` key (Force Color, SPEC.md 39.28) folded into a spare bit of an existing record - `'VM'` (ksp12/ctrl) or `'FD'`'s reserved bit 2 (ksp12/theme) | every `SYSTEM.CFG` written before: an unknown key is dropped, so a user's Force Color is lost ONCE | -1 `.bss` (`CFG_NB`), -5 of blob on kern_big and kern_emu | ksp12/theme.md, ksp12/ctrl.md; HANDOFF-KERNEL-SIZE-P13.md 3 | **proposed** (still a key of its own: `kernel/driver.inc`'s `dw 'VC'`). Best taken with a `'VM'` version bump that resets the display arrangement anyway |
| **F2** | `WIN95.DRV` renamed shorter (`WIN.DRV`) | a file on every shipped system disk, the Theme page's toast, SPEC.md 76.14, `tests/thm95.py` | -2 resident (`mod_f_win`) | ksp12/theme.md | **recorded** - a user-visible rename, not a size row |

## L. Memory layout, segments and kern_mega

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **L1** | **The FAT window and `dsk_secbuf` move BELOW the kernel image**: ten sectors at 0x0060, `KERNEL_SEG` at 0x01A0, `.text`+`.bss` no longer rounded to 512 - one rounding left, the heap base's | `KERNEL_SEG equ 0x0060` is assembled into every package, every driver, `crt0.asm`, DOS (`apps/dos/`, `kerndos/`), the hard-disk loader and both boot paths, hibernate images, the ROMs and `COLDHMA.SYS`, and every host tool and test that names 0x0060 (`tools/os88sym.py` first). ES = KERNEL_SEG on callback entry is contract too. **Both numbers bump** | the image's pad: **165 bytes at build 477, 0-511 depending on the day, ~256 on average**, on every kernel - a one-off, not a rate; and it is the first step to an unaligned heap base | LAST-DROP-BYTES.md 7.13 ("WAITING FOR THE NEXT ABI BREAK", the owner's call) | **proposed - the owner has said to take it at the next break**. kern_mega note (this audit): 0x0060 is also kern_mega's GDT index 12, the readable-code descriptor every package's far calls land on (KERN-MEGA-PLAN 2.5); 0x01A0 is GDT index 52, which already exists - kern_mega's GDT is the identity map of every segment up to `COLD_SEG` (`kernel/mega.inc`, `MEGA_GDT_ID`) - so the row moves which identity entry is the readable-CODE one and which the FAT window's, and costs the table nothing; what kern_mega's own ladder pads below 1MB after the switch is the figure to measure before quoting the saving there |
| **L2** | Ring 3 on kern_mega: every cell its own call gate selector, IOPL or an I/O permission policy for the 22 packages that program video inside fsx brackets | every package reassembled | protection, not bytes | KERN-MEGA-PLAN.md 2.5 and its later-waves table | **recorded - a later wave**, a flag day of its own |
| **L3** | **kern_mega's protected-mode fixups made unnecessary by contract**: `mega_gpfix` (an image far-calling another image by the DATA segment it was handed - `HDD.DRV` <-> `HDDTOOL.DRV`), `mega_gpw` (writes through a code selector: `[cs:x]` stores in packages, writes through callback ES = 0x60), `mega_null0` + `mega_db` (a driver hooking its IRQ through segment 0 - `SB.DRV`, `USBMOUSE.DRV`). The contract would be: an IRQ is hooked through a cell, a far target is named by its code selector (`OSAPI_MEM_CODESEL` exists), callbacks get the data alias in ES, and a package does not store through CS | every package that stores through CS (many - the per-write #GP is what kern_mega pays today), the two IRQ-hooking drivers, `HDD.DRV`/`HDDTOOL.DRV`, every package writing through callback ES (4 in Paint, 2 in `os88stik.inc` per KERN-MEGA-PLAN 2.5) | **up to ~1,190 bytes of `.mega`** (MEASURED spans: `mega_gpfix` 593, `mega_gpw` 526, `mega_null0` 74, plus part of `mega_db` 69) - above 1MB, not conventional - and a #GP a write wherever a package still does it | this audit; KERN-MEGA-PLAN.md 2.5, items 46-50 | **proposed - TOP OF THE OWNER'S LIST, blocked on The Wire serving kern_mega builds** (section 1). LARGE: the per-package half is the bulk and the fixups must stay until the last old binary is gone, so it is a programming-model change rather than a row. The `PKG_MEGA` arms (SPEC.md 20.18) are where it would start |
| **L4** | `OS88_HUGE_INCR` stops being a constant: a claim above 1MB with consecutive LDT selectors at a step the kernel answers | `OSAPI_MEM_HUGE` callers (RealDMG, the C SDK, six tests) | flexibility; no consumer yet | KERN-MEGA-PLAN.md item 50 | **recorded** - "an ABI decision", nobody needs it |
| **L5** | The WEAVE canvas core's and LOOM preview's module entries take their own DATA segment from the caller (`WSM_ABI`, WEAVE-SPEC 1.2.2's header) | WEAVE, LOOM and their modules - a PACKAGE-PRIVATE ABI, not `PKG_FMT` | two #GPs a verb on kern_mega | KERN-MEGA-PLAN.md items 49, 50 | **recorded** |

## P. Package-private and kernel-private ABIs

Not `PKG_FMT`/`DRV_VER` - each has a version of its own - but a flag day is
the natural moment to tidy them.

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **P1** | DOS's back-end slot table drops `DBE_COPY`: nothing in the core calls it, the thunk is gone, and `dos_k_copy` stays in both hosts (`apps/dos/dos.asm`, `kerndos/kdback.inc`) "because the slot numbers are the back end's ABI" | `DOS.O88` and `kern_dos` together (they ship as one) | a table word and a 4-byte door in each host, ~-12 | HANDOFF-APPS-SHARED.md; `apps/dos/dos.asm` | **proposed, low** |
| **P2** | `VX_ABI_VER`, `HD_ABI_VER` and the module entry tables (`os88mod.py`) are private and move freely | - | - | ksp14/vesa.md, ksp11/drivers.md | **recorded** - not flag-day rows |

## A. Additions priced (no bump needed)

| id | what | buys | source | status |
|---|---|---|---|---|
| **A1** | A shared-planes form of `OSAPI_GFX_BLITP` (planes 0-2 the same bytes): a new cell appended, or a flag bit (which RE-contracts) | ~17% of a full RealDMG scrolling draw | WINDOWED-DRAW-PLAN.md W1 | **proposed** - append rather than flag, so no bump |
| **A2** | A published cell for the PicoMEM probe that `dsk_fdd_pmemu`, `SOUND.DRV`'s `pm_init` and `USBMOUSE.DRV`'s `um_pmattach` each carry | ~20 bytes of duplicate | ksp11/picomem.md | **refused**: six bytes of ABI for ~20, across the kernel/driver boundary |
| **A3** | A compression cell (`COMPRESS`) | - | O88-COMPRESSION-PLAN.md 12.1 | **refused** - the compressor rides in `DISKOPS.DRV` with an ABI of its own |

## X. Refused elsewhere, recorded so nobody re-derives them

* **`WF_DESK` in `W_FLAGS`'s low byte** (~-11 across the `WM_CHROMELESS`
  sites) - no free bit (ksp13/desk.md).
* **`SP`/`EP` as one `SYSTEM.CFG` key** - a blob belongs to a ROW (SPEC.md
  51.9.1); merging would make a driver read another's blob (ksp12/ctrl.md).
* **A kern_mega-only table or SDK** - one SDK serves every kernel (T2, T3).
* **`OSAPI_WM_ONMOUSEUP`/`_ONCLOSE`/`_ONDRAG`/`_ONTIMER` inline on kern_mega**
  - its cell cannot store through CS and a `MEGA_ICWB` stub is eleven bytes
  for a four-byte body (SPEC.md 20.3); the other inline setters on kern_mega
  pay a `jmp`+`nop` cell and an 11-byte stub each (MEASURED span,
  `mega_icw_*`/`mega_icwb_*`), which no ABI shape removes either.

## S. Superseded - taken since it was proposed

| what | taken by |
|---|---|
| The rare cells at six bytes (from eight) | kernel size pass 4 (HANDOFF-KERNEL-SIZE-P5.md batch 4, -402 each kernel) - T1 is its follow-on |
| The inline cell; withdrawn cells deleted rather than held (the free list) | kernel size pass 4 (SPEC.md 20.3, 20.3.1) |
| `MOD_NENT` 4 -> 8 (UIHELPERS-PLAN 20.9) | `MOD_NENT` is gone: a module's slot block is its own entry count (HANDOFF-KERNEL-SIZE-P5.md batch 18) |
| `MAX_TASKS` in the snapshot ABI (STACK-SLOTS-PLAN 7.4: "+5 bytes a slot, one flag day") | `SS_TMAX equ 16` pins the layout so the count moves without an offset moving |
| `CC_STACK` an unguarded mirror of `SCH_STACK` (STACK-SLOTS-PLAN 12.4) | `CC_STACK equ SCH_STACK` in `apps/cc/crt0.asm`, held by `t_mirror` |
| No `os88_mem_movable` in the C SDK (HEAP-UNPIN-PLAN) | it exists (`apps/cc/os88.h`) |
| The RAM disk's class 5 -> 8 | `PKG_FMT` 8 -> 10, `DRV_VER` 9 -> 11 (SPEC.md 62.9.19, the lock's history) |
| The 512-byte heap granule for drivers | `DRV_VER` 11 -> 12 (the lock's history) |
| `OSAPI_WM_DESK` verb 4 for the shared Sticky App library | built with the owner's approval (STIKLIB-PLAN, SPEC.md 110.15) |
| `OSAPI_FSX_SPK` thin door; `OSAPI_DRV_SUSPEND`'s mask | R4, R5 |

## V. Speed - rows that would make a HOT path faster

Opened by kernel hot-paths pass 1 (`docs/plans/khp1/BRIEF.md`, 2026-11-06).
The sections above are mostly about BYTES. This one is about CYCLES. Each row
is a frozen interface that a hot path pays for on every call, priced in
cycles on the 4.77 MHz 8088 (MartyPC guest cycles unless it says
PREDICTED), in user-visible milliseconds for a named operation, and in bytes.
A row here can cost bytes and still be worth a flag day, because it buys
time. Every row carries the same status words as above. Ids are `V<n>`.
Rows found elsewhere that are speed trades are cross-referenced rather than
copied: **T2-2** (section 10) is one, trading kern_big's 52-cycle inline
`OSAPI_SET_COLOR` against kern_mega's conventional bytes.

| id | what changes | who it breaks | what it buys (cycles, ms, bytes) | source | status |
|---|---|---|---|---|---|
| **V1** | Let `OSAPI_GFX_FILL`, `_PIXEL`, `_HLINE`, `_VLINE` and `_FRAME` **clobber AX..DX** instead of "clobbers: flags" (SPEC.md 5.7's last paragraph is why `sw_rect` pushes eight registers) | every package that keeps a coordinate in AX..DX across a drawing call (+2..4 bytes a site there) | drops four push/pop pairs from every rect primitive: **~120 cycles, ~25 us of a ~510 us small call (~5%)** on the 5150; kernel -8 bytes. MEASURED call, PREDICTED total | khp1/gfxcall.md 5 | **proposed** - the largest per-call lever found, and the one the 756 us floor is partly made of |
| **V2** | `OSAPI_TASK_YIELD` (0x009E) as a cell that enters the yield directly, building `task_yield`'s frame under the far return instead of the 8-byte SLOT's `push ds / push cs / pop ds` and near call | every cell after 0x009E moves (renumber); every package that yields | **~60 cycles, 13 us of a 283 us yield (4.5%)**, after this pass's sched-1 made the yield 644 -> 283 us | khp1/sched.md 5 | **proposed, low** - fold into any flag day, not worth one |
| **V3** | Promote `OSAPI_XMEM_RUN`, `_WALK` and `_SEL` (0x04B3..0x04CB) from rare 6-byte cells to hot 8-byte SLOTs at the next renumbering | the renumber | ~85 cycles a call; a player streaming from XMS at a few calls a frame: ~0.1 ms a second; +6 bytes `.text` | khp1/sched.md 5 | **recorded** - only with a renumber (T1 is one) |
| **V4** | `OSAPI_GFX_SAVE`'s buffer stored in bank order on the 1bpp adapters, so `sw_xfer` walks banks as mono-1's rect walk does, without a per-row wrap test | any caller that indexes its own save buffer (none found in `apps/`, but the layout is published, SPEC.md 5) | ~20 cycles a row, ~4% of `sw_xfer`, **~2 ms of a CGA raise**, for ~+40 bytes. PREDICTED | khp1/mono.md 5 | **recorded, low value** - so nobody re-derives it |
| **V5** (addition, no bump) | A **batched rect call**: N fills or frames in one far call, the clip-region and display questions asked once | none (a new slot at the end) | per extra primitive the far cell (~165 cycles) and the head (~300): a Calc or Minesweeper redraw of ~30 cells **~14k cycles, ~3 ms**; ~60-80 bytes `.text`. PREDICTED | khp1/gfxcall.md 5 | **proposed as an addition** - wants a package that would use it first |
| **V6** (recorded) | A length-counted `OSAPI_FONT_RUN` (CX = cells) instead of ASCIIZ | every text caller | ~14 cycles of ~150 a cell, under 1% of a run | khp1/text.md 5 | **refused by its finder** - not worth a cell contract |

vga, pointer, wm and files found none. disk found none worth proposing: carrying
an image's and its bss's sizes outside the file would let the loader skip its
header peek, but after disk-4 that peek already does the read's work
(khp1/disk.md 5).

## 10. Rows from kern_mega size pass 2's agents

Folded in at the merge from each agent's *WANTS AN ABI BREAK*
(`docs/plans/completed/msp2/<agent>.md`). megados and bootonce found none
(everything they touched is kernel-internal and versioned with the kernel).
Ids carry the kind's letter and a `2` so they cannot collide with the audit's.

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **L3** (corroborated) | pmcore priced the same row from the other end: retire `mega_gpfix`, `mega_gpw`, `mega_null0`/`mega_db` **and** `mega_strcal`/`mega_strfix` and `mega_tramp`, with `PKG_FMT`/`DRV_VER` refusing a pre-flag-day image on kern_mega only (HDDTOOL's code-alias `rep movsb` is why `DRV_VER` goes too) | as L3, kern_mega only | **~1,400 `.mega` + ~60 `.text`** ESTIMATED from the `mega_gpfix`..`mega_strfix` span (the trampoline and the string-restart state are the `.text` part), and the 286 string-restart calibration with it | msp2/pmcore.md | **proposed** - one row with L3; L3's "up to ~1,190" is the narrower span |
| **T2** (corroborated) | the hot cell's two `nop`s (88 `.text`) and the one FJ cell's four | - | - | msp2/pmfd.md | **recorded, not proposed**, as T2 |
| **T2-2** | the ten `MEGA_ICW` inline setter cells made ordinary SLOTs (or one indexed getter/setter cell) - on kern_mega each jumps to an 11-byte stub because a store through CS is illegal there | every package (rebuild); kern_big loses its 52-cycle inline `OSAPI_SET_COLOR` | **~-80 `.text` on kern_mega** (conventional), +0 kern_big, ESTIMATED; a speed trade against the XT | msp2/pmfd.md | **proposed, a trade** - the XT's cycles against kern_mega's conventional bytes |
| **R2-1** | a raw `int 13h` into a buffer above 1MB refused (AH = 09h, as the 8237 would), deleting `mega_hibnc` | any package reading a disk raw into `OSAPI_MEM_HUGE`/XMEM memory; `tests/megaxm`'s `i13` step is the only one known | **-223 `.mega`** (MEASURED span at msp2's tip) | msp2/pmfd.md | **proposed** |
| **R2-2** | `OSAPI_SEG`'s SEG_AIM stack stated as a worst-case number (a driver caller's chain, 16 + the cell) instead of "~40 bytes" | nothing | a checkable contract | msp2/pmmem.md | **proposed - wording only, no bump** (an `abilock.py --update` if the SDK prose moves) |
| **R2-3** | `OSAPI_SEG`'s 32 window rows (`MEGA_WTN`, SPEC.md 50.6.11) raised to 64 - since msp2 the rows are above 1MB, so the count costs pool and no conventional byte | nothing that fits in 32; a contract number | ~128 bytes of pool; eases C64's and RealDMG's window refusals | msp2/pmmem.md | **proposed** - a clarified contract rather than a moved one, could ride any bump |
| **R2-4** | `OSAPI_FSX_SPK`'s two open verbs (AL = 0 real mode, AL = 2 protected) as one, the DOOR always writing IRQ0's vector | every package using `apps/os88spk.inc` (rebuild) | ~-15 `.text` kern_mega, a few bytes kern_big, the library's real-mode vector write in each package; mostly a cleaner contract | msp2/shared.md | **proposed** |
| **R2-5** | `FSXF_REAL`'s answer returned in a register the door already uses (or a query slot), not by `fsx_run` rewriting the caller's `kent_di` frame | the packages that set `FSXF_REAL` (the Video Player's PM arms) | ~-10 `.text` kern_mega | msp2/shared.md | **proposed, low** |
| **R2-6** | heap claims named by HANDLE, not by segment value - deletes `MEGA_ISHI`'s dual paths in `OSAPI_MEM_FREE`, `REGROW`, `MOVABLE` and the P cell | every package that claims memory, the C SDK | **several hundred `.cold` bytes on kern_mega** (the forks measure +48, +97, +42, +88, +47 in their routines) | msp2/shared.md | **recorded** - a design question larger than a flag day row; pairs with C1 |
| **P2-1** | a module header word holding the image's DATA selector, so CSDO is `mov ds, [cs:MOD_H_DSEL]` - a read through CS, legal on a code alias | the module header (`MOD_VER`, `tools/os88mod.py`, every module - all built with the kernel, so no shipped package) | ~0 bytes, **~5x faster at every CSDO site** in every module on kern_mega | msp2/modcsdo.md | **proposed** - kernel-private (a module is versioned with its kernel), so it needs no `PKG_FMT` flag day at all |
| **F2-1** | a call filter in the packed formats: `E8 rel16` stored as its absolute target before LZ, so near calls repeat and pack | the `'CZ'` container's decoder on every kernel, every packed package, driver and module (SPEC.md 20.13) | an unmeasured share of every call-heavy packed file; it is what made modcsdo refuse a near door per module (-611 image, +433 packed) | msp2/modcsdo.md | **proposed, unmeasured** - wants a host-side experiment over the shipped files first |

### 10.1 Kernel size passes 17 and 18

Pass 17's agents' rows (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P18.md`
section 4, folded in here by pass 18's coordinator). Every figure is an
ESTIMATE from the finder's reading.

| id | the break | kern_big | callers | source | status |
|---|---|---:|---|---|---|
| **K17-1** | **Five-byte rare cells** (`call api_rX / dw target`, the cell's `push bp` into the five stubs) | ~-90 net (-119 table, ~+30 stubs), ~+20 clocks a rare call | every package (renumbers the table) | ksp17/ui.md 1; pass 13 priced ~-105 and the owner declined it then | **refused once, re-priced** |
| **K17-2** | **The seven window-flag setters as one `OSAPI_WM_FLAG`** (SIZABLE, SNAP, SAVEU, OWNBG, KEEPH, NOANIM, QUIET - 129 bytes of bodies measured) | ~-65 to -75, -36 more if the six retired cells are renumbered away | 113 source files under `apps/` | ksp17/wm.md 1 | **proposed** |
| **K17-3** | **The ten handler setters as one cell** (ONSIZE, ONMOUSEUP, ONRESIZE, ONDRAG, ONTIMER, ONWAKE, ONCLOSE, ONRCLICK, ONCLICK, ONWHEEL - ~55 bytes of inline cells) | ~-40 to -48 if renumbered | 218 references in `apps/` and `drivers/`, plus `os88.h` | ksp17/sweep.md 1 | **proposed** - pairs with K17-2 as one window-field setter by index, ~-140 to -160 together |
| **K17-4** | **Thirteen not-hot SLOT cells as rare cells** | -26, ~+85 clocks a call | those cells' callers; each wants a call-frequency reading first | ksp17/ui.md 2 | **proposed, low** |
| **K17-5** | **Retire the duplicate `drv_pkg_call_x` SLOT at 0x038B** (the XCELL at 0x0348 is a second door) | -8 | that cell's callers | ksp17/ui.md 3 | **proposed, low** |
| - | `OSAPI_FSX_CAPS` answering `VID_VESA` in DL | -7 | nine packages compare with `VID_VGA` | ksp17/draw.md | **refused by its finder** |

**Pass 18** (`docs/plans/completed/ksp18/`): ten agents found **no new
break**. Two notes against the rows above, from `ksp18/wmui.md`:

* **OSAPI_WM_TIMER's kern_big target is now the body itself** (the
  `wm_timer_c` wrapper is gone), so under K17-3 the timer setter joins with
  no wrapper to retire.
* **`OSAPI_SEG`'s refusal shares its exit only on kern_big**; a break that
  reshapes it can give kern_small the same shape.

## 11. Defects and stale text found by the audit (none is an ABI change)

* **SPEC.md 20.3's slot-contract block still gives `OSAPI_SND_PLAY` the
  BLOCKING-clip contract** ("BLOCKS for the clip; a mouse click aborts it,
  out AX = 0 ok / 1 busy ..."). It has been the AL = 0 GRANT / 1, 2 RELEASE
  door since `PKG_FMT` 6 -> 8 (SPEC.md 34.4, 20.2.0's own paragraph).
* **`kernel/kernel.asm`'s table carries a stale comment block**, "every
  published slot keeps its NUMBER ... everything ADDED since starts at
  0x0200", above the `(dropped)` arena lines - the opposite of SPEC.md
  20.3.1 today.
* **The 8-byte cold cell macros** (`OSAPI_CXCELL`, `OSAPI_NCELL`,
  `OSAPI_CSLOT`, `OSAPI_FARCELL`, `OSAPI_JSLOT`) are still defined and no
  cell uses them (SPEC.md 20.3 says so) - zero bytes, but a trap for a reader.
* HEAP-UNPIN-PLAN's "`apps/cc/os88.h` has no `os88_mem_movable`" is stale.

## 12. Sources

Every *WANTS AN ABI BREAK* section under `docs/plans/completed/ksp12/`
(ctrl, files, hma, imgmount, rom, round, theme, xfile), `ksp13/` (assoc,
desk, dispsw, links, misc, theme, wkcore, wkfs, wkgui, xfile), `ksp14/` (api,
files, fold, menubar, nopin, reach, serial, vesa), `ksp15/` (blob, fsys, gui,
recent, snd) and `msp1/` (pmcore, pmfd, pmmem, shared) - most answer
"None", and those are not repeated above; `ksp10/core.md` and
`ksp11/{drivers,picomem}.md`, which have no such section but price one;
HANDOFF-KERNEL-SIZE-P5, P7, P9, P13, P14 and P16; LAST-DROP-BYTES.md 7.7.7,
7.7.8, 7.11 and 7.13; CONTENT-COORDS-PLAN.md 6; HEAP-GRANULARITY-PLAN.md 3.3;
KERN-SMALL-CUT-PLAN.md 5.2; KERN-DOS-PLAN.md; KERN-MEGA-PLAN.md 2.5 and items
46-50; STACK-SLOTS-PLAN.md 7.4 and 12.4; UIHELPERS-PLAN.md 20.9;
MULTISELECT-PLAN.md 8; WINDOWED-DRAW-PLAN.md W1; O88-COMPRESSION-PLAN.md
12.1; STIKLIB-PLAN.md; HANDOFF-APPS-SHARED.md. The caller census behind T4,
C3, C7 and C8 counted every `OSAPI_*` token per package directory under
`apps/`, `drivers/` and `tests/` at `ead16c61`.
