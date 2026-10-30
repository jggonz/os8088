---
name: win16-reference
description: Recreate (or use) the WIN16 reference machines - MS-DOS 6.22 with a from-scratch Windows 3.0 or Windows 3.1 install under QEMU, built entirely from archive.org downloads, plus the sample Windows programs (Windows 3.0/3.1 Minesweeper and Calculator, Duff Software's WinMine 2.6-2.9). Use when work on SPEC.md 124's Windows 3.x layer needs the REAL thing to compare against - reference screenshots per adapter, a program's behaviour, a census of its imports or opcodes - or when win16ref/ is missing on this checkout and has to be rebuilt.
---

# WIN16 reference machines

Before starting, read the [shared runtime guide](../../../docs/AGENT-SKILLS.md)
for Claude Code and Codex tool mappings.

SPEC.md §124 runs Windows 3.x programs on os8088. Its plan
(`docs/plans/WIN16-PLAN.md`) is written against two kinds of evidence this
skill produces:

- **The programs themselves:** the census of imports, opcodes and resources.
- **What they looked like on real Windows**, so WIN16 can be held to it.

Both come from Microsoft's and Duff Software's files, so **nothing here is
ever committed**. It all lives in `win16ref/` at the repository root, which
`.gitignore` excludes. That directory is per checkout: a new worktree starts
without it, and this skill is how it gets it.

## What you get

```
win16ref/
  downloads/      every input, SHA-256-pinned (the script refuses a mismatch)
    dos622/       MS-DOS 6.22 disks 1-3          archive.org disk-1_202602
    w30disks/     Windows 3.0, seven 720K disks   archive.org microsoft-windows-3.0-3.5-720-k
    w31disks/     Windows 3.1 3.10.103, 1.44MB    archive.org windows-3.1-disks
    W3.zip        a pre-installed Windows 3.0     archive.org w-3_20231227
    WinMine-2.*.zip                               minesweepergame.com
  w30/c.img       MS-DOS 6.22 + Windows 3.0 (VGA) + WinMine 2.9; c-pristine.img without WinMine
  w31/c.img       MS-DOS 6.22 + Windows 3.1 (VGA);  c-pristine.img identical at install
  dosboot.img     DOS disk 1, for the floppy drive (it is ejected at boot)
  samples/        winmine-2.6/2.8/2.9, w3-archive/ (the pre-installed tree), w31/ (3.1 WINMINE, CALC)
  ref/            reference screenshots
  log/w30, log/w31  one screenshot per install step
```

The disks are 33.5 MB raw images, partitioned, FAT16, with CHS 65/16/63. That
is the geometry QEMU picks for that size, so mtools reads them with the
`mtoolsrc` the script writes.

## Recreate it

```sh
python3 .claude/skills/win16-reference/scripts/win16ref.py all    # ~15 min: fetch, install30, install31
```

It needs `qemu-system-i386`, `mtools`, `unzip`, `curl` and `python3`. All of
them are already required by `make deps`, except `unzip` and `curl`, which
macOS ships.

What `all` does, which is also what to do by hand when a step needs it:

1. **`fetch`**: download, verify and extract.
2. **`install30` / `install31`**: each makes a fresh DOS disk, then installs
   Windows from its floppies.
   - **The DOS disk:** mtools partitions, activates and formats a blank image.
     QEMU then boots a COPY of DOS disk 1 whose AUTOEXEC is `FDISK /MBR`,
     `SYS C:`. No DOS Setup and no keystrokes.
   - **Windows:** boot C:, type `a:` and `setup`, then replay the recorded
     key sequence with fixed waits. Floppies are swapped with QEMU's
     `change floppy0 <img> raw`.
   - **3.0:** accept the defaults, uncheck printers/apps/documents (`Alt+P`,
     `Alt+A`, `Alt+D`), feed disks 2-7, then Return to DOS.
   - **3.1:** Express Setup, name `os8088`, disks 2-6, "No Printer
     Attached", Skip Tutorial, then Return to MS-DOS.
3. **The check:** after each install the script reads `C:\WINDOWS` back with
   mtools and exits non-zero if `WIN.COM`, `CALC.EXE` and the rest are
   missing.

**Nothing reads the screen.** If a check fails, open `log/<install>/` and find
the step that diverged. Then finish by hand: `run`, then `key`/`type`/`shot`,
then `quit`.

## Use it

```sh
S=.claude/skills/win16-reference/scripts/win16ref.py
python3 $S run w30 "win /r winmine"     # Windows 3.0, REAL MODE - the mode WIN16 imitates
python3 $S shot w30-winmine 76,38,158,240 2
python3 $S key alt-g                    # open WinMine's Game menu
python3 $S type '2.01-2='               # into Calculator: 3.0 and 3.1 both answer 0.00
python3 $S quit
python3 $S run w31 "win /s calc"        # Windows 3.1 has no real mode; standard mode
```

The WIN16 rows that run Microsoft's programs (`win16mines`, `win16calc`,
`win16cpu` and their XT legs) read them from `build/win16/`, never from this
tree. `samples` puts them there, renamed where two versions share a name:
CALC, CLOCK and REVERSI from Windows 3.0, `W31CALC.EXE` and `W31MINE.EXE`
from 3.1, and WinMine 2.9 as `WINMINE.EXE`. `make clean` removes them, so
run it again after one:

```sh
python3 $S samples                      # win16ref/samples/ -> build/win16/
```

To try a program of your own, such as the WIN16 test programs
`make win16fixtures` builds, copy it onto a STOPPED machine's disk and start
it from `C:\FIX`:

```sh
python3 $S copy w30 build/win16/*.EXE   # into C:\FIX; refuses while a machine runs
python3 $S run w30 'win /r c:\fix\menu'
```

`win /r PROGRAM` starts Windows with that program open. Restart between
programs with `key esc` then a fresh `run`, or by sending `system_reset`
yourself.

To change the display (EGA, CGA) on a copy of a disk, run `SETUP` in
`C:\WINDOWS` at the DOS prompt (not from inside Windows). It asks for disk 2.
QEMU's VGA does EGA's 640x350 and CGA's 640x200 modes. It has no Hercules, so
that adapter is 86Box's: `vm/xt-hercules`. Each adapter's system font is on
disk 2 (`CGASYS`/`EGASYS`/`VGASYS.FON`, SZDD-compressed). Hercules shares
EGA's font: `SETUP.INF` gives it EGA's resolution class `133,96,72`.

## Traps, each one already paid for

- **QEMU's `fat:rw:` directory drive pauses the VM on Windows' first write.**
  `info status` says `paused`, and the screen looks like a hang after
  `WIN.COM`. It is why every disk here is a real raw image.
- **os8088's own FreeDOS floppy cannot run Windows**, and neither could
  FreeDOS 1.3's `KERNL386.SYS`. Both hang at `WIN /R` and at `HIMEM.SYS`.
  MS-DOS 6.22 runs both Windows versions.
- **`tools/qmp.py`'s `sendkey` waits for the key to reach the BIOS buffer.**
  Windows' keyboard driver bypasses that buffer, so each key costs 10 s and
  prints a false "had not stored" warning. The script's `Q` class sends raw.
- **QMP serves ONE client.** A connection held open while `tools/shot.py`
  takes a screenshot leaves the screenshot in the listen backlog for ever,
  and the install sits on its first screen with no error. The script's
  second unattended run did exactly that. Every `Q` call now connects and
  disconnects.
- **QEMU's key names:** `=` is `equal`, `:` is `shift-semicolon`, `.` is
  `dot`. A key it does not know is dropped silently, so a typed calculation
  ends without its `=`.
- **minesweepergame.com:** its download links are relative to `/database/`,
  and it answers 403 without a browser User-Agent and Referer.
- **The WinMine zips are IMPLODED** (zip method 6). Python's `zipfile` cannot
  read them; `unzip(1)` can.
- **Windows 3.x setup files are SZDD** ("COMPRESS"): the 4-byte magic
  `SZDD`, then LZSS with a 4 KB window that starts at 4096-16. Expand them
  before parsing a `.FON` or `.DRV`.
- **QMP's socket path** must stay under ~104 bytes. Keep `WIN16REF` (the
  root override) short.
- **A paused or orphaned QEMU keeps the pidfile**, and the next launch then
  fails silently. `quit` first; never use `pkill` (it reaches other
  worktrees' emulators).

## What the reference showed (2026-10-28)

| | Windows 3.0 (real mode) | Windows 3.1 (standard mode) |
|---|---|---|
| Minesweeper | Duff's WinMine 2.9, titled "Win Mine" | Microsoft Minesweeper 3.10 |
| CPU census | WinMine 2.9 is 80186; CALC 3.00 is 8086 + x87 | WINMINE 3.10 is 80186; CALC 3.10 is 186 + x87 |
| check mark | a real check glyph, not os88ui's square | the same |
| `2.01 - 2` in Calculator | `0.00` | `0.00` |
| the WIN16 fixtures (`make win16fixtures`) | all eight run in real mode; `ref/w30-fix-*.png` | not yet run |

The screenshots are in `win16ref/ref/`. `tools/os88ne.py` (`make win16census
WIN3X=win16ref/samples`) is the census these verdicts come from.
