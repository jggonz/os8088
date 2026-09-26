#!/usr/bin/env python3
"""A package may SHIP the 8x8 its documents wear (SPEC.md 54.3.2), and every
reader of that block prefers it to the reduction.

    python3 tests/unit/t_docglyph.py

Three readers of a package's first 128 bytes exist on the host - the
validator (tools/os88pkg.py), the baker (tools/os88mini.py) and the disk
builder's ASSOC.DAT writer (tools/os88disk.py) - and the kernel's four are on
the glass (tests/assocglyph.py). This row is the host half, and its shape is
docs/WRITING-TESTS.md 1: every rule is broken on purpose and must go red.

  1. THE VALIDATOR: a well-formed bit-5 package is accepted; one with a blank
     glyph, one with bit 5 and no association block, one with a non-zero
     reserved half, and one whose entry lands inside the block are refused.
  2. THE CLEAR PREFIX: compressing a bit-5 package keeps its first 128 bytes
     verbatim, so the mount reads the glyph off the first sector with no
     decoder (SPEC.md 20.13.1), and image_unwrap gives the input back.
  3. THE BAKER: os88mini emits the SHIPPED bytes, and they differ from the
     reduction of the same icon - which is the control that says the bit is
     being read rather than the icon.
  4. THE TREE: build/dos.o88 sets bit 5, its glyph is the baked DOS line in
     build/associco.inc, and every shipped ASSOC.DAT that carries a DOS row
     is version 2 and carries that glyph in the row.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
from t_image import Vol, read, SYSTEM_IMAGES, DATA_IMAGES  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88pkg                                            # noqa: E402
import os88mini                                           # noqa: E402

PKG = os.path.join(ROOT, "tools", "os88pkg.py")
MINI = os.path.join(ROOT, "tools", "os88mini.py")
GLYPH = bytes((0x7E, 0xFF, 0xBF, 0xDF, 0xB1, 0xFF, 0x7E, 0x00))
ASC_HDR, ASC_ROW, ASC_ROWGLY = 16, 88, 80

# An icon whose reduction is NOT the glyph above: a one-pixel outline box,
# which majority-of-2x2 keeps as an outline - the DOS CRT's own failure.
ICON_DATA = [0x0000, 0x7FFE, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002,
             0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x7FFE, 0x0000, 0x0000]


def package(flags=0x23, glyph=GLYPH, reserved=bytes(8), assoc=True,
            entry=None, body=None):
    """A v3 .bin: header, icon, association block, glyph block, code."""
    if body is None:
        body = (b"\xC3" + bytes(range(256)) * 8)       # ret, then compressible
    parts = [bytearray(32)]
    icon = b"".join(struct.pack("<H", 0x7FFE) for _ in range(16))
    icon += b"".join(struct.pack("<H", w) for w in ICON_DATA)
    parts.append(icon)
    if assoc:
        parts.append(b"\x01COM" + bytes(12))
    if flags & 0x20:
        parts.append(glyph + reserved)
    code_at = sum(len(p) for p in parts)
    if entry is None:
        entry = code_at
    img = bytearray(b"".join(parts) + body)
    struct.pack_into("<HBBHHHH", img, 0, 0x384F, os88pkg.PKG_FMT, flags, 0, entry,
                     len(img), 0)
    img[12:15] = bytes((0xFF, 0xD5, 0xCB))
    img[16:20] = b"TEST"
    return bytes(img)


def run_pkg(blob, tmp, name, *extra):
    src = os.path.join(tmp, name + ".bin")
    out = os.path.join(tmp, name + ".o88")
    with open(src, "wb") as f:
        f.write(blob)
    r = subprocess.run([sys.executable, PKG, src, "-o", out] + list(extra),
                       capture_output=True, text=True)
    return r.returncode, r.stderr + r.stdout, out


def main():
    tmp = tempfile.mkdtemp(prefix="t_docglyph.")

    # 1. the validator
    rc, msg, good = run_pkg(package(), tmp, "good")
    check(rc == 0, "validator: a bit-5 package with icon, block and glyph is "
          "accepted", got=msg.strip()[-200:])
    rc, msg, _ = run_pkg(package(glyph=bytes(8)), tmp, "blank")
    check(rc != 0 and "all zero" in msg, "validator: a BLANK shipped glyph is "
          "refused", "all-zero is the UNRESOLVED sentinel (SPEC.md 54.2) and "
          "would silently mean 'reduce after all'", got=msg.strip()[-200:])
    rc, msg, _ = run_pkg(package(flags=0x21, assoc=False), tmp, "noassoc")
    check(rc != 0 and "bits 0 and 1" in msg, "validator: bit 5 without an "
          "association block is refused", got=msg.strip()[-200:])
    rc, msg, _ = run_pkg(package(reserved=b"\x01" + bytes(7)), tmp, "rsvd")
    check(rc != 0 and "reserved" in msg, "validator: a non-zero reserved half "
          "is refused", got=msg.strip()[-200:])
    rc, msg, _ = run_pkg(package(entry=120), tmp, "entry")
    check(rc != 0, "validator: an entry inside the glyph block is refused",
          got=msg.strip()[-200:])

    # 2. the clear prefix survives compression
    src = package()
    rc, msg, packed = run_pkg(src, tmp, "packed", "--compress", "lz4")
    if check(rc == 0, "compress: a bit-5 package compresses",
             got=msg.strip()[-200:]):
        blob = open(packed, "rb").read()
        check(blob[3] & 0x08, "compress: the output says it is compressed",
              got=hex(blob[3]))
        check(blob[:3] == src[:3] and blob[4:128] == src[4:128]
              and blob[3] & 0x20,
              "compress: the first 128 bytes - header, icon, block, GLYPH - "
              "are verbatim (the flags byte gains bit 3 and keeps bit 5)",
              "SPEC.md 20.13.1: the mount reads the glyph off the first "
              "sector with no decoder")
        check(len(blob) < len(src), "compress: the file got shorter",
              got=len(blob), want="< %d" % len(src))
        check(os88pkg.image_unwrap(blob) == src, "compress: image_unwrap gives "
              "the input back")
        check(os88pkg.clear_prefix(0x23) == 128 and
              os88pkg.clear_prefix(0x03) == 112,
              "compress: clear_prefix is 128 with the glyph and 112 without")

    # 3. the baker
    inc = os.path.join(tmp, "mini.inc")
    r = subprocess.run([sys.executable, MINI, "-o", inc, "T=" + good],
                       capture_output=True, text=True)
    if check(r.returncode == 0, "baker: os88mini accepts a bit-5 package",
             got=(r.stderr + r.stdout).strip()[-200:]):
        line = [l for l in open(inc).read().splitlines()
                if l.startswith("    db")]
        got = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})",
                                                    line[0])) if line else b""
        check(got == GLYPH, "baker: it bakes the SHIPPED glyph", got=got.hex(),
              want=GLYPH.hex())
        reduced = bytes(os88mini.reduce8(ICON_DATA))
        check(reduced != GLYPH, "baker: ...which is NOT the icon's reduction, "
              "so the bit is what is being read", got=reduced.hex())
        check(line and "shipped" in line[0], "baker: the line says 'shipped'",
              got=line)

    # 4. the tree: DOS ships one, the kernel bakes it, every cache carries it
    dos = os.path.join(ROOT, "build", "dos.o88")
    ico = os.path.join(ROOT, "build", "associco.inc")
    if check(os.path.exists(dos) and os.path.exists(ico),
             "build/dos.o88 and build/associco.inc exist", "run `make` first"):
        d = open(dos, "rb").read()
        check(d[3] & 0x20 and d[3] & 3 == 3, "DOS.O88 sets flags bits 0, 1 "
              "and 5", got=hex(d[3]))
        shipped = d[112:120]
        check(shipped == GLYPH, "DOS.O88 ships the terminal glyph",
              got=shipped.hex(), want=GLYPH.hex())
        line = [l for l in open(ico).read().splitlines()
                if l.startswith("    db") and "; DOS" in l]
        baked = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})",
                                                      line[0])) if line else b""
        check(baked == shipped, "the kernel bakes DOS's shipped glyph, not a "
              "reduction", got=baked.hex(), want=shipped.hex())
        rows = 0
        for img in SYSTEM_IMAGES + DATA_IMAGES:
            p = os.path.join(ROOT, "build", img)
            if not os.path.exists(p):
                continue
            v = Vol(read(p), img)
            for path, name11, attr, clus, size in v.walk():
                if path != "" or name11 != b"ASSOC   DAT":
                    continue
                chain, _ = v.chain(clus)
                blob = b""
                for c in chain:
                    off = v.cluster_lba(c) * v.byts
                    blob += v.blob[off:off + v.spc * v.byts]
                blob = blob[:size]
                check(blob[6] == 2, "%s: ASSOC.DAT is version 2" % img,
                      got=blob[6:7])
                for i in range(blob[7]):
                    o = ASC_HDR + i * ASC_ROW
                    if blob[o:o + 8] != b"DOS     ":
                        continue
                    rows += 1
                    g = blob[o + ASC_ROWGLY:o + ASC_ROWGLY + 8]
                    check(g == shipped, "%s: the DOS row carries the shipped "
                          "glyph, so a cache HIT wears it" % img,
                          got=g.hex(), want=shipped.hex())
        check(rows > 0, "at least one shipped ASSOC.DAT carries a DOS row",
              got=rows)
    done("shipped document glyphs (SPEC.md 54.3.2)")


if __name__ == "__main__":
    main()
