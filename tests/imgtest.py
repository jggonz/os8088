#!/usr/bin/env python3
"""IMGTEST on the machine: apps/os88img.inc's only gate (the .PIX/.BMP/.PCX
decoders that Paint, Scribe, Frotz and the rest load pictures through).

The `imgcases` fast row holds apps/imgtest/imgcases.inc to the FORMAT
DOCUMENTS; it never runs the decoder. IMGTEST does - every case in its entry
proc, before it makes its window - and until this row that answer was read off
the glass by hand (Makefile: `make imgtestdisk && make test ...`).

THE ANSWER IS READ OUT OF THE PACKAGE, NOT THE PICTURE. it_entry writes each
case's verdict back into its own record, over the picture number at +2: the
record's own (non-zero) address for a pass, zero for a fail. So this row reads
`imgc_tab` out of the package's segment once the window exists. Where
`imgc_tab` is, how many records and how long one is, are asked of the
ASSEMBLER: a probe that %includes imgtest.asm and appends those three words
after it, checked byte-identical to build/imgtest.bin over the image so it
cannot describe a different build. A record-format change therefore moves
nothing here, as long as +2 stays the verdict.

The corpus is generated into a PRIVATE directory (tools/os88imgcase.py's
build(), with its OUTDIR pointed away from build/imgcases) - the same files
`make imgtestdisk` puts on its disk, without writing the shared tree.

IMGTEST is VGA-only (its window is one row a case, taller than a CGA band),
so the machine is MartyPC's register-level VGA XT.

VERIFIED TO FAIL: `--break` flips one byte of BDOWN.BMP's pixel data after
the corpus is written, and that case's checksum misses - one FAIL, named.

  python3 tests/imgtest.py [machine] [--break]
"""
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, "tools")
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88imgcase                                            # noqa: E402
import os88marty as M                                         # noqa: E402
import os88ui                                                 # noqa: E402

_args = [a for a in sys.argv[1:] if not a.startswith("--")]
BREAK = "--break" in sys.argv
MACHINE = _args[0] if _args else "os8088_xt_vga"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def probe(d, binpath):
    """(imgc_tab, IMGC_N, IMGC_REC) for the IMGTEST in build/, from nasm."""
    src = os.path.join(d, "probe.asm")
    out = os.path.join(d, "probe.bin")
    with open(src, "w") as f:
        f.write('%%include "%s"\n    dw imgc_tab, IMGC_N, IMGC_REC\n'
                % os.path.join(ROOT, "apps", "imgtest", "imgtest.asm"))
    subprocess.check_call(["nasm", "-f", "bin", "-w+error",
                           "-I", os.path.join(ROOT, "apps") + "/",
                           "-I", os.path.join(ROOT, "apps", "imgtest") + "/",
                           "-o", out, src])
    blob = open(out, "rb").read()
    ref = open(binpath, "rb").read()
    if blob[:-6] != ref:
        sys.exit("imgtest: the probe does not describe build/imgtest.bin - "
                 "run `make`")
    return struct.unpack("<3H", blob[-6:])


def main():
    o88 = os88build.at("build/imgtest.o88")
    binp = os88build.at("build/imgtest.bin")
    if not os.path.exists(o88):
        sys.exit("imgtest: build/imgtest.o88 missing - `make` builds it")
    d = os.path.join(os88build.at("build"), "imgtest-%d" % os.getpid())
    os.makedirs(d, exist_ok=True)
    try:
        tab, n, rec = probe(d, binp)
        image = struct.unpack_from("<H", open(o88, "rb").read(), 8)[0]
        corpus = os.path.join(d, "cases")
        os88imgcase.OUTDIR = corpus
        cases = os88imgcase.build(False)
        if len(cases) != n:
            sys.exit("imgtest: %d cases generated, the package has %d - "
                     "imgcases.inc is stale" % (len(cases), n))
        names = [c[0] for c in cases]
        if BREAK:
            p = os.path.join(corpus, "BDOWN.BMP")
            b = bytearray(open(p, "rb").read())
            off = struct.unpack_from("<I", b, 10)[0]  # bfOffBits: the pixels
            b[off] ^= 0x5A
            open(p, "wb").write(bytes(b))
            print("imgtest: --break flipped BDOWN.BMP's first pixel byte")
        files = ["APPS:" + o88] + ["APPS:" + os.path.join(corpus, x)
                                  for x in names]
        disk = M.scratch_disk(os.path.join(d, "imgtest.img"), *files,
                              size=360)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=MACHINE) as ui:
            ui.path("B:/APPS/IMGTEST.O88")
            w = ui.wait_window("os88img self-test", limit=120.0)
            m = ui.m
            r = ui._S("wm_wins") + w.i * geom.WIN_SIZE
            seg = struct.unpack("<H", m.read(r + geom.W_SEG, 2))[0]
            raw = bytes(m.readseg(seg, tab, n * rec))
    finally:
        shutil.rmtree(d, ignore_errors=True)
    bad = [names[i] for i in range(n)
           if struct.unpack_from("<H", raw, i * rec + 2)[0] == 0]
    print("imgtest: %d cases, %d failed%s" % (n, len(bad),
          (": " + ", ".join(bad)) if bad else ""))
    sys.exit(1 if bad else 0)


main()
