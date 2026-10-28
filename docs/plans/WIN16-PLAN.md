# WIN16 — running Windows 3.x programs on os8088

**STATUS: PLAN. Nothing here is built.** Every size is an ESTIMATE unless the
line says MEASURED, and every claim about what WINMINE.EXE or CALC.EXE
contains is INFERRED until story S1 dumps the user's own copies. The binding
contract is SPEC.md §124, which is pinned story by story as this plan's
checkboxes are ticked. The diary is
[HOW-WIN16-WAS-BUILT.md](../HOW-WIN16-WAS-BUILT.md).

The ask: double-click the original Windows 3.x **Minesweeper** (`WINMINE.EXE`)
or **Calculator** (`CALC.EXE`) in a Disk window and play it in an os8088
window, through a compatibility layer, "a Wine for os8088", that lives on
disk until a Windows program is opened.

---

## The verdict, before the detail

**It is feasible, and for the same reason DOS.O88 was** (SPEC.md §96): a
Windows 3.x program is 16-bit x86 machine code for the processor we are
standing on. Windows 1.x–3.0 ran these programs in 8086 real mode. There is no
CPU to emulate. "Running" a Windows program is:

1. loading its NE segments;
2. applying its relocations;
3. pointing its `KERNEL`, `USER`, `GDI` and `SHELL` imports at far-callable
   code of ours;
4. calling its entry point.

Wine and winevdm are what make this tractable as a plan. They are not a source
of code: Wine is LGPL and winevdm is GPL, and this tree is MIT and
hand-written (decision D7). What they give us is the NE loader's every corner
case, the ordinal of every API function, and the behaviour of each one,
cited by file and line.

Five findings shape everything below.

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

3. **The program runs as a COROUTINE on the UI task, not in a worker.**
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

4. **The CPU is the real fork, and S1 measures it before anything is built.**
   - **Windows 3.1:** it has no real mode, and Microsoft told its developers
     to build for the 286 (KB Q78326). The 3.1 WINMINE and CALC very likely
     contain 286 instructions (`push imm`, `enter`/`leave`, `shl r,imm`).
   - **Why an 8088 cannot run them:** it does not fault on those opcodes. It
     executes them as something else: `0x60`–`0x6F` alias the conditional
     jumps, and `0x0F` is `pop cs`. A census per code segment at load time is
     therefore mandatory, not optional.
   - **The 286 and up:** `kern_big` runs in real mode, so 186/286 opcodes
     execute natively.
   - **The 8088 / 8086:** the honest default is a refusal that names the
     reason. Story S11 is a decision-gated translation tier.
   - **The NEC V20/V30:** these run the 186 set natively, so a binary whose
     census shows 186 opcodes and no 286 opcodes runs on a V20 XT.
   - **Windows 3.0:** its CALC.EXE ran on 8086 real mode, so it is very
     probably 8086-clean.

5. **Calculator needs floating point; Minesweeper very probably does not.**
   - **How MS C compiles x87 code for Windows:** as `int 34h`–`3Dh` sequences,
     with NE "OS fixups" (relocation type 3) marking each one. A loader
     patches them to real x87 instructions when a coprocessor is present.
     WIN87EM.DLL emulates them when it is not.
   - **What that means here:** CALC on a machine without an 8087 needs an
     emulator for the subset of x87 it uses. That is its own story (S9),
     sized by S1's census.

---

## What "run a Windows program" decomposes into

| # | problem | who provides it today | work |
|---|---|---|---|
| 1 | NE load: segments, relocations, prologs, DGROUP, local heap, InitTask | nobody (DOS.O88's MZ loader is a model, not a part) | medium |
| 2 | The message model: queue, `GetMessage`/`DispatchMessage`, the coroutine | nobody | medium |
| 3 | Windows: top-level and child windows, classes, `DefWindowProc` | **the window manager (SPEC.md §11)**, one os8088 window per top-level window | medium |
| 4 | GDI: DCs, pens, brushes, text, bitmaps, `BitBlt` | **`OSAPI_GFX_*`**, `OSAPI_FONT_RUN`, the 4bpp blitter (SPEC.md §5.4.1.3) | large |
| 5 | Resources: menus, accelerators, strings, bitmaps, icons, dialog templates | nobody; read out of the in-memory image | medium |
| 6 | Dialogs and controls: BUTTON, STATIC, EDIT, LISTBOX, `MessageBox` | **`apps/os88ui.inc`** and `os88line.inc` draw them (SPEC.md §13.8–§13.17, §83) | large |
| 7 | Menus | **`OSAPI_MENU_SET`** (SPEC.md §12.2): one Mac-style bar, at most 5 menus, no check marks | small–medium |
| 8 | Profiles: `GetProfileInt`, `WritePrivateProfileString` | **`SYSTEM\APPDATA`** (SPEC.md §19.9), but no INI parser | small |
| 9 | Timers, capture, beep, clipboard, About | **`OSAPI_WM_TIMER`**, press-tracking (SPEC.md §13.7), `OSAPI_SND_TONE`, `OSAPI_CLIP_*`, `OSAPI_ABOUT_SET` | small |
| 10 | Floating point (CALC) | nobody | medium–large |
| 11 | Launch from a double-click | **assoc (SPEC.md §54)**: `EXE` is DOS.O88's, so DOS.O88 forwards an `NE` file | ~nil |

Items 3, 4, 6, 9 and 11 are mostly mapping. Items 1, 2, 5, 8 and 10 have to be
written.

---

## Delivery stories

Each wave is a reviewable commit pushed to the same draft PR. A story is
ticked only when its outcome has been **seen on the glass** and its named
rows exist. "Pinned" means SPEC.md §124 has the contract and the code does
not. Stories S8 and S10 are the two the ask is about; everything before them
is what they stand on.

- [ ] **S0: Foundation.** Isolated worktree (`~/Repos/os8088-win16`, branch
  `plan/win16`), this plan, SPEC.md §124's contract skeleton, the diary, and a
  draft PR. *Verify:* `make` passes `checkdocs` and the index check.

- [ ] **S1: Census.** *As a developer I can point the build at my own
  Windows 3.x files and see exactly what each one needs before a line of the
  layer exists.*
  - `tools/os88ne.py` (host, Python, written from the format: SPEC.md
    §124.3) reports:
    - the NE header;
    - the segment table;
    - every import as `MODULE.ordinal` plus its name;
    - the resource directory (menus, dialogs with their control classes,
      bitmaps with their depth, accelerators, strings);
    - the OS-fixup count;
    - an **instruction census per code segment**: every 186/286/x87 opcode
      found by a linear sweep from the entry points, with its address.
  - `make win16census WIN3X=<dir>` runs it over every `.EXE` in a directory
    the user supplies. **No Microsoft binary is ever committed** (D7).
  - A committed **synthetic fixture**, `tests/win16/hello16.asm`, is assembled
    by `nasm -f bin` with a hand-built NE header. It calls MessageBox,
    registers a class, creates a window, paints, and takes a timer, so every
    gate below has something to load without the user's files.
  - *Outcome:* the API-surface table in this plan is rewritten from fact. D1
    (CPU tiers) and D5 (floating point) are decided on numbers.
  - *Verify:* a fast row over the fixture (`win16ne`), plus the census of the
    owner's copies quoted in the diary.

- [ ] **S2: Load or refuse.** *As a user, when I double-click WINMINE.EXE I
  get either the program, or one sentence saying why not, and never a crash.*
  - **Launch:** DOS.O88 sniffs `e_lfanew` → `NE` and forwards the document to
    `WIN16.O88` with `OSAPI_PKG_START`'s document form (SPEC.md §21.5.3).
  - **Loader:**
    - all segments go into ONE arena claim, loaded fixed: never moved,
      never discarded (D3);
    - relocations are applied, including chains and additive records;
    - exported prologs are patched (Wine's `NE_FixupSegmentPrologs` is the
      reference);
    - DGROUP is built with its local heap and stack, and `InitTask`'s
      register contract is honoured;
    - `hInstance` = the DGROUP paragraph.
  - **Refusals, each worded:**
    - an import we do not implement: *"WINMINE needs USER.287 (name), which
      this layer does not provide"*;
    - a CPU the census rules out (D1);
    - a self-loading or library module;
    - out of memory.
  - *Verify:* the fixture reaches its WinMain; a doctored fixture with an
    unknown import refuses with its name; `kern_small` and `kern_mega` refuse
    (D6).

- [ ] **S3: The message pump.** *As a user I see the program's window, I can
  move it, click in it, and close it with the close box.*
  - The API: `RegisterClass`, `CreateWindow`, `ShowWindow`, `UpdateWindow`,
    `DefWindowProc`, `GetMessage`/`PeekMessage`/`TranslateMessage`/
    `DispatchMessage`, `PostMessage`/`SendMessage`, `PostQuitMessage`,
    `SetTimer`/`KillTimer`, `SetCapture`/`ReleaseCapture`, `GetTickCount`.
  - The coroutine of finding 3, and the stack gate of finding 2.
  - Each top-level window is one os8088 window.
  - Messages: `WM_CREATE`, `WM_PAINT`, `WM_ERASEBKGND`, `WM_LBUTTON*`,
    `WM_RBUTTON*`, `WM_MOUSEMOVE`, `WM_KEY*`, `WM_CHAR`, `WM_TIMER`,
    `WM_CLOSE`, `WM_DESTROY` and `WM_QUIT`.
  - The close box and a Task Manager kill both tear down every claim.
  - *Verify:* the fixture's window opens, repaints only its damage, counts
    timer ticks, and closes cleanly; `mem_tab` is back to its starting state.

- [ ] **S4: GDI.** *As a user the program's window looks the way it did on
  Windows, in colour on VGA/EGA and legibly on CGA and Hercules.*
  - **DCs:** `BeginPaint`/`EndPaint`, `GetDC`/`ReleaseDC`, memory DCs
    (`CreateCompatibleDC`, `SelectObject`, `DeleteDC`).
  - **Drawing:** stock and solid pens and brushes, `PatBlt`, `Rectangle`,
    `MoveTo`/`LineTo`, `SetPixel`.
  - **Text:** `TextOut`/`ExtTextOut`/`DrawText` and `GetTextMetrics` with the
    system-font metrics of D4.
  - **Bitmaps:** DIB resources become the packed 4bpp form `OSAPI_GFX_BLIT4`
    takes (rows flipped, palette remapped to the 16 desktop colours);
    `BitBlt`/`StretchBlt` (1:1 only) and `SetDIBitsToDevice`.
  - **Mono:** a 1bpp mapping that follows the desktop's own grey rules
    (SPEC.md §39).
  - **The cost rule:** priced in primitive calls, not pixels (PERFORMANCE.md
    Part 5). A cell-sized blit is one call; nothing writes a pixel twice.
  - *Verify:* the fixture's test card compared on VGA, EGA, CGA and Hercules;
    a call counter on a full Minesweeper repaint written into the diary.

- [ ] **S5: Resources and menus.** *As a user the program's own menus appear
  in the menu bar, its shortcuts work, and its check marks show.*
  - `LoadMenu` → `OSAPI_MENU_SET`. The bar's 5-menu limit and ~24-character
    items are checked by the census.
  - `CheckMenuItem`/`EnableMenuItem`. Greying exists; **check marks do
    not**, so this story owns the smallest way to show one (D8).
  - `LoadAccelerators`/`TranslateAccelerator`, `LoadString`, `LoadIcon`
    (the dock and About card), `LoadCursor` (mapped to the arrow), and
    `LoadBitmap`.
  - *Verify:* WINMINE's Game menu and CALC's View menu open, check and
    command; F2 starts a new game.

- [ ] **S6: Dialogs and controls.** *As a user the program's dialog boxes
  open, I can type in them, and OK/Cancel do what they say.*
  - `DialogBox`/`EndDialog`, dialog-template parsing with dialog units from
    D4, and `MessageBox`.
  - Controls: BUTTON (push, default, check box, radio, group box), STATIC,
    single-line EDIT, and LISTBOX for CALC's statistics box. All are drawn
    with `os88ui.inc`/`os88line.inc` inside the dialog's one os8088 window.
  - `GetDlgItem`/`SetDlgItemText`/`GetDlgItemInt` and friends.
  - `ShellAbout` → an About card through `OSAPI_ABOUT_SET`.
  - **Loaded as a part**, fetched on the first dialog and dropped after the
    last.
  - *Verify:* WINMINE's Custom Field and Best Times dialogs, and its name
    entry.

- [ ] **S7: Profiles.** *As a user my best times and my chosen difficulty are
  still there tomorrow.*
  - `GetProfileInt`/`GetProfileString`/`WriteProfileString` and the
    `PrivateProfile` forms.
  - A minimal INI reader and writer over `SYSTEM\APPDATA\WIN16\` (SPEC.md
    §19.9): `WIN.INI` and each program's own `.INI`.
  - Writes are made on the UI task, so the file rules hold.
  - *Verify:* set Expert, record a time, close, reopen; the INI on the host
    reads back with `mtools`.

- [ ] **S8: Minesweeper.** *As a user I can play the original Windows 3.1
  Minesweeper on os8088, every level, with the timer, the mine counter, the
  face, marks, colour and best times, on every adapter my machine has.*
  - Its sound, if the census finds `MessageBeep` or a SOUND voice call, goes
    to `OSAPI_SND_TONE`.
  - WinHelp refuses with its reason, greyed (SPEC.md §47).
  - The CPU it needs is whatever D1 decided, stated on the glass when it is
    not met.
  - *Verify:* MartyPC and QEMU rows (`win16mines`) play a scripted game to a
    win and a loss; the four adapters are looked at; the XT's frame cost is
    in the final pass.

- [ ] **S9: Floating point.** *As a user Calculator gets the right answer
  whether or not my machine has a coprocessor.*
  - With an 8087/287/387 present: the OS fixups are patched to native
    instructions.
  - Without one: an emulator of exactly the x87 subset S1's census found,
    loaded as a part.
  - The 3.0–3.11 decimal-subtraction bug (KB Q72540) is reproduced, not
    fixed: this is the original program.
  - *Verify:* a host-side oracle compares the emulator against Python's
    IEEE doubles over the census's opcode set; a QEMU leg runs with an FPU
    and a MartyPC leg without.

- [ ] **S10: Calculator.** *As a user I can use the original Windows
  Calculator in Standard and Scientific modes, copy and paste through the
  os8088 clipboard, and use its Statistics box.*
  - Clipboard through `OSAPI_CLIP_PUT`/`GET`.
  - Its keypad is either the program's own drawing or BUTTON controls; S1
    says which.
  - *Verify:* the `win16calc` row keys a calculation in each mode and reads
    the display, then pastes a number in from Note Pad.

- [ ] **S11: The 8088 tier (gated on D1).** *As an XT owner without a V20,
  either the programs run, or I am told plainly they cannot.*
  - If S1 shows the 3.1 binaries carry 186/286 code, this story is load-time
    translation: each such instruction becomes a 2-byte `int` trampoline,
    with a side table keyed by return address.
  - That is only taken if the census proves a sweep from the entry points
    reaches every such instruction. Otherwise the refusal stays and is the
    shipped answer.
  - The Windows 3.0 CALC, if 8086-clean, runs on the 8088 regardless.

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
  - The four adapters, the XT (MartyPC, with V20 if D1 needs it), a 286
    (86Box `vm/286`) and a 386 (QEMU).
  - Measured resident cost (zero kernel bytes is the target, checked with
    `tools/kernsize.py`), and the package image and claims MEASURED.
  - The diary closed with screenshots and the limitations stated.

---

## Decisions for the owner

Each has a recommendation. The ones marked **gate** are taken on S1's
numbers, not now.

| # | decision | recommendation | alternative judged |
|---|---|---|---|
| D1 **gate** | Which CPUs run which binaries | Census per segment at load. 186 set → V20/V30/186/286+. 286 set → 286+. Clean → everything. Otherwise refuse with the reason. S11 decides translation on numbers | Interpreting the program's code on an 8088: every message handler at ~1/50th speed, and a flood-fill reveal in Minesweeper would take seconds |
| D2 | Where the Windows program runs | A coroutine on the UI task (finding 3) | A worker task (SPEC.md §20.6): no file access, and paint would be deferred out of `W_PAINT`, so every exposure draws twice |
| D3 | Memory model | Every segment fixed in one arena claim; nothing moves or discards; `GlobalAlloc` sub-allocates inside a second claim (8 claims per owner, SPEC.md §50.3) | Real-mode Windows' movable segments: `INT 3Fh` loader stubs and BP-chain walking, for two programs that fit easily |
| D4 | System font and dialog units | Report the metrics Windows' own driver for that adapter used (CGA 8x8, EGA 8x12, VGA 8x16, Hercules 8x14), drawn from the 8x8 set (line-doubled on VGA), so templates lay out as they did on that adapter | Report 8x8 everywhere: dialogs come out half height on VGA |
| D5 **gate** | Floating point without a coprocessor | Emulate only the census's opcode subset, as a part | A whole x87 emulator, or refusing CALC on FPU-less machines |
| D6 | Kernels | `kern_big` and `kern_emu` only. `kern_small` lacks `OSAPI_WM_TIMER` (SPEC.md §13.9), so it is a §24.5 omission. `kern_mega` refuses (`OS88_MEGA_REFUSE`, SPEC.md §20.18) | A kern_mega arm giving the program real selectors, winevdm's model: later, not now |
| D7 | Sources and licence | Clean-room, MIT, 8086 assembly. Wine and winevdm read at pinned commits as references; formats, ordinals and constants are interoperability facts and are used. Microsoft binaries are supplied by the user (`WIN3X=<dir>`, `GBROMS=`'s shape) and never committed | Porting Wine's C through the C toolchain (SPEC.md §73): it thunks to Win32 USER/GDI, so the half that would port is the half we do not need, and the tree would become LGPL |
| D8 | Menu check marks | The smallest addition that shows one: a marker prefix beside `MENU_DIS` in `OSAPI_MENU_SET`'s strings | Renaming items ("Marks: on"): changes the program's text |
| D9 | Name | `WIN16.O88`, "Windows 3.x programs", `apps/win16/`. "Wine" names a different project with no code here | The owner's choice |

---

## The API surface, as far as it can be known before S1

The ordinals are Wine's (the spec files at the pinned commits below). Which
of them these programs import is INFERRED until S1, which replaces this table.

| module | likely imports |
|---|---|
| KERNEL | InitTask 91, WaitEvent 30, LocalInit, LocalAlloc/Free, GlobalAlloc/Lock/Unlock/Free, LockSegment, MakeProcInstance, GetProfileInt/String, WriteProfileString, the `PrivateProfile` forms, GetModuleHandle, lstrcpy/lstrlen, Catch/Throw 55/56, DOS3Call 102, the constants `__AHINCR` 114 (real mode: 0x1000), `__AHSHIFT` 113 (12), `__WINFLAGS` 178 (WF_CPU086 0x40 / WF_CPU186 0x80 / WF_CPU286 0x02, never WF_PMODE, plus WF_80x87 0x400 when present), and `__0040H`, `__A000H`, `__B800H`, `__F000H` |
| USER | MessageBox 1, InitApp 5, PostQuitMessage 6, SetTimer 10, KillTimer 12, GetTickCount 13, SetCapture 18, ReleaseCapture 19, BeginPaint 39, EndPaint 40, CreateWindow 41, ShowWindow 42, RegisterClass 57, GetDC 66, ReleaseDC 68, DialogBox 87, EndDialog 88, the dialog-item calls 92–96, MessageBeep 104, DefWindowProc 107, GetMessage 108, TranslateMessage 113, DispatchMessage 114, UpdateWindow 124, InvalidateRect 125, the clipboard 137–142, LoadMenu 150, CheckMenuItem 154, EnableMenuItem 155, WinHelp 171, LoadCursor/Icon/Bitmap/String/Accelerators 173–177, TranslateAccelerator 178, GetSystemMetrics 179, GetSysColor 180, wsprintf 420 (cdecl) |
| GDI | the DC, pen, brush, text and bitmap calls of S4; SetDIBitsToDevice 443; ExtTextOut 351 |
| SHELL | ShellAbout 22 |
| WIN87EM | CALC only, if it links the emulator library (S9) |

Two documented quirks are already known:

- **WINMINE creates its window `WS_MINIMIZE` and sizes it afterwards**
  (winevdm issue #181, where the width was clamped to `SM_CXMINTRACK`). Our
  `GetSystemMetrics` must not reproduce that clamp.
- **Its settings live in `WINMINE.INI`**: Difficulty, Height, Width, Mines,
  Mark, Color, Xpos/Ypos, Time1–3, Name1–3, and Sound/Tick, which the 3.1 menu
  does not expose.

---

## Memory and speed, ESTIMATED

**Memory:**

- **The layer's image:** under `APP_MAX_SIZE`, with dialogs and floating
  point as parts. A guess of 30–40 KB resident in the package, against Paint's
  ~22 KB and ETHER.DRV's ~11 KB.
- **A program:** WINMINE's file is roughly 28 KB and CALC's roughly 43 KB.
  Each runs in one arena of its segments plus DGROUP (stack and local heap
  from the NE header), and a `GlobalAlloc` claim.
- **One program's total:** about 100 KB.
- **What that means per machine:** two programs fit a 640 KB machine
  comfortably. A 256 KB machine runs one, and the second refuses before any
  I/O with the arithmetic, which is WEAVE's floor-machine shape (WEAVE-SPEC
  §1.4).

**Speed**, on a 4.77 MHz 8088 (or a V20 at the same clock), priced from
PERFORMANCE.md:

- **One 16x16 Minesweeper cell:** one `OSAPI_GFX_BLIT4`. The planar decoder
  prices it at about 107 cycles a pixel, so roughly 6 ms.
- **A Beginner board (81 cells):** about 0.5 s to paint.
- **An Expert board (480 cells):** about 3 s on the XT and a fraction of that
  on a 286.

That is the case for S4's bitmap cache and for drawing only the damage,
which the coroutine model makes natural. S13 measures it; nothing here is
promised.

---

## Existing facilities reused

docs/INDEX.md was read first.

| what | facility |
|---|---|
| launch | the association of `EXE` with DOS.O88 (SPEC.md §54, §96) and `OSAPI_PKG_START`'s document form |
| code loaded on demand | package parts (SPEC.md §20.12, `apps/os88parts.inc`) and `OSAPI_PKG_REHOME` |
| windows | the window record and its callbacks (SPEC.md §11), damage (§11.90), clip (§11.3) and press tracking (§13.7) |
| drawing | `OSAPI_GFX_*`, `OSAPI_FONT_RUN` (one pass, SPEC.md §6.1), `OSAPI_GFX_BLIT4` |
| UI | the menu bar (SPEC.md §12.2), `os88ui.inc`'s controls (SPEC.md §13.8–§13.17), line editing (SPEC.md §83) and the About card (SPEC.md §12.2's `OSAPI_ABOUT_SET`) |
| services | the timer (SPEC.md §13.9), the clipboard (`OSAPI_CLIP_*`), speaker tones (`OSAPI_SND_TONE`), claims (SPEC.md §50.3) and app data (SPEC.md §19.9, `apps/os88data.inc`) |
| CPU detection | `OSAPI_CPU_INFO` (SPEC.md §60.1) tells 8086/286/386 apart but not a V20. `apps/redline/detect.inc` does, and is SHARED AS SOURCE rather than copied (WEAVE-SPEC §1.2's rule) |

**Missing, and owned by a story:**

- check marks in the menu bar (S5, D8);
- an INI parser (S7);
- an x87 emulator (S9);
- a Win16-shaped dialog manager (S6).

---

## Sources

Read at these commits, cited by path, never copied (D7):

| source | commit | licence | what it is good for |
|---|---|---|---|
| Wine (`github.com/wine-mirror/wine`) | `0738d528db522f52d9d75f2aec712688dd31d1d2` | LGPL-2.1+ | `dlls/krnl386.exe16/ne_segment.c` (relocations at lines 132–326, prolog patching at 510–574), `ne_module.c` (task start, lines 1062–1084), `task.c` (InitTask, MakeProcInstance), `dlls/user.exe16/dialog.c` (templates), `include/winnt.h` and `include/wine/winbase16.h` (structures and flags), the four `.spec` files (ordinals), `tools/winedump/ne.c` (a reference NE dumper to check `os88ne.py` against) |
| winevdm / otvdm (`github.com/otya128/winevdm`) | `b88d7f8736d77baa5710eb0c35f81b489540c1a0` | GPL-2; Wine-derived files LGPL; MAME CPU core BSD-3 | Behaviour on real Windows 3.1 programs; issue #181 (WINMINE's window width) and #60 (Windows 3.1 CALC running) |
| Microsoft KB Q11979, Q78326, Q79194, Q72540; *Windows Internals* (Pietrek); *Undocumented Windows* (Schulman) | — | references | Prologs, real mode vs protected mode, `__AHINCR`, the CALC bug |
| Win3mu (Brad Robinson, GPL-3) | — | prior art | An emulator plus thunks that runs Minesweeper; its write-up on thunking |

Nothing found reimplements Win16 natively on a real-mode 8086, so this
would be the first.

---

## Verification discipline

**During waves:**

- assemble with `cpu 8086` and `-w+error`;
- run `make`, which runs the fast tier;
- run `tools/kernsize.py` on both kernels (the target is no change);
- boot once to look;
- name the rows each story adds in `tests/suite.py`: `win16ne` (fast, host),
  `win16load`, `win16pump`, `win16gdi`, `win16dlg`, `win16ini`, `win16mines`,
  `win16fpu`, `win16calc`, `win16multi`.

**Rows that need the user's binaries** skip with a note when `WIN3X` is not
set, and say so. They never pass silently.

**After the last wave:**

- one deduplicated `os88test.py soak -k …` union, then `make test-full`;
- re-run failures only.

**Emulators:**

- QEMU is the instrument for behaviour.
- MartyPC is the instrument for the XT's cost and for the 8088 refusal.
- 86Box `vm/286` is where a human looks at the 286 tier.
