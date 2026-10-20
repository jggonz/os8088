# Content coordinates — the kernel owns a package's origin and its clip

**Status: SHELVED by the owner, 2026-10-13 - kept as the costed design, not
scheduled.** The reason is the budget and not the design: kern_big has no
~800-1,000 resident bytes to give it, so packages keep their own origin
arithmetic and the new-session reminder stays a reminder. Re-open it when a
kernel size pass has funded the bytes; every figure below is still the one to
start from, measured at `e73b3224`.

**Status before that: PLAN. Nothing here is built into the tree.** A measuring rig was built
on the side - a prototype kernel arm, an SDK switch and three converted packages
- and every figure marked MEASURED below comes off it, on MartyPC's 4.77 MHz
5150 (CGA, GLaBIOS), at `e73b3224`. The rig is NOT in the tree: it is
`docs/plans/content-coords-rig.patch`, which applies to that commit and carries
its own build and run commands (§12). Figures marked ESTIMATED are arithmetic
against those measurements and say so.

The ask, in the owner's words: *"That sounds like it would solve this once and
for all, and I'd stop having to manually point out to every new session
developing an app that they are drawing outside their window / can't use screen
origin to draw from."* And then: work out **both** a design with **no flag day**
and a design where **the ABI may break**, and compare them.

So this is a CORRECTNESS-BY-DEFAULT question first and a bytes question
second, and the answer is laid out in that order.

---

## 0. The answer in one page

| | today | design A: NO FLAG DAY (§5) | design B: ABI BREAK (§6) |
|---|---|---|---|
| what a package passes | screen coordinates | content coordinates, **if it declared so** | content coordinates, always |
| can it draw outside its content box through a slot? | yes, silently | **no** for a declared package; yes for an undeclared one, and a ratchet makes undeclared the exception | **no**, any package |
| an old `.o88` off somebody's floppy | runs | **runs, unchanged** | refused at load (`PKG_FMT` 8 → 10), "Load failed" |
| kernel bytes, each of kern_big / kern_small / kern_emu | - | rig **+1,001 / +998 / +1,001** `.text` (MEASURED), production ~950-1,050 (ESTIMATED) | ~800-850 (ESTIMATED: A less the twin table, plus raw kernel doors) |
| a SCREEN call (kernel, undeclared package) | - | **+0 cycles** (the cells are separate) | not applicable: no screen calls from packages |
| a CONTENT call over a screen one, rig | - | rect **+432**, 1-cell glyph **+466**, 8-cell run **+720**, 16x8 blit1 **+474** cycles (MEASURED) | the same shapes; ~50 cycles less if each hot cell names its shape directly (ESTIMATED) |
| a whole first paint, rig | Piano 241.7 ms, Calc 174.6 ms | Piano **+1.7%**, Calc **+4.2%** (MEASURED, cleaned twins) | the same |
| a callback of a content package | - | **+3,006 cycles** to set and restore its DC (MEASURED, rig); ~1,000 single-derivation (ESTIMATED) - work the package stops doing itself (§3.3) | the same, for every package |
| package source to migrate | - | **one line a package** (MEASURED: Piano and Calc flipped with no other change, pixel-identical through open and clicks), its §7.2 leaks, the test scripts that read its origin words, then an optional clean-up | none for the flip - the rebuild IS the flip - but every package's leaks and all 42 such test scripts in the same commit |
| package bytes back | - | 0 for the flip; **-53 to -181 image** a package for the clean-up (MEASURED on three); ~3-7 KB tree-wide (ESTIMATED) | the same, plus worker `CLIP_SET`s folded into the lock |

**Recommendation (§10): design A as the vehicle, design B as its last wave.** A
lets every package move on its own commit, each proven pixel-identical before
it lands, and lets nothing outside the tree break; when the ratchet list is
empty, B is a cheap flag day that takes the twin table back out. Done that way
the whole tree reaches B's guarantee without ever having been in a state where a
hundred packages changed meaning in one commit.

**The one decision only the owner can make is kern_small** (§8): the feature is
ABI, so it must exist on the 128KB kernel too, and SPEC.md 39.27.4 says nothing
may be spent there. The rig lands kern_small's `KERN_SIZE` on **64,000 of a
64,000 budget, exactly**.

---

## 1. What the tree does today

Every `OSAPI_GFX_*` and `OSAPI_FONT_*` slot takes **screen** coordinates and
clips to the **screen** - or to SPEC.md 11.3's clip region when one is armed,
which the kernel does for a `W_PAINT` inside a damage pass (11.3.3's cull) and
for nothing else. A package therefore:

1. calls `OSAPI_WM_CONTENT` (and `OSAPI_WM_GEOM` - always as a pair) at the top
   of every callback, because SPEC.md 11.96.12's drag cache means a moved
   window never calls its painter and a cached origin is a stale one;
2. adds the origin to every coordinate of every drawing call;
3. subtracts it from every click (`W_ONCLICK`/`W_ONMOUSEUP`/`W_ONDRAG`/
   `W_ONRCLICK` hand CX/DX in screen coordinates) and from `OSAPI_MOUSE`;
4. clamps to its content box itself, or draws over its own frame and the
   window beneath when its layout outgrows the box;
5. arms `OSAPI_WM_CLIP_SET` before drawing from anything but `W_PAINT`, or draws
   over the window on top of it.

**The census** (`tests/dcrig/dcsurvey.py` and `dccsurvey.py` in the rig patch,
§12; a call site is a `call OSAPI_*` line, or an `os88_*(` call in C, with
comments stripped):

| | packages that draw | drawing calls | `WM_CONTENT` | `WM_GEOM` | `WM_CLIP_SET` | `GFX_LOCK` |
|---|---|---|---|---|---|---|
| `apps/` (assembly, incl. 7 shared includes) | 50 + 7 | **880** | 98 | 71 | 98 | 83 |
| C packages (`os88_*` calls) | 7 | **228** | 19 | 19 | 25 | - |
| `tests/` packages | 43 | 168 | 69 | 17 | 12 | 4 |
| `drivers/` | 5 + `wirezone.inc` | 31 | 6 | 3 | 0 | 3 |

27 assembly packages and 5 C packages hire a worker (SPEC.md 20.6), so 32
packages draw from a context no dispatch sets up.

### 1.1 What it has cost — the incident record

Every one of these was found from scratch, most of them in the field:

| where | what happened |
|---|---|
| SPEC.md 11.3.1 | Note Pad's worker drew its scroll bar after a close and after a minimize - "across whatever the window was covering" |
| SPEC.md 11.100.8 | Word, sized down by the grow box to 96x85, drew both 376-px chrome strips "and the surplus onto the desktop, because the `gfx_*` primitives clip to the SCREEN and not to a window" |
| SPEC.md 11.96.12 / 96.32 | the DOS window, dragged, left its **cached button rects** on the old title bar - clicks landed on the drag handle. "At least the third time this has been found from scratch rather than read" (`apps/os88api.inc`'s banner) |
| SPEC.md 20.5.1.1 | an About card drawn from a menu handler "lands on top of whatever window is covering yours" |
| SPEC.md 49.3 | TameGram carries 143 bytes of clamp-and-offset wrappers so "a rect one row too tall" does not paint over the window below |
| SPEC.md 70.4 | Telnet drew the rows that did not fit a CGA-clamped window - "the bottom of the terminal was painted over the dock" |
| SPEC.md 77.33 | FTPD's log, from its once-a-second flush and its wake handler, printed "straight across the front of a Disk window sitting over it" (field screenshot) |
| SPEC.md 82.1.1 (#142) | Chart filled to `W_W`/`W_H` - the outer frame - and painted over its own border |
| SPEC.md 96.33.17.2 | the DOS console's wake-handler repaint cut "a black band through" Note Pad on top of it |
| `apps/os88ui.inc` alert | the alert's buttons were laid out at x = 0 "off the left of the alert entirely", every hit test agreeing because both asked the same wrong routine |
| `drivers/ramdisk/page.inc` | "font_str clips to the SCREEN and not to the pane, so a longer [caption] paints over the panel's border and out the other side" - a rule the page carries in prose |
| port-to-os8088 LESSONS | CWORD's About box "put its OK button on the DESKTOP on CGA and Hercules" |
| native-game-port LESSONS | "review lens 1 found a click path that painted unclipped" |

And SPEC.md 20.5.1.5 is the attempt to fix it in the SDK: `OS88UI_CREL` was
built, measured and refused, because an include can only share what a package's
own wrappers duplicate - **Piano -13 image / +37 file**, TameGram +20, Arkanoid
+10 - and concluded *"the win is a KERNEL one"*. This document is that.

---

## 2. The mechanism both designs share: a DRAWING CONTEXT

**One record of kernel state - the DC - says where (0,0) is and what may be
drawn.** Four words: the content origin `dc_ox`/`dc_oy` in screen (virtual)
coordinates, and the box's last column and row `dc_w1`/`dc_h1` in **content**
coordinates. The box's top-left is the origin, so a content rect is in the box
when `x1 >= 0`, `y1 >= 0`, `x2 <= w1`, `y2 <= h1`.

### 2.1 Applied at the package boundary, never in a primitive

The translation and the cut happen **in the API cell's shape**, between the
package's far call and the primitive. That is the whole of why the kernel's own
drawing is untouched: kernel code reaches `gfx_fill` by a near call and never
meets a cell, so the desktop, the chrome, the dock, the menus, the file dialog
and every kernel window keep screen coordinates by construction, not by a rule.
It is also SPEC.md 20's principle restated: *the package boundary is solved once,
not per call site.*

The alternative - every primitive reading the DC itself where it already clamps
to the screen (`gfx_rect_setup`'s `or bx,bx / jns`, `font_run`'s edge tests,
each blitter's) - was weighed and is not recommended (§9.1): it touches every
body on both renderers, and the clamps it would reuse run in DISPLAY-local
coordinates below SPEC.md 39.14.1's display hook, where the content box is not.

### 2.2 Who sets it, and when

| context | the DC | rig |
|---|---|---|
| a package window callback - `W_PAINT`, `W_ONKEY`, `W_ONCLICK`, `W_ONMOUSEUP`, `W_ONDRAG`, `W_ONRCLICK`, `AM_ONCMD`, About, `W_ONTIMER`, `W_ONWAKE`, `W_ONRESIZE`, `W_ONCLOSE`, the file dialog's completion | that window's content box, set by `wm_pkgcall` and STACKED (the caller's comes back on return - a paint pass dispatches several windows inside one lock hold, exactly as `[gfx_mono1]` is stacked today) | built (`wm_pkgcall.crel`) |
| a worker, after `OSAPI_GFX_LOCK` | **empty** - every content call draws nothing - until the worker selects a window | not built |
| a worker selecting a window | `OSAPI_WM_CLIP_SET` (BX = a window of yours) arms the visible region as today **and** sets the DC to that window: "select" | built (`dcs_sel`) |
| a callback drawing into ANOTHER of its windows | the same select; the next dispatch's stacking puts the callback's own back | built |
| `fsx_run`'s entry (SPEC.md 53) | the bracket's surface - `fsx_surf`'s rect - so (0,0) is the corner of the display the app's window is on, which retires SPEC.md 53.7.1's defect class by construction | not built |
| a driver's Control Panel page (SPEC.md 31.9) | optionally the pane, set by the CP before the page verb | not built |
| the entry proc, a kernel window, a driver with no window | the DC is irrelevant: none of them calls a content cell |  |

**Empty-on-lock is the fail-safe that matters.** A worker that forgets to select
draws *nothing*, which is a bug its author sees on the first run - where today a
worker that forgets `WM_CLIP_SET` draws over the window on top, which is a bug a
user sees in the field (§1.1 has three). The lock banks the DC it found and the
unlock restores it, because a callback that unlocks mid-gesture (Paint's drag
sampling, SPEC.md 53) must find its own DC when it locks again; only one task
holds the lock, so one bank is enough.

### 2.3 What each slot does with it

| slot | shape | semantics in content coordinates |
|---|---|---|
| `FILL`, `FRAME`, `FILL_GRAY`, `XOR_RECT`, `XOR_FILL`, `FILL_PAT`, `SCROLL` | rect | cut **exactly** to the box, then translated; an empty cut draws nothing |
| `HLINE`, `VLINE`, `PIXEL` | rect | the same (a line is a one-row rect; a pixel outside the box is dropped) |
| `FONT_RUN`, `FONT_STR_XPARENT`, `FONT_CHAR_XPARENT` | text | wholly inside: translated and drawn, **nothing armed**; straddling: the box is armed as a one-fragment region for the call, so a cut cell is dropped WHOLE - SPEC.md 11.3's existing rule, and TameGram's own choice in 49.3 |
| `ICON_DRAW` | text, always armed | an icon record carries its own size; the rig arms unconditionally |
| `BLIT1`, `BLIT4`, `BLITP` | blit | wholly inside: as given; straddling: the box armed, so `gfx_blit1` cuts exactly and `gfx_blitp` refuses as it does under any region (SPEC.md 5.4.3.3) |
| `SAVE`, `REST`, `WM_CLIP_TEST` | translated only | a cut would change a buffer's layout or a question's answer |
| `WM_DAMAGE` | rect OUT | the damage rect comes back in content coordinates |
| `MOUSE` | point OUT | the pointer comes back relative to the DC's origin (§7.2: a worker polling outside its lock must name the window instead) |
| `FSX_SURF` | rect OUT | (0, 0, w, h) |
| `WM_CONTENT` | **(0,0), inline** | the origin of a content coordinate is zero - see §2.4 |
| `POINTS`, `SPANS` | records in memory | the origin folded into the pass's existing bias (`gfx_pt_rtab` already carries `vid_oy`, so y is free; x is one `add` a point, ~+20% of SPEC.md 5.6.9's 92 cycles, ESTIMATED); the per-point box test is already there |
| click callbacks' CX/DX | point IN | translated by the dispatch (`dc_ptxl`, the rig) |

### 2.4 The trick that makes migration nearly free: `WM_CONTENT` answers (0,0)

A content-mode package's every existing `call OSAPI_WM_CONTENT / add ax, ...`
is still CORRECT - it adds zero. A click arrives content-relative and the
package's `sub cx, [origin]` subtracts zero. So **flipping a package is one line
and its source is otherwise unchanged**, and the clean-up that takes the
now-dead arithmetic out is a separate, optional, byte-saving step.

MEASURED, both halves:

- **The flip**: Piano and Calc assembled with `-DOS88_CONTENT` against the rig
  SDK - which maps every coordinate slot name to its content cell - are the
  **same size to the byte** (Piano image 3,440 / file 3,052; Calc 4,777 /
  4,329) and draw **0 differing pixels** against the stock build: Piano on
  open and after three key clicks, Calc on open and after four button clicks,
  the window and a 32-px ring of bare desktop round it compared bit for bit
  off the CGA framebuffer (`dcflip.py`).
- **The negative control**: the same Piano flip with `WM_CONTENT` left
  answering the screen origin - so every coordinate is offset twice - differs
  by **6,210** pixels inside the window and **0** outside it. The wrong
  package was CAUGHT by the comparison and still **could not draw outside its
  window**: the clip half held while the origin half was broken.

### 2.5 The clip half, shown

`dcbench` draws the same two spills through the screen cells and the content
cells (rows of the CGA framebuffer, `#` lit, the content's left edge at column
16 of each strip, the window's border at column 15 and the Disk window beneath
it to the left):

```
content FILL at x = -4            screen FILL at origin - 4
########.###.##.............####  ########...#....................
###############.............####  ############................####
   border kept, Disk window kept     4 columns of the Disk window and
                                     the border painted black

content RUN at x = -12            screen RUN at origin - 12
###############...##..#######..#  ####..##..###.....##..#######..#
   the two cut cells dropped         'A' and 'B' lettered across the
                                     border and onto the window below
```

---

## 3. What the rig measured

### 3.1 Kernel bytes

`tests/dcrig/dcksec.py` - `kernel.asm` re-assembled with `-DKERNSIZE` for
each build, stock against `-DDCPROTO`:

| | `.text` | `.bss` | `.cold` | `KERN_SIZE` |
|---|---|---|---|---|
| kern_big | 43,368 -> 44,369 (**+1,001**) | +0 | +0 | 97,792 -> 98,816 (+1,024, two rungs) |
| kern_small | 31,828 -> 32,826 (**+998**) | +0 | +0 | 62,976 -> **64,000 - the KERN_SMALL budget exactly** |
| kern_emu | 43,627 -> 44,628 (**+1,001**) | +0 | +0 | 98,816 -> 99,840 |

By span (kern_big), and every byte of it is `.text` because every byte is on the
path of a drawing call:

| what | bytes |
|---|---|
| the twin cells: 20 x 7 (content cells) + 3 x 6 (blit cells) + 4 (`WM_CONTENT`, inline) | 162 |
| `wm_pkgcall`'s content arm (header-bit test, stack the DC, set, dispatch, restore) | ~56 |
| the click translation `dc_ptxl` (37) and its five call sites in `ui.inc` (15) | 52 |
| the DC state (12) and `dc_set` (38) | 50 |
| rect 80, rect X-type 76, hline 53, vline 53, pixel 38 | 300 |
| text: char 15, string 41, icon 17, core 84, `dc_arm` 38 | 195 |
| blit 99, translated-only 28, rect-out 26, point-out 18, select 15 | 186 |

What production would change (ESTIMATED): hline, vline and pixel are one-row
rects and `gfx_hline`/`gfx_vline` ARE `gfx_fill` (SPEC.md 5), so they become
~12-byte register remaps onto the rect shape (-110); the X-type rect shares the
rect body behind a flag (-60); it ADDS the worker lock's empty-and-bank (~40),
`fsx_run`'s surface DC (~25), POINTS/SPANS' bias (~40) and the visible-region
arm for `W_ONWAKE`/`W_ONTIMER` (~20, §4.3), and a single-derivation `dc_set`
(+7). **Production design A: ~950-1,050 bytes on each kernel.**

### 3.2 Cycles a call

`dcbench.py`: one `W_PAINT` draws the same pixels through the screen cell and
the content cell as back-to-back runs of identical calls; a breakpoint on each
cell reads MartyPC's cycle counter, the gap between consecutive hits is one
whole call. A second breakpoint on the primitive's own entry splits off the
prologue, which is where the content cell's work is.

| call | screen | content | extra | of which before the primitive |
|---|---|---|---|---|
| `FILL` 8x8 | 3,096 | 3,528 | **+432** (+14.0%) | 72 -> 434 |
| `FRAME` 40x20 | 16,416 | 16,848 | **+432** (+2.6%) | 73 -> 431 |
| `HLINE` 64 px | 3,036 (2,648-3,218, CGA wait states) | 3,672 | **+636** median; prologue +295 | 72-76 -> 362-371 |
| `FONT_CHAR_XPARENT`, one cell | 4,032 | 4,498 | **+466** (+11.6%) | 76 -> 484-495 |
| `FONT_RUN`, 8 cells | 13,392 | 14,112 | **+720** (+5.4%) | 162 -> 822 |
| `BLIT1` 16x8 | 4,196 | 4,670 | **+474** (+11.3%) | 74 -> 516 |

All of it on kern_big. The shapes are the same instructions on kern_small and
kern_emu, so the EXTRA carries over (ESTIMATED); kern_small's primitive bodies
under it differ, so its totals do not.

PERFORMANCE.md's fixed part of a drawing call is 756 us = 3,606 cycles, so the
rect shape adds **~12% of the smallest call** and ~3% of a moderate one. It is
an argument against the *shape* rather than the idea that **the text shape's
first build ARMED THE BOX FOR EVERY CALL**: a glyph cost **+1,846** (+46%) and
an 8-cell run +1,224, and Piano's whole paint was +6.0%. Testing containment
first - an 8-px cell, or a `repne scasb` for a run's length - and arming only on
a straddle is what took those to +466 and +720 and the paint to +1.7%. The rect
shape has the same kind of slack left in it: four `push`/`pop` pairs (108
cycles) to keep SPEC.md 20's "preserves every register", the `push bp / mov bp
/ jmp` indirection (~50), and its clamps written as taken branches in the
uncommon direction - **~300 cycles a rect is the production target (ESTIMATED)**.

### 3.3 Cycles a callback

`wm_pkgcall`'s content arm, breakpoints on its own labels: **2,823 cycles to set
the DC, 183 to restore it** - 630 us a callback. Almost all of it is `dc_set`
calling `wm_content` and `wm_geom`, which between them derive SPEC.md 11.95's
flush borders three times. Two notes:

- **It is work the package already does.** Every package calls `WM_CONTENT` and
  `WM_GEOM` at the top of every callback today (§1), through two far calls and
  the same derivations; a content package stops doing it. Net, ~0.
- A single-derivation `dc_set` (one `wm_flush`, the four words computed in
  line) is ~800-1,000 (ESTIMATED), and a LAZY one - the dispatch stacks one word
  saying which window, and the first content call computes - is free for a
  callback that draws nothing.

An UNDECLARED package's callback pays the header-bit test: one `test byte
[3], 40h` and a not-taken `jnz`, ~24 cycles (COUNTED, not measured).

### 3.4 A whole paint

`dcpaint.py` times each package's own first `W_PAINT` - `wm_draw_win`'s `.paint`
to `.uncharged`, matched to the package's window by DI, four opens each:

| package | stock | flipped (source unchanged) | cleaned |
|---|---|---|---|
| Piano (77 drawing calls: 27 fill, 18 frame, 20 glyph, 5 hline, 5 run, 2 string) | 1,153,346 cycles, **241.7 ms** | 1,185,236, +2.8% | 1,173,263, **+1.7%** |
| Calc (66 calls: 21 fill, 21 frame, 23 run, 1 blit1) | 833,316, **174.6 ms** | 869,588, +4.4% | 867,951, **+4.2%** |

Piano gains back what its wrappers cost (§4.1) and Calc, which added its origin
inline, does not. With the ~300-cycle rect shape the two would be ~+0.7% and
~+2.5% (ESTIMATED). This is the number SPEC.md's PERFORMANCE rule 1 asks for: a
paint is priced by its calls, and every call pays the shape once.

### 3.5 Package bytes

| package | style | image | file (LZ4) | verified |
|---|---|---|---|---|
| Piano | six near-call wrappers + a rect-staging proc for `os88ui_btn` | 3,440 -> **3,259 (-181, -5.3%)** | 3,052 -> 2,868 (-184) | 0 px different, open + 3 clicks |
| TameGram | clamp-and-offset wrappers (SPEC.md 49.3) | 3,353 -> **3,201 (-152, -4.5%)** | 3,173 -> 3,034 (-139) | 0 px outside the window; the HUD differs run to run even stock-against-stock (163 px), so in-window identity is NOT established for this one |
| Calc | origin added inline, cleaned MECHANICALLY (every `add/sub reg,[cal_ox]` deleted, every `mov reg,[cal_ox]` made `xor reg,reg`) | 4,777 -> **4,724 (-53, -1.1%)** | 4,329 -> 4,279 (-50) | 0 px different, open + 4 clicks |

Two things about these:

- **A wrapper's NEAR call becomes a FAR call**: 3 bytes a site become 5, so
  deleting Piano's 108 bytes of wrappers costs +2 a call site back - which is
  exactly why the wrappers were never worth sharing as an include and why the
  saving here is the arithmetic plus the bodies, not the bodies alone.
- **Disk tracks image** (unlike 20.5.1.5's include, which saved RAM and cost a
  cluster): the arithmetic that goes is not the near-identical text LZ4 was
  compressing well.

**Tree-wide (ESTIMATED).** `tests/dcrig/dcorigest.py` finds the memory words each
package banks `WM_CONTENT`'s answer in and every instruction naming them:
~4.6 KB of origin references and ~1.4 KB of `WM_CONTENT`/`WM_GEOM` call sites
across the 50 shipped assembly packages, largest Sheet (~620 + ~120), Scribe
(~440), Word (~380), Redline, PiXEL and Tracker (~240-260 each). Calibrated
against the three conversions it under-counts wrappers and clamps (Piano 102
estimated / 181 measured, TameGram 42 / 152) and over-counts arithmetic a
mechanical clean-up leaves behind (Calc 79 / 53): **~3-7 KB of package image
across the tree**, resident only while each package runs. The C packages add
their `org.x + ...` arithmetic behind 19 `os88_wm_content` calls, unmeasured.

---

## 4. Weighing the two halves separately

### 4.1 The ORIGIN half

What it buys: the arithmetic of §3.5; **a stale origin becomes impossible** -
content coordinates do not change when the window moves, so a rect cached in
bss stays right across SPEC.md 11.96.12's drag replay, which is the DOS
cached-button defect and `os88api.inc`'s two-rule banner retired by
construction; clicks arrive in the coordinates the package draws in; `fsx`'s
(0,0) is the right display's (53.7.1).

What it costs: the four adds and the register save in every shape - ~200 of
the rect's ~430 cycles (ESTIMATED) - and the DC bracket per callback, which
replaces the package's own two calls.

### 4.2 The CLIP half

What it buys: **the class in §1.1** - a layout one row too tall, a CGA-clamped
window, a grow box that shrank the window under its chrome, a string longer than
its pane. Nothing in that list is a mistake about the ORIGIN; every one is a
correct origin and a rect that outgrew the box. **The clip half is what answers
the owner's ask**, and the negative control in 2.4 is the demonstration: a
package with its origin wrong by a whole window still painted nothing outside.

What it costs: the rect's four compares (~100 cycles) and, for text, the
containment test (a `repne scasb` per run, ~200 cycles for eight cells).

**A clip-only design** (screen coordinates in, cut to the content box) was
considered and is worse than both: it needs the same DC, the same per-callback
set, the same lock semantics and nearly the same shapes, keeps every byte of
the origin arithmetic in every package, and leaves the stale-origin class open.
The two halves share all their machinery; taking one is paying for both.

### 4.3 ...and the half NEITHER buys: the window on TOP

Drawing over a window ABOVE yours is a third failure, not a clip one: the
content box of a covered window is under somebody else's pixels. SPEC.md 77.33
and 96.33.17.2 are this, from wake handlers. Both designs close it the same way
and it costs almost nothing: the dispatch arms the VISIBLE REGION (today's
`wm_clip_set`) for a callback that is neither a `W_PAINT` (the painter's
algorithm, or the cull, already resolve those) nor on the front window (a click
and a key go to the front window, which nothing covers) - in practice
`W_ONWAKE`, `W_ONTIMER` and an About picked off a background window. A worker's
select already arms it. ~20 bytes (ESTIMATED), and a region build per such
callback, which are a few a second.

---

## 5. Design A — NO FLAG DAY

### 5.1 Shape

- **Twin cells.** Every coordinate-bearing slot gets a content twin appended to
  the table (~25 cells; the rig built 24). The screen cells are untouched, so a
  package built yesterday, a test package, a driver and every `apic_`/`cw_`
  kernel door that IS a cell (SPEC.md 20.3, `kernel.asm`'s thirteen `cw_X equ
  apic_X`) keep their meaning exactly.
- **A header bit**, `PKGF_CREL` = flags bit 6 (free, SPEC.md 20.2), set by
  `OS88_HEADER` when the package declares `OS88_CONTENT` before including the
  SDK. `wm_pkgcall` reads it off the package's own header (`[ds:3]`, DS being
  the package's at that point) to decide whether to set the DC and translate
  the click.
- **The SDK switch.** Under `OS88_CONTENT`, `apps/os88api.inc` re-points every
  coordinate slot NAME at its twin and **does not define the screen names at
  all**, so a content package cannot reach a screen cell by name. Every shared
  include (`os88ui`, `os88con`, `os88line`, `os88text`, `os88type`, `os88gfx`,
  `os88chart`) is converted with its includer and needs no source change of its
  own except the leaks in §7.2.
- **The C SDK**: `CC_PACKAGE`'s flag defines `OS88_CONTENT` for `crt0.asm`, whose
  `%include` of `os88thunk.asm` then calls the twins; `os88.h`'s
  `os88_wm_content` documents (0,0).
- **The ratchet**: a fast-tier row, `t_coords`, in `t_movable`'s shape - every
  package under `apps/` declares `OS88_CONTENT`, or is listed in
  `tests/coords.txt` with a reason, and the list only turns one way. It also
  refuses `call KERNEL_SEG:<number>` in a content package (the one way round the
  SDK switch) and flags reads of `W_X`/`W_Y` there (§7.2).

### 5.2 Costs

| | kern_big | kern_small | kern_emu |
|---|---|---|---|
| resident, rig | +1,001 | +998 | +1,001 |
| resident, production | ~950-1,050 (ESTIMATED) | the same, and see §8 | the same |
| an undeclared package's drawing call | +0 | +0 | +0 |
| an undeclared package's callback | ~+24 cycles | ~+24 | ~+24 |
| a declared package's call | §3.2 | §3.2 | §3.2 |

### 5.3 The guarantee

A DECLARED package cannot draw outside its content box through any slot. The
ratchet makes every in-tree package declared except the listed ones. An
UNDECLARED package - an old binary, an exempt one - is exactly as it is today.
Neither design reaches a package that writes the framebuffer itself (every
`fsx` game after its mode set, SPEC.md 53.7), and nothing on this machine could.

### 5.4 Migration, per package

1. Add `%define OS88_CONTENT` (assembly) or the `CC_PACKAGE` flag (C).
2. Fix its leaks (§7.2) - most packages have none.
3. Re-point any of the 42 test scripts that read its origin words to click
   (§7.2) - they turn content-relative at the flip.
4. Prove it: the package's own rows, plus a `dcflip`-shaped row (stock against
   flipped, the window and a ring of desktop, bit for bit; it went red on the
   negative control in 2.4).
5. Optionally, later: delete the dead arithmetic (§3.5), proven the same way.

Risk is per package and visible per package; a package that is wrong after the
flip is wrong INSIDE ITS OWN WINDOW, which is the point.

---

## 6. Design B — THE ABI MAY BREAK

### 6.1 The cleanest design when compatibility is not a constraint

- **Every package-facing coordinate is content-relative, in and out, always.**
  The existing cells change meaning at the same numbers (or are renumbered -
  nothing pins them once every package is rebuilt); `PKG_FMT` 8 -> 10 so an old
  package is refused at load with SPEC.md 20.2.0's "Load failed" rather than
  drawing in the wrong place. No twin table, no header bit, no SDK switch.
- **`OSAPI_GFX_LOCK` takes BX = the window to draw in.** Lock, select, arm the
  visible region and set the DC in one call; `WM_CLIP_SET` from a worker
  disappears (98 sites). BX = 0 or not yours: the DC is empty and everything
  draws nothing.
- **`OSAPI_MOUSE` takes BX = the window** and answers relative to its content -
  from any context, so a worker's out-of-lock poll is no longer a special case.
- **`OSAPI_WM_CONTENT` is withdrawn** (SPEC.md 20.3.1's rule: a withdrawn cell is
  deleted with its SDK name) - or kept as the 4-byte (0,0) inline cell for one
  cycle, which is what makes the flag day mechanical (§6.4).
- **Drivers**: a driver with no window keeps the identity DC (the saver draws
  the whole screen); a Control Panel page gets the PANE as its DC, retiring
  `page.inc`'s prose rule; `DRV_VER` moves with `PKG_FMT`.
- **Kernel doors**: the six `cw_`/`HB_API_` doors that are coordinate cells
  (`cw_gfx_hline`, `cw_gfx_fill_gray`, `cw_gfx_xor_fill`, `cw_wm_clip_test`,
  `HB_API_FILL`, `HB_API_FONTX`) are re-pointed at raw shims (+4 bytes each),
  because cold kernel code runs inside package callbacks (the file dialog's
  synchronous half) and must not inherit a package's DC. The rig SAW this:
  opening a package on kern_big put 12 `HLINE`s, a `FILL_GRAY` and an
  `XOR_FILL` through the cells from kernel code.

### 6.2 Costs

| | kern_big | kern_small | kern_emu |
|---|---|---|---|
| resident, ESTIMATED | ~800-850 (A less the ~170-byte twin table and the ~30 bytes of header tests, plus ~24 of raw doors) | the same, §8 | the same |
| a drawing call | §3.2's shapes; the hot cells may name their shape directly instead of through BP, ~-50 cycles at +1-2 bytes a cell | | |
| a callback | §3.3, for every package | | |

### 6.3 The guarantee

**No package can draw outside its content box through any slot, by
construction, with no list and no exception.** The same caveat as A about the
framebuffer.

### 6.4 What converting the whole tree takes

| | count | work |
|---|---|---|
| assembly packages | 50 shipped + 43 test packages | the one-line flip each (B makes it the default, so the line is DELETING nothing - the flip is the rebuild) |
| shared includes | 7 | `os88ui.inc`'s four `W_X`/`W_Y` sites in the alert (§7.2); the rest none |
| C packages | 7, + 5 host-test stubs of `os88_wm_content` | rebuild; the stubs answer (0,0) |
| drivers | 5 that draw | the saver none; the pages take the pane DC |
| workers | 32 packages | `GFX_LOCK` gains `mov bx, <window>` (83 sites); `WM_CLIP_SET` after it deleted |
| the leaks | §7.2 | the same list as A |
| tests | **42 of 637 `tests/*.py` read a package's origin words** (`pt_cx0` alone in 30 places, `tk_ox`, `dd_ox`, `tw_ox`, `tg_ox`, `px_ox`...) to turn a content point into a SCREEN click | **broken by the FLIP, not the clean-up**: those words hold content coordinates from the moment `WM_CONTENT` answers (0,0), so every such script must add the window's content origin from the record itself (`os88geom.Win.content`) in the same commit as its package's flip |
| docs | §11 | |
| the suite | **the whole soak**, which is the owner's to ask for (CLAUDE.md, Testing) | |

With §2.4's trick B's flag day is mechanical rather than a rewrite: every
package still adds a (0,0) origin and is still right. What makes it a FLAG DAY
is not the work, it is that a hundred packages change meaning in one commit and
the only proof is the whole suite at once.

---

## 7. Interactions, and the leaks a flip does not fix

### 7.1 Everything the brief asked about

| | answer |
|---|---|
| workers under the gfx lock | §2.2: empty DC on lock, select to draw, the lock banks and restores (A); `GFX_LOCK BX = window` (B) |
| `fsx` brackets | `fsx_run` sets the DC to the surface; `FSX_SURF` answers (0,0,w,h); after `fsx_mode` the drawing slots are illegal anyway (53.7) and the app owns the hardware |
| the kernel's own drawing | untouched by construction (§2.1); the rig's whole desktop, chrome and Disk window ran on it |
| the extended desktop | the DC origin is VIRTUAL, and the shapes translate before `GFXCLIP`/`GFXDISP`, which split by display as they do today (SPEC.md 39.14.1's ordering argument is the same one) - NOT measured on a two-card machine |
| several instances | each window's own DC per dispatch; a worker selects its own window |
| drawing for a DIFFERENT window | select it (`WM_CLIP_SET`, the rig's `dcs_sel`); the dispatch's stacking restores the callback's own |
| `os88ui.inc` - About cards, alerts, drop-downs, scroll bars, buttons | content-bound already: the About card and the drop-down list are confined to the window by design (`os88ui_drfit`), the button records hold content rects (Piano's record became its RECTS table directly, §3.5); the alert's four `W_X`/`W_Y` sites are the one leak |
| `BLIT1`/`BLIT4`/`BLITP`/`ICON_DRAW`/`FONT_RUN`/... | §2.3 |
| `SCROLL` and `BLIT4`, today's two clip HOLES (SPEC.md 11.3) | the content cut closes them for the content box; the visible region is still not honoured by them, as today |
| menus, the menu bar, the dock, the Disk window | kernel; untouched |
| the C toolchain's 96-byte frame rule, `&local` | unaffected; `os88_wm_content(w, &org)`'s out-parameter simply answers zero |

### 7.2 The leaks: screen coordinates that do not come from a slot

The flip makes `WM_CONTENT` zero, so anything that reaches a SCREEN number some
other way is now wrong - and wrong inside the package's own window, which is
the safe direction. A grep finds them all:

| leak | where | fix |
|---|---|---|
| `W_X`/`W_Y` read from the record | `os88ui.inc` (alert: 4 sites), `taskmgr` (3), `paint` (4 - which WRITE them, to place a window: legitimate) | `WM_CONTENT` (0,0) and `WM_GEOM`; the gate's allowlist for writes |
| a window template placed from the content origin | Tracker's playlist (`tw_ox`), TameGram, Missile, PIXELSTEIN | a slot answering the SCREEN origin for placement only (`OSAPI_WM_ORIGIN`), or `W_X`/`W_Y` |
| `OSAPI_MOUSE` polled by a worker outside its lock | 23 files call `MOUSE`; the polling workers among them | A: name the window (`CMOUSE BX`); B: BX is the contract |
| `OSAPI_GFX_POINTS`/`SPANS` | Mines, `os88gfx.inc`'s walkers, Paint | §2.3 (kernel-side) |
| a test harness reading a package's origin words to click | 42 scripts | in the SAME commit as the package's flip: the words turn content-relative at the flip |

---

## 8. kern_small — the decision

SPEC.md 39.27.4: *"bytes returned to the small build stay returned"*, and the
small kernel is on a diet rather than a budget. The feature is ABI - a content
package on `make smallapps`' floppies calls the twins - so it cannot be gated
out of kern_small the way a resident nicety is. The rig puts kern_small's
`KERN_SIZE` at **64,000 of a 64,000 budget**.

Three ways through, for the owner:

1. **Pay it**, ~950-1,050 bytes (A) or ~800-850 (B), argued on the incident
   record and on the fact that a running content package gives back 50-180
   bytes of its own image - one or two running packages repay a fifth of it.
   Honestly, not a RAM win on the 128KB machine; a correctness one.
2. **A compact kern_small arm** (ESTIMATED ~350-450 bytes): every content call
   translates and then arms the box as a one-fragment region unconditionally,
   reusing SPEC.md 11.3's machinery for every cut. One shape per argument
   layout and no containment tests. The cost is cycles on the slowest machine:
   the rig's always-arm text shape measured +1,846 a glyph, and a rect would
   take `gfx_clip_run`'s armed path on every call.
3. **Fund it** from the open rows of docs/plans/KERN-SMALL-CUT-PLAN.md.

---

## 9. Alternatives weighed and not recommended

### 9.1 In the primitives instead of at the boundary

Each primitive reads the DC where it already clamps and addresses. ESTIMATED
~100-150 cycles a call against the boundary's ~300-430, because nothing is
saved and restored and there is no extra dispatch. Not recommended: it touches
every drawing body on both renderers (VGA planar, the 1bpp software renderer)
and both kernels; the existing clamps are below the display hook in local
coordinates; the kernel's own drawing must then be given an identity DC on
every path that reaches a primitive (the boundary design needs none); and it is
SPEC.md 39.14's whole-shape hook problem again, one layer up.

### 9.2 Content-relative slots BESIDE the screen ones with no header bit

Design A without the bit: the package simply calls the twins. It works for the
drawing and fails for the INPUT - the dispatch cannot know to translate a click
or set a DC without asking the package - so the bit is the cheapest way to ask.

### 9.3 A per-package mode tested in the SHARED cells

One set of cells, each testing a mode byte. Every undeclared caller pays the
test (~28 cycles a call, COUNTED) for ever and the cells cannot grow in place,
so the test lands in front of each primitive's entry; the twins cost the same
bytes and nothing per call.

### 9.4 The include again (20.5.1.5)

Measured and refused once, and §3.5 says why the kernel version differs: the
SDK switch moves the arithmetic OUT of every package rather than sharing it
between a package's own copies.

---

## 10. Recommendation and waves

**Design A as the vehicle, B as the last wave.** Every figure below is per
kernel unless it says otherwise.

| wave | what | costs | buys |
|---|---|---|---|
| **W0** | SPEC.md first: the DC (a new subsection of SPEC.md 11.3, or a section of its own), the twin cells in 20.3, the header bit in 20.2, the semantics table of §2.3 | 0 | the contract |
| **W1** | the kernel: DC, `wm_pkgcall`'s arm, click translation, the rect/text/blit/translated/out shapes, the twins, `WM_CLIP_SET` as select, empty-on-lock, `fsx_run`'s surface | ~950-1,050 resident (ESTIMATED; the rig's +1,001 is this set less the lock, `fsx` and POINTS) - and **kern_small's decision (§8) is due HERE** | nothing yet: no package declares |
| **W2** | the SDK switch, `CC_PACKAGE`'s flag, `t_coords` with EVERY package listed, a `dcflip`-shaped row family | 0 resident | the ratchet exists and can only turn one way |
| **W3** | flip the packages, smallest first, each its own commit with its own proof; fix the §7.2 leaks as each one meets them | per-call §3.2, per paint ~+2-4% (MEASURED on the rig; ~+1-2.5% with the production rect, ESTIMATED) | **the guarantee, package by package** - and every NEW package, which is the owner's ask: the SDK makes content the default the day W2 lands, so a new session never meets a screen coordinate at all |
| **W4** | the clean-up: delete the (0,0) arithmetic, re-point the tests that read origin words | 0 resident | -53 to -181 image a package (MEASURED), ~3-7 KB tree-wide (ESTIMATED) |
| **W5** | B: `PKG_FMT` 8 -> 10, the screen cells become the content cells, the twin table and the header test come out, `GFX_LOCK BX`, `MOUSE BX`, `WM_CONTENT` withdrawn, the six kernel doors re-pointed | **-150 to -200** against W1 (ESTIMATED) | the guarantee for every package by construction, and the list deleted |

W3 is where the owner's ask is actually met for NEW work: the SDK default is
content from W2, the ratchet refuses a new undeclared package, and the
documents in §11 stop teaching the origin at all.

---

## 11. Making the rule the system's and not a reminder

| document | change |
|---|---|
| `apps/os88api.inc` | the banner *"DRAW FROM THE CONTENT ORIGIN. ALWAYS."* becomes *"COORDINATES ARE YOUR CONTENT'S"*: (0,0) is your content's top-left, nothing you draw leaves the box, a click arrives in the same coordinates, and the two rules about caching stop being rules; every coordinate slot's comment says "content" |
| CLAUDE.md, Hard rules | one line: a package draws in content coordinates and the kernel cuts it to its box (SPEC.md §); the clip half is not the package's job |
| SPEC.md 5, 6, 11.3, 20.2, 20.3, 20.5, 53 | the DC, the twins (A) / the changed cells (B), the bit, the semantics table, `fsx`'s surface |
| docs/INDEX.md | regenerated; the twins appear under Drawing / Text / Windows with "content" in their `takes` column |
| docs/C-TOOLCHAIN.md, `apps/cc/os88.h` | `os88_wm_content` answers zero; the out-parameter row stays (it is still an out-parameter) |
| `.claude/skills/port-to-os8088` and `native-game-port` LESSONS | the clip lesson and the content-box lesson become one sentence: "your coordinates are content coordinates and the kernel clips to the box" |
| gates | `t_coords` (the ratchet, the raw-address refusal, the `W_X`/`W_Y` read flag); a `dcflip`-shaped row per flipped package that needs one |

---

## 12. Reproducing the rig

`docs/plans/content-coords-rig.patch` applies to `e73b3224`. It adds
`kernel/dcproto.inc` and wires it in behind `-DDCPROTO` (nothing that ships
defines it), the SDK switch behind `DCPROTO` + `OS88_CONTENT`, `os88pkg.py`'s
acceptance of header bit 6, and `tests/dcrig/` - the bench package, the three
drivers (`dcbench.py`, `dcflip.py`, `dcpaint.py`), the converted Piano, Calc and
TameGram, the census and estimator scripts (`dcsurvey.py`, `dccsurvey.py`,
`dcorigest.py`, `dccount.py` - a paint's call mix), `dcksec.py`, and
`build.sh`, which builds the rig
kernel into `build/proto/` (a private tree, SPEC.md's `BUILD=` rule) and the B:
disk the drivers boot. None of `tests/dcrig/` is a suite row and none of it may
become one as it stands: every file needs the rig kernel.

Three traps it hit, for whoever builds W1:

- `os88sym` adds `-DKERN_KNOB` for any define it does not know, which changes
  `.ovl` padding and makes the map refuse the image; the drivers append
  `DCPROTO` to `os88sym._SHIPPED_DEFS`.
- `os88kz.py`'s second pass runs `$NASM` as ONE word, so `NASM="nasm -DDCPROTO"`
  fails; `build.sh` uses a wrapper script.
- `os88sym`'s addresses are LINEAR; a window record compared against a
  register needs `- 0x600`. The first whole-paint figures timed the Disk
  window's repaint instead of the package's for exactly that reason, and read
  as "content coordinates cost nothing".
