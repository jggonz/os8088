# WireFleet

WireFleet is a naval battle for two PCs connected by a null modem serial
cable. Place five ships, take alternate shots and sink the enemy fleet.
Each PC keeps its own ships private. Chat shares the cable.

![A fleet placed and ready on the VGA desktop.](shot:wirefleet-ready)

## Connect and deploy

Open `WIREFLET.O88` on each PC. Wait for the serial mouse to work, then
choose **Link > Connect COM1** or **Connect COM2** for the other port.
The connection uses 9600 baud, 8N1, without flow control. An occupied port
is refused. **Disconnect** releases the port.

Select a ship with `1` through `5` or its name. Click your grid to place
it, or move with the arrows and press `Enter`. `R` rotates. Right-click
a ship to remove it. Ships may touch; they cannot overlap or cross an edge.

The **Fleet** menu offers **Rotate R**, **Auto A** and **Clear C**.
**Game > Ready [Y]** locks a complete fleet. When both PCs are ready, the
match chooses its first captain. Select an enemy square and click **Fire**,
or use arrows and `Enter`. A dot is a miss, a cross is a hit, and a sunken
ship's squares are marked. Sinking all five ships wins.

## Chat and connection loss

Press `T` or click **Chat**. Type up to 32 characters, then `Enter` to send;
`Esc` cancels. The compact CGA desktop shows the latest chat line.

If the cable goes quiet, firing pauses. Ships, shots and an unanswered
shot stay in memory. Reconnect the same PCs to resume. A history mismatch
keeps the match paused. **Game > New fleet [N]** starts over; both captains must
start a new match together.

## Save and resume

**Game > Save S** opens the file chooser with `FLEET.WFL` as its proposed
name. Save on both PCs before closing them. **Load O** opens each PC's own
saved file; connect afterward. Ships, shots, turn, opponent identity and
pending commands return. Invalid files leave the current match intact.

These files contain private ship positions. Keep your own file on your PC.

## Display and sound

`F` or `Alt+Enter` toggles fullscreen; `Esc` returns. VGA has color on the
desktop, CGA has monochrome on the desktop and four colors fullscreen,
and Hercules uses monochrome. Save and Load return to the desktop chooser.

**Game > Sound M** mutes or enables effects. The system Sound setting
chooses PC Speaker, AdLib or Sound Blaster. `H` opens controls; **About
WireFleet** is in the WireFleet menu.
