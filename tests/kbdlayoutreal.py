#!/usr/bin/env python3
"""The active CP850 provider lends ASCII safely inside actual FSXF_REAL.

Bochs/OS88_MEGA=1 only. The package copies legacy 'A' before, inside and
after the bracket, recording bounds, stride and real CPU state. The old
FONT_GLYPHS path read protected KLFP selectors as paragraphs inside REAL;
this must either expose unequal glyphs or fail to finish, never pass.

Prerequisites supplied by wants=: kbdlayoutreal.img and kbdreal.bin, plus
the normal mega image/kernel-symbol artifacts. No fixture build happens
here. This specifically tests the FSX borrow, not hibernation's path.
"""
import argparse
import os
from pathlib import Path
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                                # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from kbdsettings import require                                 # noqa: E402


def u16(data, off=0):
    return int.from_bytes(data[off:off + 2], "little")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/mega.img")
    ap.add_argument("--fixture", default="build/kbdlayoutreal.img")
    ap.add_argument("--binary", default="build/kbdreal.bin")
    a = ap.parse_args(argv)
    require(os.environ.get("OS88_MEGA") == "1",
            "kbdlayoutreal requires OS88_MEGA=1 and the protected-mode kernel")
    require(os88marty.emulator() == "bochs", "kbdlayoutreal requires Bochs")
    image = Path(os88build.at(a.image)).resolve()
    fixture = Path(os88build.at(a.fixture)).resolve()
    binary = Path(os88build.at(a.binary)).resolve()
    require(fixture.is_file() and binary.is_file(),
            "build the declared KBDREAL fixture before running this row")
    with os88ui.boot(str(image), apps=str(fixture), machine="os8088_xt_vga",
                     quick=True, label="kbdlayoutreal") as ui:
        m = ui.m
        require(getattr(m, "bochs", False), "this run did not use Bochs")
        win = ui.path("B:/KBDREAL.O88")
        os88marty.ui_done(m, "KBDREAL fixture launch to finish")
        pkg = os88marty.Pkg(m, win, image=binary.stat().st_size, sym=ui.sym)
        require(m.lin(pkg.seg, 0) >= 0x100000,
                "OS88_HIGH fixture did not get a high region, so no REAL copy is tested")
        require(pkg.byte(56) == 0 and pkg.byte(3) == 0,
                "KBDREAL was already run before the triggering key")
        ui.press("KeyR", need=True)
        ui._wait(lambda: pkg.byte(3) == 1,
                 "the real-mode glyph borrow to return", os88ui.T_WINDOW,
                 snapshot=lambda: "started=%d ran=%d" % (pkg.byte(56), pkg.byte(11)))
        record = bytes(pkg.read(0, 58))
        require(u16(record) == 0x0102 and u16(record, 6) == 0,
                "Spanish optional provider did not activate before borrowing")
        require(record[2] == 0 and u16(record, 4) == 0,
                "FSXF_REAL refused or fell back: CF=%d SI=%d" %
                (record[2], u16(record, 4)))
        require(record[11] == 1 and not u16(record, 12) & 1,
                "callback did not execute with actual CR0.PE clear")
        require(0 < u16(record, 14) < 0xa000,
                "callback CS was not a conventional real-mode paragraph")
        require(record[8] & 0x10 and record[9] & 0x10 and not record[10] & 0x10,
                "CPU_INFO did not show PM before/after and REAL inside")
        copies = []
        for name, off in (("before", 18), ("inside REAL", 30), ("after", 42)):
            require((record[off], record[off + 1], u16(record, off + 2)) == (32, 126, 8),
                    "%s FONT_GLYPHS bounds/stride are wrong" % name)
            glyph = record[off + 4:off + 12]
            require(any(glyph) and len(set(glyph)) > 1,
                    "%s ASCII A glyph is blank or a solid tile" % name)
            copies.append(glyph)
        require(copies[0] == copies[1] == copies[2],
                "legacy ASCII glyph changed across real-mode borrowing: %s" %
                [g.hex() for g in copies])
        require(m.read(ui.sym("font_last"), 1) == b"\xff",
                "native extended glyph provider was not restored after FSXF_REAL")
        ui.press("Semicolon", need=True)
        ui._wait(lambda: pkg.byte(16) == 0xa4,
                 "Spanish enie after returning from REAL", os88ui.T_NAV,
                 snapshot=lambda: "last key=%02x" % pkg.byte(16))
        require(m.read(ui.sym("font_last"), 1) == b"\xff",
                "post-bracket native input lost the extended glyph provider")
        print("PASS protected/REAL/protected ASCII borrow, restored CP850 and Spanish input")
        ui.close(win)


if __name__ == "__main__":
    main()
