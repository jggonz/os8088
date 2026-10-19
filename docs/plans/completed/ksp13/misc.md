# ksp13 agent "misc" - what no other concept owned

Kernel size pass 13 (the brief, written as pass 14, is
`docs/plans/completed/ksp14/BRIEF.md` on this branch). Concept: everything
since pass 12's close (`3bb038f3`) that the assoc, theme, desk and links
agents do not own - a parted package's packed image (`a7629386`, not on
`main`); what Timer and Bounce leaving the kernel (#264, `a123653b`) left
behind; #252's PR-side fixes that arrived through `b47de145`/`84a3038c`;
`fsx_page` (#257). Branch `ksp13-misc` (cut as `ksp14-misc`) from
`aedadd25`. Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes, resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.

## BASE AND TIP

Base `aedadd25` (the brief on `4833dfef`, no code).

| | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,757 | **92,702** | **-55** |
| kern_big `.text` / `.bss` / `.cold` | 43,756 / 5,223 / 38,084 | 43,622 / 5,222 / 38,164 | -134 / -1 / +80 |
| kern_big `.boot2` / `.ovl` | 5,129 / 5,106 | 5,129 / 5,103 | 0 / -3 |
| kern_big `KERN_SIZE` | 98,816 | 98,816 | no rung moved (`.cold` +80 of the 316 its rung had left) |
| **kern_small resident** | 60,260 | **60,231** | **-29** |
| kern_small `.text` / `.bss` / `.cold` | 31,847 / 3,063 / 22,482 | 31,825 / 3,062 / 22,476 | -22 / -1 / -6 |
| kern_small `.ovl` / `KERN_SIZE` | 1,480 / 62,464 | 1,477 / 62,464 | -3 / 0 |
| **kern_emu resident** | 93,016 | **92,961** | **-55** |
| kern_emu `KERN_SIZE` | 99,328 | **98,816** | **-512**: its image rung, uncrossed |
| modules, drivers | | | none touched |

`.text` -134 and `.cold` +80 on kern_big are mostly MOVES: three routines
that #252 had put in `.text` "by size alone" (for a `.cold` rung) went back
beside their callers in `.cold`, which is where the near calls pay.

## WHO OWNS WHAT, measured

The brief's table gave this concept `ui_task` +30, `ui_drag` +11,
`loader.inc`'s `ld_alloc` +30 / `ld_start` +13 / `ld_pkg_byname` +9 and
`apic_inst_minimize` +32. Traced with `git log -S` and
`git diff 3bb038f3 HEAD`, none of those is misc's:

| routine | hunk | owner |
|---|---|---|
| `ui_task` +30, `ui_drag` +11 | the `WF_DESK` press-without-raise, the posted DRAG, `wm_dmg_npro` | **desk** (#251) |
| `ld_alloc` +30, `ld_start` +13, `ld_run_body_x`'s `test [ld_cfmt], 3` | `OS88_F_DESK`'s top-down record, `IF_DESK` | **desk** (#251) |
| `ld_pkg_byname` +9 | `lnk_is` | **links** |
| `apic_inst_minimize` +32 | rdiff's name for the API table's TAIL: `OSAPI_CLK_GET`, `OSAPI_WM_DESK`, `thm_desk`/`gfx_fill_gray`, `OSAPI_THM_SET`'s ICELL, `OSAPI_ASSOC_FLUSH` | **desk**, **theme**, **assoc** |
| `inst_alloc_di` | the desk widget's top-down record | **desk** |

What was misc's: `ld_hdr_ok`/`ld_check_hdr` (+33 `.cold` both kernels,
`a7629386`); `app_launch`, `inst_kinds`, `ui_cmd`, `cw_wm_content` and the
deferred launch (#264's leftovers); `dsk_img_orphan` (+42 `.text`),
`dsk_img_host` (+23 `.text`), `dsk_vol_rmv` (+19 `.cold`), `fcp_lrec` (+40
`.text`) and `fm_dgdrop`'s list test (#252); `fsx_page` +10 (#257).

## TAKEN

Per item, kern_big / kern_small resident.

* **`ld_hdr_ok` stores the packed length itself** (-6 / -6, `.cold`).
  `a7629386` read the link word twice - `cmp word [es:si+4], 0` in
  `ld_hdr_ok`, then `mov ax, [es:si+4] / or ax, ax / jz` in `ld_check_hdr` to
  store it. `ld_hdr_ok` now loads it into AX for its own zero test and stores
  `[ld_fsz]` in its package branch: -7. AH is dead at all three callers
  (`ld_check_hdr` and `drv_check` document AX clobbered; the re-home arm's
  next call, `ld_hdr_size`, writes it) and DX is untouched, so `drv_check`'s
  contract holds. The store comes before the flags test, so a REFUSED
  package header with a link word leaves it in `[ld_fsz]` - nothing reads
  `[ld_fsz]` after a refusal (steps 3-6a are its only readers, all behind the
  check, and the re-home's check runs after step 6). One byte of the seven is
  spent on parity: `mov word [ld_fsz+2], 0` (6) for `and` (5), which keeps
  the FAT walk (`dsk_next_clus_x`, the read-ahead, `dsk_copy_seg_x`) at an
  even shift. **Cost of SPEC.md 20.13.8 now +27, from +33.**
* **`ui_cmd`'s About is the launch tail** (-4 / -4). `.about` was `mov al,
  KIND_ABOUT / jmp .launch`; `.launch` had no other caller once Timer and
  Bounce left. AL is already `CMD_ABOUT - 1` from the dispatch's `dec ax`,
  which is `KIND_ABOUT`, asserted at assembly.
* **`app_launch` shows through `ui_lcall`** (-4 / -4): `mov bp, wm_show /
  call ui_lcall` for the three calls, `call bp` on the held arm. BP's last
  use as the record is the line above. +2 bytes of UI-task stack, ~30 clocks
  a built-in launch.
* **`inst_kinds` without `KD_PAD`** (-8 / -6). #264 kept the dead
  `KD_TASK` word "so the stride keeps its power of two". `mov ah, KD_SIZE /
  mul ah / add ax, inst_kinds / xchg si, ax / mov al, [inst_knd]` is the same
  eleven bytes as `cbw / mov si, ax / mov cl, 4 / shl si, cl / add si,
  inst_kinds` (`add ax, imm16` is a byte shorter than `add si, imm16`, which
  pays for the reload), so the stride is 14 and the pad a word a row: four
  rows on kern_big, three on kern_small. `mul r8` is ~50 clocks more a
  built-in launch, against a window being created. `aad 14` (two bytes) was
  not used: a NEC V20, a common XT upgrade, ignores AAD's immediate.
* **`cw_wm_content` is its cell** (-4 / -4). Size pass 9's "the cell is the
  door" rule excluded it, with `task_sleep`, `task_yield` and
  `wm_clip_set`, because the Bounce WORKER reached it on a 128-byte slice.
  Bounce is a package; what is left is the UI task's (the Disk window's
  layout, self-repaint fill and click conversion, the Control Panel, the
  hibernate card), once an operation each: ~50 clocks and 2 bytes of UI
  stack a call. The cell forces DS = KERNEL_SEG, which the shim assumed.
* **`dsk_img_host` written into `dsk_vol_rmv`, `dsk_img_orphan` into
  `.cold`** (-14 / 0). Both were `.text` far routines "by size alone, for a
  `.cold` rung" - the reason the brief refuses. `dsk_vol_rmv`, the host
  lookup's one caller, now does it inline (DS loaded from the claim, one byte
  shorter than the ES form), and its orphan exit is `dec bx` on the refused
  lookup's BX = 0 (BL = 0xFF; `dec` keeps CF = 1; every caller reads BL
  alone). `dsk_vol_del` calls `dsk_img_orphan` near. -13, and `.text`'s
  odd -65 let `disk.inc`'s `align 2` pad drop a byte.
* **`fcp_lrec` written at its one call** (-6 / 0): `fcp_lnext`'s record
  copy is in `fcp_lnext` again, byte for byte the routine's body under the
  same `pushf`/`cli`, without the far call and the `retf`. FM_MSEL is
  kern_big's alone.
* **`fsx_page`'s Hercules arm** (-6 / -2). `vid_dual_ok` PRESERVES AX (its
  header says so, and `vid_disp_init` relies on it), so #257's `push ax /
  pop ax` round it went; the dual-card arm sets BH = 1 and falls into the
  single arm's `or al, al / jz .h0` (AL is 0 there) instead of jumping; and
  `mov bh, 3` before `.h0` is dead since #257 made BX = 0x030A up front. A
  page call is once a frame and ends in a retrace wait; the dual path is ~25
  clocks faster.
* **The deferred launch is the Disk window's, and a byte** (-3 / -3; `.ovl`
  -3). `inst_launch_post` took AL = any kind while the Builtins menu posted
  Timer and Bounce through it; its one caller now is `fm_newwin`. It stores
  `KIND_FILES + 1` into a byte and reads no AL (10 bytes from 12);
  `inst_init` clears it with the AL it already has (in `.ovl`). **`ui_task`
  is touched by one instruction**: the drain's `xchg ax, [inst_launch]`
  becomes `xchg al, ...` - the same bytes and **8 clocks less on every idle
  pass** on an 8088 (the word's second bus cycle each way).

Docs: SPEC.md 20.13.8 (the store and the cost), 29.3 (stride 14, the task
word's history), 14.6/14.8's two `KD_PAD` mentions, 18.101.1 and 18.101.7
(`dsk_img_host` gone), 22.27 (`fcp_lrec` gone), 29.4's
`inst_launch_post` row, 13's drain step, and the `task_spawn` caller list
(app_launch is not one since 14.8).

### Hot paths and parity

No hot routine got slower; `ui_task`'s idle pass is 8 clocks FASTER.
`tools/os88rdiff.py kernel aedadd25 . --hot ...` over `gfx_blit4`,
`gfx_blit1_x`, `font_run`, both mouse ISRs, `kbm_isr`, `sch_isr`,
`sch_switch`, `api_x`, `dsk_xfer`, `dsk_runcap`, `dsk_next_clus_x`,
`dsk_copy_seg_x`, `ui_task`: every one identical and at an even shift on
kern_big, kern_small and kern_emu (kern_emu with `--equ-ok
'.*_SEG|COLD_HOME'`, its cold segment having moved down a rung), except
`ui_task`, which is CHANGED by the one `xchg` and nothing else. The first
build put the FAT walk at +53; the `dec bx` and the `mov word` put it at +52.
`.bss` is one byte shorter after `inst_launch`, so 468 `.bss` symbols moved
by an odd amount (282 even -> odd, 186 odd -> even) - the base's own `.bss`
parity was mixed, and an 8088's 8-bit bus has no alignment cost.

### Equivalence

No row reaches a malformed header or an orphaned image, so the five
routines with branchy contracts were run against the BASE's own bytes in
Unicorn's 8086 core: both trees assembled with `[map all]`, `.text` and
`.cold` placed by the map, each routine called near or far as its callers do.

* `ld_check_hdr`: **75,600 headers** (version x flags x link x image x bss x
  entry x dispatcher x a 32-bit file size), 816 accepted - CF, AL on a
  refusal, BX..SP, and on acceptance `[ld_fsz]`, `[ld_img]`, `[ld_bss]`,
  `[ld_ent]`, `[ld_cfmt]` and DX all identical. The only difference is the
  documented one: 15,120 refused package headers with a link word leave it in
  `[ld_fsz]`.
* `ld_hdr_ok` called directly as `drv_check` and the re-home arm call it
  (AL = `DRV_VER` / `PKG_FMT`): 288 headers, CF, AL on a refusal and BX, CX,
  **DX**, SI, DI, BP identical, and a driver call never stores.
* `dsk_vol_rmv`: 4,000 random volume tables (kind, unit, class, a claim's
  host byte including 0xFF and out-of-range), every live row - CF and BL, and
  BX whole on CF = 0. `dsk_img_orphan`: 12,000 calls, the claim bytes and
  every register but BX (an end-of-table pointer, relative to `dsk_vtab`)
  identical. Broken on purpose (`dec bx` -> `inc bx`): 4,009 differences.
* `fsx_page`: 810 states (bracket kind, `vid_kind`, `vid_avail`, AL) - the
  OUT sequence to 3BFh/3B8h/3D4h up to the retrace wait's first IN, CF and
  the registers identical; the four Hercules outcomes (dual page 0 writes
  3BFh = 1, single page 0 and page 1 write 3, page 1 sets 3B8h bit 7, dual
  page 1 refused) each covered.
* `app_launch`, `ui_cmd`, `fcp_lnext`: the instruction diff
  (`os88rdiff --show`) is exactly the edits above and the `KD_*` offsets.

`tools/stkbalance.py` over the seven files touched, base against tip: the
same 12 unbalanced paths (783 entries walked against 785 - the two routines
that went).

## REFUSED

* **`ld_hdr_ok`'s flags test** (-10): drop it and let a non-zero link mean
  "packed and parted". Then a package header with link = image and no flags,
  or bit 2 alone with link past the image, would LOAD where it is refused
  today - a hostile header's outcome, which the brief's bar covers.
* **`cwd` for `[ld_fsz+2]`**: a packed image of 32K or more (link >= 8000h,
  legal under a 60K image) would get a high word of FFFFh and be refused.
* **The version test in `ld_hdr_ok`'s link branch** (5 bytes): it is what
  keeps a DRIVER header with link != 0 and flags 0Ch refused (and keeps a
  driver load from writing `[ld_fsz]`); every register spelling of it
  (`mov ah, al` up front, `cmp ah`) is the same size.
* **The `and`-form zeroing** (-1): taken as `mov` for the FAT walk's parity.
* **`app_launch`'s `.failj` trampoline**: still needed - the pool scan's
  not-taken `jb .pslot` falls into it.
* **`fm_dgdrop`'s list-member test** (#252, 26 bytes): `mov di, [fm_vp]`
  looks redundant beside the block in DI two lines up, but `fm_hit` returns
  DI as the x it was handed; the window compare needs the hit window, which
  is gone from BX by then. No shorter spelling found.
* **`inst_launch` beside `inst_tab`**, so `inst_init`'s `rep stosb` clears it
  (-3): `inst_init` is `.ovl`, not resident, and it reorders `.bss`.

## WANTS THE OWNER

* **`app_launch`'s at-cap refusal** (`cmp byte [si+KD_CAP], 1 / ja .failj`,
  -6 `.text` both kernels). It existed for Timer and Bounce (cap 10:
  "open Bounce for the eleventh time" must refuse, not front one). The only
  cap above one left is the Disk kind's, whose two launchers - `fm_choose`
  and `fm_newwin` - count `FM_MAXWIN` themselves and never reach it. If that
  is accepted as unreachable, the line goes; if it ever were reached, a Disk
  launch at the cap would FRONT a Disk window instead of beeping. Not
  recommended for 6 bytes - the proof is a reading of two callers and a
  posted launch, and the failure would be silent.

## WANTS AN ABI BREAK

None. `a7629386`'s format (not on `main`) was left as it is; the one
reshape it invites is the flags test above, which is a behaviour change and
not an ABI one.

## CROSS-FILE

* **`fm_newwin`'s `mov al, KIND_FILES`** (`kernel/files.inc`, wkfs): dead
  since `inst_launch_post` reads no AL. -2 `.cold`, both kernels.
* **`cw_task_yield` -> `equ` its cell** (`kernel/kernel.asm`, wkcore): label
  the `OSAPI_SLOT task_yield` cell (0x009E) and make the shim an `equ`, as
  `cw_wm_content` now is. The exclusion's reason (the Bounce worker's slice)
  is gone; its callers - the desktop and Disk-window drag tracks,
  `fm_linger`, `drv_unload`'s wait, the shutdown card - are UI-task loops
  that yield (693 us) each pass, so ~50 clocks of DS switch a yield. -4
  `.text`, both kernels.
* **`cw_wm_timer` -> `equ` its cell** (`kernel/kernel.asm`, wkcore): the
  0x033A cell is `OSAPI_SLOT wm_timer_c` (`wm_timer` + `clc`); the one caller
  (`files.inc`'s scroll-bar pause, once per pointer movement) reads no flag
  after it. -4 `.text`, kern_big. Its comment's "the Timer needs it under
  SBDRAGOFF=1" is stale since 14.8.
* **The byte at `ui_task`'s drain** is the one line of `ui_task` this branch
  touches (desk owns the routine's other hunks this pass): a merge should
  find it alone.

## ROWS RUN

One pass after the last code commit, `--marty-jobs 1`:
**14 passed, 0 failed** in 1,136 s (`os88test.py soak -k ...`):

| row | covers | result |
|---|---|---|
| `pxopen` | a PACKED parted package (`PIXEL.O88`, the one built with a link word) launched through `ld_hdr_ok`'s store | ok 65.8 s |
| `doscom` | a parted package with link 0 (`DOS.O88`) | ok 15.5 s |
| `lzload` | a compressed package's ladder and step 6a | ok 34.7 s |
| `imgmount`, `imgswap` | `dsk_vol_rmv` through `disk_mount`/`dsk_media_ok`, an image on a floppy swapped, unmount | ok 85.3 s, 18.9 s |
| `msel`, `fcpcopy` | a list paste through `fcp_lnext`, the copy engine | ok 84.5 s, 47.2 s |
| `vidpageherc`, `vidpagehercno`, `wireballvideo` | `fsx_page` on a lone Hercules, the two-card refusal, page flips on VGA/CGA/Hercules/dual | ok 39.8 s, 25.9 s, 127.4 s |
| `fdlgchoose` | Nav > New Window (`inst_launch_post`, the byte, `ui_task`'s drain) and the chooser (`app_launch`'s held-lock arm) | ok 86.5 s |
| `thm95` | About from the System menu (`ui_cmd`'s `.about`), `app_launch` | ok 280.0 s (declared 75 s; not investigated - a wall-clock figure on a box shared by eight agents, and the row passed) |
| `timerbounce` | the two packages that replaced the kinds | ok 22.0 s |
| `smalllaunch` | every kern_small package opening on the 128 KB machine (`app_launch`, the loader) | ok 202.5 s |

Broken on purpose, new to this agent: `fsx_page`'s dual-card refusal of
page 1 taken out (`jnz fsx_wait.fail` -> two `nop`s) - `vidpagehercno` went
**red** (0 passed, 1 failed), and green again on the restored tree. The
Unicorn harness was broken on purpose too (above).

## DEFECTS FOUND

* **`tools/os88rdiff.py` could not diff the kernel against `3bb038f3` or
  later** (FIXED, one line): `vga12.inc` includes
  `drivers/thmpal/thmrows.inc` by a ROOT-relative path, and the scratch
  export's include fetcher looked only in the source's directory and the
  `-I` list. It now looks in the export root too. Found on the brief's own
  command, which died at `nasm`.
* **`git stash` is shared by every worktree of a repository.** A stash
  pushed here and an assoc agent's stash pushed a moment later swapped
  places: this tree's `stash pop` applied assoc's work (conflicting in
  `ascw.inc`), and assoc's own pop then took its own back. Nothing was lost -
  assoc's tree had its changes, and this one was restored from a patch of
  its stash entry, which was then dropped by hash - but the next two agents
  to do it may not be so lucky. The coordinator has since warned every
  agent; base comparisons here used a `git worktree add --detach` base tree.
* **SPEC.md's `task_spawn` caller list** still named `app_launch` (a
  built-in kind's `KD_TASK`) after 14.8 (FIXED).
