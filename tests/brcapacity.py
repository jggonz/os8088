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
from ethernet import Qemu, Mouse, settle, SOCK

S = os88sym.linear
FLOOR = 196 * 1024
SOURCE_SIZE = 200 * 1024
SENTINEL = b"END196KFLOOR"


def main():
    os88qemu.own()
    os88qemu.kill()
    subprocess.run(["make", "browsertest"], check=True)
    subprocess.run([
        "python3", "tools/os88disk.py", "-o", "build/brcapacity.img",
        "--size", "1440", "build/browser.o88",
        "build/brmemory/mem196k.htm",
    ], check=True)
    subprocess.run(["make", "test", "TESTAPPS=build/brcapacity.img"],
                   check=True)
    m = Qemu(SOCK)
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
            with open("build/browser.map", encoding="ascii") as f:
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
