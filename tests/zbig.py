#!/usr/bin/env python3
"""Frotz plays a story past 64KB on the XT, and what it costs (KERN-MEGA-MEMORY-PLAN MC7).

    make build/frotz.o88 build/zt/ZBIG.Z5 && python3 tests/zbig.py
    python3 tests/zbig.py --pkg OLD/frotz.o88     the same, another build: the
                                                  A/B the cycle figures are for

Frotz walks a story by 32KB PAGE (apps/frotz/zmem.inc): a table of one segment
per page, OSAPI_SEG's SEG_SUB at load - a selector each on kern_mega, the
claim's segment plus the page on every real-mode kernel. It replaced 128-byte
granule arithmetic on the claim's segment, which a selector cannot do, and it
sits on the interpreter's hottest paths: zx_step's guard runs before every
instruction, zm_map on every byte read past 64KB, zm_pcaddr on every call.

THE STORY is tests/frotz/zbiggen.py's (152KB, v5): calls into every page,
loops whose bodies straddle the page boundaries (a backward @jump wraps the
offset, zx_jrel; straight-line code walks over one, zm_norm), stream 3 decodes
of strings in the top pages (zm_map) and an @verify of the whole file. The
generator models every statement, so the gold is ITS answer and needs no
reference interpreter: `go`, `text`, `go` must print exactly
build/zt/ZBIG5.GLD.

THE FIGURE. Each command is typed, the machine PAUSED, Enter injected, and
the guest then advanced STEP cycles at a time until the worker is back at
@aread having printed its line - so the count is the guest's own, from the
key to the next prompt, to within STEP, and nothing the host does moves it.
Run it with --pkg on the build before the change for the A/B: the UI's share
(the key, the echo, one line of text) is the same on both, so the difference
is the interpreter's.

VERIFIED RED: with zx_jrel's page step taken out (a backward branch that
wraps the offset then lands 32KB further on, in the middle of other code)
the first `go` never comes back to its prompt - "FAIL: 'go' never came back
to the prompt", after BUDGET guest seconds - three times `text`'s 80.
And the FROTZ.O88 from before the page table is green here, which is the
point of the A/B: on a real-mode kernel the granules were right, only
slower - the build that is red is kern_mega's (tests/megafrotz.py).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import editmove                                         # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88sym                                          # noqa: E402
import os88ui                                           # noqa: E402

S = os88sym.linear
STORY = os.path.join(ROOT, "build", "zt", "ZBIG.Z5")
GOLD = os.path.join(ROOT, "build", "zt", "ZBIG5.GLD")
STEP = 50000                    # guest cycles a poll: ~10 ms, the resolution
BUDGET = 240                    # guest seconds a command may take: `go` is
                                # 29.5 and `text` 79.8 on the XT
ZIW_NONE, ZIW_LINE = 0, 1
PROBES = ["zi_want", "zi_linen", "zw_sbseg", "zw_sbhead", "zw_sbcount",
          "zw_sbmax", "zf_err", "zf_dead"]
ZW_UPMAX, ZW_LSTRIDE = 12, 128


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


class Frotz:
    def __init__(self, ui, win):
        self.ui, self.m, self.wi = ui, ui.m, win.i
        p = editmove.pkg_syms(os.path.join(ROOT, "apps", "frotz", "frotz.asm"),
                              (os.path.join(ROOT, "apps") + "/",
                               os.path.join(ROOT, "apps", "frotz") + "/"),
                              PROBES + ["os88_image_end"])
        # RELATIVE to the image's end, which is the header's image size
        # (+8) and differs between two builds of the package; these names'
        # places in the bss do not
        self.rel = {n: p[n] - p["os88_image_end"] for n in PROBES}

    def seg(self):
        return u16(self.m.read(os88geom.winptr(self.m, self.wi, S)
                               + os88geom.W_SEG, 2))

    def at(self, name):
        seg = self.seg()
        return seg * 16 + u16(self.m.read(seg * 16 + 8, 2)) + self.rel[name]

    def b(self, name):
        return self.m.read(self.at(name), 1)[0]

    def w(self, name):
        return u16(self.m.read(self.at(name), 2))

    def lines(self):
        sb, head, cnt, mx = (self.w(n) for n in
                             ("zw_sbseg", "zw_sbhead", "zw_sbcount",
                              "zw_sbmax"))
        out = []
        for L in range(cnt):
            slot = (head - cnt + L) % mx
            off = sb * 16 + ZW_UPMAX * ZW_LSTRIDE + slot * ZW_LSTRIDE
            n = self.m.read(off, 1)[0]
            out.append(self.m.read(off + 2, n).decode("latin-1").strip())
        return out

    def halted(self):
        return self.b("zf_err") or self.b("zf_dead")


def run(pkg, verbose, story=STORY, goldf=GOLD):
    gold = open(goldf).read().split("\n")
    tmp = tempfile.mkdtemp(prefix="zbig-")
    try:
        shutil.copy(pkg, os.path.join(tmp, "FROTZ.O88"))
        shutil.copy(story, os.path.join(tmp, "ZBIG.Z5"))
        disk = os.path.join(tmp, "zbig.img")
        subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                        "--size", "360", os.path.join(tmp, "FROTZ.O88"),
                        os.path.join(tmp, "ZBIG.Z5")],
                       check=True, stdout=subprocess.DEVNULL)
        with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                         apps=disk, quick=True, verbose=verbose) as ui:
            m = ui.m
            ui.open_drive("B")
            win = ui.open("ZBIG.Z5")
            fz = Frotz(ui, win)
            # VERIFY runs before the first prompt; it walks all 152KB, a
            # page at a time
            os88marty.until(m, lambda _: fz.halted() or
                            fz.b("zi_want") == ZIW_LINE,
                            "the story's first prompt", limit=240)
            if fz.halted():
                return None, "halted before its first prompt: %r" % \
                    fz.lines()[-3:]
            got, cyc = [l for l in fz.lines() if l.startswith("VERIFY")], []
            for cmd in ("go", "text", "go"):
                m.type_text(cmd)
                os88marty.until(m, lambda _: fz.w("zi_linen") == len(cmd),
                                "%r echoed" % cmd, limit=30)
                n0 = len(fz.lines())
                m.pause()
                c0 = int(m.status()["cycles"])
                m.key("Enter")
                seen_none, spent = False, 0
                while True:
                    m.advance(cycles=STEP)
                    spent += STEP
                    want = fz.b("zi_want")
                    if want == ZIW_NONE:
                        seen_none = True
                    if fz.halted():
                        m.run()
                        return None, "halted on %r: %r" % (cmd,
                                                           fz.lines()[-3:])
                    if seen_none and want == ZIW_LINE:
                        break
                    if spent > 4770000 * BUDGET:
                        m.run()
                        return None, "%r never came back to the prompt" % cmd
                c1 = int(m.status()["cycles"])
                m.run()
                new = [l for l in fz.lines()[n0:]
                       if l.startswith(("GO", "TEXT"))]
                got += new
                cyc.append((cmd, c1 - c0))
                if verbose:
                    print("zbig:   %-4s %11d cycles  %s" % (cmd, c1 - c0,
                                                            new[-1:]))
            want = gold[:4]
            if got != want:
                return cyc, "transcript %r, want %r" % (got, want)
            return cyc, None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pkg", default=os.path.join(ROOT, "build", "frotz.o88"))
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--story", default=STORY,
                    help="another zbiggen.py story (v5), e.g. one under 64KB")
    ap.add_argument("--gold", default=GOLD, help="...and its --gold")
    a = ap.parse_args()
    for f in (a.pkg, a.story, a.gold):
        if not os.path.exists(f):
            print("zbig: no %s - `make build/frotz.o88 build/zt/ZBIG.Z5`" % f)
            return 1
    cyc, err = run(a.pkg, not a.quiet, a.story, a.gold)
    if err:
        print("FAIL: " + err)
        return 1
    tot = {}
    for cmd, c in cyc:
        tot.setdefault(cmd, []).append(c)
    for cmd, cs in tot.items():
        print("zbig: %-4s %s cycles (%.2f s on a 4.77 MHz 8088)"
              % (cmd, " ".join("%d" % c for c in cs), cs[0] / 4.77e6))
    print("zbig: ok - the transcript is the model's, to the line")
    return 0


if __name__ == "__main__":
    sys.exit(main())
