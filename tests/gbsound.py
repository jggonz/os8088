#!/usr/bin/env python3
"""REALDMG.O88's speaker, against a ROM whose sound is known.

    python3 tests/gbsound.py

docs/REALDMG-SPEC.md section 17 and docs/plans/REALDMG-PLAN.md wave 5's
sound row. tools/gbours.py builds GBTONE.GB (tools/gbprof/gbtone.asm, ours):
square channel 1 holds 440 Hz for 60 frames, its DAC is switched off for 30,
then channel 2 plays 880 Hz with a length of 64 - a quarter of a second -
and stops by itself. This boots os8088 on MartyPC's VGA XT with the package
and the ROM on B:, opens the ROM, and captures the speaker (MARTYPC_WAV);
the capture must hold

  * a steady ~440 Hz tone (the trigger, and the louder channel chosen);
  * then a steady ~880 Hz one that ENDS while the capture is still running
    (the length counter), with silence between the two (the DAC off);
  * and nothing after it.

Durations are not asserted tightly: the 8088 interprets, and the game may run
at less than its own speed. Breaking the length counter (rdg_sqframe never
counting down) leaves the 880 Hz tone running to the end of the capture, and
the row fails naming that.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import sndcheck  # noqa: E402

MACHINE = "os8088_xt_vga_720b"


def runs(path, win=0.02, tol=0.025):
    """[(t0, t1, hz)] of the steady tones in a speaker capture: the rising
    edges are the pitch, a window's worth at a time, and neighbouring windows
    within `tol` of each other are one tone (tests/excitebike_audio.py's
    reader, which this follows)."""
    rate, mono, dur = sndcheck.load(path)
    peak = max((abs(v) for v in mono), default=0)
    if peak < 0.02:
        return [], dur
    hi = 0.5 * peak
    edges, state = [], False
    for i, v in enumerate(mono):
        if not state and v > hi:
            state = True
            edges.append(i)
        elif state and v <= hi:
            state = False
    w = int(rate * win)
    slots = {}
    for e in edges:
        slots.setdefault(e // w, []).append(e)
    est = {}
    for k, es in slots.items():
        if len(es) >= 3:
            est[k] = (rate * (len(es) - 1) / float(es[-1] - es[0]), es[0] / float(rate),
                      es[-1] / float(rate))
    out, grp, last = [], [], None

    def close():
        if len(grp) >= 3:
            fs = sorted(g[0] for g in grp)
            out.append((grp[0][1], grp[-1][2], fs[len(fs) // 2]))
    for k in sorted(est):
        f = est[k][0]
        if grp and (k != last + 1 or abs(f - sorted(g[0] for g in grp)[len(grp) // 2]) > tol * f):
            close()
            grp = []
        grp.append(est[k])
        last = k
    close()
    return out, dur


def main():
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py")],
                                  text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="gbsound-")
    gb = os.path.join(tmp, "GBTONE.GB")
    with open(rom, "rb") as i, open(gb, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720", os.path.join(ROOT, "build", "realdmg.o88"), gb],
                          stdout=subprocess.DEVNULL)
    cap = os.path.join(tmp, "audio")
    os.environ["MARTYPC_WAV"] = cap
    try:
        with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                         machine=MACHINE) as ui:
            os.environ.pop("MARTYPC_WAV", None)
            M.guest_sleep(ui.m, 2)          # the boot's own beep, behind us
            ui.path("B:/GBTONE.GB")
            M.guest_sleep(ui.m, 8)
    finally:
        os.environ.pop("MARTYPC_WAV", None)
    wav = [os.path.join(tmp, f) for f in sorted(os.listdir(tmp))
           if f.startswith("audio") and "pc_speaker" in f]
    if not wav:
        print("FAIL gbsound: no speaker capture in %s" % sorted(os.listdir(tmp)))
        return 1
    tones, dur = runs(wav[0])
    print("gbsound: %.1f s captured; tones: %s" % (
        dur, ", ".join("%.0f Hz %.2f-%.2f s" % (h, a, b) for a, b, h in tones) or "none"))
    fails = []
    a = [t for t in tones if abs(t[2] - 440) < 440 * 0.03]
    t0 = a[0][0] if a else 0.0          # (the BIOS's boot beep is before it)
    b = [t for t in tones if abs(t[2] - 880) < 880 * 0.03 and t[0] > t0]
    if not a or a[0][1] - a[0][0] < 0.4:
        fails.append("no steady 440 Hz tone (channel 1, triggered)")
    if not b:
        fails.append("no 880 Hz tone (channel 2)")
    elif a and b[0][0] - a[0][1] < 0.15:
        fails.append("no silence between 440 and 880 Hz (channel 1's DAC off)")
    if b and dur - b[-1][1] < 1.0:
        fails.append("the 880 Hz tone runs to the end of the capture (its length counter)")
    others = [t for t in tones if t not in a and t not in b and t[0] > t0]
    if others:
        fails.append("tones that are neither: %s" % others)
    for f in fails:
        print("FAIL gbsound: " + f)
    print("gbsound: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
