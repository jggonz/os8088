============================
  os8088 - READ ME FIRST
============================


CONTENTS

  1  The chip menu
  2  Windows, dock, desktop
  3  Disk windows
  4  Files and disks
  5  Programs
  6  Control Panel
  7  Drivers
  8  Linking to a DOS PC
  9  Hard disk, hibernate
 10  No mouse
 11  Messages
 12  Limits, switching off


----------------------------
1. THE CHIP MENU
----------------------------

The chip at the left of the menu bar is the system menu, in every program:

  About os8088  version,
                build,
                display
  Control Panel section 6
  Task Manager  what runs,
                and memory
  Hibernate     section 9
  Restart       reboot now
  Shut Down     save
                settings,
                stop safely

Task Manager only watches: click it to step through processes, memory map and heap. It cannot stop a program.

Beside the chip, the front program's name is a menu: its About, and Close.

Click the clock to set it. Short messages appear in the bar and fade.


----------------------------
2. WINDOWS, DOCK, DESKTOP
----------------------------

The left title box closes, the right puts the window into the dock; both act on release, so slide off to change your mind. Double-click a title bar to fill the screen; again to restore. Right-click a back window to raise it. Keys go only to the front window.

DOCK
One tile per program. A click restores, puts away or raises it; right-click offers Close. Control Panel > Dock moves it Left or Right and can auto-hide it to a 1-pixel line that opens when the pointer rests on it.

DESKTOP
Drag an item from a disk window onto the desktop to make a SHORTCUT. Enter opens it; Del or File > Remove Shortcut removes it (the file stays). Up to 15, kept in SYSTEM.CFG on A:.

Hold Alt as you drop an item on a disk window to make a LINK (NAME.LNK) there; the original stays put. Links in AUTOSTRT at the boot disk's root open at every start; hold Shift to skip them.

With no window in front the bar reads Locator: File (Close Window, Remove Shortcut) and Builtins (Timer, Bounce, Disk). Builtins > Disk opens a disk window.

SCREEN SAVER
After 5 idle minutes; the wake-up key is swallowed. Control Panel > Theme > Screen Saver picks the scenes and timing, or 0 for off.


----------------------------
3. DISK WINDOWS
----------------------------

Double-click a drive icon. Up to four windows. Refresh re-reads after a floppy swap; the other header button switches list and icons. The bottom line shows sizes and free space.

Double-click opens a folder, runs a program, or opens a document in its program (found on any mounted disk). Right-click an item or empty space for a menu, including Open in New Window and Paste Into.

FILE MENU
Open, New Folder, Rename, Delete. Compress packs a file (programs still read it, so it only saves room); Uncompress undoes it, Uncompress To joins a split set. Format Disk, Clone Disk (floppy to floppy or to a .IMG file) and Write Img (an image back onto a floppy) need a floppy window.

KEYS
  Enter     open
  Backspace up one folder
  A, B      show A:, B:
  R         re-read
  V         list / icons
  N         new folder
  Del       delete: press
            Del again to
            confirm
  Up, Down  move selection
  PgUp,PgDn by a page
  Ctrl+X    cut
  Ctrl+C    copy
  Ctrl+V    paste


----------------------------
4. FILES AND DISKS
----------------------------

The system disk is the OS; the apps disk holds programs - keep it in B:, or swap and press R. Both are plain DOS FAT floppies.

Names are 8.3 capitals: letters, digits and $ % ' - _ @ ~ ` ! ( ) { } ^ # &. New Folder and Rename ask on the status line: Enter accepts, Esc cancels.

There is no undo and no wastebasket. Deleting a folder deletes its contents.

Paste copies into the folder shown; folders copy whole. A DRAG always MOVES, across disks too. If a name exists: Enter replaces, A all, Esc stops.

Programs' open/save box is a disk window with Open or Save, Cancel and Drive. It starts in MEDIA, then remembers each program's last folder. Type a save name on its status line; clicking a file copies its name. Saving over a file replaces it without asking.

Folders: APPS and GAMES hold programs, MEDIA sample documents, SYSTEM/APPDATA programs' own settings, SYSTEM/FONTS typefaces (double-click one to see it), SYSTEM/DOS the DOS-side programs. System files are hidden and "Protected".


----------------------------
5. PROGRAMS
----------------------------

Several run at once, most twice over; text programs share one clipboard. Nothing is saved for you. Many go full screen on F or Alt+Enter; Esc returns.

Types that open by double-click: TXT Note Pad, BMP/GIF Paint, MOD Tracker, MD Artful Type, COM/EXE/LNK DOS, and whatever else installed programs declare.

NOTE PAD: up to 16KB. Ctrl+O/S open/save, Ctrl+Z undo, Ctrl+A select all, Ctrl+F find, Ctrl+R replace, F3 / Shift+F3 next / previous, Tab between the boxes, Esc closes the panel. Tick Rx for patterns: . [a-z] [^a] * + ? ^ $, \ for a literal.

TEXPAD: LaTeX source left, typeset page right. F5 typesets, Esc typesets and moves to the page, [ and ] turn pages, Ctrl+C copies the whole source, Ctrl+X cuts a line. Exports PDF and PostScript.

SHEET: 256x16384 cells, four sheets. F2 edits in place, Tab/Enter commit and move, Shift+arrows select. Saves SLK, DIF or BIF; only BIF keeps all sheets. CHART graphs a column of one and exports BMP.

PAINT: Ctrl+Z undoes and redoes, Ctrl+F full screen, Del clears a selection. Opens BMP and GIF, saves either.

CALCULATOR: type it. N negate, Q root, R 1/x, E clear entry, Esc all, H the tape.

ARTFUL TYPE: Markdown, full screen. Ctrl+B/I/K/L bold, italic, code, link; Shift+Ctrl+Z redo; Esc leaves.

BROWSER, TELNET: need Ethernet or os88net. The browser speaks plain HTTP; for HTTPS and modern pages run tools/os88proxy.py on another computer and Open Location <that host>:8088. Telnet takes host:port, speaks ANSI with F-keys, Ctrl+] full screen, and receives Zmodem by itself.

FTP SERVER: port 21, one client. Setup sets the user, password, root folder and Read Only - with no user, anyone may log in.

TRACKER, AUDIO, MIDIRACK, VIDEO: MOD, WAV, MIDI and V88 players. Space pauses, Left/Right step. Tracker: 1-4 mute channels, P loops a pattern, R sample rate, E edits the play list. Audio plays on in the background; a WAV opened while it runs joins its list. MIDIRack: L loops, I file info, output in Options > Settings. Video: R repeats, M mutes, I info. Sound card, Covox or PC speaker.

PIANO plays from a s d f... (black keys w e t...). FRACTAL zooms where clicked. PIXEL opens most image formats; F1 lists its keys. MINES: right-click flags.

DOS: runs real DOS .COM and .EXE in a window. Has CD, DIR, COPY, TYPE, OPEN and HELP. Setup > Memory > Shut down the OS gives the program the whole machine (~586K of 640K); os8088 starts again when it exits.

THE WIRE: the Wire desktop icon, shown while a network link is up, opens os8088.com's program library: Load Program runs one, Add to Disk keeps it.

GAMES: F full screen and P pause in most. Arkanoid: Space serves. Tank: arrows or WASD, Space fires. Cyclone: Z superzapper, J jump. Clear Skies: arrows fly, W/S throttle, A/D rudder, B brakes, R back to the runway; full throttle, pull back at 55 knots. Missile: 1/2/3 fire from a base. Solitaire: A finishes. TameGram: Space rotates, Enter drops, C colour-blind. Pixelstein: Ctrl fires, Space opens doors, Shift runs, Tab map. DrMarco: Z/X rotate, H help.

STICKY APPS (STIKAPPS) live on the desktop under every window and return at each start until their x closes them. Drag one by its name tag; click it for keys and menus, Esc lets go. On 128KB each is a window.

DESK CALENDAR: arrows pick a day, PgUp/PgDn the month, T today. + adds a line: "9:30 Dentist" is an appointment, anything else a task. Enter edits, Del deletes, a click ticks a task. A reminder rings: S snoozes, D dismisses.

STICKY NOTE: a note is a .TXT file in SYSTEM\APPDATA\NOTES. Enter or the arrow opens it in Note Pad, + makes one, < > turn pages, Del twice deletes. A click ticks a [ ] box.

DESK CLOCK: nine faces, three sizes, a daily alarm (A); Enter opens Settings.

More disks: Frotz (Infocom stories), Word, CWord, RunCPM, C64, Apple II, Weave/Loom. Word keeps Word 1.1's keys: Ctrl+C centres, Shift+Del cuts, Shift+Ins pastes.


----------------------------
6. CONTROL PANEL
----------------------------

Pages: Scheduler (leave Pre-emptive), Date/Time (with 12-hour and seconds options; names the clock chip found), Drivers, Display (two cards only: Set Primary, and Right or Below to span both screens), Sound, Theme (Bright, Dark, Color on VGA/EGA; window animations; screen saver), Dock, Floppy (each drive's type, and whether to read a track or a cylinder at a time; applies at the next start) - plus a page per loaded driver.

*** IMPORTANT ***
Settings are written when you CLOSE the panel, or on Restart or Shut Down - not when docked. The system disk must be in A: and writable; the bar says if not.


----------------------------
7. DRIVERS
----------------------------

Off until ticked in Control Panel > Drivers; a tick loads it now and at every start. Each row says Loaded or why not.

  Sound      AdLib, Sound
             Blaster, Covox,
             MPU-401
  Hard Drive section 9
  Ethernet   NE1000/NE2000
             network card
  Ram Disk   a disk in spare
             memory
  os88net    section 8
  USB Mouse  CH375 card

The Ethernet page shows the address DHCP gave; Renew asks again and Set Up enters a fixed address, subnet, router and name server.

Load Sound before picking a card on the Sound page; Test plays a tone. Ram Disk and os88net cannot run together. The Ram Disk page sets the size, uses XMS above 1MB if present, and Preserve / Load keep its contents in a .RAM file across switch-off.


----------------------------
8. LINKING TO A DOS PC
----------------------------

A DOS machine's folder becomes a drive here, over a parallel data-transfer (LapLink) cable - not a printer cable. Copy SYSTEM/DOS/OS88NET.COM from the apps disk to the DOS machine and run it in the folder to share:

  C> CD \PHOTOS
  C> OS88NET

ESC stops it. Switches:

  folder   share this one
  /W       every drive
  /RO      read only
  /P:378   this port only
  /N       carry network
           traffic too
  /?       list them

Here: tick os88net in Drivers, then Connect on its page. Linked means a Link drive icon is on the desktop. No partner means the cable or the DOS end; set /P: if the ports differ. With /N and a DOS packet driver the Browser, Telnet and The Wire work through the PC. About a quarter of floppy speed.


----------------------------
9. HARD DISK, HIBERNATE
----------------------------

INSTALL.O88, in the root of this disk, puts os8088 on a hard disk: it turns the driver on, asks which partition and whether to keep its files, then copies both disks. To upgrade, start from the hard disk and run it again with the new system disk in a floppy drive.

Or tick Hard Drive in Drivers: its page has Format, Mount and Install.

Hibernate (chip menu) writes everything to C:\HIBERNAT.IMG and stops. At the next start choose Resume to get every window back, or Discard.


----------------------------
10. NO MOUSE
----------------------------

With no mouse and Num Lock off, the keypad drives the pointer:

  Arrows     move; hold to
             speed up
  Home,PgUp, diagonals
  End,PgDn
  Space,     left button
  keypad 0/5
  Del        right button

A press is a click on buttons and a hold for menus and drags; two quick presses double-click. In an open menu arrows step through items. SCROLL LOCK hands the keys to the window under the pointer, and back. Serial (COM1/COM2), PS/2 and late-plugged mice are found by themselves, ending this mode.


----------------------------
11. MESSAGES
----------------------------

Needs NAME.O88
  The program for that
  document is on no
  mounted disk.

Needs Sys Disk A:
  Put the system disk in.

Load failed
  Not a program, or
  damaged.

Folder full
  Too many items; make a
  folder.

No os8088 disk
  Format Disk makes it one.

RAM (at start)
  Too little memory.

Disk error (at start)
  The system disk could
  not be read.


----------------------------
12. LIMITS, SWITCHING OFF
----------------------------

A folder shows 64 items (32 on a 128KB machine, which also has no drivers and does not save settings).

Restart reboots at once. Shut Down asks first (Enter yes, Esc no), saves settings, stops the drives and says when to switch off; close your work first. Never switch off or remove a disk while its light is on.


----------------------------

os8088 is free software under the MIT licence.
