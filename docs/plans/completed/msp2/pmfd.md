# msp2 agent "pmfd" - the API stubs, the huge claims and the native floppy

kern_mega size pass 2 (`docs/plans/completed/msp2/BRIEF.md`). Range:
`kernel/mega.inc` lines 5020-8035 at the base `ead16c61` - the kern_mega
arms of the hot cells (`mega_sl_*`, `mega_fj_*`, `MEGA_ICW*`), `mega_bhex*`,
`mega_rmaskf`/`mega_vrm`, `mega_hibnc`, the huge-claim code and the native
floppy (`mega_fd*`, `MEGA_FDIO`'s two copies). Branch `msp2-pmfd`. Figures
are ASSEMBLED bytes: `tools/kernsize.py --build build/megak --json`
absolutes, and `tools/os88rdiff.py kernel <base> . -D KERN_BIG -D KERN_MEGA
-D KERN_KNOB --build build/megak` per routine.

Mid-pass the coordinator gave the `mx_tab`/`MX_*`/`mem_sel` access sites to
pmmem, so the huge-claim routines (`mega_hugef`, `mega_hugec`, `mega_hugebf`,
`mega_xmrunf`, `mx_runcntf`, `mega_mxat`) are **untouched** here: a 6-byte
`.cold` change to `mega_hugef`'s `MX_FL` stores was built and then reverted
(DEFERRED, for pmmem). Nothing in this diff touches an `MX_*` or `mem_sel`
site.

## BASE AND TIP

| | base `ead16c61` | tip | delta |
|---|---:|---:|---:|
| kern_mega `.text` | 46,579 | 46,567 | **-12** |
| kern_mega `.bss` | 5,427 | 5,428 | +1 |
| kern_mega `.lowbss` / `.vgabuf` | 6,751 / 336 | same | 0 |
| **kern_mega conventional resident** (`.text`+`.bss`+`.lowbss`+`.vgabuf`) | 59,093 | 59,082 | **-11** |
| kern_mega `.cold` | 46,165 | 46,165 | 0 |
| kern_mega `.mega` (above 1MB) | 21,440 | 21,280 | **-160** |
| kern_mega `.mega` scratch | 1,490 | 1,490 | 0 |
| kern_mega `.boot2` / `.ovl` | 6,709 / 5,118 | same | 0 |
| kern_mega `KERN_SIZE` at boot | 134,656 | 134,656 | 0 (no rung crossed) |
| kern_mega footprint after the switch (`mres`) | 65,024 | 65,024 | 0 |
| kern_mega `.text`+`.bss` segment left | 13,530 | 13,541 | +11 |
| **kern_big** | | | **every `kernsize` section identical** to `/tmp/msp2/base-big.json` |

kern_small and kern_emu do not assemble `mega.inc` (the whole file is inside
`%ifdef KERN_MEGA`) and were not rebuilt. Total: **171 bytes** of kern_mega,
11 of them conventional for ever.

## TAKEN

All in `kernel/mega.inc`; commit `1bd8906b` (and the note's commit, which
carries the `megafdnat` `--break` update below).

| item | section | bytes | hot? |
|---|---|---:|---|
| **`mega_hibnc` in registers** (pass 1's DEFERRED): the landing in ES:BX for the whole call, the CHS in CX and DL/DH, the caller's offset in SI, the cap and the sectors left in DI (high/low), the run's count pushed across the nested `int 13h`; the sectors moved are the caller's count less those left, so `done` is not kept. `.copy` keeps everything with `pusha`/`push ds`/`push es` and reads its direction off the frame's AH. The track-end sum is `neg`/`add` instead of a load and a `sub` (same value, same flags out of `inc`). 309 -> 223, and the nine `mega_hb*` variables (13 bytes of `.text`) are gone | `.mega` -86, `.text` -13 | no (a program's own int 13h into a high window); every access is a register where it was memory |
| **A native floppy failure returns from `mega_fdgo` directly** (pass 1's "sticky `mm_fdout`", built as a longjmp so the success path costs nothing): `mega_fdgo` banks its entry SP in `[mega_fdsp]`; `.mega`'s `mm_fdout` and `mm_fdin`, on a time-out while `[mega_fdbusy]` is set, and `mega_fdirq` on its own, go to `mega_fdlj` (`mov sp, [mega_fdsp] / jmp mega_fdgo.fall`). So **no `jc` follows any of them** in `mega_fdgo` or `mega_fdseek` - 23 gone. `mega_fdirq` drops its `push ax`/`pop ax` too (every caller loads AL straight after). The hook is a per-copy macro (`mm_fdtmo`/`mm_fdtmi` against empty or `stc` `mega_` twins) so **`.text`'s copy, which `mega_r286` runs in real mode, assembles byte for byte as before** (os88rdiff: `mega_fdout`/`mega_fdin`/`mega_fdsense`/`mega_fdrprobe` `same`) | `.mega` -54 net, the next three rows included (`mega_fdgo` 563 -> 506, `mega_fdseek` 60 -> 47, `mm_fdout` +6, `mm_fdin` +8, `mega_fdirq` -5, `mega_fdlj` +7), `.text` +2 (`mega_fdsp`) | per command byte: **faster** - each call site loses a not-taken `jc` (3 clocks on a 286), ~25 a transfer (SPECIFY 3, SEEK 3 + its IRQ and SENSE, READ/WRITE 9 + its IRQ, 7 result bytes) = ~75 clocks, against one `mov [mega_fdsp], sp` (3). A time-out is the same fallback it was |
| `mega_fdseek`'s verdict is `and al, 0C0h / xor ah, bl / or al, ah / neg al / ret` (CF = 1 for any bit left) where it was test/jnz/cmp/je/stc | in the row above | per seek: faster (9 clocks against ~15) |
| the 765's HD/US byte, twice (`mega_fdgo`'s READ/WRITE and `mega_fdseek`): `mov ax, [mega_fdd] / shl ah, 2 / or al, ah` (`mega_fdh` follows `mega_fdd`) | in the rows above, -2 each | per command: faster (14 clocks against 19) |
| `mega_fd13`: drive/head and sector/cylinder stored as two words (`mega_fds` and `mega_fdc` swapped in the declaration so CL/CH land right); the answer's tail drops `or ah, ah / jz` and the `clc` (CF = 0 out of `or`/`xor`/`or`). 282 -> 269 | `.mega` -13 | per call; fewer instructions |
| `mega_fdgo`'s two `clc` before `ret` (CF = 0 already out of `test` and `xor ah, ah`) | in the `mega_fdgo` row above, -2 | per transfer |
| `mega_fdtk` takes its count in CX (its one caller computes it there with `imul cx, ax, 9`), no `push cx`/`pop cx`/`mov cx, ax`. 23 -> 19 | `.mega` -4 | once a spin-up |
| `mega_bhex2`'s nibble: `cmp al, 10 / sbb al, 69h / das` (pass 1's DEFERRED). 22 -> 19. Shipped: the fault reporter's line is built with it | `.mega` -3 | no |
| `mega_fddir` (written before it is read) moved from `.text`'s state to `.bss` beside `[mega_fdbusy]` | `.text` -1, `.bss` +1 | - it is the PARITY fix below |

**Parity.** Every hot routine pass 1 and the brief name keeps its address
parity: `.text` moves by **-12** from `mega_xdesc` on (ui_task,
wm_paint_all and the rest were briefly odd at -11 until `mega_fddir` went to
`.bss`), `.mega` by **-160** from `mega_fdtk` to its end (selbase,
`mega_lincopy`, the fixups), and `mm_fdout` (-172), `mm_fdin` (-166) and
`mm_fdsense`/`mm_fdrprobe` (-158) are all even shifts. `mega_fdlj` is 7 bytes
(a near `jmp` to `.fall`, not `stc / ret`) for that. What is at an ODD shift
now: `mega_rmaskf`/`mega_vrm`/`mega_hibnc` (-3), `mx_runcntf`/`mega_fdisr`/
`mega_fd13` (-89), `mega_fdseek` (-159), `mega_fdirq` (-151) - per call, per
seek and a wait that spins on a BDA bit until IRQ6 - and **22 routines of
`fsx.inc`'s real-mode bracket** (`fsx_rsvcf` .. `fsx_rupf`, -163): `fsx_rlist`
re-relaxed one `jae` (-3) and `fsx_rupf` one jump the other way (+3), NASM's
own relaxation on shared's code, net zero after `fsx_rupf`. None is on the
brief's hot list; noted for `shared`.

**Equivalence, by reading** (no unicorn here). `mega_hibnc`: a nested
`int 13h` AH=02h/03h keeps every register but AX and FLAGS on both its
routes - native (`mega_fd13` writes only the frame's AX and FLAGS) and
reflected (`mega_svc_back` puts ES back to its own selector when the BIOS
left it, and the landing's selector is an identity one) - which is what the
register state rests on; `.copy` is the same move both ways (DS:SI caller
to ES:DI landing for a write, swapped for a read). The run's count is
`min(left, cap, track rest)` exactly as before, the next-CHS walk is the
same instructions on registers, the answer is AH = the status, AL = the
sectors moved, CF in the caller's FLAGS. It runs 2 bytes DEEPER across the
nested `int 13h` (the pushed count), and `.copy` (a leaf) 20 deeper than
the old one's 2. `mega_fdlj`: every `mm_fdout`/`mm_fdin`/`mega_fdirq` caller
with `[mega_fdbusy]` set is inside `mega_fdgo` (the only `.mega` callers are
`mega_fdgo`, `mega_fdseek` from it, `mm_fdsense` from those, and
`mm_fdrprobe`, which `mega_fdgo` calls with `[mega_fdbusy]` already set and
which therefore returns before any byte - DEFECTS FOUND); `mega_fdgo` has no
state to undo on a failure (its old `.fall` was `stc / ret`), IF is 1 at
every such site (after the `sti` in `mega_fd13`, and the DMA block's `cli`
holds no command byte), and SP put back drops only what `mega_fdgo` and its
callees pushed. The one observable difference is a MEGA_E9/MEGADIAG
fallback line after a SENSE that timed out inside a recalibrate: `[mega_fdres]`
is no longer overwritten with the half-read AX on the way out. The shipped
kernel writes no such line. `tools/stkbalance.py kernel/mega.inc`: 0
unbalanced (base 0), `mega_fdirq` marked `STKBALANCE-OK` for the abandoned
`push bx`. MEGADIAG, MEGAFDMASK, both together and MEGATRAP assemble.

## REFUSED

* **The READ/WRITE DATA command as two runs** (pass 1's DEFERRED; the
  coordinator cleared it for 86Box): with the time-out longjmp every command
  byte is `mov al, [x] / call mm_fdout`, 6 bytes, and a run helper
  (`lodsb / call / loop`) needs C, H, R and N=2 contiguous - which costs
  `mega_fd13` its two word stores back (+8), a constant byte of `.text` (+1,
  conventional, and an odd `.text` shift), and ~10 clocks a command byte.
  Net about -9 `.mega` for +1 conventional and a slower command phase.
* **`mega_hugef`'s three `MX_FL` ORs as one** (`mov al, flags ... or [ss:di+
  MX_FL], al`, -6 `.cold`): built, measured, reverted - it is an `MX_*` site
  and pmmem's now (CROSS-FILE).
* **The ICW stubs' shared `pop ds / retf`**, **the hot cells' stubs**, **the
  one FJ stub**: as pass 1 (each the minimum, or a cycle on every call);
  there is ONE `OSAPI_FJCELL` in the table, so a shared FJ trampoline buys
  nothing.
* **`mega_hgfillx` computing BP from SI**: 31 bytes against 30-31 today.

## DEFERRED (estimates, labelled as such)

* **For pmmem** (`MX_*` sites now): `mega_hugef`'s flags as above, -6
  `.cold` measured; `mega_hugef`/`mega_hugec` share six-pop epilogues of
  identical shape but `retf` against `ret`, nothing to share.
* **Drop the `.mega` `mm_fdrprobe` or make it live** - see WANTS THE OWNER;
  deleting it is ~73 bytes `.mega` (68 + the call).
* **`mega_fd13`'s and `mega_fdjudge`'s E9/MEGADIAG fallback lines as a
  table** (~40 bytes, knob builds only) and **the `MEGA_DIAG` floppy strip,
  the capture stubs and `mega_fdhex`** (~2.9KB) - still unreviewed, they cost
  the shipped kernel nothing.
* **Single-caller routines at the call**: `mega_vrm` (~7), `mega_fdtype`
  (~4), `mega_fdms_wait` (~4, and it needn't preserve AX/CX/DX: its one
  caller clobbers all three, -6 more). Kept for the names the plans cite
  and for parity: every one is EVEN, and the run after them is where
  `selbase` and `mega_lincopy` live. Take them as a pair with an odd byte.
* **`mega_rmaskf`'s IRQ3/IRQ4 vector compare by computed offset** (~2) -
  not worth the reading.
* **`.text`'s MEGA_FDIO copy** (138 bytes, conventional for ever) is only
  `mega_r286`'s real-mode probe; nothing smaller reaches the 765 in real
  mode after a 286 reset. Noted so nobody re-derives it.
* **`mega_fdgo`'s `pushf`/`popf` of ZF round the motor-on test** and
  **`mega_fd13`'s `pushf / push ax ... pop ax / popf`** round the motor count:
  both protect a flag across an `and`; no smaller form found.

## WANTS THE OWNER

* **`mega_fdgo`'s reset probe is DEAD** (DEFECTS FOUND): make it live by
  moving `mov cl, 1 / call mm_fdrprobe` into `mega_fd13` ahead of `mov byte
  [mega_fdbusy], 1` (ES is the BDA there already; 0 bytes, one SENSE
  INTERRUPT STATUS per native transfer and a recalibrate when a reset is
  seen - a behaviour change), or delete it and the `.mega` copy of
  `fdrprobe` (-73 `.mega`), leaving `mega_r286`'s real-mode probe as the only
  one, which is what has actually been running.
* Pass 1's **E9 lines of the native floppy** behind `MEGADIAG=1` - since
  msp1/e9gate.md the shipped kern_mega writes no E9 at all, so this is done.

## WANTS AN ABI BREAK

* **The kern_mega hot cell's two `nop`s** (44 cells, 88 bytes `.text`) and
  **the FJ cell's four** (1 cell): the cell length is one ABI for every
  kernel. Recorded, as pass 1 did.
* **`MEGA_ICW` inline setter cells on kern_mega** jump to an 11-byte stub
  each (10 stubs, 110 bytes `.text`, conventional) because a store through
  CS is illegal in protected mode. A flag day that made those ten cells
  ordinary SLOTs (or published the variables through a getter/setter pair on
  one cell with an index) would delete the stubs on kern_mega - but the
  inline form is what makes `OSAPI_SET_COLOR` 52 cycles on the XT, so it is
  a trade against kern_big's speed. Every package rebuilt; est. -80 `.text`
  on kern_mega, +0 on kern_big.
* **A package's `int 13h` into a buffer above 1MB** (`mega_hibnc`, 223 bytes
  `.mega`) exists because the BIOS surface is offered to protected-mode
  packages unchanged. A contract that refused ES above 1MB for a raw
  `int 13h` (09h, as the 8237 would) would delete it; it breaks any package
  that reads a disk raw into `OSAPI_MEM_HUGE`/XMEM memory (`tests/megaxm`'s
  `i13` step is the one known).

## CROSS-FILE

* **pmmem**: `mega_hugef`'s `MX_FL` stores (-6 `.cold`, above). No `MX_*` or
  `mem_sel` site was changed in this diff.
* **pmcore**: `mega_hexb` (MEGADIAG) and `MEGA_E9IO`'s `hex2` (E9 knobs)
  print a nibble with `add '0' / cmp '9' / jbe / add 7`; `cmp al, 10 / sbb
  al, 69h / das` is 3 bytes shorter each, knob builds only.
* **shared**: 22 `fsx.inc` routines in `.mega` sit at an odd shift because
  NASM re-relaxed `fsx_rlist`'s `jae .no` (-3) and one jump in `fsx_rupf`
  (+3); they are the real-mode bracket's setup and teardown, not hot.
* **tests**: `tests/megafdnat.py`'s `--break` planted `stc / ret` at
  `mm_fdout`, which is no longer a time-out (its callers do not test CF); it
  now plants `jmp mega_fdlj`, the time-out's own exit, and the row's
  description in `tests/suite.py` says so.

## ROWS TO RUN (named, not run)

* **`megafdnat`** (86Box, `vm/286-mega`): the native floppy with the 286
  reset path - `mega_fdgo`, `mega_fdseek`, `mm_fd*`, `mega_fdlj` and
  `mega_r286`'s unchanged `.text` probe together - green, AND `--break`
  red (8 fallbacks, the path off) with the new break. **This is the
  measurement pass 1 asked for on the command phase**: a 120KB copy's wall
  time against the base's.
* **`megaxmove`**: tests/megaxm's `i13` step is `mega_hibnc` - 18 sectors in
  two runs of the landing's nine, read into a block above 1MB at an odd
  offset and compared, written back and read again.
* **`mega-uilayer`**, **`megacurdisk`**, **`megahuge`**, **`megavidbig`**:
  every Bochs boot of `mega.img` reads through `mega_fd13`/`mega_fdgo`.
* **`megaflt`**: the fault reporter's line (`mega_bhex2`, `mega_bhex4`).
* **`megatree`** (host).
* A native floppy **WRITE** and a **write-protected** disk: whichever
  `mega-*` row copies onto A: (megafdnat does); nothing here forces ST1 NW.

Host checks run: `make -j1 mega`, `make -j1` (fast tier, 68 passed, 2
skipped, `hotpar` ok), `tools/stkbalance.py kernel/mega.inc`,
`os88ovlchk` (in `make`), the four knob assemblies above, `checkdocs`.

## DEFECTS FOUND

* **`mega_fdgo`'s `mm_fdrprobe` never probes.** `mega_fd13` sets
  `[mega_fdbusy]` to 1 and then calls `mega_fdgo`, whose first act is
  `mov cl, 1 / call mm_fdrprobe` - and `fdrprobe`'s first test is
  `cmp byte [mega_fdbusy], 0 / jne .no` (*"an IRQ reflected out of the
  middle of our own transfer"*). So the protected-mode half of the probe
  KERN-MEGA-PLAN 14 describes (*"reset by a reflected service? recalibrate"*,
  and `mega_fdrst`'s *"at mega_r286, and at mega_fdgo"*) has returned CF = 0
  without touching the 765 on every transfer. The real-mode probe in
  `mega_r286` (CL = 0) is what has been running. Not fixed here (behaviour);
  WANTS THE OWNER has the two ways out. **The coordinator made it LIVE** at
  the merge, on the owner's deferral: the call moved into `mega_fd13`, after
  `inc byte [sch_lock]` and before the busy flag is set, ES = the BDA and
  IF = 0 there as the probe needs (it polls the 765, it waits on no IRQ).
  0 bytes; one SENSE INTERRUPT STATUS per native transfer, which an idle
  765 answers 80h - microseconds against a transfer's ~400 ms. Deleting it
  would have saved ~73 `.mega` and left a 386 (the CR0 route, which never
  passes `mega_r286`) with no protection against the end-stop seek the probe
  exists for. 86Box `megafdnat` (green and `--break` red) is its check.
* `mega_fdirq`'s header said *"halting between"*; the loop SPINS (it has,
  since the Victory 286's parity NMIs - the comment inside says so). Header
  corrected.
