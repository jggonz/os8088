# How Byteopoly was built

A diary kept while the work happened, for later reuse. It separates what was
observed from what was intended, and names what was actually tested. The
plan is [plans/BYTEOPOLY-PLAN.md](plans/BYTEOPOLY-PLAN.md); the contract is
SPEC.md §131.

## The brief, 2026-11-17

The owner handed over a build brief and five concept screenshots: the board
at rest with a purchase prompt, a live auction for the Commodore 64, a trade
offer from George to Ada, the cable lobby with four seats split two and two,
and the "cable link lost" pause with its retained action and last paired
save. The brief's requirements: a complete property-trading game in C, for
2-4 players on one machine or two machines over a null modem, on an XT, with
saves that survive and a link that recovers.

## Reading the OS first

Before any design, the existing facilities:

- **The C toolchain** (§73, `docs/C-TOOLCHAIN.md`): one translation unit,
  four refusals, a 65,024-byte ceiling on image plus bss, and one overlay
  segment of code for anything infrequent.
- **Breadboard XT** (§130) is the newest C package and the closest shape: an
  eager overlay part, a lazy art part, and a band-composing drawing core in
  assembly that C calls. Byteopoly shares that core as source rather than
  writing another. Its one Breadboard-specific tail (an alert bridge that
  calls back into Breadboard's C) is now behind `%ifndef BX_CORE_ONLY`.
- **The serial lease** (§115) and `apps/os88cable.inc`: the null-modem
  framer written once for two-machine packages (Wire Arena uses it). Its
  frames carry at most 32 bytes, so Byteopoly's protocol has to be compact.

## Decisions taken at the foundation

- **Lockstep replay under a banker.** The PRNG state is part of the canonical
  match, so the client can apply the host's committed command with the same
  engine and reach the same state; the result carries the dice/card and a
  digest to prove it. A command is a ~20-byte record, which fits the cable's
  32-byte frames; a whole-state snapshot is only the recovery path.
- **The board runs clockwise on the glass**, BOOT bottom-left, REPAIR SHOP
  top-left, SWAP MEET top-right, CRASH! bottom-right: the brief's "clockwise
  ring" and the screenshots' corners, over one sentence of the brief that
  listed the sides in the other order.
- **The package file is `BYTEOPLY.O88`** (an 8.3 stem) in
  `apps/byteopoly/`; the label is "BYTEOPOLY".

## Observed: the skeleton boots (2026-11-17)

The foundation package (window, menus, About, an overlay function, the art
part, the band core) built at 14,102 bytes of image and 3,254 of bss, and
opened on MartyPC's VGA XT showing the 28 labelled cells. The menu bar
showed four of the five menus: "Help" did not fit beside "Collection" and
"Options". That is the panels lane's to resolve.

## Integration, first half (2026-11-17)

Art, save, rules and face landed on `feat/byteopoly` in that order, each
followed by a full `make`, the host harness and a size check.

- **Art** came back once. Its first BPART.BIN was 61,239 bytes unpacked,
  and a part is one segment, so the lead capped it at 56 KB. Re-cutting the
  LARGE property sprites at 48x32 instead of 64x40 brought it to 52,033
  bytes (16,180 lz4). Three codex master sheets were cut by
  `tools/os88bpart.py draft` and finished by hand. The dice, Workshop,
  coins, keycap, cable and card frames were redrawn from scratch. 29
  monochrome twins exist where the core's 2x2 patterns turn a colour sprite
  to mush.
- **Rules** measured 184,112 host checks under ASan/UBSan. They include 21
  scripted complete games, with conservation, ownership, even-build and
  invariant checks at every step. `tests/bpmath` showed the checked 32-bit
  arithmetic passing 36 of 36 on MartyPC's 8088.
- **Save** wrote an independent Python reader (`tools/bpsave.py`) from the
  SPEC tables. It agrees with the C codec on all 126 verdicts. Validation
  disagreed with the engine on three resting states: auction masks, the
  auction space id, and the decider once the game is over. Rules found them
  by running its scripted games through save's validator, and save fixed
  them before landing.
- **Face** measured strips per update on MartyPC with a counter. A selection
  move repaints 4 strips on VGA, a log line 1, and a token move 6. The
  centre is the dominant cost of a commit.
- **The segment balance moved twice.** The overlay looked full when face put
  everything in it. Then resident looked full when link moved code back. The
  rule that settled it: only what an ordinary turn touches stays resident.
  After face, the package is 45,499 of 65,024 resident bytes, and the three
  lanes still to land project to about 1.4 KB spare.

## Integration, second half (2026-11-17)

Panels, app and link landed after face, then each of panels and app took a
short follow-up round for the cable-only pieces. Those pieces needed link's
functions first: "Use host" after a divergence, paired-save progress, the
lobby's Ready column and the remote-seat checks.

- **Link** played the cable game on two QEMU guests, joined by a relay that
  drops, duplicates, corrupts and delays frames and pulls the cable. The
  lane brief allowed three runs.
  - Run 1 failed at the reopen: the script did not answer "Leave the cable
    game?".
  - Run 2 failed after a correct Resume Pair: the script pressed End Turn
    with a doubles roll owed.
  - Run 3 passed end to end, playing to revision 77 with the same digest
    on both machines at every revision.

  The real engine also exposed a deadlock in the host harness. A client's
  move crossing the host's paired save left each side waiting on the
  other. The client now answers the save with its move still outstanding
  (SPEC 131.7.5).
- **Validation and the engine** were made to agree in both directions.
  `t_rules` now runs save's `bp_validate` on every resting state of its 21
  scripted games, which comes to 190,820 checks. Running the real engine
  through it found five rules in save's validator that would have refused
  legal states: the auction masks, the auction space id, the decider once
  the game is over, a pending Repair Shop move, and the bytes after a
  name's NUL.
- **Sizes, all lanes in:** image 54,656, bss 8,527, that is 63,183 of
  65,024 resident bytes. The overlay is 58,684 bytes and BPART.BIN 52,033
  bytes unpacked. BYTEOPLY.O88 packs to 78,452 bytes, 77 clusters, so it
  rides its own disk rather than `games360.img`.
- **Rows.** `byteopolyhost` (the host harness, 9.5 s, which a salary of 210
  turns red) and `byteopolylink` (the QEMU pair) are registered.

## The QA run and the fix round (2026-11-17)

Two Opus testers ran the one QA pass on MartyPC. qa1 played complete local
games on the VGA XT; qa2 checked every screen on CGA and lone Hercules and
compared the art with the concept screenshots. Neither edited source. They
found 33 defects between them. Every rule they checked by hand held: rent
cases, even building, mortgage and redeem, all four Repair Shop exits,
atomic trades, settle, bankruptcy to a player and to the bank, and the
Classic, Quick and shared-win scores. Save and load also held, including
damaged and truncated files.

- **The blocker** was found by both testers within minutes. A key that
  changed only a panel's own state (focus, the bid editor, a handoff)
  repainted nothing, because the face's section signatures cannot see panel
  state. The screen said "Bid 10" while Enter bid 40. It was hotfixed in the
  middle of the pass (one change in `os88_onkey`/`os88_onclick`) so the rest
  of QA could trust the glass, and both testers re-checked it.
- **The XT cost** was the finding that mattered most, and only the 4.77 MHz
  machine could show it. Every switch between the bottom bar and a centre
  panel repainted the WHOLE window, at 7.5-10.4 s of guest time, so a
  two-player turn spent about 25 s repainting. The face had treated a change
  of `bp_pn_covers()` as a reason to repaint everything. The fix round split
  it in two. The face repaints the centre alone, draws no background under a
  panel, and gets `bp_face_dirty_rect` for a damaged band. The panels name
  the band a key changed. Measured again on the XT:

  | action | before | after |
  |---|---|---|
  | End Turn | 7.4 s | 2.1 s |
  | handoff acknowledged | 8.8 s | 2.7 s |
  | Collection open | 7.9 s | 1.5 s |
  | Tab | 2.7 s | 0.6 s |
  | a bid step | 2.3 s | 1.4 s |
- **A held Enter acted for the next player.** The panels lane's clock-only
  freshness rule counted from a key's arrival, and typematic repeats had
  queued behind an 8-second repaint. The rule now counts 1.1 s of quiet
  from the END of the previous key's handling, repaint included. The OS key
  map would also have worked, but arming it makes arrows step twice under
  QEMU for the whole session, and `kern_small` refuses it.
- **Focus carried across panels.** The debt panel opened focused on Bankrupt
  and its confirmation on Yes, so two Enters went bankrupt. Focus now
  resets to the first enabled button, and a confirmation opens on No.
- **The re-check** (a third tester, on the fixed head) passed every priority
  row on the glass and found two small regressions of the fix round. The
  composer's band missed a line that moved when a refusal appeared, and
  focus could be left on a greyed button after a passed auction. Both were
  fixed the same afternoon.

What stayed unfixed: "!" and "Y" are hard to read in the 8-pixel strike
(they look like "I" and "T"), because that font is WIN16's, shared as
source. A Rules page still takes about 4 s to turn on the XT.

The fan-out's own lesson was that the stall watchdog went blind. Lane
looks had left about 30 MartyPC instances running inside the worktrees,
and the watchdog counts a process in a worktree as an agent at work. One
lane sat 45 minutes after a green build with its fixes uncommitted before
the lead noticed by hand.

## The uplift

*Started 2026-11-17, the day the game merged. The story list is
[BYTEOPOLY-UPLIFT-PLAN.md](plans/BYTEOPOLY-UPLIFT-PLAN.md).*

The owner put the merged game beside the five concept boards it had been
built from and asked for the boards' look: "a high detail pass ... like a
professional, high detail game like the design", with the 3D feel of the
renders, play running clockwise with BOOT's arrow pointing the way, prices
that rise around the board, a soundtrack and effects, and - in their words -
"a re-usable drawing library for the C toolchain that we can leverage for
future games as well".

**What stood in the way was memory, not drawing.** At the merge the package's
segment had 1,113 bytes spare, the overlay 2,085 and the art part 1.8 KB. A
richer face drawn as C would not fit, so the plan pays in this order: the UI
strings move out of the segment into text records in a second art part; the
static layout of a screen becomes DATA (a "scene" record drawn by one
interpreter) instead of code and literals; the widgets are written once in
assembly as the game kit (SPEC.md 133), which any later C game can include.
