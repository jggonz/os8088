#!/usr/bin/env python3
"""Every build configuration `make all` does not build.

    python3 tests/unit/t_buildmatrix.py [-j N]

`all` builds ONE kernel.  The tree has several dozen more - the ROSTER is the
Makefile's own $(KNOBS) list, read by makefile_knobs() rather than kept here,
and KNOBS below is the hand table of the VALUE each one needs; a knob in the
Makefile with no row here fails this file - and nothing builds them until
somebody types the knob by hand:

  * `kern_small` - the 128-256KB machine's kernel (SPEC.md 62.9.15), a
    genuinely different binary with its own `KERN_BUDGET`, its own driver set
    and whole features compiled out behind `%ifdef KERN_BIG`. `make small` is
    a separate target and `all` does not depend on it, so a change that breaks
    it is invisible until a release.
  * the testing knobs - `VIDEO=`, `RTC=`,
    `RAMKB=`, `FLOPPY1=`, `DISKCNT=`, `DIRTYRAM=`, `FSNOSTAMP=`, `DISKAL=`,
    `BOOTDIAG=`,
    `REDRAWFULL=`, `HEAPCOMPACT=`, `FDDPROBE=`, `SNAPAUDIT=`, `BOOTPROF=`,
    `MOUIDSLOW=`, `TRACKRUN=`, `QUANTUM=`, `SBDRAGOFF=`/`SBRATE=`,
    `DIRW1=`, `PICOMEM=`, `BOOTMARK=`/`BOOTHALT=`/`BOOTSTOP=`, `NOPS2=`,
    `BAND=`, `TITLESNAP=`, `SPLSTARS=`, `NOUNAL=`,
    `NOFLUSHR=`, `FATWGATE=`, `FDDSLOW=`.
    Each one is
    `%ifdef`'d code that no ordinary build compiles, so it rots in silence -
    and every one of them is the A/B half of a gate somewhere in `tests/`.
    A knob that no longer assembles takes its gate with it, and the gate is
    what proves the fix still works.

The C toolchain is the other thing `all` does not build and is NOT in here:
it needs a compiler that is not in the tree, so it is a capability rather than
a knob and it has a row of its own (tests/unit/t_ctoolchain.py). Do not read
this file's title as covering it - that assumption is how it went two releases
without assembling.

This only ASSEMBLES them.  That is deliberate and it is most of the value for
almost none of the time: a `%ifdef` arm that has fallen behind a rename fails
at `nasm`, not at run time.  What a knob DOES is the job of the gate that uses
it.

A ROW PAYS FOR ITS OWN ASSEMBLY AND NOTHING ELSE, and three make variables are
what hold it to that.  Each row used to be a whole cold build in a directory of
its own, of which the assembly under test was under a third: it also rebuilt
four application packages to arrive at a byte-identical associco.inc, ran
os88ovlchk.py over source no knob can change, and re-assembled the finished
kernel a second time for a size report this file captures and throws away.  At
43 rows that was ~230 seconds of a 4-core box per run, none of it about a knob.

  ICODIR=build     take the four packages and associco.inc from the default
                   build.  NOT passed for a row whose knob reaches a PACKAGE
                   and not only the kernel - PKG_VARS below is that list, read
                   out of the Makefile's own $(PKGSBDEF) rather than copied
                   here, so a knob added to it stops sharing without anybody
                   remembering to edit this file
  NOOVLCHK=1       do not run the overlay gate per row.  It takes no argument
                   and expands no %ifdef, so its answer is a function of
                   kernel/ alone and 43 runs are one answer 43 times.  THE GATE
                   STILL RUNS: once, here, as a check of this file's own, and
                   the matrix fails on it exactly as a build would
  NOKERNSIZE=1     skip the size REPORT, which re-assembles the kernel to
                   measure it and whose output never leaves this process

None of the three changes a byte of any kernel this file builds, which is the
property that makes them safe and the one tests/unit/t_bmshare.py asserts.

EVERY BUILD IS OUT OF TREE, in `build/bm-<name>/`, and that is not tidiness.
CLAUDE.md's `cgak` note is the reason: a knob build landing in `build/` puts
a kernel that boots the wrong adapter - or counts disk sectors, or restores a
removed bug - on top of the shipped one, and nothing afterwards says so.
Verified here rather than assumed: `build/kernel.bin`'s md5 is taken before
the matrix and again after, and a change is a failure.
"""
import argparse
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402


def pkg_vars():
    """The make variables that reach a PACKAGE build and not only the kernel.

    DERIVED, from the Makefile's own $(PKGSBDEF) line, because a list of knob
    names kept here is a list that goes stale silently: the failure it would
    cause is a row sharing the default build's notepad.o88 and so no longer
    assembling `apps/notepad` under the knob it exists for - green, and one
    gate poorer.  Reading the definition means a knob added to PKGSBDEF stops
    sharing on the next run.  An empty answer is a FAILURE below, not a
    default: it means the definition has moved and this can no longer tell.
    """
    txt = open(os.path.join(ROOT, "Makefile"), encoding="utf-8").read()
    m = re.search(r'^PKGSBDEF\s*:?=\s*(.*)$', txt, re.M)
    return set(re.findall(r'\$\(if\s+\$\((\w+)\)', m.group(1))) if m else set()


PKG_VARS = pkg_vars()


def makefile_knobs():
    """Every knob the Makefile stamps - its own $(KNOBS) list, read out.

    THE ROSTER IS DERIVED AND THE VALUES ARE THE HAND TABLE. The list below
    used to be the roster as well, and it covered 43 of the Makefile's 78:
    sixteen knobs were assembled by NO tier at all (ANIMOFF, CURFIX, FONT,
    STRAD, ...), fifteen only by a soak row that builds the knob for its own
    A/B (NOUIBLOCK, NOBLITCUT, NOCURDISK, DLJUNK, ...), three only by `make
    stkdiag` - and CLAUDE.md says of four of those that the knob is "the only
    thing keeping ... assembling", while none of them was in test-full. A
    hand roster is a list that goes stale silently; this reads $(KNOBS) so a
    knob added to the Makefile FAILS the matrix below until it has a row,
    rather than joining the unbuilt list.
    """
    txt = open(os.path.join(ROOT, "Makefile"), encoding="utf-8").read()
    m = re.search(r'^KNOBS\s*:=\s*\$\(strip\s+\$\(foreach\s+k,(.*?),\s*\\?'
                  r'\s*\$\(if\b', txt, re.M | re.S)
    if not m:
        return []
    return re.findall(r'[A-Z][A-Z0-9_]*', m.group(1).replace('\\', ' '))


# What every row passes: see the module docstring. Held here rather than spelt
# into build() so that the one place it is decided is the one place it reads.
NOWASTE = ["NOOVLCHK=1", "NOKERNSIZE=1"]

# (name, [make variables]) or (name, [make variables], target). The target is
# that build's own kernel unless the row names another - `small` has a target of
# its own because it is a whole second tree (its own drivers, its own Control
# Panel), and the rows below that say `boot360.bin` are the ones whose %ifdef
# arms are in the BOOT SECTOR. A knob that reaches only a DRIVER or a PACKAGE
# names that file: a `kernel.bin` row for PICOMEM assembled a kernel the knob
# never touches and reported the sound driver's arm alive.
#
# THIS TABLE IS THE VALUE EACH KNOB NEEDS, not the roster: main() reads the
# roster out of the Makefile's $(KNOBS) and fails on any knob with no row here.
#
# A boot-sector row is not the same question as a kernel row and asking the
# kernel one is how four broken knobs shipped: `make FLOPPY1=1 kernel.bin`
# succeeds while `make FLOPPY1=1 boot360.bin` dies with `TIMES value -3 is
# negative`, because 510 bytes is a budget and every one of these spends some of
# it. boot360.bin DEPENDS on kernel.bin, so a boot row still covers the kernel
# and costs nothing extra.
KNOBS = [
    ("video-cga",   ["VIDEO=cga"]),
    ("video-herc",  ["VIDEO=herc", "HERCSEG=0x7000"]),
    ("video-ega",   ["VIDEO=ega"]),
    ("rtc-bios",    ["RTC=bios"]),
    ("rtc-none",    ["RTC=none"]),
    ("rtc-ns",      ["RTC=ns"]),
    ("rtc-at",      ["RTC=at"]),
    ("rtc-rp",      ["RTC=rp"]),
    ("ramkb-128",   ["RAMKB=128"], "boot360.bin"),   # RAM_KB reaches the SECTOR
    ("ramkb-104",   ["RAMKB=104"], "boot360.bin"),   # RAM_KB reaches the SECTOR
    ("floppy1",     ["FLOPPY1=1"], "boot360.bin"),
    ("diskcnt",     ["DISKCNT=1"]),
    ("diskal",      ["DISKAL=1"], "boot360.bin"),
    ("dirtyram",    ["DIRTYRAM=1"]),
    ("fsnostamp",   ["FSNOSTAMP=1"]),
    ("redrawfull",  ["REDRAWFULL=1"]),
    ("heapcompact", ["HEAPCOMPACT=0"]),
    ("fddprobe",    ["FDDPROBE=0"]),
    ("snapaudit",   ["SNAPAUDIT=1"]),
    ("dirw1",       ["DIRW1=1"]),
    # PICOMEM= reaches SOUND.DRV and nothing else (SNDDEF), so the target is
    # the driver: this row built kernel.bin for a year and assembled no arm.
    # PM_BASE/PM_SB_PORT are only read under it, so one row takes all three.
    ("picomem",     ["PICOMEM=1"], "sound.drv"),
    ("picomem-ports", ["PICOMEM=1", "PM_BASE=0x2A0", "PM_SB_PORT=0x220"],
     "sound.drv"),
    ("bootprof",    ["BOOTPROF=1"]),
    ("mouidslow",   ["MOUIDSLOW=1"]),
    ("trackrun",    ["TRACKRUN=1"], "boot360.bin"),
    # SPEC.md 18.93.1/18.93.2's instruments. BOOTMARK= puts a MARK expansion
    # into ~60 places in kmain that expand to NOTHING in every other build, so
    # nothing else assembles them; BOOTHALT= is the arm inside that macro;
    # BOOTSTOP= and NOPS2= are the boot sector's and mouse_init's own %ifdefs.
    # A boot that stops is exactly when these get reached for, which is the
    # worst moment to find out one of them no longer assembles.
    #
    # BOOTSTOP takes BOTH its arms, because they are not the same build: =2
    # defines BOOT_NOSPLASH and so compiles the splash call OUT, which pays for
    # itself, while =1 is pure addition to a sector that is already nearly full.
    # BOOTDIAG= is the diagnostic disk's (`make field`), and it is the largest
    # single spender in the sector.
    ("bootmark",    ["BOOTMARK=1"]),
    ("boothalt",    ["BOOTMARK=1", "BOOTHALT=20"]),
    ("bootstop",    ["BOOTSTOP=2"], "boot360.bin"),
    ("bootstop1",   ["BOOTSTOP=1"], "boot360.bin"),
    ("bootdiag",    ["BOOTDIAG=1"], "boot360.bin"),
    ("nops2",       ["NOPS2=1"]),
    # QUANTUM= is stamp-tracked (SPEC.md 53.2.1's sub-tick) and its
    # %ifdef SCH_QUANTUM arm compiles in no other configuration - the same
    # sentence as every row above. 4 is the deepest setting, so it is the
    # one that keeps the divider arithmetic honest too.
    ("quantum",     ["QUANTUM=4"]),
    # SPEC.md 13.10.5's thumb drag SHIPS, so what needs keeping alive is the
    # configuration nobody builds: the reference kernel WITHOUT it, and the
    # rate constant, which only the second of these reaches.
    ("sbdragoff",   ["SBDRAGOFF=1"]),
    ("sbrate",      ["SBRATE=2"]),
    # The LOOK/measurement knobs, which nothing else builds at all. Each
    # switches a whole path in or out - and BAND is now the only thing that
    # assembles the COMPOSED title bar at all, because SPEC.md 5.9.6 sent it
    # back to a knob and no shipped kernel carries it. This row is therefore
    # the whole of what keeps kernel/band.inc, wm_title_band and wm_tsend
    # assembling; the fifteen-call path it replaces needs no row of its own,
    # being what every other build in this table draws.
    #
    # THE ROW FLIPPED WITH THE DEFAULT and had to: while the composer shipped,
    # `NOBAND=1` was what kept the fifteen calls alive here. A row left naming
    # the retired knob would have gone on passing - `make NOBAND=1` is a make
    # variable nothing reads, so it builds the DEFAULT kernel and reports a
    # pass for a configuration nobody assembled.
    ("band",        ["BAND=1"]),
    ("titlesnap",   ["TITLESNAP=1"]),
    # SPLSTARS= is TITLESNAP's sentence one screen along - the loading screen's
    # animation A/B (SPEC.md 15.3.7) - and it carries a second reason this
    # roster is the only thing watching: it is the ONE configuration whose
    # `.boot2` differs from the shipped one at all. It USED to take an OVL_AT
    # of its own (3,072 against 2,560) AND a BOOT2_SECS of its own, one sector
    # up, because the twinkle wanted ~330 bytes of `.boot2` the shipped split
    # had not got - over WHEREVER OVL_AT fell, so moving the split alone could
    # not pay for it. SPEC.md 15.3.8.5.1 is the size pass that took that arm to
    # 2,568 + 1,421 = 3,993 of 4,096, and BOTH of those constants went with it:
    # one blob length, one split at 2,624, and KSIG_OFF freed from an
    # intersection over two blob lengths (SPEC.md 18.93.1).
    #
    # The margins are NOT quoted here any more and that is the fix rather than
    # an omission: they were "34 on one side and 30 on the other" against an
    # OVL_AT of 2704, and every one of those three numbers was stale - the
    # constant had moved to 3072 and the two halves had moved with every commit
    # that spent a blob byte since. A margin printed in a comment is a number
    # nobody re-measures. What does not rot is the mechanism: BOTH halves are
    # asserted at the foot of kernel.asm, each `%error` names which one ran
    # out, and THIS ROW is the only thing that ever runs those assertions for
    # the knob arm - which matters MORE now, not less: the knob arm's `.boot2`
    # is what sets the floor under OVL_AT for every build in this table.
    ("splstars",    ["SPLSTARS=1"]),
    # NOHEDGE= is the first knob in this table that reaches a DRIVER and not
    # the kernel, so it names a target of its own - SAVER.DRV - and the row
    # costs two files instead of a tree. It is SPEC.md 79.5.10's A/B: the
    # shipped sea reserves eight pixels at the right edge on Hercules so
    # 86Box's plain renderer has nothing to copy onto column 0, and this is the
    # only thing that assembles the unreserved arm. The behaviour half is
    # tests/fishedge.py, which pokes [sv_hlim] at RUNTIME rather than
    # rebuilding - so what is left for a build row is exactly what a build row
    # is for: does the other arm still assemble.
    ("nohedge",     ["NOHEDGE=1"], "saver.drv"),
    # MOUDIAG= is SPEC.md 9.9.6's identify-window table drawn on the finished
    # desktop, and it had NO ROW HERE AT ALL until SPEC.md 2.9.12 - which is
    # how a short jump out of range inside the moved mouse cluster went
    # unfound. It is a knob exactly like the ones above: `%ifdef MOU_DIAG` code
    # in mouse.inc and ui.inc plus the whole of kernel/moudiag.inc, which no
    # ordinary build assembles, and it spends `.boot2` bytes as well as `.text`
    # ones - so it is a blob configuration too, not only a code one.
    #
    # `knobhd` (soak) builds it paired with BOOTPROF=1 and is not the pre-merge
    # gate; this row is, and it costs seconds.
    ("moudiag",     ["MOUDIAG=1"]),
    ("nounal",      ["NOUNAL=1"]),
    # The three this PR added and nothing else names: NOFLUSHR is SPEC.md
    # 11.95.3's A/B for the right border alone, FATWGATE moves 18.8.2's heap
    # gate, FDDSLOW puts the pre-18.92 floppy timing back. None of them has a
    # gate in tests/ the way NOBLITCUT and FATWNONE do, so this roster is the
    # ONLY thing keeping them assembling - which is what this file is for.
    ("noflushr",    ["NOFLUSHR=1"]),
    ("fatwgate",    ["FATWGATE=64"]),
    ("fddslow",     ["FDDSLOW=1"]),
    # NOPLANE= is the same sentence as BAND one polarity over, and the
    # Makefile says so at its definition: it is "the only thing keeping the
    # run-only path assembling",
    # and that path is not dead code either - a FLAT row, a clipped blit and a
    # block hanging off the screen edge all take it. The A/B PERFORMANCE.md
    # Set 107 comes off is the other reason, and neither survives a build
    # failure nobody sees until they reach for it.
    ("noplane",     ["NOPLANE=1"]),
    # GFXAUDIT= is vga12.inc's gfx_aud counters - a whole %ifdef path, four
    # words of bss and a bump in every drawing primitive. Only a SOAK row
    # builds it (tests/gfxlk.py runs `make GFXAUDIT=1` itself), which is not
    # the pre-merge gate, and an instrument that stopped assembling is found
    # at the moment somebody needs it to answer a question.
    ("gfxaudit",    ["GFXAUDIT=1"]),
    # NOSEAMCUT= is SPEC.md 39.14.11's A/B: font_char's whole-cell drop at a
    # display seam, which the cut replaced. Its gate (tests/dispseam.py) builds
    # it - and that gate is on SOAK, which is not the pre-merge one, so this is
    # NOPLANE's sentence exactly: an A/B that stopped assembling is found at
    # the moment somebody reaches for it to tell a real fix from a null run.
    ("noseamcut",   ["NOSEAMCUT=1"]),
    # --- the thirty-five $(KNOBS) no tier assembled (see makefile_knobs) ---
    # Each value is the one its gate or its Makefile block documents; a knob
    # that takes a NUMBER is given the one the A/B uses (DLJUNK=0x61 is the
    # Packard Bell 286, FDDABSENT has an =2 arm of its own, THEMEDARK is 1
    # Dark and 2 Colour), and a knob that reaches a driver or a package names
    # that file for PICOMEM's reason above.
    ("kfz",         ["KFZ=1"]),
    ("instro",      ["INSTRO=1"]),
    ("keeph",       ["KEEPH=0"]),
    ("strad",       ["STRAD=all"]),
    ("heappark",    ["HEAPPARK=0"]),
    ("heapparklk",  ["HEAPPARKLK=0"]),
    ("fddabsent",   ["FDDABSENT=1"]),
    ("fddabsent2",  ["FDDABSENT=2"]),
    ("nosplit",     ["NOSPLIT=1"]),
    ("nosuoccl",    ["NOSUOCCL=1"]),
    ("sndsniff",    ["SNDSNIFF=sb"]),
    ("dragcache",   ["DRAGCACHE=0"]),
    ("fatwnone",    ["FATWNONE=1"]),
    ("scrollrow",   ["SCROLLROW=1"]),
    ("curfix",      ["CURFIX=1"]),
    ("font",        ["FONT=tallx"]),
    ("instchunk",   ["INSTCHUNK=1"], "hdd.drv"),
    ("instchunk-tool", ["INSTCHUNK=1"], "hddtool.drv"),
    ("animoff",     ["ANIMOFF=1"]),
    ("disink0",     ["DISINK0=1"]),
    ("themedark",   ["THEMEDARK=1"]),
    ("themedark2",  ["THEMEDARK=2"]),
    # STKDIAG=1 refuses QUANTUM= (the Makefile's $(error)), so it is a row of
    # its own and never paired; NOMOUPRIV/NOCHAINPRIV are `make stkdiag`'s
    # arms 2 and 3 and were built by nothing in the suite.
    ("stkdiag",     ["STKDIAG=1"]),
    ("nomoupriv",   ["NOMOUPRIV=1"]),
    ("nochainpriv", ["NOCHAINPRIV=1"]),
    ("ethprof",     ["ETHPROF=1"], "ether.drv"),
    ("ftpdslow",    ["FTPDSLOW=1"], "ftpd.o88"),
    ("ftpdbg",      ["FTPDBG=1"], "ftpd.o88"),
    ("nosizesnap",  ["NOSIZESNAP=1"]),
    ("nocolfast",   ["NOCOLFAST=1"]),
    ("noblitcut",   ["NOBLITCUT=1"]),
    ("nouiblock",   ["NOUIBLOCK=1"]),
    ("nocurdisk",   ["NOCURDISK=1"]),
    ("vgadirty",    ["VGADIRTY=1"]),
    ("dljunk",      ["DLJUNK=0x61"], "boot360.bin"),
]


def covered(knob):
    """Does some row build this knob? KERN_SMALL is `make small` below."""
    if knob == "KERN_SMALL":
        return True
    return any(v.split("=")[0] == knob for row in KNOBS for v in row[1])


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def shares(variables):
    """...and whether THIS row may take the default build's packages."""
    return not ({v.split("=")[0] for v in variables} & PKG_VARS)


def build(name, variables, target="kernel.bin"):
    out = os.path.join(ROOT, "build", "bm-" + name)
    cmd = ["make", "BUILD=" + os.path.relpath(out, ROOT)] + NOWASTE + \
          (["ICODIR=build"] if shares(variables) else []) + variables + \
          [os.path.relpath(os.path.join(out, target), ROOT)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=300)
    ok = r.returncode == 0 and os.path.exists(os.path.join(out, target))
    size = os.path.getsize(os.path.join(out, target)) if ok else 0
    err = "" if ok else "\n".join((r.stdout + r.stderr).strip().splitlines()[-6:])
    shutil.rmtree(out, ignore_errors=True)
    return name, ok, size, err


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=min(4, os.cpu_count() or 2))
    a = ap.parse_args()

    shipped = os.path.join(ROOT, "build", "kernel.bin")
    before = md5(shipped) if os.path.exists(shipped) else None

    # The sharing above is only as good as the list it is withheld for, and
    # that list is READ rather than kept - so an empty read is the failure,
    # long before a row can quietly stop assembling a package arm.
    check(bool(PKG_VARS), "the package-knob list still reads out of the Makefile",
          "PKG_VARS is derived from $(PKGSBDEF); an empty answer means that "
          "definition has moved, and every row would then share the default "
          "build's packages - including the ones whose knob reaches one",
          got=repr(sorted(PKG_VARS)), want="a non-empty set of make variables")

    # THE ROSTER, read out of the Makefile. Every knob it stamps must have a
    # row in KNOBS above (or be kern_small, built below), so a knob added to
    # $(KNOBS) fails here on the day it is added rather than rotting unbuilt.
    roster = makefile_knobs()
    check(len(roster) >= 60,
          "the knob roster still reads out of the Makefile's $(KNOBS) (%d)"
          % len(roster),
          "the regex over `KNOBS := $(strip $(foreach k,...` found nothing or "
          "next to nothing; the definition has moved, and every knob would "
          "then be 'covered' by an empty list", got=roster, want=">= 60 knobs")
    unbuilt = [k for k in roster if not covered(k)]
    check(not unbuilt, "every knob in $(KNOBS) has a row in this matrix",
          "a knob with no row is a %ifdef arm nothing assembles - give it a "
          "row in KNOBS with the value its gate uses, and the target it "
          "reaches if that is a driver or a package rather than the kernel",
          got=unbuilt, want="[]")
    stale = sorted({v.split("=")[0] for row in KNOBS for v in row[1]}
                   - set(roster))
    check(not stale, "no row names a variable the Makefile no longer stamps",
          "`make NOBAND=1` is a make variable nothing reads - it builds the "
          "DEFAULT kernel and reports a pass for a configuration nobody "
          "assembled, which is how that row once went green for a retired "
          "knob", got=stale, want="[]")

    # THE OVERLAY GATE, ONCE. Every row is built with NOOVLCHK=1, and this is
    # the other half of that bargain: the gate runs over exactly the source all
    # 43 of them assemble, and a failure here fails the matrix the same way a
    # failure inside one of the builds used to.
    r = subprocess.run(["python3", "tools/os88ovlchk.py"], cwd=ROOT,
                       capture_output=True, text=True, timeout=300)
    check(r.returncode == 0, "os88ovlchk passes over the source every row assembles",
          "the rows are built with NOOVLCHK=1, so this run is the gate for all "
          "of them - it reads tracked source and expands no %ifdef, which is "
          "what makes one run cover every knob",
          got="\n".join((r.stdout + r.stderr).strip().splitlines()[-8:]), want="exit 0")

    # ...and the packages the sharing rows are about to name. `all` has usually
    # built these already and this is then a no-op; asking for them by name is
    # what stops a row dying with "No rule to make target build/paint.o88" on a
    # tree where it has not.
    r = subprocess.run(["make", "-j%d" % a.j, "build/associco.inc"], cwd=ROOT,
                       capture_output=True, text=True, timeout=600)
    check(r.returncode == 0, "the shared packages and associco.inc are present",
          "every row that does not touch a package build takes these from "
          "build/ instead of rebuilding four byte-identical packages of its own",
          got="\n".join((r.stdout + r.stderr).strip().splitlines()[-6:]), want="exit 0")

    sizes = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.j) as ex:
        for name, ok, size, err in ex.map(lambda kv: build(*kv), KNOBS):
            check(ok, "make %s assembles" % name,
                  "a %ifdef arm that no ordinary build compiles has fallen behind "
                  "a rename - and it takes the gate that uses it with it",
                  got=err or "no kernel produced", want="a kernel")
            if ok:
                sizes[name] = size

    # kern_small is a whole second tree, so it gets the real target.
    r = subprocess.run(["make", "small"], cwd=ROOT, capture_output=True,
                       text=True, timeout=600)
    check(r.returncode == 0, "make small (kern_small) builds",
          "SPEC.md 62.9.15: the 128-256KB machine's kernel is a different binary "
          "with its own budget and whole features compiled out. `all` does not "
          "build it, so nothing catches this until a release",
          got="\n".join((r.stdout + r.stderr).strip().splitlines()[-8:]), want="exit 0")

    # `make small` shares build/ with the default build - SMALLDRIVERS are the
    # same `build/*.drv` paths - and it is a target-specific `KMODDIR` away
    # from restamping one of them for the SMALL kernel. Measured: it left
    # `build/hddtool.drv` disagreeing with the copy already on the shipped
    # images, and a later plain `make` did not put it back because the file
    # was newer than its sources. So the matrix restores the tree itself
    # rather than leaving that for the next thing to trip over - a test suite
    # that dirties the build is a test suite people stop running.
    r = subprocess.run(["make", "-j%d" % a.j], cwd=ROOT, capture_output=True,
                       text=True, timeout=600)
    check(r.returncode == 0, "the default build is restored afterwards",
          "the matrix must not leave build/ in a state the next test reads as a "
          "stale image", got="\n".join((r.stdout + r.stderr).strip().splitlines()[-6:]))

    if before:
        check(md5(shipped) == before, "the shipped kernel was not clobbered",
              "a knob build landing in build/ puts a kernel that boots the wrong "
              "adapter on top of the shipped one, and nothing afterwards says so "
              "(CLAUDE.md's cgak note)")

    print("t_buildmatrix: %d knob builds + kern_small (%d shared the default "
          "build's packages, %d built their own: %s)"
          % (len(sizes), sum(1 for k in KNOBS if shares(k[1])),
             sum(1 for k in KNOBS if not shares(k[1])),
             ", ".join(sorted(k[0] for k in KNOBS if not shares(k[1]))) or "none"))
    done("t_buildmatrix")


if __name__ == "__main__":
    main()
