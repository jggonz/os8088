#!/usr/bin/env python3
"""Measure reserve-safe truncation on a conventional-only machine.

    make browsertest && python3 tests/brcapacity.py

The local fixture removes Ethernet-ring pressure from the memory measurement.
This QEMU profile has conventional RAM but no EMS device. A page whose line
index cannot grow must discard the unindexed formatted tail, keep scrolling
within the retained lines, and report truncation. EMS-backed floor coverage is
a separate gate because this machine has no EMS device.
"""
import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
import dispcp
import os88build
import os88qemu
import os88sym
import os88fixture
from ethernet import Qemu, Mouse, settle

S = os88sym.linear
FLOOR = 196 * 1024
SOURCE_SIZE = 200 * 1024
SENTINEL = b"END196KFLOOR"


def fixtures():
    """Host-only: run tools/brmemory.py into a scratch dir and check sizes."""
    import tempfile
    with tempfile.TemporaryDirectory() as out:
        subprocess.run([sys.executable, os.path.join(_ROOT, "tools",
                        "brmemory.py"), "--out", out], check=True,
                       stdout=subprocess.DEVNULL)
        want = {"mem96k.htm": 96 * 1024, "mem196k.htm": SOURCE_SIZE,
                "boundary100k.htm": 100 * 1024,
                "boundary160k.htm": 160 * 1024,
                "boundaryui100k.htm": 100 * 1024,
                "emsgrowth.htm": 768 * 1024}
        for name, size in want.items():
            got = os.path.getsize(os.path.join(out, name))
            if got != size:
                raise SystemExit("brcapacity: %s is %d bytes, want %d"
                                 % (name, got, size))
        with open(os.path.join(out, "mem196k.htm"), "rb") as f:
            if SENTINEL not in f.read():
                raise SystemExit("brcapacity: mem196k.htm lacks %r" % SENTINEL)
        with open(os.path.join(out, "links400.htm"), "rb") as f:
            links = f.read()
        if links.count(b"<a href=") != 400 or links.count(b"<form ") != 8:
            raise SystemExit("brcapacity: links400.htm is not 400 links "
                             "and 8 forms")
        for name in ("linearena.htm", "docwide.htm", "sourcepressure.htm"):
            if os.path.getsize(os.path.join(out, name)) == 0:
                raise SystemExit("brcapacity: %s is empty" % name)
    print("brcapacity --fixtures: sizes and counts ok")


def main():
    if "--fixtures" in sys.argv[1:]:
        return fixtures()
    # NOTHING SHARED (docs/plans/SOAK-PARALLEL.md 17): the package is this
    # row's `wants=`, the page and the disk are made in this process's own
    # directory, and the machine is private (os88qemu.make_test) - where this
    # was `make browsertest`, a page and a disk under build/ and `make test`
    # on build/qmp.sock, which kept the row out of every shared lane.
    os88fixture.need("build/browser.o88")
    pages = os.path.join(os88qemu.RUNDIR, "brmemory")
    subprocess.run([sys.executable, os.path.join(_ROOT, "tools", "brmemory.py"),
                    "--out", pages], check=True, stdout=subprocess.DEVNULL)
    disk = os.path.join(os88qemu.RUNDIR, "brcapacity.img")
    subprocess.run([
        "python3", "tools/os88disk.py", "-o", disk,
        "--size", "1440", os88build.at("build/browser.o88"),
        os.path.join(pages, "mem196k.htm"),
    ], check=True, stdout=subprocess.DEVNULL)
    os88qemu.make_test("TESTAPPS=" + disk)
    m = Qemu()
    try:
        os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=30,
                       what="desktop UI idle")
        mouse = Mouse()
        dispcp.open_drive(m, mouse, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mouse, S, settle, wx, wy, "MEM196K.HTM")
        wins = dispcp.win_list(m, S)
        if len(wins) < 2:
            raise RuntimeError("the 196 KiB local fixture did not open")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        image = os.path.getsize(os88build.at("build/browser.bin"))

        def symbol(name):
            with open(os88build.at("build/browser.map"), encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] == name:
                        return int(fields[0], 16) - image
            raise RuntimeError(name + " missing from build/browser.map")

        def word(offset):
            raw = m.readseg(pseg, image + offset, 2)
            return raw[0] | (raw[1] << 8)

        def byte(offset):
            return m.readseg(pseg, image + offset, 1)[0]

        def dword(low, high):
            return word(low) | (word(high) << 16)

        doclen_lo, doclen_hi = symbol("br_doclen"), symbol("br_doclen_hi")
        srclen_lo, srclen_hi = symbol("br_srclen"), symbol("br_srclen_hi")
        os88qemu.acted(
            m, lambda: word(38) > 0 and dword(doclen_lo, doclen_hi) > 0,
            secs=120, what="196 KiB page parse/layout")
        # ...and then until the LAYOUT HAS FINISHED. The condition above holds
        # from the first line laid out, and QEMU reads guest memory between
        # any two instructions: with heap enough to hold the whole formatted
        # page, the line table is what runs out, and br_layout sets br_trunc
        # and cuts br_doclen only at its own end - so a sample taken at the
        # first line read 204,744 formatted bytes, 404 lines and no cut, which
        # is the layout half way through and not the defect it looked like.
        os88qemu.quiesce(
            m, lambda: (word(38), dword(doclen_lo, doclen_hi),
                        byte(symbol("br_trunc"))),
            what="the 196 KiB page's layout")
        doclen = dword(doclen_lo, doclen_hi)
        srclen = dword(srclen_lo, srclen_hi)
        truncated = byte(symbol("br_trunc"))
        print("brcapacity: source=%d formatted=%d lines=%d truncated=%d"
              % (srclen, doclen, word(38), truncated))
        if srclen != SOURCE_SIZE:
            raise RuntimeError("local read retained %d source bytes, expected %d"
                               % (srclen, SOURCE_SIZE))

        docseg = word(26)
        doc = bytearray()
        offset = 0
        while offset < doclen:
            count = min(0x4000, doclen - offset)
            doc.extend(m.readseg((docseg + (offset >> 4)) & 0xFFFF,
                                 offset & 15, count))
            offset += count
        if not truncated or doclen >= FLOOR or SENTINEL in doc:
            raise RuntimeError("conventional-only page did not safely discard its "
                               "unindexed tail: len=%d trunc=%d sentinel=%s"
                               % (doclen, truncated, SENTINEL in doc))

        lines = word(38)
        if lines == 0:
            raise RuntimeError("no display lines survived conventional reserve")
        line_table = m.readseg(word(symbol("br_lseg")), 0, lines * 10)
        last_end = int.from_bytes(line_table[-6:-2], "little")
        if last_end != doclen:
            raise RuntimeError("formatted tail is not aligned to the final line: "
                               "doclen=%d last_end=%d" % (doclen, last_end))

        bx, by, bw, bh = dispcp.win_rect(m, S, wins[-1])
        mouse.click(bx + min(100, bw // 2),
                    by + dispcp.TITLE_H + min(40, max(20, bh // 4)))
        settle(m)
        m.key("End")
        settle(m)
        top, rows = word(14), word(12)
        if top < max(0, lines - rows):
            raise RuntimeError("End did not reach the last retained display lines")
        print("brcapacity: retained source=%d formatted=%d lines=%d; "
              "unindexed tail discarded and reserve-safe End verified"
              % (srclen, doclen, lines))
    finally:
        m.quit()


if __name__ == "__main__":
    main()
