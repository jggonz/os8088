# An engine of its own for each aeroplane — CLEAR SKIES

> **BUILT.** SPEC.md 88.8.2 is the contract and SPEC.md 88.10.6 is the fix that
> had to come first; this file is the design record behind both, kept for the
> half that was hard, which was not the sounds.
>
> Four aeroplanes, four engines, and the sailplane still silent. **123 bytes**
> of image, one word of bss, no kernel byte, and `SKIES.O88` came out three
> bytes *smaller*. `tests/skiessound.py` is the gate — three red runs — and all
> 34 `skies*` soak rows are green.
>
> **It shipped five times**, and §5 to §8 are the field passes — the last of
> which was not in this package at all: the first build was
> arithmetic and a person listened to it on a real speaker. Two of the four
> tops came down, a feature was **deleted**, and the one complaint nobody had
> predicted got a mechanism of its own.
>
> **The reason this file is worth reading is that it began as a costing and
> the costing was WRONG**, in a way the tree had already written down once and
> nobody had gone back to fix. §1 is that error. The bytes are §3 and take a
> paragraph.

---

## 1. The costing said 52% of everything the program had left. It was 1.1%.

The first pass at this measured the feature honestly — 109 bytes, built and
reverted — set it against Clear Skies' growth headroom of **208 bytes**, and
recommended cutting the design down to fit. Every number in that sentence is
right except the one that mattered.

**Where the 208 came from.** `skies.asm` declares its bss as

```
    OS88_BSS (CS_VOCAB_AT - (os88_image_end - $$)) + CS_VOCAB_MAX + CS_WLD_MAX
```

so `image + bss` is `CS_VOCAB_AT` plus the overlay — **a constant, whatever
the program's own size**. A gate reading that field cannot see a 2,000-byte
addition. The only honest reading of what is left is the **gap**: the middle
of the three `times` at the end of the file, the room between the top of the
ZWORD chain and the overlay's fixed base. That much the costing got right, and
it is a real and useful finding.

**What it then did wrong was treat the gap as a budget.** It is not a budget.
It is the distance to a **hand-set constant**, and the constant had stopped
tracking anything years of work ago:

| | |
|---|---|
| `CS_VOCAB_AT` goes 0xB400 → 0xBE00 | §88.4.5.5's end tables need the room |
| …and then stays at 0xBE00 while | §88.10.2 packs the art, §88.10.3 takes it out of the image, §88.10.4 makes the body a part, §88.10.5 takes the nine worlds out |
| so the program gets **thousands of bytes smaller** | and its growth room gets **no bigger at all** |

By the time anybody looked, the gap was 208 bytes, `-DCSPROBE` was 167 bytes
**over and did not assemble**, a `--vocab-at 0xC200` knob had been bolted onto
the Makefile to buy the diagnostic trees one rung back, and `skies.asm`'s own
comment said the gap was *1,444 bytes* — stale by 1,236, and believed.

**The tree had already caught this exact error and recorded it.** SPEC.md
§88.4.5.5, on the 2,560-byte end tables that were refused against this same
gap and then taken anyway:

> *"the arithmetic that refused it was correct … and it never asked what that
> gap was actually protecting."*

It protects **nothing**. It is claimed RAM that no instruction reads. The
costing made that mistake a third time, against a sentence in the binding
contract that says not to.

**The lesson is one line and it is not "check your arithmetic".** The
arithmetic was right both times. What neither pass did was ask what the number
it was measuring against was made of — and this one was made of somebody's
hand, years ago, on a program that no longer exists.

---

## 2. The fix: the address is DERIVED (SPEC.md 88.10.6)

`CS_VOCAB_AT` is `APP_MAX_SIZE - CS_VOCAB_MAX - CS_WLD_MAX` — 0xE3C0, the top
of the segment — computed in `tools/csworlds.py`, which imports `APP_MAX_SIZE`
from `tools/os88pkg.py` rather than copying it. There is no number left to
tune, so there is none left to leave behind.

| | before | after |
|---|---:|---:|
| the gap, shipped build | **208** | **9,872** |
| `CSDIAG` | 456 | 9,096 |
| `CSHZPROBE` | 871 | 9,511 |
| `CSPROBE` | **−167: did not assemble** | **8,473** |
| the heap claim | 51,776 | 61,440 |

**The claim is the whole cost and it is one this program has already paid.**
61,440 is 340 bytes above the 61,100 that `image + bss` measured before
§88.10.3 — a configuration that shipped and ran on every machine in the tree.
§88.4.5.5's three checks hold unchanged: the bss ships inside the part as a run
of zeros that LZ4 all but deletes, so the floppy does not notice; `SKIES` is in
`SMALLOMIT_GAMES`, so the 128 KB machine never loads it; and the rest is heap
on a machine this program takes whole for as long as it runs.

Two things went with it. **`--vocab-at` is deleted** — the overlay is hard
against the ceiling and there is nowhere to raise it to — and so is the
Makefile's `CSVOCABAT`, so a diagnostic tree and a shipped tree now assemble
against one `cswidx.inc`. **`make skiesdiag` works again**, and the row that
uses it passed in the soak below for the first time since it broke.

What this does **not** buy is room past `APP_MAX_SIZE`. The segment is 61,440
bytes because a package addresses itself with 16-bit offsets. The next time
Clear Skies runs out, the answer is another part — not another address.

---

## 3. The engines (SPEC.md 88.8.2)

`CSP_SND` at offset 46 — **appended**, for `CSP_INDK`'s reason — points at a
seven-byte record, and 0 means *no engine*. One shaper reads it:

> `Hz = CSS_IDLE + (source × CSS_SPAN) / 100`

| | idle → full | source | (as first built) |
|---|---|---|---|
| Cessna 172 | 60 → 105 Hz | lever | unchanged |
| Pitts Special | 75 → 160 | lever | unchanged |
| Fouga Magister | **180 → 700** | **spooled thrust** | *was 260 → 1,200* |
| Icon A5 | 95 → **170** | lever | *was 95 → 190* |
| Wassmer Bijave | *no record* | — | — |

**206 bytes of image** of a 9,872-byte gap: **2.1%**. `SKIES.O88` is 44,226
bytes, **three fewer** than before, and no floppy in any of the four geometries
moves a cluster. One word of bss. Under 1% of a flown frame.

Three things in it are worth more than the table:

- **A shut throttle is an IDLE and not silence.** It is the change that does
  most of the work: each aeroplane announces itself on the runway before
  anything is touched. `CSS_IDLE` = 0 *is* silence, so the field is the switch
  as well as the number and no code tests for it.
- **The Fouga follows `[cs_thracc]`, not the lever.** §88.7.5 has modelled a
  5.3-second spool since the jet shipped and nothing could ever *hear* it.
  This is the row that earns the feature; the other three are a table.
- **The note has inertia.** `CSS_LAG` closes a fraction of the gap each tick,
  so a throttle that shuts in one step glides rather than changing note. That
  is §5's answer and not the first build's — the first build had a *beat*
  there instead, and §5 is why it is gone.

---

## 4. What was measured, and what still cannot be

`tests/skiessound.py`, 24 checks, 62 s solo. It asks the **guest** what it is
playing — `[cs_tone]` over sixteen consecutive ticks — and computes the
expected note on the host from the record the guest holds. Two red runs:
`--clobber-shared` (every aeroplane gets the Cessna's engine: three checks go
red) and `--clobber-spool` (the jet reads the lever: one check goes red, on its
own, which is what makes it the jet's).

**All 34 `skies*` soak rows pass** — 5:35 at lane 4 — which is the scope the
overlay move reaches, every world pointer in the program having moved 0x25C0.

Three things the row's own construction had to learn, kept because each one
looked like a broken feature first:

- **`[cs_back]` is not "is it flying".** SPEC.md 88.10.5 says it is the mode
  the bracket took and is *never cleared on the way out*; the predicate is
  `cs_back ≠ 0 AND cs_quit = 0`. A loop waiting for `cs_back` to reach zero
  waits for ever.
- **`F` toggles.** Typing it again while the first is still being acted on
  walks straight back in, so the row asks once and then waits on state.
- **The Plane drop-down's clip is filled in when the title page arms it**, and
  reading it at launch answers a box whose centre is 41 pixels left of the real
  one. Every click then misses and the row that gets picked is the row that was
  already picked — which reads exactly like a drop-down that does not work.

**Two questions are left, and neither can be answered in a container.** Both
are `CSS_IDLE`'s. A real PC speaker rolls off badly below ~100 Hz and a
synthesised square wave does not, so the piston idles sound fine under MartyPC
and QEMU and may be inaudible on a 5150; and whether a 9.1 Hz tremolo reads as
an engine or as a fault is a listen. `CSS_IDLE` and `CSS_BEAT` are one word and
one byte per aeroplane, which is deliberately the cheapest thing in the design
to change once somebody has heard it (docs/FIELD-MACHINES.md).


---

## 5. The second pass: what a person with ears changed

The first build was arithmetic checked against a test that reads `[cs_tone]`.
Everything in it was *correct* and three of its decisions were wrong, which is
the whole case for docs/FIELD-MACHINES.md existing. The report, verbatim:

> **Cessna:** Idle is audible. Once revved up there is a periodic "dip" in the
> sound that does sound like a bug, rather than an engine.
> **Pitts:** A much more frequent "bug". The varying framerate making the rate
> of the sound vary is hurting this, too, so constant may be what we need.
> **Fouga:** Probably appropriate for a jet, but much too high pitched for
> human ears, see if you can bring it down a bit. No warble here though and
> that works.
> **A5:** Sounds pretty good at idle, slightly high pitched at full. Probably
> accurate, but again, human ears and all.
> **For all of them:** The stepping, between throttle levels, sounds more like
> it is playing a note than switching engine pitches.

### 5.1 The beat is deleted, and both halves of why are worth keeping

It read as a **fault** — *"does sound like a bug, rather than an engine"* — on
the aeroplane with the gentlest setting the design had, one tick in four at
five hertz. That is not a tuning problem; no depth makes a periodic dip sound
like combustion.

And its rate moved with the frame rate **anyway**. §88.8.2.1 had defended that
with the wall clock, and the defence was only ever half of one: the wall clock
fixes a beat's **period** and cannot fix its **sampling**, because `cs_steps`
caps a frame's owed ticks at `CS_MAXSTEP` = 3 and discards the rest. The
document predicted the aliasing and shipped the feature anyway. The field heard
it.

**So `CSS_BEAT` and `CSS_MASK` are gone rather than zeroed** — the record is six
bytes, there is no inert path, and the finding lives in SPEC.md 88.8.2.1 where
the next person to want engine texture will meet it.

### 5.2 Two tops came down, and both were arithmetically right

The Magister's 1,200 Hz is a defensible reading of a Marboré at 21,500 rpm and
is *"much too high pitched for human ears"* — 1,200 sits in the band a PC
speaker is harshest in, which is a fact about a 1981 cone and not about
turbojets. The A5's 190 Hz is exactly where a Rotax 912 fires and is *"slightly
high pitched… probably accurate, but again, human ears and all"*.

**The reporter conceded the arithmetic in both sentences and overruled it in
both**, which is the clearest statement of what this measurement is for that
the project has on file.

### 5.3 "Playing a note" — the one nobody predicted

> *"The stepping, between throttle levels, sounds more like it is playing a
> note than switching engine pitches."*

A throttle that moves in one step moved the note in one tick. Two steady square
waves and an instant transition between them is a synthesiser retuning; an
engine has mass. **`CSS_LAG` is that mass** — `[cs_eng]` closes that fraction
of the gap each tick, never by less than one hertz — and the brake shutting the
throttle now falls through the range: **ten** distinct notes on the Cessna,
**thirty-five** on the Magister.

**Be honest about what it does not fix.** During a *sustained* sweep the note
tracks the lever at the lever's rate, so the steps are the same size they were:
fifty throttle levels traversing the range in fifty ticks, whatever the lag is.
The slew fixes every transition that is *not* a sustained sweep — a tap, a cut,
the brake, the reset, and the first tick of a flight, where the engine audibly
comes up to idle from nothing.

What was left after that was resolution, and only the jet had any to reclaim:
`[cs_thracc]` is 8.8 where the lever is fifty whole steps, so the Magister
scales straight off it now and never rounds through a percentage. The pistons'
steps are ~1 Hz and stay there, because fifty lever positions and one square
wave is the floor.

### 5.4 The test had to change shape, and is better for it

Three of its checks were about a beat. What replaced them:

- **the note is ONE value over sixteen SETTLED ticks** — steadiness asserted
  directly, which is what the field asked for in as many words;
- **it GLIDES** — a lever shut in one step must produce four or more distinct
  intermediate notes and still arrive, with `--clobber-lag` (every `CSS_LAG` to
  0) the red run, reading *"0 distinct notes"*;
- **settle before you read.** A reading taken the tick after the lever moved is
  now a reading of the glide. `settle()` asks for convergence rather than
  counting ticks out, because a trainer's 45 Hz takes fifteen at a shift of 2
  and the jet's 520 takes four times that at 3.

One bug in the harness is worth writing down because it cost a wrong reading:
the pin held `[cs_thracc]` at a *proportion of full thrust*, and `cs_step`
computes its target as `thr × CSP_THRUST / 100` in **whole units** before
shifting into 8.8 — so 50% of a 19-unit engine is 9 and not 9.5. The pin and
the model disagreed by four parts in 2,400, `cs_step` dragged the thrust back
every tick, the thrust never settled and neither did the note. **A pin that
fights the model is not a pin**; pin what the model wants.


---

## 6. The third pass: a fourth is not a glide, and idle is where you start

The second pass fixed what the field named and introduced one thing it had not
seen yet. The report:

> **Cessna:** Good
> **Pitts:** Slightly steppy, but overall good
> **Fouga:** This one still "plays notes" as it goes up or down, but the top,
> stable, is now pretty good. Very high pitched still but it is a jet.
> **A5:** Good
> **All of them:** On entry to the scene they all start at one point, and
> change to another point. They should probably all start at their "idle"
> point without ramping to it.

### 6.1 The slew was right in hertz and wrong in intervals

`CSS_LAG` moved the note by a share of the **gap**. That is a constant fraction
in hertz and a wild one in *interval*, and the complaint tracked the arithmetic
across all four aeroplanes exactly:

| | first step, lever shut | in cents | the verdict |
|---|---:|---:|---|
| Cessna | 45 >> 2 = 11 Hz at 60 | 290 | *"Good"* |
| Pitts | 85 >> 2 = 21 Hz at 75 | 400 | *"slightly steppy"* |
| **Magister** | **520 >> 3 = 65 Hz at 180** | **500** | *"still plays notes"* |

Five hundred cents is a musical **fourth** in one tick. The ear was measuring
intervals and the design was measuring hertz, which is why widening the record
was the answer and re-tuning it was not: no value of a gap-share is small at
180 Hz and large at 700 at the same time.

**`CSS_CAP` is a second shift on the NOTE**, and the step is the lower of the
two. A share of the note is a constant interval by construction, so the time a
glide takes is proportional to the **octaves** crossed rather than to the
hertz. Measured, lever shut in one step: the Cessna 105 → 60 Hz through **15**
distinct notes in 19 ticks, the Magister 625 → 180 through **97** in 101 — 1.8
octaves at ~22 cents a tick. The jet's is slow and should be: `CSP_SPOOL` gives
the thrust 5.3 seconds and the note now takes about the same.

It also answers the Pitts for free. Where the ceiling is *tighter* than the
lever's own step, the note is smoother than the lever: the Pitts' lever moves
1.7 Hz a tick at 39 cents and its note moves 1–2 at 23.

### 6.2 Starting at idle is a sentinel, not a special case

`[cs_eng]` begins at 0, so every flight opened by gliding **up to idle** — a
rising note no aeroplane makes, and the one thing in the second pass nobody had
predicted because the ramp only exists at all *because* the slew does.

A note of 0 means "not running yet" and the first tick **snaps**. That
generalises correctly rather than special-casing entry: a later flight carries
the throttle the last one left (§88.8), and starting at *that* engine's note is
the same rule. Clearing `[cs_eng]` at bracket entry is what makes the sentinel
true, and is also what stops a Cessna gliding down from the Magister the last
flight left behind.

### 6.3 Two things the test had to learn

- **A check can stop being about its subject and keep passing.** The spool's
  check was *"still climbing 24 ticks after its own slew settled"*, which
  discriminated while the note arrived quickly. `CSS_CAP` makes the Magister's
  own glide take a hundred ticks — longer than the spool — so the delay stopped
  measuring the spool and `--clobber-spool` went **green**. It is a source
  check now: at a half-open lever the jet plays **426 Hz**, the thrust it has,
  and not the **440** the lever asks for, the two being separable because
  `cs_step` rounds its target to whole units before shifting into 8.8.
- **Compute both sides of a comparison host-side when the clobber can reach
  one.** The first spelling read the "thrust" figure through `law()`, which
  honours the guest's `CSS_FLAGS` — so under `--clobber-spool` it moved to the
  lever's value and the red run read as two identical numbers disagreeing.

### 6.4 And one honest limit, restated

During a *sustained* sweep the note cannot be finer than the thing it follows,
and the lever has fifty whole steps. A piston's sweep is ~1 Hz a tick and stays
there. Everything in §6 is about the transitions that are not sustained
sweeps — and the one source with real resolution to give was the jet's.


---

## 7. The fourth pass: the wobble was under the note the whole time

> *"The changes go 'up, then down, then up'. So the jet — if I change it
> between just two throttle ranges — sounds great, no up/down. But when I
> change it straight from 0-100 as fast as possible the ramp is 'up, down a
> bit, up a bit more, down a bit, up a bit more' etc."*

**A monotone throttle producing a non-monotone note is a defect, not a
perceptual limit**, so this one was measured before anything was touched. A
probe flew the Magister, slammed the lever to 100 and recorded `[cs_thr]`,
`[cs_thracc]`, `[cs_eng]` and `[cs_tone]` at every sim tick; a second held the
key instead and pinned nothing. Both read **0 down-steps in 149**, strictly
monotone from 180 Hz to 684.

So nothing at or above `cs_tone` could account for a down, and the search moved
below it — to `spk_tone`, which wrote the 8253's **control word on every
frequency change**. That resets the counter's output to its initial state and
inhibits it until a count is loaded: every change **restarted the square wave**
rather than retuning it. During that sweep the note changed on **149 of 149
consecutive ticks**, so the speaker was restarted 149 times in a row, each one
truncating the cycle in progress.

**The report's own shape is what identifies it.** *"Between just two throttle
ranges — sounds great"* is one or two restarts, inaudible. *"0-100 as fast as
possible"* is a hundred and fifty in a row. No property of the Hz sequence
distinguishes those two cases; the number of port writes is the only thing that
does.

The fix is SPEC.md 34.1.1 and it needs **no new state**: `snd_ch2mode` already
says whether a tone is sounding, so the control word goes in when it is not 1
and is skipped when it is. Mode 3 loads a new count at the end of the current
half-cycle — a clean retune — and mode 3 is the chip's resting state here
anyway, §34.4's PWM being the only thing that leaves it and `spk_pcm_idle`
latching it back. **7 bytes**, measured on the symbol span, because
`kernel.bin` is the same length either way: section padding swallowed it whole,
which is CLAUDE.md's rungs rule catching a reader who measures the artefact.

**Two things about it are decisions rather than side effects.** It changes what
*consecutive* notes sound like — two non-zero tones with no silence between
them used to be separated by the restart's click and are now connected, legato
where it was marcato — and a tune that wants articulation gets it by going
through zero, which is what a rest already is. And **the suite cannot see any
of this**: every shipped sound gate asserts frequencies, and the frequencies
never changed. It is verified by the mechanism and by a listener, which is the
same standing as every number in §6.

### 7.1 What the scoped soak said, and what it did not

Fifty rows — every `skies*` plus every row that reaches a tone, a kernel byte
or a package that beeps — **48 green, and both failures accounted for and
neither the change's**:

- `dotdelwin` — a window-geometry assertion, nothing to do with sound.
  `os88bisect.py classify` re-ran it alone at HEAD: **3/3 GOOD**, so it was
  contention and the base was never built.
- `paccman` — a **fast**-tier host-side source check that passes in a plain
  tree in 0.0 s. It died in the soak's frozen tree on a missing generated
  `paccman.gen.asm`, which is the standing "rows that FAIL where they mean
  SKIP" shape, one artefact along.


---

## 8. The fifth pass: the note moved once a FRAME, not once a tick

> *"90% of the stepping is gone — and this fixed the bug in dot del sound that
> we couldn't find in that session. The last 'stepping' left is, I think,
> because the sound only changes as the frame draws, and a 4.77 MHz running a
> 3d game is slow. Is there an easy option to pace the sound changes more often
> than the frame rate? If not, this is good enough."*

The diagnosis was right and the arithmetic is stark. `cs_sound_step` ran once
per **simulation** tick, and `cs_steps` owes a frame up to `CS_MAXSTEP` = 3 of
them which the `.sim` loop runs **back to back at the top**. So the note moved
three times in a millisecond and then stood still for the 130–280 ms the frame
took to draw: what the ear got was **one change a frame — 4 to 8 a second**,
and every step had to be that much bigger to cover the ground.

### 8.1 The tick is the gate, and then the render can call it

`cs_sound_step` reads `OSAPI_GET_TICKS` and returns unless the tick has moved
since `[cs_sndtk]`. Two things follow, and the second is the point:

- **the `.sim` burst collapses to one step**, which is *right* — a burst is
  catching up on simulation, not on time, and three steps in a millisecond were
  never three steps of glide;
- **the call is now free when nothing has elapsed**, so `cs_scene` can call it
  **once per drawn object**. That routine is 78% of a frame, which is where the
  ticks actually elapse, and servicing them where they fall takes 4–8 changes a
  second to **18.2**.

The stall's cadence gets the same correction for nothing — `[cs_stallt]`
counted calls and now counts ticks, so the beep no longer runs faster on a
machine with a faster frame.

Cost: one `OSAPI_GET_TICKS` per drawn object, and it is **measured** rather
than derived — `tests/skiesperf.py` carries it as a stage now, so it is A/B'd
against a pinned scene and stays re-measurable. **0.1–0.2% of a frame on every
scene in the table** (runway −0.22 ms of 159.02, city −0.23 of 161.01, tower
−0.32 of 180.07, citybank −0.31 of 232.78), with the city figure repeating to
the hundredth across runs.

**The arithmetic that preceded it was pessimistic by two to three times**, and
the reason is worth keeping: it priced each call at PERFORMANCE.md Part 2's
46.7 µs for an `OSAPI_*` far call, and these divide out at **~18 µs**.
`OSAPI_GET_TICKS` returns a word and has almost no body, where the published
figure is a round trip against a slot that does something. Which is the
standing rule one more time — *measure before redesigning* cuts both ways, and
a number quoted from a table is not a measurement of your own call site.

### 8.2 The interrupt was the obvious answer and is REFUSED

Hooking `int 08h` for the bracket would give a perfectly even 18.2 Hz instead
of one quantised to wherever a call site falls, and it is **proven in this very
package** — `CSDIAG=1` (§88.14) does exactly that, chaining to the kernel's.
Two things were measured before refusing it, and both are worth keeping:

- **the stack was never the obstacle.** The whole render chain runs on the UI
  task's 512-byte `STK0`, and a probe that flew the jet over Paris and broke at
  each of the deep render routines found the deepest (`cs_seg`) entering at
  **114 bytes** — 398 free, where the ISR frame plus a kernel call is ~50.
- **the CONTRACT is.** `OSAPI_SND_TONE` is documented worker-safe *by
  construction*, because `snd_req_inst` resolves the **running task's**
  instance — and an ISR is not a task. It would work here, the bracket being
  exclusive so the interrupted task is always ours; and *"works because of who
  happens to be running"* is the shape of reasoning §20.3 exists to refuse.
  Taking it would have meant extending a published ABI for one package's last
  10%.

### 8.3 And it broke the pause, which the suite caught in one run

§88.8.1 put the tone release on the **P key** deliberately, *so that the frame
would not have to carry a compare*. That was sound while the only caller was
the sim loop — a pause is exactly the thing that skips it. The render reaches
`cs_sound_step` now, **and a render runs while paused**, so `P` released the
tone and the very next object put it straight back.

`tests/skies.py` went red on all three adapters in the first scoped soak after
the change: *"PAUSED and the engine tone is still 105 (SPEC.md 88.8.1)"*. The
fix is one compare, placed **before** the far call so a paused frame costs a
`cmp` an object and nothing else.

**The lesson is about the shape of the reasoning, not the bug.** §88.8.1's
argument was explicitly *"testing `[cs_pause]` once a frame would cost a compare
on every frame of every flight to catch a transition that happens when a key is
pressed"* — a correct optimisation whose premise was **who the callers were**.
Adding a caller invalidated it silently, and nothing in the code said so. That
is what a row asserting the *behaviour* is for, and why it is worth more than a
row asserting the mechanism.
