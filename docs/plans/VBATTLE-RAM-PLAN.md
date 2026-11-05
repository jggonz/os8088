# VBATTLE-RAM — what a battle holds in 640 KB, and when it reads

**Status: PLANNED (2026-11-05). Nothing below is built; section 2 is the
memory as it stands, measured.** The owner, after the Hercules rounds:
*"We should probably take a close look at the entire ram layout, and when we
decide to both load and preload. For example, we never need to preload a
disabled/unreachable action's files, and right now switching between two
actions causes a noticeable delay ... We can probably make the interface part
more responsive by changing when we delay to after a selection, so the user
can use the interface freely, and then the longer waits (when needed) are
hidden in the delay AFTER they make the selection."*

SPEC.md 125.4 is the reader and the rings as built, 125.4.3 the builder's
reader's model (the burst cap) and 125.4.4 the map below as a fact.
docs/plans/VBATTLE-MEMORY-PLAN.md is the earlier round (streaming loops,
XMS/EMS holds); this one is about **conventional memory on the owner's 5150**
and the reading policy, which every machine shares.

## 1. Why it matters: the rings are the burst buffer

A clip streams through one of two **clip rings**. What a ring holds ahead of
the clip's sound is all there is to ride out a burst - Bolt's first 1.5 s at
129 KB/s against a 74 KB/s average, Search's seconds 4-6 at 131 (125.4.3) -
and the builder's cap (`CAP_HOLD`, 56 KB) is sized from it. **Every KB the
rings gain is a KB of burst the builder no longer has to cut**, on every
adapter. Today the owner's 100% Hercules battle gets **88 KB a ring**; the
plan below takes it to **~170**.

## 2. The map today (measured)

MartyPC's Hercules 5150, 640 KB (`os8088_5150_herc_hdd_sb_gla`; the owner's
86Box profile is the same machine: 256 KB + an AST SixPakPlus's 384), the
owner's bundle played to its first menu, the heap read off the machine
(`tools/heapmap.py`'s `Map`) and the engine's own claim split by its offsets
(`vb_psfx` ... `vb_pbband`):

| | 100% Hercules (`BATTLEH100`) | 75% (`BATTLEH75`) |
|---|---|---|
| below the arena: kernel, its low tables, the boot-loaded drivers | 94 KB | 94 KB |
| the desktop's caches: the directory read-ahead (`MEM_P_DIRW`, 31.5), the icon store (`MEM_P_ICO`, 5), a window's raise cache (`MEM_P_WSAVE`, 8) | **44.5 KB** | **44.5 KB** |
| a 2 KB kernel claim, a driver image (8.5 KB) | 10.5 KB | 10.5 KB |
| VBATTLE.O88: code and bss | 40.5 KB | 40.5 KB |
| **the engine's block** | **196 KB** | **156 KB** |
| ...its tables | 4 | 4 |
| ...the menu's sounds | 12 | 12 |
| ...the music's ring (32 KB + a piece) | 36 | 36 |
| ...**the entries** (every action's first frame, from each of its anchors) | **84** | **48** |
| ...the idle loop | 20 | 16 |
| ...the band's pictures | 8 | 8 |
| ...the band's two buffers (16 KB each) | 32 | 32 |
| **the two clip rings** (each + its 16 KB guard) | **208 KB: 88 a ring** | **248 KB: 108 a ring** |
| the card's ring (page-safe, from the top) | 17 KB | 17 KB |
| free, in two pieces (largest 15) | 30 KB | 30 KB |

Three things in it are waste, and one is the size of a ring:

1. **The caches are stranded under the block.** The engine's claims are
   PINNED and the block is claimed first, so it lands on the first free
   paragraph - just above the caches - and the 44.5 KB they hold is a pocket
   no ring can reach. `OSAPI_MEM_AVAIL` counts a cache as free (SPEC.md
   50.6.3) but a ring is one claim, and the pocket is not contiguous with
   anything after the block lands.
2. **30 KB is left over in two pieces**: `VB_RKEEP`'s 8 KB, the rounding of
   two rings to whole 4 KB, and the gap the card's top-down ring leaves.
3. **The band's buffers are 16 KB each and the band is 58 rows**: 58 x 80
   = 4.6 KB is all either holds since the partial redraw (125.5) cut the
   copy and the clear to the band's rows.
4. **The entries are 84 KB of the block** - every action's first frame from
   every anchor, held from the play's start to its end, of which one is
   drawn at a time.

## 3. The waves

### R1 - the claims (engine only; no format change)

- **The band's buffers at the band's size**: `(200 - band row) x 80`,
  4 KB-rounded - 2 x 5 KB on every adapter at the default band, where it
  is 2 x 16. **+11 KB a ring.**
- **One claim for the block and the rings together**, sized from
  `OSAPI_MEM_AVAIL` at `MEM_LVL_TOP` less the card's ring, made BEFORE the
  card's: the claim path sheds and packs the caches to serve it (SPEC.md
  50.6.3, 66.4), so the 44.5 KB pocket and the 30 KB of pieces come back
  as ring. The directory read-ahead is the dearest cache in the system
  (SPEC.md 50.6.6), but the battle reads one file through its own cursors
  (`RG_CUR`, `vb_curm`) and walks no directory while it plays - and the
  window rebuilds it when the play ends. **+~37 KB a ring.**
- `VB_RKEEP` is re-justified or dropped: what in the play claims after
  the rings? (The stream's records are the driver's own; the bracket claims
  nothing.) Measured, not assumed.

**R1 alone: 88 -> ~136 KB a ring (100%), 108 -> ~156 (75%).**

### R2 - the entries leave the block (format 8: builder and engine)

An entry is drawn only as its clip starts, and only the one for the anchor
the idle loop reaches. So a clip's entries go **in front of its own records
on the disk**, and the head read that already happens before an action may
start (`VB_HEAD`, 40 KB) brings them into the clip's ring with the records.
The anchor table keeps (anchor, offset) per clip, the offset now into the
clip's region rather than into the ENTRIES; `vb_lanchor`/`vb_anchor` choose
as now and decode the entry from the ring.

- The head grows by the clip's entries (one per anchor: 1-5 KB each at 100%
  Hercules, so 5-20 KB a clip) - `VB_HEAD` counts records AFTER them.
- A clip whose anchor comes before its head is in waits as it does today;
  the reader's priorities do not change.
- Bundles of format 7 are refused with "rebuild it" (the builder makes 8).
- The builder's memory line drops the entries; its budget reports each
  clip's entries with its head.

**R2: the block loses 84 KB (100%) / 48 (75%): +42 / +24 KB a ring. With
R1: ~178 / ~180 KB a ring** - about double today's at 100%.

### R3 - when the reader reads: the interface first, the waits after a choice

What the reader does today at a menu (SPEC.md 125.4's steps), and what
changes:

| step | today | planned |
|---|---|---|
| 1 the music, under 3/4 | a WHOLE read (up to 32 KB, ~0.3 s on an ST-225) - the round that made the music hold, and the one most likely to make a key wait | at a menu: whole only when under HALF; between half and 3/4, 8 KB a call. A key waits for one read at most, so a read is kept to what a key can wait for |
| 2 a playing ring under half | whole | whole (a stream running low is the one thing worth a key's wait) |
| 6 the clip under the cursor | its head, once the keys have been quiet 6 ticks (`VB_KQUIET`) | **only if the action can be TAKEN** (`can_pay`: its MP, its item, its limit; not a dead target's), and only after the cursor has rested ~0.5 s - passing over an item reads nothing |
| 7 the enemy's | the clip of its likeliest move | the same, after step 6 |
| a choice made | the pending action's head, before anything | the same - and this is where the waits belong: the menu closes and the message shows AT ONCE, the head is read in the pause before the clip, which is the pause the owner asked for |

And two things to stop doing:

- **Evicting a ring for a prefetch that may not be used.** Today a ring
  holding the last hovered clip is given to the next hovered one. Planned:
  a ring holding something the reader may want next (the enemy's clip,
  the other hovered item) is kept, and a prefetch takes a free ring or none.
- **Reading the unreachable at all**: a disabled item (no MP, no potions),
  a sub-menu's items while its parent is closed, the enemy's moves its
  stance cannot make.

Measured today (MartyPC's Hercules 5150, the owner's 100% battle, ARI's
Magic menu, the cursor between Ice and Bolt, 1.5 s apart): the cursor moved
in **0.05 - 0.45 s** and the menu was drawn by then, no read between - both
heads were held, one a ring. The "noticeable delay" the owner sees is then
one of: a cursor on an item whose head is not held (a 40 KB read in the
menu), or step 1's whole read landing under a key (the change of two rounds
ago). R3 takes both out of the menu; its row measures a key's wait with a
head NOT held and the music low.

### R4 - the builder learns the ring

`CAP_HOLD` is a constant (56 KB) today. With R1-R2 the ring a battle gets
on a 640 KB machine is a function of the bundle (its block) - the builder
already estimates it (`vbtana.memory`'s `ring`) - so the cap holds a clip to
**that ring less the lead's share**, and a battle whose rings come out
small is told so on the Build tab (as the budget's red does today).

## 4. Rows

| wave | row | measures |
|---|---|---|
| R1 | `vbtram` (new, MartyPC's Hercules 5150): the owner-shaped battle to its first menu, the heap read | the rings' KB against today's (88 / 108 on the owner's two), the caches gone during the play and back after it, nothing stranded |
| R2 | `vbtbuild`, `vbtengine`, `vbtadapters` (format 8), `vbtram` | the block without entries; an action from every anchor exact (the engine against `vbtrules.Sim`, unchanged) |
| R3 | `vbtmenu` (new): a key's wait at a menu with a head NOT held and the music under half | under 0.15 s, against today's |
| R4 | `vbtcap` | the cap at the bundle's own ring |

Then the owner's battles again on 86Box's pc5150 (the scratch harness that
found 125.4.3): the late sound and the waits per clip, before and after.

## 5. Not in it

- Hercules' second page (VGA's too): a ring's worth of memory, not ours.
- Moving the band into EMS/XMS: the band is drawn every turn; the caches
  and the entries are the cheap 120 KB.
- A smaller music ring: 3 s is what R3's "under half" needs.
