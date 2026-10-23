# ksp13 agent "assoc" - the ASSOC.DAT runtime writer

Kernel size pass 13 (its brief is `docs/plans/completed/ksp14/BRIEF.md` on
this branch - the pass was renumbered after the brief was written). Concept:
`ASSOC.DAT` written back at runtime (SPEC.md 54.7.6) - `13c9752f`,
`eb90b2ca`, `52fb5822` and what the `4833dfef` merge brought. Owned: the
resident hunks those commits added (`asc_post`, `asc_learn`, `asc_flush`,
`osapi_assoc_flush_x`, the hold in `fm_fmt_drop`, `assoc_locate`'s and
`assoc_run_x`'s flush hunks, the capture's constants and its two word stores
in `disk.inc`, the 16/32 slot tables' growth) and the whole of
`kernel/ascw.inc`, which rides `FORMAT.DRV` (`.modf`). Branch `ksp13-assoc`,
cut from `aedadd25` (`4833dfef` plus the brief). Figures are ASSEMBLED bytes:
`tools/kernsize.py --json` absolutes for the three kernels, resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; the module is the
UNPACKED image (`os88drv.image_unwrap`).

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big resident | 92,757 | 92,725 | **-32** |
| kern_big `.text` / `.bss` / `.cold` | 43,756 / 5,223 / 38,084 | 43,756 / 5,223 / 38,052 | 0 / 0 / -32 |
| kern_big `.boot2` / `.ovl` | 5,129 / 5,106 | 5,129 / 5,106 | 0 |
| kern_big `KERN_SIZE` | 98,816 | 98,816 | no rung moved (cold rung: 316 left -> 348) |
| kern_emu resident | 93,016 | 92,984 | **-32** (`.cold` only, `os88rdiff -D KERN_EMU`) |
| kern_small resident | 60,260 | 60,260 | 0 - **every routine `same`** (`os88rdiff -D KERN_SMALL`): kern_small has no associations (SPEC.md 54.0), and the one shared line moved is assembled identically |
| **`FORMAT.DRV` image (kern_big, kern_emu)** | **3,113** | **3,017** | **-96: the claim goes 4 KB -> 3 KB** |
| `FORMAT.DRV` file (lz4) | 2,851 | 2,821 | -30 |
| `FORMAT.DRV` image (kern_small) | 1,136 | 1,136 | 0 (it carries no flush) |

The concept's own unpassed resident, routine by routine against `3bb038f3`,
was ~322 bytes on kern_big: `asc_learn` 67, `asc_flush` 43, `asc_post` 29,
`assoc_locate` +26, `assoc_run_x`'s flush branch 16, `fm_fmt_drop` +7, the
API cell 6, `FMFP`'s fifth slot 4 (`.bss`), the far entry 4, the harvest's
call 3, `asc_hold`/`asc_wno`/`ico_capn` 3, two word stores 2 - and **112 of
it is the slot tables' growth from 12/24 to 16/32**, which is a capacity
decision and not code (see REFUSED). -32 is 10% of the whole and 15% of the
210 that is code.

**The module is where the KB was.** `FORMAT.DRV` was 41 bytes over 3 KB, so
every format and every flush claimed 4 KB of heap for 3,113 bytes; it is 55
under now. That is 1,024 bytes of heap for as long as the image is loaded -
transient on both its paths, but the installer HOLDS it (SPEC.md 54.7.6.4)
for the whole of an install.

## TAKEN

### `ascw.inc`: the scratch off BP, the resident calls by inline word - -96 of image

* **Every scratch cell is `[cs:bp+asw_x-asw_v]`**, a d8, with BP set once at
  the entry to the block (`asw_v`) and written by nothing below - where it
  was `[cs:asw_x]`, a disp16, at 117 sites. Spelled out rather than through a
  macro because `os88ovlchk` reads the `cs:` in the source. The sites that
  were accumulator moves (`A0`/`A2` moffs, already four bytes with the
  prefix) gain nothing, which is why it is not -117.
* **`ASWX` is `call asw_cold / dw routine`**: five bytes a site against
  `mov bp, x / call COLD_SEG:wm_cbd`'s eight, at fifteen sites, for a
  21-byte helper that reads the word after its return, steps the return past
  it and far-calls `wm_cbd`. It restores BP, which is what lets BP hold the
  scratch base; `pop` and `ret` write no flag, so every routine's CF reaches
  its caller as before. One far call to `COLD_SEG` instead of fifteen, so the
  ROM arm's module fix list is shorter too (`make rom-big`: built, 365 bytes
  of the window spare).
* Cost: the flush's stack is 4 bytes deeper at each resident call (BP and a
  near return on top of the far frame `wm_cbd` already took), on the UI task
  or a package callback, never a worker (SPEC.md 20.6 rule 7). Not a hot path.

### `asc_flush` is one body - -12

`dskw_fmt_go` refuses by returning past ITS caller. `asc_flush` nested two
routines (`.go`, `.ld`) so that the refusal came back to it and the drop
always ran. Called directly, a refusal answers `asc_flush`'s own caller -
CF=1, AX = `FERR_NODISK`, both callers' contract - and skips
`fm_fmt_drop`. What it skips frees nothing: `mod_need` refuses only through
`.fail`/`.freefail`, both of which leave `MODR_SEG` 0, and `mod_free_row`
is a compare on that word. On the hold's acquire the skipped drop was a no-op
twice over (`[asc_hold]` is already set). Acquire and release share the
`xor ax, ax` that answers CF=0, AX=0.

### `assoc_locate`'s learn test - -4

BX = DI = the slot, so the cluster word is `[assoc_clus+bx+di]` with no
`shl`; DX (restored at `.out`) carries it, so AL is still the volume for
`asc_post`; and `asc_post` preserves BX, so `assoc_hint_here` is reached
without the second `mov bx, di`.

### The capture: 17-byte records, and `asc_learn`'s frame - -10

* The row array (11 bytes, padded to 12) and the 16-byte records after it
  are ONE array of 17-byte records - the row, then the block. `asc_learn`
  finds its record with `mov ah, 17 / mul ah / add ax, ICO_CAP` and writes the
  row with one `stosb` (-5); the flush's search walks one stride and the
  block is `[bx+1]` (module -4). 187 bytes in the same 200 of the store
  claim's KB rounding (`5,107` of 5,120, asserted). SPEC.md 54.7.6.2's table
  says so.
* The block copy is `ss lodsw / stosw / loop` (-2) - not `rep ss movsw`,
  which is two prefixes on a string op and the 8086's lost-prefix resume.
* `asc_learn` banks AX and ES only (-3): its one caller, `disk_mount_x`'s
  harvest, holds nothing in BX, CX, SI or DI across it - `ico_glyph_put`
  takes AL/DX/SI and SI is reloaded, `dsk_get_dir_x` answers SI, the loop
  head sets CX and DI and takes BX from `dsk_peek_x`, `dsk_docpass` takes BP,
  and the routine leaves through `kretc_es`. Its header says so.

### `ico_need` does not clear `[ico_n]` on kern_big - -6

`[ico_seg]` is written by three things: `ico_need`'s claim, `ico_reloc`'s
move, and `ico_demote`'s zero - which `mem_pg_forget`'s `.ico` arm is the
only way into, and which clears the WORD `[ico_n]` (so `[ico_capn]` with
it). So on `ico_need`'s claim path `[ico_seg]` is 0 and both bytes are
already 0 (at boot they are `.bss`). The kern_small arm is wkfs's: see
CROSS-FILE.

## REFUSED

* **The slot tables in a heap claim, or in the writer's** (the brief's
  question). They are not the writer's data: `assoc_stem`/`assoc_glyph`/
  `assoc_ext`/`assoc_clus`/`assoc_drv`/`assoc_dfold` are the association map
  itself - read for every listed file (document icons, SPEC.md 54.3) and
  every double-click (54.4), with the five built-ins and their BAKED glyphs
  (`associco.inc`) required with no disk at all. A claim held for the
  session is the same RAM in another account; a purgeable one loses every
  learned association to a shed, re-learned only by browsing the declaring
  volume again - a functionality change that buys nothing while the machine
  is in use. Only the CAPACITY is a lever, and it is in WANTS THE OWNER.
* **`assoc_run_x`'s `jmp .out` as a short jump to the handover's `jmp short
  .out`** (-1): the target is exactly 127 bytes on, with the links concept's
  hunk (`lnk_is`/`lnk_open`) between; any growth there fails the merge's
  assembly. Not worth one byte of the coordinator's day.
* **`ASCF_HOLD` renumbered to `MOD_FMT`'s value** so `fm_fmt_drop`'s hold
  test is `cmp al, [asc_hold]` after its own `mov al, MOD_FMT` (-1): the ABI
  is free, but it ties an SDK constant to a kernel module number for a byte.
* **The far entry `osapi_assoc_flush_x` folded into `asc_flush`** (`retf`,
  and `push cs / call` at `assoc_run_x`, -3): `dskw_fmt_go`'s refusal pops a
  NEAR return, so `asc_flush` cannot be far.
* **`FME_ASCW` through an existing module entry with a selector** (-4 of
  `.bss`): nothing the four formatter stubs' callers leave in a register can
  be proved never to equal the selector.
* **`asc_post` loading `[disk_drive]` itself**: both callers already hold it
  in AL, and `asc_learn` needs the row elsewhere anyway - +2.
* **`[asc_wno]` folded into `[asc_vol]`** (a refused write un-keying the
  cache): the next harvest re-reads `ASSOC.DAT`, re-keys, and posts again -
  the loop `[asc_wno]` exists to stop, plus I/O.
* **The hold test out of `fm_fmt_drop`**: `asc_flush`'s own drop has to
  honour it too, so it would move rather than go.
* `ascw.inc` past 3,017: the next KB step is 2,048, out of reach; further
  image bytes are disk bytes only.

## WANTS THE OWNER

* **Drop the capture** (SPEC.md 54.7.6.2): `asc_learn` becomes `push ax /
  mov al, [disk_drive] / call asc_post / pop ax` at its call site, and
  `[ico_capn]` and the two word stores go - **~-54 resident on kern_big**
  (ESTIMATED from the routine sizes: `asc_learn` 57 + its call 3 against 8
  inline, `ico_capn` 1, `ico_demote`'s word store 1), and ~-40 of
  `FORMAT.DRV`. The flush then reads the HEADER of each newly-learned
  DECLARING package (`asw_header`'s block-only arm, which it already takes
  for a built-in program) - one `dsk_peek` each, ~400 ms of `int 13h` on the
  XT, so a fresh apps disk with ~11 declaring programs flushes in ~4 s more
  (ESTIMATED; `assocflush` measures the capture's case at 3.65 s for one
  package and 4.07 s for seventeen). The capture is a speed feature, and
  this is that trade.
* **The slot tables back to 12/24** (-112): it re-opens the defect
  `52fb5822`/`eb90b2ca` closed - nothing frees a slot within a session, so a
  machine meeting more than seven declaring programs past the built-ins
  drops the rest SILENTLY (office360 + apps360 wanted nine). Not
  recommended; recorded because it is the concept's largest resident row.

## WANTS AN ABI BREAK

None. `OSAPI_ASSOC_FLUSH` and `ASSOC_FL_*` are free and were left as they
are: folding the flush into frozen `OSAPI_ASSOC_SET` has no register left
to carry a mode (ES:SI is the whole argument), and the cell plus its far
entry is 10 bytes against a feature two callers use.

## CROSS-FILE

* **wkfs, `assoc.inc` `assoc_tryvol`**: `assoc_dfold` is a CONSTANT table -
  one reader (`.fold`), no writer anywhere, 1 for the five built-ins and 0
  past them. `mov bx, 1 / cmp di, 5 / jb .one` in place of `mov bx, di / mov
  bl, [assoc_dfold+bx] / or bl, bl / jnz .one / inc bx` and the 16-byte
  table goes: **-19 kern_big** (and BH is 0 either way). The table grew 12 ->
  16 with this concept, but the reader is not a concept hunk.
* **wkfs, `disk.inc` `ico_need`**: kern_small's `mov byte [ico_n], 0` is
  redundant for the same reason as kern_big's (`DSK_WANT_ICONS` is both
  kernels', so `ico_demote` is the only zeroer of `[ico_seg]` there too):
  **-5 kern_small**. The line now sits under `%ifndef OS88_ASSOC`; delete it.
* **wkfs**: `asc_learn`'s contract is weaker now - it clobbers BX, CX, SI and
  DI. A harvest change that keeps anything in those across `call asc_learn`
  must bank it.

## ROWS RUN

One scoped pass was STARTED after the last code commit (`os88test.py soak
-k assocflush -k instassoc -k assocglyph -k assocopen -k assocfind -k
assocsweep -k assocvol -k assocwake -k assocstale -k icostore -k icoshed -k
fmtlow -k fmtreach -k fmthumb -k rombig --marty-jobs 1`, 818 s declared)
and had NOT REPORTED when this note was written - **no row result is
claimed here**. The coordinator's close should run that union.
`assocflush` is the gate for everything taken (the capture, `asc_learn`,
the locate trigger, `[asc_wno]`, the flush itself); `instassoc` the hold;
`icoshed` the store's demote/re-claim (the `ico_need` change); `fmt*` the
formatter's own load and drop through `fm_fmt_drop`; `rombig` the module
fix list. Run at every batch: the fast tier inside `make` (47/47), `make
small`, `make emu`, `make rom-big` (builds; 365 bytes of the ROM window
spare).

## EQUIVALENCE

* `os88rdiff` against the base: kern_small every routine `same`; kern_big
  and kern_emu change only `assoc_locate`, `asc_learn`, `ico_need`,
  `asc_flush` and the module, and every `.cold` shift run is EVEN (+0, -4,
  -14, -20, -32) - no hot routine moved at all (`--hot` over the brief's
  list: all `same`).
* `tools/stkbalance.py` over `assoc.inc`, `diskw.inc`, `disk.inc` and
  `ascw.inc` together: 8 unbalanced paths at the base and at the tip, the
  same ones (`asc_take_x`'s tail into the ladder and the kret family); the
  new `asw_cold` is declared `STKBALANCE-OK`.
* The refusal arms of `asc_flush` were read rather than run - no row starts
  a flush with the system disk out - and the argument is in TAKEN.

## DEFECTS FOUND

None in behaviour. Two stale comments in `ascw.inc` (`asw_old`'s "napp <=
32", `asw_rowoff`'s "<= 31 * 88") predate the 55-row cap; both products
still fit AX (54 x 88 = 4,752). Not edited: the rows were running.

A process note for the coordinator: `git stash` is SHARED by every worktree
of one repository, so an agent that stashes to measure a base can pop
another agent's work; this note's base figures were taken with `git show
<base>:file` into a scratch directory instead.
