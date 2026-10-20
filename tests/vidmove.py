#!/usr/bin/env python3
"""A RESIDENT BLOCK MOVES UNDER A LIVE PLAY - SPEC.md 98.1.7.4.

    make && python3 tests/vidmove.py        (VIDMOVE_MAP=1: the heap, drawn)

A resident block is the biggest claim the player makes, held while its
window is open, so pinned it is a wall in the middle of the heap for as
long as a Live window sits on the desktop (VIDEO-PLAN 15.4 D). It is
declared MOVABLE once it is loaded and walked. The keeper and the poster,
which lay under it, come from the top of the heap now.

On the owner's 5150 (Hercules, a hard disk), three windows of the player:

1. A - a stored resident file of ~180 KB - plays once and stays open: its
   block lies low in the heap.
2. B - a Live clip - plays Live, held at a frame. Its block, above A's, is
   declared movable (MC_RLOC = vp_rmove); its keeper and poster are
   top-down.
3. A closes: a hole under B's block. C - a stored file picked NOW, from the
   heap's own map, to be bigger than every free run and no bigger than what
   an ascending pass joins - plays. Its claim can only be served by moving
   B's block, and it MOVES: [vp_rblk] a claim at a new place, its bytes the
   host's there, the cursor's base [vp_rbseg] with it.
4. THE PLAY GOES ON: B's holds after the move are the host's decode, in the
   box, across the seam - the Live worker came back from its park.

Broken on purpose - never declared (vp_rload's `mov ax, vp_rmove` made
`xor ax, ax`) - 3 FAILS: the block is pinned, and C is refused or served
elsewhere; vp_rmove a bare `ret` - 3 FAILS: [vp_rblk] names no claim after
the move.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
import heapmap                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

WB, H, NF, FPS, L = 20, 60, 40, 15.0, 10      # B, the Live clip
MACHINE = "os8088_5150_herc_hdd_gla"
CLUSTER = 2048                                # the VHD's (os88hdd's FAT16)
C_KB = range(150, 420, 15)                    # C's candidates, block KB


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def live():
    """vidlive's Hercules rendition: LIN80, a seam back to frame L"""
    g = vid.Geom(vid.LAY_LIN80, WB, H)
    w = vid.Writer(g, int(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="move", keysecs=100.0, loop=L)
    surf = g.surface()
    for f in range(NF):
        ch = []
        for y, b in enumerate(g.base):
            x0 = (f * 3 + y // 6) % WB
            for x in range(WB):
                v = (0x3C if (y + f) % 3 else 0xFF) \
                    if x0 <= x < x0 + 4 else 0
                if surf[b + x] != v:
                    surf[b + x] = v
                    ch.append(b + x)
        w.frame(vid.spans(ch, surf, g), surf)
    return w


def noise(path, kb, seed):
    """a stored resident Hercules file of noise, its block ~kb KB"""
    rnd = random.Random(seed)
    g = vid.Geom(vid.LAY_HERC, 40, 100)
    w = vid.Writer(g, 1500, 100, vid.AUD_NONE, 0, vid.PF_MONO1, title="n",
                   keysecs=100.0)
    surf = g.surface()
    for f in range(max(2, kb * 1024 // 4300)):
        cv = rnd.randbytes(4000)
        for y, b in enumerate(g.base):
            surf[b:b + 40] = cv[y * 40:(y + 1) * 40]
        w.frame(vid.spans([b + x for b in g.base for x in range(40)],
                          surf, g), surf)
    st = vid.write_resident(path, [w], title="n", pack=vid.PK_LZB)
    (u, p), = st["blocks"]
    if u != p:
        raise SystemExit("vidmove: %s is packed, not stored" % path)
    return (u + 2 * CLUSTER + 1023) // 1024     # its claim, vp_bkkb's


class Q(object):
    """heapmap.Map reads through .read(linear, n), which Marty has"""

    def __init__(self, m):
        self.m = m

    def read(self, linear, n):
        return self.m.read(linear, n)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        tmp = os.path.abspath(tmp)
        vb = os.path.join(tmp, "LIVE.V88")
        vid.write_resident(vb, [live()], title="move", repeat=True,
                           live=[vid.TARGETS["herc"]])
        vid.verify_v88(vb)
        r = vid.Reader(vb)
        blk = b"".join(r._recs)
        files = {"LIVE.V88": vb, "A.V88": os.path.join(tmp, "A.V88")}
        noise(files["A.V88"], 180, 1)
        ckb = {}
        for i, kb in enumerate(C_KB):
            n = "C%d.V88" % kb
            files[n] = os.path.join(tmp, n)
            ckb[n] = noise(files[n], kb, 10 + i)
        import os88vencgui as G
        cmd, img = G.disk_argv(vb, 6, out=os.path.join(tmp, "M.VHD"),
                               build=os88build.at("build"))
        for flag, val in zip(("--cyls", "--heads", "--spt"),
                             ("615", "4", "26")):
            cmd[cmd.index(flag) + 1] = val
        i = next(i for i, c in enumerate(cmd) if c.endswith("=" + vb))
        del cmd[i - 1:i + 1]                  # (--file, NAME=path: ours below)
        for n, p in files.items():
            cmd += ["--file", "%s=%s" % (n, p)]
        subprocess.run(cmd, check=True, capture_output=True)
        m = os88marty.launch(None, machine=MACHINE,
                             extra=["--mount", "hd:0:" + img])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=300)
            hsym = {n: m.sym(n) for n in ("mem_base", "mem_top", "spl_live",
                                          "mem_tab")}

            def hmap():
                return heapmap.Map(Q(m), hsym)

            def wait(cond, what, guest=90.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=900.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            class P(object):
                """one player window: its record, its bss"""

                def __init__(self, name):
                    self.w = ui.path("C:/" + name)
                    self.rebase()

                def rebase(self):
                    rec = m.read(ui._S("wm_wins") + self.w.i * geom.WIN_SIZE,
                                 geom.WIN_SIZE)
                    self.base = u16(rec, geom.W_SEG) << 4

                def rw(self, n):
                    return u16(m.read(self.base + syms[n], 2))

                def rb(self, n):
                    return m.read(self.base + syms[n], 1)[0]

                def ww(self, n, v):
                    m.write(self.base + syms[n], struct.pack("<H", v))

                def play(self, what):
                    wait(lambda mm: self.rb("vp_loaded") == 1,
                         "%s's header" % what)
                    m.write(self.base + syms["vp_played"], b"\0")
                    m.type_text("p")

                def played(self, what):
                    wait(lambda mm: self.rb("vp_played") == 1,
                         "%s to play to its end" % what, 300.0)

            dg = vid.Geom(vid.LAY_HERC, 90, 348)

            def hold(p, n, what):
                wait(lambda mm: p.rb("vp_held") == 1 and p.rw("vp_done") == n
                     and p.rw("vp_dy1") == 0, "%s (frame %d)" % (what, n))
                os88marty.pace(m, 0.3)
                px, py = p.rw("vp_px"), p.rw("vp_py")
                seg = bytes(m.read(0xB0000, 65536))
                got = b"".join(seg[dg.base[py + y] + px // 8:
                                   dg.base[py + y] + px // 8 + WB]
                               for y in range(H))
                d = sum(1 for x, y in zip(got, vid.decode_at(r, n - 1))
                        if x != y)
                print("   %s: the box holds frame %d, %d bytes of %d differ"
                      % (what, n - 1, d, len(got)))
                if d:
                    bad.append("%s: frame %d differs in %d bytes"
                               % (what, n - 1, d))

            try:
                # --- 1: A, loaded low and kept
                a = P("A.V88")
                a.play("A")
                a.played("A")
                print("   1: A's block at %04x" % a.rw("vp_rblk"))
                # --- 2: B, Live, held
                b = P("LIVE.V88")
                b.ww("vp_stopat", 5)
                b.play("B")
                wait(lambda mm: b.rb("vp_lsess") == 1, "B's live session")
                hold(b, 5, "before")
                seg0 = b.rw("vp_rblk")
                cl = {c.seg: c for c in hmap().claims}
                bc, kc = cl.get(seg0), cl.get(b.rw("vp_keep"))
                pc = cl.get(b.rw("vp_pseg"))
                print("   2: B's block %04x %s (A's %04x); its keeper %s, "
                      "its poster %s" % (
                          seg0, "movable" if bc and bc.rloc ==
                          syms["vp_rmove"] else "NOT DECLARED",
                          a.rw("vp_rblk"),
                          "top-down" if kc and kc.hi else "BOTTOM-UP",
                          "top-down" if pc and pc.hi else "BOTTOM-UP"))
                if not bc or bc.rloc != syms["vp_rmove"]:
                    bad.append("2: B's block is not declared movable")
                if not (kc and kc.hi and pc and pc.hi):
                    bad.append("2: B's keeper or poster is bottom-up")
                if seg0 < a.rw("vp_rblk"):
                    raise Stop("B's block is under A's: no hole can open "
                               "beneath it")
                # --- 3: A closes; C is picked off the map, and plays
                aseg = a.base >> 4
                ui.close(a.w)               # ...and its claims gone, which is
                wait(lambda mm: not any(    # after the window is
                    x.own == aseg or x.seg == aseg for x in hmap().claims),
                    "A's claims to be freed")
                hm = hmap()
                if os.environ.get("VIDMOVE_MAP"):
                    hm.report("A closed")
                # WHAT C MUST BEAT: every KB the kernel can gather on either
                # side of B's block WITHOUT moving it - below it the caches
                # shed and the movable claims packed down (a save-under is a
                # cache too), above it the run to the next barrier. Bigger
                # than both and no bigger than an ascending pass joins, C can
                # only be served by moving the block
                bseg = seg0
                held = sum(x.para for x in hm.claims
                           if x.seg < bseg and not x.purgeable)
                below = (bseg - hm.base - held) // 64
                above = max((n for s0, n in hm.runs(drop_purgeable=True)
                             if s0 > bseg), default=0) // 64
                up = max((n for _, n in hm.compacted()), default=0) // 64
                pick = [n for n in sorted(ckb, key=ckb.get)
                        if max(below, above) + 4 < ckb[n] <= up - 8]
                print("   3: A closed: %d KB to be had under B's block and "
                      "%d over it without moving it, %d after an ascending "
                      "pass; C is %s" % (
                          below, above, up, "%s, a %d KB claim"
                          % (pick[0], ckb[pick[0]]) if pick
                          else "NONE OF THEM"))
                if not pick:
                    raise Stop("no C between %d and %d KB"
                               % (max(below, above), up))
                c = P(pick[0])
                c.play("C")
                wait(lambda mm: c.rb("vp_ready") == 1 or c.rb("vp_played")
                     == 1 or c.rw("vp_msg") == syms["vp_mbuf"],
                     "C to load or refuse", 300.0)
                if not c.rw("vp_rblk"):
                    raise Stop("C did not load: %r" % msg(m, c.base, syms))
                c.played("C")
                if os.environ.get("VIDMOVE_MAP"):
                    hmap().report("C played")
                    print("   C: block %04x msg %04x ready %d" % (
                        c.rw("vp_rblk"), c.rw("vp_msg"), c.rb("vp_ready")))
                b.rebase()
                seg1 = b.rw("vp_rblk")
                cl = {x.seg: x for x in hmap().claims}
                got = bytes(m.read(seg1 << 4, len(blk))) if seg1 else b""
                d = sum(1 for x, y in zip(got, blk) if x != y)
                print("   3: B's block %04x -> %04x: %s, %d of %d bytes "
                      "differ from the host's; the cursor's base %04x"
                      % (seg0, seg1, "a claim" if seg1 in cl else
                         "NOT A CLAIM", d, len(blk), b.rw("vp_rbseg")))
                if seg1 == seg0:
                    bad.append("3: B's block did not move")
                if seg1 not in cl or d or b.rw("vp_rbseg") != seg1:
                    bad.append("3: after the move: a claim %s, %d bytes "
                               "differ, base %04x" % (seg1 in cl, d,
                                                      b.rw("vp_rbseg")))
                # --- 4: B goes on, over the seam
                ui.close(c.w)
                ui.raise_window(next(x for x in ui.windows()  # (the rect
                                     if x.i == b.w.i))      # as it is NOW)
                b.rebase()
                for i, n in enumerate((20, NF, L + 3)):
                    b.ww("vp_stopat", n)
                    m.write(b.base + syms["vp_held"], b"\0")
                    hold(b, n, "after %d" % i)
                b.ww("vp_stopat", 0xFFFF)
                m.write(b.base + syms["vp_held"], b"\0")
            except (Stop, os88marty.MartyError, os88ui.UIError) as e:
                bad.append(str(e).splitlines()[0])
        finally:
            m.close()
    for x in bad:
        print("   FAIL: %s" % x)
    if not bad:
        print("   ok")
    return 1 if bad else 0


def msg(m, base, syms):
    """the player's message, as the window shows it"""
    p = u16(m.read(base + syms["vp_msg"], 2))
    t = bytes(m.read(base + p, 64))
    return t.split(b"\0")[0].decode("latin-1").rstrip()


if __name__ == "__main__":
    sys.exit(main())
