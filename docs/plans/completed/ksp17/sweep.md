# ksp17 agent "sweep" - a second look at the file side and the window side

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`), second wave: a
fresh-eyes pass over what the finished first-wave agents `files` and `wm`
freed - `files.inc`, `fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc`'s
resident half, `assoc.inc` (not fresh's hunks), `icons.inc`, `wm.inc`,
`instance.inc`, `clip.inc`, `events.inc`, `dock.inc`, `apps.inc` - plus any
kernel file in no row of BRIEF section 1. Their notes (`files.md`, `wm.md`)
were read first and nothing on their REFUSED lists is re-proposed except
`wm_su_flay`'s byte, which `wm.md` left free for exactly this. Branch
`ksp17-sweep`, base `b1794f60` (the wm merge). Figures are ASSEMBLED;
resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: plus `.mega`, which did not move).

**TAKEN outside the rows**: none edited. `drvvol.inc`, `dskwin.inc`,
`ascw.inc`, `shutdown.inc` and the module files (`dockmod`, `extmod`,
`winmod`, `deskmod`, `stikmenu`, `diskops`) are in no row; `drvvol.inc` and
`dskwin.inc` were read and have nothing (`dskwin.inc` is data; `drvvol.inc`'s
banks are its callers' contract), the rest are module images.

**How it was read.** Six scratch scripts over the thirteen files, each a
shape pass 16 named, and then every hit read by hand: a hand-banked prologue
of four to eight registers in ladder order whose epilogue is (or can be) a
ladder rung; a `push R / call X / pop R` window whose callee's header and
body keep R; a `.bss` or `.text` variable no instruction ever READS; a `mov
word [m], 0` or `0FFFFh` whose flags die before they are read; `mov al, [x]`
/ `mov bl, al` followed by a zeroing of the high byte where the value is a
bounded index (`cbw`); runs of word stores into a contiguous record. Then the
single-caller routines of the census, read at their call.

## BASE AND TIP

| kernel | base `b1794f60` | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,659 | **91,584** | **-75** | `.text` 40,794 -> 40,769 (-25), `.bss` 5,419 -> 5,417 (-2), `.cold` 37,720 -> 37,672 (-48) |
| kern_small | 57,528 | **57,467** | **-61** | `.text` 30,377 -> 30,358 (-19), `.bss` -2, `.cold` 21,336 -> 21,296 (-40) |
| kern_emu | 91,913 | **91,838** | **-75** | as kern_big (`.text` 41,048 -> 41,023) |
| kern_mega (with `.mega` 21,712) | 124,963 | **124,888** | **-75** | `.text` 46,036 -> 46,011, `.bss` 5,457 -> 5,455, `.cold` 45,852 -> 45,804 |
| kern_mega segment overhead (`os88megaseg`) | 3,248 | 3,248 | 0 | gate green |

The pass therefore stands at **-295 on kern_big** (files -63, wm -157, this
-75) against the base 91,879. No `.lowbss`, `.vgabuf`, `.vga`, `.boot2`,
`.ovl`, `.cfx` or module byte moved; `BOOT2_SECS` and `KERN_SIZE` (97,280 /
59,904) unchanged. Base and tip are both my own assembly of `kernel.asm` at
each commit against the same generated includes, `tools/kernsize.py --json`
agreeing with the tip.

## TAKEN

Bytes are kern_big (= kern_emu) / kern_small. Every routine named here was
read whole, with every exit, and every caller of a changed contract.

### 1. Hand-banked frames onto the ladder (-13 / -13)

* **`desk_draw_zone`, both arms** (-4 / -4, `.cold`): eight pushes, AX..BP
  and ES, ending in `jmp kretc_es` - exactly `call kentc_bp / push es`. Once
  a zone, beside a body that draws an icon and a caption (thousands of
  cycles); +~95 clocks.
* **`db_click` and `db_rclick`** (-5 / -5, `.text`): five pushes each and a
  shared `pop di / jmp kret_dx` tail; both now open on `kent_di` and the
  tail is `jmp kret_di`. SI joins the bank, so more is preserved, not less.
  Once a click on the strip.
* **`wm_dc_take`** (-4 / -4, `.text`): six pushes (AX, BX, CX, SI, DI, ES)
  and `pop es / pop di / pop si / jmp kret_cx`; now `kent_di / push es` and
  `pop es / jmp kret_di`. Once a drag, at the release.

### 2. Banks round a callee that keeps the register (-6 / -6)

* **`wm_dmg_wins`' BX bank round `wm_ovl`** (-2): `wm_ovl` documents "every
  register preserved" and its body touches AX alone. On the damage pass:
  FASTER, two stack operations a marked window.
* **`wm_ovl`'s two exits** (-2): `clc / pop ax / ret` and `stc / pop ax /
  ret` are `CLC_OR_STC .no / pop ax / ret`. Both callers (`wm_win_over`'s
  two, `wm_dmg_wins`) read CF alone (checked: no `jz`/`jnz` after either).
  The success path is +2 clocks for the `test al` over `clc` - with the bank
  above, the damage pass is still net faster.
* **`wm_zoom`'s DI bank round `wm_bank4`** (-2): nothing after it reads DI -
  `wm_resize_nb` takes BX, CX and DX and so do its callees (`wm_min_floor`,
  `wm_snap_win`, `wm_nat_bank`, `wm_rz_paint`, all read) - and the routine
  leaves through `kret_di`.

### 3. Dead data (-10 and -2 `.bss` / same)

* **`fm_dgon`** was written twice in `fm_drag` and READ NOWHERE (the census
  of every `.bss` label in the thirteen files for a read: this was the one
  real hit). Both stores went (-10) and the byte went (-1 `.bss`). Its
  neighbour `fm_dgdrv` then moved to the end of `files.inc`'s `.bss`, in the
  place of the one-byte parity pad `fm_bsspad` (-1 more): `fm_name` + 
  `fm_hdrbuf` are 65 bytes, so the byte after them keeps every `.bss` word
  after this file at the parity it had, which was the pad's whole job
  (`files.md` refused deleting the pad; it is not deleted, a real variable
  does its work).

### 4. Written at the one call, or once (-18 / -10)

* **`inst_es_slot`** (-5, kern_big - `OS88_COMPACT`): `inst_parksafe_set`
  and `inst_restart_set` each spelt `mov bx, es / call inst_of_seg / jc /
  call inst_fhome_idx` and then a `clc` "because inst_fhome_idx clobbers the
  flags". It does - to CF = 0: `inst_idx`'s `shr ax, cl` shifts out bit 4 of
  `DI - inst_tab`, and a record is 32 bytes. The head is one routine that
  falls into `inst_fhome_idx`, both `clc`s went, and `inst_restart_set`'s
  CF = 0 is its own `shl bx, 1` (BX < INST_MAX). `inst_fhome_idx`'s header
  now says CF = 0.
* **`fdlg_seed` into `fdlg_open_x`** (-3 / module only): its one call; the
  two `ret`s become a fall-through and one `jmp short .seeded` over the
  MEDIA arm. No branch of `fdlg_open_x` crosses it. SPEC.md 38.10's four
  mentions now say `fdlg_open.seed`. (`kernel.asm:4644`'s comment still
  names it - ui's file, CROSS-FILE.)
* **`fm_kinit_x`** (-10 / -10, `.cold`): a fresh window's fields were seven
  word stores off one zero and a `mov [di], ax` of -1; they are STOSW - FS_SEL
  -1, then FS_SCRL/FS_CLKT/FS_N, `scasw` twice over FS_CWD/FS_DRV/FS_MOK
  (reads and flags only), then FS_VIEW..FS_DIRTY as four words, which writes
  exactly the bytes the seven stores did. ES = DS: KD_INIT's one caller is
  `app_launch`, which sets it at `.tpl` for `wm_create`'s template and
  documents that nothing between changes it (`inst_set_name_x` reads ES:SI
  and writes no ES). DF = 0 is SPEC.md 1. An `%if` pins all eleven offsets.

### 5. Encodings (-26 / -20)

* **`wm_su_flay`'s `inc bx` for `not bl`** (-1, kern_big): `wm.md`'s byte,
  left out there for parity and free "to whoever next moves a kern_big-only
  byte there" - `inst_es_slot`'s -5 was that byte, and the pair keeps
  `ui_task` even. BX is reloaded before it is read again.
* **`app_about_center_x`** (-3 / -3): `((W - 2 - w) >> 1) + 1` is
  `(W - w) >> 1` for any `W - w >= 2`, and every About line is far narrower
  than the 300-wide box; `dec / dec / inc` went. And `app_about_paint_x`'s
  `xchg si, ax` for `mov si, ax` (-1): AX is the helper's to write next.
* **`cbw` for a high-byte zero where AL is a bounded index** (-1 each):
  `wm_dmg_wins` twice (`[wm_zn]` <= MAX_WIN; also a clock faster on the
  damage pass), `wm_zwalk`, `wm_ptr2idx` - placed BELOW its clamp, so AL <
  MAX_WIN and AH = 0 even for the bad pointer the clamp exists for (above it a
  quotient of 128..255 would have left AH = 0FFh) - `fm_brect`'s `.hdr` (an
  id 1..FM_BDRIVE) and `fm_rclick_x` (bounded by MC_NITEM).
* **`cbw / xchg bx, ax`** for `mov bl, al / xor bh, bh` where AL is an app
  slot (< ASSOC_NAPP = 16) and AX is dead after (-2 each, kern_big):
  `assoc_locate` (AX is next loaded by `assoc_try_ix`; the stem loop writes
  AH only), `asc_seed`'s row walk (AX loaded two lines on) and
  `asc_take_x` (`assoc_hint_here` clobbers AX). `desk_cbit`: `cbw / mov bx,
  ax` (-1 / -1) - a cell is <= DSL_SLOT and AH is its documented clobber;
  every caller passes a masked cell (read).
* **`fm_seed`** (-2 / -2): `mov word [fm_seed_nam], 0` is `mov
  [fm_seed_nam], si`: 0 when there is no name, and overwritten below when
  there is.
* **`and`/`or` for `mov word [m], 0 / 0FFFFh` where the flags die** (-1
  each, `.cold`): `fmv_store`'s FS_SEL and FS_SCRL (`fmv_winof` writes CF
  next), kern_small's `fmv_demote`'s FS_VSEG and `[fm_vseg]` (a `cmp`; a
  `popf`),
  `fmv_load`'s `[disk_nfiles]`, `fm_onup_x`'s W_TIMER (`fm_layout` next),
  `fm_c_format`'s `[fm_swapwho]` (`fm_edit_arm` reads none), `fm_scrollpaint`'s `[fm_rlo]`
  (`neg`); and on kern_small's arms `[fm_lsel]` and fm_rclick's FS_SEL.

### Per routine, kern_big (`tools/os88rdiff.py kernel b1794f60 .`)

`desk_draw_zone` 71 -> 67, `db_click`/`db_rclick` -5 together, `wm_dc_take`
139 -> 135, `wm_ovl` 55 -> 53, `wm_dmg_wins` 314 -> 310, `wm_zoom` 169 ->
167, `wm_ptr2idx` -1, `wm_zwalk` -1, `wm_su_flay` -1, `fm_drag` -10,
`inst_parksafe_set`/`inst_restart_set` -12 with `inst_es_slot` +7,
`fdlg_open_x` + `fdlg_seed` -3, `fm_kinit_x` 144 -> 134, `fm_seed` -2,
`app_about_center_x` 22 -> 19, `app_about_paint_x` -1, `assoc_locate` -2,
`asc_seed` -2, `asc_take_x` -2, `fmv_store` -2, `fmv_load`, `fm_brect`,
`fm_onup_x`, `fm_c_format`, `fm_rclick_x`, `fm_scrollpaint` -1 each, `desk_cbit` -1, and the one-byte stores above.

## HOT PATHS AND PARITY

Nothing on the hot list changed its own bytes. On the damage-repaint path
(pass 15's DECLINED 3) every change is FASTER: `wm_dmg_wins` loses a push
and a pop per marked window and two `xor ah, ah` become `cbw`, `wm_ptr2idx`
(per window) takes `cbw` for `xor`; `wm_ovl`'s success exit costs 2 clocks
more and is outweighed by the bank it lets go. No `kent_*` went on the
damage path, the save-under cache or anything per row, per glyph or per
pixel: the four ladder frames are per zone, per click, per drag.

`hotpar` green on both gated kernels at every commit, **no new pad**. Two
builds were red on the way and were answered by choosing items, not pads:
`inst_es_slot`'s -5 (kern_big only, between `wm_paint_all` and `ui_task`)
was paired with `wm_su_flay`'s -1; and `app_about_center_x`'s -3 in `.cold`
put `dsk_xfer`, `dsk_runcap` and `dsk_next_clus_x` (which are `.cold`, after
`apps.inc`) odd, so the About box's `xchg` (-1) went with it. The `assoc.inc`
items are an even -6 for the same reason (`assoc.inc`'s `.cold` is ahead of
`disk.inc`'s).

`tools/stkbalance.py` over the eight touched files, base against tip: 39
unbalanced paths at the base, 37 at the tip - the two gone are `db_click`'s
and `wm_dc_take`'s hand-banked tails, and `desk_draw_zone`'s ladder tail is
now at depth +1 (the `push es`) where it was +8. None new.

## REFUSED

* **`wm_grow_erase`, `db_erase_tile` and `wm_an_setw` on `kent_di`** (-3,
  built and reverted): the first two share a frame through
  `wm_grow_erase.fill`, so they move together, and on kern_small - which has
  no `WM_ANIM` - that leaves an odd -1 between `wm_paint_all` and `ui_task`
  with no kern_small-only byte in that stretch to pair it. Doing it on
  kern_big alone is `%ifdef`s in two files for three bytes at ~95 clocks
  each.
* **`wm_grow_paint` on `kent_di`** (-3): `THV_GRIP` jumps far into
  WIN95.DRV's grip body, which returns through wm_grow_paint's five-word
  frame; the frame is a kernel <-> module contract (`winmod.inc`).
* **`gfx_restore`'s DI bank in `wm_su_try`** (-2): the contract says DI is
  kept, and the VGA and software bodies keep it, but the VESA driver's
  `vxp_restore` is reached through the hook and was not proven; it is also
  the save-under cache (S3's trade).
* **`fm_mzero` by STOSW** (-3): its callers' ES is not established.
* **`fmv_get_dir`'s multiply as shifts** (0): DSK_DE_STRIDE is 24, so the
  shift sequence is longer than `mul` plus the DX bank.
* **`.fs_st_fmt` / `.fs_st_clone` in `fm_draw_status` through one register**
  (-10, ESTIMATED): the two arms differ only in the routine they call twice,
  but no register is proven to survive `clo_call` (a module verb) and
  `dskw_fmt_line`, and `fm_draw_status` does not bank BP.
* **`sc_modcall`'s AX bank across `mod_live`** (-1): `mod_live` and
  `mod_need` keep AX, but the module ids are loaded into AL, so the bank
  stays; the one byte left is a `pop`/`push` merge that costs a byte at
  `.full`.
* **`mov word [fm_elen], 0` in `fm_edit_end`** (-1): `fm_edit_end` is
  reached by tail jumps from routines whose callers' flags were not all
  read.
* **A `ct_` trampoline for `cw_toast_show`** (0 on this row's sites alone):
  five `.cold` far calls, three of them here; a trampoline is 6 bytes and a
  site saves 2, so it pays -4 only with the other two (ui's and disk's files)
  converted too - CROSS-FILE.
* **`asc_n` / `asc_ne` as bytes** (~-6): their word readers would each grow.

## WANTS THE OWNER

None. Every item is byte-identical behaviour: the register and flag values
that differ are ones no caller reads (each read), and `wm_ptr2idx`'s answer
for a bad pointer is unchanged because its `cbw` is below the clamp.

## WANTS AN ABI BREAK

1. **The ten handler setters as one cell** - `OSAPI_WM_ONSIZE` (0x01BC),
   `_ONMOUSEUP` (0x0196), `_ONRESIZE` (0x02EB), `_ONDRAG` (0x0334),
   `_ONTIMER` (0x0342), `_ONWAKE` (0x0357), `_ONCLOSE` (0x0362), `_ONRCLICK`
   (0x0380), `_ONCLICK` (0x0450), `_ONWHEEL` (0x0499). Every one is already
   an INLINE cell of five or six bytes (the store, a `clc` on two, kern_mega's
   `MEGA_ICWB` form), so ~55 bytes of `.text` between them. One cell taking
   the field (DI = the W_ON* offset, which the SDK already publishes, or an
   index into a ten-byte table) is ~7-12 bytes, so **~-40 to -48 of `.text`,
   ESTIMATED**, if the break renumbers rather than leaving stubs. Callers:
   **218 references in `apps/` and `drivers/`** (ONWAKE 39, ONCLOSE 33,
   ONTIMER 30, ONMOUSEUP 27, ONDRAG 24, ONRESIZE 24, ONWHEEL 19, ONCLICK 10,
   ONRCLICK 9, ONSIZE 3), and `apps/cc/os88.h`'s wrappers. It sits beside
   `wm.md`'s seven window-flag setters and would want doing with them.

## CROSS-FILE

* **`fmv_ld_ent` written at its one call** (`files.inc`, 7 bytes, called
  only from `loader.inc:679`, disk's): `cmp ax, [di+FS_N] / jae .badix /
  call fmv_dient` at the call, ~-5 ESTIMATED.
* **A `ct_cw_toast_show` trampoline** (`kernel.asm` or any `.cold`): -4 with
  all five `.cold` far-call sites converted (three in `files.inc`/`desk.inc`,
  two in other rows' files).
* **`kernel.asm:4644`**'s comment names `fdlg_seed`, which is now
  `fdlg_open.seed` (ui's file).
* `wm.md`'s `wm_dg_precover` into `ui_drag` stands (ui's file); not re-read.

## ROWS RUN

Once, at the code tip `3eb476d3`, after the last code commit and with the
tree not edited while they ran: `python3 tools/os88test.py soak -j 1
--marty-jobs 1 -k ...` over **40 rows: 40 passed, 0 failed, 0 skipped**
(1,218.8 s), MartyPC on the GLaBIOS twins (nothing needed the IBM ROM).

| rows | what of this pass they reach |
|---|---|
| `deskwhole`, `zonedmg`, `deskclip`, `deskzoom`, `desksc`, `deskdrag`, `deskdock` | `desk_draw_zone` on `kentc_bp`, `desk_cbit`, the zone repaint |
| `dockmark`, `dockpos`, `deskdock` | `db_click`/`db_rclick`'s frame, the dock paint |
| `dragui`, `dispdrag`, `msel` | `wm_dc_take` on the ladder, `fm_drag` without `fm_dgon`, `fm_dgdrv`'s new home |
| `dmgcull`, `paintcull`, `win1bpp`, `su1bpp`, `tmrepair`, `zoomsave`, `alertanim` | `wm_dmg_wins`' bank and `cbw`s, `wm_ovl`'s exit, `wm_ptr2idx`, `wm_zwalk`, `wm_su_flay` |
| `dispzoom`, `zoomsave` | `wm_zoom`'s DI |
| `dispapp`, `dispclose`, `dispclose-small` | the About box's centring, `fm_kinit_x` (every Disk window opened), `app_launch` |
| `fdlgchoose`, `fdlgsmall`, `fdlgdrop` | `fdlg_open.seed` (both arms of the MEDIA default), FDLG.DRV |
| `assocopen`, `assocfind`, `assocvol`, `assocsweep`, `ascabsorb` | `assoc_locate`, `asc_seed`, `asc_take_x` |
| `fmbtn`, `fmarrows`, `fmthumb`, `fmcommit`, `fcpcopy`, `diskclone` | `fm_brect`, `fm_scrollpaint`'s `[fm_rlo]`, `fm_onup_x`, `fm_seed`, `fmv_store`/`fmv_load`, `fm_c_format`'s swap prompt |
| `regwork`, `regmove`, `heapcheck` | `inst_restart_set` / `inst_parksafe_set` through `inst_es_slot` |

Reached by no row run here, and equivalent by reading: `fm_rclick_x`'s
`cbw` (the context menu's item, bounded by MC_NITEM), `wm_ovl` through
`wm_win_over` (the precover pass), kern_small's `fmv_demote` and its two
`[fm_lsel]`/FS_SEL stores. No row was broken on purpose: none of them is new
to this pass, and `t_hotpar` (fast tier) went red twice on the way, which is
the gate this work could break silently.

## DEFECTS FOUND

* **`fm_dgon`** - written on every drag and read by nothing: the outline's
  state is carried by the drag loop itself. Not a behaviour defect, dead
  state (removed, item 3).
* **`docs/INDEX.md` was stale at the merge base `b1794f60`**, which fails
  `make` in a fresh worktree; regenerated in its own commit.
