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

- [ ] **L2: Sir Hangman's install, a known limit in S25.**
  - Sir Hangman 1.0 is the InstallShield 3 installer, O3.
  - Its engine, `_INST16.EX_` expanded to `_INS0433._MP` (542,720 bytes, module ISINST30), has 607 selector fixups by segment number into its 64 discardable code segments. So `w16l_keepck` keeps all 64, 404 KB, and a launch from the launcher's task refused with "Not enough memory to load it".
  - Started as the first program, from a claim of its own, the engine loads.
  - The routes:
    - a later program's segment block as a claim of its own, as the first program's is;
    - O8's `#NP` reload (L1), which would cover it too.
  - The program itself, `HANGMAN.EXE`, is VB 3.0 again, so after the install it is L1's question.

## 4. Open items found on the way (not blocking #327)

Each was recorded in the Phase B/C report and not yet closed. Re-check each one against the merged tree before starting it.

- [ ] **Q1: a protected-mode fault in program code takes the machine down.**
  - With DS = SS = the program's DGROUP, `mega_fown` does not attribute the fault to WIN16. The result is "FAULT 0D … reboot", where it should close the package.
  - The kernel has to recognise a WIN16 program's selectors as WIN16's: the LDT range WIN16 owns, or a registered owner word in the shape of D2's hook.
  - **Seen under Bochs in N3 (#327, `win16ovr`).** OVRTEST's write one byte past its block raises the #GP that N1 promised. But `[mega_infault]` = 1 and the machine stops, where Windows would close the program.
  - **Why the kernel cannot tell who owns the fault.**
    - `mega_cs2ds` assumes an image's code/data selector pair, with the data selector 8 above the code selector.
    - `mega_fown` then searches conventional paragraphs below A000.
    - A WIN16 program's CS is a SUB or tile selector over WIN16's memory, which has been above 1 MB since S22 batch 9. So neither test finds an owner.
  - **The proposed fix** is to attribute a CS that is not an image pair by its descriptor's BASE, the way 14.137's `mega_bown` does: a pool block's `MX_OWN`, or `mem_tab`'s `MC_OWN` below 1 MB. It needs its own owner rows.
  - **Check, not yet done:** whether the +8 assumption can blame a fault on whichever package owns the selector 8 above the faulting one. If it can, it would close the WRONG package, which is worse than halting.

- [ ] **Q2: real mode's STACK OVERFLOW TASK 00 (UI task)** during `win16xvol`'s copy across volumes on `kern_big`.
  - The copy still finishes and reports XVOL OK.
  - It is not SPEC.md 96.11.11's doing. It was seen with a core that never asks DFREE, and with the older core.
  - It is undiagnosed, and it matters for the real-mode installers.

- [ ] **Q3: MSCDEX's `int 2Fh` is deferred** (SPEC.md 124.33.3).
  - It costs about 100 image bytes, and no target has yet been seen to ask for it.
  - Mozart's CD-ROM run (L1) is where the question is answered.

- [ ] **Q4: the rest of S25's acceptance.**
  - Arcade's five games are 386 code over `WAVEMIX`, started from their links.
  - Mozart's Red Book tracks play through S23's `cdaudio`, on 86Box's `286-mega-cdrom` and `386-mega-cdrom`.
  - What of all three runs in real mode, where their CPU tier allows.
  - `tools/w16reach.py` re-run against WIN16-ANY-PLAN §1.

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

- [ ] **Q7: `SEG_BWIN` checks no owner** (SPEC.md 50.6.11). Found while fixing 14.137 on #327. `mega_wset`'s re-bind now asks `mega_bown` (a claim's `MC_OWN`, a pool block's `MX_OWN`), but `SEG_BWIN` itself binds a window into any block it is given. That is 14.135 item 2's class of hole, one verb along. The fix is the same test at `SEG_BWIN`'s entry. Every shipped caller binds its own block, so the fix should refuse none of them. It is a kernel `.mega` change, so it goes in its own PR, with megaown's row.

- [ ] **Q8: `w16censall` fails on main as well as on #327** (`tests/unit/t_w16cens.py`). It fails for Windows 3.1 Media Player's and Sound Recorder's segment 1, which were added to `win16ref/samples/w31mm`. The guest walk reaches 1,138 and 1,108 bytes that the host's does not.
  - The cause is a divergence on overlapping decodes. `tools/os88ne.py` reads MPLAYER's switch table at 0x87C one word past its end, which seeds 0x16A, inside `sub ax,7` at 0x169. The guest stops before that word, and from there the two walks reach different functions.
  - It reproduces identically on main's d323a9a65, so it is not Phase B/C's doing.
  - The fix is to bound the host's table read the way the guest does, or to decide which walk is right and make the other match. Then re-run the row.

- [ ] **Q9: WIN16's pinned claims on the arm** (`tests/pins.txt`, bucket N). `@w16_vclaim` holds a program's segments, arenas and big bitmaps as map claims, and `w16_lseg` is D2's lent BIOS stack. Neither can move, because the program holds selectors and the kernel's hook word holds the stack.
  - **To let `@w16_vclaim` move:** a relocation proc that re-bases every descriptor and the map row.
  - **To let `w16_lseg` move:** a proc that re-registers the hook word.
  - Both are walls in the arena until then (NO-PINNING-PLAN 0).

## 5. Sources

As WIN16-ANY-PLAN's, plus #327's record:

- the Phase B/C report: its milestones 30–51;
- its SPEC sections: 124.26.10–124.26.11, 124.31.2–124.31.3, and 50.6.11 (D2's lent stack);
- KERN-MEGA-PLAN 14.136.
