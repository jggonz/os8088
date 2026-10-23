REALDMG - a Game Boy for os8088
===============================

REALDMG.O88 runs original Game Boy (DMG) cartridges, in a window or full
screen. The CPU, the picture, the timers, the sound (all four channels on
an FM card, the square channels on the speaker), battery saves and the MBC3
clock are all here. Bring your own
games: no game ships with it, and the only ROM on this disk is our own demo.

ON THIS DISK
------------
REALDMG.O88 the program. The Game Boy's CPU core is inside it, and so is
            the recompiler that a 286 or later uses.
GBDEMO.GB   RealDMG's demo, written for an IBM PC 5150: a bouncing ball,
            a clock and a blip on every landing. It is ours, and it runs
            on a real Game Boy too.
README.TXT  this file (REALDMG.TXT when it came from The Wire).

Put your own .GB files anywhere and double-click one: .GB is RealDMG's
document, so the program opens with the game. Or open REALDMG.O88 and use
Machine > Load Game, which also swaps one game for another. .GBC opens too: a game made for both the Game Boy and the
Game Boy Color plays as a Game Boy game; one for the Color alone is
refused, and so is a cartridge type RealDMG does not have, each with a
note saying so. A cartridge is 32K to 256K (MBC1, MBC2, MBC3 or MBC5), or up
to 8MB on a machine with extended memory: there the cartridge is kept above
1MB and its banks come down as the game switches to them. A 256K game on a
640K machine goes there too when that is what leaves room for the
recompiler.

KEYS
----
Arrows      the D-pad          (as shipped: Machine > Controls...
X           A                   gives any button any other key)
Z           B
Enter       Start
Space       Select
F1          the speed line (Machine > Show Stats)
F2          Machine > Interlace's next setting
F3          the next of CPU > Recompiler, Static Recompiler and
            Interpreter
F4          CPU > Speed's next setting
F9          Machine > Sound's next setting
1 to 9      the save state slot
F5          save the game's state in that slot
F8          load it back
F6          record a replay from that slot, and F6 again to stop
F7          play the slot's replay, and F7 again to stop it
F12         write the log now (Machine > Log)
Alt+Enter   full screen, and back
Esc         leave full screen

The speed line is the game's speed (100% is the Game Boy's own 59.7 frames
a second) and the frames drawn a second. They are separate on purpose: the
game keeps its speed first, and the picture gets what is left.

SAVE STATES
-----------
A save state is the whole machine at a moment, to come back to: nine slots
for each game, kept beside it as NAME.SS1 to NAME.SS9 (34 to 66KB each).
Press 1 to 9 to pick a slot, F5 to save and F8 to load - in the window or
full screen - or the Save State menu, which lists slots 1 to 6. A slot only loads into
the game that saved it. The battery save (NAME.SAV) is separate, and is
written when the window closes, as before.

REPLAYS
-------
A replay is a save state and the buttons pressed from there, a frame at a
time. F6 (Save State > Record Replay) saves the slot's state, as F5 does, and
keeps the buttons until F6 again, then writes them beside the game as
NAME.RP1 to NAME.RP9 - a few hundred bytes. F7 (Save State > Play Replay) loads
the slot's state and plays the same buttons again; the keyboard does
nothing to the game until it ends. It plays exactly as it was played, so
the same game can be timed again and again - with the log on, against
another build, on another machine. At its end it says "ended", or "OUT OF
STEP" if the game came out anywhere else. A replay belongs to its slot's
state: save over the slot, and the replay will not play. Reset, Load State
and Load Game wait for a replay to end.

MENUS
-----
A * marks the setting that is chosen; a key at the right does the same.

Machine > Load Game...   a game to play: another one in its place if one
                         is running
Machine > Reset          start the cartridge again
Machine > Pause          stop the clock
Machine > Full Screen    the game on the whole screen (Alt+Enter). The
                         window is the slow way to play, and says so
                         for a second and a half before a game starts
Machine > Sound          On, At 100% only, or Off (F9). A slowed game
                         plays its tune slower too; At 100% only is
                         silent below full speed
Machine > Show Stats     the speed line (F1)
Machine > Interlace      Off, On, Scrolling or Doubled (F2): less to draw
                         when the picture moves. On draws half the
                         lines each time; Scrolling only while it
                         scrolls; Doubled while it scrolls draws the
                         even lines and repeats each one below it,
                         without the comb Scrolling leaves on a
                         moving edge (the status bar stays whole)
Machine > Log            a record a second, to a file (F12 writes it now)
Machine > Controls...    which key is which button: click a button on
                         the Game Boy drawn there, then press its key.
                         A key another button had changes places with
                         it; Esc, Alt, F1 to F9, F12 and 1 to 9 are
                         RealDMG's own.
                         Default puts them back. Kept for every game

CPU > Recompiler         translates the game as it runs (a 286 or
                         later; F3 steps through the three)
CPU > Static Recompiler  translates it before it runs, then never
                         again, so play never slows for it. It uses
                         NAME.RDP beside the game: play with the
                         Recompiler and Profile for Static on first
CPU > Interpreter        no translating: the only way on an 8088, and
                         it gives the recompiler's memory back
CPU > Profile for Static on, the Recompiler keeps what it translated
                         as NAME.RDP beside the game, for Static. Off
                         by default
CPU > Cache: 128K        64K more for the recompiler's code, besides
                         the 64K and the ~45K it finds inside RealDMG
                         itself for nothing - which hold Kirby's Dream
                         Land. For a bigger game that still slows while
                         its code is translated again; a game whose
                         profile is that big asks for it by itself.
                         Kept for every game after
CPU > Speed              100%, 90%, 75% or 50% of the Game Boy's
                         speed (F4), held steady: what the game does
                         not use goes to the picture. A game too slow
                         for its machine is smoother a little slower
                         (Speed and Sound are kept for each game, in
                         SYSTEM\APPDATA on this program's own disk)

Save State > Slot 1-6    the slot the states and replays use (keys 1 to
                         9 choose any of the nine)
Save State > Save State  save the game's state in the slot (F5)
Save State > Load State  load it back (F8)
Save State > Record Replay  record a replay from the slot (F6)
Save State > Play Replay    play it back (F7)

The log is written beside REALDMG.O88 when you turn it off or close the
window, under a new name each time: the game's first six letters and a
number (SUPER00.LOG, SUPER01.LOG, ...). It names the game and the machine,
then gives a line a second: the speed, the frames run and drawn, how long
the drawing took, and anything you did (Reset, Pause, Full Screen, the
Recompiler, a replay recorded or played). It is what to send back after a
test run - and F12 writes it at once, without closing anything.

Full screen is VGA's 320x200 at 1x, a Hercules card's page at 2x2, or
CGA's 320x200 in four colours.

SAVES
-----
A cartridge with a battery is saved when you close its window, to
<game>.SAV beside the game. It is read back the next time the game opens. The file is the usual .SAV format, with VBA's clock
block for an MBC3, so it moves to and from other emulators. The MBC3 clock
counts while the game runs, and catches up when a save opens on the time
the save was put away.

WHAT TO EXPECT
--------------
On a 286 at 12MHz most games run near full speed, faster with the
recompiler. On a 4.77MHz 8088 a busy game runs slowly. GBDEMO.GB runs at
full speed there, drawing about four frames a second, because the demo was
written to need very little.

With a Sound Blaster or an AdLib (SOUND.DRV loaded, and the Control
Panel sending tones to it) all four of the Game Boy's channels play on its
FM chip: both square channels, the wave channel and the noise, as near as
the chip comes to them. Without one, the PC speaker plays the louder of the
two square channels, and the wave and noise channels are silent.
