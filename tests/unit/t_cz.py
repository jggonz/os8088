#!/usr/bin/env python3
"""The split set's three implementations agree about the format (SPEC.md 20.17).

tools/os88cz.py is the REFERENCE; kernel/compress.inc's join (22.23.5) and
dostools/os88cz.asm (OS88CZ.COM, 20.17.4) are the copies. This row is the
host half of keeping them one format - the machine half is tests/czjoin.py
and the DOS half tests/czdos.py:

  1. `os88cz --selfcheck`: every method round-trips at two part sizes, and
     each refusal the machine makes is made on the host first - a missing
     part, a part from another set, two parts swapped, a damaged byte, a
     truncated set, a name that would be its own part's.
  2. THE NUMBERS ARE ONE SET OF NUMBERS: the part header, the record header
     and the block size as each copy spells them, read out of its source.
     A copy that disagrees does not crash - it rejects every set, or worse,
     accepts a short one.
  3. "A PART FITS A FRESH DISK" IS A CLAIM, so it is tested: a part of
     exactly SIZES[g] bytes is put on a floppy of geometry g by the tool
     that builds every shipped floppy, and must fit. The table is four
     cluster counts somebody typed.
  4. The window's arithmetic, which runs with no display: the estimate a
     person reads BEFORE anything is written must be the stored split's real
     part count, and a long file name must become a name DOS will take.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88cz                                              # noqa: E402
import os88lz                                              # noqa: E402
import os88czgui                                           # noqa: E402

fails = []


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        fails.append(what)


def equ(path, name):
    m = re.search(r"^\s*%s\s+equ\s+(\S+)" % name,
                  open(os.path.join(ROOT, path)).read(), re.M)
    return int(m.group(1), 0) if m else None


def main():
    print("t_cz: the split set's three copies (SPEC.md 20.17)")
    check(os88cz._selfcheck([]) == 0, "os88cz --selfcheck")

    # --- 2. the numbers --------------------------------------------------
    for path, pairs in (
            ("kernel/compress.inc", (("CMZ_JHDR", os88cz.CS_HDR),
                                     ("CMZ_JREC", os88cz.REC_HDR))),
            ("dostools/os88cz.asm", (("CS_HDR", os88cz.CS_HDR),
                                     ("REC_HDR", os88cz.REC_HDR),
                                     ("BLOCK", os88cz.BLOCK),
                                     ("MAXPARTS", os88cz.CS_MAXPARTS)))):
        for name, want in pairs:
            got = equ(path, name)
            check(got == want, "%s %s = %r (os88cz.py: %d)"
                  % (path, name, got, want))
    # the join's window: a whole record plus one refill must fit it
    win = equ("kernel/compress.inc", "CMZ_JWIN")
    rd = equ("kernel/compress.inc", "CMZ_JRD")
    check(win is not None and rd is not None and
          os88cz.REC_MAX - 1 + rd <= win * 1024,
          "the join's %sKB window holds a record less a byte plus a %d-byte "
          "refill (%d)" % (win, rd or 0, os88cz.REC_MAX - 1 + (rd or 0)))
    for path in ("kernel/compress.inc", "dostools/os88cz.asm"):
        src = open(os.path.join(ROOT, path)).read()
        check("'CS'" in src, "%s names the 'CS' magic" % path)
    # the DOS tool's size table is the same four disks
    src = open(os.path.join(ROOT, "dostools/os88cz.asm")).read()
    for g, kb in (("360k", 360), ("720k", 720), ("1.2m", 1200),
                  ("1.44m", 1440)):
        sz = 1024 if g in ("360k", "720k") else 512    # the cluster
        cl = os88cz.SIZES[g] // sz
        check("%d * %d" % (cl, sz) in src,
              "OS88CZ.COM's %d is %d * %d, os88cz.py's" % (kb, cl, sz))

    # --- 3. a part fits a fresh disk -----------------------------------------
    with tempfile.TemporaryDirectory() as d:
        for g, kb in os88cz.GEOM_KB.items():
            p = os.path.join(d, "FULL.001")
            open(p, "wb").write(b"\xA5" * os88cz.SIZES[g])
            img = os.path.join(d, "f.img")
            r = subprocess.run([sys.executable,
                                os.path.join(ROOT, "tools", "os88disk.py"),
                                "-o", img, "--size", str(kb), p],
                               capture_output=True, text=True)
            check(r.returncode == 0,
                  "a %s part of %d bytes fits a fresh %dKB floppy"
                  % (g, os88cz.SIZES[g], kb))
            open(p, "wb").write(b"\xA5" * (os88cz.SIZES[g] + 1))
            r = subprocess.run([sys.executable,
                                os.path.join(ROOT, "tools", "os88disk.py"),
                                "-o", img, "--size", str(kb), p],
                               capture_output=True, text=True)
            check(r.returncode != 0,
                  "...and one byte more does NOT (the table is the edge)")

    # --- 4. the window's arithmetic ------------------------------------------
    data = bytes(range(256)) * 1500             # 384,000 bytes
    for size in ("360k", "720k", 70000):
        est = os88czgui.estimate(len(data), size)
        real = len(os88cz.split(data, "E.DAT", size, os88cz.M_STORE, jobs=1))
        check(est == real, "estimate %r: %d part(s), a stored split makes %d"
              % (size, est, real))
    for name in ("my holiday video.v88", "a.b.c", "LONGERNAME.TEXT",
                 "clip.001"):
        s = os88czgui.suggest_name(name)
        try:
            os88cz.name83(s)
            ok = True
        except os88cz.CZError:
            ok = False
        check(ok and len(s) <= 12 and not os88cz.is_part_name(s),
              "%r suggests %r, an 8.3 name that is not a part's"
              % (name, s))
    # --- 5. where a DROPPED file goes (os88czgui.drop_tab) -----------------
    # the classification is off the file's head, and a 'CZ' file needs more
    # than its first two bytes: reading two once sent every one to Split
    T = os88czgui
    with tempfile.TemporaryDirectory() as d:
        body = bytes(range(256)) * 200
        cases = {"part": os88cz.split(body, "D.DAT", 40000,
                                      os88cz.M_STORE, jobs=1)[1],
                 "cz": os88cz.pack(body, os88cz.M_LZ4),
                 "plain": body}
        for want, blob in cases.items():
            p = os.path.join(d, want)
            open(p, "wb").write(blob)
            check(T.file_kind(p) == want, "a %s file reads as %r"
                  % (want, T.file_kind(p)))
    for kind, cur, tab in (("part", T.T_SPLIT, T.T_JOIN),
                           ("part", T.T_PACK, T.T_JOIN),
                           ("cz", T.T_SPLIT, T.T_PACK),
                           ("cz", T.T_JOIN, T.T_PACK),
                           ("plain", T.T_SPLIT, T.T_SPLIT),
                           ("plain", T.T_JOIN, T.T_SPLIT),
                           ("plain", T.T_PACK, T.T_PACK)):
        check(T.drop_tab(kind, cur) == tab, "a %s dropped on tab %d goes "
              "to tab %d (wanted %d)" % (kind, cur, T.drop_tab(kind, cur),
                                         tab))
    big = os88cz.CZ_OPENMAX + 1
    check(T.drop_tab("plain", T.T_PACK, big) == T.T_SPLIT,
          "a plain file too big to pack, dropped on Pack, goes to Split")

    # --- 6. pack: what no machine can open is refused, and LZB is FAST ----
    # A 'CZ' file is expanded whole into a claim of its size, so over
    # CZ_OPENMAX it is `Not enough memory` everywhere; the refusal says Split.
    # And --lzb takes the machine's parse: os88lz's exact one ran for longer
    # than anyone waited on a 720KB disk image, and zero-filled runs are its
    # worst case - this block is what made it slow, and must be seconds
    import time
    try:
        os88cz.pack(bytes(big), os88cz.M_LZB)
        check(False, "a %d-byte file is refused before anything is encoded"
              % big)
    except os88cz.CZError as e:
        check("Split" in str(e), "a %d-byte file is refused, pointing at "
              "Split: %s" % (big, str(e)[:60]))
    zeros = (bytes(range(256)) * 64 + bytes(48 * 1024)) * 4    # 256KB
    t = time.time()
    z = os88cz.pack(zeros, os88cz.M_LZB)
    dt = time.time() - t
    check(os88cz.unpack(z) == zeros and dt < 20,
          "--lzb over 256KB of zero runs: %.1fs, %d bytes, round trip" %
          (dt, len(z)))
    check(z[os88lz.CZ_HDR:] == os88lz.lzb_compress_machine(zeros),
          "...and it IS the machine's parse (what cmz_pack writes)")
    print("t_cz: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
