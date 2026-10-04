"""PiXEL's guest reader (SPEC.md 106.13): the package's symbols out of nasm's
map, assembled from THIS tree, and the instance found by the image it
carries. Imported by tests/pxdecode.py, tests/pxopen.py and tests/pxpaint.py;
not a test itself."""
import os
import subprocess
import tempfile

import os88geom

u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)


def pkg_syms(defines=()):
    """{name: offset} and the image bytes, of apps/pixel/pixel.asm as it is."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open("apps/pixel/pixel.asm").read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/pixel/"] + ["-D" + x for x in defines] +
                       ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def instance(m, S, image):
    """PiXEL's segment: the instance whose image carries this header."""
    for i in range(12):
        r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
        c = u16(r, os88geom.I_SPTR)
        if c and m.read(c * 16, 32) == image[:32]:
            return c
    return None


def part_syms(asm, build="build"):
    """{name: offset} of one of PiXEL's LINKED parts (apps/pixel/<asm>), as
    THIS tree assembles it against build/pxlink.inc - the part's own state
    is read through its segment, which op_table names while it is fetched."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(os.path.join("apps", "pixel", asm)).read()
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/pixel/", "-I", build + "/",
                        "-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out
