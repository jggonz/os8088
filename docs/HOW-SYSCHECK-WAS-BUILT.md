# How System Check was built

**Is the installed system all one release?**

This is the record of what was actually built and tested, written as the
work happened, so that it can later be reused on the website. Plans live in
[docs/plans/SYSCHECK-PLAN.md](plans/SYSCHECK-PLAN.md) and the contract in
SPEC.md 118; this file says what happened, and keeps what was measured apart
from what was intended.

## 2026-10-20 — The question came from an upgrade

It started as a question about upgrades: a machine has os8088 installed on
its hard disk, a new release comes out - how does it get onto the machine?
The answer turned out to be mostly built already. The hard disk tool's
Install, with *Erase the partition first* left clear, writes the new
kernel, drivers and programs over the old ones and keeps everything else
(SPEC.md 52.10.15): the user's files, their settings, their `APPDATA`.

What it does not do is tidy up. "Nothing else is touched" is exactly right
for a user's documents and exactly wrong for a program the new release
retired, a driver it renamed, a package built for the old API table that
will now answer *Wrong os8088 version*, or a `KPAD.TMP` left by an install
the power cut short. Those were the gaps, and the owner asked for a program
that finds them, with a progress display like REDLINE's.

## 2026-10-20 — A design before a line of code

The window was drawn first. Codex's image tool was given four real os8088
screenshots - REDLINE's summary and its running screen, the hard disk
Install dialog and the Task Manager - and a description of the program, and
asked for three screens: a scan half done, a scan finished, and the
finished screen again on a monochrome Hercules card. The prompt and what
Codex passed to its own image tool are both kept in
[docs/syscheck/design/](syscheck/design/).

![The finished scan, as designed](syscheck/design/system-check-results.png)

The mockups got one thing wrong that only the operating system could
point out: the *This System* card reads "Kernel build 2190", and nothing a
program can ask will tell it the running kernel's build. The debug registry
that knows is forbidden to shipped software (SPEC.md 57.3), and adding a
slot would spend resident bytes on one program's question. The answer that
cost nothing was to make the question unnecessary: System Check lives in
`SYSTEM\` on the system disk, so the install writes it beside the kernel it
describes, and every upgrade replaces it. Its own build number *is* the
release's, and its manifest is that release's file list.

![The same screen on Hercules: shape, not colour](syscheck/design/system-check-mono.png)

The monochrome mockup settled the other design question. On a 1bpp card,
red, amber and green all round to black, so the lamps had to become shapes:
a tick, a checkerboard, a solid square, a hollow one.

## 2026-10-20 — What the operating system already had

Three surveys of the tree came before the contract. REDLINE was the look to
copy and the scheduling not to: it runs a whole benchmark inside one key
handler with the screen locked, which is fine for a benchmark and wrong for
a disk scan that should leave the desktop usable. The scan instead runs in
slices on the UI task through the wake mechanism RunCPM and the Font Viewer
use, because a worker task may not touch a file.

The file system had a surprise in it that shaped the design: a package
**can read** a hidden system file by name and **cannot list** one. So
drivers cannot be discovered - they have to be asked for, by the names this
release ships and by the names earlier releases shipped. Those historical
names came out of the Makefile's git history: `DEBUG.DRV`, `CLONE.DRV`,
`DESKTOP.DRV`, and `VMMOUSE.DRV`, which only the browser kernel carries now.

## 2026-10-20 — Four bugs on the first boot, and one in the fixture

The first build assembled at 13,550 bytes and booted straight into a scan,
which was more than it deserved. The screenshot then showed four bugs at
once, and every one of them was the kind this machine makes easy:

- the percentage labels under the bar were drawn **in the menu bar** -
  a `mul` had overwritten the y coordinate in DX, the 8086's habit of
  using DX as the top half of every product;
- the findings card sat **on top of** the checks card, for the same reason
  one routine further up;
- the Packages row said **OK** on a disk planted with two broken packages.
  `OSAPI_FILE_FIND` types a file as a package only when the loader would
  accept it - so the packages the check exists to find were arriving as
  plain files. The test is now the `.O88` name;
- the Settings row warned about a settings file that was fine, because a
  helper clobbered the register holding the record's length.

The fixture had a lesson of its own. A hibernation pointer from another
build was planted, and the row stayed OK: the kernel had already **deleted
it at boot**, image and all, which is exactly what SPEC.md 87.3 says it
does. So the leftover a real machine can carry is an image with no pointer,
and that is what the fixture plants now.

![Every planted breakage found: 5 broken, 6 warnings.](syscheck/vga-planted.png)

## 2026-10-20 — Clean Up, and the report

Clean Up asks with the standard alert (Cancel is the default, so Enter
cannot delete anything), removes only the files whose finding says they are
debris, rebuilds `ASSOC.DAT`, and scans again. On the planted disk it
removed `KPAD.TMP`, the retired `MODPLUG.O88` and the orphaned hibernation
image, fixed the associations, and left everything SPEC.md 118.8 says it
must leave: the duplicated program, the broken packages, and the hidden
system files a program cannot touch.

![After Clean Up: the warnings it could act on are gone, and Clean Up greys itself.](syscheck/vga-cleaned.png)

The report came back off the image on the host naming all eleven planted
findings, with one cosmetic bug - the year read `2,026`, because the shared
number formatter puts a thousands separator in every number. That is now
the test's to catch: `tests/syscheck.py` builds the planted disk, boots it,
saves the report, checks every line, cleans up and checks again, in about
47 seconds.

## 2026-10-20 — On the 8088, and on black and white

MartyPC's 5150 ran it from a 360KB system floppy on Hercules and on CGA.
**Measured: 46-48 seconds** for a whole scan at 4.77 MHz, nearly all of it
reading the release's files off the floppy to sum them, with the screen
live the whole time. And on a disk fresh from the build every row was OK
and the status read *No problems found* - the negative control that matters
most, since a checker that cries wolf on a clean install is worse than none.

![Hercules: shape, not colour, exactly as the mockup asked.](syscheck/herc-clean.png)

![CGA's 640x200: the short layout, two columns of checks.](syscheck/cga-clean.png)

## 2026-10-20 — It asks which drive

The first version checked the boot volume and nothing else, and the owner
asked for a question instead: *which drive?*, with the hard disk already
chosen. That turned out to matter most on exactly the machine the program
is for. Install a release onto a fresh hard disk and you are still running
from the floppy, so "the boot volume" is the floppy, and the disk you want
checked is the other one.

So the window now opens on a chooser in the place of the checks: every drive
the machine has, the hard disk in inverse video. The rule for the default is
one line - the boot volume if it is a hard disk, else the first hard disk -
and "hard disk" had to mean *fixed and not a cable*, because a drive
redirected over the parallel link answers fixed too. A one-drive machine's
B: is left out, because the BIOS quietly maps it onto A:.

The planted-disk test now presses Scan once and gets the hard disk it was
offered; it passes as before.

![The chooser: C:, the hard disk, offered first.](syscheck/vga-chooser.png)

## 2026-10-20 — Section 118, not 116

While this was in review, Lemmings merged as SPEC.md 116 and the INSTALL
wizard's open pull request had already claimed 117, so System Check's
contract moved to SPEC.md 118 when main was merged in. Nothing changed
but the number: every citation in the source, the tests and these
documents now says 118.

## 2026-11-01 — Check the disk, repair it, defragment it

The owner asked for two more things System Check should do: *basic
chkdsk-style FAT checks, and defragmenting, as options.* Then, partway
through, a constraint: the 360KB system disk has no room left, so whatever
the feature needs on disk must be able to live on `util360` beside
`SYSCHECK.O88` itself.

**The first decision was where the code goes, and it was not a choice.** A
package cannot read a sector - the public disk surface is file-level on
purpose, because a raw write underneath the kernel's FAT window, its
read-ahead cache and its stream cursors has no safe answer. So the checker
had to be kernel code. Kernel code that most machines never run is what an
on-demand module is for, and that is what it became: `kernel/fsck.inc`,
assembled with the kernel and cut out as `FSCHECK.DRV`. It reaches the FAT
through the kernel's own `dsk_next_clus_x`, `dskw_setfat` and `dskw_flush_x`,
so both FAT copies and the FAT window stay the kernel's business, and every
sector it writes goes through `disk_write_x`, so the cache invalidates
itself by the one rule it already had.

**The owner's constraint changed how it is loaded.** The kernel only ever
loads a module from the boot volume, and the boot volume is exactly the disk
with no room. So `FSCHECK.DRV` is not a module the kernel knows: System Check
reads it from its own folder into a claim of its own and hands the segment
over on every call. The kernel keeps nothing between calls, checks the
image's header against its own build and layout every time, and the image
and its workspace die with the program. What that costs every kern_big
machine is **101 bytes**: one API cell, a 66-byte stub, a trampoline, and a
counter.

Two things nearly went wrong there. The kernel can move its `.cold` code at
boot - over the VGA rung on a mono machine, into a ROM - and it re-points
the far calls in a module *when it loads one*. Nothing loads this one, so a
`call COLD_SEG:` inside it would have called into the wrong segment on every
CGA and Hercules machine. It builds its far pointer from the kernel's own
`[api_coldseg]` on every call instead. And kern_mega runs protected mode,
where an image cannot write to itself, so its build carries a 19-byte image
that refuses.

**The counter is the safety net.** A check is hundreds of calls apart and
the desktop is live between them, so the user can save a file onto the disk
being defragmented. Every sector write now bumps a per-volume byte, and the
engine compares it at every step. A move copies into free clusters first,
links the new chain, rewrites the file's entry, then frees the old chain -
so a power cut leaves lost clusters, never a file pointing at nothing - and
a write by anybody else in the middle ends the run before the link.

**Every finding is a code and two numbers**, and System Check makes the
words. So the window and the saved report say the same thing, and the
kernel carries no English for it.

The test plants seven faults on a copy of `util360.img` - a lost chain, a
chain longer and one shorter than its size, a cross-link, a chain into a
free cluster, a wrong `..`, two FAT copies that differ. It checks, repairs
and defragments under MartyPC, and judges the result with a FAT reader of
its own rather than with the program's report. The first run found the
test's mistake rather than the program's: the planted `..` belongs to
`SYSTEM\DOS`, whose parent is `SYSTEM` and not the root, and the engine
said so. The second found two of the program's. A repaired finding still
counted as broken, and *- repaired* ran off the end of the reason field.
Both are fixed. **Measured on the 4.77 MHz 5150: 5 seconds to check a 360KB
floppy, 13 to defragment it.** Looking at CGA showed the last thing: the
task list ran off the bottom of the short card, so the tasks moved to a
column of their own beside the drives.

![The chooser asks what to do as well as which drive (VGA).](syscheck/vga-tasks.png)

![The same on CGA's short card, all four tasks beside the drives.](syscheck/cga-tasks.png)

![After Check and repair: everything fixed but the cross-link, which is the user's call (VGA).](syscheck/vga-repaired.png)

![The same on CGA.](syscheck/cga-repaired.png)

## 2026-11-02 — The installed system, a hard disk, and kern_mega

Three follow-ups from the owner: kern_mega has to run it (kern_small may
refuse), there has to be a hard-disk test, and the case that started all
this - a fragmented os8088 install on a hard disk - should be able to
defragment its own files too.

**The installed system's own files.** The first version left every hidden
or system file where it was, on the guess that something found them by
position. Reading the boot path settled it: everything is found BY NAME -
the drivers, the modules, the hibernation image - except `KERNEL.SYS`,
which the hard disk's boot sector reads as one flat run from an offset the
installer patches into it. So the rule became the fact. Everything moves,
and `KERNEL.SYS` moves only when the volume's own boot sector is the one
that names it, with that sector rewritten - after the new run is linked and
before the entry is, so a stop between any two writes still boots.

**The hard-disk test found the bug that mattered.** It builds the install a
fragmented machine would have: the kernel moved high (the boot sector told),
three system files cut into interleaved pieces, a user file in a folder in
pieces, and an `AUTOSTRT` link that runs the defragmenter at boot. The first
green-looking run moved the kernel and *did not tell the boot sector* - a
machine that would never boot again. The name compare that recognises
`KERNEL.SYS` cleared its own zero flag with two `inc`s on the way out, so it
never matched anything, and the kernel was moved like any other file. The
test's second boot - every free cluster zeroed first, so a stale boot sector
would load zeros - is what makes that kind of mistake visible rather than
lucky.

That test needed a way to run System Check with nobody at the machine, so it
grew one users get too: a link's parameter (`C: DEFRAG REPORT`) names the
drive and the task and saves the report. Its first version returned a
garbage window pointer from the entry proc when a drive was named - the
boot hung on the loading screen - and its second started the scan from the
entry proc; it now starts at the window's first paint.

**kern_mega.** The engine ran out of its own image and kept its variables
there, which protected mode forbids - a code selector is not writable - so
its scratch moved into the workspace and three words of the kernel's. Bochs
then found the second problem in one boot: System Check's claims live above
1MB on kern_mega, and a claim there is a data selector alone, with no code
half to far-call. That is a known case, not a defect: RealDMG meets it for
its recompiled code and answers it the same way (`rw_csel`). The stub had
far-called without asking. So System Check asks the kernel for a code/data
pair over the image (`OSAPI_SEG SEG_SUBC`), and the stub refuses a selector
with no code half instead of faulting. The SDK's general note, which said
every claim is a pair, now names the exception and both examples. Repair and defragment then ran under
kern_mega with no fault, held to the same independent FAT reader as on the
8088.

## 2026-11-02 — Folders, and the last open items

The owner asked whether folders were the only thing left. They were not:
nothing had ever made a write happen mid-run to prove the guard against
it, and the release checks had never been run against a planted disk on
the two 1bpp adapters. All three are done now.

**A folder is named by its cluster in more places than a file.** Its own
`.`, its children's `..` and its parent's entry are on the disk, and the
engine rewrites all three, in an order where a stop leaves `..` entries a
repair puts right and never a folder nothing names. The machine names it
too: every program's current folder, every Disk window's, the clipboard's.
Those are the kernel's own words and the engine patches them. What it
cannot patch is a folder cluster a PROGRAM has kept for itself -
`OSAPI_FILE_HERE` hands one out, fifty packages ask for one, and System
Check itself banks two. So the rule is plain: folders move only when no
other program is open, and the report says so when one is (*folders not
moved: 1 programs open*). The test leaves a Disk window standing inside the
folder it moves, with the Task Manager open, and runs the defragmenter
twice: the first run moves files and leaves the folder; the second, with
the Task Manager closed, moves the folder - and the window's own folder
word follows it.

**A write mid-run** is now proved both ways: the report System Check saves
moves the volume's write generation (so `disk_write_x` really bumps it), and
a generation moved while a check runs ends the run with *the disk changed -
scan again*. The first attempt poked the generation before the run had
recorded it, and the test went red - which is what showed it would.

**CGA and Hercules** run the planted release-check disk as an XT-IDE hard
disk on MartyPC's 5150, unattended through the link parameter; both find
the same 5 broken and 6 warnings the VGA row does.

The owner also asked what was "wrong about the claim" behind kern_mega's
fault, and the honest answer is nothing: a claim an `OS88_HIGHMEM` program
gets above 1MB is a data selector alone by design, RealDMG already handles
it the same way, and the SDK's general note now says so.

## 2026-11-02 — Clean Up reaches the retired drivers

The owner asked whether System Check could use the engine sitting beside it
to clean up the system's own files. Until then a retired driver was a
finding Clean Up could not act on: *retired - install with Erase*, because a
`.DRV` is hidden + system and a package can neither list nor delete one
(SPEC.md 19.6). `FSCHECK.DRV` is kernel code and already rewrites directory
entries and frees chains, so it was the natural place.

**The engine decides what may go, not the package.** The new verb,
`FSK_DELSYS`, takes a name - and refuses it unless the name is on a list
assembled into the image itself: the `.DRV` lines of
`apps/syscheck/retired.txt`. So no caller, however broken, can talk it into
deleting `KERNEL.SYS`, `SYSTEM.CFG`, a driver the kernel loads or a module
it reads. A plain *"anything not in `drv_tab`"* rule was considered and
refused: `HTTPPAGE.DRV`, `HDDTOOL.DRV` and `FSCHECK.DRV` itself are images
that something other than `drv_tab` loads, and that rule would have offered
them up. The two lists are one list in two places, and `t_sysman` fails the
build when they disagree. kern_emu's image leaves `VMMOUSE.DRV` off, since
that kernel still loads it.

The delete itself is `dskw_delete`'s own commit entered past its protection
test, so the caches, listings and Disk windows follow as for any delete.
Measured: the kernel is byte-identical (the verb is the image's), the engine
+229 bytes, System Check +25. The `syscheck` row now plants `DEBUG.DRV` and
requires Clean Up to remove it; with `DEBUG` taken off the engine's list it
goes red with *Clean Up left ::/DEBUG.DRV*.

**What it still does not do**, and why: a driver of another version, or a
module from another build, is not a leftover - it is a file this release
needs, and the right answer is the new file rather than no file. Those
findings still say to install again.

### ...and the drivers the kernel refuses, on a second click

The first cut left one kind of system file alone on purpose: a driver of
another version, or a module from another build, because *install again*
was supposed to be the answer. The owner pointed out that it is not always:
the 360KB system disk stopped carrying `XMEM.DRV`, so a machine upgraded
from that pair keeps its old `XMEM.DRV` for ever, refused at every boot,
and no install touches it. So Clean Up removes those too - as a **second
question**, after the leftovers, Cancel the default.

The engine judges these as well, with a second form of the verb. The
package hands over a buffer the size of the expanded file; the engine reads
the file through the kernel's own read (which unpacks a `'CZ'` file, as the
driver loader does) and deletes it only if its header is `'O8'` and neither
`drv_check` nor `mod_check` would take it. Every image anything loads today
- the sub-images such as `HTTPPAGE.DRV` included - carries a current header,
so none of them can be offered up; and because the judge is the running
kernel, kern_mega's engine keeps kern_mega's modules, which kern_big's
manifest calls another build's.

**Building the test found a real bug and a trap.** The bug: System Check
spelled `DRV_VER` as 11, a cycle after the kernel moved to 12 - so the old
`XMEM.DRV` this feature is for would have been reported as merely
*different*, and a current driver that differed as the wrong version.
`t_mirror` now holds the copy to the kernel's. The trap: the first fixture
planted a `FORMAT.DRV` of another build, and Clean Up could then no longer
rebuild `ASSOC.DAT` - the associations' writer lives in that module. True,
and worth knowing (a refused `FORMAT.DRV` already breaks that before Clean
Up runs), but not what the test is about; the planted module is `WIN95.DRV`
now.

After both answers the report is honest rather than tidy: `RAMDISK.DRV` and
`WIN95.DRV` read as *missing - install again*, which they are, and the old
`XMEM.DRV` is simply gone, being an optional file.

### ...and programs that will not load, and an old COLDHMA.SYS

The owner's install had two more files nothing would ever replace: an old
`DOLDEL.O88` built for an earlier package format and no longer on the 360KB
apps disk, and a `COLDHMA.SYS` the 360KB system disk stopped shipping. Both
now go on Clean Up's second question, which reads *unusable files*
rather than *refused drivers*.

A package is the easy half: it is a visible file, the scan has already read
the header the loader would reject, and an ordinary delete does it. The
associations are refreshed after, so a program that was removed does not
come back as the next scan's finding.

`COLDHMA.SYS` is the awkward one. It is hidden and system, so it has to go
through the engine, and its header carries **no build key**: whether it fits
this kernel is a hash over `.text` that only its own install routine takes.
So the engine judges two facts it can check - the file is an HMA image, and
`.cold` is not running from it this boot - and leaves "is it this
release's" to System Check's manifest. Deleting a current one by mistake
would cost a 286 heap until the next install, never a boot.

**The bug that took the time** was eight bytes long. The new 11-byte name
compare ended `inc si / inc di / loop`, and `inc` sets ZF - so after a FULL
match the flags said *different*, and every `COLDHMA.SYS` was refused. It
was found by having a temporary build write a checkpoint and a copy of the
parsed name into kernel memory and reading both back off the running
machine: the name was right, so the compare was wrong. The older
retired-list loop has the same shape and is immune only because it ends in
an unconditional jump.

### The words, read the way a user reads them

The owner's note on the second question: *"refused"* did not say that it
was the SYSTEM refusing the file - it read as though System Check were
refusing something. So the question asks about **unusable** files, and
every finding it counts ends the same way, **will not load**, with what the
file is beside what the system needs (`driver version 11, needs 12 - will
not load`). The same pass went over everything else System Check prints:
"retired" became *dropped from os8088*, a missing `BOOTHD.SYS` says it is
*needed only to install again* where it said *only an install reads it*,
`not one sector ending 55AA` became *not a valid boot sector*, the
hibernation findings say *nothing will resume it* and *its image file is
missing* in place of pointers, and the defragmenter's *folders not moved*
says what to do about it - *close other programs* - rather than counting
them. Technical words stay where they are the right ones (a FAT, a cluster,
a chain): the bar was that a user can tell what to do from the line in
front of them.
