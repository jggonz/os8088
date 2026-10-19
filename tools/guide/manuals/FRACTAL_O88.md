# Fractal

Fractal draws five famous fractal pictures, starting with the Mandelbrot
set. Pick a picture and a colour scheme from the menus, then click anywhere
on it to zoom in on that spot. The drawing goes on in the background, so you
can keep using the desktop, move windows and run other programs while it
works.

![The Mandelbrot set, finished, in the Spectrum colours.](shot:fractal-window){full}

## Starting Fractal

Open the disk that holds it, then the `APPS` folder, and double-click
**FRACTAL.O88**. It starts drawing the Mandelbrot set at once. You can open
more than one Fractal; each draws its own picture.

Fractal runs on every adapter. On a colour screen it starts in the Spectrum
colours. On a black-and-white screen it starts in Contour, the scheme made
for those screens.

## Watching it draw

A whole picture takes a minute or two on an IBM PC/XT. Fractal does not make
you wait for all of it: the first pass paints the whole picture in coarse
bands four rows tall, and two more passes fill in the detail.

![A quarter of the way through: the whole picture is there, in coarse bands.](shot:fractal-progress){right}

The strip along the top of the window shows the picture's name, the zoom
level, how much of the picture is done, and the colour scheme.

Moving the window, covering it or minimizing it does not start the picture
again. When the window comes back, Fractal puts back what it had already
drawn and carries on from there. Changing the picture, the colours or the
zoom starts a new drawing.

## Zooming in

![One click on the small bulb at the top: Zoom 1, centred on the click.](shot:fractal-zoom){left}

Click a point on the picture. Fractal moves that point to the middle and
zooms in one level, so each click takes you closer to the spot you clicked.
There are five levels, `Zoom 0` to `Zoom 4`. At `Zoom 4` a click still moves
the picture but goes no deeper.

**View > Zoom Out** goes back one level, and **View > Reset** goes back to
the picture's starting view.

## The Fractal menu

![The second Fractal menu: the five pictures.](shot:fractal-menu){right}

The menu bar has two menus called Fractal. The first holds **About
Fractal**, which shows who made it (click the picture to put it away), and
**Close**. The second chooses the picture:

| Item | What it draws |
|---|---|
| Mandelbrot | The Mandelbrot set |
| Julia Dendrite | A Julia set made of thin, branching threads |
| Julia Rabbit | The "rabbit" Julia set, a chain of fat round blobs |
| Burning Ship | The Burning Ship fractal, a ship with its hull to the lower left |
| Tricorn | The three-cornered cousin of the Mandelbrot set |

Each picture opens at its own starting view, at `Zoom 0`.

![Julia Rabbit in the Ice colours.](shot:fractal-julia){full}

## The Colour menu

| Item | What it does |
|---|---|
| Spectrum | Bands through twelve colours of the rainbow |
| Fire | Grey through red, orange and yellow to white, and back |
| Ice | Grey through blue and cyan to white, and back |
| Contour | White and grey bands, made to read well on a black-and-white screen |

The inside of every fractal is black in every scheme.

## The View menu

| Item | What it does |
|---|---|
| Zoom In | Zooms in one level on the middle of the picture |
| Zoom Out | Zooms out one level |
| Reset | Goes back to this picture's starting view |
| Redraw | Starts the picture again from the beginning |

## Good to know

- Fractal has no keys. Everything is a click or a menu item.
- The window is a fixed size and cannot be resized.
- Fractal does not save pictures.
- If the machine is already running as many background jobs as it can, the
  picture area says **No free task slot for the render**. Close another
  program, then choose **View > Redraw**.
