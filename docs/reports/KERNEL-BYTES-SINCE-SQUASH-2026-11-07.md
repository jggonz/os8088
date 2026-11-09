# Kernel bytes since the #328 squash, by concept

**A measurement, not a description.** Taken 2026-11-07 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json --strict`, which RE-ASSEMBLES the kernel rather than
reading a build. Every commit of the round, and every parent it names, was
measured in a clean tree of its own: `git archive` of that commit's
`kernel/`, `boot/`, `drivers/`, `tools/`, Makefile and the top-level files of
`apps/`, that commit's `apps/stiklib.asm` assembled there into the
`stiklib.bin` that `deskmod.inc` includes, and that commit's own `kernsize`
measuring **`kern_big`, `kern_small`, `kern_emu` and `kern_mega`**. A tree whose
kernel inputs hash the same as one already measured takes that tree's figures:
**556 points, 351 distinct kernel trees, and every one of them assembles on
all four kernels** (no stretch of this round had to borrow a parent's
figures). `associco.inc` and `buildnum.inc` are B's in every tree, so the
build number is held for every tree: `BUILD_STR` is the commit count (SPEC.md
14.2), and a digit is not a cost anybody spent. Module and driver images come
from `make`, `make emu` and `make mega` at the two endpoints only. It is true
of the commits it names and of no other tree.

It is the seventeenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-11-02.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `e9d78a10` | **`main`** (`jggonz/os8088`): #344, *Release 20261106: version file* |
| **D** | `d323a9a6` | `main`'s #328 squash, *Elendilon -> Main (Live Windowed DOS, ...)*: where this round's line of `elendilon` starts |
| **B** | `f8c9937c` | **`elendilon-pr`**: `elendilon` at `09945b88`, then main's CLAUDE.md and AGENTS.md with the `KHP_*` knobs added, and three `make help` lines |

The round is **`e9d78a10..f8c9937c`: 553 commits, 74 of them merges**. Its
first commit is `f113f3f7`, a squash of `elendilon` past #328 onto `D`, and it
merges `main` twice (`e0fc386c`: #329 WIN16-NEXT, #333/#335 PCIETHER.DRV,
#341 WireChat and the rest; `20a9d63e`: #343 Pravetz and #344). So `B − A` is
the round's own work: `B − D` less what `main` itself added after #328,
`A − D`, which `main` already has.

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M − M^1 −
Σ(every in-round commit M brings in)`. **The commits and the merges sum to
`B − D` exactly, in every section, on all four kernels** (checked by the
script, not assumed). The two merges of `main` come out at **exactly `A − D`**
on every kernel - `kern_big` +72, `kern_small` 0, `kern_emu` +72, `kern_mega`
+495 conventional and +1,344 `.mega` - so merging `main` in cost nothing
beyond what `main` had already spent, and they drop out of `B − A` whole.

On `kern_big` **122** of the round's commits and 2 of its merges move a
resident byte; on `kern_small` 85 and none; on `kern_mega`'s conventional
sections 111 and 2.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The round SAVES 264 resident bytes on `kern_big`, 272 on `kern_emu` and 369
on `kern_small`. `KERN_SIZE` falls one 512-byte rung on `kern_small` and
`kern_emu` and holds on `kern_big`.** Kernel size pass 18 took 775 bytes off
`kern_big`; the hot-paths pass and the 1,024-entry Disk window spent 485 of
them.

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 40,605 | 40,340 | **−265** | 30,208 | 30,046 | **−162** | 40,859 | 40,586 | **−273** |
| `.bss` | 5,411 | 5,366 | −45 | 2,945 | 2,936 | −9 | 5,411 | 5,366 | −45 |
| `.cold` | 37,514 | 37,562 | +48 | 21,225 | 21,027 | **−198** | 37,514 | 37,562 | +48 |
| `.vga` | 2,032 | 2,030 | −2 | - | - | | 2,032 | 2,030 | −2 |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 91,256 | **90,992** | **−264** | 57,246 | **56,877** | **−369** | 91,510 | **91,238** | **−272** |
| `.ovl` (window) | 5,118 | 5,119 | +1 | 1,474 | 1,474 | 0 | | | |
| `.boot2` (blob) | 6,456 | 6,478 | +22 | 4,741 | 4,745 | +4 | | | |
| **`KERN_SIZE`** | 96,768 | **96,768** | 0 | 59,392 | **58,880** | **−512** | 97,280 | **96,768** | **−512** |
| spare of the budget | 4,096 | **4,096** | | 4,608 | **5,120** | | 3,584 | **4,096** | |

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---:|---:|---:|---:|
| **Kernel size pass 18** (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P19.md`; `58a44207`'s side) | 39 movers | **−775** | **−456** | **−773** |
| **Kernel hot-paths pass 1**, `kernel-hotpaths-lean` (`docs/plans/khp1/LOG.md`; `6d865a34`'s side) | 88 movers | **+237** | +75 | **+227** |
| **Disk windows of up to 1,024 entries** (`69cb417c`, `15803099`, `82fd9bd1`; SPEC.md 22.6.4) | 3 | **+242** | 0 | **+242** |
| the Shell sort's case fold (`e2c2f85f`, SPEC.md 19.4.1) | 1 | +6 | 0 | +6 |
| the previous round's tail, `f113f3f7` (the IRQ1-only ROM hand-off, SPEC.md 9.8.3, and the rest) | 1 | +26 | +12 | +26 |
| `main` merged in, twice (`e0fc386c`, `20a9d63e`) **less `main`'s own A − D** | 2 merges | 0 | 0 | 0 |
| the other ~420 commits and 72 merges (ARCADE, Video Battle, the C64 and Apple II cores, WIN16, tests, docs) | | 0 | 0 | 0 |
| **the round, against A** | | **−264** | **−369** | **−272** |

**Size pass 18's −775** is `.text` −293, `.cold` −472, `.bss` −10. Its
largest rows: `files.inc`'s dead strip pass and row painter (`e332ac7b`,
−105), kern_big's boot blob taking `vid_tab`'s constant rows (`6a239e0c`, −72
resident; the rows move into the blob, which is why `.boot2` is +22), the
Disk window listing's one-call arithmetic (`45663f33`, −66), `sys`'s dead
banks (`3b39e7ae`, −62), the `wm`/`ui`/`menu`/`instance` finder batch
(`0d3249f0`, −59), `assoc.inc`'s ASSOC.DAT parse (`da242b84`, −42) and the
two sweeps (`b2054873` −40, `79841610` −36, `8b9e03cd` −31). The one commit of
the pass that ADDS a byte is `3ff685c2` (+3), which put `fm_mdraw` back as a
routine after an inline in the same pass lost a register (found by `msel`).

**The hot-paths pass's +237** is almost all `.cold` (+238). The lean branch
kept the free items and ten picks of +108 (`docs/plans/khp1/LOG.md` L), then
disk-3 (+14) and a standalone disk-5 (+60, the 8-sector LBA 0 fill of a
listing mount); every other pick is a commit and its `Revert`, which net to
zero here and are kept as `KHP_*` knobs (`kernel/kernel.asm`'s KHP block).
`KHP_LZ` on `kern_big` and `kern_emu` (`0445ea49`) is **+190** of it - the
LZ4 and LZB decoders' inline paths, the owner's pick because the saving is
CPU and a launch is mostly decode on a hard disk - and size pass 18 took 26 of
those back (`bfa9bd8f`). The free items and the size agent's trims net
**−135**: `cur_move_mono`'s overlap decided once (−26), the unaligned run
(−20), the size agent's dead banks (−66 in four commits), and `mod_cfix`'s
exact opcode test (+4, a fix). The ten picks, net of each other: the
scheduler's `sch_top` (+47) and the idle halt (+12), the 1bpp save and restore
by words (+16), `font_width_x` (+6), `gfx_rect_setup` (+6), the scroll thumb
(+7), Up One Folder (+5), the window put back whole (+4), the VGA XOR fill
(+3) and `font_char`'s Bit Mask index (+2). `kern_small` pays +75 because it
takes neither `KHP_LZ` nor the VGA picks.

**The Disk window's +242** is `.cold` +276 and `.bss` −34: a listing claim
sized to its folder, 1 KB holding 32 entries and growing with one extra
directory read for a bigger folder, the multi-select bitmap moved into the
claim (the `.bss` it gives back), a 1 KB floor (+6) and a re-list in place
keeping the claim (+18). `kern_small` is byte-identical. Size pass 18 then
took 80 of it back on the Disk window's own code (`45663f33`, `60bc8f3c`),
which this table charges to the pass.

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 45,853 | 46,086 | **+233** |
| `.bss` | 5,449 | 5,404 | −45 |
| `.cold` | 46,113 | 46,100 | −13 |
| `.lowbss` | 5,576 | 5,574 | −2 |
| `.vgabuf` | 336 | 336 | 0 |
| conventional sections (the five above) | 103,327 | **103,500** | **+173** |
| `.mega` | 23,056 | 23,056 | 0 |
| `.mega`'s scratch | 1,510 | 1,510 | 0 |
| `.boot2` (blob) | 6,773 | 6,709 | −64 |
| **`KERN_SIZE` at boot** | 134,144 | **134,144** | 0 |
| **resident once switched** (`MEGA_RESIDENT`, the budgeted figure) | 62,976 | **62,976** | **0** |

| concept | conventional | `.mega` | **resident once switched** |
|---|---:|---:|---:|
| **Kernel size pass 18**'s `kern_mega` rows and the shared code | **−704** | −16 | **−512** |
| **Kernel hot-paths pass 1**: the ten `KHP_*` VGA and window-manager rewrites lean left out, on `kern_mega` (`6c631694`, +646) and the shared picks | **+629** | 0 | **+512** |
| **Disk windows of up to 1,024 entries** (kern_big and kern_mega, SPEC.md 22.6.4) | +242 | 0 | 0 |
| the Shell sort's case fold | +6 | 0 | 0 |
| **`SEG_BWIN` binds into a package's own conventional claim** (`a408aaf5`, KERN-MEGA-PLAN 14.139) | 0 | +16 | 0 |
| `main` merged in, less `main`'s own A − D | 0 | 0 | 0 |
| **the round, against A** | **+173** | **0** | **0** |

What a running `kern_mega` keeps is **unchanged**: the hot-paths pass spends
one 512-byte rung of it - `kern_mega` runs on a 286 or 386, almost always
with VGA or EGA, so it carries ten rewrites `kern_big` does not - and size
pass 18 gives one back. The `.text` +233 is those rewrites (`.text` +454 on
the pass, against size pass 18's −221).

What binds `kern_mega` at B:

- **the budget**: resident 62,976 of 68,096, **5,120 spare** (as at A);
- **the 64 KB segment**: `tools/os88megaseg.py` reads kern_mega's
  `.text`+`.bss`+`.vga` 51,490, **14,046 left** (14,234 at A), carrying
  **3,754 bytes beyond `kern_big`** there (3,254 at A). `tests/megaseg.txt`
  allows 3,754 (3,283 on `main`), so it has no headroom left: sweep3 moved
  `vid_tab`'s rows out of `kern_big`'s `.text` into its blob and `kern_mega`
  keeps them resident (`VID_TAB_RES`), which widens the gap with nothing on
  `kern_mega` growing;
- **above 1MB**: `.cold` 46,100 + `.mega` 23,056 + its scratch 1,510 = 71,158
  of the code area's 261,120-byte cap, **189,962 left** (the same at A).

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu`, 64,000 on `kern_small` and 68,096 on `kern_mega`. **`make
budget-check` passes at B on all four, with no grace** - 4,096, 5,120, 4,096
and 5,120 spare - so B could go to `main` as it stands.

## The on-demand modules and the loadable drivers

Image unpacked plus bss (the header's paragraph count at +31), as `make`,
`make emu` and `make mega` built them at A and at B. Every module and driver
not listed is byte-identical in its image.

| file | kernel | A | B | **B − A** |
|---|---|---:|---:|---:|
| `PCIETHER.DRV` | `kern_big` | 17,265 | 17,053 | **−212** (its first size pass; −4 of it the `[eth_ntx]` fix) |
| `ETHER.DRV` | `kern_big` | 17,397 | 17,379 | −18 |
| `EXTD.DRV` | `kern_big`, `kern_emu` | 1,872 | 1,955 | +83 |
| `CTRL.DRV` | `kern_big`, `kern_emu` | 15,704, 15,722 | 15,741, 15,759 | +37 |
| `CTRL.DRV` | `kern_mega` | 15,920 | 15,874 | −46 |
| `CTRL.DRV` | `kern_small` | 5,652 | 5,658 | +6 |
| `FILECP.DRV` | `kern_small` | 5,134 | 5,119 | −15 |
| `HIBER.DRV` | `kern_big`, `kern_emu`, `kern_mega` | 9,456, 9,456, 13,200 | 9,443, 9,443, 13,187 | −13 |

None crosses a claim's 512-byte rung but `EXTD.DRV`, whose 1,955 is still a
2,048 claim. `FSCHECK.DRV`, `STIKMENU.DRV`, `DISKOPS.DRV`, `DOCK.DRV`,
`FORMAT.DRV` and `WIN95.DRV` are unchanged in their images on every kernel.

## What this report does not cover

- **Time.** The hot-paths pass bought cycles for bytes; its own notes
  (`docs/plans/khp1/`) and size pass 18's base-against-tip timing table
  (`HANDOFF-KERNEL-SIZE-P19.md`, `518f0618`) are where those numbers are.
- **Packages.** ARCADE, Video Battle, the C64 and Apple II fast cores and the
  rest of the round's packages move no kernel byte and are not measured here.
- **Knob kernels.** No budget binds one (CLAUDE.md, *Memory is THREE rules*),
  and the six `KHP_*` knobs no kernel defines are off in every tree above.
