#!/usr/bin/env python3
"""SPEC.md 22.6.4: a Disk window lists a 300-entry folder, and the claim follows.

    python3 tests/bigdir.py

Driven on MartyPC (kern_big, the 720KB system disk) against a scratch B: built
here: a folder BIG holding 280 one-byte documents and 20 copies of MINES.O88 under
names of their own - 300 entries and '..', where the listing used to stop at
64 and say nothing - and NO ASSOC.DAT, so every package's icon is a real
`int 13h` peek. Every check reads guest state: the window's own block, its
listing claim, and the kernel's words; nothing is judged off pixels.

  1. B:\\BIG lists ALL of it: FS_N = 301, every name present, in SPEC.md
     19.4's order (the Shell sort, 19.4.1), and the claim is fmv_need's size
     for that many - 10KB, at 32 entries a KB;
  2. the harvest spent its budget and no more: exactly DSK_HARV_MAX of the 20
     packages carry an icon reference on the first visit;
  3. up to B:\\, and the claim is cut back to VIEW_KB;
  4. into BIG again: the sixteen bodies are in the store now and cost
     nothing, so the NEXT four are read - all 20 iconned;
  5. Ctrl+A selects all 300 (the bitmap past index 255 included), and Delete
     asks about "300 items" - a word count, where a byte went negative at 128;
     Escape leaves the folder untouched;
  6. a double-click on the LAST package, index 300, launches it - index + 1 is
     301, whose low byte was the "nothing pending" 0 when [ld_pending] was a
     byte... and 45, which is what a byte holds of 301; so the check is that
     a MINES window is what opens.

VERIFIED TO FAIL: with fmv_rsz made to refuse every grow (`stc` / `ret` at
its head) the listing stops at 64 entries in a 2KB claim, and nine of the
eleven checks are red - Delete counts '63 items' - leaving only the cut back
to VIEW_KB and the launch, which never got that far.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

MACHINE = "os8088_5150_herc_720_gla"
NDOC, NPKG = 280, 20
_EQ = os88sym.equates()
FV_DOFF = _EQ["FV_DOFF"]
FS_VKB = _EQ["FS_VKB"]
FS_IOFH = _EQ["FS_IOFH"]
HARV = _EQ["DSK_HARV_MAX"]
VIEW_KB = _EQ["VIEW_KB"]
ICO_R_NONE = _EQ["ICO_R_NONE"]

fails = []


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def build_disk(tmp):
    docs = os.path.join(tmp, "docs")
    os.makedirs(docs)
    args = []
    for i in range(1, NDOC + 1):
        p = os.path.join(docs, "F%03d.TXT" % i)
        with open(p, "wb") as f:
            f.write(b"x")
        args.append("BIG:" + p)
    src = os88build.at("build/mines.o88")
    for i in range(1, NPKG + 1):
        p = os.path.join(docs, "M%02d.O88" % i)
        with open(src, "rb") as f, open(p, "wb") as g:
            g.write(f.read())
        args.append("BIG:" + p)
    disk = os.path.join(tmp, "b.img")
    r = subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "720"] + args, capture_output=True,
                       text=True)
    if r.returncode:
        sys.exit("bigdir: os88disk refused the scratch disk:\n" +
                 r.stderr[-2000:])
    # ...and no warm ASSOC.DAT: it would answer every icon read the harvest
    # budget exists to bound
    subprocess.run([sys.executable, "tools/os88fat.py", "del", disk,
                    "ASSOC.DAT"], check=True, capture_output=True)
    want = [".."] + ["F%03d.TXT" % i for i in range(1, NDOC + 1)] + \
        ["M%02d.O88" % i for i in range(1, NPKG + 1)]
    return disk, want


def main():
    os.chdir(ROOT)
    tmp = os.path.join(ROOT, "build", "bigdir-%d" % os.getpid())
    os.makedirs(tmp)
    try:
        return run(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run(tmp):
    disk, want = build_disk(tmp)
    sysimg = os88build.at("build/os8088-720.img")
    with os88ui.boot(sysimg, apps=disk, machine=MACHINE, quick=True) as ui:
        m = ui.m
        S = ui._S
        lin = getattr(m, "lin", lambda s, o=0: (s << 4) + o)

        def word(blk, off):
            return int.from_bytes(m.read(blk + off, 2), "little")

        def byte(blk, off):
            return m.read(blk + off, 1)[0]

        def iconned(blk, names):
            vseg = word(blk, geom.FS_VSEG)
            n = word(blk, geom.FS_N)
            refs = m.read(lin(vseg, byte(blk, FS_IOFH) << 8), n)
            return [nm for nm, r in zip(names, refs)
                    if nm.endswith(".O88") and r != ICO_R_NONE]

        # --- 1: the whole folder --------------------------------------------
        w = ui.path("B:/BIG")
        ui.still()
        blk = ui._fsblk(w)
        n = word(blk, geom.FS_N)
        names = [nm for nm, _t in ui.listing(w)]
        print("== SPEC.md 22.6.4: B:\\BIG, %d entries listed, claim %dKB =="
              % (n, byte(blk, FS_VKB)))
        check("FS_N is all 301 (300 and '..')", n == len(want), "(%d)" % n)
        check("every name, in folders-then-name order", names == want,
              "(first difference at %r)" % next(
                  (i for i, (a, b) in enumerate(zip(names, want)) if a != b),
                  len(names)))
        need = max(VIEW_KB, (len(want) + 31) // 32)
        check("the claim is fmv_need(301) = %dKB" % need,
              byte(blk, FS_VKB) == need, "(%d)" % byte(blk, FS_VKB))

        # --- 2: the harvest budget ------------------------------------------
        first = iconned(blk, names)
        check("the first visit peeked exactly %d packages" % HARV,
              len(first) == HARV, "(%d: %s)" % (len(first), first))

        # --- 3: up, and the claim is cut back --------------------------------
        w = ui.path("B:/")
        ui.still()
        blk = ui._fsblk(w)
        print("  B:\\ in block %x, FS_N %d" % (blk, word(blk, geom.FS_N)))
        check("at B:\\ the claim is back to VIEW_KB",
              byte(blk, FS_VKB) == VIEW_KB, "(%dKB)" % byte(blk, FS_VKB))

        # --- 4: again, and the next four ------------------------------------
        w = ui.path("B:/BIG")
        ui.still()
        blk = ui._fsblk(w)
        names = [nm for nm, _t in ui.listing(w)]
        check("the second visit lists all 301 again",
              word(blk, geom.FS_N) == len(want))
        again = iconned(blk, names)
        check("...and every package is iconned now",
              len(again) == NPKG, "(%d)" % len(again))

        # --- 5: Ctrl+A over 300, and Delete's count -------------------------
        m.key("ControlLeft", down=True, up=False)
        os88marty.pace(m, 0.05)
        os88marty.key_taken(m, "KeyA")
        ui.still()
        m.key("ControlLeft", down=False, up=True)
        os88marty.pace(m, 0.15)
        vseg = word(blk, geom.FS_VSEG)
        bm = m.read(lin(vseg), FV_DOFF)
        nbits = sum(bin(b).count("1") for b in bm)
        check("Ctrl+A set 301 bits, index 300 among them",
              nbits == len(want) and bm[300 >> 3] & (1 << (300 & 7)),
              "(%d bits)" % nbits)
        os88marty.key_taken(m, "Delete")
        ui.still()
        onam = bytes(m.read(S("fm_onam"), 13)).split(b"\0")[0].decode()
        check("Delete asks about '300 items' ('..' is never one)",
              onam == "300 items", "(%r)" % onam)
        os88marty.key_taken(m, "Escape")
        ui.still()
        check("...and Escape deleted nothing",
              len(ui.listing(w)) == len(want))

        # --- 6: launch the last entry --------------------------------------
        w2 = ui.path("B:/BIG/M%02d.O88" % NPKG)
        ui.still()
        title = (w2.title if hasattr(w2, "title") else "") or ""
        print("  launched: %r" % (title,))
        check("a double-click on index 300 runs THAT package",
              "MINE" in title.upper(), "(%r)" % title)

    print("bigdir: %s" % ("FAIL: " + ", ".join(fails) if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
