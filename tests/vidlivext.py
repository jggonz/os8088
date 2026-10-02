#!/usr/bin/env python3
"""LIVE IN COLOUR ON AN EXTENDED DESKTOP leaves the display nest where it
found it (SPEC.md 5.4.3.6, 98.3.10.4).

    make && python3 tests/vidlivext.py

gfx_blitp's region walk is a call that takes NO display hook of its own: it
probes the whole block and then draws each piece, and on an extended desktop
the probe and every piece take the whole-shape hook and bring it down again,
leaving [gfx_bp_hk] = 1 behind them. The walking call's own exit used to read
that byte and bring down a nest nobody had put up - `dec byte [gfx_dnest]`
from 0 - so the first Live pass left [gfx_dnest] at 255 for the rest of the
session: every primitive after it believed it was nested, and gfx_blitp
refused every block for being inside an outer hook.

A Live pass is ALWAYS a walk - vp_lblit arms the window's own clip, one
fragment when nothing covers it - so this needs no covering window to fire;
one is put over the box anyway, for the many-fragment arm. On
os8088_xt_vga_mda with the desktop extended, the Video Player plays the VGA4
rendition on the VGA; [gfx_dnest] is sampled forty times uncovered and forty
under a Disk window, and any value but 0 or 1 (1 being a primitive caught
mid-hook) FAILS - as does a play that stops advancing.

Broken on purpose - gfx_blitp's walk exit jumping to .out again rather than
past the teardown - every sample reads 255 and it FAILS; the frames played
fall by four fifths with it.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88sym, os88vid as vid  # noqa: E402
import os88geom as geom                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
import vidlive as VL                                          # noqa: E402
import dispcp                                                 # noqa: E402

S = os88sym.linear


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = os.path.join(tmp, "LIVE.V88")
        vid.write_resident(v88, [VL.writer(0), VL.writer(1), VL.writer4()],
                           title="live", repeat=True,
                           live=[vid.TARGETS[t] for t in VL.TARGETS])
        disk = os.path.join(tmp, "live.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine="os8088_xt_vga_mda") as ui:
            m = ui.m
            st = os88marty.settle
            dispcp.open_panel(m, ui.mo, S, st)
            dispcp.set_primary(m, ui.mo, S, st, 0)
            dispcp.set_mode(m, ui.mo, S, st, "right")
            dispcp.close_panel(m, ui.mo, S, st)
            nd = m.read(S("vid_ndisp"), 1)[0]
            print("   displays: %d" % nd)
            if nd != 2:
                bad.append("the desktop did not extend (%d displays)" % nd)
                return report(bad)
            w = ui.path("B:/LIVE.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = VL.u16(rec, geom.W_SEG) << 4
            rw = lambda n: VL.u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ui.mo.to(8, 470)
            m.write(base + syms["vp_stopat"], struct.pack("<H", 0xFFFF))
            m.write(base + syms["vp_played"], b"\0")
            m.write(base + syms["vp_rep"], b"\1")
            m.type_text("p")
            os88marty.until(m, lambda mm: rb("vp_lsess") == 1, "a live "
                            "session", poll=0.3, limit=600.0, guest=30.0)
            got = (rb("vp_flive"), rb("vp_rend"))
            print("   live %d, rendition %d (want 1, 2: the VGA's)" % got)
            if got != (1, 2):
                bad.append("the player took %s" % (got,))
            os88marty.pace(m, 1.0)

            def sample(what):
                seen, f0 = {}, rw("vp_vseq")
                for _ in range(40):
                    os88marty.pace(m, 0.05)
                    d = m.read(S("gfx_dnest"), 1)[0]
                    seen[d] = seen.get(d, 0) + 1
                f1 = rw("vp_vseq")
                print("   %s: [gfx_dnest] read %s, %d frames played"
                      % (what, sorted(seen.items()), f1 - f0))
                if any(d > 1 for d in seen):
                    bad.append("%s: [gfx_dnest] read %s" % (what,
                                                           sorted(seen)))
                if f1 - f0 < 10:
                    bad.append("%s: %d frames played" % (what, f1 - f0))
            sample("uncovered")
            px, py = rw("vp_px"), rw("vp_py")
            dk = ui.open_drive("A")
            ui.move_window(dk, px + VL.WB * 4, py + VL.H // 3)
            ui.mo.to(8, 470)
            sample("under a Disk window")
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
