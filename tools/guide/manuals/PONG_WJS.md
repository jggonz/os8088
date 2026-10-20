# Pong script

`PONG.WJS` is Weave Pong's game logic. **Serve** puts the ball in the
middle and sends it towards each end in turn. `A` moves your paddle up and
`Z` down. The computer's paddle follows the ball once it crosses the
middle, one pixel a frame at most, so a steep return can beat it. Where the
ball strikes a paddle sets its angle, each return is a little faster, and
a hit and a point each play a tone. The score is kept as "left : right".

![The Pong app these files make, running in Weave.](shot:weave-pong)

## Where it is

In the `LOOM` folder of the Weave disk and the Loom disk, in every size,
and of the second disk of the everything set. The compiled app it belongs
to is in the `WEAVE` folder beside it.

## Using it

Double-click **PONG.WJS** and Loom ([[LOOM.O88]]) opens the Pong project; click
**PONG.WJS** in the list on the left to edit the script. Its notes explain each handler, so it is the place to make
the computer harder or easier to beat. The packed game is [[PONG.WAB]].
