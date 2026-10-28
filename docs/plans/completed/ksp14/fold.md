# ksp14 agent "fold" - folding: two routines that are one

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`), the last general
(whole-kernel) agent. Concept: FOLDING - a second copy of a loop or a tail, a
register convention that makes every caller set up the same thing, a family
that wants one body - plus the leftovers the concept agents named for the
general pass: parity pads that cancel in pairs (api.md), `osapi_drv_dlg_x`
into `CTRL.DRV` (api.md), hiber.inc's stale `dsk_streq` comment (files.md),
the relaxed-jump remainder, and, mid-pass, nopin.md's relocation procs as
`mem_rr_tab` rows. Branch `ksp14-fold`, base `7bffaddc`; `kernel-size-14`
(nopin merged, `2cb4d9da`) merged in at `0c990066`. Figures are ASSEMBLED
bytes, `tools/kernsize.py --json` absolutes (kern_mega off `make mega`'s
`kernsize[mega]` line); resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`.

## THE INSTRUMENT

Four scratch scripts over `tools/os88rdiff.py`'s listing parser (`Build`), on
a `-Le` listing of each kernel - not committed, the method is what is worth
keeping:

* **repeated runs**: every resident routine's instructions normalised
  (relocations resolved to names, a branch to a routine by name, a local one
  by displacement), a suffix array over the lot, every maximal run of 9+
  bytes that occurs twice; a TAIL mode keeps only runs that end in a
  `jmp`/`ret`. After fourteen passes it finds almost nothing that is not a
  hot path's deliberate inline copy (`gfx_nextrow`'s step written into
  eleven loops, the mouse ISRs' frames).
* **call-site setup**: for every callee, the 1-3 instructions its callers
  write immediately before the `call`, grouped. A sequence three callers
  share is an ENTRY that sets it and falls into the body - the same
  instructions in the same order, so it costs nothing at run time - provided
  nothing falls into the callee from above (the script checks the previous
  routine's last instruction on both kernels) and no label sits between the
  setup and the call (the edit script refuses a site unless the two source
  lines are adjacent). **This is where the pass's bytes were.**
* **call-site teardown**: the instruction every caller writes after a call
  (none taken - see REFUSED).
* **parity pads**: every `PARITY` pad's section, address and the two hot
  routines it lies between, per kernel.

## BASE AND TIP

| | base `7bffaddc` | `kernel-size-14` (`2cb4d9da`, nopin) | tip | **this branch** |
|---|---:|---:|---:|---:|
| **kern_big resident** | 91,884 | 91,867 | **91,589** | **-278** |
| kern_big `.text` / `.bss` / `.cold` | 43,080 / 5,396 / 37,714 | 43,078 / 5,394 / 37,701 | 43,010 / 5,398 / 37,487 | -68 / +4 / -214 |
| **kern_small resident** | 58,295 | 58,221 | **58,131** | **-90** (`.text` 30,820 -> 30,790, `.cold` 21,564 -> 21,504, `.bss` 2,969 both) |
| **kern_emu resident** | 92,140 | 92,123 | **91,845** | **-278** (`.text` 43,266, `.bss` 5,398, `.cold` 37,487) |
| **kern_mega resident** | 102,240 | 102,223 | **101,967** | **-256** (`.text` 46,767, `.bss` 5,396, `.cold` 42,211, `.lowbss` 7,257; `.mega` 14,698 unchanged) |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,377 / 5,114 / 97,792 | | 6,377 / 5,114 / 97,792 | 0 |
| `CTRL.DRV` kern_big (unpacked image + bss) | 14,770 + 164 | | **14,889** + 164 | +119, inside its 15 KB claim (307 spare) |
| `CTRL.DRV` kern_small | 5,496 | | 5,496 | 0 |

No `.lowbss`, `.vgabuf`, `.ovl` or blob
byte moved on any kernel, and `.text` is EVEN on every kernel at every commit
(`.bss` follows it with `valign=1`, so an odd `.text` puts every `.bss` word on
an odd address - see TAKEN 8). `hotpar` is green on both gated kernels at
every commit; the fast tier (61 rows, `stkbalance` among them) at every
commit; `make emu` and `make mega` build at the tip.

## TAKEN

Bytes are kern_big / kern_small (kern_emu is kern_big's on every item; kern_mega
is kern_big's except where it says). None touches a hot path's instructions:
where a hot routine calls an item, the item's CALL TARGET changed and nothing
the routine executes did.

### 1. `OSAPI_DRV_DLG`'s body is `CTRL.DRV`'s - -98 / 0 (`e96e6906`)

`osapi_drv_dlg_x` (106 bytes of `.cold`) can only succeed with the Control
Panel's window up - it borrows that window, and its own `cp_live` test refuses
otherwise - and a panel up is `CTRL.DRV` loaded. So the body is the module's
`modc_e_dlg` (entry `CPE_DLG`, `CP_NENT` +1, the slot block +4 `.bss`) and the
resident cell is `jmp far [CPFP + CPE_DLG*4]`: with no image the slot is
`mod_gone`'s CF = 1, every register kept, which is what the body answered.
The slot never loads the image, so no feature needs the system disk that did
not. On the image-loaded-no-panel path `cp_live` refuses exactly as before; on
the no-image path the old body had staged the mode, proc, segment and name
before `cp_live` refused and the new one writes nothing - nothing reads those
words unarmed (`drv_dlg_done_x` tests `[drv_dlg_row]` first), which is SPEC.md
51.10's own "nothing is written until the fences pass" made stricter.
`drv_dlg_done_x`, a kernel window's completion, stays resident. The module
reaches the five kernel routines through `CPXC` (`cp_cgo`, every register and
flag preserved). SPEC.md 51.10 and 2.8's `CP_NENT` count updated.

### 2. Five shared tails and one shared answer - -48 / -31 (`df2b052d`)

* `wm_win_rect_bx` (`mov si, bx` falling into `wm_win_rect`): six callers
  wrote it at the call. -10 / -10.
* `db_erase_tile` ends `jmp wm_grow_erase.fill` - its last three lines were
  `wm_grow_erase`'s. -6 / -6.
* `ui_arm_trk`'s drag dispatch ends `jmp ui_rdown.lock` - lock, bill,
  dispatch, unlock was the right button's own three lines. -6 / -6.
  (A W_ONDRAG dispatch, Calculator's alone today: one near `jmp` more.)
* `wm_about_set` stores and joins `menu_win_set.mine` - the same
  relayout-if-owner and `popf`. -8 / -8. Once per package start.
* `cpf_cp_onup` / `cpf_cp_ondrag` are `call far [slot] / ret`, the shape
  `cpf_cp_onkey` already had: with no image the slot is `mod_gone`, CF = 1,
  registers kept, which is what `push ax / mov al, MOD_CTRL / call mod_live /
  pop ax / jc` returned - except that `mod_live` also left BX = the module's
  row. Read both ways: the bodies (`modc_e_onup`, `modc_e_ondrag`) bank BX and
  write it before reading it, and the dispatcher tolerated a clobbered BX, so
  one that is preserved is the same to it. -18 / -9. `mod.inc`'s banner,
  which called these two "the complete set" of `mod_live`'s callers, now
  names the two that are left.

### 3. Twenty fall-through entries that carry their callers' setup - -94 / -51 (`f86805c4`, `dbca237b`, `00a620ce`)

`X_y: <setup> / X:` - the instruction(s) N callers wrote before `call X`,
written once and fallen into, so every site loses the setup and the entry
costs it once. Same instructions, same order; the site's call target is the
only change. Each callee was checked on both kernels to have an unconditional
transfer before it (nothing falls in), and each site's setup and call were
adjacent source lines.

| entry | setup | sites | kern_big | kern_small |
|---|---|---:|---:|---:|
| `dsk_get_dir_dx` | `mov ax, dx` | 4 | -6 | |
| `fm_repaint_bx` | `mov si, bx` | 3 | -4 | |
| `fm_relayout_si` | `mov bx, si` | 3 | -4 | |
| `fm_settitle0` | `xor si, si` | 3 | -4 | |
| `dskw_name83n` | `mov di, dskw_n83` | 3 | -6 | |
| `dsk_ncopy8` | `mov cx, 8` | 3 | -6 | |
| `assoc_stage8` | `mov cx, 8` | 3 | -6 | (no assoc) |
| `wm_su_slot_di` | `mov bx, di` | 3 | -4 | |
| `wm_su_rect_bx` | `mov di, bx` | 3 | -4 | |
| `assoc_app_di` | `mov si, di` | 3 | -4 | (no assoc) |
| `dsk_dest_b0` | `xor bx, bx` | 2 (3 before nopin's merge took one) | -2 | |
| `dsk_copy_seg_es` | `mov ax, es` | 3 | -4 | |
| `fm_pen8_di` | `mov ax, di / add ax, [fm_lscr]` | 2 | -6 | |
| `assoc_try_ix` | `shl bx, 1 / mov ax, [assoc_clus+bx]` | 2 | -6 | |
| `fmv_winof_vp`, `fmv_hload_vp`, `fm_fmt_ok_vp` | `mov di, [fm_vp]` | 2 each | -12 | |
| `ld_slot_rec` | `mov di, [ld_rec]` | 2 | -4 | |
| `dsk_vol_row_dl` | `mov bl, dl` | 4 | -6 | |
| `dsk_fatw_slota` / `_slotd` | `mov bl, al` / `mov bl, [disk_drive]` (`_slotd` is `jmp short` into the body) | 4 + 3 | -10 | |

kern_small takes the same entries where the sites exist (-29, -16 and -8 in
the three commits, less the `dsk_dest_b0` site the merge took). One cost to name: the setup now runs right after the
`call` flushed the prefetch queue instead of from a queue the instructions
before it had filled - on an 8088 a few clocks per call. None is on the hot
list; `wm_dmg_wins` (a damage repaint, per window) and the disk mount's
harvest loop are the busiest sites.

### 4. Two pad pairs cancel - -4 / -2 (`9c14bc6f`)

Re-derived on the layout as it stood (`pads` above): `memory.inc`'s pad after
`mem_bytes_kb_x` and `mod.inc`'s in `mod_disarm` (both in `.cold` between
`gfx_blit1_x` and `dsk_xfer`, on both kernels, with only cold code between),
and kern_big's `instance.inc` pad before `inst_minimize` with `menu.inc`'s
before `menu_popup` (between `wm_paint_all` and `ui_task`). REFUSED below are
the pairs api.md listed that are not clean.

### 5. The rename shares the delete's refusal - -3 / -3 (`9c14bc6f`)

`dskw_nbody`'s FERR_PROT `jz .patch / jmp dskw_wbody.prot` is `jnz
dskw_dbody_n83.protj`, the delete's own `jmp` 85 bytes up. A refusal path,
one jump more. The only one of the 55 long branches with an existing jump to
its target in short reach that was not a relay already (RELAXED below).

### 6. `DSK_WR1D dskw_clba` is one routine - -3 / -3 (`7d505561`)

The data walk's cursor was loaded at three macro sites (the mkdir's `.`/`..`
sector, its zeroed tail, `dskw_wone`). `dsk_wr1_clba` loads it and jumps to
the one-sector writer: a jump per sector written, against ~400 ms of `int
13h`. (As a fall-through entry it would sit just ahead of `dsk_xfer` and move
it by an odd three.)

### 7. hiber.inc's comment (`00a620ce`)

It said the name lookup's compare "is `dsk_streq`"; that routine is gone. The
lookup parses the name with `dskw_name83`, which reads DS:SI - the comment's
point, KERNEL_SEG or nothing, still holds. 0 bytes.

### 8. Four relocation procs are `mem_rr_tab` rows - -28 / 0 (`3027ed70`; nopin.md's CROSS-FILE)

`menu_reloc`, `ico_reloc`, `dsk_rah_reloc` and `asc_reloc` were `cmp bx, [w] /
jne / mov [w], dx / ret`, 11 bytes; each word is now a `MEM_RR_ONE` row (4)
and the claim declares `mem_rr_none` - `mem_rr_walk`'s own `ret` - as its
proc, so it stays movable (`MC_RLOC` non-zero) and `mem_reloc_call`'s first
half, `mem_region_reloc`, does the patch for every move. **The argument, per
word** - a row rewrites its word on ANY block's move, so it is exact only if
the word never holds a base that is not its own live claim:

* `[menu_sseg]` (`MEM_P_MSAVE`): zeroed at boot (`menu.inc`, in `kmain_o`,
  before `mem_unblob`'s first compaction), zeroed before it is claimed
  (`menu_drop`, `stm_open`), taken and zeroed in one `xchg` before every free
  (`menu_drop`'s `.sel`, `stm_shut` twice), and zeroed by `mem_shed_one`
  through `mem_pg_own` before a shed frees it.
* `[ico_seg]` (`MEM_P_ICO`): the store is never freed but by a shed, whose
  `mem_pg_forget` arm is `ico_demote` - `mov word [ico_seg], 0`. The one other
  release is `mem_hicaches_f`, kern_mega's alone.
* `[dsk_rah_seg]` (`MEM_P_DIRW`): its one free is `dsk_rah_redo`
  (`dsk_rah_dropf` and `dsk_rah_redof` call it), which zeroes the word right
  after `mem_free_x` succeeds - nothing can compact between the two - and
  leaves it when the free refuses (the claim still lives); the shed zeroes it
  through `mem_pg_own`.
* `[asc_seg]` (`MEM_K_ASC`, not purgeable): a one-call file buffer; `asc_use`'s
  single exit zeroes the word BEFORE `mem_free_x`, and a refused claim never
  set it. `.text` with an initialiser of 0.

**kern_mega keeps all four procs and gets no rows**: `mem_hicaches_f` swaps
`[ico_seg]` for a pool SELECTOR and `[dsk_rah_hi]` makes `[dsk_rah_seg]` one,
and a walk that compares every word with every moved base must not see a
selector that happens to equal a segment. On the other kernels the rows sit in
`.text` between `wm_paint_all` and `ui_task` (+16) with `menu_reloc` (-11) and
`instance.inc`'s K9 pad (-1, now `%ifdef KERN_MEGA`): +4, even. The other
three procs are after `ui_task` (-33) and `dsk_ico`'s `align 2` evens out
everything before `ico_reloc`, so one parity byte in `ico_reloc`'s place keeps
`.text` - and so `.bss` - even. Every compaction move walks four more rows
(~20 clocks each) against a `rep movsw` of the block. `tests/unit/t_pins.py`
reads the movable calls, not the proc's name: green, 137 declared.

`clip_reloc` and `sc_reloc` (5 bytes each, no compare) were left: a row is 4,
so each is -1, and an odd byte with a `.text` parity to keep.

## REFUSED

* **Pad pairs api.md listed that are not clean** (measured on this layout):
  kern_big `sched.inc`'s pad + `wm.inc`'s after `wm_nat_take_wh` - `sched.inc`'s
  sits ahead of `sch_s_ui` and **`sch_stkbase`**, the per-slot word table
  `sch_switch` reads, which would go to an odd address; kern_big's five
  between `gfx_blit4` and `font_run` (`vga12.inc` x4, `font.inc`) - drawing
  primitives and `.text` data words (`gfx_sub_*`, read per row in `sw_xfer`)
  between them would all move by one; kern_small's `vga12.inc` pad, whose
  whole job is `gfx_sub_st`/`_rs`/`_ps`'s word alignment; `vga12.inc`'s
  `.vxplanar` pad in `.cold`, which `gfx_blit1_x`'s planar per-run body sits
  behind; `wm_lf_pad` and `menu_dropd`, which are labelled data. A pair is
  only clean if nothing hot AND no word-aligned data lies between - `hotpar`
  checks entries, not the words the loops read.
* **`dsk_here_ok` / `drv_vol_back` / fdlg's commit sharing a "drive and folder
  still current?" helper**: kern_big -10, kern_small +4 (its fdlg is a module
  and `drv_vol_back` is kern_big's), and `inst_vol_enter` is `.text` where
  the others are `.cold`.
* **Teardown into the callee** (`menu_widest`'s `add ax, 16`, `dws_gate`'s
  `cmp byte [dsk_mntok], 1`, `fm_stage_onam`'s `cmp word [fm_ftype], 3`,
  `dsk_fat_window`'s compare, `fm_count`, `dskw_pmask`; -2 to -5 each): each
  either has a caller outside the resident listing (`fmf_fm_stage_onam` for
  DISKOPS.DRV, `CTRL.DRV`'s and the blob's `drv_row_x`) whose flags would
  change, a callee whose exit is a shared ladder, or sits on
  `dsk_next_clus_x`'s per-cluster path.
* **`mov si, MEM_FIRST` / `mov di, inst_tab - I_RECSZ` / `call
  dsk_dirw_start_x` into an entry** (the three largest setup groups, ~-55
  between them): every site is a loop whose label sits between the setup and
  the call.
* **`call X / ret` as `jmp X`**: of the 17 pairs, the two without a label on
  the `ret` (`drv_call`, `drv_fs_call`) push a segment that `drv_stamped`
  takes off the stack under its own return address.
* **`ui_dispatch`'s `call ui_bill / jmp gfx_unlock` into `ui_rdown.bill`**:
  `.app_done` is a `jcxz` target, which cannot be relaxed.
* **`fcp_run2`/`fcp_step`, `dsk_peek_x`/`dskw_read_at_x` and the pop-ladder
  tails**: their shared tails end in local labels or ladders whose order
  differs; 3-5 bytes each where a common tail exists at all.
* **`dskw_read_x` and `dskw_wrp` as one body**: the read keeps DX (an output)
  and the write banks it, so they leave by different ladders.
* `cur_put_1`/`cur_move_1`'s shared tail, `kbm_move`/`mou_apply`'s, the font
  and cursor row steps: the mouse ISR and the per-glyph loops.

## RELAXED JUMPS

55 long conditional branches on kern_big; 27 are nasm's synthesised
`Jncc +3 / jmp near` and the rest are hand-written pairs (most of them
relays other branches jump to). Of the 27, 17 are in hot routines (`gfx_blit4`,
`font_run_x`, `ui_task`) or the `sw_*` 1bpp arms of the primitives, three are
pass 12's refusals (`fm_editkey`, `fm_draw_status`, `fcp_xfer`), and the
nearest miss, `ld_start`'s `jne .rehome`, is 8 bytes over with `.abort` placed
between on purpose so step 8's `jc .abort` stays short. Only TAKEN 5 had an
existing jump to its target in reach.

## WANTS THE OWNER

None. Every item is byte-identical behaviour on every path read; TAKEN 1's
no-image path writes four fewer words that nothing reads.

## WANTS AN ABI BREAK

None. `CPE_DLG`/`CP_NENT` are the kernel <-> CTRL.DRV interface `os88mod.py`
checks on both halves; `OSAPI_DRV_DLG`'s cell kept its address, length and
contract.

## CROSS-FILE

None outside the tree's own files; for the next pass:

* `clip_reloc` / `sc_reloc` as rows (-1 each, see TAKEN 8).
* The `PARITY` pads' comments name the hot ROUTINES they protect; several
  also protect word-aligned DATA (`sch_stkbase`, `gfx_sub_*`) that `hotpar`
  cannot see. A data-alignment check beside `hotpar` - the words a hot loop
  reads, at the parity they were measured at - would let the next pad-pair
  pass trust the gate.

## ROWS RUN

**Broken on purpose first** (on this tree, then restored and rebuilt):
`modc_e_dlg` made to refuse at once - **`rdup` went red** on G and H (*"Load
put a file dialog up"*, *"Preserve As put a file dialog up"*); and
`MEM_RR_ONE ico_seg` deleted - **`icoshed`, `icostore`, `regwork`, `heapcheck`,
`regmove` and `dirwshed` all stayed green: NO ROW MOVES THE ICON STORE**, so
TAKEN 8's `[ico_seg]` row rests on the reading above (as `ico_reloc` itself
did). A coverage gap worth a row.

Then ONE pass at the tip (`3027ed70`, the tree not being edited),
`python3 tools/os88test.py soak --marty-jobs 1 -k ...`, MartyPC on the GLaBIOS
twins (nothing needed the IBM ROM; `mtools` installed): **46 / 46 ok** in
929 s.

| rows | what of this pass they reach |
|---|---|
| `rdup`, `rdpreserve`, `drvup`, `install` | TAKEN 1: the RAM disk page's Load / Preserve As through `CPE_DLG`, to the commit; OSAPI_DRV_USE beside it |
| `btncp`, `cpup`, `shutdown`, `drvscroll` | TAKEN 2's release thunk and the panel's buttons; the Shut Down card |
| `dispdrag`, `dragui`, `dispclose`, `wdcombo`, `timerrace` | `wm_win_rect_bx`'s six sites, `ui_arm_trk`'s drag dispatch, the About/menu setters |
| `dockpos`, `dockmodule` | `db_erase_tile` into `wm_grow_erase.fill` |
| `fmcommit`, `fmbtn`, `fmthumb`, `fcpcopy`, `fcpapi`, `imgmount`, `desksc`, `deskwhole` | the Disk window's entries (TAKEN 3), the mkdir's `dsk_wr1_clba` (TAKEN 6), the rename/delete refusal (TAKEN 5) |
| `assocsweep`, `assocstale`, `assocfind`, `assocopen`, `ascabsorb` | the assoc entries and `[asc_seg]`'s row |
| `regwork`, `regmove`, `regpin`, `regapp`, `heapcheck`, `heapdrv`, `drvmove`, `sndmove`, `pins`, `pinsrt` | TAKEN 8: compaction through `mem_rr_tab`, and nopin's gates |
| `icoshed`, `icostore`, `dirwshed`, `fatwpin`, `hdauto` | the icon store, the read-ahead store and the FAT window entries |
| `stikmenu`, `wdmenusu` | `[menu_sseg]`'s row: the Sticky Apps panel's and Word's menu save-unders |
| `small128` | kern_small on its 128KB floor machine |

`fatwpin` took 82 s against 30 declared and passed. Every commit: `make -j2`
(the fast tier, 61/61, `hotpar` and `stkbalance` in it), `make -j2 small`;
`make emu` and `make mega` at the tip.

## DEFECTS FOUND

None in shipped behaviour. The coverage gap above (no row compacts the icon
store), and the `PARITY`-pad / data-alignment gap under CROSS-FILE.
