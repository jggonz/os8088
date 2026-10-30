WIN16 - Windows 3.x programs on os8088
======================================

WIN16.O88 runs a Windows 3.0 program in an os8088 window: its menu bar,
its dialogs, its controls and its own icon, drawn as Windows 3.0 drew them
on the same display. Each program is a WIN16 window of its own, so two
programs run side by side and the Task Manager lists each by its own name.

HOW TO START ONE
----------------
Double-click the program's .EXE. DOS.O88 opens it, reads its header, and
hands a Windows program to WIN16 before running anything - where Windows'
own DOS stub would only print "This program requires Microsoft Windows."
WIN16.O88 must be on a disk in a drive when you do; a machine without it
says "Needs WIN16.O88".

Close a program with its close box, or with Exit on its own menu. If it
does not answer, a second click on the close box offers End Task.

THREE FILES, ONE FOLDER
-----------------------
WIN16.O88   the package - and everything it needs is INSIDE it: the
            System fonts, the dialog manager, the 8087 emulator, the
            loader and Windows' message-box icons, each read out of the
            same file when it is first needed
COPYING     the GNU General Public License, version 2. It is not read by
            the program; it is here because the licence has to travel
            with the binary.
README.TXT  this file

WHERE A PROGRAM'S SETTINGS GO
-----------------------------
WIN.INI and a program's own .INI file are kept in SYSTEM\APPDATA\WIN16 on
the disk WIN16.O88 was started from, and written when the program ends.
Two programs that write the same file both keep what they wrote.

WHAT IT RUNS
------------
Programs written for Windows 3.0's real mode: Minesweeper, Calculator,
Clock, Reversi and programs like them. A program that uses the 80186's
instructions needs a V20, a 186 or better. A program that asks for
something WIN16 does not have yet is closed with the name of what it
asked for. Not yet: programs over 64KB, Windows 3.1's protected-mode-only
programs, a program's own DLLs, and installers. Windows itself is never
needed and never run.

NO MICROSOFT FILE IS ON THIS DISK
---------------------------------
Bring your own programs. On the computer that builds os8088,
`make win16disk WIN3X=<dir>` writes a floppy in all four sizes holding
WIN16 and the Windows programs found in <dir>.

LICENCE
-------
WIN16's tables, its System font and its loader, dialog and control logic
are brought in from Wine (LGPL-2.1 or later) and winevdm (GPL-2), and
translated into 8086 assembly. apps/win16/ is therefore GPL-2-or-later;
the rest of os8088 is not. The full licence text is COPYING, beside this
file, and apps/win16/COPYING in the os8088 source tree.

Microsoft and Windows are trademarks of Microsoft Corporation. WIN16 is
not Microsoft's and contains none of Microsoft's code.
