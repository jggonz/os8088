# msp2 agent "pmmem" - the pool's tables, and what only protected mode reads

kern_mega size pass 2 (BRIEF.md), agent **pmmem**: `kernel/mega.inc` lines
8036-end at the base `ead16c61`, and every `mx_tab`/`MX_*` access site in any
file - then, on the owner's instruction, **every `mx_tab`/`MX_*`/`mem_sel`
site in every file**, pmfd's range and shared's files included. Branch
`msp2-pmmem`, two rounds. The headline was msp1/pmmem.md DEFERRED 3: the
**1,184 bytes of `.lowbss`** - `mx_tab` (960), `mega_wtab` (128), `mem_sel`
(64), `mega_ustab` (32) - that only protected mode reads and that every
kern_mega machine carried in conventional memory for ever, a refused one
included. **All 1,184 are above 1MB now**: the TABLES' TAIL, past the IDT's
last gate in the tables' block.

## BASE AND TIP

`tools/kernsize.py --build build/megak --json`. Three points: the pass's base
`ead16c61`, the coordinator's merge `b95e1ca8` (`mega-size-2`, every other
agent and this branch's round 1 in it), and this branch's tip.

| kern_mega | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.mega` (+scratch) | `.boot2` | `.ovl` | `KERN_SIZE` at boot | resident after the switch | conventional for ever |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `ead16c61` (pass base) | 46,579 | 5,427 | 46,165 | 6,751 | 336 | 21,440 (+1,490) | 6,709 | 5,118 | 134,656 | 65,024 | 59,093 |
| `b95e1ca8` (the merge) | 46,308 | 5,428 | 45,788 | 6,593 | 336 | 21,120 (+1,510) | 6,739 | 5,118 | 133,632 | 64,512 | 58,665 |
| tip | 46,302 | 5,428 | 45,863 | 5,569 | 336 | 21,280 (+1,510) | 6,739 | 5,118 | 132,608 | 63,488 | 57,635 |
| **tip - merge** (round 2) | **-6** | 0 | **+75** | **-1,024** | 0 | **+160** | 0 | 0 | -1,024 | -1,024 | **-1,030** |

**This agent's whole contribution** (round 1, measured against `ead16c61`
alone, plus round 2): `.lowbss` **-1,184**, `.text` **-6**, `.cold` **+75**,
`.mega` **+192** - conventional memory **-1,190**, code above 1MB **+267** -
and the tail itself (`MEGA_MXT_SIZE` = **1,216** bytes, whole 64-byte lines)
inside the tables' block, so `[mega_tblkb]` grows by one or two KB of POOL and
never a conventional byte. On mega_tlowf's machine, too small for the block
above 1MB, the block is a conventional claim and the tail rides in it - no
worse than `.lowbss` was. `KERN_SIZE` and the resident footprint crossed rungs
back on the way (-512 in round 1, -1,024 in round 2): reported, not the point.

* **`os88megaseg`'s overhead 3,357 -> 3,351** (`mega_lincopyf`);
  `tests/megaseg.txt` set to the measured **3,351**.
* **kern_big BYTE-IDENTICAL**: `tools/os88rdiff.py kernel b95e1ca8 .`, **4,118
  routines same, 0 at an odd shift**; `kernsize --json` identical to
  `b95e1ca8`'s in every field. Every kernel line touched is inside `%ifdef
  KERN_MEGA` (`mega.inc`, `megados.inc`, `hiber.inc`'s mega arm, `fsx.inc`'s
  `.mega` twins, `memory.inc`'s `mx_shedf` and `mem_val`), so **kern_small and
  kern_emu assemble none of it** (`make`'s `kernsize[small]` sections line is
  identical to `b95e1ca8`'s).
* Gates: `make -j1 mega`, `make -j1` (fast tier **68 passed** - `stkbalance`,
  `ovlchk` and `hotpar` among them), `python3 tools/checkdocs.py`.

## TAKEN

### 1. The tables' tail and its selector (round 1)

The tables' block is the GDT, the LDT on it and the IDT after them, sized per
machine by `mega_layoutf`. The tail is past the IDT's last gate, so it is
carried by everything that already carries the block, with three constants
changed and no new mechanism:

* `mega_layoutf` sizes it in (`MEGA_TBL_TAIL` = `MEGA_IDT_SIZE` +
  `MEGA_MXT_SIZE`) - still a pure function of `[mega_extkb]`, so a park's
  anchor and a hibernate's tables are found where this kernel put them;
* **a hibernate's run 0** is `[mega_ldtend] + MEGA_TBL_TAIL` (`hbm_xplan`), and
  `hbm_xruns`'s exact-size test agrees - an older image is REFUSED at resume,
  never resumed with a tail it did not save;
* **the DOS park's sum** covers it with the IDT's last region, whose lines
  ended exactly at the IDT's end: `17 + MEGA_MXT_LINES` (`mega_dsave`) - no new
  region, so `MPK_RMAX` and the park's header do not move.

`MEGA_SEL_MXT` is a GDT data selector at `MEGA_GDT_SCR - 3` (the zero padding
before the scratches; an `%error` keeps it above `MEGA_GDT_ID`), its limit
exactly the tail. `mega_mxtinit` aims it and zeroes the tail first thing in
`mega_pmtail` - after `mega_tset` (the boot) and before `mega_frames_fix`, so
before anything reads a row. Round 1 moved `mega_wtab` and `mega_ustab` (160
bytes); `mega_wnew` became `mega_wset`'s and `mega_segbw`'s one free-row body.

### 2. `mx_tab` and `mem_sel` into the tail (round 2) - `.lowbss` -1,024

**The rule that replaces "through SS"**, stated once in `mega.inc` beside the
layout: every routine that reads a row holds **ES = `MEGA_SEL_MXT`** while it
does (`MXT_ES`; `MXT_ESR reg` where the code is assembled `cpu 8086`), its
caller's ES banked and given back. The pool's helpers that take an entry
(`mx_key`, `mx_tophys`, `mx_tokfit`, `mx_kill`, `mx_unsel`, `mx_dfree`,
`mx_runcntf`, `mx_find`, `mx_inblk`, `mega_xmcls`, `mx_canmove`, `mx_cpflags`,
`mx_cpmove`, `mega_mxat`, `mega_mxbase`, `mega_selfind`, `mega_blkof`) say "ES
= the tail" and are only called holding it. **DS stays the kernel's
throughout** - chosen over a DS convention because a kernel variable read
through the wrong DS would read the tail and be SILENT, where a row read
through the wrong ES faults or reads a descriptor. `mx_tab` and `mem_sel` are
`equ` OFFSETS into the tail now, not labels, so `os88sym.linear()` and every
harness's `sym()` refuse the names (KeyError): a missed reader fails at once.

The routines, by how they take the tail:

* **Self-loading** (push es / `MXT_ES` / ... / pop es): `mx_maxrun`, `mx_caps`,
  `mx_alloc`, `mx_free`, `mx_relall`, `mega_xmsel`, `mx_chk`, `mx_compactf`,
  `mx_packupf`, `mega_xmrunf`, `mega_xmwalk` (its records go to the CALLER's
  ES:DI, so the walk keeps that selector in BP and `.put` swaps it in per
  `stosw`), `mega_rgnblk` (which now hands back CX = the block's KB, so its
  two callers read nothing after it), `mega_rgnconv`, `mega_bclip`,
  `mega_p2d`, `mega_mpnewf`, `mega_mpsyncf`, `mx_shedf` and `mem_val`
  (memory.inc), and brackets inside `mega_hugef`, `mega_hugebf`, `mega_hugec`,
  `mega_hiclaim`, `mega_segbw`, `mega_mpfreef`, `mega_dosclaimf` and
  `mega_dosparkf`.
* **`mega_claimfind` RETURNS ES = the tail** (it calls `mega_ownerf` with the
  caller's ES first): `mega_claimfreef`, `mega_claimkbf` and
  `mega_claimregrowf` bank their ES before it. `mega_claimregrowf`'s
  OSAPI_MEM_HUGE arm had ES = the LDT and read the entry through SS in the
  same lines; it reads the entry first now (the run's first selector into BX,
  whose BL was already spent), works the LDT, then stores through the tail -
  `.hfill` likewise. Its copy arm would have read the claim's selector through
  the SCRATCH selector once ES moved (`mov ds, [es:si+MX_SEL]` after `mov es,
  bx`): read first.
* **`mx_cpmove` RETURNS ES = the tail** (its rebase spends ES on the LDT and
  the GDT between pieces; its contract had said "clobbers ES", and both
  callers re-read the entry after it).
* **`hbm_xplan`** (HIBER.DRV's `.modh`) writes its run list through ES = the
  park's scratch and adds into kernel variables, so it reads each entry's two
  words through a DS swap (~40 clocks an entry, once a hibernate).

**KERN-MEGA-PLAN 14.46, one frame out.** `mega_ldt_free` nulls the CALLER's ES
on its frame when that ES is the selector it frees (PiXEL's Blur and Sepia free
a claim with ES still on it). A routine that banks its caller's ES and frees
under it (`mx_free`, `mx_relall`, `mega_claimfreef`) runs **`MXT_ESPOP`** just
before its `pop es`: `verr` on the banked word, and a descriptor no longer
readable comes back as the NULL selector - the same answer, without the #GP at
the pop that `mega_gpfix`'s `.stale` arm would have turned into it a trap
later. `mega_mpfreef` takes its slot under a short bracket and gives the
caller's ES back BEFORE its two `mega_ldt_free`s, so their own test reads it.

**The real-mode twins** (`fsx.inc`'s `rms_xcaps`, `rms_xchk`) read `mx_tab`
through SS in REAL mode, which cannot reach above 1MB. `fsx_rsvcf` (protected
mode, at the drop) now hands them the two facts they need: `mx_caps`'s answer
(`rms_xkb`, `rms_xbl`) and, by the new `fsx_rxrows`, the bracket instance's own
live blocks as `{MX_AUX, MX_KB, MX_OFF}` rows after the stream runs in the
service area (`rms_xrow`..`rms_xend`), in the table's order - exactly the
entries `rms_xchk`'s owner-and-KB filter used to pass, its one caller passing
`[rms_slot]`. Nothing changes the pool inside a real-mode bracket, so the copy
is exact. The service claim grows by 6 bytes a block the program owns.

`mega_xminit` and `mega_mpall` no longer zero their tables: `mega_mxtinit` has,
earlier, the whole tail at once.

### 3. Dead code and doors

* **`mega_sblk`** (`.mega`, ~70 bytes) had **no caller at all**: deleted.
* **`mega_lincopyf`** (`.text`, 6): its last caller, `fsx_rcp1`, calls
  `mx_lcopy` since shared's merge - deleted on the coordinator's word.
* **`mega_hugef`'s three `MX_FL` ORs as one** (pmfd's CROSS-FILE): the flags
  composed in AL (banked) and stored once.

### Hot paths, and the cost of the segment load

`mx_lcopy`, `mega_lincopy`'s inner copy and everything per-byte are
**untouched**: none of them reads the tail. Per-CALL costs on a 286 in
protected mode (a segment-register load is ~17-20 clocks; with its push and
pop ~45):

* **SEG_AIM (`mega_segaim`, the rate hook's one verb): 9 clocks FASTER**
  (round 1), and its `mega_blkof` -> `mx_inblk` chain reads `mx_tab` through
  the ES it already holds: **no load added**. Its deepest stack chain is as
  round 1 left it (worst case unchanged: a driver caller's `mega_segown`).
* **OSAPI_XMEM_COPY** (`mx_copy`): `mx_chk` loads the tail once a call,
  **~45 clocks** - once a copy whose conventional end is below 1MB, three
  times at most - against the copy's own thousands. The DOS trip's XMS moves
  and the Video Player's ring go through it.
* **A conventional claim, free or regrow in protected mode**: `mem_val`
  (~40) and `mega_mpnewf`/`_mpsyncf`/`_mpfreef` (~45 each), one or two per
  heap operation.
* **Every other pool entry point** (`mx_alloc`, `mx_free`, `mx_caps`,
  `mega_xmsel`, the claim family, the walks): one load each, ~45 clocks.
* **The window and real-segment rows** (round 1): `mega_wfind`, `mega_wslot`,
  `mega_iswin`, `mega_useg` ~40 each.

The one listed hot path in this code (SEG_AIM) got faster; none of the rest
is on a listed path.

### Tests and tools - one place knows where the tail is

`os88sym.TAIL` and **`os88sym.tail(name, read, sym, equ)`**: the tail's linear
base is `[mega_tbllin] + [mega_ldtend] + MEGA_IDT_SIZE`, read off the running
machine, plus the name's `equ`; it raises before the switch (`mega_tbllin` 0).
`Box.tail()` (`tools/os88bochs.py`) and `MegaMixin.tail()`
(`tools/os88megacommon.py`, so MegaBochs, Mega86Box and the API backend all
have it) wrap it. Every reader changed to it, each keeping its meaning -
including the five that WRITE pinned `mx_tab` records through the debugger
(`setpmem` is physical, and with no paging the tail's linear address is):
`tests/dockpos.py`, `hdnoclaim.py`, `megadosroom.py`, `megahiber.py`,
`megahma.py`, `megarehome.py`, `megashed.py`, `mseglazy.py`, `multiseg.py`,
`os88qmpmega.py` (`mem_sel`), `pkgmax.py`, `rehomeabort.py`, `vidclose.py`,
`tools/heapmap.py` (`mx_tab` and `mem_sel`), `tools/megaconvres.py`, and
`tests/megatbl.py` (round 1: the tail's descriptor and the block's size).
**Written, not run.**

### Equivalence, by reading

No row was run (the brief's rule). Read and recorded per site: ES at every
access (the census above is every routine that touches a tail table); every
callee called while ES is the tail keeps ES (`mega_ldt_alloc`/`_free`,
`mega_selbase`, `mega_kblin`, `mem_pg_forget` - "preserves every register" -
`mega_dreg_*`, `mega_iswin`) or loads its own; every routine whose ES is an
INPUT (`mega_segown`, `mega_slotof`, `mega_ownerof`: the caller's) is called
with the caller's ES; `mx_cpmove` and `mx_pcut` spend ES, and the entry is
re-read only after `MXT_ES`. The 286 reset route's reflections run
`mega_rseg` -> `mega_p2d`/`mega_useg`, which bank and restore ES - the 86Box
check below is named for them.

SPEC.md (50.6.11's window rows, 87.6's run 0) and KERN-MEGA-PLAN's head (MC9's
`mx_tab`) say where the tables are now.

## REFUSED

* **The tail through `MEGA_SEL_LDTD` with its limit extended** (round 1): the
  alias is cut to exactly the LDT on purpose, and the IDT sits between.
* **A DS convention for the tail** (round 2): fewer prefixes in the pure walks,
  but a missed kernel-variable override reads the tail through DS silently.
  ES faults or reads a descriptor; DS lies.
* **A 960-byte snapshot of `mx_tab` in the bracket's service claim** for the
  real-mode twins: a bigger claim inside a bracket can refuse a tight machine.
  The two facts they read cost 6 bytes a block of the program's own.
* **Parallel arrays for `mega_wtab`** (`repne scasw`): every row pointer in
  nine routines changes meaning, for speed and no bytes.
* **`mega_xmsel`'s banked window on the stack** (-8 `.mega`, -6 `.text`) and
  **`mega_claimregrowf`'s `mega_xmself` tails as a helper** (-10 `.cold`):
  each +2 bytes under a claim chain on a task slice.

## DEFERRED

Estimates are labelled; pass 3's list.

1. **`mx_tab` walk idiom** (msp1 DEFERRED 5): `mov si, mx_tab / ... / add si,
   MX_BLKSZ / cmp si, mx_tab_end / jb` ~20 times; an `mx_next` would hide the
   segment too. est. -40.
2. **`MXT_ES` at fewer places**: some self-loading routines are only ever
   called by others that hold the tail (`mx_chk` from `mx_copy`; `mega_rgnblk`
   from `mega_rgnsubfree`/`mega_subsegf` could take it held). A near entry that
   assumes it saves ~6 bytes and ~45 clocks each. est. -30 `.mega`.
3. **`mx_maxrun`'s two walks per pinned block** are O(n^2) over 96 entries; a
   sorted pass reads half as much. Speed only.
4. **`pusha`/`popa` in the run-time preserve-everything routines** (msp1
   DEFERRED 6, est. -70 `.mega`): `tools/stkwater.py` on the claim paths first.
5. **`mega_xmsel`'s window on the stack** and **`mega_claimregrowf`'s three
   `mega_xmself` tails** (REFUSED above): -14 and -10, +2 stack each.
6. **`.text` data written before it is read** (`mxs_*`, `mega_xsrc`/`_xdst`,
   `mega_bf*`): ~30 bytes of `kernel.sys`, no resident change.
7. **The tail could take more**: any protected-mode-only table in `.lowbss` or
   `.text` (`mega_pcj`, `mega_fkq`, ...) is one `equ` and its readers' segment
   away. Not surveyed this round.

## WANTS THE OWNER

Nothing changes what anyone sees. To know: a hibernate image written by a
kernel before this change is refused at resume by `hbm_xruns`'s size test (run
0 is 1,216 bytes longer), and a DOS park left standing across the upgrade is
not found if `[mega_tblkb]`'s rounding moved the tables. Both are the existing
cross-build rules working.

## WANTS AN ABI BREAK

* **SEG_AIM's "~40 bytes of stack"** (`apps/os88api.inc`, SPEC.md 53.2.2):
  state the worst case (a driver caller) as a checkable number. Wording only.
* **`OSAPI_SEG`'s 32 window rows** (`MEGA_WTN`, SPEC.md 50.6.11): above 1MB
  the count costs pool, not conventional memory - 64 rows would be ~128 bytes
  of pool and no resident byte; "32" is in the contract.

## CROSS-FILE

Taken in this diff on the coordinator's instruction (every
`mx_tab`/`MX_*`/`mem_sel` site is this agent's): `kernel/megados.inc`
(`mega_dosclaimf`, `mega_dosparkf`, `mega_dsave`'s line count),
`kernel/hiber.inc` (`hbm_xplan`, `hbm_xruns`), `kernel/fsx.inc` (`fsx_rsvcf`,
`fsx_rxrows`, the two twins), `kernel/memory.inc` (`mx_shedf`, `mem_val`),
pmfd's range of `mega.inc` (`mega_p2d`, `mega_hugef`/`bf`/`c`, `mega_mxat`,
`mega_xmrunf`, `mx_runcntf`), and the `MXT_ES`/`MXT_ESR`/`MXT_ESPOP` macros at
the top of `mega.inc` (pmcore's range: a macro must be defined before its first
use). `tests/megaseg.txt` 3,357 -> 3,351.

* **bootonce**: `mega_mxtinit` (`.mega`, 59 bytes) runs once at the switch; it
  can join any init tail that runs after `mega_tset` and before
  `mega_frames_fix`.
* **coordinator**: SPEC.md 98492's `MEGA_TBL_SIZE` row is stale from before
  this pass (no such symbol).

## ROWS TO RUN

Named, not run. **Bochs** - everything that reads or writes the tail or walks
the pool:

* the tables and the switch: `megatbl` (the tail's descriptor and the block's
  size at 1, 4 and 16MB), `megaflt`, `megainst`, `megaown`;
* the pool's claims, frees, moves and walks: `megashed`, `megahuge`,
  `megaxmove`, `megacaps`, `megahma`, `megarehome`, `megatmmem`, `megamod`,
  `brmega`, `mega-scsize`, `mega-dockpos`, `mega-hdnoclaim`, `mega-mseglazy`,
  `mega-mseglzslack`, `mega-multiseg`, `mega-pkgmax`, `mega-rehome`,
  `mega-rehomeabort`, `mega-rehomemove`, `mega-ringmove`, `mega-editmove`,
  `mega-wdheapmove`, `mega-wdparts`, `mega-drvmove`, `mega-heaphi`,
  `mega-heapmap`, `mega-heapcheck`, `mega-postboot`;
* hibernate (run 0 carries the tail): `megahiber`, `megahibfull`,
  `megap2hiber`;
* the DOS trip (the park's sum, the arena's PIN): `megadosp`, `megadosroom`,
  `mega-doscom`, `mega-dosxmsq`, `mega-dosram`, `mega-rdxms`;
* the real-mode bracket's twins (`rms_xcaps`, `rms_xchk` off the copied rows):
  `megavidbig`, `megavidring`, `megavidtier`, `mega-vidvga4`,
  `mega-vidmodex`, `mega-vidclose`;
* windows and ROM segments (`mega_wtab`, `mega_ustab`, `mem_sel`):
  `mega-c64part`, `mega-c64move`, `megapixel`, `megafont`, `megarmb`,
  `mega-skiesworlds`, `mega-pxfolder`;
* the rows whose harness reads the tables through `tools/heapmap.py` or
  `tests/os88qmpmega.py`: `mega-ethernet`, `mega-httpinstall`,
  `mega-httpmount`, `mega-ps2mouse`, `mega-recorder`, `mega-vesa`,
  `mega-vesa8`, `mega-vesablit4`, `mega-vesacp`, `mega-vesafsx`,
  `mega-vesashut`, `mega-wheelps2`, `mega-wirezone`.

**86Box** (`vm/286-mega`): boot to the desktop, play a Video Player clip,
start the C64 and open Paint - the 286 reset route's reflections run
`mega_rseg` -> `mega_p2d`/`mega_useg` on the back half of every BIOS call that
hands back a segment, and the claims, windows and frees all go through the
tail. Any FAULT line is this change.

## DEFECTS FOUND

* **`mega_wtab` was never zeroed** (round 1): `.lowbss` is not zeroed at boot
  and nothing wrote it before its first read, so a warm restart (`int 19h`
  keeps RAM) or a dirty BIOS handed OSAPI_SEG stale rows naming selectors the
  new LDT hands to somebody else - `mega_iswin` could take a SUB for a window
  and leave it unfreed with its block. Fixed by the move (`mega_mxtinit`
  zeroes the tail).
* **`mega_sblk` was dead code** in `.mega` (no caller): deleted.
* SPEC.md 98492's table entry is stale (CROSS-FILE).
