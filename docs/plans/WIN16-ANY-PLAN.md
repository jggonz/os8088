# WIN16-ANY-PLAN: from two programs to any Win16 program

**Status: PLAN, not authorised.** Stories S14–S25 are pinned here for the
owner to take, reorder or cut; nothing below is shipped behaviour. The
contract stays SPEC.md §124: each story pins its own subsections there
*before* its code, as S0–S13 did. The diary stays
`docs/HOW-WIN16-WAS-BUILT.md`.

**What came before.** `docs/plans/WIN16-PLAN.md` delivered S0–S13 (PR #320):
Windows 3.0's and 3.1's Minesweeper and Calculator, Clock, Reversi and a
dozen fixtures, in an os8088 window, on every adapter, from a stock 8088 up,
with no kernel byte. Its last section, *Beyond S13*, named nine rows (R1–R9)
between that layer and the owner's end goal:

> The end goal is to be able to run any Win16 application (including CD-ROM
> based win16 applications with installers). *(owner, 2026-10-29)*

This plan turns those nine rows into stories, ordered by what the
measurement below says unlocks the most programs first.

---

## 1. Where WIN16 stands, MEASURED

`tools/w16reach.py` (new in this PR) runs `tools/os88ne.py`'s census on every
program and library in a directory, against WIN16's own export rows
(`apps/win16/w16api.inc`), and says which R rows stand in each one's way.
Run against Windows 3.0's installed `\WINDOWS` (`win16ref/samples/
w3-archive`) and Windows 3.1's (the reference machine's `c-pristine.img`),
neither of which enters the tree:

| | programs | nothing in the way | over 64 KB (R2) | protected mode only (R3) | a module WIN16 lacks (R4) |
|---|---|---|---|---|---|
| Windows 3.0 | 21 | 11 | 7 | 0 | 5 |
| Windows 3.1 | 32 | 11 | 10 | 3 | 17 |

**"Nothing in the way" is not "runs".** WIN16 exports 235 functions from
KERNEL, USER, GDI, KEYBOARD, SOUND, WIN87EM and SHELL. Since S5 a missing
import does not refuse the load (R1); the program runs until it calls one
and is then ended, with the import's name. Across the two versions the
programs import **605 distinct functions WIN16 does not export**. Of the
22 programs with nothing in the way, six lack no import or none they call:
the Calculators, Clock 3.0, Reversi and Minesweeper 3.1, which S9–S11 ran,
and the two WINVERs, which nobody has tried. The other sixteen each lack
between 8 and 57 imports.

**The imports most programs call that WIN16 lacks** (how many of the 75
files import each):

| | imports |
|---|---|
| files (R5) | `OpenFile` 49, `_lclose` 40, `_lread` 35, `_llseek` 33, `_lwrite` 31, `_lopen` 22, `_lcreat` 17, `GetTempFileName` 16, `GetWindowsDirectory` 19 |
| libraries (R4) | `GetProcAddress` 46, `LoadLibrary` 25, `FreeLibrary` 24, `GetModuleHandle` 19 |
| USER and GDI (R7) | `SetScrollPos` 30, `SetScrollRange` 30, `GetActiveWindow` 28, `ScrollWindow` 22, `CopyRect` 22, `InvertRect` 21, `CreateCompatibleBitmap` 20, `FrameRect` 19, `SetWindowExt` 19, `DrawText` 16, `SetMapMode` 16 |
| text | `OemToAnsi` 30, `AnsiToOem` 25 |
| huge pointers (R3, R6) | `__AHINCR` 17, `__AHSHIFT` 16 |
| system DLLs (R4) | `COMMDLG.GetOpenFileName` 16, `GetSaveFileName` 12; `SHELL.DragAcceptFiles` 13 |

**What a Windows 3.1 program imports beyond WIN16's modules:** COMMDLG (18
files), OLECLI and OLESVR (3 each), MMSYSTEM (3), LZEXPAND (3), TOOLHELP (2),
VER (2), and a program's own library (PBRUSH, RECORDER).

**Their sizes** (the census's segment tables):

| program | code | data | segments | discardable |
|---|---|---|---|---|
| SOL 3.0 | 28,098 | 1,066 | 11 | 10 |
| RECORDER 3.0 | 25,007 | 1,107 | 8 | 7 |
| NOTEPAD 3.1 | 19,469 | 3,048 | 6 | 5 |
| REGEDIT 3.1 (protected mode only) | 21,500 | 766 | 2 | 0 |
| WRITE 3.0 | 163,213 | 9,736 | 79 | 78 |
| WINHELP 3.1 | 199,774 | 9,410 | 102 | 101 |

SOL is 180 KB on disk and 28 KB of code: most of the file is its card
bitmaps, which S5's resource reads already fetch one at a time. So **R2 for
SOL is a loader change, not a memory problem**. WRITE and WINHELP are the
memory problem: about 200 KB of code each, which is R6.

**The CPU tiers** (D1 stands: the census decides per program): of Windows
3.1's 32 programs, five are 8086 code, 23 are 186 code, WRITE 3.1 is 286
code, and MPLAYER, SOUNDREC and DRWATSON reach 386 instructions. Below their tier they refuse
with the reason, as CALC 3.1 does on an 8088 today.

---

## 2. The constraints every story meets

These are facts about the tree on 2026-10-30, not choices.

1. **WIN16's segment has 4,946 bytes left.** The image is 46,552 bytes and
   its bss 14,038, in one 64 KB segment. Six hundred missing functions do not
   fit in five kilobytes. S6's dialogs, S9's emulator and S12's loader were
   each moved into a part to make room. S14 makes that the architecture
   (D11).
2. **An owner holds at most 8 claims** (`MEM_OWNER_MAX`, SPEC.md §50), and
   the kernel has 32 records in all (`MEM_MAX`). A program today takes five
   to seven. A program of 79 segments therefore cannot have a claim each,
   and a DLL cannot have a claim of its own beside its user. Segments, heaps
   and libraries are sub-allocated from a few arena claims.
3. **`OSAPI_WM_CLOSE` is deferred in one word** (§75.2). S13 found that
   several Windows programs started in one autostart pass leave DOS windows
   open. R9's tasks close windows in bursts; this is open decision O2.
4. **No Microsoft file enters the tree.** Every program named here is
   fetched by the `win16-reference` skill into the git-ignored `win16ref/`,
   or brought by the user (`make win16disk WIN3X=<dir>`). Where no real
   program exercises a feature, an Open Watcom fixture does (D10).
5. **The real-mode arm stays.** Every story before S22 runs on `kern_big` on
   an XT, so the 8088/V20 floor S11 measured is kept. Only R3 and what rests
   on it need a 286.

---

## 3. Decisions the owner is asked for

| # | question | recommendation | why |
|---|---|---|---|
| O1 | Build the protected-mode arm (R3) on `kern_mega`? | **Yes, as phase B**, after phase A's real-mode stories | Three Windows 3.1 programs are marked protected-mode only, and so are four of its own libraries (COMMDLG, SHELL, MMSYSTEM, TOOLHELP), which suggests that 3.1-era installers' libraries will be too; O3's installers are where that is measured. It is also how a 200 KB program gets memory above 1 MB. It runs on a 286 or better only |
| O2 | Spend kernel bytes? (a) a queue behind `OSAPI_WM_CLOSE`; (b) on `kern_mega` only, a verb handing out **consecutive** LDT selectors, which Windows' `__AHINCR = 8` huge pointers need; (c) `MEM_OWNER_MAX` | (a) **yes**, a few resident bytes, and it is a kernel defect regardless of WIN16; (b) **yes**, when S22 reaches it; (c) **no**: WIN16 sub-allocates | WIN16 has spent no kernel byte so far, and each of these is the owner's to allow |
| O3 | Which CD-ROM titles and installers are the acceptance targets? | **Two installers**, one built with the Microsoft Setup Toolkit (`_MSSETUP.EXE`) and one with 16-bit InstallShield, each pinned by SHA-256 off archive.org the way `win16ref.py` pins Windows; plus **one CD-ROM title** whose program runs from the disc | They are the two toolkits seen most often in installers of the period. A title the owner cares about makes a better target than one chosen here |
| O4 | Split the layer into one far-called part per Windows module (D11)? | **Yes**, as S14, before anything grows | Constraint 1 |
| O5 | Where is "the Windows directory" when a program is installed? | **Unchanged: `SYSTEM\APPDATA\WIN16`** on the disk WIN16 started from, with `SYSTEM\` under it as `GetSystemDirectory`. `LoadLibrary` searches the program's folder, then the system directory, then the Windows directory, Windows' own order | S7's profiles already live there, and a second root would split one program's state across two |
| O6 | OLE, printing, networks | **Answer "not available"**, never a refusal at load: `OleRegisterClientDoc` fails, `PrintDlg` returns `FALSE`, `Escape` reports no printer | WRITE, CARDFILE and PACKAGER import OLECLI and run fine without it on a Windows with no OLE servers |

---

## 4. The stories

Each story is a wave on one PR, as S0–S13 were (CLAUDE.md, *Building an app
or game in waves*). Its verification names its rows; the rows run once, in
S25's final pass. `tools/w16reach.py` is re-run at the end of each wave and
its table goes in the diary, so the reach is measured, not claimed.

### Phase A: real mode, on the XT and up

- [x] **S14: The layer becomes modules (D11, O4).** *Shipped differently
  from the proposal below: SPEC.md §124.19.*
  - *As the owner, I can add the next six hundred functions without
    running out of segment.*
  - WIN16.O88 keeps the gate, the six-byte stubs, the coroutine, the queue
    and the window glue (SPEC.md §124.2). Each Windows module's handlers move
    into a **part of their own**: KERNEL, USER, GDI, and later COMMDLG,
    SHELL, LZEXPAND, VER and MMSYSTEM. A part is fetched on its first call
    and far-called through one vector per module, which is the dialog part's
    `w16_pfv` shape (S6) made general. A module's part may grow to 64 KB.
  - The export rows stay in one table, so a row names a module and a
    handler offset inside that module's part.
  - **Verification:** every S1–S13 row passes unchanged; the reach table is
    unchanged; WIN16's segment room is MEASURED and written down; the cost of
    a call through a module vector is priced on MartyPC (`win16mines`'
    reveal median is the bar: 1.26 s on a V20).
  - **Shipped:** one **library part**, not a part per module: an owner
    holds at most 8 claims and a program already took five to seven, so
    each resident part is a claim the program loses (§124.19). A per-program
    **bind table** of 384 slots replaces the export table in the image; the
    rows moved to the loader part, and a missing import binds a slot like
    any other. Profiles, wsprintf, the clipboard, accelerators, icons,
    bitmaps, SOUND and menus moved into the part beside the dialog manager.
    `t_w16xsec` (fast) refuses a call that crosses sections without the
    macros, which nasm assembles silently.
  - **Measured:** the segment's room went from **4,946 to 19,368 bytes**
    (image 30,583, bss 15,585); the library part is 29,903 bytes. The reach
    table is byte-identical to S13's. Each running program is **16 KB
    larger** (WIN16's share 76 KB against 60) and keeps one more claim, so
    four programs and the Disk window hold 30 of 32 claims and a fifth is
    refused, as at S13.
  - **Seen:** on QEMU, W31CALC, W31MINE, ICONS and MENU at once; ICONS'
    hand box, INIFILE, GDICARD; W31MINE's Game menu with its checks,
    separators and accelerators.
  - **Not yet run:** every `win16*` row, `w16cens` beyond the fast tier,
    and the MartyPC price of a call through the library gate (`win16mines`'
    reveal median). They run in S25's final pass.

- [x] **S15: Programs over 64 KB (R2).**
  - *As a user, I double-click Solitaire and play it.*
  - The loader reads the NE header and tables with `OSAPI_FILE_READ_AT`,
    then each segment to its place, rather than the whole file into one
    claim (SPEC.md §124.3). Segments are placed in **arena claims** WIN16
    sub-allocates (constraint 2), all of them loaded and fixed: nothing is
    discarded yet.
  - `__AHINCR` = 1000h and `__AHSHIFT` = 12, the real-mode values.
  - **Targets:** SOL 3.0 and SOL 3.1 (180 KB, 12 and 15 imports missing),
    then CONTROL 3.0. A Watcom fixture of 100 KB of code and a 128 KB huge
    array checks what SOL does not.
  - **Verification:** a `win16sol` row plays a deal on QEMU and draws its
    cards on all four adapters; the XT's load time on MartyPC.
  - **Shipped:** the loader reads a file in pieces through a cluster cache
    (SPEC.md §124.20.1); Solitaire's calls (§124.20.4): bitmaps keep their
    palette and live in the global heap, sixteen of them; `GetPixel` on the
    screen; nested DCs; `BitBlt`/`PatBlt` INTO a memory DC; the
    pattern-free source ROPs onto the screen; `WS_CLIPCHILDREN` for a child
    along an edge; the first `WM_SIZE` of a hidden window deferred to its
    `ShowWindow`; the client rounded UP to the 8-pixel grid. Control
    Panel 3.0's start-up and main window (§124.20.5), and `TRANSPARENT` text
    (§124.17.1). The `BIG.EXE` fixture (tests/win16/big.c, medium model,
    with a `.wcl` options file the fixture build now reads).
  - **Measured:** the segment's room is **15,915 bytes** (image 30,987, bss
    18,634), down from S14's 19,368; the library part is 34,759 bytes.
    BIG.EXE is 17 code segments and about 155 KB of code.
  - **Seen:** on QEMU, Solitaire 3.0 and 3.1 dealt on VGA, EGA, CGA and
    Hercules, one at a time; 3.0's drag and a legal move; 3.1's ace to a
    foundation, scored; the empty piles' outlines on VGA, EGA and Hercules.
    Control Panel's window, its selection and Help > About. BIG.EXE's
    MessageBox equal to the sums computed on the host.
  - **Not yet:** on CGA Solitaire 3.1 draws no outline on an empty pile -
    no source ROP reaches the screen there, so it draws them another way;
    Control Panel's Settings dialogs (a control class of the program's own
    in a template, `COMBOBOX`, `SCROLLBAR`) and its cut-off Settings popup
    (§124.20.5) are S17's. Two Solitaires at once run short of memory, and
    3.1 says so and drags outlines.
  - **Not yet run:** `win16sol` and `win16big` (registered), and the XT's
    load time on MartyPC. They run in the final pass (S25).

- [ ] **S16: Files (R5).**
  - *As a user, Notepad opens my text file and saves it back.*
  - KERNEL's file API: `OpenFile` (with `OF_EXIST`, `OF_PARSE`, `OF_DELETE`,
    `OF_PROMPT`'s refusal), `_lopen`, `_lcreat`, `_lread`, `_lwrite`,
    `_llseek`, `_lclose`, `GetTempFileName`, `GetWindowsDirectory`,
    `GetSystemDirectory`, `OemToAnsi`/`AnsiToOem`.
  - `int 21h`'s file and directory calls (`3Ch`–`43h`, `47h`, `4Eh`/`4Fh`,
    `56h`, `57h`, `5Ah`, `39h`–`3Bh`) come from DOS.O88's own source, shared
    as source (§96.11, §96.53), never copied.
  - **Targets:** NOTEPAD 3.0, CARDFILE 3.0 and CALENDAR 3.0, which reach
    the files first; their USER and GDI gaps are S17.
  - **Verification:** a `win16file` row writes a file through Notepad and
    reads its bytes off the image on the host.

- [ ] **S17: USER and GDI, the common half (R7).**
  - *As a user, Notepad, Cardfile, Calendar, Character Map, Clipboard and
    PIF Editor run.*
  - The reach table's top missing list, in order:
    - scroll bars on a window and as controls (`SetScrollPos`,
      `SetScrollRange`, `GetScrollPos`, `ScrollWindow`);
    - focus and activation (`GetActiveWindow`, `SetActiveWindow`);
    - the rectangle helpers;
    - `DrawText`, `DrawFocusRect`, `CreateCompatibleBitmap`,
      `SaveDC`/`RestoreDC`, `IntersectClipRect`;
    - the mapping modes (`SetMapMode`, window and viewport origin and
      extent);
    - `CreateFont`, answered from the display's System font and os8088's
      faces (D4);
    - `SetWindowsHook` and `DefHookProc` as a chain that is called;
    - `RegisterWindowMessage`;
    - `DlgDirList` and `DlgDirSelect`.
  - The multi-line EDIT that Notepad *is*.
  - **Targets:** NOTEPAD 3.0, CARDFILE 3.0, CALENDAR 3.0, CLIPBRD 3.0,
    PIFEDIT 3.0, TASKMAN, CHARMAP 3.1, the 3.1 screen savers' settings
    dialogs.
  - **Verification:** a row per program family; a look on all four
    adapters; the redraw priced in calls on Notepad's scroll (PERFORMANCE.md
    Part 5's rule: no full repaint where a scroll will do).

- [ ] **S18: Libraries (R4, the program's own).**
  - *As a user, Recorder runs with its RECORDER.DLL, and Paintbrush with
    PBRUSH.DLL.*
  - NE libraries join the module table (SPEC.md §124.2.1):
    - an implicit import is resolved at load;
    - `LoadLibrary`, `GetProcAddress`, `FreeLibrary`, `GetModuleHandle` and
      `GetModuleFileName` work on them;
    - `LibMain` runs at the first load and `WEP` at the last free;
    - a library has **one** DGROUP, shared by every program that uses it,
      as on real-mode Windows.
  - A library's segments go in the same arenas (constraint 2).
    `GetProcAddress` by name reads the resident and non-resident names
    tables.
  - **Targets:** RECORDER 3.0, then PBRUSH 3.0 (also S15's R2).
  - **Verification:** a `win16dll` row, with a Watcom DLL fixture that
    exports by name and by ordinal and keeps a counter in its DGROUP across
    two users.

- [ ] **S19: The system libraries WIN16 provides (R4, the system's own).**
  - *As a user, File > Open in a Windows 3.1 program shows os8088's Standard
    File dialog.*
  - **COMMDLG** over the Standard File dialog (§38):
    - `GetOpenFileName`, `GetSaveFileName` and `GetFileTitle`;
    - `ChooseColor` and `ChooseFont` from WIN16's dialog part;
    - `PrintDlg` and `FindText`/`ReplaceText` answer "not available" (O6).
  - **SHELL**: `DragAcceptFiles` and the drag calls answer "nothing was
    dropped"; the registration database (`RegOpenKey`, `RegQueryValue`,
    `RegSetValue`) reads and writes `REG.DAT` in the Windows directory (O5);
    `ShellExecute` opens a document through os8088's associations (§54).
  - **LZEXPAND** expands SZDD files. `tools/w16font.py` already reads the
    format on the host, so this is a port of it to the machine.
  - **VER**: `GetFileVersionInfo`, `VerQueryValue`, `VerFindFile` and
    `VerInstallFile`. This is the installer's core (S24).
  - **OLECLI and OLESVR** answer "not available" (O6).
  - **Targets:** the 3.1 accessories that the census stops at COMMDLG:
    NOTEPAD, CLIPBRD, CLOCK, PIFEDIT, CALENDAR, RECORDER, SSMARQUE.
  - **Verification:** a `win16cdlg` row opens and saves through the
    dialog; an LZ row expands a fixture on the machine and compares bytes
    with the host's expansion.

- [ ] **S20: Big programs in small memory (R6).**
  - *As a user, Write opens a document on a 640 KB machine.*
  - Discardable code segments are loaded when first called and **discarded
    under memory pressure**. Real-mode Windows did this with a thunk per
    moveable entry point and with stack walking that patches return
    addresses into discarded segments (KB Q79194, Pietrek). WIN16 does the
    same against its arenas:
    - an entry-table thunk per exported or far-called moveable entry;
    - the coroutine's stack walked by the `INC BP` far-frame marks the
      compiler emits;
    - a code-cache budget taken from free memory at launch.
  - **Targets:** WRITE 3.0 (163 KB of code), WINHELP (200 KB), TERMINAL,
    WINFILE 3.0, CONTROL 3.0.
  - **Verification:** a `win16write` row opens a document under a cache
    deliberately smaller than the program, checks that segments were
    discarded and reloaded, and checks that the document is intact; the XT's
    cost per reload on MartyPC.

- [ ] **S21: One instance, several programs (R9).**
  - *As a user, a Help menu opens Windows Help beside the program, and
    Program Manager starts Notepad.*
  - One WIN16 instance hosts several Windows tasks, rather than S12's one
    instance per program:
    - `WinExec` and `LoadModule` start the next program in the same
      instance;
    - `GetModuleUsage`, `GetCurrentTask`, `GetNumTasks`, `IsTask` and
      `EnumTaskWindows` are answered;
    - `SendMessage` and `PostMessage` cross programs;
    - a library's DGROUP is shared across them (S18);
    - `WinHelp` starts WINHELP.EXE, which retires §124.6's message box.
  - Each Windows top-level window is still an os8088 window, named for its
    program in the Task Manager (§124.18.3). The instance is the one owner
    of the claims, which is constraint 2 again, and why the arenas come
    first.
  - **This story is where O2(a) bites:** programs end in bursts.
  - **Targets:** PROGMAN 3.0 starting CLOCK and NOTEPAD; WINHELP from
    Notepad's Help menu; DDE between two Watcom fixtures (`WM_DDE_INITIATE`
    through `WM_DDE_TERMINATE`).
  - **Verification:** a `win16tasks` row; the claim records counted with
    three programs and a DLL in one instance.

### Phase B: protected mode, on a 286 and up (O1)

- [ ] **S22: The `kern_mega` arm (R3).**
  - *As a user on a 286, Registry Editor runs, and Write keeps its document
    in memory above 1 MB.*
  - WIN16 gains a `PKG_MEGA` arm (§20.18), as the shipped packages with a
    kern_mega arm have. On it:
    - a program's segments get **LDT selectors** through `OSAPI_SEG`
      (`SEG_SUB` for data, `SEG_SUBC` for a code/data pair);
    - the loader's fixups write selectors; `GlobalAlloc` and `GlobalLock`
      hand out selectors;
    - `__AHINCR` = 8, over consecutive selectors (O2(b));
    - `AllocSelector`, `FreeSelector`, `PrestoChangoSelector`,
      `GetSelectorBase`, `SetSelectorBase` and `SetSelectorLimit` work;
    - `GetWinFlags` answers `WF_PMODE | WF_STANDARD`;
    - a DPMI `int 31h` subset covers what Windows 3.0 standard mode offered
      programs.
  - Claims may live above 1 MB (`OS88_HIGH`, `OS88_HIGHMEM`), which
    retires WIN16's two `tests/megahigh.txt` lines and its `megapkg` row.
  - The real-mode arm is unchanged and remains the build on every other
    kernel (D6, revised).
  - **Targets:** REGEDIT 3.1 (marked protected-mode only); SOL and WRITE in
    extended memory; a Watcom fixture that calls the selector functions.
  - **Verification:** Bochs MEGATRAP (docs/BOCHS.md) for the trap census;
    86Box `286-mega` and a 386 to look at; the same rows as phase A on the
    mega disks.

- [ ] **S23: MMSYSTEM (R4).**
  - *As a user, Sound Recorder plays a WAV through my Sound Blaster, and
    Media Player plays an audio CD.*
  - `sndPlaySound`, `waveOut*` and `waveIn*` over `SOUND.DRV` (§34);
    `mciSendCommand` and `mciSendString` for `waveaudio` and `cdaudio`, the
    latter over `CDROM.DRV`'s package door (§119.4); `midiOut*` where
    SOUND.DRV has a MIDI device; `timeGetTime` and `timeSetEvent`.
  - **Targets:** SOUNDREC and MPLAYER 3.1, both protected mode only and
    386 code, so after S22.
  - **Verification:** `make test-snd`'s capture; 86Box `386-sound` and
    `386-mega-cdrom` to listen to.

### Phase C: CD-ROM titles and installers

- [ ] **S24: The installer's environment (R5, R8).**
  - *As a user, I run a Windows program's SETUP.EXE from its floppies or
    its CD, and when it finishes the program is on my desktop.*
  - MSCDEX's `int 2Fh` `15xxh` answers over `CDROM.DRV`, so a setup that
    asks "is this a CD?" is told yes, and `GetDriveType` answers as Windows
    3.1 with MSCDEX did. A write to the CD fails with access denied rather
    than ending the program.
  - Copying, with directories made, `VerInstallFile` and LZ expansion
    (S19), into the folder the user chose and into the Windows and system
    directories (O5). `WIN.INI` and `SYSTEM.INI` edits go through S7's
    profiles.
  - **A Program Manager DDE server**: `[CreateGroup]`, `[AddItem]`,
    `[ShowGroup]` and `[DeleteGroup]` make os8088 links, the kind
    `\AUTOSTRT` holds (§26.10), in a desktop folder named for the group.
  - `ExitWindows` with `EW_RESTARTWINDOWS` ends the setup and says that a
    restart is not needed.
  - **Targets:** the two installers of O3, through both toolkits'
    bootstraps: `SETUP.EXE`, which copies the toolkit to a temporary folder
    and `WinExec`s it (S21).
  - **Verification:** a `win16setup` row runs an installer fixture to its
    end and reads the installed files and the links off the image.

- [ ] **S25: Acceptance.**
  - *As the owner, I can see a Windows CD-ROM title installed and running
    on os8088, and what still does not run.*
  - The O3 title installed from its CD and run from its desktop link on
    86Box's `286-mega-cdrom` and `386-mega-cdrom`.
  - What of it runs in real mode, on the XT, where its CPU tier allows.
  - `tools/w16reach.py` over Windows 3.0's and 3.1's programs, and the
    titles' own, with how many now run, compared with this plan's §1.
  - The deduplicated soak union of every row S14–S24 named, then
    `make test-full`.
  - The resident cost (zero kernel bytes, unless O2 spent some), the
    package's image and the claims, MEASURED.
  - The diary closed with screenshots, against the reference machines, and
    the limitations stated.

---

## 5. What this plan does not do

- **A 32-bit program.** Win32s and Windows 95's PE programs are not Win16.
- **Windows' own drivers** (`.DRV`, `.386`). WIN16 is the system
  underneath a program, as Wine is; it does not load Windows' `USER.EXE`,
  `GDI.EXE` or display drivers.
- **Enhanced mode's virtual machines.** A DOS box inside a Windows program
  is os8088's DOS.O88, beside it.
- **Printing** (O6).

---

## 6. Sources

As WIN16-PLAN.md's, plus:

- Wine's `dlls/krnl386.exe16/` (`ne_module.c`'s library loading,
  `selector.c`, `dosmem.c`, `int31.c`), `dlls/commdlg.dll16/`,
  `dlls/lzexpand/`, `dlls/ver.dll16/`, `dlls/shell.dll16/` and
  `dlls/mmsystem.dll16/`, at the commit WIN16-PLAN pins;
- winevdm's `krnl386` (its discardable-segment and selector handling on
  real programs) and its issue tracker, which records which installers
  needed what;
- Pietrek, *Windows Internals*, chapters 2 and 3 (the global heap, moveable
  and discardable segments in real mode, the `INC BP` frames); Schulman,
  *Undocumented Windows* (DPMI under standard mode, MSCDEX);
- Microsoft KB Q79194 (real versus protected mode) and Q72540, as before.
