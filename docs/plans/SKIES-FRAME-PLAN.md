# CLEAR SKIES — where the frame goes, and the candidates that are left

**Status: OPEN, and §7 is now the head of it.** §3 IS BUILT (SPEC.md 88.5.12)
— up to 5.64 ms, 2.38% of a frame; §2 is priced and parked; §4 is the queue,
§6 the wireframe question answered and closed, and **§7 is what an IN-FLIGHT
profile found that no pinned frame could — a banked turn costs 1.7× a level
one and the objects are barely any of it.** SPEC.md §88 is the contract, SPEC.md §88.12 is what the
frame costs and how it got there, and this file is only the *forward* list —
candidates with a measured ceiling apiece, in the order the evidence ranks
them.

It exists because SPEC.md §88.12's closing paragraph — *"the per-primitive
floors are the frame, and the remaining levers are content"* — is true and is
not the end of the argument. **A frame here is 170–255 ms and a tick is 54.9,
so the levers left are worth 1–3 ms each and there are several of them.**
Nothing in this file is worth 10%; two or three of them together are worth a
frame in ten.

## 0. The instrument, and the rule that makes its numbers mean anything

`make skiesprobe` + `python3 tests/skiescount.py --scene <s>` (SPEC.md
§88.11.1). **Every term is priced by ADDING it, never by removing it**, so
every arm draws the identical picture.

That rule is not fastidiousness, it is the difference between two answers.
`tests/skiesperf.py` prices a stage by NOPing its call, and **a NOPed call
takes its consequences with it**: NOP `cs_edge` and the chains keep their
+big/−big, so the polygon's rows are never filled and the reading is the
tracing PLUS the fill it removed. On `city` that is **17.50 ms by removing
and 8.47 by adding** — the first number is twice the truth, and a candidate
sized against it is a candidate sized against pixels it was never going to
save.

Both instruments are wanted. `skiesperf` answers *what does this stage cost*;
`skiescount` answers *how much of it is redundant, and what would replacing it
cost*.

### 0.1 A PIXEL A/B OF THIS PROGRAM IS NOT REPRODUCIBLE — and the control says so

Anyone changing a cull or a winding here will reach for the obvious gate: the
same pinned scene, old build against new, framebuffers compared byte for byte.
**It does not work, and it fails in the way that wastes the most time** — a
single scene of twelve differs, by a few hundred pixels in a band along the
horizon, and WHICH scene it is moves from run to run.

Four things were tried and none of them fixed it: capturing at a `cs_render`
breakpoint rather than after host frames; counting GUEST frames since the pin;
pinning the flight state the position does not (`cs_spd`, `cs_hs`, `cs_vs`,
`cs_ht`, `cs_thr`, `cs_thrust`, `cs_thracc`, `cs_rrate`, `cs_prate` — the
simulation advances by TICKS, not frames); and re-pinning all of it before
every frame, at the breakpoint, so the poke lands on the same instruction in
both arms.

**The control is what ends the argument**: two arms that are byte-identical in
behaviour AND in speed — `[cs_axoff]` = 1 on both sides of the same image —
differ in **2 runs of 6, by 865 and 896 pixels**, in the same band. So the
noise floor of this comparison is about 900 pixels and it is not the change.

Run it anyway — **it is what found the SI clobber in §3** that the verdict
audit could not see — but do not certify with it. **Certify with a verdict
audit**: the new test's answer against the old one, face by face, on the guest,
counted (SPEC.md 88.11.1's `cs_axoff` arm). That is deterministic, and it is
what says 0 disagreements over 12 scene-and-fill configurations.

### 0.2 …and an A/B that can silently measure NOTHING needs an arm check

`cs_axmask` was added to bisect which family of faces caused a difference.
Package bss is ZEROED, so it defaulted to 0, and `and dl, [cs_axmask]` then
cleared every axis bit: **the cull was inert in BOTH arms** and six scenes read
a tidy +0.06%. The timing looked perfectly reasonable. What caught it was
printing, beside the milliseconds, the COUNTERS OF WHAT EACH ARM ACTUALLY DID
— faces walked, faces the winding culled, faces the axis test culled — and
seeing `axis-culled 0` on both sides. Every A/B here prints that line now.

## 1. The scene decides the answer, and §88.12's three are the wrong ones

`runway`, `city` and `tower` are the frames SPEC.md §88.12 is measured on and
they are right for that. They are **wrong for any question about building
faces**: the skyline in them is far enough to be LOD-boxed (SPEC.md §88.5.4),
so it contributes no faces at all, and the polygons that actually cover the
view are the one-face FLAT models — ground, river, runway, roads — which have
no shared edges and no back faces.

`dflevel`, `dfangled` and `dfsquare` stand among La Défense's six
`cs_m_tower2` — 60 m square, 110 m tall — at ~950 m, so all three share a
projected size and an LOD rung and differ only in where the eye is. **Four
times the tracing of the pinned three**, and the discriminator is the PITCH:

* eye ABOVE the roofs → a box shows two walls AND a roof: three faces meeting
  at **three shared edges of twelve traced**, measured at 23.2% and 25.0%.
* eye BELOW them → two walls: **one shared edge of eight**, measured 11.1%.

That is the textbook arithmetic landing on the glass, and it is why a
candidate about faces must be measured on a scene that has some.

**Add a `df*` scene to any face-or-edge measurement in this file. Quote the
pinned three beside it, never instead of it.**

## 2. PRICED AND PARKED — dedup the shared edge (SPEC.md §88.4.2.1)

`cs_poly` traces all four edges of every front face, so an edge between two
front faces is scan-converted twice. `cs_wire` already refuses that for the
outline; the fill does not.

**Refused at runtime, on all six scenes**: the test (`cs_edgemark` on the
face's own indices, 295–501 cycles) is paid on 20–68 edges to save on 1–17,
and the net is **−1.12 to −2.73 ms**. With the topology precomputed per model
— a flag beside each face index, ~60 cycles — the ceiling is **+1.56% of a
frame** on `dfangled` and under +0.35% on four of the six.

Parked rather than closed, because the ceiling is real and the cost is
bounded: a topology byte per face-edge over 122 models, in a package that has
already met `APP_MAX_SIZE` at a merge (SPEC.md §88.5.9). **If §3 is built
first the two do not add**, and §3 is worth more for less — the faces §3
removes are the ones §2 would have shared edges with.

The finding worth keeping either way: **in WIREFRAME a duplicate edge is
duplicate PIXELS, and in a FILL it is duplicate BOOKKEEPING.** Two front faces
fill two different interiors, and `cs_polyrows_herc` lays a row as two masked
end bytes and a `rep stosw` between, into the shadow; only the span table is
refilled. The shape of a redundancy says nothing about its price.

## 3. BUILT — the cull walk: 29–60% of faces were gathered, projected and thrown away

`cs_faces` decides a face is back-facing from the **signed area of its
PROJECTED points** (SPEC.md §88.5.10). Correct, and exact since the quad
became its diagonals' cross — but it is decided *after* the face's vertices
have been gathered by index into `cs_pv` and the two `imul`s taken. A face
that is culled has paid all of that for nothing.

**Measured** (`cs_dupface`, which repeats the gather and the cross into
scratch, so the arm draws the identical picture):

| | runway | city | tower | dflevel | dfangled | dfsquare |
|---|---|---|---|---|---|---|
| faces walked | 9.0 | 15.8 | 14.6 | 28.1 | 28.1 | 28.1 |
| back-culled | 3.4 (38%) | 4.5 (29%) | 5.6 (38%) | **16.9 (60%)** | 11.2 (40%) | 11.2 (40%) |
| a face's preamble | — | 1,326 cy | — | 1,402 | 1,380 | 1,376 |
| **the culled ones cost** | — | **1.25 ms (0.70%)** | — | **4.96 ms (2.14%)** | **3.25 ms (1.27%)** | **3.24 ms (1.30%)** |

**`dflevel` is the case to design against and it is the ordinary one** — level
flight among buildings, where a box shows two of its five faces and three are
culled. It is also where §2 is worth least (0.35%) and this is worth most
(2.14%): the two candidates are complements, not alternatives.

The measured figure is a **lower bound** — the arm repeats the gather and the
cross and not `.cnt`, the near/side vertex count that runs in front of them.

**BUILT as `cs_axcull` (SPEC.md 88.5.12), 143 bytes**, and it took most of the
ceiling: **−5.64 ms (−2.38%) on `dflevel`**, −3.71 on `dfangled`, −3.68 on
`dfsquare`, −1.02 on `runway`, −0.64 on `city`, −0.65 on `tower`. On `dflevel`
350 walked faces become 140 and the winding is left with nothing to cull.

**It needed no per-model data and no multiply**, which was the finding that
made it worth building: a stack's side face is an axis-aligned plane in WORLD
space when its level pair is untapered, so *"the eye is behind it"* is one
compare against the eye's own world position — and `cs_scale` already has that
offset, it being the input to the rotation. `dot(M x̂, M d) = d.x` for an
orthonormal M, so the camera-space dot product a normal test would take is the
number that was there before the rotation.

### 3.1 What was tried, and what is left

**Taken: option 1**, in the form below. **Options 2 and 3 are still open and
are what would widen it** — the axis test refuses a tapered level pair, a face
with no axis flag and every `CSF_NOCULL` face, and those refusals fall through
to the winding at full price.

1. **A STACK's side faces are two of four, and the signs say which.** A box's
   four side normals in camera space are ±M₀ and ±M₂ — **columns of the matrix
   `cs_matrix` already built this frame** — so at most two of the four can face
   the eye, and which two is the sign of two dot products with the object's
   camera-space origin. Computed once per OBJECT (not per face), that skips
   two of five faces before either is gathered. The top face is a third sign
   against M₁. `CS_SIDES`/`CS_TOP` (SPEC.md §88.6) emit the faces in a fixed
   order, so the mapping from signs to face indices is a table and not a
   search.
2. **…and if that is too coupled to the model macros**, the cheaper half
   alone: gather **two vertices before four**. The diagonals' cross needs
   v0..v3, but a *sign* test on the two diagonals can be attempted from the
   projected `cs_sxv`/`cs_syv` by index without copying anything into `cs_pv`
   — the copy is what the `.cp` loop is for, and a culled face never needs it.
   This is a pure win with no per-model data at all, and it is the one to
   measure first.
3. **Order the walk so a cull is cheap to repeat.** Every consumer already has
   `cs_pinside`/`cs_pwhole` (SPEC.md §88.5.7); a per-object *facing* byte set
   once would join them.

**Do not touch the winding TEST itself.** SPEC.md §88.5.10 is a bug fix with
photographs behind it: the whole polygon's area, not one triangle. Anything
here must produce the same verdict on the same face — the gate is
`tests/skiesgeom.py --clobber-fan`, and `tests/skies.py`'s full-redraw diff is
what catches a face wrongly dropped.

**What would make it not worth doing**: if the per-object sign work costs more
than 1,326 cycles × the faces it removes. On `dflevel` that is 16.9 faces, so
the budget is generous; on `runway` it is 3.4 and the budget is ~4,700 cycles
for the whole object pass. Measure both.

## 7. THE BIGGEST THING HERE — the rolled horizon, and the ADI: BOTH BUILT

`tests/skiesprof.py` (SPEC.md 88.12.1) flies five profiles instead of pinning
one, and every stage is bracketed at its call site so the accounting adds to
99.9% of the loop. Two findings dwarfed everything else in this file, and both
have been taken: the horizon's span pass (7.1, SPEC.md 88.3.1.1) and the ADI's
erase table (7.2, SPEC.md 88.9.2.5). **What each of them LEFT is where the
next reading goes** — 7.1.3 and 7.2's own tail — and in both cases the answer
turned out to be the same shape: what is left is fixed cost a row rather than
pixels.

### 7.1 BUILT — a banked turn is 1.7x a level one, and it is the SKY that does it

| Hercules 8088, 20 flown frames | level | 45 deg held |
|---|---|---|
| frame | 164.5 ms | **280.1** |
| `cs_skyground` | 8.76 | **47.75** (5.5x) |
| `cs_blit` | 9.01 | **36.17** (4.0x) |
| the two together | 17.8 ms, 10.8% | **83.9 ms, 30%** |

SPEC.md 88.3.1 predicted it in its own words - *"a row whose kind changed, and
every split row, is refilled and marked whole"* - and a rolled horizon makes
every row a split row. So the sky/ground pass refills the whole view and the
blit then has the whole view to carry.

**COUNTED, with `CSHZPROBE`** (`make skieshzprobe`; SPEC.md 88.3.1.1), 20
flown frames a profile, Hercules, the view 400x112 and so 50 bytes wide:

| | split rows a frame | carried today | the crossing's band | crossing did not move a BYTE |
|---|---|---|---|---|
| `turnhold` 45 deg held | **112 (every row)** | 5,600 B | **336 (6.0%)** | **112 of 112 - 100%** |
| `rollsweep` 2 deg/frame | 109.1 | 5,455 | 441 (8.1%) | 64.5 (59%) |
| `bank` decaying | 29.1 | 1,452 | 123 (8.5%) | 12.6 (43%) |
| `cruise` level | 1.0 | 50 | 3 (6.0%) | 1 (100%) |

**The first reading of that table was wrong in its biggest cell and looked
right**: `cs_dbg_hzby` is a word, 112 x 50 x 20 is 112,000, and `turnhold`
reported **2,323** - one wrap divided by the frames. Rows x `[cs_wbn]` is what
caught it.

#### 7.1.1 What was built: the SPAN, in a pass of its own

A split row still LAYS the whole view; its **span** is the crossing's own byte
and one either side. The span says what CHANGED, and `cs_blit` carries the
union of this frame's set and last frame's - which holds last frame's band and
whatever an object drew - so nothing is left on the glass. A row that was not
split last frame gets `cs_fullspan`; Mode X is refused outright, `cs_r_begin`
setting every kind to 3 there so that a kind of 3 tells you nothing.

| | frame | `cs_skyground` | `cs_blit` |
|---|---|---|---|
| `turnhold`, before | 280.1 | 47.75 | 36.17 |
| ...decided INLINE in the fill loop | 280.2 | 55.88 | 28.31 |
| ...**decided in a pass of its own** | **276.0** | 51.70 | 28.37 |
| `bank`, before | 256.0 | 34.60 | 33.39 |
| ...**decided in a pass of its own** | **254.3** | 36.88 | 29.04 |
| `cruise`, before | 164.3 | 8.77 | 9.95 |
| ...decided in a pass of its own | 164.5 | 9.27 | 8.83 |

Level flight is untouched - the band there is ONE row - and `cruise`'s 164.5
against 164.3 is inside its own 4% spread.

**+125 bytes**, `tests/skieshz.py` is the gate, and `[cs_hzfull]` is the
runtime A/B (the profiler takes `--hzfull 1`).

**The lesson is the two middle rows.** Inline in the loop that was already
walking those rows, the identical arithmetic cost **533 cycles a row** and the
whole change measured **nothing**: 280.2 against 280.1. In a walk of its own,
constants hoisted into registers and the rows stepped with `lodsw`/`stosw`, it
is **~168**. Nothing was removed from the decision. What fell is the number of
BYTES of code a row runs through, which on an 8088 is the price.

#### 7.1.2 REFUSED - narrowing the fill's range

The obvious companion is to lay only `union(last frame's span, this frame's
band)`. It was built and measured, `cs_hzproc` bracketed at all 112 of its
calls a frame:

| `cs_hzrow_sh`, `turnhold` | ms a frame | cycles a row |
|---|---|---|
| the whole view - 50 bytes | 33.76 | 1,437 |
| the union - typically 4 to 10 | 29.92 | 1,273 |

**A tenth of the pixels is 11% of the time.** ~1,100 cycles of a split row's
fill is fixed - the row offset, the crossing byte, the mask lookup, two
`cs_fillrun` calls - and the 50 bytes it lays are ~350. Computing the union
costs about what it saves, and it wants `cs_hzb0`/`cs_hzbn` threaded through
all three row fillers.

#### 7.1.3 BUILT - the fill's fixed cost, measured and then cut

`cs_hzproc` bracketed at all 112 of its calls a frame, and its two
`cs_fillrun` calls inside that:

| `cs_hzrow_sh`, `turnhold` | ms a frame | cycles a row |
|---|---|---|
| the two `cs_fillrun` calls - 49 bytes of pixels | 15.60 | 664 |
| **its own body, everything else** | **17.76** | **756** |
| the whole call | 33.37 | 1,421 |

`rep stosw` over 49 bytes is ~350 cycles, so **1,070 of 1,421 is overhead**,
and all of it is work the caller had already done or the frame had already
decided. Three cuts (SPEC.md 88.3.1.2), **+8 bytes**:

1. **DI is passed and WALKS the row.** The band loop holds the row's first
   view byte and steps it by the stride; the routine rebuilt it from
   `cs_rowoff` and `cs_tbase`, reloaded ES, then bracketed the left run in
   `push di`/`pop di` to get back to it. Now the left run leaves DI on the
   crossing's byte, the blend is a `stosb`, the right run carries on.
2. **The two runs are INLINE** (`FILLRUN`). Each was a `call` into a routine
   that re-did `cld`, the odd-address test and the halving - 314 cycles a row
   between them. `cs_fillrun` survives for `cs_hzrow_modex`.
3. **The pixel mask is the FRAME's.** Which of `cs_hlm`/`cs_clm`, and whether
   the index masks to 7 or 3, is the adapter's answer; it was re-decided every
   row. `[cs_hzmt]`/`[cs_hzmm]` now, set beside the ink patterns - and the
   `push cx`/`pop cx` round the shift goes with it.

| Hercules 8088, 20 flown frames | frame | `cs_skyground` | `cs_blit` |
|---|---|---|---|
| `turnhold`, before 7.1 | 280.1 | 47.75 | 36.17 |
| ...with the span pass | 276.0 | 51.70 | 28.37 |
| ...**and the fill diet** | **267.3** | **42.74** | 28.31 |
| `bank`, before 7.1 | 256.0 | 34.60 | 33.39 |
| ...**and the fill diet** | **248.7** | **31.81** | 28.89 |
| `cruise`, before 7.1 | 164.3 | 8.77 | 9.95 |
| ...**and the fill diet** | 164.5 | 9.28 | 8.83 |

**280.1 -> 267.3 ms in a held bank, 3.57 -> 3.74 fps, for 133 bytes.**

#### 7.1.4 BUILT - and the row stops being a CALL at all

7.1.3 took the filler's half and left the seam: 1,042 cycles a row inside it
and **601 more in the band loop around it** - three push/pop pairs, the ink
pair fetched as two bytes through two pointers, the sentinel test and a
call/ret. The band is ONE loop with no call in it now, on the shadow
backends: the ink PAIR is a word in `cs_hzpat4` built once a frame, the walk
of `cs_xl` is `lodsw` against a hoisted `[cs_hzend]`, and the row's three runs
write straight through DI. Mode X keeps the per-row call; `cs_hzrow_sh` and
`[cs_hzproc]` are DELETED, so it is **+89 bytes**.

| Hercules 8088, 20 flown frames | frame | `cs_skyground` | `cs_blit` |
|---|---|---|---|
| `turnhold`, before 7.1 | 280.1 | 47.75 | 36.17 |
| ...span pass | 276.0 | 51.70 | 28.37 |
| ...fill diet | 267.3 | 42.74 | 28.31 |
| ...**fused** | **263.1** (3.80 fps) | **38.41** | 28.44 |
| `bank`, **all three** | **246.1** (4.06 fps) | 29.18 | 29.21 |

**280.1 -> 263.1 ms in a held bank for 222 bytes.**

**IT BOUGHT A QUARTER OF WHAT THE BYTE COUNT SAID.** Predicted ~936 cycles
against 1,643 (16.6 ms) by the method that made 7.1's span pass a 3x win;
measured ~1,460, so 4.3 ms. The span pass's body is ~33 bytes of register
work; a fill row is ~135 bytes with twenty memory operands and two `rep stosw`
runs whose 25 words are ~600 cycles - **41% of the row on their own**. Count
bytes where the body is registers; treat it as an upper bound where the body
is memory.

#### 7.1.5 REFUSED - the "pre-pay the horizon" table, and the cache behind it

The idea was to pre-generate the sky/ground picture for a constrained set of
attitudes and make `cs_skyground` a memory copy. **The premise is right and
better than it looks**: `cs_matrix`'s second column is `(-sr.cp, cr.cp, sp)`,
so the horizon is a function of `(roll, pitch)` ALONE - no heading, no
position, no framerate.

It is refused on two counts, and the first needs no look judgement. **A copy
is 4 bytes over the bus per byte laid where a fill is 2** (`rep movsw` reads
and writes; `rep stosw` only writes), so a pre-made picture is ~1.8x the cost
of the fill it replaces on an 8088 - and its only advantage, no per-row
decision, is what 7.1.3 and 7.1.4 buy for 97 bytes. Second, it does not fit:
one pixel at the view edge is 0.29 degrees of roll and one row is 0.20 of
pitch, so ~1,257 x 112 = 140,784 states - 788 MB of pictures, 31.5 MB of
`cs_xl` arrays, 1.1 MB of line endpoints. At a fixed roll pitch is a pure
vertical shift, so a strip per roll would do; a strip is 224 rows x 50 bytes =
11.2 KB, so even 32 roll steps is 358 KB on a machine with 50.5 KB of free
heap. (Quantisation is NOT the argument: at 3.6 fps a decaying bank already
steps ~2 degrees between displayed frames.)

**The cache behind it is refused on a measurement, and a SECOND profile was
built to attack the measurement.** "Has the horizon moved" is an exact
five-word compare once a frame, and in a held bank it has not moved at all.
`turnhold` is busy on purpose, so `sparse` was added - the same held bank over
an empty corner of Paris, the Issy aerodrome's three outbuildings (12, 10 and
15 m) and two of the Seine's western ribbons, nothing tall and no sky object:

| held 45 degree bank, 20 frames | `turnhold` | `sparse` (EMPTY) |
|---|---|---|
| frame | 263.1 ms (3.80 fps) | **77.6 ms (12.88 fps)** |
| `cs_scene` | 176.07 | 6.50 |
| `cs_skyground` | 38.41 (14.6%) | **38.64 - 49.8%** |
| `cs_blit` | 28.44 | 15.09 |
| band rows object-free | 0 of 112 | **112 of 112** |
| mean span width | 31 of 50 bytes | **3.0** |
| horizon identical to last frame | 20 of 20 | **19 of 20** |

**With nothing to draw the horizon IS the frame**, and every row is skippable
- the case the cache wanted. On a still frame 77.6 -> ~43 ms, 12.9 -> ~23 fps,
on nineteen frames in twenty. In `turnhold` and `bank` it is worth nothing.

**That 19 was 7 until the instrument was fixed.** The held-bank profiles
pinned the roll at the FRAME's start and let the model run, so what cs_matrix
saw was 45 degrees less whatever 88.7.5's easing rolled out over that frame's
ticks - and at 77.6 ms a frame takes one tick or two. The raw attitude took
exactly two values 146 units apart, one tick of roll-out, and the horizon
alternated between two positions three rows apart. It was the PIN wobbling,
not the aeroplane. Held profiles pin at the matrix now.

**THE AERODROME WAS TRIED FIRST AND IS THE WRONG SCENE.** Three short
buildings and two of the Seine's ribbons reads as sparse and measures as busy:
0 of 112 rows object-free and spans WIDER than turnhold's, 33.1 against 31,
with FEWER objects and an empty sky. `cs_markrows` marks an object's BOX
(88.3.2) and a flat ground model kilometres across whose ink is a thin
diagonal has a box the size of the view. A per-row interval represents a
diagonal horizon perfectly; ONE RECTANGLE PER OBJECT CANNOT REPRESENT A
DIAGONAL AT ALL.

#### 7.1.6 What is left, in the order the evidence ranks it

1. ~~The narrow fill~~ - **BUILT AND REFUSED, 7.1.8 below.**
2. ~~The horizon cache~~ - **CHECKED AND REFUSED, 7.1.12 below**, and what
   the check FOUND is item 5. A split row is an OCCUPIED row.
5. **THE MARK, at slight bank** (7.1.12.1) - the open one. A 5 degree bank
   costs the frame 34.2 ms and the horizon is 27% of it; one object's mark
   goes 2 rows to 52 across 12 degrees while its ink stays two rows thick.
4. **`cs_blit`'s own per-row walk**, ~300 cycles over rows that are mostly a
   few bytes now.

Two instrument traps, both of which cost a run:

1. **A breakpoint takes a FLAT address** and the listing gives an offset in
   the package. Arm one without the load segment and nothing hits - and the
   wait then sits out its whole limit looking like a slow GUEST.
2. **`cs_hzy0`/`cs_hzy1` are not the band.** They are where the line meets the
   view's left and right edges: at 45 degrees in a 400-wide view that is rows
   -61..174 of a 112-row view, so a host-side walk that trusts them reads 236
   rows and a negative `y0` reads in FRONT of the span array.

### 7.2 BUILT — the ADI was 41 ms a frame for as long as the attitude was moving

The released bank decays 45 deg to 0 over 24 frames and the panel goes with it:
**44.7 ms a frame while the roll is moving, 2.4 ms once it settles** - a cliff
in one frame at frame 10 of the trace. 88.9's items redraw when the value they
show changes, and in a turn the attitude indicator changes every frame.

**14% of a banked frame is one instrument.** The held bank is CHEAPER in the
panel than the released one (5.02 against 20.61) for exactly this reason,
which is also the proof that it is the ADI and not the panel in general.

Nothing here is a defect - it is redrawing because it changed. What was worth
pricing is HOW it redraws, and the answer was not the line at all: **90% of
the ADI is the ERASE**, a filled ellipse whose half-width is a SQUARE ROOT A
ROW, taken again on every redraw for a radius that cannot change in flight.

**An F6 cycled four modes** to price it (SPEC.md 88.9.2.5), on `skiesprof`'s
`rollsweep`:

| | `cs_panel` mean | worst frame | the erase per redraw |
|---|---|---|---|
| `Full` - the root a row | 22.43 ms | 44.88 | **30.8 ms** |
| **`Fast` - the table, and WHAT SHIPS** | **15.84** | **33.41** | **14.6 ms** |
| `Small` - `Fast` + a half-radius glass | 9.06 | 20.99 | 7.5 ms |
| `Off` - not drawn at all | 4.28 | 8.31 | 0 |

Confirmed on the shipped default, same session, `rollsweep`, tier 1, twenty
frames, the two arms of the new build reading identically: `cs_panel` **22.53
-> 14.16 ms** mean and **44.80 -> 28.78** worst, the FRAME **243.2 -> 235.0**
(4.11 -> 4.26 fps).

**`Fast` is now the only path and the ladder is gone.** It is
**pixel-identical** - 0 differing of 5,040 over the instrument's box - so it
wins outright, where `Small` and `Off` buy their time by drawing less and are
not choices a player should have to find. Removing the key, the three other
modes and the byte behind them gave **107 bytes** back, and took
`skiesprof`'s `--adi` with them.

**The general shape is worth keeping**: a knob that exists only to measure
an alternative is an INSTRUMENT, and once it has answered it comes out. It
was never a setting.

**What is left is `cs_prect`, one call a row.** Going further means laying
those rows without its per-row loop and `cs_markspan`, or composing the
instrument into a band and blitting it once (5.9's shape). Neither is done,
and the second is what the screen saver already does for a whole cube.

### 7.3 ...and three smaller things the same run turned up

* **`cs_step` is 6.2% of a level frame** - three calls, one per tick. Every
  measurement in 88.12 charges it nothing, the world being paused there.
* **`cs_fclip` is 10.91 ms in the CLIMB** against 4.39 level, 7.1% of that
  frame: on the runway the strip crosses both side planes at its near end,
  which is 88.5.7's own worst case, measured in flight for the first time.
* **`cs_consider` never drops below 6.5%** - 47 objects considered every
  frame to draw 7 to 17, 9.9 to 19.0 ms. 88.5.2's skip ticks already cut it;
  what is left is the largest stage after the drawing.

## 4. The queue behind it, with what is known about each

* **The erase under a solid.** `cs_skyground` refills a row and the faces then
  write over the part they cover, so those pixels are written twice. The
  refill is already span-limited to last frame's span for the row (SPEC.md
  §88.3.1), so this is smaller than it looks — but `skyground` is 7.5% of the
  `city` frame and nobody has measured how much of it a solid immediately
  covers. **Count it before designing anything**: the arm is a counter in
  `cs_polyrows_herc` for bytes written over a row the sky pass refilled this
  frame.
* **The outline over a filled solid.** A solid above `CS_EDGEPX` draws its
  faces AND its edges (SPEC.md §88.4.7), so a shared edge's pixels are laid by
  two fills and then a `cs_seg`. `seg (in edges)` is 7.4% of the `city` frame.
  This is a LOOK question and not a free win — the outline is what makes a
  wall read as a wall at size — so what is wanted first is a screenshot pair,
  not a measurement.
* **The two `rep stosw` inits in `cs_poly`.** The chains are stored
  unconditionally by `.left`/`.right`, so the +big/−big init is only load-
  bearing for horizontal edges' `.both` and for rows no edge covers. ~28
  cycles a row per face. Small, cheap to try, and needs a proof that a convex
  polygon clamped to the view leaves no row uncovered.
* **`.cnt` when `cs_pinside` is set.** SPEC.md §88.5.7 skips the per-vertex
  side test for a whole object; the per-face `.cnt` loop in `cs_faces` still
  runs. Worth a counter before anything else.

## 6. CLOSED — the wireframe's per-pixel write (SPEC.md 88.4.3.1)

Asked because §88.13.3's dedup was worth 16.7 ms and a 1bpp pixel is a
read-modify-write of the byte around it: *is the shadow buffer's alignment
costing us, and is the bigger win still out there for wire mode?*

**No.** One `or [es:di], al` is **14.2–14.5 cycles**, every plot in a wire
frame comes to **0.9–1.5% of it**, and the plots a byte accumulator could merge
are **0.08–0.21%**. Three reasons, all measured:

* **78% of the pixels are steep or vertical**, where consecutive pixels are 80
  bytes apart and nothing can merge. A wireframe tower is made of steep lines.
* **Above six pixels a row the slice already lays whole runs** (§85.3.6) —
  23.6 segments of 47.3 take it — so the only mergeable arm is a shallow line
  under six a row.
* **The write is 14 cycles of a steep pixel's ~85.** The rest is the DDA and
  the `loop`. Nothing is 8-alignment: a polygon row's middle is `rep stosw` on
  whatever alignment, the 8088's bus being eight bits wide (§88.4.6).

**What §88.13.3's dedup actually removed was whole SEGMENTS**, not their
pixels: ~2,400 cycles of clip, mark, DDA setup and dispatch each, 47.3 of them
a frame. And its 167.5-against-184.2 figure is **Mode X**, where a pixel is an
`out` and a store rather than 14 cycles — so the pixel share there is much
larger than it is on the shadow backends.

**Where a wire frame's time actually goes is the per-segment floor**, and that
is the open question this leaves: 47.3 segments a frame at ~2,400 cycles is
~24 ms of a 172 ms frame. Nobody has priced the parts of that floor.

## 5. Ruled out, so nobody re-derives them

* **A whole-object scanline pass** — one span table for the silhouette, spans
  merged per row and emitted left to right. It collapses the per-face bounding
  box, the table init and the per-row address computation, and it costs a
  per-row span merge on an 8088. The prize it is competing for is the poly
  floor, and §2's numbers say the whole edge subsystem is 10.8% of the frame
  in its best scene: a merge that costs anything per row cannot come out
  ahead. Not measured; refused on the arithmetic.
* **Sharing the DDA rows between two faces without a topology table.** That is
  §2, and it is measured: −0.69% to −1.28%.

#### 7.1.8 REFUSED, MEASURED - the row laying the UNION and not the view

The last candidate: lay `union(last frame's span, this frame's band)` instead
of the whole view. Correct - the union is every byte that can differ from the
glass, and outside it the row already holds the pattern this frame would lay -
and `tests/skieshz.py` passes unchanged. **+109 bytes, and a regression
wherever there is anything to draw:**

| 20 flown frames | fused | + the narrow fill |
|---|---|---|
| `turnhold` | **263.1** | 270.1 (**+7.0**) |
| `bank` | **246.1** | 248.6 (**+2.5**) |
| `sparse` - the EMPTY turn | 77.6 | **71.3** (-6.3, 12.9 -> 14.0 fps) |

**A proportional saving against a fixed cost.** The union costs ~340 cycles a
row to obtain - three indexed reads, four compares, and u0/u1/the mask in
memory temporaries because the fused loop has no register left - and laying a
byte fewer saves ~10. It pays when the union is under 16 bytes of the 50:
sparse's is 3, turnhold's is 31. Halving the block would move break-even only
to 30, so it is not a code-quality problem.

**It is 7.1.7's wall from the other side.** The narrow fill fails because the
mean span is 31 and not 12; the span is 31 because a mark is an object's BOX;
tightening the box costs more than the blit it saves. Reverted,
byte-identical.

#### 7.1.9 Where this leaves the horizon

Three changes KEPT (7.1, 7.1.3, 7.1.4) and three REFUSED with numbers (7.1.5's
table and cache, 7.1.7's marks, 7.1.8's fill). **Everything that paid removed
per-row FIXED cost; nothing that tried to draw LESS paid at all**, because the
blit carries a byte for ~4.5 cycles and every scheme for carrying fewer costs
more than that to decide.

What is left, in the order the evidence ranks it:

1. ~~`cs_blit`'s own per-row walk~~ - **MEASURED AND PART-TAKEN, 7.1.10.**
2. **The horizon cache - PARKED, not refused.** Worth an empty turn's 77.6 ms
   -> ~43 on nineteen frames in twenty. The owner's reading is that it helps
   HALF-empty scenes too, not only the extreme, and that the question is
   whether it costs a busy scene anything - which it need not: the "has the
   horizon moved" test is one five-word compare a FRAME, so a still frame can
   branch to a second band loop and a moving one runs the loop it runs today,
   unchanged. Pick it up with that shape, and measure `turnhold` for a
   regression rather than assuming there is none.
3. **`cs_scene` itself** - BROKEN DOWN in 7.4 below.

#### 7.1.10 BUILT - cs_blit's row is 490 cycles, and one of them was a segment

Breakpointing the row loop's own top and taking the delta between hits gives
the row whole, copy and all - 856 of them over `turnhold`:

| a `cs_blit` row, Hercules | cycles |
|---|---|
| **empty** - nothing in either set | **~129** |
| narrowest working row (p10) | ~518 |
| median working row | 904 |
| widest | 1,850 |

So the fixed part is **~490 a row** and a word to Hercules VRAM is **40-49** -
confirmed independently by solving the two unknowns from two scenes. That
splits turnhold's 28.4 ms blit into **12.9 walking, 14.8 copying, 0.6 empty**.
**The empty rows are not the target**: the range is rows 0..157, not the
screen's 348, so only 29.7 of 155.7 rows are walked for nothing.

**What came out of the 490 was a segment register.** The row swapped DS to the
shadow and back every iteration - 43 clocks for a value that cannot change
inside a frame. A package runs CS = DS, so DS holds the shadow for the whole
walk now and the three package-data reads take a `cs:` override. **+4 bytes,
cs_blit 28.36 -> 27.2 ms.**

#### 7.1.11 THE CONTROL MUST BE SAME-SESSION - and this is how that was found

The -1.1 ms above is quoted against a control built from the committed source
and run beside the change, because measured an hour apart **the identical
build reads 263.1 ms and then 266.6** on the same profile - 1.3%. Host-side
breakpoint overhead changes how many 18.2 Hz ticks land inside a frame, the
aeroplane flies a slightly different path, and `cs_scene` follows it: 176.07
then 177.67 for code that did not change. A change INSIDE cs_blit therefore
appeared to make cs_scene 1.8 ms slower.

`tests/skiesprof.py` is exact WITHIN a run - the brackets are cycle counts and
a stopped guest burns none - and the drift is entirely in which frames get
flown. Build the control from the tree you are comparing against, run it
beside the change, and treat any cross-session delta under ~2 ms on a 260 ms
frame as unmeasured.

#### 7.1.12 DESIGN CHECK - the banked row cache at SLIGHT bank, and where the money really is

The ask was the third case §7.1.5 never measured: *"slightly banked is not
doing much different from level, as far as which rows need redrawn, and yet it
is getting bank's full massive slowdown."* `slightbank` is that view built as
a profile - 33 m over the Champ de Mars (the panel's ALT 00108 is FEET), the
tower filling the view, 5 degrees of roll HELD - and the answer is four
findings, of which the last two decide it. **SPEC.md 88.3.1.3.4 is the
measurement; this is what to do about it, and the answer is nothing.**

**1. There is no bank MODE - the cost is exactly the rows the horizon splits.**
`cs_skyground` over a poked roll sweep is linear and in nothing else: **8.19 ms
+ 0.306 ms a split row**, every one of eight points within 1.3 ms, and 0.306 ms
is 1,462 cycles - 88.3.1.2's split-row fill to 3%. 5 degrees splits 23 of 112
rows and costs 15.89 ms against level's 8.73. **It is not getting bank's full
slowdown**: 45 degrees splits all 112 and costs 41.79. What 5 degrees costs
this frame is ~6 ms of **185.9**, which is **3.9%** - and at slight bank the
frame is `cs_scene` at **72.1%**, not the horizon at 8.9%. The premise is worth
correcting before anything is built on it.

**2. The cache's population at slight bank is ZERO.** A split row is skippable
when the crossing byte has not moved and nothing drew on the row - and last
frame's span pair answers both, being the crossing's byte plus or minus one
exactly when the row is clean. Measured off the set `cs_blit` is about to
walk: **0 object-free rows at 2, 5 and 8 degrees**, 11-13% at 12-20, 0 again
from 30. So **an angle gate would be gating an empty cache**, and the payout
such a gate could harvest is at MEDIUM bank - the opposite end from the ask.

The angle is the wrong gate anyway. `cs_matrix`'s second column is
`(-sr.cp, cr.cp, sp)`, so the horizon is a function of **roll and pitch
alone** - heading and position do not enter it, and a steady banked turn holds
it pixel-still. The exact and free gate is *"did roll or pitch change since
last frame"*: two words compared once a frame, nothing spent on the frames it
fails, every angle covered including level.

**3. ONE object's box is the wall, it is not the tower, and it is 88.3.2.1's
own object.** Every `cs_markrows` call of one 5-degree frame, traced with its
rows and byte range (the view is 112 rows of 50 bytes):

| mark | rows | of the view | bytes | of the width | the object |
|---|---|---|---|---|---|
| **#0** | **50-72 (23)** | **21%** | **15-61 (47)** | **94%** | `CSM_FLAT` nv3 **nf0 ne2** `CSO_ROAD` |
| #1 | 51-60 (10) | 9% | 35-51 (17) | 34% | `CSM_STACK` nv2 nf5 |
| #2 | 41-59 (19) | 17% | 47-50 (4) | **8%** | `CSM_STACK` nv3 nf8 - **the tower** |

Three calls make the whole frame and the band lies inside #0 entirely. **#2 is
the tower** - the tallest thing in the world, filling the view in the
screenshot that prompted this, marking four bytes of fifty. And #0 is
`cs_m_axis`, **88.3.2.1's axis road**: three vertices, no faces, two edges, the
same object and the same wall at 5 degrees instead of 45.

**That closes the door 88.3.1.3.2 left open.** Its unblocking change was to
mark from `cs_poly`'s per-row `cs_xl`/`cs_xr`, *"a compare-and-store on a pair
the loop already holds"* - and **a polyline has no such loop**. `nf0` is the
whole answer: what remains is 88.3.2.1's banded PASS, built, measured at
**+2.9 ms**, refused.

**4. And it is the INK, not the box.** A tighter mark can only unblock a row
the box covers and the ink misses, so the ceiling of every marking change is
the band with the road NOT DRAWN AT ALL. Poking its `CSO_RANGE` to zero so the
cull drops it measures exactly that, both arms in one session (7.1.11):

| held roll | band | free, road drawn | free, road **DROPPED** | mean span |
|---|---|---|---|---|
| 5 deg | 23 | **0.0** | **2.8** | 36.5 -> 12.5 |
| 12 deg | 55 | 7.4 | 10.6 | 36.7 -> 14.9 |
| 20 deg | 92 | 13.3 | 23.9 | 38.0 -> 12.8 |

**Deleting the blocking object outright buys 2.8 rows of 23 at 5 degrees** -
1.1 ms - with its mark's span down by two thirds. The other twenty are blocked
by INK. **The horizon's band is where distant scenery projects**, so the rows a
shallow horizon splits are the rows objects draw on; that is geometry and no
marking scheme touches it. It is the one sentence under three separate
refusals of the same row - 7.1.8's narrow fill, 88.3.2.1's banded marks and
this cache all lose because **a split row is an OCCUPIED row**.

##### What it is worth

On this session's two unit costs - 1,462 cycles for the fill a cached row
skips, ~390 more for the `cs_blit` row that then finds an empty span (88.3.6's
518 against 129) - against ~35 cycles a band row for the width test behind the
once-a-frame attitude gate:

| held roll | band | free | the cache ALONE | ceiling with the road GONE |
|---|---|---|---|---|
| 5 deg | 23 | 0.0 | **-0.17 ms** | +1.0 |
| 12 deg | 55 | 7.4 | **+2.5** | +3.7 |
| 20 deg | 92 | 13.3 | **+4.5** | +8.6 |
| 45 deg | 112 | 0 | -0.82 | - |

Break-even is 1.9% of band rows. **88.3.2.1 refused finer marking against the
wrong consumer** and that is still worth writing down: its arithmetic is *a row
costs ~50 cycles to MARK and ~4.5 a byte to CARRY*, so a tighter mark must save
eleven bytes on every row it touches, and banding the axis road saved 10.5. A
row unblocked for the CACHE is worth 1,852 cycles, not 4.5 a byte, which moves
the break-even to one row in thirty. But the DROPPED column caps what any mark
can deliver, 88.3.2.1 measured the pass at +2.9 ms, and the pair is therefore
net negative.

##### 7.1.12.1 …and then the CONTROL was taken, and it moved the answer

Everything above prices `cs_skyground` against ITSELF at two angles, which is
not a frame. **SPEC.md 88.3.1.3.5 takes the real control** - `--roll` on the
same scene from the same place - and three things change:

1. **A 5 degree bank costs the FRAME 34.2 ms**, 151.8 -> 186.0 (6.59 -> 5.38
   fps), and the horizon is **27% of it**. `cs_blit` is 32% and `cs_scene`
   35%; the vertex pipeline, which does the rotating, is **0**.
2. **"Slightly banked" was 12 degrees, not 5.** The horizon's screen slope is
   tan(roll) x scly/sclx, so the field's photograph gives its own bank: 54
   rows over 398 px is 11.9 degrees, and that crosses **55 of 112 rows**.
3. **One object's mark goes from 2 rows to 52.** The Seine is a 400-px-wide
   ribbon two rows thick; rotated 12 degrees its INK is still two rows thick
   and its BOX is 55 rows tall. Level, 94 of 112 rows are carried by nothing
   at all; banked, 52.

So the field's premise - *the same rows change* - is CORRECT, and what grows
is the bounding boxes rather than the ink.

##### Recommendation - the cache is not the lever; the BOX is

1. **Do not build the cache**, and do not gate it by angle. It attacks 27% of
   what a bank costs and finds 0 of 23 rows at 5 degrees and 7 of 55 at 12.
   The angle is the wrong gate anyway: the horizon is `(-sr.cp, cr.cp, sp)`,
   a function of roll and pitch ALONE, so *"did roll or pitch change"* is
   exact, free, and covers every angle including level.
2. **Re-measure the MARK at slight bank before inheriting 88.3.2.1's
   refusal.** That was one object at 45 degrees saving 10.5 bytes a row
   against an 11-byte break-even, priced against `cs_blit` alone. Here the
   same object is 2 rows -> 52, which is a case 26x more extreme, and the
   ceiling is no longer just blit bytes: 88.3.1.1.2's narrow fill "pays
   exactly when the union is under 16 bytes of the 50" and today's union is
   31 **because** the mark is a box. Tighten the mark and that refusal flips
   with it.
3. **Neither half is worth building alone and the pair has never been
   measured.** That is the one experiment this round leaves open.

#### 7.1.13 THE FIND - the blit carries 21x what changed, and it is worst LEVEL

7.1.12 chased a cache and the check kept saying "the mark is a box". The field
put it the other way round - *we are drawing no pixels and yet half the scene
is marked dirty* - and that turns out to be measurable exactly, with no model
at all. **SPEC.md 88.3.2.2 is the measurement.**

At the `cs_blit` call site the shadow holds the NEW frame and **the card still
holds the OLD one**. Differencing them across the view is the TRUE dirty set;
`cs_blit`'s own rule says what it will carry. Six settled frames a point:

| held roll | bytes CARRIED | bytes that DIFFER | waste |
|---|---|---|---|
| 0 deg - LEVEL | 267 | **11** | **24.2x** |
| 5 deg | 1,184 | **44** | **27.0x** |
| 12 deg - the field's | 2,603 | **125** | **20.9x** |

**Level is the worst ratio of the three**, which is the part that reframes
everything above: the looseness is not a bank defect, it is there all the time
and a bank only inflates the boxes.

**It is `cs_seg`, and it is a rectangle around a DIAGONAL.** After clipping, a
segment marks min/max of its two ENDS in x and y and hands that box to
`cs_markacc`. A line 400 px wide and 55 rows tall marks 55 x 50 bytes where
its ink is one pixel a row.

**The object IS drawn, and it is 219 pixels.** The field's reading was that it
bounds half the view and is then culled to nothing; the truth is one step
short of that. Dropping it (`CSO_RANGE` = 0) and differencing the glass:
**310 pixels on 2 rows level, 219 pixels on 47 rows at 12** - `CSI_MARK` is
solid white on Hercules, so it is a **one-pixel-per-row hairline** corner to
corner. Against a mark of 52 rows x 46 bytes that is **87x its own ink**, and
it lights FEWER pixels banked than level because the diagonal runs out of the
view. One object accounts for most of the frame's 21x.

##### What to build, in order

1. **Settle the 3 bytes.** The same scoring finds 3 bytes a frame that DIFFER
   and are NOT carried, at 5 and 12 degrees and none level, at the view's
   right edge. Either the host model's word rounding or stale pixels on the
   glass. It is a correctness question and it uses the same instrument
   everything below would be measured on, so it goes first.
2. **A mark that STEPS.** Not 88.3.2.1's banded pass - `cs_markrows`' own row
   loop with the byte pair interpolated, two adds a row on a loop that already
   reads, compares and writes. 88.3.2.1's refusal was 16 bands over one object
   at 45 degrees and its unit costs still stand (~50 cycles a row to mark,
   ~4.5 a byte to carry, so 11 bytes a row to break even); what is new is that
   the mark is **39 bytes a row wide where the ink is under 2**.
3. **Then re-price the two refusals it unlocks** - 7.1.8's narrow fill, whose
   own break-even is "under 16 bytes of the 50" against today's 31, and
   7.1.12's cache, which finds nothing only because the box says every band
   row has ink.

The instrument is `dirty.py`'s shape and it should become a registered row:
carried against differing is a RATCHET, and there is no way to make a mark
looser without it going up.

##### 7.1.13.1 STATE OF THIS THREAD - what is measured, what is open, what to type

Written down so it can be picked up cold. **Nothing has been BUILT** - every
line below is measurement and documents.

**Settled, and quotable:**

1. A 5 degree bank costs the FRAME 34.2 ms in this scene (151.8 -> 186.0,
   6.59 -> 5.38 fps) and `cs_skyground` is **27%** of it; `cs_blit` 32%,
   `cs_scene` 35%, the vertex pipeline 0 (88.3.1.3.5).
2. `cs_skyground` is linear in split rows: **8.19 ms + 0.306 a row**, and
   0.306 ms is 1,462 cycles (88.3.1.3.4).
3. The field's "slightly banked" was **12 degrees**, from its own photograph:
   the horizon's screen slope is tan(roll) x scly/sclx, so 54 rows over 398 px
   is 11.9 - and that crosses 55 of the view's 112 rows.
4. **THE FIND.** Scored against the glass, one position and heading with only
   the bank moving: carried **265 / 1,898 / 2,997 / 4,248** bytes at 0 / 5 /
   12 / 20 degrees while what DIFFERS stays under 140. Pinned so that nothing
   moves at all, 20 degrees carries **4,201 bytes for THREE** (88.3.2.2).
5. It is `cs_seg`: a clipped segment marks min/max of its two ENDS, so a
   DIAGONAL marks its whole rectangle. THE SEINE lights 322-495 pixels at
   every angle and its box goes 2 rows to 88 across the sweep.
6. The row cache is **refused** (7.1.12) and its refusal is not the mark's.

**Open, in the order they should be taken:**

- ~~**A. The stale pixels**~~ - **FIXED**, SPEC.md 88.3.1.1.3, and
  docs/FIELD-NOTES.md 40 is CLOSED. It is the narrow span meeting a SIDE SWAP:
  at the zero crossing `cs_hzl`/`cs_hzr` exchange, the fill lays every band row
  mirrored about a crossing that has barely moved, and the span still claims
  three bytes. The centre row is the only row in the band on both sides of the
  crossing, which is why the field saw exactly one line. 18 bytes of `.text`,
  2 of `.bss`, one compare a frame; `tests/skiesstale.py` is the gate and its
  `--clobber` reads the artefact being born at roll +0.0.
  **The instrument that appeared to find it was WRONG** - a host-side
  reconstruction of `cs_blit`'s union rule, disagreeing with the real one at
  the edges by about the width of the real bug. Assert against the card and
  the shadow, never against a second implementation of the code under test.
- ~~**B. A mark that STEPS**~~ - **BUILT**, SPEC.md 88.3.2.3. `cs_markstep`
  gives a thin diagonal's rows their own intervals; a gate (8 rows, 128
  pixels) keeps everything else on its box, which leaves 88.3.2's tower
  refusal intact. Settled (`--warm 18`): **frame -2.8 at 12 degrees, -3.9 at
  20**, `cs_blit` down 13-15%, level a DEAD HEAT, `turnhold` NEUTRAL where
  88.3.2.1's banded pass was 2.9 ms slower. 243 bytes of `.text`, 3 of
  `.bss`. `cs_mknostep` is the A/B. **RE-MEASURED and it is a TRADE rather
  than a win** (88.3.2.3.7): those numbers are MEANS, and a frame here has a
  long tail, so re-run as the MEDIAN of 39 frames twice per arm it reads
  **-1.62 ms on `slightbank`, -0.03 on `turnhold` at 12 and +1.13 at 45** -
  each pair of runs repeating to 0.03 ms. It pays where the BLIT is the
  expensive half and loses where the MARKING is, `cs_markstep`'s row being
  39 bus bytes against `cs_markrows`' 27.
- **B2. The two things B left on the table**, both measured on the same 12
  degree frame. **The SLOP is now the dominant term in a stepped mark** - a
  row's own interval is 1-2 bytes and the slop is 6, and the ladder says why
  it cannot just be lowered (0 slop reads 232 stale rows, 1 reads 22, 2 reads
  10, 3 reads 0): rounding the step to NEAREST rather than truncating halves
  the drift it covers. And **the widest marks left are not segments** -
  `MONTMARTRE` is a BOX of 18 rows x 16 bytes and `LES INVALIDES` 18 x 4,
  both `CSM_STACK` buildings whose FILLED polygons still box-mark through
  `cs_markacc`. `cs_poly`'s row loop already holds the exact per-row bounds
  it fills between, which is 88.3.1.3.2's original proposal - B is the same
  idea for the segment path, and the polygon path is untouched.
- ~~**C. Re-price what B unlocks**~~ - **MEASURED**, SPEC.md 88.3.2.3.3, one
  fresh guest per point. **The narrow fill flips and the cache does not.** The
  union is **15.4 / 12.9 / 11.1 bytes at 5 / 12 / 20 degrees** stepped against
  35-39 boxed, and 7.1.8's break-even is 16 - but on its own unit costs (~340
  cycles a row to obtain the union, ~10 saved a byte not laid) that is only
  **+0.4 ms at 12 degrees and +0.9 at 20**, against +109 bytes. The cache's
  population is IDENTICAL in both arms (0 / 8 / 17.8 rows), because a stepped
  mark makes a row's span narrower and never makes a row UNMARKED - a diagonal
  crosses every row of its own extent. 7.1.12's refusal stands.
- **C2. The narrow fill was BUILT and is NOT CORRECT** - 7.1.14 below is the
  whole attempt, code included, so the next go starts from there.
- **D. WHERE THE FRAME ACTUALLY IS NOW.** At 12 degrees it is `cs_scene` 69%,
  `cs_skyground` 11%, `cs_blit` 10%, and a split row's 1,460 cycles are only
  ~350 pixels. Marking is no longer the lever: `cs_faces` and `cs_edges` are.
  7.5 and 7.6 are where those were taken apart.

**The instruments, all host-side and none registered yet** (they live in a
scratch directory; 7.1.13 says the one worth registering):

| what it answers | shape |
|---|---|
| carried vs actually-changed, per angle | diff the shadow against the CARD at the `cs_blit` call site - the card still holds the old frame |
| which object marks what, named | breakpoint `cs_markrows` with `regs`, read SI/DI and `cs_mklo`/`cs_mkhi`, name it through `[cs_obj] + CSO_NAME` |
| what ONE object actually lights | poke its `CSO_RANGE` to 0 so the cull refuses it, re-render, difference the glass - and RESTORE it between arms, or every later arm is a dropped arm |
| the frame, level vs banked | `tests/skiesprof.py --roll N`, same profile |

**Four traps this thread already paid for:** a stage compared with ITSELF at
two angles is not a control (it read 7 ms where the frame moved 34); a probe
that pokes the POSITION every frame freezes the scene, so "differ" collapses
to ~3 and the waste ratio divides by nothing; `cs_devoff` is a TABLE, not
a pointer - reading it as one puts every device row at the wrong offset and
reports ~1,300 missed bytes a frame that are not there; and **a profile TELEPORTS
the aeroplane onto a guest that has already flown**, which is not fixable by
settling: the arm inherits `cs_rowkind`, the cull's `CSO_SKIP` and the SHADOW
ITSELF, and nothing repaints a row nothing marks. That put blocks on the
compare panel no object had marked, and made every angle after the first read
its predecessor's world. The same build at 12 degrees read 781, 1,509 and
2,696 carried bytes depending only on which arm it was. **ONE FRESH GUEST PER
ANGLE** (`panel.py`) reads 797 and holds. `tests/skiesprof.py` was never
affected - it launches its own machine per invocation - which is why its frame
numbers stood while three versions of the carried column did not.

#### 7.1.14 THE NARROW FILL - built, wrong, reverted, and written down whole

SPEC.md 88.3.1.1.4 is the summary. This is everything else, kept because the
next attempt should not have to re-derive any of it.

##### Why it was worth trying again

88.3.1.1.2 refused it on a quantity that has since moved. It "pays exactly
when the union is under 16 BYTES of the 50" and the union was 31; with
88.3.2.3's stepping mark it is **15.4 / 12.9 / 11.1 at 5 / 12 / 20 degrees**
(88.3.2.3.3), so the break-even is met at every angle that has a band.

##### What it costs and what it should save

On 88.3.1.1.2's own two unit costs - ~340 cycles a row to obtain the union,
~10 saved a byte not laid - a union of 12.9 is **+31 cycles a row at 12
degrees and +49 at 20**, so **~0.4 ms and ~0.9 ms**. The 340 was measured
BEFORE 88.3.1.3.1 fused the band loop, and the version below is cheaper than
that (the row's index is already in SI), so treat 0.4-0.9 as a floor rather
than an estimate. 7.1.8 measured the old one at **+109 bytes**.

##### THE GATE IS NOT `tests/skiesstale.py`

An under-fill leaves the SHADOW wrong and the card faithfully matches it, so
card-equals-shadow passes. The gate is a **pixel identity A/B**: the same
profile, the same flight, one `cs_nonarrow` poke apart, compared frame for
frame off `m.vram("herc")`. Do not register it as a soak row until the change
exists - with the fill reverted both arms are identical and it would be a
green row that tests nothing (docs/WRITING-TESTS.md 1).

##### And the measurement it needs, which was never run

**Every profile, both arms** - the cost is FIXED and the saving PROPORTIONAL,
so a busy scene is where it loses, and 7.1.8's own numbers say so: turnhold
**+7.0 ms**, bank +2.5, sparse **-6.3**. One fresh guest per point
(88.3.2.3.2). None of that has been done, because two arms that draw
different pictures cannot have their times compared.

##### Three defects found, and the two that are already fixed below

1. **`add di, [cs_fbu]` adds the PACKED PAIR as a word.** `cs_fbu` is
   `(last << 8) | first`, so the row started 276 bytes along - and the picture
   was STILL NEARLY RIGHT, surviving a fourteen-frame pixel A/B at four
   differing pixels. A displacement that large should be obvious and is not,
   which is the trap worth remembering.
2. **`sub di, [cs_wb0]` reads a BYTE as a word**, and `cs_wb0`'s neighbour is
   `cs_wbn`. Both of these want a byte register and an explicit `xor ah, ah`.
3. **The residual, unfixed**: 6 frames in 14 differ by 1-2 pixels, every one
   at the crossing's own BLEND byte, which the narrow arm is missing where the
   whole-row arm has it. Bisecting `.fbw` back to the whole row does **not**
   move it, so it is in the SPLIT arm's three runs.

**Ruled out for the residual**, each checked: the union itself (the differing
byte is well inside it), `cs_fullspan`'s value (it is `(wb0, wb0+wbn-1)`), the
row phase, the pixel mask (taken before the shift, unchanged), and the run
counts as written - left is `byte - first`, right is `last - byte`, and both
land where the arithmetic says.

**The next diagnostic, which was not run**: instrument the split arm rather
than its output. Breakpoint each `FILLRUN` and the blend `stosb` and log DI
and CX per row for one differing row. Three passes of reading the source did
not find it; one run of that would.

##### The code, as it stood when it was reverted

`cs_fbu` (ZWORD, the packed union), `cs_fbdlt` (ZWORD, this frame's span set
to last frame's) and `cs_nonarrow` (ZBYTE, the A/B) go in `skies.asm`'s bss.
The comments below were written during the debugging and mention the defects
they were found by:

```
    --- apps/skies/csraster.inc	2026-09-09 23:18:14.294704907 +0000
    +++ /tmp/claude-0/-home-user-os8088/46586853-8656-50bd-85d0-a528956ae697/scratchpad/cs.narrow	2026-09-09 23:15:35.895259506 +0000
    @@ -1257,6 +1257,10 @@
         mov al, 0xFF
     .hk3:
         mov [cs_hzsplit], al
    +    mov ax, [cs_spprv]              ; ...and the step from a row's pair in THIS
    +    sub ax, [cs_spcur]              ; frame's set to the same row's in last
    +    mov [cs_fbdlt], ax              ; frame's, so the fill's union is one
    +                                    ; `add` and not a second index (88.3.1.1.4)
         push cx
         push bx
         push di
    @@ -1343,10 +1347,40 @@
         mov ax, [cs_wx1]                ; untouched inside the band: the crossing
         inc ax                          ; is off the right, so the row is the
     .fb0:                               ; left side's
    +    ; --- THE ROW'S UNION (SPEC.md 88.3.1.1.4) -----------------------------
    +    ; The row is laid over union(this frame's band mark, last frame's whole
    +    ; span) instead of its whole width. That is exactly cs_blit's own rule
    +    ; one stage earlier and it rests on the same argument: outside the union
    +    ; the row already holds what this frame would lay. The two places that
    +    ; argument fails both hand the row cs_fullspan first - a kind change
    +    ; (cs_hzrows' .kind arm) and a SIDE SWAP (88.3.1.1.3) - so the union is
    +    ; the view there and the fill is the whole row again.
         mov bx, si
    -    sub bx, cs_xl + 2               ; BX = the row's phase, doubled - SI is
    -    and bx, 6                       ; the row counter here, and lodsw has
    -    mov dx, [cs_hzpat4 + bx]        ; already stepped it
    +    sub bx, cs_xl + 2               ; BX = the row, doubled
    +    mov bp, bx
    +    add bx, [cs_spcur]
    +    mov dx, [bx]                    ; DL/DH = this frame's - never empty, the
    +    add bx, [cs_fbdlt]              ; span pass has just written it
    +    mov cx, [bx]                    ; CL/CH = last frame's
    +    cmp cl, 0xFF
    +    je .fbu2                        ; last frame drew nothing on the row
    +    cmp cl, dl
    +    jae .fbu1
    +    mov dl, cl
    +.fbu1:
    +    cmp ch, dh
    +    jbe .fbu2
    +    mov dh, ch
    +.fbu2:
    +    cmp byte [cs_nonarrow], 0       ; the A/B: the whole view, which is what
    +    je .fbu3                        ; shipped before 88.3.1.1.4
    +    mov dx, [cs_fullspan]
    +.fbu3:
    +    mov [cs_fbu], dx                ; DL = the first byte, DH = the last
    +    mov bx, bp
    +    and bx, 6                       ; BX = the row's phase, doubled
    +    mov dx, [cs_hzpat4 + bx]        ; (SI is the row counter here, and lodsw
    +                                    ;  has already stepped it)
         cmp ax, [cs_wx0]
         jg .fb1
         mov dl, dh                      ; wholly the right pattern
    @@ -1362,10 +1396,16 @@
         shr ax, cl                      ; AX = the crossing's byte
         mov bp, ax
         mov cx, ax
    -    sub cx, [cs_wb0]                ; CX = the bytes wholly left of it
    -    mov al, dl
    -    mov ah, dl
    -    push di
    +    sub cl, [cs_fbu]                ; CX = the bytes of the union left of it
    +    xor ch, ch
    +    mov ax, bp                      ; ...and the union's first byte is where
    +    sub al, [cs_wb0]                ; the row starts now. BOTH OF THESE ARE
    +    xor ah, ah                      ; BYTE reads: cs_fbu is a PAIR in one word
    +    sub ax, cx                      ; and cs_wb0's neighbour is cs_wbn, so a
    +    push di                         ; word `add di, [cs_fbu]` put the row 276
    +    add di, ax                      ; bytes along and the picture was still
    +    mov al, dl                      ; nearly right, which is how it survived
    +    mov ah, dl                      ; a fourteen-frame pixel A/B at 4 pixels
         FILLRUN                         ; the left run - DI lands ON the crossing
         mov al, bl                      ; the crossing's byte: the two patterns
         mov ah, al                      ; through the mask and its complement
    @@ -1374,22 +1414,27 @@
         and ah, dl
         or al, ah
         stosb
    -    mov cx, [cs_wb0]                ; the right run: the bytes after it
    -    add cx, [cs_wbn]
    +    mov cl, [cs_fbu+1]              ; the right run: to the union's last byte
    +    xor ch, ch
         sub cx, bp
    -    dec cx
         mov al, dh
         mov ah, dh
         FILLRUN
         pop di
         jmp short .fbn
     .fbw:
    -    mov al, dl                      ; a whole view row of one pattern
    -    mov ah, dl
    -    mov cx, [cs_wbn]
    -    shr cx, 1
    +    mov cl, [cs_fbu+1]              ; the union, all one pattern
    +    sub cl, [cs_fbu]
    +    xor ch, ch
    +    inc cx
    +    mov al, [cs_fbu]
    +    sub al, [cs_wb0]
    +    xor ah, ah
         push di
    -    rep stosw
    +    add di, ax
    +    mov al, dl
    +    mov ah, dl
    +    FILLRUN
         pop di
     .fbn:
         add di, 80
```

## 7.4 WHERE cs_scene's 169 ms GOES

Tier 2 over twelve flown frames of `turnhold`, with tier 3's sub-splits:

```
cs_scene                          169.1 ms
├─ cull  cs_consider x47           19.1   11%
├─ occlude                          0.1
└─ drawpass x2                    148.8   88%
   └─ drawobj x16.1               146.9
      ├─ faces x8.8                 67.9   40%
      │  ├─ poly x11.5              52.1
      │  │  ├─ THE FILL (excl)      39.8   24%   <- biggest single item
      │  │  └─ edge x21             12.4    7%
      │  ├─ fclip x1.0               6.1    4%
      │  ├─ axcull x16.5             1.5
      │  └─ its own                  7.3
      ├─ project x8.8               15.6    9%
      ├─ scale x16.1                14.3    8%
      ├─ edges x6.8                 12.8    8%   (seg x6.5 = 6.8, own 5.3)
      ├─ flatverts x5.8             11.7    7%
      ├─ markrows x5.4               9.5    6%
      ├─ stackverts x3.0             4.9
      ├─ boxlod / wireclr / sizepx   6.1
      └─ its own                     3.8
```

Per call, in cycles - which is where the surprises are:

| | ms | calls | cycles each |
|---|---|---|---|
| the polygon fill | 39.8 | 11.5 | **16,500** |
| `cs_flatverts` | 11.7 | 5.8 | **9,640** |
| `cs_project` | 15.6 | 8.8 | 8,450 |
| `cs_markrows` | 9.5 | 5.4 | 8,410 |
| `cs_stackverts` | 4.9 | 3.0 | 7,840 |
| `cs_fclip` | 6.1 | **1.0** | **29,000** |
| `cs_scale` | 14.3 | 16.1 | 4,246 |
| `cs_edge` | 12.4 | 21 | 2,813 |
| `cs_consider` | 19.1 | 47 | 1,894 |
| `cs_axcull` | 1.5 | 16.5 | 434 |

#### 7.1.15 THE SHORT-RUN BODY, COSTED - and the outline story corrected

The field asked why a slight bank costs so much, was given an account in
7.1.14's terms, and then asked for the next idea to be COSTED before it was
written. This is that costing. It changes the account as well as pricing the
idea, and the correction is the more valuable half.

##### 7.1.15.1 Four instrument repairs, because none of this could be measured

Nothing in `all` builds the diag trees, so nothing in `all` can see them rot,
and all four of these were found by trying to take a number:

1. **`make skiesprobe` did not ASSEMBLE.** `skies.asm` lays its bss under a
   fixed overlay address and writes the GAP as its own subtraction precisely
   so that outgrowing it is a refusal rather than a silent overlap - and the
   -DCSPROBE arm had outgrown it by **331 bytes**. `tools/csworlds.py` takes
   `--vocab-at` now and the Makefile passes it for any tree with a
   `CSDIAGDEF`, so a diag build moves its overlay up and pays a bigger heap
   claim it does not have to fit on a floppy. **The shipped `skies.bin` is
   byte-identical either side** (`66aead79...`), which is the whole point of
   the -DCSPROBE design and is checked rather than asserted.
2. **`tests/skiescount.py` re-assembled against the SHIPPED overlay address.**
   It makes two nasm calls; `probemap()` passes the probe tree's own
   `cswidx.inc` under a comment saying exactly why, and the tick-wait one
   passed `build/`'s. One of the two had learned the lesson. It got away with
   it until repair 1's overflow made the difference matter.
3. **A PAUSED census is not the FLYING one, and the counts do not transfer.**
   This is the one to remember. `skiescount` pins the aeroplane and stops the
   world, which is right for an A/B where both arms must draw the IDENTICAL
   picture and wrong for a population count: `slightbank` paused at 12 degrees
   puts **2.2** segments a frame into a line body, and flying puts **15** into
   `cs_seg`. Costing off the paused number would have priced a renderer nobody
   runs. `--fly` teleports and then lets go, re-pinning the bank at each
   frame's `cs_render` so 88.7.5's easing cannot roll it out over the census.
4. **Nothing counted the slice's ROWS.** `cs_dbg_wsl` counted sliced SEGMENTS,
   and a segment is not the unit that is paid. `cs_dbg_wslrow`/`cs_dbg_wslpx`
   are two more probe-only words and they are what turned this from an
   argument into a table.

##### 7.1.15.2 The census - the same 670 pixels, and 4 runs against 94

`slightbank`, flying, 16 counted frames, one fresh guest an angle:

| roll | sliced px | sliced RUNS | px a run | walked px | steep | vertical |
|---|---|---|---|---|---|---|
| 0 | 670.1 | **4.0** | 167.5 | 0.0 | 0 | 0 |
| 5 | 670.0 | **43.0** | 15.6 | 0.0 | 0 | 0 |
| 12 | 652.0 | **94.2** | 6.9 | 3.1 | 0 | 0 |
| 20 | 1.1 | 0.2 | 5.7 | **635.7** | 0 | 0 |

**The same ~670 pixels are drawn at every bank.** What the tilt changes is the
number of RUNS they are laid in - 4, then 43, then 94 - because a run is one
row's worth of contiguous bytes and a flat line is one run for its whole
length. That is 7.1.13's "priced by the row, not the pixel" stated exactly, in
the one place where the pixel count is held constant by construction.

##### 7.1.15.3 A sliced run costs 785 cycles, measured twice and not quoted

SPEC.md 85.3.6 priced Tank's row at "some 800 cycles" by counting its bytes
and its memory accesses. This scene gives the same number by SUBTRACTION,
which is a different method and a different program:

- roll 0 -> 12: **+90.2 runs** for **+14.83 ms** of `edges` = **785 cycles a
  run**
- roll 0 -> 5: **+39.0 runs** for **+6.26 ms** = **766 cycles a run**

Both arms hold the pixel count, the object count and the `cs_seg` call count
fixed, so the run is the only term moving. What is left over is `edges`'
FIXED part - `cs_seg` entered 15 times, Cohen-Sutherland clipping most of
them away, and the marking - and it comes to **~7.0 ms at every angle**,
which is why `edges` reads 7.70 at level where its four runs are worth 0.66.

`cs_slice_herc`'s row body is **129 bytes** measured off the listing (45 in
`.row`, 20 in `.multi`, 12 in `.step`, the rest one-time setup), against
85.3.6's "about 130". Two independent methods, two sources, one number.

##### 7.1.15.4 THE CORRECTION: there are no steep or vertical lines at all

The account this costing was asked for said that a bank takes every upright
edge off the exactly-vertical body and every flat one off the slice, and that
both halves were the story. **The first half has no customers in this scene at
any angle** - the steep and vertical columns above are 0.0 throughout, because
these buildings are drawn as FILLED FACES and not as outlines, so `cs_edges`
never sees their uprights. The claim was reasoned from the renderer's shape
and never checked against a count.

What survives, and is now exact rather than argued, is the flat half: 670
pixels in 4 runs becomes 670 pixels in 94. And the fill's own version of it
survives too, from the same runs - the polygon edge tracer's rows go **171.2
-> 355.0 -> 440.6** at 0, 12 and 20 degrees, which is the wide-flat-polygon
argument measured rather than asserted.

##### 7.1.15.5 The costing, and the verdict

**The customers are the SLICED runs and not the walked pixels.** At 12 degrees
the per-pixel arm draws **3.1 pixels a frame**, so the body as first proposed -
a cheaper laydown BELOW the slice's six-pixel threshold - has no work to do at
the angle the field flies. It only has customers at 20 degrees, and there the
whole per-pixel arm is 635.7 pixels, which even eliminated entirely is ~6% of
the frame.

Re-aimed at the runs the slice IS taking, the arithmetic is:

- **The budget.** 94.2 runs at 785 cycles = **15.8 ms of a 213.6 ms frame,
  7.4%**. That is the whole of what the idea plays for at 12 degrees.
- **The body.** Written and assembled to check the size rather than estimated:
  a run of q <= 8 from bit b spans at most two bytes, so one word off a 64-entry
  table and TWO UNCONDITIONAL ORs draw it - the second is a no-op write when
  the table's high byte is zero, which needs no branch. **37 bytes**, against
  the 65 of `.row` + `.multi` it replaces, and with 3 memory accesses against
  85.3.6's sixteen.
- **The saving.** The current row runs at 785 cycles for 77 bytes = 10.2
  cycles a byte, well above the 4.34 fetch floor, which is the `rep` setup and
  the memory accesses. The short body should sit nearer the floor: 49 bytes
  (37 + `.step`'s 12) x 4.34 = 213, plus three accesses ~60 = **275 cycles**,
  and **500** if it runs as memory-bound as the body it replaces. So
  **285-510 cycles a run, 5.6-10.1 ms, 2.6-4.7% of the frame at 12 degrees.**
  At 5 degrees 2.1%; at level **zero**, its four runs being 167 pixels long
  and out of the body's reach by construction.
- **The price.** ~37 bytes of body + 128 bytes of table + a dispatch ~= **170
  bytes** of package image, and a long run must not get slower for it.

**Verdict at costing time was "not now"** - 2.6-4.7% for 170 bytes with an
error bar a factor of two wide. **The field took it anyway, and it is BUILT
and MEASURED** (SPEC.md 88.4.6.2): **-1.8% on `slightbank`, 0.0% on every
other profile, pixel-identical at 113 of 113 points**, for 212 bytes. Three
things came out different from the costing and all three are worth keeping:

- **The body is DEARER than predicted, not cheaper.** 594 cycles a run
  against the general body's 785 - 191 saved, 24% - where the estimate said
  275-500. The reasoning was that 49 bytes with three memory accesses would
  sit nearer the fetch floor than 129 bytes with sixteen; it sits FURTHER
  from it (12.1 cycles a byte against 10.2), because a word table read and
  two read-modify-write ORs inside 37 bytes is a denser mix of memory work
  per byte of code. **Fetch is the floor, not the price** - the rule this
  file has now got wrong in both directions.
- **The band is two values of q**, so exactly one profile in eight moves.
  That is not a disappointment to hide: 212 bytes that pay 1.8% in the
  attitude the field flies and cost NOTHING in the other seven is the trade
  as offered, and `turnhold` reading equal to the hundredth in both arms is
  what says the gate is free when it does not fire.
- **Proving identity was harder than building the body** and is its own
  record (88.4.6.2.1): three harness designs each made a correct renderer
  look broken, and the one that works is A/B/A inside one guest with A == A
  checked per point.

The census's other half still stands and is still unspent: the pixels are
constant and the ROWS are 23x, so the remaining lever is the row COUNT and
not the row's price.

##### 7.1.15.7 …and the FILL was asked the same question, and answered NO

7.1.15.6 set this as the next thing to ask, on the arithmetic that `cs_faces`
is 52.4 ms at 12 degrees against `cs_edges`' 22.5, so a body that helped
there would have a customer three times the size. **The rows ARE short - 78%
of them under two bytes at 12 degrees and 94% under eight even at LEVEL - and
the answer is still no** (SPEC.md 88.4.6.4).

Two probe counters and one census answered it, exactly as predicted; what was
NOT predicted is the reason. The line's slice paid because its row body was
built for a LONG run and had never been revisited. **The fill's had been -
three times.** SPEC.md 88.4.5.2 took the one-byte row out of the loop's span,
88.4.5.4 moved the clamp question to the caller, and 88.4.5.5 made each end a
single word-table load. Assembled and compared rather than argued: today's
two-byte row is **88 bytes** and the identical mask-pair trick is **85**. The
index arithmetic a `(bit, run)` table needs costs precisely what the two end
tables it would replace already cost.

**That is the transferable finding**, and it is the opposite of the one the
line taught: an optimisation is worth trying where the code has NOT already
been cut for the case, and the census that proves the case exists says
nothing about whether the code has. Both halves have to be checked, and the
cheap half is the second one - it is one `nasm` invocation.

The by-product is the next lead and is measured: **the tracer costs about
what the filler does.** A fill row is 478 cycles (least squares over four
angles against `cs_poly`'s exclusive time, with ~2,900 cycles a polygon
beside it), and `cs_edge` is 203 cycles a traced row over 355.0 of them at 12
degrees against the filler's 152.5. Per row that gets INK the machinery is
~950 cycles to lay 8.8 pixels, and 19% of traced rows are a shared edge
traced twice - 25% at 20 degrees. SPEC.md 88.4.2.1 refused a runtime dedup on
a measurement; this is what refusing it costs.

##### 7.1.15.6 What to ask next, with the instrument now in place

The same question has never been asked of the FILL, which is three times the
stage: `cs_polyrows_herc`'s run is already inline (SPEC.md 88.4.6) but nothing
counts its runs or their lengths, and `faces` is 52.71 ms at 12 degrees
against `edges`' 22.53. If a tilted polygon's rows are as short as a tilted
line's runs, the same 37-byte laydown has a customer three times the size -
and if they are wide, the fill is already at its floor and the answer is the
row COUNT again. Two counters and one census would say which, and the
counters are the same two.

### 7.4.1 The CULL, taken apart - and it is not fat

`cs_consider` bracketed at its own call site, 323 calls over six frames, split
by whether `cs_nvisn` moved (the object was FILED) or not:

| per frame | calls | cycles each | ms |
|---|---|---|---|
| cheap rejects - the tier ladder, the skip counter, Manhattan | ~21 | ~390 | 1.7 |
| **cone rejects** | ~14 | ~2,080 | **6.1** |
| **filed** | ~15 | **3,255** | **10.3** |

A filed object costs ~1,175 cycles MORE than a rejected one that did the same
work - that is `.file`: the six-word record and an insertion sort whose body is
29 bytes and **~126 cycles a shift**.

The ~2,080 common to every expensive call is the range test and the cone, and
it is **five 8086 multiplies** (one in `cs_range`, four `MUL14` in the cone at
~150 clocks each) plus a fetch floor of ~200 bytes. **It is not fat**: 29
in-range objects a frame each need a rotation to know where they are.

**BUILT (88.5.2.1), +13 bytes:** the Detail rung's SCALE was looked up per
OBJECT - `[cs_setlod]`, a shift, an index into `cs_lodscl`, and a push pair to
borrow BX - for an answer that cannot change inside a frame; it is
`[cs_lodsc]`, read once in `cs_scene`. And at `CSL_MOD`, the rung the
simulator ships on, that scale is **256** - so `range x 256 >> 8` is an
~130-cycle `mul` by one, and the identity is tested for instead. Measured
against a control built from the committed source in the same session:

| turnhold, tier 2 | control | + the hoist |
|---|---|---|
| `cull#1` | 18.75 | **17.80** |
| `cs_scene` | 169.05 | **168.00** |
| `drawobj` / `faces` | 146.81 / 68.34 | 146.82 / 68.34 |

### 7.4.2 REFUSED - the angular skip; BUILT - the sort

**1. The ANGULAR skip: REFUSED (SPEC.md 88.5.2.2).** It was the biggest thing
left in the cull and it measured like it - cone rejects **12.6 -> 7.7 a
frame**, `cs_consider` **17.80 -> 15.46**, the frame **257.6 -> 251.6** for 56
bytes. **It was buying a changed picture**: three landmarks and a road stopped
being drawn.

This row said "the bound is the hard part and must be measured before it is
built", and that was the wrong instruction in two ways. The bound is not
measured, it is DERIVED - `CSP_TURNK` 90 + `CS_RUDDER` 24 = 114 units a tick
is an exact ceiling on the closing rate, where the distribution of `across -
si` says nothing about safety at all. And the hard part was not the bound: it
is that **the cone is not a conservative test**, refusing an object at
`f |along| + r` where a vertex r from the centre needs `f |along| + (1+f) r`.
At f = 1 that is short by a whole radius - 3,739 m for the Paris
peripherique - so the cone throws the road out, the frustum keeps it, and it
stays on screen only because 88.5.1 files an object drawn last frame WITHOUT
a cone test. Re-testing every frame repairs that in one frame. A skip does
not.

**What this cost was three wrong gates, and they are the reusable part:**

| gate | why it says nothing |
|---|---|
| the profiler's `objects 18 -> 18` | that is the WORLD's object count |
| the FILED SET, frame by frame | an object can be filed and then refused by `cs_drawobj`'s frustum - the first comparison found ten differing frames that were all one road drawing no pixels |
| the whole framebuffer | the panel integrates over TICKS and the two builds do not spend them alike, so 29 of 46 frames "differed" on airspeed |
| a turn scripted per FRAME | the bound is per TICK: `sparse` is ~1.8 ticks a frame, so 2.88 deg a frame is 1.5 deg a TICK, 2.5x what the aeroplane can do. **The harness violated the premise, not the code** |

The one that works pins `[cs_last]` as well as the attitude - every frame
advances the tick counter by exactly N and the heading by exactly N x 0.626
deg - and reads the DRAWN set (`CSO_SEEN`) beside a hash of the 3D VIEW's
pixels. Both builds then see the identical world at the identical tick at
frame i whatever they cost to draw, and it is exactly reproducible: **the same
build twice differs in 0 of 93 frames.** That control is what turned a
counter-intuitive result into a second bug - widening the margin made the
picture WORSE, which is impossible, because the sum passes 32,767 and `jle`
is signed.

**2. The insertion sort: BUILT (SPEC.md 88.5.2.3), -3 bytes.** Measured rather
than estimated - a breakpoint on the shift body counts **67 shifts over 15
filed objects a frame, 1.77 ms**, confirming this row's ~2.0. But the
`std`/`movsw` shape it proposed is REFUSED: `movsw` writes ES:DI and ES is the
kernel's in a package, so it needs a push/pop pair around a routine called 15
times a frame - ~0.16 ms back - and leaves DF set on every exit. **It is 0.05
ms better than free.** What shipped needs neither: the source pointer SI was a
register the loop did not need (`[di-4]` and `[di-2]` address the source for
nothing), and the compare's loaded word IS the word the shift stores. 131 ->
116 cycles a shift, 29 -> 26 bytes, and **`cs_consider` 17.80 -> 17.42/17.43
ms** against a control run between the two arms - **0.375 ms**, against 0.21
predicted, because three bytes out of a 29-byte loop relieve the 8088's
prefetch queue for the code around it too.

**3. Seeding the sort from the previous frame's order** is the only thing that
would take the rest of the 1.77 ms - a held bank files the same 15 objects in
nearly the same depth order, so a seeded insertion sort is O(n). It needs an
identity map from object to slot across frames and it fails by drawing the
painter's order WRONG. **Parked**: the sort is 0.7% of the frame.

**4. `cs_fclip` at 29,000 cycles in ONE call a frame** - 4% of the scene spent
clipping the single face that straddles the near plane. Untouched.

### 7.4.3 Where the cull stands, and the one thing to know before touching it

The cull is **17.42 ms**, down from 18.75 when 7.4.1 took it apart, over two
changes totalling **+10 bytes of `.text`**: the Detail scale hoisted out of the per-object
path (88.5.2.1) and the sort's source pointer (88.5.2.3). What is left is the
five multiplies a rotation needs, and 7.4.1's verdict has not changed - **it
is not fat**.

**The thing to know is that `CSO_SEEN` is not an optimisation.** 88.5.1 reads
as one - "it is filed without the cone, which cs_drawobj's frustum repeats
exactly" - and it is really the repair for a cone that refuses objects the
frustum keeps. Any change that stops an object being cone-tested EVERY FRAME
walks into 88.5.2.2, whatever else it is for.

## 7.7 BUILT - the FLIGHT MODEL, which had no tier at all

`cs_step` was 10.8 ms a frame at three calls and had never been opened, because
`skiesprof` had no bracket table below it. Adding one - TIER6, nine rows, the
flight model's own calls - answered it in a single run: **`cs_collide` is 5.92
of the 10.77, 55%**, `cs_move` 1.00, the attitude proc 0.46, everything else
under 0.9 together, and the model's own arithmetic 2.41.

`cs_collide` walks every object in the world three times a frame asking x, then
z, then y - and **the y question is the expensive one**, chasing the object to
its model and the model to its vertex table to find the tallest level's height.
`cs_ctop` holds that word per object now (SPEC.md 88.7.13), so the walk opens
with one compare and 46 of Paris' 47 stop there.

A WORLD-wide maximum was the first idea and the numbers killed it: the Eiffel
Tower's 324 m against profiles that fly at 300, so one object would have kept
the walk alive for the other 46.

`cs_step` **10.77 -> 7.63 ms** (`turnhold`), 10.61 -> 7.63 (`bank`); the frame
4.10 -> 4.15 fps and 4.21 -> 4.27. `climb` costs **+0.13** - on the runway the
aeroplane is below everything, so the first compare never rejects - and that is
the right way round. 256 bytes of bss out of the gap, no claim. The table is
read back off a running machine and checked against the arithmetic it replaces,
47 of 47.

**The method note**: this is the first round here that started with an
instrument rather than a reading, and the instrument was cheaper. Nine rows of
`skiesprof` turned "10.8 ms, never opened" into "one call, 55%, here is which"
before a line of the model was read.

## 7.5 THE POLYGON FILLER, taken apart - and one PARKED question about the algorithm

`cs_scene` is 66% of a banked frame and `cs_poly` is the largest thing in it.
Three changes have landed off one breakdown (SPEC.md 88.4.5.1, 88.4.2.2,
88.4.2.3) - `cs_scene` **174.73 -> 169.32 ms**, the frame **262.2 -> 256.8**,
for **-121 bytes** - and all three were removing work the code already knew was
unnecessary rather than trading space for speed.

Where the remaining time is, `turnhold`, measured:

| | ms a frame | |
|---|---|---|
| the row loop's fill | ~35.5 | `~421 + 101.5 x bytes`, 257 rows a frame at 2.1 bytes |
| **`cs_edge`'s Bresenham stepping** | **~12.0** | `83 + ~83 x rows`, **689 row-stores a frame** |
| `cs_edge`'s setup | 4.60 | of which the `idiv` is ~1.04 |
| the edge loop's per-edge setup | 2.43 | |
| the min/max pass, the box mark, the counter | 2.30 | |

### 7.5.2 BUILT - the row census, and a gate one comparison too loose

A fetch-bound loop only pays for the bytes a row EXECUTES, so the breakdown
above is the wrong unit: a row's ARM is. `turnhold`, 279.5 rows a frame, each
measured `.row` to `.row` with the delta closed at the routine's `ret`:

| the row | a frame | share | cycles |
|---|---|---|---|
| multi, two bytes | 70.7 | 25.3% | 635 |
| clipped multi, two bytes | 70.0 | 25.0% | 763 |
| multi with a middle run | 63.3 | 22.7% | 728 |
| ONE byte | 50.3 | 18.0% | 537 |
| clipped multi with a middle | 21.0 | 7.5% | 828 |
| clipped ONE byte | 4.2 | 1.5% | 658 |

Two bytes or fewer is 43% of every row and an EMPTY row never happens at all.
Two things came out of it, both built (SPEC.md 88.4.5.2, 88.4.5.3):

- **the one-byte arm was inline**, so 80% of rows took a jump to skip it - and
  its fifteen bytes put the loop's span at 138, where the backward `jle .row`
  is out of `rel8` and nasm emits `jnle $+5` / `jmp .row`: five bytes and a
  second taken jump on every row of the program. Out of line the span is 122.
- **the clip gate tested `<=` and `>=`** where `cs_edge` only ever writes an
  `xl`/`xr` INSIDE the box, so a box merely TOUCHING an edge turned the block
  on for nothing. One polygon a frame, 81.6 rows, 32% of every row drawn, not
  one of them clamped at either end.

`cs_poly` 49.09 -> 47.79 ms (`turnhold`), 36.54 -> 35.37 (`bank`), 18.27 ->
17.72 (`climb`); the frame moves by the same and the image is 4 bytes smaller.
569 frames on six pinned profiles are pixel-identical.

### 7.5.3 BUILT - two row loops to free BP

What is left in the loop's fixed cost, per row, after the two above:

| | bytes | why it is there |
|---|---|---|
| `mov si,bx` / `shl si,1` / two array reads | 12 | the row's ends |
| **`or bp,bp` / `jne .clip`** | **4** | the clip gate - and `turnhold` never takes it now |
| `cmp ax,cx` / `jg .nrow` | 4 | an empty row, which never happens |
| the pattern byte | 9 | `bx & 3` into `cs_pat` |
| **the two ends** | **30** | `and si,7`, three `shr`, a mask each |
| the address and the width | 10 | |
| the two masked bytes | 24 | |
| the middle run | 19 | 30% of rows |
| **`cmp bx,[cs_py1]` / `jle`** | **6** | a disp16, because no register is free |

**BP is the contested register.** It carries a per-row boolean that `turnhold`
now never uses, and if it carried `cs_py1` instead the loop's compare would be
`cmp bx, bp` - two bytes rather than four - and the gate would go entirely.
That is **-6 bytes a row, about 26 cycles, ~1.4 ms a frame**, and the price is
that the clip block has to move INLINE into a second copy of the loop, chosen
by `cs_poly` through a second `[cs_rowsproc]`-style vector. Written as a
`%macro` assembled twice it is one source and about **120 bytes of image**;
the clipping copy also gets its clamp reordered so a row that needs neither
end falls through it (measured: 43-100% of clipped rows need neither), which
is two fewer prefetch flushes on the rows that still take it.

**TAKEN, on the owner's decision, and it delivered nearly double the
costing** (SPEC.md 88.4.5.4, PERFORMANCE.md Set 135): `cs_poly` 47.79 ->
44.98 ms in `turnhold`, 35.90 -> 33.48 in `bank`, 17.72 -> 16.72 in `climb`,
and the frame 3.96 -> 4.01 fps. It is the first change in this round that buys
speed with SIZE rather than by removing work: **+155 bytes** of image for the
second expansion, plus six in `cs_poly`, eight in the backend arms and a word
of bss.

The costing was -6 bytes a row and the delivery was **-11**, which is the
opposite of this file's usual error - a prediction off the fetch floor is an
UPPER bound - and it undershot because the byte count it was made against was
the wrong one. Two of the five extra bytes have nothing to do with the split
and are the part worth remembering: twelve instructions sit between `push bx`
and the `mov bl, al` that rebuilds BX, so **BX is dead there** and the pattern
index belongs in it rather than in SI (`and bx, 3` / `mov dl, [cs_pat + bx]`,
-2), and `and bx, 3` then leaves BH zero, retiring the `xor bh, bh` under it
(-2). That one was NOT dead before - a Hercules view is ~300 rows, so the row
index really does reach BH.

Gated on **seven** profiles rather than the usual six: `climb` is the only one
that reaches the whole-view arm or `cs_rect`'s dispatch, so the six alone would
have gated a register reallocation on the arm it did not touch. 653 frames,
pixel-identical.

### 7.5.4 BUILT - a table for the row's two ends, and a refusal that priced nothing

The 30 bytes the two ends cost is the largest single block left, and a word
table indexed by x - `(mask << 8) | (x >> 3)`, exactly the AH:AL the left end
wants and the CH:CL the right one does - reduces it to 20 for **-10 bytes a
row**. It needs **2,560 bytes** (two tables, 640 entries; a view's x cannot
leave [0, 639] because the shadow row is 80 bytes and `cs_vptab`'s Hercules
box is 640 wide, so that is exact rather than generous).

**BUILT** (SPEC.md 88.4.5.5, PERFORMANCE.md Set 136): `cs_poly` 44.90 ->
42.96 ms in `turnhold`, 33.51 -> 31.86 in `bank`, 16.64 -> 16.14 in `climb`;
the frame 4.01 -> 4.04 fps and 4.14 -> 4.17. `cs_endtab` fills both beside
`cs_ktabs`, ~4 ms once a bracket.

**AND THE REFUSAL THIS SECTION USED TO CARRY IS THE THING TO REMEMBER.** It
read: *"it needs 2,560 bytes and the package has ~1,416 of gap below
`CS_VOCAB_AT`; raising it grows the heap claim on every machine that runs the
program."* Every clause was true and the conclusion was wrong, because it
never asked WHERE the size lands - and there were three places, all cheap to
check:

| | |
|---|---|
| the DISK | the bss ships inside the part as a run of zeros and LZ4 is best at that: `skies.o88` is **43,717 bytes in both arms of the A/B, identical to the byte** |
| the CLAIM | 49,216 -> **51,776**, `CS_VOCAB_AT` 0xB400 -> 0xBE00 - which is the real cost, and it is 2.5 KB of a kern_big machine's heap |
| the FLOOR MACHINE | `SKIES` is in `SMALLOMIT_GAMES` and never reaches the 128 KB disks at all, so it pays nothing |

A size refusal is only as good as its account of where the size lands. This
one priced none of the three.

A single 640-byte `x >> 3` table with the masks left alone would have been
only **-2 bytes a row**, and that one really is not worth 640 bytes.

### 7.5.1 PARKED - a fractional DDA instead of exact Bresenham

**The one item left that is worth more than a millisecond is the STEPPING
ALGORITHM, and it is parked because the picture is the thing being spent.**

`cs_edge` steps an exact integer Bresenham: `q = floor(dx/dy)` and a remainder
`r`, and every row carries `x += q; err += r; if err >= dy then x++, err -= dy`.
That is six instructions and ~13.5 bytes a row, on a loop whose cost IS its
byte count (88.4.5.1). A 16.16 fractional DDA is three:

    mov [bx], si            ; x, integer part
    add di, bp              ; fraction += step.frac
    adc si, ax              ; integer += step.int + carry

**~6 bytes a row against ~13.5**, which at the 8088's 4.34-a-byte floor is
~32 cycles a row over 689 rows a frame - **~4.6 ms predicted**, the largest
single item nameable in the scene. The setup gets simpler too: one `div` of a
shifted numerator instead of `idiv` plus the floor correction.

**What it costs is what makes it a question rather than a task.** The user's
own observation is that these lines are *better than any period DOS game's*,
and that is the thing being traded. Two claims should be separated before
anyone acts on this, because only one of them is obviously true:

- **The accumulated drift is probably negligible and is MEASURABLE.** A
  correctly rounded 16.16 step drifts at most `rows x 2^-16` pixels, which
  over the tallest edge in the view (112 rows) is 0.0017 px. On that
  arithmetic the picture should be identical or within one pixel on a
  vanishing fraction of edges - so **the honest first step is to measure the
  pixels, not to argue about them**, with the tick-driven six-profile gate
  that every change in this round has used.
- **Shared edges stay consistent**, which is the failure that would actually
  show: two faces meeting along one edge must produce the SAME x per row or
  the seam cracks or double-draws. Both algorithms are a pure function of the
  two endpoints, so two polygons handed the same endpoints agree either way.
  This is worth stating because it is the risk a reader will assume is fatal.

So the shape of the investigation is: build it, run the pixel gate, and **let
the count decide**. If it is 0 differing pixels the quality question never
arises. If it is not, the trade is the user's and the bar is explicit - *"hard
to give up for less than a big win"* - and ~4.6 ms of a 257 ms frame is 1.8%,
which is probably not that bar on its own.

**Two cautions for whoever picks this up.** A prediction off the fetch floor is
an UPPER bound where the operands are memory (88.4.5.1 predicted 3.6 ms and
delivered 2.5), so 4.6 could be three. And `cs_edge` has three callers, two of
them the rolled horizon's (csraster.inc), so the change is not confined to the
polygon filler.

## 7.6 THE VERTEX PIPELINE, taken apart - and it is MULTIPLY-bound only a third of the way

`cs_scale` + `cs_project` + `cs_flatverts` + `cs_stackverts` is **46 ms of a
254 ms frame** and nothing had opened it. The premise going in was that it is
the one part of the program bound by the 8088's multiply unit rather than by
its prefetch queue - `MUL14` is `imul bx` and three fixups, ~150 clocks against
8 bytes - and that premise is **only a third true**.

### 7.6.1 Every multiply the package executes, counted

211 multiply and divide instructions exist in the image; what matters is how
often each RUNS. Breakpointed all 211, `turnhold`, per frame:

**885 multiplies and divides a frame, ~28.3 ms, 11% of the frame.**

| routine | a frame | ~ms |
|---|---|---|
| `cs_colscale` | 218 | 6.86 |
| `cs_dot` | 167 | 5.26 |
| `cs_consider` | 108 | 3.39 |
| `cs_project0` + `cs_project2` | 110 | 3.70 |
| `cs_edge` | 39 | 1.44 |
| `cs_cxing` | 37 | 1.20 |
| `cs_step` | 40 | 1.20 |
| `cs_faces` | 28 | 0.88 |
| `cs_sizepx` | 29 | 0.85 |

(That is the top of the list and not all of it - `cs_project4` is below the
cut and DOES run, 24.4 vertex projections a frame; see 7.6.4.)

So of the vertex block's 46 ms, **~15.8 ms is multiplies and ~30 ms is the
scaffolding around them** - and scaffolding is the removable kind. That
inverts the reason for opening the block, and it is the finding.

### 7.6.2 `cs_projall`, examined - 1,335 cycles a vertex, and it is SPREAD THIN

The biggest of the four and the least multiply-bound (22%), so it was taken
first. 41 vertices a frame reach the loop; the per-vertex phases:

| | ms a frame | cycles a vertex |
|---|---|---|
| the loads and the near test | 1.02 | 119 |
| **`CS_PROJ` itself** | **7.63** | **885** |
| the stores, the flag, and the box or the side test | 3.06 | 355 |

...and inside `CS_PROJ`, over 56.6 projections a frame:

| | ms a frame | cycles a vertex |
|---|---|---|
| the depth test + **ROW SELECTION** | 2.03 | 171 |
| the index shift | 0.12 | 10 |
| `imul` kx, clamp, shift, add vcx | 3.55 | 299 |
| `imul` ky, clamp, shift, add vcy | 3.84 | 324 |

**There is no lever here, and that is worth writing down rather than
rediscovering.** The two `imul` are ~318 of the 804 and are irreducible at
this precision. What is left is 486 cycles spread over four things that are
each 150 cycles or less: a piecewise depth-to-row index, two overflow clamps
that §85.5.3 put there after a wrong-signed crossing, and two adds. **The
largest single item is the row selection at 2.03 ms**, and no cheaper mapping
suggests itself - the three ranges are what compress a 16,000 m depth into
2,048 table rows.

Two things ruled OUT on the way, both worth not re-checking:

- **Nothing is projected for nobody.** `cs_projall` runs 8.8 times a frame,
  exactly matching `cs_faces` - a box impostor takes `cs_boxlod` and never
  enters here - so no vertex is transformed for an object that then draws as
  a box.
- **The three `cs_project0/2/4` copies are not duplication to merge.** They
  differ in the depth shift, the clamp bound AND the post-multiply shift, and
  the shift is a `%rep` of `shl`/`rcl` pairs because a variable-count 32-bit
  shift is a loop that the macro's own note says was measured at 20 cycles a
  step. Collapsing them would cost more than the indirect call saves.

### 7.6.3 Where the evidence points instead

Of the four, `cs_projall` had the lowest multiply share and turned out thin.
The other three are the opposite shape, and `cs_colscale` is where the
multiplies actually are:

| | ms | per unit | multiply share |
|---|---|---|---|
| `cs_flatverts` (+ `cs_colscale`) | 11.82 | ~1,621 cycles a vertex | **~55%** |
| `cs_scale` (+ `cs_rot`, `cs_dot`) | 14.12 | 4,185 cycles a call | ~32% |
| `cs_stackverts` (+ `cs_colscale`) | 4.82 | | |

`cs_flatverts` is the one to open next: **six `MUL14` a vertex** through two
`cs_colscale` calls that each write three words to memory which the caller
then reads back and adds - eight push/pops, two calls and twelve memory
round-trips a vertex around 900 cycles of arithmetic. That is scaffolding of
exactly the kind §88.4.2.3 found in `cs_edge`, and it is ~45% of 11.82 ms.

**BUILT, and it was the register the scalar sat in** - SPEC.md §88.5.6.2, and
7.6.5 below.

### 7.6.6 BUILT - `cs_scale`, and a shift that was a per-FRAME constant

`cs_scale` was 14.6 ms at 16.4 calls and had never been opened. By phase,
`turnhold`, 3,994 cycles a call:

| | cycles a call | |
|---|---|---|
| A the `pshr` ladder and the projection variant | 309 | 7.7% |
| **B three `cs_sdiff` and the stores** | **1,101** | **27.6%** |
| C `cs_rot` - three `cs_dot`, nine `MUL14` | 2,349 | 58.8% |
| D the three `sar` to whole metres | 226 | 5.7% |

Two changes, and the first is arithmetic rather than a peephole (SPEC.md
88.5.6.3): phase B built a 32-bit `coordinate x 256`, subtracted the 32-bit
16.8 eye and shifted the pair down `8 - pshr`, three times an object. **The
shift distributes exactly** - `(c*256 - p) >> s == (c << (8-s)) - ceil(p/2^s)`
- and `ceil(p / 2^s)` is a property of the FRAME. Nine of them once a frame
(`cs_eyeshift`) leaves `shl ax, cl` and a word subtract at the call site.
`cs_sdiff` is deleted, ladder and all. **1,101 -> 457.**

The second is 7.6.5's finding with the registers the other way round (88.5.6.4):
`cs_rot`'s vector was in bss and `cs_dot` read it back nine times. Here the
multiplicand must be AX and the MATRIX element varies, so the vector needs
three registers of its own - CX, SI and BP, DI accumulating, the two finished
rows on the stack. **2,365 -> 1,979**, and `cs_rot` is 71% multiply.

`cs_scale` **14.53 -> 10.99 ms** (`turnhold`), `13.14 -> 9.76` (`bank`),
`6.23 -> 4.83` (`climb`); `cs_matrix` +0.38 for the builder; the frame **4.04
-> 4.10 fps** and `bank` **4.17 -> 4.21**. A call is 4,229 -> 3,199, -24%.
+148 bytes of image, +10 of bss. 649 frames on seven pinned profiles are
pixel-identical.

**What is left in it** is the two phases that did not change: A (the ladder and
two table lookups) and D (three `sar` by CL to the whole-metre form the size
tests want). Neither has an obvious lever - D's three shifts are what
`cs_ocx/y/z` ARE - and together they are under 15% of a call now.

### 7.6.4 The precision ladder is already three rungs, and it costs nothing

The sub-metre eye position exists because a runway rotated from whole metres
**jumped a metre across between frames - 20 pixels at 22 m** (the note above
`cs_scale`). The obvious question is whether the program is still paying for
that when nothing is under the wheels, and the answer is no: `[cs_pshr]` is
chosen PER OBJECT PER FRAME from its reach plus its radius - sixteenths inside
~2,048 m with a radius under 1,024, quarters inside ~8,192 m with a radius
under 4,096, whole metres beyond - and it selects the projection variant with
it. Measured, objects a frame:

| | objects | sixteenths | quarters | whole metres |
|---|---|---|---|---|
| `turnhold` | 17.2 | 20% | 57% | 23% |
| `cruise` | 14.0 | 17% | 59% | 24% |
| `climb` | 7.7 | **29%** | 57% | 14% |
| `descend` | 10.8 | **0%** | 80% | 20% |

...and by VERTEX, which is what the projection variant is picked for:

| | projections a frame | `cs_project4` | `cs_project2` | `cs_project0` |
|---|---|---|---|---|
| `turnhold` | 94.2 | 26% | 41% | 33% |
| `climb` | 80.2 | **47%** | 45% | 9% |

All three rungs are in use every frame, `climb` - on the runway, the case the
precision was built for - is 47% at the finest, and `descend` at 600 m never
needs it at all. The ladder is doing exactly what it was written to do.

**And it is free either way, which closes "spend less precision" as a lever.**
The rung does NOT change the multiply count: `cs_rot` is nine `MUL14` and
`cs_flatverts` six a vertex whatever it is. All it changes is `cs_sdiff`'s
shift chain - a two-instruction byte shuffle at whole metres against four or
six `sar`/`rcr` pairs - which is at most ~70 clocks an object, **~0.2 ms a
frame** across the whole scene. The vertex pipeline's cost is the multiplies
and the scaffolding, and both are scale-independent.

### 7.6.5 BUILT - `cs_flatverts`, and what a commutative multiply is worth

Bracketed phase by phase (`bank`, 5.9 calls a frame of 4.5 vertices) the
routine read **2,128 cycles a vertex**, of which the six `MUL14` are ~936:

| phase | cycles a vertex |
|---|---|
| A the x setup | 155 |
| **B `cs_colscale` (x M0)** | **683** |
| C the z setup | 140 |
| **D `cs_colscale` (z M2)** | **714** |
| E the sum and the stores | 370 |
| F the loop's advance | 66 |

The 1,192 that is not the multiply is two `call`/`ret`, eight push/pops, two
reloads of a loop-invariant `[cs_pshr]`, and twelve memory round-trips -
`cs_colscale` writes its three products to `cs_col0`/`cs_col2` and the caller
reads all six back and adds them.

**`MUL14` is `imul bx`: it clobbers AX and DX and leaves BX alone.** Put the
scalar in BX and the matrix element in AX - commutative, so the product is
identical bit for bit - and one scalar serves all three of its multiplies with
no save, no call and no scratch array. CL keeps `[cs_pshr]` for the whole
routine, BP counts the vertices, the x column stores into the outputs and the
z column adds into them.

| tier 2, 16 frames | `cs_flatverts` | frame |
|---|---|---|
| `turnhold` | 11.87 -> **8.60 / 8.83** ms | 256.8 -> **253.7** |
| `bank` | 10.04 -> **7.71 / 7.71** | 248.5 -> **245.9** |
| `cruise` | 10.44 -> **7.53 / 7.37** | 162.6 -> **159.4** |

**-2.3 to -3.2 ms, the frame moving by the same amount**, a vertex 2,128 ->
1,394, and the routine now **62% multiply** where it was 44%. +28 bytes, out
of the gap ahead of `CS_VOCAB_AT` rather than out of the image. The picture is
bit-identical over 554 frames on six pinned profiles.

**What this says about the rest of the pipeline.** The lever was not the
multiply count and not the precision (7.6.4 above closed that) - it was
that a three-product column had been factored into a routine whose only way of
returning three words is memory. `cs_stackverts` has the same shape at three
call sites and is 4.8 ms; `cs_scale`'s `cs_rot`/`cs_dot` is the same question
asked of nine multiplies. Neither is as cheap as this one was, because
`cs_stackverts` needs the column TWICE per level (`+col` and `-col` for the
four corners) and so genuinely wants it stored, and `cs_rot` returns into
three different destinations. `cs_colscale` stays for them.
