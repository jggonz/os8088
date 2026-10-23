#!/usr/bin/env python3
"""VIDEO.O88 plays a VERSION 2 file - SPEC.md 98.1.10's blocks, through the
block decoder part (98.3.23).

    make && python3 tests/vidv2.py [--layout lin320|modex|vga4|cga|herc]
                                   [--screen herc] [--enc MODE] [--refuse]
                                   [--machine NAME]

THE CLIP IS MADE HERE, so the row needs no samples: 160 x 96, 40 frames at
15 fps, silent. Its EVEN frames are version 1's records (boxes that move,
noise) and its ODD frames are BLOCKS ONLY - 150 of them at random places,
every kind the layout has (LIN320's FILL, TWO and EIGHT lists; MODEX's 20h,
21h and 22h leads; VGA4's PFILL and TWO; one bit's TILES, with the whole
codebook at frame 0 and after every key and a slice of it in the rest), so
a frame cannot come out right unless every kind does. On MartyPC's XT VGA,
or its CGA and Hercules 5150s for one bit:

1. IS IT TAKEN? Version 2 read, the block decoder FETCHED at launch and
   its segment banked, and the file playable.
2. IS EVERY FRAME RIGHT, full screen? Held before chosen frames - after
   block frames and version 1 ones - the screen against tools/os88vid.py's
   decode: LIN320's bytes in mode 13h and one bit's in B800h/B000h, read
   straight out of memory; MODEX and VGA4 off the RENDERED frame through
   the palette, the planes being behind the Graphics Controller.
3. ROUND THE TEXT BOX: Space pauses with "Paused" up (vo_clip 1), three
   frame steps (98.3.14.1) decode block records ROUND the box - every byte
   through the part's callback, the box's own into its save - and Space
   again takes the box down. The next hold must be the decode to the byte,
   under where the box was too.
4. IS THE PART MOVABLE? Its claim in the kernel's map (mem_tab) carries a
   relocation proc on the desktop, after the load and after the play, and
   none - pinned - at a hold inside the bracket, where the hook calls into
   it at interrupt time (SPEC.md 66, 98.3.23).
5. VGA4 only: IN THE WINDOW (the same-mode bracket, 98.3.7), and with the
   player told the card has NO WRITE MODE 3 ([vp_m3ok] 0 - an EGA's path,
   a plane at a time, which a VGA takes as well), both held and compared.

--screen herc plays the CGA clip on a Hercules, THROUGH THE SHADOW (98.3.2):
the tiles decoded into the file's own layout at the shadow's 0, which is
what the holds read.

--enc MODE is 98.1.10.1's table on the machine: a 4-second test card
ENCODED by tools/os88venc.py with --blocks on and one feature - page-ahead
on Mode X (pgmx) and on a Hercules (hpa), flipped every frame (flmx), three
pages and the page pool on VGA4 (pg4), a seam with Repeat on LIN320
(lp320: held in lap 1, straight after the seam and on), a layer (ly4) -
played full screen and held against the decode. Needs ffmpeg.

--refuse boots a CGA XT: the part fetched there too, a one-bit version 2
file playable, and one with a seam (its flag set by hand - the encoder
will not make one) refused in version 1's words.

Broken on purpose - the part's skip counted a byte where a block is four
(vb_walk), its Set/Reset b given a's colour (vb_v2c), vp_bcb writing
nothing, the shadow's origin left at the screen's (vp_blin1) or a CGA's
banks read as one (vp_bset) - it FAILS, each at the leg it names.
tests/vblkemu.py holds the part to the reference in seconds and with no
machine; this is the player around it - the fetch, the parse, the
dispatch, the box, the bracket.
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

MEM_MAX = 32                            # (kernel/memory.inc, heapcheck's)

W, H, NF, FPS = 160, 96, 40, 15.0
STOPS = (2, 4, 11, 24, 33)
STEP_AT = 16                            # the box's leg: paused before it
NBLK = 150


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def palette():
    p = bytearray(768)
    for i in range(1, 256):
        p[3 * i:3 * i + 3] = bytes(((i * 5) % 60 + 3, 63 - (i >> 2),
                                    (i * 23) % 64 | 1))
    return bytes(p)


def blocks(rnd, g, lay):
    """NBLK random blocks over the whole canvas, every kind, none sharing
    a byte: [(top address, payload, kind or lead)]"""
    step = 4 if lay == vid.LAY_LIN320 else 1
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4)
            for x in range(0, g.wb - step + 1, step)]
    out = []
    for a in rnd.sample(tops, NBLK):
        if lay == vid.LAY_LIN80:
            if rnd.random() < 0.5:
                out.append((a, bytes([rnd.randrange(256), rnd.randrange(17)]),
                            vid.BLK_PFILL))
            else:
                out.append((a, bytes(rnd.randrange(256) for _ in range(5)),
                            vid.BLK_TWO))
            continue
        k = rnd.randrange(3)
        n = (1, 4, 10)[k]
        pay = bytes(rnd.randrange(256) for _ in range(n))
        if lay == vid.LAY_LIN320:
            out.append((a, pay, ("fill8", "two8", "eight8")[k]))
        else:
            out.append((a, pay, 0x20 + k))
    return out


def put_blocks(surf, g, bl, lay):
    for a, pay, k in bl:
        rows = vid.blk_rows(g, a)
        if lay == vid.LAY_LIN80:
            for r, pb in zip(rows, vid.vga4_block(k, pay)):
                for p in range(4):
                    surf[p * vid.PLANE + r] = pb[p]
            continue
        kind = k if lay == vid.LAY_LIN320 else vid.MODEX_LEADS[k]
        px = vid.v8_block(kind, pay)
        for r, ra in enumerate(rows):
            for x in range(4):
                if lay == vid.LAY_LIN320:
                    surf[ra + x] = px[4 * r + x]
                else:
                    surf[x * vid.PLANE + ra] = px[4 * r + x]


def clip(tmp, lay):
    rnd = random.Random(2010 + lay)
    pf = vid.PF_VGA4 if lay == vid.LAY_LIN80 else vid.PF_VGA8
    g = vid.Geom(lay, W // vid.PIX_PER_BYTE[lay], H,
                 bitplanes=lay == vid.LAY_LIN80)
    g.v2 = True
    wr = vid.Writer(g, round(FPS * 100), 100, vid.AUD_NONE, 0, pf,
                    title="vidv2 clip", keysecs=2.0,
                    palette=palette() if pf == vid.PF_VGA8 else None)
    wr.v2 = True
    surf = g.surface()
    for f in range(NF):
        prev = g.canvas(surf)
        if f % 2:                           # BLOCKS ONLY
            bl = blocks(rnd, g, lay)
            put_blocks(surf, g, bl, lay)
            if lay == vid.LAY_LIN320:
                ops = bl
            else:
                ops = [(lead, [(a, p, False) for a, p, k in bl if k == lead])
                       for lead in sorted({k for a, p, k in bl})]
            wr.frame(ops, surf)
            continue
        cv = bytearray(prev)                # version 1: a box, and noise
        ncol = 16 if pf == vid.PF_VGA4 else 256
        x, y = (f * 7) % (W - 40), (f * 5) % (H - 30)
        for r in range(30):
            cv[(y + r) * W + x:(y + r) * W + x + 40] = \
                bytes([(f * 13 + r) % ncol]) * 40
        for _ in range(40):
            a = rnd.randrange(W * H - 8)
            cv[a:a + 8] = bytes(rnd.randrange(ncol) for _ in range(8))
        cv = bytes(cv)
        if g.planes > 1:
            ops = vid.vga4_subs(cv, prev, g) if g.bitplanes else \
                vid.modex_subs(cv, [p != q for p, q in zip(cv, prev)], g)
            g.put(surf, cv)
        else:
            new = g.surface()
            g.put(new, cv)
            ops = [(g.base[yy] + xx, bytes([new[g.base[yy] + xx]]), False)
                   for yy in range(H) for xx in range(g.wb)
                   if new[g.base[yy] + xx] != surf[g.base[yy] + xx]]
            surf = new
        wr.frame(ops, surf)
    out = os.path.join(tmp, "V2CLIP.V88")
    wr.write(out, 1)
    vid.verify_v88(out)
    return out


def clip1(tmp, lay):
    """THE ONE-BIT CLIP (98.1.10): even frames version 1's bytes, odd ones a
    codebook and NBLK tiles - the whole codebook at frame 0 and in the
    record after every key, a slice of it in the rest, so a tile reads
    entries from both"""
    rnd = random.Random(3010 + lay)
    g = vid.Geom(lay, W // 8, H)
    g.v2 = True
    wr = vid.Writer(g, round(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                    title="vidv2 one bit", keysecs=2.0)
    wr.v2 = True
    surf = g.surface()
    cb = bytearray(rnd.randrange(256) for _ in range(2048))
    tops = [g.base[y] + x for y in range(0, g.h - 3, 4)
            for x in range(0, g.wb - 1, 2)]
    for f in range(NF):
        full = f == 0 or (f - 1) % wr.keyint == 0
        if f % 2 or full:
            if full:
                first, ent = 0, bytes(cb)
            else:
                first = rnd.randrange(256)
                n = rnd.randrange(1, min(32, 256 - first) + 1)
                ent = bytes(rnd.randrange(256) for _ in range(8 * n))
                cb[first * 8:first * 8 + len(ent)] = ent
            ops = [(first, ent, "cbook")]
            for a in rnd.sample(tops, NBLK):
                i = rnd.randrange(256)
                for r, ra in enumerate(vid.blk_rows(g, a)):
                    surf[ra:ra + 2] = cb[i * 8 + 2 * r:i * 8 + 2 * r + 2]
                ops.append((a, bytes([i]), "tile"))
            wr.frame(ops, surf)
            continue
        ops = []                            # version 1: bytes, anywhere
        for a in rnd.sample([g.base[y] + x for y in range(H)
                             for x in range(g.wb)], 300):
            v = rnd.randrange(256)
            if v != surf[a]:
                surf[a] = v
                ops.append((a, bytes([v]), False))
        wr.frame(sorted(ops), surf)
    out = os.path.join(tmp, "V2CLIP.V88")
    wr.write(out, 1)
    vid.verify_v88(out)
    return out


# --enc MODE (SPEC.md 98.1.10.1): a clip ENCODED by tools/os88venc.py with
# --blocks on and the feature the mode names - the encoder, the file and the
# player together, the way an owner makes one. (layout, machine, screen, its
# arguments, whether the full screen's text is clipped round in it)
VGA = "os8088_xt_vga"
ENC = {
    "pgmx": (vid.LAY_MODEX, VGA, (320, 240), True,
             ("--layout", "modex", "--pixfmt", "vga8", "--box", "320x240",
              "--page-ahead", "on", "--disk", "20000")),
    "flmx": (vid.LAY_MODEX, VGA, (320, 240), False,
             ("--layout", "modex", "--pixfmt", "vga8", "--box", "320x240",
              "--flip", "--disk", "20000")),
    "pg4": (vid.LAY_LIN80, VGA, (320, 240), True,
            ("--layout", "lin80", "--pixfmt", "vga4", "--screen", "320x240",
             "--box", "320x240", "--page-ahead", "on", "--pages", "3",
             "--disk", "15000")),
    "lp320": (vid.LAY_LIN320, VGA, (320, 200), True,
              ("--layout", "lin320", "--pixfmt", "vga8", "--box", "320x200",
               "--loop-from", "1", "--repeat", "--disk", "20000")),
    "ly4": (vid.LAY_LIN80, VGA, (640, 480), True,
            ("--layout", "lin80", "--pixfmt", "vga4", "--box", "320x240",
             "--layer-disk", "40000", "--disk", "15000")),
    "hpa": (vid.LAY_HERC, "os8088_5150_herc_gla", (720, 348), True,
            ("--preset", "herc", "--page-ahead", "on", "--disk", "4000")),
}


def encclip(tmp, args):
    """a 4-second test card through the encoder, version 2, verified"""
    src = os.path.join(tmp, "src.mp4")
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", "testsrc2=size=320x240:rate=15:duration=4",
                    src], check=True)
    out = os.path.join(tmp, "V2CLIP.V88")
    subprocess.run([sys.executable, "tools/os88venc.py", src, out,
                    "--blocks", os.environ.get("OS88_V2_BLOCKS", "on"),
                    "--audio", "none"] + list(args),
                   check=True, capture_output=True)
    vid.verify_v88(out)
    return out


def onebit(tmp):
    """a one-bit version 2 file: a codebook and a tile (98.1.10)"""
    g = vid.Geom(vid.LAY_CGA, 80, 200)
    g.v2 = True
    wr = vid.Writer(g, 1500, 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                    title="vidv2 one bit")
    wr.v2 = True
    surf = g.surface()
    cb = bytes(range(8)) * 256
    for r in range(4):
        surf[g.base[r]:g.base[r] + 2] = cb[2 * r:2 * r + 2]
    wr.frame([(0, cb, "cbook"), (g.base[0], b"\x00", "tile")], surf)
    out = os.path.join(tmp, "V2BIT.V88")
    wr.write(out)
    vid.verify_v88(out)
    return out


def refuse(machine):
    """--refuse, on a CGA XT: the part FETCHED there too (every screen plays
    one bit), a one-bit version 2 file playable, and one with a seam
    (98.1.10.1, which the encoder will not make - its LOOPREC flag set by
    hand) refused in version 1's words"""
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        bit = onebit(tmp)
        seam = os.path.join(tmp, "V2SEAM.V88")
        d = bytearray(open(bit, "rb").read())
        d[6] |= 2                           # (V88F_LOOPREC)
        open(seam, "wb").write(d)
        files = (("V2BIT.V88", bit, 1, "vp_s_ready"),
                 ("V2SEAM.V88", seam, 0, "vp_s_ver"))
        disk = os.path.join(tmp, "vidv2no.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg] + [f for _, f, _, _ in files],
                       check=True, capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            for name, _, ok, want in files:
                w = ui.path("B:/" + name)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = u16(rec, geom.W_SEG) << 4

                def rw(n):
                    return u16(m.read(base + syms[n], 2))

                os88marty.until(m, lambda mm: m.read(base + syms["vp_loaded"],
                                                     1)[0] == 1 or
                                rw("vp_msg") == syms["vp_s_ver"], "the open",
                                poll=0.3, limit=600.0, guest=30.0)
                os88marty.pace(m, 1.0)
                got = (rw("vp_bseg") != 0, m.read(base + syms["vp_ok"],
                                                  1)[0], rw("vp_msg"))
                print("   %s: block decoder here %d, playable %d, message %s"
                      % (name, got[0], got[1],
                         next((k for k, v in syms.items() if v == got[2]
                               and k.startswith("vp_s_")), "%04x" % got[2])))
                if got[:2] != (True, ok) or (not ok and got[2] != syms[want]):
                    bad.append("%s: want the decoder, playable %d%s" % (
                        name, ok, "" if ok else " and " + want))
                ui.close(w)                 # (it covers drive B's zone)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--layout", choices=("lin320", "modex", "vga4", "cga",
                                         "herc"), default="lin320")
    ap.add_argument("--screen", choices=("same", "herc"), default="same",
                    help="herc: a CGA file on a Hercules, through the shadow")
    ap.add_argument("--enc", choices=sorted(ENC),
                    help="an ENCODED clip with blocks and this feature "
                         "(98.1.10.1): page-ahead, flipped, N pages and "
                         "the pool, a seam, a layer, a Hercules's pages")
    ap.add_argument("--refuse", action="store_true",
                    help="a CGA XT: the part fetched, one bit played and "
                         "one bit's seam refused")
    a = ap.parse_args()
    if a.refuse:
        return refuse(a.machine if a.machine != "os8088_xt_vga" else
                      "os8088_5150_cga_gla")
    lay = {"lin320": vid.LAY_LIN320, "modex": vid.LAY_MODEX,
           "vga4": vid.LAY_LIN80, "cga": vid.LAY_CGA,
           "herc": vid.LAY_HERC}[a.layout]
    enc = ENC.get(a.enc)
    if enc:
        lay, a.machine = enc[0], enc[1]
    v4 = lay == vid.LAY_LIN80
    one = lay in vid.ONEBIT_LAYOUTS     # ONE BIT: on its own adapter, or
    shadow = a.screen == "herc"         # a CGA file through the shadow
    noclip = shadow or bool(enc and not enc[3])     # (a flipped file is
    loop = a.enc == "lp320"             # never decoded round the box)
    if one and a.machine == "os8088_xt_vga":
        a.machine = "os8088_5150_herc_gla" if lay == vid.LAY_HERC or shadow \
            else "os8088_5150_cga_gla"
    SW, SH = {vid.LAY_LIN320: (320, 200), vid.LAY_MODEX: (320, 240),
              vid.LAY_LIN80: (640, 480), vid.LAY_CGA: (640, 200),
              vid.LAY_HERC: (720, 348)}[lay] if not enc else enc[2]
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    pal = vid.STD16 if v4 else palette()
    ncol = 16 if v4 else 256
    pal8 = [tuple((v * 255 + 31) // 63 for v in pal[3 * i:3 * i + 3])
            for i in range(ncol)]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = encclip(tmp, enc[4]) if enc else \
            clip1(tmp, lay) if one else clip(tmp, lay)
        r = vid.Reader(v88)
        CW, CH = len(vid.decode_at(r, 0)) // r.g.h, r.g.h
        if enc and r.palette:           # (an encoded file's own colours)
            pal8 = [tuple((v * 255 + 31) // 63
                          for v in r.palette[3 * i:3 * i + 3])
                    for i in range(ncol)]
        if enc:
            print("   %s: %d frames, %d x %d, version %d" % (
                a.enc, r.frames, CW * (8 if one else 1), CH, 2 if r.v2
                else 1))
        disk = os.path.join(tmp, "vidv2.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        ty0, tx0 = (SH - CH) // 2, (SW - CW) // 2
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/V2CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def until(cond, what, guest=120.0):
                os88marty.until(m, cond, what, poll=0.3, limit=900.0,
                                guest=guest)

            def check(n, what, x0, y0):
                """the screen before frame n against the decode of n - 1"""
                want = vid.decode_at(r, n - 1)
                if enc:                     # (where the player put it)
                    x0 = rw("vp_tx0") * {vid.LAY_LIN320: 1,
                                         vid.LAY_MODEX: 4}.get(lay, 8)
                    y0 = rw("vp_ty0")
                if one:                     # ONE BIT: its bytes, where the
                    if shadow:              # decode put them - the shadow's
                        lin, org = rw("vp_shseg") << 4, 0   # own 0, or the
                    else:                   # page on the glass at the origin
                        lin, org = rw("vp_vseg") << 4, \
                            rw("vp_org") + rw("vp_foff")
                    got = b"".join(bytes(m.read(lin + ((org + r.g.base[yy])
                                                       & 0xFFFF), CW))
                                   for yy in range(CH))
                    wrong = sum(bin(p ^ q).count("1")
                                for p, q in zip(got, want))
                elif lay == vid.LAY_LIN320:     # (always the full screen)
                    seg = bytes(m.read(0xA0000, 64000))
                    got = b"".join(seg[(y0 + yy) * 320 + x0:
                                       (y0 + yy) * 320 + x0 + CW]
                                   for yy in range(CH))
                    wrong = sum(1 for p, q in zip(got, want) if p != q)
                else:
                    fw, fh, rgb = m.fbuf(0)
                    sx, sy = fw / SW, fh / SH
                    if sy < sx * 0.99:      # (a 480-line retime MartyPC
                        sy = sx             # scans into 400: vidpage's)
                    wrong = 0
                    for yy in range(CH):
                        ry = int((y0 + yy + 0.5) * sy)
                        if ry >= fh:        # (MartyPC's Mode X: 200 rows
                            continue        # of 240 on its glass)
                        for xx in range(CW):
                            o = 3 * (ry * fw + int((x0 + xx + 0.5) * sx))
                            c = pal8[want[yy * CW + xx]]
                            if max(abs(rgb[o + j] - c[j])
                                   for j in range(3)) > 6:
                                wrong += 1
                print("   %s, before frame %2d: %d of %d pixels not the "
                      "decode's" % (what, n, wrong, CW * CH *
                                    (8 if one else 1)))
                if wrong:
                    bad.append("%s: before frame %d, %d pixels wrong"
                               % (what, n, wrong))

            until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
            # --- 1
            got = (rb("vp_v2"), rw("vp_bseg") != 0, rb("vp_ok"),
                   rb("vp_pixfmt") + 1, rb("vp_layout") + 1)
            print("   version 2 %d, block decoder here %d, playable %d, "
                  "format %d, layout %d" % got)
            if got != (1, True, 1, r.pixfmt if enc else vid.PF_MONO1
                       if one else vid.PF_VGA4 if v4 else vid.PF_VGA8, lay):
                bad.append("the player took the file as %r" % (got,))
            if shadow and rb("vp_shadow") != 1:
                bad.append("the CGA file is not played through the shadow")

            def rloc(when, want):
                """the part's claim's relocation proc: 0 = pinned"""
                raw = m.read(ui._S("mem_tab"), MEM_MAX * geom.MC_SIZE)
                seg = rw("vp_bseg")
                for i in range(MEM_MAX):
                    r = raw[i * geom.MC_SIZE:(i + 1) * geom.MC_SIZE]
                    if u16(r, 0) == seg:
                        p = u16(r, 8)
                        print("   the part's claim %s: %s" % (
                            when, "movable (proc %04x)" % p if p
                            else "pinned"))
                        if bool(p) != want:
                            bad.append("the part's claim %s is %s" % (
                                when, "movable" if p else "pinned"))
                        return
                bad.append("no claim at the part's segment %04x %s"
                           % (seg, when))

            rloc("after the load", True)

            def play(stops, where, nowin=1, steps=False, step_at=STEP_AT):
                m.write(base + syms["vp_nowin"], bytes([nowin]))
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                until(lambda mm: rb("vp_ready") == 1 or rb("vp_played") == 1,
                      "the play to start")
                if not nowin and rb("vp_winm") != 1:
                    bad.append("the play did not take the window")
                for i, n in enumerate(stops):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "the hold before frame %d" % n)
                    if i == 0:
                        rloc("in the bracket", False)
                    if not nowin:
                        check(n, where, rw("vp_tx0") * 8, rw("vp_ty0"))
                    else:
                        check(n, where, tx0, ty0)
                    if steps and n == step_at:
                        boxleg(n, stops[i + 1] if i + 1 < len(stops)
                               else 0xFFFF)
                        continue
                    ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                if loop:                    # (Repeat off: on to the end)
                    m.type_text("r")
                    until(lambda mm: rb("vp_rep") == 0, "R, Repeat off",
                          30.0)
                until(lambda mm: rb("vp_played") == 1, "the play to end")
                if a.enc == "ly4":          # (whether this machine took the
                    print("   the layer: this play %s it" % (   # layer)
                        "read" if rb("vp_lyon") else "did not read"))
                if rb("vp_err"):
                    bad.append("%s: the play ended in an error" % where)
                os88marty.pace(m, 1.0)
                rloc("after the play", True)

            def boxleg(n, nxt):
                """3: paused, the text up, three steps round it"""
                ww("vp_stopat", n + 3)
                m.type_text(" ")            # Space: paused, "Paused" up -
                until(lambda mm: rb("vp_upause") == 1, "the pause")
                m.write(base + syms["vp_held"], b"\0")     # ...THEN let go
                if not noclip:              # (THROUGH THE SHADOW the box
                    until(lambda mm: rb("vo_clip") == 1, "the box up",
                          30.0)             # is the copy's: the decode
                else:                       # goes into the shadow whole)
                    os88marty.pace(m, 1.0)
                # THREE STEPS, as many presses as it takes: a key is lost
                # now and then, and one held while the machine is busy can
                # repeat (vidfskeys' note) - [vp_stopat] at n + 3 is what
                # makes that safe, the hook holding there however many land
                for t in range(12):
                    was = rw("vp_done")
                    if was >= n + 3:
                        break
                    m.key("Period")
                    try:
                        until(lambda mm, was=was: rw("vp_done") > was and
                              rw("vp_stepq") == 0, "a frame step", 6.0)
                    except os88marty.MartyError:
                        pass
                if rw("vp_done") != n + 3:
                    bad.append("the steps reached frame %d, not %d"
                               % (rw("vp_done"), n + 3))
                if rb("vo_clip") != (0 if noclip else 1):
                    bad.append("the box was %s for the steps" % (
                        "clipping" if noclip else "not up"))
                m.type_text(" ")            # ...on: the box down
                until(lambda mm: rb("vp_held") == 1 and
                      rw("vp_done") == n + 3, "the hold after the steps")
                if not noclip:
                    until(lambda mm: rb("vo_clip") == 0, "the box down",
                          30.0)
                os88marty.pace(m, 0.5)
                check(n + 3, "after three steps round the box", tx0, ty0)
                ww("vp_stopat", nxt)
                m.write(base + syms["vp_held"], b"\0")

            # --- 2 and 3: full screen, and the box
            if loop:                        # A SEAM (98.1.1.2): lap 1, its
                L = r.loop[0]               # last frame, straight after the
                play((5, r.frames, L + 1, L + 9), "round the seam")  # seam
            elif enc:                       # (a paged play draws its first
                play((18, 24, 33, 44, 55), "full screen",   # frames before
                     steps=True, step_at=24)    # its hook holds: vidpage's
            else:                               # holds start at 18 too)
                play(tuple(sorted(set(STOPS + (STEP_AT,)))), "full screen",
                     steps=True)
            if v4 and not enc:
                # --- 4: in the window, and with no write mode 3
                play(STOPS, "in the window", nowin=0)
                m.write(base + syms["vp_m3gate"], b"\0")   # (and what it
                m.write(base + syms["vp_m3ok"], b"\0")     # gates, now)
                play(STOPS, "with no write mode 3")
                if rb("vp_m3ok") != 0:
                    bad.append("[vp_m3ok] came back %d: the plane-at-a-time "
                               "arm was not the one played" % rb("vp_m3ok"))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
