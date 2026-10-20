WireFleet -- Two PCs. Two fleets. One winner.

Join two PCs with a null modem cable, 2400 baud, 8N1, no flow control.
Keep the serial mouse on one port and use the other for the game. Open
WIREFLET.O88 on both PCs: each one looks for the other by itself, on
whichever COM port is free, and says when the opponent is found. If the
mouse has not moved yet, move it once. Link / Automatic starts looking
again; Link / Connect COM1 or COM2 uses one port only.

Open WIREFLET.O88. Place five ships on the left grid. They may touch, but
must not overlap or extend off the grid. Select a ship with 1-5 or click
its name. Click a square, or move with arrows and press Enter to place it.
R rotates. Right-click a placed ship to remove it. A auto-places all five;
C clears the fleet. Y readies and locks it; N begins a new fleet.
After both captains are Ready, the lower match identity takes the first
turn. Select a square in enemy waters and click Fire, or use arrows and
Enter. Misses are dots, hits are crosses, and sunken ships are marked.
The defender judges each shot; unhit ship positions stay on that PC.

T opens chat. Enter sends up to 32 characters; Escape cancels. If the
cable goes quiet, firing pauses while fleets and the pending shot stay
in memory. Reconnect the same opponents to continue. A history mismatch
pauses the match rather than guessing whose board to keep.

S saves FLEET.WFL through the file chooser; O loads a saved match. Save
on BOTH PCs before closing. Load each PC's own file; each looks for the
same opponent by itself.
The local fleet, both shot grids, turn, peer identity and pending command
are preserved. Files contain your private fleet; do not give one to the
opponent. N starts a fresh match. Before the first shot the other PC
follows by itself; after a finished match both captains press N.

H opens controls/About. M mutes or enables the finite sound effects.
The system Sound setting selects PC Speaker, AdLib or Sound Blaster.
F or Alt+Enter toggles fullscreen. CGA fullscreen uses cyan, magenta,
white and black; the CGA desktop and Hercules use monochrome artwork.
Escape returns from fullscreen. S/O return to the desktop file chooser.

The host builds four standalone application disks with make wirefleetdisk.
The design contract is SPEC.md section 114; the story list and build diary
are docs/plans/WIREFLEET-PLAN.md and docs/HOW-WIREFLEET-WAS-BUILT.md.
