# Apps size pass 2 - what it brought to elendilon, measured 2026-10-16

**A MEASUREMENT** (docs/README.md): true of the two trees below and of no
other. A later measurement is a new file.

The second apps size pass (docs/plans/HANDOFF-APPS-SIZE-P2.md) measured a
package by what it costs a DISK. Four sets ran in four sessions, coordinated
over the pass branch `claude/apps-size-2-disk-reclaim-6rn5hh`:

| set | packages | report |
|---|---|---|
| A, the system disk | dos, paint, notepad, deskcal, thewire, taskmgr, fontview, rlapply, mines - and an owner-approved follow-on (packed parted images, opt-in include gates, Task Manager's columns, Mines' two-fill cells) | docs/reports/APPS-SIZE-P2-SETA-2026-10-16.md |
| B, the apps disk: network and office | browser, ftpd, telnet, OS88NET.COM, texpad, artful, audio, calc - and LZB for fourteen packages, the owner's pick | docs/reports/APPS-SIZE-P2-SETB-2026-10-16.md |
| C, the apps disk: games | tracker, wireball, tank, cyclone, missile, arkanoid, solitaire, tamegram | docs/reports/APPS-SIZE-P2-SETC-2026-10-16.md |
| D, owner-constrained and small | skies, video, fractal, piano, timer, midirack, chart | docs/reports/APPS-SIZE-P2-SETD-2026-10-16.md |
| E, the office and games disks | pixel, pixelstein, word, drmario, isotron, dotdel, gorillas | **deferred to the next round** (the owner's call; DrMarco gets its own pass turning its files into parts) |

- **Before**: `elendilon` at `0d35815c`, built in a private worktree in this
  container (`make`, `make small smallapps`).
- **After**: the same tree with the pass branch merged (`f69a50fd`), built
  the same way. So every figure below is what the MERGE brings, and none of
  it is `elendilon`'s own twelve commits since the pass was cut.
- **Box**: the 4-core cloud container this session ran in. Every number is
  counted (bytes, clusters, sections); the launch costs are the set reports'
  MartyPC measurements and estimates.

## The disks

Free clusters, before -> after (`tools/os88diskuse.py`):

| image | cluster | free before | after | gained |
|---|---:|---:|---:|---:|
| `os8088-360.img` (system) | 1 KB | 7 | **11** | **+4** |
| `apps360.img` | 1 KB | 1 | **30** | **+29** |
| `office360.img` | 1 KB | 51 | 72 | +21 |
| `games360.img` | 1 KB | 80 | 95 | +15 |
| `network360.img` | 1 KB | 279 | 289 | +10 |
| `media360.img` | 1 KB | 145 | 148 | +3 |
| `small360.img` (kern_small system) | 1 KB | 57 | 79 | +22 |
| `smallapps360.img` | 1 KB | 154 | 176 | +22 |
| `os8088-720.img` | 1 KB | 191 | 200 | +9 |
| `apps720.img` | 1 KB | 34 | 74 | +40 |
| `os8088.img` (1.44MB) | 512 B | 1,836 | 1,853 | +17 |
| `apps.img` (1.44MB) | 512 B | 1,119 | 1,211 | +92 |
| `os8088-120.img` (1.2MB) | 512 B | 1,360 | 1,377 | +17 |
| `apps120.img` (1.2MB) | 512 B | 643 | 735 | +92 |

The two disks the pass was opened for - the 360KB system disk and the 360KB
apps disk - were at 5 and 1 free on the tree the pass was cut from
(`2c6edafc`); `elendilon`'s own work had taken the system disk to 7 by the
time of this merge.

## Every shipped package that changed

The shipped FILE (LZ4 or LZB, what a floppy carries) and image + bss (what
an instance claims, SPEC.md 20.1). `small` is the APP_SMALL arm on the small
disks; `kdos/DOS.O88` is what ships, a loader plus three parts (its
image + bss is the loader's).

| package | file before | after | bytes | image + bss before -> after |
|---|---:|---:|---:|---|
| PiXEL | 113,147 | 105,467 | **-7,680** | unchanged (packed loader) |
| Sheet | 37,029 | 32,072 | **-4,957** | 51,905 -> 51,745 |
| WireBall | 14,315 | 11,393 | -2,922 | 39,176 -> 39,203 (**+27**) |
| Browser | 17,832 | 15,325 | -2,507 | 26,885 -> 25,970 |
| TeXpad | 16,492 | 14,272 | -2,220 | 29,843 -> 29,579 |
| ArtfulType | 15,387 | 13,217 | -2,170 | 29,467 -> 28,887 |
| Dot Delirium | 16,305 | 14,138 | -2,167 | unchanged |
| MIDIRack | 30,991 | 28,965 | -2,026 | 47,484 -> 47,392 |
| Missile | 10,689 | 8,811 | -1,878 | 15,893 -> 14,877 |
| Audio | 10,939 | 9,148 | -1,791 | 18,868 -> 17,875 |
| Telnet | 12,361 | 10,588 | -1,773 | 21,186 -> 20,736 |
| Cyclone | 11,410 | 9,655 | -1,755 | 16,159 -> 15,724 |
| Tank | 13,559 | 11,947 | -1,612 | 22,593 -> 22,381 |
| The Wire | 10,256 | 8,782 | -1,474 | 14,728 -> 14,265 |
| Gorillas | 12,798 | 11,408 | -1,390 | unchanged |
| Skies | 45,991 | 44,610 | -1,381 | unchanged (loader) |
| Chart | 8,849 | 7,483 | -1,366 | 10,930 -> 10,597 |
| FTPD | 12,291 | 11,742 | -549 | 26,624 -> 25,600 |
| RLAPPLY | 1,979 | 1,447 | -532 | 2,282 -> 1,648 |
| Solitaire | 4,259 | 3,738 | -521 | 5,735 -> 5,628 |
| DOS (`DOS.O88`) | 48,172 | 47,660 | -512 | 2,386 -> 2,320 (loader) |
| Note Pad | 15,233 | 14,766 | -467 | 20,133 -> 19,498 |
| Calculator | 4,329 | 3,866 | -463 | 5,255 -> 5,130 |
| Timer | 1,813 | 1,467 | -346 | 2,072 -> 1,603 |
| Task Manager | 7,047 | 6,707 | -340 | 9,747 -> 9,282 |
| Paint | 21,934 | 21,614 | -320 | 32,378 -> 31,873 |
| TameGram | 3,175 | 2,864 | -311 | 3,937 -> 3,818 |
| Mines | 1,678 | 1,405 | -273 | 1,849 -> 1,616 |
| Arkanoid | 5,285 | 5,031 | -254 | 6,509 -> 6,169 |
| DeskCal | 12,659 | 12,422 | -237 | 17,533 -> 17,229 |
| Tracker | 26,498 | 26,268 | -230 | 42,944 -> 42,603 |
| Font Viewer | 3,549 | 3,400 | -149 | 5,923 -> 5,741 |
| Fractal | 2,818 | 2,724 | -94 | 3,511 -> 3,403 |
| Video | 41,163 | 41,124 | -39 | 56,184 -> 56,100 |
| Piano | 3,052 | 3,041 | -11 | 4,095 -> 4,027 |
| Task Manager (small) | 4,175 | 3,305 | -870 | 6,004 -> 4,954 |
| Paint (small) | 15,002 | 14,312 | -690 | 21,370 -> 20,419 |
| Tank (small) | 14,109 | 12,459 | -1,650 | 23,921 -> 23,709 |
| Calculator (small) | 3,353 | 2,891 | -462 | 3,849 -> 3,609 |
| Note Pad (small) | 10,269 | 9,850 | -419 | 13,234 -> 12,742 |
| DeskCal (small) | 12,914 | 12,680 | -234 | 17,853 -> 17,565 |
| Solitaire (small) | 4,067 | 3,938 | -129 | 5,532 -> 5,433 |

**-51,171 bytes of shipped package files** (one copy each, both arms), and
image + bss down on every package bar WireBall's +27 (set C's report says
why). Dot Delirium, Gorillas, PiXEL and Skies are set E/D packages whose
FILES moved only because of tool-level changes (LZB, the packed parted
image); their code was not touched. Off the C toolchain's disks, the same
packed-image change took C64 and the Apple II 8,192 bytes each.

Drivers: `ETHER.DRV` 11,296 -> 11,168 and `SAVER.DRV` 7,388 -> 7,263 (their
shared-include carriers); every kernel module (`CTRL`, `DESK`, `DISKOPS`,
`DOCK`, `EXTD`, `FORMAT`, `HIBER`, `WIN95`, and the small kernel's) is the
same size and differs only by the kernel offsets it is built against.

## The kernel

`.cold` **+33 bytes on each kernel** (kern_big 38,051 -> 38,084; kernel.sys
+32 / +16), and nothing else: it is the loader taking a parted package's
packed image length from the header's link word (SPEC.md 20.13.8). No rung
crossed by the merge. `PKG_FMT` unchanged (no old package sets the pair;
an older kernel refuses the new one as `Bad package`).

**`elendilon`'s own tip crossed kern_big's `.cold` rung (74 -> 75) BEFORE
this merge** - it is the ASSOC.DAT writer's, not the pass's - so the merged
tree has 316 bytes of cold rung left.

## What changed that a user can see

- **Launches of the LZB packages are slower** - the owner's pick in set B,
  ~10 points of ratio for ~4x the decode. Measured on ArtfulType (5150, 360KB
  floppy): first launch 1.60 -> 2.14 s (+541 ms), again 1.22 -> 1.63 s.
  Estimated for the rest at ~+26 ms per KB of image: Calculator +130 ms up
  to Sheet +1.26 s. Seventeen packages ship LZB now: Artful, Audio, Browser,
  Calculator, Chart, Cyclone, Dot Delirium, Gorillas, Missile, Sheet,
  Solitaire, TameGram, Tank, Telnet, TeXpad, The Wire, WireBall.
- **Task Manager**: the heap page's TYPE column shows `Compress`, `Shortcut`
  and `Disk img` whole, and the group headings show owner names whole.
- **Mines**: opened cells draw in two fills - less flash (a flood 136 -> 31
  transient pixels on CGA) and a 35-37% faster flood.
- Everything else is meant to be pixel-identical; the set reports carry the
  hot-path cycle counts (none slower where measured).

## Defects fixed on the way

From the four set reports: DOS (a full COMSPEC path not recognised as the
built-in shell; the packet driver overwriting the client's CX), The Wire (a
status line overrunning into the message on screen), Note Pad (Find opened on
a multi-line selection could search a different pattern from the one shown),
Paint (Save Gif's RAM check asked the wrong question), plus each other set's
own (their reports' Defects sections).

## Open, for the owner

- **SPEC.md 20.13.8.1**: the loader's step-6 read can run up to 511 bytes
  past the region on a hostile compressed package's header, true since
  SPEC.md 20.13; one bound test fixes it at a few resident bytes.
- **The shared-include wins no set could take** are tabled in each set
  report; set A's follow-on built the ones it could as opt-in gates, and the
  per-package savings left for other carriers (Telnet, Word, CWORD, LOOM) are
  in set A's report.
- **Set E** - next round.

## Coverage of the merge

The merged tree built clean (`make`, fast tier 47/47; `make small
smallapps`), `make test-full` **54/54** (with `ctoolchain` run against the
main checkout's toolchain), and the rows both sides of the merge reach -
DeskCal, Sticky Apps, the association layer, the `lz*` family, `dispapps`,
`sheetmove`, `pkgrun`, `pkgbig`, `small128`, `kernresident` - **55/55** in one
soak (`20261016-174537`). Each set ran its own union before landing (see
their reports).
