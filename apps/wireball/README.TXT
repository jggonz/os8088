WIREBALL - Across the wire
=========================
One Pong court stretched across two computers. First to 11 wins.

Connect a serial null modem between COM1 or COM2 on each computer. The port
numbers need not match. Use crossed TX/RX and a shared signal ground (DB9:
pins 2/3 crossed, 5 straight; DB25: pins 2/3 crossed, 7 straight). No modem,
network driver, handshaking wires or baud setup is needed. Disconnect other
serial devices during a serial match; COM1 and COM2 are borrowed while the
fullscreen game runs. All UART settings are restored when you leave.

Open WIREBALL.O88 on both computers and choose Serial Match (or press S). The
games find one another and assign LEFT and RIGHT automatically. Each player
has a paddle at the outside of their screen. The ball disappears through
the inside edge and continues onto the other screen. Each computer judges hits
against its own paddle, then passes the ball to the other at the center.
Both players can pause or restart the match.
The match begins automatically, with a two-second delay before each serve.

Up/Down or W/S   Move your paddle (hold to move)
P               Pause/resume (either player)
N               Start a fresh match (either player)
Escape          Return to the desktop and release the serial ports
L at the panel  Practice against the computer, on one screen
H at the panel  Show/hide the controls page
M               Toggle game sound (also a button on the splash)

If a player leaves or the cable is unplugged, play stops within three
seconds and the remaining computer searches again. Rejoining starts a fresh
match. Noise and lost packets are discarded; later snapshots repair state.

The splash shows two CRTs connected by a cable, with native play buttons
and an instructions page. Its artwork adapts to color, CGA and Hercules.
Paddle hits, wall bounces, points and the winner have short sound cues. The
OS chooses the PC speaker or your configured sound driver; M mutes sound.

The court uses pixel graphics on VGA, CGA, EGA and Hercules. VGA and lone
Hercules adapters flip hidden VRAM pages; CGA updates small sprite spans.
Paddles and physics move in quarter-row steps at 72.8 Hz on an XT. The small
kernel uses an 80x25 text court. Serial: 2400 baud, 8N1.
Standard COM1 (3F8h) and COM2 (2F8h) only; no USB serial adapters.

Aim with the paddle: middle hits go flatter, end hits make steeper shots.
Moving the paddle as you hit adds a small directional impulse. The ball
speeds up every two returns, up to a limit, then slows for the next serve.
Both computers must run the same version (serial protocol 3).

Build: make wireball
Application disks (all four floppy sizes): make wireballdisk
Automated two-computer test: make wireballtest
Pixel court, VRAM pages and 8088 frame rates: python3 tests/wireball.py --video
CGA/Hercules 8088 test: python3 tests/wireball.py --mono
8088 physics test: python3 tests/wireball.py --physics
Splash/button test on all adapters: python3 tests/wireball.py --front

Two-PC 86Box launcher (macOS/Linux): python3 tools/wireball-pair.py
Or: make wireball-pair
Requires 86Box 6.0+ with the XT and OTI video ROMs and the repo build tools.
On macOS, tools/setup-macos.sh installs the tools, 86Box and its ROMs.
The launcher builds the disks and opens two 640KB XTs with COM2 connected.
Each VM has its own writable disks under build/86box-wireball-pair/pc1 or pc2.
Open B: and launch WIREBALL on both, then press S for Serial Match.
Click a VM to control its paddle; Escape returns to its desktop.
Close both VMs and rerun the launcher to load an updated build.
Overrides: --86box PATH, --roms DIR. --prepare-only builds without launching;
--no-build uses existing disk images. Run --help for details.

See SPEC.md section 107 for the protocol and hardware validation limits.
