#!/usr/bin/env python3
"""os88vidprof - where a Video Player play's time goes, on MartyPC.

    python3 tools/os88vidprof.py CLIP.V88 [--machine NAME] [--hdd]
        [--secs S] [--fs] [--top N] [--define X]

AN INSTRUMENT, not a gate (docs/plans/VIDEO-PLAN.md 15.8: "measured before
redesigned"). It boots the named MartyPC machine with VIDEO.O88 and the clip -
on a scratch floppy, or with --hdd on a bootable XT-IDE VHD the way
tests/vidsound.py makes one - opens the clip, plays it full screen, and while
it plays SAMPLES THE GUEST'S IP over the debug link. The link's poll is
uncorrelated with anything the guest does, so the histogram is a profile
(tools/os88prof.py's method) and costs the guest nothing.

Every sample is attributed to:
  - a VIDEO.O88 label (the package's own map, locals included), rolled up
    by routine;
  - a kernel routine (tools/os88sym.py, every section at its own segment);
  - the ROM (F000:), or `other`.

Then the play's own counters: frames, stalls, late periods, the ticks it
took against the file's, the longest the hook was held off.
"""
import argparse
import bisect
import os
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty, os88ui, os88build, os88sym, os88geom as geom  # noqa: E402
import os88vid as vid                                           # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
HZ = 4772727.0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def pkg_map(defines=()):
    """{name: offset} for every VIDEO.O88 label, locals included"""
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(ROOT, "apps/video/video.asm")
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I",
                        os.path.join(ROOT, "apps/"), "-I",
                        os.path.join(ROOT, "apps/video/")]
                       + ["-D" + x for x in defines]
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def kern_table():
    """[(linear, name)] for every kernel label with a fixed segment"""
    off = os88sym.syms()
    out = []
    for n in off:
        try:
            out.append((os88sym.segment_of(n) * 16 + off[n], n))
        except (RuntimeError, KeyError):
            pass
    out.sort()
    return out


def routine(n):
    """A local label belongs to the routine above it (DISK-CPU-PLAN 1)"""
    return n.split(".")[0]


def decrec_call(image, syms):
    """The offset of vp_frame's `call vp_decrec` - the one a streamed frame
    takes, flipping and the seam aside"""
    o, tgt = syms["vp_frame.dec"], syms["vp_decrec"]
    while o < syms["vp_frame.flip"]:
        if image[o] == 0xE8 and (o + 3 + u16(image, o + 1)) & 0xFFFF == tgt:
            return o
        o += 1
    sys.exit("os88vidprof: no call of vp_decrec in vp_frame.dec")


FEAT = ("frame", "P1", "P2", "P3", "P4", "P5", "P6", "slice", "slice B",
        "run", "run B", "seg", "abs")


def features(rec, planar=False):
    """A record's constructs, in FEAT's order (tools/os88vid.py's
    cycles_of walks the same lists)"""
    f = [1.0] + [0.0] * (len(FEAT) - 1)
    mode = [False]

    def write(k, di, m):
        if mode[0]:
            f[12] += 1
        if k <= vid.L_P6:
            f[1 + k] += 1
        elif k in (vid.L_SLICE, vid.L_SLICEL):
            f[7] += 1
            f[8] += m
        else:
            f[9] += 1
            f[10] += m

    def seg(absolute):
        f[11] += 0 if absolute else 1
        mode[0] = absolute
    if not planar:
        vid.walk_lists(bytearray(65536), rec, vid.REC_HDR, write, seg)
    else:
        si = vid.REC_HDR
        while rec[si]:
            si = vid.walk_lists(bytearray(65536), rec, si + 1, write, seg)
    return f


def calibrate(r, cyc):
    """Fit FEAT's costs to the measured frames, and say how far the encoder's
    own model is off on them"""
    import numpy as np
    planar = r.g.planes > 1
    recs = [rec for rec, at, i in r.records()]
    fr = sorted(i for i in cyc if i < len(recs))
    n = len(fr)
    X = np.array([features(recs[i], planar) for i in fr])
    y = np.array([cyc[i] for i in fr], float)
    model = np.array([vid.cycles_of(recs[i], planar, r.g.layout)
                      for i in fr])
    period = HZ * r.spf / r.rate
    print("calibrated %d frames: measured mean %.0f cycles (%.1f%% of a "
          "period), max %.0f (%.1f%%)" % (n, y.mean(), 100 * y.mean() / period,
                                         y.max(), 100 * y.max() / period))
    print("   the encoder's model: mean %.0f, max %.0f - measured / model "
          "%.3f on the mean, %.3f..%.3f per frame"
          % (model.mean(), model.max(), y.mean() / model.mean(),
             (y / model).min(), (y / model).max()))
    used = [j for j in range(len(FEAT)) if X[:, j].any()]
    coef, *_ = np.linalg.lstsq(X[:, used], y, rcond=None)
    fit = X[:, used] @ coef
    print("   fitted (residual rms %.0f cycles, %.1f%% of the mean):"
          % (np.sqrt(((fit - y) ** 2).mean()),
             100 * np.sqrt(((fit - y) ** 2).mean()) / y.mean()))
    for j, c in zip(used, coef):
        print("      %-8s %8.1f   (x %.1f a frame)" % (FEAT[j], c,
                                                       X[:, j].mean()))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("v88")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--hdd", action="store_true",
                    help="boot a VHD holding the clip (a *_hdd_* machine)")
    ap.add_argument("--apps", default=None,
                    help="with a floppy: the scratch disk's size in KB")
    ap.add_argument("--secs", type=float, default=0,
                    help="guest seconds to sample (default: the whole play)")
    ap.add_argument("--fs", action="store_true",
                    help="F then Space, where Play would use the window")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--live", action="store_true",
                    help="play LIVE, on the desktop (98.3.10): a resident "
                         "Live file; give --secs, since it may repeat")
    ap.add_argument("--local", action="store_true",
                    help="bucket by local label, not by routine")
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--cal", action="store_true",
                    help="CALIBRATE instead of sampling: every frame's decode "
                         "timed to the cycle (vp_frame's call of vp_decrec) "
                         "and fitted to the encoder's cost model")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, image = pkg_map(a.define)
    ptab = sorted((v, k) for k, v in syms.items())
    ktab = kern_table()
    r = vid.Reader(a.v88)
    print("clip: %s, %d frames, %.3f fps, layout %d, %d bytes"
          % (a.v88, r.frames, r.rate / r.spf, r.g.layout,
             os.path.getsize(a.v88)))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        if a.hdd:
            vhd = os.path.join(tmp, "prof.vhd")
            subprocess.run(
                ["python3", "tools/os88hdd.py", "--template", TEMPLATE,
                 "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
                 "--vbr", os88build.at("build/boothd.bin"),
                 "--mbr", os88build.at("build/mbr.bin"),
                 "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
                 "--file", "HIBER.DRV=" + os88build.at("build/hiber.drv"),
                 "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
                 "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
                 "--file", "VIDEO.O88=" + os88build.at("build/video.o88"),
                 "--file", "CLIP.V88=" + a.v88], check=True,
                capture_output=True)
            m = os88marty.launch(None, machine=a.machine,
                                 extra=["--mount", "hd:0:" + vhd])
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            where = "C:/CLIP.V88"
            ctx = None
        else:
            size = a.apps or ("1440" if os.path.getsize(a.v88) > 340000
                              else "360")
            disk = os.path.join(tmp, "prof.img")
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                            "--size", size,
                            os88build.at("build/video.o88"),
                            a.v88],
                           check=True, capture_output=True)
            ctx = os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                              machine=a.machine)
            ui = ctx.__enter__()
            m = ui.m
            where = "B:/" + os.path.basename(a.v88).upper()
        try:
            w = ui.path(where)
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            seg = u16(rec, geom.W_SEG)
            base = seg << 4
            lo, hi = syms["vd_native"], syms["vd_n_runl.done"]
            if bytes(m.read(base + lo, hi - lo)) != image[lo:hi]:
                sys.exit("os88vidprof: the running VIDEO.O88 is not this "
                         "source's build - run `make`")

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            os88marty.until(m, lambda mm: rb("vp_loaded") == 1,
                            "the clip's header", poll=0.5, limit=300.0,
                            guest=30.0)
            if rb("vp_ok") != 1:
                sys.exit("os88vidprof: the player will not play the clip")
            if not a.live:
                m.write(base + syms["vp_nowin"], b"\1")
            m.write(base + syms["vp_played"], b"\0")
            if a.cal:
                at = decrec_call(image, syms)
                # ...and the kernel's far call of the hook (sch_rhook), so a
                # hook call's cycles are split into the decode and the rest
                kh = os88sym.linear("sch_rhook")
                code = bytes(m.read(kh, 64))
                j = code.find(b"\x2e\xff\x5c")     # [cs:si+disp8]
                hk = [] if j < 0 else [kh + j, kh + j + 4]

                def done(mm, h):            # the frame, off the guest
                    return rw("vp_done")
                with os88marty.bp_trace(m, base + at, base + at + 3, *hk,
                                        cap=200000, on_hit=done) as tr:
                    m.type_text("p")
                    tr.until(lambda: rb("vp_played") == 1, "the play to end",
                             limit=3600)
                cyc = {}
                for h0, h1 in zip(tr.hits, tr.hits[1:]):
                    if h0["addr"] == base + at and h1["addr"] == base + at + 3:
                        cyc[h0["hit"]] = h1["cycles"] - h0["cycles"]
                if hk:
                    calls, dec, t0, nf, busy = [], 0, None, 0, 0
                    for h0, h1 in zip(tr.hits, tr.hits[1:]):
                        if h0["addr"] == hk[0]:
                            t0, dec, nf = h0["cycles"], 0, 0
                        if h0["addr"] == base + at and \
                                h1["addr"] == base + at + 3:
                            dec += h1["cycles"] - h0["cycles"]
                            nf += 1
                        if h1["addr"] == hk[1] and t0 is not None:
                            calls.append((h1["cycles"] - t0, dec, nf))
                            t0 = None
                    work = [c for c in calls if c[2]]
                    idle = [c for c in calls if not c[2]]
                    if work:
                        ov = sum(c[0] - c[1] for c in work)
                        print("hook: %d calls drew %d frames; outside the "
                              "decode %.0f cycles a call that drew, %.0f a "
                              "frame; %d calls drew none at %.0f cycles each"
                              % (len(calls), sum(c[2] for c in work),
                                 ov / len(work),
                                 ov / sum(c[2] for c in work), len(idle),
                                 sum(c[0] for c in idle) / max(1, len(idle))))
                return calibrate(r, cyc)
            m.type_text("p")
            run = "vp_lrun" if a.live else "vp_ready"
            os88marty.until(m, lambda mm: rb(run) == 1,
                            "the play to start", poll=0.05, limit=300.0,
                            guest=120.0)
            print("playing: shadow %d, sound %d, ring K=%d"
                  % (rb("vp_shadow"), rb("vp_snd"), rw("vp_k")))
            c0 = int(m.status()["cycles"])
            hits, n, k = {}, 0, 0
            t0 = time.time()
            m.pause()
            while True:
                st = m.advance(cycles=1)    # one batch: ~1,193 cycles
                ip = st["flat_ip"]
                wt = int(st["advanced_cycles"])
                n += wt
                if base <= ip < base + len(image):
                    i = bisect.bisect_right(ptab, (ip - base, "\xff")) - 1
                    nm = ptab[i][1] if i >= 0 else "?"
                    key = "pkg " + (nm if a.local else routine(nm))
                elif ip >= 0xF0000:
                    key = "ROM %04X" % ((ip - 0xF0000) & 0xFF00)
                else:
                    i = bisect.bisect_right(ktab, (ip, "\xff")) - 1
                    if i >= 0 and ip - ktab[i][0] < 0x2000:
                        key = "krn " + routine(ktab[i][1])
                    else:
                        key = "other %05X" % (ip & 0xFFF00)
                hits[key] = hits.get(key, 0) + wt
                k += 1
                if k % 64 == 0:
                    if rb(run) == 0:
                        break
                    if a.secs and (int(st["cycles"]) - c0) / HZ > a.secs:
                        break
            c1 = int(m.status()["cycles"])
            m.run()
            wall = time.time() - t0
            if not a.live:
                os88marty.until(m, lambda mm: rb("vp_played") == 1,
                                "the bracket to return", poll=0.5,
                                limit=300.0, guest=60.0)
            stats = dict((k, rw(k)) for k in (
                "vp_done", "vp_stall", "vp_late", "vp_dt", "vp_gap"))
        finally:
            if ctx:
                ctx.__exit__(None, None, None)
            else:
                m.close()
    gs = (c1 - c0) / HZ
    print("sampled %.1f guest s in batches (%.0f host s)" % (gs, wall))
    print("play: drew %(vp_done)d, stalls %(vp_stall)d, late %(vp_late)d, "
          "%(vp_dt)d ticks, hook held off at most %(vp_gap)d periods" % stats)
    print("   want %.1f ticks" % (r.frames * r.spf / r.rate * 1193182 / 65536))
    groups = {}
    for k, v in hits.items():
        g = k.split()[0]
        groups[g] = groups.get(g, 0) + v
    print("   " + "   ".join("%s %.1f%%" % (g, 100.0 * v / n)
                             for g, v in sorted(groups.items())))
    for k, v in sorted(hits.items(), key=lambda x: -x[1])[:a.top]:
        print("   %5.1f%%  %s" % (100.0 * v / n, k))
    return 0


if __name__ == "__main__":
    sys.exit(main())
