#!/usr/bin/env python3
"""doswpart - the windowed DOS layer is a PART, and its claim's whole life
(SPEC.md 96.54.9, docs/plans/DOS-PARTS-PLAN.md section 2's option C').

    make && make build/heapfrag.o88 && python3 tests/doswpart.py

On kern_big the layer of SPEC.md 96.54 is part DOS_PART_DVW of DOS.O88, read
by the host's op_self into a claim of its own at the FIRST Run in Window. So
the row is about the CLAIM - when it exists, when it may move, and that the
layer still runs after it has moved - and about what a refusal says:

  1  a window that has only run a program on the FULL SCREEN holds no part:
     [dvw_pseg] is 0 and no claim of the instance's is the part's size
  2  Run in Window in window A: one claim, the part's KB, PINNED while the
     program runs (the IVT and the worker name it); the program runs (its
     count moves); and once it has ended the claim is MOVABLE, its proc
     dvw_preloc
  3  window B does the same, so its part is claimed ABOVE A's; then A runs a
     program on the full screen, which DROPS A's part (the bracket's arena is
     everything) - [dvw_pseg] 0 and the claim gone
  4  HEAPFRAG opens and its suite compacts the heap (SPEC.md 66.8): B's part
     slides down into the hole A's left - IT MOVED
  5  ...and B's NEXT Run in Window works from where it went: the program
     counts, takes its `q` and says so - which the stubs reach only through
     the segment dvw_preloc patched into them
  6  a REFUSED fetch says so in the window and does not fall back to the full
     screen: A's row is made hostile (len 0, which op_self refuses), Run in
     Window leaves no run, no bracket and no part, and the console reads
     `Run in Window could not load its part of DOS.O88.`

**Break it on purpose** (docs/WRITING-TESTS.md 1): with a `ret` put
after dvw_preloc's store, so it no longer falls into dvw_patch - the segment
banked, the stubs left naming the old one - leg 5's run far-calls a claim the compactor has walked past.
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402
import sheetmove                                               # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
HF = os.path.join(ROOT, "build", "heapfrag.o88")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwfs.asm")
OP_R_LEN = 4


def fail(msg):
    print("doswpart: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    """One DOS instance, by its place among the package windows."""

    def __init__(self, m, slot, dm):
        self.m, self.slot, self.dm = m, slot, dm

    def seg(self):
        return int(dosmap.instance(self.m, self.slot))

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m, self.slot))

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def w(self, name):
        return int.from_bytes(dosmap.cellread(self.m, self.dm, self.base(),
                                              name, 2), "little")

    def rows(self):
        scr = self.m.read(self.base() + self.dm["con_scr"], 4000)
        return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i] < 127
                        else " " for i in range(0, 160, 2)).rstrip()
                for r in range(25)]

    def count(self):
        psp = self.w("dos_ldpsp")
        return int.from_bytes(self.m.read(psp * 16 + 0x103, 2), "little")


def claims(m):
    """{segment: (bytes, owner, MC_RLOC)} out of the kernel's own table"""
    raw = m.read(os88sym.linear("mem_tab"), os88geom.MEM_MAX * os88geom.MC_SIZE)
    out = {}
    for i in range(os88geom.MEM_MAX):
        r = raw[i * os88geom.MC_SIZE:]
        seg = r[0] | r[1] << 8
        if seg:
            out[seg] = ((r[2] | r[3] << 8) * 16, r[4] | r[5] << 8,
                        r[8] | r[9] << 8)
    return out


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def windowed(ui, m, d, win, what):
    ui.raise_window(win)
    dosmap.run_in_window(ui)
    if not until(m, lambda: d.b("dvw_run") == 1, what, 30):
        fail("%s: Run in Window put nothing on the worker ([dos_err] %d, "
             "[dvw_pseg] %04X)" % (what, d.b("dos_err"), d.w("dvw_pseg")))
    c = d.count()
    if not until(m, lambda: d.count() > c + 1, "%s counting" % what, 20):
        fail("%s: the program does not run in its window (count %d)"
             % (what, d.count()))


def quit_run(ui, m, d, win, what):
    ui.raise_window(win)
    m.type_text("q")
    if not until(m, lambda: d.b("dvw_run") == 0, "%s's end" % what, 20):
        fail("%s: `q` did not end the program" % what)
    os88marty.settle(m)


def bracket_once(ui, m, first):
    if not first:
        ui.raise_window(ui.window("Disk"))
    if not ui.path("B:/DWFS.COM" if first else "DWFS.COM"):
        fail("could not launch DWFS.COM")
    os88marty.pace(m, 2)
    m.type_text("q")
    os88marty.pace(m, 3)
    os88marty.settle(m)


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()
        if "dvw_pseg" not in dm:
            fail("this DOS keeps its windowed layer INLINE - no [dvw_pseg] "
                 "(SPEC.md 96.54.9 is the parted host, -DDOSKPART)")
        part_kb = (len(open(os.path.join(ROOT, "build", "doswp.bin"),
                            "rb").read()) + 1023) // 1024

        # --- 1: the full screen only: no part --------------------------------
        bracket_once(ui, m, True)
        wa = [w for w in ui.windows() if w.title == "DOS"][0]
        a = Dos(m, 0, dm)
        if a.w("dvw_pseg"):
            fail("a window that only ran on the full screen holds a part at "
                 "%04X" % a.w("dvw_pseg"))
        mine = [s for s, c in claims(m).items()
                if c[1] == a.seg() and c[0] == part_kb * 1024]
        if mine:
            fail("window A owns a %d KB claim at %04X with no windowed run"
                 % (part_kb, mine[0]))
        print("doswpart: 1 a window that ran on the full screen holds no part")

        # --- 2: the first Run in Window --------------------------------------
        windowed(ui, m, a, wa, "A's first windowed run")
        pa = a.w("dvw_pseg")
        c = claims(m).get(pa)
        if not pa or not c or c[0] != part_kb * 1024 or c[1] != a.seg():
            fail("A's part should be one %d KB claim of its own: [dvw_pseg] "
                 "%04X, claim %r" % (part_kb, pa, c))
        if c[2]:
            fail("A's part is MOVABLE (proc %04X) while its program runs - the "
                 "IVT and the worker name it" % c[2])
        quit_run(ui, m, a, wa, "A's first windowed run")
        c = claims(m).get(a.w("dvw_pseg"))
        if not c or c[2] != dm["dvw_preloc"]:
            fail("between runs A's part should be movable through dvw_preloc "
                 "(%04X); it is %r" % (dm["dvw_preloc"], c))
        print("doswpart: 2 the first Run in Window claims %d KB at %04X, "
              "pinned for the run and movable after it" % (part_kb, pa))

        # --- 3: B above A, and A's full-screen run drops A's -----------------
        bracket_once(ui, m, False)
        wins = sorted([w for w in ui.windows() if w.title == "DOS"],
                      key=lambda w: w.i)
        wa, wb = wins
        a, b = Dos(m, 0, dm), Dos(m, 1, dm)
        windowed(ui, m, b, wb, "B's first windowed run")
        quit_run(ui, m, b, wb, "B's first windowed run")
        pb = b.w("dvw_pseg")
        ui.raise_window(wa)
        dosmap.run_full(ui)          # the mode is A's from leg 2
        os88marty.pace(m, 2)
        m.type_text("q")
        if not until(m, lambda: not a.b("dos_inbr") and a.w("dvw_pseg") == 0,
                     "A's part dropped", 30):
            fail("A ran on the full screen and still holds its part at %04X"
                 % a.w("dvw_pseg"))
        if pa in claims(m) and claims(m)[pa][1] == a.seg():
            fail("A's [dvw_pseg] is 0 but the claim at %04X is still A's" % pa)
        os88marty.settle(m)
        print("doswpart: 3 B's part at %04X; A's full-screen run dropped A's"
              % pb)

        # --- 4: a compaction moves B's --------------------------------------
        ui.raise_window(ui.window("Disk"))
        if not ui.path("HEAPFRAG.O88"):
            fail("could not open HEAPFRAG.O88")
        if not until(m, lambda: sheetmove.heapfrag_ran(m, os88sym.linear),
                     "HEAPFRAG's suite and its compaction", 120):
            fail("HEAPFRAG's suite never finished")
        os88marty.settle(m)
        pb2 = b.w("dvw_pseg")
        if pb2 == pb:
            fail("B's part did not move (still %04X) - the compaction had "
                 "nowhere to put it, so leg 5 would prove nothing. Claims: %r"
                 % (pb, sorted(claims(m).items())))
        c = claims(m).get(pb2)
        if not c or c[1] != b.seg():
            fail("[dvw_pseg] says %04X and the kernel's table has %r there"
                 % (pb2, c))
        print("doswpart: 4 the compaction moved B's part %04X -> %04X"
              % (pb, pb2))

        # --- 5: ...and B runs from where it went ----------------------------
        wb = sorted([w for w in ui.windows() if w.title == "DOS"],
                    key=lambda w: w.i)[1]
        windowed(ui, m, b, wb, "B's run after the move")
        quit_run(ui, m, b, wb, "B's run after the move")
        if not any("exit code 113" in r for r in b.rows()):
            fail("B's moved part ran a program that did not say exit code "
                 "113 ('q'): %r" % b.rows()[-3:])
        print("doswpart: 5 B's moved part ran a program to its `q`")

        # --- 6: a refused fetch says so -------------------------------------
        wa = sorted([w for w in ui.windows() if w.title == "DOS"],
                    key=lambda w: w.i)[0]
        m.write(a.base() + dm["dos_dvwrow"] + OP_R_LEN, b"\0\0")
        ui.raise_window(wa)
        dosmap.run_in_window(ui)
        if not until(m, lambda: any("could not load its part" in r
                                    for r in a.rows()), "the refusal", 30):
            fail("a refused fetch said nothing in the window: %r"
                 % a.rows()[-4:])
        if a.b("dvw_run") or a.w("dvw_pseg") or a.b("dos_inbr"):
            fail("a refused fetch left a run ([dvw_run] %d), a part (%04X) or "
                 "the full screen ([dos_inbr] %d)"
                 % (a.b("dvw_run"), a.w("dvw_pseg"), a.b("dos_inbr")))
        print("doswpart: 6 a refused fetch says so and runs nothing")


def main():
    for f in (SYS, HF):
        if not os.path.exists(f):
            fail("%s is missing - `make && make build/heapfrag.o88`" % f)
    tmp = tempfile.mkdtemp(prefix="doswpart")
    try:
        com = os.path.join(tmp, "DWFS.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwpart360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com, HF], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswpart: ok")


if __name__ == "__main__":
    main()
