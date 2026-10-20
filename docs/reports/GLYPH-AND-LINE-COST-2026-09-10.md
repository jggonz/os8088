# What the graphics arc cost, in kernel bytes, app bytes and guest cycles

**A measurement, not a description.** Taken 2026-09-10 on a four-core cloud
container — `nasm` 2.16.01, MartyPC at the pinned commit, guest figures off an
`os8088_5150_herc_gla` (a 4.77 MHz 8088 with a Hercules) and a second pass on
`os8088_xt_vga`. It is true of the three commits it names and of no other
tree; a later measurement is a new file.

It answers the three questions the owner set in
`docs/plans/completed/CTRL-GLYPH-PLAN.md` §4:

> * How many bytes came out of kernel?
> * How many bytes went into each app? (ram usage and compressed disk size increase)
> * How much performance was gained/lost in the converted calls?

## The three points

| | commit | what it is |
|---|---|---|
| **A** | `2324ede` | `origin/elendilon`, the branch's base. `git merge-base origin/elendilon HEAD` |
| **B** | `a05c463` | *gfx: the LINE FAMILY is out of both kernels* — the end of the **embeddable-graphics** arc (GFX-EMBEDDABLE waves 1–8) |
| **C** | `9de9ac4` | *ctrl: the last two blank-then-redraw sites* — the end of the **glyph and Control Panel** arc |

So **A→B is the graphics arc** and **B→C the glyph arc**, and they are read
separately throughout because they push in opposite directions: the first
moves kernel code into the apps that use it, the second removes code from both
at once.

Every kernel figure is `tools/kernsize.py --json`, which re-assembles rather
than reading a build, so each point is measured on its own tree. Every package
figure is its header's `image` + `bss` (what the loader claims, SPEC.md 20.1)
for RAM and the **file on the floppy** for disk — which is not the image, since
`PKGZ ?= lz4` compresses it. **`KERN_BUDGET` is 129,536 at all three points**:
nothing below is a budget move.

---

## 1. Bytes out of the kernel

### 1.1 `kern_big`, the shipped default

| section | A | B | C | **A→B** | **B→C** | **total** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 51,066 | 49,016 | 48,870 | **−2,050** | **−146** | **−2,196** |
| `.bss` | 6,081 | 6,016 | 6,016 | **−65** | 0 | **−65** |
| `.cold` | 39,175 | 39,175 | 39,220 | 0 | **+45** | **+45** |
| `.ovl` / `.ovlw` / `.lowbss` / `.boot2` | — | — | — | 0 | 0 | 0 |
| **sum** | 96,322 | 94,207 | 94,106 | **−2,115** | **−101** | **−2,216** |
| **`KERN_SIZE`** | 112,128 | 110,080 | 110,080 | **−2,048** | 0 | **−2,048** |
| heap start (`kend`, paras) | 7,104 | 6,976 | 6,976 | **−128** | 0 | **−128** |

**The accrued figure is −2,216 bytes and the footprint moved by −2,048** —
four 512-byte rungs off the image rung, which is 2,048 bytes of *every*
machine's RAM back on the heap. The `.cold` +45 is the glyph arc's Control
Panel work, and `.cold` is the boot overlay's rung, not the image's.

### 1.2 `kern_small`, the 128KB floor machine

| | A | B | C | **A→B** | **B→C** | **total** |
|---|---:|---:|---:|---:|---:|---:|
| `kernel.bin` | 76,005 | 75,493 | 74,981 | **−512** | **−512** | **−1,024** |
| `HEAP_PARA` (boot) | 4,864 | 4,832 | 4,800 | **−32** | **−32** | **−64** |

**A rung each**, and the second one is the finding worth keeping: the glyph
arc's `.text` −146 crosses a rung on `kern_small` and crosses nothing on
`kern_big`, so a change that reads as free on the shipped kernel gave the
floor machine 512 bytes. That is CLAUDE.md's *design for bytes, never for
rungs* seen from the other side — the byte was worth taking, and where it
landed was not the reason.

### 1.3 …and what that did to the floppies

| | A | B | C |
|---|---:|---:|---:|
| `KERNEL.SYS` (lz4) | 85,200 | 83,840 | 83,760 |
| …in sectors | 167 | 164 | 164 |
| system disk, 360KB | 273/354 | 271/354 | 271/354 |
| system disk, 1.2MB | 529/2371 | 527/2371 | 526/2371 |
| apps disk, 1.44MB | 766/2847 | 769/2847 | 768/2847 |
| apps disk, 360KB | 317/354 | 318/354 | 318/354 |

The kernel lost **three sectors** of a compressed `KERNEL.SYS` and the system
disk two clusters; the apps disk gained two, which is the graphics arc's three
games paying for what the kernel gave up. **Net across both disks: near zero,
against 2,048 bytes of resident RAM returned.** That is the trade the brief
described, arriving.

---

## 2. Bytes into each app

### 2.1 Packages — RAM (`image` + `bss`) and the compressed file

| package | RAM at A | A→B | B→C | **total** | disk at A | A→B | B→C | **total** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `ARTFUL` | 41,965 | 0 | −116 | **−116** | 15,726 | 0 | −105 | **−105** |
| `AUDIO` | 30,174 | 0 | 0 | **0** | 7,468 | 0 | −107 | **−107** |
| `BROWSER` | 19,794 | 0 | −116 | **−116** | 13,053 | 0 | −111 | **−111** |
| `CALC` | 7,351 | 0 | −116 | **−116** | 5,289 | 0 | −115 | **−115** |
| `CYCLONE` | 16,793 | **+639** | 0 | **+639** | 11,186 | +235 | 0 | **+235** |
| `FTPD` | 28,160 | 0 | 0 | **0** | 12,744 | 0 | −115 | **−115** |
| `MINES` | 2,448 | +93 | 0 | **+93** | 1,779 | +20 | 0 | **+20** |
| `MISSILE` | 15,372 | **+724** | 0 | **+724** | 10,448 | +272 | 0 | **+272** |
| `NOTEPAD` | 20,453 | 0 | −116 | **−116** | 15,292 | 0 | −114 | **−114** |
| `PAINT` | 33,390 | +138 | −116 | **+22** | 21,977 | +65 | −97 | **−32** |
| `PIANO` | 5,098 | 0 | −116 | **−116** | 3,366 | 0 | −108 | **−108** |
| `RECORDER` | 7,866 | 0 | −116 | **−116** | 3,107 | 0 | −108 | **−108** |
| `SHEET` | 51,684 | −34 | −116 | **−150** | 36,801 | −7 | −98 | **−105** |
| `TANK` | 30,920 | **+537** | 0 | **+537** | 13,724 | +259 | 0 | **+259** |
| `TELNET` | 31,378 | 0 | −116 | **−116** | 12,440 | 0 | −102 | **−102** |
| `TEXPAD` | 36,567 | 0 | −116 | **−116** | 19,142 | 0 | −123 | **−123** |
| `THEWIRE` | 15,515 | 0 | −116 | **−116** | 9,936 | 0 | −104 | **−104** |
| `WIRE` | 4,982 | **−370** | 0 | **−370** | 2,750 | −328 | 0 | **−328** |
| **total** | | **+1,727** | **−1,276** | **+451** | | **+516** | **−1,407** | **−891** |

### 2.2 Drivers — RAM is the whole image (bss ships inside, SPEC.md 51)

| driver | RAM at A | A→B | B→C | **total** |
|---|---:|---:|---:|---:|
| `CTRL.DRV` | 7,397 | 0 | **+129** | **+129** |
| `ETHER.DRV` | 17,413 | 0 | −4 | **−4** |
| `HDD.DRV` | 7,959 | 0 | −116 | **−116** |
| `HDDTOOL.DRV` | 12,567 | 0 | −116 | **−116** |
| `NET.DRV` | 6,355 | 0 | −116 | **−116** |
| `RAMPAGE.DRV` | 3,463 | 0 | −116 | **−116** |
| `SAVER.DRV` | 13,797 | **+206** | −116 | **+90** |
| **total** | | **+206** | **−455** | **−249** |

### 2.3 What those two tables actually say

**The graphics arc cost +1,933 bytes of app RAM in five programs and nowhere
else** — Cyclone +639, Missile +724, Tank +537, Paint +138, Mines +93, plus
`SAVER.DRV` +206 — against −2,115 of kernel. And **it made one program
smaller**: `WIRE` −370, because `apps/wire/` had `GFXE_BAND` and `GFXE_LINE`
hand-rolled and the shared library replaced both (SPEC.md 78.5.1). `SHEET`
−34 the same way.

**The glyph arc took bytes off everything at once.** `os88ui_glyph`'s bitmap
machinery is 116 bytes a copy, and it came out of **eleven packages and five
drivers** as well as the kernel — sixteen copies here, and there are more in
the tree that this build does not carry (`WORD`, `CWORD`, `WEAVE`, `LOOM`,
`PACCMAN`, `C64`, `FROTZ`). CTRL.DRV is the only thing that grew, by **+129**,
and it is an on-demand module (§2.8): those bytes are a floppy's, present only
while the Control Panel is open.

**Net over both arms: +451 bytes of app RAM, −891 of app disk, −2,216 of
kernel.** The brief's trade — *expensive kernel space for cheaper disk and
Control Panel space* — came out better than that, because the second arc
bought back most of what the first spent without touching the kernel's side
of the bargain.

**Three rows do not mean what they look like, and each is a property of the
package rather than of this work.**

- **`FTPD` shows 0 RAM and −115 disk.** Its `FD_BSS equ fd_stage + FD_STGSZ −
  os88_image_end` sizes the bss to reach a fixed address, so an image that
  shrinks by 116 grows the bss by 116 and the loader's claim never moves. The
  saving is real and reaches the floppy; it does not reach the heap.
- **`AUDIO` shows 0 RAM.** Its image is the same length at all three points
  and 455 of its bytes differ — it is padded, so a saving inside it cannot
  show as a size.
- **`SKIES` is absent.** Its `.o88` is 49,216 bytes at every point and its
  parts are fixed extents, so the file cannot change length; it *did* change
  (the two binaries differ), and by how much is not readable this way.

---

## 3. What the converted calls cost

### 3.1 The instrument

`tests/glyphbn` carries **both implementations in one package** — the
pre-13.15.1 `os88ui_glyph` lifted verbatim out of `apps/os88ui.inc` at
`2324ede` as `gbo_glyph`, beside today's — so an arm differs from its twin in
the code being timed and in nothing else: one kernel, one boot, one adapter,
one point on the screen, the same clip. **A cross-checkout A/B could not say
that**, because this arc removed the whole `gfx_line` family from the kernel
underneath.

Spans are MartyPC's cycle counter between two marker addresses. The counter
does not advance while a breakpoint holds the guest, so a span is exact guest
cycles rather than a host timing. Eight calls an arm; an empty arm of the
identical shape is subtracted. `tests/glyphcost.py` is the row and it was
**verified red** by putting four extra box fills into `os88ui_glyph`.

### 3.2 `os88ui_glyph`, per call, on a 4.77 MHz 8088

Hercules (`os8088_5150_herc_gla`):

| | bitmap | fills | change | Δ ms |
|---|---:|---:|---:|---:|
| check, clear | 29,491 cyc / 6.18 ms | 20,388 / 4.27 ms | **−30.9%** | −1.91 |
| check, set | 29,000 / 6.08 | 25,685 / 5.38 | **−11.4%** | −0.69 |
| radio, clear | 28,712 / 6.02 | 21,572 / 4.52 | **−24.9%** | −1.50 |
| radio, set | 28,712 / 6.02 | 30,584 / 6.41 | **+6.5%** | **+0.39** |

VGA (`os8088_xt_vga`), the same bench:

| | bitmap | fills | change |
|---|---:|---:|---:|
| check, clear | 24,397 / 5.11 ms | 16,323 / 3.42 | **−33.1%** |
| check, set | 23,088 / 4.84 | 18,685 / 3.92 | **−19.1%** |
| radio, clear | 22,934 / 4.81 | 16,612 / 3.48 | **−27.6%** |
| radio, set | 22,215 / 4.66 | 24,130 / 5.06 | **+8.6%** |

**THE PLAN'S OWN EXPECTATION WAS WRONG, AND IN OUR FAVOUR.**
`docs/plans/completed/CTRL-GLYPH-PLAN.md` §2 priced this as *"what it COSTS is calls —
eight for a set radio against the sprite pass's one"*, and the owner took the
trade on look and on size expecting to pay for it. **Three of the four kinds
got faster** and only the set radio is dearer, by 0.39 ms — well inside the
owner's *"between 1ms and 2ms is not huge"*. The masked sprite pass is one
drawing call and composes twelve mask-and-data rows to make it; five fills
beat it, and eight roughly tie it.

**Two figures that were in the source are wrong and are corrected in place.**
`ctrl.inc` and `os88ui.inc` carried *"~35–50 ms of the field machine's time"*
for a glyph, in five places and two respectively. That number is 44–64
`gfx_pixel` calls at ~756 µs — the arithmetic of **`.gpix`, the CLIPPED
fallback**, and never what the normal path did. The normal path was **6.0–6.2
ms** and is **4.3–6.4 ms** now. Nothing that rested on those comments changes:
*don't redraw a control that did not change* is the same argument at 6 ms as
at 40.

The spread between runs is ~1–3% and is IRQ0: a ~50 ms bracket takes about one
18.2 Hz tick, and the span carries it. It is not worth removing — it is
smaller than any difference above that the report leans on.

### 3.3 …and the two do NOT draw the same glyph

`tests/glyphcost.py` draws all four kinds twice, bitmap column and fill
column, and reads both off the framebuffer. Only the open square comes out
identical:

| kind | pixels differing of 144 |
|---|---:|
| radio, clear | **40** |
| radio, set | **56** |
| check, clear | 0 |
| check, set | **32** |

The radio is the one to look at. **The bitmap was a circle; the fill is a
square with its four corner pixels nipped off** — `os88ui_gring` is four runs
(SPEC.md 13.15.1), and four runs cannot make an arc:

```
         bitmap                    fills
      ....####....              .##########.
      ..##....##..              #..........#
      .#........#.              #..........#
      .#........#.              #..........#
      #..........#              #..........#
      #..........#              #..........#
      #..........#              #..........#
      #..........#              #..........#
      .#........#.              #..........#
      .#........#.              #..........#
      ..##....##..              #..........#
      ....####....              .##########.
```

**This is a look change that shipped, and it is the owner's to accept or
reject.** SPEC.md 13.17.1's rule — *the corners must be clear, so it is not a
rectangle* — is satisfied by both, so `tests/radio.py` passes on either: the
rule turns out to be too weak to tell a ring from a nipped square. What it
would cost to put the circle back **as fills** is the useful half of the
finding: the bitmap ring's twelve rows coalesce into **12 fills** against
`os88ui_gring`'s 4, which at the ~854 µs a fill this bench measures is about
**11 ms a glyph** — worse than the bitmap's 6.2 and nearly three times
today's 4.5. A middle shape (two-pixel corner cuts) is 8 fills, ~7 ms.

So the three options, measured or priced from measured parts:

| | fills | ms/glyph | shape |
|---|---:|---:|---|
| today | 4 | **4.52** | square, corners nipped 1px |
| a rounder outline | 8 | ~7 | octagon, corners cut 2px |
| the old circle, drawn with fills | 12 | ~11 | the bitmap's own arc |
| *(the bitmap itself)* | 1 sprite | 6.02 | the bitmap's own arc |

**Nothing has been changed on the strength of this.** It is a look question
with a price attached, which is what the measurement was for.

---

## 4. The three answers, in one place

| the question | the answer |
|---|---|
| **bytes out of the kernel** | **−2,216 accrued**, footprint **−2,048** (four rungs) on `kern_big`; **−1,024** and two rungs of heap on `kern_small`. `KERN_BUDGET` unmoved |
| **bytes into each app — RAM** | **+1,933 in five programs and one driver** (the graphics arc), **−1,731 across sixteen** (the glyph arc). **Net +451** |
| **bytes into each app — DISK** | **+516 then −1,407**, net **−891** compressed. Two clusters onto the 1.44MB apps disk, two off the 360KB system disk, three sectors off `KERNEL.SYS` |
| **the converted calls** | `os88ui_glyph` is **30.9%, 11.4% and 24.9% FASTER** for three of its four kinds and **6.5% slower** for the set radio (+0.39 ms). The design was expected to cost time and does not |

**And one thing the brief did not ask for, which is the most actionable line
in the file**: the fill-drawn radio is not the shape the bitmap drew, and
section 3.3 is what it would cost to change that.
