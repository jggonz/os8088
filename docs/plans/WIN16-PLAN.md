# WIN16 — running Windows 3.x programs on os8088

**STATUS: BEING BUILT. S0–S4 have shipped; the owner's decisions were taken
2026-10-28, and D3, D6 and D8 were revisited 2026-10-29 for the end goal
below.** Every size is an ESTIMATE unless the line says MEASURED. The census
figures below are MEASURED with a scratch script over the sample binaries; S1
turns that script into the shipped tool. The binding contract is SPEC.md
§124, pinned story by story as this plan's checkboxes are ticked. The diary is
[HOW-WIN16-WAS-BUILT.md](../HOW-WIN16-WAS-BUILT.md).

The ask: double-click the original Windows **Minesweeper** or **Calculator**
in a Disk window and play it in an os8088 window that looks the way it did on
Windows, through a compatibility layer, "a Wine for os8088", that lives on
disk until a Windows program is opened.

**The end goal (the owner, 2026-10-29) is any Win16 program**, CD-ROM
programs and their installers included. Minesweeper and Calculator remain the
first two programs, and S0–S13 still deliver them. From S5 on, no choice may
close the road to the rest: a mechanism shaped to those two programs alone
counts as a defect, and *Beyond S13* below lists what stands between the
layer and the end goal.

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
| 5 | Resources: menus, accelerators, strings, bitmaps, icons, dialog templates | nobody; the resource table is kept and each resource is read from the file when it is loaded (`OSAPI_FILE_READ_AT`, SPEC.md §18.4.4), as Windows read it | medium |
| 6 | Dialogs and controls: BUTTON, STATIC, EDIT, LISTBOX, `MessageBox` | **`apps/os88ui.inc`** and `os88line.inc` (SPEC.md §13.8–§13.17, §83), restyled to Windows 3.0's look | large |
| 7 | Menus | nobody: WIN16's own menu manager after Wine's `menu.c` (D8, revised). `OS88UI_MENU` (SPEC.md §13.16) was judged and set aside: no nested popups, a one-byte action, os8088's 8x8 cell | medium |
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

- [x] **S1: Census.** *As a developer I can point the build at my Windows
  files and see exactly what each one needs before a line of the layer
  exists.* SHIPPED 2026-10-28.
  - `tools/os88ne.py` (host Python, standard library only; SPEC.md 124.4.1)
    reports:
    - the NE header, the segment table and the loader's refusal flags;
    - every import as `MODULE.ordinal` plus its name, from the vendored
      `.spec` files;
    - the resources, with menus item by item (CHECKED, GRAYED), dialog
      control classes, bitmaps, strings, accelerators and FONT metrics;
    - the OS-fixup counts by kind;
    - every `int` with its AH where a constant load sets it, and port I/O;
    - an **instruction census per code segment by recursive descent** from
      CS:IP, the entry table, internal relocations and switch tables, then
      the prologue heuristic over what is still dark. It reports coverage
      and the 186-looking bytes left in the dark. Its verdict is 8086, 186,
      286 or 386, plus x87 and the x87 subset by mnemonic and operand.
      A 286 instruction behind the standard `push sp` probe is reported as
      guarded and does not count.
  - `make win16census WIN3X=<dir>` runs it over every `.EXE`, `.DLL`, `.DRV`
    and `.FON` in a directory; `--json` is what the gates read.
  - The ordinal tables are vendored verbatim from winevdm in
    `tools/win16spec/`, not `apps/win16/spec/`. They are host data the census
    and the build read, and `apps/` holds only packages that ship
    (`t_retired`, `t_livefull`).
  - **Fixtures, two kinds** (D10), with every one run on the Windows 3.0
    reference machine first (`win16ref/ref/w30-fix-*.png`):
    - `tests/win16/tiny16.asm`, a minimal NE built by `nasm -f bin`. It calls
      InitTask, WaitEvent, InitApp and MessageBox, and it is a census trap in
      five `-D` variants: 186-looking data behind a jump, a switch table, a
      procedure only its prologue finds, x87 with OS fixups, and Open
      Watcom's guarded 286 probe.
    - seven Win16 programs in C, `tests/win16/*.c`, built by Open Watcom with
      `make win16fixtures` into `build/win16/`:
      - `MSGBOX`: a message box;
      - `WINDOW`: class, window, paint, timer, mouse, keys;
      - `MENU`: popups, CHECKED, GRAYED, an accelerator;
      - `DIALOG`: STATIC, EDIT, group box, radio, check box, LISTBOX,
        buttons;
      - `PROFILE`: a private .INI plus WIN.INI;
      - `BITMAP`: 4bpp and 1bpp, BitBlt and StretchBlt;
      - `FPU`: doubles.
    - All eight are 8086 code (FPU.EXE + x87), and all eight run on Windows
      3.0 in real mode.
  - *Verified:*
    - the `win16ne` fast row (`tests/unit/t_win16ne.py`), 52 checks;
    - it fails when the prologue heuristic, the probe guard or the descent
      itself is taken out (three sensitivity runs);
    - over the samples it reproduces this plan's table: WinMine 2.9 and both
      3.1 programs are 186, and CALC 3.00 is 8086 + x87;
    - VGASYS.FON reads as D4's System 16 px.
  - **Found on the way, carried into later stories:**
    - the MS C runtime calls `int 21h` itself (S2);
    - the x87 subset by name (D5);
    - Windows' check glyph, grey text and separator on the menu fixture
      (S5, `w30-fix-menu-open.png`).

- [x] **S2: Load or refuse.** *As a user, when I double-click a Windows
  program I get either the program, or one sentence saying why not, and never
  a crash.* SHIPPED 2026-10-28, two waves (the loader, then the census); what
  was NOT seen on the glass is listed at the end.
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
  - **DOS calls:** the MS C start-up and exit call `int 21h` directly, not
    through `DOS3Call` (S1's census: CALC 3.00 uses AH = 25h, 30h, 35h, 3Eh,
    40h, 44h, 4Ch; WinMine 2.9 uses 25h, 30h, 35h, 4Ch). WIN16 hooks
    `int 21h` for the program's lifetime and answers these, with 4Ch ending
    the program as the close box does (SPEC.md 124.3).
  - *Verify:*
    - the fixture reaches its WinMain;
    - a doctored fixture with an unknown import refuses with its name;
    - `kern_mega` refuses at entry in WIN16's own words;
    - `kern_small`'s disks do not carry it (D6).
  - **What shipped:**
    - `apps/win16/` (`make win16`, 8,595 bytes): the loader
      (`w16load.inc`), the coroutine and the gate (`w16core.inc`, SPEC.md
      124.2.1), `int 21h` (`w16i21.inc`), the exports S2 needs
      (`w16api.inc`: InitTask, WaitEvent, GetVersion, GetWinFlags,
      IsDBCSLeadByte, GetModuleFileName, InitApp, MessageBox as os88ui's
      alert until S6, GetKBCodePage) and the 8088 census (`w16cens.inc`,
      with `w16optab.inc` written by `os88ne.py --optab`).
    - The handover is `apps/dos/dosw16.inc`, assembled only under
      `-DDOS_WIN16`, which `make win16test`'s DOS.O88 alone carries until
      S12 decides where WIN16 ships. The shipped DOS.O88 is byte-identical
      to the one before. The kernel's built-in stem row `WIN16` took a row
      of `times` padding, so `kernsize` reads *unchanged*.
    - WIN16 is a `local` package in `apps/RETIRED.txt`, and its region is
      pinned in `tests/movable.txt` (the program holds its segment).
  - **Seen on the glass** (QEMU, `make test TESTAPPS=build/win16test.img`):
    - TINY16 and MSGBOX reach their MessageBox from a desktop double-click,
      OK ends them, and WIN16's window closes;
    - TINYUNK refuses *Needs USER.287 / which WIN16 does not provide yet.*,
      and WINDOW.EXE *Needs USER.10 and 31 more*;
    - with the census forced on (`-DW16_CENSUS_ALWAYS`): TINY186 refuses
      *Needs a V20, 80186 or better.*, while TINY16 and MSGBOX run; with
      the guard taken out, MSGBOX refuses *Needs an 80286 or better.*,
      which is the negative control.
  - **Not seen yet:** kern_mega's refusal on a kern_mega boot; the
    self-loading, library and out-of-memory refusals; the census on a real
    8088 or MartyPC, and its time there. S13's final pass owns all of these.

- [x] **S3: The message pump.** *As a user I see the program's window, I can
  move it, click in it, and close it with the close box.* SHIPPED
  2026-10-29; SPEC.md 124.2.2 and 124.8 are the contract as built.
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
  - **What shipped:**
    - **Calls into the program are continuations on its stack** (SPEC.md
      124.2.2, `w16core.inc`): `w16_cbopen` pushes a record and any extra
      bytes (WM_CREATE's CREATESTRUCT), `w16_cbcall` the arguments and a far
      return to `w16_cbret`, and the gate enters the callee with AX = DS =
      DGROUP. The callee's return is an import of its own, `W16_CBROW`,
      whose handler runs the continuation. The host stack never holds a call
      into the program, so a window procedure that waits waits like any
      import.
    - **USER** (`w16user.inc`, SPEC.md 124.8): 8 classes, 8 windows, one
      top-level window that is WIN16's own os8088 window, a 32-message queue
      taken in Windows' order, 4 timers on one os8088 one-shot, the input
      table, `W_PAINT` as `OSAPI_WM_OWNBG` resuming a program waiting in
      `GetMessage` inside the paint, and the close negotiator posting
      `WM_CLOSE`. The rows: USER 6, 10, 12, 13, 15, 18, 19, 33, 39, 40, 41,
      42, 53, 56, 57, 66, 68, 107-111, 113, 114, 124, 125, 127, 173, 174 and
      232.
    - **GDI's S3 subset** (`w16gdi.inc`, SPEC.md 124.8.1): one DC, the stock
      objects, solid brushes, `MoveTo`/`LineTo`, `Rectangle` and `TextOut`,
      with WIN16's own clip to `rcPaint` inside the kernel's.
    - **A second fixture, `tests/win16/pump.c` (PUMP.EXE)**, for what
      WINDOW.EXE does not reach: the `WS_MINIMIZE` start then `MoveWindow`,
      `MoveWindow`/`SetWindowPos` on keys, a `PeekMessage` idle loop that
      counts its empty turns, `SendMessage` (answers 7), `PostMessage`,
      capture and `GetTickCount`. It was run on Windows 3.0 FIRST, and
      matching it changed two things: the caption costs 19 rows, not 20,
      and a window created minimized keeps its created size through
      `MoveWindow` until `ShowWindow` restores it.
    - `make win16` is 14,432 bytes; the kernel is unchanged.
  - **Seen on the glass** (QEMU, `make test TESTAPPS=build/win16test.img`):
    - WINDOW.EXE: its window is titled *WIN16 window* and sized to the
      client asked for; the tick counter runs at 2 a second; a click reads
      *clicks 1 at 170,62* in client coordinates; a key reads *key a*
      (`WM_KEYDOWN`, then `TranslateMessage`'s `WM_CHAR`); covering it with
      the Disk window and raising it again repaints everything; the close
      box ends it (`WM_CLOSE`, `DestroyWindow`, `WM_DESTROY`,
      `PostQuitMessage`, AH=4Ch).
    - PUMP.EXE against Windows 3.0's own screenshots: *size 192 x 73*
      after the minimized start, *sent 7*, *posted 2* after two `p`s, 312 ×
      153 after `s`, *capture 1* while the button is held and 0 after, the
      idle count climbing while the desktop stays live. Covered by the Task
      Manager while it idles in `PeekMessage`, it repaints in full.
    - The claim map (`tools/heapmap.py`) after each close is the one taken
      before the launch, byte for byte, twice.
    - On CGA (`VIDEO=cga`, 640 × 200): WINDOW.EXE runs, text is legible
      and the grey brush dithers; the window is clamped to the screen, so
      the bottom of the picture is cut off.
    - MSGBOX.EXE still reaches its MessageBox, and OK ends it.
  - **Not as the story first said, and why:**
    - **There is no Task Manager kill.** os8088's Task Manager is a viewer
      (SPEC.md 28: nothing there can kill a task), so the close box is the
      only end a package has, and it is negotiated: a program that refuses
      `WM_CLOSE` keeps its window. Whether WIN16 should offer an "End Task"
      of its own (a second close click, say) is OPEN, for the owner.
    - `WM_COMMAND` has no source until menus (S5, D8) and dialogs (S6);
      `DispatchMessage` delivers it like any other message.
    - **The width differs from Windows by up to 7 pixels.** os8088 keeps a
      window's width on its 8-pixel grid: PUMP's `m` (260 wide) has a 256
      client here and 252 on Windows 3.0.
    - On a 1bpp adapter a colour is the adapter's rounding of the os8088
      colour: WINDOW's red box is white on CGA, where Windows' mono driver
      draws it black. With D4's System font, that belongs to S4.
  - **Not seen yet:** WinMine's own start (it needs S4's GDI and S5's
    menus to run at all; PUMP reproduces the sequence and the Windows 3.0
    answer); "repaints only its damage" was checked by construction and by
    eye, not by counting primitive calls; Hercules, EGA and an XT. S13's
    final pass owns the measurements.

- [x] **S4: GDI and the Windows look.** *As a user the program's window looks
  the way it did on Windows on that display, in colour on VGA/EGA and
  legibly on CGA and Hercules.*
  - **What shipped:**
    - **DCs:** `BeginPaint`/`EndPaint`, `GetDC`/`ReleaseDC`; each open
      resets the ROP, the text colour and the pen and brush.
    - **Drawing:** stock and solid pens and brushes (`CreatePen`,
      `CreateSolidBrush`, `SelectObject`, `DeleteObject`, `COLOR_x + 1`
      brushes), `SetROP2` (R2_COPYPEN, R2_WHITE, R2_BLACK, R2_NOT and
      R2_NOTXORPEN, the five the targets use), `Rectangle` (with Windows'
      null-pen size quirk), `RoundRect`, `FillRect`, `MoveTo`/`LineTo`,
      `SetPixel`, `SetRect`, `PtInRect`.
    - **Text:** `TextOut`, `SetTextColor`, `GetTextExtent`,
      `GetTextMetrics`, `lstrlen`, in the **System font for the display**
      (D4): 16 px on VGA, 12 on EGA and Hercules, 8 on CGA. The faces are
      committed art, `apps/win16/fonts/sys16/12/08.txt`, drafted by
      `tools/w16font.py` from Wine's System font and fitted by hand, and
      ride as three lz4 PARTS of `WIN16.O88`, fetched at launch (SPEC.md
      124.7.1).
    - **Bitmaps:** `SetDIBitsToDevice`, 1 and 4 bpp, info and core
      headers, into `OSAPI_GFX_BLIT4`.
    - **Questions:** `GetDeviceCaps`, `GetSystemMetrics` and `GetSysColor`
      answer what Windows 3.0 answered: measured on the reference machine
      for VGA, and read out of the Windows 3.0 EGA, CGA and Hercules
      drivers' GDIINFO for the others (SPEC.md 124.8.2).
    - **Mono:** a colour is reduced by luminance before the kernel sees
      it: black, the desktop's grey dither, or white; text is lit at 160.
    - `GDICARD.EXE` (`tests/win16/gdicard.c`), the test card, joins the
      fixtures. `win16ne` checks the font art and the parts against the
      gate's own offsets, with a negative control.
    - `make win16` is 18,846 bytes of image and 20,962 on disk with the
      three faces; the kernel is unchanged (`kernsize`: *unchanged*).
  - **Seen on the glass** (QEMU):
    - VGA: GDICARD prints the same numbers as on Windows 3.0, every one:
      the text metrics, the extent of *Win Mine 2.01* (94 x 16), the caps,
      all 36 system metrics and all 19 system colours. Aligned on the dark
      pixels, the geometry of every shape is identical to Windows'
      screenshot.
    - EGA (`VIDEO=ega`): the 12 px face, caption 16, menu 14.
    - CGA (`VIDEO=cga`): the 8 px face is legible; the red text is black,
      as Windows' mono driver draws it; the greys dither.
    - WINDOW.EXE and MSGBOX.EXE still work; PUMP.EXE still matches
      Windows 3.0's screenshot exactly. The claim map after WINDOW and
      after PUMP is the one taken before, byte for byte.
  - **Not as the story first said, and why:**
    - **No `.F88` face and no `os88type.inc`.** The text goes through a
      composer of WIN16's own: the run is composed into one band and put
      down in one `OSAPI_GFX_BLIT1` (`BLIT1_PEN` for the colour), which is
      one primitive call a `TextOut` whatever its length.
    - **`OSAPI_GFX_SPANS` refuses under an armed clip region** (SPEC.md
      5.10), and a DC always has one. `RoundRect` and the rest coalesce
      their rows into rectangles instead, so a straight edge is one fill.
    - **The palette is os8088's, not Windows'.** Dark grey is 555555 here
      and 808080 there, and light red FF5555 against FF0000.
      `GetSysColor` still answers Windows' values.
    - **No Windows EGA or CGA screenshots.** The reference machine was
      not re-run through `SETUP` for those displays; their numbers are the
      drivers' own GDIINFO, read from the files.
    - The glyphs are Wine's shapes, not Microsoft's: `4`, `7` and `1`
      differ by a pixel or two.
  - **Not seen yet:** Hercules (QEMU has none); a call count on a full
    Minesweeper repaint (WinMine needs S5's menus to start); the `.FON`
    override; `BitBlt` and memory DCs, which WinMine 3.1 uses and 2.9 does
    not. S13's final pass owns the measurements.

- [x] **S5: Memory, resources and menus.** *As a user the program's menu bar
  sits in its window as it did on Windows, its shortcuts work, and its check
  marks show.* Menus are resources, resources need memory, and both targets
  need memory before their first window, so the story carries all three.
  - **Unprovided imports no longer refuse the load** (Beyond S13, R1). Each
    is bound to a stub that, when CALLED, ends the program with the sentence
    S2 printed at load: *Needs USER.287 / which WIN16 does not provide yet.*
    Real programs import far more than they call. The load-time refusal stays
    for what cannot load at all.
  - **The global heap:** `GlobalAlloc`/`ReAlloc`/`Free`/`Lock`/`Unlock`/
    `Size`/`Compact`/`Handle`, `LockSegment`/`UnlockSegment`, sub-allocated
    in arena claims (D3). A `GMEM_FIXED` handle IS its segment, as on real-
    mode Windows; a moveable one is a handle-table entry, never a segment, so
    a protected-mode arm can put a selector behind the same calls (R3).
  - **The local heap:** `LocalInit` over DGROUP's heap at `InitTask`, then
    `LocalAlloc`/`ReAlloc`/`Free`/`Lock`/`Unlock`/`Size`.
  - **Resources, read on demand:** the NE resource table is kept;
    `FindResource`/`LoadResource`/`LockResource`/`FreeResource`/
    `SizeofResource` read a resource from the program's file with
    `OSAPI_FILE_READ_AT` into a global block, shared until freed. No program
    is limited by what fits at load (R2).
  - `LoadString`, `LoadAccelerators`/`TranslateAccelerator`, and
    `LoadIcon`/`LoadCursor`/`SetCursor`/`ShowCursor` (the arrow; the icon is
    S12's).
  - **Menus, WIN16's own (D8, revised):** `LoadMenu`/`LoadMenuIndirect`,
    `CreateMenu`/`CreatePopupMenu`/`AppendMenu`/`InsertMenu`/`DeleteMenu`/
    `ChangeMenu`, `SetMenu`/`GetMenu`/`GetSubMenu`/`DrawMenuBar`,
    `CheckMenuItem`/`EnableMenuItem`/`GetMenuState`, and the class menu.
    Nested popups and 16-bit IDs. Drawn as Windows 3.0 drew them, in the
    display's System font, inside the window. `WM_INITMENU` and
    `WM_INITMENUPOPUP` before a popup opens, `WM_COMMAND` posted on a pick.
    Mouse (press-drag-release and click-click) and keyboard (Alt or F10,
    mnemonics, arrows, Enter, Esc). `GetSystemMenu` answers a menu the frame
    never shows: the os8088 frame is the frame.
  - **The profile reads answer as an empty `WIN.INI` does** (their
    defaults) until S7 gives them a file, so a program reaches its window.
  - **End Task (the owner, 2026-10-29).** A second click on the close box,
    while the program has not yet closed for the first, asks *It has not
    closed. End it?* and Proceed ends it, its memory and its window. A program
    that never yields cannot be reached; os8088 has nothing that can.
  - *Verify:* MENU.EXE (a nested popup, checks, greys, an accelerator,
    `LoadString`, the global and local heaps) against Windows 3.0's
    screenshot; **WinMine 2.9 reaches its board**, its Game menu checks the
    level and Marks, and F2 starts a new game; a doctored import ends the
    program with its name when called; End Task on a program that refuses
    `WM_CLOSE`; the claim map back to the pre-launch one after each. CALC's
    View menu moves to S10, CALC needing S6's dialog window first.
  - **Shipped** (two commits on PR #320):
    - Part 1: the global heap in 16 KB arenas with moveable handles, the
      local heap in DGROUP, resources read on demand, `LoadString`, the
      profile defaults, and the stub that ends a program at a missing import.
    - Part 2: `apps/win16/w16menu.inc`, WIN16's menu manager after Wine's
      `menu.c` - the USER heap, the template parser, 28 USER calls, the
      layout and drawing, the loop and the accelerators. The loop is the
      import waiting inside `DefWindowProc`, moved by os8088's mouse and key
      callbacks, which SEND `WM_INITMENU` and `WM_INITMENUPOPUP` through a
      new host send, `w16_resumecb` (SPEC.md 124.2.2). End Task in
      `w16_close` (SPEC.md 124.12).
    - `make win16` is 32,201 bytes of image and 31,714 on disk with the
      three faces; the kernel is unchanged (`kernsize`: *unchanged* on
      kern_big and kern_small).
  - **Seen on the glass** (QEMU):
    - VGA: MENU.EXE's bar, its File and Colour popups and the nested More
      popup laid over Windows 3.0's screenshots: the same pixels, the
      glyphs aside (D4). A pick closes the popups and puts the bank back
      clean. F2 and Ctrl+B count `WM_INITMENU` and `WM_INITMENUPOPUP` as
      Windows did. F10, the arrows, Enter, Alt+F and Esc work.
    - **WinMine 2.9 reaches its board**, with its Game and Help menus, the
      level's check and the accelerators. Intermediate resizes it to 16 x 16
      with 40 mines, a cell click reveals and starts the clock, F2 starts a
      new game, and the close box closes it.
    - TINYUNK runs its `MessageBox` and is then ended at USER.287 with
      *Needs USER.287 / which WIN16 does not provide yet.*
    - End Task: MENU.EXE with F5 refuses `WM_CLOSE`, the second click asks,
      and Proceed ends it and closes its window.
    - The claim map after MENU.EXE's exit is the one taken before its
      launch, line for line.
    - CGA (`VIDEO=cga`) and EGA (`VIDEO=ega`): the bar, the popups, the
      nested popup and a pick, each in its own face. On CGA the shadow is
      drawn black; a dithered one-pixel line read as dots.
  - **Not seen:** Hercules (QEMU has none); WinMine's Marks toggle, which
    was not clicked; its Custom field, which needs S6's dialog; a call count
    on a full Minesweeper repaint (S13's final pass).

- [x] **S6: Dialogs and controls.** *As a user the program's dialog boxes
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
  - **Changed by what was found:** CALC 3.0 imports `WIN87EM.__FPMATH`, so
    it refuses before its first window (*Needs WIN87EM.__FPMATH*) and its
    Scientific panel, a `WS_CHILD` dialog, is checked in S10 behind S9's
    emulator. `SS_ICON` and the message box's icons move to S12 with the
    rest of the icons. SPEC.md 124.13 records what shipped where it differs
    from what was pinned: the modal loop's turn cap, a dialog that is the
    program's first window taking WIN16's own, the message box's measured
    layout and 255-character limit, and the white button face on 1bpp.
  - **Shipped** (one commit on PR #320):
    - `apps/win16/w16dlg.inc`: the dialog manager after Wine's
      `dialog16.c`/`dialog.c` and the four controls after `button.c`,
      `static.c`, `edit.c` and `listbox.c`, as part 3 of `WIN16.O88`
      (fetched on the first dialog, control or message box, dropped after
      the last). WIN16-built `MessageBox` replaces S2's os8088 alert.
    - The window tree grew a creation order (`W16W_SEQ`), hide, show,
      front and free-tree helpers, a mouse that climbs from a disabled
      control to its parent and brings a modal dialog to the front, and
      paint picks that see a second os8088 window.
    - `tests/win16/modeless.c`: `CreateDialog` with an `IsDialogMessage`
      loop, `SetDlgItemInt(-42)` and `GetDlgItemInt` read back.
    - `make win16`: a 38,243-byte image (32,201 at S5) and a 12,761-byte
      dialog part, 46,787 bytes on disk with the faces, all parts lz4. The
      kernel is unchanged.
  - **Seen on the glass** (QEMU):
    - VGA: DIALOG.EXE against Windows 3.0's screenshot: the radio buttons,
      the check box, the list, Tab order, an EDIT selected whole on Tab,
      typing, Enter for OK and Esc for Cancel. Its values reach a
      `MessageBox` (*OK: name win, large, sound on, list 2*), Enter closes
      that, and the program ends cleanly.
    - **WinMine 2.9**: About, titled from its owner, then Preferences,
      which matches Windows 3.0's but for the bomb (`SS_ICON`, S12).
      Expert then OK gives 24 x 24 with 99 mines; a custom 12 high, 20
      wide and 30 mines, typed into its three EDITs (WinMine clears the
      skill radio itself on `EN_CHANGE`), gives a 20 x 12 board with the
      counter at 030.
    - MODELESS.EXE: *OK: "17" reads 17 (a number)*.
    - CGA (`VIDEO=cga`) and EGA (`VIDEO=ega`): DIALOG.EXE and its message
      box, each on its own unit grid. On CGA the button face is white:
      the grey dithered the captions into noise.
  - **Not seen:** Hercules (QEMU has none); CALC's Scientific panel (S10);
    a call count on a dialog's first paint (S13's final pass).

- [x] **S7: Profiles.** *As a user my best times and my chosen difficulty are
  still there tomorrow.*
  - `GetProfileInt`/`GetProfileString`/`WriteProfileString` and the
    `PrivateProfile` forms.
  - A minimal INI reader and writer over `SYSTEM\APPDATA\WIN16\` (SPEC.md
    §19.9): `WIN.INI` and each program's own `.INI`.
  - Writes are made on the UI task, so the file rules hold.
  - *Verify:* set Expert, record a time, close, reopen; the INI on the host
    reads back with `mtools`.
  - **Amended at S7** (SPEC.md §124.14, every row measured on Windows 3.0):
    `GetProfileInt` is unsigned (`-42` reads 0); a quoted value is answered
    without its quotes; a `NULL` default leaves the buffer and answers 0; a
    written value is trimmed. Writes are kept in memory and each changed
    file is one write when the program ends or flushes, not one a key.
    WinMine 2.9 has no best times, so "record a time" waits for a program
    that keeps them.
  - **Shipped** (one commit on PR #320):
    - `apps/win16/w16prof.inc`: the profile calls after Wine's `profile.c`.
      Up to four files held, each in its own claim, least recently used
      out first, at most 32,000 bytes each.
    - `apps/os88data.inc`'s `OD_TVOL` mode, so WIN16 reaches
      `SYSTEM\APPDATA\WIN16\` on the volume it was launched from. Every
      other package that includes it assembles byte-identical.
    - What WinMine called on its way out: `WinHelp` (USER.171, `TRUE` to
      `HELP_QUIT`), and `wsprintf`/`wvsprintf` (USER.420/421,
      `apps/win16/w16fmt.inc`).
    - `tests/win16/inifile.c`: the oracle fixture, run on Windows 3.0 and on
      WIN16; `win16test.img` carries the `WIN16` folder.
    - `make win16`: a 41,833-byte image (38,243 at S6), 49,863 bytes on
      disk. The kernel is unchanged.
  - **Seen on the glass** (QEMU, VGA):
    - INIFILE.EXE: its message box matches Windows 3.0's, and the
      `W16INI.INI` it leaves is byte-identical to the reference's (`cmp`).
    - PROFILE.EXE, run twice: run 1, then run 2; `W16TEST.INI` read back on
      the host with `mtype`.
    - **WinMine 2.9**: Expert, then Exit, writes `WINMINE.INI`'s ten keys
      (`Difficulty=2`, 24 x 24, 99 mines, the sound/mark/menu/tick flags,
      the position); relaunched, it opens as Expert. Intermediate, then
      Exit, then relaunch: 16 x 16 with 40 mines.
    - A disk without the folder: PROFILE.EXE says run 1 both times, with
      no error, and nothing is written.
  - **Not seen:** the fifth held file sending one out (no program here
    opens five); a profile over 32,000 bytes.

- [x] **S8: Minesweeper.** *As a user I can play the original Windows
  Minesweeper on os8088, every level, with the timer, the mine counter, the
  face, marks, colour and best times, on every adapter my machine has.*
  - SOUND.DRV's voice API (`OpenSound`, `SetVoiceNote`, `SetVoiceAccent`,
    `StartSound`, `StopSound`, `CloseSound`) plays through `OSAPI_SND_TONE`.
  - WinHelp refuses with its reason, greyed (SPEC.md §47). (S7 already
    answers `TRUE` to `HELP_QUIT`, which WinMine sends as it exits.)
  - The CPU it needs is stated on the glass when it is not met.
  - *Verify:* QEMU and MartyPC-with-V20 rows (`win16mines`) play a scripted
    game to a win and a loss; the four adapters are looked at; the XT's
    frame cost is taken in the final pass.
  - **Amended at S8** (SPEC.md §124.15): the target is Windows 3.1's
    Minesweeper (3.0a, 80186 code) beside WinMine 2.9. SOUND's behaviour
    was read off Windows 3.0's `SOUND.DRV` itself, including its bug: dotted
    notes are ignored. WinHelp is a message box with its reason rather than
    a greyed item - the item is the program's, and WIN16 cannot grey it.
  - **Shipped** (one commit on PR #320):
    - `apps/win16/w16snd.inc`: the voice queue, 32 notes, played from WIN16's
      tick with the error carried in 1/2000 of a tick.
    - `apps/win16/w16bmp.inc`: memory DCs (four), bitmaps (eight, each a
      pinned claim in os8088's sixteen colours), `CreateDIBitmap`,
      `LoadBitmap`, `GetObject`, `BitBlt` and `StretchBlt` (`SRCCOPY`,
      `BLACKNESS`, `WHITENESS`, `PATCOPY`). An even source column is one
      `OSAPI_GFX_BLIT4` straight from the claim.
    - The calls around them (§124.15.3), `DOS3Call` and `__WINFLAGS` bound
      at load rather than through a gate, and `FatalExit`/`FatalAppExit`.
    - `tests/win16mines.py`, rows `win16mines` (QEMU) and `win16minesxt`
      (MartyPC: the 8088's refusal, a V20's game and its cost), behind a new
      `win16samples` capability: they run only where the samples are.
    - `make win16`: a 45,701-byte image (41,833 at S7) and 13,064 bytes of
      bss, 6,259 bytes under the 64KB segment. The kernel is unchanged.
  - **Seen on the glass** (QEMU):
    - W31MINE on VGA matches the Windows 3.1 screenshot, colours aside. A
      game won shows the sunglasses and the name dialog, and Best Times
      then reads *Beginner: 128 seconds Os8088*. A game lost shows the
      frown and the mines. Flags and `?` marks, Expert (30 x 16, 99 mines,
      kept across a relaunch), Custom Field, Color off (matching Windows
      3.1's own mono shot, `win16ref/ref/w31-mine-mono.png`), the About box
      and the WinHelp refusal.
    - Sound, captured with `make test-snd`: the win tune is 247, 277, 311,
      330, 370, 415, 466 and 494 Hz exactly; the loss alternates 494 and
      247 Hz; the clock ticks at 2,488 Hz. WinMine 2.9 ticks and plays too.
    - EGA (640 x 350): colour, and it fits. CGA (640 x 200): mono bitmaps,
      and the bottom rows are off the screen (§124.15.4).
    - BITMAP.EXE: the copy, the 2x `StretchBlt` and the mono twin are
      pixel-for-pixel where Windows 3.0 puts them.
  - **Not seen yet, owed to S13's final pass:** the two rows have been
    written and registered but not run (soak runs once, at the end);
    Hercules, which QEMU does not have; the 8088's refusal on MartyPC (it
    is §124.4's census, gated host-side by `win16ne`); and the V20's frame
    cost.

- [x] **S9: Floating point.** *As a user Calculator gets the right answer
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
  - **Amended at S9** (SPEC.md §124.5):
    - D5 taken and widened: the 8087 less its transcendentals and BCD, not
      only the census's subset (the D5 row says why and what is lacking).
    - The reference became Windows 3.0's own `WIN87EM.DLL` 6.00.11,
      disassembled - its `__FPMATH` table, its exception codes, its `int 2`
      design - rather than winevdm's, which answers the same calls.
    - The oracle is EXACT arithmetic (Python's `fractions`), not the host's
      doubles: it is the 80-bit register, three precisions and four
      rounding modes that need checking, and a double holds none of them.
    - The XT leg runs on a V20 5150 with no coprocessor, because the stock
      8088's in-guest census refuses the fixtures (written into S11).
    - KB Q72540 moves to S10: Calculator now gets past `__FPMATH`, the C
      runtime's floating-point start-up and `RegisterClass` into its message
      loop, but its window - a dialog-class window, which S10 is - is not
      shown yet, so there is no display to read 0.00 from. S10's
      `win16calc` row reads it.
  - **Shipped** (one commit on PR #320): see the S9 entry of
    docs/HOW-WIN16-WAS-BUILT.md for the account.
    - `apps/win16/w16fp.inc`, part W16P_FPU (7,860 bytes, 6.3 KB packed):
      the emulator, self-contained, run on the program's own stack.
    - `apps/win16/w16x87.inc`: the fixups patched per machine, the vectors
      swapped with int 21h's, `__FPMATH` with a coprocessor, the `int 2` /
      `int 3Dh` exception path, and the x87 row that ends a program on an
      instruction the emulator lacks.
    - The loader: OS fixups counted in pass 0, the mode chosen, the part
      fetched, sites patched in pass 1; `__FPMATH` bound straight to its
      far routine; a by-name import table (`__FPMATH`, CALC 3.00's way).
    - `cbWndExtra` up to 32 (Windows' `DLGWINDOWEXTRA` is 30, CALC's).
    - `tools/os88ne.py` names what a program reaches that the emulator
      lacks; `FPX.EXE`, an exception fixture; `-DW16_X87_EMU`, the test
      knob that runs the emulator on QEMU.
    - Rows: `w16fp` (fast, the oracle), `win16fpu` (QEMU) and `win16fpuxt`
      (MartyPC's V20).
    - `make win16`: a 47,055-byte image (45,701 at S8) and 13,950 bytes of
      bss, 4,531 bytes under the 64KB segment; 60,684 bytes on disk. The
      kernel is unchanged.

- [ ] **S10: Calculator.** *As a user I can use the original Windows
  Calculator in Standard and Scientific modes, copy and paste through the
  os8088 clipboard, and use its Statistics Box.*
  - The clipboard through `OSAPI_CLIP_PUT`/`GET`
    (`OpenClipboard`, `GetClipboardData`, `IsClipboardFormatAvailable`).
  - The Scientific panel is a `WS_CHILD` dialog (S6), first reached here,
    after S9 lets CALC past `WIN87EM.__FPMATH`.
  - *Verify:* the `win16calc` row keys a calculation in each mode and reads
    the display, then pastes a number in from Note Pad.

- [ ] **S11: The CPU floor on the glass.** *As an XT owner, the Windows
  programs my CPU can run, run, and the rest tell me plainly what they need.*
  - On a stock 8088, the 8086-clean programs run: CALC, CLOCK and REVERSI
    from Windows 3.0.
  - WinMine says it needs a V20.
  - On a V20 everything runs.
  - The V20/186 test is `apps/redline/detect.inc`'s, shared as source.
  - **Found at S9, S11's to fix:** the IN-GUEST census (`w16cens.inc`,
    S2's) refuses the Open Watcom fixtures on a stock 8088 as *Needs an
    80386 or better.* where the host census (`os88ne.py`) says 8086, on the
    same bytes, patched or not. It reaches FPU.EXE's segment 1 offset
    0x13 - the imm8 of `sub sp, 60h` at 0x11 - as an instruction start, and
    from there decodes `60` (`pusha`) and worse; 17 tiered records in all.
    Seen on QEMU with `-DW16_CENSUS_ALWAYS`, so it is the walk and not the
    CPU. No 8088 run had been taken before S9: S8's refusal of W31MINE on
    an 8088 is unseen too. S9's no-coprocessor leg runs on the V20 5150
    meanwhile, where the census does not run.
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

## Beyond S13: any Win16 program

The owner's end goal is any Win16 program, CD-ROM programs and their
installers included. This section is the road from the S13 layer to it. It
is NOT authorised and NOT estimated; each row becomes stories when the owner
says so. What S0–S13 must do meanwhile is not block it, and four properties
are kept for that:

- **The loader reads a file it can seek.** `OSAPI_FILE_READ_AT` serves S5's
  resources, and the same call loads a segment at a time (R2).
- **Imports go through a module table and stubs** (SPEC.md 124.2.1), so a
  DLL's exports can join it (R4).
- **`int 21h` is WIN16's while the program runs** (SPEC.md 124.3), so DOS's
  file calls can be served there (R5).
- **Memory is handles, not segments** (D3, revised): only a `GMEM_FIXED`
  handle is its segment, which is what real-mode Windows did too, so a
  protected-mode backend can supply selectors behind the same calls (R3).

| # | what blocks | why | the way through |
|---|---|---|---|
| R1 | An import WIN16 does not provide refuses the load | Programs import far more than they call; an installer imports hundreds | **Taken in S5:** a stub that ends the program, naming the import, when it is called |
| R2 | Files over 64 KB | S2 reads the whole file into one claim | Load segment by segment with `OSAPI_FILE_READ_AT`; S5's resource reads are the first user |
| R3 | Protected-mode programs | Windows 3.1 has no real mode, so many 3.1-era programs set the protected-mode-only flag, or use selectors (`AllocSelector`, `__AHINCR` huge pointers) and DPMI `int 31h` | A `kern_mega` arm (KERN-MEGA-PLAN): the program's segments get selectors from the kernel's descriptor tables, which is winevdm's model. 286 or better only; the real-mode arm stays for the XT. **The largest fork, and the owner's to take** |
| R4 | DLLs: the program's own, and system ones | Installers ship DLLs, and programs import COMMDLG, LZEXPAND, VER, SHELL, MMSYSTEM, TOOLHELP and DDEML | NE libraries loaded into the module table (`LibMain`, `WEP`, shared data). WIN16-provided COMMDLG (os8088's Standard File dialog), LZEXPAND (SZDD, which `tools/w16font.py` already expands host-side), VER, SHELL, and MMSYSTEM over `SOUND.DRV` and `CDROM.DRV` (SPEC.md §119) |
| R5 | DOS breadth | Installers copy files, make directories and read CDs | `int 21h` file and directory calls from DOS.O88's subset (SPEC.md §96), shared as source; MSCDEX's `int 2Fh` over `CDROM.DRV` |
| R6 | Discardable segments | A large program expects its code to be discarded and reloaded under memory pressure | `int 3Fh` loader stubs for discardable code segments, read with `OSAPI_FILE_READ_AT` on first call (D3, revisited) |
| R7 | USER and GDI breadth | Child windows, MDI, COMBOBOX, SCROLLBAR, multi-line EDIT, `BitBlt` and memory DCs, more fonts, palettes | S4's GDI and S6's controls grow, each by the programs that need them; printing refuses with its reason |
| R8 | The installer's environment | `WIN.INI`/`SYSTEM.INI` edits, and DDE to Program Manager to make a group | S7's INI files; a Program Manager DDE server that makes os8088 desktop links |
| R9 | Programs that talk to each other | DDE, a DLL's shared data | One WIN16 instance hosting several tasks, instead of S12's one instance per program |

---

## Decisions

Taken by the owner on 2026-10-28 unless marked otherwise.

| # | decision | taken | alternative judged |
|---|---|---|---|
| D1 | CPU floor | **V20.** The census at load decides per program: 8086-clean runs everywhere, 186 needs a V20/V30/186/286+, and an 8088 refuses a 186 program with the reason. No 8088 translation tier | Load-time translation of 186 opcodes for the 8088, or interpreting the program's code: not wanted |
| D2 | Where the program runs | **A coroutine on the UI task** (finding 3) | A worker task (SPEC.md §20.6): no file access, and paint deferred out of `W_PAINT`, so every exposure draws twice |
| D3 | Memory model (recommendation; not raised; revisited 2026-10-29) | Every segment fixed in one claim; nothing moves or discards yet. `GlobalAlloc` sub-allocates in arena claims. **Handles, not segments:** a `GMEM_FIXED` handle is its segment, as on real-mode Windows, and a moveable one is a table entry, so a protected-mode arm (R3) and discardable segments (R6) stay reachable | Real-mode Windows' movable segments: `INT 3Fh` loader stubs and BP-chain walking, for two programs that fit easily |
| D4 | Fonts | **What Windows showed on that display**, so the programs look as they did on Windows. MEASURED from Windows 3.0's setup disk 2 (`CGASYS`, `EGASYS`, `VGASYS.FON`), all proportional with maximum width 14: **CGA 8 px (average 7, ascent 7); EGA 12 px (7, 10); VGA 16 px (7, 13)**. **Hercules uses EGA's 12 px font**: `SETUP.INF` gives it EGA's resolution class, `133,96,72`. VGA's face comes from Wine's System font, whose 16 px strike matches (`fonts/system.sfd`). CGA's and EGA's come from Windows' own `.FON` when the user supplies their Windows directory, and otherwise from os8088 strikes drawn to those metrics from source art (CLAUDE.md's artwork rule). S4 shipped each as committed art drawn into a part of `WIN16.O88` and a composer of WIN16's own, not as an `.F88` face through `os88type.inc` (SPEC.md 124.7). Dialog units follow the font, as on Windows | os8088's 8x8 cell everywhere: dialogs half height on VGA and nothing like Windows |
| D5 | Floating point without a coprocessor | **An emulator part of the 8087's instruction set less its transcendentals and BCD** (taken at S9 on the owner's "go ahead", WIDENED from the census's subset toward the end goal - so the owner can still overrule it). S1 MEASURED the subset: CALC 3.00 reaches 35 kinds and CALC 3.10 40, all 8087 instructions; no 287 or 387 form appears. The rest of the 8087 that shares their code (`fxtract`, `fscale`, `fprem`, the `m16`/`m64` integer forms, `fsave`/`frstor`, the constants) came for the price of a table row each, so a program compiled against the same runtime is not refused for an instruction CALC happened not to use. What it LACKS - `f2xm1`, `fyl2x`, `fyl2xp1`, `fptan`, `fpatan`, `fbld`, `fbstp`, every 387 form - ends a program that reaches one, naming it (SPEC.md 124.5.1), and `tools/os88ne.py` reports it ahead of time. The file carries NATIVE x87 code with the OS fixups on the `wait` bytes; the loader rewrites those to `int 34h`–`3Dh` only on a machine without a coprocessor, as Windows did | Only the census's subset (the plan's first wording): a CALC-shaped emulator that refuses the next program over `fxtract`. A whole x87 emulator with the transcendentals: worth it when a program here needs them, and it is a part, so it grows without touching WIN16's image |
| D6 | Kernels | **`kern_big` and `kern_emu`.** `kern_mega` refuses at entry, *WIN16 needs real mode* (not `OS88_MEGA_REFUSE`, whose words name a MEGA apps disk). `kern_small` lacks `OSAPI_WM_TIMER` (SPEC.md §13.9), so its disks do not carry it (§24.5) | A kern_mega arm giving programs real selectors, winevdm's model: later, and now the way to most Windows 3.1-era programs (Beyond S13, R3) |
| D7 | Sources and licence | **Bring in whatever is needed from Wine and winevdm.** `apps/win16/` becomes GPL-2.0-or-later (finding 6), `apps/apple2`'s footing. Microsoft binaries are still never committed | Clean-room MIT: slower, and gains nothing the owner wants |
| D8 | Menus and check marks (revised 2026-10-29, for the end goal) | **WIN16's own menu manager, after Wine's `menu.c`, inside the program's window.** A Windows program's menu bar is in its window and the package owns it, so it costs no kernel bytes, which is what the first decision was for. It draws in the display's System font with Windows' ✓, nests popups, carries 16-bit IDs, and has room for `TrackPopupMenu` and owner-draw later | **`OS88UI_MENU`** (SPEC.md §13.16), the 2026-10-28 decision: no nested popups, a one-byte action per item and os8088's 8x8 cell, so a general Win16 program cannot be served by it. The kernel's global bar: at most 5 menus, no check marks, and not where Windows put it |
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
- the System font for each display (S4: shipped as WIN16's own parts,
  not as an `.F88` face).

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
