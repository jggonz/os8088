# WireBall

WireBall is Pong, played full screen. Its best trick is the Serial Match:
join two computers with a null modem cable and the court stretches across
both screens. Each player has a paddle at the outside edge of their own
screen. The ball leaves one monitor through the inside edge and carries on
across the other. With one computer, you can practise against the computer
instead. The first player to 11 points wins.

![A practice rally: you are the paddle on the left, the computer is on the right.](shot:wireball-practice){full}

## Starting WireBall

WireBall is in the `GAMES` folder of the software disk, and at the top
level of the games disk. Double-click **WIREBALL.O88**. A window opens with
the title picture, two monitors joined by a cable, and four buttons:

![The WireBall window, ready to start a match.](shot:wireball-splash){full}

| Button | Key | What it does |
|---|---|---|
| Serial Match [S] | `S` | Look for another computer on the cable and play it |
| Practice [L] | `L` | Play against the computer on this screen |
| How to Play [H] | `H` | Show the controls. The button becomes **Back [H]** |
| Sound: On [M] | `M` | Turn the game sounds off and on again |

The **Game** menu has the same two ways to play: **Serial match** and
**Practice**. Clicking the picture itself does nothing, so a stray click
cannot start a match.

## Playing

The match fills the screen. The top line shows the score, left player
first, and what is happening: **Get ready...** before each serve, which
comes two seconds after a point, then **Rally!** while the ball is in play.

| Key | What it does |
|---|---|
| `Up` / `Down`, or `W` / `S` | Move your paddle. Hold the key to keep moving |
| `P` | Pause, and press again to carry on |
| `N` | Start a new match |
| `M` | Sound off and on |
| `Esc` | Leave the match and go back to the desktop |

Aim with the paddle. A hit near the middle sends the ball back flatter, a
hit near either end sends it off at a steeper angle. Moving the paddle as
the ball arrives adds a little push in the direction you are moving. The
ball speeds up every two returns, up to a limit, and slows down again for
the next serve. When someone reaches 11 the top line says **LEFT wins!** or
**RIGHT wins!**; press `N` to play again.

![How to Play, the window's own list of keys.](shot:wireball-help){full}

In Practice you are always the LEFT paddle, and the computer plays the
right. Practice never touches the serial ports.

## Two players over a cable

You need two computers, each running os8088 with WireBall, and a **null
modem cable** between a serial port on each one. Either COM1 or COM2 will
do, and the two computers do not have to use the same port number. The
cable needs only three wires: transmit and receive crossed over, and signal
ground straight through.

| Connector | Transmit and receive | Ground |
|---|---|---|
| 9-pin | pins 2 and 3 crossed | pin 5 to pin 5 |
| 25-pin | pins 2 and 3 crossed | pin 7 to pin 7 |

No handshake wires, modem, network driver or speed setting is needed.

1. Connect the cable and open WireBall on both computers.
2. Choose **Serial Match** (or press `S`) on both.
3. Each screen says **Searching COM1/COM2...** until it hears the other.
   Then the games pick sides by themselves: one screen says **LEFT - serial
   linked**, the other **RIGHT - serial linked**, and the match begins.

![Serial Match waiting for the other computer.](shot:wireball-search){full}

Each computer judges hits against its own paddle, so what you see on your
screen is what counts. Either player can pause (`P`) or start a new match
(`N`), and the other screen follows. If one player leaves or the cable is
pulled out, play stops within three seconds and the remaining computer goes
back to searching. Joining again starts a fresh match.

If neither port is there, the top line says **No COM1/COM2 UART found.**
Press `Esc` to go back.

## Sound

Sound is on when WireBall starts. You hear short tones for a wall bounce, a
paddle hit, a point, each serve, the link being made, and a little tune for
the winner. They come from the PC speaker, or from the sound card if one is
set up with the sound driver, [[SOUND.DRV]]. `M`, or the **Sound** button, turns them off and on.

## The WireBall menu

| Item | What it does |
|---|---|
| About WireBall | Shows the program's name and its serial settings |
| Close | Closes WireBall |

## Good to know

- WireBall works on VGA, EGA, CGA and Hercules displays, and runs at full
  speed on an original 4.77 MHz IBM PC or XT.
- While a Serial Match is running, WireBall borrows COM1 and COM2. Unplug
  any other device on those ports, such as a modem. Everything is put back
  when you return to the desktop, and a serial mouse works again then.
- Only the standard COM1 and COM2 ports are used. USB serial adapters do not
  work.
- Both computers must run the same version of WireBall. The link runs at
  2400 baud.
- The scores are not saved.
