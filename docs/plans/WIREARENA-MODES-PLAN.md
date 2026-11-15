# WireArena: the score to win, team deathmatch, king of the hill, and arenas in rotation

**STATUS: BUILT** (2026-11-15): M1 to M4. Written to the owner's
brief below, before any code; section 8 is the waves and section 9 what was
built.

The owner, 2026-11-15, after capture the flag
([WIREARENA-CTF-PLAN](WIREARENA-CTF-PLAN.md)):

> *"CTF plays amazingly well. Lets make a Score field (or Victory, or
> Points, or find a better label?) for match setup so they can configure
> the score needed to win. Kills for ffa/tdm, flag captures for ctf.
> Points for the upcoming king of the hill.*
>
> *Team Deathmatch: The next thing to add. Should be easy given its
> basically CTF without the flags part.*
>
> *King of the Hill mode: This will need map types the opposite of CTF;
> one central point (hedge works as both at least). Run over a flag, and
> turn it to your color. Gain points for every 10 seconds the flag stays
> your color.*
>
> *Map picking: When selecting a mode, limit the arena list to only those
> that support that type of play.*
>
> *Arena list: Add a selection to the bottom of arena that opens a sub
> window that lets you add arenas to a list, default is all arenas for the
> picked type. Then, on match victory, after 10s, go to the next map in
> the rotation instead of the menu.*
>
> *Renderer space: The other session declined needing the space, but its
> now down to like 700b with their work. Likely up to you this round, if
> you need that space."*

## 1. The modes, and what each counts

| mode | teams | spawns | a point is | the arenas |
|---|---|---|---|---|
| free-for-all | none | anywhere | a kill | all |
| team deathmatch | red, blue (CTF-PLAN 2) | own side | a kill, for the killer's team | those with sides |
| capture the flag | red, blue | own side | a capture | those with bases |
| king of the hill | red, blue | own side where the arena has sides, else anywhere | 10 s of the hill held | those with a hill |

Teams, side spawns and no friendly fire are CTF's (CTF-PLAN 2), for every
team mode.

## 2. "Win at" - the score to win

**The label is `Win at`**: its items then read as a sentence - *Win at: 3
captures*, *Win at: 20 kills*, *Win at: 10 points*. The items are the
mode's:

| mode | items | default |
|---|---|---|
| free-for-all | no limit, 5, 10, 15, 20, 25, 30 kills | 20 kills |
| team deathmatch | no limit, 10, 15, 20, 25, 30, 50 kills | 30 kills |
| capture the flag | no limit, 1, 2, 3, 5, 7, 10 captures | 3 captures |
| king of the hill | no limit, 3, 5, 10, 15, 20, 30 points | 10 points |

Each mode keeps its own pick. *No limit* plays as the game always has: on
until Esc.

**The win**: the first player (free-for-all) or team to the score. The
winner is shown - `YOU WIN`, `COG WINS`, `RED WINS` - for **10 s**, and then
the next arena of the rotation starts (section 5).

## 3. Team deathmatch

CTF with no flags: the teams, side spawns, no friendly fire; a team's score
is its players' kills as the board counts them - so a suicide is a kill
less, as in free-for-all; the HUD's top line is each team's kills
(`RED 12  BLUE 9`). Bots play it as free-for-all against
their foes.

## 4. King of the hill

- **The hill**: one point an arena may have (`ARENA_HILL`, on a route node
  a bot can reach). Its flag stands there, white - no one's.
- **Touching it** turns it to the toucher's team's colour, whatever colour
  it was.
- **Holding**: every 10 s the hill stays one team's colour, that team
  scores a point.
- **The HUD**: each team's points, and the hill's owner.
- **The bots**: a bot whose team does not hold the hill heads for it (CTF's
  FETCH, CTF-PLAN 6); a bot whose team holds it roams and fights near it.
- **The arenas**: the hedge, the heights arena (its central platform), the
  Twin (the path's bottom). On the hedge the hill is the court's floor at
  the stack's south foot, not the stack's top as first planned: no route
  climbs the stack, so a bot could never take a hill there (section 9).

## 5. Arenas by mode, and the rotation

- **The Arena list shows only the mode's arenas** (section 1's column), in
  the setup's order. Changing the mode to one the current arena cannot
  play picks the mode's first arena.
- **Its last item, `Rotation...`, opens a window** listing the mode's
  arenas, each with a check box, all checked by default. Unchecking one
  takes it out; at least one stays checked. Each mode keeps its own.
- **After a win's 10 s** the match goes on in the next checked arena after
  this one, round, in the list's order - the scores anew, everyone on
  their side. With one arena checked, it is that arena again.
- **Linked**, the host's rotation: it says which arena is next, and every
  machine loads it and starts again together.

**Loading in the match**: an arena is a lazy part (PARTS-PLAN 7.5) read by
the file slots, which SPEC.md 53.7 allows inside the FSX bracket - so the
next arena loads without going back to the desktop. The screen holds the
win's picture while it reads.

## 6. The data

- `tools/arenagen.py`: each arena's modes as a bitmask in a table the
  setup reads without loading the arena (`wa_amodes`): free-for-all
  always; team deathmatch where each side has two respawn points; CTF
  where it has bases; king of the hill where it has a hill.
- The hill: after the bases in the graph block, a count (0 or 1), its
  x, y, z and node.

## 7. Memory

The renderer part has 1,202 bytes free (63,822 of 65,024). King of the
hill's flag is CTF's flag record with a third colour; no other renderer
code is needed, so nothing moves this round. The game segment has ~18 KB.

## 8. Waves

- [x] **M1 Win at, team deathmatch, arenas by mode, the rotation's
  default, the next arena** - alone. Checked by `wamodes` (soak): each
  mode's list of arenas, a win at the score in free-for-all, team
  deathmatch and CTF, the 10 s, the next arena loaded and played.
- [x] **M2 King of the hill** - alone, the bots too. Checked by `wamodes`.
- [x] **M3 The rotation's window.** Checked by `wamodes`.
- [x] **M4 Linked**: the win and the next arena from the host; team
  deathmatch's and king of the hill's scores on the wire. Checked by
  `wactflink`'s pattern.

## 9. As built

**M1** (2026-11-15):

- **The setup** (`apps/wirearena/wasetup.inc`): Mode's items are
  free-for-all, team deathmatch, capture flag (`wa_smodes` maps the list to
  `wa_gmode`'s numbers). `Win at` is the seventh drop-down, right of Mode;
  the nick moved under Mode, with Play and Keys beside it. **The window grew
  from 338 to 402 wide**: "team deathmatch" and "10 captures" did not fit
  the old drop-downs (seen on the glass, VGA and CGA); the lobby's rows
  widen with it (`WL_COLS` is derived from the margin now). Each mode keeps
  its Win at pick (`wa_sgoal`), the values in `wa_sgval`.
- **Arenas by mode**: `tools/arenagen.py` writes each arena's modes as
  `WA_AM<n>` (`arena_modes`: free-for-all always; CTF with bases; team
  deathmatch with two respawns a side; king of the hill with a hill), the
  setup's `wa_amodes`. `wa_sarlist` builds the Arena list from them in the
  setup's order on every change of mode (and a guest's on the host's
  HELLO), and moves the arena to the mode's first when it cannot play.
- **The game** (`apps/wirearena/wactf.inc`): `wa_ctfstart` takes the mode
  only where the arena plays it, and Win at's score as `wa_ggoal`. Team
  deathmatch is CTF's teams, side spawns and team colours with the flags
  off; its score (`wc_score`, a tick, alone) sums each team's kills off the
  board. A win shows for 10 s (`WC_WINT` 300) - `YOU WIN`, `<NAME> WINS`,
  `RED WINS` - and then `wc_next` loads the next arena of the mode in the
  setup's order, inside the bracket (SPEC.md 53.7's file slots), and starts
  it with the scores anew and the bots' names kept. An arena that cannot be
  read ends the match; the toast shows once the desktop is back.
- **Linked** is unchanged until M4: CTF's win starts the round again on the
  same arena; free-for-all and team deathmatch have no win linked yet.
- **Checked by** `wamodes` (soak, 12 s), and `wactf`'s win now waits for
  the hedge after the Twin. Red: see the row.

**M2** (2026-11-15):

- **The hill** (`tools/arenagen.py`'s `ARENA_HILL`, `hill()`): a place, its
  floor and two route nodes whose leg passes within reach of it (one node
  where the hill stands on it), after the flag bases in the graph block;
  the game's `wa_ghlp`. The heights: its central platform (1248, 1024, 80),
  at node 1. The hedge: (1024, 832), the court's floor at the stack's
  south foot, node 1 - the stack has no route, so it is the high ground
  over the hill rather than the hill. The Twin: (1024, 832), the path's
  bottom, between nodes 17 and 18 (both lanes within reach).
- **The rules** (`wc_hill`, a tick, alone): one team alone on the hill and
  not its holder takes it (`WE HAVE THE HILL` / `THEY HAVE THE HILL`); both
  teams on it leave it as it is; its holders score a point each 10 s held
  (`WC_HOLD`), and Win at's wins as in the other modes. The hill is flag
  0's record (`WC_ST` the holder, `WC_TMR` its time), drawn white while
  nobody's and in its holders' colour after - no renderer code.
- **Spawns**: a team mode spawns on the sides only where the arena has
  them (`wa_gside`) - the heights has none, so king of the hill there
  spawns as free-for-all does.
- **The bots** (`wc_hgoal`): a bot whose team does not hold the hill, and
  is not fighting, FETCHes its node, or from one node to the other across
  it. Measured on the hedge, two bots and us idle: without it the hill
  stood a minute untaken; with it both teams took it within 29 to 50 s.
- **The HUD**: each team's points, `#` by the holders.
- **Linked** is M4's: a guest plays king of the hill's teams and spawns, but
  the hill's rules are not on the wire yet.

**M3** (2026-11-15):

- **The Arena list's last item is `rotation...`** (`wa_sarlist`, its arena
  0FFh); picking it opens the Rotation window and leaves the arena as it
  was. **R** opens it from the keyboard too; Left and Right on the Arena go
  round the arenas alone, so stepping through the list never pops a
  window.
- **The window** (`wasetup.inc`'s `wa_ropen` .. `wa_rset`) is a window of
  its own with its own class, os88ui's check boxes (`OS88UI_CHK`, SPEC.md
  13.15): the mode's name, then a box per arena of the mode. A click or
  Space toggles; Up and Down move the `>` marker; Esc, Enter or the close
  box put it away and give the setup its keys back. The last box checked
  cannot be unchecked (a toast: `One arena stays in`). Changing the mode
  closes it; so does closing WireArena.
- **The rotation** is `wa_srot`, a bit per arena by mode, all set at first.
  `wc_next` takes the next arena of the mode that is in it, round; with one
  checked it is that one again.
- **Checked by** `wamodes`.

**M4** (2026-11-15):

- **Win at on the wire**: the HELLO's byte 14 is the difficulty (bits 0-1),
  Win at's pick for the mode (bits 2-4) and the mode (bits 5-6); a guest
  takes the host's pick into its own table, so its setup shows the host's.
- **The host's rules for every mode**: free-for-all's and team
  deathmatch's scores (off the board every machine shares) and king of
  the hill's hill run on the host as alone. A **GAME** event (`EV_GAME`,
  5) carries the teams' scores, the win (1 + the winner's slot, or team)
  and the hill's holder whenever one changes (`wc_gsend`); a guest shows
  them (`wc_gin`) - the same messages, its own `YOU WIN` when the slot is
  its own. CTF keeps its FLAG events.
- **The next arena**: when the win's 10 s are over the host queues a
  **NEXT** event (`EV_NEXT`, 6) with the arena - a full queue waits a tick -
  and goes on to it (`wc_go`); a guest goes on to it as it applies the
  event, so every event after it is the new arena's. `wa_lkround` is the
  link's half: the board anew, the lifts, the absent parked, our player
  respawned, the host's pickups ROLLed one a tick as the queue has room
  (`wc_rolls`) and the guest's waiting for them. The event queue and its
  sequence carry on: nothing of the link restarts. A win is held off for
  5 s in a new arena (`wc_grace`), while a peer still on the old one may
  send its old board's rows.
- **Checked by** `wamodeslink` (soak, QEMU: king of the hill's hill, a
  point, the win and the next arena on both machines). Free-for-all's and
  team deathmatch's linked wins share the path but are not driven by a
  row of their own.
