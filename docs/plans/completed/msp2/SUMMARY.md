# kern_mega size pass 2: the coordinator's record

Branch `mega-size-2`, cut from `heap-size` at `3d5acee7`; the brief is
`BRIEF.md` (`ead16c61`), and each agent's note beside it is the authority on
its own items. This file is the merged figure, what the merge itself
changed, what ran, and what is waiting on the owner. The ABI breaks every
agent found are folded into `docs/plans/ABI-BREAK-LIST.md` section 10.

## BASE AND TIP (assembled, `tools/kernsize.py --json`)

| kern_mega | base `ead16c61` | tip | delta |
|---|---:|---:|---:|
| `.text` | 46,579 | 46,182 | **-397** |
| `.bss` | 5,427 | 5,428 | +1 |
| `.lowbss` | 6,751 | 5,569 | **-1,182** |
| `.vgabuf` | 336 | 336 | 0 |
| **conventional, after the switch** | **59,093** | **57,515** | **-1,578** |
| `.cold` (above 1MB) | 46,165 | 45,866 | -299 |
| `.mega` (above 1MB) + scratch | 21,440 + 1,490 | 21,408 + 1,510 | -32 (+20) |
| `.boot2` (given back) | 6,709 | 6,739 | +30 |
| `.ovl` | 5,118 | 5,118 | 0 |
| **resident after the switch** (`MEGA_RESIDENT`) | **65,024** | **62,976** | **-2,048** (5,120 spare of 68,096) |
| **`KERN_SIZE` at boot** | **134,656** | **132,096** | **-2,560** |
| `.text`+`.bss` window | 52,006 | 51,610 | 13,926 left; `tests/megaseg.txt` 3,627 -> 3,231 |

Module images on kern_mega (modcsdo, plus megados/pmmem in HIBER.DRV):
CTRL -136, FORMAT -119, DISKOPS -346 (and the coordinator's door callers),
HIBER -219, STIKMENU -263 (one 512-byte granule of claim), WIN95 -35,
SERIAL -21, DOCK -7, EXTD -7.

**kern_big, kern_small and kern_emu: byte-identical** (every agent checked
its own at the base's build number; the merged tree's `kernsize --json` for
kern_big equals the base's in every field, and `make small` / `make emu`
build).

Where it came from, by agent (each note has the itemised list): **pmmem**
-1,184 `.lowbss` - the pool's block table, `mem_sel`, the window and
upper-segment tables moved into a tail of the tables' block above 1MB behind
`MEGA_SEL_MXT`, which also fixed `mega_wtab` never being zeroed; **megados**
-134 `.text` (-126 after the merge fix below), -160 `.mega`; **pmcore** -58
`.text`, -77 `.cold`, -48 `.mega`; **shared** -27 `.text`, -259 `.cold`
(one `.cold` rung of `KERN_SIZE`), 15 door callers direct, `DIRW1=1
KERN_MEGA=1` assembling again; **bootonce** the switch gate's side-effect-free
half in the blob (`mega_gateb`), -18 `.text`, -41 `.cold`, one more `.cold`
rung; **pmfd** -12 `.text`, -160 `.mega`, the floppy's time-out as one
longjmp (~75 clocks a transfer faster); **modcsdo** -1,029 bytes of module
images (CSDO runs); **abilist** `docs/plans/ABI-BREAK-LIST.md`.

(The tip is after the owner's answers: `mega_parkcap` gated and bootonce2's
refusal strings moved into `.mega`, -124 `.text`.)

## WHAT THE MERGE ITSELF CHANGED

* **Four doors deleted** once their last caller moved: `mega_irqromf`
  (megados's caller), `mega_slidef`, `mega_oddsegf`, `mega_s2segf` (the last
  six callers in `compress.inc`, `diskops.inc` and `mega.inc` name the
  `.cold` bodies) - -24 `.text`; pmmem deleted `mega_lincopyf` (-6).
* **The native floppy's reset probe is LIVE again** (pmfd's DEFECT; the owner
  deferred the call): `mega_fdgo`'s `mm_fdrprobe` had returned at its own
  `[mega_fdbusy]` test on every transfer, so only `mega_r286`'s real-mode
  probe ran and a 386 had none. The call is in `mega_fd13` now, under the
  switch lock and before the flag: 0 bytes, one SENSE INTERRUPT STATUS a
  native transfer.
* **`mega_down`'s 286 drop keeps its own `int 3`** in the reset body megados
  shared with `mega_svc_lo.r286`: one fault address for both made
  `megarmb286`'s per-bracket reset count unreadable. +8 `.text`.
* **Tests**: `megafdnat --break` now plants `mm_fdtmo`'s own shape (an
  unconditional `jmp mega_fdlj` longjmped out of the live probe to a stale
  SP); `megarmbref`, `vidrmb` and `vidrmbsnd` read the API table's length
  off the map (their `0x04D9` predated `OSAPI_MEM_P`).

## ROWS RUN (once, after the last merge)

* **Bochs/QEMU, the union every agent named - 122 rows**:
  `os88soak.py start` with every name as `-k`: **119 ok, 3 FAIL**.
  `megarmbref` was the stale table length above, ok after the fix.
  **`mega-scsize` and `mega-pkgmax` fail identically at `ead16c61`**
  (`os88bisect classify --base ead16c61 -n 2`: BAD 2/2 on the same legs) -
  pre-existing from the heap granule work on `heap-size`, not this pass:
  Scribe's picture claim is 2KB where the row wants 1.5KB, and a 64KB
  package region on kern_mega is claimed as 4,064 paragraphs (`I_SIZE`
  0xFE00, selector limit 65,023) where the row wants 4,096 - **the second
  looks like a real defect** (a 64KB region rounded DOWN by a granule)
  for whoever owns that branch.
* **86Box, the 286 reset route**: `megafdnat` (green, and `--break` red as
  documented: 8 fallbacks, 49 resets, the copy byte for byte through the
  BIOS), `megarmb286`, `vidrmb286`, `vidrmbsnd286`, `hma286`, `megacdrom`,
  `vidmegaclaims`, `mega-vidcard`, `mega-midiracksb`, `mega-thm95-86box`,
  and from the union `mega-dosirq`, `mega-dockmark`, `megacz`,
  `megacurdisk`, `megarmbext`: all ok. `megaaltrdg` SKIPs (no `rgbds`).
* **Fast tier** in `make`: 69 passed, 1 skipped.
* **By hand, 86Box `286-victory`**: Ctrl-Alt-Del at the desktop (pmcore's
  `mega_k09` change) did NOT warm-reboot in 60 emulated seconds - and **the
  base kernel does not either**, same script, so nothing here changed it;
  whether the harness's `ctrl+alt+del` tap arrives as a chord there was not
  established.
* **Not run** (no row exists, named by bootonce): a 286 with little or no
  extended memory (the tables-in-conventional arm) and a forced
  protected-mode refusal.

## WANTS THE OWNER

1. **`mega_parkcap`** (megados): a park-size cap only a debugger writes and
   no row names. **Owner: gate it** - it is `MEGADIAG=1` only now: -2
   `.text` and ~9 bytes of `.mega` content (inside the section's padding);
   `tests/megaseg.txt` 3,357.
2. **The refusal toast staged from the blob** (bootonce): -117 conventional
   `.text` (the four reason strings); the visible difference is that the
   refusal toast is raised before `drv_notice_x`, so a Control Panel toast
   on the same boot would replace it.
   **TAKEN** (owner: yes; `msp2-bootonce2`), by a route with no visible
   difference at all: three of the four reasons are decided at the switch,
   after the blob is gone, so the lines moved into `.mega`, whose RUNG is
   still conventional when `mega_enterf` refuses and is freed only after
   `toast_show` has copied the line (`; ovlchk: rung data`, offsets only).
   `.text` -124, `.mega` +128, `.cold` +3, `MEGA_RESIDENT` -512. Verified
   on Bochs: a refusal forced by poking `[mega_gwhy]` at `mega_enterf` ends
   on a real-mode desktop with the toast in the menu bar (bootonce.md TAKEN
   4). It also found that every refusal line is over the 24-character cap
   and always was (bootonce.md DEFECTS FOUND) - a rewording is the owner's.
3. **A near door in every module** (modcsdo): -611 bytes of images (heap
   while loaded) for +433 bytes of packed files on the system disk; modcsdo
   chose the disk **Owner: the smaller disk files, as built.**
4. **`mega_k09`'s function-key ladders as a table** (pmcore, refused): ~-22
   `.mega` for ~5 cycles on every function-key interrupt.

## WATCH

* kern_mega's blob has 230 bytes spare shipped, but **30 under
  `BOOTMARK=1 MEGADIAG=1`**, a pair no build-matrix row assembles.
* A hibernate image from a kernel before this pass is refused at resume
  (run 0 grew by the tables' tail) - the cross-build rule, as designed.
* **Every kern_mega refusal toast is TRUNCATED** (bootonce2, on the base
  too): the toast holds 24 characters and the four lines are 25-41, so the
  no-room one reads `real mode: no room for k`. `tests/unit/t_toast.py`
  could not see them. **TAKEN on the owner's word**, as bootonce2 worded
  them: `No PM: 8086-class CPU`, `No PM: no table room`, `No PM: A20 will
  not open`, `No PM: an older task`; MEGADIAG's `native floppy off: BIOS
  from now` (32) is `Floppy: BIOS from now`. **Why the gate missed them**:
  its string pattern was `label: db` and these are `label db` - nasm makes
  the colon optional. With it optional the gate finds exactly six, all in
  `mega.inc` (the five above less `mega_t86`, plus two E9-only lines now
  registered in `tests/toastlong.txt`); nothing else in the tree is over
  24. It is a FAST-tier row and takes 1.7 s. **Its remaining blind spot**:
  a string that reaches the toast through a memory word - `mega_t86` is
  stored into `[mega_gwhy]` by the blob and loaded from there - is found by
  neither pattern.
* **The rerun after the owner's answers** (the Bochs harness's `_HOME`
  probe moved, so every row): 131 rows, 128 ok; `mega-scsize` and
  `mega-pkgmax` as before (pre-existing), `megamod` timed out a 60 s wait
  under the width-4 lane and passed alone (273 s).
