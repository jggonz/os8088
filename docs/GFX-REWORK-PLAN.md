# The graphics layer rework — what is known, and what to decide

**Status: DESIGN NOT STARTED.** This is a handoff, not a plan that has landed.
Everything here was learned while trying to make one title bar draw faster
(SPEC.md §11.101, PERFORMANCE.md Sets 86–88), and the conclusion of that
attempt is that the title bar was the wrong subject: **the cost is in the
graphics layer's call structure, not in any one drawing routine.**

Read PERFORMANCE.md Set 88 before anything else in this file. It is the record
of a redesign that was obviously correct and measured **four times slower**
than the fifteen primitive calls it replaced. It is the trap this whole
document exists to keep the next person out of.

---

## 1. The finding

A drawing call on this machine spends most of what it costs on machinery that
has nothing to do with the pixels. Taken apart, one `gfx_pixel` is **196 guest
instructions across eleven routines**, a third of it push/pop pairs and near
call/rets: the API cell, rect marshalling, §11.3's clip test, the buffer
dispatch, `vga_rect_setup`, `gfx_rowbase`, `sw_ink`, `sw_plane_op`, `sw_col`.

> **That list is one release out of date and is kept for its SHAPE, not its
> members.** `bb_mono_chk`, the `[bb_on]` dispatch and `bb_dirty_rect` went
> with SPEC.md §32's back buffer and no longer exist; the count of eleven has
> not been retaken since. Re-derive it before quoting it — the shape survives
> the deletion, the number may not.

**Almost all of it is recomputed on every call and almost none of it changes
within a `gfx_lock` hold.**

- the clip region changes only at `wm_clip_set` / `gfx_unlock`
- the display changes only at `gfx_disp_enter`
- the blit pen changes only at `gfx_blit1_pen`
- the deferred cursor hide is explicitly one-way within a hold

A window title bar makes **15 primitive calls** and costs a measured **40.8 ms**
on Hercules.

> **THERE IS NO PER-CALL FLOOR HERE TO MULTIPLY BY FIFTEEN, and this paragraph
> used to try.** It quoted SPEC.md §5.7's ~756 µs as the fixed part of any
> drawing call, converted it to ~3,608 cycles and multiplied: *"15 × 3,608 =
> 11.3 ms of pure floor"*. Both halves are wrong. §5.7's 756 µs was measured on
> the SMALLEST calls there are — an 8-pixel hline, a pixel — so it is what a
> tiny call costs, not a floor under a large one; and §2's own table falsifies
> it in the other direction, because a 6-pixel `gfx_hline` is 3,235 cycles
> (678 µs, *under* the "floor"), an 11×11 `gfx_frame` is 16,402 and a four-cell
> `font_str_x` is 18,140. The fifteen calls in a title bar span **678 µs to
> 3.8 ms each**. A design that beat an invented 11.3 ms floor would still be
> four times slower than the thing it replaced, which is PERFORMANCE.md Set
> 88's whole story and CLAUDE.md's standing warning about this number.

Menus, dialogs, the file manager and the Control Panel all pay the same shape.

### 1.1 What that means for the composer that started this

`band.inc` (SPEC.md §5.9) composes a whole picture in RAM and blits it once.
**On both 1bpp adapters it is now the faster path**, and it has no double-draw
in it at all:

| adapter | the fifteen primitive calls | composed |
|---|---:|---:|
| **Hercules** | 40.8 ms | **36.5 ms** |
| **CGA** | 42.0 ms | **37.1 ms** |
| VGA | **30.3 ms** | 38.3 ms |

It opened at **71.1 ms against 41.9** and this document once said, with
numbers, that it was a dead end. It was four routines, and **not one of the
four changed what a band is**:

| | Hercules |
|---|---:|
| composed, `BAND_MAXW` = 128 → 3 chunks a bar | 71.1 ms |
| …640 → **1** chunk (SPEC.md §5.9.2) | 56.0 ms |
| …ragged ends drawn correctly (§11.101.1) | 61.2 ms |
| …`bnd_cell` as one hoisted loop (§5.9.4) | 47.7 ms |
| …`band_fill_x` the same (§5.9.5) | **36.5 ms** |

Every composed operation now beats its primitive:

| one title bar | primitive | composed |
|---|---:|---:|
| a 6-px collapse mark | `gfx_hline` 3,235 cyc | `band_hline_x` **1,714** |
| a 312-px pinstripe | `gfx_hline` 4,112 cyc | `band_hline_x` **2,110** |
| an 11×11 box frame | `gfx_frame` 16,402 cyc | `band_frame_x` **8,647** |
| the 4-cell caption | `font_str_x` 18,140 cyc | `band_run_x` **12,501** |
| clearing the band | — | `band_open_x` 7,157 |
| the blit | — | `band_emit_x` 18,776 |

**What is left for THIS rework is the last two rows** — 25,933 cycles (5.4 ms)
a bar with no primitive beside them, and the reason VGA is still behind. The
blit is `gfx_blit1_x`, whose own prologue is **78 bytes of blitting inside
603**: the clip region (~96 B), the display-extent clip (143 B), the
multi-display resolve (~62 B), the pen resolve (~100 B) and the register/frame
banking, five far calls, all in the prologue. That is §3's Phase 1, pointed at
the one call the composer cannot avoid making.

**Read §6 before estimating anything here.** This section has been wrong twice
in the same way — once by pricing an instruction sequence instead of measuring
it, once by dividing two aggregates and calling the answer a property of the
design.

---

## 2. The measured facts, with provenance

Everything below was taken on **MartyPC**, cycle-accurate 4.77 MHz 8088,
`os8088_5150_herc_gla` unless stated. Nothing here is estimated.

| | |
|---|---|
| one window raise, title bars only | Hercules **40.8 ms**, CGA **42.0**, VGA **30.3** |
| the same, composed — `make BAND=1` (SPEC.md §5.9.6; it was the default for a cycle) | Hercules **36.5 ms**, CGA **37.1**, VGA **38.3** |
| `gfx_hline`, 6 pixels | **3,235 cycles** |
| `gfx_hline`, 312 pixels | **4,112 cycles** — 877 more for 306 more pixels |
| `gfx_fill`, over one whole raise | 3,106 → 41,323 cycles |
| `gfx_frame`, an 11x11 box | 16,402 cycles |
| `font_str_x`, a 4-cell caption | 18,140 cycles |
| `band_hline_x`, the same two lines | **1,714** and **2,110** cycles |
| `band_frame_x`, the same 11x11 box | **8,647** cycles |
| `band_run_x`, the same 4-cell caption | **12,501** cycles (was 42,041 before SPEC.md §5.9.4) |
| `band_open_x` + `band_emit_x` — the composer's FIXED cost a bar | **25,933** cycles, and the blit swings 5,000 of that on whether the cursor hide is owed |
| a transparent glyph cell (`font_str`) | **4,024 cycles**, 0.84 ms |
| an opaque run cell (`font_run`), 25-cell string | **1,243 cycles** |
| an opaque run cell, short aligned rows (`fm_draw_lrows`) | **~537 cycles** |
| `band_open_x`, 256 B band | 3,288 cycles; 112 B band 1,804 → **10.3 cyc/byte, ~651 fixed** |
| `band_emit_x`, 256 B band | 11,550 cycles; 112 B band 8,517–9,652 → **13–21 cyc/byte, 6,100–8,200 fixed** |
| a far call through a `cw_` shim | ~290 cycles against a near call+ret's ~52 |

**Hercules is the worse case for chrome**, which is the opposite of §6.1.10's
text-only finding — a title bar is mostly *fills*, and VGA fills faster than it
letters. Optimise the 1bpp path.

**And this is the whole argument in one row: `gfx_hline` spends 3,235 cycles to
draw six pixels and 4,112 to draw 312.** The width barely matters. Whatever the
rework is, that base is what it is for. PERFORMANCE.md Part 1's "756 µs fixed"
is a fair average of the whole `gfx_*` family and **must not be quoted as a
floor a design has to beat** — measure the call you actually mean.

### 2.1 What is NOT known, and matters

- **Nothing about the rework has been measured at all.** Every figure in §3 is
  a projection from the table above.
- `band_emit_x`'s fixed and per-row costs are still entangled, and the two
  112-byte samples differed by 13%. **This no longer matters** — Set 89 closed
  the composer question without needing it — but it is the shape of the gap to
  avoid leaving in the rework's own numbers: two points at two sizes, always.

---

## 3. The three phases, in this order

The ordering is the most important thing in this document.

> **Phase 2 is done** (SPEC.md §2.6.1, PERFORMANCE.md Set 90) and it turned out
> to be a 340-byte size change rather than a speed one — the shim it removes is
> 67 cycles, not the "far call plus a near call plus two returns" this document
> priced it at. **The far call itself is still there and Phase 3 is what
> removes it.** Read Phase 2 below before trusting any other estimate here:
> every number in this section is a projection, and the one that got measured
> came back an order of magnitude smaller.

> ## Phase 1 is DONE, and its premise was mostly wrong
>
> This section says the layer "resolves the clip region, display and pen per
> call when none of them changes within a `gfx_lock` hold". Measured, on a
> 2,650-cycle minimal `gfx_fill` of which **367 write pixels**:
>
> | the supposed invariant | cycles | what happened |
> |---|---:|---|
> | the clip + the display (`GFXCLIP`/`GFXDISP`) | 199 | already two compares. **Nothing to hoist** |
> | the pen (`sw_ink`) | 158 | genuinely varies; a value cache measured no better |
> | the row base (`gfx_rowbase`) | 319 | **the one true invariant. Tabulated: −39%** (§39.3.1) |
> | the deferred hide (`cur_unlazy`) | ~120 | **a call to be told "already spent". Inlined** (§39.3.2) |
>
> `gfx_fill`'s floor **2,650 → 2,405**, `gfx_hline`'s **2,771 → 2,534**, the
> fifteen-call title bar 40.8 → **40.0 ms**. About **9%**, for 114 bytes of
> `.text` and 256 of `.lowbss`, no budget raised.
>
> **The other 85% is not re-resolution — it is per-RECT work**, and no amount
> of lock-time hoisting reaches it: `vga_rect_setup` spends 654 clipping *this*
> rect and computing *its* two masks and span, and `sw_rect` spends ~216 on
> eight pushes and eight pops that its callers' "preserves everything" contract
> requires. Those are real answers to real questions asked once per call.
>
> **So the lever is fewer calls, not cheaper ones** — which is Phase 3, and
> which the composer already demonstrates from the other side: `band_hline_x`
> composes a 312-pixel stripe in 2,286 cycles and `gfx_hline` still needs 3,203
> to draw one.
>
> **And the binding limit is no longer `KERN_BUDGET`.** `.text + .bss` is
> **63,796 of `KERN_CODE_MAX`'s 65,536 — 1,740 left**, and that ceiling is what
> a 16-bit offset reaches. More budget does not buy `.text`. Only `.cold` does,
> which is §4's argument arriving from a direction nobody planned.

### Phase 1 — hoist the resolve to LOCK time (do this first, alone)

Resolve the clip region, the display and the pen **once**, when they are set,
into a record every primitive reads. Estimated: the per-call floor **3,608 →
~1,200**, and `gfx_blit1_x`'s fixed cost **6,100–8,200 → ~2,000**.

Why first:

- **It is ABI-neutral.** No package recompiles. Nothing breaks.
- **It is worth more than the other two phases combined.** A title bar's floor
  goes 11.3 ms → 3.8 ms with no change to what it draws, and every fill, frame
  and hline in the system gets it.
- **It produces the cost model the other phases need.** There is no honest way
  to design Phase 3 without knowing what a call, a byte and a lock cost.

The design already half-admits this is possible: `gfx_b1act`/`gfx_b1g0`/
`gfx_b1g1` carry the comment *"gfx_blit1's PEN, resolved once a band"* — but
they are filled inside `gfx_blit1_x`, not in `gfx_blit1_pen` where they would
be resolved once per pen CHANGE. That single move is ~100 bytes and ~200 cycles
off every VGA blit, and it is the pattern for the whole phase.

**The correctness core of Phase 1 is the lifetime rule**: what invalidates the
record. `gfx_unlock` clears the clip; `wm_clip_set` re-arms it, possibly per
fragment mid-hold; `gfx_disp_enter` nests via `[gfx_dnest]`. Settle this in
design. A stale resolved record draws in the wrong place, silently.

### Phase 2 — the far-call cost — **DONE, and it was a SIZE change**

*Landed as SPEC.md §2.6.1; measured as PERFORMANCE.md Set 90.* This section is
kept as written plus its correction, because the correction is the lesson.

It said: every cross-segment call goes through `cw_X: call X / retf`, so it
pays a far call **plus** a near call **plus** two returns, and a direct far
entry — the body itself ending in `retf` — roughly halves it, globally, for no
memory.

**Measured, the middle pair is 67 cycles** (`cw_gfx_rowbase` 386 entry-to-far-
return against `gfx_rowbase`'s body at 319). Deleting it leaves the `retf` the
caller still owes, so the saving is **33 cycles a crossing** — and the far call
itself, which is the majority, is untouched and needs Phase 3. `gfx_blit1_x`
makes five crossings a blit: 165 cycles back out of 18,756.

What it *did* buy is **340 bytes** — 84 thunks at four bytes each — off the two
tightest rungs in the build: `.text` 88 → 162 bytes of slack, `.cold`
173 → 419. Worth having, and not what the sentence above promised.

**"A far call plus a near call plus two returns" is a description of an
instruction sequence, not a cost.** It went into this plan as though it were
one. One breakpoint pair and a subtraction would have priced it in ten
minutes.

### Phase 3 — the segments, and the primitives

**Only after 1 and 2 have produced numbers.** See §4 and §5.

---

## 4. The segment proposal — OVERLAPPING, not partitioned (PINNED)

> **UN-COSTED again, and pinned.** A costing lived here from 2026-08-24 to
> 2026-08-27 and was withdrawn with the set behind it (PERFORMANCE.md Set 95):
> it counted crossings over two gestures at an idle desktop and concluded from
> them that this proposal would buy nothing. Two gestures cannot carry that
> conclusion, and it was read as settling the question for three days.
>
> **So nobody knows what §4 is worth, and the speed case is not the only
> case.** What IS measured is the SIZE of the machinery, in PERFORMANCE.md
> Set 109: **2,245 resident bytes** of shims, thunks and far-call encoding
> across **391 crossing sites** — nine times what the one cheaper measure
> still on the table there would recover. Overlapping segments would retire most of
> it, in `.text` and `.cold` alike, and every one of those bytes counts at
> face value (CLAUDE.md's rung rule). Anyone re-taking the speed side needs a
> harness that samples across applications and workloads and says what it did
> not cover.
>
> The one durable finding of the withdrawn run is SPEC.md §38.1.1: a guard
> written on the wrong side of the boundary, costing ~1% of the machine at
> idle. It is measured independently there.


`.text` and `.cold` today are a **budget** split, not a cohesion one, and the
64 KB ceiling that forces a split is real: `.text + .bss` is **63,398 of
65,536** — 2,138 bytes left. Offsets are 16 bits; this is the machine, not a
policy.

The proposal is to stop splitting by temperature and split by **who calls
whom**: files and disk on one side, graphics/WM/input on the other, each near
within itself.

A straight partition does not work. By source lines the clusters are roughly
**graphics/WM/input ~23k, files/disk/drivers ~19k, and ~13k SHARED** (memory,
scheduler, instance, clock, events). Put the shared third in one segment and
the other side far-calls it constantly — the same problem, moved.

**The answer is that 8086 segments may OVERLAP.** With `SEG_A` at 0x0060 and
`SEG_B` at 0x0860, the physical span 0x8600–0x10600 is addressable from *both*
— so shared code placed in the overlap is **near-callable from either side**,
and each cluster sees "one segment" from its own point of view.

```
   physical   0x0600 ......... 0x8600 ......... 0x10600 ......... 0x18600
   SEG_A      |<-- A only -->|<--- SHARED --->|
   SEG_B                     |<--- SHARED --->|<-- B only -->|
```

**What it costs, and what design must settle:**

- A routine in the overlap has **two different offsets** depending on which
  segment addresses it. Anything holding an address — the `OSAPI_SLOT` table
  above all (SPEC.md §20.1: a slot is a pinned address) — must pick one, and
  every caller in the other segment reaches it the long way.
- `ret` versus `retf` discipline gets subtle at the boundary. SPEC.md §5.4.2.1
  is what one such mistake already cost, and `tools/os88ovlchk.py` exists
  because of it. That gate must be taught the new layout.
- The overlap's size is the design's main dial: too small and the shared code
  does not fit, too large and neither cluster has room.
- Task stacks, `.lowbss` and the FAT window all sit in the same physical ladder
  (docs/KERNEL-MEMORY.md). A new segment layout moves them.

**Phase 1 may dissolve most of the case for this.** With the resolve hoisted
and the shims made direct, crossings drop from five to ~three per blit and each
one roughly halves — perhaps 300 cycles total. The design phase should be
allowed to conclude that the churn is not worth it.

---

## 5. Phase 3's other half — the primitives

The case for reconsidering them from the ground up is symptomatic, not
aesthetic:

- §5.7's floor is 196 instructions across eleven routines to set one pixel.
- `font_run_x` is **1,009 bytes** across five paths: mono aligned, mono
  unaligned (§6.1.11), planar (§6.1.10), `.slow` (a `gfx_fill` + `font_str`
  fallback), and a per-cell escape for clipped runs.
- `gfx_blit1_x` has two emit loops, and the complemented one **sheared any band
  an odd number of bytes wide** — latent since the pen landed, found this
  session by the first caller that chunked (SPEC.md §5.4.2.3).
- `ico_core` is a masked 1bpp band composer that nobody knew was one until
  §25.6 went looking for a masked sprite pass.
- `font_char`/`font_str` need a six-case rule (§6.6.2), a registry file and a
  build-gate test to police. 37 sites remain and each one is an argument.

### 5.1 The boundaries a band cannot cross

Established while building `band.inc`, and they bound any composer:

1. **Anything that reads the screen** — §38.3's XOR selection band, drag
   outlines, `gfx_scroll`, the cursor's save/restore. Composition cannot read.
2. **Large-area fills** — a window's content erase is cheaper direct.
3. **More than two colours in one picture.** A band is 1bpp; the colours arrive
   at blit time from the pen. A 4bpp band at 640 px is 320 bytes a row, so 2 KB
   is six rows. The Task Manager's memory map, Paint's canvas, Fractal and
   colour icons stay direct. **A band is chrome.**

Measured against the prologue, **those three account for ~100 bytes of the
~520-byte prologue and near-zero cycles on 1bpp** — the pen block is skipped
outright when `[vid_mono]` is set. They are not what the prologue is for.

Two more were found by making the composed bar pixel-exact (Set 89), and both
are about the *edges* of a composed picture rather than its middle:

4. **1bpp ignores the pen entirely.** A set band bit is a lit pixel, so a band
   composed ink-as-set-bits is upside down whenever the ink is dark — which on
   Bright is every title bar there is. It shipped that way and Set 88 measured
   it without seeing it, because the first check was on VGA where the bug
   cannot appear. §5.9.3 is the fix (a storage polarity, free) and the standing
   lesson: **check a 1bpp drawing change on a 1bpp adapter.**
5. **The last byte at each end is not the composer's.** `gfx_blit1` takes a
   multiple of 8, the picture does not end on one, and what lives in those
   up-to-7 pixels is *not* just ground — the pinstripes reach into them.
   Putting them back cost six primitive calls at each end, 5.2 ms of the 61.2
   (§11.101.1). Any blit-based design pays this at every unaligned edge.

### 5.2 The trap, stated plainly

**A design that removes calls can still lose, because the work it replaces them
with is not free.** Set 88 is the worked example: fifteen `gfx_*` calls is real
floor and beatable, and the first composer spent more replacing them than it
saved. Four rounds of optimisation took it from 167 ms to 71.

**And the second trap is the first one's cure applied lazily.** Set 89 nearly
closed the composer as "unfixably slower" on a ratio of two *totals* — 61.2
against 40.8. Measuring the six operations separately reversed it: five are
fine, composing a line already beats drawing one, and the entire gap is one
routine making sixteen near calls to place a character. **A ratio of totals is
not a measurement of a design.** Price ONE replacement operation against ONE
primitive, always.

**Every design conclusion in this rework needs a number attached, and the
number has to be of the thing you are deciding about.** A design agent that
cannot measure will reproduce Set 88; one that measures only aggregates will
reproduce Set 89's first draft, which is worse — it has numbers, and they are
answering a different question.

---

## 6. What was asserted this session and turned out to be wrong

Kept because the next reader will be tempted by the same three.

1. **"The composer should be `font_run`'s emit with a destination pointer."**
   No. `font_run_x` carries the display split (§39.14.6), `GFXDENTERCD`'s
   nesting, the planar prologue, the unaligned phase and the clip machinery.
   Threading a RAM destination through all of it risks the hottest primitive in
   the system to save the small half of the work. A band is always 1bpp, so it
   needs only the mono composition. Share `font_glyphs`, nothing else.

2. **"Land the cheap pattern-fill win first, then the composer."** The staged
   version was the *more* expensive path: `.text` had 90 bytes left, the
   stripe-selection code was ~86, and it failed the build outright. Check the
   rung before promising a staging order.

3. **"The band is byte-perfect."** It was not, on either 1bpp adapter, and
   the claim was made from a VGA check. Two separate defects (§5.9.3, and
   §11.101.1's lost stripes) survived a full round of *performance* work
   because nobody had compared the two paths pixel by pixel. **A "the output is
   identical" claim needs the diff that proves it, on every adapter.**

4. **"Composing is not cheaper than the call it replaces, so no buffer size
   fixes it."** Written into three documents on the strength of a mixed mean
   divided by a call count. `band_hline_x` composes a 312-pixel stripe in 2,110
   cycles and `gfx_hline` draws one in 4,112 — the composer was already ahead
   on the operation it performs most. What was actually slow was `bnd_cell`
   and then `bnd_span`, neither of which anything had measured. Fixing them
   took the bar from 61.2 ms to 36.5 and **past** the baseline it was supposed
   to be unfixably behind. **Never generalise from an aggregate to a
   mechanism.**

5. **"Speed decides it."** It does not. docs/TEXT-PLAN.md §1.1 is the standing
   ordering — **flicker first, speed second** — and the composed title bar is
   the only one in the tree with no double-draw in it. A slower bar that does
   not flash is a trade this project takes. Any "verdict" on this rework that
   quotes only milliseconds is answering half the question.

6. **"The pixels match, so the new path works."** Not when the new path has a
   fallback that draws the same pixels. `BANDCOMP` was defined 500 lines after
   the `%ifdef` that gated `band_init`'s call — nasm's preprocessor is one
   pass — so the composer never ran, every screen was correct, and only the
   *timing* said so (41.0 ms, the baseline to the cycle). **Verify a fast path
   with a number or a counter, never with output alone.**

7. **"The blit pays §5.7's floor and `font_run` doesn't."** Neither does.
   `font.inc` uses `GFXCLIP` zero times and so does `gfx_blit1_x`; both are
   hand-rolled paths with their own prologues. The real difference was measured
   afterwards (§2), and the number that had been quoted for it was a residual
   fitted from ONE data point with an assumed per-byte rate — about 40% low.

---

## 7. The instruments

The design agent must measure. These exist and work:

| | |
|---|---|
| `/tmp/os88ver/tbar.py` (recreate) | breakpoint pair on `wm_draw_title`, entry to return, over one window raise with two windows up |
| `/tmp/os88ver/prof.py` (recreate) | the same for a set of symbols, summed per op — this is what found "the cost is per chunk" |
| the two-point solve | measure one op at two sizes and solve for fixed vs per-byte. One point is a fit, not a measurement |
| `tools/os88sym.py` | pass the knob's defines (`BAND`, `TITLESNAP`, …) or every address is wrong |
| `tools/kernsize.py` | sections, rungs, the 64 KB segment and the ladder |

**Driving rules that cost time to learn:** `os88mouse` polls the guest's own
cursor, so a guest stopped at a breakpoint cannot be driven — park the pointer
first, arm, then inject ONE raw packet. A package that draws continuously
(`apps/fractal`) cannot be driven at all under a text breakpoint. `m.advance()`
stops the guest; call `m.run()` after. And committing invalidates
`build/kernel.bin` for the symbol reader — `make` after committing.

---

## 8. What is in the tree now

- `kernel/band.inc` — the composer, **`make BAND=1`** (SPEC.md §5.9.6 — it was
  kern_big's default for a cycle and is a knob again), with a
  2 KB `MEM_K_BAND` heap claim (§5.9.2), the 1bpp storage polarity (§5.9.3) and
  both hoists (§5.9.4, §5.9.5). **Correct on all three adapters and the FASTER
  path on both 1bpp cards** — see §1.1. It is the only title bar in the
  tree with no double-draw in it at all, which is the property
  docs/TEXT-PLAN.md exists to get. What is left of its cost is the band clear
  and the blit, 25,933 cycles a bar, and the blit is this rework's own subject.
- `kernel/wm.inc`'s `wm_title_band` / `wm_title_band_x` / `wm_tsend` — the
  first consumer (§11.101), behind the same knob, with the fifteen-call path
  the one every other build draws: `kern_small` has no `gfx_blit1`, and a
  `BAND=1` machine whose heap refuses the 2 KB claim falls through to it too.
  `make` against `make BAND=1` is the A/B.

**Two things this rework OWES the composer**, and the second changed hands
when the default went back:

1. **VGA.** The composed bar is 38.3 ms there against the primitives' 30.3.
   That loss shipped while the composer was the default because the flicker is
   worth more (§5.9.6), and it is what a `BAND=1` build still chooses. If Phase
   1 and Phase 3 bring the per-call cost down far enough that fifteen calls
   stop flashing in practice, the composer stops being wanted at all. **Measure
   that, do not assume it**: `make BAND=1` is the A/B.
2. **`band_emit_x`, 18,776 cycles a bar.** That is `gfx_blit1_x`, whose 603
   bytes of which 78 do the blitting are this document's subject. It came OFF
   the shipped path with §5.9.6's flip, so the caller that cared is a knob
   build now — but `gfx_blit1` is API 0x0418 as well, and every package that
   composes a band (docs/TEXT-PLAN.md, apps/os88type.inc) is still that caller.

> **No longer available** (SPEC.md §5.9.6): while the composer was the default
> there was a second saving here — the fifteen-call path's own bytes, and any
> primitive that exists only to serve it. The fifteen calls are the shipped bar
> again, so that is off. Nobody had measured it.
- `gfx_blit1_x`'s odd-row fix (§5.4.2.3) — **unconditional, and it ships.**
- PERFORMANCE.md Sets 86, 87, 88, 89, 90, 91, 92 — the measurements this file
  is built on. Sets 91 and 92 are the two that reversed its conclusion.
