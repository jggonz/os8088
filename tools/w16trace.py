#!/usr/bin/env python3
"""w16trace - read WIN16's call ring out of a running QEMU (SPEC.md 124.17).

    nasm ... -DW16_TRACE ...   (W16_DEFINES=W16_TRACE for the tests' symbols)
    python3 tools/w16trace.py build/qmp.sock CLOCK.EXE

A `-DW16_TRACE` build of WIN16 records the last 128 things a program asked
for in a ring in its bss: every export through the gate as its w16api.inc
row, and every int 21h as 80h:AH. It answers the question a program that
"just stops" leaves - what did it ask for last, and what does it keep
asking for? - which is how CLOCK was found spinning on int 21h 2Ch. The
instance is found by the program's name in w16_name.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sock, name = sys.argv[1], sys.argv[2].upper()
    os.environ["W16_DEFINES"] = (os.environ.get("W16_DEFINES", "")
                                 + " W16_TRACE").strip()
    import xmcheck
    from win16fpu import pkg_syms
    S = pkg_syms()
    rows = []
    for line in open("apps/win16/w16api.inc"):
        if line.startswith("W16EXPORT"):
            rows.append(line.split(";")[1].strip() if ";" in line else line.strip())
    mem = bytes(xmcheck.read_bytes(sock, 0, 0xA0000))
    want = name.encode() + b"\0"
    found = False
    for seg in range(0x1000, 0xA000):
        a = (seg << 4) + S["w16_name"]
        if mem[a:a + len(want)] != want:
            continue
        found = True
        n = struct.unpack_from("<H", mem, (seg << 4) + S["w16_trn"])[0]
        print("WIN16 at %04X: %d calls" % (seg, n))
        tr = struct.unpack_from("<128H", mem, (seg << 4) + S["w16_trc"])
        for i in range(max(0, n - 128), n):
            r = tr[i & 127]
            if r & 0xFF00 == 0x8000:
                what = "int 21h AH=%02Xh" % (r & 0xFF)
            else:
                what = rows[r] if r < len(rows) else "?"
            print("%6d  %s" % (i, what))
    if not found:
        sys.exit("w16trace: no WIN16 instance running %s" % name)


if __name__ == "__main__":
    main()
