#!/usr/bin/env python3
"""DOS's FCB READ calls answer what IBM DOS 3.30 answers (SPEC.md 96.56.2).

Galaxy Trek (TREKRUN.EXE, compiled BASIC, 1983) reads its DATA.DAT through
an FCB - AH=0Fh open, AH=21h random read, AH=10h close - and 21h was at
.bad, AX=0001. An FCB call answers in AL alone, so the read read as AL=01,
END OF FILE, and the program said "Input past end" and stopped. IBM DOS
answers 03 (a partial record, padded). 96.56.1's layer had 0Fh, 10h and
27h; 96.56.2 put 14h, 21h and 24h on 27h's body.

tests/dostrap/fcbrd.asm reads a 300-byte FCBRD.DAT (byte i = (i*7+3) & 255)
through an FCB with every read call the layer answers. THE TABLE BELOW IS A
MEASUREMENT of IBM DOS 3.30 running that same binary off the same disk
(`--ref YOUR_DOS.img` re-takes it; no DOS is in this tree), so it is written
down rather than derived. SUM covers the whole 128-byte record and the DTA
is filled with FFh first, so a partial record that is not ZERO-padded fails.

VERIFIED TO FAIL: with 21h, 14h and 24h pointed back at .bad in .t21 the
five RND and SEQ rows differ.

    python3 tests/dosfcbrd.py                   this DOS, MartyPC
    python3 tests/dosfcbrd.py --ref DOS330.img  print IBM DOS's answers
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os88build.at("build/os8088-360.img")
MACHINE = "os8088_5150_herc_sb_720_gla"     # (the --ref machine too)

def fail(msg):
    print("dosfcbrd: FAIL: %s" % msg)
    sys.exit(1)


def disk(d):
    com = os.path.join(d, "FCBRD.COM")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-o", com,
                           os.path.join(ROOT, "tests/dostrap/fcbrd.asm")])
    dat = os.path.join(d, "FCBRD.DAT")
    with open(dat, "wb") as f:
        f.write(bytes((i * 7 + 3) & 255 for i in range(300)))
    img = os.path.join(d, "fcbrd.img")
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", img,
                           "--size", "360", com, dat], cwd=ROOT)
    return img


def lines(m):
    rows = []

    def done(mm):
        rows[:] = [r.rstrip() for r in (mm.screen() or [])]
        return any(r.startswith("DONE") for r in rows)
    try:
        os88marty.until(m, done, "FCBRD's DONE", poll=0.25, guest=60.0)
    except os88marty.MartyError:
        fail("the probe never finished; the screen was %r"
             % [r for r in rows if r.strip()])
    return [r for r in rows if r[:3] in ("OPE", "RND", "SEQ", "SET", "BLK",
                                         "CLO")]


def ours(img):
    with os88ui.boot(SYS, apps=img, machine=MACHINE, quick=True) as ui:
        ui.open_drive("B")
        ui.open("FCBRD.COM", expect=None)
        got = lines(ui.m)
        ui.m.type_text("x")
    return got


def ref(img, dos):
    d = tempfile.mkdtemp(prefix="dosfcbrd-ref-")
    boot = os.path.join(d, "dos.img")
    shutil.copyfile(dos, boot)
    try:
        with os88marty.launch(boot, apps=img, machine=MACHINE, boot=30) as m:
            for k in ("@Enter", "@Enter", "B:", "@Enter", "FCBRD", "@Enter"):
                if k.startswith("@"):
                    m.key(k[1:])
                else:
                    m.type_text(k)
                os88marty.pace(m, 1.5)
            return lines(m)
    finally:
        shutil.rmtree(d, ignore_errors=True)


# IBM DOS 3.30, measured with `--ref` (tests/dostrap/fcbrd.asm, 2026-11-02)
WANT = [
    "OPEN AL=00 SIZE=0000012C",
    "RND 0 AL=00 SUM=DE00",
    "RND 2 AL=03 SUM=87E4",             # partial: 44 bytes and 84 ZEROS
    "RND 3 AL=01 SUM=FFC0",             # end of file: the DTA untouched
    "SEQ AL=00 SUM=DE00",
    "SEQ AL=00 SUM=E200",
    "SEQ AL=03 SUM=87E4",
    "SET RR=0003",
    "BLK AL=03 CX=0003 SUM=DE00 RR=0003",   # the partial one counts
    "CLOSE AL=00",
]


def main():
    d = tempfile.mkdtemp(prefix="dosfcbrd-")
    try:
        img = disk(d)
        if "--ref" in sys.argv:
            got = ref(img, sys.argv[sys.argv.index("--ref") + 1])
            print("\n".join(got))
            return 0
        if not os.path.exists(SYS):
            fail("%s is missing - `make`" % SYS)
        got = ours(img)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("dosfcbrd: this DOS answered:")
    for r in got:
        print("   | %s" % r)
    bad = [(w, g) for w, g in zip(WANT, got + [""] * len(WANT)) if w != g]
    if bad:
        fail("%d row(s) differ from IBM DOS 3.30:\n%s" % (
            len(bad), "\n".join("  want %-36s got %s" % b for b in bad)))
    print("dosfcbrd: ok - all %d rows are IBM DOS 3.30's" % len(WANT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
