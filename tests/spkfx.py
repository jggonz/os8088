#!/usr/bin/env python3
"""THE SPEAKER SHAPER against its reference - apps/os88spkfx.inc against
tools/os88spkfx.py, to the byte, on MartyPC's 4.77 MHz 5150.

    make && python3 tests/spkfx.py [--rate R] [--samples N]

tests/spkfx/spkfx.asm is assembled into a scratch floppy and opened from
B:. The harness writes a signal into the package's input claim - a
deterministic piece of "music": tones that rise and fall, a noise burst, an
onset out of silence and a stretch of silence, so every level, the gate and
the carrier's slide are all reached - then runs the shaper over it in every
leg below and reads the output claim back:

  1. PRE_DIFF with the carrier's slide, spans of the rate's block, each span
     emitted in two pieces (a ring's wrap) - the Audio and Video shape;
  2. PRE_NONE with the slide - Tracker's;
  3. PRE_DIFF without it;
  4. an 11,025 Hz pass (a block of 256 still) and a 16,000 Hz one (512, a
     pulse of 74 counts - the shortest an 8088's door takes).
Each must equal the model's counts EXACTLY. The time of os88spkfx_emit, entry
to exit, and of os88spkfx_level is printed as cycles a sample - what the
shaper costs a producer on top of the pulses.

Broken on purpose - the pre-emphasis's `rcr` made a `shr` - it FAILS at the
first sample that differs and names it: in the copies that shift the carrier,
legs 1, 4 and 5; in the d = 0 copies, all four PRE_DIFF legs (each leg
reaches both bodies but 3, which reaches only the second).
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88geom as geom          # noqa: E402
import os88spkfx as fx                              # noqa: E402
from cycweb import pkg_syms                         # noqa: E402

MACHINE = "os8088_5150_cga_gla"
SRC = "tests/spkfx/spkfx.asm"


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def build(tmp):
    b = os.path.join(tmp, "spkfx.bin")
    o = os.path.join(tmp, "SPKFX.O88")
    img = os.path.join(tmp, "spkfx360.img")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-o", b,
                    SRC], check=True)
    subprocess.run([sys.executable, "tools/os88pkg.py", b, "-o", o],
                   check=True, capture_output=True)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", img, "--size",
                    "360", o], check=True, capture_output=True)
    return img


def signal(n, rate):
    """a deterministic stand-in for music: every level, the gate, an onset
    out of silence, and silence, in n samples"""
    out, seed = bytearray(), 8088
    for j in range(n):
        t = j / float(rate)
        seg = (j * 8) // n              # eight sections
        if seg in (3, 7):               # silence (the carrier put away)
            v = 0.0
        else:
            env = [1.0, 0.25, 0.05, 0, 0.8, 0.02, 0.5, 0][seg]
            v = env * (0.6 * math.sin(2 * math.pi * 110 * t) +
                       0.3 * math.sin(2 * math.pi * 880 * t * (1 + seg / 8.0))
                       + 0.1 * math.sin(2 * math.pi * 2500 * t))
            if seg == 6:                # a noise burst
                seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
                v += 0.4 * ((seed >> 16) / 32768.0 - 0.5)
        out.append(max(0, min(255, int(round(128 + 127 * v)))))
    return bytes(out)


def model(xs, rate, pre, idle, span, split, rat=0):
    s = fx.Shaper(rate, pre, idle)
    if rat:
        s.ratchet(rat - 1)
    out = []
    for j in range(0, len(xs), span):
        sp = xs[j:j + span]
        s.level(sp)
        if split and split < len(sp):
            out.append(s.emit(sp[:split]))
            out.append(s.emit(sp[split:]))
        else:
            out.append(s.emit(sp))
    return b"".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=24000)
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms(SRC)
    bad = []
    legs = [("PRE_DIFF, slide, in pieces", 8000, 1, 1, 100),
            ("PRE_NONE, slide", 8000, 0, 1, 0),
            ("PRE_DIFF, no slide", 8000, 1, 0, 0),
            ("11,025 Hz", 11025, 1, 1, 37),
            ("16,000 Hz", 16000, 1, 1, 300),
            ("PRE_NONE, ratchet from 10", 8000, 0, 1, 0, 11)]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        img = build(tmp)
        with os88ui.boot("build/os8088-360.img", apps=img,
                         machine=MACHINE) as ui:
            m = ui.m
            w = ui.path("B:/SPKFX.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rb = lambda n: m.read(base + syms[n], 1)[0]
            rw = lambda n: u16(m.read(base + syms[n], 2))
            os88marty.until(m, lambda mm: rb("fx_up") == 1, "the claims",
                            poll=0.2, limit=120.0, guest=20.0)
            iseg, oseg = rw("fx_iseg") << 4, rw("fx_oseg") << 4
            for k, leg in enumerate(legs):
                name, rate, pre, idle, split = leg[:5]
                rat = leg[5] if len(leg) > 5 else 0
                n = min(a.samples, 32768)
                xs = signal(n, rate)
                span = fx.block_for(rate)
                m.write(iseg, xs)
                m.write(oseg, bytes(n))
                for nm, v in (("fx_rate", rate), ("fx_len", n),
                              ("fx_span", span), ("fx_split", split)):
                    m.write(base + syms[nm], bytes([v & 255, v >> 8]))
                m.write(base + syms["fx_pre"], bytes([pre, idle, rat]))
                done = rb("fx_done")
                ent = base + syms["os88spkfx_emit"]
                ext = base + syms["os88spkfx_emit.end"]
                lev = base + syms["os88spkfx_level"]
                levx = base + syms["os88spkfx_level.h"]
                t = {"emit": [0, 0], "level": [0, 0], "at": None}

                def hit(mm, r):
                    if r["addr"] in (ent, lev):
                        t["at"] = (r["addr"], r["cycles"], mm.regs()["cx"])
                    elif t["at"]:
                        k_ = "emit" if t["at"][0] == ent else "level"
                        t[k_][0] += r["cycles"] - t["at"][1]
                        t[k_][1] += t["at"][2]
                        t["at"] = None
                timed = True
                if timed:
                    with os88marty.bp_trace(m, ent, ext, lev, levx,
                                            on_hit=hit) as tr:
                        m.type_text("g")
                        tr.until(lambda: rb("fx_done") > done, name,
                                 limit=900.0)
                else:
                    m.type_text("g")
                    os88marty.until(m, lambda mm: rb("fx_done") > done, name,
                                    poll=0.3, limit=600.0, guest=60.0)
                got = m.read(oseg, n)
                want = model(xs, rate, pre, idle, span, split, rat)
                j = next((i for i in range(n) if got[i] != want[i]), None)
                cyc = ""
                if timed and t["emit"][1]:
                    cyc = ", emit %.1f cycles a sample, level %.1f" % (
                        t["emit"][0] / float(t["emit"][1]),
                        t["level"][0] / float(t["emit"][1]))
                print("   %d: %-28s %d samples: %s%s" % (
                    k + 1, name, n, "EXACT" if j is None else
                    "differs at %d: got %s want %s" % (
                        j, list(got[j:j + 6]), list(want[j:j + 6])), cyc))
                if j is not None:
                    bad.append("%d: %s differs at sample %d" % (k + 1, name,
                                                                j))
    for b in bad:
        print("FAIL " + b)
    print("spkfx: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
