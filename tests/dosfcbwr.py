#!/usr/bin/env python3
"""The rest of DOS's FCB family answers what IBM DOS 3.30 answers (SPEC.md
96.56.3).

96.56.1 and 96.56.2 answered the calls WordPerfect 4.1 and Galaxy Trek made
and left the rest at .bad's AX=0001 - which an FCB call reads as AL=01, a
plausible wrong answer rather than a refusal. tests/dostrap/fcbwr.asm asks
the rest once each: the one-record WRITES 15h and 22h, 28h's block and
current, 17h rename (by name and by pattern), 23h file size, 12h find next,
the EXTENDED FCB's attribute on find, make, open and delete, the PSP's two
FCBs - parsed out of the command tail at the top and copied out of an EXEC's
parameter block - and 1Ah, 2Fh, 0Dh, 2Eh and 54h. THE TABLE BELOW IS A
MEASUREMENT of IBM DOS 3.30 running that binary, typed as `FCBWR FOO.TXT
Z:BAR` at its prompt (`--ref YOUR_DOS.img` re-takes it; no DOS is in this
tree). Here the same line goes into the DOS window's path box, which splits
it the way COMMAND does (SPEC.md 96.32.1.1).

VERIFIED TO FAIL: with 15h, 22h, 17h, 23h and 12h pointed back at .bad in
.t21 the 15, 22, 0F, 27, 23, 17, REN and 11 rows differ, and with the
command-tail parse taken out the PSP row does.

    python3 tests/dosfcbwr.py                   this DOS, MartyPC
    python3 tests/dosfcbwr.py --ref DOS330.img  print IBM DOS's answers
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import dosmap                                                  # noqa: E402
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88mouse                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = os88build.at("build/os8088-360.img")
MACHINE = "os8088_5150_herc_sb_720_gla"     # (the --ref machine too)
ARGS = "FOO.TXT Z:BAR"
TAGS = ("PSP", "KID", "DTA", "15 ", "22 ", "28 ", "0F ", "27 ", "23 ", "17 ",
        "REN", "11 ", "X11", "XFC", "13 ")


def fail(msg):
    print("dosfcbwr: FAIL: %s" % msg)
    sys.exit(1)


def disk(d):
    com = os.path.join(d, "FCBWR.COM")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-o", com,
                           os.path.join(ROOT, "tests/dostrap/fcbwr.asm")])
    img = os.path.join(d, "fcbwr.img")
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", img,
                           "--size", "360", "--folder", "SUB", com], cwd=ROOT)
    return img


def done(m, after):
    rows = []

    def ready(mm):
        rows[:] = [r.rstrip() for r in (mm.screen() or [])]
        return sum(1 for r in rows if r.startswith("DONE")) > after
    try:
        os88marty.until(m, ready, "FCBWR's DONE", poll=0.25, guest=90.0)
    except os88marty.MartyError:
        fail("the probe never finished; the screen was %r"
             % [r for r in rows if r.strip()])
    os88marty.pace(m, 1.0)
    rows[:] = [r.rstrip() for r in (m.screen() or [])]
    last = max(i for i, r in enumerate(rows) if r.startswith("PSP"))
    return [r for r in rows[last:] if r[:3] in [t[:3] for t in TAGS]]


def ours(img):
    with os88ui.boot(SYS, apps=img, machine=MACHINE, quick=True) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.open("FCBWR.COM", expect=None)
        done(m, 0)                  # the double-click: no arguments
        m.type_text("x")
        os88marty.settle(m)
        w = ui.window("DOS")
        if not w:
            fail("the DOS window did not survive the first run")
        ui.raise_window(w)
        mo = os88mouse.Mouse(marty=m)
        mo.click(*dosmap.centre(m, dosmap.instance(m), dosmap.package(),
                                "dos_pln"))
        os88marty.settle(m)
        # **TYPED IN CHUNKS THE GUEST CONFIRMS TAKING**, never as one burst.
        # 166 keys sent at host speed against ~10 guest ms a key in the path
        # box overran the BIOS's 15-entry ring whenever the emulator ran
        # slower than the keys arrived - the box then held `FB.MOXZBAR`, the
        # run said `It could not be read.`, and this row reported the probe
        # as never finishing. Ten at a time fits the ring however the host
        # paces them, and text_taken waits on the ring's own tail.
        for _ in range(14):
            os88marty.text_taken(m, "\b" * 10)
        line = "B:\\FCBWR.COM " + ARGS
        for i in range(0, len(line), 10):
            os88marty.text_taken(m, line[i:i + 10])
        got = bytes(dosmap.cellread(m, dosmap.package(),
                                    dosmap.lin(m, dosmap.instance(m)),
                                    "dos_path", 64)).split(b"\0")[0]
        if got.decode("latin-1") != line:
            fail("the path box holds %r and %r was typed" % (got, line))
        m.key("Enter")
        got = done(m, 1)
        m.type_text("x")
    return got


def ref(img, dos):
    d = tempfile.mkdtemp(prefix="dosfcbwr-ref-")
    boot = os.path.join(d, "dos.img")
    shutil.copyfile(dos, boot)
    try:
        with os88marty.launch(boot, apps=img, machine=MACHINE, boot=30) as m:
            for k in ("@Enter", "@Enter", "B:", "@Enter",
                      "FCBWR " + ARGS, "@Enter"):
                if k.startswith("@"):
                    m.key(k[1:])
                else:
                    m.type_text(k)
                os88marty.pace(m, 1.5)
            return done(m, 0)
    finally:
        shutil.rmtree(d, ignore_errors=True)


# IBM DOS 3.30, measured with `--ref` (tests/dostrap/fcbwr.asm, 2026-11-02)
WANT = [
    "PSP FF00 00 FOO     TXT 00000000 1A BAR",     # Z: is past the last: AH=FF
    "KID 0000 02 KIDNAME TXT 00000000 00 OTHER   DAT",  # 12 bytes, not 16
    "DTA 1A5A 2F5A 0729 1 0D 2E01 01 00",          # AX back from 1Ah and 2Fh
    "15 00 00 00 00 0000 03 00001234 0000001E",    # the size follows the writes
    "22 00 0000 05 00000005 0000003C",
    "28 00 0002 0000 08 00000008 00000050",        # current moves with 28h
    "0F 00 00 0080 00000050 1 0000 00",
    "27 01 0003 0000 08 00000008 2D29",
    "23 00 5A000008 00 5A00000C 00 5A000001 00 5A000001 FF",  # 3 bytes; 0=128
    "17 00 FF 00 0001",
    "REN 00 FF FF",
    "11 00 02 FCBV1   TMP 00 02 FCBV2   TMP FF 00 20 00000050",
    "X11 00 FF00000000000002 FCBV1   TMP 00 FF",
    "XFC 00 00 00 00 FF 00 0080 0050 00",
    "13 00 00",
]


def main():
    d = tempfile.mkdtemp(prefix="dosfcbwr-")
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
    print("dosfcbwr: this DOS answered:")
    for r in got:
        print("   | %s" % r)
    bad = [(w, g) for w, g in zip(WANT, got + [""] * len(WANT)) if w != g]
    if bad or len(got) != len(WANT):
        fail("%d row(s) differ from IBM DOS 3.30:\n%s" % (
            len(bad), "\n".join("  want %-50s\n  got  %s" % b for b in bad)))
    print("dosfcbwr: ok - all %d rows are IBM DOS 3.30's" % len(WANT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
