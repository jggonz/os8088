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
| O2 | Spend kernel bytes? (a) a queue behind `OSAPI_WM_CLOSE`; (b) on `kern_mega` only, a verb handing out **consecutive** LDT selectors, which Windows' `__AHINCR = 8` huge pointers need; (c) `MEM_OWNER_MAX` | (a) **yes**, a few resident bytes, and it is a kernel defect regardless of WIN16; (b) **yes**, when S22 reaches it; (c) **no**: WIN16 sub-allocates. **The owner answered yes to (a) and (b) on 2026-11-01** | WIN16 has spent no kernel byte so far, and each of these is the owner's to allow |
| O3 | Which CD-ROM titles and installers are the acceptance targets? | **Two installers**, one built with the Microsoft Setup Toolkit (`_MSSETUP.EXE`) and one with 16-bit InstallShield, each pinned by SHA-256 off archive.org the way `win16ref.py` pins Windows; plus **one CD-ROM title** whose program runs from the disc. **Chosen 2026-11-01 at the owner's request ("look for a suitable ISO title from archive.org to try"): Microsoft Multimedia Mozart (1993) as the title, Microsoft Arcade 1.0 (1993) for the Setup Toolkit, and Sir Hangman 1.0 (1995) for InstallShield 3; section 3.1 below** | They are the two toolkits seen most often in installers of the period. A title the owner cares about makes a better target than one chosen here |
| O4 | Split the layer into one far-called part per Windows module (D11)? | **Yes**, as S14, before anything grows | Constraint 1 |
| O5 | Where is "the Windows directory" when a program is installed? | **Unchanged: `SYSTEM\APPDATA\WIN16`** on the disk WIN16 started from, with `SYSTEM\` under it as `GetSystemDirectory`. `LoadLibrary` searches the program's folder, then the system directory, then the Windows directory, Windows' own order | S7's profiles already live there, and a second root would split one program's state across two |
| O6 | OLE, printing, networks | **Answer "not available"**, never a refusal at load: `OleRegisterClientDoc` fails, `PrintDlg` returns `FALSE`, `Escape` reports no printer | WRITE, CARDFILE and PACKAGER import OLECLI and run fine without it on a Windows with no OLE servers |

### 3.1 O3's targets, chosen (2026-11-01)

Each is fetched by `win16ref.py titles` into the git-ignored `win16ref/`,
SHA-256-pinned, and never enters the tree. `w16unpack.py` (beside it) opens
what the toolkits compress, so `tools/w16reach.py` can read every program.

| role | title | archive.org item, file | SHA-256 | what it is |
|---|---|---|---|---|
| **the CD-ROM title** | *Microsoft Multimedia Mozart: The Dissonant Quartet* 1.0, Microsoft Home, 1993 | `Microsoft_Multimedia_Mozart_Win3.1_Eng`, `MSMOZART.bin` + `.cue` (the whole mixed-mode disc, 699 MB); `MSMOZART`, `MSMOZART.iso` (its data track alone, 147 MB) | `222209d6...` (bin), `8ef7ad55...` (cue), `c17aedd7...` (iso) | an ISO 9660 data track and **six Red Book audio tracks**. `\MOZART\MOZART.EXE` runs **from the disc**; the setup installs only shared libraries and a group |
| **the Setup Toolkit installer** | *Microsoft Arcade* 1.0, Microsoft, 1993 | `microsoft-arcade-v-1.0-microsoft-1993`, `Microsoft Arcade v1.0 (Microsoft) (1993).img` (one 1.44 MB disk) | `b7bfabca...` | `SETUP.EXE`, `SETUP.LST`, `_MSSETUP.EX_`, `_MSTEST.EX_` and the `MS*STF.DLL` set, SZDD-compressed: the toolkit in its textbook form. Five games (Asteroids, Battlezone, Centipede, Missile Command, Tempest) |
| **the InstallShield installer** | *Sir Hangman* 1.0, Ssorg Software, 1995 | `Night_Owl_The_Best_of_Shareware_NOPV-20_Night_Owl_Publisher_1996`, `...(1996).ISO/001A/SIR_HM10.ZIP` (archive.org's own copy-out of one file of the disc) | `f9c9896f...` | **InstallShield 3.0** (Stirling Technologies' copyright in `_INST16.EX_`): `SETUP.EXE`, `_INST16.EX_`, `_SETUP.LIB`, `SETUP.INS`, `SETUP.PKG`, `PROGRAM.LIB`, `SYSTEM.LIB`. Makes the group "Ssorg Games" |

Full hashes are in `win16ref.py`'s `TITLES`, which refuses a mismatch.

**What the census says** (`tools/w16reach.py`, 2026-11-01, against WIN16's
560 exports):

| program | CPU | in the way | what it needs |
|---|---|---|---|
| `MOZART.EXE` (248 KB) | 8086 | R2, R4 | one import, `VBRUN300.100`: it is **Visual Basic 3.0** p-code |
| `VBRUN300.DLL` (398 KB, 101 segments) | 186 + x87 | R2, **R3** | 300 imports, 48 missing; protected mode only. The same file, byte for byte, rides with Sir Hangman |
| Mozart's `SETUP.EXE`, `_MSTEST.EXE` | 186, 386 | **R3** | the Setup Toolkit's bootstrap and its MS Test interpreter; `SETUP.MST` calls `CreateProgManGroup` and `CreateProgManItem` (S24's DDE server) |
| Mozart's `ENCCDPRF.DLL`, `MMUTIL.DLL` | 186 | R3, R4 | MMSYSTEM: the setup's performance check (`MMPERF.DAT`, `MMPERF.INC`) |
| Arcade's `SETUP.EXE`, `_MSTEST.EXE` | 186, 386 | **R3** | as Mozart's |
| Arcade's five games (165-408 KB) | **386 + x87** | R2, R3, R4 | `WAVEMIX.DLL` over MMSYSTEM's `waveOut*` (S23), and three libraries of their own |
| `HANGMAN.EXE` (623 KB) | 8086 | R2, R4 | `VBRUN300.100`, with MCI, GRID and CMDIALOG VBXs |
| InstallShield's `SETUP.EXE`, `_INST16.EXE` (543 KB, 68 segments) | 186 (+ x87) | R2, R4 | **real-mode capable**; `DDEML` for the Program Manager conversation |

So the three are one layer apart, and it is the same layer: **Visual Basic
3.0's runtime** carries the title and one installer's program, and every
Setup Toolkit bootstrap is protected mode only. That is S22's. CD audio
(`MCI.VBX` over `mciSendCommand`'s `cdaudio`) and WAV are S23's. The
Program Manager DDE, `WinExec` and the CD's `int 2Fh` are S24's.

**Licences, honestly.** Sir Hangman is ASP shareware whose `README.TXT`
grants redistribution of the unregistered version; it is the one of the
three that is plainly redistributable. Mozart and Arcade are Microsoft's
commercial titles, long out of print, hosted by archive.org without a
stated licence. None of the three is redistributed by this tree: the user
fetches them.

**Rejected, and why:**

- *Microsoft Multimedia Beethoven* (1992): its data track is **High Sierra**,
  not ISO 9660, which `CDROM.DRV` does not read (§119.3.1 asks for `CD001`),
  and its program is the ToolBook runtime (`TBOOK.EX_`). A second runtime
  and a driver change before the first title runs.
- *Microsoft Multimedia Stravinsky*, *Strauss*, *Schubert*: Mozart's engine
  on larger discs; Mozart's data track is the smallest (147 MB).
- *Living Books* (Broderbund, 1992-): a 256-colour engine on a 16-colour
  desktop.
- *Netscape Navigator 2* and *Acrobat Reader 3*, 16-bit: InstallShield 3, but
  multi-megabyte programs over Winsock, with an evaluation licence
  (Netscape); a target for after this plan.
- archive.org's Windows 3.x Software Library (`softwarelibrary_win3`): its
  items are **installed** trees, run in the browser, not installers.

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

- [x] **S16: Files (R5).**
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
  - **Shipped:** WIN16 **hosts DOS.O88's core** (SPEC.md §124.21.1) - the
    same `build/doscore.bin`, as a lazy part, with a 426-byte host part
    above it in one ~20 KB claim and an 8 KB read window, both fetched at a
    program's first file call. DOS.O88's back end moved into
    `apps/dos/dosbe.inc`, which both hosts include; DOS.O88 and `kern_dos`
    assemble byte-identical. The core's gate is found through a map of the
    same assembly (`tools/w16dcmap.py`), because a jump-table slot took
    `kern_dos` over a kilobyte rung. `int 21h`'s file and directory calls go
    to the core (§124.21.2); KERNEL's `_lopen`, `_lcreat`, `_lread`,
    `_lwrite`, `_llseek`, `_lclose`, `OpenFile`, `GetTempFileName` and the
    `AnsiToOem` family, and USER's `DlgDirList`/`DlgDirSelect` - pulled in
    from S17, being the File Open dialog every target has (§124.21.3). A
    program's DGROUP is laid out at 64 KB when the arena has it, so its
    local heap can grow (§124.21.4): Notepad needed that to start at all.
    The `FILES.EXE` fixture and the `win16file` row.
  - **Measured:** the segment's room is **about 14,885 bytes** (image 31,521,
    bss about 19,130); the library part is 36,523 bytes. A program that
    touches no file pays nothing; one that does pays the ~20 KB DOS and its
    8 KB window.
  - **Seen:** on QEMU, FILES.EXE's message box says `files: 0 failed`, and
    its 10,000-byte `W16FILE.TXT` read off the image on the host is every
    byte it wrote; its `W16DIR` gone again. Notepad 3.0's window and menus,
    and its File Open: `programs.txt` under Files, `[-a-] [-b-] [system]
    [win16]` under Directories, `b:\` beside Directory; OK opens the file.
  - **Not yet:** Notepad then says the file is *too large*: its document is
    the multi-line EDIT's buffer (`EM_SETHANDLE`), S17's, and so is saving
    one. Cardfile is refused at load (`CreateIC`) and Calendar ends at
    start-up on its Windows 2.x bitmap - both GDI, S17. **A handle that has
    grown its file writes only at its end**, the core's own rule (§96.11.2):
    a program that writes a header and seeks back to patch it gets error 5,
    as a DOS program does. Lifting it is a change to the core - and so to
    DOS.O88 and `kern_dos`, whose `KD_IMG_KB` the core's 10 bytes of slack
    sit against - for the owner to decide. `57h` and `5Ah` are not in the
    core and no target issues them; they wait for the installer story.
  - **Not yet run:** `win16file` (registered). It runs in the final pass
    (S25).

- [x] **S17: USER and GDI, the common half (R7).**
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
  - **Batch 1, Notepad - done** (SPEC.md §124.22.1-124.22.4, 124.22.6):
    - [x] scroll bars on any window, outside its client: drawn when the
      frame is owed, pressed, dragged and auto-repeated; `SetScrollPos`,
      `SetScrollRange` (an empty range hides the bar), `GetScrollPos`,
      `GetScrollRange`, `ShowScrollBar`;
    - [x] `ScrollWindow`, through `OSAPI_GFX_SCROLL`, invalidating only
      what it uncovers;
    - [x] the multi-line EDIT, its text in the program's own local heap
      (`EM_GETHANDLE`/`EM_SETHANDLE`), with the keys, the mouse, the
      clipboard, one step of undo and Notepad's `EM_GETTHUMB`;
    - [x] `TabbedTextOut` and `GetTabbedTextExtent`; `Escape` answering 0;
    - [x] what Notepad needed around them (124.22.6): `WM_ACTIVATE`, a
      parent painted before its children, Alt passed up from a control as
      the `WM_SYSCOMMAND` it makes, and the ROM's Ctrl/Alt/Shift scan codes
      folded back to their keys - Ctrl+End did nothing anywhere before;
    - **Seen on QEMU**, VGA and CGA: Notepad 3.0 opens a 61-line file
      through its own File Open, scrolls by its bar's arrow and by Page
      Down with its own thumb following, takes Ctrl+End, Ctrl+Home and
      typing at both ends, and File Save writes the file - the bytes read
      off the floppy image on the host were the edited text exactly.
    - **Not yet seen:** `TabbedTextOut` drawing (Notepad imports it only to
      print; Cardfile, batch 2, draws its list with it - seen there); word
      wrap (pinned for the last batch, moved up after batch 2); `SB_CTL`
      scroll bar controls (shipped in batch 2).
    - **Not yet run:** `win16file`'s Notepad leg, which is written and runs
      in the final pass (S25).
    - What it costs (not yet measured): the EDIT is drawn twice when
      Notepad opens a file - once by its own `UpdateWindow`, once after its
      parent's erase (124.22.6) - which a MartyPC timing in the final pass
      will price.
  - **Batch 2, Calendar and Cardfile - done** (SPEC.md §124.22.7-124.22.11,
    and additions to 124.13, 124.22.1 and 124.22.6):
    - [x] system bitmaps: `LoadBitmap(NULL, OBM_OLD_*)`, WIN16's own art
      in the icon part, made through the same DIB routine as a program's
      bitmap;
    - [x] the SCROLLBAR control (`SB_CTL`), its presses posted to its
      parent;
    - [x] the caret: `CreateCaret` to `GetCaretBlinkTime`, steady, taken off
      around a paint;
    - [x] Calendar's calls (`GetUpdateRect`, `GetActiveWindow`, `AnyPopup`,
      `HiliteMenuItem`, `FlashWindow`, `IsTwoByteCharPrefix`), and a hidden
      window has no pixels;
    - [x] an EDIT's `EN_SETFOCUS`/`EN_KILLFOCUS`, SENT when the program's
      own `SetFocus` raises them - Calendar stores the line it leaves on
      `EN_KILLFOCUS`;
    - [x] Cardfile's calls: `CreateIC`, `IntersectClipRect`, `SaveDC`,
      `RestoreDC`, `GetBitmapBits`, `Catch`, `Throw`, the Comm calls
      refused;
    - [x] activation gives the focus: `DefWindowProc`'s `WM_ACTIVATE`, and
      a program's own window posted `WM_SETFOCUS`/`WM_KILLFOCUS`;
    - [x] a scroll arrow's first repeat after six ticks, not four;
    - [x] **a latent bug since S6**: a one-line EDIT made with no text
      wrote its NUL into WIN16's own code. Calendar was the first program to
      make one.
    - **Seen on QEMU**, VGA and CGA: Calendar 3.0's day view, its day arrows
      stepping one day a click, an appointment typed at 7:00 staying there
      after Enter, the caret moving to 8:00. Cardfile 3.0's card view,
      typing straight into the card, F6's Index dialog, F7 adding a second
      card in sorted order, and List view (`TabbedTextOut`, seen drawing at
      last).
    - **Not yet seen:** a card with a picture (`GetBitmapBits`, saving),
      `Catch`/`Throw` taken, `SaveDC`/`IntersectClipRect` drawing a picture
      (Cardfile reaches them only with a picture in a card); Calendar's
      month view and alarms.
    - [x] **Batch 3, word wrap** (SPEC.md §124.22.3): an EDIT with
      neither `ES_AUTOHSCROLL` nor `WS_HSCROLL` breaks its lines at the
      client's right, after the last space. **Seen on QEMU (VGA)**:
      Cardfile's card, a word pushed down by typing into the line above
      and pulled back up by a deletion; Calendar's notes; Notepad's Edit >
      Word Wrap re-making its EDIT and the text wrapping, and Notepad
      without it still scrolling across.
    - [x] **Two old bugs it uncovered** (SPEC.md §124.22.11): the HWND's
      lookup answered its slot in CX, so `Get`/`SetWindowWord`,
      `Get`/`SetWindowLong` and `SetClassWord` used the slot as the index
      and `GetWindowText` as the count - no subclass ever took. With it
      fixed, Cardfile's subclass of its card paints through `BeginPaint`
      and passes `WM_PAINT` on with the DC, so a control now paints
      inside a DC handed to it in `wParam`.
    - Known and pinned: End at a soft break shows the caret at the start
      of the line below (one index for both places).
    - [x] **Batch 3, the PIF Editor and the Task Manager** (SPEC.md
      §124.22.12): `GetModuleHandle` and `GetProcAddress` on WIN16's own
      modules by name (`W16PROCNAME`), `MapVirtualKey`, `GetCurPID`;
      `GetWindow`, `GetDesktopHwnd`, `WindowFromDC` (and Windows 3.0's
      reading of its ordinal), `SwitchToThisWindow`, and four calls that
      answer 0. A dialog control of a class the program registered is
      made and sent `WM_CREATE` before `WM_INITDIALOG`. `W16_MAXWND` 48 ->
      80. Controls are drawn grey by their own `WS_DISABLED` only.
      **Seen on QEMU (VGA)**: PIF Editor 3.0 in both modes, its Advanced
      Options with the program's own shortcut-key control, Save As writing
      a 545-byte `TEST.PIF`; Task Manager 3.0's Task List, closed by
      Cancel.
    - [x] **Batch 3, the Clipboard viewer** (SPEC.md §124.22.13): the
      viewer chain and `WM_DRAWCLIPBOARD` (posted when this program
      changes the clipboard, and when os8088's clipboard has changed at the
      viewer's next paint), registered formats, `SetViewportOrg` as the
      translation it is in `MM_TEXT`, the mapping mode and extents kept,
      `CreateBitmapIndirect`, palettes and metafiles answering as a display
      with none. A NULL class brush makes `fErase` TRUE. **Seen on QEMU
      (VGA)**: the viewer empty, then showing text copied in Notepad, then
      emptied by its own Delete.
    - **The mapping modes are only partly done**: `MM_TEXT` drawing with a
      viewport origin. Scaling modes are kept and not applied; they move to
      the first program that draws through one.
    - [x] **Batch 4, the COMBOBOX and owner-drawn items** (SPEC.md
      §124.22.14): `CBS_SIMPLE`, `CBS_DROPDOWN` and `CBS_DROPDOWNLIST`, a
      field and a real list box window, the list dropped as a child of the
      top-level window with the pointer captured, the `CBN_` notifications
      and the `CB_` messages passed on as their `LB_` twins.
      `CBS_`/`LBS_OWNERDRAWFIXED` with and without strings:
      `WM_MEASUREITEM` before `WM_INITDIALOG`, and `WM_DRAWITEM` QUEUED by
      a control's paint and sent where the program next asks for a message
      (`GetMessage`, `PeekMessage`, the modal loop), a DC on the control
      clipped to the item.
    - [x] **Batch 4, Character Map** (SPEC.md §124.22.15): `MulDiv`,
      `GetCharWidth`, `EnumFontFamilies` (one family, System),
      `GetTextFace`, `VkKeyScan`, `GetMessagePos`, `GetCursorPos`,
      `WindowFromPoint`, `DrawFocusRect` (solid, not dotted) and
      `GetWindowDC` (the client's). **Seen on QEMU (VGA)**: the grid, the
      owner-drawn font list showing *System*, dropped and picked.
    - [x] **Batch 4, the screen savers' setup dialogs** (SPEC.md
      §124.22.16): `CreateWindowEx`, `SetWindowOrg`, `Polygon` (edges
      only), `GetRasterizerCaps`, two palette calls answering 0, and
      `SetWindowsHook`/`UnhookWindowsHook`/`DefHookProc` as a chain whose
      `WH_MSGFILTER` the modal loop CALLS (the savers' F1). A disabled push
      button's caption, which Windows 3.0's `COLOR_GRAYTEXT` drew into the
      face, is the shadow's grey. **Seen on QEMU (VGA)**: Starfield,
      Marquee (its owner-drawn colour list), Mystify (its shape list
      dropped and picked) and Flying Windows, and Password Protected
      enabling and greying *Set Password*.
    - [x] **Batch 5, the dialog face** (SPEC.md §124.22.17, and changes to
      124.13, 124.22.15 and 124.22.16): a `DS_SETFONT` dialog naming
      `MS Sans Serif` or `Helv` at 8 points is laid out in 7 x 13 units on
      VGA (the dialog face's rows on EGA, Hercules and CGA) and its
      controls draw their text in Wine's MS Sans Serif, emboldened as
      Windows drew it - committed art drafted per adapter by
      `tools/w16font.py dlgdraft` and carried in each System font part,
      after the System face. A combo box's list and field, and the DC an
      owner-drawn control hands its owner, use it too. On a 1bpp display
      a disabled caption is drawn at half density, since both greys round
      to black there.
    - **Seen on QEMU**, VGA: Character Map 618 pixels across with every
      button on the screen, its font list dropped in the dialog face, its
      grid unchanged; Starfield's setup dialog in the dialog face. CGA:
      Character Map and Mystify in the 8-row face, and *Set Password*'s
      caption halved while it is disabled.
    - Known and pinned (SPEC.md §124.22.17): `WM_SETFONT`/`WM_GETFONT`
      carry no font and a program's `SelectObject` of a font changes
      nothing, so text a program draws itself (Character Map's status
      line) is still the System font's. `CreateFont` from the dialog face
      moves to S19 with the font dialogs. The EGA, Hercules and CGA faces
      are derived from the 13-row one and not measured against Windows,
      whose reference machine has VGA only.
    - What batch 5 costs, against batch 4: the image 32,990 bytes (+159),
      its bss 23,997 (+8), 8,549 bytes of room left in the 64 KB; the
      library part 55,642 (+505); the three face parts 3,919, 3,112 and
      2,414 bytes (+1,721, +1,390, +1,168 - the dialog faces).
    - **Not yet run:** `win16fam` (registered), S17's row for the program
      families: each of the eleven targets opened, its title and its client
      checked, Character Map's window inside the screen, and closed, on VGA
      and CGA. It runs in the final pass (S25), with the look on all four
      adapters and Notepad's scroll priced in calls.
    - [x] **WIN16 has its own disk** (SPEC.md §124.18.1, the owner's
      decision of 2026-10-31): `win16.img`, `win16-120.img`,
      `win16-720.img`, `win16-360.img`. WIN16.O88 had outgrown the 1.44MB
      apps disk (3 clusters) and `more720.img` (4), and it grows with each
      story; the apps disks and `more720.img` no longer carry it. The
      network-install trees take it from the new disk
      (`tools/os88nettree.py` now merges any number of disks); INSTALL's
      floppy path does not ask for it, which is pinned (SPEC.md
      §124.18.1).
    - **Open, not investigated:** a THIRD WIN16 instance (Clock and
      Calculator running, QEMU's 640 KB) does not start, and nothing says
      so - neither a window nor a refusal. Two at once is SPEC.md
      §124.18.2's case; the third wants measuring and its own refusal.
    - Known and pinned (SPEC.md §124.22.14-124.22.16): no `CB_DIR`;
      `CB_FINDSTRINGEXACT` is a prefix find; no `WM_COMPAREITEM` or
      `WM_DELETEITEM`; no `WM_MEASUREITEM` for a combo box made by
      `CreateWindow`; owner-draw entries past `W16_ODQN` (8) dropped until
      the next paint; paint under a dropped list waits until it goes up; the
      list is cut to its top-level window; only the System font is
      enumerated; hooks other than `WH_MSGFILTER` are kept and not called;
      Marquee's Format Text is `COMMDLG.ChooseFont`, S19's.
    - Known and pinned: the PIF Editor's Advanced Options is taller than
      os8088's desktop, and its last row (the shortcut key) is below the
      screen's edge. Its template names `Helv` 8, so batch 5's dialog face
      should make it shorter; that has not been looked at. The window
      records live in WIN16's 64 KB data segment (102 bytes each), so the
      next rise of `W16_MAXWND` should move them out of it rather than
      spend more of it: 9,762 bytes of room were left then.
    - **Found and left:** Calendar and Cardfile at once on CGA - the second
      is refused, "WIN16's library part could not be read", because each
      instance holds its own image, bss and library part. One instance
      running several programs is S21.
    - What it costs: the image 32,237 bytes (+281), its bss 20,251 (+170),
      13,048 bytes of room left in the 64 KB; the library part 46,325
      (+1,791).
    - What batch 4 costs, against batch 3's 32,454 / 23,798 / 48,543: the
      image 32,831 bytes (+377), its bss 23,989 (+191 - the owner-draw
      queue, the combo boxes' saved create arguments, the hooks), 8,716
      bytes of room left in the 64 KB (9,284 before); the library part
      55,137 (+6,594).

- [x] **S18: Libraries (R4, the program's own).**
  - *As a user, Recorder runs with its RECORDER.DLL, and Paintbrush with
    PBRUSH.DLL.* Paintbrush's half moved to S19b (below): its library loads
    and runs, and what stops it is GDI's drawing half, not a library.
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
  - **Done** (SPEC.md §124.23): a library's DGROUP, code and module block
    in the global heap; implicit imports bound at load by ordinal and by
    name (resident, then non-resident names); `LibMain` through fixed slot
    4 before the program starts; `LoadLibrary`, `FreeLibrary` (with `WEP`),
    `GetProcAddress`, `GetModuleHandle`, `GetModuleFileName` and
    `GetModuleUsage`; up to 8 libraries, 4 deep. `tools/w16disk.py` carries
    a program's libraries onto `make win16disk`'s floppies.
  - **Seen on QEMU's VGA:**
    - `DLLUSE.EXE` reports **DLLUSE OK**: LibMain first, imports by name and
      by ordinal, `GetProcAddress` both ways, the shared counter, a library
      importing another, `WEP` on `FreeLibrary`, and 2 for a missing file.
    - Recorder 3.0 with `RECORDER.DLL`: its window with File, Macro, Options
      and Help; Macro's greyed items; Record… opens its dialog. Two
      defects found on the way and fixed: a 16-character class name was
      refused, and a hidden first window took WIN16's window and its menu
      bar from the real one (§124.23.8).
    - Paintbrush 3.0 with `PBRUSH.DLL`: 23 of its 35 missing imports added
      (§124.23.9). Its window opens with all nine menus and its toolbox,
      then *Insufficient memory for this operation* and it ends cleanly.
      (S18 put that down to a 153,600-byte bitmap. S19b batch 1 measured it:
      the cause was WIN16's eight claims, §124.25.1.)
  - **Fixed, older than this story:** `GetMenuString` put DS back from CS in
    the library part, where CS is the part's segment; after it, WIN16's DS
    accesses went into its code. Paintbrush's MessageBox did not paint, the
    launching DOS window stayed, and a keystroke rebooted the machine.
    `t_w16xsec` rule 6 now refuses the pattern.
  - **Not yet run:** `win16dll` (registered). It runs in the final pass
    (S25).
  - **Pinned** (§124.23.6): a library's own resources; `WEP` at program
    end; a DGROUP that grows; sharing a library between two programs (S21).
    A hidden helper window shown later stays undrawn (§124.23.8).
  - What it cost, against S17 batch 5: the image 33,810 bytes (+820), its
    bss 24,686 (+689), **6,528 bytes of room left in the 64 KB** (8,549
    before); the library part 57,714 (+2,072); the loader part 11,260
    (+2,257). The room is now the tightest it has been: S19 should put what
    it can in parts.

- [x] **S19: The system libraries WIN16 provides (R4, the system's own).**
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
  - [x] **Batch 1: the system-library part, COMMDLG's file dialogs, the
    "not available" answers** (SPEC.md §124.24-124.24.3).
    - Part 9, `section .w16sys` (`apps/win16/w16syslib.inc`): fetched at a
      system library's first call, dropped when the program ends; `W16SYS`,
      `SCALL`, `SCONT`, `SLCALL`; `t_w16xsec` knows it (rows say part 9).
    - COMMDLG is WIN16's module 7: `GetOpenFileName`, `GetSaveFileName`
      over §38's chooser, the program waiting in the call; `GetFileTitle`;
      `CommDlgExtendedError`; `PrintDlg` (no default printer), `FindText`,
      `ReplaceText` (no dialog). SHELL's drag calls: nothing dropped.
    - **Cancel is seen, not told**: the chooser's completion is called only
      for a file, so WIN16's tick looks for the chooser no longer frontmost
      while a call waits on it. No kernel byte and no SDK change.
    - **Seen on QEMU's VGA, Notepad 3.1:** File › Open opens the os8088
      chooser in its Open form; choosing PROGRAMS.TXT answers its full path
      and Notepad reads and shows it. Cancel answers FALSE and
      `CommDlgExtendedError` 0, and Notepad carries on. Save As offers
      PROGRAMS.TXT; typing `COPY` saved **COPY.TXT** (`lpstrDefExt`), read
      back off the disk image byte-identical. Print is greyed, as on a
      Windows with no printer. Calendar, Clipboard, PIF Editor and Recorder
      3.1 start.
    - **Not yet run:** `win16cdlg` (to be registered with batch 2). It runs
      in the final pass.
    - **Pinned** (§124.24.2): the filter, initial directory and title are
      not used; a hook or custom template is not honoured.
    - What it costs, against S18: the image 34,008 bytes (+198), its bss
      24,698 (+12), 6,318 bytes of room; part 9 642 bytes; the library part
      unchanged at 57,714.

  - [x] **Batch 2: ChooseFont, ChooseColor and a font a program makes**
    (SPEC.md §124.24.4, §124.24.5).
    - `ChooseFont` and `ChooseColor` are WIN16 dialogs built in part 9:
      a list (WIN16's two faces; the sixteen colours by name), OK and
      Cancel, run in the dialog manager's modal loop with `WDF_CHOOSE`,
      the structure filled on OK before the dialog ends.
    - `CreateFont`/`CreateFontIndirect` map onto the two faces (MS Sans
      Serif or Helv by name, or the Swiss family: the dialog face, one
      shared handle `W16_HFDLG`). Nine text calls carry `W16B_DCF` in
      their export row, and the gate draws them in the face of the DC's
      font; `t_w16xsec` holds the flag to `w16api.inc`'s list. `GetObject`
      on a font answers its `LOGFONT`.
    - **Fixed on the way:** `w16_pfetch` kept BX and DX but not ES, SI,
      CX and DI. An SLCALL that was the first to fetch the library part
      lost ES:SI.
    - **Seen on QEMU's VGA:** Clock 3.1's Set Font (MS Sans Serif mapped
      from its own font; choosing System redraws the time in System);
      Marquee 3.1's Format Text (System from its Times New Roman; a
      double-click on MS Sans Serif is OK); CDLG.EXE's message box
      `font 1 MS Sans Serif 80; colour 1 0000FF; face MS Sans Serif 13`.
      The font dialog seen on CGA.
    - **Registered, not yet run:** `win16cdlg` (CDLG.EXE, Notepad's
      Cancel, Marquee's Format Text). It runs in the final pass.
    - **Pinned** (§124.24.4): every size is the one strike; the enumerations
      still name System alone; no sample, no style or size lists.
    - What it costs, against batch 1: the image 34,087 bytes (+79), its bss
      24,703 (+5), 6,234 bytes of room; the library part 58,068 (+354);
      part 9 1,783 (+1,141).

  - [x] **Batch 3: the registration database, and OLE answering "not
    available"** (SPEC.md §124.24.6, §124.24.7).
    - SHELL's seven Reg calls keep `REG.DAT` in the Windows directory as a
      profile file: one `[HKEY_CLASSES_ROOT]` section, a `name=value` line
      a key, held, merged and written by §124.14's machinery. The root and
      eight open keys, held in part 9.
    - OLECLI (module 8) and OLESVR (module 9): a row for every function in
      their `.spec` tables, the eight stubs sized from the 3.1 SDK's
      `ole.h`. Eight answers: document calls `OLE_OK`, clipboard queries
      `OLE_ERROR_FORMAT`, object-making calls `OLE_ERROR_CLASS`, calls on
      an object `OLE_ERROR_OBJECT`, the server registrations `OLE_OK` with
      a handle.
    - **Seen on QEMU's VGA:** REGDB.EXE (new fixture) says `REGDB OK`, and
      `REG.DAT` read off the floppy after it holds `.w16=w16file` and
      `w16file=W16 Document`, what its delete left. Cardfile 3.1 runs, its
      Edit menu with Paste Link, Paste Special and Insert Object greyed.
    - **Registered, not yet run:** `win16reg`.
    - **Not done:** `ShellExecute` and `FindExecutable`.
    - What it costs, against batch 2: the image 34,101 bytes (+14, the two
      module names), bss unchanged, 6,220 bytes of room; the loader part
      12,034 (+657, 73 new export rows); part 9 4,214 (+2,431, of which
      1,024 is the open keys' table).

  - [x] **Batch 4: LZEXPAND and VER, the installer's core** (SPEC.md
    §124.24.8, §124.24.9).
    - LZEXPAND (module 10) expands SZDD files as they are read: `LZInit`,
      `LZOpenFile` (with the `_` name when the plain one is not there),
      `LZRead`, `LZSeek` in the expansion (back before the position expands
      again from the start), `LZCopy`, `GetExpandedName`. Sixteen handles,
      each a 5,152-byte fixed block in the program's own heap.
    - VER (module 11): `GetFileResourceSize`/`GetFileResource` over an NE's
      resource table, by number or name; `GetFileVersionInfoSize`/
      `GetFileVersionInfo`; `VerQueryValue`; `VerLanguageName` with the
      table Windows 3.1's own `VER.DLL` answers (read off the reference
      machine); `VerFindFile`; `VerInstallFile`, which copies through the
      expander. Every file VER reads is opened as `LZOpenFile` opens it, so
      a packed `.DL_` answers its version.
    - **Seen on QEMU's VGA:** LZT.EXE (new fixture, its version in
      `tests/win16/lzt.rc`) says `LZT OK 24000`. `SAMPLE.TXT`, copied out
      by `LZCopy`, and `SECOND.DAT`, installed by `VerInstallFile` into the
      system directory, read off the guest's floppy, are the host's
      expansions byte for byte. With Windows 3.1's own `VER.DL_` (packed
      by Microsoft's `COMPRESS.EXE`) in `SECOND.DA_`'s place, the 9,008
      bytes `VerInstallFile` wrote were `tools/w16font.py`'s expansion
      exactly. That was a look; no Microsoft file is in the row.
    - **Found on the way:** DOS's seek answers the position in DX:AX, so
      the reader's own seek clobbered the file position it was walking the
      resource table by. A packed file never showed it (its seek is the
      expander's), and the plain one answered "no version".
    - **Registered, not yet run:** `win16lz`.
    - **Pinned** (§124.24.9): `VerInstallFile` compares no versions and
      writes no temporary file; no KWAJ; no PE version resource.
    - What it costs, against batch 3: the image 34,114 bytes (+13, the two
      module names), bss unchanged, 6,207 bytes of room; the loader part
      12,196 (+162, 18 export rows); part 9 9,335 (+5,121, of which 624 is
      the language table and 496 the buffers and words).

  - [x] **Batch 5: ShellExecute and FindExecutable** (SPEC.md §124.24.10).
    - `ShellExecute` opens a document as a Disk-window double-click does:
      the file is checked, its locator (name, folder cluster, volume, asked
      of the instance while WIN16's DOS stands in the folder) is written
      into WIN16's image, and the launch is posted to `w16_wake`, where
      `OSAPI_PKG_START` takes the association route. Verb `open` or NULL
      only; one launch waits at a time (8 for a second); success is 33.
    - `FindExecutable` answers a program's own path; for anything else it
      answers 31, because os8088's associations name packages, not Windows
      programs.
    - **Seen on QEMU's VGA:** SHX.EXE (new fixture) says `SHX OK`, and
      Note Pad opens `DOCS\SHX.TXT`, the folder's file and not the root's.
    - **Found on the way:** a document on another drive moved that drive's
      folder but not the DOS, so `OSAPI_FILE_HERE` would have named the
      wrong folder; WIN16's DOS now selects the document's drive around the
      question.
    - **Seen, and pinned, not fixed:** a program whose FIRST act is
      `ShellExecute` on another Windows `.EXE` got a second DOS.O88 that
      handed off, but no second WIN16, and both DOS windows stayed. After
      a `MessageBox` first, the same call ran the second WIN16 with no DOS
      window left. The one-word deferred close (§75.2) is the likely cause
      and is not proven; S24's installer, which launches what it installs,
      settles it.
    - **Registered, not yet run:** `win16shx`.
    - What it costs, against batch 4: the image 34,130 bytes (+16), bss
      24,720 (+17, the locator and its flag), 6,174 bytes of room; the
      loader part 12,214 (+18, 2 export rows); part 9 10,333 (+998).
    - **S19 is complete.** Its rows, `win16cdlg`, `win16reg`, `win16lz` and
      `win16shx`, run in S25's final pass.

- [x] **S19b: GDI, the drawing half (R7).**
  - *As a user, Paintbrush opens, and I draw with its tools.* (Moved from
    S18: Paintbrush's library works; its drawing does not.)
  - Drawing **into a memory DC**: lines, rectangles, ellipses, polygons,
    text and `BitBlt`/`StretchBlt` between bitmaps, with the ROPs Paintbrush
    uses - the same primitives the window DC has, against a bitmap's claim
    instead of the screen.
  - **Bitmaps over 64,000 bytes**: not needed by Paintbrush, measured in
    batch 1. Its bitmap manager (`PBRUSH.DLL`) keeps the picture in bands of
    at most 32 KB and pages them to a temporary file; it asks for no bitmap
    over 8,960 bytes. Left for a program that needs one.
  - The imports Paintbrush still lacks (§124.23.9): `SetBitmapBits`,
    `GetDIBits`, `SetDIBits`, `ExtFloodFill`, `CreateRectRgn`,
    `CreatePolygonRgn`, `SelectClipRgn`, `ExcludeClipRect`,
    `CreateHatchBrush`.
  - **Targets:** PBRUSH 3.0 with PBRUSH.DLL, then 3.1's.
  - **Verification:** a row that draws with each tool into a fixture's
    memory DC and compares the bitmap with the host's expected pixels, and
    Paintbrush opened, drawn in and saved.
  - [x] **Batch 1: WIN16 within its eight claims, and a library's heap with
    room** (SPEC.md §124.25.1, §124.25.2).
    - **Found:** Paintbrush stopped on the eight claims an owner may hold,
      not on a bitmap. A temporary probe logged its 65 bitmap requests (none
      over 8,960 bytes, none refused); a call trace showed `OpenFile`
      creating its temporary file fail with DOS error 8; the heap, frozen at
      that moment, had 145 KB free and WIN16 holding seven claims. The DOS's
      block was the eighth and fetching its core the ninth.
    - WIN16 keeps the program, the library part and at most three arenas
      (32 KB, 64 KB, then the largest free run less 32 KB) as claims.
      Profiles, the DOS and its window, and the System font's, icon,
      system-library and emulator parts are blocks of the global heap
      (`w16_kclaim`, `w16_pkeep`), aligned to 512 bytes.
    - Then `PBRUSH.DLL`'s 1 KB local heap ran out for its band table. A
      library with a heap is given 4 KB more of it, as Windows' growable
      heap would have.
    - **Seen on QEMU's VGA:** Paintbrush 3.0 opens with its toolbox,
      palette, scroll bars and a white canvas, and waits for input; WIN16
      holds four claims. A stroke draws nothing yet (batch 2). REGDB and
      LZT, which lean on profiles, the DOS and LZEXPAND, still say
      `REGDB OK` and `LZT OK 24000`.
    - **Rows that cover it, for S25's pass:** every `win16*` row, since
      every program takes its face, DOS and profiles from the heap now;
      `win16fpu` for the emulator part; `win16dll` for the library heap.
    - What it costs, against S19 batch 5: the image 34,353 bytes (+223),
      bss 24,708 (-12), 5,963 bytes of room; the library part 58,129 (+61),
      the loader part 12,227 (+13).
  - [x] **Batch 2: drawing into a memory DC, and what Paintbrush needed
    around it** (SPEC.md §124.25.3-§124.25.6, §124.8, §124.9).
    - A memory DC's state is a record in the open DC's layout; a GDI row
      flagged `W16B_MDC` swaps it in around the handler, and under the call
      the primitives write the bitmap (`w16_mfill`, `w16_mband`,
      `w16_mblit4`). `BitBlt`/`PatBlt` into one take its brush, origin and
      clip; one as a SOURCE has its origin applied.
    - `Polygon` fills its inside under `ALTERNATE`/`WINDING`.
    - A DC open across a wait is parked - the gfx lock and clip go back and
      are taken again when the program runs - or Paintbrush's stroke loop,
      which holds `GetDC` through `PeekMessage`, waited forever.
    - The nine imports: `SetBitmapBits`, `SetDIBits`, `GetDIBits`,
      `ExtFloodFill`, `CreateRectRgn`, `CreatePolygonRgn`, `SelectClipRgn`,
      `ExcludeClipRect`, `CreateHatchBrush` (a solid brush).
    - **Found driving Paintbrush:**
      - After a stroke no tool or colour could be picked: the canvas takes
        the capture on a press and lets it go on a `WM_MOUSEMOVE` off the
        canvas with no button down, and os8088 reports no such move. WIN16's
        tick now reads the pointer while a move can be the program's and
        posts them (`w16_hover`); consecutive moves to one window merge;
        `GetCursorPos` reads the pointer.
      - The roller is `ExtFloodFill` on the WINDOW's DC. It reads the
        screen back into a one-bit mask in an os8088 claim of its own: a
        4bpp copy of the canvas (50 KB) could not be had.
      - The heap still ran out after a fill: Paintbrush keeps its picture
        in `GMEM_DISCARDABLE` strips. WIN16 now discards those, least
        recently used first - but not a `GMEM_NOTIFY` one: Paintbrush locks
        a strip only its notify procedure would have told it was gone,
        without a NULL check, and the first cut wrote a strip over the
        interrupt vectors. Paintbrush handles a refused `GlobalAlloc`
        itself, by paging a strip out.
    - **Seen on QEMU's VGA:** strokes that stay through a repaint, a tool
      and a colour picked after a stroke, a box, and the roller filling
      inside it and across the whole canvas, three fills running; MEMDC.EXE
      (`tests/win16/memdc.c`) says `MEMDC OK`.
    - **Not yet** (at batch 2): wide pens (1 pixel, §124.8.1), the text
      tool, File Save and Open, 3.1's Paintbrush.
    - **Rows, for S25's pass:** `win16memdc` and `win16pbrush` (new), and
      every `win16*` row, since every GDI call goes through the memory-DC
      test and every wait parks its DC.
    - What it costs, against batch 1: the image 35,565 bytes (+1,212), bss
      25,480 (+772), 3,979 bytes of room; the library part 59,287 (+1,158),
      the loader part 12,308 (+81, 9 export rows), part 9 12,725 (+2,392
      against S19 batch 5). The trace build's ring is 1,024 rows now: 2,048
      no longer fits.
  - [x] **Batch 3: wide pens, the text tool, File Save and File Open**
    (SPEC.md §124.9.1, §124.25.7-§124.25.9, §96.11.6.4).
    - **Wide pens:** a line paints the pen's width across its major axis;
      a closed figure's frame sits outside the box, or inside it under
      `PS_INSIDEFRAME`; a round rectangle's frame is the band between two
      outlines.
    - **The text tool:** `CreateCompatibleBitmap` refused Paintbrush's
      canvas-sized bitmap, so it made no caret. Then it held five memory
      DCs where WIN16 had four (now 8). Then it draws with source ROPs under
      a solid brush (a table per call, each plane's half picked by the
      brush's bit), and with pattern brushes made from one-colour bitmaps,
      which are now that colour's solid brush. A queued `WM_SIZE` is now
      superseded by a newer one: the canvas's own 0 x 0 arrived last and
      emptied it.
    - **File Save:** DOS.O88 refused a created handle's write behind its
      end, and Paintbrush writes the bitmap's body and then its header at
      0. The write is now made by flushing first and overwriting in place
      (§96.11.6.4; WRGAP.COM's step H).
    - **File Open** took four fixes, found one after another:
      - the handle table: 64 moveable handles ran out on PBRUSH.DLL's
        band cache, so it is now 256;
      - `GMEM_NOTIFY`: the band cache asks its `GlobalNotify` procedure
        (DISCARDBAND) to page a band out, and WIN16 now calls it when
        `GlobalAlloc` is short, as Windows did (§124.9.1). The first cut
        passed the wrong handle, because the callback setup clobbers DI, and
        overwrote the interrupt table;
      - `DIB_PAL_COLORS`: logical palettes, so `SetDIBits` can name
        colours by palette index (§124.25.8). This was the second
        `Insufficient memory`;
      - odd handles: PBRUSH.DLL marks its own virtual DCs and bitmaps with
        bit 0, so WIN16's slot 1 (`3011h`, `3305h`) was taken for one of
        them. The picture opened blank and saved black. Every DC, bitmap
        and palette handle is even now (§124.25.9).
    - **Found in a test helper:** `Prog.waiting_msgbox` compared a BIND
      SLOT with an export ROW, and has done since S14 (§124.19.1). It now
      compares the slot's handler (`win16fpu.serves`), and `win16cdlg` and
      `win16hiber` use the same helper.
    - **Seen on QEMU's VGA:** typed text in green; File > Open of a
      640 x 442 16-colour BMP with 0 of 100,864 visible canvas pixels
      differing from the file; that picture saved again (Save As) with
      every pixel the same; a picture drawn from new saved. MEMDC.EXE's new
      checks (even handles, palettes, a notify procedure asked about the
      oldest block, 200 handles) say `MEMDC OK`.
    - **Not yet:** brushes, pens and stock objects are still numbered by
      slot and can be odd (no program seen tests them); one selected
      palette per program, not one per DC; 3.1's Paintbrush.
    - **Rows, for S25's pass:** `win16pbrush` (now with Save As, the file
      read back off B:), `win16memdc`, `dosgap` (step H), `win16cdlg`,
      `win16hiber` and every row through `waiting_msgbox`, and the `dos*`
      rows for `.fwback`.
    - What it costs, against batch 2: the image 35,986 bytes (+421), bss
      27,207 (+1,727: 192 more handles, 4 more memory DCs, the palettes),
      **1,831 bytes of room**; the library part 59,821 (+534), the loader
      part 12,308 (+0), part 9 13,671 (+946). The room is the next
      constraint: S20's segment cache needs bss of its own, so S20 should
      start by moving tables out of the image's bss.
  - [x] **Batch 4: Windows 3.1's Paintbrush** (SPEC.md §124.25.10,
    §124.9.1, §124.24.2).
    - Eight imports: `AddAtom`/`GlobalAddAtom` in the clipboard-format
      table (Windows' formats are global atoms), `DeleteAtom`/
      `GlobalDeleteAtom`, `GetMessageExtraInfo`, `CreateMetaFile`/
      `CloseMetaFile` (NULL), `GetCharABCWidths` (FALSE, not TrueType),
      `StretchDIBits` (0, not seen called).
    - **Found:** after a fill, the text tool's canvas-sized
      `CreateCompatibleBitmap` failed, because the bands filled the heap and
      only `GlobalAlloc` asked the notify procedure. Bitmap creation now
      asks too, and the continuation re-dispatches whichever call ran short.
      3.0's Paintbrush has the same path.
    - **Found:** File > Open opened in WIN16's temp folder, where the swap
      file is: a handle's write leaves the machine in that file's folder
      (§96.52). The dialog now asks DOS for `AH=47h` first, which stands
      the machine in the program's own folder.
    - **Seen on QEMU's VGA:** it opens, draws a box, fills inside and
      around it, types text after the fills, and at 320 x 200 pixels saves
      a BMP whose pixels are right and opens it again after File > New.
    - **Not yet, moved to S20:** a full-screen picture's save needs one
      141,440-byte block, and on 640 KB the largest free run beside its
      150 KB of code is about 98 KB. S20's discardable code is what frees
      it. Not registered as a row: 3.1's Paintbrush is only looked at.
    - `tools/w16reach.py --missing` no longer crashes on an import by name
      (a program's own library's).
    - **Rows, for S25's pass:** `win16memdc`, `win16pbrush` (the retry
      path), `win16cdlg` and every row that opens the file dialog.
    - What it costs, against batch 3: the image unchanged (35,986 bytes,
      1,831 of room); the library part 59,866 (+45), the loader part 12,389
      (+81, 9 export rows), part 9 13,694 (+23).
    - **S19b is complete.** Its rows run in S25's final pass.

- [x] **S20: Big programs in small memory (R6).**
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
    WINFILE 3.0, CONTROL 3.0, and Windows 3.1's Paintbrush saving a
    full-screen picture, which needs one 141,440-byte block (S19b batch 4).
    **Re-planned at batch 7:** WINFILE needs MDI, which is S20b; the
    full-screen save cannot fit on 640 KB at all and is S22's.
  - **Verification:** a `win16write` row opens a document under a cache
    deliberately smaller than the program, checks that segments were
    discarded and reloaded, and checks that the document is intact; the XT's
    cost per reload on MartyPC.
  - [x] **Batch 1: discardable code** (SPEC.md §124.26).
    - A discardable code segment is left out of the program's claim. It is
      loaded into a fixed block of the global heap, taken from the top of an
      arena, at launch if it holds the entry point or is PRELOAD, and
      otherwise when one of its entries is first called.
    - **An 8-byte thunk an entry ordinal**: `jmp far` the entry while the
      segment is loaded, `call far w16_thkhit` while it is not. Every
      relocation to such an entry is given its thunk, so far calls,
      callbacks and linked far pointers all reach the code through it. A
      hit enters the gate as fixed slot 5, `w16_segld`, which loads the
      segment and resumes the call at the entry with AX and DX intact.
    - **The plan's `INC BP` walk was not built.** A discard scans the
      program's stack instead, from the waiting frame to the SP it started
      with, and keeps any segment whose paragraph is a word there. Nothing
      is patched on the stack, and it finds what a walk cannot: the C
      runtime's frames without the mark (9 of Write's 409 entries), the
      gate's own frame, and a thunk's return while its segment loads.
      `Catch` needs nothing (§124.26.5).
    - **The plan's code-cache budget taken at launch was not built either.**
      Code and data share the global heap's least-recently-used rule, as on
      Windows. A test build sets one (`W16DEFS=-DW16_CODEKB=n`, a new
      stamped Makefile knob), and that is how the row will make Write
      discard.
    - The loader part is **kept** after launch (14.4 KB, with its bind
      table) by a program with discardable code, and `LoadLibrary` uses
      that copy. A module block holds the NE tables, relocation room, a
      live table and the thunks: Write's is 8.3 KB.
    - **The bss move the room called for was not needed:** S20 took 31
      bytes of bss.
    - **Found:** `GetFreeSpace` answered the largest truly free run, and
      with Paintbrush 3.0's 57 KB of loaded code in the arenas that was
      9 KB: it stopped at *Insufficient memory to edit image*.
      `GlobalCompact` and `GetFreeSpace` now count code that may be
      discarded. Paintbrush 3.0 opens and draws again.
    - **Found:** the trace build no longer fit in 64 KB. `W16_TRN` is now a
      define, so a trace build is `-DW16_TRACE -DW16_TRN=256`.
    - **Seen on QEMU's VGA:** Notepad 3.0 with a 16 KB and then a 4 KB code
      budget: typing, the Save prompt, File Open, Find and About. Segments
      were discarded, one in use was passed over, and segments 5 and 6 were
      loaded again after being discarded, all drawn right. On the plain
      build, CALC, both Solitaires, Cardfile, Reversi, Calendar, DLLUSE and
      both Paintbrushes reach their windows; 3.1's Paintbrush runs with 144
      thunks loaded and 101 discarded. Write still stops at *Needs GDI.10*,
      which is batch 2.
    - **Not seen:** the 8088 census path, where every discardable segment
      loads at launch. Rows `win16cpuxt` and `win16fpu` run it in S25.
    - **Rows, for S25's pass:** every `win16*` row, because every program
      now loads through this path; `win16dll` (the kept loader under
      `LoadLibrary`), `win16cpuxt` and `win16fpu` (the census),
      `win16pbrush` (GetFreeSpace), and `w16xsec` and `w16cens` in the fast
      tier.
    - What it costs, against S19b: the image 36,193 bytes (+207), bss 27,238
      (+31), **1,593 bytes of room**; the library part 59,941 (+75), the
      loader part 14,412 (+2,023), part 9 unchanged.
  - [x] **Batch 2: Write's calls** (SPEC.md §124.26.7, §124.11).
    - Five imports: `SetTextJustification` (TRUE, not applied),
      `GetBitmapDimension` (0), `GetMessageTime` (the last message's time),
      `InSendMessage` (FALSE) and `EnumChildWindows`, which shares
      `EnumWindows`' callback walk, with bit 15 of the continuation word
      saying which.
    - **Found:** Alt+letter did nothing in Write. Its keys go to its
      document window, a child, and `SC_KEYMENU` started the menu loop on
      that child, which has no menu. It now starts on the top-level window.
    - **Seen on QEMU's VGA:** Write 3.0 opens with its menus (by mouse and
      by Alt+letter), types, and opens Windows 3.1's `PRINTERS.WRI` through
      its own Open dialog. It lays out the headings and paragraphs and pages
      through them. With `W16_CODEKB=40` it does the same while its segments
      are discarded and loaded.
    - **Found, for batch 3:** all four of Write's window classes are
      `CS_OWNDC`. WIN16 has one window-DC handle, so Write's drawing for its
      document went through the main window's DC: its text and Page Down's
      erase ran over the page number and the horizontal scroll bar.
    - Still seen: the end mark draws as `?`, because the faces stop at 7Eh
      (§124.7.1); a justified paragraph would be ragged.
    - **Rows, for S25's pass:** `win16file` and `win16cdlg` (Alt+letter
      through `DefWindowProc`), `win16reg` and every row whose program
      calls `EnumWindows`.
    - Sizes: the image 36,196 (+3), bss unchanged, **1,590 bytes of room**;
      the library part 60,027 (+86), the loader part 14,457 (+45, 5 rows).
  - [x] **Batch 3: own DCs** (SPEC.md §124.25.11, §124.11, §124.22.6).
    - A `CS_OWNDC` window (or `CS_CLASSDC`, taken as it) gets a DC of its
      own: a handle of its own, `W16_ODC0` + 2 *i*, and its state kept in a
      record between uses. Up to four, in a global-heap block made with the
      first, so they cost the image's bss 6 bytes.
    - The DC a call names is made the open DC first: at dispatch for the
      `W16B_MDC` rows, and by an explicit call in the blit core, `SaveDC`,
      `RestoreDC`, `GetPixel`, `DrawIcon`, `SelectObject` and
      `WindowFromDC`. The displaced DC goes back to its record, or onto the
      stack under it (the shared DC's), and the named one is armed as a
      parked DC is unparked. `BeginPaint`, `EndPaint`, `GetDC` and
      `ReleaseDC` answer and take the own handle.
    - **Found:** batch 2's Alt+letter fix opened Write's menu, but
      `WM_INITMENU` and the chosen `WM_COMMAND` still went to the document
      child, so Alt+F, O did nothing. They go to the top-level window now,
      and Write greys its own items on `WM_INITMENU` (Print).
    - **Found:** after typing, Alt+F still did nothing. Write sits in a
      `PeekMessage(PM_NOREMOVE)` + `int 21h 2Ch` loop after a keystroke, so
      WIN16 took the key from os8088 after Alt was let go and read the shift
      state then. A letter or digit with no character from the ROM is now
      taken as pressed with Alt.
    - **Seen on QEMU's VGA:** Write's text is clipped to its document
      window, Page Down no longer wipes the page number or the scroll bar,
      and the page number is white on black, as on Windows 3.0's own Write
      (`win16ref/ref/w30-write.png`, taken this batch). Alt+F, O opens the
      Save prompt and the File Open dialog. CALC, Solitaire, Cardfile,
      Clock, Calendar, MEMDC, Notepad, Character Map and Paintbrush 3.0
      look as they did, and a Paintbrush stroke matches the last build's.
    - **Rows, for S25's pass:** every row that paints, since `BeginPaint`
      and `w16_dcopen` changed: all `win16*` rows, and `win16memdc` and
      `win16pbrush` above all.
    - Sizes: the image 36,387 (+191), bss 27,244 (+6), **1,393 bytes of
      room**; the library part 60,771 (+744).
  - [x] **Batch 4: Windows Help's, Terminal's and Control Panel's calls**
    (SPEC.md §124.26.8, §124.21.1, §124.9).
    - 31 imports these three programs lacked, from `GetTextColor` to
      `SetDeskWallPaper`. Regions are rectangles (`CombineRgn`,
      `PtInRegion`, `FillRgn`, `GetClipBox`). `GrayString` calls the
      program's output function when there is one, and `ChildWindowFromPoint`
      walks the parent's children. The Control Panel's settings calls
      answer and change nothing, because those settings belong to
      os8088's own Control Panel.
    - `AccessResource` opens the program's own file through WIN16's DOS
      and seeks to the resource. The DOS host records the program's path
      the first time it is fetched, from its own `47h`, because a bare name
      stops naming the file once the program changes folder.
    - **Found, and the batch's real fix:** `GlobalAlloc` treated every
      handle at or above `W16_GH0` as a moveable entry. A fixed handle is
      its segment, which is always above that value. So every fixed
      `GlobalAlloc` wrote an age word at `w16_ghage` + (segment − 100h),
      which lands inside WIN16's own image. Which byte it hit depended on
      where the block landed. Windows Help lost the `jae` in `w16_gcut`,
      and from there the heap and then the CPU: the free-block header
      written into its live code, and the endless `#UD` at 4c3e:0b74 that
      batch 1's notes saw. It took a gdb single-step with a watch on the
      changed word, run from the import before the one that broke. The
      range test is now the one `w16_gfind` makes.
    - **Seen on QEMU's VGA:** Windows Help 3.0 opens its window with the
      menus, the button bar and the logo bitmap. Terminal 3.0 opens with
      its menus and its *Default Serial Port* dialog.
    - **Still seen:**
      - Windows Help says *Cannot create bookmark file*, and its buttons
        have no captions.
      - Terminal says *Emulation selected is not available*. That comes
        from `FindResource(1003, "CCTL")`, a type named by string, while
        Terminal's NE table holds the type as the integer `F008h`. Wine's
        lookup would not match these either, so the reference machine
        decides what Windows does here before WIN16 changes.
      - Control Panel and File Manager are not yet looked at.
    - **Open:** a 256-entry trace build no longer runs through DOS.O88's
      hand-off: it overflows task 0's stack in a DOS-and-WIN16 recursion
      when WIN16 cannot load. A trace build now uses `-DW16_TRN=128`.
      Whether a low-memory machine can reach the same recursion is not
      known. Task 0's deepest use while Windows Help starts is 404 of
      512 bytes.
    - **Rows, for S25's pass:** every `win16*` row, because `GlobalAlloc`
      is on every program's path; `win16pbrush` and `win16write` above
      all, as the heaviest allocators.
    - Sizes: the image 36,407 (+20), bss unchanged, **1,373 bytes of
      room**; the library part 61,402 (+631), part 9 14,578, the DOS host
      514 of its 1,024.
  - [x] **Batch 5: the system's parameters** (SPEC.md §124.26.9).
    - `SystemParametersInfo` (USER.483, a new row) and `ControlPanelInfo`
      (USER.273, which answered 0 before) share one table of thirteen
      queries. A query in the table gets Windows 3.1's VGA default in the
      caller's buffer: border 3, icon spacing 75 by 72, the mouse's 6, 10
      and 1, and so on. Every other action, a setting among them, answers
      TRUE and changes nothing, as batch 4's settings calls do.
    - **Found:** Control Panel 3.0's Desktop applet showed Icon Spacing
      3146 and Border Width 1036. That was its own stack, read back from a
      buffer `ControlPanelInfo` never filled.
    - **Seen on QEMU's VGA:** the Desktop applet shows Icon Spacing 75,
      Border Width 3 and Granularity 0. Its Pattern list is still empty,
      because the patterns come from `CONTROL.INI` and there is none.
    - **Rows:** `w16xsec` (fast; the table is part 9's own and read through
      CS), which flagged the table's address load until it was marked. No
      emulator row runs a program that makes either call, so the Desktop
      applet above is the whole of the evidence; S25 decides whether
      Control Panel earns a row.
    - Sizes: the image 36,407 and bss unchanged, **1,373 bytes of room**;
      part 9 14,727 (+149), the loader part 14,745 (one row).
  - [x] **Batch 6: Windows Help and Terminal, held to the reference**
    (SPEC.md §124.8, §124.10, §124.21.2, §124.25.12).
    - Windows Help 3.0 and Terminal 3.0 were run on the reference machine
      (`win16ref/ref/w30-winhelp.png`, `w30-terminal2.png`, taken this
      batch). Real Windows Help shows no bookmark error, and five disabled
      buttons with grey icons and captions.
    - **`SetWindowText` sends `WM_SETTEXT`**, and `DefWindowProc` keeps and
      answers the text (`WM_SETTEXT`, `WM_GETTEXT`,
      `WM_GETTEXTLENGTH`). Windows Help's button procedure loads its two
      bitmaps in `WM_SETTEXT`, named from the text, and stores them in its
      window words 2 and 4. WIN16 stored the text without telling the
      window, so both words stayed 0 and the paint drew nothing.
    - **A colour bitmap copied into a monochrome one is a mask** by the
      source DC's background colour. The buttons' bitmaps are drawn on
      green, and Windows Help repaints the green the button face through
      that mask and `DSPDxax`. Without it the faces were green.
    - **The name table answers a string type with a numbered name.**
      Terminal's `FindResource(1003, "CCTL")` is type F008h, number 1003, in
      its `RT_NAMETABLE`. Batch 4's note that Wine would not match it was
      wrong: Wine's `NE_FindNameTableId` does this, and WIN16 already did
      it for a string name. It skipped the table whenever the name was a
      number.
    - **`int 21h` `59h` is answered**, from the last failed call into
      WIN16's DOS. Windows Help `_lopen`s `WINHELP.BMK` and creates it only
      when the extended error is 2. The core has no `59h` and one byte of
      `CORE_MAX` left, so WIN16 keeps the code where both of its paths into
      the core pass; DOS.O88 is unchanged.
    - **Seen on QEMU's VGA:** Windows Help opens with no message, its
      buttons as on the reference, `WINHELP.BMK` created in WIN16's Windows
      directory and the Bookmark menu enabled. Terminal opens its main
      window with no *Emulation selected is not available*. Notepad's and
      Cardfile's titles still arrive, now through `WM_SETTEXT`.
    - **Still differs from the reference:** the gradient behind Windows
      Help's logo is a blue block and black, and Terminal's ground is white
      where Windows' is a pale yellow. Both are colours outside the
      sixteen, which Windows' display driver dithers in a solid brush and
      WIN16 rounds to the nearest. That is a drawing story of its own.
    - **Rows, for S25's pass:** every `win16*` row whose program sets a
      window's text (all of them, through their titles); `win16cdlg` and
      `win16file` (`SetDlgItemText`); `win16pbrush` and `win16memdc` (the
      bitmap copy); `win16mines` and `win16calc` (the name table, CALC's
      `"SC"`); and `w16xsec` in the fast tier.
    - Sizes: the image 36,495 (+88), bss 27,246 (+2), **1,283 bytes of
      room**; the library part 61,686 (+284).
  - [x] **Batch 7: a full heap refuses in the program's words**
    (SPEC.md §124.26.3, §124.25.10).
    - 3.1's Paintbrush, saving its default 640 x 442 picture on 640 KB, was
      ended by WIN16 with *Not enough memory for its code*. Halted at that
      moment with a knob build, the heap had 36 KB free, its largest run
      17,312 bytes; the segment wanted 10,332 at most. The loader had
      claimed its 8 KB cluster cache FIRST, which took that run, and every
      other run was under 4.8 KB with Paintbrush's code pinned by its stack.
    - **The segment's block is taken first, and the cache shrinks to one
      cluster when 8 KB cannot be had.** The new block is marked kept while
      the cache is claimed: `w16l_codeold` re-ages every segment it finds in
      use, so on a full heap the newest block could otherwise become the
      oldest and be discarded for its own cache.
    - **The full-screen save itself cannot fit on 640 KB**, and is not
      S20's to give: a global block lies inside one arena, WIN16 has three,
      and the largest is the free run less 32 KB, 88 KB here. Windows 3.1
      only ever ran with extended memory. Moved to S22, whose claims may
      live above 1 MB.
    - **Seen on QEMU's VGA:** Paintbrush now answers *Not enough memory for
      this operation* itself, keeps running and opens its menus; Notepad
      3.0 under a 4 KB code budget (`-DW16_CODEKB=4`) types, opens Find and
      About and counts its characters, every segment reloaded through the
      new order.
    - **Rows, for S25's pass:** `win16write` (the reload path under a small
      budget), `win16pbrush`, `win16big`, and every row whose program has
      discardable code.
    - Sizes: the image, bss and room unchanged (1,283); the loader part
      grows by the new order.
  - [x] **Batch 8: the `win16write` row** (SPEC.md §124.26.6).
    - `tests/win16write.py` builds WIN16 with `W16DEFS=-DW16_CODEKB=40` into
      a private tree, types a sentence into Write 3.0, saves it as `ROW.WRI`
      through Write's own dialog, starts a new document and opens `ROW.WRI`
      again, the window named for it each time. It reads `ROW.WRI` off B: as
      a Write file holding the sentence, and two new counters,
      `w16_cnload` and `w16_cndrop`, must both have moved.
    - Driven once by hand to write it: under the 40 KB budget Write loaded
      265 segments and discarded 261 while it typed, saved, cleared and
      opened, and the file was 31BEh with the sentence at 128.
    - The win16-reference skill's `samples` now copies `WRITE.EXE` too.
    - **Left for S25's pass:** the row itself, and the XT's cost per reload
      on MartyPC, which the story's verification names.
    - Sizes: bss 27,250 (+4), **1,279 bytes of room**.

- [ ] **S20b: Windows inside a window, MDI (R6, R9).**
  - *As a user, File Manager 3.0 opens its drive window, and Program
    Manager its groups.*
  - S20 listed File Manager 3.0 as a target. Its 18 missing imports are
    small, but three of them are the Multiple Document Interface, which
    WIN16 has none of: the `MDICLIENT` class (`WM_MDICREATE`,
    `WM_MDIDESTROY`, `WM_MDIACTIVATE`, `WM_MDIGETACTIVE`, `WM_MDINEXT`,
    `WM_MDIMAXIMIZE`, `WM_MDIRESTORE`, `WM_MDITILE`, `WM_MDICASCADE`,
    `WM_MDIICONARRANGE`, `WM_MDISETMENU`), `DefFrameProc`,
    `DefMDIChildProc` and `TranslateMDISysAccel`. An MDI child is a child
    window with a caption, a frame, a system menu and minimize and
    maximize boxes, drawn and dragged inside its parent's client, which
    WIN16's windows have never been. Program Manager, S21's target, is MDI
    too, so this comes before S21.
  - It needs room the image does not have (1,283 bytes): MDI goes in a part
    of its own, fetched at the first `MDICLIENT`.
  - **Targets:** WINFILE 3.0 and PROGMAN 3.0 to their windows; a Watcom
    fixture that creates, tiles, cascades, maximizes and closes MDI
    children.
  - **Verification:** a `win16mdi` row on the fixture.
  - **All of it is part 9's** (SPEC.md §124.27): a part of its own would
    have spent a gate's bytes in an image with 1,279 to spare. The image
    keeps only what is in its paths already - a child's frame in the
    geometry, the press routing and the paint order.
  - [x] **Batch 1: the calls around it** (SPEC.md §124.27.1).
    - `GetDriveType`, `InquireSystem`, `GetHeapSpaces`, `DirectResAlloc`,
      `LongPtrAdd`, `ExitWindows`, `GlobalFindAtom`, `IsCharAlpha`,
      `AnsiUpperBuff`, `GetIconID`, `LoadIconHandler` and the ten segment
      equates File and Program Manager import beside MDI.
  - [x] **Batch 2: the client, the children, the frame and the caption**
    (SPEC.md §124.27.2-.4).
    - `MDICLIENT` is a WIN16 class; `WM_MDICREATE` and `WM_MDIDESTROY` call
      `CreateWindow` and `DestroyWindow` back through the program's stack.
      A child's frame and caption were measured off Windows 3.0's Program
      Manager. `DefFrameProc`, `DefMDIChildProc`, `TranslateMDISysAccel`.
    - **File Manager looped forever** recolouring a bitmap's blue in a
      block it had freed and loaded again: a `FreeResource` that takes the
      usage to 0 now discards the block, so the reload reads the file.
  - [x] **Batch 3a: moving, sizing, maximizing and minimizing**
    (SPEC.md §124.27.5).
    - Windows 3.0's XOR outline over `OSAPI_GFX_XOR_RECT`; the bar merge
      with the child's system menu and a restore box as `MF_BITMAP` items;
      a minimized child as its class's icon with its title under it.
  - [x] **Batch 3b: tile, cascade, arrange icons, and the paint order**
    (SPEC.md §124.27.6-.7).
    - WIN16's clip is one rectangle, so overlapping MDI children are
      painted bottom to top: a child painted owes the ones above it the
      overlap (`w16i_above`).
  - [x] **Batch 4: Program Manager's items** (SPEC.md §124.27.5, .7).
    - Each item is an iconic plain child that Program Manager places with
      `SetInternalWindowPos` and paints itself, making its icon from the
      `.GRP` file's bits with `CreateCursorIconIndirect` in every
      `WM_PAINT`. That call now converts 4-plane, 4bpp and 1bpp bits to an
      icon `DrawIcon` reads, and `DestroyIcon` frees it.
    - **Two defects on the way:** the plane loop's counter was CX, whose
      high byte held the column's bit, so every colour read 0; and a FIXED
      block's handle is its segment, above `W16_HSYSICO` where WIN16's own
      icons are numbered, so nothing drew and nothing was freed. The block
      is moveable now.
    - A posted `WM_SETFOCUS` is acted on only while the child still has
      the focus, which ended a focus ping-pong between two items.
  - [x] **Batch 5: the fixture and the row** (SPEC.md §124.27.8).
    - `MDI.EXE` (`tests/win16/mdi.c`) checks every message by what a
      program reads back. Its first run found five defects:
      `WM_MDIGETACTIVE`'s high word, `IsIconic` and `IsZoomed` answering
      `FALSE` always, a tile placing the icon (its icon-row measure refilled
      the list being placed), `WM_MDINEXT` activating the active child
      again (a clobbered DI), and `WM_MDIMAXIMIZE` not activating.
    - **Seen on QEMU's VGA:** `MDI OK`; File Manager's tree window moved,
      sized, maximized, minimized, tiled and cascaded; Program Manager's
      Main group with its six item icons beside Windows 3.0's screenshot.
      Program Manager's titles are cut where Windows' are not: WIN16 has
      only the System font, wider than the small one Windows titled icons
      in.
    - **Rows, for S25's pass:** `win16mdi` (new); `win16mines`,
      `win16sol`, `win16calc` and `win16desk`, whose windows pass through
      the child geometry and the paint order; `w16xsec` in the fast tier.
    - Sizes: the image 36,727, bss 27,399, **898 bytes of room**; part 9
      24,437 bytes.

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
  - **Pinned in SPEC.md §124.28 before its code.** A task's state is
    swapped between the bss and a context block per task, not indexed:
    the image has 898 bytes and every use would cost some. Owners go
    beside the full window and class records; a later program's segments
    are a global block, not a ninth claim.
  - [x] **O2(a), first:** `OSAPI_WM_CLOSE` keeps every request, a bit per
    window slot (SPEC.md §75.2.1). +20 bytes of `.text` on both kernels,
    no RAM, no rung crossed.
  - [x] Batch 1: the task model with one task — the task table, the
    context list and its switch, the owner bytes, and the task calls —
    and nothing a single program can see changes. *Its three export rows
    went in between `W16_SEGROW` and `W16_SEGROWP`, which moved every
    segment equate (`__0040H` and the rest) by three. The guard compared
    the second with the end, so it passed. Batch 2 moved the rows and
    made one number of the two.*
  - [x] Batch 2: `WinExec` and `LoadModule`, the turns, input to a
    window's task, a second program's os8088 window.
    - A callback switches to its window's task first (`w16_cbin`). A post
      goes to the window's task's queue. `GetMessage` looks only at its
      own task's windows and timers. `w16_after` runs the parked tasks'
      turns.
    - A later program's segments are a global block (`w16_kclaim`). It
      shares the bind table. The loader keeps one copy for every task.
    - Seen: TASKS starts TASKB with a tail and `SW_SHOWNOACTIVATE`. TASKB
      opens its own os8088 window. Posts cross both ways, and
      `PostAppMessage`, `GetWindowTask`, `IsTask` and `GetNumTasks` all
      answer: *TASKS OK*. MDI still says *MDI OK* alone. The TASKS
      fixtures were written now rather than in batch 5, to see this
      batch.
    - 121 bytes of the image (632 left), the rest in part 9.
  - [ ] Batch 3: messages between tasks, a task's end while others run,
    the instance window handed over.
  - [ ] Batch 4: `WinHelp` and WINHELP.EXE; Program Manager starting Clock
    and Notepad.
  - [ ] Batch 5: DDE, the `win16tasks` row (its fixtures came in batch 2).

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
  - **The contract is SPEC.md 124.31** (pinned 2026-11-01). The batches:
    - [x] *1: the contract and the build.* SPEC.md 124.31; the arm built
      in kern_mega's tree (`-DPKG_MEGA -DDOS_MEGA`) and put on
      `build/megawin16.img` and `megawin16-120.img` by `make mega`;
      kern_mega's `DOS.O88` hands an NE over; the arm accepts `NEF_PMODE`
      and refuses a real-mode kernel.
    - [x] *2: virtual paragraphs.* `apps/win16/w16pm.inc`: the map,
      `W16SEG`/`W16VOF`/`W16TOSEL`, the claim wrappers, the edge
      (`w16_psel`, `w16_pvirt`, `w16_pin`, `w16_pout`); the global heap,
      `w16_kclaim`/`w16_pkeep`, the face, bitmaps, resources, menus and
      dialogs' segment loads through them. The census is out of the arm
      (it decides only for an 8088), which is what pays for it: the arm
      has 1,656 bytes of room against the real-mode build's 1,279.
      **Seen 2026-11-01: TINY16 runs on kern_mega in protected mode** -
      InitTask, InitApp and its MessageBox, drawn - and stops at its exit's
      `int 21h` (item 4).
    - [x] *3: the loader.* A program's own segments: selectors per
      segment, fixups, DGROUP and the PSP as selectors, the image's code
      half for imports, part 3/5/9 vectors through code halves. Then
      discardable code (the module block's thunks name code halves; a code
      segment's selector reaches its claim's end so the thunk, the
      `w16_segld` frame and the stack scan agree), libraries (DGROUP and
      hInstance as `w16_pblk` selectors, `W16LM_SEGS` as the program's
      selectors), `W16CSEG` for the 27 `push cs / pop ds` sites, and
      AH=35h's stand-in for the machine's vectors (124.31.4-124.31.6).
      **Seen 2026-11-01 on a dev kern_mega with 124.31.2's (d1): W31MINE
      plays, CLOCK, SOL, NOTEPAD and DLLUSE (two libraries) run in
      protected mode.** CALC and FPU stop at the x87 vectors (item 4);
      PBRUSH at the DOS host's far call (item 6, since done).
    - [ ] *4: the vectors.* Needs the owner's answer on 124.31.2 item 2,
      CORRECTED 2026-11-01: 21h is IRQ1's gate in protected mode, and a
      package's IVT write never reaches `mega_ivt_sync`, so 2Fh, 31h and
      34h-3Dh must be read at the int too (the prototype's `mega_swv`).
    - [x] *5: what the program is told* (124.31.6). `GetWinFlags` answers
      standard mode; `SEG_TILE` (kern_mega, O2(b)) tiles a block over
      64 KB; the selector functions (`w16sel.inc`) and a DPMI 0.9 subset on
      `int 31h` (`w16i31.inc`); a freed ES comes back 0. New fixture
      `SELTEST.EXE` (`tests/win16/seltest.c`). **Seen 2026-11-01 on the dev
      kern_mega with (d1): BIG's 128 KB huge block reads back with 0 wrong
      at `__AHINCR` 8; SELTEST says 0 wrong, flags 0415; and with (d1)'s
      software vectors read at the int, CALCULATOR computes (9 x 9, then
      sqrt) and the FPU fixture answers.**
    - [x] *6: DOS* (124.31.7): the core unchanged; the block's code half,
      the PSP's selector, the window's cell rewritten as a selector after
      `W16DH_INIT`. **Seen 2026-11-01: PAINTBRUSH (with PBRUSH.DLL) draws
      a line; WRITE, CARDFILE, CALENDAR and Windows 3.1 Solitaire run in
      protected mode.** REGEDIT is not in the fixture set here; it is
      S25's, with the win16ref programs.
    - [x] *7: above 1 MB*, retiring WIN16's `tests/megahigh.txt` lines.
      The arm says `OS88_HIGH | OS88_HIGHMEM`. Its image (37,877 bytes and
      26,382 of bss) and its own plain claims go above 1 MB. The map's
      claims, which hold everything the program is handed, stay
      conventional through the DMA door, because `SEG_WIN` and
      `GlobalDOSAlloc` need a paragraph (124.31.3). **Seen 2026-11-01 on
      the dev kern_mega with (d1):** SELTEST reports WIN16's image at
      linear `00F9D000` with 0 wrong. BIG, Minesweeper 3.1 and Paintbrush
      run as they did below 1 MB.

- [ ] **S23: MMSYSTEM (R4).**
  - *As a user, Sound Recorder plays a WAV through my Sound Blaster, and
    Media Player plays an audio CD.*
  - `sndPlaySound`, `waveOut*` and `waveIn*` over `SOUND.DRV` (§34);
    `mciSendCommand` and `mciSendString` for `waveaudio` and `cdaudio`, the
    latter over `CDROM.DRV`'s package door (§119.4); `midiOut*` where
    SOUND.DRV has a MIDI device; `timeGetTime` and `timeSetEvent`.
  - **Targets:** SOUNDREC and MPLAYER 3.1, both protected mode only and
    386 code, so after S22.
  - The module is the kern_mega arm's alone (SPEC.md 124.32): every
    importer is a protected-mode program.
    - [x] *Batch 1: the clock, the devices and mmio* (124.32.1-2), in
      part 9 (`w16mm.inc`); new fixture `MMTEST.EXE`. **Seen 2026-11-01
      on the dev kern_mega:** "mmtest: 0 wrong, waveOut 0, step 1 ms".
      Its RIFF file went out through CreateChunk and Ascend and back in
      three ways (Descend and Read; GetInfo and Advance; a memory file),
      then EXIST and DELETE. Arm: 589 bytes of room.
    - [ ] *Batch 2: `waveOut` and `sndPlaySound`* over SOUND.DRV's ring.
    - [ ] *Batch 3: MCI*, `cdaudio` over CDROM.DRV and `waveaudio`.
    - [ ] *Batch 4: `waveIn`, `timeSetEvent`, `midiOut`*; SOUNDREC and
      MPLAYER.
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
  - **Targets:** the two installers of O3 (section 3.1), through both
    toolkits' bootstraps: `SETUP.EXE`, which copies the toolkit to a
    temporary folder and `WinExec`s it (S21).
    - *Microsoft Arcade* 1.0 off its floppy: the Setup Toolkit's
      `SETUP.EXE` copies `_MSTEST.EXE`, the `MS*STF.DLL` set and
      `ARCADE.MST` to `~MSSTFQF.T` and runs `_mstest arcade.mst`; the
      script makes the group with `CreateProgManGroup` (DDE).
    - *Sir Hangman* 1.0 off its folder: InstallShield 3's `SETUP.EXE`
      expands `_INST16.EX_` and runs it against `SETUP.INS`; it unpacks
      `PROGRAM.LIB` into the folder the user chose and `SYSTEM.LIB`'s
      `VBRUN300.DLL`, `MMSYSTEM.DLL`, `COMMDLG.DLL` and three VBXs into the
      system directory, then makes "Ssorg Games" through `DDEML`. Its
      bootstrap is real-mode capable, so it is also the real-mode leg.
      Its `MMSYSTEM.DLL` and `COMMDLG.DLL` are Windows for Workgroups'
      (4.00): WIN16's own modules must keep answering for those names when
      a copy of the real one lands in the system directory, as winevdm's
      built-ins do.
    - *Microsoft Multimedia Mozart*'s own setup, off its CD: the Setup
      Toolkit again, from `\SETUP.LST`, with a performance check over
      MMSYSTEM (`ENCCDPRF.DLL`, `MMPERF.INC`) and a 16- or 256-colour art
      set chosen from the display.
  - **Verification:** a `win16setup` row runs an installer fixture to its
    end and reads the installed files and the links off the image.

- [ ] **S25: Acceptance.**
  - *As the owner, I can see a Windows CD-ROM title installed and running
    on os8088, and what still does not run.*
  - The O3 title, *Microsoft Multimedia Mozart* (section 3.1), installed
    from its CD and run from its desktop link on 86Box's `286-mega-cdrom`
    and `386-mega-cdrom`, playing its Red Book tracks (S23's `cdaudio`) and
    its WAVs. It rests on **Visual Basic 3.0's runtime**, which is
    protected mode only, so it is S22's first real program after REGEDIT.
  - *Microsoft Arcade* and *Sir Hangman* installed and their programs
    started from their links: Arcade's games are 386 code over `WAVEMIX`
    (S23), and Sir Hangman is VB 3.0 again.
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
