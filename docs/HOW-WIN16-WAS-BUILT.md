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

## 2026-10-29 — S3: the message pump

**The story.** *As a user I see the program's window, I can move it, click in
it, and close it with the close box.* WINDOW.EXE, the S1 fixture with a
class, a timer, the mouse, the keyboard and a paint, was the bar: S2 had
measured it as *Needs USER.10 and 31 more*.

**The decision that shaped it: a call into the program lives on the
program's stack.** A Windows program is called back all the time:
CreateWindow sends WM_CREATE, DispatchMessage calls the window procedure,
DestroyWindow sends WM_DESTROY. The obvious shape is a nested call from
WIN16's code on the UI task's stack. But a window procedure can wait (a
MessageBox in WM_CLOSE is ordinary), and a wait in the middle of a host-stack
call has nowhere to go. So the handler pushes a small CONTINUATION record
below the frame of the import it is serving, then the callee's arguments and
a return address into WIN16, and lets the gate enter the callee as if the
import were finishing. The callee's return is itself an import, row
`W16_CBROW`, whose handler runs the continuation. Nothing is held on the host
stack between two resumes, which is the property S2's coroutine already
depended on (SPEC.md 124.2.2).

**The pump.** GetMessage with nothing to give WAITS, and the os8088 callback
that has something resumes it. A click, a key, the one-shot timer, the
close box and the window's own W_PAINT each queue their message and call
`w16_kick`. The W_PAINT case is the interesting one: the window is
`OSAPI_WM_OWNBG`, the kernel's damage joins the invalid rectangle, and the
program is resumed *inside the paint*, under the lock and the damage clip, so
its WM_PAINT lands in the same pass and nothing flashes white first.

**The bug that hid the first run.** The first boot showed *Running* and no
window. The machine was idle: the program was waiting in GetMessage with no
window, so CreateWindow had returned NULL. Reading WIN16's bss out of guest
memory (QMP `pmemsave`, located by searching for the image's bytes, then
the NASM map) showed the class registered and the window table empty. The
class lookup compared sixteen bytes and then tested ZF, which the `inc di`
after the last compare had already overwritten. A full match read as a
mismatch. RegisterClass had gone through only because the same bug made
"already registered" read as "not registered".

**What Windows 3.0 said that the contract had wrong.** WINDOW.EXE does not
reach MoveWindow, SetWindowPos or PeekMessage, so a second Watcom fixture,
`tests/win16/pump.c`, was written for them and run on the Windows 3.0
reference machine before it ran here. Two things changed because of it:

- **The caption costs 19 rows, not 20.** At 320 × 180, Windows 3.0 gives a
  312 × 153 client. SM_CYCAPTION is 20, but the caption shares a row with
  the frame.
- **A window created minimized keeps its created size.** PUMP creates its
  window `WS_MINIMIZE` at 200 × 100 and then calls MoveWindow to 300 × 160.
  On Windows 3.0 the window comes up 200 × 100, because MoveWindow on an
  icon moves the icon, and ShowWindow restores the window to the size it
  was created at. That is WinMine's start sequence too.

(The Windows 3.0 screenshots are not committed: they are taken on the
reference machine, `win16ref.py run w30 "win /r c:\fix\pump"`, into the
git-ignored `win16ref/ref/`.)

**PeekMessage yields.** An empty PeekMessage on Windows 3.x yields to the
other tasks; a program that never yielded would own this machine, because
the scheduler never switches away from the program's stack. Here an empty
PeekMessage waits for one UI pass (`OSAPI_WM_WAKE`) and is answered then.
PUMP's idle counter climbs, the Task Manager shows WIN16 at 99%, and the
clock, the menus and the Task Manager itself all keep working.

**What there is no way to do.** The story said "the close box and a Task
Manager kill". os8088's Task Manager cannot kill anything; it is a viewer
(SPEC.md 28). So the close box is the only end a package has, and here it is
negotiated: it posts WM_CLOSE, and a program that refuses keeps its window.
Whether WIN16 should have an "End Task" of its own is left for the owner.

**Tested, on the glass (QEMU):**

- WINDOW.EXE: the title, the size, two ticks a second, *clicks 1 at 170,62*,
  *key a*, a full repaint after the Disk window covered it, and the close
  box ending it;
- PUMP.EXE: every line matching Windows 3.0's screenshots, except the
  width after `m` (256 here, 252 there: os8088 keeps widths on 8 pixels);
- the claim map after each close is the one before the launch, byte for
  byte, twice;
- CGA: legible, the grey dithered, the red box white where Windows' mono
  driver would draw it black, and the window clamped to 200 lines. Those
  last two belong to S4, with the System font.

**Not measured:** how much a strip repaint costs, in primitive calls or on an
XT. That is S13's.

## 2026-10-29 — S4: GDI and the Windows look

**The story.** *As a user the program's window looks the way it did on
Windows on that display.* "The way it did" needed a reference before it
could be checked, so the first thing written was a test card,
`tests/win16/gdicard.c`. It prints the text metrics, an extent, the device
caps, all 36 system metrics and all 19 system colours, and draws one of
everything the two targets draw: CALC's round keys and its NOTXORPEN line,
WinMine's grey bevels, a FillRect, dots, three DIBs. It ran on the Windows 3.0
reference machine first. (Its first name was GDI.EXE, and Windows refused to
run it: a program's module name may not be GDI.)

**The font.** D4 says each display gets its Windows System font. Microsoft's
.FON files cannot be committed, so the faces are drawn from Wine's System
font (LGPL), whose 16 px strike already has Windows' VGA advances.
`tools/w16font.py draft` turns it into text art, and resamples it to 12 and
8 rows for EGA/Hercules and CGA. A naive resample loses thin strokes, so the
draft chooses the rows it keeps per zone (above the caps, the caps, the
x-height, the descenders) to keep the most ink. Then the art becomes the
source. Sixteen CGA glyphs and three EGA ones were redrawn by hand: quotes,
the caret, the tilde, descenders, `@` and `#`. `make` turns each file into a
part of `WIN16.O88`, 1.2 to 2.2 KB each, lz4-packed, so only the face the
display needs is read at launch.

**Text is one call.** The plan said `.F88` and `os88type.inc`. What shipped is
a composer of WIN16's own: a TextOut is composed into one band at the
screen's byte grid and put down by one `OSAPI_GFX_BLIT1`, coloured by
`BLIT1_PEN`. That is one primitive call a string, which is the price
PERFORMANCE.md asks for. The first version clipped the left of every word:
BLIT1's band starts at the byte column holding x and skips `x & 7` pixels,
so the origin has to be aligned down first (SPEC.md 5.4.2.8).

**What the kernel refused.** RoundRect was first written on
`OSAPI_GFX_SPANS`, and its frame did not appear. SPANS refuses under an
armed clip region, and a DC always has one. So shapes coalesce their rows:
consecutive rows with the same left and right become one rectangle, and a
round key is its corners plus four fills.

**The numbers are Windows'.** On VGA, every number GDICARD prints here is the
number it printed on Windows 3.0: `tm 16 13 3 3 0 7 14 700`, an extent of
94 x 16, `caps 640 480 1 4 16`, the 36 metrics and the 19 colours. For EGA,
CGA and Hercules the reference machine was not re-run through SETUP. The
answers come from the GDIINFO block in each Windows 3.0 display driver,
read with the same tool that reads the .FON files, so CGA answers 640 x 200,
2 colours, LOGPIXELSY 48. Laid over the Windows screenshot on the dark
pixels, every shape lands on the same pixels. What differs is the glyphs
(Wine's `4`, `7` and `1` are not Microsoft's) and two palette entries.
os8088's dark grey is 555555 and its light red FF5555; Windows' are 808080
and FF0000.

**Mono.** A 1bpp display gets a colour after WIN16 reduces it by luminance:
black, the desktop's grey dither, or white. Text is lit at 160 and above. On
CGA the test card's red text is black, the way Windows' own mono driver
drew it, which was the open question S3 left.

**Tested, on the glass (QEMU):**

- GDICARD on VGA, EGA (`VIDEO=ega`) and CGA (`VIDEO=cga`), each with its
  face and its numbers;
- WINDOW.EXE, MSGBOX.EXE and PUMP.EXE unchanged in behaviour (PUMP still
  matching Windows 3.0's screenshot line for line);
- the claim map after WINDOW and after PUMP is the one taken before,
  byte for byte. While a program runs, WIN16 holds 23 KB for the instance,
  16 KB for the program and 3 KB for the face.

**Not seen:** Hercules (QEMU has none); a call count on a whole Minesweeper
repaint, which needs S5's menus before WinMine will start; BitBlt and memory
DCs, which WinMine 3.1 uses and 2.9 does not.

## 2026-10-29 — S5: memory, resources and menus

**The story.** *As a user the program's menu bar sits in its window as it did
on Windows, its shortcuts work, and its check marks show.* Menus are
resources, resources need memory, and WinMine needs all three before its
first window, so they were one story in two commits.

**Memory first.** The global heap is WIN16's, sub-allocated in 16 KB arena
claims. A fixed block's handle is its segment, as on real-mode Windows. A
moveable one is a handle-table entry at 0100h and up, which no RAM segment
can be, so a protected-mode arm can put a selector behind the same handle
later. The local heap is the one in DGROUP, and every `Local*` call reads the
CALLER's DS off the gate's frame, as Windows did.

**Resources stay in the file.** Only the NE resource table is kept. A
`LoadResource` reads that one resource with `OSAPI_FILE_READ_AT` into a
moveable block, so a program's size is never limited by what fits at load,
which an installer will need.

**A missing import is not a refusal any more.** Programs import far more than
they call. Each unprovided import is bound to a stub, and the stub ends the
program when it is CALLED, naming the import. TINYUNK shows it: its
`MessageBox` comes up, then its call to USER.287 ends it with *Needs USER.287
/ which WIN16 does not provide yet.*

**The menu manager is WIN16's own.** The plan first said os8088's own menus
(`OS88UI_MENU`). They have no nested popup, a one-byte action and os8088's
8x8 cell, and the end goal is any Win16 program. So `w16menu.inc` follows
Wine's `menu.c`: menus live in a USER heap, an HMENU is a record's offset
there, IDs are 16 bits, and popups nest. The look was measured off Windows
3.0 screenshots of the fixture and matched pixel for pixel: the bar's height
and its line, the popup's box and one-pixel shadow, the separator, the 8 x 10
check, the 5 x 9 arrow, the underline row, and where a nested popup goes
(the parent's right border less 7, the item's top less 1).

**The loop is the import waiting.** Windows' menu loop runs inside
`DefWindowProc`. Here `DefWindowProc` starts it and then WAITS, as S3's
`GetMessage` waits, and os8088's mouse and key callbacks move the selection.
Two messages must be answered before a popup is drawn: `WM_INITMENU` and
`WM_INITMENUPOPUP`, because the program may check or grey items there. So the
callback SENDS them, through a new "host send", `w16_resumecb`: the program is
resumed into its window procedure and comes back to the loop when it
returns. A pick POSTS `WM_COMMAND`, as Windows does.

**A deadlock, found with two counters.** The first build hung on the first
click. The menu callbacks run with the gfx lock already held, but WIN16's
"do I hold it" byte said no, so the first draw asked for the lock again. Two
temporary counters at 0:4F0h, read over QMP, showed the call that went in
and never came out. The callbacks now mark the lock as held.

**A bank can go stale.** What a popup covers is banked with
`OSAPI_GFX_SAVE` and put back with `OSAPI_GFX_REST`. The lock is not held
between two clicks, so the desktop can repaint the window under an open menu.
Such a bank is never written back: the area is invalidated and the program
paints it.

**End Task needed a second design.** The first version offered End Task only
when the program had not taken `WM_CLOSE`. MENU.EXE's F3 puts up a
`MessageBox`, and os8088 has one alert at a time, so the second click raised
the message box and never asked. What shipped is simpler: the first click
posts `WM_CLOSE`; if the program is still running at the next click, it has
had its say, and the click asks *It has not closed. End it?* Proceed ends it
where it waits.

**Mono.** On CGA the popup's grey shadow dithered into a dotted line. It is
black on a 1bpp display now.

**Tested, on the glass (QEMU):**

- MENU.EXE on VGA against Windows 3.0's screenshots; on EGA and CGA, the
  bar, the popups, the nested popup and a pick;
- the `WM_INITMENU` and `WM_INITMENUPOPUP` counts for a mouse pick and for
  F2 and Ctrl+B, which match Windows';
- the keyboard: F10, the arrows, Enter, Alt+F, Esc;
- WinMine 2.9: its board, its menus, Intermediate (16 x 16, 40 mines), a
  click that reveals and starts the clock, F2, and the close box;
- TINYUNK ended at USER.287 with its name;
- End Task on MENU.EXE refusing `WM_CLOSE`;
- the claim map after MENU.EXE's exit is the one taken before its launch.

**Not seen:** Hercules (QEMU has none); WinMine's Marks toggle; its Custom
field, which needs S6's dialogs.

## 2026-10-29 — S6: dialogs and controls

**The story.** *As a user the program's dialog boxes open, I can type in
them, and OK/Cancel do what they say.* WinMine's About and Preferences are
dialogs, and every installer the end goal names is a stack of them.

**WIN16 draws its own controls.** os8088's own widgets are not Windows', and
a program talks to its controls in Windows messages (`BM_SETCHECK`,
`LB_ADDSTRING`, `EM_SETSEL`). So `w16dlg.inc` follows Wine's `dialog16.c`
and the four control files, and each control is a child window of a class
WIN16 registers. The look was laid over Windows 3.0's screenshots of a
fixture: the push button's two-pixel bevel, the default button's black
ring, the 12-pixel radio and check boxes, the group box's caption gap, the
list's highlight, the EDIT's selection.

**It is a part, not a bigger image.** WIN16's image and its bss share one
64 KB segment, and every later story grows them. The dialog code is part 3
of `WIN16.O88`, assembled in WIN16's own nasm run behind `vstart=0` and cut
off by `os88ovl.py`, so it addresses WIN16's variables by the same offsets
the image does. It is fetched when the first dialog, control or message box
is made and dropped after the last. It is 12,761 bytes.

**A modal dialog is the import waiting, again.** `DialogBox` builds the
dialog, sends `WM_INITDIALOG`, disables the owner and then WAITS, as
`GetMessage` and the menu loop already wait. os8088's callbacks run the
dialog's loop: `IsDialogMessage`, then dispatch. A dialog procedure can
open another dialog (WinMine's About opens Preferences), and each loop
keeps its state in its own dialog's record.

**Five bugs worth writing down.**

- **A `BOOL` is AX.** The first dialog repainted forever. A dialog
  procedure answers `BOOL`, a 16-bit value, but the code tested DX:AX, and
  DX held whatever the procedure left in it. Every `WM_PAINT` looked
  "handled" or "not handled" at random. Only AX is read now. The loop also
  takes at most 32 messages a turn and then gives the task back, so a
  procedure that posts itself a message every time cannot hold the machine.
- **`SetFocus` overwrote the message being handled.** Clicking a list row
  selected the wrong one. The click handler called `SetFocus`, which SENDS
  `WM_KILLFOCUS` to the old control, and that send reused the same
  variables as the click's `lParam`. `W16SMKEEP` now saves them around
  every call that can send.
- **The gfx lock deadlocked twice.** Both were "do I hold the lock" being
  wrong in a nested call. First, a resize painted a window synchronously in
  the middle of a callback and reset the flag to "not held". Second, a
  program that exited inside a key callback freed its windows, which took
  the lock it already held. The flag is now saved and restored around every
  nested use, and the end-of-callback work runs with the lock still held.
- **The close box froze the machine.** Found while taking the S6
  screenshots: GDICARD ends as soon as it gets `WM_CLOSE`, and it ended
  inside the close callback, which the kernel calls with the gfx lock held.
  WIN16's flag said "not held", so freeing the program's windows asked for
  the lock again and the UI task waited forever. The whole callback now runs
  with the flag set.
- **Message-box buttons sat off-centre** inside WIN16's own window,
  because the client width was read before the window had been resized.
  The width the layout computed is used instead.

**A dialog can be the whole program.** The DIALOG fixture has no main
window; its dialog is its first window. Rather than open an empty WIN16
window with the dialog beside it, the first window a program shows, dialog
or not, takes WIN16's own os8088 window.

**Mono.** On CGA the button face (`COLOR_BTNFACE`, light grey) became the
50% dither, and the captions on it were hard to read. Windows' monochrome
drivers drew buttons white, so the controls do too on a 1bpp display, with
a black shadow. `GetSysColor` still answers Windows' colours.

**CALC waits.** Windows 3.0's Calculator imports `WIN87EM.__FPMATH`, the
floating-point emulator, so it refuses before it opens anything. Its
Scientific panel, the one `WS_CHILD` dialog in the plan, moves to S10,
after S9 provides the emulator.

**Tested, on the glass (QEMU):**

- DIALOG.EXE on VGA against Windows 3.0's screenshot: radio buttons, check
  box, list, Tab order, EDIT select-all and typing, Enter and Esc; its
  values shown in a WIN16 `MessageBox`; a clean exit;
- WinMine 2.9: About, then Preferences; Expert (24 x 24, 99 mines); a
  custom 20 x 12 board with 30 mines typed into its EDITs;
- MODELESS.EXE: `CreateDialog`, `IsDialogMessage`, `SetDlgItemInt` and
  `GetDlgItemInt`;
- CGA and EGA: DIALOG.EXE and its message box, each on its own grid.

**Not seen:** Hercules (QEMU has none); CALC's Scientific panel (S10); the
icons (S12); a call count on a dialog's first paint (S13's final pass).

## 2026-10-29 — S7: profiles

**The story.** *As a user my chosen difficulty is still there tomorrow.*
WinMine keeps its settings in `WINMINE.INI`, and every installer the end
goal names writes `WIN.INI`. So this is the first story where a Windows
program leaves something on an os8088 disk.

**Where Windows' directory is.** A program's state goes in
`SYSTEM\APPDATA\` (SPEC.md §19.9), so the Windows directory is
`SYSTEM\APPDATA\WIN16\` on the disk WIN16 was launched from. The shared
walk to that folder, `os88data.inc`, only knew "the system disk" and "the
disk the package came from"; a third mode, `OD_TVOL`, names a byte holding
the drive. Every package that already included it assembles byte-identical.
A program that asks for `C:\WINDOWS\WINMINE.INI` gets `WINMINE.INI` there:
only the last part of the name counts until WIN16 has DOS paths (R5).

**One write a file, not one a key.** WinMine writes ten keys as it exits.
On a floppy each `OSAPI_FILE_WRITE` is a FAT update and a directory update,
so ten writes would be ten times the disk time. WIN16 holds each file in a
claim of its own and writes it once, when the program ends (however it
ends) or flushes with `WriteProfileString(NULL, NULL, NULL)`. A disk
without the folder keeps the profile for the session and says nothing.

**Windows 3.0 was asked rather than guessed.** The first draft of the SPEC
assumed four things, and the reference machine disagreed with all four:

- `GetProfileInt` is unsigned: `-42` reads as 0;
- a value in double quotes is answered without them;
- a `NULL` default leaves the buffer alone and answers 0, not `""`;
- a written value is trimmed of blanks at both ends.

The fixture `tests/win16/inifile.c` writes, updates, deletes a key and a
section, writes a quoted value, then reads them back seven ways and shows
the answers in a message box. On WIN16 the message box reads the same as
Windows 3.0's, and the `W16INI.INI` it leaves is byte-identical to the
reference's, including where a deleted section leaves its blank line.

**What WinMine called on its way out.** Expert, then Exit, and WinMine
stopped with *Needs USER.171*: `WinHelp(HELP_QUIT)`, which every program with
a Help menu sends as it closes. That answers `TRUE` now. Then *Needs
USER.420*: `wsprintf`, which WinMine uses to turn each number into the
string it writes. That is USER's own printf, cdecl and variadic, so its
export row says 0 argument bytes and the handler reads the format to know
how many there were. `w16fmt.inc` follows Wine's `wsprintf.c`. Neither
import was in S1's census of what WinMine needs at start-up, because both
are only called at the end.

**Two mistakes on the reference machine, both mine.**

- In Windows 3.0's Minesweeper menu, `e` is the mnemonic of both Expert and
  Exit, so it only moves the highlight; it needs Enter after it. The first
  runs "chose" nothing.
- With Expert really chosen and Windows exited, Windows 3.0 still left no
  `WINMINE.INI`. This is not explained yet. WIN16's file is checked against
  the format the fixture measured, not against a WinMine file from the
  reference.

**Tested, on the glass (QEMU, VGA):**

- INIFILE.EXE: message box and file identical to Windows 3.0's;
- PROFILE.EXE twice: run 1, then run 2;
- WinMine 2.9: Expert, Exit, relaunch opens as Expert; Intermediate, Exit,
  relaunch opens 16 x 16 with 40 mines; `WINMINE.INI` read back on the host;
- a disk without the folder: run 1 both times, no error, nothing written.

**Not seen:** a fifth held file pushing one out; a file over 32,000 bytes.
WinMine 2.9 keeps no best times, so the story's "record a time" waits for a
program that does.

## 2026-10-29 — S8: Minesweeper

**The story.** *As a user I can play the original Windows Minesweeper.* The
target moved up a version: Windows 3.1's Minesweeper (3.0a) has Custom,
Color, marks and best times, which WinMine 2.9 does not. It is 80186 code,
so on an 8088 §124.4's census refuses it with *Needs a V20, 80186 or
better.* before a byte of it runs.

**Three groups of calls.** What it imports that S1–S7 did not provide falls
into sound, bitmaps, and a dozen small USER, KERNEL and SHELL calls. The
small ones are each a row of SPEC.md §124.15.3. Two are not calls at all:
`DOS3Call` is bound at load straight to an `int 21h / retf` in WIN16's code,
and `__WINFLAGS` is a constant the loader writes where the fixup points.

**SOUND was read off Windows' own driver.** No document says exactly what
Windows 3.0's `SOUND.DRV` does with a note, so the driver was disassembled:

- its frequency table is the eighth octave's twelve semitones, shifted
  down by the octave and rounded;
- a note's length is 96,000 / (tempo x length) units of 2.5 ms; normal
  notes sound 7/8 of it, staccato 3/4, legato all;
- its error codes and their order of checking;
- and a bug: the dots argument is accepted and ignored. WIN16 ignores it
  too, because the programs were written against that driver.

The queue plays from WIN16's own tick, with each note's start rounded to the
18.2 Hz tick and the remainder carried in 1/2000 of a tick, so a tune does
not drift.

**Minesweeper is silent by default.** The first runs made no sound at all,
and the reason was in the program, not in WIN16: it only plays when
`WINMINE.INI` says `Sound=3`, and it only ticks with `Tick=1` as well.
With those set, `make test-snd` captured the win tune at 247, 277, 311, 330,
370, 415, 466 and 494 Hz, exactly the driver's table.

**A one-tick note can be two milliseconds long.** The clock tick is a 32nd
note at tempo 120, 62.5 ms, which rounds to one 55 ms tick. But
`StartSound` can be called anywhere inside a tick, and a one-tick tone that
starts near the end of a tick is cut off by the next one. The first capture
had ticks of about 20 ms. The first note after `StartSound` now rounds up
by half a tick, which gives about 74 ms on average.

**One memory DC, sixty-four blits.** Minesweeper keeps its cell pictures in
one 16 x 256 bitmap, selects it into a memory DC, and `BitBlt`s a 16 x 16
cell out of it for each square. So a bitmap here is converted once, when it
is made, into os8088's own sixteen colours in a heap claim of its own, and a
`SRCCOPY` from an even column is one `OSAPI_GFX_BLIT4` straight out of that
claim. An odd column, and `StretchBlt`, go through WIN16's band with a
nearest-pixel scaler. The LEDs and the face are `SetDIBitsToDevice`, which
S4 already had. The `BITMAP.EXE` fixture puts its copy, its 2x stretch and
its mono twin on exactly the pixels Windows 3.0 does.

**The first click is never a mine.** The scripted loss clicked a mine as its
first move and won a cell instead: Minesweeper moves a mine away from the
first click. The script, and later the row, open a safe cell first.

**What the adapters show.** On EGA the grey is darker. That is the program's
own choice: it picks a darker grey on a display under 351 lines. On CGA the
bitmaps go through the mono map, and with *Color* off on Windows 3.1 the
reference machine draws the same picture (`w31-mine-mono.png`). But CGA's
200 lines are fewer than Beginner's window, so its bottom rows are off the
screen. WIN16 does not move or shrink a program's window, so that is a
limit of the machine, written into §124.15.4.

**The row reads the board.** `tests/win16mines.py` does not click blind. It
reads the program's board out of its data segment (32 bytes a row at
0x440, a mine bit and a revealed bit per cell) and asks WIN16's hit test
where the client is. So every click is aimed at a cell whose contents the
row already knows: a flag and its removal, every safe cell for a win, a mine
after a safe cell for a loss, then the `WINMINE.INI` it leaves. Its MartyPC
leg holds an 8088 to the refusal and plays a game on a V20, printing what
each reveal costs.

**Sizes.** WIN16 is a 45,701-byte image, up 3,868 bytes, with 13,064 bytes
of bss: 6,259 bytes are left under the segment. The kernel is unchanged.

**Tested, on the glass (QEMU):**

- W31MINE on VGA: a game won, with the name dialog and Best Times reading
  *Beginner: 128 seconds Os8088*; a game lost; flags and marks; Expert,
  kept across a relaunch; Custom Field; Color off; About; the WinHelp
  refusal;
- the win and loss tunes and the clock tick, from the captured sound;
- EGA and CGA, as described above;
- WinMine 2.9, which ticks and plays its loss tune.

**Not seen:** Hercules (QEMU has none); the two rows, written but not run
until S13's final pass; the 8088 refusal on MartyPC; the V20's cost per
reveal.

## 2026-10-29 — S9: floating point

**The decision first.** D5 was the owner's to take: emulate only the x87
instructions the census found in Calculator, or something wider. The owner
said "go ahead", and the end goal - any Win16 program - decided the width:
the emulator is the whole 8087 except its five transcendentals and its two
BCD instructions. The rest shares Calculator's code, so `fxtract`, `fscale`,
`fsave` and the integer forms cost a table row each. An instruction it lacks
ends the program with its name - *Needs the x87's FPTAN, which WIN16's
emulator does not have.* - and the census tool says so before anything
runs. The plan's D5 row records it as taken and widened, so it can still be
overruled.

**The reference is Windows' own WIN87EM.** winevdm's `win87em` answers the
same calls, but Windows 3.0's `WIN87EM.DLL` 6.00.11 is the thing Calculator
was written against, so it was disassembled. `__FPMATH` is a far call with
the function in `BX`: 0 starts, 1 resets to control word `1332h`, 3 takes
the runtime's error handler, 7 is `fistp` into `DX:AX`, and so on to 12.
Exceptions reach the program as a far call to that handler with WIN87EM's
code in `AX` - `83h` for a zero divide - which is how the C runtime's
`signal(SIGFPE)` works. With a real coprocessor WIN87EM never calls the
program from an interrupt: its `int 2` handler keeps the code, and the
program's next `fwait` delivers it. WIN16 does the same.

**What the loader rewrites.** A Microsoft C program carries native x87 code
with an "OS fixup" on each `wait` byte, six kinds of them. Read off three
programs: kind 5 turns `9B D8` into `CD 34` (`int 34h`), kind 6 turns
`90 9B` into `CD 3D`, and kinds 1-4 turn a segment-overridden escape into
`int 3Ch` plus a byte whose top two bits are the segment. Windows patched
every site and put the native bytes back on first use if a coprocessor was
there. WIN16 decides at load instead: with a coprocessor, only the lone
`fwait`s become `int 3Dh`, where an exception is reported; without one,
every site becomes an interrupt and the emulator part is fetched.

**The emulator is a part, and self-contained.** `w16fp.inc` assembles at
`vstart 0` after the dialog part and is cut out of WIN16.O88 by the same
tool, as part 4 (7,860 bytes, 6.3 KB packed). Its registers, status and
control words live in its own segment and it calls nothing of WIN16's, so
the same bytes load into unicorn on the host. It keeps a value unpacked -
an 80-bit mantissa with sixteen guard bits, an exponent word, a sign - and
rounds every result once, to where it is going: a register at the control
word's precision with the extended exponent range, then an `m64` or `m32`
again on a store. That is the 8087's double rounding, reproduced.

**The oracle is exact arithmetic.** The plan said Python's doubles; doubles
cannot see a 64-bit register, a 24-bit precision mode or a round-toward-zero
store. So `tests/unit/t_w16fp.py` uses `fractions`: every operation exact,
then rounded by a model that does what the 8087 manual says. Unicorn runs
the emulator on PATCHED instruction sequences - the bytes the loader leaves -
and every result is compared bit for bit, with the status word's flags,
condition codes and stack top. Operands are chosen to hit ties, gradual
underflow, overflow on a store, signed zeros, infinities and NaNs. The
first run found fifteen bugs, among them the indefinite NaN's bytes (in the
oracle), an infinity stored with stale mantissa bits, `fistp` raising
precision beside invalid, and the edge case -2^15 being refused. Since then
it has passed 3,000-4,000 seeded cases per seed on several seeds. The same
row runs every instruction the census can name through the emulator and
checks that exactly the "lacks" list ends a program, so the census report
and the emulator cannot disagree.

**FPX, an exception fixture, and Windows' answer to it.** `fpx.c` installs
`signal(SIGFPE)`, unmasks zero-divide, divides 1 by 0 and longjmps out of
the handler; then prints 1/0 masked, 1/3 and sqrt(2) to fifteen places. On
the Windows 3.0 reference machine the first build died with *Not enough
memory to allocate file structures*: `signal()` pulls in stdio, and the
default 1 KB local heap was too small. Linked with a 16 KB heap it shows
`SIGFPE`, `infinity`, `0.333333333333333`, `1.414213562373095`. The
fixture rule now takes per-program `wlink` options from a `.wlk` file.

**FPX's "masked" line is the machine's answer.** On the XT the first FPX
never reached its message box: `caught` climbed by about a thousand a
second. Watcom's runtime looks for an 8087 with a bare `fninit` and
`fnstcw` that carry no OS fixup, and an 8086 has no way to trap an ESC, so
with no coprocessor those are no-ops and the runtime finds none. Its
`_fpreset` and `_control87` then do nothing. WIN87EM's own control word,
`1332h` (Microsoft C's default, which leaves zero divide unmasked), stands,
so the "masked" 1/0 raised SIGFPE again and the handler longjmped back into
the loop. Windows' WIN87EM cannot trap those bytes either, so on a Windows
3.0 machine with no coprocessor this Watcom program does the same; that is
inferred, not seen, as no reference machine here lacks an FPU. The fixture
now guards the masked division with its own `setjmp`, and on the XT it
prints `1/0 masked: SIGFPE (no 8087 to mask)`. QEMU cannot show this case
even with `-DW16_X87_EMU`: the probe meets QEMU's real FPU and the mask
takes. So the knob also clears `WF_80x87` from `GetWinFlags`, and only the
XT leg expects the second SIGFPE. Windows 3.0 still prints the original
four lines for the changed fixture.

**Seen on the glass.** On QEMU, which has a coprocessor, FPU.EXE and FPX.EXE
print exactly Windows 3.0's text - read off the program's stack, the text
pointer MessageBox was given - and the unmasked 1/0 travels QEMU's IRQ 13,
SeaBIOS's `int 75h`, WIN16's `int 2` and the next `int 3Dh` into the
program's own signal handler. A test build, `-DW16_X87_EMU`, takes the
emulator on QEMU too: the same four lines. On MartyPC's 4.77 MHz V20 5150,
with no coprocessor, FPU.EXE prints its three lines through the emulator
and FPX its four (the second being `SIGFPE (no 8087 to mask)`, below). From
the double-click to the message box, with WIN16, the program and the
emulator part all read off a 360 KB floppy first, FPU.EXE took 18.6 s and
FPX 15.4 s; that time was not split into loading and arithmetic.

**Calculator gets as far as its message loop.** It imports `__FPMATH` by
NAME, which WIN16's loader had never bound, so a small by-name table came
in. Then it exited at once: a gate trace showed `RegisterClass` refused its
class, because its main window is a dialog-class window asking for
Windows' `DLGWINDOWEXTRA`, 30 bytes of window extra, and WIN16 allowed 16.
Raising that to 32 costs 768 bytes of bss. Calculator then runs its whole
start-up - the floating point, its profile, its dialog items - into
`GetMessage`. Its window is a dialog with its own class, which is S10's
story, so KB Q72540 (2.01 - 2 showing 0.00) is read there.

**Found on the way, for S11.** The stock 8088's leg refused FPU.EXE as
*Needs an 80386 or better.* The host census says 8086 for the same bytes.
The in-guest census reaches offset 0x13 of the code segment - the immediate
byte of `sub sp, 60h` - as an instruction, reads `60` as a 186 `pusha`, and
goes on from there. It does this without S9's patches too, so it is S2's
walk, and no 8088 run had been taken before. S11 owns the CPU floor; this
leg runs on the V20 until then.

**Making the XT bearable.** Counting host instructions per emulated
instruction in unicorn showed `fdiv` at 3,034, most of it an 80-step
restoring division whose remainder lived in memory. With the remainder in
four registers and its 65th bit carried in CF from one step to the next it
is 1,702, and each of those is a register operation rather than a
read-modify-write of memory. Rounding was rewritten the same way: word
masks instead of shifting the mantissa down and back bit by bit.
