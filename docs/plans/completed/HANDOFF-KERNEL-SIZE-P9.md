# Kernel size pass 8: the record, and what is left for a ninth

**PASS 8 HAS LANDED** on `kernel-size-optimization-p8`, cut from `elendilon` at
`4d10404`. The file is named `-P9` for the reason pass 6's record is named
`-P7`: each pass's record is named for the pass it hands to. (Pass 7 was the
stream writer's own, `docs/reports/STREAM-WRITER-SIZE-2026-09-30.md`, and has
no handoff file.) Its companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P7.md`**: pass 6's record. Its
  §3 costed list is mostly answered below; its §2 adjacencies still stand.
* **`docs/plans/LAST-DROP-BYTES.md`** §7: 7.11 and 7.7.8 are BUILT by this
  pass, 7.7.7 re-derived, 7.8.1 still open.
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand. Blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: everything that had not had a size pass since pass 6 landed
(`e33338f`), with the stream writer excluded because it had just had its own.
The target was **50% of the resident bytes those commits added** for anything
not on a hot path, and hot paths optimised without losing significant speed.
Optimisations outside the target were in scope, and the owner opened two doors
no earlier pass had: **ABI/API changes are allowed**, and **innovative
approaches and reworks are allowed**.

The per-commit measurement (every first-parent commit touching `kernel/`,
`kernsize --json` at each) put the un-passed work at:

| un-passed work | resident big / small | module image growth |
|---|---:|---:|
| split sets and File > Uncompress To (`c39ac5a` `f6280f2` `fd3bd19`) | +124 / 0 | CLONE.DRV **+2,699** |
| the Control Panel's Floppy page (`52e7703` `b1ac925`) | +17 / 0 | CTRL.DRV +615 |
| a failing disk looks alive, Write Img, the clone's failure report (`1774795` `f57ef3a` `85514c4` `2a205da`) | +39 / +39 | |
| Format lays the tracks down (`a294a88`) | ~0 | FORMAT.DRV +302 |
| test-combo merges | +1 / +2 | |
| **total** | **+181 / +41** | **+3,616** |

So the resident target was small and the module growth was the larger
number; the modules were made a secondary target at the same 50%.

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`, by
`tools/kernsize.py --json`, never by rungs:

| | base `4d10404` | close | Δ |
|---|---:|---:|---:|
| kern_big resident | 100,723 | **97,384** | **−3,339** (−3.3%) |
| kern_small resident | 68,210 | **65,398** | **−2,812** (−4.1%) |
| kern_big `KERN_SIZE` | 106,496 | **102,912** | −3,584 (a fact, not a goal) |
| kern_small `KERN_SIZE` | 70,656 | **67,584** | −3,072 |
| CTRL.DRV image | 9,104 | **8,781** | −323 (52.5% of +615, net of ~300 moved IN from resident) |
| CLONE.DRV image | 10,585 | **9,932** | −653 (24% of +2,699; the rest is the join itself) |
| FORMAT.DRV image | 1,499 | **1,304** | −195 (−251, 83%, net of the 56-byte table moved IN) |
| HIBER.DRV image | 6,474 | **6,662** | +188 (the resume's timed font re-pick; −3 of jccs) |
| DOCK.DRV image | 2,547 | 2,515 | −32 |

**The pass took out 18x what the target added on kern_big and 69x on
kern_small.** Against the target's own lines the take was 25-88% per topic
(§1); the rest came from the files around them, which the brief allowed.

## 1. WHO TOOK WHAT

Six agents, each in a worktree of its own cut at `4d10404` and owning a set of
kernel files, merged `--no-ff` by the coordinator and re-measured after every
merge. **Every merge reproduced the sum of the branches to the byte.**

| agent | files | big | small | module |
|---|---|---:|---:|---|
| **speaker** (outside focus) | snd, fsx, sched, kernel.asm, events, instance, apps, ui, xmem, cpudet, hiber, memory, loader, assoc | **−725** | **−748** | HIBER −3 |
| **split** | compress, clone, files, filecp, lz, mod | −253 | −172 | CLONE −671 |
| **floppy** | ctrl, driver, menu, desk, dock, dockmod, extmod, toast, clock, blank | −468 | −178 | CTRL −323, DOCK −32 |
| **disk** | disk, diskw, fdlg, fprog, dskwin | −260 | −220 | FORMAT −195, CLONE +18 |
| **wm** | wm, icons, font, clip | **−1,301** | **−1,323** | |
| **gfx** | vga12, softgfx, band, vidsel, viddet, splash, mouse, mouproto | −336 | −171 | |
| coordinator | cross-owner leftovers | −2 | 0 | FILECP (small) −2 |

### 1.1 The largest items

* **Glyphs read in the ROM, not copied** (wm, −768 `.lowbss` on both, −710 /
  −743 net). SPEC.md 6. `font_init` stores `[font_seg]:[font_base]` and every
  renderer reads the table there. Measured on the GENUINE IBM 5150 27 OCT 82
  ROM under MartyPC: `font_char` −1.0%, `font_run_x` **+1.0%** (a segment load
  a run and three `cs:` a row, not the ROM read), `font_run_cell` +0.1%.
  **MartyPC charges a ROM read exactly what a RAM read costs**, by
  construction (`bus/memory.rs` `get_read_wait`), which is right for a 5150's
  planar ROM and says nothing about an 8-bit option ROM on a faster bus - so
  §6.0.1 was added (below) and `tests/romfont` is the field instrument.
  `BAKED_FONT` keeps the RAM copy, and `FONT=` now builds on kern_small too.
* **The PWM clip is a library** (speaker, −493 on both). `apps/os88pcm.inc`
  `os88pcm_play` carries the old slot contract; `OSAPI_SND_PLAY` became its
  grant/release door (SPEC.md 34.4). The 256-byte `snd_xlat`, `spk_pcm_run`,
  `snd_xlat_build` and `snd_abort` left the kernel. RECORDER is the one
  program that carries the loop (+463 of package). **kern_small plays no clip
  at all** (−173 more, SPEC.md 34.4.1): its only player is live-media-only.
* **`OSAPI_FSX_SPK` as a thin door** (speaker, −108 big; LAST-DROP-BYTES 7.11,
  priced at 70-80). The caller writes the IRQ0 vector and channel 0 through
  `apps/os88spk.inc`; the N range check, `fsx_mine`, the SI/DI fence, the
  block, the DX:BX return and the close's answer went. **The SI/DI fence
  (`SPK_E_ADDR`) was upstream's, added in #203** - its removal wants agreeing
  with upstream when this goes there.
* **The window manager's rects** (wm, −512 on `wm.inc`): one copier for the
  kernel's rects, the title bar's boxes by one body, the clip fragment in
  registers, one overlap test, the resize damage through `wm_dmg_union`. The
  clip paths got FASTER: `wm_clip_set` −8.1%, `wm_clip_occl_r` −12.2%,
  `wm_chrome_clip` −8.0%, `wm_dmg_wins` −4.1%.
* **The Control Panel's tables and names into CTRL.DRV** (floppy, −418 big
  across three commits): `cp_items`, the static list names, `drv_cp_name`,
  the DRVE_* strings, `drv_errstr`. CTRL.DRV got its own epilogue ladder
  (`kretm_*`, 39 sites) and prologue helpers, which paid for what moved in.
* **FORMAT.DRV re-cut** (disk): `dskw_fmt_tab` moved into the image (read
  through CS), a table row IS the BPB span, the probe banks geometry on the
  stack. Format's bar counts TRACKS now.
* **Relaxed jccs.** The split agent's `relax.py`/`tramp.py` (a relaxed 5-byte
  jcc whose target already has a `jmp` in short reach is a "free
  trampoline") found the last rich peephole; each agent took its files'. The
  hot ones are faster too (a short taken jcc is 16 cycles against 4+15).
  Those that remain are listed in §4.

## 2. HOT PATHS

| path | before → after | how |
|---|---|---|
| lock + unlock | 1,633 → 1,559 (**−4.5%**) | MartyPC, Hercules and VGA, parked stub, best of 3 |
| 8×8 `gfx_fill` VGA | 2,651 → 2,432 (**−8.3%**) | same, two gfx commits combined |
| 64×16 `gfx_fill` VGA | 6,077 → 5,618 (**−7.6%**) | same |
| 64×16 `gfx_fill_pat` / `_gray` VGA | 8,135 → 7,815 / 6,346 → 6,282 | same |
| cursor hide+show, both edges | −2.3% Herc, −1.4% VGA | same |
| `font_char` | −1.0% CGA, −1.1% Herc, +0.7% VGA | MartyPC paired calls, genuine IBM ROM |
| `font_run_x` | **+1.0% CGA, +1.0% Herc**, +0.04% VGA | same |
| `wm_clip_set` / `wm_clip_occl_r` / `wm_chrome_clip` | −8.1% / −12.2% / −8.0% | same |
| `sch_isr`, `sch_switch`, `snd_tick`, `evq_*`, `task_yield` | identical | instruction-for-instruction listing compare |
| a show/hide that does NOT cross an edge | +11 / +14 cycles | hand count |
| `fpg_baron` | +43 cycles an int 13h | hand count, against ≥24 ms |

The one regression over 1% is `font_run_x` at +1.0%, inside the 2% the brief
set, and it is what 768 bytes of every machine's RAM cost.

## 3. THE ROM FONT ON A SLOW BUS: SPEC.md 6.0.1

The owner has a PVGA1A in a 5150 - an 8-bit VGA whose option ROM MartyPC
cannot price - and could not run the field benchmark for a week. The kernel
already asked `int 10h AX=1130h` first, which on that machine answers the
CARD's ROM at `C000:`, not the zero-wait planar one.

A PIT-timed detector with a 1.25x threshold and a 1KB claim did not fit the
nine-sector blob (15 bytes of kern_small's `.ovl` left, and an overlay scan
found 3 bytes), so it first shipped as a cheaper planar-match rule. The owner
asked for the room to be made, and **`BOOT2_SECS` went 9 -> 10**: still 2
`int 13h` calls on all four geometries (`t_blobruns`), `KSIG_OFF` 6144 -> 5632
so the canary stays at file sector 21, KERNEL.SYS +384 bytes. What shipped,
at **0 resident bytes** on both kernels:

* `FONT_PICK` (in the blob on both kernels now) times, with PIT channel 0
  latched around a `rep lodsb` and IF off, the BIOS's `AX=1130h` table, RAM,
  and `F000:FA6E` - the last only when its 760 bytes are identical to the
  BIOS's, so the choice never changes what text looks like. The planar table
  wins unless the BIOS table beats it by an eighth (a tie goes to the system
  board; the margin stops two boots picking differently on noise).
* Only when even the chosen table is more than a quarter slower than RAM is
  it copied, into a 1KB `MEM_K_FONT` claim made top-down and pinned before
  any driver or package exists - the heap's top, no barrier to either
  compaction pass.
* The three counts (chosen, RAM, BIOS) are left at `0040:00F8` behind `'FP'`
  (the upper half of the BIOS's inter-application area; kern_dos's mailbox
  is the lower half) for ROMFONT to show.
* `hbm_wake` re-runs the timed pick after a resume (HIBER.DRV +188 of module
  across both steps): a hibernated image resumed elsewhere no longer keeps a
  pointer into the writing machine's ROM. The same path serves kern_dos's
  live return.
* Boot time, MartyPC: `font_init` +10.4 ms on a 5150 CGA, absorbed by the
  mouse probe's fixed window (+72 cycles to a settled desktop); +10.9 ms on
  the VGA XT; and **-166 ms** on kern_small's 360KB disk, where the tenth
  sector moved the kernel read's split and saved a revolution. The kernel's
  own reading on MartyPC's VGA XT: 2525 / 2525 / 2526 PIT counts, reads
  `F000:FB6E` in place.
* `FONTSLOW=1` forces the copy; `tests/fontpick.py` (soak) checks both arms
  and the recorded ratio (900-1250), and went red for three planted breaks.

**`ROMFONT` is the owner's field instrument** (`make romfont`,
`build/romfont360.img`): it shows the BIOS table, whether the planar set
matches, the kernel's choice, and ROM/RAM ×1000 for both tables. MartyPC reads
999-1000.3 on both ratios on both the genuine IBM CGA machine and the VGA XT.

## 4. WHAT IS LEFT, costed

| candidate | bytes | why not taken |
|---|---:|---|
| **Standard File dialog and Cut/Copy/Paste as modules on kern_big** | **~−7,400** | the ON-DEMAND test fails for the dialog on a one-drive machine (a package on a data disk would need the system disk to open a file); **the owner's call**, offered and not answered |
| LAST-DROP-BYTES 7.8.1, the Dock's patched far jump | −42 | self-modifying code: **the owner's call** |
| `osapi_drv_dlg` + `drv_dlg_done` into CTRL.DRV | ~−120 | the driver's completion is lost if the module is dropped with the system disk out; no row reaches `OSAPI_DRV_DLG` |
| `drv_cp_call` + `drv_cp_closed` into CTRL.DRV | ~−90 | same disk hazard |
| kern_small's sixteen refusal cells (LAST-DROP-BYTES 7.7.7) | 128 + | a cell's offset is the ABI both kernels share; dropping them is a second ABI. Their bodies were taken |
| demote rarely-called SLOT cells to RSLOT | ~2 each | renumbers the whole table |
| Timer / Bounce as packages | ~1.8KB big | a product decision |
| `gfx_unlock`'s `[dws_hold]` filter | −7 | +~160 cycles every UI-task unlock |
| mono `jne sw_*` dispatches, `gfx_blit4`'s loop jccs | −3 each | +12 cycles a 1bpp primitive / loop bodies over 127 bytes |
| `font_run_x`'s per-row `cs:` | 0 bytes | the accepted +1% |
| `desk.inc` `ovw_desk_rowcalc` through `spw_near` | −4 resident | +3 to +5 `.ovlw`, which has 28 left |
| `toast_pass`'s lock/call/unlock through `ui_lcall` | −1 | BP is live (the routine preserves every register) |
| remaining relaxed jccs (ui dispatch, sched, wm 5, font 5, icons 3, diskw 5, files ~15) | −3 each | hot, or no free trampoline, or measured flat |
| `wm_su_flay`/`wm_su_try`, `wm_paint_dmg`'s S2/S3 stores | ~−26 | `rect_get`/`rect_put`'s other sets are HELD by the owner (LAST-DROP-BYTES 7.10) |
| `W_DISP` removal | −13 big | needs two static far pointers |
| `hbf_*` thunks through a BP helper | ~−11 `.cold` | a BP audit across every window callback |

**The overlays at the close**, blob at ten sectors (capacity 2,496 of `.ovl`):
kern_big `.ovl` 308 bytes left and `.ovlw` 152 of 5,120; kern_small `.ovl`
415 and `.ovlw` 39 of 1,536. An eleventh sector is still 2 calls on every
geometry (`t_blobruns --sectors 11`). `make NOKZIP=1`'s 360KB system disk
does not fit (361 of 354 clusters; 360 before the tenth sector, so it was
already over).

## 5. BEHAVIOUR AND ABI CHANGES

* **ABI**: `OSAPI_FSX_SPK` (34.11.1: IF=0 across the call, DX=N, BX=CS in,
  AX=K out; `SPK_E_N`/`SPK_E_NOTOPEN`/`SPK_E_ADDR` retired); `OSAPI_SND_PLAY`
  (34.4: a grant/release door, the loop is `apps/os88pcm.inc`); kern_small's
  caps drop `SND_CAP_PCM_EXCL`; `OSAPI_FONT_GLYPHS` answers the ROM table (or
  the heap copy, or the baked face) and it is READ-ONLY; `CLA_SAVE` and
  `CLV_JOIN` are gone from CLONE.DRV's verb set, which opens its own Save
  boxes. Every caller in the tree moved with each.
* **Visible**: Format's bar counts tracks. A plain Uncompress of a part says
  its FERR_* itself. Uncompress To's probe takes the join's 82KB claim, so a
  tight heap refuses before the box opens.
* **Internal contracts**, each documented at the routine: `dsk_dirw_get_x`
  answers end-of-chain in ZF; `drv_cls_svc_x` refuses `DRVC_POINT`;
  `fm_vp_set` returns DI; `spk_off` clobbers AX; `ui_lit_on/off` and
  `cur_shape_pass` spend BP; `wm_clip_add` takes its fragment in AX..DX;
  `[ui_post]` may be `inc`'d (a wrap after 256 unwalked posts is recovered by
  the tick's sweep within 55 ms, SPEC.md 13.12/13.13).
* **New layout adjacencies**, each pinned by an `%if`/`%error` and planted
  once to prove it fires (except the disk agent's three): `ui_armr`/`ui_post`,
  `cur_shape`/`cur_level`, `vga_pn`/`vga_pm`, `gfx_pairbuilt`/`vga_p4built`,
  `vid_ndisp`/`vid_cur`, `cp_dirty`/`cp_wdirty`, `menu_bdirty`/`menu_bovr`,
  `cp_only`/`cp_skip`, the `clk_sec..mon` and `clk_sn_*` layouts,
  `cmz_jtgtd`/`cmz_jsrcd`, the part header's bytes 6..20, `cmz_done`, the
  CL_*SPT triples, `W_MENUS`, `ZR_*/NR_*`, `WSU_*`, `WCR_SZ`, the seam cells,
  `MODC_BSS`, the FORMAT table's row offsets and span.
* **New helpers others may use**: `ui_lcall` (`mov bp, X / call ui_lcall` in
  place of lock/call/unlock), `spw_near` (a far door from an overlay or module
  into any near `.text` routine, routine in BP), `kretm_*`/`kentm_*` (CTRL.DRV's
  own ladder).

## 6. DEFECTS FOUND

Fixed:
* **The split-set join said "Missing SET.003" early**: `[cmz_jretry]` was a
  flag Enter set and only the prompt cleared. It is the PART Enter was for now;
  `czto`'s `ask3quiet` leg goes red without the fix.
* **`drv_svc_clear` did not test CF** after `drv_cls_svc_x` (latent until
  class 6 lost its slot). `tests/unit/t_clscf.py` (fast) found it.
* **A hibernated image resumed on another machine kept a ROM pointer** into
  the first machine's BIOS - introduced by the ROM font and fixed with it (§3).
* **`FONT=` did not build on kern_small** at the base.
* The stack-overflow panel's evidence moved onto the stack (`stkpanel` row).
* Dead code: `mem_can_move`'s no-op test, `hb_pad`, `gfx_bitclr`,
  `splf_fill`, `cp_ipap`, `dkx_px_rect`, `dkx_hole_winb`.
* Two rigs that named symbols this pass removed: `tests/cpnames.py` and
  `tools/os88linecost.py` (the latter was already broken at the base for
  another reason: `gfx_ln_cx1` went with the `gfx_line` family).

Found and NOT fixed, all present at the base:
* **`fcpcopy` is intermittent**: 3/6 at `4d10404`, 3/4 at the close, the
  same leg - the folder reaches the disk (`mdir` shows `SYSTEM/MEDIA`) but
  the window's listing does not show it when the harness reads. Either the
  harness reads before the paste's narrowed re-list (SPEC.md 22.3.1) or the
  re-list misses the window; `os88bisect classify` refuses to bisect a rate.
* **`lzship` / `lzship-lzb` fail**: Tracker's screen never settles after it
  opens BEVERLY.MOD, at the base and at the close.
* **`FDDABSENT=1` does not assemble on kern_big**: the boot overlay's window
  half outgrows its region.
* `tools/os88modcost.py` reports ".ovlw attributed 35% short" at the base.
* `czto` is load-sensitive (host-time polling).
* Shared `build/martypc/inst/` means two agents running the SAME row at once
  can collide (one `hdboot` failure, passed alone at base and branch).

## 7. METHOD LESSONS

* **The per-commit measurement is the brief.** It turned "everything since
  the squash" into 181 resident bytes and 3,616 of modules, which is what
  made the modules a target at all.
* **The worktrees started at the right commit this time**, because the
  coordinator cut them itself (`git worktree add -b ksp8-<t> ... 4d10404`)
  rather than using the Agent tool's isolation; every agent reproduced the
  base to the byte first.
* **Forward every scanner.** The speaker agent's `ui_post`/jump sweep and the
  split agent's relaxed-jcc tools went to every other owner mid-pass; both
  found sites in every file set. The tools are worth keeping: `relax.py` and
  `tramp.py` (generalised with `WT=<tree>`) - a candidate for `tools/`.
* **Symbols a pass deletes are named by rigs on other branches.** Two
  harnesses broke only at integration (`font_glyphs`), which no branch's own
  rows could see. A scoped integration soak after the last merge is what
  caught them: 56 rows, 55 green, and the one red is §6's pre-existing rate.
* **An emulator that does not model a cost cannot measure it.** The ROM font
  was kept on a measurement that was exact about work and silent about wait
  states; the right response was a field instrument and a zero-cost rule
  that avoids the unmodelled case, not a threshold tuned on MartyPC.

## 8. WHAT WAS RUN

The fast tier at every agent commit and every merge; `make small` and
`os88ovlchk` at every merge; checkdocs clean. Per agent, 18 to 124 soak rows
at two emulator lanes, all green bar the §6 base failures. On the merged tree:
a 56-row integration soak (55 ok, `fcpcopy` the base's rate), then the font
follow-up's 10 rows (10/10). The whole soak tier was not run: it is the
owner's to ask for.
