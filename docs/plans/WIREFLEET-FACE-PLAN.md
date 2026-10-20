# WireFleet — automatic link and colour face

The second WireFleet delivery. The first (docs/plans/WIREFLEET-PLAN.md) made
a correct game; this one makes it easy to start and pleasant to look at.
The binding contract is SPEC.md §114.9 (automatic link), §114.10 (the colour
face) and the §115.1 amendment (moving a lease between ports). The diary
continues in [HOW-WIREFLEET-WAS-BUILT.md](../HOW-WIREFLEET-WAS-BUILT.md).

The owner's brief, 2026-10-20:

1. Make the UI more user-friendly, and colourful on screens that can show
   colour, after a supplied mock-up (a navy title bar with a big caption, a
   "Your ships" card with pictures and check boxes, a notice strip, two
   framed boards, a legend with a Fire button, a chat panel and a status
   bar). Extend the `vga-face` skill with flat interface elements.
2. Link the two PCs automatically, the way WireBall's Serial Match does, as
   soon as each player is ready, with the Link menu kept as the fallback. On
   two 86Box XTs the players could not find each other.

## What the first delivery got wrong about linking

Read before designing, 2026-10-20:

- **Nothing linked until a menu was used.** Link → Connect COM1/COM2 was the
  only way to open the port; the Ready message pointed at the menu, and
  COM1 is the serial mouse on most machines, so the first choice offered is
  the one that is refused.
- **A new fleet after connecting stranded the other PC.** Game → New fleet
  (N) gives the captain a fresh identity, and the peer, still bound to the
  old one, refuses every frame from the new one while the new one refuses
  every frame addressed to the old one. Neither side can recover without
  the other also pressing N - which reads exactly as "they cannot find each
  other". Nothing is at stake at turn 0, so §114.9 lets a peer rebind there.
- **The 86Box pair itself was not reproduced here.** This host has no
  accessibility grant, so no 86Box window can be driven; the QEMU two-PC
  harness links. What is fixed is what the code shows, and the PR says so.

## Delivery stories

Each wave is a commit pushed to one draft PR. Boxes are ticked only for
behaviour that works and was checked; the evidence is named beside it.

### Wave 1 — automatic link

- [x] **Opening the game starts looking.** With no menu use, each PC leases
  a free COM port as the window opens and says which one in the status line.
  On a machine with the serial mouse on one port the other is the only
  candidate.
- [x] **Two free ports are both tried.** With no serial mouse (PS/2, USB,
  VMware pointer) the search alternates between COM1 and COM2 on a jittered
  dwell, without unloading SERIAL.DRV between them, and stays on the first
  port a valid WireFleet frame arrives on.
- [x] **Refusals are waited out, in words.** An unsettled mouse ("move the
  mouse once"), another program holding the port, and a missing SERIAL.DRV
  each get their own line; the first two retry by themselves.
- [x] **The battle starts by itself.** When both captains are Ready and the
  link is up the first captain may fire at once; the status line says
  "Opponent ready" or "Opponent is placing ships" while waiting.
- [x] **A relaunched or renewed opponent is found again.** Before the first
  shot is committed, a HELLO from a new identity rebinds the match. After a
  finished match, the opponent's new match is announced.
- [x] **The Link menu is the fallback.** Automatic (the default), Connect
  COM1, Connect COM2, Disconnect. A manual port pins the search to it;
  Disconnect stops it until Automatic or Connect is chosen.
- [x] **The 86Box pair launcher says the right thing**: no menu step.

### Wave 2 — the flat vocabulary

- [x] **A reusable flat-UI include** (`apps/os88flat.inc`): a title bar with
  a double-size caption in one blit, a notice strip with a coloured status
  lamp, a status bar, a flat framed card with a title, a flat button painter
  for `OS88UI_OWN` (filled, darker border, pressed/greyed states, primary and
  accent colours), and a check box. Every element draws each pixel once.
- [x] **The gate**: the colour face runs on a 4bpp display whose content box
  is tall enough (VGA, and EGA at 16-pixel cells); Hercules, CGA and the
  CGA fullscreen bracket keep their faces exactly.

### Wave 3 — deployment, in colour

- [x] Navy title bar "DEPLOY YOUR FLEET", the board in a framed card with
  its coordinates, and a "Your ships" card: each ship's picture, name and
  length, a check box when placed, the selected row highlighted, "n / 5
  placed", Rotate / Auto-place / Clear and a large Ready.
- [x] A hint line under the board, the chat entry with a Send button, and a
  status bar naming the port and the opponent's state.

### Wave 4 — battle, in colour

- [x] Title "YOUR TURN" / "OPPONENT'S TURN" / "VICTORY!" / "DEFEAT" with the
  turn count, a notice strip for link news, two framed boards, a legend
  (hit, miss, sunk), a Fire button that names its target, the last two
  events, and a chat panel with three lines, an entry and Send.

### Wave 5 — the skill, the contract, the evidence

- [x] `vga-face` gains the flat vocabulary and what this face learned.
- [x] SPEC §114 and the diary describe what shipped; screenshots in the PR.
- [x] Final pass: the deduplicated soak union of the affected rows once,
  then `make test-full` once (CLAUDE.md, Testing).

## Found on the way (fixed)

- F hung a VGA or Hercules desktop: `OSAPI_WM_DISPLAY` answers the height in
  BX and `OSAPI_FULLSCREEN` was handed it as the window.
- Typed chat was never drawn (`os88line_key` edits; drawing is the caller's).
- The Hercules enemy board at x 326 left 22 stray pixels a cursor move; at
  328 none. Why an unaligned `BLIT1` lights pixels past its width is left
  open as a kernel question.
- The ship atlas was cut in equal fifths and every hull carried its
  predecessor's keel.
- A full battle repaint took 3.9 s on a 4.77 MHz VGA XT; 1.39 s now.

## Rows this touches

`wirefleetart`, `wirefleetdeploy`, `wirefleetlink`, `wirefleetsystem`,
the new `wfdraw` and `wfdrawherc`, `kernmods` (SERIAL.DRV) and the WireBall
rows that share `tests/wireball.py`'s machine (`wireball`, `wireballmono`).
Named here, run once at the end.
