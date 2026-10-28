# RealDMG on kern_mega: why it was slow, measured before and after (2026-10-25)

A MEASUREMENT, true of the trees named below and of no other (docs/README.md).
Base: `elendilon` at `8eac6506`. After: `mega-dmg` at `e81042ae` (the code)
plus `tools/86box/patches/03-cs-exec-check.patch` (the instrument). Box: a
four-core cloud container; Bochs 2.7 (Ubuntu's), 86Box at
`tools/86box/UPSTREAM`'s pin with this tree's patches. The cartridges are the
owner's (Kirby's Dream Land, Super Mario Land, Looney Tunes in its 128KB DMG
and 1MB dual-mode releases) and blargg's `cpu_instrs` (`tools/getgbtest.py`).
None is committed. The contract is docs/REALDMG-SPEC.md sections 16.2.4 and
10.3.1.

## 1. The cause

kern_mega maps the first megabyte by identity, so a heap claim's segment S is
two selectors: S, a writable DATA segment, and `(S >> 2) | 4`, its CODE alias.
A far jump through S loads a data selector into CS - #GP, completed by
`mega_gpfix` - and a write through CS is #GP too, completed by `mega_gpw`.
Each costs an exception (~1,200 clocks on a 286), and RealDMG did both in its
hottest paths:

| what | how often | where |
|---|---|---|
| every entry into the core: `jmp far [ss:S_RESVEC]` and the I/O trips back through `S_RETFAR`, `S_DISP` | every event (one a scan line at least) and every register the core does not answer | rdgsys.inc `rdg_resume`; the vectors `rdg_init` stamped with `rdg_coreseg` |
| the recompiler entering a block, `jmp far [ss:S_JITOUT]`, with the cache's segment from the block table | every lookup | rdggen.py `tr_go`, `tr_lgo`, `tr_int` |
| every chain between two tiers, a `jmp far SEG:OFF` with the data segment in its bytes - and `tr_ljmp` compared CS (a code alias) with the table's data segment, so chains that should have been NEAR were FAR | every execution of a chained exit | translated code |
| the link patches, `mov [cs:bx],...` | every link made | rdggen.py `tr_ljmp`, `tr_lpatch`, `tr_rpatch` |
| `rdg_dvmm`, `rdg_bwst` through CS | a frame, or a changed line on VGA | rdgppu.inc |

**And inside the core a completed fault is also WRONG.** The core runs with
SS:SP the Game Boy's own stack, interrupts off so that nothing pushes there;
an exception at ring 0 pushes its frame there regardless - ~30 bytes of the
game's WRAM or HRAM below its SP, each time.

## 2. The trap census (Bochs 2.7, 16MB, `make megatrap`)

Every completed #GP is a line on E9 (`tools/megatraps.py`). Each cartridge
double-clicked from B:, 45 emulated seconds of its attract (no input); the
recompiler, the default CPU.

| cartridge | base | after | after: in the core or a cache |
|---|---:|---:|---:|
| GBDEMO.GB (ours, HALT-heavy), 30 s | 29,273 | 140 | 0 |
| cpu_instrs | 87,703 in its first 10 s, then **looped in test 01** | 96, **passes all eleven** | 0 |
| Kirby's Dream Land | 2,107,507 | 96 | 0 |
| Looney Tunes (128KB) | 999,120 | 96 | 0 |
| Super Mario Land | 792,724 | 96 | 0 |

The 96 left are the package's own once-a-session writes through CS
(`rdg_init`'s two words, the lossy picture's `rdg_lpatch`, ~90) and two of the
kernel's at the launch - all on the host's stack. cpu_instrs under the
INTERPRETER passed on the base build: its one trap a core entry, whose frame
landed below an SP test 01 never reads back.

## 3. The speed (86Box `286-mega`: the owner's mr286, 16 MHz, 4MB)

`tests/rdgcore/rdgwin.py --vm 286-mega` with `RDGWIN_A=build/mega.img`:
Kirby's Dream Land, Start twice, then 20 emulated seconds holding Right,
second by second against the Game Boy's 59.73 frames a second. 86Box is not
cycle-accurate (docs/86BOX.md); the ratio is the finding.

| build | % of real time | frames drawn a second | least second |
|---|---:|---:|---:|
| base, on upstream's 86Box | 101.5% | 32 | 90% |
| **base**, with `03-cs-exec-check.patch` | **35.9%** | **8** | 30% |
| after, shipped build | 101.2% | 30 | 84% |
| after, kern_mega's arm (10.3.1) | 101.1% | 28 | 79% |

**The first row is the instrument's defect and not the program's speed.**
86Box's `loadcsjmp`, `loadcscall` and `loadcs` test a descriptor's S bit and
then treat any non-system segment as code: a far jump through a DATA selector
loads CS and runs, where an 80286 (and Bochs) raises #GP. So the base build's
dominant fault cost nothing there, and 86Box said "full speed" about a build
that ran at a third of it. `tools/86box/patches/03-cs-exec-check.patch`
raises the #GP; with it, 86Box reproduces the report.

All three after-rows are at the Game Boy's own rate, where the package's
speed limit holds them; the headroom under it is not measured here.

Looney Tunes (1MB, 64 banks), the same harness, Start three times then Right:

| build | % of real time | banks fetched in the 20 s |
|---|---:|---:|
| after, shipped build (the bank cache, 12 slots) | 99.5% | 6 |
| after, kern_mega's arm (in place) | 97.3% | 0 |

Both runs ended on a white transition screen and their seconds differ in
where it fell (the arm's last second 47%, the shipped build's 64%), so the
frames-drawn counts of that span (54 and 33 a second) are not a comparison
and are not quoted as one.

## 4. kern_mega's arm: the cartridge in place (REALDMG-SPEC 10.3.1)

Bochs, `make megatrap`, the arm's build (`build/megak/apps/realdmg.o88`), 40 s
each, read out of the package and its view:

| cartridge | banks | `[rw_bcache]` | ROMX now | banks fetched | second cache segment | table |
|---|---:|---|---|---:|---|---|
| Kirby's Dream Land | 16 | 2 (in place) | the run's window 5 (bank 6) | 0 | claimed | 64KB |
| Looney Tunes (1MB) | 64 | 2 | window 0 (bank 1) | 0 | claimed | 64KB |
| Super Mario Land | 4 | 2 | window 1 (bank 2) | 0 | claimed | 64KB |
| cpu_instrs | 4 | 2 | - | 0 | claimed | 64KB - **passes all eleven** |

The shipped build on the same machine holds Kirby's Dream Land in the bank
cache's 12 slots in conventional memory (86Box, section 3).

## 5. The static recompiler's branch

`realdmg-fullstatic` (`74f3f46b`) merges with this cleanly, and its
`tests/gbaot.py` passes on the merge - 15 ROMs and both negative controls - but
only after two changes the merge found: the code selectors' "0 means the data
segment" is read at every lookup (its host moves `S_JITSEGA` a round at a
time without `rt_init`), and the link patches take their data segment from
the state block and not from a word in each tier (its host builds code at one
place and loads it at another, so the word arrived stale and GBFLAGS' sum
went wrong). Its precompiled list's segments, and anything it far-jumps to,
want the CODE selector on kern_mega - a loader's job, `rdg_csel` the hook.
