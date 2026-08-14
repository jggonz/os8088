# The kernel's memory

**This document is maintained.** It is the standing account of what the
os8088 kernel spends RAM on and why, and it is expected to be updated in the
same commit as any change that moves a number in it. SPEC.md §2 is the
binding contract for the addresses; this is the reasoning behind them.

Every figure below was measured against the shipped build on the day it was
written. The section sizes, the rungs and the per-module breakdown are
**generated** now — `tools/kernsize.py`, which `make` runs — and the Task
Manager's own rows were read off a running machine. The section at the end
says how to re-measure what is left. **Nothing here is derived from an
earlier edition of this file**; that is how it went three budget moves stale
last time.

---

## The rule

**The kernel is ONE contiguous span starting at linear 0x00600, and that
includes its buffers.** The budget is 102.5KB today; the span it holds
currently runs 0x00600 through **0x19DFF**, and the budget's ceiling is
0x1A000 — so there is **ONE 512-byte step** left under it, against the
four-step standard the moves below are granted on. That is a figure to raise
deliberately or to spend down, not headroom to draw on.
`tools/kernsize.py`, below, is what says so, and `make` runs it on every
build.

Not the code and then some scratch elsewhere: *everything*. Code, read-only
data, `.bss`, the cold segment, the FAT window, the directory and icon
caches, the sector buffer and every task stack are one contiguous span
starting at `KERNEL_SEG`. The **`KERN_BUDGET`** guard in `kernel/kernel.asm`
measures that whole span and fails the build if it is over.

**The two guards are named, not numbered**, and if you have read an older
copy of this file they were "guard 1" and "guard 2". The numbering was the
reason the distinction kept getting lost, because nothing about "1" and "2"
says which is which:

| name | what it bounds | can it be raised? |
|---|---|---|
| **`KERN_BUDGET`** | the **footprint** — this whole span, RAM taken from the machine | yes, by asking. Fourteen times so far — see below |
| **`KERN_CODE_MAX`** | the **segment** — `.text` + `.bss` in one 64KB window | **no.** It is what a 16-bit offset reaches |

They are relieved by different things, and that is the distinction that
matters in practice: the boot overlay (SPEC.md §2.5) and the cold segment
(SPEC.md §2.6) buy room against `KERN_CODE_MAX` and **nothing at all**
against `KERN_BUDGET` — overlay code is still read off the disk into the FAT
window, cold code is still resident. Moving a module cold to fix a footprint
overrun is a no-op that looks like a fix, and because the two rungs it moves
between round separately it usually costs a 512-byte step.

**There is exactly one deliberate exception** to "everything is in the span",
and it is a heap claim rather than a reservation: the menu save-under
(SPEC.md §12.4), which exists only while a pull-down is on screen and is
handed back the moment it closes. It is not part of the kernel's footprint
because on any given tick it usually is not there — `menu_drop` claims it on
the way in and releases it on the way out, *before* the selected item runs,
so a menu that launches something has already given it back by the time the
launch asks for memory.

That claim is **sized from the rect actually dropped** (`menu_save_kb`), not
from the worst case: Locator's File menu wants about 4KB on VGA and about 1KB
on Hercules, where `[vid_planes]` is 1 and three quarters of a fixed figure
would never be written to at all. `MENU_SAVE_KB` = 20 survives as the
build-time **ceiling** that guard 4 proves the arithmetic can never exceed.
Both corrections were the same bug twice: the flat 20KB was first claimed once
at `menu_init` and held for the whole session — more than a third of a 128KB
machine's heap, held permanently against nothing — and then, once it was
transient, it was still 20KB per menu, which on a machine with a sound card
could not be had at all, so every menu there took the slow repaint path.

**The size in RAM is the actual size, not a budget.** There is no growth room
anywhere in the ladder. Each rung is the measured size of what it holds,
rounded up only as far as alignment demands, so the heap starts where *this
build's* kernel happens to end and moves when the kernel does. A fixed
ceiling with slack under it is memory that nothing can ever use — which is
what the **package pool** had become: 60KB reserved above the kernel whether
or not a package was loaded. A package's region is an ordinary heap claim now
(SPEC.md §20.1), taken from the top of the heap downward while data claims
grow up from the bottom.

### `KERN_BUDGET` ran out of room to be raised, and then the wall moved

For one release this was the most important fact in this document:
**`KERN_BUDGET` was exactly equal to guard 5's ceiling**, so raising it
bought nothing. Guard 5 existed because `boot/boot.asm` relocated itself to a
fixed `BOOT_RELOC:7C00` and is *still executing* while the kernel's sectors
land, so the kernel had to end below the relocated sector's stack:

```
KERNEL_SEG*16 + KERN_SIZE  <=  BOOT_LIN - BOOT_STACK
       1,536  +  KERN_SIZE  <=   86,016  -    2,048
                 KERN_SIZE  <=   82,432
```

Confirmed at the time by padding `.text` until something broke: at 2,182
bytes of padding the build passed, at 2,183 guard 5 fired — not guard 1.

**The sector is at the top of conventional RAM now (SPEC.md §2.7).** It reads
`int 12h` and lands its last byte on the machine's last byte, so it is above
every kernel that could fit the machine at all, on every machine, by
construction. What that deletes is not the guard but its *address*: there is
nothing left for `kernel.asm` to compare against, so guard 5 became a
statement about the smallest machine the system is claimed to run on —

```
KERNEL_SEG*16 + KERN_SIZE  <=  MIN_RAM_KB*1024 - BOOT_SECT - BOOT_STACK
       1,536  +  KERN_SIZE  <=       131,072   -    512    -    2,048
                 KERN_SIZE  <=   126,976
```

— which is **44.5KB above `KERN_BUDGET`**. `BOOT_RELOC` is gone from both
files; `KERNEL_SEG` is the only constant they still share.

So a tenth budget move is possible again, on the terms the nine below were:
asked for, granted, and spent on something named. What is *not* available is
a move that takes the slack — 44.5KB of headroom is the fifth move's mistake
at five times the size, and this constant has only ever bought scrutiny.

**Move 10 is that tenth move**, 82,432 → 86,528, granted in advance for
"Incoming QOL improvements". Two things about it are on the record rather
than in a commit message. It is the third raise granted *ahead* of the work
(3 and 7 were the others), so move 7's term applies: the raise is spent by
the commits that need it, and a plan document is not one of them. And it
left **4,608 bytes spare — nine 512-byte steps** (three now: SPEC.md §14.1's
Timer redraw and §22.9's status line each took one, and move 11 handed a
further step back rather than leaving it), where the fifth move
settled that 2,048 is the right amount: enough that an ordinary bug fix does
not trip the guard, small enough that a feature does. Until the QOL work
lands, the guard is looser than the project's own standard. If that work
comes in under the 4KB, the fifth move is the precedent for what to do with
the rest — hand it back, rather than leave it for the next author to find.

**And when the kernel does approach 126,976, the answer is not a raise
either.** It is two kernels off one tree — a full one and a minimum one —
because a 128KB machine and a 640KB machine stop wanting the same feature set
long before they stop fitting the same image. Raising `MIN_RAM_KB` instead
would be the project quietly dropping the machines it was written for.

### `kern_small` does not fit today, and it is 512 bytes

Measured on the integration branch, not inferred, and recorded here because
`make small` is not part of `all` — so this fails only for whoever asks for it,
and it does so with a `%error` that names this document:

| | `KERN_SIZE` | `KERN_BUDGET` | spare |
|---|---:|---:|---:|
| `elendilon` before on-demand modules | 98,816 | 97,280 | **−1,536** |
| ...with SPEC.md §2.8's two modules | 97,792 | 97,280 | **−512** |

The twenty-first move landed this build **exactly** on the figure — 0 spare,
which its own comment says was the guard doing its job — and everything added
to `.text` or `.bss` since has therefore been an overrun. SPEC.md §2.8 took the
Control Panel and the floppy formatter out of the image and closed 1,024 of it,
which also gave the small build a formatter it had never had; the last 512 is
a twenty-second move of 1KB, or a third module, and it is the owner's call
which. **Do not route around it by re-guarding the formatter back out** — that
undoes the feature the modules were built for and buys one rung.

### The first thing to take out again, if space becomes the priority

**The keyboard mouse (SPEC.md §9.6, `kernel/mouse.inc`) — 520 bytes of
`.text`, two 512-byte steps: the spare went 4,096 → 3,584 → 3,072.** It is recorded here at the owner's request,
because a size decision is one the next author should be able to *find*
rather than rediscover, and this one was taken on grounds that are about
priority rather than about a number.

It went in because it is the difference between a usable machine and an
unusable one at exactly the moment the mouse fails, which stopped being
hypothetical on the Compaq Portable III (docs/FIELD-MACHINES.md). It was
costed and approved against the budget as it stood *before* move 10, where
the same 512-byte step was half the remaining slack rather than an eighth of
it, so **the case for keeping it is stronger here than it was when it was
granted**. The recommendation stands
regardless.

If footprint ever outranks it, this is the first candidate: dropped outright,
or built only into the testing and benchmark kernels, where the harness drives
the mouse over QMP and never needs it. Nothing else depends on it — one module
plus six call sites (`ui_task`'s key poll and its deferred ladder, the
`kbm_poll` in `menu_track`, `ui_drag` and `ui_grow`, and `osapi_mouse`).

**The second step was SPEC.md §9.6.1/§9.6.2 — 114 bytes of code for a
512-byte step**, and the arithmetic is worth keeping because it is what this
guard is *for* rather than a sign the feature was expensive. The image rung
had **one byte** of slack (`.text` + `.bss` = 49,151 against a 49,152
ceiling), so any addition at all cost the same 512 and trimming the feature
could not have avoided it — which also means the next author who adds one
instruction anywhere in `.text` pays 512 and should expect to. Buying the step
back needs 666 bytes out of `.text`, and `.cold` has 160 bytes of rung slack,
so moving a module cold would only spend the same step over there. Measured
with the bisect above, not inferred.

**A third addition — SPEC.md §9.6.3/§9.6.4 — cost 300 bytes and NO step**,
which is the same guard reporting the opposite answer and is worth recording
beside the paragraph above for exactly that reason. §9.6.3's item-granular
menu navigation is **+186** (`menu_kbnav` and one byte of `.text` state in
`menu.inc`, plus the hook in `kbm_move`) and §9.6.4's int 09h peek for keypad
5 is **+114** (`kbm_isr`, `kbm_p5spend`, a saved vector and the install). The
image rung had 483 bytes of slack when they were written and has 183 after, so
the footprint did not move at all and `KERN_SIZE` is unchanged.

**Keypad 5's total is 125 bytes, and that figure is the one to quote if it is
ever weighed again** — measured by building the kernel three times rather than
by counting instructions, which is the only honest way to price a removal:
`.text` is 48,858 as shipped, 48,744 with §9.6.4 taken out, and 48,733 with
the key gone as a button key altogether. So the hook is 114 and the
pre-existing `cmp ah, 0x4C` (which could only ever fire with NumLock on, and
so in the mode where the keyboard mouse is switched off) is another 11. It
survived that weighing on the grounds that 125 is well inside the slack and
that Ins and Space, the other two keys of the same one action, are not
affected either way. **Extending it from tier 0 to every tier cost −2**: the
7-byte `[cpu_tier]` test came out and a 5-byte guard went into `kbm_btn`,
which is what makes the hook a fallback on a BIOS that delivers the key
rather than a second opinion that double-presses.
Both are measured with `make`'s own report rather than inferred, and neither
is a candidate for removal ahead of the feature they belong to: they are the
difference between a keyboard mouse that can reach a menu item and one that
takes six presses per item and shows nothing at all on a separator.

**Recommend it; do not remove it unasked.** On a machine with no working
mouse, taking it out means the desktop cannot be clicked at all.

### The seventeen moves

`KERN_BUDGET` was 65,536 — the first 64KB above the BIOS data area, which is
where the "one region" rule came from. It has moved seventeen times; the raises
were each asked for and granted, and moves 5 and 11 are the two downward.
The constant's own comment in `kernel/kernel.asm` is the long form of every
row below, and it is the copy to trust if the two ever disagree.

**From move 15 the table is about TWO figures.** The guards split at
docs/KERN-SPLIT-PLAN.md, so a row says which of them moved: 15 and 16 are
`kern_big`'s alone, and 17 moves both by the same 2KB.

| | budget | bought |
|---|---:|---|
| 1 | 65,536 → **71,680** | the SPEC.md §41 extended-memory store, and the two API surfaces that came with it (`wm_geom`, `wm_about_set`) |
| 2 | 71,680 → **72,704** | the loadable sound driver (SPEC.md §51) and its Control Panel pages — the one raise that **bought more than it spent**, since `sndfm.inc` and `sndsb.inc` were 3,260 lines resident on every machine whether or not a card was in it |
| 3 | 72,704 → **76,800** | SPEC.md §51.5's keyed `SYSTEM.CFG`. Granted in ADVANCE of the work, with an optimisation pass to follow |
| 4 | 76,800 → **80,896** | the file manager's Cut/Copy/Paste, its recursive paste engine and the drag (SPEC.md §22.3/§22.4) |
| 5 | 80,896 → **74,240** | *nothing — this one gives back.* The passes after raise 4 left 8,704 bytes spare, which is the guard switched off: anything short of an 8KB addition passed without the conversation this constant exists to force |
| 6 | 74,240 → **76,288** | SPEC.md §5.6's `gfx_line` and the file dialog's size-before-load (§38.6), which met at the guard. Either alone fitted; together they overran by 512 |
| 7 | 76,288 → **78,336** | SPEC.md §54's file type associations and the disk path, costed together before either was written |
| 8 | 78,336 → **80,384** | the association work's own bug reports — §54.4.1's notice naming the missing program — plus the §18.92/§18.93/§18.4.2 disk work, which cost the footprint nothing and paid back in seconds of boot |
| 9 | 80,384 → **82,432** | the file modules into `.cold` (SPEC.md §2.6). The first raise bought for the OTHER guard, and it landed exactly on guard 5's ceiling — the last one possible until that ceiling moved |
| 10 | 82,432 → **86,528** | **"Incoming QOL improvements"** — 4KB granted in ADVANCE, and the first move taken against the room SPEC.md §2.7 opened up. Terms below |
| 11 | 86,528 → **86,016** | *nothing — this one gives back too.* SPEC.md §53.6.1's XMS desktop stash was removed (a snapshot cannot restore a desktop that moved while the bracket ran), and the 512-byte step it cost goes back with it rather than becoming slack. **It landed alongside §22.9's status-line work, which spent a step of its own**, so the footprint is back at 84,480 and the slack is 1,536 — one step UNDER the standard. That is the guard working rather than a fault in either change: the next feature has to ask |
| 12 | 86,016 → **90,112** | 4KB asked for and granted **in advance**, on move 7's terms: SPEC.md §39.11's adapter switching took the spare to EXACTLY ZERO — 6 bytes left in the image rung and 155 in the cold one — and what the headroom buys immediately is §39.11.4 (blanking the card the machine has just left, so a two-monitor 5150 does not sit with a frozen desktop on the tube nobody is using) and §31.10's hiding of a Display page with nothing to choose between. Granted WITHOUT the usual "hand back what the optimisation pass saves", because the 128KB floor is to be met by a SECOND BUILD of this kernel rather than by holding one build to a figure both machines can live with. Until that exists this is still the only guard there is, so move 5's rule stands: headroom for ordinary growth, not an invitation to spend it |
| 13 | 90,112 → **92,160** | 2KB, and move 12's story again: the spare hit EXACTLY ZERO, this time from two directions at once — SPEC.md §52's hard-disk installer arriving on the integration branch, and §11.95.1's "a window that grew reveals nothing" (193 B of `.text`, 8 of `.bss`). Granted at 2KB rather than 4, which puts the guard back within reach of ordinary growth without pre-authorising another feature's worth |
| 14 | 92,160 → **94,208** | 2KB granted **in advance** for SPEC.md §18.94.2's finding: a file operation spends over half its disk TIME on work the progress widget never shows, because the kernel optimised for SECTORS where the media charges for REVOLUTIONS. Measured over one install, the payload streams at **5.78 sectors per `int 13h` call and every other phase runs at exactly 1.00** — `dsk_dirw_next` hands out one LBA at a time and every caller reads it with `cx = 1` into a single 512-byte buffer. What it funds: a per-volume banked BPB (a fixed disk cannot be swapped, so it revalidates once ever) and coalescing the directory walks into runs, which needs somewhere bigger than `dsk_secbuf`. The batch bracket and its sector cache still to come cost this figure **nothing** — that one is a refusable heap claim by explicit decision, so a 128KB machine can still install, just slowly |
| 15 | **big** 94,208 → **96,256** | 2KB for the rest of SPEC.md §39's dual display (docs/DUAL-DISPLAY-PLAN.md): estimated at 1,400–1,900 bytes against a spare that had fallen to 1,024. **The first move that is one build's alone** — `kern_small` stayed at 94,208, which is the whole reason the split exists |
| 16 | **big** 96,256 → **98,304** | 2KB again, on move 15's terms, for §39.16's union and what follows it |
| 17 | **both** big 98,304 → **100,352**, small 94,208 → **96,256** | 2KB each for **window drawing optimizations** — SPEC.md §5.8's partial restore, §11.96.6's cache restoring only what the pass painted, §11.96.8's bounded edge merge, §11.90.1's opt-out fill and §11.90.2's damage rect. One window restore went **49.22 → 23.36 ms (2.11x)**, a raise's white flash disappeared, and Paint's canvas 8,670 → 6,759 ms. **It moves BOTH guards, and that is the argument rather than a convenience**: a redraw optimisation is worth most on the slowest machine, and the machine that feels a 49 ms restore is the 4.77MHz one at the RAM floor — so this is not work `kern_small` may be kept out of, and move 15's "small should drift tighter" does not apply in this direction. What spent the PRIOR step is the same round: §11.96.9's fix (a partial draw may not re-bank — the field bug §11.96.6 introduced) crossed the rung the image had 15 bytes left of, taking the spare to ONE step against a standard of four. Granted at 2KB on move 13's terms, with the round's biggest item still to come — a raise restoring only what was **covered** (docs/HANDOFF-REDRAW.md item A), which is a `wm_raise` change and not a new mechanism. **On the integration branch it lands on top of §41.11's removal, which had just handed small two rungs of its own**, so small comes out at SEVEN steps and owes the conversation the "Where it goes" section below names — the raise was asked for and granted against a one-step figure, and that figure had moved underneath it |
| 18 | **big** 100,352 → **102,400** | 2KB on move 13's terms — headroom, half a step — for SPEC.md §62's **network driver** (docs/NET-PLAN.md), `kern_big`'s alone. Stage 1's 175 bytes landed inside a rung the merge had already opened; what it is FOR is stage 2's file redirector |
| 19 | **big** 102,400 → **102,912** | One step, and an **ASK rather than a grant**: SPEC.md §5.4.1.1's pair decoder landed the kernel EXACTLY on the guard, so without it the next byte anywhere failed to build. What the step buys is 512 bytes of **pair table** — a source byte is two pixels and maps to a two-bit destination pattern, so 2 × 256 entries make the blit's inner loop a read, an `xlat` and a shift. The constant's own comment carries two costed ways to hand it back |
| 20 | **big** 102,912 → **104,960** | 2KB asked for and granted for the rest of that work, **"blit4 rendering speed"** — `gfx_blit4` is the largest single drawing cost in the system and it is under Paint's canvas, Solitaire's card backs and ArtfulType's keystroke. Measured: a Paint canvas 5,526 → 2,431 → 517 → **259 ms** and CONSTANT in the content (PERFORMANCE.md Sets 41–43). 148 of it went on §5.4.1.2's aligned bodies and 417 on §11.96.11's cache band — which is not blit work, and is charged here because this is the step that was open |
| 21 | **small** 96,256 → **97,280** | 1KB asked for and granted, **allocated to window redraw improvements** — and this figure's FIRST move at the 1KB unit its own rule sets, `kern_big` having moved by 2KB throughout. SPEC.md §11.96.11 had landed the small build EXACTLY on the old figure, 0 spare and not one byte, so the next addition to `.text` or `.bss` anywhere would have failed to assemble there and only there. The ask was made with what the last of the old figure bought already measured (a Paint raise **680.9 → 451.0 ms**, PERFORMANCE.md Set 46). It is move 17's allocation continued rather than a new one, and what is left over is bound by move 5's rule: headroom for ordinary growth, not an invitation. **Moves 17 and 21 together are the tier named `window redraw`, and that tier is CLOSED** — 94,208 → 97,280, spent; the tier above it is not a continuation of it |
| 22 | **small** 97,280 → **99,328** | **2KB asked for and granted, and a stated departure from this figure's own 1KB rule** — the owner's, made explicitly, recorded here rather than quietly taken, because a standing rule that can be stepped over without a note is not one. The rule is NOT withdrawn: the next move is 1KB again unless somebody says otherwise. **What it repairs first is a small build that had stopped assembling** — measured at the grant, `KERN_SIZE` 98,304, which is 1,024 OVER the old figure, and the tree had been in that state for some time without anyone seeing it because `all` never builds `kern_small`. 512 of the overshoot predates the drag-cache round and 512 arrived with it; neither was asked for. Against 99,328 the same build is 1,024 spare — two steps, the smallest honest landing place rather than a comfortable one. **What is in front of the rest** is the size-changed notification and its straddle rule (a window spanning two displays adopts the more restrictive size), which is kernel-side and lands on both guards. It is deliberately un-numbered here — it is not written yet, and a forward reference to a heading that does not exist is what `tools/checkdocs.py` catches. On move 10's terms: granted ahead of the work, and whatever that does not spend is handed back rather than kept |
| 23 | **small** 99,328 → **102,400** | **3KB asked for and granted, allocated to performance and disk.** It repairs the same breakage move 22 did, and that repetition is the point: `make small` had stopped assembling again — `KERN_SIZE` measured at **100,864** by bisecting the guard, **1,536 over the old figure, three rungs** — and once again nobody saw it arrive, because `all` still never builds `kern_small` and nothing else does either. **Twice now this figure has been discovered broken rather than reported broken**, which is not the guard failing but the guard being the only thing watching, asked only when somebody happens to run the target. **Why 3KB and not the 1KB first asked for**: 1KB (100,352) does not clear 100,864 at all and would have failed on the next assemble, and the smallest figure that assembles is 100,864 exactly — 0 spare, handing the next byte added anywhere the same failure. 3KB lands **1,536 spare, three steps**, and keeps the whole-KB unit this figure's rule sets; move 22's 2KB landed two steps and called that the smallest honest landing place, and this is that judgement applied to an overshoot half again as large. The 1KB unit is NOT withdrawn — the next move is 1KB again unless somebody says otherwise |

**Moves 18, 19 and 20 met in ONE MERGE and none of them cancels another.** The
network driver's raise and the blit's were granted against the same 100,352
base on different branches, so taking the larger of the two answers would have
silently revoked one — the network driver's, whose spending is mostly still
ahead of it. The figure is the base plus all three, and the numbering follows
the integration branch's; the merged tree measures 102,400 against it, five
steps.

**And one place NOT to go looking for bytes.** §18.98's `DVOL_MAX` 6 → 8
costs `.bss` 134, of which 128 are `dsk_bpbv` — a **64-byte banked BPB per
volume**. Re-indexing it from volume 2 to reclaim those was proposed here and
is **wrong**: the array is not fixed-disk-only, however its declaration used
to read. A floppy banks there too, inside §18.9.3's batch bracket with
`[dsk_bpbok] = 2`, and rows 0 and 1 are its heaviest users — that bracket is
the whole of §18.9.3's measured win (an install's BPB reads 41 → 2, the floppy
side 356 → 163 sectors). `dsk_bpbsg` beside it is the §18.8.2 disk-identity
signature, banked with the head and read by `dsk_fatw_pick`. The split §18.9.2
makes is between **permanent** (fixed, 1) and **batch-scoped** (floppy, 2),
not between volumes that use the array and volumes that do not. The stale
comment that said otherwise — future tense, written before §18.9.3 landed —
is fixed at the declaration.

**`kern_small` grows by 1KB, `kern_big` by 2KB.** A standing rule from here
on rather than a property of any one move, and the asymmetry is the point of
the split: small is the guard the 128KB machine lives under, so it is asked
for in the smallest useful unit. Two 512-byte rungs is enough for ordinary
growth to continue and not enough to pre-authorise a feature — which is also
the direction move 15 said this figure should drift in.

**Move 17's open question is part-answered**: it left small at SEVEN steps,
three over the standard, because §41.11's removal handed it two rungs the
raise had not counted on. The floppy round (§18.96.2's user-picked format
size, §18.98's third and fourth drives with `DVOL_MAX` 6 → 8, and §26.4's CGA
icon and caption) has since spent one of them. It still owes the rest of that
decision.

**`BOOT_RELOC` moved with the first five** — 0x0940 → 0x0AA0 → 0x0B80 →
0x0C00 → **0x0D40** (linear 0x11000 → 0x12600 → 0x13400 → 0x13C00 →
**0x15000**) — and never moved again, which is why moves 6 through 9 consumed
the whole of the gap it left. **That sequence is over rather than paused**:
the sector is at the top of RAM now (SPEC.md §2.7), so there is no address to
move and no constant to keep in step across two separately-assembled files.
Each of those five moves also raised the smallest machine that could boot, by
exactly the distance it travelled, which is the cost that made it a decision;
the computed placement raises nothing, because the sector is by definition
already above whatever the machine has.

### Which guard binds

`.text` + `.bss` are addressed through one segment with 16-bit offsets, so
`KERN_CODE_MAX` caps them at 65,536 **whatever the budget says**. That limit
is untouched and cannot be raised at all.

For most of this project's life the budget was the tighter of the two, which
was the intended order — a budget is a decision and a segment is physics.
Move 9 made that lopsided:

| | headroom |
|---|---:|
| `KERN_CODE_MAX`, the segment | **11,920 B** for `.text` + `.bss` |
| **`KERN_BUDGET`, the footprint** | **512 B** for the whole span — one step |
| guard 5, the smallest supported machine | **40,448 B** for the whole span |

The budget is still the tighter of the three and is meant to be. What changed
with SPEC.md §2.7 is that it is a *decision* again rather than a wall: below
it sits the same conversation the nine moves record, and above it sits a real
ceiling 44.5KB away instead of one the budget was already touching. `.cold`
and `.ovl` relieve the segment and nothing else, exactly as before; the
levers that take bytes off **both** are still deleting kernel code and moving
a feature out to a package (SPEC.md §28's precedent, below).

**Do not trust that table — measure it.** It has been stale twice, once by
1,024 bytes and once by three whole budget moves, which is how two features
met at the guard without either author knowing it was close:

```sh
lo=70000; hi=86016
while [ $((hi-lo)) -gt 1 ]; do mid=$(((lo+hi)/2))
  sed -i "s/^KERN_BUDGET equ .*/KERN_BUDGET equ $mid           ; x/" kernel/kernel.asm
  nasm -f bin -w+error -I kernel/ -I build/ -o /dev/null kernel/kernel.asm 2>/dev/null \
      && hi=$mid || lo=$mid
done; echo "KERN_SIZE = $hi"; git checkout kernel/kernel.asm
```

The same loop against `KERN_CODE_MAX` gives `.text` + `.bss`. Both are in
`kernel/kernel.asm`; nothing else needs touching.

The two are also coupled through the rounding, and that coupling is
load-bearing in both directions. A byte moved from `.bss` to `.lowbss` helps
`KERN_CODE_MAX` but *hurts* `KERN_BUDGET` until the image falls far enough to
drop a 512-byte step: when the `.lowbss` rung is full, the very first byte
moved costs a whole step. Even with three steps left under the budget, **moving data
out of the segment is not free**, and neither is moving code into `.cold`.

---

## Accounting: a rung is the unit, and staying under one is not free

**"It did not cross a rung" is not "it cost nothing".** It is the single
easiest thing to get wrong when reporting a change, and it has been reported
wrongly in this tree, so it gets its own section.

A section's bytes are rounded up to a whole 512 before they enter the ladder,
so most changes move `KERN_SIZE` by exactly **zero**. That is a true statement
about the machine — no boot takes another byte of RAM, the heap starts in the
same place, no figure a user can see moves — and it is a false statement about
the cost. The bytes came out of the **slack in that rung**, and that slack is
not the change's to keep: it is the space the *next* feature has before it
pays a whole 512 for its first byte.

So a change has two prices and both have to be named:

| | what it is | who pays |
|---|---|---|
| **the bytes** | `.text` + `.bss` + `.cold` + `.lowbss` deltas, summed | the next feature, out of the rung's slack |
| **the rung** | whether `KERN_SIZE` moved a step | the machine, in RAM, on every boot |

Two failure modes follow, and they are opposite. Reporting only the rung says
"free" about a change that quietly ate 400 of a rung's 512 bytes, and the
author of the next 200-byte change then gets handed a 512-byte bill they did
nothing to earn — the SPEC.md §14.1 case, where 80 bytes of Timer state met a
rung with **12** left and cost a whole step. Reporting only the bytes hides
the moment the machine's RAM actually changed, which is the thing
`KERN_BUDGET` exists to make a decision rather than an accident.

**Report both, always: the per-section jumps, their sum, and the rung state
including how much slack is left in each.** The last part is the one that
makes the next author's estimate possible. `tools/kernsize.py` produces
exactly that, and `make` runs it.

### `tools/kernsize.py` — the numbers, as a command

Every figure in this document used to be produced by hand, either by
bisecting a constant (the recipe above) or by injecting a `%warning` probe
into a copy of `kernel.asm`. That is why it has gone stale three times: a
number nobody can produce in one command stops being produced. The probe is
in `kernel.asm` now, behind `%ifdef KERNSIZE`, and the tool reads it:

```
$ make                                     # ...or tools/kernsize.py
kernsize[big]: sections   text 47,094 +273  bss 3,889 +100  cold 20,355 +0  lowbss 7,748 +0  ovl 2,680 +0   (sum +373)
kernsize[big]: rungs      image 51,200 +512 (217 left, was 78)   cold 20,480 +0 (125 left, was 125)   low 9,216 +0 (444 left, was 444)
kernsize[big]: footprint  KERN_SIZE 85,504 of KERN_BUDGET 86,016 -> 512 spare (1 step), was 1,024  [+512]
kernsize[big]: segment    .text+.bss 50,983 of KERN_CODE_MAX 65,536 -> 14,553 left
kernsize[big]: ladder     KERNEL 0x0060  COLD 0x0ce0  FAT 0x11e0  LOW 0x1300  HEAP 0x1540 = 85.0 KB   (heap KB = int 12h - 85.0)
kernsize[big]: *** the image rung CROSSED: 99 -> 100 steps of 512 - the machine's RAM moved ***
```

That is a real run — SPEC.md §11.95's title-bar zoom, reported against the
commit it was branched from — and it is this section's worked example twice
over. (Its figures predate several rounds; the `[big]` tags do not, and are
what every line carries since the split. The numbers are left as they were
taken, because a worked example that is re-typed to match today's build stops
being a record of anything.) The image rung had **78 bytes** left when the work started, so the
first commit's 140 bytes crossed a step and cost the machine 512; the second
commit's 186 landed inside the new rung's 512 and was reported as "free",
which is the word this section exists to retire. What the second commit
actually did was take 186 of the 403 bytes the crossing had just bought.

Three things about it:

- **It never fails the build.** The guards inside `kernel.asm` are what refuse
  an overrun; a reporter that can break `make` is a reporter somebody deletes.
- **The figures come out of NASM**, from `kernel.asm`'s own `KIMG_PARA` /
  `COLD_PARA` / `LOW_PARA` equations, not from a Python copy of them. A second
  opinion about how a rung rounds is a second opinion that can drift, and the
  tool would be the last place anyone looked when it did.
- **A knob build is measured as itself.** `make VIDEO=cga`, `DISKCNT=1` and
  the rest pass their own `-D` flags through, because a report describing a
  different binary from the one on disk is worse than none. The first thing
  that turned up is worth keeping as a habit: **`DISKCNT=1` costs a whole
  rung** — 87,040 against the shipped 86,528, so SPEC.md §18.94.1's field
  kernel runs one step nearer the guard than the one you tested. It had
  landed on `KERN_BUDGET` *exactly*, with 0 spare, before move 12 raised the
  budget; measure it again rather than trusting either figure. `--bless`
  refuses a knob build for the same reason: the baseline is the shipped
  kernel.
- **A VARIANT is not a knob, and it is blessable.** `KERN_BIG` / `KERN_SMALL`
  each produce a kernel that *ships* (docs/KERN-SPLIT-PLAN.md §5), so each has
  a baseline of its own in the block below and each is blessed separately;
  `--bless` merges rather than replaces. Every reported line names its variant
  (`kernsize[big]:` / `kernsize[small]:`), because two products with separate
  baselines *and* separate budgets otherwise produce unlabelled figures
  somebody compares against the wrong build. The module and theme tables are
  the **default** variant's alone — one of each exists, and written by
  whichever bless ran last they would silently describe `kern_small` in a
  document whose every other figure is the shipped kernel's.
- **The baseline is in this file**, in the fenced block below, and
  `tools/kernsize.py --bless` rewrites it. So the delta `make` prints is
  "since this document last told the truth" — which means the document cannot
  go stale quietly any more: an un-blessed change shows up as a non-zero delta
  on every build until somebody either explains it or blesses it. Bless it in
  the same commit as the change, and paste the report into the commit message.

<!-- kernsize:begin -->
```json
{
  "big": {
    "bss": 5920,
    "budget": 104960,
    "codemax": 65536,
    "cold": 34954,
    "coldpara": 2208,
    "fatpara": 288,
    "imgpara": 3456,
    "kend": 6624,
    "kseg": 96,
    "ksize": 104448,
    "lowbss": 7762,
    "lowpara": 576,
    "ovl": 2825,
    "stk0": 1024,
    "text": 49239
  },
  "small": {
    "bss": 5649,
    "budget": 102400,
    "codemax": 65536,
    "cold": 34439,
    "coldpara": 2176,
    "fatpara": 288,
    "imgpara": 3264,
    "kend": 6400,
    "kseg": 96,
    "ksize": 100864,
    "lowbss": 7762,
    "lowpara": 576,
    "ovl": 2796,
    "stk0": 1024,
    "text": 46397
  }
}
```
<!-- kernsize:end -->

---

## Where it goes

Measured on the shipped build: the five section sizes by bisection, the rungs
derived from them exactly as `kernel/kernel.asm` derives them.

| region | size | what it is |
|---|---:|---|
| image (`.text` 49,239 + `.bss` 5,920) | 55,296 B | all resident kernel code in the kernel's own segment, its read-only data, and its scratch |
| cold code | 35,328 B | 34,954 bytes with a CS of their own: the Control Panel, the five file modules, and SPEC.md §2.6's second round — assoc, disk, driver, memory and desk |
| FAT window | 4,608 B | nine of the mounted volume's FAT sectors (SPEC.md §18.8) — the whole FAT on any floppy, a sliding window on a hard disk |
| `.lowbss` + task 0's stack | 9,216 B | 7,762 B of tables, stacks and disk buffers, plus `STK0_SIZE` = 1,024 |
| the boot overlay | 0 B | 2,825 bytes of code inside the FAT window, gone by the first mount |
| **total** | **104,448 B** | of a 104,960-byte budget — **512 B spare, ONE step** |

**This table is HAND-WRITTEN and the block above it is not**, which is how it
came to disagree with the blessed JSON by 800 bytes before this was noticed.
`--bless` regenerates the baseline, the module table and the theme table; it
does not touch these six rows. Re-derive them from `kernsize`'s own
`sections` and `rungs` lines when you bless, or the next reader gets a
confident wrong answer about how much room is left.

**These are `kern_big`'s figures**, which is to say the shipped kernel's
(docs/KERN-SPLIT-PLAN.md). **The two builds have DIVERGED** — in both
directions now — so this table is big's alone and `make kernsplit` is what
prices the difference. Things ADDED to big behind `%ifndef KERN_SMALL`:
SPEC.md §18.96's floppy formatter, §39.11's dual display. Things REMOVED from
small: SPEC.md §41.11's extended-memory store, the first of those and so far
the only one.

`kern_small` stands at **100,864 B of its own 102,400-byte budget, 1,536 B
spare, THREE steps** — inside the four-step standard, and moves 21 and 22 are
where the surplus went. It stood at seven steps then, three over the
standard, which **owed a conversation rather than being headroom**: two things
had arrived at the same figure from opposite directions and neither knew about
the other:
isolating the store took `.text` −1,035, `.bss` −124 and `.ovl` −386 off small,
two whole 512-byte rungs, and move 17 then raised BOTH guards by 2KB for the
window drawing work on the reasoning that a redraw optimisation is worth most
on the slowest machine. Both are right on their own terms. The composition is
what move 5 calls the guard switched off, so the options are move 11's — hand a
step or two back now that a removal has paid for them — or to spend it on the
round the raise was granted for; **what is not an option is leaving it
unremarked**, which is how the fifth move's 2,048 became 512 without the
constant being revisited.

Each rung is its contents rounded up to a whole 512 bytes, and the remainders
are the only slack anywhere in the ladder: **137 bytes on the image, 374 on
the cold segment, 430 on `.lowbss`** (big's; small's are 178, 377 and 430). They are rounding artefacts, not
reservations — and per the accounting section above they are also the whole
of what the next feature can spend without moving the machine's RAM.

**§22.12.1's toast is the worked example of what that slack is for.** Written
as a proc it measured +55 bytes of `.cold` against the 51 that were free at
the time, and crossed a rung; inlined at its one call site — `fm_edit_commit`
has already saved AX, SI and DI, so a proc was spending ten bytes re-saving
them — it is +51 and costs the machine **nothing**. Four bytes were the
difference between free and 512.

The ladder lands on these segments: `KERNEL_SEG` 0x0060, `COLD_SEG` 0x0DE0,
`FAT_SEG` 0x1680, `LOW_SEG` 0x17A0, `HEAP_SEG` 0x19E0. `tools/kernsize.py`
prints that line, so it need never be derived by hand again.

**Run `python3 tools/kernsize.py` rather than trusting the numbers in this
paragraph.** It reads the build; this prose does not, and `--bless` rewrites
only what sits between its own markers, so everything outside them — this
sentence included — goes stale silently and has done so before.

**`.lowbss` is where the rounding last bit**, and SPEC.md §14.1 is the worked
example of the warning above it: the Timer's per-instance state grew 8 bytes
to hold the characters its window is showing, ten instances of that is 80
bytes, and the rung had **12** left — so 80 bytes of data cost a 512-byte
step and took the footprint 81,920 → 82,432. The alternative on offer was a
parallel array in `.bss`, whose rung did have room; it was refused because
indexing per-instance state by a slot derived from a stride is a thing that
breaks silently, and because a step of move 10's nine is what move 10 was
granted for. A rung step is the unit to reason in, not a byte count.

Everything above `KERN_END` is the claim heap, up to whatever int 12h
reports. The arithmetic is exact and worth writing down, because every RAM
figure in this project falls out of it:

> **heap KB = what int 12h reports − 89.0**

`KERN_END` is 5,696 paragraphs = 91,136 bytes = **exactly 89.0 KB**, and the
heap starts there. It was a round 80.0 for the whole of moves 1..10, and a
`.lowbss` step took it to the awkward 80.5 that is easy to drop from a mental
sum. **Do not re-derive it by hand**: it moves with every rung crossing, and
`tools/kernsize.py`'s `ladder` line prints both the segment and this
subtraction, which is the point of the tool.

## What it actually takes to run

The **heap** column is the property that decides behaviour, and it is
measured — by clamping what `mem_init` believes int 12h said and booting each
size under QEMU. (The clamp is a throwaway; it is not in the tree.) The RAM
column is that heap plus `KERN_END`'s 89.0KB. **The two rows marked *measured*
were measured against an earlier, smaller `KERN_END` and are therefore
optimistic by the difference — re-measure before quoting the floor.**

The two rows marked *measured* were re-run for this edition, because the low
end is where `KERN_END`'s growth actually changes the answer. The rest keep
their original heap measurements — a heap of 23KB behaves the same however
big the kernel below it is — with only the RAM column re-derived.

| RAM | heap | what happens |
|---|---|---|
| < 82KB | — | **cannot boot**, and nothing to do with the heap: the kernel's whole span has to fit under the top of RAM, because `.lowbss` and task 0's stack are the top of it. **This row now tracks the kernel** — the boot sector relocates to the top of conventional RAM (SPEC.md §2.7) rather than to a fixed address, so it is never the thing in the way |
| < 105KB | — | **refuses**, on this build: the sector compares its computed base against where the kernel's read plus its own 2,048-byte stack would end, prints `RAM` and halts rather than loading a kernel over itself. Measured at exactly the boundary — 105KB boots to a desktop, 104KB never loads a byte (`make test RAMKB=104`). The number moves with the kernel's size, which is the point of computing it |
| 85KB | 5KB | boots, full desktop, opens a Disk window and browses both floppies — and **will not load a package**. Measured |
| 88KB | 8KB | **loads a package** (`hello`). Measured |
| 103KB | 23KB | Note Pad runs. Paint loads and puts up its "Not enough memory" notice — the designed tier, not a crash |
| 167KB | 87KB | Paint still gets the notice |
| 183KB | 103KB | **Paint runs live**, full 448×280 canvas |
| 639KB | 559KB | everything, with room to spare |

So the honest floor is **85KB to boot and browse, 88KB to run something**,
and **~183KB for every shipped app at full function**. The often-quoted
"128KB" sits between those: it runs the OS and most of the packages, and
Paint declines.

**The boot floor and the useful floor have come apart**, and that is the one
qualitative change here. They used to coincide — the first machine that could
boot at all had 14KB of heap and ran packages fine. `KERN_END` has since
risen to 89.0KB, so the smallest machine that gets to a desktop has a heap too
small to load anything. Two machines that both "run os8088" are a few
kilobytes apart and do different things.

That gap is now a property of the *kernel* and not of an address: with the
sector at the top of RAM (SPEC.md §2.7) both floors move together whenever
`KERN_END` does, where the old fixed `BOOT_RELOC` held the boot floor still
and let the kernel drift up towards it. The floors will still separate as the
kernel grows — that is arithmetic — but nothing in the memory map is holding
them apart on purpose any more.

Two things this table is not. It is not a promise about *speed* — these were
measured under QEMU, which does not model 8086 timing at all (SPEC.md §5.4).
And the sizes below 640KB were simulated by clamping the heap, so they
exercise every "the heap said no" path faithfully but do not exercise a BIOS
that reports a small number to the KERNEL, which only real hardware and 86Box
can do.

The **boot sector's** half of that is testable here now, which it was not
before: SeaBIOS answers 639KB whatever `-m` says, so `make test RAMKB=<n>`
assembles the sector to believe a different number (SPEC.md §2.7). That is
what the two rows above were measured with. It moves the sector and nothing
else — the kernel still reads the real `int 12h` for its heap — so it tests
the relocation and the refusal, not the small-heap behaviour the rows below
it describe.

### What the Task Manager shows

The same breakdown, live, one indented row per buffer under **System**. Read
off a running machine, and it agrees with the ladder above to the kilobyte:

```
RAM   89/639K [] HEAP  10/559K       <- the map's caption, both figures
[==============================]     <- every byte the machine has
CPU  386+     XMS   0/64448K         <- and what it has no address for
[==============================]

  NAME          ADDR  SIZE   HEAP
[]System        0600   79K     2K
    Code+data          66K      -
[]  Stacks              4K      -
[]  Disk bufs           4K      -
[]  FAT snap            5K      -
```

`System` is `KERN_KB` = 79 (80,384 B rounded up), at `KERNEL_SEG`. Its four
buffer rows sum to it **exactly**, and that is a property rather than a
coincidence: every rung of the ladder is a whole number of 512-byte sectors,
so half of them are an odd half-kilobyte and four independently rounded parts
can lose two kilobytes against a total that rounds once — so `SK_IMG` and
`SK_DSK` are residuals and absorb it (SPEC.md §20.9). `SK_IMG` deliberately
covers the **cold segment** as well as the image, which is why `Code+data` is
66K and not 46: cold code is code, it is resident, and it is inside the span
`System` measures. Leaving it out made the parts sum three kilobytes short of
the whole with no row saying where they went.

The `HEAP` column beside `System` is the kernel's own claims, which is why
the task-list view's System row reads 81K where this one reads 79K: 79 of
span plus 2 of claim.

Every figure comes from `OSAPI_SYS_KB` rather than from assembly-time
constants of the window's own, because the window is a package and the
kernel's footprint moves with every build.

The heap has no map of its own and never will: a claim is drawn in the
conventional map at its real address, in among the kernel and everything
else, so its figures belong to that map's caption and share the top line with
RAM. **Package regions are claims too** (SPEC.md §20.1) and are drawn there
in their per-slot patterns — at the far right, because they are claimed from
the top of the heap downward while data grows up from the bottom, so the two
kinds separate visibly.

Each row's legend square is the texture its memory is drawn in on the maps,
so the two can be read against each other:

| square | band | where |
|---|---|---|
| 50% gray | the kernel's own span | `System` |
| horizontal bars | its buffers | `Stacks`, `Disk bufs`, `FAT snap` |
| framed light block | a live heap claim | beside the `HEAP` figures |
| per-slot pattern | one package's region | each package row |

A row only gets a square when the texture is its own. `Code+data` has none —
it is drawn in the same gray as `System`, and a square that repeats one above
it is not a legend. `Builtins` has none because a built-in owns no band at
all: its code is already inside `Code+data`, and its memory is heap claims
billed to its own row. A claim is the only band drawn with a **frame**,
because it is the only one that comes and goes while you watch, several sit
shoulder to shoulder, and the scale is coarse enough (4KB per pixel on a
640KB machine) that a 3KB Disk-window cache is one column.

---

## Each region in detail

### The image — `.text` 47,736 B + `.bss` 3,897 B

One flat binary at `KERNEL_SEG:0000`, assembled `-f bin` with no linker.
`.bss` follows `.text` immediately and is uninitialised by definition, so it
costs nothing on the floppy and everything in RAM. The ladder charges the
pair **rounded up to a whole 512 bytes** (see the alignment invariant below)
— 51,712 B, so 79 bytes of the rung are rounding remainder.

**The file on disk runs past that rung**, and the gap is not padding for its
own sake — it is where the cold segment and then the boot overlay live.
`.bss` is nobits, so `kernel.bin` used to be `.text` alone and the boot
sector's contiguous read landed sector K at offset K·512, somewhere inside
`.bss` rather than at the paragraph the ladder calls `COLD_SEG`. Declaring
both as sections with an explicit `start=` closes that gap: NASM emits the
space between them as zeros, so each lands exactly on its rung in the boot
sector's existing single read — no second loop, no gap constant, and
`KERNEL_SECTORS` still falls out of the file size.

The padding is not wasted either. **The whole of `.bss` is now zeroed before
`kmain` runs**, which nothing previously did — `nasm -f bin` zeroes nothing,
which is why `[fdlg_win]` has to live in `.text` as a `dw 0` (`fdlg_grab`
reads it on the machine's very first mouse press). The splash is safe through
it: `viddet` and `splash` keep all their data in `.text` precisely because
they run during the load.

Expressing it as a section start is what makes it non-circular. Padding
*inside* `.text` would grow `KTEXT_SIZE`, which grows `KIMG_PARA`, which
grows the padding, and there is no fixed point; `.cold`'s and `.ovl`'s own
sizes are not terms in their own `start=`, so there is nothing to converge.

**Not all of the kernel's code is here**, and that is a change from what this
section used to say. There was a period after `.fartext` was retired
(SPEC.md §33) when it was true — when "cold code is ordinary code" and there
was nowhere to put a module that was too cold to be worth the space. That
stopped being true with SPEC.md §2.6: `.cold` holds **20,839** bytes today,
and `.ovl` another 2,662 that cost nothing at all. Both have their own sections
below. What has *not* changed is the warning that went with it: neither
mechanism buys a byte of footprint, so neither is a way to make the kernel
smaller.

### Task stacks — 3,840 B

Eleven background slots of `SCH_STACK` = **256** bytes (`MAX_TASKS-1`, since
task 0 owns no slice of `sch_stacks`), plus `STK0_SIZE` = 1,024 bytes for
task 0 itself. They live in `.lowbss`, addressed through SS, which is why
SS ≠ DS everywhere in the kernel (SPEC.md §1).

**Both numbers are measured.** A 0xCC fill over the whole stack region, then
the machine driven as hard as it goes — Timer, two Bounces, About, the
Control Panel on both its pages, the Task Manager with a window drag, a Disk
window, the Fractal with its worker task, and Paint saving a GIF into a
folder it created from the file dialog — leaves its deepest mark at **274
bytes** on task 0's stack and **142** on a background task's, the latter
confirmed twice over, by the Fractal's drawing worker and by Tracker's
streaming worker with a Sound Blaster's IRQs nesting on top of it. ISR frames
are included: the tick and mouse handlers run on whichever stack they
interrupt. So 256 is 1.8× the worst observed background depth and 1,024 is
3.7× task 0's.

**1.8× is thinner than this project usually runs, so it is checked rather
than trusted.** `SCH_MAGIC` sits at the bottom word of every slice, written
by `task_spawn` and compared by `sch_switch` against the task it is switching
away from; a mismatch means the next push would land in the slice below —
another task's stack — and `sch_stkdie` halts the machine instead. The check
is four instructions and no multiply, because `SCH_STACK` = 256 makes slot
*n*'s base the slot index in the high byte of BX and nothing else; a
build-time `%error` pins that assumption to the constant.

**The cold segment does not change this.** A far call costs two extra bytes
of stack per crossing, and the file path now crosses on the way in and again
on every call back out to a resident routine — but those modules are UI-task
only, so what they spend comes off task 0's 1,024 rather than a 256-byte
slice, and the deepest chain adds well under a dozen bytes to a mark that
already had 750 to spare.

**`STK0_SIZE` is a constant, and that is the whole point.** It used to be
"whatever is left between the top of `.lowbss` and the kernel segment" —
which meant task 0's stack silently absorbed every byte saved anywhere below
it. Two rounds of shrinking the buffers under that rule freed exactly
nothing: the FAT buffer gave up 7KB and task 0's stack grew by 7KB. Naming
the number is what turned those savings into memory.

**The QEMU probe understates a real BIOS.** SeaBIOS services its interrupt
entries on an internal extra stack, so under `make test` the only foreign
frames a task slice ever carries are this kernel's own tick and mouse
handlers. A real IBM BIOS runs int 09h — which it STIs early, so the tick and
the mouse nest *on top of* it — and its int 08h chain on whichever task stack
is current. `tests/stackprobe` exists for exactly this gap (docs/TESTING.md).

**And it has been run there.** On a real 5150 (640K, Hercules, a 20MB MFM
disk through its controller ROM) with a floppy-to-hard-disk copy running, the
keyboard mashed for typematic and the mouse in motion, 217 samples over ~2
minutes read **112 of 256, canary intact** — against 92 for the same probe
under QEMU, so the real BIOS's interrupt nesting costs ~20 bytes the emulator
cannot show. The probe's own frames are ~30 of that 112, putting the pure ISR
+ switch component near 82; add the deepest *application* depth the 0xCC
fills have ever recorded (~80, the Fractal/Tracker workers) and the projected
real-hardware worst case is ~160–170 of 256 — a ~1.6× margin, with
`SCH_MAGIC` still underneath it.

### Disk buffers — 3,584 B

Three buffers in `.lowbss`, written by int 13h through ES:BX and read only
through `dsk_get_dir` / `dsk_get_icon`, which stage one entry at a time back
into the kernel segment so no drawing or parsing code has to learn about
segments:

- `disk_dir`, 1,024 B — the mount-time directory listing, 32 synthesized
  32-byte entries. The 32-entry cap is what sizes it.
- `disk_icons`, 2,048 B — one harvested 64-byte icon per listed entry.
- `dsk_secbuf`, 512 B — one sector of scratch: the directory sector being
  read-modify-written on a write, and the zero-padded final sector of a file.

Together they are exactly `.lowbss` minus the eleven background stacks and
the four tables below, which is how the Task Manager's `Disk bufs` row
derives itself.

### FAT window — 4,608 B

`DSK_FAT_SECS` × 512 — the whole FAT on any floppy, and a sliding window on a
hard disk (SPEC.md §18.8). Re-read from the volume on **every** mount, with
`dsk_next_clus` its single reader and `dskw_setfat` its single writer, both
through ES only.

`DSK_FAT_SECS` = 9 is not a buffer with slack — it is an **acceptance
threshold**. Mount rule 10 (SPEC.md §18.2) refuses a volume whose declared
FAT is larger before a byte of it is read, so the number is exactly the
largest FAT any geometry this OS boots or builds declares: 1.44MB = 9,
1.2MB = 7, 720KB = 3, 360KB = 2.

It is also where the boot overlay lives until the first mount, which is the
only reason `.ovl` costs nothing (below).

### What is reserved on every machine, used or not

Two things in the ladder are paid for by machines that can never use them,
and both are deliberate. They are worth knowing about because they are the
first place to look if the footprint ever has to come down without deleting a
feature.

**`DRV_BLOB_SZ` = 34 bytes of `.bss`, plus `CFG_FBUF` for the file's own
record.** That is the hard-disk driver's settings, carried inside
`SYSTEM.CFG` (SPEC.md §51.9) rather than in a file of the driver's own. A
128KB machine with no hard disk reserves them anyway. The alternative was a
second file, and it was measured and rejected: reading it cost a second
directory search, a second read, and — because every file slot resolves in
the *current* volume — two full remounts around them. This is the honest cost
of not making the one file the boot already reads into two, and it is why one
blob is shared by whichever driver asks rather than one being reserved per
class.

`CFG_FBUF` is **derived rather than chosen**: the keys tile the settings
struct exactly, so the file's length is
`CFG_REC0 + CFG_NKEY * CFR_HDR + CFG_NB + 2` and there is nothing to round
up. Slack in a buffer whose exact size is an expression anyone can evaluate
is `.bss` that nothing can ever use.

**The per-volume FAT windows are the one place the kernel claims heap for
speed rather than capacity.** `DSK_FAT_SECS` sectors — 4.5KB, rounded to a
5KB claim tagged `MEM_K_FATW` — **per driver-backed volume**, so a copy
alternating between two hard-disk partitions stops reloading nine FAT sectors
on every switch (SPEC.md §18.8.1). Two mounted partitions is 10KB of heap
that a machine with no hard disk never pays, a floppy never asks for, and a
refused claim degrades out of entirely. The kernel-side cost is 24 bytes of
`.bss` for the two per-volume arrays, four words moved from `.bss` into
`.text` so they can carry real initialisers, and the park/pick/claim/drop
routines.

**…and the directory window is the second, on softer terms.** `MEM_P_DIRW`
(SPEC.md §19.2.3) is 16KB of cached directory sectors, and it is **purgeable**
(SPEC.md §50.6) where the FAT windows are not: a copy that is halfway through
needs its FAT, and nothing needs this. So it is given back the instant anything
else wants the room, and `dsk_rah_want` will not even ask unless `mem_avail`
reports twice its size — because `mem_claim` sheds and retries, and a directory
walk that took the window raise cache away every time it ran would be a worse
neighbour than the slow read it is replacing. The kernel-side cost is the code,
one `.text` word (`dsk_rah_seg`, which the shed zeroes), and 36 bytes of `.bss`.

---

## Two invariants that are easy to break

### Every disk-visible base is 512-byte aligned

int 13h moves one sector per call, which bounds a transfer to 512 bytes —
but **does not stop one from straddling a 64KB physical boundary**. Only
starting on a 512-byte boundary does that, and the DMA controller answers a
straddle with error 09h.

Every base in this ladder is an int 13h target: the FAT window, the disk
buffers, a package image being loaded, and a package's file buffer out of the
heap. So the image rounds up to a whole **512 bytes** rather than to a
paragraph, and because `FAT_PARA` (288) and `LOW_PARA` are both multiples of
32 paragraphs, aligning that one rung aligns the whole ladder. Guard 6 proves
it — and guard 6b proves the claim heap keeps it, since a package image is
read by int 13h into a **claim** now: `mem_claim` rounds to whole KB, so
every base it hands out is `HEAP_SEG` + n·64 paragraphs.

This held by luck until the ladder became derived: every base used to be a
round constant like `0x0300` or `0x2A00`, and nothing said why that mattered.
The symptom when it broke was a **"Disk error" toast on any save larger than
the distance from the buffer to the next 64KB boundary** — Paint's 63KB BMP
hit it immediately, a Note Pad text file never would.

### The boot sector has to get out of the way

The BIOS loads `boot/boot.asm` to 0000:7C00 and it is *still executing* while
the kernel's sectors arrive — it far-calls the splash at `KERNEL_SEG:0008`
after every run of them. With the kernel landing at 0x00600 and running up to
80KB, it covers 0x7C00 long before the last sector.

So the sector's first act is to copy itself **to the top of conventional
RAM** — `int 12h`, times 64, less `0x7E0` for its own offset and its own 512
bytes — and far-jump there. **The copy keeps the same offset**, so every
label in the file still resolves at `org 0x7C00` and only the segment
registers change; its stack rides along at the same offset and grows down
from its new base, with `BOOT_STACK` = 2,048 bytes under it. The far jump is
`push`/`push`/`retf` rather than `jmp seg:off`, because the segment is not a
constant any more and the 8086 has no `push imm`.

That is 2,560 bytes at the ceiling, and only until handoff: `kmain` sets
`SS:SP` in its fourth instruction, after which the sector is dead and those
bytes are ordinary heap — the first package loaded lands on them, since
`mem_claim_hi` hands regions out downward.

**It used to be a fixed `BOOT_RELOC` = 0x0D40 (linear 0x15000), and that is
what guard 5 was about.** The address, not any property of the kernel,
capped the footprint at 82,432 bytes — which `KERN_BUDGET` reached in move 9,
so for one release the budget could not be raised at all. See
"`KERN_BUDGET` ran out of room" above for what replaced it.

`KERNEL_SEG` is now the **only** constant `boot/boot.asm` and
`kernel/kernel.asm` share (`BOOT_STACK` is in both, but it is a size that
cannot be wrong by being stale — the same 2,048 bytes asked about two
different machines). `apps/os88api.inc` carries a third copy of
`KERNEL_SEG`, because it is baked into every package's far-call targets —
**a kernel move means rebuilding every `.o88` and both apps floppies**, or a
package calls into empty memory.

---

## Where the code goes

Every byte of `.text` and `.cold`, attributed to the file that emitted it.
**Both tables below are generated** — `tools/kernsize.py --modules`, blessed
in by `--bless` — by bracketing every `%include` with a bare label in each
section and reading the differences back.

Bare labels emit nothing, and the tool **proves** that rather than asserting
it: the markers go into a temporary copy, `kernel/kernel.asm` is never
written to, and it assembles both the plain and the instrumented source and
refuses to report a single number unless the two binaries are byte for byte
identical. A measurement that can perturb what it measures is not one. The
module rows sum to the section totals, which is the check that the
attribution is complete — and `kernel.asm`'s own row is the **residual**, so
anything the pass failed to attribute lands there in plain sight instead of
disappearing.

Three results are worth knowing before you go looking. The figures for them
are in the tables and deliberately not repeated here: a number stated twice
is a number that goes stale once, which is how this section came to be
generated in the first place.

- **The file system is the largest theme by a wide margin** — bigger than the
  whole window system and its furniture put together. FAT12 is not a small
  thing to implement twice (read and write), and the Disk window is the
  largest single module in the tree.
- **Nearly a third of the kernel's code is not in the kernel's segment, and
  it is exactly the file modules and the Control Panel.** Six modules hold
  every byte of `.cold` between them; `fsx.inc` was a seventh at 190 bytes
  until SPEC.md §53.6.1 removed its XMS desktop stash. That is what bought
  `KERN_CODE_MAX` its headroom, and it bought the footprint nothing.
- **The three built-in kinds are about 2%.** About, Timer and Bounce together
  cost less than moving Note Pad out to a package (SPEC.md §27) saved on its
  own.

<!-- kernsize:themes -->
| theme | bytes | share |
|---|---:|---:|
| the file system, end to end | 30,957 | 36.8% |
| the window system and its furniture | 21,250 | 25.2% |
| drawing: adapters, primitives, glyphs, icons | 12,924 | 15.4% |
| hardware: drivers, clock, mouse, sound, CPU, XMS | 9,990 | 11.9% |
| the kernel proper: API table, heap, scheduler, events | 6,726 | 8.0% |
| the three built-in kinds | 1,376 | 1.6% |
| the Control Panel | 846 | 1.0% |
| **total** | **84,193** | |
<!-- /kernsize:themes -->

<!-- BEGIN generated table -->
| module | `.text` | `.cold` | code | `.bss` | `.lowbss` |
|---|---:|---:|---:|---:|---:|
| `wm.inc` — the window manager (§11) | 9,690 | — | **9,690** | 789 | — |
| `files.inc` — the Disk window (§22) | 1,078 | 8,158 | **9,236** | 471 | — |
| `disk.inc` — volumes, mount, the FAT read path (§18–19) | 358 | 6,034 | **6,392** | 890 | 3,584 |
| `vga12.inc` — the VGA planar primitives (§5) | 5,346 | — | **5,346** | 653 | — |
| `diskw.inc` — the FAT write path (§18.4–18.6) | 179 | 5,025 | **5,204** | 155 | — |
| `fdlg.inc` — the Standard File dialog (§38) | 223 | 3,950 | **4,173** | 139 | — |
| `mouse.inc` — serial mouse and the cursor (§9) | 3,632 | — | **3,632** | 149 | — |
| `menu.inc` — the menu bar and pull-downs (§12) | 3,019 | — | **3,019** | 197 | 98 |
| `driver.inc` — loadable drivers + `SYSTEM.CFG` (§51) | 410 | 2,584 | **2,994** | 341 | — |
| `assoc.inc` — file type associations (§54) | 517 | 2,327 | **2,844** | 43 | — |
| `ui.inc` — the UI task and the event ladder (§13) | 2,718 | — | **2,718** | 40 | — |
| `filecp.inc` — Cut/Copy/Paste (§22.3–22.5) | — | 2,306 | **2,306** | 139 | — |
| `memory.inc` — the claim heap (§50) | 14 | 2,014 | **2,028** | 14 | 256 |
| `instance.inc` — instances and the built-in kinds (§29) | 1,837 | — | **1,837** | 673 | — |
| `clock.inc` — the clock ladder (§37) | 1,794 | — | **1,794** | 89 | — |
| `font.inc` — the 8x8 text renderers (§6) | 1,632 | — | **1,632** | 197 | 768 |
| `icons.inc` — the icon renderer (§10) | 1,570 | — | **1,570** | 34 | — |
| `vidsel.inc` — which adapters the machine HAS, and switching between them (§39.11) | 1,395 | — | **1,395** | 84 | — |
| `apps.inc` — the three built-in kinds (§14) | 1,376 | — | **1,376** | 11 | 240 |
| `softgfx.inc` — the software renderer, §39.5's 1bpp driver (§32) | 1,205 | — | **1,205** | 4 | — |
| `snd.inc` — the sound layer (§34) | 1,195 | — | **1,195** | 300 | — |
| `sched.inc` — pre-emptive scheduling (§7–8) | 1,088 | — | **1,088** | 168 | 2,816 |
| `desk.inc` — the desktop and volume zones (§14/§26.1) | 15 | 1,052 | **1,067** | 18 | — |
| `splash.inc` — the boot splash (§15) | 961 | — | **961** | — | — |
| `fsx.inc` — fullscreen exclusive (§53) | 919 | — | **919** | 9 | — |
| `ctrl.inc` — the Control Panel (§31) | 652 | 194 | **846** | — | — |
| `viddet.inc` — adapter detection and geometry (§39) | 815 | — | **815** | — | — |
| `loader.inc` — the package loader (§21) | — | 802 | **802** | 58 | — |
| `dock.inc` — the dock (§30) | 793 | — | **793** | 34 | — |
| `toast.inc` — the menu bar's transient message (§59) | 547 | — | **547** | 25 | — |
| `fprog.inc` — the file-operation progress widget (§12.8) | 467 | — | **467** | — | — |
| `mod.inc` — on-demand kernel modules (§2.8) | 36 | 412 | **448** | 34 | — |
| `xmem.inc` — memory above 1MB (§41.4–41.5) | 269 | 96 | **365** | 22 | — |
| `clip.inc` — the system clipboard (§55) | 193 | — | **193** | 6 | — |
| `events.inc` — the event ring (§10) | 141 | — | **141** | 134 | — |
| `blank.inc` — **(undescribed)** | 124 | — | **124** | — | — |
| `cpudet.inc` — CPU tiers and the A20 gate (§41.1–41.3) | 10 | — | **10** | — | — |
| `kernel.asm` — API table, entry points, `kmain`, the shims | 3,021 | — | **3,021** | — | — |
| **total** | **49,239** | **34,954** | **84,193** | **5,920** | **7,762** |
<!-- END generated table -->

### Reading it

A few rows say something that is not obvious from the size alone. The
figures are the table's; what is here is the reason for them, so a row that
moves does not drag a paragraph out of date with it.

- **What `files.inc` keeps in `.text` is DATA, not code.** What stayed
  is the window template, the menu sets, every string and error table, and
  the two command-dispatch tables. Cold code reads all of it through DS
  (SPEC.md §2.6), so the split is code/data and nothing else — which is why
  `diskw.inc` keeps 20 bytes and `filecp.inc` and `loader.inc` keep none at
  all.
- **`clock.inc` is four clocks, and more than half of it boots away.** Each
  rung of SPEC.md §37.90's ladder is a different chip with a different
  register layout. Only one can ever run on a machine and there is no way to
  know which until the probe has walked them, so none of it can be *loadable*
  the way the sound tiers are — but the walking happens once, so all four
  probe-and-read halves are in the boot overlay. What is left in `.text` is
  the four writers (the Control Panel can set the clock all session), the
  software calendar the tick advances, and the formatters.
- **`kernel.asm`'s row is almost entirely tables of stubs**, and it is a
  RESIDUAL: whatever the per-module pass did not attribute lands there, so it
  is the row to look at first if a total ever looks wrong. The API
  jump table, its X and N stubs, and now 91 `cw_*` shims plus 42 resident
  thunks for the cold segment. That is the price of a package living in its
  own segment (SPEC.md §20.1) and of code living outside the kernel's, and
  both are paid once rather than at every call site.
- **`font.inc`'s 760-byte glyph table left the segment** (below), so its
  `.bss` is 17 bytes of `font_run` line state and nothing else.
- **`instance.inc` no longer keeps a copy of every package's icon.** It used
  to hold 768 bytes of `.bss` — one 64-byte body per instance, staged at load
  time — while the original sat in the package's own region at the fixed
  header offset the whole time, living exactly as long as the instance that
  owned it. `I_ICON` is a sentinel now and `inst_icon_ptr` stages 64 bytes
  only when a dock tile is actually drawn (the `dsk_get_icon` idiom), which is
  what paid for hard-disk support. Its remaining `.bss` is the record table.
- **`splash.inc` pays ~266 bytes for primitives that already exist.** It runs
  inside the first `SPL_RESIDENT` sectors, before `vga12.inc` is aboard, so it
  cannot call `gfx_*` and open-codes its own hline, vline and fill. That
  window is also why the cold segment's shim block sits *below* every
  `%include` in `kernel.asm` — at over 500 bytes it pushed the splash out of
  its sectors when it sat above them, and the build failed naming splash and
  nothing else.
- **`clip.inc` is 193 bytes** and `cpudet.inc` is 10 — the two smallest things
  with a chapter in SPEC.md. Not everything that has a name has a footprint.

### How to re-measure this

    tools/kernsize.py --modules          # look
    tools/kernsize.py --bless            # ...and write it back into this file

That is the whole recipe now; what follows is how it works, for whoever has
to change it. The tool brackets every `%include` in a **temporary copy** of
`kernel/kernel.asm` with a bare label in each of `.text`, `.bss`, `.lowbss`,
`.cold` and `.ovl`, and emits the differences through `%assign` + `%warning`
at the end of the file, where every label it names already exists. Four
things make it exact:

1. **Bare labels emit no bytes**, so no offset moves — and the tool does not
   take that on trust: it assembles the plain source and the instrumented one
   and **refuses to report unless the two binaries are identical**. Nothing is
   ever written to `kernel/kernel.asm`.
2. **The marker block ends in `section .text`.** Every `%include` in
   `kernel.asm` sits at `.text` scope and every module is required to switch
   back before it ends (SPEC.md §4), so the marker has to hand back what it
   was given or the module after it starts in the wrong section.
3. **The module deltas must sum to the section totals**, and the table's
   total row is those totals rather than a sum of its own rows — so a gap
   shows up as an implausible `kernel.asm` residual instead of quietly
   vanishing.
4. **Descriptions come from the table itself**, not from the tool, so the
   prose half of each row survives regeneration. A module the table has never
   seen is rendered **(undescribed)** and that placeholder is deliberately
   never read back as a description: write one.

The theme grouping is a judgement, so it lives in `THEMES` in the tool rather
than in this file. A module that is in no theme stops the report and names
itself.

For per-routine detail there is still no tool: take each label's address from
a `-l` listing — a routine's size is the distance to the next label — and
attribute by **address range**, not by the listing's `<1>` include markers,
because macro expansions are marked at include depth too.

---

## Moving data out of the segment, and where that stops

`KERN_CODE_MAX` counts `.text` + `.bss`. It does **not** count `.lowbss`,
which lives in `LOW_SEG` and is reached through SS — so a table moved from
one to the other hands its whole size back to the segment guard. `SS` is
`LOW_SEG` from `kmain` onwards and never changes again, so the access is an
`ss:` prefix with no register to set up, nothing to save and restore, and no
ordering hazard: one byte and about two cycles per field.

**It is not free on the footprint**, and that matters more now than when this
was written: `.lowbss` and the image are different rungs, each rounded to 512
bytes, so a byte that leaves the segment when `.lowbss` is full costs a whole
step until the image falls far enough to drop one.

Four objects made the trip — the stacks (2,816) and the disk buffers (3,584)
were never in the segment to begin with, and are `.lowbss` by design
(SPEC.md §2.1). What decides a migration is not size but **how many places
dereference the pointer**, which is not the same question as how many places
take its address:

| object | bytes | `ss:` prefixes | net | where |
|---|---:|---:|---:|---|
| `font_glyphs` (+ `font_zero`) | 768 | 5 | **+763** | `font.inc` only |
| `mem_tab` | 256 | 64 | **+192** | `memory.inc` only |
| `app_ball_pool` + `app_tmr_pool` | 160 | 37 | **+123** | `apps.inc` only |
| `menu_bar` | 84 | 34 | **+50** | `menu.inc` only |

Those four plus the two by-design blocks are the whole of `.lowbss`: 768 +
256 + 160 + 84 + 2,816 + 3,584 = 7,668, which is `KLOW_SIZE` exactly.

Three that were candidates on size alone did not go, and the reasons are
worth keeping:

- **`fm_pool` (80 B) is a net loss.** `[fm_vp]` points into it and the Disk
  window dereferences that pointer 111 times, so the prefixes cost more than
  the table is worth. Bytes-per-dereference is the metric, not bytes.
- **`inst_tab` (384 B) is entangled, not merely expensive.** Its field
  accesses are only the visible half: `I_NAME` is handed out as an ordinary
  near string pointer — a Disk window's `W_TITLE` aims *into* the record, and
  the dock, the menu bar and the Task Manager all letter it through DS.
  Moving the table would need a segment beside every one of those pointers,
  which is exactly the `MB_SEG` trap of SPEC.md §12.2. This was tried, and it
  failed the way that trap always does: the build was clean and the machine
  booted to a desktop that could not launch anything.
- **`snd_xlat` (256 B) is refused on speed, not entanglement.** Only two
  sites, but they are `spk_pcm_run`'s per-sample loop, where a prefix is not
  free the way it is everywhere else on this list.

**`font_glyphs` needed the ABI amended, and was worth it.** At 760 bytes
against five dereferences it is the best ratio in the kernel — better than the
other four together — but `OSAPI_FONT_GLYPHS` published it as an offset in
`KERNEL_SEG`, and SPEC.md §20.8 rule 4 says a shipped slot keeps its contract.
The cell answers `DX:SI` now, a recorded one-time amendment: exactly one
package reads it (Paint's text tool), it is in this tree, and `make` rebuilds
it.

The five dereferences are the glyph-row loops, so this one has a **measurable
run-time cost** where the others do not, and it was checked rather than waved
through. A segment override is one byte and 2 clocks on an 8088 — up to 4 if
the four-byte prefetch queue is starved, which in these loops it will be. Per
glyph the read runs eight times (once per row) on the mono adapters and in
`font_char`'s VGA path:

| | clocks/glyph | of a ~4,770-clock cell |
|---|---:|---:|
| mono — the 8088 target | 16–32 | **0.34–0.67%** |

The 1 ms a cell costs on a real 4.77 MHz XT with a Hercules card comes from
`tests/fontbench` (SPEC.md §6.1.1). Two thirds of one percent on the machine
this OS is for.

**There is no cheaper encoding.** `[bp]` would default to SS with no prefix,
but 8086 addressing has no `mod=00` form for BP — it assembles as `[bp+0]`,
so it is the same extra byte *and* a worse effective address (9 clocks
against `[si]`'s 5). `ss:` is the floor.

**The trap this sprang, and the one to expect next time.** A field-offset
regex finds `[di+I_STATE]`; it does not find `add di, I_NAME` followed by a
bare `[di]`, and it does not find a `rep stosb` whose ES was set with
`push ds / pop es`. Both exist, both assemble, and both write to the wrong
segment at run time. Every migration here had to be checked for three shapes,
not one: field accesses, bare dereferences of an advanced pointer, and string
operations whose segment register is set from DS.

---

## The boot overlay: code that costs no memory at all

Some of the kernel runs exactly once, from `kmain`, and is then unreachable
forever. `.ovl` is where that code goes, and it is the only rung on this
ladder that costs **nothing** — not RAM, not budget, and not the segment.

It works because of what the `FAT_SEG` window is doing at boot: nothing.
`disk_mount` is the only routine that writes it, and the earliest call is
`drv_boot` — the *last* thing `kmain` does before the first paint. So there
is a 4,608-byte hole in the middle of the kernel's own ladder that is live
for the whole of start-up and dead the instant the first volume mounts. The
overlay is **3,069 bytes** of it, with 1,539 spare:

| | bytes | |
|---|---:|---|
| `clock.inc` — the probe-and-read ladder | 1,839 | `clk_init`, `clk_probe`, `clk_commit` and all four rungs' read halves |
| `cpudet.inc` minus `cpu_info` | 314 | the tier test and the whole A20 gate. `cpu_info` stays: it is API slot 0x0188 and answers all session long |
| `xmem.inc` — `xm_init` | 123 | sizing the store is a once. `xm_arm` stays resident — `xm_copy` re-arms unreal mode inside the window that uses it — so it gets a shim |
| `snd.inc` — `snd_init` | 107 | saving the boot 61h bits and publishing `snd_live`. `snd_unhook` is the shutdown path and stays |
| `disk.inc` — `dsk_fdd_probe` | 398 | asking the FDC whether a unit is really there (SPEC.md §18.97), retiring drive B's volume row if not, and filling that unit's row of §57.5's published block. `make FDDPROBE=0` takes it out |
| `desk.inc` — `desk_init` | 122 | counting volumes and laying out their zones, and the 21 bytes that contest the count against the probe above. `desk_ord` and `desk_zone_label` are called by the runtime painters and stay |
| `kernel.asm` — the entry stubs | 24 | |

**The rows are hand-kept and the total is measured, so they do not sum** —
they are short by ~158 bytes that predate this note. Trust the total and the
spare; treat a row as "roughly what this module put here".

**The number to watch is NOT the overlay's spare, it is the IMAGE's last
sector.** `kernel.bin` is **88,115 bytes** and the boot sector reads
`(size + 511) / 512` = **173** of them, which hold 88,576 — so there are
**461 bytes** of slack in the file, and once that is gone the next thing added
to `.ovl`, however small, costs a whole sector of boot read (~65 ms on the
field machine). `tools/kernsize.py` reports the three *rungs* and not this,
because the rungs are what the RAM ladder is built from; the file's tail is a
separate question and this is where it is written down.

It was under 100 bytes at four consecutive measurements of one round — 8
before §18.97's probe, 5 after it, 5 again after `font_run` crossed an image
rung underneath it, 3 after §18.97.1 — and then a rung boundary moved and it
is 461. That swing is the point, and it is worth reading as a pattern rather
than as a run of coincidences: **`.text` and `.ovl` land in the same file and round at
different places**, so the tail's slack is not a budget anyone is steering
and it can be spent to nearly nothing by a change that never touches the
overlay at all. Re-measure it; do not carry a figure from a commit message.

`.ovl` is declared `start=OVL_START vstart=0`, and both halves matter.
`start=` is the *file* offset, so NASM emits the gap as zeros and the boot
sector's existing single read lands the overlay exactly on `FAT_SEG`. No
second read loop, no gap constant, and the splash's progress bar still spans
the whole load because there is still only one total to span. `vstart=0`
makes the overlay's own labels offsets from `FAT_SEG`, so
`call FAT_SEG:ovl_cpu_detect` resolves at assembly time.

**It is one assembly, and that is the whole trick.** A separate build would
not know where `cpu_tier`, `xm_kb` or the eighteen `snd_*` words live, and
every one would have to be marshalled through a hand-written ABI. Because
`.ovl` is a section of the same source, every kernel symbol resolves normally
— and because the overlay runs with **DS = KERNEL_SEG**, those references
execute exactly as they did in `.text`. Nothing was rewritten.

The contract is `CS = FAT_SEG, DS = KERNEL_SEG, SS = LOW_SEG`, with one sharp
edge: **the overlay may not reach its own labels through DS.** It has no data
of its own today; anything added needs a `cs:` override, and NASM will not
warn.

**`drv_boot` must not go in it**, though it is single-call and looks like a
perfect candidate: it would overwrite itself mid-execution, because
`disk_mount` is what fills `FAT_SEG`. That is the edge of the idea.

Guard 4b holds the overlay to the FAT window it is read into; guard 4c
refuses an empty one, because every `FAT_SEG:` far call in `kmain` would then
land in whatever the FAT buffer happens to hold.

### How the clock's split was decided

`clock.inc` is the largest thing in the overlay and the only one that was not
two `section` lines. It interleaves each rung's *read* helpers with its
*write* helpers, and the writers stay resident because the Control Panel can
set the clock all session — so the boundary had to be derived rather than
eyeballed. It was: build the module's call graph, take everything reachable
from `clk_init`, subtract everything reachable from the six symbols called
from outside the module (`clk_tick`, `clk_snapshot`, `clk_fmt`,
`clk_fld_str`, `clk_fld_adj`, `clk_rtc_write`), and what is left is movable
by construction.

That answered 26 routines in eight non-contiguous runs — and five helpers
that both halves use and which therefore cannot move: `clk_at_get`,
`clk_at_done`, `clk_ns_put`, `clk_ns_stamp`, `clk_rp_get`. It also settled
two that look like they could go either way: `clk_bcd` moves (only the read
paths decode BCD; the writers use `clk_tobcd`), and `clk_commit` moves (only
`clk_probe` calls it).

**`tools/os88ovlchk.py` exists because of this change**, and it is now the
gate for `.cold` as well.

---

## Cold code: resident, but not in the segment

The boot overlay works because its code is *transient*. Most cold code is
not: the Control Panel has to be there whenever the user opens it, and the
file manager whenever a window is clicked. `.cold` is for that — a second
code segment, resident for the whole session, that `KERN_CODE_MAX` cannot
see.

**This is `.fartext` returning, and both reasons it was retired have
inverted.** It died (SPEC.md §33) because the mechanism needed a fixed
10,752-byte reservation to hold a 5,455-byte blob, and because the number
being steered by was the *footprint*. Today the ladder is derived —
`COLD_PARA` is the size rounded to 512 with no slack at all — and the segment
was the guard with no room. Nothing is copied and nothing is reserved.

The six tenants are `files`, `diskw`, `fdlg`, `ctrl`, `filecp` and `loader`,
and **their sizes are the `.cold` column of the generated table in "Where the
code goes"** — stated there and nowhere else, because this table used to
restate them and was 268 bytes and one missing module out of date by the time
anybody noticed. `tools/kernsize.py` prints the rung and its remaining slack.

`fsx.inc` was a seventh tenant at 190 bytes — SPEC.md §53.6.1's XMS desktop
stash, the one cold module that was not part of the file system or the
Control Panel — and its removal is what took this rung from 40 × 512 to 39.

It shares the overlay's contract exactly — **CS = `COLD_SEG`, DS =
`KERNEL_SEG`** — and that is again what makes it cheap. Every data reference
in a cold module is unchanged, because the data did not move.

**The five file modules went together, and that is the design.** They mostly
call each other, so a call inside the set stays near and only the ones that
leave it pay a shim. Growing the set makes what is already in it *cheaper*:
`fdlg.inc` joining turned its calls to `fm_ultoa`, `dskw_mkdir` and
`dskw_char` from a double crossing — cold, out to a resident thunk, back into
cold — into near calls, and retired those three thunks.

**A second round added five more, and the reason to record it is what it was
steered by.** `.text` + `.bss` had reached **65,065 of 65,536** — 471 bytes
under the guard that cannot be raised — so the question stopped being "what
would be tidy cold" and became "what is genuinely cold, in cadence rather
than in size". The answer was `assoc.inc` (a double-click), `disk.inc` (a
mount, and everything in it bounded by a floppy at ~24 ms a sector),
`driver.inc` (a boot and a Control Panel page), `memory.inc` (a claim) and
`desk.inc` (a drive-zone click). **Segment headroom 471 → 12,698, at a cost
of two 512-byte steps of `KERN_BUDGET`** — which is the trade §2.6 always
makes, and the reason the spare in the table above is two steps rather than
four.

What it also produced is three new build refusals in `tools/os88ovlchk.py`,
because three of the four rules below were broken during the round and every
one of them assembled, booted, and failed somewhere else: a CS assumption
cost a mount, seven colon-less data lines cost a freeze, and a tail call to a
`cw_` shim cost another. **The deliberate non-candidates are worth as much as
the list**: the drawing primitives and both ISRs are excluded on cadence, and
`splash.inc` and `viddet.inc` are excluded structurally — they must be
resident inside the image's opening sectors, and the cold segment lands after
the image rung.

Four rules hold it up, and every one of them describes something that
assembles cleanly and runs wrong:

- **Data stays in `.text`.** DS is still `KERNEL_SEG`, so a string or table
  that moved with its code would be read at the wrong segment. A module with
  data islands toggles sections around each; `files.inc` does it four times.
- **Nothing may take a kernel segment from `CS`.** `push cs`/`pop es` and
  `[cs:x]` are the two spellings. `loader.inc` had three, including a
  documented one — the far pointer it calls a package's dispatcher through,
  which reads through ES now because that is the only register still naming
  the kernel at that point. `files.inc` had a fourth, whose own comment said
  *"this code is .text, so CS is the kernel"*.
- **A `.text` table of cold pointers is fine only if cold code alone
  dispatches through it.** There are four: `ctrl.inc`'s page table, and
  `files.inc`'s `fm_jmp` and two `fm_ctx_*` sets. The mirror rule is what
  broke first — a table `.text` *does* dispatch through must name the
  **thunk** and not the `_x` body, which is what `fm_tpl`, `fm_menus` and
  `fdlg_tpl` do.
- **A macro argument is a call site.** `OSAPI_SLOT dskw_dfree` near-calls its
  argument from inside the macro body, and six of those pointed into these
  modules. `os88ovlchk.py` reads the source, so it saw none of them until it
  was taught the cell macros.

The wiring is four-byte `cw_*` shims outward and six-byte resident thunks
inward, the thunk keeping the **public** name and the body taking an `_x`
suffix — so no caller outside a cold module changed, including the
`OSAPI_SLOT`/`OSAPI_NSTUB` cells. `wm_pkgcall` sets DS from `W_SEG`, which is
the wrong contract for cold code, which is why window callbacks go through
thunks rather than through the window record.

---

## The one lever that moves both guards

`.cold` and `.ovl` relieve the segment. Nothing relieves the footprint any
more except doing less. There is one precedent for doing less, and it is the
Task Manager.

> **A second lever exists now and the first thing has gone through it** —
> SPEC.md §2.8, docs/ONDEMAND-PLAN.md. The **Control Panel** is a file
> (`CTRL.DRV`, 3,185 bytes) read into a heap claim when it is opened and freed
> when it closes: measured, `KERN_SIZE` **102,912 → 100,352, −2,560 bytes**,
> five 512-byte steps, on both builds. What is left in the table below for
> `ctrl.inc` is its DATA and the two routines that could not go
> (`cp_tick_due`, `cp_drv_gone`) — 918 bytes against 3,876.
>
> **The floppy formatter went through next** (`FORMAT.DRV`, 802 bytes), and
> its payoff is the other kind: it was `%ifndef KERN_SMALL`, a whole feature
> compiled out, so **the 128KB machine now HAS a formatter it never had** —
> and `kern_small` came out of the same round at **97,280 → 96,256 with 1,024
> spare**, against the zero it stood at. `kern_big` is **102,912 → 99,840**.
>
> **Neither is free against the other guard, and that is the number to watch
> now.** `.text` + `.bss` is **65,375 of 65,536 — 161 bytes left**. The
> dispatch really is free (the `.text` thunks did not change), but the loader's
> five `cw_*` shims, two thunks, the module table and — mostly — the four
> refusal and prompt strings are `.text` all the same. **The next addition to
> `.text` or `.bss` anywhere will need one of the levers in this document**,
> and this is the guard nobody can raise.
>
> **On-demand kernel modules**, in the original note's words: It is this one without the
> published ABI: `.cold` is already `vstart=0`, contains **zero data
> directives**, runs with `DS = KERNEL_SEG` and calls out only through the 107
> `cw_*` shims, so cold code is already position-independent at paragraph
> granularity and could execute from a heap claim unchanged. Making the *cold*
> thunk load-and-dispatch spends **no `.text`**, which is the guard with 438
> bytes left.
>
> **What may go is decided by the user and not by the seam**, which is that
> document's ONDEMAND-PLAN §1 and the thing to read first: a feature qualifies
> only if the system disk is already required to use it, or can be required
> without interrupting what the user was doing — on a one-floppy machine every
> load is a disk swap. Two candidates survive it. The **Control Panel**, worth
> **3,072 bytes on both builds**, whose precondition is one it already has
> (`cp_flush_close` writes `SYSTEM.CFG` to that disk). And **Disk Format**,
> which is not a saving at all: it is compiled out of `kern_small` by
> `%ifndef KERN_SMALL`, so on demand **gives that build a feature it does not
> have**, funded by the panel's 3,072. MS-DOS drew the same line — `COPY` in
> `COMMAND.COM`, `FORMAT` external — and the file dialog and Cut/Copy/Paste
> fail it for `COPY`'s reason.
>
> The passage above says the Control Panel is *"the window you want when a
> driver will not attach"*, and it is right that this is the objection to
> weigh; ONDEMAND-PLAN §2.2 weighs it and finds it survivable, because
> `drv_notice` runs before `ui_task` starts and so before any disk can have
> been swapped.

A cold segment would have taken about 4,900 bytes off `KERN_CODE_MAX` and
**nothing** off `KERN_BUDGET`. Making it a package on the system disk took
6,040 off *both* — the span went 76 KB → 69 KB and the segment gained 5,380 —
and the memory it uses is now spent only while the window is open. SPEC.md
§28 has the design; what is worth recording here is the shape of the
exchange.

**It was the only built-in that could not be lifted out**, and every reason
was one reason: it read `sch_cycles`, `sch_tasks`, `sch_cur`, `inst_tab`,
`mem_tab` and seven assembly-time constants of this very ladder directly,
because it was kernel code and could. Nothing else in the tree wanted any of
that, so no API slot had ever been written for it. SPEC.md §20.9's four cells
are that API — three table snapshots into a caller-supplied buffer and a
patterned fill — and they cost 1,240 bytes of kernel to save 6,279.

The step order mattered and is worth copying: the cells were added **first**,
with the module still built in and converted to use them, and only then did
the module move. That way the API was proved sufficient while the code was
still somewhere a debugger could reach, and a missing field showed up with
everything else unchanged.

| | `KERN_CODE_MAX` | `KERN_BUDGET` |
|---|---:|---:|
| four API cells, module still built in | +1,240 | +1,024 |
| the module leaves (`taskmgr.inc`, 6,279) | −6,279 | −5,632 |
| `ui_tm_open`, `ui_note` and `dsk_find_name` | +341 | +512 |
| unwiring the kind, its icon and `tm_init` | −682 | −1,024 |
| **net** | **−5,380** | **−5,120** |

(The `KERN_BUDGET` column moves in 512-byte steps because the image rung
rounds to whole sectors, which is why its arithmetic does not match
`KERN_CODE_MAX`'s row for row.)

The cost is real and worth stating in the same place as the saving: opening
the Task Manager needs a working disk and about 8 KB of free heap, on the
machine where you are opening it precisely because something is wrong. Two
things make that acceptable. The **Control Panel** — the window you want when
a driver will not attach, and where `drv_notice` sends you — is cold and
therefore still resident. And the failure is not silent: the chip menu's item
stays live (SPEC.md §47 rule 3 — the only honest test is the load itself) and
puts up a notice naming the reason.

---

## History

| change | budget | kernel footprint |
|---|---:|---:|
| before any of this (v1.0.20260728) | — | ~107 KB |
| low memory sized to measurement, kernel moved to 0x0800 | 64 KB | 75 KB |
| `.fartext` retired, ladder derived, buffers trimmed, kernel at 0x0060 | 64 KB | 63.5 KB |
| raise 1 — the SPEC.md §41 XMS store | 70 KB | 66 KB |
| raise 2 — the SPEC.md §51 driver subsystem | 71 KB | 70.5 KB |
| raise 3 — SPEC.md §51.5's keyed `SYSTEM.CFG` | 75 KB | not recorded |
| raise 4 — SPEC.md §22.3/§22.4 Cut/Copy/Paste and the drag | 79 KB | not recorded |
| hard disks as a driver (§18.7/§18.8/§51.2.1) — budget **not** raised | 79 KB | 78.5 KB |
| `.lowbss` migration + 256-byte task stacks | 79 KB | 76.5 KB |
| the glyph table follows it out (§6/§20.3) | 79 KB | 76 KB |
| the boot overlay: the image padded to its rung, four boot-only routines out | 79 KB | 76.5 KB |
| the clock's probe-and-read ladder follows them (§37.90) | 79 KB | 74 KB |
| the Control Panel's code into a cold segment (§2.6) | 79 KB | 74 KB |
| four API cells for what only the kernel could see (§20.9) | 79 KB | 76 KB |
| the Task Manager becomes a package on the system disk (§28) | 79 KB | 69 KB |
| the copy pipeline, write runs and a FAT window per volume (§18.9/§18.8.1/§22.5) | 79 KB | 69.5 KB |
| the guards renamed, and `KERN_BUDGET` lowered onto the kernel (move 5) | **72.5 KB** | 71.5 KB |
| raise 6 — `gfx_line` (§5.6) and the file dialog's size-before-load (§38.6) | 74.5 KB | 73 KB |
| raise 7 — file type associations (§54) and the disk path, costed in advance | 76.5 KB | 74.5 KB |
| raise 8 — §54.4.1's notice, plus §18.92/§18.93/§18.4.2 | 78.5 KB | 76.5 KB |
| raise 9 — the five file modules into `.cold` (§2.6) | **80.5 KB** | 78 KB |
| ...the elendilon merge, and where it stood then | 80.5 KB | **78.5 KB** (80,384 B) |
| moves 10–17 — see `KERN_BUDGET`'s own comment in `kernel/kernel.asm` | 80.5 → 98 KB | — |
| raise 18 — SPEC.md §62's network driver, **`kern_big` only** | **100 KB** | 97.5 KB (99,840 B) |

**Moves 10 through 17 are deliberately one row.** The table was written when
there was one guard and the story fitted a line each; since the kern_big /
kern_small split there are two figures per move and the reason for each is a
paragraph, so the history that is maintained is `KERN_BUDGET`'s own comment in
`kernel/kernel.asm` — which says, per move, what was asked for, what was
granted, what spent the previous step and which guard moved. A summary table
that has to be updated in a second place is a summary table that goes quietly
wrong, and this one had: its last row read 78.5 KB against a kernel of 97.5.

Every footprint from move 5 down was **measured at the commit where that
raise took effect on `elendilon`**, by bisecting `KERN_BUDGET` in a throwaway
worktree at that revision; the rows above it are from the commit messages of
the time and two of them were never recorded at all. The last row is the one to re-measure rather than
trust: it moves with every commit that adds code, and it is not the budget —
it is what the budget is being spent on. "Where it goes" carries the same
figure to the byte, and the Task Manager's `System` row shows it live.

Three things the shape of this table says.

**A raise lands with the commit that first needs it**, and the measurements
say so almost too neatly: raises 7 and 8 each landed on a kernel of 76,288
and 78,336 bytes — *exactly* the budget then in force, to the byte. Raise 6
landed 512 over it, which is the same thing one step later. Only raise 9 was
granted with room to spare, because it was asked for ahead of the work rather
than by a build that had already failed. The slack after a raise was 2,048
bytes every time through move 9, and every time it was spent. **Move 10 is
the exception on both counts**: 4,608 bytes, and unspent as this is written,
because it was granted for work that has not landed yet.

**The footprint has grown 8,704 bytes since the low point at move 5** —
73,216 to 81,920 — and every one of those kilobytes was a feature that was
asked for.

**The budget caught up with the boot sector at move 9**, and for one release
the next row could not have been a raise at all. Moving the sector to the top
of RAM (SPEC.md §2.7) ended that, and move 10 is the first row taken against
the room it opened — on the same terms as the nine: asked for, granted, and
owed to something named. The row after the row that reaches **126,976** is
not a raise either, and not a deletion: it is a second kernel.

`docs/MEMORY-PLAN.md` is the narrative of how it got here, step by step, and
what was rejected along the way. This document is what it looks like now.
