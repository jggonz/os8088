#!/usr/bin/env python3
"""FPTEST on the machine: apps/os88fp.inc's only gate (SPEC.md 84.4).

FPTEST is not shipped software (Makefile: "deliberately NOT on any disk"), and
until this row it was read off the glass by hand. Every case in
apps/fptest/fpcases.inc is a host-computed IEEE-754 expectation, and the
package runs all of them in its W_PAINT - once on the software path and, on a
machine with an 8087, once more on the coprocessor - and composes a summary
line in its own bss:

    os88fp ALL PASS  soft 0  8087 -

That line is what this row reads, out of the package's segment (W_SEG of its
window) at `os88_image_end + 7` (fpt_out, after fpt_ox/oy/bad/r/cferr). It is
the same string the window draws, so a pass here is the reading a person took
before - without the picture.

Sheet and Chart call fp_i2a, fp_a2i and fp_cmpab (and fpx_cmpab with an 8087)
and those had no case until this row's commit; they are rows 15-32 of the
x-150 column now.

VERIFIED TO FAIL, through `--o88` on a copy assembled with one include
swapped: `I2A 1000, d_1000` pointed at d_100 reads `FAILURES  soft 1`, and
fps_cmpab's trailing `cmp ax, 0` changed to `cmp ax, 1` - AX right, the FLAGS
wrong - reads `soft 3` (the three cases whose answer is not -1).

  python3 tests/fptest.py [machine] [--o88 PATH]
"""
import os
import sys
import struct

sys.path.insert(0, "tools")
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88marty as M                                         # noqa: E402
import os88ui                                                 # noqa: E402

_args = sys.argv[1:]
O88 = "build/fptest.o88"        # --o88 PATH: a deliberately broken build,
if "--o88" in _args:            # for watching the row go red
    O88 = _args.pop(_args.index("--o88") + 1)
    _args.remove("--o88")
MACHINE = _args[0] if _args else "os8088_5150_cga_gla"
FPT_OUT = 7                     # fpt_out's offset past os88_image_end


def main():
    o88 = os88build.at(O88) if O88.startswith("build/") else O88
    if not os.path.exists(o88):
        sys.exit("fptest: %s missing - `make` builds it" % o88)
    image = struct.unpack_from("<H", open(o88, "rb").read(), 8)[0]
    img = os.path.join(os88build.at("build"), "fptest-%d.img" % os.getpid())
    disk = M.scratch_disk(img, "APPS:" + o88, size=360)
    try:
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=MACHINE, verbose=False) as ui:
            ui.path("B:/APPS/FPTEST.O88")
            w = ui.wait_window("FP self-test")
            m = ui.m
            rec = ui._S("wm_wins") + w.i * geom.WIN_SIZE
            seg = struct.unpack("<H", m.read(rec + geom.W_SEG, 2))[0]

            def line(mm=m):
                b = bytes(mm.readseg(seg, image + FPT_OUT, 40))
                return b.split(b"\0")[0].decode("latin-1")

            M.until(m, lambda mm: line(mm).startswith("os88fp "),
                    "FPTEST's summary line", poll=0.2, limit=60.0)
            got = line()
    finally:
        for f in (img, img + ".args"):     # scratch_disk's sidecar too
            try:
                os.unlink(f)
            except OSError:
                pass
    print("fptest: %s" % got)
    f = got.split()
    ok = "ALL PASS" in got and f[f.index("soft") + 1] == "0" \
        and f[f.index("8087") + 1] in ("0", "-")
    print("fptest: %s" % ("all pass" if ok else "FAILED"))
    sys.exit(0 if ok else 1)


main()
