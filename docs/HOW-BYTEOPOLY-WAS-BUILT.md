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
