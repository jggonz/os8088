#!/usr/bin/env python3
"""VIDEO.O88 plays a PALETTE A SCENE - SPEC.md 98.1.3.2.3: a sixteen-colour
file whose table names the frame each scene starts at and its sixteen, which
the player loads into the DAC before decoding that frame.

    make && python3 tests/vidpal.py [--machine NAME]

The clip is made here: 224 x 144, 45 frames at 15 fps on the desktop's
640 x 480, three SCENES starting at frames 0, 10 and 25 - each boxes of
every pixel value that move, and a burst of noise - in three palettes
nothing like each other or the EGA's. Keyframes fall every 15 frames, so key
1 (frame 15) is inside scene 1 and key 2 (frame 30) inside scene 2: a key
is never a scene's first frame. Booted on MartyPC's VGA XT:

1. FROM THE START, WITHOUT FORCING THE FULL SCREEN (a file in its own
   colours refuses the window, 98.1.3.2.2): held before frames either side
   of each scene's first, every mode pixel the glass shows against the
   decode through the palette the table says that frame is in. Only right if
   each scene's sixteen are loaded at its frame - not a frame early, which
   the hold BEFORE a scene's first frame would show, nor late.
2. THE POSTER of key 1, picked in the window: vga4_pack through vga4_xlat of
   the palette ITS scene is in, byte for byte.
3. FROM KEY 1 (a seek): the key itself and frames after it, through the
   palettes the table gives them - the play's palette taken from the table
   at the key and not from the header.
4. HOME IN THE FULL SCREEN (98.3.14): from key 1 to scene 2, then a seek
   to the start inside the play. Only the session's own look at the table
   (vp_spos) puts scene 0's colours back - picking a key in the window does
   it too, and every play from the window starts there, which is why 3
   alone cannot see that look go missing.

Broken on purpose: with vp_palev's call taken out every hold after frame
10 is in scene 0's colours and FAILS; with vp_spos's vp_palat taken out the
hold after Home is in scene 2's colours and FAILS; with vp_pevload's order
test taken out the hostile file is taken and it FAILS.

--layer (98.1.3.2.3.1) is the same clip with a LAYER (98.1.9): its base is
drawn without the fourth box, and the layer draws that box on top every
frame - so the base's picture and the layered one differ at every hold, and
at a scene's first frame the layer's record is drawn in the scene's new
colours, after vp_palev, as the base's is. Every hold is the layered decode
through the table's palette, and the player must have drawn layer records
and left none out. The bench is not under test: key 0's verdict is poked in
as kept and passed ([vp_lycv] 1), vidlyplay's shape, and the file's disk
words are zero. With the player's old refusal put back (`.pev`: a layer's
word non-zero -> CF=1) the player refuses the layered clip at its open
and the run FAILS there, before any hold.

--loop and --repeat0 are THE LAP (the options audit, 2026-10-14), with
Repeat on: held before the end of lap 1, then twice in lap 2 - before
frames 8 and 12, scene 0's sixteen back and scene 1's again. --loop is the
clip with a loop from frame 5, whose seam must take frame 5's colours
(vp_frame .seam's vp_palat; a reader and the player refused the pair
before). --repeat0 makes the lap kind 0 - a file whose keys are past one
read here - which restarts from frame 0 over black; .skey asked vp_palat
for frame FFFFh there, the LAST scene, and lap 2 played in its colours.
Broken on purpose, each FAILS at lap 2's first hold.
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

W, H, NF, FPS = 224, 144, 45, 15.0
LOOP_L = 5                  # --loop: the lap's join, inside scene 0
SCENES = (0, 10, 25)
STOPS = (5, 10, 11, 25, 26, 40, NF)
FSXM_VGA12 = 7


def pal(seed):
    """Sixteen nothing like the EGA's or each other's, black at 0"""
    rnd = random.Random(seed)
    return [(0, 0, 0)] + [(rnd.randrange(64), rnd.randrange(64),
                           rnd.randrange(64)) for _ in range(15)]


PALS = [pal(s) for s in (11, 22, 33)]


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def canvases(boxes):
    """The clip's frames, with `boxes` of the four moving boxes"""
    rnd = random.Random(1613)
    cvs, cv = [], bytearray(W * H)
    for f in range(NF):
        k = f % 9
        if f in SCENES:
            cv = bytearray([(f // 5) % 15 + 1]) * (W * H)
        if k == 4:
            for _ in range(200):
                a = rnd.randrange(W * H - 12)
                cv[a:a + 12] = bytes(rnd.randrange(16) for _ in range(12))
        for b in range(boxes):      # every value, moving
            x, y = (f * 7 + b * 50) % (W - 40), (f * 3 + b * 31) % (H - 32)
            for r in range(32):
                cv[(y + r) * W + x:(y + r) * W + x + 40] = \
                    bytes([(b * 4 + r // 8) % 16]) * 40
        cvs.append(bytes(cv))
    return cvs


def clip(tmp, layer=False, loop=None):
    g = vid.Geom(vid.LAY_LIN80, W // 8, H, bitplanes=True)
    cvs = canvases(3 if layer else 4)
    pb = [bytes(v for c in p for v in c) for p in PALS]
    pev = [(s, p) for s, p in zip(SCENES, pb)]
    pal = pb[0] + bytes(vid.PAL_BYTES - 48)
    out = os.path.join(tmp, "PALSCN.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_VGA4, pal, "vidpal clip",
                        keysecs=1.0, poster=0, pev=pev, loop=loop)
    if layer:
        # THE LAYER (98.1.3.2.3.1): the fourth box drawn on top of the
        # base every frame - its ops worked out over the base's own record
        # applied to the layered screen, which is what the player does, and
        # the whole box written each time
        rb, full = vid.Reader(out), canvases(4)
        surf, lops = g.surface(), []
        for f, (rec, at, i) in enumerate(rb.records()):
            rb.apply(surf, rec, check=False)
            prev = bytearray(g.canvas(surf))
            x, y = (f * 7 + 150) % (W - 40), (f * 3 + 93) % (H - 32)
            for r in range(32):
                for c in range((y + r) * W + x, (y + r) * W + x + 40):
                    prev[c] = full[f][c] ^ 15
            lops.append(vid.vga4_subs(full[f], bytes(prev), g))
            g.put(surf, full[f])
        rate = max(1, round(FPS * 100))
        wr = vid.Writer(g, rate, 100, vid.AUD_NONE, 0, vid.PF_VGA4,
                        title="vidpal clip", keysecs=1.0, palette=pal)
        wr.pev = pev
        # (40 KB/s, 128 KB of slots, no prefill, no bank; a yardstick
        # whose line no key reaches - the bench is not under test)
        wr.layer = (40, 128, 0, 0, 40000, 40000)
        bs, prev = g.surface(), g.canvas(g.surface())
        for f, cv in enumerate(cvs):
            ops = vid.vga4_subs(cv, prev, g)
            g.put(bs, cv)
            prev = cv
            wr.frame(ops, bs)
            wr.layer_frame(lops[f])
        wr.write(out, 0)
    vid.verify_v88(out)
    # ...and a HOSTILE one (98.1.6): the same file with its third scene's
    # frame put before the second's
    d = bytearray(open(out, "rb").read())
    at = struct.unpack_from("<I", d, 192 + vid.R_PEV)[0]
    struct.pack_into("<I", d, at + 2 * vid.PEV_LEN, 8)
    open(os.path.join(tmp, "PALBAD.V88"), "wb").write(d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--layer", action="store_true")
    ap.add_argument("--loop", action="store_true")
    ap.add_argument("--repeat0", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, a.layer, LOOP_L if a.loop else None)
        r = vid.Reader(v88)
        g = r.g
        keys = [e[0] for e in r.keys]
        # what each hold is: the layered decode (98.1.9) where there is a
        # layer - and it must differ from the base's alone, or the check
        # cannot see one
        def from_key(i):
            """{f: the canvas after frame f} for a play from key i: the
            key's BASE picture, then base and layer from its next frame -
            the layer resumes where its key table says (98.1.9), so the
            key's own layer record is never drawn"""
            k, krec, spo, spk, idx = r.key(i)
            surf = g.surface()
            r.apply(surf, krec, key=True)
            out = {k: g.canvas(surf)}
            lit = {lf: lrec for lf, lrec, *_ in r.layer_records(
                at=r.layer_key(i), frame0=k + 1)}
            f = k + 1
            for rec, at, j in r.records(at=spo, nsec=spk, skip=idx):
                r.apply(surf, rec, check=False)
                if lit.get(f) is not None:
                    r.apply(surf, lit[f], check=False, layer=True)
                out[f] = g.canvas(surf)
                f += 1
            return out

        layered = {}
        if a.layer:
            layered = {i: from_key(i) for i in range(len(keys))}
            same = [f for f in range(1, NF) if layered[0][f] ==
                    vid.decode_at(r, f)]
            full = canvases(4)
            if [f for f, s_ in vid.v88_layered(r)
                    if g.canvas(s_) != full[f]]:
                sys.exit("vidpal: the layered decode is not the four boxes")
            lrecs = sum(1 for f, rec, *_ in r.layer_records()
                        if rec is not None)
            print("  layer: %d records, %d bytes; the base's picture and the "
                  "layered one from key 0 alike in %d of %d frames" % (
                      lrecs, len(r.d) - r.lsp0, len(same), NF - 1))
            if same:
                bad.append("the layer leaves frames %s as the base's" % same)

        def ref_at(f, key):
            """the picture after frame f, in a play from key `key`"""
            return layered[keys.index(key)][f] if a.layer else \
                vid.decode_at(r, f)
        print("  scenes at %s, keys at %s" % (
            ", ".join(str(f) for f, _ in r.pev), ", ".join(map(str, keys))))
        if keys[1] in SCENES or keys[2] in SCENES or len(keys) < 3:
            sys.exit("vidpal: the keys %s fell on a scene's first frame"
                     % keys)
        disk = os.path.join(tmp, "vidpal.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88,
                        os.path.join(tmp, "PALBAD.V88")], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            # --- 0: the hostile table refused at open
            wb_ = ui.path("B:/PALBAD.V88")
            recb = m.read(ui._S("wm_wins") + wb_.i * geom.WIN_SIZE,
                          geom.WIN_SIZE)
            bb = u16(recb, geom.W_SEG) << 4
            # (DONE, not the first sight of "damaged": vp_parse sets that
            # as its default while it runs - loaded, or the card opened)
            os88marty.until(m, lambda mm: m.read(bb + syms["vp_loaded"], 1)
                            [0] == 1 or m.read(bb + syms["vp_card"], 1)[0]
                            == 1, "the hostile file's open", poll=0.3,
                            limit=300.0, guest=60.0)
            okb = m.read(bb + syms["vp_loaded"], 1)[0] or \
                u16(m.read(bb + syms["vp_msg"], 2)) != syms["vp_s_bad"]
            print("   PALBAD.V88 (a scene's frame out of order): %s"
                  % ("REFUSED as damaged" if not okb else "taken"))
            if okb:
                bad.append("a palette table out of order was taken")
            w = ui.path("B:/" + os.path.basename(v88))
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def until(cond, what, guest=90.0):
                os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                guest=guest)

            until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
            if a.layer:                 # (key 0's verdict, kept and passed)
                m.write(base + syms["vp_lycv"], b"\1")
            until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
            got = (rb("vp_pixfmt") + 1, rw("vp_pevn"), rb("vp_mode"),
                   rb("vp_ok"))
            print("   format %d, %d palettes, mode %d, ok %d" % got)
            if got != (vid.PF_VGA4, len(SCENES), FSXM_VGA12, 1):
                bad.append("the player read it as %r" % (got,))

            def glass(n, what, key=0):
                """the glass after frame n - 1 against the decode through
                the palette the table gives that frame"""
                ty0, tx0 = (480 - H) // 2, ((640 - W) // 16) * 8
                ref = ref_at(n - 1, key)
                p6 = vid.palette_at(r, n - 1)
                pal8 = [tuple((p6[3 * k + j] * 255 + 31) // 63
                              for j in range(3)) for k in range(16)]
                fw, fh, rgb = m.fbuf(0)
                sx, sy = fw / 640, fh / 480
                wrong, box = 0, [640, 480, -1, -1]
                for y in range(480):
                    ry = int((y + 0.5) * sy)
                    for x in range(640):
                        cy, cx = y - ty0, x - tx0
                        v = ref[cy * W + cx] if 0 <= cy < H and \
                            0 <= cx < W else 0
                        o = 3 * (ry * fw + int((x + 0.5) * sx))
                        c = pal8[v]
                        if max(abs(rgb[o + j] - c[j])
                               for j in range(3)) > 6:
                            wrong += 1
                            box = [min(box[0], x), min(box[1], y),
                                   max(box[2], x), max(box[3], y)]
                print("   %s, hold before frame %2d (scene from %d): %d "
                      "of %d pixels wrong" % (
                          what, n, max(s for s in SCENES if s <= n - 1),
                          wrong, 640 * 480))
                if wrong and os.environ.get("VIDPAL_DEBUG"):
                    import numpy as np
                    gl = np.frombuffer(bytes(rgb), np.uint8).reshape(
                        fh, fw, 3)[[int((y + 0.5) * sy) for y in
                                    range(ty0, ty0 + H)]][:, [
                            int((x + 0.5) * sx) for x in
                            range(tx0, tx0 + W)]].astype(int)
                    best = []
                    for fr in range(NF):
                        cvn = np.frombuffer(bytes(vid.decode_at(r, fr)),
                                            np.uint8).reshape(H, W)
                        for pi, pp in enumerate(PALS):
                            p8 = np.array([[(v * 255 + 31) // 63
                                            for v in c] for c in pp])
                            e = (np.abs(p8[cvn] - gl).max(2) > 6).sum()
                            best.append((e, fr, pi))
                    print("     closest:", sorted(best)[:4])
                if wrong:
                    bad.append("%s: the glass before frame %d is wrong "
                               "in %d pixels, (%d,%d)-(%d,%d); text %d "
                               "drawn %d" % ((what, n, wrong) +
                                             tuple(box) +
                                             (rb("vo_kind"),
                                              rb("vo_drawn"))))

            def holds(stops, what, key=0):
                ty0, tx0 = (480 - H) // 2, ((640 - W) // 16) * 8
                for si, n in enumerate(stops):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "%s: hold before frame %d"
                          % (what, n), 240.0)
                    glass(n, what, key)
                    ww("vp_stopat", stops[si + 1] if si + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                until(lambda mm: rb("vp_played") == 1, "the play's end",
                      60.0)
                if rb("vp_err"):
                    bad.append("%s: the play ended in an error" % what)
                if a.layer:
                    ly = (rw("vp_lygot"), rw("vp_lymis"), rw("vp_lyskp"))
                    print("   %s: layer drew %d, %d missed, %d left out"
                          % ((what,) + ly))
                    if not ly[0] or ly[1] or ly[2]:
                        bad.append("%s: layer drew %d, %d missed, %d left "
                                   "out" % ((what,) + ly))

            if a.loop or a.repeat0:
                # --- THE LAP (options audit, 2026-10-14): Repeat on, a hold
                # just before the end, then two in the next lap - scene 0's
                # sixteen back at the join and scene 1's again at frame 10.
                # --loop: the file's own seam (98.1.1.2), which takes frame
                # L's colours at the join (vp_frame .seam). --repeat0: the
                # lap kind made 0 (a file whose keys are past one read
                # here), which restarts from frame 0 over black and wrapped
                # in the LAST scene's colours (vp_palat of FFFFh)
                kind = rb("vp_lkind")
                if a.repeat0:
                    m.write(base + syms["vp_lkind"], b"\0")
                m.write(base + syms["vp_rep"], b"\1")
                print("   the lap: kind %d%s" % (
                    kind, ", made 0" if a.repeat0 else ""))
                if a.loop and kind != 1:
                    bad.append("the looped clip's lap is kind %d, not its "
                               "seam" % kind)
                lap = ((NF - 1, "lap 1"), (8, "lap 2"), (12, "lap 2"))
                ww("vp_stopat", lap[0][0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                for i, (n, what) in enumerate(lap):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "%s: hold before frame %d"
                          % (what, n), 240.0)
                    glass(n, what)
                    ww("vp_stopat", lap[i + 1][0] if i + 1 < len(lap)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                if rb("vp_err"):
                    bad.append("the lap ended the play in an error")
                for b_ in bad:
                    print("   FAIL: %s" % b_)
                if not bad:
                    print("   ok")
                return 1 if bad else 0
            # --- 1: from the start, the full screen taken by itself
            ww("vp_stopat", STOPS[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            holds(STOPS, "from the start")
            # --- 2: key 1's poster, in its scene's colours
            ui.raise_window(w) if hasattr(ui, "raise_window") else None
            n0 = rw("vp_ploads")
            m.key("ArrowRight")
            until(lambda mm: rw("vp_ploads") > n0, "Right to load key 1")
            if rw("vp_sel") != 1:
                bad.append("Right picked key %d, not 1" % rw("vp_sel"))
            k = r.key(1)
            surf = g.surface()
            r.apply(surf, k[1], key=True)
            ps = rw("vp_ps")
            want, obw, ow, oh = vid.vga4_pack(
                g.canvas(surf), W, H, ps,
                vid.vga4_xlat(vid.palette_at(r, keys[1])))
            gotp = bytes(m.read(rw("vp_pseg") << 4, len(want)))
            d = sum(1 for x, y in zip(gotp, want) if x != y)
            print("   key 1's poster: %d of %d bytes differ from vga4_pack "
                  "through its scene's vga4_xlat" % (d, len(want)))
            if d:
                bad.append("key 1's poster differs in %d bytes" % d)
            # --- 3: from key 1
            k1 = keys[1]
            stops = (k1 + 1, 25, 26, NF)
            ww("vp_stopat", stops[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            holds(stops, "from key 1", k1)
            # --- 4: HOME, in the full screen: from key 1 to scene 2, then a
            # seek to the start inside the play - only the session's own
            # look at the table (vp_spos) puts scene 0's colours back
            ww("vp_stopat", 40)
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 40,
                  "home: the hold before frame 40", 240.0)
            ww("vp_stopat", 6)
            m.key("Home")               # (vidfskeys 7's shape: the jump,
            until(lambda mm: rw("vp_done") <= 6, "Home's jump", 60.0)
            m.write(base + syms["vp_held"], b"\0")     # then the hold)
            print("   Home: the play went on from frame %d" % rw("vp_base"))
            holds((6, NF), "after Home")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
