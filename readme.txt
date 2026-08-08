============================
  os8088 - READ ME FIRST
============================

os8088 is a small graphical operating system for the IBM PC, XT and compatibles. It needs an 8086 or 8088, 128K of memory, one floppy drive, and a VGA, Hercules or CGA display card. A serial mouse is recommended but not required.

This file explains how to use it. No programming knowledge is needed. It is a plain text file, so you can widen this window and it will re-flow to fit.

Version 1.0


CONTENTS

  1  Starting up
  2  The screen
  3  Using the mouse
  4  Windows
  5  Menus
  6  Disks and folders
  7  Working with files
  8  Running programs
  9  Opening and saving
 10  The Control Panel
 11  The clock
 12  If you have no mouse
 13  All the keys
 14  Messages explained
 15  Limits to know about
 16  Restarting and
     switching off


----------------------------
1. STARTING UP
----------------------------

os8088 comes on two disks: the system disk, which is the OS, and the apps disk, which holds the programs.

Put the system disk in drive A: and switch the machine on. A welcome screen with a progress bar appears while the system loads; on a 4.77MHz machine that takes a few seconds. When the desktop appears, os8088 is ready.

With two floppy drives, put the apps disk in drive B: and leave it there.

With one drive, swap disks when you want a program: take the system disk out, put the apps disk in, and press R (or click Refresh) in a disk window. The apps disk then answers as A:.

Both disks are ordinary FAT floppies, so you can also read and write them on a DOS PC or a modern computer.


----------------------------
2. THE SCREEN
----------------------------

MENU BAR
The strip across the top. From left to right: the system menu (the small logo), the name of the program you are using, that program's own menus, and the clock.

DESKTOP
The patterned area behind everything. A disk icon sits near the right edge for each drive: Disk A, Disk B, and HDD C onwards if a hard disk is set up.

DOCK
The white strip along the bottom, with one small tile per running program. A heavy border marks the one you are using; a tile drawn in reverse is a program put away out of sight. Click a tile to bring it back.


----------------------------
3. USING THE MOUSE
----------------------------

os8088 works with a serial mouse on COM1 or COM2. Plug it in before switching the machine on. The system finds it on its own - there is nothing to set up and no driver to load. Move it for a moment if the pointer has not appeared yet.

  Click        press and
               let go once
  Double-click two clicks,
               quickly
  Drag         hold the
               button down
               while moving

The right button, inside a disk window, opens a short menu of commands for whatever is under the pointer.

No mouse? See section 12.


----------------------------
4. WINDOWS
----------------------------

A window has a title bar along its top. The front window's bar is striped and has a small box at each end:

  Left box   close the
             window
  Right box  put it away
             into the dock

Putting a window away does not close it. The program keeps running and its dock tile brings it back.

To move a window, drag its title bar. An outline follows the pointer and the window moves when you let go.

Disk windows and some programs can be resized: drag the small grow box in the bottom right corner.

Clicking any part of a window brings it to the front. Only the front window receives what you type, so click a window before typing into it.


----------------------------
5. MENUS
----------------------------

Press the mouse button on a menu title and hold it down. The menu drops. Slide down to the item you want and let go. Letting go off the menu, or on a greyed item, does nothing. Greyed items cannot be used just now.

THE SYSTEM MENU
The logo at the far left. It is the same in every program:

  About os8088...
  Control Panel
  Task Manager

About os8088 gives the version and the display card found at start-up.

Task Manager lists what is running, how much of the processor each part uses, and where the memory has gone. Click inside it to switch between its two views. It only reports - it cannot stop a program.

THE DESKTOP'S OWN MENUS
When no program is in front, the menu bar reads Locator:

  File     Timer, Bounce,
           Disk, Close
           Window
  Special  Restart

Disk opens a disk window, Timer is a stopwatch, and Bounce is a toy. Clicking the bare desktop always brings these menus back.


----------------------------
6. DISKS AND FOLDERS
----------------------------

Double-click a disk icon to open a disk window. Up to four can be open at once, each on its own disk or folder.

A disk window shows a header line (which drive, how many items), the list of items, a scroll bar, and a status line at the bottom.

Two buttons sit at the top right. Refresh re-reads the disk - use it after swapping a floppy. The other switches between the list view and the icon view.

TO OPEN SOMETHING
Double-click it. A folder opens in the same window, a program starts, and a document opens in the program that made it if that program is on either disk.

TO GO BACK UP
Press Backspace, or use Folder > Up One Folder. The top level of a disk is called the root and there is nothing above it. Every folder also lists ".." at the top, which goes up one level.

THE STATUS LINE
When nothing else needs saying it shows the size of what is listed and the room free on the disk. Otherwise it reports what is happening, or why something did not work.

THE MENUS
While a disk window is in front the menu bar carries File (open, rename, delete, cut, copy, paste), Folder (new window, refresh, move about, change drive), View (list or icons) and Special.


----------------------------
7. WORKING WITH FILES
----------------------------

Click an item once to select it.

NEW FOLDER
File > New Folder..., or press N. Type a name on the status line and press Enter. Esc cancels.

RENAME
Select the item, then File > Rename.... Type the new name and press Enter.

DELETE
Select the item, then File > Delete. The status line asks you to confirm. Enter means yes; ANY other key means no.

There is no undo and no wastebasket. A deleted file is gone, and deleting a folder deletes everything inside it.

COPY AND MOVE

  Cut    File > Cut,
         or Ctrl+X
  Copy   File > Copy,
         or Ctrl+C
  Paste  File > Paste,
         or Ctrl+V

Paste puts the item into the folder the window is showing, which may be on another disk. A cut item moves; a copied item can be pasted again and again. Folders copy whole, with everything inside them.

You can also drag an item onto a folder to move it there, including a folder in another window. Dragging always moves - it never copies.

If a file of that name is already there you are asked. Enter replaces that one, A replaces every one without asking again, Esc stops.

NAMES
Up to eight characters, then optionally a dot and up to three more: TEXT.TXT, NOTES, WAVE-1. Letters, digits and - _ # $ % & @ are fine; spaces and most other punctuation are refused as you type them. Lower case becomes capitals.


----------------------------
8. RUNNING PROGRAMS
----------------------------

Programs live on the apps disk in the folders APPS and GAMES. Open a disk window on that disk, open the folder, and double-click a program.

Several programs can run at once, and most can be started more than once. The machine shares time between them, so a game keeps moving while a file is being copied.

TO STOP A PROGRAM
Click the close box at the left of its title bar. Save your work first - nothing is saved for you.

If a program will not start, the status line of the disk window says why: see section 14. A smaller machine runs fewer at once because memory runs out; the Task Manager shows how much is left.


----------------------------
9. OPENING AND SAVING
----------------------------

Programs that read and write documents put up the same box for both jobs.

  The list    the current
              folder;
              double-click
              a folder to
              go into it
  Open/Save   does the job
  Cancel      changes
              nothing
  Drive       switches to
              the other
              floppy
  New Folder  (saving only)
              makes a folder
              named as typed
              below, and
              goes into it

When saving, type the name in the box at the bottom. The naming rules of section 7 apply, and the box refuses anything the disk cannot store.

Selecting a file while saving copies its name into the box. That does not replace it - you still have to press Save.

The box remembers, for each program, where you last used it.


----------------------------
10. THE CONTROL PANEL
----------------------------

System menu > Control Panel. The list on the left picks a page.

SCHEDULER
How the machine shares time between programs. Leave it on Pre-emptive. Cooperative lets one program hold the machine until it gives way: a little faster, much less smooth.

DISPLAY
Direct to screen, or Double buffered. Double buffered draws more smoothly but needs 150K of spare memory and a VGA card, so it is greyed out on a smaller machine. Close a program and it may come back.

DATE/TIME
See section 11.

DRIVERS
Optional extras, off until you switch them on. Tick a box to load one now. Under each name it says whether it loaded, or why it did not. Sound is here, and so is hard disk support.

SOUND
Which sound hardware to use: PC Speaker, AdLib or Sound Blaster. Hardware the machine does not have is greyed out. The Test button plays a tone through whatever is picked, so you can hear whether it works.

*** IMPORTANT ***
Settings are written to the system disk when you CLOSE the Control Panel - the box at the LEFT of its title bar. Putting it away into the dock does not save them. If the system disk is not in drive A:, or it is write-protected, nothing is remembered and the machine starts up with the old settings.


----------------------------
11. THE CLOCK
----------------------------

The time sits at the right of the menu bar. Click it to jump straight to the Date/Time page of the Control Panel.

To set the clock, click a field - month, day, year, hour, minute, second - and use the + and - buttons. Two options sit below: 12-hour clock, and seconds in the menu bar.

The bottom line names the clock chip the machine has, or says none. With no clock chip os8088 keeps time while it is switched on but starts from a fixed date each time; you can still set it by hand.

Close the Control Panel so the settings are kept.


----------------------------
12. IF YOU HAVE NO MOUSE
----------------------------

With no mouse attached, the arrow keys become one.

  Arrow keys   move the
               pointer.
               Hold a key
               down and it
               speeds up.
  Space, or    the left
  keypad 0,    button
  or keypad 5
  Del          the right
               button

A press is a click for buttons and icons, and a hold for menus and drags: press over a menu title, arrow down, press again on the item. Two presses close together are a double-click.

While this is on, programs cannot see the arrow keys. Press SCROLL LOCK to hand the whole keyboard to the window under the pointer, and Scroll Lock again to get the pointer back. On a keyboard with a Scroll Lock lamp, the lamp tells you which mode you are in.

If a mouse IS attached none of this happens and the arrow keys go to programs as usual.

If your mouse is not found, check that it is a serial mouse, that it is on COM1 or COM2, and that it was plugged in before the machine was switched on.


----------------------------
13. ALL THE KEYS
----------------------------

Keystrokes go to the FRONT window only. With no window open the keyboard does nothing.

DISK WINDOWS

  A          show drive A:
  B          show drive B:
  R          re-read this
             disk
  V          list / icons
  N          new folder
  Backspace  up one folder
  Enter      open the
             selected item
  Up, Down   scroll a line
  PgUp,PgDn  scroll a page
  Ctrl+X     cut
  Ctrl+C     copy
  Ctrl+V     paste

Rename and Delete have no key on purpose: one stray keypress must not be able to destroy a file.

WHILE TYPING A NAME

  Enter      accept
  Esc        cancel
  Backspace  rub out one

CONFIRMING A DELETE

  Enter      yes
  any other  no

WHEN ASKED TO REPLACE

  Enter      replace it
  A          replace all
  Esc        stop

OPEN AND SAVE BOX

  Up, Down   move the
             selection
  PgUp,PgDn  move a page
  Enter      open or save;
             on a folder,
             go into it
  Esc        cancel

While saving, type the name. Left, Right, Home and End move within it; Backspace and Del rub out.

WITH NO MOUSE
See section 12: the arrows move the pointer, Space is the button, and Scroll Lock hands the keyboard back to the program.


----------------------------
14. MESSAGES EXPLAINED
----------------------------

These appear on the status line of a disk window, or in a small notice window.

No disk
  Nothing readable in the
  drive. Check that a disk
  is in it and the door is
  shut.

Disk error
  The drive could not read
  or write. Try again, or
  try another copy of the
  disk.

No os8088 disk (B:)
  There is a disk, but not
  one os8088 understands.

Write protected
  The disk's tab is set to
  protect it. Move the tab
  and try again.

Disk full / Folder full
  No room. The top level of
  a disk holds a limited
  number of items - make a
  folder and put things
  inside it.

Bad name / Name exists
  See the naming rules in
  section 7.

Protected
  A system file. os8088
  will not let you change
  or delete the parts of
  itself it needs.

Bad package / Load failed
  Not a program, or a
  damaged one. A data file
  nothing claims gives this
  when double-clicked.

Too large
  The file is bigger than
  the program can take in.

Out of memory
  Not enough free memory.
  Close something and try
  again.

NAME.O88 - not on this disk
  You opened a document but
  the program that reads it
  is on neither floppy. Put
  the apps disk in and try
  again.

RAM
  (at start-up) The machine
  has too little memory to
  run os8088.

os8088: disk error
  (at start-up) The system
  disk could not be read.
  Try again, or use another
  copy.


----------------------------
15. LIMITS TO KNOW ABOUT
----------------------------

Four disk windows can be open at once.

A folder lists at most 32 items. It can hold more, but only the first 32 are shown, so keep folders small.

How many programs run at once depends on free memory, not on a fixed number.

There is no undo anywhere in the system, and nothing is saved for you. Save your work before closing a program.


----------------------------
16. RESTARTING AND
    SWITCHING OFF
----------------------------

TO RESTART
Special > Restart, on the desktop or in a disk window. The machine reboots at once, so close your work first.

TO SWITCH OFF
There is no shut-down command and none is needed. Close what you were working in, wait for the drive light to go out, and switch off.

Never switch off or take a disk out while the drive light is on: that is when it is being written to.


----------------------------

os8088 is free software, under the MIT licence. Enjoy it.
