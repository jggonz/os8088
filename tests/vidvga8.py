#!/usr/bin/env python3
"""VIDEO.O88 plays 256 colours in mode 13h - SPEC.md 98.1.2's LIN320,
VIDEO-PLAN wave 11a - and in Mode X, MODEX's planes (98.1.3.1, wave 11b).

    make && python3 tests/vidvga8.py [--layout lin320|modex] [--rows2]
                                     [--machine NAME]

THE CLIP IS MADE HERE: 60 frames of a 160 x 96 VGA8 canvas at 15 fps,
silent - boxes of colour that move, a band of one value and bursts of noise,
so the lists are all exercised - with a palette nothing like the BIOS's, so
a DAC that was never loaded shows. On MartyPC's VGA XT, four questions:

1. IS THE FILE TAKEN AS VGA8? The player's own reading: the format, the
   layout, mode 13h (FSXM_VGA13), and not the shadow.
2. IS THE POSTER THE LUMA? The Preview's one-bit picture, read out of its
   claim, must equal tools/os88vid.py's vga8_mono of the poster keyframe -
   the player's vp_v8mono bit for bit.
3. IS EVERY FRAME RIGHT, IN ITS COLOURS? (And the play started from KEY 0,
   whose entry it had to load itself, the poster being key 1: 98.2.9.) Held before chosen frames, the
   screen's bytes at the centred origin must equal the reference decode, and
   the RENDERED frame's canvas pixels must all be colours of the file's
   palette.
4. IS IT ON TIME? A second play, whole: every frame, no stall, no late
   period, 60 frames in 4 s of ticks.

--layout modex makes the same clip in Mode X: the frames are sub-records
under a Map Mask, four-pixel groups of one colour under 0Fh. Mode X is four
planes behind the Graphics Controller and the debug read sees one, so 3
reads the RENDERED frame instead, every mode pixel on the glass against the
reference decode put through the palette - the bytes and the DAC in one
comparison. MartyPC does not model Mode X's retime to 480 lines: it scans
the 240 rows twice each into 400, so rows 200 to 239 are off its glass and
the row checks only the 200 it shows (the canvas is inside them). The clip must hold 0Fh sub-records and plane ones,
or it tests half the decoder.

--flip makes the Mode X clip PAGE-FLIPPED (98.3.8): the player draws the
page that is not showing and points the CRTC at it. The glass at each hold
is only right if both halves work - a draw into the back page that is never
shown leaves the glass a frame behind, alternately - and the holds are an
odd and an even number of frames apart. Broken on purpose (vp_show's OUTs
skipped) it FAILS. Its frame 0 is BLACK but for a box, so a keyframe that
writes no black byte lands on whatever the pages held: the first flipped
build zeroed the 31 KB record copy at the keeper's 75 KB instead of the
keeper - 45 KB of heap overrun - and put the uncleared keeper on both pages,
which a frame 0 of one flat colour hid, and this FAILS it by 5,976 pixels.

--repeat seam|key plays it REPEATING (98.3.9), held before chosen frames
across two laps: `seam` gives the clip a seam back to frame 20 and Repeat
on from its flag; `key` gives it none and presses R, so a lap joins through
keyframe 0 over a cleared canvas. With --flip both run through the page
logic: the seam is a frame drawn into the back page like any other, and the
key is decoded into BOTH pages with no last record owed. Broken on purpose
(vp_cclear not zeroing the keeper) the key arm FAILS at the hold after the
join.

--rows2 makes the clip at half its rows with a ROW SCALE of 2 (98.2.4):
the player sets the CRTC to show each row twice, so the picture keeps its
size, and 3 reads the glass on either layout - the rows only come out right
if the register was written. The poster is the luma of the rows shown, each
twice.

Broken on purpose - vp_dac skipped - the rendered colours are the BIOS's and
it FAILS on 3; with the luma threshold's compare flipped it FAILS on 2; for
modex, with the Map Mask OUT skipped, the plane writes land in all four
planes and 3 FAILS.
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

W, H, NF, FPS = 160, 96, 60, 15.0
STOPS = (1, 9, 23, 38, 52, NF)
LOOP = 20
FSXM_VGA13 = 6


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def palette():
    """256 colours no BIOS palette has: index 0 black, the rest a walk"""
    p = bytearray(768)
    for i in range(1, 256):
        p[3 * i:3 * i + 3] = bytes(((i * 5) % 60 + 3, 63 - (i >> 2),
                                    (i * 23) % 64 | 1))
    return bytes(p)


def clip(tmp, layout, rs=1, flip=False, loop=None, black0=False):
    rnd = random.Random(1311)
    g = vid.Geom(layout, W // vid.PIX_PER_BYTE[layout], H)
    cvs, cv = [], bytearray(W * H)
    for f in range(NF):
        k = f % 20
        if k == 0 and f == 0 and black0:
            # frame 0 BLACK but for a box: keyframe 0 writes no black byte,
            # so a join through it is only right over a cleared canvas
            cv = bytearray(W * H)
            for y in range(10, 40):
                cv[y * W + 20:y * W + 60] = bytes([77]) * 40
        elif k == 0:
            cv = bytearray([1 + (f // 20) * 40]) * (W * H)
        elif k in (6, 7):
            for _ in range(300):
                a = rnd.randrange(W * H - 16)
                cv[a:a + 16] = bytes(rnd.randrange(256) for _ in range(16))
        elif k == 12:
            for y in range(20, 70):
                cv[y * W + 10:y * W + 150] = bytes([200]) * 140
        else:
            x, y = (f * 7) % (W - 24), (f * 5) % (H - 20)
            for r in range(20):
                cv[(y + r) * W + x:(y + r) * W + x + 24] = \
                    bytes([(f * 13 + r) & 0xFF]) * 24
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "COLOR.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_VGA8, palette(),
                        "vidvga8 clip", keysecs=2.0, poster=1, rowscale=rs,
                        flip=flip, loop=loop, repeat=loop is not None)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--layout", choices=("lin320", "modex"),
                    default="lin320")
    ap.add_argument("--rows2", action="store_true")
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--repeat", choices=("seam", "key"))
    a = ap.parse_args()
    rs = 2 if a.rows2 else 1
    global H
    H //= rs
    lay = vid.LAYOUT_BY_NAME[a.layout]
    modex = lay == vid.LAY_MODEX
    mode_want = 8 if modex else FSXM_VGA13
    SW, SH = (320, 240) if modex else (320, 200)
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    pal8 = [tuple((v * 255 + 31) // 63 for v in palette()[3 * i:3 * i + 3])
            for i in range(256)]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, lay, rs, a.flip,
                   LOOP if a.repeat == "seam" else None,
                   bool(a.repeat) or a.flip)
        r = vid.Reader(v88)
        g = r.g
        if modex:                       # both halves of the decoder
            masks = set()
            for rec, _, _ in r.records():
                si = 6
                while rec[si]:
                    masks.add(rec[si])
                    si = vid.walk_lists(bytearray(65536), rec, si + 1)
            print("   the clip's Map Masks: %s" % sorted(masks))
            if 0x0F not in masks or not masks & {3, 12} or \
                    not masks & {1, 2, 4, 8}:
                bad.append("the clip holds Map Masks %s: it does not test "
                           "the 0Fh, the pair and the plane sub-records"
                           % sorted(masks))
        disk = os.path.join(tmp, "vidvga8.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        ty0, tx0 = (SH // rs - H) // 2, (SW - W) // 2
        rows_at = [(ty0 + y) * 320 + tx0 for y in range(H)]
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/COLOR.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def until(cond, what, guest):
                os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                guest=guest)

            until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
            until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
            # --- 1
            got1 = (rb("vp_pixfmt") + 1, rb("vp_layout") + 1, rb("vp_mode"),
                    rb("vp_shadow"), rb("vp_ok"))
            if rb("vp_flip") != int(a.flip):
                bad.append("the player's flip is %d" % rb("vp_flip"))
            print("   format %d, layout %d, mode %d, shadow %d, ok %d"
                  % got1)
            if got1 != (vid.PF_VGA8, lay, mode_want, 0, 1):
                bad.append("the player read the file as format %d layout %d "
                           "mode %d shadow %d ok %d" % got1)
            # --- 2: the poster, key 1, at its own size
            k = r.key(1)
            surf = g.surface()
            r.apply(surf, k[1], key=True)
            cvp = g.canvas(surf)            # the rows SHOWN, each rs times
            cvp = b"".join(cvp[y // rs * W:(y // rs + 1) * W]
                           for y in range(H * rs))
            want = vid.vga8_mono(cvp, W, H * rs, r.palette)
            pseg = rw("vp_pseg") << 4
            ps, pbw, prows = rw("vp_ps"), rw("vp_pbw"), rw("vp_prows")
            got = bytes(m.read(pseg, pbw * prows)) if pseg else b""
            d = sum(1 for x, y in zip(got, want) if x != y) + \
                abs(len(got) - len(want))
            print("   the poster: scale %d, %d x %d bytes, %d of %d differ "
                  "from vga8_mono" % (ps, pbw, prows, d, len(want)))
            if ps != 1 or d:
                bad.append("the poster is not the luma dither (scale %d, %d "
                           "bytes differ)" % (ps, d))
            def glass(n):
                """98.1.3.1: the rendered frame, each MODE pixel sampled at
                its centre, against the decode through the palette"""
                want = vid.decode_at(r, n - 1)
                fw, fh, rgb = m.fbuf(0)
                if os.environ.get("VIDVGA8_PNG"):
                    from PIL import Image
                    Image.frombytes("RGB", (fw, fh), bytes(rgb)).save(
                        os.path.join(os.environ["VIDVGA8_PNG"],
                                     "glass%02d.png" % n))
                # MARTYPC SCANS MODE X'S 240 ROWS TWICE EACH INTO 400 LINES:
                # it does not model the retime to 480, so rows 200..239 are
                # off its glass. The canvas here (rows 72..167) is not
                sx = fw / SW
                sy = sx * rs                # a row scale shows each twice
                shown = min(SH // rs, int(fh / sy))
                wrong = 0
                for y in range(shown):
                    ry = int((y + 0.5) * sy)
                    for x in range(SW):
                        cy, cx = y - ty0, x - tx0
                        v = want[cy * W + cx] if 0 <= cy < H and \
                            0 <= cx < W else 0
                        o = 3 * (ry * fw + int((x + 0.5) * sx))
                        if max(abs(rgb[o + j] - pal8[v][j])
                               for j in range(3)) > 6:
                            wrong += 1
                print("   hold before frame %2d: %d of %d mode pixels on the "
                      "glass are not the decode's colour (%dx%d rendered, "
                      "%d rows shown)" % (n, wrong, SW * shown, fw, fh, shown))
                if ty0 + H > shown:
                    bad.append("the canvas runs past the %d rows the glass "
                               "shows" % shown)
                if wrong:
                    bad.append("the glass before frame %d is wrong in %d "
                               "pixels" % (n, wrong))

            # --- 3: every frame right, in its colours
            m.write(base + syms["vp_nowin"], b"\1")
            stops = STOPS
            if a.repeat:                # two laps, held across each join
                j = LOOP + 1 if a.repeat == "seam" else 1
                stops = (1, 9, NF, j, 23, NF, j, 30)
                if a.repeat == "key":
                    m.type_text("r")
                until(lambda mm: rb("vp_rep") == 1, "Repeat on", 10.0)
            ww("vp_stopat", stops[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            # THE RING GETS WHAT THE SESSION LEAVES: on this 640 KB machine
            # it is all eight slots - a keeper claimed four times its size
            # (the KB taken as 16 paragraphs) left two, and a real clip then
            # paused whole every half second on the owner's 286
            until(lambda mm: rb("vp_ready") == 1 or rb("vp_played") == 1,
                  "the play to start", 60.0)
            # ...FROM KEY 0 (98.2.9, 98.3.5): the poster is key 1, and a
            # colour play from the start has to take key 0's entry itself -
            # it fell back to the stream's first record while key 1's was
            # the one loaded, which is where a pre-roll's paint shows
            print("   the play took key %d's entry (want 0: the poster is "
                  "key 1)" % rw("vp_kload"))
            if rw("vp_kload") != 0:
                bad.append("a colour play from the start used key %d's "
                           "entry, not key 0's" % rw("vp_kload"))
            print("   the ring: %d slots" % rw("vp_k"))
            if rw("vp_k") < 8:              # (SPEC.md 98.3: as many as fit
                bad.append("the ring has %d slots, under 8: something the "
                           "session claims is too big" % rw("vp_k"))
            for si, n in enumerate(stops):
                until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                      "hold %d, before frame %d" % (si, n), 120.0)
                nxt = stops[si + 1] if si + 1 < len(stops) else 0xFFFF
                last = si + 1 == len(stops)
                if modex or rs > 1:
                    glass(n)
                    ww("vp_stopat", nxt)
                    if last:
                        m.write(base + syms["vp_rep"], b"\0")
                    m.write(base + syms["vp_held"], b"\0")
                    continue
                seg = bytes(m.read(0xA0000, 64000))
                got = b"".join(seg[b:b + W] for b in rows_at)
                want = vid.decode_at(r, n - 1)
                diff = sum(1 for i in range(len(got)) if got[i] != want[i])
                fw, fh, rgb = m.fbuf(0)
                cols = {}
                for i in range(0, len(rgb), 3):
                    c = tuple(rgb[i:i + 3])
                    cols[c] = cols.get(c, 0) + 1
                used = {pal8[v] for v in set(want)}
                off = sum(c for col, c in cols.items() if col != (0, 0, 0)
                          and not any(max(abs(col[j] - p[j])
                                          for j in range(3)) <= 6
                                      for p in used))
                lit = sum(c for col, c in cols.items() if col != (0, 0, 0))
                print("   hold before frame %2d: %d bytes of %d differ; %d of "
                      "%d lit rendered pixels not the file's colours"
                      % (n, diff, len(got), off, lit))
                if diff:
                    bad.append("the screen before frame %d differs in %d "
                               "bytes" % (n, diff))
                if not lit or off > lit // 100:
                    bad.append("before frame %d, %d of %d rendered pixels are "
                               "not the file's palette" % (n, off, lit))
                ww("vp_stopat", nxt)
                if last:                    # the laps done: it ends now
                    m.write(base + syms["vp_rep"], b"\0")
                m.write(base + syms["vp_held"], b"\0")
            until(lambda mm: rb("vp_played") == 1, "the first play", 60.0)
            done1 = rw("vp_done")
            # --- 4: on time
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            until(lambda mm: rb("vp_played") == 1, "the second play", 60.0)
            done2, stall, late, dt, err = (rw("vp_done"), rw("vp_stall"),
                                           rw("vp_late"), rw("vp_dt"),
                                           rb("vp_err"))
    want_t = NF / FPS * 1193182 / 65536
    print("   the first play drew %d; the second drew %d, stalls %d, late %d, "
          "%d ticks (want %.1f), error %d"
          % (done1, done2, stall, late, dt, want_t, err))
    if done1 != NF or done2 != NF or err:
        bad.append("the plays drew %d and %d of %d (error %d)"
                   % (done1, done2, NF, err))
    if stall or late or abs(dt - want_t) > 2:
        bad.append("%d stalls, %d late, %d ticks for %.1f s"
                   % (stall, late, dt, NF / FPS))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
