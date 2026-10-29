# How WIN16 was built

This is the development diary of WIN16, os8088's layer for running original
Windows 3.x programs (SPEC.md §124). It records what was actually found,
decided, built and checked, in the order it happened, for later reuse. The
delivery plan, [WIN16-PLAN.md](plans/WIN16-PLAN.md), keeps planned work
separate from completed work; nothing here is a promise.

## 2026-10-28 — the plan, and the three things that were already true

**The ask.** Run the original Windows 3.x Minesweeper and Calculator on
os8088, through a "Wine"-like layer that is loaded on demand. Use Wine and
winevdm as sources, and build it in user stories.

**What was read.** Three sources fed the plan:

- **os8088's own surface:** docs/INDEX.md, the package model (SPEC.md §20),
  parts (§20.12), workers (§20.6), the association table (§54) and DOS.O88
  (§96, `docs/plans/DOS-EXEC-PLAN.md`).
- **Wine** at `0738d528`: its NE loader `dlls/krnl386.exe16/ne_segment.c`
  and `ne_module.c`, its four `.spec` ordinal tables, and `tools/winedump/ne.c`.
- **winevdm** at `b88d7f87`: its architecture and its issues on these two
  programs.

**Three things were already true, and they set the design.**

1. **The kernel already runs a task safely on a foreign stack.** A Windows
   program runs with SS = its own data segment, and every os8088 task shares
   `SS = LOW_SEG`, with a switch that saves only SP. That looked like a kernel
   change until `kernel/sched.inc` was read: `sch_switch` already declines to
   switch when SS is not `LOW_SEG` (line 2389). It learned that for ROMs that
   hop stacks inside `int 16h`. The mouse ISR banks SS:SP for the same reason
   (`kernel/mouse.inc` line 396). So a Windows program on its own stack is
   simply not pre-empted, which is Windows 3.x's cooperative model, at zero
   kernel bytes.

2. **A worker task is the wrong home.** It may not touch a file, and a paint
   delivered to it would be drawn after `W_PAINT` had returned. A coroutine on
   the UI task, resumed from each callback and suspended in `GetMessage`, is
   both simpler and how Windows itself scheduled programs.

3. **DOS.O88 already owns `.EXE` and already reads the whole file.** The
   launch is a four-byte sniff at offset 3Ch and a forward, not a new
   association.

**Two things are not known, and the first story exists to measure them.**

- **The CPU.** Windows 3.1 had no real mode, and Microsoft advised its
  developers to build for the 286. So the 3.1 binaries very likely contain
  286 instructions, which an 8088 silently mis-executes.
- **Floating point.** Calculator very likely uses the MS C x87 emulator
  sequences.

Story S1's census of the owner's own copies decides both, and decisions D1
and D5 wait for its numbers.

**What was tested.** Nothing ran. This entry is the plan, the SPEC.md §124
skeleton, and a draft PR.

## 2026-10-28 — the owner's decisions, the samples, and the census

**Decisions.** The owner took these:

- **D1:** a V20 floor. No 8088 translation tier.
- **D2:** a coroutine on the UI task.
- **D4:** the font Windows showed on each display, so the programs look as
  they did on Windows.
- **D6:** `kern_big` and `kern_emu`; `kern_mega` refuses.
- **D7:** bring in whatever is needed from Wine and winevdm, licence
  notwithstanding. That makes `apps/win16/` GPL-2.0-or-later, as
  `apps/apple2` is.
- **D8:** delegated. It became the in-window bar `OS88UI_MENU`, which
  already draws a tick and is where Windows put its menu bar.
- **D9:** the name `WIN16`.

**The samples.** These were downloaded into `~/Repos/win16-samples`, outside
the tree, with provenance and SHA-256s in its `README.txt`:

- **Windows 3.0** from archive.org (`w-3_20231227`): an installed `WINDOWS`
  directory prepared for DOSBox, VGA only.
- **WinMine 2.6, 2.8 and 2.9** from minesweepergame.com. These are Duff
  Software's 1990 releases for Windows 3.0, which became Microsoft's
  Minesweeper. The site's links are relative to `/database/`; the obvious
  path answers 403.
- **Three CD-archive `WINMINE.ZIP`s** from textfiles.com. Two are
  byte-identical to 2.9.

**The census.** A scratch script parsed each NE: header, segments,
relocations, resources, dialogs and menus. It named every import from
Wine's `.spec` files, and swept each code segment with `ndisasm` for
186/286/386/x87 opcodes.

| program | CPU | imports |
|---|---|---|
| WinMine 2.9 | **80186**: 311 `push imm8`, 64 `push imm16`, 43 shifts by an immediate, 5 `imul r,imm`. No 286 or 386 | KERNEL 21, USER 48, GDI 8, SOUND 6 |
| WinMine 2.6, 2.8 | the same shape | — |
| CALC 3.00 | **8086**, plus 288 x87 instructions and 348 floating-point OS fixups | KERNEL 22, USER 57, GDI 13, WIN87EM 1 |
| CLOCK, REVERSI, NOTEPAD, SOL (3.00) | **8086** | — |

The CALC, CLOCK, REVERSI, NOTEPAD and SOL hits were a few isolated
"instructions" that decode as `movhps`, `cmovo` and `o32`. That is data, read
by a linear sweep. The shipped tool must sweep from the entry points.

The two programs import 130 distinct functions between them, 46 of them
shared. Neither imports a KERNEL constant (`__AHINCR`, `__WINFLAGS`) or
`GetWinFlags`.

- **WinMine 2.9:**
  - **KERNEL:** FatalExit 1, LocalAlloc 5, LocalReAlloc 6, LocalFree 7,
    GlobalAlloc 15, GlobalReAlloc 16, GlobalFree 17, GlobalUnlock 19,
    LockSegment 23, UnlockSegment 24, WaitEvent 30, MakeProcInstance 51,
    FreeProcInstance 52, FindResource 60, LoadResource 61, LockResource 62,
    InitTask 91, OutputDebugString 115, GetPrivateProfileInt 127,
    WritePrivateProfileString 129, FatalAppExit 137.
  - **USER:** MessageBox 1, InitApp 5, PostQuitMessage 6, SetTimer 10,
    KillTimer 12, GetCurrentTime 15, SetCapture 18, ReleaseCapture 19,
    ClientToScreen 28, SetWindowText 37, BeginPaint 39, EndPaint 40,
    CreateWindow 41, ShowWindow 42, MoveWindow 56, RegisterClass 57, GetDC 66,
    ReleaseDC 68, SetRect 72, PtInRect 76, DialogBox 87, EndDialog 88,
    SetDlgItemInt 94, GetDlgItemInt 95, CheckRadioButton 96,
    CheckDlgButton 97, IsDlgButtonChecked 98, DefWindowProc 107,
    GetMessage 108, PeekMessage 109, PostMessage 110, TranslateMessage 113,
    DispatchMessage 114, UpdateWindow 124, InvalidateRect 125,
    SetClassWord 130, LoadMenu 150, ChangeMenu 153, CheckMenuItem 154,
    GetSystemMenu 156, SetMenu 158, WinHelp 171, LoadCursor 173, LoadIcon 174,
    LoadAccelerators 177, TranslateAccelerator 178, GetSystemMetrics 179,
    _wsprintf 420.
  - **GDI:** SetROP2 4, LineTo 19, MoveTo 20, SelectObject 45, CreatePen 61,
    CreateSolidBrush 66, GetStockObject 87, SetDIBitsToDevice 443.
  - **SOUND:** OpenSound 1, CloseSound 2, SetVoiceNote 4, SetVoiceAccent 5,
    StartSound 9, StopSound 10.
- **CALC 3.00:**
  - **KERNEL:** LocalAlloc 5, LocalReAlloc 6, LocalFree 7, GlobalAlloc 15,
    GlobalReAlloc 16, GlobalFree 17, GlobalLock 18, GlobalUnlock 19,
    GlobalSize 20, LockSegment 23, UnlockSegment 24, GlobalCompact 25,
    WaitEvent 30, MakeProcInstance 51, FreeProcInstance 52, GetProfileInt 57,
    GetProfileString 58, WriteProfileString 59, lstrcpy 88, lstrcat 89,
    lstrlen 90, InitTask 91.
  - **USER:** MessageBox 1, InitApp 5, PostQuitMessage 6, GetTickCount 13,
    SetFocus 22, IsIconic 31, GetWindowRect 32, EnableWindow 34,
    GetWindowText 36, SetWindowText 37, BeginPaint 39, EndPaint 40,
    CreateWindow 41, ShowWindow 42, DestroyWindow 53, RegisterClass 57,
    GetDC 66, ReleaseDC 68, SetCursor 69, ShowCursor 71, SetRect 72,
    FillRect 81, DialogBox 87, EndDialog 88, CreateDialog 89,
    IsDialogMessage 90, GetDlgItem 91, SetDlgItemText 92,
    CheckRadioButton 96, CheckDlgButton 97, MapDialogRect 103,
    MessageBeep 104, DefWindowProc 107, GetMessage 108, SendMessage 111,
    TranslateMessage 113, DispatchMessage 114, UpdateWindow 124,
    InvalidateRect 125, OpenClipboard 137, CloseClipboard 138,
    GetClipboardData 142, CheckMenuItem 154, EnableMenuItem 155, GetMenu 157,
    GetSubMenu 159, WinHelp 171, LoadCursor 173, LoadIcon 174,
    LoadString 176, LoadAccelerators 177, TranslateAccelerator 178,
    GetSysColor 180, IsClipboardFormatAvailable 193, SetWindowPos 232,
    lstrcmp 430, AnsiUpper 431.
  - **GDI:** SetROP2 4, SetTextColor 9, Rectangle 27, RoundRect 28,
    TextOut 33, SelectObject 45, CreatePen 61, CreateSolidBrush 66,
    DeleteObject 69, GetDeviceCaps 80, GetStockObject 87, GetTextExtent 91,
    GetTextMetrics 93.
  - **WIN87EM:** `__FPMATH`.

**Fonts.** Windows 3.0's `VGASYS.FON` is "System", 10 pt at 96 dpi:
proportional, 16 px high, average width 7, maximum 14, ascent 13. Wine's
`fonts/system.sfd` has a 16 px strike, and an 18 px one, and nothing smaller.
So it covers VGA. EGA, CGA and Hercules need the sizes from Windows' own
`EGASYS`/`CGASYS`/`HERCSYS.FON`, which are on the setup disks and not in this
install.

**Booting Windows 3.0 did not work, yet.** The aim was reference screenshots.
The setup was QEMU (486, 4–8 MB), FreeDOS from a copy of os8088's DOS floppy,
and the Windows directory as a `fat:rw:` C: drive.

| attempt | result |
|---|---|
| `WIN /R` on os8088's 8088-tuned FreeDOS kernel (build 2046) | hangs silently after `WIN.COM` starts |
| `DEVICE=C:\HIMEM.SYS` (Windows 3.0's own) for standard mode | hangs at device load |
| the same two on FreeDOS 1.3's official `KERNL386.SYS` (build 2043) | the same hangs |

None of this blocks the layer: the binaries come straight out of the zip. The
archive's own target emulator is DOSBox, which is not installed here, and an
MS-DOS boot disk would be the other route. That is a question for the owner.

**What was tested.** Before this entry was committed, `make` passed
(checkdocs, the index, the fast tier). The first commit's direct `checkdocs`
run had passed only because the new plan was not yet tracked: `checkdocs`
reads tracked files. A citation split across a line break failed it once the
plan was tracked.

## 2026-10-28 — the real Windows 3.0 and 3.1, installed from floppies

**Asked:** "Use files from archive.org to get your Windows 3.0 reference
running", then "install Windows 3.0 from scratch… or Windows 3.1", then
"save the installed instance in a local .gitignored directory and create a
skill that explains how to recreate this installation".

**Why the first attempts hung.** Booting archive.org's pre-installed tree on
genuine MS-DOS 6.22 hung exactly as it had on FreeDOS. QEMU's monitor said why:
`VM status: paused`. QEMU's `fat:rw:` directory drive had hit a write error on
Windows' first write, and QEMU pauses a guest on a disk error. On a raw
FAT16 image the same tree reached the desktop at once. So the FreeDOS result
in the previous entry was never really FreeDOS's.

**From scratch, Windows 3.0.** The DOS disk was a blank 33.5 MB image
partitioned and formatted by mtools, then made bootable by `FDISK /MBR` and
`SYS C:` from a copy of DOS 6.22 disk 1. Setup ran off the seven 720K disks,
swapped through QEMU's monitor:

- the defaults (VGA, PS/2 mouse);
- printers, application search and on-line documents unchecked;
- Return to DOS.

Its CALC.EXE is byte-identical to archive.org's pre-installed one.

**From scratch, Windows 3.1** (3.10.103 retail, 1.44MB): Express Setup, the
name `os8088`, disks 2–6, "No Printer Attached", Skip Tutorial. Express never
asked for disk 7.

**What they showed.**

- **WinMine 2.9 under real-mode Windows 3.0** is titled "Win Mine", and its
  Game menu marks Beginner, Sound and Marks with a ✓ glyph. That glyph is not
  os88ui's square, which changes D8 from "if" to "S5 adds it".
- **Calculator** draws its keys itself as colour `RoundRect`s, as the census
  said. Scientific view's panel is the dialog template.
- **`2.01 - 2 = 0.00` on 3.0 and on 3.1.** The famous bug is on the glass,
  and it is S9's oracle.
- **Microsoft's Minesweeper 3.10 is 80186**, so the V20 floor covers it. It
  ships 1bpp twins of its three bitmaps, imports `SHELL.ShellAbout`, and adds
  Custom…, Color and Best Times… to the Game menu.
- **The adapters' system fonts** are on Windows 3.0's disk 2, SZDD-compressed:
  CGA 8 px, EGA 12 px and VGA 16 px. `SETUP.INF` gives Hercules EGA's
  resolution class, so EGA's font.

**Kept.** Everything is in `win16ref/` (git-ignored, 170 MB): the downloads,
both installs (each with a pristine copy), the samples and ten reference
screenshots. The skill `.claude/skills/win16-reference` holds the script that
recreates it end to end. Its first unattended run found a bug of its own (a
missing parent directory before `unzip -d`), which is fixed.

## 2026-10-28 — Open Watcom, for the test programs

**Asked:** "is there a C toolchain that would help us get what we need if
your C toolchain isn't enough?" Then: "Open Watcom works for me."

**Why it was needed.** SmallerC (SPEC.md §73) has no `long`, and the Win16
API is built on `LONG`. So it cannot build a Windows program, and S1's
fixtures would have been NE headers assembled by hand.

**What Open Watcom turned out to be.** Its release page offers installers
for Linux, Windows, DOS and OS/2 only. The CI snapshot tarball also carries
macOS builds: `armo64` for Apple Silicon and `bino64` for Intel. The
monthly dated release (`2026-10-01-Build`) has the same tarball as a fixed
asset, which makes a SHA-256 pin possible. `tools/setup-watcom.sh` fetches
it, unpacks only this host's binaries, the headers and the 16-bit libraries,
and proves the result with a canary: a MessageBox program that must link as
a Windows NE importing USER.

**First program.** `HELLO16.EXE` (1,906 bytes, `-0`, so 8086 code) registers
a class, creates a window and paints a line of text. Run on the Windows 3.0
reference machine in real mode:

- **The first build** showed its window and an hourglass, and painted
  nothing. Its `WndProc` was not `__export`ed, so Windows' loader never
  patched the prologue that loads the program's DS. The callback ran on the
  wrong data segment.
- **Rebuilt with `__export`**, it painted "Hello from Open Watcom".

That failure is exactly S2's prologue patching, seen from the other side.

## 2026-10-28 — S1: the census, and eight programs that run on Windows

**Asked:** "yes", to "Shall I start S1?"

**The tool.** `tools/os88ne.py` reads an NE file the way WIN16's loader
will: header, segments, relocations (chains and additive records), entry
table, resources and imports. It names every import from Wine's own
ordinal tables, which winevdm extends; those are vendored verbatim in
`tools/win16spec/`. Its centre is the instruction census, and that had to
be **recursive descent**. The scratch census of the planning stage swept
linearly, and on Open Watcom's 8086-only HELLO16 it counted 25 "186 and 386
instructions". Every one was in a string: lower-case ASCII *is* the 186
opcode range. Descent starts at what is known to be code and follows the
branches: CS:IP, the exports, internal relocations, switch tables, then
standard prologues in what is still dark.

**What it found that the plan did not have:**

- **Every Open Watcom program contains a 286 instruction.** Its start-up
  code probes `push sp / pop ax / cmp ax, sp / jnz`, and runs `smsw` on the
  286 side. Counting it would refuse every Watcom program on an 8088, so the
  census recognises the probe and reports the instruction as *guarded*.
- **The C runtime talks to DOS directly.** CALC 3.00 issues `int 21h` with
  AH = 25h, 30h, 35h, 3Eh, 40h, 44h and 4Ch from MS C's start-up and exit.
  So WIN16 must own `int 21h` while a program runs, which is now in S2 and
  SPEC.md §124.3.
- **SPEC.md §124.5 had floating point backwards.** The file carries native
  x87 code, with each OS fixup on a `wait` byte. Windows' loader rewrote
  those into the emulator interrupts only when no coprocessor was present.
  The section is corrected, and it now lists the subset: 35 kinds in CALC
  3.00 and 40 in CALC 3.10, all 8087.
- **The dark parts are data.** CALC's undecoded quarter of segment 4 holds
  real code, reached only through a pointer table, plus a table of powers of
  ten as doubles. The 11 "186" bytes there are all in that table, so the
  8086 verdict stands.

The verdicts match the plan's hand census:

| program | verdict |
|---|---|
| WinMine 2.6–2.9 | 186 |
| CALC 3.00 | 8086 + x87 |
| WINMINE 3.10 | 186 |
| CALC 3.10 | 186 + x87 |
| every Windows 3.0 accessory | 8086 |

The font census reads VGASYS.FON as D4 measured it: System 16 px, ascent 13,
average 7, maximum 14.

**The fixtures.**

- `tests/win16/tiny16.asm` is a Windows program written as NASM data: an
  MZ stub, the NE header, its tables, two segments and a relocation table,
  851 bytes. It is also a trap. Five bytes of `pusha`/`enter`/`pop cs`/...
  sit behind a jump, a switch dispatches through a table, and one procedure
  is reachable only by its prologue. Variants add a reached `push 0`, x87
  code with OS fixups, and Watcom's probe.
- Two NASM details cost a rebuild each:
  - `bin` will not shift a section's start address, so the two segments sit
    at fixed file offsets;
  - `follows=` pads a section to 4 bytes, which put the relocation table
    three bytes past the end of the code segment, and the census read it as
    "no relocations".
- Seven C programs cover the APIs of S2 to S9 one each: a message box; a
  window with paint, timer, mouse and keys; a menu with a check, a grey item
  and an accelerator; a dialog with every control class WinMine and
  Calculator use; profiles; bitmaps; doubles.
- `make win16fixtures` builds them. The bitmaps are generated by
  `tests/win16/mkbmp.py`, so nothing binary is committed.

**Run on Windows first.** All eight were copied onto the Windows 3.0 machine
(`win16ref.py copy`, new) and started with `win /r c:\fix\NAME`. All eight
ran in real mode, the NASM one included, and the screenshots are each
fixture's expected picture (`win16ref/ref/w30-fix-*.png`). The menu
fixture's open Colour menu is S5's reference: a ✓ beside Red, Unavailable
in grey, a full-width separator.

**The gate.** `win16ne` (fast) assembles the five variants and checks each
verdict, the imports by name, the switch table and the prologue find. Its
negative control is the base build: a linear sweep over its dark bytes must
still find 186 opcodes, so its 8086 verdict is a decision and not an
accident. Every prefix of the file and 80 corrupted copies must not raise.
When present, the Watcom fixtures and the reference programs must give the
verdicts above. Three sensitivity runs each broke the census once: no
prologue heuristic, no probe guard, and a linear sweep in place of descent.
They failed 2, 9 and 16 checks respectively.

## 2026-10-28 — S2: load or refuse

**Asked:** the same "yes", which covered S1, S2 and S3.

**The shape.** WIN16 is one package, `apps/win16/`, and it is a coroutine
on the UI task (D2):

- The program's code runs on its own stack, SS = DS = DGROUP.
- Every import is a six-byte stub, `mov ax, row / jmp near w16_gate`, so a
  relocated far call lands in the gate with the export's row in AX.
- The gate builds a frame on the program's stack, switches to the UI task's,
  and runs the handler there.
- A handler that has to wait for the user (MessageBox) sets one byte, and
  the gate returns to the os8088 callback that resumed the program instead.
- The program's frame is simply left where it is. That frame IS the waiting
  program, and the alert's completion resumes it.

**Wine was the logic, not the code.** The loader follows winevdm's
`ne_segment.c`:

- one claim for every segment;
- relocations in two passes (count what is missing, then apply), with
  winevdm's chain semantics: each site holds the next and is read before it
  is written, and 0xFFFF or a site outside the segment ends the chain
  silently;
- `NE_FixupSegmentPrologs` for the exported entry points;
- DGROUP = minalloc + stack + heap, as `NE_InitProcess` lays it out.

InitTask's register contract is `task.c`'s.

**The C runtime talks to DOS directly**, which S1's census had said. So
`int 21h` belongs to the program only while its code runs. It answers the
functions the census found and nothing else, and AH = 4Ch ends the program
the way a close box will.

**How a program gets there.** `.EXE` is DOS.O88's association, so DOS reads
the header and hands an NE on. The hand-off is the document arm of
`OSAPI_PKG_START` with the stem `WIN16`, a new built-in row in the kernel's
stem table that went into a row of padding: zero bytes.

**The forward is test-build only.** Until S12 decides where WIN16 ships, a
stock machine has no WIN16.O88. A forward there would rebuild the
association cache and toast *Needs WIN16.O88* with DOS's window already
closed, which is worse than the stub's own sentence. So the forward is
built only into the test disk's DOS.O88 (`-DDOS_WIN16`), and the shipped
one was checked byte for byte against HEAD's.

**On the glass,** in QEMU off `make win16test`'s disk:

- Double-clicking TINY16.EXE in a Disk window opened DOS.O88 for an
  instant, then WIN16's window and the program's MessageBox. That means
  InitTask, WaitEvent, InitApp and MessageBox, through the gate and back.
- MSGBOX, a C program whose runtime makes those `int 21h` calls, did the
  same.
- OK ended both, and WIN16's window closed behind them.
- The doctored TINYUNK refused with *Needs USER.287*.
- WINDOW.EXE, S3's fixture, refused with *Needs USER.10 and 31 more*: the
  size of S3, measured.

**Found on the way: os88ui's alert does not clip its message.** The SDK
says the message "is CLIPPED by the window". TINY16's 43-character line was
drawn straight across the alert's frame on both sides, and it left pixels on
the desktop after the alert closed. WIN16 now cuts the line to
`OS88UI_AMAX` (S6 brings a real MessageBox). The SDK's claim is not true,
and it is noted here rather than fixed in a wave about something else.

**The second wave: the census on the machine.**

**Why it was needed.** D1 sets the floor at a V20, but a plain 8088 must
still never be handed a 186 program: it would run something else silently.

**What it is.** `apps/win16/w16cens.inc` is `tools/os88ne.py`'s recursive
descent, decision for decision:

- the three seeds, the third being a third pass of the relocator;
- the switch tables, after the walk;
- the prologue sweep;
- the post-hoc guard for Open Watcom's `smsw` probe.

It runs after relocation, so a far call's segment is either one of the
program's or WIN16's own stub. The opcode table is the host's, written by
`os88ne.py --optab` and held to it by the fast row.

**What it costs.** About 2.5KB of the package, a transient claim of at most
17KB, and only on a CPU that cannot run 186 code.

**How it was tested without an 8088.** QEMU's CPU runs 186 code, so a test
build that runs the census on any CPU (`-DW16_CENSUS_ALWAYS`) was booted:

- TINY16 runs;
- TINY186 refuses with *Needs a V20, 80186 or better.*;
- MSGBOX runs.

The negative control matters most: with the guard call taken out, MSGBOX
refuses with *Needs an 80286 or better.* So the walk really does reach
Watcom's `smsw`, and the guard is what admits the program. The census on a
real 8088, and what it costs there, is S13's measurement.
