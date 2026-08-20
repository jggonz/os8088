# Window animations — the drag outline, borrowed for five more events

**Status: BUILT, `kern_big` only.** SPEC.md §11.99 is the contract; this file
is the investigation that produced it and the record of what it cost.

**Measured, on the shipped build:** `.text` **+451**, `.bss` **+27**,
`.cold` +1 — one 512-byte image rung, `KERN_BUDGET` spare **2,048 → 1,536**
(4 steps → 3). **`kern_small` is byte-for-byte unchanged** — `text` 48,474 and
1,024 spare, exactly its baseline — because the whole feature is inside one
`WM_ANIM` symbol and the small build never defines it.

**Verified the way this tree verifies an overlay.** `make ANIMOFF=1` builds the
same kernel with the feature compiled out (`text` 52,358 — the tree's own
pre-change figure to the byte), and the identical scripted session through both
is **0 differing pixels** at every settled point: five states on CGA (desktop,
open, minimized, restored from the dock, closed), and open on Hercules and on
VGA mode 12h (0 of 921,600). That is the entire safety argument — the outline
is XOR and must restore the screen exactly — and a screenshot of one build
cannot check it.

**Timing, measured in guest cycles between consecutive `wm_an_pace` hits on a
cycle-accurate 4.77MHz 8088** — five outline frames of 32.4, 34.5, 35.8 and
38.2 ms, **~176 ms end to end**. It was a whole tick a frame and ~330 ms, which
read as slow; the frame is half a tick off the PIT now (§11.99.4) and the step
COUNT is unchanged, because the count was never what was wrong.

**A close does not animate.** It was built and then removed on looking at it:
closing is the one window operation with nothing at the far end, so the zoom
was a third of a second spent saying something had stopped existing. The window
vanishing is the feedback. Verified — `wm_an_pace` never fires on a close.

---

## 1. The mechanism is already in the tree, twice

`ui_drag` (SPEC.md §13) and `ui_grow` (§11.1) both track with a 1px XOR
outline through **`vga_xor_rect_vram`**, and every property an animation needs
is already contracted:

- **Self-inverting.** Drawing it twice restores the screen exactly, so there is
  no save-under, no back buffer (there is none — §32), and nothing to claim
  off a heap that can refuse.
- **It clips to the screen**, so an outline reaching off the edge is free
  rather than illegal.
- **It bypasses the clip region** (§11.3) deliberately, and **owes its own
  `cur_unlazy`** (§7.1.4) — the two things a transient overlay has to get
  right, both already got right.
- **It is one entry point for all three adapters.** `gfx_xor_rect_d` puts
  `GFXDENTER` above the `[vid_mono]` dispatch (§39.14.6), so mono goes to
  `sw_xor_rect` and VGA to the planar `vga_vline_core` out of one call.

So the whole of a window animation is: interpolate a rect, XOR it on, hold,
XOR it off. **No kernel primitive changes and no package is invalidated.**

---

## 2. The five events, and what each one's two rects are

| event | source rect | destination rect | both ends known where? |
|---|---|---|---|
| **zoom out** (maximize) | the record's rect | the standard rect | **yes** — `wm_zoom`'s `.go` holds both in registers |
| **zoom back** (restore) | the record's rect | the banked `ZR_*` rect | **yes** — same routine, `.rgo` |
| **minimize** | the window frame | its dock tile | **yes** — `dock_tile_x` + `[vid_dock_ty0]` |
| **restore from dock** | its dock tile | the window frame | **yes** — the reverse |
| **open / close** | *nothing* | the window frame | **no** — see below |

**`wm_zoom` is the best candidate by a distance** and would be the one to build
first if only one is built. Both rects are already computed there for reasons
that have nothing to do with animation, the user has explicitly asked for a
geometric change, and the operation is rare enough that a third of a second is
not in anybody's way.

**Minimize/restore is the second**, and the honest one: a window flying to the
tile it is about to become is the whole reason the dock's tile↔window mapping
is stable (§30). It needs one new four-line helper (`dock_tile_rect`, lifted
out of `dock_erase_tile` exactly as `dock_hit` was lifted out of `dock_click`).

**Open/close is the weak one, and §5 is why.**

---

## 3. What it costs to draw — MEASURED

An outline is four strips, and **the two verticals are one framebuffer
read-modify-write per scan line**, so the cost is a function of the outline's
HEIGHT and barely of its width at all. Nothing published priced that shape:
every `gfx_fill` row in PERFORMANCE.md Part 2 is at least 8 pixels wide, and a
1-pixel column is the case none of them is. Read off those rows the answer
brackets between **46 and 182 µs per scan line** and settles nothing.

So `tests/gfxbench` gained three rows — `GFX_XOR_RECT 64x64`, `256x128` and
`256x1` — and **the subtraction of the last two is 252 vertical scan lines and
nothing else**. `tests/gfxbench` on `os8088_5150_cga_gla`, a cycle-accurate
4.77MHz 8088 with a real CGA:

| row | N | µs/call |
|---|---:|---:|
| `GFX_PIXEL` | 300 | **629.13** |
| `GFX_FILL 256x1` | 100 | 861.29 |
| `GFX_FILL 256x128` | 6 | 23,989.77 |
| `GFX_XOR_FILL 64x64` | 24 | 9,318.17 |
| **`GFX_XOR_RECT 64x64`** | 24 | **5,261.59** |
| **`GFX_XOR_RECT 256x128`** | 6 | **8,765.40** |
| **`GFX_XOR_RECT 256x1`** | 24 | **1,196.03** |

**The harness agrees with the iron before it is asked anything.** `GFX_PIXEL`
629.13 against Part 2's **756**, which is 83% — and Part 2's figures predate
§5.7, whose seven changes were predicted at −17 to −20%. `ISA status port in`
reads **8.74 µs** against Part 2's **8.7**. Neither was arranged.

`256x128` minus `256x1` is one extra 256px horizontal plus 252 vertical scan
lines, and `256x1` minus `GFX_PIXEL`'s floor is that horizontal on its own:

> **a vertical scan line costs 27.8 µs**, and an outline of height *h* costs
> **≈ 1,760 + 56·(h−2) µs**

which reproduces the 64x64 row to 0.6% and the 256x128 row to 0.6%.

**That is 1.7x below even the LOW end of the bracket and 6.5x below the high
end, and it changes the design.** A whole animation's drawing:

| | intermediate heights | 3 frames, drawn AND erased |
|---|---|---:|
| CGA, a 300x120 window zoomed to the desktop band | 124 / 128 / 132 | **52.9 ms** |
| Hercules, a 400x280 window minimized to its tile | 215 / 150 / 85 | **60.3 ms** |

---

## 4. The shape that falls out

**One to two ticks of drawing for the whole thing, so the TICK is the frame
clock and not the drawing.** That is the opposite of what the published fill
rows implied, and it is the good outcome: six tick-paced frames is
6 × 55 ms = **330 ms**, which is the third of a second asked for, and the
drawing is under a fifth of it.

```
WM_ANIM_N   equ 6          ; steps; N-1 outlines are actually drawn - step 0
                           ; and step N are the window itself, before and
                           ; after, and drawing those is a double-draw flash
```

Three consequences, all of which fall out of the measurement rather than
being chosen:

- **The pace is real work on every machine, not a cap for fast ones.** ~280 ms
  of a 330 ms animation is waiting, so 5.2's "it must yield" stops being
  hygiene and becomes the main cost of the feature.
- **The corner-bracket variant is not needed.** It was drafted as the escape
  hatch if a scan line turned out to cost 182 µs; at 27.8 it buys nothing
  anybody could see and it is not a window outline.
- **No adapter needs a different frame count.** VGA's rows are ~3.4x cheaper
  (5150 #2's `GFX_FILL 64x64`, 53 µs a row against mono's ~182) and its windows
  are taller, which roughly cancels; Hercules is the slow case at 60 ms and
  that is still one tick.

---

## 5. What breaks, in the order it will break

**5.1 The pointer disappears for the whole animation.** `vga_xor_rect_vram`
calls `cur_unlazy` and the lock is held end to end, so the arrow is off the
screen for the third of a second — on exactly the three events where the user's
hand is on the mouse. §7.1.4's deferred hide is a *promise* the lock makes, and
this spends it on the first outline. It is *probably* safe to leave the cursor
up (the outline XORs the composited pixels, the mouse ISR cannot draw under the
held lock, and the outline is off again before the lock drops) — but that is an
argument, not a test, and §7.1.2's smearing is what it would cost if wrong.

**5.2 The pace must yield.** A spin on `[ticks]` starves every background task
for 1/3 s. `gfx_unlock`/`task_yield`/`gfx_lock` is the idiom (§7.1.3) — except
that dropping the lock is exactly what `ui_drag` may not do with the outline
lit, so the pace has to `task_yield` *holding* the lock, as `ui_drag`'s
`.linger` does.

**5.3 The un-minimize is FOUR call sites, and each clears the flag itself.**
`files.inc:1572`, `instance.inc:1370`, `ui.inc:1957` and `inst_restore` all do
`and byte [di+I_FLAGS], 0xFE` and then `wm_show`. So an animation hooked at
`wm_show` **cannot tell "restore from the dock" from "open fresh"** — the one
fact that decides which rect it flies from has already been thrown away. The
fix is to move the test ahead of the clear in one place, not to add a
suppression byte per caller (the spike used a byte, and it is wrong: it covers
one of the four). **This is the failure that would ship** — a restore animating
from the middle of the screen instead of from its tile is visually wrong and
crashes nothing.

**5.4 Nine `wm_show` callers and eight `wm_destroy` callers are not nine and
eight events.** Hooking the two routines catches the **modal file dialog**
(`fdlg.inc:277`), the **notice window** (`ui.inc:2216`, a reused window that
comes back repeatedly), every **package secondary window** through
`cw_wm_show` (ModPlug's Setup and PlayList), and `wm_destroy_seg`'s teardown
loop — which would animate *each* of a package's windows in turn, a third of a
second each, while it is quitting. An opt-out is needed and W_FLAGS has a free
bit for it — but **not bit 8**, which this plan first reached for: bits 8–14 of
`W_FLAGS` are §7.2.1's cursor *shape* field and bit 8 is `CUR_CROSSSH`. The bit
is **14**, the top of that field, for the reason `WF_STALE` is 15 (SPEC.md
§11.99.2), and it costs nothing in the record.

**5.5 There is no honest source rect for an opening window.** A Mac zooms out
of the icon that was double-clicked; here a launch arrives from a menu item, a
desktop drive zone, a Disk window row or a package's own `wm_create`, and
`wm_show` is handed none of that. The spike used the window's own centre, which
is defensible and is what most systems without an icon do. It is also the event
that happens most often and the one where 1/3 s of waiting is least wanted.

**5.6 Inherited, not new: an outline crossing the display seam.** `gfx_xor_rect_d`
does one `GFXDENTER`, so an outline spanning two displays is drawn on one of
them. That is the drag outline's existing behaviour (§39.16) and the animation
inherits it — worth knowing, not worth fixing here.

---

## 6. The size, measured

A spike carrying the shared engine and **all five** call sites, assembled
against `626e3d6`:

| | `.text` | `.bss` | total |
|---|---:|---:|---:|
| the engine (`wm_anim`, lerp, xor, pace, the two rect loaders) | +177 | +26 | **+203** |
| `wm_zoom`, both directions | +98 | — | +98 |
| minimize / restore-from-dock (incl. `dock_tile_rect`, `inst_idx_of`) | +103 | — | +103 |
| open / close (incl. the centre seed and the visible test) | +134 | +1 | +135 |
| **total** | **+512** | **+27** | **+539** |

| | before | after |
|---|---|---|
| `kern_big` image rung | 58,880 (270 left) | 59,392 — **one rung crossed** |
| `kern_big` `KERN_BUDGET` | 2,048 spare, 4 steps | **1,536 spare, 3 steps** |
| `kern_small` image rung | 54,784 (487 left) | 55,296 — **one rung crossed** |
| `kern_small` `KERN_BUDGET` | 1,024 spare, 2 steps | **512 spare, 1 step** |
| `KERN_CODE_MAX` (big) | 6,926 left | 6,410 left — no issue |

`docs/window-anim-spike.patch` is that spike, kept beside this file so the
numbers can be re-taken rather than re-derived: `git apply` it, `make`, run
`tools/kernsize.py`. It is a MEASURING INSTRUMENT and not a candidate for
merge - it has a `jmp short` that had to become a `jmp`, a suppression byte
that 5.3 says is the wrong shape, and no opt-out at all.

**Read `kern_small` first.** It lands at ONE step, and docs/KERNEL-MEMORY.md
records that figure being discovered broken twice already because `all` never
builds it. A feature that takes it there should be either asked for as a budget
move or built `%ifdef KERN_BIG`, which is what §62.9.15 did to the RAM disk for
the same reason.

**And it is not one feature.** Built as `wm_zoom` alone it is **+301 bytes**
(engine + one call site), which fits `kern_big`'s existing 270-byte slack to
within 31 bytes and crosses one rung; built as zoom + dock it is +404. The
open/close pair is a third of the cost and, per §5.4 and §5.5, all of the risk.

---

## 7. What was built, and what the two tooling bugs cost

Sections 1-6 are the investigation as it stood before the code; §5's list is
what the implementation had to answer, and it did:

- **5.3** — `inst_unmin` does the flag clear AND the arm, at all four sites
  (SPEC.md §11.99.3). A fifth path cannot forget the second half.
- **5.4** — `WF_NOANIM` on the Standard File dialog and the notice window, and
  a close animates only for the FRONT window (§11.99.2).
- **5.5** — an opening window zooms out of its own centre (§11.99.1), which is
  the first pass and is the thing to look at and revise.
- **5.1/5.2** — the pointer is off for the hold, and `wm_an_pace` `task_yield`s
  rather than spinning. Both stand as written.

`WM_ANIM_N` is **6**, not §4's 4: the measurement said the tick is the clock.

### RETRACTED: the "two instrument bugs" were both mine

**An earlier revision of this file, and commit 2e6c865's message, asserted that
`m.sym` returns `.text` symbols 0x600 too high and that exec breakpoints do not
fire in this MartyPC build. Both claims are FALSE and both were operator
error.** They are retracted here rather than deleted, because a wrong claim
that a shipped instrument is broken is worse than the bug it was invented to
explain: the next agent stops trusting the debugger, or goes and "fixes" it.

**`m.sym` is correct and returns a FLAT address** — `KERNEL_SEG*16 + offset`,
which is what `read()` and an `exec` breakpoint's `addr` take. Its own
docstring shows `m.read(m.sym("fpg_on"), 1)` and `read` is flat. The 0x600 is
`KERNEL_SEG << 4` and nothing else; measured, `sym("wm_an_pace")` is `0x8133`
and `0x8133 - 0x600 = 0x7B33`, which is exactly where an opcode search finds
the routine. Every breakpoint user in the tree already pairs it with the flat
form — `tools/os88span.py` is literally
`{"type": "exec", "addr": m.sym(n)}` — and none uses `execseg` with it, which
is why nobody had met this before. I put a flat address into an `execseg`'s
`off`, which armed a real address 0x600 further on that is never reached.

**Exec breakpoints fire.** `execseg` and flat `exec` both, verified against the
live `int 08h` vector and against `wm_an_pace`. **The stop state is
`"breakpoint"`, not `"paused"`** — and every script in the failed
investigations polled `== "paused"`, which is true only of an explicit
`pause()` and so is false forever at a breakpoint that is working. That fact
was already documented (docs/MARTYPC-DEBUG.md's `until` section) and already
had a reference implementation in the tree (`tests/dispfreeze.py:55`,
`m.status()["state"] == "breakpoint"`); it simply was not written in the
BREAKPOINT section, where somebody arming one is standing. It is now.

**What the episode actually produced**, and the only part worth keeping:

- `Marty.stopped()`, `Marty.wait_stop()` and `Marty.bp_exec()` in
  `tools/os88marty.py`, so neither trap is reachable from the client — the
  first two test `!= "running"`, and the third takes symbol names or flat
  addresses and emits the flat form.
- The trap pair written into docs/MARTYPC-DEBUG.md's breakpoint list.
- Proof they work: `m.bp_exec("wm_an_pace")` catches **exactly five** hits at
  `0060:7B33` through one open — `WM_ANIM_N - 1`, independently confirming the
  frame count — and the five captures' difference boxes expand monotonically
  about (263, 97), which is the Disk window's own centre. §11.99.1, measured
  from outside the guest.

**The lesson is not about MartyPC.** Two facts were documented, one had a
working example in the tree, and an afternoon still went into concluding the
tool was broken. When an instrument that other people use appears to fail,
the first hypothesis is the operator — and the cheapest test is to find who
else uses it and read how (`grep -rln "breakpoints(" tests/ tools/` answers in
a second, and would have ended this at the start).

### What is left

- **The open animation's source rect is the honest weak point** (§11.99.1), and
  it is first-pass on purpose. Look at it before deciding.
- **`GFX_XOR_RECT`'s three new `gfxbench` rows are CGA-only so far.** Hercules
  and VGA would each be one run.
- **Nothing has been on the iron.** Every figure here is MartyPC's, which
  agrees with Part 2 to 17% on `GFX_PIXEL` in the direction §5.7 predicts — but
  a third of a second is a claim about how something FEELS, and that is the
  5150's question and nobody else's.
