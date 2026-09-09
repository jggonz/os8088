"""The test registry - what runs, in which tier, and why.

`tools/os88test.py` reads this and nothing else.  Adding a test is adding a
row here; a test that is not in this file does not run in any tier, which is
the state all ninety of them were in before it existed.

**BEFORE ADDING ONE: docs/WRITING-TESTS.md.**  This file is the registry's
own contract - the tiers, and what earns a `full` row - and that one is how to
write the test the row points at.  The four things it exists to stop are all
visible in this file's own history: a `secs` nobody measured (the compression
family declared 2,721 seconds for rows that take 701), a `builds=True` where a
`wants=` or a private tree was the answer, a hand-rolled click at a remembered
coordinate, and a `time.sleep` that hands the guest a third less work under
load.  Its checklist is thirteen lines and takes a minute.

THE THREE TIERS.

  fast   Budget 30s. Host-side only - it reads what `make` just built and
         checks the invariants that break SILENTLY. Hangs off the default
         build, so it cannot be skipped.

  full   Budget 3 minutes. THE PRE-MERGE GATE, and it asks ONE question:
         DID YOU OBVIOUSLY BREAK THE OS?  Does it compile, does it boot,
         does it do the basic things, is anything critical gone.

  soak   No budget. Everything else - the rest of `tests/`, which is a great
         deal and is where the deep single-subject gates live.

WHY `full` IS CURATED AND NOT "ALL OF THEM", which is the thing to understand
before adding a row to it.  Measured on a cycle-accurate 5150 in a container:
a MartyPC boot to a settled desktop is **7.8 seconds**, and the emulator tests
in `tests/` run **40-75 seconds each** because each one boots its own machine
and then drives a session through it.  Instances are isolated now, so
`--marty-jobs` runs emulator rows side by side - but the lane is CORES-1 wide
and the box is four cores, so the arithmetic barely moves.

So THREE minutes is about four such rows, not fifty.  That is not a limitation
to be engineered away - it is what the machine costs - and the honest response
is to say which four and put the rest in `soak` where they are still one
command away (`os88test.py soak -k disp*`).  The runner FAILS the tier when it
overruns, so this stays true as rows are added rather than drifting until the
suite is too slow to run.  The 180s is a target for FOUR LANES on an ordinary
box; a slower one is expected to take longer, and today's tier leaves room for
that (60.3s in the runner and 75.3s of wall on a cold four-core container,
37.8s / 51.1s warm).

WHAT EARNS A `full` ROW.  ONE QUESTION: DID YOU OBVIOUSLY BREAK THE OS?  Does
it compile, does it boot, does it do the basic things, is anything critical
gone.  `bootsmoke` is the model: thirteen seconds for a boot to a desktop on
both 1bpp adapters, exercising the boot sector, FAT12, the `int 13h` splitter,
adapter detection, the heap ladder, `drv_boot` and the first paint - so it
fails for almost any serious regression, wherever it was.

Three things follow, and each one retired a row when the tier was recut:

  NOTHING APP-SPECIFIC.  A package is not the OS.  A row may DRIVE an app as
  the vehicle for a generic check - `ctoolchain` builds four C packages
  because that is what a toolchain produces - but a row whose SUBJECT is one
  program belongs in `soak`.  That is what moved `weavesmoke` (72.8s) and
  `appsmall` out.

  A KERNEL CHECK IS ALLOWED, AND SHOULD BE SHORT.  `kernresident` boots a VGA
  machine and walks `mem_tab` in 13.7s; that is the shape.  A deep sweep of
  one subsystem is not, however true it is: `smallboot` walked three adapters
  for 118s where `small128` beside it already builds that kernel and boots it.

  THE SUBJECT IS THE OS, NOT THE TREE AND NOT THE SUITE.  `buildmatrix`'s 99
  knob configurations are instruments, `bmshare` and `kernmods` are about
  build-speed variables and a size report, `martyconc` gates the emulator
  harness and `stackprose` reads prose.  Every one is worth having; none of
  them can answer this tier's question, and `buildmatrix` alone was 143s of
  a 180s budget.

A row that can only fail for one narrowly-scoped reason belongs in `soak`,
next to the change that would break it.  docs/WRITING-TESTS.md section 2.2 is
this rule written for somebody adding a row rather than moving one.

WHAT EARNS A `fast` ROW, which is the harder question and the one this list
got wrong for a long time.  `fast` is the only tier NOBODY OPTS INTO - `all`
depends on it - so every second of it is charged to the contributor who is
NOT working on its subject and has never read the code it defends.  The
question is therefore never "is this check worth having"; every row in
`tests/` is.  It is **"is it worth having to somebody who did not touch
this?"**

Two questions retire a row from `fast`, and either one on its own is enough:

  1. IS IT ABOUT ONE PACKAGE OR ONE DRIVER?  Then it is `soak`.  Whoever
     changes SKIES tests SKIES; charging every other contributor two seconds
     a build for it buys them nothing, and `soak -k 'cs*'` is one command
     run by the person it is for.

  2. CAN ONLY A KERNEL CHANGE BREAK IT?  Then it is `soak` too.  A row about
     how the kernel works INSIDE - a .bss sentinel, `.lowbss`'s order, the
     eviction ranks, `clk_mlen`'s mask - is defended by whoever edits that
     subsystem, and that is exactly the person who will run `soak -k` on it.
     `fast` is not where the kernel is proved to still work.  It is where a
     writer who has never opened that subsystem is caught breaking it by
     accident.  THE EXCEPTION IS WHAT MAKES THE RULE USEFUL: a kernel-side
     row stays if code OUTSIDE the kernel can reach it - a package, a
     driver, the SDK, or a Makefile recipe.  `api-abi` and `drvovl` are both
     kernel-side and both stay, because the other end of each is somebody
     else's file.

     AND ONE ROW STAYS FOR WHAT IT PRINTS.  `kernbudget` is kernel-internal
     by any reading, costs 28ms, and puts `KERN_BUDGET big <n>, small <n>`
     on every build - which is how kernel size drift stays visible between
     one person's commits and the next.  A row may earn `fast` by what it
     puts on the SCREEN as well as by what it catches; the bound is that it
     must be effectively free, and the number must be one the project
     actually steers by.  It is the only one, and it is the owner's call.

What survives is four families, and a new row should be able to say which
one it is joining:

  THE BOUNDARY          the kernel and the code loaded onto it, edited by
                        different people, with neither side's build saying
                        so - api-abi, stkclass, drvovl, fonts, pkgdeps
  RULES OVER EVERY LINE  what any assembly in this tree must obey, kernel
                        and package alike - asmrules, ovlchk, textrules,
                        stkbalance, stkapps, swallow
  THE SHIPPED ARTIFACTS  the floppies themselves, which anybody adding a
                        file reaches - image, pkg, diskverify, canary,
                        checkreadme
  THE TREE AND ITS SUITE duplication, generated docs, and the gates'
                        own integrity - mirror, checkdocs, docindex,
                        registry, machines, qemuown, fixtures, layout,
                        stkwalker

The membership is whatever carries the tier `fast` below; the families are
how to argue about a new one.  docs/WRITING-TESTS.md section 2.1 is the same
rule written for somebody adding a row rather than moving one.
"""
import os


class Row:
    """One registered test."""

    __slots__ = ("name", "tier", "cmd", "secs", "needs", "serial", "why",
                 "timeout", "builds", "alone", "wants")

    def __init__(self, name, tier, cmd, secs, why, needs=(), serial=False,
                 timeout=None, builds=False, alone=False, wants=()):
        self.name, self.tier, self.cmd = name, tier, cmd
        self.secs, self.why = secs, why
        self.needs = tuple(needs)
        self.serial = serial
        # BUILDS: this row shells out to `make`, so it writes build/ and
        # cannot share the tree with anything - not with another builder, and
        # not with a row reading what it is halfway through rewriting. It is
        # the one thing that still forces a row to run ALONE now that
        # emulator instances are isolated (tools/os88test.py's --marty-jobs),
        # and `tests/unit/t_registry.py` checks the flag against the script
        # rather than trusting it: a row that gains a `make` and not the flag
        # would be a suite that fails one run in five for no visible reason.
        self.builds = builds
        # WANTS: build artefacts this row OPENS and `make all` does not
        # produce - `("build/pkgbig.img",)`. Paths, not make targets, because
        # the path is what the row actually opens and the runner can then ask
        # whether it is there; `make <path>` builds it, since every one of
        # them is a `$(BUILD)/x` rule.
        #
        # **THE POINT IS THAT THEY ARE BUILT BEFORE ANY ROW RUNS.** A row that
        # makes its own artefact mid-run rewrites build/ under every other row
        # reading it, and that is not a theory: the first full soak of this
        # work lost nine rows to a four-minute window opened by one row's
        # `make` (docs/plans/SOAK-PARALLEL.md 12). Declaring the artefact moves the
        # build to a moment when nothing else is running.
        #
        # It also ends the OTHER failure this caused, which reads as a broken
        # feature: eleven images under tests/ had a Makefile rule, no builder,
        # and a row that died on `FileNotFoundError` several frames from the
        # cause - mseg360, pkgbig and pkgfence did exactly that in that soak.
        self.wants = tuple(wants)
        # ALONE: this row's ANSWER needs the machine to itself, which is a
        # different claim from `builds` and was not sayable until now. A row
        # whose assertion is a RATE - frames a second, milliseconds a redraw -
        # cannot share four cores with two other guests: that is not a flaky
        # row, it is the wrong measurement. Neither can a row whose clicks are
        # paced by a HOST-timed settle, because how much guest time a settle
        # covers is then a property of the box (docs/plans/HANDOFF-SOAK-FINDINGS.md
        # B5).
        #
        # It used to be spelled by EXCLUDING those rows from the wide run and
        # taking them in a second one - `-x saverate -x deskbench ...`, written
        # into two handoffs and remembered by whoever read them. A property of
        # the row is the place for it: the runner keeps an `alone` row out of
        # the shared lane and runs it in the one-at-a-time lane of the SAME
        # run, so there is no second pass to forget.
        #
        # It is not `builds`. A builder cannot share the TREE; one of these
        # can, and only needs the CORES.
        self.alone = alone
        # A generous default: the point of the per-row timeout is to stop a
        # hung emulator eating the tier, not to police a slow machine.
        self.timeout = timeout or max(60, int(secs * 4) + 30)


def py(*a):
    return ["python3"] + list(a)


def _kernel_sources():
    """Every kernel source, ROOT-relative and sorted.

    Asserted non-empty on purpose: a gate reporting 0 findings because its file
    list came out empty is indistinguishable from a clean tree, and the whole
    point of docs/plans/completed/STKBALANCE-KERNEL.md's sensitivity work is that a quiet gate
    has to be quiet for a reason. The runner execs rows with cwd=ROOT, so these
    stay relative.
    """
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = sorted(os.path.relpath(os.path.join(root, "kernel", f), root)
                 for f in os.listdir(os.path.join(root, "kernel"))
                 if f.endswith(".inc"))
    assert len(src) >= 30, "kernel/*.inc came out as %d files" % len(src)
    return src + [os.path.join("kernel", "kernel.asm")]


# --------------------------------------------------------------------------
# fast - host-side, no emulator, no build. Runs on every `make`.
# --------------------------------------------------------------------------
FAST = [
    Row("pacman-maze", "fast", py("tests/unit/t_pacman.py"), 0.1,
        "the Atari maze has 260 reachable dots, bounded tunnel edges and complete sprites"),
    Row("blobruns", "soak", py("tests/unit/t_blobruns.py"), 0.1,
        "how many int 13h calls stage 1 spends on the blob, per geometry "
        "(SPEC.md 15.3.8.5) - the count is NOT a function of BOOT2_SECS "
        "alone, because a run is bounded by the track and KERNEL.SYS starts "
        "where each BPB puts the data area. 13 is the last sector that fits "
        "two calls on a 720KB disk, and the 14th costs a whole revolution to "
        "move one sector. "
        "SOAK and not fast: the blob's shape is stage 1's and the BPB's, "
        "which no package can reach - it belongs beside a boot or geometry "
        "change",
        needs=("nasm",)),
    Row("bootfloor", "soak", py("tests/unit/t_bootfloor.py"), 3.5,
        "stage 1's RAM floor against the kernel's own ladder (SPEC.md 2.7.1) "
        "- HEAP_PARA is INJECTED, so the two can disagree, and guard 5c used "
        "to reconcile them until stage 1 started testing the exact condition "
        "and the guard became `x > x + 160`. Also the FLAT_PAYLOAD clamp: a "
        "small diagnostic payload bounds below RELOC_ADJ, where the `sub` "
        "after the compare underflows and relocates the sector to the top of "
        "a 1MB machine that is not there. "
        "SOAK and not fast: HEAP_PARA and the ladder are kernel-internal "
        "and nothing outside the kernel can move either",
        needs=("nasm",)),
    Row("lowwin", "soak", py("tests/unit/t_lowwin.py"), 10.0,
        "the mount-owned window is the BOTTOM of .lowbss (SPEC.md 2.1.2), so "
        "that it and the FAT window under it are one contiguous 8,192-byte "
        "region dead for the whole of kmain. It is bought by one include line "
        "and nothing else would notice it sliding: no RAM moves, no address "
        "any code names changes, and the kernel boots either way - only "
        "stage C would find out, by writing the overlay over vid_rowtab. "
        "SOAK and not fast: .lowbss's order is one include line inside "
        "the kernel, and ten seconds of every `make` is a high price for a "
        "line only a kernel change touches",
        needs=("nasm",)),
    Row("api-abi", "fast", py("tests/unit/t_api_abi.py"), 3.3,
        "the API table decoded from kernel.bin and compared with the SDK - the "
        "silent merge collision CLAUDE.md asks to be checked by hand"),
    Row("stackprose", "soak", py("tests/unit/t_stackprose.py"), 10.0,
        "a doc or comment that names the task stack's SIZE names the one the kernel has. SCH_STACK has been 1,536, 512, 256 and 384; SPEC.md 2.1 and 20.6 rule 6 followed it every time and the forty-odd places CITING them did not. That is not a typo class - docs/UPSTREAM.md's stale 256 had a session report a worker-stack contract difference between this branch and `main` that had not existed since #112, and go looking for what to adapt. os88geom guards the copies a SCRIPT retyped; this guards the ones a HUMAN did."
        "SOAK rather than fast or full: a stale comment misleads a reader, "
        "it does not break a build - so it answers neither tier's question, "
        "and both are paid for by people it is not about",
        needs=()),
    Row("drvovl", "fast", py("tests/unit/t_drvovl.py"), 0.1,
        "SPEC.md 20.13/62.9.9: a driver-loaded OVERLAY may not be COMPRESSED. "
        "RAMPAGE.DRV and HDDTOOL.DRV are read by RAMDISK.DRV and HDD.DRV "
        "themselves, with OSAPI_FILE_READ - which expands a 'CZ' FILE and NOT "
        "a v4 driver container, the only thing that expands one being "
        "drv_expand inside drv_load. So a compressed overlay reaches its "
        "loader as its own compressed bytes, that loader's header check "
        "refuses it, and what the user sees is not a decode error: it is 'Ram "
        "Disk needs the system disk', with the driver loaded, its Control "
        "Panel cells published and every control on the page inert. THAT "
        "SHIPPED - the compression pass gave rampage.drv the $(OS88DRV) "
        "recipe, which carries $(PKGZARG), where hddtool.drv had always "
        "spelled the tool out without it. It cost the RAM disk to save 646 "
        "bytes of a 360KB disk, tests/rdup.py reported it as three UI "
        "failures, and the reason took a screenshot to see. The gate reads "
        "the DRIVERS' OWN SOURCE for the names they load, so a third overlay "
        "is covered the day it is written"),
    Row("lzfmt", "soak", py("tests/unit/t_lzfmt.py"), 4.0,
        "docs/plans/O88-COMPRESSION-PLAN.md wave 0: both compression formats "
        "round-trip. tools/os88lz.py is the REFERENCE and the kernel's "
        "decoders are the copy, so this is what makes that claim mean "
        "anything. The corpus is small and FIXED on purpose - an empty file, "
        "one byte, a file shorter than LZ4's 12-byte match limit, a long run "
        "and incompressible noise are each a place an end-of-block rule or a "
        "length field is got wrong, and not one of them occurs in a real "
        "package. It also asserts the IN-PLACE MARGIN the loader will "
        "reserve, which is the number that lets a compressed image be read "
        "into the top of its own region and expanded downwards with no "
        "second buffer - noise measures 17 bytes where every real package "
        "measures 2, because LZ4 EXPANDS data that does not compress. "
        "SOAK and not fast: the codec is one subsystem that no package "
        "can reach, nobody edits it build to build, and lzfmt-all beside it "
        "is already soak",
        needs=()),
    Row("lzfmt-all", "soak", ["python3", "tools/os88lz.py", "--selfcheck"], 12.0,
        "the same round trip over every binary the tree builds - packages, "
        "drivers and the kernel. SOAK and not fast: the fixed corpus above is "
        "what catches a format bug, this is what catches a bug that only some "
        "real file's byte pattern reaches, and it costs 4s"),
    Row("mirror", "fast", py("tests/unit/t_mirror.py"), 4.5,
        "a constant written down in two files must agree in both; there is no "
        "linker here to notice"),
    Row("artpath", "fast", py("tests/unit/t_artpath.py"), 0.1,
        "a row that opens a BUILD ARTEFACT must resolve it through "
        "os88build.at(), or it reads build/ while the soak is reading its own "
        "frozen tree (docs/plans/SOAK-PARALLEL.md 14.2). It works standalone - "
        "at() is the identity with $OS88_TREE unset - and fails only in a "
        "soak, an hour in, as a FileNotFoundError several frames from the "
        "cause. TEN rows failed one 368-row run this way and the count had "
        "grown every soak, because a `wants=` they already had looked like "
        "the answer: prebuild builds the artefact INTO THE TREE and the row "
        "then opens build/. FAST because it is a whole-suite invariant that "
        "costs a directory walk, and because the alternative to catching it "
        "here is catching it in ninety minutes"),
    Row("p2restore", "fast", py("tests/unit/t_p2restore.py"), 0.3,
        "SPEC.md 9.9.7: the PS/2 probe's FAILURE paths must put the 8042's"
        " command byte back UNDOCTORED. [mou_p2cmd0] is banked with bit 5"
        " forced set for mou_p2_off's sake - the aux clock on a PS/2"
        " controller and PC MODE on an AT one, which stops the 8042"
        " translating, so a field 286 typed a different character for every"
        " key. NOTHING IN THIS TREE CAN GATE IT AT RUNTIME: the probe never"
        " runs on an 8088, it SUCCEEDS on QEMU so no failure path is taken"
        " there, and QEMU does not model the translate bit either (measured -"
        " clearing it deliberately leaves ps2mouse fully green). So the"
        " invariant is asserted over the source instead",
        needs=()),
    Row("csair", "soak", py("tests/unit/t_csair.py"), 0.3,
        "SPEC.md 88.7.6.3: CLEAR SKIES' eight rects of lift and sink keep"
        " CS_LIFTCLR out of the circuit - which is where every OTHER test of"
        " this simulator flies, so a rect edged toward the runway would be"
        " found as a broken glide ratio three rows away. It measures the"
        " NEAREST CORNER and not the centre, because a rect 3,000 m out that"
        " reaches 900 m in is 2,100 m out. Also that there is both lift and"
        " sink to find, and that the swoop's ramp lands exactly on its far"
        " end - CS_SWOOPLO + CS_SWOOPT x CS_SWOOPD = CS_SWOOPHI. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`",
        needs=()),
    Row("csink", "soak", py("tests/unit/t_csink.py"), 0.3,
        "SPEC.md 88.4.4, 88.6.5, 88.13.9.1: CLEAR SKIES' three families of"
        " PARALLEL TABLE, each indexed by something declared somewhere else"
        " and each failing the same way - silently, on one adapter or one"
        " setting, long after the row that was forgotten. Six ink tables of"
        " CSI_NINK rows, so a new ink added to four of them does not draw in"
        " whatever byte follows the other two; every river far model in every"
        " world carrying CSI_RIVLINE, one left behind being a white river on"
        " a colour display; cs_set_at / _max / _best all CS_SETN long,"
        " where the trap is that BEST IS NOT MAX - the Mode byte's ceiling is"
        " CGA, so a 286 given the best of everything off the clamp table gets"
        " the worse of two displays. And every CSM_STACK LOD pair the same"
        " HEIGHT, because a far model that stands in for a full one at a"
        " different height makes the object CHANGE SIZE at the switch - the"
        " Eiffel's was 300 against 324 and popped 24 m as you flew at it,"
        " where every other pair in every world already agreed. Deleting one"
        " ink row, whitening one river and shortening one apex take it red on"
        " all three. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`",
        needs=()),
    Row("csplane", "soak", py("tests/unit/t_csplane.py"), 0.3,
        "SPEC.md 88.7.4: CLEAR SKIES' five plane records agree with their own"
        " drag. CSP_DRAGK is what decides where an aeroplane stops"
        " accelerating - every record's comment says 'balances THRUST at"
        " VMAX' - so a THRUST changed without re-deriving it moves the TOP"
        " SPEED instead, silently, and no flight test in the suite is long"
        " enough to notice: an A5 takes 42 seconds of guest time to reach 95"
        " knots. The row integrates the model's own drag at VMAX and at three"
        " quarters of it, holds the speeds in order (the sailplane's"
        " unreachable VROT exempt), and checks each record still declares its"
        " fields in CSP_ order, without which every value below a new row"
        " would be read off by one. It was written for the change the field"
        " asked for - a quarter more thrust in the A5 - and raising that"
        " thrust alone takes it red. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`",
        needs=()),
    Row("cssin", "soak", py("tests/unit/t_cssin.py"), 0.3,
        "SPEC.md 88.5.9: CLEAR SKIES' sine table is a QUARTER of the turn"
        " now, and nothing held it to its generator before it became one."
        " The row regenerates the 257 entries from 88.5's own snippet, then"
        " walks cs_sin's arithmetic - top ten bits, bit 8 reflects, bit 9"
        " negates - over all 1,024 indices of a full turn against sin"
        " itself. The ONE deliberate difference is asserted rather than"
        " tolerated: 270 degrees reads -32767 where the full table held"
        " -32768, and every other index must agree to the unit. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("cspanel", "soak", py("tests/unit/t_cspanel.py"), 1.0,
        "SPEC.md 88.9.5/88.9.8: the five CLEAR SKIES cockpits fit, on all"
        " three adapters. A panel is one drawing in TWO units that do not"
        " scale together - a window's width is CELLS and a cell is 8 device"
        " pixels, while its x is the 320-wide layout's, which Hercules"
        " doubles - so a layout that is tidy on CGA can overlap on Hercules"
        " and a test that looks at one adapter sees neither. No two windows"
        " overlap, no round instrument overlaps a window or another"
        " instrument, every window is wide enough for what is lettered into"
        " it and no more than four cells wider, and everything is inside the"
        " panel. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("paccman", "fast", py("tests/unit/t_paccman.py"), 0.3,
        "PACCMAN's generated arcade tables say what they claim to (SPEC.md "
        "91). apps/paccman/pmc_rom.c is the build's TRUTH - the reference is "
        "not vendored (CONTRIBUTING.md 6) and an ordinary build never reads "
        "it - so nothing else checks the 240 dots, the four pills, the two "
        "ghost-house doors, the open tunnel row, the table lengths or the "
        "pinned commit in its header. It also asserts that the prelude's "
        "MELODY is voice 1 at 539 then 1078 Hz and its bass voice 0 at 67, "
        "because the two have been the wrong way round once and BOTH "
        "orderings produce sound; and that pmcband.inc and paccman.c agree "
        "about the band's three sizes, which is a constant written down in "
        "two files with no linker here to notice. The byte-for-byte "
        "reproduction row SKIPS, naming the pin, without $PACMANC_SRC"),
    Row("csworld", "soak", py("tests/unit/t_csworld.py"), 2.0,
        "SPEC.md 88.6.3: no collidable building in any CLEAR SKIES world"
        " stands in that world's own water - every base footprint against"
        " every river polygon, edges and containment and not just corners."
        " Nine locations and eight worlds since 88.6.4, and it walks them all. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("csrad", "soak", py("tests/unit/t_csrad.py"), 2.0,
        "SPEC.md 88.5.11: no CLEAR SKIES model declares a CSM_RAD smaller"
        " than its own vertices need. Three things read that bound and"
        " cs_sizepx's comment has always said it must never be under the true"
        " radius - and it was, for 36 of 122 models, because every macro"
        " computed wx + wz + h/2 where the origin is the BASE. cs_projall"
        " trusts it to say an object is wholly in front of the near plane and"
        " cs_edge1 then draws each edge out of cs_sxv without testing cs_fv,"
        " so a vertex never projected this frame drew a line from whatever"
        " the last object left in its slot - unclipped, across the cockpit. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("csworlds", "soak", py("tests/unit/t_csworlds.py"), 2.0,
        "SPEC.md 88.6.4: every CLEAR SKIES world costs about what PARIS costs."
        " The 12 fps budget was measured on Paris alone (88.12), so a world"
        " written afterwards can miss it by a factor with nothing to say so -"
        " slowness is one of the three defects an emulator cannot show. It"
        " prices each world's PEAK frame the way the renderer does and holds"
        " it to 1.15x Paris', and refuses a world that can put more than 30"
        " objects in one frame when CS_NVIS is 32 and drops the rest silently. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("csart", "soak", py("tests/unit/t_csart.py"), 0.6,
        "apps/skies/csart.inc is what tools/csart.py generates (SPEC.md 88.10):"
        " the launcher's two 1bpp bands are drawn by the tool and checked in,"
        " and the include cannot drift from the drawing. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("csterrain", "soak", py("tests/unit/t_csterrain.py"), 0.2,
        "SPEC.md 88.13.1: every CLEAR SKIES object carries the class flag "
        "its MODEL implies - CSO_TERRAIN for the hills and the water, "
        "CSO_ROAD for the roads and bridges. The Detail Level ladder refuses "
        "objects before they are transformed and those bits are what exempt "
        "them, so a row without one simply vanishes at a rung it should have "
        "survived: thirty water objects had no flag and every river in the "
        "tree emptied at None. "
        "SOAK and not fast: CLEAR SKIES is ONE package, so this belongs "
        "beside a change to it - `soak -k 'cs*'`"),
    Row("pkgdeps", "fast", py("tests/unit/t_pkgdeps.py"), 1.4,
        "every %include a package pulls in must be a prerequisite of its .bin "
        "rule, or editing a shared library does not rebuild what includes it "
        "and `make` says 'up to date'. apps/os88ui.inc was missing from NINE "
        "shipped packages and apps/os88type.inc from three; it was found by an "
        "A/B that measured zero because the package never reassembled"),
    Row("sndmove", "soak", py("tests/sndmove.py"), 150.0,
        "SPEC.md 66.6.3.1/66.6.4: the LAST pinned claims. SOUND.DRV is the "
        "only driver that hooks an interrupt vector - five of them - so its "
        "image was the one thing mem_can_move still refused outright; the "
        "kernel patches the IVT now and moves the image at IF=0. Its 8KB DMA "
        "ring sits immediately below it and could never move while it was "
        "pinned, which is why the two are one row. Six assertions, and THREE "
        "of them are A/B'd: with sbl_ring_reloc storing the old base 5 goes "
        "red alone, with the IVT loop out 5b goes red alone AND THE MACHINE "
        "STILL DRAWS - which is the whole reason that check exists. It wants "
        "a Sound Blaster: os8088_5150_sb_gla, and the driver is already up at "
        "the first desktop frame there - the first draft went to the Control "
        "Panel and clicked row 0, which UNLOADED it",
        wants=("build/sndmove360.img",)),
    Row("drvmove", "soak", py("tests/drvmove.py"), 170.0,
        "SPEC.md 66.6.3: a DRIVER IMAGE moves. It drives the scenario the "
        "whole study exists for - mount the hard disk, mount the RAM disk "
        "above nothing, unmount the hard disk, and before this the hole "
        "stayed for the session. Its third assertion reads every drv_fseg*, "
        "drv_blkseg, drv_tab row and claim owner BY NAME for the old segment, "
        "because a stale one does not fault: it far-calls a dispatcher in "
        "freed memory on the next volume access",
        wants=("build/regmove360.img",)),
    Row("regapp", "soak", py("tests/regapp.py"), 150.0,
        "SPEC.md 66.6.1/66.6.2 per SHIPPED PACKAGE: five that hire a worker "
        "declare OS88_REGION_MOVABLE and OS88_WORKER_RESTARTABLE, and a "
        "declaration the owner fence refused is indistinguishable from one "
        "that took, from inside the package (66.5.6.2). So this reads MC_RLOC "
        "and inst_restart back out of the kernel's own tables. regwork proves "
        "the move; this proves the packages - and it found the region "
        "declaration placed at the SPAWN, where a package that hires no "
        "worker never reaches it",
        wants=("build/regapp360.img",)),
    Row("regwork", "soak", py("tests/regwork.py"), 170.0,
        "SPEC.md 66.6.2: a WORKER-OWNING region moves once the package has "
        "declared a restart point, and the worker comes back. regpin is the "
        "same disk, the same arena and the same forcing ask with the 'R' key "
        "NOT pressed - the two rows are one experiment either side of one "
        "declaration. The assertion that matters is the last: a restart that "
        "built a frame the scheduler never resumed leaves the counters right "
        "and the machine one worker short, so the loop count is read twice",
        wants=("build/regpin360.img",)),
    Row("regpin", "soak", py("tests/regpin.py"), 160.0,
        "THE NEGATIVE ARM of SPEC.md 66.6.1 (docs/plans/HEAP-UNPIN-PLAN.md "
        "10.1): a region whose package owns a WORKER must NOT move, because "
        "task_spawn wrote the segment into the worker's frame and a pass that "
        "moved it would not fault - it would run the wrong memory. "
        "tests/regmove.py is the positive half. Its subject is tests/pinme and "
        "NOT tests/filler: a package reaches mem_claim only from inside its "
        "own callback, so the asker's own region is refused for having a "
        "frame in it and a row built that way stays green with the pin taken "
        "out of the kernel - measured. SHEET is the control: PINME's region "
        "must stand still WHILE SHEET'S MOVES, or the run proves nothing",
        wants=("build/regpin360.img",)),
    Row("drvclaim", "soak", py("tests/unit/t_drvclaim.py"), 0.1,
        "a driver's SERVICE TASK may not reach a claim door: mem_claim can "
        "reach mem_compact, which far-calls a holder's relocation proc on the "
        "stack it was entered on, and a 384-byte worker slice is not STK0. "
        "SPEC.md 20.6 rule 7 binds a package's worker and nothing binds a "
        "driver's; SOUND.DRV is the only driver in the tree that spawns one, "
        "so today the fact is true and unwritten - the second one is where it "
        "stops being obvious. docs/plans/HEAP-UNPIN-PLAN.md 12 question 4. "
        "SOAK and not fast: it can only fire when a SECOND driver gains a "
        "service task, which is not a build-to-build event"),
    Row("inktab", "soak", py("tests/unit/t_inktab.py"), 0.2,
        "SPEC.md 42.23.1: Paint's two ink-class masks ARE the kernel's "
        "gfx_inktab. A one-bit canvas stores what a 1bpp SCREEN shows, so the "
        "two have to agree about which of the sixteen are solid and which are "
        "the 50% dither - and the first version of the masks was a GUESS that "
        "put six dither colours in the white class. gfx_inktab is a `db` "
        "table, so `mirror` cannot see it: that is why this is a row of its "
        "own and not one of its names. "
        "SOAK and not fast: the masks are PAINT's half of the mirror, so "
        "it belongs beside a PAINT or gfx_inktab change and not on every "
        "build",
        ),
    Row("frinset", "soak", py("tests/unit/t_frinset.py"), 1.9,
        "fr_inset never claims a pixel frac_iter would have escaped, and its"
        "rejection boxes still match the closed forms (SPEC.md 40.5). "
        "SOAK and not fast: FRACTAL is ONE package - `soak -k 'fr*'`"),
    Row("frstepv", "soak", py("tests/unit/t_frstepv.py"), 0.4,
        "The axis-phased pass order is still a permutation of the canvas, a"
        "row's twin is still the row before it, and rc=0 is still the order"
        "walked before the phase existed (SPEC.md 40.6). SPEC.md 40.1 rests"
        "the whole restore cache on that arithmetic and nothing else records"
        "which cached row is which. "
        "SOAK and not fast: FRACTAL is ONE package - `soak -k 'fr*'`"),
    Row("frcycle", "soak", py("tests/unit/t_frcycle.py"), 0.8,
        "SPEC.md 40.7's cycle check returns exactly what the uncut core"
        "returns, for all five types. The CLAIM needs no sweep - a repeated"
        "state in a deterministic map can never escape - but the BOOKKEEPING"
        "does: a reference refreshed at the wrong moment or left over from"
        "the last pixel reads FR_CAP for a point that escapes. "
        "SOAK and not fast: FRACTAL is ONE package - `soak -k 'fr*'`"),
    Row("appsmall", "soak", py("tests/unit/t_appsmall.py"), 0.8,
        "SPEC.md 27.16's two claims: -DAPP_SMALL costs the SHIPPED package zero bytes (docs/history/KERN-SPLIT-PLAN.md 6's gate, one level down), and the small build is really smaller. Both fail silently - a %ifdef one line too wide changes the shipped package for a feature it still has, and a define that stops reaching the source leaves build/smallapps*.img as the ordinary floppy under another name. It is also the only thing keeping the small arm ASSEMBLING: nothing in `all` builds it."
        "SOAK and not fast or full: it is a build CONFIGURATION, "
        "t_buildmatrix's sentence one package along - `fast` may not build "
        "at all, and whether five named packages' small arm still assembles "
        "is not 'did you obviously break the OS'. It follows t_buildmatrix "
        "down"),
    Row("ktags", "soak", py("tests/unit/t_ktags.py"), 0.1,
        "every owner tag the kernel ships has a TYPE name on the Task "
        "Manager's heap page - SPEC.md 28.4's hex fallback is for a tag this "
        "build has never seen, and three shipped ones had been sitting in it. "
        "SOAK and not fast: an owner tag is a kernel constant, so only a "
        "kernel change adds one"),
    Row("dirwsize", "soak", py("tests/unit/t_dirwsize.py"), 0.1,
        "The directory cache picks its WIDTH from the machine now (SPEC.md "
        "18.95.5), so three numbers in three places have to agree: the "
        "constants, the gate's divisor, and the shift-add that turns slots "
        "into KB. The row that matters is that the claim COVERS the width at "
        "every n the machine can pick - dsk_rah_fill addresses the last slot "
        "inside the claim, so a claim short by one slot is an int 13h writing "
        "into whatever the heap handed out next. Host-side because a partial "
        "width needs a 36-126KB free run and no emulator here can be put in "
        "that state on demand. "
        "SOAK and not fast: the directory cache's arithmetic is "
        "kernel-internal",
        needs=(), serial=False),
    Row("pgrank", "soak", py("tests/unit/t_pgrank.py"), 0.1,
        "The purgeable caches are ORDERED - WSAVE below FATW below DIRW - "
        "and that ordering IS the machine's eviction policy (SPEC.md 50.6.4). "
        "A rank is one token with no callers and is silent both ways: too low "
        "and the cache is thrown away in front of something cheaper to "
        "rebuild, too high and it survives at a dearer one's expense. "
        "MEM_P_FATW shipped at LOW for one commit on a per-event cost weighed "
        "against a whole-install one (SPEC.md 18.8.4). Also checks each rank "
        "is inside the purgeable range at all, and that dsk_fatw_want asks "
        "mem_avail_lvl at its OWN rank so it may take the caches it outranks. "
        "SOAK and not fast: the eviction order is the memory manager's "
        "own and no package can set a rank",
        needs=(), serial=False),
    Row("kernbudget", "fast", py("tests/unit/t_kernbudget.py"), 0.1,
        "docs/KERNEL-MEMORY.md's blessed baseline carries THIS kernel's KERN_BUDGET - it went two moves behind because tools/kernsize.py compared spare and could not see a budget move at all."
        "FAST by the owner's decision, and it is rule 2's ONE STATED "
        "EXCEPTION (docs/WRITING-TESTS.md 2.1): the row costs 28ms and "
        "PRINTS `KERN_BUDGET big <n>, small <n>` on every build, which is "
        "how kernel size drift stays visible between one person's commits "
        "and the next. A row may earn `fast` by what it puts on the screen "
        "as well as by what it catches - bounded by being effectively free, "
        "and by the number being one the project actually steers by. Do not "
        "move it back"),
    Row("swallow", "fast", py("tests/unit/t_swallow.py"), 0.1,
        "a statement that ended up inside a block comment: it compiles clean, "
        "runs never, and cost apps/c64 a Paste that outlived a machine reset"),
    Row("drvmem", "soak", py("tests/unit/t_drvmem.py"), 0.1,
        "the Drivers page's memory column (SPEC.md 31.6.2) re-derived: every "
        "image term against the .drv this build made, every claim term against "
        "the constant in the driver that takes it. "
        "SOAK and not fast: it re-derives ONE PAGE of ONE application "
        "against per-driver constants"),
    Row("ccmake", "fast", py("tests/unit/t_ccmake.py"), 2.1,
        "automatic compiler setup: missing/partial install, parallel dependents, "
        "warm reuse, setup failure propagation and fresh live-media dependencies"),
    Row("imager", "fast", py("tests/unit/t_imager.py"), 0.1,
        "host media detection, image compatibility, confirmation and read-back "
        "verification without writing physical devices"),
    Row("image", "fast", py("tests/unit/t_image.py"), 0.1,
        "the shipped floppies read by an independent FAT12 walker: contiguity, "
        "the standard BPB, SPEC.md 19.6's attributes"),
    Row("pkg", "fast", py("tests/unit/t_pkg.py"), 0.1,
        "package/driver/module headers, and every file on every image proved "
        "identical to the artifact it was built from"),
    Row("fonts", "fast", py("tests/unit/t_fonts.py"), 0.1,
        "the typefaces are in SYSTEM/FONTS on every shipped system image and "
        "nowhere else (SPEC.md 19.8.1), and apps/os88type.inc's ty_gofonts "
        "spells the same two components. The two ends of that path are in "
        "files that never see each other, and when they disagree nothing says "
        "so: ty_scan answers CF=1 and every Font menu on the machine is one "
        "item long, which looks exactly like a Font menu. `pkg` above cannot "
        "see it - it matches every file BY NAME, so the folder can move and "
        "each of its rows still passes"),
    Row("sfx", "soak", py("tests/unit/t_sfx.py"), 0.4,
        "OS88NET.COM's self-extracting stub (SPEC.md 62.12) EXECUTED - the "
        "shipped bytes run in a small 8086 and must rebuild os88net.raw "
        "exactly. The DOS end has shipped broken twice for want of ever "
        "being run (tests/dosstub); a packer checked only by its own "
        "decoder is that shape again. "
        "SOAK and not fast: the stub is one artifact of one tool, and no "
        "other build reaches it"),
    Row("diskverify", "fast", py("tests/unit/t_diskverify.py"), 0.5,
        "the tree's own fsck, pointed at the seven images `make` ships and "
        "never ran on"),
    Row("qemuown", "fast", py("tests/unit/t_qemuown.py"), 0.1,
        "every test that LAUNCHES a QEMU registers a teardown for it. `make "
        "test` daemonises the emulator and returns, so for most of this tree's "
        "life a row that FAILED simply left its running - and the only thing "
        "that ever killed one was the next run's kill-stale, which reaches an "
        "instance in the same checkout with the same pidfile and nothing else. "
        "Two of them survived FIVE HOURS from two worktrees and broke "
        "`ps2mouse` on the pre-merge gate with a write-lock error naming "
        "build/os8088.img: the cost of the leak is paid by an unrelated row, "
        "hours later, wearing a message about the wrong subject "
        "(docs/plans/HANDOFF-SOAK-FINDINGS.md B9)"),
    Row("canary", "fast", py("tests/unit/t_canary.py"), 0.1,
        "SPEC.md 18.93.1's canary offset re-derived from every shipped image's "
        "own BPB: it has to name a sector a transfer run reads AFTER the head "
        "boundary, because the half before it loads correctly on exactly the "
        "machine the canary is for - which is how the first one shipped wrong"),
    Row("mlen", "soak", py("tests/unit/t_mlen.py"), 3.4,
        "twelve month lengths, read back out of build/kernel.bin. clk_mlen "
        "carries the eleven non-February ones as a 16-bit MASK since kernel "
        "size pass 3 - three bytes shorter than the db table it replaced, and "
        "twelve facts collapsed into one hex constant nobody can check by "
        "eye. Nothing else in the tree covers them: tests/dtfield.py row 3 is "
        "the only test that reaches the routine at all and it is '30 Jan + "
        "one month lands on 28/29 Feb', i.e. February - which is the BRANCH "
        "below the mask and the one arm the rewrite did not touch. A wrong "
        "bit surfaces as '31 April accepted in the Date/Time page' and as a "
        "midnight rollover on the wrong day, which no harness here can run "
        "long enough to see. "
        "SOAK and not fast: clk_mlen is kernel-internal and only a clock "
        "change reaches it"),
    Row("bsssentinel", "soak", py("tests/unit/t_bsssentinel.py"), 3.5,
        "a sentinel byte whose RESTING value is not zero cannot live in .bss "
        "(SPEC.md 12.8.5.1): `-f bin` emits nothing for it and the boot read "
        "lands padding on those bytes, so it comes up 0. fsx_cur shipped that "
        "way the moment fpg_arm started reading it from OUTSIDE an fsx "
        "bracket, and the file-progress widget was refused for every file "
        "operation on the machine - which on an install reads as a lock. "
        "SOAK and not fast: only a kernel writer can put a byte in the "
        "kernel's .bss"),
    Row("invariants", "soak", py("tests/unit/t_invariants.py"), 1.2,
        "three run-time facts that no %if can express, checked by WHO WRITES "
        "the byte: [sch_cur] is never 0xFF (fsx's ownership compares refuse "
        "[fsx_task]'s no-bracket sentinel only because of that, so a second "
        "writer parking one there grants a bracket to nobody); "
        "[vid_mono]/[vid_planes] are one fact written together (SPEC.md 39.26 "
        "deleted four plane loops on it, and a writer that moves one leaves "
        "all four drawing plane 0 alone on every adapter); and "
        "[vid_rseg] has one writer, which is a DIFFERENT fact because "
        "sw_xfer used to end on a segment compare. "
        "SOAK and not fast: all three are facts about who writes a KERNEL "
        "byte, and no package writes one",
        needs=(), serial=False),
    Row("assocpage", "soak", py("tests/unit/t_assocpage.py"), 0.1,
        "the document page is GENERATED now (SPEC.md 54.3), so its 32 words "
        "are replayed on the host against a golden list - the only copy of "
        "them left in the tree. tests/assocglyph.py is the gate on the glass, "
        "but two of its three assertions compare this kernel against ITSELF, "
        "so a generator that composes the same WRONG page every time passes "
        "both of them cleanly and the icon it is wrong about is on every "
        "document in the system. Its third (--ref) closes that and needs a "
        "capture taken BEFORE the change, on a 1bpp adapter, under an "
        "emulator; this row is the same proof for the DATA half in a fifth of "
        "a second, on every make. "
        "SOAK and not fast: the generator is the association layer's, and "
        "assocglyph beside it is already soak",
        needs=()),
    Row("registry", "fast", py("tests/unit/t_registry.py"), 0.2,
        "every test in tests/ is registered in a tier or says why not - the row "
        "that stops this suite going back to a directory nobody can enumerate"),
    Row("machines", "fast", py("tests/unit/t_machines.py"), 0.3,
        "no row names a machine whose ROM this tree has not got. MartyPC "
        "falls back to glabios_pc when a romset is absent and says NOTHING, "
        "so nine rows spent months reporting passes about a machine they "
        "never booted (docs/plans/HANDOFF-SOAK-FINDINGS.md E3). It also checks each "
        "GLaBIOS twin still differs from its IBM original in `rom_set` alone "
        "- a drifted twin measures the config's difference and calls it the "
        "kernel's"),
    Row("asmrules", "fast", py("tests/unit/t_asmrules.py"), 2.0,
        "unreachable code after an unconditional jump, a prologue restored in "
        "the WRONG ORDER (SPEC.md 1's register discipline: balanced depth, "
        "swapped pair, nothing faults), a `cpu 8086` reachable from every "
        "root, and a kernel LOCAL BLOCK nothing can reach - check 1 stops at "
        "\"is there a label between the jump and this line\" and a label only "
        "helps if something reaches it, which is how the DMA staging arm of "
        "both file pipelines rotted for a year with this row green "
        "(SPEC.md 18.4.2.1)"),
    Row("resident", "soak", py("tests/unit/t_resident.py"), 3.7,
        "nothing the splash's first tick runs may jump to SPEC.md 15.1.2's "
        "epilogue ladder - the ladder is at the far end of .text and the "
        "floppy has not delivered it yet, so the machine dies with a blank "
        "screen and no message. kernel.asm's SPL_RES_SIZE guard measures where "
        "the resident code ENDS, and size is not reach. "
        "SOAK and not fast: the splash's reach into the epilogue ladder "
        "is kernel-internal"),
    Row("wakedrain", "soak", py("tests/unit/t_wakedrain.py"), 0.2,
        "every event-queue drain gives a package's wake back - one that eats "
        "it deafens the window for the rest of its life (SPEC.md 74.1.1). "
        "SOAK and not fast: an evq_pop site is kernel code, so only a "
        "kernel change adds one"),
    Row("wab", "soak", py("tests/unit/t_wab.py"), 0.1,
        "the demo bundles `all` just packed, read back by an independent "
        "second reader of the .WAB format - weavesim and t_wab are two "
        "implementations written from WEAVE-SPEC that can disagree, and "
        "until the 8086 runtime lands this row is the disagreement's only "
        "audience. "
        "SOAK and not fast: the .WAB format is the Weave family's - `soak "
        "-k 'weave*' -k 'wab' -k 'lmpack'`"),
    Row("wire", "fast", py("tests/unit/t_wire.py"), 2.5,
        "the Wire's two formats (SPEC.md 92.2 and 92.13), from both ends at "
        "once: tools/os88wire.py packs a fixture out of build/hello.o88 and "
        "build/mines.o88 and a reader written from the SPEC alone reads it "
        "back, every refusal the writer owns is fed the input that breaks it, "
        "and every WC_*/WIRE_* equ in apps/thewire/wcat.inc and every WA_*/"
        "WARC_* in warc.inc is compared against the tool's. The mirror is the "
        "half that cannot be got by reading either file - there is no linker "
        "here, so a half-applied format change packs perfectly and the 8088 "
        "then reads a record at the wrong offset (t_mirror's argument, for a "
        "pair of files it does not cover). The ARCHIVE half adds a third "
        "reading on top of that: 92.13 pins its compression BY ITS DECODER, "
        "so the tool's encoder, the tool's reference decoder and a decoder "
        "written here from the paragraph alone must all agree - and the "
        "8088's resumable unpacker will be a fourth"),
    Row("lmpack", "soak", py("tests/unit/t_lmpack.py"), 10.0,
        "WEAVE-SPEC 11.1's byte-identity gate, host-side: LOOM's five "
        "SHIPPING compilers built with the host cc, packing every demo, "
        "every template and every case in tests/weave/packerr/, diffed "
        "against tools/weavesim.py bundle for bundle and sentence for "
        "sentence. It is NOT the gate - `weavepack` packs on the MACHINE, "
        "and the difference between the two is one word wide (int is 32 "
        "bits here) - but it is what makes an on-machine compiler writable "
        "at all, and it puts a weavesim change in front of the next `make` "
        "rather than the next soak run. SKIPS with no host compiler, "
        "because a clone with nasm and python3 builds every floppy this "
        "project ships and a red suite there would be reporting on the box. "
        "SOAK and not fast: WEAVE and LOOM are two packages, and six "
        "seconds of every `make` is the wrong place to prove their "
        "compilers agree - `soak -k 'lmpack'`, which is what a change to "
        "either one runs",
        needs=()),
    Row("textrules", "fast", py("tests/unit/t_textrules.py"), 0.7,
        "SPEC.md 6.6's ratchet: transparent text (font_char/font_str) draws every "
        "pixel twice and flashes on the target machine, so every call site is "
        "registered in tests/textsites.txt with a reason and the count can only "
        "go down"),
    Row("layout", "fast", py("tests/unit/t_layout.py"), 0.1,
        "SPEC.md 2.9: a GUEST ADDRESS IS NOT A FILE OFFSET. Stage 2 sits in "
        "front of .text in kernel.bin, so a host-side reader that indexes the "
        "image by a symbol, a segment or a return address lands 6,656 bytes "
        "early - on real code, silently. Five readers got it wrong "
        "independently: two rows dead since 2.9, two reporting .cold as "
        "corrupt every run, and stkwater recognising 126 of 3,000 call sites"),
    Row("fixtures", "fast", py("tests/unit/t_fixtures.py"), 0.1,
        "a row's scratch floppy is a BUILD PRODUCT: os88disk.py behind a bare "
        "`not os.path.exists` builds it once and every run after boots "
        "whatever build/ held that minute, which is the stale kernel.bin trap "
        "in other clothes. It read paintsu as 0 pixels wrong against a Paint "
        "without the fix in it, and that number was pushed on"),
    Row("vbrseg", "soak", py("tests/unit/t_vbrseg.py"), 3.4,
        "SPEC.md 52.10.2.1: build/boothd.bin's BLOB_SEG and SPL_FSEG read back "
        "out of the assembled sector and compared with build/kernel.bin's own "
        "map. The volume boot record is told where the heap starts by a host "
        "tool re-running over kernel.asm, and a knob kernel whose ladder the "
        "tool did not know about boots into wild execution with no build "
        "error anywhere. "
        "SOAK and not fast: the volume boot record's segments are "
        "kernel-internal",
        ),
    Row("checkdocs", "fast", py("tools/checkdocs.py"), 1.6,
        "stale SPEC.md citations and slot numbers in prose (already in `make`; "
        "here too so the suite is a complete statement)"),
    Row("docindex", "fast", py("tools/os88index.py", "--check"), 0.2,
        "docs/INDEX.md still matches the tree - an index that has drifted is "
        "worse than none, because it is consulted and believed"),
    Row("imgcases", "fast", py("tools/os88imgcase.py", "--check"), 0.3,
        "apps/imgtest/imgcases.inc still matches what the format documents "
        "say - the expectations are GENERATED, and a generated file with no "
        "staleness gate describes a corpus that has moved out from under it "
        "(SPEC.md 93.3)"),
    Row("checkreadme", "fast", py("tools/checkreadme.py", "readme.txt"), 0.1,
        "README.TXT's width and size rules - Note Pad refuses a file one byte "
        "too long and shows nothing at all"),
    Row("ovlchk", "fast", py("tools/os88ovlchk.py"), 1.4,
        "no near call crosses a section boundary - it assembles cleanly and "
        "runs wrong"),
    Row("dsegaudit", "soak", py("tools/dsegaudit.py"), 0.2,
        "no path holding [dsk_dseg] can reach a claim, and a claim COMPACTS "
        "(SPEC.md 50.6.2). It is a 0/1 gate with no harness around it and "
        "nothing ran it - not `make`, not this file, and not t_registry, "
        "whose walk is over tests/ and cannot see a tool. A static gate that "
        "nobody runs is a comment. "
        "SOAK and not fast: [dsk_dseg]'s reach is inside the kernel's "
        "disk layer"),
    Row("stknosave", "soak",
        py("tools/stkdepth.py", "drivers/ether/ether.asm", "--check"), 0.4,
        "every `; STKDEPTH-NOSAVE:` in ETHER.DRV still holds: the routines "
        "that stopped saving a register to fit a 384-byte task slice (SPEC.md "
        "72.16.4) still get it back from every callee. Without this the trade "
        "is a landmine for whoever edits the TCP stack next. "
        "SOAK and not fast: every one of those markers is ETHER.DRV's "
        "own, so this is a per-driver row"),
    Row("stkbalance", "fast",
        py("tools/stkbalance.py", "apps/sheet/sheet.asm", "apps/chart/chart.asm",
           "apps/os88chart.inc", "apps/os88fp.inc", "apps/os88text.inc",
           "apps/os88line.inc", *_kernel_sources()), 0.9,
        "every `ret` in the KERNEL and in SHEET, CHART and the includes they "
        "share is reached at "
        "the depth it started at. `ch_legend` pushed SI and never popped it, so "
        "its `ret` jumped to the saved register: a black canvas and a wedged "
        "app, with no crash and no message (SPEC.md 82.7.3). The walk is "
        "path-aware because a naive push-vs-pop count flags one routine in ten "
        "and would just be ignored. STILL SCOPED to these files, but no longer "
        "because the kernel cannot be walked: the walker follows tail jmps "
        "across files now, and the two `; STKBALANCE-OK:` in sched.inc that "
        "cover the context switch and task_yield's fabricated int 08h frame "
        "have landed, so the kernel measures ZERO and is GATED here from this "
        "commit on (docs/plans/completed/STKBALANCE-KERNEL.md carries the triage of all 24). "
        "Turned on DURING size pass 2 rather than after it, so an imbalance is "
        "caught by the batch that introduces it instead of by a bisect. "
        "One gap "
        "is left and is counted in the tool's own summary line: loop back-edge "
        "conflicts are suppressed, because the count lives in a register"),

    Row("stkapps", "fast", py("tests/unit/t_stkapps.py"), 2.1,
        "every `ret` in EVERY SHIPPED PACKAGE AND DRIVER is reached at the "
        "depth it started at. `ch_legend` pushed SI and never popped it, so its `ret` "
        "jumped to the saved register: a black canvas and a wedged app, with "
        "no crash and no message (SPEC.md 82.7.3). This row walked only SHEET, "
        "CHART and four shared includes - 776 entries - until three blind "
        "spots in the walker were closed; it walks 9,038 now, drivers/ "
        "included - the TCP/IP stack had never been walked either. Each blind spot "
        "hid a whole class: `apps/*/*.inc` was in no file list, so RunCPM's "
        "Z80, the C64's 6510 and Weave's VM had never been walked by anything; "
        "all three dispatch as `jmp [cs:bx+tab]`, which a walker looking for "
        "`jmp [tab+reg]` reads as every opcode handler being a routine entered "
        "at depth 0; and wvm.inc puts its branches inside macros. It found one "
        "real defect - `op_size` in os88parts.inc returned into a saved "
        "register on a malformed part table, in every package via "
        "os88api.inc. The KERNEL is the `stkbalance` row above, not this one: "
        "the two file lists have nothing in common and were arrived at from "
        "opposite ends (docs/plans/completed/STKBALANCE-KERNEL.md 4)",
        ),

    Row("gifdrag", "soak", py("tests/gifdrag.py"), 150.0,
        "THE FIELD'S OWN FREEZE, driven end to end (SPEC.md 8.7.4): the Task "
        "Manager on its HEAP page while PAINT holds MEDIA/OS8088.GIF, then the "
        "window dragged again and again. It asserts the MARGIN and not the "
        "survival, which is the whole reason it is a test rather than a "
        "screenshot: 'it did not freeze' is what every run before the report "
        "also said, because this machine's interrupt floor is 32 bytes where "
        "SPEC.md 8.7 sizes against 64 on iron - so the walk that killed a real "
        "5150 reads 180 of 192 here and passes. It fails when any slice, or "
        "task 0's own 512, goes past 80% full, which is the emulator's honest "
        "question: is there room left for the frame it is not charging? SOAK "
        "and not full - it boots, launches two packages, decodes a GIF and "
        "drags eight times, which is minutes",
        ),

    Row("stkclass", "fast", py("tests/unit/t_stkclass.py"), 5.0,
        "every package's DECLARED stack class (SPEC.md 8.7.2) covers its "
        "worker's deepest chain plus SPEC.md 8.7's 64-byte interrupt floor, at "
        "Frotz's 1.25x - the thinnest margin the tree already carries, so "
        "nothing shipping has to move and only a NEW thinnest can fail. The "
        "row above checks a package's stack arithmetic BALANCES; nothing "
        "checked the slice was big enough to hold it, and `OS88_STACK_192` was "
        "a number a human typed after running a tool once. SPEC.md 8.7.4 is "
        "what that cost: tools/stkdepth.py followed `call` edges and not tail "
        "jumps, so `tm_worker` priced at 56 bytes when the heap page it reaches "
        "by `ja tm_upd_heap` is 96 - the Task Manager took 192 on the strength "
        "of 56, measured 180 of them with its heap page open beside PAINT, and "
        "went through the canary into sch_stkdie's cli/hlt on a real 5150. It "
        "reads the class out of the BUILT .o88's header byte, not out of the "
        "source, so a packer that stops emitting the field fails this too",
        ),

    Row("stkwalker", "fast", py("tests/unit/t_stkbalance.py"), 0.6,
        "the stack walker itself, against eleven idioms it must stay QUIET "
        "about and six defect shapes it must catch. A gate that reports "
        "nothing passes every build and defends nothing; one that reports a "
        "routine in ten gets ignored and defends nothing either, which is why "
        "the kernel went ungated for this tree's whole life. Both halves are "
        "pinned here: the QUIET half is every idiom that was once a finding "
        "(a continuation, a cross-file shared tail, `jmp short $+2`, `pushf` + "
        "`call far`, `push`/`push`/`retf`, a dispatched jump table, a data "
        "table, `owner.local`), and the LOUD half is what a size pass actually "
        "produces - a deleted `pop`, a cross-jumped epilogue that is not a "
        "twin, one overflow handler serving two depths. Nine of the seventeen "
        "fail against the walker as it was, and one of those nine is a LOUD "
        "row: the old walk skipped a routine whose every exit was a tail jmp, "
        "so it could not see that shape at all"),
]

# --------------------------------------------------------------------------
# full - everything above, plus these. Run when major work reaches the
# integration branch, not per commit (docs/TESTING.md, `When to run which
# tier`).
# --------------------------------------------------------------------------
FULL = [
    Row("buildmatrix", "soak", py("tests/unit/t_buildmatrix.py"), 180.0,
        "the knob kernels and kern_small - every configuration `all` "
        "does not build, and so the only thing that keeps them assembling"
        ". SOAK and not full: 99 knob configurations is not 'did you "
        "obviously break the OS' - a knob is an instrument, the shipped "
        "kernel is built by `make` and kern_small by small128's own private "
        "tree - and at 143s it is four fifths of the whole tier budget on "
        "its own. It is what a change to a knob runs", builds=True),
    Row("bmshare", "soak", py("tests/unit/t_bmshare.py"), 30.0,
        "...and that the three variables it builds them WITH change no byte. "
        "ICODIR/NOOVLCHK/NOKERNSIZE each take work out of a knob build - the "
        "shared packages, the source-only overlay gate, the size report's "
        "second assembly - and taking work out of a build is the change that "
        "goes wrong in silence. It builds one knob kernel both ways and "
        "compares the images, and it checks the exclusion the sharing rests "
        "on: SBDRAGOFF/SBRATE reach notepad's own nasm line, t_buildmatrix "
        "derives that pair from $(PKGSBDEF) rather than keeping a copy, and "
        "both ends of that derivation are asserted here"
        ". SOAK and not full: it is about t_buildmatrix's own build-speed "
        "variables, so it follows that row down"),
    Row("kernmods", "soak", py("tests/unit/t_kernmods.py"), 30.0,
        "tools/kernsize.py's PER-MODULE pass still measures - the byte "
        "compare inside it worked and nothing ran it, so --bless returned 1 "
        "without writing while t_kernbudget went on advising it. Here and "
        "not in fast because it assembles the kernel twice"
        ". SOAK and not full: it gates tools/kernsize.py's reporting pass, "
        "which is an instrument and not the OS",
        needs=("nasm",), serial=False),
    Row("ctoolchain", "full", py("tests/unit/t_ctoolchain.py"), 8.0,
        "the C toolchain still produces a package - the OTHER thing `all` "
        "does not build, and the one that had a `cc` capability with no row "
        "behind it while no C package assembled for two releases",
        needs=("cc",), serial=True, builds=True),
    Row("martyconc", "soak", py("tests/martyconc.py"), 20.0,
        "TWO EMULATORS AT ONCE, and every way that used to go wrong. It is "
        "here rather than in soak because it gates the INSTRUMENT the whole "
        "marty tier runs on, and every failure it catches is SILENT: two "
        "instances sharing a floppy do not error - one boots the other's "
        "disk; two sharing a port do not error - the second attaches to the "
        "first's machine; and a second client on one used to HANG rather "
        "than be refused. It asserts separate ports, directories, disks and "
        "memories, a refusal that arrives in under a second and names the "
        "holder, and that reap() takes an orphan and leaves a live, owned "
        "instance alone. Runs three machines and boots two, so it is also "
        "the one row that would notice the isolation costing more than it "
        "saves"
        ". SOAK and not full: it gates the test INSTRUMENT and not the OS, "
        "so it cannot answer this tier's question. It is what a change to "
        "tools/os88marty.py runs",
        needs=("marty",), serial=True),
    Row("bootsmoke", "full", py("tests/bootsmoke.py"), 20.0,
        "does it still reach a desktop on both 1bpp adapters - the widest "
        "reach per second of any test here",
        needs=("marty",), serial=True),
    Row("smallboot", "soak", py("tests/smallboot.py"), 110.0,
        "does KERN_SMALL still reach a desktop - buildmatrix assembles that "
        "build and nothing has ever booted it, which is how it has been "
        "DISCOVERED broken three times rather than reported broken. Here "
        "rather than soak because SPEC.md 39's VGA renderer is now gated out "
        "of it, and an %ifdef that takes one body too many assembles "
        "perfectly and dies at the first paint. It builds its own image "
        "(`make small`, into build/smallk/) because there is no capability "
        "to probe for and `all` never builds that kernel"
        ". SOAK and not full: small128 beside it already builds this kernel "
        "and boots it to a desktop, so what this adds is the THREE-adapter "
        "sweep - the deep gate a kern_small change runs, at 118s",
        needs=("marty",), serial=True),
    Row("thewire", "soak", py("tests/thewire.py"), 340.0,
        "THE WIRE, end to end over a real card (SPEC.md 92.12): a host HTTP "
        "server on 8092 serves a fixture catalog packed by tools/os88wire.py "
        "out of build/hello.o88, build/mines.o88, a tier-3 WF_DISK entry and "
        "a WF_ARC one whose .WPK the test packs with --archive, and the "
        "machine fetches it because `make thewiretest`'s "
        "SYSTEM/APPDATA/WIRE.CFG says to. Eleven assertions: the catalog is "
        "understood, the host saw the request it expected, the list is the "
        "catalog, the 8088/8086 filter cuts four rows to three, the predicate "
        "greys Load Program on a WF_DISK record and NOT Add to Disk, the "
        "picture matches the file pixel for pixel, Add to Disk writes both "
        "files to B: byte-identical, Load Program runs one out of memory, "
        "AN ARCHIVE'S WHOLE TREE lands on B: byte-identical - eight entries "
        "at three depths, an LZSS one, two INCOMPRESSIBLE ones that put the "
        "stream over 65,536 bytes so the dword Content-Length and the 32-bit "
        "byte count are read with a high word in them, an empty file and a "
        "TWELVE-character name that fills its slot with no NUL, read back on "
        "the host by an independent FAT12 reader after `quit` - "
        "an archive mounts a RAM disk and runs its program entry off it "
        "(SPEC.md 92.14), and SPEC.md 92.6.1's CLIP assertion: after Load "
        "Program the launched window's content is captured, dragged 8px and "
        "back for a clean repaint, and the two must agree pixel for pixel, "
        "which they do not when the Wire's wake handler has drawn its "
        "buttons into a window that is not its own. "
        "**SOAK AND NOT FULL, and the tier's own rule is why**: this file's "
        "header lists eight emulator rows as what ten minutes buys, and what "
        "earns one is BREADTH PER SECOND. This is a boot, twenty clicks and "
        "three floppy write chains that can only fail for one package's "
        "reasons - the definition of a soak row. It runs in 96 seconds on "
        "an idle host and the budget is 340 for the reason every budget "
        "here is generous: a concurrent build makes an emulator row three "
        "or four times slower and a tier that failed on that would be "
        "reporting on the box. It is also QEMU's and "
        "cannot be MartyPC's: MartyPC has no network card of any kind, so "
        "ETHER.DRV cannot be hosted on it at all (SPEC.md 72.9). It builds "
        "its own two disks, and it DELETES them first - QEMU mounts B: "
        "writable and the write assertion would otherwise find last run's "
        "files already there",
        needs=("qemu",), serial=True, builds=True),
    Row("stk0water", "soak", py("tests/stk0water.py"), 70.0,
        "how deep TASK 0's stack has actually been (SPEC.md 15.1). That "
        "section says `redo the fill probe before lowering either` and the "
        "probe was a hand edit to kmain plus a hand read, so it had been run "
        "once - which is why `STK0_SIZE` sat at 4x a figure nobody had "
        "re-taken. This is it automated: fill everything below task 0's SAVED "
        "SP with 0xCC, drive the machine, read the deepest byte back. It "
        "reads 238 against 15.1's 246 (a heavier drive), and STK0_SIZE is 512 "
        "on both kernels now. Three things it had to get right and each was "
        "wrong first: the LIVE SP is a worker's, because SPEC.md 8.1.2 has "
        "ui_task block and an idle machine is 96.9% halted; the canary at the "
        "bottom must not be filled over, because SPEC.md 8.7 put slot 0 in "
        "sch_stkbase and sch_switch checks it on every switch - filling it "
        "reaches sch_stkdie and the only symptom is a pointer that will not "
        "move; and a menu released inside its pane SELECTS an item, which "
        "launched the About box and left the screen animating for ever. "
        "`soak` because it is a MEASUREMENT rather than an assertion - it "
        "prints the margin at five candidate sizes and fails nothing",
        needs=("marty",), serial=True),
    Row("small128", "full", py("tests/small128.py"), 40.0,
        "...and it reaches that desktop on a machine with 128KB IN IT. Every "
        "other MartyPC profile here is 640KB, so `MIN_RAM_KB` had been an "
        "ARITHMETIC claim since the day it was written - guard 5 compares two "
        "constants at assembly time and nothing had ever asked the result to "
        "run. The row above proves the build boots; this one proves the "
        "MACHINE does, which is a different question, because a purgeable "
        "claim that sizes itself off available heap has a floor of its own "
        "and the directory read-ahead is 64KB on a 640KB box. It is also "
        "docs/plans/KERN-SMALL-CUT-PLAN.md 8.2's `cheapest unexamined lever`: it "
        "walks mem_tab on the machine and fails if ANY pinned claim stands on "
        "a bare desktop, because that is heap the machine never gets back and "
        "no assembler can see it - SPEC.md 54.0's association cache was "
        "holding 3,072 bytes of one and was found by accident. Reads 0 "
        "pinned, 18,432 purgeable, 40.5 KB usable. Builds its own image for "
        "smallboot's reason"
        ". 40s and not 20 since smallboot went to soak: this row now "
        "pays for the `make small` tree itself - 38.4s measured cold "
        "against 16.1s when the tree is already there",
        needs=("marty",), serial=True),
    Row("int0sweep", "soak", py("tests/int0sweep.py"), 60.0,
        "Does anything raise a DIVIDE ERROR? (SPEC.md 11.96) On an IBM "
        "5150/5160 ROM the INT 0 vector is a BIOS stub that writes 0FFh to "
        "the 8259 mask and IRETs, so ONE divide overflow anywhere is a dead "
        "machine - IMR=FF, the tick stopped, the CPU parked in "
        "sch_idle_body's hlt with IF=1 and even the ISR-paced pointer "
        "frozen. THE POINT IS THE ROM. Every other MartyPC row in this file "
        "runs GLaBIOS, whose INT 0 handler does not touch the PIC, so the "
        "identical fault there is a wrong clip index and the session "
        "carries on: wm_ttl_rect spending BX under wm_clip_occl locked the "
        "machine hard on an IBM ROM and passed assocopen and every other "
        "row on GLaBIOS. Worse, a machine naming an IBM romset SILENTLY "
        "RESOLVES to glabios_pc when the ROM file is absent, so the handful "
        "of rows that ask for one were not testing it either. Arms INT 0 "
        "across a broad UI session and reports where it fired. The declared "
        "240 is MEASURED (207-209s observed): it said 180, which was the "
        "figure from when the row could not run at all. soak enforces no "
        "budget, so this is a description rather than a limit - but a "
        "description that is wrong is what makes the next person distrust "
        "the column",
        needs=("marty",), serial=True),
    Row("vgadrop", "soak", py("tests/vgadrop.py"), 40.0,
        "SPEC.md 39.22: the heap floor starts UNDER .vgabuf on a machine with "
        "no VGA and AT KERN_END on one that has it. Reads [mem_base] as a "
        "WORD on three adapters rather than a KB total, because a KB rounds "
        "and rounding is where an off-by-a-rung hides - and it is the only "
        "thing that would notice the gate being on [vid_mono] instead of "
        "[vid_avail], which reads identically until somebody switches a VGA "
        "machine to mono",
        needs=("marty",), serial=True),
    Row("weavesmoke", "soak", py("tests/weavesmoke.py"), 70.0,
        "WEAVE opens FORM.WAB and draws a window on both 1bpp GLaBIOS twins - "
        "the Weave family's widest single row (WEAVE-SPEC 12.3), and "
        "the widest reach per second the family has: the .WAB association, "
        "the accept idiom, the bundle reader, the flow walk and the first "
        "paint all fail here. It asserts the drawn window's STRUCTURE and "
        "never a golden screenshot, for bootsmoke's reason. It BUILDS ITS OWN "
        "DISK, which `full` may do and `fast` may not - and that is why it "
        "needs `cc` as well as WEAVE-SPEC 12.3's `marty`: WEAVE is a C "
        "package, so a tree without SmallerC cannot run this row at all and "
        "should say so as a SKIP rather than as a failure. 75s is 45s "
        "MEASURED here - two boots, two Disk-window navigations and two "
        "package launches - taken up by the ~1.6x a boot costs on the "
        "slowest box this suite is written for (7.8s against 5.0s), with a "
        "little room for the package still growing. It is NOT 2x bootsmoke: "
        "the launch after the boot costs as much again as the boot, and it "
        "went 41s -> 45s when wdraw.inc's paint core took weave.o88 from "
        "21,076 bytes to 27,020"
        ". SOAK and not full: WEAVE is a PACKAGE, and `full` carries "
        "nothing app-specific - `soak -k 'weave*'` is twelve rows including "
        "weavepack, which is WEAVE-SPEC 11.1's actual gate",
        needs=("marty", "cc"), serial=True, timeout=300),
]

# --------------------------------------------------------------------------
# soak - registered, discoverable, not in anybody's budget.
#
# Each row names the subsystem it is about, so `os88test.py soak -k <glob>`
# is how you run the ones your change could have broken. These are the deep
# single-subject gates; several are worth reading before touching their area.
# --------------------------------------------------------------------------
SOAK = [
    Row("pacman", "soak", py("tests/pacman.py"), 100.0,
        "native 8088 Pac-Man movement, score, pellets, fruit, level transitions, "
        "pause, full-screen repaint and worker teardown", needs=("marty",)),
    Row("paccman", "soak", py("tests/paccman.py"), 100.0,
        "PACCMAN's attract screen and tick path on a cycle-accurate 8088 "
        "(SPEC.md 91): the program opening on the attract screen with the "
        "CHARACTER / NICKNAME reveal run, a real Space arriving at int 09h "
        "starting a round, the speaker asked for the prelude's tones, the "
        "worker hired by the first paint, the game advancing with nobody "
        "touching it, dots eaten, the reserve strip down a life, and the row "
        "step this ADAPTER needs (2 on CGA, 1 everywhere else). Several of "
        "those are things the host harness structurally cannot answer - it "
        "drives pmc_frame() itself, pokes the latch byte and models the "
        "glass, so it never runs a real worker on a real scheduler nor a real "
        "keystroke through the kernel - and one is the measurement that "
        "sizes OS88_STACK_256: tools/stkdepth.py composes a 160-byte static "
        "chain, and the water mark in the worker's own slice (188 to 190 of 256 "
        "across the three profiles) is the only thing that says the interrupt "
        "floor "
        "on top of it fits. Wave 4 added the two SCORING FIXTURES - a "
        "frightened ghost put on Pac-Man's own tile must score exactly 200 "
        "and become eyes, the bonus fruit exactly 100 - written into bss by "
        "symbol at the worker's frame boundary, so the image check beside "
        "them still covers every byte of code and every arcade table; and "
        "the MEASUREMENT, one bracket over this port's frame proc and "
        "PACMAN.O88's on the same profile, printing fps / ms per frame / gfx "
        "calls per frame / effective game speed side by side with the "
        "verdict on the user\'s \'maybe more performant on XTs\' either way "
        "(it is not: 2.18 fps against 4.14 on os8088_xt_vga). SOAK and not "
        "full, deliberately: `make test-full` measured 597.4 s of its 600 s "
        "budget before this port, so a row that boots two machines belongs "
        "where there is no wall clock to overrun - what the full tier "
        "carries instead is t_ctoolchain BUILDING paccman, which runs "
        "build.sh\'s three host gates", needs=("marty", "cc")),
    Row("nasm3", "soak", py("tests/unit/t_nasm3.py"), 165.0,
        "THE OTHER ASSEMBLER. Every tier here assembles with whatever nasm "
        "the box has, which on this container, on CI and on every Debian or "
        "Ubuntu box is 2.16 - and CONTRIBUTING.md's floor being 2 is read as "
        "3.x being equivalent, which it is not: nasm 3 REFUSES constructs "
        "2.x takes. `add di, mod_fp - mod_tab*7` in kernel/mod.inc's mod_fpr "
        "is `invalid operand type` there and silent under 2.16, so it "
        "reached a merge un-buildable for everyone whose nasm is 3.x "
        "(Homebrew's is) and had to be adapted after the fact - commit "
        "799c5a9. Nobody was careless; the construct assembles perfectly on "
        "the assembler everybody in the loop was running, and a gate is the "
        "only thing that closes that. This one assembles the SHIPPED SET "
        "(read out of the Makefile's own `all:` rule, so a tenth artefact "
        "joins it the day it is added), then kern_small, the APP_SMALL "
        "package arms, kern_emu and every knob in t_buildmatrix's roster - "
        "imported, not restated. It does NOT assert that the two assemblers "
        "emit the same bytes: they do not, and it is legitimate (xmem.drv's "
        "32-bit movers come out with the two prefixes in the other order). "
        "165s is MEASURED cold on this container, 128s with the private "
        "tree already there; the knob half is cold every run either way. "
        "Soak rather than full because it is three minutes of pure `make` "
        "and the thing it defends moves at the speed of somebody typing a "
        "new construct, not per commit - run it before a merge that lands "
        "kernel or package assembly",
        needs=("nasm3",)),
    Row("weavevm", "soak", py("tests/weavevm.py"), 10.0,
        "WEAVE-SPEC 12.3: the SHIPPING apps/weave/wvm.inc run in a raw-QEMU "
        "BOOT SECTOR with SS != DS and no OS under it at all, diffed case by "
        "case against tools/weavesim.py's end states - the rcz80test / "
        "c64memtest shape, and the gate wave 3's whole interaction half is "
        "built on (13.1 gates it FIRST). It asks docs/TESTING.md's question "
        "differently from every other qemu row here: this is not QEMU instead "
        "of MartyPC for a machine feature, it is a boot sector with one "
        "%included file in it, so what the emulator supplies is an 8086, a "
        "serial port and isa-debug-exit and nothing about the machine is "
        "being asserted. Which is also why it asserts CORRECTNESS and never "
        "a time. Every case runs TWICE, at a 256-op budget and at a budget "
        "of ONE, because a core that kept state in a register across a slice "
        "boundary passes the first and fails the second; and the corpus "
        "carries negative controls the harness must FAIL, without which the "
        "comparison proves nothing. 20s is 1s MEASURED here (the guest runs "
        "in well under a second) plus the corpus generation and the nasm "
        "run, with room for the corpus growing",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("weavecanvas", "soak", py("tests/weavecanvas.py"), 10.0,
        "WEAVE-SPEC 12.1.3: the SHIPPING apps/weave/wspr.inc and "
        "apps/weave/wwork.inc - WEAVE.WSM's composer and frame loop - run in "
        "a raw-QEMU BOOT SECTOR with SS != DS and no OS under them, diffed "
        "case by case against the model's own canvas composer. It is the one "
        "differential in this family whose ORACLE HAD TO BE WRITTEN: every "
        "other row diffs against something that was already there, and "
        "6.10.2's composition had nothing - the model does not draw pixels "
        "and the canvas buffer is on no card, so a sprite composed a byte to "
        "the left or a dirty run a band too short is invisible in every "
        "screenshot this family takes. Four comparisons a case: the sprite "
        "records (the 1/16-px accumulators, the bounce mirrors, the score "
        "latch), the staging ring record for record, the DIRTY-BAND RUNS the "
        "last frame emitted - which is the 2-4 that 14 prices - and the "
        "composed buffer byte for byte. Negative controls the harness must "
        "FAIL, one wrong buffer and one wrong end state. 20s is 1s MEASURED "
        "plus the corpus generation and the nasm run",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("weavesession", "soak", py("tests/weavesession.py"), 150.0,
        "WEAVE-SPEC 12.3, 12.3.1: a scripted session driven through the "
        "SHIPPING package under MartyPC - type in a field, press a button, "
        "toggle a check, take a menu command, dismiss an alert - and every "
        "reading diffed against `weavesim --run` given the same events. It "
        "reads facts that are on the glass or in the kernel's own window "
        "table (a meter's fill in pixels, a check's glyph, whether an alert "
        "window exists) and never a transcript, because a transcript is a "
        "claim the program makes about itself and a -DWVHARNESS build would "
        "be a second implementation of the thing under test (12.3.1 says so "
        "at length). It is the only row that exercises the ring, the slice "
        "and the native surface END TO END - weavevm cannot reach any of "
        "them, having no runtime under it. 90s is 55s MEASURED here for one "
        "boot, one navigation, one launch and eleven gestures per adapter, "
        "MEASURED at 135s over two clean runs and 150s over one that lost a "
        "double-click to host load and spent its three navigation retries. "
        "It is not the 90s this row was declared at before it had ever been "
        "run, and a declared figure nobody has taken is the thing this "
        "registry's budgets exist to stop drifting",
        needs=("marty", "cc"), serial=True, timeout=360,
        wants=("build/weave360.img",)),
    Row("weavegrid", "soak", py("tests/weavegrid.py"), 120.0,
        "WEAVE-SPEC 13.1's wave-4 gate: the <grid>, against the model and "
        "against itself. Three things no other row in this family can see. "
        "(1) Every visible BAND is read off the glass by 12.3.2's "
        "consistency rule and compared with weavesim's own band() - 6.9.1's "
        "pinned layout, 5.2.1's display conversion, the justification and "
        "the scroll origin, all at once. (2) The set of bands whose PIXELS "
        "changed across an edit must equal the set whose model text changed: "
        "5.5.1's per-row damage said as a fact about the glass, and a "
        "runtime that repaints the whole grid on every edit passes every "
        "value check and fails only this one (a 20-row page is 291 ms "
        "against one row's 14.5). (3) tests/tpdraw.py's identity for the "
        "grid - the pixels after an incremental edit against the pixels "
        "after a full re-compose of the same state, the re-compose forced "
        "with the arrow keys, which is what catches the XOR selection path "
        "and the band composer disagreeing about which cells the selection "
        "covers. It drives BOTH ways into the store, because they share no "
        "code: `Cider +1` is SHEET's own setCell() through the ring, a "
        "slice and CALLM, and then a formula is TYPED into an empty cell "
        "through os88line, 6.9.3's classification and 6.9.2's compiler into "
        "a 5.6 kind-6 pool slot. 200s is 157s MEASURED here over two "
        "adapters - two boots, two navigations, two launches and ~20 "
        "gestures - taken over three consecutive clean runs at 156.8, 157.4 "
        "and 156.7, with room for the demo growing",
        needs=("marty", "cc"), serial=True, timeout=480,
        wants=("build/weave360.img",)),
    Row("weavegfx", "soak", py("tests/weavegfx.py"), 90.0,
        "WEAVE-SPEC 12.3's pixels-vs-model row, zgfx's shape: every other "
        "gate in this family reads a number or a structure, and none of them "
        "can see a component drawn at the wrong row, a control that draws "
        "nothing at all, or a card whose ink runs outside the content box - "
        "which are precisely a widget library's failure modes (12.4). It is "
        "NOT a golden screenshot, for bootsmoke's reason: it compares the "
        "machine's picture against `weavesim --render`, the oracle 12.1 "
        "makes every differential in this family diff against. Three "
        "assertions per card and two cards - FORM is the widget zoo and "
        "SHEET is the band composer, which draws through GFX_BLIT1 rather "
        "than FONT_RUN - on both 1bpp adapters, because grey rounds to "
        "black there and a drawing change is not done until it has been "
        "looked at on one. The ink-presence half is what makes the text "
        "half honest: an unlearned glyph reads '?' and is skipped, so a "
        "component that drew nothing would otherwise pass a comparison made "
        "entirely of question marks. 240s is 122s MEASURED CLEAN over three "
        "consecutive runs (121, 122) and 190s on the third, which spent "
        "weavesmoke's three navigation retries and then failed - FOUR "
        "sessions is four double-clicks, so this row carries twice "
        "weavesession's exposure to the one thing that flakes in this "
        "family: a double-click whose two presses straddle the kernel's "
        "9-tick window is seen as two FIRST clicks, and on a loaded host "
        "that happens. The retry is weavesmoke's and is not loosened here - "
        "a gate that hid it would hide a host that had really got slower",
        needs=("marty", "cc"), serial=True, timeout=600,
        wants=("build/weave360.img",)),
    Row("weaveprev", "soak", py("tests/weaveprev.py"), 240.0,
        "WEAVE-SPEC 1.7.1 and 12.3: LOOM's PREVIEW PANE against "
        "`weavesim --render --preview`. Wave 7 draws the pane with WEAVE's "
        "own flow walk and WEAVE's own component painter, compiled a second "
        "time into LOOM.WPV - a second RESIDENT segment (1.2.4) - and "
        "NOTHING ELSE IN THIS FAMILY ENTERS THAT MODULE AT ALL: weavegfx "
        "reads the runtime's window and every assertion it makes would pass "
        "with the pane blank. Because the two images run the same TEXT "
        "(apps/weave/wflow.c and apps/weave/wpaint.c are #included rather "
        "than reimplemented, 1.2's 'never a second copy'), a wrong picture "
        "here is the SEAM or the SEGMENT and never the painter - the pane "
        "rect arriving wrong, the module's .bss not zeroed, the caller's DS "
        "not banked, a stale module believed. Those are exactly the failures "
        "a second segment adds and an overlay does not. weavegfx's three "
        "assertions, aimed at the pane; all THREE demo projects, because "
        "SHEET has a <grid> and PONG a <canvas> and 1.7.1's rule is that a "
        "Preview draws those as their frame - which the model was taught in "
        "one flag rather than the test being taught to ignore two "
        "components. Both 1bpp adapters - six sessions, 180 checks. 260s is "
        "239s MEASURED over three consecutive runs (238.7 inside the tier, "
        "238.5 and 238.6 standalone) with a margin for the one thing that "
        "flakes in this family, a double-click whose two presses straddle "
        "the kernel's 9-tick window; the retry is weavesmoke's and is not "
        "loosened here",
        needs=("marty", "cc"), serial=True, timeout=600,
        wants=("build/loom360.img",)),
    Row("weaveone", "soak", py("tests/weaveone.py"), 60.0,
        "WEAVE-SPEC 1.4's 256KB machine, ASSERTED: the family's floor board "
        "holds exactly ONE Weave app, and the second launch is refused while "
        "the first goes on running. It is the one row in this family about "
        "MEMORY rather than about a picture or a number, and the arithmetic "
        "it checks is the one WEAVE-SPEC 1.4 states and nothing else "
        "exercised - wave 5 moved it by one claim and wave 7 found the "
        "document naming the wrong refusal: the second launch never reaches "
        "WEAVE, because a package region is claimed by the KERNEL before the "
        "package runs (SPEC.md 20.1, 21) and WEAVE's is 60,320 bytes, so the "
        "loader answers LD_ENOMEM and the Finder says `Out of memory`. What "
        "is asserted is that byte and not the toast drawn from it, which is "
        "a ~3s transient no polling rate worth having catches; plus that the "
        "first app is STILL THERE, which is kernel/loader.inc's own opening "
        "promise and the thing a refusal that took the running app down "
        "with it would break. MartyPC on a GLaBIOS 256KB machine, because a "
        "machine wanting IBM's ROM cannot boot in this tree; `make "
        "xt-weave-256` is the same question on 86Box and is manual evidence "
        "only (docs/TESTING.md). 90s is 46s measured over three consecutive "
        "runs (46.1 inside the tier, 46.0 and 45.9 standalone)",
        needs=("marty", "cc"), serial=True, timeout=300,
        wants=("build/weave360.img",)),
    Row("weavegame", "soak", py("tests/weavegame.py"), 50.0,
        "WEAVE-SPEC 6.10, 12.3, 14: PONG.WAB under MartyPC, and it asks "
        "wirefps's and wireflick's two questions of a sprite canvas "
        "(SPEC.md 78.9). HOW MANY GFX CALLS A FRAME, read out of WEAVE.WSM's "
        "own frames and blits counters - the only honest way to price a "
        "redraw here (CLAUDE.md: a redraw costs what it CALLS), and 14 "
        "prices a two-sprite frame at 2-4. WHAT THE GLASS SHOWED between the "
        "erase and the draw, sampled once per DISPLAYED frame the way "
        "wireflick does, because m.flicker() waits for a screen to settle "
        "and a running game never does again. AND INPUT OVERRUN, which is "
        "the one of CLAUDE.md's three invisible defects that can be turned "
        "into a number at all: 6.10.6's staging ring counts every record it "
        "could not take, and that counter is asserted at zero. AND THAT "
        "ONTICK FIRED MORE THAN ONCE: PONG's computer paddle is steered from "
        "ontick, and the row reads its y out of the canvas claim before and "
        "after the frames - the module shipped waves 5-7 delivering ONE "
        "ontick per start() (6.10.6) and no counter showed it. No threshold "
        "on TIME - wirefps's rule, that a number which fails a build when a "
        "harness gets slower teaches nobody anything - so the fps is printed "
        "and the FIELD RUN (docs/FIELD-MACHINES.md, WEAVE-PLAN 4.2) is what "
        "turns it into a claim. 50s is 34s MEASURED plus room for the one "
        "navigation retry weavesmoke's own flake can cost",
        needs=("marty", "cc"), serial=True, timeout=300),
    Row("weavepack", "soak", py("tests/weavepack.py"), 1500.0,
        "WEAVE-SPEC 11.1's gate and the one wave 6 closes on: LOOM packs "
        "every demo and every template ON THE MACHINE, the guest's floppy is "
        "flushed to the host, and each .WAB is read back out of it by an "
        "independent FAT12 reader and compared whole. That last part is what "
        "makes the comparison mean anything - without it a scripted session "
        "makes a program save a file and then has to ask the program whether "
        "it worked, which cannot catch the case where the writer and the "
        "reader agree on the same wrong thing. tests/unit/t_lmpack.py packs "
        "the same seven with the HOST cc in four seconds and is the dev "
        "loop; the difference between the two is one word wide (`int` is 32 "
        "bits there and 16 here), so that row proves the logic and this one "
        "proves the arithmetic. Needs `cc` because LOOM is a C package, and "
        "`marty` for the boot. 1,500s MEASURED, and it is eleven LAUNCHES rather "
        "than one session: each project is its own instance (WEAVE-SPEC 1.4) "
        "and they cannot all be open at once, so every one costs a package "
        "load - 55KB of LOOM plus 43KB of LOOM.OVL off an emulated floppy - "
        "and that read is the whole of the time. It is the price of asking "
        "the question on the target rather than on the host",
        needs=("marty", "cc"), serial=True, timeout=3000,
        wants=("build/loom.o88", "build/LOOM.OVL")),
    Row("weavefuzz", "soak", py("tests/weavefuzz.py"), 180.0,
        "a thousand DAMAGED projects through both packers, asking the two "
        "questions a fixture cannot: did they agree about whether it is a "
        "program, and when both said yes are the bytes identical "
        "(WEAVE-SPEC 11.1). Fixed seeds, so a find on Tuesday is still there "
        "on Wednesday. Message TEXT is reported and not asserted, and the "
        "row's own header says why - weavesim scans a whole element before "
        "analysing any of it and LOOM analyses as it goes, so a DOUBLY "
        "broken document makes them name different faults; the single-fault "
        "documents an author types are what tests/weave/packerr/ holds them "
        "to. Measured when it was written: 0 verdict disagreements, 0 byte "
        "disagreements, 93 differing messages in 1,000",
        needs=()),
    Row("weavelat", "soak", py("tests/weavelat.py"), 40.0,
        "SPEC.md 7.3's click-to-action bar with a WEAVE FORM as the load "
        "(WEAVE-SPEC 12.4), measured the way tests/uilat.py measures it - "
        "two memory breakpoints and the cycle counter, because os88mouse's "
        "injection path has a ~0.51 s floor and cannot see 40 ms. The "
        "question it asks is the one 4.10's slice design could get wrong: a "
        "handler runs in ONWAKE without the gfx lock, and a runtime that "
        "took the lock for the slice rather than for the flush would hold it "
        "for 51-154 ms against a 37-70 ms bar. That is invisible in every "
        "functional test in this family and it is exactly what this row is "
        "for. 120s is uilat's own figure: the same shape, one more launch",
        needs=("marty", "cc"), serial=True, timeout=480,
        wants=("build/weave360.img",)),
    Row("assocglyph", "soak", py("tests/assocglyph.py"), 30.0,
        "A DECLARED extension's icon is right from a COLD mount (SPEC.md"
        "54.7.3).",
        needs=("marty",), serial=True),
    Row("assocwake", "soak", py("tests/assocwake.py"), 30.0,
        "SPEC.md 54.10: a document launch draws the PROGRAM'S WINDOW first, "
        "and only then reads the document. The instrument is a breakpoint on "
        "assoc_handover - the guest cannot have read the file yet at that "
        "instruction - and the pixels inside the new window's frame are what "
        "says wm_show already drew it. The 'Decoding GIF' toast (42.14) and "
        "the picture arriving are what stop it passing vacuously.",
        needs=("marty",), serial=True),
    Row("assocopen", "soak", py("tests/assocopen.py"), 40.0,
        "SPEC.md 22.13.2: opening a DOCUMENT draws no pixel of the Disk "
        "window. The instrument is a breakpoint on fm_repaint, and the "
        "FOLDER open beside it is the control that says the breakpoint "
        "fires at all.",
        needs=("marty",), serial=True),
    Row("fontview", "soak", py("tests/fontview.py"), 60.0,
        "SPEC.md 90: an F88 association launches FONT VIEWER with that family "
        "selected, every installed face is listed, typing edits the specimen, "
        "and both arrow and mouse selection finish loading another face.",
        needs=("marty",), serial=True),
    Row("fmcommit", "soak", py("tests/fmcommit.py"), 80.0,
        "SPEC.md 22.13.3: a committing keystroke redraws the Disk window and "
        "a REFUSED character does not. fm_onkey banks fm_editkey's answer "
        "across the modal-dialog test, because `cmp word [x], 0` clears the "
        "carry and left that `jc` dead - so Delete removed a file and left "
        "its row on the glass. The instrument is a breakpoint on fm_repaint "
        "(assocopen's), and the refused comma is the leg that says the fix "
        "did not buy the repaint back with one nobody owes.",
        needs=("marty",), serial=True),
    Row("multiseg", "soak", py("tests/multiseg.py", "1440"), 20.0,
        "SPEC.md 20.12: a package carries its parts in its OWN FILE and loads "
        "them ITSELF. The kernel parses none of it - all it learns is flags "
        "bit 2, that the file is longer than the image on purpose, and it "
        "hands the entry proc the name of the file it came out of; "
        "apps/os88parts.inc is the rest, and it is package code. THREE "
        "INDEPENDENT CHECKS PER PART, because a segment number proves a claim "
        "was made and not that it was filled: the signature the primary reads "
        "out of the part's own bytes, a far call to <part>:0 answering a "
        "value only that module computes, and the module summing its data "
        "area with a ROTATING add against the figure the assembler computed "
        "over the same generated bytes - a plain sum would pass on a "
        "transposition, which is what a misaligned read actually produces. "
        "SEVEN PARTS: three filed modules, a required 8KB scratch part that "
        "costs no disk at all, a 600KB OPTIONAL one that must be REFUSED, "
        "an OP_XMS one that on every machine here comes back as an "
        "ordinary conventional claim - the FALLBACK, and what makes OP_XMS a "
        "hint rather than a mode (tests/msegxms.py is the other half, on "
        "QEMU, where there is a store) - and an OP_LAZY one that must NOT be "
        "here at all until something fetches it (tests/mseglazy.py drives "
        "that cycle; this row only says it did not happen by itself). The "
        "verdict is the window "
        "TITLE, read out of the package's segment rather than off the glass. "
        "MEASURED with the kernel's bit-2 exception disabled: ld_status 2, "
        "`Bad package`. Needs `make mseg`.",
        needs=("marty",), serial=True, wants=("build/mseg360.img",)),
    Row("mseg360", "soak", py("tests/multiseg.py", "360"), 40.0,
        "...and the same package off a 360KB disk, where it is NOT a "
        "duplicate. A part begins on a 512-byte boundary in the FILE and "
        "OSAPI_FILE_READ_AT will only BEGIN a read on a CLUSTER boundary - "
        "512 bytes at 1.44MB, 1KB here, and up to 32KB on a volume neither "
        "of these is, which is why the file cannot simply be laid out to "
        "suit. So the read starts BELOW the run and op_claim's head slack is "
        "what puts each part's segment where it belongs (SPEC.md 20.12.2). "
        "MSEG's image is padded to an ODD number of sectors on purpose so "
        "the case exists at all - unpadded its first part landed at 1,024, "
        "aligned on both geometries, and this row passed without ever "
        "running the arithmetic it is for; the slack is asserted (512 here, "
        "0 at 1.44MB) so that cannot recur. MEASURED with the slack not "
        "added back: this row answers `MSEG 0/6 BAD` and the 1.44MB row "
        "still answers `MSEG 6/6 OK`, which is why neither alone is the "
        "gate. Needs `make mseg`.",
        needs=("marty",), serial=True, wants=("build/mseg360.img",)),
    Row("msegz", "soak", py("tests/multiseg.py", "1440", "--comp"), 60.0,
        "SPEC.md 20.12.7: the same seven parts with two of them COMPRESSED. "
        "It is one source built twice - `-DMSEG_COMP` puts OP_COMP on parts 0 "
        "and 2 - so every assertion the row above makes has to come out "
        "IDENTICAL, which is a stronger statement about op_unpack than any "
        "new check would be: the three per-part proofs are unchanged, and so "
        "are the segments each part lands at. THE MIX IS THE POINT. Part 0 is "
        "the FIRST row, so its expansion starts at the very base of the "
        "carve; part 2 is in the MIDDLE, so a plain row is expanded past on "
        "each side; parts 1 and 5 are plain, so they take op_unpack's `move "
        "it down` arm - and part 5 is the OP_XMS fallback, which is a plain "
        "row that only joins the carve at run time. MEASURED: with the "
        "format byte left unread, op_unpack handed the decoder the low half "
        "of a SEGMENT as the format and every stream was refused - `A part "
        "will not unpack` on a package whose bytes, lengths and pointers "
        "were all correct. Needs `make msegz`.",
        needs=("marty",), serial=True, wants=("build/msegz.img",)),
    Row("msegz360", "soak", py("tests/multiseg.py", "360", "--comp"), 60.0,
        "...and off a 360KB disk, where op_claim's head slack is 512 rather "
        "than 0 - so op_unpack's walk starts a paragraph run into the claim "
        "and not at its base. It is mseg360's argument applied to the new "
        "arithmetic: the slack is the one term that is zero on the geometry "
        "everything else is tested on, and a walk that ignored it would pass "
        "at 1.44MB and put every part 512 bytes low here. Needs `make msegz`.",
        needs=("marty",), serial=True, wants=("build/msegz360.img",)),
    Row("msegnomem", "soak", py("tests/msegnomem.py"), 40.0,
        "SPEC.md 20.12.3: a package that cannot fit is refused BEFORE IT "
        "READS ANYTHING, and this row measures that rather than asserting it. "
        "MSEGBIG is MSEG's twin - the same three filed parts and one more, a "
        "REQUIRED 640KB scratch part, which is the whole of the biggest "
        "machine here (512 was the first figure and a 640KB XT GRANTED it, so "
        "the row passed on a mechanism it had never run). The instrument is "
        "dsk_dbg_sec, the kernel's own count of SECTORS transferred, so this "
        "row BUILDS A DISKCNT KERNEL and puts build/ back afterwards. "
        "MEASURED: the refusal moves 9 sectors and the successful launch "
        "21, and the margin is op_read's 13-sector carved run - with the "
        "odds against it, because the refusal goes first on a cold volume "
        "and the success second on a warm one. SECTORS AND NOT CALLS: this "
        "row asserted `at most two int 13h calls` until wave 4 padded both "
        "images to five sectors, which put MSEG's image across a cylinder "
        "boundary, and the driver split one run into two for a launch that "
        "read no extra byte - the refusal then cost the same 3 calls as the "
        "success while moving 12 sectors against 21, and wave 5's seventh "
        "sector moved them again to 1 and 3. The count is a fact about where "
        "the file sits on the disk. The heap is BYTE-FOR-BYTE untouched "
        "across the refusal, which "
        "is stronger than the kernel-side design could manage: it tried the "
        "claim and mem_claim sheds purgeable caches before refusing (SPEC.md "
        "50.6.2), where op_load asks OSAPI_MEM_AVAIL and a question costs "
        "nothing. VERIFIED by A/B - made to try the claim instead of asking, "
        "the table goes 4 claims to 2 and this row names it. Needs `make "
        "mseg`.",
        needs=("marty", "nasm"), serial=True, wants=("build/mseg.img",)),
    Row("c64part", "soak", py("tests/c64part.py"), 30.0,
        "THE FIRST REAL CONSUMER of the parts standard (SPEC.md 20.12, and "
        "C64-SPEC 1.4): C64.ROM - 20,480 bytes of KERNAL, BASIC and "
        "character generator - was a SIDECAR a file copy could separate from "
        "the program it is useless without, and it is part 0 of C64.O88 now. "
        "The port carried a whole halted-machine state to say so when it went "
        "missing: a permanent status row naming the file, a four-line notice "
        "with its own expose repair, three greyed menu items and a SECOND "
        "host-test process. All deleted, because a greying may not outlive "
        "its reason (SPEC.md 47). FIVE ASSERTIONS - C64.ROM is not in the "
        "folder, read out of the guest's own listing; the package declares "
        "parts and its image is smaller than its file; it launched; "
        "os88_part_seg(0) is the segment the C put in c64_m.romseg, which is "
        "the standard's answer and the package's use of it; and five 16-byte "
        "windows of the ROM in the guest equal build/c64-rom/C64.ROM - "
        "including THE LAST SIXTEEN BYTES OF THE PART, because a carve one "
        "sector short reads perfectly at the front. It deliberately does not "
        "assert that the KERNAL BOOTS: measured by A/B against the "
        "unconverted package, the 6510 on this branch runs and never writes a "
        "byte of its own RAM, which is not this wave's to fix and would be a "
        "row failing for a reason it does not name. VERIFIED TO FAIL - a "
        "package truncated by one sector gives ld_status 4 and no window, "
        "because op_read now refuses a run that arrives short. Needs `make "
        "c64disk`, so it needs the C toolchain.",
        needs=("marty", "cc"), serial=True,
        wants=("build/c64.img",)),
    Row("apple2part", "soak", py("tests/apple2part.py"), 30.0,
        "c64part's shape one machine along (SPEC.md 20.12, APPLE2-SPEC 1.5), "
        "and the difference is that APPLE2 never had a sidecar to convert: "
        "the 14,848 bytes of Applesoft, the Autostart Monitor, the character "
        "generator and the Disk II boot ROM have been part 0 since wave 1, so "
        "what this row defends is that the shape STAYED that way. SIX "
        "ASSERTIONS - APPLE2.ROM is not a file in the folder, read out of the "
        "guest's own listing (and WELCOME.BAS is, because the folder is the "
        "binding shape); the package declares parts, its image is smaller "
        "than its file and the one part is an ASSET of exactly 14,848; it "
        "launched; os88_part_seg(0) is the segment the C put in a2_m.romseg "
        "and it is at or above $0D00, below which the core's `romseg - "
        "($D000 >> 4)` fetch bias underflows silently; THREE 16-byte windows "
        "of the ROM in the guest equal build/apple2-rom/APPLE2.ROM - "
        "including the LAST SIXTEEN BYTES, because a carve one sector short "
        "reads perfectly at the front - and the RESET vector at $FFFC reads "
        "$FA62, which is the one number that says AUTOSTART Monitor rather "
        "than some other Apple II ROM; and BOTH DISPLAY TABLES EXIST after "
        "os88_main and before any wake, which is the negative control for "
        "keeping the chargen decode and the 7-bit reverse table off the "
        "overlay (APPLE2-SPEC 7.3) - a disk with no APPLE2.OVL has to be a "
        "program whose MENUS refuse, not a window that draws nothing. It also "
        "says the 6502 is RUNNING and not jammed, and stops there: what the "
        "machine puts ON THE GLASS belongs to the driven QMP runs and to "
        "a2uitest. Needs `make apple2disk`, so it needs the C toolchain and "
        "the pinned ROM fetch.",
        needs=("marty", "cc"), serial=True,
        wants=("build/apple2.img",)),
    Row("mseglazy", "soak", py("tests/mseglazy.py"), 50.0,
        "SPEC.md 20.12.4: an OP_LAZY part is NOT READ AT LOAD and can be "
        "given back. That is the first half of goal 3 - `load only some "
        "minimal amount` - where msegnomem is the second, and MSEG's part 6 "
        "is the biggest of its five modules on purpose, because what lazy "
        "buys is measured in the sectors the launch did not move. A KEY "
        "fetches it and not the entry proc: a fetch inside the entry happens "
        "during the launch, so its sectors would be indistinguishable from "
        "the carve's. FIVE ASSERTIONS - it is absent after the launch and "
        "MSEG's own verdict agrees that is CORRECT (it checks part 6 against "
        "what it asked for, so presence would be the failure); the carve, "
        "read out of the guest as [op_first]/[op_secs], ENDS BEFORE part 6's "
        "first sector, which is the structural half and the one that cannot "
        "be faked; the key moves at least the part's six sectors "
        "(dsk_dbg_sec, so this row builds a DISKCNT kernel) and the three "
        "module checks then pass on it; a second key gives it back with the "
        "claim table BYTE-FOR-BYTE what it was, which is what makes lazy a "
        "saving rather than a postponement; and a third fetches it again. "
        "VERIFIED TO FAIL, and how it failed is the argument for assertion "
        "2: with op_size made to size a lazy row like any other the carve "
        "runs to sector 25 instead of 19 and reads the part at load - and "
        "assertion 1 does NOT notice, because op_seg answers a lazy row out "
        "of the row itself and that is still 0. Presence is what the package "
        "was told; the carve is what the disk did. Needs `make mseg`.",
        needs=("marty", "nasm"), serial=True, wants=("build/mseg.img",)),
    Row("msegxms", "soak", py("tests/msegxms.py"), 50.0,
        "SPEC.md 20.12.4: an OP_XMS part really goes ABOVE 1MB. Every MartyPC "
        "row proves the FALLBACK - an 8088 has nothing up there, so the part "
        "comes back as an ordinary conventional claim, which is what makes "
        "OP_XMS a HINT and not a mode - and this is the other half. WHY QEMU: "
        "docs/TESTING.md's closed list, entry 1 - MartyPC cannot host "
        "extended memory at all, so there is no `prefer MartyPC` to weigh; it "
        "borrows tests/xmcheck.py's boot and block-table reader for the same "
        "reason. FOUR ASSERTIONS and the last is what makes the first three "
        "mean anything: op_seg answers ZERO and op_lin a non-zero linear "
        "base; MSEG says `MSEG 6/6 OK`, which means it brought the part back "
        "DOWN through OSAPI_XMEM_COPY and its rotating sum matched; its own "
        "ms_xwhere says 'X' and not 'C', because the bytes are the same "
        "bytes either way and the sum alone cannot tell the two paths apart; "
        "and the block is owned by the INSTANCE rather than XM_OWN_KERN - "
        "xm_alloc attributes through inst_caller, and the loader already "
        "brackets the entry call with the instance's stamp (SPEC.md 41.5.1), "
        "which is what lets a package claim for itself with NO kernel change. "
        "That last one is silent otherwise: a block nobody frees looks "
        "exactly like a block nobody claimed. VERIFIED by A/B - with op_load "
        "made to believe there is no store, the title still says `MSEG 6/6 "
        "OK` and three of the four assertions fire. It CAUGHT TWO DEFECTS on "
        "its first green run: op_xload walked op_xlin as its copy cursor and "
        "put it back by subtracting the BLOCK's size rather than the SPAN's, "
        "so every part came out 512 bytes low and the failure path freed an "
        "address that was never claimed; and MSEG itself passed the copy "
        "direction in DI, which is also its part-loop counter, so the entry "
        "proc never returned. Needs `make mseg`.",
        needs=("qemu",), serial=True,
        wants=("build/os8088.img", "build/mseg.img")),
    Row("pkgbig", "soak", py("tests/pkgbig.py"), 60.0,
        "SPEC.md 19.1's package-size rule and the loader half that makes it "
        "safe. APP_MAX_SIZE bounds the primary SEGMENT's image+bss; it "
        "stopped bounding the FILE when a package could carry parts, so the "
        "mount types a *.O88 up to PKG_FILE_HI (1MB) and ld_run_body step 1 "
        "tests the staged size's HIGH word before it trusts the low one. "
        "LIFTING THE MOUNT'S RULE ALONE IS A DEFECT and this is what catches "
        "it: a 70KB file's low word is 4,608, a plausible small package, so "
        "without the guard the loader sizes a region from a wrapped length "
        "and answers `Bad package` about a file whose only fault is its size "
        "(SPEC.md 21.4's hazard). TWO FILES AND THE PAIR IS THE EXPERIMENT - "
        "70,144 bytes must type 1 and be refused LD_EBIG, 1,048,576 must "
        "type 0 and be refused LD_EBAD; BIGPKG alone is also what a rule "
        "that types everything looks like, and HUGE alone is also what the "
        "old `high word == 0` rule looks like. The instrument is [ld_status] "
        "read out of the guest, so it answers for all three adapters out of "
        "one run. Needs `make pkgbig`, which uses os88disk.py --raw: the "
        "fixtures are *.O88 files that are deliberately not packages, and "
        "validate_o88 exists to make those unbuildable. 40s measured.",
        needs=("marty",), serial=True, wants=("build/pkgbig.img",)),
    Row("pkgfence", "soak", py("tests/pkgfence.py"), 60.0,
        "SPEC.md 21 steps 4 and 6's WRITE BOUND: ld_check_hdr's `image + bss` "
        "fence. Both operands are separately bounded at APP_MAX_SIZE, so "
        "their sum reaches 0x1E000 - SEVENTEEN BITS - and the compare that "
        "used to stand there read a WRAPPED value, with the comment on the "
        "line stating the defect as its own proof (`img+bss <= 0x1E000: no "
        "wrap`). The repair is `add dx, ax / jc .toobig` and it shipped in "
        "size pass 3 with NO ROW BEHIND IT: nothing else in the tree can see "
        "this, because every other gate loads a well-formed package and "
        "os88pkg.py refuses to build a malformed .O88 at all - which is the "
        "point, since the input this is about comes off a disk (SPEC.md 19). "
        "TWO FILES AND THE PAIR IS THE EXPERIMENT - BSSWRAP.O88 is image = "
        "bss = 0xF000, whose sum wraps to 0xE000, BELOW the bound, for a 56KB "
        "claim and 4KB past it; BSSWORST.O88 is image = 0xF000, bss = 0x1001, "
        "whose sum wraps to 1, for a ONE KILOBYTE claim and 60,416 bytes "
        "written through whatever mem_claim_hi placed under it, which is a "
        "resident package's code because it places top-down. The first alone "
        "under-states the fault fifteenfold and the second alone looks "
        "contrived. Both must answer LD_EBIG. A regression does NOT answer "
        "cleanly - it corrupts the guest's heap and this row times out, which "
        "is inherent: the write the fence bounds has already happened by the "
        "time anything could report it. The instrument is [ld_status] read "
        "out of the guest, so it answers for all three adapters out of one "
        "run. Shares build/pkgbig.img with the pkgbig row - `make pkgbig`.",
        needs=("marty",), serial=True, wants=("build/pkgbig.img",)),
    Row("clipkeep", "soak", py("tests/clipkeep.py"), 40.0,
        "SPEC.md 11.96.18: a wholly covered window keeps its raise cache when"
        "it arms a clip, and a partly covered one still loses it.",
        needs=("marty",), serial=True),
    Row("fcpcopy", "soak", py("tests/fcpcopy.py"), 70.0,
        "SPEC.md 22.3-22.5: Cut/Copy/Paste actually moves a file AND a folder "
        "tree. Nothing exercised kernel/filecp.inc at all until this row - a "
        "whole-file pass over the copy engine could be green on assembly, "
        "stkbalance, ovlchk and every size guard while leaving a machine that "
        "cannot copy a file. The load-bearing assertion is the THIRD one: "
        "os88disk --verify walks the volume the engine left behind, because a "
        "stranded cluster or a cross-linked chain looks perfectly fine in the "
        "guest's own listing, which is drawn from the structures that are "
        "wrong. Runs on the 1.44MB disk: the 360KB one is 354 of 354 clusters "
        "in use after one paste, so the folder copy correctly refuses there "
        "with FERR_FULL and the row would be measuring the geometry. "
        " because the script can shell out to `make small` when it "
        "is pointed at kern_small (the fcpsmall row below), and the runner "
        "gives a building row the tree to itself.",
        needs=("marty",), serial=True,
        wants=("build/small.img", "build/smallapps.img")),
    Row("fcpsmall", "soak",
        # OS88_APPSIMG IS NOT OPTIONAL HERE. The default is build/apps.img,
        # a plain 1.44MB FAT12 declaring NINE FAT sectors - and kern_small's
        # DSK_FAT_SECS is 2, so mount rule 10 refuses it before a byte is
        # read. The row then met an empty B: and failed three steps later
        # saying "'MEDIA' is not in this folder". build/smallapps.img is the
        # disk `make small` tells you to pair with build/small.img, and it is
        # --fatcap 2 as of the same commit as this line - it was not, which
        # is why this arm could never have passed.
        ["env", "OS88_DEFINES=KERN_SMALL", "OS88_BUILD=build/smallk",
         "OS88_SYSIMG=build/small.img",
         "OS88_APPSIMG=build/smallapps.img"] + py("tests/fcpcopy.py"), 70.0,
        "...and the SAME drive against kern_small, where Cut/Copy/Paste is an "
        "on-demand module (SPEC.md 22.3, docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2) "
        "rather than resident code. It is a different engine to reach: every "
        "call the image makes to the kernel is a far one through an xf_ entry, "
        "the shared register epilogues are copies inside the image because a "
        "`jmp kretc_cx` would return through a near `ret` against a far frame, "
        "and the whole thing is read off the disk by mod_need and given back "
        "at the end of each operation. NONE of that is exercised by the row "
        "above, which runs the resident build - and the first time this one "
        "ran it caught FILECP.DRV missing from the floppy entirely, with every "
        "build step green and the machine booting. It builds its own image "
        "(`make small`) for smallboot's reason.",
        needs=("marty",), serial=True,
        wants=("build/small.img", "build/smallapps.img")),
    Row("cppromise", "soak", py("tests/cppromise.py"), 50.0,
        "SPEC.md 31.12: the Control Panel promises per PAGE, and the clock"
        "page is the one that cannot.",
        needs=("marty",), serial=True),
    Row("cpup", "soak", py("tests/cpup.py"), 41.3,
        "SPEC.md 13.8.3: the Control Panel acts on the RELEASE, not the"
        "press.",
        needs=("marty",), serial=True),
    Row("dtfield", "soak", py("tests/dtfield.py"), 50.0,
        "SPEC.md 37.93: the Date/Time field editor still edits now that it "
        "runs from inside CTRL.DRV. The day clamp is the load-bearing leg - "
        "cw_clk_mlen is the only call clk_fld_adj makes out of the image, so "
        "a bad thunk shows up there and nowhere else on the page.",
        needs=("marty",), serial=True),
    Row("dtwrite", "soak", py("tests/dtwrite.py"), 30.0,
        "SPEC.md 37.94: the hardware clock is written by the Control Panel's "
        "CLOSE and no longer drained off the system tick. [clk_dirty] "
        "SURVIVING ~54 ticks with the panel open is the leg that matters - "
        "on the old kernel ui_task spent it inside 55 ms. No rung is reached "
        "here: a 5150 has no RTC and MartyPC models no clock card, so the "
        "writers themselves are a QEMU session (see the docstring).",
        needs=("marty",), serial=True),
    Row("saver", "soak", py("tests/saver.py"), 70.0,
        "the animated screen saver end to end (SPEC.md 79): every mode draws, "
        "the overlay is loaded and freed, the wake puts the whole desktop back "
        "including the bar and the dock, no block is left in the menu bar, and "
        "all three fallbacks reach the blanker with the framebuffer untouched",
        needs=("marty",), serial=True),
    Row("fishfit", "soak", py("tests/fishfit.py"), 20.0,
        "does the most expensive sea the generator can roll still fit ONE "
        "TICK? (SPEC.md 79.5.8). Sea life is the one saver mode that ever "
        "cost more than the 54.93 ms a task_sleep(1) parks for, and what that "
        "cost is not a slow mode: 18.2 / (floor(work / 54.93) + 1) is 18.2 a "
        "millisecond under and 9.1 a millisecond over, with nothing between. "
        "saverate is the KERNEL half of that - a mode asleep while it is "
        "behind - and cannot catch this half, because a sea that legitimately "
        "costs 70 ms is slow and busy and passes it. The assertion is the "
        "PASS, in guest cycles between two sv_step entries, with all four "
        "swimmers forced to the larger size: one roll in sixteen, so a test "
        "that waited for one would usually measure something cheaper.",
        needs=("marty",), serial=True),
    Row("fishedge", "soak", py("tests/fishedge.py"), 150.0,
        "does sea life leave the reserved strip at the right edge DARK on "
        "Hercules? (SPEC.md 79.5.10). SPEC.md 79.5.9 places the field's "
        "column-0 shimmer in 86Box's plain Hercules renderer rather than in "
        "this kernel - the mark is one row DOWN from the right edge, which no "
        "write here can reach - and 79.5.10 is the product answer to it: an "
        "unlit column reads as the edge of the monitor and a shimmering one "
        "does not, so the mode reserves SV_HEDGE pixels and never lights "
        "them. THE ASSERTION IS AN A/B AND HAS TO BE: a sea whose swimmers "
        "never went near the edge leaves the strip dark too and reads exactly "
        "like a pass, so the same forced sweep runs twice - once as the "
        "driver armed it, once with [sv_hlim] poked to 0, which is the state "
        "every other adapter is in - and the strip must be clean under the "
        "first and dirty under the second. The CGA leg asserts the strip is "
        "NOT armed there, which is what keeps gfx_blit1's own right clip the "
        "only cut on the two adapters with no artifact to hide.",
        needs=("marty",), serial=True),
    Row("saverate", "soak", py("tests/saverate.py"), 50.0,
        "is a saver mode ASLEEP while it is behind? (SPEC.md 79.5.7, 8.1.2.4). "
        "ui_task's task_sleep(1) quantises a deadline polled once a pass to "
        "whole ticks, so a mode whose pass runs a millisecond into the next "
        "one drops to the divisor below rather than to its cost - sea life "
        "measured 12.0 fps swinging 9.2-17.8 with 37.2% of the machine "
        "HALTED. The assertion is NOT a frame rate, which cannot tell an "
        "expensive sea from a quantised one: it is slow AND halted, which no "
        "content can produce. The other three modes are the control and are "
        "counted off [sv_due], which cannot see a re-anchored mode - so they "
        "catch a mode that stopped drawing and not one that was quantised.",
        needs=("marty",), serial=True, alone=True),
    Row("deskbench", "soak", py("tests/deskbench.py"), 180.0,
        "THE STANDARD BUSY DESKTOP, priced: what a full-screen redraw, a "
        "window move and a raise cost with four windows open (PERFORMANCE.md "
        "Part 3). A measurement, not a gate - it asserts its own SCENE and "
        "prints numbers. `--all` runs one per adapter.",
        needs=("marty",), serial=True, alone=True),
    Row("arkpuwipe", "soak", py("tests/arkpuwipe.py"), 80.0,
        "Does a capsule the blit REFUSED leave a streak behind it? (SPEC.md "
        "44.10.6.2). VGA on purpose - on CGA ARK_PUFALL floors to 1 and the "
        "one vacated row is the capsule's BLACK top edge on a BLACK playfield, "
        "so the broken build scores zero. `--small --img build/small360.img` "
        "is trigger A, and wants `make small` first.",
        needs=("marty",), serial=True),
    Row("cycweb", "soak", py("tests/cycweb.py"), 40.0,
        "Does the claw eat the web it slides over? (SPEC.md 67.5.3.1)",
        needs=("marty",), serial=True),
    Row("cycfire", "soak", py("tests/cycfire.py"), 50.0,
        "Does holding the mouse button repeat the gun, and does a press on "
        "somebody else's window leave it alone? (SPEC.md 67.11.3)",
        needs=("marty",), serial=True),
    Row("bootstatus", "soak", py("tests/bootstatus.py"), 90.0,
        "Does the boot say WHAT it is doing, not only how far? (SPEC.md 15.6)"
        " Builds a disk whose SYSTEM.CFG wants two drivers, then reads the "
        "composed line out of the overlay AND hashes the pixel band under the"
        " bar, on both 1bpp adapters",
        needs=("marty", "nasm"), serial=True, wants=("build/ether360.img",)),
    Row("blobsum", "soak", py("tests/blobsum.py"), 60.0,
        "Does a SHORT READ of stage 2's blob halt instead of executing what "
        "landed? (SPEC.md 2.9.7) Blanks one sector in the middle of it - the "
        "failure that is not a disk error, because stage 2 and the loading "
        "screen are in the sectors that DID arrive",
        needs=("marty",), serial=True),
    Row("postboot", "soak", py("tests/postboot.py"), 20.0,
        "Does the machine survive its FIRST DISK ACCESS AFTER THE DESKTOP? "
        "(SPEC.md 2.9.5.1) Every other boot row in this file stops at the "
        "first frame, which is how a kernel whose next int 13h jumped into "
        "cold_entry passed all of them",
        needs=("marty",), serial=True),
    Row("cylrun", "soak", py("tests/cylrun.py"), 10.0,
        "Did the kernel load actually CROSS A HEAD? (SPEC.md 18.93.3) "
        "boot_cylrun at 0060:0004 is written on the one path where the "
        "cylinder bound, the 8088 gate and the canary all held - and 18.93's "
        "reload is what makes losing all three look like a normal boot",
        needs=("marty",), serial=True),
    Row("splashbar", "soak", py("tests/splashbar.py"), 40.0,
        "Does the progress bar ADVANCE during the kernel load? (SPEC.md 15, "
        "15.3.1) The counter AND the lit width of the trough, sampled a frame "
        "at a time - a bar that parked at 44% for the whole load passed every "
        "other row in this file and was found by somebody watching it",
        needs=("marty",), serial=True),
    Row("splashspin", "soak", py("tests/splashspin.py"), 20.0,
        "Does the logo turn on the WALL CLOCK? (SPEC.md 15.3.6) The composed "
        "angle checked against the guest's own BIOS tick on every frame it "
        "changes, and the rate compared either side of the notch rate "
        "changing - a stopped tick parks the logo at one angle and the "
        "machine still boots, so no screendump in this tree would notice",
        needs=("marty",), serial=True),
    Row("bootfloor-ab", "soak", py("tests/bootfloor.py"), 420.0,
        "SPEC.md 2.7.1's floor, both sides, both kernels: RAMKB at the floor "
        "must reach a DESKTOP - everything the bound is computed from is "
        "downstream of the refusal - and one KB under it must print RAM. "
        "kern_small is the half that matters: guard 5 has asserted it boots "
        "on 128KB since the split and stage 1 refused it at 129 until 2.7.1, "
        "which no host-side row could have noticed",
        needs=("marty",), serial=True),
    Row("dljunk", "soak", py("tests/dljunk.py"), 210.0,
        "SPEC.md 2.9.11's DL check, both ways: a BIOS that never set DL left "
        "0x61 in it and every int 13h named a unit that is not there, which "
        "is `Disk error` since the first commit (docs/FIELD-NOTES.md 36). "
        "DLJUNK=0x61 must reach a desktop and DLJUNK=1 - a legal unit the "
        "check must LEAVE ALONE, on a machine whose drive 1 is empty - must "
        "not: without the second half a sector that ignored DL outright "
        "would pass the first",
        needs=("marty", "nasm"), serial=True, timeout=420),
    Row("fatwpin", "soak", py("tests/fatwpin.py"), 120.0,
        "Is the kernel's own FAT window somebody's PIN, and only ever one "
        "volume's? (SPEC.md 18.8.3) FAT_SEG used to be a fallback owned by "
        "nobody, so a machine whose volumes all won heap claims reserved "
        "4,608 bytes it never touched and bought the same window again out "
        "of the arena. Asserts the saving (no MEM_K_FATW record at all with "
        "only A: mounted), the safety property (the mounted volume always "
        "has a home - a homeless one is pointed at FAT_SEG and mounts into "
        "it WITHOUT demoting the holder, which 18.8.2's signature cannot "
        "catch because two os8088 floppies of one geometry have identical "
        "boot sectors), FATWNONE=1's ping-pong, where the pin is the "
        "only home there is and must follow the mount, and 18.8.4's SHED - "
        "tests/heapfrag fills the heap at the ordinary rank, which outranks "
        "MEM_P_FATW's MED, and the LIVE window must come back as the pin "
        "with [dsk_fatw0] invalidated, not a pointer into freed memory",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("vgadirty", "soak", py("tests/vgadirty.py"), 30.0,
        "Does vid_setmode leave the VGA framebuffer black whatever the ROM "
        "did? (SPEC.md 39.23) Builds a VGADIRTY=1 kernel, which fills A0000 "
        "in the one window a machine cannot - after the ROM's mode set and "
        "before ours - and asserts the loading screen comes up on black",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("ps2mouse", "full", py("tests/ps2mouse.py"), 10.0,
        "Does the PS/2 mouse reach the pointer, and does the KEYBOARD survive "
        "the handshake? (SPEC.md 9.9) -serial none, so no UART probes present "
        "and the aux port is the only pointing device: mou_p2st 9, port 04, "
        "line FF, ptr 1, and the pointer landing on the EXACT requested pixel, "
        "then every live menu-bar title opening the cell under that PS/2 "
        "coordinate. This is the statement about the sign handling, 9.9.3's "
        "Y inversion and the bar hit test that nothing else here makes. Then "
        "six keys must advance "
        "the BIOS buffer by twelve bytes, because both halves of the probe are "
        "a chance to take a byte from int 09h. QEMU by name on CLAUDE.md's "
        "closed list - MartyPC is an 8088 and has no 8042 to test",
        needs=("qemu", "nasm"), serial=True, timeout=420,
        wants=("build/os8088.img", "build/apps.img")),
    Row("vmmouse", "soak", py("tests/vmmouse.py"), 45.0,
        "The VMware absolute pointer (SPEC.md 9.11), the browser's grabless "
        "mouse - and the one CI gate a browser-only feature gets. QEMU's pc "
        "machine carries a vmport and a vmmouse by default, so VMMOUSE.DRV's "
        "backdoor probe succeeds here exactly as it does under v86. It boots "
        "build/vmmouse.img and NOT os8088.img, and since SPEC.md 9.11.7 that "
        "is a KERNEL difference and not only a settings one: the whole "
        "resident half is inside %ifdef KERN_EMU, so the shipped kernel has "
        "no row for SYSTEM.CFG's bit 5 to tick and does not carry "
        "VMMOUSE.DRV on its disk at all. `make vmmousetest` builds the "
        "kern_emu kernel (build/emuk/) onto a disk whose SYSTEM.CFG has bit 5 "
        "set, ether360.img's shape; build/emu.img is the same pair as a "
        "product. vmport ON and "
        "-serial none, so the backdoor is the only pointing device. Asserts "
        "cpu_tier 2 (vmm_boot_x refuses to READ the image below it, the last "
        "CPU gate in the tree), vmm_on 1, mou_port 6 (MOU_VMROW, so "
        "mou_lockon retired the serial rows), then absolute positions "
        "injected through vmmouse landing within a few px - the sign and axis "
        "handling that a boot-state read cannot see - and a drag through a "
        "menu, which is what proves the task_yield service point. QEMU by "
        "name on CLAUDE.md's closed list - MartyPC has no backdoor"
        ". SOAK and not full: a browser-only pointer on a THIRD kernel, and "
        "it drags `make vmmousetest` - a whole kern_emu build - into the "
        "tier's prebuild for it. ps2mouse keeps pointer-and-keyboard "
        "covered on the kernel that ships",
        needs=("qemu", "nasm"), serial=True, timeout=420,
        wants=("build/os8088.img", "build/apps.img", "build/vmmouse.img")),
    Row("wirezone", "soak", py("tests/wirezone.py"), 50.0,
        "Does the desktop SERVICE zone arrive with its driver and LEAVE with "
        "it? (SPEC.md 26.7) The kernel's half of the Wire is a generic zone a "
        "driver registers - no glyph, no caption, no launch name in the "
        "kernel - so a machine with no card pays one compare. Boots "
        "`make ethertest`'s disk with an ne2k_isa, asserts [desk_svc_seg] is "
        "set and the zone's rect HAS A PICTURE IN IT, then unticks Ethernet "
        "on the Control Panel's Drivers page - the one user route to a "
        "detach - and asserts the segment is 0 and the rect is bare desktop "
        "with NO STALE PIXELS; then the shipped os8088.img with no NIC, where "
        "both must be so from the start. The measure is the LONGEST "
        "HORIZONTAL RUN of one colour in the rect: the desktop is a perfect "
        "50% dither so its longest run is 1, and anything drawn over it is "
        "solid somewhere - 36 px against 1 px measured, which separates the "
        "two states by more than a tuned threshold could. It is the only "
        "thing in the tree that reaches wz_withdraw, desk_zmark's delete edge "
        "and the `inc byte [desk_zhw]` that covers the ordinal past the last "
        "volume, and the bug they guard - an icon left on the glass after its "
        "driver has gone - is invisible to every assertion about state. QEMU "
        "by name: MartyPC has no network card of any kind",
        needs=("qemu", "nasm"), serial=True, timeout=420, builds=True),
    Row("pkgrun", "soak", py("tests/pkgrun.py"), 110.0,
        "OSAPI_PKG_RUN (SPEC.md 21.5): the loader's back half with the disk "
        "read replaced by a copy, which is how the Wire runs a package it "
        "fetched over the network into a claim. `make pkgrun` builds a TEST "
        "package no shipped floppy carries (the mseg/covl shape, SPEC.md "
        "78.9); it reads the SHIPPED hello.o88 off the disk beside it into a "
        "claim and hands it to the slot three times. Asserts a live instance "
        "named HELLO in the KERNEL's own inst_tab - so the pass does not rest "
        "on the test package's opinion - then CF=1 / LD_EBAD for a spoiled "
        "magic and CF=1 / LD_EBAD for header flags bit 2, a package carrying "
        "PARTS, which are read out of a FILE that does not exist here "
        "(SPEC.md 20.12). The two refusals also say the region and the "
        "instance record a failed load reserved were given back. QEMU because "
        "nothing here is a time and all three answers are state; it builds "
        "its own disk, so it needs no capability of its own",
        needs=("qemu", "nasm"), serial=True, timeout=420, builds=True),
    Row("heapmap", "soak", py("tests/heapmap.py"), 30.0,
        "What does the claim heap look like when the boot is over? (SPEC.md "
        "50, 66) Every driver attached at once on a machine WITH memory above "
        "1MB, sampled from instruction zero: the order claims are taken in, "
        "and MC_RLOC for each - which is the machine-readable answer to "
        "'can this be compacted'. IT HAS CAUGHT ONE (SPEC.md 51.1.2): drv_load "
        "padded its claim by 4KB for a bss it had not read the header to "
        "learn, and mem_regrow's shrink keeps the BASE and frees the TAIL - "
        "which on a top-down claim is walled in above the driver. 14,336 "
        "bytes stranded and the largest free run 375.0K -> 361.0K, from a "
        "change whose own comment priced it at 'a few hundred transient "
        "bytes'. Nothing else in the suite sees it, because nothing else "
        "looks at WHERE the free memory is - and the obvious repair, growing "
        "the claim instead, this row scored WORSE than the bug",
        needs=("qemu", "nasm"), serial=True, timeout=300,
        wants=("build/os8088.img",)),
    Row("dockmark", "soak", py("tests/dockmark.py"), 90.0,
        "Does the dock strip mark windows it did not draw under? (SPEC.md"
        "30.3.3)",
        needs=("marty",), serial=True),
    Row("dualcheck", "soak", py("tests/dualcheck.py"), 10.0,
        "Can this MartyPC drive TWO video cards at once?"
        "(docs/plans/completed/DUAL-DISPLAY-PLAN.md 9)",
        needs=("marty",), serial=True),
    Row("gfxlk", "soak", py("tests/gfxlk.py"), 150.0,
        "Does ANYTHING draw with the gfx lock free - which is the one state "
        "the mouse ISR draws in? (SPEC.md 7/12.8.4, docs/FIELD-NOTES.md 34) "
        "Rebuilds the tree, because the counters are a knob kernel",
        needs=("marty", "nasm"), serial=True),
    Row("ovlhigh", "soak", py("tests/ovlhigh.py"), 20.0,
        "docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 1: a C package's OVERLAY is "
        "claimed from the TOP (SPEC.md 50.3.2 - its base is a CS) and declares "
        "itself movable. CWORD.OVL is 18,565 bytes, bigger than every kernel "
        "module put together, and it took the low door for as long as overlays "
        "have existed. It reads MC_HI, the placement and MC_RLOC out of "
        "mem_tab, because a declaration mem_movable REFUSED looks identical "
        "from inside the package (SPEC.md 66.5.6.2). Verified to fail in both "
        "halves: the low door reddens three checks, dropping the declaration "
        "reddens the fourth",
        needs=("marty", "cc"), serial=True,
        wants=("build/cword360.img",)),
    Row("cmemmove", "soak", py("tests/cmemmove.py"), 110.0,
        "A C PACKAGE DECLARES A CLAIM MOVABLE and the compactor moves it "
        "(docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 3). os88_mem_claim was the "
        "whole of the C SDK's heap surface until now, so every C claim was "
        "pinned by construction - C64's 64KB, RunCPM's 64KB, Weave's canvas, "
        "Loom's project buffers. The round trip is longer than any other "
        "callback's (C, thunk, kernel, cc_onmove, C) and the assertion that "
        "earns its keep is that `was` and `now` did not arrive SWAPPED: "
        "verified by swapping the two pushes in cc_onmove, which leaves "
        "[ch_seg] stale and the move count at 0. Needs `cc`",
        needs=("marty", "cc"), serial=True,
        wants=("build/cmemmove360.img",)),
    Row("regmove", "soak", py("tests/regmove.py"), 130.0,
        "A package's REGION moves and the package keeps working (SPEC.md "
        "66.6.1). 66.6 said since it was written that a region can never move "
        "because its base IS its CS; this is the door open. FOUR PACKAGES and "
        "each has a job - PAINT takes the ceiling, SHEET goes under it and is "
        "the one that has to move, FILLER takes the arena down to a few tens "
        "of KB, and closing PAINT leaves the hole. tests/filler is an "
        "instrument with NO assertions of its own, which heapfrag cannot be: "
        "its comb is sized from the largest run IT sees and its own checks "
        "fail when another package's claims are interleaved, so a refused "
        "forcing claim looks exactly like a granted one",
        needs=("marty",), serial=True,
        wants=("build/regmove360.img",)),
    Row("sheetmove", "soak", py("tests/sheetmove.py"), 130.0,
        "Compact the heap out from under a LIVE Sheet "
        "(docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 2). SHEET was the largest "
        "undeclared holder in the tree - six claims at its entry proc, ~99KB, "
        "pinned for the session - which made SPEC.md 66.5.10.2's 'the arena "
        "below the top now has no barrier in it at all' false the moment a "
        "sheet opened. Five are declared now; sh_stgseg is the ES:BX of all "
        "seven of the package's file calls and stays pinned, and this asserts "
        "THAT too, because 'we meant to leave that one' and 'we forgot that "
        "one' are the same picture. paintmove's recipe. VERIFIED TO FAIL: "
        "drop sh_cellseg from sh_reloc's table and check 3 reads STALE while "
        "check 4's repaint differs over 24 rows of the grid",
        needs=("marty",), serial=True,
        wants=("build/sheetmove360.img",)),
    Row("heapcheck", "soak", py("tests/heapcheck.py"), 40.0,
        "Drive tests/heapfrag and read its verdict out of the guest (SPEC.md"
        "66.8).",
        needs=("marty",), serial=True,
        wants=("build/heapfrag360.img",)),
    Row("xmcheck", "soak", py("tests/xmcheck.py"), 50.0,
        "The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4). QEMU and "
        "not MartyPC, and the row said `marty` for a year: the machine has "
        "to HAVE memory above 1MB and the target machine never can (SPEC.md "
        "41.9 rule 1), which is one of docs/TESTING.md's seven legitimate "
        "uses of QEMU. It also needs nasm for the OVERLAY's map - xm_tab is "
        "in XMEM.DRV now (SPEC.md 41.12), not in the kernel.",
        needs=("qemu", "nasm"), serial=True, timeout=600,
        wants=("build/os8088.img", "build/xmtest.img")),
    Row("brpromise", "soak", py("tests/brpromise.py"), 40.0,
        "SPEC.md 71.11: the Browser's WF_SAVEU promise follows the FETCH - "
        "withdrawn when br_go starts one, back when it settles. The plain apps "
        "disk, so unlike the other br* rows it needs no `make browsertest`",
        needs=("marty",), serial=True),
    Row("calcflick", "soak", py("tests/calcflick.py"), 90.0,
        "Does the Calculator FLASH? (PERFORMANCE.md Part 3.1, SPEC.md 65.4)",
        needs=("marty",), serial=True, wants=("build/calcref.img",)),
    Row("ftpdflick", "soak", py("tests/ftpdflick.py"), 120.0,
        "What does clicking into an FTPD Setup field cost, and is it still "
        "two cells rather than the page? (SPEC.md 77.45)",
        needs=("marty",), serial=True),
    Row("ftpdfocus", "soak", py("tests/ftpdfocus.py"), 60.0,
        "Does FTPD's Setup page keep a caret it cannot type into? "
        "(SPEC.md 77.45.4)",
        needs=("marty",), serial=True),
    Row("dispapp", "soak", py("tests/dispapp.py"), 40.0,
        "Does a PACKAGE stay on its own display? (SPEC.md 39.2.1)",
        needs=("marty",), serial=True),
    Row("dispapps", "soak", py("tests/dispapps.py"), 120.0,
        "Do the apps that lay out ONCE re-derive when the adapter changes?",
        needs=("marty",), serial=True),
    Row("dispband", "soak", py("tests/dispband.py"), 54.1,
        "Can a window use the SECOND display's top rows? (SPEC.md 39.16.2)",
        needs=("marty",), serial=True),
    Row("dispzoom", "soak", py("tests/dispzoom.py"), 50.0,
        "SPEC.md 11.95.2.1: does a ZOOM land flush on an EXTENDED desktop?"
        "wm_snap_ax refuses to move a window right when it would hang off the"
        "screen, a test written against [vid_w] - which before 39.16 WAS the"
        "screen and on two cards is the SUM. So the refusal that keeps a"
        "maximized window at x=0 on one display stops firing on two, the"
        "window walks 7px, and wm_flush_ck then puts the left border back on"
        "top of the gap. Zooms the same window on one display and on two and"
        "requires the same answer; the single-display half is the CONTROL, so"
        "a failure says the second display broke it rather than that zoom is"
        "broken. Reads the record, because 7px of geometry does not show in a"
        "screenshot of a mostly-white window",
        needs=("marty",), serial=True),
    Row("dispblit", "soak", py("tests/dispblit.py"), 60.0,
        "Does a BLIT reach the second display? (SPEC.md 39.14.7)",
        needs=("marty",), serial=True),
    Row("dispbrow", "soak", py("tests/dispbrow.py"), 60.0,
        "The field's browser report: a drag that does not move it, and a"
        "width cut on a card wide enough to hold it",
        needs=("marty",), serial=True),
    # THE THREE ROWS BELOW DECLARED 60s AND TAKE FOUR TO NINE TIMES THAT.
    # `secs` derives the kill timeout (`max(60, secs*4+30)` = 270s), so all
    # three were being killed MID-RUN and reported as TIMEOUT - which reads
    # like a hung emulator and is not one. Measured on this container, each
    # run directly and to completion, every assertion passing:
    #
    #     dispsize    295 s        dispcalc    406 s        dispprefer  546 s
    #
    # Nothing stalls. dispsize profiled with `settle` instrumented: 61 calls,
    # 153.4 s of a 311.6 s wall inside settle (49.2%), and the LONGEST single
    # settle is 11.4 s against settle's own 120 s limit. Half of one of these
    # rows is the harness's screen-polling floor - `settle(quiet=1.0,
    # stable=2)` cannot return in under ~2 s and dispprefer alone makes 16
    # adapter switches at four settles each - so the wall time is sampling
    # cost, not the guest being slow and not the guest being stuck.
    #
    # The `secs` below is therefore what the row COSTS, and the explicit
    # timeout is ~2x that: enough that container jitter cannot kill a healthy
    # run, small enough that a genuinely hung emulator is still caught in
    # minutes rather than tens of them.
    Row("dispcalc", "soak", py("tests/dispcalc.py"), 250.0,
        "Does the Calculator add up, fold cleanly and redraw nothing spare?",
        needs=("marty",), serial=True, timeout=900),
    Row("dispcalcx", "soak", py("tests/dispcalcx.py"), 150.0,
        "Does the Calculator re-fold cleanly when its box moves under it?",
        needs=("marty",), serial=True),
    Row("dispcheck", "soak", py("tests/dispcheck.py"), 60.0,
        "Did os8088 bring the SECOND card up, can it DRAW on it, and does the",
        needs=("marty",), serial=True),
    Row("dispclose", "soak", py("tests/dispclose.py"), 90.0,
        "SPEC.md 75: closing ASKS - W_ONCLOSE, the deferred OSAPI_WM_CLOSE, "
        "and os88ui_ask's alert, driven through every branch and finished by "
        "reading the saved file off the floppy with os88flush",
        needs=("marty",), serial=True, timeout=900),
    Row("dispclose-small", "soak",
        # THE ARM IS DECLARED HERE, once, and every tool in the session
        # follows it: os88sym picks the kern_small map and CHECKS it against
        # build/smallk/kernel.bin, and os88geom's per-arm constants resolve to
        # a 28-byte window record. The row used to build that map in process
        # with check=False and patch one stride by hand, which was enough
        # until anything else read the window table.
        ["env", "OS88_DEFINES=KERN_SMALL", "OS88_BUILD=build/smallk"]
        + py("tests/dispclose.py", "--small"), 100.0,
        "...and the same suite on kern_small, which since SPEC.md 75.3.2 has "
        "the identical behaviour rather than a fallback. It needs `make "
        "small` first, and it is the ONE gate here that drives that build",
        needs=("marty",), serial=True, timeout=900),
    Row("dispcold", "soak", py("tests/dispcold.py"), 300.0,
        "WHO DRAWS INTO .cold? (docs/plans/completed/DUAL-DISPLAY-VGA.md 8(11))",
        needs=("marty",), serial=True),
    Row("dispcorner", "soak", py("tests/dispcorner.py"), 120.0,
        "REPORTED ARTIFACTS, LOCALISED (a corner pixel, and two drags across"
        "a seam)",
        needs=("marty",), serial=True),
    Row("dispdepth", "soak", py("tests/dispdepth.py"), 60.0,
        "Does a window dragged BACK from a different-depth display arrive"
        "intact?",
        needs=("marty",), serial=True),
    Row("dispdrag", "soak", py("tests/dispdrag.py"), 40.0,
        "Does a window DRAGGED across the seam arrive on both displays?",
        needs=("marty",), serial=True),
    Row("dispfit", "soak", py("tests/dispfit.py"), 60.0,
        "Is changing adapter and changing back the IDENTITY on every window"
        "rect?",
        needs=("marty",), serial=True),
    # 546 s measured - the longest of the three, 16 adapter switches at four
    # settles each. See the note above `dispcalc`.
    Row("dispprefer", "soak", py("tests/dispprefer.py"), 560.0,
        "Does a package's PER-ADAPTER preference and floor survive a drag"
        "across the seam, and does a USER outrank it? (SPEC.md 11.100)",
        needs=("marty",), serial=True, timeout=1200),
    Row("disptitle", "soak", py("tests/disptitle.py"), 80.0,
        "Does a title bar STRADDLING the seam have one polarity? (SPEC.md"
        "5.4.2.4) - it builds `make BAND=1` itself, the composer being a knob"
        "again since SPEC.md 5.9.6, and puts the default kernel back",
        needs=("marty",), serial=True),
    Row("dispthm", "soak", py("tests/dispthm.py"), 60.0,
        "Does SPEC.md 76's theme meet the extended desktop honestly? Color is"
        "a fact about the PRIMARY and a window can be on the other card",
        needs=("marty",), serial=True),
    # 295 s measured, and the row the settle profile above was taken on.
    # See the note above `dispcalc`.
    Row("dispsize", "soak", py("tests/dispsize.py"), 210.0,
        "What size is a window given when it lands on the other card?"
        "(SPEC.md 11.100.3/11.100.4)",
        needs=("marty",), serial=True, timeout=900),
    Row("dispfrac", "soak", py("tests/dispfrac.py"), 180.0,
        "Does apps/fractal's restore cache survive an adapter change?"
        "(SPEC.md 40.1)",
        needs=("marty",), serial=True),
    Row("dispfreeze", "soak", py("tests/dispfreeze.py"), 120.0,
        "The field's freeze: a straddling window over a Disk window, then a"
        "click",
        needs=("marty",), serial=True),
    Row("dispfsx", "soak", py("tests/dispfsx.py"), 60.0,
        "WHICH MONITOR DOES A FULLSCREEN BRACKET LAND ON? (SPEC.md 53.7.1)",
        needs=("marty",), serial=True),
    Row("dispherc1", "soak", py("tests/dispherc1.py"), 60.0,
        "HERCULES PRIMARY, VGA SECOND (SPEC.md 39.19.2's other arrangement)",
        needs=("marty",), serial=True),
    Row("dispmcfs", "soak", py("tests/dispmcfs.py"), 180.0,
        "SPEC.md 11.2 fullscreen with the window's CENTRE on the second"
        "display",
        needs=("marty",), serial=True),
    Row("tank", "soak", py("tests/tank.py"), 30.0,
        "SPEC.md 85: TANK ATTACK draws, ADVANCES, and does not flash - the ink"
        "on the glass per DISPLAYED frame, whose floor against its median is"
        "the whole question a foreign-mode raster is built to answer",
        needs=("marty",), serial=True),
    Row("tankaim", "soak", py("tests/tankaim.py"), 30.0,
        "SPEC.md 85.6.5's aim assist and its reticle: no bearing is unreachable"
        "(by enumeration, not by the algebra), the TURN is untouched at TK_TURN"
        "a tick, the gun corrects inside the closed sight's box and nowhere"
        "else, and the sight closes on exactly the shots that land - read"
        "against tk_aimq and tk_aimz, the code's own measured error and range",
        needs=("marty",), serial=True),
    Row("tankspawn", "soak", py("tests/tankspawn.py"), 40.0,
        "SPEC.md 85.6.6: no round starts inside a piece of scenery - which one"
        "in nineteen did, sealing the player in a box a 26-unit step cannot"
        "leave - and a player who somehow IS inside one can still drive out",
        needs=("marty",), serial=True),
    Row("skies", "soak", py("tests/skies.py"), 35.0,
        "SPEC.md 88: CLEAR SKIES draws and advances, takes off from the runway"
        " under full throttle and the stick, crashes when the nose is held"
        " into the ground and comes back to the airport's reset point, and"
        " its frames do not flash - SPEC.md 85.1's instrument on a raster"
        " that redraws the whole view every frame. Hercules, the target."
        " Measured at 30 s wall alone on an idle four-core box - it was 120"
        " before SPEC.md 88.5.6.1 took the frame rate back",
        needs=("marty",), serial=True),
    Row("skiescga", "soak", py("tests/skies.py", "--machine",
                               "os8088_5150_cga_gla"), 35.0,
        "SPEC.md 88 on CGA: the 320x112 view (88.13.4 s default there is"
        " FULL, which is the geometry CGA shipped with), palette 0 over a"
        " light-blue background, the same flight",
        needs=("marty",), serial=True),
    Row("skies160", "soak", py("tests/skies160.py"), 35.0,
        "SPEC.md 88.15: CLEAR SKIES in SIXTEEN COLOURS on a CGA - the 160x100"
        " text hack, reached through the Settings page's Mode row and not a"
        " poke. Seven colours at once on a card that has four in 320x200, all"
        " 8,000 character cells still the half block (a blit that wrote pairs"
        " is exactly the defect this catches), the strip at the bottom of the"
        " picture - which only a hundred two-scan-line rows put there - the"
        " three readings changing over a climb with the speed standing beside"
        " the take-off prompt, and nothing stale against a forced full"
        " redraw, and a SIZE change that keeps the mode - black is 0x00DE"
        " here and not 0, and the take-off prompt naming THIS aeroplane's"
        " rotate speed in the strip's own units. Three red runs:"
        " --clobber-crtc leaves the 6845's max scan line at 7,"
        " --clobber-clear zeroes the screen on this backend too, and"
        " --clobber-fit lengthens the strip's sentence past the fourteen"
        " cells it gets with cs_d_msg's fit clamp taken out - which is a"
        " message that vanishes off the glass entirely. Measured at 35 s"
        " wall alone on an idle four-core box",
        needs=("marty",), serial=True),
    Row("fsxclip", "soak", py("tests/fsxclip.py"), 22.0,
        "SPEC.md 53.1.1: an fsx bracket entered from a CLICK handler comes"
        " back to a whole desktop - the menu bar, the background and the dock"
        " held pixel for pixel against what they were, because fsx_run clears"
        " the clip region the handler armed",
        needs=("marty",), serial=True),
    Row("skiesset", "soak", py("tests/skiesset.py"), 75.0,
        "SPEC.md 88.13: the Settings page and its four knobs reaching the"
        " picture - Few files fewer objects and draws faster, a fill box"
        " clears its bit, the in-flight hotkeys do the same without the page,"
        " and a smaller view leaves none of the larger one beside it. Also"
        " 88.13.6's two defects: a drop-down's list has to BANK and reach the"
        " glass (the pick works without either, which is how this row passed"
        " while the page could not be dropped down at all) and Done has to be"
        " the full 13.7 gesture. And two about the top rung: the ladder NESTS"
        " with the dense bits on and with them cleared in the guest's own"
        " table (88.13.1.3 gave every world a dense city, so the equal branch"
        " has no location left to stand on and is synthesised rather than left"
        " to stop running), and a CSO_DENSE building is NOT SOLID below High"
        " (88.13.1.4) - flown through at Moderate, crashed into by name at"
        " High, because cs_collide reads the table and never the ladder. And"
        " 88.13.5's CYCLING hotkeys: F1/F2/F3 each step their own ladder one"
        " rung and round, walked a FULL LAP so the wrap is seen, F4/F5 toggle"
        " the fills, and every one of them raises 88.13.8's TOAST - checked"
        " against the Settings page's own list of names read out of the"
        " guest, and then left to expire back to the strip it replaced - a"
        " fill's toast says FILL or WIRE and a SECOND toast has to repaint"
        " the strip, which is the panel's key and not the byte. And"
        " 88.13.9's round trip: four settings picked on the page, Done, the"
        " window closed, the package opened again, and the file in"
        " SYSTEM/APPDATA is what the new instance comes back with",
        needs=("marty",), serial=True),
    Row("skiesocc", "soak", py("tests/skiesocc.py"), 26.0,
        "SPEC.md 88.13.7: the occlusion pass, and the only thing keeping its"
        " width rule honest. Every verdict cs_occlude reaches is checked"
        " against the glass WITH THE PASS OFF - with it on the object is"
        " already skipped, so removing it changes nothing and the check"
        " passes whatever the pass believes, which is how the first version's"
        " --clobber-occ run came back green with twenty-two verdicts"
        " 'confirmed invisible'. Nine viewpoints, three of them off the"
        " centreline, because the rule is exact for an object dead ahead",
        needs=("marty",), serial=True),
    Row("skieslod", "soak", py("tests/skieslod.py"), 40.0,
        "SPEC.md 88.5.4.2: a solid too small to tell apart is one filled"
        " rectangle PAST SIX KILOMETRES too. cs_drawobj built 11 cz in a"
        " word, which stops fitting at 5,958 m, and past there the product"
        " wrapped and every solid in the band drew all of its vertices and"
        " faces to cover four pixels - 11.9 ms a tower against 4.2 on a"
        " 4.77 MHz 8088. Nothing shipped stood in the band, so the row moves"
        " JFK's anonymous towers onto the sight line at 8 km and reads"
        " which path they take. Also 88.5.4.3: the impostor must be the SIZE"
        " of the model it stands in for - cs_boxlod clobbered cs_pshr and a"
        " REFUSED impostor left the full path running in whole metres, so a"
        " building drew at a fraction of its size over exactly the part of"
        " the approach where the rectangle crosses CS_LODPX; and 88.5.4.4,"
        " that nothing on the skyline goes away and comes back as the"
        " aeroplane taxis - a DIP and not a step, because the skyline"
        " legitimately grows and shrinks. And 88.13.2.1: at DRAW DISTANCE ="
        " ULTRA the same towers at the same place take the POLYGONS instead,"
        " cs_boxlod not entered at all and nothing reaching cs_rect, which is"
        " that rung's whole feature",
        needs=("marty",), serial=True),
    Row("skiespitts", "soak", py("tests/skiespitts.py"), 34.0,
        "SPEC.md 88.7.2: the second aeroplane flies by its own CSP_ATT - the"
        " Pitts rolls right round and loops over the top and stays where the"
        " stick left it, where the trainer clamps both axes and returns to"
        " level - and wears its own scattered panel (88.9.3)",
        needs=("marty",), serial=True),
    Row("skiespanel", "soak", py("tests/skiespanel.py"), 32.0,
        "SPEC.md 88.9.4: the panel is SAMPLED on the gate and PAINTED per"
        " page, so Mode X's two pages cannot hold readings taken different"
        " gates apart - the altimeter that read 1,683 feet one frame and"
        " 1,666 the next. The displayed sequence never goes backwards in a"
        " climb; --clobber-share sends the between-gates path back to .same"
        " and it does, four frames in twelve. And 88.9.4.2's state box over"
        " all EIGHT combinations of state, stall, cs_onwater and 88.7.10.1's"
        " brake latch: the key packs four things into one word and the"
        " painter tested the whole of the high byte, so an amphibian airborne"
        " OFF the water read STALLED for the whole flight. The intermediate"
        " state each row forces is CONFIRMED at the painter and no longer"
        " counted in card frames - a gate is every CS_PRATE TICKS, so eight"
        " card frames is a fifth of one on Mode X, and under load the row"
        " reported that the key had not changed, which was true and useless",
        needs=("marty",), serial=True),
    Row("skiesfleet", "soak", py("tests/skiesfleet.py"), 71.0,
        "SPEC.md 88.7.5-88.7.7.1: the three aeroplanes that came after the"
        " Pitts, each checked on its MECHANIC. The Magister's roll rate ramps"
        " and decays and its engine spools; the Bijave starts in the air with"
        " no engine and glides better than 12:1; the A5 starts on the water,"
        " gets off it and lands back on it, and the SAME touchdown in the"
        " Cessna is a crash. Since 88.7.7.1 it also lands on water FAR from"
        " the strip - the face furthest from it in that world's own object"
        " table, walked out of the GUEST rather than carried here as a"
        " coordinate - and dry land off the runway is still a crash, which is"
        " the pair that says the strip stopped being an invisible runway"
        " without the edge going away. The glider then gets 88.7.6.1-88.7.6.3"
        " to itself: W held for two hundred frames opens no throttle and makes"
        " no tone, an announcement AGES OUT with nothing else happening, and"
        " the air is read against cs_lifts as the guest holds it - still air"
        " is still, the table's strongest column and deepest sink move an"
        " aeroplane by exactly what the row says, and crossing into either"
        " arms the swoop. Since 88.7.7.2 the WATER stops it, and only with"
        " the throttle shut - a hull that dragged harder than the engine"
        " pushes is an amphibian that cannot take off, which is how the first"
        " build of that read - and 88.7.10.1's brake is a LATCH a typed b"
        " toggles rather than a level read nobody could see. The air is read"
        " against cs_lifts one whole TILE out on both axes as well as in tile"
        " zero (88.7.6.4), and every aeroplane's prompt is checked to name"
        " its OWN rotate speed (88.7.9). --clobber-lag, --clobber-amphib and"
        " --clobber-water are the three red runs",
        needs=("marty",), serial=True),
    Row("skiesease", "soak", py("tests/skiesease.py"), 34.0,
        "SPEC.md 88.7.3: the horizon captures the approach - held toward"
        " level both aeroplanes land EXACTLY on it on both axes, the Pitts"
        " lands on INVERTED level too, and held away nothing is eased at"
        " all. Read at a cs_step breakpoint: a frame spends one, two or"
        " three ticks, so a per-frame sample cannot see the landing."
        " Then 88.7.3.1 asks the question the PILOT asks, which is the"
        " per-FRAME one: the ease landing mid-frame is no use if the frame's"
        " remaining ticks carry the axis off before anything is drawn, and"
        " that is the Pitts 'skipping the horizon' the field reported. Every"
        " horizon a continuous roll passes gets a frame on it, from three"
        " start angles, because ONE of them landing on a frame boundary by"
        " luck is exactly what the old code did. And 88.7.3.2's contract:"
        " the eased ticks of an approach are EQUAL - a plateau, not a dive -"
        " and the landing is the frame's LAST tick, so the detent discards"
        " nothing. That replaced a floor of 60% of the rate, which passed on"
        " 78%-then-22% - the shape the field called a pause at the horizon."
        " --clobber-ease puts a ret on cs_ease and is the red run; the"
        " --clobber-hold beside it is GONE, the ladder having left the hold"
        " nothing to discard so that knob could no longer fail",
        needs=("marty",), serial=True),
    Row("skiestap", "soak", py("tests/skiestap.py"), 40.0,
        "SPEC.md 88.7.5.2: the two remainders of the per-tick stick, both"
        " reported off the glass. The shortest press MartyPC can express is"
        " walked across a frame in twelve phases and every one must turn the"
        " aeroplane - int 16h is an EVENT and OSAPI_KEY_DOWN a LEVEL read, so"
        " without the latch it is 4 of 12, which is the owner's \"one in"
        " three\". And a held approach to level, read at cs_render rather"
        " than cs_step, shows level on EXACTLY ONE drawn frame: one and not"
        " zero is the capture made visible, one and not four is the promise"
        " that this is not Tank Attack's lock. --clobber-tap is the red run;"
        " the --clobber-hold beside it is GONE - what it removed was the jet"
        " zeroing its rate on arrival, which 88.7.5.2.1 now does only for a"
        " CENTRED stick, and the detent it aimed at is a rounding safety net"
        " since 88.7.3.2 lands on a frame's last tick",
        needs=("marty",), serial=True),
    Row("skiesbody", "soak", py("tests/skiesbody.py"), 30.0,
        "SPEC.md 88.7.8 and 88.7.8.1: the elevator is a rate about the"
        " AEROPLANE's wing axis, and past the vertical its contribution to"
        " the heading REVERSES - the 1/cos(pitch) that 88.7.8 drops for being"
        " singular there had a sign in it, so after a loop a banked pull"
        " turned the wrong way. --clobber-invert is check 5's red run."
        " Originally: the elevator is a rate about the AEROPLANE's wing"
        " axis and every model added it straight into [cs_pitch], which is"
        " the world's. Wings level nothing changes (cos 0, sin 0); in a 90"
        " degree bank the world pitch stands still and the whole of"
        " CSP_PITCHR goes into the TURN; and at pitch 180 roll 180 - upright"
        " and facing back, which is a loop then a roll to level - the same"
        " key moves [cs_pitch] the other way and the aeroplane CLIMBS, which"
        " is the controls coming back the right way round with no case"
        " analysis. The trainer is wired to it through cs_axisp."
        " --clobber-body is the red run and it reproduces both reports",
        needs=("marty",), serial=True),
    Row("skiesrad", "soak", py("tests/skiesrad.py"), 34.0,
        "SPEC.md 88.5.11: cs_pwhole never lies. cs_projall PREDICTS off"
        " CSM_RAD that an object is wholly in front of the near plane, and"
        " cs_edge1 then reads cs_sxv without testing cs_fv while cs_pinview"
        " turns cs_seg's clip off - so a vertex that was never projected this"
        " frame drew an edge from whatever the last object left in its slot,"
        " unclipped, across the cockpit. Reported off the machine as \"in"
        " wire mode sometimes lines will draw across the cockpit\". It dives"
        " past the Shard, the tallest model in any world, and asserts the"
        " INVARIANT rather than the pixels - deliberately, because whether a"
        " line lands on the panel depends on what was in the slot before, so"
        " it showed on 1 of 80 poses and a pixel row would go green on a"
        " broken build four times in five. --clobber-rad restores BOTH halves"
        " (the Shard's old 209, and no 88.5.11.1 guard) and it goes red",
        needs=("marty",), serial=True),
    Row("skiesdiag", "soak", py("tests/skiesdiag.py"), 20.0,
        "SPEC.md 88.14: Clear Skies' watchdog, which is an instrument for a"
        " machine that has HARD FROZEN - int 08h hooked for the length of the"
        " fsx bracket, painting the last three interrupted IPs and a tick"
        " counter straight into VRAM every tick, so a frozen screen says"
        " where it is stuck in a photograph. The row tests it the only way"
        " such a thing can be tested: it patches a `jmp $` over cs_render and"
        " requires all three blocks to NAME that address off the glass while"
        " the counter goes on climbing. Needs `make skiesdiag` (a private"
        " tree; the shipped skies.o88 is byte-identical without it) - DECLARED,"
        " because it is not the row's own business to report its absence: it"
        " said SKIP and returned 0 for its whole life, so the suite scored it"
        " `ok` in 0.1s and nothing ever drove the watchdog. wants= builds the"
        " tree AND keeps it current, which a capability cannot do",
        needs=("marty",), wants=("build/skiesdiag/apps360.img",),
        serial=True),
    Row("skiesadi", "soak", py("tests/skiesadi.py"), 30.0,
        "SPEC.md 88.9.2.2: THE HARD FREEZE, reduced to one instruction. The"
        " attitude indicator drew its horizon bar at t x tan(roll) and got"
        " the tangent with `idiv cx`, CX = cos - on a note reading \"over 0.5"
        " within MAXROLL\", which is true of a TRAINER and false of every"
        " aerobatic aeroplane here. cs_sintab is 1024 entries over the turn,"
        " so cos is EXACTLY 0 for the 64-unit window at +-90 and the divide"
        " faults. The row arms the INT 0 VECTOR - which catches every divide"
        " fault in the program at once - and walks the roll through both"
        " windows on an aeroplane with no clamp. --clobber-adi puts the raw"
        " idiv back and is the red run",
        needs=("marty",), serial=True),
    Row("skieshz", "soak", py("tests/skieshz.py"), 52.0,
        "SPEC.md 88.3.3.1 and 88.13.3.1: the horizon reaches the GLASS, in"
        " BOTH modes - the fill's band, and the one segment that is the whole"
        " horizon when the ground fill is off. The second went the same way as"
        " the first a mode along: cs_skyground runs before cs_scene, so"
        " cs_seg read the PREVIOUS frame's last object's cs_pinview,"
        " cs_pwhole and object box, and the segment was drawn into the shadow"
        " and never carried. Reported as \"at some angles, some of the time,"
        " the horizon line disappears in wire view\". --clobber-hzmark is"
        " check 2's red run and reads 3 of 36 poses short."
        " SPEC.md 88.3.3.1: the horizon reaches the GLASS. cs_skyground was"
        " right the whole time - pinned at 45 degrees its cs_xl is a correct"
        " diagonal - and the band's rows were drawn into the shadow and never"
        " carried, because the band loop wrote each split row's span and never"
        " widened the span set's ROW RANGE, which is the only thing cs_blit"
        " walks. The row checks the glass against the guest's own normal at"
        " every bank angle, over GROUPS OF FOUR ROWS because two of the"
        " Hercules ground's four dither phases are blank and a per-row test"
        " passes on a broken build. It PINS the attitude, which is what makes"
        " the failure reachable: nothing else then widens the range."
        " --clobber-range is the red run",
        needs=("marty",), serial=True),
    Row("skiesgeom", "soak", py("tests/skiesgeom.py"), 48.0,
        "SPEC.md 88.5.5-88.5.8: every polygon and segment of a frame, on nine"
        " pinned scenes - four BANKED, four low among the buildings - held to"
        " a host replay of the guest's own near clip, side clip and per-scale"
        " projection: the two faults a straight flight never reached"
        " (88.5.6.1, 88.5.7.1), each with a red run that patches it back."
        " Plus 88.5.8's invariant, which is NOT a replay: with the wings"
        " level a world-vertical edge must project vertical, and the replay"
        " cannot catch a fault in the algorithm because it reproduces it."
        " And 88.5.4.1: no IMPOSTOR rectangle bigger than CS_LODPX, which is"
        " the screen-axis-aligned square that stood upright in a bank. And"
        " 88.5.10's winding: every face's signed area held to the shoelace of"
        " the same points computed here, EXACTLY - the back-face test read one"
        " triangle of a trapezoid, which is noise once the points are whole"
        " pixels, and the two POI towers' crown faces went on and off a frame"
        " at a time on the machine",
        needs=("marty",), serial=True),
    Row("skiesthr", "soak", py("tests/skiesthr.py"), 40.0,
        "SPEC.md 88.9.1.1: the THROTTLE is a user input, so it never waits"
        " for 88.9.1's gate. The instruments read the aeroplane every"
        " CS_PRATE = 6 ticks, which is right for a speed or an altitude -"
        " they move every tick in a climb and nine opaque glyphs a change at"
        " the frame rate is a tenth of the frame - and wrong for the one"
        " control the pilot is holding down. cs_pitem exempted the state and"
        " the message by NAME and nothing else, so the throttle waited with"
        " them: measured with W held, cs_thr moved on 16 frames and the panel"
        " redrew on 8, the glass showing the previous number every other"
        " frame. The row reads cs_pkeys - what is ON THE GLASS, not what the"
        " aeroplane holds - at a cs_blit breakpoint, and asks both halves of"
        " the ask: every frame it moves the new value is shown, and a STEADY"
        " throttle redraws NOTHING, which is what makes it free when nobody"
        " is touching it. --clobber-now puts the two throttle entries of"
        " cs_pnow back to 0 and check 1 goes red at about half the frames",
        needs=("marty",), serial=True),
    Row("skiesmode", "soak", py("tests/skiesmode.py"), 40.0,
        "SPEC.md 88.13.11: the Settings page's MODE row is HIDDEN where the"
        " display has one raster, not greyed. 47 rule 2 greys a control the"
        " machine could use in another STATE, and the adapter is not a state"
        " - it is fixed for the session, so a greyed Mode row is a promise"
        " the machine can never keep. Hiding is also what makes it safe:"
        " a row that is never painted never has OS88UI_DR_DIS written, so"
        " 13.14.5's refusal would not fire for it, but a row outside"
        " cs_nsets' count is reached by no walk on the page at all. This arm"
        " is the Hercules one - no rect, and a press where the row used to be"
        " opens nothing. --clobber-hide NOPs cs_nsets' `dec cx` and the row"
        " comes back",
        needs=("marty",), serial=True),
    Row("skiesmodevga", "soak",
        py("tests/skiesmode.py", "--machine", "os8088_xt_vga", "--modes", "1"),
        40.0,
        "SPEC.md 88.13.11 the other way round: a display that HAS two rasters"
        " keeps the Mode row. Both arms are registered because a change that"
        " hid the row EVERYWHERE would pass the Hercules one, and 'hide it'"
        " must not come to mean 'delete it'",
        needs=("marty",), serial=True),
    Row("uidrdis", "soak", py("tests/uidrdis.py"), 55.0,
        "SPEC.md 13.14.5: a DROP-DOWN DRAWN DISABLED TAKES NO PRESS."
        " OS88UI_DIS was a paint-time argument and nothing else - os88ui_drop"
        " took it in DI and greyed the box, os88ui_drpress took BX/CX/DX and"
        " could not know - so a press on a greyed control ran the whole open"
        " path and the app then repainted a list it was still greying. The"
        " state is the CONTROL's now (OS88UI_DR_DIS, written by the painter)"
        " and the press half refuses without SPENDING the press, so a greyed"
        " box behaves as though it were not there. Two things this row had to"
        " learn: OS88UI_DR_DIS is DERIVED, so poking it disables nothing -"
        " the app's own predicate is retargeted at a LIVE row instead - and a"
        " drop-down opens on the PRESS and picks on the RELEASE, so a click"
        " does both and reads OPEN=0 either way. --clobber-dis turns the"
        " guard's `je` into a `jmp` so the refusal is never taken, which is"
        " the tree as the field had it; NOPing that `je` instead makes every"
        " control refuse every press and passes for the opposite reason",
        needs=("marty",), serial=True),
    Row("skiesflat", "soak", py("tests/skiesflat.py"), 62.0,
        "SPEC.md 88.4.6.1: an EXACTLY HORIZONTAL segment lands where it was"
        " asked. CS_SLICE's flat arm - dy zero, so the whole line is one run -"
        " jumped into the shared row loop without loading DX, which is where"
        " that loop takes the run's first x; DX held 3xBP from the caller's"
        " own slice test and BP is 2|dy|, so every exactly-horizontal segment"
        " on CGA and the 160x100 hack was drawn at the view's LEFT EDGE."
        " It needs an EXACT angle to show, so it wants a model edge between"
        " two vertices of the same height seen with the wings level: the"
        " Eiffel's platform bar (88.5.4.5) is the first such edge in any"
        " world, and even then the ink landed where nothing had MARKED, so it"
        " never reached the glass and surfaced only as a stale pixel at the"
        " other end of the screen. The row reads the SHADOW and asks the"
        " direct question - stood on the Issy runway looking at the tower, a"
        " nine-pixel run at the tower's own x and nothing at the left edge,"
        " read at a breakpoint on cs_blit so the frame it reads is a WHOLE"
        " one (docs/WRITING-TESTS.md 13 entry 44)."
        " --clobber-flat NOPs the four bytes and the bar moves to x = 0",
        needs=("marty",), serial=True),
    Row("skiesrwy", "soak", py("tests/skiesrwy.py"), 33.0,
        "SPEC.md 88.6.2.1: the runway keeps its lines PAST ITS OWN MIDDLE."
        " cs_drawobj's size test opened `cmp cx, 2600 / ja .out` on the"
        " object's camera z, and ja is unsigned - so an origin BEHIND the eye"
        " read as 65,000-odd and was dropped as far and small. The runway's"
        " origin is its midpoint, and .out is below cs_edges AND below"
        " cs_rwline, so taxi past the halfway board and the outline and the"
        " centreline went together. The row walks the aeroplane down the"
        " strip at 2 m and counts what the guest ENTERS, both halves, because"
        " a row that walked only the far half could not tell a fix from a"
        " runway that had stopped being drawn at all; then once at 40 m,"
        " which is the other half of the report. --clobber-rwy takes the four"
        " bytes of the guard back out and it reads 30, 30, 30, 0, 0, 0",
        needs=("marty",), serial=True),
    Row("skiesui", "soak", py("tests/skiesui.py"), 90.0,
        "SPEC.md 88.10's title page on the VGA machine: the two drop-downs"
        " (SPEC.md 13.14's first users) drop, close and pick, Esc closes one,"
        " Flight -> Instructions turns the page and back, the Mode menu's CGA"
        " pick flies in CGA320, and the release of the pick's press is owed to"
        " the launcher's window - [ui_armw] read directly, the check that"
        " catches a click handler coming back with SI clobbered. LEG 7 is the"
        " control's two REFUSALS (13.14.3), and both are FORCED in the guest"
        " because no gesture reaches either: DR_WIN zeroed so OSAPI_WM_CLIP_SET"
        " must refuse - the control then has to stay SHUT, where it used to"
        " believe it was open with nothing drawn and let the next press pick an"
        " invisible cell - and api_gfx_rest patched to stc/ret so the write-back"
        " must refuse, where drback said 'repaired' and left 2,719 pixels of"
        " list on the glass. Both legs carry their own arranging checks, 7a"
        " because the first version was GREEN against the defect it was written"
        " for (leg 6 had left the Instructions page up, so the click landed on"
        " no box at all), and both captures park the pointer, because parked on"
        " the box the arrow hangs four rows into the band and reads as five"
        " pixels of a list that is not there.",
        needs=("marty",), serial=True),
    Row("skiesvga", "soak", py("tests/skies.py", "--machine",
                               "os8088_xt_vga"), 45.0,
        "SPEC.md 88 on MODE X - MartyPC's VGA hosts the unchained mode, whatever"
        " an earlier session believed - the 320x144 view on two pages, the same"
        " flight at ~4 fps, and the page SHOWN changing every second in flight:"
        " the owner once saw this backend freeze on its first frame with the"
        " loop still running, and a flip that never shows the drawn page is"
        " exactly that",
        needs=("marty",), serial=True),
    Row("wireflick", "soak", py("tests/wireflick.py"), 30.0,
        "SPEC.md 78.5's three draw orders, as ink on the glass per displayed"
        "frame - the flicker measured rather than argued about",
        needs=("marty", "wiredisk"), serial=True,
        wants=("build/wire360.img",)),
    Row("wirefps", "soak", py("tests/wirefps.py"), 30.0,
        "What SPEC.md 5.6.4.1 is worth to a program that draws lines - apps/wire"
        "reading its own frame rate, with the dispatch poked out and back",
        needs=("marty", "wiredisk"), serial=True, alone=True,
        wants=("build/wire360.img",)),
    Row("paintrate", "soak", py("tests/paintrate.py"), 60.0,
        "SPEC.md 42.8.1: is Paint's brush stroke still sampled at the TICK? The"
        "facets in a hand-drawn curve were one 55ms sleep each. On the GLaBIOS"
        "twin, like paintwipe - and unlike paintwipe this row DOES take a"
        "number, so its docstring argues the case: the window is guest cycles"
        "with no int 13h in it, and the assertion is a separation of an order"
        "of magnitude rather than a calibrated figure",
        needs=("marty",), serial=True),
    Row("paintwalk", "soak", py("tests/paintwalk.py"), 30.0,
        "SPEC.md 42.8.3: a brush chord steps each axis exactly |d| times. The"
        "denominator lived in CX, which `loop` decrements, so a wide nib drew"
        "a zig-zag that grew with the hand's speed",
        needs=("marty",), serial=True),
    Row("paintblank", "soak", py("tests/paintblank.py"), 120.0,
        "SPEC.md 42.15: a full-canvas repaint is 980 ms through the pair"
        "decoder and one gfx_fill when every pixel is the same colour, which"
        "is the picture Paint draws most. Counts the DECODER, not the clock,"
        "and checks the stroke is still on the glass afterwards",
        needs=("marty",), serial=True),
    Row("paintsize", "soak", py("tests/paintsize.py"), 60.0,
        "SPEC.md 42.8.6.1: a maximize GROWS Paint's canvas and a restore"
        "shrinks it, so the two clicks walk pt_ucopy over every row at two"
        "strides. A row has AT MOST eight blocks and the walk assumed exactly"
        "eight: 97 seconds and a band of garbage in the saved picture",
        needs=("marty",), serial=True),
    Row("paintundo", "soak", py("tests/paintundo.py"), 60.0,
        "SPEC.md 42.8.6: draw, Ctrl+Z, Ctrl+Z - does the picture come back to"
        "the pixel? Nothing covered undo at all until the copy-on-first-touch"
        "bitmap went from a bit a ROW to a bit a BLOCK",
        needs=("marty",), serial=True),
    Row("spantest", "soak", py("tests/spantest.py"), 30.0,
        "SPEC.md 5.10: gfx_spans against the GFX_FILL a row its own refusal"
        "sends a caller to - nine shapes including an EMPTY row, both clips"
        "and a middle grey's dither, plus the refusal itself. apps/paint only"
        "ever asks for the shapes a brush chord makes",
        needs=("marty",), serial=True,
        wants=("build/spantest.img",)),
    Row("spantest-vga", "soak",
        py("tests/spantest.py", "--machine", "os8088_xt_vga"), 30.0,
        "...and the same on VGA, which is gfx_spans' other row writer - the"
        "latch-and-bit-mask one, with vga_set_color and vga_gc_reset hoisted"
        "out of the span loop",
        needs=("marty",), serial=True,
        wants=("build/spantest.img",)),
    Row("paintundo-vga", "soak",
        py("tests/paintundo.py", "--machine", "os8088_xt_vga"), 60.0,
        "...and the same on VGA, which is where SPEC.md 5.10's gfx_spans takes"
        "its OTHER row writer - the latch-and-bit-mask one. The redo hash is"
        "what compares it against the canvas the untouched walk wrote, so this"
        "is the gate on the planar half of the primitive",
        needs=("marty",), serial=True),
    Row("uilat", "soak", py("tests/uilat.py"), 30.0,
        "SPEC.md 7.3: how long a click waits while a worker draws, bracketed"
        "by two memory breakpoints because the mouse harness has a half-second"
        "floor and cannot see it (7.3.1)",
        needs=("marty", "wiredisk"), serial=True, alone=True,
        wants=("build/wire360.img",)),
    Row("evqfull", "soak", py("tests/evqfull.py"), 20.0,
        "SPEC.md 10.1: a full event ring discards its OLDEST input, and never"
        "a coalesced WAKE - asked of evq_push directly, with the CPU parked",
        needs=("marty",), serial=True),
    Row("linefast", "soak", py("tests/linefast.py"), 90.0,
        "Does SPEC.md 5.6.4.1's fast walk lay 5.6.4's pixels? Both inks, all"
        "eight octants, clipped and not - against the same kernel with the"
        "dispatch poked out",
        needs=("marty",), serial=True),
    Row("dispmine", "soak", py("tests/dispmine.py"), 30.0,
        "Can Minesweeper's bottom row be PLAYED on a CGA? (SPEC.md 11.93)",
        needs=("marty",), serial=True),
    Row("curshape", "soak", py("tests/curshape.py"), 60.0,
        "Does the pointer change SHAPE over a window that asks for one? "
        "(SPEC.md 7.2) - nothing covered it when 7.2.1.1 rewrote the test",
        needs=("marty",), serial=True),
    Row("dispmode", "soak", py("tests/dispmode.py"), 60.0,
        "Single or Extend, where the second display sits, and does it survive"
        "a",
        needs=("marty",), serial=True),
    Row("dispmodex", "soak", py("tests/dispmodex.py"), 120.0,
        "Which display does Missile Command ask about Mode X? (SPEC.md"
        "39.18.1)",
        needs=("marty",), serial=True),
    Row("dispnp", "soak", py("tests/dispnp.py"), 60.0,
        "Does a WIDE straddling Note Pad letter its whole row? (SPEC.md"
        "27.2.1)",
        needs=("marty",), serial=True),
    Row("cfgtrip", "soak", py("tests/cfgtrip.py"), 30.0,
        "SPEC.md 51.5.3: does a setting still survive the panel and a reboot?"
        "The parser is two copies now - the reader in the boot overlay and the"
        "writer inside CTRL.DRV - sharing no segment, no table and no buffer,"
        "and every way of getting that wrong assembles cleanly and boots. So"
        "the assertion is the round trip: poke three settings, close the panel,"
        "flush the disk the guest wrote and boot IT. Two boots, which is why it"
        "is here and not in the gate",
        needs=("marty",), serial=True),
    Row("dispreboot", "soak", py("tests/dispreboot.py"), 300.0,
        "WHO WRITES ui_rebootq? (docs/plans/completed/DUAL-DISPLAY-VGA.md 8(11))",
        needs=("marty",), serial=True),
    Row("dispsave", "soak", py("tests/dispsave.py"), 60.0,
        "Does the raise cache work on the SECOND display? (SPEC.md 39.14.8)",
        needs=("marty",), serial=True),
    Row("dispblitp", "soak", py("tests/dispblitp.py"), 180.0,
        "SPEC.md 5.4.3: does gfx_blitp's REFUSAL survive its own teardown?"
        "Its whole output is CF and the teardown opened with a `cmp`, so every"
        "refusal came back as drawn - invisible until an extended desktop,"
        "where a straddle is one. Two legs, because a DIRECT move onto the"
        "mono display refuses on a different guard and leaked a display nest."
        "Needs the VGA+mono machine",
        needs=("marty",), serial=True),
    # Two boots a machine and two machines, plus the two `make`s the A/B needs,
    # which is what puts it at four minutes rather than one. It EARNS them: the
    # fixed leg alone cannot tell a conserved run from a run that never crossed
    # a cell, and this file can be null in a way that looks exactly like a pass
    # (SPEC.md 39.14.6). `--no-build` drops to the fixed leg for a hand-built
    # image; `--machine` picks one orientation.
    # THE POSITIVE CONTROL IS THE POINT OF THE ROW. Every assertion in it is
    # "nothing outside its own columns" or "the same bytes as the unclipped
    # draw", and all of them pass on a harness that draws nothing at all -
    # which is what a boot-and-diff version of this would BE, since nothing on
    # a stock desktop puts an icon off the right edge.
    # NOTHING ELSE IN THE TREE REACHES sw_fill_pat. A Disk listing that fits
    # draws no chevrons and the Task Manager has to be open, so a
    # boot-and-look version of this is a null test that reads like a pass -
    # icoclip's problem one primitive along, and the same answer.
    Row("fillpat", "soak", py("tests/fillpat.py"), 20.0,
        "Does the 1bpp PATTERNED fill lay the tile down where it says? "
        "(SPEC.md 5, 32) - gfx_fill_pat on a mono adapter is two masked edge "
        "columns through sw_patcol plus a rep stosw interior, with the tile "
        "row picked by (y & 7). Calls it through the debugger over rows it "
        "zeroed itself, at four rect shapes that run every arm including the "
        "one-byte-wide fold, and checks each byte against the kernel's OWN "
        "staged gfx_patbuf and edge masks rather than a golden image. Both "
        "strides.",
        needs=("marty",), serial=True),
    # THE ENTRY IS NAMED HERE and it is not decoration: ico_disk32 is the
    # INDEXED kind now (SPEC.md 25.7) and `icon_draw` reads that record as
    # 258 bytes of plain art, walking off its 13 into whatever follows. Every
    # assertion in this row is "nothing outside its own columns" or "the same
    # bytes as the unclipped draw", so a mismatched pair DRAWS GARBAGE AND
    # PASSES - measured, on the tree that introduced the kind. The record and
    # the entry have to be named together or this row tests nothing.
    Row("icoclip", "soak", py("tests/icoclip.py", "--entry", "icon_draw_ix"),
        40.0,
        "Does a 32-wide icon HANGING OFF THE RIGHT EDGE still clip byte for "
        "byte? (SPEC.md 25.6) - ico_pass_bb's per-byte column test is the "
        "only thing between an icon at x = w-8 and a write on the NEXT SCAN "
        "LINE, and ico_core does not refuse the shape. Calls icon_draw_ix "
        "through the debugger at all eight shift phases and at every column "
        "that hangs off, on BOTH strides (CGA 80, Hercules 90), over a zeroed "
        "background so two draws are comparable.",
        needs=("marty",), serial=True),
    Row("uilayer", "soak", py("tests/uilayer.py"), 50.0,
        "Does tools/os88ui.py do what it says, and is confirming cheaper "
        "than settling? Every verb - open_drive, open, drag_window, "
        "raise_window from BEHIND another window, menu_pick off the live "
        "menu_bar[], close - then every failure path, because the layer's "
        "whole claim is that a miss raises where it happened instead of "
        "surfacing twenty steps later. Ends with the same navigation run "
        "both ways on one machine: settle-and-hope against read-the-answer.",
        needs=("marty",), serial=True),
    Row("dispseam", "soak", py("tests/dispseam.py"), 300.0,
        "Does the one cell a display SEAM crosses still reach the glass?"
        "(SPEC.md 39.14.11) - it builds `make NOSEAMCUT=1` itself for the A/B"
        "and puts the default kernel back, both seam orientations",
        needs=("marty",), serial=True),
    Row("dskwstage", "soak", py("tests/dskwstage.py"), 120.0,
        "SPEC.md 18.4.2.1: does the DMA STAGING arm run, and does it move the "
        "RIGHT bytes? dskw_runadd's third answer - CF=0 with CX != 0, `not "
        "one sector fits this DMA page` - fell through into a shared "
        "`jmp .ioerr` from 2e8e292 until then, so dskw_wdata.stg and "
        "dskw_rdata.stg had never executed and the fix TURNED ON a routine "
        "nobody had watched. Nothing on a desktop reaches it (18.4.1 keeps "
        "the kernel's own bases 512-aligned), so the row arranges it: a 200KB "
        "mem_claim spans three 64KB physical boundaries, and a buffer 0xF0 "
        "bytes short of one is the only thing that makes dskw_runmax answer "
        "0. Five cases with two page-safe CONTROLS, .stg counted by exec "
        "breakpoint rather than inferred, and the bytes settled OFF the "
        "machine - the floppy is flushed and walked by tests/unit/t_image's "
        "own FAT12 reader, which shares no code with the kernel that wrote "
        "it, so a writer and a reader agreeing on the same wrong thing "
        "cannot pass. `--bug` asserts the PRE-fix refusal instead, which is "
        "what makes the A/B repeatable against an old image",
        needs=("marty",), serial=True),
    Row("dispstrad", "soak", py("tests/dispstrad.py"), 30.0,
        "Does a window dragged across the seam give back the rows only ONE"
        "display",
        needs=("marty",), serial=True),
    Row("disptext", "soak", py("tests/disptext.py"), 40.0,
        "Does going back to text name the CARD? (SPEC.md 39.20)",
        needs=("marty",), serial=True),
    Row("dispvy", "soak", py("tests/dispvy.py"), 40.0,
        "How many rows of the SECOND monitor can a straddling window use?",
        needs=("marty",), serial=True),
    Row("lzdrv", "soak", py("tests/lzdrv.py"), 45.0,
        "docs/plans/O88-COMPRESSION-PLAN.md 12.6: a COMPRESSED DRIVER loads, "
        "expands and answers. RAMDISK.DRV is the subject because it has both "
        "halves of wave 3 - a 2,416-byte bss drv_bss re-makes and a body "
        "the transparent read unpacks (SPEC.md 20.13.3.1: a compressed driver "
        "is a 'CZ' file, expanded into the claim drv_load cut from the "
        "directory hint) - so one file exercises the whole path. The image "
        "is compared byte for byte; the BSS deliberately is not, because by "
        "the time the row has a segment the driver has attached and its bss "
        "is its working memory. What stands in for it is the three driver "
        "probes, which a bss full of floppy leftovers does not answer",
        needs=("marty",), serial=True,
        wants=("build/lzdrv360.img", "build/drvcall360.img")),
    Row("lzload", "soak", py("tests/lzload.py"), 30.0,
        "SPEC.md 20.13: a COMPRESSED package loads and expands to the same "
        "bytes. The loader reads it HIGH, brings the clear prefix down and "
        "expands the body into the same region with no second claim, so what "
        "this asserts is the WHOLE image byte for byte and not that a window "
        "opened - a decoder that got the last run wrong would still open one. "
        "The third subject is compressed in the format the default kernel "
        "does NOT carry: 20.13.3 says the cell is in every build and a "
        "missing format answers CF=1, and no amount of reading the source "
        "demonstrates that",
        needs=("marty",), serial=True,
        wants=("build/lzload360.img",)),
    Row("lzload-lz4", "soak", py("tests/lzload.py", "--lz4only"), 80.0,
        "...and the REFUSAL, on a kernel built COMPRESS=lz4. The shipped one "
        "carries both formats (SPEC.md 20.13.6), so the row above proves the "
        "dispatch and this one proves the fence: SPEC.md 20.13.3 says the "
        "cell is in every build and a format the build has not got answers "
        "CF=1, which no amount of reading the source can demonstrate and "
        "which matters to anyone who cuts a single-format kernel. The kernel "
        "and the fixture are built in a PRIVATE TREE (tools/os88build.py), so "
        "it neither writes build/ nor spends a second build putting it back",
        needs=("marty", "nasm"), serial=True),
    Row("lzfence", "soak", py("tests/lzfence.py"), 20.0,
        "SPEC.md 20.13.4: OSAPI_DECOMP REFUSES a hostile stream rather than "
        "writing. The bounds in kernel/lz.inc were measured for size and "
        "speed before anything ever fed them a bad stream, so this is the row "
        "that turns 'it refuses' from an assertion into a fact - a truncated "
        "blob, a zero offset, a match reaching below the caller's buffer and "
        "one running past the declared output, each of which would otherwise "
        "reach a neighbour's region under mem_claim_hi's top-down placement. "
        "THE POSITIVE CONTROL IS THE POINT: a decoder that refuses everything "
        "passes all four negatives, so a valid stream runs first and its "
        "twelve bytes are compared one by one",
        needs=("marty",), serial=True,
        wants=("build/lzfence360.img",)),
    Row("lzfile", "soak", py("tests/lzfile.py"), 30.0,
        "SPEC.md 20.14: a COMPRESSED FILE is read transparently, and a write "
        "derives the hint from the bytes it is writing. The disk carries one "
        "document TWICE - plain, and inside a 'CZ' wrapper - so every "
        "assertion is the two of them compared with each other and the "
        "fixture can be rewritten without touching test code. Six verdicts, "
        "of which the last two are the write half: PACKED.TXT's RAW bytes "
        "written back under another name come back EXPANDED (without that, a "
        "file-manager copy turns a document into gibberish), and a PLAIN "
        "file written over that same name reads back plain - a stale mark "
        "would send prose to the decoder, which refuses it. The middle two "
        "are the size an application claims against: OSAPI_FILE_FIND reports "
        "the UNPACKED size with bit 0 of +22 set, and a plain file must NOT "
        "carry that bit, or a cell that set it unconditionally would pass. "
        "AND THEN IT OPENS README.TXT off the shipped system disk by "
        "double-clicking it (SPEC.md 20.14.2.1), which no fixture could stand "
        "in for: the manual's reader has 16,384 bytes for 16,334 of text and "
        "in-place expansion wants 16,413, so the field saw 'Too big' on a "
        "file the machine had just reported as fitting. np_len is what says "
        "it worked - an empty note and a full one look identical at every "
        "zoom - and it reads 16,019, the CRLF file FOLDED, so 315 carriage "
        "returns had to arrive to be dropped",
        needs=("marty",), serial=True,
        wants=("build/lzfile360.img",)),
    Row("lzcomp", "soak", py("tests/lzcomp.py"), 150.0,
        "SPEC.md 22.22: File > Compress, and the machine's LZB stream against "
        "the host's BYTE FOR BYTE. os88lz.lzb_compress_machine is a mirror of "
        "kernel/compress.inc statement for statement rather than a model of "
        "its output, so the assertion is equality of the whole file - the 'CZ' "
        "header and every byte of the stream - and not a ratio or a round "
        "trip. THAT IS THE POINT: a round trip passes on any encoder that "
        "emits a decodable stream, which is every parse anybody could write, "
        "and it would have said nothing about either of the two bugs the "
        "first draft of the module had (a lookahead that poisoned the slot it "
        "had just read, and a write bound tested once a symbol rather than "
        "once a pass). Four subjects, each a different half of the verb: a "
        "PLAIN file; the SAME bytes already wrapped LZ4, which is what a "
        "shipped floppy carries and which must produce the identical file "
        "because dskw_read_x hands both paths the same bytes; a PACKAGE, "
        "refused and untouched; and the plain file a second time, refused as "
        "already compressed. The disk is read back with os88flush rather than "
        "by asking os8088 - the writer and the reader inside are one FAT12 "
        "implementation, so the one bug a write can have that matters is the "
        "one that cannot be seen from in there. Its FIRST leg is not about "
        "the verb at all: Copy/Paste must move a compressed file AS IT SITS "
        "(SPEC.md 20.14.3), which the copy engine has always done - raw "
        "clusters, dskw_stat's on-disk size, the hint re-derived at the other "
        "end - and which nothing asserted until this. The installer had the "
        "same job and got it wrong (52.10.13.1); tests/instdeep.py is that "
        "half",
        needs=("marty",), serial=True),
    Row("lzmod", "soak", py("tests/lzmod.py"), 30.0,
        "SPEC.md 20.14.5: BEVERLY.MOD, COMPRESSED, opened by a double-click. "
        "The file this whole feature is for - 116,085 bytes is 114 of a 360KB "
        "disk's 354 clusters, which is why that geometry ships the module on "
        "a floppy of its own (24.4); LZ4 takes it to 42,177 and 42, so "
        "Tracker AND the module fit one disk with 294 clusters left. It is "
        "also the ONLY file in the tree that crosses a segment, so every path "
        "in 20.14.5 - the bumped ES, the borrowed match source one segment "
        "down, lz_cross splitting a copy at the boundary, and LZ_F_BUMP "
        "retiring the offset compare - runs here and nowhere else. All "
        "116,085 bytes are compared BYTE FOR BYTE, because a decoder that got "
        "one match wrong across the boundary still opens a window, still "
        "shows the title, and still plays - it plays a click",
        needs=("marty",), serial=True,
        wants=("build/lzmod360.img",)),
    Row("lzmod-lzb", "soak", py("tests/lzmod.py", "--fmt", "lzb"), 30.0,
        "...and the same module through the OTHER decoder, on the SHIPPED "
        "kernel - which carries both now (SPEC.md 20.13.6), so this row no "
        "longer builds a knob and is the proof that a format the machine "
        "does not WRITE is one it can READ. It is the only thing that ever "
        "EXECUTES LZB's segment-crossing arm: nothing on any shipped disk is "
        "LZB, so t_buildmatrix keeps the single-format arms assembling and "
        "this keeps the one that matters correct. Its FIXTURE spends ~10s "
        "compressing 116KB with a bit-oriented format, which is why it is "
        "soak and why lzmod itself stays on LZ4 - the KERNEL is the shipped "
        "one on both arms now, so neither builds anything",
        needs=("marty",), serial=True,
        wants=("build/lzmodlzb360.img",)),
    Row("lzship", "soak", py("tests/lzship.py", "--fmt", "lz4"), 80.0,
        "THE WHOLE SHIPPED SET, COMPRESSED (`make zset ZFMT=lz4`): every "
        "shipped package, every shipped driver and every data file on both "
        "360KB floppies at once, under a kernel built to carry that format. "
        "The other rows in this family compress one subject each; this one has "
        "a failure mode none of them can have, because the system disk's NINE "
        "drivers expand during BOOT, by a kernel that has not finished "
        "starting, into a heap still being laid out. It asserts the boot, the "
        "drivers actually attaching (off drv_tab, not off the screen - one "
        "that failed to expand is silently absent rather than visibly "
        "broken), a compressed package opening off the shipped disk, and "
        "BEVERLY.MOD opening from MEDIA/ on the APPS disk with all 116,085 "
        "bytes intact - which is the point of the exercise, that geometry "
        "needing a whole second floppy for that file today (SPEC.md 24.4). "
        "The set is built in a PRIVATE TREE (tools/os88build.py) rather than "
        "through `make zset`, which existed only because the compressed "
        "images land at the paths a plain build uses",
        needs=("marty", "nasm"), serial=True),
    Row("lzship-lzb", "soak", py("tests/lzship.py", "--fmt", "lzb"), 120.0,
        "...and the same set through the bit-oriented decoder. It is a second "
        "full build of everything plus LZB's ~4x compression time, which is "
        "why it is separate from the row above rather than a loop inside it - "
        "and it is what says the two formats are interchangeable at the DISK "
        "level and not only at the decoder's. A tree of its own, so the two "
        "formats coexist instead of overwriting each other",
        needs=("marty", "nasm"), serial=True),
    Row("kzboot", "soak", py("tests/kzboot.py"), 30.0,
        "SPEC.md 2.9.13: the COMPRESSED KERNEL boots, and it is the SHIPPED "
        "one. The only compression in the tree whose decoder is not in the "
        "kernel - it is in the BLOB, which mem_unblob hands back to the heap "
        "at the end of kmain, so the whole feature is resident for the length "
        "of one boot and costs the running machine nothing. Two assertions, "
        "and the second is the one a screenshot cannot make: it reaches a "
        "desktop, AND the image in memory is the image on the host BYTE FOR "
        "BYTE, because a decoder that got one match wrong still boots, still "
        "draws a desktop, and is a kernel with a wrong instruction somewhere "
        "in it. The comparison is taken at a breakpoint on KERNEL_SEG:0 "
        "rather than at the desktop, that being the one moment the image is "
        "exactly what the file says - stage 2 writes 18.93.1's boot_cylrun at "
        "+4 and the boot timer at +12 before it jumps, and kmain writes a "
        "great deal more, so a comparison taken later reports ~103 "
        "differences on a kernel that expanded perfectly. TWO GEOMETRIES, "
        "because there are two floppy boot sectors: 360KB takes the byte "
        "comparison and 1.44MB is a different 512 bytes (18 spt against 9) "
        "that gets booted. It builds nothing - the four images it reads are "
        "the shipped ones",
        needs=("marty",), serial=True),
    Row("kzboot-off", "soak", py("tests/kzboot.py", "--nokzip"), 100.0,
        "...and the A/B (`NOKZIP=1`), which is what tells 'the packed disk "
        "boots' from 'any disk boots'. It is also the only row that ever "
        "RUNS the unpacked arm of either loader - t_buildmatrix keeps it "
        "assembling and nothing else keeps it correct. It used to rebuild "
        "build/ twice, once for the knob and once to put it back; the knob "
        "kernel is a PRIVATE TREE now (tools/os88build.py) and the shipped "
        "directory is never written",
        needs=("marty", "nasm"), serial=True),
    Row("drvcall", "soak", py("tests/drvcall.py"), 60.0,
        "Can a PACKAGE reach a DRIVER? (SPEC.md 20.11, docs/plans/completed/NET-STACK-PLAN.md"
        "stage A)",
        needs=("marty",), serial=True,
        wants=("build/drvcall.img", "build/drvcall360.img")),
    Row("drvscroll", "soak", py("tests/drvscroll.py"), 80.0,
        "SPEC.md 31.1.2: scrolling the Drivers list draws the LIST once, not"
        "thrice.",
        needs=("marty",), serial=True),
    Row("drvup", "soak", py("tests/drvup.py"), 60.0,
        "SPEC.md 13.8.4: a DRIVER's Control Panel page acts on the RELEASE.",
        needs=("marty",), serial=True),
    Row("editmove", "soak", py("tests/editmove.py", "--app", "notepad"), 150.0,
        "Compact the heap out from under a live app that is holding a big"
        "claim",
        needs=("marty",), serial=True,
        wants=("build/editmove360.img", "build/mppmove360.img", "build/zmove360.img")),
    Row("frcyclefull", "soak",
        py("tests/unit/t_frcycle.py", "--stride", "7"), 900.0,
        "...and the same agreement at stride 7 - about 25 million (point,"
        "type) pairs, each run through the core twice (SPEC.md 40.7)."),
    Row("frinsetfull", "soak",
        py("tests/unit/t_frinset.py", "--stride", "2"), 900.0,
        "...and the same sweep at stride 2 - 5.7 million of the lattice"
        "points fr_inset can claim, against the core (SPEC.md 40.5). The fast"
        "row strides 32 to fit its tier. Stride 1 is the exhaustive one and"
        "is a flag away, but it is FORTY MINUTES of interpreted Q4.12 and"
        "would be the longest row in the tree by a factor of two; it was run"
        "once, at the shipped margin and again at a margin of zero, and"
        "SPEC.md 40.5 records what it found."),
    Row("frpromise", "soak", py("tests/frpromise.py"), 150.0,
        "SPEC.md 40.4: Fractal promises when the frame lands and takes it"
        "back when the view moves.",
        needs=("marty",), serial=True),
    Row("fdlggrey", "soak", py("tests/fdlggrey.py"), 60.0,
        "The file dialog's default button: REDRAWN IN PLACE must equal"
        "FRESHLY PAINTED.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img",)),
    Row("fdlgsmall", "soak",
        ["env", "OS88_DEFINES=KERN_SMALL", "OS88_BUILD=build/smallk",
         "OS88_SYSIMG=build/small360.img"] + py("tests/fdlggrey.py"), 300.0,
        "...and the SAME drive against kern_small, where the WHOLE dialog is "
        "an on-demand module (SPEC.md 38.0, docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md "
        "9.2.6) rather than resident code. It is `fcpsmall`'s argument one "
        "feature along and a bigger engine: seven entries with two exit "
        "conventions, every call out of the image a far one through an `xd_` "
        "entry, the register epilogues copied inside the image, and mod_need "
        "reading it off the disk on fdlg_open with mod_drop giving it back in "
        "fdlg_reap. NONE of that is exercised by the row above, which runs "
        "the resident build. It builds its own image (`make small`) for "
        "smallboot's reason.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img", "build/small.img", "build/smallapps.img")),
    Row("fdlgdrop", "soak", py("tests/fdlgdrop.py"), 80.0,
        "...and the module comes BACK on every route a dialog ends by "
        "(SPEC.md 38.0.1). The row above drives the dialog and never asks "
        "what happened to its image; three of the four dismissals - Open, "
        "Cancel, Escape - clear [fdlg_win] from inside the image's own "
        "W_ONCLICK, and mod_drop sat behind three separate compares of that "
        "same word, so the pass that should have collected the claim was "
        "turned away by the very store it was meant to notice. A 16KB claim "
        "held for the rest of the session on the machine with 128KB in it, "
        "and the CLOSE BOX - the one route nobody uses - is the one that "
        "worked, which is how it survived the module split's own testing. "
        "The assertion is mod_tab[MOD_FDLG].seg and not a picture, because "
        "the leak is invisible: the dialog really is gone and the next one "
        "reuses the image it never gave back. Each route is a TRANSITION - "
        "held while the dialog is up, zero after - so a build that stopped "
        "LOADING the module fails the first half rather than passing the "
        "second. Against the kernel before it: three fail, the close box "
        "passes. It builds its own image (`make small`) for smallboot's "
        "reason.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img", "build/small360.img")),
    Row("fdlgup", "soak", py("tests/fdlgup.py"), 60.0,
        "SPEC.md 13.8.3: the Standard File dialog's buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img",)),
    Row("fmthumb", "soak", py("tests/fmthumb.py"), 30.0,
        "SPEC.md 13.10.5: the Disk window's scroll-bar THUMB is dragged, and"
        "x is never read.",
        needs=("marty",), serial=True),
    Row("fdlgthumb", "soak", py("tests/fdlgthumb.py"), 50.0,
        "SPEC.md 13.10.5: ...and the Standard File dialog's, which is the"
        "second bar one gesture record has to tell apart (13.10.5.10).",
        needs=("marty",), serial=True, builds=True),
    Row("npscroll", "soak", py("tests/npscroll.py"), 30.0,
        "SPEC.md 27.7.6.1/27.7.2: scrolling a note whose height is still being"
        "counted neither freezes the machine nor blanks half the scroll bar.",
        needs=("marty",), serial=True),
    Row("pkgthumb-np", "soak", py("tests/pkgthumb.py", "notepad"), 50.0,
        "SPEC.md 13.10.7: the thumb gesture inside a PACKAGE - Note Pad.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("pkgthumb-br", "soak", py("tests/pkgthumb.py", "browser"), 50.0,
        "SPEC.md 13.10.7: ...the Browser.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("pkgthumb-wd", "soak", py("tests/pkgthumb.py", "word"), 50.0,
        "SPEC.md 13.10.7: ...and Word, which needed 13.10.6.4 settling first -"
        "its menus are a modal poll and the thumb's two edges are disjoint"
        "from them.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdtype", "soak", py("tests/wdtype.py"), 420.0,
        "SPEC.md 27.4.3: a keystroke stops walking where the row indices "
        "reconverge (205.6 -> 80.4 ms). Legs B..D are CORRECTNESS legs and the "
        "old code was correct, so they pass on a build with the early-out "
        "compiled out - leg E is the one that fails there, and it is a "
        "BREAKPOINT on wd_eoutck.rok rather than a stopwatch, because the "
        "first version bounded wd_walk's cycles and PASSED at 344,824 with the "
        "feature disabled. Leg D is the one that catches the dangerous "
        "failure, an early-out that fires without its index proof, and it "
        "PROVES a reflow was arranged before asserting: a row below whose "
        "start index moved by something other than the characters typed. It "
        "was green against that break until it did (3,849 differing pixels "
        "after). The pixel reference is a page down and back, which a "
        "formatted document always full-repaints (68.6), so the comparison is "
        "against a screen no early-out touched.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdcaret", "soak", py("tests/wdcaret.py"), 480.0,
        "SPEC.md 27.4.6: a caret move lays the note out ONCE. Leg A counts "
        "wd_walk calls inside one keystroke and requires 1 - the change "
        "itself, and what fails on a build with the feature off; leg C is the "
        "A/B inside one boot, wd_1pok being the whole arming. The trap the "
        "gate exists for is a level under the pixels: [wd_clip] gates the "
        "GLYPH STORE as well as the drawing, by the same three tests and "
        "deliberately, so clipping the one pass to the dirty range composed no "
        "cells at all for a row whose signature was not yet known and "
        "wd_rflush's delta then re-lettered the whole row - 419 differing bits "
        "on a Right arrow, on a screen that still read as text. Leg D is the "
        "one ordering the collapse changes: wd_seecaret now runs AFTER the "
        "drawing, so a Down that scrolls lands on rows this pass already drew. "
        "The pixel reference throughout is a page down and back, which a "
        "formatted document always full-repaints (68.6).",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdenter", "soak", py("tests/wdenter.py"), 450.0,
        "SPEC.md 27.4.5: an Enter pushes the note below the split down with "
        "one gfx_scroll instead of erasing to the content bottom and "
        "lettering every row in it (448.2 -> 133.3 ms). Leg A is the one that "
        "fails on a build with the feature off - a BREAKPOINT on "
        "wd_nlpush.d1, past the scroll - and leg F is the A/B inside one "
        "boot: wd_nlband is the whole arming, so stc/ret over it in the guest "
        "turns the push off and the same keystroke must draw the same screen "
        "the slow way. The pixel reference throughout is a page down and "
        "back, which a formatted document always full-repaints (68.6), and "
        "THE BAND INCLUDES THE SLIVER below the last whole row: the first "
        "build scrolled to [wd_bot] and left four scanlines of the last "
        "row's glyphs standing, which still reads as text. Leg E is the "
        "corruption case rather than a speed one - an Enter on the last "
        "visible row makes the caret-follow scroll, and the push has repaired "
        "the tables for a layout the glass has not been given, so wd_redraw "
        "must refuse the blit and repaint.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdscroll", "soak", py("tests/wdscroll.py"), 330.0,
        "SPEC.md 68.2.2 and 27.7.2.2: Word's scroll bar is not part of the "
        "text band, and a scroll UPWARD blits like a scroll down. Leg A "
        "samples the bar's ARROW CELL through a down-arrow click and requires "
        "0 of 48 altered; leg D requires a click ABOVE the thumb not to enter "
        "wd_paint - it always did, repainting menu bar, ruler and text at 622 "
        "ms against the down click's 251. Leg E is the A/B, wd_upheight being "
        "the whole arming, AND the only thing still exercising [wd_sbkeep]: "
        "leg D used to BE the refusal. Its target view is deliberately NOT the "
        "top of the note, because returning to top 0 passed while the <8px "
        "SLIVER below the last drawable row was blitted into and never erased "
        "- at top 0 the pixels pushed into it happened to be white. Leg F is "
        "the one that looks at what a scroll LEAVES BEHIND rather than what it "
        "draws: the pricing walk banked wd_rows, which wd_shiftrows reads as "
        "its SOURCE, so the up blit's own screen was perfect to the pixel and "
        "the next page down drew three rows of the wrong text. Leg B puts BOTH "
        "ends of its round trip against a forced repaint separately - a round "
        "trip says something is wrong and never which end. Leg G is the THUMB "
        "DRAG (68.2.4): SB_RATE is 0 here, so the gesture commits once at the "
        "release and jumps further than [wd_vrows] - the blit refuses, and "
        ".fullpaint white-filled the whole content box and drew all four "
        "chrome strips again for a scroll that cannot have moved any of them. "
        "It asserts BOTH halves against their own defect - no wd_chrome call, "
        "and the same pixels over the WHOLE window as the same drag with "
        "wd_sigsame forced to refuse, which is the one path that still owes "
        "the strips.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdmove", "soak", py("tests/wdmove.py"), 210.0,
        "SPEC.md 68.3.1: Word's document movers go a WORD at a time, and the "
        "assertion is the BUFFER rather than the glass - a wrong word is a "
        "corrupted document, not a slow one, and no pixel test would see it. "
        "Both claims are read whole, a character is inserted and then "
        "backspaced, and the ORIGINAL bytes must come back. Parity is the "
        "point: wd_mvup does the odd byte first and steps onto a word's low "
        "byte, wd_mvdn does it last, so the caret is placed at odd and even "
        "tails and at both end stops where the count is 0 or 1. Verified to "
        "go red - dropping wd_mvup's step-back fails every text assertion.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdcombo", "soak", py("tests/wdcombo.py"), 150.0,
        "SPEC.md 68.2.3: Word's three combos are os88ui_drop records rather "
        "than rows of wd_mtab, so the gesture is THREE EVENTS (press, drag, "
        "release) where the pseudo-menu ran one modal poll - and each edge "
        "fails silently on its own. Without W_ONDRAG reaching the record "
        "DR_HOT stays 0FFh and the release picks nothing while leaving the "
        "list on screen; without the press being ROUTED to an open list "
        "before the strip hit tests, the click-then-click spelling puts its "
        "second press on the ruler's indent-drag row and the list never comes "
        "down. All three combos are driven, because each sits in a different "
        "strip with a different hit test in front of it, and the Font one's "
        "list is built at runtime by wd_fontscan and is the only one whose "
        "pick ACTS - picking a face has to reach wd_a_csel and rename the "
        "box, or, when ty_openfam refuses, leave it naming the face that "
        "reads (wd_dfsel). Every cycle ends in PIXELS: the bank is written "
        "back, so it leaves the content bit-for-bit. The second cycle pokes "
        "OS88UI_DR_SEG = 0, which is what a refused claim leaves, and PUTS IT "
        "BACK - a poke that only clears the word orphans ~1.4KB of heap, and "
        "one leak makes Word's next re-layout read its piece table through a "
        "stale segment.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("wdmenusu", "soak", py("tests/wdmenusu.py"), 190.0,
        "SPEC.md 68.2.1: Word's dropdown BANKS the pixels it covers and the "
        "close writes them back (521.4 ms -> 19.7 ms on a 4.77MHz 8088). The "
        "assertion is PIXEL EQUALITY, because a save-under that is fast and "
        "wrong is worse than a repaint that is slow and right: banking the "
        "panel without its drop shadow, clamping differently from wd_mrepair, "
        "or taking the plane count off the wrong display all show up here and "
        "nowhere else. It pokes [wd_suseg] = 0 for the second cycle, which is "
        "what a REFUSED claim leaves behind, so one run checks the banked path "
        "and the wd_mrepair fallback against one reference.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("pkgthumb-tp", "soak", py("tests/pkgthumb.py", "texpad"), 50.0,
        "SPEC.md 13.10.7.2: ...and TexPad, whose TWO bars share one gesture"
        "record. --bar=1 drives the preview pane's.",
        needs=("marty",), serial=True,
        wants=("build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")),
    Row("facescan", "soak", py("tests/facescan.py"), 18.0,
        "SPEC.md 19.8: ty_scan WALKS to SYSTEM/FONTS on the machine and comes "
        "back with every family - a package standing on the apps floppy, told "
        "nothing but OSAPI_VOL_SYS. The unit rows either side of it check the "
        "disk (`fonts`) and the bytes (`pkg`) and neither runs the walk; a "
        "walk that fails is SILENT, because face 0 is the kernel's own cell "
        "and a Font menu one item long looks like a Font menu",
        needs=("marty",), wants=("build/bench360.img",)),
    Row("fmbtn", "soak", py("tests/fmbtn.py"), 60.0,
        "SPEC.md 22.18: the Disk window's two header buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True),
    Row("fsxdisp", "soak", py("tests/fsxdisp.py"), 60.0,
        "Does an fsx bracket take ONE display and dark the others? (SPEC.md"
        "39.18)",
        needs=("marty",), serial=True,
        wants=("build/fsxtest360.img",)),
    Row("knobhd", "soak", py("tests/knobhd.py"), 180.0,
        "SPEC.md 52.10.2.1: a KNOB kernel installed to a hard disk and booted "
        "off it, on BOTH adapters. The build matrix assembles knob kernels and "
        "never boots one; hdboot boots a disk and only the shipped kernel; "
        "every other boot row is a floppy - and the defect needed all three at "
        "once, because the volume boot record is the only loader that has to "
        "be TOLD where the heap starts. REBUILDS build/ and puts it back, and "
        "erases the VHD.",
        needs=("marty",), serial=True, timeout=2400),
    Row("hdboot", "soak", py("tests/hdboot.py"), 120.0,
        "Does os8088 BOOT from the hard disk it was installed to? (SPEC.md "
        "2.9.9) instdeep proves the bytes ARRIVE and every other boot row "
        "boots a floppy, so the volume boot record - a different 512 bytes "
        "with a different loader - was unexercised, and 2.9 broke it. Then "
        "three things about the desktop it reaches, because a hard-disk boot "
        "can look almost right and not be: the loading screen was SEEN, the "
        "clock is drawn IN FULL (the boot overlay's own signature - clk_init, "
        "font_init and desk_init are OVLGATEs), and EVERY CELL OF THE MENU BAR "
        "DROPS ITS OWN MENU. That last one is not decoration: hb_ok reaches "
        "OSAPI_XM_CAPS only when a fixed volume exists, that call answers in "
        "DX:CX as well as BL, and it used to save BX alone - so on an "
        "INSTALLED machine the press's x came back 0, every title hit-tested "
        "at x = 0, and cell 0 is the System menu in every application. Every "
        "menu on the bar dropped the System menu and no floppy-booted machine "
        "could see it (SPEC.md 87.2)",
        needs=("marty",), serial=True),
    Row("instdeep", "soak", py("tests/instdeep.py"), 120.0,
        "SPEC.md 52.10.13: an install reproduces the source disk's WHOLE "
        "tree - the empty SYSTEM/APPDATA and SYSTEM/DOS/OS88NET.COM included, "
        "which one folder level could not reach - AND ITS BYTES (52.10.13.1). "
        "README.TXT is compressed on the shipped floppy, 8,850 bytes against "
        "16,304 expanded, and the installer had two copy shapes chosen by "
        "size: the small one used OSAPI_FILE_READ, which is the TRANSPARENT "
        "read, so the manual was installed EXPANDED with its directory hint "
        "gone while every file too big for the buffer was copied raw and "
        "correctly. The tree check passes either way; only the bytes say "
        "which happened, and one FAT reader reads both sides. It ERASES the "
        "VHD.",
        needs=("marty",), serial=True, timeout=1200),
    Row("hibernate", "soak", py("tests/hibernate.py"), 300.0,
        "SPEC.md 87: Hibernate... writes the machine to the hard disk and the "
        "next boot offers to resume it - the About box is the witness, read "
        "out of the restored instance table; then the same again with "
        "Discard. Builds its own VHD under build/, keyed to the PROCESS - it "
        "was a fixed path, and three concurrent runs then mounted one hard "
        "disk read-write in three emulators (2 runs in 6, at a different leg "
        "every time; docs/WRITING-TESTS.md 5.5)",
        needs=("marty",), serial=True, timeout=1500),
    Row("hibernatedrv", "soak", py("tests/hibernate.py", "--driver"), 300.0,
        "SPEC.md 87 through HDD.DRV: a floppy boot whose SYSTEM.CFG wants the "
        "driver, so C: is a DVK_DRV volume and the resume's transport facts "
        "come through DSV_GEOM",
        needs=("marty",), serial=True, timeout=1500),
    Row("instrest", "soak", py("tests/instrest.py"), 120.0,
        "SPEC.md 52.10.6.1: the installer's ACTION BUTTON reads Install and "
        "then Restart, there is no third button, and clicking it at the end "
        "restarts the machine. The caption is read out of the framebuffer "
        "against the kernel's own glyph table. It ERASES the VHD.",
        needs=("marty",), serial=True, timeout=1200),
    Row("hddcp", "soak",
        py("tests/hddcp.py", "build/os8088-360.img", "build/hddcp-out.bin"),
        90.0,
        "The hard-disk driver's Control Panel page, the two windows behind"
        "it, and SPEC.md 52.6.1's tick-mounts-the-disk. It takes an image"
        "and an output path and DEFAULTS both: registered with neither,"
        "every run died on sys.argv[1] before the emulator started.",
        needs=("marty",), serial=True),
    Row("mediadisk", "soak", py("tests/mediadisk.py"), 30.0,
        "The 360KB MEDIA DISK mounts, and the apps disk keeps MEDIA (SPEC.md"
        "24.4).",
        needs=("marty",), serial=True),
    Row("minexflag", "soak", py("tests/minexflag.py"), 50.0,
        "A wrong flag must not be drawn pixel-identical to a mine (SPEC.md "
        "23): the X over it is light red because a black one lands entirely "
        "inside the black glyph beneath and cannot be seen.",
        needs=("marty",), serial=True, timeout=900),
    Row("minesrc", "soak", py("tests/minesrc.py"), 80.0,
        "SPEC.md 13.11's right button: it flags a Minesweeper cell, and it "
        "does nothing on the strip, on an open cell or on a window that was "
        "not already frontmost.",
        needs=("qemu", "nasm"), serial=True, timeout=900,
        wants=("build/os8088.img", "build/apps.img")),
    Row("tmsmall", "soak", py("tests/tmsmall.py"), 30.0,
        "SPEC.md 28.12: the APP_SMALL Task Manager gates out two of its three "
        "PAGES, which is 39.9% of one heap claim and the largest saving of "
        "any package in the tree - and the two that go share their row "
        "machinery, their check words and their bss chain with the one that "
        "stays. t_appsmall.py reads that off two headers and would pass "
        "unchanged on an arm whose window opens blank. This is an A/B on the "
        "SHIPPED kernel (27.16: a small build is not a second ABI): the full "
        "arm's click cycles the page and the small arm's does not, and the "
        "page they share is compared pixel for pixel.",
        needs=("marty",), serial=True, timeout=900, wants=("build/smallapps360.img",)),
    Row("tmload", "soak", py("tests/tmload.py"), 20.0,
        "SPEC.md 28.7: the CPU meter and the process rows read the same "
        "numbers. The page used to show two figures that contradicted each "
        "other in plain sight and were both right - it charged its own "
        "spinning worker 34-38% of CPU TIME while the graph drew 0-2% of "
        "SPIN COUNT. This compares three readings computed three ways: the "
        "kernel's sch_cycles, the page's tm_load, and the page's tm_pct.",
        needs=("marty",), serial=True, timeout=900),
    Row("curdisk", "soak", py("tests/curdisk.py"), 240.0,
        "SPEC.md 7.4: the arrow TRACKS the hand through a disk transfer. It "
        "used to freeze with the machine and then LEAVE THE SCREEN - once an "
        "operation moved FPG_WARM = 3 sectors the widget armed, and the "
        "unclipped menu_draw_bar inside fpg_arm spent gfx_lock's promised "
        "hide. Both claims here are DIFFERENCES, so the row builds "
        "NOCURDISK=1 itself: a cursor move while [gfx_lock_flag] is set is "
        "unreachable on that arm by mou_apply's own first compare, and a "
        "one-armed reading could not tell that from a test that never "
        "reached a freeze at all.",
        needs=("marty",), alone=True, serial=True, timeout=900),
    Row("fddpark", "soak", py("tests/fddpark.py"), 300.0,
        "SPEC.md 18.100: a Restart leaves the floppy heads on TRACK 0. int "
        "19h resets no hardware, so the next boot inherits drive B's head "
        "where the session left it - which costs 18.97's probe its fast path "
        "on every restart after any use of B:, and above cylinder 77 hands it "
        "the ST0 that RETIRES the drive. The evidence has to be taken before "
        "int 19h, because every emulator here starts the second boot parked "
        "anyway (18.97.4 verified that three ways), so this breaks on "
        "ui_cmd_reboot's own int 19h and reads ST3 off the emulated 765 from "
        "the host. It builds NOFDDPARK=1 itself: reading TRK0 set on one arm "
        "says only that SOMETHING parked the head.",
        needs=("marty",), alone=True, serial=True, timeout=900),
    Row("uiblock", "soak", py("tests/uiblock.py"), 20.0,
        "SPEC.md 8.1.2: ui_task blocks instead of spinning, so an idle "
        "desktop is 97% HALTED and the loop runs 18 times a second instead "
        "of 1,134 - which nothing on screen can show, so the rate is the "
        "only witness. Its last row is the one that matters to a person: the "
        "LOST WAKEUP (8.1.2.3) is a TAIL, not a median, and before the guard "
        "existed it was one mouse event in fourteen waiting a whole tick.",
        needs=("marty",), serial=True, timeout=900),
    Row("schacct", "soak", py("tests/schacct.py"), 90.0,
        "SPEC.md 8.1.1: the scheduler charges a slice only when the task "
        "CHANGES, so an idle desktop - where every switch resumes the task "
        "that was already running - pays the TICK rate and not the switch "
        "rate. The only thing in this tree that can see it: the rule is "
        "exact, so putting the unconditional call back changes no counter, "
        "no screen and no snapshot, and costs 10.9% of a 4.77 MHz 8088. The "
        "books are checked beside it against MartyPC's own cycle counter, "
        "which is an authority outside the kernel's arithmetic.",
        needs=("marty",), serial=True, timeout=600),
    Row("heapscrl", "soak", py("tests/heapscrl.py"), 120.0,
        "SPEC.md 28.4.4: the Task Manager's heap page scrolls, its bar "
        "survives six refreshes of the list beside it (tm_rowr), and a scroll "
        "down and back leaves the rows byte-identical - which is what says "
        "SPEC.md 28.2's per-chunk cache is indexed by the SCREEN row and not "
        "the table row. On a 5150 with a CGA, because the TWO-COLUMN layout "
        "is the one that can put a tm_mrow_nolast blank between the table and "
        "its own end stop and no one-column machine can show it.",
        needs=("marty",), serial=True, timeout=900),
    Row("trkscrl", "soak", py("tests/trkscrl.py"), 80.0,
        "SPEC.md 45.12.2: a jump of n rows in the pattern view costs ONE "
        "gfx_scroll and no full repaint, and what it leaves on the screen is "
        "byte-identical to a repaint of the same view. QEMU, because the "
        "graphics fullscreen is not what a tier-0 machine draws.",
        needs=("qemu", "nasm"), serial=True, timeout=900,
        wants=("build/os8088.img", "build/trkscrl.img")),
    Row("mouseup", "soak", py("tests/mouseup.py"), 60.0,
        "SPEC.md 13.7's release, apps/os88ui.inc's arm, and MOUSEUP-PLAN"
        "4.2's guard.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img",)),
    Row("paintgif", "soak", py("tests/paintgif.py"), 40.0,
        "HOW LONG DOES PAINT TAKE TO OPEN OS8088.GIF? - in GUEST CYCLES",
        needs=("marty",), serial=True),
    Row("paintlzw", "soak", py("tests/paintlzw.py"), 40.0,
        "SPEC.md 42.21: ...and WHICH HALF of it. paintgif times the whole "
        "operation, which is the right shape for a regression that could be "
        "anywhere and cannot say where this one was: the decode was 12,547 ms "
        "and 999 cycles a pixel of it were the LZW loop against 169 in "
        "pt_line_put, because the reader emitted one pixel per near call with "
        "two more around it for the character stack. Breakpoints on Paint's "
        "own labels split pt_gif_in four ways and then split the decode "
        "again, pt_line_put against the loop that feeds it. The ceilings are "
        "loose on purpose - they catch a return to the old SHAPE, not a "
        "picture whose dither packs differently",
        needs=("marty",), serial=True),
    Row("paintanchor", "soak",
        py("tests/paintanchor.py", "--machine", "os8088_5150_herc_gla"), 50.0,
        "SPEC.md 11.90.3: a pure SHRINK owes its content nothing. ui_grow"
        "repaints the union of the old rect and the new, so the window used to"
        "be told to draw all of itself about pixels nothing painted over."
        "Asserts pt_blit is entered with an EMPTY rect, that every surviving"
        "canvas pixel is byte-identical across the drag - which is what makes"
        "skipping it legitimate rather than lucky - and that the vacated"
        "columns went back to the desktop",
        needs=("marty",), serial=True),
    Row("paintshrink", "soak",
        py("tests/paintshrink.py", "--machine", "os8088_5150_herc_gla"), 60.0,
        "SPEC.md 42.17: a shrink that would lose ink gives back what it CAN."
        "Refused used to mean pinned where it started, so one stroke kept the"
        "whole canvas. Inks a stroke at a known place, types a size well past"
        "it into each size box in turn - the GROW BOX cannot reach, the window"
        "has a minimum - and reads what pt_resize was handed, because"
        "pt_szapply resizes the window and pt_track re-fits the width back up"
        "before anything else can look",
        needs=("marty", "nasm"), serial=True),
    Row("paintrz", "soak", py("tests/paintrz.py"), 120.0,
        "SPEC.md 42.19.1: does a resize still have the PICTURE afterwards?"
        "pt_resize used to carry it through the undo image, where every walk"
        "order is safe because the source is a buffer of its own; it moves the"
        "picture where it lies now, and the order IS the correctness argument."
        "The first build had it inverted and every other paint row passed -"
        "they all resize the one way the wrong answer survives - while it"
        "wiped the picture from the middle of the canvas down. Two strokes far"
        "apart, then five resizes: each axis in each direction, and the two"
        "disagreeing (which is what the two passes exist for). The ink is read"
        "out of the CANVAS and compared as a SET, so neither a repaint nor a"
        "smear inside the bounding box can flatter it",
        needs=("marty", "nasm"), serial=True),
    Row("paint1bpp", "soak",
        py("tests/paint1bpp.py"), 40.0,
        "SPEC.md 42.23: is the canvas ONE BIT a pixel, and is the DIB in"
        "front of it a valid 1bpp BMP? The claim is the assertion and not"
        "the pixels - 448x258 is 14.2KB one bit deep against 56.6 packed,"
        "which on the 128KB floor machine is the difference between the full"
        "default picture and 42.6.5's letterbox, and NOT ONE other paint row"
        "would notice a canvas that came out four times bigger than it had"
        "to be. The header is checked field by field against the live"
        "geometry because the canvas IS the file (42): a save is one write"
        "of it, so a header that lies is a file no host can open. A blank"
        "canvas must read all 0xFF, which is 42.23.1's polarity - 1 is"
        "WHITE, both because that is what a 1bpp BMP means and because"
        "gfx_blit1 takes a band that way up at 12.5 clocks a byte instead of"
        "the complementing loop's 17",
        needs=("marty",), serial=True),
    Row("paint1bpp-colour", "soak",
        py("tests/paint1bpp.py", "--machine", "os8088_xt_vga", "--colour"),
        30.0,
        "...and THE NEGATIVE, which is the half that would drift silently: a"
        "COLOUR adapter is untouched by 42.23 - four planes, sixteen"
        "colours, a 118-byte DIB and the 4bpp arithmetic to the byte. A new"
        "canvas opens in colour on a VGA and only a mono screen opens one"
        "bit. Nothing else in tests/ asserts a negative about the format, so"
        "a change that made EVERY canvas one bit deep would pass the whole"
        "suite and quietly cost the VGA fifteen of its colours",
        needs=("marty",), serial=True),
    Row("paintpal", "soak",
        py("tests/paintpal.py"), 90.0,
        "Does SPEC.md 42.26.1's COMPOSED palette draw what the fill, the "
        "frame and the sprite pass drew? One boot draws it both ways - the "
        "second with stc/ret poked over the gfx_blit1 thunk, which is the "
        "refusal kern_small answers - and compares the pixels. The greyed "
        "fill glyph is the round that matters: it is the one thing in the "
        "composition with no primitive behind it. The third round is a "
        "CONTROL that must DIFFER, or a rect that missed the palette would "
        "pass the first two.",
        needs=("marty",)),
    Row("paint1blit", "soak",
        py("tests/paint1blit.py"), 90.0,
        "SPEC.md 42.23.4: the TWO paths a one-bit canvas reaches the screen"
        "by, compared. kern_big has gfx_blit1 and blits the band straight in;"
        "kern_small carries the SLOT AND NOT THE BODY (5.4.2), so Paint"
        "expands each row for gfx_blit4 instead - and the two must draw the"
        "same picture to the pixel. Neither arm alone would catch a wrong"
        "one: the fast path could draw a plausible picture one row or one"
        "byte out, and the fallback is what every other 1bpp row already"
        "exercises. The fixture is BUILT here, every byte differing from its"
        "neighbours, because a flat picture passes all three of those"
        "mistakes. It is also what makes 42.23.4's negative-stride claim a"
        "checked fact rather than a second piece of reasoning",
        needs=("marty",), serial=True),
    Row("paint1load", "soak",
        py("tests/paint1load.py"), 30.0,
        "SPEC.md 42.23.6: does a 1bpp BMP LOAD? The one path in 42.23 no"
        "other row reaches - pt_line_put's bit arm, pt_fmtpick running before"
        "pt_adopt, and the fixture is BUILT here rather than committed"
        "because what it has to be is a pure function of what the reader is"
        "being asked: a pattern whose every byte differs from its neighbours,"
        "so a row read one byte early or one bit out of phase cannot come"
        "back looking right. The oracle is the CANVAS against the FILE and"
        "not the screen - a one-bit canvas is byte-for-byte a 1bpp BMP's"
        "pixel rows (42.23.2) - and [pt_trunc] must be CLEAR, because"
        "nothing was reduced here and File > Save has to stay allowed",
        needs=("marty",), serial=True),
    Row("paint1load-vga", "soak",
        py("tests/paint1load.py", "--machine", "os8088_xt_vga"), 40.0,
        "...and the same on a COLOUR adapter, which is the interesting leg:"
        "42.23.6 opens a colourless file one bit deep on ANY card, so this is"
        "a one-bit canvas drawn through the planar renderer - pt_blit_1's"
        "expansion into pt_line and gfx_blit4, on the machine whose every"
        "other canvas is four planes",
        needs=("marty",), serial=True),
    Row("paintrz-1bpp", "soak",
        py("tests/paintrz.py", "--machine", "os8088_5150_herc_gla"), 120.0,
        "...and the ONE-BIT canvas, which is a different move: one run of"
        "bits a row instead of four plane-runs whose three inner boundaries"
        "all shift with the width (SPEC.md 42.23, 42.13.2). IT WAS"
        "`paintrz-packed` and the rename is the point: since 42.23 a Hercules"
        "gives Paint a one-bit canvas and not a packed one, so the row that"
        "read `packed` in its name had stopped covering that format. Packed"
        "4bpp is now reached two ways - a COLOUR file opened on a 1bpp"
        "adapter, and gfx_blitp refusing on a colour one - and `paintpack` is"
        "the row that forces the second",
        needs=("marty", "nasm"), serial=True),
    Row("alertbtn", "soak",
        py("tests/alertbtn.py", "--machine", "os8088_5150_herc_gla"), 80.0,
        "SPEC.md 75.3.0: the STANDARD alert's button row - os88ui's and not"
        "Paint's, which is only the alert easiest to raise. One press must"
        "draw ONE button (os88ui_adn redrew the whole row, so a press lettered"
        "three where one was needed) and the row must TRACK: held and dragged"
        "off, the button comes up, which is what says the gesture is cancelled"
        "before the finger commits. Also checks 42.16.1's GIF default",
        needs=("marty",), serial=True),
    Row("alertanim", "soak",
        py("tests/alertanim.py", "--machine", "os8088_5150_herc_gla"), 60.0,
        "SPEC.md 11.99.2.1: the 'Save changes?' alert must NOT zoom open. The"
        "user clicked the close box and got a dialog instead, so a third of a"
        "second of outline in front of it is the machine making a show of"
        "getting in the way. AN A/B AND NEITHER HALF IS A ROW ALONE: a LAUNCH"
        "must still animate, or a theme with the zoom off would pass this"
        "trivially, and the alert must be on the glass at the end, or a dialog"
        "that failed to open reads as one that opened quietly",
        needs=("marty",), serial=True),
    Row("paintdirty", "soak", py("tests/paintdirty.py"), 70.0,
        "SPEC.md 42.16: does Paint ask before it throws a picture away? A"
        "FLAG and not Note Pad's checksum, so the places that set and clear it"
        "are the whole feature - opened 0, MAXIMIZED AND RESTORED 0 (a resize"
        "really does change the document, and firing there would ask about a"
        "blank picture nobody drew on), one stroke 1, and the close box"
        "refuses and puts an alert up",
        needs=("marty",), serial=True),
    Row("paintcull", "soak", py("tests/paintcull.py"), 70.0,
        "SPEC.md 5.4.3.3: does 11.3.3's CULL cost Paint its four planes? An"
        "armed clip region is one of gfx_blitp's refusals and pt_topacked"
        "reads any refusal as a fact about the MACHINE, so one ordinary"
        "damage repaint converted a VGA canvas to nibbles for the session."
        "Open, ONE STROKE - 42.15 answers a blank canvas with a fill and"
        "never blits - maximize, restore; asserts [pt_planar] at each step and"
        "reports which guard fired if it did not hold",
        needs=("marty",), serial=True),
    Row("paintplan", "soak", py("tests/paintplan.py"), 60.0,
        "SPEC.md 42.13: is Paint's PLANAR canvas the picture? Opens"
        "OS8088.GIF and compares the screen against the FILE, so the GIF"
        "decoder, pt_line_put's packing into four planes and gfx_blitp are"
        "all inside one answer",
        needs=("marty",), serial=True),
    Row("blitp", "soak", py("tests/blitp.py"), 120.0,
        "SPEC.md 5.4.3: does gfx_blitp put the bytes where it was given them?"
        "Reads the four PLANES rather than the rendered frame - which below"
        "the raster is last frame's, and reads exactly like a blit that"
        "stopped halfway. Needs `make bench`",
        # ...and SAYS SO to the runner, not only to the reader. `all` does not
        # build build/gfxbench.o88, so this row failed at HEAD and at the base
        # alike and was written up as a pre-existing defect
        # (docs/plans/HANDOFF-SOAK-FINDINGS.md F1). With the artefact present it
        # passes: 256 plane-rows, 2,048 bytes, all as given. B4's shape again -
        # the suite modelling tools rather than artefacts.
        needs=("marty", "nasm"), serial=True,
        wants=("build/gfxbench.o88",)),
    Row("blitpair", "soak", py("tests/blitpair.py"), 90.0,
        "SPEC.md 5.4.1.1: is the 1bpp canvas the PICTURE? OS8088.GIF is two"
        "colours, so 39.4 sends every pixel to a solid class and the"
        "framebuffer can be compared against the FILE - which is the only"
        "thing that can see sw_blit_row's tables read through the wrong"
        "segment (5.4.1.3 moved them to .lowbss)",
        needs=("marty",), serial=True),
    Row("paintdraw", "soak", py("tests/paintdraw.py"), 70.0,
        "SPEC.md 42.13: does DRAWING on the planar canvas touch only what it"
        "drew? paintplan covers the routines that write a whole row; this one"
        "covers pt_rect, which is the pencil's dab and builds a left mask, a"
        "right mask and a byte count the packed path gets from one shift",
        needs=("marty",), serial=True),
    Row("paintsu", "soak", py("tests/paintsu.py"), 120.0,
        "SPEC.md 11.96.11: on a 1bpp adapter Paint banks its WHOLE content"
        "rather than the tool column, because there the cache is ~9KB and the"
        "canvas it saves redrawing is 399 ms. Asserts the size asked for, that"
        "no canvas blit crosses an uncover, and that what came back is right",
        needs=("marty",), serial=True),
    Row("paintfill", "soak", py("tests/paintfill.py"), 80.0,
        "SPEC.md 42.13.2: does the FLOOD FILL find the same edges the picture"
        "has? pt_fpix gathers one bit per plane, so a plane addressed wrongly"
        "does not corrupt anything - it makes the fill see a picture that is"
        "not there. The oracle is a flood fill on the host over the same file",
        needs=("marty",), serial=True),
    Row("paintbig", "soak", py("tests/paintbig.py"), 150.0,
        "SPEC.md 42.13.2: GROW the canvas, which is the only thing that"
        "changes [pt_bpr] - the one number the two storage formats do not"
        "share - then copy a block past the clipboard's 4KB floor and paste"
        "it back at a DIFFERENT bit phase (SPEC.md 42.13.3), which is what"
        "makes both shifts and both edge masks run. Nothing else resizes",
        needs=("marty",), serial=True),
    Row("paintback", "soak", py("tests/paintback.py"), 180.0,
        "SPEC.md 11.96.11.4 and 42.13.1.3: a window dragged clear onto the"
        "other card and home again - the PICTURE first, because the stale BX"
        "this caught had the kernel writing zeros into its own .text and the"
        "damage lands wherever the layout puts it; then [pt_planar], because"
        "the canvas has to come home as four planes. The herc leg is the only"
        "row here on a machine whose colour card is not the primary",
        needs=("marty",), serial=True),
    Row("paintrow", "soak", py("tests/paintrow.py"), 50.0,
        "SPEC.md 42.13.1.2: pt_line_get's FOUR-PLANE row reader, whose only"
        "caller is the GIF writer - so nothing that draws can fail on it and"
        "no screenshot here can see it. Calls the routine directly, through"
        "five bytes written over pt_blit's entry, and compares the colour"
        "classes it returns against the file's",
        needs=("marty",), serial=True),
    Row("paintwipe", "soak", py("tests/paintwipe.py"), 30.0,
        "SPEC.md 42.13.2.1: is a BLANK canvas blank? Every other paint row"
        "opens a picture and compares it against the file, which is the one"
        "oracle that cannot see pt_wipe - a wrong ground is not a difference"
        "from the file, it is what every comparison starts from. Hercules,"
        "because 42.13 stores the canvas packed only on a 1bpp adapter and"
        "the body that was wrong is the one VGA never runs - the GLaBIOS twin"
        "of it, since this row takes no timing and ibm5150_82_v4 is not in"
        "this tree.",
        needs=("marty",), serial=True),
    Row("paintpack", "soak", py("tests/paintpack.py"), 210.0,
        "SPEC.md 42.13.1: the REFUSAL path. Builds the NOPLANE kernel, where"
        "every gfx_blitp says no in six bytes, so Paint's pt_topacked runs"
        "for real and the nibbles it produced are compared against the file,"
        "then paintbig again over it - the only kernel on which the PACKED"
        "half of pt_copy/pt_paste runs without a second monitor. Rebuilds the"
        "tree, like blitplane",
        needs=("marty", "nasm"), serial=True),
    Row("bouncecost", "soak", py("tests/bouncecost.py"), 20.0,
        "SPEC.md 14/2.6: what one Bounce frame costs the machine in 8088 "
        "cycles. Its PERIOD is task_sleep's, so nothing here can move its "
        "frame rate - what it measures is how much of a 4.77MHz machine one "
        "live Bounce takes away from the UI, which is the quantity SPEC.md "
        "2.6's cadence test is an argument about. Frames are PAIRED on the "
        "ball's (x,y,vx,vy) and the trajectory is seeded, so two kernels are "
        "compared frame for frame and the per-frame variation - which is real "
        "work, not noise - cancels instead of being averaged over.",
        needs=("marty",), serial=True, timeout=600),
    Row("atkey", "soak", py("tests/atkey.py"), 100.0,
        "WHAT ONE ARTFULTYPE KEYSTROKE COSTS on a 4.77MHz 8088, in guest "
        "cycles off at_onkey's entry to its return. Nothing in this tree had "
        "ever measured this app - SPEC.md 46.1 states a contract and every "
        "millisecond attached to it was PREDICTED - so this is the row that "
        "makes the figures readings. It prints the SCENE with the number, "
        "because the answer depends entirely on how many visual lines the "
        "caret's PARAGRAPH has: at_apply_edit repaints at_dfrom..+at_rlk-1 "
        "and at_relayout sets at_dfrom from at_lhome, the paragraph's first "
        "visual line. A keystroke figure without its paragraph length is not "
        "a figure. It reads at_rlk back afterwards, which is what turns "
        "46.1's honest 'that paragraph's visual lines' into a table.",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("atmenusu", "soak", py("tests/atmenusu.py"), 45.0,
        "SPEC.md 46.5.1: ArtfulType's pull-down banks the pixels it covers and "
        "the close writes them back, instead of repainting every text line the "
        "panel crossed FULL WIDTH - 104.9 ms to 14.5 on a 4.77MHz 8088. THE "
        "ASSERTION IS PIXEL EQUALITY and it is the only one worth making: a "
        "save-under that is fast and wrong is worse than a repaint that is "
        "slow and right, and every way of getting it wrong shows up in a "
        "photograph - the shadow left out of the bank, the rect clamped "
        "differently from the erase, the plane count taken from the wrong "
        "display. It dismisses the menu WITHOUT PICKING, by releasing while "
        "still over the title, because every item runs a command that "
        "repaints the screen and would hide the error. The second cycle pokes "
        "[at_suseg] = 0 while the panel is down - what a refused claim leaves "
        "behind - so one run checks the write-back and the repaint fallback "
        "against one reference. Verified to go red: leaving the drop shadow's "
        "ROW out of the bank is 68 differing pixels on exactly that row. It "
        "BUILDS NOTHING - it reads the shipped disks, so it declares them in "
        "wants= and shares the emulator lane.",
        needs=("marty", "nasm"), serial=True, timeout=900,
        wants=("build/os8088-360.img", "build/apps360.img")),
    Row("atblit", "soak", py("tests/atblit.py"), 165.0,
        "SPEC.md 46.4.2: does ArtfulType's BAND emit draw the same picture as "
        "the expander it replaced? at_draw_line hands at_compose's 1bpp strip "
        "straight to OSAPI_GFX_BLIT1 now instead of widening it to packed "
        "4bpp for OSAPI_GFX_BLIT4, which means the strip's POLARITY flipped - "
        "and every way of getting that wrong is a plausible-looking wrong "
        "picture rather than a crash. Miss one of the five writers into "
        "at_strip1 and that element renders inverted; the fifth is at_bigtext "
        "in atui.inc, which an audit of atrend.inc misses. Complement above "
        "at_glyph's italic rcr chain and every italic grows a bar down its "
        "left edge. Forget AT_X4TAB or atimg.inc's xor and the 4bpp fallback "
        "draws the negative - which no kern_big row would ever execute. So "
        "the gate is 0 differing pixels against NOATBLIT1=1, which assembles "
        "byte for byte identical to the package before the change. It PACES "
        "ITS TYPING on the app's own at_caret: type_text outruns a 4.77MHz "
        "ArtfulType, the key queue overflows, and the two arms then receive "
        "different documents - which reads exactly like a rendering bug. "
        "Rebuilds the tree, like blitplane, because the A/B is two packages. "
        "It GENERALISES: --knob picks which of ArtfulType's A/Bs to run and "
        "every one of them must draw the identical picture, so a wave adds a "
        "knob rather than a row. Six scenes, and three of them exist because "
        "a break test came back green - the SPLASH is at_bigtext and "
        "at_drawimg, which an audit of atrend.inc misses; the ZOOMED-IN one "
        "is the only state in which a plain line renders above scale 1 "
        "(SPEC.md 46.4.9); and the document's mid-paragraph edit had to gain "
        "two ArrowUps before anything could be pushed past a wrap "
        "(SPEC.md 46.4.11).",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("blitplane", "soak", py("tests/blitplane.py"), 180.0,
        "SPEC.md 5.4.1.3: does gfx_blit4's PLANAR DECODER draw the same "
        "pixels as the run writer, on both destination phases, and is it "
        "still several times quicker? Rebuilds the tree - one of two rows "
        "that do, with gfxlk - because the A/B is two kernels. It drives "
        "Paint OFF THE BYTE GRID on purpose: since SPEC.md 42.13 a canvas on "
        "the grid is four planes and repaints through gfx_blitp, so a window "
        "left where it opens does not reach this primitive at all.",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("blitcut", "soak", py("tests/blitcut.py"), 330.0,
        "SPEC.md 39.14.7.2: does a STRADDLING gfx_blit4 draw the same pixels "
        "cut at the seam as it does whole-virtual, and is it several times "
        "quicker? blitplane's shape one seam along, and rebuilds the tree for "
        "the same reason - the A/B is two kernels, this one and NOBLITCUT=1. "
        "It needs a two-card machine and a window DRAGGED across the seam: "
        "the drag is what makes gfx_blitp refuse and Paint convert its canvas "
        "to nibbles (SPEC.md 42.13.1), which is what puts the block through "
        "gfx_blit4 at all, and a W_X written by hand would skip it.",
        needs=("marty", "nasm"), serial=True, timeout=1800),
    Row("paintmove", "soak", py("tests/paintmove.py"), 150.0,
        "Compact the heap out from under a LIVE Paint canvas (SPEC.md"
        "66.2/42).",
        needs=("marty",), serial=True,
        wants=("build/heapfrag360.img",)),
    Row("rdmove", "soak", py("tests/rdmove.py"), 150.0,
        "Compact the heap out from under the RAM disk's store (SPEC.md"
        "66.5.10).",
        needs=("marty",), serial=True),
    Row("hdmove", "soak", py("tests/hdmove.py"), 120.0,
        "Compact the heap out from under a DONATED listing claim (SPEC.md "
        "66.5.10.2) - the only claim in the tree with three holders, two of "
        "them the kernel's and on the far side of the ABI from the callback. "
        "A declaration is not a mechanism: check 1 is that the block MOVED, "
        "and check 4b that no word anywhere still holds the old base",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("heaphi", "soak", py("tests/heaphi.py"), 90.0,
        "A driver's second image goes at the TOP of the heap (SPEC.md "
        "50.3.2.1). The user's sequence - tick Hard Drive, tick Ram Disk, "
        "select its page - and then the number mem_claim answers a claim "
        "from rather than the one mem_avail prints: a compaction must still "
        "be worth the 63KB read-ahead. RAMPAGE.DRV claimed low first-fits "
        "ABOVE the movable claims and pinned the arena into two pieces, "
        "which moved that number by 64.5KB and the LIVE largest run by "
        "nothing at all",
        needs=("marty",), serial=True),
    Row("modstr", "soak", py("tests/modstr.py"), 60.0,
        "modstr - a module's own strings letter correctly (SPEC.md 2.8.6). "
        "The bytes, out of fm_hdrbuf and toast_buf, because a string read "
        "through DS instead of CS lands in kernel code and letters plausible "
        "rubbish rather than faulting",
        needs=("marty",), serial=True),
    Row("diskclone", "soak", py("tests/diskclone.py"), 120.0,
        "diskclone - Clone Disk... (SPEC.md 18.99/22.21) driven end to end, "
        "with the assertion that cannot pass for the wrong reason: the two "
        "floppies read back off the guest and diffed byte for byte. Cross "
        "drive, same drive, the un-swapped-disk guard and Esc",
        needs=("marty",), serial=True),
    Row("rdup", "soak", py("tests/rdup.py"), 60.0,
        "SPEC.md 62.9.11.3: the Ram Disk page acts on the RELEASE.",
        needs=("marty",), serial=True),
    Row("sbar", "soak", py("tests/sbar.py"), 60.0,
        "SPEC.md 13.10: the shared scroll bar, and the two kernel bars are"
        "one now.",
        needs=("marty",), serial=True,
        wants=("build/muptest.img",)),
    Row("sizesnap", "soak", py("tests/sizesnap.py"), 20.0,
        "the SIZE snap aligns a content width WITHOUT shrinking the zoom "
        "(SPEC.md 11.94.5) - a maximized window must stay x=0, w=[vid_pw]",
        needs=("marty",), serial=True),
    Row("telnet", "soak", py("tests/telnet.py", "--machine",
                             "os8088_5150_cga_gla"), 300.0,
        "SPEC.md 70.8: TELNET's 80x25 screen of CHARACTER AND ATTRIBUTE, both "
        "renderers and the 1bpp polarity rule. Seven assertions and no wire - "
        "the transport is tests/socktest's - and the two defects 70.8.8 "
        "records are the last two: full screen never scrolled at all, and the "
        "kept worker parked on the gfx lock the FSX bracket holds, which "
        "SPEC.md 53.2 calls death by another name for a feeder. The GLaBIOS "
        "twin because the default machine wants the licensed IBM ROM",
        needs=("marty",), serial=True),
    Row("telnetherc", "soak", py("tests/telnet.py", "--adapter", "herc",
                                 "--machine", "os8088_5150_herc_gla"), 300.0,
        "...and the same seven on the OTHER 1bpp adapter, which is not a "
        "duplicate: Hercules is 720 wide, so the window shows all EIGHTY "
        "columns there and CGA shows 79 of them and only 13 rows - the "
        "viewport arithmetic (70.8.10) is a different answer on each, and the "
        "full-screen framebuffer is B000 rather than B800 with the MDA "
        "attribute mapping (70.8.9) under it",
        needs=("marty",), serial=True),
    Row("telpen", "soak", py("tests/telpen.py"), 300.0,
        "SPEC.md 5.4.2.2.1: gfx_blit1's pen used to REFUSE a pair whose two "
        "colours share no plane in either direction - green on red, and most "
        "of the sixteen-colour pairs a board's art is made of - and the Map "
        "Mask splits the band between two passes now. The only row in the "
        "tree that can see it: the pen is not read on a 1bpp adapter at all, "
        "and mode 12h has no flat framebuffer, so it is os8088_xt_vga plus "
        "`fbuf`. Every cell is rendered on the HOST out of the guest's own "
        "glyph table and compared pixel for pixel",
        needs=("marty",), serial=True),
    Row("telansi", "soak", py("tests/telansi.py"), 900.0,
        "SPEC.md 70.9/70.10/70.12: the ANSI-BBS PARSER on the machine, against "
        "tools/ansisim.py - the same state machine in Python, and the "
        "contract's second reader the way htmsim.py is the browser's. Thirteen "
        "fixtures from tests/fixtures/ansi/ are fed by tools/os88bbs.py in "
        "deliberately RAGGED fragments, and te_scr is read out of guest memory "
        "and compared with the simulator's 4,000 bytes CHARACTER AND ATTRIBUTE "
        "- the oracle computed at test time, never stored, so it cannot drift "
        "from the reference renderer. Then the negotiation and both "
        "subnegotiations out of the server's own log (a screenshot cannot see "
        "a byte this end SENDS), the mirror against a second server asking for "
        "an option this terminal does not implement, the DSR and DA answers, "
        "the twelve special keys as the exact bytes on the wire, Enter as a "
        "BARE CR under TRANSMIT-BINARY, the Zmodem trigger's handover offset, "
        "and full screen as a memcmp of te_scr against text VRAM. QEMU by "
        "name for tests/ethernet.py's reason: MartyPC has no NIC, so this "
        "package's receive path cannot be reached on it at all",
        needs=("qemu",), serial=True, builds=True),
    Row("telzm", "soak", py("tests/telzm.py"), 300.0,
        "SPEC.md 70.11/70.12: ZMODEM RECEIVE end to end, with the bytes read "
        "back OFF THE DISK. tools/os88bbs.py's pure-Python sender sends two "
        "batches over one boot: first the rows of its own MANGLE83_CASES as "
        "tiny files - seven dialogs answered with Return, five cancelled with "
        "Escape and TWO OF THOSE ADJACENT, which is the case that proves a "
        "cancelled dialog does not poison the one after it - and [tz_name] read "
        "out of guest memory - which is what stops the 8086's copy of SPEC.md "
        "77.20's 8.3 rule drifting from the host's, since the two share no "
        "code - and the committed ones asserted again as DIRECTORY ENTRIES in "
        "MEDIA/, which is where SPEC.md 38.10 opens a Save dialog for an "
        "application that has chosen nowhere. It runs LAST because a "
        "subdirectory here is ONE 512-byte cluster - sixteen entries - and does "
        "not grow. Then one file under 4KB (one "
        "chunk) and one of about 40KB (many, spanning both staging halves and "
        "ten commits), saved with Return and read back off build/telnetsys.img "
        "by an independent FAT12 reader and compared BYTE FOR BYTE - and the "
        "terminal's own 2,000 cells asserted BLANK afterwards, because not one "
        "byte of a transfer may reach the ANSI parser. Then a sender that "
        "declares a size of 1 for a file it sends in full, which is what used "
        "to divide by it and raise #DE on a kernel with no int 0 handler "
        "(SPEC.md 70.11.6). Finally a CANCEL - Escape, then Return on the next "
        "file - proving a ZSKIP ends one file and not the batch, and the "
        "headers out of the server's JSON log: the ZRINIT this end advertises "
        "(CANFDX|CANOVIO, buffer size 0, and NOT CANFC32), the ZRPOS, the "
        "ZACKs, and the ZNAK that refuses the one deliberate ZBIN32 header. "
        "QEMU by name for tests/ethernet.py's reason: MartyPC has no NIC",
        needs=("qemu",), serial=True, builds=True),
    Row("netpromise", "soak", py("tests/netpromise.py"), 90.0,
        "SPEC.md 70.7/77.47: Telnet and the FTP server promise per DEBT, not"
        "per session.",
        needs=("marty",), serial=True),
    Row("monoink", "soak", py("tests/monoink.py"), 40.0,
        "SPEC.md 11.96.17.1's hook reaches font_run and not font_str - it must "
        "not perturb the path it does not serve, and the gap is measured",
        needs=("marty",), serial=True),
    Row("su1bpp", "soak", py("tests/su1bpp.py"), 50.0,
        "SPEC.md 11.96.17: a two-colour window's raise cache is ONE plane, a "
        "quarter the size, and puts back the pixels a full repaint would",
        needs=("marty",), serial=True),
    Row("win1bpp", "soak", py("tests/win1bpp.py"), 70.0,
        "SPEC.md 11.96.17's two-colour declaration reaches W_FLAGS, and clear "
        "of SPEC.md 7.2.1's cursor shape in the same byte",
        needs=("marty",), serial=True),
    Row("tpsaveu", "soak", py("tests/tpsaveu.py"), 50.0,
        "TeXPad - the largest window in the tree, and the one four planes "
        "cannot fund - keeps its pixels one plane deep (SPEC.md 11.96.17)",
        needs=("marty",), serial=True),
    Row("tmrup", "soak", py("tests/tmrup.py"), 60.0,
        "SPEC.md 13.8: the Timer's three buttons fire on the RELEASE.",
        needs=("marty",), serial=True),
    Row("kernresident", "full", py("tests/kernresident.py"), 20.0,
        "kernel.asm rule 3: kern_big fully RESIDES in KERN_RESIDENT_KB at a "
        "bare desktop - the half of the rule an assembler cannot see, which "
        "is a claim made at boot and never given back.",
        needs=("marty",), serial=True),
    Row("zoomsave", "soak", py("tests/zoomsave.py"), 150.0,
        "SPEC.md 11.96.16.2: a window ZOOMED over another banks it - the "
        "precover pass had one caller and a maximize took 0 caches.",
        needs=("marty",), serial=True),
    Row("dmgcull", "soak", py("tests/dmgcull.py"), 50.0,
        "SPEC.md 11.3.3: a marked window does not paint where something above "
        "it is about to - 452 cells under the mover became 26. Counts CELLS "
        "and not calls, because a culled cell is still a call.",
        needs=("marty",), serial=True),
    Row("tmdmg", "soak", py("tests/tmdmg.py"), 60.0,
        "SPEC.md 28.10.2: a partial repaint of the Task Manager draws only "
        "the part - 225 cells put on the glass became 0, against 549 for a "
        "whole repaint. CELLS and not calls (11.3.3).",
        needs=("marty",), serial=True),
    Row("tmrepair", "soak", py("tests/tmrepair.py"), 80.0,
        "SPEC.md 28.11: the Task Manager's quiet pages hold a raise cache by "
        "REPAIRING at the restore - a whole-content band, and tm_update "
        "spends the debt W_PAINT is handed. **IT IS INTERMITTENT AND HAS "
        "BEEN FOR A WHILE**, which is worth knowing before anybody calls a "
        "red one a regression: rated with tools/os88bisect.py it fails 3 of "
        "4 at b49fff1 - a tree where one soak reported it PASSING - 2 of 3 "
        "at b5cef54, 1 of 3 at 7f5c07a and 1 of 4 at dc3b200, so today's head "
        "is the best of every point measured. The failing leg is REPAIR: the "
        "promise is made (WF_SAVEU and a whole-content band) and is gone by "
        "the uncover with ZERO wm_su_drop calls for it, so whatever "
        "withdraws it is not that path. A rate is not a side, so there is "
        "nothing here to bisect until the row is 0/N or N/N",
        needs=("marty",), serial=True),
    Row("tmselfsu", "soak", py("tests/tmselfsu.py"), 300.0,
        "SPEC.md 28.8.1: the Task Manager stops repainting for ITS OWN raise "
        "cache and so gets to keep one - and still sees everybody else's, "
        "which is what makes the cut the self-reference and not the range. "
        "Also the row that would notice tm_quiet's key going unrecorded again",
        needs=("marty",), serial=True),
    Row("tmowner", "soak", py("tests/tmowner.py"), 300.0,
        "SPEC.md 28.4.5: a raise cache is listed under the PACKAGE that owns "
        "the window, and a kernel window's stays under System. Reads the rows "
        "the page COMPOSES rather than the pixels, which is the only way to "
        "say which group a row is in",
        needs=("marty",), serial=True),
    Row("tmground", "soak", py("tests/tmground.py"), 60.0,
        "SPEC.md 28.10: the Task Manager paints its own ground, so a repaint"
        "is not a 450ms white hole.",
        needs=("marty",), serial=True),
    Row("trackmove", "soak", py("tests/trackmove.py"), 150.0,
        "Compact the heap out from under a LOADED module (SPEC.md 66.5.2/45).",
        needs=("marty",), serial=True,
        wants=("build/trackmove360.img",)),
    Row("tpdraw", "soak", py("tests/tpdraw.py"), 300.0,
        "Does TeXPad's INCREMENTAL source redraw draw what a full repaint"
        "draws? (SPEC.md 69.8)",
        needs=("marty",), serial=True),
    Row("trkrate", "soak", py("tests/trkrate.py"), 120.0,
        "trkrate - XT mode's second rate, and the surface it refuses (SPEC.md"
        "45.9.3)",
        needs=("marty",), serial=True,
        wants=("build/trklog360.img",)),
    Row("trktxsurf", "soak", py("tests/trktxsurf.py"), 180.0,
        "The fullscreen SURFACE is a pick, not XT mode's - text at a 45.10"
        "rate (SPEC.md 45.13.7)",
        needs=("marty",), serial=True,
        wants=("build/trkship360.img",)),
    Row("wmartifact", "soak", py("tests/wmartifact.py"), 260.0,
        "Two window-manager artifacts, reproduced with NO package of ours"
        "involved.",
        needs=("marty",), serial=True),
    Row("xorrect", "soak", py("tests/xorrect.py"), 20.0,
        "gfx_xor_rect draws the same pixels it always did (SPEC.md 39.14.10)",
        needs=("marty",), serial=True),
]


def rows():
    """Every registered row, cheap ones first so failures report early."""
    return FAST + FULL + SOAK
