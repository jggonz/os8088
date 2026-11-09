"""The debug disks (SPEC.md 127), read back off what `make` built.

    python3 tests/unit/t_debugdisk.py

Two claims, each with nothing else checking it:

1. THE SHIPPED SOUND.DRV IS UNTOUCHED BY THE DEBUG HOOKS (127.1 rule 1).
   sound.asm includes drivers/sound/snddiag.inc and calls its SDG_* macros at
   the probe and stream sites; without -DSNDDIAG every one must expand to
   nothing. So the driver is assembled again from copies of sound.asm and
   sb.inc with every hook line, every `%ifdef SNDDIAG` block and the include
   deleted, and the result must be build/sound.bin byte for byte. A hook
   that emits a byte in the shipped arm is red here.

2. EACH DEBUG IMAGE CARRIES THE DIAG DRIVER AND BOOTS INTO ITS PROGRAM
   (127.2): SOUND.DRV is build/snddiag/sound.drv and not the shipped one (the
   Makefile's swap is a filter-out, and one matching nothing is silent),
   SNDDIAG.O88 and SNDDEBUG.TXT are at the root, \\AUTOSTRT holds the link,
   and SYSTEM.CFG asks for SOUND.DRV. And the shipped system disks still
   carry the SHIPPED SOUND.DRV - the swap must not leak.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
from os88fat import Fat12          # noqa: E402
from harness import check, done    # noqa: E402

BUILD = os.path.join(ROOT, "build")
DEBUG = ["debug-sound.img", "debug-sound-120.img", "debug-sound-720.img",
         "debug-sound-360.img"]
SHIPPED = ["os8088.img", "os8088-120.img", "os8088-720.img", "os8088-360.img"]


def entries(fat, first):
    if first is None:
        for _i, _o, e in fat.entries():
            yield e
        return
    csz = fat.spc * fat.bps
    for c in fat.chain(first):
        o = fat.cluster_off(c)
        for k in range(0, csz, 32):
            e = bytes(fat.img[o + k:o + k + 32])
            if e[0] == 0:
                return
            if e[0] != 0xE5:
                yield e


def find(fat, path):
    """The bytes of A/B/C.EXT, or None."""
    first = None
    parts = path.split("/")
    for i, name in enumerate(parts):
        hit = None
        for e in entries(fat, first):
            if Fat12.pretty(e[:11]) == name:
                hit = e
                break
        if hit is None:
            return None
        if i < len(parts) - 1:
            first = struct.unpack_from("<H", hit, 26)[0]
            continue
        size = struct.unpack_from("<I", hit, 28)[0]
        c0 = struct.unpack_from("<H", hit, 26)[0]
        if not c0:
            return b""
        csz = fat.spc * fat.bps
        return b"".join(bytes(fat.img[fat.cluster_off(c):fat.cluster_off(c) + csz])
                        for c in fat.chain(c0))[:size]
    return None


def snddef():
    """The shipped sound.bin's defines, out of the Makefile's own SNDDEF
    lines for a plain build (no knob is set when `make` runs this row)."""
    out = []
    if not os.environ.get("NOPICOMEM"):
        out.append("-DPICOMEM")
    return out


def stripped(td):
    """sound.asm and sb.inc as if the debug disk had never been written: every
    SDG_* hook line, every `%ifdef SNDDIAG` block and the include gone. The
    copies go in `td`, which is searched ahead of drivers/sound/."""
    d = os.path.join(ROOT, "drivers", "sound")
    for name in ("sound.asm", "sb.inc"):
        src = open(os.path.join(d, name)).read()
        src = re.sub(r"(?ms)^%ifdef SNDDIAG\n.*?^%endif[^\n]*\n", "", src)
        src = re.sub(r"(?m)^\s*SDG_[A-Z]+\b.*\n", "", src)
        src = re.sub(r'(?m)^%include "snddiag.inc".*\n', "", src)
        code = re.sub(r";.*", "", src)     # (comments may name them)
        if "SDG_" in code or "SNDDIAG" in code:
            return "%s still names the hooks after stripping" % name
        with open(os.path.join(td, name), "w") as f:
            f.write(src)
    return None


def main():
    sound_bin = os.path.join(BUILD, "sound.bin")
    if not os.path.exists(sound_bin):
        print("t_debugdisk: no build/sound.bin - run make first")
        sys.exit(1)

    # --- 1: the hooks taken out assemble to the shipped driver -----------
    with tempfile.TemporaryDirectory() as td:
        err = stripped(td)
        check(err is None, "the hooks strip cleanly", err)
        out = os.path.join(td, "s.bin")
        r = subprocess.run(["nasm", "-f", "bin", "-w+error"] + snddef() +
                           ["-I", td + "/", "-I", "drivers/sound/",
                            "-I", "drivers/", "-I", "apps/", "-o", out,
                            os.path.join(td, "sound.asm")],
                           cwd=ROOT, capture_output=True, text=True)
        check(r.returncode == 0, "sound.asm assembles with every debug hook "
              "taken out", r.stderr.strip()[:300])
        if r.returncode == 0:
            a, b = open(out, "rb").read(), open(sound_bin, "rb").read()
            check(a == b, "the shipped SOUND.DRV is byte-identical to one "
                  "with the debug hooks never written (SPEC.md 127.1 rule 1)",
                  "a hook emits bytes without -DSNDDIAG, or build/sound.bin "
                  "is a knob build", got=len(b), want=len(a))

    # --- 2: the images ----------------------------------------------------
    diag = os.path.join(BUILD, "snddiag", "sound.drv")
    ship = os.path.join(BUILD, "sound.drv")
    pkg = os.path.join(BUILD, "snddiag.o88")
    have = [n for n in DEBUG if os.path.exists(os.path.join(BUILD, n))]
    check(len(have) == len(DEBUG), "all four debug-sound images are built",
          "they are in $(SHIPIMGS), so `make` builds them",
          got=have, want=DEBUG)
    if not (os.path.exists(diag) and os.path.exists(pkg)):
        check(False, "build/snddiag/sound.drv and build/snddiag.o88 exist")
        done("t_debugdisk")
    dbytes, sbytes = open(diag, "rb").read(), open(ship, "rb").read()
    pbytes = open(pkg, "rb").read()
    check(dbytes != sbytes, "the debug SOUND.DRV differs from the shipped one",
          "-DSNDDIAG must reach the debug build")
    for n in have:
        fat = Fat12(os.path.join(BUILD, n))
        check(find(fat, "SOUND.DRV") == dbytes,
              "%s: SOUND.DRV is the -DSNDDIAG build" % n,
              "the Makefile's swap filtered nothing out, or the shipped "
              "driver rode along")
        check(find(fat, "SNDDIAG.O88") == pbytes,
              "%s: SNDDIAG.O88 at the root" % n)
        check(find(fat, "SNDDEBUG.TXT") is not None,
              "%s: SNDDEBUG.TXT at the root" % n)
        lnk = find(fat, "AUTOSTRT/SNDDIAG.LNK")
        check(lnk is not None and b"SNDDIAG.O88" in lnk,
              "%s: \\AUTOSTRT\\SNDDIAG.LNK opens SNDDIAG.O88" % n)
        cfg = find(fat, "SYSTEM.CFG")
        dw = None
        if cfg and cfg[:6] == b"O88CFG":
            m = re.search(rb"DW\x01\x02(..)", cfg, re.S)
            dw = struct.unpack("<H", m.group(1))[0] if m else None
        check(dw is not None and dw & 1, "%s: SYSTEM.CFG asks for SOUND.DRV "
              "(DW bit 0)" % n, got=dw)
    for n in SHIPPED:
        p = os.path.join(BUILD, n)
        if not os.path.exists(p):
            continue
        fat = Fat12(p)
        check(find(fat, "SOUND.DRV") == sbytes,
              "%s still carries the SHIPPED SOUND.DRV" % n)
        check(find(fat, "SNDDIAG.O88") is None,
              "%s carries no SNDDIAG.O88" % n)
    done("t_debugdisk")


if __name__ == "__main__":
    main()
