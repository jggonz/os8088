# Byteopoly — delivery plan

Collect vintage computers, trade them, and outlast your friends — on one XT
or on two joined by a null-modem cable.

Byteopoly is a native os8088 C package (`apps/byteopoly/`, `BYTEOPLY.O88`).
The binding contract is SPEC.md §131; the shared C contract is
`apps/byteopoly/bp.h`. The build diary is
[HOW-BYTEOPOLY-WAS-BUILT.md](../HOW-BYTEOPOLY-WAS-BUILT.md). The owner's
brief is the story source; its five concept screenshots are the visual
reference (kept outside the repository; the art lane records what it took from them in `apps/byteopoly/art/PROMPT.md`).

Status letters: **-** not started, **B** built on a lane branch,
**I** integrated on `feat/byteopoly`, **S** seen working in the QA run in
emulation (with the evidence named), **X** deferred with a reason.

## Lanes and ownership

Built in parallel by lane agents from one shared brief (story-fanout), each
in its own worktree, merged into `feat/byteopoly` as it finishes. One QA run
in emulation follows integration; then the fix round; then the final pass.

| lane | owns | SPEC |
|---|---|---|
| lead | `bp.h`, `byteoply.c`, `byteoply.asm`, `hosttest/bphost.h`, Makefile rules, §131.1-131.3, 131.11-131.12, this plan, the diary | 131.1-3, 131.11-12 |
| rules | `bp_math.c` `bp_board.c` `bp_cards.c` `bp_rules.c` `hosttest/t_rules.c` | 131.4, 131.4.1 |
| save | `bp_codec.c` `bp_save.c` `hosttest/t_save.c` | 131.7 (file half) |
| link | `bp_link.c` `bp_cable.inc` `hosttest/t_link.c`, the pair launcher | 131.5, 131.6, 131.7 (protocol half) |
| face | `bp_face.c` | 131.8 |
| panels | `bp_panels.c` (the in-game panels) | 131.9.7-131.9.11 |
| app | `bp_app.c` `bp_setup.c` (callbacks, setup, lobby, menus, files, sound) | 131.9.1-131.9.6 |
| art | `art.h` `art/` `icon.inc` `tools/os88bpart.py` | 131.10 |

## Stories

| story | what the user gets | lanes | status |
|---|---|---|---|
| US-01 | Launch from the desktop: window, icon, About, focus, close, two independent instances, lease released on close | lead, panels | I |
| US-02 | Set up 2-4 seats on one PC: names, distinct tokens, Classic/Quick, roll-off, handoff needing fresh input | panels, rules | I |
| US-03 | Read the board: 28 cells and hit targets, details pane with full rent schedule, active seat/cash/decision/dice/round/link, stacked tokens | face | I |
| US-04 | Roll and move: recorded dice, salary, doubles, third double, board wrap, wrong-seat/phase refusals, fixed-seed fixtures | rules, panels | I |
| US-05 | Buy a computer: purchase panel, single debit, decline -> auction | rules, panels | I |
| US-06 | Rent and charges: set/upgrade/mortgage/utility rent, payer/recipient/calculation shown, debt on shortfall | rules, panels | I |
| US-07 | Auctions: order, minimum, increments, pass, budget, out-of-turn handoff, no timeout | rules, panels | I |
| US-08 | Upgrades: set, even build/sell, RAM chips/Workshop markers visible on mono | rules, face, panels | I |
| US-09 | Mortgage and redeem: proceeds/interest shown, rent suppression, set effects | rules, panels | I |
| US-10 | Floppy Find / System Error: all 16 cards, held disks, bounded chains | rules, panels | I |
| US-11 | Repair Shop: visiting vs detained, pay/disk/doubles, third failure debt and pending move | rules, panels | I |
| US-12 | Trades: compose, freeze, recipient-only accept, revalidate, atomic | rules, panels | I |
| US-13 | Debt and bankruptcy: shortfall screen, liquidation, estate to bank or player, elimination | rules, panels | I |
| US-14 | Finish: last survivor, Quick Match score breakdown and shared wins, Review/New/Exit | rules, panels | I |
| US-15 | Save and resume a local game from every pending decision; bad files refused, prior save kept | save, panels | I |
| US-16 | Connect two PCs: Host/Join, search/peer/compatible/ready/started, port conflicts, every seat split | link, panels | I |
| US-17 | Synchronized play: banker authority, machine-gated input, matching digests | link | I |
| US-18 | Survive a pulled cable: pause, resume the pending transaction exactly once | link | I |
| US-19 | Paired saves: synchronized checkpoint, compatible pair, resume after reopening both | link, save, panels | I |
| US-20 | Chat and link diagnostics | link, panels | I |
| US-21 | Keyboard and mouse: shortcuts, focus order, fresh input after handoff | panels, face | I |
| US-22 | CGA and Hercules: usable layouts, no hue-only meaning, mixed-adapter pairs | face, art | I |
| US-23 | Distinctive computer art: 16 computers, tokens, dice, markers, corners, mono variants | art | I |
| US-24 | Restrained sound and animation through the system's routing; mute; no idle repaint | panels | I |
| US-25 | XT memory and latency budgets, measured | all, lead | - |
| US-26 | Verified under faults in paired emulators; complete local and cable games | link, lead | I (host t_link; byteopolylink passed on QEMU in the lane) |
| US-27 | Maintainable, reproducible C build; module map; host harness | lead, all | I |

## Foundation (lead) — done

- [x] Isolated worktree `~/Repos/os8088-byteopoly`, branch `feat/byteopoly`
  from `origin/main` at `7c3c60ab3`.
- [x] SPEC.md §131 with the binding choices and each lane's reserved
  subsection.
- [x] `bp.h`: types, capacities, the canonical state, commands, results and
  every subsystem's interface; stub bodies so each lane builds alone.
- [x] Board and card tables (`bp_board.c`, `bp_cards.c`).
- [x] Skeleton package: window, menus, About, overlay, art part, the shared
  band core; `make byteopoly`, `byteopolydisk`, `byteopolyhost`,
  `byteopoly-pair` rules. Booted on MartyPC's VGA XT: the ring draws.

## Distribution

Byteopoly ships on its own disk in all four geometries (`make
byteopolydisk`: `byteopoly.img`, `byteopoly720.img`, `byteopoly120.img`,
`byteopoly360.img`) and on the live media. It does not join
`games360.img`: BYTEOPLY.O88 is 78,452 bytes packed (77 clusters), and that
disk has 67 of 354 clusters free (measured 2026-11-17).

## QA list

The QA run in emulation (2026-11-17) was ONE pass by two testers on
MartyPC, on the integrated head `acdc76270` and then the hotfix `cdae5a1f5`:
- **qa1:** VGA on the XT, playing complete local games;
- **qa2:** CGA and lone Hercules, every screen, and the art against the
  concept screenshots.

The full reports and screenshots are in the lead's log directory
(`qa1-report.md`, `qa2-report.md`); they are not committed. Every rule
checked by hand held: rent cases, even building, mortgage and redeem, all
four Repair Shop exits, atomic trades, settle, bankruptcy to a player and to
the bank, and the Classic and Quick scores including a shared win. Save and
load, including damaged and truncated files, also passed.

Fix groups are file-disjoint. F-face is `bp_face.c`, F-panels is
`bp_panels.c` (plus `os88_onkey`/`os88_onclick` in `bp_app.c`), F-app is
`bp_app.c`/`bp_setup.c` and F-art is `art/`.

| id | sev | what was seen | adapter | group | status |
|---|---|---|---|---|---|
| Qqa1-1 = Qqa2-1 | blocker | a panel-only key or click repainted nothing: the bid committed 40 while the screen said 10, and a dismissed handoff stayed up | all | lead | hotfixed cdae5a1f5, re-checked PASS on VGA and CGA |
| Qqa1-4 | major | every bar/panel switch repaints the whole window, 7.5-10.4 s on the XT, and each panel key repaints the whole centre (Tab 1.85 s, bid +/- 2.2 s) | XT | F-face + F-panels (`bp_face_dirty_rect`) | fixed, re-checked PASS (qa3) |
| Qqa1-3 (Q-pre-5) | major | a held Enter on End Turn confirmed the next player's handoff, rolled, acked a card and rolled again | all | F-panels | fixed, re-checked PASS (qa3) |
| Qqa1-2 = Qqa2-4 | major | focus carries between panels: DEBT opens on Bankrupt and its confirm on Yes, so two Enters bankrupt; Esc out of Rules then Enter opens Trade | all | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-2 | major | MANAGE/AFTER and Review bar status text, black on a grey dither, illegible | CGA, Herc | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-3 | major | trade composer cursor and active column shown by yellow alone | CGA, Herc | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-5 | major | Rules paging on MID steps 13 lines and shows 9; it runs into the Keys text and ends on a blank page | Herc | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-9 = Qqa1-5 | major | every roll-off roll is logged as "X goes first", with no dice | all | F-app | fixed, re-checked PASS (qa3) |
| Qqa2-6 | minor | seat 2's black token vanishes on the inverted active row | CGA, Herc | F-face | fixed, re-checked PASS (qa3) |
| Qqa2-7 | minor | the COMPACT link row (round, dice) is stale after a roll: BP_D_LINK is not in the commit mask | CGA | F-app | fixed, re-checked PASS (qa3) |
| Qqa2-8 | minor | the MID mortgage mark spills into the next cell and the hint bar | Herc | F-face | fixed, re-checked PASS (qa3) |
| Qqa2-12, -13, -15 | cosmetic | COMPACT letter chip abuts the name, the "M" is clipped; on MID the owner mark and a token look the same | CGA, Herc | F-face | fixed, re-checked PASS (qa3) |
| Q-pre-4, Qqa1-10 | cosmetic | "POWE"/"E-WAST" truncated in cells; "Round 11" shown after a 10-round match | all | F-face | fixed; truncation re-checked PASS (qa3), "Round 11" not re-checked |
| Qqa2-21 | minor | no picture for the corners and specials on MID cells, and no deck art in the MID card panel | Herc | F-art (TINY specials) + F-face (cells) + F-panels (card) | fixed, re-checked PASS (qa3) |
| Qqa2-14, Qqa1-10 | minor | "[K] Yes, bankru": the label is cut, also in the hint | CGA, VGA | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-22 | cosmetic | COMPACT lists show 3 rows with no "more" mark | CGA | F-panels | fixed, re-checked PASS (qa3) |
| Qqa2-23 | minor | the hint lists "Space Roll" while Roll is greyed | all | F-panels | fixed, re-checked PASS (qa3) |
| Qqa1-1 rest | minor | menu commands that open a panel (Help > Keys, Assets > Standings, Load) do not dirty the centre | all | F-app | fixed, re-checked PASS (qa3) |
| Qqa1-6 | minor | after a Load the handoff keeps the old name and the first Enter is ignored | VGA | F-panels | fixed (host/inspection), not re-checked on the glass |
| Qqa2-10, -11, -18 | minor | the DEFAULT tab is barely marked on 1bpp; "Classic" reads "lassic"; "+" is not disabled at 4 players | CGA, Herc | F-app | fixed, re-checked PASS (qa3) |
| Qqa2-17 | minor | menu greying: Propose is enabled in BUY, after the end and in setup; Keys is greyed before a match | all | F-app | fixed, re-checked PASS (qa3) |
| Qqa1-8 | minor | a default name blocks typing until it is backspaced | all | F-app | fixed, re-checked PASS (qa3) |
| Qqa1-9 | minor | the log omits rent arithmetic, mortgaged no-rent and the third-double reason; New Game and Load keep old lines; the refusal text is generic | all | F-app | fixed (host/inspection), not re-checked on the glass |
| Q-pre-2, Q-pre-3 = Qqa1-7, Qqa2-19 | minor | help text: "jail"; the Keys page is wrong or incomplete (C vs M, no F1/P/R/S/K/U/Home/PgUp, "Esc opens the menu", "arrows move between buttons") | all | F-art | fixed, re-checked PASS (qa3) |
| Qqa2-20 | minor | four pairs of computers near-identical: Apple II/IIe, XT/AT, Amiga/ST, 386/486 | all | F-art | fixed, re-checked PASS (qa3) |
| Qqa2-16 | cosmetic | "!" reads like "I" in the 8-px font (CRASHI) | all | - | not fixed: the WIN16 strike is shared source |
| Q-pre-1 | ? | stale "rev"/"Sent - waiting" text in the cable game | cable | final pass (byteopolylink) | not checked: needs a pair |
| Qqa3-1 | minor | trade composer: after a refusal the incremental repaint erased the balance line (and half the cash line on Herc) | all | F-panels | open |
| Qqa3-2 | minor | double, auction, everyone passes: the bar returns focused on the greyed End Turn | Herc | F-panels | open |
| Qqa3-3 | cosmetic | upper-case Y reads as T in the 8-px font (DUTT, FLOPPT) | all | - | not fixed: the WIN16 strike is shared source |
| Qqa3-4 | cosmetic | the MAC's TINY mono twin does not read as a computer | Herc | F-art | open |

After the fix round (qa3, VGA XT): End Turn 2.07 s (was 7.5), Collection open 1.48 s
(was 9.4), Tab 0.60 s (was 1.85); a Rules page turn is still about 4 s. Not seen
on the glass: nine of the sixteen cards, the refusal to trade an upgraded set from
the accept side, a failed save keeping the previous one, and Fast Animation
(the host harness covers the rules). Measured on the XT (MartyPC guest cycles): launch 15.9 s to a settled picture,
188 KB held by a running instance, idle repaint none, idle tone none.

Leads to check on the glass, carried from the lanes' reports:
- Q-pre-1 (link): after a commit the screen showed "rev 35" at revision 37,
  and "Sent - waiting" with nothing outstanding, while both machines'
  records agreed - a stale redraw in the face or panels?
- Q-pre-2 (art): help page heading "Corners, cards, jail" - the game says
  Repair Shop and detained, never jail.
- Q-pre-3 (help vs panels): the Keys page says C shows the collection and M
  manages; the panels use M (and C) for the one Collection panel; F1 Rules is
  not on the Keys page.
- Q-pre-4 (face): CGA COMPACT truncates POWER and E-WASTE in the cell.
- Q-pre-5 (panels): a key held past ~1 s across a handoff could still reach
  the next player (clock-only freshness rule).
- Q-pre-6 (panels): paint cost per strip on the XT with several bp_can calls.

## Evidence

(Filled as stories reach S.)
