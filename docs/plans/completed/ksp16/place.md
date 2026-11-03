# ksp16 agent "place" - where code lives

Kernel size pass 16 (`docs/plans/completed/ksp16/BRIEF.md`, row `place`):
the blob, `.cfx`, `.ovl`, `extmod.inc`, `desksc.inc`, `desk.inc`,
`ctrl.inc`, `drv_boot_x`/`ovc_*` of `driver.inc`. Branch `ksp16-place`, base
`714e9d64`. Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes on each kernel's own build; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga`. Module figures are each kernel map's
`MODC_SIZE` (CTRL.DRV's image with its bss) and `MODS_SIZE` (its settings
core).

## THE INSTRUMENT, AND WHAT IT SAYS IS LEFT

Pass 14 reach's call graph, rebuilt in a scratch script over
`tools/os88rdiff.py`'s listing parser (`Build`): every emitted line filed
under its top-level routine and REAL section, every global label (or an
`equ` naming one) on it an edge, a fall-through edge where the previous
routine does not end in `jmp`/`ret` - and **not where either side is a data
directive**, which the first version got wrong and which hid `cp_s_dsdsk`
behind a "fall-through" from the string before it. Then the fixpoint: a
resident label whose every referrer is the blob, `.ovl`, `.cfx` or a module.
The API table (`apic_*`), the ISRs `.ovl` installs, `sch_idle_body` and the
`rmk_*` cell runs are roots.

**On kern_big the fixpoint returns 523 bytes and almost all of it is
already refused, with reasons that still hold**: `vid_tab` (the VESA row is
written by VESA.DRV; ksp14 reach), `vx_null` (a hook target the driver's
detach installs), `img_reloc` (a registered relocation proc), `desk_ico14`
(read at run time through `desk_pico`), `clo_fnbuf` (its own comment), the
debug spans (`clk_dbg_*`, `fdd_dbg_*`, `mou_id*`), CTRL.DRV's one-byte
scratch and persistent state (ksp14 reach), `drv_dlg_done_x`/`fm_ncpye` and
the `dskf_`/`drvf_`/`dkf_` far shims (ksp15 blob), `kbm_i15` (an ISR). What
was NOT refused for a reason that holds was two items, below - both
"DS-passed names and messages" that pass 14 dismissed as
MODULE-SELFCONTAIN-PLAN 5.4's trap, which for these two is not one: the
toast COPIES its line (`toast.inc`, *The string is COPIED*), and a filename
can be STAGED.

**Room**: the brief's three rooms are not what binds this pass - nothing
is left whose move needs one. The blob was 6,656 of 6,656 at the base (full
to the byte, one more than pass 15 left) and is 6,655 now; `.ovl` 5,119 ->
5,116 of 5,120. CTRL.DRV no longer has a cliff: since `aafe18bc` a module's
claim is its file to the GRANULE (`mod_need` -> `mem_bytes_gp_x`), not to
the KB, so "a module must still fit its claim" is true by construction and
an image byte costs a granule's share of heap while the panel is open.
`.cfx` (pass 15's room) took the boot's half of item 2.

## BASE AND TIP

| | base `714e9d64` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,136 | **92,090** | **-46** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 41,289 / 5,418 / 37,703 / 5,358 / 336 / 2,032 | 41,243 / 5,418 / 37,703 / 5,358 / 336 / 2,032 | -46 / 0 / 0 / 0 / 0 / 0 |
| kern_big blob (map `BOOT2_SIZE`) / `.ovl` / `.cfx` | 6,656 / 5,119 / 494 | 6,655 / 5,116 / 530 | -1 / -3 / +36 |
| kern_big `BOOT2_SECS` / `KERN_SIZE` | 13 / 97,792 | 13 / 97,792 | 0 |
| **kern_small resident** | 57,984 | **57,960** | **-24** (`.text`) |
| kern_small blob / `.ovl` / `BOOT2_SECS` / `KERN_SIZE` | 4,956 / 1,477 / 11 / 59,904 | 4,956 / 1,474 / 11 / 59,904 | 0 / -3 / 0 / 0 |
| **kern_emu resident** | 92,392 | **92,346** | **-46** (`.text`) |
| kern_emu blob / `.ovl` / `.cfx` / `BOOT2_SECS` | 6,771 / 5,119 / 494 / 14 | 6,770 / 5,116 / 530 / 14 | -1 / -3 / +36 / 0 |
| **kern_mega** `.text` (all else equal; `.mega` 21,648 unchanged) | 46,592 | **46,546** | **-46** |
| kern_mega blob / `.ovl` / `BOOT2_SECS` | 6,939 / 5,118 / 14 | 6,972 / 5,115 / 14 | +33 / -3 / 0 |
| kern_mega's surplus in the kernel segment (`os88megaseg`) | 3,309 | 3,309 | 0 (the pre-existing +78 over `tests/megaseg.txt`'s 3,231 is not added to) |

| image (unpacked, with bss) | base | tip |
|---|---:|---:|
| CTRL.DRV kern_big (core `MODS_SIZE`) | 15,277 (3,100) | 15,336 (3,191) |
| CTRL.DRV kern_emu | 15,295 (3,101) | 15,354 (3,192) |
| CTRL.DRV kern_mega | 15,473 (3,264) | 15,536 (3,359) |
| CTRL.DRV kern_small | 5,619 (193) | 5,650 (256) |

`t_canary` (fast tier, green): KSIG at memory offset 4096, file sector 21,
crossing a head on all 6 geometries for the one blob length [13], sum
0x6AD6 - unchanged. `t_blobruns`: the blob is 2/2/2/2/2/2 `int 13h` calls
(360/720/1.2/1.44/small360/small720), unchanged. `DSK_OVLPAD` 0 on the
shipped kern_big, unchanged.

## TAKEN

No hot path is touched: both items are the Control Panel's save, its toast
and the boot's SYSTEM.CFG read. `hotpar` green at every commit; **no new
parity pad** (both `.text` deltas are even, -24 and -22).

### 1. The save's no-system-disk line moves into CTRL.DRV (`20a42fb8`) - kern_big/emu/mega `.text` -24, kern_small -24; `.ovl` -3 everywhere

`cp_s_dsdsk` ('Not Saved: No Sys in A:', 24 bytes with its letter) was
resident because `dsk_bootltr` stamps its drive letter at the boot mount and
its one reader, `cp_flush_x`, is CTRL.DRV's. It is now beside `cp_s_dsok`
and `cp_s_dserr` in the settings core, a third entry of `cp_dsvt`, and
`cp_flush_x` copies the letter in from the resident `[cp_s_nodrv]` -
which `dsk_bootltr` stamps already, for the panel's OPEN refusal, and which
holds the same letter by construction (the boot volume) - with `CSDO`, so
kern_mega's write goes through the image's data door. `dsk_bootltr` (in
`.ovl`) loses its `mov [cp_s_dsdrv], al`: `.ovl` -3, and that line is
`disk.inc`'s - fsys's file, a routine no row names, SAID HERE.
Same toast, same text, same moment; the toast copies its line before the
module drops.

### 2. SYSTEM.CFG's and KERNEL.SYS's names leave resident (`9568dea4`) - kern_big/emu/mega `.text` -22; kern_small 0 (not assembled there)

`drv_cfgname`/`drv_sysname` stayed resident on SPEC.md 51.5.3's rule that
`dskw_read`, `dskw_stat` and `dskw_write_sys` read a name through DS. They
still do; each image now STAGES its own copy into the panel's `cp_sbuf`:

* CTRL.DRV's `cpc_save` through `cp_stage` (`CPSTAGE cpc_sysname` before
  the stat, `CPSTAGE cpc_cfgname` before the write - `cp_stage` keeps ES:BX,
  CX and DX, the write's other inputs). `cp_stage` moved from `.modu` into
  the settings core (`.modc`) so the core reaches it; `.modu` reaches it near
  in the same image, unchanged. The two `CPS` strings are the core's.
* The boot's `ovc_load` through `CFGN_STAGE` (driver.inc), a macro expanded
  as `cfx_cfgname` in `.cfx` on kern_big/emu (`call CFX_RAM:cfx_cfgname`,
  five bytes where `mov si` was three) and as the blob's own `ovc_cfgname`
  on kern_small/mega (a near call, the same three). `.cfx`'s two rules hold:
  it is after `cfx_ctxcap`, past `cfx_tab`, and names no segment; it runs in
  `drv_boot`, before `mem_unblob`.
* `ovl_sc_load` (desksc.inc, blob) took the name from its one caller,
  which had just loaded the same SI: its own `mov si` goes, which pays for
  the far call - the kern_big blob nets -1.

SPEC.md 51.5.3's *Three things stay resident* is now two, with a paragraph
saying where the names went. `cp_sbuf` is free at both moments: the boot
has no panel, and the save is the panel's teardown.

## REFUSED

* **Inlining `desk_col` and `desk_cap` into `desk_draw_zone`** (their one
  caller): -4 in the bodies, but `desk_draw_zone`'s `jc .done` then reaches
  past ~250 bytes and relaxes to a five-byte pair, +3; net -1 for a large
  diff on the damage path. Not taken.
* **`kentc_di` on `desk_dmg_zones_x` / `osapi_desk_item_x`'s pushes** (-3
  each): the first is on the damage-repaint path (pass 15 DECLINED 3), the
  second answers AL, which the ladder's `pop ax` would eat.
* **Moving `drv_notice_x` and `stik_boot` (86) or kmain's post-`mem_unblob`
  tail (~60) out of resident**: they are boot-only but run after
  `mem_unblob`, when the blob, `.cfx` and `.ovl` are gone; running them
  before it changes the heap a boot-time panel or Sticky App gets (pass 15
  blob's refusal, which holds), and the blob has no room for them anyway.
* **kmain's entry head (~18) into the blob**: the hard-disk loader enters
  `kmain` without stage 2, and the SS:SP switch cannot be done inside a
  called blob routine without the blob ending in a far jump back; for 18
  bytes against a blob with none spare.
* The rest of the fixpoint, read and dismissed above under THE INSTRUMENT.

## WANTS THE OWNER

1. **The shortcut and link OPEN path into CTRL.DRV's settings core,
   ~-280 to -330 resident on kern_big/emu/mega** (ESTIMATED from symbol
   spans: `sc_walk` 154, `lnk_open` 117, `sc_open` 13, `stik_boot` 60, less
   ~20-30 of `sc_modcall` thunks; `lnk_is`, `osapi_arg_text_x` and
   `lnk_argo` stay). Desktop shortcuts already load the core for every
   OTHER gesture (Move, Remove, Add, the Alt-drop - SPEC.md 26.8.7), so the
   mechanism is there. **What changes**: double-clicking a desktop shortcut,
   pressing Enter on one, or opening a `.LNK` would read the core off the
   SYSTEM disk first (one `int 13h`, ~400 ms on a 5150) and would refuse
   with `Needs Sys Disk A:` when it is not in its drive - today a shortcut
   to B: opens with any disk in A:; and the core's ~3 KB is claimed while
   the target launches, so a package that needs nearly all of the heap
   could open from its Disk window and not from its shortcut. `stik_boot`
   is the safe half (at boot the system disk is in by definition) but it
   calls `sc_open`, so it goes with it or not at all.
2. **A shortcut's right-click popup and Delete key into the core, ~-100**
   (`sc_rclick_x` 77, `desk_key_x` 42, less the thunks). The one command it
   offers (Remove) already loads the core; moving the popup there would put
   the module read BEFORE the popup appears and refuse the popup itself
   without the system disk, where today the popup shows and the pick
   refuses.

## WANTS AN ABI BREAK

None. No cell, header, format or SYSTEM.CFG key moved; `OSAPI_MEM_P` and
`OSAPI_FS_OP` untouched.

## CROSS-FILE

* **fsys - `hb_s_ptr` (13, `.text`)**: referenced only by the blob's
  `hb_probe_x` and HIBER.DRV's `hbm_nset`. If it is a name or message the
  same staging or copy-in as item 1/2 may apply; it is hiber.inc's
  resident half, so not read further here.
* **fsys - `fm_s_swapnd`** is stamped by `dsk_bootltr` like `cp_s_dsdrv`
  was; it is FORMAT.DRV's refusal line, said when FORMAT.DRV cannot be read,
  so it must stay resident - noted so nobody re-derives item 1 for it.
* **heap - `dockpos` fails at the base** on *advanced Dock has exactly its
  rounded module claim*: `tests/dockpos.py` still rounds `MODK_SIZE` to the
  KB (`(MODK_SIZE + 1023) // 1024 * 64` paragraphs) where `aafe18bc` made a
  module's claim the file to the granule. A stale mirror in the test, not a
  defect of this branch (red identically on `714e9d64`).
* **coordinator - `tools/guide/manuals/CTRL_DRV.md`** names the panel's
  messages; `guidelock-full` (full tier) may ask for a restamp now that
  `ctrl.inc` moved.

## ROWS RUN

`python3 tools/os88test.py soak -j 2 --marty-jobs 1 -k ...` over 17 rows,
ONCE, after the last code commit (`9568dea4`; the tree not edited while it
ran): **14 ok, 3 FAIL**, then each failure chased:

| rows | what of this branch they reach | result |
|---|---|---|
| `cfgtrip`, `fddpage`, `dispreboot` | the save through `CPSTAGE cpc_*` and a reboot reading the written file through `cfx_cfgname` | ok |
| `desksc`, `deskitem`, `stikboot` | SYSTEM.CFG with shortcuts: `ovl_sc_load` on the staged name, and the core's saves | ok |
| `cpnames`, `cpnameshdd` | `cp_stage`, moved into the core, staging every page string | ok |
| `small128`, `smallboot` | kern_small booting with item 1 and the core's `cp_stage` | ok |
| `kzboot`, `coldslide`, `bootsmoke` | the packed kernel, `.cfx` after the slide on a mono machine, a plain boot | ok |
| `mega-cfgtrip` (Bochs) | kern_mega's save (`CSDO`) and its blob `ovc_cfgname` | ok |
| `mega-stikboot` (Bochs) | kern_mega's shortcut boot | FAIL (mtools not installed), installed, re-run alone: **ok** |
| `shutdown` | the save's failure toast (item 1) on all three adapters | FAIL, re-run alone: FAIL; **ok at the base** - a race in the test, fixed (`DEFECTS FOUND`), re-run: **ok** |
| `dockpos` | none of this branch (DOCK.DRV's claim) | FAIL, **identical at the base** (CROSS-FILE, heap) |

* **Broken on purpose**: `CFGN_STAGE`'s name made `SYSTEM.CFX` - `cfgtrip`
  went red (*clk_h12 came back 0, wrote 1*, every setting lost on the
  reboot). Restored, rebuilt.
* Every batch: `make -j1` (the fast tier, incl. `hotpar`, `t_canary`,
  `blobruns`, `ovlchk`, `asmrules`); at the tip `make -j1 small`, `make -j1
  emu`, `make -j1 mega` (which fails only on the pre-existing `os88megaseg`
  gate, 3,309 against 3,231 at the base and the tip alike).
  Bochs and mtools were installed for the mega rows (`apt-get`).

## DEFECTS FOUND

1. **`tests/shutdown.py` raced the module load** (fixed in `0bb69dd4`): `active()` asked for the CTRL.DRV
   row's segment and then read `sd_active` through the CPFP SLOT - but
   `mod_need` sets the row before it fills the slot, and in between the slot
   is still `mod_gone`'s KERNEL_SEG. The byte at KERNEL_SEG:`sd_active`'s
   offset read 0 at the base and 1 at this branch's layout, so the row took
   the kernel's bytes for the dialog's origin and clicked at (16224, 2732).
   `active()` now also requires the slot to name the row's segment. Found
   by the row going red here and green at the base.
2. `dockpos` is red at the base (CROSS-FILE above).
