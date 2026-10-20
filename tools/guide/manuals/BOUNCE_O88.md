# Bounce

Bounce is a tiny demonstration program. A small black square bounces around
inside its own window, turning round whenever it meets an edge. It moves on
its own while you carry on working in other windows.

![A Bounce window with the square on its way to the bottom edge.](shot:bounce-window){full}

## Starting Bounce

Open the disk that holds it, then the `APPS` folder, and double-click
**BOUNCE.O88**. Bounce needs nothing special and runs on every machine.

You can open several Bounces at once. Each new one opens on top of the
last, so drag it aside to see them both. Each square starts in the top-left
corner and keeps its own path.

![Two Bounces side by side, each with its own square.](shot:bounce-two){full}

## Watching it

There is nothing to do but watch. The square takes a small step about nine
times a second, three pixels across and two down, and bounces off each edge
it meets.

When another window covers part of a Bounce, the square still crosses the
part you can see. When the window is covered completely or minimized, the
square keeps moving without being drawn, and it is wherever it has got to
when the window comes back.

## The Bounce menu

| Item | What it does |
|---|---|
| About Bounce | Shows a small card with the program's name. Click in the window to put it away |
| Close | Closes this Bounce |

## Good to know

![The About card.](shot:bounce-about){right}

- Bounce has no keys, no settings and saves nothing.
- The square is not drawn while the About card is up. Click in the window
  to put the card away, and the square is back where it has bounced to.
- If the machine is already running as many background jobs as it can, a
  new Bounce opens with its square standing still in the corner. Close
  another program and the square starts moving the next time the window is
  redrawn.
