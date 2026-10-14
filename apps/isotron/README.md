# isoTron

A light-wall duel for os8088: you against the computer on a raised slab,
each of you trailing a wall you cannot cross. Crash into any wall, or the
slab's outline, and the round is lost. Win two rounds running and the next
arena is stacked one slab higher, with one more computer head to beat; past
four slabs the tower starts again. Three losses in a row end the game.

It is a native 8086 assembly remake of a 1982 IBM PC game, written from the
game's observed rules alone: there is no original code in it, and every
picture it draws is drawn by code.

**Status: wave 1 of 6.** The rules, the computer and the pace are complete
and are checked against a reference model (`tools/isotronsim.py`). What the
window shows for now is a flat top-down view of the arena, one pixel a cell;
the 3D picture, full screen and sound arrive in the next waves. See SPEC.md
section 108 and `docs/plans/ISOTRON-PLAN.md`.

## Keys

| key | does |
|---|---|
| 1, 2, 3 | at the title: a Rookie, Veteran or Lunatic game, with sound |
| A, B, C | ...the same, silent |
| Ins, Left, Z | turn left |
| Del, Right, X | turn right |
| F10, Up | faster (each press, held keys too, up to 9/6 of a cell) |
| F9, Down | back to normal speed |
| P | pause |
| Space, Enter | continue after a round |
| Esc | end the game |

The Game menu starts a game at any tier, pauses and ends the game.

## Build

```
make isotron        # build/isotron.o88
make isotrondisk    # four standalone floppies: 1.44MB, 720KB, 1.2MB, 360KB
make isotrontest    # the host model's selfcheck, then seeded games on MartyPC
```

`ISOTRON.O88` also ships in `GAMES/` on the apps disks (and on
`games360.img` at 360KB). It claims 58 KB of free memory beside its own
~5 KB, and says so in words when the machine has less.
