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
