# kern_mega size pass 2: bootonce

Owner of the MECHANISM for code that runs once around the protected-mode
switch, and of `kmain`'s order. Branch `msp2-bootonce`. The brief is
[BRIEF.md](BRIEF.md); the work list was msp1/pmcore's ~370 bytes
([../msp1/pmcore.md](../msp1/pmcore.md) DEFERRED), msp1/pmmem DEFERRED 4's
~670 ([../msp1/pmmem.md](../msp1/pmmem.md)), msp1/shared DEFERRED 10's ~660
([../msp1/shared.md](../msp1/shared.md)) and the bootstrap GDT.

**The question was whether the switch's real-mode half can run before
`mem_unblob`, so it lives in the boot blob and is given back.** The answer
is **only its side-effect-free half, and it is small**. The other half -
the task test, A20 opened and proved, the 82KB conventional claim for the
tables, and the refusal (toast, `.mega`'s rung to the heap) - has to happen
at the switch, in the order it always had, and the blob has far less room
than the work list supposed. A `.mega`/`.cold` init tail dropped after
`mega_pmtail` is REFUSED for this pass (below): what it would free is
extended memory at 1KB granularity, and the mechanism is not small.

## BASE AND TIP

Measured against the merged `mega-size-2` at `64c8c1f5` (pmcore, megados and
pmmem in), which is what this branch's last three commits sit on; section
lengths from the assembler's map of the same build, `KERN_SIZE`,
`MEGA_RESIDENT` and `MEGA_CODE_KB` from its symbols. Tip `f4e3da8b` plus this
note.

| kern_mega | merged base | tip | delta |
|---|---:|---:|---:|
| `.text` | 46,383 | 46,365 | **-18** |
| `.bss` | 5,427 | 5,427 | 0 |
| `.cold` | 46,088 | 46,047 | **-41** |
| `.lowbss` | 6,591 | 6,593 | +2 (`mega_gwhy`) |
| `.vgabuf` | 336 | 336 | 0 |
| `.mega` | 21,280 | 21,280 | 0 (+3 bytes of code, absorbed by the section's tail padding) |
| `.boot2` (given back at `mem_unblob`) | 6,894 | 6,938 | +44 |
| `.ovl` | 5,118 | 5,118 | 0 |
| `KERN_SIZE` at boot | 134,144 | 133,632 | **-512** (`.cold`'s rung, 46,088 -> 46,047 across 46,080) |
| `MEGA_RESIDENT` (conventional after the switch) | 64,512 | 64,512 | 0 |
| conventional bytes after the switch (`.text`+`.bss`+`.lowbss`+`.vgabuf`) | | | **-16** |
| `MEGA_CODE_KB` (the code area above 1MB) | 68 | 68 | 0 |
| `.text`+`.bss` of the 64KB window | 51,810 | 51,792 | -18 |

Net resident: **-57 bytes** (-16 conventional for ever, -41 `.cold`, which
is above 1MB after the switch and CONVENTIONAL FOR EVER on a machine that
refuses it), for +44 of blob that is given back.

**kern_big, kern_small, kern_emu: byte-identical.** Every change is inside
`mega.inc`/`megados.inc` (`%ifdef KERN_MEGA` throughout) or a `%ifdef
KERN_MEGA` hunk of `kmain_o`; `kernsize --json` for kern_big equals
`/tmp/msp2/base-big.json`, and `cmp -l` against the base `kernel.bin` shows
only the build-number bytes (SPEC.md 14.2). kern_small and kern_emu
assemble none of it.

## TAKEN

1. **The switch's gate, split by side effect** (`mega_gatef` -> `mega_gateb`
   in `.boot2` + checks in `mega_enterf`). `kmain_o`'s last call,
   `BLOBCALL mega_gateb`, asks the tier, runs `mega_layoutf` (a pure
   function of `[mega_extkb]`, already run once in the blob by
   `mega_wrprobe`) and, when the tables go above 1MB, aims them
   (`mega_tset`: stores to words real mode never reads). The verdict goes
   into `[ss:mega_gwhy]` (`.lowbss`, so not the 64KB window): 0 go, 1 go
   with the tables in conventional memory, else the refusal's string.
   `mega_enterf` then makes, at the switch's own moment and in `mega_gatef`'s
   order: the task test (`mega_tasks_ok`, inlined - it had one caller), A20
   opened and proved, the low tables' claim (`mega_tlowf`, inlined, same
   reason), and the refusal path (E9 line on MEGA_E9 builds, toast, `.mega`'s
   rung). `mega_tset` became FAR (`retf`), called from the blob and by
   `COLDCALL` from `mega_enterf`. **`.cold` -42, `.boot2` +44, `.lowbss`
   +2.**
   *Equivalence by reading*: every step with an effect (A20 poked, the claim,
   the toast, the rung, `[mega_mfree]`) runs at the same moment, in the same
   order, after the same refusals, as before. What runs earlier is the tier
   compare, `mega_layoutf` (pure; same inputs) and, on the high-tables arm,
   `mega_tset`'s stores - which on a machine that then refuses on the task
   test or A20 leave `mega_gdtr`/`mega_idtr`/`mega_ldtlin`/the descriptors
   and `[ss:mega_ldte]`/`[ss:mega_ldtl]` aimed at the top of extended memory
   where the old code left them 0. Every reader of those is protected-mode
   code (`mega_enterf`'s `.room`, the LDT allocator, `mega_ivt_sync`, the DOS
   park, `hiber.inc`'s PM resume staging), none of which runs on a refused
   machine. The blob's `mega_gwhy` default never matters: `kmain_o` always
   runs (it IS the boot), and `mega_enter` is reached only from `kmain`.
   Reason strings are `.text` offsets, all > 1, so the 0/1 codes cannot
   collide. Not on the 286 reset path.
2. **`mega_enter`'s copy-up and tail into `mega_enterf`; the `mega_coldup`
   door deleted.** `mega_enter` is now `call COLD_SEG:mega_enterf / ret`, kept
   NEAR and named because `kmain` near-calls it, `tools/kernsize.py` detects
   kern_mega by that symbol, and `tests/stiknodesk.py`/`stikhiber.py` break
   on it and read a near return off SS:SP. `mega_enterf` ends `COLDCALL
   mega_coldupf / call MEGA_SEL_CODE:mega_pmtail / retf`. **`.text` -16,
   `.cold` +7** (+8, less the refusal's `stc`, which nothing read).
   *Equivalence by reading*: the same three calls in the same order; the
   refusal returns before the other two, as `jc .x` did. `mega_coldupf` runs
   in the `.cold` it copies by pass 1's argument, and that argument covers
   its caller's frame too: `mega_coldupf`'s `retf` pops COLD_SEG, whose
   descriptor it has just re-based, and lands at the same offset of an
   identical copy (`.cold` holds no data, os88ovlchk rule 1, so the copy is
   exact). `mega_pmtail` then runs from the high copy and returns there;
   `mega_enterf`'s `retf` pops 0x0060 as before. Stack: 2 bytes deeper on
   task 0's stack inside `mega_coldupf` and 4 deeper inside `mega_pmtail`
   (which `sti`s) - STK0 is 512 bytes and this is the boot's only frame on
   it. `mega_coldupf`'s CF ("nothing moved") was never read before either.
3. **`mega_dsidt` deleted** (megados's finding, handed over by the
   coordinator because it is `mega_tset`'s store): it was written once by
   `mega_tset` as `[mega_ldtend] + 100h` and read once by `mega_dosparkf`;
   that reader computes it now. **`.cold` -6, `.text` -2 (the `dw`), `.mega`
   +3.** `[mega_ldtend]` is written only by `mega_layoutf`, a pure function,
   so the value is the one that was stored.

4. **The four refusal lines out of conventional `.text`** (the owner's
   answer to WANTS THE OWNER, taken on `msp2-bootonce2` after the merge).
   `mega_t86`, `mega_tmem`, `mega_ta20`, `mega_ttask` moved from `.text`
   into `.mega`, bracketed `; ovlchk: rung data`. **They are NOT staged
   from the blob, and that is deliberate**: only the tier test is decided
   in the blob - the task test, A20 and the low tables' claim are decided at
   the switch, after the blob is gone, so three of the four lines must
   still exist then. They do: when `mega_enterf` refuses, `.mega`'s RUNG is
   still in conventional memory at `MEGA_RAM` (the refusal is what gives it
   to the heap, via `mega_rung`, AFTER `toast_show` has copied the line), so
   the refusal reads the line through `ES = MEGA_RAM`. On a machine that
   switches, the lines go above 1MB with the rest of `.mega` and are never
   read. So **the toast is raised at exactly the moment and in the order it
   always was** - the owner's condition holds trivially, and the
   `drv_notice_x` ordering question does not arise. MEGA_E9 builds print
   the line through `DS = MEGA_RAM`. Measured against `mega-size-2` at
   `03abb371`: **`.text` -124 conventional (the 127 bytes of strings, less
   alignment), `.cold` +3, `.mega` +128 (127 + its tail padding), `.boot2`
   0, `KERN_SIZE` -512, `MEGA_RESIDENT` 63,488 -> 62,976 (-512: the
   conventional footprint crossed a rung down)**; kern_big, kern_small and
   kern_emu assemble byte-identical. `tools/os88ovlchk.py` learns the
   marker: a label inside it leaves the `.mega`-data-through-CS rule, and
   may be named ONLY as an offset load (`mov reg, label`) - 4 labels.
   `tools/os88bochs.py`'s `_HOME` (the "kernel is in conventional memory"
   probe every Bochs row uses) read `mega_tmem` in `.text`; it reads
   `mega_s_kbdos` ("Use this disk's DOS.O88", megados.inc, `.text`,
   unconditional) instead.
   **Verified on Bochs** (the one emulator check this item was allowed): a
   breakpoint on `mega_enterf`, `[mega_gwhy]` poked to `mega_ta20`'s offset
   in the rung, run on: `[mega_on]` 0, `[mega_mfree]` 1, `[toast_on]` 1,
   `toast_buf` = `real mode: A20 will not `, and the screenshot shows the
   toast in the menu bar over a real-mode desktop
   (`/tmp/msp2/bootonce/refuse-toast.png`; the probe is
   `/tmp/msp2/bootonce/probe/refuse.py`). The truncation is the 24-character
   cap and is NOT new - see DEFECTS FOUND.

Docs: SPEC.md (two sentences naming `mega_tasks_ok`/`mega_tlowf`) and
docs/HEAP-CLAIMS.md's `MEM_K_MTBL` row now name `mega_gateb`/`mega_enterf`.
No hot path changed (`hotpar` green); `.text` moved by -16, then -2, both
even.

## REFUSED

* **The whole gate in the blob** (built: commits `871e1f59`, `2f5e55db`).
  Moving `mega_gatef`, `mega_tasks_ok`, `mega_tlowf` and `mega_tset` took
  `.cold` -126 for `.boot2` +120 - and **the blob has 260 bytes spare on
  the shipped kern_mega, 113 under `BOOTMARK=1` and 60 under `BOOTMARK=1
  MEGADIAG=1`**, so `BOOTMARK=1` stopped assembling (7 over). Keeping
  `mega_tset` in `.cold` (+94 blob) fixed that and broke the pair (34 over).
  Moving the claim too early was also the wrong ORDER: an 82KB pinned claim
  made in the blob is left behind by an A20 refusal at the switch. What is
  taken (+44) leaves every knob combination fitting: shipped 230 spare,
  MEGADIAG 177, BOOTMARK 83, BOOTMARK+MEGADIAG 30.
* **Running the whole switch before `mem_unblob`** (so `mega_enterf` and
  `mega_coldupf` could be blob code): the switch would then precede
  `mem_unblob`'s compaction, the first paint, `cursor_show` and
  `drv_notice_x` (which loads CTRL.DRV), all of which today run in real mode
  on paragraph-named claims; `mem_unblob_x` compacting in protected mode
  before `mem_xlatf` is a different heap. Not a size-pass change.
* **A `.mega`/`.cold` init tail dropped after `mega_pmtail`** (pmcore's ~370:
  `mega_frames_fix`, `mega_mods_fix`, `mega_ivt_sync`'s first build,
  `mega_strcal`, pmmem's new `mega_mxtinit`; shared's ~660: `kmain_hic`,
  `drv_hiboot_f`, `mem_hicaches_f`, `mem_hiclaims`, `as_bootc`,
  `mod_hiboot`): all of it runs in protected mode from the HIGH copy, and
  the code area sits at the TOP of extended memory above the tables
  ([pool | tables | `.cold` | `.mega` | scratch]), so a freed tail is not
  contiguous with the pool. Giving it back needs either a second pool run
  (mx_tab and the compactor learn a hole at the top) or running the tail
  from the LOW rung before `.mega` is copied (the low rung is heap from
  `mega_rung` on, and the tail itself claims). Either is a mechanism of
  ~100-200 bytes (estimate) to return what rounds to 1KB of extended memory
  (`MEGA_CODE_KB` is 68 either side of ~1KB of tail) on machines that have
  megabytes. Not conventional memory; not worth its own code now.
* **The bootstrap GDT (`mega_bgdt`, `mega_bwin`, 48 `.text`) built at run
  time**: it must exist at the switch (after the blob is gone) in memory the
  bootstrap's KDATA (base 0x600, 64KB) can read for `.gspecial`'s copy into
  the real GDT, and `.cold` may hold no data (rule 1) nor read through CS
  (rule 2). Building it with pushes on the stack is ~70 `.cold` bytes for 48
  `.text` - a loss on a refused machine, where `.cold` stays conventional.
  See DEFERRED for the one shape that might pay.
* **The refusal toast posted from the blob** (would move the four reason
  strings into the blob): the owner said yes, but three of the four reasons
  are decided at the switch, after the blob is gone, and moving those checks
  into the blob is the ordering and blob-ceiling problem above. TAKEN 4
  reaches the same -124 conventional by the `.mega` rung instead, with no
  change in when the toast appears.

## DEFERRED

Estimates, labelled; pass 3's list.

1. *(taken - TAKEN 4.)*
2. **`mega_bgdt` written by the blob into the low `.mega` rung's scratch**
   (`MEGA_RAM:MEGA_SCR_OFF`, conventional and untouched until `mega_coldupf`
   gives the rung to the heap), with a bootstrap data entry to read it:
   ~-48 `.text`, ~+40 `.boot2`, ~+10 `.cold`. Needs the `.gspecial` copy
   rewritten to read through that entry; and `mega_bwin`'s store in
   `mega_tset` would aim it. On the 286 the reset path does not touch it.
3. **`kmain_hic` and the four far calls it makes** (`drv_hiboot_f`,
   `mem_hicaches_f`, `stik_boot`, `as_bootc`, ~660 `.cold` with their
   bodies): boot-once protected-mode code; only an init tail (REFUSED above)
   returns it. Estimate -600 above 1MB, rounds to ~0-1KB of pool.
4. **`mega_rung`'s two callers** (`mega_enterf`'s refusal, `mega_coldupf`):
   both boot-once, ~18 `.cold`; inline both and the routine goes (~-4).
5. **`mega_tset`'s pairs of stores** (`mov [x+2],ax / mov [x+4],dl` five
   times): a `stosw`/`mov [di],dl` helper with ES = DS saves ~10 `.cold`
   (estimate); the low arm is the only `.cold` caller left, the blob the
   other.
6. **`mega_wrprobe`'s MEGA_E9 string** (`.said`, 38 bytes + 15 of loop, in
   `.boot2` on MEGADIAG/MEGATRAP builds): through `mega_putsf` from a
   `.text` string it would free 53 blob bytes on the diag builds, which are
   the tightest. CROSS-FILE (megados).

## WANTS THE OWNER

* ~~May the refusal toast be staged from the boot blob?~~ **Answered yes,
  and TAKEN** (TAKEN 4) by a route that changes nothing visible: the lines
  live in `.mega`'s rung and are read before it is freed. Verified on
  Bochs with a forced refusal; the toast is on the glass.
* **The four refusal lines are over the 24-character toast cap** (DEFECTS
  FOUND): rewording them is a visible change and the owner's call -
  e.g. `No PM: 8086-class CPU`, `No PM: no table room`,
  `No PM: A20 will not open`, `No PM: an older task` (all <= 24).

## WANTS AN ABI BREAK

None found in this code: everything here is kernel-internal (`kmain`,
`mega_enter*`, the tables' words) and no cell, header or file format is
near it.

## CROSS-FILE

* **megados**: DEFERRED 6 (`mega_wrprobe`'s `.said` out of the blob, -53
  blob on the diag builds). `mega_dsidt` is gone - its reader in
  `mega_dosparkf` computes `[mega_ldtend] + 100h` (taken here, at the
  coordinator's word).
* **pmmem**: `mega_mxtinit` (59 bytes, once-only, in `mega_pmtail`) was
  offered for an init tail; none is built (REFUSED above), so it stays.
* **all**: comments that still say `mega_coldup` (kernel.asm's
  `API_COLDSEG` note, memory.inc:6030, tools/os88megacommon.py) mean
  `mega_coldupf` now that the door is gone; tests/megaseg.txt line 27
  ("mega_enter's real-mode refusals, the A20 gate and the task test") now
  describes `mega_enterf`.
* **coordinator**: the blob's spare is a shared ceiling - after this branch
  230 bytes on the shipped kern_mega and **30 under BOOTMARK=1 MEGADIAG=1**.
  Anyone else adding to kern_mega's `.boot2` should check that pair
  (`nasm ... -DKERN_BIG -DKERN_MEGA -DBOOT_MARK -DMEGA_DIAG`); no build-matrix
  row assembles it.

## ROWS TO RUN (named, not run)

Bochs (`tests/megaport.txt` / suite `mega*`):

* `megatbl` - the tables' placement and the LDT/IDT words at 1, 4 and 16MB
  (`mega_tset` from the blob).
* `megainst`, `megaflt`, `megashed`, `megahiber`, `megahibfull`,
  `megap2hiber` - boots through the new `mega_enterf` tail and the resume
  staging that reads `mega_gdtr`/`mega_idtr`.
* `megadosp`, `megadosroom` - the DOS park's IDT line (`mega_dsidt` ->
  `[mega_ldtend] + 100h`).
* `megahma`, `megaxmove`, `megahuge` - the copy-up and the pool right after
  it.
* `stiknodesk`, `stikhiber` (their kern_mega legs) - they break on
  `mega_enter` and read its near return.
* `megatrap0` (MEGATRAP build) and the build matrix's `megadiag`,
  `megatrap`, `piclose*` rows (`soak -k buildmatrix`): the knob builds still
  fit the blob.

86Box (the 286 route; `vm/286-mega`, the owner's mr286, through
`tools/os88box.py`):

* boot `286-mega` to a desktop with `faults` empty - the switch and the
  copy-up from `mega_enterf` on a real 286 reset BIOS;
* a DOS program through a live resume on `286-mega` (the park's IDT line);
* **the two arms no emulator here has exercised**: a 286 with 640KB and no
  extended memory (or too little for the tables: under their own KB plus
  the HMA's 64, ~70KB) - the LOW
  tables' claim, now made in `mega_enterf` - and one where A20 will not open
  or a refusal is forced, to see the toast and the real-mode desktop. Bochs
  cannot go below 1MB, so neither arm has a row.

## DEFECTS FOUND

* `mega_enter`'s header says it keeps BX; `mega_enterf`'s `.room` path has
  always clobbered BX (`.gdt`'s loop, `mega_idt_build`). Nothing reads BX
  after it in `kmain`; the comment is wrong, not the code. Left as it was.
* **Every kern_mega refusal toast is truncated, and was before this pass**:
  `TOAST_MAX` is 24 and the lines are 25-41 characters - `real mode: no
  room for kern_mega's tables` shows as `real mode: no room for k`, A20's as
  `real mode: A20 will not `. `tests/unit/t_toast.py` (SPEC.md 59.10) does
  not see them because the line reaches `toast_show` through DX -> SI in
  `mega_enterf`'s shared tail. Not fixed here (WANTS THE OWNER).
* Not a defect in the tree, but worth knowing: `BOOTMARK=1 MEGADIAG=1` on
  kern_mega is the tightest blob in the tree and nothing assembles it.
