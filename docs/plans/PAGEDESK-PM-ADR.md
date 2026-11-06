# PAGEDESK-PM-ADR - a protected-mode PageDesk? (PD-27)

Architecture decision record, 2026-11-05. Author: the `host` agent of wave 2.
**Status: proposed - the owner decides.** Exploratory by the assignment's own
word: no code in the package, nothing here is shipped, and a planned edition is
never listed as shipped.

*Verified 2026-11-05 by verify-host (Opus):* every kern_mega fact below was
checked against KERN-MEGA-PLAN.md and SPEC.md at the sections cited; one was
out of date and is corrected (a claim over 64 KB above 1 MB is a run of
selectors today, not conventional: SPEC.md 50.3.5, plan 14.37); the cache
and display-list sizes are the wave-2 ones; the licences are read from each
engine's own file (2.2); and section 5's numbers were re-run and reproduce.
The recommendation follows from that evidence and is unchanged.

PD-27 asks four things: does os8088 have a *supported* protected-mode
application contract; what would PageDesk gain from one; what would it cost;
and, if there is no safe runtime, what are the blockers and the follow-up. The
constraints are the assignment's: a 286 has a different execution and
transition model from a 386 (no single assumed 32-bit runtime); no CR0 switch,
no unreal mode, no scheduler replacement and no DOS extender inside a PageDesk
paint callback; a protected-mode build declares its own minimum CPU/RAM and is
never selected by accident on an XT; and it does not block or weaken the XT
release.

## 1. The answer in six lines

1. **There is a protected-mode runtime, and PageDesk is already written for
   it.** `kern_mega` (docs/plans/KERN-MEGA-PLAN.md, SPEC.md 20.18) is a 16-bit
   protected-mode kernel for 286 and later: identity-mapped first megabyte,
   packages above 1 MB, claims above 1 MB, faults that close the package. It
   runs most packages unchanged, and PageDesk was built to (section 2.3).
2. **What it does not have is 32-bit code, a flat address space, ring 3 or a
   second way back to real mode.** A "386-only 32-bit PageDesk" has no runtime
   to land on, today, and building one is a kernel project, not a PageDesk one
   (section 4, option C).
3. **PageDesk is not short of conventional RAM at its own ceiling.** The
   contract's peak is 224 KiB of a 640 KiB XT's ~534 KiB heap, with over
   300 KiB unclaimed (PAGEDESK-SPEC 16.1). What stops a bigger block cache or
   display list today is a constant in the code - the engine's cache is cut at
   32 blocks (20.2.2; 64 KiB at a 2 KiB block, 128 KiB at a 4 KiB cluster) -
   not the machine. The first, cheapest gain therefore needs
   neither extended memory nor protected mode (section 3).
4. **What protected mode would uniquely add is memory above 640 KiB used in
   place** (selectors instead of the 32 KiB-a-copy `OSAPI_XMEM_*` window) and
   fault containment. Both are real and neither is needed by anything on the
   required path.
5. **Recommendation: do not build a protected-mode edition.** Ship the XT
   release; do PD-25 (Enhanced real-mode caching and Flate) first; verify on
   kern_mega that the shipped PageDesk runs there (it should; nobody has
   looked); and only then decide whether a narrow `PKG_MEGA` arm that places
   caches above 1 MB is worth its gate. Revisit the idea of a 32-bit engine
   only if the kernel grows a 32-bit program class (section 6).
6. **What was and was not measured.** One cheap host measurement is in section
   5; no kern_mega boot was done (there is no Bochs in this worktree, and the
   brief forbids emulator rows), so every statement about kern_mega below is
   from the plan and the SPEC and is marked as such.

## 2. What os8088 offers today

### 2.1 The runtime

From KERN-MEGA-PLAN.md (sections cited) and SPEC.md 20.18; not re-measured here.

| fact | where |
|---|---|
| `kern_mega` is the protected-mode kernel; the floor is a **286** (decided 2026-10-17); resident 68 KB of conventional memory; it boots on a 1 MB machine's 640 KB | plan 8; CLAUDE.md |
| 16-bit protected mode, **ring 0**; the first megabyte is mapped by identity, so every baked segment constant keeps working; code segments and everything above 1 MB live in the LDT | plan 2.5, 3 |
| **no 32-bit code, no flat model, no FS/GS, no V86, no paging, no ring 3, no task gates** in the first build; a 286 leaves protected mode only by resetting the CPU (412 us a BIOS call on the Victory 286), a 386 by CR0 | plan 1, 4; MEGADIAG toast names the route |
| the kernel, the BIOS reflection, DOS (run *beside* the kernel for the length of a bracket) and the hibernate are the only places the mode changes, and they are the kernel's | plan 2.2, 2.6, 2.7 |
| a package opts into the high region with `OS88_HIGH` (its image) and `OS88_HIGHMEM` (its claims); a claim is a pool block above 1 MB under its selector(s), and the compactor may MOVE any of them at any instant, rebasing the selectors (no relocation proc; "my segment + N" is `OSAPI_MEM_SUBSEG`). A claim over 64 KB is one pool block under a RUN of LDT selectors, a 64 KB window each, stepped by the CX that `OSAPI_MEM_HUGE` answers (8; a window every 32 KB with BX = 8000h). Plan 13.6's "a claim over 64KB stays conventional" was true when written and was overtaken by 14.37 and the memory round | plan 13.6, 14.37, 14.41; SPEC.md 50.3.5, 50.3.6, 50.6.12 |
| `OSAPI_MEM_AVAIL` answers for the whole machine; `OSAPI_XMEM_*` and `OSAPI_XMEM_RUN` reach extended memory; `OSAPI_MEM_CODESEL`/`OS88_CODESEL` turns a claim into a callable code selector | SPEC.md 20.18, 50.6.12 |
| a fault in a package closes the package, not the machine (a #GP names the CS:IP in a toast) | plan 14.42 |
| a package that is real-mode by design refuses on kern_mega in its own words (`OS88_MEGA_REFUSE`, WIN16, the Video Player's shipped build); kern_mega pairs with its own release's apps disk | SPEC.md 20.18, 124 |
| `kern_big`, `kern_emu` and `kern_small` are real-mode kernels; an 8086 never sees any of this | CLAUDE.md |

Short of protected mode, a 286+ gets real-mode extended memory through
`XMEM.DRV`, `EMS.DRV` and the HMA (SPEC.md 41, 2.10.7): a data store copied a
32 KiB window at a time, no executable code, no selector.

### 2.2 The toolchain and the licence ground

- The C toolchain (`tools/cc8086.py` over SmallerC) emits and **refuses
  anything above the 8086**, and its four rules (no `&local`, no string
  instructions, no `long`/`float`/`double`, 96-byte frames) are what make code
  host-testable and 8088-safe. A 32-bit engine is not expressible in it; it
  would need a second compiler, a second runtime (`crt0`) and a 32-bit
  descriptor kernel service that does not exist.
- A mature PDF engine would be a port with a licence and a size; the size is
  not assessed here. The licences were read from each project's own
  published file on 2026-11-05 (default branch, so a port must still pin a
  release and re-read it; first 16 hex of the SHA-256 of the fetched file):

  | engine | licence, as its own file says | source read |
  |---|---|---|
  | MuPDF | GNU **Affero** GPL v3 or later; commercial licence from Artifex | `ArtifexSoftware/mupdf` `COPYING` (57c8ff33c9c0cfc3) and `README` (7c84deefa852ec45) |
  | Poppler | **GPL** (not LGPL): "programs which call Poppler must be licensed under the GPL as well" | `poppler` `README.md` (3a6d2dfa541596d6) |
  | Xpdf | GPL v2 and GPL v3, dual: v2 only, v3 only, or either | xpdfreader.com `opensource.html` (af690aa560f88866) |
  | PDFium | BSD 3-clause (`The PDFium Authors`), the file also carrying the Apache 2.0 text | `pdfium` `LICENSE` (beef23306e826b03, base64 as gitiles serves it) |
  | qpdf | Apache 2.0 - a rewriter, not a renderer | `qpdf/qpdf` `LICENSE.txt` (cfc7749b96f63bd3) |
  | PDFio | Apache 2.0 with optional exceptions (embedded object form; GPLv2 linking) - a reader/writer, not a renderer | `michaelrsweet/pdfio` `LICENSE` (cfc7749b96f63bd3, the same Apache text) and `NOTICE` (7f08e9e1433efb26) |

  The tree already carries GPL-2-or-later packages (`apps/apple2`,
  `apps/win16`) so copyleft is not automatically a blocker, but AGPL (MuPDF)
  and a commercial option are owner decisions, not defaults; PDFium is the
  permissive renderer of the set and also the largest C++ tree. Every one of them assumes `malloc`, `stdio`,
  floating point and a flat 32-bit address space, none of which exists on a
  16-bit protected-mode kernel without an FPU requirement.

### 2.3 What PageDesk already is, on kern_mega

PageDesk's own source was written with the runtime in view (read from
`apps/pagedesk/pagedesk.asm`, `pdui.inc` and `pdumod.c`, not run): the class
byte is `OS88_STACK_128 | OS88_HIGH | OS88_HIGHMEM` (`pagedesk.asm` line 42);
the primary does no arithmetic on its own segment or on a claim's - the
document claims are carved with `os88_seg_sub` (`OSAPI_MEM_SUBSEG`, SPEC.md
50.3.6: a selector on kern_mega, segment + paragraphs elsewhere,
`pdumod.c`); every module is reached through a vector
whose segment `OS88_CODESEL` turns into a code selector; every claim is handed
to the modules as an opaque segment; data crosses only through the exchange
block and claims by far copy (PAGEDESK-SPEC 3.2, 4.3). Two things have **not**
been checked and are the first spike (section 6): that it runs on a booted
kern_mega at all, and what the shipped build's `OS88_HIGHMEM` claims cost in
pool moves. PAGEDESK-SPEC 3.4 pins every claim, but pinning is a real-mode
heap property: above 1 MB a claim moves under its selector at any instant the
compactor runs (plan 14.15, 14.37; SPEC.md 50.3.6), which is safe exactly as
long as nothing in PageDesk keeps a LINEAR address or a raw `segment + N` -
the carve above says it does not; spike 1 is what would show it.

## 3. What PageDesk could gain, against what it already has

PAGEDESK-SPEC 16.2's claims, and the real-mode ways to spend memory first:

| candidate | today | real mode, any 8086+ with RAM to spare | what protected mode adds |
|---|---|---|---|
| block cache | 16 KiB, 32 blocks max (20.2.3) | raise the block count in the engine and claim 64-128 KiB from the unused conventional heap (the XT has 300 KiB unclaimed at peak) | blocks above 1 MB: only matters beyond the first ~128 KiB, and the cache indexes would need a segment per window |
| page/object index | a 256-subsection table, 20-byte entry reads (7.3, L4) | a larger resolved-offset cache (512 entries, 20.2.2) | a whole-file object index in RAM for a >1000-object file |
| display list | 29 KiB: 22 KiB list + PDREND's 6,912-byte workspace (D13, `pdumod.c`), overflow re-scans a narrower row range (D4, 10.2) | a bigger list from the same unused heap; fewer re-scans | none that real mode lacks: the list is addressed in one 64 KiB segment either way |
| glyph and image caches | 24 KiB, the image row buffer and strike cache carved in it (20.3.4) | same, larger | same |
| thumbnails | 4 KiB | same | many pages' thumbnails kept at once |
| undo / edit store | 16 + 4 KiB (wave 2) | a bigger claim; spill to disk (journal, 14.7) | spill above 1 MB instead of disk (PD-25's `OSAPI_XMEM_*` does this in real mode already) |
| Flate window | not in the XT build (PD-25) | `OSAPI_XMEM_*` store, 32 KiB window | none |
| whole documents in RAM | never | 8088: a floppy page-in at ~400 ms a call | a 100-page document held above 1 MB |
| engine capability (a mature PDF engine) | no | no | **only if** a 32-bit runtime existed (section 4C) |

Read left to right: almost every gain a protected-mode PageDesk would have is
first available as a *constant* in real mode, from conventional heap the
contract already leaves unclaimed, and the rest (a large document held in RAM,
spill above 1 MB) is what `OSAPI_XMEM_*` and PD-25 already cover without a
mode switch. What only protected mode gives is **in-place** addressing of
extended memory (no 32 KiB copy through a window) and a hardware fault in
place of a wild write.

## 4. Options, with the alternative each is judged against

**A. Real mode everywhere; PageDesk unchanged on kern_mega; enhanced caching
and Flate through PD-25 and larger constants.** Judged against B and C: it
costs the least, keeps one binary and one source for 8088, 286 and 386, and
every claim in the table above that is not "in place above 1 MB" is delivered.
Risk: whatever kern_mega's pool does to a package it did not know about (not
measured, section 6). Cost: PD-25's own (a Flate decoder shared as source from
`apps/pixel/pxpng.asm`, L6; an XMEM abstraction).

**B. A narrow `PKG_MEGA` arm of the same C source (SPEC.md 20.18): caches above
1 MB, selectors where they pay.** Judged against A: it spends 64-512 KiB of
extended memory in place instead of 32 KiB copies, at the price of what an arm
is - a second build of the package on the `$(MEGAPKGS)` list, `t_megapkg`'s
registry, a pairing with kern_mega's own apps disk, and an engine whose cache
and display list are **no longer one segment** (a block would be a
(selector, offset) pair and the 20.2.3 arithmetic would grow an indirection on
the 8088 path or be compiled twice). Its reach is the stepping: a claim over 64 KB
is a run of selectors the engine must walk by the CX `OSAPI_MEM_HUGE` answers
(SPEC.md 50.3.5), which is the indirection above, not a refusal. The size of that win over A is **not measured**; do not
take it without a number (section 6, spike 2). Minimum CPU declaration if
built: a 286 (`CPU_F_PM` from `OSAPI_CPU_INFO` at run time, never by probing),
extended memory of at least the arm's declared cache plus the kernel's; refuses
or runs the shipped build on every other kernel.

**C. A 386-only protected-mode edition: a 32-bit engine (a ported mature
library, or PDENG/PDREND recompiled 32-bit), as a separate package.** Judged
against A and B: the only option that changes what PageDesk can *render* (fonts,
transparency, JPEG/JBIG2 without a host step). It needs what does not exist:
a 32-bit program class in kern_mega (the plan's first build forbids any 32-bit
code, section 4), 32-bit code descriptors for a package's CS, a 32-bit
toolchain and `crt0` next to SmallerC, a libc subset, and probably a soft-float
decision for a 386 without a 387; plus a licence decision (2.2), a test bench
(Bochs and 86Box only: MartyPC is an 8088 and QEMU hides 286 mistakes, plan 9)
and a port nobody has sized. It would be a project on the scale of kern_mega itself. It
**cannot be justified by PageDesk alone**; if it were wanted, it is a
KERN-MEGA-PLAN wave first. Rejected for now.

**D. Host preparation only (PD-22's `tools/os88pdprep.py`, built in this
wave).** Not an alternative to the others but the substitute for C's one
unique gain: a modern host rewrites or rasterises the file so an 8088 never
meets the feature (PAGEDESK-SPEC 20.14). It has its own cost (a picture page is
not vector text) but it works on every machine and needs no protected mode.

**E. A 386 "unreal mode" or a DOS extender under PageDesk.** Excluded by the
assignment (and by the kernel's rule that the mode is the kernel's). Listed so
the answer to "why not" is written down: a paint callback is on the UI task
inside the scheduler's accounting, an interrupt can arrive in a window the
mode change does not cover, and kern_mega's reflection already pays for the BIOS.

### Summary

| | one source for 8088/286/386 | declared minimum | changes what can be rendered | new kernel work | new test bench |
|---|---|---|---|---|---|
| A (PD-25, constants) | yes | none (tiers by `OSAPI_XMEM`) | Flate only | none | none |
| B (`PKG_MEGA` arm) | yes, a second build | 286 + extended RAM | no | none | kern_mega apps disk, Bochs |
| C (32-bit edition) | no | 386 | yes | a 32-bit program class | a new row family on Bochs/86Box |
| D (host prep, built) | yes | none | by a host step | none | none |

## 5. The cheap measurement (host harness; no emulator)

A spike was permitted "only if cheap and honest". The question it answers is
the one section 3 turns on: *does a larger block cache buy fewer disk calls?*
`build/pde_test dump FILE -k <KB> -c 4` prints the READ_SEQ calls `OPEN` made
(PAGEDESK-SPEC 20.2.1: at ~400 ms a call on a floppy, cost disk work in
calls). The stress set is `tools/os88pdfgen.py --stress`; 4-sector clusters;
the engine cuts the cache at 32 blocks.

| file | size | 8 KiB | 16 KiB | 32 KiB | 64 KiB |
|---|---|---|---|---|---|
| BIGFILE.PDF | 722 KB | 2 | 2 | 2 | 2 |
| SPARSE.PDF (10,000 objects) | 580 KB | 107 | 107 | 106 | 106 |
| ANNOTS256.PDF | 101 KB | 7 | 6 | 6 | 6 |
| STRESS64.PDF (64 pages) | 19 KB | 14 | 12 | 10 | 10 |

verify-host re-ran BIGFILE, SPARSE and ANNOTS256 at all four sizes and
STRESS64 at 8 and 64 KiB on the wave-2 tree and got the same numbers.

Reading it honestly: **at open, a larger cache buys almost nothing** (at most
4 calls of 14, 1 of 107), and the 64 KiB column is the engine's own ceiling, so
nothing bigger than that is measurable without changing the engine. What stands
out is SPARSE.PDF's 106 calls to open - about 43 s at a floppy's ~400 ms a call (a hard disk is far less) - and
the cache is not what is slow: how the 106 splits between xref reads, the page
tree and the first page I did not break down. **This number belongs to PD-29's
hardware pass, not to protected mode.** Nothing here measured rendering,
re-reading a content stream across bands, thumbnails or a save, which is where a
bigger cache could matter; the first spike below does.

## 6. Recommendation and follow-up plan

**Decision requested: take option A, keep B as a gated follow-up, reject C for
now, and record D as built.**

Ordered work (none of it blocks the XT release):

1. *Spike 1 (half a day, kern_mega under Bochs, `docs/BOCHS.md`):* boot the
   shipped PageDesk on kern_mega with the shipped apps disk; open the samples
   and `SPARSE.PDF`; record whether it runs, the pool it claimed, and anything
   the pool moved. Expect it to work (2.3). A failure is a bug in a shipped
   build and wins over every option here.
2. *PD-25 first, with larger constants (real mode):* a block cache of 64-128 KiB
   from the unused heap, XMEM-backed Flate window and caches; measure on the
   MartyPC XT and a 286/386 86Box machine with a render of a many-band page
   (reads per page, time per page).
3. *Spike 2 (only if 2 leaves a measured gap):* a throwaway `-DPKG_MEGA` build
   that moves exactly one structure (the block cache, or the thumbnail store)
   above 1 MB through `OS88_HIGHMEM` windows, and its delta over the best real
   mode figure at the same RAM. A win under ~20% of a page's time is not worth
   an arm (it is a second build in `$(MEGAPKGS)` and a registry line).
4. *Revisit C* only when one of these changes: kern_mega gains a 32-bit program
   class; an owner decision on licensing (AGPL or commercial) exists; a measured
   XT-versus-host gap shows prepared copies (D) are not enough.

Exit criteria for "protected mode is not needed": spike 1 clean, PD-25's figures
in the capability matrix, and the XT gates (PD-29) unchanged with and without
the extra memory.

**Blockers recorded, as PD-27 asks.** No 32-bit program class; no flat model;
no ring 3 (a package is ring 0, so a PageDesk wild write is contained by limit
checks only, plan 2.5); the 8088 target has no runtime for any of it and the
286/386 test bench is Bochs and 86Box (docs/BOCHS.md, docs/86BOX.md; plan 9: MartyPC is an 8088 and QEMU hides 286 mistakes). None of these is a
PageDesk defect and none blocks the native XT release or weakens a baseline
guard. PageDesk's recovery journal (14.7) is what turns a kern_mega fault that
closes the package into a recoverable session rather than a lost one.

## 7. Not decided here

The owner's call: whether any larger-machine edition is wanted at all after
PD-25; whether an AGPL or commercial engine is acceptable for a distributed
floppy; whether a 32-bit program class is a kernel goal. This record takes no
position on those, only on what PageDesk should do while they are open.
