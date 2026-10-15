# Kernel size pass 10: the record, and what is left for an eleventh

**PASS 10 HAS LANDED** on `kernel-size-p10`, cut from `elendilon-next` at
`833f13e4`. The file is named `-P11` for the reason pass 9's record is named
`-P10`: each pass's record is named for the pass it hands to. Its companions,
and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P10.md`**: pass 9's record.
* **`docs/plans/completed/ksp10/`**: the eleven agents' own notes, one file
  each, written AS THEY DECIDED (pass 9's lesson): every item TAKEN with its
  bytes on both kernels, every item REFUSED with the bytes it would have been
  and why, and the cross-file list. **Read the refused lists before
  re-proposing anything** - most of what an eleventh pass will think of first
  is in one of them with a reason.
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand, blessed at the close
  of this pass for all three kernels.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: **1.5 KB of resident kernel** across the whole kernel, more
welcome. The owner expected the remaining wins to be refactors and small bytes
in the less-visited code, since nine passes had been over the big things, and
asked that the work which had never had a pass get an agent or two: #230's
review fixes (+227 resident on `kern_big`, +15 on `kern_small`, never passed:
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-08.md`), the squash's 2 bytes
in `fdlg_grab`, and main's #229 Covox page (`CTRL.DRV` +292, `SOUND.DRV`
+252). About a dozen agents.

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`:

| | base `833f13e4` | close | Δ |
|---|---:|---:|---:|
| **kern_big resident** | 93,296 | **91,120** | **-2,176** (-2.3%) |
| kern_big `.text` / `.bss` / `.cold` | 44,308 / 5,133 / 37,921 | 43,199 / 5,105 / 36,882 | -1,109 / -28 / -1,039 |
| kern_big `KERN_SIZE` | 99,328 | **97,280** | **-2,048** (four rungs: 2 KB of heap on every machine) |
| kern_big `.text`+`.bss` of `KERN_CODE_MAX` | 49,441 | 48,304 | 16,095 -> **17,232 left** |
| **kern_small resident** | 62,826 | **61,189** | **-1,637** (-2.6%) |
| kern_small `KERN_SIZE` | 65,024 | **63,488** | -1,536 |
| kern_emu resident / `KERN_SIZE` | | | -2,188 / -2,560 (97,280) |
| overlay (`.ovl`+`.ovlw`), big / small | | | -10 / -10 (`snd.inc`'s boot zeroing) |
| `CTRL.DRV` big / small | 11,450 / 4,621 | 11,300 / 4,490 | -150 / -131 |
| `SOUND.DRV` (`sound.bin`) | 6,721 | 6,637 | -84 |
| `FDLG.DRV` (small) | 1,256 | 1,246 | -10 |
| `FILECP.DRV` (small) | 1,940 | 1,936 | -4 |
| `CLONE`, `HIBER`, `DOCK`, `EXTD`, `FORMAT` | | | 0 |

**The merge reproduced the sum of the branches exactly**, on both kernels, at
every one of the eleven merges (the kernsize line was read after each): no two
agents touched the same bytes. The figures include five correctness fixes
(§3), which together cost +2 resident on `kern_big` and +6 on `kern_small`.

## 1. WHO TOOK WHAT

Ten agents, each in a worktree cut by the coordinator at `833f13e4` and owning
a set of kernel files (editing nothing else; a saving that needed another
agent's file went on a cross-file list), merged `--no-ff`; then an eleventh,
`xfile`, on the merged tree, for the cross-file lists.

| agent | files | big | small |
|---|---|---:|---:|
| **disk** | disk, diskw, dskwin, drvvol, lz, mod | -385 | -367 |
| **input** | mouse, mouproto, menu, events, icons, clip, toast | -305 | -300 |
| **files** | files, filecp, fprog | -300 | -219 |
| **core** | kernel.asm, memory, sched, instance, apps, loader, cpudet | -253 | -158 |
| **wm** | wm | -242 | -218 |
| **gfx** | vga12, softgfx, font, band, splash, spinner | -205 | -63 |
| **shell** | ui, driver, assoc, dock, dockmod, desksc, clock, clockw, blank, shutdown | -203 | -155 |
| **new** | fdlg, desk + #230's review fixes | -108 | -10 |
| **hw** | fsx, vidsel, viddet, xmem, hiber, hb*, clone, compress, extmod, vmmouse | -71 | -43 |
| **xfile** | the cross-file lists, the `.text` entry ladder | -63 | -69 |
| **covox** | ctrl, snd, `drivers/sound/` + #229's Covox | -41 | -39 |
| coordinator | §3.7's stub | 0 | +4 |
| **total** | | **-2,176** | **-1,637** |

### 1.1 The shapes that recurred

The agents' commit messages carry the instruction-level detail and their notes
the per-item bytes; these are the shapes worth knowing.

* **Two bodies that are one body.** `dskw_wdata`/`dskw_rdata` share one
  cluster body, `dskw_xclus`, its next-cluster step a continuation in BP (disk,
  -45). `wm_clip_occl`/`wm_dmg_occl` share one occlusion walk, the subtract
  routine named in DI (wm, -46, and faster per window). `cur_mvcols`'s two
  column loops are one (input, -24). `menu_setup` is `menu_track`'s and
  `menu_popup`'s shared head. `dsk_synth_name`'s stem and extension loops are
  one.
* **A `.text` entry ladder.** `.cold` had `kentc_*` and `.text` only the
  `kret_*` exits. `kent_bp` and `kent_di` now sit beside `kret_*` in
  `kernel.asm` (wm, then xfile) and serve 28 once-per-operation prologues, 3-4
  bytes each. **Never** a drawing primitive, the cursor, `sch_*`, `wm_clip_rows`
  or `wm_su_*`; and **not `viddet.inc`'s splash-time routines**, because the
  ladder is at the end of `.text`, which the splash calls before it is loaded
  (`SPL_RESIDENT`; the helper's header says so).
* **Dead code nobody had noticed was dead.** `dskw_remount_x`, `dsk_find_name_x`
  (-60), `vgas_left`, `gfx_denter`, `kbm_slock`, `cp_snd_lptok`, the fourth
  `cp_snd_tiers` entry, `wm_clip_subx`, and `snd_tick`'s generation compare
  (§3.5).
* **Register banks a callee did not need**, each proved against every caller:
  `inst_vol_enter` (five pairs, under every file API call), `dsk_ent_ofs`,
  `dskw_flush_x` / `dsk_fat_window` (because `disk_read`/`disk_write` preserve
  every register), and the `.selmove` and `fm_draw_icon16` banks.
* **Single-caller routines written at their call site**: `menu_save_kb`,
  `sch_wk_restart` (into `mem_region_reloc`), `mem_busy_seg`, `mem_cp_both`,
  `mem_cp_move`, `ui_krect4`, `vgas_lincopy`.
* **`.bss` sharing a transient**: `menu_tbuf equ menu_clkbuf` (-25 on both
  kernels). Both are composed from scratch on every use and every user runs on
  the UI task under the gfx lock; the source carries the argument and a fit
  assertion.
* **A relaxed jump is a silent 5-byte pair.** NASM relaxes an out-of-range
  `jcc` into `j!cc +3 / jmp near` under `cpu 8086` without a word; `dsk_xfer`
  had four, the Covox page two. Re-laying code so they stay short was worth
  bytes in three files.

### 1.2 The unpassed work

* **#230's `e890eb7`** (the desk/wm zone overflow, +147): the desk half's ~117
  bytes gave back 74 (a 33-bit rotate-through-carry ring for the zone mask,
  checked against the old code in a Python model over 20,000 masks), the wm
  half's re-seed gave back 20 (`wm_dmg_rebands`). The fix stands.
* **#230's `7cd330e`** (the chooser that has ended, +73): ~4 back. What is left
  IS the fix - `fdlg_btn`'s release guard and `fdlg_gdis` - and the agent found
  the fix incomplete (§3.2).
* **The squash's 2 bytes in `fdlg_grab`**: with main's new branches the `cmc`
  shape is worth 1, not 2; the answered test that `fdlg_gate` absorbed saved 7.
* **#229's Covox**: `CTRL.DRV`'s page about 48% back (292 -> ~150, by listing),
  `SOUND.DRV`'s about 37% (259 -> ~164). What would close the driver's gap drops
  a published cell or reorders port I/O (`ksp10/covox.md`).
* **#230's `3b503fd`** (Disk window arrows, +7): -11 in `files.inc`; the
  `vga12.inc` refusal ladder left exactly as it is.

## 2. HOT PATHS

Every agent wrote an instruction or cycle count for each hot routine it
touched into the commit message. **None got slower; the ones that moved,
moved down:** the serial packet decode (25 -> 21 instructions, no longer
touches SI), `mou_byte`'s phase dispatch, `cur_move_mono` pass 1 (10-11 -> 5
instructions a row), `font_run_cell`'s row loop and `font_run_x`'s four
character loops (an instruction and ~5 clocks each), `gfx_ink` (~73 -> ~66
clocks), `fsx_insync`'s poll (~27 clocks), `task_yield`'s unlocked arm (12
clocks), the occlusion walk, and the FAT read path per cluster (18 cycles).
`sch_switch`, `sch_isr`'s switch path, the API far-call cell and
`dsk_xfer`'s per-sector path are untouched.

**gfxbench on MartyPC** (gfx agent, base against its branch, VGA XT, 5150
Hercules and CGA): every `FONT_RUN` row 0.5-1.2% faster on all three
adapters, `FONT_STR` and `PAIR` 0.1-0.4%, fills and XOR rects up to 0.6%,
pixel/span/glyph rows flat within 0.02%. **Two regressions the bench showed
were reverted** - a gray fill sharing its teardown cost 57 clocks where ~15
was estimated (+0.29% on VGA), and `gfx_frame`'s shared tail was turned round
- and **one is kept with its price**: `GFX_FILL_PAT` +0.28% on VGA for 26
bytes, the rare fill. `GFX_UNLOCK+LOCK` and `SET_COLOR` moved on VGA only, in
untouched code; pass 9 recorded the same two rows as noise.

The costs paid on purpose, all once per operation: the FAT write path, 11
cycles a cluster against `dskw_alloc`'s FAT scan per cluster; a `kent_*`
prologue, ~95 cycles a call; the drag tracker, one call per axis per pass,
~40 cycles a tick.

## 3. DEFECTS FOUND AND FIXED

Each in its own commit or its own paragraph of one.

### 3.1 `fm_choose`'s in-place move repainted the PATH BUFFER as a window

`eb9407ac`, kern_big, +2. `.inplace` loaded BX with the window's `FS_PATH`
buffer for the path seed, then did `mov si, bx / call fm_repaint`, so the
repaint read a window record out of the path's bytes - a bit of `W_FLAGS`
cleared, `fm_cfill` white-filling a rectangle read from the path, the listing
drawn at those coordinates. Reached with all four Disk windows open, by a
desktop drive icon or `fmf_files_open`. Since `ea48c34`. **Found by reading;
not reproduced on the emulator.**

### 3.2 A key could still reach a Standard File chooser that had answered

`2da1ad07`. SPEC.md 38.2: once `[fdlg_act]` is set, `fdlg_top` sends a key to
no window. #230's fix taught `fdlg_grab` (presses) about the answered case
and not the key path, so a key later in the same event drain still reached the
chooser: Backspace or Enter could move the folder a posted Save commits into,
and Escape could overwrite a commit with a cancel. `fdlg_gate` now reports an
answered chooser as gone and `fdlg_reap` takes the answer before asking.
**No row sends that key**; `fdlgchoose`'s drain test sends a press.

### 3.3 `SOUND.DRV` re-ran both port probes at every `DRVV_READY`

`ae1e0076`, 0 bytes. `snd_entry`'s READY jumped to `.nosb`, and the MPU-401
and Covox probes had since been written below that label, so every load sent
the MPU reset and UART command again and rewrote the three LPT latches. READY
now jumps to `.ready`, which is what its comment always said.

### 3.4 `mem_owner_of_x` matched a FREE record when BX = 0

`d12ffcf7` (not split out). Its counted loop compared every record's `MC_SEG`
with BX, free ones included, and answered the first free record's stale owner
where the header says CF = 1. `mem_sum_kb` reaches it through
`drv_owns_seg_x` with owner word 0 (instance slot 0), so a free record still
naming a driver's segment could file slot 0's claims under System. It walks
with `mem_next` now, which skips free records. Behaviour differs only for
BX = 0.

### 3.5 Not a defect, but a guard that could never refuse

`snd_tick` compared `[snd_texp_gen]` with `[snd_town_gen]`; both are written
together, from one register, in one IF=0 window, and zeroed together, so they
were always equal. The compare, both bytes, their stores and their boot zeroing
are gone (-24 `.text`, -2 `.bss`, -10 overlay). SPEC.md 34.3 now says it is the
grant's atomicity that keeps the expiry the current owner's.

### 3.6 `ANIMOFF=1` stopped assembling

The pass's own regression, found by `soak -k buildmatrix`: `CTRL.DRV` went to
11,301 bytes, and the knob's image (11,177) then rounded to an 11 KB claim
that the module's 131 bytes of `.modcb` did not fit (the `MODC_BSS`
assertion). The knob now pads its image to one byte past a KB, which is
`DOSRMARK=1`'s answer for `HIBER.DRV`; the shipped kernel is byte-identical.
**The assertion will fire again** the next time any build shrinks a module
under a KB boundary with its bss on top; §5 has the general fix.

### 3.7 kern_small's `drv_svc_call` far-called a NEAR stub

**Latent since the kern_small arm was written; EXPOSED by this pass, and found
by the integration soak.** `kernel.asm`'s `drv_svc_call` thunk reaches
`drv_svc_call_x` with a FAR call - the live body's `.none` is a `retf` and its
comment says why - but kern_small's stub block shared `drv_svc_none`'s near
`ret`, so a call returned to the caller's offset with `COLD_SEG` still on the
stack. Every caller tested BP = 0 first, so nothing took it - until the covox
agent's `snd_release_inst` (`59dca542`, -4) trusted the callee's own refusal,
and **every instance teardown on kern_small** then went wild: the Standard
File chooser's FDLG.DRV was never given back and the pointer stuck
(`fdlgdrop`, `dispclose-small` and `fdlgchsmall` red 3 of 3 at the close,
green 3 of 3 at the base; `fdlgdrop` bisected to the covox branch, then to
`59dca542`). The stub has its own `xor ax, ax / stc / retf` now: kern_small
+4, so the covox change nets 0 there and keeps its -4 on kern_big. The three
rows are green with it. **The covox agent's own rows were all kern_big**, which
is how a change correct against its callee's documented contract shipped a
crash on the other kernel: the contract was the live body's, and the stub did
not keep it.

Two small races closed as a side effect (input): `toast_pass`'s
`[toast_dirty]` and `menu_draw_bar`'s `[menu_bdirty]` were each tested and then
cleared in two instructions; each is one `xchg` now.

## 4. WHAT WAS RUN

* `make`, `make small` and the fast tier (61/61) after every batch on every
  branch and after every merge; `make emu` at every branch tip and at the
  close. Knob builds as each agent's files needed them (`KBDDIAG=1`,
  `DOSRMARK=1`, `MOUDIAG=1`, and the gfx agent's thirteen display knobs on
  both kernels). `kern_dos` (which includes `disk.inc`, `diskw.inc` and
  `mouproto.inc`) assembles in every `make`.
* `tools/stkbalance.py` over `kernel/`, base against tip, on every branch: no
  new unbalanced path anywhere (core's report went 64 -> 62, the two removed
  being tails it rewrote).
* **Per-branch soak rows, ~45 in all**, one at a time, every one green:
  `deskwhole`, `deskzoom`, `deskflash`, `fdlgchoose`, `fdlgchsmall`,
  `wmchrome`, `zonedmg`, `fmarrows`, `fmcommit`, `fcpcopy`, `shedrelist`,
  `dskwstage`, `lzfile`, `rdcz`, `gfxpoints`, `dispblit`, `wdenter`,
  `tmrepair`, `ptsext`, `mouwheel`, `toastbar`, `icoclip`, `deskitem`,
  `curshape`, `heapcheck`, `regwork`, `drvmove`, `mouseup`, `tmload`,
  `assocopen`, `drvup`, `dispfsxherc`, `hibernate`, `pixelstein-vga`,
  `xmcheck`, `bootsmoke` (CGA and Hercules), `tmrup`, `dispfsx`, `sndplay`
  and `tests/covox.py`'s four arms. `regrowshed` is red at the base with the
  same numbers (its staging no longer fills the 128 KB machine's heap).
* Scratch harnesses on MartyPC, not committed: the DMA bounce (§3.1 of pass
  9's record) driven on purpose through a buffer 0xF0 short of a 64 KB page,
  both arms firing once with the right bytes in the guest and on the flushed
  floppy; `dsk_synth_name` and `dskw_name83` against Python models of the
  originals over 29 crafted inputs; the Disk window's path titles and letter
  keys.
* **The integration soak** is §4.1's. The whole soak tier was not run: it is
  the owner's to ask for.

### 4.1 The integration soak, and why it was taken twice

A first run, 246 rows scoped to every subject the pass touched, was started on
the merged tree and **stopped at 104 rows green**: `tools/os88soak.py` runs
from the checkout (`meta.json`'s `root`), not from a copy, and the
coordinator committed on that checkout while it ran - which moved the build
number (the commit count, SPEC.md 14.2) and the source under the rows, and five
rows then failed in `os88sym` with *"the map describes a DIFFERENT kernel"*.
**Do not commit on the soak's checkout while it runs**; work in another
worktree. Its two real findings were `buildmatrix` (§3.6) and `deskflash`,
below.

**The second run, on `fe770e8d`, was 240 of 246 green.** The six:
`fdlgdrop`, `dispclose-small` and `fdlgchsmall` were §3.7, fixed, and green
after it; `dispreboot` fingerprints `ui_task`'s step 0 byte for byte and
watches `ui_cmd`'s entry, and the shell agent had shrunk the first (one
`xchg`) and dropped the `jz` in front of the second (`ui_cmd` ignores AX = 0
itself) - the row now reads the ten-byte head and lets an AX = 0 entry
through, and is green in both of its arms; `regrowshed` and `mediadisk` are
red at the base with the same output (`mediadisk` expects `media360.img`'s root
to be `MEDIA`/`SYSTEM` and MIDIRack ships there now).

**`deskflash` is intermittent, at the base and at the close, with the same
signature.** Its CGA leg read *"3 px changed, 3 flashed"* (in-place repaint)
in two of five runs on the merged tree, and the base `833f13e4` read
*"back24: ... 3 flashed"* in one of five. Three flashed pixels at both points
and five samples each cannot tell the two rates apart, so it is filed as
pre-existing; `tools/os88bisect.py classify deskflash` is the instrument if
anybody wants the rates (docs/plans/SOAK-PARALLEL.md 10: N=1 is not a rate).

## 5. WHAT IS LEFT

* **`rect_get`/`rect_put` at the 14 damage-repaint and save-under sites: -138
  on both kernels, built and backed out.** LAST-DROP-BYTES 7.10 records these
  as held by the owner (+1,088 cycles a damage repaint, 0.16% of a window
  close; the save-under set ~0.37% of a close, ~0.2% of a raise). The converted
  version is `146f7878` in this branch's history; `kent_bp` at the seven
  save-under prologues (25 bytes, ~1,100 cycles a cached restore) is in the
  same position. **This is the largest single item left, and it is the owner's
  call.**
* **The module bss assertion wants a general fix.** `mod_need` claims the
  FILE's size rounded to a KB, so a module whose image shrinks under a KB
  boundary loses its bss slack. Two knobs now pad around it (`DOSRMARK=1`,
  `ANIMOFF=1`); the honest fix is the claim counting `image + bss`, a few
  resident bytes, or a build-time pad that is applied whenever the assertion
  would fire.
* **A row for the DMA bounce.** Pass 9 asked for one and the disk agent built
  the harness (`ksp10/disk.md`, cross-file 1): ~30 s on MartyPC.
* **`tests/dskwstage.py`** sets breakpoints on `dskw_wdata.stg` and
  `dskw_rdata.stg`, now `equ` aliases of `dskw_xclus.stg`; point it there and
  drop the aliases (-0 bytes, a tidier map).
* **About 12 bytes of relaxed jumps on error paths** (`dsk_xfer`'s `je .fail`,
  `dskw_rbody`'s `jne .czbad`, `dskw_read_at_x`'s `jz .badarg`/`jc .err`), and
  `sbl_isr`'s two relaxed jumps in `SOUND.DRV` (-1 byte, but ~24 cycles a block
  IRQ: worth taking as a SPEED change).
* **`tools/stkbalance.py` does not understand `lea sp, [bp+N]`**; once it
  does, `gfx_blit1_x`'s `.noswap` can take that form (-2, faster).
* **A clipped XOR fill on VGA spends the deferred cursor hide** (gfx, noted
  and not changed): `gfx_xor_fill_raw` falls into `vga_xor_fill_vram`'s `call
  cur_unlazy`, where the 1bpp arm does not. Harmless - one extra hide - but
  SPEC.md 7.1.4's owner should say whether it is meant.
* `.lowbss` (34 bytes of rung slack) and `.vgabuf` were again left alone: the
  task stacks are sized from measurement.

## 6. METHOD LESSONS

* **Eleven agents on four cores worked**, at a load average of 24-35 for two
  hours. Builds were slow and no row failed for it, because the waits hang off
  the guest clock (SOAK-PARALLEL).
* **Notes written as decided survived and are the record.** Every agent kept
  `docs/plans/ksp10/<name>.md` committed with each batch; they are
  `docs/plans/completed/ksp10/` now. The cost is one `docs/INDEX.md` conflict
  per merge (the index lists every tracked doc): take either side and re-run
  `tools/os88index.py`.
* **A cross-file list plus a follow-up agent on the merged tree** collected the
  savings that file ownership had deliberately left on the table (-63), and the
  one dependency the coordinator had to police by hand was a register contract
  across two agents (`inst_vol_enter` dropping banks on the strength of
  `dsk_chdir_q`'s, while the disk agent was editing it). Name such contracts to
  both agents when one of them relies on the other's routine.
* **The soak is not frozen** (§4.1).
* **Pre-populating a worktree's `build/` saves little and has a trap**: a copied
  `build/` touched to a time in the FUTURE makes every later source edit look
  older than its target, and `make` does nothing. Touch to now, or just build.
