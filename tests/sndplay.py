#!/usr/bin/env python3
"""THE PWM CLIP, end to end - apps/os88pcm.inc on the OSAPI_SND_PLAY door,
SPEC.md 34.4, 34.3, 53.2.1.

    make && python3 tests/sndplay.py [--keep DIR]

tests/sndplay/sndplay.asm is assembled into a scratch 360KB floppy of its
own and opened from B: on MartyPC's CGA 5150, with the speaker captured
(MARTYPC_WAV) and every OUT to ports 40h, 42h and 43h traced. Key 'p' plays
on the desktop, key 'f' inside an FSXF_FASTTICK bracket. What must hold:

  1. the answers: a tone is granted; a rate of 1,000 and of 20,000 Hz are
     refused with AX = 2; the clip returns AX = 0, CF = 0; a tone AFTER it is
     GRANTED (only possible if the release left channel 2 free) and goes off;
     the bracket's clip returns 0 and the bracket itself CF = 0;
  2. THE PORTS ARE THE CLIP'S, in order: channel 2 to mode 0 (43h <- 90h),
     then exactly one 42h write a sample, each t[s] = 1 + s*(N-2)/255 at
     N = 149, then channel 2 back to mode-3 idle (43h <- B6h) - and inside
     the bracket, around that, the sub-tick parked (43h <- 34h, 40h <- 0, 0)
     and handed back (43h <- 34h, 40h <- 55h, 55h: 65536/3);
  3. the kernel is left as it was: snd_ch2mode and snd_pcm_busy 0, no tone
     owner, the speaker gate (61h bits 0-1) low; and in the bracket, right
     after the clip, [sch_fast] = 3 again;
  4. the speaker SOUNDED: the capture's level varies across each clip.

Broken on purpose - os88pcm_play's `out 0x42, al` taken out - 2 FAILS (no
pulses) and 4 FAILS (flat); spk_pcm_idle's release store taken out - 1
FAILS (the tone after the clip is refused) and 3 FAILS.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88geom as geom          # noqa: E402
import sndcheck                                     # noqa: E402
from cycweb import pkg_syms                         # noqa: E402

MACHINE = "os8088_5150_cga_gla"
SRC = "tests/sndplay/sndplay.asm"
N = 1193182 // 8000                     # 149
TAB = [1 + s * (N - 2) // 255 for s in range(256)]
SAMP = [(i * 37) & 0xFF for i in range(300)]
PWM = [TAB[s] for s in SAMP]


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def build(tmp):
    b = os.path.join(tmp, "sndplay.bin")
    o = os.path.join(tmp, "SNDPLAY.O88")
    img = os.path.join(tmp, "sndplay360.img")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-o", b,
                    SRC], check=True)
    subprocess.run([sys.executable, "tools/os88pkg.py", b, "-o", o],
                   check=True, capture_output=True)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", img, "--size",
                    "360", o], check=True, capture_output=True)
    return img


def outs(m, rec):
    """An io stop: (port, AL) if it was an OUT to 40h/42h/43h, else None.
    The stop is AFTER the instruction, so the `out imm8, al` is the two
    bytes behind IP"""
    r = m.regs()
    ip = (r["cs"] << 4) + r["ip"]
    for at in (ip - 2, ip):
        op = m.read(at, 2)
        if op[0] == 0xE6 and op[1] in (0x40, 0x42, 0x43):
            return (op[1], r["ax"] & 0xFF)
    return None


def ch2of(m):
    """snd_ch2mode and snd_pcm_busy, each by its own name - they are one
    word on some kernels and not on others"""
    return bytes([m.read(m.sym("snd_ch2mode"), 1)[0],
                  m.read(m.sym("snd_pcm_busy"), 1)[0]])


def phase(seq, fsx):
    """The expected OUT sequence of one clip, latches filtered out."""
    want = []
    if fsx:
        want += [(0x43, 0x34), (0x40, 0x00), (0x40, 0x00)]
    want += [(0x43, 0x90)] + [(0x42, c) for c in PWM] + [(0x43, 0xB6)]
    if fsx:
        want += [(0x43, 0x34), (0x40, 0x55), (0x40, 0x55)]
    return want


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", help="copy the speaker capture here")
    a = ap.parse_args()
    os.chdir(ROOT)
    bad = []
    syms, _ = pkg_syms(SRC)
    with tempfile.TemporaryDirectory() as tmp:
        img = build(tmp)
        cap = os.path.join(tmp, "cap")
        os.environ["MARTYPC_WAV"] = cap
        booted = os88ui.boot("build/os8088-360.img", apps=img,
                             machine=MACHINE)
        os.environ.pop("MARTYPC_WAV", None)
        with booted as ui:
            m = ui.m
            w = ui.path("B:/SNDPLAY.O88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            fmark = base + syms["sp_fmark"]
            k_run = base + syms["os88pcm_play.grant"]   # past the rate
                                        # check: where the kernel's clip
                                        # engine used to be entered
            ev = []                     # ("run", cyc) / ("fmark", ...) / port

            def hit(mm, r):
                if r["addr"] == k_run:
                    ev.append(("run", r["cycles"]))
                elif r["addr"] == fmark:
                    ev.append(("fmark", mm.read(mm.sym("sch_fast"), 1)[0],
                               ch2of(mm)))
                else:
                    o = outs(mm, r)
                    if o and o != (0x43, 0x00):     # a latch, not a program
                        ev.append((o, r["cycles"]))
            io = [{"type": "io", "addr": p} for p in (0x40, 0x42, 0x43)]
            rb = lambda: m.read(base + syms["sp_done"], 1)[0]
            with os88marty.bp_trace(m, k_run, fmark, *io, on_hit=hit) as tr:
                m.type_text("p")
                tr.until(lambda: rb() >= 1, "the desktop leg", limit=600.0)
                c_d = m.status()["cycles"]
                m.type_text("f")
                tr.until(lambda: rb() >= 2, "the bracket leg", limit=600.0)
            res = m.read(base + syms["sp_res"], 16)
            ch2 = ch2of(m)
            tact = m.read(m.sym("snd_town_act"), 1)[0]
            p61 = m.inb(0x61)
        caps = [os.path.join(tmp, f) for f in os.listdir(tmp)
                if f.startswith("cap.") and "speaker" in f]
        if a.keep and caps:
            shutil.copy(caps[0], a.keep)
        wav = sndcheck.load(caps[0]) if caps else None

    # --- 1: the answers ------------------------------------------------------
    r = [(res[2 * i], res[2 * i + 1]) for i in range(8)]
    names = ["tone", "rate 1000", "rate 20000", "clip", "tone after",
             "tone off", "clip in bracket", "bracket"]
    want = [None, (2, 1), (2, 1), (0, 0), None, None, (0, 0), None]
    for i, (nm, (al, cf)) in enumerate(zip(names, r)):
        ok = (cf == 0) if want[i] is None else ((al, cf) == want[i])
        print("   1: %-16s AL %3d CF %d  %s" % (nm, al, cf,
                                                 "ok" if ok else "WRONG"))
        if not ok:
            bad.append("1: %s answered AL %d CF %d" % (nm, al, cf))

    # --- 2: the ports, per clip ----------------------------------------------
    runs = [i for i, e in enumerate(ev) if e[0] == "run"]
    if len(runs) != 2:
        bad.append("2: the clip granted %d times, want 2" % len(runs))
    else:
        for k, (lo, hi, fsx) in enumerate([(runs[0], runs[1], False),
                                           (runs[1], len(ev), True)]):
            seg = [e[0] for e in ev[lo + 1:hi] if isinstance(e[0], tuple)]
            # from the grant: what follows the clip (the tone after
            # it, the bracket's own exit) is cut off at the expected length
            want = phase(None, fsx)
            got = seg[:len(want)]
            same = got == want
            n42 = sum(1 for p in got if p[0] == 0x42)
            print("   2: %s: %d OUTs traced, %d pulses, %s" % (
                "bracket" if fsx else "desktop", len(seg), n42,
                "THE CLIP'S, in order" if same else "DIFFERENT"))
            if not same:
                j = next((i for i, (x, y) in enumerate(zip(got, want))
                          if x != y), min(len(got), len(want)))
                bad.append("2: %s leg differs at OUT %d: got %s want %s" % (
                    "bracket" if fsx else "desktop", j,
                    got[j:j + 3], want[j:j + 3]))

    # --- 3: the kernel as it was ---------------------------------------------
    fm = [e for e in ev if e[0] == "fmark"]
    print("   3: after: snd_ch2mode/busy %02x %02x, tone owner %d, 61h bits "
          "%d; in the bracket after the clip [sch_fast] = %s" % (
              ch2[0], ch2[1], tact, p61 & 3,
              fm[0][1] if fm else "never read"))
    if ch2 != b"\0\0" or tact or p61 & 3:
        bad.append("3: the kernel was left changed")
    if not fm or fm[0][1] != 3 or fm[0][2] != b"\0\0":
        bad.append("3: the bracket's sub-tick or channel 2 after the clip")

    # --- 4: it sounded ---------------------------------------------------------
    if wav is None:
        bad.append("4: no speaker capture")
    else:
        hr, vals, _ = wav
        k = hr / 4772727.0
        # the desktop clip alone: the grant to the idle word, the
        # tones either side of it kept out of the window
        c0 = next((e[1] for e in ev if e[0] == "run"), None)
        c1 = next((e[1] for e in ev if e[0] == (0x43, 0xB6) and c0 is not
                   None and e[1] > c0), c_d)
        seg = vals[int(c0 * k):int(c1 * k)] if c0 is not None else []
        span = max(seg) - min(seg) if seg else 0.0
        print("   4: the speaker's level across the desktop clip varies by "
              "%.3f" % span)
        if span < 0.05:
            bad.append("4: the capture is flat")

    if bad:
        print("sndplay: FAIL")
        for b in bad:
            print("   " + b)
        return 1
    print("sndplay: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
