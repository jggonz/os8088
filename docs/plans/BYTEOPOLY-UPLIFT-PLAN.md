# Byteopoly uplift: the concept boards' look, clockwise play, rising prices, music

The owner's request (2026-11-17): *"a high detail pass UI uplift to Byteopoly
to match more closely the look and feel of the VGA designs I am attaching ...
make the gameplay flow clockwise with the red arrow pointing in the direction
of play, and the price of the properties increase as you go through the board
like the original monopoly gameplay does ... get each part of the UI looking
like a professional, high detail game like the design."* Then: *"You can
build out a re-usable drawing library for the C toolchain that we can
leverage for future games as well"*, *"I like the 3D look and feel of the
designs I provided"* and *"I also want to make sure we have a sound track and
sound effects available."*

The designs are the five concept boards of the original intake
(`~/Repos/byteopoly-intake/concept/`: `1-board.png`, `2-auction.png`,
`3-trade.png`, `4-lobby.png`, `5-linklost.png`; 1448x1086 renders of a
640x480 VGA screen). The contract is SPEC.md 131 (Byteopoly) and the new
SPEC.md 132 (the C game kit). The build diary is
[HOW-BYTEOPOLY-WAS-BUILT.md](../HOW-BYTEOPOLY-WAS-BUILT.md), section *The uplift*.

Status letters: `-` planned, `B` built on its lane branch, `I` integrated on
`feat/byteopoly-uplift`, `S` seen working on the glass in the QA run.

## 1. Constraints that shape the work

Measured at the base (`d242a0e53`): the resident image is 55,306 bytes and
bss 8,605, so **1,113 bytes** of the 65,024-byte segment are spare; the
overlay is 63,451 of 65,536 (**2,085 spare**); `BPART.BIN` is 55,473 of its
56 KB cap. ~7,000 bytes of the resident image are string literals (the
overlay's included: SPEC.md 73.14 leaves literals resident).

So the uplift is paid for in this order:

1. **Strings out**: UI text moves to text records in a second art part,
   read with `bx_artstr` (prep lane).
2. **Decoration as data**: a panel's static layout (boxes, bars, labels,
   pictures, buttons) is a SCENE record in the art part, drawn by one
   resident interpreter (`gk_scene`), not code and literals.
3. **A second art part**, `BPART2.BIN` (lazy part 2, up to 56 KB) for the new
   pictures, scenes, text and music.
4. **Widgets in assembly**: the kit's widgets are shared assembly, smaller
   than the C `bp_w_*` they replace.

Owner decision taken by the lead (reported to the owner): the art parts are
REQUIRED from the uplift on. Byteopoly targets a 640 KB XT, the plain-face
fallback doubles every drawing path, and the UI text now lives in the art.
A machine that cannot load them refuses with a reason (SPEC.md 47), never a
half-drawn face.

## 2. Stories

| id | story | lane | status |
|---|---|---|---|
| U1 | The game kit: 3D boxes, panels, title bars, buttons, key caps, tabs, check boxes, radios, drop-downs, banners, arrows, shadowed sprites, a role table with a mono twin (SPEC.md 132.1-132.5) | kit | - |
| U2 | Scenes: a text source compiled to an art record, drawn by `gk_scene`, hit rects by `gk_scene_rect` (132.6) | kit | - |
| U3 | A second art part, routed by ID in the core (`BX_ART2`), built by `os88bpart.py`; art parts required | prep | I |
| U4 | UI strings move to text records; resident relief measured | prep | I |
| U5 | The sidebar moves to its own file `bp_side.c` (prep); `bp_w_*` drawn by the kit (lead, at integration) | prep, lead | I |
| U6 | New art (art2.h 256..383) drawn from the concept boards in the EGA palette, with mono twins; BOOT's arrows point UP | art | I |
| U7 | Prices rise monotonically along the direction of play (PET 100, VIC-20 120, C64 140); the protocol refuses a peer with the old board | rules | I |
| U8 | Music and effects: a reusable player (FM through SOUND.DRV, speaker fallback), original title, in-game, auction and victory themes, effects for every event; Options > Music | sound | I |
| U9 | The board: ivory 3D cells, collection band on top, bold names, price at the bottom, big corner art, RAM chips on the band, a yellow selection frame; the teal centre with the tilted cards, the pointing mascot, BYTEOPOLY key caps and 3D dice; the hint bar as key caps | board | - |
| U10 | The sidebar: "PLAYER n - YOUR TURN" with the big token and cash well, the roster with tokens and a yellow active row, the details card, YOUR COLLECTION, the connection box with its art, the log well | side | - |
| U11 | The landing (Buy/Auction), auction, trade and link-lost panels as the concept boards draw them | panelsA | - |
| U12 | Every other centre panel in the same kit and style (cards, repair, debt, manage, compose trade, handoff, rules, chat, diagnostics, standings) | panelsB | - |
| U13 | New Game and the cable lobby as the concept board draws them: tabs, the roster table, rules radios, the null-modem box, the mascot | lobby | - |
| U14 | QA in emulation on VGA, Hercules, CGA (and MartyPC for the XT costs), fixes, the final pass | lead | - |

## 3. Lanes, files and budgets

Phase 1 runs kit, prep, art, rules and sound at once. Phase 2 (board, side,
panelsA, panelsB, lobby) starts when kit and prep are integrated, with
byte budgets set from prep's measured relief.

| lane | owns | must not touch |
|---|---|---|
| kit | `apps/cc/gk.inc`, `apps/cc/gk.h` (implements the pinned API), `tools/os88gk.py`, `tests/unit/t_gkscene.py`, SPEC.md 132.1-132.7 and 132.9, `docs/C-TOOLCHAIN.md` (a *Game kit* section) | anything under `apps/byteopoly/` |
| prep | `apps/breadxt/draw.inc` (the `BX_ART2` hook only), `tools/os88bpart.py`, `apps/byteopoly/{byteoply.asm,byteoply.c,bp.h,bp_face.c,bp_side.c,bp_panels.c,bp_setup.c,bp_save.c,bp_link.c,bp_app.c}` for string moves and the split (bp_app.c outside its menus and sound block), `art/tx-*.txt`, Makefile's Byteopoly block, SPEC.md 131.8.1 (split note), 131.10.7, 131.11 | `art/sprites*.txt`, `art2.h` IDs below 460, `bp_board.c`, bp_app.c's menus and sound block |
| art | `art/sprites.txt` (the BOOT arrows only), `art/sprites2.txt`, `art2.h` 256..383, `art/master5.png`, `art/PROMPT.md`, SPEC.md 131.10.8 | code |
| rules | `bp_board.c`, `bp_link.c`'s protocol version constant, `hosttest/t_*.c` expectations, SPEC.md 131.2.1 and 131.5's version line, `docs/BYTEOPOLY-GUIDE.md`'s prices | everything else |
| sound | `apps/cc/gkmus.inc`, `tools/os88gkmus.py`, `art/music.txt`, `art/sfx.txt`, `art2.h` 384..399, `bp_app.c`'s sound block and Options menu (and the Assets -> Collection menu rename), a delimited block in `os88bpart.py`, SPEC.md 132.8 and 131.9.5 | bp_app.c outside those |

Phase 2 ownership is set in its brief.
