#!/usr/bin/env python3
"""A Sticky App carries a CARD (SPEC.md 110.12): the name, the description and
the 32x32 picture the Sticky Apps menu lists it by, read out of the file's
first sector without launching it.

    python3 tests/unit/t_stikcard.py

The kernel's reader is the menu (kernel/stikmenu.inc, on the glass in
tests/stikmenu.py). This row is the host half, and its shape is
docs/WRITING-TESTS.md 1: every rule is broken on purpose and must go red.

  1. THE VALIDATOR: a well-formed bit-7 package is accepted, alone and behind
     an icon, an association block and a glyph; a bad magic, a name with no
     NUL, an empty name, a non-printable description, bytes after a NUL, an
     entry inside the card and an image too short to hold it are refused.
  2. THE CLEAR PREFIX: compressing a bit-7 package keeps the card verbatim -
     it is inside the clear prefix (SPEC.md 20.13.1), so the menu reads it
     with no decoder - and image_unwrap gives the input back; clear_prefix
     counts the 192 bytes and stik_card_at finds the card behind every
     combination of the three blocks in front of it.
  3. THE KERNEL AGREES: kernel/loader.inc's and kernel/compress.inc's
     ladders step over LD_STIK_SIZE = 192 on LD_HF_STIK = 0x80, and the
     menu's own constants (kernel/stikmenu.inc) are the tool's.
  4. THE TREE: build/deskcal.o88 sets bit 7, its card is valid and names
     'Desk Calendar', and every .O88 in STIKAPPS/ on every shipped system
     image carries a valid card - a Sticky App the menu cannot list is one
     the user can only reach by hand.
"""
import atexit
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402
from t_image import SYSTEM_IMAGES                         # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88pkg                                            # noqa: E402

PKG = os.path.join(ROOT, "tools", "os88pkg.py")
BUILD = os.environ.get("BUILD", os.path.join(ROOT, "build"))
OPTIONAL_SYSTEM = ["emu.img"]


def card(name=b"Probe", desc=b"A test card", magic=b"SA", ver=1, rsv=0,
         picture=None):
    c = bytearray(192)
    c[0:2] = magic
    c[2] = ver
    c[3] = rsv
    c[4:4 + len(name)] = name
    c[28:28 + len(desc)] = desc
    c[64:192] = picture if picture is not None else bytes(range(128))
    return bytes(c)


def package(flags=0x80, crd=None, entry=None, body=None, short=False):
    """A v3 .bin: header, then whatever blocks `flags` declares, the card,
    code."""
    if body is None:
        body = b"\xC3" + bytes(range(64)) * 128   # ret, then compressible
    parts = [bytearray(32)]
    if flags & 1:
        parts.append(b"\xFF\xFF" * 16 + b"\x7E\x7E" * 16)
    if flags & 2:
        parts.append(b"\x01COM" + bytes(12))
    if flags & 0x20:
        parts.append(bytes((0x7E,) * 8) + bytes(8))
    if flags & 0x80:
        c = crd if crd is not None else card()
        parts.append(c[:100] if short else c)
    code_at = sum(len(p) for p in parts)
    if entry is None:
        entry = code_at
    img = bytearray(b"".join(parts) + (b"" if short else body))
    struct.pack_into("<HBBHHHH", img, 0, 0x384F, os88pkg.PKG_FMT, flags, 0,
                     min(entry, len(img) - 1), len(img), 0)
    img[12:15] = bytes((0xFF, 0xD5, 0xCB))
    img[16:21] = b"PROBE"
    return bytes(img)


def run_pkg(blob, tmp, name, *extra):
    src = os.path.join(tmp, name + ".bin")
    out = os.path.join(tmp, name + ".o88")
    with open(src, "wb") as f:
        f.write(blob)
    r = subprocess.run([sys.executable, PKG, src, "-o", out] + list(extra),
                       capture_output=True, text=True)
    return r.returncode, r.stderr + r.stdout, out


def equs(path, names):
    """{NAME: int} for `NAME equ <number>` lines in a source file."""
    out = {}
    for line in open(path, encoding="latin-1"):
        m = re.match(r"\s*([A-Z_0-9]+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)\b", line)
        if m and m.group(1) in names:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def main():
    tmp = tempfile.mkdtemp(prefix="t_stikcard.")
    atexit.register(shutil.rmtree, tmp, True)

    # 1. the validator ----------------------------------------------------
    for flags in (0x80, 0x81, 0x83, 0xA3, 0xC1):
        rc, msg, _ = run_pkg(package(flags), tmp, "good%02x" % flags)
        check(rc == 0, "validator: a card behind flags 0x%02X is accepted"
              % flags, got=msg.strip()[-200:])
    bad = [
        ("magic", package(crd=card(magic=b"XX")), "'SA'"),
        ("version", package(crd=card(ver=2)), "'SA'"),
        ("nonul", package(crd=card(name=b"N" * 24)), "no NUL"),
        ("empty", package(crd=card(name=b"")), "empty"),
        ("unprint", package(crd=card(desc=b"tab\there")), "printable"),
        ("after", package(crd=card(name=b"Ok\x00junk")), "after its NUL"),
        ("entry", package(entry=40), "outside"),
        ("short", package(short=True), "needs at least"),
    ]
    for tag, blob, word in bad:
        rc, msg, _ = run_pkg(blob, tmp, "bad_" + tag)
        check(rc != 0 and word in msg, "validator: refuses a card with %s"
              % tag, got=msg.strip()[-200:])

    # 2. the clear prefix -------------------------------------------------
    check(os88pkg.clear_prefix(0x80) == 224 and
          os88pkg.clear_prefix(0x81) == 288 and
          os88pkg.clear_prefix(0xA3) == 320,
          "prefix: clear_prefix counts the card's 192 bytes (224, 288, 320)")
    for flags, at in ((0x80, 32), (0x81, 96), (0x83, 112), (0xA3, 128)):
        check(os88pkg.stik_card_at(flags) == at,
              "prefix: the card behind flags 0x%02X starts at %d" % (flags, at),
              got=os88pkg.stik_card_at(flags))
    src = package(0xA3)
    rc, msg, packed = run_pkg(src, tmp, "packed", "--compress", "lz4")
    if check(rc == 0, "compress: a bit-7 package compresses",
             got=msg.strip()[-200:]):
        blob = open(packed, "rb").read()
        check(blob[3] & 0x88 == 0x88, "compress: the output says it is "
              "compressed and keeps bit 7", got=hex(blob[3]))
        check(blob[:3] == src[:3] and blob[4:320] == src[4:320],
              "compress: the first 320 bytes - header, icon, block, glyph, "
              "CARD - are verbatim",
              "SPEC.md 20.13.1: the menu reads the card off the first sector "
              "with no decoder")
        check(os88pkg.image_unwrap(blob) == src,
              "compress: image_unwrap gives the input back")

    # 3. the kernel agrees -------------------------------------------------
    ld = equs(os.path.join(ROOT, "kernel", "loader.inc"),
              ("LD_HF_STIK", "LD_STIK_SIZE"))
    check(ld.get("LD_HF_STIK") == os88pkg.STIK_BIT and
          ld.get("LD_STIK_SIZE") == os88pkg.STIK_SIZE,
          "kernel: loader.inc's LD_HF_STIK/LD_STIK_SIZE are the tool's", got=ld)
    for f in ("loader.inc", "compress.inc"):
        txt = open(os.path.join(ROOT, "kernel", f), encoding="latin-1").read()
        check(re.search(r"test al, LD_HF_STIK\s*;[^\n]*\n\s*jz \.nostk[^\n]*"
                        r"\n\s*add (bx|dx), LD_STIK_SIZE", txt) is not None,
              "kernel: %s's clear-prefix ladder steps over the card" % f,
              why="a ladder that does not would expand a compressed Sticky "
                  "App's body from inside its card")
    sm = os.path.join(ROOT, "kernel", "stikmenu.inc")
    if os.path.exists(sm):
        k = equs(sm, ("STM_C_NAME", "STM_C_DESC", "STM_C_PIC", "STM_C_SIZE",
                      "STM_C_NAMEMAX", "STM_C_DESCMAX"))
        want = {"STM_C_NAME": os88pkg.STIK_NAME[0],
                "STM_C_DESC": os88pkg.STIK_DESC[0],
                "STM_C_PIC": 64, "STM_C_SIZE": os88pkg.STIK_SIZE,
                "STM_C_NAMEMAX": os88pkg.STIK_NAME[1],
                "STM_C_DESCMAX": os88pkg.STIK_DESC[1]}
        check(k == want, "kernel: stikmenu.inc's card offsets are the tool's",
              got=k, want=want)

    # 4. the tree ----------------------------------------------------------
    dc = os.path.join(BUILD, "deskcal.o88")
    if check(os.path.exists(dc), "tree: build/deskcal.o88 exists"):
        b = open(dc, "rb").read()
        check(b[3] & 0x80, "tree: DESKCAL.O88 sets flags bit 7",
              got=hex(b[3]))
        at = os88pkg.stik_card_at(b[3])
        c = b[at:at + 192]
        check(os88pkg.stik_card_problem(c) is None,
              "tree: DESKCAL.O88's card is valid",
              got=os88pkg.stik_card_problem(c))
        check(c[4:18] == b"Desk Calendar\x00",
              "tree: DESKCAL.O88's card names 'Desk Calendar'", got=c[4:28])
        check(any(c[64:192]), "tree: DESKCAL.O88's card has a picture")
    from t_stik import walk
    for img in SYSTEM_IMAGES + OPTIONAL_SYSTEM:
        p = os.path.join(BUILD, img)
        if not os.path.exists(p):
            check(img in OPTIONAL_SYSTEM, "tree: %s is missing" % img)
            continue
        apps = {k: v for k, v in walk(p).items()
                if k.startswith("STIKAPPS/") and k.endswith(".O88")}
        check(apps, "tree: %s's STIKAPPS/ holds a Sticky App" % img)
        for k, v in sorted(apps.items()):
            at = os88pkg.stik_card_at(v[3])
            why = (os88pkg.stik_card_problem(v[at:at + 192])
                   if v[3] & 0x80 else "flags bit 7 is clear")
            check(why is None, "tree: %s %s carries a valid card" % (img, k),
                  got=why)
    done("t_stikcard")


if __name__ == "__main__":
    main()
