# Kernel size pass 17: the record, and what is left for an eighteenth

**PASS 17 HAS LANDED** on `kernel-size-17`, cut from `elendilon` at
`af5579d2`. The file is named `-P18` for the reason the records before it are
named one ahead: each pass's record is named for the pass it hands to.

The brief is `docs/plans/completed/ksp17/BRIEF.md`; each agent's note is
beside it (`fresh.md`, `files.md`, `disk.md`, `wm.md`, `draw.md`, `ui.md`,
`sys.md`, and the second wave's `sweep.md`) and is the authority on its own
items. This file is the summary, the ABI breaks gathered, and what is left.

## 0. THE BRIEF AND THE OUTCOME

The owner's words: *"another somewhat small round. ... The goal is another
512b of bytes recovered. The targets will be the recently added unsized work,
plus an indepth whole kernel pass (which is likely where the bulk will come
from). ABI breaks are still being gathered - likely to be taken next round
rather than this round."* Seven agents at once, and an eighth (`sweep`)
started when `files` and `wm` had landed.

**The goal is met: kern_big resident -755 against a goal of -512.**

Kernel resident (`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`,
plus `.mega` on kern_mega), assembled:

| kernel | base (`00348bdc`) | tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---|
| **kern_big** | 91,879 | **91,124** | **-755** | 97,280 -> **96,768** |
| kern_small | 57,718 | 57,208 | -510 | 59,904 -> **59,392** |
| kern_emu | 92,133 | 91,378 | -755 | 97,792 -> **97,280** |
| kern_mega | 125,183 | 124,468 | -715 | conventional 62,976 of 68,096, unchanged |

kern_big at the tip: `.text` 40,542, `.bss` 5,411, `.cold` 37,445, `.lowbss`
5,358, `.vgabuf` 336, `.vga` 2,032; `.boot2` 6,455 and `.ovl` 5,116 are
unchanged. Every kernel came down a 512-byte `KERN_SIZE` step but kern_mega's,
whose footprint is the conventional rungs. `tests/megaseg.txt` stays at
3,250 (sys's kern_big-only `mem_region_reloc` saving took the figure 3,248 ->
3,250).

Off the system disk: **FSCHECK.DRV -255** unpacked (5,679 -> 5,424), -91
packed (fresh); CTRL.DRV +2 and FILECP.DRV -5.

## 1. WHO TOOK WHAT (kern_big resident)

| agent | scope | kern_big | the shape of it |
|---|---|---:|---|
| **wm** | `wm.inc`, `instance.inc`, `clip.inc`, `events.inc`, `dock.inc`, `apps.inc` | -157 | eight single-caller routines at their call; `inst_tplbuf` (16 `.bss`) gone - `app_launch` offsets the template in place; `wm_cov_c1..c4` on `wm_clip_split`'s scratch (lifetimes proved disjoint) |
| **ui** | `ui.inc`, `menu.inc`, `toast.inc`, `fprog.inc`, `snd.inc`, `sched.inc`, `kernel.asm` | -128 | the menu bar's helpers saved what their caller had already saved (-91); `ui_sys_find`/`ui_tm_sysdir`/`ui_drag_phase` at their call; three rare-cell stubs reach `api_far` by `push cs / call` |
| **fresh** | the work since pass 16 | -87 | `dsk_xfer`'s 64KB staging through `.bounce`'s own copier (938 -> 888; the BIOS sector loop byte-identical, the driver arm ~55 cycles a run faster); `osapi_vol_check` 73 -> 44 on `mod_check`'s compare; FSCHECK.DRV -255 |
| **disk** | `disk.inc`, `diskw.inc`, `loader.inc`, `lz.inc` | -85 | eight single-caller routines at their call; error stanzas as `SKIPF2` chains onto one `cbw/stc/ret`; `ld_lname_cp` on `fm_ncpy` |
| **draw** | drawing, fonts, mouse, video detection, fsx | -82 | `kbm_accel`, `mou_claim`, `gfx_pt_resolve` and others at their call; `vid_text` out of the splash's banking; the serial mouse ISR ~95 clocks a packet FASTER on a settled port |
| **sys** | heap, modules, drivers, clock, xmem, hibernate, placement | -78 | `driver.inc` -43 (pass 16's `drv_load_at` DX bank, dead reloads, one-compare range check); `clk_mlen` by arithmetic; placement lever confirmed spent |
| **sweep** (wave 2) | a second look at files' and wm's files | -75 | hand-banked frames onto the shared ladders; dead `fm_dgon`; `inst_parksafe_set`/`inst_restart_set` one entry; `cbw` where AL is a known small index |
| **files** | the Disk window, dialogs, desk, assoc, icons | -63 | dead frames and banks; `fm_find_view`/`fm_front_win`/`fm_rows_only` at their call |

Pass 16's lesson 4 held again: **single-caller routines and callee-saves no
caller reads were the largest share of every finder's bytes.**

## 2. HOT PATHS AND PARITY

Nothing on the hot-path list got slower. `dsk_xfer`'s BIOS per-sector loop,
`dsk_runcap`, `dsk_next_clus_x`, the LZ and directory-scan loops are
byte-identical; `wm_paint_all` and the damage-repaint path, the serial mouse
ISR and the driver arm of `dsk_xfer` got faster. **No new parity pad** - one
was REMOVED (`gfx_scroll`, draw item 13). Every agent chose between
equivalent forms to keep shifts even; draw.md lists four such choices, which
are pads in all but name for the next pass's pairing. `hotpar` is green on the
merged tip.

## 3. WANTS THE OWNER

1. **One write generation for the machine instead of one per volume**, -13
   resident (fresh.md; FSCHECK.DRV -9 more). A write to ANY volume would end
   a System Check in progress with "changed", where today only a write to
   the volume under check does; SPEC.md 118.14.6 says per volume. A
   follow-on (-4, the bump inside `dsk_rah_wr`) needs the same decision.
2. **Draw item 7, already TAKEN and revertible for +7**: the Mode X CR11
   unlock writes a fixed `0x0E11` where it read-modified-wrote. The final
   register state is identical on any card; only the transient low bits
   during the mode set can differ, and only on a clone BIOS whose mode 13h
   CR11 is not IBM's `8Eh`. Kept because nothing a user sees can differ;
   say if it should go.

Pass 16's three section-3 items (shortcut open, its popup, the About box into
modules) still stand undecided and were not re-proposed.

## 4. ABI BREAKS GATHERED (none taken)

Every figure is an ESTIMATE from the agent's reading unless marked; each is
a flag day on the API table (SPEC.md 20.8 rule 4, `PKG_FMT` and
`tools/abilock.py`).

| # | the break | kern_big | callers | source |
|---|---|---:|---|---|
| 1 | **Five-byte rare cells** (`call api_rX / dw target`, the cell's `push bp` into the five stubs) | ~-90 net (-119 table, ~+30 stubs), ~+20 clocks a rare call | every package (renumbers the table) | ui.md 1; pass 13 priced ~-105 and the owner declined it then |
| 2 | **The seven window-flag setters as one `OSAPI_WM_FLAG`** (SIZABLE, SNAP, SAVEU, OWNBG, KEEPH, NOANIM, QUIET - 129 bytes of bodies measured) | ~-65 to -75, and -36 more if the six retired cells are renumbered away | 113 source files under `apps/` | wm.md 1 |
| 3 | **The ten handler setters as one cell** (ONSIZE, ONMOUSEUP, ONRESIZE, ONDRAG, ONTIMER, ONWAKE, ONCLOSE, ONRCLICK, ONCLICK, ONWHEEL - ~55 bytes of inline cells) | ~-40 to -48 if renumbered | 218 references in `apps/` and `drivers/`, plus `os88.h` | sweep.md 1; pairs naturally with 2 |
| 4 | **Thirteen not-hot SLOT cells as rare cells** | -26, ~+85 clocks a call | the thirteen's callers; each wants a call-frequency reading first | ui.md 2 |
| 5 | **Retire the duplicate `drv_pkg_call_x` SLOT at 0x038B** (the XCELL at 0x0348 is a second door to it) | -8 | that cell's callers | ui.md 3 |
| - | `OSAPI_FSX_CAPS` answering `VID_VESA` in DL | -7 | nine packages compare with `VID_VGA` | draw.md - priced NOT worth it |

2 and 3 together are one design (a window-field setter by index) and ~-140
to -160 if both renumber.

## 5. DEFECTS FOUND

1. **`kernmods` was red at the base**: `kernel/fsck.inc` (System Check) had
   no row in `tools/kernsize.py`'s THEMES. Fixed by the coordinator (the file
   system's row).
2. **`docs/INDEX.md` was stale at the brief's commit** (the brief itself
   added a file), so `make` failed in every fresh worktree; every agent
   regenerated it, and every merge after the first conflicted on it (pass
   15's and 16's lesson again).
3. **`vid_pop8`'s header and SPEC.md 15.1.2 said `vid_text` runs from the
   loading screen**; nothing does. Corrected (draw).
4. Not fixed: `gfx_ls_box`'s banner claims to preserve every register and
   spends BX; its one caller reloads it (draw). Pass 16's `wm_dg_precover`
   banner item still stands.
5. **Every build regenerates the tracked `apps/wireball/art/wbfront.inc`
   with different bytes** on this box; every agent restored it. Not a size
   item - a host-converter determinism question for WireBall's owner.

## 6. WHAT IS LEFT

- **Held for parity, ready to pair** (each built, each odd alone): sys's
  `str_len` (-1), `mem_region_reloc`'s `cbw/xchg` (-1), `dsk_fatw_pick`'s
  jump to a `ret` (-1, flips `dsk_next_clus_x`); ui's `toast_say` into
  `.cold` (-4) and `sched_stop`'s two interrupt windows (-3). Taking these
  is a pairing exercise across rows, ~-10.
- **CROSS-FILE, priced and free**: `wm_dg_precover` into `ui_drag` (~-8,
  wm.md); `toast_place` taking `str_len` inline (~-7, sys.md); `fmv_ld_ent`
  at its one call in `loader.inc` (~-5, sweep.md); `osapi_fs_ent_x` on
  `osapi_desk_item_x`'s tail (-3, pass 16's, still not taken); `dsk_unit`
  after `dsk_op` for one word load in `dsk_xfer` (-4, fresh.md); a
  `ct_cw_toast_show` trampoline (-4 if all five far sites move, sweep.md).
- **`mod_needi`** - the `push ax / mov al, id / call mod_need / pop ax`
  shape at ~15 sites as `call mod_needi / db id`, ~-15 net, kernel-internal,
  spans five rows' files (sys.md REFUSED 3).
- The placement lever is spent without a function change (sys confirmed
  pass 16's reading; the only new resident routines were fresh's).

## 7. WHAT WAS RUN

Each agent ran its own rows once after its last code commit (the notes list
them, all green). The coordinator ran:

- **The deduplicated union of every agent's rows, once, on the merged tip**
  (`tools/os88soak.py`, run `20261102-190326`, Bochs and unicorn present so
  the `mega-*` and `fsckmega` rows ran): **171 ok, 0 FAIL, 0 SKIP** in 32
  minutes.
- **`test-full`, once: 82 ok, 0 FAIL, 3 SKIP** (`w16cens`, `w16censall`
  want the Win16 fixtures and samples; `ctoolchain` wants the C toolchain).
- `make`, `make small`, `make emu` and `make mega` build at the tip; the
  fast tier is 70/70 with one skip; `hotpar` and `megaseg` green.

## 8. METHOD LESSONS

- **Seven first-wave agents on four cores worked** - each took 1.5-3 hours
  of wall time and none blocked on another. The cost was build contention
  (one agent ran two builds in one tree and lost a packed kernel; another
  edited sources mid-build) - `make` in a tree is single-tenant.
- **A brief that adds a file must regenerate `docs/INDEX.md` in the same
  commit**, or every worktree's first `make` fails. This pass's brief did
  not.
- **Two agents' rows overlapped on `dsk_xfer`** (fresh by commit, disk by
  file). Telling the later one at the moment the first merged cost nothing
  and the merge was clean; name a routine in exactly one row next time.
- **The second wave paid again** (sweep, -75 on files already passed by two
  agents): a fresh reader on freed files is worth starting as soon as two
  rows land.
