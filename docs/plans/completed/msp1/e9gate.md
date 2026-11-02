# msp1 follow-up "e9gate" - no debug-port output in the shipped kern_mega

kern_mega size pass 1 (BRIEF.md), follow-up step **e9gate**, branch
`msp1-e9gate`, cut from `mega-size-1` at `759da0e6` after the pass's four
agents merged. The owner's decision, verbatim: *"Debug port writes: These
should be gated just in principle, we shouldn't leave our debug lying around
the shipped kernel. We did a pass earlier to gate off all the debug toasts,
but I didn't think to ask about debug that wasn't a toast."*

The contract is KERN-MEGA-PLAN 14.127; this is the working record. Starting
inventory: the four agents' E9 SITES sections (pmcore.md, pmmem.md, shared.md;
pmfd.md's are in its DEFERRED), completed by a grep for `out 0xE9`, `0xE9`,
`MEGA_E9IO` and every caller of `mega_`/`mm_` `say`/`puts`/`putc`/`hex4`/
`hex2`/`putsf`, with a nesting-aware scan of the `%if` each sits under.

## BASE AND TIP

`tools/kernsize.py --build build/megak`, base `759da0e6` against the tip:

| kern_mega | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.mega` (+scratch) | `.boot2` | `.ovl` | `KERN_SIZE` at boot | resident after the switch |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| base | 46,298 | 5,435 | 45,979 | 6,655 | 336 | 18,057 (+1,497) | 6,620 | 5,109 | 130,560 | 64,512 |
| tip | 46,226 | 5,435 | 45,945 | 6,655 | 336 | 16,988 (+1,494) | 6,567 | 5,109 | 129,024 | 64,000 |
| delta | **-72** | 0 | **-34** | 0 | 0 | **-1,069** (-3) | -53 | 0 | -1,536 | **-512** |

* **Conventional memory for ever** (`.text`+`.bss`+`.lowbss`+`.vgabuf`):
  58,724 -> 58,652, **-72**; `os88megaseg`'s overhead over kern_big 3,167 ->
  **3,095**, and `tests/megaseg.txt` is ratcheted to it.
* **Above 1MB** (`.cold` + `.mega` + scratch): **-1,106**. `.mega` and its
  scratch together are -1,072; the split between them moved by a byte
  between two commits of this branch (16,987 + 1,495 at the kernel commit),
  the build number being in the image.
* **Rungs**: the image rung fell one step (52,224 with 491 left -> 51,712
  with 51 left), which is the resident figure's -512; `KERN_SIZE` at boot
  fell three 512-steps (the image, and the `.mega` rung 19,968 -> 18,944).
  Reported, not designed for.
* **kern_big and kern_small BYTE-IDENTICAL** (md5 of `kernel.bin` against
  the base tree built at the same commit count), and so are the
  **`MEGADIAG=1` and `MEGATRAP=1` kern_megas** - `build/megadiag/megak/
  kernel.bin` and `build/megatrap/megak/kernel.bin` md5-equal to the base
  tree's own `make megadiag` / `make megatrap`. Every gate wraps exactly the
  code that was there, so the diagnostic builds print every line they did.
  kern_emu assembles nothing of it (every site is inside `%ifdef KERN_MEGA`).

## THE GATE

`kernel.asm` defines **`MEGA_E9`** when `MEGA_DIAG` (MEGADIAG=1) or
`MEGA_TRAPLOG` (MEGATRAP=1) is defined. MEGATRAP is included because its
census is read in the context of these lines (`tools/megatraps.py`, the
census rows), and nine rows boot a MEGATRAP tree and read them. No new knob.

## E9 SITES AND THEIR FATE

Every `out 0xE9` the shipped kern_mega made. All are now `%ifdef MEGA_E9`.

| site (file, routine, section) | line it wrote | fate |
|---|---|---|
| `mega.inc` `mega_kwd` `.lost` (`.mega`) | `kern_mega: 8042 edge lost, 64=ss` | gated; the re-delivery stays. String `mega_mkwd` gated |
| `mega.inc` `mega_report` (`.mega`) | `kern_mega FAULT vector vv  stack: ...` + registers | gated; the GLASS line (`mega_fbuf`) and `[mega_infault]` stay - they are how the harness reads a fault now |
| `mega.inc` `mega_fkmsg_mk` (`.mega`) | `kern_mega CLOSED NAME vv at cccc:iiii err eeee` | gated; the toast (`mega_fkmsg`) stays. `mega_mclo`/`mega_mcle` gated |
| `mega.inc` `mega_svc` `.refl1` (`.mega`) | `a fixed-disk transfer went to the BIOS (int 13h reflected)`, once | the whole test gated (it existed only to say it), so a reflected `int 13h` no longer pays four compares; `mega_hdsaid` gated |
| `mega.inc` `mega_strcal` (`.mega`) | `kern_mega: string restart r.... p....` | the print gated; the MEASUREMENT (`mega_sdr`/`mega_sdp`, read by `mega_gpfix`) stays |
| `mega.inc` `mega_drvsayf` (`.mega`) + `driver.inc` call | `kern_mega: above 1MB: NAME` | routine and call site gated (the `mov es` before the call stays: ES is what the caller is left with) |
| `mega.inc` `mega_modsayf` (`.mega`) + `mod.inc` call | `kern_mega: above 1MB: NAME base` | routine and call site gated |
| `mega.inc` `mega_fd13` (`.mega`) | `native floppy X` once per drive; `floppy fell back to the BIOS at step ...` | both gated; `mega_fdsaid` gated |
| `mega.inc` `mega_fdjudge` (`.mega`) | `native floppy OFF - eight fallbacks in a row` | gated; the MEGADIAG toast inside it was already MEGADIAG; `[mega_fdoff]` stays |
| `mega.inc` `mega_gatef` `.refuse` (`.cold`) | `kern_mega: real mode: <reason>` | gated; the toast stays. `mega_mrm` gated |
| `mega.inc` `mega_pmtail` (`.mega`) | `kern_mega: protected mode` | gated; the `Entered protected mode` toast stays. **A bug in the first build of this edit, caught by the fast tier's `stkbalance`**: the replaced text took `push ds` with it, so the toast call's `pop es` popped the far return address. Fixed before the commit |
| `megados.inc` `mega_dosparkf` (`.mega`) | `kern_mega: parked kkkkK at pppppp sum ssss:ssss` | gated |
| `megados.inc` `mega_dwake` (`.mega`) | `kern_mega: unparked` | gated; `[mega_dwoke]` stays |
| `megados.inc` the hibernate wake (`.mega`) | `kern_mega: resumed` | gated |
| `megados.inc` `mega_wrprobe` (`.boot2`) | `kern_mega: a park survived the reset` (its own `out`) | gated |
| `mega.inc` `MEGA_E9IO mega_` / `mm_`, `mega_putsf` | the writers | gated: nothing in a shipped kern_mega can write E9 |
| `mega.inc` `mega_nmitell`, `mega_tlog`, `mx_cplog`, `mx_lcchk` | NMI lines, MEGATRAP's `T` lines, compactor log | already `MEGA_DIAG` / `MEGA_TRAPLOG` - unchanged |

**Outside kern_mega**: no kernel, driver, `kerndos/`, `dostools/` or `apps/`
source writes port E9h (`0xE9` in `apps/` is opcode data: a JMP's byte, an
SBC immediate, a glyph). The only other writers are TEST packages under
`tests/` (MEGARUN, MEGARSQ, MEGAHUGE, the dostrap probes, the x3 benches) -
not shipped, and their lines are what their rows read. Nothing to list for
the coordinator.

## READERS AND THEIR FATE

The harness (`tools/os88bochs.py`, `Box`) reads the same facts out of guest
memory. Every read first checks a `.text` constant (`mega_tmem`'s toast
text) at its address, because in a DOS trip conventional memory is kern_dos's
(kern_dos loads at `KD_SEG` 60h, the kernel's own segment).

| fact | was | now (state) |
|---|---|---|
| the switch | `kern_mega: protected mode` / any `kern_mega:` line | `wait_pm`: `[mega_on]` (after Bochs has taken its first `c`) |
| a FATAL fault | `kern_mega FAULT` line | `fault()`: `[mega_infault]` and the glass line `mega_fbuf`; asked at most once a second, so `until` still ends on one |
| a package CLOSED | `kern_mega CLOSED` line | `faults()`: `[mega_fkn]` with `mega_fkmsg` (the LAST incident; no `err eeee`); `said()` = `lines()` + `faults()`; a Box that was asked reads it once more in `stop()` and answers that afterwards |
| a DOS trip's return | `kern_mega: unparked` | `arm_trip()` writes `[mega_dwoke]` 0, `wait_trip()` waits for 1 with the kernel home (the kernel itself zeroes it at the next trip's start and reads it only at that trip's returns) |
| the park's KB/base | `parked kkkkK at pppppp` | `park()`: `[mega_dikb]`, `[mega_dpbase]` |
| a resume | `kern_mega: resumed` | `wait_resumed()`: `[hb_resumes]` past 0, kernel home and switched |
| a module above 1MB | `above 1MB: NAME base` | `mod_base(name)`: its `mod_tab` row's MODR_SEG, through the LDT |

Per reader:

| reader | fate |
|---|---|
| `Box.up()`, the CLI's `boot` | (a) `wait_pm` |
| `Box.until` / `fault()` | (a) state; same contract |
| `sweep` / `_sweep_boot` | (a) polls `faults()` each pass; FAULT ends the boot as before; CLOSED lines kept in `faults.out` beside `e9.out` |
| `tests/megasweep.py` | (a) reads `faults.out` too |
| megapixel, megakbtrip, megarsq, megahddtick, megavidtier, megafont, megavidbig, megahuge, megarehome, megavidring, megaxmove, megapmmouse, megainst, megahma, megaflt, megaovl, megaown, megadosroom, megacaps, megafrotz, megap2hiber (+ the MEGATRAP rows below) | (a) readiness: `wait_pm` |
| brmega, lemmega, megafrotz, megahddtick, megainst, megamod, megapaint, megapixel, megasheet, megatbl, megatmmem, megavidbig, megavidring, megavidtier, x3mega | (a) FAULT/CLOSED comprehensions -> `faults()` |
| megacaps, megahuge, megaovl, megaown, megaxmove, megarsq, megapmmouse, megadosroom, megap2hiber, megafont, megarehome, megashed | (a) `lines = box.lines()` -> `box.said()` |
| megaflt | (a) polls `said()` while MEGARUN starts the builds five seconds apart and keeps each CLOSED toast it sees - so all six builds are still named with their vectors on the SHIPPED kernel. The `err eeee` field is gone from what it can print |
| megakbtrip | (a) `arm_trip` per trip, `wait_trip`; "never handed over" is now *kernel still home and the trip not back* |
| megahma | (a) `arm_trip`/`wait_trip`; the park line it only printed is `park()` |
| megadosroom | (a) `wait_resumed`; "the trip PARKED" is `trip_back()` after the session |
| megafont | (a) `wait_resumed`; `above 1MB: DESK.DRV` is `mod_base` |
| megashed | (a) `above 1MB: CTRL.DRV` is `mod_base` with the panel open |
| megap2hiber | (a) `wait_resumed`; HIBER.DRV's base for the breakpoint is `mod_base` at the off screen, where it read the E9 line |
| **megapiclose** | **(b)**: both PICLOSE trees now also `MEGADIAG=1`. The green leg's proof is `8042 edge lost`, and the watchdog keeps NO count to read instead. Its `unparked` and readiness reads stay E9 because the tree writes them |
| megadosp, megahiber, megasheet, megatrap0, megavidv2, pxmega, pxsmega, rdgmega, rdgaotmega | MEGATRAP=1 private trees: still write every line (MEGA_E9), so their E9 reads stand; their readiness and comprehension edits read state and agree |
| megacdrom, megacz, 86Box rows (`os88box faults`, `OS88_FAULTLOG`) | never read E9 - untouched. **86Box's fault log is the emulator's own patch, independent of E9** |
| MegaBochs-ported rows (`tools/os88megabochs.py`) | go through `Box.up()`; nothing else read E9 |
| `tools/megatraps.py` | MEGATRAP lines only - untouched |
| docs/BOCHS.md recipes | rewritten: `boot` returns once switched; E9's kernel lines are a MEGADIAG/MEGATRAP tree's; the new `Box` methods tabled; the restart proof is the boot toast |
| tests/suite.py | three descriptions said `kern_mega: resumed` / E9's `edge lost`; reworded |

No assertion was dropped.

## ROWS RUN

After the last code commit, once, scoped, Bochs,
`python3 tools/os88test.py soak -k ... --marty-jobs 1` over every row whose
reader changed plus three `Box.up()` port rows (mega-autostart,
mega-stikboot, mega-ringmove-tracker): **39 passed, 4 failed, 1 skipped**
(1,031.7 s).

* `megapixel`, `megapixelship` FAILED on THIS branch: they ask for faults
  once more after `box.stop()`, which raised `ProcessLookupError` from the
  debugger. Fixed (`e89dcb94`: `stop()` takes the last reading for a Box
  that has been asked) and re-run: **2/2 ok**.
* `x3mega` FAILS **at the base too** - `os88bisect classify --base 759da0e6
  x3mega`: BAD at the tip and at 759da0e6, same legs. The fault is
  `kern_mega FAULT 06 err 0000 at 10D4:0008` / `0D ... 10D4:02A7` (read off
  STATE here), the SOUND.DRV family below.
* `megamod` FAILS, known at the base (pmmem.md): every SOUND.DRV leg, now
  read off STATE as `kern_mega FAULT 0D err 0000 at 10D4:089A` (one `06 at
  10D4:0008`) - the same IPs 087F-089A as the base's CS 1154h; the
  selector moved with the smaller kernel.
* `rdgaotmega` SKIP (needs unicorn).

Everything else ok: brmega, lemmega, megacaps, megadosp, megadosroom,
megaflt, megafont, megafrotz, megahddtick, megahiber, megahibfull, megahma,
megahuge, megainst, megakbtrip, megaovl, megaown, megap2hiber, megapaint,
megapiclose, megapmmouse, megarehome, megarsq, megashed, megasheet,
megasweep, megatbl, megatmmem, megavidbig, megavidring, megavidtier,
megavidv2, megaxmove, pxmega, pxsmega, rdgmega, mega-autostart,
mega-stikboot, mega-ringmove-tracker.

**Broken on purpose**: `megaflt`'s own failing case - a `ret` at the top of
`mega_fkill` - on the SHIPPED kernel, which writes no E9 line at all now:
the row goes red with `FATAL: kern_mega FAULT 06 err 0000 at 1144:0076 sp
1BC5` read out of state, and nothing after FLTA starts. Reverted. And one
boot each of `build/` and `build/megadiag/`: both `pm()` True and
`faults()` empty; E9's kernel lines are `[]` on the shipped kernel and
`string restart`, `protected mode`, `above 1MB: Hard Drive` on MEGADIAG=1.

Host gates at each commit: `make -j2 mega`, `make -j2` (fast tier 66/66),
`make -j2 megadiag`, `make -j2 megatrap`, `python3 tools/checkdocs.py`.

## WANTS THE OWNER

* **The fatal-fault E9 dump is gone from the shipped build** - I concluded it
  need not stay: the glass line carries vector, error, CS:IP and SP, the
  harness reads it from `mega_fbuf`, and `mega_report` halts with it there.
  What is lost on a shipped kern_mega under Bochs is the sixteen stack words
  and the ten registers the dump added; a MEGADIAG=1 or MEGATRAP=1 tree has
  them. Say if a shipped Bochs run should keep that dump.
* **MEGATRAP=1 also turns MEGA_E9 on.** The alternative (MEGADIAG alone)
  would make the census log lose its FAULT/CLOSED/park context and nine rows
  read state where they read lines; the cost of including it is zero bytes
  on the shipped build either way.

## DEFERRED

* A count in `mega_kwd` (`inc word` of a `.lowbss` word, ~4 bytes `.mega`
  + 2) would let `megapiclose` drop its MEGADIAG=1 trees and test the
  shipped watchdog. Not taken: it spends shipped bytes for a test.
* `driver.inc`'s `mov es, [bx+DRVR_SEG]` before the gated call (4 bytes,
  `.cold`) is probably dead on the shipped build, but ES is what the caller
  is left with and no caller was audited.
* Docstrings of several rows still describe their evidence as "a `kern_mega
  FAULT` line"; the fact is the same, read from state.
