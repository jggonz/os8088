# Kernel size pass 19: the flag day - the record, and what is left for a twentieth

**PASS 19 HAS LANDED** on `kernel-size-19`, cut from `elendilon` at
`4beca096`. Named `-P20` for the pass it hands to, as every record before it.
The brief is `docs/plans/ksp19/BRIEF.md`; each agent's note is beside it and
is the authority on its rows. `docs/plans/ABI-BREAK-LIST.md` section 15 is the
row-by-row status this pass leaves.

## 0. The brief and the outcome

The owner's words: *"This will be 'the big one', finally cashing in all the
abi breaking size options we have been collecting in an abi breaks document.
... Other abi breaking size reductions spotted during this pass ARE in scope.
Other non-abi breaking size reductions spotted are also in scope."*

**`PKG_FMT` 10 -> 13, `DRV_VER` 12 -> 14.** Nothing built at the old numbers
loads. Every package, driver, module and C package in the tree is rebuilt.

The owner's rulings during the pass (BRIEF.md 0.0, 0.1):

1. T5's software-interrupt door, not T1's five-byte cells; the IF rule
   decided by audit (three deliberate IF=0 callers, so the door restores the
   caller's FLAGS); C packages declare their window class; L1 only if
   kern_small comes out neutral.
2. **No row grows bytes** - *"We're doing a size pass, so why are we growing
   bytes at all?"* Additions, speed-ups and correctness fixes that cost bytes
   wait for an additions pass (ABI-BREAK-LIST section 14).
3. Small package growth that buys RESIDENT kernel bytes is fine.
4. **Inside the flag day nothing is frozen** - *"breaking now isn't a break,
   it's just work. So if it's smaller and faster, go ahead and do it."*
5. C1 kept; then *"why do packages need to call for KB at all?"* - every
   package claim moved to paragraphs and the KB arm went.
6. *"We JUST did a bunch of work to get mega able to fully compact. Make sure
   we didn't break that work."* - it had been broken (section 3); restored.

### Kernel resident, assembled (`tools/kernsize.py --json`)

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(+ `.mega`).

| kernel | base `4beca096` | tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---|
| **kern_big** | 90,899 | **89,618** | **-1,281** | 96,256 -> **95,232** |
| kern_small | 56,533 | **55,821** | **-712** | 58,368 -> **57,856** |
| kern_emu | 91,145 | **89,864** | **-1,281** | 96,768 -> **95,232** |
| kern_mega | 130,367 | **128,945** | -1,422 | conventional 62,464 -> **61,440** of 68,096 |

kern_mega's conventional sections (`.text`+`.bss`+`.lowbss`+`.vgabuf`) went
57,080 -> 55,942 (**-1,138**); above 1MB `.cold` -268, `.mega` -16 (after
megacmp put +425 of pool DMA-head code back). On top of the section figures,
l1 removed the image's 512-byte ladder pad on kern_big (-208), kern_small
(-128) and kern_emu (-480) - memory, not a section.

`make budget-check`: kern_big 95,232 of 100,864 (5,632 spare), kern_small
57,856 of 64,000 (6,144), kern_emu 95,232 of 100,864, kern_mega 61,440 of
68,096 (6,656).

**Packages**: T5 -9.8KB over 170 images (parts included; -4.8KB top-level),
R14 -1.7KB asm and -0.9KB C, kbpara/C1 -244, mcmerge +23 (RunCPM and the
Browser's huge regrow).

## 1. Who took what

| agent | rows | kern_big | note |
|---|---|---:|---|
| **gates** | T6's `%imacro call` mechanism and CELL KINDS block, `tools/os88cells.py`, four-kernel `t_api_abi`, the C SDK and VX/XM/HD in the lock | 0 (every image byte-identical) | gates.md |
| **door** | the door (`int 81h`, `rd_tab`, `retf 2`), the IF audit, DOS virtualising 81h in both hosts, P4 (`KDOOR`), V7's reading | -78 | door.md |
| **l1** | L1 superseded: guard 6 proves the FAT window and `dsk_secbuf` inside one 64KB page, `KIMG_PARA` to the paragraph; `KERNEL_SEG` unchanged | ladder -208 | l1.md |
| **recon** | R2 (later reverted), V2, C10, R12 (later reverted) | -6 | recon.md |
| **drvtab** | histsys-7, N1, D4, R3, F1, F3, R2-3 (later reverted) | -229 | drvtab.md |
| **memdoors** | BR-105, C1, C2, C4', R1 | -141 | memdoors.md |
| **class** | R14 + K17-2 | -406 | class.md |
| **nogrow** | the no-growth rule applied: R2, R12, R2-3 reverted; R2's unread AX answers taken | +42 then removed by kbpara | nogrow.md |
| **t5** | T5: 96 door cells, 77 hot, `OSAPI_GFX_DESK` promoted, The Wire's new generation | -352 | t5.md |
| **swwm**, **swmem**, **swdrv** | wave C sweeps; the Sticky App descriptor's unread words | -18, -32, -5 | swwm.md, swmem.md, swdrv.md |
| **wmrec** | `{W_CLASS, W_SEG}` a far pointer for every window | -6 | wmrec.md |
| **kbpara** | every package claim in paragraphs, the KB arm deleted | -50 | kbpara.md |
| **megacmp**, **mcmerge** | kern_mega placement restored (section 3) | 0 | megacmp.md, mcmerge.md |
| merges | classmerge, t5merge, kbmerge: every slot address re-derived from the merged kernel's decoded table | - | their notes |

## 2. Speed

* **A rare call through the door: +205 clocks** (PREDICTED; +193 priced, +12
  for the `add` that keeps one index space on every kernel).
* **A window callback: +158 clocks** over pre-R14 on the real-mode kernels
  (+214 at R14, -56 by wmrec), +209 on kern_mega. Once per callback, never per
  pixel; at 40 drag packets/s that is under 0.2% of the CPU.
* `OSAPI_TASK_YIELD` ~95 clocks faster (V2), `OSAPI_GFX_DESK` ~85 faster (V7).
* No `int 13h` call was added or removed by any agent. `READ_AT` +35 clocks
  a call (swmem, on a path that reads the disk).
* The MartyPC A/B (`tools/os88hot.py ab --base 4beca096`) is the final
  pass's - section 7.

## 3. The kern_mega regression, and its repair

C1 withdrew the KB DMA doors, which on kern_mega put an OS88_HIGHMEM
package's DMA ring into a pinned above-1MB pool block with a page-safe head.
The paragraph conversion put those rings in conventional memory, and swdrv
then deleted the pool DMA-head machinery as dead. A KB regrow of a huge claim
past 1,023KB also wrapped (`megargw` faulted). megacmp audited every converted
site against `4beca096` and restored both: **`MEMP_DMAPOOL`** (a verb bit,
40h, 0 bytes off kern_mega) and **`OSAPI_MEM_HUGE`'s `MEMH_REGROW`** for
1,024KB and more. `megajw`, `megargw`, `megaxmove`, `megahuge`, `megahigh`,
`heaphi` green. The per-site table is `mcmerge.md`. **Lesson: a conversion of
a memory door is checked per kernel against the base's PLACEMENT, not only
against its result.**

## 4. Wants the owner (open)

1. **`OSAPI_BATCH_END` withdrawn** (-18 resident big/small/emu, -6 kern_mega
   conventional): SPEC.md 20.3.1 rule 1 names it as optional by design.
2. **`W_DISP`'s offset dropped**, the far call built on the stack: about -20
   resident, about +47 clocks a callback (wmrec.md).

## 5. Deferred to the additions pass

R2, R2-3, R12 (no net-negative form), R15, R16, V1; refusing KB counts of
1,024 or more (+6 a kernel); a wider regrow for C's `os88_mem_regrow` past
1MB. See ABI-BREAK-LIST sections 14 and 15.

## 6. What is left

* **RCS door for `call COLD_SEG:x`** (swdrv: 395 sites, -2 a site, +2 a
  target, ~+190 clocks a call) - kernel-private, wants a per-site cycle check.
* **`OSAPI_GFX_PIXEL` withdrawn** (recon: the 8-byte cell against +4 at 7
  callers).
* The old Wire catalogue on os8088.com carries PKG_FMT 10 packages; the next
  release republishes it at 13 (SPEC.md 92.2).
* `tests/megaseg.txt` allows 3,763; the tip uses 3,650 - ratchet it.
* `picomem`'s `sb` leg does not assemble (`SDG_SET` redefined) - see the
  final pass for whether it predates this pass.

## 7. What was run

- **The deduplicated union of every agent's rows, once** (`tools/os88soak.py`,
  run `20261110-101855`, 656 rows with Bochs and the automation-enabled 86Box
  present; the four WIN16 rows and `hmahiber` left out for missing fixtures):
  **554 ok, 102 FAIL**. The failures were triaged by `fix` and `fix2`
  (`docs/plans/ksp19/fix.md`, `fix2.md`): ~38 test scripts reading W_SEG at
  R14's old offset 22; R14 removing a CF-clearing call at Clear Skies' entry
  (every first launch refused, ~40 rows, +1 byte); kbpara's conversion
  clobbering PiXEL's CL and ExciteBike's BX; INSTALL's fixed-offset kernel
  probe; headless DOS's template; a Sticky App close; tests moved to the new
  contracts. **All 102 re-ran green** from the fix branch; `sbirq5` and
  picomem's `sb` leg also fail at `4beca096` (pre-existing).
- **The clobber audit** (`clobber.md`): all 269 new `OSAPI_MEM_P` sites read
  for destroyed registers and changed outputs - none live in shipped code;
  two test packages (MEGAHI, MEGAHG) fixed.
- **`test-full`, once: 93 ok, 1 FAIL, 2 SKIP** (`w16cens`, `w16censall` want
  the Win16 fixtures). The failure was `pins` not knowing `MEMH_REGROW` is a
  resize; taught, green.
- `make budget-check` on all four kernels: met (section 0).
- **The MartyPC before/after against `4beca096`, run after the field trip**
  (`docs/plans/ksp19/timing.md`, every table): `os88hot time` every scenario
  on CGA, Hercules and VGA (kern_big) and on kern_small at 640KB and 128KB;
  gfxbench, sysbench and REDLINE; and three games in play - Clear Skies'
  pinned frame, WireBall's timed rallies, ExciteBike's whole first course.
  **No meaningful regression.** CGA/VGA desktop operations within +-0.2%
  except typing (+0.4% CGA, +0.9% VGA: the priced callback and door costs,
  ~0.1 ms a key); gfxbench 71/74 (CGA) and 70/74 (VGA) rows within 0.5%,
  REDLINE's CPU/RAM rows within 0.01% and graphics identical or faster; Clear
  Skies' render work 0.0 to -0.12% on every adapter, WireBall and ExciteBike
  frame for frame identical. The section 2 figures, MEASURED: the rare door
  **+222 to +228 clocks a call** (WM_BAND, FILE_HERE, FILE_DFREE; predicted
  +205), `OSAPI_TASK_YIELD` **-109** (predicted ~-95); boot -100 to -205 ms
  on CGA and Hercules. Disk-bound rows move with the platter only (same int
  13h calls, sectors and seeks on both arms).
- **Hercules on MartyPC is not a kernel measurement until the card's phase is
  pinned** - the P19 section 7 mystery, solved: MartyPC's MDA model indexes
  its wait-state table by the card's clock mod 16, and in graphics mode that
  clock steps by 16, so the residue left at the last text-to-graphics switch
  prices every VRAM access for the session. The same BASE build, with only a
  5-cycle text excursion poked in, reproduces the tip's Hercules gfxbench to
  0.1% (VRAM write word +7.1%, a loop inside the bench package); averaged over
  residues the tip is equal or faster on Hercules too (timing.md section 6).
- `tests/skiesperf.py` was fixed on the way: it could not find a call site
  once `call` became the `OSAPI_RARE` macro.
- `ftpd` stalled once at STOR-after-LIST and passed on re-run; not checked at
  the base.

## 8. Method lessons

* **Two scripted renumberings of one table cannot be merged hunk by hunk.**
  classmerge's answer - turn every slot citation into a cell token per side,
  merge, then re-derive every address from the merged kernel's decoded table
  (`cm_*.py`) - is the way; t5merge then needed none because the sweeps moved
  no address.
* **A "no growth" rule must be stated at the START of a size pass that breaks
  an ABI**: correctness re-contracts arrived as rows and grew bytes until the
  owner stopped them.
* **The soak's frozen tree builds every on-demand artefact its rows declare**:
  four test C packages (`chello`, `cfsx`, `covl`, `lembench`) had not followed
  R14 and only the final pass's tree build found them. Build the test C
  packages with the C SDK change.
* Worktrees are ~0.9GB each; nineteen of them filled the disk at the final
  pass. Remove a worktree when its branch merges.
