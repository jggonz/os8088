# Boot path and kernel init

Reviewer area key: `boot`. Tree: `the-big-review` at the base it was cut from
(clean working tree). Read-only review: nothing assembled, nothing booted. The
numbers quoted from a build come from the `build/kernel.map`,
`build/smallk/kernel.map`, `build/kernel.kz.json` and the shipped `build/*.img`
that were already in the tree (dated 2026-11-08 03:09). The arithmetic that
depends on them is reproducible with the two short Python snippets under
*Evidence scripts* at the end.

## Scope read

| file | lines | read |
|---|---|---|
| `boot/boot.asm` | 593 | all |
| `boot/boot2.asm` | 904 | all |
| `boot/boothd.asm` | 496 | all |
| `boot/mbr.asm` | 200 | all |
| `boot/osrom.asm` | 1,283 | 1-760 in full (header, `rom_init`, `rom_stub`, socket check, `rom_patch`, `rp_walk`, `rp_list`, `rom_modfix`); the HMA pool half skimmed |
| `kernel/kernel.asm` | 9,886 | 1-330, 1400-2480 (budgets, ladder, BLOB_LIFT, BOOT2_SECS, section map, fixed entries), 5225-5265 (spl_fp/spl_ifp), 5380-5470 (SPLCALL/OVLCALL family), 5560-6420 (`kmain_o`, `kmain`, `kmain_hic`), 8620-8740 (SPL_RESIDENT and blob guards), 9380-9720 (guards 1-7), the `%error` list as a whole. Not the API table (another reviewer) |
| `kernel/splash.inc` | 1,608 | 1-800, 1150-1300 in full; the drawing primitives skimmed for multiplies/divides |
| `kernel/bootprof.inc` | 333 | 1-120 in full, the rest skimmed |
| `kernel/cpudet.inc` | 241 | all |
| `kernel/shutdown.inc` | 352 | all |
| also | | `kernel/viddet.inc` 1727-1800 (the splash's resident bytes), `kernel/disk.inc` 2680-2860 (`dsk_boot_from_x`), 2905-2945, 4060-4100, 5320-5350 (`dsk_cylrun`), `kernel/sched.inc` 2320-2360 (sch_isr's splash call), `tests/unit/t_canary.py` (all), the Makefile's `KSIG_OFF`, `KZ_HEAD`, `BLOBSUMDEF` and `kernel.sys` rules, `drivers/hdd/inst.inc` 3500-3550 |
| SPEC.md | | §1 rule 2, §2.9 (2.9-2.9.9 in full, headings of the rest), §3, §4's two rows, §14.2, §15 (top) , §15.1 (top), §60 in full; `docs/FIELD-NOTES.md` 33 and 36 |
| plans | | `docs/plans/ABI-BREAK-LIST.md` and `docs/plans/KERNEL-BYTE-QUEUE.md` grepped for boot/splash/blob/canary rows; `docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md` has nothing on this area; `docs/plans/LAST-DROP-BYTES.md`'s KSIG_OFF paragraph |

## Verdict

The boot path is one of the best-reasoned parts of the tree: a 512-byte stage 1
that relocates itself by arithmetic, verifies the blob it read with a word sum,
and hands a 13-sector stage 2 its geometry; a stage 2 that batches whole
cylinders, verifies the *transfer* rather than the table, and falls back once;
and a boot half of `kmain` (`kmain_o`) that runs from the blob and is given back
to the heap floor, so boot-only code costs no resident byte at all. That shape -
a transient that sits at the bottom of the heap and is compacted away - is
better than anything DOS, Windows 3.x or System 6 did with their init code.

But the one guard whose whole purpose is the fault nobody can reproduce in an
emulator is **inert on the XT's own disks**. Since kernel size pass 14 took
`BOOT2_SECS` from 12 to 13, stage 2's first run on the 360KB and 720KB kern_big
disks starts on head 1 and never crosses a head, and the canary sector (file
sector 21) is in that run - so on the BIOS the canary exists for, the canary
passes, `boot_cylrun` is published non-zero, and `dsk_xfer` then writes across
heads for the rest of the session. `t_canary` stayed green because it models
runs as starting at the data area rather than at `data + BOOT2_SECS`. That is
`boot-1`, it is a two-number fix, and it is the only high finding here.

The rest is the usual shape of a heavily size-passed area: invariants held by a
label that sits four bytes short of what it guards (`boot-4`), a second loader
(`boothd.asm`) that still asks a question `boot.asm` retired (`boot-6`) and
fetches its BPB with a geometry the hard-disk sector deliberately stopped
trusting (`boot-5`), a dead duplicate of the DPT patch in a blob with four
bytes free (`boot-7`), and comments that narrate the history of a constant
rather than state its invariant - and are wrong about the current value in
more than a dozen places (*Doc/code mismatches*). Nothing here needs an ABI
break.

## Strengths

1. **The blob at the heap FLOOR, given back and compacted** (`boot/boot.asm`
   369, `boot/boot2.asm` 135-155, `kernel/kernel.asm` 6098-6121). Every
   boot-only body - the loader, the splash, `kmain_o`, two dozen `*_x` init
   routines - lives in `.boot2`, and `mem_unblob_x` drops `[mem_base]` back
   over it after `spl_finish`. A transient at the floor rejoins the long run;
   one at the top strands itself above unmovable claims. This is the right
   answer to "init code costs RAM forever" and it is the model any new
   boot-only code should follow.
2. **Verify the TRANSFER, never the table** (`boot/boot2.asm` 401-470). Reading
   back your own patch proves nothing; summing a sector the run landed in does.
   The sum (not the first word) closed the kern_mega case where the wrong
   sector's first word was also 0. The principle is correct even though its
   placement is currently wrong (`boot-1`).
3. **Stage 1's own blob checksum** (`boot/boot.asm` 458-505) catches the
   failure that is not a disk error - a sector that silently did not arrive -
   for six bytes of compare and a 20 ms loop, and `BOOTDIAG=1` prints the sum
   so a missing sector can be named from the host.
4. **Assembly-time arithmetic where a runtime check would be dead code**:
   `MEMNEED` clamped to `RELOC_ADJ` (`boot.asm` 137-147), the blob-overlap
   `%error` under `kz_all` (`boot2.asm` 515-519), `.cfx` against
   `BLOB_SEG_AT` (`kernel.asm` 8873), `b2_heads - b2_drive != 1` (`boot2.asm`
   883). Each turns a field fault into a build failure at zero bytes.
5. **The DL range check instead of a fallback** (`boot.asm` 266-298,
   `mbr.asm` 74-77): a range check cannot pick up the wrong disk; a fallback
   can. The reasoning is written down and it is right.
6. **The fixed low words with an indirection** (`kernel.asm` 2338-2412):
   `0060:0004` boot_cylrun, `000C` boot_ticks, `000E` the debug registry
   pointer - a cheap ABI-stable channel between the loader, the kernel and the
   test harness, and the registry stops the next instrument needing a word of
   its own.
7. **One decoder for both loaders** (`boot2.asm` 521-616): the hard disk and
   the floppy put the packed tail at the same address by agreeing arithmetic,
   so `kz_hd` is a 17-byte move plus the floppy's `kz_all`. No second copy to
   drift.
8. **The interrupt has its own far pointer** (`kernel.asm` 5242-5254,
   `sched.inc` 2326-2352): `spl_ifp` exists because `SPLCALL` stages its
   target in a shared word two instructions before the call. A one-in-fifty
   race found and closed by construction rather than by `cli`.
9. **The section-end guards** (`kernel.asm` 9795-9884): "something landed in
   `.X` below `X_end`" for every section, so a stray `section` switch cannot
   silently under-report a size that a ladder rung is derived from.
10. **`boothd.asm`'s CHS proof** (`boothd.asm` 378-422): the two guards that
    make a single `div` pair safe on bytes off a disk, with the failure each
    one prevents written beside it. A model for any code that divides by a
    field read from media.
11. **`cpu_detect`'s x87 pad on its own frame** (`cpudet.inc` 117-139): the
    `[bp+0]` with no override is the mechanism and the comment says why a
    "fix" would break it. That is how a deliberate non-obvious encoding should
    be documented.

## Findings

Ranked most severe first. Every byte figure is signed; "blob" is `.boot2`
(transient, given back at `spl_finish`), "`.ovl`" the window half (dead at the
first mount), "boot sector" is stage 1 or `boothd.asm` (not RAM at all).

### boot-1 (bug, HIGH, now) The boot canary is inert on kern_big's 360KB and 720KB disks, and `t_canary` cannot see it

**Evidence.**
- `boot/boot2.asm` 318-322: `.reload` starts stage 2's read at
  `[b2_lba0] + BOOT2_SECS`, i.e. at `data + 13` on kern_big.
- The shipped BPBs (`build/os8088-360.img`, `build/os8088-720.img`): `rsvd 1,
  nfat 2, fatsz 3, root 112` - the data area is **LBA 14**, so `.text` begins
  at **LBA 27 = cylinder 1, head 1, sector 1**.
- `read_run` (`boot2.asm` 713-723) bounds a run at the cylinder: LBA 27/18
  leaves 9, so run 1 is LBA 27..35 - **head 1 only, no head crossed**.
- `KSIG_OFF = 4096` (`Makefile` 1021, `boot2.asm` 57): kernel sector 8 =
  file sector 21 = **LBA 35**, the last sector of that non-crossing run.
- `tests/unit/t_canary.py` 99-117: `crosses()` walks runs from `lba = data`,
  not from `data + BOOT2_SECS`, so it believes run [18..35] started on head 0
  and reports file sector 21 as crossing. The docstring's claim "Mirrors
  boot.asm read_run's partitioning exactly" (line 101) is true of the stage-1
  era and false since SPEC.md 2.9.
- Before kernel size pass 14 (BOOT2_SECS 12), `.text` began at LBA 26 = head 0
  sector 9 and run 1 crossed at LBA 27, so the canary was live; the 12 -> 13
  step broke it with no line of the loader changed. kern_big 1.2MB and 1.44MB
  and both kern_small disks are still correct (their first run starts on
  head 0 and crosses before the canary).
- Consequence on the BIOS class the canary exists for (one that ignores the
  int 1Eh EOT patch; on a 286 it is reachable once the user ticks Cylinder,
  SPEC.md 31.14): every later cylinder's head-1 half loads wrong, the canary
  passes, `[es:0x0004]` is written (`boot2.asm` 467), `dsk_cylrun` becomes 1
  (`disk.inc` 5343-5346) and every crossing multi-sector **read** for the
  session returns the other head's sectors on a BIOS that flips at the wrong
  sector - and a FAT or directory sector read wrong is then rewritten.
  (Corrected (follow-up, 2026-11-08): this said "write"; cylinder runs are
  reads only, SPEC.md 18.91.3. The date is corrected too: on the shipped KZIP
  kernel the canary has been inert on the 9-sector disks since 2026-09-05,
  and the 12 -> 13 step broke only the `NOKZIP=1` arm,
  `../followup-canary.md` section 3. Fixed (build, 2026-11-08):
  docs/plans/BOOT3-PLAN.md section 16.6.)

**Recommendation.** Re-derive the band under the real partition (runs start at
`data + BOOT2_SECS`, cylinder-bounded). Over all six shipped system images and
the 11/13/14-sector blob lengths, the legal file sectors are **{57, 106..110}**
(script below); 21 is not in it. Take **file sector 57**: `KSIG_OFF := 22528`
in the Makefile and `KSIG_OFF equ 22528 - BOOT2_KNOBSEC*512 +
BOOT2_SMALLSEC*512` in `boot2.asm` (kern_big memory sector 44, kern_small 46,
both well inside `.text`; under KZIP it lands in the packed tail, which the
`%if KSIG_OFF >= KZ_HEADSEC*512` arm already handles). Fix `crosses()` to start
its walk at `data + boot2_secs` for each blob length, and break it on purpose
by putting KSIG_OFF back at 4096 - the row must go red on `os8088-360.img`.
Better still, see `boot-10`: let the build pick the lowest legal sector instead
of a hand-typed number checked after the fact.

**Bytes:** 0 (an immediate changes value).

### boot-2 (hazard, MEDIUM, now) The splash executes code from crossing runs BEFORE the canary has verified them

**Evidence.** `boot2.asm` 350-377 ticks `spl_tick` after every run once
`SPL_RESIDENT` sectors are aboard; `spl_chrome` far-calls `vid_setmode`
(`.text` 0x7F5, kernel sector 3) and `gfx_rowbase` (0x8C4, sector 4) through
`spw_near`, and `spl_isr` keeps calling `gfx_rowbase` from IRQ0 for the rest of
the load. The canary runs only after the whole load (`boot2.asm` 419-473). On the
1.2MB disk `.text` starts at LBA 42 = head 0 sector 13, so run 1 crosses at
LBA 45 = **kernel sector 3**: both routines are in the half a bad BIOS
delivers wrong, and the first tick jumps into them. The shorten-and-reload can
never run because the machine is already executing garbage. Same on the KZIP
head (read with the same `[b2_runmax]`, `boot2.asm` 336-339).

**Recommendation.** Cheapest correct shape: keep the TRACK bound until the
resident head is aboard, i.e. widen `[b2_runmax]` to the cylinder only after
`SPL_RESIDENT` sectors (move the `mul` at `boot2.asm` 306-310 behind a
`[b2_left]` test, or run the first `read_run` with the track bound). It costs
one extra int 13h on geometries whose first crossing falls inside the head
(1.2MB today; 1.44MB if the head ever reaches 8 sectors) and nothing on 360/720
(run 1 is head-1-only there). The alternative - verify the canary before the
first tick - delays the bar to roughly a third of the load once `boot-1` moves
the canary to sector 44, so it is the worse trade.

**Bytes:** about +8 blob (transient). kern_big's blob has 4 bytes free
(`boot2_end` 0x19FC against 0x1A00), so it wants `boot-7`'s 41 bytes first.

### boot-3 (bug, MEDIUM, now) A canary reload resets the splash's `.text` state while the blob still thinks the splash is up

**Evidence.** `.rerun` (`boot2.asm` 525-529) reloads from `.text` sector 0,
which re-lands `spl_live`, `spl_busy`, `spl_quiet` (viddet.inc 1753-1796,
`.text` 0x922-0x925) and the `vid_*` block (0x753-0x795) with their image
values. `[cs:spl_on]` is in the blob and stays 1, so the second pass's
`spl_tick` skips the first-tick branch (`splash.inc` 241-244) and never raises
`spl_live` again. Then: `spl_isr` refuses every frame (`splash.inc` 1180), the
bar draws with the image's default `vid_*` (the wrong segment and stride on a
CGA or Hercules), `vid_init` sees `spl_live = 0` and performs the mode set the
splash was protecting (wiping the screen), and every later `spl_step` takes the
"finished" exit (`splash.inc` 346-348) - the loading screen is gone for the
rest of the boot, on exactly the machines the reload rescues. Separately, the
DMA re-landing sector 4 writes `spl_busy = 0` underneath an `spl_isr` frame
that holds it with `sti` (`splash.inc` 1195-1202), so a nested IRQ0 can start a
second frame through the same Graphics Controller state.

**Recommendation.** Gate the first-tick branch on the kernel byte instead of
the blob byte: `cmp byte [spl_live], 0 / jne .running` in place of `cmp byte
[cs:spl_on], 0 / jne .running`, keeping the `mov byte [cs:spl_on], 1` (which
`spl_step` still needs). After a reload the next tick re-probes, re-chromes and
re-raises `spl_live`; `spl_hook` is already guarded by `[cs:spl_hooked]`. The
nested-frame race closes too, because the reload's first tick re-takes the
lock. One fewer `cs:` prefix.

**Bytes:** -1 blob.

### boot-4 (hazard, MEDIUM, now) The SPL_RESIDENT guard stops four bytes short of what the first tick writes, and the rung is four sectors too big

**Evidence.** `kernel.asm` 8653: `SPL_RES_SIZE equ spw_resident_end - $$`. The
first tick WRITES `spl_live` and `spl_busy` (`splash.inc` 237-241) and reads
`spl_quiet`, all placed *after* `spw_resident_end` (`viddet.inc` 1747-1796).
SPEC.md 2.9.4 is explicit that "a first-tick store belongs in the head or in
the blob, never past it" - and the guard does not cover the four stores it
names. Today they sit at 0x922-0x925 with the line at 0x922, so nothing is
broken; the label is just in the wrong place. Meanwhile `SPL_RESIDENT` is 9
(`splash.inc` 36) against a measured need of **0x926 = 2,342 bytes, 5 sectors**
on kern_big and 0x846 = 2,118 on kern_small. `viddet.inc` 1743 says the door
"lands in sector 7 ... and why it fell from 9 to that" - neither half is true
(it is sector 4, and the constant is still 9). Under KZIP, `KZ_HEADSEC =
SPL_RESIDENT` (`Makefile` 2828), so four sectors of `.text` ship **unpacked**:
`kernel.kz.json` has `head 4608`, against a tail ratio of 73,904/92,082.

**Recommendation.** (a) Move `spw_resident_end` below `spl_quiet` (0 bytes)
so the guard covers the bytes the first tick stores. (b) Derive the rung:
`SPL_RESIDENT equ (SPL_RES_SIZE + 511) / 512` at the foot of `kernel.asm`, and
have the Makefile read it off the `ks:` `%warning` line instead of `sed`-ing
`splash.inc` - there is no circularity, because `SPL_RESIDENT` only reaches
`.boot2` immediates. Before lowering it, audit every store the first tick can
make (vid_setmode, vid_wipe, vid_cga_equip, vid_6845_prog, bvs_apply) for an
address in sectors 5-8: SPEC.md 2.9.4's `ui_post` incident is exactly that
class. Payoff: the plain head goes 4,608 -> 2,560 bytes on kern_big.

**Bytes:** 0 resident; about -400 bytes of KERNEL.SYS on kern_big at the tail's
80% ratio (one sector, and on the 360KB disk a cluster nearer the edge); the
bar's start does not move on any geometry, run 1 being 9+ sectors everywhere.

### boot-5 (hazard, MEDIUM, now) A hard-disk boot reads its own BPB with a geometry the hard-disk sector deliberately stopped trusting

**Evidence.** `boothd.asm` 16-36 (and docs/FIELD-NOTES.md 33): the geometry
that wrote the volume is the BPB's, and int 13h AH=08h can disagree; the
sector reads with `bpb_spt`/`bpb_heads`. But `dsk_boot_from_x` (`disk.inc`
2786-2808) asks **AH=08h** for `[dsk_bootspt]`/`[dsk_boothds]`, and
`disk.inc` 4076-4081 uses exactly those to fetch the boot partition's BPB - the
one read that turns `bpb_hidd` into a CHS. Where the two geometries differ (the
FIELD-NOTES 33 machine), boothd boots and the kernel then fails to mount the
volume it booted from: no SYSTEM.CFG, no HDD.DRV, a desktop that looks like it
works. The system disagrees with itself one layer below where 33 fixed it.

**Recommendation.** Hand the BPB's geometry over: boothd sets `SI = [spt]`,
`DI = [bpb_heads]` before `jmp KERNEL_SEG:0` (8 bytes, which `boot-6` pays
for), and `dsk_boot_from_x` takes them when they are sane (1..63, 1..255),
falling back to AH=08h otherwise - an old VBR chaining a new KERNEL.SYS leaves
SI/DI as junk and must still boot. Write the handoff down as a contract (DL,
BX:CX, SI, DI, BP) in SPEC.md 52.10.3; today it exists only as comments, two of
which are wrong (see mismatches).

**Bytes:** +8 boothd (boot sector); about -20 `.ovl` (the AH=08h block and its
four push/pops shrink to two checked stores; transient).

### boot-6 (weakness, MEDIUM, now) `boothd.asm`'s RAM check asks the question SPEC.md 2.7.1 retired, and does not ask the one that matters

**Evidence.** `boothd.asm` 121-129 subtracts `RELOC_ADJ` first and compares
the relocated segment BASE against `KERNEL_SEG + ksecs*32 + BOOT_STACK/16` -
exactly the shape `boot.asm` 232-236 calls out as demanding "31,744 bytes of
headroom that nothing was ever going to use". It never mentions the blob,
which this sector reads to `BLOB_SEG` (line 237-239), above the image by the
FAT window, `.lowbss` and `.vgabuf`. Today the over-strictness happens to
cover the blob; nothing asserts that it does. `BLOB_SEG` is already an injected
immediate here.

**Recommendation.** Do what stage 1 does: `cmp ax, MEMNEED` against the TOP
before the `sub`, with `MEMNEED = max(BLOB_SEG + BOOT2_SECS*32 + 160,
RELOC_ADJ)` computed at assembly time (under KZIP add the packed tail's end,
which `boot2.asm`'s `%error` already bounds below BLOB_SEG). That drops the
`[ksecs]` load and shift.

**Bytes:** about -9 boothd (`mov bx,[ksecs] / mov cl,5 / shl bx,cl / add bx,imm
/ cmp ax,bx` is 13 bytes; `cmp ax,imm16` is 3, plus the `sub` it already has).
The sector has **7 bytes free** (last non-zero byte at 496 of 504), not the 23
or 25 its comments claim.

### boot-7 (size, MEDIUM, now) Stage 2's copy of the DPT patch is dead, in a blob with four bytes free

**Fixed (build, 2026-11-08)**: the block and stage 2's `DPT_AT` are deleted, -41 blob on every kernel (docs/plans/BOOT3-PLAN.md 16.6).

**Evidence.** `boot.asm` 300-334 patches int 1Eh before stage 1's first
multi-sector read (SPEC.md 2.9.8); the header comment says stage 2's copy is
"now idempotent rather than necessary" (line 318-319). Nothing enters
`boot2_entry` except stage 1 (FLAT_PAYLOAD jumps to KERNEL_SEG; the hard disk
never enters stage 2, SPEC.md 2.9.13.5), and `kmain_o`'s `dsk_dpt_init_x`
covers the hard-disk boot. `boot2.asm` 262-284 is 41 bytes.

**Recommendation.** Delete the block (keep the `DPT_ROM` knob meaning by
leaving stage 1's `%ifndef DPT_ROM` as the one site). kern_big's blob ends at
0x19FC of 0x1A00; this is the headroom `boot-2` and `boot-5`'s kernel side
need.

**Bytes:** -41 blob on every kernel.

### boot-8 (consistency, LOW, now) `.bss` arrives ZEROED by the load, and three comments and SPEC.md 60.1 say it does not

**Evidence.** In the shipped kern_big the file between `kernel_text_end`
(file 0xB788) and the next section (`.ovl` at 0xCE00) is all zero bytes, and
`.bss` (0x9D88-0xB27E in memory) maps entirely into that gap; every loader
(stage 2, `boothd`, `kz_expand`) writes it. `kmain_o` relies on it ("NO
loader_init ... .bss is zero at boot (SPEC.md 2.5)", `kernel.asm` 5880-5886).
Contradicting it: `kernel.asm` 5813 ("`-f bin` zeroes no .bss, so until this
runs those slots hold whatever was in memory"), `cpudet.inc` 29, `bootprof.inc`
43, `kernel.asm` 5227, SPEC.md 60.1. The property holds by construction
(`COLD_START`/the next file section is derived from `KTEXT_SIZE + KBSS_SIZE`)
but nothing asserts it.

**Recommendation.** One `%if` beside the section-end guards: the first file
section after `.text` must start at or past `BOOT2_PAD + KTEXT_SIZE +
KBSS_SIZE`, with an error naming `kmain_o`'s reliance. Then correct the
comments (mismatches below). Do not move any data on the strength of it: a
byte in `.bss` is as resident as a byte in `.text`, and `CPU_8086` is 0 either
way.

**Bytes:** 0.

### boot-9 (weakness, LOW, now) Comment archaeology: the three hand rungs carry their history, and the history is now wrong about the present

**Evidence.** `kernel.asm` 1983-2138 (BOOT2_SECS: "The six that took it from 13
... 18 is 36 bytes short ... 19 takes it ... 22 buys 1.44MB a third call", for a
constant that is 13 and has been since pass 14; "It is nine on kern_small
too", for a kernel at 11); `boot2.asm` 51-115 (KSIG_OFF: eight values in a
row, ending in a band "21, 57 and 106..110" that `boot-1` shows is wrong);
`splash.inc` 36-75 (SPL_RESIDENT: three raises, none current). Each block's own
opening line says to read the constant rather than the prose ("THE TOTAL IS
THIS, so read those two rather than a number written out in prose"), and the
prose then gives numbers anyway.

**Recommendation.** For a derived or hand-set constant, the comment states the
INVARIANT and the guard that enforces it, and nothing else; the ledger of past
values moves to `docs/history/` once (the BOOT2_SECS one is ~150 lines). That
is the convention `docs/history/KERN-BUDGET-LEDGER.md` already uses for the
budgets.

**Bytes:** 0.

### boot-10 (rethink, LOW, now) Let the build CHOOSE KSIG_OFF and SPL_RESIDENT instead of checking hand-typed values after the fact

**Evidence.** KSIG_OFF is typed twice (Makefile 1021, `boot2.asm` 57) and
checked by `t_canary` against a model; SPL_RESIDENT is typed once and `sed`-ed
by the Makefile (2828). Both have moved half a dozen times and every move was a
manual re-derivation; `boot-1` is what one wrong model costs.

**Recommendation.** One host tool (it is `t_canary`'s `crosses()` with the
start fixed) computes the band from the shipped geometries and blob lengths and
emits `-DKSIG_OFF=<lowest legal>` to the kernel's assembly; `boot2.asm`
consumes it as it consumes KZ_*. KSIG_OFF only reaches a `mov si, imm16` in
`.boot2`, so `.text` cannot move with it. SPL_RESIDENT as `boot-4`. Keep
`t_canary` as the independent second reader, now with the right model.

**Bytes:** 0.

### boot-11 (perf, LOW, now) `sch_isr` draws a 28.7 ms spinner frame with IF=0 during the whole of `kmain_o`

**Evidence.** `sched.inc` 2318-2353: from `sched_init` to `spl_finish`, every
phase change (one per 4 ticks, `SPL_SPINSH` 2) runs `spl_anim` inside IRQ0
without `sti` - deliberately, to avoid re-entering the scheduler. That blocks
IRQ6 (the floppy's completion, so every boot-mount and driver read in that
window may wait up to a frame) and IRQ4 (the mouse; `spl_quiet` covers only the
identify window). At one frame per ~220 ms that is roughly 13% of the CPU and
up to 28.7 ms of latency on each disk completion it lands on.

**Recommendation.** During `kmain_o` the main line already notches the bar
through `splf_step`; let `sch_isr` only set a "frame due" byte and let
`spl_step`/`spl_stepq` draw it on the main line (they hold the lock already).
The animation becomes notch-paced during kmain - which is what it was before
15.3.8 and is the part of the boot the user watches least. Measure on MartyPC
first (`os88boot.py` phase times) before buying it.

**Bytes:** about -10 `.text` (the far call and its staging leave sch_isr),
+6 blob.

### boot-12 (hazard, LOW, record) A floppy image written to a USB stick boots drive A

**Evidence.** `boot.asm` 294-298 forces any DL above 3 to 0. A user who `dd`s
`os8088.img` (instead of the live image, SPEC.md 80) to a stick that the BIOS
boots as USB-HDD (DL = 80h) reads drive A: `Disk error` with no floppy, or the
kernel off whatever disk is in A with one. The comment's "a range check cannot
pick up the wrong disk" is true only of the junk-DL case it was written for.

**Recommendation.** No code (the sector's CHS geometry is wrong for a stick
anyway). Say it in docs/LIVE-MEDIA.md's troubleshooting: the floppy images are
not USB images.

**Bytes:** 0.

### boot-13 (consistency, LOW, now) The floppy handoff leaves BX:CX as residue; two comments promise zeros

**Evidence.** `boot2.asm` 460-508: after the canary BX is 0 and CX 0, but on
the `.nocross` path (every 286, every TRACKRUN build) BX/CX are what
`read_run` left (CL = 5, CH = a cylinder), and under KZIP `kz_all` leaves BX a
segment. `kmain_o` (`kernel.asm` 5603) and `dsk_boot_from_x`'s header
(`disk.inc` 2697) say "both 0". Harmless today (the floppy arm ignores
BX:CX), but it is the kind of contract a later reader extends.

**Recommendation.** Correct the two comments (below) and write the handoff
table once (`boot-5`). Do not spend blob bytes zeroing them.

**Bytes:** 0.

### boot-14 (size, LOW, record) Stage 1's runtime DMA-page bound for the blob is an assembly-time fact

**Evidence.** `boot.asm` 408-420: the destination is `HEAP_PARA`, an
immediate, and the geometry is per-image constant, so whether the blob read
crosses a 64KB page is known when the sector is assembled. The comment that
justifies the runtime code (lines 379-387) still describes the retired
top-of-RAM landing.

**Recommendation.** Only worth it if stage 1 needs the room (36 bytes free
today): replace the bound with a `%if` that refuses an image whose blob would
straddle, about -20 bytes of boot sector. FLAT_PAYLOAD keeps the runtime form.
Fix the comment either way.

**Bytes:** about -20 boot sector, not RAM.

### boot-15 (consistency, LOW, record) Shut Down uses bare `cli`/`sti` and leaves hard-disk heads where they are

**Evidence.** `shutdown.inc` 24 and 54: a bare `cli ... sti` pair, against
CLAUDE.md's "pushf/cli ... popf, never cli/sti". It is a terminal path that
wants IF=1 afterwards, so it is not wrong, but it is the one place in this area
that breaks the rule without saying so. Separately, "You can now turn off your
computer" (line 328) on an XT with an ST-225 leaves the heads over data;
period practice was PARK.COM (seek to the landing-zone cylinder).

**Recommendation.** A comment on the pair. Parking is a HDD.DRV verb Shut Down
could call when a hard disk is mounted (int 13h AH=0Ch to the last cylinder,
~20 bytes of CTRL.DRV or HDD.DRV, never resident) - record it, do not build it
until a field machine asks.

**Bytes:** 0 now; ~+20 module if taken.

### boot-16 (rethink, LOW, record) Stage 2 could walk the FAT, and "KERNEL.SYS first and contiguous" would go

**Evidence.** `boot.asm` 341-350, `boothd.asm` 52-57, SPEC.md 52.10.15's
`kofs` patch site: both loaders read KERNEL.SYS as a flat run, which is why the
installer must put it first (or find a free run and patch `kofs`) and why
`os88disk.py` must allocate it first.

**Prior art.** MS-DOS 5+ did exactly the split os8088 now has - the boot
sector loads the first three sectors of IO.SYS, and that loader walks the FAT
for the rest - which is what let `SYS` stop needing IO.SYS at cluster 2.

**Recommendation.** Record only. A FAT12 walker in stage 2 is ~100 bytes of
blob, which kern_big does not have, and the contiguity constraint has cost one
patch site so far. Revisit if a non-destructive install keeps failing to find
a run.

**Bytes:** +~100 blob if taken.

### boot-17 (rethink, LOW, now) `kmain_o` as a TABLE, not a call sequence

**Evidence.** `kernel.asm` 5597-6040: about 35 init calls of three kinds
(`OVLCALL` far to FAT_SEG, `BLOBCALL` near, far shims to `.text`/`.cold`),
interleaved with 31 hand-numbered `MARKW n` and 8 `BPMARKW n` sites whose
numbers must not be renumbered ("they are counted, not named, so renumbering
would move every BOOTHALT number after it for nothing", 5880-5886).

**Prior art.** AmigaOS's resident list: every module carries a RomTag with a
priority and flags, and `InitCode` walks the sorted list - the order is data,
and a diagnostic can name the module that hung by its tag. OS/2 and Windows
3.x drive the late half of boot from CONFIG.SYS/SYSTEM.INI for the same reason.

**Recommendation.** A blob-resident table of `(kind byte, offset word)`
entries walked by one loop, with MARK/BPMARK emitted by the loop from the
entry index (so BOOTMARK and BOOTPROF stop needing per-site macros and cannot
be misnumbered). The conditional sites become conditional table rows.
Measure before committing: the win is in the knob builds and in readability,
and on the shipped kernels it is roughly 3 bytes a site against 5.

**Bytes:** about -30 to -60 blob (transient); 0 resident.

### boot-18 (weakness, LOW, record) Guard 5 omits BLOB_LIFT

**Evidence.** `kernel.asm` 9684: `KERNEL_SEG*16 + KERN_SIZE + BOOT2_PAD`;
the blob actually sits at `BLOB_SEG_AT = HEAP_SEG + BLOB_LIFT` (1913-1933),
1,536 bytes higher on a ROM_COLD build. Every ROM_COLD build today is a knob
build (guard 5 exempt) and stage 1's own `MEMFIT` uses the lifted `HEAP_PARA`,
so nothing ships wrong.

**Recommendation.** Use `BLOB_SEG_AT` in the sum so the guard and stage 1
agree by construction.

**Bytes:** 0.

### boot-19 (weakness, LOW, now) `boot.asm`'s defaults would build a wrong sector silently

**Evidence.** `boot.asm` 49-54: `KERNEL_SECTORS 16` and `BOOT2_SECS 2` "must
match kernel/kernel.asm's own constant" - it has been 11-14 for a long time.
`HEAP_PARA` has no default and fails loudly; these two would not.

**Recommendation.** `%error` when either is missing on the kernel (non
FLAT_PAYLOAD) arm.

**Bytes:** 0.

## Rethinks

- **`boot-10` + `boot-4`: derive the hand rungs.** KSIG_OFF, SPL_RESIDENT and
  the KZ head are all functions of the build and the shipped geometries. Every
  past fault in this area (the 1.2MB canary, the 12 -> 13 canary, three
  SPL_RESIDENT raises, the CX-clobbered run bound) was a hand number that
  stopped matching what it described. The tools that check them already compute
  the right answer.
- **`boot-17`: the init order as data** (AmigaOS RomTags). Cheaper, safer
  diagnostics, and an order that a test can read.
- **`boot-16`: a FAT-walking stage 2** (MS-DOS 5). Record; not affordable in
  kern_big's blob today.
- **One handoff record instead of registers** (`boot-5`, `boot-13`). Three
  loaders (stage 2, boothd, and the KZ door) hand the kernel DL, BX:CX, BP and
  a published `[spl_fseg]`, and the meaning of each depends on which loader
  ran. A 16-byte record at a fixed low address (`0060:0004..000F` is already
  the precedent) - drive, partition base, spt, heads, t0, loader id, cylrun -
  would make the contract a structure a test can read. It moves the boot
  timer's and cylrun's words, so it is an internal flag day for the loaders and
  the kernel together (both ship on the same disk, so no package ABI moves);
  bytes about neutral.
- **What NOT to rethink:** the cylinder-run read itself. DOS never crossed a
  head in one call and paid for it in calls; os8088's batching is the largest
  single boot win in the tree (SPEC.md 18.93). The canary is the right price -
  it only has to be in the right sector.

## Prior art

| system | how it boots / inits, at the level of the model | better than os8088 | os8088 better | cheap to adopt |
|---|---|---|---|---|
| **MS-DOS 2-6** | Boot sector loads IO.SYS (from 5.0, only its first 3 sectors; that loader walks the FAT); SYSINIT relocates itself to the top of memory, copies the DPT to 0000:0522 with its own EOT/head-settle values, processes CONFIG.SYS, loads COMMAND.COM | FAT-walking second stage (no contiguity rule); a plain-text, user-editable init order (CONFIG.SYS) | os8088 verifies what it read (blob sum, canary); DOS trusts every CF=0. DOS never batched across a head; os8088 reads a cylinder per call. DOS's init code stays resident until the end of SYSINIT and its relocated SYSINIT is lost memory in practice; os8088's blob rejoins the heap floor | the FAT walk (record, `boot-16`) |
| **Windows 3.0/3.1** | Boots from DOS: WIN.COM picks the mode, KRNL386/KRNL286 load USER/GDI and the drivers SYSTEM.INI's `[boot]` names, then the shell | declarative driver list; the mode decided before any of it loads | os8088 decides the adapter at the first tick with one binary for three adapters (SPEC.md 39), where Windows needed Setup to pick a driver set and a reboot to change it | nothing new - `SYSTEM.CFG` already plays SYSTEM.INI's role |
| **OS/2 1.x** | Boot sector + OS2LDR with a **mini-FSD** (a small file system driver used only to load the kernel and base drivers), then CONFIG.SYS's `DEVICE=` order; the boot drivers are discarded once the real FSD loads | the boot-time reader is a *disposable driver*, not code in the kernel | os8088's equivalent - `dsk_boot_from_x` claiming the boot partition as a `DVK_BIOS` row so the kernel can read before HDD.DRV exists (SPEC.md 52.10.3) - is the same idea in 100-odd transient bytes | already adopted |
| **Mac System 5-7** | Boot **blocks** (two sectors) carry a parameter header: boot-block version, names of the System and Finder files, heap sizes, a flags word, read by the ROM; then INITs load in sorted order, each showing its icon along the bottom of "Welcome to Macintosh" | configuration in a documented header at fixed offsets, editable by tools without reassembling (ResEdit/"Startup Disk") | os8088's §15.6 status line tells the user what the boot is waiting on (mouse, drive B:, drivers), which INIT icons never did; the splash never delays the load | os8088 already has the seed: `BS_CYLASK` at 509 and boothd's `KSECS`/`KOFS` at 504-509 are patch sites at documented offsets. Name the region a *boot parameter block* in SPEC.md so the next flag goes there rather than finding its own byte |
| **GEOS (PC/GEOS 1.x-2.0)** | Starts from DOS (GEOS.EXE / LOADER.EXE), reads GEOS.INI for the video/mouse/printer drivers and the system libraries, loads the kernel as a "geode", and treats every driver as a geode with the same load/unload machinery as apps | drivers and apps share ONE loader and one handle-based relocation model | os8088 boots on bare metal in ~10 s on an XT; GEOS needed DOS underneath and most of a 640KB machine; os8088's `.DRV`/module loaders are smaller | none here; the geode idea is the driver reviewer's |
| **AmigaOS 1.x** | Kickstart in ROM scans for **RomTags** (resident modules with priority and flags) and initialises them in priority order; the floppy's **boot block carries a checksum** the ROM verifies before running it; KickStart 1.x captured "cold capture" vectors for resident debuggers | init order as data (`boot-17`); a boot block the ROM refuses if its sum is wrong | os8088's stage 1 verifies stage 2 itself (BLOBSUM) - the same protection without needing the ROM's help, which a PC BIOS would never give | the RomTag table (`boot-17`) |

## Doc/code mismatches

Each entry: file:line, the current text, what the code does, and proposed text.

1. **kernel/kernel.asm:15** - "0060:0008  boot splash tick (SPEC.md 15): far-called by the boot sector". The splash is in `.boot2` and called near by stage 2; 0x08..0x0B are free (`kernel.asm` 2364-2369). Proposed: `;   0060:0004  boot_cylrun - the loader's head-crossing finding (SPEC.md 18.93.1)` / `;   0060:000C  boot_ticks, 000E the debug registry (SPEC.md 15.4, 57)`.
2. **kernel/kernel.asm:9** - "there is no far code and no second code segment". `.cold` runs at COLD_SEG and is far-called throughout (`call COLD_SEG:mem_unblob_x`, 6121), the blob at `[spl_fseg]`, `.ovl` at FAT_SEG. Proposed: "kernel calls are near within `.text`; `.cold` (COLD_SEG), the boot blob and the boot overlay's window half are separate segments reached by far calls through fixed shims (SPEC.md 2.5, 2.6, 33)."
3. **kernel/kernel.asm:117** - "FAT_SEG     mount-time FAT snapshot     FAT_PARA" (the ladder diagram, 113-121). On the non-ROM_COLD arm FAT_SEG follows `.cold` (`FAT_SEG equ COLD_RAM + COLD_PARA`, 1794); the diagram has no `.cold`, `.vga` or `.vgabuf` rung, and on the ROM_COLD arm the order is KERNEL, FAT, LOW, [VGA], COLD, [MEGA], VGABUF (1656-1898). Proposed: replace the diagram with "KERNEL_SEG .text+.bss | FAT_SEG | LOW_SEG .lowbss + task 0's stack | [.vga] | .cold | [.mega] | .vgabuf | HEAP_SEG - the ROM_COLD order; `kernsize`'s `ks:` line prints the ladder this build has (KS_COLDS, KS_FATS, KS_LOWS, KS_VGAS)."
4. **kernel/kernel.asm:2279** - "reads the whole blob before it jumps and stage 2 copies it to HEAP_SEG, so". Stage 1 reads the blob straight to HEAP_PARA; there is no copy (`boot.asm` 103-108, `boot2.asm` 135-143). Proposed: "reads the whole blob to the heap's floor before it jumps (SPEC.md 2.9.5), so".
5. **kernel/kernel.asm:2114** - "The six that took it from 13 are SPEC.md" (and 2122 "18 is 36 bytes short", 2134 "20 and 21 change no call"). `BOOT2_SECS` is 13 (`kernel.asm` 2099). Proposed: delete 2100-2138's history and keep "THE TOTAL IS `BOOT2_SECS` above; what each sector costs per geometry is `tests/unit/t_blobruns.py`'s table" (`boot-9`).
6. **kernel/kernel.asm:2032** - "nine on kern_small too, which has no such copy". kern_small's blob is 11 (`13 - BOOT2_SMALLSEC 2`). Proposed: delete the paragraph (it is superseded by the THIRTEEN paragraph below it).
7. **kernel/kernel.asm:8655** - "raise SPL_RESIDENT in BOTH kernel/splash.inc and boot/boot2.asm". `boot2.asm` defines no SPL_RESIDENT; the Makefile `sed`s it out of splash.inc for KZ_HEAD (2828). Proposed: "raise SPL_RESIDENT in kernel/splash.inc (the Makefile reads KZ_HEAD from it), or move what grew (SPEC.md 2.9.4)".
8. **kernel/kernel.asm:5603** - "boot BX:CX are 0 and DL is 0 or 1". Stage 2 does not set BX/CX (residue: `read_run`'s CL=5/CH=cylinder, or a segment under KZIP), and DL may be 0..3 (`boot.asm` 294). Proposed: "On a floppy boot DL is the unit (0..3) and BX:CX are whatever stage 2 left - the floppy arm ignores them".
9. **kernel/disk.inc:2697** - "BX:CX = the partition base (both 0". Same. Proposed: "BX:CX = the partition base on a hard-disk boot (boothd.asm sets them); undefined on a floppy boot, where this routine does not read them".
10. **kernel/kernel.asm:5813** - "`-f bin` zeroes no" (".bss, so until this runs those slots hold whatever was in memory"). The load writes the file's zero gap over `.bss` (`boot-8`), so the slots hold 0000:0000 - still a bad far-call target, which is the real reason. Proposed: "`.bss` arrives as the file's zeros, so until this runs every slot is 0000:0000 - the IVT, not a routine".
11. **kernel/cpudet.inc:29** - "-f bin zeroes nothing at boot". As above; and the initialiser is CPU_8086 = 0 either way. Proposed: ".text and not .bss by choice: the safe answer is the image's own byte whichever loader ran".
12. **kernel/cpudet.inc:26** - "with two writers that each own their bits". Three: xmem (A20/HMA/UNREAL), this probe (X87), and `mega_enter` (CPU_F_PM, line 62). Proposed: "with three writers that each own their bits: XMEM.DRV the three memory bits, this probe CPU_F_X87, and kern_mega's mega_enter CPU_F_PM".
13. **kernel/bootprof.inc:43** - "`-f bin` zeroes nothing, and a mark that was never reached". As 10. Proposed: "initialised in .text so a mark never reached reads 0 on every loader, including one that does not zero .bss".
14. **kernel/viddet.inc:1743** - "Here it lands in sector 7, which is what SPL_RESIDENT" (and 1752 "967 bytes left"). `spw_resident_end` is `.text` 0x922 (sector 4) and SPL_RESIDENT is 9. Proposed: "Here it lands in `.text` sector 4; SPL_RESIDENT (splash.inc) must cover it and the four bytes below it (`boot-4`)".
15. **kernel/splash.inc:4-5** - "it far-calls spl_tick (via the pinned 0800:0008 entry)"; and 18-21 "State lives in in-module data words, never .bss: .bss starts at the image end, where the final sector read lands". Stage 2 near-calls `spl_tick` in its own segment (`boot2.asm` 368); the module is `.boot2`. Proposed: "Stage 2 calls spl_tick NEAR after every run once SPL_RESIDENT sectors of `.text` are aboard (SPEC.md 2.9.4). State lives in the blob (`[cs:]`) except spl_live/spl_busy/spl_quiet, which are `.text` bytes in viddet.inc because the IRQ0 handlers read them through DS."
16. **kernel/splash.inc:37** - "(must match boot/boot.asm)". Nothing in boot.asm reads it. Proposed: "(the Makefile reads it for KZ_HEAD; the %if at the foot of kernel.asm checks it)".
17. **boot/boot.asm:379-380** - "The blob lands just under the top of" ("conventional RAM, in [top-8192, top-2560)"). It lands at HEAP_PARA (369). Proposed: "The blob lands at HEAP_PARA, a build constant, so whether it straddles a 64KB page is fixed per kernel; the bound stays because FLAT_PAYLOAD shares the loop".
18. **boot/boot.asm:53** - "must match kernel/kernel.asm's own constant" (default 2). Proposed: `%error` in place of the default (`boot-19`).
19. **boot/boot2.asm:6** - "Everything the sector could not afford is here: the int 1Eh". Stage 1 patches int 1Eh since SPEC.md 2.9.8 (`boot.asm` 300-334). Proposed: "...is here: read_run's bounds, the 286 head-cross gate (18.93.2), 18.93.1's canary and its shorten-and-reload. (The int 1Eh table is stage 1's since 2.9.8.)" - and delete the block (`boot-7`).
20. **boot/boot2.asm:13** - "IS ONE BINARY FOR THREE GEOMETRIES". Four (SPEC.md 19, 2.9.2). Proposed: "IS ONE BINARY FOR FOUR GEOMETRIES".
21. **boot/boot2.asm:113** - "21, 57 and 106..110, and 21 is the bottom of". Under the real partition (stage 2 starts at data + BOOT2_SECS) the band is {57, 106..110}; 21 is inert on kern_big 360/720 (`boot-1`, a CODE bug). Proposed after the fix: "The band, over every shipped geometry and blob length with runs starting where STAGE 2 starts, is file sectors 57 and 106..110; 57 is the bottom of it."
22. **boot/boot2.asm:611** - "R is 20,992" (and 603 "the sector has 23 spare bytes"). `kernel.kz.json`: `r` 18,432; boothd has 7 bytes free. Proposed: "R is KZ_RPARA*16 (os88kz.py, 18,432 at build of 2026-11-08) and always far above the head" and drop the spare-byte count.
23. **boot/boothd.asm:7** - "ticking the splash". It does not tick (72-83). Proposed: "pull KERNEL.SYS off the volume - the blob to BLOB_SEG, `.text` to 0060:0000 - publish [spl_fseg] and jump; the kernel starts its own loading screen".
24. **boot/boothd.asm:14** - "Three things differ from boot/boot.asm and NOTHING ELSE does". It also reads two passes (blob and `.text`), never enters stage 2, has no canary, no blob sum, no video setup, takes `kofs`/`ksecs` from patch sites and calls `KZ_HD`. Proposed: "It is a second loader, not a variant: it does stage 1's AND stage 2's job for a hard disk (SPEC.md 2.9.9) and shares only the BPB arithmetic with boot/boot.asm."
25. **boot/boothd.asm:59** - "Assembled with -DKERNEL_SECTORS=<n> by the Makefile, exactly as the floppy". The count is a patch word the installer writes (470-495). Proposed: "Assembled with BLOB_SEG, SPL_FSEG and BOOT2_SECS from the kernel's build; the kernel's length and start are patched in by the installer at BOOTHD_KSECS/BOOTHD_KOFS."
26. **boot/boothd.asm:154** - "25 spare bytes"; **244** - "there are 23 spare bytes here"; SPEC.md 2.9.9 "It fits in the 512 bytes with 23 to spare". The built sector's last non-zero byte is at 496 of 504: 7 free. Proposed: drop the counts ("a sector with single-digit bytes free") or quote `boot-6`'s figure.
27. **boot/boothd.asm:158** - "boot/boot.asm reads the equipment word and sets mode 3 or mode 7" (and 160 "spent here on the geometry probe"). Stage 2 sets the text mode (`boot2.asm` 206-216); boothd has no geometry probe since FIELD-NOTES 33. Proposed: "The floppy's stage 2 sets mode 3 or mode 7 while the kernel lands; this sector has no room for it, and nothing is lost but tidiness."
28. **boot/boothd.asm:444** - "and we set a text mode above". It sets none. Proposed: "and POST left a text mode".
29. **boot/boothd.asm:473-474** - "it ships inside" / "HDD.DRV". It ships as BOOTHD.SYS on the system disk (250-256, SPEC.md 52.10.16). Proposed: "it ships as BOOTHD.SYS beside the KERNEL.SYS it will boot".
30. **boot/boothd.asm:429-430** - "BOOTDIAG=1 makes on the floppy side, which spends its four spare bytes on two hex digits (SPEC.md 18.94)". BOOTDIAG's digits are stage 2's now, with hundreds of bytes (SPEC.md 2.9.10). Proposed: "the trade BOOTDIAG=1 used to make in the floppy sector before stage 2 existed".
31. **tests/unit/t_canary.py:101** - "Mirrors boot.asm read_run's partitioning exactly." It walks runs from the data area; stage 2's runs start at data + BOOT2_SECS (`boot-1`). Proposed (with the fix): "Mirrors boot2.asm read_run's partitioning: cylinder-bounded runs from the first sector AFTER the blob."
32. **SPEC.md:32212-32219** (§15) - "**Boot splash entry — 0800:0008.**" ... "`SPL_RESIDENT` (= 6) sectors" ... "The boot sector defines the same `SPL_RESIDENT` constant". Proposed: "**The splash is stage 2's (§2.9.4).** Stage 2 near-calls `spl_tick` after every run once `SPL_RESIDENT` (9 today; the `%if` on `spw_resident_end` guards it) sectors of `.text` are aboard. Contract: AX = sectors loaded, DX = total; every register preserved; runs on stage 1's stack with DS = KERNEL_SEG and its own data `cs:`; returns `ret`. The kernel reaches the rest of the splash far, through `spl_fp`/`spl_fseg` (§2.9.5.1)."
33. **SPEC.md:32220-32233** (§15) - "The splash module ... is **included first, before every other module but `viddet.inc`** ... Its state lives in in-module data words, **never .bss**: .bss begins at the image's end, so the final sector read lands on top of it". It is `.boot2`; only the four DS bytes stay in `.text`. Proposed: replace with a pointer to §2.9.4 and the rule that survives: "a first-tick store belongs in the head or in the blob".
34. **SPEC.md:32248** (§15 kmain order) - "`sched_init` → `clk_init` (§37 ...". The actual order (`kernel.asm` 5597-6040, 6098-6150): dsk_boot_from → [ROM doorbell] → xm_sniff (kern_big, runs cpu_detect) / cpu_detect → dsk_dpt_init → sched_init → sch_idle_start → clk_init → vid_init → vid_ctx_init (big) → vid_probe_avail → bvs_disp_init (big) → [cfx_slide] → mem_init → mod_fp fill → font_init → wm_init → menu_init → inst_init → mouse_init → desk_init + rowcalc → dock_init → files_init → drv_init → drv_sniff → snd_init → drv_boot (+vmm_boot) → rowcalc → hb_probe (big) → thm_set → spl_finish → vx_boot → as_boot; then kmain: mem_unblob → wm_paint_all → boot timer → cursor_show → drv_notice → ui_task. Proposed: that list, with "kmain_o (the blob) runs everything up to spl_finish; kmain (.text) the rest".
35. **SPEC.md:4041-4057** (§3) - "KERNEL_SEG   equ 0x0800", "KERN_BUDGET  equ 74240", "STK0_SIZE    equ 1024", and a ladder without `.cold`/`.vga`/`.vgabuf`. Code: KERNEL_SEG 0x0060 (`kernel.asm` 27), KERN_BUDGET derived 100,864 / 64,000 / 68,096 (1444-1455), STK0_SIZE 512 (1568), DSK_FAT_SECS 2 on kern_small (1508). Proposed: regenerate the block from `kernel.asm`, or replace it with "the constants are `kernel.asm`'s and `kernsize`'s `ks:` line is their current values" - a §3 that is a copy is a copy that goes stale, which is exactly this.
36. **SPEC.md:4092, 4095** (§4) - "**boot splash entry at 0x0008** (§15)" and "far-ticked by the boot sector per sector read; self-contained, no .bss". Proposed: kernel.asm row: "fixed words at 0x0004/0x000C/0x000E (§15.4, §18.93.1, §57)"; splash.inc row: "stage 2's (`.boot2`, §2.9.4): ticked near by the loader per run, reached far by the kernel through spl_fp".
37. **SPEC.md:2207** (§2.9) - "Measured: **stage 1 is 378 of 510 bytes (132 free), stage 2 is 379.**". Stage 1's last code byte is 472 (36 free to BS_CYLASK); the blob is 6,652 of 6,656 on kern_big. Proposed: "stage 1 is ~470 bytes (36 free below the Cylinder byte); stage 2 shares a 13-sector blob with the splash and the boot overlay's blob half (§2.9.14)".
38. **SPEC.md:2392** (§2.9.5) - "which is stricter than the blob's requirement by about 21KB — **guard 5c** in". Stage 1 now asks the blob question directly (`MEMFIT`, `boot.asm` 135) and guard 5c is retired (`kernel.asm` 9714). Proposed: "Below the top of RAM, because stage 1 refuses a machine that cannot hold the blob at HEAP_PARA under its own 2,560 bytes (§2.7.1); guard 5 asks the same of MIN_RAM_KB."
39. **SPEC.md:2399-2400** (§2.9.5) - "`kmain` points" / "`spl_fp` / `spl_fseg` at `spl_dead` and calls `mem_unblob`". `kmain` zeroes `[spl_fseg]` (`kernel.asm` 6118-6120; §2.9.5.3). Proposed: "`kmain` writes `[spl_fseg]` = 0 - the sentinel every far call through the pair tests - and calls `mem_unblob`".
40. **SPEC.md:112157** (§60.1) - "On `kern_small` nothing ever writes it and it reads 0". The x87 probe writes CPU_F_X87 on both kernels (`cpudet.inc` 207, no `%ifdef`). Proposed: "On kern_small nothing writes the three memory bits; CPU_F_X87 is probed on both kernels."
41. **SPEC.md:112161** (§60.1) - "once from `kmain`, first thing,". On kern_big it runs inside `xm_sniff`, after `dsk_boot_from_x` (`kernel.asm` 5597-5655). Proposed: "once from `kmain_o`, before `sched_init` - on kern_big from inside `xm_sniff`, which needs the tier first".
42. **SPEC.md:103-107** (§1 rule 2) - "CS = DS = `KERNEL_SEG` (0x0800)", "**SS = `LOW_SEG`** (0x0440)", and "there is no far code and no second code segment (§33)". KERNEL_SEG is 0x0060, LOW_SEG is above the image and build-dependent, and `.cold`/blob/`.ovl` are far. The sentence is also garbled ("modules in / there is no far code"). Proposed: "CS = DS = `KERNEL_SEG` (0x0060) for kernel `.text` and every task; SS = `LOW_SEG`, above the image. Calls inside `.text` are near; `.cold` (COLD_SEG), the boot blob and the boot overlay's window half are separate segments reached through far shims (§2.5, §2.6, §33)."
43. **CLAUDE.md:336 and AGENTS.md:305** - "Every inter-module kernel call is near — there is no far code (§33)." As 42. Proposed: "Calls inside `.text` are near; `.cold` and the boot blob are far, through shims (§2.6, §33) - never a far call into `.text` from `.text`."
44. **kernel/kernel.asm:1489** - "The sector refuses to relocate at all when the kernel's read would". It refuses when the BLOB does not fit at HEAP_SEG under its 2,560 bytes (`boot.asm` 110-136). Proposed: "The sector refuses a machine that cannot hold stage 2's blob at HEAP_SEG under its own 2,560 bytes (SPEC.md 2.7.1)".

## Open questions

1. **Does any real BIOS ignore int 1Eh's EOT for multi-track reads on an 8088?**
   The canary's rationale names "a BIOS that keeps its own table"; the only
   observed case in the tree is a 286 ROM after a Cylinder save
   (`tests/megacz.py`). If no 8088 BIOS does, `boot-1` and `boot-2` are about
   the Cylinder option only - still worth fixing (it is the user's explicit
   request being honoured wrongly), but it changes how loudly to say it.
2. **Which geometry did `tests/megacz.py`'s third boot use?** If it was 360KB
   or 720KB the canary there was the inert one, and that test passing for the
   right reason needs re-checking after `boot-1`.
3. **`boot-4`'s audit:** does any first-tick path store into `.text` sectors
   5-8 on any adapter? SPEC.md 2.9.4's `vid_apply` incident says the class
   exists; a host check (`os88ovlchk`-style, following the first tick's call
   graph for stores) would make lowering SPL_RESIDENT safe rather than
   argued.
4. **Should kern_small's 1.44MB disk (`small.img`, on demand, "does NOT cross
   there") be added to `t_canary`'s set** once the model is fixed? Under the
   corrected model file sector 57 may well cross on it.

### Evidence scripts

Both read only `build/*.img`; nothing is written.

```python
# the band under stage 2's real partition (boot-1)
import struct
def geo(n):
    d = open("build/" + n, "rb").read(512)
    rsvd = struct.unpack_from("<H", d, 14)[0]; nfat = d[16]
    fatsz = struct.unpack_from("<H", d, 22)[0]; root = struct.unpack_from("<H", d, 17)[0]
    spt = struct.unpack_from("<H", d, 24)[0]; heads = struct.unpack_from("<H", d, 26)[0]
    return rsvd + nfat * fatsz + (root + 15) // 16, spt, heads
def ok(data, spt, heads, fsec, b2):          # runs start AFTER the blob
    run = spt * heads; want = data + fsec; lba = data + b2
    if want < lba: return False
    while True:
        s0 = lba % run; span = run - s0
        if lba + span > want: return s0 < spt and s0 + (want - lba) >= spt
        lba += span
big = ["os8088-360.img", "os8088-720.img", "os8088-120.img", "os8088.img"]
sm = ["small360.img", "small720.img"]
print([f for f in range(14, 140)
       if all(ok(*geo(n), f, b) for n in big for b in (13, 14))
       and all(ok(*geo(n), f, 11) for n in sm)])     # -> [57, 106, 107, 108, 109, 110]
```

`ok(*geo("os8088-360.img"), 21, 13)` is `False`; `t_canary`'s `crosses()`
answers `True` for the same sector.

## Verification

Adversarial pass, 2026-11-08, against the same `build/` artefacts (both maps,
`kernel.kz.json`, `boot*.bin`, `boothd.bin`, the six shipped system images).
Nothing assembled, nothing booted. Verdicts as in `data/boot.json`.

Findings:

- boot-1 CONFIRMED (high). Re-derived from the images' BPBs: on the 9/2 disks
  `.text` starts at LBA 27 (cyl 1 h1 s1), run 1 is 27..35 on one head, the
  canary is LBA 35. Walking from data+BOOT2_SECS the band is [57, 106..110];
  `t_canary.crosses()` really starts at `lba = data`.
- boot-2 CORRECTED (medium -> high). Mechanism right, exposure wrong: it is not
  only the 1.2MB disk. kern_small's 360KB/720KB disks start `.text` at head 0
  sector 6 and keep `vid_setmode` (0x766) and `gfx_rowbase` (0x7E4) in `.text`
  sector 3, which an EOT-8 BIOS already delivers from the wrong head. On those
  disks this, not boot-1, defeats the canary. kern_big 360/720/1.44MB are not
  exposed.
- boot-3 CORRECTED. The reload/`spl_on` defect is real; the secondary claim (a
  nested IRQ0 frame once the DMA zeroes `spl_busy`) is REFUTED and removed -
  `spl_live` (0x922) is re-landed as 0 before `spl_busy` (0x924), so a nested
  `spl_isr` refuses at its first test.
- boot-4 CORRECTED (medium -> low). Facts right; the uncovered 4 bytes sit
  1,770 bytes below the boundary today, and the ~400-byte win is disk, not RAM.
  The `%error` text naming boot2.asm is code, so it moved to the recommendation.
- boot-5 CONFIRMED. Plus the stale "same question boothd asks" comment at the
  AH=08h call, added to the doc fixes.
- boot-6 CORRECTED (medium -> low). Code and the 7 free bytes confirmed; it only
  over-refuses, by ~31KB, on machines that are 640KB in practice.
- boot-7 CONFIRMED. Only stage 1 enters `boot2_entry`; 41 bytes re-summed.
- boot-8 CONFIRMED. Zero gap checked on both kernels; the contradiction is wider
  than three comments (about 15 in `kernel/`, several in SPEC).
- boot-9 CONFIRMED.
- boot-10 CONFIRMED (spot-check).
- boot-11 CONFIRMED.
- boot-12 CONFIRMED.
- boot-13 CONFIRMED (sharpened: on the `.nocross` path BX holds the boot tick).
- boot-14 CONFIRMED (stage 1 has 35 bytes free, not 36).
- boot-15 CONFIRMED.
- boot-16 unchecked (record item).
- boot-17 unchecked (design suggestion).
- boot-18 CORRECTED (record -> now). Its premise is wrong: ROM_COLD is the
  DEFAULT (`%ifndef NO_ROM_COLD`), not a knob, so guard 5 under-counts kern_big's
  and kern_small's boot by BLOB_LIFT's 1,536 bytes today. Still low: stage 1's
  run-time check uses the lifted HEAP_PARA.
- boot-19 CONFIRMED.

Doc/code mismatches: 45 fixes carried in `data/boot.json`, each old text
checked to occur exactly once and to apply in sequence, and every `.asm`/`.inc`
fix checked to change comment text only. Not carried:

- `kernel.asm` 8655, the `%error` string - REFUTED as a doc fix (it is code);
  folded into boot-4.
- `boot2.asm` 13, "ONE BINARY FOR THREE GEOMETRIES" - REFUTED: the sentence is
  about SPT/HEADS, and the four disks have three (360KB and 720KB share 9/2, as
  boot.asm's own "Four geometries, THREE sectors" says).
- `tests/unit/t_canary.py` 101 docstring - REFUTED as a standalone fix: the
  proposed text describes the walk AFTER boot-1's code fix, and applied alone
  it would claim the walk does what it does not. Change it with the code.
- `CLAUDE.md` 336 and `AGENTS.md` 305, "there is no far code (§33)" - real
  mismatch (`.cold`, the blob and `.ovl` are far-called), WITHHELD: those files
  are agent configuration that a workflow stage may not edit. The owner should
  apply the same wording to both.
- Corrected in passing: SPEC.md 2207 (35 bytes free, not 36); the SPEC §3 block
  (fixed line by line, with a note that it is an excerpt, rather than
  regenerated); `boothd.asm` 429 (stage 1 keeps BOOTDIAG's four-digit sum, and
  stage 2 took only the two status digits); `boot2.asm` 113 (worded as a
  finding while the code still sits at 21).
