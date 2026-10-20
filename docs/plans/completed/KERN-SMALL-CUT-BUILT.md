# What the `kern_small` cut actually took

**BUILT, and this is the record.** It is the completed half of
`docs/plans/KERN-SMALL-CUT-PLAN.md`, which is still open: that document is the
menu of what a 128KB machine could *further* stop carrying, and this one is
what has already been taken off it. The two were one document until the rows
below closed; they are split because a built row and a proposed row read
identically in a table, and every row here had stopped being a proposal.

**The headline is one number measured on a real 128KB machine.** A `kern_small`
desktop had **32.5 KB** of free heap when the study opened and has **50.5 KB**
now (`tests/small128.py`, MartyPC `os8088_5150_cga_128k`) - every other profile
in this tree has 640KB, so `MIN_RAM_KB` had been an arithmetic claim rather
than a measurement since the day it was written. `kern_big` moved by 512 bytes
across the whole batch, D4's, the only item here it shares.

What was taken: **A3** (loadable drivers, SPEC.md §51.0), **A4** (`disk.inc`),
**A2** (the clock ladder, SPEC.md §37.0.1), the dead half of **A1**, **C3**
(associations gated out, SPEC.md §54.0), **D1/D2/D4/D7**, and **B5** (the icon
pool, SPEC.md §25.8) - which was not on the original list at all.

**Three findings outlive the rows**, and they are why this file is kept rather
than summarised into a line of the plan:

1. **SECTIONS ARE NOT HEAP** (§2). 3,632 bytes of sections bought 2,560 bytes
   of heap, and the difference is not rounding - so every per-feature row in
   the open plan is an *upper bound* on free heap rather than an estimate of
   it.
2. **In a kernel with overlays, a byte's value depends on WHERE it is** (§3).
   A2 was refused on a correct measurement of the wrong quantity - forcing one
   clock rung is 44-51 bytes - and then taken anyway, because `.ovlw` is what
   caps three of the biggest data cuts and the clock was 40% of it. Gating the
   ladder unlocked D2, which is 3,584 bytes: seven times what A2's own row
   claimed.
3. **A claim is not a section** (§4). B5's second half is 1,024 bytes per open
   Disk window and never appears in `kernsize` at all. Per-instance claims are
   the lever the open plan has still not counted.

**And a reporting trap worth not repeating** (§2): `kernsize`'s `sum` is a
delta against the *blessed baseline*, not against the tree you started from, so
a reading taken with a stale baseline reports three waves as one and reads
exactly like an increment. Bless after a wave lands; measure tree-to-tree when
the number is going into a document.

> **The 70KB target this work was commissioned against is RETIRED**, and the
> reason is in docs/plans/KERN-SMALL-CUT-PLAN.md §8.2 and
> docs/plans/KERN-SMALL-CUT-PLAN.md §0.1: it was derived from SHEET's
> region, SHEET claims ~100KB of heap on open - more than the machine has - so
> no row in the study ever ran it. The brief is open-ended now.

---

## 1. A3 and A4 ARE BUILT, A2 IS REFUSED, A1 IS DEFERRED

**A3 is SPEC.md §51.0 and A4 is in `disk.inc`.** Measured tree-to-tree across
both:

```
             before    after    delta
.text        39,731   39,272     -459
.bss          5,417    4,848     -569
.cold        27,215   25,602   -1,613     } -2,641 IN THE LADDER
.ovl          1,226      423     -803     } -991 boot overlay
.ovlw         4,516    4,328     -188     }
                                -------
                                 -3,632   ->  HEAP -2,560 = 2.5 KB

KERN_SIZE   88,064 -> 85,504          heap floor 87.5 KB -> 85.0 KB
free heap on a 128KB machine, MEASURED on one:   40.5 KB -> 43.0 KB
kern_big                                          byte-identical
```

## 2. The finding this row exists for: SECTIONS ARE NOT HEAP

**3,632 bytes of sections bought 2,560 bytes of heap, and the difference is
not rounding.** 991 of them are `.ovl`/`.ovlw` — boot-overlay code loaded into
memory the machine reuses once it is up — so they move `KERN_SIZE` and the
boot-time minimum and move `HEAP_SEG` by *nothing*. Only 81 bytes went to the
512-byte rung rounding.

**That makes this document's whole method optimistic**, and it is worth saying
plainly before anything else here is decided off it. docs/plans/KERN-SMALL-CUT-PLAN.md §2-§5 price features by
adding up `.text`, `.cold` and `.bss` off a symbol map. That is the right
answer for **footprint**, and it is the wrong answer for **free heap** whenever
any of the bytes are in an overlay: the ladder is `.text+.bss`, then `.cold`,
and nothing else in the sum reaches it. A row should be read as an upper
bound on heap, not an estimate of it.

The resident half of the estimate was **good**: A3's `.text`+`.bss`+`.cold`
was priced at 2,550 and came in at **2,277**, 12% *high*. What the row missed
was the 991 bytes of overlay — and those are exactly the bytes that buy
nothing.

**And there is a reporting trap behind it worth not repeating.** `kernsize`'s
`sum` is a delta against the **blessed baseline**, not against the tree you
started from. The baseline had not been blessed since before W1, so a reading
taken after A3 reported `-6,529` — W1 and W2 included — and it reads exactly
like an increment. Bless after a wave lands, and measure a change
tree-to-tree when the number is going in a document.

**A2 is refused, on a measurement and on a judgement.**

The measurement first, because it settles the row on its own. `clock.inc`
already has the mechanism this row describes — `CLK_FORCE`, the `RTC=` knob —
and forcing a single rung is worth **44 to 51 bytes**, not ~510:

```
full ladder      .ovlw 4,328        (-DCLK_FORCE=n, kern_small, .text/.bss identical in all five)
rung 1 only      .ovlw 4,277   -51
rung 2 only      .ovlw 4,277   -51
rung 3 only      .ovlw 4,284   -44
rung 4 only      .ovlw 4,279   -49
```

`CLK_TRY` appears five times and all five are inside `clk_probe`: it gates the
**probes**, in the boot overlay. The per-rung **read and write** bodies are in
`.text` and are selected at run time off `[clk_tier]`, so they are not gated
by anything and this row's `~450 .text` has no mechanism behind it. Getting
that 450 would mean building a second gate over ~20 bodies.

And it should not be built, because the row picks the wrong rungs. **Rungs 2
and 3 are XT clock cards** — a National MM58167 or a Ricoh RP5C01 on a card at
2C0h — which is precisely the add-on the machine this build exists for would
have. Rung 1 is the MC146818, and *that* is the AT-only part. So the row's own
reasoning ("a 5150 has no RTC") argues for dropping rung 1 and keeping 2 and
3, which is the opposite of what it proposes and is worth ~51 bytes.

**A1 is deferred at the owner's instruction** — *"keep pc speaker for this
round - we may cut it later, but for now."* Worth recording for whoever picks
it up: A3 has already made **part of it dead**. The FM and Sound Blaster tiers
are reached through `SOUND.DRV`, so with no driver loadable the `SND_RT_FM`
and `SND_RT_SB` routes, `snd_str_busy` and the stream API can never be
selected on `kern_small`. What the speaker actually needs is the tone path and
`snd_xlat`'s 256 bytes of PCM rescale — so A1 splits, and the half that is
already unreachable is the cheaper half to take.

**A3 is the one to think hardest about.** It is the largest item here and it is
not a device — it is the ability to load one. A `kern_small` machine with no
drivers cannot gain a RAM disk, a hard disk, a screen saver or a network later,
and `SYSTEM.CFG` stops being read. Against that: on a 128KB machine there is no
heap to host a driver in anyway, which is close to an argument that the
mechanism is already unusable there rather than merely unused.

---

---

## 3. A2 was refused and then TAKEN, for a completely different reason

§1 above refused it on a measurement that stands: `CLK_FORCE` already exists, and
forcing one rung is worth **44–51 bytes**, not ~510. That measurement was of
the wrong thing.

**The owner settled the hardware question, and it settles all four rungs:**

> *"if they have a sixpakplus then they have more than 128kb ram. The
> sixpakplus is a ram expansion card. And the first thing that had the toshiba
> clock shipped with 256kb ram so its not really valid either."*

That is right, and it is stronger than this document's reasoning was. Rung 1
is AT-only; rung 4 is `int 1Ah AH=02h`, which §37.90's own opening says an XT
BIOS does not implement; and rungs 2 and 3 — the add-on cards that looked like
*exactly* the XT upgrade path — are on boards that came with the RAM that
takes the machine off this build's floor. **No rung is reachable on a 128KB
machine**, so the ladder is not unlikely there, it is dead code. SPEC.md
§37.0.1 is the contract.

**And what it is worth is not its own bytes.** `.ovlw` went **4,328 → 2,789**,
and `.ovlw` is what docs/plans/KERN-SMALL-CUT-PLAN.md §7 caps three of the most attractive data cuts against:

```
                              .ovlw   rounded   region at 2 FAT sectors
before gating the ladder      4,328     4,608   4,352   -> D2 REFUSED by 256
after                         2,789     3,072   4,352   -> D2 fits, 1,280 spare
```

So the clock unlocked **D2**, which is 3,584 bytes of `FAT_SEG` — seven times
what A2's own row claimed — and the measurement in §1 could not have seen
that because it was measuring footprint and this is a **placement**
constraint. Worth keeping as a caution against the next row that looks small:
in a kernel with overlays, a byte's value depends on where it is, not only on
how big it is.

## 4. The batch, measured

A3 + A4 + A1's dead half + A2 + D1 + D2 + D4 + D7, all built:

```
                        KERN_SIZE   heap floor   free heap on 128KB
before this work           96,256      95.5 KB         32.5 KB
after W0-W2 (modules)      88,064      87.5 KB         40.5 KB
+ A3, A4                   85,504      85.0 KB         43.0 KB
+ A1 dead half             84,992      84.5 KB         43.5 KB
+ A2, D1, D4, D7           82,432      82.0 KB         46.0 KB
+ D2                       78,848      78.5 KB         49.5 KB
+ the icon pool (B5)       77,824      77.5 KB       * 50.5 KB *
```

**...and B5 has a second half that is not in that column at all**, because it
is HEAP rather than footprint: the per-window view cache (SPEC.md 22.6.1)
carried the same duplication one layer out - 2KB of its 3KB was one icon body
per entry, in EVERY open Disk window's private claim. Pooling it too takes
`VIEW_KB` 3 -> 2 for `.cold` +34, which is **1,024 bytes per open window and
4,096 with all four up** (SPEC.md 25.8.5). Measured on the 128KB machine: a
Disk window's claim reads 2,048 bytes where it read 3,072.

That one is worth more than its size suggests and is easy to miss in a table
of section deltas, because a claim is not a section: it never appears in
`kernsize` at all. The lever it points at for the rest of this document is
**per-instance claims**, which nothing here has counted.

**50.5 KB, measured on a machine with 128KB in it** (`tests/small128.py`), and
`kern_big` moved by 512 bytes — D4's, the only item here it shares.

**B5 is the row that was not on the list**, and it is the shape docs/plans/KERN-SMALL-CUT-PLAN.md §3 was looking
for and did not find: `disk_icons` is 2,048 bytes of `.lowbss` holding one
64-byte body per directory entry, and a listing does not have 32 distinct
icons — every folder is the same picture and most entries have none at all.
SPEC.md 25.8 makes it a **16-body pool with a 32-byte index**, 1,056 against
2,048, and the low rung uncrosses twice: `.lowbss −992`, `.cold +148`,
`.text +2`, **KERN_SIZE −1,024**.

It is the only cut in this document so far that **loses no feature and no
picture**: the seventeenth icon in one listing falls back to the generic icon
§25 already draws, and the A/B against the kernel before it is **0 differing
pixels of 2,740** in both a folder window and a package window. What made it
non-trivial is not the pool, it is that `files.inc` is a **second reader** of
the same array with a slot-per-entry layout baked into a per-window cache —
so the pool stops at `fmv_store`, which expands into that cache rather than
copying it (SPEC.md 25.8.2). Getting that wrong was 301 differing pixels in
exactly one of the two windows.
