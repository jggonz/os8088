#!/usr/bin/env python3
"""The arena and the read-ahead cache do not overlap (SPEC.md 96.44.11).

kern_dos sizes the DOS program's block BEFORE it mounts the volume the program
came off, and the mount then CLAIMS: `dsk_rah_want` takes §18.95's read-ahead
out of the same bump allocator, lowering `[kd_top]` by 32 KB. Nothing re-read
it, so the program was handed a block whose top 24 KB the cache was sitting in
- and §96.11's 8 KB file window, carved off that same overstated ceiling, sat
inside the cache outright.

The figure on the glass was RIGHT THE WHOLE TIME, which is why no row saw it:
588 KB is 588 KB whether or not something else is living in the top of it.
So this row does not check a number the program prints. It checks the four
words kern_dos laid out, against each other and against the ceiling they were
supposed to be cut from.

WHAT IT WOULD CATCH, and the first two were TRUE before 96.44.11:

  - the arena sized before the mount and    -> `dos_arena + dos_apara` is 0x9E00
    never re-read                              with `[kd_top]` at 0x9800: 24 KB
                                               of the program's own block is
                                               the cache's, and a program that
                                               uses its top pages corrupts it
                                               silently
  - the cache never given back              -> `[dsk_rah_seg]` is still live at
                                               the handover, so `dsk_rah_have`
                                               serves the PROGRAM'S bytes as
                                               disk sectors for the rest of the
                                               session
  - `kd_arena` leaving the window where     -> `[dos_wseg]` is not the paragraph
    `dos_fh_setup` put it                      the arena ends at, so 8 KB of
                                               hole sits in the middle of the
                                               block
  - the overlap "fixed" by SHRINKING the    -> every assertion above passes and
    arena instead of by shedding               the program has less memory than
                                               it did. Assertion 4 is the one
                                               that refuses that trade

WHAT IT DOES NOT COVER is the ladder under the LOAD - `kd_shed` on a
`dos_load` refusal - which needs a program big enough to want the cache's
memory before it has been handed over. The handover's own run to the bottom
is assertion 3.

It runs on MartyPC and must: the arm tears os8088 out of memory, so every
word read here is read out of a guest with no operating system in it.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402
from os88geom import KD_SEG                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS = "build/os8088-360.img"
COM = "build/doscom360.img"
# **THROUGH THE RUN'S TREE** (tools/os88build.at). A soak reads a FROZEN
# tree and not `build/`, and these two are the only paths here the host
# opens directly rather than handing to os88marty, which resolves them
# itself - so a literal `build/` path reports "not built" about an
# artefact the run has, which reads as `make kdostest` never having been
# typed.  os.path.join keeps it ROOT-anchored when no tree is applied and
# leaves an absolute tree path alone, join discarding everything before
# an absolute component.
KDBIN = os.path.join(ROOT, os88build.at("build/kerndos.bin"))
MACH = "os8088_5150_cga_gla"

RD_N, RD_SEL, RD_PITCH, RD_DIS = 10, 12, 14, 16
WHOLE, NARM = 1, 2
DOS_PSPP = 10                   # apps/dos/dos.asm: the PSP's paragraph in the
                                # block, mirrored here only to say what the KB
                                # figure is a figure OF
A_BW, A_BG, A_BH, A_BTNY, TITLE_H = 72, 12, 13, 46, 18

WANT = ("kd_top", "kd_floor", "dos_arena", "dos_apara", "dos_wbytes",
        "dos_wseg", "dsk_rah_seg", "dsk_rah_runs")


def fail(msg):
    print("kdarena: FAIL: %s" % msg)
    sys.exit(1)


def rows(m):
    return [r.rstrip() for r in (m.screen() or [])]


def wait_text(m, want, secs=150, what=""):
    rs = []

    def seen(mm):
        rs[:] = rows(mm)
        return any(want in r for r in rs)
    try:
        os88marty.until(m, seen, repr(want), poll=0.25, limit=secs)
        return rs
    except os88marty.MartyError:
        pass
    fail("%s: %r never appeared. The last screen was %r"
         % (what or want, want, [r for r in rows(m) if r.strip()][:10]))


def symbols():
    """{name: offset} for the SHIPPED kern_dos, and proved to be its own.

    The Makefile emits no map for `build/kerndos.bin`, so this assembles the
    same root again to get one - and then compares the BINARY, which is the
    whole of what makes the offsets trustworthy. `tools/os88sym.py` does
    exactly this for the kernel and for exactly this reason: a map that
    describes a different build resolves every name to a plausible wrong
    address and nothing says so.
    """
    if not os.path.exists(KDBIN):
        fail("build/kerndos.bin is not built - `make kdostest`")
    with tempfile.TemporaryDirectory() as td:
        binp = os.path.join(td, "k.bin")
        mapp = os.path.join(td, "k.map")
        root = os.path.join(td, "root.asm")
        with open(root, "w") as f:
            f.write("[map all %s]\n%%include \"%s\"\n"
                    % (mapp, os.path.join(ROOT, "kerndos", "kdos.asm")))
        cmd = ["nasm", "-f", "bin", "-w+error", "-DDOS_EXTCORE"]
        for inc in ("kernel", "kerndos", "apps", "apps/dos", "drivers/net"):
            cmd += ["-I", os.path.join(ROOT, inc) + os.sep]
        cmd += ["-o", binp, root]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            fail("kern_dos would not assemble for its map:\n%s" % r.stderr)
        if open(binp, "rb").read() != open(KDBIN, "rb").read():
            fail("the map describes a DIFFERENT kern_dos to the one on the "
                 "disk. Rebuild (`make kdostest`) before trusting this row - "
                 "or, if a knob is in build/, build it into a tree of its own "
                 "(tools/os88build.py)")
        out = {}
        for ln in open(mapp):
            p = ln.split()
            if len(p) == 2 and p[1] in WANT:            # Real  Name
                out[p[1]] = int(p[0], 16)
            elif len(p) == 3 and p[2] in WANT:          # Real  Virtual  Name
                out[p[2]] = int(p[1], 16)
    missing = [w for w in WANT if w not in out]
    if missing:
        fail("nasm's map has no %s" % ", ".join(missing))
    return out


def rec(m, pseg, dm, off):
    return int.from_bytes(m.read((pseg << 4) + dm["dos_mrad"] + off, 2),
                          "little")


def alert_up(m, base, dm):
    return int.from_bytes(m.read(base + dm["os88ui_awin"], 2), "little")


def alert_button(m, base, dm, i, n=2):
    w = alert_up(m, base, dm)
    if not w:
        fail("no alert is up to click")
    r = m.read((KD_SEG << 4) + w, 8)            # the window record is the
    wx = int.from_bytes(r[2:4], "little")       # KERNEL's, and at this point
    wy = int.from_bytes(r[4:6], "little")       # the kernel is still there
    ww = int.from_bytes(r[6:8], "little")
    row = n * (A_BW + A_BG) - A_BG
    left = wx + (ww - row) // 2 + i * (A_BW + A_BG)
    return left + A_BW // 2, wy + TITLE_H + A_BTNY + A_BH // 2


def topmem(rs):
    for r in rs:
        if "Memory to top of block:" in r:
            try:
                return int(r.split(":")[1].strip().split()[0])
            except (IndexError, ValueError):
                fail("could not read a KB figure out of %r" % r)
    fail("the program never printed its top-of-memory figure")


def main():
    syms = symbols()
    print("kdarena: the map is this kern_dos's own (%d bytes)"
          % os.path.getsize(KDBIN))

    with os88ui.boot(SYS, apps=COM, machine=MACH) as ui:
        m = ui.m
        if not ui.path("B:/DOSHELLO.COM"):
            fail("DOSHELLO.COM did not open a DOS window")
        wait_text(m, "READY", secs=120, what="the windowed run")
        m.type_text("x")                        # ...out of the console and
        os88marty.settle(m)                     # back to the main page
        dm = dosmap.package()
        pseg = dosmap.instance(m)
        mo = os88mouse.Mouse(marty=m)

        # --- the Shut down the OS arm ---------------------------------------------------
        mo.click(*dosmap.centre(m, pseg, dm, "dos_erect"))
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_N) != NARM:
            fail("the Memory page has %d arms" % rec(m, pseg, dm, RD_N))
        if rec(m, pseg, dm, RD_DIS) & (1 << WHOLE):
            fail("the Shut down the OS arm is GREYED on a build that carries kern_dos")
        x1, y1, x2, _ = dosmap.rect(m, pseg, dm, "dos_mrad")
        pitch = rec(m, pseg, dm, RD_PITCH)
        mo.click((x1 + x2) // 2, y1 + WHOLE * pitch + pitch // 2)
        os88marty.settle(m)
        if rec(m, pseg, dm, RD_SEL) != WHOLE:
            fail("clicking the Shut down the OS arm left OS88UI_RD_SEL at %d"
                 % rec(m, pseg, dm, RD_SEL))

        mo.click(*dosmap.centre(m, pseg, dm, "dos_trect"))
        os88marty.settle(m)
        base = pseg << 4
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))
        os88marty.settle(m)
        if not alert_up(m, base, dm):
            fail("Run on the Shut down the OS arm went straight to the launch "
                 "(SPEC.md 96.42)")
        mo.click(*alert_button(m, base, dm, 1))         # Proceed
        rs = wait_text(m, "READY", secs=150, what="the run under kern_dos")

        # --- what kern_dos laid out, read out of the machine ------------------
        def w(name):
            b = m.readseg(KD_SEG, syms[name], 2)
            return b[0] | (b[1] << 8)

        top, floor = w("kd_top"), w("kd_floor")
        arena, apara = w("dos_arena"), w("dos_apara")
        wseg, wbytes = w("dos_wseg"), w("dos_wbytes")
        rseg, runs = w("dsk_rah_seg"), w("dsk_rah_runs")
        wpara = (wbytes + 15) >> 4
        kb = topmem(rs)
        print("kdarena: floor 0x%04X  top 0x%04X  arena 0x%04X+0x%04X  "
              "wseg 0x%04X (%d B)  rah 0x%04X/%d  program %d KB"
              % (floor, top, arena, apara, wseg, wbytes, rseg, runs, kb))

        # 1: the window is where the arena ENDS.
        if wseg != arena + apara:
            fail("[dos_wseg] is 0x%04X and the arena ends at 0x%04X: "
                 "SPEC.md 96.11's file window is a HOLE in the middle of the "
                 "program's block, not the paragraph past it. `kd_arena` "
                 "moves the window with the ceiling for exactly this reason"
                 % (wseg, arena + apara))
        print("kdarena: 1/4 the file window is the paragraph the arena ends at")

        # 2: ...and NOTHING is above the ceiling it was cut from.
        if arena + apara + wpara > top:
            over = arena + apara + wpara - top
            fail("the block runs to 0x%04X and [kd_top] is 0x%04X - %d KB "
                 "of what the program was told it owns is above the "
                 "allocator's ceiling (SPEC.md 96.44.11). That is the arena "
                 "sized before the mount claimed and never re-read: the "
                 "read-ahead is sitting in it, and a program that touches its "
                 "top pages corrupts the cache with nothing to report it"
                 % (arena + apara + wpara, top, (over + 63) >> 6))
        print("kdarena: 2/4 the block plus the window ends at or below "
              "[kd_top]")

        # 3: the cache that was KEPT is ABOVE the ceiling, not in the program.
        #
        # **THIS CHECK USED TO SAY "SHED TO NOTHING" AND THAT CONTRACT IS
        # OVER** (SPEC.md 96.44.11.4). `kd_giveback` stopped at zero because
        # `dos_build_psp` hands a program EVERYTHING, so any width kept was a
        # width kept INSIDE the program's block - until 96.44.11.3 closed the
        # allocator and the cache could sit above `[kd_top]` with the window
        # and the program below it. Then the design's own figure got measured
        # and it is worth keeping: `KD_RAH_KEEP` rungs take Test Drive III's
        # load from 75 seconds to 16, which beats the DOS this box imitates.
        #
        # So what is asserted is the SEPARATION rather than the absence, and
        # the two failures below are different bugs: a ladder that overshoots
        # its own stop, and a cache that is kept where the program can reach
        # it. The old check would pass for neither and also fails for the
        # shipped, correct machine, which is how it was found.
        keep = dosmap.kd_const("KD_RAH_KEEP")
        if runs > keep:
            fail("the ladder stopped at %d runs and KD_RAH_KEEP is %d: "
                 "`kd_giveback` gave back less than it owes the program "
                 "(SPEC.md 96.44.11.4)" % (runs, keep))
        if runs and rseg < top:
            fail("[dsk_rah_seg] is 0x%04X with %d runs and [kd_top] is "
                 "0x%04X - the kept cache is BELOW the ceiling, which is "
                 "memory the program was told it owns. 96.44.11.4 keeps a "
                 "width only because 96.44.11.3 put the cache ABOVE "
                 "[kd_top]; below it, a program that touches its top pages "
                 "corrupts the cache with nothing to report it"
                 % (rseg, runs, top))
        print("kdarena: 3/4 the read-ahead stopped at %d run(s) of %d, and "
              "at 0x%04X it is above [kd_top] 0x%04X" % (runs, keep, rseg, top))

        # 4: ...and none of that was bought by giving the program LESS.
        #
        # The block is the whole of [kd_floor, kd_top) less the window, so the
        # only honest way to satisfy 2 is to GIVE THE CACHE BACK. Shrinking the
        # arena satisfies it too, and would leave every check above green with
        # the program 32 KB worse off - which is the opposite of what 96.44.11
        # is for.
        want = ((top - floor - wpara - DOS_PSPP) * 16) // 1024
        if kb < want:
            fail("the program reads %d KB above its PSP and the machine has "
                 "room for %d: the overlap was resolved by SHRINKING the "
                 "arena rather than by shedding the cache into it"
                 % (kb, want))
        print("kdarena: 4/4 the program has %d KB of the %d the ceiling "
              "allows" % (kb, want))

    print("kdarena: ok")


if __name__ == "__main__":
    main()
