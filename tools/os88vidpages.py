#!/usr/bin/env python3
"""os88vidpages - PAGE-AHEAD ON MORE THAN ONE HIDDEN PAGE, measured before
it is built (docs/reports/VIDEO-PAGES-2026-10-15.md).

SPEC.md 98.1.3.7 builds a cut frame early on ONE hidden page. Mode X has
three pages at 320 x 240 and four at 320 x 200, and 16 colours on a screen
of their own have six at 320 x 240 and eight at 320 x 200, so the question
is what K hidden pages buy over one, in four shapes (the owner's):
  - two or more scene changes close together after a calm;
  - a burst of action after a calm, every frame of it a target;
  - more than one expensive frame PRELOADED before the clock starts;
  - --flip's two pages AND a page ahead (`--flip-ahead`).

It answers with the shipped encoder's own model and nothing else: the plan
pass is os88venc's (`_paok`, page-ahead on), and the real pass is
os88venc's `_encode` with `PageAhead` swapped for `PageAheadK` below - K
hidden pages, each built toward a target of its own, the nearest target
first. K = 1 is checked against the shipped class to the frame before any
K > 1 is believed. The file a K > 1 pass writes is not a format (it carries
one page's hidden records) and is thrown away unverified.

    python3 tools/os88vidpages.py SRC --pages 1,2,3 [--targets starts|all]
        [--flip-ahead] [--json OUT] [--label NAME] -- [os88venc options]

`--targets starts` is the shipped plan (each cut run's first frame);
`all` makes every frame the plan cut a target, which is what a burst needs.
`--flip-ahead` runs every pass with the encoder FLIPPED (--flip's cost
model and schedule), page-ahead's refusal of that bypassed, a flip to a
page built ahead charged a whole canvas of latch copy on the frame after
it - the price of bringing the next back page up from the page now shown."""

import argparse
import copy
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

FORCE_FLIP = [False]
COPY_OK = [None]        # --copy-ok: a flip is refused when what is left of
                        # its back page's copy is more than this share of a
                        # period - the frame after it could not pay it.
                        # None refuses nothing, and the frame after pays
CUR = [None]                            # the PageAheadK of the pass running


def flip_classes():
    """EncoderX and EncoderP built flipped whatever `a.flip` says - so
    _encode's refusal of --flip beside --page-ahead is not reached - with
    the flip schedule (98.2.1.1.1) and wm3 set as a.flip would set them,
    silently, one frame a call"""
    out = {}
    for base in (V.EncoderX, V.EncoderP):
        class F(base):
            def __init__(self, *args):
                args = list(args)
                if len(args) >= 7:
                    args[6] = True
                else:
                    args.append(True)
                super().__init__(*args)

            def begin(self):
                if not getattr(self, "_pa_hid", False) and \
                        self.fgrid is None and self.cpu.per is not None:
                    self.fgrid = self.q
                    self.flag_lag = 1
                    self.wm3 = False
                super().begin()
        F.__name__ = "Flip" + base.__name__
        out[base] = F
    return out


def make_pa(K, flip_ahead=False, stats=None, precopy=False):
    """PageAhead with K hidden pages. `precopy` (--flip-ahead on FOUR
    pages): the page a flip will need as its next back page is copied from
    the built one AHEAD, in bands out of spare cycles once the build is
    whole, so the frame after the flip pays only what is left of it"""

    class PageAheadK(V.PageAhead):
        def __init__(self, enc, plan, key0=0, kcap=V.KEY_PLAYER,
                     kpart=vid.KEY_PART, ref=0):
            super().__init__(enc, plan, key0, kcap, kpart, ref)
            h0 = self.h
            h0._pa_hid = True
            self.hs = [h0]
            for _ in range(K - 1):
                h = copy.deepcopy(h0)
                if hasattr(h, "sv"):
                    h.sv = np.frombuffer(h.surf, dtype=np.uint8)
                    h.tv = np.frombuffer(h.tsurf, dtype=np.uint8)
                self.hs.append(h)
            self.asg = [None] * K       # page -> the target it is built for
            self.done = set()           # targets reached
            self.unbuilt = 0            # ...that had no page at all
            self.flipcopy = False
            self.cleft = {}             # target -> copy cycles still owed
            self.copyrefused = 0
            self.extra = 0              # builds on a page past the first
            self.Fat_all = {}
            self._assign(-1)
            CUR[0] = self

        def _assign(self, j):
            """Free pages take the next targets nobody is building yet,
            nearest first"""
            held = set(x for x in self.asg if x is not None)
            for F in self.targets:
                if None not in self.asg:
                    break
                if F > j + 1 and F not in held and F not in self.done:
                    i = self.asg.index(None)
                    self.asg[i] = F
                    held.add(F)
            live = [x for x in self.asg if x is not None]
            self.F = min(live) if live else None

        def before(self, j, target):
            self.flipped = False
            e = self.e
            if flip_ahead and getattr(e, "flip", False):
                if self.flipcopy is not False:
                    # the frame after a flip to a page built ahead: the
                    # next back page is brought up from the page shown,
                    # all of it (98.3.8.1's latch copy, its rows whole) -
                    # or with `precopy`, what the calm did not already do
                    e.prev_c = self.flipcopy
                self.flipcopy = False
            if j in self.targets and j not in self.done:
                self.done.add(j)
                if j in self.asg:
                    i = self.asg.index(j)
                    h = self.hs[i]
                    owe = 0.0
                    if flip_ahead and getattr(e, "flip", False):
                        owe = self.cleft.get(j, e.g.h * 80 * e.lcopy +
                                             V.CYC_LCOPY0) if precopy else \
                            e.g.h * 80 * e.lcopy + V.CYC_LCOPY0
                    if COPY_OK[0] is not None and owe > COPY_OK[0] * e.q:
                        self.lost += 1  # (its back page unpaid for: a flip
                        self.copyrefused += 1   # would overrun the period)
                    elif np.count_nonzero(h.screen != target) < \
                            np.count_nonzero(e.screen != target):
                        for k in V.PA_STATE:
                            if hasattr(e, k) and hasattr(h, k):
                                x, y = getattr(e, k), getattr(h, k)
                                setattr(e, k, y)
                                setattr(h, k, x)
                        e.scache = h.scache = None
                        self.flipped = True
                        self.flips += 1
                        if flip_ahead and getattr(e, "flip", False):
                            e.prev_c = 0    # (no back page caught up: the
                            full = e.g.h * 80 * e.lcopy + V.CYC_LCOPY0
                            self.flipcopy = self.cleft.get(j, full) \
                                if precopy else full    # one built is
                                                        # drawn on)
                    else:
                        self.lost += 1
                    self.asg[i] = None
                else:
                    self.unbuilt += 1
            for i, F in enumerate(self.asg):    # (a target passed unflipped)
                if F is not None and F < j:
                    self.asg[i] = None
            self._assign(j)
            self.Fat[j] = self.F
            self.Fat_all[j] = tuple(self.asg)

        def preload(self):
            """Key 0's hidden pages: EVERY page's target whole, at no cost
            to any frame (keys in parts, 98.1.3.8)"""
            self.preloaded = 0
            ops0 = None
            order = sorted((F, i) for i, F in enumerate(self.asg)
                           if F is not None)
            for F, i in order:
                h = self.hs[i]
                T = self.T[F]
                p = self.P.get(F)
                if p is not None:
                    h.set_pal(p)
                h.peak = h.ceil = math.inf
                h.cpu = V.Budget(None, None)
                h.disk = V.Budget(None, None)
                h.rec_max = 1 << 30
                h.prev_c = 0
                ops = h.frame(T, b"")[0]
                n = int(np.count_nonzero(h.screen == T))
                if ops0 is None:
                    ops0, self.preloaded, self.h = ops, n, h
                if stats is not None:
                    stats.setdefault("preload", []).append(
                        (F, n, int(T.size)))
            self.pre_ops = ops0

        def upgrade_keys(self, wr):
            return 0                    # (not a format: nothing to upgrade)

        def after(self, j, rec, audio, target=None):
            e = self.e
            if j == self.key0 and not hasattr(self, "preloaded") and \
                    any(x is not None for x in self.asg):
                self.preload()
            if self.flipped or all(x is None for x in self.asg) or \
                    e.last_why is not None or j >= len(self.log) or \
                    any(qa <= j < qb for qa, qb in self.quiet):
                return None
            c = e.cost(rec) + (e.prev_c if getattr(e, "flip", False)
                               else 0.0)
            cpu_left = min(e.cpu.room(), e.ceil, e.peak) - c
            dsk_left = math.inf
            if e.disk.per is not None:
                dsk_left = min(e.disk.room() - e.disk_floor(),
                               self.safe_bytes(j, len(rec) - len(audio))) - \
                    (len(rec) - len(audio))
            if e.dcurve and e.disk.per is not None:
                rate = e._drate() if e.bank else e.drate
                base = (c + e.audio_cyc + V.HOOK_CYC) / e.q + e.spk

                def loss(cc):
                    return max(0.0, (e.disk_rel(base) - e.disk_rel(
                        base + cc / e.q)) * rate * 0.99 / e.fps)
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
            dsk_left = min(dsk_left, e.rec_max - len(rec))
            first, tc, tb = None, 0.0, 0.0
            for F, i in sorted((F, i) for i, F in enumerate(self.asg)
                               if F is not None):
                if cpu_left < 4 * e.ct[0] or dsk_left < 256:
                    break
                h = self.hs[i]
                T = self.T[F]
                if not np.any(h.screen != T):
                    if precopy:         # (built: its copy, in bands)
                        full = e.g.h * 80 * e.lcopy + V.CYC_LCOPY0
                        left = self.cleft.setdefault(F, full)
                        cc = min(left, max(0.0, cpu_left))
                        self.cleft[F] = left - cc
                        cpu_left -= cc
                        tc += cc
                    continue            # (built: the next page's turn)
                p = self.P.get(F)
                if p is not None and (h.pal is None or
                                      not np.array_equal(h.pal, p)):
                    h.set_pal(p)
                h.peak = h.ceil = cpu_left
                h.cpu = V.Budget(None, None)
                h.disk = V.Budget(None, None)
                h.rec_max = int(dsk_left)
                h.prev_c = 0
                ops, hrec = h.frame(T, b"")
                if not ops:
                    break
                hc, hb = float(h.cost(hrec)), float(len(hrec))
                cpu_left -= hc
                dsk_left -= hb
                tc += hc
                tb += hb
                self.start.setdefault(F, j)
                if first is None:
                    first, self.h = ops, h
                else:
                    self.extra += 1
            if first is None:
                if tc:
                    e.pa_spend = (tc, 0.0)
                    self.cyc += tc
                return None
            e.pa_spend = (tc, tb)
            self.cyc += tc
            self.bytes += tb
            return first

    return PageAheadK


PHOTOS = ("apps/pixel/samples/HOUSE.PNG", "apps/pixel/samples/MOUNTAIN.BMP",
          "apps/pixel/samples/VACATION.JPG", "apps/pixel/samples/FLOWER.JPG",
          "apps/pixel/samples/ROOM.JPG", "apps/pixel/samples/BALLOONS.PNG",
          "apps/pixel/samples/CAT.GIF", "apps/pixel/samples/LAKE.JPG")
CW, CH, FPS = 320, 240, 30


def _photos(root):
    from PIL import Image
    out = []
    for p in PHOTOS:
        im = Image.open(os.path.join(root, p)).convert("RGB")
        out.append(im)
    return out


def _cover(im, w, h, zoom=1.0, fx=0.5, fy=0.5):
    """`im` scaled to cover w x h at `zoom`, the window at (fx, fy) of the
    slack"""
    from PIL import Image
    s = max(w / im.width, h / im.height) * zoom
    iw, ih = max(w, int(round(im.width * s))), max(h, int(round(im.height * s)))
    big = im.resize((iw, ih), Image.BILINEAR)
    x = int(round((iw - w) * fx))
    y = int(round((ih - h) * fy))
    return np.asarray(big.crop((x, y, x + w, y + h)), dtype=np.uint8)


def _calm(pic, n, t0):
    """n frames of a still with a 48-pixel box moving across it - the
    report's `still`"""
    for i in range(n):
        f = pic.copy()
        x = int((t0 + i) * 4) % (CW - 48)
        y = CH // 2 - 24 + int(40 * math.sin((t0 + i) / 20.0))
        f[y:y + 48, x:x + 48] = (255, 255, 255)
        f[y + 4:y + 44, x + 4:x + 44] = (200, 30, 30)
        yield f


def _action(im, n):
    """n frames in which EVERY pixel moves: a fast pan and zoom"""
    for i in range(n):
        u = i / max(1, n - 1)
        yield _cover(im, CW, CH, 1.6 + 0.6 * u, u, 1.0 - u)


CLIPS = {
    "still": "8 stills, 3 s each, a hard cut between them (the 2026-10-13 "
             "report's `still`)",
    "pairs": "a 3 s calm, then TWO hard cuts 6 frames apart - four times",
    "pairs2": "a 3 s calm, then TWO hard cuts 2 frames apart - four times",
    "triples": "a 3 s calm, then THREE hard cuts 5 frames apart - three times",
    "burst": "a 3 s calm, then 1 s in which every pixel moves, then calm - "
             "four times",
    "burst10": "as burst, the action 10 frames (a third of a second)",
    "opening": "a hard cut every 4 frames for the first 32, then a 5 s calm",
}


def clip_frames(name, root):
    ph = _photos(root)
    pics = [_cover(im, CW, CH) for im in ph]
    t = 0
    if name == "still":
        for k in range(8):
            for f in _calm(pics[k], 90, t):
                yield f
            t += 90
    elif name in ("pairs", "pairs2", "triples"):
        n, gap = dict(pairs=(2, 6), pairs2=(2, 2), triples=(3, 5))[name]
        k = 0
        for rep in range(4 if n == 2 else 3):
            for f in _calm(pics[k % 8], 90, t):
                yield f
            t += 90
            for c in range(n):
                k += 1
                for f in _calm(pics[k % 8], gap, t):
                    yield f
                t += gap
            k += 1
        for f in _calm(pics[k % 8], 90, t):
            yield f
    elif name in ("burst", "burst10"):
        na = 30 if name == "burst" else 10
        for rep in range(4):
            for f in _calm(pics[(2 * rep) % 8], 90, t):
                yield f
            t += 90
            for f in _action(ph[(2 * rep + 1) % 8], na):
                yield f
        for f in _calm(pics[0], 90, t):
            yield f
    elif name == "opening":
        for k in range(8):
            for f in _calm(pics[k], 4, t):
                yield f
            t += 4
        for f in _calm(pics[0], 150, t):
            yield f
    else:
        raise SystemExit("os88vidpages: no clip %r (%s)" % (
            name, ", ".join(sorted(CLIPS))))


def make_clip(name, out):
    """A synthetic clip, lossless, out of the photographs the PiXEL
    package ships (apps/pixel/samples) - the same frames every run"""
    import subprocess
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y", "-f", "rawvideo",
           "-pix_fmt", "rgb24", "-s", "%dx%d" % (CW, CH), "-r", str(FPS),
           "-i", "-", "-c:v", "ffv1", out]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    n = 0
    for f in clip_frames(name, root):
        p.stdin.write(np.ascontiguousarray(f).tobytes())
        n += 1
    p.stdin.close()
    if p.wait():
        raise SystemExit("os88vidpages: ffmpeg failed on %s" % out)
    return n


def key_cost(wr, j, nkeys):
    """After frame j's write: when it wrote a key, what that key would
    carry of EVERY hidden page - each page's writes from the key's own
    picture, as 98.1.3.7's hidden record is - against the key alone"""
    pa, st = CUR[0], KEYSTATS[0]
    if pa is None or st is None or len(wr.keys) == nkeys:
        return
    e, g = pa.e, pa.e.g
    k = wr.keys[-1]
    pages = []
    for h in pa.hs:
        if np.any(h.screen != e.screen):
            pages.append(len(vid.record(vid.delta_ops(h.surf, e.surf, g), g,
                                        limit=None)))
    st.append(dict(j=j, key=len(k[1]), pages=sorted(pages, reverse=True)))


KEYSTATS = [None]


class Zeros(dict):
    """A refused write's result: every figure the encode reads off it 0"""

    def __missing__(self, k):
        return 0


def parse(src, opts, out):
    a = V.parser().parse_args([src, out] + opts)
    a.preview_png = None
    a.progress = False
    a.quiet = True
    return a


def run(a, keep=None, K=None, flip_ahead=False, stats=None, precopy=False):
    """One encode: os88venc's own, with K hidden pages when K is given"""
    orig = (V.PageAhead, vid.verify_v88, V.EncoderX, V.EncoderP,
            vid.Writer.frame, vid.Writer.write)
    try:
        if stats is not None:
            KEYSTATS[0] = stats.setdefault("keys", [])
            wf = orig[4]
            cnt = [0]

            def frame(self, *x, **k):
                n = len(self.keys)
                r = wf(self, *x, **k)
                key_cost(self, cnt[0], n)
                cnt[0] += 1
                return r
            vid.Writer.frame = frame
        if K is not None and (K > 1 or flip_ahead or stats is not None):
            V.PageAhead = make_pa(K, flip_ahead, stats, precopy)
            vid.verify_v88 = lambda *x, **k: None
        if flip_ahead:
            fc = flip_classes()
            V.EncoderX, V.EncoderP = fc[orig[2]], fc[orig[3]]
            ww = orig[5]

            def write(self, *x, **k):
                # (the writer was made unflipped, so it packs a flipped
                # encode's small records past the 255 a key can name: a
                # file nobody reads, and the encoder's figures are kept)
                try:
                    return ww(self, *x, **k)
                except vid.V88Error as err:
                    if "can name 255" not in str(err):
                        raise
                    return Zeros(bytes=1, kdropped=[])
            vid.Writer.write = write
        t0 = time.time()
        res = V.encode(a, keep=keep)
        return res, time.time() - t0
    finally:
        V.PageAhead, vid.verify_v88, V.EncoderX, V.EncoderP, \
            vid.Writer.frame, vid.Writer.write = orig
        KEYSTATS[0] = CUR[0] = None


def fig(res):
    pg = res.get("page") or {}
    return dict(cuts=res.get("cuts"), q_vis=res.get("q_vis"),
                q_worst=res.get("q_worst"), q_broken=res.get("q_broken"),
                flips=pg.get("flips"), lost=pg.get("lost"),
                targets=pg.get("targets"), cyc=pg.get("cyc"),
                bytes=pg.get("bytes"),
                q_frames=res.get("q_frames"))


def table(paths):
    """`os88vidpages.py table A.json ...`: the runs as the report's
    markdown - per page count, the frames cut, the error as seen, the worst
    second, the seconds visibly broken and the flips, and the keys' growth"""
    print("| run | plain | " + " | ".join(
        "K=%s" % k for k in ("1", "2", "3", "5", "7")) + " |")
    print("|---|---|" + "---|" * 5)
    for p in paths:
        r = json.load(open(p))
        pl = r["plain"]

        def cell(f):
            return "%d / %.2f / %.1f" % (
                f["cuts"], 100 * (f["q_worst"] or 0), f["q_broken"] or 0)
        row = ["%s" % r["label"], cell(pl)]
        for k in ("1", "2", "3", "5", "7"):
            f = r["runs"].get(k)
            row.append("-" if f is None else "%s, %d flips" % (
                cell(f), f["flips"] or 0))
        print("| " + " | ".join(row) + " |")


def keys_table(paths):
    for p in paths:
        r = json.load(open(p))
        for k, f in sorted(r["runs"].items()):
            ks = f.get("keys") or []
            if not ks:
                continue
            base = sum(x["key"] for x in ks) / len(ks)
            pages = [sum(x["pages"]) for x in ks]
            print("%s K=%s: %d keys, key %.0f KB, the pages it would carry "
                  "%.0f KB on average, %.0f KB at most" % (
                      r["label"], k, len(ks), base / 1024,
                      sum(pages) / len(ks) / 1024, max(pages) / 1024))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["table"]:
        return table(argv[1:])
    if argv[:1] == ["keys"]:
        return keys_table(argv[1:])
    if argv[:1] == ["clip"]:
        if len(argv) != 3:
            raise SystemExit("os88vidpages clip NAME OUT.mkv - %s" % "; ".join(
                "%s: %s" % kv for kv in sorted(CLIPS.items())))
        print("%s: %d frames" % (argv[2], make_clip(argv[1], argv[2])))
        return 0
    if "--" in argv:
        i = argv.index("--")
        mine, opts = argv[:i], argv[i + 1:]
    else:
        mine, opts = argv, []
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--pages", default="1,2,3")
    ap.add_argument("--targets", choices=("starts", "all", "scenes"),
                    default="starts")
    ap.add_argument("--scene", type=float, default=0.2,
                    help="--targets scenes: the share of a frame's pixels "
                    "that must differ from the frame before for a cut "
                    "frame to be a target")
    ap.add_argument("--flip-ahead", action="store_true")
    ap.add_argument("--copy-ok", type=float,
                    help="--flip-ahead: refuse a flip whose back page's "
                    "copy left to pay is more than this share of a period")
    ap.add_argument("--precopy", action="store_true",
                    help="--flip-ahead on FOUR pages: the next back page "
                    "copied from the built one in the calm before the flip")
    ap.add_argument("--json")
    ap.add_argument("--label")
    ap.add_argument("--check", action="store_true",
                    help="also run K = 1 through PageAheadK and require it "
                    "to equal the shipped class's pass")
    m = ap.parse_args(mine)
    tmp = tempfile.mkdtemp(prefix="vidpages")
    COPY_OK[0] = m.copy_ok
    label = m.label or os.path.basename(m.src)
    Ks = [int(x) for x in m.pages.split(",")]

    # THE PLAN: os88venc's own plain pass, page-ahead on, every target kept
    b = parse(m.src, opts, os.path.join(tmp, "plan.v88"))
    b._paok, b.page_ahead = True, "on"
    if m.flip_ahead:
        b.flip = False                  # (flipped by the classes instead)
    T_all = []
    res1, t1 = run(b, keep=T_all, flip_ahead=m.flip_ahead)
    plan = res1["paplan"]
    log = plan["log"]
    if len(T_all) != len(log):
        raise SystemExit("os88vidpages: %d targets kept against %d frames "
                         "logged" % (len(T_all), len(log)))
    cut = [j for j, r in enumerate(log) if r["why"] is not None]
    if m.targets in ("all", "scenes"):
        # ALL: every frame the plan cut. SCENES: the plan's own targets
        # (each cut run's first) and every cut frame whose picture is a
        # CHANGE - `--scene` of its pixels off the frame before - so a run
        # of cut frames that is one scene still being drawn is one target,
        # and a run that is a burst, or cuts back to back, is one a frame
        if m.targets == "scenes":
            cut = sorted(set(plan["T"]) | set(
                j for j in cut if j and np.count_nonzero(
                    T_all[j] != T_all[j - 1]) > m.scene * T_all[j].size))
        P, last = {}, None
        for j in range(len(log)):
            if j in plan["P"]:
                last = plan["P"][j]
            if j in cut:
                P[j] = plan["P"].get(j, last)
        plan = dict(plan, T=dict((j, T_all[j]) for j in cut), P=P)
    out = dict(label=label, opts=opts, targets=m.targets,
               flip_ahead=m.flip_ahead, frames=len(log),
               plain=fig(res1), plan_targets=len(plan["T"]),
               cut_frames=len(cut), runs={})
    print("%s: plain %d frames, %d cut (%d targets), seen %.3f%% worst "
          "%.2f%% broken %.1f s  (%.0f s)" % (
              label, len(log), res1["cuts"], len(plan["T"]),
              100 * res1["q_vis"], 100 * (res1.get("q_worst") or 0),
              res1.get("q_broken") or 0, t1))
    if m.check:
        c = parse(m.src, opts, os.path.join(tmp, "ship.v88"))
        c._paok, c.page_ahead, c._paplan = True, "on", plan
        r0, _ = run(c)
        c = parse(m.src, opts, os.path.join(tmp, "k1.v88"))
        c._paok, c.page_ahead, c._paplan = True, "on", plan
        rk, _ = run(c, K=1, stats={})
        same = (r0["cuts"], r0.get("q_frames")) == \
            (rk["cuts"], rk.get("q_frames"))
        print("  CHECK K=1 against the shipped class: %s (cut %d / %d)" % (
            "IDENTICAL" if same else "DIFFERENT", r0["cuts"], rk["cuts"]))
        out["check"] = same
    for K in Ks:
        c = parse(m.src, opts, os.path.join(tmp, "k%d.v88" % K))
        c._paok, c.page_ahead, c._paplan = True, "on", plan
        if m.flip_ahead:
            c.flip = False
        st = {}
        r, t = run(c, K=K, flip_ahead=m.flip_ahead, stats=st,
                   precopy=m.precopy)
        f = fig(r)
        f["preload"] = st.get("preload")
        f["keys"] = st.get("keys")
        out["runs"][K] = f
        print("  K=%d: cut %4d, seen %.3f%% worst %.2f%% broken %.1f s; "
              "%d flips of %d targets, built %.0f KB  (%.0f s)" % (
                  K, r["cuts"], 100 * r["q_vis"],
                  100 * (r.get("q_worst") or 0), r.get("q_broken") or 0,
                  f["flips"] or 0, f["targets"] or 0,
                  (f["bytes"] or 0) / 1024, t))
    if m.json:
        with open(m.json, "w") as fo:
            json.dump(out, fo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
