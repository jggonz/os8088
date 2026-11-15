# WireArena: the score to win, team deathmatch, king of the hill, and arenas in rotation

**STATUS: PLAN** (2026-11-15). Written to the owner's brief below, before
any code; section 8 is the waves and section 9 what was built.

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

CTF with no flags: the teams, side spawns, no friendly fire; a kill scores
1 for the killer's team (a suicide nothing); the HUD's top line is each
team's kills (`RED 12  BLUE 9`). Bots play it as free-for-all against
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
- **The arenas**: the hedge (the stack's top, its highest place), the
  heights arena (its central platform), the Twin (the path's bottom).

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

- [ ] **M1 Win at, team deathmatch, arenas by mode, the rotation's
  default, the next arena** - alone. Checked by `wamodes` (soak): each
  mode's list of arenas, a win at the score in free-for-all, team
  deathmatch and CTF, the 10 s, the next arena loaded and played.
- [ ] **M2 King of the hill** - alone, the bots too. Checked by `wamodes`.
- [ ] **M3 The rotation's window.** Checked by `wamodes`.
- [ ] **M4 Linked**: the win and the next arena from the host; team
  deathmatch's and king of the hill's scores on the wire. Checked by
  `wactflink`'s pattern.

## 9. As built

(Filled in as each wave lands.)
