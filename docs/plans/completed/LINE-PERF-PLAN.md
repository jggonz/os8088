# Line drawing — what a pixel costs, and the cheaper walk

**Status: BUILT, and this document is now the record of how.** §4's candidate
is SPEC.md §5.6.4.1 and shipped in `kernel/vga12.inc`; §7's instrument is
`tools/os88linecost.py`. What landed, and where the numbers live:

| | |
|---|---|
| the walk itself | SPEC.md §5.6.4.1–§5.6.4.2, PERFORMANCE.md Set 70 — **4.76× steep, 5.13× shallow**, pixels identical |
| a defect the comparison found | §5.6.4.3 — a line entering the screen from the top drew **four rows low** on Hercules, and had since the primitive existed |
| what it cost | **762 bytes** of `.text` and one 512-byte image rung; `kern_small` does not get it (§5.6.4.4) |
| what it broke, and the fix | Set 72 — the *spread* by angle went 2× the wrong way and Missile Command's trails visibly changed pace. §5.6.6.1 made §5.6.6's wide walk the fallback |
| what it is worth to a program | Set 71 — `apps/wire` (§78) at **18.2 fps against 8.1**, which is the tick |

§5's estimates are left as written so they can be read against the outcome:
the `.text` guess was **~250–400 bytes** and the answer was **762** — §5.1
below is where that went.

---

## 5.1 Where the 762 bytes went, since the estimate was half of it

| bytes | |
|---:|---|
| **324** | the eight loop bodies (§5.6.4.2) |
| **280** | the setup — **161** of it eligibility, and that is the part the estimate missed |
| 41 | the register loads and the 8-way dispatch |
| 71 | `gfx_lf_wide3`, which is §5.6.6.1 and was not foreseen at all |
| 33 | `gfx_lm_pre`, net of 22 bytes it *saved* in `gfx_line_mono`'s header — the §5.6.4.3 bug fix, on both kernels |
| 13 | the two hooks and `.empty` |
| +11 `.bss` | `d`, `sub2`, `add2`, `first`, `cnt`, `flg` |

**The 161 is two octant blocks that are mirror images and cannot share code.**
Steep tests the whole x span against the box and derives its step interval
from y; shallow does the transpose. On an 8088 there is no cheap way to say
"the same code, over the other pair of words" — reaching a named word through
a computed base costs more per use than the duplication costs once. So the
split by octant, which is what makes the *loops* cheap, is also what makes the
*setup* expensive, and that is the same trade §4.1 describes seen from the
other side.

Nothing here is recoverable without giving back speed; §5.2 prices the three
candidates that were considered.

## 5.2 What the savings would have cost, measured

`tools/os88linecost.py pieces` prices the indirect jump the biggest one needs
at **37 cycles**:

| saving | what it costs |
|---:|---|
| ~38 bytes | collapse the four steep bodies behind one indirect jump: **+6%** on a 32×127 line, **+24%** on a 45° one — and the steep/shallow spread goes **1.18× → 1.47×**, which is buying back exactly what Set 72 cost a round to remove |
| ~148 bytes | an ink-independent plot (two read-modify-writes a pixel): **+25% steep, +30% shallow**, on every line |
| ~148 bytes | drop the black loops: every erase back to 723 cyc/px, **4.8×** |
| 324 bytes | no fast walk at all |

---



The question that started it: *DOS wireframe games draw many line segments a
frame on a 5150, so why is `gfx_line` slow?* The short answer is that it is
not slow per instruction — it executes **forty instructions a pixel** where
five or six would do, and on an 8088 almost all forty touch memory.

---

## 1. What a line costs today

`gfx_line` on a 1bpp adapter is exactly `a + b·pixels`, fitted over three
lengths per geometry and exact to one cycle at the middle length:

| | arrival | per pixel |
|---|---:|---:|
| steep (y-major) | 1,822 cyc = **382 µs** | 723.1 cyc = **151.5 µs** |
| shallow (x-major) | 1,905 cyc = **399 µs** | 657.1 cyc = **137.7 µs** |

The arrival here is the *near* entry — no far call, no `gfx_lock` — so a
package's own arrival is SPEC.md §5.7's ~756 µs and this is the part inside
it.

Two things fall out immediately:

- **A line pixel costs more than a whole `gfx_fill` scan line.** Set 4
  measured a 64-pixel fill row at 126 µs; one thin line pixel is 151.5. §5.6.1
  put the `gfx_line`/`gfx_fill` crossover at "~27px" *as an estimate*; the
  measured ratio says a walked pixel is far dearer than that estimate assumed
  and the crossover wants re-deriving from these two numbers.
- **The walk dominates from three pixels up.** 723 cyc/px against a 1,822 cyc
  arrival: batching arrivals (§5.6.8's `gfx_lstepv`) was worth doing and is
  not where the remaining time is.

---

## 2. Where the 723 cycles go

Measured piece by piece, 500 iterations each, net of the loop (Set 69):

| piece | cyc/px | share |
|---|---:|---:|
| the five per-pixel guard compares — §5.6.3's clip box + §5.6.6's `gfx_ln_wide` | **170** | 24% |
| the `e2` block — `e2 = 2*err` and both Bresenham tests, through `.bss` | **~270** | 37% |
| the row step, as `call gfx_nextrow` | **~134** | 19% |
| the framebuffer read-modify-write | ~38 | 5% |
| the rest — end-of-line test, ink select, the `jmp` back | ~110 | 15% |

**The pixel itself is 5% of the cost of drawing it.**

### 2.1 The one sentence that explains all of it

**40.8 instructions per pixel at 17.7 cycles each.** A register-resident
candidate measures 15.7 cycles an instruction, so the per-instruction cost is
not the finding — the *count* is. And 17.7 is simply what a direct memory
operand costs on an 8088: `cmp si, [gfx_ln_cx1]` is four bytes and a word
read, which is 15 clocks of execution, 4 more for the 8-bit bus, and 4 clocks
a byte of prefetch that the queue cannot hide across the loop's eight taken
branches. PERFORMANCE.md Part 2's `max(clocks, 4.34 × instruction bytes)`
floor is the whole story, and a 110-byte loop body is over it.

Every one of `dx`, `dy`, `err`, `e2`, `sx`, `x2`, `y2`, the four clip edges,
the wide flag and the ink is a word in `.bss`. There are eight general
registers and the loop uses four of them.

---

## 3. The three suspects

### 3.1 "Are we making the lines too good?" — partly, and only one item is expensive

Three things `gfx_line` does that a game's own Bresenham would not:

| | cost | verdict |
|---|---|---|
| §5.6.3's per-pixel clip-box test | **170 cyc/px, 24%** | the real one — and it is avoidable without giving up anything §5.6.3 protects (§4.2) |
| §39.4's three inks — the dither tested per pixel | ~40 cyc/px, 6% | small, and it disappears into a solid-ink specialisation |
| §5.6.5's dilation | **nothing on the thin path** | opt-in, erase-only; measured 1.36× steep and 2.98× shallow |

So: one quarter of the cost is generality, three quarters is the loop being
written the way it would be written on a machine with a data cache.

### 3.2 "Is it because Missile Command needs them erasable?" — no

Erasability is §5.6.2's downward normalisation: two compares and two `xchg`s,
**once per line**, about 20 cycles against a 94,000-cycle call. The dilation
(§5.6.5) is per-*erase* and the caller asks for it. Neither is in the
per-pixel path.

It is worth stating the stronger form, because it is the thing that makes §4
safe: **the candidate keeps the erase contract for free.** It lays the
identical pixel set, so a line drawn by one and erased by the other still
cancels.

### 3.3 "Is there a cheaper way?" — yes, ~4.9× measured, byte-identical

§4.

---

## 4. The candidate

Built as a stub, parked on a running os8088, measured on both 1bpp adapters,
and diffed against `gfx_line`'s own framebuffer output byte for byte
(`tools/os88linecost.py candidate`).

### 4.1 Split by octant, and the state disappears

`gfx_line_mono` runs one loop for every line, so it must ask each pixel which
axis stepped. Split it in two and the major axis steps *every* pixel, which
needs no test at all — and the minor axis's whole decision becomes the sign of
one register:

```
steep   (y-major): d = 2dx - dy;  per pixel: plot; if d>0 {x+=sx; d-=2dy}; d+=2dx; y++
shallow (x-major): d = 2dy - dx;  per pixel: plot; if d>0 {y++;   d-=2dx}; d+=2dy; x+=sx
```

That is the classical form, and `tools/os88linecost.py --model` checks it lays
**the same pixels as `gfx_line_mono`** over 9,480 endpoint pairs — 0
mismatches. It is not an approximation of the current rasteriser, it *is* the
current rasteriser with the invariants folded in.

The register file then closes exactly:

| reg | holds | reg | holds |
|---|---|---|---|
| AX | `d` | BL | the rotating bit mask |
| DX | the subtrahend (`2dy` steep, `2dx` shallow) | DI | framebuffer offset |
| SI | the addend (`2dx` steep, `2dy` shallow) | BP | `vid_rowadd` |
| CX | pixel count, so the loop ends on `loop` | ES | the framebuffer |

`x` and `y` are **not tracked at all** — DI and BL carry the position. That is
what frees the two registers, and it is only possible because of §4.2.

The steep body, whole:

```nasm
.px:
    or  [es:di], bl                 ; solid white: the plot is ONE instruction
    or  ax, ax
    jg  .xstep
.noxs:
    add ax, si
    add di, bp
    test di, [WRAPBIT]
    jnz .wrap
.back:
    loop .px
```

### 4.2 Clip ONCE, as an interval — not per pixel

§5.6.3 refuses to clip because *clipping the endpoints moves the lattice and
the erase then misses*. That is true and it is an argument against clipping
the **endpoints** — not against clipping.

Interval clipping does not touch the endpoints. It keeps the same start point
and the same initial `d`, and computes the **range of major-axis steps** that
fall inside the box. For a monotone line inside an axis-aligned rect that set
is contiguous, so it is one `[first, last]` pair, and the walk becomes: skip
to `first`, then run `last-first+1` iterations with no test. The rasterisation
is untouched, which is the property §5.6.3 exists to protect.

Two ways to reach `first`, and the first version should be the cheap one:

- **Walk without drawing.** Same loop, store suppressed. Costs ~83 cyc/px for
  the skipped pixels (measured) and nothing for the drawn ones. Correct by
  construction.
- **Jump the Bresenham state forward in closed form.** `d` after *n* major
  steps is computable, so the skip is arithmetic. Faster, and the thing to get
  wrong.

Screen clipping comes from the same mechanism, which is where `gfx_line` gets
its today, so nothing new is owed there.

**This is not an optional part of the change.** Keeping the per-pixel guards
means keeping `x` and `y` in registers, which pushes `2dx`/`2dy` back into
`.bss`; the modelled result is ~337 cyc/px, about **2.1×** rather than 4.9×.
The octant split and the interval clip pay for each other.

### 4.3 The row step, inline

Measured: `call gfx_nextrow` is **159 cyc**, the same three instructions
inline are **100**, and with `vid_rowadd` already in BP and the wrap test
inverted so the common case falls through it is **~28** — or ~7 on Hercules
alone, where `vid_wrapbit` is 0x8000 and a plain `js` tests it. That last one
is a trap, not a shortcut: CGA's wrapbit is 0x4000 and `js` silently lays a
different line there. The harness catches it; a reviewer would not.

`kernel/vga12.inc`'s `gfx_blit1` already inlines `gfx_nextrow` off stack slots
(PERFORMANCE.md Set 3), so there is precedent and a house form for it.

### 4.4 What it measures

128-pixel lines, both 1bpp adapters, the shipped kernel either side:

| | Hercules | CGA |
|---|---:|---:|
| `gfx_line` | 94,383 cyc (19,775 µs) | 86,063 (18,032) |
| candidate, portable row step | **19,415 (4.86×)** | **20,244 (4.69×)** |
| ...Hercules-only `js` wrap | 16,086 (5.87×) | *lays a different line* |
| setup, either way | 577 cyc (121 µs) | 575 |

Across all six geometries the portable form is **4.7–5.2×**, and every row
marked identical is byte-for-byte identical framebuffer.

### 4.5 What did NOT pay

**Accumulating a framebuffer byte** — one read-modify-write per byte instead
of per pixel — is worth **10%**, not the 8× the store count suggests: 112.1
cyc/px against 124.9. A line shallow enough for one byte to hold eight of its
pixels is one `gfx_line` already tail-calls `gfx_hline` for (§5.6.1); at
127×32 the row changes every fourth pixel and the byte must be spent then
anyway. PERFORMANCE.md rule 5 in miniature — the optimisation's shape was
right and its reason was not present. It is measured and in the harness so
that nobody costs it again.

---

## 5. What building it cost

**These were written before it was built and are left unedited**, so the
estimates can be read against §5.1's outcome. Every one of them turned out to
matter; the `.text` figure was the one that was wrong.

**SPEC.md §5.6.4 is rewritten first, not after.** It currently documents one
mono loop; this is four (steep/shallow × the ink cases) or two with the ink
handled by a patched opcode. §5.6.3's "the full line is walked and each pixel
tested against a box" becomes the interval, and the reasoning there — why
endpoint clipping is wrong — is what has to survive the edit intact.

**`.text`, and it is the thing most likely to refuse the change.** The steep
body is 34 bytes and the shallow one about the same, but a shipped version
needs the interval-clip setup, both ink specialisations and the
fallback for the dither: **~250–400 bytes, estimated and not measured.**
docs/KERNEL-MEMORY.md's two guards are different mechanisms and `kern_big`
has had as little as 116 bytes of rung slack; raising either is a decision
taken with whoever asked for the feature. **Assemble it and read
`kernsize.py` before believing any of this is affordable.**

**`tests/linetest` is the gate and it should pass unchanged.** It is a
byte-for-byte framebuffer comparison of a dilated steep fan (§5.6.6). If the
pixel set really is identical, that gate does not move — which makes it the
proof rather than a chore. The same is true of `make zgfx`'s screens and every
`disp*` pixel gate.

**§5.6.7's `gfx_lstep_mono` is the same loop and wants the same treatment** —
it is where Missile Command's drain actually spends its time (Set 5/6:
160 µs a walked pixel there against 570 through `gfx_line`'s arrivals). It
cannot simply be given the same body, though: it walks in the **caller's**
direction rather than §5.6.2's normalised one, so it is four octants and not
two, and it re-resolves its clip rect per pixel (`gfx_ls_box`) precisely
because a resumable walk does not know its own extent. The interval trick does
not transfer to it unchanged.

**And the two must move together, or neither.** `apps/cyclone`'s web repair
records that `gfx_line` and `gfx_lstep` are already *different* rasterisers —
measured, 86 pixels of disagreement — and repairs with the walker that drew.
Changing one lattice and not the other is safe only because that is already
the state of the world; changing one *to* the other would be better, and is a
separate decision.

**The VGA path is untouched.** `gfx_line_runs` coalesces along the major axis
into `gfx_fill_raw` calls and is not per-pixel; none of this applies to it,
and §5.6.4's split — mono gets the tight loop because mono *is* the 4.77 MHz
machine — stays exactly right.

---

## 6. What it would buy

A wireframe frame of 40 segments averaging 25 pixels — 1,000 line pixels —
drawn and then erased:

| | draw | draw + erase |
|---|---:|---:|
| today | 167 ms → **6 fps** | 334 ms → **3 fps** |
| the candidate | 36 ms → **28 fps** | 72 ms → **14 fps** |

That is the difference between "a wireframe is not a thing this machine does"
and "a wireframe is a thing this machine does". It is also, and more
immediately, ~24 ms off Missile Command's worst measured trail erase and
roughly a fifth off `apps/cyclone`'s web repair budget (§67.19 prices three
line walks at ~16 ms).

---

## 7. The instrument

`tools/os88linecost.py`, and it is worth keeping whatever happens to §4.

```
python3 tools/os88linecost.py --model        # the pixel sets, no emulator
python3 tools/os88linecost.py gfx_line       # the shipped cost, fitted
python3 tools/os88linecost.py candidate      # ...against the alternatives
python3 tools/os88linecost.py pieces         # the per-piece attribution
```

It is not a package and nothing goes on a floppy: MartyPC's `park` points the
CPU at an address with the prefetch queue flushed and clears IF, so a stub
written into `gfx_pairtab0` — 256 idle `.bss` bytes inside KERNEL_SEG — is
timed on a booted, otherwise-idle machine with no tick and no mouse ISR in the
number. `tests/gfxbench`'s four `GFX_LINE` rows remain the right instrument
for the shipped cost of the shipped call; this one exists because trying a
*different rasteriser* through gfxbench means a disk, a boot and a GUI to
drive, and a ladder of candidates has to be cheaper than that or it does not
get built. The two agree to 7%, in the direction gfxbench's extra far call and
wrapper predict.

The framebuffer diff is not decoration. It is what caught the `js` wrap
shortcut being wrong on CGA — 64 bytes different, no error, and a line that
looks entirely plausible on a screendump.
