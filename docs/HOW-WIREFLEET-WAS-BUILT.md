# How WireFleet was built

WireFleet — Two PCs. Two fleets. One winner.

This is the development diary for a native os8088 naval game. It records
what was actually built and checked, including the decisions that changed
the implementation. It can later be adapted for the website. Planned work
is kept separate from completed work in the delivery plan.

## 2026-10-20 — A screenshot, five ships, and an 8250

The brief started with two views: placing a fleet, and continuing a battle
after a connection returned. Both belong in ordinary overlapping windows.
The target includes VGA in color, CGA on the desktop and in four-color
fullscreen, and Hercules. A ship must remain identifiable without color,
and a hit cannot be distinguished from a miss by color alone.

We started in `feat/wirefleet`, in a separate worktree based on the freshly
fetched main branch. The original checkout's VM settings were left alone.
Before designing the game, we read the generated OS API index and WireBall's
contracts and implementation. Window creation, About, controls, clipping,
sound routing and fullscreen mode changes already have conventions.

One convention exposed the first engineering problem. WireBall temporarily
borrows both serial ports while the desktop is frozen. WireFleet must keep
the desktop mouse alive during play. The mouse interrupt handler examines
both ports, so merely disabling interrupts on the game's port would still
lose game bytes whenever the mouse moved. A coordinated serial lease is
needed before windowed multiplayer can be called reliable.

The ship source was generated with the built-in imagegen tool: an aircraft
carrier, battleship, cruiser, submarine and destroyer, all viewed directly
from above. The original source and exact prompt are retained under
`apps/wirefleet/art/`. Build-time conversion will produce bounded palette
assets, including explicit monochrome forms. There is no generation service
in the build or on the old computer.

The first delivery is a contract and reviewable work plan. The game,
reconnection, saved matches and sound are still implementation stories at
this point; this entry does not claim they have been tested.

## 2026-10-20 — Deployment on three desktops

The first executable wave puts all five ships on a native desktop grid.
Placement checks the entire proposed ship before removing its old position:
an invalid move cannot make a carrier disappear. Auto-placement tries a
bounded number of candidates, then has a simple legal fallback. Ready locks
the fleet until the captain explicitly starts a new one.

The runtime uses 38 opaque tiles: water, three shot shapes, and 17 ship
squares in each orientation. The selected generated source becomes packed
4bpp color plus explicit 1bpp tiles. A full board is assembled into ten row
spans. On a change, cached tile identities find the affected range first;
only that range is composed and blitted. There is no timer-driven repaint
of an idle sea.

The layouts are based on the actual content height. VGA cells are 24 by 24;
Hercules cells are 24 by 18; CGA desktop cells are 24 by 8. The last sounds
flat until the 640 by 200 display's pixel aspect is taken into account.
The compact layout keeps buttons and its status line above the dock.

We booted the package on MartyPC's VGA, CGA and Hercules XTs, inspected the
empty and auto-placed fleets, and read 17 occupied cells with the expected
five ship lengths. All four standalone disk sizes build. The package's
early image was 18.3 KB before compression, about 8.6 KB on disk, plus
3.5 KB of zeroed working storage. These are build observations, not timing
measurements or final acceptance. The registered `wirefleetdeploy` and
`wirefleetart` rows are deferred to the final test pass.

The repository's host checks caught a naming detail: the product name is
nine letters, while FAT's stem allows eight. The explicit `WIREFLET.O88`
name now follows through the build, live-media inventory and symbol checks.
The standard button registry also required its painter to declare its own
record. Both checks prevented apparently correct UI code from becoming an
inconsistent shipping package.

At the owner's request, this approach is now a shared repository rule in
both AGENTS.md and CLAUDE.md: isolated worktree, contract first, reviewable
story waves through one draft PR, retained art sources and a continuing
diary. The existing final-pass-only regression rule stays in force.

## 2026-10-20 — Keeping the mouse alive beside the cable

The serial boundary became a reusable OS facility rather than a private
WireFleet interrupt handler. The mouse owns its detected UART; a desktop
program can lease the other one at 9600 baud, 8N1. The existing interrupt
handler routes that port to bounded byte queues before examining mouse
packets. Both queues live in the pinned application region. The OS releases
the lease before freeing an instance, even on forced close.

Most of this service lives in an on-demand `SERIAL.DRV`, available to both
kernels. Its measured resident cost crosses one 512-byte allocation rung:
the big kernel occupies 97,792 of its fixed 100,864-byte budget, leaving
3,072 bytes; the small kernel occupies 61,440 of 64,000, leaving 2,560.
The active module itself needs a 1KB heap claim. These are build and budget
measurements; the adapter, IRQ and forced-close assertions belong to the
final integration pass. Hibernate refuses an active serial lease rather
than restoring a half-old hardware connection from a memory image.

## 2026-10-20 — Battles and a recoverable unanswered shot

Each captain keeps the private fleet locally. Fixed-size escaped frames
carry identities, a turn, a command and a CRC. The defending PC judges a
shot. Only a sunk ship reveals its extent. A turn history hash detects
disagreement without transmitting either private board.

The awkward reconnect case is a defender that has recorded a hit while
the shooter never received its answer. The defender caches that answer;
the shooter retains the unanswered command. Reconnection repairs that one
turn through replay and acknowledgment. Duplicate commands must not add
shots or sound twice. Chat has its own sequence and acknowledgment so a
lost message cannot starve turn synchronization.

Both captains can save their own 512-byte `.WFL` record. It includes the
pending shot and cached answer, rather than only the visible grids. Loading
checks the checksum, fleet geometry, shot counts, turn parity and sunk
markers before replacing live state. The final fault fixture deliberately
loses a result, saves and closes both apps, loads each PC's own file and
reconnects the same peers. At this entry that fixture is written, with its
result still to be established.

## 2026-10-20 — A fourth visual form and routed sound

The original art now produces 72 tiles, including hits over visible ships,
plus a 12-pixel-wide, packed two-bit form for CGA's 320-by-200 color mode.
Monochrome hits have a white halo around a dark cross so they survive a
gray hull. Hercules rows were shortened to 16 pixels to leave space for
chat. The desktop and color fullscreen paths share model and tile selection;
only the output backend differs. Headers, buttons and chat rows are cached
as well as the sea, so an idle timer does not redraw the whole interface.

Sound cues are finite note sequences through the existing sound router.
That gives PC Speaker, AdLib and Sound Blaster effects while respecting the
user's system setting. The game does not take over the timer or program a
sound card itself. Fullscreen returns through FSX and releases its tone.
These paths compile; the final machine checks will distinguish that from
observed display, input and sound behavior.
