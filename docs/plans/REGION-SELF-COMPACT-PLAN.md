# A region that can compact ITSELF — and the pass pair that has to land with it

**Status: BUILT AND ADOPTED. §3's defect fix is +35 bytes; §5's posted request
is +367 and shipped as SPEC.md 66.4.3; §8 is Tracker, the first consumer,
at +128 bytes of its own `.o88` and none resident.** Two things were wrong and
each made the other measure as worthless. SPEC.md 66.6.1 built everything a region needs to move
and left out the only moment a package ever wants it; SPEC.md 66.4.1's
"alternatives, not cumulative" rule then means **a claim that needs both
compaction passes gets neither of them**. Fix either alone and the measured
gain is zero, which is how both have stayed invisible.

Read SPEC.md 66 first. §3 below is a defect against what SPEC.md 66 was asked
for, not a new feature, and it is the half that moves the number.

---

## 1. The report

> The program, a DOS program runner, needs massive heap for the DOS arena.
> The sound driver is mounted at the top of the heap. The DOS package runs
> under the sound driver. The DOS package calls for the sound driver to
> unmount, and it does. The DOS package asks for max heap — and it cannot
> compact into the space freed by the sound driver, so it loses 14KB of
> potential heap.

And the standing requirement it is measured against, from the ask SPEC.md 66
was built for:

> * ALL regions are movable, with the exception of the package making the call
>   and of modules. ALL packages and all workers subscribe to being movable.
> * ALL available ram is reported, and recovered when a compaction is done.

The first bullet's exception is §2. **The second bullet is not met today**, and
that is §3.

---

## 2. The caller's own region — the pin the report is about

`mem_frameless` (SPEC.md 66.6.1) asks four questions of a region before the
compactor may move it. The third is `mem_in_nest`: *is a frame standing in the
image at BX?* — answered off `wm_pkgs[0..wm_pkgd)`, the stack of segments
`wm_pkgcall` pushes before every kernel→package far call.

**A package reaches `mem_claim` only from inside its own callback.** So at the
instant package S asks for memory, `wm_pkgs` names S, `mem_in_nest` answers
yes, `mem_frameless` refuses, and `mem_can_move` pins S's region.

> A package's region is pinned at exactly the moment moving it would pay, and
> by the act of asking.

That is not a defect in `mem_in_nest`. It is telling the truth: the CPU pushed
S as the far-return CS at the `call far`, `OSAPI_SLOT` pushed S again with its
own `push ds`, and the package may have pushed it a third time itself — Paint
does `push ds` at twelve sites. Those words are real and they are stale the
moment the region moves. `wm_pkgs` is the cheap, exact proxy for them.

### 2.1 …and the geometry it produces

Booted `build/os8088-360.img` on `os8088_xt_hdd`, opened the Calculator and
then the Browser, and read `mem_tab` off the running machine with
`tools/heapmap.py`:

```
99000..9E000    20.0K  inst 3 (BROWSER region)  movable  top-down  rloc=160
9E000..A0000     8.0K  inst 1 (CALC region)     PINNED   top-down  rloc=0
```

A region is claimed **top-down** (`mem_claim_hi_x`, `kernel/loader.inc`), so
the package launched FIRST sits at the ceiling and the next one lands directly
beneath it. Close the Calculator and there is an 8KB hole at the top of the
heap with the Browser's region under it — the report's shape exactly, with a
package standing in for the driver and 8KB standing in for 14.

The Browser is one of the five packages in the tree that already declares
`OS88_REGION_MOVABLE` (`rloc=160` above, against the Calculator's `rloc=0`),
so the declaration is not what is missing.

**What is measured and what is modelled.** The layout above is READ off a
running machine. The close is MODELLED — the Calculator's record is dropped
from the map that was read, which is what `mem_free` does to it — and so are
§3.1's pass figures, through `heapmap.Map.compacted()`, the tree's own
host-side model of `mem_cp_plan` (`tests/heaphi.py` and `tests/drvmove.py`
already assert against it). §7 is how the whole of it gets asserted in the
guest instead.

---

## 3. The defect: a claim that needs both passes gets NEITHER

This is the half that was mis-reported the first time this plan was written,
and it is a defect against §1's second bullet rather than a design trade.

SPEC.md 66.4 gave the compactor two passes. The ascending one packs bottom-up
claims down onto the floor; the descending one (66.4.1) packs top-down claims —
every CS on the heap: regions, driver images, modules — up against the ceiling.
`mem_cp_mine` makes a claim whose door disagrees with the pass in flight a
**barrier**, so each pass moves only its own half.

`mem_compact`'s ladder then picks **one**:

```
.plan:  call mem_cp_plan        ; CX = the run THIS pass would leave
        or dx, dx
        jz .nowt                ; nothing this pass can move
        or di, di
        jz .doit                ; "compact regardless" (AX = 0)
        cmp cx, di
        jae .doit               ; it fits → run this pass and RETURN
.nowt:  ; …park, then:
.flip:  cmp byte [mem_cp_msk], 0
        jne .undo               ; already turned round: nothing left to try
        mov word [mem_cp_msk], 0xFFFF
        jmp short .plan
.doit:  call mem_cp_run
        call mem_cp_end
        clc
.undo:  call mem_cp_end
        stc                     ; ← NOTHING WAS COPIED
```

`.doit` runs one pass and returns. For a sized claim, CF = 0 comes back only
when some **single** pass's own plan already satisfies it. So:

> If the ascending pass alone is short and the descending pass alone is short,
> `mem_compact` copies nothing at all and answers CF = 1 — even when the two
> together would have satisfied the claim twice over.

`mem_claim`'s retry loop cannot rescue it: the loop re-enters `mem_compact`
only after a call that returned CF = 0, and CF = 0 means the claim already
fits. So the two passes are never sequenced for the one claim that needs them.
The claim falls through to `mem_shed_one` and then fails, with the room sitting
there in two runs.

`mem_avail` has the same hole from the reporting side. It calls `mem_cp_plan`
once, and `mem_cp_end` leaves `[mem_cp_msk]` at 0 on every path, so what it
reports is the **ascending pass alone** (`kernel/memory.inc`, `mem_avail_x`).
Its own header says under-reporting is "the error that is invisible", and this
is that error with a second cause.

### 3.1 What it costs, on the measured layout

`heapmap.Map.compacted()` over §2.1's map with the Calculator's record dropped
— the 8KB hole standing above the Browser's region:

| | ascending | descending |
|---|---|---|
| caller's region PINNED — today | **494.5K** | 418.0K |
| caller's region MOVABLE, one pass | **494.5K** | 426.0K |
| caller's region MOVABLE, both passes | **502.5K** | |

Read the middle row against the top one. `mem_compact` takes whichever single
pass is better, the ascending one wins by 68.5K, and the answer is 494.5K
**whether the caller's region is pinned or not**. Read the bottom row: both
passes together give 502.5K — 494.5 + 8.0, the hole to the byte.

So each fix measures zero on its own, for a different reason:

- **Un-pin the caller only.** The pass that could use the answer is never the
  pass that runs. 494.5K.
- **Run both passes only.** The descending pass reaches the ceiling and finds
  the caller's region pinned, so the hole above it stays a separate run.
  494.5K.

**Together: 502.5K.** That is why they are one plan and why taking either alone
would look like a feature that did nothing.

### 3.2 What the rule was protecting, and why it survives the fix

66.4.1's argument is a cost one: *"Running both would spend the ascending copy
for a claim the descending pass was going to have to satisfy anyway — and the
descending copy is the expensive one, being over the largest blocks on the
machine."*

Keep it, by asking in order and stopping early:

1. plan ascending — if it satisfies, run it and stop. (Today's behaviour, and
   the common case.)
2. else plan descending — if it satisfies alone, run it and stop. (Today's
   behaviour.)
3. else run **both**, cheapest first.
4. else refuse.

Nothing is wasted at step 3: step 2 has already established that the expensive
pass alone is not enough, so the cheap copy is needed rather than speculative.
And the ascending pass cannot make the largest run *smaller* — it only slides
bottom-up claims onto floor paragraphs the walk has already passed, which
merges holes upward — so committing it before the descending plan is taken
loses nothing even when the claim ends up refused anyway.

### 3.3 BUILT AND MEASURED: +35 bytes, and it needs no new planning machinery

The fix is the two existing walks, sequenced — `mem_compact`'s last-resort arm
stops refusing and runs the pair instead:

```
.both:
    mov word [mem_cp_msk], 0
    call mem_cp_run             ; ...the floor packs down
    mov bx, dx                  ; DX = what it moved, banked across the second
    mov word [mem_cp_msk], 0xFFFF     ; walk (mem_cp_walk preserves BX)
    call mem_cp_run             ; ...and then the ceiling packs up
    or bx, dx                   ; DID EITHER PASS MOVE ANYTHING? mem_claim's
    call mem_cp_end             ; retry loop rests on CF = 1 when nothing did -
    or bx, bx                   ; a CF = 0 that moved nothing is an infinite
    jz .none                    ; loop there (SPEC.md 66.4's termination)
    clc
```

**Measured, not estimated** (`tools/kernsize.py`, `kern_big`):

| | bytes |
|---|---|
| `.cold` | 39,352 → 39,387 = **+35** |
| `.text`, `.bss`, `.lowbss` | **0** |
| `KERN_SIZE` footprint | 110,592 → 110,592, **no rung crossed** |
| `kern_small` | **0** — `OS88_COMPACT` is `KERN_BIG` only (`kernel/kernel.asm:381`) |

It crosses no rung but leaves the cold rung with **37 bytes of headroom** where
it had 72, so the byte is what to quote and the step is not (CLAUDE.md's
banner). `.undo` became unreachable and was deleted — `t_asmrules` caught it,
which is 3 of the 35 back.

**A combined PLAN is NOT needed, and that is what makes this cheap.** The
earlier revision of this document costed one at ~150–250 bytes because
`mem_avail` has to report a number without moving anything. It does not need
one, for a reason the deferred shape supplies for free:

> After the posted compaction has run, the heap is packed in **both**
> directions — bottom-up claims at the floor, top-down at the ceiling, one run
> between. `mem_avail`'s ascending-only plan then sees that single run and is
> **exact**. The package reads its number on the wake, after the move, instead
> of predicting it before.

So there is no third walk body, no second fill point, no invariant that a plan
must not over-report a run, and none of SPEC.md 66.4's one-body rule is
touched. `mem_cp_plan` and `mem_cp_run` stay two entries into one walk.

### 3.4 What it costs in TIME, and the honest answer is "nothing that matters"

`rep movsw` is measured at **13.3 cycles a byte** (PERFORMANCE.md Set 117.2),
which is 2.79 µs a byte and **2.86 ms a KB** at 4.77 MHz. So a compaction costs
about 2.9 ms per KB it actually moves:

| | moved | cost on a 4.77 MHz 8088 |
|---|---|---|
| §2.1's measured layout (9KB of floor claims + the 20KB region) | 29 KB | **~83 ms** |
| a DOS-runner-shaped heap (a 60KB region + ~100KB of other claims) | 160 KB | **~460 ms** |

**No existing claim gets slower.** Steps 1 and 2 of §3.2's ladder are today's
code unchanged: a claim the ascending pass alone funds still runs one pass and
returns, and so does one the descending pass alone funds. The `.both` arm is
reached only where today **nothing happens at all** — so it does not make a
success dearer, it turns a refusal into a success and charges that success the
copies it needs.

**Where a combined plan WOULD have been faster is one case only**: a claim that
even both passes cannot fund. The plan would refuse before copying; `.both`
copies first and refuses after, at the 2.9 ms/KB above. And those copies are
mostly not wasted — the heap is left better packed, and `mem_claim`'s next
tier is the *shed*, which SPEC.md 66.4 already ranks as the dearer primitive
because dissolving `MEM_P_DIRW` is priced at seconds of `int 13h`.

So on the stated rule — *~300–400 ms of difference would not buy 100+ bytes* —
this is not close: the difference in the success path is **0 ms**, and in the
failure path it is a few hundred milliseconds on a claim that was going to be
refused either way. **Take the 35 bytes.**

#### 3.4.1 The one behaviour change outside the feature, and it is separable

`mem_compact(AX = 0)` — "compact regardless" — now takes both passes where it
took the ascending one. It has two callers: the posted request of §5, which
wants exactly that, and `mem_unblob` at the end of `kmain` (SPEC.md 50.6.3).
On a machine that mounted drivers from `SYSTEM.CFG` before that point, a driver
image is a top-down movable claim (SPEC.md 66.6.3), so boot now pays one
ceiling pack: **~17 ms for a 6KB image** against a hard-disk boot of 2,087 ms
(`docs/plans/completed/BOOT-PERF-PLAN.md`). That is a boot-time *improvement*
in the thing HEAP-UNPIN-PLAN §2.0 is about — the wall a boot-order mount
builds — but it is a number `tools/os88boot.py` should take rather than an
argument, and if anyone would rather not spend it, giving the posted request
its own entry instead costs about **4 bytes**.

## 4. Option 1 from the report — move the caller's region and patch its frame

*"Allow a compaction of the calling region during the compaction, and then tell
it it moved in the return."*

**What it would take.** The two words the KERNEL pushed are at computable
offsets, and that is the half that works. An `OSAPI_SLOT` cell is eight bytes
of fixed shape:

```
push ds        ; the PACKAGE's DS  ── stale after a move
push cs
pop ds
call <routine>
pop ds
retf           ; pops the far-return CS the CPU pushed ── stale after a move
```

so at entry to the kernel routine the frame is `[SP+0]` near return, `[SP+2]`
DS = S, `[SP+4]` far IP, `[SP+6]` far CS = S. Patch +2 and +6 and the package
comes back with DS and CS already correct and never knows it moved.

**Why it is refused.** Those are the only two copies the kernel can find, and
they are not the only two copies:

| copy | where | can the kernel find it? |
|---|---|---|
| far-return CS | `[SP+6]` | yes |
| `OSAPI_SLOT`'s `push ds` | `[SP+2]` | yes |
| `api_x`'s conditional `push es` | frame | yes, per cell family |
| **whatever the package pushed itself** | anywhere | **no** |
| **whatever the package holds in a register** | ES, a spare | **no** |
| a second frame, if S appears twice in `wm_pkgs` | anywhere | no |

Rows four and five can only be answered by a declaration — *"my frame holds no
copy of my own segment and no register does either"* — which the kernel cannot
verify, which a package gets right on the day it is written and wrong on the
day someone adds a `push ds`, and whose violation is **not a crash but a wrong
answer**: the package reads its own data out of a segment that is no longer its
image. SPEC.md 66.3 rule 5 already refuses its sibling for that reason.

It also couples `mem_claim` — eight near calls deep by the time `mem_cp_run`
copies anything — to the byte layout of an API cell, and it does not escape §3:
a patched frame un-pins the region and the ascending pass still wins, so option
1 built alone measures 494.5K against today's 494.5K.

**Verdict: refused.** Not because it cannot be built: because what it rests on
cannot be checked.

---

## 5. The proposal

Option 2 from the report — *"take it off as the running program, compact, and
let it claim on its next turn"* — plus §3. Three pieces.

### 5.1 The EXACT combined plan — required, and not only for the what-if

This section has been wrong twice and the corrections went in opposite
directions, so here is the settled position with the reason each earlier one
failed.

**A failed claim is DESTRUCTIVE, which is what makes a pre-post estimate
necessary.** `mem_claim`'s refusal path is compact → *shed* → retry, and the
shed dissolves purgeable caches at the claimant's own rank. SPEC.md 66.4
prices rebuilding `MEM_P_DIRW` at seconds of `int 13h`. So a package that wants
**a specific amount** — Tracker opening a 400KB module — cannot be told to
"post and find out": posting throws the caches away and then refuses anyway.
Its refusal has to mean *"I could not have had this even trying my hardest"*,
and that is the question `mem_avail` exists to answer. §5.1's earlier
*"read it on the wake"* answer is right for a package that wants **all of it**
(the DOS runner) and wrong for one that wants **an exact figure**.

#### 5.1.1 …and the +35-byte fix has already made plain `mem_avail` short

This is the finding that decides it, and it is about code already committed.
`mem_claim` compacts both ways now (§3.3); `mem_avail` still plans **one**. So
the number the SDK teaches a package to ask for is smaller than the number the
allocator would hand out — measured over the same thirteen layouts
(`tools/heapwhatif.py`):

| layout | `mem_avail` says | `mem_claim` can now deliver | |
|---|---:|---:|---|
| `[free][me][gap][gap][two movable]` | 426.5 | **474.5** | short by **48** |
| two holes, one above one below | 458.5 | **482.5** | short by **24** |
| `[free][me][gap][another movable]` | 458.5 | **474.5** | short by **16** |
| either interleaved layout | 458.5 | **462.5** | short by 4 |
| the other eight | — | — | exact |

Not a regression — it under-reports, so no promise is broken, and it
under-reported before too. But it makes the both-passes fix **inert for the
ask-then-claim pattern**, which is the pattern the SDK teaches and the only one
a package with an exact requirement can use. **So the combined plan is the
other half of the change already in the tree**, and the what-if is the same
code with a flag rather than a feature of its own.

#### 5.1.2 The algorithm, validated in the model before anyone writes assembly

`tools/heapwhatif.py`'s `exact2` — **13 of 13 layouts exact**, agreeing with the
wake reading everywhere, including both interleaved ones.

**The ordering is the whole trick, and the obvious spelling is wrong in the
dangerous direction.** Two independent sweeps — floor fill rising over the
bottom-up claims, ceiling fill falling over the top-down ones — is the natural
reading of "plan both passes", and it **over-reports by 12–20K** whenever a
bottom-up claim sits above a top-down one, because the two stacks **wall each
other in**: the lo claim is a barrier to the descending pass, so the hi claim
beneath it cannot reach the ceiling; and once the descending pass has left it
there, it is a barrier to the ascending pass in turn. Two sweeps **in the order
the passes actually run** see that; two independent ones cannot.

An over-report is the failure this whole section is about — `mem_avail`
promising memory `mem_claim` cannot produce, so the package claims, the shed
fires, and the caches go for nothing.

**The kernel cannot mutate `mem_tab` inside a plan**, so the second sweep needs
each top-down claim's *post-descending* base. Two spellings:

| | cost |
|---|---|
| ask per barrier — an O(n²) ceiling re-walk over at most `MEM_MAX` = 32 records | no scratch; microseconds, and the plan is already O(`MEM_MAX`²) |
| bank them in `MEM_MAX` words of `.bss` | 64 bytes of `.bss`, one sweep |

The first is preferred on this project's own arithmetic: a `.bss` byte is worth
a `.text` byte (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md`), and nothing
here is on a hot path — `mem_avail` is called when a package is about to ask
for memory, not per frame.

**Estimated ~90–120 bytes**, which is the range the requester sanctioned, and
it buys three things rather than one: plain `mem_avail` telling the truth about
the kernel it now has, the what-if (the same walk with the caller's own region
excused), and a refusal a package can trust. Unlike §3.3's +35 this is an
**estimate** — the algorithm is validated, the encoding is not.

### 5.2 One posted request

> **`OSAPI_MEM_COMPACT_WAKE`**, at the next free cell (`0x0550` today) —
> *"compact everything you can, including my own region, then wake me."*
>
> `BX` = your window. `AL` = the shed rank this compaction is to respect.
> Out: CF = 0 posted — **return from your callback**, and do your sizing and
> claiming in your `OSAPI_WM_ONWAKE` handler (SPEC.md 74.1).
> CF = 1 refused: `BX` is not your window, or one of your requests is already
> standing.

**The rank, and why it is on the request.** `mem_compact` derives the rank a
cache must be cheaper than from the *pending claim's* owner (`mem_rank_bh`), and
forces 0 — dissolve nothing — when `AX` is 0. The posted pass runs after the
asking package's turn is over, so there is no claimant to derive one from and
the default would decline to shed at all. Carrying `AL` and letting the service
point store it into `[mem_pg_rank]`'s own door is about **10 bytes**.

It is worth those ten rather than leaning on `mem_claim`'s own shed at claim
time — which would also be correct, `mem_claim`'s loop being
compact → shed → retry — because a purgeable claim the posted pass may not
dissolve is a **barrier** to that pass, so the pack it produces is worse and the
wake then needs a second shed-and-compact round to reach the same place. Ten
bytes to make the number on the wake the final one.

**One request and one wake.** An earlier draft said "two calls", meaning two
kernel-internal `mem_compact` calls, one per direction; §3.3 makes that one
`mem_compact` that runs the pair, so the package posts once and one wake comes
back.

### 5.3 It is two patterns this tree already ships, composed

**`OSAPI_PKG_REHOME` solved this exact problem once.** SPEC.md 20.12.10's slot,
in the SDK's own words:

> *CALL IT FROM YOUR ENTRY PROC AND RETURN CF=0 WITH BX=0. It only RECORDS:
> you are still executing in the region this frees, so the kernel does the
> work after you return.*

Word for word the problem here, with "frees" for "moves".

**And `ui_task` already has the quiescent point.** `kernel/ui.inc`, the top of
the event loop:

```
.loop:
    ; --- 0. a posted restart, spent with NOTHING held (SPEC.md 20.10) ---
    cmp byte [ui_rebootq], 0
```

A posted action, cleared before it is acted on, spent at the top of the loop
with no lock held and no callback in flight — and `[wm_pkgd]` is 0 there by
construction, because every `wm_pkgcall` on this task has returned. That
matters beyond tidiness: `mem_compact`'s park request DROPS `[sch_lock]` for up
to `INST_PARKW` ticks (SPEC.md 66.5), which from inside a repaint pass holding
`gfx_lock` would be a deadlock against `OSAPI_MEM_PARKSAFE` rather than a slow
path.

**So `mem_can_move`, `mem_frameless` and `mem_in_nest` are untouched.** That is
the whole argument for this shape over §4: at the service point the region is
**genuinely frameless**, and the existing predicate answers "movable" because
it is TRUE, not because it was bypassed.

Kernel side, in four pieces:

| piece | where | bytes |
|---|---|---|
| §3.3's both-passes arm | `memory.inc` `mem_compact` | **+35 `.cold`, MEASURED** |
| `[mem_cpq]` + `[mem_cpq_lvl]` | `.bss` | 3 |
| the cell — validate `BX` is the caller's window, store, `sch_uiwake` | `osapi_table` 0x0550 | ~40 body + 8 table; the closest analogue in the tree, `osapi_pkg_rehome_x`, is **33 bytes counted** for the same validate-and-record shape |
| the service — clear first, `mem_compact` at the posted rank, `wm_wake` | `ui.inc` `.loop` step 0 | ~30 |
| the rank door (§5.2) | `memory.inc` | ~10 |
| the exact combined plan + the what-if flag (§5.1) | `memory.inc` | ~90-120 |
| **the region un-pin itself** | — | **0** |

**Total ≈ 215-245 bytes resident on `kern_big`, 0 on `kern_small`**, of which 35
is measured, ~90-120 is an algorithm validated in the model but not encoded,
and the rest is anchored on a counted analogue.

**The un-pin is still free, and that remains the point of the shape.** No
predicate changes: at `ui_task` step 0 `[wm_pkgd]` is 0, nothing is loading, and
the package's worker is absent or parked — so `mem_frameless` already answers
"movable" there. The feature is *where* the compaction runs, not new code to let
it run.

**The un-pin is free, and that is the point of the whole shape.** No predicate
changes: at `ui_task` step 0 `[wm_pkgd]` is 0, nothing is loading, and the
package's worker is absent or parked — so `mem_frameless` already answers
"movable" there. The feature is *where the compaction runs*, not new code to
let it run.

### 5.4 The package needs no new question answered afterwards

After the service point the region has PHYSICALLY moved, so:

- The package is not told it moved and does not need to be. `wm_pkgcall` sets
  DS from `W_SEG` live and `mem_region_reloc` put `W_SEG` right; its own data
  claims were fixed by its own relocation proc; its near offsets never moved,
  a package being `org 0` with no relocation of any kind.
- Plain `OSAPI_MEM_AVAIL` on the wake is the number to claim, and it is exact:
  the region is where it is going to stay, so the promise form answers the
  whole run.

### 5.5 The go-round, and why the decision belongs on the second trip

A package that posted, woke, found less than it hoped for and posted again
would ping-pong. Three things stop it, and only the first is the kernel's:

1. **One request per package may stand at a time** — a second post while one is
   unserviced is refused (CF = 1). That bounds the queue, not the loop.
2. **The compaction is idempotent.** A second pass immediately after the first
   finds every claim at its fill point, counts no movers and answers CF = 1
   (SPEC.md 66.4's own termination argument), so a re-post costs a walk of 32
   records and changes nothing. The loop cannot starve the machine.
3. **The contract says decide on the wake, and the SDK macro enforces it** with
   a one-shot flag: post once, and on the wake claim what plain `mem_avail`
   reports and proceed — whether or not it equals what the what-if predicted.
   The what-if was a measurement of a state that has since changed, which is
   exactly what §5.1 says it is.

The requester's own instinct is the rule: *"on the second trip it makes its
decision, and doesn't recall the max compact a second round."*

---

## 6. What this does NOT solve

1. **A claim made by the package's WORKER.** A worker pins its own region
   through `mem_busy_seg`, not through the nest, and cannot park itself while
   inside `mem_claim`. The deferred path helps where the in-line path never
   could — the service point runs on `ui_task`, so `mem_compact`'s own park
   request can stand the worker up — but only if the package also declares
   `OSAPI_TASK_RESTARTABLE` (SPEC.md 66.6.2). A sentence in the slot's
   contract, no kernel code.
2. **On-demand MODULES stay pinned**, which is the standing decision (§1's
   first bullet): each is a temporary user action and nobody has done the work.
   A module standing mid-arena is still a barrier in both passes.
3. **The mid-session mount wall itself** (`docs/plans/HEAP-UNPIN-PLAN.md` §2.0).
   This makes the wall healable on demand; it does not stop it forming.

---

## 7. What is built, what it measured, and what is NOT covered

### 7.1 Built

`mem_cp_both` + `mem_cp_newbase` + `mem_cp_ceilmv` (the combined plan),
`[mem_cp_self]` and `mem_frameless`'s excuse, `OSAPI_MEM_AVAIL_MAX` (0x0550),
`OSAPI_MEM_COMPACT_WAKE` (0x0558), `[mem_cpq]`/`[mem_cpq_lvl]` and
`mem_cpq_run_x` at `ui_task` step 0.

**MEASURED on `kern_big`, against the +35 defect fix:** `.text` +44,
`.bss` +5, `.cold` +318 — **+367 bytes**, and `KERN_SIZE` 110,592 → 111,104,
so **one cold rung crossed** (spare 37 → 36 steps). `kern_small` takes the two
cells and the queue words and none of the plan.

**That is over the ~90–120 this document estimated for the plan**, and the
estimate is where it went wrong rather than the encoding: `mem_cp_newbase`'s
two scans each carry a full register-save discipline, and the estimate priced
the arithmetic without it. The obvious trim is merging the two scans into one
pass that collects `B` and `S` together; it is not taken, and the byte is
quoted rather than the rung (CLAUDE.md's banner).

**Three of the new bytes were a defect `make` cannot see.** `[mem_cpq]` and
`[mem_cpq_lvl]` were declared inside `%ifdef OS88_COMPACT` while the cells that
name them are published on **both** kernels (SPEC.md 24.5). `kern_big` built
perfectly; `kern_small` failed with six undefined symbols, and only the rows
that build the small tree saw it — `regrowshed`, `small128` and two others went
red together, which is what a scoped soak is for.

### 7.2 What the gate asserts

`tests/heapfrag` grew four rows and they are **programmatic**: the package runs
them itself on its first paint and writes a verdict byte each, and
`tests/heapcheck.py` reads them out of its bss. No extra clicking, and the row
is 37s.

| | |
|---|---|
| 15 `mem_avail` is claimable | ask, claim exactly that, free it. The guard on the whole combined plan, in the one direction that matters |
| 16 `avail_max >= avail` | the what-if excuses our own region, so it can only find more |
| 17 the post round-tripped | `OSAPI_MEM_COMPACT_WAKE` accepted, serviced, and the wake arrived |
| 18 the wake's `avail` is not short | of what the what-if predicted before the post |

…plus a host-side reading that is worth more than any of them: **`heapcheck`
now models the same claim map itself** and compares. `mem_cp_both` is new
arithmetic whose dangerous error is an over-report, so a second, independent
reader is the right instrument. It agrees to the KB on the live machine.

**Two amputations, because a row nobody has broken is a row nobody has
tested** (`docs/WRITING-TESTS.md` 1): `mem_cp_both`'s barrier arm patched not to
resume past a barrier, and its result replaced by the whole arena — the
over-report direction exactly. Both take the row red.

### 7.3 …and the three gaps 7.2 left, all closed

1. **`mem_cp_newbase`'s own branch is exercised now.** It was not: heapfrag
   owns a worker, so `mem_busy_seg` pinned every claim it held against a
   `mem_avail` that does not park, and an amputation of `newbase` left every
   row green. The region rows reach it, and the amputation now takes **R4**
   red — `avail_max` reads 270K against the 267K the machine delivers, which
   is the over-report direction exactly.

2. **The region physically moves, and it is asserted.** `heapcheck` opens
   **PAINT first** so its region takes the ceiling and heapfrag's lands
   underneath, runs the suite, closes Paint, and presses a key; heapfrag's
   `W_ONKEY` asks both `mem_avail` questions and posts, and its wake answers
   the rest. Measured: `avail 234K, avail_max 267K, wake 267K, base 9700 ->
   9f40` — the region really moves 33KB up into the hole and the wake's plain
   `mem_avail` is the what-if's number to the KB.

   It is four rows in heapfrag's **own** result bytes and not appended to
   `hf_res`: nine test files put `HEAPFRAG.O88` on a disk, and the harness
   that reads `hf_n` expects exactly `HF_ROWS`.

3. **`tests/sndmove` is green, and the diagnosis was worth the dig.** Its
   staging did not assume what §7.3 first said it did. `base0` and `sndseg`
   were always captured before the filler; only `vec0` was read after it — and
   `fl_fill` claims `OSAPI_MEM_AVAIL`, which since §5.1.1 plans both passes
   and which `mem_claim` now delivers, so **the filler's own fill is the
   forcing event** and the image had already moved by the time `vec0` was
   taken. One line moved. **And the fix that looks right is wrong**: re-reading
   `sndseg` beside `vec0` fixes 5b and moves the failure to 4b, because the
   image has then already reached its packed position and does not move again.
   5b now reads `['0f'] -> ['0f']` — a real vector following a real move.

#### 7.3.1 Two defects the region rows found, both in this work

- **The what-if excused `mem_in_nest` and not `mem_busy_seg`**, on the stated
  ground that the worker is as true at the service point as now. That is
  false: `mem_compact` **parks** on its way past a refusal it is told about
  (`[mem_wpin]`, §66.5), so a worker merely running now is not what it will be
  then. `avail_max` read 234K where the machine went on to deliver 267K — the
  under-report that has a package skip a post that would have worked. It
  excuses both now, and the restart declaration tested above still binds.
  **The what-if may therefore read HIGH**, which is the right direction for
  it: nothing is claimed against it and the wake is exact.
- **The harness read the package's bss at the base it had before the post**,
  so if the feature worked it decoded the bytes the region used to occupy —
  which look like a base that did not move. It re-reads `W_SEG` every time
  round the poll now. A row that would have reported the exact failure it
  exists to catch.

### 7.4 What is still open

`mem_cp_newbase`'s two scans cannot be one, and its header says why rather
than leaving it to be re-attempted: `S` is a sum over `[me, B)` and `B` is not
known until a scan has ended, so a single unordered pass would have to
un-count the movers between a newly-found lower barrier and the one it
replaced. A descending pass does fix it — `B` only falls — but `mem_tab` is
unordered and ordering it is the `O(n²)` the routine exists to avoid. The
slack it **did** have came out: −19 bytes.


---

### 7.5 The size pass — one door, one walk, and the pass order it found

The feature above stood at **+367 resident bytes** and the size pass took
**189** of them back on `kern_big` (`.text` −18, `.cold` −171) and **119** on
`kern_small` (`.text` −28, `.bss` −5, `.cold` −86); SPEC.md 66.4.3 and 66.4.3.1
are the contract as it stands now. Three things changed and one was found:

1. **The two cells are one door.** `OSAPI_MEM_AVAIL_MAX` and
   `OSAPI_MEM_COMPACT_WAKE` are `OSAPI_MEM_COMPACT` (0x0590) with the verb in
   `AH` — `MEMC_WHATIF` / `MEMC_POST` — on `OSAPI_VOL_STAT`'s precedent, and
   `OSAPI_DOS_HANDOFF` moved up to 0x0598. Every caller sets `AX`.
2. **`mem_cp_newbase` and `mem_cp_ceilmv` are gone.** The trim 7.1 named —
   *merge the two scans* — was the wrong trim: the walk reaches `B − S` on its
   own by *deferring* it (a ceiling mover adds its size to the fill point and
   raises a flag; the next floor mover or pinned claim is where the run stops).
   The plan went 202 bytes → 96 with `mem_avail`'s total-free loop folded into
   the same walk on `kern_big`.
3. **`kern_small` assembles none of it.** Its cell is a nine-byte `.text`
   stub — the what-if is the plain level door, a post is refused — so the
   service hook, the queue words and both bodies left that kernel.
4. **`mem_compact`'s `.both` ran the floor first and the plan modelled the
   ceiling first** — `tools/heapwhatif.py`'s `true_combined` and
   `tests/heapcheck.py`'s `both_passes()` both say ceiling-first, and 7.1's
   *"the order the passes run"* was written against them. The two orders cut
   the same free space at different places, so the plan can promise a run
   the pass then fails to produce (`[H][L][60][H][60][L]`: 120 planned, 60
   delivered). Turning `.both` round was tried and MEASURED WRONG: the
   ceiling pass moves the asker's region, which restarts its parked worker,
   and the floor pass then pins the asker's claims (`heapcheck` R4: 223K on
   the wake against 250K). The order stays floor-first and the mismatch is
   recorded in SPEC.md 66.4.3.1 as open.

## 8. Tracker, the first consumer (SPEC.md 45.3.2)

`tests/heapfrag` proves the door opens. It cannot prove the door is the right
shape, because heapfrag is written to exercise the kernel and a real consumer
is written to get a job done — so the adoption is where the two halves of the
API turn out to be for **different kinds of program**, which is the finding
worth keeping out of this section.

**A package that wants *whatever is going* does not need the what-if at all.**
The report's own DOS runner is that shape: post, return, claim the largest run
the wake reports, and if it is less than hoped, use less. `OSAPI_MEM_AVAIL_MAX`
tells it nothing it will act on, and §5.1.1's argument — that a package with an
exact requirement cannot be told to post and find out — does not bind it.

**Tracker is the other shape and it is the one that pays for the what-if.** Its
requirement is *this module or no module*, and §5.1.1 is exactly its problem: a
failed claim sheds every purgeable cache on the way down, so a refusal that
could have been avoided is paid for twice. `trk_cpq_try` is therefore the
whole adoption in one routine — ask `OSAPI_MEM_AVAIL_MAX`, and only say
`Too big for free memory` when the answer is *not even if the machine emptied
itself for me*.

### 8.1 What it cost, and where the bytes went

**+128 bytes of `apps/tracker/tracker.o88`, none of it resident** — a package
image is compressed disk present only while the program runs, which is the
trade CTRL-GLYPH-PLAN §4 states and it points the same way here. The kernel
side is **zero**: every slot this uses was already published.

Three of the four pieces are not the feature. `trk_cpq_try` is the ask and the
post; `trk_cpqload` is the retry, and it is `trk_argload`'s shape with nothing
to look up, because the name is already in this package's own segment and the
size is already banked — so `trk_fdone`'s own copy is onto itself and there is
no `OSAPI_FILE_GOTO` to do. `[trk_cpq]` is one byte doing two jobs that are the
same job. And `.outq` is five bytes: `trk_fdone` clears the flag at its single
exit and the posted path jumps past that clear, because its wake is the same
attempt continuing.

### 8.2 The finding: a park-safe worker is not a pump

`OS88_WORKER_RESTARTABLE`'s SDK note says a worker that mixes audio must not
pair the declaration with region-movability blanket-fashion, and the obvious
reading of that is to make the declaration a **window** around
`OSAPI_TASK_ALIVE`. **That reading is wrong here and it fails silently**, which
is why it is written down: `mem_frameless` reads `[inst_restart]` **at plan
time**, and Tracker's worker is outside its own `OSAPI_TASK_ALIVE` for
essentially all of a tick. A windowed declaration therefore makes
`OSAPI_MEM_AVAIL_MAX` answer *no better* for a heap the compactor could have
emptied — the low read that §66.4.3.2 names as the failure that is silent.

So the declaration is permanent and the work goes into making the restart
lossless from **both** park points. Tracker is park-safe (SPEC.md 66.5.4), so
the second one is *blocked in `OSAPI_GFX_LOCK`* — and enumerating what that
costs is a two-line answer rather than the open-ended risk the SDK note
implies:

* `trk_render` takes the lock as its first action and draws nothing before it,
  so a restart there loses a frame and no pixels.
* `[trk_inrend]` is set by the CALLER before `call trk_render`, so it is 1 at
  that park point and would stay 1 for ever — with `trk_fs_enter`'s drain
  waiting on a worker that is no longer inside `trk_render`. `trk_worker`'s own
  head clears it, which is where the restart lands.
* `[trk_mixing]` needs nothing, and the reason is the general one: `trk_feed`
  blocks on no lock at all, so neither park point is ever inside a feed pass.

**The rejected alternative is worth naming too.** Moving `[trk_inrend]`'s store
*inside* `trk_render`, after the lock, removes the stuck state with no new code
— and breaks the drain: the flag would then mean *has the lock* rather than
*is between deciding to render and finishing*, so `trk_fs_enter` could see 0
while the worker is blocked on the lock the drain itself holds, proceed into
`OSAPI_FSX_RUN`, and let the worker draw windowed content onto the fullscreen
surface.

### 8.3 The gate, and why it counts rather than assumes

`tests/trkcompact.py` builds the heap out of **Tracker itself** — instances
stacked down from the ceiling until the floor run is under the module's size,
then the topmost closed so the survivor has a hole above it. A package region
is a top-down claim (SPEC.md 50.3.2), so the stack grows down and the one hole
in the arena is the floor; each instance takes one region off it, so **the
first N whose floor run is under the module is guaranteed to be within one
region OF it** — which is precisely the window the feature lives in, proved by
the loop's stopping rule rather than by a count somebody measured once.

Two things it got wrong first, both worth not repeating:

* **The raw largest hole is not the number a package is quoted.** The first
  shape stopped at a 106KB floor, watched a 114KB module simply fit, and
  reported the feature dead. 74KB of disk cache was sitting in that hole:
  `mem_claim` sheds every purgeable claim on its way down, so a precondition
  that does not count caches as free is measuring a heap the guest does not
  have.
* **The plan is deliberately NOT modelled.** Every region in that scenario
  belongs to an instance with a LIVE worker, so `mem_frameless` pins it until a
  compaction parks it and a bare `mem_avail` moves none of them. The one that
  does move is the asker's own, which the precondition adds by hand — and that
  asymmetry between what `mem_avail` can plan and what `mem_claim` can achieve
  is a standing property of the park, not something this work introduced.

The assertions are the **guest's own verdict** and not a screen reading:
`[trk_cpq]` seen non-zero is the post (only `trk_cpq_try` writes it, and only
after both questions have been asked), `[tui_msgp]` equal to `trk_s_cpq` is the
sentence being on the glass **before** the freeze rather than after it, and a
loaded module is a load the same heap refused before the feature. Measured:
nine instances, a 79KB floor, `BEVERLY.MOD` at 114KB, the asker's region
`8780` → `93c0` and the module playing.

### 8.4 The owner's own scenario, end to end

`tests/trkcompact.py` builds its heap out of Tracker instances because that
is what a repeatable gate can build. The scenario the feature was *asked* for
is a different thing and worth recording separately, because every step of it
is something a user does:

> boot with the sound driver not mounted, 640K, Hercules → open Sheet → open
> Paint → open Clear Skies → **mount the sound driver** → open Tracker →
> close Sheet, Paint and Clear Skies → open a 400KB `.mod`

Driven on `os8088_5150_herc_sb_gla_144` with `SLINGER.MOD`, 406,354 bytes =
**397KB**:

| step | largest run |
|---|---|
| bare desktop | 531 KB |
| Sheet | 307 |
| Paint | 231 |
| Clear Skies | 120 |
| `SOUND.DRV` mounted mid-session | 120 |
| Tracker | 71 |
| the three closed | **365**, with a second run of **92** above the driver |

The heap at the click is `[365 free][Tracker 49][pool 8][SOUND.DRV 6][92
free]`, and that is §2's pin and HEAP-UNPIN-PLAN §2.0's mount-mid-session
wall as ONE picture: the two free runs are separated by the asker's own
region, so plain `mem_avail` — which may not move it — reports 365 against a
397KB requirement, and `OSAPI_MEM_AVAIL_MAX` reports what the machine could
have had. Tracker posts, the descending pass packs all three top-down claims
into the ceiling hole (**every one of them by exactly 92KB**: the region
`7940` → `9040`, the pool `8580` → `9c80`, the driver image `8780` →
`9e80`), and the 397KB claim comes out of the 457KB run that leaves.

**The A/B is the same machine, the same disk and the same clicks** with only
`trk_cpq_try`'s call removed: `Too big for free memory`, nothing moved, no
module. The 365 is identical in both arms, which is the point — the heap is
not what changed.

**It needed a machine that did not exist**, and the reason is worth keeping
because it is not the one expected. `os8088_5150_herc_sb_gla` has 360KB
drives and a 397KB file does not fit on one; `os8088_5150_herc_gla_144` has
the drives and no card, and `SOUND.DRV`'s ATTACH refuses when neither an OPL
nor a DSP answers - so the mid-heap wall cannot be made to exist there. And
the obvious fix, copying the first machine's sound block, **breaks step 1**:
the boot overlay sniffs 388h for an OPL2 and sets the sound row's `DRVR_WANT`
when one answers, so a machine with an AdLib in it has `SOUND.DRV` mounted
BEFORE THE FIRST PAINT and "boot with it not mounted" is not a state that
machine has. The new one carries a DSP and no OPL, which is
`os8088_5150_sbonly`'s reasoning (SPEC.md 51.3.1) pointed at a different
question.

**IT IS A SUITE ROW** — `tests/trkbigmod.py`, 78 seconds — and what stood in
the way was only the module. Every real one this size is somebody's file,
which CONTRIBUTING.md §6 keeps out of the tree, so `tools/os88mkmod.py`
writes one at an exact length instead: 31 sample headers, an order table,
notes in pattern 0 and a sawtooth in sample 1, with `--selfcheck`
re-deriving `mp_load`'s own gates over six sizes. **A real module and not a
blob**, because a file `mp_load` refused would exercise the claim and then
fail the load — a green row about a machine that never played anything.

The generated 397KB module reproduces the reported run to the byte: the same
365KB/92KB split, `7940` → `9040`, `8780` → `9e80`, `trk_s_playing`. The
A/B is the row's own: with `trk_cpq_try`'s call removed, check 3 still passes
— the heap is identical — and checks 4 and 5 fail with `trk_s_nofit` and
nothing moved.

**Check 3 is the one that keeps the row honest**, and its measure took a
correction worth keeping: the gain from a compaction is *the free space
ABOVE the largest run*, not the weight of the movable claims between. The
descending pass packs every mover onto the ceiling, so whatever is free above
the run ends up joined to it whatever those claims weigh — counting the
claims is right here by luck (63KB against a 92KB hole, and the assertion is
the stricter one) and over-reports the moment the movers weigh more than the
hole they have to move into.
