# WireFleet

WireFleet is a naval battle for two PCs connected by a null modem serial
cable. Place five ships, take alternate shots and sink the enemy fleet.
Each PC keeps its own ships private. Chat shares the cable.

![A fleet placed and ready on the VGA desktop.](shot:wirefleet-ready)

## Connect and deploy

Open `WIREFLET.O88` on each PC. Each one looks for the other by itself on
whichever COM port the mouse is not using; if the mouse has not moved yet,
move it once. The status line names the port, and a light shows the link:
green connected, yellow looking, red stopped. The connection uses 2400
baud, 8N1, without flow control.

The **Link** menu is the manual route: **Automatic** looks again,
**Connect COM1** or **Connect COM2** uses that port only, and
**Disconnect** releases the port, and **Statistics** shows what the cable
has delivered: bytes, bytes with line errors, frames and refused frames.

Select a ship with `1` through `5` or its name. Click your grid to place
it, or move with the arrows and press `Enter`. `R` rotates. Right-click
a ship to remove it. Ships may touch; they cannot overlap or cross an edge.

The **Fleet** menu offers **Rotate R**, **Auto A** and **Clear C**.
**Game > Ready [Y]** locks a complete fleet. When both PCs are ready, the
battle starts and the title says whose turn it is. Select an enemy square
and click **Fire**, or use arrows and `Enter`. A missile launches from one
of your ships and flies to that square; on the other PC it flies in to the
same square. It explodes on a hit and splashes on a miss, and the result
shows when it lands. A dot is a miss, a cross is a hit, and a sunken
ship's squares are marked. Sinking all five ships wins.

## Chat and connection loss

Press `T` or click **Chat**. Type up to 32 characters, then `Enter` or
**Send**; `Esc` cancels. Chat works while ships are still being placed. The
compact CGA desktop shows the latest chat line.

If the cable goes quiet, firing pauses. Ships, shots and an unanswered
shot stay in memory. Reconnect the same PCs to resume. A history mismatch
keeps the match paused. **Game > New fleet [N]** starts over. Before the
first shot the other PC follows by itself; after a finished match both
captains press `N`.

## Save and resume

**Game > Save S** opens the file chooser with `FLEET.WFL` as its proposed
name. Save on both PCs before closing them. **Load O** opens each PC's own
saved file, which then looks for the same opponent. Ships, shots, turn, opponent identity and
pending commands return. Invalid files leave the current match intact.

These files contain private ship positions. Keep your own file on your PC.

## Display and sound

`F` or `Alt+Enter` toggles fullscreen; `Esc` returns. VGA and EGA show a
color layout: a title bar, a ships list with check boxes, both boards,
the last shot each way and a chat panel. CGA has monochrome on the desktop
and four colors fullscreen, and Hercules uses monochrome. Save and Load return to the desktop chooser.

A sea march plays while the window is open and goes quiet whenever a
launch, an explosion or a splash sounds. **Game > Music U** turns the music
off or on; **Game > Sound M** mutes every sound. The system Sound setting
chooses PC Speaker, AdLib or Sound Blaster: AdLib plays the music in three
voices, a Sound Blaster also plays sampled effects, and the PC Speaker plays
the tune alone. `H` opens controls; **About
WireFleet** is in the WireFleet menu.
