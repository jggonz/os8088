# ksp14 agent "nopin" - the NO-PINNING work's kernel hunks

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`). Concept: the
NO-PINNING work (`docs/plans/NO-PINNING-PLAN.md` is its design record, SPEC.md
66.x the contract), which landed on the pass's branch after the pass began and
had not had a size pass: `8c209a11` (K8 only the UI task compacts or sheds,
K1a a DMA pin is the driver's, K2 a mounted image's run table moves),
`5dd8d976` (the adversarial review), `e598b802` (K9 a worker's call into an
image is counted, not stacked) and `4bc6dbc5` (`fmv_icorefs`). `f953b6cf`
(`vid_depth_set`) is not this concept's and was not touched.
`git diff 2d3de3ac 7bffaddc -- kernel/` is exactly what the concept changed.
Branch `ksp14-nopin`, base `7bffaddc`. Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`. Clocks are 8088 (EA 6 direct, 9 `[bx+disp16]`, 5
`[bx]`; a word memory operand +4 a transfer; `jcc` 4 not taken, 16 taken).

## BASE AND TIP

| | base `7bffaddc` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 91,884 | **91,867** | **-17** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,080 / 5,396 / 37,714 / 5,358 / 336 | 43,078 / 5,394 / 37,701 / 5,358 / 336 | -2 / -2 / -13 / 0 / 0 |
| kern_big `.boot2` / `.ovl` | 6,377 / 5,114 | 6,377 / 5,114 | 0 |
| **kern_small resident** | 58,295 | **58,221** | **-74** (`.text` 30,830 -> 30,820, `.bss` 2,987 -> 2,969, `.cold` 21,610 -> 21,564) |
| **kern_emu resident** | 92,140 | **92,123** | **-17** (`.text` 43,336 -> 43,334, `.bss` -2, `.cold` -13) |
| **kern_mega resident** | 102,240 | **102,223** | **-17** (`.text` 46,809 -> 46,807, `.bss` 5,394 -> 5,392, `.cold` 42,444 -> 42,431; `.mega` 14,698 unchanged) |
| `DISKOPS.DRV` image (unpacked) | 10,132 | 10,126 | -6 (a 10 KB claim either way) |

The concept cost kern_big **+108** resident (`.text` +34, `.cold` +70, `.bss`
+4; `tools/os88rdiff.py kernel 2d3de3ac 7bffaddc`) and 0 on kern_small; it
costs **+91** now, so 16% of it came back - and kern_small, which the concept
never touched, gave 74 bytes the nest had been costing it all along. No
`.boot2`, `.ovl` or `.lowbss` byte moved; `t_hotpar` is green on both kernels.

## TAKEN

### 1. K9's count is the HIGH BYTE of the depth's word - kern_big -7 (`.cold` -5, `.bss` -2)

`[wm_pkgd]` (the UI task's nest depth) and `[wm_tcnt]` (K9's count of other
tasks' calls into an image) were two words. They are two adjacent BYTES now,
depth low, asserted where they are declared:

* **`mem_in_nest` loses its K9 test** (36 -> 29, the size it was before K9).
  `mov cx, [wm_pkgd]` / `cmp cx, WM_PKGD_MAX` / `ja .yes` reads both: a
  non-zero count makes CX >= 256, past any `WM_PKGD_MAX` (8). Equivalence:
  base pinned when the count was non-zero OR the depth > 8; tip pins when
  count*256 + depth > 8, which is the same set while the depth is under 256
  (it is a call-stack depth; past 8 both pin everything anyway). An underflow
  pins on both: base read the word as 0xFFFF, tip reads 0x00FF or 0xFFxx.
* **`wm_tcnt`'s word goes** (`.bss` -2; `wm_pkgs` keeps its alignment, the
  pair is one word). The count cannot overflow a byte: it is the calls into
  an image in flight on tasks other than the UI task, at most a couple per
  worker for twelve workers.
* **The push indexes the stack by the LOW byte alone** (`mov bl, [es:wm_pkgd]`
  with BX zeroed first) - which is the trap in this layout and the reason the
  push did not get shorter by the whole test: a push that bounded the whole
  word would skip recording a level the UI task enters while a worker's count
  stands, and once the count fell back to zero `mem_in_nest` would walk that
  slot's stale segment and could move an image a frame stands in.
  `tests/tnest.py` gained **assertion 7** for exactly that, and it was
  **verified red** with the index made a word load again (`got ('0x5a00', 0,
  0)`: the slot still held the previous step's segment).
* **The push is 44 bytes, 46 at the first cut**: `xor bx, bx` before
  `cmp [es:sch_cur], bl` (the register form, a byte shorter than the
  immediate) leaves BX = 0 on the worker's arm, so both arms share `inc byte
  [es:bx+wm_tcnt]` with the UI arm setting `BX = wm_pkgd - wm_tcnt` (-1).
* **The pop** dec's bytes: same size, cheaper.

**Cycles, the K9 package call path** (`wm_nest_pushd` after `pop ax`, and the
pop, on every far call into a package or a driver image, including
`snd_tick`'s service call inside IRQ0):

| path | base | tip |
|---|---:|---:|
| push, UI task | 121 | **106** |
| push, another task (the K9 count) | 75 | **62** |
| pop, either | `dec word` 29 | **`dec byte` 21** |
| `mem_in_nest` (each record a compaction asks about) | count test 31 + ... | **0** + ... |

Byte operands are a bus cycle cheaper on an 8088, which pays for the `xor`.
Faster on every path, so the brief's cycle-neutral bar is cleared both ways.

### 2. kern_small has no nest - kern_small -74 (`.text` -10, `.cold` -46, `.bss` -18)

**Outside the concept** (the nest predates K9), taken because it is in the
routines K9 rewrote. The nest's ONE reader is `mem_in_nest`, which is
`OS88_COMPACT` - kern_big's alone - so on kern_small `wm_nest_pushd` wrote a
stack nothing ever read. Verified on kern_small's listing: `wm_pkgd` and
`wm_pkgs` are touched by the push, the pop and `wm_init`'s zero run and by
nothing else, and no host tool reads them on that kernel (`tests/regmove.py`,
`tests/tnest.py` and the `pins` rows are kern_big's). Every caller but
`wm_pkgcall` was already kern_big-only (`driver.inc`, `xmem.inc` under
`OS88_DRIVERS`, `vmmouse.inc` kern_emu, `mega.inc` kern_mega), and
`OS88_COMPACT`, `OS88_DRIVERS` and `KERN_BIG` are one condition
(`kernel/kernel.asm`). So `wm_nest_pushd`/`wm_nest_pop`, their far entries,
`wm_pkgcall`'s two far calls and the 18 bytes of `.bss` are `OS88_COMPACT`;
`wm_init`'s run starts at a new `wm_zrun` label, which is `wm_pkgd` on
kern_big (byte-identical) and `wm_wins` on kern_small. Every package callback
on kern_small is ~500 clocks faster (the two far calls and the bodies behind
them, ~330 in and ~190 out); the callback's CF never depended on them (the
pop preserved the flags). One
never-executed **parity pad** keeps kern_small's hot `.cold` routines even
(`dsk_xfer`, `dsk_runcap`; `-46` and not `-47`).

### 3. `mem_in_xfer`: the image pin is a BASE - kern_big -3 (`.cold`)

K2 made `mem_in_xfer` walk two adjacent words, `[mem_pinseg]` and
`[mem_pinimg]`, through the same range test (41 bytes). `[mem_pinimg]` is
asked first now, by an EQUAL compare against `MC_SEG` with `stc`/`je` (38
bytes; the adjacency `%error` goes with the walk). **Equivalence**: every
writer of `[mem_pinimg]` stores a claim's own base - `dsk_xfer`'s image arm
stores DX = `[row+IMG_DVSEG]`, `clo_unmnt` the same word, `clo_umount` ES =
`clo_mfind`'s, which is that word again - and `IMG_DVSEG` is written only by
`clo_mount` (DX from `mem_claim_hi_x`, the record's `MC_SEG`) and by
`img_reloc` (DX, the new base). Claims do not overlap, so for a live record
"the pin is inside it" and "the pin is its base" are the same answer. The one
difference is a record whose `MC_SEG` is 0 with no pin standing: tip answers
"pinned" where base answered "movable" - but `MC_SEG` 0 is a FREE table slot,
which neither caller (`mem_can_move`, the walk's `.pinned` arm) is ever
handed, and the direction would be the safe one. Faster in every case: the
second trip round the loop was ~50 clocks of `inc`/`cmp`/`jb`.

### 4. `dsk_xfer`'s image arm: one exit pops the pin - kern_big -2 (`.cold`)

K2 put `pop word [mem_pinimg]` on both of the arm's exits, and pushed the
success exit's `jmp .out` out of short range (it was `jmp short` before K2:
134 bytes back). Now `.iref` sets CF (`stc`, +1), the failed piece already has
it, and the loop's foot `jmp short .ijf` with CF = 0 off its `or`; `.ijf` pops
once and `jc .fail` / `jmp .out`, both short from there. 14 bytes -> 11, plus
**one never-executed parity pad** for `dsk_runcap` and `dsk_next_clus_x`
(`t_hotpar` caught the odd shift). **Cycles: +19 on a COMPLETED image
transfer** (a short `jmp` and a `jc` not taken) and +2 on a refused one; the
arm runs once per call into an image volume and each pass of it is a whole
`int 13h` on the host (~400 ms), so this is not the per-sector path the brief
fences. Stack and CF at every exit are what they were: ES is popped before
`.ijf` on all three paths, as before.

### 5. `img_reloc` counts with `loop` - kern_big -2 (`.text`)

`mov cx, DVOL_MAX` / `loop .r` in place of `cmp si, <end>` / `jb .r` (25 ->
24 bytes, and faster), and K2's parity pad after it goes too: the routine is
even now and the 401 routines after it (`ui_task` among them) keep the shift
they had. CX is free: `mem_reloc_call` banks every register (`kentc_bp`).
The CLASS test stays - see REFUSED 1.

### 6. `fmv_icorefs` takes its zero from BX - kern_big -1 (`.cold`)

`4bc6dbc5`'s `mov byte [di+FS_DIRTY], 0` (4) is `mov [di+FS_DIRTY], bl` (3),
the `xor bx, bx` that `dsk_dest_x` wants moved up above the test. `asc_use_x`
preserves DI (the base already read `[di+FS_DIRTY]` after it) and nothing
between reads BX. kern_small has no `OS88_ASSOC`, so its copy only moved.

### 7. `DISKOPS.DRV`'s `.marked` stops clearing a pin nothing set - module -6

The review (NO-PINNING-PLAN 5, #9) removed the mount's own `[mem_pinimg]`
pin and left its release, `mov word [mem_pinimg], 0`, at `.marked`.
`[mem_pinimg]` is 0 there: the mount path never sets it; `clo_unmnt` and
`clo_umount`, the only `clo_*` writers, both leave through `clo_mdrop` ->
`clo_mfree`, which clears it; `dsk_xfer`'s image arm pushes and pops it.
10,132 -> 10,126 bytes of image, 10 KB of claim either way.

## REFUSED

1. **`img_reloc` as a `mem_rr_tab` row** (`MEM_RR_ROW dsk_vtab + IMG_DVSEG,
   DVOL_MAX, DV_SIZE`, 4 bytes for its 24, ~-20). The walk re-points every
   row's word that equals the old base, and `IMG_DVSEG` is label bytes 6-7 on
   a row that is NOT an image: `dsk_vol_add` copies a label up to its NUL and
   pads nothing, so a reused row keeps a stale `IMG_DVSEG` past a short
   label, and a 6-glyph label's NUL is byte 6 itself. An image claimed again
   at the stale segment and then moved would overwrite a live label's NUL.
   `img_reloc`'s class test is what stops it.
2. **`dsk_xfer`'s outer frame pinning `[mem_pinimg]` for every transfer**
   (push at entry, pop at `.out`; the image arm then only stores - ~-4).
   Two memory operands (~60 clocks) on EVERY transfer, which is the hot path
   the brief fences.
3. **`wm_nest_pop` through BX** (`xor bx, bx` / `cmp [sch_cur], bl` / `dec bx`
   / `dec byte [bx+wm_tcnt]`, 20 -> 18): +27 clocks on every call back out of
   a package or driver image, including inside IRQ0.
4. **`mem_in_xfer`'s pin words and the K8 gate in fewer bytes by making
   `[mem_cp_busy]` the byte below `[sch_cur]`**, so `mem_compact`'s existing
   `cmp byte [mem_cp_busy], 0` widens to a word test that gates the UI task
   for free (net ~-7 with the gate moved into `mem_shed_one`). The byte below
   `[sch_cur]` is `[ticks]`'s high byte, and inserting one shifts
   `sch_cycles` and the rest of the scheduler's words to odd addresses.
5. **Dropping `dsk_xfer`'s image-arm push/pop for a plain store and clear**:
   K2 made it NESTED on purpose, for `DISKOPS.DRV`'s unmount pin standing
   around disk calls of its own, and a store/clear is bigger anyway (4 + 6
   against 4 + 4).
6. **`inst_seg_parked`'s K9 test without its parity pad**: the test is 7
   bytes (no register is known zero at `.drv`), `ui_task` sits between it and
   `img_reloc` in `.text`, so the pad is load-bearing.

## WANTS THE OWNER

Nothing. Every item above is byte-identical behaviour.

## WANTS AN ABI BREAK

Nothing.

## CROSS-FILE (for `fold` / the general pass)

* **Every kernel-tag relocation proc of the form `cmp bx, [w]` / `jne` /
  `mov [w], dx` / `ret` can be a `mem_rr_tab` row**: `asc_reloc`
  (`assoc.inc`), `dsk_rah_reloc` and `ico_reloc` (`disk.inc`), `menu_reloc`
  (`menu.inc`) are 11 bytes each against a 4-byte `MEM_RR_ONE` (or 0 beside an
  adjacent row), `clip_reloc` and `sc_reloc` 5 each - ~-30 on kern_big, and
  the declaration then names any shared `ret`. `mem_region_reloc` already
  walks the table for EVERY move, so nothing calls them that the walk would
  not reach. **The caveat that decides each one**: the walk rewrites a word
  whatever claim it names, so each word must be zeroed whenever its claim is
  freed or shed - a stale one equal to an unrelated claim's old base would be
  re-pointed into that claim (the proc is only called for its own claim). The
  rows already there (`menu_seg`, `fdlg_rqsp`, `dskw_seg`) live under that
  rule; each candidate needs its free/shed paths read. Not taken: those
  files and procs are not this concept's.
* `mem_regrow`'s and `mem_claim_x`'s K8 gates (`cmp byte [sch_cur], 0` /
  `jne`, 7 each) have no cheaper encoding found: no register is known zero at
  either.

## ROWS RUN

One pass after the last code commit, `os88test.py soak -k ... --marty-jobs 1`:
**20 of 20 green** - `tnest`, `regpin`, `regmove`, `regwork`, `regapp`,
`drvmove`, `sndmove`, `heapcheck`, `heapdrv`, `imgmount`, `imgswap`,
`imgtest`, `imgcases`, `pkgmove-ftpd`, `pkgmove-texpad`, `pkgmove-browser`,
and the full-tier `pins`, `pinsrt`, `small128` (kern_small on its 128KB
machine, with the nest gone) and `kernresident`. None was red, so none needed
classifying against the base. Of the two rows the no-pinning commits recorded
as red at `origin/elendilon`, `imgmount` is green here and `regrowshed` was not
run (its K8 gate is untouched).

`tnest` was broken on purpose for the new assertion (above): red with the
push's index a word load, green restored. The fast tier is green at every
commit (`make` runs it; `hotpar` caught item 4's odd shift and kern_small's).
`make emu` and `make mega` build at the tip. `tools/stkbalance.py` over
`wm.inc`, `memory.inc`, `disk.inc`, `files.inc`, `instance.inc` and
`diskops.inc`: the same unbalanced-path lists as the base.

## DEFECTS FOUND

None in behaviour. The base's `mem_in_xfer` carried a `clc` its own comment
said was already true (the walk's `jb` falling through is CF = 0); it went
with the walk.
