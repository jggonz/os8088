# Startup items

`AUTOSTRT` is the folder of things to open every time the machine starts.
Put a link in it and, as soon as the system has finished loading, the link
is opened just as if you had double-clicked it: a link to a program starts
the program, a link to a document opens it in its program, and a link to a
folder opens a Disk window on that folder.

## Where it is

The folder must be at the top level of the disk the machine starts from,
beside `SYSTEM` and `APPS`. Every system disk carries it, empty: all four
sizes, the emulator system disk and the live USB image and CD. If it has
gone, make it again (see below). An `AUTOSTRT` folder on any other disk, or
inside another folder, does nothing.

## Adding a program

Only links are started. A program, document or note copied into the folder
itself is ignored, so it is safe to leave a reminder there.

1. Open the boot disk (**A:**) and double-click `AUTOSTRT` to open it in a
   Disk window.
2. Open another Disk window on the program you want, for example
   `B:/APPS/CALC.O88`.
3. Hold **Alt** and drag the program into the `AUTOSTRT` window. When you
   let go, a link called `CALC.LNK` appears there. The program itself stays
   where it was.

The next start opens the Calculator by itself. Several links start several
things, one after another.

Some programs write their own link. REDLINE's **Apply on boot** button puts
`RLAPPLY.LNK` here, and a shortcut saved by the DOS box starts its DOS
program when it is placed here.

If the folder has been deleted, open **A:**, choose **New Folder** from the
Disk window's File menu (or press `N`), and name it `AUTOSTRT`.

## Skipping and removing

- Hold **Shift** while the machine starts to skip everything in the folder
  for that one start. The menu bar says `Autostart skipped`.
- To stop a program starting, delete its link from the folder in a Disk
  window. The program is not touched.

## Good to know

- A link names a drive. A link to a program on drive B needs its disk in B
  when the machine starts; if it is missing, the desktop says
  `Shortcut not found (B:)` and the start carries on.
- A link made by dragging from another drive keeps pointing at that drive,
  even if the disk in it changes.
- The boot always reaches the desktop. A link that cannot be followed is
  passed over.
