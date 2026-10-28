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
