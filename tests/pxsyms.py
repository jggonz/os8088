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


def same_header(got, image):
    """The first 32 bytes of a loaded PiXEL against this tree's image, with
    what the FILE adds excepted: a compressed package runs with the header it
    shipped (the clear prefix, SPEC.md 20.13.1), so the compression bits of
    the flags byte (+3, bits 3 and 4) are set there and not in the assembly,
    and a parted package whose image ships packed keeps the packed length in
    the LINK word (+4), where the assembly has 0 (SPEC.md 20.13.8)."""
    return (got[:3] == image[:3] and (got[3] & ~0x18) == (image[3] & ~0x18)
            and got[6:32] == image[6:32])


def instance(m, S, image):
    """PiXEL's segment: the LIVE instance whose image carries this header.
    A record whose state is not 1 is a closed one whose region may still
    hold the image (review-w8 N2's probe waited for a close through a freed
    record and never saw one)."""
    for i in range(12):
        r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
        c = u16(r, os88geom.I_SPTR)
        if (r[os88geom.I_STATE] == 1 and c
                and same_header(m.read(c * 16, 32), image)):
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
