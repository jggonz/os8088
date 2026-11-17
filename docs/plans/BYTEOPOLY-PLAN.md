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
| US-01 | Launch from the desktop: window, icon, About, focus, close, two independent instances, lease released on close | lead, panels | I (skeleton) |
| US-02 | Set up 2-4 seats on one PC: names, distinct tokens, Classic/Quick, roll-off, handoff needing fresh input | panels, rules | - |
| US-03 | Read the board: 28 cells and hit targets, details pane with full rent schedule, active seat/cash/decision/dice/round/link, stacked tokens | face | B |
| US-04 | Roll and move: recorded dice, salary, doubles, third double, board wrap, wrong-seat/phase refusals, fixed-seed fixtures | rules, panels | - |
| US-05 | Buy a computer: purchase panel, single debit, decline -> auction | rules, panels | - |
| US-06 | Rent and charges: set/upgrade/mortgage/utility rent, payer/recipient/calculation shown, debt on shortfall | rules, panels | - |
| US-07 | Auctions: order, minimum, increments, pass, budget, out-of-turn handoff, no timeout | rules, panels | - |
| US-08 | Upgrades: set, even build/sell, RAM chips/Workshop markers visible on mono | rules, face, panels | - |
| US-09 | Mortgage and redeem: proceeds/interest shown, rent suppression, set effects | rules, panels | - |
| US-10 | Floppy Find / System Error: all 16 cards, held disks, bounded chains | rules, panels | - |
| US-11 | Repair Shop: visiting vs detained, pay/disk/doubles, third failure debt and pending move | rules, panels | - |
| US-12 | Trades: compose, freeze, recipient-only accept, revalidate, atomic | rules, panels | - |
| US-13 | Debt and bankruptcy: shortfall screen, liquidation, estate to bank or player, elimination | rules, panels | - |
| US-14 | Finish: last survivor, Quick Match score breakdown and shared wins, Review/New/Exit | rules, panels | - |
| US-15 | Save and resume a local game from every pending decision; bad files refused, prior save kept | save, panels | - |
| US-16 | Connect two PCs: Host/Join, search/peer/compatible/ready/started, port conflicts, every seat split | link, panels | - |
| US-17 | Synchronized play: banker authority, machine-gated input, matching digests | link | - |
| US-18 | Survive a pulled cable: pause, resume the pending transaction exactly once | link | - |
| US-19 | Paired saves: synchronized checkpoint, compatible pair, resume after reopening both | link, save, panels | - |
| US-20 | Chat and link diagnostics | link, panels | - |
| US-21 | Keyboard and mouse: shortcuts, focus order, fresh input after handoff | panels, face | - |
| US-22 | CGA and Hercules: usable layouts, no hue-only meaning, mixed-adapter pairs | face, art | B |
| US-23 | Distinctive computer art: 16 computers, tokens, dice, markers, corners, mono variants | art | - |
| US-24 | Restrained sound and animation through the system's routing; mute; no idle repaint | panels | - |
| US-25 | XT memory and latency budgets, measured | all, lead | - |
| US-26 | Verified under faults in paired emulators; complete local and cable games | link, lead | - |
| US-27 | Maintainable, reproducible C build; module map; host harness | lead, all | I (skeleton) |

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

## QA list

(Filled by the QA run in emulation once every lane is integrated.)

## Evidence

(Filled as stories reach S.)
