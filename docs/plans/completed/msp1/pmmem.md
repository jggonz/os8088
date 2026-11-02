# msp1 agent "pmmem" - memory above 1MB

kern_mega size pass 1 (BRIEF.md), agent **pmmem**: `kernel/mega.inc` lines
8108-13715 at the base `4fb1e6d5` - `.cold` up, the XMEM pool, the LDT
allocator, selectors, claims and regrow, A20, ownership, and the `.mega`
bodies low code calls. Branch `msp1-pmmem`.

## BASE AND TIP

Absolute figures, `tools/kernsize.py` (`make mega` prints them), base
`4fb1e6d5` against the tip of `msp1-pmmem`:

| kern_mega | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.mega` (+scratch) | `.boot2` | `.ovl` | `KERN_SIZE` at boot | resident after the switch |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| base | 47,548 | 5,435 | 44,957 | 7,679 | 336 | 18,237 (+1,493) | 6,620 | 5,109 | 131,584 | 66,560 |
| tip | 47,350 | 5,435 | 45,088 | 7,679 | 336 | 18,150 (+1,500) | 6,620 | 5,109 | 132,096 | 66,560 |
| delta | **-198** | 0 | **+131** | 0 | 0 | **-87** (+7, padding) | 0 | 0 | +512 | 0 |

* **Conventional memory for ever** (`.text` + `.bss` + `.lowbss` +
  `.vgabuf`): **-198 bytes**. `.text`+`.bss` 52,983 -> 52,785, so
  `os88megaseg`'s overhead over kern_big is **4,417 -> 4,219** (12,553 ->
  12,751 left under `KERN_CODE_MAX`); `tests/megaseg.txt` still allows 4,417
  and can be ratcheted down once the four branches are merged.
* **Above 1MB** (`.cold` + `.mega`): **+44 bytes** net (+131 / -87), the
  price of moving 204 bytes of switch code out of `.text`.
* **Total kernel bytes**: **-154**.
* **`KERN_SIZE` at boot +512**: `.cold` crossed its rung by **32 bytes**
  (99 left at the base, the move needed 131). The image rung did not drop
  back (`.text` 265 -> 463 left), so the boot figure rose a rung and the
  resident figure after the switch did not move. Per the banner this is
  reported, not designed around: any 32 bytes of `.cold` that pmcore or
  shared take back uncross it.
* **kern_big BYTE-IDENTICAL**: `tools/os88rdiff.py kernel 4fb1e6d5 .` at the
  tip, **4,084 routines same, 0 at an odd shift**, and `kernsize --json`
  identical in every field. Nothing here is outside `%ifdef KERN_MEGA`, so
  kern_small and kern_emu do not assemble a byte of it.

## TAKEN

Six commits. Bytes are assembled, per section, kern_mega only.

### 1. `mega_coldup` and its two helpers out of `.text` - `.text` -198, `.cold` +201

`mega_coldup` (143), `mega_mdesc` (47) and `mega_hilin` (14) ran ONCE, in
protected mode, from `mega_enter`, and lived in `.text` - 204 bytes of the
64KB window for a routine nobody calls twice. They are `.cold` now
(`mega_coldupf`), behind a six-byte `.text` door that keeps the name, so
pmcore's `call mega_coldup` in `mega_enter` is untouched. The near calls they
made became far ones (`KERNEL_SEG:mega_xdescf` twice, `MEGA_SEL_CODE:mx_lcopyf`
twice: +8).

**Equivalence, by reading** (no row can watch the switch from inside): the
routine now runs in the `.cold` it copies, which is sound for three reasons,
each recorded in its header - (a) the copy is a snapshot of bytes nothing
writes before it returns (IF = 0 and NMI off across the whole switch:
`mega_enter`'s `cli` and port 70h); (b) re-basing COLD_SEG's descriptor
changes no CS already loaded, the hidden cache keeping the old base until a
far transfer, so the instructions after the store still come off the old
rung, which is heap only once `[mem_base]` says so and nothing claims before
this returns; (c) every far transfer after it (`mega_vbaimf`'s call and
retf - which the old `.text` body ALREADY made after the re-base - and this
routine's own retf to the door) lands at the same offset of an identical
copy. `.cold` is reachable at that moment through COLD_SEG's identity
descriptor, which `mega_enter` already proves by calling `mega_vbaimf`
before `mega_coldup`.

### 2. `mega_rung` - one dead rung to the heap, two callers (inside 1's figure)

`mega_gatef`'s real-mode refusal (`.mega`'s rung) and `mega_coldupf`
(`.cold`'s rung) both ended in the same six instructions: `mega_vbaimf`, the
`VGABUF_PARA` lift, `[mem_base]`. One near `.cold` routine now; 8086 code,
because the refusal runs on an 8088 too.

### 3. The claim family's duplicates - `.cold` -70

* `mega_claimfreef` carried its own copy of `mega_claimfind`'s owner-and-
  selector walk and of `mega_mxbase`'s KB-to-linear: it calls both. Same
  CF on every exit (`mega_selfind`'s `stc`, `mega_ownerf`'s carry).
* `mega_claimregrowf` had three nine-pop exits; CF is set before one shared
  tail (`dec byte [sch_lock]` keeps CF), and `.huge`'s `jz .h64 / jmp .nol`
  is `jnz .hnol`.

### 4. `.mega`'s own copies called near - `.mega` -9 (in commit 2), -32 (commit 3)

* `mx_copy` (OSAPI_XMEM_COPY's body) and three more `.mega` callers
  (`mega_imghuge`, `mega_imghif`, ...) went `call KERNEL_SEG:mega_lincopyf`,
  which is `call MEGA_SEL_CODE:mx_lcopyf`, which is `call mx_lcopy`: two far
  round trips through `.text` to reach a routine in their own segment. They
  `call mx_lcopy`. **Faster** as well as smaller (two far call/retf pairs
  per copy fewer). The only observer of the frame is MEGATRAP's `mx_lcchk`,
  which prints the caller's CS:IP and now reads a near frame there - a knob
  build's diagnostic, and only when the copy runs unlocked, which is itself
  the defect it reports.
* `mx_free` and `mx_relall` share `mx_kill` (unsel, KB 0, owner FFh).
* `mega_imghifree`'s two callers are both `.mega`: it is near, and
  `mega_drvhifree` jumps into `mega_pkghifree`'s tail.
* `.mega` is `cpu 286` code (mega.inc:191): five KB conversions take an
  immediate shift count instead of CL (`mega_xmsel` twice, `mx_tokb`,
  `mx_chk`, `mega_slotof`), and `mega_slotof` stops pushing CX. Same cycles
  on a 286 (5 + n either form), and `mx_tokb` now clobbers less than its
  contract says.
* `mega_strcalf` and `mega_fd13f` were doors with **no caller** (deleted).

### 5. `push MEGA_SEL_LDTD / pop es` - `.mega` -30

Ten `.mega` sites spelled `push ax / mov ax, MEGA_SEL_LDTD / mov es, ax /
pop ax`. Same registers, same flags; none is in a per-byte path (the LDT
allocator's entries, the selector walks).

### 6. `pusha` in the switch's two once-only walks - `.mega` -16

`mega_mpall` and `mega_vxpm` run once, from `mega_pmtail` on the boot
stack, and keep every register. The wider `pusha` frame is 6 and 2 bytes
deeper there, on a stack nothing else is using.

### 7. Comments that were wrong (0 bytes)

`mega_pm`'s declaration said `.lowbss` is zeroed (it is not - kmain stores
the 0, kernel.asm:5638); `MX_NBLK`'s said 8 bytes an entry (`MX_BLKSZ` is
10); `mega_claimhf`'s header sat above `mega_gatef`; `mega_lincopy`'s named
`mega_coldup` as a caller.

## REFUSED

* **The four real-mode refusal strings and "kern_mega: " into `.cold`** -
  138 bytes of `.text`, read only by `mega_gatef`'s refusal. Built (a `.cold`
  E9 loop reading `[cs:si]`, the toast through ES = CS) and **refused by
  `os88ovlchk`**: SPEC.md 2.6 rules 1 and 2 forbid data and any CS
  assumption in `.cold`, absolutely. The reading is sound (the strings ride
  with the code that reads them, `.cold` in real mode is at COLD_SEG), but a
  rule with zero exceptions is not one to bend inside a size pass. See
  DEFERRED 1.
* **`push PKG_DISP` in `mega_vxpm`** (-1): AH then holds the code selector's
  high byte instead of `PKG_DISP`'s at the driver's `VXV_PM` call. Whether
  VESA.DRV's dispatcher reads AH was not proved, so not taken.
* **`pusha` in the run-time `.mega` routines that keep every register**
  (DEFERRED 6): stack, not bytes.
* **`push imm / pop es` where the register is dead after a `mov reg, SEL /
  mov es, reg`** (six sites, -1 each): 4 clocks slower each on a 286, and one
  of them is `mx_lcopy`'s setup. Not worth a byte.

## DEFERRED

Estimates are labelled; everything here is the next pass's list.

1. **The refusal strings, 138 `.text` bytes** (REFUSED above). Two ways: an
   `os88ovlchk` exemption scoped to kern_mega's real-mode `.cold` (a marker
   comment, as `ovlchk: builds its own far frame` already is for another
   check), or a named-segment form - `.cold` strings read through ES =
   COLD_SEG, which rule 2 allows, plus a rule-1 exemption for strings so
   read. ~-138 `.text`, ~+150 `.cold`. Needs a SPEC.md 2.6 sentence.
2. **`mega_fbuf`, 60 `.text` bytes** (and `MEGA_FB_*`): the fault reporter's
   message template, written only by pmcore's `.mega` reporter
   (mega.inc:2107-2127) and read by it. It could be composed in `.mega`'s
   writable scratch (MEGA_SEL_PSCR) or on the reporter's stack from a `.mega`
   template read through CS. ~-60 `.text`, ~+70 `.mega`. Cross-file: pmcore
   owns the reporter.
3. **`mx_tab`, 960 bytes of `.lowbss` - conventional memory for ever** - and
   with it `mega_wtab` (128), `mem_sel` (64) and `mega_ustab` (32): 1,184
   bytes of `.lowbss` that only protected mode reads, and that a machine
   which REFUSED protected mode carries for nothing. Two options: (a) put the
   tables in the fixed area above 1MB beside the GDT/LDT/IDT (`mega_layoutf`
   sizes it) behind a GDT data selector, every `[ss:si+MX_*]` (~150 sites)
   becoming a segment the routine loads - the big win, ~-1,184 conventional,
   ~+300 `.mega` estimated, and every `mx_*` routine touched; (b) claim them
   from the heap in `mega_xminit` - still conventional, but zero on a refusal
   and zero on a machine with no pool. (a) is a pass of its own.
4. **The switch-only code is resident above 1MB for ever**: `mega_gatef`
   (115), `mega_coldupf` (~190), `mega_a20`/`a20s`/`a20t`/`kbw` (114),
   `mega_tasks_ok` (44), `mega_tlowf` (37), `mega_tset` (89), `mega_rung`
   (~18), `mega_mdesc`/`hilin` (~65) - ~670 bytes of `.cold` that run once.
   They cannot go to `.ovl`/`.boot2` because `mega_enter` runs AFTER
   `mem_unblob` (kernel.asm:5672 against 5746). If the switch's real-mode
   half (`mega_gatef`: tier, tasks, A20, layout) could run before
   `spl_finish`, it could live in the blob and be given back - a kmain
   ordering question for pmcore/shared, est. -300..-670 bytes of `.cold`
   and of `KERN_SIZE` at boot.
5. **`mx_tab` walk idiom**: `mov si, mx_tab / .l: cmp word [ss:si+MX_KB], 0
   / ... / add si, MX_BLKSZ / cmp si, mx_tab_end / jb .l` is written ~20
   times across `.mega` and `.cold`. An `mx_next` (advance, compare, flags)
   saves ~3 bytes a site where the loop shape allows: est. -40 `.mega`/
   `.cold`, a few cycles per entry in walks that are not per-byte.
6. **`pusha`/`popa` in the run-time preserve-everything routines**:
   `mega_mpnewf` -6, `mega_mpsyncf` -8, `mega_mpfreef` -6, `mega_mpmovef`
   -10, `mega_hifree` -6, `mega_imghifree` -6, and the six-register entries
   whose AX is not an output (`mega_modhif`, `mega_imghuge`, `mega_imghif`,
   `mx_cpmove`'s check) - est. -70 `.mega`. NOT taken because each makes the
   frame 2-8 bytes deeper on whatever task slice claims or frees memory, and
   STACK-SLOTS-PLAN's slices are cut from measured water: `tools/stkwater.py`
   / `stkdepth.py` on the claim paths first.
7. **KB <-> bytes in `.cold`**: `mov dx, ax / shl ax, 10 / shr dx, 6` is
   inline four times in the claim family (`mega_claimhf`, `.grow`, `.h64`,
   `mega_mxbase`'s body duplicates `.mega`'s `mx_kblin`), and `mov cx, bp /
   shl cx, 10 / call MEGA_SEL_CODE:mega_xmself` three times in
   `mega_claimregrowf`. Helpers: est. -20 `.cold`.
8. **`mega_claimregrowf .copy`** re-finds the new block's entry by MX_SEL
   with its own loop; `mega_mxat` (which `.h64` already uses) answers it.
   est. -15 `.cold`.
9. **`mega_xmsel`'s banked window** (`mxs_lo/hi/len`, 6 `.text` bytes of
   data plus 24 of `mov`s): the window could stay on the stack and be read
   with `[bp+n]`. est. -6 `.text`, -10 `.mega`.
10. **`mx_lcopy` reaches `mega_xdesc` through `.text` twice per copy**
    (`call KERNEL_SEG:mega_xdescf`): a `.mega` copy of the 21-byte body is
    +17 bytes and two far round trips off every pool copy - a SPEED trade,
    for whoever profiles the pool (the DOS park's and hibernate's copies are
    the heavy users).
11. **Far doors whose body could be near for some callers**: `mega_imghif`
    and `mega_mpnewf` are far-called from `.mega` itself (`mega_pkghif`,
    `mega_mpall`) as well as from low code; a near entry plus a door would
    save ~1-2 bytes each and a far round trip.
12. **Initialized `.text` data that is written before it is read**
    (`mega_xsrc`/`mega_xdst`, `mxs_*`, the bridge words `mega_bf*`): `.bss`
    instead - no resident change, ~25 bytes off `kernel.sys`.

## WANTS THE OWNER

Nothing taken here changes what anyone sees. DEFERRED 1 and 4 would need a
written rule change (SPEC.md 2.6's `.cold` rules; kmain's order) rather than
the owner's word on behaviour.

## WANTS AN ABI BREAK

None.

## E9 SITES

Every write to the debug port in this range, for the owner's MEGADIAG=1
gating step (not touched in this pass, by the coordinator's instruction).
Bytes are assembled at the tip.

| routine (section) | prints | shipped? | bytes it frees if gated |
|---|---|---|---|
| `mega_modsayf` (`.mega`, far from `mod_hi` in mod.inc:795) | `kern_mega: above 1MB: CTRL.DRV 00FF3800` per module moved above 1MB (string `mega_mdrv`, shared with `mega_drvsayf` outside this range) | yes | the whole routine, 52 `.mega`, plus mod.inc's far call (5 `.cold`); `mega_mdrv` only when `mega_drvsayf` goes too |
| `mega_gatef` `.refuse` (`.cold`, real mode) | `kern_mega: real mode: <reason>` + LF, before the same reason's toast | yes | ~16 `.cold` of calls and the LF, and `mega_mrm` ("kern_mega: ", 12 `.text`); the four reason strings stay, the toast reads them |
| `mega_pmtail` (`.mega`) | `kern_mega: protected mode` (`mm_say mega_mon`; the string is at mega.inc:4956, pmcore's) | yes | 6 `.mega` here, 27 of string there |
| `mega_putsf` (`.text` door) | - (the door `mega_gatef` and pmcore's reporter, mega.inc:1998, reach `mega_puts` through) | - | 4 `.text` once both callers are gated |
| `mx_cplog` (`.mega`) | compactor move log | no: `%ifdef MEGA_TRAPLOG` already | 0 |
| `mx_lcchk` (`.mega`) | `L cs:ip word` for an unlocked copy | no: `%ifdef MEGA_TRAPLOG` already | 0 |

## CROSS-FILE

* **shared** (`kernel/megados.inc`): nine `.mega` callers of
  `call KERNEL_SEG:mega_lincopyf` (lines 905, 933, 1041, 1095, 1356, 1475,
  1492, 1514, 1574) can `call mx_lcopy` - **-18 `.mega`** and two far round
  trips off every DOS-park copy - after which `mega_lincopyf` (mine, `.text`)
  has no caller and goes: **-6 `.text`**. Line 1465's
  `call MEGA_SEL_CODE:mega_selbasef` from `.mega` can be `call mega_selbase`
  (-2).
* **shared** (`kernel/mod.inc`:907/921, `kernel/driver.inc`:4127):
  `mega_dklibfree` (18) and `mx_relallf` (13) are `.text` only because their
  callers say `KERNEL_SEG:`; both callers are `.cold`. With the callers
  naming `COLD_SEG:` both bodies move to `.cold` (they run in real mode too,
  so not `.mega`): **-31 `.text`**, ~+31 `.cold`.
* **pmcore**: `mega_fbuf` (DEFERRED 2); `mega_enter`'s order against
  `mem_unblob` (DEFERRED 4); `mega_putsf`'s only other caller is the
  reporter at mega.inc:1998.
* **coordinator**: `tests/megaseg.txt`'s 4,417 allowance can come down to
  what the merged tree measures (4,219 on this branch alone).

## ROWS RUN

One pass after the last code commit, `python3 tools/os88test.py soak -j 1
--marty-jobs 1 -k megatbl -k megaown -k megahuge -k megaxmove -k megacaps
-k megainst -k megaflt -k megashed -k megahiber -k brmega -k mega-vesa
-k mega-dosxmsq -k mega-rdxms -k mega-scsize -k megamod` (Bochs) - the switch
itself (every row boots through `mega_coldupf`), the tables' layout, claims
and their free and regrow (`brmega` regrows OS88_HIGHMEM claims, `megahuge`
the MX_FHUGE runs), ownership, the pool's moves, caps and copies, instance
teardown, driver and module images above 1MB, the VESA renderer's switch
(`mega_vxpm`) and the XMS arena.

**14 passed, 1 failed** (882.5 s): `megatbl`, `megainst`, `megashed`,
`megaflt`, `megaown`, `megaxmove`, `megahiber`, `megahuge`, `megacaps`,
`brmega`, `mega-dosxmsq`, `mega-scsize`, `mega-rdxms` and `mega-vesa` ok.

**`megamod` FAILS, and is PRE-EXISTING**: `python3 tools/os88bisect.py
classify --base 4fb1e6d5 megamod` fails at the tip AND at the base, on the
same legs - every leg that plays through SOUND.DRV (`trk`, `wide`, `trkarm`,
`mpp`, `mr`, `mrsb`, `mrarm`) ends in `kern_mega FAULT vector 0D` (one `06`)
at CS 1154h, IP 087F-089A; the legs that only open and read pass. Not this
branch's; it wants an owner, and it is in nobody's range here as far as the
fault address says (an image selector, not kernel code).

## DEFECTS FOUND

None in this range's behaviour. `megamod` fails at the BASE (ROWS RUN) -
reported, not chased. Three stale comments (TAKEN 7), and the contradiction
between `mega_pm`'s comment and `mega_xminit`'s about whether `.lowbss` is
zeroed - the code was right (kmain zeroes `mega_pm` itself), the comment was
not.
