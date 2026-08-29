# Text without the flash — the plan for `font_str`, `font_char` and the VGA gap

**Standing plan for how os8088 puts TEXT on the screen.** SPEC.md is the
binding contract for what the kernel *is*; this is the plan, in
REDRAW-SPEC.md's shape. Stage 1 has landed and its contract is
**SPEC.md §6.1.10**; everything below that is scoped and not started.

| stage | what | status |
|---|---|---|
| 1 | VGA gets `font_run`'s single-store path | **landed** — SPEC.md §6.1.10 |
| 2 | **make the flickering call unreachable by default** | **landed** — SPEC.md §6.6 |
| 3 | the third answer: compose into a buffer, blit once | **landed** — SPEC.md §5.4.2.2, §44.10.6 |
| 4 | the 189 registered call sites | **started** — §4, and the registry is the tracker |
| 5 | one-pass UNALIGNED runs — SPEC.md §6.1.4 is wrong | **landed** — SPEC.md §6.1.11 |
| 6 | alignment: the apps can move | `make TITLESNAP=1` is the first one, §6.1 |
| — | **tabled until the shape is known** | §7 |

---

## 1. The problem is the DEFAULT, not the primitive

### 1.1 Flicker first, speed second

**A 50 fps picture where ten of those frames draw blank looks far worse than a
25 fps picture drawn solid.** That is the ordering for everything below.
PERFORMANCE.md Part 1 names three defects invisible in an emulator — a visible
redraw, a **double-draw flash**, and input overrun — and this plan is mostly
about the second. Speed matters, and Stage 1's VGA finding was worth having on
its own, but a change that makes text 2× faster and still writes every pixel
twice has not solved the thing that is actually wrong.

### 1.2 The real failure mode is a person having to say "it flickers"

`font_str` is the natural thing to reach for. It is the shortest call, it is
what every existing package uses, and it does the obvious thing. So a new
package, a new window, a new status line gets written with `gfx_fill` + a
`font_str`, and somebody has to notice, say *it flickers*, and send it back.
**That loop has run enough times to be the actual defect.** The primitive is
only its instrument.

This is the same shape as QEMU. QEMU was not deleted; docs/TESTING.md gave it
a **closed, enumerated list** of the cases it is still right for, and said why
the list is a list:

> stated as a list so that "a legitimate need" is something you can check
> rather than something you can argue yourself into … **"It is quicker to
> type" is not on it** … If you find a fifth entry, add it here rather than
> treating the rule as advisory.

That worked. §2 is the same treatment for transparent text.

### 1.3 So `font_str` is a primary target, not a bystander

An earlier draft of this plan said `font_char` is 9% of the problem because it
has 13 call sites against `font_str`'s 128. **That is the wrong reading.**
`font_str` *is* `font_char` in a loop — every one of those 128 sites is a
per-cell transparent walk, and it is the one people reach for. The two are one
target with two spellings, 141 sites:

| | shipped | tests | what it is |
|---|---:|---:|---|
| `font_str` / `font_str_x` | 128 | 45 | transparent, per cell, the default |
| `font_char` | 13 | 2 | transparent, one cell, the engine |
| `font_run` | 85 | 22 | **opaque, one decision per cell** — the answer |

And where the erase lives decides the shape of each fix. It is usually **not**
adjacent:

| | sites |
|---|---:|
| `font_str` with a `gfx_fill` within 25 lines above — the classic pair | **20** |
| `font_str` onto a pane some *enclosing* routine filled | **108** |

The second group is "fill the panel, then letter 28 labels onto it". Every text
pixel is still written twice and the panel is still momentarily blank; the fix
is not "replace the pair" but "make the labels opaque and shrink the fill to
what no label covers".

### 1.4 What Stage 1 was, and why it is not the point

`font_run`'s single-store path was gated on `[vid_mono]`, so on VGA every run
fell through to `gfx_fill` + `font_str` — the pair SPEC.md §6.1 exists to
replace — and **was 3.3% slower than writing that pair by hand**. A run has one
pair of colours, so which planes vary across the glyph is a question the *run*
answers once, not the cell. SPEC.md §6.1.10 is the contract.

| VGA row | before | after | | Hercules |
|---|---:|---:|---:|---:|
| `FONT_RUN 10 aligned` | 7,431.22 µs | **2,993.20** | **2.48×** | 3,163.68 |
| `FONT_RUN 20 padded` | 12,941.87 | **3,467.21** | **3.73×** | 3,798.12 |
| `one full-width row` | 50,348.57 | **17,529.28** | **2.87×** | 18,156.31 |
| `whole page of rows` | 2,554,046 | **741,497** | **3.44×** | 494,331 |

VGA is now faster than the Hercules on every aligned text row. +177 bytes of
`.text` on `kern_big`, +15 on `kern_small`, **no budget
raised** — the 2KB offered for this rework is untouched, and §7.1 is the only
item that would want any of it.

**But nothing above removes a single double-write.** It made the *right* call
cheap on the adapter where it was expensive. Making the *wrong* call unavailable
is Stage 2, and it is the one that stops the loop in §1.2.

---

## 2. Stage 2 — make "it flickers" not the default (LANDED)

**SPEC.md §6.6 is the contract.** Three mechanisms, weakest to strongest; all
three shipped together, and the third is the one that changes behaviour.

Not one shipped byte moved: `build/os8088-360.img`, `apps360.img` and
`kernel.bin` are md5-identical across the whole of Stage 2.

### 2.1 A closed list — SPEC.md §6.6.2

Written the way docs/TESTING.md's QEMU list is written: **here is the whole of
transparent text's remaining list**, enumerated, with a closing line saying a
sixth kind is added *there* rather than treated as advisory. It settled at
**five**: a ground that is not a uniform colour; a ground already correct and
*not drawn this frame*; a pen no constant can reach; the published C surface;
and a harness measuring the primitive itself.

It also names the case that **looks** like the list and is not — text that is
part of a sprite redrawn as a unit. Arkanoid's capsule letter passed for case 2
for years on the strength of "the body is already there", and the body is drawn
*that same frame*. That one is §3.

### 2.2 Strict labels — rename what the call SAYS

`OSAPI_FONT_STR` did not say what it costs. **`OSAPI_FONT_STR_XPARENT` and
`OSAPI_FONT_CHAR_XPARENT`** do, with `os88_font_str_xparent` /
`os88_font_char_xparent` in the C SDK, and an SDK comment beside them saying
`OSAPI_FONT_RUN` is the one you want.

**The slot numbers did not move** — 0x0068 and 0x0060 are what they always were
— so nothing about the ABI changed. `tests/unit/t_api_abi.py` noticed the SDK
name and the kernel routine parting company and refused the build until the
pair was written into its `ALIAS` table, which is that gate working exactly as
intended: the rename is now a thing a reviewer sees rather than a drift.

### 2.3 A build gate — the part that binds

**`tests/unit/t_textrules.py`**, a `fast`-tier row so it runs on every `make`.
It walks `kernel/`, `drivers/`, `apps/` and `tests/` for every transparent call
— the SDK's `_XPARENT` spellings *and* the kernel-internal `font_char` /
`font_str` / `font_str_x` and their `cw_` shims, because `kernel/` and
`drivers/` hold more than a third of the sites and `ctrl.inc` alone has 28 —
and compares per-file counts against **`tests/textsites.txt`**.

Three failures, all three verified by deliberately provoking them:

| provocation | what it says |
|---|---|
| a **new package** calls it | `apps/probe/probe.asm is not registered` — *use font_run, or add a line saying WHICH of 6.6.2's five cases this is* |
| an existing file **gains** one | `drew more than it is allowed` — *the ratchet only turns one way* |
| a conversion lands and the registry is **not lowered** | `is registered too high` — *lowering it is the diff saying the work happened* |

That third one matters more than it looks: without it a file that converts nine
of ten sites keeps a budget of ten, and the tenth creeps back silently.

**It is a ratchet, not a clean gate**, and the registry starts at what the tree
actually has: **189 sites in 61 files**, of which six files carry one of
§6.6.2's five reasons and the rest say `backlog:` and point at where they go.
A rule that cannot be enforced from the day it is written is not enforced at
all. What binds from day one is that the number cannot go **up**.

The precedent is two rows over: `t_asmrules.py` already scans every source for
unreachable code, and the suite's `registry` row already checks that every test
*"is registered somewhere or says why not"*.

---

## 3. Stage 3 — the third answer: compose into a buffer, blit once (LANDED)

**SPEC.md §5.4.2.2 is the primitive and §44.10.6 the first consumer.**
`gfx_blit1` takes an optional `(ink, paper)` pair — slot 0x04A0, dying with the
gfx lock as `[gfx_dis]` does — implemented with §6.1.10's plane grouping, which
turns out not to be specific to glyph rows at all. Arkanoid's falling capsules
are **one blit** where they were seven drawing calls, and the mark is now in the
same write as the body rather than a transparent pass over it. Cost to a caller
that sets no pen: a fixed **~78 µs a band, +0.62%** on `GFX_BLIT1 128x128`
(PERFORMANCE.md Set 77).

Two things the work turned up that the design did not predict:

- **On a 1bpp adapter the pen is not read**, and is *ignored* rather than
  refused. A band on one plane already means lit and unlit; refusing would blank
  the shape on two adapters of three for a caller who set a pen the third one
  wanted, and a silent blank is the worst failure the contract could pick.
- **`gfx_blit1` had a citation to a `SPEC.md 5.4.2.2` that never existed**, and
  `checkdocs` could not see it because the citation is split across two source
  lines — the `SPEC.md ` prefix and the number are never on the same line, so
  the pattern never matches. A wrapped citation is invisible to the doc gate.
  Recorded in §5.4.2.2 rather than fixed; the fix wants testing against the
  whole tree's comment style.

The rest of this section is the reasoning, kept because §4 leans on it.

### 3.0 The argument

`font_run` (opaque, on the screen) and `font_char` (transparent, one glyph) are
not the only two options, and treating them as if they were is what leaves
things like Arkanoid's capsules flickering after the obvious fixes are done.
**The third answer is: build the picture in your own RAM, then put it down in
one call.**

**Most of it already exists.** SPEC.md §6.3 is that method for proportional
type — compose a 1bpp band in package RAM, emit with one `OSAPI_GFX_BLIT1` —
and `apps/os88type.inc` (§6.5) is it written once. `OSAPI_FONT_GLYPHS`
(slot 0x0218) already publishes the kernel's 8×8 bitmaps for exactly this, in
its own words *"for an app that needs the BITMAP of a character rather than a
drawn one — scaling it, **stamping it into a canvas**, measuring a cell."*

So a package can already compose cell text into a band today. What is missing is
a **library** for the 8×8 case (os88type is for `.f88` faces) and one primitive
that does not exist yet.

### 3.1 The worked example: Arkanoid's falling capsules

The user-visible symptom, and the reason this section exists. Per capsule per
frame, `ark_draw_pu` makes **seven drawing calls**: a body fill, four edge
strips, the letter, plus the wipe of the strip it vacated. Three capsules can
fall at once, so up to ~18 ms of an 55 ms tick, and at SPEC.md §5.7's 756 µs
floor most of that is *floor*, not pixels.

§44.10.4 already fixed the worst of it — the body used to be a black rect with
the body inset into it, so every body pixel was written twice a frame — but
**the letter is still a separate transparent pass over pixels the body fill
wrote this frame**. There is an instant, every frame, when the capsule has a
body and no letter. That is the residual flicker.

The fix is exactly the shape asked for:

- Compose each of the five capsule kinds **once, at init**, into a small 1bpp
  sprite — body, edge and letter together, with `OSAPI_FONT_GLYPHS` supplying
  the glyph. 16×10 bits is 20 bytes; five kinds is 100 bytes of package bss.
- Compose the **erase into the sprite**: make the band tall enough to include
  the rows the capsule vacated this frame, with those rows set to background.
  Same trick as SPEC.md §27.2's *"the padding IS the erase"*.
- Emit one `OSAPI_GFX_BLIT1` a frame. **One call instead of seven, and no pixel
  written twice** — so no letter-less instant and no flash.

Two things it needs, and both are decisions rather than obstacles:

- **`gfx_blit1` wants x and width on a multiple of 8.** A capsule falls
  straight down, so its x is fixed for its whole life: snapping it once at spawn
  costs at most 7 px of horizontal position, invisibly, and the body is 12 px
  in a 16 px band. Cheap.
- **`gfx_blit1` is colour-blind** — see §3.2.

### 3.2 The missing primitive, and Stage 1 already built its engine

`gfx_blit1` puts a band down with one `rep movsw` a row, which is what makes it
one call — but the resting VGA state means a set bit is colour **15** and a
clear bit **0**, and there is no ink or paper argument (SPEC.md §5.4.2 pins
this deliberately). `gfx_blit4` *does* carry colour, but it emits **one
`gfx_hline` per coalesced run**, so a 12×10 sprite with a letter in it is fifty
calls, not one. Measured: `GFX_BLIT4 4px runs` is 13,713 µs against `solid`'s
2,276.

**So the compose-and-blit answer carries colour or costs one call, and not
both.** That is a real gap in the primitive set and it is worth naming.

**Can `blit4` be fixed instead? No — and the 1/4 is the source's bit depth, not
a colour flag.** `gfx_blit1` takes a **1-bit-per-pixel** band, `gfx_blit4` takes
**4-bits-per-pixel** packed pixels, two to a byte. That is the whole of the
naming. What differs behind it is the *bargain*, and SPEC.md §5.4.2 states it:
`blit4` takes ordinary pixels and **does the arriving for you**; `blit1` takes
framebuffer bytes you have already decided and does the arriving **once for the
whole band**.

`blit4`'s cost is `runs × ~0.5 ms` and that is its contract working, not a
defect: it emits one `gfx_hline` per coalesced run precisely so it needs *no*
adapter-specific code and honours a clip region for free. **How flat the picture
is, not how big, decides what it costs** — the field figure is the same 64×64
block at one run a row (28 ms) against sixteen runs a row (561 ms).

It can be made *faster* — SPEC.md §5.4.1 already took the arriving out on 1bpp
(`gfx_blit_span`), and §5.4 already scopes the VGA half: *"a plane-parallel VGA
path … would beat this on detailed pictures and lose on flat ones, and would
need its own 1bpp twin; it changes no caller, so it stays available as a later
optimisation."* But it cannot be made **one call**, because one call means the
caller has already decided the framebuffer bytes — and a caller that has done
that is holding a `blit1` band, not a `blit4` image.

**So `blit4` is not this plan's problem.** Its plane-parallel path belongs to
whoever owns detailed multi-colour pictures — Paint's canvas (§42) — and is a
different optimisation with a different trade. The two-colour sprite case is
`blit1`'s, and `blit1` + a colour pair covers it in one pass.

**And Stage 1 is its implementation.** SPEC.md §6.1.10's plane grouping — GC1
takes the planes both colours agree on, GC0 their value, the Bit Mask stays FF
so no latch is read, and the varying planes take the CPU byte — is not specific
to glyph rows. It works for *any* band of framebuffer bytes. Giving `gfx_blit1`
an optional `(ink, paper)` pair costs one `out` pair per call on VGA, nothing on
mono (where there is no colour anyway), and turns it into the missing primitive:
**a two-colour sprite of any size, down in one call, no pixel written twice.**

That serves the capsules, any 2-colour sprite in any package, and coloured
proportional text — which §6.3 cannot do today either.

**Recommend taking this before Stage 4**, because it changes what a call site
should be converted *to*.

---

## 4. Stage 4 — the 141 call sites

### 4.0 What the first pass found, and one correction to this section

**STAGE 4 IS FINISHED — eleven batches, two changes to a primitive, and two
redraw fixes the sweep turned up on the way**, in the order §4.1 sets.
Registry: **189 → 37 sites, 51 files → 17**, and every line that remains
carries a REASON rather than a queue position. Four still spell that reason
`backlog:` — the three in §4.4 and the Tracker's — and §4.4 is why none of the
four is a conversion somebody is going to type.

**The Task Manager's four summary lines** (SPEC.md §28.5.1) — the memory page's
XMS line and the heap page's TOTAL, SPLIT and FRAG. Each was `tm_lfill` (white
the band) plus a `font_str` over it, behind `tm_elchk`, so they redrew whenever
their figure moved — for a live heap, most refreshes. One helper, `tm_lrun`,
replaces the pair at all four: it pads `tm_str` to the band's exact width in
place and draws one opaque run, so the padding *is* the erase (§27.2). The
width is asserted at build time — `TM_LCELLS × 8` must equal `TM_RW − TM_PEN +
1`, and it comes to 27 cells, which is exactly what `tm_str` holds. Registry
`apps/taskmgr` 8 → 4.

**The Control Panel's Date/Time page** (SPEC.md §31.5.1). It erased its
whole field band and re-lettered it **once a second**, which is the worst shape
a double-draw comes in — a one-off flash is a blink, a repeating one is what a
person cannot stop noticing. `cp_time_rows` now takes a flag: the tick draws
**seven opaque runs and nothing else** — no band fill, no separators, no black
box — because every field's text is exactly its own width, so the run *is* the
erase for it. Registry `kernel/ctrl.inc` 28 → 27.

**The file manager's list row** (SPEC.md §22.11.3) — the name and the size, a
`font_str` each, fourteen rows of them on a 320x200 window and forty on a full
screen. Both are opaque runs now: measured on MartyPC, `fm_draw_lrows` entry to
return goes **175,352 → 150,550 cycles, −14.1%** (36.7 ms → 31.5 ms on the
target machine), with the rendered window byte-identical. Registry
`kernel/files.inc` 6 → 4.

**That batch also corrected a rule this plan had been applying too widely.**
Padding the name out to `[fm_nch]` so that "the padding is the erase" (§27.2)
was tried first and measured **178,160 cycles — slower than the transparent
version it replaced**, because it letters eleven spaces a row that nothing
needs. §27.2's shape only pays where **the row is exactly a glyph tall**: a
list row is `FM_ROW_H` = 16 px against an 8-px glyph, so a run of any width
covers half of it and the eight blank scanlines above and below still need
erasing by somebody. The Task Manager's summary lines and the Control Panel's
clock are 8-px rows and the padding is the erase there; a 16-px row keeps its
band fill and takes the opaque run for the *cells*, which is still −14%.

**`menu_drop`'s items** (SPEC.md §12.2.1) — the second most frequently redrawn
surface in the system after a title bar, and every menu open paid the pair. Every
item is one `font_run_x` now, with `thm_pair` supplying the chrome's two colours.
On Hercules the chip menu goes **417,618 → 244,346 cycles, −41.5%** (87.5 → 51.2
ms), the greyed cell −30.9%, the third bar cell −17.5%; on VGA −27.0%, −19.9% and
−8.5%. Every drop byte-identical on both adapters, checkerboard included.

**And that batch found §6.6.2's sixth case by measuring the wrong version
first.** Written uniformly — every item a run, greyed ones included — it is
correct, and the all-greyed menu went **+13.4% on Hercules**: `font_run` tests
`[gfx_dis]` itself and falls to its own `gfx_fill` + `font_str_x`, filling ground
`menu_drop` laid four instructions earlier. A third write of a strip that already
took two, arriving through the fix for exactly that.

**The sixth case lasted one commit.** Rather than teach every caller to remember
it, the primitive gave it up: §6.1.12 folds §47's checkerboard into the run's own
mask — the mono composition is `(glyph & xm) ^ bm`, so masking the glyph first is
`glyph & (xm & d)` and the row loop does not change. **43 bytes of `.text`**, and
the greyed menu went from +13.4% to **−30.9% against the pre-sweep kernel**. Every
menu item is a run again, `kernel/menu.inc` leaves the registry, and §6.6.2 is
back to five cases with the episode written down. *A case every caller has to
remember is worth one hard look at the primitive first.*

**The Control Panel, all of it** (SPEC.md §31.11) — **27 sites, the largest
population in the tree, down to 0.** Every heading, row label, caption, driver
name and status line goes through one five-instruction helper, `cp_run`, which
takes the ink from `[gfx_color]` and the paper from the pane. **Reading the pen
rather than taking it in a register is what made the conversion mechanical**:
each site already set `[gfx_color]` and then called the shim, so each became a
`cp_run` without touching the line above it — and the greyed rows came along
with no special case, because §6.1.12 had just landed. The page list's rows are
the one exception and carry their own pair, a selected row being white on a
black bar.

It cost **one byte of `.text`**: a near call inside `ctrl.inc`'s segment where
`cw_font_str` was a far call to the shim, so twenty-six sites paid for the
helper. Every page byte-identical to the build before it, and the Sound page's
greyed rows — at an unaligned pen, which is the case §6.1.12 shipped unverified
— byte-identical to `make NOUNAL=1`'s pair (PERFORMANCE.md Set 82).

**...and then the Control Panel's CONTROLS** (SPEC.md §31.1.3), which is not a
text conversion at all but was two feet away from one. Every page redrew its
whole control set on **both** edges of a press: measured, the Sound page's three
radios are **395,130 cycles — 82.8 ms — per edge**, so one click on one radio
drew six glyphs and about a sixth of a second of the target machine's time to
change the look of one. `cp_ctlpaint` is a two-pass driver now, and the filter
lives in `cp_dngly`/`cp_dnbtn` because those are the only two routines that know
a control's id and they already run immediately before every control this window
draws. **395,130 → 160,501, −59.4%**, for **+2 bytes**, every state
byte-identical.

**And it named the biggest unclaimed number in the sweep** — one 12×12 control
glyph at **24–34 ms**, because `os88ui_glyph` plotted its picture *one drawing
call per set bit*, 44 for a ring and 64 for a crossed box. **That is now one
call: 160,501 → 31,992 cycles, 33.6 ms → 6.7 ms, −80.1%** (SPEC.md §25.6,
PERFORMANCE.md Set 84). Against where this window started, a Sound-page press
edge is **395,130 → 31,992 cycles, −91.9%**.

**And the way it was done is the lesson, not the number.** The instinct was a
masked `gfx_blit1` — new code in the hottest primitive in the system — or a
caller that declares the ground around a control, which writes columns the
caller does not own and breaks silently the first time two controls sit within
12 px of each other. Neither was needed. `ico_core`, icons.inc's icon pass, is
**already** a masked 1bpp sprite engine: a mask-row underlay and a data-row
overlay, at any x, on either adapter, clipped, leaving everything outside the
mask alone. It had two colours hardcoded into it. What the whole change needed
was `icon_pen` — two bytes — and then a control glyph is a record whose mask
rows are its box and whose data rows are the bitmap `os88ui_glyph` already held.

*Look at the primitive before teaching every caller a rule* — §6.6.2's case 6
again — **and before writing a new primitive, grep for the one that already does
it.** `OSAPI_ICON_DRAW` publishes it, so a package's controls are one call too;
FTPD's checkbox is byte-identical through it.

**Word's chrome** (SPEC.md §68.14) — the application §4.1 called the higher
value, because it is the one people type in. Word draws its own menu bar,
pull-downs, ribbon and ruler, and every string on them was transparent. Seven
sites are opaque runs now: a dropped menu's **item label** and its **accelerator
caption** (the surface that redraws on every menu open), both combos' captions,
and `Font:`/`Pts:`/`Style:`/`1.5` on the two strips. Measured end to end —
breakpoints on `font_run_x` and `font_str_x`, summed across one menu open —
**672,382 → 386,329 cycles, −42.5%** (140.9 → 80.9 ms), the same seventeen
calls. Two different menus diffed whole: byte-identical, greyed items and their
stippled captions included. Registry `apps/word` 20 → 13.

**The one thing a package cannot do, and the shape that answers it.**
`OSAPI_GFX_PEN` takes CF and sets the colour *and* `[gfx_dis]` together, and
there is no `OSAPI_GET_COLOR` — so `cp_run`'s trick of reading the pen back does
not port. A package converting a site that inherits its pen has to decide the
run's ink at the same branch that decides the CF and keep it; Word keeps it in
one byte. The *flag* stays the pen's, so §6.1.12 does the greying for free.

**ArtfulType's chrome, all of it** (SPEC.md §46.10) — §46.5's whole point is that
the app draws its own Macintosh, and all eight of its text sites were
transparent: the fullscreen **bar's** titles (whose pair *inverts* in Writer
mode, so it is built where the pen was and kept), a **pull-down item**'s label
and its right-aligned shortcut, the alert box's message lines, and `at_ctext`'s
three About lines. All opaque runs now, and byte-identical across the bar and
all four pull-downs. Registry `apps/artful` 8 → 0.

**And it found a §47 rule 1 violation that this batch deliberately does NOT
fix** (§46.10.1): a disabled item is a bare `CLGRAY` store with no `[gfx_dis]`,
which §39.4 reduces to **solid black** on a 1bpp adapter — so on the target
machine a dead item looks live. The fix is one line, and it is not here because
no state this session could drive put a disabled item on screen to diff it
against, and it would have shipped unverified beside work that was verified.

**`HDD.DRV`'s two surfaces** (SPEC.md §52.12) — the Control Panel's Disks page
and the Disk Tool window, eleven sites, the last of §4.1 item 5's big
populations. Nine are constants (black on the pane's white) and name their pair
outright; the two selected device rows are white on a bar they have just
filled, and keep their pair in `[hd_pair]` — a **driver** cannot read
`[gfx_color]` back any more than a package can. All five of `tests/hddcp`'s
captures byte-identical. Registry `drivers/hdd` 11 → 0.

**And it took a pixel off the C/H/S editor, which is worth naming as a general
hazard.** The selected field's inner frame ends on exactly the row the value's
last cell row occupies. A transparent glyph left it alone — that row of a digit
is blank — and an opaque run paints it, so the ring lost its bottom. The fix is
ordering, not geometry: chrome wins, so the frame is drawn *after* the value.
**Whenever a run replaces a glyph inside a frame, check whether the cell and the
frame share a row** — a box is sized to its text plus a margin, and a margin of
one is common.

**The kernel's tail** — seven sites across four files, which takes the KERNEL
to one: `wm_draw_title`, and that is §3 work rather than a conversion.

| | |
|---|---|
| the **Standard File dialog** (SPEC.md §38.12) | every string it draws goes through one helper, `fdlg_text`, and the ink is read back from `[gfx_color]` rather than passed — so seven call sites did not change. `cp_run`'s shape again. Its selection is §38.3's XOR band drawn *after* the rows, which is what makes the paper a constant |
| the **desktop's drive captions** (§26.6) | the white label rect and the caption in it were the pair, redrawn for every drive on every repaint and every selection change |
| the **file manager's** header, both buttons and the icon-grid caption | the content's own white |
| `os88ui_apaint`'s **alert message** | a content the window manager has just filled |

Desktop, Disk window in **both** views, and the file dialog all byte-identical.

**What is left in the kernel is not convertible and says so on its line**:
`kernel/kernel.asm`'s one is `cw_font_str`'s SHIM and `apps/os88ui.inc`'s two
are `UI_STR`'s macro bodies — none of the three is a call site, and all three go
when `font_str` itself does. `kernel/bootprof.inc`'s three are `BOOTPROF=1`
only, the one surface no shipping build draws.

**And a correction: `wm_draw_title` is not a call-site conversion.** §4.1 below
listed it first on the strength of "a `gfx_fill` and a centred `font_str` on
every window operation", and reading the whole routine says otherwise. It
already writes the caption band **three or four times**: the bar is filled
white, six stripes are drawn across it (four of them cross the caption band),
the white gap is filled over those stripes, and only then is the caption
lettered. Converting the `font_str` alone would treat the smallest of those.

The real answer for a title bar is **§3's**: it is a 16-row two-colour picture,
which is exactly what `gfx_blit1` plus §5.4.2.2's pen now draws in one call. The
obstacle is bss — a full-width band is 16 rows × 90 bytes = 1,440 bytes, and
`KERN_BUDGET` has none of that spare. Composing it in horizontal chunks, or
composing only the caption-and-gap band, are both open. **It is Stage 7 work,
not Stage 4 work**, and it should not be attempted as a one-line conversion.

**The Task Manager's headings and its last unconverted caption** (SPEC.md
§28.5.2, §28.5.3) — the three pages' column headings, and `tm_txt_ram_y`, the
one caption in this window still drawn as `tm_lfill` + `font_str`. It also
lettered from `[tm_cx] + 6` rather than `TM_PEN`, which §28.5's move had left
behind, so it became the **fifth `tm_lrun`** and both problems closed together:
the pen aligned, the fill gone, and `tm_lfill` deleted with its last caller.
`TMM_HSQ_X` moves 105 → 108 so the claim swatch stays in the two-space gap —
centred in it exactly now, which its comment had always claimed.

Measured on one view-switch paint (PERFORMANCE.md Set 86): the 27-cell RAM
caption **118,711 → 40,056 cycles, −66.3%**, the 25-cell heading **100,607 →
31,069, −69.1%**, the whole page's 98 text calls **249.2 → 217.5 ms, −12.7%**.
The 96 unchanged row chunks land within **0.3%** across the two builds, which
is what makes the other two figures the change rather than the weather.

**Read those two differently, and the difference is the finding.** The heading's
pen was 8-aligned in both builds, so **−69.1% is opacity alone**. The RAM line
got opacity *and* alignment in one commit — `+6` is 6 mod 8, so every cell of it
straddled two bytes and none could take `font_run`'s single-store path. When a
conversion also moves a pen, say which half is which.

**Fractal's status strip** (SPEC.md §40.2.2) — the name, `Zoom`, the exponent,
the percentage and the palette, five fields over a band `fr_status` had already
white-filled. The band is `FR_STRIP_H` = 10 against an 8-px glyph, so §22.11.3
applies and **the fill stays**; the runs buy the cells. The exponent becomes a
**one-cell string** rather than an opaque `font_char`, which does not exist and
does not need to — a run of one character is the same call, and `fr_numbuf` is
free four instructions before the percentage composes into it. `fr_status` and
`fr_nowork` no longer set the pen at all. Registry `apps/fractal` 6 → 0,
`apps/taskmgr` 4 → 0; `taskmgr.o88` −59 bytes, `fractal.o88` +15.

**And a harness limit worth more than the number it cost.** The strip was to
have been measured the same way and cannot be: `os88mouse` polls the guest's own
cursor, a guest stopped at a breakpoint never moves it, and **this package
letters continuously** — 83 progress ticks landed inside one attempt. A raw
drag *was* delivered and `fr_status` still did not draw, because its opening
`OSAPI_WM_CLIP_TEST` refuses the whole strip when an edge cuts it and a window
being moved is exactly that. So the strip is verified by pixels — byte-identical
across three captures a full render apart — and priced from §28.5.2. *Before
arming a breakpoint pair on a package, ask whether that package ever stops
drawing.*

**The tail, all of it, in one batch** (SPEC.md §6.6.5) — forty-odd sites one
and two at a time across twenty-five files: every About box in the tree, seven
driver Control Panel pages, Word's dialogs, Paint, Notepad, Recorder, Frotz's
splash, ModPlug, Tamegram's HUD, the Tracker's channel numbers, the kernel's
own About window, and thirteen unshipped harnesses. None was worth a batch of
its own and all of them were the same four shapes, which is why they went
together:

| shape | what it needed |
|---|---|
| a helper that already **set its own pen** (`ec_txt`, `sc_txt`, `net_txt`, `eu_txt`, `dbg_page_row`, `hl_line`) | one `(CWHITE << 8) \| CBLACK` and a run; the pen store goes with it |
| a helper that **inherited** one (`rp_str`, Word's dialog controls, `pt_ctext`, `mc_textc`) | the ink written down at the branch that decides §47's flag — three new one-byte cells, because there is no `OSAPI_GET_COLOR` |
| a helper with **two callers and two grounds** (`tg_str`, `mppu_strr`) | two entries sharing one guard: `tg_run`/`tg_str`, `mppu_run` and no twin at all |
| a **single character** (`wd_btn12`, `tui_charc`) | a two-byte buffer and a one-cell run — there is no opaque `font_char` and no reason to add one |

**Three things it had to be careful about**, each of which cost something:
a pen store is **not dead** just because the text stopped reading it (Notepad's
Find panel sets `CBLACK` for the label *and the box frame*); **AL is not
inherited** across a `mov ax, [something]` (Tamegram's HUD loads the score
between two of its eight fields, so every one names both halves of its pair);
and §52.12's **frame hazard** was checked at each of the five sites that has
one rather than assumed.

**And it found §6.6.2's sixth case.** Five sites in four files — Word, Texpad,
both of Frotz's window layers and `apps/cword` — are a **bold overstrike**: one
typeface, so bold is the same string struck again a pixel right, and a second
*run* would repaint the cells' ground and erase the first strike. Those two
passes cannot be collapsed by any primitive, which is exactly what distinguishes
this case from the sixth case that was retired. They had been `backlog:` lines
claiming work somebody would do; they are a case now.

### 4.4 What is left, and why none of it is Stage 4

**37 sites in 17 files**, and the registry is now a list of decisions rather
than a queue:

- **six files** carry §6.6.2's correctness cases 2, 3 and 5 — Mines, Piano,
  Solitaire and the three benches that *price* the pair;
- **one** is case 1, a ground that is not a colour: Tamegram's banner, which
  lands on the matrix;
- **five** carry the new case 6, the overstrike;
- **one** is `apps/os88ui.inc`, and its three are the SDK's own: `UI_STR`'s two
  macro bodies, one per build, and `ui_button`'s invocation of it. All three go
  the day `font_str` does;
- **three** say `backlog:` and none of them is a conversion — Arkanoid's capsule
  letter is §3.1's sprite, `kernel/bootprof.inc` draws on a surface no shipping
  build has, and `wm_draw_title` is §3's compose-and-blit job;
- **one** is the honest odd one out: the Tracker's `tui_runc` takes the run on
  mono and `tui_textc` on colour, where `font_run`'s fallback is a `gfx_fill` of
  ground the pattern view filled once for the whole row band. That is a **cost**
  argument, not a correctness one — the same shape as the retired sixth case —
  so it is registered with its reasoning and the note that the fix, if it comes,
  belongs in `font_run`.

**A new `backlog:` line is now a claim that somebody is going to do it**, which
is what the word should have meant all along.

**Two files joined afterwards and are not part of this account.** Sheet and
Chart (SPEC.md 81, 83) were written on `main` while stage 4 was being written
here; they reached the registry at the merge, at the 21 sites they had, with
one mixed line each. They are the ratchet doing its job on a tree it arrived
in, not stage 4 leaving work behind.

### 4.1 Priority is redraw frequency, not site count

A panel drawn once when a window opens costs a user one flash. A status field
redrawn on every keystroke costs them all of them. PERFORMANCE.md Part 5's
standing budget is the bar for anything already listed there.

1. ~~**`wm_draw_title`**~~ — **moved out of Stage 4**, see §4.0. It is a
   compose-and-blit job, not a conversion.
2. ~~**`menu_drop`'s items**~~ — **done**, see §4.0. The one thing the plan had
   wrong: the drop's fill does *not* shrink to the frame and the gaps.
   `MENU_ITEM_H` is 16 px against an 8-px glyph, so a run covers half a cell
   however wide it is made — §22.11.3's rule, which this batch is the second
   confirmation of.
3. ~~**`kernel/ctrl.inc`**~~ — **done**, see §4.0. It was 28 sites and it is 0:
   one helper, `cp_run`, and the pen read from `[gfx_color]` rather than passed,
   which is the shape to copy for any other file with a page painter in it.
4. ~~**`apps/word`** and **`apps/taskmgr`**~~ — **both done**, see §4.0. Word's
   CHROME is converted; what is left there is the dialogs (drawn once when they
   open), the ruler's inch digits (§6.6.2 case 1) and the bold overstrike, which
   must ADD to the glyphs under it and so can never be opaque.
5. ~~**`kernel/files.inc`** (6)~~ — **done**, see §4.0: the list row is two
   opaque runs and the four that remain are the dialogs. Then
   ~~**`drivers/hdd/page.inc`** (8)~~, ~~**`apps/artful`** (8)~~,
   ~~**`apps/fractal`** (5)~~ — **all done** — and the tail.
6. **What is left is the tail**, and it is 111 sites in 49 files: Word's
   dialogs (13), the registered §6.6.2 cases (19 across five files), the
   unshipped harnesses (36), `kernel/bootprof.inc`'s knob-only three,
   `font_str`'s own shim and `UI_STR`'s two macro bodies, `wm_draw_title`
   (Stage 7), and **one or two sites each in twenty shipped packages and
   drivers** — a dialog line here, a status field there. No population left
   is worth a batch of its own; they go by file, cheapest first, and each is
   verified the same way: open it, diff its window, lower the registry line.

**The nine packages that call `FONT_STR` and never `FONT_RUN`** are named in
PERFORMANCE.md Set 29 — Paint, ArtfulType, Solitaire, Arkanoid, Fractal,
Recorder, Piano, Minesweeper, Tamegram. Parts of that list have since been
worked (Arkanoid's status strip is one `font_run` now), but the shape holds:
most of the system's text does not reach the fast path.

### 4.2 Each conversion is the same four questions

Getting the third wrong adds a pass instead of removing one; getting the fourth
wrong is what §3 exists for.

1. Is the background under this text a **uniform colour**? If not, it is either
   transparent-legitimate (§4.3) or a compose-and-blit (§3).
2. Can the enclosing `gfx_fill` **shrink or disappear** once the text is opaque?
3. Does the run **pad to its field**? SPEC.md §27.2 — the padding *is* the
   erase, so a shortened string erases its own tail with no fill, and §6.1.8's
   `rep stosb` makes the padding nearly free. **Only where the row is exactly a
   glyph tall**, though: a run is 8 scanlines however wide it is made, so on a
   taller row the band fill survives the padding and the padding is then
   lettered spaces nobody needed. Measured at +1.6% on a 16-px row —
   PERFORMANCE.md Set 79 — against −14.1% for the same conversion unpadded.
4. Is this text part of a **sprite** that is redrawn as a unit? Then it is §3,
   and converting the text alone leaves the flash.

### 4.3 The closed list — draft

The 13 `font_char` sites, re-read. **These are candidates for change, not
protected**: nothing here is written in stone, and the question for each is
whether it is on a path that redraws.

| site | what | verdict |
|---|---|---|
| `arkanoid` capsule letter | the letter on a falling capsule | **change** — §3.1, the worked example. Its only `font_char`, and it flickers every frame |
| `word` toolbar button letter | a letter on a button face just framed | **change** — a 2-cell `font_run` on a known ground |
| `fractal` zoom digit | one digit of a status row | **change** — the row's other fields are already runs |
| `mines` ×3 | the adjacency digit, `+4,+4` in a 16 px cell | **keep** — drawn once per reveal, and SPEC.md §11.94.4 protects the centring by name |
| `solitaire` ×2 | a card's rank, `'1'`+`'0'` for the ten | **keep** — drawn on deal and move, over a card face already down |
| `piano` key letter | `key_x + 5`, off a run-time-scaled keyboard | **keep** — repaint-only, and no constant can align it (SPEC.md §11.98.1) |
| `word` ruler inch digits | over the ruler's tick pattern | **keep** — genuinely transparent: the ground is a pattern, not a colour. §3 if the ruler ever redraws hot |
| `tracker` `tui_charc` | one pattern-grid cell | **check** — SPEC.md §45.17 says the per-frame text self-aligns; confirm this is not on it |
| `cc/os88thunk` | `os88_font_char()` | **keep** — the C SDK's published surface (SPEC.md §73) |

So the list that survives is roughly: **text over a genuinely non-uniform
ground** (Word's ruler), **text drawn once over a ground already down**
(Solitaire, Minesweeper), **a pen no constant can reach** (Piano), and **the
published C surface**. That is the shape §2.1's SPEC list should state — and
five or six entries is a list somebody can check.

---

## 5. Stage 5 — SPEC.md §6.1.4 is wrong about runs (LANDED)

**SPEC.md §6.1.11 is the contract, and §6.1.4 now carries a superseded note
pointing at it.** An unaligned run is one pass: `n−1` plain stores and two edge
merges a row, against the erase-and-letter pair it used to take.

| `FONT_RUN 10 skewed` | before | after | | the pair, same skew |
|---|---:|---:|---:|---:|
| Hercules | 11,038.40 µs | **5,288.62** | **2.09×** | 10,784.53 |
| VGA | 8,337.76 | **4,989.13** | **1.67×** | 8,080.12 |

**A skewed run is 1.66× an aligned one on both adapters now**, where it was
3.5× and 2.8×. But the speed is the smaller half: the old path was the *pair*,
so every unaligned run in the system — all the centred chrome included — was
writing every text pixel twice and showing the gap.

**Verified pixel-identical against `make NOUNAL=1`** on both adapters, reading
the finished picture at `font_run_x.out`. PERFORMANCE.md Set 78 also records
three ways the measurement went wrong first, all of them general: `fbuf` is the
card's rasterised frame and lags a halted guest; sampling a fixed number of
cycles after a run *starts* photographs the pair half-drawn; and a breakpoint
that fires mid-click strands the mouse's release packet.

**Cost: +262 bytes of `.text`, and it crossed `kern_big`'s rung by 77 of them**
— `KERN_BUDGET` 116,224 → 116,736, one 512-byte step of the 2KB offered.
`kern_small` does not carry it, for §6.1.10's reason: that build already takes
`.slow` for every VGA run it draws, and its budget is deliberately the tighter
of the two.

The rest of this section is the argument, kept because it is what §6.1.4 got
wrong and the correction is worth being able to re-read.

### 5.0 The arithmetic

§6.1.4 — *"Unaligned cannot be made fast"* — argues:

> At an unaligned x an opaque cell row spans two framebuffer bytes and the
> neighbours' bits in both of them must be preserved, so the write becomes
> read-both / merge / write-both: **4 accesses per cell row, 320 per
> ten-character run**.

**True of a CELL, false of a RUN.** In a run the "neighbours" whose bits must be
preserved are, for every cell but the last, *the next cell of the same run* —
bits the run owns and is about to write anyway. A run of `n` cells at an
unaligned x covers `n+1` framebuffer bytes, of which `n−1` are **wholly** inside
it. Only the two end bytes have foreign neighbours:

| | accesses per row | ten cells, eight rows |
|---|---:|---:|
| §6.1.4's figure | 4 per cell | 320 |
| a run: `n−1` stores + 2 read-merge-writes | `n+3` = 13 | **104** |

The shift is one instruction per cell row and no table: with `k = x & 7`, output
byte `j` is `(c[j−1] << (8−k)) | (c[j] >> k)`, which is what
`mov ah, c[j−1]` / `mov al, c[j]` / `shr ax, cl` leaves in `AL`. An 8088 charges
8+4k, so 12–36 clocks against an aligned cell row's ~30–40 — an unaligned run
should land at **1.5–2× an aligned one**, against the 2.8–3.5× the fallback
costs today. The two ragged end bytes are **column-major, not per row**: set the
mask once, walk eight rows, exactly as `gfx_fill_gray` already does with
`.lcol` / `.irow` / `.rcol`.

**This is what makes Stage 4 unconditional.** With it, converting a call site to
`font_run` gets the single-store path whether or not its pen is on the grid — so
no conversion is blocked on a layout argument, and §6's protected off-grid pens
stop costing anything. §6.1.4 now carries a superseded note saying exactly which
sentence was wrong; the section is kept because the *cell* argument is sound and
is why `font_char` is what it is.

**So Stage 6 is now optional everywhere.** Alignment is still worth 1.66×, but
it is no longer the difference between one pass and two — which is why the
window-title question can be settled on how it LOOKS, with nothing else riding
on it.

---

## 6. Stage 6 — alignment: the apps can move

An off-grid pen is not a fact of nature. A button that sits at x ≡ 3 can be
moved to x ≡ 0, and the question is only whether it is worth it — which turns
on **how often that text redraws**, not on whether the constant looks tidy.
Drawn once and left alone: leave it. On a path that repaints: worth moving.

Stage 1 **raised the price** of being off-grid, because before it there was no
fast path on VGA to miss:

| | aligned | skewed 5 | penalty |
|---|---:|---:|---:|
| VGA, before Stage 1 | 7,431.2 µs | 8,312.6 | 1.12× |
| **VGA, after** | 2,993.2 | 8,337.8 | **2.79×** |
| Hercules | 3,163.7 | 11,038.4 | 3.49× |

### 6.1 The one candidate that matters: centred chrome

**`wm_draw_title` is the hot one** and it is not in SPEC.md §11.94.3's survey at
all, because that survey is about what *apps* do and this is chrome. The title
is centred — `(W_W − width)/2 + W_X` — so it is off-grid roughly seven times in
eight, and it redraws on **every window operation**.

The decision to take, and it is a visual one: **centre to the nearest 8 px**.
That moves a title by at most 4 px from true centre, on an 8×8 cell font, in a
16-row bar.

**`make TITLESNAP=1` is that, on a knob**, because it is a look question and the
right way to settle one is to look. Default off; `wm_draw_title` rounds its pen
with `add cx, 4` / `and cx, 0FFF8h`, which is *nearest* rather than leftward and
is correct on a title wider than its window too (the pen is signed there, and
the two's-complement round behaves). The white gap is computed **from** the pen
so it moves with the text and stays centred on it — the gap is not separately
snapped and must not be.

Worked example, a Disk window at `W_X` 103, `W_W` 320, title `Disk` (32 px):
the pen is `103 + (320−32)/2 = 247`, which is ≡ 7 — off-grid — and snaps to
**248**, a **+1 px** move. That was verified on VGA against the shipped kernel
of the day — 80 pixels differ, all inside the title band — and **which build
the shipped kernel is has moved twice since**, which is the next paragraph.

**WHICH BUILD THE KNOB IS VISIBLE ON HAS CHANGED TWICE, so check before
concluding it does nothing.** `TITLESNAP`'s only occurrence in the tree is
`kernel/wm.inc:7228`, inside the fifteen-call path — below the `call
wm_title_band` / `jnc .sep`, which is compiled in only by `make BAND=1`
(SPEC.md §5.9.6). The composed bar centres its caption itself, so wherever
that path is in the build the snap is assembled and never reached: measured,
plain against `TITLESNAP=1` in one session while the composer was kern_big's
default, **0 differing pixels of 307,200**.

§5.9.6 has since sent the composer back to a knob, so the fifteen calls are
what a default `kern_big` draws again and **`make TITLESNAP=1` against `make`
moves the pen once more** — the reading above was a fact about that cycle's
default and not about the knob. The build that now hides it is `BAND=1`, and
`KERN_SMALL=1` never did. Run the A/B on a build that takes the fifteen calls,
or it will look as though the snap were invisible rather than absent.

SPEC.md §11.94.3 lists "a centred string" as one of three off-grid pens that are
*correct*; this narrows that to **"centred, to the nearest cell"**, which would
also reach `ui_note`, the About box and Missile's banner if it is adopted.

**The knob only buys alignment, not the flash.** On the fifteen-call path
`wm_draw_title` is still a `gfx_fill` plus a `font_str_x`, so it still writes
every title pixel twice; what the snap buys is that the *conversion* to
`font_run` (§4.1 item 1) will land on the fast path when it happens. The two
are independent and the conversion is the bigger half. On a `BAND=1` build the
flash is gone by the other route entirely — §3's compose-and-blit, which
SPEC.md §11.101 is: every pixel of the bar written once, into a 1bpp band, and
blitted. That was the shipped bar for a cycle and is a knob again on bytes
(§5.9.6), which puts the flash back on the default build's title bar and makes
the conversion above worth more, not less.

### 6.2 Two things I had wrong

Worth stating because the earlier draft of this plan said otherwise and someone
would have gone and "fixed" them:

- **The menu bar and its pull-downs are already 8-aligned by construction.**
  `menu.inc:574` — *"the text is centred — and 8-aligned, since BX is and a
  glyph width is a multiple of 8, so every cell after this one is too"* — and
  `menu_x1` is that same `MB_XL`, with items at `menu_x1 + 8`. So §4.1's item 2
  is a pure conversion with no layout change. (The one exception is the clamped
  path at `menu.inc:1758`, where a drop near the right edge shifts left; check
  whether that clamp should round to 8.)
- **Arkanoid's status strip is already one `font_run`**, space-padded to a fixed
  width. Its remaining flicker is the capsule letter, which is §3.1 and not an
  alignment question at all.

### 6.3 Do not re-run the survey

SPEC.md §11.94.3 and §11.94.4 are the app-side census and its closure — three
entries mattered (Fractal, the Disk window, Tamegram), all three are done, and
everything else turned out to be centring, a frame rect miscounted as a pen,
geometry derived at run time, or a handful of cells on an event. `make
SNAPAUDIT=1` (SPEC.md §11.94.2) is the instrument if a specific window is
suspected again; it names the routine, not just the bucket. What §6 adds is that
"it is centred" and "it is a control's own geometry" are now **reasons to
consider moving the control**, where §11.94.4 treated them as closed.

---

## 7. Tabled — bring these back when the shape is known

### 7.1 The second plane pass, and `kern_small`

Both are decisions to take at the end, against the finished shape, not now.

- **One pen in the system still needs two passes**: `CDGRAY` on `CLGRAY`, the
  Color theme's *disabled* chrome (8 against 7, sharing no plane in either
  direction). It takes `.slow`. A second pass is a Map Mask split, ~60 bytes.
- **`kern_small` does not carry the planar prologue**, on `gfx_blit1`'s
  precedent (SPEC.md §5.4.2). The precedent fits less well here — `blit1` is a
  feature a 128KB machine can do without, this is the *speed* of a path it has
  anyway — and a `kern_small` machine with a VGA card draws text at 2.5× the
  cost for want of ~162 bytes, which is one 512-byte rung and inside the 2KB
  offered.

### 7.2 What BECOMES legacy

The interesting version of the legacy question is not *what is dead now* but
**what stops being needed** once Stages 2–5 land. Candidates to look at then,
none of them actionable yet:

- **`font_str` / `font_str_x` themselves.** If the closed list in §4.3 settles
  at five or six entries, a *string* spelling of transparent text may have no
  callers left — the survivors are single glyphs. The API slot cannot be
  retired (a slot is a pinned address, SPEC.md §20.1) but the kernel body and
  the SDK define can go.
- **`font_run`'s `.slow` path.** It exists for unaligned runs, planar
  two-pass pairs and clip-cut runs. Stage 5 removes the first, §7.1 the second.
- **`font_run_scell`.** Only reachable from `.slow` and the clip-cut path.
- **`font_char_bb`'s blank-row skip.** *Not* a removal candidate — it is the
  mono renderer's only content-dependent branch and is deliberately absent from
  the opaque path (PERFORMANCE.md Set 26). Do not "unify" the two loops.

**What is already done:** the pre-GUI text shell's five relics — `video.inc`,
`keyboard.inc`, `string.inc`, `gfx.inc`, `kernel-shell.asm.bak`, 614 lines — are
deleted. They cost 0 bytes of every build; the argument was grep, and that
CONTRIBUTING.md had to carry a standing rule telling every arrival to ignore
them. The API surface itself is tight: all 142 slots have a consumer and none is
reachable only from `tests/`.

---

## 8. How to check any of this

- **Flicker is the assertion, and it has an instrument.**
  `os88marty.py flicker` counts transient pixels and frames across an operation
  — PERFORMANCE.md Parts 3.1/3.2 are its manual, and SPEC.md §12.9's menu-bar
  entry is the worked example (*9 frames flashing, worst 534 transient pixels* →
  *1 frame, worst 37 — and those 37 are the mouse arrow*). **A conversion's
  result is a frame count and a transient-pixel count, not a µs figure.**
- **`tests/gfxbench` for the speed half**, on `os8088_xt_vga` *and* a mono
  machine. `FONT_CHAR one cell` and `PAIR 10 aligned` are the control rows on
  every change in this plan; a harness that moves them is measuring itself.
- **Do not quote a sub-1% mono delta from `gfxbench`.** On Hercules its own raw
  `VRAM write word` row — a loop with no kernel code in it — moves 10% between
  two kernels, because MDA writes contend with the CRTC and a differently-sized
  kernel boots onto a different retrace phase. PERFORMANCE.md Set 76 has the
  table. VGA is free of it.
- **Verify pixels, not appearance**, and compare a **located** difference rather
  than a hash: the menu-bar clock's last cell changes whenever a capture crosses
  a minute, so a differing MD5 with a 7×7 bounding box at the top right is the
  clock. Run the fixture under `THEMEDARK=2` too — the Color theme is the only
  one that exercises a non-trivial plane group.
- **Run `tests/dispcheck.py` by hand** for anything touching `viddet.inc` or
  `vidsel.inc`. It is a `soak` row, so neither `full` nor `fast` runs it, and it
  is the only thing in the tree that exercises the live video block on a machine
  with two of them. It caught Stage 1's one real defect, which five identical
  VGA framebuffers had nothing to say about.
- **`python3 tools/kernsize.py`** before and after, both builds. The rung, not
  the byte count, is what costs a machine anything.
