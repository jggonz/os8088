# WIN16 — running Windows 3.x programs on os8088

**STATUS: PLAN, with the owner's decisions taken (2026-10-28). Nothing here is
built.** Every size is an ESTIMATE unless the line says MEASURED. The census
figures below are MEASURED with a scratch script over the sample binaries; S1
turns that script into the shipped tool. The binding contract is SPEC.md
§124, pinned story by story as this plan's checkboxes are ticked. The diary is
[HOW-WIN16-WAS-BUILT.md](../HOW-WIN16-WAS-BUILT.md).

The ask: double-click the original Windows **Minesweeper** or **Calculator**
in a Disk window and play it in an os8088 window that looks the way it did on
Windows, through a compatibility layer, "a Wine for os8088", that lives on
disk until a Windows program is opened.

---

## The verdict, before the detail

**It is feasible, and for the same reason DOS.O88 was** (SPEC.md §96): a
Windows 3.x program is 16-bit x86 machine code for the processor we are
standing on, and Windows 1.x–3.0 ran such programs in 8086 real mode. There is
no CPU to emulate. "Running" a Windows program is:

1. loading its NE segments;
2. applying its relocations;
3. pointing its `KERNEL`, `USER`, `GDI`, `SOUND` and `WIN87EM` imports at
   far-callable code of ours;
4. calling its entry point.

Six findings shape everything below.

1. **The layer is a PACKAGE, `WIN16.O88`, and costs zero resident bytes.**
   - **Home:** this is DOS.O88's argument from `docs/plans/DOS-EXEC-PLAN.md`
     §3, and it holds unchanged. A package is on disk until it is wanted, has
     its own segment and data, and is launched by the file association that
     already exists (SPEC.md §54). A kernel module's data would be resident
     (SPEC.md §2.8).
   - **Two levels of on demand:** the layer loads only when a Windows `.EXE`
     is opened. Its larger pieces load only when a program reaches them, as
     package PARTS (SPEC.md §20.12): the dialog manager and controls, the
     floating-point emulator, and the loader itself, which is dropped once the
     program is running.

2. **The stack question answers itself, at zero kernel bytes.**
   - **The constraint:** a Windows program runs with `SS = DS = DGROUP`, its
     own data segment, and is compiled to assume it. Every os8088 task runs on
     `SS = LOW_SEG`, and a context switch swaps only SP (`kernel/sched.inc`
     lines 20-24).
   - **What the kernel already does:** the scheduler refuses to switch when it
     finds `SS != LOW_SEG`. It skips that tick rather than park a foreign SP
     (`kernel/sched.inc` lines 2389-2397, SPEC.md §8.5). Both private-stack
     ISRs tolerate a foreign SS too: the mouse ISR banks SS:SP
     (`kernel/mouse.inc` line 396), and the tick's ROM chain runs in place
     off-`LOW_SEG` (`kernel/sched.inc` line 2190). The kernel had to learn all
     three for ROM services that hop stacks.
   - **Why that fits:** while Windows code runs it is simply not pre-empted,
     which is exactly Windows 3.x's own cooperative contract. The layer swaps
     back to the task's own stack for every kernel call, so pre-emption
     resumes inside each `OSAPI_*` call and whenever the program is waiting
     for a message.

3. **The program runs as a COROUTINE on the UI task (D2, taken).**
   - **Why not a worker:** a package's worker may not touch a file and must be
     spawned under the gfx lock (SPEC.md §20.6).
   - **What the coroutine does instead:** a Windows program blocks in
     `GetMessage`. The layer resumes the program on its own stack from inside
     an os8088 callback (a paint, a click, a timer) and switches back when the
     program asks for its next message with the queue empty. That is how
     Windows 3.x itself scheduled programs.
   - **What it buys:**
     - `WM_PAINT` is delivered *inside* os8088's `W_PAINT`, under the gfx lock
       and the damage clip (SPEC.md §11.90), so a repaint costs what the
       program draws and nothing is drawn twice.
     - INI writes happen on the UI task, where files are allowed.
     - Modal dialogs are nested message loops, which a coroutine gives for
       free.

4. **The CPU floor is a V20 (D1, taken), and the census MEASURED that it is
   the right floor.**
   - **Minesweeper:** every Duff Software WinMine (2.6, 2.8 and 2.9, the
     1990 Windows 3.0 releases that became Microsoft's Minesweeper) uses the
     80186 set: 311 `push imm8`, 64 `push imm16`, 43 `shr/shl r,imm`, 5
     `imul r,imm` in WinMine 2.9. It has **no** 286 or 386 opcodes.
   - **Windows 3.0's own applets:** CALC, REVERSI, CLOCK, NOTEPAD and SOL are
     8086-clean. Their handful of hits are linear-sweep noise inside data
     (`movhps`, `cmovo`), which is why S1's tool sweeps from the entry points.
   - **Why an 8088 must refuse:** it does not fault on 186 opcodes. It
     executes them as something else: `0x60`–`0x6F` alias the conditional
     jumps, and `0x0F` is `pop cs`. So the census at load time is mandatory.
   - **What each CPU gets:**
     - **V20/V30/186 and 286+ (`kern_big`, real mode):** both classes run
       natively.
     - **8088/8086:** runs the 8086-clean ones, and refuses a 186 program
       with the reason on the glass.
   - **Windows 3.1 builds:** compiled `-G2` for the 286, but a real-mode 286
     program only adds 186-level instructions. The 286's own additions are
     protected-mode system instructions (`0F xx`, `arpl`) that an application
     does not issue. MEASURED: Microsoft's WINMINE.EXE 3.10 is 80186 with no
     286 opcodes, so it runs on a V20. CALC.EXE 3.10 is 186 plus x87, and its
     few `0F`/`66` hits sit in the x87 code's data, for S1's recursive sweep
     to confirm.

5. **Calculator needs floating point; Minesweeper does not.** MEASURED:
   - **CALC 3.00:** imports `WIN87EM.__FPMATH`, carries 288 `wait`+ESC x87
     instructions, and has 348 NE OS fixups (types 5: 288, 6: 57, 3: 3). Those
     fixups are the MS C emulator convention: a loader patches each site to a
     real x87 instruction when a coprocessor is present, or to an
     `int 34h`–`3Dh` emulator call when not.
   - **WinMine:** has none.
   - **What that means here:** CALC on a machine without an 8087 needs an
     emulator for exactly the x87 subset it uses (S9).

6. **Wine and winevdm are SOURCES, not just references (D7, taken).**
   - **What is brought in:** whatever is needed. The four Wine `.spec`
     ordinal tables plus winevdm's `sound` and `win87em` ones, vendored. Wine's
     System bitmap font (`fonts/system.sfd`, the 16 px strike that matches
     Windows' VGA system font). And their logic: the NE loader, dialog-template
     parsing, `DefWindowProc`/`DefDlgProc`, and the BUTTON/EDIT/STATIC/LISTBOX
     behaviour, translated into 8086 assembly.
   - **Licence:** Wine is LGPL-2.1-or-later. winevdm's top-level `LICENSE` is
     GPL-2 with no version pinned (so GPL-2 §9 lets a recipient choose any),
     and its Wine-derived files are LGPL-2.1-or-later. So **`apps/win16/` is
     GPL-2.0-or-later**, with `COPYING` and a `README.TXT` shipping beside
     `WIN16.O88`. That is `apps/apple2`'s footing (docs/APPLE2-SPEC.md section 1.3).
     The rest of the tree is untouched.
   - **Why translated, not compiled:** the C does not go through the C
     toolchain (SPEC.md §73). Win16's API is built on `LONG`/`DWORD`, which
     §73 refuses, and Wine's USER/GDI call Win32 underneath. So the code is
     translated into 8086 assembly, and each translated routine names its
     Wine or winevdm source file in a comment.

---

## The programs, MEASURED

The samples live in the git-ignored `win16ref/`, which the win16-reference
skill recreates from archive.org and minesweepergame.com, every file
SHA-256-pinned. **No Microsoft binary is ever committed.** The build takes
them from `WIN3X=<dir>`.

| | WinMine 2.9 (Duff Software, July 1990) | CALC 3.00 (Windows 3.0) |
|---|---|---|
| source | minesweepergame.com, WinMine-2.9.zip (also the CD-archive WINMINE.ZIP, byte-identical) | archive.org `w-3_20231227`, `WINDOWS/CALC.EXE` |
| SHA-256 | `a08690c1…2b812aa4e8` | `9c40900d…5c2f731c1` |
| file | 27,062 bytes | 40,480 bytes |
| NE | flags 0x0302 (MULTIPLEDATA), expected version 3.00 | flags 0x0302, 3.00 |
| segments | 1 code (10,078), DGROUP 1,994 + heap 4,096 + stack 8,192 | 4 code (26,086 total), DGROUP 3,412 + heap 2,048 + stack 6,144 |
| CPU | **80186** (V20 or better) | **8086** + x87 via OS fixups |
| modules | KERNEL, GDI, USER, SOUND | KERNEL, GDI, USER, WIN87EM |
| imports | 83 distinct: KERNEL 21, USER 48, GDI 8, SOUND 6 | 93 distinct: KERNEL 22, USER 57, GDI 13, WIN87EM 1 |
| bitmaps | 410 = 16x256x4bpp (16 cells of 16x16), 411 = 13x276x4bpp (LED digits), 412 = 24x120x4bpp (5 faces of 24x24) | none: it draws its keypad (`RoundRect`, `TextOut`) |
| menus | Game (New F2, Beginner/Intermediate/Expert, Sound, Marks (?), Preferences… F3, Exit), Help | Edit (Copy, Paste), View (Scientific, Standard), Help |
| dialogs | 70 About (STATIC); 80 Preferences (radio, check boxes, group box, three single-line EDIT) | 1 the Scientific panel, built with `CreateDialog` + `IsDialogMessage` (radio, check boxes, STATIC); 2 Statistics Box (LISTBOX, buttons); 3 About |
| also | `GetPrivateProfileInt`/`WritePrivateProfileString` (WINMINE.INI), SOUND.DRV voices, `SetDIBitsToDevice`, `SetCapture`, `SetTimer` | `GetProfileInt`/`WriteProfileString` (WIN.INI), clipboard, `GetTextMetrics`, `MapDialogRect` |

Together the two programs import **130 distinct functions**, 46 of them
shared: USER 77, KERNEL 30, GDI 16, SOUND 6 and WIN87EM 1. The exact lists
are in the diary's 2026-10-28 entry, and S1 makes them a checked-in census
report that the build regenerates from `WIN3X`.

**Windows 3.1's own programs** came out of a from-scratch install off the
retail floppies (below). They are the same shapes, grown:

| | WINMINE.EXE 3.10 | CALC.EXE 3.10 |
|---|---|---|
| SHA-256 | `633c1328…eb0c35f81b` | `09b5e717…a586468cf` |
| file | 27,776 bytes, expected version 3.10 | 43,072 bytes, 3.10 |
| CPU | **80186** | **80186** + x87 |
| modules | KERNEL, GDI, USER, SOUND, **SHELL** | KERNEL, GDI, USER, WIN87EM, **SHELL** |
| new over 3.0 | `ShellAbout`, `__WINFLAGS`, `DOS3Call`, `BitBlt`, `CreateDIBitmap`, `CreateCompatibleDC`, `SetPixel`, `GetPrivateProfileString`; **1bpp twins of all three bitmaps** (410/411, 420/421, 430/431) for monochrome displays | `ShellAbout`, `__WINFLAGS`, `DOS3Call` |
| menus | Game: New, Beginner/Intermediate/Expert/**Custom…**, Marks (?), **Color**, **Best Times…**, Exit | as 3.0, shortcuts Ctrl+C/Ctrl+V |

**The reference machines.** The skill `.claude/skills/win16-reference`
recreates two QEMU machines from archive.org into the git-ignored
`win16ref/`: MS-DOS 6.22 with **Windows 3.0 installed from its seven 720K
disks** (run in real mode, `win /r`), and MS-DOS 6.22 with **Windows 3.1
installed from its retail 1.44MB disks** (standard mode). Their screenshots
are what S4–S10 are compared against. Three findings came off them already:

- **Calculator's `2.01 - 2` is `0.00`** on both 3.0 and 3.1, so S9's oracle
  is the real program's answer.
- **A Windows menu check mark is a ✓ glyph**, not `os88ui.inc`'s solid
  square (D8).
- **WinMine 2.9 runs in real mode** exactly as WIN16 will run it, on an
  8086-class layout of memory, so it is the closest reference there is.

---

## What "run a Windows program" decomposes into

| # | problem | who provides it today | work |
|---|---|---|---|
| 1 | NE load: segments, relocations, prologs, DGROUP, local heap, InitTask | nobody (DOS.O88's MZ loader is a model, not a part); Wine's `ne_segment.c` is the logic | medium |
| 2 | The message model: queue, `GetMessage`/`DispatchMessage`, the coroutine | nobody | medium |
| 3 | Windows: top-level and child windows, classes, `DefWindowProc` | **the window manager (SPEC.md §11)**, one os8088 window per top-level window | medium |
| 4 | GDI: DCs, pens, brushes, text, bitmaps, `SetDIBitsToDevice` | **`OSAPI_GFX_*`**, the 4bpp blitter (SPEC.md §5.4.1.3), proportional type (`apps/os88type.inc`) | large |
| 5 | Resources: menus, accelerators, strings, bitmaps, icons, dialog templates | nobody; read out of the in-memory image | medium |
| 6 | Dialogs and controls: BUTTON, STATIC, EDIT, LISTBOX, `MessageBox` | **`apps/os88ui.inc`** and `os88line.inc` (SPEC.md §13.8–§13.17, §83), restyled to Windows 3.0's look | large |
| 7 | Menus | **`OS88UI_MENU`**, the in-window bar (SPEC.md §13.16), which already draws a tick (§13.16.2.1) | small |
| 8 | Profiles: `GetProfileInt`, `WritePrivateProfileString` | **`SYSTEM\APPDATA`** (SPEC.md §19.9), but no INI parser | small |
| 9 | Timers, capture, sound, clipboard, About | **`OSAPI_WM_TIMER`**, press tracking (SPEC.md §13.7), `OSAPI_SND_TONE`, `OSAPI_CLIP_*`, `OSAPI_ABOUT_SET` | small |
| 10 | Floating point (CALC) | nobody; winevdm's `win87em` is the reference | medium–large |
| 11 | Launch from a double-click | **assoc (SPEC.md §54)**: `EXE` is DOS.O88's, so DOS.O88 forwards an `NE` file | ~nil |

---

## Delivery stories

Each wave is a reviewable commit pushed to the same draft PR (#320). A story
is ticked only when its outcome has been **seen on the glass** and its named
rows exist. "Pinned" means SPEC.md §124 has the contract and the code does
not. S8 and S10 are the two the ask is about; everything before them is what
they stand on.

- [x] **S0: Foundation.** Isolated worktree (`~/Repos/os8088-win16`, branch
  `plan/win16`), this plan, SPEC.md §124's contract skeleton, the diary, a
  draft PR, the owner's decisions, and the sample binaries censused.
  *Verify:* `make` passes (checkdocs, the index, the fast tier).

- [ ] **S1: Census.** *As a developer I can point the build at my Windows
  files and see exactly what each one needs before a line of the layer
  exists.*
  - `tools/os88ne.py` (host Python) reports:
    - the NE header and segment table;
    - every import as `MODULE.ordinal` plus its name, from the vendored
      `.spec` files;
    - the resource directory with dialog control classes and menu items;
    - the OS-fixup counts;
    - an **instruction census per code segment by recursive descent from the
      entry points**, so data in a code segment is not counted. Its verdict
      is 8086, 186 or 286 plus x87.
  - `make win16census WIN3X=<dir>` runs it over every `.EXE` and `.FON` in
    the user's directory.
  - The vendored ordinal tables go in `apps/win16/spec/`.
  - **Fixtures, two kinds** (D10):
    - **A minimal NE assembled by `nasm -f bin`**, `tests/win16/tiny16.asm`,
      with a hand-built header. It needs no compiler, so the fast row can
      always run.
    - **Win16 programs in C**, `tests/win16/*.c`, built by Open Watcom
      (`tools/setup-watcom.sh`, `-0 -bt=windows -l=windows`) with
      `make win16fixtures`. Each covers one story's API: MessageBox; a class,
      a window, a paint and a timer; a menu; a dialog; a profile; a bitmap.
      Every fixture is run on the reference machines first, so its expected
      picture is Windows' own.
    - Without the user's files, every gate still has something to load.
  - *Verify:* a fast row over the fixture (`win16ne`). Over the samples it
    must reproduce this plan's table: WinMine 2.9 is 186, and CALC 3.00 is
    8086 plus x87.

- [ ] **S2: Load or refuse.** *As a user, when I double-click a Windows
  program I get either the program, or one sentence saying why not, and never
  a crash.*
  - **Launch:** DOS.O88 sniffs `e_lfanew` → `NE` and forwards the document to
    `WIN16.O88` with `OSAPI_PKG_START`'s document form (SPEC.md §21.5.3).
  - **Loader:**
    - all segments go into ONE arena claim, loaded fixed: never moved,
      never discarded (D3);
    - relocations are applied, including chains and additive records;
    - exported prologs are patched as Wine's `NE_FixupSegmentPrologs` does;
    - DGROUP is built with its local heap and stack, and `InitTask`'s
      register contract is honoured;
    - `hInstance` = the DGROUP paragraph.
  - **Refusals, each worded:**
    - an import the layer lacks: *"WINMINE needs USER.287 (name), which
      WIN16 does not provide yet"*;
    - a 186 program on an 8088: *"needs an NEC V20, 80186 or better"*;
    - a self-loading or library module;
    - out of memory.
  - *Verify:*
    - the fixture reaches its WinMain;
    - a doctored fixture with an unknown import refuses with its name;
    - `kern_mega` refuses with `OS88_MEGA_REFUSE`;
    - `kern_small`'s disks do not carry it (D6).

- [ ] **S3: The message pump.** *As a user I see the program's window, I can
  move it, click in it, and close it with the close box.*
  - The API: `RegisterClass`, `CreateWindow`, `ShowWindow`, `UpdateWindow`,
    `MoveWindow`, `SetWindowPos`, `DestroyWindow`, `DefWindowProc`, the
    `GetMessage`/`PeekMessage`/`TranslateMessage`/`DispatchMessage` loop,
    `PostMessage`/`SendMessage`, `PostQuitMessage`, `SetTimer`/`KillTimer`,
    `SetCapture`/`ReleaseCapture`, `GetTickCount`/`GetCurrentTime`.
  - The coroutine of finding 3, and the stack gate of finding 2.
  - Each top-level window is one os8088 window.
  - Messages: `WM_CREATE`, `WM_PAINT`, `WM_ERASEBKGND`, `WM_LBUTTON*`,
    `WM_RBUTTON*`, `WM_MOUSEMOVE`, `WM_KEY*`, `WM_CHAR`, `WM_TIMER`,
    `WM_COMMAND`, `WM_CLOSE`, `WM_DESTROY` and `WM_QUIT`.
  - WinMine's `WS_MINIMIZE`-then-resize start (winevdm issue #181) sizes
    correctly.
  - The close box and a Task Manager kill both tear down every claim.
  - *Verify:* the fixture's window opens, repaints only its damage, counts
    timer ticks and closes cleanly; `mem_tab` is back to its starting state.

- [ ] **S4: GDI and the Windows look.** *As a user the program's window looks
  the way it did on Windows on that display, in colour on VGA/EGA and
  legibly on CGA and Hercules.*
  - **DCs:** `BeginPaint`/`EndPaint`, `GetDC`/`ReleaseDC`.
  - **Drawing:** stock and solid pens and brushes, `SetROP2`, `Rectangle`,
    `RoundRect`, `FillRect`, `MoveTo`/`LineTo`.
  - **Text:** `TextOut`, `SetTextColor`, `GetTextExtent` and
    `GetTextMetrics`, in the **Windows System font for that adapter**, drawn
    proportionally through `os88type.inc` (D4).
  - **Bitmaps:** `SetDIBitsToDevice` from a DIB resource goes to
    `OSAPI_GFX_BLIT4`, with rows flipped and the palette remapped to the 16
    desktop colours, one call per visible cell.
  - **`GetDeviceCaps`/`GetSystemMetrics`/`GetSysColor`:** answer what
    Windows 3.0 answered on that adapter (VGA 640x480x16).
  - **Mono:** a 1bpp mapping that follows the desktop's own grey rules
    (SPEC.md §39).
  - **The cost rule:** priced in primitive calls, not pixels (PERFORMANCE.md
    Part 5).
  - *Verify:* the fixture's test card, and a side-by-side against the
    reference machines' screenshots (`win16ref/ref/`, the win16-reference
    skill) on VGA, and on EGA and CGA after their `SETUP`; a call counter on a
    full Minesweeper repaint written into the diary.

- [ ] **S5: Resources and menus.** *As a user the program's menu bar sits in
  its window as it did on Windows, its shortcuts work, and its check marks
  show.*
  - `LoadMenu`/`SetMenu`/`GetMenu`/`GetSubMenu` → an `OS88UI_MENU` bar inside
    the window (D8).
  - `CheckMenuItem`/`EnableMenuItem`, and WinMine's old `ChangeMenu`.
  - `GetSystemMenu` maps to nothing: the os8088 frame is the frame.
  - `LoadAccelerators`/`TranslateAccelerator`, `LoadString`, `LoadIcon`
    (the dock and About card), `LoadCursor`/`SetCursor`/`ShowCursor` (mapped
    to the arrow), and `FindResource`/`LoadResource`/`LockResource`.
  - *Verify:* WinMine's Game menu checks the level and Marks; F2 and F3 work;
    CALC's View menu switches Standard and Scientific.

- [ ] **S6: Dialogs and controls.** *As a user the program's dialog boxes
  open, I can type in them, and OK/Cancel do what they say.*
  - `DialogBox`, `CreateDialog`, `IsDialogMessage`, `EndDialog`,
    `MapDialogRect`, and template parsing with the dialog units of D4.
  - Controls, styled as Windows 3.0 drew them: BUTTON (push, default, check
    box, radio, group box), STATIC, single-line EDIT, and LISTBOX for CALC's
    Statistics Box. Each is a child of the dialog's one os8088 window.
  - `GetDlgItem`, `SetDlgItemText`/`Int`, `GetDlgItemInt`,
    `CheckRadioButton`, `CheckDlgButton`, `IsDlgButtonChecked`,
    `EnableWindow`, `SetFocus`.
  - `MessageBox`.
  - **Loaded as a part**, fetched on the first dialog and dropped after the
    last.
  - *Verify:* WinMine's Preferences (custom height, width and mines) and
    About; CALC's Scientific panel.

- [ ] **S7: Profiles.** *As a user my best times and my chosen difficulty are
  still there tomorrow.*
  - `GetProfileInt`/`GetProfileString`/`WriteProfileString` and the
    `PrivateProfile` forms.
  - A minimal INI reader and writer over `SYSTEM\APPDATA\WIN16\` (SPEC.md
    §19.9): `WIN.INI` and each program's own `.INI`.
  - Writes are made on the UI task, so the file rules hold.
  - *Verify:* set Expert, record a time, close, reopen; the INI on the host
    reads back with `mtools`.

- [ ] **S8: Minesweeper.** *As a user I can play the original Windows
  Minesweeper on os8088, every level, with the timer, the mine counter, the
  face, marks, colour and best times, on every adapter my machine has.*
  - SOUND.DRV's voice API (`OpenSound`, `SetVoiceNote`, `SetVoiceAccent`,
    `StartSound`, `StopSound`, `CloseSound`) plays through `OSAPI_SND_TONE`.
  - WinHelp refuses with its reason, greyed (SPEC.md §47).
  - The CPU it needs is stated on the glass when it is not met.
  - *Verify:* QEMU and MartyPC-with-V20 rows (`win16mines`) play a scripted
    game to a win and a loss; the four adapters are looked at; the XT's
    frame cost is taken in the final pass.

- [ ] **S9: Floating point.** *As a user Calculator gets the right answer
  whether or not my machine has a coprocessor.*
  - With an 8087/287/387 present: the OS fixups are patched to native
    instructions.
  - Without one: an emulator part of exactly the x87 subset S1's census
    found, with winevdm's `win87em` as the reference.
  - The 3.0 decimal-subtraction bug (KB Q72540) is reproduced, not fixed:
    this is the original program.
  - *Verify:* a host oracle compares the emulator against Python's IEEE
    doubles over the census's opcode set; a QEMU leg runs with an FPU and a
    MartyPC leg without.

- [ ] **S10: Calculator.** *As a user I can use the original Windows
  Calculator in Standard and Scientific modes, copy and paste through the
  os8088 clipboard, and use its Statistics Box.*
  - The clipboard through `OSAPI_CLIP_PUT`/`GET`
    (`OpenClipboard`, `GetClipboardData`, `IsClipboardFormatAvailable`).
  - *Verify:* the `win16calc` row keys a calculation in each mode and reads
    the display, then pastes a number in from Note Pad.

- [ ] **S11: The CPU floor on the glass.** *As an XT owner, the Windows
  programs my CPU can run, run, and the rest tell me plainly what they need.*
  - On a stock 8088, the 8086-clean programs run: CALC, CLOCK and REVERSI
    from Windows 3.0.
  - WinMine says it needs a V20.
  - On a V20 everything runs.
  - The V20/186 test is `apps/redline/detect.inc`'s, shared as source.
  - *Verify:* MartyPC as a stock 8088 and as a V20, and QEMU as a 386.

- [ ] **S12: Living with the desktop.**
  - Two Windows programs at once (two `WIN16.O88` instances).
  - Minesweeper beside Paint.
  - Close and kill from the Task Manager.
  - Hibernate across a running game.
  - The live media's payload, or `EXEMPT_DIRS` with its reason
    (`tests/unit/t_livefull.py`).
  - `make win16disk WIN3X=<dir>` in all four geometries.
  - *Verify:* `mem_tab` returns to its pre-launch state after each close;
    the Task Manager shows the program's own name.

- [ ] **S13: Acceptance.**
  - Every row S1–S12 named, as one deduplicated soak union, then
    `make test-full`.
  - The four adapters, the XT (MartyPC as an 8088 and as a V20), a 286
    (86Box `vm/286`) and a 386 (QEMU).
  - Measured resident cost (zero kernel bytes, checked with
    `tools/kernsize.py`), and the package image and claims MEASURED.
  - The diary closed with screenshots, against Windows 3.0 reference
    screenshots where those exist, and the limitations stated.

---

## Decisions

Taken by the owner on 2026-10-28 unless marked otherwise.

| # | decision | taken | alternative judged |
|---|---|---|---|
| D1 | CPU floor | **V20.** The census at load decides per program: 8086-clean runs everywhere, 186 needs a V20/V30/186/286+, and an 8088 refuses a 186 program with the reason. No 8088 translation tier | Load-time translation of 186 opcodes for the 8088, or interpreting the program's code: not wanted |
| D2 | Where the program runs | **A coroutine on the UI task** (finding 3) | A worker task (SPEC.md §20.6): no file access, and paint deferred out of `W_PAINT`, so every exposure draws twice |
| D3 | Memory model (recommendation; not raised) | Every segment fixed in one arena claim; nothing moves or discards; `GlobalAlloc` sub-allocates inside a second claim (8 claims per owner, SPEC.md §50.3) | Real-mode Windows' movable segments: `INT 3Fh` loader stubs and BP-chain walking, for two programs that fit easily |
| D4 | Fonts | **What Windows showed on that display**, so the programs look as they did on Windows. MEASURED from Windows 3.0's setup disk 2 (`CGASYS`, `EGASYS`, `VGASYS.FON`), all proportional with maximum width 14: **CGA 8 px (average 7, ascent 7); EGA 12 px (7, 10); VGA 16 px (7, 13)**. **Hercules uses EGA's 12 px font**: `SETUP.INF` gives it EGA's resolution class, `133,96,72`. VGA's face comes from Wine's System font, whose 16 px strike matches (`fonts/system.sfd`). CGA's and EGA's come from Windows' own `.FON` when the user supplies their Windows directory, and otherwise from os8088 strikes drawn to those metrics from source art (CLAUDE.md's artwork rule). Each is converted on the host into an `.F88` face (SPEC.md §6.3) and drawn through `os88type.inc`. Dialog units follow the font, as on Windows | os8088's 8x8 cell everywhere: dialogs half height on VGA and nothing like Windows |
| D5 **gate** | Floating point without a coprocessor | Emulate only the census's opcode subset, as a part. The subset comes from S1 | A whole x87 emulator, or refusing CALC on FPU-less machines |
| D6 | Kernels | **`kern_big` and `kern_emu`.** `kern_mega` refuses (`OS88_MEGA_REFUSE`, SPEC.md §20.18). `kern_small` lacks `OSAPI_WM_TIMER` (SPEC.md §13.9), so its disks do not carry it (§24.5) | A kern_mega arm giving programs real selectors, winevdm's model: later |
| D7 | Sources and licence | **Bring in whatever is needed from Wine and winevdm.** `apps/win16/` becomes GPL-2.0-or-later (finding 6), `apps/apple2`'s footing. Microsoft binaries are still never committed | Clean-room MIT: slower, and gains nothing the owner wants |
| D8 | Menus and check marks | **The in-window bar, `OS88UI_MENU`** (SPEC.md §13.16): a Windows program's menu bar is inside its window, and the package owns it, so it costs no kernel bytes. The element's tick is a solid square, and the reference machines show Windows drawing a ✓ glyph, so S5 adds a check-glyph option to `os88ui.inc` (shared source) | The kernel's global bar: at most 5 menus, no check marks, and not where Windows put it |
| D9 | Name | **`WIN16.O88`**, `apps/win16/` | — |
| D10 | Compiler for Win16 TEST programs | **Open Watcom v2** (`tools/setup-watcom.sh`): the dated `2026-10-01-Build` snapshot, SHA-256-pinned, ~150 MB, into `build/watcom/`, which `make clean` spares. It runs natively on this Mac (`armo64`) and builds genuine Win16 NE programs with 8086 code (`-0`). MEASURED: a 1,906-byte HELLO16.EXE built this way runs on the Windows 3.0 reference machine in real mode. The shipped layer stays 8086 assembly (D7) | gcc-ia16: no Win16 target, and harder to install here. SmallerC (SPEC.md §73): no `long`, so no Win16 API |

---

## Memory and speed, ESTIMATED

**Memory:**

- **The layer's image:** under `APP_MAX_SIZE`, with dialogs and floating
  point as parts. A guess of 30–40 KB in the package, against Paint's
  ~22 KB and ETHER.DRV's ~11 KB.
- **The programs' own segments** (MEASURED from the NE headers):
  - **WinMine:** 10 KB of code and a 14 KB DGROUP.
  - **CALC:** 26 KB of code and a 12 KB DGROUP.
  - Plus a `GlobalAlloc` claim, and the System font strike (~4 KB at 16 px).
- **One program's total:** about 80–100 KB.
- **What that means per machine:** two programs fit a 640 KB machine
  comfortably. A 256 KB machine runs one, and the second refuses before any
  I/O with the arithmetic, which is WEAVE's floor-machine shape (WEAVE-SPEC §1.4).

**Speed**, on a 4.77 MHz V20 or 8088, priced from PERFORMANCE.md:

- **One 16x16 Minesweeper cell:** one `OSAPI_GFX_BLIT4`. The planar decoder
  prices it at about 107 cycles a pixel, so roughly 6 ms.
- **A Beginner board (81 cells):** about 0.5 s to paint.
- **An Expert board (480 cells):** about 3 s on the XT and a fraction of that
  on a 286.

That is the case for drawing only the damage, which the coroutine model makes
natural. S13 measures it; nothing here is promised.

---

## Existing facilities reused

docs/INDEX.md was read first.

| what | facility |
|---|---|
| launch | the association of `EXE` with DOS.O88 (SPEC.md §54, §96) and `OSAPI_PKG_START`'s document form |
| code loaded on demand | package parts (SPEC.md §20.12, `apps/os88parts.inc`) and `OSAPI_PKG_REHOME` |
| windows | the window record and its callbacks (SPEC.md §11), damage (§11.90), clip (§11.3) and press tracking (§13.7) |
| drawing | `OSAPI_GFX_*`, `OSAPI_GFX_BLIT4`, and proportional type (`apps/os88type.inc`, SPEC.md §6.3) |
| UI | the in-window menu (SPEC.md §13.16), `os88ui.inc`'s controls (SPEC.md §13.8–§13.17), line editing (SPEC.md §83) and the About card (`OSAPI_ABOUT_SET`, SPEC.md §12.2) |
| services | the timer (SPEC.md §13.9), the clipboard (`OSAPI_CLIP_*`), speaker tones (`OSAPI_SND_TONE`), claims (SPEC.md §50.3) and app data (SPEC.md §19.9, `apps/os88data.inc`) |
| CPU detection | `OSAPI_CPU_INFO` (SPEC.md §60.1) tells 8086/286/386 apart but not a V20. `apps/redline/detect.inc` does, and is SHARED AS SOURCE (WEAVE-SPEC §1.2's rule) |

**Missing, and owned by a story:**

- an INI parser (S7);
- an x87 emulator (S9);
- a Win16-shaped dialog manager and Windows-styled controls (S6);
- the System font as an `.F88` face (S4).

---

## Sources

| source | commit / version | licence | what is taken |
|---|---|---|---|
| Wine (`github.com/wine-mirror/wine`) | `0738d528db522f52d9d75f2aec712688dd31d1d2` | LGPL-2.1+ | the `krnl386`, `user`, `gdi` and `shell` `.spec` ordinal tables (vendored); `fonts/system.sfd` (the System font); the logic of `dlls/krnl386.exe16/ne_segment.c` (relocations at lines 132–326, prolog patching at 510–574), `ne_module.c` (task start, 1062–1084), `task.c` (InitTask, MakeProcInstance), `dlls/user.exe16/dialog.c` (templates); `include/winnt.h` and `include/wine/winbase16.h` (structures and flags); `tools/winedump/ne.c`, to check `os88ne.py` against |
| winevdm / otvdm (`github.com/otya128/winevdm`) | `b88d7f8736d77baa5710eb0c35f81b489540c1a0` | GPL-2 (any version); Wine-derived files LGPL-2.1+ | the `sound` and `win87em` modules and their `.spec`s; the behaviour of the Win16 USER and GDI on real programs; issues #181 (WinMine's width) and #60 (CALC) |
| Microsoft KB Q11979, Q78326, Q79194, Q72540; *Windows Internals* (Pietrek); *Undocumented Windows* (Schulman) | — | references | prologs, real mode vs protected mode, `__AHINCR`, the CALC bug |
| the sample binaries and the reference machines | archive.org `disk-1_202602` (MS-DOS 6.22), `microsoft-windows-3.0-3.5-720-k`, `windows-3.1-disks` (3.10.103), `w-3_20231227`; minesweepergame.com WinMine 2.6–2.9. Every file SHA-256-pinned in `.claude/skills/win16-reference/scripts/win16ref.py` | Microsoft's and Duff Software's; never committed, kept in the git-ignored `win16ref/` | census, testing, and the real Windows 3.0 and 3.1 to compare against |

---

## Verification discipline

**During waves:**

- assemble with `cpu 8086` and `-w+error`: the layer's own code is 8086 even
  where the programs it runs are not;
- run `make`, which runs the fast tier;
- run `tools/kernsize.py` on both kernels (the target is no change);
- boot once to look;
- name the rows each story adds in `tests/suite.py`: `win16ne` (fast, host),
  `win16load`, `win16pump`, `win16gdi`, `win16dlg`, `win16ini`, `win16mines`,
  `win16fpu`, `win16calc`, `win16cpu`, `win16multi`.

**Rows that need the samples** skip with a note when `WIN3X` is not set, and
say so. They never pass silently.

**After the last wave:**

- one deduplicated `os88test.py soak -k …` union, then `make test-full`;
- re-run failures only.

**Emulators:**

- QEMU is the instrument for behaviour.
- MartyPC is the instrument for the XT's cost and for the 8088 refusal, and
  its V20 setting for the 186 tier.
- 86Box `vm/286` is where a human looks at the 286 tier.
