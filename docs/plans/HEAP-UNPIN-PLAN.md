# Nothing left unmovable — unpinning regions, driver images and overlays

**Status: BUILT, except §12's open questions.** The unpinning this document
plans has shipped — regions, driver images, overlays and `SOUND.DRV`'s ring all
move, and `tests/suite.py`'s `reg*`, `drvmove`, `sndmove` and `hdmove` rows are
its gates. The byte figures below are the ESTIMATES the plan was written
against, not what it cost; `docs/reports/` carries the measurement. SPEC.md 66.6 is the door this
document costs and SPEC.md 66.9 is the register it works through.

It is a *last resort* by construction: everything in it is reached only after
the plan the compactor already makes has been found not to help, which is where
SPEC.md 66's worker park already sits.

Read SPEC.md 66 first. This document assumes it.

---

## 1. The question

> As a last resort to claim memory, what would it take to be able to compact
> the top of the heap? Program regions, drivers, overlays. Unmount and remount
> for hardware that interrupts write into, or otherwise make it safe. This
> would mean nothing is left unmovable.

Six reasons pin a block (SPEC.md 66.9); three are answered elsewhere. What the
question is about is the other two: **a base that IS a CS** — every package
region, every driver image, every on-demand module — and **a bus master may be
looking at it**, everything carrying `MC_DMA`.

The finding is that those are not one problem with one answer. They are four
populations with four costs, three of them cheap, and all four turn on the same
question — *is any frame anywhere inside this image?* — which has an **exact**
answer in each of them, not the probabilistic one a stack scan gives (§4). One of
the three counters that answer it is already built and exact (`[drv_wcnt]`); one
is already designed and refused only for want of it
(docs/plans/completed/ONDEMAND-PLAN.md §7.1); the third is eleven bytes.

**"Nothing left unmovable" IS reachable — but not by kernel code alone.** Every
population here can be made to move: the sound ring by quiescing the chip it is
armed on (§3.2), a module by being dropped and re-read (§3.3), a driver image by
fixing 66 kernel words and its IVT vector (§3.4), and a region by proving no
frame is inside it (§4.2). The one population that resists — a package that owns
a worker, **20 of 32 shipped packages and 354,316 bytes of region against
232,460** (§4.6, recounted; an earlier figure of 191,350 against 135,930 came
off a table that left the six largest packages out) — is reached only by
**asking its owner**: a declaration that the worker's stack may be thrown away
and the worker re-entered (§4.7).

**But the question's own framing is sharper than the answer it wants.** Every one
of those claims is already at the top — a package region goes through
`mem_claim_hi_x` exactly as a driver image does (kernel/loader.inc:810) — and the
allocator already refills any ceiling hole big enough for the claim being made.
So unpinning the top buys a **merge** of free runs, not a reclaim (§2.1); the real
mid-arena barriers are three cheaper things nobody has done (§2.1.1); and none of
it is safe until the compactor can pack *upward* (§5).

So the honest headline is that the last of it is an **ABI question, not a kernel
one**. The kernel side stops at ~740–870 bytes; getting to *nothing* left
unmovable means every driver publishing two verbs and every package with a worker
declaring a restart point, and a package built before that day simply stays
pinned — which is the right default, and the one SPEC.md 66.2 already sets.

---

## 2. What is pinned today, and what it is worth

`mem_can_move` (kernel/memory.inc:1190, **80 bytes**) is the whole predicate, and
it applies **six** tests in order: `MC_RLOC` = 0; `MC_DMA` ≠ 0; `mem_in_xfer`;
the purgeable range on `MC_OWN`'s high byte; the `MEM_K_DRV`/`MEM_K_MOD` tags;
and then a **two-way split** — `mem_is_region` on the instance-slot arm,
`mem_busy_seg` on the segment arm. Everything below plugs in there, and the
detail that matters for costing is that split: **making a region movable means
changing two arms, not one**, because a package's own segment can appear as an
owner either way.

Everything else is already movable. The RAM disk's store declares `rd_reloc`
(drivers/ramdisk/rdstore.inc:112), Frotz's 508KB story `zf_reloc`, Tracker's
116KB module `trk_reloc`, and SPEC.md 66.9's reason 5 — the donated
per-partition listing, written up as *"structural, and the interesting one"* —
**has since been built**: the kernel's own holders are rows of `mem_rr_tab` (kernel/memory.inc) and are the
kernel's half of a donated claim's move and `mem_reloc_call` runs it before the
owner's proc on every move. **SPEC.md 66.9 reason 5 and docs/HEAP-CLAIMS.md's
row for it are stale and should be corrected whether or not anything here is
built.**

### 2.0 The scenario this is for, and why it is a last resort

The owner's, in his words, and it is the case the whole document should be read
against:

> Ethernet driver is mounted at boot. The user opens Sheet. The user opens Paint.
> The user opens Tracker. Oops, they realize they forgot to mount the sound
> driver. They go and mount it, and unmount Ethernet while they are there. After
> a while, they close all of the above apps. But the sound driver is now sitting
> almost 150k from the top — and that is fragmentation.

With this tree's own numbers: `ETHER.DRV` takes 18KB of image plus a 14KB socket
pool at the ceiling; SHEET's region is 48KB, Paint's 28KB, Tracker's 17KB, each
packing down beneath it. `SOUND.DRV` is then mounted into the highest base that
still fits — **~125KB below `[mem_top]`** — and its 8KB ring beneath that. Now
unmount Ethernet: 32KB frees at the very top. Now close all three programs: 93KB
frees between them and the sound driver.

**The machine is left with a 6KB driver image and an 8KB ring standing in the
middle of ~125KB of free space, and nothing that exists can ever move them.**
Every later claim must fit entirely above the wall or entirely below it. The
total free memory is fine; the largest run is half of it.

**Three properties make this the case that justifies the work**, and each is
worth stating separately because §2.1's arithmetic understates all of them:

1. **It does not heal.** `mem_claim_hi` will refill either side, but the wall
   never moves. The only events that clear it are unmounting the driver — which
   costs the user the feature they went to Settings for — or a reboot.
2. **It accumulates.** Every mount, unmount or launch performed at a *different*
   heap occupancy leaves another block at another depth. Over one session the
   top arena's fragmentation is monotone: it can only get worse.
3. **It is caused by ordinary, sensible use.** Nothing above is a stress test.
   Mounting a driver mid-session is what the Control Panel is *for*, and doing it
   while some programs happen to be open is the normal case, not the corner one.

That is why this is the *last* resort and not a general facility: it earns its
bytes exactly when a claim would otherwise be refused against a heap that has
the room but not in one piece.

**The same failure mode has already been reported from the field, one arena
along.** docs/FIELD-NOTES.md 2: on a 384KB machine, load `BEVERLY.MOD`, play,
close Tracker, open it again — *refused, with the Task Manager showing ~104KB of
heap free.* *"The total said there was room; the **largest run** said otherwise,
because two long-lived claims had been left in the middle of the heap."* Both
offenders were **data** claims; both were fixed, and SPEC.md 66's compactor now
moves that class. The note's own closing line is where this document begins:
*"a region's base is its CS and never moves."*

So the shape is not hypothetical. It has bitten, it was severe enough to close
two bugs, and the fix reached only the arena the compactor can enter. §2.0 is the
same thing in the arena it cannot.

**And it is `SOUND.DRV` in the scenario, which is the awkward one** — the only
driver in the tree that hooks an interrupt vector and the only one that spawns a
worker (§3.4). It is also 6KB, so its interrupts-off copy is ~17 ms; `[drv_wcnt]`
already answers for its worker; and `[sbl_str_act]` (§3.2) says whether the ring
is live. A machine that is not playing anything can move both.

### 2.1 The arena, and where "the top" actually is

**Every claim whose base is a CS is claimed top-down, regions included.** A
package region goes through `mem_claim_hi_x` — kernel/loader.inc:810, *"from the
TOP down, away from the data claims growing up from the bottom"* — and so do
driver images (kernel/driver.inc:2474), module images (kernel/mod.inc:422), the
Sound Blaster ring (drivers/sound/sb.inc:2484) and ETHER's socket pool
(drivers/ether/tcp.inc:784).

```
[mem_base]  data claims + purgeable caches, growing UP  ...gap...  rings, modules, drivers, REGIONS, packed DOWN  [mem_top]
```

So *"the top of the heap"* in the question is exactly the set of claims the
compactor treats as barriers, and the question's own list — program regions,
drivers, overlays — is precisely that set. That framing is right and an earlier
draft of this document had it wrong.

**What the allocator already does, and what it cannot do.** Two things handle
part of this already:

- `mem_claim_1`'s down arm walks from `[mem_top]` past every overlap
  (kernel/memory.inc:723), so it **already refills either side of a wall** with
  any claim that fits there;
- `mem_cp_plan`'s `.barrier` (:1596) and `.tail` (:1607) **already report every
  such hole**, so `mem_avail` is honest about them.

So for **one claim in isolation** the gain is only (sum of the free runs) −
(largest free run) — the ability to merge. That arithmetic is true and it is not
the case for the work: §2.0 is. What the allocator cannot do is remove a wall,
and a wall placed by an ordinary mount persists for the session and accumulates
with every later one.

The counter-argument is that nobody has yet measured a session in that state
**in this arena** — docs/FIELD-NOTES.md 2 measured it in the other one, where a
second Tracker load was refused against ~104KB free, and SPEC.md says so itself:
*"The measurement that was asked for first did arrive, from the field:
docs/FIELD-NOTES.md 2 is a refusal where the total and the largest run differed
by more than 100KB."* The closest ceiling-side figure is SPEC.md's own **FRAG 4K
out of a 544K span**, the phenomenon at a scale that costs nothing — and on the
128KB machine this whole memory effort exists for, `tests/small128.py:133`
asserts *no pinned claim stands on a bare desktop* and measures **0 bytes
pinned**, because kern_small loads no drivers at all (kernel/driver.inc:89) and
holds one or two regions. **So the yield on kern_small is structurally zero and the case is
entirely kern_big's.** Open question 1 is what would settle the size.

Cost side: the top-down stack is the largest single thing on the heap (~209KB at
a busy 640KB moment), and at 2.79 ms/KB a full pass over it is **~583 ms** —
against a park that already costs 220.


### 2.1.1 …and the real mid-arena barriers are somewhere else

The genuine barriers — pinned blocks sitting *inside* the bottom-up arena, which
is what SPEC.md 50.3 warns about — are not the things the question names. They
are three, and all three are cheaper to fix than anything in §3:

1. **A package's OVERLAY image is claimed BOTTOM-UP, its base IS a CS, and it is
   the single largest pinned block in the arena.** `CWORD.OVL` is **18,565
   bytes** (docs/C-TOOLCHAIN.md:531) — *bigger than every kernel module in the
   tree put together* — claimed with `OSAPI_MEM_CLAIM`, not `_HI`
   (apps/cc/crt0.asm:958), and pinned mid-arena for the program's whole life.
   `apps/word/word.asm:19843` does the same. That is **SPEC.md 50.3.2.1's exact
   defect one layer out from the two driver images that section fixed**, and it
   affects Word and **every C package** (CWORD, RUNCPM, C64, WEAVE).

   **The unconditional half of the fix is the slot number, not the
   declaration.** Claiming `_HI` moves the overlay out of the data arena whatever
   else is true. *Declaring* it movable is gated: an overlay claim is owned by
   the package's **segment**, so `mem_can_move` routes it to `mem_busy_seg` →
   `inst_seg_parked`, and a package with a live **unparked** worker keeps it
   pinned even when declared. Every C package hires a worker, so the declaration
   pays only while that worker is parked — which SPEC.md 66.5's machinery already
   delivers, but it is not the unconditional win the placement change is.

   **And the relocation proc it would need already exists.** `cc_ovbind`
   (apps/cc/crt0.asm:1084) re-stamps `[cc_ovseg]` into every far pointer in
   `cc_ovm_first..cc_ovm_end`; a proc that sets `[cc_ovseg]` and calls it is
   about ten bytes of `crt0.asm`. So this is **the best value in the entire
   document**: an 18KB mid-arena barrier removed for every C package, for **zero
   kernel bytes**, using code that is already written. It has to go in
   `crt0.asm` rather than in C, because `apps/cc/os88.h` has no
   `os88_mem_movable` — which is item 3.
2. **SHEET puts 99KB of undeclared claims into the arena at its entry proc** —
   six unconditional `OSAPI_MEM_CLAIM` calls (apps/sheet/sheet.asm:548-573), none
   declared movable. SPEC.md 66.5.10.2's closing *"the arena below the top now has
   no barrier in it at all — every claim there is movable or purgeable"* is true
   of the configuration it was measured on and false the moment SHEET opens.
3. **A C package cannot declare a claim movable at all**: `apps/cc/os88.h` has no
   `os88_mem_movable`, so every C package's claims are pinned by construction and
   no author can change it.

**None of those needs any of §3.** They are a slot number, a declaration and an
SDK function.


### 2.2 The regions, split by the thing that decides everything

Every package region in the tree, from the `image` field of its `.o88` header,
against whether the package hires a worker (`OSAPI_TASK_SPAWN`):

| bytes | package | worker? |
|---:|---|---|
| 48,452 | SHEET | **no** |
| 27,921 | PAINT | **no** |
| 24,279 | TEXPAD | **no** |
| 19,257 | ArtfulType | yes |
| 18,480 | Note Pad | yes |
| 18,473 | ModPlug | yes |
| 17,963 | Tank Attack | yes |
| 17,359 | Tracker | yes |
| 15,921 | ftpd | yes |
| 15,286 | Browser | yes |
| 13,378 | Cyclone | yes |
| 13,336 | Missile | yes |
| 12,107 | CHART | **no** |
| 9,216 | Audio | yes |
| 8,917 | Task Manager | yes |
| 7,025 | Arkanoid | yes |
| 6,508 | Calculator | **no** |
| 5,881 | Solitaire | **no** |
| 5,141 | Tamegram | yes |
| 4,719 | Telnet | yes |
| 4,171 | Piano | **no** |
| 4,129 | Fractal | yes |
| 3,843 | Recorder | **no** |
| 2,750 | WIREFRAME | yes |
| 2,021 | Mines | **no** |
| 747 | hello | **no** |

**worker-owning 191,350 bytes · worker-less 135,930 bytes — OVER THIS TABLE,
which is not the shipped set**: it leaves out Word, LOOM, WEAVE, CWORD, C64,
Frotz and RUNCPM, and counts WIREFRAME, which ships on no floppy. §4.6 carries
the count over every shipped package on this same basis — **354,316 against
232,460** — and it is the one to quote. The C packages are
**not** uniformly on the pinned side, as an earlier draft said: `apps/cc/crt0.asm`
offers the spawn but **C64 and LOOM do not take it** (apps/c64/c64.asm:44,
apps/loom/loom.asm:112), so their regions are reachable where CWORD's and
RUNCPM's are not.

The undeclared *data* claims those packages hold are larger than any region here
and are a separate problem (§2.1.1) — with the sizes stated carefully, because
two circulated figures are worst cases rather than holdings: **SHEET 99KB** in
six unconditional claims at its entry proc; **C64 64KB**; **LOOM 29KB**
long-lived (`LM_CLAIMKB`, apps/loom/lmproj.c:28 — its 50KB and 62KB claims are
*transient*, held only across a Pack); and **Browser's document claim capped at
63KB** (`BR_DOCMAX`, apps/browser/browser.asm:124), the "109KB" being the worst
case for a 32KB page rather than what it holds.

Why that split is the whole answer is §4.5. The short version: a worker's stack
carries its own package's segment from the moment it starts, so a package with a
worker can never be *proved* to have no frame — and the three biggest regions in
the tree have no worker.

The other populations: driver images 1–11KB packed (up to `DRV_MAX` = 5 loaded),
module images ~1–6KB, and the Sound Blaster ring at `SBL_WANT` = 8KB.

---

## 3. Four populations, four answers

### 3.1 `MC_DMA` is a placement constraint, and the compactor already owns the code to honour it

`MC_DMA` stores *the 64KB-page-safe head in paragraphs* (kernel/memory.inc:75) —
a statement about where a block may **land**, not that a chip is reading it.

**There are FOUR `MC_DMA` sites in the tree.** SPEC.md 66.9's reason 2 names two
(*"the Sound Blaster's double-buffer and the file manager's copy buffer"*) and
its reason 3 covers the third, the directory read-ahead, as a purgeable cache —
so the register is complete for three of four. **The one real gap is Word's
typeface cache** (apps/os88type.inc:603, 9KB, up to three per Word or CWORD
instance, session-lived), which appears in neither SPEC.md 66.9 nor
docs/HEAP-CLAIMS.md.

| claim | why it asked | bus master armed? |
|---|---|---|
| directory read-ahead | one `int 13h` fill in fewer calls | **no** — and its `MC_DMA` pin is a **no-op**: `mem_can_move` tests `MC_DMA` *before* the purgeable range test (kernel/memory.inc:1203 vs :1207), so deleting it still reaches `.pin` on the purgeable arm |
| file manager copy buffer | it is an `int 13h` target | **no** — see below |
| Word's typeface cache | a 512-byte aligned base for file reads | **never** |
| the Sound Blaster ring | the 8237 holds its page and offset | **yes**, and it is the only one |

**The page rule is not a correctness requirement for any disk buffer in this
kernel, and two independent passes over the tree agree on it.** `dsk_runcap` (kernel/disk.inc:1512) caps every kernel `int 13h` run at
the page and `hd_bios_run` does the same on the HDD driver, and a single
512-aligned sector cannot straddle a page at all. So `MC_DMA` on a disk buffer
buys **call count, not correctness** — and the tree says so itself:
`kernel/filecp.inc:597` falls back to a plain `mem_claim_x` **with no `MC_DMA` at
all** when no page-safe run exists, and *"still copies, exactly as it did
before"*. **A pin that protects correctness cannot have a fallback that drops
it.** SPEC.md 22.5.1 prices what it really buys: **84 `int 13h` calls against
93**.

**Nothing else on the machine is a bus master into host RAM**, and each was
checked rather than assumed: the NE2000's *"remote DMA"* is a PIO window at one
port with the CPU doing the moving (drivers/ether/ne2000.inc:174), the IDE rung is
`in ax,dx`/`stosw` (drivers/hdd/hdd.asm:1260), XMS is `int 15h AH=87h` or a plain
`rep movsw` under a kernel-raised `[sch_lock]` (kernel/xmem.inc:597), and VMMOUSE
says of itself *"No interrupt vector, no IRQ line, no DMA channel"*. And *"is
hardware writing here right now"* is **already answered exactly** for every
`int 13h` in the kernel by `[mem_pinseg]`/`mem_in_xfer` inside `dsk_xfer`'s
`[sch_lock]` hold — which `mem_can_move` already calls.

**And "unrelocatable in principle" is refuted by a shipped routine in the same
file.** `mem_regrow`'s path 3 (kernel/memory.inc:2082) **already relocates an
`MC_DMA` claim**: it stages `MC_DMA` into `[mem_dma]`, lets `mem_hifit` +
`mem_dmaok` pick a page-safe base, `mem_bcopy`s the block there and rewrites
`MC_SEG` — with no quiesce and, deliberately, no notification. **The machinery
for "re-place a page-constrained claim, page-safely" is built, shipped and
`.cold`.** What the compactor lacks is the call to it and the notify.

Cost to demote `MC_DMA` from a pin to a constraint: **~69 bytes** of `.cold`
(ESTIMATE). Note the direction of error if it is got wrong: a block landing
across a page is answered by the 8237 wrapping to the start of its page and
moving *the wrong memory, silently* (kernel/memory.inc:75). It must be gated by a
test that reads the resulting address.

**One free byte-saving found on the way, unrelated to any of this — BUILT.**
`apps/os88type.inc` claimed `TY_FACE_KB + 1` *"because the hand alignment below
gives back up to 496 bytes of it"*, then rounded with
`add dx,31 / and dx,0xFFE0`. Guard 6b (`kernel/kernel.asm`) already makes every
claim base `HEAP_SEG + n*MEM_PARA_KB` paragraphs and **asserts `MEM_PARA_KB` is
a multiple of 32** — so the round-up could never move the base and the extra KB
was never consumed. **1KB of heap per open face, up to 3 per Word or CWORD
instance, for nothing.**

The rounding is replaced by a `test dl, 0x1F / jnz` that **refuses the face**
rather than by nothing, because a package cannot see guard 6b and the rounding
was the one line that would have noticed a change to it — silently, by pushing
`TF_SEG` up to 496 bytes into a claim that no longer had 496 spare.
`tests/facescan.py` reads the claim's size and base out of `mem_tab` and
asserts both; putting the `+ 1` back reads `3000/9KB` and goes red.


### 3.2 The bus master that is real — and its unmount/remount already exists

The ring is 8KB (drivers/sound/sb.inc:2375), claimed top-down, and the 8237
holds its page and offset in the chip's registers. It is the one claim the
phrase "hardware that interrupts write into" describes.

**Both halves of the unmount/remount are already in the driver:**

- `sbl_halt` (drivers/sound/sb.inc:2549) — DSP `D0h`, or masking DMA channel 1
  at the 8237 on high-speed hardware, which is *one `OUT`, ISR-safe*.
- `sbl_go_on` (drivers/sound/sb.inc:2567) — `D4h`, or unmask.
- `sbl_dma_map` (drivers/sound/sb.inc:2468) already derives `[sbl_page]` and
  `[sbl_dmaoff]` from a base segment, and `sbl_arm_half`/`sbl_arm_rec` re-arm
  the 8237 from those two words.

**Most of the time no quiesce is needed at all, and the driver already keeps the
byte that says so.** `[sbl_str_act]` is 0 when no stream is open, and with no
stream open no DREQ can reach the 8237 — the ring is 8KB of dead weight and can
be moved like any other block. On a machine that is not playing anything (which
is most machines, most of the time) the whole of the rest of this section is
unnecessary. That one-byte test should be the first arm, and the quiesce below
only the second.

What is missing for the second arm is a **rendezvous**: today `mem_reloc_call`
tells a holder only *after* the bytes have moved. The ring needs both sides — halt, copy, re-derive
page and offset, re-arm, continue.

The cheapest correct shape is **not** a second handle in every claim record
(`MC_SIZE` 10 → 12 costs 64 bytes of `.lowbss` on kern_big for one customer). It
is a pair of **driver verbs**, `DRVV_QUIESCE(base, para)` and
`DRVV_REARM(base, newbase)`, dispatched through `drv_call` — the existing far
boundary for exactly this (SPEC.md 51.2). A driver that does not publish them
keeps every claim it owns pinned, which is the house default.

**What the user hears is worse than a click, and this is the correction that
matters.** An 8237 write to port 0x02 loads the **base and current registers
together**, so an auto-init channel reprogrammed at a new base restarts at the
ring start — while the DSP's own block counter does not. `[sbl_play]` and the
chip then go **permanently out of phase**: not one click, but corruption for the
rest of the stream. So the honest verb here is a stream **RESTART**, not a
resume, and the driver has to be told which it is doing.

**And there is a second sting that binds every top-down claim, not just this
one.** The ring is claimed top-down. The compactor has **no descending pass**
(§5), so making the ring movable *today* would pack it down into the data arena
— which is exactly what `mem_claim_hi` exists to prevent (SPEC.md 50.3's *"one
long-lived data claim landing mid-heap permanently splits the space a package can
be loaded into"*). **Piece A must not ship without piece E.**

### 3.3 Overlays — the answer is DROP, not MOVE, and the design is already written

A module image is **already disposable**: read from a file by `mod_need`
(kernel/mod.inc:373), and `mod_free_row`/`mod_disarm` (kernel/mod.inc:541)
already point every one of its `mod_fp[]` far pointers back at
`mod_gone:COLD_SEG` before freeing the claim. **The far-pointer half of dropping
a module is finished code today.**

`mod_drop`'s own banner names the one thing missing:

> nothing here can tell whether the module is on the stack … Dropping a module
> that is still executing frees the code that is running — which does not fault
> and is why this is not, and must not become, something the heap does on its
> own initiative. docs/plans/completed/ONDEMAND-PLAN.md 7.1/7.2 is the
> purgeable-claim design that would need a pin; it is deliberately not built.

ONDEMAND-PLAN §7.2 is the target — tag the image `MEM_PG_LOW`, *"losing it costs
a little I/O, or a visible pause"*, which is exactly a re-read. ONDEMAND-PLAN
§7.1 is its price: **a one-byte nesting count per module, incremented by the stub
before the far call**, because a module can re-enter itself through a callback.

**That price is affordable, and §4.3 costs it: ~104 bytes.** The count is
`resb MOD_MAX` in `.bss`, an increment in `mod_need` — which already holds the id
— and a small `mod_leave` that the **29 thunk sites** (over seven far-pointer bases in seven files — `CMZFP` is `CLFP+4`, the compressor riding in `CLONE.DRV`) call after their
`call far [XXFP + n*4]`. It has to reach the thunks that deliberately do *not*
call `mod_need` (SPEC.md 13.8.3's two edges, kernel/ctrl.inc:5820), which is
break 3 in §8.

**This is the best bytes-per-KB in the study and it should be built first — but
price the prize correctly.** Modules are dropped when their feature closes, so
they are not all resident at once: the realistic ceiling is **one module, 8KB on
kern_big** (`CTRL.DRV` is 7,397 unpacked, so an 8KB claim) and **~9KB on
kern_small**, where the Control Panel's 5KB and the file dialog's 4KB can be up
together. Not the ~25KB a sum over all four modules suggests. Against ~104–168
bytes that is still the best ratio in the document, and it is the *right* bytes:
a module image is in the same top-down arena as a package region
(kernel/loader.inc:810) and a driver image (kernel/driver.inc:2474), so it is
directly what a package launch wants, **with no compaction involved at all**.

The lifetimes are what make it worth having, because a module is not a transient.
`CTRL.DRV` is resident from Control Panel open to close (kernel/ctrl.inc:5881),
`FDLG.DRV` for as long as a file dialog is up (kernel/fdlg.inc:3148), and only
the formatter's is dropped at *"the one moment nothing is on the module's stack
and nothing will call it again"* (kernel/diskw.inc:4869). The whole resident
mechanism today is **477 bytes** on kern_big (`.cold` 309 + `.text` 56 + `.bss`
112), so the pin is a 22% addition to it.

**Measured, not estimated**: `CTRL.DRV` 7,397 bytes (an 8KB claim), `CLONE.DRV`
5,810 (6KB), `HIBER.DRV` 3,398 (4KB), `FORMAT.DRV` 1,129 (2KB) on kern_big — 20KB
if every one were resident, which the features being mutually exclusive means
never happens. kern_small adds `FILECP.DRV` 2,161 (3KB) and `FDLG.DRV` 3,243
(4KB).

**Three things sharpen the picture, and the first one refutes a claim an earlier
draft of this document made.**

**A stack scan does NOT replace ONDEMAND-PLAN §7.1's pin, and neither does a
per-call counter.** Both are sound about *frames* and blind to a **held span** —
a stretch where no code is executing in the module and the feature still needs
the image. There are three, and one of them is unrecoverable: `files.inc:5461`
loads the formatter or the cloner at the *"the system disk is in NOW"* prompt and
then holds it while the user takes the system disk **out of the drive** again. A
shed there cannot be undone. `filecp.inc`'s `FCPS_ASK` holds it across an
overwrite question, and the Control Panel holds it open-to-close. ONDEMAND-PLAN
§7.1 said this in as many words — *"for the Control Panel the pin is an
open-to-close span, not a per-call one"* — and it is the sentence to read twice.
**The pin has to be per-span, taken by the feature, not per-call taken by the
thunk.**

**And the rank is arguable in both directions.** ONDEMAND-PLAN §7.2 proposes
`MEM_PG_LOW`, *"a little I/O"*. A re-read goes through `drv_mounted` and, if the
user is standing elsewhere, `drv_vol_back` as well — at PERFORMANCE.md's 12
sectors / 4 calls, up to **~1.6 s on the 5150**, which is `MEM_PG_MED` by the
argument SPEC.md 18.8.4 used for `MEM_P_FATW`. But both are **quiet** mounts and
`dsk_here_ok` bounds them below by **zero**: an installed hard-disk machine
sitting at the boot volume root pays nothing at all, and neither does a floppy
whose BIOS motor-off countdown is still running. So the cost is 0 to ~1.6 s
depending on where the user is standing, and the rank is a judgement about which
end of that to design for rather than a settled number.

Third, **five of the six modules claim memory from inside their own image**
(kernel/ctrl.inc:5507, clone.inc:413, compress.inc:983, filecp.inc:593,
hiber.inc:1361), so each is pinned at exactly the moment a drop would be worth
most; and **shedding `FDLG.DRV` on kern_small breaks a live dialog**: its six thunks far-call **without** reloading (kernel/fdlg.inc:3108-3167)
and `mod_tab[MOD_FDLG].MODR_SEG` is read at kernel/ui.inc:546 and
kernel/kernel.asm:6511 as the *reap guard* (SPEC.md 38.0.1), so a shed leaves a
dead, undismissable dialog whose `fdlg_grab_x` swallows every press. That module
must be excluded, or those six thunks must gain a `mod_need`.

**A figure in the tree is wrong on the way past.** SPEC.md 38.0.1 calls the
stranded file-dialog image *"a 16KB claim, held for a dialog nobody could see, on
the machine with 128KB in it"*. 16KB is `MOD_MAX_KB` (kernel/mod.inc:169), the
**cap**; `drv_find` cuts the claim from the image size and `build/smallk/fdlg.drv`
is 3,243 unpacked, so the claim was **4KB**. The defect was real; the number
quoted for it is the ceiling.

C-package overlays (SPEC.md 73.14) and multiseg parts (SPEC.md 20.12) cost the
kernel nothing: apps/os88parts.inc:9 is explicit that *"the kernel learns three
things and nothing else … there is no .o88 v6, no kernel-parsed block and no API
slot"*. A part's segment lives in the **package's** own data, so a part claim is
declarable movable today with a package-side proc, and the only frames that can
be inside an `OP_SEG` part are that package's own.

### 3.4 Driver images — unhook, move, rehook

The kernel's copies of a driver's segment are few and enumerable:

| word | where | count |
|---|---|---|
| `DRVR_SEG` in `drv_tab` | kernel/driver.inc:160; `drv_tab` is 80 bytes = 5 rows × 16 | 5 |
| `drv_fseg` … `drv_fseg5`, the published per-class fast path | kernel/driver.inc:842–851, deliberately contiguous | 6 |
| `drv_blkfp`, `drv_dlg_seg` | kernel/driver.inc:1591, 4285 | 2 |
| `MC_OWN` where the owner IS the driver's segment | kernel/memory.inc:73 | scan `mem_tab` |
| `W_SEG`, because **a driver can own a window** | ethcfg.inc:166, hdd/tool.inc:148, hdd/inst.inc:181, saver/svcfg.inc:126 | 4 creators |
| `mod_fp[]` for modules | kernel/mod.inc:647 | scan |

**Three of those rows are not in `drv_tab` and a walk of it would miss them**:
`ss_row` (kernel/blank.inc:205, SAVER.DRV), `xm_row` (kernel/xmem.inc:114,
XMEM.DRV) and `vmm_row` (kernel/driver.inc:987, VMMOUSE.DRV) are each *"sixteen
bytes shaped like a `drv_tab` row and not in it"* (kernel/blank.inc:184). And two
drivers hold a **second image**: `[rd_pfar+2]` (drivers/ramdisk/rdpage.inc:136)
and `[hd_tfar+2]`/`[hd_tseg]` (drivers/hdd/hdtool.inc:163) — `HDDTOOL.DRV` is
12,567 bytes unpacked and sits at the ceiling for the whole of a Format or an
Install, which makes it one of the better prizes in the document and one only its
owner can fix.

There are **five** class pairs, `drv_fptr`/`drv_fseg` through
`drv_fptr5`/`drv_fseg5` (kernel/driver.inc:841), and two dispatch sites beyond
the obvious: `call far [es:drv_fptr5]` (:1721) and a generic `call far [es:si]`
(:1818) that walks the same contiguous block.

The service table a driver returns at attach is **copied once and holds no
segment at all** — every `DSV_*` cell is a *near* offset (drivers/os88drv.inc:167)
and the segment lives once per class in `drv_fseg`. Counted end to end, a driver
image is named by **66 kernel words across 9 tables and singletons**, and that is
the whole of what a move has to fix on the kernel side.

**Half the driver-side predicate is already built, and it is exact.**
`[drv_wcnt]` (kernel/driver.inc:4277) counts live driver worker tasks — *"+1
inside the spawn's IF=0 window, -1 inside `task_exit`'s"* — and **`drv_unload`
already waits for it to reach 0 before a byte of the image is freed**
(kernel/driver.inc:2961, SPEC.md 51.7). So a driver needs no stack scan for its
own workers: it has a counter that cannot be wrong. What the scan is still needed
for is the *other* direction — a UI-task chain currently inside the image through
`drv_call`.

Two things make a driver harder than a module:

1. **The IVT — and it is more than one vector.** The sound driver installs
   `sbl_isr` on its own IRQ (drivers/sound/sb.inc:3089's `sbl_hooked`,
   `sbl_oldvec`) and unhooks at detach. **But it also writes its CS into a set of
   CANDIDATE vectors during IRQ detection** (drivers/sound/sb.inc:2986, saving
   each into `sbl_dsc_oldv` and installing `sbl_dsc_stub`), so a move that masks
   *the* IRQ and fixes *the* IVT word would leave up to four stale far pointers
   into freed memory. Any `DRVV_RELOC` for this driver must walk both sets. Rewriting it is that same
   `sbl_unhook`/`sbl_hook` pair with a new segment — the `DRVV_QUIESCE`/
   `DRVV_REARM` verbs §3.2 already wants. No other shipped driver hooks a
   vector; vmmouse says so explicitly (drivers/vmmouse/vmmouse.asm:34: *"No
   interrupt vector, no IRQ line, no DMA channel, no port it keeps"*).
2. **A proc the kernel far-calls from inside an ISR.** `DSV_TICK` is *"called
   from `snd_tick` — INSIDE IRQ0, at IF=0"* (drivers/os88drv.inc:174), and
   `mem_compact` raises `[sch_lock]` but leaves interrupts **on** by design (the
   `dsk_xfer` bargain, SPEC.md 18.9). So the image may not be copied while IRQ0
   could dispatch into it. `DRVV_QUIESCE` is where the driver takes its own
   publication down; the kernel's part is to bank and clear `drv_fseg` for the
   class across the copy.

**What is actually at stake here is bigger than it looks: ~69KB.** `drv_memk`'s
own constants (kernel/driver.inc:1057) give the images as SOUND 6KB, HDD 8KB,
**ETHER 18KB**, RAMDISK 9KB, NET 6KB, VMMOUSE 1KB — 48KB — and on top of that sit
the 8KB Sound Blaster ring and **ETHER's 14KB ring pool**, both claimed top-down
and both `MC_RLOC` = 0. On a fully-loaded kern_big machine that is the driver
share of "the top of the heap", and it is three times the furniture this document
first estimated.

**The IF=0 problem belongs to exactly one driver.** `SOUND.DRV` is the only
driver in the tree that hooks an interrupt vector (drivers/sound/sb.inc:3056) and
the only one that spawns a worker task — kernel/sched.inc:141 says so in as many
words: *"one driver calls `OSAPI_DRV_TASK` in the whole tree"*. Every other
shipped driver states in its own header that it hooks nothing. So the
interrupts-off window is 6KB ≈ **16.7 ms**, not the 50.2 ms an 18KB `ETHER.DRV`
would have cost, and moving the other four images is **pointer fix-ups only**,
under `[sch_lock]` with interrupts on like everything else the compactor does.

**A comment in the stack asserts the opposite of §3.1 and should be corrected.**
`drivers/ether/tcp.inc:786` justifies pinning the socket pool with *"the card's
own descriptors point into these rings and it DMAs into them"*. It does not: the
8390's *"remote DMA"* is the port-0x10 data window (drivers/ether/ne2000.inc:86),
not a bus-master path into host RAM. Harmless today because the pool is claimed
`_HI` anyway — but it is exactly the reasoning somebody copies.

**And two drivers hold pointers the kernel cannot reach at all.** `HDDTOOL.DRV`
and `RAMPAGE.DRV` each keep two words naming their parent driver's segment
*inside a heap claim* — SPEC.md 66.5.10.1's *"a donated claim has holders the
callback cannot reach"*, one level worse, because here the holder is a second
image rather than a kernel table. Only a `DRVV_RELOC` verb reaches them.

**The user's "unload and remount" is not hypothetical. This tree already does it,
twice, and it is 91 bytes.** `hbm_detach`/`hbm_reload` (kernel/hiber.inc:1321,
called at :1202 and :1258 — 61 and 30 bytes) unload every driver whose state is
*hardware* before a hibernate image is written and reload them from a bitmask
afterwards, deliberately keeping `DRVC_DISK` and `DRVC_FILE` loaded *"because
their state is memory"* (SPEC.md 87.4 step 2). And `ss_reap_x`
(kernel/blank.inc:699) calls `drv_release` on `SAVER.DRV`'s image **every time
the screen saver finishes**.

On two axes reload beats moving outright. It reaches **strictly more memory** — it
frees the 8KB ring and the 14KB ETHER pool, which no relocation gives back at all
— and **it breaks no interface**, because a package addresses a driver by CLASS
(`net_cls`, apps/os88sock.inc:39) and never by segment.

What it costs is disk time and state, and the state is the real bill: a reload is
~8–13 `int 13h` calls (two quiet remounts plus the image), plus `ETHER`'s
`DHCP_WAIT` of 110 ticks — **6.04 s** (drivers/ether/ether.asm:48), and that is
a **hard block on `ui_task`**, not merely six seconds of I/O: there is not one
`OSAPI_TASK_YIELD` anywhere in `drivers/ether/`, and `eth_dhcp_wait`'s own header
says *"this is the one thing in this driver that blocks"*. The desktop stops.
`rd_unmount`
*"IT DISCARDS"*: a RAM disk loses its **contents**. Every driver-backed volume
unmounts with the Disk windows standing on it, and every TCP connection goes.

**What it cannot be is a compaction primitive**, and that is structural rather
than a judgement. The blocker is **`[mem_cp_busy]`**, not the lock:
`mem_claim_x.go`'s first act is `cmp byte [mem_cp_busy], 0 / jne .busy`
(kernel/memory.inc:570), the byte is set *before* `[sch_lock]` is raised and
stays set across the park window, so **every** claim is refused for the whole
compaction — and `drv_load`'s own `dskw_read_x` claims. It belongs where hibernate already puts it: a decision
taken on the UI task, outside the compactor, from a known-safe context — and, on
this evidence, offered to the **user** on the Control Panel's Drivers page rather
than taken by the heap on its own initiative.


### 3.5 Package regions — what the fix list actually is

SPEC.md 66.6 lists the holders. Every one is a **scan-by-value over a small
kernel table**, which is a shape this tree has a worked example of at 36 bytes
(`dsk_dseg_reloc` over `dsk_vtab` plus one live word), and a hook that already
runs on every move: *"`mem_reloc_call` calls it for EVERY move, before the
owner's own proc, because the kernel is a holder of claims it does not own"*
(kernel/disk.inc:4425).

| holder | table | size |
|---|---|---|
| `I_SPTR` | the instance table, 12 records | written in exactly **two** places (kernel/loader.inc:995, kernel/instance.inc:1494) and read in 24 |
| `W_SEG` | `wm_wins` | 408 bytes |
| `MB_SEG` | `menu_bar` | 84 bytes |
| `MC_OWN` | `mem_tab` | 320 bytes (`MEM_MAX` 32; 20 on kern_small) |
| `[menu_seg]` the bar owner, `[menu_dseg]` the DROPPED menu | kernel/menu.inc:2687, :2690 | 2 words |
| `[fdlg_rqsp]` the file dialog's staleness cookie, `[drv_dlg_seg]` | kernel/fdlg.inc:2973, kernel/driver.inc:4285 | 2 words |
| `[ld_base]`/`[ld_fp+2]` the region being loaded | kernel/loader.inc:1162 | cheaper to **refuse**: pin any region whose base is `[ld_base]`, ~6 bytes |
| `[dskw_seg]`, `[dskw_wseg]`, `[dskw_czseg]` — a **caller's** segment banked across a whole file transfer | kernel/diskw.inc:4922, :4924, :4962 | 3 words |

**SPEC.md 66.6 names four of those and misses seven.** `[menu_seg]`,
`[menu_dseg]`, `[fdlg_rqsp]` and `[ld_base]` are not in its list, and each fails
in a way nobody would trace back: a stale `[menu_seg]` draws every bar title out
of the wrong segment, and a stale `[fdlg_rqsp]` makes the completion callback
silently skip, so the user's Save does nothing. `[menu_dseg]` is live in exactly
the context SPEC.md 66.3 rule 3 names — a menu command claiming with the menu
still down.

**One holder the kernel cannot fix**: `SSI_SEG` in the `OSAPI_SYS_SNAPSHOT`
buffer (kernel/instance.inc:95, :2253) is a **copy in the caller's own buffer** —
the Task Manager's. It is **not** display-only, as an earlier draft said: the Task Manager converts
it to a base KB to place an instance's band, compares a claim's base against it
to classify region-vs-data, and tests window ownership with it
(apps/taskmgr/taskmgr.asm:3254, :4032). Staleness gives a wrong *picture*, not
nothing — and the fix is still one sentence in SPEC.md 20.9 saying the field is a
sample rather than a handle.

**What needs nothing at all is most of the kernel**, and that is the encouraging
half: sound grants, XMS blocks, toast ownership, the dock, the clipboard,
`wm_owner` and every `wm_about`/`wm_onwk`/`wm_oncl`/`wm_onrc`/`wm_pref` hook are
keyed on an **instance slot** or on a near offset read live through `W_SEG`.
There are two existing routines shaped exactly right to reuse: `inst_of_seg`
(kernel/instance.inc:593, **34 bytes**, finds a record by segment) and
`wm_destroy_seg` (**30 bytes**, walks `wm_wins` by segment).

Three things make the region case *cheaper* than SPEC.md 66.6 sounds:

- **Package code is `org 0` with no relocation of any kind**, so every near
  offset inside the image survives a move untouched. Only **segment words** are
  wrong afterwards. That is the whole reason the fix list is finite.
- **`MC_RLOC` is a near offset, not a far pointer** (kernel/memory.inc:84) — the
  segment half comes from the owner's dispatcher at call time — so a move does
  not invalidate any relocation handle, including its own.
- **A plain package needs no relocation proc at all.** `DS = CS` for a package
  and the kernel reloads `DS` from `W_SEG` on every dispatch (kernel/wm.inc:320).
  A package that never forms a segment from its own base and stashes it has
  nothing to fix; multiseg parts and a C overlay are *separate claims* whose
  segments the region's move does not touch.

And **no new API slot is needed to declare a region movable.** A region *is* a
claim, so `OSAPI_MEM_MOVABLE` is already the door; what has to widen is
`mem_find_own`'s fence, which compares the caller's **segment** while a region's
`MC_OWN` is the instance **slot**. That is a better answer than the header flag
this document first proposed, and it keeps the declaration where SPEC.md 66.2
already puts every other one.

So the region case reduces to the one hard thing: **the saved CS**.

---

## 4. The predicate: three exact counters, not a stack scan

> **Is there any frame anywhere that refers to the image at segment S?**

The first draft of this research answered that by scanning every task stack for
the candidate's base. **That is refused** (§4.5), and the reason it is refused is
the most useful thing in the document: the question has an *exact* answer in all
three populations, at a cost comparable to the guess.

### 4.1 The six ways a segment reaches a stack

Any design has to account for all of these, and SPEC.md 66.6 names one:

1. the far-return `CS` the CPU pushes at every `OSAPI_*` call from package code;
2. `OSAPI_SLOT`'s own `push ds` — the caller's `DS`, which for a package **is**
   its segment (kernel/kernel.asm:2694). So an ordinary crossing leaves the value
   on the stack **twice**;
3. `api_x`'s `push es` — a third copy, but only *conditionally*: it is the
   caller's `ES`, which equals its segment only when the package had `ES = DS`.
   Guaranteed per outstanding call is **two**, not three. The X cells are `OSAPI_MEM_CLAIM`, `OSAPI_MEM_FREE`, `OSAPI_MEM_REGROW`,
   `OSAPI_TASK_SPAWN`, `OSAPI_DRV_CALL` … i.e. exactly the calls that can trigger
   a compaction (kernel/kernel.asm:3937);
4. a suspended task's `SCH_FRAME` — `DS` at `[T_SP+0]`, `CS` at `[T_SP+20]`
   (kernel/sched.inc:29-41), written by `task_spawn` before the worker has run a
   single instruction (kernel/sched.inc:873, :889);
5. the CPU's own interrupt gate frame, when an IRQ lands in package code;
6. whatever the **package itself** pushed — Paint does `push ds` at 12 sites,
   Tracker at 5, Word throughout `wdutil.inc`.

Only 4 is at a computable offset. Frames cannot be walked to find the rest: the
kernel has 7 `mov bp, sp` sites and **none of them chain**, so below the one
`SCH_FRAME` at `T_SP` the stack is untyped words. (The 203 chained frames in
apps/ are SmallerC's, and they terminate at the assembly boundary without
crossing into the kernel.)

So no design can *find* the copies. What a design can do is know that **none was
ever made**, and that is a counting question.

### 4.2 A region: `[wm_pkgd]` + no worker + not being loaded — about 11 bytes

Package code runs in exactly three contexts and the kernel opens all three:

- **a callback**, dispatched by `wm_pkgcall` (kernel/wm.inc:320) — one site,
  which already covers paint, key, click, drag, resize, timer, wake and the menu;
- **its entry proc**, far-called once by the loader (kernel/loader.inc:975);
- **its worker**, whose existence is `I_TASK != 0xFF` in the instance record.

A single global byte bracketing the first — `inc byte [wm_pkgd]` before the
dispatch, `pushf / dec / popf` after — plus the same bracket at the loader's
call, makes the predicate exact:

> a region is frameless iff `[wm_pkgd] == 0` **and** its instance's
> `I_TASK == 0xFF` **and** `MC_SEG != [ld_base]`.

**Global and not per-instance, deliberately.** kernel/wm.inc:270 documents that
callbacks nest across packages — a callback may call `OSAPI_WM_SHOW`, which
repaints, which dispatches *another* package's paint proc — and `[snd_inst]` is
one word, so nothing in the kernel can enumerate the nest. A global depth is
conservative in the safe direction and costs one byte.

It also disposes of two cases that look like special pleading and are not.
`wm_destroy` repaints what a window uncovered **on the caller's stack**
(`wm_paint_dmg` → `wm_pkgcall`, kernel/wm.inc:11097), and a C package's worker is
what calls `wm_destroy` — so a worker's stack can carry a segment that is not its
own. The global counter sees that; a per-task rule would not. And the claimant's
own region needs no special case: a package reaches `mem_claim` only from inside
a callback or its entry proc, so its depth is already non-zero.

**Cost: ~10 bytes of `.text` and 1 of `.bss`**, and ~60 clocks on a path that is
already a far call into a callback that does real work.

**The compactor is itself a door into a package, and the counter must not see
it.** `mem_reloc_call` far-calls a holder's relocation proc through `PKG_DISP`
(kernel/memory.inc:1786). Bracket *that* as well as `wm_pkgcall` and the moment
`mem_cp_run` notifies the first movable data claim the depth goes non-zero and
**the compactor pins every region against itself** — a feature that silently does
nothing, which is the worst failure shape available. The bracket belongs at
`wm_pkgcall` and the loader's entry call and nowhere else.

**What makes the counter safe is already there.** `mem_compact` raises
`[sch_lock]` across the plan *and* the moves (kernel/memory.inc:1848), so no task
can raise the depth between the test and the copy — without which a check reading
0 could be falsified by another task during a 135 ms `rep movsw`.

**One trap that binds every predicate here.** `mem_compact` **drops `[sch_lock]`
across the park request** (kernel/memory.inc:1870) and every other task runs for
up to `INST_PARKW` = 4 ticks — ~220 ms — between the first plan and the second.
Any fact sampled before that request is stale after it. It is safe today only
because `mem_cp_run` re-calls `mem_can_move` per block under the re-raised lock;
a design that hoisted the predicate out of that loop for speed would be wrong in
a way nothing would catch.

### 4.3 A module: ONDEMAND-PLAN's per-module count — about 104 bytes

docs/plans/completed/ONDEMAND-PLAN.md §7.1 already specified it: *"a one-byte
nesting count per module, pinned while non-zero, incremented by the stub before
the far call. Per module and not one flag, because a module can re-enter itself
through a callback."* `resb MOD_MAX` in `.bss` (4 on kern_big, 5 on kern_small),
incremented in `mod_need` — which already holds the id — and decremented by a
small `mod_leave` that each of the **29 thunk sites** (over seven far-pointer bases in seven files — `CMZFP` is `CLFP+4`, the compressor riding in `CLONE.DRV`) calls after its
`call far [XXFP + K*4]`.

That is the pin `mod_drop`'s banner says nothing can supply, and with it
ONDEMAND-PLAN §7.2 lands: `MEM_K_MOD` becomes `MEM_PG_LOW` and `mem_cp_drop`
dissolves it under pressure.

### 4.4 A driver: half of it is already built and exact

`[drv_wcnt]` (kernel/driver.inc:4277) counts live driver worker tasks — *"+1
inside the spawn's IF=0 window, -1 inside `task_exit`'s"* — and **`drv_unload`
already waits for it to reach 0 before a byte of the image is freed**
(kernel/driver.inc:2961, SPEC.md 51.7). What is missing is the other direction, a
depth count over the six dispatchers (`drv_call`, `drv_svc_call`,
`drv_blk_call`, `drv_fs_call`, `drv_cp_call`, `drv_pkg_call`) — about 40 bytes
with a shared leave.

### 4.5 Why the stack scan is refused

The tempting alternative is one routine that sweeps every task's live stack for
the old base. It is small (~60–95 bytes), it needs `sch_stkbase`/`sch_stksize`
which already exist and already include slot 0, and it breaks nothing published —
*"which is exactly what makes it seductive."*

**As a patcher it is unsound, and the arithmetic is the argument.** `HEAP_SEG` is
0x1B60 = 7,008 and the ceiling is 0xA000, so every heap segment number lies inside
the near-pointer range of two things that are dense on a stack: kernel return
addresses (`.text` is 50,892 bytes) and a package's own near pointers (more than
half of Note Pad's 20,453-byte address space collides). A conservative sweep would
therefore overwrite a coincidental match — silent corruption of a return address.
SPEC.md 66.3 rule 5 refused an unenforceable rule in this exact problem space on
this exact reasoning; it is the project's own standard.

**One correction to that arithmetic, because arguing from a wrong number is the
failure being guarded against.** Alignment *does* help, and an earlier draft said
it did not. A region base is `HEAP_SEG + n×64` paragraphs and `MC_PARA` is in the
same units (kernel/memory.inc:632, :739), so a colliding word has to be 64-aligned
**and** equal — 32–64× fewer candidates than a uniform model gives, which takes a
~0.6% per-pass exposure to roughly **0.01%**. **The refusal does not rest on that
number**: a fail-silent heuristic loses to a fail-safe-by-construction standard at
any rate, and the density of near return addresses is not uniform anyway. But the
verdict must not be argued from a figure that is two orders out.

**As a refuser it is sound and still pointless.** Refusing on a coincidence is
the safe direction, but it buys nothing the counters do not already give exactly.

**And there is a blindness both share.** A scan and a per-call counter are each
sound about *frames* and blind to a **held span** — a stretch with nothing
executing in the image that the feature still needs (§3.3's three, one of them
across a disk swap). Wherever a held span exists the pin has to be taken by the
feature, per span, and no amount of stack inspection substitutes.

Between the two, the counters still win on every axis — exact rather than
probabilistic, comparable in bytes, and *"grey a fact, never a guess"*
(SPEC.md 47) is the house rule they satisfy and the scan does not.

### 4.6 THE LIMIT: a worker-owning package is still pinned

None of this reaches a package that owns a worker, and the reason is §4.1 item 4:
`task_spawn` writes the package's segment into the worker's initial `DS` and `CS`
slots before it runs, and from then on the worker's stack carries it at an
arbitrary depth — parked at `OSAPI_TASK_ALIVE`, parked in `gfx_lock`
(`inst_park_lk`, SPEC.md 66.5.4 — the park added precisely to reach *drawing*
workers, the ones with the big claims), or pre-empted anywhere else. Three park
shapes, only one tabulatable, and above every one of them the package's own chain
which pushes `DS` itself.

A targeted two-word patch at the `ALIVE` park is arithmetically possible — the
offsets are `T_SP+40` and `T_SP+44`, from a fixed push chain across
kernel.asm, instance.inc and sched.inc — and it is **refused**: it needs a new
author rule (*"do not bank your own segment above the crossing"*) that is
unenforceable and already violated, it is a distance dependency across three
files that nothing asserts, and it reaches the sleepers rather than the drawing
workers that matter.

**What that costs, exactly.** Re-counted on §2.2's own basis (the `image` field
alone) over EVERY shipped package rather than the 26 that table lists — it
leaves out the six largest, Word, LOOM, WEAVE, CWORD, C64 and Frotz, and counts
WIREFRAME, which is an instrument and ships on no floppy: **20 worker-owning
packages stay pinned, 354,316 bytes; 12 worker-less ones are reached, 232,460**
(RUNCPM is worker-owning and was not built in the tree this was counted on, so
the pinned side is larger still).

**"every C package" was wrong and §2.2 already says so** — `apps/c64/c64.asm:44`
and `apps/loom/loom.asm:112` each state in their own source that they take NO
worker, and they are the **two largest members of the movable side**, LOOM at
54,994 and C64 at 41,442. Only CWORD, RUNCPM and WEAVE define `CC_HAS_WORKER`.
SHEET, PAINT and TEXPAD are there too, and SHEET is the package that claims
~100KB of heap on open (SPEC.md 24.5.2), which is to say the one most likely to
be standing where the memory is needed.

**Under today's ABI that limit is absolute**: the frame cannot be found (§4.1)
and cannot be forbidden (§4.5). §4.7 is the way past it, and it is a change to
the ABI rather than to the compactor.

One claim made elsewhere in this research is worth refuting here: it is **not**
true that an `MC_DMA` block is unmovable in principle. The 64KB page rule
constrains where such a block may *land*, not whether it may move — §3.1 and §3.2
between them show that three of the four `MC_DMA` claims in the tree have no bus
master on them at all, and that the fourth already has both halves of its quiesce
built.

### 4.7 Past the limit: tell the package, and let it give its worker back

The limit above is *"the worker's stack holds the segment at depths nothing can
find"*. The way past it is not to find them — it is to **arrange for the stack not
to exist**, and that is a thing only the package can authorise.

**The API.** `OSAPI_TASK_RESTARTABLE(offset)`, declared by the worker itself,
cleared with 0. It says:

> *While this stands, my worker's stack holds nothing that matters. If you have to
> move my region, throw the stack away and re-enter me at `offset` in the new
> segment.*

Same shape and same spirit as `OSAPI_MEM_PARKSAFE` (SPEC.md 66.5.4), which is
already a package telling the kernel it may do something to its worker; same
default, which is *no*.

**What the kernel does with it.** Nothing new — it is `task_spawn`'s tail
(kernel/sched.inc:857-892) run again on a slot that already exists:
`T_SP = slice top − SCH_FRAME`, then `DS = CS =` **the new segment**, `IP =` the
declared offset, the rest zeroed. About 35 bytes, and the slice is **never
released**, which matters: a kill-and-respawn could fail to get its slot back and
the kernel would have destroyed a worker it could not promise to return. Rebuilding
in place cannot fail. (`task_exit` is the wrong primitive for a second reason —
it releases the instance record, kernel/sched.inc:`.norel`.)

**It reaches both park points, including the one that matters.** A worker parked
at `OSAPI_TASK_ALIVE` is spinning in `inst_park_hold`; a worker parked in
`gfx_lock` is spinning in that routine's `.block` retry arm
(kernel/vga12.inc:5366) and **holds no lock there** — that is what it is retrying
for. Either can be discarded. So this reaches the *drawing* workers, which
§4.6 could not and which SPEC.md 66.5.3 says are the ones with the big claims:
Tracker's 114KB module sat still with 38KB free directly beneath it.

**What it costs the package, honestly.** Three things, and the second is the one
that will bite:

1. Everything the worker's loop needs must live in the **region** (which moves
   with it) or a **claim** (which has its own relocation proc), never on the
   stack across the declaration.
2. The declaration is a *window*, not a property. A worker that mixes audio or
   interprets Z-code declares only between units of work, exactly the way
   `OSAPI_MEM_PARKSAFE` is set around `gfx_lock` and cleared after
   (kernel/instance.inc:536). A worker that declares while mid-anything loses it.
3. The re-entry is a **restart, not a resume** — the loop begins again. For a
   pump, a poller, a game loop or a redraw worker that is free. For Frotz's
   interpreter it is not, and Frotz will not declare.

**What it costs the SDK.** One appended API cell. `kernel/kernel.asm:3849`
asserts the table is exactly 161 8-byte slots, so the assert and the SDK mirror
change — but **appending moves no published offset, so no package needs
rebuilding**, and a package built before this simply never declares and stays
pinned. One word per instance for the offset (`INST_MAX*2` = 24 bytes of `.bss`,
a side table like `inst_parksafe`, because `I_RECSZ` is full).

**What it is worth.** It is the only route found past §4.6, and it puts the other
**354,316 bytes** of region in reach — Word, WEAVE, CWORD, Frotz, Browser, ftpd,
Telnet, the Task Manager, the games, ModPlug and Tracker between buffers. One
change in `apps/cc/crt0.asm` covers CWORD, RUNCPM and WEAVE at once: §12
question 10 checked the runtime's loop and it IS re-enterable. **All eighteen
assembly workers could declare the point too, and §12 question 11 says why that
is the easy half** — the hard half is that the honest declaration is *"my
statics are consistent here"* rather than *"my loop restarts"*, and the one
point where that is true is `OSAPI_TASK_ALIVE`, which SPEC.md 66.5's park
already stops them at.

**Estimate: ~90 bytes** on top of piece C — 35 for the frame rebuild, 24 `.bss`,
~20 for the API cell and its setter, ~10 for the `mem_can_move` arm that accepts a
parked-and-restartable worker instead of demanding `I_TASK == 0xFF`. ESTIMATE,
calibrated on `task_spawn`'s measured tail and on `inst_parksafe_set` (22 bytes).

**The one thing to be careful of** is that this hands the kernel the power to
destroy work. `OSAPI_MEM_PARKSAFE` only ever costs a package a missed
optimisation if it is declared wrongly; this costs it a lost loop iteration, and
if the worker was holding something the declaration was wrong about, it costs
correctness. That asymmetry should be stated in the SDK next to the call, and it
is the reason the declaration is a window rather than a header bit.

### 4.8 What it costs to run

A block copy is **13.3 cycles a byte** (`rep movsw`, PERFORMANCE.md:11154) =
**2.79 ms per KB** at 4.77 MHz:

| | copy time |
|---|---|
| the 8KB sound ring | 22 ms |
| a 6KB module | 17 ms |
| an 11KB `ETHER.DRV` | 31 ms |
| a 48KB SHEET region | 134 ms |

All of it sits inside the price the compactor already pays: `mem_compact`'s worker
park costs up to `INST_PARKW` = 4 ticks ≈ **220 ms** (kernel/instance.inc:652).

---

## 5. The descending pass — needed for *conditionally* movable claims, and only those

**The claim record carries no direction bit.** `MC_SEG`, `MC_PARA`, `MC_OWN`,
`MC_DMA`, `MC_RLOC` and nothing else (kernel/memory.inc:71), so
`mem_cp_plan`/`mem_cp_run` walk every live claim in ascending base order
regardless of which door claimed it. **A top-claimed block that merely declares a
relocation proc is slid all the way down to the bottom fill point by today's
engine, unchanged.**

An earlier draft of this document concluded from that that a descending pass was
required before any of §3 could ship. **That is too strong, and the correction is
worth having**: if a claim is *unconditionally* movable, sliding it to the bottom
is harmless — SPEC.md 50.3's warning is about a *long-lived, unmovable* block
splitting the arena, and a movable one is neither. Better still, if everything
became movable, **one ascending pack leaves every claim contiguous from
`[mem_base]` with a single free run to `[mem_top]`** — the ideal layout — and
SPEC.md 66.4's one-sentence termination argument survives verbatim, because a
claim still only ever slides down onto paragraphs the walk has already passed.

**But the softening only goes so far, and the precise rule is about the
population rather than the claim.** `mem_cp_run`'s fill point starts at the heap
**floor** and copies every movable block down to it (kernel/memory.inc:1637). So
an ascending pack is ideal only when the top-down population is *entirely*
movable. While any of it is pinned, sliding the movable subset to the floor moves
the free run from *below the whole stack* to *between the movers and the
stayers* — and the machine ends up with two runs where it had one. **That is
strictly worse than doing nothing.**

§4.6 guarantees exactly that situation for regions: a worker-owning package's
region cannot move, so piece C without piece E slides a 28–48KB region out of the
ceiling into the middle of the data arena and splits the free space. **So E is a
precondition for C after all**, and for A and F, and the only piece it is not
required by is B — because a dropped module is not moved anywhere.

**And for a claim that is movable only SOMETIMES it is required twice over**,
because such a claim needs somewhere to be put *back*:

- the **sound ring**, movable while `[sbl_str_act]` = 0 (§3.2). Slide it to the
  bottom while the machine is silent, let the user then open a stream, and there
  is a pinned bus-master block in the middle of the data arena — SPEC.md 50.3's
  warning made real by the fix.
- a **region** whose worker is restartable only inside a declared window (§4.7),
  and a **module** outside a held span (§3.3).

So the honest rule has two halves: **an ascending pack is safe only when the
whole top-down population moves, and a conditionally movable claim needs
somewhere to be put back.** Piece B is the only one that needs neither.

The shape is a mirror of the existing pair — parameterise the direction through
both bodies plus a descending twin of `mem_cp_next`, or duplicate them.
**~120 bytes parameterised, ~220 duplicated** (ESTIMATE, on the measured bodies:
`mem_cp_plan` 104, `mem_cp_run` 115, `mem_cp_next` 55).

**It needs to know which door a block came in through, and that should NOT be a
new field.** `MC_` has five and none records the direction (kernel/memory.inc:71).
A direction bit is `MEM_MAX` × 2 = 64 bytes of `.lowbss`, which has 34 bytes of
its rung left — so it would cross one. **The direction is derivable from what the
record already holds, and `mem_can_move` already derives exactly this
classification** (§2): `MEM_K_DRV` and `MEM_K_MOD` are tags, a region is
`mem_is_region` on the instance-slot arm, a bus-master buffer carries `MC_DMA`,
and a driver-owned claim has a driver segment as its owner. Every one of §5.1's
seven top-down sites is identifiable that way without storing anything — at the
cost of a few compares per claim, on a path that is about to `rep movsw` up to
64KB. Confirm it against those seven before relying on it; on that enumeration it
holds for all of them.

**One claim gets its 14KB back for nothing, and §5.1 is why it is safe.**

---

### 5.1 ETHER.DRV's socket pool — the comment that pinned it is wrong

There are **seven** top-down call sites in the whole tree — three kernel
(loader.inc:810, driver.inc:2474, mod.inc:422) and four driver (sound/sb.inc:2484,
ramdisk/rdpage.inc:108, hdd/hdtool.inc:161, ether/tcp.inc:784); **no package uses
either door.** Six of the seven claim a CS or a bus-master buffer. The seventh
is **ETHER.DRV's 14KB socket pool** (drivers/ether/tcp.inc:784), and the
comment beside that claim says:

> the card's own descriptors point into these rings and it DMAs into them, so
> this block is as unmovable as a driver image

**Both clauses are false for this card, and the same file contradicts them 100
lines earlier.** The objection they encode — *the moment an IRQ from the card
arrives it writes into memory we have moved something else into* — cannot happen
here, for three independent reasons, any one of which is sufficient:

1. **There is no IRQ.** `ETHER.DRV` hooks no vector at all, deliberately:
   *"IT POLLS, AND HOOKS NOTHING — No interrupt vector, no IRQ line, no DMA
   channel"* (drivers/ether/ether.asm:27), and drivers/ether/ne2000.inc:41 gives
   the reason — *"an ISR would have to run at IF=0 on a machine whose kernel is
   not re-entrant"* (SPEC.md 72.2.1). The card's ring is drained by polling from
   inside the socket verbs a package already calls once a tick.
2. **The card has no path to host RAM.** Received frames land in *the card's own*
   packet memory — a circular buffer of 256-byte pages between PSTART and PSTOP,
   `CURR` being where the card will write next and `BNRY` where we have read to
   (drivers/ether/ne2000.inc:28). The 8390's two DMA engines are both internal:
   *"the card's LOCAL DMA wraps at the ring end; its REMOTE DMA — the window we
   read through — does not"* (:35). The host side of remote DMA is
   `in al, dx` / `stosb` through one port (`ne_dma_read`, :207). **The CPU moves
   every byte.** There are no host-memory descriptors to point anywhere.
3. **No task can be inside the pool during a compaction.** The pool is a
   *software* ring the driver fills with the CPU from inside socket verbs, which
   run on the calling package's task — and `mem_compact` raises `[sch_lock]`
   across the plan and the moves. The only claim in the whole driver is
   `sk_claim` (tcp.inc:784), reached only through `sk_reclaim`, which calls
   `sk_release` **first**: at the one moment a compaction can fire from inside
   this driver, `[sk_seg]` is 0 and there is no pool to move.

**And the driver's own author already wrote down that a move is safe.**
tcp.inc:680, explaining why `sk_reclaim` frees and re-claims rather than
regrowing:

> A FREE AND A CLAIM AND NOT A REGROW, deliberately: **`mem_regrow` can move a
> block, and everything that reads a ring reads `[sk_seg]` fresh through
> `sk_ring`, so a move would be safe** — but the sizes and the shifts change with
> the rung and every ring's CONTENTS would then be at the wrong offsets inside it.

That is a statement about a **rung change**, not about a move. The move is
conceded in the same sentence.

**So the relocation proc is one word.** `sk_ring` (tcp.inc:813) is the single
accessor — *"every ring access goes through this rather than loading `[sk_seg]`
by hand"* — and re-derives `ES` from `[sk_seg]` on every access; `[sk_base]`
(ethstate.inc:142) is an **offset inside** the claim and does not change when the
claim moves. `mov [sk_seg], dx / ret` is `clip_reloc`'s shape, and the
right calibration is **`sbl_reloc` at 11 bytes** — because **the sound driver
already does exactly this**. `drivers/sound/sb.inc:2579` is a working
relocation proc for a *driver-owned* claim, declared at :2604 with
`OSAPI_MEM_MOVABLE`, and its comment names the reason: *"this is the claim
docs/FIELD-NOTES.md 2 named as the fragmenting party: a LATE claim, taken on the
first stream grant, so it lands above whatever the app already holds"*. **A late
driver claim fragmenting the heap is a solved problem in this tree — solved
once, for one claim, by exactly the five-line change §5.1 proposes for the
second.**

**Placement should not change.** Top-down is still right — it is a long-lived
driver-owned block, and docs/HEAP-CLAIMS.md's *"placement is a second axis"*
applies. What changes is only that it stops being a **wall**; and because
SPEC.md 66.7 forbids packing into a hole *below* a pinned block — so a movable
claim can never pass one — declaring it slides the pool down onto the highest
pinned block beneath it and merges the gap under it with the run above.
**A strict improvement, needing no descending pass.**
**BUILT, once E was.** It was deferred by decision and not for want of
evidence — the owner's call: *"leave it for now, it can compact to the top with
the rest after we implement this work."* That was the right sequencing:
declaring the pool movable **before** §5's descending pass existed would have
slid it *down* to close the gap beneath it, and what is wanted is for it to
pack **up** with the rest of the ceiling. `sk_reloc` is the eleven bytes this
section predicted, `sbl_reloc`'s shape to the instruction, plus four at the
claim site.

`tests/ethernet.py` gained assertion **1c**, and it reads `mem_tab` rather than
the driver — which is the whole point, because `mem_movable`'s fence is *yours,
or not at all* and a refusal is a CF the driver discards. On QEMU 8.2.2 it
reads `pool claim 97C0 14KB owner 9B40 MOVABLE HI` with the rest of the gate
still green end to end (card, DHCP, the browser fetching over TCP). **Broken on
purpose**: with the `OSAPI_MEM_MOVABLE` call replaced by a `nop` it reads
`PINNED HI` and exactly one assertion goes red, everything else unchanged.

What this does NOT assert is that the pool has been seen to move — that needs
§2.0's whole session (mount, open three apps, mount, unmount, close) on a
machine with a NIC, and MartyPC has none. The claim is made in two halves
instead, and each half is a gate: `heapcheck` check 13 says the descending pass
moves a declared top-down claim, and this says ETHER's pool is a declared
top-down claim.

---


## 6. Where it goes: tier 3 of `mem_compact`

`mem_compact` (kernel/memory.inc:1816) already escalates once:

```
plan -> worth? -> run
     \-> not worth, and [mem_wpin] says a WORKER blocked it
         -> drop [sch_lock], inst_park_req (up to 220 ms), re-plan -> worth? -> run
```

The hard pass is **tier 3 in the same ladder**, gated on a new `[mem_hpin]`
exactly as tier 2 is gated on `[mem_wpin]`: *the last plan was blocked by a
CS-based claim, and standing the workers did not fix it.*

Three properties fall out of putting it there and nowhere else:

- **`mem_cp_worth` already refuses a pass that would not help** — *"IT STILL
  WOULD NOT FIT. Move nothing … a quarter second of memcpy would not change
  that"*. The expensive pass is never paid speculatively, which is what makes
  "last resort" enforceable rather than aspirational.
- The monotone-slide termination argument (SPEC.md 66.4) is untouched: more
  blocks become movable, none becomes movable *upward*.
- `HEAPCOMPACT=0` keeps its meaning, because it pins everything in
  `mem_can_move` and the plan then sees the heap it really has (SPEC.md 66.8).

---

## 7. The bill, in kernel bytes

**A correction that moves the tight budget.** A kernel relocation proc is
dispatched through `cw_mem_disp`, which is `call bp` with `CS = KERNEL_SEG`, so
**BP has to be a `.text` offset** (kernel/memory.inc:1697) — which is why
`mem_region_reloc` sits in `.text` inside a `.cold` file. So the region and driver
fix-ups below are **`.text`, not `.cold`**, and they spend the budget with 8,901
bytes left rather than the one with 18,944.

**Calibration.** Counted narrowly, SPEC.md 66 as shipped is **801 bytes of
`.cold`**
(`mem_can_move` 80, `mem_is_region` 31, `mem_busy_seg` 16, `mem_in_xfer` 33,
`mem_cp_drop` 21, `mem_pg_cheap` 21, `mem_cp_next` 55, `mem_cp_plan` 104,
`mem_cp_run` 115, `mem_reloc_call` 89, `mem_compact` 122, `mem_cp_worth` 16,
`mem_cp_unpark` 20, `mem_bcopy` 58, `mem_movable_x` 20) plus **279 bytes of
`.text`** for the park — **362** once `inst_of_seg` (34) and `inst_svc_parked`
(49) are counted with it — and ~25 bytes of `.bss`. Counted to include the five
kernel relocation procs and the `.lowbss` it uses, an independent pass puts the
whole built feature at **~1,426 resident bytes** — 870 `.cold`, 350 `.text`
(park), 115 `.text` (relocation procs), ~68 `.lowbss`, ~23 `.bss`. Both are
measured off `tools/os88sym.py --all`, by diffing each symbol against the next
global one in its section; the second is the fairer anchor for anything new,
because a new mechanism needs its fix-up procs too — with the caveat that it
**over-attributes by roughly 133 bytes**, four of the routines counted being
shared with features that predate compaction (`mem_bcopy` is `mem_regrow`'s
mover, `mem_fatw_dirty` and `mem_pg_cheap` the purgeable shed's). Other measured shapes used below: `dsk_dseg_reloc` 36 (one
scan-by-value over a table plus one live word), `fm_reloc` 52, `mem_hifit` 93,
`inst_of_seg` 34, `wm_destroy_seg` 30, `mod_drop` 15, `mod_disarm` 19,
`drv_unload_x` 70, `drv_load_row` 193, `drv_call` 61, `wm_pkgcall` 45.

**memory.inc is `.cold`**, so most of this spends `KERN_BUDGET` and not
`KERN_CODE_MAX` — which matters, because kern_big has **18,944 bytes of
`KERN_BUDGET` spare and only 8,901 of `KERN_CODE_MAX`** (kern_small: 28,672 and
21,543). The accrued line is worth quoting with them, because two of these
pieces spend `.bss` and `.lowbss`: **image 315/512, cold 171/512, low 478/512,
vgabuf 336/512**. `.lowbss` is the one nearly spent — which is a fact to know
before adding a `MC_` field, not a reason to size one differently (CLAUDE.md's
rule). The three items that must be `.text` are the `wm_pkgcall` bracket, the
`mod_leave` calls at the thunks, and the driver dispatch counters.

It is priced as **five separable pieces**, because they are separable and the
first two are worth taking whatever is decided about the rest.

| # | piece | bytes | of which `.text` | basis |
|---|---|---:|---:|---|
| **0** | **Documentation only.** Add the four holders SPEC.md 66.6 misses; note `SSI_SEG` is a sample not a handle; correct SPEC.md 66.9 reason 5 and docs/HEAP-CLAIMS.md's donated-listing row, both stale; delete the dead `[ty_selfseg]` write | **0** | 0 | −4 bytes from every package that includes apps/os88type.inc |
| **A** | **Stop pinning three claims for a placement constraint** (§3.1): `mem_can_move` drops the `MC_DMA` refusal, `mem_cp_plan`/`mem_cp_run` bump the fill point to the next page-safe base | **~50** | 0 | ESTIMATE; `mem_dmaok` (28) exists and is the whole test |
| **B** | **Modules become purgeable** (§3.3, §4.3) at `MEM_PG_MED`, with a PER-SPAN pin and `FDLG.DRV` excluded: `resb MOD_MAX` count, `mod_leave` ~12, 29 thunk sites × 3, +5 in `mod_need`, `mem_pg_forget` arm ~14, `mem_cp_drop` guard ~8, plus the three held-span brackets | **~215–240** | ~180–200 | ESTIMATE, and **three passes disagreed**: 104, 152–168 and 215–240. The high figure is the right one — the bracket **cannot** be an increment in `mod_need`, both because ONDEMAND-PLAN §7.1 says *"incremented by the stub before the far call"* and because kernel/ctrl.inc:5839 far-calls the module after `mod_live` with **no `mod_need` at all**. So it is enter+leave per site, plus the held-span arms. **§12 question 3 has since named all three exceptions and counted the sites**: 29 thunks far-call through seven FP blocks, 26 of them `need`/`jc`/`call far`, and the three that are not are files.inc:5461 and ctrl.inc:5929 (a `need` with no far call — both held spans) and ctrl.inc:5839 (a far call with no `need`) |
| **C** | **Regions move when idle** (§3.5, §4.2): `[wm_pkgd]` + its two brackets ~11, the fix-up routine ~110, the `mem_can_move` arm ~20, widen `mem_find_own`'s fence ~20, the `[ld_base]` refusal ~6, tier-3 gate ~15 | **~200** | ~11 | ESTIMATE; the fix-up is `dsk_dseg_reloc`'s shape over four tables and five words. Two agents arrived at ~110 independently |
| **D0** | **Driver unload/reload as a policy step** (§3.4): the mechanism is BUILT — `hbm_detach`/`hbm_reload` are 91 bytes, `ss_reap_x` does it per session. Only a policy hook is new | **~40** | 0 | ESTIMATE. Reaches **more** memory than a move (the 8KB ring and ETHER's 14KB pool) and breaks nothing, because a package names a driver by CLASS |
| **D** | **Driver images move in place** (§3.2, §3.4, §4.4): the 66-word fix-up, a dispatch depth count over SEVEN sites, the mask/unmask bracket, `DRVV_QUIESCE`/`DRVV_REARM`/`DRVV_RELOC` | **BUILT: 260** (est. 240–310) | 28 | MEASURED (§10.12): `.text` +28, `.bss` +9, `.cold` +223, and **none of it on kern_small** - `OS88_DRIVERS` is `KERN_BIG`-only, so the build that cannot load a driver pays nothing. THREE of the four estimated pieces were not needed: no `DRVV_*` verb, no mask/unmask, and the fix-up is table ROWS | The depth count is **MEASURED at 15 bytes a site**: `driver.inc:1552` is reached from `snd_tick` inside IRQ0 and a bare `inc byte [mem]` is an interruptible read-modify-write on an 8086, so each site is `pushf/cli/inc/popf` — **105 bytes**, or ~72 through a shared `drv_enter`/`drv_leave`. The rest ESTIMATE; `[drv_wcnt]`'s half costs 0 |
| **E** | **The descending pass** (§5) — **A, C and D are harmful without it** | **~120** | 0 | ESTIMATE; parameterising `mem_cp_plan` (104) + `mem_cp_run` (115) + a descending `mem_cp_next` (55), with direction DERIVED from the tag/owner rather than a sixth `MC_` field (§5) |
| **F** | **Worker-owning regions, by declaration** (§4.7): `OSAPI_TASK_RESTARTABLE`, the frame rebuild, the `mem_can_move` arm | **BUILT: 231** (est. ~90) | 177 | MEASURED (§10.10): `.text` +177, `.bss` +24, `.cold` +30, and it crossed the image rung. 2.6x the estimate, and §10.10 says where |

**Everything: ~915–1,010 bytes**, of which **~430–460 is `.text`** once the
fix-up procs are counted there — against 8,901 bytes left of `KERN_CODE_MAX`.
Comfortable, but no longer the rounding error the `.cold` framing suggested. Against the 801 bytes the
existing engine cost for a comparable amount of machinery that is plausible, so
**plan against 650–900**, and treat any single row as ±40% — piece B's two
estimates already differ by 60%.
**Nothing here has been assembled**; every figure is hand-encoded 8086 calibrated
against a measured routine, which is exactly the kind of estimate PERFORMANCE.md
rule 5 says to distrust.

**None of it can be an on-demand module**, and the argument should be recorded
rather than rediscovered: this is code that runs when the heap is already
exhausted, which is exactly the moment a module read would need heap it does not
have. It is resident by necessity.

**Per-package and per-driver cost: zero** for anything that does not declare
itself movable. Piece C needs **no new API slot** — a region *is* a claim, so
`OSAPI_MEM_MOVABLE` is already the door. Piece D adds two `DRVV_*` verbs, which
SPEC.md 51.2's own rule makes additive (*a driver that does not implement it
returns*), and every driver holding a self-referential far pointer must implement
it — `RAMDISK.DRV` (`[rd_pfar+2]`) and `HDD.DRV` (`[hd_tfar+2]`) do.

**kern_small is the build to check, not kern_big.** It has `MEM_MAX` 20 rather
than 32, `MOD_MAX` 5 rather than 4, and 28,672 bytes of `KERN_BUDGET` spare — but
docs/plans/KERN-SMALL-CUT-PLAN.md's rule is that **bytes returned to kern_small
stay returned** (SPEC.md 39.27.4), so ~650 spent there is spent against a budget
nothing may draw on. That is a decision for whoever owns that machine, not a
build fix.

---

## 8. The broken interfaces

Ordered by how much they cost somebody who is not reading this document.

1. **SPEC.md 66.9 reason 1 is retracted.** *"Its base IS a CS — permanent"*
   becomes *"…unless the image declares itself relocatable and no frame refers to
   it"*. That sentence is quoted in `mem_can_move`'s own comment, in
   `mem_busy_seg`'s, in `mem_is_region`'s and in drivers/sound/sb.inc:2489
   (*"it can no more be moved than a package's region can"*). Four comments and
   one contract.
2. **SPEC.md 20.1 (a region's base) and SPEC.md 51.3 (a driver's image) stop
   being stable for the life of the instance.** A package that cached its own
   segment somewhere the kernel cannot reach breaks *silently*. The
   `OSAPI_MEM_MOVABLE` declaration keeps that from being a flag day; the cost is
   a promise nothing can check, which SPEC.md 66.3 rule 5 is on record as
   disliking. §4.3's survey says only the C SDK's `cc_ovbind` needs it today.
3. **`MEM_K_MOD` becomes purgeable**, so a module can vanish between two calls.
   `mod_disarm` already makes the far pointers safe, but SPEC.md 13.8.3's
   deliberate no-`mod_need` thunks (kernel/ctrl.inc:5820) change meaning from
   *"the image is certainly in"* to *"the image is in unless the heap took it"*.
   Each of those sites needs re-deciding. **This is the one change here that
   alters an existing deliberate decision rather than extending it.**
4. **`FDLG.DRV` on kern_small must be excluded from the shed, or six thunks must
   change.** Its entry thunks far-call **without** reloading
   (kernel/fdlg.inc:3108-3167) and `mod_tab[MOD_FDLG].MODR_SEG` is the *reap
   guard* read at kernel/ui.inc:546 and kernel/kernel.asm:6511 (SPEC.md 38.0.1).
   Shedding it leaves a dead, undismissable dialog whose `fdlg_grab_x` swallows
   every press. **This is the sharpest break in the document** and it is on the
   machine the whole exercise is for.
5. **NOT a break, recorded so it is not re-raised**: `[hb_wakep]`
   (kernel/hiber.inc:135) is a far pointer into `HIBER.DRV`'s image, but it is
   written at step 4 of `hbm_perform` and consumed at step 6 of the same run,
   holding both the gfx lock and `[sch_lock]`. Nothing can shed the image
   in between.
6. **`MC_DMA`'s meaning splits** into "must land page-safe" (kept, SPEC.md 50.3)
   and "a chip is armed on it" (moves to the driver verbs). SPEC.md 66.9 reason 2
   states them as one thing.
7. **`mem_reloc_call` gains a "before" phase** for claims whose owner published
   `DRVV_QUIESCE`. SPEC.md 66.3 rule 3 — a relocation proc may not claim — must
   extend to it, and the driver ABI gains two verbs.
8. **`wm_pkgcall` gains two instructions on the hottest kernel→package path
   there is** — every paint, every keystroke, every click of every package. Ten
   bytes and ~60 clocks against a routine that is 45 bytes today. It is almost
   certainly noise beside the callback it dispatches, and it has not been
   measured (open question 6).
9. **The compaction becomes non-UI-task-only in principle.** §4.1's bound rests
   on SPEC.md 20.6 rule 7, which is a rule about *packages*. If a driver verb
   ever claimed from a service task the running stack would be a 384-byte slice
   instead of `STK0`, silently. Worth an assertion rather than a comment.
10. **`HEAPCOMPACT=0` must pin the new categories too**, or the A/B kernel reports
   a run a compaction it cannot perform would have left — SPEC.md 66.8's own
   trap, one population further on.
11. **`SSI_SEG` in an `OSAPI_SYS_SNAPSHOT` buffer becomes a sample, not a
    handle** (kernel/instance.inc:2253). It is a copy in the *caller's* memory,
    so no kernel fixup can reach it. Display-only in the Task Manager today; it
    needs a sentence in SPEC.md 20.9 before anything else reads it.
12. **`mem_find_own`'s fence widens** so a region may declare itself movable
    through the existing slot. That is an ABI *widening*, not a break, but it
    means a package can now hand `OSAPI_MEM_MOVABLE` a claim whose `MC_OWN` is a
    slot rather than its segment — a case the fence exists to reject.
13. **docs/HEAP-CLAIMS.md's verdict column loses "PINNED (forever)"**. Two of its
    rows and two of SPEC.md 66.9's six reasons are *already* stale, independent of
    anything here: **reason 5** (the donated HDD listing) was built as
    `dsk_dseg_reloc`, and **reason 6** names `MEM_K_FATW` as *"the only honest
    declarable left"* when that tag no longer exists — kernel/memory.inc:109
    records *"0xFF05 was `MEM_K_FATW` … it is `MEM_P_FATW` below now: a cache"*.
    docs/HEAP-CLAIMS.md also omits `MC_DMA` from its `MEM_P_DIRW` row and from
    Word's typeface row (§3.1).

---

## 9. What it buys — and the argument against

**For.** The top-down stack is where the memory is: ~69KB of driver furniture on
a fully-loaded machine (§3.4), 8–9KB of module image, and every package region.
When a claim fails against two free runs that would together satisfy it, nothing
in the machine can merge them today.

**Against, and it is the stronger case.** Four things:

1. **The prize is a merge, not a reclaim.** `mem_claim_1`'s `.hi` arm already
   refills any ceiling hole big enough, and `mem_cp_plan` already reports each one
   — so `mem_avail` already sees them. The gain is (sum of runs) − (largest run),
   in the instant a claim exceeds the largest. Nobody has photographed that
   instant (open question 1).
2. **The cost is the largest copy this machine can make.** ~209KB of top-down
   stack at 2.79 ms/KB is **~583 ms**, against a park that costs 220.
3. **The real mid-arena barriers are elsewhere and cost almost nothing** (§2.1.1):
   a package's overlay image claimed bottom-up with a CS base, SHEET's 99KB of
   undeclared claims, and a C SDK with no `os88_mem_movable` at all. A slot
   number, a declaration and one SDK function.
4. **A boot-time ceiling reservation was proposed and is REFUSED.** `drv_memk`
   (kernel/driver.inc:1091) is *"one word per `drv_tab` row, in the same order"*,
   so the kernel could reserve a ceiling band at boot and have a mid-session
   mount land in it. No ABI, no relocation, no predicate. **The owner's answer
   is that it fails on both counts**: *"reserving multiple kb of ram to try to
   save 850b of ram is not a good trade — and still wouldn't work in the end
   unless we reserved enough we could never overrun it."*

   Both halves are right and the second is the one that kills it. A **partial**
   reservation does not remove the wall, it relocates it: the first mount that
   does not fit the band lands below it exactly as today, and the machine is
   then short the band *as well*. Only a reservation large enough that nothing
   can ever overrun it works, and that is ~48KB of a 640KB machine permanently
   unavailable — spent to avoid ~850 bytes. **Recorded so it is not re-proposed
   as a cheap alternative; it is neither cheap nor an alternative.**
5. **The user who is out of memory usually wants a program to go away.** Closing
   one returns 5–48KB of region **plus every claim it holds**, at no engineering
   cost — and SPEC.md 47's rule is that refusal is normal. A refusal that named
   *what to close and how much it would give back* would serve that user better
   than 800 bytes of compactor, and nothing in the tree does it today.

**So the order to take this in is not the order the question implies.**

| take | why |
|---|---|
| **§2.1.1 item 1 — DECIDED, being fixed** | the owner's call: *"CWORD should not be claiming at the bottom, as a program."* 18,565 bytes out of the middle of the arena for every C package, and `cc_ovbind` is already its relocation proc. ~10 bytes of `crt0.asm`, **zero kernel bytes** |
| **ETHER's socket pool** | 14KB, top-claimed, neither a CS nor DMA, merely **undeclared** — it would move under today's engine for **zero kernel bytes**. In §2.0's scenario that is 14KB of the wall |
| **§2.1.1's other two** | a declaration in SHEET (99KB), one SDK function in `os88.h` (which unlocks Browser 109KB, LOOM 141KB, C64 64KB). Also no kernel byte |
| **piece 0** | free, and an incomplete SPEC.md 66.6 is worse than none: the next person fixes five of nine words and ships a machine that draws its menu titles out of the wrong segment |
| **piece B** | ~215–240 bytes for up to 27KB of claimed ceiling on kern_big (`MOD_MAX` = 4, rounded up to whole KB). **The only piece safe without E**, with `FDLG.DRV` excluded or six thunks changed |
| **piece E** | before **A**, **C** and **F**. An ascending pack is safe only when the whole top-down population moves, and §4.6 guarantees it will not (§5) |
| then reconsider | D0 (~40, already built), D (~175–242, one IF=0 window and **more than one IVT vector**), F (~90, asks package authors for something — and §12 question 11 now says what: the point every worker already parks at) |

**§10 is now the record of what happened to that order**, and three rows of it
moved. **E, piece 0, §2.1.1's three items, ETHER, A, F and D are BUILT** (§10.2
to §10.12), which is everything except the modules. **D0 IS DISCARDED** by the owner's decision: it was their own proposal
for reaching the driver furniture *at all*, and A, C and F reach it by moving
things instead - *"you found other ways to make them movable, so discard D0"*.
**And B is re-framed by the same decision**: *"as long as the modules can MOVE,
I don't care if they purge"*, so what the module row wants is §66.6's treatment
and not §66.10's — a different piece with a different predicate, and the only
population left pinned.

**MEASURED, AND THE ANSWER IS TO LEAVE THEM.** The question is not what a
module costs to move, it is whether a module is a WALL - and on a running
machine it is not. Two sequences, read off `tools/heapmap.py` with the
direction column:

    drivers up, panel CLOSED     9K DRV | 8K DRV | 8.0K HOLE (top)
                                 descending 499.5K
    panel REOPENED               9K DRV | 8K DRV | 8K MOD    <- the CEILING
                                 descending 491.5K
    hard disk UNMOUNTED, open    9K DRV | 8.0K HOLE | 8K MOD
                                 descending 499.5K
    panel CLOSED                 9K DRV | 16.0K HOLE (top)
                                 descending 507.5K

**A module lands at the ceiling and stays there**, even when it is dropped and
re-taken: `mem_claim_1`'s `.hi` arm refills the ceiling hole it left, so
reopening the panel put `CTRL.DRV` back at `9E000..A0000` rather than under the
two driver images. And the 8KB hole that opens *below* it when a driver is
unmounted **is closed by the descending pass** - the claims under it pack up to
its base. The difference between "panel open" and "panel closed" is 507.5 −
499.5 = **8.0K, which is exactly the module itself**, not a stranded run. That
is the price of the feature being open, and it is the same price a resident
implementation would charge for ever.

**What made that true is D and F**, and it would not have been before them:
everything below a module in the ceiling stack is now movable, so a hole under
one is closable. A module is only a wall when something PINNED sits above it,
and after D that is one case - `SOUND.DRV` while it is attached, which the IVT
scan refuses. A module loaded under an attached sound driver strands the run
between them until one of the two goes away.

**Span, for the record**, since "scoped to the feature" is what decides it:
`CTRL.DRV` is loaded by `cp_open_x` and dropped by the panel's close, and the
panel is an ordinary non-modal window (`cw_app_launch`, `KIND_CTRL`) - so the
user CAN leave it open and go on working. The other five are per-operation, and
the two spans that outlive a single call are already named in §12 question 3.

**What B' would take if it is ever wanted, costed against D's actual shape.** The
fix-up is the same near-nothing: `mod_fp[]`'s segment halves are one more
`mem_rr_tab` row. The predicate is where it differs and where the cost is —
**29 thunk sites** far-call a module image against the driver path's seven, and
they cannot reuse `drv_enter` because a module runs with `DS = KERNEL_SEG` (it
is kernel code in another segment) where a driver runs with DS = its own, so
the segment has to be read out of `[XFP+2]` at each site. Bracketing all 29 is
**~200 bytes**.

**There may be an argument that costs none of it, and it is checkable rather
than believed.** A compaction can only run while a frame is inside a module if
something claims from inside one, and: no module body calls `mem_claim` (grep:
none of the six module sources has a claim site at all), a package's worker may
not claim (§20.6 rule 7), and no driver service task's cone reaches a claim door
(`tests/unit/t_drvclaim.py` asserts exactly that, today). If those three hold
then the UI task cannot be inside a module and inside `mem_claim` at once, and
the pin is unnecessary. **That is a proof obligation, not an observation** —
each of the three wants its own gate in `t_drvclaim`'s shape before a byte
rests on it, and the failure if one is wrong is the silent kind. It is the
cheapest remaining piece by a wide margin and the one most worth doing
carefully.

**But the prize is measured at zero**, which is why the recommendation is to
leave it: the numbers above say a module costs its own size while its feature
is open and strands nothing, so ~200 bytes of resident kernel would buy back
8KB that is not lost in the first place. The one case worth remembering is a
module under an attached `SOUND.DRV`, and the cheaper fix for that is the
sound driver's own quiesce (§3.2, already half-built as `sbl_halt`/`sbl_go_on`)
rather than making every module movable.
**C is built** (§10.9) — it needed a filler package before it could be gated
and §4.2's global counter replaced with a segment stack, both of which that
section records. **Six of §12's open questions are now answered** — 1b, 3, 4, 6,
10 and 11, all of them by reading the tree rather than by building anything, and
two of them correcting figures this document had been quoting (§4.6's population
and the `wm_pkgcall` bracket's cost). **B is not built and its prize is
smaller than §3.3 says**: the Control Panel's thunks already call `mod_need`
before every far call, so shedding `CTRL.DRV` is survivable — but a module is
already freed when its feature closes, so what purgeable adds is only shedding
it *while the feature is open*, and the three held spans §3.3 lists are the
spans where that is unsafe. The one that is not a held span is the Control
Panel, whose re-read is 0 to ~1.6 s depending on where the user is standing.


## 10. What has been built, and what it was verified against

**Built (commit `d02276e`): the direction invariant, and nothing else.**
`MC_HI` in the claim record, stamped at publish from `[mem_dir]`; `mem_is_hi`
reading it in ten bytes; `mem_cp_mine` asking whether a claim is this pass's
business, called from both `mem_cp_plan` and `mem_cp_run` immediately after
`mem_can_move`. **+89 bytes** — `.cold` +56, `.bss` +1, `.lowbss` +32 — with
`KERN_BUDGET` unmoved at 18,944 spare and no rung crossed.

It is a **no-op today** and that is the point: every one of the seven top-down
claims is pinned, so nothing reaches the new test with a different answer. What
it establishes is the property §5 needs — a claim goes back through the door it
came in by — before anything can be unpinned against it.

**Verified on a real machine**, not just on the host tier:

| gate | result | what it covers |
|---|---|---|
| `make test-fast` | **50/50** | the host invariants, the API table, every shipped image |
| `make test-full` | **61 passed, 0 failed, 2 skipped** | the pre-merge gate — the knob kernels, **kern_small**, `kernresident`, `small128`, a boot on both 1bpp adapters |
| `heapcheck` | **ok, 31.6s** | SPEC.md 66.8's own gate: a comb of claims, every other one freed, then a claim only compaction can satisfy — and **the contents of every survivor** checked against a per-block pattern |
| `heapmap` | **ok** | reads `mem_tab` out of a running guest at the new 11-byte stride |
| `paintmove`, `trackmove`, `rdmove`, `hdmove` | **ok** | live relocations through `pt_reloc`, `trk_reloc`, `rd_reloc` and the donated HDD listing |
| `fatwpin`, `msegnomem`, `mseglazy` | **ok** | the FAT window's pin, and the multiseg claim paths |
| `editmove` | **skipped** | wants `build/zmove360.img`; the Frotz stories are never committed |

Two things that cost time and are worth writing down. `mseglazy` fails with
*"os88map: no build/mseg.bin - build it first"* unless `make mseg` has run —
it is an on-demand artefact a plain `make` does not build, and the row does not
declare it. And `MC_SIZE` 10 → 11 broke **thirteen** harness files, every one
caught by `t_mirror`, including `tools/os88geom.py` itself — the single mirror
the others are supposed to import from rather than retype.

### 10.2 …and then the pass itself (§5, piece E)

**Built: the descending pass, parameterised** — SPEC.md 66.4.1 is the contract
and §5 here the design. `mem_cp_plan` and `mem_cp_run` are the same two bodies
they were; the eight decisions the directions disagree about are eight
routines both of them call (`mem_cp_fill0`, `_step`, `_near`, `_far`, `_adv`,
`_dest`, `_gap`, `_tail`), `mem_cp_next` gained a descending arm, `mem_bcopy` a
backward one, and `mem_compact` turned its escalation ladder into a loop.
**+344 bytes** cumulative — `.cold` +311, `.bss` +1, `.lowbss` +32 — with
`KERN_BUDGET` still at 18,944 spare and no rung crossed. Against §7's estimate
of ~250 for E on its own that is ~255 for the pass over the invariant's 89, so
the estimate held.

**Two things this file said and the code disagreed with**, both recorded
because the argument was made confidently in each case:

1. *"Derive the direction, store nothing."* Wrong on both counts. A predicate
   over the claim's own shape is 47 bytes against `MC_HI`'s 10, and it is right
   for four of the seven top-down sites and wrong for three. The byte is
   cheaper **and** exact.
2. *"E can ship independently of C."* Wrong. `mem_cp_run`'s fill point starts
   at the heap floor, so a population that is only *partly* movable splits the
   free run rather than merging it — the descending pass is a **precondition**
   for unpinning anything claimed through the top-down door, not an optional
   companion to it.

**Verified by amputation, which is the only A/B that says anything here.**
`HEAPCOMPACT=0` is too blunt: with no compactor at all everything past check 6
goes red, so that arm cannot separate *this* pass from *any* pass. So
`tests/heapfrag` gained **check 13**, which brings its own mover — two claims
through `OSAPI_MEM_CLAIM_HI`, the lower declared movable and filled, the upper
freed, then an ask for one KB more than the largest run — and the kernel was
then rebuilt with `mem_compact`'s `.flip` arm patched to `jmp .undo`:

| arm | result |
|---|---|
| shipped kernel | checks 1–13 **all pass** |
| `HEAPCOMPACT=0` | 7, 10 and 13 fail, the rest pass — as declared |
| `.flip` amputated | **1–12 pass, only 13 red** |

The third row is the assertion. It is also what turned up the one real defect
in the first build of this pass: the escalation was a **ladder**, and it parked
the workers only for the *ascending* plan, so a machine whose only blocked
mover was a top-down claim owned by a package with a worker reached the
descending pass with that worker still running and `mem_can_move` refused the
one block the pass existed for. As a loop it costs **2 bytes** and cannot
happen. `heapcheck` runs in 31.8s against a declared 40.

### 10.3 Piece 0 — the documentation, and a build bug under it

**Built: piece 0, entirely.** Zero kernel bytes, five bytes off every package
that includes `apps/os88type.inc`.

- **SPEC.md 66.6's holder list was four entries and is eleven.** `[menu_seg]`,
  `[menu_dseg]`, `[fdlg_rqsp]`, `[drv_dlg_seg]`, `[ld_base]`/`[ld_fp+2]` and
  `diskw.inc`'s three banked caller segments were all missing, and each fails
  in a way nobody would trace back to a compaction. The section now carries
  the table from §3.5, plus the two things that make the list finite (a
  package is `org 0`, so only *segment* words are ever wrong) and the one
  holder the kernel cannot reach.
- **SPEC.md 20.9 says `SSI_SEG` is a sample, not a handle.** Read it, use it,
  take it again.
- **SPEC.md 66.9 reasons 5 and 6 were both stale**, independently of anything
  here. Reason 5 (the donated HDD listing) was BUILT as `dsk_dseg_reloc`; the
  claim is movable today. Reason 6 named `MEM_K_FATW` as "the only honest
  declarable left" and that tag no longer exists — §18.8.4 made the window a
  cache and deleted the proc with the tag. Both entries are kept and rewritten
  rather than deleted, because the first generalises and the second is what a
  reader who finds the old name quoted elsewhere needs.
- **SPEC.md 38.0.1's "16KB claim" is 4KB.** 16 is `MOD_MAX_KB`, the cap
  `mod_need` refuses above; the claim is the image rounded up, and
  `FDLG.DRV`'s image is 3,243 bytes.
- **docs/HEAP-CLAIMS.md** gained the two `MC_DMA` rows it omitted, and they are
  the evidence for piece A: `MEM_P_DIRW` carries the tag as a *placement*
  constraint for one `int 13h`, and the typeface cache carries it for
  **512-byte alignment alone** — it asks for +1KB and rounds the segment up by
  hand. No chip is armed on either.
- **`[ty_selfseg]` is deleted.** Written by `ty_init`, read by nothing. `DS` is
  `CS` for a package and the kernel reloads it from `W_SEG` on every dispatch,
  so it could never have said anything `ds` did not — and once a region can
  move it would have been a stale word to fix for no reader.

**And the measurement of that last one found a build bug worth more than the
piece.** The five-byte deletion A/B'd at **exactly zero**, twice, because
`$(BUILD)/word.bin`'s rule does not list `apps/os88type.inc` — so `make` said
"up to date" and never reassembled. A scan of every `.bin` rule against its
real `%include` closure found **twelve** rules in the same state, of which
`apps/os88ui.inc` missing from **nine shipped packages** (chart, fractal,
frotz, hello, mines, tank, taskmgr, loom, npbench) is the one that matters:
edit the UI library and none of them rebuilds. All twelve are fixed, and
`tests/unit/t_pkgdeps.py` is now a fast-tier row so it cannot come back — 91
package rules, 0.8s, and it parses the Makefile as *text* because `make -p`,
`-n` and `-q` all evaluate the `$(shell ...)` beside `$(VIDSTAMP)` that
deletes `build/kernel.bin`. With the fix in, the deletion measures **5 bytes**
against the 4 this document estimated.

### 10.4 §2.1.1 item 1 — the overlay takes the top-down door

**Built, in `apps/cc/crt0.asm` and `apps/word/word.asm`. Zero kernel bytes,
+16 bytes of every C package.** This document's own ranking put it first of
everything because `CWORD.OVL` is **18,565 bytes** — bigger than every kernel
module in the tree put together — claimed through the LOW door and pinned for
the program's whole life. It was SPEC.md 50.3.2.1's defect one layer out from
the two driver images that section fixed, and the fix is a slot number.

**The C half took the declaration too, and the reason it is safe is worth
recording**, because §66.6's general refusal is *"every saved CS on every
stack"* and an overlay is code. An overlay has exactly one way to put its own
CS on a stack: the `call far` into `cc_ovthunk`. **That thunk discards it** —
`pop dx ... discarded rather than stashed` — and rebuilds the `retf` from
`[cc_ovseg]` *after* the call returns, at every level of the nest. So a move
that happened inside a resident routine is invisible to the return path, and
`cc_ovbind` (already written, for a different reason) fixes the `cc_ovm_*`
vectors. `cc_ovreloc` is **four bytes**: one store, falling through into
`cc_ovbind`. The property was written because there is only one module; it is
what makes the declaration free.

**Word takes the placement and REFUSES the declaration**, and the difference
between the two packages is exactly the shim convention. `wd_s_*` is
`call`/`retf`, so the module's CS is on the stack for the whole of every
shimmed routine and a move under one returns into memory that is no longer
there. Changing that is a shim redesign, not a declaration. (`WORD.OVL` is an
18-byte ping stub today, so Word's half is prophylactic — but `WD_OVKB` is
claimed whole whatever the module holds.)

**`tests/ovlhigh.py` is the gate** (soak, `marty`+`cc`, 14.5s of a declared
20). It boots CWORD, presses **F5** — `CWA_GOTO` → `ovl_dlg_open`, an overlay
function, so `cc_ovneed` claims, reads and binds — and then reads `mem_tab`
from outside the guest for four facts: `MC_HI` is 1, the claim is above every
bottom-up claim, it is packed against the ceiling with no hole above it, and
`MC_RLOC` is not 0. The last is the one SPEC.md 66.5.6.2 exists because nobody
made: `mem_movable`'s fence is *yours, or not at all*, so a refused
declaration is a CF the caller discards and looks identical from inside the
package. It is a key rather than a menu pick because CWORD draws its own menu
bar inside its window, so the kernel's bar carries only Apple and the app's
name.

**Verified to fail in both halves**, separately:

| arm | result |
|---|---|
| shipped | 6 checks pass; the overlay is at `8c40`, 19KB, `MOVABLE HI`, abutting the region at `9100` |
| `_HI` → `MEM_CLAIM` | 3 red: `MC_HI` 0, the claim at `2fc0` **inside the arena**, and a 371KB hole above it |
| the `OSAPI_MEM_MOVABLE` call removed | 1 red: `MC_RLOC` 0 |

The middle row is the before-picture in one line, and it also confirms this
document's §2.1 finding on a running machine: **a package REGION is itself a
top-down claim** (`9100`, 60KB, HI), so "highest fit" puts the overlay
directly below the region rather than above everything — abutting the CS-based
group at the ceiling, which is where it belongs.

### 10.5 §2.1.1 item 2 — SHEET declares five of its six

**Built. +82 bytes of SHEET, zero kernel bytes, ~67KB of arena unblocked.**
SHEET was the largest undeclared holder in the tree: six unconditional claims
at its entry proc, ~99KB, every one pinned for the session — which made SPEC.md
66.5.10.2's closing line, *"the arena below the top now has no barrier in it at
all — every claim there is movable or purgeable"*, false the moment a sheet
opened.

Two facts made it cheap. **SHEET hires no worker**, so `mem_can_move` passes
its claims on `I_TASK = 0xFF` alone and no park is involved at all. And every
reference to the six segments is a fresh `mov es, [sh_*seg]` — everything else
in the package is an *offset* into one of them.

**`sh_reloc` is a TABLE and not a ladder of compares**, for one reason worth
keeping: it patches **every** word that names the old base rather than the
first. `os88chart.inc`'s `ch_srcseg`, `ch_stgseg` and `ch_srcseg2` are borrowed
second copies of segments this package also holds directly, and SPEC.md 66.1 is
the record of a word-poke design that failed on exactly that — *"the pair that
killed the word-poke design"*.

**`sh_stgseg` is deliberately not declared**, and the gate asserts that too,
because *"we meant to leave that one"* and *"we forgot that one"* are the same
picture. It is the `ES:BX` of **all seven** of the package's
`OSAPI_FILE_READ`/`WRITE` calls (SPEC.md 66.9 reason 4), and a file call
claims, so a compaction inside one would move the buffer out from under a
transfer the kernel already has the address of. SPEC.md 66.5.7.1's pin/unpin
pair is what it would take.

`tests/sheetmove.py` is `paintmove`'s recipe with a bigger claimant: heapfrag
owns the floor, Sheet lands above it, `12345` is typed into A1, heapfrag closes
and is re-opened, and its big claim forces the pass. On a 640x200 CGA three of
the five move (`sh_cellseg` 6060→58e0, `sh_txtseg`, `sh_bordseg`), the contents
hash identically, every word follows, `sh_stgseg` does not budge and the
repaint is byte-identical. **Verified to fail**: drop `sh_cellseg` from
`sh_reloc`'s table and check 3 reads `STALE: sh_cellseg` while check 4's
repaint differs over 24 rows of the grid — a plausible wrong sheet rather than
a crash, which is the whole reason a contents hash is not enough.

### 10.6 …and a FALSE GREEN found while writing it

`sheetmove` failed its repaint check with 129 of 135 rows differing, and the
cause was in the two rows it was copied from. `m.vram()` answers a row per
scanline and **a BYTE per pixel** — a colour index, not packed bits — and
`paintmove` and `editmove` both slice it as `rows[y][cx0 // 8 : cx1 // 8]`.
Against a 640-wide frame that compares the leftmost (cx1−cx0)/8 **pixels** of
each row instead of the window: a strip of desktop, identical in both captures
whatever the window does. **Both rows' final assertion — the one whose
docstring says it is the only thing that tests `pt_rowseg` — was green by
construction.** Fixed in all three, and both now compare the real rectangle and
still pass.

The second half of that fix is the **mouse pointer**: it is drawn into the
framebuffer, the two captures are taken after different gestures, and with the
rectangle finally correct the difference was six pixels wide — `x 115..120,
y 63..64`, the tail of the arrow a window drag had left one row inside the
content. All three rows park it at a fixed spot before each capture now.

### 10.7 §2.1.1 item 3 — the C SDK can declare

**Built.** `os88_mem_movable(seg, on)` in `apps/cc/os88.h`, `_os88_mem_movable`
in `os88thunk.asm`, and the `cc_onmove` trampoline in `crt0.asm`, all three
behind `%define CC_HAS_ONMOVE` so a package that does not declare pays nothing.
`os88_mem_claim` was the whole of the C SDK's heap surface, so every claim a C
package made was pinned **by construction** and no author could change it —
C64's 64KB of RAM, RunCPM's 64KB Z80 space, Weave's bundle and canvas, Loom's
29/50/62KB project buffers.

**`cc_onmove` is not a window callback and that is the whole hazard.** It is
dispatched from inside `mem_reloc_call`, in the middle of the walk, on whatever
task asked for the memory — so SPEC.md 66.3 rule 3 binds the *C* on the other
side of it: no claim, no free, no yield, no drawing, no file call. It also
preserves **AX**, which no window callback has to, because a relocation proc is
called between two instructions of somebody else's routine rather than from a
dispatch loop.

**`tests/chello` is the first adopter, deliberately rather than an
application.** The round trip is longer than any other callback's — C, thunk,
kernel, `cc_onmove`, C — and until something built out of it reports a move
that actually happened, the trampoline's argument order is inference from
emitted assembly. That is the same sentence `chello.c`'s own header uses about
the crosshair, and it is why the claim's live base and where it came from are
both drawn in the window.

`tests/cmemmove.py` is the gate (91.5s): heapfrag owns the floor, CHELLO lands
above it, heapfrag closes and re-runs, and the row reads `MC_RLOC` out of the
kernel's table and `[ch_seg]`/`[ch_moves]`/`[ch_was]` out of CHELLO's statics.
**Verified to fail by swapping the two pushes in `cc_onmove`**: `[ch_seg]` goes
stale at the old base, the move count stays 0, and assertions 2 and 3 both go
red. That break also shows the handler shape is self-protecting — the
`if (my_seg == was)` guard means a swapped pair does *nothing* rather than
assigning garbage, which is why the SDK documents the guard rather than
`my_seg = now`.

**No shipped C package declares yet**, and that is now an audit rather than an
impossibility. `docs/HEAP-CLAIMS.md`'s row says so.

### 10.8 Piece A — `MC_DMA` stops being a pin

**Built. +61 bytes of `.cold`, and it CROSSED A RUNG** — `.cold` 38,370 →
38,431, which took the footprint 18,944 → 18,432 spare (36 steps). It still
fits with room, and the crossing is reported rather than designed around
(CLAUDE.md's rung rule): the byte cost is 61 and the 512 is what the previous
work had already spent into that rung.

`mem_can_move`'s `MC_DMA` refusal is gone; `mem_cp_dest` honours the constraint
instead, in both directions, and `mem_cp_adv` and both walks' *"would it
move"* test now read the **destination** rather than the fill point, because a
bump puts the two apart. SPEC.md 66.4.2 is the contract and §3.1 above the
evidence: four `MC_DMA` claims in the tree, one chip, and what keeps the Sound
Blaster's ring still is what keeps everything still — nobody declared it.

**`tests/heapfrag` check 14 is the gate and the head is the WHOLE block**,
which is what makes it sharp. A block of P paragraphs whose whole length must
end inside one 64KB page can only sit at a base whose page offset is ≤
0x1000 − P, and every heap base is a multiple of 64 paragraphs (guard 6b) — so
of the 64 offsets a destination can have, only a handful are legal. The check
claims 60KB with a 60KB head, declares it movable, fills it, frees the block
underneath and asks for one KB more than the largest single run.

| arm | where it landed |
|---|---|
| shipped | page offset **0** paragraphs — legal |
| the bump amputated (`mem_cp_dest`'s `MC_DMA` read forced to 0) | page offset **3,520** paragraphs, with 3,840 of head to place: **straddling by 204KB** |
| `HEAPCOMPACT=0` | refused, as declared |

The middle row is what this piece is about. A straddle does not fault: the 8237
wraps to the start of its page and moves **the wrong memory, silently**, which
is why the check reads an address and not a flag.

### 10.9 Piece C — a package's REGION moves

**Built.** `.text` +164, `.bss` +18, `.cold` +98, and `KERN_BUDGET` unmoved at
18,432 spare — but the **image rung has 15 bytes left** (497/512 accrued), so
the next `.text` or `.bss` byte crosses it. §66.6 has said since it was written
that a region can never move *"because its base IS its CS"*; that is the door,
and it is open.

**It was written, reverted for want of a gate, and re-applied once the gate
existed.** The instrument is what was missing, not the code — see the last
section below.

#### The audit §4.2 asserted, done

**There are exactly two `call far` sites in the kernel that reach a package**:
`wm_pkgcall` and the loader's entry call. Every other far call in the tree
reaches a **module** (the `CPFP`/`FMFP`/`FDFP`/`CLFP` thunks) or a **driver**
(`drv_fptr`, `DRVR_DISP`, `drv_blkfp`); `drv_pkg_call_x` runs the other way.
`mem_reloc_call` is the third and must **not** be counted — count it and the
moment `mem_cp_run` notifies the first movable data claim the depth goes
non-zero and the compactor pins every region against itself.

#### §4.2's GLOBAL COUNTER IS WRONG, and this is the finding

§4.2 proposes one byte and disposes of the claimant's own region with *"a
package reaches `mem_claim` only from inside a callback or its entry proc, so
its depth is already non-zero."* That sentence is true and it is fatal to the
design it is defending: **the depth is non-zero at the one moment a
package-driven compaction runs**, so a global counter pins *every* region
whenever *any* package is claiming — and a package claiming is what a
compaction almost always is. The feature would fire on kernel-initiated claims
alone.

What shipped records **which** segment is at each level: `wm_pkgs[WM_PKGD_MAX]`
beside the depth, pushed at `wm_pkgcall` and scanned by `mem_frameless`. Past
`WM_PKGD_MAX` the answer is *pin everything* rather than a guess. And the
loader's entry call then needs **no bracket at all**: the only region a frame
of it can refer to is the one being loaded, and `[ld_base]` names that one.

#### Three defects the build turned up, all silent

1. **`dec byte [wm_pkgd]` after the far call runs with DS = THE PACKAGE'S.**
   `wm_pkgcall` restores DS at `.out`, below the decrement, so an unprefixed
   store lands one byte into the package's own image. The symptom was every
   window title coming back as line noise. `cs:` is the answer in `wm.inc` (it
   is `.text`); `ES` is not, because a callback may clobber it.
2. **`[ld_base]` was cleared on the abort path only** — so on a successful
   launch the predicate pinned the most recently launched region for the rest
   of the session.
3. **`mem_reloc_call` had no arm for a region.** Its instance-slot case says
   *"a region is pinned and never arrives here"*, and with one declared it
   does; the holder is the **package itself, at its new base**, through
   `PKG_DISP` and not through the kernel's shim. Without the arm it far-called
   a *kernel* near proc at a *package's* offset.

And one in the package, the ordinary kind: SHEET's declaration sat between
`mov [sh_chartseg], dx` and a `mov es, dx` eighty lines later, so `mov dx, cs`
sent the 118-byte BMP header into offset 0 of SHEET's own image, over the
`.o88` header. The window opened with an empty title and `ld_status` said
success.

#### The instrument, which is the part that took the time

`tests/heapfrag` cannot force this. Its comb is sized `L/8` from the largest
run *it* sees and its assertions are about the arena it expects to own, so with
another package's claims interleaved its own checks 8 and 11 fail — and a run
in which the forcing claim was refused looks exactly like one in which it was
granted and did nothing.

**`tests/filler` is an instrument with no opinions**: it takes the arena down
and, on a keypress, asks. Three things about it are the experiment rather than
the code:

- **the ask is a DESCENDING LADDER, not `avail + 1`.** `avail + 1` is more than
  any single run, so it forces *a* compaction — and the **ascending** pass
  usually funds it, after which `mem_cp_worth` never asks the descending one.
  Nor can the ask be "everything free": `mem_cp_worth` refuses to run a pass
  that will not reach the number, so an unreachable ask moves nothing at all.
  The ladder starts just under the total and steps down, and the **first grant
  is the largest claim the machine can fund** — reached by whichever passes it
  took.
- **the fill ALTERNATES with the ask.** The ascending pass a forcing ask
  triggers packs the other packages' claims down and leaves a fresh hole where
  they were, bigger than anything a ceiling merge could produce. An arena
  filled once is not filled after the first ask; alternating converges until
  the only room left is the ceiling's.
- **the filler opens LAST.** Its region is claimed top-down, so it lands at the
  top of the hole the closing package left and seals the rest of that hole
  above the region under test with a pinned barrier — which is the shape the
  descending pass exists for.

`tests/regmove.py` is the row: PAINT takes the ceiling, SHEET goes under it and
is the package that has to move, PAINT closes, FILLER opens and fills, and five
rounds of fill-then-ask follow.

| arm | result |
|---|---|
| shipped | region **8b00 → 9300**; every kernel word followed; the code hashed identically; it still draws |
| `mem_region_reloc` amputated to `ret` | the Sheet window is no longer findable by name and its title reads `\x1e\x13Å\x9a\xa0` — `W_SEG` still names freed memory |

The second row is the whole reason `mem_region_reloc` could not ship
unexercised: a stale `W_SEG` does not fault, it far-calls a dispatcher in
memory that is now somebody else's.

**Assertion 4 hashes the first 2KB and not the region**, which is worth
recording: a region is code *and* the package's live bss, and SHEET is running,
so hashing the whole of it compares a program with itself a few seconds later
and always differs.

**What is still pinned is a package that owns a WORKER** — `task_spawn` writes
the region's segment into the worker's frame before it runs. §4.7 is the way
past it and it is an ABI change.

---

### 10.10 Piece F — a worker-owning region moves, and the worker comes back

**BUILT. SPEC.md 66.6.2 is the contract.** `OSAPI_TASK_RESTARTABLE`
(`inst_restart_set`, slot `0x0518`) declares a near offset; `mem_frameless`
accepts a region whose worker has one **and is parked**; `mem_wk_restart` finds
the instance at the new base and `sch_wk_restart` rebuilds the frame.

**231 bytes, against ~90 estimated** — `.text` +177, `.bss` +24, `.cold` +30,
and it crossed the image rung. Where the estimate went wrong is the *shape*
rather than the arithmetic: §4.7 costed a frame rebuild and an accept arm, and
what it takes is **two predicates and two lookups**. The declaration alone is
not enough (see below), so `mem_frameless` grew a second question and a
`[mem_wpin]` arm; and the rebuild cannot be reached from where §4.7 imagined,
because `mem_region_reloc` has already rewritten every `I_SPTR` by the time the
move is done — so finding the instance needs a second lookup by the **new**
segment, which is `mem_wk_restart`, ~45 bytes that were not in the estimate.

**THE DESIGN CHANGED IN ONE PLACE AND IT IS THE IMPORTANT ONE.** §4.7's API
says *"my worker's stack holds nothing that matters"* and stops there. That is
a claim about the PACKAGE's stack and says nothing about the KERNEL's: a worker
pre-empted inside `gfx_lock` **holding** the lock, or inside a driver call,
would take that with it and the machine would never draw again. So the built
predicate is two questions — declared **and** `[sch_parked]` — and the park byte
is exactly the proof that the worker is standing at one of the two points where
it holds neither (SPEC.md 66.5.4). It costs nothing: that byte is already
maintained, and `[mem_wpin]` now makes `mem_compact` spend its one park request
on a region rather than only on a package's data claims, which is what gets a
declared-but-running worker parked in the first place.

**A defect the design walked into and out of: `[sch_parked]` must be CLEARED by
the rebuild.** Both park points clear it on the way out — `inst_park_hold`'s
`.done` and `gfx_lock`'s `inst_park_unlk` — and a rebuild deletes that way out.
Left set, `inst_seg_parked` answers *"parked"* for ever and the **next**
compaction moves the claims of a worker that is running: a silent corruption
introduced by the fix for a silent corruption. `[gfx_lock_want]` is left set
and is harmless (SPEC.md 7.3 makes it a hint the next contended acquire spends).

**The gate is two rows that differ by one keystroke.** `tests/regpin.py` opens
the same disk, builds the same arena and makes the same forcing ask **without**
pressing `R`; `tests/regwork.py` presses it. One says the region must not move,
the other that it must — which is a better A/B than any kernel patch, because
nothing but the declaration differs:

    regpin    3 pinme region STAYED       8280, pm_reloc called 0 times
    regwork   3 the region MOVED          8280 -> 92c0, pm_reloc called 1 time(s)
              4 the worker was RE-ENTERED entered 1 -> 2
              5 ...and it is RUNNING      loop 1942 -> 2040 over 4s

**Assertion 5 is the one that earns the row**, and the kernel-side A/B proves
it: with `mem_wk_restart`'s call nop'd out the region still moved and
`pm_reloc` still fired, and the worker's tick count **froze at 1717**. It did
not fault — it resumed at an offset in memory that was no longer its own and
wandered off, which is §10.1's *"would not fault; it would run the wrong
memory"* happening in front of the row that was written for it.

### 10.11 …and the packages that use it

**The kernel side of F delivers nothing until a package declares**, so the same
wave adopted it. `apps/os88api.inc` gains two macros - `OS88_REGION_MOVABLE`,
which carries its own `ret` relocation proc and jumps over it so there is no
second thing to place, and `OS88_WORKER_RESTARTABLE` - and the C SDK gains
`os88_task_restartable(int)` plus a region declaration in `crt0.asm` itself.

**`cc_regreloc` is `cc_ovbind` and that is not a convenience.** Its `.res` loop
writes the live `CS` into `cc_ovv_*`, the far vectors the overlay calls BACK
through, and inside a relocation proc CS is the region's new base - so the one
routine a C package already had is exactly the one a region move needs. Without
an overlay it is a `ret`.

**Adopted, and it is 165,765 bytes plus the C packages**: Word (56.8KB), Tank
Attack (30.9), Audio (30.2), ftpd (28.2), Browser (19.8), and CWORD and RUNCPM
through the SDK. **Not adopted: the six that declare `OSAPI_MEM_PARKSAFE`**
(ArtfulType, Fractal, Frotz, ModPlug, Note Pad, Tracker) - that declaration
lets the kernel stop their worker while it is blocked in `gfx_lock`, which is
*anywhere* in the loop, so the honest form for them is a window around
`OSAPI_TASK_ALIVE` rather than a blanket, and it is a per-package judgement
about what a lost half-frame or half-buffer costs.

**THE PLACEMENT WAS WRONG THE FIRST TIME AND THE ROW CAUGHT IT IN A MINUTE.**
Both declarations went beside the `OSAPI_TASK_SPAWN` - they read as a pair - and
a package that hires no worker never reaches that line. Audio hires only when
playback starts and ftpd only when the card is up, so on a machine with no NIC
neither ever did: the two packages that move most easily were declaring
**nothing**. `tests/regapp.py` reads `MC_RLOC` back out of `mem_tab` rather
than trusting the call, which is the only reason it was visible at all. The
region declaration is in each entry proc now and only the restart stays at the
spawn.

### 10.12 Piece D — a driver image moves, and it needs no declaration

**BUILT. SPEC.md 66.6.3 is the contract, and three of the four pieces §9
costed were not needed.**

**The fix-up is table ROWS, not a proc.** §3.4 counted 66 kernel words in 9
tables and priced a routine to walk them. `mem_region_reloc` already walks
tables, already runs unconditionally for every move, and already covers two of
the nine (`MC_OWN` and `W_SEG`). So D's whole fix-up is two rows in
`mem_rr_tab` and three scalars in `mem_rr_sc` - `drv_tab`'s `DRVR_SEG` at
`DRVR_SIZE` stride, the five contiguous class fast paths at stride 4, and
`ss_row`, `xm_row` and `drv_blkseg`, the three that are shaped like a `drv_tab`
row and deliberately outside it (§41.12.5). **~20 bytes for the half the
estimate put at 90.**

**NO `DRVV_QUIESCE`, NO `DRVV_REARM`, NO `DRVV_RELOC`, and no declaration of
any kind.** The plan assumed the driver would have to be asked. It does not,
because every word that names a driver image belongs to the kernel - and the
one thing the kernel cannot read out of its own tables is whether an interrupt
vector points into the image. `mem_ivt_names` **asks the 8086**: 256 vectors,
one compare each on the segment half, ~512 compares once per candidate in a
pass that is about to copy kilobytes.

> **CORRECTED IN PLACE at §10.13.** That scan was a REFUSAL - a driver with a
> vector into it stayed pinned - and it is a PATCH now, the same loop writing
> the new segment instead of setting CF. `mem_ivt_names` is deleted and the
> loop lives in `mem_region_reloc` beside every other table it fixes up
> (SPEC.md 66.6.3.1). Everything below about *why the kernel asks rather than
> the driver declaring* is unchanged and is the reason the patch is cheap.

That is not a shortcut, it is the safer answer. `SOUND.DRV` hooks **five**
vectors - `sbl_isr` on its IRQ, plus up to four candidates it installs a stub
in *while it is finding* that IRQ - so a driver-side declaration would have had
to be right about all five, once, for ever, in a driver nobody is editing. The
scan is right about all of them without being told, right about a driver
written before any of this, and cannot be forgotten by one written after. It is
§47's *grey a fact, never a guess* applied to the compactor, and it is what
turns "the driver opts in" into "the kernel checks".

**What DID cost what the estimate said** is the dispatch bracket:
`drv_enter`/`drv_leave` at the seven far-call sites, `pushf`/`cli` because
`drv_dispatch` is reached from `snd_tick` inside IRQ0, and neither half able to
trust `ES` because five of the seven restore it only after the call. Two of the
seven are `.text` and reach the pair - which lives in `.cold` with the other
five - through four-byte `retf` wrappers, §2.6.1 forbidding a far-called body
that ends in a near `ret`.

**The gate is the study's own opening scenario** (§2.0). `tests/drvmove.py`
mounts the hard disk, mounts the RAM disk under it, **unmounts the hard disk**,
and forces a pass:

    hdd 9c00   ramdisk 99c0
      1 image declared movable   MC_RLOC=8126, 9KB
          hard disk unmounted - the hole is above the RAM disk now
      2 the image MOVED          99c0 -> 9d80
      3 nothing still holds 99c0  OK (17 kernel words checked)
      4 the machine still draws   OK

Its third assertion reads every `drv_fseg*`, `drv_blkseg`, `drv_tab` row and
claim owner **by name**, because a stale one does not fault - it far-calls a
dispatcher in freed memory on the next volume access. The A/B, with the
`drv_tab` row taken out of `mem_rr_tab`, names `drv_tab[3]` exactly - **and
assertion 2 goes red with it**, because the row reads the segment *from*
`drv_tab` and a build that does not update it cannot even tell the image moved.
That is the bug's own shape, and it is why assertion 3 is by name and not a
summary.

**One thing this row does NOT assert and it is worth saying**: `drv_fseg2` was
0 after the unmount and the first draft asserted the RAM disk into it. That is
the machine's own state - the DISK class has one published pair and the hard
disk's detach cleared it - so the assertion would have been asserting a bug.
What a move can get wrong is a word left holding the OLD segment, and that is
what it reads.

### 10.13 …and SOUND.DRV, the last one

**BUILT. SPEC.md 66.6.3.1 and 66.6.4 are the contracts.** §10.12 shipped a
driver-image move that every driver in the tree could take **except the one the
study kept naming**, because `SOUND.DRV` is the only driver here that hooks an
interrupt vector - and `mem_ivt_names` refused on exactly that. Two claims were
left: the 6KB image, and the 8KB DMA ring sitting immediately underneath it.

**The refusal became a patch, and it is one loop.** A vector into a driver
image names it by SEGMENT and its offset does not move, so it is the same
fix-up every other table in `mem_region_reloc` already gets. The scan was
already written and already correct about all five of the vectors `SOUND.DRV`
hooks; turning it round cost `cmp`/`jne`/`mov` where there had been
`cmp`/`je`/`stc`, and **deleted** `mem_ivt_names` and its two exits.

**And then it corrupted a machine, which is the finding worth more than the
feature.** The turned-round loop kept the refusal's 256-entry sweep, on the
argument that asking the 8086 is right about a driver nobody has written yet.
**The IVT is not a table of pointers.** Its unused slots are scratch and third
parties use them as such: `os8088_xt_hdd`'s XT-IDE option ROM keeps two words
at int C1h and int C3h, one of them read `0x8000`, a package claim moved off
`0x8000`, the sweep rewrote the ROM's word to `0x6000` - and the hard disk then
probed as **"No hardware found"**. A machine with no C: drive, out of a heap
compaction, with nothing in the log. `tests/hdmove.py` caught it, which is the
argument for running the whole family after a `mem_can_move` change and not
only the row you wrote.

`mem_iv_patch` is cut to **sixteen** slots - int 08h..0Fh and int 70h..77h, the
hardware IRQ vectors - which is what a driver can legitimately own and is a
fact rather than a guess. It is SPEC.md 66.3 rule 5 one address space along:
**a sweep that PATCHES on a coincidence corrupts, where one that merely REFUSES
is only conservative**, and `mem_ivt_names` had the identical false positive
for a release without anyone noticing, because all it ever did was pin.

**What the turn round DID need is six bytes of `mem_cp_run`.** Between the
`rep movsw` and the rewrite, every vector still names the old copy - whose
bytes are intact but whose bss is about to be left behind, so a write an ISR
makes there is lost. `pushf` / `cmp MC_OWN, MEM_K_DRV` / `cli` closes the whole
window for every vector at once, where masking one line would have to know
which. It costs ~17 ms - one tick - charged once, and only when a driver image
actually moves.

**The ring needed a rule and three words.** §66.4.2 had already established
that `MC_DMA` is about where a claim may *land*; the ring is the one claim in
the tree where a **chip** also points into it, and the 8237 cannot be told
about a move mid-transfer. `[drv_wcnt]` answers it exactly and was already
exact - a stream lives only while its refill or drain task does (SPEC.md 34.5)
and the chip is armed only while a stream is open - so `mem_can_move` refuses
any `MC_DMA` claim while it is non-zero. **Parked is not enough**: a parked
refill task means the DSP is *playing*, not that it has stopped. The driver's
side is `sbl_ring_reloc`, which stores the new base and falls through into
`sbl_dma_derive` - the tail of `sbl_dma_map`, **factored rather than copied**.

**The gate is `tests/sndmove.py`, and building its ARENA was the hard half.**
Loaded at boot the image sits at the ceiling with the ring packed under it,
which is where it belongs - so a correct compaction moves neither. The row
therefore performs §2.0's own defect on purpose: drop the sound driver, mount
the RAM disk into the ceiling, bring sound back *underneath* it, drop the RAM
disk. Two attempts before that failed for reasons worth keeping:

  - **a spacer package walls it off.** Opening Paint the way `tests/regmove.py`
    does put a 33KB pinned region in the largest ceiling run - which is the one
    *directly under* the ring - so the pair had free space above and a wall
    below, and packing them up merged nothing. Measured, with a correct kernel:
    moved nothing, twice.
  - **the filler's own FILL seals the hole.** It is first fit ascending, so the
    hole the row had just opened above the image is the lowest run big enough:
    13KB of undeclared, therefore pinned, claim landing against the very block
    the ask needs moved. `tests/filler` grew an **'S' key** - ask without
    filling - for a row that has built its own arena.

Once the arena is right the pass is unambiguous:

    sound driver at 9e80 (at the CEILING, where it boots)
    re-mounted under the RAM disk, which is now gone: sound at 9900
      1 the ring is there        9700 8KB dma-head 512 para
      2 declared movable         MC_RLOC=11c3
      3 the chip is idle         drv_wcnt=0
      4 the ring MOVED           9700 -> 9c40
      4b ...and so did the IMAGE 9900 -> 9e40
      5 the 8237's words followed sbl_seg=9c40 dmaoff=c400 page=09
      5b the vectors followed    ['0f'] -> ['0f']

...merging 8.5KB and 21KB into one 29.5KB run.

**Two A/Bs, and both were needed to know the row was worth having.** With
`sbl_ring_reloc` storing the *old* base, 1 to 4b stay green and only 5 goes
red. With the IVT loop taken out, 1 to 5 stay green, **the desktop still
draws**, and only 5b goes red - which is the whole reason that check exists: a
stale vector is silent until the card next raises its IRQ, and by then the
compactor has re-let the bytes the CPU would jump into.

**One thing the row does not cover, said plainly**: the refusal arm - that the
ring does NOT move while a stream is playing - wants a playing stream, which is
Tracker's harness and not this one's. It also had to *earn* its vector: the
driver hooks the IRQ at the first stream open and not at attach, so a machine
that has never made a sound has none, and 5b would have been vacuous on it.
`SBTEST.O88` rides on the disk to open and close one stream first.

### 10.14 On-demand kernel MODULES are let off the hook, conditionally

The one population left pinned, and it is a **decision** rather than a
mechanism. `CTRL.DRV`, `FORMAT.DRV`, `CLONE.DRV`, `HIBER.DRV` and, on
kern_small, `FILECP.DRV` and `FDLG.DRV` are each loaded for a *temporary
action* - the user opens the Control Panel, formats a disk, clones one,
hibernates, copies a file, picks a file - completes it and leaves. So a module
can fragment the arena only for as long as the user is inside it, and
`mem_claim_1`'s `.hi` arm puts one back at the ceiling when it is dropped and
re-taken. Measured: a Control Panel open costs **8.0KB, exactly its own size**,
not a trapped run.

**The condition is written down so that it is not re-derived from scratch: a
PERSISTENT module would reopen this, and only for that module.** Nothing in
the mechanism refuses one - a module image is claimed with a kernel tag and
`mem_region_reloc` already covers `MODC_*` - so what would be needed is the
fix-up for whatever the module publishes, not a new door.

## 10.1 How the rest would be verified

Compaction's success looks exactly like its failure until something reads the
wrong memory an hour later (SPEC.md 66.8), and every item here widens that
window.

- **`tests/heapfrag` extended with a live PACKAGE, not just a live worker.** The
  existing gate fills every block with a per-block byte pattern and checks the
  contents after; the new arm must open a package, move its region, and then
  *use* it — a paint, a keystroke, a menu — because a region that moved correctly
  and one whose `W_SEG` was missed both report a successful claim.
- **A negative arm is as important**: open a package that owns a worker, run the
  hard pass, and assert its region did **not** move (§4.5). A pass that moved it
  would not fault; it would run the wrong memory. **BUILT — `tests/regpin.py`**,
  and it took three attempts because the first two were GREEN WITH THE PIN
  REMOVED FROM THE KERNEL:

  1. the subject was a region at the **ceiling**, where nothing can move it
     whatever any predicate says (`regmove.py`'s own header, one step along);
  2. the subject was the package making the forcing ask, and §10.9's finding
     met from the other side — a package reaches `mem_claim` only from inside
     its own callback, so `[wm_pkgd]` held its segment and `mem_frameless`
     refused its region for having a **frame** in it, correctly and for the
     wrong reason.

  So the asker and the subject are two packages: `tests/filler` asks and
  `tests/pinme` is asked about — a 215-byte instrument that declares at entry,
  hires a do-nothing worker at its first paint, and counts relocations, so
  *"it did not move"* is tellable from *"it moved and the proc was skipped"*.
  Two Paint instances are spacers whose closing makes the two holes. The A/B is
  exact: with `mem_frameless`'s `I_TASK` test taken out, PINME packs
  `8280 → 92c0` and `pm_reloc` fires once.
- **A dropped-module arm**: open the Control Panel, force a drop from another
  task, click in the panel, assert the module was re-read rather than re-entered.
- **The DMA arm must read an address.** A page-straddling destination does not
  fault; it plays the wrong memory. Assert `[sbl_page]`/`[sbl_dmaoff]` against the
  new base and that the head does not cross `0x1000` paragraphs.
- **`tools/heapmap.py`** already reads `MC_RLOC` out of `mem_tab` on a running
  machine, which is how SPEC.md 66.5.6.2's silently-refused declaration was
  caught. **The direction bit is BUILT into it** — it prints `top-down` /
  `bottom-up` beside every verdict, and `Map.compacted()` models the two passes
  separately instead of packing everything downwards, which it did before and
  which over-reported by the whole ceiling stack. The first thing it showed is
  a claim nothing could see: `seg 95C0 14.0K movable top-down` on a stock boot
  is ETHER's socket pool, movable since §5.1. §4.3's colliding-word report is
  still open.
- **§4.3's false-refusal rate is measurable**: count passes where `mem_frameless`
  refused and only one word matched. If that is not near 0.9% on a busy machine
  the arithmetic is wrong.

---

## 11. What this must NOT become

- **Not a background walk.** SPEC.md 66.7 is unchanged: compaction happens when
  something needs the room; tier 3 happens when tiers 1 and 2 did not find it.
- **Not something `mem_claim` reaches on an ordinary refusal.** `mem_cp_worth`
  is the gate and must stay in front.
- **Not scan-and-patch.** §4.5 is why: region bases share a 16-bit range with
  kernel return addresses and with a package's own near pointers, so a
  coincidental match is silent corruption of a return address. The counters are
  exact; the sweep is a guess, and SPEC.md 47's rule is to grey a fact.
- **Not "unmount and remount" as a compaction primitive.** `drv_unload_x` +
  `drv_load_row` already exist and cost zero new bytes, which is what makes it
  tempting. `drv_release` drops the class's volumes, so it costs a RAM disk its
  **contents**, a hard disk its mounted partitions, `ETHER.DRV` its DHCP lease
  and every open socket, and the sound its note. It is a fine *user* action on
  the Control Panel's Drivers page and a bad thing for the heap to do by itself.
- **Not a way to move a module out from under running code.** `mod_drop`'s banner
  is right, and §4 makes it answerable rather than optional.
- **Not a default.** Every image stays pinned until it declares otherwise, for
  the reason SPEC.md 66.2 gives.

---

## 12. Open questions

1. **SHAPE ANSWERED, SIZE NOT.** §2.0 is the case, from the owner operating the
   machine: a driver mounted mid-session becomes a wall that never moves and
   accumulates with every later mount. So the question is no longer *"does this
   shape exist"* — it is **how big the wall gets in a real session**. The only
   measurement in the tree is SPEC.md's **FRAG 4K out of a 544K span**, which is
   the phenomenon at a scale that costs nothing. `tools/heapmap.py` after §2.0's
   exact sequence would give the number, and it is a twenty-minute run rather
   than a design.
1a. **Can the 8237's current address and word count be reprogrammed independently
   of the base?** §3.2 says no — a write to port 0x02 loads base and current
   together, so a moved auto-init ring restarts while the DSP's block counter does
   not, and `[sbl_play]` goes permanently out of phase. That is asserted from the
   part's behaviour, not measured on this hardware, and it is what decides whether
   the ring can be moved at all or only restarted.
1b. **ANSWERED — does a descending pass want a direction BIT in the claim
   record?** YES, and it is `MC_HI` (§10.2). Both arms were built before the
   answer was taken: stored is **10 bytes** of `.text` (`mem_is_hi`) plus one
   byte a record, and DERIVING it from the tag and the owner is **47 bytes**
   and right for only four of the seven top-down call sites — a driver image
   and a module image are their tags, a region is `mem_is_region`, and the
   Sound Blaster's ring is the one driver-owned claim carrying `MC_DMA`, but
   ETHER's socket pool and the second images `RAMPAGE.DRV` and `HDDTOOL.DRV`
   hold are indistinguishable from a driver's ordinary bottom-up claim. So the
   heuristic is not merely a guess, it is a WRONG guess three times in seven,
   and it costs four and a half times what the fact costs.
2. **Does `mem_cp_plan`'s `.tail` run already count the ceiling holes correctly**
   when the pinned blocks are at the top (kernel/memory.inc:1607)? If it does not,
   `mem_avail` under-reports today and that is a defect independent of everything
   here.
3. **ANSWERED — is `mod_need` ever called on a path that does not then far-call
   the module? YES, twice, and once in the OTHER direction too**, which settles
   piece B's estimate at the high figure and names the three sites.

   Every module has a wrapper (`clo_need`, `dskw_fmt_load`, `fdlg_load`,
   `fcp_need`, `hbf_need`, `cpf_need`) that is `mov al, MOD_x` / `call
   mod_need` / `ret`, and **29 thunks** far-call an image through the seven FP
   blocks (`CPFP` 7, `FDFP` 7, `HBFP` 7, `FMFP` 4, `FCPFP` 3, `CLFP` 1,
   `CMZFP` 1). Twenty-six of them are `need` / `jc` / `call far` and are the
   shape an increment-in-`mod_need` scheme assumes. The three that are not:

   * **kernel/files.inc:5461 (`.swapneed`)** — a `mod_need` with **no far call
     at all**. It is a HELD SPAN made deliberate: the user has been asked to
     put the system disk in, so the image is made resident *while the disk is
     in the drive*, and the operation resumes at the next keystroke. An
     increment here has nothing to decrement at and leaks the pin for the rest
     of the session.
   * **kernel/ctrl.inc:5929 (`cp_open_x`)** — `cpf_need` then
     `cw_app_launch`, so the panel's window is created with its image already
     in. The far calls arrive later, from `cpf_cp_paint` and its siblings.
     Same leak, same reason.
   * **kernel/ctrl.inc:5839** — SPEC.md 13.8.3's two edges far-call the image
     with **no `mod_need` at all**, deliberately: a release or a drag can only
     follow a press the panel took, and taking it is what loaded the image. An
     increment-in-`mod_need` scheme *under*-counts here — a live far call into
     an image whose pin was never raised, which is the failure that matters.

   So the bracket cannot be an increment in `mod_need`: it is enter+leave per
   site plus the held-span arms, which is exactly the **215–240** figure §9's
   table calls the right one, and it now has three named sites behind it
   instead of two general arguments.
4. **ANSWERED — can any driver verb claim from a `TF_SERVICE` task?** No, and
   `tests/unit/t_drvclaim.py` is now the assertion this asked for rather than a
   comment. `SOUND.DRV` is the only driver in the tree that calls
   `OSAPI_DRV_TASK` at all, its two tasks are `sbl_refill_task` and
   `sbl_drain_task`, and neither call cone reaches a claim door — every claim
   in that driver is in `sbl_attach` (mount) or `sbl_grant_alloc` beneath
   `sbl_v_grant`, a stream verb that runs on its CALLER's task. The row walks
   `call`/`jmp` by name from each spawn's entry and fails on any
   `OSAPI_MEM_CLAIM*` or `OSAPI_MEM_REGROW` in the cone; broken on purpose
   three levels down (a claim in `sbl_dsp_wr`) it names both tasks and the
   whole path.

   **The worry in the question was the wrong one, and the right one is worse.**
   `[wm_pkgd]` is not a property of the running stack: it is a global count of
   kernel→package far calls in flight with ONE writer, the UI task, at
   `wm_pkgcall` and the loader's entry call. It answers *"is any callback
   live"*, which is true or false whoever asks. What a claim from a service
   task would really cost is the STACK — `mem_compact` far-calls a holder's
   relocation proc (SHEET's `sh_reloc`, the C SDK's `cc_ovbind`) on whatever
   stack it was entered on, and a 384-byte worker slice under
   docs/plans/completed/STACK-SLOTS-PLAN.md's interrupt floor is not `STK0`.
5. **Can the 8237's live address and count be read back reliably** on the hardware
   this project targets, so a moved ring resumes mid-buffer instead of clicking?
   Period 8237s latch differently across clones — a field question
   (docs/FIELD-MACHINES.md), not an emulator one.
6. **ANSWERED — what does the `wm_pkgcall` bracket actually cost? 39 bytes and
   ~202 clocks, against 10 and ~60 estimated.** The bytes are EXACT (the
   sequence assembled on its own): enter is 32 — `push ax`, `push bx`, the
   depth load, the `WM_PKGD_MAX` fence, `shl bx, 1`, the segment load and
   store, `inc`, `pop bx`, `pop ax` — and leave is 7, the `pushf`/`dec`/`popf`
   a callback's answer in the flags makes necessary. The clocks are DERIVED
   from the 8088 tables with PERFORMANCE.md's `max(clocks, 4.34 × bytes)` fetch
   floor applied over the whole run rather than per instruction (the BIU fills
   the queue during the four stack ops): enter 153, leave 49, **~42.3 µs on a
   4.77 MHz 8088**. The deepest arm, where the fence sends it past the table,
   is 116.

   **It is 3.9× the estimate because the DESIGN changed, not because the
   estimate was sloppy.** §10.9 records why a global depth cannot work — a
   package reaches `mem_claim` only from inside its own callback, so the depth
   is always non-zero exactly when a package-driven compaction runs — and what
   replaced it records WHICH SEGMENT is at each level. Eleven bytes buys a
   counter; thirty-nine buys a counter that can answer *"is it THIS region"*.

   **Affordable at the scale it runs at**: one per package window per repaint
   pass, so three package windows is ~127 µs against a title bar's 40 ms —
   0.3%. Quoted as derived, not measured, which is exactly the reading
   PERFORMANCE.md rule 5 says to distrust; a cycle-counted A/B would settle it,
   and nothing turns on it at 0.3%.
7. **Is `MEM_PG_LOW` the right rank for a module image?** ONDEMAND-PLAN §7.2 says
   so; a re-read of `CTRL.DRV` mid-session is several `int 13h` calls while the
   user is looking at the panel.
8. **ANSWERED — does any shipped package cache its own segment?** Essentially no.
   `[ty_selfseg]` (apps/os88type.inc:532) is the only instance and it has **no
   reader anywhere in the tree**. Only the two-instruction *write* is removable
   (4 bytes per including package) — the label and its reserved word must stay,
   or `ty_curseg` and everything laid out after it move. The C SDK's
   `cc_ovv_*`/`[cc_ovseg]` (apps/cc/crt0.asm:960, :1093) do, and `cc_ovbind`
   already fixes them; every C package hires a worker, so §4.6 pins them anyway.
9. **Would top-down or best-fit region placement (SPEC.md 50.3.1 cure 2) beat
   piece C outright?** It needs no ABI at all and it works on the worker-owning
   regions §4.6 can never reach. It is the comparison that decides whether C is
   worth building, and it has never been costed.
10. **ANSWERED — is `apps/cc/crt0.asm`'s worker loop re-enterable? YES, and it
    is three packages and not four.** `cc_worker` is `cld` / `mov [cc_wksp],
    sp` / `push word [cc_win]` / `call _os88_worker`, then a park loop. It
    carries nothing across the call: `[cc_wksp]` is re-banked on entry (it is
    an SP in `LOW_SEG`, so a region move does not touch it) and `[cc_win]` is a
    KERNEL window handle, equally unaffected. So one change to `crt0.asm`
    declares the restart point for every C package that has a worker.

    **C64 and LOOM are not among them**: they declare no worker at all, in
    their own words (apps/c64/c64.asm:44 *"NO CC_HAS_WORKER"*,
    apps/loom/loom.asm:112 *"NOT CC_HAS_WORKER"*), so they are on the movable
    side already and §4.7 is not what unlocks them. `CC_HAS_WORKER` is defined
    by exactly three: CWORD, RUNCPM and WEAVE.

    What the runtime cannot answer for is `os88_worker()` itself, and the three
    are not alike: CWORD's and RUNCPM's are `for (;;)` polls whose only inputs
    are statics (`cw_quit`, `rc_mode`), so a restart costs a poll; WEAVE's is a
    three-instruction trampoline into `WEAVE.WSM`, so its restartability is
    `wcv_run`'s and lives in a module.
11. **ANSWERED, and the answer reframes the question: ALL of them could, and
    that is not the interesting fact.** Every one of the eighteen assembly
    workers was read, and they are the same shape to a line:

        <entry>:
            [an optional one-time deadline anchor]
        .loop:
            mov bx, [x_win]
            call OSAPI_TASK_ALIVE
            ... every input a package STATIC ...
            jmp .loop

    Not one holds anything in a register or on its slice across an iteration:
    `[ark_due]`, `[at_cphase]`, `[ap_sopen]`, `[br_gen]`, `[cy_due]`,
    `[fr_pass]`, `[zx_seen]`, `[fd_xbytes]`, `[mc_due]`, `[np_hdirty]`,
    `[tk_due]`, `[trk_fs]`, `[wd_quit]` — all of them live in the region the
    move carries. So re-entering at `.loop` costs a frame, a poll or a step,
    and never a state.

    **What a restart costs is therefore not a property of the loop, it is a
    property of WHERE the worker is when the compactor wants it** — the call
    chain the kernel throws away is `x_render` half-drawn, `zx_step` with an
    opcode half-retired, `mpp_feed` mid-buffer. Which is to say the declaration
    piece F asks for cannot honestly be *"my loop restarts"*; it has to be
    *"my statics are consistent HERE"*, and there is exactly one such point per
    worker.

    **That point already has a name.** All eighteen reach `OSAPI_TASK_ALIVE`
    within the first 4 to 8 lines of the worker (the Task Manager at 17, having
    slept first), and `OSAPI_TASK_ALIVE` is where SPEC.md 66.5's park already
    stops a consenting worker. So piece F is not asking authors for something
    new: it is `OSAPI_MEM_PARKSAFE` one step further, and six packages already
    declare that (ArtfulType, Fractal, Frotz, ModPlug, Note Pad, Tracker).

    **It also weakens §4.6's third refusal.** The targeted two-word patch at the
    `ALIVE` park was refused partly because *"it reaches the sleepers rather
    than the drawing workers that matter"* — but the drawing workers
    (ArtfulType, Tamegram, Tracker, Audio, Fractal) all call `TASK_ALIVE` at
    their loop top too, so shape 1 is not a subset of the population, it is all
    of it. §4.6's first two reasons stand unchanged, and shapes 2 and 3
    (parked inside `gfx_lock`, pre-empted anywhere) are untouched by this.
