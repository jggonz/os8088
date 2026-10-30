#!/usr/bin/env python3
"""Frotz plays a story past 256KB on kern_mega, in protected mode (KERN-MEGA-MEMORY-PLAN MC7).

    make mega build/frotz.o88 build/zt/ZBIG.Z8 && python3 tests/megafrotz.py
    python3 tests/megafrotz.py --pkg OLD/frotz.o88   another build (the red)
    python3 tests/megafrotz.py --keep                keep the E9 log and shots

Under kern_mega in protected mode every claim is an LDT SELECTOR (SPEC.md
50.6.12), and a selector reaches at most 64KB. Frotz holds a whole story in
ONE claim and reached past 64KB of it by adding 128-byte granules to the
claim's segment - which names some other descriptor once the segment is a
selector - and it tested the TI bit to decide a selector meant "a story of
64KB or less" and read every high address out of the first 64KB. It now
walks the claim by 32KB PAGE through a table of OSAPI_SEG SEG_SUB selectors
made at load (apps/frotz/zmem.inc), the same table holding plain segments on
every real-mode kernel.

THE STORY is tests/frotz/zbiggen.py's 282KB v8 one: nine pages, past the
256KB a v5 story stops at, so the page index reaches DX = 4 of a 20-bit
address. Its @verify walks all of it before the first prompt; `go` calls a
routine in every page, through loops straddling every boundary; `text`
decodes strings out of the top pages into a stream 3 table. What it must
print is the generator's own model (build/zt/ZBIG8.GLD) - no reference
interpreter is needed.

One boot of build/mega.img under Bochs, a 1.44MB B: holding FROTZ.O88 and
ZBIG.Z8, the story opened from B:'s Disk window by its association. The row
fails on a `kern_mega FAULT`, on the interpreter halting, or on any line of
the transcript differing from the gold.

THE FAILING CASE (docs/WRITING-TESTS.md 1): FROTZ.O88 from before this
(`--pkg`, the build at fd19b8d0) - see the module's own run for its reading.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
import editmove  # noqa: E402
import os88bochs  # noqa: E402

BUILD = os.path.join(ROOT, "build")
STORY = os.path.join(BUILD, "zt", "ZBIG.Z8")
GOLD = os.path.join(BUILD, "zt", "ZBIG8.GLD")
ZIW_NONE, ZIW_LINE = 0, 1
PROBES = ["zi_want", "zi_linen", "zw_sbseg", "zw_sbhead", "zw_sbcount",
          "zw_sbmax", "zf_err", "zf_dead", "zf_sseg", "zf_skb"]
ZW_UPMAX, ZW_LSTRIDE = 12, 128


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


class Frotz:
    def __init__(self, box):
        self.box = box
        p = editmove.pkg_syms(os.path.join(ROOT, "apps", "frotz", "frotz.asm"),
                              (os.path.join(ROOT, "apps") + "/",
                               os.path.join(ROOT, "apps", "frotz") + "/"),
                              PROBES + ["os88_image_end"])
        # relative to the image's end - the header's image size, +8 - so a
        # build of another size (--pkg) is read at its own offsets
        self.rel = {n: p[n] - p["os88_image_end"] for n in PROBES}

    def bss(self):
        """The whole probe block in one go: {name: word}."""
        base = self.box.pkg_base("FROTZ")
        if base is None:
            return None
        end = u16(self.box.read(base + 8, 2))
        lo = min(self.rel.values())
        hi = max(self.rel.values()) + 2
        raw = self.box.read(base + end + lo, hi - lo)
        return {n: u16(raw, r - lo) for n, r in self.rel.items()}

    def lines(self, v):
        sb = self.box.sel_base(v["zw_sbseg"]) if v["zw_sbseg"] & 4 \
            else v["zw_sbseg"] * 16
        head, cnt, mx = v["zw_sbhead"], v["zw_sbcount"], v["zw_sbmax"]
        out = []
        for L in range(cnt):
            slot = (head - cnt + L) % mx
            off = sb + ZW_UPMAX * ZW_LSTRIDE + slot * ZW_LSTRIDE
            rec = self.box.read(off, 2 + ZW_LSTRIDE - 2)
            out.append(rec[2:2 + rec[0]].decode("latin-1").strip())
        return out


def halted(v):
    return (v["zf_err"] & 0xFF) or (v["zf_dead"] & 0xFF)


def play(box, fz, out):
    """Open the story, run go/text/go; answer (transcript lines, failure)."""
    box.capture()
    box.dbl(591, 105, "B:'s Disk window")
    box.dbl(170, 139, "ZBIG.Z8's open", lock=False)     # the second row
    v = box.until(lambda: (lambda v: v if v and (halted(v) or
                                                 v["zi_want"] & 0xFF ==
                                                 ZIW_LINE) else None)
                  (fz.bss()), "the story's first prompt", 600, poll=1.0)
    box.shot(os.path.join(out, "prompt.png"))
    print("megafrotz: story claim %04x (%s), %dKB" %
          (v["zf_sseg"], "a selector" if v["zf_sseg"] & 4 else "a segment",
           v["zf_skb"]))
    if halted(v):
        return fz.lines(v), "halted before its first prompt"
    for cmd in ("go", "text", "go"):
        n0 = len(fz.lines(v))
        box.type(cmd)
        box.key("Return")
        t0, seen = time.time(), [False]

        def back():
            v = fz.bss()
            if v is None:
                return None
            if v["zi_want"] & 0xFF == ZIW_NONE:
                seen[0] = True
            if halted(v) or (seen[0] and v["zi_want"] & 0xFF == ZIW_LINE
                             and len(fz.lines(v)) > n0):
                return v
            return None
        v = box.until(back, "%r back at the prompt" % cmd, 1800, poll=2.0)
        print("megafrotz:   %-4s %5.0fs  %s" % (cmd, time.time() - t0,
                                                fz.lines(v)[-2:-1]))
        box.shot(os.path.join(out, "%s.png" % cmd))
        if halted(v):
            return fz.lines(v), "halted on %r" % cmd
    return fz.lines(v), None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pkg", default=os.path.join(BUILD, "frotz.o88"))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for f in (a.pkg, STORY, GOLD, os.path.join(BUILD, "mega.img")):
        if not os.path.exists(f):
            print("megafrotz: no %s - `make mega build/frotz.o88 "
                  "build/zt/ZBIG.Z8`" % f)
            return 1
    out = os.path.join(BUILD, "bochs-megafrotz")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="megafrotz-")
    box, err, lines = None, None, []
    t0 = time.time()
    try:
        shutil.copy(a.pkg, os.path.join(tmp, "FROTZ.O88"))
        shutil.copy(STORY, os.path.join(tmp, "ZBIG.Z8"))
        disk = os.path.join(out, "zbig.img")
        subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                        "--size", "1440", os.path.join(tmp, "FROTZ.O88"),
                        os.path.join(tmp, "ZBIG.Z8")],
                       check=True, stdout=subprocess.DEVNULL)
        box = os88bochs.Box(out, [], bimg=disk, btype="1_44")
        if not box.wait_for("kern_mega: protected mode", 300):
            err = "kern_mega never reached protected mode"
        else:
            box.up()
            try:
                lines, err = play(box, Frotz(box), out)
            except RuntimeError as e:
                err = str(e)
        f = box.fault()
        if f:
            err = f
        closed = [l for l in box.lines() if "kern_mega CLOSED" in l]
        if closed and not err:
            err = closed[0]
    finally:
        if box:
            box.shot(os.path.join(out, "end.png"))
            box.stop()
        shutil.rmtree(tmp, ignore_errors=True)
    print("megafrotz: %.0fs under Bochs" % (time.time() - t0))
    got = [l for l in lines if l.startswith(("VERIFY", "GO", "TEXT"))]
    want = open(GOLD).read().split("\n")[:4]
    if not err and got != want:
        err = "transcript %r, want %r" % (got, want)
    if err:
        print("FAIL: " + err)
        print("megafrotz: the log and the shots are in %s" % out)
        return 1
    for l in got:
        print("megafrotz:   " + l)
    print("megafrotz: ok - kern_mega, protected mode, the model's transcript")
    if not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
