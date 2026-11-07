# Kernel size pass 18: the record, and what is left for a nineteenth

**PASS 18 HAS LANDED** on `kernel-size-18`, cut from `elendilon` at
`aede17b1` (the brief's commit `c5a7b0f9`). The file is named `-P19` for the
reason the records before it are named one ahead: each pass's record is named
for the pass it hands to.

The brief is `docs/plans/completed/ksp18/BRIEF.md`, with the fresh-routine
ownership table beside it (`fresh-routines.md`); each agent's note is beside
them (`hot.md`, `dir.md`, `main.md`, `wmui.md`, `draw.md`, `files.md`,
`sys.md`, and the second wave's `sweep.md`, `sweep2.md`, `sweep3.md`) and is
the authority on its own items. This file is the summary, the owner's
decisions, and what is left.

## 0. THE BRIEF AND THE OUTCOME

The owner's words: *"New kernel and speed work has come in at a decent chunk
of bytes. This work is speed sensitive - it needs to not get meaningfully
slower (or re-introduce disk reads we just removed). Size pass it carefully.
Size pass anything coming in from main, and anything else we have added since
the last size pass. ... Also start a few agents on a more general pass, with
the goal to recover 512b resident on big. This pass is still not the ABI break
pass - keep recording those for now, but not taking them."*

The last sized tree was `d323a9a6` (main's #328 squash, which carried pass
17). Since then the kernel hot-paths pass (`khp1`), the 1,024-entry Disk
window, the Shell sort, the TOASTSTR sweep, WIN16 Phase B/C, WIN16-NEXT and
PCIETHER.DRV had added **+583** to kern_big.

**The goal is met: kern_big resident -775 against a goal of -512** - which
takes it 192 bytes BELOW the last sized tree, so the pass paid for every byte
added since pass 17 and then some.

Kernel resident (`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`,
plus `.mega` on kern_mega), assembled:

| kernel | last sized (`d323a9a6`) | base (`aede17b1`) | tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---:|---|
| **kern_big** | 91,184 | 91,767 | **90,992** | **-775** | 97,280 -> **96,768** |
| kern_small | 57,246 | 57,333 | 56,877 | -456 | 59,392 -> **58,880** |
| kern_emu | 91,438 | 92,011 | 91,238 | -773 | 97,792 -> **96,768** |
| kern_mega | 124,544 | 127,276 | 126,556 | -720 | conventional 63,488 -> **62,976** of 68,096 |

kern_big at the tip: `.text` 40,340, `.bss` 5,366, `.cold` 37,562,
`.lowbss` 5,358, `.vgabuf` 336, `.vga` 2,030; `.ovl` 5,117. kern_big's
`.boot2` blob is **6,652 of 6,656 at 13 sectors** (4 bytes spare - see
section 6). `tests/megaseg.txt` is 3,754 (sweep3: kern_mega keeps the
`vid_tab` rows kern_big moved into the blob).

Off the system disk: **PCIETHER.DRV -212 assembled** (17,265 -> 17,053;
first size pass, -4 of it the owner's `[eth_ntx]` fix, section 4); ETHER.DRV
-18 assembled; FILECP.DRV -15. kern_small
CTRL.DRV +40 and kern_emu CTRL.DRV/EXTD.DRV +83 each (draw's `vid_tab` copy;
no sector count or claim moved).

## 1. WHO TOOK WHAT (kern_big resident)

| agent | scope | kern_big | the shape of it |
|---|---|---:|---|
| **files** | the Disk window, dialogs, desk, assoc, icons, fsck | -203 | the ASSOC.DAT parse, `fm_paste_res`'s two passes, `fm_draw_lrow`, `fcp_selfchk` at their one call; `fm_scrollpaint`'s never-run strip pass deleted (-70, owner: keep - section 3) |
| **sys** | disk, loader, memory, mod, driver, clock, sched | -84 | dead callee-saves across `diskw.inc`; `ld_run_body_x` -10; `dsk_read_chain_x` -9; `dsk_geom_pick` at its call in `dsk_xfer` (the call ~95 cycles faster, the sector loop byte-identical) |
| **sweep3** (wave 2) | sys's files again, placement | -82 | 87 bytes out of kern_big's `.boot2` (`read_run`, the spinner, the config reader) so draw's `vid_tab` move (-72) fits WITHOUT a 14th sector; `sch_account` -7 and faster on IRQ0 |
| **dir** | the 1,024-entry Disk window, the Shell sort | -80 | `fmv_cap`/`fmv_need`/`fmv_rsz` into `fmv_load` (-25), `fmv_take` into `fmv_bcast` (-17), `dsk_sdcmp` inline (-12); the sort ~5% faster on a cycle model, bit-identical on 420 listings |
| **sweep2** (wave 2) | wmui's files again | -76 | four locking doors on one body (-7); `wm_display` on `osapi_video`'s loads (-12); banks no caller reads |
| **sweep** (wave 2) | dir's and main's routines again | -67 | `assoc_run_x`'s resweep (-8), `fm_onclick_x` -10, the `fmv_*` family; kern_mega's `mega_segty` -11 and `mega_npx` -3 (Bochs rows ran) |
| **wmui** | wm, instance, clip, events, dock, apps, ui, menu, toast, ctrl, kernel.asm | -63 | the side-border question asked once (-11); `wm_wk_rec` initialised (-6); `wm_timer` as cell 0x033A's body |
| **hot** | the khp1 speed work | -58 | `lz_fsync` shared by both decoders under KHP_LZ (-26, fast paths instruction-identical); the read-ahead cap as the fill's own ceiling (-14, same sectors read); `cur_move_mono` -7 and ~58 clocks a packet faster |
| **draw** | drawing, fonts, mouse, video | -44 | eleven banks/reloads/stores; the serial and PS/2 decoders branchless; `vid_tab`'s constant rows into the code that reads them (kern_small -36, kern_emu -72; kern_big's -72 taken by sweep3) |
| **main** | the work from main | -18 | `kbm_isr`'s self-chain test on a constant (faster per key); PCIETHER.DRV's name on ETHER.DRV's tail; PCIETHER.DRV's first size pass |

Pass 16 and 17's lesson 4 held a third time: **single-caller routines written
at their call, and callee-saves no caller reads, were the bulk of every
finder's bytes.** The second wave paid again: three sweeps took 225 of the
775, on files a first-wave agent had already passed.

## 2. SPEED: HOT PATHS, DISK READS AND PARITY

The owner's bar was *no meaningful slowdown, and no disk read the speed pass
removed coming back*. What held it:

* **No khp1 pick was reverted.** `hot` shrank the picks at their speed:
  `KHP_LZ`'s fast paths are instruction-identical (2,134 decode cases
  bit-identical under unicorn); every read-ahead fill reads exactly what it
  did; the cursor and `sch_switch` got faster.
* **No `int 13h` call was added, removed or moved by any agent.** `navcalls`
  (navigation calls counted off MartyPC's FDC) and `fdswap` passed with the
  same counts on every branch and on the merged tip.
* **Hot routines changed only to get faster**, each with a count:
  `kbm_isr` (~8 clocks a key), the serial and PS/2 decoders (at most one
  queue flush where there were two), `sch_account` (~12 clocks a tick),
  `sch_switch` (3 a slot), `cur_move_mono` (~58 a packet), `dsk_xfer`'s
  setup (~95 a call). The H3/H4 costs that were allowed are each under 0.1%
  of their operation (dir's `fm_mflip`, wmui's `and` stores, sweep2's doors).
* **Parity**: `hotpar` green on kern_big and kern_small on every branch and
  on the merged tip. Pads REMOVED: hot 3, wmui 2, sweep3 1 (sys's new one);
  pads added: sys 1 (taken back by sweep3). Net -5.
* **Measured on MartyPC, base against tip**: section 7's `os88hot ab` table.

## 3. THE OWNER'S DECISIONS THIS PASS

1. **files' strip pass: KEEP THE SAVING** (-70 on every kernel).
   *"70 bytes saved to something never run seems like a bargain, keep the
   savings."* `fm_scrollpaint`'s dead strip pass is gone; SPEC.md 22.11.1.1's
   invariant is an assembly-time `%if FM_PEN_X < 7` instead.
2. **draw's `vid_tab` 14th sector: NOT NEEDED.** draw put a 14th `.boot2`
   sector to the owner for kern_big's -72; sweep3 freed the blob's own bytes
   instead, which was the alternative draw's note named. No decision was
   asked.

## 4. WANTS THE OWNER (open)

1. Pass 17's section 3 items (one write generation per machine, -13; draw
   item 7's fixed CR11 unlock, revertible +7) and pass 16's three (shortcut
   open, its popup, the About box into modules) still stand undecided.

Decided this pass, after the record was first written (both 2026-11-07):

* **The raw-frame double count in `[eth_ntx]`: TAKEN as a bug fix.** *"That
  sounds like a bug? Changing the display to be accurate sounds like a good
  thing to do."* Both network drivers counted each raw frame twice (nic_tx,
  then `eth_v_rawtx`); the second `inc` is gone and the Control Panel's
  transmit count is now true. ETHER.DRV and PCIETHER.DRV -4 each;
  `ethernet`, `pciether`, `dospkt` green.
* **`assoc_run_x`'s `.noassoc` falling into `.fwd` (-5): DECLINED for now.**
  *"It would depend on how actually visible the change was (humans don't
  count in ms), but for 5 bytes its probably not worth chasing tonight."* The
  toast would start its timer before the volume is put back; re-propose only
  with a measurement of how much later it starts.


## 5. WANTS AN ABI BREAK

Nothing new was found. Pass 17's list (P18 handoff section 4, now in
`docs/plans/ABI-BREAK-LIST.md`) stands as priced, with two notes from wmui
folded in there (section 10.1, with pass 17's rows, which had not been copied in until now): OSAPI_WM_TIMER's kern_big target is now the body, so the
timer setter can join the window-field setter break with no wrapper to
retire; and `OSAPI_SEG`'s refusal shares its exit only on kern_big.

## 6. WHAT IS LEFT

- **kern_big's `.boot2` is 4 bytes from full** (13 sectors). The next thing
  that goes into it frees bytes first or takes a sector; sweep3.md lists where
  the remaining slack is. Note `tools/kernsize.py`'s `boot2` figure (6,478 on
  kern_big) is NOT the guard's `BOOT2_SIZE`/`boot2_end` (6,652) - draw lost a
  build to that reading.
- **kern_mega's image rung has ~1 byte left** after main's work (main.md):
  two bytes of conventional kern_mega growth cost a 512-byte step.
- **Held for parity, ready to pair** (each built, each odd alone):
  `wm_isrec` as `lea` (-1, `[wm_paint_all, ui_task)`), `wm_dmg_rebands`'
  `cbw` (-1, faster, `[sch_switch, wm_paint_all)`), `mod_free_row` (-1,
  `[gfx_blit1_x, dsk_xfer)`), `osapi_seg` (-1 `.cold`), `dsk_fatw_pick` and
  `mem_region_reloc` (-1 each, pass 17's), hot's `sch_resume` and `sch_isr
  .tres` pads, `font_width_x`'s pad, kern_small `menu_draw_bar`'s long
  `mov dx`. sweep2's `cbw` pairs with hot's `sch_resume` pad by range.
- **The Hercules scroll's placement sensitivity** (section 7): PgDn moves
  +-2.5% on MartyPC's Hercules machine with layout alone (125.0 base, 126.6
  tip, 128.3 with 72 bytes of data put back). Worth understanding before the
  next pass reads a Hercules timing as a regression or a win.
- **CROSS-FILE, priced and free**: `str_len` into `toast_place` (-3 net,
  both halves odd; sweep2.md); `dsk_copy_in_x`'s `mov ax, ds` (-1;
  sweep.md); `mega_i21` (-1, only with another `.mega` cut across its
  `align 16` - main priced three kern_mega items and sweep took the other two); the
  `hb_kinit`/`fm_kinit` thunks (~-6, needs `apps.inc` and `hiber.inc`
  together; wmui.md); WIN95.DRV's `wn_sel` redundant guards (~-10 module
  bytes, `winmod.inc`, no row owns it; sweep2.md).
- **REFUSED with a price, not to be re-derived**: `mod_needi` (12 sites,
  ~-7 net for edits in 9 files - sys.md); `sch_account` through
  `sch_currec` (+40 cycles a tick); `inst_ptr` by `mul` (-5, +15 clocks a
  package callback).

## 7. WHAT WAS RUN

Each agent ran its own rows once after its last code commit (the notes list
them; all green after files' one self-inflicted `msel` failure was fixed and
re-run). On the merged tip, the coordinator ran:

- **The deduplicated union of every agent's rows, once** (`tools/os88soak.py`,
  run `20261107-052122`, 198 rows, with Bochs, the automation-enabled 86Box
  and the Win16 fixtures all present so the `win16*` and `megacurdisk` rows
  ran): **198 ok, 0 FAIL, 0 SKIP** in 18 minutes.
- **`test-full`, once: 88 ok, 0 FAIL, 2 SKIP** (`w16censall` wants the Win16
  samples; `ctoolchain` wants the C toolchain).
- `make`, `make small`, `make emu` and `make mega` build at the tip; the fast
  tier is 73/73 with one fixture skip; `hotpar`, `canary`, `blobruns` and
  `megaseg` green.
- **`tools/os88hot.py ab --base c5a7b0f9`**, MartyPC, every scenario on CGA,
  Hercules and VGA (guest time off the cycle counter, 4.77 MHz):

Median of 3 reps unless marked; `busy` is CPU time inside the window.

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| raise a window (ms) | 163.7 -> 161.8 (-1.2%) | 209.1 -> 209.2 (0.0%) | 360.7 -> 356.9 (-1.1%) |
| drag, 8 packets (ms) | 497.0 -> 495.7 (-0.3%) | 519.0 -> 517.5 (-0.3%) | 660.0 -> 658.1 (-0.3%) |
| PgDn / PgUp (ms) | 106.2 -> 106.0 (-0.2%) / -0.2% | **125.0 -> 126.6 (+1.2%) / +1.4%** - see below | 99.4 -> 99.2 (-0.2%) / -0.2% |
| menu open (busy ms) | 31.0 -> 30.9 (-0.2%) | 30.8 -> 30.4 (-1.1%) | 35.1 -> 35.0 (-0.2%) |
| menu close (ms, n=8) | 23.2 -> 23.3 (+0.2%) | - | - |
| launch Calc, cached (ms) | 834.3 -> 832.4 (-0.2%) | 839.8 -> 838.7 (-0.1%) | 853.0 -> 851.5 (-0.2%) |
| type 31 keys (busy ms) | 499.0 -> 489.5 (-1.9%) | 441.2 -> 430.7 (-2.4%) | 520.3 -> 512.7 (-1.5%) |
| pointer sweep (busy ms) | 138.7 -> 139.3 (+0.4%) | 138.0 -> 139.4 (+1.0%) | 182.3 -> 179.5 (-1.5%) |
| idle desktop 5 s (busy ms) | 179.7 -> 177.2 (-1.4%) | -1.4% | -1.4% |
| open A:'s Disk window (ms; fastest of 3) | 1,184.3 -> 1,181.2 | med +3.8% | med -4.8% |
| launch Calc, cold (ms) | med +5.5% | - | - |
| boot to the desktop (ms) | 14,355 -> 14,559 (+1.4%) | +0.7% | +0.6% |

**Disk calls are identical everywhere**: opening A: 3 reads, 18 sectors, 1
seek at base and tip on all three adapters; boot 18 reads, 190 sectors, 24
seeks; a cached launch none. The disk-bound rows' medians move with the
platter's rotational phase (the khp1 LOG found the same; the fastest reps
agree to 0.3%).

**Boot's +100-200 ms is rotational, not code.** `tools/os88boot.py` phase
by phase (CGA, GLaBIOS): every code phase is equal or FASTER (kernel load
-33 ms, `mouse_init` -42 ms), with the same int 13h calls; the time is two
phases waiting a revolution - `drv_boot_x` +90 ms (same 3 reads, 18
sectors) and `as_boot` +189 ms. `KERNEL.SYS` is one cluster smaller
(85,776 -> 85,168), so every file after it sits one sector earlier on its
track (ASSOC.DAT LBA 182 -> 181), its read-ahead fill reads to the track's end
(4 sectors, was 3), and the head now arrives just past the next sector. Any
kernel-size change moves this by a revolution, in either direction.

**Menu close's first reading (+3.4%, n=3) was tick phase**: at n=8 it is
+0.2%.

**Hercules PgDn/PgUp +1.3% (+1.6 ms) is LAYOUT, not work - but not
explained.** Bisected on the merge chain: flat through `28d2a265` (draw) and
`750788df` (files), it appears at the sweep3 merge `1fabea63` and is flat
after. It is NOT sweep3's `vid_tab` move: a probe of the tip with kern_big's
`vid_tab` resident again (72 bytes of data read once a mode set, no
instruction on the scroll path changed) reads **128.3 ms**, slower still.
So the Hercules scroll on MartyPC moves +-2.5% with placement alone, CGA and
VGA do not, and nothing in this pass made the PgDn path do more work. The
mechanism is open - section 6.

## 8. METHOD LESSONS

- **The brief's commit left `docs/INDEX.md` stale AGAIN** - the coordinator
  ran `os88index.py` before `git add`, and the index counts tracked files.
  Every worktree's first `make` failed and every merge conflicted on it.
  **Run `os88index.py` AFTER `git add` of the new files, then `--check`,
  then commit.** This is passes 15, 16, 17 and 18 now.
- **Merging each branch the moment it landed, and building the merged tip,
  caught nothing but INDEX conflicts** - the routine-level ownership table
  (`fresh-routines.md`) kept seven first-wave agents off each other's lines.
  Name a routine's owner, not only a file's.
- **Three sweeps on freed files paid 225 bytes** - start one per landed row,
  from the merged tip, as soon as that row lands.
- **A container restart mid-pass cost only the in-flight builds and rows**:
  worktrees, branches and installed tools survived, and each agent resumed
  from its transcript after a rebuild.
- **`tools/kernsize.py`'s `boot2` is not the blob guard's figure** (draw), an
  `%error` text that names a routine makes `stkbalance` walk it (draw), and
  NASM `-f bin` silently assembles a sum of two section-relative labels to a
  wrong constant (hot). Each cost an agent a build.
