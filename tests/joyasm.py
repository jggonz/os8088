"""JOYSTICK.DRV's own assembly, for the rows that need its symbols
(tests/joystick.py reads its state in the guest, tests/unit/t_joycal.py runs
its routines under unicorn). `assemble()` builds drivers/joy/joy.asm into a
scratch directory - never build/ - and returns (image, {symbol: offset}),
the offsets taken from NASM's own map, so nothing here is remembered."""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NASM = os.environ.get("NASM", "nasm")


def equ(path, name):
    """A numeric `name equ N` out of a source file under the repository."""
    txt = open(os.path.join(ROOT, path)).read()
    m = re.search(r"^%s\s+equ\s+(\w+)" % name, txt, re.M)
    if not m:
        sys.exit("joyasm: %s is not an equ in %s" % (name, path))
    return int(m.group(1), 0)


def assemble():
    d = tempfile.mkdtemp(prefix="joyasm")
    out, mp = os.path.join(d, "joy.bin"), os.path.join(d, "joy.map")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "drivers/",
                        "-I", "drivers/joy/", "-I", "apps/", "-o", out,
                        "--before", "[map all %s]" % mp,
                        os.path.join(ROOT, "drivers/joy/joy.asm")],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.exit("joyasm: joy.asm does not assemble:\n" + r.stderr)
    sym = {}
    for line in open(mp, encoding="ascii"):
        m = re.match(r"^\s*([0-9A-F]+)\s+(?:[0-9A-F]+\s+)?(\S+)\s*$", line)
        if m:
            sym.setdefault(m.group(2), int(m.group(1), 16))
    return open(out, "rb").read(), sym


def sign_break(image, sym):
    """THE RED ARM (docs/WRITING-TESTS.md 1): a copy of the image whose
    joy_calpos no longer negates the left/up half - `neg al` (F6 D8) after
    `.scale` on the low side becomes two nops. Returns (patched image, the
    offset patched). The patch is ANCHORED on the bytes it replaces and says
    so when the driver has moved under it, rather than quietly doing
    nothing."""
    lo, hi = sym["joy_calpos"], sym["joy_dir"]
    at = bytes(image[lo:hi]).find(b"\xF6\xD8")
    if at < 0:
        sys.exit("joyasm: no `neg al` in joy_calpos - the red arm's anchor "
                 "moved; re-aim it")
    out = bytearray(image)
    out[lo + at:lo + at + 2] = b"\x90\x90"
    return bytes(out), lo + at
