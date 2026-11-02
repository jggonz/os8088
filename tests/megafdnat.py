#!/usr/bin/env python3
"""megafdnat - kern_mega's NATIVE floppy path, on a real 286's reset path
(docs/plans/KERN-MEGA-PLAN.md 10 M2; kern_mega size pass 1).

    make mega && python3 tests/megafdnat.py [--break] [--keep]

On kern_mega a floppy `int 13h` AH=02h/03h is run by the 765 and the 8237 in
protected mode (`mega_fd13` and the `mm_*` byte level in `.mega`), and every
OTHER BIOS call is REFLECTED - which on a 286 means a CPU reset through CMOS
shutdown code 0Ah, back in at `mega_r286`, whose real-mode `mega_fdrprobe`
(the `.text` copy of MEGA_FDIO) asks the 765 whether the BIOS reset it. No
other row runs that pair: megacurdisk switches the native path OFF, and Bochs
and QEMU are 386s, which leave protected mode by clearing CR0.PE and never
take the reset arm at all (docs/BOCHS.md, docs/86BOX.md).

This boots build/mega.img on 86Box's 286 (vm/286-mega, the owner's mr286)
with the native path ON, and copies BIG.DAT (120KB) from a scratch B: to A:
with Copy and Paste - a native READ and a native WRITE of 240 sectors:

  1. the native path is on after boot and still on after the copy, and
     [mega_fdfail] is 0: no transfer fell back to the BIOS;
  2. the 286 reset path RAN during the copy (86Box's soft-reset count rose),
     and stayed under RESETS: a copy the native path carried takes a handful
     of resets for the reflections around it (2 measured), where one the
     BIOS carried takes one per call (48);
  3. A:'s BIG.DAT is B:'s byte for byte;
  4. nothing charged an exception to a loaded image.

THE FAILING CASE (docs/WRITING-TESTS.md 1): --break plants `stc / ret` at
the head of `.mega`'s mm_fdout - at its copy ABOVE 1MB, where it runs once
mega_enter has moved `.mega` - so every native command byte times out. The
row goes red on 1 and 2 (8 fallbacks, the path off, 48 resets) while the
copy itself still succeeds through the BIOS, which is exactly why 3 alone
could not be the test.

Needs a built 86Box (python3 tools/os88box.py build) and its ROM set.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
ap.add_argument("--break", dest="brk", action="store_true",
                help="the failing case: mm_fdout made to fail every byte")
ap.add_argument("--keep", action="store_true")
A = ap.parse_args()
import megacdrom as MC                                        # noqa: E402
import megacz as CZ                                           # noqa: E402
import os88sym                                                # noqa: E402

BIGKB = 120
RESETS = 20                     # the copy's resets: 2 native, 48 reflected
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def mega_lin(b, name):
    """NAME's linear address once `.mega` runs above 1MB: the code area
    mega_layoutf placed ([mega_coldlin]) plus the `.cold` rung, plus NAME's
    offset in `.mega` - os88sym.linear() answers its conventional RUNG,
    which is dead memory after the switch (os88sym's SECTION_SEG note)"""
    e = os88sym.equates(CZ.DEFS)
    cl = int.from_bytes(b.memr(CZ.S("mega_coldlin"), 4), "little")
    return cl + e["COLD_PARA"] * 16 + CZ.S(name) - e["MEGA_RAM"] * 16


def main():
    for f in ("mega.img", "megak/kernel.map"):
        if not os.path.exists(os.path.join(CZ.B, f)):
            sys.exit("megafdnat: build/%s is not built - `make mega`" % f)
    tmp = tempfile.mkdtemp(prefix="megafdnat")
    img = os.path.join(tmp, "mega.img")         # (named mega*: os88box then
    shutil.copy(os.path.join(CZ.B, "mega.img"), img)    # reads megak/)
    data = bytes((i * 7 + (i >> 9)) & 0xFF for i in range(BIGKB * 1024))
    big = os.path.join(tmp, "BIG.DAT")
    with open(big, "wb") as f:
        f.write(data)
    bimg = os.path.join(tmp, "megab.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440", big], check=True,
                   stdout=subprocess.DEVNULL)
    S = CZ.S
    b = CZ.boot(img, bimg, "megafdnat%d" % os.getpid(), A.keep)
    copied = False
    try:
        if A.brk:
            a = mega_lin(b, "mm_fdout")
            b.cmd("memw 0x%X F9" % a)               # stc
            b.cmd("memw 0x%X C3" % (a + 1))         # ret
            print("   --break: mm_fdout at 0x%X is stc / ret" % a)
        want(CZ.rb(b, S("mega_fdoff")) == 0,
             "1. the native floppy path is on after boot")
        slota, _ = CZ.disk_window(b, "A")
        slotb, blkb = CZ.disk_window(b, "B")
        err = CZ.select(b, slotb, blkb, "BIG.DAT")
        if err:
            raise SystemExit("B:'s BIG.DAT: " + err)
        b.tap("ctrl+c")
        b.ui_done(cap_ms=3000)
        x, y = MC.win_recs(b)[slota][:2]            # A: to the front, the
        b.to(x + 60, y + 5)                         # paste's target
        b.click()
        b.ui_done(cap_ms=3000)
        r0 = b.cmd("info").get("soft_resets", 0)
        t0 = b.ms()
        b.tap("ctrl+v")
        b.ui_done(cap_ms=180000)
        took = b.ms() - t0
        r1 = b.cmd("info").get("soft_resets", 0)
        off, fail = CZ.rb(b, S("mega_fdoff")), CZ.rb(b, S("mega_fdfail"))
        print("   the copy took %.1f emulated s, %d CPU resets, %d native "
              "fallbacks" % (took / 1000.0, r1 - r0, fail))
        want(off == 0 and fail == 0,
             "1. the native path carried it: [mega_fdoff] %d, "
             "[mega_fdfail] %d (want 0 and 0)" % (off, fail))
        want(0 < r1 - r0 <= RESETS,
             "2. the 286 reset path ran, and not once a call: %d resets "
             "during the copy (want 1..%d)" % (r1 - r0, RESETS))
        copied = True
    except SystemExit as e:
        want(False, "the machine stopped answering or a gesture missed: %s"
             % e)
    finally:
        f = CZ.faults(b)
        want(not f, "4. exceptions charged to a loaded image: %d%s" % (
            len(f), "".join("\n        " + x for x in f[:6])))
        b.quit(remove=not A.keep)
    if copied:
        got = os.path.join(tmp, "got.dat")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "cat", "-o", got, img, "BIG.DAT"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        same = r.returncode == 0 and open(got, "rb").read() == data
        want(same, "3. A:'s BIG.DAT is B:'s, byte for byte")
    if not A.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megafdnat: %s" % ("FAIL: " + "; ".join(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
