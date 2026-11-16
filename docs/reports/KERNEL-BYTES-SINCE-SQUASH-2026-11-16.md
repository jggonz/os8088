# Kernel bytes since the #345 squash, by concept — the Wire Arena round

**A measurement, not a description.** Taken 2026-11-16 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json --strict`, which RE-ASSEMBLES the kernel rather than
reading a build. Every point named below was measured in a clean tree of its
own: `git archive` of that commit's `kernel/`, `boot/`, `drivers/`, `tools/`,
`apps/` and Makefile, that commit's `apps/stiklib.asm` assembled into the
`stiklib.bin` `deskmod.inc` includes, and that commit's own `kernsize`
measuring **`kern_big`, `kern_small`, `kern_emu` and `kern_mega`**.
`associco.inc`, `buildnum.inc` and `ksig.inc` are B's in every tree, each
kernel's own: the build number is held (`BUILD_STR` is the commit count,
SPEC.md 14.2), so a figure here never moves because a commit was counted. The
clean-tree figures for B were checked against `kernsize` on B's real `make`,
`make small` and `make mega` builds: identical. **57 points, and every one
assembles on all four kernels.** It is true of the commits it names and of no
other tree.

It is the twentieth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-11-13.md`, and is an edit of none
of them. **#365 is still open**: `main` has not moved past `7b802356`
(#364), so this round does not start a new PR. It adds to #365, and the
question is two questions: *what does this round add to what #365 already
asked of `main`'s kernel*, and *what does the PR now cost `main` in all*.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `7b802356` | **`main`**: #364, *Release 20261113: version file*. `main`'s commits since the #345 squash (`08f0c7ec`, #346 to #364: PageDesk, Browser 1.7, the ES1868/ES1869 DMA routes, the releases) measure **identically to `08f0c7ec` on all four kernels**: none of them moves a kernel byte |
| **P** | `9adfc9f8` | **the kernel #365 carries**: `elendilon` at *Merge fix-flaky*, which `elendilon-pr` (`0591ff00`, #365's head) took and then only merged `main` and wrote documents onto. `0591ff00` and `9adfc9f8` measure identically on all four kernels |
| **B** | `b5897f25` | **`elendilon`** after this round: Wire Arena (`multiplayer-fps`), the parts limit, driver size pass 1, the soak passes. The merge onto `elendilon-pr` adds no kernel source of its own (its `kernel/` and `boot/` are `elendilon`'s byte for byte) |

**The round is `9adfc9f8..b5897f25`: 590 commits, 100 of them merges, and 27
of them change a kernel input** (`kernel/`, `boot/`, `apps/stiklib.asm`, or
an SDK `.inc` the kernel assembles against). Each of those was measured
against its first parent. **Six of the 27 move a resident byte**; the other
21 are the SDK's and the drivers' (SERIAL.DRV's block verbs, ETHER.DRV's UDP
verbs, the Drivers page's figures, VESA.DRV's attach code) and measure 0 on
every kernel.

## Headline

**The round adds 72 resident bytes to `kern_big` and `kern_emu`, 6 to
`kern_small` and 86 to `kern_mega`'s conventional memory. `KERN_SIZE` moves
on no kernel, and `kern_mega`'s resident figure (`MEGA_RESIDENT`) does not
move either.** Against `main`, the whole PR still saves on every kernel.

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` (conventional) |
|---|---:|---:|---:|---:|
| **A** `main`, resident | 90,901 | 56,534 | 91,147 | 105,665 |
| **P** #365's kernel | 89,666 | 55,788 | 89,912 | 104,222 |
| **B** after this round | **89,738** | **55,794** | **89,984** | **104,308** |
| **B − P, the round** | **+72** | **+6** | **+72** | **+86** |
| **B − A, the PR against `main`** | **−1,163** | **−740** | **−1,163** | **−1,357** |
| `KERN_SIZE` at A / P / B | 96,256 / 95,232 / 95,232 | 58,368 / 57,344 / 57,344 | 96,768 / 95,232 / 95,232 | 137,728 / 136,704 / 136,704 |
| spare of the budget at B | 5,632 | 6,656 | 5,632 | 6,656 (`MEGA_RESIDENT` 61,440 of 68,096) |

`make budget-check` passes on all four kernels on the merged
`elendilon-pr` tree, with no branch grace: kern_big and kern_emu 95,232 of
100,864, kern_small 57,344 of 64,000, kern_mega 61,440 of 68,096.

## By concept

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** | **`kern_mega`** |
|---|---|---:|---:|---:|---:|
| **`OSAPI_MOUSE_REL`** (SPEC.md 9.14): the mickeys before the clamp, for Wire Arena's mouse-look - a cell appended at 0x0236 and its accumulator in the mouse ISR | `37071f2a` | **+38** (`.text` +34, `.bss` +4) | +38 | **+38** | **+40** (`.text` +36: kern_mega's KDS prefix) |
| ...gated off `kern_small`, whose cell refuses (the owner's) | `df8851ed` | 0 | **−28** | 0 | 0 |
| **`OSAPI_KEY_MAP`** (SPEC.md 9.7.3): the whole key-state map in one call - a cell appended at 0x0239, its body beside `kbd_down`, and kern_mega's `fsx_rkeep` row so a real-mode bracket keeps it | `37a9f136` | **+34** | +34 | **+34** | **+38** |
| ...refused on `kern_small` too, which was 36 bytes past its locked 70.5KB heap rung (guard 1b; the cell lands on `MOUSE_REL`'s refusal, `api_file_path` becomes `xm_copy`'s bytes) | `d7553e79` | 0 | **−38** | 0 | 0 |
| **One part may pass 64KB** (`OP_BIG`, SPEC.md 20.12.15; RealDMG's embedded ROM): kern_mega's part loader walks a big part as a run of selectors | `1004a733` | 0 | 0 | 0 | **+8** (`.cold`) |
| the other 21 kernel-input commits (the SDK and the drivers' constants), and the ~560 commits that touch no kernel input (Wire Arena itself, the drivers, the tests, the docs) | | 0 | 0 | 0 | 0 |
| **the round, B − P** | | **+72** | **+6** | **+72** | **+86** |

**The merges carry exactly their branches' sums and add nothing of their
own**: `e4ad0682` and `f4a29b01` the key map's +34/+38, `03d453fd` the part
loader's +8, `18686073` (*Merge multiplayer-fps into elendilon*) and
`6ff72cca` Wire Arena's +72/+44/+78, `2dba2d63` kern_small's −38. Along
`elendilon`'s first-parent line the movers sum to `B − P` in every section
on all four kernels (checked by the script, not assumed).

**Where it lands**: `kernel/mouse.inc` (the accumulator, the refusal and the
key-map body), `kernel/kernel.asm` (two table cells and guard 1b),
`kernel/fsx.inc` (one `fsx_rkeep` row), `kernel/lz.inc` (the big part's
selector walk, kern_mega's `.cold`). `kernel/serial.inc`'s +95 lines and
`kernel/driver.inc`'s figures are SERIAL.DRV's block verbs and the Drivers
page's numbers; neither moves a resident byte.

**`kern_small` is held to its heap rung**: guard 1b (`HEAP_SEG` ≤ `0x0E60`,
no grace, the owner's of 2026-11-15) leaves it **2 bytes** of slack on that
rung at B. Both Wire Arena cells refuse there, and Wire Arena refuses to
start there (it needs kern_big or kern_mega).

## The drivers: driver size pass 1

Not kernel bytes, but the same question for what a machine loads beside it
(`docs/plans/completed/dsp1/RECORD.md`): **drivers −4,796 bytes of image and
−2,984 of file**, with SAVER.DRV −1,314, CDROM.DRV −835, VESA.DRV −699,
PCIETHER.DRV −567 and ETHER.DRV −541 of image the largest, six drivers a
heap granule smaller, and the system disk four clusters freer. NET.DRV's
transport rewrite was reverted when it made `doscable` intermittent, which
gave back +90 of image and +77 of file. On the merged `elendilon-pr`,
SOUND.DRV measures 21 sectors with both the pass and `main`'s ES1868/ES1869
routes in it, so `DRVM_IMG_SND` is 21 there.
