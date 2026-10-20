#!/usr/bin/env python3
"""os88vidlayer - docs/plans/VIDEO-OVERAGE-PLAN.md 4's measurement, taken
before a byte of the layered format is written: what an ENHANCEMENT layer
costs, and what it buys a machine with more disk or more memory than the
file was made for.

    python3 tools/os88vidlayer.py SRC -- [os88venc options for the BASE] \\
        [--machine 300:64,448:64,448:3072] [--prefill-kb 0]

THE BASE is an ordinary encode (os88venc.py, the options after `--`): what
every machine plays. It is encoded exactly as it would ship - the tool only
watches it.

THE LAYER is encoded ON TOP OF IT, in the same run, by a second Encoder
whose screen is the ENHANCED player's: for each frame, the base's writes are
put on that screen first, and the second encoder then spends its own budget
on the best further writes toward the same target - the encoder's own
ranking, best first, never undoing a base write it does not have to. Its
budgets are what the better machine has LEFT once the base is paid - the
CPU above what every later base frame still needs, and the disk the base's
own bucket would CLIP at that machine's rate (Layer says exactly how),
banked in the layer's memory (KB), which `--prefill-kb` starts fuller.
Each `--machine DISK:RAM` is one such layer, all run beside one base.

Reported per layer: its bytes a second, its share of the base, and the
error as seen of the enhanced play against the base's and against a SINGLE
stream encoded for that machine's disk (run separately with the same
options and that --disk: what layering costs against not layering).

NOT MODELLED: the seek between the base's place on the disk and the layer's
(a few ms each, in blocks of 32 KB or more), and the hook's cost of a second
record a frame beyond its bytes. Both make the layer's numbers an upper
bound.
"""
import argparse
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np                                             # noqa: E402
import os88venc as V                                           # noqa: E402


class Layer(object):
    """One better machine's layer beside the base. Both of its budgets are
    what the base CANNOT use there, so the base plays exactly as it would
    without it:
      - THE DISK: the base's own bucket simulated at THIS machine's rate (the
        profile's curve at the base's CPU share, as the base's encoder prices
        it) - and what it would CLIP at the ring's depth, the disk idle with
        the ring full, is the layer's, a frame at a time, banked in the
        layer's memory. That is also exactly what a player sees in real
        time: a full ring and a free disk;
      - THE CPU: ONE bucket the two share, at the base's rate and depth. A
        first pass logs every base record's cost; a backward pass over them
        gives, for each frame, the least the bucket must hold after it to
        pay every later base frame on time (need[f] = need[f+1] + cost[f+1]
        - rate, never below 0). The layer may take only what stands above
        that - which is exact: no base frame runs late for it, and nothing
        the base could not use is withheld, a bucket at its depth clipping
        what the layer leaves"""

    def __init__(self, base, disk_kbs, ram_kb, pre_kb, need):
        e = copy.copy(base)
        e.surf = bytearray(base.surf)
        e.sv = np.frombuffer(e.surf, dtype=np.uint8)
        e.screen = e.sv[e.idx]
        e.tsurf = bytearray(base.tsurf)
        e.tv = np.frombuffer(e.tsurf, dtype=np.uint8)
        e.age = base.age.copy()
        e.wv = base.wv.copy()
        e.stats = {k: 0 for k in base.stats}
        e.q_vis = e.q_wrong = e.q_flick = e.q_tflick = 0.0
        e.q_prev = e.q_tprev = (None, None)
        e.cpu = V.Budget(0.0, 1e18)     # (set a frame at a time below)
        e.dcurve = None
        e.owe = None
        e.bank, e.dslow = 0, 1.0
        e.dfloor = 0
        e.disk = V.Budget(0.0, ram_kb * 1024.0)
        e.disk.level = min(pre_kb, ram_kb) * 1024.0
        e.alead, e.arefill, e.slead, e.srefill = 0, [], 0, []
        e._layer = True
        self.e, self.base = e, base
        self.rate = disk_kbs * 1024.0   # the machine's disk, nominal
        self.bl = base.reserve / 2.0    # the base's bucket on THIS machine
        self.need = need                # per frame: what the base still needs
        self.S = base.cpu.cap / 2.0     # the shared bucket, as the base's
                                        # starts (Budget: half full)
        self.f = 0
        self.name = "%g KB/s, %g KB" % (disk_kbs, ram_kb)
        self.bytes = 0
        self.cycles = 0.0
        self.skipped = 0

    def frame(self, target, ops, rec):
        e, b = self.e, self.base
        for a, bs, run in ops:          # the base's writes, on its screen
            e.surf[a:a + len(bs)] = bs
        e.screen = e.sv[e.idx]
        c = b.cost(rec)
        rel = 1.0
        if b.dcurve:
            share = (c + b.audio_cyc + V.HOOK_CYC) / b.q + b.spk
            rel = b.disk_rel(share)
        per = (self.rate * rel * 0.99 - b.abps) / b.fps
        lvl = self.bl + per
        clip = max(0.0, lvl - b.reserve)      # THE SPARE: the ring full
        self.bl = min(lvl, b.reserve) - (len(rec) - b.abps / b.fps)
        e.disk.per = clip
        cp = b.cpu
        self.S = min(cp.cap, self.S + cp.per) - c   # the base's, paid first
        nd = self.need[self.f] if self.f < len(self.need) else 0.0
        room = max(0.0, self.S - nd)
        room = min(room, max(0.0, b.peak - c))
        self.f += 1
        e.disk.tick()                   # (begin() ticks nothing: per 0)
        e.disk.per = 0.0
        if room <= e.ct[0] * 2 or e.disk.level < 64:
            self.skipped += 1           # (no room: no layer record - and
            e.measure(target)           # never a negative room into frame(),
            return                      # whose retries flip its sign)
        e.cpu.per, e.cpu.level = 0.0, room
        e.peak = room
        ch, er = e.frame(target, b"")
        if ch:
            cc = e.cost(er)
            e.disk.spend(len(er))
            self.S -= cc
            self.cycles += cc
            self.bytes += len(er)
        e.measure(target)


def main():
    argv = sys.argv[1:]
    if "--" not in argv:
        sys.exit(__doc__)
    i = argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--machine", default="300:64,448:64,448:3072",
                    help="DISK_KBs:RAM_KB layers, comma separated")
    ap.add_argument("--prefill-kb", type=float, default=0.0)
    a = ap.parse_args(argv[:i])
    out = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                       "os88vidlayer-%d.v88" % os.getpid())
    va = V.parser().parse_args([a.src, out] + argv[i + 1:])
    machines = [tuple(float(x) for x in m.split(":"))
                for m in a.machine.split(",")]
    orig_f, orig_c = V.Encoder.frame, V.Encoder.charge

    # --- PASS 1: the base alone, every record's cost
    costs, rates = {}, {}

    def charge1(self, rec, abytes):
        c = orig_c(self, rec, abytes)
        costs.setdefault(id(self), []).append(self.cost(rec))
        rates[id(self)] = self.cpu.per
        return c
    V.Encoder.charge = charge1
    try:
        V.encode(va)
    finally:
        V.Encoder.charge = orig_c
    k = max(costs, key=lambda x: len(costs[x]))
    cs, rate = costs[k], rates[k]
    need = [0.0] * len(cs)              # after frame f, for f+1 onwards
    for f in range(len(cs) - 2, -1, -1):
        need[f] = max(0.0, need[f + 1] + cs[f + 1] - rate)

    # --- PASS 2: the same encode, the layers beside it
    per = {}

    def frame(self, target, audio=b""):
        ops, rec = orig_f(self, target, audio)
        if getattr(self, "_layer", False):
            return ops, rec
        ls = per.get(id(self))
        if ls is None:
            ls = per[id(self)] = (self, [Layer(self, d, r, a.prefill_kb, need)
                                         for d, r in machines])
        for L in ls[1]:
            L.frame(target, ops, rec)
        return ops, rec
    V.Encoder.frame = frame
    try:
        V.encode(va)
    finally:
        V.Encoder.frame = orig_f
    b, layers = max(per.values(), key=lambda v: v[0].stats["frames"])
    nbase = os.path.getsize(out)
    n = max(1, b.stats["frames"])
    secs = n / b.fps
    print("\nbase: %d frames, %.1f KB/s, error as seen %.2f%%"
          % (n, nbase / secs / 1024, 100 * b.q_vis / n))
    for L in layers:
        e = L.e
        print("layer %-18s %6.1f KB/s, %3.0f%% of the base's bytes, %4.1f%% "
              "more CPU; enhanced play %.2f%% error as seen (%d frames "
              "with no room)" % (L.name, L.bytes / secs / 1024,
                                 100.0 * L.bytes / nbase,
                                 100.0 * L.cycles / (b.cpu.per * n),
                                 100 * e.q_vis / n, L.skipped))
    if os.path.exists(out):
        os.remove(out)


if __name__ == "__main__":
    main()
