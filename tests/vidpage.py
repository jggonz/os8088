#!/usr/bin/env python3
"""VIDEO.O88 plays a PAGE-AHEAD file - SPEC.md 98.1.3.7, 98.3.8.3: every
frame drawn in place on the page on the glass, the frames before a scene
change building it on the HIDDEN page, and one flip showing it whole.

    make && python3 tests/vidpage.py [--layout lin80|modex] [--machine NAME]

The clip is made here with the writer itself, so every piece is placed on
purpose rather than left to a budget: 60 frames at 15 fps of a scene of
blocks with a box moving across it, and two SCENE CHANGES (frames 20 and
45) to a picture of new blocks. In the frames before each change, a band of
the new picture's rows a frame goes onto the hidden page, and its last
rows are left for the FLIP record, so the flip frame's own record is drawn
on the hidden page as well. Key 1 (frame 30) falls inside the second
window, so it carries the hidden page's half-built picture (98.1.3.7). On
lin80 it is sixteen colours on the 640 x 400 screen (98.1.3.2.1), on modex
Mode X - the two screens with a second page - each in a palette nothing
like the BIOS's, booted on MartyPC's VGA XT:

1. IS THE FILE TAKEN AS PAGE-AHEAD? `[vp_flip]` 2, the screen and its page.
2. IS EVERY HOLD THE CLIP? Full screen, from the start, held before frames
   either side of each flip: every mode pixel the glass shows against the
   clip's own canvas - not a decode of the file, the picture the clip was
   made to show. Right only if the frames drawn in place, the hidden
   records, the flip record's draw into the hidden page AND the flip all
   work.
3. FROM KEY 1: Right in the Preview picks key 1 and Play starts there - the
   key onto both pages and its hidden record onto the second - and held
   before frames 31, 45 and 46, the glass is the clip, which the flip at 45
   shows only if the key brought the hidden page back.

Broken on purpose: with vp_pgdec's call of vp_show skipped the glass stays
on the old page at every hold after a flip; with vp_pghid a bare ret the
hidden page is never built and the flip shows the picture of two scenes
ago; with vp_decboth's hidden record left out both plays go wrong after a
flip - key 0 carries the first band of the hidden page too, so a play from
the start is a play from a key here.

0. THE ONE A PLAYER MUST REFUSE (the options audit, 2026-10-14), opened
   first: PAGELOOP.V88, the clip LOOPED - which the writer refuses, so the
   loop is set past it - must say it is damaged (its seam went through
   vp_flipdec's copy into a keep page-ahead never claims, segment 0). And
   the play from the start must hold NO KEEPER ([vp_nokeep] 1): out of the
   full screen a keeper's one page cannot bring the hidden page back.
   Broken on purpose - .fp2's flag test out, the keeper block out - each
   FAILS.
4. NO KEYS PLAYS (the owner, 2026-10-14: a broken start is better than a
   video that does not play): PAGENK.V88, the clip with no keys, opens and
   plays from the stream's start, both pages black, and every hold is the
   REFERENCE decode's - which differs from the clip at 4 holds, the first
   flip's missing preload. The audit refused it; the encoder no longer
   makes one (98.2.1.8).

--layer is PAGE-AHEAD WITH A LAYER (98.2.1.8.3): the same clip with a layer
drawing a box over the page shown every frame but key 0's, which a
page-ahead play never draws. Leg 2 only, every hold against the LAYERED
decode on two pages (os88vid.v88_layered), which must differ from the
clip's in every frame; and the player must have drawn layer records and
missed and left out none. Key 0's bench verdict is poked in as kept and
passed, vidpal --layer's way. With vp_frame's vp_lydec after vp_pgdec
taken out the layer is never drawn and every hold FAILS.
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

NF, FPS = 60, 15.0
FLIPS = (20, 45)
STOPS = (18, 19, 20, 21, 22, 44, 45, 46, NF)
KSTOPS = (31, 45, 46)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def palette(n):
    """n colours no BIOS palette has: 0 black, the rest a walk"""
    p = bytearray(768)
    for i in range(1, n):
        p[3 * i:3 * i + 3] = bytes(((i * 5) % 60 + 3, 63 - (i * 4) % 64,
                                    (i * 23) % 64 | 1))
    return bytes(p)


def scene(rnd, W, H, ncol):
    cv = bytearray([rnd.randrange(1, ncol)]) * (W * H)
    for _ in range(40):
        x, y = rnd.randrange(0, W - 32, 8), rnd.randrange(H - 16)
        w, h = rnd.randrange(8, 32, 8), rnd.randrange(4, 16)
        c = rnd.randrange(ncol)
        for r in range(h):
            cv[(y + r) * W + x:(y + r) * W + x + w] = bytes([c]) * w
    return cv


def clip(tmp, layout, loop=None, name="PAGE.V88", layer=False,
         kpart=vid.KEY_PART):
    """(path, the canvases): 98.1.3.7's file, written frame by frame -
    with `loop`, the same file LOOPED, which no writer makes (the seam would
    join pages it does not know): set past the writer's refusal, for the
    player's to be tested"""
    rnd = random.Random(1310)
    if layout == vid.LAY_MODEX:
        W, H, ncol, scr, pf = 160, 96, 256, vid.SCR_480, vid.PF_VGA8
        g = vid.Geom(layout, W // vid.PIX_PER_BYTE[layout], H)
    elif layout == vid.LAY_HERC:        # (one bit: W is BYTES, a canvas's
        W, H, ncol, scr, pf = 40, 144, 256, vid.SCR_480, vid.PF_MONO1
        g = vid.Geom(layout, W, H)      # values eight pixels each)
    else:
        W, H, ncol, scr, pf = 224, 144, 16, vid.SCR_640X400, vid.PF_VGA4
        g = vid.Geom(vid.LAY_LIN80, W // 8, H, bitplanes=True)
    scenes = [scene(rnd, W, H, ncol) for _ in range(len(FLIPS) + 1)]
    cvs = []
    for f in range(NF):
        k = sum(1 for F in FLIPS if f >= F)
        cv = bytearray(scenes[k])
        x, y = (f * 3) % (W - 24), (f * 2) % (H - 16)
        for r in range(16):
            cv[(y + r) * W + x:(y + r) * W + x + 24] = \
                bytes([(f * 7 + 1) % ncol]) * 24
        cvs.append(bytes(cv))

    def box(f):
        """the layer's writes for frame f: a 24 x 12 box over the clip's
        picture, every byte of it written whole (its canvas against one
        where every box pixel differs)"""
        top, bw = bytearray(cvs[f]), 24 if g.planes > 1 else 3
        oth = bytearray(cvs[f])
        x, y = (f * 5 + 16) % (W - bw - 8), (f * 3 + 40) % (H - 14)
        x -= x % (8 if g.bitplanes else 4 if g.planes > 1 else 1)
        for r in range(12):
            for c in range((y + r) * W + x, (y + r) * W + x + bw):
                top[c] = (f * 11 + 5) % ncol if ncol > 2 else 0xA5
                oth[c] = top[c] ^ (15 if ncol == 16 else 0xFF if
                                   ncol == 256 else 0xFF)
        return delta(bytes(oth), bytes(top))

    def delta(cur, new):
        if g.bitplanes:
            return vid.vga4_subs(new, cur, g)
        if g.planes > 1:
            return vid.modex_subs(new, [a != b for a, b in zip(new, cur)], g)
        t = g.surface()
        g.put(t, new)
        return vid.spans([b + x for y, b in enumerate(g.base)
                          for x in range(g.wb)
                          if cur[y * g.wb + x] != new[y * g.wb + x]], t, g)
    rate, spf = round(FPS * 100), 100
    pal = palette(ncol)
    wr = vid.Writer(g, rate, spf, vid.AUD_NONE, 0, pf, title="vidpage clip",
                    keysecs=2.0, palette=None if pf == vid.PF_MONO1 else pal,
                    screen=scr, page=True)
    wr.kpart = kpart
    if loop is not None:
        wr.loop = loop
    if layer:
        # A LAYER (98.2.1.8.3): 40 KB/s, 128 KB of slots, no prefill, no
        # bank, a yardstick no key reaches - vidpal --layer's - and a box
        # drawn on top of the page shown every frame but key 0's, whose
        # layer a page-ahead play never draws
        wr.layer = (40, 128, 0, 0, 40000, 40000)
    vis, hid = g.surface(), g.surface()
    vcv, hcv = g.canvas(vis), g.canvas(hid)
    start = 0
    full = {}
    for f in range(NF):
        if f == 0:                      # THE PRELOAD: key 0's hidden page is
            g.put(hid, cvs[FLIPS[0]])   # the first scene change whole, and
            hcv = cvs[FLIPS[0]]         # no frame before it builds any
        nxt = [F for F in FLIPS if F > f]
        flip = f in FLIPS
        if flip:                        # the pages swap: the record is the
            vis, hid = hid, vis         # rest of the target, drawn on the
            vcv, hcv = hcv, vcv         # page about to be shown
            start = f + 1
        ops = delta(vcv, cvs[f])
        g.put(vis, cvs[f])
        vcv = cvs[f]
        hops = None
        if nxt and not flip:
            # A BAND OF THE NEXT SCENE A FRAME, its last 6 rows left for
            # the flip record
            F = nxt[0]
            span = max(1, F - 1 - start)
            per = -(-(H - 6) // span)
            r0, r1 = (f - start) * per, min(H - 6, (f - start + 1) * per)
            if r0 < r1:
                new = bytearray(hcv)
                new[r0 * W:r1 * W] = cvs[F][r0 * W:r1 * W]
                new = bytes(new)
                hops = delta(hcv, new)
                g.put(hid, new)
                hcv = new
        wr.frame(ops, vis, hidden=hops, flip=flip, hsurf=hid)
        if layer:
            wr.layer_frame(box(f) if f else [])
        if flip:
            full[f] = bytes(vis)
    # ...and key 1, inside the second window, holds ITS flip whole rather
    # than the half the stream has built by then (98.2.1.8's upgrade)
    k, rec, cv = wr.keys[1]
    vs = g.surface()
    g.put(vs, cv)
    wr.keys[1] = (k, vid.key_record(vid.keyframe_ops(vs, g), g,
                                    vid.delta_ops(full[FLIPS[1]], vs, g),
                                    True, 0, kpart), cv)
    out = os.path.join(tmp, name)
    wr.write(out, 1)
    if loop is None:
        vid.verify_v88(out)
    return out, cvs, W, H, pal


def hostile(tmp, layout):
    """PAGELOOP.V88 is page-ahead and LOOPED - its seam was copied into a
    keep page-ahead never claims, segment 0, and the player must refuse it
    (the options audit, 2026-10-14) - and PAGENK.V88 is page-ahead with NO
    KEYS, so no key 0 to preload the hidden page from: it must PLAY"""
    lp = clip(tmp, layout, loop=10, name="PAGELOOP.V88")[0]
    d = bytearray(open(clip(tmp, layout, name="PAGENK.V88")[0], "rb").read())
    struct.pack_into("<HH", d, 192 + 12, 0, 0xFFFF)  # (the slot's key count
                                                     # and poster, video.asm
                                                     # R_NKEYS and R_POSTER)
    nk = os.path.join(tmp, "PAGENK.V88")
    open(nk, "wb").write(d)
    return lp, nk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine")
    ap.add_argument("--layout", choices=("lin80", "modex", "herc"),
                    default="lin80")
    ap.add_argument("--layer", action="store_true",
                    help="the clip with a LAYER: every hold the layered "
                    "decode on two pages (98.2.1.8.3)")
    ap.add_argument("--kpart", type=int, default=vid.KEY_PART,
                    help="a key's read at most (98.1.3.8): a small one cuts "
                    "each key - its hidden record with it - into KEY PARTS")
    ap.add_argument("--refused", action="store_true",
                    help="herc: on a machine with a colour card beside the "
                    "Hercules, the play must REFUSE (98.3.8.3)")
    a = ap.parse_args()
    os.chdir(ROOT)
    lay = vid.LAYOUT_BY_NAME[a.layout]
    modex = lay == vid.LAY_MODEX
    herc = lay == vid.LAY_HERC
    if a.machine is None:
        a.machine = ("os8088_5150_both_gla_mono" if a.refused else
                     "os8088_5150_herc_gla") if herc else "os8088_xt_vga"
    SW, SH = (320, 240) if modex else (720, 348) if herc else (640, 400)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88, cvs, W, H, pal = clip(tmp, lay, layer=a.layer,
                                   kpart=a.kpart)
        ref_cvs = cvs
        if a.layer:                     # (the layered decode on two pages;
            r_ = vid.Reader(v88)        # it must differ from the clip's)
            ref_cvs = [bytes(r_.g.canvas(s_)) for f_, s_ in
                       vid.v88_layered(r_)]
            same = sum(1 for f_ in range(1, NF) if ref_cvs[f_] == cvs[f_])
            print("   layer: %d records; the layered picture is the clip's "
                  "in %d of %d frames after key 0" % (
                      sum(1 for x in r_.layer_records() if x[1] is not None),
                      same, NF - 1))
            if same:
                bad.append("the layer leaves %d frames as the base's" % same)
        r = vid.Reader(v88)
        nflip = sum(1 for rec, _, _ in r.records()
                    if u16(rec, 2) & vid.REC_FLIP)
        nhid = sum(1 for rec, _, _ in r.records()
                   if u16(rec, 4) & vid.REC_HID)
        k1 = r.key(1)
        k1p = vid.key_split(k1[1])      # (98.1.3.8: its parts, or itself)
        k1h = any(u16(p, 4) & vid.REC_HID for p in k1p)
        print("   %s: %d flips, %d hidden records, key 1 at frame %d %s a "
              "hidden record%s" % (a.layout, nflip, nhid, k1[0],
                                   "with" if k1h else "WITHOUT",
                                   ", in %d parts" % len(k1p)
                                   if len(k1p) > 1 else ""))
        if nflip != len(FLIPS) or not nhid or not k1h or \
                (a.kpart < vid.KEY_PART and len(k1p) < 2):
            bad.append("the clip does not test what it is for")
        pal8 = [tuple((v * 255 + 31) // 63 for v in pal[3 * i:3 * i + 3])
                for i in range(256)]
        if modex:
            ty0, tx0 = (SH - H) // 2, (SW - W) // 2
        elif herc:                      # (the player's origin, SPEC.md
            ty0 = (SH - H) // 2 // 4 * 4    # 98.1.2: whole banks, bytes)
            tx0 = (90 - W) // 2 * 8
        else:
            ty0, tx0 = (SH - H) // 2, ((SW - W) // 16) * 8
        disk = os.path.join(tmp, "vidpage.img")
        lp, nk = hostile(tmp, lay)
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88, lp, nk], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            # --- 0: THE ONE THE PLAYER MUST REFUSE, at its open
            for fn, want in (("PAGELOOP.V88", "vp_s_bad"),):
                wh = ui.path("B:/" + fn)
                rh = m.read(ui._S("wm_wins") + wh.i * geom.WIN_SIZE,
                            geom.WIN_SIZE)
                bh = u16(rh, geom.W_SEG) << 4
                # (DONE - loaded, or the card opened - as vidpal's hostile
                # leg waits: vp_parse says "damaged" while it runs)
                os88marty.until(
                    m, lambda mm: m.read(bh + syms["vp_loaded"], 1)[0] == 1
                    or m.read(bh + syms["vp_card"], 1)[0] == 1,
                    "%s's open" % fn, poll=0.3, limit=300.0, guest=60.0)
                okh = m.read(bh + syms["vp_ok"], 1)[0]
                msg = u16(m.read(bh + syms["vp_msg"], 2))
                said = next((k for k in ("vp_s_bad",)
                             if syms[k] == msg), "%04x" % msg)
                print("   %s: ok %d, says %s" % (fn, okh, said))
                if okh or said != want:
                    bad.append("%s was not refused with %s" % (fn, want))
                ui.close(wh)            # (its window covers the drive's zone)
            w = ui.path("B:/PAGE.V88")
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
            if a.layer:                 # (key 0's verdict, kept and passed:
                m.write(base + syms["vp_lycv"], b"\1")     # vidpal's)
            until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
            # --- 1
            got = (rb("vp_flip"), rb("vp_screen"), rw("vp_page"))
            want = (2, 0 if modex or herc else vid.SCR_640X400,
                    19200 if modex else 0x8000 if herc else SH * 80)
            print("   flip %d, screen %d, page %d" % got)
            if got != want:
                bad.append("the player read it as %r, not %r" % (got, want))

            ap_ = {}

            refs = {}

            def glass(n, what):
                ref = refs[n - 1] if refs else ref_cvs[n - 1]
                fw, fh, rgb = m.fbuf(0)
                if herc:                # ONE BIT: lit or not - at the
                    def miss(dx, dy, step):     # offset MartyPC's aperture
                        n = 0                   # crops the 720 x 348 by
                        for y in range(0, H, step):
                            for x in range(0, W * 8, step):
                                o = 3 * ((ty0 + y + dy) * fw + tx0 + x + dx)
                                lit = sum(rgb[o:o + 3]) > 3 * 96
                                n += lit != bool(ref[y * W + x // 8] >>
                                                 (7 - x % 8) & 1)
                        return n
                    if "off" not in ap_:    # (found once, on the first hold,
                        ap_["off"] = min(   # which is page 0's)
                            ((dx, dy) for dx in range(-24, 25)
                             for dy in range(-4, 5)),
                            key=lambda d: miss(d[0], d[1], 3))
                    wrong = miss(ap_["off"][0], ap_["off"][1], 1)
                    print("   %s, hold before frame %2d (page %d shown): %d "
                          "of %d canvas pixels wrong" % (
                              what, n, 1 if rw("vp_foff") else 0, wrong,
                              W * 8 * H))
                    if wrong:
                        bad.append("%s: the glass before frame %d is wrong "
                                   "in %d pixels" % (what, n, wrong))
                    return
                sx, sy = fw / SW, fh / SH
                if sy < sx * 0.99:      # (Mode X's 480-line retime, which
                    sy = sx             # MartyPC scans into 400)
                shown = min(SH, int(fh / sy))
                wrong = 0
                for y in range(ty0, min(shown, ty0 + H)):
                    ry = int((y + 0.5) * sy)
                    for x in range(tx0, tx0 + W):
                        v = ref[(y - ty0) * W + x - tx0]
                        o = 3 * (ry * fw + int((x + 0.5) * sx))
                        c = pal8[v]
                        if max(abs(rgb[o + j] - c[j]) for j in range(3)) > 6:
                            wrong += 1
                print("   %s, hold before frame %2d (page %d shown): %d of "
                      "%d canvas pixels wrong" % (
                          what, n, 1 if rw("vp_foff") else 0, wrong,
                          W * (min(shown, ty0 + H) - ty0)))
                if ty0 + H > shown:
                    bad.append("the canvas runs past the %d rows the glass "
                               "shows" % shown)
                if wrong:
                    bad.append("%s: the glass before frame %d is wrong in %d "
                               "pixels" % (what, n, wrong))

            def play(stops, what):
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.write(base + syms["vp_nowin"], b"\1")
                m.type_text("p")
                for si, n in enumerate(stops):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "hold before frame %d" % n,
                          240.0)
                    glass(n, what)
                    ww("vp_stopat", stops[si + 1] if si + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                until(lambda mm: rb("vp_played") == 1, "the play's end",
                      120.0)
                if rb("vp_err"):
                    bad.append("%s: the play ended in an error" % what)
            if a.refused:               # --- a colour card beside it
                ww("vp_stopat", 0xFFFF)
                m.write(base + syms["vp_played"], b"\0")
                m.write(base + syms["vp_nowin"], b"\1")
                m.type_text("p")
                until(lambda mm: rb("vp_err") or rb("vp_played"),
                      "the play's refusal", 120.0)
                msg = rw("vp_errmsg")
                print("   two cards: error %d, message %s" % (
                    rb("vp_err"), "vp_s_hpage" if msg == syms["vp_s_hpage"]
                    else "%04x" % msg))
                if not rb("vp_err") or msg != syms["vp_s_hpage"]:
                    bad.append("with a colour card in the machine the play "
                               "was not refused for the Hercules page")
                for b in bad:
                    print("   FAIL: %s" % b)
                if not bad:
                    print("   ok")
                return 1 if bad else 0
            # --- 2
            play(STOPS, "from the start")
            if a.layer:
                ly = (rw("vp_lygot"), rw("vp_lymis"), rw("vp_lyskp"))
                print("   layer drew %d, %d missed, %d left out" % ly)
                if not ly[0] or ly[1] or ly[2]:
                    bad.append("the layer drew %d, %d missed, %d left out"
                               % ly)
                for b_ in bad:
                    print("   FAIL: %s" % b_)
                if not bad:
                    print("   ok")
                return 1 if bad else 0
            # (no keeper: a page-ahead play's hidden page is half a frame
            # to come, so out of the full screen it stops at the key at or
            # before rather than coming back to a keeper's one page)
            nk_ = (rb("vp_nokeep"), rw("vp_keep"))
            print("   the keeper: nokeep %d, keep %04x" % nk_)
            if nk_ != (1, 0):
                bad.append("a page-ahead play kept a keeper: nokeep %d, "
                           "keep %04x" % nk_)
            # --- 3: Right picks key 1, and Play starts there
            n0 = rw("vp_ploads")
            m.key("ArrowRight")
            until(lambda mm: rw("vp_ploads") > n0, "Right to load a key",
                  60.0)
            if rw("vp_sel") != 1:
                bad.append("Right picked key %d, not 1" % rw("vp_sel"))
            else:
                play(KSTOPS, "from key 1")
            # --- 4: NO KEYS (the owner, 2026-10-14: a broken start is
            # better than a video that does not play): PAGENK.V88 opens and
            # plays from the stream's start with both pages BLACK, so the
            # first flip's missing preload is black where the reference
            # decode has it black - and every hold is that decode, exactly
            ui.close(w)
            w = ui.path("B:/PAGENK.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            until(lambda mm: rb("vp_loaded") == 1 or rb("vp_card") == 1,
                  "PAGENK.V88's open", 60.0)
            print("   PAGENK.V88: ok %d, %d keys" % (rb("vp_ok"),
                                                    rw("vp_nkeys")))
            if not rb("vp_ok"):
                bad.append("PAGENK.V88, page-ahead with no keys, was refused")
            else:
                rn = vid.Reader(nk)
                for n in STOPS:
                    refs[n - 1] = vid.decode_at(rn, n - 1)
                broken = sum(1 for n in STOPS if refs[n - 1] != cvs[n - 1])
                print("   PAGENK.V88: the reference differs from the clip "
                      "at %d of %d holds (no preload)" % (broken, len(STOPS)))
                if not broken:
                    bad.append("PAGENK.V88's reference is the clip: the leg "
                               "tests nothing")
                play(STOPS, "no keys, from the start")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
