#!/usr/bin/env python3
"""THE FULL SCREEN'S KEYS AND ITS TEXT - SPEC.md 98.3.13, 98.3.14.

    make && python3 tests/vidfskeys.py [--kind KIND]

KIND is the screen the text is drawn on, one of vosd.inc's renderers each:

    herc      Hercules, one bit                  the 5150's memory
    cga4      CGA 320 x 200, two bits            the 5150's memory
    c160      CGA's text hack, through the SHADOW - its cells' attributes
    vga8      13h, a byte a pixel                the VGA XT's memory
    modex     Mode X, a plane at a time          the glass (planes are not
    modexflip Mode X, PAGE FLIPPING: a save a page       memory)
    vga4      mode 12h, sixteen colours, four planes saved     the glass

A clip at 15 fps with a keyframe every second or two: a textured ground
that never changes (so a text box put back wrong shows), bars that sweep
through the box's top rows, and - from frame 30 on, written once and never
again - a block under the glyphs, which is what a put-back taken from a
stale save loses for good. It plays FULL SCREEN, held before chosen frames
(vp_stopat), and every check is the screen's canvas against the host's
decode with the box the player should be showing drawn into it from the
kernel's own glyphs - byte for byte in memory, or colour for colour on the
glass.

1. SPACE SAYS PAUSED: the box reads "Paused" over the held frame, and the
   rest of the canvas is the frame.
2. ...AND PUTS IT BACK: Space again, and the canvas is the frame exactly.
3. R SAYS SO, AND THE PLAY GOES ON UNDER IT: "Repeat on" while frames are
   decoded - frame 30 among them - and NEVER OFF THE GLASS: looked at up to
   20 times while it is up, every look shows the text (98.3.13.1: decoded
   round it by vd_clip where the screen is the reference, drawn on a page
   before it is shown when flipping, copied round it through the shadow).
   Then gone, and the next hold is its frame exactly: every write under the
   box went into the save.
4. RIGHT TWICE: the play pauses and the box says where it will land,
   ">> m:ss" - ten seconds on - and when the wait is over the play goes on
   from the keyframe at or before that, IN the bracket: the hold after it
   is that key's stream, exactly. (The wait, half a guest second, is
   held open through vp_skwt while the text is read.)
5. LEFT WHILE SPACE HAS IT PAUSED: "<< m:ss", then paused on the key it
   landed on with "Paused" back over it.
6. SPACE: on from there, and the hold is exact.
7. HOME: the start, at once - key 0 for a colour file, the stream's first
   record for a one-bit one - and on playing: the hold after it is exact.

--cost is an INSTRUMENT, not a question, and gates nothing: every vp_hook
call is timed to the cycle, entry to return, shared among the frames it
drew (a call that catches up draws more than one), for the
same frames (21 to 32) twice - the text down, then R's toast up - and the
two are printed side by side. `--src DIR` runs it on another tree's player
(`git archive HEAD apps | tar -x -C DIR`), so the text's cost is read
against the build before it (SPEC.md 98.3.13.1's table).

Broken on purpose - vc_span storing the box's bytes onto the screen and not
into the save, so the decode goes over the text - question 3 FAILS twice on
vga8: the text off the glass 20 looks in 20, and the hold after it 328 bytes
out; vp_flipdec's vo_flipon call taken out and modexflip's 3 FAILS (17 in
20); vo_bparts copying whole rows and c160's 3 FAILS (20 in 20);
vp_fseek's canvas clear taken out and question 5's does. `--src` runs a
broken copy of the tree without touching this one. Needs MartyPC and the IBM ROM's GLaBIOS
twins.
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

FPS = 15.0
H = 100
YOFF, ROWS = 4, 12                  # vosd.inc's box: 4 rows in, 12 tall
CGAPAL = 0x51
X2 = [sum(3 << (2 * i) for i in range(4) if n >> i & 1) for n in range(16)]

# per kind: layout, the canvas's row (in its own units: bytes, cells or
# pixels), the Geom's bytes a row, pixel format, machine, how the screen is
# read, the box's left in canvas units, frames and seconds a key (a big key
# costs a 360 KB disk: fewer of them), and the values a canvas byte takes
KINDS = {
    "herc": (vid.LAY_HERC, 40, 40, vid.PF_MONO1, "os8088_5150_herc_gla",
             "mem", 1, 240, 1.0, 255),
    "cga4": (vid.LAY_CGA, 40, 40, vid.PF_CGA4, "os8088_5150_cga_gla",
             "mem", 2, 240, 1.0, 255),
    "c160": (vid.LAY_C160, 80, 80, vid.PF_C160, "os8088_5150_cga_gla",
             "mem", 4, 180, 2.0, 255),
    "vga8": (vid.LAY_LIN320, 160, 160, vid.PF_VGA8, "os8088_xt_vga",
             "mem", 8, 180, 2.0, 255),
    "modex": (vid.LAY_MODEX, 160, 40, vid.PF_VGA8, "os8088_xt_vga",
              "glass", 8, 180, 2.0, 255),
    "modexflip": (vid.LAY_MODEX, 160, 40, vid.PF_VGA8, "os8088_xt_vga",
                  "glass", 8, 180, 2.0, 255),
    "vga4": (vid.LAY_LIN80, 160, 20, vid.PF_VGA4, "os8088_xt_vga",
             "glass", 8, 180, 2.0, 15),
}


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def canvases(row, px, nf, vmax):
    """`px`: the canvas is pixels, a bar 8 of them wide - else bytes"""
    out = []
    step = 8 if px else 1
    ground = bytearray(row * H)
    for y in range(H):
        for x in range(row):
            ground[y * row + x] = ((x // step * 37 + y * 11) * 131) & vmax
    for f in range(nf):
        cv = bytearray(ground)
        for y in range(0, 10):
            # two bars a row, sweeping the width every few frames, through
            # the box's top rows (4..9) and its columns
            for c in ((f * 3 + y // 5) * step % row,
                      (f * 5 + 17 + y // 3) * step % row):
                for x in range(c, min(row, c + 2 * step)):
                    cv[y * row + x] = (0xFF if not px else
                                       (f * 7 + y) % 200 + 20) & vmax
        if f >= 30:
            # A BLOCK, from frame 30 on and never again: written ONCE, under
            # the toast's glyphs (rows 10..15). Put back from a save taken
            # before it, it is lost for good - the bars heal themselves
            for y in range(10, 16):
                for x in range(2 * step, 9 * step):
                    cv[y * row + x] = (0x5A if not px else 99) & vmax
        out.append(bytes(cv))
    return out


def palette():
    p = bytearray(768)
    for i in range(1, 256):
        p[3 * i:3 * i + 3] = bytes(((i * 5) % 60 + 3, 63 - (i >> 2),
                                    (i * 23) % 64 | 1))
    return bytes(p)


def cost(hookcost, press, hold, release, ww, rb, rw, span):
    """--cost: the hook's cycles a frame, the text down and then up"""
    hi = 20 + span
    release(hi + 8)
    down = hookcost(21, hi)
    ww("vp_stopat", 20)
    press("Home", lambda mm: rb("vo_skp") == 0 and rw("vp_done") <= 20,
          "Home")
    hold(20, "cost")
    press("KeyR", lambda mm: rb("vo_kind") == 3, "R's toast")
    release(hi + 8)
    up = hookcost(21, hi)
    print("   cost: frame   text down   text up (kind)")
    both = sorted(set(down) & set(up))
    for f in both:
        print("   cost: %5d %11d %9d (%d)" % (f, down[f][0], up[f][0],
                                           up[f][1]))
    both = [f for f in both if up[f][1] == 3]      # (the toast still up)
    if both:
        d = sum(down[f][0] for f in both) / len(both)
        u = sum(up[f][0] for f in both) / len(both)
        print("   cost: mean over %d frames: %.0f down, %.0f up (%+.1f%%), "
              "%.2f / %.2f ms at 4.77 MHz" % (len(both), d, u,
                                              100.0 * (u - d) / d,
                                              d / 4772.727, u / 4772.727))
        x = sorted(up[f][0] - down[f][0] for f in both)
        print("   cost: the text's extra a frame: median %d, max %d"
              % (x[len(x) // 2], x[-1]))
    else:
        print("   cost: no frame timed both ways")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", choices=sorted(KINDS), default="herc")
    ap.add_argument("--machine")
    ap.add_argument("--cost", action="store_true",
                    help="time the hook with the text down and up, and stop")
    ap.add_argument("--sym", default="vp_hook", help="with --cost: the "
                    "routine timed (default the whole hook)")
    ap.add_argument("--clip", help="with --cost: time this .V88 instead of "
                    "the row's own (its screen the KIND's)")
    ap.add_argument("--span", type=int, default=12, help="with --cost: the "
                    "frames timed, from 21")
    ap.add_argument("--src", help="with --cost: a tree whose apps/ to build "
                    "the player from, for the build before")
    a = ap.parse_args()
    (lay, row, gwb, pf, machine, how, xoff, nf, keysecs,
     vmax) = KINDS[a.kind]
    px = a.kind in ("vga8", "modex", "modexflip", "vga4")
    flip = a.kind == "modexflip"
    ink = 15 if a.kind == "vga4" else 255
    os.chdir(ROOT)
    bad = []
    retries = [0]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        if a.src:
            apps = os.path.join(a.src, "apps") + "/"
            binf = os.path.join(tmp, "video.bin")
            pkg = os.path.join(tmp, "VIDEO.O88")
            subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", apps,
                            "-o", binf, apps + "video/video.asm"], check=True)
            subprocess.run([sys.executable, "tools/os88pkg.py", binf, "-o",
                            pkg], check=True, capture_output=True)
            syms, _ = pkg_syms(apps + "video/video.asm", (apps,))
        else:
            syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
            pkg = os88build.at("build/video.o88")
        g = vid.Geom(lay, gwb, H, bitplanes=pf == vid.PF_VGA4)
        clip = os.path.join(tmp, "KEYS.V88")
        vid.encode_canvases(canvases(row, px, nf, vmax), g, clip, FPS, pf,
                            palette() if pf == vid.PF_VGA8 else None, "keys",
                            keysecs=keysecs, flip=flip,
                            cgapal=CGAPAL if pf == vid.PF_CGA4 else None)
        if a.clip:
            clip = os.path.join(tmp, "KEYS.V88")
            with open(a.clip, "rb") as f, open(clip, "wb") as o:
                o.write(f.read())
        vid.verify_v88(clip)
        r = vid.Reader(clip)
        keyf = [e[0] for e in r.keys]
        src = r.palette if pf == vid.PF_VGA8 else vid.STD16
        rgb = [tuple((v * 255 + 31) // 63 for v in src[3 * i:3 * i + 3])
               for i in range(len(src) // 3)]
        disk = os.path.join(tmp, "keys.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, clip], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine or machine) as ui:
            m = ui.m
            w = ui.path("B:/KEYS.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n: u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.2, limit=900.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def glyphs():
                gs, go = rw("vo_gseg"), rw("vo_goff")
                f, l = rb("vo_gfst"), rb("vo_glst")
                t = bytes(m.read((gs << 4) + go, (l - f + 1) * 8))
                return f, l, t

            def box(text, want):
                """the box the player draws, into a canvas image; returns
                the canvas offsets it covers"""
                f, l, t = glyphs()
                where = []
                for rr in range(ROWS):
                    bits = [0]
                    for ch in text:
                        c = ord(ch)
                        gy = rr - 2
                        bits.append(t[(c - f) * 8 + gy]
                                    if 0 <= gy < 8 and f <= c <= l else 0)
                    bits.append(0)
                    lit = [b >> (7 - i) & 1 for b in bits for i in range(8)]
                    if px:
                        out = bytes(ink if v else 0 for v in lit)
                    elif a.kind == "cga4":
                        out = bytes(v for b in bits
                                    for v in (X2[b >> 4], X2[b & 15]))
                    elif a.kind == "c160":
                        out = bytes((0xF0 if lit[2 * c] else 0) |
                                    (0x0F if lit[2 * c + 1] else 0)
                                    for c in range(len(lit) // 2))
                    else:
                        out = bytes(bits)
                    o = (YOFF + rr) * row + xoff
                    want[o:o + len(out)] = out
                    where.extend(range(o, o + len(out)))
                return where

            def boxed(text, kind):
                """up to 20 looks at the box while frames decode under it,
                the machine stopped for each, while the toast is still up:
                (looks, how many did not show the text, vo_clip)"""
                want = bytearray(row * H)
                where = box(text, want)
                miss = looks = 0
                clip = None
                for i in range(20):
                    m.pause()
                    if rb("vo_kind") != kind:
                        m.run()
                        break
                    if clip is None:
                        clip = rb("vo_clip")
                    looks += 1
                    try:
                        if how == "mem":
                            got = mem()
                            bad_ = any(got[j] != want[j] for j in where)
                        else:
                            ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                            fw, fh, img = m.fbuf(0)
                            if a.kind == "vga4":
                                sx, sy, x0 = fw / 640, fh / 480, tx0 * 8
                            else:
                                sx, x0 = fw / 320, tx0 * 4
                                sy = sx
                            bad_ = False
                            for j in where:
                                y, x = divmod(j, row)
                                o = 3 * (int((ty0 + y + 0.5) * sy) * fw +
                                         int((x0 + x + 0.5) * sx))
                                c = rgb[want[j]]
                                if max(abs(img[o + q] - c[q])
                                       for q in range(3)) > 6:
                                    bad_ = True
                                    break
                    finally:
                        m.run()
                    miss += bad_
                    os88marty.pace(m, 0.01)
                return looks, miss, clip

            def mem():
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                if a.kind == "c160":
                    v = bytes(m.read(0xB8000, 16384))
                    return bytearray(v[(ty0 + y) * 160 + (tx0 + x) * 2 + 1]
                                     for y in range(H) for x in range(row))
                seg = bytes(m.read(rw("vp_vseg") << 4, 65536))
                banks, stride, rows, _ = vid.LAYOUTS[lay]
                out = bytearray()
                for y in range(H):
                    sy = ty0 + y
                    b = (sy % banks) * 8192 + (sy // banks) * stride + tx0
                    out += seg[b:b + row]
                return out

            def glass(want):
                """the rendered screen, each canvas pixel at its centre,
                against the colour `want` says; the count that are not"""
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                fw, fh, img = m.fbuf(0)
                if a.kind == "vga4":
                    sx, sy, x0 = fw / 640, fh / 480, tx0 * 8
                else:           # (MartyPC scans Mode X's rows twice each)
                    sx, x0 = fw / 320, tx0 * 4
                    sy = sx
                wrong = 0
                for y in range(H):
                    ry = int((ty0 + y + 0.5) * sy)
                    for x in range(row):
                        o = 3 * (ry * fw + int((x0 + x + 0.5) * sx))
                        c = rgb[want[y * row + x]]
                        if max(abs(img[o + j] - c[j]) for j in range(3)) > 6:
                            wrong += 1
                return wrong

            def check(what, frame, text=None):
                want = bytearray(vid.decode_at(r, frame))
                if text:
                    box(text, want)
                if how == "mem":
                    got = mem()
                    d = sum(1 for x, y in zip(got, want) if x != y)
                    if d and os.environ.get("VFSK_WHERE"):
                        print("      at", [(i // row, i % row, got[i], want[i])
                                          for i in range(len(want))
                                          if got[i] != want[i]][:6])
                else:
                    d = glass(want)
                print("   %s: frame %d%s, %d of %d %s differ"
                      % (what, frame, " with %r" % text if text else "", d,
                         len(want), "bytes" if how == "mem" else "pixels"))
                if d:
                    bad.append("%s: the screen differs from frame %d%s in %d "
                               "places" % (what, frame, " with %r" % text
                                           if text else "", d))

            def press(key, cond, what):
                """a key, and what it does: pressed again, a few guest seconds
                on, if it plainly never arrived - a key made and broken while
                the Mode X flipper had the machine was lost now and then
                (counted and printed, so it cannot hide)"""
                for n in range(3):
                    m.key(key)
                    try:
                        wait(cond, what, 6.0)
                        return
                    except Stop:
                        retries[0] += 1
                raise Stop("%s never happened, pressed three times" % what)

            def hold(n, what):
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                     "%s: the hold before frame %d" % (what, n), 180.0)

            def release(nxt):
                ww("vp_stopat", nxt)
                m.write(base + syms["vp_held"], b"\0")

            def hookcost(lo, hi):
                """{frame: (cycles, the text's kind)}, frames lo..hi: each
                vp_hook call's, shared among the frames it drew"""
                out = {}
                ent = base + syms[a.sym]
                while True:
                    m.bp_exec(ent)
                    m.run()
                    if not m.wait_stop(limit=120.0):
                        raise Stop("vp_hook never ran")
                    d0 = rw("vp_done")
                    if d0 >= hi:
                        break
                    r_ = m.regs()
                    ret = u16(m.read((r_["ss"] << 4) + r_["sp"], 2))
                    m.bp_exec((r_["cs"] << 4) + ret)
                    c0 = m.status()["cycles"]
                    m.run()
                    if not m.wait_stop(limit=120.0):
                        raise Stop("vp_hook never returned")
                    c = m.status()["cycles"] - c0
                    d1 = rw("vp_done")
                    if a.sym != "vp_hook":
                        d1 = d0 + 1
                    for f in range(d0 + 1, d1 + 1):  # (a call that caught
                        if lo <= f <= hi and f not in out:  # up: its cost
                            out[f] = (c // (d1 - d0), rb("vo_kind"))  # shared)
                m.bp_exec()
                m.run()
                return out

            def secs(fr):
                s = fr * r.spf // r.rate
                return "%d:%02d" % (s // 60, s % 60)

            try:
                wait(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
                m.write(base + syms["vp_nowin"], b"\1")
                ww("vp_stopat", 20)
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play")
                print("   full screen: mode %d, the text's renderer %d, %d "
                      "characters fit, shadow %d, flip %d"
                      % (rb("vp_fsmode"), rb("vo_mode"), rb("vo_cmax"),
                         rb("vp_shadow"), rb("vp_flip")))
                if not rb("vo_mode") or rb("vp_winm") or \
                        rb("vp_shadow") != int(a.kind == "c160") or \
                        rb("vp_flip") != int(flip):
                    bad.append("the play is not full screen the way the "
                               "row wants, with the text on")
                hold(20, "start")
                if a.cost:
                    cost(hookcost, press, hold, release, ww, rb, rw, a.span)
                    raise Stop(None)
                check("held", 19)
                # --- 1, 2
                press("Space", lambda mm: rb("vp_upause") == 1 and
                      rb("vo_kind") == 1 and rb("vo_drawn"), "Space's Paused")
                check("1: Space", 19, "Paused")
                press("Space", lambda mm: rb("vp_upause") == 0 and
                      rb("vo_kind") == 0, "Space to resume")
                check("2: Space again", 19)
                # --- 3: R while HELD, so the toast is up before frame 30
                press("KeyR", lambda mm: rb("vo_kind") == 3, "R's toast")
                d3 = rw("vp_done")
                release(80)
                looks, miss, clip = boxed("Repeat on", 3)
                native = a.kind in ("herc", "cga4", "vga8", "modex", "vga4")
                print("   3: the box, looked at %d times as frames decode "
                      "under it: %d without the text (vd_clip %s)"
                      % (looks, miss, clip))
                if miss or looks < 8 or (native and not clip):
                    bad.append("3: the toast left the glass %d times in %d "
                               "looks (vd_clip %s)" % (miss, looks, clip))
                wait(lambda mm: rb("vo_kind") == 0, "the toast to go", 60.0)
                d3b = rw("vp_done")
                print("   3: the toast was up from frame %d to %d" % (d3, d3b))
                if not d3 < 31 <= d3b:
                    bad.append("3: the toast (frames %d to %d) was not up "
                               "when the block under it arrived (frame 30)"
                               % (d3, d3b))
                hold(80, "3")
                check("3: the hold after the toast", 79)
                # --- 4: Right twice, ten seconds on. The wait after the last
                # press is HELD while the text is read (it is half a
                # guest second, and the host cannot see inside that), then
                # let go: vp_skwt is 9 ticks
                ww("vp_stopat", 0xFFFF)
                m.write(base + syms["vp_skwt"], b"\xF0")
                press("ArrowRight", lambda mm: rb("vo_skp") == 1 and
                      rw("vp_skn") == 1, "a Right")
                press("ArrowRight", lambda mm: rb("vo_skp") == 1 and
                      rw("vp_skn") == 2, "two Rights")
                b0 = rw("vp_skb")
                f5 = (5 * r.rate + r.spf // 2) // r.spf
                t = min(r.frames - 1, b0 + 2 * f5)
                k = vid.key_at(r, t)
                if keyf[k] <= b0 and k + 1 < len(keyf):
                    k += 1
                ww("vp_stopat", keyf[k] + 1 + 6)    # before the seek fires
                check("4: the seek's text", b0, ">> " + secs(t))
                m.write(base + syms["vp_skwt"], bytes([9]))
                hold(keyf[k] + 7, "4")
                # (the key's own number is not kept: with Repeat on, the
                # lap's join reads key 0's entry the moment the play goes
                # on - so where it landed is the frame the stream drew first)
                print("   4: from frame %d, target %d: the play went on from "
                      "frame %d - key %d is frame %d"
                      % (b0, t, rw("vp_base"), k, keyf[k]))
                if rw("vp_base") != keyf[k] + 1:
                    bad.append("4: the seek landed at frame %d, not key %d's "
                               "%d" % (rw("vp_base") - 1, k, keyf[k]))
                check("4: the hold after the seek", keyf[k] + 6)
                # --- 5: paused, Left once
                press("Space", lambda mm: rb("vo_kind") == 1 and
                      rb("vo_drawn"), "Paused again")
                m.write(base + syms["vp_skwt"], b"\xF0")
                press("ArrowLeft", lambda mm: rb("vo_skp") == 1, "the Left")
                b1 = rw("vp_skb")
                t = max(0, b1 - f5)
                k = vid.key_at(r, t)
                check("5: the seek's text", b1, "<< " + secs(t))
                m.write(base + syms["vp_skwt"], bytes([9]))
                wait(lambda mm: rb("vo_skp") == 0 and rw("vp_base") ==
                     keyf[k] + 1 and rb("vo_kind") == 1 and rb("vo_drawn"),
                     "the seek back, paused", 60.0)
                if rb("vp_upause") != 1:
                    bad.append("5: a seek made paused did not stay paused")
                check("5: paused on the key", keyf[k], "Paused")
                # --- 6
                ww("vp_stopat", keyf[k] + 1 + 5)
                press("Space", lambda mm: rb("vp_upause") == 0, "Space on")
                hold(keyf[k] + 6, "6")
                check("6: on from the key", keyf[k] + 5)
                # --- 7: Home, from a play held mid-file
                ww("vp_stopat", 6)
                press("Home", lambda mm: rb("vo_skp") == 0 and
                      rw("vp_done") <= 6 and rb("vp_upause") == 0,
                      "Home's jump to the start")
                hold(6, "7")
                print("   7: Home: the play went on from frame %d"
                      % rw("vp_base"))
                check("7: from the start", 5)
                m.key("Escape")
                wait(lambda mm: rb("vp_played") == 1, "Esc", 30.0)
            except Stop as e:
                if e.args[0] is not None:
                    bad.append(str(e))
    if retries[0]:
        print("   (%d key press(es) sent again: the first never arrived)"
              % retries[0])
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
