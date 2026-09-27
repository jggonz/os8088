#!/usr/bin/env python3
"""A RESIDENT CLIP AS BIG AS MEMORY, AND ONE BIGGER - SPEC.md 98.1.7.1.

    make && python3 tests/vidresbig.py [--leg plays|refuses]

A resident block used to be bounded by one read and one decompression -
under 60 KB packed, under 128 KB unpacked - which made a Live clip about
four seconds (VIDEO-PLAN 15.4). A STORED block is now any size under 1 MB:
the player reads it in 32 KB pieces straight into its claim, and the
machine's memory is the bound, asked BEFORE anything is claimed.

1. IT PLAYS (the Hercules 5150, from a 360 KB floppy): 48 frames of noise,
   one rendition, a block of ~190 KB - past both old bounds, so the writer
   STORES it. The block in its claim is the host's byte for byte, and holds
   at frames spread across the clip are the host's decode.
2. IT REFUSES WITH NUMBERS (the same machine, off a hard disk): a ~700 KB
   block, more than a 640 KB machine has. Play refuses before claiming
   anything, and the window's message reads "Needs n KB of memory, m KB
   free" - n the block's claim, m what a claim would be served.
   Onto the screen, the keeper is the canvas's bound (27 KB, not the
   layout's 32) and the block's writes are NOT walked - nothing decodes
   into the keeper.
3. THE KEEPER IS THE CANVAS (98.1.7.3; the Hercules 5150, a 360 KB floppy):
   a resident LIN80 clip, 160 x 60, plays through the shadow - which was a
   64 KB keeper and is now the canvas's bound, 60 rows of 80 = 5 KB, in
   the player's own byte and in the heap's record of the claim; and its
   holds are the host's decode. Beside it, three twins of that clip, one
   byte each different:
   - EDGE: a frame writes the LAST byte inside the bound - it plays;
   - BAD: a frame writes the FIRST byte past it - Play refuses, the block
     "damaged", and nothing is held;
   - BADKEY: keyframe 1's record writes it - the block is sound and loads,
     and a play from that key refuses ("the keyframe cannot be read").

Broken on purpose - vp_pbk's old 128 KB bound put back - 1 FAILS (the
header is refused as it is read, so the file never loads); vp_fits
answering yes always - 2 FAILS (the claim is attempted, and the message is
never the one with the numbers); vp_rbnd answering "sound" always - 3's BAD
and BADKEY FAIL (they play); the keeper claimed at 64 KB again - 3 FAILS.
"""
import argparse
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
from cycweb import pkg_syms                                   # noqa: E402

WB, H, FPS = 40, 100, 15.0
MACHINE = "os8088_5150_herc_hdd_sb_gla"


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(path, nf, seed):
    """A resident Hercules clip of nf frames of NOISE - every byte changes,
    so a frame is a whole canvas and the block grows ~4 KB a frame"""
    rnd = random.Random(seed)
    g = vid.Geom(vid.LAY_HERC, WB, H)
    w = vid.Writer(g, int(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="big", keysecs=100.0)
    surf = g.surface()
    for f in range(nf):
        cv = bytes(rnd.randrange(256) for _ in range(WB * H))
        ch = []
        for y, b in enumerate(g.base):
            row = cv[y * WB:(y + 1) * WB]
            for x in range(WB):
                if surf[b + x] != row[x]:
                    ch.append(b + x)
            surf[b:b + WB] = row
        w.frame(vid.spans(ch, surf, g), surf)
    st = vid.write_resident(path, [w], title="big", pack=vid.PK_LZB)
    vid.verify_v88(path)
    return st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("plays", "refuses", "keeper", "all"),
                    default="all")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        tmp = os.path.abspath(tmp)

        def session(ui, name):
            m = ui.m
            w = ui.path(name)
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            return m, base

        def waiter(m):
            def wait(cond, what, guest=90.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=900.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))
            return wait

        if a.leg in ("plays", "all"):
            v88 = os.path.join(tmp, "BIG.V88")
            st = clip(v88, 48, 1)
            (ulen, plen), = st["blocks"]
            r = vid.Reader(v88)
            blk = b"".join(r._recs)
            print("   1: the file: %d bytes, the block %d unpacked, %d on "
                  "disk (%s)" % (st["bytes"], ulen, plen,
                                 "STORED" if ulen == plen else "packed"))
            if ulen <= vid.BLK_UNPACKED_MAX or ulen != plen:
                bad.append("1: the block is %d bytes, %d on disk - not a "
                           "stored block past the old bounds" % (ulen, plen))
            disk = os.path.join(tmp, "big.img")
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                            "--size", "360", pkg, v88], check=True,
                           capture_output=True)
            with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                             machine="os8088_5150_herc_gla") as ui:
                m, base = session(ui, "B:/BIG.V88")
                rw = lambda n: u16(m.read(base + syms[n], 2))
                rb = lambda n: m.read(base + syms[n], 1)[0]
                ww = lambda n, v: m.write(base + syms[n],
                                          struct.pack("<H", v))
                wait = waiter(m)
                try:
                    wait(lambda mm: rb("vp_loaded") == 1, "the header")
                    if not rb("vp_ok"):
                        raise Stop("the player will not play it: %s"
                                   % msg(m, base, syms))
                    stops = (10, 30, 47)
                    ww("vp_stopat", stops[0])
                    m.write(base + syms["vp_played"], b"\0")
                    m.type_text("p")
                    wait(lambda mm: rb("vp_ready") == 1 or
                         rb("vp_played") == 1, "the play to start", 300.0)
                    if not rb("vp_ready"):
                        raise Stop("the play did not start: %s"
                                   % msg(m, base, syms))
                    kb = (3 * 8192 + (H // 4 - 1) * 90 + 90 + 1023) // 1024
                    print("   1: onto the screen: the keeper %d KB (the "
                          "canvas; it was 32), the writes %s"
                          % (rb("vp_kkb"), "CHECKED" if rb("vp_rchk")
                             else "not walked"))
                    if rb("vp_kkb") != kb or rb("vp_rchk"):
                        bad.append("1: the keeper %d KB (want %d), walked %d"
                                   % (rb("vp_kkb"), kb, rb("vp_rchk")))
                    bseg = rw("vp_rblk") << 4
                    got = bytes(m.read(bseg, len(blk))) if bseg else b""
                    d = sum(1 for x, y in zip(got, blk) if x != y) + \
                        abs(len(got) - len(blk))
                    print("   1: the block in its claim at %05x: %d of %d "
                          "bytes differ from the host's" % (bseg, d,
                                                            len(blk)))
                    if d or not bseg:
                        bad.append("1: the block in memory differs in %d "
                                   "bytes" % d)
                    dg = vid.Geom(vid.LAY_HERC, 90, 348)
                    for i, n in enumerate(stops):
                        wait(lambda mm: rb("vp_held") == 1 and
                             rw("vp_done") == n, "the hold at %d" % n)
                        ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                        seg = bytes(m.read(0xB0000, 65536))
                        got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                           dg.base[ty0 + y] + tx0 + WB]
                                       for y in range(H))
                        want = vid.decode_at(r, n - 1)
                        d = sum(1 for x, y in zip(got, want) if x != y)
                        print("   1: the hold before frame %d: %d bytes of "
                              "%d differ" % (n, d, len(got)))
                        if d:
                            bad.append("1: frame %d differs in %d bytes"
                                       % (n - 1, d))
                        ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                           else 0xFFFF)
                        m.write(base + syms["vp_held"], b"\0")
                except Stop as e:
                    bad.append("1: %s" % e)

        if a.leg in ("refuses", "all"):
            v88 = os.path.join(tmp, "HUGE.V88")
            st = clip(v88, 175, 2)
            (ulen, plen), = st["blocks"]
            kb = (ulen + 2 * 2048 + 1023) // 1024
            print("   2: the file: a %d-byte stored block, a %d KB claim"
                  % (ulen, kb))
            import os88vencgui as G
            cmd, img = G.disk_argv(v88, 6, out=os.path.join(tmp, "H.VHD"),
                                   build=os88build.at("build"))
            for flag, val in zip(("--cyls", "--heads", "--spt"),
                                 ("615", "4", "26")):
                cmd[cmd.index(flag) + 1] = val
            subprocess.run(cmd, check=True, capture_output=True)
            m = os88marty.launch(None, machine=MACHINE,
                                 extra=["--mount", "hd:0:" + img])
            try:
                ui = os88ui.UI(m)
                ui.ready(limit=300)
                m, base = session(ui, "C:/HUGE.V88")
                rb = lambda n: m.read(base + syms[n], 1)[0]
                rw = lambda n: u16(m.read(base + syms[n], 2))
                wait = waiter(m)
                try:
                    wait(lambda mm: rb("vp_loaded") == 1, "the header")
                    m.write(base + syms["vp_played"], b"\0")
                    m.type_text("p")
                    wait(lambda mm: rw("vp_msg") == syms["vp_mbuf"],
                         "the refusal", 120.0)
                    text = msg(m, base, syms)
                    print("   2: Play says %r; the block's claim %04x"
                          % (text, rw("vp_rblk")))
                    want = "Needs %d KB of memory, " % kb
                    if not text.startswith(want) or \
                            not text.endswith(" KB free") or rw("vp_rblk"):
                        bad.append("2: %r (wanted %r... KB free, nothing "
                                   "claimed)" % (text, want))
                except Stop as e:
                    bad.append("2: %s" % e)
            finally:
                m.close()
        if a.leg in ("keeper", "all"):
            keeper(tmp, pkg, syms, waiter, session, bad)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


KW, KH, KN = 20, 60, 24
KBOUND = (KH - 1) * 80 + 80      # LIN80: the last row, a whole stride


def stray(rec, addr):
    """a record with one more write, a byte at `addr`: an absolute P1
    segment in front of its P1 list (98.1.3)"""
    return (struct.pack("<H", len(rec) + 4) + rec[2:6] + b"\x81" +
            struct.pack("<H", addr) + b"\xff" + rec[6:])


def kclip(path, frame=None, key=None):
    """the keeper leg's clip: a resident LIN80 160 x 60, keys every 8
    frames; `frame`/`key` = (index, address) put one stray write there"""
    rnd = random.Random(3)
    g = vid.Geom(vid.LAY_LIN80, KW, KH)
    w = vid.Writer(g, int(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="keep", keysecs=8 / FPS)
    surf = g.surface()
    for f in range(KN):
        ch = []
        for _ in range(60):
            y, x = rnd.randrange(KH), rnd.randrange(KW)
            a = g.base[y] + x
            surf[a] = rnd.randrange(256)
            ch.append(a)
        w.frame(vid.spans(ch, surf, g), surf)
    if frame:
        w.recs[frame[0]] = stray(w.recs[frame[0]], frame[1])
    if key:
        k, rec, cv = w.keys[key[0]]
        w.keys[key[0]] = (k, stray(rec, key[1]), cv)
    vid.write_resident(path, [w], title="keep", pack=vid.PK_LZB)
    return w


def keeper(tmp, pkg, syms, waiter, session, bad):
    import heapmap
    files = {"GOOD": {}, "EDGE": {"frame": (5, KBOUND - 1)},
             "BAD": {"frame": (5, KBOUND)}, "BADKEY": {"key": (1, KBOUND)}}
    for n, kw in files.items():
        kclip(os.path.join(tmp, n + ".V88"), **kw)
    if len(vid.Reader(os.path.join(tmp, "GOOD.V88")).keys) < 2:
        bad.append("3: the clip has fewer than two keys")
        return
    disk = os.path.join(tmp, "keep.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                    "--size", "360", pkg] +
                   [os.path.join(tmp, n + ".V88") for n in files],
                   check=True, capture_output=True)
    r = vid.Reader(os.path.join(tmp, "GOOD.V88"))
    with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                     machine="os8088_5150_herc_gla") as ui:
        for name in files:
            m, base = session(ui, "B:/%s.V88" % name)
            rw = lambda n: u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))
            wait = waiter(m)
            try:
                wait(lambda mm: rb("vp_loaded") == 1, "%s's header" % name)
                if name == "BADKEY":
                    m.key("ArrowRight")
                    wait(lambda mm: rw("vp_sel") == 1, "Right to key 1")
                if name == "GOOD":
                    ww("vp_stopat", 9)
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1 or rb("vp_played") == 1
                     or rw("vp_msg") in (syms["vp_s_bad"],
                                         syms["vp_s_kbad"]),
                     "%s's play to start or refuse" % name, 120.0)
                if name in ("BAD", "BADKEY"):
                    want = "vp_s_bad" if name == "BAD" else "vp_s_kbad"
                    got = rw("vp_msg")
                    print("   3: %s: Play says %r; the keeper %04x, the "
                          "block %04x" % (name, msg(m, base, syms),
                                          rw("vp_keep"), rw("vp_rblk")))
                    if got != syms[want] or rw("vp_keep") or \
                            (name == "BAD" and rw("vp_rblk")):
                        bad.append("3: %s was not refused as %s" % (name,
                                                                    want))
                    continue
                if not rb("vp_ready"):
                    raise Stop("%s did not play: %s" % (name,
                                                        msg(m, base, syms)))
                if name == "GOOD":
                    kseg = rw("vp_keep")
                    hm = heapmap.Map(M(m), {s: m.sym(s) for s in
                                            ("mem_base", "mem_top",
                                             "spl_live", "mem_tab")})
                    c = [c for c in hm.claims if c.seg == kseg]
                    kb = (KBOUND + 1023) // 1024
                    print("   3: GOOD: the keeper %d KB by the player, %s "
                          "by the heap (the bound %d bytes; it was 64 KB)"
                          % (rb("vp_kkb"), "%.1f KB" % c[0].kb if c
                             else "NO claim", KBOUND))
                    if rb("vp_kkb") != kb or not c or c[0].para != kb * 64:
                        bad.append("3: the keeper is not %d KB" % kb)
                    if not rb("vp_rchk"):
                        bad.append("3: GOOD plays through the shadow and "
                                   "its writes were never checked")
                    wait(lambda mm: rb("vp_held") == 1 and
                         rw("vp_done") == 9, "the hold at 9")
                    ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                    seg = bytes(m.read(0xB0000, 65536))
                    dg = vid.Geom(vid.LAY_HERC, 90, 348)
                    got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                       dg.base[ty0 + y] + tx0 + KW]
                                   for y in range(KH))
                    want = vid.decode_at(r, 8)
                    d = sum(1 for x, y in zip(got, want) if x != y)
                    print("   3: GOOD: the hold before frame 9, through the "
                          "5 KB shadow: %d bytes of %d differ" % (d, len(got)))
                    if d:
                        bad.append("3: frame 8 differs in %d bytes" % d)
                    ww("vp_stopat", 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                else:
                    print("   3: EDGE: a write at the bound's last byte "
                          "(%d) plays" % (KBOUND - 1))
                wait(lambda mm: rb("vp_played") == 1, "%s's end" % name)
                wait(lambda mm: rw("vp_keep") == 0, "%s's claims freed"
                     % name)
            except Stop as e:
                bad.append("3: %s" % e)


class M(object):
    """heapmap.Map reads through .read(linear, n), which Marty has"""

    def __init__(self, m):
        self.m = m

    def read(self, linear, n):
        return self.m.read(linear, n)


def msg(m, base, syms):
    """the player's message, as the window shows it"""
    p = u16(m.read(base + syms["vp_msg"], 2))
    t = bytes(m.read(base + p, 64))
    return t.split(b"\0")[0].decode("latin-1").rstrip()


if __name__ == "__main__":
    sys.exit(main())
