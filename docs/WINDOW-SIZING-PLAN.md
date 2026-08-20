# Preferred size, minimum size, and the window that came back smaller

**Status: the KERNEL half is BUILT and gated — SPEC.md §11.100 — and §4, the
package-by-package sweep is DONE for every package this container can build:
`modplug`, `browser`, `paint`, `texpad`, `word` and `frotz` are converted, ten
more were checked and need nothing, and `cword` is blocked on a C toolchain
failure that reproduces at `origin/elendilon` (§10.3 below).** `tests/dispsize.py` is the gate
and asserts all three: a window walked across the seam and home again keeps its
size (§1.1's 200 → 140 → **200**), a window dropped clear across lands on rows
the display has (§1.2's 303 → **200**), and `apps/modplug` — the first package
to declare a per-adapter size — takes its compact face onto the CGA and its
full one home again, **0 differing pixels** against a forced full repaint.

Everything below is as it was written, before any of it was built. §1's
measurements are the defects as they stood; §3's proposal is what shipped,
with one change §8 did not foresee and §4's table is still a first cut.

The one-line summary: **the kernel has a clamp and a bank, and neither of them
is the size a window wants on the display it is on.** The clamp only ever takes
away; the bank only ever replays the last thing a human did. Between them a
window dragged onto the smaller monitor comes back smaller for the rest of the
session, and a window dropped there in one motion is not resized at all.

---

## 1. What the machine actually does today

Both runs are `tests/dispsize.py` on `os8088_5150_both_gla` — a 720x348
Hercules at (0,0) and a 640x200 CGA at (720,20), Hercules primary, **Extend /
Right**, which is §39.19.2's layout and the field machine's.

### 1.1 The straddle cut is PERMANENT

A Disk window, opened on the Hercules, dragged across the seam, on to the CGA,
and all the way back. `rect` is the record; `bank` is the natural bank
(§39.11.2.1), the rect the window is supposed to go *back* to:

| step | rect | bank |
|---|---|---|
| opened on the Hercules | (103,80) **320x200** | (110,80) 320x200 |
| straddling the seam | (607,80) **320x140** | (607,80) **320x140** |
| wholly on the CGA | (759,80) 320x140 | (759,80) 320x140 |
| dragged back to the Hercules | (199,80) **320x140** | (199,80) 320x140 |

Sixty rows go on the way over and **do not come back**. The mechanism is one
line of ordering in `ui_drag`'s release: `wm_strad_fit` (§39.16.3) cuts the
record, and `wm_nat_bank` runs **after** it, so what the window remembers
wanting is the cut. Every later drag re-banks the same cut, and the only thing
in the machine that could undo it is a `wm_refit` — an adapter switch, which
this is not.

That contradicts the natural bank's own argument. §39.11.2.1 introduced the
bank precisely because **`wm_fit` is a clamp and a clamp throws the number
away**, and it lists what may not bank: "`wm_fit` and `wm_snap_win`, the clamp
itself — banking there would make the clamp its own source and the bank would
say nothing". `wm_strad_fit` is a clamp by the same definition and is not on
that list.

### 1.2 …and a window dropped clear across is not cut at all

Solitaire — a fixed-size window whose card metrics are picked from the screen
height at launch — opened on the Hercules and dragged onto the CGA in **one**
motion:

| step | rect |
|---|---|
| opened on the Hercules | (231,20) 258x**303** |
| wholly on the CGA | (743,20) 258x**303** |

The CGA occupies virtual rows 20..219. A 303-row frame at y=20 ends on row
323, so **104 rows of it are in the dead zone** (§39.2.1) — drawn nowhere,
clickable nowhere, on no monitor. `wm_strad_fit` returns `.none` when the frame
does not *reach* the other display, and it is evaluated once, at the release;
a drag that ends wholly on the short display therefore meets no clamp of any
kind, because `ui_drag` bounds x and y against the whole union and never calls
`wm_fit`.

These are opposite failures — one window too small, one too large — of the same
missing idea, and the second is the worse of the two: §1.1 costs the user
sixty rows, §1.2 costs them a third of a game of Solitaire with no pixel on
either screen saying so.

### 1.3 The layout that came with it is the Hercules'

Solitaire also keeps the card metrics it picked at launch. It *does* register
§11.98, and the notice does fire — but `OSAPI_VIDEO` answers about the
**primary** (§53.7.1 is the same trap on the fullscreen surface), which is
still the Hercules. There is no call in the SDK that answers "which adapter is
the display my window is now on", so an app that wanted to re-derive correctly
could not.

---

## 2. The hodgepodge

24 packages (plus `apps/cc`, the C SDK) and the kernel's own window kinds. How
each decides its size:

| how | packages |
|---|---|
| **fixed template, VGA-authored, take the clamp** (14) | `artful` 360x300, `calc` 226x137, `fractal` 322x199, `frotz` 560x396, `hello` 240x90, `mines` 146x183, `notepad` 260x180, `piano` 224x177, `recorder` 220x140, `telnet` 528x190, `texpad` 628x400, `tracker` 420x180, `word` 600x440, `cword` 592x384 |
| **patches its own template from `OSAPI_VIDEO` before `wm_create`** (9 packages, 11 windows) | `arkanoid`, `browser`, `cyclone`, `missile`, `modplug` (+ its Setup and PlayList windows), `paint`, `solitaire`, `tamegram`, `taskmgr` |
| **re-derives on §11.98** (7) | `arkanoid`, `calc`, `cyclone`, `missile`, `piano`, `solitaire`, `taskmgr` |
| **installs a `W_ONSIZE` negotiator** (2) | `frotz`, `paint` |
| **`WF_KEEPH`** (2) | `browser`, `mines` |

Eleven derivations, eleven shapes. `solitaire` builds a frame from the tallest
column the game can make and clamps it to the band by hand, one pixel short,
with a nine-line comment explaining the pixel. `taskmgr` runs its layout
**twice** because the first pass answers a different question from the one the
§11.98 handler can ask. `cyclone` takes `min(CY_WIN_H, band)`. `browser` takes
**90% of the band, centred**, on VGA and Hercules and the whole screen below
the bar on a CGA, and turns `WF_KEEPH` on for the CGA alone — and carries a
comment recording the run it cost when `OSAPI_VIDEO` answered the screen
height in the register that held the window pointer. `modplug` carries two
whole content heights, `MPP_CHF` 196 and `MPP_CHC` 132, and picks between them
on screen height — which is the thing this document proposes, built by hand, in
one package, for one axis.

### 2.1 …and on a CGA, ten of nineteen windows are exactly 155 rows tall

Measured, `os8088_5150_cga_gla`, every package on the apps floppy opened in
turn and its record read (640x200, `dock_y0` 176, desktop band **155**):

| pinned at the band — the clamp, not a choice | fits, and is its own size |
|---|---|
| `arkanoid` 208x**155**, `artful` 360x**155**, `cyclone` 322x**155**, `fractal` 322x**155**, `notepad` 260x**155**, `piano` 224x**155**, `solitaire` 222x**155**, `telnet` 528x**155**, `texpad` 628x**155**, `tracker` 420x**155** | `hello` 240x90, `calc` 226x137, `missile` 560x136, `recorder` 220x140, `tamegram` 250x143, `modplug` 418x151, `paint` 494x152 |

and two that hang over the dock on purpose: `mines` 146x179 and `browser`
496x179, both `WF_KEEPH` (§11.93).

**The left-hand column is the whole argument for §3.1 in one number.** Ten
windows, ten different applications, all exactly the same height, and not one
of them chose it. Four of the seven on the right *did* choose — `missile`,
`tamegram`, `modplug` and `paint` derive — and the other three simply happen
to be small enough that the question never came up.

The same sweep on `os8088_5150_herc_gla` (720x348, `dock_y0` 324, band **303**)
cuts **three**: `arkanoid` 250x**303**, `solitaire` 258x**303**, `texpad`
628x**303**. Everything else fits — `missile` 630x266, `artful` 360x300,
`paint` 494x300, `browser` 496x273 (which is 90% of 304, its own arithmetic
landing exactly where §2 says it does), `modplug` 418x215 (`MPP_FHF` exactly),
`cyclone` 322x219, `fractal` 322x199, `telnet` 528x190, `notepad` 260x180,
`tracker` 420x180, `piano` 224x177, `recorder` 220x140, `calc` 226x137,
`hello` 240x90, and `mines` 146x183 by `WF_KEEPH`. `tamegram`'s row was a
mis-read — the sweep closes each window before opening the next and this one
did not close, so what came back was the previous window's rect — and is left
out rather than reported. `frotz`, `word` and `cword` ride disks of their own
(§61.9, §68.5) and were not in the sweep; their templates are 560x396, 600x440
and 592x384, so the band cuts all three.

**So the CGA is where this matters and the Hercules is where it is latent.**
Ten cut against three, on the same nineteen windows.

Three observations fall out of the survey and each is worth stating on its own:

- **Every FIXED window in the tree is a VGA-shaped window.** The widest
  template is `texpad`'s 628 against the Hercules' **720**; `word` is 600,
  `cword` 592, `frotz` 560. None of them has ever used those columns, because a
  template is a size that gets *cut* and there has never been a way to say a
  size that is *chosen*. The derived ones can and do — `missile` measures
  630x266 on a Hercules and 560x136 on a CGA — which is the same observation
  from the other side: the only windows that fit the machine they are on are
  the ones that each rolled their own arithmetic to do it.
- **The CGA is where the clamp bites and where nothing declares anything.** On
  a 155-row desktop band, `word` loses 285 rows, `frotz` 241, `texpad` 245,
  `artful` 145. Of those four only `frotz` can answer at all, and it answers
  through a negotiator that is never consulted by `wm_fit`.
- **`WF_KEEPH` is already a minimum height wearing a flag.** §11.93 says "my
  layout is FIXED, so my height is not the kernel's to reduce", and its floor
  is the display's own bottom. That is exactly `min height = the height I asked
  for`, with the same rule about never making a window unreachable.

---

## 3. The proposal

Six parts. (1) and (2) are new API cells, (3) is how their
clamps compose, (4) and (5) are the behaviour the cells buy, and (6) is the
hole §1.2 found, which has to be closed either way.

### 3.1 `OSAPI_WM_PREFER` (0x0478) — a frame size per adapter kind

`BX` = window, `SI` = the offset **in the window's own segment** of a 12-byte
table: three `(w, h)` pairs in `VID_VGA`, `VID_HERC`, `VID_CGA` order, which is
0, 1, 2 and so indexes directly. `SI = 0` withdraws it. A `(0,0)` pair means
*no preference on that adapter* — keep whatever the window has — so an app may
say only the thing it cares about without restating the other two.

It registers **and applies**: `wm_pref_take` then `wm_fit`, FLAGS-preserving,
which is `wm_keeph`'s contract for `wm_keeph`'s reasons — by the time an entry
proc can call this `wm_create` has already fitted the window, and an entry
proc's CF is the loader's return value, so a `cmp` inside the re-fit aborts the
launch.

**The kernel keeps the OFFSET, not a copy.** One word per slot (24 bytes of
`.bss` against 144), read through `W_SEG` at use time exactly as `W_TITLE` and
every menu string already are — `wm_strseg` is the routine and it answers
`KERNEL_SEG` for `W_SEG` = 0, so a kernel window declares a preference with no
special case at all.

**Why a table and not a callback.** A callback is the shape §11.1's negotiator
and §11.98's notice already use, and it is the wrong shape here for three
reasons. The answer is wanted from inside `wm_fit` and from `ui_drag`'s
release, which are clamp sites rather than dispatch sites. It is consulted on
every drag, and a `wm_pkgcall` there is a segment reload and a far call to say
a number that cannot have changed. And a table is *declarative*, which is what
"standardized" has to mean: all three answers are visible in one place, to a
reader and to `tools/` alike, where a callback's three answers are three
branches you have to run to see.

**Derivation is not lost.** An app whose size is genuinely computed —
`solitaire`'s card metrics, `paint`'s memory tier — fills its own table at
entry and then declares it. What changes is not *whether* it derives but *how
many answers it writes down*: today it patches `WT_W`/`WT_H` with the one
answer for the screen it happens to have booted on, and afterwards it records
all three.

### 3.2 `OSAPI_WM_MINSIZE` (0x0480) — a floor the kernel may not cut through

`BX` = window, `CX` = minimum outer width, `DX` = minimum outer height;
`CX = DX = 0` withdraws. A per-slot side table, 4 bytes x `MAX_WIN` = 48 bytes
of `.bss`.

Honoured by **every** path that reduces a size. Two of them already have a
floor — `ui_grow` and `wm_resize` clamp at the global `WMIN_W` x `WMIN_H`
(96x64) — and **two have none at all**: `wm_fit` and `wm_strad_fit` will cut a
frame to any height the display has. `WMIN_W`/`WMIN_H` stay as the default, so
a window that declares nothing behaves exactly as it does now.

**The floor is on the SIZE and never on the POSITION.** A minimum may not make
a window unreachable, so `wm_fit`'s y clamp and "the title bar has to land
somewhere a person can reach" still win: a window whose minimum is taller than
the display can offer hangs over the dock, which is `WF_KEEPH`'s behaviour
(§11.93) and is already handled by `wm_dock_under` and `wm_dock_clear`.

That equivalence is deliberate and is the reason to build this one second:
**`WF_KEEPH` becomes `min height = the height the template asked for`**, and
the two would share one floor in `wm_fit`. Retiring the flag is a follow-on
and not part of this — `tests/dispmine.py` pins its behaviour and `make
KEEPH=0` is its A/B — but the flag and the number must not be allowed to
disagree about the same window, so whichever is more generous wins and the
`wm_fit` code path is one.

**The minimum is itself clamped to the display's own extent**, which is
§11.93's fourth deliberate thing generalised: "the floor is the DISPLAY's
bottom and not infinity, so a template taller than the whole adapter is still
cut — a window whose title bar is off the screen cannot be dragged back". A
minimum may cost the user rows behind the dock; it may never cost them the
title bar.

**A window sitting at its minimum on a display too small for it does have
content in the dead zone**, and that is the accepted price of the rule rather
than an oversight — it is `mines` hanging over the dock, one display over. It
is also exactly why §3.4 matters: a fixed-size window that lands wholly on the
small display should be taking that display's *preferred* size, not sitting at
a minimum chosen for a bigger one.

**One minimum per window, not one per adapter.** A minimum is a statement about
a layout ("below this my controls stop fitting"), and an app whose layout
really is per-adapter has a per-adapter *preferred* size for that; if it needs
a different floor as well it re-declares one from its §11.98 handler, which is
the notice that tells it the environment moved.

### 3.3 The order the three clamps run in

Preferred size, then the display box as a **ceiling**, then the minimum as a
**floor** — and the floor is last so that it wins, which is the tree's own
existing idiom rather than a new rule: `ui_grow`'s size clamp is already
written "high bound first, low wins", and `ui_drag`'s x clamp and
`menu_drop`'s both carry the comment "low bound last so it wins". The floor is
then capped at the display's extent per §3.2. Getting this order wrong is not a crash: it is a window that is one pixel short of its
own minimum on one adapter out of three, which is §11.98.1.1's shape — a
clamp that reads its own output, right on every call but the one nobody tested.

### 3.4 Landing on a display adopts the preference — for the windows the user cannot resize

At `ui_drag`'s release, once the frame is wholly on one display, and that
display's adapter kind (`VID_CTX_KIND`, which the record already carries)
differs from the kind the window was last sized for: a window **without**
`WF_SIZABLE` takes its preferred size for the new kind, is re-fitted to that
display's box, and is told through §11.98.

A window **with** `WF_SIZABLE` does not. The user chose that size with the grow
box and the kernel may not overrule it; it gets §3.5 instead. That is the
distinction the request draws and it is the right one — a preference is what
the *application* wants, and a resizable window's size is what the *user*
wants, which outranks it. It also means the two halves of the request are
answered by different mechanisms and neither can cover for the other: §3.4 for
the windows nobody can resize, §3.5 for the ones somebody already has.

**One byte per slot records the kind the window was last sized for**, seeded at
`wm_create` from the display it lands on (12 bytes of `.bss`, `KERN_BIG` only).
Without it the adoption fires on every drag inside one display and overwrites
anything the app did with `wm_resize` between drags.

**`wm_refit` applies it too, and that is the single-display half of the same
rule.** §39.11.2 re-fits every window when the adapter changes under it, and
today that is `wm_nat_take` then `wm_fit` — replay what the window asked for,
clamp it to the new screen. With a preference it becomes replay, *then take the
new adapter's preferred size if there is one*, then clamp — so switching a VGA
machine to its CGA row on the Display page gives every window its CGA size
rather than the VGA one cut to 155. That is where most users will meet this,
since most machines have one card; the drag case is the same code reached from
the other side.

**`wm_zoom` (§11.95) is unaffected in both directions.** A zoom is the user
asking for the whole band, which outranks a preference exactly as a grow does,
and a minimum under it is unreachable because a zoom only ever grows.

### 3.5 The straddle cut stops being what a window goes back to

`ui_drag`'s release takes the **banked size** back before it clamps, re-runs
`wm_strad_fit` against where the window has actually been put, and banks the
**position only**. Every drag then re-derives the size from the bank and the
current position, which makes a seam round trip reversible in exactly the way
an adapter round trip already is.

This is the fix for §1.1's 200 → 140 → 140, and it is the smallest part of the
proposal: the bank site moves above `wm_strad_fit` and gains a size-restoring
sibling.

### 3.6 A release wholly on one display must be fitted to it (WITHDRAWN — §11)

§1.2's hole is not fixed by any of the above for a *resizable* window: it never
straddles at the release, so no clamp runs. The answer is that a release whose
frame lies wholly within one display ends in `wm_fit` — which is that routine's
existing job, `wm_fit_box` already answering with the box of the display the
origin is on — while a release that straddles keeps `wm_strad_fit`. `wm_fit`'s
position clamps are a no-op for a frame already inside the box, so this cannot
fight the drag.

This one is a bug fix and should land **first, on its own**, ahead of the two
API cells: it is the defect with content in the dead zone, and it wants a gate
of its own before anything else moves in `ui_drag`.

**It is not a bug, and §11 is the correction.** It shipped, the field looked at
it, and the premise turned out to be wrong: the rows below the short display
are not a *hole* in the desktop that a window may fall into, they are simply
somewhere no display is — the same somewhere as the rows below the tall one,
which every window in the machine is allowed to hang into. The clamp is
withdrawn; everything else in §3 stands.

---

## 4. What each package would declare

The kernel change is small; this is the bulk of the work and the part that
retires the hodgepodge. `w x h` are **frame** sizes. A dash is "no preference,
keep the template", which is the correct answer for every window that already
fits every adapter — the mechanism must not become something all 24 packages
have to fill in whether or not they have anything to say.

**The numbers below are a first cut, not a decision.** Each row is the app
author's call and several want looking at on the glass before they are fixed —
a full-width `word` on a CGA is 155 rows of a page and may read worse than a
narrower one. What the table is asserting is the *shape*: three answers per
window, declared in one place, instead of one answer and a clamp.

| package | VGA | Hercules | CGA | min | deletes |
|---|---|---|---|---|---|
| `arkanoid` | derived | derived | derived | – | `ark_entry`'s template patch |
| `artful` | 360x300 | 360x300 | 638x155 | – | – |
| `browser` | derived (90% of the band) | derived | derived | – | conditional `WF_KEEPH` |
| `calc` | – | – | – | – | – |
| `cyclone` | 322x219 | 322x219 | 322x155 | – | `cy_entry`'s three clamps |
| `fractal` | 322x199 | 322x199 | 322x155 | – | – |
| `frotz` | 560x396 | 718x303 | 638x155 | – | – |
| `hello` | – | – | – | – | – |
| `mines` | – | – | – | 146x183 | `WF_KEEPH` (§3.2) |
| `missile` | derived | derived | derived | – | `mc_entry`'s template patch |
| `modplug` | 418x215 | 418x215 | 418x151 | – | `MPP_FHF`/`MPP_FHC` pick |
| `notepad` | 260x180 | 260x180 | 260x155 | – | – |
| `paint` | derived | derived | derived | canvas min | `pt_wsize`'s screen half |
| `piano` | 224x177 | 224x177 | 224x155 | – | – |
| `recorder` | – | – | – | – | – |
| `solitaire` | derived | derived | derived | – | `sol_entry`'s band clamp |
| `tamegram` | derived | derived | derived | – | `tg_entry`'s template patch |
| `taskmgr` | derived | derived | derived | – | one of its two layout passes |
| `telnet` | 528x190 | 528x190 | 528x155 | – | – |
| `texpad` | 628x400 | **718x303** | 638x155 | – | – |
| `tracker` | 420x180 | 420x180 | 420x155 | – | – |
| `word` | 600x440 | **718x303** | 638x155 | – | – |
| `cword` | 592x384 | **718x303** | 638x155 | – | – |

Two things the table shows that the current scheme cannot express. The
**Hercules column is a size and not a cut** — `word` and `texpad` on a 720-wide
screen become 718 wide instead of staying VGA-shaped, which is 92 columns of
document nobody has ever seen. And the **CGA column is a decision** — 638x155
is "as wide as it can be and every row there is", which is a different
statement from 600x440 truncated, and one an app can lay out for.

The all-dash rows are the point as much as the entries are: `hello`,
`recorder` and `calc` fit every adapter already, and a mechanism they need not touch
is a mechanism that has not spread.

---

## 5. What it would cost

Measured against the tree at this commit (`make` and `make small`):

| | `.text` + `.bss` | image rung | footprint |
|---|---|---|---|
| `kern_big` today | 58,587 of 65,536 | 58,880, **293 left** | 110,080 of 112,128, 2,048 spare (4 steps) |
| `kern_small` today | 54,308 of 65,536 | 54,784, **476 left** | 104,448 of 105,472, 1,024 spare (2 steps) |

The estimate is ~84 bytes of `.bss` (24 preference offsets + 48 minimum sizes +
12 last-kind bytes) and ~250–350 bytes of `.text` (two API cells at 8 bytes
each in the jump table, `wm_pref_take`, a shared floor in `wm_fit`, and the
`ui_drag` release rework). On `kern_big` that **crosses one 512-byte rung**:
2,048 spare → 1,536, three steps, which is inside what the guard is for and
does not need a raise. On `kern_small` it lands inside the existing rung and
costs the footprint **nothing** — 476 bytes of slack against a ~430-byte
addition, which is close enough that it has to be re-measured rather than
trusted.

§3.4 is `KERN_BIG`-only (there is no second display on a small machine and no
`vid_ctx` to ask). §3.1 and §3.2 are in both builds, because deciding the size
at creation is a single-display question too — it is what the CGA column of §4
is about.

The application side is net **negative**: 12 bytes of table and one call
against the arithmetic nine packages delete. `sol_entry`'s band clamp alone is
about 40 bytes and nine lines of comment about one pixel.

---

## 6. How it would be checked

- **`tests/dispsize.py` is already written** and is what produced §1's two
  tables; it prints today's behaviour and takes `--gate` to assert the fixed
  behaviour instead, so on the day §3.5 and §3.6 land it becomes their gate
  with no new file.
- **`tests/dispstrad.py` already covers §3.6's opposite** and would gain the
  one-motion drag as a second case — the run in §1.2 is four lines added to it.
- **A new `tests/disppref.py`**: on the two-card machine, a fixed-size package
  opened on the Hercules, dragged wholly onto the CGA, and its frame compared
  against its own declared CGA pair read out of its image; then dragged back
  and compared against the Hercules pair. The assertion is arithmetic, for
  `dispstrad.py`'s stated reason — the dead zone is where nothing is drawn, so
  a capture of a broken build and a fixed one differ only in the part of the
  window that was never on either monitor.
- **A round trip is the property, not a size.** §1.1's table is the shape of
  the gate: rect and bank at four steps, and the assertion is that step 4
  equals step 1.
- **`make KEEPH=0` stays working** as long as §3.2 shares `wm_fit`'s floor with
  the flag, and `tests/dispmine.py` is what says so.
- **Byte identity on a single-card machine.** §3.1 and §3.2 must be a no-op for
  every package that declares nothing, and the cheap proof is an identical
  scripted session on `os8088_5150_cga_gla` and `os8088_5150_herc_gla` against
  a reference kernel: 0 differing framebuffer bytes.

---

## 7. Considered and not taken

- **A ninth and tenth template word.** `wm_create` copies **eight** words and
  growing that reads one word past every existing package's template — §11.93
  gives the same reason for `wm_keeph` being a call. Every one of these has to
  be a call.
- **Deriving the preference from `WF_SIZABLE` and the handlers a window
  installed.** "No negotiator and no §11.98 handler means fixed" lands on the
  right answer for every window in the tree today and is still wrong, for
  §11.93's reason repeated: it makes *adding* a resize handler silently change
  a window's size. The application knows; nothing else does.
- **Making the preference a callback.** §3.1's three reasons. The one that
  decided it is that `wm_fit` is a clamp site and not a dispatch site.
- **One preferred size plus a scale.** `piano` (§11.98.1) scales its keyboard
  and that reads correctly at any height, but a card table, a minefield and a
  tracker grid do not — §11.98.1 says so itself, which is why `arkanoid` and
  `solitaire` carry whole metric *records* rather than a scale factor. Three
  sizes is what the tree already believes.
- **Per-adapter position as well as size.** Position is `wm_fit`'s and the
  user's. An app that also wants to be centred can ask for its size and then
  `wm_resize`; an app that wants to be *put* somewhere is asking for something
  the window manager has always refused.
- **Retiring `WMIN_W`/`WMIN_H`.** They are the default a window gets by
  declaring nothing, and the whole design above depends on there being one.
- **Keying the table on the display's SIZE instead of on the adapter kind.**
  A height threshold is what `arkanoid`, `solitaire` and `tracker` already use
  (`>= 300` picks the big metric record), and it generalises to a display this
  tree has never had. It was not taken because the three adapters have fixed
  geometries, so kind *is* geometry here, and because the kind is the thing the
  record already carries (`VID_CTX_KIND`) and the thing apps already branch on
  (`OSAPI_VIDEO`'s `DL`). A threshold also has the failure §53.7.1 records: it
  answers about *a* height, and the moment there are two displays there is a
  question about *which*.

---

## 8. If it is built, this order

1. **§3.6 alone** — the release wholly on one display is fitted to it. It is a
   bug with content in the dead zone, it is small, and it wants its own gate
   before anything else moves in `ui_drag`.
2. **§3.5** — the bank moves above `wm_strad_fit` and a drag re-derives its
   size from the bank. Four instructions, and it is the fix for the defect as
   the field reported it.
3. **§3.2** — `OSAPI_WM_MINSIZE`, sharing one floor in `wm_fit` with
   `WF_KEEPH`. Useful on every machine, single display or not, and it is the
   half of the request that has no ordering hazards in it.
4. **§3.1 + §3.4** — `OSAPI_WM_PREFER` and adopt-on-landing, together, because
   the second is what the first is for.
5. **§4, package by package**, each its own commit, each looked at on a CGA —
   §11.94.3's list is the precedent for how that survey goes, and its lesson is
   the one to carry: most rows will turn out not to be worth changing, and
   knowing which is the useful half of the work.

Steps 1 and 2 are worth doing whatever happens to the rest.

---

## 9. What was built, and the one thing this document did not foresee

`docs/FIELD-NOTES.md` 26 is the defect record; SPEC.md §11.100 is the contract.

**§3.1–§3.6 all shipped**, with the numbers §5 predicted: `.text` +659 and
`.bss` +84 on `kern_big`, **one image rung**, footprint 2,048 → **1,536 spare**
(3 steps); `kern_small` **+0 footprint**, 476 → 75 bytes of rung slack.

**What this document did not foresee is `wm_reflows`** (SPEC.md §39.16.3.1),
which landed on `elendilon` between the investigation and the build. It says
`wm_strad_fit` may only shorten a window that **lays itself out from the live
content box** — because the frame is not the pixels, so shortening a fixed
layout stops the window manager knowing about rows the app goes on drawing,
and leaves 4,130 of them un-repainted in the corner where two displays meet.

Two consequences, and the second is the more interesting:

- **§3.6's hole clamp was gated the same way**, because it invents a size just
  as the straddle does. Without that it would have put the corner residue
  straight back — and it is withdrawn now for a different reason entirely
  (§11).
- **§3.1 is the answer to the residual §39.16.3.1 had to accept.** The kernel
  may not invent a size for a window that cannot follow — but it may hand one
  the window itself **declared**. `wm_reflows` asks "will you follow an
  arbitrary box"; a preference answers "I will follow these three". Different
  promises, and a fixed-layout window's corner leaves the dead zone not by
  being cut but by being asked for a size it published and redrawing at it.
  That is measured rather than argued: ModPlug on the CGA is 0 differing
  pixels against a full repaint.

**One thing §3.1 needed that this document missed**: an app cannot know how
tall another adapter's desktop band is — §1's own rule about `[vid_*]` says ask
rather than assume, and nothing answers *for an adapter you are not on*. It
does not have to: a preference is clamped by the screen exactly as a template
is, so a real **width** and a **generous height** is the whole idiom, and
638x300 on a CGA comes back 638x155.

### 9.0 A rule §3 got wrong, and `apps/browser` is what found it

§3.4 said a window **with** `WF_SIZABLE` never adopts, because a user outranks
an application. The conclusion is right and the test was wrong: `apps/browser`
is resizable and a freshly launched one is whatever size `br_size` chose, so
refusing on the flag threw away every browser nobody had grown yet — which is
all of them. The question is not *could* a user have sized this window but
*did* they, and that is one byte (`wm_usrsz`, SPEC.md §11.100.5) set by
`ui_grow` and `wm_zoom` and by nothing else. `wm_resize` deliberately does not
set it: an app sizing itself is the same voice a preference speaks in.

One rule now covers the drag, the adapter change and the declaration, where
§3.4 had a flag test in one place and nothing in the others. `tests/dispfit.py`
is what says the other half still holds: a window a user HAS grown comes back
at their size across a VGA → CGA → VGA round trip, which is §39.11.2.1's
promise and the one thing a preference may not break.

### 9.1 Where the rest of §4 stands

**`apps/modplug`** is the first and is the reference consumer — its two faces
(SPEC.md §56.4) were the tree's own hand-built version of this idea, so the
declaration is three lines and a §11.98 handler that keeps `[mpp_compact]`
following the box. Nothing about how it opens changed; what it gained is every
LATER moment the adapter can move under it.

**`apps/word`** (SPEC.md §11.100.8) is TeXPad's case again — two strips of
fixed-position chrome laid out to x = 376 against a `WMIN_W` of 96, so the grow
box reached 96x85 with both strips drawn onto the desktop. It floors at
402x143 and takes 712x303 on a Hercules, content 598 → 710.

**`apps/frotz`** is a DELETION: `zf_onsize` was ten instructions clamping to
`ZF_MIN_W`/`ZF_MIN_H`, which is the minimum said the long way and said only to
the two paths that consult a negotiator. It also carries the third instance of
"a fact tested once" — `[zw_vidok]` latching `[zw_bpp]`, dropped now on the
notice. No width: a story's line length is its author's typography, not the
screen's.

**`apps/texpad`** is the fourth (SPEC.md §11.100.7) and is the first consumer of
the **minimum**: two panes whose own clamp wants 120 + 140 columns against a
`WMIN_W` of 96, so the grow box could produce a preview pane **−26 pixels**
wide. It also carries the sweep's first per-adapter WIDTH — 712 on a Hercules
against 628 on a VGA, the preview page going 426 → 510 — with `(0, 0)` on the
other two adapters, which is the mechanism's "nothing to say" doing what it is
for.

**`apps/paint`** is the third (SPEC.md §11.98.2) and is the shape that has no
preference in it: seven facts taken from the adapter in `pt_geom`, once, and
**all seven** still describing the VGA after a switch to a CGA — a
sixteen-colour palette on a 1bpp screen, a 480-row surface for §53's bracket on
a 200-row one, a canvas ceiling of 390 over a band of 155. `pt_geom` also
claims memory, so the fix is a split (`pt_screen`) rather than a re-run.

**`apps/browser`** is the second (SPEC.md §11.100.6) and is a different shape of
consumer: what it decided once was not a size but a **question** — whether to
hang over the dock — so its conversion is a two-way `br_keeph`, the same
routine wired to §11.98, and three declared heights whose CGA entry is
deliberately generous so `WF_KEEPH`'s own ceiling produces it. Measured, a
browser switched onto a CGA was 496x**155** where one launched there is
496x**179**: the same machine, the same adapter, two different windows
depending on how you got there.

The rest of §4's table is unconverted and is still a first cut. Two findings
from doing the first one are worth carrying into the rest:

- **`artful` was checked and needs nothing**, which is the other useful
  outcome: `at_geom_init` re-runs on every fullscreen entry and its windowed
  splash reads the live content box, so both of its modes already follow. So
  do `tamegram` and `tracker`, whose per-frame re-derivation §11.98's survey
  claims and which turn out to be right.
- **A conversion may involve no preference at all.** `apps/paint` declares
  none and never will — its box already drives everything it draws, and a
  published size would fight the negotiator that refuses to crop artwork. What
  it needed was the §11.98 handler alone, for seven facts it took from the
  ADAPTER once. So the sweep is really two questions per package and only one
  of them is about size.
- **A routine that is called twice has to answer BOTH ways**, and this has now
  turned up in two of the three conversions: `br_keeph` set `WF_KEEPH` on a CGA
  and never cleared it, `pt_screen` set `[pt_mono]` on 1bpp and never cleared
  it. Both are correct exactly once — at launch, when `.bss` is zero — and both
  are wrong the first time anything asks again. Look for it in every package
  that latches an adapter fact.
- **A conversion can be about a FLAG rather than a size.** Browser's real
  defect was `WF_KEEPH` decided once; the three heights are the smaller half.
  The question to ask of each package is not only "what size do you want here"
  but "what did you decide once, from the adapter, at launch".
- **A conversion is usually TWO changes, not one.** Declaring the sizes is the
  easy half; the other is whatever byte the app derived from its size *once*
  at launch. ModPlug's `[mpp_compact]` chooses a whole coordinate table, and
  without a §11.98 handler to keep it in step the window would have taken the
  compact frame and drawn the full face into it — §39.16.3.1's defect
  reintroduced by the thing meant to pay it off.
- **Most rows will not be worth changing**, and knowing which is the useful
  half of the work — §11.94.3's survey is the precedent. Measured, ten of
  nineteen windows are cut to exactly 155 rows on a CGA, but for most of them
  the honest CGA preference *is* 155 and declaring it changes nothing. The
  ones with something to say are the ones whose good CGA shape is a different
  **width** (`word`, `texpad`, `frotz`, `cword`, `artful`) and the ones that
  already carry two layouts (`modplug`, done; `tracker`; `taskmgr`).

### 9.2 One pre-existing failure this work did not cause, and half of one it fixed

`tests/dispcalcx.py` fails on `origin/elendilon` and fails here, and the
comparison is worth recording because the numbers moved in one direction only.
Dragged over the seam onto the CGA, `apps/calc` leaves **220 differing pixels**
— one row, `x 90..309 y 134` — on **both** builds, identically; the test's own
message attributes it correctly ("the residue is the package's, not the window
manager's"). What changed is its **control**: a kernel Disk window over the
same seam left **746** pixels before and leaves **0** now — that one is
§3.5's, the drag re-deriving its size from the natural bank, and it survives
§11's withdrawal of §3.6 (re-measured: still 0). So the window-manager half of
that test is clean for the first time and the package half is exactly where it
was.

The file is also flaky at 6–20 pixels in the menu-bar band on both builds
(`x 704..709`, varying run to run, which is where the clock is) — a
capture-and-repaint pair straddling a minute boundary.

---

## 10. The sweep, finished

| package | preference | minimum | §11.98 handler | what it fixed |
|---|---|---|---|---|
| `modplug` | 3 sizes | – | added | two faces, picked once |
| `browser` | 3 heights | – | added | the dock question, decided once |
| `paint` | – | – | added | seven adapter facts, taken once |
| `texpad` | Hercules width | **262x103** | – | a preview pane −26 px wide |
| `word` | Hercules width | **402x143** | – | chrome drawn onto the desktop |
| `frotz` | – | **200x120** | added | a negotiator, and a latched depth |

**Ten more were checked and need nothing**, which is the half of the work worth
having done: `artful` (re-derives on every fullscreen entry, splash reads the
live box), `tamegram` and `tracker` (per frame), `taskmgr`, `calc`, `piano`,
`solitaire`, `arkanoid`, `missile`, `cyclone` (all register §11.98 already),
`fractal` (per paint), and `hello`, `recorder`, `mines`, `telnet`, `notepad`,
`calc` — whose honest per-adapter size *is* what the clamp already gives.

### 10.1 Three shapes, and only one of them is a size

The sweep's own finding, and it is not what §4's table predicted:

- **A latched ADAPTER FACT** — `paint`'s seven, `frotz`'s `[zw_bpp]`,
  `modplug`'s `[mpp_compact]`, `browser`'s `WF_KEEPH`. Four of the six, and the
  fix is a §11.98 handler; two of them declare no preference at all.
- **A FLOOR the app already knew about** — `texpad`'s two panes, `word`'s two
  strips, `frotz`'s negotiator. Each already had the constants; what it lacked
  was a way to say them to the kernel.
- **A per-adapter SIZE** — `modplug`'s two faces, `texpad` and `word`'s
  Hercules width, `browser`'s three heights. The thing §4 assumed the sweep was
  about, and it is the least of the three.

### 10.2 The rule that turned up three times

**A routine that is called twice has to answer BOTH ways.** `br_keeph` set
`WF_KEEPH` on a CGA and never cleared it; `pt_screen` set `[pt_mono]` on 1bpp
and never cleared it; `zw_geom` latched `[zw_bpp]` and never dropped it. All
three are correct exactly once — at launch, when `.bss` is zero — and all three
are wrong the first time anything asks again. It is the signature of a
conversion, and the thing to grep for in any package added later.

### 10.3 What is not done

`apps/cword` (SPEC.md §11.100.8.1). It would be `word`'s change plus two thunks
in `apps/cc/os88thunk.asm` and their `os88.h` declarations — and it is not done
because **no C package can be built in this container**: `make cword` and
`make chello` both fail on duplicate labels between SmallerC's output and the
hand-written thunks, and both fail identically at `origin/elendilon` with none
of this work present. §73's own hazard (a `call`/`retf` shim moving a compiled
routine's arguments two bytes) is precisely the class that needs a build to
catch, and this tree's cautionary tale about shipping unexecuted code is
`OS88NET.COM`, which reached the field twice without one instruction of it ever
having run.

---

## 11. §3.6 is withdrawn: the union's bounding box is not a surface

**The field looked at the finished work and reported one thing:**

> The windows are behaving much better. First bug I noticed: on the primary
> screen, windows are allowed to go 'below the desktop' — they keep their
> shadow under, and can be moved down and up. On the secondary screen they are
> always resizing, even if they have not crossed a screen boundary.

Both sentences are about **one act**, which is what makes it a premise error
rather than a tuning one. On the field machine's pair — a 720x348 Hercules at
(0,0) and a 640x200 CGA at (720,20) — `[vid_h]` is **348**, the union's
bounding box. §3.6's clamp asked *is this display's last row inside the
desktop*: for a window on the Hercules the answer is `348 >= 348`, so it was
left alone and could hang off the bottom; for one on the CGA it is `220 < 348`,
so it was cut — **every time, having crossed nothing.**

The guard was reading the bounding box as a *surface with a hole in it*. It is
not a surface at all: rows 220..347 at x ≥ 720 belong to no display, exactly as
row 349 at x < 720 belongs to none. §39.2.1's dead zone is a real place and a
window is allowed to hang into it, from either display, in the same way and for
the same reason.

**The fix is one branch.** `wm_strad_fit`'s "…and it must reach the other one"
test gates the whole clamp again (`jc .none`), rather than gating only the
*other* display's contribution to the limit. A window that straddles is still
cut to the rows both displays have; a window that never crossed is left exactly
as the drag put it. SPEC.md §39.16.3.2 is the record and
`tests/dispsize.py` leg B now asserts the opposite of what it asserted for one
round — 303 rows out, **303 rows back**.

**What is kept from §3.6 is the finding, not the code**: a rule derived from
the union's bounding box will treat regions no display has as though the
desktop owned them. `[vid_w]`/`[vid_h]` describe an *extent*, and the thing to
ask a question of is `vid_ctx[]`.

Cost of the withdrawal: nothing moved. `kern_big` is still one image rung and
**1,536 spare**, `kern_small` still **+0** — the branch that changed is inside
`wm_strad_fit` either way.

---

## 12. The rule is about ADAPTER IDENTITY, not geometry

**§3 and §11 were both written as geometry** — *does this frame fit inside that
box* — and every defect the field has reported since is the same substitution
error. The rule the machine wants is an identity one:

> **My preferred/minimum size is X on adapter kind Y, and I am standing on
> adapter kind Y.**

The second field report is what forced it, and it named three symptoms at once:
a window dragged from one place to another **on the CGA** came back at its
**Hercules** size; Missile Command straddling was cut to a letterbox that
changes the gameplay; and the Disk window could be clamped until it showed no
file rows at all.

### 12.1 The trigger: a window is resized when the ADAPTER KIND under it changes

Never on a move within one kind, never on a grow (that is the user acting),
never otherwise. One comparand, `[wm_pkind]`, which already exists.

Three things were wrong before and all three are this rule missing:

- **`ui_drag_size` called `wm_nat_take_wh` unconditionally**, so every release
  anywhere restored the natural bank — which holds the rect the window was
  BORN with. A drag from place A to place B inside the CGA therefore handed it
  the Hercules size. §3.5 introduced that restore to fix "the straddle cut is
  permanent", and the correct fix is not to restore a bank but to **take the
  size for the kind you have arrived on**.
- **the straddle was its own mechanism**, cutting geometrically to the rows
  both displays have. That produces an *arbitrary* number, never a designed
  one.
- **a landing was a third mechanism** (§3.6, withdrawn in §11).

One mechanism replaces all three.

### 12.2 The question: which kind am I on?

- Wholly on one display → that display's kind.
- **Straddling two displays → the MORE RESTRICTIVE of the two**, meaning the
  one with the smaller usable band along the axis they tile on (side by side ⇒
  the shorter desktop band; stacked ⇒ the narrower).

That second line is what makes the seam feel right rather than merely correct.
A Hercules-to-CGA round trip becomes **exactly two resizes**: one entering the
straddle, one leaving it on the far side back home.

| step | kind | resize? |
|---|---|---|
| on the Hercules | HERC | – |
| → straddle | CGA (restrictive) | **yes** |
| → wholly on the CGA | CGA | no |
| → straddle | CGA | no |
| → wholly on the Hercules | HERC | **yes** |

Under §3/§11 the same trip was four resizes, two of them to numbers no-one
designed.

### 12.3 The answer: the size for that kind

In priority order:

1. **The user's own size**, if they grew or zoomed it (`wm_usrsz`, §11.100.5) —
   a user outranks an application, unchanged.
2. **The declared preference** for that kind (`OSAPI_WM_PREFER`, §11.100.1).
3. **The natural size** — the rect the window asked for (§39.11.2.1).

…then **clamped to the display box if and only if the window will follow**
(§12.5), and **floored at the declared minimum** (`OSAPI_WM_MINSIZE`,
§11.100.2), which outranks the box.

**A preference is a REQUEST, not a floor**, and the distinction is load-bearing
rather than pedantic. §9's own idiom is *a real width and a generous height*,
because no application can know how tall another adapter's desktop band is —
`638x300` on a CGA is meant to come back `638x155`. Make a preference a floor
and that idiom becomes a window hanging off the bottom of every small screen.
Missile Command's minimum happens to EQUAL its CGA preference, and that is the
application saying so, not the kernel deriving it.

### 12.4 Flooring out SNAPS on a kind change and HANGS within one adapter — and both fall out of the trigger

When the floor beats the box the window cannot fit where it was dropped, and
there are two honest answers: leave it hanging past the bottom, or move its
origin up until it fits. The field settled it, and the reasoning is what to
keep:

> "If we let it hang, then we don't override their movement — but they might
> now have to move again. I think snap is right after all, here. […] they
> probably didn't mean to hang it — and the reason is because they didn't know
> what its floor was, visually, as they were dragging it. But if they are on
> the same adapter, we hit the 'moved within A' rule and it hangs."

**A user cannot see a window's floor while dragging it**, so a placement that
floors out on a kind change was never an informed one; a placement on the
adapter they are already on is.

**Neither half costs a line of code.** `wm_fit` already applies the floor
*above* its position clamps and then floors y at the band top, so a window
whose minimum binds ends exactly on the display's last row — that IS the snap.
And a move within one kind never reaches `wm_fit` at all, because §12.1's
trigger does not fire, so it hangs by construction rather than by a rule. The
hang a window keeps on the primary (§39.16.3.2) and the snap it gets crossing
a seam are therefore the same mechanism seen from two sides.

### 12.5 `wm_reflows` tightens to `WF_SIZABLE`

Its second arm — *has an §11.98 handler* — is too loose, and it is what cut
Missile Command into a letterbox. **A size-CHANGED handler means "tell me when
my box moved", not "you may pick any box for me."** Nine packages register one
(Solitaire, Missile, Calc, Piano, Task Manager, Cyclone, Arkanoid, Paint,
Frotz) and only some of them lay out from the live content box.

So the test becomes `WF_SIZABLE` alone, keeping the `WF_KEEPH` veto. A grow box
is a promise the app can keep: it moves the content box on the user's whim, so
an app that takes one has no choice but to lay out from the live one.

**An app that is not sizable opts in by DECLARING A PREFERENCE**, which is a
promise about three specific boxes rather than about every box. That is
§9's answer to §39.16.3.1's residual, and it now carries the whole
non-sizable case rather than half of it.

| the window is… | on a kind change it gets… |
|---|---|
| `WF_SIZABLE` | its natural size **clamped to the box**, floored at min |
| not sizable, declares a preference | that preference, floored at min, not clamped |
| not sizable, declares nothing | **nothing at all** — it keeps its size and may hang |

The third row is the pre-straddle behaviour, which nobody ever reported.

### 12.6 `OSAPI_VIDEO` answers about the PRIMARY, so a package on the second monitor is blind

The slot's own comment says so and is right about why (§39.2.1: answering the
union made Arkanoid lay itself out for a 1360px screen). But **every §11.98
handler in the tree re-reads it**, so `sol_onresize` standing on the CGA
re-derives *Hercules* card metrics and `pt_screen` banks `pt_scrh` = 348 while
its window sits on a 200-row display. The handlers follow **Activate Mode**
correctly — that moves the primary — and do not follow a **drag**.

Two things are missing and neither is a package's to fix:

- **`OSAPI_WM_DISPLAY`**, a new cell at the tail of the table — `OSAPI_VIDEO`
  for the display *your
  window* is on: width, height, the first row the dock owns (or the
  display's own bottom on a secondary), kind, bpp. It is `wm_fit_box` published, and it
  is §53.7.1's `OSAPI_FSX_SURF` lesson arriving in the windowed half of the
  system: *fullscreen is not the whole desktop, and neither is a window's
  screen.*
- **§11.98 must fire on a KIND change with the size unchanged.** `wm_sz_notify`
  runs only when the box moved, so an app dragged to another adapter at the
  same size is never told its adapter facts went stale.

### 12.7 Every situation

**One display — the ordinary machine, and nothing here may change.**

| | |
|---|---|
| launch | template, fit to the band, floored at min |
| move | nothing |
| grow / zoom | the user's size, floored at min, clamped to the band |
| Activate Mode | kind changed → §12.3 |
| minimize / restore | nothing |

**Two displays of the SAME kind**

| | |
|---|---|
| any move, straddle included | the kind never changes → **nothing ever resizes** |

**Two displays of DIFFERENT kinds**

| | |
|---|---|
| moved within A | nothing |
| A → straddle | kind → restrictive(A,B): one resize if B is the smaller |
| straddle → wholly B | **nothing** — the kind is already B |
| A → wholly B in one drag | one resize |
| B → straddle → A | nothing, then one resize home |
| grown by hand on B, then → A | the user's size, clamped and floored; no preference |
| min > B's band, dragged to B | floors, and **snaps up** to end on B's last row |
| no preference, not sizable, → B | nothing |
| Activate Mode swaps the primary | kinds may be unchanged and the BOX moved (a display gains a dock, another loses one), so `wm_refit` must still fit |
| Extend on/off | displays appear or vanish; every window fits |

### 12.8 Build order

1. **The kernel**, in one commit — `wm_kind_now` learns the straddle;
   `ui_drag_size` fires only on a kind change; `wm_strad_fit`'s geometric body
   is deleted; `wm_reflows` tightens; `wm_sz_notify` fires on a kind change.
   Deleting the straddle body should roughly pay for the rest.
2. **`OSAPI_WM_DISPLAY`**, and repoint the three §11.98 handlers where being
   wrong is visible: Paint, Missile, Solitaire.
3. **The Disk window's minimum**, `194 x 92`. 194 is exactly where `fm_nch`
   reaches 12 characters, so a full 8.3 name **plus** the size column **plus**
   the scroll bar; 92 is exactly 2 file rows plus the header, the buttons and
   the status line. It already degrades its own chrome below cw 142, so nothing
   else in `files.inc` moves.
4. **Missile Command** — per-adapter preferences and a minimum equal to its CGA
   preference, because the playfield size is gameplay.
5. **The rest of the §11.98 handlers** onto `OSAPI_WM_DISPLAY`.
6. **Solitaire, LAST** — a compact CGA tableau is a real package change rather
   than a system one, and it is the one window in the tree whose content
   genuinely does not fit a 155-row band: a King on the deepest pile with a
   full run under it is 6 face-down plus 13 face-up.

---

## 13. The `OSAPI_VIDEO` sweep, and what it found that §12.6 did not predict

§12.8 step 5 was "the rest of the §11.98 handlers onto `OSAPI_WM_DISPLAY`", and
the list turned out to be **shorter** than §12.6 assumed and the real problem
**wider**.

### 13.1 Only half the handlers ever read the adapter

Nine packages register an §11.98 handler. Four of them never ask about the
adapter at all — they re-derive from the **content box**, which is what the
handler hands them, and that was right before this work and is right after it:

| | |
|---|---|
| `calc` | `OSAPI_WM_CONTENT` + `OSAPI_WM_GEOM` |
| `piano` | `OSAPI_WM_GEOM` |
| `taskmgr` | the handler takes `CX`/`DX`; its one `OSAPI_VIDEO` is at **launch**, before a window exists, which is correct — a window is born on the primary |
| `modplug` | the handler is `OSAPI_WM_GEOM`; its adapter question is in `mppu_layout` (below) |

And **`cyclone` had already solved it**, the same way `missile` had: `cy_pal`
asks `OSAPI_FSX_CAPS` with `BX` = its window, which answers *that display's*
kind. Two packages got there before the kernel did.

So step 5's real list was three: `arkanoid`, `browser`, `frotz`.

### 13.2 …and the class is bigger than "an §11.98 handler"

Grepping every `call OSAPI_VIDEO` in `apps/` splits cleanly in two. Most are in
an entry proc — **launch time, before a window exists** — and are correct by
construction. The rest run with a window in hand, and every one of them was
asking about the primary:

| | |
|---|---|
| `modplug` `mppu_layout` | the LCD ink and the chassis body are §39.4 questions (green and `CLGRAY` are both dither classes), decided per layout |
| `artful` `at_geom_init` | called again at **fullscreen entry** — and §11.2 sizes a fullscreen window to the display it is *on*, so a Writer entered on the second card was laying its text column out for the primary's width |
| `frotz` `zwin6.inc` | v6 pictures' mono decision |

All three had a window pointer within a line or two of the call.

**One is left and it is not a call swap**: `cyclone`'s fullscreen path banks
`cy_scrw`/`cy_scrh` from `OSAPI_VIDEO` on either side of `OSAPI_FSX_RUN`, and
its own comment already says *"the bracket's display, which on a multi-display
machine is not necessarily the one we were on"*. §53.7.1's answer is
`OSAPI_FSX_SURF`, asked **inside** the bracket — the calls here are outside it,
where that cell cannot answer. Recorded rather than guessed at.

### 13.3 The trap that repeats: `SI` is an output now

`OSAPI_WM_DISPLAY` answers the band's top in `SI`, and three of the five
conversions had a routine that promised *preserves all* while framing only
`AX`–`DX`. `browser`'s `br_keeph` was the sharpest: it banked its window
pointer **in SI** precisely because `OSAPI_VIDEO` returns the height in `BX`,
so the obvious swap would have destroyed the pointer a different way. It banks
on the stack now, and its comment keeps the original lesson — the run where
`WM_KEEPH` was handed the number 200 as a window pointer.

### 13.4 What is still owed, and it is one shape

**`arkanoid` and `solitaire` are the same window-sized problem**: 303 rows of
game on a 200-row CGA, kept because neither is `WF_SIZABLE` and neither
publishes a preference (§12.5's third row). Arkanoid's is the cheaper of the
two — `ark_met_sml` already exists, so the small screen is designed for and the
CGA preference is just declaring the size its own entry code would compute
there. Solitaire's is a real layout change (§12.8 step 6). They belong in one
round.

---

## 14. Solitaire: the arithmetic does not close, and that is the finding

> **§14.2 IS WRONG AND §16 IS THE CORRECTION.** It says the clamp cuts about
> seven cards off the deepest pile. It does not: `sol_colfan` has always
> compressed a deep column to fit, face-up step first. The pile is SQUASHED,
> never clipped - so §14.3's option 2 was already built, and the question was
> never *how do we fit* but *how few pixels a card does the worst case get*.

§12.8 step 6 was "a compact CGA tableau, because a King on the deepest pile
with a full run under it is 6 face-down plus 13 face-up". Measured, **the
compact tableau already exists** — and it is still 89 rows too tall.

### 14.1 What is already there

`sol_metrics` has had two records since the app was written and picks between
them on the screen height, exactly as `apps/arkanoid` does. Both frame sizes
fall out of the record fields:

| | `big` (VGA, Hercules) | `sml` (CGA) |
|---|---|---|
| card | 32 x 44 | 28 x 28 |
| gap / margin / top | 4 / 4 / 4 | 3 / 3 / 1 |
| fan, face-down / up | 5 / 14 | 3 / 12 |
| `pitch` = cw + gap | 36 | 31 |
| `taby` = topy + ch + 2·gap | 56 | 35 |
| **frame width** = pitch·7 + 2·marg − gap + 2 | **258** | **222** |
| **frame height wanted** = 6·fand + 12·fanu + taby + ch + 19 | **317** | **244** |

So the first half of the work is `apps/arkanoid`'s exact change and nothing
more: publish `258x317` for VGA and Hercules and `222x244` for CGA
(§11.100.1), derive those four numbers from the record fields so the two cannot
drift, and floor at the CGA pair (§11.100.2). The Hercules clamp takes 317 to
303, which costs one fan step and is what the code already says.

### 14.2 …and it does not fit, by 89 rows

A CGA's desktop band is **155**. The `sml` record wants **244**. The clamp
takes the difference out of the bottom of the tableau, which is 89 rows ≈ **7
face-up cards** off the deepest pile.

`sol_entry`'s own comment says the clamp "costs Hercules and CGA one row of
tableau". That is true of Hercules and wrong about CGA by a factor of seven,
and it should be corrected whatever else is done here.

**Fanning cannot close it, and the arithmetic is short enough to check.** The
tableau has `content_h − taby` = `(155−19) − 35` = **101 rows**, and the
deepest column needs `6·fand + 12·fanu + ch` to show a King with a full run:

| fand | fanu | ch | needs | against 101 |
|---|---|---|---|---|
| 3 | 12 | 28 | 172 | −71 |
| 2 | 5 | 28 | 100 | fits, at 5 rows a card |
| 2 | 5 | 22 | 94 | fits, at 5 rows a card |

A rank glyph is 8px tall and sits 2px down, so **10 rows is the floor for
reading a card**, and 12·10 = 120 alone exceeds the 73 rows left after the
face-down fan. **A fully-built King column cannot be shown legibly on a
155-row display.** That is arithmetic, not a layout failure, and no choice of
constants changes it.

### 14.3 So the choice is about how it DEGRADES

1. **Publish the sizes and leave the clip** — §14.1 alone. Solitaire becomes
   correctly proportioned on the small card and its deepest pile is cut, which
   is what a CGA-primary machine has always done. Cheapest, honest, and no
   worse than today.
2. **Adaptive fan** — `sol_fanu` becomes per column, computed each layout from
   that column's depth and the space left, capped at the record's value and
   floored at 10. An ordinary game is pixel-identical to today; only a deep
   pile compresses, and it compresses *gradually* instead of falling off the
   bottom all at once. Past the floor it still clips. This is what desktop
   Klondike implementations do. Cost: `sol_fanu` becomes an array and about
   eight y-computations take the column's step — every one of them already has
   the column index in hand.
3. **A different CGA layout** — two tableau rows, or a scrolling content. A
   real redesign, and scrolling in particular fights the drag-and-drop this
   game is built on.

**My recommendation is 1 then 2, as separate commits.** 1 is mechanical, is
the same change three other packages just took, and stands on its own. 2 is a
genuine improvement to every adapter (a deep pile on Hercules is clipped by
one card today) and is where the judgement is — so it should be looked at on
the glass before it is kept.

### 14.4 What a gate can and cannot say here

`tests/dispsize.py`'s sweep can assert the FRAME follows the card, the way it
does for Paint, Arkanoid and the Task Manager. It cannot assert the tableau is
readable. That one is a screenshot of a deliberately-built King column, on a
CGA, looked at by a person.

---

## 15. Cyclone, Arkanoid, the Task Manager — and one gate left red

### 15.1 `apps/cyclone` was doing its own thing because the mechanism did not exist

Its fullscreen path banked `cy_scrw`/`cy_scrh` from `OSAPI_VIDEO` on either
side of `OSAPI_FSX_RUN`, and `cy_org`'s fullscreen branch returned **(0,0)**
with them. Its own comment already named the problem — *"the bracket's
display, which on a multi-display machine is not necessarily the one we were
on"* — and there was nothing to ask instead when it was written.

There is now. `OSAPI_FSX_SURF` (§53.7.1) answers the rect a bracket owns, and
this bracket sets no video mode, so §39.18.3 does **not** collapse the
desktop: the coordinates are the whole virtual desktop's and (0,0) is the
**primary**. Fullscreening Cyclone on the second monitor drew the entire game
onto the other one — §53.7.1's measured defect, in a second consumer.

`cy_fsx_main` asks at the top and banks x, y, w and h; `cy_org` returns all
four. The pre-bracket `OSAPI_VIDEO` is **gone** rather than kept as a
fallback, and so is the post-bracket re-bank: those four words describe a rect
a bracket owns, `cy_org` reads them only while one is up, and re-filling them
from the primary's size on the way out left a pattern for somebody to copy.
Cyclone is down to one `OSAPI_VIDEO`, in its entry proc, which is correct.

### 15.2 `apps/arkanoid` had the small record all along

It has had two metric records since it was written and `ark_metrics` picks
between them on the screen height — so the game *does* get fatter and squatter
on a CGA. What never followed was the **frame**: a game dragged onto a CGA
re-derived a small wall and drew it into a window still 303 rows tall.

The three sizes are derived from the record fields rather than written twice —
`ARK_BW_*`, `ARK_RAIL_*` and `ARK_CHW_*` are named once and spent in both the
`dw` records and the preference — so 250x319 and 208x156 cannot drift from the
layout they are meant to hold. Measured across the seam: **250x303 → 208x156**.

### 15.3 The Task Manager's two-column mode could never be reached twice

Reported from the field: *"Task manager has a CGA two column mode, and its not
getting that when it intersects CGA."* Two things compounded.

**`tm_vw` was handed the FRAME's width.** `tm_layout`'s one reader of it asks
*is there room for a second column* — and a one-column Task Manager is 232px
wide, 232 is less than `TM_W + TM_COLW`, so once `tm_onresize` had run the
answer was **no for ever**. It is the display's width now, which is what the
launch path had always passed.

**And the frame never followed.** `tm_layout` writes `tm_tpl+4/+6` — the frame
this adapter's layout needs — and nothing applied it after launch. A static
preference cannot express it (the numbers are computed), so `tm_onresize`
raises `[tm_szowed]` and **`tm_worker` spends it** with `OSAPI_WM_RESIZE`:
drawing is forbidden in an §11.98 handler and a resize repaints, and the
worker is the one place with the lock, no clip armed, and a repaint of its own
to give up. Measured: **232x284 / 1 column → 464x196 / 2 columns**.

### 15.4 `tests/dispapps.py` is RED and it is this work's doing

A/B'd: it passes with the Task Manager change stashed and fails with it. The
final `switch_to(VID_VGA)` never takes, with six windows open on a CGA.

**What has been ruled out**, and the gate now prints the evidence rather than
only the verdict (`dispfit.switch_to` dumps the z-order and the panel's rect
on failure — that dump is what ruled these out):

- **Not the Task Manager covering the panel.** The Control Panel is
  **frontmost** in every failing dump, at (159,24,320,151) — the same rect it
  has in a run that succeeds.
- **Not geometry.** An isolated probe — Task Manager alone, VGA → CGA → VGA —
  switches **both ways correctly**, 232x284 → 464x152 → 232x284.
- **Not a fullscreen bracket.** Missile's `0x0103` is bits 0, 1 and **8**, and
  bit 8 of `W_FLAGS`' high byte is §7.2.1's cursor shape, not `WF_FULL`.
- **Not `settle` failing between the two clicks.** Degrading it to a bounded
  wait changed nothing.

Two speculative harness fixes were tried and **reverted rather than left in**:
raising the panel by its title bar (which lands on whatever covers it, and
made the failure worse) and by the chip menu (which may reset the panel's
selected page, and did not help either).

So: the change is right, its own gate proves it, and `dispapps` is red for a
reason I have narrowed and not found. It should not be left there.

---

## 16. …and option 2 was already there. What the room is actually worth

**`sol_colfan` has existed since the app was written.** It takes each column's
depth, compares it against `[sol_avail]` — the room a column's *last* card may
start at — and tightens the face-up step first, then the face-down one, never
below one pixel. §14.2's "seven cards off the deepest pile" is wrong: nothing
is clipped, it is *compressed*. What is worth measuring is therefore not
whether a King column fits but **how many pixels of card the player gets**.

Measured, with a full King run (6 face-down, 13 face-up):

| | `sol_avail` | King | 6d+7u | 6d+9u | 6d+11u |
|---|---|---|---|---|---|
| today, clamped to the dock | 73 | 4 | 9 | 6 | 5 |
| **+ `WF_KEEPH`, over the dock** | 97 | 6 | **12** | 9 | 7 |
| + top gap 6 → 3 | 100 | 6 | 12 | **10** | **8** |
| + top gap 6 → 1 | 102 | 7 | 12 | 10 | 8 |

**The dock is the win, and it is not the worst case that it wins.** A 7-card
build stops compressing *at all* — 9px a card becomes the record's full 12 —
and a 9-card one goes 6 → 9. Those are the columns an ordinary game actually
builds. The King column, which most games never reach, goes 4 → 6.

**The top gap is worth one step for the middle cases and stops there.** 6 → 3
takes a 9-card build to 10 and an 11-card to 8; 6 → 1 buys the King column its
seventh pixel and nothing else, and a 1px gap between the foundation row and
the tableau does not read as a gap. So `SOL_TGAP_S` is **3**.

What that costs: on a CGA the window hangs over the dock strip, so the dock's
tiles are behind it while Solitaire is up — `apps/browser` already makes that
trade on the same adapter and for the same reason.

Both numbers are per-record, and every field the two published frame sizes are
a function of is named once and spent twice (`SOL_CW_*`, `SOL_TGAP_*`, …) —
`apps/arkanoid`'s discipline, for `apps/arkanoid`'s reason.

**A gate can say `sol_avail` went from 73 to 100. It cannot say the cards are
readable.** That is a screenshot of a deliberately-built King column, on a CGA,
looked at by a person — and it is worth doing before this is called finished.

---

## 17. A HARD FREEZE, and the rule it leaves behind

Reported, reproducible, second drag back: *"Had task manager sitting in its
'herc' size on the cga screen; opened a file manager; chip → Task Manager
again; dragged it from CGA fully to herc; dragged it from herc fully back to
CGA; froze."*

**A WORKER MAY NOT CALL `OSAPI_WM_RESIZE`.** §15.3 deferred the Task Manager's
resize to `tm_worker` on the reasoning that an §11.98 handler may not draw and
a resize repaints — which is true, and picks the wrong escape. A resize
repaints, and a repaint runs **every overlapped window's `W_PAINT`** through
`wm_pkgcall` — on the calling task, holding the gfx lock. A Disk window's is
`fm_repaint`, which can re-list a folder. Hence *"opened a file manager"* in
the reproduction: it is not incidental, it is the ingredient.

**The right answer was the one every other package here already uses.** The
frame is **published** (`OSAPI_WM_PREFER`) and the KERNEL applies it, inside
`wm_land_fit`, at the one moment a resize is safe — before any repaint, on the
UI task. That also fixes the *"two full redraws on drag"* in the same report:
there were two because the kernel resized and then the worker resized again.

`tm_layout`'s arithmetic became `TM_PREF_H`/`TM_PREF_W1`/`TM_PREF_W2`. The
height is the one a machine WITH a store wants, deliberately: `TMM_XSHIFT` is
added back when there is no XMS bar to draw, which is a property of the
**machine** and not of the adapter, so no per-adapter constant is exact for
both — and a blank row is cosmetic where a missing process row is not.

### 17.1 Two answers to one question

The same report's other half — *"it isn't picking its more restrictive mode on
a straddle"* — was a second defect and a more interesting one.
`wm_kind_now` reads a straddling window as the **more restrictive** display
(§12.2), because that is what its frame is sized for; `OSAPI_WM_DISPLAY` read
the display its **origin** sits on. So the Task Manager straddling was handed
the CGA's frame and told the Hercules' geometry, concluded one column fits, and
stayed one column inside a window twice that wide.

`wm_disp_now` is the single answer both come through (SPEC.md §39.16.4.1).
**Two answers to one question is the defect, not either answer** — either rule
is defensible alone; a window sized against one and informed by the other is
not, and nothing on screen says which it believed.

---

## 18. The Task Manager's height, and `tests/dispapps.py` back to green

§17 published `TM_PREF_H` as *the height a machine with a store wants* and
called the other machine's blank row an accepted cosmetic cost. It was neither
accepted nor cosmetic: `dispapps` measured `tm_colrows` **20 against 19** and
failed, because a row of slack IS a row `tm_layout` counts.

**The app knows.** `[tm_xoff]` is `TMM_XSHIFT` on a machine with no XMS bar to
draw and 0 otherwise, it is settled long before the window exists, and the
preference table is in the package's **own segment** — so `tm_entry` subtracts
it and writes the three heights in before publishing. Exact on both kinds of
machine, and it retires the "pick which machine to be wrong on" paragraph
entirely.

**A second one the same gate caught**: `tm_onresize` was handing `tm_layout`
the display's **band** where its argument is a **frame**, so `tm_colrows`
became what fits the SCREEN (32 rows on a VGA) rather than what fits the
window (19). The kernel applies a published frame *before* it notifies
(§39.16.3.3), so the content box the handler is handed already describes the
window it now has — which is what it uses again.

`dispapps` had been red since §15 and is green.

### 18.1 …and `dispsize` leg B had to change what it tests

Leg B asserted that a fixed-size window dropped clear across comes back the
SAME size (§39.16.3.2). Its subject was Solitaire — which now **publishes** a
size per adapter, so it is supposed to change, and the leg was asserting the
opposite of the feature. The property still needs testing and it needs a window
in §12.5's **third row**: not sizable, nothing published. Minesweeper is one.

**A gate whose subject acquires the behaviour under test stops testing it, and
says so by passing.** This one failed instead, which is luck rather than
design — the size it measured moved. Worth remembering when the next package
publishes a preference.
