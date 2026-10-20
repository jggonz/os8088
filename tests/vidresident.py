#!/usr/bin/env python3
"""VIDEO.O88 plays a RESIDENT file - one rendition per screen, each read
whole and expanded before the play - SPEC.md 98.1.7, VIDEO-PLAN wave 10.

    make && python3 tests/vidresident.py [--screen herc|cga|vga]
                                         [--pack lzb|lz4|none]

ONE FILE, THREE RENDITIONS, made here: 40 frames of a 160 x 60 canvas in
the CGA, Hercules and LIN80 layouts, each drawn with its own pattern so a
rendition played on the wrong screen is a different picture - LZB-packed
by default, with a seam back to frame 10 and Repeat on. On the screen named
(the Hercules 5150, the CGA 5150, or MartyPC's VGA XT):

1. THE RENDITION IS THIS SCREEN'S: the player reads the file as resident
   and takes the rendition whose layout is the desktop's own - played in
   the window, not through the shadow.
2. IT IS IN MEMORY AND NOWHERE ELSE: the block's claim holds the block
   exactly as tools/os88vid.py expands it, and the session has no ring.
3. EVERY FRAME, ACROSS THE SEAM: held before chosen frames over two laps -
   the last frame, the frame after the seam (the screen then frame 10) -
   and each hold's window against that rendition's host decode.
4. REPEAT OFF ENDS IT, at the file's end, with no stall and no error.

Broken on purpose - vp_open taking the first rendition always - it FAILS on
1 on two screens of three; vp_rnext stepping a record short FAILS on 3.
"""
import argparse
import os
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

WB, H, NF, FPS, L = 20, 60, 40, 15.0, 10
REND = ("cga", "herc", "lin80")          # the file's order
SCREEN = {"herc": ("os8088_5150_herc_gla", 0xB0000, "herc", 348, 90),
          "cga": ("os8088_5150_cga_gla", 0xB8000, "cga", 200, 80),
          "vga": ("os8088_xt_vga", 0xA0000, "lin80", 480, 80)}


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def canvases(ri):
    """A pattern of the rendition's own: rendition ri's bar has ri + 1
    lit bytes in every other one, and moves at its own speed"""
    out = []
    for f in range(NF):
        cv = bytearray(WB * H)
        for y in range(H):
            x0 = (f * (ri + 2) + y // 6) % WB
            for x in range(x0, min(WB, x0 + 3 + ri)):
                cv[y * WB + x] = (0xF0, 0x3C, 0x5A)[ri] if (y + f) % 3 \
                    else 0xFF
        out.append(bytes(cv))
    return out


def writer(lay, ri):
    g = vid.Geom(vid.LAYOUT_BY_NAME[lay], WB, H)
    w = vid.Writer(g, int(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="resident", keysecs=100.0, loop=L)
    surf = g.surface()
    for cv in canvases(ri):
        ch = []
        for y, b in enumerate(g.base):
            row = cv[y * WB:(y + 1) * WB]
            for x in range(WB):
                if surf[b + x] != row[x]:
                    ch.append(b + x)
            surf[b:b + WB] = row
        w.frame(vid.spans(ch, surf, g), surf)
    return w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screen", choices=sorted(SCREEN), default="herc")
    ap.add_argument("--pack", choices=("lzb", "lz4", "none"), default="lzb")
    a = ap.parse_args()
    machine, vseg, dlay, rows, stride = SCREEN[a.screen]
    want_r = REND.index(dlay)
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    pk = {"lzb": vid.PK_LZB, "lz4": vid.PK_LZ4, "none": vid.PK_NONE}[a.pack]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = os.path.join(tmp, "RES.V88")
        st = vid.write_resident(v88, [writer(l, i) for i, l in
                                      enumerate(REND)], title="resident",
                                repeat=True, pack=pk)
        vid.verify_v88(v88)
        r = vid.Reader(v88, want_r)
        blk = b"".join(r._recs) + r._seam
        print("   the file: %d bytes, blocks %s (unpacked, packed)"
              % (st["bytes"], st["blocks"]))
        disk = os.path.join(tmp, "res.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            w = ui.path("B:/RES.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n: u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def screen(n, what):
                dg = vid.Geom(vid.LAYOUT_BY_NAME[dlay], stride, rows)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                seg = bytes(m.read(vseg, 65536))
                got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                   dg.base[ty0 + y] + tx0 + WB]
                               for y in range(H))
                want = vid.decode_at(r, n)
                d = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: the window holds frame %d, %d bytes of %d "
                      "differ" % (what, n, d, len(got)))
                if d:
                    bad.append("%s: frame %d differs in %d bytes"
                               % (what, n, d))
            try:
                wait(lambda mm: rb("vp_loaded") == 1, "the header")
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                got1 = (rb("vp_resid"), rb("vp_rend"), rb("vp_nrend"),
                        rb("vp_ok"))
                print("   resident %d, rendition %d of %d (want %d), ok %d"
                      % (got1[0], got1[1], got1[2], want_r, got1[3]))
                if got1 != (1, want_r, 3, 1):
                    bad.append("the player took %s, not rendition %d"
                               % (got1, want_r))
                ui.mo.to(8, rows - 8 if rows < 400 else 470)
                stops = (5, NF, L + 1, 25, NF, L + 1, 20)
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play to start")
                if rb("vp_winm") != 1 or rb("vp_shadow"):
                    bad.append("the play is %s, %s" % (
                        "in the window" if rb("vp_winm") else "FULL SCREEN",
                        "through the SHADOW" if rb("vp_shadow")
                        else "native"))
                # --- 2: the block in its claim, and no ring
                bseg = rw("vp_rblk") << 4
                got = bytes(m.read(bseg, len(blk))) if bseg else b""
                d = sum(1 for x, y in zip(got, blk) if x != y) + \
                    abs(len(got) - len(blk))
                print("   the block: %d bytes at %05x, %d differ from the "
                      "host's; the ring %04x" % (len(blk), bseg, d,
                                                 rw("vp_ring")))
                if d or not bseg or rw("vp_ring"):
                    bad.append("the block in memory differs in %d bytes "
                               "(ring %04x)" % (d, rw("vp_ring")))
                # --- 3: every hold, two laps
                for i, n in enumerate(stops):
                    nxt = stops[i + 1] if i + 1 < len(stops) else 0xFFFF
                    wait(lambda mm: rb("vp_held") == 1 and
                         rw("vp_done") == n, "hold %d, frame %d" % (i, n))
                    screen(n - 1, "hold %d" % i)
                    ww("vp_stopat", nxt)
                    if i + 1 == len(stops):
                        m.write(base + syms["vp_rep"], b"\0")
                    m.write(base + syms["vp_held"], b"\0")
                # --- 4
                wait(lambda mm: rb("vp_played") == 1, "the play to end")
                res = (rw("vp_done"), rw("vp_stall"), rb("vp_err"))
                print("   Repeat off: ended at frame %d, stalls %d, error %d"
                      % res)
                if res != (NF, 0, 0):
                    bad.append("the play ended as %s" % (res,))
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
