#!/usr/bin/env python3
"""dosbigtail - an .EXE is read as far as its LOAD MODULE and no further
(SPEC.md 96.8.2).

DOS loads an MZ file's header + image and leaves everything after it on the
disk for the program to read itself. os8088's DOS used to read the WHOLE
FILE into the arena, so a program whose file is larger than its block was
refused `Not enough RAM` however small its image: Microsoft Works 1.05 is a
319,483-byte WORKS.EXE with a 2,791-byte image, and a DOS window's 128 KB
(SPEC.md 96.54.2) could not start it.

`tests/dostrap/bigtail.asm` is a 3,000-byte module; this row appends 200 KB
of patterned payload to it (written here, never committed), so the file is
~203 KB against a window's 128 KB arena.

  1  double-click BIGTAIL.EXE: a DOS window, the program run once in the
     bracket (the whole arena - this leg passes either way)
  2  Program > Run in Window: 128 KB, smaller than the FILE and far larger
     than the module, and the program must START there
  3  ...and read its own appended payload back through AH=3Dh/42h/3Fh at
     three places, word for word, with its one relocation applied

**Break it on purpose** (docs/WRITING-TESTS.md 1): with `dos_load` reading
the whole file again (the DBE_READ arm alone), leg 2 fails - the read
answers FERR_BIG off the directory entry, `[dos_err]` is DER_FIT and the
worker never runs.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "bigtail.asm")
TAIL_WORDS = 102400                     # bigtail.asm's own constant
WANT_KB = 128                           # a window's default (SPEC.md 96.54.2)


def fail(msg):
    print("dosbigtail: FAIL: %s" % msg)
    sys.exit(1)


def payload():
    return b"".join(struct.pack("<H", (j & 0xFFFF) ^ (0x5A5A if j >= 65536
                                                       else 0))
                    for j in range(TAIL_WORDS))


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.2, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="dosbigtail")
    try:
        exe = os.path.join(tmp, "BIGTAIL.EXE")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", exe, PROBE],
                       check=True)
        with open(exe, "rb") as f:
            mod = f.read()
        hdr = struct.unpack("<3H", mod[:6])
        if len(mod) != (hdr[2] - 1) * 512 + (hdr[1] or 512):
            fail("bigtail.asm's module is %d bytes and its header says "
                 "otherwise" % len(mod))
        with open(exe, "ab") as f:
            f.write(payload())
        size = os.path.getsize(exe)
        if size <= WANT_KB * 1024:
            fail("the file is %d bytes, not larger than the %d KB arena - "
                 "the row would test nothing" % (size, WANT_KB))
        disk = os.path.join(tmp, "bigtail360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", exe], check=True,
                       stdout=subprocess.DEVNULL)
        print("dosbigtail: BIGTAIL.EXE is %d bytes, its load module %d"
              % (size, len(mod)))
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("dosbigtail: ok")


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        dm = dosmap.package()

        def base():
            return dosmap.lin(m, dosmap.instance(m, 0))

        def b(name):
            return m.read(base() + dm[name], 1)[0]

        def w(name):
            return int.from_bytes(m.read(base() + dm[name], 2), "little")

        def rows():
            scr = m.read(base() + dm["con_scr"], 4000)
            return ["".join(chr(scr[r * 160 + i]) if 32 <= scr[r * 160 + i]
                            < 127 else " " for i in range(0, 160, 2)).rstrip()
                    for r in range(25)]

        # --- 1: the bracket, once -------------------------------------------
        if not ui.path("B:/BIGTAIL.EXE"):
            fail("could not launch BIGTAIL.EXE")
        os88marty.pace(m, 3)
        os88marty.settle(m)
        wins = [x for x in ui.windows() if x.title == "DOS"]
        if len(wins) != 1:
            fail("a double-click should leave one DOS window; there are %d"
                 % len(wins))
        print("dosbigtail: 1 the bracket's run is over")

        # --- 2: in the window, on 128 KB ------------------------------------
        def readies():
            return sum("BIGTAIL READY" in r for r in rows())
        n0 = readies()                  # the bracket's own run may have left
        ui.raise_window(wins[0])        # its lines in the console
        dosmap.run_in_window(ui)
        done = until(m, lambda: readies() > n0, "BIGTAIL READY", 60)
        if not done:
            fail("Run in Window never printed BIGTAIL READY: [dos_err] %d, "
                 "[dvw_run] %d, the window's last lines %r. DER_FIT is %d - "
                 "a program whose FILE is bigger than the arena refused "
                 "although its load module is %s (SPEC.md 96.8.2)"
                 % (b("dos_err"), b("dvw_run"),
                    [r for r in rows() if r.strip()][-4:], 7, "3 KB"))
        akb = w("dos_akb")
        if akb != WANT_KB:
            fail("the window's program was given %d KB; the row needs the "
                 "window's default %d, which the file does not fit"
                 % (akb, WANT_KB))
        print("dosbigtail: 2 it started in a %d KB window" % akb)

        # --- 3: what it read --------------------------------------------------
        out = [r.strip() for r in rows() if r.strip()]
        last = max(i for i, r in enumerate(out) if "BIGTAIL START" in r)
        out = out[last:]                # the window's run, and only it
        for r in out:
            print("      | %s" % r)
        want = ["RELOC OK", "TAIL 0 OK", "TAIL 1 OK", "TAIL 2 OK"]
        for line in want:
            if line not in out:
                fail("the program did not print %r: its relocation, or its "
                     "own appended payload read back through AH=3Fh, is "
                     "wrong" % line)
        if any("BAD" in r for r in out):
            fail("the program reported a BAD line")
        print("dosbigtail: 3 relocated, and its payload read back at all "
              "three places")


if __name__ == "__main__":
    main()
