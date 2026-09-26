# The Last Drop Of Bytes


> **A REGISTER, NOT A PLAN WITH AN END.** Every row here is deferred
> indefinitely: something the machine COULD do, priced, with the reason it is
> not taken today. It lives under `docs/plans/` because a row is a proposal
> rather than a description of the tree — but it is *outstanding forever* by
> design, and it is finished when it is empty, which is not a state anyone is
> working towards. **Nothing here is a commitment and no row is owed.**
>
> Read it the way it is written: before spending a byte, to see whether the byte has already been
> priced — and before proposing a cut, to see whether it is already in
> §7's register of things refused with the arithmetic attached.

> Companion: **docs/plans/LAST-DROP-PERF.md** is the same idea for CYCLES — optimisations
> built, measured, found correct, and priced. This file is BYTES.

**A MENU OF BYTES THE MACHINE COULD STILL GET BACK — what is left, what each one
costs, and why it is not taken today.**

Every row here is a thing nobody has done. Read it before spending a byte, before
proposing a footprint saving of your own, and before re-deriving anything in §7:
that section is the list of bodies and merges that *look* available and are not,
each with the exact reason, so that the second person to notice them spends five
minutes rather than an afternoon.

> ### What this file used to be, in one paragraph
>
> It was written as a REGISTER of 22 boot-only bodies that could move into `.ovl`,
> at a moment when the blob had 194 bytes free and none of them fitted. The size
> pass then took `BOOT2_SECS` 13 → 19 (SPEC.md §2.9.12) and landed **18 of the 22**,
> plus SPEC.md §9.4.7's 1,024-byte mouse subset; `KERN_SIZE` came down 7,168 bytes,
> measured at `46e40a9`. What is below is the **remainder**, re-measured on this
> tree. The roll-call of what landed is kept only because about nineteen comments
> in `kernel/` cite these bodies by their row number and would otherwise point at
> nothing:
>
> **READ `.ovl` IN THIS TABLE AS "THE BOOT OVERLAY"**: SPEC.md 2.5.3 split it
> into `.ovl` (the blob, dead at `spl_finish`) and `.ovlw` (the FAT window,
> dead at the first mount) long after these rows landed, and most of them are
> in the window half today. Where the two builds now disagree the row says so.
>
> | row | body | where it is now |
> |---:|---|---|
> | 1 | `drv_boot_x` | `.ovl` |
> | 2 | `vid_probe_avail` + `vid_memchk` + `vid_cga_alias` | `.ovlw` on `kern_big`, **`.ovl` on `kern_small`** — SPEC.md 2.5.3.2 |
> | 3 | `sched_init` | `.ovl` |
> | 4 | `dsk_boot_from_x` + `dsk_bootltr` | `.ovl` |
> | 5 | `xm_boot_x` | `.ovl` |
> | 6 | `files_init_x` | `.ovl` |
> | 7 | `font_init` | `.ovl` |
> | 8 | `drv_init_x` + `drv_svc_clear_all` | `.ovl` |
> | 9 | `dsk_flop_add_x` | `.ovl` |
> | **10** | **`vid_detect`** | **`.text` — §2.1** |
> | 11 | `mem_init_x` | `.ovl` |
> | 12 | `dsk_dpt_init_x` | `.ovl` |
> | 13 | `vid_ctx_init` | `.ovl` |
> | 14 | `wm_init` | `.ovl` |
> | 15 | `dock_init` | `.ovl` |
> | 16 | `sch_idle_start` | `.ovl` |
> | 17 | `menu_init` | `.ovl` |
> | 18 | `inst_init` | `.ovl` |
> | ~~19~~ | ~~`loader_init_x`~~ | **GONE — deleted outright in kernel size pass 3, §2.2**: all four of the loader's resting values were already zero |
> | **20** | **`mod_init_x`** | **`.cold` — §2.3** |
> | ~~21~~ | ~~`evq_init`~~ | **GONE — deleted outright in kernel size pass 3, §2.2** |
> | **22** | **`vid_init`** | **`.text` — §2.1** |
>
> `tests/ovlrefs.txt` is the live enforcement: every reference from outside `.ovl`
> into it, with the reason each is safe, checked by `tools/os88ovlchk.py`. That
> file, not this one, is what stops a landed row gaining a runtime caller.

---

## 0. The rule that decides every `.ovl` row

**`.ovl` is released at `spl_finish`, so a body with a single post-boot caller is
disqualified completely. There is no partial credit.**

The disqualifier to hunt is a body reached from `ui_task` on any pass, or from an
ISR, or from a published `OSAPI_*` slot, or from a pointer stored in a table that
outlives the boot. A body that is 99% boot-only and 1% reachable afterwards is a
freed heap claim being executed — the `desk_pdisk`/`desk_phdd` freeze whose story
`tools/os88ovlchk.py` carries in its own comments.

The precise window: `[spl_fseg]` is published by stage 2 at `boot/boot2.asm:288`, so
the overlay is live from `kmain`'s **first instruction**; it is retired when `kmain`
sets `[spl_fseg]` to `COLD_SEG` and the memory is given back one instruction later
by `mem_unblob_x`.

An `.ovl` byte is a footprint byte **returned**, where a `.text` or `.cold` byte is
one **kept**. Moving a boot-only body there is still the only class of change in
this tree that gives a `KERN_BUDGET` rung back.

---

## 1. The blob today — measured, not remembered

`kernel/kernel.asm` sets the constants and asserts the two bounds at its own foot:

```
BOOT2_SECS  equ 9             ; SPEC.md 2.5.3 split the overlay; 15.3.8.5.1 and
OVL_AT      equ 2624          ; 2.9.13 then took the blob to NINE sectors
BOOT2_PAD   equ BOOT2_SECS * 512                       =  4,608

%if BOOT2_SIZE > OVL_AT            -> "the loader has outgrown its share"
%if OVL_AT + OVL_SIZE > BOOT2_PAD  -> "the boot overlay does not fit"
```

**THIS SECTION READ `BOOT2_SECS 19` / `OVL_AT 2560` FOR A LONG TIME AND BOTH
WERE WRONG.** §2.5.3 split the overlay by deadline and sent eleven sectors of
it back into the kernel's own read, §15.3.8.5.1's splash size pass took the
loader's half to one `OVL_AT`, and §2.9.13 packed the kernel — so what this
paragraph called load-bearing, *"nineteen sectors, and at 17 it does not fit
at all"*, describes a blob that has not existed for a long time. Re-measure
before quoting; that is what the paragraph below the table has always said and
it is the only part of the old text that survived.

Measured on this tree, `nasm -DKERNSIZE` reading `kernel.asm`'s own `ks:` line
— **and it is per-build now**, §2.5.3.2 having made the `.ovl`/`.ovlw` split a
build choice on `kern_small`:

```
blob      BOOT2_SECS 9 sectors = 4,608 bytes
  .boot2  2,250                              of OVL_AT 2,624   ->   374 free (both)
  .ovl    1,511  kern_big                    of 1,984          ->   473 free
  .ovl    1,333  kern_small                  of 1,984          ->   651 free
                                             TOTAL BLOB SLACK      847 / 1,025 bytes
  ...BOOTMARK=1:                                                   847 / 953
  ...BOOTMARK=1 MOUDIAG=1 together (not a requirement, §5):         841 / 922
```

**`kern_big` is what binds the blob, and keeping it that way is a rule.**
`kern_small`'s `.ovl` grew 910 bytes when §2.5.3.2 moved the mouse and adapter
probes into it, and it was left with more room than `kern_big` has on purpose:
the day the small build has less, a body added to `.ovl` breaks one kernel and
not the other, and the cheap answer — move an `.ovlw` body across — stops
being available in the direction it is needed.

> **THE RULE IS BROKEN, ON PURPOSE, SINCE SPEC.md 2.5.3.3.** `kmain`'s boot
> half is a blob body (`kmain_o`) on both kernels now, and it cost kern_small's
> blob more than kern_big's: on that tree `.ovl` is 1,942 of 1,984 on
> kern_small (**42 free**) against 1,832 on kern_big (152 free), so
> **kern_small binds the blob**. The owner took the trade for 334 / 248
> resident bytes. The way back the paragraph above worries about is still
> there, smaller: an `OVBCALL` body can go to kern_small's `.ovlw`, which has
> 34 bytes of its 1,536-byte region left. The figures in the table above are
> the older tree's; re-measure.

**Those bytes are ONE POOL.** `OVL_AT` is a byte offset with no alignment
requirement — the only constraints are the two `%if`s above — and moving it costs
nothing at all: `kernel.asm` says so in the file's own words, *"the blob is
BOOT2_SECS sectors either way, so no image byte, no RAM and no extra int 13h
changes — only the split."* Quote the single figure, and re-derive it after any
change to either side rather than trusting a number in prose.

**Nine sectors is LOAD-BEARING** — a change that grows `.boot2` or `.ovl` by
more than the figure above is a `BOOT2_SECS` conversation (§4), not a build
fix, and `BOOT2_SECS` is in `MIN_RAM_KB`'s guard 5 (SPEC.md 2.5.1.1) as well
as in every boot's read. `SPLSTARS=1` is the worked example of running out:
the twinkle and the kernel decompressor are 2,748 bytes of a 2,624-byte
`.boot2`, so that knob requires `NOKZIP=1` (SPEC.md 15.3.8.5.2).

**On `kern_small` there is a second, cheaper pool beside it**: `.ovlw` has 138
bytes before it rounds up a sector, and a body moved the other way (from
`.ovl` back to `.ovlw`) costs the blob nothing. The two headrooms are ONE
number — move a byte across and one grows as the other shrinks — and on that
build it currently stands at **789 bytes**, split 651 blob / 138 window. That
invariance is why the split point is a judgement about which side is more
likely to grow rather than an optimisation.

`.boot2`'s share is not freely tradable *down* either: its fifth sector is
SPEC.md §15.3.4's row composer, which ships, so `OVL_AT` cannot go to 2,048.

**One constant moves WITH `BOOT2_SECS` and is easy to miss**: `KSIG_OFF` (the
Makefile and `boot/boot2.asm`, one number typed twice — 8,704 today). It is a
*memory* offset and SPEC.md §18.93.1's canary is a question about a *file sector*,
so growing the blob slides the probe further into the file — out of the band where
it lands in a transfer run's second half, and into the half that loads correctly on
exactly the machine the canary exists to catch. `tests/unit/t_canary.py` is the
fast-tier row that refuses the build otherwise, and it is what found this.

---

## 2. What is left — three rows, and all three fit

Body sizes re-measured on this tree from a `[map all]` assembly (address to the
next top-level label in the same section). The Δ columns are the pass's own
whole-kernel measurements; **the bodies are unchanged since**, and none of the
three has a caller or callee that has moved into `.ovl` in the meantime, so
nothing has made them cheaper or dearer.

| # | body | now | bytes | Δ`.text` | Δ`.cold` | Δ`.ovl` | resident returned | blocked by |
|---:|---|---|---:|---:|---:|---:|---:|---|
| 10+22 | **`vid_detect` + `vid_init`** | `.text` | 84 | **−68** | 0 | **+88** | **68** | the ROUTE, not the bytes — §2.1 |
| ~~21~~ | ~~`evq_init`~~ | — | — | — | — | — | **22, banked** | **SETTLED: deleted, not moved — §2.2** |
| 20 | `mod_init_x` | `.cold` | 24 | +6 | −16 | +28 | 10 | nothing; it is just a bad trade — §2.3 |
| **Σ** | | | **130** | **−76** | **−16** | **+138** | **92** | |

"Resident returned" is `−(Δ.text + Δ.cold)` — the bytes that stop existing on the
machine after `spl_finish`, and the column to rank by for footprint. `Δ.text` on
its own is the **segment** column (guard 2, `KERN_CODE_MAX`), which a `.text` →
`.ovl` move buys and a `.cold` → `.ovl` move sells. §3 is which of the two to rank
by today.

### 2.1 `vid_detect` + `vid_init` — 84 bytes, refused on the ROUTE

**What it is.** `viddet.inc:313` and `:942`, 67 and 17 bytes. Which adapter this
machine has, and the one call that sets it up. `vid_detect` has two callers:
`vid_init` (from `kmain`) and `spw_vid_detect`, the 4-byte `.text` far shim the
**splash** calls.

**Evidence that the bodies are boot-only.** `spl_isr` is the *pre-`kmain`*
`int 08h` handler, replaced by `sched_init`, so everything reached only from it is
boot-only too. A reachability walk that treats the splash timer ISR as a runtime
root — the safe default — marks the whole splash chain live and loses this pair;
that is how the first sweep here missed it.

**Measurement.** Alone, `vid_detect`: `.text` −57, `.ovl` +67. As a PAIR: `.text`
−68, `.ovl` +88 — better than the two taken separately, because `vid_init →
vid_detect` becomes a near call inside `.ovl`. Both gate OK.

**The price, and it is not bytes.** `vid_detect` in `.ovl` *is* aboard before stage
1 jumps. **The path to it is not.** `spw_vid_detect` lives in `.text` at 0x0C84 —
sector 6, inside the `SPL_RESIDENT` = 9 sectors stage 2 waits for — and is called on
the splash's FIRST TICK. Any route from `.text` into `.ovl` goes through `spl_gate`,
which is at `.text` 0xCA18: **sector 101 of 103**. The call would enter memory the
floppy has not delivered: no fault, no message, whatever the machine left there.

Nothing would have caught it. SPEC.md §15 said the residency assertion "is on
`spw_resident_end`", and that label was **referenced by nothing in the tree** — the
guard had been lost. It is a `%if` at the foot of `kernel.asm` again, and these two
rows are refused by a measurement rather than by an argument. **68 resident bytes
is not worth a boot that dies on one machine class and not on the emulator.**

**What would flip it.** An `.ovl`/`.boot2` consolidation. The two are already ONE
address space (§6.1), so `.boot2` could near-call `vid_detect` directly — no
`spl_gate`, no `.text` anywhere on the path, and the residency question does not
arise because the whole blob is aboard before stage 1 jumps. That change is worth
more than these 68 bytes on its own, and it makes them free.

### 2.2 `evq_init` — SETTLED, and the answer was better than the row

**What it was.** `events.inc`. Seeded the event ring. One caller, `kmain`.

**The row as filed** proposed moving it to `.ovl` for 14 resident bytes and was
blocked on a host instrument: `tests/evqfull.py` poked a **near**
`call evq_init` into `snd_xlat` inside `KERNEL_SEG` and executed it, and in
`.ovl` `sym["evq_init"]` is an `OVL_AT`-relative offset in the blob's segment,
so that near call would have landed somewhere arbitrary and run it. The row's
own verdict was that 14 resident bytes for a test edit is a bad trade.

**Kernel size pass 3 deleted the routine instead, and banked the whole 22** (17
of body plus its 3-byte call, plus the 2 the byte-sized indices then took out
of `evq_pending`'s neighbours). The premise the `.ovl` move never needed is the
one that settles it: the three indices are `.bss` and **`.bss` arrives zeroed**
— measured on the built binary, the `.text`→`.cold` padding run is 0 non-zero
bytes — and nothing can push before the point in `kmain` the call sat at.
`evqfull.py`'s `reset()` is now three host **byte** writes with the guest
paused, which is simpler than the poke-and-run and atomic from the guest's
side, which the poke was not. See SPEC.md §10.

**The general lesson for the rest of this file**: an `.ovl` row asks "where can
this body live"; it is worth asking first whether the body needs to exist.

### 2.3 `mod_init_x` — 24 bytes, the worst ratio in the file

**What it is.** `mod.inc`, `.cold`, one caller in `kmain`.

**The price.** `.text` **+6** and 28 blob bytes to return **10**. A `.cold` row
sells 6 bytes of the 64KB segment every time (§2.4), and two `.cold` far shims are
needed besides.

**What would flip it.** Nothing plausible. It is recorded so that the next sweep
does not spend an hour rediscovering a 10-byte row; it was dropped once already,
when the blob became scarcer than the segment.

### 2.4 The `.cold` arithmetic, stated once

It is not the same trade as a `.text` row, and row 20 is the only one left that
takes it:

* A `.cold` body is called `call COLD_SEG:X` (5 bytes). In `.ovl` that becomes
  `OVLGATE X` (3 bytes) plus one `SPLSTUB` (8): **`.text` +6, every time.**
* `.cold` and `.ovl` have the **same calling discipline** — CS of their own,
  `DS = KERNEL_SEG`, far calls out through `cw_` shims — so nothing inside the body
  changes. That is what makes these rows mechanically cheap.
* A near call to a `.cold` body that is **staying** becomes far (+2 at the site) and
  wants a 4-byte `retf` shim in `.cold`.

So a `.cold` row **sells 6 bytes of the 64KB segment to buy back its whole body of
RAM.** Which way that reads depends on which guard is tighter when the question is
asked, which is why the table keeps the two columns apart.

---

## 3. What fits today — all of it, and that is the finding

The whole of §2 is **138 blob bytes** against **420 free on the tightest knob arm**
and 583 on the shipped build. Nothing left in this file needs a sector, an
`int 13h`, or one byte of any image. **If you came here looking for a sector, you
do not need one** — and two of the three rows are refused for reasons that are not
about bytes at all, so a sector would not help if you did. (This section used to be
a menu of subsets that fitted in 194 bytes. The blob is no longer the binding
constraint on anything in this file.)

Which column to rank by, when a NEW row appears:

* **Footprint (`KERN_BUDGET`, guard 1)** — rank by *resident returned*.
  `KERN_SIZE` is 109,056 of 129,536, so **20,480 spare, forty steps**. Not tight.
* **The 64KB segment (guard 2, `KERN_CODE_MAX`)** — rank by `Δ.text` alone.
  `.text`+`.bss` is 55,886 of 65,536, so **9,650 left**. **This is the guard that
  BINDS**, not because it is the smaller number — it is not — but because it
  **cannot be raised at all**, where `KERN_BUDGET` is derived from
  `KERN_RESIDENT_KB` and moving it is a rule change. (An earlier revision of this
  section called the segment *"the looser of the two guards"* at 7,055 bytes and
  ranked rows on that. Slackness is not the test; §7.2 carries the correction
  with the arithmetic.) While it stays this slack, a `.cold` row's standing
  `.text` +6 (§2.4) is still not much of an argument against one.
* **The blob** — 583 bytes, §1. Only §4 changes that, and 20 and 21 are free.
  **Kernel size pass 3 moved this too**: `.boot2` fell 2,439 → 2,250 and
  `BOOT2_SECS_STARS` was retired, so both arms are one blob length at one
  `OVL_AT` — re-derive from `t_blobruns` rather than reusing 583.

Quote `tools/kernsize.py`'s SUM and its ACCRUED line for both guards, never its
step count (CLAUDE.md, "Design for BYTES, never for rungs").

---

## 4. The growth table — what a sector costs and what it admits

`tests/unit/t_blobruns.py --sectors N` prices this host-side in 0.1 s, per geometry,
against the images already in `build/`. Re-run on this tree:

| `BOOT2_SECS` | blob | 360KB | 720KB | 1.44MB | free over today's 9,145 |
|---:|---:|---:|---:|---:|---:|
| 17 | 8,704 | 3 | 3 | 2 | **does not fit (−441)** |
| 18 | 9,216 | 3 | 3 | 2 | 71 |
| **19 (SHIPPED)** | **9,728** | **3** | **3** | **2** | **583** |
| 20 (`SPLSTARS`) | 10,240 | 3 | 3 | 2 | 1,095 |
| 21 | 10,752 | 3 | 3 | 2 | 1,607 |
| 22 | 11,264 | 3 | 3 | **3** | 2,119 |
| 23 | 11,776 | 3 | **4** | 3 | 2,631 |

(The call columns are the tool's output; `t_blobruns.py` FAILS at 22 and 23 rather
than reporting them, which is the ratchet doing its job. "free" is
`N x 512 − .boot2 2,457 − .ovl 6,688`, `OVL_AT` set wherever the split needs to be —
the two sides are one pool, §1.)

**Read the call columns and not the sector numbers.** A run is bounded by the TRACK
and `KERNEL.SYS` starts wherever each BPB puts the data area, so the boundary is in
a different place on each geometry. One `int 13h` is 1–2 revolutions **whatever it
moves** — 199 ms for one sector and 384 ms for a nine-sector track on the field 5150
(PERFORMANCE.md Part 2, Sets 14/22). A sector *inside* an existing run is ~24 ms.

So the ladder has four prices and everything in between is free:

| step | what it costs | what it admits |
|---|---|---|
| 13 → 14 | one extra `int 13h`, **720KB only** | — historical; the blob is past it |
| 14 → 15 | nothing | — |
| **15 → 16..21** | one extra `int 13h`, **360KB only** (~400 ms on the field XT) | **already bought.** 19 is where the blob sits; **20 and 21 are FREE from here** |
| 21 → 22 | a third `int 13h` on **1.44MB** — the release geometry, and the one most tests boot | 2,119 |
| 22 → 23 | a **fourth** on 720KB | 2,631 |

> **The load-bearing line: sectors 20 and 21 cost nothing that has not already been
> paid.** 1,024 more bytes are available for the price of ~24 ms of pre-splash
> in-run reads and no extra call on any geometry. **The next claim after that hits a
> price nobody has approved** — 22 buys 1.44MB its third call. That is the
> conversation to have before spending sector 22, not after.

**What the call table hides, and it is not small.** The in-run sectors land on
**every** geometry, including the one that pays no call: 13 → 19 was six more
sectors inside an existing run on 1.44MB, ~144 ms at 24 ms each, with nothing in the
call column to show for it. All of it is pre-splash — stage 1 reads the blob before
the first splash pixel — so it is time on a blank screen rather than a slower-looking
boot, and `docs/plans/completed/BOOT-PERF-PLAN.md`'s phase tables want re-taking because of it.

`tests/unit/t_blobruns.py`'s ratchet is **per geometry**: 3 on 360KB, 3 on 720KB,
**2 on 1.44MB**, each with its reason beside it. One number for all three was the
shape of the original mistake.

---

## 5. What each knob needs at nineteen sectors

A knob is bound by physics, never by a documented limit, and *"all knobs together
fit"* is not required. `SPLSTARS=1` was the model in the tree: `BOOT2_SECS_STARS`
sat beside the shipped value and the Makefile's `sed` was deliberately anchored to
find only the shipped one.

> **RETIRED — SPEC.md §15.3.8.5.1.** `SPLSTARS=1` fits the shipped blob now:
> the splash's own size pass took its `.boot2` from 2,768 to 2,568 and the blob
> to 3,989 of 4,096, so `BOOT2_SECS_STARS`, the second `OVL_AT` and the second
> `sed` are all deleted, and `OVL_AT` is 2,624 for every build. **The table
> below and the paragraph under it are the record of the mechanism, not the
> tree.** What retiring it bought is §18.93.1's canary: `KSIG_OFF` had to be
> legal for both blob lengths, and that intersection is four sectors at the top
> of `.text`.

Measured on this tree:

| build | `.ovl` | `.boot2` | its `OVL_AT` | blob free | fits 19? |
|---|---:|---:|---:|---:|---|
| shipped (kern_big) | 6,688 | 2,457 | 2,560 | 583 | yes |
| `KERN_SMALL=1` | 5,959 | 2,457 | 2,560 | 1,312 | yes |
| `BOOTPROF=1` | 6,688 | 2,457 | 2,560 | 583 | yes |
| `BAND=1` | 6,688 | 2,457 | 2,560 | 583 | yes |
| `MOUDIAG=1` | 6,754 | 2,463 | 2,560 | 511 | yes |
| **`BOOTMARK=1`** | **6,851** | 2,457 | 2,560 | **420** | yes — **the tightest arm** |
| `BOOTMARK=1 MOUDIAG=1` | 6,917 | 2,463 | 2,560 | 348 | yes |
| `SPLSTARS=1` | 6,688 | **2,786** | **3,072** | 766 **at 20** | **only at 20** |

**`SPLSTARS` is the only knob that still needs a `BOOT2_SECS` of its own**
(`BOOT2_SECS_STARS equ 20`): its `.boot2` is 226 bytes over the shipped split and it
is over *wherever* `OVL_AT` falls. `tests/unit/t_buildmatrix.py` is what watches all
of this, and it carries a `MOUDIAG` row because it had none, which is how a
short-jump break in a knob went unfound.

**Do not lower the shipped `OVL_AT` to make `SPLSTARS` fit 19.** It would work and
costs the shipped side nothing, but it is a shipped constant re-tuned for a knob's
overflow. The knob has a sector.

---

## 6. Standing caveats — read before adding a row

### 6.1 `.ovl` is a different address space, and the gate enforces it
A near call from `.text` into `.ovl` (or back) is a displacement computed between
two address spaces. `tools/os88ovlchk.py` refuses it, and that gate stays. Inbound
is an `OVLGATE`/`SPLSTUB` pair; outbound is `call KERNEL_SEG:cw_X` /
`call COLD_SEG:cwc_X` to a 4-byte `retf` shim.

`.boot2` and `.ovl` are the exception that is *not* an exception in practice: they
are already **one** address space (`.boot2 start=0 vstart=0`, `.ovl start=OVL_AT
vstart=OVL_AT`, one segment), so a near call between them is already correct today
and the gate's refusal is safe over-strictness rather than a correctness
requirement. **Do not weaken it for bytes** — it was measured at 34 bytes on the
mouse cluster and refused there. Do the consolidation properly (§2.1) or pay the
gate.

### 6.2 CS may never be stored from `.ovl`
`os88ovlchk.py` exempts `.ovl` from the `.cold` CS check **by design**, because the
overlay's data rides with it and `[cs:si]`, `push cs` and `cs lodsw` are correct
idioms there. **Storing CS into memory never is** — it always means "the kernel's
segment", and in `.ovl` it is the blob's. That is now its own gate check.

The generalisation worth writing down: **an `.ovl` body may take the address of a
`.text` label freely** — `.text` has `vstart=0` and the consumer supplies
`KERNEL_SEG`, so `mov ax, sch_idle_body` in `.ovl` is correct — **but an address of
an `.ovl` label stored anywhere that outlives the blob is a pointer into freed
heap.**

### 6.3 `MARK` is not textually a call
`MARK n` expands to `call mark_here` inside the macro body, so a `call` scan is
blind to it in either destination. `BOOTMARK=1` is the tightest arm in §4 for
exactly this reason — audit a candidate for `MARK`/`BPMARK` by hand.

### 6.4 A string op in `.ovl` may not take a `cs:` source
The 8086 **loses the segment prefix when a string instruction is restarted after an
interrupt**. An `.ovl` body that addresses kernel data through `DS`/`ES` is correct
as written, `DS = KERNEL_SEG` there exactly as in `.text`; but if an `.ovl` body
ever grows data of its own, a block move out of it must load a segment register and
never wear a `cs:` prefix.

### 6.5 A pre-existing `section` directive inside a body's span
`dsk_bootltr` and `mod_init_x` each have a `section` line between their label and
the next one. A bracket placed at the "next top-level label" therefore swallows it
and silently re-sections whatever follows — in the harness for this file it moved a
12-byte `.text` table into `.cold` and made a clean measurement read `−6` instead of
`+6`. Bracket to the **first section directive** inside the span, not to the next
label, and check the map afterwards.

The related rule that always applies: a NASM local `.foo` belongs to the last
non-local label, so a `section` line inserted mid-body re-parents nothing but a
*moved* body re-parents everything. Insert brackets **in place** so that no body
changes file position.

### 6.6 `.ovl` fails silently, and that is the standing risk
A future maintainer who adds a runtime path into a landed body gets a **silent
no-op**, not a crash: `spl_gate` tests `[spl_fseg]` and returns. That is a safe
failure and an invisible one. `tests/ovlrefs.txt` is what names, per symbol, what
may never gain a runtime caller.

### 6.7 THERE IS NO DEAD-ROUTINE GATE IN THIS TREE

Nothing in `tests/` can tell you that a whole routine has stopped being reached.
Say it here because this file's entire method is "find the bodies nobody needs
at run time", and a reader reasonably assumes the suite already does half of it.

* **`tests/unit/t_swallow.py`** looks for a statement that ended up inside a
  block comment. It is a C-comment scanner; it has nothing to say about
  assembly reachability.
* **`tests/unit/t_asmrules.py`** catches code after an *unconditional jump* —
  that is, an unreachable **tail inside** a routine. A whole `ret`-terminated
  body with a label nobody names is perfectly well-formed to it.
* **`tools/os88ovlchk.py`** walks sections and calls; it is asking whether a
  call crosses an address space, not whether anything makes the call.
* **`tools/stkbalance.py`** enumerates entries and *deliberately* walks a
  global that nothing calls — that is what "an entry may be entered at
  depth 0" means. An orphan is a thing it measures, not a thing it reports.

So the only instrument is a grep, and a grep is only as good as the directory
list it was given: kernel size pass 2 found two dead bodies by hand, and found
a "module-private" claim made after grepping four directories and not `tests/`.
**If you delete a body, say in the commit which directories you searched.**

---

## 7. Priced and not taken — do not re-derive these

Three classes now: bodies that look boot-only and are not (§7.1–§7.5); changes
that are correct, were BUILT, and cost more than they save (§7.6); and — since
§7.9 — a row that is **priced, sound and simply not done yet**.

**Read the row before assuming which kind it is.** Everything from §7.1 to
§7.8 is a REFUSAL and the arithmetic is there to stop you spending an
afternoon rediscovering it. §7.9 is the opposite: it is a deferral, the case
for it stands, and whoever picks it up starts from *yes, probably*. A register
that files both under one word is a register that loses the difference, which
is the whole value of writing either down.

### 7.1 Reached after the blob is retired

`kmain` gives the memory back at `mem_unblob_x`. Three `.cold` bodies are called at
or after that point and can never be in it:

| body | bytes | killing edge |
|---|---:|---|
| `mem_unblob_x` | 14 | **it is the routine that releases the blob** |
| `mem_floor_ax` | 14 | called from `mem_init_x` (before) **and `mem_unblob_x`** (after) |
| `drv_notice_x` | 23 | after `spl_finish` — "and only NOW say what did not load" |

### 7.2 Reached from `ui_task`, an ISR, or a published slot

| body | bytes | killing edge |
|---|---:|---|
| `osapi_sys_snapshot` | 258 | `osapi_table` cell — a **published slot**, called about once a second by the Task Manager |
| `menu_kbnav` | 177 | `kbm_move` — an arrow key, for the session |
| `ui_tm_open` + `ui_note` | 143 + 57 | `ui_cmd ← ui_dispatch ← ui_task`, and `cw_ui_note` besides |
| `vid_disp_init` | 135 | `kmain` **and** `vid_disp_relayout` — Control Panel → Display, SPEC.md §39.19.1. The largest lookalike in the tree |
| `mou_hotplug` | 131 | `ui_task` **every pass** — the worked example, and the edge that nearly disqualified the whole mouse cluster |
| `mouse_unhook` | 116 | `sched_unhook` ← Chip → Restart. Runs at **reboot**, when the blob is long gone |
| `vid_apply`, `vid_setmode` | — | `vid_switch`, `fsx_enter` — a runtime mode change |
| `mou_pall`, `mou_pout`, `mou_newround`, `mou_lockon`, `mou_p2_off` and the four `mou_p2*` writers | 339 | `mou_hotplug` / `mouse_unhook` |
| `mou_claim` | — | `mou_isrs`, the ISR vector table |

The `mou_*` cluster is the half of SPEC.md §9.4.7 that did **not** move: `mouse_init`
is in `.ovl` and these are still `.text`. `.cold` is available to them and would buy
**segment** rather than **footprint** — 339 bytes off `KERN_CODE_MAX` and nothing off
`KERN_BUDGET`.

#### The condition this row carried is now MET, and the answer is still no

It used to read *"worth having when the segment is the tighter guard; it has
7,055 bytes today, so it is not."* **The segment is the tighter guard now** — it
is the one that cannot be raised at all, `KERN_BUDGET` having forty steps — so
that sentence, read literally, says take it. It should not be taken, and it is
the **condition** that was wrong rather than the verdict. Priced on the tree
kernel size pass 3 closed (`08a8743`):

| | |
|---|---:|
| segment (`KERN_CODE_MAX`) relieved | **−339** |
| `.cold` grows | **+339** |
| `.cold` rung headroom there (accrued 308/512) | **204** |
| so `.cold` crosses one 512-byte rung | **footprint +512** |
| segment headroom already spare | **9,650** |

**339 bytes of segment headroom bought for 512 bytes of every machine's RAM**, on
a machine `kern_big` must fully reside in at 128KB, with 9,650 segment bytes
already free. It is understated besides: the cluster's callers (`mou_hotplug`,
`mouse_unhook`) are `.text`, so every call becomes **far**, +2 a site against the
339.

**The corrected trigger.** "The segment is tighter than the footprint" is a
comparison between two quantities measured in different money and it should never
have been the test. The test is:

> Take this when the segment is tight enough that **512 bytes of RAM is worth 339
> bytes of window** — that is, when something that has to land cannot fit
> `KERN_CODE_MAX` without it. Read `.cold`'s **ACCRUED** line at the same moment:
> the +512 is only certain while that rung has under 339 bytes left, and if it
> has more, the move is genuinely 339 bytes of window for nothing — which is a
> different row and a much better one.

Re-derive both figures from `kernsize` when the question is asked; do not reuse
the ones above.

### 7.3 Cannot move by construction

| body | why |
|---|---|
| `kmain` | a hub of ~35 near calls plus its `.text`-only gate sites. In `.ovl` every gate reverts to the 20-byte inline `SPLCALL` and every call goes far. **Deeply negative**, re-confirmed twice |
| `spl_gate` (13 bytes) and its `splg_` thunks (8 each) | `.text`-only *by construction* (SPEC.md §2.9.5.2): they exist **because** a near call out of another address space is refused |
| the `spw_*` / `cw_*` / `ovw_*` / `dkf_*` far shims | 4 bytes each; a shim in `.ovl` is a shim that cannot be reached from `.text` |
| `dsk_fdd_probe`, `clk_init`, `cpu_detect`, `xm_sniff`, `snd_init`, `desk_init`, `drv_snd_sniff`, `mouse_init` and the rest of the 6,688 | **already there** |

### 7.4 Two candidates that are corrections, not rows

`ui_tm_open`/`ui_note` and `menu_kbnav` appear in the original planning documents as
boot-only candidates. **They are not** (§7.2). Recorded here because the correction
is the useful half.

### 7.5 `.text` → `.cold` is not on this menu
It is a real move and it is not a *byte* saving: a `.cold` byte is resident for the
life of the machine exactly as a `.text` byte is. It buys the 64KB segment
(`KERN_CODE_MAX`) and nothing else. Rank it against guard 2 when guard 2 is tight,
and do not confuse it with a row above.

### 7.6 The grey fill and the pattern fill are ONE function — 139 bytes, and they stay two

**Status: BUILT, MEASURED, REVERTED.** The only row here that was in the shipped
kernel for a cycle.

**What it is.** `gfx_fill_gray_raw`'s VGA body and `gfx_fill_pat_raw`'s are the same
masked-edge, `rep stosb`-interior fill. The only difference is where the row byte
comes from: grey toggles `not bh` between 0AAh and 55h keyed on `[vga_y1] & 1`,
pattern indexes `vga_patbuf[y & 7]`. With the eight bytes `AA 55 AA 55 AA 55 AA 55`
those are the same function, so grey can point `[gfx_pat]` at a static table and jump
into the pattern body. **139 bytes of `.text`**, and it is real duplication rather
than a trick.

**Evidence it is safe.** `[gfx_pat]` is spendable: `osapi_gfx_fill_pat` writes it on
every API call and `files.inc` writes it immediately before its own `gfx_fill_pat`;
nothing carries it across another primitive. And the 1bpp arm is untouched —
`jne sw_fill_gray` stays where it is, so both mono adapters take byte for byte the
path they already took.

**Measured.** `gfxbench` on MartyPC, cycle-accurate 4.77 MHz 8088, `GFX_FILL_GRAY
64x64`, one run per adapter:

| adapter | standalone body | merged into `gfx_fill_pat` | delta |
|---|---:|---:|---|
| **VGA** | **4,266.31 µs** | **5,685.53** | **+1,419.22, +33.27%** |
| CGA | 8,081.55 | 8,081.55 | ±0.00% — the 1bpp arm |
| HERC | 7,797.61 | 7,771.56 | −0.33% — the 1bpp arm |

The commit that took it predicted **+17%** and said this row was what would settle
it. It settled it at **twice that**: `vga_pat_stage` is a call per fill and the row
byte is a table index per row, and on a 64×64 rect neither is lost in the interior's
`rep stosb` the way the estimate assumed.

**The price, and why 33% is refused here when it would be accepted elsewhere.**
`UI_GRAY` in `apps/os88ui.inc` is `os88ui_sbar` and `os88ui_sbmove` — **the shared
scrollbar trough** — plus `kernel/fprog.inc`'s progress widget, and `word` ×6,
`texpad` ×3, `sheet` ×2, `taskmgr`, `tracker`, `artful` and `apps/cc/os88thunk.asm`,
so every C package as well. A scrollbar is on nearly every window in the system and
redraws on every scroll. The owner's ruling, in his words: *"If I was more sure it
was only scrollbars I would keep it — scrollbars are nowhere near the capacity of
any machine, and the user can't scroll and drag a window at the same time. But I'm
leery of some game wanting to use the API and finding it unperformant."* This is a
**published `OSAPI_*` slot**, and the cost lands on code nobody in this tree has
written yet.

**What would flip it.**

* **The grey fill stops being on a shared-library path.** If `UI_GRAY` were the
  scrollbar's alone, and the scrollbar's cost were bounded by the widget rather than
  by whoever calls the slot, 33% of 4.3 ms on a redraw nobody waits for is cheap.
  What makes it dear is that `OSAPI_GFX_FILL_GRAY` is public.
* **139 bytes mattering more than 33% on scrollbars.** They did not here: taking the
  merge out cost 130 bytes (Fvga12-05's `mov ax,[vga_m8l]` word form stays at the
  restored body's two edge columns) and crossed one image rung, `KERN_SIZE` 113,152 →
  113,664. If the kernel were up against `KERN_BUDGET` rather than 15,872 bytes clear
  of it, that is a different sentence.
* **A cheaper merge.** The +33% is `vga_pat_stage` plus a per-row table index. A
  merged body that took the row byte from a register the caller pre-loaded — grey
  passing `not`, pattern passing a pointer — would keep most of the 139 bytes without
  the staging call. Nobody has costed one, and it is the only version of this idea
  worth building.

The refusal is recorded **in the source as well**, above `gfx_fill_gray` in
`kernel/vga12.inc`, because the two bodies really are the same function and the next
size sweep will find them again.

### 7.7 Kernel size pass 3's refusals, in this file's remit

`docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md` is the pass's record; these are the rows that
belong here, because each is a move or a merge that **looks available and is
not**. Four are still OPEN and are the owner's to take.

#### 7.7.1 Two byte-identical routines that may not be merged — the canonical shape

`wm_lk` (`wm.inc`) and `fpg_arm` (`fprog.inc`) really are **seven byte-identical
instructions** around `gfx_lock_flag`/`gfx_lock_own`. A helper merging them was
proposed and **refused independently by two reviewers**, because **the two jump
targets ARE the semantics** and they partition three lock states with different
groupings: `fpg_arm` asks `{free,mine}` vs `{other}`, `wm_lk` asks `{free,other}`
vs `{mine}`, and **no single flag out of one helper can express both**. What
would have shipped is a **recursive `gfx_lock`** in the common case, on a routine
whose banner says it is not re-entrant, and `wm_show_b` running its `gfx_save`
with **no lock at all** — `docs/FIELD-NOTES.md` 34.1 reintroduced. The correct
three-state helper was then priced at **−2** and refused too.

**Take the two dead `push ax`/`pop ax` pairs instead (−4), which pass 3 did.**
Recorded here because this is exactly the row a duplicate scanner will file
again, and it looks like free bytes from the listing.

#### 7.7.2 `drv_load_row` ≡ `mod_need` — BUILT, MEASURED, refused on its increment

`driver.inc`'s and `mod.inc`'s claim-and-read ladders share most of a body. The
merge was **built** with the real class block and failure ladders so the `jcc`
distances are honest, and measured at **−53 `.cold` / −4 `.bss`** (an earlier
entry in the same record has the same build at 306 → 257 = −49; *the two are not
reconciled — re-measure*). It was refused because it **subsumes smaller rows
already worth −24/−4 and collapses an −8 row**, so its **increment is ~−21
`.cold`** — a currency that does not bind — for the highest-risk change in that
pass.

Two hazards, neither named by either finder, and both to carry forward:

* **`mov cl, 10` destroys CL before the `MEM_K_*` tag is needed**, silently
  rewriting the heap owner tag **0xFF03 → 0xFF0A**, and it **assembles cleanly**.
  Carry the size in **BP** instead — `push bx / mov bx, cx` above it.
* The shared-`ld_fsz` premise the merge needs was **refused by its own author**
  (*"a proof of mutual exclusion I could not construct"*). That premise is now
  **moot** — both sides carry the size in BP and neither `.bss` word exists any
  more — so whoever takes it starts better than the position it was refused at.

#### 7.7.3 OPEN — `clk_ns_stamp` out of `.text` (−65 segment, +60 `.ovlw`, +60 `.modc`)

The strongest currency argument in that pass: 65 bytes of the binding guard for
120 bytes of two transient images, one of which is forfeit at the first mount.
**Its mitigation does not assemble.** The filed form is "one `%include`d source
fragment assembled twice"; both expansions define `clk_ns_stamp:` and NASM
answers `label inconsistently redefined`, rc=1 — proved with nasm. A `%macro`
taking the entry name works, and then: `os88ovlchk.py`'s `EXTRA` map takes one
file → **one** section, so a fragment included into `.ovlw` *and* `.modc` is not
representable there at all; and **both emitted labels become invisible to
ovlchk's label map**, so neither copy is covered by the near-call check or the
return-kind check. SPEC.md §37.93 refused the same trade at 24 bytes. The −65 is
gross: `F-clock3-15` supersedes into it and has already landed, so the
incremental gain is **−60**.

#### 7.7.4 OPEN — `toast_say` `.text` → `.cold` (−35 segment, +27 `.cold`)

§7.5 says `.text` → `.cold` is not on this menu, and this is the row to weigh
against guard 2 anyway, because it is clean: **no `.text` caller and no indirect
one** — no `dw toast_*` table entry, no `OSAPI_*` slot. It is also the rare case
where `os88ovlchk`'s return-kind rule is **not** blind (the body keeps its own
`ret`), so any surviving far call fires *"far-called, ends in a NEAR ret"*. Name
the thunk **`toastf_say`**. Stack: the `.cold` path gains +2 and the `.modc` path
+6, both on `STK0_TOP`. At `08a8743` the `.cold` rung has 204 bytes left, so +27
crosses nothing.

#### 7.7.5 `fprog`'s far-return row — refused by a gate its own note did not know

`F-fprog3-18` turns `os88ovlchk` **red** (*near-called, ends in `RETF`*). The
finding's own refusal note did not know that, which is worth recording: the gate
was the cheapest check available and nobody ran it before writing the
justification.

#### 7.7.7 OPEN — the SIXTEEN refusal cells `kern_small` carries for features it does not have (128 bytes of table, plus their bodies)

*Since kernel size pass 4 three of the sixteen (`gfx_line`, `gfx_lstep`,
`gfx_lstepv`) are DELETED and the table has two cell sizes (SPEC.md 20.3), so
the 8-byte arithmetic below is the old table's: the thirteen left cost 6 bytes
each if rare and 7 or 8 if hot.*

**SPEC.md §20.8 rule 4 says a slot's cell exists in BOTH kernels and the small
one refuses**, so that a package built against `kern_big`'s SDK gets a refusal
rather than a wrong routine. That rule has a standing price nobody had
counted, and on the kernel with **four bytes left in its image rung** it is
the largest single figure in this file:

| | |
|---|---|
| cells whose `kern_small` body is a bare refusal | **16** |
| the cells alone | **16 × 8 = 128 bytes of `.text`** |
| their refusal bodies | 2–3 bytes each, several already sharing one `stc`/`retf` |

`gfx_line`, `gfx_lstep`, `gfx_lstepv`, `gfx_spans`, `gfx_blitp`, `wm_band`,
`xm_alloc`, `xm_free`, `osapi_snd_fm_x`, `osapi_drv_cfg_x`, `osapi_drv_dlg_x`,
`osapi_desk_svc_x`, `osapi_pkg_rehome_x`, `osapi_vol_stat`, `drv_pkg_call_x`
and `osapi_mouse_feed`.

**Two constraints make it hard, and the second is the one that is not obvious.**

1. **Only a TAIL cell can be retired without holing the table.** *(True of
   the uniform 8-byte table this was written against. Kernel size pass 4
   renumbered the whole table when it went to two cell sizes, and a withdrawn
   cell is now deleted wherever it sits — SPEC.md §20.3.1; the renumber this
   item priced has been paid.)* Retiring one
   in the middle leaves a hole that SPEC.md §20.3.1's free list has to carry;
   retiring the last one SHRINKS the table and the free list stays empty. This
   tree has shrunk the tail three times — `OSAPI_MEM_COMPACT_WAKE` became
   `0x0590`'s `MEMC_POST` verb, the DOS handoff became `OSAPI_DRV_SUSPEND`'s
   `AL = 2`, and `0x0598` was freed and immediately re-spent on
   `OSAPI_MOUSE_FEED`. So the set cannot be deleted; it can only be retired
   one tail cell at a time, and bringing the other fifteen TO the tail is a
   renumber — mechanical in-tree, since `apps/os88api.inc` is the one source
   of every offset and `tests/unit/t_api_abi.py` decodes the table out of
   `kernel.bin` to check it, but it invalidates every `.o88` already written
   to a floppy, and this project ships images.
2. **A retired cell needs a DOOR, and every door costs 6–9 bytes to open** —
   SPEC.md §9.12.5.3 is that arithmetic done in full for one slot. A door
   needs a selector test it does not already make, plus a register shuffle,
   because a slot's arguments collide with whatever registers the door already
   uses. On the kernel that HAS the feature the eight bytes come straight back
   out of the door; `kern_small` keeps all eight only because there is nothing
   there to open a door for. **So the whole of this row's value is on
   `kern_small`, which is where it is worth most anyway.**

Re-derive it with a listing walk rather than by reading source: assemble
`kernel.asm` with `-DKERN_SMALL -l`, take every `OSAPI_*CELL`/`*SLOT` target
out of the table, and keep the ones whose body is `stc`/`ret`, `stc`/`retf` or
`xor ax, ax`/`stc`/`ret`. Measured on `5ca2de18`.

#### 7.7.8 OPEN — the `drv_cls_svc_x` CF gate: a CHECK, worth more than the 38 bytes that motivated it

Not a saving — a **gate**, and it is filed here because it is what stands
between this file and 38 bytes of `.bss` that are dead by construction.

`drv_cls_svc_x` publishes *"`CF = 1` and `DI = 0` if the class is out of
range"*, and **`DI = 0` is `drv_svc + 0`, which is the SOUND driver's published
service table.** A caller that misses the `CF` test therefore does not crash —
it writes one class's services over another's, which is exactly the silent
cross-class disconnection SPEC.md §51.2.1 exists to prevent, arriving through
the routine that implements it. There are **ten callers**. `drv_cls_fp_x`
publishes the identical refusal and has the same hazard.

**The gate:** a source walk in `tools/os88ovlchk.py`'s shape — find every
`call drv_cls_svc_x`, `call drv_cls_fp_x` and their `COLD_SEG:drvf_*` far
forms, and fail the build unless a `jc`/`jnc` appears within the next few
instructions. About forty lines of host Python and a `fast`-tier row. NASM
cannot do this itself (it has no control flow) and neither can a `.bss` canary
(the bad write lands at the *start* of the table, not past its end).

**What it unlocks, once it is green:** `DRVC_POINT` (SPEC.md §9.12) needs no
`drv_svc` slot at all. `USBMOUSE.DRV` publishes only `DSV_NAME`, and
`DSV_NAME` is read **nowhere** in `kernel/` — three matches, all of them
comments — so the class's 36-byte slot plus its `drv_owner` word is dead the
day it is allocated. Size `drv_svc` at `DSV_SIZE * (DRVC_MAX - 2)` and have
`drv_cls_svc_x` refuse the class the way it already refuses class 3.
**Gate first, size change second**: the 38 bytes are what pays for writing the
gate, not the reason to skip it.

#### 7.7.6 `.lowbss` is not on this menu at all, and it is now PROVED so

`dskwin.inc`'s 3,328, `viddet.inc`'s 696-byte row table and `events.inc`'s
128-byte ring were all examined and all three are **floors**, with the arithmetic
in `docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md` §3.1. The one worth knowing here: taking
bytes out of the mount window **spends `.ovlw` headroom one for one** — they are
the same bytes seen from either end — and past a point it fires
the `.ovlw` guard beside `SKB_DSK` in `kernel.asm` at assembly time.

---

## 8. Method, and how to re-derive any of it

### 8.1 Sizes — whole-kernel re-assembly, never fragment arithmetic
A `[map all …]` line at the top of a **copy** of `kernel.asm` makes NASM emit every
symbol with its section and address; a body's size is its address to the next
**top-level** label **in the same section** (NASM locals appear as `parent.child`
and are excluded, and sections overlap in address space, so mixing them gives
nonsense). The probe is proved non-perturbing: the `ks:` line is byte-identical with
and without it.

Every row was then **built** — the body bracketed into `.ovl` in place, the call
site converted, the shims added — and measured with
`nasm -f bin -w+error -w-error=user -DKERNSIZE`, reading `kernel.asm`'s own `ks:`
line, which is `tools/kernsize.py`'s exact procedure.

### 8.2 Boot-only — closure, not inspection
The call graph carries six edge kinds, because a plain `call` scan misses four:

* **CALL** — `call`/`jmp`/`jcc`/`loop`, near and far
* **CELL** — `OSAPI_SLOT`/`JSLOT`/`NSTUB`/`XSTUB` (the macro body near-calls its argument)
* **MAC** — `%macro` bodies mapped onto every expansion site (`MARK` → `mark_here`)
* **GATE/STUB** — `SPLGATE`/`OVLGATE`/`SPLCALL`/`OVLCALL`/`OVLCALLC`/`SPLSTUB`, whose
  target is pasted into a generated label and is invisible to every regex above
* **DATA** — `dw`/`dd <label>`, a stored proc address
* **ADDR** — *any other* immediate mention of a known code label (`mov reg, sym`,
  `push sym`, `mov word [x], sym`). Deliberately over-broad: an IVT install is this
  and nothing else, and a candidate must survive it.

A body is boot-only iff **every** direct caller is boot-only, computed as a fixpoint
from `{cold_entry, kmain, the stage-2 splash chain, everything already in .ovl}`,
with three classes blocked from joining: `ui_task`, every ISR, and every body whose
inbound edges are *only* ADDR/DATA — a stored pointer entered later.

Closure over the whole kernel returned **59 boot-only bodies and no more**. Every
routine in `wm.inc`, `menu.inc`, `ui.inc`, `files.inc`, `fdlg.inc`, `icons.inc`,
`clip.inc`, `blank.inc`, `toast.inc`, `fprog.inc`, `assoc.inc`, `filecp.inc`,
`clone.inc`, `diskw.inc`, `fsx.inc`, `snd.inc`, `band.inc`, `font.inc`, `vga12.inc`
and `softgfx.inc` is either session-lifetime or already `.cold`. **There is no
fourth row waiting to be found in this tree** — a new one has to come from new code.

### 8.3 Reproducing

```sh
nasm -f bin -w+error -w-error=user -DKERNSIZE \
     -I <kerneldir>/ -I apps/ -I build/ -o /dev/null <kerneldir>/kernel.asm   # read ks:
python3 tests/unit/t_blobruns.py --sectors 19
python3 tools/os88ovlchk.py                    # from a tree root; 11 checks
```

### 7.8 The Dock's routing is at its floor at 112 bytes — three cheaper schemes, all refused

SPEC.md 30.5's fourteen Dock operations cost `kern_big` **112 resident bytes**:
fourteen four-byte `jmp far [dkv + 4i]` entry points and a fourteen-slot far
pointer table. Twenty-eight of those bytes are a repeated `KERNEL_SEG`, which
looks like the obvious thing to take, and it is not takeable. Priced on
`6bfcffb7`, where the scheme it replaced was **155 bytes** and **227 guest
cycles** an operation:

| scheme | resident bytes | basic-path cycles | why not |
|---|---:|---:|---|
| **built** — `jmp far [dkv+4i]` + a 14-slot far table | **112** | **42** (measured) | — |
| near vector + one shared far dispatcher | 56 + 28 + 84 + 6 = **174** | ~120 | a NEAR vector cannot name the module's segment, so the mounted arm needs a six-byte trampoline per operation to carry an index. The trampolines are what the far table buys its way out of |
| `call near [dkv+2i]` at the CALL SITE, no entry points | 33 + 28 + 84 + 6 = **151** | ~30 | same trampolines, plus the basic bodies would need a second `retf` entry (they are near-called from inside `dock.inc` too, and are `kern_small`'s whole Dock), plus `hiber.inc` takes `dock_force`'s address and `cw_mem_disp` near-calls it |
| **`jmp far KERNEL_SEG:<body>` PATCHED in place at mount** | 14 × 5 = **70** | ~22 | **the only scheme that beats the built one**, on both axes. It is not refused on arithmetic and it has its own row: **§7.8.1** |

#### 7.8.1 The patched far jump — a row to say yes or no to, not a refusal to re-derive

**Not built, deliberately.** It wins on both axes and the whole of the
question is whether this project wants self-modifying `.text`.

**The shape.** Each of the fourteen entry points becomes a five-byte
`jmp far KERNEL_SEG:<basic body>` — an `EA` whose offset and segment are
IMMEDIATES. `DOCK.DRV`'s `dkx_hook` rewrites those four bytes to its own
`<modseg>:<landing pad>` and `dkx_unhook` writes the kernel's back, which is
exactly what both already do to `dkv` today — the module carries both tables
either way, so **the module side does not change at all**. `dkv` disappears.

| | built (`jmp far [dkv+4i]`) | patched (`jmp far imm`) | delta |
|---|---:|---:|---:|
| entry points | 14 × 4 = 56 | 14 × 5 = **70** | +14 |
| the table | 14 × 4 = **56** | none | **−56** |
| **resident** | **112** | **70** | **−42** |
| basic-path cost | **42 cycles**, MEASURED | ~22 cycles, PREDICTED | ~−20 |
| the whole Dock feature | 486 | **444** | −42 |

The 42 is measured — `dock_paint` → `db_paint` under MartyPC at 4.77 MHz, six
identical samples. The 22 is `jmp far imm`'s 15 clocks against the 8088's
`max(clocks, 4.34 × 5 bytes)` fetch floor and is **predicted, not measured**.

**What it costs, and it is not a safety argument.** The write happens inside
`dkx_hook`/`dkx_unhook`, in the module's own segment, under the graphics lock
SPEC.md 30.5 already requires for a settings change — so no operation can be
executing, and the 8088's four-byte prefetch queue is nowhere near the bytes
being written. The price is the one
`docs/plans/completed/SCHED-IDLE-PLAN.md` §8 already names for its own
self-modifying option: **`os88marty verify` has to be taught the patch
table**, or it reports fourteen five-byte runs differing from
`build/kernel.bin` on any machine with a non-default Dock setting. `verify` is
a REPORT and exits 0, so no tier goes red — which is the reason to teach it
rather than a reason to leave it.

**The knob shape, if it is taken.** SCHED-IDLE-PLAN §8's `NOSMC=1` — *"the
only one that costs literally zero when off"* — is the precedent and the right
spelling here: `%ifdef NOSMC` keeps `dkv` and the indirect entries, so the A/B
is one define, the un-patched arm stays assembling, and the decision is
reversible per build rather than per commit. That is this tree's standing
pattern for a change somebody may want to look at twice (`NOCURDISK=1`,
`NOMOUPRIV=1`, `NOSEAMCUT=1`).

**What is NOT claimed:** that it reaches 400. It does not — 444 is 44 short,
and `docs/reports/DOCK-RESIDENT-COST-2026-09-17.md` §6.4 says what would.

**Two operation counts were also tried and are not worth having.** Folding
`DKI_FORCE` away by letting the resident `db_force` read `[dock_la1]`/
`[dock_la2]` is −8 +1 = **−7** and is DEAD: those two words are now in the
module image (SPEC.md 30.5), so no resident body can read them. Moving the four
module-only operations onto `mod_fp`'s already-allocated spare slots — which
cost the kernel nothing, `MOD_NENT` being 7 and the Dock using 2 — is **+8**
rather than −16, because `mod_disarm` rests a slot on `mod_gone`'s `retf` and a
far JUMP to it would pop a near frame as CS:IP, so each would have to buy back
the six- to eight-byte guard the table's own `dkb_ret`/`dkb_clc` slot replaced.
Only `gfx_hole_arm` qualifies (its call site tests `[gfx_hole]` already) and it
is **4 bytes** for a hole in an otherwise uniform table.

**And the rung it would take to uncross is not in this feature.** `kern_big`'s
image rung wants `.text + .bss` at 55,808 and the tree stands at 56,074; the
whole Dock placement feature's `.text + .bss` is 441. Uncrossing means taking
**266 of those 441**, which is 60% of a feature whose mechanism is already at
its floor and whose remaining bytes are one `%ifdef` at a time across nine
files.

---

## 9. Evidence owed by whoever takes a row

Nothing static substitutes for these, and this file does not claim otherwise.

1. **A boot.** Any row is a body that ran during boot and would then run from a
   different segment. One MartyPC boot to a desktop settles it.
2. **`vid_detect` + `vid_init` (§2.1)** — the adapter probe decides the mode, so
   both 1bpp adapters (`VIDEO=cga`, `VIDEO=herc`) and the `xt-multimon` two-card XT,
   `vid_cga_alias` running *only* when the mono card is primary. And, because the
   refusal is about the ROUTE rather than the body, a boot off a **floppy** and not
   only off an image the emulator has entirely in RAM: the failure this row is
   refused by is a sector that has not landed yet.
3. ~~**`evq_init` (§2.2)**~~ — landed as a deletion in kernel size pass 3;
   `tests/evqfull.py` and `tests/unit/t_wakedrain.py` were both edited with it
   and are the soak rows that cover it.
4. **`tools/os88ovlchk.py`, all 11 checks**, on the build and on every knob arm in
   §5. It has earned its place: it caught four `retf`/near-call mismatches and a
   dead shim during the pass, all of which assemble cleanly and are wrong.
5. **A 360KB boot** if any `int 13h` step is bought (§4). **MartyPC cannot host a
   720KB drive with the ROM sets in this tree**, which is precisely how
   SPEC.md §15.3.8.5's boundary was missed the first time. 86Box, or the field 5150.

### 7.9 `HDD.DRV` and `NET.DRV` carry `os88ui`'s glyph family and never call it — 309 bytes, DEFERRED FOR TIME

**This row is NOT a refusal.** It was priced on 2026-09-17 during the file
dialog's size pass, found sound, and left undone because the cycle ran out of
time. Nothing about it has been argued against; whoever picks it up is
starting from "yes, probably", not from "here is why not".

`apps/os88ui.inc`'s default block is `%ifndef OS88UI_NOBTN`, and that block is
**all-or-nothing**: it carries `os88ui_btn` and `os88ui_bhit` **and**
`os88ui_glyph` + `os88ui_gring` + `os88ui_gdot` + `os88ui_gdn` as one unit
(`os88ui.inc:329-848`). Five standalone drivers include the library and **none
of them defines any `OS88UI_*` region macro**, so all five take the whole
block. What each actually calls, counted over its own directory:

| driver | `os88ui_btn` | `os88ui_glyph` |
|---|---:|---:|
| `ether` | 5 | 2 |
| `saver` | 1 | 3 |
| `ramdisk` | 1 | 1 |
| **`hdd`** | **5** | **0** |
| **`net`** | **5** | **0** |

So `HDD.DRV` and `NET.DRV` each carry the glyph family for nothing. **309
bytes in the kernel's copy of those four routines** (`glyph` 144, `gring` 79,
`gdot` 66, `gdn` 20) and more in a package-arm copy, which is what a driver
image is. The change is a `%ifndef OS88UI_NOGLYPH` sub-gate inside the
`NOBTN` block, so a driver can decline the glyph without losing the button —
which `OS88UI_NOBTN` would take with it today, and is why neither driver can
opt out now.

**WHAT THE ROW IS WORTH DEPENDS ON A FACT NOBODY HAS MEASURED**, and it is the
first thing to settle rather than the last. The bytes are a driver IMAGE's,
not the kernel's — resident only while the driver is loaded — and **a driver
whose display elements are the point tends to be loaded only while the user is
configuring it**, which would make 309 bytes of a transient image nearly
worthless. But `hdd` and `net` are not obviously that kind of driver: `hdd` is
`DRVC_DISK` and `net` is `DRVC_FILE`, and a driver serving a mounted volume is
plausibly up for the whole session, in which case the 309 is resident in the
ordinary sense. **Measure the residency before spending the effort**;
`tools/heapmap.py` reads what is claimed on a running machine and
`[drv_wcnt]` is the kernel's own liveness word.

Two things already established, so they need not be re-derived:

- **Neither driver is missing the glyph by accident.** Both draw buttons and
  neither draws a checkbox, radio or disclosure mark; `os88ui_glyph` is the
  mark renderer (`docs/plans/completed/CTRL-GLYPH-PLAN.md`), and a driver with
  no mark to draw has no call to make.
- **The kernel is not affected either way.** `os88ui.inc` is `%include`d
  exactly once in the kernel (`kernel/fdlg.inc:3128`), every `os88ui_*` body
  lands in `.cold`, and **not one is in a `.mod*` section** — so no kernel
  module carries a second copy. This row is about the five standalone driver
  images and nothing else.

`tools/incsize.py` is the instrument for the per-driver figure and **will not
build a driver as-is**: it hard-codes `-I apps/`.

### 7.10 `rect_get`/`rect_put`'s other two sets — 150 bytes, HELD by the owner

Kernel size pass 4 (docs/plans/completed/HANDOFF-KERNEL-SIZE-P5.md) added
`rect_get`/`rect_put` to `wm.inc`: `call` + `dw rect` (5 bytes) for the
15-byte four-`mov` load or store of AX..DX from four adjacent words. The
helpers cost **+99 cycles a get and +173 a put**, measured on MartyPC's 5150
against the inline form, and every site of the shape was counted with
execution breakpoints over eleven scenarios on CGA, Hercules and VGA
(`boot`, a Disk window, a package open and close, a raise, a drag and drop
over one window and over a five-window stack, a menu, closing five windows).
The owner took the **S1 set only** — 19 sites that are cold or run once per
operation, never above 0.06% of any measured operation, 7 of them never
reached at all — for −142 bytes on kern_big. The other two are held here:

| set | sites | bytes | cost, measured |
|---|---|---:|---|
| **S2, the damage repaint** | `wm_dmg_bands` ×3, `wm_paint_dmg` ×3, `wm_dmg_gray` (the two that run) | ~80 | +1,088 cycles (0.23 ms) per damage repaint: **0.16% of a window close**, 0.02% of a drag drop |
| **S3, the save-under cache** | `wm_su_owed`, `wm_su_sub`, `wm_su_vset`, `wm_su_srect` (the hot one), `wm_su_flay`, `wm_su_try` ×2 | ~70 | ~0.37% of a close and ~0.2% of a raise; `wm_su_flay` alone is **0.13%** of a close (5 puts, 70-78 runs a session) |

S3 is the worst trade in the set — the save-under cache exists to make raise
and close cheap — and prefers GET sites if any are ever taken (a put is 1.75x
a get). **Never convert `gfx_clip_run`** (`vga12.inc`): the same shape, once
per clipped drawing RUN, up to 59 times in one operation. The `.cold` sites of
the shape (`ui_krect4`, `fmv_uadd`) cannot use the helpers at all — the `dw`
is read through DS, which is not CS there. The measurement's scripts and raw
counts are the pass's scratch findings (`rect-count.md`); re-derive rather
than quote if the window manager has moved.
