# WIN16-NEXT-PLAN: closing Phase B/C, and what it leaves open

**Status: PLAN.** This plan follows `docs/plans/WIN16-ANY-PLAN.md`:

- Its Phase A (S14–S21) is merged as #322.
- Its Phases B and C (S22–S25) are on PR #327, branch `plan/win16-phase-bc`.

This plan holds two things:

1. What #327 still owes before it merges (N0–N3).
2. The limits and open items it will merge with (L1, L2, Q1–Q5), so they are not lost once that PR closes.

The contract stays SPEC.md §124. The diary stays `docs/HOW-WIN16-WAS-BUILT.md`.

The owner's end goal is unchanged:

> The end goal is to be able to run any Win16 application (including CD-ROM
> based win16 applications with installers), so don't block ourselves out of
> that ultimate goal.

## 1. Where #327 stands (2026-11-02)

Measured on QEMU's `kern_mega`, by looking at the screen, not by suite rows:

- **Microsoft Arcade 1.0's setup** (the Setup Toolkit installer, O3) runs to "Congratulations! Microsoft Arcade Setup successful!".
  - `C:\ARCADE` holds all 22 files its INF lists, at their sizes.
  - The ARCADE Program Manager group has its five links.
- **Mozart's setup** (the O3 CD title's installer) copies every file, including VBRUN300.DLL, its VBXs, fonts and MSACM. It makes its Program Manager group through DDE.
  - Its final "Setup complete" dialog is invisible. The cause is that a 42 KB bitmap cannot be had from the heap; N0 is the fix.

The owner's decisions taken on the way:

- **D1:** GlobalReAlloc keeps a quarter's slack. A program's block selector reaches its claim's end, as a stopgap.
- **D2:** a BIOS call made from a Windows program's stack runs on a stack WIN16 lends, with no LOW rung.
- **D3:** library code is discardable (M1).
- Two boot-only `.mega` rungs, recorded in KERN-MEGA-PLAN 14.136.

## 2. Before #327 merges

- [x] **N0: commit M1, discardable library code** (SPEC.md 124.26.11). DONE on #327 (7111ba35e, f948f4347). Seen on QEMU's `kern_mega`: Mozart's last dialog with its picture, then *Microsoft Mozart Setup is complete*; Arcade's *Congratulations!*
  - It is implemented and was uncommitted when this plan was written. One bug is already fixed: a reloaded library segment bound its imports of other libraries as missing.
  - *Done when* Mozart's setup shows its final dialog on `kern_mega`, Arcade's setup still reaches *Congratulations!*, and the SPEC states both:
    - what is discarded: library segments reached only through entry tables (at=3);
    - what is kept: segments named by number (at=2/5), which is all of VBRUN300 and MOZART.EXE (L1).

- [x] **N1: replace D1's stopgap with the proper fix.** This brings back the overrun fault that Windows gives.
  - *Status:* implemented on #327 (ad390599c, `SEG_HND`, KERN-MEGA-PLAN 14.138). The stopgap text is gone, and the LDT high-water across Mozart's setup is 1,658 of 1,992. DONE: `win16ovr` passes under Bochs (74fe5956f). The write one byte past OVRTEST's block raises #GP inside the program's code, and its 40→80 KB grow reads back with 0 wrong. What happens after the fault is Q1's: the machine stops where Windows would close the program.
  - D1's (a) lets a program's block selector reach its claim's end. An overrun of a global block then writes into the next block silently, where Windows faults. SPEC.md 124.31.3 marks it as a stopgap.
  - The replacement has two parts:
    - **One descriptor per handle.** GlobalReAlloc re-aims that descriptor's base and limit in place, on the same selector, as Windows' protected modes do. A block that grows a step at a time then costs no new LDT entry, and its limit stays its own length.
    - **A bound walk window** (`SEG_BWIN` + `SEG_AIM`, re-based by the compactor) for WIN16's own header, split and zeroing touches. Before, one Mozart setup run took a W16SEG at 880 distinct paragraphs, and each one cost an LDT entry. Through the window it costs none.
  - Cost: about 110 bytes for the window and more for per-handle descriptors, in the protected-mode image. If they do not fit after M1, move cold code into an on-demand part first (parts 9 and 10 are the precedent). If they still do not fit, the numbers go to the owner.
  - *Done when:*
    - a fixture's write one byte past a global block faults on `kern_mega`, and the fixture is a row;
    - the LDT's high-water mark across Mozart's whole setup is measured and written into SPEC.md 124.31.3, against the 2,016 entries there are;
    - (a) is gone from SPEC.md 124.31.3, and its stopgap text with it.

- [x] **N2: finish the plan, the diary and the PR description.** DONE on #327 (f948f4347). S22 is ticked; S23, S24 and S25 stay open for what nobody has seen yet.
  - WIN16-ANY-PLAN's S22–S25 checkboxes, ticked only for what was seen.
  - The diary's Phase B/C chapters, with what each milestone measured.
  - #327's body: what works, what was decided, the rung record, the rows to run, and this plan's L1/L2 and Q1–Q5 as what remains.

- [x] **N3: the final pass, once, at the end** (CLAUDE.md, *Testing*). DONE on #327 at 129fb67af:
  - **The soak union:** five failures. Three were the rows' own (xvol, tasks, nedos). One was a Write regression from c2002f526, fixed in 072167d52. The fifth was w16censall, which is Q8 and also fails on main. All five have been re-run and are green except w16censall.
  - **The Bochs set:** win16ovr and 14.135's megaxmove, megaown, megarehome and megatbl all pass.
  - **test-full:** 85 ok and 2 failed. pins was registered in dc4aa4f05 and is now ok alone. w16censall stays red (Q8).
  - **budget-check:** passes.
  - **Not run here:** megarmb286, which needs 86Box (a Linux build), and mega-c64move and mega-c64part, which need Bochs and qemu-system-i386 on one box.
  1. Refresh `build/win16/DOS.O88` first, with `make build/win16/DOS.O88` or `make win16test`. A plain `make` leaves it stale, and every win16* row puts it on its B:, so a stale copy tests the wrong DOS core.
  2. Run the deduplicated soak union of every row S14–S25 named, in one `os88test.py soak -k … -k …` call.
  3. Then run `make test-full` alone, with no concurrent `make`, because the buildmatrix row clobbers.
  4. Run `make budget-check`.
  5. If anything fails, re-run only the failing rows. Re-run `test-full` only if a fix touched code outside them.

## 3. The limits #327 merges with

- [ ] **L1: Mozart itself, not its installer.**
  - `MOZART.EXE` (248 KB) is Visual Basic 3.0 p-code over `VBRUN300.DLL` (398 KB, 101 segments, protected mode only).
  - Both load segment numbers into registers directly:
    - VBRUN300 has 2,567 segment-only relocations in code and 112 in data, which name 98 of its 101 code segments;
    - MOZART.EXE has 606.
  - Under SPEC.md 124.26.1 a segment named by number is kept, so M1 discards none of their code. About 520 KB of fixed code (VBRUN300 329 KB, MOZART 184 KB) has to sit in `kern_mega`'s four arenas, about 810 KB. The setup's libraries will have exited by then.
  - **First, measure** whether Mozart starts from its desktop link and plays (S25). Do not assume either way.
  - **If it does not fit:**
    - Making those segments discardable needs a new `kern_mega` hook. A dropped segment's selector is marked not present, and the `#NP` fault it raises reloads the segment and restarts the instruction, as Windows' standard mode did.
    - That is a kernel change, so it is **owner decision O8**. It goes to the owner with:
      - its `.mega` and resident cost;
      - its re-entry argument, since a fault can arrive inside a BIOS call or a callback;
      - the arithmetic that says Mozart needs it.
    - Any large Visual Basic 3.0 program will eventually need the same hook, so this is the route the end goal runs through.
  - **Status 2026-11-04 (measure; QEMU `kern_mega`, `-cpu 486 -m 16`; looks, not rows).** Mozart does not start yet, for two reasons that come before memory, and O8 is not shown to be needed.
    - **Setup:** the CD's data track was extracted to `C:\SRC` (`win16ref/titles/mozart/x` holds only the setup's own files, so the copy stops at `GLOSSARY.IDX` on it). Setup then reaches *Microsoft Mozart Setup is complete* on the merged tree, with the 42 KB picture, and *Return to Windows* leaves the group's desktop icon.
    - **First refusal, from the group's link:** *Needs VBRUN300.100 which WIN16 does not provide yet.* SPEC.md 124.23.1 finds a library only in the program's own folder. Mozart's setup puts `VBRUN300.DLL` in the Windows directory (`SYSTEM\APPDATA\WIN16`), and `MOZART.EXE` is in `C:\MOZART`. WIN16-ANY-PLAN O5 chose Windows' order (program folder, system directory, Windows directory) and the loader does not do it. This is a SPEC and loader fix that costs no kernel byte. Not done here.
    - **Second finding, same install:** `MCI.VBX`, `MSGBLAST.VBX`, `PICCLIP.VBX`, `MVFS2.DLL` and `MCICDA.DRV` are in the INF's lists but were not copied (8 of the 13 shared and system files were). Not investigated.
    - **With those files copied beside `MOZART.EXE` (outside the setup):** it loads (*Loading...*, 6 to 27 s), WIN16's box says *Running*, and 1 to 2 s later the instance is gone. No dialog, no toast, no *Needs* line. A fault would show kern_mega's *closed* toast, a missing import its *Needs* window, and a memory refusal its own words. `HANGMAN.EXE` (VB 3.0 too, run from `hangman/x` with its own `VBRUN300.DLL` beside it) does the same, so it is VBRUN300's start, not Mozart's. **The cause is not found.** The `-DW16_TRACE` arm does not assemble (word displacements and `APP_MAX_SIZE`), and the `make megadiag` kernel starts none of this tree's WIN16 arms. A trace that fits is the next tool.
    - **Arithmetic:** fixed code is VBRUN300 329 KB + MOZART 184 KB = 513 KB against the four arenas' about 810 KB (the plan's figures). The load finished with no *Not enough memory*, so that fixed code did fit with the setup's libraries gone. The free space at that moment was not read: the arena table is behind the virtual map and nothing reads it from outside.
    - **So O8's case is not made by this look.** The program never reached a window, so how much it allocates is unknown. The next walls are not memory: VBRUN300 imports 23 functions WIN16 lacks (`GDI` `OffsetWindowOrg` `CopyMetaFile` `SetRectRgn` `EnumMetaFile` `PlayMetaFileRecord` `Arc` `Pie` `CreateBrushIndirect` `CreatePenIndirect`; `KERNEL` `SetHandleCount` `GetCurrentPDB` `FreeModule` `GetCodeHandle`; `USER` `WaitMessage` `GetClassWord` `GetClassLong` `EndMenu` `GetUpdateRgn` `BeginDeferWindowPos` `DeferWindowPos` `EndDeferWindowPos` `CreateIcon` `TrackPopupMenu`). It was 48 on 2026-11-01.

  - **Status 2026-11-04, end of round 2 (SEEN unless marked).** Mozart starts and reaches its own message box, which is further than any earlier look. The walls, in the order they fell: VBRUN300's missing imports (`vbgdi`, `vbuser`, `props`, `part11`); `GlobalReAlloc` of its own DGROUP (`dgroup`, SPEC.md 124.9.2); `GetClassInfo` and a class table that held 12 (`ctlproc`, 124.25.18); a full local heap in a moveable block (`lheap`, 124.25.19); `GetDOSEnvironment` in a library's C start-up (`vbxlib`, 124.25.21); an even data selector where VB tests `test cl,1` (`oddsel`, 124.25.23). Mozart now stops at VB's *Insufficient resources to continue*, because its `Declare`s bind with `GetProcAddress` by name and WIN16 knows 16 to 18 names (see "Found in round 2"). **O8 was built (`o8np`, SPEC.md 124.26.x) and is not what Mozart waits for.** The setup completes and leaves 12 files in the system directory (`mozcopy`, 124.20.5.1). Not seen: Mozart's window, or playing.

- [ ] **L2: Sir Hangman's install, a known limit in S25.**
  - Sir Hangman 1.0 is the InstallShield 3 installer, O3.
  - Its engine, `_INST16.EX_` expanded to `_INS0433._MP` (542,720 bytes, module ISINST30), has 607 selector fixups by segment number into its 64 discardable code segments. So `w16l_keepck` keeps all 64, 404 KB, and a launch from the launcher's task refused with "Not enough memory to load it".
  - Started as the first program, from a claim of its own, the engine loads.
  - The routes:
    - a later program's segment block as a claim of its own, as the first program's is;
    - O8's `#NP` reload (L1), which would cover it too.
  - The program itself, `HANGMAN.EXE`, is VB 3.0 again, so after the install it is L1's question.
  - **Status 2026-11-04 (measure).** Sir Hangman's `SETUP.EXE` from A: answers InstallShield's own *Insufficient memory available to run Setup ... Error 111* on `kern_mega` (`-m 16`) and on `kern_big`, the same text on both. It is the L2 refusal as InstallShield words it, not WIN16's *Not enough memory to load it*. The engine's 404 KB of kept code is the cause on the arm, as written above; why `kern_big` says the same was not looked at. `HANGMAN.EXE` run directly: see L1.
  - **Status 2026-11-04, end of round 2.**
    - **`kern_big`:** `l2diag` showed the Error 111 is the launcher's own `GetFreeSpace(0) >= 700,000` test. Under the owner's honest-`GetFreeSpace` decision it stays there: Sir Hangman installs on `kern_mega` only. `thunk` (124.26.14) is built and seen on its own fixture, and does not change that.
    - **`kern_mega`, in the order the walls fell (seen):** the launcher's test passes (honest `GetFreeSpace`, 124.25.1); the engine's out-of-memory box with no text (`isbox`: 405 KB of kept code, 17.5 KB free) goes with O8's `#NP` reload (`o8np`); `LoadLibrary` finds the engine's DLLs by full path and `SetWindowsHookEx` exists (`hangman2`, 124.25.22); `DdeInitialize` is answered (`ddeml`, 124.25.26). The engine then stops at *Not enough memory for its code* about 14 s in. Not seen: its welcome dialog, an install.

## 4. Open items found on the way (not blocking #327)

Each was recorded in the Phase B/C report and not yet closed. Re-check each one against the merged tree before starting it.

- [x] **Q1: a protected-mode fault in program code takes the machine down.** DONE (2026-11-04, `q1fault`, SPEC.md 124.31.11, KERN-MEGA-PLAN 14.139-14.141): seen once under Bochs (`win16ovr` step 3: `kern_mega CLOSED OVRTEST 0D at 11BC:0517`, no halt); `q1ivt` puts back the IVT cells the package held, 5 of 5 under Bochs. The close names the program's stem, and a multi-program instance closes whole (below).
  - With DS = SS = the program's DGROUP, `mega_fown` does not attribute the fault to WIN16. The result is "FAULT 0D … reboot", where it should close the package.
  - The kernel has to recognise a WIN16 program's selectors as WIN16's: the LDT range WIN16 owns, or a registered owner word in the shape of D2's hook.
  - **Seen under Bochs in N3 (#327, `win16ovr`).** OVRTEST's write one byte past its block raises the #GP that N1 promised. But `[mega_infault]` = 1 and the machine stops, where Windows would close the program.
  - **Why the kernel cannot tell who owns the fault.**
    - `mega_cs2ds` assumes an image's code/data selector pair, with the data selector 8 above the code selector.
    - `mega_fown` then searches conventional paragraphs below A000.
    - A WIN16 program's CS is a SUB or tile selector over WIN16's memory, which has been above 1 MB since S22 batch 9. So neither test finds an owner.
  - **The proposed fix** is to attribute a CS that is not an image pair by its descriptor's BASE, the way 14.137's `mega_bown` does: a pool block's `MX_OWN`, or `mem_tab`'s `MC_OWN` below 1 MB. It needs its own owner rows.
  - **Check, not yet done:** whether the +8 assumption can blame a fault on whichever package owns the selector 8 above the faulting one. If it can, it would close the WRONG package, which is worse than halting.

- [x] **Q2: real mode's STACK OVERFLOW TASK 00 (UI task)** (2026-11-04: not reproducible; 302 of 512 bytes, SPEC.md 124.31.8) during `win16xvol`'s copy across volumes on `kern_big`.
  - The copy still finishes and reports XVOL OK.
  - It is not SPEC.md 96.11.11's doing. It was seen with a core that never asks DFREE, and with the older core.
  - It is undiagnosed, and it matters for the real-mode installers.

- [x] **Q3: MSCDEX's `int 2Fh` is deferred** (SPEC.md 124.33.3).
  - It costs about 100 image bytes, and no target has yet been seen to ask for it.
  - Mozart's CD-ROM run (L1) is where the question is answered.
  - Done (2026-11-04): `w16_i2f` answers 1500h, 150Bh, 150Ch and 150Dh off `CDROM.DRV`'s volume; other 15xxh are refused. Sir Hangman's installer is the target that asks. +110 bytes of image and bss on each arm. Row `win16mscdex`; the kern_mega arm is not yet run.

- [ ] **Q4: the rest of S25's acceptance.** (2026-11-04, end of round 2: Asteroids, Battlezone and Missile Command start a game and exit cleanly, Tempest reaches its rate screen, Centipede reaches its dialog and draws a black field; all seen on `kern_mega`, silent. Not done: Mozart's Red Book tracks, a run in real mode, w16reach re-run on the integrated tree.)
  - Arcade's five games are 386 code over `WAVEMIX`, started from their links.
  - Mozart's Red Book tracks play through S23's `cdaudio`, on 86Box's `286-mega-cdrom` and `386-mega-cdrom`.
  - What of all three runs in real mode, where their CPU tier allows.
  - `tools/w16reach.py` re-run against WIN16-ANY-PLAN §1.
  - **Status 2026-11-04 (measure; QEMU, looks).**
    - **Arcade's setup** reaches *Congratulations! Microsoft Arcade Setup successful!* on the merged tree, on `kern_mega` at `-m 16`, and its group has five links.
    - **All five games refuse the same way**, a second or two after the link, with WIN16's *Needs USER.26 which WIN16 does not provide yet.* USER.26 is `SetProp`; `GetProp` (25) and `RemoveProp` (24) are missing as well, and no game gets past them. So the 386 code does load (nothing refuses the CPU tier on QEMU's 486), and `WAVEMIX` is not yet reached. Across the five games, Mozart's and Hangman's files, the imports most programs call that WIN16 lacks are `USER` 24, 25, 26 and 112 `WaitMessage` (7 programs each), `CloseWindow` (5), `GetClassLong` (3), then the games' own libraries.
    - **Real mode (`kern_big`, QEMU):** Arcade's and Mozart's `SETUP.EXE` both answer *It needs protected mode.* Sir Hangman's `SETUP.EXE` answers InstallShield's Error 111 (L2). No setup of the three goes further on the XT-class arm than that, as the census said (R3).
    - **Not done:** Mozart's Red Book tracks (needs 86Box's `286-mega-cdrom`, which this Mac does not run, and Mozart itself does not start, L1).
    - **w16reach, 2026-11-04 against 2026-11-01** (`tools/w16reach.py` over `win16ref/titles/{arcade,hangman}/x`, the CD's `MOZART`, `MSSTP` and `MSSTP\SHARED`):

      | | 2026-11-01 | 2026-11-04 |
      |---|---|---|
      | WIN16's exports | 560 | **720** |
      | `VBRUN300.DLL`, imports WIN16 lacks (of 300) | 48 | **23** |
      | `MOZART.EXE`, in the way | R2, R4 | R2, R4 (the one import is `VBRUN300`) |
      | the five games, in the way | R2, R3, R4 | R2, R3, R4 (R4 names `WAVEMIX`, `ARCADEVF`, `AMDSPIN`, `ARCCTL`); 9 missing imports each |
      | `_INST16.EXE`, in the way | R2, R4 | R2, R4 (`DDEML`); 10 missing |
      | Setup Toolkit libraries (`MSCOMSTF` ... `MSUILSTF`) | not listed | 1 to 4 missing each |

- [ ] **Q5: re-verify after main's #328.** #328 ("Elendilon -> Main", d323a9a65) was merged into #327 on 2026-11-02 (e1a8c21f5). It changed:
  - heap claims to paragraphs rounded to 512 bytes, with `OSAPI_MEM_P` (SPEC.md 50.2.1, 50.3.7). WIN16's own claims followed on both arms.
  - parts to 16-byte paragraphs (SPEC.md 20.12.2);
  - packing, with the near-call filter (SPEC.md 20.13.9);
  - the DOS core, now live in a window (SPEC.md 96.54). Its budget went up by 32 bytes for 96.11.11, and kern_dos kept its 34 KB at the owner's word, paid for by kd_resume's shared exits.

  So N0–N3's evidence is retaken on the merged tree, not carried over from before it.

  Re-verifying turned up two of #328's own defects, both fixed on #327 as commits of their own:
  - **ASSOC_PWBANK.** A posted `OSAPI_PKG_START` document launch (SPEC.md 21.5.4) re-banked the document's locator from `[dsk_cwd]`. Through a link to another volume, `sc_walk` had already moved the machine back to the link's own drive, so WIN16 was handed `C:\SETUP.EXE` and answered "This is not a Windows program".
  - **KERN-MEGA-PLAN 14.137.** 14.135's low stopped `mega_wset` from re-aiming a BOUND window. GlobalReAlloc's move of a locked block into another arena then left the program's selector on the freed block, so Mozart's script engine wrote over a new block's header. The fix re-binds the caller's own bound window when the new base lies in a block the caller owns. A base anywhere else still gets a fresh window.

- [x] **Q6: two 42 KB blocks at once.** Closed for Mozart by route (D) in f948f4347; (A) and (C) below stay open for the general case. Mozart's final dialog does `LoadBitmap` of a 435x194 4bpp DIB, which needs the resource and the bitmap's bits at the same time.
  - Arena 3's 65 KB free is split by MSCOMSTF segments that are really on the stack, and by 32 KB of KEEP library code.
  - N0 takes route **(D)**: when the global heap refuses a bitmap's bits, they get a claim of their own (one vmap row). This is a fallback for bitmaps only, never for GlobalAlloc, so it is not a fifth arena.
  - Two routes stay open:
    - **(A)** stream a large DIB row by row into the bitmap block, so only one block is ever needed (about 150–250 B, in a part);
    - **(C)** a compactor that moves code. It needs N1's per-handle descriptors first, and it is the general answer to fragmentation by segments that cannot move.

- [ ] **Q7: `SEG_BWIN` checks no owner** (SPEC.md 50.6.11). **Built, not seen** (2026-11-04, `q7bwin`, KERN-MEGA-PLAN 14.139): `mega_segbw` asks `mega_segown` and `mega_bown` before binding, refusing with AX = 3; `megaown`'s new `mo_bwin` leg is built and not run. Found while fixing 14.137 on #327. `mega_wset`'s re-bind now asks `mega_bown` (a claim's `MC_OWN`, a pool block's `MX_OWN`), but `SEG_BWIN` itself binds a window into any block it is given. That is 14.135 item 2's class of hole, one verb along. The fix is the same test at `SEG_BWIN`'s entry. Every shipped caller binds its own block, so the fix should refuse none of them. It is a kernel `.mega` change, so it goes in its own PR, with megaown's row.

- [x] **Q8: `w16censall` fails on main as well as on #327** DONE (2026-11-04, `chores`; host-side, `t_w16cens.py --all` 110 checks passed). The cause was not the switch-table bound: the host did not follow a far call whose segment word carries a selector-only relocation, and now does (SPEC.md 124.4.1). (`tests/unit/t_w16cens.py`). It fails for Windows 3.1 Media Player's and Sound Recorder's segment 1, which were added to `win16ref/samples/w31mm`. The guest walk reaches 1,138 and 1,108 bytes that the host's does not.
  - The cause is a divergence on overlapping decodes. `tools/os88ne.py` reads MPLAYER's switch table at 0x87C one word past its end, which seeds 0x16A, inside `sub ax,7` at 0x169. The guest stops before that word, and from there the two walks reach different functions.
  - It reproduces identically on main's d323a9a65, so it is not Phase B/C's doing.
  - The fix is to bound the host's table read the way the guest does, or to decide which walk is right and make the other match. Then re-run the row.

- [ ] **Q9: WIN16's pinned claims on the arm** (`tests/pins.txt`, bucket N). `@w16_vclaim` holds a program's segments, arenas and big bitmaps as map claims, and `w16_lseg` is D2's lent BIOS stack. Neither can move, because the program holds selectors and the kernel's hook word holds the stack.
  - **To let `@w16_vclaim` move:** a relocation proc that re-bases every descriptor and the map row.
  - **To let `w16_lseg` move:** a proc that re-registers the hook word.
  - Both are walls in the arena until then (NO-PINNING-PLAN 0).

### Found in round 2 (next round)

The round's frozen scope ended at the walls below; each was reported by an agent or its reviewer and is open. An estimate is the report's, and "none" means the report gave none. The next round is planned from this list. Section numbers are the SPEC.md section the work would touch.

**Mozart and Visual Basic**
- [ ] **GetProcAddress by NAME for every bound export** (SPEC.md 124.25 with part 11, 124.25.16). Mozart's `Declare`s bind by name, about 55 of them; WIN16 knows 16 to 18 (`W16PROCNAME`, part 3, full). Names generated from the `W16EXPORT` comments into part 11: about 8 KB (about 600 exports at about 14 bytes), 0 resident, one `w16_nbind` slot per ask. Supersedes the GDI-only item from `vbgdi`. First refused names: `GetPrivateProfileInt` after `SetSystemPaletteUse`.
- [ ] **A 256-colour display** (SPEC.md 124.25 `GetDeviceCaps`; no section yet). WIN16 answers as Windows 3.0's VGA driver (BITSPIXEL 1, PLANES 4, NUMCOLORS 16, no `RC_PALETTE`), so Mozart's *Unsupported video mode* check is a correct refusal. Which index it reads was not seen. Estimate none.
- [ ] **A DGROUP laid out under 64 KB still refuses VB's `GlobalReAlloc` grow** (124.9.2, 124.21.4). VB quits with no message. Leave room after DGROUP at load, or move it on grow. Estimate none.
- [ ] **`GetClassName` answers the cut, upper-cased 16-character name; class names compare on 16 characters and global atoms on 31** (124.25.18.1, 124.10). VB calls `GetClassName` then `lstrcmpi` on its MessageBox path. Windows: 255. Estimate none.
- [ ] **WIN16 sends no `WM_NCDESTROY`; `GetClassInfo`'s class styles and `hbrBackground` differ from Wine's; `GetWindowLong(GWL_WNDPROC)` of a built-in answers slot 0** (124.25.18). Estimate none.
- [ ] **Messages WIN16 moves itself still POST to a program's procedure** (focus by Tab, a click, `WM_ACTIVATE`), `w16d_cmsg` answers 0 for `WM_GETDLGCODE` under a replaced procedure, and a program's own window gets no `WM_ENABLE` (124.25.20). Needs the dialog loop's delivery as continuations. Estimate none.

**Memory and selectors (kern_mega)**
- [ ] **An UNLOCKED `GlobalReAlloc` move can discard its own source** (124.31.3; `oddsel-q4audit.md`, both arms, pre-existing). Mark the block in `gdold`/`gdiscard` while it moves. Estimate none.
- [ ] **`GlobalWire` is `GlobalLock` and `GlobalFix` a no-op, so a wired block moves on `GlobalReAlloc`** (124.31.3). A pin bit (`.canmove`) refuses if a target needs it. Estimate none.
- [ ] **`GMEM_MODIFY` with a SELECTOR of a moveable block answers as for a fixed block** (`.mfx`; 124.31.3). Resident bytes, none stated.
- [ ] **The SUB fallback and an unlocked tiled block's move leave the program a selector that does not follow its block** (124.31.3, stated). Estimate none.
- [ ] **`AllocCStoDSAlias` of a CODE selector still returns a SUB**, so a program that `PrestoChangoSelector`s that alias in place gets 0 (124.31.6, KERN-MEGA-PLAN 14.142). `.mega` has 10 bytes left in its rung.
- [ ] **FS/GS are not reloaded after a heap move** (124.25.19, open in the SPEC), and `LocalCompact`/`LocalShrink` grow nothing.
- [ ] **A compaction can move a block during `SEG_BWIN`/`SEG_AT` before the window is bound** (50.6.11, KERN-MEGA-PLAN 14.139). A few bytes; fits the padding.
- [ ] **`mx_cpflags` holds the rate hook only for XMEM-owned `MX_FWIN` blocks, not claims** (comment says claims too; decide the intent). 50.6.
- [ ] **`mega_fbown`'s owner test is `test al,0x50`**, equivalent only while no owner class is `20h|x` (124.31.11, 14.140). Assert the encoding table or restore the explicit compares when one is added.
- [ ] **A fault close of a multi-program WIN16 instance closes all its programs; Windows closes one task** (124.31.11). Estimate none.
- [ ] **The `.mega` rung has 10 bytes left** (KERN-MEGA-PLAN 14.142): the next kern_mega change of more than 10 bytes crosses a rung and needs a size pass or a move to `.cold` or a module first.

**The library part and part sizes**
- [ ] **Split the library part (`.w16dlg`)**: 64,977 of 65,536 on kern_mega (559 left; 64,163 real). Dialog and edit code into a second part is the durable fix; making the `w16e_*` calls to `w16_mheap` indirect also removes the nearest real wrapping transfer (7,108 bytes away). Nothing may be added to part 3 until it is split. Estimate none.
- [ ] **Part 11 is 1,048 bytes and parts are claimed in whole KB, so it takes 2 KB** (124.25.16): trim 24 bytes or accept.
- [ ] **Part 5 (the loader) grew 788 bytes with `thunk`** against an estimate of 150 to 250, and its kept copy is paid per program with a discardable segment (124.26.14). Trim candidate.

**Sir Hangman and the installers**
- [ ] **The engine's `Not enough memory for its code`** (`w16_r_code`, about 14 s in; SPEC.md 124.26.11-12). Capture the segment, its size and the free heap first (one boot with heap counters). Estimate 50 to 150 part bytes and 0 to 20 resident, a guess.
- [ ] **DDEML's `PROGMAN` conversation routed into 124.33.2's parser** (124.25.26). 300 to 500 part bytes and a conversation table. `DdeConnect` was never reached by the reference set; "no server" ships.
- [ ] **CTL3D's `WH_CBT` hook firing** (124.25.22). Set by `SetWindowsHookEx` type 5; nothing calls the hook yet. Estimate none.
- [ ] **`thunk` and the far-pointer data fixups** (124.26.14): 26 of `_INST16.EXE`'s 64 discardable segments (240,345 bytes) are named from DGROUP as far pointers and stay kept; thunking them withdraws the "no data fixups" line and is an owner decision. Only relevant if the installer could run on `kern_big`, where the launcher's own test stops it, so parked.
- [ ] **`_INST16.EXE` on `kern_big` after 28 kept segments: *Its System font could not be read*** (undiagnosed; probably the heap, about 450 KB of 640 KB). Check free memory at that point or boot `TINY16` on the same disk.
- [ ] **`OpenFile` does not search the EXE's folder or PATH; `VerFindFile` does not create `WIN16\SYSTEM`; the Windows directory is spelled two ways; `GetSystemDirectory` re-walks every call** (124.20.5.1, 124.21.3). Estimate none.
- [ ] **`AccessResource` reads the program's file for a library's resource; `LoadModule` of a DLL name starts it as a program** (124.23). Pre-existing. Estimate none.
- [ ] **A library-of-a-library found in a folder is not re-looked-for beside the program; `GetModuleFileName` on a library handle prints the program's folder** (124.23.1.1). Estimate none.

**Menus, windows and GDI**
- [ ] **`TrackPopupMenu`: whether Windows 3.1 sends `WM_INITMENU` is unverified (Wine does); no menu loop sends `WM_MENUSELECT`; 1bpp, mouse-release and bar-repaint fixes and the part-fetch failure path were never seen** (124.25.16). Estimate none.
- [ ] **Paintbrush: the outlined brush stroke did not reproduce** (two boots; every `Polygon` filled). Record `Polygon`'s arguments, `w16_brush` and `w16_rop` when it happens. Options > Brush Shapes draws its six buttons as tiny open-bottomed squares (owner-drawn?). 124.25.14.
- [ ] **Centipede's black field after *One*** (`bigbmp`'s seam at row 360 crossed by its 270x360 `BitBlt`; `BIGBMP`'s `bltseam` covers it). Look at it on the integrated tree. 124.25.17.
- [ ] **`Get/SetBitmapBits` and `CreateBitmap`'s initial bits reach a tiled bitmap's first tile only** (124.25.17). A huge-pointer program needs a per-tile loop. Estimate none.
- [ ] **`MEMDC.EXE` faults on `kern_mega` (`0D` in WIN16's code, `122C:8896`, not on an instruction start in any WIN16 section), with the old WIN16 too** (pre-existing). Needs a MEGATRAP or Bochs read of the selector's base. `win16megamemdc` is registered knowing it.
- [ ] **`memdc`'s colour legs fail on a 1bpp adapter** (`setdib bm2scr bm2scrdev wflood wfloodbig paldib`; pre-existing). Is `win16memdc` VGA-only by design, or is the 4bpp DIB and flood path wrong on 1bpp? 124.25.14.
- [ ] **`EnumProps` passes the atom for string-named properties; 16 atoms are shared with clipboard formats; `CloseWindow` is a stub** (124.25.13). Raising `W16_NATOM` costs only part bytes.

**Names, tools and rows**
- [ ] **A resource or class name past 31 characters still fails** (124.10; Windows allows 255) and `asterr` added no fixture with a long-named dialog. Cheap.
- [ ] **Sound with a Sound Blaster on the Arcade games is untried**; the rig had no card.
- [ ] **A program ending from a part by another path** (`w16_endshow`, the `ntk >= 2` `w16t_end` path via the task part) still frees from inside the same `ICALL` (124.8.3). A fixture for "a part's ICALL ends the program" on kern_mega, and a Bochs leg for `astexit`; `win16hiber`'s new Alt+F, X leg has not run.
- [ ] **The WIN16 trace misses a program's own exit** (`w16_i21` takes `AH=4Ch` before its trace point; record 4Ch with AL), and the kern_mega ring is 64 entries (a bigger one costs `APP_MAX_SIZE`, about 100 bytes of limit). 124.17.4.
- [ ] **`apps/browser/brdownload_part.asm` includes `build/brdownload_part_symbols.inc` by a hard-coded path**, so a `BUILD=<dir>` tree in a worktree with no `build/` fails. Fix is an `-I $(BUILD)/` include; not WIN16's.
- [ ] **kern_mega rows for what only `kern_big` runs**: a library's DGROUP relocations (Q3: `win16dll` is real-mode only), `GlobalReAlloc` of a moveable block by its selector (U1), a callback whose CS is an odd conforming selector, `lheap`'s heap move and stack guard, the class table's growth path (`win16ctlproc`), `win16dgroup`'s `GMEM_MODIFY` fix, and a `kern_mega` Bochs run of `BIGBMP` (the five `win16mega*` rows cover some and none had run). `win16megamenu` is the first Bochs row that sends keys; a red may be the key injection (`--keep` leaves `end.png`).
- [ ] **Built and never run** (for the final pass to name): `win16bigbmp`, `win16csalias`, `win16dgroup`, `win16dosenv`, `win16lheap`, `win16props`, `win16dde`, `win16np` (Bochs, never run), `win16thunk`, `win16mscdex` (re-run with the corrected `150Dh`), `FloodFill`'s `memdc` leg.

## 5. Sources

As WIN16-ANY-PLAN's, plus #327's record:

- the Phase B/C report: its milestones 30–51;
- its SPEC sections: 124.26.10–124.26.11, 124.31.2–124.31.3, and 50.6.11 (D2's lent stack);
- KERN-MEGA-PLAN 14.136.
