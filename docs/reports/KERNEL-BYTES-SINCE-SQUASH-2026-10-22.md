# Kernel bytes since the #277 squash, by concept

**A measurement, not a description.** Taken 2026-10-22 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file tables), which
RE-ASSEMBLES the kernel rather than reading a build. Each of the **99
non-merge commits** of the round, and every parent they name, was measured in
a clean tree of its own: `git archive` of that commit's `kernel/`, `boot/`,
`tools/`, `drivers/`, Makefile and the top-level files of `apps/`,
`apps/stiklib.asm` assembled there into the `stiklib.bin` that `deskmod.inc`
includes, and that commit's own `kernsize` measuring **`kern_big`,
`kern_small`, `kern_emu` and - where the tree has `kernel/mega.inc` -
`kern_mega`**. A tree whose kernel inputs hash the same as one already
measured takes that tree's figures. Module and driver images come from `make`,
`make small` and `make mega` at the endpoints only. It is true of the commits
it names and of no other tree.

It is the twelfth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-20.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?** It has had **no
size pass**, so every byte below is unpassed.

**It is `kern_mega`'s FIRST report.** There is no earlier `kern_mega` figure
to diff against, so its section diffs it against **`kern_big` at B** - the
kernel it is built from (`-DKERN_BIG -DKERN_MEGA -DKERN_KNOB`) - and then
gives its own arc since it arrived on this branch. Every later report can
diff it against this one.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `425b00be` | **`main` at the cut**: the last squash, *Elendilon -> Main (VGA Demo Videos, …)* (#277, `eea2d24a`), plus #272 (*benchlib*). **A is #277 to the byte** on every kernel, so #277 is not a column |
| **C** | `491e28bb` | **`main` at its tip**: A plus twenty commits - WireFleet (#278), System Check (#286), INSTALL (#287), WireFleet's battle (#288), Lemmings (#168, #290), WireChat (#291, #292, #294), the Mixer (#296) and the CD-ROM (#297, #298) |
| **B** | `5d036146` | **`elendilon`**: cut from A as `bc95ab20` (*Squash elendilon onto main after #277*), with kern_mega (`protected-mode`), RealDMG, the video-compression squash and **C merged in twice** (`636be8bd`, `51553b16`). Because C is wholly inside B, B's kernel IS the squash a PR would make |

**The round is `425b00be..5d036146`: 99 commits that are not merges** - 20 of
them `main`'s (A..C) and 79 ours - and 34 merges.

**The build number is held at 367 for every tree** (B's own is 370, the same three digits): `BUILD_STR` is
the commit count (SPEC.md 14.2), and a digit is not a cost anybody spent.
**It was 1,334 this morning** - see *Found on the way*.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu` and 64,000 on `kern_small`, with `KERN_GRACE` 2,048.
**`make budget-check` passes at B on all three shipped kernels**, each with
2,560 spare, so B could go to `main` as it stands. `kern_mega` is a
`KERN_KNOB` build: no `KERN_BUDGET` binds it (its own section says what does).

## Headline: `kern_big`, the shipped default

| section | A | C, `main` | B | **C − A** (`main`) | **B − C** (ours) |
|---|---:|---:|---:|---:|---:|
| `.text` | 42,816 | 43,288 | 43,374 | +472 | **+86** |
| `.bss` | 5,253 | 5,307 | 5,311 | +54 | **+4** |
| `.cold` | 37,473 | 37,715 | 37,780 | +242 | **+65** |
| `.lowbss` | 5,358 | 5,358 | 5,358 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 |
| **resident** | 91,236 | 92,004 | **92,159** | **+768** | **+155** |
| window | 5,118 | 5,118 | 5,118 | 0 | 0 |
| blob | 5,929 | 5,930 | 5,925 | +1 | −5 |
| **`KERN_SIZE`** | 97,280 | 97,792 | **98,304** | **+512** | **+512** |
| spare of the budget | 3,584 | 3,072 | 2,560 | | |

**`main` spent five times what we did.** Its +768 is the Mixer's menu-bar
speaker (+316), WireFleet's serial (+192), the CD-ROM's class and row (+154)
and INSTALL (+104), and WireFleet crossed the image rung. **Ours is +155**,
and it crossed the rung again: kern_mega's shared bits (+80) took `.text` past
the 45 bytes C had left in it.

**The boot blob is the near limit, and the merge hit it.** `kernsize`'s
`blob` line is the blob before the kernel is packed; the shipped blob, with
the decoder and its tables, was **6,145 of 6,144 bytes** once `main`'s
CD-ROM row joined the boot-time driver table - a build failure. Nine bytes
came back in `boot/boot2.asm` in the merge itself (`51553b16`: the head loop
banks its test with `pushf`/`popf`, and `read_run` divides by a byte), so B
is **6,136 of 6,144**. `kern_emu` is 6,225 of 6,656 and `kern_small` 4,592
of 5,632.

## Headline: `kern_small`, the 128KB floor machine

| section | A | C, `main` | B | **C − A** | **B − C** |
|---|---:|---:|---:|---:|---:|
| `.text` | 31,138 | 31,253 | 31,307 | +115 | **+54** |
| `.bss` | 2,979 | 2,991 | 2,993 | +12 | **+2** |
| `.cold` | 21,970 | 22,040 | 22,050 | +70 | **+10** |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 |
| **resident** | 58,955 | 59,152 | **59,218** | **+197** | **+66** |
| window | 1,486 | 1,486 | 1,486 | 0 | 0 |
| blob | 4,387 | 4,387 | 4,381 | 0 | −6 |
| **`KERN_SIZE`** | 60,928 | 61,440 | **61,440** | **+512** | 0 |
| spare of the budget | 3,072 | 2,560 | 2,560 | | |

**WireFleet is the floor machine's whole `main` bill** (+191 of +197; it
crossed the rung), and our +66 is kern_mega's shared bits (+48), the mouse
ISR's SS (+12) and re-home HIGH's six. **`kern_small`'s image rung has FOUR
bytes left at B** - the next `.text` or `.bss` byte anybody spends there is
billed 512.

## `kern_emu`

`kern_big` plus SPEC.md 9.11's absolute pointer. Resident **92,428** at B:
`main` +766, ours **+167** - ours is `kern_big`'s +155 plus 12, the mouse
ISR's SS swap costing 26 bytes there against 14. `KERN_SIZE` 98,304,
2,560 spare; the rung `main` crossed on `kern_big` it crossed with the Mixer
here, and ours crossed none.

## `kern_mega`, the 286+ protected-mode kernel - its first report

### Against `kern_big`, at B

| section | `kern_big` | `kern_mega` | **mega − big** |
|---|---:|---:|---:|
| `.text` | 43,374 | 47,366 | **+3,992** |
| `.bss` | 5,311 | 5,348 | **+37** |
| `.cold` | 37,780 | 42,518 | **+4,738** |
| `.lowbss` | 5,358 | 7,257 | **+1,899** |
| `.vgabuf` | 336 | 336 | 0 |
| `.mega` | - | 14,460 | **+14,460** |
| `.mega`'s scratch | - | 1,482 | **+1,482** |
| window | 5,118 | 5,117 | −1 |
| blob | 5,925 | 6,288 | +363 |
| **`KERN_SIZE` at boot** | 98,304 | **125,440** | **+27,136** |
| **resident once switched** | 98,304 | **66,048** | **−32,256** |

**The one line that matters is the last.** `kern_mega` boots 27 KB bigger
than `kern_big` and then, at the switch, moves `.cold` and `.mega` above 1MB
and hands their rungs (59,392 bytes) to the heap: **it leaves a 286 32 KB
more conventional memory than `kern_big` does.** What binds it instead of a
`KERN_BUDGET`:

- **the 64 KB segment**: `.text`+`.bss` 52,714 of 65,536, **12,822 left**
  against `kern_big`'s 16,851. `tests/megaseg.txt` holds what it carries
  beyond `kern_big` there to **4,029** (`tools/os88megaseg.py`) - +6 at this
  merge, see below;
- **above 1MB**: `.cold` 42,518 + `.mega`'s rung + its scratch = 58,950 of
  the 110,592 between `MEGA_COLD_LIN` and `MEGA_XPOOL`, **51,642 left**;
- **its image rung has 22 bytes left** (`kernsize`'s rung line).

**Where it is, by file** (`kernsize --modules`, mega − big at B,
`.text` / `.cold` / `.bss` / `.lowbss`). The table has no `.mega` column:
the 14,460 bytes there are `mega.inc`'s protected-mode bodies, which this
tool does not attribute per file yet.

| file | mega − big |
|---|---|
| `mega.inc` (and `megados.inc`, the DOS park) | +3,457 / +1,995 / +5 / +857, blob +534 |
| `dskwin.inc` | 0 / 0 / 0 / **+1,024** |
| `memory.inc` (the pool: claims above 1MB, regrow, moves) | +10 / +764 / 0 / 0 |
| `driver.inc` (drivers placed high, `OS88_DRV_HIGHOK`) | +26 / +649 / +25 / 0, blob −46 |
| `disk.inc` | +3 / +594 / +2 / 0 |
| `loader.inc` | 0 / +224 / 0 / 0 |
| `diskw.inc` | 0 / +205 / +3 / 0 |
| `mod.inc` (modules above 1MB) | 0 / +194 / 0 / 0 |
| `kernel.asm` (`k_dsel`, the macros' selector loads) | +121 / +15 / 0 / 0, blob −23 |
| `snd.inc` | +112 / 0 / +2 / 0 |
| `sched.inc` | +70 / 0 / 0 / 0 |
| `lz.inc` | 0 / +64 / 0 / 0 |
| `wm.inc` | +59 / 0 / 0 / 0 |
| `font.inc` | +35 / 0 / 0 / +18 |
| `mouse.inc` | +51 / 0 / 0 / 0 |
| `menu.inc` | +41 / 0 / 0 / 0 |
| `hiber.inc` | 0 / +31 / 0 / 0 |
| `ui.inc` | +21 / 0 / 0 / 0 |
| `vga12.inc` | +18 / 0 / 0 / 0 |
| `viddet.inc`, `files.inc`, `vidsel.inc` | +9, +3 (`.cold`), +2 |
| `xmem.inc` | −43 / 0 / 0 / 0, blob −102 |

### Its arc on this branch

kern_mega arrived squashed (`43006665`, *kern_mega: the 286+ protected-mode
kernel*). From there to B, conventional resident (`.text`+`.bss`+`.cold`+
`.lowbss`+`.vgabuf`), with `.mega` beside it:

| commit | what | resident | `.mega` | `KERN_SIZE` |
|---|---|---:|---:|---:|
| `43006665` | the squash | **101,983** (`.text` 48,740, `.cold` 41,170) | - | 120,832 |
| `a34480c0` | the park's buffers and the refusals out of the segment (W4, 14.45) | **−1,114** (`.text` −1,378, `.cold` +264) | **+14,354** (scratch +916) | |
| `6027f7df` | the pool's follow-ups: split regrow, 96-entry table, 32KB windows (14.49) | **+629** (`.text` −234, `.cold` +95, `.lowbss` +768) | +454 (scratch +570) | +2,048 |
| `76082e66` | claims of any size move and regrow in the pool; drivers' images high | +352 (`.cold`) | | +1,024 |
| `04e1dcad` | a fault in a package closes the package, not the machine | +251 (`.cold` +163, `.lowbss` +84) | | +1,024 |
| `fc042556` | the action modules above 1MB | +88 (`.cold`) | +92 | |
| `a099b81b` | the Video Player's ring above 1MB (14.41) | +84 (`.cold`) | | |
| `b636b1cb` | re-home HIGH | +51 | | +512 |
| `63db200d` | the 8042 across the DOS trip (14.62) | +22 | | |
| `689bfeae` | re-home HIGH part 2, PIXELSTEIN 3D | +11 | | +512 |
| `b344f4d7` | the mouse ISR's swap sets SS (9.10.1, every kernel) | +10 | | |
| `2a94c2d0`, `23024638` | DMA rings bottom-up; the tick re-delivers a held byte | +4, +2 | +164 | |
| `9e6e496b` | the CS-write trampoline | 0 | | +512 |
| `1ec91d39` | the debug toasts behind `MEGADIAG=1` (14.65) | **−322** | −920 | −1,024 |
| **the merges** | `main`'s arm arriving (C − A), and `ui_volume_service`'s `KDS`/`KES` (+6) | **+774** | +316 (scratch +20) | |
| **B** | | **102,825** | **14,460** (scratch 1,482) | **125,440** |

**`main`'s arm costs `kern_mega` +774 where it costs `kern_big` +768**: the
six are the merge's own fix, `KDS`/`KES` in the volume callback, which read
`[cs:k_dsel]` on kern_mega (five bytes each) and are `push cs`/`pop`
everywhere else.

## The attribution is exact

Every non-merge commit was measured against its first parent. On `kern_big`,
`kern_small` and `kern_emu` **the merges' remainder is 0 in every field**:
the 99 commits sum to `5d036146 − 425b00be` exactly, in every section, the
window and the blob (checked, not assumed). **79 of the 99 move no kernel
byte.** On `kern_mega` the remainder is the +774 row above - `main`'s
commits have no `kern_mega` to be measured on, so their cost to it can only
arrive through the merges.

## `main`'s arm, by commit (C − A)

| commit | `kern_big` resident | `kern_small` | `kern_emu` |
|---|---:|---:|---:|
| `cd38ec7d` **Mixer: menu-bar volume and channel controls** (#296) | **+316** (`.text`) | 0 | +316 |
| `2edd2c29` **WireFleet: native serial naval battles** (#278) | **+192** (`.text` +110, `.bss` +12, `.cold` +70), `KERN_SIZE` **+512** | **+191**, `KERN_SIZE` **+512** | +190 |
| `cc4db7cf` **CD-ROM: `CDROM.DRV`** (#297) | **+154** (`.text` +38, `.bss` +38, `.cold` +78), blob +1 | 0 | +154 |
| `8a509992` **INSTALL: a setup wizard** (#287) | **+104** (`.text` +6, `.bss` +4, `.cold` +94) | +6 | +104 |
| `6163caed` *vidsel: display 0's `vid_ctx`* | +2 | 0 | +2 |
| the other 15 | 0 | 0 | 0 |
| **`main`** | **+768** | **+197** | **+766** |

## Our arm, by concept (B − C)

| concept | commit | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---|---:|---:|---:|
| **kern_mega's shared bits**: `k_dsel` and the `KDS`/`KES`/`KDATAR` spellings, `OS88_DRV_HIGHOK`, the pool's shared hooks | `43006665` | **+80** (`.text` +68, `.bss` +2, `.cold` +10), `KERN_SIZE` **+512** | **+48** | +80 |
| **The elendilon squash after #277**: a mounted image's read-only bit is virtual | `bc95ab20` | **+55** (`.cold`) | 0 | +55 |
| **The mouse ISR's private-stack swap sets SS as well as SP** (SPEC.md 9.10.1) | `b344f4d7` | **+14** | **+12** | **+26** |
| **Re-home HIGH**: `OSAPI_MEM` for a re-homed program's keep | `b636b1cb` | **+6** | **+6** | +6 |
| the other 75 | | 0 | 0 | 0 |
| **ours** | 4 of 79 | **+155** | **+66** | **+167** |

## Where the bytes are, by file

`kernsize --modules` (`.text` / `.cold` / `.bss`), every row that moved:

| file | `main`, A → C, `kern_big` | ours, C → B, `kern_big` | `main`, `kern_small` | ours, `kern_small` |
|---|---|---|---|---|
| `driver.inc` | +38 / +94 / +38, blob +1 | blob +1 | - | - |
| `menu.inc` (the speaker) | +153 / 0 / 0 | - | - | - |
| `ui.inc` (the popup's bridge) | +150 / 0 / 0 | - | - | - |
| `serial.inc` (WireFleet) | +56 / +70 / 0 | - | +56 / +70 / 0 | - |
| `desk.inc` (a volume's own picture) | 0 / +78 / 0 | - | - | - |
| `mouse.inc` | +26 / 0 / 0 | +12 / 0 / +2 | +26 / 0 / 0 | +10 / 0 / +2 |
| `hiber.inc` | +2 / 0 / +16 | - | +2 / 0 / +12 | - |
| `mod.inc` | +15 / 0 / 0 | - | +15 / 0 / 0 | - |
| `fprog.inc` | +13 / 0 / 0 | - | - | - |
| `kernel.asm` | +12 / 0 / 0 | **+48** / 0 / 0, blob −6 | +12 / 0 / 0 | **+42** / 0 / 0, blob −6 |
| `diskw.inc` | - | 0 / **+45** / 0 | - | - |
| `xmem.inc` | - | +24 / 0 / +2 | - | - |
| `disk.inc` | +1 / 0 / 0 | 0 / +10 / 0 | - | - |
| `memory.inc` | - | 0 / +10 / 0 | - | 0 / +10 / 0 |
| `instance.inc` | +4 / 0 / 0 | +2 / 0 / 0 | +4 / 0 / 0 | +2 / 0 / 0 |
| `vidsel.inc` | +2 / 0 / 0 | - | - | - |

## How close B stands to the next rung

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A | 59 left | 415 left | 274 left |
| `kern_big` at C | 45 left | 173 left | 274 left |
| `kern_big` at B | 467 left | **108 left** | 274 left |
| `kern_small` at A | 187 left | 46 left | 204 left |
| `kern_small` at C | 60 left | 488 left | 204 left |
| `kern_small` at B | **4 left** | 478 left | 204 left |
| `kern_mega` at B | **22 left** | 490 left | 423 left (`.mega` 442 left) |

**`kern_small`'s image rung is 508/512 spent.** Per CLAUDE.md's banner this
decides WHEN the machine pays and never what a change cost.

## The on-demand modules

Images unpacked plus bss. The claim is the image rounded up to whole KB.

| module | A | C | B | **C − A** | **B − C** | claim C → B, KB |
|---|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 14,210 | 14,333 | 14,301 | +123 | **−32** | 14 → 14 |
| `DESK.DRV` | 4,988 | 4,988 | 5,000 | 0 | **+12** | 5 → 5 |
| `DISKOPS.DRV` | 12,216 | 12,216 | 12,181 | 0 | **−35** | 12 → 12 |
| `HIBER.DRV` | 6,715 | 6,768 | **8,401** | +53 | **+1,633** | 7 → **9** |
| **`kern_small`** `CTRL.DRV` | 4,979 | 4,979 | 4,946 | 0 | **−33** | 5 → 5 |

- **`HIBER.DRV` +1,633 is the round's largest single growth, and it is
  kern_mega's work in a file `kern_big` shares**: the DOS trip and the
  hibernate taught the 8042 and the PS/2 mouse (KERN-MEGA-PLAN 14.57, 14.62,
  `a6a3ab5a`), the trip's CS writes as `CSDO` (14.46). It is +2 KB of heap
  while a hibernate or a DOS trip is in flight, on every `kern_big` machine.
  Nothing else resident.
- **The −32/−33/−35 rows are `apps/os88ui.inc`'s shared control**
  (`43006665`, `72fba2b3`), which every carrier includes.

**`kern_mega`'s modules against `kern_big`'s, at B**: `HIBER.DRV` **+3,072**
(11,473), `STIKMENU.DRV` **+2,865** (17,739), `DISKOPS.DRV` **+1,062**,
`FORMAT.DRV` +372, `CTRL.DRV` +356, `DESK.DRV` +256, `SERIAL.DRV` +91,
`WIN95.DRV` +86, `DOCK.DRV` +78, `EXTD.DRV` +25.

## The loadable drivers

Image + bss, `make` at the endpoints. Every driver not listed is the same.

| driver | A | C | B | **C − A** | **B − C** |
|---|---:|---:|---:|---:|---:|
| `SOUND.DRV` | 7,283 | 11,602 | 11,649 | **+4,319** | +47 |
| `CDROM.DRV` | - | 9,059 | 9,033 | **new** | **−26** |
| `SERIAL.DRV` | - | 781 | 781 | **new** | 0 |
| `HDDTOOL.DRV` | 16,499 | 17,810 | 18,322 | +1,311 | **+512** |
| `XMEM.DRV` | 2,041 | 2,041 | 2,098 | 0 | **+57** |
| `ETHER.DRV` | 17,011 | 17,011 | 16,978 | 0 | −33 |
| `RAMPAGE.DRV` | 2,469 | 2,469 | 2,436 | 0 | −33 |
| `THMPAL.DRV` | 2,617 | 2,617 | 2,584 | 0 | −33 |
| `RAMDISK.DRV` | 5,272 | 5,272 | 5,275 | 0 | +3 |

**`SOUND.DRV`'s claim went 8 KB → 12 KB on `main`** - the Mixer's popup,
1,056 bytes of it a slider band held for the driver's whole life
(docs/plans/MIXER-PLAN.md's open follow-ups).

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at `main` (C) | 97,792 | 61,440 | 98,304 | - |
| `KERN_SIZE` at B | **98,304** | **61,440** | **98,304** | 125,440 at boot, **66,048 resident** |
| resident bytes, ours against `main` | **+155** | **+66** | **+167** | **new**: its sections +10,666 and `.mega` 14,460 against `kern_big` at B |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | none (a knob) |
| spare | **2,560** (5 steps) | **2,560** (5 steps) | **2,560** (5 steps) | segment 12,822, above 1MB 51,642 |
| guard 5, before it cannot boot on 128 KB | 22,528 | 59,904 | 22,016 | - |
| on-demand modules, ours | `HIBER.DRV` **+1,633** | `CTRL.DRV` −33 | as `kern_big` | |
| loadable drivers, ours | `HDDTOOL.DRV` +512, `XMEM.DRV` +57 | | | |

**In one line each:**

- **`main`** spent **+768 resident on `kern_big`** and **+197 on
  `kern_small`** since the #277 squash, most of it the Mixer's speaker and
  WireFleet's serial, and **4 KB of every sound machine's heap** on the
  Mixer's popup. It crossed a rung on both kernels.
- **We** spent **+155 on `kern_big`** and **+66 on `kern_small`**, of which
  +80 and +48 are what kern_mega needs every kernel to carry, and crossed the
  `kern_big` image rung `main` had left 45 bytes in. Nothing has had a size
  pass. **`kern_mega` itself costs the shipped kernels nothing else** - its
  bodies are in `.mega`, behind `%ifdef KERN_MEGA`, or in `HIBER.DRV`.

## Found on the way

- **`elendilon` carried ~760 commits `main` already had.** `daabc39a`, *Merge
  gameboy into elendilon* (2026-10-21), recorded RealDMG as a two-parent
  merge, and `gameboy` had been cut from the PREVIOUS cycle's elendilon - so
  the merge brought that whole cycle's history (16-20 October, squashed home
  as #268 and #277) into this branch's ancestry, though its tree took only
  RealDMG's work. **Rewritten on 2026-10-22** with the owner's go: the merge
  re-recorded with one parent and the twelve commits after it re-created on
  identical trees; `origin/elendilon` and `origin/protected-mode` force-pushed.
  Every tree is the same to the byte. **The build number fell 1,334 → 367**,
  which is the right count - the 1,334 was the stowaways.
- **The merge broke two limits and one gate, all fixed in it**: the boot blob
  1 byte over (above), `make mega`'s segment gate at 4,029 against 4,023
  (`ui_volume_service`'s `KDS`/`KES`, `0e5a45af`), and two kern_mega lints on
  `main`'s code (`t_cswrite`, `t_seg0`).
- **`main`'s CD-ROM and Mixer did not run on kern_mega at all, and do now**
  (`67b238f1`, `5d036146`, KERN-MEGA-PLAN 14.68): `CDROM.DRV` folded DI's
  paragraphs into ES after every DRQ block - a segment off the 8-paragraph
  grid, which faulted the machine on its first protected-mode read - and read
  the BIOS tick through segment 0; the menu-bar speaker refused kern_mega's
  data alias in ES. Fixed for **0 kernel bytes on every build** and
  `CDROM.DRV` −26 (the tick is `OSAPI_GET_TICKS` now). CDRack and the Mixer
  are verified running above 1MB on Bochs, an 86Box 286 and an 86Box 386, and
  declare `OS88_HIGH | OS88_HIGHMEM`, which turns `t_megahigh` green; the
  `megacdrom` soak row is the gate. `RAMDISK.DRV`'s `rd_fold` and
  `NET.DRV`'s `net_norm` fold paragraphs the same way and are unrun there.
- **`docs/KERNEL-MEMORY.md`'s baselines did not describe B** - `kernsize`'s
  "was" figures were C's on `kern_big`. **Re-blessed at B in the same commit
  as this file**, `kern_big`, `kern_small` and `kern_emu`.
