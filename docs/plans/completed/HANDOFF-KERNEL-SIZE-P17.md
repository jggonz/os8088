# Kernel size pass 16: the record, and what is left for a seventeenth

**PASS 16 HAS LANDED** on `kernel-size-16`, cut from `elendilon` at
`23791571`. The file is named `-P17` for the reason the records before it are
named one ahead: each pass's record is named for the pass it hands to.

The brief is `docs/plans/completed/ksp16/BRIEF.md`; each agent's note is
beside it (`heap.md`, `ops.md`, `gui.md`, `fsys.md`, `place.md`, and the
second wave's `sweep.md`) and is the authority on its own items. This file is
the summary, what wants the owner and what is left.

## 0. THE BRIEF AND THE OUTCOME

The owner's words: *"The goal of this size pass is to cover the kernel work
since the last size pass, and then, with that and/or general work, recover
(with some spare for bugfixes) the gained kern-big size, ending neutral or
better than main. Use agents as needed, we have spare capacity."* Five agents
at once, and a sixth (`sweep`) started when the first three had landed and
the pass stood at -224.

**The goal is met.** The base was **188 bytes over `main`** on kern_big
resident and had crossed one `KERN_SIZE` rung; the tip is **431 bytes under
`main`** and back on main's rung.

Kernel resident (`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`,
plus `.mega` on kern_mega), assembled:

| kernel | `main` (`7b4b441d`) | base (`714e9d64`) | tip | delta on the base |
|---|---:|---:|---:|---:|
| **kern_big** | 91,948 | 92,136 | **91,517** | **-619** |
| kern_small | - | 57,984 | 57,668 | -316 |
| kern_emu | - | 92,392 | 91,771 | -621 |
| kern_mega | - | 125,598 | 124,980 | -618 |

`KERN_SIZE` kern_big 97,792 -> **97,280** (main's figure; the gui merge took
it back); kern_small 59,904 unchanged. kern_mega's conventional footprint is
62,976 of its 68,096 budget.

## 1. WHO TOOK WHAT (kern_big resident)

| agent | scope | kern_big | the shape of it |
|---|---|---:|---|
| **gui** | window/menu/drawing/scheduler | -186 | ~20 single-caller routines written at their one call (most of the bytes were pushes and pops the callee kept for registers its one caller never reads); three compares made twice; `menu_drop`'s rect loaded once; `sch_isr`'s ROM chain as one call for both arms (ops's suggestion: -10, ~45 clocks faster a tick) |
| **heap** | `memory.inc`, `mod.inc`, the claim lines | -104 | the DMA cells' far entries ARE the bodies; `mem_avail_lvl_p` on `mem_cp_gap`/`mem_cp_tail`; dead stores in `mem_regrowp`/`mem_compact`; `drv_release` on `dsk_ent_zero.zero` |
| **fsys** | the file side | -104 | ~13 hand-banked frames onto `kentc_di`/`kentc_bp`/`kent_di`; exits written once by `equ` or retargeted branch; `dsk_vol_add` calling `fm_ncpye`; registers already holding the value |
| **sweep** (wave 2) | everything else resident, and a second look | -103 | `vid_mono_text`, `vid_ac_pal`, `ss_start_x`/`ss_set_x`, `mod_check`, the dock's `db_*` written at their calls; `api_cpname` banks ES:DI itself; `mem_region_reloc` on the ladder; `and word [x],0` where flags are dead |
| **ops** | toasts, fprog, `OSAPI_FS_OP`, the counting hunks, `fsx.inc` | -74 | the queued and staged toasts as two records of one shape (`toast_promote` folded); `FPG_NOWN` = 0FEh so 0FFh is the kernel owner untranslated; `fpg_opend` gone |
| **place** | where code lives | -46 | the save's no-system-disk line into CTRL.DRV; `SYSTEM.CFG`/`KERNEL.SYS` names staged into `cp_sbuf` by each image (SPEC.md 51.5.3) |
| coordinator | the merge | -2 | `sch_chsave` had no reader left after ops and gui (`.bss`) |

The unpassed work itself (the heap granule and `OSAPI_MEM_P`, the toast queue
and `OSAPI_FS_OP`, the review fixes) was +188 on kern_big; heap and ops, whose
rows were exactly that work, took -178 of it back, and the general finders the
rest and more.

## 2. HOT PATHS AND PARITY

Nothing on the hot-path list grew; `dsk_xfer`, `dsk_runcap`,
`dsk_next_clus_x`, the LZ and directory-scan loops are byte-identical, the
damage-repaint path only got cheaper (gui built inlining `wm_dmg_shadowed`
there, measured ~12 clocks a window from two relaxed jumps, and backed it
out - pass 15's DECLINED 3's reason). Faster: IRQ0's `sch_isr` chain, the idle
`toast_pass`, `fpg_finish` on every `gfx_unlock`, the dock paint.

**No new parity pad this pass.** Every agent chose between equivalent forms to
keep the shifts in front of the hot routines even; gui moved two one-byte
`.text` variables (`kbd_ovb`, and kern_small's `menu_dropd`) to just after
`ui_task` for the same end, and fsys spent pass 12's `cmc`/`jc` byte in
`dsk_bpb_bank` (now `jae`). `hotpar` is green on the merged tip.

## 3. WANTS THE OWNER

Every one is a FUNCTION change - a feature that today needs no disk would read
the system disk (one `int 13h`, ~400 ms on a 5150) and refuse without it. All
are ESTIMATES from symbol spans; none is built.

1. **Shortcut and `.LNK` OPEN into CTRL.DRV's settings core, ~-280 to -330**
   (place.md: `sc_walk`, `lnk_open`, `sc_open`, `stik_boot`). Desktop
   shortcuts already load the core for every other gesture. Cost: opening a
   shortcut or a `.LNK` needs the system disk in, and the core's ~3 KB is
   claimed while the target launches.
2. **A shortcut's right-click popup and Delete key into the core, ~-100**
   (place.md). The popup itself would refuse without the system disk, where
   today it shows and only the pick refuses.
3. **The About box into a module, ~-260 on every kernel** (sweep.md:
   `app_about_paint_x` and its strings and template). About would refuse
   without the system disk.

## 4. WANTS AN ABI BREAK

None. `OSAPI_MEM_P` and `OSAPI_FS_OP` (not yet on `main`) were left as their
contracts read. `dsk_fatw_claim` now returns CF = 0 on a refusal, outside its
documented "out: nothing" (heap.md).

## 5. DEFECTS FOUND (all fixed but the last)

1. **kern_mega: a protected-mode regrow that MOVES a conventional claim
   rebased its selectors by 0 or 1 paragraph** instead of the distance,
   since `c2e139eb` - `mem_bcopy` writes DX while its banner said it preserved
   everything. Fixed (+2, kern_mega only), banner corrected. Found by reading;
   no row drives that path (heap.md).
2. **`make mega` failed at the base** on `tests/megaseg.txt`: `1dea2435`
   added 78 bytes of kern_mega `.text` with no line. Fixed by moving the
   bracket's protected-mode stores into `.mega` (ops.md item 0); the figure is
   3,248 at the tip against the file's 3,250.
3. **`tools/os88rom.py` gave `vid_seg` an EMPTY hash span**, so the kern_small
   ROM refused its kernel whenever `vid_seg` sat at an even address (it had
   been odd by luck; this pass's shifts moved it). Fixed - a span ends at the
   next HIGHER address; `COLDHMA.SYS` is cut by the same code (sweep.md).
4. **`dockpos` was red at the base**: it rounded the Dock module's claim to
   the KB, which `aafe18bc` changed to the granule. Fixed, and shown to go red
   with the KB rounding put back (sweep.md).
5. **`tests/shutdown.py` raced the module load** - it read the dialog
   through the CPFP slot before `mod_need` filled it. Fixed (place.md).
6. **`docs/INDEX.md` was stale at the brief's commit**, which fails `make` on
   a fresh worktree; regenerated.
7. Not fixed: `wm_dg_precover`'s banner claims to preserve registers its
   callee clobbers; its one caller reloads them all (gui.md).
8. **The toast queue (SPEC.md 59) has no registered row**; ops drove it with
   an unregistered MartyPC script (stage, queue, replace, promote, retire).

## 6. WHAT IS LEFT

- **The "where code lives" lever is spent without a function change** (place
  walked the reachability: 523 bytes on kern_big, everything but this pass's
  two items refused for reasons that still hold). What remains there is
  section 3.
- The blob is 6,655 of 6,656, `.ovl` 5,116 of 5,120; CTRL.DRV's claim is its
  file to the granule now (`aafe18bc`), so it has no KB cliff.
- `tests/megaseg.txt` may be tightened 3,250 -> 3,248.
- CROSS-FILE odds and ends no one took: `osapi_fs_ent_x` reusing
  `osapi_desk_item_x`'s tail (-3), `dsk_fatw_pick`'s jump to a `ret` (-1),
  `drv_load_at`'s DX bank around `mem_movable_x` (-2), `hb_s_ptr` (13, blob
  and HIBER.DRV only).

## 7. WHAT WAS RUN

Each agent ran its own rows once after its last commit (the notes list them;
all green after the fixes in section 5). The coordinator ran the
deduplicated union of every agent's rows once on the merged tip with
`tools/os88soak.py`, and `test-full` once - section 7.1.

### 7.1 The final pass

- **The union, 159 rows, once, on the merged tip** (`tools/os88soak.py`,
  run `20261101-093532`, `unicorn` installed and 86Box built first so the
  PicoMEM and `megarmb286`/`megarmbext`/`megacurdisk` rows ran): **157 ok,
  1 FAIL, 1 SKIP** in 17 minutes. The SKIP is `hmahiber` (wants QEMU 9.2).
  The FAIL is `guidelock-full`: this pass's two pages (CTRL_DRV, VESA_DRV,
  both through `ctrl.inc`, nothing a user reads changed) are restamped, and
  the **24 others were already stale at the base** - their sources are
  `apps/` packages no agent touched - and are left to their owners.
- **`test-full`, once: 76 ok, 5 FAIL, 3 SKIP.** `image`, `pkg` and `sysman`
  read images built before the record's own commits moved the build number,
  and `docindex` was stale after the last merge; with `make` re-run and the
  index regenerated, all four are ok. `guidelock-full` fails as above.
- `make`, `make small`, `make emu` and `make mega` build at the tip (the
  megaseg gate green); the fast tier is 68/68 with 2 skips.

## 8. METHOD LESSONS

- **A second wave on what the first wave's finished agents freed paid as much
  as any first-wave agent** (sweep, -103). Starting it when three of five had
  landed cost nothing: their files were free and the box had cores.
- **Every merge after the first conflicted on `docs/INDEX.md`** again (pass
  15's lesson 2). Take either side and regenerate; it is generated.
- **A gate can be green by luck of PARITY** - `os88rom.py`'s empty span was
  covered by the round-up to words whenever the label was odd. A size pass is
  what moves odd labels, so it is the pass that finds these.
- **A single-caller routine's register saves are the cheapest bytes left**:
  gui's and sweep's largest items were callee-saves no one read.
