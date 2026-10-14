# isoTron

A light-wall duel for os8088: you against the computer on a raised slab,
each of you trailing a wall you cannot cross. Crash into any wall, or the
slab's outline, and the round is lost. Win two rounds running and the next
arena is stacked one slab higher, with one more computer head to beat; past
four slabs the tower starts again. Three losses in a row end the game.

It is a native 8086 assembly remake of a 1982 IBM PC game, written from the
game's observed rules alone: there is no original code in it, and every
picture it draws is drawn by code.

**Status: wave 4 of 6.** The rules, the computer and the pace are complete
and are checked against a reference model (`tools/isotronsim.py`). The
window shows the arena in 3D - the slab, the tower and every wall, one
column a step - on VGA and EGA in 16 colours and on CGA and Hercules in
black-and-white patterns, the picture held pixel for pixel to the model's.
**Full screen** (F or Alt+Enter, or Game > Full Screen) plays the same game
in colour wherever the card has it: the 1982 look in CGA 320 x 200 with
palette 1, sixteen colours on a VGA (320 x 200) and on an EGA (its own
640 x 350), and black-and-white patterns at 640 x 300 on a Hercules.
**The flow** is the whole game's: a title with the word isoTron in raised
block letters, the crash and its score, a wait for Space, and every new
arena SWEPT onto the screen the way the original drew it - rims and faces,
the front face and the floor left to right, the outline last - in about a
second and a half, the tower rising and starting again through the same
sweep; three losses end the game on its own line. The sounds arrive in the
next wave.
See SPEC.md section 108 and `docs/plans/ISOTRON-PLAN.md`.

## Keys

| key | does |
|---|---|
| 1, 2, 3 | at the title: a Rookie, Veteran or Lunatic game, with sound |
| A, B, C | ...the same, silent |
| Ins, Left, Z | turn left |
| Del, Right, X | turn right |
| F10, Up | faster (each press, held keys too, up to 9/6 of a cell) |
| F9, Down | back to normal speed |
| P | pause (and a game in a window that loses the front pauses itself) |
| M | sound on or off |
| C (in a game) | the colours: Classic (the 1982 four) or isoTron on a 16-colour card; high intensity on a CGA in full screen |
| F, Alt+Enter | full screen, and back |
| Space, Enter | continue after a round |
| Esc | end the game; at the title in full screen, back to the window |

The Game menu starts a game at any tier, turns the sound on and off, changes
the colours, goes full screen, pauses and ends the game. Full Screen is greyed, with its reason, on a
display that has no mode for it.

## Build

```
make isotron        # build/isotron.o88
make isotrondisk    # four standalone floppies: 1.44MB, 720KB, 1.2MB, 360KB
make isotrontest    # the host model's selfcheck, then on MartyPC seeded games
                    # against it, the canvas byte for byte and the window's
                    # picture pixel for pixel on CGA, Hercules, VGA and EGA,
                    # full screen on all four, the toggle, two displays,
                    # and the game's flow through its own keys
```

`ISOTRON.O88` also ships in `GAMES/` on the apps disks (and on
`games360.img` at 360KB). It claims 63 KB for its picture and 30 KB for its
work beside its own ~19 KB (15 KB on the disk), and says so in words when
the machine has less.
