# msp2 agent "shared" - kern_mega in the kern_big files

kern_mega size pass 2 (`docs/plans/completed/msp2/BRIEF.md`). Scope (BRIEF
0.2, row `shared`): every `%ifdef KERN_MEGA` hunk in the kern_big files and
`kernel/kernel.asm`, less modcsdo's CSDO/KSEG sites and bootonce's boot-once
routines and kmain's order; the `?macro` growth. Branch `msp2-shared`, base
`ead16c61`. Every figure is ASSEMBLED (`tools/kernsize.py --build build/megak
--json`, the two maps); conventional resident is `.text` + `.bss` + `.lowbss`
+ `.vgabuf` (BRIEF 0.1).

## BASE AND TIP

| kern_mega | base `ead16c61` | tip | delta |
|---|---:|---:|---:|
| `.text` | 46,579 | 46,552 | **-27** |
| `.bss` | 5,427 | 5,427 | 0 |
| `.lowbss` | 6,751 | 6,751 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| **conventional resident** | **59,093** | **59,066** | **-27** |
| `.cold` (above 1MB) | 46,165 | 45,906 | **-259** |
| `.mega` (above 1MB, 16-byte steps) | 21,440 | 21,440 | 0 (+~20 of content inside the step) |
| `.mega` scratch | 1,490 | 1,490 | 0 |
| `.boot2` / `.ovl` | 6,709 / 5,118 | 6,709 / 5,118 | 0 / 0 |
| **`KERN_SIZE` at boot** | **134,656** | **134,144** | **-512** (the `.cold` rung) |
| footprint after the switch (`mres`) | 65,024 | 65,024 | 0 |
| `.text`+`.bss` window overhead (`os88megaseg`) | 3,627 | 3,600 | -27; `tests/megaseg.txt` left at 3,627 for the coordinator to set after the merge |
| `CTRL.DRV` / `HIBER.DRV` images (unpacked) | | | -2 / -2 |

**kern_big, kern_small and kern_emu: BYTE-IDENTICAL.** Checked at the tip by
assembling the base's `kernel/` (`git archive ead16c61`) and the tip's with the
same generated includes - so the same build number - for all four kernels:
three `cmp` identical. **Re-checked after `git merge mega-size-2`** (pmcore,
megados, pmmem): against that branch's `kernel/` the same four-way assembly
gives kern_big, kern_small and kern_emu identical and kern_mega `.text` -27,
`.cold` -259, `.mega` +16 (one 16-byte step), `.modc` (CTRL.DRV) -2, `.modh`
(HIBER.DRV) -2 - this branch's own work, unchanged by the merge. The one change outside
`%ifdef KERN_MEGA` (`disk.inc`'s `DIRW_ONE` arm) also assembles kern_big's
`DIRW1=1` kernel byte-identically.

**Hot paths**: no hot routine's code changed. Every hot routine on kern_mega
(t_hotpar's kern_big list plus `mega_t08`, `mega_k09`, `mega_i16`,
`mega_gpfix`, `mega_gpw`, `mega_null0`, `mega_lincopy`, `mx_lcopy`, `mega_svc`,
`mega_fd13`, `dsk_copy_seg_x`) is at an EVEN shift against the base map:
`.text` ones -4 to -12, `.cold` ones -124, `.mega` ones 0 (the same against
mega-size-2's map after the merge). Two hot-ish
routines were made FASTER: `kbm_shf` (on every keyboard IRQ and UI pass) and
`kbd_ovflow` lose a `xor ax, ax`.

## TAKEN

1. **Dead zeroes before `MEGA_SEG0`: -12 `.text`, -2 `CTRL.DRV`, -2
   `HIBER.DRV`.** Seven sites computed `xor ax, ax` (or `bx`) only for the
   `mov es, ax` that kern_mega's `MEGA_SEG0` replaces with a memory load:
   `mouse_unhook` (twice: the port loop and the theft check, AX written or
   popped before any read), `kbm_shf` (AH = 0 was in its header, and none of
   its three callers reads AH - the header now says so for kern_mega),
   `kbd_ovflow`, `sched_stop`, `mem_region_reloc` (`mem_rr_walk` starts with a
   `lodsw`), `VGC_FORCE` (CTRL.DRV, AX reloaded before read) and `hbm_wake`
   (HIBER.DRV, BX loaded straight after). Faster as well as shorter.
2. **The door callers name the bodies: 0 bytes, ~50 cycles a call.** Fifteen
   callers in this agent's files of `mega_s2segf`, `mega_slidef`,
   `mega_oddsegf` (pmcore's `.text` doors, `call COLD_SEG:...c / retf`) and
   `mega_lincopyf` (pmmem's) now name the body: `call COLD_SEG:mega_*c` from
   `.cold` (lz, loader, diskw), `.mega` (fsx x3), CTRL.DRV and HIBER.DRV; the
   `KPARA` macro (kernel.asm) takes the body too, which moves modcsdo's seven
   KPARA sites in DISKOPS.DRV with it at no byte; and `fsx_rsnddn`'s
   `mega_lincopyf` is a near `call mx_lcopy` inside `.mega` (-2). `.cold` may
   not `push cs / call` a retf body (SPEC.md 2.6 rule 2, ovlchk), so the
   `.cold` sites stay 5 bytes. **The doors are deletable once every caller
   has moved** - see CROSS-FILE.
3. **`MEGA_ISHI` in `.cold` as a near call: ~-170 `.cold`.** The macro is 17
   bytes inline (`push ax / mov ax, x / cmp [mega_on] / stc / je / call far /
   pop ax`) at fourteen `.cold` sites in memory, loader, driver and mod
   (bootonce's `mem_hicaches_f` keeps its two - see below).
   `mem_ishidx` (every register kept) and `mem_ishiax` (AX clobbered, as
   `mega_ishif`'s contract) in `memory.inc`'s `.cold` kern_mega block make a
   `dx` site 3 bytes and an `ax`/memory site 5-8. Same CF, same registers.
   Claim/free/regrow/load paths only; +~25 cycles a call on a 286.
4. **`MEM_MP`'s three steps as near calls: ~-33 `.cold`.** The macro was 12
   bytes inline at eight sites; `mem_mpfree`/`mem_mpmove`/`mem_mpsync` hold the
   `[mega_on]` test and the far call, and the macro is now `call %1`.
5. **`MEGA_PCALLC` for three `.cold` MEGA_PCALLs: ~-39 `.cold`.**
   `MEGA_CSELTOP` is 25 bytes inline; `.cold` sites (the package entry in
   `ld_run_body_x`, and
   `drv_svc_call_x`/`drv_blk_call`'s driver calls) take `MEGA_PCALLC`, whose
   CSELTOP is the near `mem_cselc` reading the word at `[bp+4]`. The frame the
   callee sees and every register are unchanged. **Cycles**: +~18 on a 286 per
   driver verb call (`drv_svc_call_x` is OSAPI_DRV_CALL's body, not a cell)
   against the call's own selector loads and `retf`. `MEGA_CSELTOP` itself is
   unchanged, because os88ovlchk reads a macro body without evaluating its
   `%ifidni` and so faulted the `.text` site in driver.inc when the arm was
   section-dependent.
6. **`KDATAR_C ax` as `mem_kdatac`: ~-7 `.cold`.** Two sites in memory.inc
   (`mem_reloc_call`) through a `KDATAR_CA` macro
   (the plain `KDATAR_C ax` on every other kernel). `dsk_copy_seg_x`'s site is
   left inline: it is the hottest routine in the disk family.
7. **`fsx_rprepf`'s MEGA_ISHI loses its real-mode test: -8 `.mega`.** `.mega`
   runs in protected mode only, so `cmp [mega_on] / stc / je` is dead there;
   the site far-calls `mega_ishif` directly.
8. **The speaker door's protected-mode tail into `.mega`: -11 `.text`, +11
   `.mega`.** `osapi_fsx_spk`'s PM open loaded `[spk_seg]` around the far call
   and then set CX:DX = `KERNEL_SEG:sch_isr`; `spk_gate0f` (its one caller) now
   does both. A slow path (once per bracket).
9. **`fsx_run`'s region copy down: -4 `.text`, +5 `.mega`.** `fsx_rcopydf`
   (`pusha / xor ax, ax / jmp fsx_rcopyf.go`) is the AL = 0 entry with AX
   kept, so the arm in `fsx_run` loses `push ax / xor ax, ax / pop ax`.
10. **`DIRW1=1 KERN_MEGA=1` assembles** (msp1 DEFERRED 9): `dsk_rah_want`'s
    `DIRW_ONE` arm was `jmp short .out`, past reach on kern_mega; a plain `jmp`
    lets nasm pick. kern_big's DIRW1 kernel and every shipped kernel are
    byte-identical.

Per-item `.cold` figures are read off the routine sizes; the section total
(-259 = items 3-6 plus the helpers) is the assembled one. **bootonce's two
routines are left exactly as they were**: an early commit substituted the
helpers into `mem_hicaches_f` (two MEGA_ISHI) and `mem_hiclaims` (a
MEGA_PCALL and two KDATAR_C) and the last code commit reverted that, so
bootonce can move them without a conflict - they are worth ~-80 `.cold`
more wherever they land (CROSS-FILE).

## REFUSED

* **A section-aware `KPARA`/`MEGA_CSELTOP` macro** (near call in `.cold`, far
  elsewhere): os88ovlchk expands macro bodies without evaluating `%ifidni`,
  so it reported a near cross-segment call at every non-`.cold` site. A
  separate macro (`MEGA_PCALLC`) or a direct call at the site is the shape
  that passes.
* **`push cs / call mega_s2segc` from `.cold`** (-1 a site): SPEC.md 2.6 rule
  2 - `.cold` never assumes CS - and ovlchk refuses it.
* **`mmf_osapi_mem_free`'s bridge free sharing `.conv`'s tail**: -7 kern_mega
  but +4 kern_big for a near-call wrapper; not worth a kern_big change.
* **`osapi_snd_stream`'s ring test reordered into `.mega`** (-9 `.text`): the
  `[mega_on]` test has to come first, which turns the two register tests into
  a far call on every stream verb in protected mode - an API body's cost for
  nine bytes.

## DEFERRED

Estimates are labelled; nothing here was assembled unless it says so.

1. **`cmp byte [mega_on], 0` / `[ss:mega_pm]` as a call in `.cold`**: 17 sites
   in memory.inc's `.cold`, 4 in disk, 2 driver, 2 mod, 1 loader. A
   `mem_ispm: cmp ... / ret` (ZF out, ret leaves it) makes each 3 bytes
   against 5-6: est. -40 `.cold`, +~25 cycles a site. Above-1MB bytes only.
2. **`MEM_VAL bx` (3 sites) and `[mega_rhval]`**: a `mem_valbx` helper, est.
   -4 `.cold`. And `mem_val`'s `div` by MC_SIZE inside `mem_find_own`'s walk
   (a divide per record per free/claim in PM) - a `mem_sel` laid out by record
   offset would make it a shift; needs pmmem's `mem_sel` move first.
3. **`drv_fs_hi`'s 21 bytes of `.bss`** (msp1 DEFERRED 3, still open): a BP
   frame would return 22 conventional `.bss` and est. ~-40 `.cold` (`[bp+d8]`
   against `[abs16]` at ~45 accesses), but adds 22 bytes to the UI task's
   stack across a redirector driver's verb - wants `make stkdiag`'s reading on
   kern_mega first.
4. **driver.inc's three KSEG sites in `.modc` (CTRL.DRV)** (msp1 DEFERRED 2):
   DISKOPS.DRV's helper rows (kernel.asm `KSG_ROW`) for `.modc`, est. -15 of
   the CTRL.DRV image.
5. **`MEGA_PCALL` in `.text`** (`wm_pkgcall`, `sch_rhook`, 37 bytes each): a
   `.text` near CSELTOP would be -40 `.text` but both are hot (every package
   callback, every rate-hook period) - only with a cycle count that says the
   near call is free next to the selector loads.
6. **More `.cold` macro bodies**: `DSKW_LD` (2 sites, ~20 bytes each) and
   `KDATAR_C` at `dsk_copy_seg_x` (hot - leave); `KSEG` outside `.modl` is
   modcsdo's.
7. **`font_rn_lem`/`font_rn_sendm`** (msp1 DEFERRED 7, 16 `.lowbss` + 19
   `.text`): HOT (font_run), not attempted.
8. **The doors once their callers have moved**: `mega_s2segf`, `mega_slidef`,
   `mega_oddsegf` (6 `.text` each, pmcore's), `mega_lincopyf` (pmmem's) - see
   CROSS-FILE. Up to -24 conventional `.text` when all agents' callers land.
9. **`tests/megaseg.txt`** stands at 3,627 against a measured 3,600 here; the
   ratchet is the coordinator's to lower after the merge, every agent moving
   it.

## WANTS THE OWNER

Nothing taken changes what a user sees.

## WANTS AN ABI BREAK

1. **`OSAPI_FSX_SPK`'s two OPEN verbs** (AL = 0 real mode, AL = 2 protected
   mode, SPEC.md 34.11.1.1): one open on every kernel where the DOOR writes
   IRQ0's vector (as AL = 2 does) would delete the caller-writes-the-vector
   contract from `apps/os88spk.inc` and both arms from `osapi_fsx_spk`. Breaks
   every package using the speaker library (rebuild). Est. ~-15 `.text` on
   kern_mega, a few bytes on kern_big, and the library's real-mode vector
   write in each package; mostly a cleaner contract.
2. **`FSXF_REAL`'s answer in the caller's SI** (SPEC.md 53.2.3): `fsx_run`
   rewrites the `kent_di` frame to return it (`fsx_rwhy`, ~15 `.text` on
   kern_mega). An answer register the door already returns in (or a separate
   query slot) would make it a plain store. Breaks the packages that set
   FSXF_REAL (the video player's PM arms). Est. -10 `.text` kern_mega.
3. **Claims named by segment VALUE**: OSAPI_MEM_FREE, REGROW, MOVABLE and the
   heap's P cell each ask `MEGA_ISHI` whether DX is a pool selector and fork,
   because the ABI hands a claim's base back as its name. A handle-keyed claim
   ABI would delete the dual paths (`mmf_osapi_mem_free` +48,
   `osapi_mem_regrow_x` +97, `osapi_mem_movable_x` +42, `osapi_mem_p` +88,
   `mem_free_x` +47 on kern_mega). Breaks every package that claims memory.
   Est. several hundred `.cold` bytes on kern_mega; a far larger design
   question than a size pass.

## CROSS-FILE

* **pmcore** (`mega.inc` 1-5019): with this branch, the callers of
  `mega_s2segf`, `mega_slidef` and `mega_oddsegf` left are compress.inc (2
  slide, 1 oddseg - modcsdo), diskops.inc (1 s2seg, 1 slide - modcsdo's
  `.modl`) and pmfd's `mega.inc:5232` (s2seg). When those name
  `COLD_SEG:mega_*c` too, the three doors go: -18 `.text`. And
  `mega_ivt_syncf`'s two memory.inc callers are `.text`/`.cold` far calls to a
  `.text` door that tests `k_dsel` - unchanged here.
* **pmmem / coordinator**: **`mega_lincopyf` has NO caller left** in the
  merged tree - fsx.inc's (`fsx_rcp1`, in `.mega`, not an mx_tab routine) is
  a near `call mx_lcopy` here and megados converted its nine - so the door
  can be deleted: -6 `.text`.
* **modcsdo**: `KPARA` (kernel.asm) now far-calls `mega_s2segc` directly, so
  its seven `.modl` sites (compress 4, diskops 3) changed with no byte moved;
  nothing to do, but a conflict there resolves to this branch's macro.
* **bootonce**: `mem_hicaches_f` (257) and `mem_hiclaims` (215) are the two
  largest kern_mega-only routines in memory.inc and are yours; untouched (an
  early commit here edited them and the last reverted it). Wherever they
  land in `.cold`, `call mem_ishidx`/`mem_ishiax` for their two MEGA_ISHI,
  `MEGA_PCALLC` for the PKG_DISP call and `KDATAR_CA` for the two
  KDATAR_C ax are ~-80 `.cold` (the helpers are this branch's, memory.inc).
* **pmmem (mx_tab / mem_sel above 1MB)**: no mx_tab, MX_* or mem_sel access
  site was changed here. Two are NEAR this branch's lines: the helpers
  `mem_ishidx` ... `mem_mpfree` are appended directly after `mx_shedf`'s
  `retf` in memory.inc's `.cold` kern_mega block (a merge conflict there
  keeps both), and `mem_val` (reads `mem_sel`) is untouched but is called by
  `MEM_VAL`, whose sites are unchanged.
* **megados**: its suggestion that hiber.inc's 22 bytes of `mega_hbxl` ..
  `mega_hbf` scratch leave `.text` was not taken here - the cells are
  megados.inc's and HIBER.DRV names them through DS (msp1 DEFERRED 4), so
  both halves move together; left for megados or pass 3.

## ROWS TO RUN

Named, not run (the brief's rule). Bochs rows from `tests/megaport.txt`
(`mega-<row>`) and kern_mega rows from `tests/suite.py`:

* the heap helpers (items 3-6): `megacaps`, `megaxmove`, `megashed`,
  `megahuge`, `megaown`, `megarehome`, `megahigh`, `megatmmem`,
  `mega-heapcheck`, `mega-heaphi`, `mega-heapdrv`, `mega-regmove`,
  `mega-drvmove`, `mega-sndmove`, `mega-rehomemove`, `mega-rehome`;
* the package/driver/module loads (items 2, 3, 5): `megapkg`, `megamod`,
  `mega-pkgrun`, `mega-lzload`, `mega-lzmod`, `mega-lzdrv`, `mega-lzfile`,
  `mega-drvcall`, `mega-ethernet`, `mega-httpmount`, `mega-bigvol`;
* the real-mode bracket (items 2, 7, 9): `megarmb`, `megarmbext`,
  `megarmbref`, `megarsq`, and **`megarmb286`** for the 286 route of the same
  bracket (86Box - the drop itself was not changed, only the copy before it);
* the speaker door (item 8): `mega-ringmove-tracker`, `megavidring`;
* the dead zeroes (item 1): `megakbtrip`, `megapmmouse`, `mega-kbdburst`,
  `mega-ps2mouse`, `mega-btncp` (CTRL.DRV), `megahiber`, `mega-stikhiber`;
* host only, already green in `make`: `hotpar`, `megaseg`.

## DEFECTS FOUND

* `DIRW1=1 KERN_MEGA=1` did not assemble (fixed, TAKEN 10).
* `docs/INDEX.md` was stale at the base (the brief's own file), so `make`'s
  doc gate failed on a fresh tree; regenerated in the first commit.
* `t_hotpar` checks kern_big and kern_small only; kern_mega's hot parity is
  checked by hand (this note's method, a map diff). A kern_mega row in
  `tests/unit/t_hotpar.py` would make it a gate.
