# Mouse Check

Mouse Check is for a machine whose mouse is not working. It shows what os8088
found, and did not find, for every kind of pointing device it supports: serial
mice on COM1 to COM4, the PS/2 mouse port, the VMware absolute pointer, the
USB mouse driver and the keyboard mouse. Each section has a lamp, and a one
line verdict at the top says whether a mouse is working and, if not, where to
look. On a machine with a PS/2 port it can also ask the keyboard controller
again, step by step, and show you where the conversation with the mouse stops.

![Mouse Check showing the Summary and the start of the serial mice.](shot:mousechk-window){full}

## Starting Mouse Check

Mouse Check lives in the `SYSTEM` folder of the system disk. On a 360KB
machine it is at the root of the utilities disk instead. Open the disk, then
`SYSTEM`, and double-click **MOUSECHK.O88**. Only one copy is needed; the
window updates itself twice a second.

It needs no mouse. If no mouse works, the keyboard mouse is on (below), and
everything in this window can also be reached with keys.

## What the window shows

The list is one long page of sections. Each starts with a heading and a lamp.
On a colour screen the lamp is blue for information, green for OK, brown for
a warning and red for a problem, with a hollow grey one for a section that
does not apply to this machine. On a black and white screen the lamp has a
different shape instead: an arrow, a tick, checks, a solid square or a hollow
one.

| Section | What it tells you |
|---|---|
| Summary | Which pointer is in use, its position and buttons, the kernel, the CPU and the display, and the verdict line |
| Serial mice | For each COM port: whether a serial port answers there, what the mouse sent when the system asked it to identify itself, whether it answered like a mouse, whether it won, and the state of the port's lines |
| PS/2 mouse | How far the system got talking to the PS/2 mouse at start-up, the device it found, and how the keyboard controller was left by the BIOS |
| VMware absolute pointer | Whether the pointer used by virtual machines is in use, and what it needs |
| USB mouse | Whether the USB mouse driver is loaded and in use, and what it needs |
| Keyboard mouse | Whether the keyboard mouse is on |

The verdict line reads **Your mouse is working** with the kind of mouse, **A
PS/2 mouse answered. Move it to confirm.**, or **No mouse found. See the
sections below for why.** While no pointer has worked, one more section,
**If your mouse is not detected**, lists the usual causes.

![The end of the list: the PS/2 mouse, the VMware, USB and keyboard sections.](shot:mousechk-page2){full}

On a machine with a PS/2 port, the PS/2 mouse section shows how far start-up
got (a number of steps), the device ID, the command byte the BIOS left in the
keyboard controller and a verdict. A mouse that is plugged in and healthy shows
the full set of steps and an ID. A section that stops part way tells you which
step failed. An 8088 PC has no PS/2 port at all, so there the section is a
single row saying so, as in the picture.

## Using it without a mouse

When no pointer has worked, the keyboard mouse is on: the arrow keys move the
pointer and keypad **5** clicks. Space or Ins also click, and Del
right-clicks. Turning ScrollLock on gives those keys back to the program.
The keyboard mouse switches itself off for the session as soon as any real
pointer works.

Because the keyboard mouse takes the arrows, Page Up, Page Down, Home, End
and Space, those keys do not scroll the list while it is on. Mouse Check has
its own keys for that, and for its buttons:

| Key | What it does |
|---|---|
| **R** | Refresh now |
| **P** | Probe PS/2 |
| **S** | Save Report |
| **C** | Copy the report |
| **Q** or **Esc** | Quit |
| **N** / **B** | Scroll a page on / back |
| **T** / **E** | Go to the top / the end |
| **+** / **-** | Scroll a line down / up |
| **Ctrl+Page Down** / **Ctrl+Page Up** | A page on / back |
| **Ctrl+Home** / **Ctrl+End** | The top / the end |

The window reminds you of the scrolling keys beside the buttons. Once a real
pointer works, or with ScrollLock on, the cursor keys, Page Up, Page Down,
Home, End and Space scroll the list too. On a screen too small to show every
line, there is also a scroll bar.

## Probe PS/2

**Probe PS/2** runs the PS/2 handshake again, one step at a time, and shows
what each step got back. It asks the keyboard controller to switch the mouse
port on, resets the mouse, asks what it is, and then tries the other ways
there are of reaching it. It puts the controller back the way it found it. The
steps appear in the PS/2 section, and they are part of the report, so a person
helping you can read exactly where the answer stopped.

![Pressing the greyed Probe PS/2 button shows why, at the top of the screen.](shot:mousechk-probe){full}

The button is greyed, and pressing it or **P** shows the reason, when a probe
would not tell you anything or could do harm:

| Reason shown | What it means |
|---|---|
| No 8042 on an 8088 PC | An 8088 has no keyboard controller with a mouse port |
| No 8042: 64h reads FF | Nothing answers where the keyboard controller should be |
| No kernel mouse record | The system gave Mouse Check no mouse record to compare against |
| Kernel's PS/2 mouse live | The PS/2 mouse is already your working pointer |
| Kernel already found it | The system already found the mouse at start-up; the PS/2 section says everything a probe could |
| Kernel's IRQ12 is armed | The system is already listening for the mouse |

So the probe is for the machine whose PS/2 mouse was not found at start-up,
and it is greyed on one where the mouse is already working. On a PS/2
machine with no mouse found, it is available.

**Do not type during a probe.** It takes about a second and a half and switches
the keyboard off and on, so a key pressed then can be lost or seem to stick.
Press the key once, then leave the keyboard alone until the trace appears.

If the verdict says **The keyboard may be off: restart now.**, the keyboard did
not come back after the probe. Restart the machine; nothing is damaged.

After a probe, a line offers a second press of **Probe PS/2** for ten seconds.
That second press asks the BIOS's own mouse routines the same questions, which
is useful to compare with the system's answer. It is only offered where the
first probe showed the controller answering normally, and it is not offered on
the 286 and later protected-mode kernel. If a BIOS has a mouse routine that
never returns, the desktop waits with it, so only press it when you want that
comparison.

## Saving and copying the report

**Save Report** (S) opens the Save dialog with `MOUSECHK.TXT` already in the
name box. It starts in the `MEDIA` folder of the disk Mouse Check was started
from, so pressing Enter saves it there on a startup disk. Choose another drive
or folder in the dialog if the startup disk cannot be written to, such as a
second floppy. A message says **Saved** with the name, or **Save failed**.

**Copy** (C) puts the same text on the clipboard and says **Report copied**.

The report is the whole list as plain text: the date, the build, and every
section with its lamp and rows, including any probe trace. **It is what to send when you ask someone for
help with a mouse.**

## The buttons

| Button | What it does |
|---|---|
| Refresh | Rebuilds the list now |
| Probe PS/2 | Runs the probe above |
| Save Report | Writes the report to a file |
| Copy | Puts the report on the clipboard |
| Quit | Closes Mouse Check |

## Good to know

- A mouse that has worked at any time this session still reads as working,
  even if you have since unplugged it. Restart to see what happens at start-up.
- QEMU, 86Box and a real machine often differ. An emulator's PS/2 port is
  nearly always fine, so a problem found on real hardware may not show there.
- An 8088 PC has no PS/2 port. Use a serial mouse, or the USB mouse driver
  with its card.
- If you have one mini-DIN socket for both keyboard and mouse, as some
  single-board computers do, it needs a Y-splitter cable.
- On a PS/2 machine, plug the mouse into the mouse socket, not the keyboard
  socket, and check BIOS Setup: the PS/2 mouse option should be enabled and
  IRQ12 should not be reserved for an ISA card.
- A serial mouse needs a COM port, and a PS/2 mouse a PS/2 port. If neither
  is found, run **Probe PS/2** and read where it stops.
