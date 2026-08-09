# Discardable claims, and a window that can be raised without repainting

**Recommendations 1–2 and the placement in 3 are BUILT** — SPEC.md §50.6
(purgeable claims) and §11.96 (the raise cache). What is left of this document
is the argument behind them, plus the items still open, which are marked in
§4. The costing below was written before the code; the measured outcome is at
the end of §4.

Forward-looking in `docs/SOUND-PLAN.md`'s sense for the rest: it is the
argument and the costing, written down before the code so the decisions are
the ones we meant to take.

It starts from two things measured in docs/NOTEPAD-NOTES.md §7: raising an
obscured Note Pad costs **1,026 ms**, of which **578 ms is lettering 464 glyph
cells that were genuinely overwritten**, and that is the one open latency with
no faster version of the existing path. The only way under one typematic
repeat is **not to repaint at all**, which means keeping the pixels — which
means memory that we do not currently have a good way to spend.

---

## 1. Checking the premise: does a covered window's content stand still?

**Nearly, and "nearly" is not good enough to build on — but the fix is one
hook, not app discipline.**

Note Pad's worker runs while backgrounded, and it draws in three places. Two
already ask: `np_sbcheck` (the thumb, when the background count moves
`[np_drows]`) and `np_pdrawn` (the find panel's match count) are both gated on
`OSAPI_WM_OBSCURED`. The third is not: `[np_sowed]`, §27.7.8's dropped-scroll
debt, calls `np_redraw` unconditionally. A toast expiring is a fourth.

So the premise is *nearly* true of Note Pad today and is not true of packages
in general, and **the design must not depend on an app keeping a promise it
does not know it is making.**

**Invalidate on draw instead, at `wm_clip_set`.** §11.3 makes that the choke
point already: a background painter arms a clip region before it draws,
because a covered window that draws without one paints over the window on top
of it. So "covered ⇒ either it armed a region, or it is already broken" is an
invariant the window manager relies on today. One test in `wm_clip_set` —
this window has a saved image, drop it — makes the save self-invalidating for
every app at once, with no new rule for anyone to forget.

Three other things must drop it, and all are one-liners at sites that already
exist: the window moving or resizing (`ui_drag`, `ui_grow`, `wm_fit`), the
adapter changing (§39.11's `vid_switch`, which already drops the back buffer),
and the window being destroyed.

---

## 2. What a window's save-under costs

The mechanism is **already in the tree at menu scale** and needs generalising
rather than inventing: `menu_drop` claims `MEM_K_SAVE`, calls `gfx_save` over
the menu's rect, restores it on dismiss, **and repaints instead when the claim
is refused**. `menu_save_kb` is the sizing, and it is already adapter-correct
— it rounds x to byte columns, adds the misalignment byte, and multiplies by
`[vid_planes_w]`.

Costed with that formula, for the **content rect only** (the chrome should be
redrawn normally, because a raise changes the title bar's pinstripes anyway):

| | Note Pad's default window | a window grown full screen |
|---|---|---|
| CGA 640x200 | ~6 KB | 16 KB |
| Hercules 720x348 | ~6 KB | 31 KB |
| VGA 640x480 (4 planes) | ~21 KB | 152 KB |

And the time, against the 578 ms it replaces: `gfx_save` is a byte copy, so
~5 KB is on the order of **10 ms** to bank and 10 ms to restore on the mono
adapters, ~40 ms on VGA. **A raise becomes ~20 ms of blitting instead of 578
ms of glyphs**, which is comfortably inside the 100 ms budget and would be the
first interaction in §7's table to reach it.

Note what the full-screen VGA figure is: **152 KB, which is §32's back buffer
to within a rounding**. That is the number `bb_canfit` already refuses on a
small machine, so we know both that it is affordable on a 640 KB box and that
it must be refusable.

---

## 3. Discardable claims — yes, and the period name for it is *purgeable*

The proposal is right and it has good precedent in exactly this era: Windows
3.x had `GMEM_DISCARDABLE`, and the Macintosh Memory Manager — which this OS
is a homage to — had **purgeable handles**, `HPurge`/`HNoPurge`, where the
Memory Manager would drop a purgeable block under pressure and the
application checked and called `ReallocateHandle`. Using the Mac's name and
shape would be in keeping and would make the contract familiar.

**A raise-cache is the ideal first consumer**, and for a reason worth stating
plainly: *its fallback is not merely designed, it is the code that ships
today.* If the image is gone, `wm_raise` does what it does now. There is no
new failure path to write, test or get wrong.

### 3.1 The one hard problem: a discarded claim leaves a dangling segment

A package keeps its claim's segment in its own bss. Discard it and that word
names memory somebody else now owns, and the package writes through it. That
is cross-instance corruption — the worst failure class here, because it
presents as a bug in the victim.

Three ways out, in increasing safety and cost:

1. **The owner must ask** (`OSAPI_MEM_VALID`) before each use. Cheap, and easy
   to forget once — after which the failure is silent and remote.
2. **Hand back a handle, not a segment**, resolved per use. Safe, but it is a
   new ABI shape for every consumer and a call in every inner loop.
3. **Do not let packages have them yet.** Make discardable a *kernel-side*
   class first, prove the mechanism on kernel caches, and publish a package
   ABI only once the shape is known.

**Recommend (3) for the first cut**, because the kernel already has three
caches whose "you cannot have it" path is written and tested:

- `MEM_K_ASC` — a volume's `ASSOC.DAT` cache (§54.7), already "reloaded on a
  volume switch".
- `MEM_K_FATW` — a volume's private FAT window (§18.8.1), already gated on
  128 KB of free heap and already documented as "a refused claim just shares".
- `MEM_K_SAVE` — the menu save-under, already "the restore repaints instead".

Every one of those is a block the kernel can lose without telling anybody,
because losing it is a state the code already handles. That makes the first
implementation almost entirely about the allocator and almost not at all about
notification.

### 3.2 Prefer shed-and-retry to discard-and-notify

The notification protocol is where this gets expensive, and the first cut can
skip it entirely.

`mem_claim` currently fails when no hole fits, and every caller already has a
refusal path. So: **on failure, shed discardable claims oldest-first and retry
once.** No callback, no ordering question, no "when is the owner told" — the
owner is told by the block being absent the next time it looks, which is
exactly what those three caches already cope with.

That also side-steps a real concurrency trap. A discard is only safe when the
owner is not mid-use, and `mem_claim` is called from the loader, from `kmain`,
from drivers and from window callbacks — some holding the gfx lock and some
not. With shed-and-retry over kernel caches, the rule is simply that a
discardable kernel cache is re-derived at its next use, which is already true
of all three.

When packages do get this, the rule to publish is: **a discardable claim may
only be touched under the gfx lock.** That is what makes a discard safe
against a worker, because the lock is what already serialises a worker against
the UI task.

### 3.3 Where they should live in the heap

Data claims grow up from the bottom; regions come down from the top (§50.3),
because a region's base is its CS and can never move — and the rule exists
precisely because "from one end they interleave and a long-lived data claim
mid-heap permanently splits the space a package can load into".

**An earlier draft of this document said to put purgeable claims at the top
end with the regions. That was wrong, and it was wrong for the reason §50.3
was written down.** A purgeable block in the region arena is indistinguishable
from a region to the placement scan while it is live, so it splits exactly the
run a package needs — and shed-and-retry does not save it, because that fires
only on *failure*. A region that still fits, but fits worse, has been
fragmented without anything noticing. The saving grace, that a purgeable block
can always be removed, only pays out when something has already been refused.

The deciding question is **which allocation a cache should be traded against**,
and the answer is: whichever one is short of the free run in the middle. Both
arenas grow toward that one run, so it is the thing everything actually
competes for.

**So: purgeable claims are HIGHEST-FIT WITHIN THE DATA ARENA.** Not a third
zone, and not the region zone — the top of the arena they already belong to.
That gets three things at once:

- The region arena keeps §50.3's property: everything in it is a region.
- A purgeable block always sits against the free middle, so **shedding it
  always enlarges the one run everything competes for**, rather than punching
  an isolated hole among the data claims.
- Data claims below it are unaffected, and their own churn stays where it was.

`mem_dir` is already a word and already carries 0 = bottom-up, 1 = top-down
(`mem_claim_hi`), so this is a third value on an existing knob rather than a
new mechanism.

Two consequences to write down with it: `mem_claim_hi` must shed-and-retry too
(a package load is a user action, a cache is not, and the cache should lose
every time), and a purgeable claim must not be a `mem_regrow` candidate —
regrowing the block that is meant to be against the free middle is how it ends
up in the middle of the arena instead.

Compaction stays out of scope, as agreed. This is the cheap way to need it
later rather than sooner.

### 3.4 What the Task Manager should say

Out of the bar and out of the used total. **And the reason is stronger than
"do not hide things from the user": from the user's point of view it is not
taken at all.** They can have it back the instant anything needs it, so
counting it as used would be the misleading choice, not the honest one — a
full bar that is not full is worse than a line nobody reads.

**One `Purgeable 21K` line all the same, and it is for US.** It is a developer
instrument: it is how you see that a save-under was claimed rather than
refused, that a shed actually happened, and that a cache is not quietly being
re-claimed on every pass. Nobody needs to act on it and the number moving is
not a problem — which is exactly why it belongs on its own line and not in the
bar. §41.6's XMS line is the precedent for a figure that is in neither map.

### 3.5 Cost of the mechanism itself

`MC_SIZE` is 8 (`MC_SEG`, `MC_PARA`, `MC_OWN`, `MC_DMA`). A flags word takes
the record to 10 and the table from 256 to 320 bytes of `.bss` — **64 bytes
against `KERN_BUDGET`**, which currently has 1,536 spare. A spare bit in
`MC_OWN` would cost nothing, but the owner word is compared whole in several
places and a masked compare is a trap for later; take the 64 bytes.

---

## 4. Recommendations, in the order worth doing them

**BUILT: 1, 2, and 3's placement.** The opt-in in 1 was dropped and then put
back: `WF_SAVEU` / `OSAPI_WM_SAVEU` (0x0340), for SPEC.md §11.96.1's reason.
Dropping it answered the MEMORY question ("purgeable memory is not spent, so
there is nothing to opt out of") and missed the CORRECTNESS one, which is what
the opt-in was really carrying — `wm_clip_set` catches a covered window that
DRAWS, and not one whose content changes without being drawn, which is exactly
what §11.3's background painters do when they skip on invisibility.

**UNVERIFIED and the first thing to check:** `wm_hide` drops the cache, and the
test for it was inconclusive — a minimize-and-restore reported no `W_PAINT`,
which is either the hook failing or the scripted click missing the minimize
box, and the harness cannot tell those apart. It is safe to ship as it stands
because the only window that opts in is Note Pad, whose promise holds across a
hide too (its worker's two background drawers ask `OSAPI_WM_OBSCURED` and its
state is not time-varying). **Verify it before a second application opts in** —
a Timer or a Bounce is exactly the case that would break. What is measured:
a raise makes **zero `W_PAINT` calls** where it used to make one costing
578 ms, and the restored content is **pixel-identical** (0 differing of a
124,928-byte content rect). The kernel cost is **+809 bytes**, which crossed
two 512-byte rungs and left `KERN_BUDGET` at 1,536 spare.

**Owed on it:** a clean end-to-end wall figure for the raise. The obvious
measurement — step frames until the screen stops changing — is confounded by
Note Pad's worker waking every `NP_WTICKS` ticks and drawing, so it reports
when the WORKER settled and not when the raise did. Bracket it kernel-side
instead (a breakpoint either end of `wm_raise`), and A/B it by patching the
`call wm_su_try` to three NOPs in the running guest, which needs no rebuild.
The first attempt at that patch used the listing's `.text`-relative offset and
hit the wrong instruction — the address to write is the one `os88marty read`
confirms holds `E8`.

1. **Per-window save-under for a raise, opt-in via a window flag** — the
   biggest single latency left, ~578 ms → ~20 ms, using `gfx_save`,
   `menu_save_kb`'s sizing and `MEM_K_SAVE`'s refusal shape, all of which
   exist. Invalidated at `wm_clip_set`, on move/resize, and on `vid_switch`.
   Un-minimizing from the dock is the same path (`wm_show`) and comes free.
2. **Discardable as a kernel-side claim class, with shed-and-retry** — no
   notification protocol, first consumers the three caches that already
   survive not existing, plus (1)'s save-under, which is the perfect fit
   because its fallback is the shipping code path.
3. **Task Manager: out of the bar and out of the total, one `Purgeable` line**
   — a developer instrument, because from the user's side it is not taken.
4. **Allocate purgeable HIGHEST-FIT WITHIN THE DATA ARENA** (§3.3), never in
   the region arena, so a shed always enlarges the free middle and §50.3's
   "everything in the region zone is a region" survives. `mem_claim_hi` sheds
   and retries too; purgeable claims never `mem_regrow`.
5. **A package-visible discardable ABI — later, and only after (2) has run.**
   Handle-based, gfx-lock-only, and worth it mainly for Paint's undo image and
   Tracker's sample buffers, which are the two large app allocations that
   could honestly be re-derived or reloaded.
6. **Heap compaction — not yet**, and (4) is what buys the time.

### Carried over from docs/NOTEPAD-NOTES.md §7

7. **Bound pass 1 for a caret move** (NOTEPAD-NOTES §7.1) — ~155 ms → ~6 ms on an arrow key.
   No character moved, so only the row the caret left and the one it arrived
   on can differ, and `np_move` knows both. The care needed is that pass 1
   also sets `[np_rowsn]`, so a shorter walk pushes traffic onto §27.13's
   index; that is probably fine now the index exists, but it wants its own A/B.
8. **The scroll's blit and `np_sbar`** (NOTEPAD-NOTES §7.2) — ~186 ms for a one-row scroll,
   of which the lettering is near its floor and the full-bar redraw for a
   one-pixel thumb move is not. Needs breakpoints of their own before
   attributing further (NOTEPAD-NOTES §6.5).
9. **`NP_HCHUNK` = 4 is still an emulator number** (NOTEPAD-NOTES §5.4) — it sizes a
   gfx-lock hold and a duty cycle, both of which the operator feels, and only
   the 5150 can set it. `make npbench` reports it.
10. **NOTEPAD-NOTES §5.2.1's 705 stale pixels** — pre-existing, reproducible in one run with
    `tools/notepad`, and the suspicion is an interleaving between the
    background count and the redraw's signatures rather than anything about
    seeding.
11. **`[np_rowsn]` is not capped to the array it indexes** (NOTEPAD-NOTES §5.3.1) — latent,
    documented, and wants its meaning settled across its four readers before
    a clamp is added.
