# RealDMG

RealDMG is a Game Boy for your PC. It runs original Game Boy cartridges,
saved as `.GB` files, in a window on the desktop or on the whole screen. The
picture, the sound, the buttons, battery saves and the clock some cartridges
carry are all there. You can also save the whole game at any moment and come
back to it, and record what you play to watch it again.

No game comes with RealDMG. You bring your own cartridges. The disk carries
one Game Boy program of the project's own, [[GBDEMO.GB]], so there is
something to run straight away.

![RealDMG running its demo cartridge in a window.](shot:realdmg-window){full}

## Starting RealDMG

RealDMG comes on a disk of its own, in a folder called `REALDMG`. The folder
holds the program, **REALDMG.O88**, the demo, **GBDEMO.GB**, and
[[README.TXT]], which has notes on the program. The everything set and the
live USB image and CD carry the same folder.

There are two ways to start a game:

- Double-click a `.GB` file. RealDMG opens with that game. Your cartridges
  can be anywhere, on any disk.
- Double-click **REALDMG.O88**. The window says **Machine > Load Game** and
  **a Game Boy .GB**. Choose **Machine > Load Game...** and pick the game in
  the file dialog.

A `.GBC` file opens the same way. A game made for both the Game Boy and the
Game Boy Color plays as a Game Boy game. A game for the Game Boy Color alone
is refused.

RealDMG runs on any machine os8088 runs on. How fast a game plays depends on
the machine: on a 286 at 12 MHz most games run near full speed, and on a
4.77 MHz 8088 a busy game runs slowly. The demo was written to need very
little, and runs at full speed even on an IBM PC.

## Playing in the window

![Before a game starts in the window, RealDMG says the window is the slow way to play.](shot:realdmg-notice){right}

When a game starts in the window, RealDMG first shows **Windowed is slow:
Alt+Enter for Full Screen** for a second and a half. Then the game starts.
Press `Alt+Enter` while the notice is up and the game starts on the whole
screen instead.

The window shows the Game Boy's screen at its own size, 160 by 144 pixels,
in four shades. On a black-and-white display the shades are drawn as dot
patterns.

RealDMG keeps the game at its proper speed first, and the picture gets the
time that is left. So on a slow machine the game keeps time and the picture
changes less often. Drawing in a window costs more than drawing on the whole
screen, which is why full screen is the faster way to play.

The game reads the keyboard only while the RealDMG window is in front.

## The buttons

The Game Boy's buttons are keys on the keyboard. As RealDMG comes, they are:

| Game Boy | Key |
|---|---|
| The D-pad | The arrow keys |
| A | `X` |
| B | `Z` |
| Start | `Enter` |
| Select | `Space` |

If you hold two opposite directions at once, such as Left and Right, the one
you pressed last wins, because a real Game Boy's pad cannot press both.

While you hold `Alt`, the game sees no buttons, so `Alt+Enter` never presses
Start.

## Changing the keys

![Machine > Controls... shows which key each button has.](shot:realdmg-controls){right}

**Machine > Controls...** opens a window of its own, **RealDMG Controls**,
with a Game Boy drawn in it. Each button has a box naming its key.

1. Click a button's box. The Game Boy's screen says **Press a key for** and
   names the button.
2. Press the key you want. If another button had that key, the two buttons
   swap keys.

`Esc` stops waiting without changing anything. Click **Default** to put every
key back as RealDMG came. Press `Esc` when nothing is waiting, or click the
close box, to put the window away.

Some keys belong to RealDMG itself and cannot be given to a button: `Esc`,
`Alt`, `F1` to `F9`, `F12` and the number keys `1` to `9`. Press one and the
screen says **That key is RealDMG's own: press another**.

The keys you choose are kept for every game, and stay chosen the next time
you start RealDMG. While the Controls window is in front, the game sees no
keys at all.

## Full screen

**Machine > Full Screen**, or `Alt+Enter`, puts the game on the whole
screen. `Esc` or `Alt+Enter` brings it back to the window. There is no menu
bar on the full screen, but every RealDMG key below works there.

![The demo on the whole screen of a VGA, with the speed line under the picture.](shot:realdmg-fullscreen){full}

What full screen looks like depends on the display:

- On a VGA, the picture is at its own size in the middle of the screen, with
  a border round it.
- On a Hercules card, the picture is twice as wide and twice as tall.
- On a CGA, the picture is in four colours.

Full Screen is greyed while no game is running, and on a display that has
none of these.

## The speed line

**Machine > Show Stats**, or `F1`, shows a line such as **100% 4fps**. In the
window it sits at the top left of the picture; on the full screen it is under
the picture.

The first number is the game's speed, where 100% is a real Game Boy's 59.7
frames a second. The second is how many pictures RealDMG drew in that second.
The two are separate on purpose: the game keeps its speed first, and the
picture gets what is left.

**JIT** after the numbers means the Recompiler is running, and **AOT** means
the Static Recompiler is. While the full screen is up, the line also shows
RealDMG's short messages, such as a new Speed setting.

## Making a game faster

Three settings trade looks or speed for smoothness.

**Interlace.** **Machine > Interlace**, or `F2`, steps through four
settings. They give RealDMG less to draw when the picture moves.

| Setting | What it does |
|---|---|
| Off | Every line is drawn every time |
| On | Half the lines are drawn each time, taking turns |
| Scrolling | Like On, but only while the picture scrolls |
| Doubled | While the picture scrolls, the even lines are drawn and each one is repeated below it. A moving edge stays clean, without the comb that Scrolling leaves. The status bar at the bottom of a game stays whole |

**Speed.** **CPU > Speed**, or `F4`, holds the game at 100%, 90%, 75% or 50%
of a Game Boy's speed. The time the game does not use goes to the picture,
so a game too slow for its machine is smoother a little slower. RealDMG
remembers the Speed for each game.

**The recompiler.** On a 286 or later, RealDMG can translate the game's
program into the PC's own instructions, which runs it much faster. The CPU
menu has three ways to run a game, and `F3` steps through them:

- **Recompiler** translates the game as it runs. It is the setting a 286
  starts with. A game may slow for a moment while new parts of it are
  translated.
- **Static Recompiler** translates the game before it runs, and never again,
  so play never slows for it. It needs a profile of the game, a file called
  `NAME.RDP` beside it. To make one, play the game first with **Recompiler**
  chosen and **CPU > Profile for Static** turned on. Choosing Static turns
  Profile for Static on and reads the profile.
- **Interpreter** does no translating. It is the only way on an 8088, and on
  a 286 it gives the recompiler's memory back to the system.

On an 8088 the recompiler's items read **Recompiler (286+)**, **Static
(286+)**, **Profile (286+)** and **Cache (286+)** and are greyed, and `F3`
does nothing.

**CPU > Cache: 128K** gives the recompiler 64K more room for the code it
translates, which helps a big game that otherwise slows while its code is
translated again. It is kept for every game after. Turned on during a game,
it starts at once when there is room, or with the next game when there is
not.

## Sound

With a Sound Blaster or an AdLib, [[SOUND.DRV]] loaded, and the Control Panel
sending tones to the card, all four of the Game Boy's sound channels play on
the card's FM chip: both square-wave channels, the wave channel and the
noise, as near as the chip comes to them. Without one, the PC speaker plays
the louder of the two square-wave channels, and the wave and noise channels
are silent.

**Machine > Sound**, or `F9`, steps through three settings:

| Setting | What it does |
|---|---|
| On | Sound at any speed. A game slowed with CPU > Speed plays its tune slower too |
| At 100% only | Sound only while CPU > Speed is 100% |
| Off | No sound |

RealDMG remembers the Sound setting for each game, as it does the Speed.

## Save states

A save state is the whole game at one moment, to come back to. Each game has
nine slots.

- Press `1` to `9` to choose a slot. RealDMG briefly says **Slot 1**,
  **Slot 2** and so on.
- Press `F5` to save the game in that slot. RealDMG says **Saved slot 1**.
- Press `F8` to load it back. RealDMG says **Loaded slot 1**.

The keys work in the window and on the full screen alike. The **Save State**
menu does the same, and lists slots 1 to 6; slots 7 to 9 are chosen with
their keys.

A slot only loads into the game that saved it. Load one from another game
and RealDMG says **Slot 1 not this game**; load an empty one and it says
**Slot 1 is empty**. Each state is a file beside the game, `NAME.SS1` to
`NAME.SS9`, of 34 to 66K, so the game's disk needs room for them.

## Replays

A replay is a save state and the buttons you pressed from there, a frame at
a time. It plays back exactly as you played, every time.

- **Save State > Record Replay**, or `F6`, saves the slot's state, as `F5`
  does, and starts recording. The item then reads **Stop Recording**; `F6`
  again stops and writes the replay. RealDMG says **Replay 1 saved**.
- **Save State > Play Replay**, or `F7`, loads the slot's state and plays the
  buttons back. The keyboard does nothing to the game until the replay ends,
  or until you press `F7` again to stop it.

At its end RealDMG says **Replay 1 ended**. If the game came out anywhere
else, it says **Replay 1 OUT OF STEP**. A replay belongs to its slot's
state: save over the slot and the replay will not play, and RealDMG says
**Slot 1 was saved over**. Each replay is a file beside the game, `NAME.RP1`
to `NAME.RP9`, a few hundred bytes long.

While a replay records or plays, Reset, Load State and Load Game wait for it
to end, and say **A replay is running**.

## The log

**Machine > Log** keeps a record of how the game ran, a line a second: the
speed, the frames run and drawn, how long the drawing took, and anything you
did, such as Reset, Pause, full screen or a replay. RealDMG says **Log on:
written at close**.

The log is written when you turn it off or close the window, beside
REALDMG.O88, under a new name each time: the first six letters of the game's
file name and a number, such as `GBDEMO00.LOG`. `F12` writes it at once without closing
anything, and starts a new one. With the log off, `F12` says **Log is off
(Machine)**. The log holds the latest two minutes. It is the thing to send
back after trying a game on a real machine.

## The Machine menu

![The Machine menu.](shot:realdmg-machine){right}

A `*` before an item marks a setting that is on. The key at the right does
the same as the item.

| Item | Key | What it does |
|---|---|---|
| Load Game... | | Open a game. If one is running, it is put away first, with its battery save written |
| Reset | | Start the cartridge again |
| Pause | | Stop the game's clock. Choose it again to carry on |
| Full Screen | `Alt+Enter` | Play on the whole screen |
| Sound: On | `F9` | Step through On, At 100% only and Off |
| Show Stats | `F1` | Show or hide the speed line |
| Interlace: Off | `F2` | Step through Off, On, Scrolling and Doubled |
| Log | `F12` | Keep a log of the game, or stop and write it |
| Controls... | | Choose the key for each button |

## The CPU menu

![The CPU menu on an 8088, where only the Interpreter can be chosen.](shot:realdmg-cpu){right}

| Item | Key | What it does |
|---|---|---|
| Recompiler | `F3` | Translate the game as it runs. Needs a 286 or later |
| Static Recompiler | `F3` | Translate the game before it runs, from its profile. Needs a 286 or later |
| Interpreter | `F3` | Run the game without translating |
| Profile for Static | | Keep what the Recompiler translated as `NAME.RDP`, for the Static Recompiler. Off as RealDMG comes |
| Cache: 128K | | Give the recompiler 64K more room |
| Speed: 100% | `F4` | Hold the game at full speed |
| Speed: 90% | `F4` | ...at 90% |
| Speed: 75% | `F4` | ...at 75% |
| Speed: 50% | `F4` | ...at half speed |

## The Save State menu

![The Save State menu.](shot:realdmg-savestate){right}

| Item | Key | What it does |
|---|---|---|
| Slot 1 to Slot 6 | `1` to `6` | Choose the slot the states and replays use. `7`, `8` and `9` choose the other three |
| Save State | `F5` | Save the game in the slot |
| Load State | `F8` | Load the slot back |
| Record Replay | `F6` | Record a replay from the slot. Reads Stop Recording while it records |
| Play Replay | `F7` | Play the slot's replay. Reads Stop Replay while it plays |

Save State and Load State are greyed while no game is running, and so are
the replays. Record Replay is greyed while a replay plays, and Play Replay
while one records.

## The RealDMG menu

The menu beside the System menu has **About RealDMG**, which names the
program and reminds you where the keys are, and **Close**, which closes the
window.

## Files

| File | Where | What it is |
|---|---|---|
| `NAME.GB` | anywhere | A game. Double-click it to play |
| `NAME.SAV` | beside the game | The cartridge's battery save. Written when the window closes or another game is loaded, and read back the next time the game opens |
| `NAME.SS1` to `NAME.SS9` | beside the game | Save states, one for each slot |
| `NAME.RP1` to `NAME.RP9` | beside the game | Replays, one for each slot |
| `NAME.RDP` | beside the game | The game's profile, for the Static Recompiler |
| `XXXXXXnn.LOG` | beside REALDMG.O88 | The log |
| `REALDMG.CFG` | in `SYSTEM\APPDATA` on RealDMG's own disk | Your keys, the Cache setting, and each game's Speed and Sound |

A battery save is the usual `.SAV` format. For a cartridge with a clock, the
clock is saved with it the way other Game Boy emulators save it, so a save
moves to and from them. The clock counts while the game runs, and catches up
on the time the save was put away when the game opens again.

Because the saves are written beside the game, keep your games on a disk
that is not write-protected and has some room.

## Good to know

- A cartridge can be 32K to 256K, with an MBC1, MBC2, MBC3 or MBC5 chip or
  none. On a 286 or later with memory above 1MB and [[XMEM.DRV]] loaded, it
  can be up to 8MB: RealDMG keeps it in that memory and brings each part of it
  down as the game asks for it.
- RealDMG refuses what it cannot run, with a short message: **REALDMG: not a
  GB ROM** for a file that is not a Game Boy game, **REALDMG: needs GB
  Color** for a game for the Game Boy Color alone, **REALDMG: MBC 22h
  unknown** (or another number) for a cartridge chip it does not have, and
  **REALDMG: over 256K wants XMS** for a big game with no memory above 1MB
  to keep it in.
- **REALDMG: no room for ROM** or **no room for RAM** means the system does
  not have the memory free. Close other programs, or choose CPU >
  Interpreter on a 286 to give the recompiler's memory back, and load the
  game again.
- If a game stops on an instruction a Game Boy does not have, the window
  says **The game stopped: an invalid opcode**. Machine > Reset starts it
  again.
- The link cable port is not connected to anything, so two-player games
  that need a second Game Boy cannot use it.
