# Kernel hot-paths pass 1: the log

The consolidated record of every change this pass made to a kernel hot
path: what it bought, what it cost in bytes, and how hot the path is. The
brief is `docs/plans/khp1/BRIEF.md` (its section 1 defines hotness H1-H4, and
section 7 defines the columns). Each agent's note in `docs/plans/khp1/` is
the authority on its own rows. This file collects them so the owner can read
the byte-costing items side by side.

**Status: COMPLETE.** All nine speed agents and the size agent are merged into `kernel-hotpaths`.

## L. This branch: `kernel-hotpaths-lean`

**The owner's words:** *"Split out the free ones, and pick around 100b of the
most impactful, and put those on a new branch."*

`kernel-hotpaths-lean` is `kernel-hotpaths` with 24 of its byte-costing picks
reverted (`git revert`, newest first) together with the three size-agent
trims that edit code those picks added (size-2 needs wm-4, size-12 needs
disk-1, size-17 needs disk-4). It keeps **every free item** (the size agent's
other fourteen included), the tools, the baseline report and the notes, and
**the ten picks below, +108 bytes** chosen from section 0's tier A by
user-visible time per byte, **and, added after, disk-3 and a standalone
disk-5 for +74** (section L.1):

| id | commit | kern_big B | what it buys |
|---|---|---:|---|
| sched-1 | `488ea2d5` | +47 | `OSAPI_TASK_YIELD` 644 -> 283 us; the idle tick -34%; pointer and typing CPU |
| mono-2 | `58ea45aa` | +16 | 1bpp save/restore by words: CGA/Hercules raise -11 to -15 ms |
| sched-4 | `d0ad21da` | +12 | the idle task halts again: pointer CPU -8%, idle CPU -11.6% |
| files-2 | `e02c1b7e` | +7 | a scroll moves the thumb only: CGA PgDn -18 ms |
| text-8 | `1139897c` | +6 | `font_width_x` -69% (menus, every layout) |
| gfxcall-1 | `10323197` | +6 | `gfx_rect_setup` 751 -> 639 cycles: -1.27 ms a CGA raise |
| files-1 | `048c7097` | +5 | Up One Folder ~-250 ms |
| wm-1 | `9c64eaea` | +4 | a covered window put back whole is not re-saved: fullscreen exit CGA -106 ms |
| vga-4 | `8b1de03b` | +3 | VGA XOR fill -36%: menu highlights, every drag-outline step |
| text-9 | `499dfc5c` | +2 | VGA `font_char` -6% a glyph |
| disk-3 | `6c8f8f05` | +14 | added later on the owner's ask: a listing mount drops the cache BEFORE the FAT window, so the window's own fill serves the root |
| disk-5 standalone | `c5d1d8fe` | +60 | added later on the owner's ask: a listing mount reads LBA 0 as ONE 8-sector fill (boot sector, FATs, root head). 360KB and 720KB: open a root 4 -> 2 int 13h calls, enter a folder 3 -> 2, Up 3 -> 1 (~0.4 s a call, P) |

**Left out, and why.** disk-4 (+208, one call fewer on a multi-track
launch) is narrow: only the loader's header peek arms it (section L.1). wm-4 (+34, fullscreen exit) and the decoders
(disk-1 +126, disk-2 +72) are next after it. Everything in tiers B and C is
out.

**One byte of glue.** vga-4's +3 in `vga_solid_rect_v` had been balanced on
`kernel-hotpaths` by vga-1/2/3; without them `gfx_fill_gray_raw_v` and the
planar save and restore sat at an odd shift from the base. A never-executed
`nop` in `.vga` puts all three back at the base's parity, and `hotpar` pins
`vga_save_vram_v` at the base's again (its body is the base's).

**What it costs.** Nothing, net: the free items and the size agent's trims
give back more than the ten picks spend.

| kernel | resident at base (`a7ee7fab`) | resident here | delta | `KERN_SIZE` |
|---|---:|---:|---:|---|
| **kern_big** | 91,530 | **91,503** | **-27** | 96,768, unchanged (4,096 spare) |
| kern_small | 57,258 | 57,259 | +1 | 59,392, unchanged (4,608 spare) |
| kern_emu | 91,784 | 91,747 | -37 | 97,280, unchanged |
| kern_mega | conventional 62,976 | conventional 62,976 | 0 | of 68,096, unchanged |

against `kernel-hotpaths`' +1,757 / +1,150 / +1,747 / +1,024.

**What it buys** (`tools/os88hot.py ab --base a7ee7fab`, MartyPC, the
emulated 4.77 MHz XT):

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| raise a window (ms) | 179.9 -> **164.1 (-8.8%)** | 234.3 -> **209.1 (-10.7%)** | 367.8 -> 360.5 (-2.0%) |
| PgDn / PgUp (ms) | 131.2 -> **106.2 (-19.1%)** / -19.4% | -5.2% / -4.6% | -4.5% / -3.8% |
| menu open (ms / CPU busy) | -3.4% / busy -14.2% | -7.3% / busy -14.1% | -1.5% / busy -10.3% |
| menu close (ms, n=6) | +2.4% (one tick's phase) | +0.6% | -3.9% |
| drag (ms, n=8, mode for mode) | -3.0% (n=6) | -3.5% | -0.9% |
| open A:'s Disk window (ms) | -5.0% (4 int 13h calls, unchanged) | -1.3% | -6.3% |
| launch Calc, cached (ms) | 970.7 -> 942.6 (-2.9%) | -3.1% | -1.8% |
| pointer sweep (CPU busy) | 237.4 -> **139.5 ms (-41.2%)** | -41.2% | -25.8% |
| typing 31 keys (CPU busy) | -19.1% | -21.4% | -16.4% |
| **idle desktop (CPU busy)** | 326.0 -> **179.7 ms (-44.9%)** | -44.9% | -44.9% |

Against `kernel-hotpaths` with every pick, lean gives up most of the VGA
raise (-24% there, -2% here: vga-1/2/3 and wm-7 stay out), the disk work
(opening A: -27% there, -5% here; launches -17 to -19% there, -3% here) and
the Hercules PgDn (-22% there): that is what the 1,665 bytes it leaves out
buy. It keeps the scheduler's whole gain (the pointer, typing and idle rows
are within a point of `kernel-hotpaths`') and most of the CGA raise and PgDn.

**Three cells needed re-measuring, and the reasons are worth keeping.**
Drag is quantised to whole timer ticks: the same base commit read VGA drag at
610 ms in one run and 666 in another, so -n 6 read lean as +8.2% against the
low mode; at -n 8 both sides took the high one and the delta is -0.9%. A
cold launch is ONE sample and its disk sits wherever the platter is: the
base read 1,299, 1,354 and 1,409 ms across three runs, lean 1,381-1,401, with
the same int 13h calls - unchanged within the rotational phase. Menu close
on CGA (+2.4%) is the same tick phase the main pass found at -n 3.

**Rows (before L.1).** The union of every row the nine notes name (150, the same `-k`
set as `kernel-hotpaths`' section 4.3), once on this tree: **150 passed, 0
failed, 0 skipped** - `glyphcost` included, files-3 being out (LOG O5).
**`make test-full`: 85 passed, 0 failed, 3 skipped** (the same three fixture
skips). `make`, `make small`, `make emu` and `make mega` build; `hotpar` is
green on kern_big and kern_small.

### L.1 disk-3 and disk-5 without disk-4 (+74)

On `kernel-hotpaths` disk-5 sat on disk-4, and it took exactly one thing from
it: the fill hint, armed to 8 so that the LBA 0 fill, which runs under the
fallback 9/2, cannot ask an 8-sector track 0 for a ninth sector. Eight never
exceeds one 9-sector slot, so disk-4's multi-slot fill, its cylinder bound and
its per-slot stamping were never on that path. Here the hint is a plain CAP
(`dsk_rah_fill` takes the smaller of the track's remainder and the cap):
**+60 against disk-4 + disk-5's +248**, with disk-3 re-applied as it was
(+14).

One fix over the original: the cap is disarmed straight after the LBA 0 read,
BEFORE its failure test. The original cleared it only on the success path, so
a refused LBA 0 left the next unrelated fill capped at 8: harmless to the
data, but state left armed. Two bytes of the +60 are parity (a never-executed
`nop` after the mount arm's `jmp`, and `mov` over `xchg` in the cap) so that
`dsk_next_clus_x` keeps its measured parity on both kernels.

| | 360 KB, before -> after | 720 KB, before -> after |
|---|---|---|
| open B: (its root) | 4 -> **2** calls (19 -> 14 sectors) | 4 -> **2** (19 -> 16) |
| enter APPS | 3 -> **2** (14 -> 13) | 3 -> **2** (18 -> 17) |
| Up (`..`) to the root | 3 -> **1** (13 -> 8) | 3 -> **1** (11 -> 8) |

MartyPC FDC counters, `os8088_5150_cga_gla` and the one 80-cylinder profile
`os8088_5150_herc_sb_720_gla`, `0890820b` against `c5d1d8fe`: the same counts
disk-4 + disk-5 gave on `kernel-hotpaths`. At ~400 ms a call on the 5150 that
is **~0.8 s off opening a root, ~0.4 s off a folder, ~0.8 s off Up**
(PREDICTED from calls). 1.44 MB is unchanged; 1.2 MB is not measured.

**Swap safety, tested and broken on purpose.** A scratch driver opens B: on
apps360, enters APPS, swaps B: for games360 under the open window (MartyPC
`mount`, then three guest seconds for the motor to stop, as an XT gives no
change signal), goes up with `..` and compares the listing with `mdir` of
games360's root, then swaps back through a games360 folder and compares with
apps360's. **Both comparisons pass on this branch and on its parent.** With
the mount's cache flush removed on purpose, the navigation after the swap
lists apps360's old root (`APPS`, `GAMES`) where games360's should be: the
flush is what keeps the re-tag from handing the old disk's chunks the new
disk's key, and the test can tell.

Bytes, with L.1: kern_big resident **91,577** (+47 against the pass base),
`KERN_SIZE` 96,768 -> **97,280** (one step; 3,584 spare); kern_small
**57,333** (+75), `KERN_SIZE` 59,392 unchanged; kern_mega and kern_dos
unchanged. SPEC.md 18.95.11 is rewritten for this branch: the listing mount's
fill, the swap reasoning, and why the loader's peek is not here.

Rows, once on `c5d1d8fe`: **the disk agent's 39 and the files agent's 13
(50 selected): 50 passed**, `imgswap`, `stikswap`, `rdmount`, `fatwpin`,
`dirwshed`, `curdisk`, `install` and the boot rows among them.

### L.2 kern_mega carries the rest that pays on its machines (`KHP_*`)

The owner's words: *"Kern mega can afford more ram, so the next step is
'unleaning' mega - can you select optimizations from the larger set -
anything 'worth its bytes' - and add them to mega?"*

kern_mega runs on a 286 or 386, almost always with VGA or EGA, in protected
mode; its `.cold` lives above 1MB and its conventional budget had room. So
it takes eleven groups of the picks lean leaves out, each behind a `KHP_*`
knob in `kernel/kernel.asm`'s KHP block, defined for `KERN_MEGA` alone:

| knob | picks | bytes on kern_big if turned on | what it buys |
|---|---|---:|---|
| `KHP_VGASAVE` | vga-1 (`7e469c23`, `7390cd6d`), pointer-2 (`49f6681c`) | +122 `.vga` | the planar save/restore by words and the VGA cursor's walks: 70% of a VGA raise |
| `KHP_VGAFILL` | vga-2 (`71a90157`) | +39 `.vga` | VGA fill interiors by words |
| `KHP_VGAEDGE` | vga-3 (`9e5511d1`) | +21 `.vga` | VGA fill edges two rows a pass |
| `KHP_VGABLITP` | vga-5 (`da0b1070`) | +32 `.vga` | `gfx_blitp` by words |
| `KHP_FRAME` | gfxcall-3 (`4ea77a8b`) | +26 | `gfx_frame`'s clip questions once a frame |
| `KHP_FPG` | gfxcall-4 (`2f15d7df`) | +18 | the unarmed progress unlock answered first |
| `KHP_RELIT` | wm-4 (`233ccd04`), size-2 (`68f50f2a`) | +32 | fullscreen exit by the damage pass: the way back from FSX |
| `KHP_CAPTION` | wm-2 (`db2e9670`), wm-3 (`cd852c89`) | +34 | the title bar's caption |
| `KHP_DMGSTALE` | wm-6 (`ce2df232`) | +38 | drags over hidden windows |
| `KHP_RAISEOL` | wm-7 (`4dd8b6d6`) | +90 | a partial raise's outline |
| `KHP_LZ` | disk-1 (`f47761a8`), disk-2 (`d122c19d`), size-12 (`e59c61ea`) | +190 `.cold` | the lz4 and LZB decoders ~1.6-1.7x |

Picks that overlap in source share a knob (wm-2/wm-3 both rewrite
`wm_draw_title`; disk-2 builds on disk-1's budget register; vga-1's second
commit drops a loop pointer-2 retired).

**Not on kern_mega, and why.** mono-1/3/4/5 and files-3: 1bpp-only gains,
least worth their bytes on a 286. text-6: its gain is 1bpp's, and on VGA it
costs +0.3%. disk-4 and disk-5: the read-ahead's hint does not run through
kern_mega's nine-sector bounce (`DSK_RAHINT` is undefined there), and its
1.2/1.44MB drives gain least. pointer-6: pays only with O4.

**How it was made, and how it is proved.** Each pick's patch was applied to a
copy of the current file and the difference written back as
`%ifdef KHP_x / new / %else / old / %endif` (a scratch tool, run per group in
the picks' original order). Two equalities at one commit (`5d419969`, so one
build number): with every knob off, kern_big's and kern_small's images are
**byte-identical** to `kernel-hotpaths-lean`'s; with every knob on, kern_mega's
is **byte-identical** to the same tree with the seventeen commits re-applied
as plain code. Each knob also assembles alone on kern_big. One hand edit: the
`VX_SAVE` hook in `vga_save_vram` is one line outside the arms (the
instruction was identical; `t_vidext` counts hooks).

**What it costs kern_mega.** Conventional resident 62,976 -> **63,488** of
68,096 (4,608 spare). Its `.cold` half is above 1MB. In the kernel's 64KB
segment kern_mega carries **399** bytes more than kern_big
(`tests/megaseg.txt` 3,283 -> 3,682): kern_big growth room spent, 13,815
left. That is the real price, because these are rewrites of `.text` routines
kern_big also has and cannot move into `.mega`.

**What it buys on kern_mega** is PREDICTED, not measured there: the same code
measured on kern_big (sections 0 and 4.2 above, and 86Box's 286 for the VGA
raise, -12.5% from the VGA items, RELATIVE). No harness here times kern_mega.

**Tools.** `tools/stkbalance.py` now walks the corpus once with every `KHP_*`
knob off and once with every one on (a knob's `push` in one arm and its
`pop` in a later one are not a defect, but read as one sequence they are).
Only `KHP_*` conditionals are evaluated.

**Rows.** The 64 kern_mega rows (Bochs, 86Box), less five WIN16 rows whose
fixtures this box has no rule to build: **56 passed, 2 failed, 1 skipped**
(`megaaltrdg`, rgbds). The two failures, `megavidring` (the live playback
leg) and `brmega` (a page's layout), miss a 240-guest-second deadline under
Bochs - **and fail identically on lean without the knobs (`5d419969`) and at
the pass's base (`a7ee7fab`)**, run alone each time: they predate the pass,
and are not diagnosed here. `make test-full` and the fast tier pass.
LAST-DROP-PERF.md section 7 is the ranked list of what kern_big could still
turn on.

### L.3 The 1bpp items and the rest: knobs that no kernel turns on

The owner's words: *"Do we have the info we need to remake the CGA/herc only
changes? The temporary branch will not live long enough to cherry pick off
of when we get back to this. We could put them into a file alongside the
perf doc, or we could make them a knob for them, with them off for now."*

They are knobs, defined by **no** kernel, so they outlive `kernel-hotpaths`:

| knob | picks (commit on `kernel-hotpaths`) | bytes on kern_big if on |
|---|---|---:|
| `KHP_MONORECT` | mono-1 (`13aeb31a`) + mono-5 (`bab1b495`), kern_big only | +360 |
| `KHP_MONOSCROLL` | mono-3 (`1eabf883`) | +56 |
| `KHP_MONOPAT` | mono-4 (`87574fa3`) | +22 |
| `KHP_ICONWORD` | files-3 (`f183b808`); turns `glyphcost` red (O5) | +80 |
| `KHP_STRRUN` | text-6 (`c01cde29`) | +362 |
| `KHP_LAZYEND` | pointer-6 (`8924a280`); pays with O4 | +8 |

Made from lean's own revert commits, applied in reverse (so each change is
in exactly lean's context). `softgfx.inc` carries two of them, and the merge
for it is built from two independent variants of the plain file and refuses
if their regions touch: patching a file that already held one knob's arms put
the second knob's change into the wrong copy of a near-identical body, which
the proof caught. Proved at one commit: with these six off, kern_big,
kern_small and kern_mega assemble **byte-identical** to lean; with them on,
kern_big and kern_small assemble byte-identical to lean with the seven
commits re-applied as plain code; `KHP_MONOPAT` and `KHP_MONORECT` each match
their own single variant alone.

**`tests/unit/t_khpknobs.py`** (row `khpknobs`, full tier, ~3.7 s) assembles
kern_big and kern_small with every `KHP_*` knob the sources test turned on, so
an arm no shipped build assembles cannot rot unseen. Broken on purpose (one
call in `KHP_STRRUN`'s arm aimed at a label that does not exist) it fails,
while the shipped kern_big still assembles. `tools/stkbalance.py`'s knobs-on
walk reads these arms too, on every `make`.

**disk-4 is not a knob**: it rewrites the same `dsk_rah_fill` and
`disk_mount_x` code the shipped standalone disk-5 does, so it cannot sit
beside it. Its commit and its size trim are kept as
`docs/plans/khp1/patches/disk4-loader-cylinder-fill.patch` and
`disk4-size17-multi-stamp.patch`, against `kernel-hotpaths`: a reference for
re-doing it as an explicit prefetch call, not a drop-in. With
`growbox-blit1.patch` (O10) that is every byte-costing item of the pass, in
the tree.

### L.4 `KHP_LZ` on kern_big (+190)

The owner's words: *"We use both; I don't want them split. Lets turn it
on."* Both decoders together: disk-1 (lz4, +126), disk-2 (LZB, +72, which
builds on disk-1's budget register) and size-12 (-8). kern_big and kern_emu
define `KHP_LZ` (`0445ea49`), as kern_mega already did; kern_small does not
(floppy-only machines, where the decode is the smaller share of a launch).

Bytes: kern_big `.cold` 37,844 -> 38,034 (+190), resident 91,577 -> **91,767**,
`KERN_SIZE` 97,280 unchanged (3,584 spare); kern_emu +190 (`KERN_SIZE` one step
to 97,792); kern_small and kern_mega unchanged. `hotpar` green.

Measured on this tree (`os88hot ab --base cc148312 time launch`, CGA): a
CACHED Calc launch (LZB, the decode with no disk) **942 -> 834 ms, -11.5%**,
the same in all three runs; a cold one 1,241 / 1,325 / 1,404 -> 1,285 / 1,200
/ 1,219 ms, a mean of -88 ms (-6.7%), within the platter's phase of the
-108 ms decode saving. The decode alone, as disk-1/2 measured it: lz4 -41.9%
(Note Pad 405 -> 235 ms), LZB -37.0% (Calc 290 -> 183 ms).

Rows, once: the twenty decoder and loader rows (`lz*`, `mseglzslack`,
`ldcost`, `smalllaunch`, `tmload`, `kzboot`) **20 of 20 passed**.

## 0. The owner's picks at a glance, ranked

Every byte-costing item, in one place, ranked by user-visible time per
resident byte on kern_big and by how often a user meets the operation. The
grades are the coordinator's reading of the agents' measured figures
(sections 1 and 4, and each note); the ms are MEASURED on MartyPC unless
marked P. "Needs" is a hard dependency: the later commit edits code the
earlier one adds. **Dropping a pick is `git revert <commit>`** (in the order
"needs" implies). Dropping all of them still leaves a kernel faster and
smaller than the base.

**A - clear wins: a lot of time for few bytes, on operations met all day.**

| id | commit | kern_big B | what it buys |
|---|---|---:|---|
| disk-5 | `073d705b` | +40 | 360/720 KB: open a root 4 -> 2 int 13h calls, enter a folder 3 -> 2, Up 3 -> 1 (~0.4 s a call). Needs disk-4 |
| files-1 | `048c7097` | +5 | Up One Folder ~-250 ms (one boot-sector read fewer) |
| wm-1 | `9c64eaea` | +4 | a covered window put back whole is not re-saved: fullscreen exit CGA -106 ms |
| sched-1 | `488ea2d5` | +47 | `OSAPI_TASK_YIELD` 644 -> 283 us; the idle tick -34%; pointer and typing CPU -12 to -20% |
| sched-4 | `d0ad21da` | +12 | the idle task halts again: pointer CPU -8%, idle CPU -11.6% |
| disk-1 | `f47761a8` | +126 | lz4 decode ~1.7x: Note Pad launch -170 ms, Paint -254 ms |
| disk-2 | `d122c19d` | +72 | LZB decode ~1.6x: Calc -107 ms, TexPad -469 ms. Needs disk-1 |
| disk-4 | `1217b318` | +208 | one int 13h call fewer on most launches (360 KB and 1.44 MB): ~-400 ms each |
| mono-2 | `58ea45aa` | +16 | 1bpp save/restore by words: raise -11 ms CGA, -15 ms Hercules |
| wm-4 | `233ccd04` | +34 | fullscreen exit and saver wake by the damage pass: with wm-1, CGA 781 -> 629 ms, VGA 1,203 -> 706 |
| text-8 | `1139897c` | +6 | `font_width_x` -69%: ~-0.9 ms a menu open (P) |
| gfxcall-1 | `10323197` | +6 | `gfx_rect_setup` 751 -> 639 cycles: -1.27 ms a CGA raise |
| files-2 | `e02c1b7e` | +7 | a scroll moves the thumb only: CGA PgDn -18 ms, fewer transients |
| vga-4 | `8b1de03b` | +3 | VGA XOR fill -36%: menu highlights, every drag-outline step |
| text-9 | `499dfc5c` | +2 | VGA `font_char` -6% a glyph |

**B - good value: tens of ms on common operations for tens of bytes.**

| id | commit | kern_big B | what it buys |
|---|---|---:|---|
| vga-1 | `7e469c23` + `7390cd6d` | +41 net | VGA save/restore by words: a raise's save 133 -> 99 ms, menu drop and dismiss -10 ms each. Revert `7390cd6d` first |
| vga-2 | `71a90157` | +39 | VGA solid/grey fill interiors by words: most of a VGA raise's fill time |
| wm-6 | `ce2df232` | +38 | a vacated strip under another window draws nothing: drags over hidden windows -12 to -32% (+~2 ms where nothing is hidden) |
| wm-3 | `cd852c89` | +24 | an unclipped caption as one `font_run_x`: -3.5 to -8.5 ms a CGA raise (kern_big only) |
| vga-3 | `9e5511d1` | +21 | VGA fill edge columns two rows a pass: `VLINE 128` -21% (one-row lines +1.9%) |
| mono-3 | `1eabf883` | +56 | 1bpp scroll by bank: `GFX_SCROLL` -25%; PgDn -28 ms on its own base, but most of that overlaps files-2/3 on the merged tree (~-4 ms left) |
| mono-4 | `87574fa3` | +22 | 1bpp pattern fill -28% |
| gfxcall-4 | `2f15d7df` | +18 | an unarmed progress widget answers before the frame: -64 us every drawing burst (~-2 ms over 31 keys) |
| wm-2 | `db2e9670` | +10 | an inactive title's caption gap is not filled: ~-2 ms a caption |
| pointer-2 | `49f6681c` | +81 (`.vga`) | the VGA cursor saves/restores its own cell: `cur_move` 21,451 -> 15,103 cycles, VGA pointer CPU -5 points. **This one crossed kern_big's top rung** |
| files-3 | `f183b808` | +80 | 1bpp icons by words: with files-2, CGA PgDn -25%, Hercules -11%. **It is also why `glyphcost` reads red (O5)** |

**C - the real choices: large bytes, or small gains, or both.**

| id | commit | kern_big B | what it buys | why it is a choice |
|---|---|---:|---|---|
| mono-1 | `13aeb31a` | **+344** | the 1bpp rect walked a bank at a time: `GFX_FILL 64x64` -51%, CGA raise -9 ms, menu open -5 ms, drag pass -6 ms; small fills +2% | the largest single item. Its value is in every big fill a package draws (Paint, games, dialogs), not in the desktop scenarios alone. kern_big only. mono-5 needs it |
| mono-5 | `bab1b495` | +16 | XOR columns two rows a loop: a drag's XOR kernel 49 -> 40 ms busy | needs mono-1 |
| text-6 | `c01cde29` | **+362** | `font_str_run`: a transparent string in one pass, 2.4x on a 17-cell caption (13.1 -> 5.6 ms) | +346 on kern_small too. After wm-3 it mostly speeds clipped captions and packages' transparent text: the desktop scenarios moved ~1-3% |
| wm-7 | `4dd8b6d6` | +90 | a partial raise redraws outline and shadow only where covered: CGA raise -5 to -14 ms | good, but +90 |
| vga-5 | `da0b1070` | +32 | `gfx_blitp` wide rows by words: Paint's canvas ~-53 ms (P) | predicted, not measured on an operation |
| gfxcall-3 | `4ea77a8b` | +26 | `gfx_frame`'s entry questions once a frame: ~-0.27 ms a raise | small |
| pointer-6 | `8924a280` | +8 | `cur_lazyend`'s early answer: 477 -> 224 cycles | pays only if O4 is taken; no measured scenario reaches it today |
| disk-3 | `04528080` | +14 | the read-ahead drop that disk-5 builds on | superseded by disk-5 on kern_big and kern_small; it is kern_mega's only form of the fix. Keep it while disk-5 stays |

## 1. Byte-costing items: the owner's picks

Each row is one commit whose subject starts `[+N]`, revertable on its own.
They are listed by agent; section 0 ranks them by value.

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| gfxcall-1 | gfxcall | `gfx_rect_setup` (softgfx.inc), every rect primitive in both renderers, `gfx_save`/`gfx_restore`, the plane blits | H2 | the row table read inline (no `gfx_rowbase` call, no reload of y1), the y empty test is the `sub` that counts the rows, both masks held in DL/DH so a one-byte rect combines in a register | +6 | +6 | **751 -> 639** a call (54 calls in a CGA raise; stepped) | raise C 178.6 -> 176.7, H 229.3 -> 226.6, V 365.9 -> 363.4 (all three items together); a CGA raise's 54 set-ups -6,048 cycles = **-1.27 ms** | M | fewer instructions and no new word operand at an odd address; hot list parity kept. Not measured on 86Box | `10323197` | [+6] OWNER'S PICK |
| gfxcall-3 | gfxcall | `gfx_frame` (vga12.inc): every window frame, title-bar box, menu frame, dialog and button outline | H2 | `GFXCLIP`'s three questions (region, cursor hide, display count) asked once an outline; the four strips go to `gfx_fill_raw`. Region, hole, second display and GFXAUDIT keep the old per-strip path | +26 (1 is a parity pad) | +20 | **11,355 -> 10,707** an 11x11 title-bar box (stepped); gfxbench `GFX_FRAME 64x64` 6,142 -> 5,895 us | 2 frames in a raise: -0.27 ms; menu drop -0.25 ms; scales with every outline a dialog draws | M | same | `4ea77a8b` | [+26] OWNER'S PICK |
| gfxcall-4 | gfxcall | `fpg_finish` (fprog.inc), reached by every `gfx_unlock` | H2 (per drawing burst) | the unarmed state answered before the frame: its only effect there was `[cur_barok]` = 0, which `fpg_step.out` does | +18 (1 is a parity pad) | +10 (1 pad) | `fpg_finish` **483 -> 176**; `gfx_unlock` without interrupts 1,497 -> 1,183 (stepped); gfxbench `GFX_UNLOCK+LOCK` pair **473 -> 403 us (-14.8%)** | -64 us per drawing burst: typing 31 keys = 31 unlocks = -2.0 ms; a Task Manager refresh, a clock tick, every drag step's unlock | M | same | `2f15d7df` | [+18] OWNER'S PICK |
| vga-1 | vga | `vga_save_vram_v`, `vga_restore_vram_v` row loops | H1 | words (`rep movsw`, 25 clk/word vs 17/byte). Two loops (even words; odd words + `movsb`) chosen once a plane. Row steps in registers. The restore's sub-rect step read into BP before DS moves. (`7e469c23` also had a one-word loop for the pointer's cell; `7390cd6d` took it out once pointer's own walks landed) | **+41** (+69 then -28) | 0 | menu drop save 188,240 -> 138,856. Menu dismiss restore 199,357 -> 145,101. (Before pointer's change, the pointer cell too: save 6,000 -> 5,100, restore 8,020 -> 6,250; pointer's own walks now replace that path) | 12-op scene: saves 536 -> 406 ms (-24%), restores 433 -> 316 (-27%). Raise Paint over Note Pad: save 132.9 -> 99.4 ms, restore 52.6 -> 36.3. Menu drop 39.4 -> 29.1, dismiss 41.8 -> 30.4 | M | words are one bus cycle on a 16-bit card; a straddling word is two (= the byte loop's traffic) | `7e469c23` + `7390cd6d` (revert the second first) | [+41] OWNER'S PICK |
| vga-2 | vga | `vga_solid_rect` / `gfx_fill_gray` interiors | H1 | `rep stosw` interior, odd byte by loop choice once a call | +39 | 0 | gfxbench `GFX_FILL 256x128` 13.37 -> 10.67 ms. `GFX_FILL 64x64` 3.56 -> 3.32. `GFX_FILL_GRAY 64x64` 4.08 -> 3.89. `GFX_FILL 8x8` unchanged | every window background, the desktop, troughs: part of raise 368.6 -> 292.4 ms and drag 610.4 -> 537.0 | M | as vga-1 | `71a90157` | [+39] OWNER'S PICK |
| vga-3 | vga | `vga_solid_rect` edge columns | H1 | two rows a pass, odd count enters mid-loop, step in CX | +21 | 0 | gfxbench `GFX_VLINE 128px` 2.08 -> 1.64 ms (-21%). `GFX_FILL 64x64` 3.32 -> 2.88 (-13%). `GFX_FRAME 64x64` -11%. **`GFX_HLINE 8px` 488.7 -> 498.2 us (+1.9%)**, `GFX_FILL 256x1` +2.6% | mean fill in the scene's raises -4% to -16% a call | M | neutral: fewer instruction bytes | `9e5511d1` | [+21] OWNER'S PICK |
| vga-4 | vga | `vga_solid_rect` XOR interior | H1 | `rep movsb` over itself (SI = DI, DS = ES = A000): the read is the latch load | +3 | 0 | gfxbench `GFX_XOR_FILL 64x64` 6.75 -> 4.35 ms (-36%). `GFX_XOR_RECT 256x1` 1.09 -> 0.88 (-19%). `256x128` 5.93 -> 5.52 | menu highlight and selection bars; every drag-outline step (four strips, erase and draw) | M | neutral | `8b1de03b` | [+3] OWNER'S PICK |
| vga-5 | vga | `gfx_blitp_v` fast path (`.frow`) | H1 | rows of 8 bytes or wider as words, loop picked once a plane | +32 | 0 | gfxbench `GFX_BLITP 256x16` 9.48 -> 7.97 ms (-16%). `GFX_BLITP 64x64` (8-byte rows) 12.38 -> 12.43 (break-even) | Paint's planar canvas (56-byte rows): a whole 448x280 canvas ~-53 ms, PREDICTED from the per-word saving. Games' bands (1942, Excitebike, DrMarco) | M (P for Paint) | words win more on a 16-bit card | `da0b1070` | [+32] OWNER'S PICK |
| mono-1 | `sw_plane_op` / `sw_col` -> bank walk (`sw_walk`, six row bodies), SPEC.md 39.31 | H1 | rect walked as B bank phases, whole rows a pass, whole edges folded into `rep stosw`, one-row rects direct, XOR interior a word at a time; short rects keep the old body | +344 (338 `.text` + 6 `.bss`) | 0 | `GFX_FILL 64x64` 30,250 -> 14,870 cyc; `GFX_VLINE 128px` 21,330 -> 16,370; `GFX_FILL 8x8` 3,180 -> 3,250 (+2%) | raise CGA 179.1 -> 170.4, Herc 229.3 -> 214.2; PgDn CGA 131.2 -> 122.7, Herc 131.8 -> 127.7; menu open CGA 46.2 -> 40.9, Herc 47.6 -> 42.8; drag busy per pass CGA 75.0 -> 69.0, Herc 94.3 -> 73.0 | M | 86Box AT/286 raise: busy 23 -> 20 ms (whole branch, relative only) | `13aeb31a` | **[+344]** |
| mono-2 | `sw_xfer` (gfx_save / gfx_restore), SPEC.md 39.32 | H1 | `rep movsw` + `adc cx,cx`/`rep movsb` odd byte; row step and wrap bit in registers | +16 | +16 | ~26 -> ~19 cyc/byte (P from the raise) | raise CGA 170.4 -> 159.3, Herc 214.2 -> 198.8; menu open CGA 40.9 -> 40.2; menu close CGA 22.8 -> 22.2 | M | movsw also wins on a 16-bit bus | `58ea45aa` | **[+16]** |
| mono-3 | `vgas_bankcopy` (1bpp `gfx_scroll`), SPEC.md 39.33 | H1 | whole-bank scroll walked bank by bank, register step, `rep movsw` | +56 | +56 | `GFX_SCROLL 256x128` 1,208 -> ~900 cyc a row | `GFX_SCROLL 256x128` CGA 30.40 -> 22.55, Herc 30.09 -> 22.85; PgDn CGA 122.7 -> 95.0, Herc 127.7 -> 111.2 (isolated) | M | 86Box AT/286 scroll: PgDn 16 -> 14 ms (whole branch) | `1eabf883` | **[+56]** |
| mono-4 | `sw_fill_pat` / `sw_patcol`, SPEC.md 39.34 | H1 | row step inlined in all three loops, no DI push/pop, one-store masked byte | +22 | +22 | `GFX_FILL_PAT 64x64` 64,130 -> 46,140 cyc | `GFX_FILL_PAT 64x64` CGA 13.44 -> 9.67, Herc 13.08 -> 9.59 (Word's ruler, Disk-window chevrons) | M | - | `87574fa3` | **[+22]** |
| mono-5 | `sw_k_xcol` (drag outline sides) | H1 | two rows a `loop` | +16 | 0 | `XOR_RECT 256x128` 6.12 -> 5.58 ms | drag, `sw_k_xcol` 49.0 -> 40.2 ms of busy a drag (CGA) | M | - | `bab1b495` | **[+16]** (needs mono-1) |
| text-6 | `font_str_x` -> new `font_str_run` | H2 | a transparent string in ONE row-major pass (SPEC.md 6.1.13): pass 1 into `font_rn_tab`, a row of the whole string at a time, the byte two cells straddle merged once; anything not provably identical returns CF=1 and the old walk runs | **+362** | **+346** | 10 cells 35,853 -> 16,103 (**-55%**), 17-cell title at x&7=3 62,728 -> 26,523 (**-58%, 2.4x**), in a one-fragment region -63%; CGA and Herc alike. Cut by a region: +4% (pass 1 wasted). VGA: +0.3% (the door) | **raise a Disk window, busy: CGA 170.3 -> 168.0, Herc 221.9 -> 219.0 ms** (whole branch; the raise letters two short captions); a 17-cell caption 13.1 -> 5.6 ms on the XT | M | same | c01cde29 | **[+362] OWNER'S PICK** |
| text-8 | `font_width_x` | H3 | `repne scasb` | **+6** (5 + PARITY) | **+6** | 40 chars 2,621 -> 810 (-69%), same AX | ~0.9 ms of a menu open's layout (8 strings), P | M (routine) / P (op) | wins (string instructions are cheap on a 286) | 1139897c | **[+6] OWNER'S PICK** |
| text-9 | `font_char` VGA writer | H1 | GC index set to 8 once a cell, masks as one-byte outs to 3CFh; column mask in BL | **+2** | 0 | `font_char` 2,961 -> 2,775 (-6.3%), skewed -8.3%; `font_str_x` VGA -5 to -6.5% | VGA only: transparent text, P ~0.2 ms a caption | M | same | 499dfc5c | **[+2] OWNER'S PICK** |
| pointer-2 | `cur_saveu`/`cur_restoreu` (VGA, EGA), `cur_rect` gone, `cur_vspan` new | H2 (per packet on a planar display, and every planar show/hide) | the cell's own four-plane walks instead of `vga_save_vram`/`vga_restore_vram` + `gfx_rect_setup` + the one-plane arm banking | **+81** (`.vga`) | 0 | `cur_move` 21,451 -> 15,103 VGA (save 5,800 -> 4,650, restore 7,600 -> 4,600) | VGA: serial ISR share while sweeping 21.5% -> 16.4%; packet byte 5.12 -> 3.79 ms. Also every planar lazy hide/show (typing under the pointer on VGA, a raise) | M | `movsb` loops; the 286 gains the rect setup outright | `49f6681c` | **[+81] OWNER'S PICK** |
| pointer-6 | `cur_lazyend` (from `gfx_unlock` when a hold's hide survived) | H3 (per unlock of a hold away from the pointer) | "not moved" asked before the eight pushes, `pushf` and `cli`; the move reloads the position under cli (gfxcall's find, its section 7) | **+8** | **+8** | not moved 477 -> 224, moved 18,984 -> 11,463 (MartyPC CGA, called in place, base `87104a50`) | ~0.05 ms an unlock; no os88hot scenario reaches it today (a paint near the arrow spends the promise first), so it pays mainly if gfxcall-5 is taken | M | same | `8924a280` | **[+8] OWNER'S PICK** |
| wm-1 | `wm_draw_win` -> `wm_su_bank` | H3 | a covered window put back WHOLE from its cache is not re-banked (SPEC.md 11.96.4.1) | +4 | +4 | 3,712,424 -> 3,206,000 (`wm_paint_all`, deskbench fullscreen exit, item alone) | fullscreen exit, CGA: 777.8 -> 671.7 | M | same (a save is a copy) | 9c64eaea | **[+4] OWNER'S PICK** |
| wm-2 | `wm_draw_title` | H3 | an inactive bar does not fill the caption's gap (SPEC.md 11.92.1) | +10 | +10 | the gap fill, ~10k cycles a caption | ~2 ms an inactive caption, CGA: every raise (1), every wm_paint_all (3) | M | same | db2e9670 | **[+10] OWNER'S PICK** |
| wm-3 | `wm_draw_title` | H3 | an unclipped caption is one `font_run_x`, not a `font_char` a glyph (kern_big) (SPEC.md 11.92.2) | +24 | 0 | a caption 7.2 -> ~1.5 ms (8 cells, CGA) | with wm-2, raise CGA: Note Pad 172.9 -> 168.0, Disk 158.6 -> 152.9, CP 155.3 -> 147.3, Paint 179.9 -> 170.9 | M | same | cd852c89 | **[+24] OWNER'S PICK** |
| wm-4 | `blk_relit` | H3 | a screen given back is the damage pass over the whole band (SPEC.md 11.91.7) | +34 | +28 | 3,726,000 -> 3,003,000 with wm-1 (`blk_relit`, CGA) | fullscreen exit, with wm-1: CGA 780.7 -> 629.2, Hercules 983.9 -> 727.4, VGA 1,203.0 -> 705.5; the saver's wake takes the same path. **CHANGES TRANSIENTS**: a pixel is written about once, not once per window layer (deskbench measured 7-8 transient pixels per changed one at the base) | M | same | 233ccd04 | **[+34] OWNER'S PICK** |
| wm-6 | `wm_dmg_stale` | H3 | a window whose vacated part is under somebody else is not marked (SPEC.md 11.91.8) | +38 | +38 | the Control Panel's draw, ~165k cycles, gone; +~2 ms of region walk a drag where nothing is hidden | drag, scene z-order, CGA: Paint away 241.9 -> 212.0, back 257.7 -> 220.8; Hercules Paint away 306.1 -> 250.6, Control Panel 260.6 -> 182.1 / 251.7 -> 171.0; VGA Control Panel 334.6 -> 228.9 / 331.1 -> 234.7; a drag with nothing hidden +0.3..+1.6% | M | same | ce2df232 | **[+38] OWNER'S PICK** |
| wm-7 | `wm_cov_rect`, `wm_raise`, `wm_draw_win` | H3 | a partial raise redraws its outline and shadow only where they were covered (SPEC.md 11.96.10.2) | +90 | +91 | three window-high 1-px columns -> those that were covered | raise, CGA (with wm-2/3): Note Pad 167.7 -> 153.6, Disk 152.7 -> 147.6, CP 147.3 -> 142.4; full tip vs base on Hercules Note Pad 190.5 -> 162.6, VGA 157.3 -> 138.0 | M | same | 4dd8b6d6 | **[+90] OWNER'S PICK** |
| files-1 | files | `fm_c_up` (files.inc) | H4 | sync only a window the globals do not describe; the quiet stand's boot-sector read was always followed by `fm_go`'s own | +5 `.cold` | +5 `.cold` | int 13h 4 -> 3 per Up (motor stopped) | Up One Folder, CGA: 1,336 / 1,122 -> 1,048 / 912 ms (two folders, ~-250 ms, one isolated sector) | M | same call saved | `048c7097` | **[+5] OWNER'S PICK** |
| files-2 | files | `fm_scroll_by` middle tier, `fm_draw_rows` split (`fm_draw_rcore`) | H3 | a windowful scroll fills left of the bar and moves the THUMB through `fm_scrollpaint`'s tail (`os88ui_sbmove`, 3 calls) instead of `os88ui_sbar` (16) | +7 `.cold` | +7 `.cold` | 64 -> ~51 drawing calls (PgDn, 12-entry folder) | PgDn CGA 131.2 -> 113.6 ms, PgUp 124.2 -> 105.4 (os88hot ab -n 3). Herc/VGA list PgDn take the blit tier there: 0; their icon-view scrolls take this tier. **CHANGES TRANSIENTS** (fewer): CGA PgDn 9 -> 7 frames of visible redraw, 8 -> 6 flashing, worst 1,487 -> 927 px | M | CPU-only, same | `e02c1b7e` | **[+7] OWNER'S PICK** |
| files-3 | files | `ico_pass_bb` (icons.inc) | H1 inside H2 | at shift 0 with the icon wholly inside the stride, one word or / and-not a row word, the op picked once a pass; the byte walk kept for shifted and edge icons | +80 `.text` | +80 `.text` | one 16x16 pass ~13,000 -> ~5,000 cycles (~2.9 ms an icon, CGA) | PgDn CGA 113.6 -> 99.0 ms (131.2 at base, both items -24.6%), PgUp 105.4 -> 91.9 (-26.0%); Herc PgDn 131.9 -> 118.0 (-10.5%), PgUp 124.0 -> 109.3 (-11.8%); open A: CGA `ico_pass_bb` 24.7 -> 15.5 ms a rep. VGA/EGA 0 (planar pass) | M | PREDICTED a win (fewer instructions; a 16-bit bus takes the word op whole when DI is even, which an aligned 16px icon's is not always) | `f183b808` | **[+80] OWNER'S PICK**. Crosses kern_big's rung: KERN_SIZE 96,768 -> 97,280 |
| disk-1 | disk | `lz_lz4` | H1 (per byte; every LZ4 package, driver, module, part, CZ file) | the plain symbol inline; a BUDGET register `min(REM, FFFFh-DI)` makes the take one `sub`/`jb`; REM settled lazily by `lz_sync`; BP the checkpoint; slow paths unchanged | +126 | +126 | Note Pad's decode 1,931,258 -> 1,122,698 (17,604 bytes, filtered) | launch Note Pad, CGA: decode 405 -> 235 ms; Paint 607 -> 353 ms | M | same direction (fewer memory operands, fewer taken branches); not measured | f47761a8 | [+126] |
| disk-2 | disk | `lz_lzb` | H1 | the same budget for the literal byte and the plain match; bit refills out of line | +72 | +72 | Calc's decode 1,384,293 -> 871,484 (4,566 bytes) | launch Calc, CGA: decode 290 -> 183 ms; TexPad 1,300 -> 831 ms | M | as disk-1; not measured | d122c19d (depends on f47761a8) | [+72] |
| disk-3 | disk | `disk_mount_x` | H4 (every navigation) | a listing mount drops the read-ahead BEFORE the FAT window, so the window's fill serves the root | +14 | +14 | - | open B: at its root, 360KB: 4 -> 3 calls (int 13h time 1,012 -> 788 ms on GLaBIOS); superseded on kern_big/small by disk-5, kept on kern_mega | M (calls), P (~400 ms a call on the 5150) | none: calls | 04528080 | [+14] |
| disk-4 | disk | `dsk_rah_fill`, `ld_run_body_x` | H4 (every launch) | the loader's header peek arms a hint (the file's sectors); its fill runs to the cylinder's end over several slots in ONE call | +208 | +208 | - | launches, 360KB: Note Pad 3 -> 2 calls, TexPad 3 -> 2, Paint 4 -> 3, Calc 1 -> 1; 1.44MB: 2 -> 1, 2 -> 1, 3 -> 2, 1 -> 1. Peek + read, GLaBIOS: Note Pad 1,193 -> 1,038 ms, Paint 1,847 -> 1,541, TexPad 1,292 -> 1,097 | M (calls and GLaBIOS ms), P (~400 ms a call on the 5150) | none: calls | 1217b318 | [+208] |
| disk-5 | disk | `disk_mount_x` step 1 | H4 (every navigation) | a listing mount empties the cache first and reads LBA 0 THROUGH it as a fill of eight (LBA 0-7: boot sector, FAT window, root head); the chunk re-tagged with the new disk's signature | +40 | +40 | - | 360KB and 720KB: open B: 4 -> 2 calls, enter APPS 3 -> 2, Up 3 -> 1 (with disk-3); 1.44MB unchanged | M (calls), P (ms) | none: calls | 073d705b (depends on 1217b318) | [+40] |
| sched-1 | sched | `sch_isr` wake loop, `sch_switch` scan, `task_spawn`, `task_exit` | H2 | the two loops stop at `sch_top`, one past the highest slot that can be live (SPEC.md 8.1.3) | +47 | +47 | idle tick 17,558 -> 11,533 (CGA, kern_big); 12,393 -> 10,897 (kern_small); a free slot in the scan ~144 -> 0; a no-op yield ~2,630 -> ~1,100 | `OSAPI_TASK_YIELD` 644 -> 283 us (sysbench, CGA, -56%); mouse packet to UI pass, scheduler share 0.74 -> 0.38 ms; menu open 45.0 -> 42.9 ms and close 24.3 -> 22.7 ms; pointer sweep busy 236.6 -> 189.8 ms (-19.8%); typing 31 keys busy 618 -> 545 ms; idle busy 6.5% -> 4.1%; with Fractal's worker running a tick 11,811 -> 9,516 cycles (+0.9% to the render) | M (all; ms rows are sched-1 + sched-2 together, os88hot `ab`) | fewer iterations on every CPU; the parity of `sch_isr`, `sch_switch` and everything after them is kept by three never-executed pads (`t_hotpar` green) | 488ea2d5 | **[+47] OWNER'S PICK** |
| sched-4 | sched | `sch_idle_body`, `sch_isr`'s wake loop, `sch_switch`'s fallback | H2 (every interrupt while idle) | the idle task halts again after an interrupt that posted no wake; the tick's wake loop posts `[sch_idlewake]`, the "nothing ready" fallback spends it (SPEC.md 8.1.2.5). Not on kern_emu | +12 | +12 | idle tick 11,205 -> 9,982 (CGA); kern_small 10,897 -> 9,362 (with sched-2); the idle task's yields over 30 mouse packets 218 -> 29 | pointer sweep busy 191.1 -> 175.9 ms (-8.0%); typing 31 keys busy 544.6 -> 516.7 ms (-5.1%); idle busy 203.2 -> 179.7 ms / 5 s (-11.6%); ms unchanged (+0.2-0.3%, rep noise; a wake pays ~90 cycles more before its yield) | M (os88hot ab vs 89fe86d7) | none beyond the cycles; parity kept by one pad | d0ad21da | **[+12] OWNER'S PICK** |

## 2. Free items: faster and no resident byte added

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| gfxcall-2 | gfxcall | `gfx_hline` shim (vga12.inc) | H2 | BX restored from gfx_fill's preserved CX instead of a push/pop pair | 0 | 0 | 3,536 -> 3,511 (a 312-px pinstripe) | ~25 cycles a hline, 9 in a raise: 0.05 ms | M | same | `9fd79fb6` | FREE |
| vga-6 | vga | `vga_blit_span` | H2 (per run) | stop banking AX, CX, DI, which its one caller reloads | -6 | 0 | gfxbench `GFX_BLIT4 solid` 39.38 -> 38.18 ms (-3.0%), ~80 cycles a run | every coalesced blit4 run on VGA | M | neutral | `47639976` | FREE |
| vga-7 | vga | `gfx_blit4` `.row` prologue | H2 (per row) | the 1bpp decoder's pair-table choice moved after the fst test, so planar rows skip it; parity via `shr dx,1` | 0 | 0 | gfxbench `GFX_BLIT4 solid` 38.18 -> 36.97 ms (-3.2%), ~95 cycles a row | every direct-path blit4 row on VGA; 1bpp unchanged | M | neutral | `f483f74c` | FREE |
| vga-0 | vga | `tools/os88sym.py` | tool | `.vga` resolves at KERNEL_SEG (it raised for every rung label) | 0 | 0 | - | - | - | - | `bbf8d217` | FREE (host tool; merged to kernel-hotpaths already) |
| text-1 | `font_ink` | H2 | `[font_dith]` = `[gfx_dis]` (one store); `gfx_ink`'s lookup inline; 00/FF by `add al,1 / sbb ch,ch` | -1 | -6 | part of text-2's row | per transparent glyph | M | same direction (call/ret and a branch gone) | b35e32d6 | FREE |
| text-2 | `font_char_bb` | H1 | row step and wrap bit in DX/BP (pushed once a cell), not CS memory operands; the disabled loop's checkerboard DL -> CH | -6 (+1 PARITY pad) | -6 | `font_char` 'W' 3,669 -> 3,314 (-9.7%); `font_str_x` 10 cells 35,853 -> 32,303 (-9.9%) CGA, Herc the same | caption, CGA: ~1.0 ms off a 17-cell title | M | wins more (word operands, no EA) | b35e32d6 | FREE |
| text-3 | `font_run_x` prologue | H2 | the two `font_ink` calls and the `[gfx_dis]` save/clear/restore become an inline `gfx_inktab` reduction; xm/bm one word store | (commit 7dec8353 net: kern_big -2, kern_small 0) | | run1 5,334 -> 4,536 with text-4/5 | with text-4/5 below | M | same | 7dec8353 | FREE |
| text-4 | `font_run_x` pass 1 | H2 | how many cells fit is asked ONCE ((cwm8-x)/8+1, capped at FONT_RN_MAX+1); a cell is one `loop`, one unsigned range test, printable falls through | (in 7dec8353) | | ~100 cycles a cell | with text-5 | M | same | 7dec8353 | FREE |
| text-5 | `font_run_x` pass 2 | H2 | table biased +8, BP counts -8..0 (`inc`/`jnz`, three loops); the one-display door skips GFXDENTERCD's second test | (in 7dec8353) | | text-3..5 together: 10 cells 14,280 -> 12,582 (**-11.9%**) CGA / 14,240 -> 12,542 Herc / 13,100 -> 12,098 VGA; 78 cells -10.4%; padded 20 -16% | **menu open busy CGA 36.0 -> 35.3, Herc 36.1 -> 35.0, VGA 39.3 -> 38.3 ms; PgDn CGA 131.2 -> 126.5, Herc 131.9 -> 127.0, VGA 104.1 -> 100.8 ms** (whole branch) | M | same | 7dec8353 | FREE (the commit: -2 / 0, incl. a PARITY pad) |
| text-7 | `font_run_x` unaligned walk (SPEC.md 6.1.11) | H1 | previous composed byte parks in BH (the next `xchg ax,bx` hands it back as AH), CH counts the cells: the per-cell `cmp si,[cs:font_rn_send]` and `mov ah,ch` go; `font_rn_send`/`font_rn_sendm` retire | -18, bss -2 | 0 | 10 cells at x&7=5 21,836 -> 19,846 this commit (base 23,534, **-16%**); 78 cells skewed 126,714 -> 102,390 (**-19%**); VGA 21,770 -> 18,778 | every off-grid run: captions after wm's `[+24]`, Disk window names in a dragged window | M | same | 6729dd47 | FREE |
| pointer-1 | `cur_move_mono` (+ `cur_mvcols`/`cur_mvrow`/`cur_mvbg` gone) | H2 (per packet) | the overlap decided once as a column delta and ONE row run; the new background banked off the screen, the run patched from the old save inside pass 1, the draw composed from the bank - no per-byte test, no helper calls | -26 | -26 | `cur_move` 19,404 -> 11,973 CGA, 19,303 -> 11,694 Herc (40-packet sweep) | serial ISR's CPU share while sweeping: CGA 20.0% -> 14.1%, Herc 19.9% -> 13.9%; a packet's third byte 4.69 -> 3.13 ms (the latency from the byte to the arrow on the glass) | M | same instructions, all register or word ops; no worse on a 16-bit bus (even shift, `hotpar` holds) | `2b493c0f` | FREE |
| pointer-3 | `mou_apply` (`mou_whl_cl`, `mou_apply_w` new) | H2 (per packet) | the driver/VMware wheel decode (two `sar`, a zero add and a SECOND `sch_wake_ui`) moved out of the path every serial and PS/2 packet runs | 0 (+3 kern_emu) | 0 | ~210 cycles off the packet's third byte | 0.04 ms a packet, ~0.2% of the CPU while sweeping | M | same | `226fb2f8`, and its defect's fix `7e8e3220` (section 9) | FREE |
| pointer-4 | `cur_move_mono` head and background walk | H2 | the save-buffer flip as `(a + b) - live` (no compare, no branch, -4 B); the background walk holds the row step's wrap bit and fix in DX/SI (+4 B) | 0 | 0 | `cur_move` 11,973 -> 11,536 CGA, 11,694 -> 11,413 Herc | packet byte 3.13 -> 3.00 ms CGA | M | same | `226fb2f8` | FREE |
| pointer-5 | `cur_put_mono`, `cur_get_mono` | H2 (per lazy hide / show) | a word a row: one read of both bytes, one word banked, masks rotated into the word's byte order; `lodsw` and the row step in registers for the hide | -8 | -8 | put 6,309 -> 5,234, get 2,916 -> 2,215 (31 typed keys, CGA) | typing 31 keys under the pointer: 11.3 ms less busy, ~0.36 ms a key (CGA). os88hot `ab` of `type` vs `87104a50`, the whole branch: busy CGA 516.9 -> 505.9 ms (-2.1%), Hercules 458.0 -> 447.5 (-2.3%), VGA 525.6 -> 486.3 (-7.5%, pointer-2's planar show/hide) | M | same | `8780a506` | FREE |
| wm-5 | `menu_draw_bar` | H3 | the logo is seven rects, not 78 `gfx_pixel`s (SPEC.md 12.05.1) | -4 | -6 | 78 calls -> 7 | fullscreen exit: CGA 629.2 -> 593.4, VGA 705.5 -> 671.3; every saver wake and the first paint | M | same | f67da13c | **FREE** |
| files-4 | files | `assoc_scan` (assoc.inc) | H4 (listing, harvest) | ES = DS and `cld` once in front of the walk, not per row | 0 | 0 (no OS88_ASSOC) | median 1,959 -> 1,530 cycles a call (os88hot cycles, opendisk, CGA) | first open of B:, ~31 calls: ~-2.8 ms | M | same | `31c9775e` | **FREE** |
| sched-2 | sched | `ui_task` (the pass's first look at the event ring) | H2 (every pass) | enter the drain through `.yield`'s byte compare instead of a full `evq_pop` on an empty ring; `ui_drain` stored and decremented as a byte to pay for the near jump | 0 | 0 | ui_task + evq_pop ~2,180 -> ~1,870 a pass; idle tick 11,533 -> 11,205 on top of sched-1 | every UI pass, idle or event: ~300 cycles (0.06 ms) off the time from an event's wake to its handler | M | none | 31309aca | **FREE** |

### 2.1 The size agent's give-back (all FREE, off the hot paths)

From `docs/plans/khp1/size.md`, which is the authority on each row.

| id | file | routine | what | bytes (big / small / emu / mega) | cycles | commit |
|---|---|---|---|---:|---|---|
| size-1 | `apps/os88ui.inc` (kernel copy) | button body, `os88ui_sbar`, `os88ui_sbmove`, `os88ui_sbpos`, `os88ui_sbthdraw` | the kernel's `.cold` copy ends on `kretc_bp` / `kretc_si` / `kretc_dx` (SPEC.md 15.1.2); a package keeps its pops. `UI_KRET <rung>` at the file-level sites, the rung as a second macro argument to `OS88UI_BTN_BODY` because kern_small expands it into CTRL.DRV and FDLG.DRV (`OS88UI_KFAR`) | -20 / -12 / -20 / -20 | +~20 clocks a button or scroll-bar draw / thumb move, each already several drawing calls (~2.3 ms) | `7d6beace`, `a9972c68` |
| size-2 | `kernel/blank.inc` | `blk_relit` | wm-4's bank of AX..DX leaves by `kret_dx`. **Depends on `233ccd04` (wm-4)** | -2 / -2 / -2 / -2 | +~20 clocks once a relight (a ~600 ms repaint) | `68f50f2a` |
| size-3 | `kernel/vidsel.inc` | `vid_pt_clamp` | CX not banked: the one caller pops its own next | -2 / 0 / -2 / -2 | faster (a push and a pop) | `968d8df2` |
| size-4 | `kernel/wm.inc` | `wm_rz_swap` | DI not banked: `mov di, bx` after one call, `pop di` after the other | -2 all | faster | `968d8df2` |
| size-5 | `kernel/wm.inc` | `wm_su_bank` | AX and BX not banked (the caller leaves by `kret_di`); both arms then TAIL-jump into `wm_su_take` / `wm_su_drop` | -6 all | faster: two pushes, two pops and a call/ret gone a window draw | `968d8df2` |
| size-6 | `kernel/clip.inc` | `clip_drop` | BX not banked: `mov bx, MEM_K_CLIP` next, or `kret_bp` | -2 all | faster | `968d8df2` |
| size-7 | `kernel/memory.inc` | `mem_bcopy` | SI not banked: both callers `pop si` next | -2 all | faster, once a block moved | `968d8df2` |
| size-8 | `kernel/files.inc` | `fm_clamp_scroll` | AX not banked: `kretc_di`, or reloaded from `[fm_lscr]` | -2 all | faster | `968d8df2` |
| size-9 | `kernel/wm.inc`, `kernel/ui.inc` | `wm_dg_precover` | its BX and flags bank and its `call wm_rz_precover / ret` were dead; it is now three instructions that FALL INTO `wm_rz_precover` (pass 17's CROSS-FILE ~-8) | -8 all | faster, once a drag's drop | `eabd9200` |
| size-10 | `kernel/vga12.inc` | `gfx_sub_off` | AX not banked: both restore bodies pop it next | -2 all | ~27 clocks faster a gfx_restore (every menu dismiss, every save-under put-back) | `eabd9200` |
| size-11 | `kernel/toast.inc` | `toast_now` | AX not banked: `toast_show` leaves by `kret_di` | -2 all | faster | `eabd9200` |
| size-12 | `kernel/lz.inc` | `lz_lz4` slow paths | `.mslow`, `.lslow`, `.litx`'s long run and `.end` `call .sync` (lz_lzb's shape) instead of LZ_FRAME + `call lz_sync` each; LZ_FRAME retired. **Depends on `f47761a8` (disk-1)**. The disk agent's inbox item | -8 all | the inline symbol path is byte-identical at the same addresses; each slow-path ENTRY +~35 clocks against the several-hundred-clock round it makes, a handful a file (<0.02% of Note Pad's 1.12M-cycle decode) | `e59c61ea` |
| size-13 | `kernel/assoc.inc` | `assoc_scan` | CX comes back unchanged without its bank: the one write is inside the row's own push/pop. kern_big only (OS88_ASSOC) | -2 / 0 / -2 / -2 | faster, ~31 calls an open of B: | `64d97ff3` |
| size-14 | `kernel/wm.inc` | `wm_su_merge` | SI and DI not banked: `wm_su_edge` reloads both before its second call and leaves by `kret_bp` | -4 all | faster, twice a clipped-edge save-under restore (per raise) | `64d97ff3` |
| size-15 | `kernel/disk.inc` | `dsk_ico_stage` | AX not banked: both callers pop it next | -2 all | faster | `64d97ff3` |
| size-16 | `kernel/disk.inc` | `dsk_fatw_demote` | AX and BX not banked (mem_pg_forget pops both); both copies of the pops go, the live exit is `.out`'s `ret` and the IF = 0 arm falls into `dsk_fatw_home` | -10 / -4 / -10 / -10 | faster, once a FAT window shed | `64d97ff3` |
| size-17 | `kernel/disk.inc` | `dsk_rah_fill` `.multi` | the record stamp as `stosw` x4 + `stosb` with ES:DI on the record (both banked in the prologue); an %if pins the layout. **Depends on `1217b318` (disk)**. The disk agent's inbox item | -7 / -7 / -7 / 0 | once a launch's header peek; equal | `6538be65` |
| - | `kernel/vga12.inc` | `GFX_U_FLOOR` | comment only: its kern_small byte is a .bss parity pad now (see 3) | 0 | - | `6320019c` |

## 3. Where the time goes: the ranked profiles

The pass's map is the baseline report,
`docs/reports/HOTPATH-BASE-2026-11-06.md` (bench, every scenario on three
adapters, with its `.json`). Each agent's note has its own ranked profile in
its section 2, which is the authority for its subject. The ranking that drove
the pass, as measured at the base (busy time of raise, drag, menu, pointer,
type and scroll together, MartyPC):

| rank | CGA | Hercules | VGA | owner | what the pass did |
|---|---|---|---|---|---|
| 1 | `sch_switch` 17.5% | `sch_switch` 17.3% | `vga_restore_vram_v` 16.7% | sched / vga | sched-1 and sched-4 stop the scans at `sch_top` and halt the idle task again (idle tick -43%); vga-1 moves the save and restore by words |
| 2 | `sw_col` 12.1% | `sw_col` 12.3% | `vga_save_vram_v` 15.2% | mono / vga | mono-1's bank-major walk; vga-1 |
| 3 | `sw_xfer` 7.2% | `sw_xfer` 9.3% | `sch_switch` 15.0% | mono / sched | mono-2 (`rep movsw`); `sw_xfer` is still ~33% of a CGA raise, near its byte-copy floor - the lever left is how much the raise cache moves (LOG O8) |
| 4 | `font_run_x` 5.1% | `font_run_x` 5.4% | `vga_solid_rect_v` 7.9% | text / vga | text-3..7; vga-2, vga-3 |
| 5 | `sw_plane_op` 4.7% | `sw_plane_op` 4.6% | `font_run_x` 4.2% | mono / text | mono-1 |
| next | `cur_move_mono` 3.6%, `sch_account` 2.7%, `ui_task` 2.5%, `cur_put_mono` 2.1% | (similar) | `cur_draw`, `sch_account`, `gfx_rect_setup` | pointer / sched / gfxcall | pointer-1..5, sched-2, gfxcall-1 |

Outside the interactive set: the lz4 decoder `lz_lzb` was 18% of a cached
launch (disk-1, disk-2), the BIOS/drive 64% of opening a Disk window (disk-3
to disk-5 cut the calls), `ico_pass_bb`/`ico_pass` 14-18% of a PgDn (files-3).
An idle desktop was 94% halted, and its busy time was `sch_switch` 45% and
`sch_account` 15%.

**Per-operation splits worth keeping** (from the notes):

* A mouse packet on CGA at the pass's base: cursor 4.3 ms, the rest of the
  ISR 1.5, scheduling ~2.5, the UI task ~0.6. At the pointer merge: 2.28,
  1.31, ~1.25, 0.78 (pointer.md 2).
* An idle tick: 17,558 cycles at the base, 9,982 at sched's tip, single-
  stepped (sched.md 2).
* The `gfx_*` fixed part this pass owned turned out to be ~6-8% of a raise
  and ~5% of a 1bpp PgDn (gfxcall.md 2): the 756 us figure is real, but most
  of a redraw is pixel loops, glyph cells, the save-under and the scheduler.
* A VGA raise was 70% save-under and restore (120 + 118 ms of 341).
* A disk launch: the decode was 405 ms of Note Pad's, now 235 (disk.md 2).

## 4. Totals

### 4.1 Bytes (assembled, `tools/kernsize.py --json`; `.mega` kernels from `make mega`)

| kernel | resident at base (`a7ee7fab`) | resident at tip | delta | `KERN_SIZE` / budget, base -> tip |
|---|---:|---:|---:|---|
| **kern_big** | 91,530 | **93,287** | **+1,757** | 96,768 -> **99,328** of 100,864 (1,536 spare, no grace used) |
| kern_small | 57,258 | 58,408 | +1,150 | 59,392 -> 60,416 of 64,000 (3,584 spare) |
| kern_emu | 91,784 | 93,531 | +1,747 | 97,280 -> 99,328 of 100,864 |
| kern_mega | conventional 62,976 | conventional 64,000 | +1,024 (one step) | of `MEGA_RESIDENT` 68,096 (4,096 spare) |

The 35 `[+N]` commits sum to **+1,910** on kern_big (vga's `[-28]`
included). The free items and the size agent's seventeen give back 153, of
which the size agent's are -83 kern_big / -65 kern_small / -83 kern_emu /
-76 kern_mega. Every byte of the pass that is not an owner's pick is a byte
saved. **Dropping every `[+N]` pick leaves the kernel smaller than the base
and still faster** (sched-2, files-4, pointer-1/3/4/5, text-1..7, vga-6/7,
wm-5, gfxcall-2 and all of size are FREE).

### 4.2 What the user sees: the whole pass, base `a7ee7fab` -> tip

`tools/os88hot.py ab --base a7ee7fab time <scenario> --all -n 3` on
MartyPC (the emulated 4.77 MHz 5150/XT), every pick kept. `busy` is the CPU
the operation took; `ms` is the operation's span on the glass. Where the
operation waits on the serial mouse or a key rate, `ms` cannot move and
`busy` is the measure. **Three cells read as regressions at -n 3 and were
re-measured at -n 6/7** (menu close on CGA and Hercules, Hercules drag): all
three are improvements, and the -n 3 readings were packet-arrival phase
(bench's caveat). Disk operations on this MartyPC model the platter, so
their ms are close to the 5150's; the call count is the robust part.

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| **raise a window** (ms) | 182.4 -> **144.3 (-20.9%)** | 230.6 -> **182.4 (-20.9%)** | 368.0 -> **281.0 (-23.6%)** |
| drag a window, 8 packets (ms) | 512.3 -> 475.3 (-7.2%) | 538.0 -> 496.6 (-7.7%, n=6) | 610.7 -> 586.5 (-4.0%) |
| menu open (ms / busy) | 44.2 -> 40.5 (-8.4%) / busy -21.3% | 45.4 -> 38.3 (-15.6%, n=7) / busy -19.5% | 49.6 -> 45.1 (-9.0%) / busy -21.1% |
| menu close (ms) | 23.4 -> 21.0 (-10.4%, n=7) | 25.0 -> 21.5 (-14.0%, n=7) | 30.4 -> 25.1 (-17.5%) |
| **PgDn / PgUp in a Disk window** (ms) | 131.3 -> **88.5 (-32.6%)** / 124.2 -> 82.4 (-33.7%) | 131.9 -> 103.5 (-21.5%) / -21.8% | 104.1 -> 95.0 (-8.8%) / -8.3% |
| **open A:'s Disk window** (ms, int 13h) | 1,750 -> **1,281 (-26.8%)**, 4 -> 3 calls | 1,683 -> 1,320 (-21.6%) | 1,708 -> 1,306 (-23.5%) |
| **launch Calc**, cold / cached (ms) | 1,354 -> 1,127 (-16.8%) / 961 -> 782 (-18.6%) | cached 977 -> 794 (-18.7%) | cached 978 -> 800 (-18.2%) |
| pointer sweep, 24 packets (CPU busy) | 235.5 -> **137.7 ms (-41.5%)** | 234.5 -> 138.1 (-41.1%) | 243.3 -> 154.5 (-36.5%) |
| typing 31 keys (CPU busy) | 618.3 -> 493.2 ms (-20.2%) | 561.9 -> 439.7 (-21.8%) | 624.4 -> 481.7 (-22.9%) |
| **idle desktop, 5 s (CPU busy)** | 326.0 -> **179.7 ms (-44.9%)**: 6.5% -> 3.6% of the machine | same | same |

Not in the table (the agents' own measurements, not re-run here): fullscreen
exit -25/-31/-44% (wm), Up One Folder ~-250 ms and a 360 KB root open 4 -> 2
int 13h calls (files, disk), Note Pad launch 1,598 -> 1,271 ms and Paint
2,454 -> 1,894 ms (disk), `OSAPI_TASK_YIELD` 644 -> 283 us (sched), mouse
byte to arrow on CGA 4.69 -> 3.13 ms (pointer).

### 4.3 Rows

* **The deduplicated union** of every row the nine notes name (148 soak rows
  plus `mega-cpup` and `box-dtwrite`: 150), once, after the last speed
  merge: **149 passed, 1 failed** - `glyphcost`, which is O5 below (a
  relative gate whose reference got faster; no glyph got slower). The first
  run of the union reported 32 failures; 31 of them were the coordinator's
  doing (section 6, lesson 1) and passed on re-run on the final tree.
* The size agent's 20 rows on its own tip (which carries every speed merge)
  passed.
* **`make test-full`** at the final tip: **85 passed, 0 failed, 3 skipped**
  (`w16cens`/`w16censall` need the WIN16 fixtures, `ctoolchain` the C
  cross-compiler; the pass touched neither).
* `make`, `make small`, `make emu` and `make mega` build at the tip; the fast
  tier (with `hotpar`) is green.

## 5. Decisions for the owner

Collected from each note's WANTS THE OWNER, plus what the coordinator found
at the merges. None of these is taken.

| id | from | what | buys | costs / why it is the owner's |
|---|---|---|---|---|
| O1 | files | Remember `ASSOC.DAT` per volume (a one-byte bitmask) instead of for one volume at a time | 1 int 13h + ~84 ms CPU on every switch between an A: and a B: window | ~+10 B; changes which drive a document opens its program from when the program is on both disks |
| O2 | sched | Move the ISR's per-tick accounting charge to `.pick` | ~500 cycles a tick, 0 B | changes SPEC.md 8.1's decision; opens a one-hour wrap for a task never switched away from |
| O3 | pointer | Wake the UI task only on a button edge, a wheel notch or once a tick, not on every motion packet | ~1.5-2 ms a packet, 6-8% of the CPU while the mouse moves | up to 55 ms of lag in cursor-shape and hover feedback |
| O4 | gfxcall | `gfx_rect_setup`'s unclipped head and `fnt_unlazy` ask `cur_lazyrect` before hiding the pointer (gfxcall-5, prototyped and reverted) | Note Pad typing on CGA 611.4 -> 553.9 ms busy, -1.85 ms a keystroke (-9.4%) | the arrow stays lit while drawing: the same question as the off-by-default `CURFIX` knob (SPEC.md 7.1.4.3/7.1.4.4) |
| O5 | coordinator | **`glyphcost` is red on the merged tree, and no glyph got slower.** The shipped fill-drawn radio-set glyph went 266,587 -> 256,579 cycles (-3.8%), but the row's reference, the retired bitmap routine kept inside `tests/glyphbn`, went 216,576 -> 130,752 (-40%), mostly from files-3's word-wide 1bpp icon pass. The row's 2 ms margin is fills minus bitmap, so it now reads +3.295 ms where base read +1.310 ms. | - | The row now says something true: on this kernel the bitmap glyph would be 3.3 ms a call faster than the fills (SPEC.md 13.15.1's trade was taken on look and size, CTRL-GLYPH-PLAN.md 2). Either re-open that trade, re-state the margin against this kernel, or drop files-3 (which also costs PgDn its -10 to -25%). |
| O6 | wm | Do not redraw a stationary drag outline every tick | ~14 ms of every 55 ms tick on CGA while the hand is still, and the outline stops flickering | background painting freezes while the hand is still: a behaviour change |
| O7 | wm | `wm_anim` in four frames instead of six | ~55 ms off every window open, close and minimize (~137 ms today, by design) | the zoom animation's look |
| O8 | wm | A regional raise cache: bank only what the raise will cover, not the outgoing window whole | the outgoing window's save is 50 ms on CGA, 87 on VGA | new cache-validity rules (SPEC.md 11.90-11.96) - a design change, not a speed edit |
| O9 | vga | A VRAM-resident save-under on VGA/EGA, copied through the latches (write mode 1) | VGA raise ~290 -> ~180 ms PREDICTED (from `GFX_SCROLL`'s measured 22.7 cycles a byte); the same idea for the pointer cell | an owner for off-screen VRAM, invalidation on every mode change, and a rework of `wm_su_take`/`wm_su_edge`: the largest single lever left on VGA |
| O10 | wm | The grow box composed into one `gfx_blit1` (files' find); built and kept as `docs/plans/khp1/patches/growbox-blit1.patch`, not committed to the kernel | ~8 ms per grow paint on CGA, PREDICTED | +150 `.text` +39 `.bss` on kern_big; needs a `tests/vidext.txt` line and a parity pad. Refused on value per byte by its author |
| O11 | mono | A frame-aware 1bpp body: one renderer set-up for a frame's two rows and one for its two columns (gfxcall's find) | ~0.4 ms a frame: ~6.5 ms of a CGA drag, ~1.6 ms of a raise, PREDICTED | ~80-100 bytes; it must reproduce each strip's own clipping, which is gfxcall's strip head. Priced at a tenth of mono-1's value per byte; can still land on top later |
| O12 | disk | Reuse the FAT window on a full mount, and skip the boot-sector read while the motor spins (files' finds 2 and 3) | after disk-5, ~1 int 13h (~400 ms) a navigation on 1.44 MB only | DISK-PERF-PLAN phases 2 and 3, which the owner dropped: they narrow swap detection on drives with no change line |
| O13 | disk | Pad R in `tools/os88kz.py` so the 360 KB boot's 64 KB DMA page boundary lands on a cylinder edge, and raise `BLOB_LIFT` (boot-time memory only) | one boot `int 13h` call on 360 KB and 720 KB (cylinder 6 is read in two: 801 ms against ~600 for one) | 0 resident bytes, but a change to a host tool that writes `KERNEL.SYS` (fork rule 6's list) |
| O14 | disk | Re-weigh which packages ship LZB rather than lz4 | at launch LZB is now 191 cycles a byte against lz4's 64 (~3x, was ~4x) | a packing-policy question (disk bytes against launch time), SPEC.md 20.13.5 |

## 6. Method lessons for the next speed pass

1. **Do not merge into the tree a soak is running from.** `os88soak.py`
   freezes its own BUILD tree (`build/trees/plain-*`), but `os88sym`
   re-assembles `kernel/kernel.asm` from the LIVE checkout to check that tree's
   `kernel.bin`, so a merge mid-run makes every later row that resolves a
   symbol fail with "the map describes a DIFFERENT kernel". 31 rows went red
   that way here. Merge after the run, or run the soak from a worktree of
   its own.
2. **A shared instrument first, in its own agent, pays.** `tools/os88hot.py`
   landed ninety minutes in and every speed agent priced its items with the
   same scenarios, the same IRQ0-aligned windows and the same `ab`; the
   baseline report then ranked the work before most of it was written.
3. **Compare medians at more than three reps where a serial packet or a key
   sets the operation's rhythm.** Menu close and Hercules drag read as
   regressions at -n 3 and as -10% to -14% at -n 6/7.
4. **A relative gate turns red when its reference gets faster** (`glyphcost`,
   O5). Read a red timing row for which arm moved before reading it as a
   regression.
5. **The fixed per-call floor was smaller than its reputation.** The 756 us
   figure is real, but gfxcall found it is 6-8% of a raise; the pixel loops,
   the save-under, the scheduler and the disk call count were where the time
   was. The baseline profile, not the folklore, should set the next pass's
   owners.
6. **Cross-file finds routed mid-pass were worth more than some owners'
   own items**: files' disk.inc finds became disk-3..5 (two int 13h calls a
   navigation), and sched's single-stepped packet split told pointer where
   85% of a packet went.

