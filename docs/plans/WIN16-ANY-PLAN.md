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
    - **Character Map is not done**: its template asks for `MS Sans Serif`
      8 (`DS_SETFONT`), 7 x 13 dialog units on Windows 3.1's VGA against the
      System font's 8 x 16, so its window is 696 pixels across and its
      buttons are past the screen's right edge. **Batch 5 is a dialog face
      for `DS_SETFONT`** - Wine's MS Sans Serif (LGPL), drafted per adapter
      as the System font was (D4) - laying such a dialog out in its own
      units and drawing its controls' text in it. `CreateFont` answers from
      it too.
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
      Marquee's Format Text is `COMMDLG.ChooseFont`, S18's.
    - Known and pinned: the PIF Editor's Advanced Options is taller than
      os8088's desktop, and its last row (the shortcut key) is below the
      screen's edge. The window records live in WIN16's 64 KB data segment
      (102 bytes each), so the next rise of `W16_MAXWND` should move them
      out of it rather than spend more of it: 9,762 bytes of room are left.
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
