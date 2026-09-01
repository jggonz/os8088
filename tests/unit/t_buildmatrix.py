#!/usr/bin/env python3
"""Every build configuration `make all` does not build.

    python3 tests/unit/t_buildmatrix.py [-j N]

`all` builds ONE kernel.  The tree has two dozen more (KNOBS below is the
roster, and the count in this sentence went stale twice before it stopped
naming one), and nothing builds them until somebody types the knob by hand:

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
at `nasm`, not at run time, and the whole matrix costs seconds.  What a knob
DOES is the job of the gate that uses it.

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
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

# (name, [make variables]) or (name, [make variables], target). The target is
# that build's own kernel unless the row names another - `small` has a target of
# its own because it is a whole second tree (its own drivers, its own Control
# Panel), and the rows below that say `boot360.bin` are the ones whose %ifdef
# arms are in the BOOT SECTOR.
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
    ("ramkb-128",   ["RAMKB=128"]),
    ("ramkb-104",   ["RAMKB=104"]),
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
    ("picomem",     ["PICOMEM=1"]),
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
    # roster is the only thing watching: it is the ONE configuration that
    # re-splits the blob, moving OVL_AT to 2704 so the twinkle fits `.boot2`.
    # That leaves 34 bytes on one side of the split and 30 on the other, so the
    # next byte spent in EITHER section breaks this arm and nothing else - and
    # it breaks it at `nasm`, naming which half ran out, which is exactly the
    # failure a build matrix is for.
    ("splstars",    ["SPLSTARS=1"]),
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
]


def md5(p):
    with open(p, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def build(name, variables, target="kernel.bin"):
    out = os.path.join(ROOT, "build", "bm-" + name)
    cmd = ["make", "BUILD=" + os.path.relpath(out, ROOT)] + variables + \
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

    print("t_buildmatrix: %d knob builds + kern_small" % len(sizes))
    done("t_buildmatrix")


if __name__ == "__main__":
    main()
