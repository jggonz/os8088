#!/usr/bin/env python3
"""os88vidpage - PAGE-AHEAD for the Video Player, measured before it is built.

The question (docs/reports/VIDEO-PAGEAHEAD-2026-10-13.md): on a screen
with a second page, could the player draw in place as an unflipped play
does, spend what each frame's budgets leave on building a FUTURE frame -
one the encoder knows will be cut, a scene change - on the hidden page,
and flip to it when it is due, so that frame is shown whole for free?

It only can if there is spare capacity, so this answers that first and then
the gain, and it answers both with the encoder's own model rather than a
second one: it runs tools/os88venc.py's encode TWICE.

  1. BASELINE: the encode as it is, with every frame's budgets logged - its
     cycles against the per-frame ceiling, the CPU and disk buckets before
     and after, what each overflowed (capacity nobody could use), and why a
     cut frame was cut. The targets are kept.
  2. PAGE-AHEAD: the same encode, the same targets, but after each frame's
     record a second encoder (the hidden page) is given what the frame left
     - the CPU to the per-frame PEAK (never the owed time), the disk bucket
     above `--keep` of its depth, and the super-packet's room - and spends
     it on the next flip target, which is the next frame the baseline cut
     that the frame before it did not (a cut RUN's first). At that frame,
     if the hidden page is nearer the target than the shown one, the pages
     swap and the frame's own record is drawn on the new one. The hidden
     page's work is charged to the same buckets as the frame's own - its
     cycles to the CPU and through a disk curve to the reader, its bytes to
     the disk - so a pre-build that starves a later frame shows up as that
     frame's cut.

Nothing is written but a JSON of the two runs and a summary: the file the
second run writes leaves the pre-build out (no player reads one) and is
thrown away.

    python3 tools/os88vidpage.py SRC [os88venc options] [--keep F]
        [--json OUT.json] [--label NAME] [--from-v88 FILE.V88]

`--from-v88` takes a .V88's stored options (98.2.17) as the encode's, so the
owner's own file is re-run as it was made; options after it override."""

import argparse
import hashlib
import json
import math
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np                      # noqa: E402
import os88venc as V                    # noqa: E402
import os88vid as vid                   # noqa: E402

HORIZON = 900           # frames the disk forecast looks ahead (30 s at 30)
STATE = ("surf", "sv", "pl", "screen", "age")


def _swap(a, b):
    """The two pages' contents swapped between encoders a and b"""
    for k in STATE:
        if hasattr(a, k) and hasattr(b, k):
            x, y = getattr(a, k), getattr(b, k)
            setattr(a, k, y)
            setattr(b, k, x)
    a.scache = b.scache = None


def _copy_state(dst, src):
    for k in ("surf", "pl", "screen", "age"):
        if hasattr(src, k):
            v = getattr(src, k)
            setattr(dst, k, bytearray(v) if isinstance(v, bytearray)
                    else v.copy())
    if hasattr(dst, "sv") and dst.screen.shape == dst.idx.shape:
        dst.sv = np.frombuffer(dst.surf, dtype=np.uint8)
        dst.screen = dst.sv[dst.idx]
    dst.scache = None


SETTINGS = ("live", "spk", "vis", "cut", "bands", "thr", "blur_r",
            "dmode", "ppb", "dpal", "pal", "wm3", "lcopy", "ct", "csub",
            "cwm3")


def probe_class(base, ctl):
    """`base` (an os88venc encoder class) with the log and, when
    ctl["page"], the hidden page"""

    class Probe(base):
        def __init__(self, *args):
            super().__init__(*args)
            self._args = args
            self.pg_j = 0               # the main loop's frame: charge()s
            self.pg_off = False         # (set on preroll's copy)
            self.pg_why = None
            self.pg_c = self.pg_b = 0.0
            self.hid = None
            self.pg_F = None
            self.pg_flips = []
            self.pg_lost = 0            # flip targets reached unflipped
            self.pg_log = []
            self.pg_pals = []           # the palette each frame drew in

        # -- the log --------------------------------------------------------
        def why(self, tc, tb, er, eb, cyc_room):
            before = dict(self.stats)
            super().why(tc, tb, er, eb, cyc_room)
            for k in ("disk", "cpu", "peak", "cap"):
                if self.stats[k] != before[k]:
                    self.pg_why = k
            self.pg_need = (float(tc), float(tb), float(er), float(eb))

        def begin(self):
            lv = (self.cpu.level, self.disk.level)
            super().begin()
            if self.pg_off:
                return
            # what the refill could not hold: capacity nobody can use
            ov_c = 0.0 if self.cpu.per is None else max(
                0.0, lv[0] + self.cpu.per - self.cpu.cap)
            ov_d = 0.0
            if self.disk.per is not None and not self.dcurve:
                ov_d = max(0.0, lv[1] + self.disk.per - self.disk.cap)
            self.pg_pre = dict(cpu=self.cpu.level, disk=self.disk.level,
                               ceil=self.ceil, ovc=ov_c, ovd=ov_d,
                               ref=0.0 if self.disk.per is None or
                               self.dcurve else float(self.disk.per),
                               floor=float(self.disk_floor())
                               if self.disk.per is not None else 0.0)

        def end(self, c):
            lv = self.disk.level
            super().end(c)
            if self.dcurve and not self.pg_off:
                share = (c + self.audio_cyc + V.HOOK_CYC) / self.q + self.spk
                per = ((self._drate() if self.bank else self.drate) *
                       self.disk_rel(share) * 0.99 - self.abps) / self.fps
                self.pg_pre["ovd"] = max(0.0, lv + per - self.disk.cap)
                self.pg_pre["ref"] = float(per)

        # -- the hidden page --------------------------------------------------
        def _hidden(self):
            h = base(*self._args)
            for n in SETTINGS:
                if hasattr(self, n):
                    setattr(h, n, getattr(self, n))
            h.look, h.future, h.metric = 0, [], False
            h.owe, h.dcurve, h.bank, h.alead, h.slead = None, None, 0, 0, 0
            h.dfloor = 0.0
            if hasattr(h, "fgrid"):
                h.fgrid = None
            _copy_state(h, self)
            return h

        def _next_target(self, j):
            for F in ctl["targets"]:
                if F > j + 1:
                    return F
            return None

        def _npix(self, e, T):
            return int(np.count_nonzero(e.screen != T))

        def frame(self, target, audio=b""):
            if self.pg_off or not ctl.get("page"):
                return self._frame(target, audio)
            j = self.pg_j
            T = ctl["T"]
            if j < len(T) and not np.array_equal(T[j], target):
                raise SystemExit("os88vidpage: frame %d's target differs "
                                 "from the baseline's" % j)
            if self.hid is None:
                self.hid = self._hidden()
                self.pg_F = self._next_target(j - 1)
            flipped = False
            if self.pg_F is not None and j >= self.pg_F:
                if j == self.pg_F:
                    nh, nv = self._npix(self.hid, target), \
                        self._npix(self, target)
                    if nh < nv:
                        _swap(self, self.hid)
                        flipped = True
                        self.pg_flips.append((j, nv, nh))
                    else:
                        self.pg_lost += 1
                self.pg_F = self._next_target(j)
            ops, rec = self._frame(target, audio)
            self.pg_c = self.pg_b = 0.0
            if not flipped and self.pg_F is not None:
                self._prebuild(rec, audio)
            return ops, rec

        def _frame(self, target, audio):
            self.pg_why = None
            self.pg_need = None
            self.pg_room = (min(self.cpu.room(), self.ceil),
                            max(0.0, self.disk.room() - self.disk_floor()))
            return super().frame(target, audio)

        def _safe_bytes(self, spent):
            """What the disk bucket can give the hidden page at this frame
            and no later frame miss. Forecast on the baseline's own frames -
            their records and their refills - from the level this frame
            leaves, over HORIZON frames: a withdrawal X lowers every later
            level by X until an overflow absorbs it, so X may be no more
            than any frame's slack plus what has overflowed before it. A
            frame the baseline cut for the disk has no slack, so the first
            one ahead bounds X by the overflow before it - and a deep XMS
            bank, which never overflows, gives what it can spare"""
            L, j = ctl["log"], self.pg_j
            curve = bool(self.dcurve)
            cap = self.disk.cap
            lvl = self.disk.level - spent
            X, O = math.inf, 0.0
            if curve:                   # (this frame's refill: at its end)
                r = L[j]["ref"]
                O += max(0.0, lvl + r - cap)
                lvl = min(cap, lvl + r)
            for k in range(j + 1, min(len(L), j + 1 + HORIZON)):
                r = L[k]
                if not curve:
                    O += max(0.0, lvl + r["ref"] - cap)
                    lvl = min(cap, lvl + r["ref"])
                X = min(X, max(0.0, lvl - r["floor"] - r["b"]) + O)
                if r["why"] == "disk" or X <= 0:
                    break
                lvl -= r["b"]
                if curve:
                    O += max(0.0, lvl + r["ref"] - cap)
                    lvl = min(cap, lvl + r["ref"])
            if X == math.inf:
                X = max(0.0, lvl - self.disk_floor())
            return max(0.0, X * ctl["share"])

        def _prebuild(self, rec, audio):
            """Spend what this frame left on the hidden page, toward the
            flip target: the CPU to the PEAK (owed time is the next frame's,
            not ours), the disk bucket above --keep of its depth and
            above the floor, and the super-packet's room"""
            if self.pg_why is not None:     # (a cut frame left nothing)
                return
            c = self.cost(rec)
            ab = len(audio)
            # the frame's own ceiling as the schedule set it (a frame after
            # one that ran into owed time has less, 98.2.1.1), and never
            # past the peak: owed time is for the frame, not the page
            cpu_left = min(self.cpu.room(), self.ceil, self.peak) - c
            if self.cpu.per is not None and self.cpu.per < self.peak:
                cpu_left = min(cpu_left, self.cpu.room() - c -
                               ctl["keep"] * self.cpu.cap)
            dsk_left = self.disk.room() - self.disk_floor() - \
                (len(rec) - ab)
            if self.disk.per is not None:
                dsk_left = min(dsk_left, self._safe_bytes(len(rec) - ab))
            dsk_left = min(dsk_left, self.rec_max - len(rec))
            if self.dcurve and self.disk.per is not None:
                # A DISK THAT SLOWS UNDER LOAD: the hidden page's cycles are
                # the reader's too, so they cost this frame's refill - out
                # of the same safe bytes. Half of them at most, the CPU cut
                # until its refill loss fits
                def loss(cc):
                    r0 = self.disk_rel((c + self.audio_cyc + V.HOOK_CYC) /
                                       self.q + self.spk)
                    r1 = self.disk_rel((c + cc + self.audio_cyc +
                                        V.HOOK_CYC) / self.q + self.spk)
                    rate = self._drate() if self.bank else self.drate
                    return max(0.0, (r0 - r1) * rate * 0.99 / self.fps)
                lo, hi = 0.0, max(0.0, cpu_left)
                if loss(hi) > dsk_left / 2:
                    for _ in range(20):
                        mid = (lo + hi) / 2
                        if loss(mid) > dsk_left / 2:
                            hi = mid
                        else:
                            lo = mid
                    cpu_left = lo
                dsk_left -= loss(cpu_left)
            if cpu_left < 4 * self.ct[0] or dsk_left < 256:
                return
            T = ctl["T"][self.pg_F]
            h = self.hid
            if ctl.get("pals"):
                p = ctl["pals"][self.pg_F]
                if p is not None and (h.pal is None or
                                      not np.array_equal(h.pal, p)):
                    h.pal = h.dpal = p
                    h.far = None
            if not np.any(h.screen != T):
                return
            h.peak = h.ceil = cpu_left
            h.cpu = V.Budget(None, None)
            h.disk = V.Budget(None, None)
            h.rec_max = int(dsk_left)
            ops, prec = h.frame(T, b"")
            self.pg_c = float(h.cost(prec)) if ops else 0.0
            self.pg_b = float(len(prec)) if ops else 0.0

        # -- the charge ------------------------------------------------------
        def charge(self, rec, abytes):
            if self.pg_off:
                return super().charge(rec, abytes)
            if getattr(self, "flip", False):
                raise SystemExit("os88vidpage: a --flip file flips every "
                                 "frame already; page-ahead is for an "
                                 "unflipped play")
            c = self.cost(rec)
            self.lean(len(rec) - abytes)
            self.cpu.spend(c + self.pg_c)
            self.disk.spend(len(rec) - abytes + self.pg_b)
            self.end(c + self.pg_c)
            self.pg_pals.append(getattr(self, "pal", None))
            self.pg_log.append(dict(
                j=self.pg_j, c=float(c), b=len(rec) - abytes,
                pc=self.pg_c, pb=self.pg_b, why=self.pg_why,
                need=self.pg_need, room=self.pg_room,
                peak=float(self.peak), cpucap=self.cpu.cap,
                diskcap=self.disk.cap, **self.pg_pre))
            self.pg_j += 1
            self.pg_c = self.pg_b = 0.0
            return c

    Probe.__name__ = "Probe" + base.__name__
    return Probe


class Kept(object):
    """What the report reads off a finished encoder, picklable"""

    def __init__(self, e):
        self.pg_log, self.pg_pals = e.pg_log, e.pg_pals
        self.stats = dict(e.stats)
        self.q_frames, self.q_bad, self.q_wrong = \
            e.q_frames, e.q_bad, e.q_wrong
        self.pg_flips, self.pg_lost = e.pg_flips, e.pg_lost
        self.fps = getattr(e, "fps", None) or vid.HZ / e.period


def run(a, ctl, keep=None):
    """One encode of `a` under the probe: (encoder, wall seconds)"""
    made = []
    orig = (V.Encoder, V.EncoderP, V.EncoderX, V.preroll)
    classes = {}
    for b in orig[:3]:
        classes[b] = probe_class(b, ctl)

    def factory(b):
        def mk(*args):
            e = classes[b](*args)
            made.append(e)
            return e
        return mk

    def preroll(enc, *x, **k):
        enc.pg_off = True
        try:
            return orig[3](enc, *x, **k)
        finally:
            enc.pg_off = False

    V.Encoder, V.EncoderP, V.EncoderX = (factory(b) for b in orig[:3])
    # isinstance/type() tests inside os88venc name the real classes
    V.preroll = preroll
    vv = vid.verify_v88
    if ctl.get("page"):                 # (the file leaves the pre-build out:
        vid.verify_v88 = lambda *x, **k: None   # its keys are not its frames)
    t0 = time.time()
    try:
        # (palettes a frame: the scene's, read off the encoder each charge)
        V.encode(a, keep=keep)
    finally:
        V.Encoder, V.EncoderP, V.EncoderX, V.preroll = orig
        vid.verify_v88 = vv
    return made[0], time.time() - t0


def summary(e, fps, label):
    L = e.pg_log
    n = len(L)
    st = e.stats
    cyc = sum(r["c"] for r in L)
    pk = sum(r["peak"] for r in L)
    head = sum(max(0.0, r["peak"] - r["c"] - r["pc"]) for r in L)
    ovc = sum(r["ovc"] for r in L)
    ovd = sum(r["ovd"] for r in L)
    byts = sum(r["b"] for r in L)
    q = V.picture_report(e.q_frames, e.q_bad, fps) if e.q_frames else None
    return dict(
        label=label, frames=n, exact=st["exact"], cut=st["cut"],
        cut_disk=st["disk"], cut_cpu=st["cpu"], cut_peak=st["peak"],
        cut_cap=st["cap"], bytes_left=st["bytes_left"],
        owed=st.get("owed", 0), held=st.get("held", 0),
        cycles=cyc, peak_cycles=pk, head_cycles=head,
        cpu_overflow=ovc, disk_overflow=ovd, stream_bytes=byts,
        prebuild_cycles=sum(r["pc"] for r in L),
        prebuild_bytes=sum(r["pb"] for r in L),
        q_mean=q[0] if q else None, q_worst=q[1] if q else None,
        broken=q[3] if q else None,
        wrong=e.q_wrong / max(1, len(e.q_frames)) if e.q_frames else None,
        flips=len(e.pg_flips), lost=e.pg_lost)


def runs_of(log):
    """The baseline's cut runs: (first, last) frame of each"""
    out, s = [], None
    for r in log:
        cut = r["why"] is not None
        if cut and s is None:
            s = r["j"]
        if not cut and s is not None:
            out.append((s, r["j"] - 1))
            s = None
    if s is not None:
        out.append((s, log[-1]["j"]))
    return out


def capacity(log):
    """Per cut run: the spare the frames since the last run left - the
    CPU under the per-frame peak, and what the buckets overflowed - against
    what the run's first frame needed past its room"""
    rows, last = [], -1
    for s, t in runs_of(log):
        win = [r for r in log if last < r["j"] < s]
        head = sum(max(0.0, r["peak"] - r["c"]) for r in win)
        ovd = sum(r["ovd"] for r in win)
        r0 = log[s]
        tc, tb, er, eb = r0["need"] or (0, 0, 0, 0)
        rows.append(dict(start=s, end=t, gap=len(win), why=r0["why"],
                         need_c=max(0.0, tc - er), need_b=max(0.0, tb - eb),
                         full_c=tc, full_b=tb, head_c=head, ovd=ovd))
        last = t
    return rows


def opts_from_v88(path):
    r = vid.Reader(path)
    d = r.options()
    if not d:
        raise SystemExit("os88vidpage: %s stores no options" % path)
    return d


def namespace(argv):
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--keep", type=float, default=0.0,
                    help="CPU bucket share the pre-build leaves (a profile "
                    "whose average is under its peak)")
    ap.add_argument("--share", type=float, default=1.0,
                    help="of the disk bytes forecast to overflow, the share "
                    "the pre-build may take")
    ap.add_argument("--json")
    ap.add_argument("--label")
    ap.add_argument("--from-v88")
    ap.add_argument("--cache", help="the baseline kept here (a pickle), "
                    "and read back on a later run of the same encode")
    ap.add_argument("--targets", choices=("starts", "all"),
                    default="starts")
    mine, rest = ap.parse_known_args(argv)
    p = V.parser()
    src = rest[0]
    out = os.path.join(tempfile.mkdtemp(prefix="vidpage"), "out.v88")
    a = p.parse_args([src, out])
    if mine.from_v88:
        d = opts_from_v88(mine.from_v88)
        for k, v in d["o"].items():
            if hasattr(a, k):
                setattr(a, k, v)
        for k in d.get("auto", []):
            if hasattr(a, k):
                setattr(a, k, "auto")
        if d["o"].get("memory") is not None:
            a.reserve = None            # (stored, but --memory's to derive)
    if len(rest) > 1:
        # the command line's own options, over the stored ones: a parser
        # whose every default is SUPPRESS names only what was given
        q = V.parser()
        for act in q._actions:
            act.default = argparse.SUPPRESS
        for k, v in vars(q.parse_args([src, out] + rest[1:])).items():
            if k not in ("src", "out"):
                setattr(a, k, v)
    a.flip = False
    a.page_ahead = "off"            # (its own pass is the page; 98.2.1.8.1)
    a.preview_png = None
    a.progress = False
    a.src, a.out = src, out
    a.quiet = True
    if getattr(a, "layer_disk", None) or getattr(a, "layer_profile", None):
        raise SystemExit("os88vidpage: a layered file is not modelled")
    return mine, a


def table(paths):
    """`os88vidpage.py table A.json B.json ...`: the runs side by side, as
    the report's markdown"""
    print("| run | frames | cut runs | CPU used / headroom (of peak) | "
          "disk overflow | cut: base -> page | seen %: base -> page | "
          "worst s %: base -> page | broken s: base -> page | flips |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for p in paths:
        r = json.load(open(p))
        b, q, fps = r["baseline"], r["page"], r["fps"]
        print("| %s | %d | %d | %.0f%% / %.0f%% | %s | %d -> **%d** | "
              "%.3f -> **%.3f** | %.2f -> %.2f | %.1f -> %.1f | %d of %d |"
              % (r["label"], b["frames"], len(r["runs"]),
                 100 * b["cycles"] / max(1, b["peak_cycles"]),
                 100 * b["head_cycles"] / max(1, b["peak_cycles"]),
                 "%d KB of %d" % (b["disk_overflow"] / 1024,
                                  b["stream_bytes"] / 1024),
                 b["cut"], q["cut"], 100 * b["q_mean"], 100 * q["q_mean"],
                 100 * b["q_worst"], 100 * q["q_worst"],
                 b["broken"] / fps, q["broken"] / fps,
                 q["flips"], q["flips"] + q["lost"]))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["table"]:
        return table(argv[1:])
    mine, a = namespace(argv)
    label = mine.label or os.path.basename(a.src)
    ctl = dict(page=False, keep=mine.keep, share=mine.share)
    T = []
    import copy
    a1 = copy.deepcopy(a)
    pals = []

    # BASELINE ---------------------------------------------------------------
    import pickle
    if mine.cache and os.path.exists(mine.cache):
        with open(mine.cache, "rb") as f:
            T, e1, t1 = pickle.load(f)
    else:
        e1, t1 = run(a1, ctl, keep=T)
        e1 = Kept(e1)
        if mine.cache:
            with open(mine.cache, "wb") as f:
                pickle.dump((T, e1, t1), f, protocol=4)
    fps = e1.fps
    s1 = summary(e1, fps, label + " baseline")
    cap = capacity(e1.pg_log)
    if mine.targets == "starts":
        targets = [c["start"] for c in cap]
    else:
        targets = [r["j"] for r in e1.pg_log if r["why"]]
    print("baseline: %d frames, %d cut in %d runs (%.0f s)" %
          (s1["frames"], s1["cut"], len(cap), t1))

    # PAGE-AHEAD -------------------------------------------------------------
    pal_at = e1.pg_pals if any(x is not None for x in e1.pg_pals) \
        else None
    ctl.update(page=True, T=T, targets=targets, pals=pal_at, log=e1.pg_log)
    a2 = copy.deepcopy(a)
    a2.out = os.path.join(os.path.dirname(a.out), "page.v88")
    e2, t2 = run(a2, ctl)
    e2 = Kept(e2)
    s2 = summary(e2, fps, label + " page-ahead")
    print("page-ahead: %d flips (%.0f s)" % (s2["flips"], t2))
    res = dict(label=label, fps=fps, keep=mine.keep, baseline=s1,
               page=s2, runs=cap, flips=e2.pg_flips,
               log1=e1.pg_log, log2=e2.pg_log,
               q1=e1.q_frames, q2=e2.q_frames, bad1=e1.q_bad, bad2=e2.q_bad,
               argv=sys.argv[1:] if argv is None else argv)
    report(res)
    if mine.json:
        with open(mine.json, "w") as f:
            json.dump(res, f)
    return 0


def report(res):
    b, p = res["baseline"], res["page"]
    fps = res["fps"]
    print("\n== %s (keep %.2f) ==" % (res["label"], res["keep"]))
    print("  CAPACITY (baseline): decode %.1f%% of the per-frame peak; "
          "headroom under it %.1f%%" % (
              100 * b["cycles"] / max(1, b["peak_cycles"]),
              100 * b["head_cycles"] / max(1, b["peak_cycles"])))
    print("    CPU bucket overflowed %.1f%% of the peak; disk bucket "
          "overflowed %d KB (stream %d KB)" % (
              100 * b["cpu_overflow"] / max(1, b["peak_cycles"]),
              b["disk_overflow"] / 1024, b["stream_bytes"] / 1024))
    runs = res["runs"]
    if runs:
        fit = sum(1 for r in runs if r["head_c"] >= r["full_c"])
        print("    %d cut runs; %d had more CPU headroom since the last run "
              "than the whole first frame costs" % (len(runs), fit))
    for k, s in (("baseline", b), ("page-ahead", p)):
        print("  %-10s cut %4d of %d (disk %d cpu %d peak %d cap %d), "
              "pixels left %d; seen %.3f%% worst sec %.3f%%, broken %.1f s"
              % (k, s["cut"], s["frames"], s["cut_disk"], s["cut_cpu"],
                 s["cut_peak"], s["cut_cap"], s["bytes_left"],
                 100 * (s["q_mean"] or 0), 100 * (s["q_worst"] or 0),
                 (s["broken"] or 0) / fps))
    print("  page-ahead: %d flips, %d targets not flipped; pre-build "
          "%.1f%% of the peak's cycles, %d KB" % (
              p["flips"], p["lost"],
              100 * p["prebuild_cycles"] / max(1, p["peak_cycles"]),
              p["prebuild_bytes"] / 1024))


if __name__ == "__main__":
    sys.exit(main())
