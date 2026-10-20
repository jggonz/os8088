# What each side added since the last squash, in bytes

**A measurement, not a description.** Taken 2026-09-11 on a four-core cloud
container with `nasm` 2.16.01. It is true of the three commits it names and of
no other tree; a later measurement is a new file, not an edit to this one.

Every figure below is read off a **clean build of that commit in its own
worktree** — `make` and `make small`, no shared `build/`, no on-demand targets
— so nothing here is a source-line estimate and nothing is contaminated by a
neighbouring build.

## The three points

| | commit | date | build no. | what it is |
|---|---|---|---|---|
| **B** | `2ce1e37` | 2026-09-08 | 146 | the last squash of elendilon into main |
| **M** | `52d482b` | 2026-09-09 | 150 | `origin/main` — four PRs on top of B |
| **N** | `65af745` | 2026-09-11 | 184 | `elendilon-next` — cut from M, this cycle's work |

`git merge-base N M` is `52d482b` exactly, so the two sides do not overlap and
the arithmetic is a clean three-point split: **main's side is B→M**, **ours is
M→N**, **overall is B→N**.

**The build number is the commit count (SPEC.md 14.2)**, and it differs at all
three points, so three bytes of `.text` move for that reason alone. It is
inside the measurement noise of every table below and is called out where it is
the *only* thing that moved.

## Headline

| | kern_big `.text` | kern_big `KERN_SIZE` | kern_small `KERN_SIZE` | shipped floppies | built artefacts |
|---|---|---|---|---|---|
| **main** (4 PRs) | **±0** | **±0** | **±0** | **±0 bytes** | +46,948 |
| **ours** | **−1,544** | **−1,536** | **−4,096** | +3 disks, +332,800 bytes of payload | +57,573 |
| **total** | **−1,544** | **−1,536** | **−4,096** | +3 disks | +104,521 |

Two sentences carry most of it. **Main's four PRs cost the kernel nothing and
the shipped floppies nothing** — both new packages ride on-demand disks of
their own. **Our side gave kernel RAM back on both builds** while adding three
floppies' worth of software: 1,536 bytes off every kern_big machine and 4,096
off every kern_small one.

## 1. What each side added

### main — four PRs, all app-side

| PR | what | built size | ships on |
|---|---|---|---|
| #161 | Picture decoders: `.PIX`/`.BMP`/`.PCX` into one 4bpp form (`apps/os88img.inc`) | — (an include) | — |
| #162 | SCRIBE — a fork of WORD as its own package | 43,897 | its own disk |
| #171 | APPLE2 — an Apple II+ emulator in C | (C toolchain) | `make apple2disk` |
| #176 | Release zip carries the Apple II, PaccMan and Scribe disks | — | — |
| | `imgtest.o88`, #161's gate package | 3,051 | nothing (built, not shipped) |

Source: **69 files, +65,586 / −67 lines**, of which `apps/scribe` is +27,774
and `apps/apple2` +23,896.

### ours — this cycle

Grouped by what the change was *for*, not by commit order:

* **DOT DELIRIUM** (SPEC.md §93) — a new game, 15,782 bytes, `apps/dotdel`
  +11,727 lines. Back on the small floppies too (§24.5.5).
* **CLEAR SKIES** grew +7,571 bytes (`apps/skies` +3,754 / −1,045).
* **ArtfulType** gained a dock box and the system clipboard (§46.5.2, §46.6.1,
  §46.5.3, §46.7.1) — +734 bytes.
* **The hourglass** (SPEC.md §7.5) — a busy pointer for a frozen machine.
* **`mem_regrow` sheds a purgeable claim before refusing** (§50.6.2.1, §27.6.1)
  — the field's *"kern_small says Too big opening README.TXT"*, which was two
  defects each hiding the other.
* **Three category floppies** at 360KB (§24.6) — office, network, games.
* **The small system disk carries the whole apps payload** (§24.5.6): 128KB is
  a single-disk machine now.
* **The graphics arc landing**: the `gfx_line` family out of the kernel into
  `apps/os88gfx.inc` (+710 lines), and the glyph/control convergence in
  `CTRL.DRV`.
* **Test and harness work** — `tests` +16,250 / −2,512, and the soak's ten
  failures closed.

Source: **269 files, +66,028 / −9,302 lines.**

## 2. Kernel bytes

Both builds, every section, read off `kernsize`.

### kern_big

| | B (squash) | M (main) | N (ours) | main | ours | total |
|---|---|---|---|---|---|---|
| `.text` (resident code) | 51,068 | 51,068 | 49,524 | **+0** | **−1,544** | **−1,544** |
| `.bss` (resident data) | 6,077 | 6,077 | 6,016 | +0 | −61 | −61 |
| `.cold` | 38,974 | 38,974 | 39,244 | +0 | +270 | +270 |
| `.lowbss` | 9,182 | 9,182 | 9,182 | +0 | +0 | +0 |
| `.vgabuf` | 848 | 848 | 848 | +0 | +0 | +0 |
| `.ovl` | 1,417 | 1,417 | 1,417 | +0 | +0 | +0 |
| `.ovlw` | 5,052 | 5,052 | 5,052 | +0 | +0 | +0 |
| **`KERN_SIZE`** | 112,128 | 112,128 | 110,592 | **+0** | **−1,536** | **−1,536** |
| `.text`+`.bss` (segment) | 57,145 | 57,145 | 55,540 | +0 | −1,605 | −1,605 |

### kern_small

| | B (squash) | M (main) | N (ours) | main | ours | total |
|---|---|---|---|---|---|---|
| `.text` | 39,469 | 39,469 | 37,445 | **+0** | **−2,024** | **−2,024** |
| `.bss` | 4,871 | 4,871 | 4,242 | +0 | −629 | −629 |
| `.cold` | 27,380 | 27,380 | 26,176 | +0 | −1,204 | −1,204 |
| `.lowbss` | 6,064 | 6,064 | 5,460 | +0 | −604 | −604 |
| `.ovl` | 423 | 423 | 423 | +0 | +0 | +0 |
| `.ovlw` | 2,789 | 2,789 | 2,789 | +0 | +0 | +0 |
| **`KERN_SIZE`** | 79,872 | 79,872 | 75,776 | **+0** | **−4,096** | **−4,096** |
| `.text`+`.bss` (segment) | 44,340 | 44,340 | 41,687 | +0 | −2,653 | −2,653 |

**Main's kernel figures are identical at every single line, on both builds.**
Not "near enough" — the same integers. Four PRs that touch no kernel byte.

Ours is a **reduction on both**, and the two are not the same reduction:
kern_big gives back 1,536 bytes (three 512-byte rungs uncrossed) and kern_small
4,096 (eight). The kern_small figure is larger because that build also sheds
`.cold`, `.lowbss` and `.bss` — the VGA reclaim and the module split reaching
the floor machine, where kern_big only sheds resident code.

**Bytes, not rungs** (CLAUDE.md's rule): the `.text` figures above are what was
actually spent and saved. The rung counts are what the guard happens to bill
today.

## 3. Disk space

### Shipped floppies

| image | B (squash) | M (main) | N (ours) |
|---|---|---|---|
| `apps.img` | 752/2847 cl, 33 f | 752/2847, 33 f | 778/2847, 31 f |
| `apps120.img` | 752/2371, 33 f | 752/2371, 33 f | 778/2371, 31 f |
| `apps360.img` | 346/354, 32 f | 346/354, 32 f | **313/354, 28 f** |
| `apps720.img` | 388/713, 33 f | 388/713, 33 f | 401/713, 31 f |
| `media360.img` | 43/354, 1 f | 43/354, 1 f | 45/354, 1 f |
| `os8088.img` | 531/2847, 35 f | 531/2847, 35 f | 527/2847, 35 f |
| `os8088-120.img` | 531/2371, 35 f | 531/2371, 35 f | 527/2371, 35 f |
| `os8088-360.img` | 274/354, 35 f | 274/354, 35 f | 272/354, 35 f |
| `os8088-720.img` | 274/713, 35 f | 274/713, 35 f | 272/713, 35 f |
| `office360.img` | — | — | 169/354, 16 f |
| `network360.img` | — | — | 69/354, 7 f |
| `games360.img` | — | — | 120/354, 10 f |
| **totals** | **9 images, 3,891 cl** | **9 images, 3,891 cl** | **12 images, 4,271 cl** |

**Main changed no shipped floppy**, and that is checked rather than inferred:
`cmp` says the four apps images and `media360.img` are **byte-identical** at B
and M, and the four system disks differ by **exactly 7 bytes** — the build
number 146→150 inside the LZ-packed kernel, and nothing else.

### The 360KB set — the geometry that binds

Clusters are 1,024 bytes here (512-byte sectors, 2 per cluster).

| | disks | clusters | payload |
|---|---|---|---|
| squash / main | 3 | 663 | 678,912 bytes |
| ours | 6 | 988 | **1,011,712 bytes** |
| delta | **+3** | **+325** | **+332,800** |

The important half is not the total, it is the **headroom**. `apps360.img` sat
at **346 of 354 clusters — eight spare** at both B and M, which is the pressure
§24.6 exists to answer. It is **313 of 354 — forty-one spare** now, *while* the
360KB machine gained three more floppies of software. What moved off it:
CHART, FONTVIEW, HELLO, PACMAN and SHEET; what moved on: DOT DELIRIUM.

### kern_small's own disks

| | B | M | N |
|---|---|---|---|
| `small360.img` | 137/354 cl, 12 f | 137/354, 12 f | **247/354, 25 f** |
| `small.img` | 44/357 cl, 12 f | 44/357, 12 f | 76/357, 25 f |

Thirteen more files on the 128KB machine's system disk — §24.5.6's
single-disk change, and the most user-visible line in this report.

**Those file counts are the RECIPE's, not the volume's**, and the two differ
by exactly one: `os88disk.py` reports the files it was HANDED when it builds,
and generates an `ASSOC.DAT` on top of them (it writes one for any packages it
is given), so `--verify` counts **13** and **26** on the same two disks. The
delta of thirteen is the same either way; a reader who mounts the floppy and
counts is not reading a different disk.

## 4. Packages and drivers

Built `.o88` and `.DRV` bytes, `PKGZ=lz4` as shipped.

### Main

| artefact | B | M | delta |
|---|---|---|---|
| `scribe.o88` | — | 43,897 | **+43,897** |
| `imgtest.o88` | — | 3,051 | +3,051 |
| everything else | | | **±0** |
| **total** | 393,803 | 440,751 | **+46,948** |

Neither new artefact ships on a shipped floppy: both are named in `all`
directly, the way `wire.o88` is (SPEC.md 78.9).

### Ours

| artefact | M | N | delta |
|---|---|---|---|
| `word.o88` | not built | 40,077 | **+40,077** — now ships, on `office360` |
| `dotdel.o88` | — | 15,782 | **+15,782** |
| `skies.o88` | 36,658 | 44,229 | +7,571 |
| `artful.o88` | 15,726 | 16,460 | +734 |
| `missile.o88` | 10,448 | 10,718 | +270 |
| `tank.o88` | 13,724 | 13,993 | +269 |
| `cyclone.o88` | 11,186 | 11,418 | +232 |
| `taskmgr.o88` | 7,242 | 7,411 | +169 |
| `ctrl.drv` | 6,100 | 6,243 | +143 |
| `pacman.o88` | 5,437 | not built | **−5,437** |
| `scribe.o88` | 43,897 | 43,754 | −143 |
| `wire.o88` | 2,750 | 2,422 | −328 |
| *~20 others* | | | **−97 to −123 each** |
| **total** | 440,751 | 498,324 | **+57,573** |

**That row of small negatives is one change, not twenty.** Every package that
includes `apps/os88ui.inc` lost about 110 bytes at its next build, because
`os88ui_glyph`'s body changed inside `%ifndef OS88UI_NOBTN` — eleven packages
and five drivers, no per-package work
(`docs/plans/completed/CTRL-GLYPH-PLAN.md`). It is the cheapest thing in this
report: **−2,214 bytes across the tree for one routine edited once.**

## 5. Totals

| | kern_big resident | kern_small resident | shipped floppies | built artefacts | source |
|---|---|---|---|---|---|
| **main** | ±0 | ±0 | ±0 bytes (7-byte build no.) | +46,948 | +65,586 / −67 |
| **ours** | −1,605 | −2,653 | +3 disks, +332,800 payload | +57,573 | +66,028 / −9,302 |
| **overall** | **−1,605** | **−2,653** | **+3 disks** | **+104,521** | **+131,479 / −9,234** |

("resident" is `.text`+`.bss`, the quantity `KERN_CODE_MAX` bounds.)

The shape of the cycle in one line: **main added software and spent nothing
resident; we added more software and gave resident bytes back on both
kernels.**

## 6. One thing this measurement found

`PACMAN.O88` is **no longer built by `make`**, and the Makefile comment beside
it says it is:

> `make` still BUILDS build/pacman.o88 - it is only the disk lists this leaves.

At M, `$(BUILD)/pacman.o88` was in `APPS_GAMES`, so `all` built it. On our side
it came out of every disk list — the owner's decision, to make room for DOT
DELIRIUM — and nothing else names it, so `apps/pacman/pacman.asm` is now
assembled by no default build. Nothing ships wrong; the package rots silently
if it breaks, which is incident 19's shape in `docs/WRITING-TESTS.md`.

The fix is one line — `$(BUILD)/pacman.o88` on `all`, beside `wire.o88`, which
is exactly the "built but does not ship" idiom the file already uses. It
changes no shipped byte.
