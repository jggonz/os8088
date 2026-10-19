# Screen saver

`SAVER.DRV` is the animated screen saver. When nobody has touched the mouse
or the keyboard for a while, it fills the screen with a moving picture, so
a machine left alone still shows that it is running. Move the mouse or
press a key and the desktop comes back exactly as you left it.

![Fish and sea life, one of the four pictures.](shot:saver-drv-fish)

## What is inside it

![The spinning cube, caught face on as it turns.](shot:saver-drv-cube){right}

Four pictures, and the settings window that chooses between them:

- **Spinning Cube**: a wire cube turning in space.
- **Flying Starfield**: stars streaming towards you.
- **Geometric Shapes**: figures that draw themselves and dissolve.
- **Fish and Sea Life**: fish and jellyfish swimming across the screen.

When more than one picture is ticked, the saver changes to another one
after a set number of seconds.

![Geometric shapes.](shot:saver-drv-shapes){left}
![The flying starfield.](shot:saver-drv-stars){right}

## Why it is on the system disk

The saver takes no memory while you work. The system reads it from the
system disk when the idle time runs out, and frees it when you come back.

If it cannot be read, for example because the system disk is not in drive
A:, the screen simply goes dark instead, and comes back the same way. The
small system for 128KB machines always does that: it has the dark screen and
no pictures.

## Settings

![The Theme page of the Control Panel, with the Screen Saver button.](shot:saver-drv-theme){right}

Open the **Control Panel**, choose **Theme**, and click **Screen Saver**.
The settings open in a window of their own.

![The screen saver's settings window.](shot:saver-drv-settings){left}

| Setting | What it does |
|---|---|
| The four boxes | Which pictures it may show. With none ticked, the screen just goes dark |
| **Change every** ... **seconds** | How long one picture runs before another is chosen. 30 to start with |
| **Start after** ... **minutes (0 = off)** | How long the machine must be left alone first, up to 59 minutes. 5 to start with. **0** turns the screen saver off altogether, the dark screen included |
| **Test** | Starts the screen saver now with these settings, so you can look at them. Move the mouse to come back to the window |
| **Ok** | Uses the settings and closes the window |
| **Cancel** | Closes the window without using what you changed since the last **Test** |

The settings take effect as soon as you press **Ok** or **Test**. They are
written to the system disk when you close the Control Panel.

If the button cannot open the window, the Control Panel says why, for
example that the system disk is not in drive A: or that there is not enough
memory.

## Good to know

- The key or click that wakes the screen does nothing else, so it cannot
  hit a window you could not see.
- A program that is busy working, such as the FTP Server during a transfer,
  can ask for the plain dark screen instead, so the pictures do not slow it
  down.
- There is no checkbox for it on the Drivers page; the Theme page's button
  and the idle time are all it needs.
