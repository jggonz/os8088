# Heap compaction — the plan, and the three things that decide its shape

**Status: the MECHANISM is built and is SPEC.md §65.** D1 was answered (a
package's data claims move; its region does not) and D3 was left to the
implementer. What landed, and what did not:

| | |
|---|---|
| §4.1 `MC_RLOC`, default PINNED | **built** — §66.2, record 8 → 10 bytes |
| §4.2 relocation is a dispatcher call | **built** — §66.2, `OSAPI_MEM_MOVABLE` at slot 0x03F8 |
| §4.4 plan-then-move inside `mem_claim` | **built** — §66.4, ahead of the shed |
| §4.5 the monotone downward slide | **built** — §66.4 |
| §4.3 the worker park at `OSAPI_TASK_ALIVE` | **built** — §66.5, with `make HEAPPARK=0` as its own A/B |
| §3 D3's adopters | **Paint** (§66.5.1), **Tracker** (§66.5.2), the **sound driver** (§66.5.5) and **four kernel claims** (§66.5.6). Only the FAT window is left, deliberately |
| §5 moving a program | **not yet** — §66.6 records what is behind that door |

**Verified on a cycle-accurate 4.77MHz 8088** (`os8088_5150_cga_gla`): the
gate passes 12 of 12 — running its whole suite with a live worker, so the
park is exercised rather than bypassed — and fails exactly checks 8 and 11
under either `make HEAPCOMPACT=0` or `make HEAPPARK=0`, which are separate
A/Bs on purpose. A five-step scripted session — desktop,
Disk window, folder, a package launched, a menu — is **0 differing bytes** of
framebuffer *and* of claim map between the two kernels, which is the proof
that "default pinned" means what it says.

The rest of this document is the reasoning that produced that shape, kept
because the two wrong answers in §1 and §2 are the obvious ones and cost a
round each.

---

It is written because the
obvious version of this feature — "slide the data claims down inside
`mem_claim`" — is about 200 bytes of code, would pass every test in the
tree, and **would not fix the failure that asked for it**. That is worth
establishing before any of it is written, not after.

The ask: a heap compaction pass that runs **on demand**, hung off a claim
that would otherwise be refused, because at that moment the user has just
done something and is expecting a delay. Data claims only in the first
pass, with the door left open to moving running programs.

Everything below is against `kernel/memory.inc` as it stands (SPEC.md §50)
and the field report in docs/FIELD-NOTES.md 2.

---

## 1. What compaction has to move, and it is not what the kernel owns

The heap has **two ends and three populations** (SPEC.md §50.2/§50.6.1):

| population | placed by | where it lands |
|---|---|---|
| package **regions**, driver images, on-demand kernel modules | `mem_claim_hi` | top down |
| **purgeable caches** (`MEM_P_WSAVE`, `MEM_P_DIRW`) | `mem_claim`, self-placed | top down, under the lowest region |
| **everything else — the data arena** | `mem_claim` | bottom up, first fit |

Fragmentation that refuses a large claim is fragmentation *of the data
arena*, so the data arena is the only thing worth compacting. Now: who is
actually in it?

```
kernel tags     MEM_K_SAVE   20 KB, transient (one menu)
                MEM_K_FATW    4.5 KB per mounted volume, long-lived
                MEM_K_ASC     ASSOC.DAT cache, one per volume
                MEM_K_CLIP    the clipboard, sized to its contents
                MEM_K_COPY    the file-manager copy buffer, one operation
                <instance>    VIEW_KB = 3 KB per open Disk window
packages        every OSAPI_MEM_CLAIM: Tracker's 116 KB module, Paint's
                canvas/undo/clipboard/LZW, Note Pad's note, ArtfulType's
                document, Frotz's story, ModPlug's module
drivers         SB's 20 KB staging pool, the ramdisk, HDDTOOL's buffers
```

**The kernel's own data claims are small and, being first fit from the
bottom, they settle at the bottom.** A 3 KB view cache or a 4.5 KB FAT
window takes the first hole that will have it, which is a low one. They are
not what splits a heap in half.

**What splits it is a package's or a driver's claim**, and docs/FIELD-NOTES.md 2
says so from the field: the two blocks that stranded a 116 KB reload were
the Sound Blaster's 20 KB staging pool and Tracker's own retained ring —
one a driver claim, one a package claim, and *both invisible to a
kernel-only compactor*.

> **Finding 1. A first pass that moves only kernel-owned claims is nearly
> a no-op on the reported failure.** It would compact four small blocks
> that were not in the way, leave every large block pinned, and report
> success. This is the finding that turns the task from an afternoon into
> a design decision.

So "data only, not programs" cannot mean "kernel data only". It has to
mean **a package's data claims move; its region (its code) does not** —
which is a good line and the one this plan takes. A region's base is its
CS and moving it is a different and much larger problem (§5 below).

---

## 2. "Exactly one kernel word names it" is false for the blocks that matter

SPEC.md §50.6's purgeable contract — one word names the block, and a zero
in it already means "no buffer, do it the slow way" — is the obvious
mechanism to borrow: to *move* a block instead of shedding it, write the
new base into that word instead of zero.

It does not survive contact with the two consumers that matter most.

**The Disk window's view cache has two naming words.** `fmv_fit` writes
the base into `[di+FS_VSEG]` (the window's state block) *and* into
`[fm_vseg]`, the global mirror, with the comment *"fm_vseg is what every
reader looks at, and this may just have changed it"* (`kernel/files.inc`).
A one-word relocation updates the state block and leaves every reader
looking at the old base.

**Tracker's module buffer has thirty-two derived segments.** `trkplay.inc`
normalises each sample to its own base — *"each sample gets a normalized
base seg = blobseg + (start >> 4)"* — and `MP_SEG` per channel holds the
*playing* sample's base segment. `[trk_modseg]` is one word, and rewriting
it relocates nothing: the mixer never reads it again after load.

> **Finding 2. Relocation cannot be a word poke. It has to be a callback**
> — which is exactly what SPEC.md §50.3.1 already names as cure 1 ("a
> relocation callback every holder implements") and declines to build.
> The good news is that the callback is cheap to deliver, because the
> package dispatcher already exists: `PKG_DISP` at offset 12 of every
> package and driver image, the same three bytes `wm_pkgcall` and
> `drv_call` go through.

---

## 3. The pre-emption hazard, and the two things that bound it

Moving a block under a holder that is *running* is silent memory
corruption — the worst failure this tree can ship, and the reason to be
slow here. Three contexts can hold a live pointer into a claim:

**(a) Other tasks.** Bounded by `sch_lock`: raising it pauses task
switching while the tick keeps running, which is `dsk_xfer`'s own
precedent for exactly this (a floppy transfer holds it for seconds).
Compaction raises `sch_lock` across the moves.

But `sch_lock` only stops a switch *from now on*. A worker already
suspended mid-mix has `MP_SEG` in a register and its segment on its own
stack. **`sch_lock` does not save Tracker.** What does is a park
handshake — see §4.

**(b) Interrupt handlers.** Not bounded by `sch_lock` at all. The
mitigation is a pin: **any claim with `MC_DMA != 0` never moves**, which
covers the Sound Blaster's DMA double-buffer and ring by construction, and
is right for a second reason — the 64 KB page property is a property of the
address, so a bus master mid-transfer cannot be relocated even in
principle.

**(c) The calling task's own stack.** `mem_claim` is reached from the UI
task or from a window callback (SPEC.md §20.6 rule 7 forbids `OSAPI_MEM_*`
to a worker), so the caller may itself be a holder deeper in its own frame
— `menu_drop` holds the save-under across the whole menu, and a menu
command can claim. This is bounded by the *holder's* discipline: re-read
the naming word at each use, never carry a derived pointer across a claim.
`menu_drop` already satisfies it (`mov ax, [menu_sseg]` at each use).

> **Finding 3. `sch_lock` plus a DMA pin covers everything except a live
> worker task**, and a live worker is precisely what Tracker, Fractal,
> ModPlug and Frotz all have. Workers need a rendezvous, not a lock.

---

## 4. The proposed mechanism

Five pieces. The first three are the feature; the last two are the door.

### 4.1 Movability is declared per claim, and the default is PINNED

`MC_SIZE` goes 8 → 10 with **`MC_RLOC`**, the relocation handle: `0` =
pinned, otherwise the near offset of the holder's relocation proc, called
through the owner's dispatcher. `mem_tab` grows 256 → 320 bytes **in
`.lowbss`**, so it costs nothing against `KERN_CODE_MAX`. This is
`MC_DMA`'s own precedent — the record went 6 → 8 to carry a property a
moved claim has to keep.

Default pinned matters: **every existing claim keeps working untouched**,
and adoption is one package at a time. A claim opts in through a new
slot (`OSAPI_MEM_MOVABLE`, or a variant claim entry — see decision D2),
and unopted claims behave exactly as they do today.

Pinned unconditionally, whatever the holder asks for:

- a package **region**, a driver image, an on-demand kernel module
  (base = CS, SPEC.md §50.2/§2.8);
- any claim with `MC_DMA != 0`;
- any claim whose owning instance has a live worker that did not park.

### 4.2 Relocation is a dispatcher call, not a word poke

```
in  (to the holder's proc)  BX = the old base segment
                            DX = the new base segment
                            the bytes have ALREADY been copied
out                         nothing; every register preserved
```

The kernel far-calls `owner:PKG_DISP` with BP = `MC_RLOC`, which is
`wm_pkgcall`'s shape exactly, so the holder writes an ordinary near proc
with a near `ret` — no `retf` anywhere, the rule SPEC.md §51 already makes
for drivers. A holder that only needs one word writes three instructions;
Tracker rebases its 31 sample segments and its four `MP_SEG` channels.

For a **kernel** claim the proc lives in `KERNEL_SEG` and is reached the
same way with the owner replaced by `KERNEL_SEG` — which also lets the
view cache fix both `FS_VSEG` and `[fm_vseg]`, the thing a word poke could
not do. (`memory.inc` is `.cold`, so this is a shim call, not a near call
across a section boundary — `tools/os88ovlchk.py` enforces that.)

### 4.3 A worker parks at `OSAPI_TASK_ALIVE`

The one call SPEC.md §20.6 rule 2 already makes mandatory once per outer
loop is the natural rendezvous. The kernel raises a park request on the
instance; `inst_pkg_alive` sees it, parks the worker there — where by
construction it holds no derived pointer, because it is at the top of its
own loop — and releases it when compaction is done.

Bounded and failure-safe: the kernel waits a few ticks, and **a worker that
does not park in time simply leaves its instance's claims pinned for this
pass**. A sleeping worker, a worker mid-`task_sleep`, a package built
against an older SDK — all degrade to "not compacted", never to "moved
underneath". That is the same shape as every refusal in §50: a normal
outcome with a documented slow path.

### 4.4 Where it runs: plan first, move only if it will work

Into `mem_claim`'s existing shed-and-retry loop, one step ahead of the shed:

```
.go:  call mem_claim_1          ; try
      jnc .out
      call mem_compact          ; CF=0 = something moved, so the map changed
      jnc .go
      call mem_shed_one         ; nothing left to move: give up a cache
      jnc .go
      ; refuse
```

Compaction **before** shedding is a deliberate reordering and an
improvement on today: a shed can destroy `MEM_P_DIRW`, whose own priority
comment prices it at *"a long operation getting much longer"* (seconds of
`int 13h` on the field machine), where compaction costs a memcpy measured
in tenths of a second. Cheap first is what the priority ladder was built to
express, and a memcpy is cheaper than that cache.

**Performance is bought by a dry run.** `mem_compact` walks the table in
ascending base order, computes where each movable claim *would* land, and
computes the largest run that would result. If that run is still smaller
than what the caller asked for, it returns CF=1 **without moving a byte**.
So the copy is paid only when it is going to succeed, and a claim that
would fail anyway costs a table walk (MEM_MAX = 32) rather than a heap
copy.

Termination: the plan pass assigns each movable claim its lowest legal
base, so an immediately following pass finds nothing to move and answers
CF=1. Sheds are bounded by MEM_MAX. The loop cannot spin.

### 4.5 The algorithm itself

One pass, low to high, over live data-arena claims sorted by base
(selection sort over 32 records — ~1,000 compares, nothing):

```
fill = mem_base
for each claim in ascending base order:
    if pinned:   fill = max(fill, claim.end);  continue
    target = fill
    if claim carries MC_DMA and target would straddle a page: bump it
    plan claim -> target;  fill = target + claim.para
```

Pinned blocks are barriers: a movable claim never slides below one, but
claims above one still close up against it, so a pinned block costs one
seam rather than the whole arena. The copy is `mem_bcopy`, which already
exists and is already segment-stepped for blocks over 64 KB.

Deliberately **not** in the first pass: sliding a small movable claim into
a hole *below* a pinned block. It is a strictly better packing and a
strictly more complex one, and the simple monotone slide is what makes the
termination argument one sentence.

---

## 5. The door to moving programs, and what is behind it

Left open by construction — `MC_RLOC` on a region record is the same
handle, and the park handshake is the same rendezvous. What is *behind*
the door, so nobody costs it as a small follow-on:

Moving a region means rewriting `I_SPTR`, `W_SEG` for every window it
owns, every `MB_SEG` in the menu bar (SPEC.md §12.2), the owner word of
every claim it holds (a package's data claims are owned by *the segment it
runs in*, §50.2), `drv_fseg` for a driver, and the instance's own
`I_TASK` frame — **and every saved CS on any stack**, because a package
that far-called the kernel has pushed its own CS as the return segment. So
a region can move only when no task has any frame inside it: no worker, and
not currently dispatching a callback. That is a real feature with a real
verification story, and it is not this one.

---

## 6. Cost

**Time.** `rep movsw` on a 4.77 MHz 8088 is ~370–400 KB/s (cross-checked
against PERFORMANCE.md's Tracker text-mode row: 1,121 words per row change
at ~4% of the machine). So ~0.27 s per 100 KB moved, ~0.8 s for a
fully-packed 300 KB arena — inside the "user did something and expects a
delay" budget the ask sets, and paid only when the dry run says it will
work. Task switching is paused for that span (`sch_lock`), which is
`dsk_xfer`'s existing bargain; **a Sound Blaster stream will underrun
across a large compaction**, which is worth knowing and is an argument for
releasing `sch_lock` between individual block moves rather than across the
whole pass (decision D4).

**Space.** Measured on this branch today, `kern_big`: `KERN_SIZE` 104,960
of `KERN_BUDGET` 107,008 — **2,048 spare, four steps** — but the image rung
has **38 bytes left**, so the first byte added to `.text` costs a whole
512-byte step. Compaction belongs in `.cold` (356 bytes left in its rung,
9,766 in the segment). Estimate ~500–700 bytes of `.cold` plus the record
growth in `.lowbss`: **one to two 512-byte steps of footprint.**

`kern_small` is a separate problem and a pre-existing one:
docs/KERNEL-MEMORY.md records it at **−512 over budget already**. This
feature cannot land on the small build without the twenty-fourth move or a
third on-demand module, and that is the owner's call rather than a build
fix.

---

## 7. Decisions needed

**D1 — scope.** Confirm the line is *"a package's data claims move, its
region does not"*, not *"only the kernel's claims move"*. §1 is the
argument; without D1 the feature does not touch the reported failure.

**D2 — how a claim opts in.** A new slot `OSAPI_MEM_MOVABLE` (DX = the
claim, AX = the relocation proc) applied after the claim, or a new claim
entry point that takes the proc up front. The first is fewer moving parts
and lets a package opt in a claim it already holds; the second makes the
property impossible to forget. The API table is unfrozen (SPEC.md §20.8
rule 4), so either is affordable.

**D3 — which holders adopt it in this pass.** My proposal, in order of
value: **Tracker's module buffer** (the field case), **Paint's canvas and
undo image** (the largest claims on a graphics machine), the **SB staging
pool** (the other half of the field case, and a driver rather than a
package — same mechanism, `drv_call`'s dispatcher). Everything else stays
pinned and unchanged. Each is a separate commit with its own A/B.

**D4 — `sch_lock` for the whole pass, or per block.** Per block lets a
sound stream survive a big compaction and lets the map change under us
between blocks (harmless — each move is independently consistent; worst
case the pass achieves less than it planned). Whole-pass is simpler and
matches `dsk_xfer`. I lean per block, with the plan re-validated per move.

**D5 — the small build.** Land this on `kern_big` only, or ask for the
budget move first.

---

## 8. How it gets verified

Compaction is a change whose success looks exactly like its failure until
something reads the wrong memory an hour later, so the gates come first:

- **A `tests/heapfrag` gate package**: claim a comb of blocks, free every
  other one, claim something only compaction can satisfy, and verify
  **the contents of every surviving block** afterwards — not just that the
  claim succeeded. A compactor that moves bytes to the right place and a
  compactor that moves the wrong bytes both report success.
- **`make HEAPCOMPACT=0`** as the A/B, removing the body and not just the
  call, on `make FDDPROBE=0`'s precedent.
- **The field case end to end** on MartyPC (`os8088_5150_sb`): load
  `BEVERLY.MOD`, play, close, reopen, load again — the docs/FIELD-NOTES.md 2
  sequence — and read `mem_tab` from outside the guest with
  `tools/os88marty.py` to confirm the arena actually packed.
- **Task Manager map before/after**, which already draws every claim at its
  real address and is the cheapest visual confirmation there is.
- The **`os88mouse.py`** discipline for any scripted clicking (CLAUDE.md),
  and the 5150 for anything with a disk in its timing.
