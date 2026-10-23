# Apps size pass 2, set A (the system disk) — measured 2026-10-16

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file.

The first set of the second apps size pass (docs/plans/HANDOFF-APPS-SIZE-P2.md),
whose subject is **disk room**: the packages on the 360KB system disk —
`dos`, `paint`, `notepad`, `deskcal`, `thewire`, `taskmgr`, `fontview`,
`rlapply`, `mines`.

- **Tree**: the pass branch `claude/apps-size-2-disk-reclaim-6rn5hh`, cut from
  `elendilon` at `2c6edafc`, with `elendilon` at `4466b348` merged in during
  the set (it carried the separate DeskCal size pass, `calendar-size`).
  **"Before" is `4466b348` built in a private worktree**, so every figure
  below is set A's work alone and none of it is `calendar-size`'s; "after"
  is the pass branch with all nine packages merged.
- **Box**: the 4-core cloud container this session ran in. Every number is
  counted, not timed, except the cycle rows, which are MartyPC guest cycles
  on a 4.77 MHz 8088.
- **Method**: one agent per package, each in its own git worktree, briefed
  with the handoff's 4.3: the packed FILE is the subject; nothing
  user-visible changes; no hot path slower; RAM may not grow; shared
  includes, kernel, drivers and shipped-byte tools off-limits; host-side
  checks only, with the rows run once at the end (below).

## The table

`file` is what ships (LZ4). Clusters are 1,024 bytes on every 360KB image;
`small` is the APP_SMALL arm on `small360.img` and `smallapps360.img`.

| package | file before | after | bytes | clusters (per copy) | copies on 360KB | image + bss before | after |
|---|---:|---:|---:|---|---|---:|---:|
| dos (`DOS.O88`) | 48,172 | 48,172 | 0 | 48 | system | 48,295 | 48,045 |
| paint | 21,934 | 21,614 | -320 | 22 | system, apps, office | 32,378 | 31,873 |
| paint (small) | 15,002 | 14,312 | -690 | **15 -> 14** | small, smallapps | 21,370 | 20,419 |
| notepad | 15,233 | 14,875 | -358 | 15 | system, apps | 20,133 | 19,610 |
| notepad (small) | 10,269 | 9,850 | -419 | **11 -> 10** | small, smallapps | 13,234 | 12,742 |
| deskcal | 12,535 | 12,500 | -35 | 13 | system | 17,389 | 17,341 |
| thewire | 10,256 | 9,984 | -272 | **11 -> 10** | system, network | 14,728 | 14,265 |
| taskmgr | 7,047 | 6,711 | -336 | 7 | system, apps | 9,747 | 9,287 |
| taskmgr (small) | 4,175 | 3,952 | -223 | **5 -> 4** | small, smallapps | 6,004 | 5,713 |
| fontview | 3,549 | 3,456 | -93 | 4 | system, office | 5,923 | 5,803 |
| rlapply | 1,979 | 1,520 | -459 | 2 | system | 2,282 | 1,723 |
| mines | 1,678 | 1,413 | -265 | 2 | system, apps, games | 1,849 | 1,628 |

**Free clusters gained** (`tools/os88diskuse.py --diff`): system 360
**+1** (7 -> 8), network360 **+1**, small360 **+3**, smallapps360 **+3**;
apps360, office360, games360 and media360 +0. On the 1.2MB and 1.44MB system
disks (512-byte clusters) RLAPPLY and The Wire each give one back as well.

DOS's image + bss is `build/dos.o88`'s header (the box); what ships is `build/kdos/DOS.O88`, a loader plus three parts.

The bytes: **-2,138 off the full arms on the 360KB system disk**, -1,330 off
the small arms. RAM fell on every package; no heap claim grew.

## What the set found about disk room

- **A size pass rarely buys a cluster.** Seven of nine packages lost 200-700
  file bytes and crossed nothing: a cluster is 1,024 packed bytes, and what
  is left in these packages after pass 1 is spread thin. The clusters that
  did come back came off the APP_SMALL arms (Paint's small arm stopped
  assembling code nothing could reach, -500) and off The Wire.
- **The file follows UNIQUE bytes, not the image.** Every agent hit it:
  removing a repeated push/pop idiom saves image bytes and often ADDS file
  bytes, because LZ4 had been packing the repetition for ~3 bytes a copy.
  Task Manager's macro change cut 34 image bytes and added 6 to the file;
  DOS's shared pop-ladder epilogues were measured at **+111** packed bytes
  and refused. Measure the packed file, never infer from the image.
- **DOS is padding-bound, not code-bound.** `DOS.O88` is a raw loader plus
  three parts, each padded to a 512-byte sector (898 bytes of padding in
  all), so 129 packed bytes out of part 0 did not move the file at all. Its
  real levers are structural and outside the package (below): together they
  are worth about three sectors.
- **The kernel grew a cluster during the set.** `KERNEL.SYS` went 82 -> 83
  clusters on the 360KB system disk between `2c6edafc` and `4466b348`, so
  the apps' reclaim is partly spent before it lands.

## Hot paths

Nothing measured got slower; most got faster (MartyPC, 5150 CGA unless noted):

| path | before | after |
|---|---:|---:|
| taskmgr `tm_sample` | 71,654 | 63,778 |
| taskmgr full repaint, memory page | 1,806,432 | 1,690,991 |
| notepad Find box, typing 'e' | 66,232 | 65,181 |
| notepad Backspace in the note | 737-742k | 735-746k (noise) |
| fontview keystroke repaint (typing X) | 471,935 | 470,785 |
| thewire one 18-byte LZSS match | 1,747 | 1,009 |
| thewire worker loop, 512 body bytes | 201,097 | 198,025 |
| mines 81-cell flood, CGA / VGA | 2.23M / 1.70M | 2.13M / 1.60M |
| mines lost mixed redraw, CGA / VGA | 3.11M / 2.75M | 3.04M / 2.68M |

Paint's stroke, fill, blits and decoders are byte-for-byte unchanged. Nothing
moved into an entry proc, so no launch got slower.

## Defects

### Fixed

- **dos**: a full COMSPEC path (`C:\COMMAND.COM`) was never recognised as the
  built-in shell - an `inc si`/`dec si` pair cancelled the step past the
  separator (SPEC.md 96.30 names that path as its example).
- **dos**: the packet driver's handle check overwrote the client's CX with 7,
  so `get_address` never refused a short buffer and `release_type` /
  `terminate` returned a changed CX.
- **thewire**: the status line could compose 57 characters into a 49-byte
  buffer whose overflow landed on the message being shown - a long title
  during an Add showed the figure's tail where "Adding" belonged.
- **notepad**: opening Find with a multi-line selection could leave the box
  showing one pattern while the search used a longer one.
- **paint**: Save Gif was gated on the clipboard's memory rather than the
  GIF encoder's ("No RAM for clipboard" on a nearly full heap).
- **mines**: a hang the pass itself introduced (`cbw` does not set flags, so
  a board with a flag ran off its shape list) - bisected and fixed before
  merge; every surviving commit was measured.

### Found, NOT fixed (the owner's call)

- **taskmgr**: the heap page's TYPE column is 7 wide, so `Compress`,
  `Shortcut` and `Disk img` have always shown truncated. Widening it to 8
  fits but moves three columns a character right.
- **mines**: drawing an opened cell as two fills instead of five would be
  ~40% faster on a flood; it changes the cell's brief flash, so it is a look
  decision.
- **dos**: the disk-cache dropdown is press-press where Word, Pixel, Skies
  and RedLine are press-drag-release; `tests/dirwshed.py`'s comment still
  names `dos_brect`.
- **paint (small)**: ~100 more bytes are available by gating the "am I
  fullscreen?" tests the small arm can never satisfy; the author left them
  ungated on purpose.

## The process, for the next sets

- **The Agent tool's `isolation: "worktree"` put every worktree under
  `.claude/worktrees/`**, where each Edit and Write is a change inside
  Claude's config directory and raised an approval prompt for the owner, and
  cut them from `a123653b`, 48 commits behind the session's branch. The
  handoff's 4.1 now has sessions make their own worktrees under
  `/home/user/wt/`; the DeskCal redo ran that way.
- **A dedicated pass on one package ran in parallel**: `calendar-size` (an
  owner-directed DeskCal pass, ~5 KB) landed on `elendilon` mid-set and
  conflicted in all five DeskCal sources. Set A's DeskCal merge was reverted
  and the work redone on top; 35 bytes of it still applied.

## Coverage

The merged tree built clean (fast tier 47/47; `make small smallapps`), and
ran the union of the nine agents' rows plus the shared rows a package change
reaches, through `tools/os88soak.py` at lane 4, once:

**125 of 126 ok in 29:32, 0 SKIP** (run `20261016-142055`), with `mtools` and
`unicorn` installed for it so the DeskCal, `instkeep`, `bigvol` and `csprobe`
rows ran rather than skipped. Then `make test-full` once: **54/54 ok**.

The one failure is **not set A's**: `fdlgchoose`'s *"the save landed in the
folder"* - a Save As into `B:\MEDIA` from Note Pad commits and the listing
does not show `NEWNOTE.TXT` - fails identically on `elendilon` at
`4466b348` built in a private worktree, with no set A change in it. It is
reported here and not investigated.

Not covered by any row: The Wire's list-row painter with a real catalog
(MartyPC has no NIC; the painter's parts were measured in isolation by the
agent's own harness, and `thewire` runs on QEMU), and a pixel A/B of Task
Manager's three pages against the base (all three painted without fault in
the agent's timing runs).

## Wins no set can take - shared includes, kernel, tools

Folded into docs/plans/HANDOFF-APPS-SIZE-P2.md 6. Every figure is the
agent's estimate unless it says measured.

| where | what | disk value |
|---|---|---|
| `tools/os88pkg.py` | compress the IMAGE (loader) of a package that has parts: DOS's 2,276-byte loader packs to 1,980 | **-1 sector of DOS.O88 (-512 B), every system disk** |
| `kerndos/` | ship the INT 21h core inline in part 0 and have kern_dos's stub read it from there - deletes part 1 and its 317 bytes of padding, one fetch and a 41 ms copy | **~-512 B, every system disk** |
| `apps/os88cp437.inc` / `os88con.inc` | generate box-drawing glyphs 0xB0-0xDF at launch instead of shipping them: 220 packed bytes of DOS part 0, which alone crosses a sector; Telnet carries the same data | **-512 B of DOS.O88**, plus Telnet's share |
| `apps/os88ui.inc` / `os88con.inc` / `os88line.inc` | gates for `os88ui_drdrag`/`drup`/`drclose`, `con_scrolldown`, `os88line_set` (unused by DOS; `os88line_set` also unused by DeskCal, 51 B) | ~180 packed in DOS |
| `apps/os88ui.inc` | split `OS88UI_NOGEST` into its setup half (`btninit`/`btnclick`/`btlist`) and its press half: Note Pad's full build carries ~111 image / ~90 file bytes of setup it never runs; wbfront, apcb, mrcb, atui and redline may gain the same | ~90 B per carrier |
| `apps/os88gfx.inc` | an opt-out for the band helpers (`gfxe_bnew`/`bfold`/`bsize`/`bpix`/`bput`): 187 image / ~160 packed bytes the calendar never calls | ~160 B, DeskCal only |
| `apps/os88type.inc` | `OS88TY_NOGRAY`: `ty_gray` and its call, for includers that never grey (Font Viewer; CWORD) | ~50 B per carrier |
| `apps/os88data.inc` | a read-only switch leaving out `od_write` (RLAPPLY is the only includer that never writes) | ~65 B, RLAPPLY |
| `apps/os88partsbody.inc` | a gate for `op_lazyok`, which DOS's loader never calls | 66 raw bytes |
| `apps/os88ui.inc` | the scroll bar (~520) and About card (~140) are ~660 packed bytes of Task Manager; no specific cut found | - |

## The follow-on (2b), owner-approved after the set landed

Built on the pass branch after sets A to C had merged; numbers are against the
tree just before each piece merged.

| change | what it bought |
|---|---|
| **A parted package's image ships LZ4-packed** (SPEC.md 20.13.8): `tools/os88pkg.py` packs the loader when that saves a sector, the kernel takes the packed length from the header's link word | **DOS.O88 48,172 -> 47,660, 48 -> 47 clusters on every system disk**; PiXEL 113,147 -> 105,467 (111 -> 103 on office360); C64 and Apple II -8,192 each (8 clusters on their 360KB disks). **Kernel: `.cold` +33 resident bytes on each kernel** (kern_big's cold rung now 1 byte from crossing). `PKG_FMT` unchanged: no old package sets the pair, and an old kernel refuses the new one as `Bad package` |
| **Opt-in include gates** (`OS88UI_NOBTNINIT`, `OS88UI_NODRGEST`, `OS88LINE_NOSET`, `GFXE_NOBUTIL`, `OS88TY_NOGRAY`, `OD_READONLY`, `OP_NO_LAZYOK`, `CON_NOSCROLLDOWN`), default off, every non-opting package byte-identical | Note Pad -109, DeskCal -208, Font Viewer -56, RLAPPLY -73, DOS part 0 -173 packed, Task Manager small arm -647 (`OS88UI_NOBTN`); no cluster crossed |
| **Task Manager**: TYPE column 7 -> 8, group headings the whole owner name (15) | `Compress`/`Shortcut`/`Disk img` and `CALCULATOR` shown whole, 3 px clear of the bar; file 6,711 -> 6,707 |
| **Mines**: an opened cell in two fills, taken because it measured LESS flash | flood transient 136 -> 31 px (CGA) / 137 -> 35 (VGA), flood 35-37% faster, settled picture identical |

Measured and refused: generating the CP437 box-drawing glyphs at launch
(LZ4 already packs them to ~179 bytes; the best decoder saved 17 packed for
+10 RAM). Dropped by the owner: the INT 21h core inside part 0 (part 1 is
moved to launch programs under kern_dos, and HIBER.DRV's handoff stub reads
it off the disk).

**Found, not fixed (the owner's call)**: SPEC.md 20.13.8.1 - step 6's read
of `roundup512(file)` at `R` can run up to 511 bytes past the region for a
hostile header with a short bss, true of every compressed package since
20.13; the decoder's output bound still keeps every decoded write inside.
The fix is one bound test in the size check, at resident bytes.

**Coverage**: one soak over the union the four pieces reach (110 rows, run
`20261016-161511`), then the rows a test-side fix touched re-run alone:
every row passed except **`pxbench`, which fails on the tree BEFORE the
packed image too** (its "first rows on the glass" ceilings swing by up to
1.2 s in both directions between two runs of either tree; the opens it times
are inside an already-running PiXEL, which the change never reaches), and
`pxopenpng` and `deskcaledit`, which each failed once under load and passed
alone. Nine PiXEL tests had compared the shipped FILE against the assembly,
and their instance finder the loaded header against it; both are taught the
packed image (`tests/pxsyms.py`'s `same_header`). `make test-full` 54/54.

**The disks after sets A-D and this follow-on**: the 360KB system disk
**5 -> 11 free**, the 360KB apps disk **1 -> 30**.
