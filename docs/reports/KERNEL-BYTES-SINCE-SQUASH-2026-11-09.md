# Kernel bytes since the #328 squash, round 2, by concept

**A measurement, not a description.** Taken 2026-11-09 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json --strict`, which RE-ASSEMBLES the kernel rather than
reading a build. Every commit of the round, and every parent it names, was
measured in a clean tree of its own: `git archive` of that commit's
`kernel/`, `boot/`, `drivers/`, `tools/`, Makefile and the top-level files of
`apps/`, that commit's `apps/stiklib.asm` assembled there into the
`stiklib.bin` that `deskmod.inc` includes, and that commit's own `kernsize`
measuring **`kern_big`, `kern_small`, `kern_emu` and `kern_mega`**. A tree
whose kernel inputs hash the same as one already measured takes that tree's
figures: **243 points, 127 distinct kernel trees, and every one of them
assembles on all four kernels**. `associco.inc`, `buildnum.inc` and - from
BOOT3-PLAN wave 4 preconditions on, where `boot/boot2.asm` first includes it -
`ksig.inc` are B's in every tree: the build number is held (`BUILD_STR` is the
commit count, SPEC.md 14.2), and `KSIG_OFF` is a canary's file sector, not a
byte of any section. Module and driver images come from `make`, `make emu`
and `make mega` at the two endpoints only. It is true of the commits it names
and of no other tree.

It is the eighteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-11-07.md`, and is an edit of none
of them. **It is the first of the family taken for a SECOND round into a PR
that is still open**: #345 (`elendilon-pr`) has not been squashed, so this
round joins it rather than starting a new one. The question is therefore not
the 2026-11-07 report's *"what does the round cost `main`"* - that report
still answers it for #345's first round - but **what does this second round
add to #345?** The two together are #345 against `main`; the last section
adds them up.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `c35be653` | **`elendilon-pr`**, #345 as it stands: `elendilon` at `09945b88`, then main's CLAUDE.md and AGENTS.md with the `KHP_*` knobs, three `make help` lines and the 2026-11-07 report. Its kernel measures identically to `09945b88`'s on all four kernels |
| **E** | `3cfb2457` | `origin/elendilon`: *Merge remote-tracking branch 'origin/elendilon' into vga-video-updates* |
| **B** | `f0ee42ea` | **`elendilon-pr` with E merged in**, the commit this round reaches #345 as. Only CLAUDE.md, AGENTS.md and the generated docs/INDEX.md conflict, none a kernel input, and B measures identically to E on all four kernels |

The round is **`09945b88..3cfb2457`: 239 commits, 48 of them merges**.
`main` did not move during it (`e9d78a10`, #344, is A's and B's merge base
with `main` alike), so no merge of `main` is in the range and **`B − A` is
the round's own work, exactly `E − 09945b88`**.

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M − M^1 −
Σ(every in-round commit M brings in)`. **The commits and the merges sum to
`E − 09945b88` exactly, in every section, on all four kernels** (checked by
the script, not assumed).

On `kern_big` and `kern_emu` **11** of the round's commits move a resident
byte and no merge does; on `kern_small` **3**; on `kern_mega`'s conventional
sections 19 commits and 2 merges, and 12 commits and 1 merge move `.mega`.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The round SAVES 140 resident bytes on `kern_big` and `kern_emu` and 360 on
`kern_small`. `KERN_SIZE` falls one 512-byte rung on `kern_big` and
`kern_small` and holds on `kern_emu`.** BOOT3 took 396 bytes off `kern_big`
and moved 5 KB of boot-only code out of `.boot2` and `.ovl` into `.boot3`;
READ_SEQ's two changes and PicoMEM 2's disk spent 234 of them.

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 40,340 | 39,962 | **−378** | 30,047 | 29,665 | **−382** | 40,586 | 40,208 | **−378** |
| `.bss` | 5,366 | 5,378 | +12 | 2,936 | 2,948 | +12 | 5,366 | 5,378 | +12 |
| `.cold` | 37,562 | 37,788 | **+226** | 21,027 | 21,037 | +10 | 37,562 | 37,788 | **+226** |
| `.vga` | 2,030 | 2,030 | 0 | - | - | | 2,030 | 2,030 | 0 |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 90,992 | **90,852** | **−140** | 56,878 | **56,518** | **−360** | 91,238 | **91,098** | **−140** |
| `.ovl` (window) | 5,119 | 3,160 | **−1,959** | 1,474 | 1,474 | 0 | 5,119 | 3,160 | **−1,959** |
| `.boot2` (blob) | 6,478 | 3,362 | **−3,116** | 4,745 | 2,936 | **−1,809** | 6,595 | 3,377 | **−3,218** |
| `.boot3` (new, 0 resident) | - | 6,148 + 892 nobits | **new** | - | 2,388 | **new** | - | 6,244 + 796 nobits | **new** |
| **`KERN_SIZE`** | 96,768 | **96,256** | **−512** | 58,880 | **58,368** | **−512** | 96,768 | **96,768** | 0 |
| spare of the budget | 4,096 | **4,608** | | 5,120 | **5,632** | | 4,096 | **4,096** | |

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---:|---:|---:|---:|
| **BOOT3, the third boot-only home** (`docs/plans/completed/BOOT3-PLAN.md`; SPEC.md 2.9.15) | 3 movers | **−396** | **−364** | **−396** |
| **READ_SEQ: a caller-held run spares the FAT, and seeks forward from a cursor** (`beff86f7`, `88f8f200`; SPEC.md 18.4.8.3) | 2 | **+179** | 0 | **+179** |
| **A PicoMEM 2's disk: several sectors a call, IRQ0 masked, no cache fill off the card** (`d501d9bc`, `c8703d36`, `04f20f04`, `df3d5b86`; SPEC.md 18.91.7.1, 18.95.12) | 4 | **+55** | 0 | **+55** |
| `mem_avail` plans the page bump (`dbbf4d85`, KERN-MEGA-PLAN 14.184) | 1 | +18 | 0 | +18 |
| *Five defects both kernels shipped* (`553727bb`) | 1 | +4 | +4 | +4 |
| the other 180 commits and 48 merges (kern_mega's release work and MEGA-HUGE, the big review, Video Battle, the video demo disks, the Interrupts page, docs and tests) | | 0 | 0 | 0 |
| **the round, against A** | | **−140** | **−360** | **−140** |

**BOOT3's −396** is `.text` −386, `.bss` +12 and `.cold` −22: the keyboard
mouse's pointer half adopted from memory instead of carried resident
(`8e270edb`, −346, `mouse.inc` −380 of `.text`), `.boot2` back to its
original purpose (`8695fa9e`, −22) and `font_init` back in the blob with
READ_SEQ's golf (`adf3a74a`, −28). The rest of BOOT3 moves nothing resident
and is the bigger story in bytes that go away: closed clusters, the
SYSTEM.CFG reader, the boot driver loader and the clock ladder go into
**`.boot3`**, the third boot-only home, in KERNEL.SYS and dropped at
`mem_unblob` - `.boot2` falls 3,116 bytes on `kern_big` (seven sectors on
every kernel) and `.ovl` 1,959. `kern_small` has no `.ovl` relief to take,
because its overlay is a build choice (SPEC.md 2.5.3.2); its blob falls 1,809.

**READ_SEQ's +179** is `.text` +6 and `.cold` +173: a run the caller already
holds spares the FAT walk (`beff86f7`, +145), and a cursor seeks forward from
where it stands, which Video Battle's and the Video Player's bookmarks use
(`88f8f200`, +34). `kern_small` takes neither.

**PicoMEM 2's +55** is on `kern_big` and `kern_emu` only: the disk read one sector a call
with IRQ0 masked (`d501d9bc`, +36), then four (`c8703d36`, +6), never filling
the sector cache off the card (`04f20f04`, +10), and no hold or cap on the
new firmware (`df3d5b86`, +3).

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 46,088 | 45,714 | **−374** |
| `.bss` | 5,404 | 5,416 | +12 |
| `.cold` | 46,100 | 48,562 | **+2,462** |
| `.lowbss` | 5,574 | 5,600 | +26 |
| `.vgabuf` | 336 | 336 | 0 |
| conventional sections (the five above) | 103,502 | **105,628** | **+2,126** |
| `.mega` | 23,056 | 24,656 | **+1,600** |
| `.mega`'s scratch | 1,510 | 1,510 | 0 |
| `.boot2` (blob) | 6,709 | 3,281 | **−3,428** |
| `.boot3` (new, 0 resident) | - | 5,941 + 715 nobits | **new** |
| **`KERN_SIZE` at boot** | 134,144 | **137,728** | **+3,584** |
| **resident once switched** (`MEGA_RESIDENT`, the budgeted figure) | 62,976 | **62,464** | **−512** |

| concept | commits | conventional | `.mega` | **resident once switched** |
|---|---:|---:|---:|---:|
| **MEGA-HUGE-PLAN: `OSAPI_MEM_HUGE` claims that bridge, regrow and read a packed file past 64KB whole** (`77e3cfbd`, `d233bc1b`, `a6da385a`, `69b3a441`, `4ebce064`, merge `42382f05`) | 5 + 1 merge | **+1,906** | +256 | 0 |
| **The real-mode bracket: drivers lent down, `FSXF_PREP`, parity with `kern_big`, a lent SOUND.DRV's vector** (`fee7fb62`, `d7c1d370`, `7bbd4508`, `40ac2771`, merge `ab33f588`; KERN-MEGA-PLAN 14.176-14.181) | 4 + 1 merge | +10 | **+832** | 0 |
| **The DMA doors: a page-safe pool block for an `OS88_HIGHMEM` package, and its rings from the floor** (`6f62137b`, `2abc5dbf`, `230f2a1e`; 14.160) | 3 | +289 | +304 | 0 |
| **A fixed-disk read into a claim, `BLKCAP_SEL`, and the read-ahead landing** (`dc3a0e28`, `49893c5e`, merge `42072b88`; 14.166-14.167) | 2 + 1 merge | +98 | 0 | 0 |
| a 22 kHz floppy stream no longer runs dry (`7aa60909`, 14.156) | 1 | +18 | +32 | 0 |
| the keyboard: Ctrl on grey keys, Pause, Ctrl+Break, Ctrl+Alt+grey Delete (`ffba6e29`, `5c05a524`) | 2 | 0 | +176 | 0 |
| **BOOT3** | 3 movers | **−396** | 0 | **−512** |
| READ_SEQ, the page-bump plan, the five defects (as above) | 4 | +201 | 0 | 0 |
| **the round, against A** | | **+2,126** | **+1,600** | **−512** |

What a running `kern_mega` keeps is **512 bytes less**, all of it BOOT3's.
Everything this round added to it is above 1MB once switched: MEGA-HUGE's
+1,906 is `.cold` +1,880 and `.lowbss` +26, and the bracket, the DMA doors
and the keyboard put 1,600 into `.mega`. At boot, before the switch,
`KERN_SIZE` is **+3,584**: `.cold` crossed four rungs and `.mega` four
(`.mega`'s rung had 10 bytes left at A), less BOOT3's one. That is the
640 KB boot question, not the budget's, and guard 5 still has 499,712 bytes.

What binds `kern_mega` at B:

- **the budget**: resident 62,464 of 68,096, **5,632 spare** (5,120 at A);
- **the 64 KB segment**: `.text`+`.bss` 51,130, **14,406 left** (14,046 at
  A); `kern_mega` carries **3,760 bytes beyond `kern_big`** there (3,754 at
  A). `tests/megaseg.txt` was raised to exactly 3,760 in the round for
  `FSXF_PREP`'s refusal in real mode (`d7c1d370`), so it again has no
  headroom;
- its rungs: image 70 left (222 at A), cold 78 (492), low 32 (58), vgabuf
  176 (176), `.mega` 458 (10).

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved: `.text` / `.cold`, and
any `.bss` or `.lowbss`. **`boot3.inc` is %included twice** (BOOT3's head
and its declarations), and `kernsize --modules` refused to print the table
for that until this commit (below); `kernel.asm` and `boot3.inc` are one
move between them and are shown as a pair.

| file | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|
| `kernel.asm` + `boot3.inc` (new) | **−311 / −159** | **−242 / −176** | **−321 / −159** |
| `mouse.inc` | **−380 / +6** | **−380 / +6** | **−380 / +6** |
| `fsx.inc` | +293 / +159 | +224 / +176 | +309 / +159, `.lowbss` +1 |
| `diskw.inc` | 0 / +130 | 0 | 0 / +890 |
| `disk.inc` | +7 / +68 | 0 | +7 / +131 |
| `memory.inc` | 0 / +18 | 0 | 0 / +349 |
| `mega.inc` | - | - | 0 / **+1,004**, `.lowbss` +25 |
| `lz.inc` | 0 | 0 | 0 / +78 |
| `hiber.inc` | +2 / 0, `.bss` +12 | +2 / 0, `.bss` +12 | +2 / 0, `.bss` +12 |
| `mod.inc` | +12 / 0 | +12 / 0 | +12 / 0 |
| `loader.inc`, `dock.inc`, `ui.inc`, `apps.inc` | 0/+4, −4/0, +2/0, +1/0 | 0/+4, 0, +2/0, +1/0 | 0/+4, −4/0, +2/0, +1/0 |

`fsx.inc` and `kernel.asm` + `boot3.inc` trade code with each other inside
BOOT3; the round's `.text` is the −378 above on `kern_big` either way.

## How close B stands to the next rung

| | image rung | top / cold rung |
|---|---:|---:|
| `kern_big` at A | 374 left | top 282 left |
| `kern_big` at B | 228 left | **top 56 left** |
| `kern_small` at A | 298 left | top 169 left |
| `kern_small` at B | 155 left | top 159 left |
| `kern_emu` at A | 128 left | top 282 left |
| `kern_emu` at B | 494 left | **top 56 left** |
| `kern_mega` at A | 222 left | cold 492, low 58, vgabuf 176, `.mega` 10 |
| `kern_mega` at B | **70 left** | cold **78**, low **32**, vgabuf 176, `.mega` 458 |

Per CLAUDE.md's banner, that decides WHEN a machine pays and never what a
change cost. `kern_big`'s and `kern_emu`'s top rung, the one `.cold` and
`.vgabuf` share, has 56 bytes before it costs every machine 512.

**Guard 5**, before a kernel cannot BOOT: `kern_big` **19,968** on 128 KB
(23,552 at A), `kern_small` 59,392 (62,464), `kern_emu` 19,456 (23,040), and
`kern_mega` 499,712 in 640 KB (509,952). Those fall although `KERN_SIZE`
does not grow, because **BOOT3 made the guard count the blob's lift**: the
`.boot3` image the blob reads in under it is boot-time memory, so `bootmax`
falls 4,096 bytes on `kern_big` (120,320 → 116,224). It is the guard getting
honest, not a cost.

## The on-demand modules and the drivers

Image unpacked plus bss, as the header records it, `make`, `make emu` and
`make mega` at the endpoints. **Every module and driver not listed is the
same size at A and B.**

| image | kernel | A | B | **B − A** |
|---|---|---:|---:|---:|
| `SOUND.DRV` | all (one driver) | 10,511 | 10,517 | +6 (`e554bc3a`: a closed stream forgets its ring's segment words) |
| `HIBER.DRV` | `kern_mega` | 13,187 | 13,278 | +91 (`a299d859`: Shut Down stops the scheduler, a refused resume restarts) |
| `CTRL.DRV` | `kern_mega` | 15,874 | 15,940 | +66 |

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at #345's first round (A) | 96,768 | 58,880 | 96,768 | 134,144 at boot, 62,976 resident |
| `KERN_SIZE` at B | **96,256** | **58,368** | **96,768** | **137,728** at boot, **62,464 resident** |
| resident bytes, this round | **−140** | **−360** | **−140** | **+2,126** conventional, `.mega` +1,600, **−512** once switched |
| ...of which BOOT3 | −396 | −364 | −396 | −396 conventional, −512 once switched |
| ...of which features | +256 | +4 | +256 | +2,522 conventional (MEGA-HUGE +1,906), `.mega` +1,600 |
| **#345 whole against `main`** (the 2026-11-07 report's B − A, plus this) | **−404** | **−729** | **−412** | **+2,299** conventional (+173 + 2,126), **−512 once switched** |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | 68,096 |
| spare | **4,608** (9 steps) | **5,632** (11 steps) | **4,096** (8 steps) | **5,632** (11 steps) |

**`make budget-check` passes at B on all four**, with no grace: 4,608,
5,632, 4,096 and 5,632 spare. B could go to `main` as it stands.

**In one line each:**

- **We** saved **140 bytes on `kern_big`** and **360 on `kern_small`**:
  BOOT3 took 396 off `kern_big` and paid for READ_SEQ (+179), PicoMEM 2's
  disk (+55) and two small fixes, and `kern_big` and `kern_small` are a
  rung smaller.
- **BOOT3** also took 3,116 bytes out of `kern_big`'s boot blob and 1,959 out
  of its overlay window, into a `.boot3` that is gone by the first desktop.
- **`kern_mega`** keeps **512 bytes less** once switched; MEGA-HUGE and the
  real-mode bracket put 3,500 bytes above 1MB, so it boots 3,584 bigger.
- **Modules and drivers** are unchanged but for `SOUND.DRV` (+6) and
  `kern_mega`'s `HIBER.DRV` (+91) and `CTRL.DRV` (+66).

## Found on the way

- **`kernsize --modules` stopped reporting at BOOT3 wave 1** (`7b4e6b1c`):
  `kernel.asm` %includes `boot3.inc` twice, the module table was keyed by
  file name, and the second include overwrote the first, so the tool said
  *"61 of 62 modules reported"* and refused - which also meant `--bless`
  could not regenerate docs/KERNEL-MEMORY.md's module and theme tables.
  **Fixed in the same commit as this file**: a file included more than once
  is one row with its marker pairs summed, and the completeness check counts
  includes rather than names; `boot3.inc` joins *the kernel proper*'s theme
  beside `kernel.asm`, whose boot ladder it is. **The soak row `kernmods`
  (`tests/unit/t_kernmods.py`) was red on `elendilon`'s tip for exactly
  this** - *"61 of 62 modules reported"*, then, once measured, *"boot3.inc
  not placed in THEMES"* - and is green (60 checks) at B. It is a soak row,
  so no `make` saw it.
- **docs/KERNEL-MEMORY.md's baselines described neither A nor B**: they were
  last blessed at the 2026-11-02 report's B, so `kernsize` at B read *"was
  93"* in `kern_big`'s image rung against A's real 374, and every BOOT3 field
  (`boot3`, `b3res`, `blift`, `b2res`) was missing. **All four re-blessed at
  B in the same commit as this file**, with the module and theme tables.
- **The ABI lock did not bump**: `PKG_FMT` stays 10 and `DRV_VER` 12; the
  round's lock entries (14.160's DMA doors, `BLKCAP_SEL`, MEGA-HUGE's `AX = 0`
  ask) are all additions recorded without a bump.
- **`make` builds B cleanly** (fast tier 76 passed, 0 failed, 1 skipped), and
  so do `make small`, `make emu` and `make mega` under `make budget-check`.
