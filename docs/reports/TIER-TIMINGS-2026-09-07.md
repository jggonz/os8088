# Tier timings, and the two recuts they drove

**A measurement, not a description.** Taken 2026-09-07 on a four-core cloud
container from a cold checkout - `nasm` 2.16.01, QEMU 8.2.2, MartyPC at the
pinned commit, SmallerC present, so all four capabilities (`nasm qemu marty
cc`) were live and no row skipped. Quote it with that box: guest cycle counts
are exact at any width, host wall clock is not.

The figures are of the tiers as they stood before `bc79df6`, and they are why
both gates were then recut - `fast` at `bc79df6`, `full` at `5466de6`. What
each tier is now FOR is `docs/WRITING-TESTS.md` 2.1 and 2.2; this file is the
evidence behind those two sections and is not maintained against later trees.

## What the two gates cost, before and after

| | rows | wall | work | budget |
|---|---|---|---|---|
| `fast` before | 55 | 17.3s | 62.7s | 30s |
| `fast` after | 26 | **8.3s** | 23.6s | 30s |
| `full` before | 68 | 350.5s cold | 452.9s | 600s |
| `full` after | 31 | **75.3s cold, 50.3s warm** | 70.2s | **180s** |

`work` is each row timed alone and summed; `wall` is the runner's, at four
host lanes and three emulator lanes. `full` cold pays for `small128`'s private
`kern_small` tree and warm does not; at `--marty-jobs 4` the warm figure is
50.0s. The declared sums moved with the rows: `fast` 85.3s -> 25.8s, and
`full` 658s -> 123.7s, the latter having quietly stood ABOVE its own 600s
ceiling and passing only because the ceiling is enforced on wall clock.

**Neither tier writes anything into the checkout.** A full file inventory of
the repository taken before and after `fast` is empty, which is exactly its
contract. What both leave is under `build/` or in `/tmp`; see the last section.

## fast - 26 rows, 8.3s

Times are the median of three standalone runs, so they are each row's own cost
with nothing else on the box. `decl` is what `tests/suite.py` declares - four
were stale and were corrected in the same commit (`stkclass` declared 12.0 and
takes 3.9).

| row | measured | decl | what it defends |
|---|---|---|---|
| `mirror` | 4.362s | 4.5s | Every constant written down in two files agrees in both. There is no linker here to notice. <br>*307 checks* |
| `stkclass` | 3.930s | 5.0s | Every package's declared stack class covers its worker's deepest chain plus the 64-byte interrupt floor, read out of the built `.o88`'s header byte. <br>*16 workers · thinnest 1.33× (80+64 in 192)* |
| `api-abi` | 3.311s | 3.3s | The API jump table decoded out of `kernel.bin` and compared with the SDK. <br>*677 checks · 159 slots, 41 aliased, 3 compat* |
| `stkapps` | 2.075s | 2.1s | Every `ret` in every shipped package and driver reached at the depth it started at — the TCP/IP stack included. <br>*9,038 entries walked* |
| `asmrules` | 2.001s | 2.0s | Unreachable code after an unconditional jump, a prologue restored in the wrong order, `cpu 8086` reachable from every root, an unreachable local block. <br>*39 checks* |
| `checkdocs` | 1.619s | 1.6s | Stale SPEC.md citations and API slot numbers in prose — a stale slot is usually still a *valid* slot, just a different call. <br>*2,101 headings · 158 slots · 0 problems* |
| `ovlchk` | 1.573s | 1.4s | No near call crosses a section boundary; every blob entry ends in `retf`; no far tail jump enters a `retf` body without a far frame. <br>*1 package keeps every overlay call far* |
| `pkgdeps` | 1.280s | 1.4s | Every `%include` a package pulls in is a prerequisite of its `.bin` rule — found by an A/B that measured zero because nothing reassembled. <br>*93 package rules* |
| `stkbalance` | 0.831s | 0.9s | Every `ret` in the kernel and in SHEET, CHART and their shared includes reached at its starting depth. Path-aware, because a naive push/pop count flags one routine in ten. <br>*4,231 entries · 0 unbalanced* |
| `textrules` | 0.668s | 0.7s | The transparent-text ratchet: every `font_char`/`font_str` site registered with a reason, and the count may only go down. <br>*40 checks · 59 sites in 20 files* |
| `stkwalker` | 0.595s | 0.6s | The stack walker itself — 11 idioms it must stay quiet about and 6 defect shapes it must catch. <br>*22 checks* |
| `diskverify` | 0.384s | 0.5s | The tree's own fsck, pointed at the images `make` ships and otherwise never run on. <br>*18 checks · 9 volumes* |
| `machines` | 0.226s | 0.3s | No row names a machine whose ROM this tree has not got, and each GLaBIOS twin still differs from its IBM original in `rom_set` alone. <br>*14 machines · 4 twin pairs* |
| `registry` | 0.150s | 0.2s | Every test in `tests/` is registered in a tier or says why not — the row that stops this suite going back to a directory nobody can enumerate. <br>*333 files · 300 registered · 33 exempted* |
| `docindex` | 0.145s | 0.2s | `docs/INDEX.md` still regenerates byte-identically. An index that has drifted is worse than none, because it is consulted and believed. <br>*0 diffs* |
| `layout` | 0.074s | 0.1s | A guest address is not a file offset. Stage 2 sits in front of `.text` in `kernel.bin`, so a host-side reader indexing by symbol lands 6,656 bytes early — on real code, silently. <br>*6 checks* |
| `pkg` | 0.060s | 0.1s | Package, driver and module headers, and every file on every image proved identical to the artifact it was built from. <br>*743 checks* |
| `qemuown` | 0.055s | 0.1s | Every test that launches a QEMU registers a teardown for it. <br>*14 launchers, 14 own theirs* |
| `fixtures` | 0.043s | 0.1s | A row's scratch floppy is a build product — a bare `not os.path.exists` boots whatever `build/` held that minute. <br>*276 checks* |
| `drvovl` | 0.042s | 0.1s | A driver-loaded overlay may not be compressed. It reads the drivers' own source for the names they load, so a third overlay is covered the day it is written. <br>*5 checks* |
| `swallow` | 0.036s | 0.1s | A statement that ended up inside a block comment: compiles clean, runs never. It cost `apps/c64` a Paste that outlived a machine reset. <br>*1 check* |
| `fonts` | 0.034s | 0.1s | The typefaces are in `SYSTEM/FONTS` on every shipped system image and nowhere else, and the SDK spells the same two components. <br>*64 checks* |
| `canary` | 0.032s | 0.1s | The boot canary's offset re-derived from every image's own BPB: it must name a sector a transfer run reads *after* the head boundary. <br>*offset 6656 · file sector 21 · 4 geometries* |
| `kernbudget` | 0.028s | 0.1s | The blessed baseline in `docs/KERNEL-MEMORY.md` carries *this* kernel's `KERN_BUDGET`, and it prints `KERN_BUDGET big <n>, small <n>` on every build. <br>*4 checks - the fast rule's one stated exception* |
| `image` | 0.026s | 0.1s | The shipped floppies read by an independent FAT12 walker — contiguity, the standard BPB, the attribute rules. <br>*4,634 checks · 9 volumes* |
| `checkreadme` | 0.014s | 0.1s | README.TXT's width and size rules — Note Pad refuses a file one byte too long and shows nothing at all. <br>*316 lines · widest 28 · 80 bytes spare* |

Total assertions reported by the shared harness across the tier: 6,919 - of
which `image` alone is 4,634, nine shipped volumes walked by an independent
FAT12 reader in 26 milliseconds.

### The 29 that left fast

`fast` is the one tier nobody opts into, so its cost lands on the contributor
who is NOT working on its subject. Two questions retire a row, either one on
its own: **is it about one package or one driver**, and **can only a kernel
change break it**. Nothing was deleted - every row below still runs, still
fails the same way, and is one `-k` away.

| row | was costing | rule | why |
|---|---|---|---|
| `lmpack` | 5.982s | one package or driver | WEAVE and LOOM are two packages; six seconds of every build to prove their compilers agree |
| `lowwin` | 5.153s | kernel-internal | `.lowbss`'s order is one include line inside the kernel |
| `lzfmt` | 4.710s | kernel-internal | the LZ codec is one subsystem no package can reach, and `lzfmt-all` beside it was already soak |
| `resident` | 3.589s | kernel-internal | the splash's reach into the epilogue ladder |
| `bootfloor` | 3.399s | kernel-internal | `HEAP_PARA` and the ladder are stage 1's; nothing outside the kernel moves either |
| `mlen` | 3.358s | kernel-internal | `clk_mlen`'s month mask — only a clock change reaches it |
| `bsssentinel` | 3.330s | kernel-internal | only a kernel writer can put a byte in the kernel's `.bss` |
| `vbrseg` | 3.245s | kernel-internal | the volume boot record's segments |
| `invariants` | 1.235s | kernel-internal | three facts about who writes a *kernel* byte |
| `appsmall` | 1.191s | a build configuration | a build CONFIGURATION `all` never builds — `full`'s job, and `fast` may not build |
| `frinset` | 0.846s | one package or driver | FRACTAL is one package — `soak -k 'fr*'` |
| `frcycle` | 0.549s | one package or driver | FRACTAL is one package — `soak -k 'fr*'` |
| `csworld` | 0.524s | one package or driver | CLEAR SKIES is one package — `soak -k 'cs*'` |
| `csworlds` | 0.454s | one package or driver | CLEAR SKIES is one package — `soak -k 'cs*'` |
| `stknosave` | 0.282s | one package or driver | every marker it checks is ETHER.DRV's own |
| `sfx` | 0.280s | one package or driver | one artifact of one tool; no other build reaches it |
| `frstepv` | 0.206s | one package or driver | FRACTAL is one package — `soak -k 'fr*'` |
| `dsegaudit` | 0.162s | kernel-internal | `[dsk_dseg]`'s reach is inside the kernel's disk layer |
| `csart` | 0.158s | one package or driver | CLEAR SKIES is one package — `soak -k 'cs*'` |
| `drvclaim` | 0.130s | kernel-internal | can only fire when a SECOND driver gains a service task |
| `wakedrain` | 0.102s | kernel-internal | an `evq_pop` site is kernel code |
| `drvmem` | 0.038s | one package or driver | one PAGE of one application against per-driver constants |
| `wab` | 0.038s | one package or driver | the `.WAB` format is the Weave family's |
| `inktab` | 0.032s | one package or driver | PAINT's half of the mirror — beside a PAINT or `gfx_inktab` change |
| `ktags` | 0.029s | kernel-internal | an owner tag is a kernel constant |
| `dirwsize` | 0.025s | kernel-internal | the directory cache's arithmetic |
| `pgrank` | 0.021s | kernel-internal | the eviction order is the memory manager's own |
| `assocpage` | 0.021s | kernel-internal | the association layer's generator; `assocglyph` was already soak |
| `blobruns` | 0.016s | kernel-internal | the blob's shape is stage 1's and the BPB's |

Two of those are worth naming as judgement calls. `drvclaim` is a real rule
about driver code, but it can only fire when a SECOND driver gains a service
task, which is not a build-to-build event. `lmpack` and `wab` are the Weave
family's, and `docs/WEAVE-SPEC.md` had them registered as fast rows on
purpose; that document now says `soak`, and its claim that `make` runs
`lmpack` every time is corrected there and in `CLAUDE.md`.

`appsmall` went to `full` in that commit and on to `soak` in the next.
`kernbudget` is in the table ABOVE rather than this one: rule 2 moved it out
and the owner put it back, and it is the rule's one stated exception - 28ms,
and the figure it prints is how kernel size drift stays visible between one
person's commits and the next.

## full - 5 rows, 50.3s warm

One question: **did you obviously break the OS?** Does it compile, does it
boot, does it do the basic things, is anything critical gone. The 180s budget
is a target for four lanes on an ordinary box, not a promise - this container
uses a third of it.

| row | warm | cold | needs | answers |
|---|---|---|---|---|
| `bootsmoke` | 12.1s | 13.1s | `marty` | **Does it boot?** Does it still reach a desktop on both 1bpp adapters — boot sector, FAT12, the `int 13h` splitter, adapter detection, the heap ladder, `drv_boot` and the first paint. The widest reach per second in the suite. <br>*12 checks · boots 6.9 s and 6.2 s* |
| `kernresident` | 12.6s | 13.7s | `marty` | **Does it boot on VGA — and is anything critical gone?** `kern_big` fully *resides* in 128 KB at a bare desktop — the half of the memory rule an assembler cannot see, because a claim made at boot and never given back is a fact about a running machine. <br>*span ends 113,152 · 17,920 B spare* |
| `small128` | 16.1s | 35.4s | `marty` | **Does the second shipped kernel compile and boot?** …and it reaches that desktop on a machine with 128 KB *in it*. Walks `mem_tab` and fails if any pinned claim stands on a bare desktop. <br>*48.5 KB usable · 0 pinned* |
| `ps2mouse` | 7.4s | 9.7s | `qemu` | **Do the basic things still work?** Does the PS/2 mouse reach the pointer, and does the *keyboard* survive the handshake — both halves of the probe are a chance to take a byte from `int 09h`. <br>*`p2st` 9 · port 04 · pointer exact on 200,150* |
| `ctoolchain` | 6.5s | 6.8s | `cc` | **Does it compile?** The C toolchain still produces a package. It had a capability with no row behind it while no C package assembled for two releases. <br>*19 checks · 4 packages from clean* |

`ctoolchain` is the carve-out working as intended: a row may DRIVE an app as
the vehicle for a generic check - it builds four C packages because that is
what a toolchain produces - and its subject is the toolchain, not any of them.

### The 9 that left full

| row | was costing | why it is not a full row |
|---|---|---|
| `buildmatrix` | 142.8s | 99 knob configurations are *instruments*, not the OS — and 143 s is four fifths of the whole new budget on its own |
| `smallboot` | 118.0s | `small128` beside it already builds this kernel and boots it; what this adds is the three-adapter sweep, at 118 s |
| `weavesmoke` | 72.8s | WEAVE is a package — `soak -k 'weave*'` is twelve rows including `weavepack`, the family's actual gate |
| `martyconc` | 14.7s | it gates the emulator *harness*, so it cannot answer whether the OS is broken |
| `bmshare` | 9.9s | about `buildmatrix`'s own build-speed variables — it follows that row down |
| `kernmods` | 9.8s | gates `kernsize.py`'s reporting pass: an instrument, not the OS |
| `vmmouse` | 8.7s | a browser-only pointer on a *third* kernel — and it dragged a whole `kern_emu` build into the prebuild |
| `stackprose` | 4.4s | a prose gate: a stale comment misleads a reader, it does not break the OS |
| `appsmall` | 1.2s | five named packages' small build arm — the same family as `buildmatrix` |

**What that costs, stated rather than discovered later.** The 99 knob `%ifdef`
arms assemble at `soak` cadence now rather than at every integration merge,
and `kern_emu` is built by NO tier at all - `make emu` and `soak -k 'vmmouse'`
build it. `kern_small` is unaffected: `small128` builds it in a private tree on
every run, which is why its cold figure is 35.4s against 16.1s warm and why its
declared `secs` went 20 -> 40 in the same commit.

## The rows that are really harnesses

Seventeen rows are not one assertion about the tree - they drive builds, they
drive the test machinery itself, or they run one comparison over a whole
corpus. Eleven have since moved to `soak`; the numbers below are what they
cost wherever they now run.

**Build drivers.** `buildmatrix` is 99 knob builds plus `kern_small` and
`kern_emu`, each into its own `build/bm-<name>/` and deleted the moment it
finishes; 97 of the 99 shared the default build's packages through `ICODIR=`
and two built their own because their knob reaches a package. `bmshare` builds
one knob kernel BOTH ways and byte-compares the images. `ctoolchain` builds
four C packages from clean plus a negative test - a call to a thunk nobody
wrote must still stop the build and name the symbol.

**Harness gates.** `martyconc` launches eight emulators and boots two, proving
separate ports, directories, disks and memories, and that a second client on
one instance is refused in 0.00s naming the holder. `stkwalker` tests the stack
walker three other rows depend on, against 11 idioms it must stay quiet about
and 6 defect shapes it must catch. `registry` reads 333 files in `tests/` -
300 registered, 33 exempted, 3 flagged as building. `qemuown` finds 14 QEMU
launchers, 14 of which own their instance. `machines` finds 14 machines named
by tests and 4 IBM/GLaBIOS twin pairs.

**Corpus walkers.** `image` is 4,634 assertions over nine volumes in 26ms.
`wab` is 3,468 over the demo bundles, by a second independently written reader
of the format. `lmpack` packs 7 projects with both packers and compares them
byte for byte, then runs 44 malformed cases and requires the same sentence from
each. `stkapps` walks 9,038 entries; `stkbalance` 4,231 with zero unbalanced;
`stkclass` measures 16 workers, thinnest 1.33x.

## What the tiers leave on disk

Measured as a full file listing of the checkout and of `/tmp`, before and
after each tier.

**`fast` writes nothing into the repository.** The repo-side diff is empty.
What it leaves is scratch in `/tmp`, and it is NOT cleaned up: one
`/tmp/os88sym<rand>/` per kernel re-assembly (~1.0 MB each, four in a parallel
run), `/tmp/appsmall.<rand>/` with eleven package binaries, and
`/tmp/tmp<rand>/a.{asm,bin,lst,map}` from every row that re-assembles to read a
symbol - the `.lst` listings are the bulk, one of them 5.7 MB. This session's
`/tmp` reached 97 MB across a handful of runs. Fewer rows re-assemble since the
recut, so this is smaller now, but nothing deletes it.

**`full` after the recut** leaves one private build tree
(`build/trees/plain-<hash>/`, 3.2 MB - `small128`'s `make small` plus
`apps360.img`) and four MartyPC instance directories (180 KB, ~45 KB each,
holding `instance.json`, `debug.port`, `martypc.log`, a cycle trace and their
own floppy). Instance directories ACCUMULATE across runs. `ctoolchain` rewrites
its four C packages in the shared `build/` each run, which is why it carries
`builds=True` and takes the tree to itself.

Before the recut the same run left FOUR private trees totalling 15.2 MB
(`buildmatrix` owned two, `weavesmoke` a 7.5 MB one), seventeen instance
directories, `build/emuk/` and `build/vmmouse.*`, and created and destroyed 99
`build/bm-*` trees and two `build/bms-*` on the way. No QEMU pidfile or socket
survives either run, which is what `qemuown` keeps true.

## Findings that drove a change

1. **`docs/TESTING.md`'s tier table was stale in three places** - it said 48
   fast rows, an 84-row knob matrix and 229 soak gates, against a registry
   holding 55, 99 and 263. Corrected.
2. **`full` declared 658s against its own 600s budget** and passed only
   because the ceiling is enforced on wall clock and three lanes absorbed the
   difference. A two-core box would have failed the budget with every row
   green.
3. **Four `secs` declarations nobody had re-measured** on rows that stayed in
   `fast`, the worst being `stkclass` at 12.0 declared against 3.9 measured.
4. **Over half of `fast` was somebody else's subject** - 12 rows about one
   package or driver and 17 about a kernel internal no package can reach,
   39.1s of the tier's 62.7s. That is the finding the whole recut came from.

## Reproducing it

```
python3 tools/os88test.py fast          # 26 rows
python3 tools/os88test.py full          # 31 rows, cumulative
python3 tools/os88test.py --list        # every row, its tier and its why
```

Per-row times here were taken by running each row's command directly, three
times, and taking the median - the runner's own per-row figures are under
contention and read a little higher. `tools/os88build.py clean` before a `full`
run is what makes it cold.
