# WireArena: capture the flag, teams and side spawns

**STATUS: PLAN** (2026-11-15). Written to the owner's brief below, before
any code. Section 9 lists the defaults this plan chose where the brief left
a choice; each is one constant or one line of data to change.

The owner, 2026-11-15:

> *"Modes: Duel and free-for-all are basically what we have already (duel
> is 1 other bot, ffa is the main mode now, unless I'm misunderstanding what
> those mean). Add team deathmatch to the list, and both it and CTF will
> need spawn rules (you spawn on your "side" only). Then lets implement CTF
> next. The renderer part may need buffers moved into heap claims; if doing
> so is cheap in performance, or otherwise slimmed down in a way that is
> cheap in performance."*

WireArena is `apps/wirearena/`. Its plans: the parent
[MULTIPLAYER-FPS-PLAN](MULTIPLAYER-FPS-PLAN.md); the bots
[WIREARENA-AI-PLAN](WIREARENA-AI-PLAN.md) (1.1 lists the modes); the link
[WIREARENA-LINK-PLAN](WIREARENA-LINK-PLAN.md) and Ethernet
[WIREARENA-NET-PLAN](WIREARENA-NET-PLAN.md); the arenas
[WIREARENA-ARENAS-PLAN](WIREARENA-ARENAS-PLAN.md) and
[WIREARENA-HEDGES-PLAN](WIREARENA-HEDGES-PLAN.md); the segments
[WIREARENA-PARTS-PLAN](WIREARENA-PARTS-PLAN.md).

## 1. The modes

| mode | what it is | where it stands |
|---|---|---|
| free-for-all | every player and bot for themselves | **the game today** |
| duel | free-for-all with one other: one bot, or one linked player | **the game today**, with the setup's bots at one |
| team deathmatch | two teams; a kill scores for the killer's team | after CTF: it is CTF's teams and side spawns with kills for points (wave C4) |
| capture the flag | two teams, a flag each; take theirs to yours | **this plan** |
| last stand | (AI-PLAN 1) | later |

## 2. The rules of capture the flag

- **Two teams, RED and BLUE.** A player's or bot's team is its SLOT's
  parity: slots 0 and 2 red, 1 and 3 blue. Slot 0 is the host (or the one
  player of a match with no link); a bot takes the slot it plays in today.
  So one bot makes 1 against 1, two make the player and the second bot
  against the first, three make 2 against 2.
- **Each team has a FLAG at its BASE.** Touching the other team's flag -
  at its base or lying where a carrier dropped it - takes it: you carry it.
- **Touching your own flag where it lies dropped returns it to its base.**
  At its base, touching it does nothing.
- **A capture**: reach your own base carrying their flag while your own
  flag is at its base. Your team scores 1; their flag goes back to its base.
- **A carrier that dies drops the flag** where it died, at its feet's
  height. A dropped flag no one touches goes home by itself after 30 s.
- **The first team to 3 captures wins.** The board shows the winner for
  5 s; then the captures, the flags and every player start again (the kills
  and deaths on Tab go on).
- **No friendly fire** in a team mode: a hitscan, a projectile or a splash
  does nothing to a teammate (the shooter's own splash still hurts them, as
  today). A bot never picks a teammate as its target.
- **Spawns on your own side** (the owner's rule, for team deathmatch too):
  a respawn point belongs to a side or to neither; in a team mode a player
  or bot respawns only at its own side's points, choosing among them as
  today (the farthest from the nearest foe). Free-for-all uses every point.

## 3. The arena's data (tools/arenagen.py)

- **`ARENA_FLAGS[tag]`**: the two bases, red then blue, `(x, y)` each, on
  the floor. An arena without it cannot be played as CTF (section 7: the
  setup says so). Refused: a base in a wall or off the floor lines, or not
  reachable by the route graph.
- **A respawn point's side**: the third byte of its record (its pad today)
  - 0 neither, 1 red, 2 blue. `ARENA_RESPAWNS` takes an optional fourth
  element. A team-mode arena must give each side at least two points.
- **The bases' route nodes**, as a pickup's (the nearest node within reach,
  else refused), so a bot can head for a base.
- All of it rides the arena part's graph block, after the bench's views:
  a count (0 or 2), then per base x, y and its node.

The arenas that get bases (owner to confirm, section 9):
- **the Twin**: its two flag places, (192, 1024) red and (1856, 1024) blue
  - the west square red, the east blue, the respawns sided to match;
- **the hedge**: the west room's platform red, the east room's blue, the
  respawns on each half sided to match; the south room and the court are
  neither side's.

## 4. Drawing (the renderer part)

- **A flag** is a THING, as a pickup is (`wa_thing`): a tall narrow block
  (half width 10, from 40 to 104 above its z), in its team's colour - red
  `0Ch`, blue `09h`; on mono, your own team's normal and theirs bright.
  The game writes two flag records into FRAME each frame (x, y, z, from,
  to, attribute, shown); the renderer draws them after the pickups. A
  carried flag rides above its carrier (its z the carrier's floor + 64, its
  span over the head); the flag the player carries is not drawn - the HUD
  says so.
- **Teams' figures**: in a team mode a figure is drawn in its team's
  colour (red `0Ch`, blue `09h`) where today it is white; on mono, foes
  bright and teammates normal. A hit's flash turns WHITE in a team mode, so
  it shows against either colour; the firing cue stays yellow. FRAME
  carries an attribute byte per figure (0: as today).
- **Forecast**: about 150 bytes of renderer code and 23 of FRAME; a frame
  pays two `wa_thing` calls where a flag is in view (pickups measure ~700
  clk each: WEAPONS-PLAN) and a compare a figure. The renderer part has
  2,791 bytes left (62,233 of 65,024), so CTF does not need buffers moved.
  Section 8 says what moving them would cost, for when something does.

## 5. The HUD and the messages (the game's overlay)

- **The flag line**, top row, centred, only in CTF: each team's captures
  and its flag's state - `RED 1 #  BLUE 0 !` - `#` at home, `!` carried,
  `_` dropped, each in its team's colour.
- **Messages**, centred, 3 s, as LINK LOST is drawn: `YOU HAVE THE FLAG`,
  `RED HAS YOUR FLAG` / `YOUR FLAG IS DROPPED` / `YOUR FLAG IS HOME`,
  `RED SCORES`, `RED WINS`.
- **Tab's board** in a team mode: the players by team, each team's line
  headed with its captures (or, in team deathmatch, its kills).
- Sounds: a take, a capture and a return, each one OSAPI_SND_TONE, as a
  pickup's.

## 6. The bots (AI-PLAN's states, one goal more)

A bot already FETCHes a pickup: it walks the route graph's next hops to a
node. In CTF, where a bot picks its next goal (the end of a leg), in order:

1. **carrying their flag**: FETCH its own base's node;
2. **its own flag dropped**: FETCH the node nearest it (the nearest node by
   the arena's table, AI-PLAN 3);
3. **their flag not carried by a teammate**: FETCH their base's node, or
   the node nearest where it lies;
4. otherwise as today (roam, hunt).

Seeing a FOE still turns it to ENGAGE, as today; a teammate is never a
target. The flags' touches are the rules' (section 2), the same for a bot
as for a player: a bot at the node walks within reach.

## 7. The setup and the link

- **The setup's Mode**: a drop-down, `Free-for-all` / `Capture the flag`
  (`Team deathmatch` with wave C4), beside the arena. CTF with an arena
  that has no bases is refused at Play with a line in the window: *"This
  arena has no flags: pick the Twin or the hedge"*.
- **Linked**: the mode is the host's, as the arena is - HELLO's byte 17
  (reserved today) carries it, and a guest echoes it. Old builds send 0:
  free-for-all.
- **THE HOST RUNS THE RULES.** It already has every position: its own
  player's, each remote player's from their SNAPs, and its bots'. Each
  tick it applies section 2 to all of them and sends a **FLAG** event (a
  new EVENT kind) on each change: flag (0 red, 1 blue), state (home,
  carried by slot n, dropped), x, y, z / 2, and that team's captures. A
  guest applies the event and draws what it says; it runs no rule. A
  guest's touch is seen by the host one SNAP later (a third of a second at
  most), which is the price of one machine deciding.
- **Budget**: a FLAG event is a frame of 16 bytes, a few a minute. The
  link's 240 bytes a second has ~30 to spare (LINK-PLAN 2).

## 8. Memory, and what could be moved

**Forecast for CTF**: renderer part ~170 bytes (2,791 free); the game
~2.5 KB of code and ~100 of BSS (about 20 KB free in its segment: image
29,831 and BSS ~15,000 of 65,536).

**If the renderer part ever needs room** (the owner's note): its five
screen-sized buffers - `wa_stamp`, `wa_cur`, `wa_lista`, `wa_listb`,
`wa_fbuf`, 3,840 bytes each at 80 columns, 19,200 in all - and `wa_rcp`
(5,764) are its largest tables. Moved into a heap claim, every access
needs a segment that is not DS. `wa_lista`/`wa_listb` are the cheapest to
move: they are written through ES with `stosw` already, and read once a
frame in `wa_present`. `wa_stamp` and `wa_cur` are read and written in
`WPLOT`, every plotted cell; an override there costs a byte and ~2 clk a
cell. Not done here, because CTF fits; measured when something needs it.

## 9. Defaults chosen, for the owner

| choice | default | change by |
|---|---|---|
| teams | slot parity: 0, 2 red; 1, 3 blue | `wa_team` |
| captures to win | 3 | a constant |
| a dropped flag's return | 30 s | a constant |
| after a win | 5 s of the board, then the round again | a constant |
| friendly fire | off | - |
| the arenas with bases | the Twin and the hedge | `ARENA_FLAGS` |
| the hedge's bases | the west room red, the east room blue | `ARENA_FLAGS` |
| the flag line | top row, centred | the overlay |

## 10. Waves

Each ends with its outcome on the glass, the rows that cover it named; the
soak rows and `test-full` run once after the last (CLAUDE.md).

- [ ] **C1 Teams, flags and the rules, one machine.** arenagen's bases,
  sides and their nodes; the Mode drop-down; teams; side spawns; no
  friendly fire; figures and flags in team colours; take, carry, drop,
  return, capture, win; the flag line and the messages.
  *Outcome:* a CTF match against bots that do not yet play for the flag:
  take theirs, bring it home, score; die carrying it and see it drop.
  *Checked by:* a new soak row `wactf`, off the game's variables: a take,
  a drop on death, a return by touch and by time, a capture only with your
  own flag home, the win at 3, side spawns, no damage between teammates.
- [ ] **C2 The bots play for the flag** (section 6).
  *Outcome:* bots take your flag, carry it home and score; they return
  their own.
  *Checked by:* `wactf`: in N minutes of bots only, a capture by each
  team.
- [ ] **C3 Linked.** The mode in HELLO; the host's FLAG events; a guest
  draws them.
  *Checked by:* `walink`'s pattern - two machines, a guest's take and
  capture seen on both.
- [ ] **C4 Team deathmatch.** The mode; a kill scores for the killer's
  team; the win at a kill count.

## 11. As built

(Filled in as each wave lands.)
