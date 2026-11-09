#!/usr/bin/env python3
"""The Video Battle's hold READ IN PLACE THROUGH LOADALL, on a 286 - 86Box
(SPEC.md 126.4.2, VBATTLE-MEMORY-PLAN 12).

    make && python3 tests/vbt286.py [--keep] [--noinp]

What neither MartyPC (an 8088) nor QEMU (a 386) can run: the battle's tier-1
borrow, apps/os88la.inc's LOADALL (0F 05), which 86Box's 286 executes.
vm/286-hma - the owner's mr286 board, 16 MHz, 4 MB - boots the 1.44 MB
system disk, and B: holds VBATTLE.O88 and the CGA4 demo with its enemy at
1 HP:

  1. the hold is XMS, one range, the whole bundle, and fills while the title
     goes round;
  2. at the fill's end the check passes ON THIS CPU: `[vb_tier]` CPU_286, so
     la_probe ran LOADALL with INT 6 hooked and the hold's first 16 bytes
     read through a LOADALL'd DS matched the driver's copy - `[vb_hinp]` 1;
  3. the battle plays to victory, its end loop and the title again, every
     ring given its clip from the fill on read in place, the decode
     YIELDING (the step poked to 256 bytes: the demo's records are mostly
     under the shipped 2 KB) - the API cells put back and LOADALL'd again
     at every yield;
  4. the title again - in place, across those yields - is the host's decode
     in B8000, byte for byte;
  5. the fault log charges nothing to the run outside the BIOS's own F000.

`--noinp` pokes the A/B gate ([vb_noinp]): the copying hold, for comparing.
Broken on purpose: see SPEC.md 126.4.2's account.

Needs a built 86Box (python3 tools/os88box.py build) and its ROM set.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88box      # noqa: E402
import os88build    # noqa: E402
import os88geom     # noqa: E402
import os88marty    # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402
from hma286 import ui_for, mr_setup, faults, BoxM     # noqa: E402
from vbtloops import screens_of     # noqa: E402

GS_MENU = 1
CPU_286 = 1
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


KEYS = {"ArrowDown": "down", "ArrowUp": "up", "Enter": "return",
        "Escape": "esc", "Backspace": "backspace", "PageDown": "pagedown",
        "PageUp": "pageup", "Home": "home", "End": "end"}


class BoxKM(BoxM):
    """hma286's adapter, with the keys os88ui's scrolling sends"""

    def key(self, k, **_kw):
        self.b.tap(KEYS.get(k, k))

    def status(self):
        """os88ui's clock: the machine is HELD, so a look at it is a step
        (20 emulated ms), its cycles counted at os88marty's rate"""
        self.b.cmd("step 20")
        return {"cycles": int(self.b.ms() * os88marty.GUEST_HZ / 1000)}


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--noinp", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    pkg = os88build.at("build/vbattle.o88")
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    b = None
    try:
        vbp = os.path.join(tmp, "h.vbp")
        B.demo_project(vbp, bars=2)
        p = P.Project.load(vbp)
        p.set(("enemy", "hp"), 1)
        p.save()
        data = B.build_project(p)
        bd = B.Bundle(data)
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(data)
        disk = os.path.join(tmp, "vbt.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440", pkg, vbt], check=True,
                       capture_output=True)
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        b = os88box.launch("286-hma", name="vbt286", hold=True, fresh=True,
                           a=sysimg, b=disk, temp=not a.keep, turbo=True)
        mr_setup(b)
        b.kernel_up(guest_ms=120000)
        ui = ui_for(b)
        m = BoxKM(b)
        ui.m = m
        ui.path("B:/VBATTLE.O88")
        seg = None
        for w in os88geom.windows(m, b.sym):
            if w.title == "Video Battle":
                seg = u16(m.read(os88geom.winptr(m, w.i, b.sym)
                                 + os88geom.W_SEG, 2))
        if not seg:
            raise SystemExit("vbt286: no Video Battle window")
        base = seg << 4

        def rb(n, off=0):
            return b.memr(base + syms[n] + off, 1)[0]

        def rw(n, off=0):
            return u16(b.memr(base + syms[n] + off, 2))

        def poke(n, data):
            b.cmd("memw 0x%X %s" % (base + syms[n], data.hex()))

        def until(cond, what, secs=120):
            b.until(cond, what, guest_ms=int(secs * 1000), step_ms=100)
        poke("vb_xyb", struct.pack("<H", 256))
        if a.noinp:
            poke("vb_noinp", b"\x01")
        b.tap("return")
        until(lambda: rb("vb_ready") == 1, "the play to start")
        ok(rb("vb_hon") == 1 and rb("vb_htype") == 1 and rb("vb_hrn") == 1
           and rw("vb_hkb4") * 4096 >= len(data),
           "1. the hold is XMS, one range, the whole bundle (%d KB)"
           % (rw("vb_hkb4") * 4))
        until(lambda: rb("vb_tplay") == 1, "the title")
        until(lambda: rb("vb_hfull") == 1, "the hold to fill", 300)
        if a.noinp:
            ok(rb("vb_hinp") == 0, "2. --noinp: the copying hold")
        else:
            ok(rb("vb_tier") == CPU_286 and rb("vb_hinp") == 1 and
               rb("vb_cmode") == 1 and rb("vb_iinp") == 1,
               "2. IN PLACE through LOADALL on this 286 (tier %d): probed "
               "with INT 6 hooked, 16 bytes matched the driver's copy - the "
               "CLIP CACHE (126.4.6), the idle loop in the block"
               % rb("vb_tier"))
        i0, y0 = rw("vb_icalls"), rw("vb_iyc")
        b.tap("return")                 # the fade, then the openings
        until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
        b.tap("return")                 # Attack, at the 1-HP enemy
        until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
        ok(rb("vb_over") == 1, "3. the battle to victory and its end loop")
        b.tap("return")                 # play again: the title
        until(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
              "the play again, at the title", 240)
        title = screens_of(bd, bd.title)
        hits = 0
        for _ in range(16):
            hits += bytes(b.memr(0xB8000, 16384)) in title
            b.cmd("step 150")
        ni = (rw("vb_icalls") - i0) & 0xFFFF
        ny = (rw("vb_iyc") - y0) & 0xFFFF
        if a.noinp:
            ok(ni == 0, "3. --noinp: nothing read in place")
        else:
            ok(ni > 0 and ny > 0 and rw("vb_irings") >= 3,
               "3. records read IN PLACE: %d, the decode yielding %d times "
               "(each a la_restore and a fresh LOADALL), %d rings"
               % (ni, ny, rw("vb_irings")))
        ok(hits >= 8, "4. the title again is the host's decode in B8000 "
           "(%d of 16 looks exact)" % hits)
        f = faults(b.run)
        ok(not f, "5. no exception outside the BIOS: %d%s"
           % (len(f), "".join("\n        " + x for x in f[:4])))
        b.tap("esc")
        until(lambda: rb("vb_ready") == 0, "Esc to leave", 60)
    finally:
        if b is not None and not a.keep:
            b.quit(remove=True)
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbt286: %d FAILED" % len(fails))
        return 1
    print("vbt286: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
